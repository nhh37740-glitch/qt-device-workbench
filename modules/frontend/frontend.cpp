#include <workbench/contracts.h>
#include <QtCore/QDateTime>
#include <QtCore/QDir>
#include <QtCore/QElapsedTimer>
#include <QtCore/QEvent>
#include <QtCore/QTimer>
#include <QtGui/QPainter>
#include <QtGui/QPainterPath>
#include <QtGui/QShowEvent>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QFormLayout>
#include <QtWidgets/QFrame>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QPlainTextEdit>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QSpinBox>
#include <QtWidgets/QTabWidget>
#include <QtWidgets/QToolButton>
#include <QtWidgets/QVBoxLayout>
#include <algorithm>
#include <cmath>

namespace {
class Plot final : public QWidget {
public:
    explicit Plot(QWidget *parent) : QWidget(parent) {
        setObjectName("samplePlot");
        setMinimumSize(460, 270);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        setProperty("pointCount", 0);
        setProperty("paintCount", 0);
        setAccessibleName(QStringLiteral("温度、湿度、电压曲线，横轴为记录时间"));
        m_refresh = new QTimer(this);
        m_refresh->setObjectName("plotRefreshTimer");
        m_refresh->setSingleShot(true);
        m_clock.start();
        connect(m_refresh, &QTimer::timeout, this, [this] {
            m_lastUpdate = m_clock.elapsed();
            update();
        });
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
        setProperty("paintCount", ++m_paintCount);
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.fillRect(rect(), QColor("#111c2d"));
        const QColor colors[] = {QColor("#ffbd70"), QColor("#46d6c8"), QColor("#ad9bff")};
        const QString names[] = {QStringLiteral("温度"), QStringLiteral("湿度"), QStringLiteral("电压")};
        const QString units[] = {QStringLiteral("°C"), QStringLiteral("%"), QStringLiteral("V")};
        const qreal panelHeight = (height() - 46.0) / 3.0;
        const qint64 firstTime = m_samples.isEmpty() ? 0 : m_samples.first().timestampMs;
        const qint64 lastTime = m_samples.isEmpty() ? 0 : m_samples.last().timestampMs;
        bool orderedTime = lastTime > firstTime;
        for (int i = 1; orderedTime && i < m_samples.size(); ++i)
            orderedTime = m_samples[i].timestampMs >= m_samples[i - 1].timestampMs;
        for (int metric = 0; metric < 3; ++metric) {
            QRectF area(76, 31 + metric * panelHeight, width() - 102.0, panelHeight - 43);
            if (area.width() <= 0 || area.height() <= 0) continue;
            double low = 0, high = 1;
            if (!m_samples.isEmpty()) {
                low = high = value(m_samples.first(), metric);
                for (const auto &sample : m_samples) {
                    low = std::min(low, value(sample, metric));
                    high = std::max(high, value(sample, metric));
                }
                const double padding = std::max((high - low) * 0.12, metric == 2 ? 0.02 : 0.2);
                low -= padding;
                high += padding;
            }
            painter.setPen(colors[metric]);
            auto font = painter.font();
            font.setBold(true);
            painter.setFont(font);
            painter.drawText(QRectF(15, area.top() - 25, width() - 30, 21),
                             Qt::AlignLeft, names[metric] + "  /  " + units[metric]);
            font.setBold(false);
            painter.setFont(font);
            for (int grid = 0; grid <= 4; ++grid) {
                const qreal y = area.top() + area.height() * grid / 4;
                painter.setPen(QPen(QColor("#27374d"), 1, Qt::DotLine));
                painter.drawLine(QPointF(area.left(), y), QPointF(area.right(), y));
                if (grid % 2 == 0) {
                    painter.setPen(QColor("#91a5be"));
                    painter.drawText(QRectF(8, y - 9, 59, 18), Qt::AlignRight | Qt::AlignVCenter,
                        QString::number(high - (high - low) * grid / 4, 'f', metric == 2 ? 3 : 2));
                }
                const qreal x = area.left() + area.width() * grid / 4;
                painter.setPen(QPen(QColor("#213047"), 1, Qt::DotLine));
                painter.drawLine(QPointF(x, area.top()), QPointF(x, area.bottom()));
            }
            QPainterPath path;
            for (int index = 0; index < m_samples.size(); ++index) {
                const qreal fraction = orderedTime
                    ? double(m_samples[index].timestampMs - firstTime) / double(lastTime - firstTime)
                    : double(index) / std::max(1, int(m_samples.size()) - 1);
                const qreal x = area.left() + area.width() * fraction;
                const qreal y = area.bottom() - area.height() * (value(m_samples[index], metric) - low) / (high - low);
                if (index == 0) path.moveTo(x, y); else path.lineTo(x, y);
            }
            painter.save();
            painter.setClipRect(area.adjusted(-3, -3, 3, 3));
            painter.setBrush(Qt::NoBrush);
            painter.setPen(QPen(colors[metric], 1.8));
            painter.drawPath(path);
            if (!m_samples.isEmpty()) {
                painter.setBrush(colors[metric]);
                painter.drawEllipse(path.currentPosition(), 2.7, 2.7);
            }
            painter.restore();
        }
        painter.setPen(QColor("#91a5be"));
        const qreal xWidth = width() - 102.0;
        if (!m_samples.isEmpty()) {
            const auto first = recordTime(m_samples.first());
            const auto last = recordTime(m_samples.last());
            painter.drawText(QRectF(76, height() - 41, xWidth / 2, 18), Qt::AlignLeft,
                             first.toString("HH:mm:ss"));
            painter.drawText(QRectF(76 + xWidth / 2, height() - 41, xWidth / 2, 18), Qt::AlignRight,
                             last.toString("HH:mm:ss"));
        }
        painter.drawText(QRectF(76, height() - 21, xWidth, 18), Qt::AlignRight,
                         QStringLiteral("%1 · 最近 %2 / 1000 条").arg(!m_samples.isEmpty() && m_samples.last().deviceId.startsWith("intel-lab-") ? QStringLiteral("录制时间（时区未注明）") : QStringLiteral("记录时间")).arg(m_samples.size()));
        if (m_samples.isEmpty()) {
            painter.setPen(QColor("#c3d1e3"));
            painter.drawText(rect(), Qt::AlignCenter, QStringLiteral("等待测量数据\n连接设备后开始采集，或在文件设置中载入历史"));
        }
    }
private:
    static QDateTime recordTime(const wb::Sample &sample) {
        return sample.deviceId.startsWith("intel-lab-") ? QDateTime::fromMSecsSinceEpoch(sample.timestampMs, Qt::UTC) : QDateTime::fromMSecsSinceEpoch(sample.timestampMs);
    }
    static double value(const wb::Sample &sample, int metric) {
        return metric == 0 ? sample.temperature : metric == 1 ? sample.humidity : sample.voltage;
    }
    void changed() {
        setProperty("pointCount", m_samples.size());
        if (!m_refresh->isActive())
            m_refresh->start(int(std::max<qint64>(0, 50 - (m_clock.elapsed() - m_lastUpdate))));
    }
    wb::SampleBatch m_samples;
    QTimer *m_refresh;
    QElapsedTimer m_clock;
    qint64 m_lastUpdate = -50;
    int m_paintCount = 0;
};

class Frontend final : public wb::Frontend {
public:
    explicit Frontend(QWidget *parent) : wb::Frontend(parent) {
        setWindowTitle(QStringLiteral("设备测量工作台"));
        setObjectName("deviceWorkbench");
        resize(1280, 860);
        setStyleSheet(QStringLiteral(R"(
            QWidget { background: #0b1220; color: #dce6f3; font-family: 'Microsoft YaHei UI', 'Segoe UI'; font-size: 12px; }
            QLabel { background: transparent; }
            QFrame[card="true"], QFrame#settingsPanel { background: #111c2d; border: 1px solid #25334a; border-radius: 10px; }
            QLabel[role="muted"] { color: #8fa3bc; }
            QLabel[role="value"] { font-size: 25px; font-weight: 600; }
            QLabel#latestSequence { font-size: 21px; }
            QLabel#dashboardTitle { font-size: 21px; font-weight: 600; }
            QLabel#dataSourceLabel { color: #9bb0cb; }
            QLabel[state="neutral"] { color: #8fa3bc; }
            QLabel[state="good"] { color: #46d6c8; }
            QLabel[state="pending"] { color: #ffbd70; }
            QLabel[state="error"], QLabel#errorBanner { color: #ff939d; }
            QPushButton, QToolButton { background: #1a2940; border: 1px solid #314461; border-radius: 6px; padding: 7px 12px; }
            QPushButton:hover, QToolButton:hover { background: #263b56; border-color: #577292; }
            QPushButton:pressed, QToolButton:pressed { background: #101d2e; }
            QPushButton#startButton { background: #168c7f; border-color: #2fb5a6; color: #f3fffd; font-weight: 600; }
            QPushButton#startButton:hover { background: #21a595; }
            QPushButton#stopButton { color: #ffb4bc; }
            QLineEdit, QSpinBox, QComboBox { background: #0c1626; border: 1px solid #2e405b; border-radius: 5px; padding: 6px; selection-background-color: #245a76; }
            QLineEdit:focus, QSpinBox:focus, QComboBox:focus { border-color: #46d6c8; }
            QComboBox QAbstractItemView { background: #16263b; selection-background-color: #245a76; }
            QTabWidget::pane { border: 0; }
            QTabBar::tab { background: #111c2d; color: #8fa3bc; padding: 9px 10px; border-bottom: 2px solid transparent; }
            QTabBar::tab:selected { color: #dce6f3; border-bottom-color: #46d6c8; }
            QPlainTextEdit { background: #0c1626; border: 1px solid #25334a; border-radius: 6px; color: #aabdd5; }
        )"));
        auto root = new QVBoxLayout(this);
        root->setContentsMargins(20, 16, 20, 14);
        root->setSpacing(10);
        auto header = new QHBoxLayout;
        auto brand = new QVBoxLayout;
        auto title = new QLabel(QStringLiteral("设备测量工作台"), this);
        title->setObjectName("dashboardTitle");
        brand->addWidget(title);
        auto subtitle = new QLabel(QStringLiteral("DEVICE WORKBENCH  /  温度 · 湿度 · 电压"), this);
        subtitle->setProperty("role", "muted");
        brand->addWidget(subtitle);
        header->addLayout(brand);
        header->addStretch();
        header->addWidget(button("startButton", QStringLiteral("▶ 开始采集"), this, [this] { emit startRequested(m_interval->value()); }));
        header->addWidget(button("stopButton", QStringLiteral("■ 停止"), this, [this] { emit stopRequested(); }));
        auto settingsToggle = new QToolButton(this);
        settingsToggle->setObjectName("settingsToggle");
        settingsToggle->setText(QStringLiteral("设置"));
        settingsToggle->setCheckable(true);
        settingsToggle->setChecked(true);
        header->addWidget(settingsToggle);
        root->addLayout(header);
        m_dataSource = new QLabel(QStringLiteral("数据来源：连接设备后确认"), this);
        m_dataSource->setObjectName("dataSourceLabel");
        m_dataSource->setWordWrap(true);
        root->addWidget(m_dataSource);
        auto indicators = new QHBoxLayout;
        m_connection = indicator("connectionIndicator", QStringLiteral("设备 · 等待连接"), indicators);
        m_capture = indicator("captureIndicator", QStringLiteral("采集 · 等待命令"), indicators);
        m_recording = indicator("recordingIndicator", QStringLiteral("保存 · 未开始"), indicators);
        m_forward = indicator("forwardingIndicator", QStringLiteral("转发 · 等待连接"), indicators);
        indicators->addStretch();
        root->addLayout(indicators);
        auto cards = new QHBoxLayout;
        cards->setSpacing(10);
        m_temperature = valueCard("latestTemperature", QStringLiteral("温度  /  TEMPERATURE"), QStringLiteral("— °C"), "#ffbd70", cards);
        m_humidity = valueCard("latestHumidity", QStringLiteral("湿度  /  HUMIDITY"), QStringLiteral("— %"), "#46d6c8", cards);
        m_voltage = valueCard("latestVoltage", QStringLiteral("电压  /  VOLTAGE"), QStringLiteral("— V"), "#ad9bff", cards);
        m_sequence = valueCard("latestSequence", QStringLiteral("有效样本  /  序号"), QStringLiteral("—"), "#dce6f3", cards);
        root->addLayout(cards);
        m_device = new QLabel(QStringLiteral("设备：—    记录时间：—"), this);
        m_device->setObjectName("latestDevice");
        m_device->setProperty("role", "muted");
        root->addWidget(m_device);
        auto content = new QHBoxLayout;
        content->setSpacing(14);
        root->addLayout(content, 1);
        auto charts = new QVBoxLayout;
        auto chartHeading = new QLabel(QStringLiteral("测量趋势"), this);
        chartHeading->setStyleSheet("font-weight: 600; font-size: 14px;");
        charts->addWidget(chartHeading);
        m_plot = new Plot(this);
        charts->addWidget(m_plot, 1);
        content->addLayout(charts, 1);
        auto settingsPanel = new QFrame(this);
        settingsPanel->setObjectName("settingsPanel");
        settingsPanel->setFixedWidth(274);
        auto settings = new QVBoxLayout(settingsPanel);
        settings->setContentsMargins(12, 12, 12, 12);
        auto settingsTitle = new QLabel(QStringLiteral("连接与数据设置"), settingsPanel);
        settingsTitle->setStyleSheet("font-weight: 600; font-size: 14px;");
        settings->addWidget(settingsTitle);
        m_settingsTabs = new QTabWidget(settingsPanel);
        m_settingsTabs->setObjectName("settingsTabs");
        settings->addWidget(m_settingsTabs, 1);
        content->addWidget(settingsPanel);
        connect(settingsToggle, &QToolButton::toggled, settingsPanel, &QWidget::setVisible);
        buildSettings();
        m_errorBanner = new QLabel(this);
        m_errorBanner->setObjectName("errorBanner");
        m_errorBanner->setWordWrap(true);
        m_errorBanner->hide();
        root->addWidget(m_errorBanner);
        m_detailsToggle = new QToolButton(this);
        m_detailsToggle->setObjectName("detailsToggle");
        m_detailsToggle->setText(QStringLiteral("事件详情 · 0 个错误"));
        m_detailsToggle->setCheckable(true);
        m_detailsToggle->setToolButtonStyle(Qt::ToolButtonTextOnly);
        root->addWidget(m_detailsToggle, 0, Qt::AlignLeft);
        m_details = new QTabWidget(this);
        m_details->setObjectName("detailsTabs");
        m_details->setFixedHeight(140);
        m_status = new QPlainTextEdit(m_details);
        m_status->setObjectName("statusLog");
        m_status->setReadOnly(true);
        m_status->setMaximumBlockCount(2000);
        m_errors = new QPlainTextEdit(m_details);
        m_errors->setObjectName("errorLog");
        m_errors->setReadOnly(true);
        m_errors->setMaximumBlockCount(2000); // Full session events are retained by the host report.
        m_details->addTab(m_status, QStringLiteral("最近事件"));
        m_details->addTab(m_errors, QStringLiteral("错误记录"));
        root->addWidget(m_details);
        m_details->hide();
        connect(m_detailsToggle, &QToolButton::toggled, m_details, &QWidget::setVisible);
    }

    void showSample(wb::Sample sample) override { display(sample); m_plot->append(sample); }
    void showStatus(QString component, QString detail) override {
        if (component.compare("data", Qt::CaseInsensitive) == 0) { m_metadataDescription = detail; setDataSource(detail); }
        const auto entry = QStringLiteral("[%1] %2：%3").arg(QDateTime::currentDateTime().toString("HH:mm:ss"), component, detail);
        m_status->appendPlainText(entry);
        const QString lower = detail.toLower();
        const bool error = component.contains(QStringLiteral("错误")) || component.contains("error", Qt::CaseInsensitive)
            || lower.contains("failed") || lower.contains("invalid") || lower.contains("timeout")
            || lower.contains("queue_full") || lower.contains("cannot") || lower.contains("corrupt")
            || lower.contains("mismatch") || lower.contains("unsupported") || lower.contains("unmatched")
            || lower.contains("socket:") || lower.contains("malformed");
        if (error) {
            m_errors->appendPlainText(entry);
            ++m_errorCount;
            m_detailsToggle->setText(QStringLiteral("事件详情 · %1 个错误").arg(m_errorCount));
            m_errorBanner->setText(QStringLiteral("最近错误 · %1：%2").arg(component, detail.left(130)));
            m_errorBanner->setToolTip(entry);
            m_errorBanner->show();
        }
        updateIndicators(component, detail, error);
    }
    void showHistory(wb::SampleBatch samples) override {
        m_plot->replace(samples);
        m_historyCount->setText(QStringLiteral("已载入 %1 条；曲线保留最近 1000 条").arg(samples.size()));
        m_historyCount->setProperty("recordCount", samples.size());
        if (!samples.isEmpty()) display(samples.last()); else clearValues();
        showStatus(QStringLiteral("历史"), QStringLiteral("已加载 %1 条记录").arg(samples.size()));
    }
protected:
    void showEvent(QShowEvent *event) override {
        const auto source = property("publicDataDescription").toString();
        if (!source.isEmpty()) setDataSource(source);
        wb::Frontend::showEvent(event);
    }
    bool event(QEvent *event) override {
        if (event->type() == QEvent::DynamicPropertyChange && m_dataSource) {
            const auto source = property("publicDataDescription").toString();
            if (!source.isEmpty()) setDataSource(source);
        }
        return wb::Frontend::event(event);
    }
private:
    void buildSettings() {
        auto device = new QWidget(m_settingsTabs);
        auto deviceLayout = form(device);
        m_host = edit("deviceHost", "127.0.0.1", device);
        m_port = spin("devicePort", 1, 65535, 9101, device);
        m_interval = spin("intervalMs", 10, 60000, 100, device);
        m_interval->setSuffix(" ms");
        deviceLayout->addRow(QStringLiteral("设备地址"), m_host);
        deviceLayout->addRow(QStringLiteral("端口"), m_port);
        deviceLayout->addRow(QStringLiteral("采样间隔"), m_interval);
        deviceLayout->addRow(button("connectButton", QStringLiteral("连接设备"), device, [this] {
            emit connectRequested(m_host->text().trimmed(), quint16(m_port->value()));
        }));
        auto deviceHint = new QLabel(QStringLiteral("连接后，使用顶部按钮开始或停止采集。\n曲线显示数据中的记录时间。"), device);
        deviceHint->setWordWrap(true);
        deviceHint->setProperty("role", "muted");
        deviceLayout->addRow(deviceHint);
        m_settingsTabs->addTab(device, QStringLiteral("设备"));
        auto recording = new QWidget(m_settingsTabs);
        auto recordLayout = form(recording);
        m_recordPath = edit("recordPath", QDir::current().filePath("samples.csv"), recording);
        m_historyPath = edit("historyPath", m_recordPath->text(), recording);
        recordLayout->addRow(QStringLiteral("保存路径"), m_recordPath);
        recordLayout->addRow(button("recordStartButton", QStringLiteral("开始保存 CSV"), recording, [this] { emit recordRequested(m_recordPath->text().trimmed()); }));
        recordLayout->addRow(button("recordStopButton", QStringLiteral("停止保存"), recording, [this] { emit recordStopRequested(); }));
        recordLayout->addRow(QStringLiteral("历史路径"), m_historyPath);
        recordLayout->addRow(button("historyButton", QStringLiteral("载入历史曲线"), recording, [this] { emit historyRequested(m_historyPath->text().trimmed()); }));
        m_historyCount = new QLabel(QStringLiteral("历史记录：0 条"), recording);
        m_historyCount->setObjectName("historyCount");
        m_historyCount->setWordWrap(true);
        m_historyCount->setProperty("role", "muted");
        recordLayout->addRow(m_historyCount);
        m_settingsTabs->addTab(recording, QStringLiteral("文件"));
        auto downstream = new QWidget(m_settingsTabs);
        auto downstreamLayout = form(downstream);
        m_sinkHost = edit("downstreamHost", "127.0.0.1", downstream);
        m_sinkPort = spin("downstreamPort", 1, 65535, 9102, downstream);
        downstreamLayout->addRow(QStringLiteral("下游地址"), m_sinkHost);
        downstreamLayout->addRow(QStringLiteral("端口"), m_sinkPort);
        downstreamLayout->addRow(button("downstreamButton", QStringLiteral("连接下游接收端"), downstream, [this] {
            emit downstreamRequested(m_sinkHost->text().trimmed(), quint16(m_sinkPort->value()));
        }));
        auto downstreamHint = new QLabel(QStringLiteral("保存与转发独立进行。\n下游确认后的状态显示在顶部。"), downstream);
        downstreamHint->setWordWrap(true);
        downstreamHint->setProperty("role", "muted");
        downstreamLayout->addRow(downstreamHint);
        m_settingsTabs->addTab(downstream, QStringLiteral("转发"));
        auto fault = new QWidget(m_settingsTabs);
        auto faultLayout = form(fault);
        m_fault = new QComboBox(fault);
        m_fault->setObjectName("faultMode");
        m_fault->addItem(QStringLiteral("正常"), "none");
        m_fault->addItem(QStringLiteral("分片传输"), "fragment");
        m_fault->addItem(QStringLiteral("错误报文"), "malformed");
        m_fault->addItem(QStringLiteral("连接中断"), "disconnect");
        m_fault->addItem(QStringLiteral("延迟传输"), "delay");
        m_every = spin("faultEvery", 1, 1000000, 10, fault);
        faultLayout->addRow(QStringLiteral("故障模式"), m_fault);
        faultLayout->addRow(QStringLiteral("每 N 条触发"), m_every);
        faultLayout->addRow(button("faultButton", QStringLiteral("应用模拟设置"), fault, [this] {
            emit simulationRequested(m_fault->currentData().toString(), m_every->value());
        }));
        auto faultHint = new QLabel(QStringLiteral("用于验证异常处理；不改变测量字段含义。"), fault);
        faultHint->setWordWrap(true);
        faultHint->setProperty("role", "muted");
        faultLayout->addRow(faultHint);
        m_settingsTabs->addTab(fault, QStringLiteral("模拟"));
    }
    static QFormLayout *form(QWidget *parent) {
        auto layout = new QFormLayout(parent);
        layout->setContentsMargins(0, 16, 0, 0);
        layout->setSpacing(10);
        layout->setRowWrapPolicy(QFormLayout::WrapAllRows);
        layout->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
        return layout;
    }
    static QLineEdit *edit(const char *name, const QString &value, QWidget *parent) {
        auto widget = new QLineEdit(value, parent);
        widget->setObjectName(name);
        widget->setMinimumWidth(0);
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
    QLabel *valueCard(const char *name, const QString &title, const QString &text, const QString &color, QHBoxLayout *layout) {
        auto card = new QFrame(this);
        card->setProperty("card", true);
        card->setMinimumWidth(0);
        auto contents = new QVBoxLayout(card);
        contents->setContentsMargins(14, 10, 14, 10);
        auto caption = new QLabel(title, card);
        caption->setProperty("role", "muted");
        contents->addWidget(caption);
        auto label = new QLabel(text, card);
        label->setObjectName(name);
        label->setProperty("role", "value");
        label->setStyleSheet("color: " + color + ';');
        contents->addWidget(label);
        layout->addWidget(card, 1);
        return label;
    }
    QLabel *indicator(const char *name, const QString &text, QHBoxLayout *layout) {
        auto label = new QLabel(text, this);
        label->setObjectName(name);
        label->setProperty("state", "neutral");
        layout->addWidget(label);
        layout->addSpacing(18);
        return label;
    }
    static void state(QLabel *label, const QString &text, const char *kind) {
        label->setText(text);
        label->setProperty("state", kind);
        // Property selectors need repolishing after a real event changes state.
        label->setStyleSheet(QString("color: %1;").arg(QString(kind) == "good" ? "#46d6c8" : QString(kind) == "error" ? "#ff939d" : QString(kind) == "pending" ? "#ffbd70" : "#8fa3bc"));
    }
    void updateIndicators(const QString &component, const QString &detail, bool error) {
        const auto lower = detail.toLower();
        const bool source = component.contains(QStringLiteral("接收")) || component.contains("source", Qt::CaseInsensitive);
        const bool store = component.contains(QStringLiteral("保存")) || component.contains("store", Qt::CaseInsensitive);
        const bool push = component.contains(QStringLiteral("下游")) || component.contains("push", Qt::CaseInsensitive);
        if (source) {
            if (lower.startsWith("device connected")) state(m_connection, QStringLiteral("设备 · 已连接"), "good");
            else if (lower.startsWith("device disconnected")) state(m_connection, QStringLiteral("设备 · 中断，正在重连"), "pending");
            else if (lower.startsWith("connecting to device")) state(m_connection, QStringLiteral("设备 · 正在连接"), "pending");
            else if (lower == "source shutdown") state(m_connection, QStringLiteral("设备 · 已关闭"), "neutral");
            else if (error && lower.startsWith("device socket:")) state(m_connection, QStringLiteral("设备 · 连接错误"), "error");
        }
        if (source && lower == "replay_finished") state(m_capture, QStringLiteral("回放已结束"), "neutral");
        else if (lower.contains("(started)") || (component == QStringLiteral("设备回复") && lower.endsWith(": started")))
            state(m_capture, QStringLiteral("采集 · 运行中"), "good");
        else if (lower.contains("(stopped)") || lower == "capture stopped" || (component == QStringLiteral("设备回复") && lower.endsWith(": stopped")))
            state(m_capture, QStringLiteral("采集 · 已停止"), "neutral");
        else if (source && lower.startsWith("start requested")) state(m_capture, QStringLiteral("采集 · 等待连接"), "pending");
        if (store) {
            if (lower == "recording stopped") state(m_recording, QStringLiteral("保存 · 已停止"), "neutral");
            else if (lower == "recording failed" || error) state(m_recording, QStringLiteral("保存 · 错误，请查看详情"), "error");
            else if (lower.startsWith("recording ")) state(m_recording, QStringLiteral("保存 · 正在写入 CSV"), "good");
        }
        if (push) {
            if (lower == "downstream connected") state(m_forward, QStringLiteral("转发 · 下游已连接"), "good");
            else if (lower.startsWith("downstream disconnected")) state(m_forward, QStringLiteral("转发 · 中断，正在重连"), "pending");
            else if (lower == "downstream connecting") state(m_forward, QStringLiteral("转发 · 正在连接"), "pending");
            else if (lower == "downstream stopped") state(m_forward, QStringLiteral("转发 · 已停止"), "neutral");
            else if (error) state(m_forward, QStringLiteral("转发 · 错误，请查看详情"), "error");
        }
        if (component == QStringLiteral("待下游确认")) {
            bool valid = false;
            const int count = detail.toInt(&valid);
            if (valid) m_forward->setToolTip(QStringLiteral("待下游确认 %1 条").arg(count));
        }
    }
    void setDataSource(const QString &description) {
        m_dataSource->setText(QStringLiteral("数据来源：%1").arg(description));
        m_dataSource->setToolTip(description);
    }
    void updateDataSource(const wb::Sample &sample) {
        const auto explicitDescription = property("publicDataDescription").toString();
        if (!explicitDescription.isEmpty()) setDataSource(explicitDescription);
        else if (!m_metadataDescription.isEmpty()) setDataSource(m_metadataDescription);
        else if (sample.deviceId.startsWith("intel-lab-")) setDataSource(QStringLiteral("公开实测数据回放 · Intel Berkeley / MIT"));
        else setDataSource(QStringLiteral("设备数据 · %1").arg(sample.deviceId));
    }
    void display(const wb::Sample &sample) {
        updateDataSource(sample);
        m_temperature->setText(QStringLiteral("%1 °C").arg(sample.temperature, 0, 'f', 2));
        m_humidity->setText(QStringLiteral("%1 %").arg(sample.humidity, 0, 'f', 2));
        m_voltage->setText(QStringLiteral("%1 V").arg(sample.voltage, 0, 'f', 3));
        m_sequence->setText(QString::number(sample.sequence));
        m_sequence->setProperty("sequence", sample.sequence);
        const bool publicRecord = sample.deviceId.startsWith("intel-lab-");
        const auto clock = publicRecord ? QDateTime::fromMSecsSinceEpoch(sample.timestampMs, Qt::UTC) : QDateTime::fromMSecsSinceEpoch(sample.timestampMs);
        m_device->setText(QStringLiteral("设备：%1    %2：%3").arg(sample.deviceId,
            publicRecord ? QStringLiteral("录制时间（时区未注明）") : QStringLiteral("记录时间"),
            clock.toString("yyyy-MM-dd HH:mm:ss.zzz")));
    }
    void clearValues() {
        m_temperature->setText(QStringLiteral("— °C"));
        m_humidity->setText(QStringLiteral("— %"));
        m_voltage->setText(QStringLiteral("— V"));
        m_sequence->setText(QStringLiteral("—"));
        m_sequence->setProperty("sequence", 0);
        m_device->setText(QStringLiteral("设备：—    记录时间：—"));
    }
    QLineEdit *m_host, *m_sinkHost, *m_recordPath, *m_historyPath;
    QSpinBox *m_port, *m_sinkPort, *m_interval, *m_every;
    QComboBox *m_fault;
    QLabel *m_temperature, *m_humidity, *m_voltage, *m_sequence, *m_device, *m_historyCount;
    QLabel *m_dataSource = nullptr;
    QLabel *m_connection, *m_capture, *m_recording, *m_forward, *m_errorBanner;
    QPlainTextEdit *m_status, *m_errors;
    QTabWidget *m_settingsTabs, *m_details;
    QToolButton *m_detailsToggle;
    QString m_metadataDescription;
    int m_errorCount = 0;
    Plot *m_plot;
};
}
extern "C" WB_FRONTEND_API wb::Frontend *wb_create_frontend(QWidget *parent) { return new Frontend(parent); }
