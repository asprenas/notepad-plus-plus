// NPPM_* compatibility messages of the plugin host (phase 7, P7-05): upstream numbers and meaning,
// strings as UTF-8 char* (sizes in bytes). The list of supported / unsupported messages is in
// docs/plugin-api-nppm.md; unsupported messages return 0.
#include "plugins/PluginHost.h"

#include "MainWindow.h"
#include "editor/EditorPane.h"
#include "menu/ActionRegistry.h"
#include "menu/CommandIds.h"

#include "core/lang/LangType.h"
#include "core/run/RunCommand.h"
#include "core/util/PathUtil.h"

#include <Scintilla.h>
#include <ScintillaEditBase.h>

#include <QAction>
#include <QCoreApplication>
#include <QLabel>
#include <QMenuBar>
#include <QStatusBar>
#include <QToolBar>

#include <cstring>
#include <filesystem>

// upstream message numbers (after the application headers: the header defines short macro names)
#define NPP_COMPAT_NO_ENUMS
#include <npp_msgs.h>

namespace npp::ui {

using namespace npp::core;

namespace {

constexpr int kMarkBookmark = 20; // upstream MARK_BOOKMARK
constexpr size_t kMaxPath = 260;  // upstream MAX_PATH buffers (NPPM_GETOPENFILENAMES)

// Copies s to buf (bufSize bytes). Returns the length, or -1 when it does not fit.
intptr_t copyTo(const std::string& s, char* buf, size_t bufSize)
{
    if (!buf)
        return static_cast<intptr_t>(s.size());
    if (s.size() + 1 > bufSize)
        return -1;
    std::memcpy(buf, s.c_str(), s.size() + 1);
    return static_cast<intptr_t>(s.size());
}

// upstream RUNCOMMAND_USER messages: TRUE / FALSE (buffer too small)
intptr_t copyBool(const std::string& s, uintptr_t size, intptr_t buf)
{
    return copyTo(s, reinterpret_cast<char*>(buf), static_cast<size_t>(size)) >= 0 ? 1 : 0;
}

const char* str(intptr_t l) { return reinterpret_cast<const char*>(l); }

std::string normalizedModuleName(std::string s)
{
    if (const auto dot = s.rfind('.'); dot != std::string::npos)
        s.erase(dot);
    for (char& c : s)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (s.size() > 3 && s.compare(0, 3, "lib") == 0)
        s.erase(0, 3);
    return s;
}

} // namespace

bool PluginHost::activateBuffer(uint64_t bufferId)
{
    MainWindow& w = *m_window;
    Buffer* b = w.buffer(bufferId);
    if (!b)
        return false;
    if (EditorPane* p = w.paneShowing(bufferId)) {
        p->activate(bufferId);
        w.setActivePane(p);
    } else {
        w.showBuffer(b, w.currentPane(), true);
    }
    return true;
}

intptr_t PluginHost::message(unsigned msg, uintptr_t wp, intptr_t lp)
{
    MainWindow& w = *m_window;
    Buffer* cur = w.currentBuffer();
    auto bufferOf = [&w](uintptr_t id) { return w.buffer(static_cast<BufferId>(id)); };
    auto setInt = [lp](intptr_t v) {
        if (int* p = reinterpret_cast<int*>(lp))
            *p = static_cast<int>(v);
        return intptr_t(1);
    };
    auto allocate = [&](int& next, int last) -> intptr_t {
        const int n = static_cast<int>(wp);
        if (n <= 0 || next + n - 1 > last || !lp)
            return 0;
        *reinterpret_cast<int*>(lp) = next;
        next += n;
        return 1;
    };
    auto hideWidget = [lp](QWidget* widget) -> intptr_t {
        if (!widget)
            return 0;
        const bool wasHidden = !widget->isVisibleTo(widget->parentWidget());
        widget->setVisible(lp == 0);
        return wasHidden ? 1 : 0;
    };

    switch (msg) {
    // --- views and documents --------------------------------------------------------------
    case NPPM_GETCURRENTSCINTILLA:
    case NPPM_GETCURRENTVIEW: {
        const int v = w.currentPane() ? w.currentPane()->viewIndex() : 0;
        return msg == NPPM_GETCURRENTVIEW ? v : setInt(v);
    }
    case NPPM_GETCURRENTBUFFERID:
        return cur ? static_cast<intptr_t>(cur->id) : 0;
    case NPPM_GETNBOPENFILES: {
        const int main = w.pane(0)->count(), second = w.pane(1)->count();
        return lp == PRIMARY_VIEW ? main : lp == SECOND_VIEW ? second : main + second;
    }
    case NPPM_GETOPENFILENAMES_DEPRECATED: { // (char** names, int count): main view then second view
        auto** names = reinterpret_cast<char**>(wp);
        const int max = static_cast<int>(lp);
        int n = 0;
        for (int v = 0; v < 2 && names; ++v)
            for (BufferId id : w.pane(v)->buffers()) {
                if (n >= max)
                    break;
                if (const Buffer* b = w.buffer(id)) {
                    const std::string& s = b->isUntitled() ? b->untitledName : b->path;
                    std::strncpy(names[n], s.c_str(), kMaxPath - 1);
                    names[n][kMaxPath - 1] = '\0';
                    ++n;
                }
            }
        return n;
    }
    case NPPM_GETFULLPATHFROMBUFFERID: {
        const Buffer* b = bufferOf(wp);
        return b ? copyTo(b->isUntitled() ? b->untitledName : b->path, reinterpret_cast<char*>(lp), static_cast<size_t>(-1) >> 1)
                 : -1;
    }
    case NPPM_GETPOSFROMBUFFERID: { // (bufferId, priorityView) -> view << 30 | index
        const BufferId id = static_cast<BufferId>(wp);
        const int first = lp == SUB_VIEW ? 1 : 0;
        for (int v : {first, 1 - first})
            if (const int i = w.pane(v)->indexOf(id); i >= 0)
                return (static_cast<intptr_t>(v) << 30) | i;
        return -1;
    }
    case NPPM_GETBUFFERIDFROMPOS: { // (index, view)
        if (lp != 0 && lp != 1)
            return 0;
        EditorPane* p = w.pane(static_cast<int>(lp));
        const int i = static_cast<int>(wp);
        return i >= 0 && i < p->count() ? static_cast<intptr_t>(p->bufferAt(i)) : 0;
    }
    case NPPM_GETCURRENTDOCINDEX: { // (0, view)
        if (lp != 0 && lp != 1)
            return -1;
        EditorPane* p = w.pane(static_cast<int>(lp));
        return p->isVisible() || lp == 0 ? p->currentIndex() : -1;
    }
    case NPPM_ACTIVATEDOC: { // (view, index)
        if (wp > 1)
            return 0;
        EditorPane* p = w.pane(static_cast<int>(wp));
        if (lp < 0 || lp >= p->count())
            return 0;
        return activateBuffer(p->bufferAt(static_cast<int>(lp))) ? 1 : 0;
    }
    case NPPM_SWITCHTOFILE: {
        if (!str(lp))
            return 0;
        const Buffer* b = w.files().findByPath(absolutePath(str(lp)));
        return b && activateBuffer(b->id) ? 1 : 0;
    }
    case NPPM_DOOPEN:
        return str(lp) && w.openFile(QString::fromUtf8(str(lp))) ? 1 : 0;
    case NPPM_RELOADFILE: { // (withAlert, path)
        Buffer* b = str(lp) ? w.files().findByPath(absolutePath(str(lp))) : nullptr;
        if (!b)
            return 0;
        w.reloadBuffer(b);
        return 1;
    }
    case NPPM_RELOADBUFFERID: {
        Buffer* b = bufferOf(wp);
        if (!b)
            return 0;
        w.reloadBuffer(b);
        return 1;
    }
    case NPPM_SAVECURRENTFILE:
        return cur && w.saveBuffer(cur, false) ? 1 : 0;
    case NPPM_SAVECURRENTFILEAS: { // (asCopy, path)
        if (!cur || !str(lp))
            return 0;
        const std::string path = absolutePath(str(lp));
        const SaveStatus st = w.files().save(*cur, path, wp != 0);
        if (st != SaveStatus::Ok)
            return 0;
        if (wp == 0)
            for (int v = 0; v < 2; ++v)
                if (w.pane(v)->current() == cur->id)
                    w.pane(v)->editor()->send(SCI_SETSAVEPOINT);
        w.refreshBuffer(cur);
        return 1;
    }
    case NPPM_SAVEALLFILES:
        return w.actions()->trigger(cmd::IDM_FILE_SAVEALL) ? 1 : 0;
    case NPPM_MAKECURRENTBUFFERDIRTY:
        if (!cur)
            return 0;
        cur->dirty = true;
        w.refreshBuffer(cur);
        return 1;
    case NPPM_GETBUFFERENCODING: {
        const Buffer* b = bufferOf(wp);
        return b ? static_cast<intptr_t>(b->uniMode) : -1;
    }
    case NPPM_SETBUFFERENCODING: {
        Buffer* b = bufferOf(wp);
        if (!b || lp < 0 || lp > static_cast<intptr_t>(UniMode::Utf16LENoBom))
            return 0;
        b->uniMode = static_cast<UniMode>(lp);
        b->encoding = -1;
        w.refreshBuffer(b);
        return 1;
    }
    case NPPM_GETBUFFERFORMAT: {
        const Buffer* b = bufferOf(wp);
        return b ? static_cast<intptr_t>(b->eol) : -1;
    }
    case NPPM_SETBUFFERFORMAT: {
        Buffer* b = bufferOf(wp);
        if (!b || lp < 0 || lp > 2)
            return 0;
        b->eol = static_cast<EolType>(lp);
        for (int v = 0; v < 2; ++v)
            if (w.pane(v)->current() == b->id) {
                ScintillaEditBase* e = w.pane(v)->editor();
                const int mode = lp == 0 ? SC_EOL_CRLF : lp == 1 ? SC_EOL_CR : SC_EOL_LF;
                e->send(SCI_SETEOLMODE, static_cast<uptr_t>(mode));
                e->send(SCI_CONVERTEOLS, static_cast<uptr_t>(mode));
            }
        w.refreshBuffer(b);
        return 1;
    }

    // --- languages ---------------------------------------------------------------------------
    case NPPM_GETCURRENTLANGTYPE:
        return setInt(cur ? cur->lang : L_TEXT);
    case NPPM_SETCURRENTLANGTYPE:
        if (!cur || lp < 0 || lp >= kLangCount)
            return 0;
        w.setLanguage(cur, static_cast<LangType>(lp));
        return 1;
    case NPPM_GETBUFFERLANGTYPE: {
        const Buffer* b = bufferOf(wp);
        return b ? static_cast<intptr_t>(b->lang) : -1;
    }
    case NPPM_SETBUFFERLANGTYPE: {
        Buffer* b = bufferOf(wp);
        if (!b || lp < 0 || lp >= kLangCount || lp == L_USER)
            return 0;
        w.setLanguage(b, static_cast<LangType>(lp));
        return 1;
    }
    case NPPM_GETLANGUAGENAME:
    case NPPM_GETLANGUAGEDESC: {
        if (static_cast<intptr_t>(wp) < 0 || static_cast<intptr_t>(wp) >= kLangCount)
            return -1;
        const LangInfo& li = langInfo(static_cast<LangType>(wp));
        const std::string s(msg == NPPM_GETLANGUAGENAME ? li.shortName : li.longName);
        return copyTo(s, reinterpret_cast<char*>(lp), static_cast<size_t>(-1) >> 1);
    }

    // --- RUNCOMMAND_USER: (bufferSize, char* buffer) ---------------------------------------------
    case NPPM_GETFULLCURRENTPATH:
    case NPPM_GETCURRENTDIRECTORY:
    case NPPM_GETFILENAME:
    case NPPM_GETNAMEPART:
    case NPPM_GETEXTPART:
    case NPPM_GETCURRENTWORD:
    case NPPM_GETNPPDIRECTORY:
    case NPPM_GETNPPFULLFILEPATH:
    case NPPM_GETCURRENTLINESTR:
    case NPPM_GETFILENAMEATCURSOR: {
        const RunVariables v = w.runVariables();
        std::string s;
        const std::string& full = v.fullCurrentPath;
        const auto slash = full.find_last_of("/\\");
        const std::string fileName = slash == std::string::npos ? full : full.substr(slash + 1);
        switch (msg) {
        case NPPM_GETFULLCURRENTPATH: s = full; break;
        case NPPM_GETCURRENTDIRECTORY: s = slash == std::string::npos ? std::string() : full.substr(0, slash); break;
        case NPPM_GETFILENAME: s = fileName; break;
        case NPPM_GETNAMEPART: s = fileName.substr(0, fileName.rfind('.')); break;
        case NPPM_GETEXTPART: {
            const auto dot = fileName.rfind('.');
            s = dot == std::string::npos ? std::string() : fileName.substr(dot);
            break;
        }
        case NPPM_GETCURRENTWORD:
        case NPPM_GETFILENAMEATCURSOR: s = v.currentWord; break;
        case NPPM_GETNPPDIRECTORY: s = QCoreApplication::applicationDirPath().toStdString(); break;
        case NPPM_GETNPPFULLFILEPATH: s = v.nppFullFilePath; break;
        case NPPM_GETCURRENTLINESTR: s = v.currentLineStr; break;
        default: break;
        }
        return copyBool(s, wp, lp);
    }
    case NPPM_GETCURRENTLINE:
        return w.runVariables().currentLine;
    case NPPM_GETCURRENTCOLUMN:
        return w.runVariables().currentColumn;

    // --- folders, versions --------------------------------------------------------------------
    case NPPM_GETPLUGINSCONFIGDIR: { // (size, buffer): length; buffer NULL = length only
        const std::string dir = pathAppend(pathAppend(w.paths().userDir(), "plugins"), "config");
        std::error_code ec;
        std::filesystem::create_directories(pathFromUtf8(dir), ec);
        return copyTo(dir, reinterpret_cast<char*>(lp), static_cast<size_t>(wp)) >= 0 || !lp ? static_cast<intptr_t>(dir.size()) : 0;
    }
    case NPPM_GETPLUGINHOMEPATH: {
        const std::string dir = (QCoreApplication::applicationDirPath() + QStringLiteral("/plugins")).toStdString();
        return copyTo(dir, reinterpret_cast<char*>(lp), static_cast<size_t>(wp)) >= 0 || !lp ? static_cast<intptr_t>(dir.size()) : 0;
    }
    case NPPM_GETNPPSETTINGSDIRPATH: {
        const std::string& dir = w.paths().userDir();
        return copyTo(dir, reinterpret_cast<char*>(lp), static_cast<size_t>(wp)) >= 0 || !lp ? static_cast<intptr_t>(dir.size()) : 0;
    }
    case NPPM_GETSETTINGSONCLOUDPATH:
        return 0; // no cloud settings folder
    case NPPM_GETAPPDATAPLUGINSALLOWED:
        return 1; // <settings>/plugins is always searched
    case NPPM_GETNPPVERSION:
        return (8 << 16) | 981; // upstream base version 8.9.8.1 (plugins check features by it)
    case NPPM_GETWINDOWSVERSION:
#if defined(Q_OS_WIN)
        return 14; // WV_WIN10
#else
        return 0; // WV_UNKNOWN
#endif
    case NPPM_GETCURRENTNATIVELANGENCODING:
        return 65001; // UTF-8
    case NPPM_GETNATIVELANGFILENAME:
        return copyTo(w.m_currentLanguageFile, reinterpret_cast<char*>(lp), static_cast<size_t>(wp));
    case NPPM_ISDARKMODEENABLED:
        return w.isDarkMode() ? 1 : 0;
    case NPPM_GETEDITORDEFAULTFOREGROUNDCOLOR:
    case NPPM_GETEDITORDEFAULTBACKGROUNDCOLOR: {
        const Style* s = w.styles().globalStyleByName("Default Style");
        const bool fg = msg == NPPM_GETEDITORDEFAULTFOREGROUNDCOLOR;
        const int rgb = s ? (fg ? s->fg : s->bg) : -1;
        return rgb >= 0 ? Style::scintillaColour(rgb) : (fg ? 0x000000 : 0xFFFFFF);
    }

    // --- menus, commands, shortcuts -------------------------------------------------------------
    case NPPM_MENUCOMMAND:
        return w.actions()->trigger(static_cast<int>(lp)) ? 1 : 0;
    case NPPM_SETMENUITEMCHECK:
        setMenuItemCheck(static_cast<int>(wp), lp != 0);
        return 1;
    case NPPM_ALLOCATECMDID:
        return allocate(m_nextDynamicId, kLastDynamicId);
    case NPPM_ALLOCATEMARKER:
        return allocate(m_nextMarker, kLastMarker);
    case NPPM_ALLOCATEINDICATOR:
        return allocate(m_nextIndicator, kLastIndicator);
    case NPPM_GETBOOKMARKID:
        return kMarkBookmark;
    case NPPM_GETSHORTCUTBYCMDID: { // (cmdId, ShortcutKey*)
        auto* sk = reinterpret_cast<NppShortcut*>(lp);
        if (!sk || !pluginOfCommand(static_cast<int>(wp)))
            return 0;
        *sk = NppShortcut{};
        const QKeySequence seq = w.actions()->action(static_cast<int>(wp))->shortcut();
        if (seq.isEmpty())
            return 0;
        const auto k = fromQtKey(seq[0]);
        if (!k)
            return 0;
        *sk = NppShortcut{k->ctrl, k->alt, k->shift, static_cast<uint8_t>(k->key)};
        return 1;
    }
    case NPPM_MSGTOPLUGIN: { // (const char* destModuleName, NppCommunicationInfo*)
        if (!str(wp))
            return 0;
        const std::string dest = normalizedModuleName(str(wp));
        for (const auto& p : m_plugins)
            if (p->enabled && p->message && normalizedModuleName(p->module) == dest) {
                const npp_plugin_message_fn f = p->message;
                return guarded(*p, "npp_plugin_message", [f, lp] { f(NPPM_MSGTOPLUGIN, 0, lp); }) ? 1 : 0;
            }
        return 0;
    }

    // --- user interface --------------------------------------------------------------------------
    case NPPM_HIDETABBAR: {
        const bool wasHidden = !w.pane(0)->tabBar()->isVisibleTo(w.pane(0));
        for (int v = 0; v < 2; ++v)
            w.pane(v)->tabBar()->setVisible(lp == 0);
        return wasHidden ? 1 : 0;
    }
    case NPPM_ISTABBARHIDDEN:
        return w.pane(0)->tabBar()->isVisibleTo(w.pane(0)) ? 0 : 1;
    case NPPM_HIDEMENU:
        return hideWidget(w.menuBar());
    case NPPM_ISMENUHIDDEN:
        return w.menuBar()->isVisibleTo(&w) ? 0 : 1;
    case NPPM_HIDETOOLBAR:
        return hideWidget(w.mainToolbar());
    case NPPM_ISTOOLBARHIDDEN:
        return w.mainToolbar() && w.mainToolbar()->isVisibleTo(&w) ? 0 : 1;
    case NPPM_HIDESTATUSBAR:
        return hideWidget(w.statusBar());
    case NPPM_ISSTATUSBARHIDDEN:
        return w.statusBar()->isVisibleTo(&w) ? 0 : 1;
    case NPPM_SETSTATUSBAR: { // (field, text)
        if (wp >= w.m_status.size() || !str(lp))
            return 0;
        w.m_status[wp]->setText(QString::fromUtf8(str(lp)));
        return 1;
    }
    case NPPM_ADDSCNMODIFIEDFLAGS:
        for (int v = 0; v < 2; ++v) {
            ScintillaEditBase* e = w.pane(v)->editor();
            e->send(SCI_SETMODEVENTMASK, static_cast<uptr_t>(e->send(SCI_GETMODEVENTMASK) | static_cast<intptr_t>(lp)));
        }
        return 1;
    case NPPM_GETLINENUMBERWIDTHMODE:
        return LINENUMWIDTH_DYNAMIC;
    case NPPM_SETLINENUMBERWIDTHMODE:
        return lp == LINENUMWIDTH_DYNAMIC ? 1 : 0;

    // --- accepted, nothing to do with Qt (Win32 dialog bookkeeping) -------------------------------
    case NPPM_MODELESSDIALOG:
        return lp;
    case NPPM_DARKMODESUBCLASSANDTHEME:
        return 1;
    case NPPM_DECODESCI:
    case NPPM_ENCODESCI:
        return static_cast<intptr_t>(UniMode::Utf8); // the editors always hold UTF-8

    default:
        return 0;
    }
}

} // namespace npp::ui
