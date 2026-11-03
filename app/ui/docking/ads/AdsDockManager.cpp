#include "AdsDockManager.h"

#include <DockAreaWidget.h>
#include <DockManager.h>
#include <DockWidget.h>

#include <QMainWindow>

namespace npp::ui {

namespace {

ads::DockWidgetArea toAdsArea(DockArea area)
{
    switch (area) {
    case DockArea::Left:   return ads::LeftDockWidgetArea;
    case DockArea::Right:  return ads::RightDockWidgetArea;
    case DockArea::Top:    return ads::TopDockWidgetArea;
    case DockArea::Bottom: return ads::BottomDockWidgetArea;
    case DockArea::Floating: break;
    }
    return ads::RightDockWidgetArea;
}

} // namespace

AdsDockManager::AdsDockManager(QMainWindow* mainWindow)
{
    // Config flags are global in ADS; set before creating the manager.
    ads::CDockManager::setConfigFlag(ads::CDockManager::OpaqueSplitterResize, true);
    ads::CDockManager::setConfigFlag(ads::CDockManager::FocusHighlighting, true);
    ads::CDockManager::setConfigFlag(ads::CDockManager::AllTabsHaveCloseButton, true);

    // A CDockManager parented to a QMainWindow installs itself as its central widget.
    m_manager = new ads::CDockManager(mainWindow);
}

AdsDockManager::~AdsDockManager() = default; // widgets are owned by the Qt object tree

void AdsDockManager::setCentralWidget(QWidget* widget)
{
    auto* central = new ads::CDockWidget(m_manager, QStringLiteral("Editor"));
    central->setObjectName(QStringLiteral("npp.central"));
    central->setWidget(widget, ads::CDockWidget::ForceNoScrollArea);
    central->setFeature(ads::CDockWidget::DockWidgetClosable, false);
    central->setFeature(ads::CDockWidget::DockWidgetMovable, false);
    central->setFeature(ads::CDockWidget::DockWidgetFloatable, false);
    m_manager->setCentralWidget(central);
}

void AdsDockManager::addPanel(const PanelDesc& desc, QWidget* content)
{
    if (m_panels.count(desc.id))
        return;

    auto* dw = new ads::CDockWidget(m_manager, desc.title);
    dw->setObjectName(QString::fromStdString(desc.id)); // ADS uses objectName as state key
    dw->setWidget(content, ads::CDockWidget::ForceNoScrollArea);
    if (!desc.icon.isNull())
        dw->setIcon(desc.icon);
    dw->setFeature(ads::CDockWidget::DockWidgetClosable, desc.closable);

    const std::string id = desc.id;
    connect(dw, &ads::CDockWidget::viewToggled, this, [this, id](bool open) {
        emit panelVisibilityChanged(id, open);
    });

    Panel& panel = m_panels[desc.id];
    panel.widget = dw;
    placePanel(panel, desc.defaultArea);

    if (!desc.visibleByDefault)
        dw->toggleView(false);
}

void AdsDockManager::placePanel(Panel& panel, DockArea area)
{
    panel.area = area;
    if (area == DockArea::Floating) {
        m_manager->addDockWidgetFloating(panel.widget);
        return;
    }

    // Panels sharing a dock area are grouped as tabs (Notepad++ behaviour).
    auto it = m_areaWidgets.find(area);
    if (it != m_areaWidgets.end() && it->second) {
        m_manager->addDockWidget(ads::CenterDockWidgetArea, panel.widget, it->second);
    } else {
        m_areaWidgets[area] = m_manager->addDockWidget(toAdsArea(area), panel.widget);
    }
}

void AdsDockManager::removePanel(const std::string& id)
{
    auto it = m_panels.find(id);
    if (it == m_panels.end())
        return;
    m_manager->removeDockWidget(it->second.widget);
    it->second.widget->deleteLater();
    m_panels.erase(it);
}

std::vector<std::string> AdsDockManager::panelIds() const
{
    std::vector<std::string> ids;
    ids.reserve(m_panels.size());
    for (const auto& [id, panel] : m_panels)
        ids.push_back(id);
    return ids;
}

void AdsDockManager::showPanel(const std::string& id, bool activate)
{
    auto it = m_panels.find(id);
    if (it == m_panels.end())
        return;
    it->second.widget->toggleView(true);
    if (activate)
        it->second.widget->setAsCurrentTab();
}

void AdsDockManager::hidePanel(const std::string& id)
{
    auto it = m_panels.find(id);
    if (it != m_panels.end())
        it->second.widget->toggleView(false);
}

bool AdsDockManager::isPanelVisible(const std::string& id) const
{
    auto it = m_panels.find(id);
    return it != m_panels.end() && !it->second.widget->isClosed();
}

void AdsDockManager::setPanelTitle(const std::string& id, const QString& title)
{
    auto it = m_panels.find(id);
    if (it != m_panels.end())
        it->second.widget->setWindowTitle(title);
}

DockLayout AdsDockManager::layout() const
{
    // TODO(phase 2): derive the actual dock area after user drag & drop.
    // For now: last placed area, or Floating when the panel is floating.
    DockLayout result;
    for (const auto& [id, panel] : m_panels) {
        PanelLayout pl;
        pl.id      = id;
        pl.visible = !panel.widget->isClosed();
        pl.area    = panel.widget->isFloating() ? DockArea::Floating : panel.area;
        result.panels.push_back(pl);
    }
    return result;
}

void AdsDockManager::applyLayout(const DockLayout& layout)
{
    for (const auto& pl : layout.panels) {
        auto it = m_panels.find(pl.id);
        if (it == m_panels.end())
            continue;
        if (pl.area != it->second.area) {
            m_manager->removeDockWidget(it->second.widget);
            placePanel(it->second, pl.area);
        }
        it->second.widget->toggleView(pl.visible);
    }
}

QByteArray AdsDockManager::saveNativeState() const
{
    return m_manager->saveState();
}

bool AdsDockManager::restoreNativeState(const QByteArray& state)
{
    return !state.isEmpty() && m_manager->restoreState(state);
}

// ADS draws title bars / tabs with its own style sheet: colors from the application palette
void AdsDockManager::applyPalette(const QPalette& p)
{
    const QString win = p.color(QPalette::Window).name(), text = p.color(QPalette::WindowText).name();
    const QString base = p.color(QPalette::Base).name(), mid = p.color(QPalette::Mid).name(), hi = p.color(QPalette::Highlight).name();
    m_manager->setStyleSheet(QStringLiteral(
        "ads--CDockContainerWidget, ads--CDockAreaWidget { background: %1; }"
        "ads--CDockAreaTitleBar { background: %1; border-bottom: 1px solid %4; }"
        "ads--CDockWidgetTab { background: %1; border-color: %4; }"
        "ads--CDockWidgetTab[activeTab=\"true\"] { background: %3; }"
        "ads--CDockWidgetTab QLabel, ads--CDockAreaTitleBar QLabel { color: %2; }"
        "ads--CDockSplitter::handle { background: %4; }"
        "ads--CDockWidgetTab[focused=\"true\"] { background: %5; }")
        .arg(win, text, base, mid, hi));
}

} // namespace npp::ui
