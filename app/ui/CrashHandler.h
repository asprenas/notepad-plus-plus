// Crash reports and abnormal termination detection (replaces upstream MISC/Exception).
//  - Windows: unhandled SEH exception -> MiniDumpWriteDump into <crash dir>/npp-<time>.dmp
//  - Linux / macOS: fatal signal -> backtrace into <crash dir>/npp-<time>.log, then the
//    default action (core dump if enabled)
//  - a "running" marker in the session folder is removed on a clean exit; finding it at the
//    next start means the previous run ended abnormally (crash, kill, power loss)
#pragma once

#include <QString>

namespace npp::ui {

// Installs the handler; dumps go to `crashDir` (created on demand).
void installCrashHandler(const QString& crashDir);

// Crashes on purpose (hidden --crash-test option): null pointer write.
[[noreturn]] void crashForTest();

class RunMarker {
public:
    // Creates the marker; `previousRunCrashed()` tells whether one was already there.
    explicit RunMarker(const QString& path);
    ~RunMarker(); // clean exit: marker removed
    RunMarker(const RunMarker&) = delete;
    RunMarker& operator=(const RunMarker&) = delete;

    bool previousRunCrashed() const noexcept { return m_previousCrashed; }

private:
    QString m_path;
    bool m_previousCrashed = false;
};

} // namespace npp::ui
