// Phase 5 UI tests: docking panels (Function List, Document Map, Project Panels, Document List,
// Character Panel, Clipboard History, Folder as Workspace) and the remaining dialogs / tools.
#include "MainWindow.h"
#include "docking/DockManager.h"
#include "editor/EditorPane.h"
#include "menu/ActionRegistry.h"
#include "menu/CommandIds.h"
#include "panels/DocumentMapPanel.h"
#include "panels/FunctionListPanel.h"
#include "panels/ProjectPanel.h"

#include <Scintilla.h>
#include <ScintillaEditBase.h>

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLineEdit>
#include <QTemporaryDir>
#include <QTest>
#include <QTreeWidget>

using namespace npp::core;
using namespace npp::ui;
using namespace npp::ui::cmd;

class TestPanels : public QObject {
    Q_OBJECT
    QTemporaryDir m_user;
    QTemporaryDir m_files;
    std::unique_ptr<MainWindow> m_w;

    void makeWindow()
    {
        m_w.reset();
        CommandLine c;
        c.noSession = true;
        m_w = std::make_unique<MainWindow>(c);
    }
    QString writeFile(const QString& name, const QByteArray& content)
    {
        const QString p = QDir(m_files.path()).filePath(name);
        QFile f(p);
        f.open(QIODevice::WriteOnly);
        f.write(content);
        return p;
    }
    ScintillaEditBase* editor() const { return m_w->currentPane()->editor(); }

private slots:
    void initTestCase()
    {
        qputenv("NPP_QT_USER_DIR", m_user.path().toUtf8());
        qputenv("NPP_DATA_DIR", (QCoreApplication::applicationDirPath() + QStringLiteral("/data")).toUtf8());
    }
    void init() { makeWindow(); }
    void cleanup() { m_w.reset(); }

    void functionListShowsClassesAndFunctions()
    {
        const QString file = writeFile(QStringLiteral("a.cpp"),
            "int top() { return 1; }\n"
            "class Foo {\npublic:\n    void bar() {}\n    int baz(int x) { return x; }\n};\n"
            "void Foo::qux() {}\n"
            "static void alpha() {}\n");
        QVERIFY(m_w->openFile(file));
        QVERIFY(m_w->actions()->trigger(IDM_VIEW_FUNC_LIST));
        QVERIFY(m_w->dockManager()->isPanelVisible(MainWindow::kPanelFunctionList));
        FunctionListPanel* fl = m_w->functionList();
        QVERIFY(fl);
        QCOMPARE(fl->tree()->topLevelItem(0)->text(0), QStringLiteral("a.cpp"));
        const QStringList entries = fl->visibleEntries();
        QVERIFY2(entries.contains(QStringLiteral("top")), qPrintable(entries.join(',')));
        QVERIFY2(entries.contains(QStringLiteral("Foo::bar")), qPrintable(entries.join(',')));
        QVERIFY2(entries.contains(QStringLiteral("Foo::qux")), qPrintable(entries.join(',')));
        QVERIFY2(entries.contains(QStringLiteral("alpha")), qPrintable(entries.join(',')));

        // filter, then sort
        fl->setFilter(QStringLiteral("ba"));
        QCOMPARE(fl->visibleEntries(), (QStringList{QStringLiteral("Foo::bar"), QStringLiteral("Foo::baz")}));
        fl->setFilter({});
        fl->setSorted(true);
        const QStringList sorted = fl->visibleEntries();
        QCOMPARE(sorted.first(), QStringLiteral("alpha"));

        // activation moves the caret to the function
        QTreeWidgetItem* alpha = nullptr;
        for (int i = 0; i < fl->tree()->topLevelItem(0)->childCount(); ++i)
            if (fl->tree()->topLevelItem(0)->child(i)->text(0) == QStringLiteral("alpha"))
                alpha = fl->tree()->topLevelItem(0)->child(i);
        QVERIFY(alpha);
        emit fl->tree()->itemActivated(alpha, 0);
        const auto line = editor()->send(SCI_LINEFROMPOSITION, editor()->send(SCI_GETCURRENTPOS));
        QCOMPARE(line, 7);

        // edits reload the list (debounced)
        editor()->send(SCI_DOCUMENTEND);
        editor()->send(SCI_REPLACESEL, 0, reinterpret_cast<intptr_t>("void omega() {}\n"));
        QTRY_VERIFY_WITH_TIMEOUT(fl->visibleEntries().contains(QStringLiteral("omega")), 5000);

        // a language without parser gives an empty tree
        QVERIFY(m_w->actions()->trigger(IDM_LANG_TEXT));
        QCOMPARE(fl->tree()->topLevelItemCount(), 0);
        QVERIFY(m_w->actions()->trigger(IDM_VIEW_FUNC_LIST));
        QVERIFY(!m_w->dockManager()->isPanelVisible(MainWindow::kPanelFunctionList));
    }

    void documentMapFollowsEditor()
    {
        QByteArray text;
        for (int i = 0; i < 2000; ++i)
            text += "line " + QByteArray::number(i) + " some text here\n";
        QVERIFY(m_w->openFile(writeFile(QStringLiteral("long.txt"), text)));
        m_w->resize(1000, 700);
        m_w->show();
        QVERIFY(QTest::qWaitForWindowExposed(m_w.get()));
        QVERIFY(m_w->actions()->trigger(IDM_VIEW_DOC_MAP));
        DocumentMapPanel* dm = m_w->documentMap();
        QTRY_VERIFY(dm->isVisible());
        ScintillaEditBase* map = dm->map();
        QCOMPARE(map->send(SCI_GETDOCPOINTER), editor()->send(SCI_GETDOCPOINTER)); // same document
        QVERIFY(map->send(SCI_GETZOOM) < 0);
        QTRY_VERIFY(!dm->viewZone().isEmpty());
        const QRect top = dm->viewZone();

        editor()->send(SCI_SETFIRSTVISIBLELINE, 1500);
        QTRY_VERIFY(dm->viewZone().top() > top.top());
        QVERIFY(map->send(SCI_GETFIRSTVISIBLELINE) > 0);

        // click near the top of the map scrolls the editor back up
        dm->scrollEditorTo(1);
        QVERIFY(editor()->send(SCI_GETFIRSTVISIBLELINE) < 1500);

        // the map is not editable: key presses are swallowed
        const auto len = editor()->send(SCI_GETLENGTH);
        QTest::keyClick(map->viewport(), Qt::Key_A);
        QCOMPARE(editor()->send(SCI_GETLENGTH), len);

        // switching documents follows the active editor
        QVERIFY(m_w->openFile(writeFile(QStringLiteral("short.txt"), "x\n")));
        QCOMPARE(map->send(SCI_GETDOCPOINTER), editor()->send(SCI_GETDOCPOINTER));

        // wrapping follows the editor
        QVERIFY(m_w->actions()->trigger(IDM_VIEW_WRAP));
        m_w->documentMap()->sync();
        QCOMPARE(map->send(SCI_GETWRAPMODE), editor()->send(SCI_GETWRAPMODE));
        QVERIFY(m_w->actions()->trigger(IDM_VIEW_WRAP));
    }

    void projectPanelWorkspaceRoundTrip()
    {
        QDir(m_files.path()).mkpath(QStringLiteral("proj/sub"));
        writeFile(QStringLiteral("proj/a.txt"), "a");
        writeFile(QStringLiteral("proj/sub/b.txt"), "b");
        QVERIFY(m_w->actions()->trigger(IDM_VIEW_PROJECT_PANEL_2));
        QVERIFY(m_w->dockManager()->isPanelVisible(MainWindow::projectPanelId(1)));
        ProjectPanel* pp = m_w->projectPanel(1);
        QTreeWidgetItem* proj = pp->addProject(QStringLiteral("My Project"));
        QTreeWidgetItem* folder = pp->addFolder(proj, QStringLiteral("Virtual"));
        pp->addFilesFromDirectory(folder, QDir(m_files.path()).filePath(QStringLiteral("proj")), true);
        QVERIFY(pp->isDirty());
        QCOMPARE(pp->allFiles().size(), 2);
        // folders first (as upstream), then files
        QCOMPARE(folder->child(0)->text(0), QStringLiteral("sub"));
        QCOMPARE(folder->child(1)->text(0), QStringLiteral("a.txt"));
        pp->moveItem(folder->child(1), -1);
        QCOMPARE(folder->child(0)->text(0), QStringLiteral("a.txt"));

        const QString ws = QDir(m_files.path()).filePath(QStringLiteral("my.workspace"));
        QVERIFY(pp->saveWorkspaceAs(ws));
        QVERIFY(!pp->isDirty());
        QCOMPARE(pp->root()->text(0), QStringLiteral("my.workspace"));
        QFile f(ws);
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QByteArray xml = f.readAll();
        QVERIFY2(xml.contains("<Project name=\"My Project\">"), xml.constData());
        QVERIFY2(xml.contains("<Folder name=\"Virtual\">"), xml.constData());
        QVERIFY2(xml.contains("name=\"proj"), xml.constData()); // relative to the workspace folder

        // activating a file opens it
        emit pp->tree()->itemActivated(folder->child(0), 0);
        QVERIFY(m_w->currentBuffer()->path.find("a.txt") != std::string::npos);

        // the workspace is reopened in the next session (config.xml <ProjectPanels>)
        m_w->shutdownForTest();
        makeWindow();
        ProjectPanel* again = m_w->projectPanel(1);
        QCOMPARE(QFileInfo(again->workspacePath()), QFileInfo(ws));
        QCOMPARE(again->allFiles().size(), 2);
        QVERIFY(m_w->projectPanel(0)->workspacePath().isEmpty());
    }
};

QTEST_MAIN(TestPanels)
#include "test_panels.moc"
