#include <workbench/contracts.h>
#include <QtCore/QCoreApplication>
#include <QtCore/QCommandLineParser>
#include <QtCore/QFile>
#include <QtCore/QJsonDocument>
#include <QtCore/QTextStream>
#include <QtCore/QTimer>
#include <QtCore/QDir>
#include <QtCore/QFileInfo>

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName("result-receiver");
    wb::registerTypes();
    QCommandLineParser parser;
    parser.setApplicationDescription("Validate, save and acknowledge workbench results.");
    parser.addHelpOption();
    parser.addOption(QCommandLineOption(QStringLiteral("bind"), "Bind IP address", "address", "127.0.0.1"));
    parser.addOption(QCommandLineOption(QStringLiteral("port"), "TCP port (0 chooses a free port)", "port", "9102"));
    parser.addOption(QCommandLineOption(QStringLiteral("output"), "Append accepted NDJSON records to this file", "path"));
    parser.addOption(QCommandLineOption(QStringLiteral("duration-ms"), "Stop after this duration", "milliseconds"));
    parser.addOption(QCommandLineOption(QStringLiteral("ready-file"), "Write JSON with actual port after listening", "path"));
    parser.addOption(QCommandLineOption(QStringLiteral("exit-after"), "Stop after this many unique accepted samples", "count"));
    parser.process(app);
    bool ok;
    const uint port = parser.value("port").toUInt(&ok);
    if (!ok || port > 65535) { QTextStream(stderr) << "Invalid port\n"; return 2; }
    int duration = 0, exitAfter = 0;
    if (parser.isSet("duration-ms")) {
        duration = parser.value("duration-ms").toInt(&ok);
        if (!ok || duration < 1) { QTextStream(stderr) << "Invalid duration-ms\n"; return 2; }
    }
    if (parser.isSet("exit-after")) {
        exitAfter = parser.value("exit-after").toInt(&ok);
        if (!ok || exitAfter < 1) { QTextStream(stderr) << "Invalid exit-after\n"; return 2; }
    }
    auto receiver = wb_create_receiver(&app);
    int count = 0;
    bool listening = false, fatal = false;
    QObject::connect(receiver, &wb::ResultReceiver::status, &app, [](QString detail) {
        QTextStream stream(stdout); stream << detail << '\n'; stream.flush();
    });
    QObject::connect(receiver, &wb::ResultReceiver::error, &app, [&](QString detail) {
        QTextStream stream(stderr); stream << detail << '\n'; stream.flush();
        if (!listening || detail.startsWith("Receiver output")) { fatal = true; QTimer::singleShot(0, &app, [&] { app.exit(1); }); }
    });
    QObject::connect(receiver, &wb::ResultReceiver::listening, &app, [&](quint16 actualPort) {
        listening = true;
        if (parser.isSet("ready-file")) {
            const QString path = parser.value("ready-file");
            QFile ready(path);
            const auto bytes = QJsonDocument(QJsonObject{{"port", actualPort}, {"bind", parser.value("bind")}}).toJson(QJsonDocument::Compact);
            if (!QDir().mkpath(QFileInfo(path).absolutePath()) || !ready.open(QIODevice::WriteOnly | QIODevice::Truncate) ||
                ready.write(bytes) != bytes.size() || !ready.flush()) {
                QTextStream(stderr) << "Cannot write ready-file\n";
                fatal = true; QTimer::singleShot(0, &app, [&] { app.exit(1); });
            }
        }
    });
    QObject::connect(receiver, &wb::ResultReceiver::received, &app, [&](wb::Sample s) {
        QTextStream stream(stdout); stream << "received " << s.deviceId << ' ' << s.sequence << '\n'; stream.flush();
        if (exitAfter && ++count >= exitAfter) QTimer::singleShot(0, &app, &QCoreApplication::quit);
    });
    QObject::connect(&app, &QCoreApplication::aboutToQuit, receiver, &wb::ResultReceiver::shutdown);
    receiver->listen(parser.value("bind"), static_cast<quint16>(port), parser.value("output"));
    if (duration) QTimer::singleShot(duration, &app, &QCoreApplication::quit);
    const int result = app.exec();
    return fatal ? 1 : result;
}
