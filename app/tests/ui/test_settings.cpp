// Phase 4 UI tests: Preferences pages (bindings, save / reload), UI language switch with dialog
// translations, light / dark mode, Style Configurator, Shortcut Mapper.
#include "MainWindow.h"
#include "editor/EditorPane.h"
#include "menu/ActionRegistry.h"
#include "menu/CommandIds.h"
#include "prefs/PreferencesDialog.h"
#include "search/FindReplaceDialog.h"
#include "search/SearchResultsPanel.h"
#include "ShortcutMapper.h"
#include "StyleConfigurator.h"

#include "core/config/ConfigFile.h"

#include <Scintilla.h>
#include <ScintillaEditBase.h>

#include <QApplication>
#include <functional>
#include <QAbstractButton>
#include <QTabBar>
#include <QRegularExpression>
#include <QTimer>
#include <QPalette>
#include <QStyleHints>
#include <QFile>
#include <QFileInfo>
#include <QMenu>
#include <QImage>
#include <QToolBar>
#include <QCheckBox>
#include <QComboBox>
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


    // Texts of a dialog that are not translated: controls without an upstream id (not translatable
    // from nativeLang) or with an id whose <Dialog><tag> text was not applied.
    QStringList untranslated(QWidget* dlg, const char* tag, const QStringList& allowed = {})
    {
        QStringList out;
        const auto d = m_w->nativeLanguage().dialog(tag);
        if (!d)
            return {QStringLiteral("no <%1> block").arg(QLatin1String(tag))};
        for (QWidget* w : dlg->findChildren<QWidget*>()) {
            QString text;
            if (auto* b = qobject_cast<QAbstractButton*>(w))
                text = b->text();
            else if (auto* g = qobject_cast<QGroupBox*>(w))
                text = g->title();
            else if (auto* l = qobject_cast<QLabel*>(w))
                text = l->text();
            else
                continue;
            if (text.isEmpty() || !text.contains(QRegularExpression(QStringLiteral("[A-Za-z]"))) || allowed.contains(text))
                continue;
            if (w->objectName() == QLatin1String("static") || w->objectName() == QLatin1String("npp-data"))
                continue;
            bool ok = false;
            const int id = w->objectName().toInt(&ok);
            if (!ok) {
                out << QStringLiteral("no id: ") + text;
                continue;
            }
            if (d->items.count(id)) {
                QString want = QString::fromStdString(d->items.at(id));
                QString plain = want;
                plain.remove(QLatin1Char('&'));
                if (text != want && text != plain)
                    out << QStringLiteral("%1: %2").arg(id).arg(text);
            }
        }
        return out;
    }

    // Runs `open` and inspects the modal dialog it shows.
    QStringList checkModal(const std::function<void()>& open, const char* tag, const QStringList& allowed = {})
    {
        QStringList issues;
        bool seen = false;
        QTimer::singleShot(0, this, [&] {
            if (QWidget* dlg = QApplication::activeModalWidget()) {
                seen = true;
                issues = untranslated(dlg, tag, allowed);
                if (auto* d = qobject_cast<QDialog*>(dlg))
                    d->reject();
            }
        });
        open();
        if (!seen)
            issues << QStringLiteral("dialog <%1> not shown").arg(QLatin1String(tag));
        return issues;
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

    void toolbarIconSetsAndConf()
    {
        QToolBar* tb = m_w->mainToolbar();
        auto buttons = [tb] {
            QList<QAction*> out;
            for (QAction* a : tb->actions())
                if (!a->isSeparator())
                    out << a;
            return out;
        };
        QCOMPARE(buttons().size(), 32); // upstream toolBarIcons: 41 entries, 9 separators
        for (QAction* a : buttons())
            QVERIFY2(!a->icon().pixmap(16).isNull(), qPrintable(a->text()));
        GuiConfig& g = m_w->mutableConfig().gui;
        for (const char* set : {"small", "large", "small2", "large2", "standard"}) {
            g.toolbarIconSet = set;
            g.toolbarFluentColor = 1; // red
            m_w->applySettings();
            QCOMPARE(m_w->mainToolbar()->iconSize().width(), std::string_view(set).starts_with("large") ? 32 : 16);
            for (QAction* a : buttons())
                QVERIFY2(!a->icon().pixmap(16).isNull(), set);
            const QString out = qEnvironmentVariable("NPP_SCREENSHOT_DIR");
            if (!out.isEmpty())
                m_w->mainToolbar()->grab().save(QDir(out).filePath(QStringLiteral("toolbar-%1.png").arg(QLatin1String(set))));
        }
        g.toolbarFluentColor = 0;
        // dark / light keep their own icon set
        g.toolbarIconSet = "standard";
        g.darkModeEnabled = false;
        m_w->applyTheme();
        g.darkToolBarIconSet = 1;
        g.darkModeEnabled = true;
        m_w->applyTheme();
        QCOMPARE(QString::fromStdString(g.toolbarIconSet), QStringLiteral("large"));
        g.darkModeEnabled = false;
        m_w->applyTheme();
        QCOMPARE(QString::fromStdString(g.toolbarIconSet), QStringLiteral("standard"));

        // toolbarButtonsConf.xml: hidden buttons and custom icons
        const QString userDir = QString::fromStdString(m_w->paths().userDir());
        QDir(userDir).mkpath(QStringLiteral("toolbarIcons/mine"));
        QImage red(16, 16, QImage::Format_ARGB32);
        red.fill(Qt::red);
        QVERIFY(red.save(userDir + QStringLiteral("/toolbarIcons/mine/new.ico"), "ICO"));
        QFile conf(userDir + QStringLiteral("/toolbarButtonsConf.xml"));
        QVERIFY(conf.open(QIODevice::WriteOnly));
        conf.write(R"(<?xml version="1.0" encoding="UTF-8" ?>
<NotepadPlus><ToolbarButtons><Standard hideAll="no">
<Button hide="yes" index="6" id="41010" name="Print..." />
<Button hide="yes" index="8" id="99999" name="wrong id: ignored" />
</Standard></ToolbarButtons><ToolBarIcons icoFolderName="mine" /></NotepadPlus>)");
        conf.close();
        m_w->applySettings();
        QCOMPARE(buttons().size(), 31);
        QVERIFY(!buttons().contains(m_w->actions()->action(IDM_FILE_PRINT)));
        const QImage newIcon = m_w->actions()->action(IDM_FILE_NEW)->icon().pixmap(16).toImage();
        QCOMPARE(QColor(newIcon.pixel(8, 8)), QColor(Qt::red));
        QFile::remove(conf.fileName());
        m_w->applySettings();
        QCOMPARE(buttons().size(), 32);
    }

    void koreanDialogs()
    {
        m_w->setUiLanguage("korean.xml");
        QStringList issues;
        auto add = [&issues](const char* what, const QStringList& l) {
            for (const QString& x : l)
                issues << QStringLiteral("%1 | %2").arg(QLatin1String(what), x);
        };
        // Find / Replace / Find in Files / Mark
        FindReplaceDialog* f = m_w->findDialog();
        add("Find", untranslated(f, "Find", {QStringLiteral("..."), QStringLiteral("Follow current doc.")}));
        QCOMPARE(f->findChild<QTabBar*>()->tabText(1), QStringLiteral("바꾸기"));
        add("GoToLine", checkModal([&] { m_w->actions()->trigger(IDM_SEARCH_GOTOLINE); }, "GoToLine"));
        add("ColumnEditor", checkModal([&] { m_w->actions()->trigger(IDM_EDIT_COLUMNMODE); }, "ColumnEditor", {QStringLiteral("Uppercase hex")}));
        m_w->actions()->trigger(IDM_SEARCH_FINDCHARINRANGE);
        for (QDialog* w : m_w->findChildren<QDialog*>())
            if (w->findChild<QWidget*>(QStringLiteral("2901")))
                add("FindCharsInRange", untranslated(w, "FindCharsInRange"));
        m_w->actions()->trigger(IDM_SEARCH_FINDINCREMENT);
        add("IncrementalFind", untranslated(m_w->findChild<QToolBar*>(QStringLiteral("incrementalSearch")), "IncrementalFind"));
        add("FindInFinder", checkModal([&] { emit m_w->searchResults(false)->findInResultsRequested(); }, "FindInFinder"));
        add("ShortcutMapper", checkModal([&] { m_w->actions()->trigger(IDM_SETTING_SHORTCUT_MAPPER); }, "ShortcutMapper", {QStringLiteral("Reset")}));
        // a recorded macro enables the macro dialogs
        ScintillaEditBase* e = m_w->currentPane()->editor();
        m_w->actions()->trigger(IDM_MACRO_STARTRECORDINGMACRO);
        QTest::keyClicks(e, QStringLiteral("ab"));
        m_w->actions()->trigger(IDM_MACRO_STOPRECORDINGMACRO);
        QVERIFY(!m_w->recordedMacro().empty());
        add("ShortcutMapperSubDialg", checkModal([&] { m_w->actions()->trigger(IDM_MACRO_SAVECURRENTMACRO); }, "ShortcutMapperSubDialg", {QStringLiteral("Shortcut :")}));
        add("MultiMacro", checkModal([&] { m_w->actions()->trigger(IDM_MACRO_RUNMULTIMACRODLG); }, "MultiMacro"));
        // Preferences / Style Configurator
        PreferencesDialog* prefs = m_w->preferencesDialog();
        for (const QString& tag : prefs->pageTags())
            add(qPrintable(tag), untranslated(prefs->findChild<QWidget*>(tag), qPrintable(tag)));
        QVERIFY(m_w->actions()->trigger(IDM_LANGSTYLE_CONFIG_DLG));
        add("StyleConfig", untranslated(m_w->styleConfigurator(), "StyleConfig"));
        m_w->styleConfigurator()->reject();
        QVERIFY2(issues.isEmpty(), qPrintable(issues.join(QLatin1Char('\n'))));

        // back to English: texts restored from english.xml
        m_w->setUiLanguage("english.xml");
        QCOMPARE(f->findChild<QTabBar*>()->tabText(1), QStringLiteral("Replace"));
        QCOMPARE(f->findChild<QAbstractButton*>(QStringLiteral("1609"))->text(), QStringLiteral("Replace &All"));
    }

    void koreanMenus()
    {
        // every main menu command and sub menu that korean.xml translates shows the translation
        m_w->setUiLanguage("korean.xml");
        const NativeLanguage& nl = m_w->nativeLanguage();
        QStringList issues;
        QSet<QString> titles;
        int commands = 0;
        std::function<void(QMenu*)> walk = [&](QMenu* m) {
            titles.insert(m->title());
            for (QAction* a : m->actions()) {
                if (a->menu()) {
                    walk(a->menu());
                    continue;
                }
                bool ok = false;
                const int id = a->data().toInt(&ok);
                if (a->isSeparator() || !ok)
                    continue;
                const QString want = QString::fromStdString(nl.menuCommand(id));
                if (want.isEmpty())
                    continue;
                ++commands;
                if (a->text() != want)
                    issues << QStringLiteral("command %1: \"%2\" instead of \"%3\"").arg(id).arg(a->text(), want);
            }
        };
        for (QAction* a : m_w->menuBar()->actions())
            if (a->menu())
                walk(a->menu());
        QVERIFY2(commands > 300, qPrintable(QString::number(commands)));

        QFile f(QCoreApplication::applicationDirPath() + QStringLiteral("/data/localization/korean.xml"));
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QString xml = QString::fromUtf8(f.readAll());
        int subMenus = 0;
        for (const auto& match : QRegularExpression(QStringLiteral("subMenuId=\"([^\"]+)\"")).globalMatch(xml)) {
            const std::string id = match.captured(1).toStdString();
            ++subMenus;
            const QString want = QString::fromStdString(nl.subMenuEntry(id));
            if (!titles.contains(want))
                issues << QStringLiteral("sub menu %1: \"%2\" not found").arg(match.captured(1), want);
        }
        QVERIFY2(subMenus > 40, qPrintable(QString::number(subMenus)));
        QVERIFY2(issues.isEmpty(), qPrintable(issues.join(QLatin1Char('\n'))));
    }

    void rtlLanguage()
    {
        // Arabic: right-to-left application layout (upstream RTL="yes"), back to left-to-right in English
        m_w->setUiLanguage("arabic.xml");
        QCOMPARE(QApplication::layoutDirection(), Qt::RightToLeft);
        QCOMPARE(m_w->preferencesDialog()->layoutDirection(), Qt::RightToLeft);
        auto* combo = qobject_cast<QComboBox*>(m_w->preferencesDialog()->control(QStringLiteral("Global"), 6124));
        QVERIFY(combo);
        QCOMPARE(combo->currentData().toString(), QStringLiteral("arabic.xml"));
        m_w->setUiLanguage("english.xml");
        QCOMPARE(QApplication::layoutDirection(), Qt::LeftToRight);
    }

    void importPlugin()
    {
        QTemporaryDir dir;
        const QString dll = dir.filePath(QStringLiteral("MyPlugin.dll"));
        QFile f(dll);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("MZ");
        f.close();
        QCOMPARE(m_w->importPlugins({dll}), 1);
        QVERIFY(QFile::exists(QString::fromStdString(m_w->pluginsDir()) + QStringLiteral("/MyPlugin/MyPlugin.dll")));
        QVERIFY(m_w->actions()->hasHandler(IDM_SETTING_IMPORTPLUGIN));
        QVERIFY(m_w->actions()->hasHandler(IDM_SETTING_OPENPLUGINSDIR));
    }

    void allThemesApply()
    {
        // every installer theme previews in the editor (Style Configurator theme switch), no crash;
        // the editor default background is the theme's one; Cancel restores the original theme
        const QDir themes(QCoreApplication::applicationDirPath() + QStringLiteral("/data/themes"));
        const QStringList files = themes.entryList({QStringLiteral("*.xml")}, QDir::Files);
        QCOMPARE(files.size(), 22);
        QVERIFY(m_w->actions()->trigger(IDM_LANGSTYLE_CONFIG_DLG));
        auto* dlg = qobject_cast<StyleConfigurator*>(m_w->styleConfigurator());
        QVERIFY(dlg);
        const QString original = dlg->currentThemeFile();
        ScintillaEditBase* e = m_w->currentPane()->editor();
        const QString out = qEnvironmentVariable("NPP_SCREENSHOT_DIR");
        for (const QString& f : files) {
            const QString name = QFileInfo(f).completeBaseName();
            dlg->setTheme(name);
            QVERIFY2(QFileInfo(dlg->currentThemeFile()).fileName() == f, qPrintable(f));
            const Style* def = m_w->styles().globalStyle(STYLE_DEFAULT);
            QVERIFY2(def && def->bg >= 0, qPrintable(f));
            QCOMPARE(int(e->send(SCI_STYLEGETBACK, STYLE_DEFAULT)), Style::scintillaColour(def->bg));
            if (!out.isEmpty())
                m_w->grab().save(QDir(out).filePath(QStringLiteral("theme-%1.png").arg(name)));
        }
        dlg->cancel();
        QCOMPARE(QFileInfo(m_w->currentThemePath().c_str()).fileName(), QFileInfo(original).fileName());
    }

    void systemModeFollowsColorScheme()
    {
        // "follow system": a platform colour scheme change switches palette and editor theme at once
        GuiConfig& g = m_w->mutableConfig().gui;
        g.darkModeEnabled = false;
        g.darkFollowSystem = true;
        QStyleHints* hints = QGuiApplication::styleHints();
        hints->setColorScheme(Qt::ColorScheme::Dark);
        QCoreApplication::processEvents();
        if (hints->colorScheme() != Qt::ColorScheme::Dark)
            QSKIP("this platform plugin cannot change the colour scheme");
        QVERIFY(m_w->isDarkMode());
        QVERIFY(qApp->palette().color(QPalette::Window).lightness() < 80);
        QVERIFY(QString::fromStdString(m_w->currentThemePath()).endsWith(QStringLiteral("DarkModeDefault.xml")));
        hints->setColorScheme(Qt::ColorScheme::Light);
        QCoreApplication::processEvents();
        QVERIFY(!m_w->isDarkMode());
        QVERIFY(qApp->palette().color(QPalette::Window).lightness() > 160);
        QVERIFY(!QString::fromStdString(m_w->currentThemePath()).contains(QStringLiteral("DarkModeDefault")));
        hints->unsetColorScheme();
        g.darkFollowSystem = false;
    }

    void screenshots()
    {
        const QString out = qEnvironmentVariable("NPP_SCREENSHOT_DIR");
        if (out.isEmpty())
            QSKIP("set NPP_SCREENSHOT_DIR to write the page screenshots");
        // Korean, Japanese, German (long strings), Arabic (RTL): checked for clipped texts by eye
        for (const char* lang : {"korean", "japanese", "german", "arabic"}) {
            m_w->setUiLanguage(std::string(lang) + ".xml");
            PreferencesDialog* d = m_w->preferencesDialog();
            d->translate();
            for (const QString& tag : d->pageTags()) {
                d->showPage(tag);
                QCoreApplication::processEvents();
                d->grab().save(QDir(out).filePath(QStringLiteral("%1-pref-%2.png").arg(QLatin1String(lang), tag)));
            }
            QVERIFY(m_w->actions()->trigger(IDM_LANGSTYLE_CONFIG_DLG));
            m_w->styleConfigurator()->grab().save(QDir(out).filePath(QStringLiteral("%1-styleconfig.png").arg(QLatin1String(lang))));
            m_w->styleConfigurator()->reject();
            m_w->findDialog()->grab().save(QDir(out).filePath(QStringLiteral("%1-find.png").arg(QLatin1String(lang))));
        }
        m_w->setUiLanguage("english.xml");
    }
};

QTEST_MAIN(TestSettings)
#include "test_settings.moc"
