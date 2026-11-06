// Platform shell helpers shared by commands and panels (file manager, terminal).
#pragma once

#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
#include <QString>
#include <QStringList>
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
    QStringList terms;
    if (const QString env = qEnvironmentVariable("TERMINAL"); !env.isEmpty())
        terms << env;
    terms << QStringLiteral("x-terminal-emulator") << QStringLiteral("gnome-terminal") << QStringLiteral("konsole")
          << QStringLiteral("xfce4-terminal") << QStringLiteral("kitty") << QStringLiteral("alacritty") << QStringLiteral("xterm");
    for (const QString& term : terms)
        if (QProcess::startDetached(term, {}, dir))
            break;
#endif
}

// PowerShell offered: always on Windows, elsewhere only with pwsh in PATH.
inline bool hasPowerShell()
{
#if defined(Q_OS_WIN)
    return true;
#else
    static const bool found = !QStandardPaths::findExecutable(QStringLiteral("pwsh")).isEmpty();
    return found;
#endif
}

// Platform name of the file manager / terminal ("Explorer" / "cmd" upstream).
inline QString fileManagerName()
{
#if defined(Q_OS_WIN)
    return QStringLiteral("Explorer");
#elif defined(Q_OS_MACOS)
    return QStringLiteral("Finder");
#else
    return QStringLiteral("File Manager");
#endif
}
inline QString terminalName()
{
#if defined(Q_OS_WIN)
    return QStringLiteral("cmd");
#else
    return QStringLiteral("Terminal");
#endif
}

// Replacement text of an Open Containing Folder command (41019 / 41020) outside Windows;
// empty: keep the localized upstream text (always on Windows).
// tabMenu: the long tab context menu form ("Open Containing Folder in ...").
inline QString folderCommandText(int id, bool tabMenu)
{
#if defined(Q_OS_WIN)
    (void)id; (void)tabMenu;
    return {};
#else
    const QString prefix = tabMenu ? QStringLiteral("Open Containing Folder in ") : QString();
    if (id == 41019) return prefix + fileManagerName();
    if (id == 41020) return prefix + terminalName();
    return {};
#endif
}

} // namespace npp::ui
