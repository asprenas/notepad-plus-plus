// Macro menu (phase 3, P3-09): recording (Scintilla SCI_STARTRECORD notifications + macroable menu
// commands + find/replace "saved search" steps), playback, saving to shortcuts.xml <Macros>,
// "Run a Macro Multiple Times". Same step model and file format as upstream, so macros recorded
// in the original Notepad++ replay here.
#include "MainWindow.h"

#include "DialogTranslator.h"

#include "editor/EditorPane.h"
#include "menu/ActionRegistry.h"
#include "menu/CommandIds.h"
#include "search/FindReplaceDialog.h"

#include "core/shortcut/Macro.h"
#include "core/util/PathUtil.h"

#include <Scintilla.h>
#include <ScintillaEditBase.h>

#include <QAction>
#include <QButtonGroup>
#include <QComboBox>
#include <QDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QKeySequenceEdit>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QRadioButton>
#include <QSpinBox>

#include <set>

namespace npp::ui {

using namespace npp::core;
using namespace cmd;
using Action = FindReplaceDialog::Action;

namespace {

QString q(const std::string& s) { return QString::fromStdString(s); }

const std::set<int>& macroableCommands()
{
    static const std::set<int> ids = {
#define NPP_MACROCMD(id) id,
#include "menu/MacroableCommands.inc"
#undef NPP_MACROCMD
    };
    return ids;
}

// FindReplaceDlg_rc.h ids used by saved find / replace macro steps (type 3)
enum : int {
    kFrInit = 1700, kFrExec = 1701, kFrBooleans = 1702, kFindWhat = 1601, kReplaceWith = 1602, kSearchType = 1625,
    kFifDir = 1653, kFifFilters = 1652,
};
enum : int { // IDF_* booleans
    kIdfWholeWord = 1, kIdfMatchCase = 2, kIdfPurge = 4, kIdfMarkLine = 16, kIdfRecursive = 32, kIdfHidden = 64,
    kIdfInSelection = 128, kIdfWrap = 256, kIdfDirection = 512, kIdfDotNewline = 1024,
};
// button ids <-> dialog actions
const std::pair<int, Action> kExecIds[] = {
    {1, Action::FindNext},            {1608, Action::Replace},        {1609, Action::ReplaceAll},
    {1614, Action::Count},            {1615, Action::MarkAll},        {1633, Action::ClearMarks},
    {1636, Action::FindAllOpen},      {1641, Action::FindAllCurrent}, {1635, Action::ReplaceAllOpen},
    {1656, Action::FindInFiles},      {1660, Action::ReplaceInFiles},
};

MacroStep savedInt(int id, long long value)
{
    MacroStep s;
    s.type = kMacroSavedSearch;
    s.message = id;
    s.lParam = value;
    return s;
}

MacroStep savedStr(int id, const std::string& value)
{
    MacroStep s = savedInt(id, 0);
    s.sParam = value;
    return s;
}

} // namespace

QMenu* MainWindow::macroMenu() const
{
    for (QObject* o : m_actions->action(IDM_MACRO_STARTRECORDINGMACRO)->associatedObjects())
        if (auto* m = qobject_cast<QMenu*>(o))
            return m;
    return nullptr;
}

void MainWindow::recordFindAction(int actionValue)
{
    if (!m_recording || m_playingMacro)
        return;
    const auto a = static_cast<Action>(actionValue);
    const auto it = std::find_if(std::begin(kExecIds), std::end(kExecIds), [a](const auto& p) { return p.second == a; });
    if (it == std::end(kExecIds))
        return; // Copy Marked Text is not recorded upstream either
    FindReplaceDialog* d = findDialog();
    const SearchOptions o = d->options();
    // FindReplaceDlg::saveInMacro
    int booleans = (o.wholeWord ? kIdfWholeWord : 0) | (o.matchCase ? kIdfMatchCase : 0) | (o.dotMatchesNewline ? kIdfDotNewline : 0);
    m_macro.push_back(savedInt(kFrInit, 0));
    m_macro.push_back(savedStr(kFindWhat, a == Action::ClearMarks ? std::string() : o.findWhat));
    m_macro.push_back(savedInt(kSearchType, static_cast<int>(o.type)));
    if (a == Action::MarkAll)
        booleans |= (d->purgeBeforeMark() ? kIdfPurge : 0) | (d->bookmarkLine() ? kIdfMarkLine : 0);
    const bool replace = a == Action::Replace || a == Action::ReplaceAll || a == Action::ReplaceAllOpen || a == Action::ReplaceInFiles;
    const bool fif = a == Action::FindInFiles || a == Action::ReplaceInFiles;
    const bool global = a == Action::FindAllOpen || a == Action::ReplaceAllOpen || a == Action::FindAllCurrent;
    if (replace)
        m_macro.push_back(savedStr(kReplaceWith, o.replaceWith));
    if (fif) {
        m_macro.push_back(savedStr(kFifDir, d->directory().toStdString()));
        m_macro.push_back(savedStr(kFifFilters, d->filters().toStdString()));
        booleans |= (d->recursive() ? kIdfRecursive : 0) | (d->includeHidden() ? kIdfHidden : 0);
    } else if (!global) {
        booleans |= (d->inSelection() ? kIdfInSelection : 0) | (o.wrap ? kIdfWrap : 0) | (o.directionDown ? kIdfDirection : 0);
    } else if (a == Action::FindAllCurrent) {
        booleans |= d->inSelection() ? kIdfInSelection : 0;
    }
    if (a == Action::ClearMarks)
        booleans = (d->bookmarkLine() ? kIdfMarkLine : 0) | (d->inSelection() ? kIdfInSelection : 0);
    m_macro.push_back(savedInt(kFrBooleans, booleans));
    m_macro.push_back(savedInt(kFrExec, it->first));
}

void MainWindow::playMacro(const std::vector<MacroStep>& steps)
{
    m_playingMacro = true;
    SearchOptions env;
    std::string dir, filters;
    int booleans = 0;
    ScintillaEditBase* e = currentPane()->editor();
    e->send(SCI_BEGINUNDOACTION);
    for (const MacroStep& s : steps) {
        switch (s.type) {
        case kMacroMenuCommand:
            m_actions->trigger(static_cast<int>(s.wParam));
            break;
        case kMacroSavedSearch: // FindReplaceDlg::execSavedCommand
            switch (s.message) {
            case kFrInit: env = {}; dir.clear(); filters.clear(); booleans = 0; break;
            case kFindWhat: env.findWhat = s.sParam; break;
            case kReplaceWith: env.replaceWith = s.sParam; break;
            case kSearchType: env.type = static_cast<SearchType>(s.lParam); break;
            case kFifDir: dir = s.sParam; break;
            case kFifFilters: filters = s.sParam; break;
            case kFrBooleans: booleans = static_cast<int>(s.lParam); break;
            case kFrExec: {
                env.wholeWord = booleans & kIdfWholeWord;
                env.matchCase = booleans & kIdfMatchCase;
                env.dotMatchesNewline = booleans & kIdfDotNewline;
                env.wrap = booleans & kIdfWrap;
                env.directionDown = booleans & kIdfDirection;
                int exec = static_cast<int>(s.lParam);
                if (exec == 1723 || exec == 1721) { // IDC_FINDNEXT / IDC_FINDPREV of 7.5.5 - 7.8.4 macros
                    env.directionDown = exec == 1723;
                    exec = 1;
                }
                const auto it = std::find_if(std::begin(kExecIds), std::end(kExecIds), [exec](const auto& p) { return p.first == exec; });
                if (it == std::end(kExecIds))
                    break;
                FindReplaceDialog* d = findDialog();
                d->setOptions(env);
                d->setInSelection(booleans & kIdfInSelection);
                d->setMarkOptions(booleans & kIdfMarkLine, booleans & kIdfPurge);
                if (!dir.empty())
                    d->setDirectory(q(dir));
                if (!filters.empty())
                    d->setFilters(q(filters));
                d->setFifOptions(booleans & kIdfRecursive, booleans & kIdfHidden);
                runFindAction(static_cast<int>(it->second));
                break;
            }
            default:
                break;
            }
            break;
        default:
            // (upstream PlayBack also notifies SCN_CHARADDED, which Notepad++ ignores while playing back)
            playScintillaStep(currentPane()->adapter(), s);
            break;
        }
    }
    currentPane()->editor()->send(SCI_ENDUNDOACTION);
    m_playingMacro = false;
}

void MainWindow::rebuildMacroMenu()
{
    QMenu* menu = macroMenu();
    if (!menu)
        return;
    for (QAction* a : m_savedMacroActions)
        delete a;
    m_savedMacroActions.clear();
    QAction* before = m_actions->action(IDM_MACRO_RUNMULTIMACRODLG);
    // upstream: saved macros after "Run a Macro Multiple Times...", behind a separator
    const auto acts = menu->actions();
    const int idx = static_cast<int>(acts.indexOf(before));
    QAction* insertBefore = idx >= 0 && idx + 1 < acts.size() ? acts[idx + 1] : nullptr;
    if (!m_shortcuts.macros.empty()) {
        QAction* sep = new QAction(menu);
        sep->setSeparator(true);
        menu->insertAction(insertBefore, sep);
        m_savedMacroActions.push_back(sep);
    }
    for (size_t i = 0; i < m_shortcuts.macros.size(); ++i) {
        const MacroShortcut& m = m_shortcuts.macros[i];
        auto* a = new QAction(q(m.name), menu);
        if (const auto k = toQtKey(m.combo))
            a->setShortcut(QKeySequence(*k));
        a->setShortcutContext(Qt::WindowShortcut);
        addAction(a);
        connect(a, &QAction::triggered, this, [this, i] {
            if (i < m_shortcuts.macros.size() && shortcutsXmlTrusted()) // upstream shortcuts.xml security check
                playMacro(m_shortcuts.macros[i].steps);
        });
        menu->insertAction(insertBefore, a);
        m_savedMacroActions.push_back(a);
    }
}

void MainWindow::registerMacroCommands()
{
    ActionRegistry* r = m_actions;
    for (EditorPane* p : m_panes)
        connect(p->editor(), &ScintillaEditBase::macroRecord, this, [this](Scintilla::Message m, Scintilla::uptr_t w, Scintilla::sptr_t l) {
            if (m_recording && !m_playingMacro)
                m_macro.push_back(recordScintillaStep(static_cast<int>(m), w, l));
        });
    connect(r, &ActionRegistry::commandTriggered, this, [this](int id) {
        if (!m_recording || m_playingMacro || !macroableCommands().count(id))
            return;
        if (id == IDM_EDIT_INSERT_DATETIME_SHORT || id == IDM_EDIT_INSERT_DATETIME_LONG || id == IDM_EDIT_INSERT_DATETIME_CUSTOMIZED) {
            // the inserted text was recorded as SCI_REPLACESEL + SCI_ADDTEXT: replaced by the command
            const size_t n = m_macro.size();
            if (n >= 2 && m_macro[n - 1].message == SCI_ADDTEXT && m_macro[n - 2].message == SCI_REPLACESEL)
                m_macro.resize(n - 2);
        }
        m_macro.push_back(menuCommandStep(id));
    });

    r->setHandler(IDM_MACRO_STARTRECORDINGMACRO, {[this] {
        m_macro.clear();
        m_recording = true;
        for (EditorPane* p : m_panes)
            p->editor()->send(SCI_STARTRECORD);
    }, [this] { return !m_recording; }});
    r->setHandler(IDM_MACRO_STOPRECORDINGMACRO, {[this] {
        m_recording = false;
        for (EditorPane* p : m_panes)
            p->editor()->send(SCI_STOPRECORD);
        normalizeMacroSteps(m_macro);
    }, [this] { return m_recording; }});
    r->setHandler(IDM_MACRO_PLAYBACKRECORDEDMACRO, {[this] { playMacro(m_macro); }, [this] { return !m_recording && !m_macro.empty(); }});
    r->setHandler(IDM_MACRO_SAVECURRENTMACRO, {[this] {
        QDialog dlg(this);
        dlg.setWindowTitle(tr("Shortcut"));
        auto* name = new QLineEdit;
        auto* key = new QKeySequenceEdit;
        auto* form = new QFormLayout(&dlg);
        auto* nameLabel = new QLabel(tr("Name :"));
        nameLabel->setBuddy(name);
        form->addRow(nameLabel, name);
        form->addRow(tr("Shortcut :"), key);
        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
        form->addRow(buttons);
        setControlId(nameLabel, 5006);
        setControlId(buttons->button(QDialogButtonBox::Ok), 1);
        setControlId(buttons->button(QDialogButtonBox::Cancel), 2);
        translateDialog(&dlg, dialogLanguage(), "ShortcutMapperSubDialg");
        connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
        if (dlg.exec() != QDialog::Accepted || name->text().trimmed().isEmpty())
            return;
        MacroShortcut m;
        m.name = name->text().trimmed().toStdString();
        if (!key->keySequence().isEmpty())
            if (const auto k = fromQtKey(key->keySequence()[0]))
                m.combo = *k;
        m.steps = m_macro;
        saveMacro(m);
    }, [this] { return !m_recording && !m_macro.empty(); }});
    r->setHandler(IDM_MACRO_RUNMULTIMACRODLG, {[this] {
        QDialog dlg(this);
        dlg.setWindowTitle(tr("Run a Macro Multiple Times"));
        auto* which = new QComboBox;
        if (!m_macro.empty())
            which->addItem(tr("Current recorded macro"), -1);
        for (size_t i = 0; i < m_shortcuts.macros.size(); ++i)
            which->addItem(q(m_shortcuts.macros[i].name), static_cast<int>(i));
        auto* times = new QRadioButton(tr("Run"));
        auto* toEof = new QRadioButton(tr("Run until the end of file"));
        times->setChecked(true);
        auto* count = new QSpinBox;
        count->setRange(1, 1000000);
        auto* form = new QFormLayout(&dlg);
        auto* whichLabel = new QLabel(tr("Macro to run"));
        whichLabel->setBuddy(which);
        form->addRow(whichLabel, which);
        auto* timesRow = new QHBoxLayout;
        auto* timesLabel = new QLabel(tr("times"));
        timesRow->addWidget(count);
        timesRow->addWidget(timesLabel);
        form->addRow(times, timesRow);
        form->addRow(toEof);
        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
        buttons->button(QDialogButtonBox::Ok)->setText(tr("Run"));
        form->addRow(buttons);
        for (auto [w, id] : std::initializer_list<std::pair<QWidget*, int>>{
                 {buttons->button(QDialogButtonBox::Ok), 1}, {buttons->button(QDialogButtonBox::Cancel), 2}, {whichLabel, 8006},
                 {times, 8001}, {timesLabel, 8005}, {toEof, 8002}})
            setControlId(w, id);
        translateDialog(&dlg, dialogLanguage(), "MultiMacro");
        connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
        if (which->count() == 0 || dlg.exec() != QDialog::Accepted)
            return;
        const int idx = which->currentData().toInt();
        if (idx >= 0 && !shortcutsXmlTrusted()) // saved macro: upstream shortcuts.xml security check
            return;
        runMacroMultiple(idx < 0 ? m_macro : m_shortcuts.macros[static_cast<size_t>(idx)].steps, toEof->isChecked() ? -1 : count->value());
    }, [this] { return !m_recording && (!m_macro.empty() || !m_shortcuts.macros.empty()); }});
}

void MainWindow::saveMacro(const MacroShortcut& m)
{
    m_shortcuts.macros.push_back(m);
    std::error_code ec;
    fs::create_directories(pathFromUtf8(m_paths.userDir()), ec);
    m_shortcuts.save(m_paths.shortcutsXml());
    recordShortcutsXmlHmac(); // written by Notepad++ itself: trusted
    rebuildMacroMenu();
}

// Upstream Notepad_plus::macroPlayback loop: a number of times, or until the end of the file
// (stops when a pass leaves the caret on the last line or does not move it anymore).
void MainWindow::runMacroMultiple(std::vector<MacroStep> steps, int times)
{
    ScintillaEditBase* e = currentPane()->editor();
    e->send(SCI_BEGINUNDOACTION);
    for (int i = 0; times < 0 || i < times; ++i) {
        const auto before = e->send(SCI_GETCURRENTPOS);
        const auto lenBefore = e->send(SCI_GETLENGTH);
        playMacro(steps);
        if (times < 0) {
            const auto pos = e->send(SCI_GETCURRENTPOS);
            const auto line = e->send(SCI_LINEFROMPOSITION, static_cast<uptr_t>(pos));
            if (line >= e->send(SCI_GETLINECOUNT) - 1 || (pos == before && e->send(SCI_GETLENGTH) == lenBefore))
                break;
        }
    }
    e->send(SCI_ENDUNDOACTION);
}

} // namespace npp::ui
