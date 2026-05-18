#include "bcragentservice.h"

#include <QCoreApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QTextStream>

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("bcr-agent"));
    QCoreApplication::setApplicationVersion(QStringLiteral("0.1.0"));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Manual HTTP bridge for the Qt BCR demo"));
    parser.addHelpOption();
    parser.addVersionOption();

    const QCommandLineOption clientHostOption(QStringList{QStringLiteral("client-host")},
                                              QStringLiteral("Target bcr-client host."),
                                              QStringLiteral("host"),
                                              QStringLiteral("127.0.0.1"));
    const QCommandLineOption clientPortOption(QStringList{QStringLiteral("client-port")},
                                              QStringLiteral("Target bcr-client port."),
                                              QStringLiteral("port"),
                                              QStringLiteral("45454"));
    const QCommandLineOption listenAddressOption(QStringList{QStringLiteral("listen-address")},
                                                 QStringLiteral("Local HTTP listen address."),
                                                 QStringLiteral("address"),
                                                 QStringLiteral("127.0.0.1"));
    const QCommandLineOption listenPortOption(QStringList{QStringLiteral("listen-port")},
                                              QStringLiteral("Local HTTP listen port."),
                                              QStringLiteral("port"),
                                              QStringLiteral("45455"));
    const QCommandLineOption timeoutOption(QStringList{QStringLiteral("timeout-ms")},
                                           QStringLiteral("TCP request timeout in milliseconds."),
                                           QStringLiteral("milliseconds"),
                                           QStringLiteral("3000"));

    parser.addOption(clientHostOption);
    parser.addOption(clientPortOption);
    parser.addOption(listenAddressOption);
    parser.addOption(listenPortOption);
    parser.addOption(timeoutOption);
    parser.process(app);

    bool clientPortOk = false;
    const quint16 clientPort = parser.value(clientPortOption).toUShort(&clientPortOk);
    if (!clientPortOk || clientPort == 0) {
        QTextStream(stderr) << "Invalid client port: " << parser.value(clientPortOption) << '\n';
        return 1;
    }

    bool listenPortOk = false;
    const quint16 listenPort = parser.value(listenPortOption).toUShort(&listenPortOk);
    if (!listenPortOk || listenPort == 0) {
        QTextStream(stderr) << "Invalid listen port: " << parser.value(listenPortOption) << '\n';
        return 1;
    }

    bool timeoutOk = false;
    const int timeoutMs = parser.value(timeoutOption).toInt(&timeoutOk);
    if (!timeoutOk || timeoutMs <= 0) {
        QTextStream(stderr) << "Invalid timeout: " << parser.value(timeoutOption) << '\n';
        return 1;
    }

    AgentSettings settings;
    settings.clientHost = parser.value(clientHostOption).trimmed();
    settings.clientPort = clientPort;
    settings.listenAddress = parser.value(listenAddressOption).trimmed();
    settings.listenPort = listenPort;
    settings.timeoutMs = timeoutMs;

    BcrAgentService service(settings);
    if (!service.start()) {
        QTextStream(stderr) << "Failed to start bcr-agent: " << service.errorString() << '\n';
        return 1;
    }

    QTextStream(stdout) << "bcr-agent listening on http://" << settings.listenAddress << ':'
                        << settings.listenPort << " and forwarding to " << settings.clientHost << ':'
                        << settings.clientPort << '\n';
    return app.exec();
}
