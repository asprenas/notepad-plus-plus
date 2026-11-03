// Settings menu (phase 4): Preferences, UI language switch, applying changed settings at run time,
// import of themes. Style Configurator and Shortcut Mapper are in their own files.
#include "MainWindow.h"

#include "editor/EditorPane.h"
#include "menu/ActionRegistry.h"
#include "menu/CommandIds.h"
#include "ShortcutMapper.h"
#include "StyleConfigurator.h"
#include "prefs/PreferencesDialog.h"
#include "search/FindReplaceDialog.h"
#include "search/SearchResultsPanel.h"

#include "core/editor/AutoCompletion.h"
#include "core/util/PathUtil.h"

#include <Scintilla.h>
#include <ScintillaEditBase.h>

#include <QApplication>
#include <QFileDialog>
#include <QFileInfo>
#include <QMenuBar>
#include <QMessageBox>
#include <QStatusBar>
#include <QTimer>
#include <QToolBar>

namespace npp::ui {

using namespace npp::core;
using namespace cmd;

namespace {
QString q(const std::string& s) { return QString::fromStdString(s); }
}

std::string MainWindow::localizationDir() const { return pathAppend(m_dataDir, "localization"); }

// Upstream LocalizationSwitcher: the chosen file is copied to <settings>/nativeLang.xml
// (English: the file is removed and the built-in strings are used)
void MainWindow::setUiLanguage(const std::string& fileName)
{
    std::error_code ec;
    const fs::path target = pathFromUtf8(m_paths.nativeLangXml());
    if (fileName.empty() || QString::fromStdString(fileName).compare(QLatin1String("english.xml"), Qt::CaseInsensitive) == 0) {
        fs::remove(target, ec);
        m_nativeLang.clear();
    } else {
        fs::create_directories(target.parent_path(), ec);
        fs::copy_file(pathFromUtf8(pathAppend(localizationDir(), fileName)), target, fs::copy_options::overwrite_existing, ec);
        m_nativeLang.loadFile(m_paths.nativeLangXml());
    }
    m_currentLanguageFile = fileName;
    QApplication::setLayoutDirection(m_nativeLang.isRTL() ? Qt::RightToLeft : Qt::LeftToRight);
    rebuildMenus();
    if (m_findDialog)
        m_findDialog->translate(&m_nativeLang);
    updateTitleAndStatus();
}

// Re-applies config.xml settings to the running UI (upstream applies each preference change at once)
void MainWindow::applySettings()
{
    const GuiConfig& g = m_config.gui;
    for (EditorPane* p : m_panes) {
        applyEditorSettings(p);
        if (Buffer* b = buffer(p->current()))
            applyLanguageToPane(p, b);
        p->tabBar()->setTabsClosable(g.tabCloseButton);
        p->tabBar()->setMovable(g.tabDragAndDrop);
        p->tabBar()->setVisible(!g.tabHide);
        p->tabBar()->setShape(g.tabVertical ? QTabBar::RoundedWest : QTabBar::RoundedNorth);
    }
    menuBar()->setVisible(g.menuBarShow);
    statusBar()->setVisible(g.statusBarShow);
    for (QToolBar* tb : findChildren<QToolBar*>())
        if (tb->objectName() == QLatin1String("mainToolbar"))
            tb->setVisible(g.toolbarShow);
    if (g.snapshotMode)
        m_backupTimer->start(std::max(1000, g.snapshotBackupTiming));
    else
        m_backupTimer->stop();
    m_files->settings();
    updateRecentMenu();
    for (EditorPane* p : m_panes)
        updateAutoCompletionLanguage(p);
    applyTheme();
    updateTitleAndStatus();
}

// Style / override changes: the global options of the language application, then every view
void MainWindow::applyStylesToEditors()
{
    const GuiConfig& g = m_config.gui;
    m_applyOptions.override = {g.globalOverrideFg, g.globalOverrideBg, g.globalOverrideFont, g.globalOverrideFontSize,
                               g.globalOverrideBold, g.globalOverrideItalic, g.globalOverrideUnderline};
    for (const auto& ls : m_styles.lexers())
        if (!ls.ext.empty())
            if (const auto t = langTypeFromName(ls.name))
                m_langs.addUserExtensions(*t, ls.ext);
    for (EditorPane* p : m_panes) {
        applyIndicatorStyles(p->editor());
        applyRepresentations(p);
        if (Buffer* b = buffer(p->current()))
            applyLanguageToPane(p, b);
    }
    if (m_searchResults)
        m_searchResults->applyStyles(m_langs, m_styles, m_applyOptions);
}

// New shortcuts (Shortcut Mapper): saved to shortcuts.xml and applied to menus, macros and editors
void MainWindow::setShortcuts(const ShortcutsFile& f)
{
    m_shortcuts = f;
    std::error_code ec;
    fs::create_directories(pathFromUtf8(m_paths.userDir()), ec);
    m_shortcuts.save(m_paths.shortcutsXml());
    m_actions->applyShortcuts(&m_shortcuts);
    rebuildMacroMenu();
    for (EditorPane* p : m_panes)
        applyScintillaKeys(p->editor());
}

PreferencesDialog* MainWindow::preferencesDialog()
{
    if (!m_prefs) {
        PreferencesDialog::Context ctx;
        ctx.config = &m_config;
        ctx.nativeLang = &m_nativeLang;
        ctx.availableLanguages = [this] {
            std::vector<std::pair<std::string, std::string>> out;
            for (const auto& l : NativeLanguage::availableLanguages(localizationDir()))
                out.emplace_back(l.name, l.fileName);
            return out;
        };
        ctx.currentLanguageFile = [this] { return m_currentLanguageFile; };
        ctx.setLanguageFile = [this](const std::string& f) { setUiLanguage(f); };
        ctx.languageNames = [] {
            std::vector<std::string> out;
            for (int i = 0; i < kLangCount; ++i)
                out.emplace_back(langInfo(static_cast<LangType>(i)).shortName);
            return out;
        };
        ctx.openStyleConfigurator = [this] { m_actions->trigger(IDM_LANGSTYLE_CONFIG_DLG); };
        m_prefs = new PreferencesDialog(ctx, this);
        connect(m_prefs, &PreferencesDialog::settingsChanged, this, &MainWindow::applySettings);
    }
    return m_prefs;
}

void MainWindow::registerSettingsCommands()
{
    ActionRegistry* r = m_actions;
    r->setHandler(IDM_SETTING_PREFERENCE, {[this] {
        PreferencesDialog* d = preferencesDialog();
        d->reload();
        d->show();
        d->raise();
    }});
    r->setHandler(IDM_EDIT_CHANGESEARCHENGINE, {[this] {
        PreferencesDialog* d = preferencesDialog();
        d->reload();
        d->showPage(QStringLiteral("SearchEngine"));
    }});
    r->setHandler(IDM_LANGSTYLE_CONFIG_DLG, {[this] {
        StyleConfigurator::Context ctx;
        ctx.styles = &m_styles;
        ctx.currentThemePath = &m_currentThemePath;
        ctx.gui = &m_config.gui;
        ctx.langs = &m_langs;
        ctx.nativeLang = &m_nativeLang;
        ctx.userThemesDir = pathAppend(m_paths.userDir(), "themes");
        ctx.dataThemesDir = pathAppend(m_dataDir, "themes");
        ctx.userStylersXml = m_paths.stylersXml();
        ctx.modelStylersXml = pathAppend(m_dataDir, "stylers.model.xml");
        ctx.apply = [this] { applyStylesToEditors(); };
        auto* dlg = new StyleConfigurator(ctx, this);
        dlg->setAttribute(Qt::WA_DeleteOnClose);
        connect(dlg, &QDialog::accepted, this, [this] {
            // upstream: the theme of the current mode is DarkMode darkThemeName / lightThemeName
            // (file name in themes/, "" = stylers.xml)
            const QFileInfo fi(QString::fromStdString(m_currentThemePath));
            const bool isStylers = fi == QFileInfo(QString::fromStdString(m_paths.stylersXml()))
                                   || fi == QFileInfo(QString::fromStdString(pathAppend(m_dataDir, "stylers.model.xml")));
            const std::string name = isStylers ? std::string() : fi.fileName().toStdString();
            (isDarkMode() ? m_config.gui.darkThemeName : m_config.gui.lightThemeName) = name;
        });
        m_styleConfigurator = dlg;
        dlg->show();
    }});
    r->setHandler(IDM_SETTING_SHORTCUT_MAPPER, {[this] {
        ShortcutMapper dlg(m_shortcuts, m_actions, &m_nativeLang, this);
        if (dlg.exec() == QDialog::Accepted)
            setShortcuts(dlg.result());
    }});
    r->setHandler(IDM_SETTING_SHORTCUT_MAPPER_MACRO, {[this] { m_actions->trigger(IDM_SETTING_SHORTCUT_MAPPER); }});
    r->setHandler(IDM_SETTING_SHORTCUT_MAPPER_RUN, {[this] { m_actions->trigger(IDM_SETTING_SHORTCUT_MAPPER); }});
    r->setHandler(IDM_SETTING_IMPORTSTYLETHEMES, {[this] {
        const QStringList files = QFileDialog::getOpenFileNames(this, tr("Import style theme(s)"), {}, tr("Style themes (*.xml)"));
        if (files.isEmpty())
            return;
        std::error_code ec;
        const fs::path dir = pathFromUtf8(pathAppend(m_paths.userDir(), "themes"));
        fs::create_directories(dir, ec);
        for (const QString& f : files)
            fs::copy_file(pathFromUtf8(f.toStdString()), dir / pathFromUtf8(QFileInfo(f).fileName().toStdString()),
                          fs::copy_options::overwrite_existing, ec);
        QMessageBox::information(this, QStringLiteral("Notepad++"), tr("%n theme(s) imported.", nullptr, static_cast<int>(files.size())));
    }});
}

} // namespace npp::ui
