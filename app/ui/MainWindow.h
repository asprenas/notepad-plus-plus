// Main window of the Qt port (phase 2 MVP): two editor panes (split view), commands of the
// upstream menu, file I/O through npp-core, session / backups / change detection.
#pragma once

#include "core/buffer/FileManager.h"
#include "core/config/AppPaths.h"
#include "core/config/ConfigFile.h"
#include "core/editor/EditorLanguage.h"
#include "core/funclist/FunctionParser.h"
#include "core/run/RunCommand.h"
#include "core/search/Search.h"
#include "core/i18n/NativeLanguage.h"
#include "core/lang/LanguageRegistry.h"
#include "core/lang/StyleRegistry.h"
#include "core/lang/UserLanguage.h"
#include "core/session/Session.h"
#include "core/shortcut/Shortcuts.h"
#include "editor/EditorAdapter.h"

#include <QColor>
#include <QDialog>
#include <QHash>
#include <QMainWindow>
#include <QPointer>
#include <QStringList>

#include <array>
#include <climits>
#include <deque>
#include <functional>
#include <memory>
#include <optional>

class QAction;
class QPrinter;
class QSystemTrayIcon;
class QMenu;
class QTranslator;
class QCheckBox;
class QDialog;
class QLineEdit;
class QToolBar;
class QFileSystemWatcher;
class QProgressDialog;
class QLabel;
class QListWidget;
class QMenu;
class QTranslator;
class QSplitter;
class QTimer;
class ScintillaEditBase;

namespace npp::core {
class AutoCompletion;
}

namespace npp::ui {

class ActionRegistry;
class DockManager;
class EditorPane;
class FindReplaceDialog;
class SearchResultsPanel;
class FunctionListPanel;
class DocumentMapPanel;
class ProjectPanel;
class FolderAsWorkspacePanel;
class DocumentListPanel;
class CharacterPanel;
class ClipboardHistoryPanel;
class UdlPanel;
class TaskListPopup;
class PreferencesDialog;
class PluginHost;

// Command line options of upstream CmdLineParams handled by the MVP
struct CommandLine {
    QStringList files;
    int  line = -1, column = -1, position = -1;
    int  x = INT_MIN, y = INT_MIN;  // -x<left> -y<top>
    QString language;          // -l<lang>
    QString sessionFile;       // -openSession <file>
    QString settingsDir;       // -settingsDir=<dir>
    bool multiInstance = false;
    bool noSession = false;
    bool readOnly = false;
    bool alwaysOnTop = false;
    bool systemTray = false;   // -systemtray
    bool noPlugin = false;     // -noPlugin
};
CommandLine parseCommandLine(const QStringList& args);

// DockManager layout <-> config.xml <GUIConfig name="DockingManager"> (PanelCommands.cpp)
struct DockLayout;
void layoutToDockingData(const DockLayout& layout, core::DockingData& d);
DockLayout dockingDataToLayout(const core::DockingData& d);

class MainWindow final : public QMainWindow {
    Q_OBJECT
    friend class PluginHost;
public:
    explicit MainWindow(const CommandLine& cmd = {}, QWidget* parent = nullptr);
    ~MainWindow() override;

    // Opens command line files (also used for files forwarded by a second instance).
    void openFromCommandLine(const CommandLine& cmd);
    core::Buffer* openFile(const QString& path, const core::LoadOptions& options = {}, bool activate = true);
    void ensureDocument();

    // Access for tests
    PreferencesDialog* preferencesDialog();
    void applySettings();
    void setUiLanguage(const std::string& localizationFile);
    void applyTheme();
    void applyStylesToEditors();
    void setShortcuts(const core::ShortcutsFile& f);
    QDialog* styleConfigurator() const noexcept { return m_styleConfigurator; }
    const std::string& currentThemePath() const noexcept { return m_currentThemePath; }
    bool isDarkMode() const;
    core::ConfigFile& mutableConfig() noexcept { return m_config; }
    QMenu* tabContextMenu();
    QToolBar* mainToolbar() const noexcept { return m_toolbar; }
    QString toolbarIconSet() const;                         // for the current tab (tests)
    QMenu* editorContextMenu() { return buildContextMenu(contextMenuEntries(true), this); }
    const core::StyleRegistry& styles() const noexcept { return m_styles; }
    const core::NativeLanguage& nativeLanguage() const noexcept { return m_nativeLang; }
    void showGoToDialog();
    void goTo(long long n, bool offset);
    void showIncrementalSearch();
    QLineEdit* incrementalSearchEdit() const noexcept { return m_incEdit; }
    core::AutoCompletion* autoCompletion(EditorPane* p);
    void playMacro(const std::vector<core::MacroStep>& steps);
    const std::vector<core::MacroStep>& recordedMacro() const noexcept { return m_macro; }
    const core::ShortcutsFile& shortcuts() const noexcept { return m_shortcuts; }
    FindReplaceDialog* findDialog();
    SearchResultsPanel* searchResults(bool show = true);
    // --- phase 5 panels (PanelCommands.cpp)
    static const char* const kPanelFunctionList;
    static const char* const kPanelDocumentMap;
    DocumentMapPanel* documentMap() const noexcept { return m_docMap; }
    static std::string projectPanelId(int i) { return "npp.project" + std::to_string(i + 1); }
    ProjectPanel* projectPanel(int i) const { return m_projectPanels[static_cast<size_t>(i)]; }
    static const char* const kPanelFolderAsWorkspace;
    FolderAsWorkspacePanel* folderAsWorkspace() const noexcept { return m_folderPanel; }
    static const char* const kPanelDocumentList;
    DocumentListPanel* documentList() const noexcept { return m_docList; }
    static const char* const kPanelCharacter;
    static const char* const kPanelClipboardHistory;
    CharacterPanel* characterPanel() const noexcept { return m_charPanel; }
    ClipboardHistoryPanel* clipboardHistory() const noexcept { return m_clipboardPanel; }
    static const char* const kPanelUdl;
    UdlPanel* udlPanel() const noexcept { return m_udlPanel; }
    const core::UserLanguageRegistry& userLanguages() const noexcept { return m_udls; }
    void refreshUdlMenu(); // Language menu entries of the UDLs
    // --- Window menu, Ctrl+Tab switcher (WindowCommands.cpp)
    void switchDocument(bool next);
    std::vector<std::pair<core::BufferId, int>> switcherOrder() const; // MRU or tab order
    QWidget* taskList() const;
    void setTaskListKeepOpen(bool on) noexcept { m_taskListKeepOpen = on; } // tests: Ctrl is not held
    void sortTabs(int key, bool ascending);   // 0 name, 1 path, 2 type, 3 size, 4 time
    void showWindowsDialog();
    QDialog* windowsDialog() const noexcept { return m_windowsDialog; }
    void updateWindowMenu();
    // --- tools, summary, monitoring, help (ToolCommands.cpp)
    QDialog* hashDialog(int type, bool fromFiles); // core::HashType
    QString summaryText();
    void setMonitoring(core::Buffer* b, bool on);
    std::string shortcutsXmlHmac() const;   // HMAC of shortcuts.xml on disk
    bool shortcutsXmlTrusted();             // config.xml shortcutsXmlHMAC matches (else review + warning)
    void setQuietSecurityWarnings(bool on) noexcept { m_quietSecurityWarnings = on; } // tests
    static QString commandLineHelpText();
    QString debugInfoText() const;
    QDialog* openedDialog() const { return m_openedDialog; }
    // --- Run (RunCommands.cpp)
    core::RunVariables runVariables();
    QString expandRunCommand(const QString& command);
    bool runCommandLine(const QString& command);
    void addUserCommand(const QString& name, const QString& command);
    QDialog* runDialog() const { return m_runDialog; }
    // --- printing (PrintCommands.cpp)
    int printDocument(QPrinter& printer, bool selectionOnly); // pages printed, -1 if the printer cannot start
    QString expandPrintVariables(const std::string& text);
    void showPrintPreview();
    // --- tray icon (TrayCommands.cpp)
    void hideToTray();
    void restoreFromTray();
    QSystemTrayIcon* trayIcon() const noexcept { return m_tray; }
    void setForceTray(bool on) noexcept { m_forceTray = on; } // tests: headless platforms have no tray
    FunctionListPanel* functionList() const noexcept { return m_funcList; }
    DockManager* dockManager() const noexcept { return m_dock.get(); }
    void reloadFunctionList();
    std::vector<core::FoundFunction> parseFunctions(core::Buffer* b, bool* parsed = nullptr);
    ActionRegistry* actions() const noexcept { return m_actions; }
    core::FileManager& files() { return *m_files; }
    EditorPane* pane(int view) const { return m_panes[static_cast<size_t>(view)]; }
    EditorPane* currentPane() const;
    core::Buffer* currentBuffer() const;
    const core::AppPaths& paths() const noexcept { return m_paths; }
    const core::ConfigFile& config() const noexcept { return m_config; }
    bool saveSessionTo(const QString& path);
    bool loadSessionFrom(const QString& path);
    // Closes everything without asking (tests) and stores session/config like a normal exit.
    void shutdownForTest();
    // Writes the snapshot backups and session.xml now (timer tick; crash test).
    void backupNow() { backupDirtyBuffers(); }
    // Previous run ended abnormally: tells what was restored and where the crash report is.
    void showCrashRecoveryNotice(const QString& crashDir);
    // "tabs=<n> dirty=<n>" for the hidden --quit-after test option
    QString stateSummary() const;
    bool findNextInCurrentForTest(const core::SearchOptions& o) { return findNextInCurrent(o, false); }

protected:
    void closeEvent(QCloseEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    bool eventFilter(QObject* o, QEvent* ev) override;
    void changeEvent(QEvent* e) override;

private:
    // --- setup (MainWindow.cpp)
    void loadSettings();
    void importNotepadPlusPlusSettings();
    void createUi();
    void createPanels();
    void createStatusBar();
    void applyScintillaKeys(ScintillaEditBase* e);
    void applyEditorSettings(EditorPane* pane);
    void restoreWindowState();
    void saveSettings();
    bool prepareExit(); // asks for dirty documents (or backs them up), saves session

    // --- documents (MainWindow.cpp)
    EditorPane* otherPane() const;
    EditorPane* paneShowing(core::BufferId id) const;
    core::Buffer* buffer(core::BufferId id) const;
    void showBuffer(core::Buffer* b, EditorPane* pane, bool activate = true);
    void onCurrentBufferChanged(EditorPane* pane, core::BufferId id);
    void setActivePane(EditorPane* pane);
    void applyLanguageToPane(EditorPane* pane, core::Buffer* b);
    void setLanguage(core::Buffer* b, core::LangType lang, const std::string& udl = {});
    bool closeBuffer(core::BufferId id, EditorPane* pane, bool askSave = true);
    bool closeBuffers(std::vector<std::pair<core::BufferId, EditorPane*>> list);
    bool saveBuffer(core::Buffer* b, bool saveAs, bool copy = false);
    core::SaveStatus saveElevated(core::Buffer* b, const QString& path, bool copy);
    bool askToSave(core::Buffer* b); // false = cancel
    void moveOrCloneToOtherView(bool clone);
    void refreshBuffer(core::Buffer* b);
    void updateTitleAndStatus();
    void updateRecentMenu();
    void addToRecent(const std::string& path);
    void watchFile(const std::string& path, bool watch);
    void checkFilesystemChanges();
    void backupDirtyBuffers();
    void reloadBuffer(core::Buffer* b, int encoding = -1, std::optional<core::UniMode> mode = {});
    QString miscString(const char* strId, const char* fallback) const;
    QString tabText(const core::Buffer& b) const;
    std::vector<std::pair<core::BufferId, EditorPane*>> allTabs() const;

    // --- session (MainWindow.cpp)
    core::Session currentSession(bool withBackups) const;
    void restoreSession(const core::Session& s);

    // --- commands (MainCommands.cpp)
    void registerCommands();
    void registerFileCommands();
    void registerEditCommands();
    void registerViewCommands();
    void registerFormatCommands();
    void registerLanguageCommands();
    void registerMiscCommands();
    // --- commands (EditCommands.cpp)
    void registerEditOpsCommands();
    // --- commands (SearchCommands.cpp)
    void registerSearchMiscCommands();
    void applyIndicatorStyles(ScintillaEditBase* e);
    void applyRepresentations(EditorPane* pane);
    // --- find / replace (FindCommands.cpp)
    void registerFindCommands();
    void runFindAction(int action);
    bool findNextInCurrent(const core::SearchOptions& o, bool showStatus);
    int findAllToPanel(const core::SearchOptions& o, const std::vector<core::Buffer*>& buffers, bool inSelection);
    int runFindInFiles(const core::FindInFilesOptions& fo, const core::SearchOptions& o, bool replacing,
                       const std::vector<std::string>* fileList = nullptr); // fileList: Find in Projects
    bool runInSlices(size_t count, const std::function<void(size_t)>& work, QProgressDialog& progress,
                     const std::function<QString(size_t)>& label);
    void openSearchResult(const QString& file, intptr_t start, intptr_t end, int line, bool lineRelative);
    std::vector<core::Buffer*> openBuffersInTabOrder() const;
    template <class F> auto withDocument(core::Buffer* b, F&& f);
    // --- highlighting (Highlighters.cpp)
    void connectHighlighters(EditorPane* p);
    void updateHighlights(EditorPane* p);
    void braceMatch(EditorPane* p);
    void smartHighlight(EditorPane* p);
    void addHotSpots(EditorPane* p);
    // --- auto-completion (Completion.cpp)
    void registerCompletionCommands();
    void connectAutoCompletion(EditorPane* p);
    void updateAutoCompletionLanguage(EditorPane* p);
    void onCharAdded(EditorPane* p, int ch);
    // --- phase 5 panels (PanelCommands.cpp)
    void createExtraPanels();
    void registerPanelCommands();
    void panelsOnBufferChanged();
    void panelsOnStylesChanged();
    bool panelsPrepareExit(); // asks to save modified workspaces
    void panelsSaveSettings(); // panel states to m_config
    void panelsAfterRestore(); // after the dock layout was restored
    void updateDocumentList();
    void noteActivated(core::BufferId id, int view);
    void registerWindowCommands();
    void connectWindowMenu();
    void registerToolCommands();
    void pollMonitoredFiles();
    void recordShortcutsXmlHmac();
    void showCommandLineHelp();
    void showDebugInfo();
    void registerRunCommands();
    void showRunDialog();
    void rebuildRunMenu();
    void registerPrintCommands();
    void addPrintPreviewAction();
    void registerTrayCommands();
    bool closeToTray(QCloseEvent* event);
    void markFunctionListEntry();
    void createPluginHost();
    void pluginNotify(uint32_t code, uintptr_t id = 0);
    // --- small dialogs (ToolDialogs.cpp)
    void registerToolDialogs();
    void showFindCharsInRange();
    void showColumnEditor();
    void hideIncrementalSearch();
    enum IncType { IncFirst, IncNext, IncHighlightOnly };
    void incrementalSearch(IncType type, bool down);
    // --- settings (SettingsCommands.cpp, Theme.cpp)
    void registerSettingsCommands();
    // --- context menus, tab colours, pinned tabs (ContextMenus.cpp)
    struct ContextMenuEntry {
        int id = 0;          // 0 = separator
        QString text;        // empty = the command's menu text
        QString folder;      // sub menu
    };
    void registerTabCommands();
    void togglePin(EditorPane* p, int index);
    QColor tabColour(int colourId) const;
    void refreshTabDecorations(const core::Buffer& b);
    std::vector<ContextMenuEntry> contextMenuEntries(bool editZone) const;
    QMenu* buildContextMenu(const std::vector<ContextMenuEntry>& entries, QWidget* parent);
    void showEditorContextMenu(EditorPane* p, const QPoint& globalPos);
    void showTabContextMenu(EditorPane* p, int index, const QPoint& globalPos);
    void ensureContextMenuFile();
    // nativeLang.xml, or english.xml when none (to restore dialog texts after a switch to English)
    const core::NativeLanguage* dialogLanguage();
    void loadQtTranslation();
    void syncToolbarFromMode(bool dark);
    void syncToolbarToMode(bool dark);
public:
    // --- plugins (phase 7, plugins/PluginHost)
    PluginHost* plugins() const noexcept { return m_plugins; }
    std::string pluginsDir() const;
    int importPlugins(const QStringList& files); // Settings > Import > Import plugin(s)
private:
    // --- toolbar (Toolbar.cpp)
    void buildToolbar();
    void buildMenus();
    void rebuildMenus();
    std::string localizationDir() const;
    void connectSystemTheme();
    // --- macros (MacroCommands.cpp)
    void registerMacroCommands();
    void recordFindAction(int action);
    void rebuildMacroMenu();
    void saveMacro(const core::MacroShortcut& m);
    void runMacroMultiple(std::vector<core::MacroStep> steps, int times);
    QMenu* macroMenu() const;

    CommandLine                 m_cmd;
    core::AppPaths              m_paths;
    bool                        m_uiPaletteDark = false; // dark palette / Fusion applied by applyTheme
    std::string                 m_dataDir;
    core::ConfigFile            m_config;
    core::LanguageRegistry      m_langs;
    core::StyleRegistry         m_styles;
    core::UserLanguageRegistry  m_udls;
    core::NativeLanguage        m_nativeLang;
    core::ShortcutsFile         m_shortcuts;
    core::LanguageApplyOptions  m_applyOptions;

    ScintillaEditBase*                 m_scratchWidget = nullptr;
    std::unique_ptr<EditorAdapter>     m_scratch;
    std::unique_ptr<core::FileManager> m_files;

    ActionRegistry*              m_actions = nullptr;
    PluginHost*                  m_plugins = nullptr; // child QObject; null with -noPlugin
    std::unique_ptr<DockManager> m_dock;
    QSplitter*                   m_splitter = nullptr;
    std::array<EditorPane*, 2>   m_panes{};
    int                          m_activeView = 0;
    DocumentListPanel*           m_docList = nullptr;
    std::function<void(const QString&)> m_setWorkspaceRoot; // Folder as Workspace panel
    FolderAsWorkspacePanel*      m_folderPanel = nullptr;
    bool                         m_dockStateRestored = false; // dockLayout-<backend>.bin was applied
    CharacterPanel*              m_charPanel = nullptr;
    ClipboardHistoryPanel*       m_clipboardPanel = nullptr;
    UdlPanel*                    m_udlPanel = nullptr;
    QTimer*                      m_udlSaveTimer = nullptr;
    std::vector<QPointer<QAction>> m_udlMenuActions;
    std::deque<std::pair<core::BufferId, int>> m_mru; // activated documents, most recent first
    TaskListPopup*               m_taskList = nullptr;
    bool                         m_taskListKeepOpen = false;
    QDialog*                     m_windowsDialog = nullptr;
    std::vector<QAction*>        m_windowMenuActions;
    QHash<QString, QPointer<QDialog>> m_hashDialogs;
    QTimer*                      m_monitorTimer = nullptr;
    bool                         m_quietSecurityWarnings = false;
    QPointer<QDialog>            m_openedDialog;
    QPointer<QDialog>            m_runDialog;
    QSystemTrayIcon*             m_tray = nullptr;
    bool                         m_forceTray = false;
    bool                         m_exitRequested = false; // File > Exit (not the close button)
    QStringList                  m_runHistory;
    std::vector<QPointer<QObject>> m_userCommandObjects; // Run menu entries of shortcuts.xml commands
    QMenu*                       m_recentMenu = nullptr;
    std::deque<std::string>      m_lastClosed;
    QFileSystemWatcher*          m_watcher = nullptr;
    QTimer*                      m_backupTimer = nullptr;
    bool                         m_checkingFiles = false;
    bool                         m_exiting = false;
    bool                         m_distractionFree = false;
    bool                         m_postIt = false;
    intptr_t                     m_beginSelectPos = -1; // Begin/End Select
    bool                         m_beginSelectColumn = false;
    int                          m_multiSelectFlag = 0;  // last Multi-select search flags
    FindReplaceDialog*           m_findDialog = nullptr;
    bool                         m_recording = false, m_playingMacro = false;
    bool                         m_highlighting = false;
    PreferencesDialog*           m_prefs = nullptr;
    std::string                  m_currentLanguageFile;
    QString                      m_defaultStyleName;
    bool                         m_themeIsDark = false, m_themeApplied = false;
    std::string                  m_currentThemePath;
    core::NativeLanguage         m_englishLang;
    QTranslator*                 m_qtTranslator = nullptr;
    QPointer<QDialog>            m_styleConfigurator;
    bool                         m_goToOffsetMode = false, m_columnEditorNumbers = false;
    QDialog*                     m_charRangeDialog = nullptr;
    QToolBar*                    m_incBar = nullptr;
    QToolBar*                    m_toolbar = nullptr;
    QLineEdit*                   m_incEdit = nullptr;
    QCheckBox*                   m_incHighlight = nullptr;
    QCheckBox*                   m_incMatchCase = nullptr;
    QLabel*                      m_incStatus = nullptr;
    std::array<std::unique_ptr<core::AutoCompletion>, 2> m_autoCompletion;
    std::vector<core::MacroStep> m_macro;
    std::vector<QAction*>        m_savedMacroActions;
    SearchResultsPanel*          m_searchResults = nullptr;
    FunctionListPanel*           m_funcList = nullptr;
    DocumentMapPanel*            m_docMap = nullptr;
    std::array<ProjectPanel*, 3> m_projectPanels{};
    std::unique_ptr<core::FunctionParsersManager> m_funcParsers;
    QTimer*                      m_panelTimer = nullptr;
    bool                         m_syncScrollV = false, m_syncScrollH = false, m_syncing = false;
    intptr_t                     m_syncLineOffset = 0, m_syncXOffset = 0;
    std::array<QLabel*, 6>       m_status{}; // language, length/lines, position, EOL, encoding, INS/OVR
};

} // namespace npp::ui
