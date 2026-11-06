// Elevated save through the GVfs admin backend (admin:// URIs), like GNOME Text Editor:
// the first write mounts admin:// (gvfsd-admin started by pkexec, one polkit prompt) and the
// mount stays with this process (admin.mount MountPerClient), so later writes need no
// password. Linux with GIO only (NPP_HAVE_GIO); no Qt headers here (GLib's "signals" member
// clashes with Qt's keyword macro).
#pragma once

#include <cstddef>
#include <string>

namespace npp::ui::gvfs_admin {

enum class Result { Ok, Cancelled, Failed, Unavailable };

// The GVfs daemon runs and supports the admin:// scheme.
bool available();

// admin:// is already mounted for this process (writes need no authentication).
bool mounted();

// Mounts admin:// when needed, then replaces the file contents (owner and mode kept).
Result write(const std::string& path, const char* data, std::size_t size, std::string* error);

} // namespace npp::ui::gvfs_admin
