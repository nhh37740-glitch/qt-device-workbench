#include <workbench/contracts.h>
#include <QtTest/QtTest>
#include <QtNetwork/QTcpSocket>
#include <QtCore/QTemporaryDir>
#include <QtCore/QFile>
#include <QtCore/QJsonDocument>
#include <QtCore/QDir>
#include <memory>

class ResultReceiverTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { wb::registerTypes(); }
    void fragmentedCombinedDeduplicatedAndValidated() {
        QTemporaryDir dir(QDir::currentPath() + "/result-receiver-XXXXXX"); QVERIFY(dir.isValid());
        std::unique_ptr<wb::ResultReceiver> receiver(wb_create_receiver(nullptr));
        QSignalSpy listening(receiver.get(), &wb::ResultReceiver::listening), received(receiver.get(), &wb::ResultReceiver::received), errors(receiver.get(), &wb::ResultReceiver::error);
        const QString path = dir.filePath("nested/out.ndjson");
        receiver->listen("127.0.0.1", 0, path); QCOMPARE(listening.size(), 1);
        QTcpSocket socket; socket.connectToHost("127.0.0.1", listening[0][0].value<quint16>());
        QTRY_COMPARE(socket.state(), QAbstractSocket::ConnectedState);
        wb::Sample s{QString::fromUtf8("设备\"α"), 1, 2, 25.123456789, 60, 3.3};
        const auto bytes = QJsonDocument(wb::sampleToJson(s)).toJson(QJsonDocument::Compact) + '\n';
        socket.write(bytes.left(7)); QTest::qWait(20); QCOMPARE(received.size(), 0);
        socket.write(bytes.mid(7) + bytes + "{\"v\":1,\"type\":\"sample\",\"sequence\":3}\n");
        QTRY_COMPARE(received.size(), 1); QTRY_VERIFY(socket.bytesAvailable() > 0);
        QByteArray acks = socket.readAll();
        QTRY_VERIFY_WITH_TIMEOUT((acks += socket.readAll()).count('\n') >= 2, 2000);
        QCOMPARE(acks.count('\n'), 2); QVERIFY(!errors.isEmpty());
        receiver->shutdown();
        QFile out(path); QVERIFY(out.open(QIODevice::ReadOnly)); const auto data = out.readAll(); QCOMPARE(data.count('\n'), 1);
        QCOMPARE(QJsonDocument::fromJson(data.trimmed()).object().value("deviceId").toString(), s.deviceId);
    }
    void retryAfterReconnectWritesExactlyOnce() {
        QTemporaryDir dir(QDir::currentPath() + "/result-receiver-XXXXXX"); QVERIFY(dir.isValid());
        std::unique_ptr<wb::ResultReceiver> receiver(wb_create_receiver(nullptr));
        QSignalSpy listening(receiver.get(), &wb::ResultReceiver::listening), received(receiver.get(), &wb::ResultReceiver::received);
        receiver->listen("127.0.0.1", 0, dir.filePath("out.jsonl")); QCOMPARE(listening.size(), 1);
        const auto port = listening[0][0].value<quint16>();
        const auto bytes = QJsonDocument(wb::sampleToJson(wb::Sample{"s", 1, 2, 25, 60, 3.3})).toJson(QJsonDocument::Compact) + '\n';
        QTcpSocket first; first.connectToHost("127.0.0.1", port); QTRY_COMPARE(first.state(), QAbstractSocket::ConnectedState);
        first.write(bytes); QTRY_COMPARE(received.size(), 1); first.abort();
        QTcpSocket second; second.connectToHost("127.0.0.1", port); QTRY_COMPARE(second.state(), QAbstractSocket::ConnectedState);
        second.write(bytes); QTRY_VERIFY(second.bytesAvailable() > 0);
        QCOMPARE(QJsonDocument::fromJson(second.readAll().trimmed()).object().value("sequence").toInt(), 1);
        QCOMPARE(received.size(), 1); receiver->shutdown();
        QFile out(dir.filePath("out.jsonl")); QVERIFY(out.open(QIODevice::ReadOnly)); QCOMPARE(out.readAll().count('\n'), 1);
    }
    void fileFailureDoesNotAcknowledge() {
        QTemporaryDir dir(QDir::currentPath() + "/result-receiver-XXXXXX"); QVERIFY(dir.isValid());
        std::unique_ptr<wb::ResultReceiver> receiver(wb_create_receiver(nullptr));
        QSignalSpy listening(receiver.get(), &wb::ResultReceiver::listening), errors(receiver.get(), &wb::ResultReceiver::error), received(receiver.get(), &wb::ResultReceiver::received);
        receiver->listen("127.0.0.1", 0, dir.path()); QCOMPARE(listening.size(), 0); QVERIFY(!errors.isEmpty());
        errors.clear(); receiver->listen("127.0.0.1", 0, dir.filePath("out.ndjson")); QCOMPARE(listening.size(), 1);
        QTcpSocket socket; socket.connectToHost("127.0.0.1", listening[0][0].value<quint16>());
        QTRY_COMPARE(socket.state(), QAbstractSocket::ConnectedState);
        auto output = receiver->findChild<QFile *>(); QVERIFY(output); output->close();
        socket.write(QJsonDocument(wb::sampleToJson(wb::Sample{"s", 1, 2, 25, 60, 3.3})).toJson(QJsonDocument::Compact) + '\n');
        QTRY_VERIFY(!errors.isEmpty()); QTest::qWait(50);
        QCOMPARE(received.size(), 0); QCOMPARE(socket.bytesAvailable(), 0); receiver->shutdown();
    }
};
QTEST_GUILESS_MAIN(ResultReceiverTest)
#include "result_receiver_test.moc"
