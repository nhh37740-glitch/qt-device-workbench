#include <workbench/contracts.h>
#include <QtTest/QtTest>
#include <QtNetwork/QTcpServer>
#include <QtNetwork/QTcpSocket>
#include <QtCore/QTemporaryDir>
#include <QtCore/QFile>
#include <QtCore/QJsonDocument>
#include <QtCore/QThread>
#include <QtCore/QDir>
#include <memory>

namespace {
QByteArray ack(QString id, qint64 sequence) {
    return QJsonDocument(QJsonObject{{"v", 1}, {"type", "received"}, {"deviceId", id}, {"sequence", double(sequence)}}).toJson(QJsonDocument::Compact) + '\n';
}
wb::Sample sample(qint64 sequence) { return {"s", sequence, 2, 25, 60, 3.3}; }
}
class ResultPushTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { wb::registerTypes(); }
    void requiresMatchingAckAndOneInFlight() {
        QTcpServer server; QVERIFY(server.listen(QHostAddress::LocalHost, 0));
        std::unique_ptr<wb::ResultPush> push(wb_create_push(nullptr));
        QSignalSpy delivered(push.get(), &wb::ResultPush::delivered), errors(push.get(), &wb::ResultPush::error), backlog(push.get(), &wb::ResultPush::backlogChanged);
        push->connectSink("127.0.0.1", server.serverPort()); push->enqueue(sample(1)); push->enqueue(sample(2));
        QTRY_VERIFY(server.hasPendingConnections()); auto peer = server.nextPendingConnection();
        QTRY_VERIFY(peer->canReadLine());
        QCOMPARE(QJsonDocument::fromJson(peer->readLine()).object().value("sequence").toInt(), 1);
        QTest::qWait(30); QCOMPARE(peer->bytesAvailable(), 0); QCOMPARE(delivered.size(), 0);
        peer->write(ack("wrong-device", 1) + ack("s", 99));
        QTRY_VERIFY(errors.size() >= 2); QCOMPARE(delivered.size(), 0);
        auto correct = ack("s", 1); peer->write(correct.left(9)); QTest::qWait(20); QCOMPARE(delivered.size(), 0);
        peer->write(correct.mid(9)); QTRY_COMPARE(delivered.size(), 1);
        QTRY_VERIFY(peer->canReadLine()); QCOMPARE(QJsonDocument::fromJson(peer->readLine()).object().value("sequence").toInt(), 2);
        peer->write(ack("s", 1) + ack("s", 2)); QTRY_COMPARE(delivered.size(), 2);
        QCOMPARE(delivered[0][0].toLongLong(), 1); QCOMPARE(delivered[1][0].toLongLong(), 2);
        QCOMPARE(backlog.last()[0].toInt(), 0); push->shutdown();
    }
    void missingAckAndDisconnectResendHead() {
        QTcpServer server; QVERIFY(server.listen(QHostAddress::LocalHost, 0));
        std::unique_ptr<wb::ResultPush> push(wb_create_push(nullptr));
        QSignalSpy delivered(push.get(), &wb::ResultPush::delivered), errors(push.get(), &wb::ResultPush::error);
        push->connectSink("127.0.0.1", server.serverPort()); push->enqueue(sample(7));
        QTRY_VERIFY(server.hasPendingConnections()); auto first = server.nextPendingConnection();
        QTRY_VERIFY(first->canReadLine()); const auto original = first->readLine();
        QTRY_VERIFY_WITH_TIMEOUT(server.hasPendingConnections(), 4000);
        auto second = server.nextPendingConnection(); QTRY_VERIFY(second->canReadLine()); QCOMPARE(second->readLine(), original);
        QCOMPARE(delivered.size(), 0); QVERIFY(!errors.isEmpty()); second->abort();
        QTRY_VERIFY_WITH_TIMEOUT(server.hasPendingConnections(), 3000);
        auto third = server.nextPendingConnection(); QTRY_VERIFY(third->canReadLine()); QCOMPARE(third->readLine(), original);
        third->write(ack("s", 7)); QTRY_COMPARE(delivered.size(), 1); push->shutdown();
    }
    void unconfiguredSamplesIgnoredThenOfflineQueueRejectsNewest() {
        std::unique_ptr<wb::ResultPush> push(wb_create_push(nullptr));
        QSignalSpy errors(push.get(), &wb::ResultPush::error), backlog(push.get(), &wb::ResultPush::backlogChanged);
        for (int i = 1; i <= 1000; ++i) push->enqueue(sample(10000 + i));
        QCOMPARE(errors.size(), 0); QCOMPARE(backlog.size(), 0);
        push->connectSink("127.0.0.1", 0); QCOMPARE(errors.size(), 1); errors.clear();
        for (int i = 1; i <= 1000; ++i) push->enqueue(sample(20000 + i));
        QCOMPARE(errors.size(), 0); QCOMPARE(backlog.size(), 0);
        QTcpServer server; QVERIFY(server.listen(QHostAddress::LocalHost, 0));
        const quint16 port = server.serverPort(); server.close();
        push->connectSink("127.0.0.1", port);
        for (int i = 1; i <= 257; ++i) push->enqueue(sample(i));
        QCOMPARE(errors.size(), 1); QVERIFY(errors[0][0].toString().contains("queue_full"));
        QCOMPARE(backlog.size(), 256); QCOMPARE(backlog.last()[0].toInt(), 256);
        // The configured destination is offline; bring it up without reconfiguring push.
        QTRY_VERIFY(errors.size() >= 2);
        QVERIFY(server.listen(QHostAddress::LocalHost, port));
        QTRY_VERIFY(server.hasPendingConnections()); auto peer = server.nextPendingConnection();
        QTRY_VERIFY(peer->canReadLine()); QCOMPARE(QJsonDocument::fromJson(peer->readLine()).object().value("sequence").toInt(), 1);
        push->shutdown();
    }
    void configuredShutdownRejectsVisibly() {
        std::unique_ptr<wb::ResultPush> push(wb_create_push(nullptr));
        QSignalSpy errors(push.get(), &wb::ResultPush::error), backlog(push.get(), &wb::ResultPush::backlogChanged);
        push->shutdown(); push->enqueue(sample(1));
        QCOMPARE(errors.size(), 0); QCOMPARE(backlog.size(), 0);
        QTcpServer server; QVERIFY(server.listen(QHostAddress::LocalHost, 0));
        push->connectSink("127.0.0.1", server.serverPort()); push->shutdown(); push->enqueue(sample(2));
        QCOMPARE(errors.size(), 1); QVERIFY(errors[0][0].toString().contains("push_stopped")); QCOMPARE(backlog.size(), 0);
    }
    void lostAcknowledgementDeduplicatesAtRealReceiver() {
        QTemporaryDir dir(QDir::currentPath() + "/result-push-XXXXXX"); QVERIFY(dir.isValid());
        std::unique_ptr<wb::ResultReceiver> receiver(wb_create_receiver(nullptr));
        QSignalSpy listening(receiver.get(), &wb::ResultReceiver::listening), accepted(receiver.get(), &wb::ResultReceiver::received);
        receiver->listen("127.0.0.1", 0, dir.filePath("out.ndjson")); QCOMPARE(listening.size(), 1);
        const auto receiverPort = listening[0][0].value<quint16>();
        QTcpServer proxy; QVERIFY(proxy.listen(QHostAddress::LocalHost, 0));
        bool dropped = false;
        connect(&proxy, &QTcpServer::newConnection, this, [&] {
            while (proxy.hasPendingConnections()) {
                auto client = proxy.nextPendingConnection();
                auto upstream = new QTcpSocket(client);
                auto buffer = std::make_shared<QByteArray>();
                connect(client, &QTcpSocket::readyRead, upstream, [client, upstream, buffer] {
                    *buffer += client->readAll();
                    if (upstream->state() == QAbstractSocket::ConnectedState) { upstream->write(*buffer); buffer->clear(); }
                });
                connect(upstream, &QTcpSocket::connected, client, [upstream, buffer] { upstream->write(*buffer); buffer->clear(); });
                connect(upstream, &QTcpSocket::readyRead, client, [&, client, upstream] {
                    const auto bytes = upstream->readAll();
                    if (!dropped) { dropped = true; return; }
                    client->write(bytes);
                });
                connect(client, &QTcpSocket::disconnected, upstream, [upstream] { upstream->abort(); });
                upstream->connectToHost("127.0.0.1", receiverPort);
            }
        });
        std::unique_ptr<wb::ResultPush> push(wb_create_push(nullptr));
        QSignalSpy delivered(push.get(), &wb::ResultPush::delivered);
        push->connectSink("127.0.0.1", proxy.serverPort()); push->enqueue(sample(5));
        QTRY_COMPARE_WITH_TIMEOUT(delivered.size(), 1, 5000); QVERIFY(dropped); QCOMPARE(accepted.size(), 1);
        push->shutdown(); receiver->shutdown();
        QFile out(dir.filePath("out.ndjson")); QVERIFY(out.open(QIODevice::ReadOnly)); QCOMPARE(out.readAll().count('\n'), 1);
    }
    void resourcesCreatedInWorkerThread() {
        QTcpServer server; QVERIFY(server.listen(QHostAddress::LocalHost, 0));
        QThread thread;
        auto push = wb_create_push(nullptr); push->moveToThread(&thread);
        connect(&thread, &QThread::finished, push, &QObject::deleteLater);
        thread.start(); const quint16 port = server.serverPort();
        QMetaObject::invokeMethod(push, "connectSink", Qt::BlockingQueuedConnection, Q_ARG(QString, QString("127.0.0.1")), Q_ARG(quint16, port));
        bool correctThread = false;
        QMetaObject::invokeMethod(push, [&] {
            const auto socket = push->findChild<QTcpSocket *>();
            correctThread = socket && socket->thread() == QThread::currentThread();
            for (auto object : push->children()) correctThread = correctThread && object->thread() == QThread::currentThread();
        }, Qt::BlockingQueuedConnection);
        QMetaObject::invokeMethod(push, "shutdown", Qt::BlockingQueuedConnection); thread.quit(); QVERIFY(thread.wait(3000));
        QVERIFY(correctThread);
    }
};
QTEST_GUILESS_MAIN(ResultPushTest)
#include "result_push_test.moc"
