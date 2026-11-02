// Phase-0 spike main window: tabs of Scintilla editors + docking panels.
#pragma once

#include "editor/LanguageConfig.h"

#include <QMainWindow>

#include <memory>

class QActionGroup;
class QLabel;
class QListWidget;
class QMenu;
class QTabWidget;
class ScintillaEditBase;

namespace npp::ui {

class DockManager;

class MainWindow final : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

    void openFile(const QString& path);

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    void createEditorArea();
    void createPanels();
    void createMenus();
    void createStatusBar();
    void loadLanguageConfig();
    void restoreSession();
    void saveSession() const;

    ScintillaEditBase* newEditor(const QString& path = {});
    ScintillaEditBase* currentEditor() const;
    ScintillaEditBase* editorAt(int index) const;
    int indexOfPath(const QString& path) const;

    void setLanguage(ScintillaEditBase* editor, const std::string& language);
    bool saveEditor(ScintillaEditBase* editor, bool saveAs);
    bool closeEditor(int index);
    void updateTabTitle(ScintillaEditBase* editor);
    void updateStatus();
    void refreshDocumentList();
    void findRegex();

    LanguageConfig               m_langConfig;
    bool                         m_langConfigLoaded = false;
    std::unique_ptr<DockManager> m_dock;
    QTabWidget*                  m_tabs = nullptr;
    QListWidget*                 m_docList = nullptr;
    QMenu*                       m_languageMenu = nullptr;
    QActionGroup*                m_languageGroup = nullptr;
    QLabel*                      m_statusLang = nullptr;
    QLabel*                      m_statusPos = nullptr;
    QLabel*                      m_statusEnc = nullptr;
    int                          m_untitledCounter = 0;
};

} // namespace npp::ui
