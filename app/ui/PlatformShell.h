// Platform shell helpers shared by commands and panels (file manager, terminal).
#pragma once

#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QUrl>

namespace npp::ui {

// Shows `path` in the file manager (selected on Windows; its folder elsewhere).
inline void revealInFileManager(const QString& path)
{
#if defined(Q_OS_WIN)
    QProcess::startDetached(QStringLiteral("explorer.exe"), {QStringLiteral("/select,") + QDir::toNativeSeparators(path)});
#else
    const QFileInfo fi(path);
    QDesktopServices::openUrl(QUrl::fromLocalFile(fi.isDir() ? fi.absoluteFilePath() : fi.absolutePath()));
#endif
}

// Opens a command prompt / terminal in `dir`.
inline void openTerminalAt(const QString& dir)
{
#if defined(Q_OS_WIN)
    QProcess::startDetached(QStringLiteral("cmd.exe"), {QStringLiteral("/K"), QStringLiteral("cd /d \"%1\"").arg(QDir::toNativeSeparators(dir))});
#elif defined(Q_OS_MACOS)
    QProcess::startDetached(QStringLiteral("open"), {QStringLiteral("-a"), QStringLiteral("Terminal"), dir});
#else
    for (const char* term : {"x-terminal-emulator", "gnome-terminal", "konsole", "xterm"})
        if (QProcess::startDetached(QString::fromLatin1(term), {}, dir))
            break;
#endif
}

} // namespace npp::ui
