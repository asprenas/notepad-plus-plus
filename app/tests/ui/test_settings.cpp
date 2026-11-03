// Phase 4 UI tests: Preferences pages (bindings, save / reload), UI language switch with dialog
// translations, light / dark mode, Style Configurator, Shortcut Mapper.
#include "MainWindow.h"
#include "editor/EditorPane.h"
#include "menu/ActionRegistry.h"
#include "menu/CommandIds.h"
#include "prefs/PreferencesDialog.h"
#include "ShortcutMapper.h"
#include "StyleConfigurator.h"

#include "core/config/ConfigFile.h"

#include <Scintilla.h>
#include <ScintillaEditBase.h>

#include <QApplication>
#include <QPalette>
#include <QStyleHints>
#include <QFile>
#include <QFileInfo>
#include <QMenu>
#include <QCheckBox>
#include <QDir>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QMenuBar>
#include <QRadioButton>
#include <QTemporaryDir>
#include <QTest>

using namespace npp::core;
using namespace npp::ui;
using namespace npp::ui::cmd;

class TestSettings : public QObject {
    Q_OBJECT
    QTemporaryDir m_user;
    std::unique_ptr<MainWindow> m_w;

    void makeWindow()
    {
        m_w.reset();
        CommandLine c;
        c.noSession = true;
        m_w = std::make_unique<MainWindow>(c);
    }

private slots:
    void initTestCase()
    {
        qputenv("NPP_QT_USER_DIR", m_user.path().toUtf8());
        qputenv("NPP_DATA_DIR", (QCoreApplication::applicationDirPath() + QStringLiteral("/data")).toUtf8());
    }
    void init() { makeWindow(); }
    void cleanup() { m_w.reset(); }

    void allPagesExist()
    {
        PreferencesDialog* d = m_w->preferencesDialog();
#if defined(Q_OS_WIN)
        QCOMPARE(d->pageTags().size(), 24);
#else
        QCOMPARE(d->pageTags().size(), 23); // File Association is Windows only
#endif
        QVERIFY(d->control(QStringLiteral("Global"), 6124));
        QVERIFY(d->control(QStringLiteral("MISC"), 6331));
    }

    void preferenceCheckboxesRoundTrip()
    {
        // every bound check box changes config.xml, and unchecking restores it
        PreferencesDialog* d = m_w->preferencesDialog();
        int bound = 0;
        for (const QString& tag : d->pageTags()) {
            if (tag == QStringLiteral("FileAssoc"))
                continue;
            for (QCheckBox* cb : d->findChild<QWidget*>(tag)->findChildren<QCheckBox*>()) {
                if (!cb->isEnabled())
                    continue;
                const std::string before = m_w->mutableConfig().saveToString();
                cb->click();
                const std::string after = m_w->mutableConfig().saveToString();
                cb->click();
                QCOMPARE(m_w->mutableConfig().saveToString(), before);
                if (after != before)
                    ++bound;
            }
        }
        QVERIFY2(bound >= 100, qPrintable(QString::number(bound)));
    }

    void nonPrintingCharacters()
    {
        ScintillaEditBase* e = m_w->currentPane()->editor();
        auto repr = [e](const char* c) {
            char buf[32] = {};
            e->send(SCI_GETREPRESENTATION, reinterpret_cast<uptr_t>(c), reinterpret_cast<sptr_t>(buf));
            return QByteArray(buf);
        };
        QCOMPARE(repr("\xC2\xA0"), QByteArray());
        QVERIFY(m_w->actions()->trigger(IDM_VIEW_NPC));
        QCOMPARE(repr("\xC2\xA0"), QByteArray("NBSP"));
        QCOMPARE(repr("\xEF\xBB\xBF"), QByteArray("ZWNBSP"));
        m_w->mutableConfig().gui.npcMode = 2;
        m_w->applySettings();
        QCOMPARE(repr("\xC2\xA0"), QByteArray("U+00A0"));
        QCOMPARE(repr("\x01"), QByteArray("SOH")); // C0 follows npcMode only with npcIncludeCcUniEol
        m_w->mutableConfig().gui.npcIncludeCcUniEol = true;
        m_w->applySettings();
        QCOMPARE(repr("\x01"), QByteArray("U+0001"));
        QVERIFY(m_w->actions()->trigger(IDM_VIEW_NPC));
        QCOMPARE(repr("\xC2\xA0"), QByteArray());
        QVERIFY(m_w->actions()->action(IDM_VIEW_NPC_CCUNIEOL)->isChecked());
        QVERIFY(m_w->actions()->trigger(IDM_VIEW_NPC_CCUNIEOL));
        QCOMPARE(repr("\xE2\x80\xA8"), QByteArray("\xE2\x80\x8B")); // hidden: zero-width space
        QVERIFY(m_w->actions()->trigger(IDM_VIEW_NPC_CCUNIEOL));
        QCOMPARE(repr("\xE2\x80\xA8"), QByteArray("U+2028"));
    }

    void lightDarkMode()
    {
        GuiConfig& g = m_w->mutableConfig().gui;
        g.darkFollowSystem = false;
        g.darkModeEnabled = true;
        m_w->applyTheme();
        QVERIFY(m_w->isDarkMode());
        QVERIFY(qApp->palette().color(QPalette::Window).lightness() < 80);
        QVERIFY(QString::fromStdString(m_w->currentThemePath()).endsWith(QStringLiteral("DarkModeDefault.xml")));
        const Style* def = m_w->styles().globalStyle(STYLE_DEFAULT);
        QVERIFY(def && def->bg >= 0 && (def->bg & 0xFF) < 0x80);
        g.darkModeEnabled = false;
        m_w->applyTheme();
        QVERIFY(!m_w->isDarkMode());
        QVERIFY(qApp->palette().color(QPalette::Window).lightness() > 160);
        QVERIFY(!QString::fromStdString(m_w->currentThemePath()).contains(QStringLiteral("DarkModeDefault")));
        g.darkFollowSystem = true; // follows the platform colour scheme
        QCOMPARE(m_w->isDarkMode(), QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark);
        g.darkFollowSystem = false;
    }

    void uiLanguageSwitch()
    {
        m_w->setUiLanguage("korean.xml");
        QCOMPARE(m_w->menuBar()->actions().first()->text(), QStringLiteral("파일(&F)"));
        QVERIFY(QFile::exists(QString::fromStdString(m_w->paths().nativeLangXml())));
        PreferencesDialog* d = m_w->preferencesDialog();
        d->translate();
        auto* cb = qobject_cast<QCheckBox*>(d->control(QStringLiteral("Global"), 6122));
        QVERIFY(cb);
        QVERIFY(!cb->text().contains(QStringLiteral("Hide")));
        m_w->setUiLanguage("english.xml");
        QCOMPARE(m_w->menuBar()->actions().first()->text(), QStringLiteral("&File"));
        QVERIFY(!QFile::exists(QString::fromStdString(m_w->paths().nativeLangXml())));
    }

    void styleConfiguratorSaves()
    {
        GuiConfig& g = m_w->mutableConfig().gui;
        g.darkModeEnabled = false;
        m_w->applyTheme();
        QVERIFY(m_w->actions()->trigger(IDM_LANGSTYLE_CONFIG_DLG));
        auto* dlg = qobject_cast<StyleConfigurator*>(m_w->styleConfigurator());
        QVERIFY(dlg);
        dlg->selectLanguage(QStringLiteral("cpp"));
        dlg->selectStyle(QStringLiteral("COMMENT"));
        dlg->setForeground(0x123456);
        dlg->setBold(true);
        QVERIFY(dlg->saveAndClose());
        const LexerStyler* cpp = m_w->styles().lexer("cpp");
        QVERIFY(cpp);
        const Style* st = cpp->findById(1);
        QVERIFY(st);
        QCOMPARE(st->fg, 0x123456);
        QVERIFY(st->fontStyle & 1);
        StyleRegistry reread;
        QVERIFY(reread.load(m_w->currentThemePath()));
        QCOMPARE(reread.lexer("cpp")->findById(1)->fg, 0x123456);

        // Cancel restores the previous styles
        QVERIFY(m_w->actions()->trigger(IDM_LANGSTYLE_CONFIG_DLG));
        dlg = qobject_cast<StyleConfigurator*>(m_w->styleConfigurator());
        dlg->selectLanguage(QStringLiteral("cpp"));
        dlg->selectStyle(QStringLiteral("COMMENT"));
        dlg->setForeground(0x654321);
        QCOMPARE(m_w->styles().lexer("cpp")->findById(1)->fg, 0x654321); // live preview
        dlg->cancel();
        QCOMPARE(m_w->styles().lexer("cpp")->findById(1)->fg, 0x123456);
    }

    void shortcutMapperConflictsAndResult()
    {
        ShortcutMapper sm(m_w->shortcuts(), m_w->actions(), &m_w->nativeLanguage());
        QVERIFY(sm.rows().size() > 300);
        QVERIFY(sm.conflicts().isEmpty());
        KeyCombo ctrlN;
        ctrlN.ctrl = true;
        ctrlN.key = 'N';
        QVERIFY(sm.setKey(0, IDM_FILE_CLOSE, ctrlN)); // Ctrl+N already = New
        QVERIFY(!sm.conflicts().isEmpty());
        KeyCombo f12;
        f12.key = 0x7B; // VK_F12
        f12.ctrl = f12.alt = f12.shift = true;
        QVERIFY(sm.setKey(0, IDM_FILE_CLOSE, f12));
        QVERIFY(sm.conflicts().isEmpty());
        const ShortcutsFile out = sm.result();
        bool found = false;
        for (const auto& c : out.internalCommands)
            if (c.id == IDM_FILE_CLOSE) {
                found = true;
                QCOMPARE(c.combo.key, 0x7B);
            }
        QVERIFY(found);
        QCOMPARE(out.internalCommands.size(), size_t(1)); // only the changed default is written
    }

    void tabColoursAndPinnedTabs()
    {
        QTemporaryDir dir;
        QStringList files;
        for (int i = 0; i < 4; ++i) {
            files << dir.filePath(QStringLiteral("f%1.txt").arg(i));
            QFile f(files.last());
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("x");
        }
        for (const QString& f : files)
            QVERIFY(m_w->openFile(f));
        EditorPane* p = m_w->currentPane();
        auto nameAt = [&](int i) { return QFileInfo(QString::fromStdString(m_w->files().find(p->bufferAt(i))->path)).fileName(); };
        const int n = p->count(); // may include the initial "new 1"
        // colour
        p->activate(p->bufferAt(n - 2)); // f2
        QVERIFY(m_w->actions()->trigger(IDM_VIEW_TAB_COLOUR_3));
        QCOMPARE(m_w->currentBuffer()->docColorId, 2);
        QVERIFY(p->tabBar()->tabData(n - 2).value<QColor>().isValid());
        // pin f2 then f3: they move to the front, in pin order
        QVERIFY(m_w->actions()->trigger(IDM_PINTAB));
        QCOMPARE(nameAt(0), QStringLiteral("f2.txt"));
        p->activate(p->bufferAt(n - 1)); // f3
        QVERIFY(m_w->actions()->trigger(IDM_PINTAB));
        QCOMPARE(nameAt(1), QStringLiteral("f3.txt"));
        QVERIFY(!p->tabBar()->tabIcon(1).isNull());
        // tab context menu: unpin text, colour icons
        QMenu* m = m_w->tabContextMenu();
        bool unpin = false, colourIcon = false;
        for (QAction* a : m->findChildren<QAction*>()) {
            if (a->objectName().toInt() == IDM_PINTAB)
                unpin = a->text() == QStringLiteral("Unpin Tab");
            if (a->objectName().toInt() == IDM_VIEW_TAB_COLOUR_1)
                colourIcon = !a->icon().isNull();
        }
        delete m;
        QVERIFY(unpin);
        QVERIFY(colourIcon);
        // session keeps colour and pin
        const QString session = dir.filePath(QStringLiteral("s.xml"));
        QVERIFY(m_w->saveSessionTo(session));
        QFile sf(session);
        QVERIFY(sf.open(QIODevice::ReadOnly));
        const QByteArray xml = sf.readAll();
        QVERIFY(xml.contains("tabColourId=\"2\""));
        QCOMPARE(xml.count("tabPinned=\"yes\""), 2);
        // Close All but Pinned
        QVERIFY(m_w->actions()->trigger(IDM_FILE_CLOSEALL_BUT_PINNED));
        QCOMPARE(p->count(), 2);
        QVERIFY(m_w->files().find(p->bufferAt(0))->pinned);
        // unpin f2 (index 0): goes after the remaining pinned tab
        p->activate(p->bufferAt(0));
        QVERIFY(m_w->actions()->trigger(IDM_PINTAB));
        QCOMPARE(nameAt(1), QStringLiteral("f2.txt"));
        QVERIFY(p->tabBar()->tabIcon(1).isNull());
    }

    void editorContextMenuFromXml()
    {
        QVERIFY(QFile::exists(QString::fromStdString(m_w->paths().contextMenuXml()))); // copied from the model
        QMenu* m = m_w->editorContextMenu();
        QList<QAction*> top = m->actions();
        QVERIFY(top.contains(m_w->actions()->action(IDM_EDIT_CUT)));
        QVERIFY(top.contains(m_w->actions()->action(IDM_EDIT_SELECTALL)));       // "Select all" (case-insensitive)
        QVERIFY(top.contains(m_w->actions()->action(IDM_EDIT_BLOCK_COMMENT)));
        QStringList subs;
        for (QAction* a : top)
            if (a->menu())
                subs << a->text();
        QVERIFY(subs.contains(QStringLiteral("Style all occurrences of token")));
        QVERIFY(!subs.contains(QStringLiteral("Plugin commands"))); // no plugins yet: empty folder not created
        delete m;
        // translated folder names (TranslateID)
        m_w->setUiLanguage("korean.xml");
        m = m_w->editorContextMenu();
        subs.clear();
        for (QAction* a : m->actions())
            if (a->menu())
                subs << a->text();
        QVERIFY2(subs.contains(QStringLiteral("모든 토큰에 형식 지정")), qPrintable(subs.join('|')));
        delete m;
        m_w->setUiLanguage("english.xml");
    }

    void screenshots()
    {
        const QString out = qEnvironmentVariable("NPP_SCREENSHOT_DIR");
        if (out.isEmpty())
            QSKIP("set NPP_SCREENSHOT_DIR to write the page screenshots");
        m_w->setUiLanguage("korean.xml");
        PreferencesDialog* d = m_w->preferencesDialog();
        d->translate();
        for (const QString& tag : d->pageTags()) {
            d->showPage(tag);
            QCoreApplication::processEvents();
            d->grab().save(QDir(out).filePath(QStringLiteral("pref-%1.png").arg(tag)));
        }
    }
};

QTEST_MAIN(TestSettings)
#include "test_settings.moc"
