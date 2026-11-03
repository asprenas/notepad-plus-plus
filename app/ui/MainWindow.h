// Sample main window on top of npp-core (phase 1): tabs of Scintilla views showing FileManager
// documents, languages/styles from the Notepad++ settings files, docking panels.
#pragma once

#include "core/buffer/FileManager.h"
#include "core/config/AppPaths.h"
#include "core/config/ConfigFile.h"
#include "core/editor/EditorLanguage.h"
#include "core/lang/LanguageRegistry.h"
#include "core/lang/StyleRegistry.h"
#include "core/lang/UserLanguage.h"
#include "core/search/Search.h"

#include <QMainWindow>

#include <memory>

class QActionGroup;
class QLabel;
class QListWidget;
class QTabWidget;
class ScintillaEditBase;

namespace npp::ui {

class DockManager;
class EditorAdapter;

class MainWindow final : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

    void openFile(const QString& path);
    // Opens the files of the Notepad++ session.xml (read only use of the settings folder).
    void restoreNppSession();
    // Adds an empty "new 1" document if no tab is open.
    void ensureDocument();

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    void loadSettings();
    void createEditorArea();
    void createPanels();
    void createMenus();
    void createStatusBar();
    void restoreWindowState();
    void saveWindowState() const;

    ScintillaEditBase* addTab(core::Buffer* buffer);
    ScintillaEditBase* currentEditor() const;
    ScintillaEditBase* editorAt(int index) const;
    core::Buffer* bufferOf(const ScintillaEditBase* e) const;
    int indexOfBuffer(const core::Buffer* b) const;

    void applyEditorSettings(ScintillaEditBase* e);
    void setLanguage(ScintillaEditBase* e, core::LangType lang, const std::string& udlName = {});
    bool saveEditor(ScintillaEditBase* e, bool saveAs);
    bool closeEditor(int index);
    void updateTabTitle(ScintillaEditBase* e);
    void updateStatus();
    void refreshDocumentList();
    void findRegex();

    core::AppPaths              m_paths;
    std::string                 m_dataDir;
    core::ConfigFile            m_config;     // read only: shared with the original Notepad++
    core::LanguageRegistry      m_langs;
    core::StyleRegistry         m_styles;
    core::UserLanguageRegistry  m_udls;
    core::LanguageApplyOptions  m_applyOptions;

    ScintillaEditBase*             m_scratchWidget = nullptr;
    std::unique_ptr<EditorAdapter> m_scratch;
    std::unique_ptr<core::FileManager> m_files;

    std::unique_ptr<DockManager> m_dock;
    QTabWidget*                  m_tabs = nullptr;
    QListWidget*                 m_docList = nullptr;
    QActionGroup*                m_languageGroup = nullptr;
    QLabel*                      m_statusLang = nullptr;
    QLabel*                      m_statusPos = nullptr;
    QLabel*                      m_statusEnc = nullptr;
};

} // namespace npp::ui
