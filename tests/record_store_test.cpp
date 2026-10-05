#include <workbench/contracts.h>
#include <QtTest/QtTest>
#include <QtCore/QTemporaryDir>
#include <QtCore/QFile>
#include <QtCore/QThread>
#include <QtCore/QDir>
#include <limits>
#include <memory>

class RecordStoreTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { wb::registerTypes(); }
    void quotedUtf8PrecisionAndIgnoredAppend() {
        QTemporaryDir dir(QDir::currentPath() + "/record-store-XXXXXX"); QVERIFY(dir.isValid());
        std::unique_ptr<wb::RecordStore> store(wb_create_store(nullptr));
        QSignalSpy stored(store.get(), &wb::RecordStore::stored), history(store.get(), &wb::RecordStore::historyReady), errors(store.get(), &wb::RecordStore::error);
        wb::Sample sample{QString::fromUtf8("设备,\"quoted\"\nline\rreturn"), 9007199254740991LL, 1791216000000LL,
                          25.123456789012345, 60.987654321098765, 3.3000000000000003};
        store->append(sample);
        QCOMPARE(stored.size(), 0);
        const QString path = dir.filePath("nested/records.csv");
        store->begin(path); store->append(sample); store->stop(); store->append(sample); store->load(path);
        QCOMPARE(errors.size(), 0); QCOMPARE(stored.size(), 1); QCOMPARE(history.size(), 1);
        const auto loaded = qvariant_cast<wb::SampleBatch>(history[0][0]);
        QCOMPARE(loaded.size(), 1);
        QCOMPARE(loaded[0].deviceId, sample.deviceId); QCOMPARE(loaded[0].sequence, sample.sequence);
        QCOMPARE(loaded[0].timestampMs, sample.timestampMs); QCOMPARE(loaded[0].temperature, sample.temperature);
        QCOMPARE(loaded[0].humidity, sample.humidity); QCOMPARE(loaded[0].voltage, sample.voltage);
    }
    void corruptionAndBounds() {
        QTemporaryDir dir(QDir::currentPath() + "/record-store-XXXXXX"); QVERIFY(dir.isValid());
        std::unique_ptr<wb::RecordStore> store(wb_create_store(nullptr));
        QSignalSpy errors(store.get(), &wb::RecordStore::error), history(store.get(), &wb::RecordStore::historyReady);
        const QList<QByteArray> badRows = {"s,1,2,25,60\n", "s,1,2,nan,60,3.3\n", "s,1.5,2,25,60,3.3\n",
            "s,1,2,25,101,3.3\n", "\"unterminated,1,2,25,60,3.3\n", "s\"bad,1,2,25,60,3.3\n",
            QByteArray("\xff,1,2,25,60,3.3\n"), "s,9007199254740992,2,25,60,3.3\n"};
        for (const auto &bad : badRows) {
            QFile f(dir.filePath("bad.csv")); QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("deviceId,sequence,timestampMs,temperature,humidity,voltage\r\n"); f.write(bad); f.close();
            errors.clear(); store->load(f.fileName()); QCOMPARE(errors.size(), 1); QCOMPARE(history.size(), 0);
        }
        QFile large(dir.filePath("large.csv")); QVERIFY(large.open(QIODevice::WriteOnly));
        large.write("deviceId,sequence,timestampMs,temperature,humidity,voltage\n");
        for (int i = 0; i < 100001; ++i) large.write("s,1,2,25,60,3.3\n");
        large.close(); errors.clear(); store->load(large.fileName());
        QCOMPARE(errors.size(), 1); QVERIFY(errors[0][0].toString().contains("100000")); QCOMPARE(history.size(), 0);
    }
    void fileAndValidationFailure() {
        QTemporaryDir dir(QDir::currentPath() + "/record-store-XXXXXX"); QVERIFY(dir.isValid());
        std::unique_ptr<wb::RecordStore> store(wb_create_store(nullptr));
        QSignalSpy errors(store.get(), &wb::RecordStore::error), stored(store.get(), &wb::RecordStore::stored);
        store->begin(dir.path()); QVERIFY(!errors.isEmpty());
        store->begin(dir.filePath("valid.csv")); errors.clear();
        wb::Sample s{"s", 1, 2, std::numeric_limits<double>::infinity(), 60, 3.3};
        store->append(s); QCOMPARE(errors.size(), 1); QCOMPARE(stored.size(), 0);
        auto file = store->findChild<QFile *>(); QVERIFY(file); file->close();
        s.temperature = 25; store->append(s); QCOMPARE(errors.size(), 2); QCOMPARE(stored.size(), 0);
        store->shutdown();
    }
    void resourcesCreatedInWorkerThread() {
        QTemporaryDir dir(QDir::currentPath() + "/record-store-XXXXXX"); QVERIFY(dir.isValid());
        QThread thread;
        auto store = wb_create_store(nullptr);
        store->moveToThread(&thread);
        connect(&thread, &QThread::finished, store, &QObject::deleteLater);
        QSignalSpy stored(store, &wb::RecordStore::stored);
        thread.start();
        const QString path = dir.filePath("worker.csv");
        QVERIFY(QMetaObject::invokeMethod(store, "begin", Qt::BlockingQueuedConnection, Q_ARG(QString, path)));
        bool correctThread = false;
        QMetaObject::invokeMethod(store, [&] { auto file = store->findChild<QFile *>(); correctThread = file && file->thread() == QThread::currentThread(); }, Qt::BlockingQueuedConnection);
        const wb::Sample s{"s", 1, 2, 25, 60, 3.3};
        QVERIFY(QMetaObject::invokeMethod(store, "append", Qt::QueuedConnection, Q_ARG(wb::Sample, s)));
        QMetaObject::invokeMethod(store, "shutdown", Qt::BlockingQueuedConnection);
        thread.quit(); QVERIFY(thread.wait(3000));
        QCoreApplication::processEvents();
        QVERIFY(correctThread); QCOMPARE(stored.size(), 1);
    }
};
QTEST_GUILESS_MAIN(RecordStoreTest)
#include "record_store_test.moc"
