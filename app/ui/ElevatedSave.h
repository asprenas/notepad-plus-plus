// Saving a protected file through a helper process with more rights
// (upstream: "Save failed ... launch Notepad++ in Administrator mode").
//  - Windows: this executable again, started with the "runas" verb (UAC prompt), in copy mode.
//  - Linux:   GVfs admin:// when available (one polkit prompt, the mount is kept for the
//             session: later saves need no password); otherwise pkexec (polkit dialog)
//             running /bin/sh to copy the bytes; writing into the existing file keeps its
//             owner and permissions.
//  - macOS:   osascript "do shell script ... with administrator privileges" (system password
//             dialog for each save), cat into the existing file.
#pragma once

#include <QByteArray>
#include <QString>

namespace npp::ui {

enum class ElevatedResult { Ok, Cancelled, Failed, Unsupported };

// True when the process already has administrator / root rights (asking again cannot help).
bool processIsElevated();

// True when an earlier elevated save authenticated for the whole session (GVfs admin://
// mounted): the next one needs neither a password nor the confirmation question.
bool elevationRemembered();

// Writes `data` to `target` with elevated rights. Blocks until the helper exits.
ElevatedResult elevatedWrite(const QString& target, const QByteArray& data, QString* error = nullptr);

// Helper mode of the executable: `--elevated-copy <source> <target>` copies the bytes of
// source into target (existing target is truncated in place, keeping its ACL / owner).
// Returns the process exit code; run before QApplication is created.
int runElevatedCopy(const QString& source, const QString& target);

// Command line option of the helper mode.
inline constexpr const char* kElevatedCopyOption = "--elevated-copy";

} // namespace npp::ui
