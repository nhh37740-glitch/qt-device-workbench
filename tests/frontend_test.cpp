#include <workbench/contracts.h>
#include <QtCore/QDateTime>
#include <QtCore/QDir>
#include <QtCore/QThread>
#include <QtCore/QTimer>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QLabel>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QPlainTextEdit>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QSpinBox>
#include <QtWidgets/QTabWidget>
#include <QtWidgets/QToolButton>
#include <QtTest/QSignalSpy>
#include <QtTest/QTest>
#include <cmath>
#include <memory>

namespace {
wb::Sample synthetic(qint64 sequence) {
    // Deliberately synthetic fixture. Never presented as a public measurement.
    const double x = double(sequence);
    return {"synthetic-ui-fixture", sequence, 1700000000000 + sequence * 1000,
            22 + std::sin(x / 24) * 1.8 + std::sin(x / 5) * .15,
            42 + std::cos(x / 40) * 4, 3.05 + std::cos(x / 80) * .02};
}
wb::SampleBatch syntheticHistory(int count) {
    wb::SampleBatch samples;
    for (int i = 1; i <= count; ++i) samples.append(synthetic(i));
    return samples;
}
}
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
        auto settings = ui->findChild<QTabWidget *>("settingsTabs");
        QVERIFY(settings);
        auto click = [&](const char *name) {
            auto button = ui->findChild<QPushButton *>(name);
            QVERIFY(button);
            for (int i = 0; i < settings->count(); ++i)
                if (settings->widget(i)->isAncestorOf(button)) settings->setCurrentIndex(i);
            QVERIFY(button->isVisible());
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
        QCOMPARE(started.count(), 1);
        QCOMPARE(started[0][0].toInt(), 250);
        QCOMPARE(stopped.count(), 1);
        auto modes = ui->findChild<QComboBox *>("faultMode");
        QVERIFY(modes);
        modes->setCurrentIndex(modes->findData("malformed"));
        number("faultEvery")->setValue(7);
        click("faultButton");
        QCOMPARE(simulation.count(), 1);
        QCOMPARE(simulation[0][0].toString(), QString("malformed"));
        QCOMPARE(simulation[0][1].toInt(), 7);
        field("recordPath")->setText("test-output.csv");
        click("recordStartButton");
        click("recordStopButton");
        QCOMPARE(record.count(), 1);
        QCOMPARE(record[0][0].toString(), QString("test-output.csv"));
        QCOMPARE(recordStop.count(), 1);
        field("historyPath")->setText("test-history.csv");
        click("historyButton");
        QCOMPARE(history.count(), 1);
        QCOMPARE(history[0][0].toString(), QString("test-history.csv"));
        number("downstreamPort")->setValue(9202);
        click("downstreamButton");
        QCOMPARE(downstream.count(), 1);
        QCOMPARE(downstream[0][0].toString(), QString("127.0.0.1"));
        QCOMPARE(downstream[0][1].value<quint16>(), quint16(9202));
    }
    void sampleStatusHistoryAndBoundedPlot() {
        std::unique_ptr<wb::Frontend> ui(wb_create_frontend(nullptr));
        ui->show();
        const wb::Sample sample{"synthetic-ui-fixture", 12, 1791216000000, 25.5, 61.2, 3.3};
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
        QVERIFY(device->text().contains("synthetic-ui-fixture"));
        QCOMPARE(plot->property("pointCount").toInt(), 1);
        ui->showStatus("source", "invalid sample: humidity");
        QVERIFY(status->toPlainText().contains("invalid sample: humidity"));
        ui->showHistory(syntheticHistory(1200));
        QCOMPARE(plot->property("pointCount").toInt(), 1000);
        QCOMPARE(ui->findChild<QLabel *>("historyCount")->property("recordCount").toInt(), 1200);
        QCOMPARE(sequence->property("sequence").toLongLong(), qint64(1200));
        ui->showSample(synthetic(1201));
        QCOMPARE(plot->property("pointCount").toInt(), 1000);
        QCOMPARE(sequence->property("sequence").toLongLong(), qint64(1201));
        auto timer = plot->findChild<QTimer *>("plotRefreshTimer");
        QVERIFY(timer && timer->isSingleShot());
        QTRY_VERIFY(!timer->isActive());
        QVERIFY(!plot->grab().isNull());
        QTest::qWait(80);
        const int paints = plot->property("paintCount").toInt();
        QTest::qWait(180);
        QCOMPARE(plot->property("paintCount").toInt(), paints);
        for (auto childTimer : ui->findChildren<QTimer *>()) QVERIFY(!childTimer->isActive());
        ui->showHistory({});
        QCOMPARE(plot->property("pointCount").toInt(), 0);
        QCOMPARE(sequence->property("sequence").toLongLong(), qint64(0));
        QTRY_VERIFY(!timer->isActive());
    }
    void sourceLabelsAndOriginalPublicClock() {
        std::unique_ptr<wb::Frontend> ui(wb_create_frontend(nullptr));
        auto label = ui->findChild<QLabel *>("dataSourceLabel");
        QVERIFY(label);
        QVERIFY(label->text().contains(QStringLiteral("连接设备后确认")));
        ui->showSample(synthetic(1));
        QVERIFY(label->text().contains("synthetic-ui-fixture"));
        QVERIFY(!label->text().contains("Intel"));
        auto publicSample = synthetic(2);
        publicSample.deviceId = "intel-lab-1";
        publicSample.timestampMs = QDateTime(QDate(2004, 2, 28), QTime(1, 2, 3), Qt::UTC).toMSecsSinceEpoch();
        ui->showHistory({publicSample});
        QVERIFY(label->text().contains(QStringLiteral("公开实测数据回放")));
        QVERIFY(label->text().contains("Intel Berkeley / MIT"));
        const auto device = ui->findChild<QLabel *>("latestDevice");
        QVERIFY(device->text().contains("2004-02-28 01:02:03.000"));
        QVERIFY(device->text().contains(QStringLiteral("录制时间（时区未注明）")));
        ui->showSample(synthetic(3));
        QVERIFY(!label->text().contains("Intel"));
        ui->setProperty("publicDataDescription", QStringLiteral("合成测试夹具 · 非实测数据"));
        ui->show();
        QVERIFY(label->text().contains(QStringLiteral("合成测试夹具")));
        ui->showSample(synthetic(4));
        QVERIFY(label->text().contains(QStringLiteral("非实测数据")));
        ui->setProperty("publicDataDescription", QVariant());
        ui->showStatus("data", QStringLiteral("自定义设备来源说明"));
        ui->showSample(synthetic(5));
        QVERIFY(label->text().contains(QStringLiteral("自定义设备来源说明")));
    }
    void statesUseActualEventsAndLogsAreBounded() {
        std::unique_ptr<wb::Frontend> ui(wb_create_frontend(nullptr));
        auto connection = ui->findChild<QLabel *>("connectionIndicator");
        auto recording = ui->findChild<QLabel *>("recordingIndicator");
        auto forward = ui->findChild<QLabel *>("forwardingIndicator");
        auto capture = ui->findChild<QLabel *>("captureIndicator");
        QVERIFY(connection && recording && forward && capture);
        ui->findChild<QPushButton *>("connectButton")->click();
        QVERIFY(connection->text().contains(QStringLiteral("等待连接")));
        ui->findChild<QPushButton *>("startButton")->click();
        QVERIFY(!capture->text().contains(QStringLiteral("运行中")));
        ui->showStatus(QStringLiteral("接收测量值"), "device connected 127.0.0.1:9101");
        QVERIFY(connection->text().contains(QStringLiteral("已连接")));
        ui->showStatus(QStringLiteral("设备回复"), "cmd-1: started");
        QVERIFY(capture->text().contains(QStringLiteral("运行中")));
        ui->showStatus(QStringLiteral("接收测量值"), "replay_finished");
        QVERIFY(capture->text().contains(QStringLiteral("回放已结束")));
        ui->showStatus(QStringLiteral("保存记录"), "recording synthetic-test.csv");
        QVERIFY(recording->text().contains(QStringLiteral("正在写入")));
        ui->showStatus(QStringLiteral("保存记录"), "recording stopped");
        QVERIFY(recording->text().contains(QStringLiteral("已停止")));
        ui->showStatus(QStringLiteral("向下游发送"), "downstream connected");
        QVERIFY(forward->text().contains(QStringLiteral("已连接")));
        ui->showStatus(QStringLiteral("向下游发送"), "downstream disconnected; retrying");
        QVERIFY(forward->text().contains(QStringLiteral("重连")));
        ui->showStatus(QStringLiteral("待下游确认"), "0");
        QVERIFY(!forward->text().contains(QStringLiteral("已连接")));
        ui->showStatus(QStringLiteral("接收测量值"), "device disconnected; reconnecting");
        QVERIFY(connection->text().contains(QStringLiteral("重连")));
        for (int i = 0; i < 2010; ++i)
            ui->showStatus(QStringLiteral("向下游发送错误"), QString("queue_full: synthetic-error-%1").arg(i));
        auto errors = ui->findChild<QPlainTextEdit *>("errorLog");
        auto events = ui->findChild<QPlainTextEdit *>("statusLog");
        QVERIFY(errors && events);
        QVERIFY(errors->document()->blockCount() <= 2000);
        QVERIFY(events->document()->blockCount() <= 2000);
        QVERIFY(errors->toPlainText().contains("synthetic-error-2009"));
        QVERIFY(ui->findChild<QLabel *>("errorBanner")->text().contains("queue_full"));
        QVERIFY(forward->text().contains(QStringLiteral("错误")));
    }
    void dashboardFitsSupportedSizes_data() {
        QTest::addColumn<QSize>("size");
        QTest::newRow("compact") << QSize(1020, 720);
        QTest::newRow("standard") << QSize(1100, 780);
        QTest::newRow("large") << QSize(1280, 860);
    }
    void dashboardFitsSupportedSizes() {
        QFETCH(QSize, size);
        std::unique_ptr<wb::Frontend> ui(wb_create_frontend(nullptr));
        ui->setProperty("publicDataDescription", QStringLiteral("合成测试夹具 · 仅用于界面验证（非实测数据）"));
        ui->resize(size);
        ui->showHistory(syntheticHistory(400));
        ui->resize(size);
        ui->show();
        QTest::qWait(100);
        QCOMPARE(ui->size(), size);
        auto plot = ui->findChild<QWidget *>("samplePlot");
        auto settings = ui->findChild<QTabWidget *>("settingsTabs");
        QVERIFY(plot && settings);
        QVERIFY2(plot->width() >= 600, "Curves must remain the dominant content at compact size");
        QVERIFY2(plot->height() >= 330, "Three channels need legible vertical space");
        for (int page = 0; page < settings->count(); ++page) {
            settings->setCurrentIndex(page);
            QCoreApplication::processEvents();
            const char *names[] = {"startButton", "stopButton", "samplePlot", "settingsPanel", "latestTemperature", "latestHumidity", "latestVoltage", "latestSequence",
                                  "deviceHost", "devicePort", "intervalMs", "connectButton", "faultMode", "faultEvery", "faultButton", "recordPath", "recordStartButton",
                                  "recordStopButton", "historyPath", "historyButton", "historyCount", "downstreamHost", "downstreamPort", "downstreamButton", "dataSourceLabel"};
            for (const auto name : names) {
                auto widget = ui->findChild<QWidget *>(name);
                QVERIFY2(widget, name);
                if (!widget->isVisible()) continue;
                const QRect bounds(widget->mapTo(ui.get(), QPoint()), widget->size());
                QVERIFY2(ui->rect().contains(bounds), name);
            }
        }
        settings->setCurrentIndex(0);
        const auto image = plot->grab().toImage();
        int orangePixels = 0;
        for (int y = 0; y < image.height(); ++y) for (int x = 0; x < image.width(); ++x) {
            const auto color = image.pixelColor(x, y);
            if (color.red() > 210 && color.green() > 130 && color.blue() < 155) ++orangePixels;
        }
        QVERIFY2(orangePixels < image.width() * image.height() / 30, "Curve must not acquire a polygon brush fill");
        const auto evidence = qEnvironmentVariable("WB_UI_EVIDENCE_DIR");
        if (!evidence.isEmpty()) {
            QVERIFY(QDir().mkpath(evidence));
            QVERIFY(ui->grab().save(QDir(evidence).filePath(QString("synthetic-dashboard-%1x%2.png").arg(size.width()).arg(size.height()))));
        }
        const int previousWidth = plot->width();
        ui->findChild<QToolButton *>("settingsToggle")->click();
        QCoreApplication::processEvents();
        QVERIFY(plot->width() > previousWidth);
    }
};
QTEST_MAIN(FrontendTest)
#include "frontend_test.moc"
