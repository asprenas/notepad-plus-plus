// Docking abstraction layer.
//
// RULES (docs/qt-cross-platform-port-plan.md, appendix B.4):
//  - Only this header is included outside ui/docking/. Backend headers
//    (ADS, QDockWidget implementation) must never leak.
//  - Panels are plain QWidgets identified by a stable string id.
//  - The backend is chosen in exactly one place: createDockManager().

#pragma once

#include <QByteArray>
#include <QIcon>
#include <QObject>
#include <QString>

#include <memory>
#include <string>
#include <vector>

class QMainWindow;
class QWidget;

namespace npp::ui {

enum class DockArea { Left, Right, Top, Bottom, Floating };

struct PanelDesc {
    std::string id;                       // stable id, e.g. "npp.documentList"
    QString     title;
    QIcon       icon;
    DockArea    defaultArea = DockArea::Right;
    bool        closable    = true;
    bool        visibleByDefault = true;
};

// Backend-independent layout info (to be persisted in config.xml).
struct PanelLayout {
    std::string id;
    bool        visible = false;
    DockArea    area    = DockArea::Right;
};

struct DockLayout {
    std::vector<PanelLayout> panels;
};

class DockManager : public QObject {
    Q_OBJECT
public:
    ~DockManager() override = default;

    // Backend name for diagnostics ("ADS", "QT").
    virtual QString backendName() const = 0;

    // The editor area. Must be called once before adding panels.
    virtual void setCentralWidget(QWidget* widget) = 0;

    virtual void addPanel(const PanelDesc& desc, QWidget* content) = 0;
    virtual void removePanel(const std::string& id) = 0;
    virtual std::vector<std::string> panelIds() const = 0;

    virtual void showPanel(const std::string& id, bool activate = true) = 0;
    virtual void hidePanel(const std::string& id) = 0;
    virtual bool isPanelVisible(const std::string& id) const = 0;
    virtual void setPanelTitle(const std::string& id, const QString& title) = 0;

    // Backend-independent layout (coarse, survives backend switch).
    virtual DockLayout layout() const = 0;
    virtual void applyLayout(const DockLayout& layout) = 0;

    // Backend-specific state (exact restore, same backend only).
    virtual QByteArray saveNativeState() const = 0;
    virtual bool restoreNativeState(const QByteArray& state) = 0;

signals:
    void panelVisibilityChanged(const std::string& id, bool visible);
};

// The only place where the backend is selected (compile-time, NPP_DOCKING_BACKEND).
std::unique_ptr<DockManager> createDockManager(QMainWindow* mainWindow);

QString dockAreaToString(DockArea area);
DockArea dockAreaFromString(const QString& s, DockArea fallback = DockArea::Right);

} // namespace npp::ui
