#include <workbench/contracts.h>
#include <QtCore/QDateTime>
#include <QtCore/QDir>
#include <QtCore/QTimer>
#include <QtGui/QPainter>
#include <QtGui/QPainterPath>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QFormLayout>
#include <QtWidgets/QGroupBox>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QPlainTextEdit>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QSpinBox>
#include <QtWidgets/QVBoxLayout>
#include <algorithm>
#include <cmath>

namespace {
class Plot final : public QWidget {
public:
    explicit Plot(QWidget *parent) : QWidget(parent) {
        setObjectName("samplePlot");
        setMinimumSize(520, 260);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        setProperty("pointCount", 0);
        setAccessibleName(QStringLiteral("温度、湿度、电压实时曲线"));
        auto timer = new QTimer(this);
        timer->setInterval(50);
        connect(timer, &QTimer::timeout, this, [this] {
            if (m_dirty) { m_dirty = false; update(); }
        });
        timer->start();
    }
    void append(const wb::Sample &sample) {
        if (m_samples.size() >= 1000) m_samples.removeFirst();
        m_samples.append(sample);
        changed();
    }
    void replace(const wb::SampleBatch &samples) {
        m_samples = samples.mid(std::max(0, int(samples.size()) - 1000));
        changed();
    }
protected:
    void paintEvent(QPaintEvent *) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.fillRect(rect(), QColor("#111c2e"));
        const QColor colors[] = {QColor("#ffb454"), QColor("#47d7cb"), QColor("#a99cff")};
        const QString names[] = {QStringLiteral("温度 °C"), QStringLiteral("湿度 %"), QStringLiteral("电压 V")};
        const qreal panelHeight = (height() - 28.0) / 3.0;
        for (int metric = 0; metric < 3; ++metric) {
            QRectF area(76, 12 + metric * panelHeight, width() - 98.0, panelHeight - 24);
            if (area.width() <= 0 || area.height() <= 0) continue;
            double low = 0, high = 1;
            if (!m_samples.isEmpty()) {
                low = high = value(m_samples.first(), metric);
                for (const auto &sample : m_samples) {
                    low = std::min(low, value(sample, metric));
                    high = std::max(high, value(sample, metric));
                }
                const double padding = std::max((high - low) * 0.1, metric == 2 ? 0.05 : 0.5);
                low -= padding;
                high += padding;
            }
            painter.setPen(QColor("#26374f"));
            for (int grid = 0; grid <= 4; ++grid) {
                const qreal y = area.top() + area.height() * grid / 4;
                painter.drawLine(QPointF(area.left(), y), QPointF(area.right(), y));
            }
            painter.setPen(colors[metric]);
            painter.drawText(QRectF(8, area.top(), 65, 18), Qt::AlignLeft, names[metric]);
            painter.setPen(QColor("#a7b9d0"));
            painter.drawText(QRectF(8, area.top() + 19, 63, 18), Qt::AlignLeft, QString::number(high, 'f', 2));
            painter.drawText(QRectF(8, area.bottom() - 18, 63, 18), Qt::AlignLeft, QString::number(low, 'f', 2));
            QPainterPath path;
            for (int index = 0; index < m_samples.size(); ++index) {
                const qreal x = area.left() + area.width() * index / std::max(1, int(m_samples.size()) - 1);
                const qreal y = area.bottom() - area.height() * (value(m_samples[index], metric) - low) / (high - low);
                if (index == 0) path.moveTo(x, y); else path.lineTo(x, y);
            }
            painter.setPen(QPen(colors[metric], 1.7));
            painter.drawPath(path);
            if (m_samples.size() == 1) painter.drawEllipse(path.currentPosition(), 3, 3);
        }
        painter.setPen(QColor("#a7b9d0"));
        painter.drawText(QRectF(76, height() - 23, width() - 98, 20), Qt::AlignRight,
                         QStringLiteral("按采样顺序 · %1 / 1000 点").arg(m_samples.size()));
        if (m_samples.isEmpty()) {
            painter.setPen(QColor("#dce5f2"));
            painter.drawText(rect(), Qt::AlignCenter, QStringLiteral("等待设备数据，或加载历史记录"));
        }
    }
private:
    static double value(const wb::Sample &sample, int metric) {
        if (metric == 0) return sample.temperature;
        if (metric == 1) return sample.humidity;
        return sample.voltage;
    }
    void changed() { setProperty("pointCount", m_samples.size()); m_dirty = true; }
    wb::SampleBatch m_samples;
    bool m_dirty = true;
};

class Frontend final : public wb::Frontend {
public:
    explicit Frontend(QWidget *parent) : wb::Frontend(parent) {
        setWindowTitle(QStringLiteral("设备测量工作台"));
        setObjectName("deviceWorkbench");
        resize(1100, 780);
        auto root = new QVBoxLayout(this);
        auto title = new QLabel(QStringLiteral("设备测量工作台 · 接收 / 显示 / 保存 / 转发"), this);
        title->setStyleSheet("font-size: 19px; font-weight: 600; padding: 8px;");
        root->addWidget(title);
        auto controls = new QHBoxLayout;
        root->addLayout(controls);

        auto device = new QGroupBox(QStringLiteral("设备与采集"), this);
        auto deviceLayout = new QFormLayout(device);
        controls->addWidget(device);
        m_host = edit("deviceHost", "127.0.0.1", device);
        m_port = spin("devicePort", 1, 65535, 9101, device);
        m_interval = spin("intervalMs", 10, 60000, 100, device);
        m_interval->setSuffix(QStringLiteral(" ms"));
        deviceLayout->addRow(QStringLiteral("设备地址"), m_host);
        deviceLayout->addRow(QStringLiteral("设备端口"), m_port);
        deviceLayout->addRow(QStringLiteral("采样间隔"), m_interval);
        auto acquisition = new QHBoxLayout;
        deviceLayout->addRow(acquisition);
        acquisition->addWidget(button("connectButton", QStringLiteral("连接设备"), device, [this] {
            emit connectRequested(m_host->text().trimmed(), quint16(m_port->value()));
        }));
        acquisition->addWidget(button("startButton", QStringLiteral("开始采集"), device, [this] { emit startRequested(m_interval->value()); }));
        acquisition->addWidget(button("stopButton", QStringLiteral("停止采集"), device, [this] { emit stopRequested(); }));
        m_fault = new QComboBox(device);
        m_fault->setObjectName("faultMode");
        m_fault->addItem(QStringLiteral("正常"), "none");
        m_fault->addItem(QStringLiteral("分片传输"), "fragment");
        m_fault->addItem(QStringLiteral("错误报文"), "malformed");
        m_fault->addItem(QStringLiteral("连接中断"), "disconnect");
        m_fault->addItem(QStringLiteral("延迟传输"), "delay");
        m_every = spin("faultEvery", 1, 1000000, 10, device);
        deviceLayout->addRow(QStringLiteral("模拟故障"), m_fault);
        deviceLayout->addRow(QStringLiteral("每 N 条触发"), m_every);
        deviceLayout->addRow(button("faultButton", QStringLiteral("应用模拟设置"), device, [this] {
            emit simulationRequested(m_fault->currentData().toString(), m_every->value());
        }));

        auto recording = new QGroupBox(QStringLiteral("文件与历史"), this);
        auto recordLayout = new QFormLayout(recording);
        controls->addWidget(recording);
        m_recordPath = edit("recordPath", QDir::current().filePath("samples.csv"), recording);
        m_historyPath = edit("historyPath", m_recordPath->text(), recording);
        recordLayout->addRow(QStringLiteral("保存文件"), m_recordPath);
        auto recordButtons = new QHBoxLayout;
        recordLayout->addRow(recordButtons);
        recordButtons->addWidget(button("recordStartButton", QStringLiteral("开始保存"), recording, [this] {
            emit recordRequested(m_recordPath->text().trimmed());
        }));
        recordButtons->addWidget(button("recordStopButton", QStringLiteral("停止保存"), recording, [this] { emit recordStopRequested(); }));
        recordLayout->addRow(QStringLiteral("历史文件"), m_historyPath);
        recordLayout->addRow(button("historyButton", QStringLiteral("加载历史"), recording, [this] {
            emit historyRequested(m_historyPath->text().trimmed());
        }));
        m_historyCount = new QLabel(QStringLiteral("历史记录：0 条"), recording);
        m_historyCount->setObjectName("historyCount");
        recordLayout->addRow(m_historyCount);

        auto downstream = new QGroupBox(QStringLiteral("结果转发"), this);
        auto downstreamLayout = new QFormLayout(downstream);
        controls->addWidget(downstream);
        m_sinkHost = edit("downstreamHost", "127.0.0.1", downstream);
        m_sinkPort = spin("downstreamPort", 1, 65535, 9102, downstream);
        downstreamLayout->addRow(QStringLiteral("下游地址"), m_sinkHost);
        downstreamLayout->addRow(QStringLiteral("下游端口"), m_sinkPort);
        downstreamLayout->addRow(button("downstreamButton", QStringLiteral("连接下游"), downstream, [this] {
            emit downstreamRequested(m_sinkHost->text().trimmed(), quint16(m_sinkPort->value()));
        }));

        auto latest = new QHBoxLayout;
        root->addLayout(latest);
        m_temperature = valueLabel("latestTemperature", QStringLiteral("温度：— °C"), latest);
        m_humidity = valueLabel("latestHumidity", QStringLiteral("湿度：— %"), latest);
        m_voltage = valueLabel("latestVoltage", QStringLiteral("电压：— V"), latest);
        m_sequence = valueLabel("latestSequence", QStringLiteral("序号：—"), latest);
        m_device = new QLabel(QStringLiteral("设备：—　时间：—"), this);
        m_device->setObjectName("latestDevice");
        root->addWidget(m_device);
        m_plot = new Plot(this);
        root->addWidget(m_plot, 1);
        m_status = new QPlainTextEdit(this);
        m_status->setObjectName("statusLog");
        m_status->setReadOnly(true);
        m_status->setMaximumBlockCount(200);
        m_status->setMaximumHeight(145);
        m_status->setPlaceholderText(QStringLiteral("连接、命令确认、保存、转发与错误状态"));
        root->addWidget(m_status);
    }

    void showSample(wb::Sample sample) override { display(sample); m_plot->append(sample); }
    void showStatus(QString component, QString detail) override {
        m_status->appendPlainText(QStringLiteral("[%1] %2：%3")
            .arg(QDateTime::currentDateTime().toString("HH:mm:ss"), component, detail));
    }
    void showHistory(wb::SampleBatch samples) override {
        m_plot->replace(samples);
        m_historyCount->setText(QStringLiteral("历史记录：%1 条（曲线最多显示 1000 条）").arg(samples.size()));
        m_historyCount->setProperty("recordCount", samples.size());
        if (!samples.isEmpty()) display(samples.last());
        else clearValues();
        showStatus(QStringLiteral("历史"), QStringLiteral("已加载 %1 条记录").arg(samples.size()));
    }

private:
    static QLineEdit *edit(const char *name, const QString &value, QWidget *parent) {
        auto widget = new QLineEdit(value, parent);
        widget->setObjectName(name);
        return widget;
    }
    static QSpinBox *spin(const char *name, int low, int high, int value, QWidget *parent) {
        auto widget = new QSpinBox(parent);
        widget->setObjectName(name);
        widget->setRange(low, high);
        widget->setValue(value);
        return widget;
    }
    template<class F> QPushButton *button(const char *name, const QString &text, QWidget *parent, F callback) {
        auto widget = new QPushButton(text, parent);
        widget->setObjectName(name);
        connect(widget, &QPushButton::clicked, this, callback);
        return widget;
    }
    QLabel *valueLabel(const char *name, const QString &text, QHBoxLayout *layout) {
        auto widget = new QLabel(text, this);
        widget->setObjectName(name);
        widget->setStyleSheet("font-size: 18px; font-weight: 600; padding: 10px; background: #edf2f8; color: #182a40;");
        layout->addWidget(widget);
        return widget;
    }
    void display(const wb::Sample &sample) {
        m_temperature->setText(QStringLiteral("温度：%1 °C").arg(sample.temperature, 0, 'f', 2));
        m_humidity->setText(QStringLiteral("湿度：%1 %").arg(sample.humidity, 0, 'f', 2));
        m_voltage->setText(QStringLiteral("电压：%1 V").arg(sample.voltage, 0, 'f', 3));
        m_sequence->setText(QStringLiteral("序号：%1").arg(sample.sequence));
        m_sequence->setProperty("sequence", sample.sequence);
        m_device->setText(QStringLiteral("设备：%1　时间：%2").arg(sample.deviceId,
            QDateTime::fromMSecsSinceEpoch(sample.timestampMs).toString("yyyy-MM-dd HH:mm:ss.zzz")));
    }
    void clearValues() {
        m_temperature->setText(QStringLiteral("温度：— °C"));
        m_humidity->setText(QStringLiteral("湿度：— %"));
        m_voltage->setText(QStringLiteral("电压：— V"));
        m_sequence->setText(QStringLiteral("序号：—"));
        m_sequence->setProperty("sequence", 0);
        m_device->setText(QStringLiteral("设备：—　时间：—"));
    }
    QLineEdit *m_host, *m_sinkHost, *m_recordPath, *m_historyPath;
    QSpinBox *m_port, *m_sinkPort, *m_interval, *m_every;
    QComboBox *m_fault;
    QLabel *m_temperature, *m_humidity, *m_voltage, *m_sequence, *m_device, *m_historyCount;
    QPlainTextEdit *m_status;
    Plot *m_plot;
};
}

extern "C" WB_FRONTEND_API wb::Frontend *wb_create_frontend(QWidget *parent) { return new Frontend(parent); }
