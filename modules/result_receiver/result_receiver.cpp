#include <workbench/contracts.h>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QDir>
#include <QtCore/QHash>
#include <QtCore/QSet>
#include <QtNetwork/QTcpServer>
#include <QtNetwork/QTcpSocket>
#include <QtNetwork/QHostAddress>

namespace {
class Receiver final : public wb::ResultReceiver {
public:
    using ResultReceiver::ResultReceiver;
    void listen(QString address, quint16 port, QString outputPath) override {
        shutdown();
        QHostAddress host;
        if (!host.setAddress(address)) { emit error("Invalid receiver bind address"); return; }
        if (!outputPath.isEmpty()) {
            if (!QDir().mkpath(QFileInfo(outputPath).absolutePath())) { emit error("Cannot create receiver output directory"); return; }
            output = new QFile(outputPath, this);
            if (!output->open(QIODevice::WriteOnly | QIODevice::Append)) {
                emit error("Receiver output open failed: " + output->errorString());
                delete output; output = nullptr; return;
            }
        }
        if (!server) {
            server = new QTcpServer(this);
            connect(server, &QTcpServer::newConnection, this, [this] { accept(); });
        }
        if (!server->listen(host, port)) {
            emit error("Receiver listen failed: " + server->errorString());
            shutdown(); return;
        }
        running = true;
        emit status(QString("receiver listening %1:%2").arg(address).arg(server->serverPort()));
        emit listening(server->serverPort());
    }
    void shutdown() override {
        running = false;
        if (server) server->close();
        const auto copy = sockets;
        for (auto socket : copy) socket->disconnectFromHost();
        if (output) {
            if (output->isOpen() && !output->flush()) emit error("Receiver output flush failed: " + output->errorString());
            output->close(); delete output; output = nullptr;
        }
        emit status("receiver stopped");
    }
private:
    QTcpServer *server = nullptr;
    QFile *output = nullptr;
    QSet<QTcpSocket *> sockets;
    QHash<QString, QSet<qint64>> accepted;
    bool running = false;
    void accept() {
        while (server->hasPendingConnections()) {
            auto socket = server->nextPendingConnection();
            socket->setParent(this); sockets.insert(socket);
            auto codec = wb_create_wire(socket);
            connect(socket, &QTcpSocket::readyRead, this, [socket, codec] { codec->feed(socket->readAll()); });
            connect(codec, &wb::WireCodec::error, this, [this](QString detail) { emit error("Receiver protocol: " + detail); });
            connect(codec, &wb::WireCodec::message, this, [this, socket, codec](QJsonObject message) {
                receive(socket, codec, message);
            });
            connect(socket, &QTcpSocket::disconnected, this, [this, socket] {
                sockets.remove(socket); socket->deleteLater();
            });
            emit status("receiver client connected");
        }
    }
    void receive(QTcpSocket *socket, wb::WireCodec *codec, const QJsonObject &message) {
        if (!running) return;
        wb::Sample s; QString detail;
        if (!wb::sampleFromJson(message, s, detail)) { emit error("Receiver invalid sample: " + detail); return; }
        const bool duplicate = accepted.value(s.deviceId).contains(s.sequence);
        if (!duplicate) {
            const QByteArray bytes = codec->encode(wb::sampleToJson(s));
            if (bytes.isEmpty()) { emit error("Receiver sample encoding failed"); return; }
            if (output && (output->write(bytes) != bytes.size() || !output->flush())) {
                emit error("Receiver output write failed: " + output->errorString());
                // Fail closed: a partial row must never be followed by further accepted rows.
                running = false; server->close();
                emit status("receiver output failed; acknowledgements stopped");
                return;
            }
            accepted[s.deviceId].insert(s.sequence);
        }
        QJsonObject ack{{"v", 1}, {"type", "received"}, {"deviceId", s.deviceId}, {"sequence", static_cast<double>(s.sequence)}};
        const auto bytes = codec->encode(ack);
        if (socket->write(bytes) != bytes.size()) emit error("Receiver acknowledgement write failed");
        socket->flush();
        if (!duplicate) emit received(s);
    }
};
}
extern "C" WB_RECEIVER_API wb::ResultReceiver *wb_create_receiver(QObject *parent) { return new Receiver(parent); }
