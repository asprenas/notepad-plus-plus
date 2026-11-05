#include "ProjectPanel.h"

#include "core/util/PathUtil.h"

#include <QAction>
#include <QDir>
#include <QDirIterator>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QInputDialog>
#include <QMenu>
#include <QMessageBox>
#include <QStyle>
#include <QToolButton>
#include <QVBoxLayout>

namespace npp::ui {

namespace {

constexpr int kKindRole = Qt::UserRole;
constexpr int kPathRole = Qt::UserRole + 1;

QString qs(const std::string& s) { return QString::fromUtf8(s.data(), static_cast<qsizetype>(s.size())); }
std::string u8(const QString& s) { return s.toUtf8().toStdString(); }

} // namespace

// --- ProjectTree ----------------------------------------------------------------------------------

ProjectTree::ProjectTree(QWidget* parent) : QTreeWidget(parent)
{
    setHeaderHidden(true);
    setDragDropMode(QAbstractItemView::InternalMove);
    setSelectionMode(QAbstractItemView::SingleSelection);
    setDefaultDropAction(Qt::MoveAction);
    setEditTriggers(QAbstractItemView::NoEditTriggers);
}

bool ProjectTree::canDrop(QTreeWidgetItem* target, QAbstractItemView::DropIndicatorPosition pos) const
{
    const QList<QTreeWidgetItem*> sel = selectedItems();
    if (sel.isEmpty() || !target)
        return false;
    const auto dragged = ProjectPanel::kindOf(sel.first());
    if (dragged == ProjectPanel::Root)
        return false;
    QTreeWidgetItem* parent = pos == QAbstractItemView::OnItem ? target : target->parent();
    if (!parent)
        return false; // above / below the root
    for (QTreeWidgetItem* p = parent; p; p = p->parent())
        if (p == sel.first())
            return false; // into itself
    const auto parentKind = ProjectPanel::kindOf(parent);
    if (dragged == ProjectPanel::Project)
        return parentKind == ProjectPanel::Root;
    return parentKind == ProjectPanel::Project || parentKind == ProjectPanel::Folder;
}

void ProjectTree::dragMoveEvent(QDragMoveEvent* e)
{
    QTreeWidget::dragMoveEvent(e);
    if (e->source() != this || !canDrop(itemAt(e->position().toPoint()), dropIndicatorPosition()))
        e->ignore();
}

void ProjectTree::dropEvent(QDropEvent* e)
{
    if (e->source() != this || !canDrop(itemAt(e->position().toPoint()), dropIndicatorPosition())) {
        e->ignore();
        return;
    }
    QTreeWidget::dropEvent(e);
    emit moved();
}

// --- ProjectPanel ---------------------------------------------------------------------------------

ProjectPanel::Kind ProjectPanel::kindOf(const QTreeWidgetItem* item)
{
    return item ? static_cast<Kind>(item->data(0, kKindRole).toInt()) : Root;
}

ProjectPanel::ProjectPanel(int index, QWidget* parent) : QWidget(parent), m_index(index)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(2);

    m_workspaceButton = new QToolButton;
    m_workspaceButton->setText(tr("Workspace"));
    m_workspaceButton->setPopupMode(QToolButton::InstantPopup);
    m_workspaceButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
    auto* menu = new QMenu(m_workspaceButton);
    menu->addAction(tr("New Workspace"), this, [this] {
        if (askToSave())
            newWorkspace();
    });
    menu->addAction(tr("Open Workspace..."), this, [this] {
        if (!askToSave())
            return;
        const QString f = QFileDialog::getOpenFileName(this, tr("Open Workspace"), QFileInfo(m_path).absolutePath(),
                                                       tr("All types (*.*)"));
        if (!f.isEmpty() && !openWorkspace(f))
            QMessageBox::warning(this, tr("Open Workspace"), tr("The workspace could not be opened.\nIt seems the file to open is not a valid project file."));
    });
    menu->addAction(tr("Reload Workspace"), this, [this] {
        if (m_path.isEmpty())
            return;
        if (m_dirty && QMessageBox::question(this, tr("Reload Workspace"),
                                             tr("The current workspace was modified. Reloading will discard all modifications.\nDo you want to continue?"))
                           != QMessageBox::Yes)
            return;
        openWorkspace(m_path);
    });
    menu->addSeparator();
    menu->addAction(tr("Save"), this, [this] { saveWorkspace(); });
    menu->addAction(tr("Save As..."), this, [this] {
        const QString f = QFileDialog::getSaveFileName(this, tr("Save Workspace As"), m_path, tr("All types (*.*)"));
        if (!f.isEmpty())
            saveWorkspaceAs(f);
    });
    menu->addAction(tr("Save a Copy As..."), this, [this] {
        const QString f = QFileDialog::getSaveFileName(this, tr("Save a Copy As"), m_path, tr("All types (*.*)"));
        if (!f.isEmpty())
            saveWorkspaceAs(f, true);
    });
    menu->addSeparator();
    menu->addAction(tr("Find in Projects..."), this, &ProjectPanel::findInProjectsRequested);
    menu->addSeparator();
    menu->addAction(tr("Add New Project"), this, [this] {
        const QString n = askName(tr("Add New Project"), tr("Project Name"));
        if (!n.isEmpty())
            addProject(n);
    });
    m_workspaceButton->setMenu(menu);
    layout->addWidget(m_workspaceButton, 0, Qt::AlignLeft);

    m_tree = new ProjectTree;
    m_tree->setObjectName(QStringLiteral("projectTree%1").arg(index + 1));
    m_tree->setContextMenuPolicy(Qt::CustomContextMenu);
    layout->addWidget(m_tree, 1);
    connect(m_tree, &QTreeWidget::customContextMenuRequested, this, &ProjectPanel::showContextMenu);
    connect(m_tree, &ProjectTree::moved, this, [this] { setDirty(true); });
    connect(m_tree, &QTreeWidget::itemActivated, this, [this](QTreeWidgetItem* item, int) {
        if (kindOf(item) == File)
            emit openFileRequested(item->data(0, kPathRole).toString());
    });
    newWorkspace();
}

QTreeWidget* ProjectPanel::tree() const noexcept { return m_tree; }

QTreeWidgetItem* ProjectPanel::root() const { return m_tree->topLevelItem(0); }

QTreeWidgetItem* ProjectPanel::makeItem(QTreeWidgetItem* parent, Kind kind, const QString& text, const QString& path)
{
    auto* item = parent ? new QTreeWidgetItem(parent, QStringList{text}) : new QTreeWidgetItem(m_tree, QStringList{text});
    item->setData(0, kKindRole, kind);
    Qt::ItemFlags flags = Qt::ItemIsEnabled | Qt::ItemIsSelectable;
    switch (kind) {
    case Root:
        item->setIcon(0, style()->standardIcon(QStyle::SP_DriveHDIcon));
        flags |= Qt::ItemIsDropEnabled;
        break;
    case Project:
        item->setIcon(0, style()->standardIcon(QStyle::SP_DirHomeIcon));
        flags |= Qt::ItemIsDragEnabled | Qt::ItemIsDropEnabled;
        break;
    case Folder:
        item->setIcon(0, style()->standardIcon(QStyle::SP_DirIcon));
        flags |= Qt::ItemIsDragEnabled | Qt::ItemIsDropEnabled;
        break;
    case File:
        item->setData(0, kPathRole, path);
        item->setToolTip(0, QDir::toNativeSeparators(path));
        item->setIcon(0, style()->standardIcon(QFileInfo::exists(path) ? QStyle::SP_FileIcon : QStyle::SP_MessageBoxWarning));
        flags |= Qt::ItemIsDragEnabled;
        break;
    }
    item->setFlags(flags);
    return item;
}

void ProjectPanel::updateRootText()
{
    if (QTreeWidgetItem* r = root())
        r->setText(0, m_path.isEmpty() ? tr("Workspace") : QFileInfo(m_path).fileName());
}

void ProjectPanel::setDirty(bool dirty)
{
    if (m_dirty == dirty)
        return;
    m_dirty = dirty;
    if (QTreeWidgetItem* r = root()) {
        QFont f = r->font(0);
        f.setItalic(dirty); // upstream uses a different root icon for a modified workspace
        r->setFont(0, f);
    }
    emit dirtyChanged(dirty);
}

void ProjectPanel::newWorkspace()
{
    m_tree->clear();
    m_path.clear();
    makeItem(nullptr, Root, tr("Workspace"));
    m_dirty = true;
    setDirty(false);
}

void ProjectPanel::buildChildren(QTreeWidgetItem* parent, const std::vector<core::ProjectItem>& items)
{
    for (const core::ProjectItem& it : items) {
        if (it.kind == core::ProjectItem::Kind::File) {
            makeItem(parent, File, qs(it.name), qs(it.path));
        } else {
            QTreeWidgetItem* f = makeItem(parent, it.kind == core::ProjectItem::Kind::Project ? Project : Folder, qs(it.name));
            buildChildren(f, it.children);
        }
    }
}

bool ProjectPanel::openWorkspace(const QString& path)
{
    const auto ws = core::loadWorkspace(u8(path));
    if (!ws)
        return false;
    m_tree->clear();
    m_path = QFileInfo(path).absoluteFilePath();
    QTreeWidgetItem* r = makeItem(nullptr, Root, {});
    updateRootText();
    for (const core::ProjectItem& p : ws->projects) {
        QTreeWidgetItem* pi = makeItem(r, Project, qs(p.name));
        buildChildren(pi, p.children);
    }
    r->setExpanded(true);
    m_dirty = true;
    setDirty(false);
    emit workspaceChanged(m_path);
    return true;
}

core::ProjectItem ProjectPanel::toItem(const QTreeWidgetItem* item) const
{
    core::ProjectItem out;
    switch (kindOf(item)) {
    case File:
        return core::ProjectItem::file(u8(item->data(0, kPathRole).toString()));
    case Project:
        out = core::ProjectItem::project(u8(item->text(0)));
        break;
    default:
        out = core::ProjectItem::folder(u8(item->text(0)));
        break;
    }
    for (int i = 0; i < item->childCount(); ++i)
        out.children.push_back(toItem(item->child(i)));
    return out;
}

core::Workspace ProjectPanel::workspace() const
{
    core::Workspace ws;
    if (QTreeWidgetItem* r = root())
        for (int i = 0; i < r->childCount(); ++i)
            ws.projects.push_back(toItem(r->child(i)));
    return ws;
}

bool ProjectPanel::saveWorkspaceAs(const QString& path, bool copy)
{
    if (!core::saveWorkspace(workspace(), u8(path))) {
        QMessageBox::critical(this, tr("Save Workspace"),
                              tr("An error occurred while writing your workspace file.\nYour workspace has not been saved."));
        return false;
    }
    if (!copy) {
        m_path = QFileInfo(path).absoluteFilePath();
        updateRootText();
        setDirty(false);
        emit workspaceChanged(m_path);
    }
    return true;
}

bool ProjectPanel::saveWorkspace()
{
    if (m_path.isEmpty()) {
        const QString f = QFileDialog::getSaveFileName(this, tr("Save Workspace As"), {}, tr("All types (*.*)"));
        return !f.isEmpty() && saveWorkspaceAs(f);
    }
    return saveWorkspaceAs(m_path);
}

bool ProjectPanel::askToSave()
{
    if (!m_dirty)
        return true;
    const auto r = QMessageBox::question(this, tr("Project Panel %1").arg(m_index + 1),
                                         tr("The workspace was modified. Do you want to save it?"),
                                         QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel);
    if (r == QMessageBox::Cancel)
        return false;
    if (r == QMessageBox::Yes)
        return saveWorkspace();
    return true;
}

QTreeWidgetItem* ProjectPanel::addProject(const QString& name)
{
    QTreeWidgetItem* p = makeItem(root(), Project, name);
    root()->setExpanded(true);
    m_tree->setCurrentItem(p);
    setDirty(true);
    return p;
}

QTreeWidgetItem* ProjectPanel::addFolder(QTreeWidgetItem* parent, const QString& name)
{
    QTreeWidgetItem* f = makeItem(parent, Folder, name);
    parent->setExpanded(true);
    m_tree->setCurrentItem(f);
    setDirty(true);
    return f;
}

QTreeWidgetItem* ProjectPanel::addFile(QTreeWidgetItem* parent, const QString& path)
{
    const QString native = QDir::toNativeSeparators(QFileInfo(path).absoluteFilePath());
    QTreeWidgetItem* f = makeItem(parent, File, QFileInfo(path).fileName(), native);
    parent->setExpanded(true);
    setDirty(true);
    return f;
}

void ProjectPanel::addFilesFromDirectory(QTreeWidgetItem* parent, const QString& dir, bool recursive)
{
    // upstream recursiveAddFilesFrom: folders first (as virtual folders), then files, sorted
    QDir d(dir);
    const QFileInfoList dirs = d.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name | QDir::IgnoreCase);
    if (recursive)
        for (const QFileInfo& sub : dirs) {
            QTreeWidgetItem* f = makeItem(parent, Folder, sub.fileName());
            addFilesFromDirectory(f, sub.absoluteFilePath(), true);
        }
    for (const QFileInfo& f : d.entryInfoList(QDir::Files, QDir::Name | QDir::IgnoreCase))
        makeItem(parent, File, f.fileName(), QDir::toNativeSeparators(f.absoluteFilePath()));
    parent->setExpanded(true);
    setDirty(true);
}

void ProjectPanel::removeItem(QTreeWidgetItem* item)
{
    if (!item || kindOf(item) == Root)
        return;
    delete item;
    setDirty(true);
}

void ProjectPanel::moveItem(QTreeWidgetItem* item, int delta)
{
    QTreeWidgetItem* parent = item ? item->parent() : nullptr;
    if (!parent)
        return;
    const int from = parent->indexOfChild(item);
    const int to = from + delta;
    if (to < 0 || to >= parent->childCount())
        return;
    const bool expanded = item->isExpanded();
    parent->takeChild(from);
    parent->insertChild(to, item);
    item->setExpanded(expanded);
    m_tree->setCurrentItem(item);
    setDirty(true);
}

QStringList ProjectPanel::allFiles() const
{
    QStringList out;
    std::function<void(QTreeWidgetItem*)> walk = [&](QTreeWidgetItem* it) {
        if (kindOf(it) == File)
            out << it->data(0, kPathRole).toString();
        for (int i = 0; i < it->childCount(); ++i)
            walk(it->child(i));
    };
    if (QTreeWidgetItem* r = root())
        walk(r);
    return out;
}

QString ProjectPanel::askName(const QString& title, const QString& initial)
{
    bool ok = false;
    const QString n = QInputDialog::getText(this, title, tr("Name:"), QLineEdit::Normal, initial, &ok);
    return ok ? n.trimmed() : QString();
}

void ProjectPanel::renameItem(QTreeWidgetItem* item)
{
    const QString n = askName(tr("Rename"), item->text(0));
    if (!n.isEmpty() && n != item->text(0)) {
        item->setText(0, n);
        setDirty(true);
    }
}

void ProjectPanel::showContextMenu(const QPoint& pos)
{
    QTreeWidgetItem* item = m_tree->itemAt(pos);
    if (!item)
        return;
    m_tree->setCurrentItem(item);
    QMenu menu(this);
    const Kind kind = kindOf(item);
    auto addFolderAction = [&] {
        menu.addAction(tr("Add Folder"), this, [this, item] { addFolder(item, tr("Folder Name")); });
    };
    auto addFilesActions = [&] {
        menu.addAction(tr("Add Files..."), this, [this, item] {
            for (const QString& f : QFileDialog::getOpenFileNames(this, tr("Add Files"), QFileInfo(m_path).absolutePath()))
                addFile(item, f);
        });
        menu.addAction(tr("Add Files from Directory..."), this, [this, item] {
            const QString d = QFileDialog::getExistingDirectory(this, tr("Add Files from Directory"), QFileInfo(m_path).absolutePath());
            if (!d.isEmpty())
                addFilesFromDirectory(item, d, true);
        });
    };
    switch (kind) {
    case Root:
        menu.addAction(tr("Rename"), this, [this, item] { renameItem(item); })->setEnabled(false);
        menu.addAction(tr("Add New Project"), this, [this] { addProject(tr("Project Name")); });
        menu.addSeparator();
        for (QAction* a : m_workspaceButton->menu()->actions())
            if (!a->isSeparator() && a->text() != tr("Add New Project"))
                menu.addAction(a);
        break;
    case Project:
    case Folder:
        menu.addAction(tr("Move Up"), this, [this, item] { moveItem(item, -1); });
        menu.addAction(tr("Move Down"), this, [this, item] { moveItem(item, +1); });
        menu.addSeparator();
        menu.addAction(tr("Rename"), this, [this, item] { renameItem(item); });
        addFolderAction();
        addFilesActions();
        menu.addSeparator();
        menu.addAction(kind == Project ? tr("Remove") : tr("Remove"), this, [this, item, kind] {
            if (item->childCount()
                && QMessageBox::question(this, kind == Project ? tr("Remove Project") : tr("Remove Folder"),
                                         tr("All the sub-items will be removed.\nAre you sure you want to remove this folder from the project?"))
                       != QMessageBox::Yes)
                return;
            removeItem(item);
        });
        break;
    case File:
        menu.addAction(tr("Move Up"), this, [this, item] { moveItem(item, -1); });
        menu.addAction(tr("Move Down"), this, [this, item] { moveItem(item, +1); });
        menu.addSeparator();
        menu.addAction(tr("Open"), this, [this, item] { emit openFileRequested(item->data(0, kPathRole).toString()); });
        menu.addAction(tr("Rename"), this, [this, item] { renameItem(item); });
        menu.addAction(tr("Modify File Path"), this, [this, item] {
            const QString f = QFileDialog::getOpenFileName(this, tr("Modify File Path"), item->data(0, kPathRole).toString());
            if (f.isEmpty())
                return;
            QTreeWidgetItem* parent = item->parent();
            const int at = parent->indexOfChild(item);
            delete item;
            QTreeWidgetItem* n = addFile(parent, f);
            parent->takeChild(parent->indexOfChild(n));
            parent->insertChild(at, n);
        });
        menu.addAction(tr("Remove"), this, [this, item] { removeItem(item); });
        break;
    }
    menu.exec(m_tree->viewport()->mapToGlobal(pos));
}

} // namespace npp::ui
