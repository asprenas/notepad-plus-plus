#include "DockManager.h"

#if defined(NPP_DOCKING_BACKEND_ADS)
#  include "ads/AdsDockManager.h"
#elif defined(NPP_DOCKING_BACKEND_QT)
#  include "qt/QtDockManager.h"
#else
#  error "No docking backend selected (NPP_DOCKING_BACKEND_ADS / NPP_DOCKING_BACKEND_QT)"
#endif

namespace npp::ui {

std::unique_ptr<DockManager> createDockManager(QMainWindow* mainWindow)
{
#if defined(NPP_DOCKING_BACKEND_ADS)
    return std::make_unique<AdsDockManager>(mainWindow);
#else
    return std::make_unique<QtDockManager>(mainWindow);
#endif
}

QString dockAreaToString(DockArea area)
{
    switch (area) {
    case DockArea::Left:     return QStringLiteral("left");
    case DockArea::Right:    return QStringLiteral("right");
    case DockArea::Top:      return QStringLiteral("top");
    case DockArea::Bottom:   return QStringLiteral("bottom");
    case DockArea::Floating: return QStringLiteral("floating");
    }
    return QStringLiteral("right");
}

DockArea dockAreaFromString(const QString& s, DockArea fallback)
{
    if (s == QLatin1String("left"))     return DockArea::Left;
    if (s == QLatin1String("right"))    return DockArea::Right;
    if (s == QLatin1String("top"))      return DockArea::Top;
    if (s == QLatin1String("bottom"))   return DockArea::Bottom;
    if (s == QLatin1String("floating")) return DockArea::Floating;
    return fallback;
}

} // namespace npp::ui
