// NPP_QT_STARTUP_LOG=1: start-up phases with the time since main() on stderr (docs/reference/performance.md).
#pragma once

#include <QElapsedTimer>

#include <cstdio>

namespace npp::ui {

inline QElapsedTimer& startupTimer()
{
    static QElapsedTimer t;
    return t;
}

inline void startupLog(const char* phase)
{
    static const bool on = qEnvironmentVariableIsSet("NPP_QT_STARTUP_LOG");
    if (on && startupTimer().isValid())
        std::fprintf(stderr, "startup %6lld ms  %s\n", static_cast<long long>(startupTimer().elapsed()), phase);
}

} // namespace npp::ui
