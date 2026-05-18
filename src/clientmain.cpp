#include "mainwindow.h"

#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QHostAddress>
#include <QTextStream>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("bcr-client"));
    QCoreApplication::setApplicationVersion(QStringLiteral("0.1.0"));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Qt WebEngine BCR demo client"));
    parser.addHelpOption();
    parser.addVersionOption();

    const QCommandLineOption addressOption(QStringList{QStringLiteral("a"), QStringLiteral("address")},
                                           QStringLiteral("Listen address."),
                                           QStringLiteral("address"),
                                           QStringLiteral("0.0.0.0"));
    const QCommandLineOption portOption(QStringList{QStringLiteral("p"), QStringLiteral("port")},
                                        QStringLiteral("Listen port."),
                                        QStringLiteral("port"),
                                        QStringLiteral("45454"));
    const QCommandLineOption agentBaseUrlOption(QStringList{QStringLiteral("agent-base-url")},
                                                QStringLiteral("Remote bcr-agent base URL, for example http://192.168.1.20:45455"),
                                                QStringLiteral("url"));

    parser.addOption(addressOption);
    parser.addOption(portOption);
    parser.addOption(agentBaseUrlOption);
    parser.process(app);

    QHostAddress listenAddress;
    if (!listenAddress.setAddress(parser.value(addressOption))) {
        QTextStream(stderr) << "Invalid listen address: " << parser.value(addressOption) << '\n';
        return 1;
    }

    bool portOk = false;
    const quint16 port = parser.value(portOption).toUShort(&portOk);
    if (!portOk || port == 0) {
        QTextStream(stderr) << "Invalid listen port: " << parser.value(portOption) << '\n';
        return 1;
    }

    const QUrl agentBaseUrl = QUrl::fromUserInput(parser.value(agentBaseUrlOption).trimmed());
    if (!parser.value(agentBaseUrlOption).trimmed().isEmpty() && !agentBaseUrl.isValid()) {
        QTextStream(stderr) << "Invalid agent base URL: " << parser.value(agentBaseUrlOption) << '\n';
        return 1;
    }

    MainWindow window(listenAddress, port, agentBaseUrl);
    window.show();

    return app.exec();
}
