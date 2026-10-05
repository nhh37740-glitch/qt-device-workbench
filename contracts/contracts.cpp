#include <workbench/contracts.h>
#include <cmath>
#include <QtCore/QJsonValue>
namespace wb {
void registerTypes() { qRegisterMetaType<Sample>("wb::Sample"); qRegisterMetaType<SampleBatch>("wb::SampleBatch"); }
QJsonObject sampleToJson(const Sample &s) {
    return {{"v",1},{"type","sample"},{"deviceId",s.deviceId},{"sequence",double(s.sequence)},
            {"timestampMs",double(s.timestampMs)},{"temperature",s.temperature},{"humidity",s.humidity},{"voltage",s.voltage}};
}
bool sampleFromJson(const QJsonObject &j, Sample &s, QString &error) {
    auto fail=[&](const QString &detail){error=detail;return false;};
    if(!j.value("v").isDouble() || j.value("v").toDouble()!=1 || j.value("type").toString()!="sample") return fail("unsupported sample type/version");
    if(!j.value("deviceId").isString() || j.value("deviceId").toString().trimmed().isEmpty() || j.value("deviceId").toString().size()>256) return fail("invalid deviceId");
    for(const char *key : {"sequence","timestampMs"}) {
        const auto value=j.value(QLatin1String(key));
        const double d=value.toDouble();
        if(!value.isDouble() || !std::isfinite(d) || d<1 || d>9007199254740991.0 || std::floor(d)!=d) return fail(QString("invalid %1").arg(key));
    }
    for(const char *key : {"temperature","humidity","voltage"}) {
        const auto value=j.value(QLatin1String(key));
        if(!value.isDouble() || !std::isfinite(value.toDouble())) return fail(QString("invalid %1").arg(key));
    }
    const double t=j.value("temperature").toDouble(),h=j.value("humidity").toDouble(),v=j.value("voltage").toDouble();
    if(t<-100 || t>200 || h<0 || h>100 || v<0 || v>1000) return fail("measurement out of range");
    Sample parsed{j.value("deviceId").toString(),qint64(j.value("sequence").toDouble()),qint64(j.value("timestampMs").toDouble()),t,h,v};
    s=parsed; error.clear(); return true;
}
WireCodec::WireCodec(QObject *p):QObject(p){}
Simulator::Simulator(QObject *p):QObject(p){}
DeviceSource::DeviceSource(QObject *p):QObject(p){}
Frontend::Frontend(QWidget *p):QWidget(p){}
RecordStore::RecordStore(QObject *p):QObject(p){}
ResultPush::ResultPush(QObject *p):QObject(p){}
ResultReceiver::ResultReceiver(QObject *p):QObject(p){}
}
