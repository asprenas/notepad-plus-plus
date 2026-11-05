#include "CrashHandler.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <cstdio>
#include <cstdlib>
#include <cstring>

#if defined(Q_OS_WIN)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dbghelp.h>
#else
#include <csignal>
#include <ctime>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#if defined(__GLIBC__) || defined(Q_OS_MACOS)
#include <execinfo.h>
#define NPP_HAVE_BACKTRACE 1
#endif
#endif

namespace npp::ui {

namespace {

// Everything the handler needs is prepared at install time: no allocation while crashing.
#if defined(Q_OS_WIN)
wchar_t g_crashDir[MAX_PATH * 2] = {};

LONG WINAPI unhandledException(EXCEPTION_POINTERS* info)
{
    ::CreateDirectoryW(g_crashDir, nullptr);
    SYSTEMTIME t;
    ::GetLocalTime(&t);
    wchar_t path[MAX_PATH * 2 + 64];
    swprintf(path, sizeof(path) / sizeof(path[0]), L"%ls\\npp-%04u%02u%02u-%02u%02u%02u.dmp", g_crashDir, t.wYear, t.wMonth,
             t.wDay, t.wHour, t.wMinute, t.wSecond);
    const HANDLE file = ::CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file != INVALID_HANDLE_VALUE) {
        MINIDUMP_EXCEPTION_INFORMATION mei{};
        mei.ThreadId = ::GetCurrentThreadId();
        mei.ExceptionPointers = info;
        mei.ClientPointers = FALSE;
        ::MiniDumpWriteDump(::GetCurrentProcess(), ::GetCurrentProcessId(), file,
                            static_cast<MINIDUMP_TYPE>(MiniDumpWithIndirectlyReferencedMemory | MiniDumpScanMemory),
                            &mei, nullptr, nullptr);
        ::CloseHandle(file);
    }
    return EXCEPTION_EXECUTE_HANDLER; // terminate without the Windows error dialog
}
#else
char g_crashDir[4096] = {};

void writeStr(int fd, const char* s)
{
    if (::write(fd, s, std::strlen(s)) < 0) {
    }
}

void appendNum(char*& p, long v, int width)
{
    char buf[24];
    int n = 0;
    do {
        buf[n++] = static_cast<char>('0' + v % 10);
        v /= 10;
    } while (v > 0 && n < 20);
    while (n < width)
        buf[n++] = '0';
    while (n > 0)
        *p++ = buf[--n];
}

void fatalSignal(int sig)
{
    ::mkdir(g_crashDir, 0700);
    char path[4096 + 64];
    char* p = path;
    for (const char* s = g_crashDir; *s; ++s)
        *p++ = *s;
    const char* prefix = "/npp-";
    for (const char* s = prefix; *s; ++s)
        *p++ = *s;
    appendNum(p, static_cast<long>(std::time(nullptr)), 1);
    *p++ = '-';
    appendNum(p, static_cast<long>(::getpid()), 1);
    for (const char* s = ".log"; *s; ++s)
        *p++ = *s;
    *p = '\0';
    const int fd = ::open(path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd >= 0) {
        char line[64];
        char* q = line;
        for (const char* s = "Notepad++ Qt crash: signal "; *s; ++s)
            *q++ = *s;
        appendNum(q, sig, 1);
        *q++ = '\n';
        *q = '\0';
        writeStr(fd, line);
        writeStr(fd, "version " NPP_QT_VERSION "\n");
#ifdef NPP_HAVE_BACKTRACE
        void* frames[128];
        const int n = ::backtrace(frames, 128);
        ::backtrace_symbols_fd(frames, n, fd);
#endif
        ::close(fd);
    }
    std::signal(sig, SIG_DFL);
    ::raise(sig);
}
#endif

} // namespace

void installCrashHandler(const QString& crashDir)
{
#if defined(Q_OS_WIN)
    const std::wstring dir = QDir::toNativeSeparators(crashDir).toStdWString();
    wcsncpy(g_crashDir, dir.c_str(), sizeof(g_crashDir) / sizeof(g_crashDir[0]) - 1);
    ::SetUnhandledExceptionFilter(unhandledException);
#else
    const QByteArray dir = QFile::encodeName(crashDir);
    std::strncpy(g_crashDir, dir.constData(), sizeof(g_crashDir) - 1);
    struct sigaction sa {};
    sa.sa_handler = fatalSignal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESETHAND;
    for (int sig : {SIGSEGV, SIGBUS, SIGFPE, SIGILL, SIGABRT})
        ::sigaction(sig, &sa, nullptr);
#endif
}

void crashForTest()
{
    std::fprintf(stderr, "crash test: writing through a null pointer\n");
    std::fflush(stderr);
    volatile int* p = nullptr;
    *p = 42;
    std::abort();
}

RunMarker::RunMarker(const QString& path) : m_path(path)
{
    m_previousCrashed = QFile::exists(path);
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        f.write(QByteArray::number(QCoreApplication::applicationPid()) + ' '
                + QDateTime::currentDateTime().toString(Qt::ISODate).toUtf8() + '\n');
}

RunMarker::~RunMarker()
{
    QFile::remove(m_path);
}

} // namespace npp::ui
