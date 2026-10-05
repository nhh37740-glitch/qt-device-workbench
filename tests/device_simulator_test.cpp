#include <workbench/contracts.h>
#include <QtTest/QTest>
#include <QtTest/QSignalSpy>
#include <QtCore/QTimer>
#include <QtCore/QElapsedTimer>
#include <QtCore/QThread>
#include <QtNetwork/QHostAddress>
#include <QtNetwork/QTcpSocket>

class Harness final : public QObject {
public:
    wb::Simulator *simulator = wb_create_simulator(this);
    wb::WireCodec *codec = wb_create_wire(this);
    QTcpSocket socket;
    QVector<QJsonObject> messages;
    QVector<QString> wireErrors;
    quint16 port = 0;
    Harness()
    {
        connect(simulator, &wb::Simulator::listening, this, [this](quint16 p) { port = p; });
        connect(&socket, &QTcpSocket::readyRead, this, [this] { codec->feed(socket.readAll()); });
        connect(codec, &wb::WireCodec::message, this, [this](QJsonObject object) { messages.append(object); });
        connect(codec, &wb::WireCodec::error, this, [this](QString detail) { wireErrors.append(detail); });
        simulator->listen(QStringLiteral("127.0.0.1"), 0);
        socket.connectToHost(QHostAddress::LocalHost, port);
    }
    ~Harness() override { simulator->shutdown(); }
    void send(const QString &id, const QString &action, QJsonObject extra = {})
    {
        extra.insert("v", 1); extra.insert("type", "command"); extra.insert("id", id); extra.insert("action", action);
        socket.write(codec->encode(extra));
    }
    QJsonObject ack(const QString &id) const
    {
        for (const auto &object : messages)
            if (object.value("type") == QStringLiteral("ack") && object.value("id") == id) return object;
        return {};
    }
    QVector<wb::Sample> samples() const
    {
        QVector<wb::Sample> values;
        for (const auto &object : messages) {
            if (object.value("type") != QStringLiteral("sample")) continue;
            wb::Sample value; QString error;
            if (wb::sampleFromJson(object, value, error)) values.append(value);
        }
        return values;
    }
};
class DeviceSimulatorTest final : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { wb::registerTypes(); }
    void acknowledgementsValidationAndStop()
    {
        Harness h;
        QVERIFY(h.port != 0);
        QCOMPARE(h.simulator->thread(), QThread::currentThread());
        QTRY_COMPARE(h.socket.state(), QAbstractSocket::ConnectedState);
        h.send("bad1", "start", {{"intervalMs", 9}});
        h.send("bad2", "start", {{"intervalMs", "100"}});
        h.send("bad3", "start", {{"intervalMs", 10.5}});
        h.send("bad4", "dance");
        h.send("bad5", "fault", {{"mode", "fragment"}, {"every", 0}});
        h.send("bad6", "start", {{"intervalMs", 60001}});
        h.send("bad7", "fault", {{"mode", "unknown"}, {"every", 1}});
        QTRY_VERIFY(!h.ack("bad7").isEmpty());
        for (int i = 1; i <= 7; ++i) QVERIFY(!h.ack(QStringLiteral("bad%1").arg(i)).value("ok").toBool());
        QVERIFY(h.samples().isEmpty());
        // Partial and combined command framing also traverse the actual server.
        const QByteArray start = h.codec->encode({{"v", 1}, {"type", "command"}, {"id", "start"},
            {"action", "start"}, {"intervalMs", 10}});
        h.socket.write(start.left(12));
        QTRY_COMPARE(h.socket.bytesToWrite(), qint64(0));
        QVERIFY(h.ack("start").isEmpty());
        h.socket.write(start.mid(12));
        QTRY_VERIFY(h.samples().size() >= 4);
        QVERIFY(h.ack("start").value("ok").toBool());
        QCOMPARE(h.messages.first().value("type").toString(), QStringLiteral("ack"));
        qint64 last = 0;
        for (const auto &value : h.samples()) { QVERIFY(value.sequence > last); last = value.sequence; }
        h.send("stop", "stop");
        QTRY_VERIFY(!h.ack("stop").isEmpty());
        QVERIFY(h.ack("stop").value("ok").toBool());
        const auto stoppedCount = h.samples().size();
        QTimer window;
        window.setSingleShot(true);
        QSignalSpy elapsed(&window, &QTimer::timeout);
        window.start(80);
        QTRY_COMPARE(elapsed.count(), 1);
        QCOMPARE(h.samples().size(), stoppedCount);
        h.simulator->shutdown();
        QTRY_COMPARE(h.socket.state(), QAbstractSocket::UnconnectedState);
        h.simulator->listen(QStringLiteral("127.0.0.1"), 0);
        QVERIFY(h.port != 0);
    }
    void malformedAndOversizedCommandRecovery()
    {
        Harness h;
        QSignalSpy errors(h.simulator, &wb::Simulator::error);
        QTRY_COMPARE(h.socket.state(), QAbstractSocket::ConnectedState);
        h.socket.write("{broken}\n");
        h.socket.write(QByteArray(65537, 'x') + "\n");
        h.send("good", "start", {{"intervalMs", 20}});
        QTRY_VERIFY(!h.ack("good").isEmpty());
        QVERIFY(h.ack("good").value("ok").toBool());
        QTRY_VERIFY(h.samples().size() >= 2);
        QCOMPARE(errors.count(), 2);
    }
    void faults_data()
    {
        QTest::addColumn<QString>("mode");
        QTest::newRow("fragment") << QStringLiteral("fragment");
        QTest::newRow("malformed") << QStringLiteral("malformed");
        QTest::newRow("delay") << QStringLiteral("delay");
    }
    void faults()
    {
        QFETCH(QString, mode);
        Harness h;
        QTRY_COMPARE(h.socket.state(), QAbstractSocket::ConnectedState);
        int readyReads = 0;
        connect(&h.socket, &QTcpSocket::readyRead, &h, [&] { ++readyReads; });
        h.send("fault", "fault", {{"mode", mode}, {"every", 2}});
        h.send("start", "start", {{"intervalMs", 30}});
        QTRY_VERIFY_WITH_TIMEOUT(h.samples().size() >= 6, 3000);
        QVERIFY(h.ack("fault").value("ok").toBool());
        QVERIFY(h.ack("start").value("ok").toBool());
        qint64 last = 0;
        for (const auto &value : h.samples()) { QVERIFY(value.sequence > last); last = value.sequence; }
        if (mode == QStringLiteral("malformed")) QVERIFY(!h.wireErrors.isEmpty());
        else QVERIFY(h.wireErrors.isEmpty());
        if (mode == QStringLiteral("fragment")) QVERIFY(readyReads > h.messages.size());
        h.send("none", "fault", {{"mode", "none"}, {"every", 1}});
        QTRY_VERIFY_WITH_TIMEOUT(!h.ack("none").isEmpty(), 3000);
        const auto before = h.samples().size();
        QTRY_VERIFY(h.samples().size() >= before + 3);
    }
    void disconnectReconnectResetsFaultAndCodec()
    {
        Harness h;
        QTRY_COMPARE(h.socket.state(), QAbstractSocket::ConnectedState);
        h.send("fault", "fault", {{"mode", "disconnect"}, {"every", 3}});
        h.send("start", "start", {{"intervalMs", 30}});
        QTRY_COMPARE(h.socket.state(), QAbstractSocket::UnconnectedState);
        QCOMPARE(h.samples().size(), 2);
        const qint64 prior = h.samples().last().sequence;
        h.codec->reset();
        h.socket.connectToHost(QHostAddress::LocalHost, h.port);
        QTRY_COMPARE(h.socket.state(), QAbstractSocket::ConnectedState);
        // New session has no inherited disconnect fault and sequence remains global.
        h.send("restart", "start", {{"intervalMs", 20}});
        QTRY_VERIFY(h.samples().size() >= 8);
        QVERIFY(h.samples().at(2).sequence > prior);
        QCOMPARE(h.socket.state(), QAbstractSocket::ConnectedState);
        h.socket.write("{\"v\":1,\"type\":\"command\"");
        QTRY_COMPARE(h.socket.bytesToWrite(), qint64(0));
        h.socket.abort();
        QTimer settle;
        settle.setSingleShot(true);
        QSignalSpy settled(&settle, &QTimer::timeout);
        settle.start(30);
        QTRY_COMPARE(settled.count(), 1);
        h.codec->reset();
        h.socket.connectToHost(QHostAddress::LocalHost, h.port);
        QTRY_COMPARE(h.socket.state(), QAbstractSocket::ConnectedState);
        h.send("fresh", "stop");
        QTRY_VERIFY(!h.ack("fresh").isEmpty());
        QVERIFY(h.ack("fresh").value("ok").toBool());
    }
    void delayKeepsEventLoopResponsive()
    {
        Harness h;
        QTRY_COMPARE(h.socket.state(), QAbstractSocket::ConnectedState);
        h.send("delay", "fault", {{"mode", "delay"}, {"every", 1}});
        QTRY_VERIFY(!h.ack("delay").isEmpty());
        QElapsedTimer timer;
        timer.start();
        qint64 firstSampleAt = -1;
        connect(h.codec, &wb::WireCodec::message, &h, [&](QJsonObject object) {
            if (object.value("type") == QStringLiteral("sample") && firstSampleAt < 0)
                firstSampleAt = timer.elapsed();
        });
        QTimer heartbeat;
        int ticks = 0;
        connect(&heartbeat, &QTimer::timeout, &h, [&] { ++ticks; });
        heartbeat.start(10);
        h.send("start", "start", {{"intervalMs", 40}});
        QTRY_VERIFY(!h.ack("start").isEmpty());
        QVERIFY(h.samples().isEmpty());
        QTRY_VERIFY(firstSampleAt >= 0);
        QVERIFY(firstSampleAt >= 140); // 40 ms period + 120 ms injected delay, with timer tolerance.
        QVERIFY(ticks >= 5);
    }
};
QTEST_GUILESS_MAIN(DeviceSimulatorTest)
#include "device_simulator_test.moc"
