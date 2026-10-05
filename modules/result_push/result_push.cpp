#include <workbench/contracts.h>
#include <QtCore/QQueue>
#include <QtCore/QTimer>
#include <QtNetwork/QTcpSocket>
#include <cmath>

namespace {
class Push final : public wb::ResultPush {
public:
    using ResultPush::ResultPush;
    void connectSink(QString host, quint16 port) override {
        if (host.trimmed().isEmpty() || !port) { emit error("Invalid downstream address or port"); return; }
        initialize();
        stopped = false;
        destination = host; destinationPort = port;
        retry->stop(); timeout->stop(); inFlight = false;
        socket->abort(); codec->reset();
        connectNow();
    }
    void enqueue(wb::Sample s) override {
        // Forwarding is optional until the user chooses a downstream destination.
        if (destination.isEmpty()) return;
        wb::Sample checked; QString detail;
        if (!wb::sampleFromJson(wb::sampleToJson(s), checked, detail)) {
            emit error("Invalid push sample: " + detail); return;
        }
        if (stopped) { emit error("push_stopped: sample rejected"); return; }
        if (pending.size() >= 256) { emit error("queue_full: newest sample rejected"); return; }
        pending.enqueue(s); emit backlogChanged(pending.size()); sendHead();
    }
    void shutdown() override {
        stopped = true;
        if (retry) retry->stop();
        if (timeout) timeout->stop();
        if (socket) socket->abort();
        inFlight = false;
        if (!pending.isEmpty()) emit error(QString("push_shutdown: %1 undelivered records remain").arg(pending.size()));
        emit status("downstream stopped");
    }
private:
    QTcpSocket *socket = nullptr;
    wb::WireCodec *codec = nullptr;
    QTimer *retry = nullptr, *timeout = nullptr;
    QQueue<wb::Sample> pending;
    QString destination;
    quint16 destinationPort = 0;
    bool inFlight = false, stopped = false;
    void initialize() {
        if (socket) return;
        socket = new QTcpSocket(this); codec = wb_create_wire(this);
        retry = new QTimer(this); retry->setSingleShot(true); retry->setInterval(200);
        timeout = new QTimer(this); timeout->setSingleShot(true); timeout->setInterval(1500);
        connect(retry, &QTimer::timeout, this, [this] { connectNow(); });
        connect(timeout, &QTimer::timeout, this, [this] {
            emit error("downstream_ack_timeout: reconnecting and resending");
            inFlight = false; socket->abort(); scheduleRetry();
        });
        connect(socket, &QTcpSocket::connected, this, [this] {
            retry->stop(); codec->reset(); inFlight = false;
            emit status("downstream connected"); sendHead();
        });
        connect(socket, &QTcpSocket::disconnected, this, [this] {
            timeout->stop(); inFlight = false; codec->reset();
            if (!stopped) { emit status("downstream disconnected; retrying"); scheduleRetry(); }
        });
        connect(socket, &QTcpSocket::errorOccurred, this, [this](QAbstractSocket::SocketError) {
            if (!stopped) { emit error("Downstream socket: " + socket->errorString()); scheduleRetry(); }
        });
        connect(socket, &QTcpSocket::readyRead, this, [this] { codec->feed(socket->readAll()); });
        connect(codec, &wb::WireCodec::error, this, [this](QString detail) { emit error("Downstream protocol: " + detail); });
        connect(codec, &wb::WireCodec::message, this, [this](QJsonObject object) { acknowledge(object); });
    }
    void connectNow() {
        if (stopped || destination.isEmpty()) return;
        socket->abort(); codec->reset(); inFlight = false;
        emit status("downstream connecting");
        socket->connectToHost(destination, destinationPort);
    }
    void scheduleRetry() { if (!stopped && !retry->isActive()) retry->start(); }
    void sendHead() {
        if (stopped || !socket || socket->state() != QAbstractSocket::ConnectedState || inFlight || pending.isEmpty()) return;
        const QByteArray bytes = codec->encode(wb::sampleToJson(pending.head()));
        if (bytes.isEmpty()) { emit error("Downstream encoding failed"); return; }
        inFlight = true;
        if (socket->write(bytes) != bytes.size()) {
            emit error("Downstream write failed"); inFlight = false; socket->abort(); scheduleRetry(); return;
        }
        timeout->start();
    }
    void acknowledge(const QJsonObject &o) {
        if (!inFlight || pending.isEmpty()) return;
        const wb::Sample &s = pending.head();
        const QJsonValue sequence = o.value("sequence");
        if (!o.value("v").isDouble() || o.value("v").toDouble() != 1 || o.value("type").toString() != "received" ||
            !o.value("deviceId").isString() || o.value("deviceId").toString() != s.deviceId ||
            !sequence.isDouble() || sequence.toDouble() != static_cast<double>(s.sequence)) {
            emit error("downstream_ack_mismatch: retaining pending record"); return;
        }
        timeout->stop(); inFlight = false;
        const auto deliveredSample = pending.dequeue();
        emit delivered(deliveredSample.sequence); emit backlogChanged(pending.size());
        sendHead();
    }
};
}
extern "C" WB_PUSH_API wb::ResultPush *wb_create_push(QObject *parent) { return new Push(parent); }
