#include <workbench/contracts.h>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QDir>
#include <QtCore/QStringDecoder>
#include <QtCore/QRegularExpression>
#include <cmath>

namespace {
const QByteArray header("deviceId,sequence,timestampMs,temperature,humidity,voltage\n");
QByteArray quote(QString value) {
    value.replace('"', "\"\"");
    return '"' + value.toUtf8() + '"';
}
bool valid(const wb::Sample &s, QString &error) {
    wb::Sample parsed;
    return wb::sampleFromJson(wb::sampleToJson(s), parsed, error);
}
// Parse one logical CSV row, including quoted newlines, without loading the file.
bool row(QFile &file, QList<QByteArray> &fields, bool &eof, QString &error) {
    enum State { Start, Plain, Quoted, AfterQuote } state = Start;
    QByteArray field;
    bool any = false;
    int bytes = 0;
    char ch;
    while (file.getChar(&ch)) {
        any = true;
        if (++bytes > 65536) { error = "CSV row exceeds 65536 bytes"; return false; }
        if (state == Quoted) {
            if (ch == '"') state = AfterQuote;
            else field += ch;
            continue;
        }
        if (state == AfterQuote && ch == '"') { field += '"'; state = Quoted; continue; }
        if (ch == ',' || ch == '\n' || ch == '\r') {
            fields.append(field); field.clear(); state = Start;
            if (fields.size() > 6) { error = "CSV has too many columns"; return false; }
            if (ch != ',') {
                if (ch == '\r' && file.peek(1) == "\n") file.getChar(&ch);
                eof = false; return true;
            }
            continue;
        }
        if (state == AfterQuote || (state == Plain && ch == '"')) {
            error = "Invalid CSV quoting"; return false;
        }
        if (state == Start && ch == '"') state = Quoted;
        else { state = Plain; field += ch; }
    }
    if (file.error() != QFileDevice::NoError) { error = file.errorString(); return false; }
    if (state == Quoted) { error = "Unterminated CSV quote"; return false; }
    eof = !any;
    if (any) fields.append(field);
    return true;
}
class Store final : public wb::RecordStore {
public:
    using RecordStore::RecordStore;
    void begin(QString path) override {
        stop();
        const QFileInfo info(path);
        if (path.isEmpty() || !QDir().mkpath(info.absolutePath())) {
            emit error("Cannot create recording directory: " + path); return;
        }
        file = new QFile(path, this);
        if (!file->open(QIODevice::WriteOnly | QIODevice::Truncate) ||
            file->write(header) != header.size() || !file->flush()) {
            fail("Cannot open recording: " + file->errorString()); return;
        }
        emit status("recording " + path);
    }
    void append(wb::Sample sample) override {
        if (!file) return;
        QString detail;
        if (!valid(sample, detail)) { emit error("Invalid recording sample: " + detail); return; }
        const QByteArray bytes = quote(sample.deviceId) + ',' + QByteArray::number(sample.sequence) + ',' +
            QByteArray::number(sample.timestampMs) + ',' + QByteArray::number(sample.temperature, 'g', 17) + ',' +
            QByteArray::number(sample.humidity, 'g', 17) + ',' + QByteArray::number(sample.voltage, 'g', 17) + '\n';
        if (bytes.size() > 65536) { emit error("CSV row exceeds 65536 bytes"); return; }
        if (file->write(bytes) != bytes.size() || !file->flush()) {
            fail("Recording write failed: " + file->errorString()); return;
        }
        emit stored(sample.sequence);
    }
    void stop() override {
        if (!file) return;
        if (!file->flush()) emit error("Recording flush failed: " + file->errorString());
        file->close(); delete file; file = nullptr;
        emit status("recording stopped");
    }
    void load(QString path) override {
        QFile input(path);
        if (!input.open(QIODevice::ReadOnly)) { emit error("History open failed: " + input.errorString()); return; }
        wb::SampleBatch batch;
        QList<QByteArray> fields;
        bool eof = false;
        QString detail;
        if (!row(input, fields, eof, detail) || eof ||
            fields != QList<QByteArray>{"deviceId", "sequence", "timestampMs", "temperature", "humidity", "voltage"}) {
            emit error("Invalid CSV header: " + detail); return;
        }
        int line = 1;
        const QRegularExpression integer("^[0-9]+$");
        for (;;) {
            fields.clear();
            if (!row(input, fields, eof, detail)) { emit error(QString("Corrupt CSV row %1: ").arg(line + 1) + detail); return; }
            if (eof) break;
            ++line;
            if (batch.size() >= 100000) { emit error("History exceeds 100000 records"); return; }
            if (fields.size() != 6) { emit error(QString("Corrupt CSV row %1: column count").arg(line)); return; }
            QStringList values;
            for (const auto &field : fields) {
                QStringDecoder decoder(QStringDecoder::Utf8);
                values.append(decoder(field));
                if (decoder.hasError()) { emit error("Invalid UTF-8 in history"); return; }
            }
            bool okSequence, okTimestamp, okTemperature, okHumidity, okVoltage;
            wb::Sample s;
            s.deviceId = values[0];
            s.sequence = values[1].toLongLong(&okSequence);
            s.timestampMs = values[2].toLongLong(&okTimestamp);
            s.temperature = values[3].toDouble(&okTemperature);
            s.humidity = values[4].toDouble(&okHumidity);
            s.voltage = values[5].toDouble(&okVoltage);
            if (!integer.match(values[1]).hasMatch() || !integer.match(values[2]).hasMatch() ||
                !okSequence || !okTimestamp || !okTemperature || !okHumidity || !okVoltage || !valid(s, detail)) {
                emit error(QString("Corrupt CSV row %1: invalid values ").arg(line) + detail); return;
            }
            batch.append(s);
        }
        emit historyReady(batch);
        emit status(QString("loaded %1 records").arg(batch.size()));
    }
    void shutdown() override { stop(); }
private:
    QFile *file = nullptr;
    void fail(QString detail) {
        if (file) { file->close(); delete file; file = nullptr; }
        emit error(detail);
        emit status("recording failed");
    }
};
}
extern "C" WB_STORE_API wb::RecordStore *wb_create_store(QObject *parent) { return new Store(parent); }
