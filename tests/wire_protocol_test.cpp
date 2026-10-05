#include <workbench/contracts.h>
#include <QtTest/QTest>
#include <QtTest/QSignalSpy>
#include <QtCore/QThread>
#include <QtNetwork/QTcpServer>
#include <QtNetwork/QTcpSocket>

class WireProtocolTest final : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { wb::registerTypes(); }
    void realTcpPartialCombinedAndRecovery()
    {
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost, 0));
        QTcpSocket client;
        auto *codec = wb_create_wire(&server);
        QCOMPARE(codec->thread(), QThread::currentThread());
        QSignalSpy messages(codec, &wb::WireCodec::message);
        QSignalSpy errors(codec, &wb::WireCodec::error);
        connect(&server, &QTcpServer::newConnection, &server, [&] {
            QTcpSocket *peer = server.nextPendingConnection();
            peer->setReadBufferSize(8192);
            connect(peer, &QTcpSocket::readyRead, codec, [peer, codec] { codec->feed(peer->readAll()); });
        });
        client.connectToHost(QHostAddress::LocalHost, server.serverPort());
        QTRY_COMPARE(client.state(), QAbstractSocket::ConnectedState);
        const QByteArray command = "{\"v\":1,\"type\":\"command\",\"id\":\"one\",\"action\":\"stop\"}\n";
        QCOMPARE(client.write(command.left(9)), qint64(9));
        // A completed peer write still cannot produce a message without newline.
        QTRY_COMPARE(client.bytesToWrite(), qint64(0));
        QCOMPARE(messages.count(), 0);
        client.write(command.mid(9) + command.left(command.size() - 1) + "\r\n");
        QTRY_COMPARE(messages.count(), 2);
        client.write("{broken}\n{\"v\":2,\"type\":\"command\"}\n{\"v\":1,\"type\":\"unknown\"}\n" + command);
        QTRY_COMPARE(errors.count(), 3);
        QTRY_COMPARE(messages.count(), 3);
        client.write(QByteArray(65537, 'x'));
        QTRY_COMPARE(errors.count(), 4);
        client.write(QByteArray(65537, 'y') + "\n" + command);
        QTRY_COMPARE(messages.count(), 4);
        QCOMPARE(errors.count(), 4); // One error for the entire discarded line.
        QCOMPARE(messages.last().first().toJsonObject().value("id").toString(), QStringLiteral("one"));
    }
    void resetAndEncodingLimits()
    {
        QObject owner;
        auto *codec = wb_create_wire(&owner);
        QSignalSpy messages(codec, &wb::WireCodec::message);
        QSignalSpy errors(codec, &wb::WireCodec::error);
        const QJsonObject object{{"v", 1}, {"type", "ack"}, {"id", "x"}, {"ok", true}};
        const QByteArray encoded = codec->encode(object);
        QVERIFY(encoded.endsWith('\n'));
        QVERIFY(!encoded.left(encoded.size() - 1).contains('\n'));
        codec->feed(encoded.left(5));
        codec->reset();
        codec->feed(encoded);
        QCOMPARE(messages.count(), 1);
        codec->feed(QByteArray(65537, 'z'));
        QCOMPARE(errors.count(), 1);
        codec->reset();
        codec->feed(encoded);
        QCOMPARE(messages.count(), 2);
        QVERIFY(codec->encode({{"v", "1"}, {"type", "ack"}}).isEmpty());
        QVERIFY(codec->encode({{"v", 1}, {"type", "ack"}, {"payload", QString(65537, 'a')}}).isEmpty());
        codec->feed("[]\n\n{\"v\":1.5,\"type\":\"ack\"}\n");
        QCOMPARE(errors.count(), 4);
    }
};
QTEST_GUILESS_MAIN(WireProtocolTest)
#include "wire_protocol_test.moc"

