// Folder as Workspace panel (upstream WinControls/FileBrowser): several root folders, lazily
// listed (folders first), refreshed on file system changes, file operations from the context menu.
#pragma once

#include "core/config/ConfigFile.h"

#include <QWidget>

class QFileSystemWatcher;
class QTreeWidget;
class QTreeWidgetItem;

namespace npp::ui {

class FolderAsWorkspacePanel final : public QWidget {
    Q_OBJECT
public:
    explicit FolderAsWorkspacePanel(QWidget* parent = nullptr);

    void addRoot(const QString& dir);          // no-op when already a root (or inside one: selects it)
    void removeRoot(QTreeWidgetItem* rootItem);
    void removeAllRoots();
    QStringList roots() const;
    // Selects (expanding the parents) the item of `path`; false if not under a root.
    bool locate(const QString& path);
    QTreeWidgetItem* itemFor(const QString& path, bool expand);
    void refresh(QTreeWidgetItem* dirItem);

    core::FileBrowserSettings settings() const;
    void restore(const core::FileBrowserSettings& s);

    QTreeWidget* tree() const noexcept { return m_tree; }
    static QString pathOf(const QTreeWidgetItem* item);

signals:
    void openFileRequested(const QString& path);
    void findInFilesRequested(const QString& dir);
    void rootsChanged();

private:
    QTreeWidgetItem* makeItem(QTreeWidgetItem* parent, const QString& path, bool dir);
    void populate(QTreeWidgetItem* dirItem);
    void showContextMenu(const QPoint& pos);
    void watch(QTreeWidgetItem* dirItem, bool on);

    QTreeWidget* m_tree = nullptr;
    QFileSystemWatcher* m_watcher = nullptr;
};

} // namespace npp::ui
