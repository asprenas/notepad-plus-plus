// ADS (Qt Advanced Docking System) backend. Private to ui/docking/.
#pragma once

#include "../DockManager.h"

#include <QPointer>

#include <map>

namespace ads {
class CDockManager;
class CDockWidget;
class CDockAreaWidget;
}

namespace npp::ui {

class AdsDockManager final : public DockManager {
    Q_OBJECT
public:
    explicit AdsDockManager(QMainWindow* mainWindow);
    ~AdsDockManager() override;

    QString backendName() const override { return QStringLiteral("ADS"); }

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
    struct Panel {
        ads::CDockWidget* widget = nullptr;
        DockArea          area   = DockArea::Right;
    };

    void placePanel(Panel& panel, DockArea area);

    ads::CDockManager* m_manager = nullptr;
    std::map<std::string, Panel> m_panels;
    std::map<DockArea, QPointer<ads::CDockAreaWidget>> m_areaWidgets; // for tab grouping
};

} // namespace npp::ui
