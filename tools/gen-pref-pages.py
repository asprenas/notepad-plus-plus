"""Generates the Preferences dialog pages of the Qt UI (phase 4, P4-01/02) from upstream
PowerEditor/src/WinControls/Preference/preference.rc + preference_rc.h (and the File Association
page of RegExtDlg.rc): every control with its upstream ID, kind, English text and position in
dialog units. Translations (nativeLang <Dialog><Preference><page><Item id=...>) apply by ID.

Usage (from the repository root):  python -I tools/gen-pref-pages.py
Output: app/ui/prefs/PreferencePages.inc
  NPP_PREF_PAGE(tag, "English title", width, height)          page (nativeLang tag)
  NPP_PREF_CTRL(Kind, id, "text", x, y, w, h, extra)           control, Kind in
      Group Label Check Radio Combo ComboEdit Edit Button List Slider Spin
  NPP_PREF_PAGE_END()
"""
import pathlib
import re

root = pathlib.Path(__file__).resolve().parent.parent
pdir = root / "PowerEditor/src/WinControls/Preference"
out = root / "app/ui/prefs/PreferencePages.inc"

# --- ids --------------------------------------------------------------------------------------
defs = {"IDC_STATIC": "-1", "IDOK": "1", "IDCANCEL": "2"}
for hdr in [pdir / "preference_rc.h", root / "PowerEditor/src/WinControls/StaticDialog/RunDlg/RunDlg_rc.h",
            root / "PowerEditor/src/MISC/RegExt/regExtDlgRc.h"]:
    if not hdr.exists():
        continue
    for line in hdr.read_text(encoding="utf-8", errors="replace").splitlines():
        m = re.match(r"\s*#define\s+(\w+)\s+(.+?)\s*(//.*)?$", line)
        if m:
            defs[m.group(1)] = m.group(2)
values = {}


def value(name, depth=0):
    if name in values:
        return values[name]
    if depth > 20 or name not in defs:
        raise KeyError(name)
    expr = re.sub(r"\b([A-Za-z_]\w*)\b", lambda m: str(value(m.group(1), depth + 1)), defs[name])
    v = int(eval(expr, {"__builtins__": {}}))
    values[name] = v
    return v


def ident(tok):
    tok = tok.strip()
    if re.fullmatch(r"-?\d+", tok):
        return int(tok)
    try:
        return value(tok)
    except KeyError:
        return -1


# --- pages (Preference::run_dlgProc _wVector order) -----------------------------------------
cpp = (pdir / "preferenceDlg.cpp").read_text(encoding="utf-8", errors="replace")
order = re.findall(r'_wVector\.push_back\(DlgInfo\(&_(\w+), L"([^"]+)", L"(\w+)"\)\)', cpp)
member_to_dialog = {}
for m in re.finditer(r"_(\w+)\.create\((IDD_\w+)", cpp):
    member_to_dialog[m.group(1)] = m.group(2)


def parse_rc(text):
    dialogs = {}
    lines = text.splitlines()
    i = 0
    while i < len(lines):
        m = re.match(r"(IDD_\w+)\s+DIALOGEX\s+\S+\s+\S+\s+(\d+),\s*(\d+)", lines[i])
        if not m:
            i += 1
            continue
        name, w, h = m.group(1), int(m.group(2)), int(m.group(3))
        while not lines[i].strip().startswith("BEGIN"):
            i += 1
        i += 1
        stmt, ctrls = "", []
        while not lines[i].strip().startswith("END"):
            s = re.sub(r"\s//.*$", "", lines[i]).strip()
            i += 1
            if not s or s.startswith("//"):
                continue
            stmt += " " + s
            if stmt.rstrip().endswith((",", "|")):
                continue
            ctrls.append(stmt.strip())
            stmt = ""
        dialogs[name] = (w, h, ctrls)
    return dialogs


def split_args(s):
    args, cur, q = [], "", False
    for ch in s:
        if ch == '"':
            q = not q
            cur += ch
        elif ch == "," and not q:
            args.append(cur.strip())
            cur = ""
        else:
            cur += ch
    args.append(cur.strip())
    return args


def unq(s):
    return s[1:-1].replace('""', '\\"') if s.startswith('"') else s


def control(stmt):
    kw, rest = stmt.split(None, 1)
    if kw not in ("CONTROL", "LTEXT", "RTEXT", "CTEXT", "GROUPBOX", "PUSHBUTTON", "DEFPUSHBUTTON", "EDITTEXT", "COMBOBOX", "LISTBOX"):
        return None
    a = split_args(rest)
    style = ""
    if kw == "CONTROL":  # text, id, class, style, x, y, w, h
        text, cid, cls, style = unq(a[0]), ident(a[1]), unq(a[2]), a[3]
        x, y, w, h = (int(v) for v in a[4:8])
        if cls == "Button":
            kind = "Radio" if "RADIO" in style else "Check" if "CHECKBOX" in style else "Button"
        elif cls == "msctls_trackbar32":
            kind = "Slider"
        elif cls == "msctls_updown32":
            kind = "Spin"
        elif cls == "Static":
            kind = "Label"
        elif cls == "ComboBox":
            kind = "Combo"
        elif cls == "Edit":
            kind = "Edit"
        else:
            kind = "Label"
    elif kw in ("LTEXT", "RTEXT", "CTEXT"):
        text, cid = unq(a[0]), ident(a[1])
        x, y, w, h = (int(v) for v in a[2:6])
        kind, style = "Label", kw
    elif kw == "GROUPBOX":
        text, cid = unq(a[0]), ident(a[1])
        x, y, w, h = (int(v) for v in a[2:6])
        kind = "Group"
    elif kw in ("PUSHBUTTON", "DEFPUSHBUTTON"):
        text, cid = unq(a[0]), ident(a[1])
        x, y, w, h = (int(v) for v in a[2:6])
        kind = "Button"
    elif kw == "EDITTEXT":
        text, cid = "", ident(a[0])
        x, y, w, h = (int(v) for v in a[1:5])
        style = ",".join(a[5:])
        kind = "Edit"
    elif kw == "COMBOBOX":
        text, cid = "", ident(a[0])
        x, y, w, h = (int(v) for v in a[1:5])
        style = ",".join(a[5:])
        kind = "ComboEdit" if "CBS_DROPDOWN " in style + " " and "DROPDOWNLIST" not in style else "Combo"
        h = 14  # dropdown height, not the list height
    elif kw == "LISTBOX":
        text, cid = "", ident(a[0])
        x, y, w, h = (int(v) for v in a[1:5])
        kind = "List"
    else:
        return None
    extra = []
    if "WS_DISABLED" in style:
        extra.append("Disabled")
    if "RTEXT" in style or "SS_RIGHT" in style:
        extra.append("Right")
    if "CTEXT" in style or "SS_CENTER" in style:
        extra.append("Center")
    if "ES_NUMBER" in style:
        extra.append("Number")
    if "ES_MULTILINE" in style:
        extra.append("Multiline")
    if "WS_GROUP" in style:
        extra.append("NewGroup")
    if "SS_ETCHEDHORZ" in style or "SS_ETCHEDFRAME" in style:
        extra.append("Line")
    flags = " | ".join("kPref" + e for e in extra) or "0"
    return f'NPP_PREF_CTRL({kind}, {cid}, "{text}", {x}, {y}, {w}, {h}, {flags})'


dialogs = parse_rc((pdir / "preference.rc").read_text(encoding="utf-8", errors="replace"))
reg_rc = root / "PowerEditor/src/MISC/RegExt/regExtDlg.rc"
if reg_rc.exists():
    dialogs.update(parse_rc(reg_rc.read_text(encoding="utf-8", errors="replace")))

lines = ["// GENERATED by tools/gen-pref-pages.py from upstream preference.rc / preference_rc.h. Do not edit."]
count = 0
for member, title, tag in order:
    dlg = member_to_dialog.get(member)
    if dlg not in dialogs:
        print("missing dialog for", member, dlg)
        continue
    w, h, ctrls = dialogs[dlg]
    lines.append(f'NPP_PREF_PAGE({tag}, "{title}", {w}, {h})')
    for stmt in ctrls:
        c = control(stmt)
        if c:
            lines.append(c)
            count += 1
    lines.append("NPP_PREF_PAGE_END()")
out.parent.mkdir(parents=True, exist_ok=True)
out.write_text("\n".join(lines) + "\n", encoding="utf-8", newline="\n")
print(f"{len(order)} pages, {count} controls")
