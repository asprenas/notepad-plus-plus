#include "QtDockManager.h"

#include <QDockWidget>
#include <QMainWindow>

namespace npp::ui {

namespace {

Qt::DockWidgetArea toQtArea(DockArea area)
{
    switch (area) {
    case DockArea::Left:   return Qt::LeftDockWidgetArea;
    case DockArea::Right:  return Qt::RightDockWidgetArea;
    case DockArea::Top:    return Qt::TopDockWidgetArea;
    case DockArea::Bottom: return Qt::BottomDockWidgetArea;
    case DockArea::Floating: break;
    }
    return Qt::RightDockWidgetArea;
}

DockArea fromQtArea(Qt::DockWidgetArea area)
{
    switch (area) {
    case Qt::LeftDockWidgetArea:   return DockArea::Left;
    case Qt::TopDockWidgetArea:    return DockArea::Top;
    case Qt::BottomDockWidgetArea: return DockArea::Bottom;
    default:                       return DockArea::Right;
    }
}

constexpr int kStateVersion = 1;

} // namespace

QtDockManager::QtDockManager(QMainWindow* mainWindow)
    : m_mainWindow(mainWindow)
{
    m_mainWindow->setDockOptions(QMainWindow::AnimatedDocks | QMainWindow::AllowNestedDocks
                                 | QMainWindow::AllowTabbedDocks | QMainWindow::GroupedDragging);
}

void QtDockManager::setCentralWidget(QWidget* widget)
{
    m_mainWindow->setCentralWidget(widget);
}

void QtDockManager::addPanel(const PanelDesc& desc, QWidget* content)
{
    if (m_panels.count(desc.id))
        return;

    auto* dock = new QDockWidget(desc.title, m_mainWindow);
    dock->setObjectName(QString::fromStdString(desc.id)); // saveState key
    dock->setWidget(content);
    QDockWidget::DockWidgetFeatures features = QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable;
    if (desc.closable)
        features |= QDockWidget::DockWidgetClosable;
    dock->setFeatures(features);

    const std::string id = desc.id;
    connect(dock, &QDockWidget::visibilityChanged, this, [this, id, dock](bool) {
        // visibilityChanged also fires for tab switches; report the "closed" state only.
        emit panelVisibilityChanged(id, !dock->isHidden());
    });

    m_panels[desc.id] = dock;
    placePanel(dock, desc.defaultArea);
    if (!desc.visibleByDefault)
        dock->hide();
}

void QtDockManager::placePanel(QDockWidget* dock, DockArea area)
{
    if (area == DockArea::Floating) {
        m_mainWindow->addDockWidget(Qt::RightDockWidgetArea, dock);
        dock->setFloating(true);
        return;
    }

    const Qt::DockWidgetArea qtArea = toQtArea(area);
    // Tab with an existing panel in the same area (Notepad++ behaviour).
    for (const auto& [otherId, other] : m_panels) {
        if (other != dock && !other->isFloating() && m_mainWindow->dockWidgetArea(other) == qtArea) {
            m_mainWindow->tabifyDockWidget(other, dock);
            return;
        }
    }
    m_mainWindow->addDockWidget(qtArea, dock);
}

void QtDockManager::removePanel(const std::string& id)
{
    auto it = m_panels.find(id);
    if (it == m_panels.end())
        return;
    m_mainWindow->removeDockWidget(it->second);
    it->second->deleteLater();
    m_panels.erase(it);
}

std::vector<std::string> QtDockManager::panelIds() const
{
    std::vector<std::string> ids;
    ids.reserve(m_panels.size());
    for (const auto& [id, dock] : m_panels)
        ids.push_back(id);
    return ids;
}

void QtDockManager::showPanel(const std::string& id, bool activate)
{
    auto it = m_panels.find(id);
    if (it == m_panels.end())
        return;
    it->second->show();
    if (activate)
        it->second->raise();
}

void QtDockManager::hidePanel(const std::string& id)
{
    auto it = m_panels.find(id);
    if (it != m_panels.end())
        it->second->hide();
}

bool QtDockManager::isPanelVisible(const std::string& id) const
{
    auto it = m_panels.find(id);
    return it != m_panels.end() && !it->second->isHidden();
}

void QtDockManager::setPanelTitle(const std::string& id, const QString& title)
{
    auto it = m_panels.find(id);
    if (it != m_panels.end())
        it->second->setWindowTitle(title);
}

DockLayout QtDockManager::layout() const
{
    DockLayout result;
    for (const auto& [id, dock] : m_panels) {
        PanelLayout pl;
        pl.id      = id;
        pl.visible = !dock->isHidden();
        pl.area    = dock->isFloating() ? DockArea::Floating : fromQtArea(m_mainWindow->dockWidgetArea(dock));
        result.panels.push_back(pl);
    }
    return result;
}

void QtDockManager::applyLayout(const DockLayout& layout)
{
    for (const auto& pl : layout.panels) {
        auto it = m_panels.find(pl.id);
        if (it == m_panels.end())
            continue;
        QDockWidget* dock = it->second;
        m_mainWindow->removeDockWidget(dock);
        dock->setFloating(false);
        placePanel(dock, pl.area);
        if (pl.area != DockArea::Floating && dock->isFloating()) // a hidden floating dock keeps the flag
            dock->setFloating(false);
        dock->setVisible(pl.visible);
    }
}

QByteArray QtDockManager::saveNativeState() const
{
    return m_mainWindow->saveState(kStateVersion);
}

bool QtDockManager::restoreNativeState(const QByteArray& state)
{
    return !state.isEmpty() && m_mainWindow->restoreState(state, kStateVersion);
}

} // namespace npp::ui
