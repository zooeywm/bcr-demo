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

    parser.addOption(addressOption);
    parser.addOption(portOption);
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

    MainWindow window(listenAddress, port);
    window.show();

    return app.exec();
}
