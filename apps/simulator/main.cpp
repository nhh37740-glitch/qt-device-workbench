#include <workbench/contracts.h>
#include <QtCore/QCoreApplication>
#include <QtCore/QCommandLineParser>
#include <QtCore/QJsonDocument>
#include <QtCore/QDir>
#include <QtCore/QVariant>
#include <QtCore/QSaveFile>
#include <QtCore/QTimer>
#include <QtCore/QTextStream>
#include <csignal>

namespace {
volatile std::sig_atomic_t interrupted = 0;
void signalHandler(int) { interrupted = 1; }
}
int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("device-simulator"));
    wb::registerTypes();
    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Qt TCP device simulator: recorded public measurement replay"));
    parser.addHelpOption();
    parser.addOptions({
        {{QStringLiteral("bind")}, QStringLiteral("Bind IP address"), QStringLiteral("address"), QStringLiteral("127.0.0.1")},
        {{QStringLiteral("port")}, QStringLiteral("TCP port (0 chooses an ephemeral port)"), QStringLiteral("port"), QStringLiteral("9101")},
        {{QStringLiteral("duration-ms")}, QStringLiteral("Exit after this many milliseconds"), QStringLiteral("milliseconds")},
        {{QStringLiteral("ready-file")}, QStringLiteral("Write listening port and data mode as JSON"), QStringLiteral("path")},
        {{QStringLiteral("data")}, QStringLiteral("Recorded measurement CSV (required in default replay mode)"), QStringLiteral("path"),
            QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("data/intel-lab-mote1.csv"))},
        {{QStringLiteral("synthetic")}, QStringLiteral("Explicitly generate synthetic sine fixtures instead of recorded replay")}
    });
    parser.process(application);
    if (parser.isSet(QStringLiteral("synthetic")) && parser.isSet(QStringLiteral("data"))) {
        QTextStream(stderr) << "choose --data or --synthetic\n" << Qt::flush;
        return 2;
    }
    bool portOk = false;
    const uint port = parser.value(QStringLiteral("port")).toUInt(&portOk);
    bool durationOk = true;
    const int duration = parser.isSet(QStringLiteral("duration-ms"))
        ? parser.value(QStringLiteral("duration-ms")).toInt(&durationOk) : 0;
    if (!portOk || port > 65535 || !durationOk || duration < 0) {
        QTextStream(stderr) << "invalid port or duration\n" << Qt::flush;
        return 2;
    }
    auto *simulator = wb_create_simulator(&application);
    if (!parser.isSet(QStringLiteral("synthetic")))
        simulator->setProperty("replayFile", parser.value(QStringLiteral("data")));
    bool readyFileFailed = false;
    QObject::connect(simulator, &wb::Simulator::listening, &application, [&](quint16 actualPort) {
        QTextStream(stdout) << "LISTENING " << parser.value(QStringLiteral("bind")) << ':' << actualPort << '\n' << Qt::flush;
        if (parser.isSet(QStringLiteral("ready-file"))) {
            QSaveFile file(parser.value(QStringLiteral("ready-file")));
            const QByteArray json = QJsonDocument(QJsonObject{{QStringLiteral("port"), actualPort},
                {QStringLiteral("bind"), parser.value(QStringLiteral("bind"))},
                {QStringLiteral("dataSource"), simulator->property("dataSource").toString()},
                {QStringLiteral("replayRows"), simulator->property("replayRows").toInt()}}).toJson(QJsonDocument::Compact);
            if (!file.open(QIODevice::WriteOnly) || file.write(json) != json.size() || !file.commit()) {
                readyFileFailed = true;
                QTextStream(stderr) << "ready_file_failed: " << file.errorString() << '\n' << Qt::flush;
                QTimer::singleShot(0, &application, [&] { application.exit(3); });
            }
        }
    });
    bool started = false;
    QObject::connect(simulator, &wb::Simulator::listening, &application, [&] { started = true; });
    QObject::connect(simulator, &wb::Simulator::status, &application, [](const QString &detail) {
        QTextStream(stdout) << "STATUS " << detail << '\n' << Qt::flush;
    });
    QObject::connect(simulator, &wb::Simulator::error, &application, [&](const QString &detail) {
        QTextStream(stderr) << "ERROR " << detail << '\n' << Qt::flush;
        if (!started) QTimer::singleShot(0, &application, [&] { application.exit(1); });
    });
    QObject::connect(&application, &QCoreApplication::aboutToQuit, simulator, &wb::Simulator::shutdown);
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);
    QTimer signalPoll;
    QObject::connect(&signalPoll, &QTimer::timeout, &application, [&] {
        if (interrupted) { QTextStream(stdout) << "STATUS interrupted\n" << Qt::flush; application.quit(); }
    });
    signalPoll.start(50);
    if (parser.isSet(QStringLiteral("duration-ms"))) QTimer::singleShot(duration, &application, &QCoreApplication::quit);
    simulator->listen(parser.value(QStringLiteral("bind")), static_cast<quint16>(port));
    if (!started) return 1;
    if (readyFileFailed) { simulator->shutdown(); return 3; }
    return application.exec();
}
