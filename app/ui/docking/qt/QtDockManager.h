// QDockWidget backend (fallback / abstraction-leak detector). Private to ui/docking/.
#pragma once

#include "../DockManager.h"

#include <map>

class QDockWidget;

namespace npp::ui {

class QtDockManager final : public DockManager {
    Q_OBJECT
public:
    explicit QtDockManager(QMainWindow* mainWindow);

    QString backendName() const override { return QStringLiteral("QT"); }

    void setCentralWidget(QWidget* widget) override;

    void addPanel(const PanelDesc& desc, QWidget* content) override;
    void removePanel(const std::string& id) override;
    std::vector<std::string> panelIds() const override;

    void showPanel(const std::string& id, bool activate) override;
    void hidePanel(const std::string& id) override;
    bool isPanelVisible(const std::string& id) const override;
    void setPanelTitle(const std::string& id, const QString& title) override;

    DockLayout layout() const override;
    void applyLayout(const DockLayout& layout) override;

    QByteArray saveNativeState() const override;
    bool restoreNativeState(const QByteArray& state) override;

private:
    void placePanel(QDockWidget* dock, DockArea area);

    QMainWindow* m_mainWindow = nullptr;
    std::map<std::string, QDockWidget*> m_panels;
};

} // namespace npp::ui
