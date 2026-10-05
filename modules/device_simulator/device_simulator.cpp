#include <workbench/contracts.h>
#include <QtCore/QDateTime>
#include <QtCore/QElapsedTimer>
#include <QtCore/QTimer>
#include <QtNetwork/QHostAddress>
#include <QtNetwork/QTcpServer>
#include <QtNetwork/QTcpSocket>
#include <cmath>
#include <deque>

namespace {
constexpr qsizetype MaxQueuedBytes = 262144;
constexpr size_t MaxQueuedFrames = 256;
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
    QElapsedTimer clock;
};

class SimulatorImpl final : public wb::Simulator {
public:
    explicit SimulatorImpl(QObject *parent) : Simulator(parent) {}
    ~SimulatorImpl() override { shutdown(); }
    void listen(QString address, quint16 port) override
    {
        shutdown();
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
private:
    QTcpServer *server = nullptr;
    QVector<Client *> clients;
    qint64 sequence = 0;
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
    const qint64 sequence = owner->nextSequence();
    ++count;
    const bool inject = count % static_cast<quint64>(every) == 0;
    if (inject && fault == QStringLiteral("disconnect")) {
        generation->stop(); socket->abort(); return;
    }
    wb::Sample value;
    value.deviceId = QStringLiteral("sim-001");
    value.sequence = sequence;
    value.timestampMs = QDateTime::currentMSecsSinceEpoch();
    const double phase = static_cast<double>(sequence % 100000) * 0.07;
    value.temperature = 25.0 + 5.0 * std::sin(phase);
    value.humidity = 55.0 + 10.0 * std::cos(phase * 0.5);
    value.voltage = 3.3 + 0.05 * std::sin(phase * 0.3);
    if (inject && fault == QStringLiteral("malformed"))
        enqueue(QByteArrayLiteral("{\"v\":1,\"type\":\"sample\",broken}\n"));
    else
        enqueue(codec->encode(wb::sampleToJson(value)), inject && fault == QStringLiteral("fragment"),
                inject && fault == QStringLiteral("delay") ? qBound(30, interval * 3, 500) : 0);
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

