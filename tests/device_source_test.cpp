#include <workbench/contracts.h>
#include <QtCore/QJsonDocument>
#include <QtCore/QThread>
#include <QtNetwork/QTcpServer>
#include <QtNetwork/QTcpSocket>
#include <QtTest/QSignalSpy>
#include <QtTest/QTest>

namespace {
QByteArray line(const QJsonObject &json) { return QJsonDocument(json).toJson(QJsonDocument::Compact) + '\n'; }
wb::Sample sample(qint64 sequence, const QString &device = "test-device") {
    return {device, sequence, 1791216000000 + sequence, 25.5, 61.2, 3.3};
}
QByteArray ack(const QString &id, bool ok = true) {
    return line({{"v", 1}, {"type", "ack"}, {"id", id}, {"ok", ok}, {"detail", "confirmed"}});
}
struct Worker {
    QThread thread;
    wb::DeviceSource *source = wb_create_source(nullptr);
    Worker() {
        source->moveToThread(&thread);
        QObject::connect(&thread, &QThread::finished, source, &QObject::deleteLater);
        thread.start();
    }
    ~Worker() {
        QMetaObject::invokeMethod(source, "shutdown", Qt::BlockingQueuedConnection);
        thread.quit();
        thread.wait();
    }
    void connectTo(quint16 port) {
        QMetaObject::invokeMethod(source, "connectDevice", Qt::QueuedConnection,
            Q_ARG(QString, QStringLiteral("127.0.0.1")), Q_ARG(quint16, port));
    }
    void start(int interval) {
        QMetaObject::invokeMethod(source, "startMeasurements", Qt::QueuedConnection, Q_ARG(int, interval));
    }
};
}

class DeviceSourceTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { wb::registerTypes(); }
    void partialCombinedValidationAndThreadAffinity() {
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost, 0));
        Worker worker;
        QSignalSpy samples(worker.source, &wb::DeviceSource::sampleReady);
        QSignalSpy errors(worker.source, &wb::DeviceSource::error);
        worker.connectTo(server.serverPort());
        QTRY_VERIFY_WITH_TIMEOUT(server.hasPendingConnections(), 3000);
        auto peer = server.nextPendingConnection();
        QVERIFY(peer);
        bool resourcesCorrect = false;
        QMetaObject::invokeMethod(worker.source, [&] {
            resourcesCorrect = worker.source->thread() == QThread::currentThread();
            const auto children = worker.source->findChildren<QObject *>();
            resourcesCorrect = resourcesCorrect && children.size() >= 4;
            for (auto child : children) resourcesCorrect = resourcesCorrect && child->thread() == QThread::currentThread();
        }, Qt::BlockingQueuedConnection);
        QVERIFY(resourcesCorrect);
        const auto first = line(wb::sampleToJson(sample(1)));
        peer->write(first.left(11));
        QTest::qWait(40);
        QCOMPARE(samples.count(), 0);
        peer->write(first.mid(11) + line(wb::sampleToJson(sample(2))) + line(wb::sampleToJson(sample(3))));
        QTRY_COMPARE_WITH_TIMEOUT(samples.count(), 3, 2000);
        QCOMPARE(qvariant_cast<wb::Sample>(samples[2][0]).sequence, qint64(3));
        auto invalid = wb::sampleToJson(sample(4));
        invalid.insert("humidity", 101);
        auto stringNumber = wb::sampleToJson(sample(4));
        stringNumber.insert("temperature", "25.5");
        auto nonInteger = wb::sampleToJson(sample(4));
        nonInteger.insert("sequence", 4.5);
        peer->write("{broken}\n" + line(invalid) + line(stringNumber) + line(nonInteger)
            + line({{"v", 2}, {"type", "sample"}}) + line({{"v", 1}, {"type", "unknown"}})
            + line(wb::sampleToJson(sample(2))) + line(wb::sampleToJson(sample(3)))
            + line(wb::sampleToJson(sample(1, "second-device"))) + line(wb::sampleToJson(sample(4))));
        QTRY_COMPARE_WITH_TIMEOUT(samples.count(), 5, 2000);
        QVERIFY(errors.count() >= 6);
        QCOMPARE(qvariant_cast<wb::Sample>(samples[3][0]).deviceId, QString("second-device"));
        QCOMPARE(qvariant_cast<wb::Sample>(samples[4][0]).sequence, qint64(4));
        // Previously emitted value copies must remain intact after further reads.
        QCOMPARE(qvariant_cast<wb::Sample>(samples[0][0]).sequence, qint64(1));
        auto recovered = line(wb::sampleToJson(sample(5)));
        recovered.chop(1);
        recovered.append("\r\n");
        peer->write(QByteArray(70000, 'x') + '\n' + recovered);
        QTRY_COMPARE_WITH_TIMEOUT(samples.count(), 6, 2000);
        QVERIFY(errors.count() >= 7);
        QCOMPARE(qvariant_cast<wb::Sample>(samples[5][0]).sequence, qint64(5));
    }

    void acknowledgementsFaultAndTimeout() {
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost, 0));
        Worker worker;
        QSignalSpy results(worker.source, &wb::DeviceSource::commandResult);
        QSignalSpy errors(worker.source, &wb::DeviceSource::error);
        worker.connectTo(server.serverPort());
        QTRY_VERIFY(server.hasPendingConnections());
        auto peer = server.nextPendingConnection();
        worker.start(70);
        QTRY_VERIFY(peer->canReadLine());
        const auto start = QJsonDocument::fromJson(peer->readLine()).object();
        QCOMPARE(start.value("action").toString(), QString("start"));
        QCOMPARE(start.value("intervalMs").toInt(), 70);
        peer->write(ack("wrong-id"));
        QTRY_VERIFY(errors.count() > 0);
        QCOMPARE(results.count(), 0);
        const auto startId = start.value("id").toString();
        peer->write(ack(startId));
        QTRY_COMPARE(results.count(), 1);
        QCOMPARE(results[0][0].toString(), startId);
        QVERIFY(results[0][1].toBool());
        QMetaObject::invokeMethod(worker.source, "setSimulation", Qt::QueuedConnection,
            Q_ARG(QString, QStringLiteral("fragment")), Q_ARG(int, 3));
        QTRY_VERIFY(peer->canReadLine());
        const auto fault = QJsonDocument::fromJson(peer->readLine()).object();
        QCOMPARE(fault.value("action").toString(), QString("fault"));
        QCOMPARE(fault.value("mode").toString(), QString("fragment"));
        QCOMPARE(fault.value("every").toInt(), 3);
        peer->write(ack(fault.value("id").toString(), false));
        QTRY_COMPARE(results.count(), 2);
        QVERIFY(!results[1][1].toBool());
        QMetaObject::invokeMethod(worker.source, "stopMeasurements", Qt::QueuedConnection);
        QTRY_VERIFY(peer->canReadLine());
        const auto stop = QJsonDocument::fromJson(peer->readLine()).object();
        QCOMPARE(stop.value("action").toString(), QString("stop"));
        QTRY_COMPARE_WITH_TIMEOUT(results.count(), 3, 3500);
        QCOMPARE(results[2][0].toString(), stop.value("id").toString());
        QVERIFY(!results[2][1].toBool());
        QVERIFY(results[2][2].toString().contains("timeout"));
    }

    void requestedStartReconnectAndSequenceReset() {
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost, 0));
        Worker worker;
        QSignalSpy samples(worker.source, &wb::DeviceSource::sampleReady);
        worker.start(120); // Retain desired capture before connecting.
        worker.connectTo(server.serverPort());
        QTRY_VERIFY(server.hasPendingConnections());
        auto firstPeer = server.nextPendingConnection();
        QTRY_VERIFY(firstPeer->canReadLine());
        const auto start = QJsonDocument::fromJson(firstPeer->readLine()).object();
        QCOMPARE(start.value("intervalMs").toInt(), 120);
        firstPeer->write(ack(start.value("id").toString()) + line(wb::sampleToJson(sample(99))));
        QTRY_COMPARE(samples.count(), 1);
        firstPeer->abort();
        QTRY_VERIFY_WITH_TIMEOUT(server.hasPendingConnections(), 3500);
        auto secondPeer = server.nextPendingConnection();
        QTRY_VERIFY(secondPeer->canReadLine());
        const auto resumed = QJsonDocument::fromJson(secondPeer->readLine()).object();
        QCOMPARE(resumed.value("action").toString(), QString("start"));
        QCOMPARE(resumed.value("intervalMs").toInt(), 120);
        QVERIFY(resumed.value("id").toString() != start.value("id").toString());
        secondPeer->write(ack(resumed.value("id").toString()) + line(wb::sampleToJson(sample(1))));
        QTRY_COMPARE(samples.count(), 2);
        QCOMPARE(qvariant_cast<wb::Sample>(samples[1][0]).sequence, qint64(1));
        QMetaObject::invokeMethod(worker.source, "shutdown", Qt::BlockingQueuedConnection);
        QTRY_COMPARE(secondPeer->state(), QAbstractSocket::UnconnectedState);
        QTest::qWait(650);
        QVERIFY(!server.hasPendingConnections());
    }
};
QTEST_GUILESS_MAIN(DeviceSourceTest)
#include "device_source_test.moc"
