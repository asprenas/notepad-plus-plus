#include "ElevatedSaveGio.h"

#include <gio/gio.h>

#include <cstring>

namespace npp::ui::gvfs_admin {

namespace {

struct MountWait {
    GMainLoop* loop = nullptr;
    GError* error = nullptr;
    bool ok = false;
};

void mountDone(GObject* source, GAsyncResult* res, gpointer data)
{
    auto* w = static_cast<MountWait*>(data);
    w->ok = g_file_mount_enclosing_volume_finish(G_FILE(source), res, &w->error);
    g_main_loop_quit(w->loop);
}

GFile* adminRoot() { return g_file_new_for_uri("admin:///"); }

bool isMounted(GFile* f)
{
    GMount* m = g_file_find_enclosing_mount(f, nullptr, nullptr);
    if (!m)
        return false;
    g_object_unref(m);
    return true;
}

// Dismissed polkit dialog / refused authorization: not retried with another method.
bool isRefusal(const GError* e)
{
    return e && e->domain == G_IO_ERROR
        && (e->code == G_IO_ERROR_PERMISSION_DENIED || e->code == G_IO_ERROR_FAILED_HANDLED
            || e->code == G_IO_ERROR_CANCELLED);
}

void setError(std::string* out, const GError* e)
{
    if (out && e && e->message)
        *out = e->message;
}

} // namespace

bool available()
{
    GVfs* vfs = g_vfs_get_default();
    if (!vfs || !g_vfs_is_active(vfs))
        return false;
    for (const gchar* const* s = g_vfs_get_supported_uri_schemes(vfs); s && *s; ++s)
        if (std::strcmp(*s, "admin") == 0)
            return true;
    return false;
}

bool mounted()
{
    if (!available())
        return false;
    GFile* root = adminRoot();
    const bool m = isMounted(root);
    g_object_unref(root);
    return m;
}

Result write(const std::string& path, const char* data, std::size_t size, std::string* error)
{
    if (!available())
        return Result::Unavailable;

    // admin:// URI of the local path (same escaping as file://)
    GFile* local = g_file_new_for_path(path.c_str());
    gchar* fileUri = g_file_get_uri(local);
    g_object_unref(local);
    const std::string uri = std::string("admin") + (fileUri + std::strlen("file"));
    g_free(fileUri);
    GFile* target = g_file_new_for_uri(uri.c_str());

    Result result = Result::Failed;
    bool ready = isMounted(target);
    if (!ready) {
        // One polkit prompt; a nested loop on the default context (Qt's GLib dispatcher keeps
        // painting meanwhile)
        MountWait w;
        w.loop = g_main_loop_new(nullptr, FALSE);
        GMountOperation* op = g_mount_operation_new();
        g_file_mount_enclosing_volume(target, G_MOUNT_MOUNT_NONE, op, nullptr, mountDone, &w);
        g_main_loop_run(w.loop);
        g_main_loop_unref(w.loop);
        g_object_unref(op);
        if (w.ok || (w.error && w.error->domain == G_IO_ERROR && w.error->code == G_IO_ERROR_ALREADY_MOUNTED)) {
            ready = true;
        } else {
            result = isRefusal(w.error) ? Result::Cancelled : Result::Failed;
            setError(error, w.error);
        }
        if (w.error)
            g_error_free(w.error);
    }

    if (ready) {
        GError* e = nullptr;
        if (g_file_replace_contents(target, data, size, nullptr, FALSE, G_FILE_CREATE_NONE, nullptr, nullptr, &e)) {
            result = Result::Ok;
        } else {
            result = Result::Failed;
            setError(error, e);
        }
        if (e)
            g_error_free(e);
    }
    g_object_unref(target);
    return result;
}

} // namespace npp::ui::gvfs_admin
