// Document List panel (upstream WinControls/VerticalFileSwitcher): open documents of both views
// with Name / Ext. / Path columns (config.xml MISC fileSwitcher*), groups per view, sorting,
// close / save of the selection.
#pragma once

#include "core/buffer/Buffer.h"

#include <QWidget>

#include <vector>

class QTreeWidget;
class QTreeWidgetItem;

namespace npp::ui {

class DocumentListPanel final : public QWidget {
    Q_OBJECT
public:
    struct Entry {
        core::BufferId id = core::kInvalidBuffer;
        int view = 0;
        QString name, path;
        bool dirty = false, readOnly = false, current = false;
    };
    struct Columns {
        bool ext = true, path = false, groups = true;
        int extWidth = 50, pathWidth = 50;
    };

    explicit DocumentListPanel(QWidget* parent = nullptr);
    void setColumns(const Columns& c);
    Columns columns() const;
    void setEntries(const std::vector<Entry>& entries);
    QTreeWidget* tree() const noexcept { return m_tree; }
    // Names in display order (tests).
    QStringList names() const;

signals:
    void activateRequested(core::BufferId id, int view);
    void closeRequested(std::vector<std::pair<core::BufferId, int>> docs);
    void saveRequested(std::vector<std::pair<core::BufferId, int>> docs);
    void columnsChanged();

private:
    std::vector<std::pair<core::BufferId, int>> selection() const;
    void showContextMenu(const QPoint& pos);

    QTreeWidget* m_tree = nullptr;
    Columns m_columns;
    bool m_updating = false;
};

} // namespace npp::ui
