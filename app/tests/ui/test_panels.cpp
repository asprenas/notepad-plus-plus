// Phase 5 UI tests: docking panels (Function List, Document Map, Project Panels, Document List,
// Character Panel, Clipboard History, Folder as Workspace) and the remaining dialogs / tools.
#include "MainWindow.h"
#include "docking/DockManager.h"
#include "editor/EditorPane.h"
#include "menu/ActionRegistry.h"
#include "menu/CommandIds.h"
#include "panels/DocumentListPanel.h"
#include "panels/DocumentMapPanel.h"
#include "panels/FolderAsWorkspacePanel.h"
#include "panels/FunctionListPanel.h"
#include "panels/ProjectPanel.h"
#include "panels/SmallPanels.h"
#include "panels/UdlPanel.h"
#include "search/FindReplaceDialog.h"
#include "search/SearchResultsPanel.h"

#include <SciLexer.h>
#include <Scintilla.h>
#include <ScintillaEditBase.h>

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QPlainTextEdit>
#include <QPrinter>
#include <QRegularExpression>
#include <QListWidget>
#include <QTableWidget>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLineEdit>
#include <QMenu>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTimer>
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
        if (!f.open(QIODevice::WriteOnly))
            return {};
        f.write(content);
        return p;
    }
    ScintillaEditBase* editor() const { return m_w->currentPane()->editor(); }
    QByteArray docText() const
    {
        return QByteArray(reinterpret_cast<const char*>(editor()->send(SCI_GETCHARACTERPOINTER)));
    }

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

    void folderAsWorkspaceRootsAndRestore()
    {
        QDir(m_files.path()).mkpath(QStringLiteral("faw/deep/er"));
        const QString target = writeFile(QStringLiteral("faw/deep/er/t.txt"), "t");
        writeFile(QStringLiteral("faw/z.txt"), "z");
        const QString root = QDir(m_files.path()).filePath(QStringLiteral("faw"));
        FolderAsWorkspacePanel* fw = m_w->folderAsWorkspace();
        QVERIFY(fw->roots().isEmpty());
        fw->addRoot(root);
        fw->addRoot(QDir(root).filePath(QStringLiteral("deep"))); // inside an existing root: no new root
        QCOMPARE(fw->roots().size(), 1);
        QTreeWidgetItem* r = fw->tree()->topLevelItem(0);
        QCOMPARE(r->childCount(), 2);
        QCOMPARE(r->child(0)->text(0), QStringLiteral("deep")); // folders first
        QCOMPARE(r->child(1)->text(0), QStringLiteral("z.txt"));

        QVERIFY(fw->locate(target));
        QCOMPARE(QFileInfo(FolderAsWorkspacePanel::pathOf(fw->tree()->currentItem())), QFileInfo(target));
        emit fw->tree()->itemActivated(fw->tree()->currentItem(), 0);
        QVERIFY(m_w->currentBuffer()->path.find("t.txt") != std::string::npos);

        // file system changes are picked up
        writeFile(QStringLiteral("faw/new.txt"), "n");
        QTRY_COMPARE_WITH_TIMEOUT(r->childCount(), 3, 5000);

        // roots, expanded folders and selection are saved in config.xml and restored
        m_w->shutdownForTest();
        makeWindow();
        fw = m_w->folderAsWorkspace();
        QCOMPARE(fw->roots().size(), 1);
        QVERIFY(m_w->dockManager()->isPanelVisible(MainWindow::kPanelFolderAsWorkspace));
        QVERIFY(fw->tree()->currentItem());
        QCOMPARE(QFileInfo(FolderAsWorkspacePanel::pathOf(fw->tree()->currentItem())), QFileInfo(target));
        fw->removeAllRoots();
        QVERIFY(fw->roots().isEmpty());
    }

    void documentListColumnsGroupsAndActions()
    {
        QVERIFY(m_w->openFile(writeFile(QStringLiteral("b.cpp"), "b")));
        QVERIFY(m_w->openFile(writeFile(QStringLiteral("a.txt"), "a")));
        QVERIFY(m_w->actions()->trigger(IDM_VIEW_DOCLIST));
        DocumentListPanel* dl = m_w->documentList();
        QVERIFY(m_w->dockManager()->isPanelVisible(MainWindow::kPanelDocumentList));
        QStringList names = dl->names();
        QVERIFY2(names.contains(QStringLiteral("b.cpp")) && names.contains(QStringLiteral("a.txt")), qPrintable(names.join(',')));
        // name and extension in separate columns
        QTreeWidgetItem* first = dl->tree()->topLevelItem(dl->tree()->topLevelItemCount() - 1);
        QCOMPARE(first->text(1), QStringLiteral("txt"));
        QCOMPARE(dl->tree()->currentItem()->text(0), QStringLiteral("a"));

        // click activates
        QTreeWidgetItem* b = nullptr;
        for (int i = 0; i < dl->tree()->topLevelItemCount(); ++i)
            if (dl->tree()->topLevelItem(i)->text(0) == QStringLiteral("b"))
                b = dl->tree()->topLevelItem(i);
        QVERIFY(b);
        emit dl->tree()->itemClicked(b, 0);
        QVERIFY(m_w->currentBuffer()->path.find("b.cpp") != std::string::npos);

        // dirty state is reflected, groups appear with two views
        editor()->send(SCI_APPENDTEXT, 1, reinterpret_cast<intptr_t>("x"));
        QVERIFY(m_w->actions()->trigger(IDM_VIEW_GOTO_ANOTHER_VIEW));
        QCOMPARE(dl->tree()->topLevelItemCount(), 2);
        QCOMPARE(dl->tree()->topLevelItem(0)->text(0), QStringLiteral("Main View"));
        QCOMPARE(dl->tree()->topLevelItem(1)->childCount(), 1);

        // middle / menu close goes through the normal close (asks for dirty documents: undo first)
        editor()->send(SCI_UNDO);
        QTreeWidgetItem* moved = dl->tree()->topLevelItem(1)->child(0);
        const auto id = moved->data(0, Qt::UserRole).toULongLong();
        emit dl->closeRequested({{id, 1}});
        QCOMPARE(m_w->pane(1)->count(), 0);
        QCOMPARE(dl->tree()->topLevelItemCount() >= 1, true);

        // the Ext. column setting is saved in config.xml (MISC fileSwitcherWithoutExtColumn)
        DocumentListPanel::Columns c = dl->columns();
        c.ext = false;
        dl->setColumns(c);
        QVERIFY(dl->names().contains(QStringLiteral("a.txt")));
        QCOMPARE(dl->tree()->topLevelItem(0)->text(1), QString());
        m_w->shutdownForTest();
        QVERIFY(m_w->config().gui.fileSwitcherWithoutExtColumn);
    }

    void characterPanelInsertsCharactersAndEntities()
    {
        m_w->ensureDocument();
        QVERIFY(m_w->actions()->trigger(IDM_EDIT_CHAR_PANEL));
        CharacterPanel* cp = m_w->characterPanel();
        QVERIFY(m_w->dockManager()->isPanelVisible(MainWindow::kPanelCharacter));
        QTableWidget* t = cp->table();
        QCOMPARE(t->rowCount(), 256);
        QCOMPARE(t->item(10, 2)->text(), QStringLiteral("LF"));
        QCOMPARE(t->item(65, 1)->text(), QStringLiteral("41"));
        QCOMPARE(t->item(38, 3)->text(), QStringLiteral("&amp;"));
        QCOMPARE(t->item(128, 4)->text(), QStringLiteral("&#8364;")); // euro sign in Windows-1252

        emit t->cellDoubleClicked(65, 2);
        emit t->cellDoubleClicked(38, 3);
        QCOMPARE(docText(), QByteArray("A&amp;"));

        // Windows-1252 document: bytes above 127 are inserted as their Unicode characters (UTF-8 inside)
        QVERIFY(m_w->actions()->trigger(IDM_FORMAT_WIN_1252));
        QCOMPARE(cp->codePage(), 1252);
        QCOMPARE(t->item(233, 2)->text(), QString(QChar(0xE9)));
        emit t->cellDoubleClicked(233, 2);
        QVERIFY(docText().endsWith("\xC3\xA9"));
        // other code pages: no HTML columns
        QVERIFY(m_w->actions()->trigger(IDM_FORMAT_WIN_1251));
        QCOMPARE(t->item(38, 3)->text(), QString());
        QCOMPARE(t->item(0xC0, 2)->text(), QString(QChar(0x0410))); // Cyrillic A
    }

    void clipboardHistoryKeepsDistinctEntries()
    {
        m_w->ensureDocument();
        QVERIFY(m_w->actions()->trigger(IDM_EDIT_CLIPBOARDHISTORY_PANEL));
        ClipboardHistoryPanel* ch = m_w->clipboardHistory();
        QApplication::clipboard()->setText(QStringLiteral("first"));
        QApplication::clipboard()->setText(QStringLiteral("second\nline"));
        QTRY_COMPARE(ch->entries().size(), 2);
        QApplication::clipboard()->setText(QStringLiteral("first"));
        QTRY_COMPARE(ch->entries().first(), QStringLiteral("first"));
        QCOMPARE(ch->entries().size(), 2);
        QCOMPARE(ch->list()->item(1)->text(), QStringLiteral("second line"));
        ch->addEntry(QString(100, QLatin1Char('x')));
        QVERIFY(ch->list()->item(0)->text().endsWith(QStringLiteral("...")));
        editor()->send(SCI_CLEARALL);
        emit ch->list()->itemActivated(ch->list()->item(2));
        QCOMPARE(docText(), QByteArray("second\nline"));
    }

    void panelLayoutInConfigDockingManager()
    {
        QVERIFY(m_w->actions()->trigger(IDM_VIEW_FUNC_LIST));
        QVERIFY(m_w->actions()->trigger(IDM_VIEW_PROJECT_PANEL_3));
        QVERIFY(m_w->actions()->trigger(IDM_VIEW_PROJECT_PANEL_3)); // shown then hidden
        m_w->shutdownForTest();
        const DockingData& d = m_w->config().docking;
        auto find = [&d](int id) -> const DockingData::PluginDlg* {
            for (const auto& p : d.plugins)
                if (p.name == "Notepad++::InternalFunction" && p.id == id)
                    return &p;
            return nullptr;
        };
        QVERIFY(find(IDM_VIEW_FUNC_LIST));
        QVERIFY(find(IDM_VIEW_FUNC_LIST)->visible);
        QCOMPARE(find(IDM_VIEW_FUNC_LIST)->curr, 1); // right
        QVERIFY(!find(IDM_VIEW_PROJECT_PANEL_3) || !find(IDM_VIEW_PROJECT_PANEL_3)->visible);

        // without the backend state file (settings from the original Notepad++): config.xml is used
        m_w.reset();
        for (const QString& f : QDir(m_user.path()).entryList({QStringLiteral("dockLayout-*.bin")}))
            QVERIFY(QFile::remove(QDir(m_user.path()).filePath(f)));
        makeWindow();
        QVERIFY(m_w->dockManager()->isPanelVisible(MainWindow::kPanelFunctionList));
        QVERIFY(!m_w->dockManager()->isPanelVisible(MainWindow::projectPanelId(2)));
        m_w->actions()->trigger(IDM_VIEW_FUNC_LIST); // leave it hidden for the next tests
        m_w->shutdownForTest();
    }

    void dockingDataMapping()
    {
        DockingData d;
        d.plugins.push_back({"SomePlugin.dll", 7, 3, 0, true}); // kept untouched
        DockLayout l;
        l.panels.push_back({MainWindow::kPanelDocumentMap, true, DockArea::Floating});
        l.panels.push_back({MainWindow::kPanelDocumentList, true, DockArea::Bottom});
        l.panels.push_back({"npp.unknown", true, DockArea::Left});
        layoutToDockingData(l, d);
        QCOMPARE(d.plugins.size(), size_t(3));
        QCOMPARE(d.plugins[0].name, std::string("SomePlugin.dll"));
        QCOMPARE(d.plugins[1].id, IDM_VIEW_DOC_MAP);
        QCOMPARE(d.plugins[1].curr, 4);
        QCOMPARE(d.floating.size(), size_t(1));
        QCOMPARE(d.plugins[2].curr, 3);
        const DockLayout back = dockingDataToLayout(d);
        QCOMPARE(back.panels.size(), size_t(2));
        QCOMPARE(back.panels[0].area, DockArea::Floating);
        QCOMPARE(back.panels[1].area, DockArea::Bottom);
        // moving a panel keeps the previous container (upstream prev)
        l.panels = {{MainWindow::kPanelDocumentList, true, DockArea::Left}};
        layoutToDockingData(l, d);
        QCOMPARE(d.plugins[2].curr, 0);
        QCOMPARE(d.plugins[2].prev, 3);
    }

    void windowMenuSortAndWindowsDialog()
    {
        for (const char* n : {"c.txt", "a.cpp", "b.h"})
            QVERIFY(m_w->openFile(writeFile(QString::fromLatin1(n), QByteArray(n))));
        auto tabNames = [this] {
            QStringList out;
            for (BufferId id : m_w->currentPane()->buffers())
                out << QFileInfo(QString::fromStdString(m_w->files().find(id)->path)).fileName();
            return out;
        };
        QVERIFY(m_w->actions()->trigger(IDM_WINDOW_SORT_FN_ASC));
        QStringList names = tabNames();
        names.removeAll(QString()); // untitled "new 1" of a fresh window has no path
        QCOMPARE(names, (QStringList{QStringLiteral("a.cpp"), QStringLiteral("b.h"), QStringLiteral("c.txt")}));
        QVERIFY(m_w->actions()->trigger(IDM_WINDOW_SORT_FT_DSC));
        names = tabNames();
        names.removeAll(QString());
        QCOMPARE(names.first(), QStringLiteral("c.txt"));

        // the Window menu lists the documents of the current view
        m_w->updateWindowMenu();
        QMenu* menu = nullptr;
        for (QObject* o : m_w->actions()->action(IDM_WINDOW_WINDOWS)->associatedObjects())
            if ((menu = qobject_cast<QMenu*>(o)))
                break;
        QVERIFY(menu);
        int docs = 0;
        for (QAction* a : menu->actions())
            if (a->isCheckable() && a->text().contains(QStringLiteral(".")))
                ++docs;
        QVERIFY(docs >= 3);

        // Windows... dialog: list, close selection
        bool seen = false;
        QTimer::singleShot(0, this, [&] {
            QDialog* d = m_w->windowsDialog();
            QVERIFY(d);
            auto* tree = d->findChild<QTreeWidget*>(QStringLiteral("windowsList"));
            QVERIFY(tree);
            seen = tree->topLevelItemCount() >= 3;
            tree->clearSelection();
            for (int i = 0; i < tree->topLevelItemCount(); ++i)
                if (tree->topLevelItem(i)->text(0) == QStringLiteral("b.h"))
                    tree->topLevelItem(i)->setSelected(true);
            for (QPushButton* b : d->findChildren<QPushButton*>())
                if (b->text().contains(QStringLiteral("Close")))
                    b->click();
            d->accept();
        });
        QVERIFY(m_w->actions()->trigger(IDM_WINDOW_WINDOWS));
        QVERIFY(seen);
        QVERIFY(!tabNames().contains(QStringLiteral("b.h")));
    }

    void ctrlTabSwitchesInMruOrder()
    {
        const QString a = writeFile(QStringLiteral("one.txt"), "1"), b = writeFile(QStringLiteral("two.txt"), "2"),
                      c = writeFile(QStringLiteral("three.txt"), "3");
        QVERIFY(m_w->openFile(a));
        QVERIFY(m_w->openFile(b));
        QVERIFY(m_w->openFile(c));
        QVERIFY(m_w->openFile(a)); // MRU: one, three, two
        auto current = [this] { return QFileInfo(QString::fromStdString(m_w->currentBuffer()->path)).fileName(); };
        m_w->mutableConfig().gui.doTaskList = false;
        m_w->switchDocument(true);
        QCOMPARE(current(), QStringLiteral("three.txt")); // previous document, not the next tab
        m_w->switchDocument(true);
        QCOMPARE(current(), QStringLiteral("one.txt"));

        // with the switcher popup: Tab steps while Ctrl is held, the release activates
        m_w->mutableConfig().gui.doTaskList = true;
        m_w->setTaskListKeepOpen(true);
        m_w->switchDocument(true);
        QWidget* popup = m_w->taskList();
        QVERIFY(popup);
        QTest::keyClick(popup, Qt::Key_Tab, Qt::ControlModifier);
        QTest::keyRelease(popup, Qt::Key_Control);
        QCOMPARE(current(), QStringLiteral("two.txt")); // MRU one, three, two: second step
        QVERIFY(!m_w->taskList());

        // tab order mode
        m_w->mutableConfig().gui.styleMru = false;
        m_w->mutableConfig().gui.doTaskList = false;
        const int idx = m_w->currentPane()->currentIndex();
        m_w->switchDocument(true);
        QCOMPARE(m_w->currentPane()->currentIndex(), (idx + 1) % m_w->currentPane()->count());
        m_w->setTaskListKeepOpen(false);
    }

    void hashToolsSummaryAndHelp()
    {
        m_w->ensureDocument();
        editor()->send(SCI_SETTEXT, 0, reinterpret_cast<intptr_t>("abc def\r\nx"));
        // Generate from selection into clipboard
        editor()->send(SCI_SETSEL, 0, 3);
        QVERIFY(m_w->actions()->trigger(IDM_TOOL_MD5_GENERATEINTOCLIPBOARD));
        QCOMPARE(QApplication::clipboard()->text(), QStringLiteral("900150983cd24fb0d6963f7d28e17f72"));
        QVERIFY(m_w->actions()->trigger(IDM_TOOL_SHA256_GENERATEINTOCLIPBOARD));
        QVERIFY(QApplication::clipboard()->text().startsWith(QStringLiteral("ba7816bf")));

        // Generate... dialog: digest follows the text, per line option
        QDialog* d = m_w->hashDialog(0, false);
        QVERIFY(d->isVisible());
        auto* text = d->findChild<QPlainTextEdit*>(QStringLiteral("1931"));
        auto* result = d->findChild<QPlainTextEdit*>(QStringLiteral("1933"));
        auto* perLine = d->findChild<QCheckBox*>(QStringLiteral("1932"));
        QVERIFY(text && result && perLine);
        text->setPlainText(QStringLiteral("abc"));
        QCOMPARE(result->toPlainText(), QStringLiteral("900150983cd24fb0d6963f7d28e17f72"));
        text->setPlainText(QStringLiteral("abc\nabc"));
        perLine->setChecked(true);
        QCOMPARE(result->toPlainText(), QStringLiteral("900150983cd24fb0d6963f7d28e17f72\n900150983cd24fb0d6963f7d28e17f72\n"));
        QCOMPARE(m_w->hashDialog(0, false), d); // one dialog per kind
        QVERIFY(m_w->hashDialog(3, true)->windowTitle().contains(QStringLiteral("SHA-512")));

        // Summary
        editor()->send(SCI_SETSEL, 0, 7);
        const QString sum = m_w->summaryText();
        QVERIFY2(sum.contains(QStringLiteral("Characters (without line endings): 8")), qPrintable(sum));
        QVERIFY2(sum.contains(QStringLiteral("Words: 3")), qPrintable(sum));
        QVERIFY2(sum.contains(QStringLiteral("Lines: 2")), qPrintable(sum));
        QVERIFY2(sum.contains(QStringLiteral("Document length: 10")), qPrintable(sum));
        QVERIFY2(sum.contains(QStringLiteral("7 selected characters (7 bytes) in 1 ranges")), qPrintable(sum));

        QVERIFY(MainWindow::commandLineHelpText().contains(QStringLiteral("-settingsDir")));
        const QString dbg = m_w->debugInfoText();
        QVERIFY(dbg.contains(QStringLiteral("Qt : ")));
        QVERIFY(dbg.contains(QStringLiteral("Docking backend : ")));
    }

    void monitoringReloadsAndFollowsTheEnd()
    {
        const QString f = writeFile(QStringLiteral("log.txt"), "line 1\n");
        QVERIFY(m_w->openFile(f));
        QVERIFY(m_w->actions()->trigger(IDM_VIEW_MONITORING));
        QVERIFY(m_w->currentBuffer()->monitoring);
        QVERIFY(editor()->send(SCI_GETREADONLY));
        QTest::qWait(1100); // file times have a coarse resolution on some file systems
        QFile out(f);
        QVERIFY(out.open(QIODevice::Append));
        out.write("line 2\nline 3\n");
        out.close();
        QTRY_VERIFY_WITH_TIMEOUT(docText().contains("line 3"), 5000);
        QCOMPARE(editor()->send(SCI_GETCURRENTPOS), editor()->send(SCI_GETLENGTH));
        QVERIFY(m_w->actions()->trigger(IDM_VIEW_MONITORING));
        QVERIFY(!m_w->currentBuffer()->monitoring);
        QVERIFY(!editor()->send(SCI_GETREADONLY));
    }

    void shortcutsXmlIntegrity()
    {
        m_w->setQuietSecurityWarnings(true);
        const QString sx = QDir(m_user.path()).filePath(QStringLiteral("shortcuts.xml"));
        QFile::remove(sx);
        QFile f(sx);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("<NotepadPlus><InternalCommands/><Macros/><UserDefinedCommands/><PluginCommands/><ScintillaKeys/></NotepadPlus>");
        f.close();
        m_w->mutableConfig().gui.shortcutsXmlHmac.clear();
        QVERIFY(!m_w->shortcutsXmlTrusted()); // missing HMAC: shortcuts.xml opened read-only for review
        QVERIFY(m_w->currentBuffer()->path.find("shortcuts.xml") != std::string::npos);
        QVERIFY(m_w->currentBuffer()->userReadOnly);
        QVERIFY(m_w->actions()->trigger(IDM_EXECUTE_VALIDATE_SHORTCUTSXML));
        QCOMPARE(m_w->config().gui.shortcutsXmlHmac.size(), size_t(64));
        QVERIFY(m_w->shortcutsXmlTrusted());
        QVERIFY(f.open(QIODevice::Append));
        f.write("\n");
        f.close();
        QVERIFY(!m_w->shortcutsXmlTrusted()); // modified outside: tampered
        QFile::remove(sx);
    }

    void runCommandsAndUserCommandsMenu()
    {
        const QString f = writeFile(QStringLiteral("run me.txt"), "hello world\n");
        QVERIFY(m_w->openFile(f));
        editor()->send(SCI_GOTOPOS, 2);
        QCOMPARE(m_w->expandRunCommand(QStringLiteral("x $(FILE_NAME) $(CURRENT_WORD) $(CURRENT_LINE)")),
                 QStringLiteral("x run me.txt hello 0"));

        // Save... adds a user command to shortcuts.xml and the Run menu (HMAC recorded: trusted)
        m_w->setQuietSecurityWarnings(true);
        m_w->addUserCommand(QStringLiteral("My Command"), QStringLiteral("cmd /c echo $(FILE_NAME)"));
        QMenu* run = nullptr;
        for (QObject* o : m_w->actions()->action(IDM_EXECUTE)->associatedObjects())
            if ((run = qobject_cast<QMenu*>(o)))
                break;
        QVERIFY(run);
        QAction* mine = nullptr;
        for (QAction* a : run->actions())
            if (a->text() == QStringLiteral("My Command"))
                mine = a;
        QVERIFY(mine);
        QVERIFY(m_w->shortcutsXmlTrusted());
        QFile sx(QDir(m_user.path()).filePath(QStringLiteral("shortcuts.xml")));
        QVERIFY(sx.open(QIODevice::ReadOnly));
        QVERIFY(sx.readAll().contains("My Command"));
        sx.close();

#if defined(Q_OS_WIN)
        // a real process with the expanded arguments
        const QString out = QDir(m_files.path()).filePath(QStringLiteral("out.txt"));
        QVERIFY(m_w->runCommandLine(QStringLiteral("cmd.exe /c echo $(NAME_PART)> \"%1\"").arg(QDir::toNativeSeparators(out))));
        QTRY_VERIFY_WITH_TIMEOUT(QFileInfo(out).size() > 0, 10000);
        QByteArray got;
        QTRY_VERIFY_WITH_TIMEOUT([&] {
            QFile o(out);
            if (!o.open(QIODevice::ReadOnly))
                return false;
            got = o.readAll().trimmed();
            return !got.isEmpty();
        }(), 10000);
        QCOMPARE(got, QByteArray("run me"));
#endif
        QFile::remove(QDir(m_user.path()).filePath(QStringLiteral("shortcuts.xml")));
    }

    void printToPdf()
    {
        QByteArray text;
        for (int i = 1; i <= 300; ++i)
            text += "print line " + QByteArray::number(i) + "\n";
        QVERIFY(m_w->openFile(writeFile(QStringLiteral("print.txt"), text)));
        GuiConfig& g = m_w->mutableConfig().gui;
        g.printHeaderLeft = "$(FILE_NAME)";
        g.printFooterRight = "Page $(CURRENT_PRINTING_PAGE)";
        QCOMPARE(m_w->expandPrintVariables(g.printHeaderLeft), QStringLiteral("print.txt"));

        auto pdfPages = [](const QString& file) {
            QFile f(file);
            if (!f.open(QIODevice::ReadOnly))
                return -1;
            const QByteArray pdf = f.readAll();
            return static_cast<int>(QRegularExpression(QStringLiteral("/Type\\s*/Page[^s]"))
                                        .globalMatch(QString::fromLatin1(pdf))
                                        .hasNext()
                                        ? pdf.count("/Type /Page\n") + pdf.count("/Type /Page\r") + pdf.count("/Type /Page ")
                                              + pdf.count("/Type /Page/")
                                        : 0);
        };
        const QString pdf = QDir(m_files.path()).filePath(QStringLiteral("out.pdf"));
        int pages = 0;
        {
            QPrinter printer(QPrinter::HighResolution);
            printer.setOutputFormat(QPrinter::PdfFormat);
            printer.setOutputFileName(pdf);
            printer.setPageSize(QPageSize(QPageSize::A4));
            pages = m_w->printDocument(printer, false);
        }
        QVERIFY2(pages >= 4, qPrintable(QString::number(pages))); // 300 lines do not fit on a few A4 pages
        QVERIFY(QFileInfo(pdf).size() > 1000);
        const int inPdf = pdfPages(pdf);
        QVERIFY2(inPdf == pages, qPrintable(QStringLiteral("%1 / %2").arg(inPdf).arg(pages)));

        // selection only: one page; a page range
        editor()->send(SCI_SETSEL, 0, 40);
        {
            QPrinter printer(QPrinter::HighResolution);
            printer.setOutputFormat(QPrinter::PdfFormat);
            printer.setOutputFileName(pdf);
            QCOMPARE(m_w->printDocument(printer, true), 1);
        }
        {
            QPrinter printer(QPrinter::HighResolution);
            printer.setOutputFormat(QPrinter::PdfFormat);
            printer.setOutputFileName(pdf);
            printer.setPrintRange(QPrinter::PageRange);
            printer.setFromTo(2, 3);
            QCOMPARE(m_w->printDocument(printer, false), 2);
        }
        // line numbers margin restored after printing without line numbers
        g.printLineNumber = false;
        const auto w = editor()->send(SCI_GETMARGINWIDTHN, 0);
        {
            QPrinter printer(QPrinter::HighResolution);
            printer.setOutputFormat(QPrinter::PdfFormat);
            printer.setOutputFileName(pdf);
            QVERIFY(m_w->printDocument(printer, false) >= 4);
        }
        QCOMPARE(editor()->send(SCI_GETMARGINWIDTHN, 0), w);
    }

    void findInProjects()
    {
        QDir(m_files.path()).mkpath(QStringLiteral("fip"));
        const QString a = writeFile(QStringLiteral("fip/a.cpp"), "int needle = 1;\nneedle++;\n");
        const QString b = writeFile(QStringLiteral("fip/b.txt"), "needle\n");
        writeFile(QStringLiteral("fip/c.cpp"), "needle not in the project\n");
        ProjectPanel* pp = m_w->projectPanel(0);
        QTreeWidgetItem* proj = pp->addProject(QStringLiteral("P"));
        pp->addFile(proj, a);
        pp->addFile(proj, b);
        emit pp->findInProjectsRequested();
        FindReplaceDialog* d = m_w->findDialog();
        QCOMPARE(d->currentTab(), FindReplaceDialog::FindInProjectsTab);
        QVERIFY(d->projectPanelChecked(0));
        QVERIFY(!d->projectPanelChecked(1));
        SearchOptions o;
        o.findWhat = "needle";
        d->setOptions(o);
        d->setFilters(QStringLiteral("*.cpp"));
        emit d->actionRequested(FindReplaceDialog::Action::FindInProjects);
        SearchResultsPanel* r = m_w->searchResults(false);
        QVERIFY(r);
        const QByteArray results(reinterpret_cast<const char*>(r->view()->send(SCI_GETCHARACTERPOINTER)));
        QVERIFY2(results.contains("a.cpp"), results.constData());
        QVERIFY2(!results.contains("c.cpp") && !results.contains("b.txt"), results.constData()); // not in the project / filtered
        QVERIFY2(results.contains("2 hits") || results.contains("(2 hits"), results.constData());
        d->hide();
    }

    void udlDialogCreatesAndHighlights()
    {
        QVERIFY(m_w->actions()->trigger(IDM_LANG_USER_DLG));
        QVERIFY(m_w->dockManager()->isPanelVisible(MainWindow::kPanelUdl));
        UdlPanel* u = m_w->udlPanel();
        QVERIFY(u->createLanguage(QStringLiteral("MyLang"), false));
        QVERIFY(!u->createLanguage(QStringLiteral("MyLang"), false)); // name in use
        QCOMPARE(u->currentName(), QStringLiteral("MyLang"));
        auto setField = [u](int id, const QString& t) {
            auto* e = qobject_cast<QPlainTextEdit*>(u->control(id));
            QVERIFY(e);
            e->setPlainText(t);
        };
        auto* ext = u->findChild<QLineEdit*>(QStringLiteral("20008"));
        QVERIFY(ext);
        ext->setText(QStringLiteral("mylang"));
        emit ext->textEdited(ext->text());
        setField(22120, QStringLiteral("foo baz"));  // Keywords1
        setField(23320, QStringLiteral("#"));        // comment line open
        setField(24217, QStringLiteral("\""));       // delimiter 1 open
        setField(24219, QStringLiteral("\""));       // delimiter 1 close
        const UserLanguage* ul = m_w->userLanguages().find("MyLang");
        QVERIFY(ul);
        QCOMPARE(ul->ext, std::string("mylang"));
        QCOMPARE(ul->keywordLists[19], std::string("foo baz"));
        QCOMPARE(ul->keywordLists[0], std::string("00# 01 02 03 04"));
        QVERIFY(ul->keywordLists[27].find("00\"") == 0);

        // Styler: bold red keywords
        u->execStyler = [](QWidget* dlg) {
            qobject_cast<QCheckBox*>(dlg->findChild<QWidget*>(QStringLiteral("25001")))->setChecked(true);
            return true;
        };
        u->editStyle(4);
        QCOMPARE(m_w->userLanguages().find("MyLang")->style(4)->fontStyle & 1, 1);

        // the Language menu lists it; a file with its extension gets it
        bool inMenu = false;
        for (QObject* o : m_w->actions()->action(IDM_LANG_USER)->associatedObjects())
            if (auto* menu = qobject_cast<QMenu*>(o))
                for (QAction* a : menu->actions())
                    inMenu |= a->text() == QStringLiteral("MyLang");
        QVERIFY(inMenu);
        QVERIFY(m_w->openFile(writeFile(QStringLiteral("t.mylang"), "foo bar # note\n\"str\"\n")));
        QCOMPARE(m_w->currentBuffer()->lang, L_USER);
        QCOMPARE(m_w->currentBuffer()->userLangName, std::string("MyLang"));
        editor()->send(SCI_COLOURISE, 0, -1);
        QCOMPARE(editor()->send(SCI_GETSTYLEAT, 0), SCE_USER_STYLE_KEYWORD1);
        QCOMPARE(editor()->send(SCI_GETSTYLEAT, 9), SCE_USER_STYLE_COMMENTLINE);
        QCOMPARE(editor()->send(SCI_GETSTYLEAT, 16), SCE_USER_STYLE_DELIMITER1);
        QVERIFY(editor()->send(SCI_STYLEGETBOLD, SCE_USER_STYLE_KEYWORD1));

        // saved in userDefineLang.xml
        const QString xml = QDir(m_user.path()).filePath(QStringLiteral("userDefineLang.xml"));
        QTRY_VERIFY_WITH_TIMEOUT(QFileInfo(xml).exists(), 3000);
        auto fileText = [](const QString& f) {
            QFile x(f);
            return x.open(QIODevice::ReadOnly) ? x.readAll() : QByteArray();
        };
        QTRY_VERIFY_WITH_TIMEOUT(fileText(xml).contains("name=\"MyLang\""), 3000);

        // export / import (a second copy gets a new name), rename, remove
        const QString exported = QDir(m_files.path()).filePath(QStringLiteral("mylang.xml"));
        QVERIFY(u->exportCurrent(exported));
        QVERIFY(u->importFile(exported));
        QCOMPARE(u->currentName(), QStringLiteral("MyLang (2)"));
        QVERIFY(u->removeCurrent());
        u->selectLanguage(QStringLiteral("MyLang"));
        QVERIFY(u->renameCurrent(QStringLiteral("Other")));
        QCOMPARE(m_w->currentBuffer()->lang, L_TEXT); // its documents become normal text (upstream)
        QVERIFY(u->removeCurrent());
        QTRY_VERIFY_WITH_TIMEOUT(!fileText(xml).contains("name=\"Other\""), 3000);
    }

    void udlFilesImportAndHighlight()
    {
        // shipped UDLs + the test UDLs: import, apply, screenshot (results in the build tree)
        const QString out = QCoreApplication::applicationDirPath() + QStringLiteral("/udl-screenshots");
        QDir().mkpath(out);
        QStringList files;
        for (const QString& dir : {QStringLiteral(NPP_ROOT_DIR "/PowerEditor/bin/userDefineLangs"), QStringLiteral(NPP_TEST_DATA_DIR "/udl")})
            for (const QFileInfo& fi : QDir(dir).entryInfoList({QStringLiteral("*.xml")}, QDir::Files))
                files << fi.absoluteFilePath();
        QVERIFY2(files.size() >= 10, qPrintable(QString::number(files.size())));
        m_w->resize(900, 600);
        m_w->show();
        QVERIFY(QTest::qWaitForWindowExposed(m_w.get()));
        UdlPanel* u = m_w->udlPanel();
        int done = 0;
        for (const QString& f : files) {
            const auto before = m_w->userLanguages().languages().size();
            QVERIFY2(u->importFile(f), qPrintable(f));
            QVERIFY(m_w->userLanguages().languages().size() > before);
            const UserLanguage& l = m_w->userLanguages().languages().back();
            // sample text: the first words of each keyword group, comments, a number
            QByteArray sample;
            for (int k = 19; k < 27; ++k)
                if (!l.keywordLists[static_cast<size_t>(k)].empty())
                    sample += QByteArray::fromStdString(l.keywordLists[static_cast<size_t>(k)]).simplified().split(' ').value(0) + " ";
            sample += "\n" + QByteArray::fromStdString(udlFieldOf(l.keywordLists[0], 0)).split(' ').value(0) + " a comment\n12345\n";
            QVERIFY(m_w->openFile(writeFile(QStringLiteral("udl%1.txt").arg(done), sample)));
            QVERIFY(m_w->actions()->trigger(IDM_LANG_USER + static_cast<int>(m_w->userLanguages().languages().size())));
            QCOMPARE(m_w->currentBuffer()->userLangName, l.name);
            editor()->send(SCI_COLOURISE, 0, -1);
            // something is styled with a non default style
            bool styled = false;
            for (intptr_t p = 0; p < editor()->send(SCI_GETLENGTH); ++p)
                styled |= editor()->send(SCI_GETSTYLEAT, p) != SCE_USER_STYLE_DEFAULT;
            QVERIFY2(styled, qPrintable(f));
            m_w->grab().save(QDir(out).filePath(QStringLiteral("%1.png").arg(QFileInfo(f).completeBaseName())));
            ++done;
        }
        QVERIFY(done >= 10);
    }
};

QTEST_MAIN(TestPanels)
#include "test_panels.moc"
