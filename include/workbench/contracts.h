#pragma once
#include <QtCore/QObject>
#include <QtCore/QJsonObject>
#include <QtCore/QVector>
#include <QtWidgets/QWidget>

#if defined(WORKBENCH_CONTRACTS_BUILD)
#define WB_API Q_DECL_EXPORT
#else
#define WB_API Q_DECL_IMPORT
#endif

namespace wb {
struct Sample {
    QString deviceId;
    qint64 sequence = 0;
    qint64 timestampMs = 0;
    double temperature = 0;
    double humidity = 0;
    double voltage = 0;
};
using SampleBatch = QVector<Sample>;
WB_API void registerTypes();
WB_API QJsonObject sampleToJson(const Sample &sample);
WB_API bool sampleFromJson(const QJsonObject &json, Sample &sample, QString &error);

class WB_API WireCodec : public QObject {
    Q_OBJECT
public:
    explicit WireCodec(QObject *parent=nullptr);
    virtual QByteArray encode(const QJsonObject &message) const = 0;
public slots:
    virtual void feed(QByteArray bytes) = 0;
    virtual void reset() = 0;
signals:
    void message(QJsonObject message);
    void error(QString detail);
};
class WB_API Simulator : public QObject {
    Q_OBJECT
public:
    explicit Simulator(QObject *parent=nullptr);
public slots:
    virtual void listen(QString address, quint16 port) = 0;
    virtual void shutdown() = 0;
signals:
    void listening(quint16 port);
    void status(QString detail);
    void error(QString detail);
};
class WB_API DeviceSource : public QObject {
    Q_OBJECT
public:
    explicit DeviceSource(QObject *parent=nullptr);
public slots:
    virtual void connectDevice(QString host, quint16 port) = 0;
    virtual void startMeasurements(int intervalMs) = 0;
    virtual void stopMeasurements() = 0;
    virtual void setSimulation(QString mode, int every) = 0;
    virtual void shutdown() = 0;
signals:
    void sampleReady(wb::Sample sample);
    void status(QString detail);
    void error(QString detail);
    void commandResult(QString id, bool ok, QString detail);
};
class WB_API Frontend : public QWidget {
    Q_OBJECT
public:
    explicit Frontend(QWidget *parent=nullptr);
public slots:
    virtual void showSample(wb::Sample sample) = 0;
    virtual void showStatus(QString component, QString detail) = 0;
    virtual void showHistory(wb::SampleBatch samples) = 0;
signals:
    void connectRequested(QString host, quint16 port);
    void startRequested(int intervalMs);
    void stopRequested();
    void simulationRequested(QString mode, int every);
    void recordRequested(QString path);
    void recordStopRequested();
    void historyRequested(QString path);
    void downstreamRequested(QString host, quint16 port);
};
class WB_API RecordStore : public QObject {
    Q_OBJECT
public:
    explicit RecordStore(QObject *parent=nullptr);
public slots:
    virtual void begin(QString path) = 0;
    virtual void append(wb::Sample sample) = 0;
    virtual void stop() = 0;
    virtual void load(QString path) = 0;
    virtual void shutdown() = 0;
signals:
    void stored(qint64 sequence);
    void historyReady(wb::SampleBatch samples);
    void status(QString detail);
    void error(QString detail);
};
class WB_API ResultPush : public QObject {
    Q_OBJECT
public:
    explicit ResultPush(QObject *parent=nullptr);
public slots:
    virtual void connectSink(QString host, quint16 port) = 0;
    virtual void enqueue(wb::Sample sample) = 0;
    virtual void shutdown() = 0;
signals:
    void delivered(qint64 sequence);
    void backlogChanged(int count);
    void status(QString detail);
    void error(QString detail);
};
class WB_API ResultReceiver : public QObject {
    Q_OBJECT
public:
    explicit ResultReceiver(QObject *parent=nullptr);
public slots:
    virtual void listen(QString address, quint16 port, QString outputPath) = 0;
    virtual void shutdown() = 0;
signals:
    void listening(quint16 port);
    void received(wb::Sample sample);
    void status(QString detail);
    void error(QString detail);
};
}
Q_DECLARE_METATYPE(wb::Sample)
Q_DECLARE_METATYPE(wb::SampleBatch)

// Independent binary boundaries, exported by their respective module DLLs.
#ifdef WB_WIRE_BUILD
#define WB_WIRE_API Q_DECL_EXPORT
#else
#define WB_WIRE_API Q_DECL_IMPORT
#endif
#ifdef WB_SIMULATOR_BUILD
#define WB_SIMULATOR_API Q_DECL_EXPORT
#else
#define WB_SIMULATOR_API Q_DECL_IMPORT
#endif
#ifdef WB_SOURCE_BUILD
#define WB_SOURCE_API Q_DECL_EXPORT
#else
#define WB_SOURCE_API Q_DECL_IMPORT
#endif
#ifdef WB_FRONTEND_BUILD
#define WB_FRONTEND_API Q_DECL_EXPORT
#else
#define WB_FRONTEND_API Q_DECL_IMPORT
#endif
#ifdef WB_STORE_BUILD
#define WB_STORE_API Q_DECL_EXPORT
#else
#define WB_STORE_API Q_DECL_IMPORT
#endif
#ifdef WB_PUSH_BUILD
#define WB_PUSH_API Q_DECL_EXPORT
#else
#define WB_PUSH_API Q_DECL_IMPORT
#endif
#ifdef WB_RECEIVER_BUILD
#define WB_RECEIVER_API Q_DECL_EXPORT
#else
#define WB_RECEIVER_API Q_DECL_IMPORT
#endif
extern "C" {
WB_WIRE_API wb::WireCodec *wb_create_wire(QObject *parent);
WB_SIMULATOR_API wb::Simulator *wb_create_simulator(QObject *parent);
WB_SOURCE_API wb::DeviceSource *wb_create_source(QObject *parent);
WB_FRONTEND_API wb::Frontend *wb_create_frontend(QWidget *parent);
WB_STORE_API wb::RecordStore *wb_create_store(QObject *parent);
WB_PUSH_API wb::ResultPush *wb_create_push(QObject *parent);
WB_RECEIVER_API wb::ResultReceiver *wb_create_receiver(QObject *parent);
}
