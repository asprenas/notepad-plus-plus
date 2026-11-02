#include "MainWindow.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QTimer>

#include <cstdio>

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("Notepad++ Qt"));
    QApplication::setOrganizationName(QStringLiteral("NotepadPlusPlusQt"));
    QApplication::setApplicationVersion(QStringLiteral(NPP_QT_VERSION));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Notepad++ Qt port (phase 0 spike)"));
    parser.addHelpOption();
    parser.addVersionOption();
    QCommandLineOption screenshotOpt(QStringLiteral("screenshot"),
                                     QStringLiteral("Smoke test: render the window, save it as PNG and exit."),
                                     QStringLiteral("file.png"));
    parser.addOption(screenshotOpt);
    parser.addPositionalArgument(QStringLiteral("files"), QStringLiteral("Files to open"), QStringLiteral("[files...]"));
    parser.process(app);

    npp::ui::MainWindow window;
    for (const QString& f : parser.positionalArguments())
        window.openFile(f);
    window.show();

    if (parser.isSet(screenshotOpt)) {
        const QString out = parser.value(screenshotOpt);
        QTimer::singleShot(1500, &window, [&window, out] {
            const bool ok = window.grab().save(out);
            std::fprintf(stderr, "screenshot %s: %s\n", ok ? "saved" : "FAILED", qPrintable(out));
            QApplication::exit(ok ? 0 : 1);
        });
    }
    return app.exec();
}
