#include <workbench/contracts.h>
#include <QtTest/QtTest>
#include <limits>
class ContractTest:public QObject {
 Q_OBJECT
private slots:
 void roundTrip(){ wb::Sample a{"sim-001",12,1791216000000LL,25.625,60.125,3.333333}; wb::Sample b; QString e; QVERIFY(wb::sampleFromJson(wb::sampleToJson(a),b,e)); QCOMPARE(b.sequence,a.sequence); QCOMPARE(b.voltage,a.voltage); }
 void invalidNumbers(){auto j=wb::sampleToJson({"s",1,1,25,60,3.3});wb::Sample s;QString e; j["sequence"]=1.5;QVERIFY(!wb::sampleFromJson(j,s,e));j["sequence"]=1;j["humidity"]="60";QVERIFY(!wb::sampleFromJson(j,s,e));j["humidity"]=101;QVERIFY(!wb::sampleFromJson(j,s,e));}
 void fieldsRequired(){auto base=wb::sampleToJson({"s",1,1,25,60,3.3});for(auto it=base.begin();it!=base.end();++it){auto j=base;j.remove(it.key());wb::Sample s;QString e;QVERIFY2(!wb::sampleFromJson(j,s,e),qPrintable(it.key()));}}
 void rejectsVersionsAndPreservesOutput(){auto j=wb::sampleToJson({"s",1,1,25,60,3.3});j["v"]=2;wb::Sample s{"original",42,1,0,0,0};QString e;QVERIFY(!wb::sampleFromJson(j,s,e));QCOMPARE(s.sequence,42);}
};
QTEST_GUILESS_MAIN(ContractTest)
#include "contracts_test.moc"
