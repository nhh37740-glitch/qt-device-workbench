#include <workbench/contracts.h>
#include <QtWidgets/QApplication>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QSpinBox>
#include <QtCore/QCommandLineParser>
#include <QtCore/QThread>
#include <QtCore/QTimer>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QDir>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonArray>
#include <QtCore/QTextStream>
#include <QtGui/QScreen>
#include <atomic>
#include <functional>
#include <type_traits>

int main(int argc,char **argv) {
    for(int i=1;i<argc;++i) if(QByteArray(argv[i])=="--headless") {
        qputenv("QT_QPA_PLATFORM","offscreen");
        qputenv("QT_QPA_FONTDIR",(qEnvironmentVariable("SystemRoot","C:/Windows")+"/Fonts").toUtf8());
    }
    QApplication app(argc,argv);
    app.setApplicationName("device-workbench"); app.setApplicationVersion(QString::fromLatin1(WB_APP_VERSION));
    wb::registerTypes();
    QCommandLineParser p;p.addHelpOption();p.addVersionOption();
    auto option=[&](const char *name,const char *description,const char *valueName="",const char *defaultValue=""){
        p.addOption(QCommandLineOption(QString::fromLatin1(name),QString::fromUtf8(description),QString::fromLatin1(valueName),QString::fromLatin1(defaultValue)));
    };
    option("headless","Run the actual GUI offscreen for automated validation");
    option("capture","Connect and start capturing automatically");
    option("device-host","Device address","host","127.0.0.1");option("device-port","Device port","port","9101");
    option("sink-host","Downstream address","host","127.0.0.1");option("sink-port","Downstream port","port","9102");
    option("interval-ms","Measurement interval","ms","100");option("duration-ms","Quit after duration","ms","0");
    option("record","CSV output file","path");option("report","Write verification report JSON on exit","path");
    option("screenshot","Save rendered GUI screenshot","path");option("fault","none|fragment|malformed|disconnect|delay","mode","none");
    option("data-description","Explicit description of the connected device's data source","text");
    option("quit-after-replay","Exit after a finite replay is fully saved and confirmed downstream; requires --capture and --record");
    option("fault-every","Apply fault every Nth sample","n","7");
    p.process(app);
    bool okDevice=false,okSink=false,okInterval=false,okDuration=false,okEvery=false;
    const int devicePort=p.value("device-port").toInt(&okDevice),sinkPort=p.value("sink-port").toInt(&okSink),interval=p.value("interval-ms").toInt(&okInterval),duration=p.value("duration-ms").toInt(&okDuration),every=p.value("fault-every").toInt(&okEvery);
    if(!okDevice||devicePort<1||devicePort>65535||!okSink||sinkPort<1||sinkPort>65535||!okInterval||interval<10||interval>60000||!okDuration||duration<0||!okEvery||every<1){qCritical("Invalid command line port/interval/duration/fault frequency");return 2;}
    if(!QStringList{"none","fragment","malformed","disconnect","delay"}.contains(p.value("fault"))){qCritical("Invalid fault mode");return 2;}
    if(p.isSet("quit-after-replay") && (!p.isSet("capture") || !p.isSet("record"))){qCritical("--quit-after-replay requires --capture and --record");return 2;}
    auto *ui=wb_create_frontend(nullptr);
    auto *source=wb_create_source(nullptr);
    auto *store=wb_create_store(nullptr);
    auto *push=wb_create_push(nullptr);
    QThread receiveThread,fileThread,pushThread;
    receiveThread.setObjectName("receive-and-check");fileThread.setObjectName("save-and-read");pushThread.setObjectName("send-downstream");
    source->moveToThread(&receiveThread);store->moveToThread(&fileThread);push->moveToThread(&pushThread);
    QObject::connect(&receiveThread,&QThread::finished,source,&QObject::deleteLater);
    QObject::connect(&fileThread,&QThread::finished,store,&QObject::deleteLater);
    QObject::connect(&pushThread,&QThread::finished,push,&QObject::deleteLater);
    const auto queued=Qt::QueuedConnection;
    QObject::connect(ui,&wb::Frontend::connectRequested,source,&wb::DeviceSource::connectDevice,queued);
    QObject::connect(ui,&wb::Frontend::startRequested,source,&wb::DeviceSource::startMeasurements,queued);
    QObject::connect(ui,&wb::Frontend::stopRequested,source,&wb::DeviceSource::stopMeasurements,queued);
    QObject::connect(ui,&wb::Frontend::simulationRequested,source,&wb::DeviceSource::setSimulation,queued);
    QObject::connect(ui,&wb::Frontend::recordRequested,store,&wb::RecordStore::begin,queued);
    QObject::connect(ui,&wb::Frontend::recordStopRequested,store,&wb::RecordStore::stop,queued);
    QObject::connect(ui,&wb::Frontend::historyRequested,store,&wb::RecordStore::load,queued);
    QObject::connect(ui,&wb::Frontend::downstreamRequested,push,&wb::ResultPush::connectSink,queued);
    QObject::connect(source,&wb::DeviceSource::sampleReady,ui,&wb::Frontend::showSample,queued);
    QObject::connect(source,&wb::DeviceSource::sampleReady,store,&wb::RecordStore::append,queued);
    QObject::connect(source,&wb::DeviceSource::sampleReady,push,&wb::ResultPush::enqueue,queued);
    QObject::connect(store,&wb::RecordStore::historyReady,ui,&wb::Frontend::showHistory,queued);
    QJsonArray errors; qint64 samples=0,stored=0,delivered=0,acks=0;
    bool replayFinished=false,finishScheduled=false;
    auto snapshot=[&]{
        if(p.isSet("screenshot")){QDir().mkpath(QFileInfo(p.value("screenshot")).absolutePath());if(!ui->grab().save(p.value("screenshot")))errors.append("screenshot save failed");}
    };
    auto maybeFinish=[&]{
        if(p.isSet("quit-after-replay") && replayFinished && samples>0 && stored==samples && delivered==samples && !finishScheduled){
            finishScheduled=true;
            QTimer::singleShot(60,ui,[&]{snapshot();app.quit();});
        }
    };
    bool uiAffinity=true;std::atomic_bool sourceAffinity{true},storeAffinity{true},pushAffinity{true};
    QObject::connect(source,&wb::DeviceSource::sampleReady,source,[&](wb::Sample){if(QThread::currentThread()!=&receiveThread)sourceAffinity=false;});
    QObject::connect(store,&wb::RecordStore::stored,store,[&](qint64){if(QThread::currentThread()!=&fileThread)storeAffinity=false;});
    QObject::connect(push,&wb::ResultPush::delivered,push,[&](qint64){if(QThread::currentThread()!=&pushThread)pushAffinity=false;});
    QObject::connect(source,&wb::DeviceSource::sampleReady,ui,[&](wb::Sample){++samples;uiAffinity &= QThread::currentThread()==app.thread();},queued);
    QObject::connect(store,&wb::RecordStore::stored,ui,[&](qint64){++stored;maybeFinish();},queued);
    QObject::connect(push,&wb::ResultPush::delivered,ui,[&](qint64){++delivered;maybeFinish();},queued);
    QObject::connect(source,&wb::DeviceSource::commandResult,ui,[&](QString id,bool ok,QString detail){if(ok)++acks;ui->showStatus("设备回复",id+": "+detail);},queued);
    auto bindStatus=[&](auto *worker,const QString &name){
        using T=std::remove_pointer_t<decltype(worker)>;
        QObject::connect(worker,&T::status,ui,[&,name](QString detail){ui->showStatus(name,detail);if(name=="接收测量值" && detail=="replay_finished"){replayFinished=true;maybeFinish();}},queued);
        QObject::connect(worker,&T::error,ui,[&,name](QString detail){errors.append(name+": "+detail);ui->showStatus(name+"错误",detail);},queued);
    };
    bindStatus(source,"接收测量值");bindStatus(store,"保存记录");bindStatus(push,"向下游发送");
    QObject::connect(push,&wb::ResultPush::backlogChanged,ui,[=](int n){ui->showStatus("待下游确认",QString::number(n));},queued);
    QObject::connect(push,&wb::ResultPush::error,source,[=](QString detail){if(detail.contains("queue_full"))source->stopMeasurements();},queued);
    receiveThread.start();fileThread.start();pushThread.start();
    bool closed=false;
    auto closeWorkers=[&]{
        if(closed)return;closed=true;
        QMetaObject::invokeMethod(source,"shutdown",Qt::BlockingQueuedConnection);
        QMetaObject::invokeMethod(store,"shutdown",Qt::BlockingQueuedConnection);
        QMetaObject::invokeMethod(push,"shutdown",Qt::BlockingQueuedConnection);
        receiveThread.quit();fileThread.quit();pushThread.quit();
        receiveThread.wait();fileThread.wait();pushThread.wait();
    };
    QObject::connect(&app,&QCoreApplication::aboutToQuit,&app,closeWorkers);
    ui->setProperty("publicDataDescription", p.value("data-description"));
    if(p.isSet("data-description"))ui->showStatus("data",p.value("data-description"));
    for(const auto &field: {qMakePair("deviceHost","device-host"),qMakePair("downstreamHost","sink-host")})
        if(auto *edit=ui->findChild<QLineEdit *>(field.first))edit->setText(p.value(field.second));
    for(const auto &field: {qMakePair("devicePort",devicePort),qMakePair("downstreamPort",sinkPort),qMakePair("intervalMs",interval)})
        if(auto *spin=ui->findChild<QSpinBox *>(field.first))spin->setValue(field.second);
    QSize initialSize(1280,860);
    if(!p.isSet("headless") && app.primaryScreen())
        initialSize=initialSize.boundedTo(app.primaryScreen()->availableGeometry().size()-QSize(32,48));
    ui->resize(initialSize.expandedTo(ui->minimumSize()));ui->show();
    if(p.isSet("record"))QMetaObject::invokeMethod(store,"begin",queued,Q_ARG(QString,p.value("record")));
    if(p.isSet("capture")){
        QMetaObject::invokeMethod(push,"connectSink",queued,Q_ARG(QString,p.value("sink-host")),Q_ARG(quint16,quint16(sinkPort)));
        QMetaObject::invokeMethod(source,"connectDevice",queued,Q_ARG(QString,p.value("device-host")),Q_ARG(quint16,quint16(devicePort)));
        QMetaObject::invokeMethod(source,"startMeasurements",queued,Q_ARG(int,interval));
        const QString faultMode=p.value("fault");
        if(faultMode!="none") QTimer::singleShot(350,source,[=]{source->setSimulation(faultMode,every);});
    }
    if(duration>0){
        // Stop generation first, allow ACK/file queues to drain, then take screenshot and quit.
        QTimer::singleShot(qMax(1,duration-700),source,[=]{source->stopMeasurements();});
        QTimer::singleShot(duration,&app,[&]{
            snapshot();
            app.quit();
        });
    }
    const int result=app.exec();closeWorkers();
    if(p.isSet("report")){
        QJsonObject report{{"version",app.applicationVersion()},{"replayFinished",replayFinished},{"samples",double(samples)},{"stored",double(stored)},{"delivered",double(delivered)},{"commandAcks",double(acks)},{"errors",errors},
            {"threads",QJsonObject{{"receive",sourceAffinity.load()},{"save",storeAffinity.load()},{"push",pushAffinity.load()},{"gui",uiAffinity}}},{"workersStopped",true}};
        QDir().mkpath(QFileInfo(p.value("report")).absolutePath());QFile f(p.value("report"));
        if(!f.open(QIODevice::WriteOnly)||f.write(QJsonDocument(report).toJson())<0){qCritical("Cannot save report");delete ui;return 3;}
    }
    delete ui;return result;
}
