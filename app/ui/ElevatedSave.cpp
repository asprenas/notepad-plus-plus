#include "ElevatedSave.h"

#if defined(NPP_HAVE_GIO)
#include "ElevatedSaveGio.h"
#endif

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryFile>

#if defined(Q_OS_WIN)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#else
#include <unistd.h>
#endif

namespace npp::ui {

namespace {

// Bytes go through a private temporary file the helper reads.
QString writeTemp(const QByteArray& data, QString* error)
{
    QTemporaryFile tmp(QDir::temp().filePath(QStringLiteral("npp-elevated-XXXXXX.tmp")));
    tmp.setAutoRemove(false);
    if (!tmp.open() || tmp.write(data) != data.size() || !tmp.flush()) {
        if (error)
            *error = tmp.errorString();
        tmp.remove();
        return {};
    }
    return tmp.fileName();
}

#if defined(Q_OS_WIN)
QString quoteArg(const QString& a)
{
    QString s = a;
    s.replace(QLatin1Char('"'), QStringLiteral("\\\""));
    return QLatin1Char('"') + s + QLatin1Char('"');
}
#endif

} // namespace

bool processIsElevated()
{
#if defined(Q_OS_WIN)
    HANDLE token = nullptr;
    if (!::OpenProcessToken(::GetCurrentProcess(), TOKEN_QUERY, &token))
        return false;
    TOKEN_ELEVATION elevation{};
    DWORD size = 0;
    const bool ok = ::GetTokenInformation(token, TokenElevation, &elevation, sizeof(elevation), &size);
    ::CloseHandle(token);
    return ok && elevation.TokenIsElevated;
#else
    return ::geteuid() == 0;
#endif
}

bool elevationRemembered()
{
#if defined(NPP_HAVE_GIO)
    return gvfs_admin::mounted();
#else
    return false;
#endif
}

ElevatedResult elevatedWrite(const QString& target, const QByteArray& data, QString* error)
{
#if defined(NPP_HAVE_GIO)
    // GVfs admin:// first: one polkit prompt, then the mount is kept for this process
    {
        const bool wasMounted = gvfs_admin::mounted();
        std::string gioError;
        switch (gvfs_admin::write(target.toStdString(), data.constData(), static_cast<std::size_t>(data.size()), &gioError)) {
        case gvfs_admin::Result::Ok:
            return ElevatedResult::Ok;
        case gvfs_admin::Result::Cancelled:
            return ElevatedResult::Cancelled;
        case gvfs_admin::Result::Failed:
            // Authenticated and still failing (disk full, ...): reported. Mount failure: pkexec below
            if (wasMounted || gvfs_admin::mounted()) {
                if (error)
                    *error = QString::fromStdString(gioError);
                return ElevatedResult::Failed;
            }
            break;
        case gvfs_admin::Result::Unavailable:
            break; // no GVfs: pkexec for each save
        }
    }
#endif
    const QString tmp = writeTemp(data, error);
    if (tmp.isEmpty())
        return ElevatedResult::Failed;
    ElevatedResult result = ElevatedResult::Failed;

#if defined(Q_OS_WIN)
    const std::wstring exe = QDir::toNativeSeparators(QCoreApplication::applicationFilePath()).toStdWString();
    const std::wstring params = (QString::fromLatin1(kElevatedCopyOption) + QLatin1Char(' ')
                                 + quoteArg(QDir::toNativeSeparators(tmp)) + QLatin1Char(' ')
                                 + quoteArg(QDir::toNativeSeparators(target))).toStdWString();
    SHELLEXECUTEINFOW sei{};
    sei.cbSize = sizeof(sei);
    sei.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC | SEE_MASK_FLAG_NO_UI;
    sei.lpVerb = L"runas";
    sei.lpFile = exe.c_str();
    sei.lpParameters = params.c_str();
    sei.nShow = SW_HIDE;
    if (!::ShellExecuteExW(&sei)) {
        result = ::GetLastError() == ERROR_CANCELLED ? ElevatedResult::Cancelled : ElevatedResult::Failed;
    } else if (sei.hProcess) {
        ::WaitForSingleObject(sei.hProcess, INFINITE);
        DWORD code = 1;
        ::GetExitCodeProcess(sei.hProcess, &code);
        ::CloseHandle(sei.hProcess);
        result = code == 0 ? ElevatedResult::Ok : ElevatedResult::Failed;
        if (code != 0 && error)
            *error = QCoreApplication::translate("ElevatedSave", "The helper could not write the file (code %1).").arg(code);
    }
#elif defined(Q_OS_LINUX)
    const QString pkexec = QStandardPaths::findExecutable(QStringLiteral("pkexec"));
    if (pkexec.isEmpty()) {
        result = ElevatedResult::Unsupported;
        if (error)
            *error = QCoreApplication::translate("ElevatedSave", "pkexec (polkit) is not installed.");
    } else {
        // cat into the existing file: owner, group and mode of the target are kept
        QProcess p;
        p.start(pkexec, {QStringLiteral("/bin/sh"), QStringLiteral("-c"), QStringLiteral("cat -- \"$1\" > \"$2\""),
                         QStringLiteral("sh"), tmp, target});
        p.waitForFinished(-1);
        const int code = p.exitStatus() == QProcess::NormalExit ? p.exitCode() : -1;
        // pkexec: 126 = authorization dismissed, 127 = not authorized
        if (code == 0)
            result = ElevatedResult::Ok;
        else if (code == 126)
            result = ElevatedResult::Cancelled;
        else if (error)
            *error = QString::fromLocal8Bit(p.readAllStandardError()).trimmed();
    }
#else
    result = ElevatedResult::Unsupported;
#endif
    QFile::remove(tmp);
    return result;
}

int runElevatedCopy(const QString& source, const QString& target)
{
    QFile in(source);
    if (!in.open(QIODevice::ReadOnly))
        return 2;
    const QByteArray data = in.readAll();
    QFile out(target);
    // Truncate in place (not replace): the file keeps its ACL and owner
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return 3;
    if (out.write(data) != data.size())
        return 4;
    return out.flush() ? 0 : 4;
}

} // namespace npp::ui
