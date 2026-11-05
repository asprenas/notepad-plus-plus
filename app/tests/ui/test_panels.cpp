// Phase 5 UI tests: docking panels (Function List, Document Map, Project Panels, Document List,
// Character Panel, Clipboard History, Folder as Workspace) and the remaining dialogs / tools.
#include "MainWindow.h"
#include "docking/DockManager.h"
#include "editor/EditorPane.h"
#include "menu/ActionRegistry.h"
#include "menu/CommandIds.h"
#include "panels/FunctionListPanel.h"

#include <Scintilla.h>
#include <ScintillaEditBase.h>

#include <QApplication>
#include <QDir>
#include <QFile>
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
};

QTEST_MAIN(TestPanels)
#include "test_panels.moc"
