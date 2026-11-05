// Phase 7 UI tests: plugin host. The sample plugins (Text Case in C, Document Info with a Qt panel),
// the ported MIME Tools and faulty plugins are copied to <settings>/plugins of a temporary user
// directory, then a MainWindow loads them.
#include "MainWindow.h"
#include "ShortcutMapper.h"
#include "docking/DockManager.h"
#include "editor/EditorPane.h"
#include "menu/ActionRegistry.h"
#include "menu/CommandIds.h"
#include "plugins/PluginHost.h"

#include "core/buffer/Buffer.h"

#include <Scintilla.h>
#include <ScintillaEditBase.h>

#include <npp_plugin_api.h>
#define NPP_COMPAT_NO_ENUMS
#include <npp_msgs.h>

#include <QAction>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QTabBar>
#include <QApplication>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolBar>
#include <QtTest>

#include <memory>

using namespace npp::core;
using namespace npp::ui;
using namespace npp::ui::cmd;

namespace {

// textcase.c: NPPM_MSGTOPLUGIN protocol
constexpr long kTextCaseStats = 1;
struct TextCaseStats {
    int ready, opened, saved, closed, activated, langChanged, shutdown, modified;
};

bool copyDir(const QString& from, const QString& to)
{
    QDir().mkpath(to);
    bool any = false;
    for (const QFileInfo& f : QDir(from).entryInfoList(QDir::Files)) {
        QFile::remove(to + QLatin1Char('/') + f.fileName());
        any = QFile::copy(f.absoluteFilePath(), to + QLatin1Char('/') + f.fileName()) || any;
    }
    return any;
}

} // namespace

class TestPlugins : public QObject {
    Q_OBJECT
    QTemporaryDir m_user;
    QTemporaryDir m_files;
    std::unique_ptr<MainWindow> m_w;

    void makeWindow(bool noPlugin = false)
    {
        m_w.reset();
        CommandLine c;
        c.noSession = true;
        c.noPlugin = noPlugin;
        m_w = std::make_unique<MainWindow>(c);
        m_w->setQuietSecurityWarnings(true);
        QTest::qWait(20); // NPPN_READY
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
    QByteArray readFile(const QString& path)
    {
        QFile f(path);
        return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
    }
    ScintillaEditBase* editor() const { return m_w->currentPane()->editor(); }
    QByteArray docText() const { return QByteArray(reinterpret_cast<const char*>(editor()->send(SCI_GETCHARACTERPOINTER))); }
    void setText(const char* text)
    {
        editor()->send(SCI_SETTEXT, 0, reinterpret_cast<intptr_t>(text));
        editor()->send(SCI_SELECTALL);
    }
    int command(const QString& plugin, const QString& name) const
    {
        for (const PluginHost::Command& c : m_w->plugins()->commands())
            if (c.plugin == plugin && c.name == name)
                return c.cmdId;
        return 0;
    }
    TextCaseStats stats()
    {
        TextCaseStats s{};
        NppCommunicationInfo ci{kTextCaseStats, "test", &s};
        const auto ok = m_w->plugins()->message(NPPM_MSGTOPLUGIN, reinterpret_cast<uintptr_t>("textcase"), reinterpret_cast<intptr_t>(&ci));
        if (!ok)
            qWarning("NPPM_MSGTOPLUGIN to textcase failed");
        return s;
    }
    intptr_t msg(unsigned m, uintptr_t w = 0, intptr_t l = 0) { return m_w->plugins()->message(m, w, l); }

private slots:
    void initTestCase()
    {
        // a plugin message box (error path) must fail the test, not block it
        auto* closer = new QTimer(this);
        connect(closer, &QTimer::timeout, this, [] {
            if (QWidget* m = QApplication::activeModalWidget()) {
                qWarning("closing modal %s", qPrintable(m->windowTitle()));
                m->close();
            }
        });
        closer->start(500);
        qputenv("NPP_QT_USER_DIR", m_user.path().toUtf8());
        qputenv("NPP_DATA_DIR", (QCoreApplication::applicationDirPath() + QStringLiteral("/data")).toUtf8());
        const QString plugins = m_user.path() + QStringLiteral("/plugins/");
        for (const char* n : {"textcase", "docinfo", "mimeTools"})
            if (!copyDir(QStringLiteral(NPP_PLUGIN_DIR "/") + QLatin1String(n), plugins + QLatin1String(n)))
                QSKIP("sample plugins not built (NPP_BUILD_PLUGINS=OFF)");
        for (const char* n : {"badversion", "initfail", "win32like", "throwing"})
            QVERIFY(copyDir(QStringLiteral(NPP_TEST_PLUGIN_DIR "/") + QLatin1String(n), plugins + QLatin1String(n)));
        // not a library at all
        QDir().mkpath(plugins + QStringLiteral("broken"));
#if defined(Q_OS_WIN)
        const QString brokenFile = plugins + QStringLiteral("broken/broken.dll");
#else
        const QString brokenFile = plugins + QStringLiteral("broken/libbroken.so");
#endif
        QFile f(brokenFile);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("this is not a shared library");
    }
    void init() { makeWindow(); }
    void cleanup() { m_w.reset(); }

    void loadsGoodPluginsAndReportsBadOnes()
    {
        PluginHost* h = m_w->plugins();
        QVERIFY(h);
        const QStringList loaded = h->loadedPlugins();
        for (const char* n : {"Text Case", "Document Info", "MIME Tools", "Throwing"})
            QVERIFY2(loaded.contains(QLatin1String(n)), qPrintable(loaded.join(',')));
        QVERIFY(!loaded.contains(QStringLiteral("Init Fail")));

        QStringList errors;
        for (const auto& e : h->errors())
            errors << QFileInfo(e.file).fileName() + QStringLiteral(": ") + e.reason;
        const QString all = errors.join('\n');
        auto has = [&](const char* file, const char* text) {
            for (const QString& e : errors)
                if (e.contains(QLatin1String(file), Qt::CaseInsensitive) && e.contains(QLatin1String(text)))
                    return true;
            return false;
        };
        QVERIFY2(has("badversion", "API version 99"), qPrintable(all));
        QVERIFY2(has("initfail", "returned 5"), qPrintable(all));
        QVERIFY2(has("win32like", "Win32"), qPrintable(all));
        QVERIFY2(has("broken", "cannot be loaded"), qPrintable(all));
        QCOMPARE(errors.size(), 4);
        // the error box is not modal: the application keeps working
        QWidget* box = m_w->findChild<QWidget*>(QStringLiteral("pluginErrors"));
        QVERIFY(!box || !box->isModal());

        // Plugins menu: one sub menu per plugin with commands, above "Open Plugins Folder..."
        QMenu* textCase = m_w->menuBar()->findChild<QMenu*>(QStringLiteral("pluginMenu_textcase"));
        QVERIFY(textCase);
        QStringList texts;
        for (QAction* a : textCase->actions())
            texts << (a->isSeparator() ? QStringLiteral("---") : a->text());
        QCOMPARE(texts, (QStringList{"UPPERCASE", "lowercase", "Swap case", "---", "Trim trailing spaces on save", "Statistics..."}));
        QMenu* mime = m_w->menuBar()->findChild<QMenu*>(QStringLiteral("pluginMenu_mimetools"));
        QVERIFY(mime);
        QCOMPARE(mime->actions().size(), 22); // upstream funcItem count, separators included
        QVERIFY(!m_w->menuBar()->findChild<QMenu*>(QStringLiteral("pluginMenu_initfail")));
        QMenu* pluginsMenu = qobject_cast<QMenu*>(textCase->menuAction()->associatedObjects().value(0));
        QVERIFY(pluginsMenu);
        const QList<QAction*> top = pluginsMenu->actions();
        QVERIFY(top.indexOf(textCase->menuAction()) < top.indexOf(m_w->actions()->action(IDM_SETTING_OPENPLUGINSDIR)));
    }

    void noPluginOption()
    {
        makeWindow(true);
        QVERIFY(!m_w->plugins());
        QVERIFY(!m_w->menuBar()->findChild<QMenu*>(QStringLiteral("pluginMenu_textcase")));
    }

    void textCaseCommandsEditTheDocument()
    {
        m_w->actions()->trigger(IDM_FILE_NEW);
        setText("Hello World 123");
        QVERIFY(m_w->actions()->trigger(command("Text Case", "UPPERCASE")));
        QCOMPARE(docText(), QByteArray("HELLO WORLD 123"));
        editor()->send(SCI_SELECTALL);
        QVERIFY(m_w->actions()->trigger(command("Text Case", "lowercase")));
        QCOMPARE(docText(), QByteArray("hello world 123"));
        setText("aBc");
        QVERIFY(m_w->actions()->trigger(command("Text Case", "Swap case")));
        QCOMPARE(docText(), QByteArray("AbC"));

        // default shortcut of the plugin item
        QAction* upper = m_w->actions()->action(command("Text Case", "UPPERCASE"));
        QCOMPARE(upper->shortcut(), QKeySequence(Qt::CTRL | Qt::ALT | Qt::SHIFT | Qt::Key_U));
        NppShortcut sk{};
        QCOMPARE(msg(NPPM_GETSHORTCUTBYCMDID, static_cast<uintptr_t>(command("Text Case", "UPPERCASE")), reinterpret_cast<intptr_t>(&sk)), 1);
        QVERIFY(sk.ctrl && sk.alt && sk.shift);
        QCOMPARE(sk.key, 'U');
    }

    void notificationsReachThePlugin()
    {
        TextCaseStats s0 = stats();
        QCOMPARE(s0.ready, 1);
        const QString a = writeFile(QStringLiteral("a.txt"), "first\n");
        const QString b = writeFile(QStringLiteral("b.cpp"), "int main() {}\n");
        QVERIFY(m_w->openFile(a));
        QVERIFY(m_w->openFile(b));
        editor()->send(SCI_APPENDTEXT, 3, reinterpret_cast<intptr_t>("// "));
        QVERIFY(m_w->actions()->trigger(IDM_FILE_SAVE));
        m_w->actions()->trigger(IDM_LANG_PYTHON);
        m_w->actions()->trigger(IDM_FILE_CLOSE);
        const TextCaseStats s = stats();
        QCOMPARE(s.opened - s0.opened, 2);
        QCOMPARE(s.saved - s0.saved, 1);
        QCOMPARE(s.closed - s0.closed, 2);       // the pristine "new 1" replaced by a.txt, then b.cpp
        QVERIFY(s.langChanged > s0.langChanged);
        QVERIFY(s.activated >= s0.activated + 3); // a, b, then a after the close
        QVERIFY(s.modified > s0.modified);       // SCN_MODIFIED of the editor
    }

    void checkedItemTrimsOnSave()
    {
        const int id = command("Text Case", "Trim trailing spaces on save");
        QAction* a = m_w->actions()->action(id);
        QVERIFY(!a->isChecked());
        QVERIFY(m_w->actions()->trigger(id));
        QVERIFY(a->isCheckable());
        QVERIFY(a->isChecked());
        const QString f = writeFile(QStringLiteral("trim.txt"), "a  \nb\t\n");
        QVERIFY(m_w->openFile(f));
        editor()->send(SCI_APPENDTEXT, 2, reinterpret_cast<intptr_t>("c "));
        QVERIFY(m_w->actions()->trigger(IDM_FILE_SAVE));
        QCOMPARE(readFile(f), QByteArray("a\nb\nc"));
        QVERIFY(m_w->actions()->trigger(id));
        QVERIFY(!a->isChecked());
    }

    void qtPanelPluginShowsDocumentInfo()
    {
        m_w->show(); // the panel refreshes while it is visible
        QVERIFY(QTest::qWaitForWindowExposed(m_w.get()));
        const std::string panel = PluginHost::dockId("docinfo", "info");
        DockManager* dm = m_w->dockManager();
        const auto ids = dm->panelIds();
        QVERIFY(std::find(ids.begin(), ids.end(), panel) != ids.end());
        QVERIFY(!dm->isPanelVisible(panel));

        const QString f = writeFile(QStringLiteral("info.txt"), "one two\nthree\n");
        QVERIFY(m_w->openFile(f));
        const int toggle = command("Document Info", "Document Info panel");
        // toolbar button added on NPPN_TBMODIFICATION (icon file next to the plugin library)
        QVERIFY(m_w->mainToolbar()->actions().contains(m_w->actions()->action(toggle)));
        QVERIFY(!m_w->actions()->action(toggle)->icon().isNull());
        QVERIFY(m_w->actions()->trigger(toggle));
        QTRY_VERIFY(dm->isPanelVisible(panel));
        QAction* a = m_w->actions()->action(toggle);
        QTRY_VERIFY(a->isChecked());

        QWidget* w = m_w->findChild<QWidget*>(QString::fromStdString(panel));
        QVERIFY(w);
        auto label = [w](const char* name) {
            QLabel* l = w->findChild<QLabel*>(QLatin1String(name));
            return l ? l->text() : QStringLiteral("<none>");
        };
        QTRY_COMPARE(label("name"), QStringLiteral("info.txt"));
        QCOMPARE(label("words"), QStringLiteral("3"));
        QCOMPARE(label("lines"), QStringLiteral("3"));

        // follows the active document and edits
        const QString g = writeFile(QStringLiteral("code.py"), "print(1)\n");
        QVERIFY(m_w->openFile(g));
        QTRY_COMPARE(label("name"), QStringLiteral("code.py"));
        QCOMPARE(label("language"), QStringLiteral("Python"));
        editor()->send(SCI_APPENDTEXT, 6, reinterpret_cast<intptr_t>("a b c\n"));
        QTRY_COMPARE(label("words"), QStringLiteral("4"));

        QVERIFY(m_w->actions()->trigger(toggle));
        QTRY_VERIFY(!dm->isPanelVisible(panel));
        QTRY_VERIFY(!a->isChecked());
    }

    void portedMimeToolsEncodesAndDecodes()
    {
        m_w->actions()->trigger(IDM_FILE_NEW);
        setText("Hello, Notepad++ Qt!");
        QVERIFY(m_w->actions()->trigger(command("MIME Tools", "Base64 Encode with padding")));
        QCOMPARE(docText(), QByteArray("SGVsbG8sIE5vdGVwYWQrKyBRdCE="));
        editor()->send(SCI_SELECTALL);
        QVERIFY(m_w->actions()->trigger(command("MIME Tools", "Base64 Decode")));
        QCOMPARE(docText(), QByteArray("Hello, Notepad++ Qt!"));

        setText("a b&c=d/\xc3\xa9");
        QVERIFY(m_w->actions()->trigger(command("MIME Tools", "URL Encode (Full)")));
        QCOMPARE(docText(), QByteArray("%61%20%62%26%63%3D%64%2F%C3%A9"));
        editor()->send(SCI_SELECTALL);
        QVERIFY(m_w->actions()->trigger(command("MIME Tools", "URL Decode")));
        QCOMPARE(docText(), QByteArray("a b&c=d/\xc3\xa9"));

        setText("ab\ncd");
        QVERIFY(m_w->actions()->trigger(command("MIME Tools", "Base64 Encode by line")));
        QCOMPARE(docText(), QByteArray("YWI\nY2Q"));
        setText("SGVsbG8=");
        QVERIFY(m_w->actions()->trigger(command("MIME Tools", "Base64 Decode strict")));
        QCOMPARE(docText(), QByteArray("Hello"));
        setText("a b/c");
        QVERIFY(m_w->actions()->trigger(command("MIME Tools", "URL Encode (RFC1738)")));
        QCOMPARE(docText(), QByteArray("a%20b%2Fc")); // MIME Tools reserves "/" (gReservedAscii)
        // SAML: URL encoded Base64 of raw deflate
        setText("sylOzM0psApKLS7IzytOVfB0sVUqMlSyy8%2B20UeVsgMA");
        QVERIFY(m_w->actions()->trigger(command("MIME Tools", "SAML Decode")));
        QCOMPARE(docText(), QByteArray("<samlp:Response ID=\"r1\">ok</samlp:Response>"));

        setText("caf\xc3\xa9 = 1");
        QVERIFY(m_w->actions()->trigger(command("MIME Tools", "Quoted-printable Encode")));
        QCOMPARE(docText(), QByteArray("caf=C3=A9 =3D 1"));
        editor()->send(SCI_SELECTALL);
        QVERIFY(m_w->actions()->trigger(command("MIME Tools", "Quoted-printable Decode")));
        QCOMPARE(docText(), QByteArray("caf\xc3\xa9 = 1"));
    }

    void throwingPluginIsDisabledAppKeepsRunning()
    {
        QVERIFY(m_w->plugins()->isEnabled(QStringLiteral("Throwing")));
        const int id = command("Throwing", "Throw");
        QVERIFY(id);
        m_w->actions()->trigger(id); // throws inside the plugin
        QVERIFY(!m_w->plugins()->isEnabled(QStringLiteral("Throwing")));
        QVERIFY(!m_w->actions()->action(id)->isEnabled());
        QVERIFY(!m_w->actions()->trigger(id)); // disabled
        bool reported = false;
        for (const auto& e : m_w->plugins()->errors())
            reported = reported || e.reason.contains(QStringLiteral("boom from a menu command"));
        QVERIFY(reported);
        // the other plugins and the editor still work
        m_w->actions()->trigger(IDM_FILE_NEW);
        setText("ok");
        QVERIFY(m_w->actions()->trigger(command("Text Case", "UPPERCASE")));
        QCOMPARE(docText(), QByteArray("OK"));

        // a throwing notification handler is disabled too
        makeWindow();
        QVERIFY(m_w->actions()->trigger(command("Throwing", "Throw on next notification")));
        m_w->actions()->trigger(IDM_FILE_NEW); // NPPN_BUFFERACTIVATED
        QVERIFY(!m_w->plugins()->isEnabled(QStringLiteral("Throwing")));
        QVERIFY(m_w->plugins()->isEnabled(QStringLiteral("Text Case")));
    }

    void shortcutsXmlAndShortcutMapper()
    {
        // shortcuts.xml <PluginCommands> overrides the plugin default (module name as upstream: file name)
        m_w.reset();
#if defined(Q_OS_WIN)
        const char* module = "textcase.dll";
#else
        const char* module = "libtextcase.so";
#endif
        QFile f(m_user.path() + QStringLiteral("/shortcuts.xml"));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(QStringLiteral("<?xml version=\"1.0\" encoding=\"UTF-8\" ?>\n<NotepadPlus>\n<InternalCommands />\n<Macros />\n"
                               "<UserDefinedCommands />\n<PluginCommands>\n"
                               "<PluginCommand moduleName=\"%1\" internalID=\"2\" Ctrl=\"yes\" Alt=\"yes\" Shift=\"no\" Key=\"75\" />\n"
                               "</PluginCommands>\n<ScintillaKeys />\n</NotepadPlus>\n")
                    .arg(QLatin1String(module))
                    .toUtf8());
        f.close();
        makeWindow();
        QCOMPARE(m_w->actions()->action(command("Text Case", "Swap case"))->shortcut(),
                 QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_K));

        // Shortcut Mapper lists the loaded plugin commands; changing one writes <PluginCommands>
        std::vector<ShortcutMapper::PluginCommand> pcs;
        for (const PluginHost::Command& c : m_w->plugins()->commands())
            pcs.push_back({c.module, c.index, c.plugin + QStringLiteral(" - ") + c.name, c.defaultKey});
        ShortcutMapper mapper(m_w->shortcuts(), m_w->actions(), nullptr, m_w.get(), pcs);
        int rowsOfPlugins = 0, upperRow = -1;
        for (const auto& r : mapper.rows())
            if (r.tab == 3) {
                ++rowsOfPlugins;
                if (r.name == QStringLiteral("Text Case - UPPERCASE"))
                    upperRow = r.id;
                if (r.name == QStringLiteral("Text Case - Swap case"))
                    QCOMPARE(r.key, (KeyCombo{true, true, false, 75}));
            }
        QCOMPARE(rowsOfPlugins, static_cast<int>(pcs.size()));
        QVERIFY(upperRow >= 0);
        QVERIFY(mapper.setKey(3, upperRow, KeyCombo{true, false, true, 'J'}));
        m_w->setShortcuts(mapper.result());
        QCOMPARE(m_w->actions()->action(command("Text Case", "UPPERCASE"))->shortcut(),
                 QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_J));
        const QByteArray xml = readFile(m_user.path() + QStringLiteral("/shortcuts.xml"));
        QVERIFY2(xml.contains("internalID=\"0\""), xml.constData());
        QVERIFY2(xml.contains("internalID=\"2\""), xml.constData());
        QFile::remove(m_user.path() + QStringLiteral("/shortcuts.xml"));
    }

    void nppmCompatibilityMessages()
    {
        const QString a = writeFile(QStringLiteral("m1.txt"), "alpha beta\n");
        const QString b = writeFile(QStringLiteral("m2.cpp"), "int x;\n");
        QVERIFY(m_w->openFile(a));
        QVERIFY(m_w->openFile(b));
        int view = -1;
        QCOMPARE(msg(NPPM_GETCURRENTSCINTILLA, 0, reinterpret_cast<intptr_t>(&view)), 1);
        QCOMPARE(view, 0);
        QCOMPARE(msg(NPPM_GETNBOPENFILES, 0, ALL_OPEN_FILES), m_w->pane(0)->count() + m_w->pane(1)->count());
        char path[1024] = {};
        QCOMPARE(msg(NPPM_GETFULLCURRENTPATH, sizeof path, reinterpret_cast<intptr_t>(path)), 1);
        QCOMPARE(QDir::cleanPath(QString::fromUtf8(path)), QDir::cleanPath(b));
        QCOMPARE(msg(NPPM_GETFULLCURRENTPATH, 3, reinterpret_cast<intptr_t>(path)), 0); // too small
        char name[64] = {};
        QCOMPARE(msg(NPPM_GETFILENAME, sizeof name, reinterpret_cast<intptr_t>(name)), 1);
        QCOMPARE(QByteArray(name), QByteArray("m2.cpp"));
        QCOMPARE(msg(NPPM_GETEXTPART, sizeof name, reinterpret_cast<intptr_t>(name)), 1);
        QCOMPARE(QByteArray(name), QByteArray(".cpp"));

        const intptr_t idB = msg(NPPM_GETCURRENTBUFFERID);
        QVERIFY(idB);
        QCOMPARE(msg(NPPM_GETFULLPATHFROMBUFFERID, static_cast<uintptr_t>(idB), 0), static_cast<intptr_t>(b.toUtf8().size()));
        const intptr_t pos = msg(NPPM_GETPOSFROMBUFFERID, static_cast<uintptr_t>(idB), MAIN_VIEW);
        QCOMPARE(pos >> 30, 0);
        QCOMPARE(msg(NPPM_GETBUFFERIDFROMPOS, static_cast<uintptr_t>(pos & 0x3FFFFFFF), 0), idB);
        QCOMPARE(msg(NPPM_GETCURRENTDOCINDEX, 0, MAIN_VIEW), pos & 0x3FFFFFFF);

        int lang = -1;
        msg(NPPM_GETCURRENTLANGTYPE, 0, reinterpret_cast<intptr_t>(&lang));
        QCOMPARE(lang, static_cast<int>(L_CPP));
        QCOMPARE(msg(NPPM_GETBUFFERLANGTYPE, static_cast<uintptr_t>(idB)), static_cast<intptr_t>(L_CPP));
        char langName[64] = {};
        msg(NPPM_GETLANGUAGENAME, L_CPP, reinterpret_cast<intptr_t>(langName));
        QCOMPARE(QByteArray(langName), QByteArray("C++"));
        QCOMPARE(msg(NPPM_SETCURRENTLANGTYPE, 0, L_PYTHON), 1);
        QCOMPARE(m_w->currentBuffer()->lang, L_PYTHON);
        QCOMPARE(msg(NPPM_GETBUFFERENCODING, static_cast<uintptr_t>(idB)), static_cast<intptr_t>(m_w->currentBuffer()->uniMode));

        // switching documents
        QCOMPARE(msg(NPPM_SWITCHTOFILE, 0, reinterpret_cast<intptr_t>(a.toUtf8().constData())), 1);
        QCOMPARE(QDir::cleanPath(QString::fromStdString(m_w->currentBuffer()->path)), QDir::cleanPath(a));
        QCOMPARE(msg(NPPM_ACTIVATEDOC, MAIN_VIEW, pos & 0x3FFFFFFF), 1);
        QCOMPARE(msg(NPPM_GETCURRENTBUFFERID), idB);
        const QString c = writeFile(QStringLiteral("m3.txt"), "x");
        QCOMPARE(msg(NPPM_DOOPEN, 0, reinterpret_cast<intptr_t>(c.toUtf8().constData())), 1);
        QCOMPARE(QDir::cleanPath(QString::fromStdString(m_w->currentBuffer()->path)), QDir::cleanPath(c));

        // menu command, saving to a path, dirty flag
        QCOMPARE(msg(NPPM_MENUCOMMAND, 0, IDM_EDIT_SELECTALL), 1);
        QCOMPARE(editor()->send(SCI_GETSELECTIONEND), 1);
        QCOMPARE(msg(NPPM_MAKECURRENTBUFFERDIRTY), 1);
        QVERIFY(m_w->currentBuffer()->dirty);
        const QString copy = QDir(m_files.path()).filePath(QStringLiteral("copy.txt"));
        QCOMPARE(msg(NPPM_SAVECURRENTFILEAS, 1, reinterpret_cast<intptr_t>(copy.toUtf8().constData())), 1);
        QCOMPARE(readFile(copy), QByteArray("x"));

        // allocations (upstream ranges)
        int first = 0;
        QCOMPARE(msg(NPPM_ALLOCATECMDID, 3, reinterpret_cast<intptr_t>(&first)), 1);
        QCOMPARE(first, PluginHost::kFirstDynamicId);
        QCOMPARE(msg(NPPM_ALLOCATECMDID, 1, reinterpret_cast<intptr_t>(&first)), 1);
        QCOMPARE(first, PluginHost::kFirstDynamicId + 3);
        QCOMPARE(msg(NPPM_ALLOCATEMARKER, 20, reinterpret_cast<intptr_t>(&first)), 0); // more than the range
        QCOMPARE(msg(NPPM_ALLOCATEINDICATOR, 2, reinterpret_cast<intptr_t>(&first)), 1);
        QCOMPARE(first, PluginHost::kFirstIndicator);

        // folders and versions
        char dir[1024] = {};
        const intptr_t n = msg(NPPM_GETPLUGINSCONFIGDIR, sizeof dir, reinterpret_cast<intptr_t>(dir));
        QVERIFY(n > 0);
        QVERIFY(QFileInfo(QString::fromUtf8(dir)).isDir());
        QVERIFY(QString::fromUtf8(dir).startsWith(QDir::fromNativeSeparators(m_user.path())) ||
                QDir::fromNativeSeparators(QString::fromUtf8(dir)).startsWith(QDir::fromNativeSeparators(m_user.path())));
        QCOMPARE(msg(NPPM_GETNPPVERSION) >> 16, 8);
        QCOMPARE(msg(NPPM_GETCURRENTNATIVELANGENCODING), 65001);

        // user interface
        QCOMPARE(msg(NPPM_HIDETABBAR, 0, 1), 0); // was visible
        QCOMPARE(msg(NPPM_ISTABBARHIDDEN), 1);
        QCOMPARE(msg(NPPM_HIDETABBAR, 0, 0), 1);
        QCOMPARE(msg(NPPM_ISTABBARHIDDEN), 0);
        QCOMPARE(msg(NPPM_SETSTATUSBAR, 0, reinterpret_cast<intptr_t>("plugin text")), 1);

        // unsupported (Win32 objects): 0
        QCOMPARE(msg(NPPM_GETMENUHANDLE, 0, 0), 0);
        QCOMPARE(msg(NPPM_DMMREGASDCKDLG, 0, 0), 0);
    }
};

QTEST_MAIN(TestPlugins)
#include "test_plugins.moc"
