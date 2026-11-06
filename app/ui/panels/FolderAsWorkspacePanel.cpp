#include "FolderAsWorkspacePanel.h"

#include "PlatformShell.h"

#include <QApplication>
#include <QClipboard>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QMenu>
#include <QMessageBox>
#include <QStyle>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <functional>

namespace npp::ui {

namespace {

constexpr int kPathRole = Qt::UserRole;
constexpr int kDirRole = Qt::UserRole + 1;
constexpr int kLoadedRole = Qt::UserRole + 2;

QString clean(const QString& p) { return QDir::cleanPath(QFileInfo(p).absoluteFilePath()); }

bool isUnder(const QString& path, const QString& dir)
{
    const Qt::CaseSensitivity cs =
#if defined(Q_OS_WIN) || defined(Q_OS_MACOS)
        Qt::CaseInsensitive;
#else
        Qt::CaseSensitive;
#endif
    if (path.compare(dir, cs) == 0)
        return true;
    const QString d = dir.endsWith('/') ? dir : dir + '/';
    return path.startsWith(d, cs);
}

std::string u8(const QString& s) { return QDir::toNativeSeparators(s).toUtf8().toStdString(); }
QString qs(const std::string& s) { return QDir::fromNativeSeparators(QString::fromUtf8(s.data(), static_cast<qsizetype>(s.size()))); }

} // namespace

QString FolderAsWorkspacePanel::pathOf(const QTreeWidgetItem* item)
{
    return item ? item->data(0, kPathRole).toString() : QString();
}

FolderAsWorkspacePanel::FolderAsWorkspacePanel(QWidget* parent) : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(2);
    auto* bar = new QHBoxLayout;
    bar->setContentsMargins(2, 2, 2, 0);
    auto button = [&](QStyle::StandardPixmap icon, const QString& tip, auto&& slot) {
        auto* b = new QToolButton;
        b->setIcon(style()->standardIcon(icon));
        b->setToolTip(tip);
        connect(b, &QToolButton::clicked, this, slot);
        bar->addWidget(b);
        return b;
    };
    button(QStyle::SP_ArrowUp, tr("Fold All"), [this] { m_tree->collapseAll(); });
    button(QStyle::SP_ArrowDown, tr("Expand All"), [this] {
        for (int i = 0; i < m_tree->topLevelItemCount(); ++i)
            m_tree->topLevelItem(i)->setExpanded(true);
    });
    bar->addStretch(1);
    layout->addLayout(bar);

    m_tree = new QTreeWidget;
    m_tree->setObjectName(QStringLiteral("folderAsWorkspaceTree"));
    m_tree->setHeaderHidden(true);
    m_tree->setContextMenuPolicy(Qt::CustomContextMenu);
    layout->addWidget(m_tree, 1);

    m_watcher = new QFileSystemWatcher(this);
    connect(m_watcher, &QFileSystemWatcher::directoryChanged, this, [this](const QString& dir) {
        if (QTreeWidgetItem* it = itemFor(dir, false))
            refresh(it);
    });
    connect(m_tree, &QTreeWidget::itemExpanded, this, [this](QTreeWidgetItem* it) {
        populate(it);
        watch(it, true);
    });
    connect(m_tree, &QTreeWidget::itemCollapsed, this, [this](QTreeWidgetItem* it) {
        if (it->parent()) // roots stay watched
            watch(it, false);
    });
    connect(m_tree, &QTreeWidget::itemActivated, this, [this](QTreeWidgetItem* it, int) {
        if (!it->data(0, kDirRole).toBool())
            emit openFileRequested(pathOf(it));
    });
    connect(m_tree, &QTreeWidget::customContextMenuRequested, this, &FolderAsWorkspacePanel::showContextMenu);
}

QTreeWidgetItem* FolderAsWorkspacePanel::makeItem(QTreeWidgetItem* parent, const QString& path, bool dir)
{
    const QString name = parent ? QFileInfo(path).fileName() : QDir::toNativeSeparators(path).section(QDir::separator(), -1, -1, QString::SectionSkipEmpty);
    auto* it = parent ? new QTreeWidgetItem(parent, QStringList{name}) : new QTreeWidgetItem(m_tree, QStringList{name.isEmpty() ? path : name});
    it->setData(0, kPathRole, path);
    it->setData(0, kDirRole, dir);
    it->setToolTip(0, QDir::toNativeSeparators(path));
    it->setIcon(0, style()->standardIcon(dir ? QStyle::SP_DirIcon : QStyle::SP_FileIcon));
    if (dir)
        it->setChildIndicatorPolicy(QTreeWidgetItem::ShowIndicator);
    return it;
}

void FolderAsWorkspacePanel::populate(QTreeWidgetItem* dirItem)
{
    if (dirItem->data(0, kLoadedRole).toBool())
        return;
    dirItem->setData(0, kLoadedRole, true);
    const QDir d(pathOf(dirItem));
    const auto filters = QDir::NoDotAndDotDot | QDir::System;
    for (const QFileInfo& fi : d.entryInfoList(QDir::Dirs | filters, QDir::Name | QDir::IgnoreCase))
        makeItem(dirItem, clean(fi.absoluteFilePath()), true);
    for (const QFileInfo& fi : d.entryInfoList(QDir::Files | filters, QDir::Name | QDir::IgnoreCase))
        makeItem(dirItem, clean(fi.absoluteFilePath()), false);
    dirItem->setChildIndicatorPolicy(dirItem->childCount() ? QTreeWidgetItem::ShowIndicator
                                                           : QTreeWidgetItem::DontShowIndicatorWhenChildless);
}

void FolderAsWorkspacePanel::refresh(QTreeWidgetItem* dirItem)
{
    if (!dirItem->data(0, kLoadedRole).toBool())
        return;
    // keep the expanded sub folders
    QStringList expanded;
    std::function<void(QTreeWidgetItem*)> collect = [&](QTreeWidgetItem* it) {
        for (int i = 0; i < it->childCount(); ++i)
            if (it->child(i)->isExpanded()) {
                expanded << pathOf(it->child(i));
                collect(it->child(i));
            }
    };
    collect(dirItem);
    const QString selected = pathOf(m_tree->currentItem());
    for (QTreeWidgetItem* c : dirItem->takeChildren()) {
        std::function<void(QTreeWidgetItem*)> unwatch = [&](QTreeWidgetItem* it) {
            watch(it, false);
            for (int i = 0; i < it->childCount(); ++i)
                unwatch(it->child(i));
        };
        unwatch(c);
        delete c;
    }
    dirItem->setData(0, kLoadedRole, false);
    populate(dirItem);
    for (const QString& e : expanded)
        if (QTreeWidgetItem* it = itemFor(e, true))
            it->setExpanded(true);
    if (!selected.isEmpty())
        if (QTreeWidgetItem* it = itemFor(selected, false))
            m_tree->setCurrentItem(it);
}

void FolderAsWorkspacePanel::watch(QTreeWidgetItem* dirItem, bool on)
{
    if (!dirItem->data(0, kDirRole).toBool())
        return;
    const QString p = pathOf(dirItem);
    if (on && !m_watcher->directories().contains(p))
        m_watcher->addPath(p);
    else if (!on && m_watcher->directories().contains(p))
        m_watcher->removePath(p);
}

void FolderAsWorkspacePanel::addRoot(const QString& dir)
{
    const QString d = clean(dir);
    if (!QFileInfo(d).isDir())
        return;
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i)
        if (isUnder(d, pathOf(m_tree->topLevelItem(i)))) { // upstream: already in the workspace, select it
            locate(d);
            return;
        }
    // a new root containing existing ones replaces them (upstream asks; here they are merged)
    for (int i = m_tree->topLevelItemCount() - 1; i >= 0; --i)
        if (isUnder(pathOf(m_tree->topLevelItem(i)), d))
            removeRoot(m_tree->topLevelItem(i));
    QTreeWidgetItem* r = makeItem(nullptr, d, true);
    r->setExpanded(true);
    emit rootsChanged();
}

void FolderAsWorkspacePanel::removeRoot(QTreeWidgetItem* rootItem)
{
    if (!rootItem || rootItem->parent())
        return;
    for (const QString& p : m_watcher->directories())
        if (isUnder(p, pathOf(rootItem)))
            m_watcher->removePath(p);
    delete rootItem;
    emit rootsChanged();
}

void FolderAsWorkspacePanel::removeAllRoots()
{
    while (m_tree->topLevelItemCount())
        removeRoot(m_tree->topLevelItem(0));
}

QStringList FolderAsWorkspacePanel::roots() const
{
    QStringList out;
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i)
        out << pathOf(m_tree->topLevelItem(i));
    return out;
}

QTreeWidgetItem* FolderAsWorkspacePanel::itemFor(const QString& path, bool expand)
{
    const QString p = clean(path);
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
        QTreeWidgetItem* it = m_tree->topLevelItem(i);
        if (!isUnder(p, pathOf(it)))
            continue;
        while (it) {
            if (isUnder(pathOf(it), p) && isUnder(p, pathOf(it)))
                return it;
            if (!it->data(0, kLoadedRole).toBool()) {
                if (!expand)
                    return nullptr;
                populate(it);
            }
            QTreeWidgetItem* next = nullptr;
            for (int c = 0; c < it->childCount() && !next; ++c)
                if (isUnder(p, pathOf(it->child(c))))
                    next = it->child(c);
            if (next && expand && next != it)
                it->setExpanded(true);
            it = next;
        }
        return nullptr;
    }
    return nullptr;
}

bool FolderAsWorkspacePanel::locate(const QString& path)
{
    QTreeWidgetItem* it = itemFor(path, true);
    if (!it)
        return false;
    m_tree->setCurrentItem(it);
    m_tree->scrollToItem(it);
    return true;
}

core::FileBrowserSettings FolderAsWorkspacePanel::settings() const
{
    core::FileBrowserSettings s;
    s.latestSelectedItem = m_tree->currentItem() ? u8(pathOf(m_tree->currentItem())) : std::string();
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
        QTreeWidgetItem* r = m_tree->topLevelItem(i);
        core::FileBrowserSettings::Root root{u8(pathOf(r)), {}};
        std::function<void(QTreeWidgetItem*)> walk = [&](QTreeWidgetItem* it) {
            for (int c = 0; c < it->childCount(); ++c)
                if (it->child(c)->isExpanded()) {
                    root.expanded.push_back(u8(pathOf(it->child(c))));
                    walk(it->child(c));
                }
        };
        if (r->isExpanded())
            root.expanded.push_back(root.folder);
        walk(r);
        s.roots.push_back(std::move(root));
    }
    return s;
}

void FolderAsWorkspacePanel::restore(const core::FileBrowserSettings& s)
{
    removeAllRoots();
    for (const auto& r : s.roots) {
        if (!QFileInfo(qs(r.folder)).isDir())
            continue;
        addRoot(qs(r.folder));
        QTreeWidgetItem* root = m_tree->topLevelItem(m_tree->topLevelItemCount() - 1);
        root->setExpanded(false);
        for (const auto& e : r.expanded)
            if (QTreeWidgetItem* it = itemFor(qs(e), true))
                it->setExpanded(true);
    }
    if (!s.latestSelectedItem.empty())
        locate(qs(s.latestSelectedItem));
}

void FolderAsWorkspacePanel::showContextMenu(const QPoint& pos)
{
    QTreeWidgetItem* it = m_tree->itemAt(pos);
    QMenu menu(this);
    if (!it) {
        menu.addAction(tr("Remove all roots"), this, &FolderAsWorkspacePanel::removeAllRoots);
        menu.exec(m_tree->viewport()->mapToGlobal(pos));
        return;
    }
    m_tree->setCurrentItem(it);
    const QString path = pathOf(it);
    const bool dir = it->data(0, kDirRole).toBool();
    const bool root = !it->parent();
    QTreeWidgetItem* dirItem = dir ? it : it->parent();
    const QString dirPath = pathOf(dirItem);

    if (root) {
        menu.addAction(tr("Remove"), this, [this, it] { removeRoot(it); });
        menu.addAction(tr("Remove all roots"), this, &FolderAsWorkspacePanel::removeAllRoots);
        menu.addSeparator();
    }
    if (!dir)
        menu.addAction(tr("Open"), this, [this, path] { emit openFileRequested(path); });
    if (dir) {
        menu.addAction(tr("New File..."), this, [this, it, path] {
            bool ok = false;
            const QString n = QInputDialog::getText(this, tr("New File"), tr("Name:"), QLineEdit::Normal, {}, &ok);
            if (!ok || n.isEmpty())
                return;
            QFile f(QDir(path).filePath(n));
            if (f.exists() || !f.open(QIODevice::WriteOnly)) {
                QMessageBox::warning(this, tr("New File"), tr("The file could not be created."));
                return;
            }
            f.close();
            refresh(it);
            emit openFileRequested(f.fileName());
        });
        menu.addAction(tr("New Folder..."), this, [this, it, path] {
            bool ok = false;
            const QString n = QInputDialog::getText(this, tr("New Folder"), tr("Name:"), QLineEdit::Normal, {}, &ok);
            if (ok && !n.isEmpty() && !QDir(path).mkdir(n))
                QMessageBox::warning(this, tr("New Folder"), tr("The folder could not be created."));
            refresh(it);
        });
        menu.addAction(tr("Find in Files..."), this, [this, path] { emit findInFilesRequested(path); });
        menu.addSeparator();
    }
    if (!root) {
        menu.addAction(tr("Rename"), this, [this, it, path] {
            bool ok = false;
            const QString n = QInputDialog::getText(this, tr("Rename"), tr("New name:"), QLineEdit::Normal,
                                                    QFileInfo(path).fileName(), &ok);
            if (!ok || n.isEmpty() || n == QFileInfo(path).fileName())
                return;
            if (!QFile::rename(path, QFileInfo(path).dir().filePath(n)))
                QMessageBox::warning(this, tr("Rename"), tr("The file could not be renamed."));
            refresh(it->parent());
        });
        menu.addAction(tr("Delete"), this, [this, it, path] {
            if (QMessageBox::question(this, tr("Delete"), tr("Are you sure you want to move \"%1\" to the Recycle Bin?")
                                                              .arg(QDir::toNativeSeparators(path)))
                != QMessageBox::Yes)
                return;
            QTreeWidgetItem* parent = it->parent();
            if (!QFile::moveToTrash(path))
                QMessageBox::warning(this, tr("Delete"), tr("The file could not be moved to the Recycle Bin."));
            refresh(parent);
        });
        menu.addSeparator();
    }
    menu.addAction(tr("Copy path"), this, [path] { QApplication::clipboard()->setText(QDir::toNativeSeparators(path)); });
    menu.addAction(tr("Copy file name"), this, [path] { QApplication::clipboard()->setText(QFileInfo(path).fileName()); });
    menu.addSeparator();
    menu.addAction(tr("%1 here").arg(fileManagerName()), this, [path] { revealInFileManager(path); });
    menu.addAction(tr("%1 here").arg(terminalName()), this, [dirPath] { openTerminalAt(dirPath); });
    if (!dir)
        menu.addAction(tr("Run by system"), this, [path] { QDesktopServices::openUrl(QUrl::fromLocalFile(path)); });
    menu.exec(m_tree->viewport()->mapToGlobal(pos));
}

} // namespace npp::ui
