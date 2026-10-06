#include <workbench/contracts.h>
#include <QtCore/QDateTime>
#include <QtCore/QElapsedTimer>
#include <QtCore/QFile>
#include <QtCore/QRegularExpression>
#include <QtCore/QStringDecoder>
#include <QtCore/QStringList>
#include <QtCore/QVariant>
#include <QtCore/QTimer>
#include <QtNetwork/QHostAddress>
#include <QtNetwork/QTcpServer>
#include <QtNetwork/QTcpSocket>
#include <cmath>
#include <deque>

namespace {
constexpr qsizetype MaxQueuedBytes = 262144;
constexpr size_t MaxQueuedFrames = 256;
constexpr qsizetype MaxReplayRows = 100000;
// Logical rows may contain quoted commas, escaped quotes and quoted newlines.
// Read incrementally so malformed files cannot cause unbounded accumulation.
bool readReplayRow(QFile &file, QList<QByteArray> &fields, bool &eof, QString &detail)
{
    enum class State { Empty, Unquoted, Quoted, Closed } state = State::Empty;
    QByteArray field;
    qsizetype rowBytes = 0;
    char ch;
    while (file.getChar(&ch)) {
        if (++rowBytes > 65536) { detail = QStringLiteral("row exceeds 65536 bytes"); return false; }
        if (state == State::Quoted) {
            if (ch == '"') state = State::Closed;
            else field.append(ch);
            continue;
        }
        if (state == State::Closed && ch == '"') {
            field.append(ch); state = State::Quoted; continue;
        }
        if (ch == ',' || ch == '\n' || ch == '\r') {
            fields.append(field); field.clear(); state = State::Empty;
            if (fields.size() > 6) { detail = QStringLiteral("too many columns"); return false; }
            if (ch == ',') continue;
            if (ch == '\r' && file.peek(1) == "\n") file.getChar(&ch);
            eof = false; return true;
        }
        if (state == State::Closed || (state == State::Unquoted && ch == '"')) {
            detail = QStringLiteral("invalid CSV quoting"); return false;
        }
        if (state == State::Empty && ch == '"') state = State::Quoted;
        else { field.append(ch); state = State::Unquoted; }
    }
    if (file.error() != QFileDevice::NoError) { detail = file.errorString(); return false; }
    if (state == State::Quoted) { detail = QStringLiteral("unterminated CSV quote"); return false; }
    eof = rowBytes == 0;
    if (!eof) fields.append(field);
    return true;
}
bool loadReplay(const QString &path, wb::SampleBatch &rows, QString &detail)
{
    QFile file(path);
    if (path.isEmpty() || !file.open(QIODevice::ReadOnly)) {
        detail = QStringLiteral("cannot open data file: ") + path + QStringLiteral(" ") + file.errorString();
        return false;
    }
    QList<QByteArray> fields;
    bool eof = false;
    if (!readReplayRow(file, fields, eof, detail) || eof || fields != QList<QByteArray>{
        "deviceId", "sequence", "timestampMs", "temperature", "humidity", "voltage"}) {
        detail = QStringLiteral("invalid CSV header: ") + detail; return false;
    }
    const QRegularExpression digits(QStringLiteral("^[0-9]+$"));
    for (qsizetype line = 2;; ++line) {
        fields.clear();
        if (!readReplayRow(file, fields, eof, detail)) {
            detail = QStringLiteral("row %1: ").arg(line) + detail; return false;
        }
        if (eof) break;
        if (rows.size() >= MaxReplayRows) { detail = QStringLiteral("data exceeds 100000 records"); return false; }
        if (fields.size() != 6) { detail = QStringLiteral("row %1: expected six columns").arg(line); return false; }
        QStringList values;
        for (const auto &field : fields) {
            QStringDecoder decoder(QStringDecoder::Utf8);
            values.append(decoder(field));
            if (decoder.hasError()) { detail = QStringLiteral("row %1: invalid UTF-8").arg(line); return false; }
        }
        bool sequenceOk, timestampOk, temperatureOk, humidityOk, voltageOk;
        wb::Sample source;
        source.deviceId = values[0];
        source.sequence = values[1].toLongLong(&sequenceOk);
        source.timestampMs = values[2].toLongLong(&timestampOk);
        source.temperature = values[3].toDouble(&temperatureOk);
        source.humidity = values[4].toDouble(&humidityOk);
        source.voltage = values[5].toDouble(&voltageOk);
        wb::Sample validated;
        if (!digits.match(values[1]).hasMatch() || !digits.match(values[2]).hasMatch()
            || !sequenceOk || !timestampOk || !temperatureOk || !humidityOk || !voltageOk
            || !wb::sampleFromJson(wb::sampleToJson(source), validated, detail)) {
            detail = QStringLiteral("row %1: invalid measurement ").arg(line) + detail; return false;
        }
        if (!rows.isEmpty() && (source.sequence <= rows.last().sequence
                || source.timestampMs < rows.last().timestampMs)) {
            detail = QStringLiteral("row %1: sequence/timestamp order regression").arg(line); return false;
        }
        rows.append(source);
    }
    if (rows.isEmpty()) { detail = QStringLiteral("data file contains no records"); return false; }
    return true;
}
bool integerInRange(const QJsonValue &value, int minimum, int maximum, int &out)
{
    if (!value.isDouble()) return false;
    const double number = value.toDouble();
    if (!std::isfinite(number) || number < minimum || number > maximum || std::floor(number) != number)
        return false;
    out = static_cast<int>(number);
    return true;
}
class SimulatorImpl;
class Client final : public QObject {
public:
    Client(QTcpSocket *socket, SimulatorImpl *owner);
    void stop();
private:
    struct Frame { QByteArray bytes; qsizetype offset = 0; bool fragment = false; qint64 readyAt = 0; };
    void command(const QJsonObject &object);
    void sample();
    void enqueue(QByteArray bytes, bool fragment = false, int delay = 0);
    void pump();
    void acknowledge(const QString &id, bool ok, const QString &detail);
    QTcpSocket *socket;
    SimulatorImpl *owner;
    wb::WireCodec *codec;
    QTimer *generation;
    QTimer *sending;
    std::deque<Frame> frames;
    qsizetype queuedBytes = 0;
    quint64 count = 0;
    QString fault = QStringLiteral("none");
    int every = 1;
    int interval = 100;
    qsizetype replayCursor = 0;
    QElapsedTimer clock;
};

class SimulatorImpl final : public wb::Simulator {
public:
    explicit SimulatorImpl(QObject *parent) : Simulator(parent) {}
    ~SimulatorImpl() override { shutdown(); }
    void listen(QString address, quint16 port) override
    {
        shutdown();
        replayData.clear();
        replayMode = property("replayFile").isValid();
        setProperty("replayRows", 0);
        setProperty("dataSource", replayMode ? QStringLiteral("recorded_replay") : QStringLiteral("synthetic_fixture"));
        if (replayMode) {
            QString detail;
            if (!loadReplay(property("replayFile").toString(), replayData, detail)) {
                replayData.clear();
                emit error(QStringLiteral("replay_data_invalid: ") + detail);
                return;
            }
            setProperty("replayRows", static_cast<int>(replayData.size()));
            emit status(QStringLiteral("recorded_replay: %1 records from %2; recorded timestamps and values")
                .arg(replayData.size()).arg(property("replayFile").toString()));
        } else {
            emit status(QStringLiteral("synthetic_fixture: generated measurements"));
        }
        QHostAddress host;
        if (!host.setAddress(address)) {
            emit error(QStringLiteral("invalid_bind_address: ") + address);
            return;
        }
        server = new QTcpServer(this);
        connect(server, &QTcpServer::newConnection, this, [this] {
            while (server && server->hasPendingConnections()) {
                auto *socket = server->nextPendingConnection();
                if (clients.size() >= 16) {
                    socket->abort(); socket->deleteLater();
                    emit error(QStringLiteral("too_many_clients"));
                    continue;
                }
                auto *client = new Client(socket, this);
                clients.append(client);
                connect(socket, &QTcpSocket::disconnected, this, [this, client] {
                    clients.removeAll(client);
                    client->stop();
                    client->deleteLater();
                    emit status(QStringLiteral("client_disconnected"));
                });
                emit status(QStringLiteral("client_connected"));
            }
        });
        if (!server->listen(host, port)) {
            emit error(QStringLiteral("listen_failed: ") + server->errorString());
            delete server; server = nullptr;
            return;
        }
        emit listening(server->serverPort());
        emit status(QStringLiteral("listening"));
    }
    void shutdown() override
    {
        const bool wasListening = server != nullptr;
        if (server) server->close();
        const auto copy = clients;
        clients.clear();
        for (auto *client : copy) {
            client->stop();
            delete client;
        }
        delete server; server = nullptr;
        if (wasListening) emit status(QStringLiteral("shutdown"));
    }
    qint64 nextSequence() { return ++sequence; }
    bool isReplay() const { return replayMode; }
    const wb::SampleBatch &replaySamples() const { return replayData; }
private:
    QTcpServer *server = nullptr;
    QVector<Client *> clients;
    qint64 sequence = 0;
    bool replayMode = false;
    wb::SampleBatch replayData;
};

Client::Client(QTcpSocket *connection, SimulatorImpl *simulator)
    : QObject(simulator), socket(connection), owner(simulator), codec(wb_create_wire(this)),
      generation(new QTimer(this)), sending(new QTimer(this))
{
    clock.start();
    socket->setParent(this);
    socket->setReadBufferSize(65536);
    socket->setSocketOption(QAbstractSocket::LowDelayOption, 1);
    sending->setSingleShot(true);
    generation->setTimerType(Qt::PreciseTimer);
    connect(generation, &QTimer::timeout, this, [this] { sample(); });
    connect(sending, &QTimer::timeout, this, [this] { pump(); });
    connect(socket, &QTcpSocket::readyRead, this, [this] { codec->feed(socket->readAll()); });
    connect(codec, &wb::WireCodec::message, this, [this](QJsonObject object) { command(object); });
    connect(codec, &wb::WireCodec::error, owner, [simulator](const QString &detail) {
        emit simulator->error(QStringLiteral("client_protocol: ") + detail);
    });
    connect(socket, &QTcpSocket::bytesWritten, this, [this] {
        if (!frames.empty() && !sending->isActive()) sending->start(0);
    });
}
void Client::stop()
{
    generation->stop(); sending->stop(); frames.clear(); queuedBytes = 0;
    codec->reset();
    // Prevent the disconnect handler from scheduling deletion during explicit shutdown.
    QObject::disconnect(socket, nullptr, owner, nullptr);
    socket->abort();
}
void Client::acknowledge(const QString &id, bool ok, const QString &detail)
{
    enqueue(codec->encode({{QStringLiteral("v"), 1}, {QStringLiteral("type"), QStringLiteral("ack")},
        {QStringLiteral("id"), id}, {QStringLiteral("ok"), ok}, {QStringLiteral("detail"), detail}}));
}
void Client::command(const QJsonObject &object)
{
    const auto idValue = object.value(QStringLiteral("id"));
    const QString id = idValue.isString() ? idValue.toString() : QString();
    if (object.value(QStringLiteral("type")) != QStringLiteral("command") || id.isEmpty()) {
        acknowledge(id, false, QStringLiteral("invalid_command")); return;
    }
    const QString action = object.value(QStringLiteral("action")).toString();
    if (action == QStringLiteral("start")) {
        int requested = 0;
        if (!integerInRange(object.value(QStringLiteral("intervalMs")), 10, 60000, requested)) {
            acknowledge(id, false, QStringLiteral("invalid_interval")); return;
        }
        interval = requested;
        if (owner->isReplay() && replayCursor >= owner->replaySamples().size()) {
            acknowledge(id, false, QStringLiteral("replay_finished")); return;
        }
        acknowledge(id, true, QStringLiteral("started"));
        generation->start(interval);
    } else if (action == QStringLiteral("stop")) {
        generation->stop();
        acknowledge(id, true, QStringLiteral("stopped"));
    } else if (action == QStringLiteral("fault")) {
        const QString requested = object.value(QStringLiteral("mode")).toString();
        int requestedEvery = 0;
        if ((requested != QStringLiteral("none") && requested != QStringLiteral("fragment")
             && requested != QStringLiteral("malformed") && requested != QStringLiteral("disconnect")
             && requested != QStringLiteral("delay"))
            || !integerInRange(object.value(QStringLiteral("every")), 1, 2147483647, requestedEvery)) {
            acknowledge(id, false, QStringLiteral("invalid_fault")); return;
        }
        fault = requested; every = requestedEvery; count = 0;
        acknowledge(id, true, QStringLiteral("fault_configured"));
    } else {
        acknowledge(id, false, QStringLiteral("invalid_action"));
    }
}
void Client::sample()
{
    if (owner->isReplay() && replayCursor >= owner->replaySamples().size()) {
        generation->stop(); return;
    }
    const qint64 sequence = owner->nextSequence();
    wb::Sample value;
    bool finalReplayRecord = false;
    if (owner->isReplay()) {
        value = owner->replaySamples().at(replayCursor++);
        if (replayCursor == owner->replaySamples().size()) {
            finalReplayRecord = true;
            generation->stop();
            emit owner->status(QStringLiteral("replay_finished"));
        }
    } else {
        value.deviceId = QStringLiteral("sim-001");
        value.timestampMs = QDateTime::currentMSecsSinceEpoch();
        const double phase = static_cast<double>(sequence % 100000) * 0.07;
        value.temperature = 25.0 + 5.0 * std::sin(phase);
        value.humidity = 55.0 + 10.0 * std::cos(phase * 0.5);
        value.voltage = 3.3 + 0.05 * std::sin(phase * 0.3);
    }
    // Sequence identifies transmission attempts; recorded values and time stay intact.
    value.sequence = sequence;
    ++count;
    const bool inject = count % static_cast<quint64>(every) == 0;
    if (inject && fault == QStringLiteral("disconnect")) {
        generation->stop(); socket->abort(); return;
    }
    if (inject && fault == QStringLiteral("malformed"))
        enqueue(QByteArrayLiteral("{\"v\":1,\"type\":\"sample\",broken}\n"));
    else
        enqueue(codec->encode(wb::sampleToJson(value)), inject && fault == QStringLiteral("fragment"),
                inject && fault == QStringLiteral("delay") ? qBound(30, interval * 3, 500) : 0);
    // End is a normal FIFO frame, following the final (possibly delayed/fragmented)
    // sample or the deliberately malformed line. A disconnected client gets no end.
    if (finalReplayRecord)
        enqueue(codec->encode({{QStringLiteral("v"), 1}, {QStringLiteral("type"), QStringLiteral("stream_end")},
            {QStringLiteral("deviceId"), value.deviceId}, {QStringLiteral("sequence"), static_cast<double>(sequence)},
            {QStringLiteral("reason"), QStringLiteral("replay_finished")}}));
}
void Client::enqueue(QByteArray bytes, bool fragment, int delay)
{
    if (bytes.isEmpty() || socket->state() != QAbstractSocket::ConnectedState) return;
    if (frames.size() >= MaxQueuedFrames || queuedBytes + socket->bytesToWrite() + bytes.size() > MaxQueuedBytes) {
        emit owner->error(QStringLiteral("client_queue_full"));
        generation->stop(); socket->abort(); return;
    }
    queuedBytes += bytes.size();
    frames.push_back({std::move(bytes), 0, fragment, clock.elapsed() + delay});
    if (!sending->isActive()) sending->start(0);
}
void Client::pump()
{
    if (frames.empty() || socket->state() != QAbstractSocket::ConnectedState) return;
    Frame &frame = frames.front();
    const qint64 remainingDelay = frame.readyAt - clock.elapsed();
    if (remainingDelay > 0) { sending->start(static_cast<int>(qMin<qint64>(remainingDelay, 500))); return; }
    const qsizetype remaining = frame.bytes.size() - frame.offset;
    const qsizetype piece = frame.fragment ? qMin<qsizetype>(17, remaining) : remaining;
    const qint64 written = socket->write(frame.bytes.constData() + frame.offset, piece);
    if (written < 0) { socket->abort(); return; }
    const bool fragmented = frame.fragment;
    frame.offset += written; queuedBytes -= written;
    if (frame.offset == frame.bytes.size()) frames.pop_front();
    if (!frames.empty()) sending->start(fragmented ? 2 : 0);
}
}

extern "C" Q_DECL_EXPORT wb::Simulator *wb_create_simulator(QObject *parent)
{
    return new SimulatorImpl(parent);
}

