// Find / Replace dialog (phase 3, P3-01/03/04): non-modal window with upstream's tabs
// (Find, Replace, Find in Files, Find in Projects, Mark) sharing one set of controls that are
// shown / hidden per tab. The dialog only collects options; MainWindow runs the actions.
#pragma once

#include "core/search/Search.h"

#include <QDialog>

class QCheckBox;
class QComboBox;
class QLabel;
class QPushButton;
class QRadioButton;
class QTabBar;

namespace npp::core {
struct FindHistory;
class NativeLanguage;
}

namespace npp::ui {

class FindReplaceDialog final : public QDialog {
    Q_OBJECT
public:
    enum Tab { FindTab = 0, ReplaceTab, FindInFilesTab, FindInProjectsTab, MarkTab };
    enum class Action {
        FindNext, Count, FindAllCurrent, FindAllOpen,
        Replace, ReplaceAll, ReplaceAllOpen,
        FindInFiles, ReplaceInFiles,
        FindInProjects, ReplaceInProjects,
        MarkAll, ClearMarks, CopyMarked,
    };

    explicit FindReplaceDialog(QWidget* parent = nullptr);

    void showTab(Tab t, const QString& findText = {});
    Tab currentTab() const;

    // Options as the core engine wants them (direction from the "Backward direction" box)
    core::SearchOptions options() const;
    void setOptions(const core::SearchOptions& o);
    bool inSelection() const;
    void setInSelection(bool on);
    QString directory() const;
    void setDirectory(const QString& dir);
    QString filters() const;
    void setFilters(const QString& f);
    bool recursive() const;
    bool includeHidden() const;
    void setFifOptions(bool recursive, bool includeHidden);
    // Find in Projects: Project Panel 1..3 check boxes (config.xml fifProjectPanel1..3)
    bool projectPanelChecked(int i) const;
    void setProjectPanelChecked(int i, bool on);
    bool bookmarkLine() const;
    bool purgeBeforeMark() const;
    void setMarkOptions(bool bookmarkLine, bool purge);

    // config.xml <FindHistory>
    void loadHistory(const core::FindHistory& h);
    void saveHistory(core::FindHistory& h) const;
    void addToHistory(); // current find / replace / filter / directory to the combo lists

    // Status line under the controls (upstream setStatusbarMessage)
    enum class Status { Normal, Warning, Failed };
    void setStatus(const QString& text, Status s = Status::Normal);
    QString status() const;

    void translate(const core::NativeLanguage* nl);

signals:
    void actionRequested(FindReplaceDialog::Action a);

private:
    void updateForTab();
    QPushButton* button(const QString& text, Action a);

    QTabBar* m_tabs = nullptr;
    QComboBox *m_find = nullptr, *m_replace = nullptr, *m_filters = nullptr, *m_dir = nullptr;
    QLabel *m_findLabel = nullptr, *m_replaceLabel = nullptr, *m_filtersLabel = nullptr, *m_dirLabel = nullptr, *m_status = nullptr;
    QPushButton* m_browse = nullptr;
    QCheckBox *m_backward = nullptr, *m_wholeWord = nullptr, *m_matchCase = nullptr, *m_wrap = nullptr, *m_inSelection = nullptr;
    QCheckBox* m_projects[3] = {};
    QCheckBox *m_recursive = nullptr, *m_hidden = nullptr, *m_followDoc = nullptr, *m_bookmarkLine = nullptr, *m_purge = nullptr;
    QCheckBox* m_dotNewline = nullptr;
    QRadioButton *m_normal = nullptr, *m_extended = nullptr, *m_regex = nullptr;
    std::map<Action, QPushButton*> m_buttons;
};

} // namespace npp::ui
