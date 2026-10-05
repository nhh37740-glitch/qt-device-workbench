#include <workbench/contracts.h>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonParseError>

namespace {
constexpr qsizetype MaxLine = 65536;
bool envelopeValid(const QJsonObject &object)
{
    const auto version = object.value(QStringLiteral("v"));
    const auto type = object.value(QStringLiteral("type"));
    if (!version.isDouble() || version.toDouble() != 1.0 || !type.isString())
        return false;
    const QString name = type.toString();
    return name == QStringLiteral("command") || name == QStringLiteral("ack")
        || name == QStringLiteral("sample") || name == QStringLiteral("received");
}

class NdjsonCodec final : public wb::WireCodec {
public:
    explicit NdjsonCodec(QObject *parent) : WireCodec(parent) {}
    QByteArray encode(const QJsonObject &object) const override
    {
        if (!envelopeValid(object)) return {};
        QByteArray line = QJsonDocument(object).toJson(QJsonDocument::Compact);
        if (line.size() > MaxLine) return {};
        line.append('\n');
        return line;
    }
    void reset() override { buffer.clear(); dropping = false; }
    void feed(QByteArray bytes) override
    {
        qsizetype offset = 0;
        while (offset < bytes.size()) {
            const qsizetype newline = bytes.indexOf('\n', offset);
            const qsizetype end = newline < 0 ? bytes.size() : newline;
            const qsizetype size = end - offset;
            if (!dropping) {
                if (size > MaxLine - buffer.size()) {
                    buffer.clear();
                    dropping = true;
                    emit error(QStringLiteral("line_too_large: maximum 65536 bytes"));
                } else {
                    buffer.append(bytes.constData() + offset, size);
                }
            }
            if (newline < 0) break;
            if (dropping) {
                dropping = false;
                buffer.clear();
            } else {
                QByteArray line = std::move(buffer);
                buffer.clear();
                if (line.endsWith('\r')) line.chop(1);
                QJsonParseError parseError;
                const auto document = QJsonDocument::fromJson(line, &parseError);
                if (parseError.error != QJsonParseError::NoError || !document.isObject())
                    emit error(QStringLiteral("malformed_json: ") + parseError.errorString());
                else if (!envelopeValid(document.object()))
                    emit error(QStringLiteral("invalid_envelope: expected v=1 and known type"));
                else
                    emit message(document.object());
            }
            offset = newline + 1;
        }
    }
private:
    QByteArray buffer;
    bool dropping = false;
};
}

extern "C" Q_DECL_EXPORT wb::WireCodec *wb_create_wire(QObject *parent)
{
    return new NdjsonCodec(parent);
}

