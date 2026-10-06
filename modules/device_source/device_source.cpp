#include <workbench/contracts.h>
#include <QtCore/QDateTime>
#include <QtCore/QHash>
#include <QtCore/QStringList>
#include <QtCore/QTimer>
#include <QtNetwork/QTcpSocket>
#include <cmath>

namespace {
class Source final : public wb::DeviceSource {
public:
    explicit Source(QObject *parent) : DeviceSource(parent) {}

    void connectDevice(QString host, quint16 port) override {
        initialize(); // Factory construction is safe before moveToThread().
        m_connectionWanted = false;
        m_reconnect->stop();
        m_socket->abort();
        failPending(QStringLiteral("connection changed"));
        m_host = host.trimmed();
        m_port = port;
        m_shutdown = false;
        if (m_host.isEmpty() || !m_port) {
            emit error(QStringLiteral("invalid device host or port"));
            return;
        }
        m_connectionWanted = true;
        connectNow();
    }

    void startMeasurements(int intervalMs) override {
        if (intervalMs < 10 || intervalMs > 60000) {
            emit error(QStringLiteral("intervalMs must be 10..60000"));
            return;
        }
        m_interval = intervalMs;
        m_captureWanted = true;
        if (isConnected()) sendCommand("start", {{"intervalMs", intervalMs}});
        else emit status(QStringLiteral("start requested; waiting for device connection"));
    }

    void stopMeasurements() override {
        m_captureWanted = false;
        if (isConnected()) sendCommand("stop");
        else emit status(QStringLiteral("capture stopped"));
    }

    void setSimulation(QString mode, int every) override {
        static const QStringList modes = {"none", "fragment", "malformed", "disconnect", "delay"};
        if (!modes.contains(mode) || every < 1) {
            emit error(QStringLiteral("invalid simulation mode or every"));
            return;
        }
        m_faultMode = mode;
        m_faultEvery = every;
        m_faultWanted = true;
        if (isConnected()) sendFault();
        else emit status(QStringLiteral("simulation requested; waiting for device connection"));
    }

    void shutdown() override {
        m_shutdown = true;
        m_captureWanted = false;
        m_connectionWanted = false;
        if (m_reconnect) m_reconnect->stop();
        if (m_ackTimer) m_ackTimer->stop();
        if (m_socket) m_socket->abort();
        if (m_codec) m_codec->reset();
        failPending(QStringLiteral("source shutdown"));
        emit status(QStringLiteral("source shutdown"));
    }

private:
    struct Pending { qint64 deadline; QString action; };
    void initialize() {
        if (m_socket) return;
        m_socket = new QTcpSocket(this);
        m_codec = wb_create_wire(this);
        m_reconnect = new QTimer(this);
        m_reconnect->setSingleShot(true);
        m_reconnect->setInterval(500);
        m_ackTimer = new QTimer(this);
        m_ackTimer->setInterval(100);
        connect(m_reconnect, &QTimer::timeout, this, [this] { connectNow(); });
        connect(m_ackTimer, &QTimer::timeout, this, [this] {
            const auto now = QDateTime::currentMSecsSinceEpoch();
            const auto keys = m_pending.keys();
            for (const auto &id : keys) {
                if (m_pending.value(id).deadline > now) continue;
                const QString action = m_pending.take(id).action;
                const QString detail = QStringLiteral("command timeout: %1 (%2)").arg(action, id);
                emit commandResult(id, false, detail);
                emit error(detail);
            }
            if (m_pending.isEmpty()) m_ackTimer->stop();
        });
        connect(m_socket, &QTcpSocket::connected, this, [this] {
            m_reconnect->stop();
            m_codec->reset();
            // Each TCP connection is a new device session. A restarted simulator
            // may legitimately begin again at sequence 1 after reconnecting.
            m_lastSequence.clear();
            emit status(QStringLiteral("device connected %1:%2").arg(m_host).arg(m_port));
            if (m_faultWanted) sendFault();
            if (m_captureWanted) sendCommand("start", {{"intervalMs", m_interval}});
        });
        connect(m_socket, &QTcpSocket::readyRead, this, [this] { m_codec->feed(m_socket->readAll()); });
        connect(m_socket, &QTcpSocket::disconnected, this, [this] {
            m_codec->reset();
            failPending(QStringLiteral("device disconnected before acknowledgement"));
            if (m_connectionWanted && !m_shutdown) {
                emit status(QStringLiteral("device disconnected; reconnecting"));
                m_reconnect->start();
            }
        });
        connect(m_socket, &QTcpSocket::errorOccurred, this, [this](QAbstractSocket::SocketError) {
            if (!m_connectionWanted || m_shutdown) return;
            emit error(QStringLiteral("device socket: %1").arg(m_socket->errorString()));
            if (!m_reconnect->isActive()) m_reconnect->start();
        });
        connect(m_codec, &wb::WireCodec::error, this, [this](QString detail) { emit error(QStringLiteral("wire: %1").arg(detail)); });
        connect(m_codec, &wb::WireCodec::message, this, [this](QJsonObject message) { receive(message); });
    }

    bool isConnected() const {
        return !m_shutdown && m_socket && m_socket->state() == QAbstractSocket::ConnectedState;
    }
    void connectNow() {
        if (!m_connectionWanted || m_shutdown || isConnected()) return;
        m_codec->reset();
        m_socket->abort();
        emit status(QStringLiteral("connecting to device %1:%2").arg(m_host).arg(m_port));
        m_socket->connectToHost(m_host, m_port);
    }
    void sendFault() { sendCommand("fault", {{"mode", m_faultMode}, {"every", m_faultEvery}}); }
    void sendCommand(const QString &action, QJsonObject extra = {}) {
        const QString id = QStringLiteral("cmd-%1").arg(++m_commandCounter);
        extra.insert("v", 1);
        extra.insert("type", "command");
        extra.insert("id", id);
        extra.insert("action", action);
        const auto bytes = m_codec->encode(extra);
        if (bytes.isEmpty() || m_socket->write(bytes) != bytes.size()) {
            const QString detail = QStringLiteral("unable to send command %1").arg(action);
            emit commandResult(id, false, detail);
            emit error(detail);
            return;
        }
        m_pending.insert(id, {QDateTime::currentMSecsSinceEpoch() + 2000, action});
        m_ackTimer->start();
        emit status(QStringLiteral("command sent: %1 (%2)").arg(action, id));
    }
    void failPending(const QString &detail) {
        const auto keys = m_pending.keys();
        m_pending.clear();
        if (m_ackTimer) m_ackTimer->stop();
        for (const auto &id : keys) emit commandResult(id, false, detail);
    }
    void receive(const QJsonObject &message) {
        if (!message.value("v").isDouble() || message.value("v").toDouble() != 1) {
            emit error(QStringLiteral("unsupported protocol version"));
            return;
        }
        const QString type = message.value("type").toString();
        if (type == "sample") {
            wb::Sample sample;
            QString detail;
            if (!wb::sampleFromJson(message, sample, detail)) {
                emit error(QStringLiteral("invalid sample: %1").arg(detail));
                return;
            }
            if (sample.sequence <= m_lastSequence.value(sample.deviceId, 0)) {
                emit status(QStringLiteral("duplicate or stale sample ignored: %1/%2").arg(sample.deviceId).arg(sample.sequence));
                return;
            }
            m_lastSequence.insert(sample.deviceId, sample.sequence);
            emit sampleReady(sample);
        } else if (type == "stream_end") {
            const auto deviceValue = message.value("deviceId");
            const auto sequenceValue = message.value("sequence");
            const auto reasonValue = message.value("reason");
            const double sequence = sequenceValue.toDouble(-1);
            if (!deviceValue.isString() || deviceValue.toString().isEmpty()
                || !sequenceValue.isDouble() || !std::isfinite(sequence)
                || sequence < 1 || sequence > 9007199254740991.0 || std::floor(sequence) != sequence
                || !reasonValue.isString() || reasonValue.toString() != "replay_finished") {
                emit error(QStringLiteral("invalid replay stream_end message"));
                return;
            }
            const auto deviceId = deviceValue.toString();
            // Malformed transport samples still consume simulator sequences.
            // A terminal end may legitimately skip them, but cannot refer to an
            // unknown device or precede its last validated sample in this session.
            if (!m_lastSequence.contains(deviceId) || qint64(sequence) < m_lastSequence.value(deviceId)) {
                emit error(QStringLiteral("unmatched or stale replay stream_end message"));
                return;
            }
            m_captureWanted = false;
            emit status(QStringLiteral("replay_finished"));
        } else if (type == "ack") {
            const auto idValue = message.value("id");
            if (!idValue.isString() || idValue.toString().isEmpty() ||
                !message.value("ok").isBool() || !message.value("detail").isString()) {
                emit error(QStringLiteral("invalid command acknowledgement"));
                return;
            }
            const auto id = idValue.toString();
            if (!m_pending.contains(id)) {
                emit error(QStringLiteral("unmatched command acknowledgement: %1").arg(id));
                return;
            }
            m_pending.remove(id);
            if (m_pending.isEmpty()) m_ackTimer->stop();
            const bool ok = message.value("ok").toBool();
            const QString detail = message.value("detail").toString();
            emit commandResult(id, ok, detail);
            emit status(QStringLiteral("command %1: %2 (%3)").arg(id, ok ? "ok" : "failed", detail));
            if (!ok) emit error(QStringLiteral("command rejected: %1").arg(detail));
        } else {
            emit error(QStringLiteral("unexpected device message type: %1").arg(type));
        }
    }

    QTcpSocket *m_socket = nullptr;
    wb::WireCodec *m_codec = nullptr;
    QTimer *m_reconnect = nullptr;
    QTimer *m_ackTimer = nullptr;
    QString m_host;
    quint16 m_port = 0;
    bool m_connectionWanted = false;
    bool m_shutdown = false;
    bool m_captureWanted = false;
    int m_interval = 100;
    bool m_faultWanted = false;
    QString m_faultMode = "none";
    int m_faultEvery = 10;
    quint64 m_commandCounter = 0;
    QHash<QString, Pending> m_pending;
    QHash<QString, qint64> m_lastSequence;
};
}

extern "C" WB_SOURCE_API wb::DeviceSource *wb_create_source(QObject *parent) { return new Source(parent); }
