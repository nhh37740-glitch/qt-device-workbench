#include <workbench/contracts.h>
#include <QtCore/QThread>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QLabel>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QPlainTextEdit>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QSpinBox>
#include <QtTest/QSignalSpy>
#include <QtTest/QTest>
#include <memory>

class FrontendTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { wb::registerTypes(); }
    void controlsEmitRequestedValues() {
        std::unique_ptr<wb::Frontend> ui(wb_create_frontend(nullptr));
        QCOMPARE(ui->thread(), QThread::currentThread());
        ui->show();
        auto field = [&](const char *name) { return ui->findChild<QLineEdit *>(name); };
        auto number = [&](const char *name) { return ui->findChild<QSpinBox *>(name); };
        auto click = [&](const char *name) {
            auto button = ui->findChild<QPushButton *>(name);
            QVERIFY(button);
            QTest::mouseClick(button, Qt::LeftButton);
        };
        QVERIFY(field("deviceHost"));
        QCOMPARE(field("deviceHost")->text(), QString("127.0.0.1"));
        QCOMPARE(number("devicePort")->value(), 9101);
        QCOMPARE(number("intervalMs")->value(), 100);
        QCOMPARE(field("downstreamHost")->text(), QString("127.0.0.1"));
        QCOMPARE(number("downstreamPort")->value(), 9102);
        QSignalSpy connected(ui.get(), &wb::Frontend::connectRequested);
        QSignalSpy started(ui.get(), &wb::Frontend::startRequested);
        QSignalSpy stopped(ui.get(), &wb::Frontend::stopRequested);
        QSignalSpy simulation(ui.get(), &wb::Frontend::simulationRequested);
        QSignalSpy record(ui.get(), &wb::Frontend::recordRequested);
        QSignalSpy recordStop(ui.get(), &wb::Frontend::recordStopRequested);
        QSignalSpy history(ui.get(), &wb::Frontend::historyRequested);
        QSignalSpy downstream(ui.get(), &wb::Frontend::downstreamRequested);
        field("deviceHost")->setText(" 127.0.0.2 ");
        number("devicePort")->setValue(9201);
        number("intervalMs")->setValue(250);
        click("connectButton");
        click("startButton");
        click("stopButton");
        QCOMPARE(connected.count(), 1);
        QCOMPARE(connected[0][0].toString(), QString("127.0.0.2"));
        QCOMPARE(connected[0][1].value<quint16>(), quint16(9201));
        QCOMPARE(started[0][0].toInt(), 250);
        QCOMPARE(stopped.count(), 1);
        auto modes = ui->findChild<QComboBox *>("faultMode");
        QVERIFY(modes);
        modes->setCurrentIndex(modes->findData("malformed"));
        number("faultEvery")->setValue(7);
        click("faultButton");
        QCOMPARE(simulation[0][0].toString(), QString("malformed"));
        QCOMPARE(simulation[0][1].toInt(), 7);
        field("recordPath")->setText("test-output.csv");
        click("recordStartButton");
        click("recordStopButton");
        QCOMPARE(record[0][0].toString(), QString("test-output.csv"));
        QCOMPARE(recordStop.count(), 1);
        field("historyPath")->setText("test-history.csv");
        click("historyButton");
        QCOMPARE(history[0][0].toString(), QString("test-history.csv"));
        number("downstreamPort")->setValue(9202);
        click("downstreamButton");
        QCOMPARE(downstream[0][0].toString(), QString("127.0.0.1"));
        QCOMPARE(downstream[0][1].value<quint16>(), quint16(9202));
    }
    void sampleStatusHistoryAndBoundedPlot() {
        std::unique_ptr<wb::Frontend> ui(wb_create_frontend(nullptr));
        ui->show();
        const wb::Sample sample{"display-device", 12, 1791216000000, 25.5, 61.2, 3.3};
        ui->showSample(sample);
        auto temperature = ui->findChild<QLabel *>("latestTemperature");
        auto humidity = ui->findChild<QLabel *>("latestHumidity");
        auto voltage = ui->findChild<QLabel *>("latestVoltage");
        auto sequence = ui->findChild<QLabel *>("latestSequence");
        auto device = ui->findChild<QLabel *>("latestDevice");
        auto plot = ui->findChild<QWidget *>("samplePlot");
        auto status = ui->findChild<QPlainTextEdit *>("statusLog");
        QVERIFY(temperature && humidity && voltage && sequence && device && plot && status);
        QVERIFY(temperature->text().contains("25.50"));
        QVERIFY(humidity->text().contains("61.20"));
        QVERIFY(voltage->text().contains("3.300"));
        QCOMPARE(sequence->property("sequence").toLongLong(), qint64(12));
        QVERIFY(device->text().contains("display-device"));
        QCOMPARE(plot->property("pointCount").toInt(), 1);
        ui->showStatus("source", "invalid sample: humidity");
        QVERIFY(status->toPlainText().contains("source"));
        QVERIFY(status->toPlainText().contains("invalid sample: humidity"));
        wb::SampleBatch history;
        for (int i = 1; i <= 1200; ++i) {
            auto next = sample;
            next.sequence = i;
            next.temperature += i / 100.0;
            history.append(next);
        }
        ui->showHistory(history);
        QCOMPARE(plot->property("pointCount").toInt(), 1000);
        QCOMPARE(ui->findChild<QLabel *>("historyCount")->property("recordCount").toInt(), 1200);
        QCOMPARE(sequence->property("sequence").toLongLong(), qint64(1200));
        auto next = sample;
        next.sequence = 1201;
        ui->showSample(next);
        QCOMPARE(plot->property("pointCount").toInt(), 1000);
        QCOMPARE(sequence->property("sequence").toLongLong(), qint64(1201));
        QTest::qWait(75); // Exercise the actual 20 Hz timer and QPainter path.
        QVERIFY(!plot->grab().isNull());
        ui->showHistory({});
        QCOMPARE(plot->property("pointCount").toInt(), 0);
        QCOMPARE(sequence->property("sequence").toLongLong(), qint64(0));
    }
};
QTEST_MAIN(FrontendTest)
#include "frontend_test.moc"
