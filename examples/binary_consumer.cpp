// Compile this client against delivered headers/import libs, without module source.
#include <workbench/contracts.h>
#include <QtWidgets/QApplication>
#include <QtCore/QLibrary>
#include <QtCore/QEventLoop>
#include <QtCore/QTimer>
#include <QtCore/QTemporaryDir>
#include <QtCore/QFile>
#include <QtCore/QDir>
#include <memory>
#include <vector>

int main(int argc,char **argv){
    qputenv("QT_QPA_PLATFORM","offscreen");
    qputenv("QT_QPA_FONTDIR",(qEnvironmentVariable("SystemRoot","C:/Windows")+"/Fonts").toUtf8());
    QApplication app(argc,argv);wb::registerTypes();
    if(argc!=2)return 2;const QString runtime=QString::fromLocal8Bit(argv[1]);
    std::vector<std::unique_ptr<QLibrary>> libraries;
    auto factory=[&](const char *module,const char *symbol){
        auto lib=std::make_unique<QLibrary>(QDir(runtime).filePath(QString("wb_%1.dll").arg(module)));
        const auto result=lib->resolve(symbol);if(!result)qCritical("Missing DLL factory %s: %s",symbol,qPrintable(lib->errorString()));
        libraries.push_back(std::move(lib));return result;
    };
    const auto wireFactory=reinterpret_cast<wb::WireCodec*(*)(QObject*)>(factory("wire_protocol","wb_create_wire"));
    const auto simFactory=reinterpret_cast<wb::Simulator*(*)(QObject*)>(factory("device_simulator","wb_create_simulator"));
    const auto sourceFactory=reinterpret_cast<wb::DeviceSource*(*)(QObject*)>(factory("device_source","wb_create_source"));
    const auto uiFactory=reinterpret_cast<wb::Frontend*(*)(QWidget*)>(factory("frontend","wb_create_frontend"));
    const auto storeFactory=reinterpret_cast<wb::RecordStore*(*)(QObject*)>(factory("record_store","wb_create_store"));
    const auto pushFactory=reinterpret_cast<wb::ResultPush*(*)(QObject*)>(factory("result_push","wb_create_push"));
    const auto receiverFactory=reinterpret_cast<wb::ResultReceiver*(*)(QObject*)>(factory("result_receiver","wb_create_receiver"));
    if(!wireFactory||!simFactory||!sourceFactory||!uiFactory||!storeFactory||!pushFactory||!receiverFactory)return 3;
    std::unique_ptr<wb::WireCodec> wire(wireFactory(nullptr));
    std::unique_ptr<wb::Simulator> sim(simFactory(nullptr));
    std::unique_ptr<wb::DeviceSource> source(sourceFactory(nullptr));
    std::unique_ptr<wb::Frontend> ui(uiFactory(nullptr));
    std::unique_ptr<wb::RecordStore> store(storeFactory(nullptr));
    std::unique_ptr<wb::ResultPush> push(pushFactory(nullptr));
    std::unique_ptr<wb::ResultReceiver> receiver(receiverFactory(nullptr));
    QTemporaryDir temp(QDir::current().filePath("consumer-XXXXXX"));if(!temp.isValid())return 4;
    bool framed=false,saved=false,forwarded=false,history=false,measured=false;
    const wb::Sample example{"binary-client",1,1791216000000LL,25.6,60.2,3.3};
    QObject::connect(wire.get(),&wb::WireCodec::message,&app,[&](QJsonObject j){wb::Sample parsed;QString e;framed=wb::sampleFromJson(j,parsed,e);});
    wire->feed(wire->encode(wb::sampleToJson(example)));if(!framed)return 5;
    QObject::connect(store.get(),&wb::RecordStore::stored,&app,[&](qint64){saved=true;});
    const QString csv=temp.filePath("records.csv");store->begin(csv);
    QObject::connect(sim.get(),&wb::Simulator::listening,source.get(),[&](quint16 port){source->connectDevice("127.0.0.1",port);source->startMeasurements(20);});
    QObject::connect(receiver.get(),&wb::ResultReceiver::listening,push.get(),[&](quint16 port){push->connectSink("127.0.0.1",port);});
    QEventLoop wait;
    QObject::connect(source.get(),&wb::DeviceSource::sampleReady,&app,[&](wb::Sample sample){if(measured)return;measured=true;source->stopMeasurements();ui->showSample(sample);store->append(sample);push->enqueue(sample);});
    QObject::connect(push.get(),&wb::ResultPush::delivered,&wait,[&](qint64){forwarded=true;wait.quit();});
    receiver->listen("127.0.0.1",0,temp.filePath("received.ndjson"));sim->listen("127.0.0.1",0);
    QTimer::singleShot(5000,&wait,&QEventLoop::quit);wait.exec();
    QObject::connect(store.get(),&wb::RecordStore::historyReady,&app,[&](wb::SampleBatch batch){history=batch.size()==1;ui->showHistory(batch);});
    store->stop();store->load(csv);source->shutdown();store->shutdown();push->shutdown();receiver->shutdown();sim->shutdown();
    if(!measured||!saved||!forwarded||!history)return 6;
    qInfo("All seven module DLL factories worked through delivered public interfaces.");return 0;
}
