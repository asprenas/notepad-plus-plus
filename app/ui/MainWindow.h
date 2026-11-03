// Main window of the Qt port (phase 2 MVP): two editor panes (split view), commands of the
// upstream menu, file I/O through npp-core, session / backups / change detection.
#pragma once

#include "core/buffer/FileManager.h"
#include "core/config/AppPaths.h"
#include "core/config/ConfigFile.h"
#include "core/editor/EditorLanguage.h"
#include "core/search/Search.h"
#include "core/i18n/NativeLanguage.h"
#include "core/lang/LanguageRegistry.h"
#include "core/lang/StyleRegistry.h"
#include "core/lang/UserLanguage.h"
#include "core/session/Session.h"
#include "core/shortcut/Shortcuts.h"
#include "editor/EditorAdapter.h"

#include <QMainWindow>
#include <QStringList>

#include <array>
#include <climits>
#include <deque>
#include <functional>
#include <memory>
#include <optional>

class QAction;
class QFileSystemWatcher;
class QLabel;
class QListWidget;
class QMenu;
class QSplitter;
class QTimer;
class ScintillaEditBase;

namespace npp::ui {

class ActionRegistry;
class DockManager;
class EditorPane;
class FindReplaceDialog;
class SearchResultsPanel;

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
};
CommandLine parseCommandLine(const QStringList& args);

class MainWindow final : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(const CommandLine& cmd = {}, QWidget* parent = nullptr);
    ~MainWindow() override;

    // Opens command line files (also used for files forwarded by a second instance).
    void openFromCommandLine(const CommandLine& cmd);
    core::Buffer* openFile(const QString& path, const core::LoadOptions& options = {}, bool activate = true);
    void ensureDocument();

    // Access for tests
    void playMacro(const std::vector<core::MacroStep>& steps);
    const std::vector<core::MacroStep>& recordedMacro() const noexcept { return m_macro; }
    const core::ShortcutsFile& shortcuts() const noexcept { return m_shortcuts; }
    FindReplaceDialog* findDialog();
    SearchResultsPanel* searchResults(bool show = true);
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

protected:
    void closeEvent(QCloseEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;

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
    // --- find / replace (FindCommands.cpp)
    void registerFindCommands();
    void runFindAction(int action);
    bool findNextInCurrent(const core::SearchOptions& o, bool showStatus);
    int findAllToPanel(const core::SearchOptions& o, const std::vector<core::Buffer*>& buffers, bool inSelection);
    int runFindInFiles(const core::FindInFilesOptions& fo, const core::SearchOptions& o, bool replacing);
    void openSearchResult(const QString& file, intptr_t start, intptr_t end, int line, bool lineRelative);
    std::vector<core::Buffer*> openBuffersInTabOrder() const;
    template <class F> auto withDocument(core::Buffer* b, F&& f);
    // --- macros (MacroCommands.cpp)
    void registerMacroCommands();
    void recordFindAction(int action);
    void rebuildMacroMenu();
    void saveMacro(const core::MacroShortcut& m);
    void runMacroMultiple(std::vector<core::MacroStep> steps, int times);
    QMenu* macroMenu() const;

    CommandLine                 m_cmd;
    core::AppPaths              m_paths;
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
    std::unique_ptr<DockManager> m_dock;
    QSplitter*                   m_splitter = nullptr;
    std::array<EditorPane*, 2>   m_panes{};
    int                          m_activeView = 0;
    QListWidget*                 m_docList = nullptr;
    std::function<void(const QString&)> m_setWorkspaceRoot; // Folder as Workspace panel
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
    std::vector<core::MacroStep> m_macro;
    std::vector<QAction*>        m_savedMacroActions;
    SearchResultsPanel*          m_searchResults = nullptr;
    bool                         m_syncScrollV = false, m_syncScrollH = false, m_syncing = false;
    intptr_t                     m_syncLineOffset = 0, m_syncXOffset = 0;
    std::array<QLabel*, 6>       m_status{}; // language, length/lines, position, EOL, encoding, INS/OVR
};

} // namespace npp::ui
