#include "CrashHandler.h"
#include "ElevatedSave.h"
#include "MainWindow.h"
#include "StartupLog.h"

#include "core/search/Search.h"

#include "editor/EditorPane.h"

#include <Scintilla.h>

#include <QApplication>
#include <QDataStream>
#include <QElapsedTimer>
#include <QIcon>
#include <QDir>
#include <QLocalServer>
#include <QLocalSocket>
#include <QTimer>

#include <cstdio>
#include <cstring>
#include <memory>

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
    // Elevated save helper (started by the editor itself with administrator rights): no GUI
    if (argc == 4 && std::strcmp(argv[1], npp::ui::kElevatedCopyOption) == 0)
        return npp::ui::runElevatedCopy(QString::fromLocal8Bit(argv[2]), QString::fromLocal8Bit(argv[3]));

    npp::ui::startupTimer().start();
    QApplication app(argc, argv);
    npp::ui::startupLog("QApplication");
    QApplication::setApplicationName(QStringLiteral("Notepad++ Qt"));
    QApplication::setOrganizationName(QStringLiteral("NotepadPlusPlusQt"));
    QApplication::setApplicationVersion(QStringLiteral(NPP_QT_VERSION));
    QApplication::setDesktopFileName(QStringLiteral("notepadpp-qt")); // Wayland: icon / grouping from the .desktop file
    {
        QIcon icon;
        for (int size : {16, 32, 48, 64, 128})
            icon.addFile(QStringLiteral(":/resources/icons/notepadpp-qt-%1.png").arg(size), QSize(size, size));
        QApplication::setWindowIcon(icon);
    }

    // Upstream style options (-n, -l, -multiInst, ...) plus --screenshot for smoke tests
    QStringList args = QCoreApplication::arguments();
    QString screenshot;
    bool crashTest = false; // hidden: crash after start (crash report / recovery test)
    int quitAfterMs = -1;   // hidden: print the state and quit (recovery test)
    for (int i = 1; i < args.size();) {
        if (args[i] == QLatin1String("--screenshot") && i + 1 < args.size()) {
            screenshot = args[i + 1];
            args.remove(i, 2);
        } else if (args[i] == QLatin1String("--crash-test")) {
            crashTest = true;
            args.remove(i);
        } else if (args[i].startsWith(QLatin1String("--quit-after="))) {
            quitAfterMs = args[i].mid(13).toInt();
            args.remove(i);
        } else {
            ++i;
        }
    }
    npp::ui::CommandLine cmd = npp::ui::parseCommandLine(args);
    if (!screenshot.isEmpty())
        cmd.noSession = true; // smoke test: do not touch the user's session

    if (!cmd.multiInstance && screenshot.isEmpty() && forwardToRunningInstance(args.mid(1)))
        return 0;

    npp::core::initRegexLocale();
    npp::ui::startupLog("before MainWindow");
    npp::ui::MainWindow window(cmd);
    npp::ui::startupLog("MainWindow constructed (settings, session)");
    const QString crashDir = QDir(QString::fromStdString(window.paths().userDir())).filePath(QStringLiteral("crash"));
    npp::ui::installCrashHandler(crashDir);
    // Abnormal termination marker: only the first instance owns the session
    std::unique_ptr<npp::ui::RunMarker> marker;
    if (!cmd.multiInstance && screenshot.isEmpty())
        marker = std::make_unique<npp::ui::RunMarker>(
            QDir(QString::fromStdString(window.paths().sessionDir())).filePath(QStringLiteral("npp-running.marker")));
    window.show();
    npp::ui::startupLog("shown");
    QTimer::singleShot(0, &window, [] { npp::ui::startupLog("first event loop pass (painted)"); });
    if (marker && marker->previousRunCrashed())
        QTimer::singleShot(0, &window, [&window, crashDir] { window.showCrashRecoveryNotice(crashDir); });

    if (crashTest)
        QTimer::singleShot(1000, &window, [&window] {
            if (npp::core::Buffer* b = window.currentBuffer()) {
                window.currentPane()->editor()->send(SCI_APPENDTEXT, 23, reinterpret_cast<intptr_t>("crash test unsaved text"));
                window.files().find(b->id)->dirty = true;
            }
            window.backupNow();
            npp::ui::crashForTest();
        });
    if (quitAfterMs >= 0)
        QTimer::singleShot(quitAfterMs, &window, [&window] {
            std::fprintf(stderr, "state: %s\n", qPrintable(window.stateSummary()));
            std::fflush(stderr);
            window.shutdownForTest(); // stores session / settings like a normal exit
            QApplication::exit(0);
        });

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
