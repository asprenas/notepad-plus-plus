// Project Panel 1-3 (upstream WinControls/ProjectPanel): workspace tree of projects, virtual
// folders and files stored in a workspace XML file (core/project/Workspace).
#pragma once

#include "core/project/Workspace.h"

#include <QTreeWidget>
#include <QWidget>

class QToolButton;

namespace npp::ui {

class ProjectTree;

class ProjectPanel final : public QWidget {
    Q_OBJECT
public:
    explicit ProjectPanel(int index, QWidget* parent = nullptr);

    int index() const noexcept { return m_index; }
    const QString& workspacePath() const noexcept { return m_path; }
    bool isDirty() const noexcept { return m_dirty; }

    bool openWorkspace(const QString& path);   // false: not a workspace file
    void newWorkspace();
    bool saveWorkspace();                       // asks for a file name when untitled
    bool saveWorkspaceAs(const QString& path, bool copy = false);
    // Asks to save a modified workspace; false = cancel.
    bool askToSave();

    core::Workspace workspace() const;
    QTreeWidget* tree() const noexcept;
    QTreeWidgetItem* root() const;

    // Edit operations on the selected item (also used by the context menus)
    QTreeWidgetItem* addProject(const QString& name);
    QTreeWidgetItem* addFolder(QTreeWidgetItem* parent, const QString& name);
    QTreeWidgetItem* addFile(QTreeWidgetItem* parent, const QString& path);
    void addFilesFromDirectory(QTreeWidgetItem* parent, const QString& dir, bool recursive);
    void removeItem(QTreeWidgetItem* item);
    void moveItem(QTreeWidgetItem* item, int delta);
    // All file paths of the workspace (Find in Projects)
    QStringList allFiles() const;

    enum Kind { Root = 0, Project = 1, Folder = 2, File = 3 };
    static Kind kindOf(const QTreeWidgetItem* item);

signals:
    void openFileRequested(const QString& path);
    void workspaceChanged(const QString& path); // file name changed (config.xml)
    void dirtyChanged(bool dirty);

private:
    void setDirty(bool dirty);
    void showContextMenu(const QPoint& pos);
    void buildChildren(QTreeWidgetItem* parent, const std::vector<core::ProjectItem>& items);
    core::ProjectItem toItem(const QTreeWidgetItem* item) const;
    void updateRootText();
    QTreeWidgetItem* makeItem(QTreeWidgetItem* parent, Kind kind, const QString& text, const QString& path = {});
    QString askName(const QString& title, const QString& initial);
    void renameItem(QTreeWidgetItem* item);

    int m_index;
    ProjectTree* m_tree = nullptr;
    QToolButton* m_workspaceButton = nullptr;
    QString m_path;
    bool m_dirty = false;
};

// Tree with drag and drop restricted to upstream rules (files / folders into projects and
// folders, projects only under the workspace root).
class ProjectTree final : public QTreeWidget {
    Q_OBJECT
public:
    explicit ProjectTree(QWidget* parent = nullptr);

signals:
    void moved();

protected:
    void dragMoveEvent(QDragMoveEvent* e) override;
    void dropEvent(QDropEvent* e) override;

private:
    bool canDrop(QTreeWidgetItem* target, QAbstractItemView::DropIndicatorPosition pos) const;
};

} // namespace npp::ui
