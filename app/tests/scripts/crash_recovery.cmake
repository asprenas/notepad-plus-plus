# Crash report + recovery test (docs/plan/phase-6-packaging-stabilization.md section 5):
#  1. start with a file, add unsaved text, write the snapshot backup and crash (--crash-test)
#  2. the crash folder holds a dump (Windows .dmp) or a backtrace log (POSIX .log)
#  3. the next start detects the abnormal end and restores the unsaved document
# cmake -DEXE=<notepadpp-qt> -DWORK=<empty dir> -P crash_recovery.cmake
file(REMOVE_RECURSE "${WORK}")
file(MAKE_DIRECTORY "${WORK}/settings")
file(WRITE "${WORK}/a.txt" "hello\n")
set(ENV{NPP_QT_INSTANCE_KEY} "npp-crash-recovery-test")

execute_process(COMMAND "${EXE}" "-settingsDir=${WORK}/settings" --crash-test "${WORK}/a.txt"
    RESULT_VARIABLE rc1 OUTPUT_VARIABLE out1 ERROR_VARIABLE err1 TIMEOUT 60)
message(STATUS "crash run: rc=${rc1} ${err1}")
if(rc1 EQUAL 0)
    message(FATAL_ERROR "the crash run exited normally")
endif()

file(GLOB reports "${WORK}/settings/crash/npp-*")
if(NOT reports)
    message(FATAL_ERROR "no crash report in ${WORK}/settings/crash")
endif()
message(STATUS "crash report: ${reports}")
file(GLOB backups "${WORK}/settings/backup/a.txt@*")
if(NOT backups)
    message(FATAL_ERROR "no snapshot backup of the unsaved document")
endif()
if(NOT EXISTS "${WORK}/settings/npp-running.marker")
    message(FATAL_ERROR "running marker missing after the crash")
endif()

execute_process(COMMAND "${EXE}" "-settingsDir=${WORK}/settings" --quit-after=2500
    RESULT_VARIABLE rc2 OUTPUT_VARIABLE out2 ERROR_VARIABLE err2 TIMEOUT 60)
message(STATUS "recovery run: rc=${rc2} ${err2}")
if(NOT rc2 EQUAL 0)
    message(FATAL_ERROR "recovery run failed")
endif()
if(NOT err2 MATCHES "abnormal termination detected: tabs=1 dirty=1")
    message(FATAL_ERROR "recovery notice not shown or unsaved document not restored")
endif()
if(EXISTS "${WORK}/settings/npp-running.marker")
    message(FATAL_ERROR "running marker left after a clean exit")
endif()
