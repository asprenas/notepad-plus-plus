// UI smoke tests of the phase 2 MVP (offscreen): open/edit/save/close/reopen, split view,
// encoding, language, session round trip, menu coverage.
#include "MainWindow.h"
#include "editor/EditorPane.h"
#include "menu/ActionRegistry.h"
#include "menu/CommandIds.h"

#include "core/search/Search.h"
#include "core/session/Session.h"

#include <Scintilla.h>
#include <ScintillaEditBase.h>

#include <QAction>
#include <QDateTime>
#include <QKeySequence>
#include <QLocalSocket>
#include <QProcess>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTest>

using namespace npp::core;
using namespace npp::ui;
using namespace npp::ui::cmd;

namespace {

std::string readAll(const QString& path)
{
    QFile f(path);
    f.open(QIODevice::ReadOnly);
    return f.readAll().toStdString();
}

void writeAll(const QString& path, const QByteArray& bytes)
{
    QFile f(path);
    f.open(QIODevice::WriteOnly);
    f.write(bytes);
}

std::string text(ScintillaEditBase* e)
{
    const auto len = e->send(SCI_GETLENGTH);
    std::string s(static_cast<size_t>(len), '\0');
    e->send(SCI_GETTEXT, static_cast<uptr_t>(len + 1), reinterpret_cast<sptr_t>(s.data()));
    return s;
}

} // namespace

class TestMainWindow : public QObject {
    Q_OBJECT
    QTemporaryDir m_user, m_work;

    std::unique_ptr<MainWindow> makeWindow(CommandLine c = {})
    {
        return std::make_unique<MainWindow>(c);
    }
    QString work(const char* name) const { return m_work.filePath(QString::fromLatin1(name)); }

private slots:
    void initTestCase()
    {
        QVERIFY(m_user.isValid() && m_work.isValid());
        qputenv("NPP_QT_USER_DIR", m_user.path().toUtf8());
        qputenv("NPP_DATA_DIR", (QCoreApplication::applicationDirPath() + QStringLiteral("/data")).toUtf8());
        initRegexLocale();
    }

    void startsWithOneUntitledDocument()
    {
        CommandLine c;
        c.noSession = true;
        auto w = makeWindow(c);
        QCOMPARE(w->pane(0)->count(), 1);
        QVERIFY(w->currentBuffer()->isUntitled());
        QVERIFY(w->pane(1)->isHidden());
    }

    void openEditSaveCloseReopen()
    {
        const QString path = work("a.txt");
        writeAll(path, "hello\r\n");
        CommandLine c;
        c.noSession = true;
        auto w = makeWindow(c);
        Buffer* b = w->openFile(path);
        QVERIFY(b);
        QCOMPARE(w->pane(0)->count(), 1); // pristine "new 1" replaced
        QCOMPARE(b->eol, EolType::Windows);
        ScintillaEditBase* e = w->pane(0)->editor();
        e->send(SCI_DOCUMENTEND);
        e->send(SCI_ADDTEXT, 5, reinterpret_cast<sptr_t>("world"));
        QVERIFY(b->dirty);
        QVERIFY(w->actions()->trigger(IDM_FILE_SAVE));
        QVERIFY(!b->dirty);
        QCOMPARE(readAll(path), std::string("hello\r\nworld"));

        QVERIFY(w->actions()->trigger(IDM_FILE_CLOSE));
        QVERIFY(!w->files().findByPath(path.toStdString()));
        QCOMPARE(w->pane(0)->count(), 1); // replaced by a new document
        QVERIFY(w->actions()->trigger(IDM_FILE_RESTORELASTCLOSEDFILE));
        QVERIFY(w->files().findByPath(path.toStdString()));
        QCOMPARE(text(w->pane(0)->editor()), std::string("hello\r\nworld"));
    }

    void splitViewMoveAndClone()
    {
        writeAll(work("s1.txt"), "one");
        writeAll(work("s2.txt"), "two");
        CommandLine c;
        c.noSession = true;
        auto w = makeWindow(c);
        w->openFile(work("s1.txt"));
        Buffer* b2 = w->openFile(work("s2.txt"));
        QCOMPARE(w->pane(0)->count(), 2);
        QVERIFY(w->actions()->trigger(IDM_VIEW_CLONE_TO_ANOTHER_VIEW));
        QCOMPARE(w->pane(1)->count(), 1);
        QCOMPARE(w->pane(0)->count(), 2);
        QVERIFY(!w->pane(1)->isHidden());
        QCOMPARE(w->currentPane(), w->pane(1));
        // An edit in one view shows in the other (shared document)
        w->pane(1)->editor()->send(SCI_APPENDTEXT, 1, reinterpret_cast<sptr_t>("!"));
        w->pane(0)->activate(b2->id);
        QCOMPARE(text(w->pane(0)->editor()), std::string("two!"));
        // Closing the clone keeps the buffer
        QVERIFY(w->actions()->trigger(IDM_FILE_CLOSE));
        QVERIFY(w->files().find(b2->id));
        QCOMPARE(w->pane(1)->count(), 0);
        // Move
        w->pane(0)->activate(b2->id);
        QVERIFY(w->actions()->trigger(IDM_VIEW_GOTO_ANOTHER_VIEW));
        QCOMPARE(w->pane(0)->count(), 1);
        QCOMPARE(w->pane(1)->count(), 1);
    }

    void encodingAndEol()
    {
        const QString path = work("enc.txt");
        writeAll(path, "caf\xC3\xA9\n");
        CommandLine c;
        c.noSession = true;
        auto w = makeWindow(c);
        Buffer* b = w->openFile(path);
        QVERIFY(b);
        QVERIFY(w->actions()->trigger(IDM_FORMAT_CONV2_UTF_8)); // UTF-8 with BOM
        QVERIFY(w->actions()->trigger(IDM_FORMAT_TODOS));
        QVERIFY(w->actions()->trigger(IDM_FILE_SAVE));
        QCOMPARE(readAll(path), std::string("\xEF\xBB\xBF" "caf\xC3\xA9\r\n"));
        // Encode in ANSI: reinterpret the bytes (reload)
        QVERIFY(w->actions()->trigger(IDM_FORMAT_ANSI));
        QCOMPARE(b->uniMode, UniMode::Ansi);
    }

    void languageMenu()
    {
        CommandLine c;
        c.noSession = true;
        auto w = makeWindow(c);
        QVERIFY(w->actions()->trigger(IDM_LANG_CPP));
        QCOMPARE(w->currentBuffer()->lang, L_CPP);
        QCOMPARE(w->pane(0)->editor()->send(SCI_GETLEXER), sptr_t(3)); // SCLEX_CPP
        QVERIFY(w->actions()->trigger(IDM_LANG_TEXT));
        QCOMPARE(w->currentBuffer()->lang, L_TEXT);
    }

    void sessionRoundTrip()
    {
        writeAll(work("x1.cpp"), "int a;\nint b;\n");
        writeAll(work("x2.txt"), "x2");
        const QString sessionFile = work("session.xml");
        {
            CommandLine c;
            c.noSession = true;
            auto w = makeWindow(c);
            w->openFile(work("x1.cpp"));
            w->pane(0)->editor()->send(SCI_GOTOPOS, 8);
            w->openFile(work("x2.txt"));
            w->actions()->trigger(IDM_VIEW_GOTO_ANOTHER_VIEW);
            QVERIFY(w->saveSessionTo(sessionFile));
        }
        const auto s = loadSession(sessionFile.toStdString());
        QVERIFY(s);
        QCOMPARE(s->mainView.files.size(), size_t(1));
        QCOMPARE(s->subView.files.size(), size_t(1));
        QCOMPARE(s->mainView.files[0].lang, std::string("C++")); // upstream menu name
        QCOMPARE(s->mainView.files[0].endPos, size_t(8));

        CommandLine c;
        c.sessionFile = sessionFile;
        auto w = makeWindow(c);
        QCOMPARE(w->pane(0)->count(), 1);
        QCOMPARE(w->pane(1)->count(), 1);
        QCOMPARE(w->files().findByPath(work("x1.cpp").toStdString())->lang, L_CPP);
        QCOMPARE(w->pane(0)->editor()->send(SCI_GETCURRENTPOS), sptr_t(8));
    }

    void lastSessionWithSnapshotBackup()
    {
        // Dirty untitled document survives a restart through the snapshot backup
        {
            auto w = makeWindow();
            w->pane(0)->editor()->send(SCI_APPENDTEXT, 7, reinterpret_cast<sptr_t>("unsaved"));
            w->shutdownForTest();
        }
        auto w = makeWindow();
        QCOMPARE(w->pane(0)->count(), 1);
        QCOMPARE(text(w->pane(0)->editor()), std::string("unsaved"));
        QVERIFY(w->currentBuffer()->isUntitled());
        QVERIFY(w->currentBuffer()->dirty);
        // Settings were written to the test user dir, not to the real profile
        QVERIFY(QFile::exists(m_user.filePath(QStringLiteral("config.xml"))));
    }

    void commandLine()
    {
        const auto c = parseCommandLine({QStringLiteral("npp"), QStringLiteral("-n12"), QStringLiteral("-c3"),
                                         QStringLiteral("-lcpp"), QStringLiteral("-multiInst"), QStringLiteral("-ro"), QStringLiteral("-x100"), QStringLiteral("-y-20"),
                                         QStringLiteral("-openSession"), QStringLiteral("s.xml"), QStringLiteral("f.txt")});
        QCOMPARE(c.line, 12);
        QCOMPARE(c.column, 3);
        QCOMPARE(c.x, 100);
        QCOMPARE(c.y, -20);
        QCOMPARE(c.language, QStringLiteral("cpp"));
        QVERIFY(c.multiInstance && c.readOnly);
        QCOMPARE(c.sessionFile, QStringLiteral("s.xml"));
        QCOMPARE(c.files, QStringList{QStringLiteral("f.txt")});

        writeAll(work("lines.txt"), "1\n2\n3\n");
        CommandLine o;
        o.noSession = true;
        o.files << work("lines.txt");
        o.line = 3;
        o.readOnly = true;
        auto w = makeWindow(o);
        ScintillaEditBase* e = w->pane(0)->editor();
        QCOMPARE(e->send(SCI_LINEFROMPOSITION, static_cast<uptr_t>(e->send(SCI_GETCURRENTPOS))), sptr_t(2));
        QVERIFY(w->currentBuffer()->isReadOnly());
    }

    // Upstream-format settings folder: config.xml, shortcuts.xml, session.xml
    void upstreamSettingsFolder()
    {
        QTemporaryDir user;
        qputenv("NPP_QT_USER_DIR", user.path().toUtf8());
        const QString py = work("compat.py");
        writeAll(py, "import os\nprint(1)\n");
        writeAll(user.filePath(QStringLiteral("config.xml")),
                 "<?xml version=\"1.0\" encoding=\"UTF-8\" ?>\n<NotepadPlus><GUIConfigs>"
                 "<GUIConfig name=\"Auto-detection\">auto</GUIConfig>"
                 "<GUIConfig name=\"Backup\" isSnapshotMode=\"no\" />"
                 "</GUIConfigs></NotepadPlus>\n");
        writeAll(user.filePath(QStringLiteral("shortcuts.xml")),
                 QStringLiteral("<?xml version=\"1.0\" encoding=\"UTF-8\" ?>\n<NotepadPlus><InternalCommands>"
                                "<Shortcut id=\"%1\" Ctrl=\"yes\" Alt=\"yes\" Shift=\"no\" Key=\"83\" />"
                                "</InternalCommands></NotepadPlus>\n").arg(IDM_FILE_SAVEALL).toUtf8());
        writeAll(user.filePath(QStringLiteral("session.xml")),
                 QStringLiteral("<?xml version=\"1.0\" encoding=\"UTF-8\" ?>\n<NotepadPlus><Session activeView=\"0\">"
                                "<mainView activeIndex=\"0\"><File firstVisibleLine=\"0\" xOffset=\"0\" scrollWidth=\"1\" "
                                "startPos=\"5\" endPos=\"5\" selMode=\"0\" offset=\"0\" wrapCount=\"1\" lang=\"Python\" "
                                "encoding=\"-1\" userReadOnly=\"no\" filename=\"%1\" backupFilePath=\"\" "
                                "originalFileLastModifTimestamp=\"0\" originalFileLastModifTimestampHigh=\"0\" "
                                "tabColourId=\"-1\" RTL=\"no\" tabPinned=\"no\" mapFirstVisibleDisplayLine=\"-1\" "
                                "mapFirstVisibleDocLine=\"-1\" mapLastVisibleDocLine=\"-1\" mapNbLine=\"-1\" "
                                "mapHigherPos=\"-1\" mapWidth=\"-1\" mapHeight=\"-1\" mapKByteInDoc=\"0\" "
                                "mapWrapIndentMode=\"-1\" mapIsWrap=\"no\" /></mainView><subView activeIndex=\"0\" />"
                                "</Session></NotepadPlus>\n").arg(QDir::toNativeSeparators(py)).toUtf8());
        {
            auto w = makeWindow();
            QCOMPARE(QFileInfo(QString::fromStdString(w->paths().sessionXml())), QFileInfo(user.filePath(QStringLiteral("session.xml"))));
            QVERIFY(w->config().gui.fileAutoDetection & cdAutoUpdate);
            QCOMPARE(w->actions()->action(IDM_FILE_SAVEALL)->shortcut(), QKeySequence(QStringLiteral("Ctrl+Alt+S")));
            QCOMPARE(w->pane(0)->count(), 1);
            Buffer* b = w->currentBuffer();
            QCOMPARE(b->lang, L_PYTHON);
            ScintillaEditBase* e = w->pane(0)->editor();
            QCOMPARE(e->send(SCI_GETLEXER), sptr_t(2)); // SCLEX_PYTHON
            QCOMPARE(e->send(SCI_GETCURRENTPOS), sptr_t(5));

            // External modification, auto update mode: reloaded silently
            writeAll(py, "changed\n");
            QFile f(py);
            QVERIFY(f.open(QIODevice::ReadWrite));
            QVERIFY(f.setFileTime(QDateTime::currentDateTime().addSecs(10), QFileDevice::FileModificationTime));
            f.close();
            QTRY_COMPARE_WITH_TIMEOUT(text(e), std::string("changed\n"), 5000);
            QVERIFY(!b->dirty);
        }
        qputenv("NPP_QT_USER_DIR", m_user.path().toUtf8());
    }

    // Second start forwards its files to the running instance and exits at once
    void singleInstanceForwarding()
    {
        const QString exe = QCoreApplication::applicationDirPath() + QStringLiteral("/notepadpp-qt");
        const QString key = QStringLiteral("npp-qt-test-%1").arg(QCoreApplication::applicationPid());
        qputenv("NPP_QT_INSTANCE_KEY", key.toUtf8());
        QProcess first;
        first.start(exe, {QStringLiteral("-nosession")});
        QVERIFY(first.waitForStarted());
        QLocalSocket probe;
        QTRY_VERIFY_WITH_TIMEOUT((probe.connectToServer(key), probe.waitForConnected(100)), 15000);
        probe.disconnectFromServer();

        writeAll(work("fwd.txt"), "fwd");
        QProcess second;
        second.start(exe, {work("fwd.txt")});
        QVERIFY(second.waitForFinished(10000));
        QCOMPARE(second.exitCode(), 0);
        QVERIFY(first.state() == QProcess::Running); // the first instance keeps the file
        first.kill();
        first.waitForFinished();
        qunsetenv("NPP_QT_INSTANCE_KEY");
    }

    void menuHasHandlersForMvpCommands()
    {
        CommandLine c;
        c.noSession = true;
        auto w = makeWindow(c);
        for (int id : {IDM_FILE_NEW, IDM_FILE_OPEN, IDM_FILE_SAVE, IDM_FILE_SAVEAS, IDM_FILE_SAVEALL, IDM_FILE_CLOSE,
                       IDM_FILE_CLOSEALL, IDM_FILE_RELOAD, IDM_EDIT_UNDO, IDM_EDIT_CUT, IDM_EDIT_COPY, IDM_EDIT_PASTE,
                       IDM_VIEW_WRAP, IDM_VIEW_GOTO_ANOTHER_VIEW, IDM_FORMAT_TOUNIX, IDM_FORMAT_ANSI, IDM_LANG_PYTHON})
            QVERIFY2(w->actions()->hasHandler(id), qPrintable(QString::number(id)));
        QVERIFY(!w->actions()->action(IDM_FILE_SAVE)->shortcut().isEmpty());
    }
};

QTEST_MAIN(TestMainWindow)
#include "test_mainwindow.moc"
