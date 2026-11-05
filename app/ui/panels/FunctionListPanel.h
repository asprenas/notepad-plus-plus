// Function List panel (upstream WinControls/FunctionList/functionListPanel.*): tree of the
// functions found by npp-core FunctionParsersManager, search filter, sort, per-file tree state.
#pragma once

#include "core/funclist/FunctionParser.h"

#include <QHash>
#include <QSet>
#include <QWidget>

#include <functional>

class QLineEdit;
class QToolButton;
class QTreeWidget;
class QTreeWidgetItem;

namespace npp::ui {

class FunctionListPanel final : public QWidget {
    Q_OBJECT
public:
    explicit FunctionListPanel(QWidget* parent = nullptr);

    // Shows the result of a parse. `key` identifies the document (full path or untitled name) for the
    // saved tree state; `parsed` false = no parser for the language (empty tree).
    void setResult(const QString& key, const QString& rootName, const std::vector<core::FoundFunction>& found,
                   bool parsed);
    // Selects the function containing `line` (upstream markEntry). lineOf maps a position to its line.
    void markLine(intptr_t line, const std::function<intptr_t(intptr_t)>& lineOf);

    void setSortByDefault(bool sort);
    bool sortByDefault() const noexcept { return m_sortByDefault; }
    bool isSorted() const noexcept { return m_sorted; }
    void setSorted(bool sorted);
    void setFilter(const QString& text);

    QTreeWidget* tree() const noexcept { return m_tree; }
    // Leaves currently shown (tests): "name" or "class::name", in display order.
    QStringList visibleEntries() const;

signals:
    void gotoPosition(intptr_t pos);
    void reloadRequested();
    void sortByDefaultChanged(bool sort);

private:
    struct TreeState {
        QString filter;
        bool sorted = false;
        QSet<QString> collapsed; // collapsed class nodes
        bool rootCollapsed = false;
    };
    void rebuild();
    void saveState();

    QLineEdit* m_filter = nullptr;
    QToolButton* m_sortButton = nullptr;
    QTreeWidget* m_tree = nullptr;
    QString m_key, m_rootName;
    std::vector<core::FoundFunction> m_found;
    bool m_parsed = false;
    bool m_sorted = false;
    bool m_sortByDefault = false;
    QHash<QString, TreeState> m_states;
};

} // namespace npp::ui
