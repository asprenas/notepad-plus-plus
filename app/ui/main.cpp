#include "MainWindow.h"

#include "core/search/Search.h"

#include <QApplication>
#include <QDataStream>
#include <QDir>
#include <QLocalServer>
#include <QLocalSocket>
#include <QTimer>

#include <cstdio>

namespace {

// Single instance (upstream: forwards the command line to the running window via WM_COPYDATA)
QString instanceKey()
{
    if (const QString test = qEnvironmentVariable("NPP_QT_INSTANCE_KEY"); !test.isEmpty())
        return test; // tests
    return QStringLiteral("notepadpp-qt-") + qEnvironmentVariable("USERNAME", qEnvironmentVariable("USER"));
}

bool forwardToRunningInstance(const QStringList& args)
{
    QLocalSocket socket;
    socket.connectToServer(instanceKey());
    if (!socket.waitForConnected(500))
        return false;
    QStringList absolute;
    for (const QString& a : args)
        absolute << (a.startsWith(QLatin1Char('-')) ? a : QDir::current().absoluteFilePath(a));
    QByteArray data;
    QDataStream out(&data, QIODevice::WriteOnly);
    out << absolute;
    socket.write(data);
    socket.waitForBytesWritten(1000);
    socket.disconnectFromServer();
    return true;
}

} // namespace

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("Notepad++ Qt"));
    QApplication::setOrganizationName(QStringLiteral("NotepadPlusPlusQt"));
    QApplication::setApplicationVersion(QStringLiteral(NPP_QT_VERSION));

    // Upstream style options (-n, -l, -multiInst, ...) plus --screenshot for smoke tests
    QStringList args = QCoreApplication::arguments();
    QString screenshot;
    for (int i = 1; i < args.size(); ++i)
        if (args[i] == QLatin1String("--screenshot") && i + 1 < args.size()) {
            screenshot = args[i + 1];
            args.remove(i, 2);
            break;
        }
    npp::ui::CommandLine cmd = npp::ui::parseCommandLine(args);
    if (!screenshot.isEmpty())
        cmd.noSession = true; // smoke test: do not touch the user's session

    if (!cmd.multiInstance && screenshot.isEmpty() && forwardToRunningInstance(args.mid(1)))
        return 0;

    npp::core::initRegexLocale();
    npp::ui::MainWindow window(cmd);
    window.show();

    QLocalServer server;
    if (!cmd.multiInstance && screenshot.isEmpty()) {
        QLocalServer::removeServer(instanceKey());
        server.listen(instanceKey());
        QObject::connect(&server, &QLocalServer::newConnection, &window, [&server, &window] {
            QLocalSocket* s = server.nextPendingConnection();
            QObject::connect(s, &QLocalSocket::disconnected, s, &QObject::deleteLater);
            QObject::connect(s, &QLocalSocket::readyRead, &window, [s, &window] {
                QStringList forwarded;
                const QByteArray bytes = s->readAll();
                QDataStream in(bytes);
                in >> forwarded;
                forwarded.prepend(QString());
                window.openFromCommandLine(npp::ui::parseCommandLine(forwarded));
                if (window.isMinimized())
                    window.showNormal();
            });
        });
    }

    if (!screenshot.isEmpty()) {
        QTimer::singleShot(1500, &window, [&window, screenshot] {
            const bool ok = window.grab().save(screenshot);
            std::fprintf(stderr, "screenshot %s: %s\n", ok ? "saved" : "FAILED", qPrintable(screenshot));
            QApplication::exit(ok ? 0 : 1);
        });
    }
    return app.exec();
}
