// Helper for tools/settings-compat/run-with-npp.ps1 (phase 4 criterion: settings compatible with
// the original Notepad++ in both directions). Not a unit test: the script drives the original
// notepad++.exe with a temporary settings folder and calls this tool between the runs.
//
//   settings_compat roundtrip <in config.xml> <out config.xml>
//       load the file written by Notepad++ and save it with the port (NPP -> port); prints the
//       <GUIConfig> fields whose loaded value differs from the port's built-in default.
//   settings_compat mutate <fresh config.xml> <out dir>
//       every <GUIConfig> field set to a non-default value, plus a user shortcut and a macro
//       in shortcuts.xml, saved by the port (port -> NPP).
//   settings_compat check <fresh config.xml> <dir>
//       after Notepad++ loaded and re-saved <dir>: prints every field (and shortcut / macro)
//       whose value Notepad++ did not keep. Exit code = number of differences.
#include "core/config/ConfigFile.h"
#include "core/shortcut/Shortcuts.h"
#include "core/util/PathUtil.h"

#include <QCoreApplication>
#include <QTextStream>

#include <functional>
#include <map>
#include <string>

using namespace npp::core;

namespace {

QTextStream out(stdout);

struct Visitor {
    std::function<void(const char*, bool&)> b;
    std::function<void(const char*, int&)> i;
    std::function<void(const char*, std::string&)> s;
};

void visit(GuiConfig& g, const Visitor& v)
{
#define NPP_BOOL_ATTR(m, node, attr, def) v.b(#m, g.m);
#define NPP_SHOWHIDE_ATTR(m, node, attr, def) v.b(#m, g.m);
#define NPP_INT_ATTR(m, node, attr, def) v.i(#m, g.m);
#define NPP_STR_ATTR(m, node, attr, def) v.s(#m, g.m);
#define NPP_BOOL_TEXT(m, node, def) v.b(#m, g.m);
#define NPP_INT_TEXT(m, node, def) v.i(#m, g.m);
#define NPP_STR_TEXT(m, node, def) v.s(#m, g.m);
#include "core/config/GuiConfigFields.inc"
#undef NPP_BOOL_ATTR
#undef NPP_SHOWHIDE_ATTR
#undef NPP_INT_ATTR
#undef NPP_STR_ATTR
#undef NPP_BOOL_TEXT
#undef NPP_INT_TEXT
#undef NPP_STR_TEXT
    v.b("statusBarShow", g.statusBarShow);
    v.b("menuBarShow", g.menuBarShow);
    v.b("splitterVertical", g.splitterVertical);
}

// darkModeEnabled is flipped to "yes": the <ToolBar> values mirror the dark mode ones (dark* + 1).
// Valid non-default values for enum-like / constrained fields; others: bool flipped, int + 1
// (0 -> 1), string -> a marker.
const std::map<std::string, int> kInts = {
    {"toolbarFluentColor", 1}, {"toolbarFluentCustomColor", 1}, {"inSelectionAutocheckThreshold", 512}, {"tabSize", 6}, {"newDocFormat", 2}, {"newDocEncoding", 1}, {"newDocLang", 2},
    {"newDocCodepage", 1252}, {"printOption", 0}, {"backupAction", 1}, {"autocAction", 2}, {"autocFromLen", 3},
    {"caretWidth", 2}, {"openSaveDir", 1}, {"leftmostDelimiter", '['}, {"rightmostDelimiter", ']'},
    {"multiInstSetting", 2}, {"searchEngineChoice", 1}, {"darkModeColorTone", 2}, {"darkToolBarIconSet", 1},
    {"lightToolBarIconSet", 2}, {"darkTabIconSet", 1}, {"lightTabIconSet", 1}, {"changeHistory", 3},
    {"currentLineIndicator", 2}, {"currentLineFrameWidth", 3}, {"eolMode", 3}, {"npcMode", 2}, {"zoom", 2},
    {"zoom2", -1}, {"maintainIndent", 2}, {"styleUrl", 1}, {"minimizeToTray", 1}, {"autoUpdateMode", 0},
    {"writeTechnologyEngine", 0}, {"largeFileSizeMB", 300}, {"snapshotBackupTiming", 9000},
    {"appX", 40}, {"appY", 50}, {"appWidth", 900}, {"appHeight", 640}, {"distractionFreeDivPart", 5},
};
const std::map<std::string, std::string> kStrings = {
    {"toolbarIconSet", "large"}, {"folderMarkStyle", "circle"}, {"lineWrapMethod", "indent"},
    {"dateTimeFormat", "HH:mm dd/MM/yyyy"}, {"darkThemeName", "Bespin.xml"}, {"lightThemeName", "Zenburn.xml"},
    {"edgeMultiColumnPos", "80 120"}, {"customWordChars", "$@"}, {"printHeaderFontName", "Consolas"},
    {"printFooterFontName", "Consolas"}, {"backupDir", "C:\\npp-compat-backup"}, {"defaultDir", "C:\\npp-compat"},
    {"lastUsedDir", "C:\\npp-compat\\last"}, {"searchEngineCustom", "https://example.org/?q=$(CURRENT_WORD)"},
    {"sessionExt", "npps"}, {"workspaceExt", "nppw"}, {"uriSchemes", "svn:// cvs:// git://"},
};
// written by Notepad++ itself; darkFollowSystem: with it Notepad++ recomputes darkModeEnabled from Windows
const char* const kSkip[] = {"shortcutsXmlHmac", "autoUpdateNextDate", "autoUpdateIntervalDays", "darkFollowSystem"};

bool skipped(const char* name)
{
    for (const char* s : kSkip)
        if (std::string_view(name) == s)
            return true;
    return false;
}

void mutate(GuiConfig& g)
{
    visit(g, {[](const char* n, bool& v) { if (!skipped(n)) v = !v; },
              [](const char* n, int& v) {
                  if (skipped(n))
                      return;
                  const auto it = kInts.find(n);
                  v = it != kInts.end() ? it->second : (v == 0 ? 1 : v + 1);
              },
              [](const char* n, std::string& v) {
                  if (skipped(n))
                      return;
                  const auto it = kStrings.find(n);
                  v = it != kStrings.end() ? it->second : std::string("compat-") + n;
              }});
    // zoomSync is flipped on: Notepad++ (and the port) then save zoom2 = zoom (Notepad_plus.cpp saveGUIParams)
    if (g.zoomSync)
        g.zoom2 = g.zoom;
}

std::map<std::string, std::string> values(GuiConfig g)
{
    std::map<std::string, std::string> m;
    visit(g, {[&m](const char* n, bool& v) { m[n] = v ? "yes" : "no"; },
              [&m](const char* n, int& v) { m[n] = std::to_string(v); },
              [&m](const char* n, std::string& v) { m[n] = v; }});
    return m;
}

ShortcutsFile expectedShortcuts()
{
    ShortcutsFile f;
    f.internalCommands.push_back({41003, {true, true, true, 0x7B}, 0}); // Close = Ctrl+Alt+Shift+F12
    MacroShortcut m;
    m.name = "compat macro";
    m.combo = {true, true, false, 'M'};
    m.steps.push_back({1, 2170, 0, 0, "abc"}); // SCI_REPLACESEL "abc"
    m.steps.push_back({0, 2300, 0, 0, {}});     // SCI_LINEDOWN
    f.macros.push_back(m);
    f.scintillaKeys.push_back({2300, 0, {{true, false, false, 'J'}}}); // SCI_LINEDOWN += Ctrl+J
    return f;
}

int diff(const std::map<std::string, std::string>& want, const std::map<std::string, std::string>& got, const char* what)
{
    int n = 0;
    for (const auto& [k, v] : want)
        if (got.at(k) != v) {
            out << what << " " << QString::fromStdString(k) << ": expected \"" << QString::fromStdString(v)
                << "\" got \"" << QString::fromStdString(got.at(k)) << "\"\n";
            ++n;
        }
    return n;
}

} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    const QStringList a = app.arguments();
    if (a.size() < 4) {
        out << "usage: settings_compat roundtrip|mutate|check <fresh config.xml> <out>\n";
        return 2;
    }
    const std::string mode = a[1].toStdString(), in = a[2].toStdString(), target = a[3].toStdString();
    ConfigFile fresh;
    if (!fresh.load(in)) {
        out << "cannot load " << a[2] << "\n";
        return 2;
    }
    if (mode == "roundtrip") {
        const int n = diff(values(GuiConfig{}), values(fresh.gui), "default-differs");
        out << n << " field(s) of the Notepad++ default config differ from the port defaults\n";
        return fresh.save(target) ? 0 : 2;
    }
    if (mode == "mutate") {
        mutate(fresh.gui); // the rest of the file (history, sessions...) unchanged
        const bool ok = fresh.save(pathAppend(target, "config.xml")) && expectedShortcuts().save(pathAppend(target, "shortcuts.xml"));
        return ok ? 0 : 2;
    }
    if (mode == "check") {
        GuiConfig want = fresh.gui;
        mutate(want);
        ConfigFile got;
        if (!got.load(pathAppend(target, "config.xml"))) {
            out << "cannot load result config.xml\n";
            return 2;
        }
        int n = diff(values(want), values(got.gui), "not-kept");
        ShortcutsFile sc;
        sc.load(pathAppend(target, "shortcuts.xml"));
        const ShortcutsFile e = expectedShortcuts();
        auto same = [](const ShortcutsFile& x, const ShortcutsFile& y) {
            return x.saveToString() == y.saveToString();
        };
        ShortcutsFile normalized = sc; // only the parts written by the port are compared
        normalized.userCommands.clear();
        normalized.pluginCommands.clear();
        if (!same(normalized, e)) {
            out << "shortcuts.xml differs:\n--- expected\n" << QString::fromStdString(e.saveToString()) << "--- got\n"
                << QString::fromStdString(normalized.saveToString());
            ++n;
        }
        out << n << " difference(s)\n";
        return n;
    }
    out << "unknown mode\n";
    return 2;
}
