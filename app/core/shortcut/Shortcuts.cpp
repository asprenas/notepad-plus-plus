#include "Shortcuts.h"

#include "core/config/XmlHelpers.h"
#include "core/util/PathUtil.h"

#include <pugixml.hpp>

#include <sstream>
#include <utility>

namespace npp::core {

namespace {

constexpr int kSciReplaceSel = 2170; // SCI_REPLACESEL
constexpr int kSciNewLine = 2329;    // SCI_NEWLINE

// Win32 VK code -> Qt key (non letter/digit/function keys)
const std::vector<std::pair<int, Qt::Key>>& specialKeys()
{
    static const std::vector<std::pair<int, Qt::Key>> k = {
        {0x08, Qt::Key_Backspace}, {0x09, Qt::Key_Tab},      {0x0D, Qt::Key_Return},   {0x13, Qt::Key_Pause},
        {0x14, Qt::Key_CapsLock},  {0x1B, Qt::Key_Escape},   {0x20, Qt::Key_Space},    {0x21, Qt::Key_PageUp},
        {0x22, Qt::Key_PageDown},  {0x23, Qt::Key_End},      {0x24, Qt::Key_Home},     {0x25, Qt::Key_Left},
        {0x26, Qt::Key_Up},        {0x27, Qt::Key_Right},    {0x28, Qt::Key_Down},     {0x2C, Qt::Key_Print},
        {0x2D, Qt::Key_Insert},    {0x2E, Qt::Key_Delete},   {0x5D, Qt::Key_Menu},     {0x90, Qt::Key_NumLock},
        {0x91, Qt::Key_ScrollLock},
        {0xBA, Qt::Key_Semicolon}, {0xBB, Qt::Key_Equal},    {0xBC, Qt::Key_Comma},    {0xBD, Qt::Key_Minus},
        {0xBE, Qt::Key_Period},    {0xBF, Qt::Key_Slash},    {0xC0, Qt::Key_QuoteLeft}, {0xDB, Qt::Key_BracketLeft},
        {0xDC, Qt::Key_Backslash}, {0xDD, Qt::Key_BracketRight}, {0xDE, Qt::Key_Apostrophe}, {0xE2, Qt::Key_Less},
    };
    return k;
}

// Numeric keypad VK codes -> Qt key (with KeypadModifier)
const std::vector<std::pair<int, Qt::Key>>& keypadKeys()
{
    static const std::vector<std::pair<int, Qt::Key>> k = {
        {0x60, Qt::Key_0}, {0x61, Qt::Key_1}, {0x62, Qt::Key_2}, {0x63, Qt::Key_3}, {0x64, Qt::Key_4},
        {0x65, Qt::Key_5}, {0x66, Qt::Key_6}, {0x67, Qt::Key_7}, {0x68, Qt::Key_8}, {0x69, Qt::Key_9},
        {0x6A, Qt::Key_Asterisk}, {0x6B, Qt::Key_Plus}, {0x6D, Qt::Key_Minus}, {0x6E, Qt::Key_Period},
        {0x6F, Qt::Key_Slash},
    };
    return k;
}

KeyCombo readCombo(const pugi::xml_node& n)
{
    KeyCombo c;
    c.ctrl = xml::getBool(n, "Ctrl", false);
    c.alt = xml::getBool(n, "Alt", false);
    c.shift = xml::getBool(n, "Shift", false);
    c.key = n.attribute("Key").as_int(0);
    return c;
}

void writeCombo(pugi::xml_node n, const KeyCombo& c)
{
    xml::setBool(n, "Ctrl", c.ctrl);
    xml::setBool(n, "Alt", c.alt);
    xml::setBool(n, "Shift", c.shift);
    xml::setInt(n, "Key", c.key);
}

} // namespace

std::optional<QKeyCombination> toQtKey(const KeyCombo& c)
{
    if (!c.isEnabled())
        return std::nullopt;
    Qt::KeyboardModifiers mods;
    if (c.ctrl) mods |= Qt::ControlModifier;  // Cmd on macOS (Qt maps it)
    if (c.alt) mods |= Qt::AltModifier;
    if (c.shift) mods |= Qt::ShiftModifier;

    const int k = c.key;
    if (k >= 0x41 && k <= 0x5A)
        return QKeyCombination(mods, static_cast<Qt::Key>(Qt::Key_A + (k - 0x41)));
    if (k >= 0x30 && k <= 0x39)
        return QKeyCombination(mods, static_cast<Qt::Key>(Qt::Key_0 + (k - 0x30)));
    if (k >= 0x70 && k <= 0x87)
        return QKeyCombination(mods, static_cast<Qt::Key>(Qt::Key_F1 + (k - 0x70)));
    for (const auto& [vk, qk] : specialKeys())
        if (vk == k)
            return QKeyCombination(mods, qk);
    for (const auto& [vk, qk] : keypadKeys())
        if (vk == k)
            return QKeyCombination(mods | Qt::KeypadModifier, qk);
    return std::nullopt;
}

std::optional<KeyCombo> fromQtKey(QKeyCombination q)
{
    KeyCombo c;
    const Qt::KeyboardModifiers mods = q.keyboardModifiers();
    c.ctrl = mods.testFlag(Qt::ControlModifier);
    c.alt = mods.testFlag(Qt::AltModifier);
    c.shift = mods.testFlag(Qt::ShiftModifier);
    const Qt::Key k = q.key();
    if (mods.testFlag(Qt::KeypadModifier)) {
        for (const auto& [vk, qk] : keypadKeys())
            if (qk == k) { c.key = vk; return c; }
    }
    if (k >= Qt::Key_A && k <= Qt::Key_Z) { c.key = 0x41 + (k - Qt::Key_A); return c; }
    if (k >= Qt::Key_0 && k <= Qt::Key_9) { c.key = 0x30 + (k - Qt::Key_0); return c; }
    if (k >= Qt::Key_F1 && k <= Qt::Key_F24) { c.key = 0x70 + (k - Qt::Key_F1); return c; }
    if (k == Qt::Key_Enter) { c.key = 0x0D; return c; }
    if (k == Qt::Key_Backtab) { c.key = 0x09; c.shift = true; return c; }
    for (const auto& [vk, qk] : specialKeys())
        if (qk == k) { c.key = vk; return c; }
    return std::nullopt;
}

std::string keyComboToString(const KeyCombo& c)
{
    if (!c.isEnabled())
        return {};
    std::string s;
    if (c.ctrl) s += "Ctrl+";
    if (c.alt) s += "Alt+";
    if (c.shift) s += "Shift+";
    const int k = c.key;
    if ((k >= 0x41 && k <= 0x5A) || (k >= 0x30 && k <= 0x39))
        return s + static_cast<char>(k);
    if (k >= 0x70 && k <= 0x87)
        return s + "F" + std::to_string(k - 0x70 + 1);
    static const std::vector<std::pair<int, const char*>> names = {
        {0x08, "Backspace"}, {0x09, "Tab"}, {0x0D, "Enter"}, {0x13, "Pause"}, {0x14, "Caps Lock"},
        {0x1B, "Esc"}, {0x20, "Spacebar"}, {0x21, "Page up"}, {0x22, "Page down"}, {0x23, "End"},
        {0x24, "Home"}, {0x25, "Left"}, {0x26, "Up"}, {0x27, "Right"}, {0x28, "Down"}, {0x2D, "INS"},
        {0x2E, "DEL"}, {0x60, "Numpad 0"}, {0x61, "Numpad 1"}, {0x62, "Numpad 2"}, {0x63, "Numpad 3"},
        {0x64, "Numpad 4"}, {0x65, "Numpad 5"}, {0x66, "Numpad 6"}, {0x67, "Numpad 7"}, {0x68, "Numpad 8"},
        {0x69, "Numpad 9"}, {0x6A, "Num *"}, {0x6B, "Num +"}, {0x6D, "Num -"}, {0x6E, "Num ."}, {0x6F, "Num /"},
        {0xBA, ";"}, {0xBB, "="}, {0xBC, ","}, {0xBD, "-"}, {0xBE, "."}, {0xBF, "/"}, {0xC0, "~"},
        {0xDB, "["}, {0xDC, "\\"}, {0xDD, "]"}, {0xDE, "'"}, {0xE2, "<>"},
    };
    for (const auto& [vk, name] : names)
        if (vk == k)
            return s + name;
    return s + "#" + std::to_string(k);
}

void normalizeMacroSteps(std::vector<MacroStep>& in)
{
    std::vector<MacroStep> out;
    auto prevIsCR = [&out] {
        return !out.empty() && out.back().message == kSciReplaceSel && out.back().sParam == "\r";
    };
    const MacroStep newline{0, kSciNewLine, 0, 0, {}};
    for (auto& step : in) {
        const bool isCR = step.sParam == "\r";
        if (step.message == kSciReplaceSel && !step.sParam.empty()
            && (isCR || step.sParam == "\r\n" || step.sParam == "\n")) {
            if (prevIsCR()) {
                if (isCR)
                    out.back() = newline;
                else
                    out.pop_back();
            }
            out.push_back(isCR ? step : newline);
        } else {
            if (prevIsCR())
                out.back() = newline;
            out.push_back(step);
        }
    }
    if (prevIsCR())
        out.back() = newline;
    in = std::move(out);
}

bool ShortcutsFile::loadFromString(std::string_view text)
{
    *this = ShortcutsFile();
    pugi::xml_document doc;
    if (!doc.load_buffer(text.data(), text.size(), xml::kParseOptions))
        return false;
    const pugi::xml_node root = doc.child("NotepadPlus");
    if (!root)
        return false;

    for (pugi::xml_node n = root.child("InternalCommands").child("Shortcut"); n; n = n.next_sibling("Shortcut")) {
        if (n.attribute("Key").as_int(-1) == -1)
            continue;
        internalCommands.push_back({n.attribute("id").as_int(0), readCombo(n), n.attribute("nth").as_int(0)});
    }
    for (pugi::xml_node n = root.child("Macros").child("Macro"); n; n = n.next_sibling("Macro")) {
        if (n.attribute("Key").as_int(-1) == -1)
            continue;
        MacroShortcut m{n.attribute("name").value(), n.attribute("FolderName").value(), readCombo(n), {}};
        for (pugi::xml_node a = n.child("Action"); a; a = a.next_sibling("Action")) {
            const int type = a.attribute("type").as_int(4);
            if (type > 3)
                continue;
            m.steps.push_back({type, a.attribute("message").as_int(0), a.attribute("wParam").as_llong(0),
                               a.attribute("lParam").as_llong(0), a.attribute("sParam").value()});
        }
        normalizeMacroSteps(m.steps);
        macros.push_back(std::move(m));
    }
    for (pugi::xml_node n = root.child("UserDefinedCommands").child("Command"); n; n = n.next_sibling("Command")) {
        if (n.attribute("Key").as_int(-1) == -1)
            continue;
        userCommands.push_back({n.attribute("name").value(), n.attribute("FolderName").value(), xml::text(n), readCombo(n)});
    }
    for (pugi::xml_node n = root.child("PluginCommands").child("PluginCommand"); n; n = n.next_sibling("PluginCommand"))
        pluginCommands.push_back({n.attribute("moduleName").value(), n.attribute("internalID").as_int(-1), readCombo(n)});
    for (pugi::xml_node n = root.child("ScintillaKeys").child("ScintKey"); n; n = n.next_sibling("ScintKey")) {
        ScintillaKeyShortcut k{n.attribute("ScintID").as_int(0), n.attribute("menuCmdID").as_int(0), {readCombo(n)}};
        for (pugi::xml_node next = n.child("NextKey"); next; next = next.next_sibling("NextKey"))
            k.combos.push_back(readCombo(next));
        scintillaKeys.push_back(std::move(k));
    }
    return true;
}

bool ShortcutsFile::load(std::string_view utf8Path)
{
    const auto content = readFileContent(utf8Path);
    return content && loadFromString(*content);
}

std::string ShortcutsFile::saveToString() const
{
    pugi::xml_document doc;
    pugi::xml_node decl = doc.append_child(pugi::node_declaration);
    decl.append_attribute("version").set_value("1.0");
    decl.append_attribute("encoding").set_value("UTF-8");
    pugi::xml_node root = doc.append_child("NotepadPlus");

    pugi::xml_node cmds = root.append_child("InternalCommands");
    for (const auto& c : internalCommands) {
        pugi::xml_node n = cmds.append_child("Shortcut");
        n.append_attribute("id").set_value(c.id);
        writeCombo(n, c.combo);
        if (c.nth != 0)
            n.append_attribute("nth").set_value(c.nth);
    }
    pugi::xml_node mroot = root.append_child("Macros");
    for (const auto& m : macros) {
        pugi::xml_node n = mroot.append_child("Macro");
        n.append_attribute("name").set_value(m.name.c_str());
        writeCombo(n, m.combo);
        if (!m.folder.empty())
            n.append_attribute("FolderName").set_value(m.folder.c_str());
        for (const auto& s : m.steps) {
            pugi::xml_node a = n.append_child("Action");
            a.append_attribute("type").set_value(s.type);
            a.append_attribute("message").set_value(s.message);
            a.append_attribute("wParam").set_value(s.wParam);
            a.append_attribute("lParam").set_value(s.lParam);
            a.append_attribute("sParam").set_value(s.sParam.c_str());
        }
    }
    pugi::xml_node uroot = root.append_child("UserDefinedCommands");
    for (const auto& u : userCommands) {
        pugi::xml_node n = uroot.append_child("Command");
        n.append_attribute("name").set_value(u.name.c_str());
        writeCombo(n, u.combo);
        n.append_child(pugi::node_pcdata).set_value(u.command.c_str());
        if (!u.folder.empty())
            n.append_attribute("FolderName").set_value(u.folder.c_str());
    }
    pugi::xml_node proot = root.append_child("PluginCommands");
    for (const auto& p : pluginCommands) {
        pugi::xml_node n = proot.append_child("PluginCommand");
        n.append_attribute("moduleName").set_value(p.moduleName.c_str());
        n.append_attribute("internalID").set_value(p.internalId);
        writeCombo(n, p.combo);
    }
    pugi::xml_node sroot = root.append_child("ScintillaKeys");
    for (const auto& k : scintillaKeys) {
        pugi::xml_node n = sroot.append_child("ScintKey");
        n.append_attribute("ScintID").set_value(k.scintId);
        n.append_attribute("menuCmdID").set_value(k.menuCmdId);
        writeCombo(n, k.combos.empty() ? KeyCombo{} : k.combos.front());
        for (size_t i = 1; i < k.combos.size(); ++i)
            writeCombo(n.append_child("NextKey"), k.combos[i]);
    }
    std::ostringstream os;
    doc.save(os, "    ", pugi::format_indent);
    return os.str();
}

bool ShortcutsFile::save(std::string_view utf8Path) const
{
    return writeFileContent(utf8Path, saveToString());
}

} // namespace npp::core
