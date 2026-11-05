#include "DocumentListPanel.h"

#include <QFileInfo>
#include <QHeaderView>
#include <QMenu>
#include <QMouseEvent>
#include <QStyle>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace npp::ui {

namespace {

constexpr int kIdRole = Qt::UserRole;
constexpr int kViewRole = Qt::UserRole + 1;

class MiddleClickFilter final : public QObject {
public:
    explicit MiddleClickFilter(QTreeWidget* tree, std::function<void(QTreeWidgetItem*)> f)
        : QObject(tree), m_tree(tree), m_f(std::move(f)) {}
protected:
    bool eventFilter(QObject* o, QEvent* e) override
    {
        if (e->type() == QEvent::MouseButtonRelease && static_cast<QMouseEvent*>(e)->button() == Qt::MiddleButton) {
            if (QTreeWidgetItem* it = m_tree->itemAt(static_cast<QMouseEvent*>(e)->position().toPoint()))
                m_f(it);
            return true;
        }
        return QObject::eventFilter(o, e);
    }
private:
    QTreeWidget* m_tree;
    std::function<void(QTreeWidgetItem*)> m_f;
};

} // namespace

DocumentListPanel::DocumentListPanel(QWidget* parent) : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    m_tree = new QTreeWidget;
    m_tree->setObjectName(QStringLiteral("documentList"));
    m_tree->setColumnCount(3);
    m_tree->setHeaderLabels({tr("Name"), tr("Ext."), tr("Path")});
    m_tree->setRootIsDecorated(false);
    m_tree->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_tree->setSortingEnabled(true);
    m_tree->sortByColumn(-1, Qt::AscendingOrder); // tab order until a header is clicked
    m_tree->setContextMenuPolicy(Qt::CustomContextMenu);
    m_tree->header()->setStretchLastSection(false);
    m_tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    layout->addWidget(m_tree);
    setColumns(m_columns);

    auto activate = [this](QTreeWidgetItem* it) {
        if (!m_updating && it && it->data(0, kIdRole).isValid())
            emit activateRequested(it->data(0, kIdRole).toULongLong(), it->data(0, kViewRole).toInt());
    };
    connect(m_tree, &QTreeWidget::itemClicked, this, [activate](QTreeWidgetItem* it, int) { activate(it); });
    connect(m_tree, &QTreeWidget::itemActivated, this, [activate](QTreeWidgetItem* it, int) { activate(it); });
    connect(m_tree, &QTreeWidget::customContextMenuRequested, this, &DocumentListPanel::showContextMenu);
    m_tree->viewport()->installEventFilter(new MiddleClickFilter(m_tree, [this](QTreeWidgetItem* it) {
        if (it->data(0, kIdRole).isValid())
            emit closeRequested({{it->data(0, kIdRole).toULongLong(), it->data(0, kViewRole).toInt()}});
    }));
}

void DocumentListPanel::setColumns(const Columns& c)
{
    const bool changed = c.ext != m_columns.ext || c.groups != m_columns.groups;
    m_columns = c;
    m_tree->setColumnHidden(1, !c.ext);
    m_tree->setColumnHidden(2, !c.path);
    m_tree->setColumnWidth(1, c.extWidth);
    m_tree->setColumnWidth(2, std::max(c.pathWidth, 50));
    if (changed)
        emit columnsChanged();
}

DocumentListPanel::Columns DocumentListPanel::columns() const
{
    Columns c = m_columns;
    if (c.ext)
        c.extWidth = m_tree->columnWidth(1);
    if (c.path)
        c.pathWidth = m_tree->columnWidth(2);
    return c;
}

void DocumentListPanel::setEntries(const std::vector<Entry>& entries)
{
    m_updating = true;
    const int sortColumn = m_tree->sortColumn();
    const Qt::SortOrder order = m_tree->header()->sortIndicatorOrder();
    m_tree->setSortingEnabled(false);
    m_tree->clear();
    bool twoViews = false;
    for (const Entry& e : entries)
        twoViews |= e.view == 1;
    twoViews &= std::any_of(entries.begin(), entries.end(), [](const Entry& e) { return e.view == 0; });
    const bool groups = m_columns.groups && twoViews;
    QTreeWidgetItem* groupItems[2] = {nullptr, nullptr};
    if (groups) {
        m_tree->setRootIsDecorated(true);
        for (int v = 0; v < 2; ++v) {
            groupItems[v] = new QTreeWidgetItem(m_tree, QStringList{v == 0 ? tr("Main View") : tr("Sub View")});
            groupItems[v]->setFlags(Qt::ItemIsEnabled);
            QFont f = groupItems[v]->font(0);
            f.setBold(true);
            groupItems[v]->setFont(0, f);
            groupItems[v]->setExpanded(true);
        }
    } else {
        m_tree->setRootIsDecorated(false);
    }
    QTreeWidgetItem* current = nullptr;
    for (const Entry& e : entries) {
        const QFileInfo fi(e.name);
        QString name = e.name, ext;
        if (m_columns.ext && !fi.suffix().isEmpty() && !fi.completeBaseName().isEmpty()) {
            ext = fi.suffix();
            name = e.name.left(e.name.size() - ext.size() - 1);
        }
        auto* it = groups ? new QTreeWidgetItem(groupItems[e.view]) : new QTreeWidgetItem(m_tree);
        it->setText(0, name);
        it->setText(1, ext);
        it->setText(2, e.path);
        it->setToolTip(0, e.path.isEmpty() ? e.name : e.path);
        it->setData(0, kIdRole, QVariant::fromValue<qulonglong>(e.id));
        it->setData(0, kViewRole, e.view);
        it->setIcon(0, style()->standardIcon(e.readOnly ? QStyle::SP_MessageBoxWarning
                                             : e.dirty  ? QStyle::SP_DialogSaveButton
                                                        : QStyle::SP_FileIcon));
        if (e.current)
            current = it;
    }
    m_tree->setSortingEnabled(true);
    m_tree->sortByColumn(sortColumn, order);
    if (current)
        m_tree->setCurrentItem(current);
    m_updating = false;
}

QStringList DocumentListPanel::names() const
{
    QStringList out;
    for (QTreeWidgetItemIterator it(m_tree); *it; ++it)
        if ((*it)->data(0, kIdRole).isValid())
            out << (*it)->text(0) + ((*it)->text(1).isEmpty() ? QString() : "." + (*it)->text(1));
    return out;
}

std::vector<std::pair<core::BufferId, int>> DocumentListPanel::selection() const
{
    std::vector<std::pair<core::BufferId, int>> out;
    for (QTreeWidgetItem* it : m_tree->selectedItems())
        if (it->data(0, kIdRole).isValid())
            out.emplace_back(it->data(0, kIdRole).toULongLong(), it->data(0, kViewRole).toInt());
    return out;
}

void DocumentListPanel::showContextMenu(const QPoint& pos)
{
    if (!m_tree->itemAt(pos))
        return;
    QMenu menu(this);
    menu.addAction(tr("Close Selected Files"), this, [this] { emit closeRequested(selection()); });
    menu.addAction(tr("Save Selected Files"), this, [this] { emit saveRequested(selection()); });
    menu.addSeparator();
    QAction* ext = menu.addAction(tr("Ext."));
    ext->setCheckable(true);
    ext->setChecked(m_columns.ext);
    connect(ext, &QAction::toggled, this, [this](bool on) {
        Columns c = columns();
        c.ext = on;
        setColumns(c);
    });
    QAction* path = menu.addAction(tr("Path"));
    path->setCheckable(true);
    path->setChecked(m_columns.path);
    connect(path, &QAction::toggled, this, [this](bool on) {
        Columns c = columns();
        c.path = on;
        setColumns(c);
    });
    QAction* groups = menu.addAction(tr("Group by View"));
    groups->setCheckable(true);
    groups->setChecked(m_columns.groups);
    connect(groups, &QAction::toggled, this, [this](bool on) {
        Columns c = columns();
        c.groups = on;
        setColumns(c);
    });
    menu.exec(m_tree->viewport()->mapToGlobal(pos));
}

} // namespace npp::ui
