// Auto-completion, call tips and auto-insertion wiring (phase 3, P3-07): one core AutoCompletion
// per view, fed by SCN_CHARADDED like upstream Notepad_plus::notify; API files are the upstream
// installer/APIs files deployed as <data>/autoCompletion/<language>.xml.
#include "MainWindow.h"

#include "editor/EditorPane.h"
#include "menu/ActionRegistry.h"
#include "menu/CommandIds.h"

#include "core/editor/AutoCompletion.h"
#include "core/editor/AutoIndent.h"
#include "core/util/PathUtil.h"

#include <Scintilla.h>
#include <ScintillaEditBase.h>

#include <QDir>

namespace npp::ui {

using namespace npp::core;
using namespace cmd;

AutoCompletion* MainWindow::autoCompletion(EditorPane* p)
{
    auto& ac = m_autoCompletion[static_cast<size_t>(p->viewIndex())];
    if (!ac)
        ac = std::make_unique<AutoCompletion>(p->adapter());
    return ac.get();
}

// Upstream AutoCompletion::getApiFileName: UDL name, else the language name (javascript -> the
// embedded one); file names are matched case-insensitively (case sensitive file systems)
void MainWindow::updateAutoCompletionLanguage(EditorPane* p)
{
    const Buffer* b = buffer(p->current());
    if (!b)
        return;
    std::string name = b->lang == L_USER ? b->userLangName : std::string(langInfo(b->lang == L_JAVASCRIPT ? L_JS_EMBEDDED : b->lang).name);
    const QString dir = QString::fromStdString(pathAppend(m_dataDir, "autoCompletion"));
    for (const QString& f : QDir(dir).entryList({QStringLiteral("*.xml")}, QDir::Files))
        if (f.compare(QString::fromStdString(name) + QStringLiteral(".xml"), Qt::CaseInsensitive) == 0) {
            name = f.chopped(4).toStdString();
            break;
        }
    AutoCompletion* ac = autoCompletion(p);
    const GuiConfig& g = m_config.gui;
    ac->setSettings({g.autocAction, g.autocFromLen, g.autocIgnoreNumbers, g.autocBrief, g.autocFuncParams});
    ac->setLanguage(b->lang, dir.toStdString(), name);
}

// SCN_CHARADDED: auto-insertion of matched characters, then auto-completion / call tip
void MainWindow::onCharAdded(EditorPane* p, int ch)
{
    const Buffer* b = buffer(p->current());
    if (!b || m_recording || m_playingMacro) // upstream: not while recording / playing back a macro
        return;
    const GuiConfig& g = m_config.gui;
    if (g.maintainIndent != kAutoIndentNone)
        maintainIndentation(p->adapter(), b->lang, g.maintainIndent, ch);
    if (b->largeFile && !g.largeFileAllowAutoCompletion)
        return; // Buffer::allowAutoCompletion
    MatchedPairConf conf;
    conf.userPairs = g.autoInsertUserPairs;
    conf.parentheses = g.autoInsertParentheses;
    conf.brackets = g.autoInsertBrackets;
    conf.curlyBrackets = g.autoInsertCurlyBrackets;
    conf.quotes = g.autoInsertQuotes;
    conf.doubleQuotes = g.autoInsertDoubleQuotes;
    conf.htmlXmlTag = g.autoInsertHtmlXmlTag;
    AutoCompletion* ac = autoCompletion(p);
    const bool anyPair = !conf.userPairs.empty() || conf.parentheses || conf.brackets || conf.curlyBrackets || conf.quotes
                         || conf.doubleQuotes || conf.htmlXmlTag;
    if (anyPair && p->editor()->send(SCI_GETSELECTIONS) <= 1) // not in column mode
        ac->insertMatchedChars(ch, conf);
    ac->update(ch);
}

void MainWindow::connectAutoCompletion(EditorPane* p)
{
    ScintillaEditBase* e = p->editor();
    connect(e, &ScintillaEditBase::charAdded, this, [this, p](int ch) { onCharAdded(p, ch); });
    connect(e, &ScintillaEditBase::notify, this, [this, p](Scintilla::NotificationData* n) {
        if (static_cast<int>(n->nmhdr.code) == SCN_CALLTIPCLICK)
            autoCompletion(p)->callTipClick(static_cast<size_t>(n->position));
    });
}

void MainWindow::registerCompletionCommands()
{
    for (EditorPane* p : m_panes)
        connectAutoCompletion(p);
    ActionRegistry* r = m_actions;
    auto ac = [this] { return autoCompletion(currentPane()); };
    r->setHandler(IDM_EDIT_AUTOCOMPLETE, {[ac] { ac()->showApiComplete(); }});
    r->setHandler(IDM_EDIT_AUTOCOMPLETE_CURRENTFILE, {[ac] { ac()->showWordComplete(true); }});
    r->setHandler(IDM_EDIT_FUNCCALLTIP, {[ac] { ac()->showFunctionComplete(); }});
    r->setHandler(IDM_EDIT_FUNCCALLTIP_PREVIOUS, {[ac] { ac()->callTipClick(1); }});
    r->setHandler(IDM_EDIT_FUNCCALLTIP_NEXT, {[ac] { ac()->callTipClick(2); }});
    r->setHandler(IDM_EDIT_AUTOCOMPLETE_PATH, {[ac] { ac()->showPathCompletion(); }});
}

} // namespace npp::ui
