// Find / Replace / Mark / Find in Files (phase 3, P3-01..04): upstream FindReplaceDlg actions
// (processFindNext, processReplace, processAll, findAllIn, findInFiles, replaceInFiles) on the
// core search engine, results in the Search results panel.
#include "MainWindow.h"

#include "docking/DockManager.h"
#include "editor/EditorPane.h"
#include "menu/ActionRegistry.h"
#include "menu/CommandIds.h"
#include "search/FindReplaceDialog.h"
#include "search/SearchResultsPanel.h"

#include "core/search/Search.h"
#include "core/util/PathUtil.h"

#include <Scintilla.h>
#include <ScintillaEditBase.h>

#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QMessageBox>
#include <QProgressDialog>

namespace npp::ui {

using namespace npp::core;
using namespace cmd;
using Action = FindReplaceDialog::Action;
using Status = FindReplaceDialog::Status;

namespace {

QString q(const std::string& s) { return QString::fromStdString(s); }
constexpr char kPanelSearchResults[] = "npp.searchResults";
constexpr int kFoundStyle = 31; // SCE_UNIVERSAL_FOUND_STYLE
constexpr int kMarkBookmark = 20;

} // namespace

// Runs `f` with the document of `b` in the scratch editor (operations on documents not shown)
template <class F>
auto MainWindow::withDocument(Buffer* b, F&& f)
{
    ScintillaEditBase* e = m_scratchWidget;
    const sptr_t prev = e->send(SCI_GETDOCPOINTER);
    e->send(SCI_ADDREFDOCUMENT, 0, prev);
    e->send(SCI_SETDOCPOINTER, 0, b->document);
    e->send(SCI_SETCODEPAGE, SC_CP_UTF8);
    struct Restore {
        ScintillaEditBase* e;
        sptr_t prev;
        ~Restore()
        {
            e->send(SCI_SETDOCPOINTER, 0, prev);
            e->send(SCI_RELEASEDOCUMENT, 0, prev);
        }
    } restore{e, prev};
    return f(static_cast<const IEditor&>(*m_scratch));
}

FindReplaceDialog* MainWindow::findDialog()
{
    if (!m_findDialog) {
        m_findDialog = new FindReplaceDialog(this);
        m_findDialog->loadHistory(m_config.findHistory);
        connect(m_findDialog, &FindReplaceDialog::actionRequested, this, [this](FindReplaceDialog::Action a) { runFindAction(static_cast<int>(a)); });
    }
    return m_findDialog;
}

SearchResultsPanel* MainWindow::searchResults(bool showIt)
{
    if (!m_searchResults) {
        m_searchResults = new SearchResultsPanel;
        m_searchResults->setNativeLanguage(&m_nativeLang);
        m_searchResults->setOneEntryPerLine(m_config.gui.finderShowOnlyOneEntryPerFoundLine);
        m_searchResults->applyStyles(m_langs, m_styles, m_applyOptions);
        connect(m_searchResults, &SearchResultsPanel::activated, this,
                [this](const QString& file, intptr_t start, intptr_t end, int line, bool lineRelative) {
                    openSearchResult(file, start, end, line, lineRelative);
                });
        m_dock->addPanel({kPanelSearchResults, miscString("find-result-caption", "Search results"), {}, DockArea::Bottom, true, false},
                         m_searchResults);
    }
    if (showIt)
        m_dock->showPanel(kPanelSearchResults, false);
    return m_searchResults;
}

void MainWindow::openSearchResult(const QString& file, intptr_t start, intptr_t end, int line, bool lineRelative)
{
    Buffer* b = nullptr;
    for (const auto& buf : m_files->buffers()) // untitled documents are identified by their name
        if ((buf->isUntitled() ? q(buf->untitledName) : q(buf->path)) == file || (!buf->isUntitled() && QFileInfo(q(buf->path)) == QFileInfo(file)))
            b = buf.get();
    if (!b)
        b = openFile(file);
    if (!b)
        return;
    EditorPane* p = paneShowing(b->id);
    if (!p) {
        showBuffer(b, currentPane());
        p = currentPane();
    }
    p->activate(b->id);
    setActivePane(p);
    ScintillaEditBase* e = p->editor();
    if (lineRelative) {
        const auto base = e->send(SCI_POSITIONFROMLINE, static_cast<uptr_t>(line - 1));
        start += base;
        end += base;
    }
    e->send(SCI_ENSUREVISIBLE, static_cast<uptr_t>(e->send(SCI_LINEFROMPOSITION, static_cast<uptr_t>(start))));
    e->send(SCI_SETSEL, static_cast<uptr_t>(start), end);
    e->send(SCI_SCROLLRANGE, static_cast<uptr_t>(end), start);
}

// Upstream processFindNext: from the selection end (or start when searching up), selects the match
bool MainWindow::findNextInCurrent(const SearchOptions& o, bool showStatus)
{
    if (o.findWhat.empty())
        return false;
    const IEditor& e = currentPane()->adapter();
    ScintillaEditBase* ed = currentPane()->editor();
    const intptr_t from = o.directionDown ? ed->send(SCI_GETSELECTIONEND) : ed->send(SCI_GETSELECTIONSTART);
    const auto m = findNext(e, o, from);
    if (!m) {
        if (showStatus && m_findDialog)
            m_findDialog->setStatus(tr("Find: Can't find the text \"%1\"").arg(q(o.findWhat)), Status::Failed);
        if (!m_findDialog || !m_findDialog->isVisible())
            QApplication::beep();
        return false;
    }
    ed->send(SCI_ENSUREVISIBLE, static_cast<uptr_t>(ed->send(SCI_LINEFROMPOSITION, static_cast<uptr_t>(m->start))));
    if (o.directionDown)
        ed->send(SCI_SETSEL, static_cast<uptr_t>(m->start), m->end);
    else
        ed->send(SCI_SETSEL, static_cast<uptr_t>(m->end), m->start);
    ed->send(SCI_SCROLLRANGE, static_cast<uptr_t>(m->end), m->start);
    if (showStatus && m_findDialog)
        m_findDialog->setStatus(m->wrapped ? (o.directionDown ? tr("Find: Found the 1st occurrence from the top. The end of the document has been reached.")
                                                              : tr("Find: Found the 1st occurrence from the bottom. The beginning of the document has been reached."))
                                           : QString(),
                                m->wrapped ? Status::Warning : Status::Normal);
    return true;
}

// Find All in a set of documents -> results panel. Returns the number of hits.
int MainWindow::findAllToPanel(const SearchOptions& o, const std::vector<Buffer*>& buffers, bool inSelection)
{
    SearchResultsPanel* panel = searchResults(true);
    panel->beginSearch(o.findWhat);
    int total = 0;
    for (Buffer* b : buffers) {
        const std::string name = b->isUntitled() ? b->untitledName : b->path;
        auto collect = [&](const IEditor& e) {
            intptr_t start = 0, end = -1;
            if (inSelection) {
                start = e.send(SCI_GETSELECTIONSTART);
                end = e.send(SCI_GETSELECTIONEND);
            }
            const auto matches = findAll(e, o, start, end);
            if (matches.empty())
                return 0;
            panel->addFile(name);
            const auto totalLines = static_cast<size_t>(e.send(SCI_GETLINECOUNT));
            for (const SearchMatch& m : matches) {
                const auto line = e.send(SCI_LINEFROMPOSITION, static_cast<uptr_t>(m.start));
                const auto ls = e.send(SCI_POSITIONFROMLINE, static_cast<uptr_t>(line));
                const auto le = e.send(SCI_GETLINEENDPOSITION, static_cast<uptr_t>(line));
                const auto endInLine = std::min(m.end, le);
                panel->addHit(name, static_cast<size_t>(line + 1), e.textRange(ls, le), static_cast<size_t>(m.start - ls),
                              static_cast<size_t>(endInLine - ls), {m.start, m.end}, totalLines);
            }
            panel->endFile(static_cast<int>(matches.size()));
            return static_cast<int>(matches.size());
        };
        EditorPane* p = paneShowing(b->id);
        total += (p && p->current() == b->id) ? collect(p->adapter()) : withDocument(b, collect);
    }
    panel->endSearch(total, static_cast<int>(buffers.size()), !inSelection);
    return total;
}

std::vector<Buffer*> MainWindow::openBuffersInTabOrder() const
{
    std::vector<Buffer*> out;
    for (const auto& [id, p] : allTabs())
        if (Buffer* b = buffer(id); b && std::find(out.begin(), out.end(), b) == out.end())
            out.push_back(b);
    return out;
}

void MainWindow::runFindAction(int actionValue)
{
    const auto a = static_cast<Action>(actionValue);
    FindReplaceDialog* d = findDialog();
    const SearchOptions o = d->options();
    if (o.findWhat.empty() && a != Action::ClearMarks && a != Action::CopyMarked)
        return;
    d->addToHistory();
    ScintillaEditBase* ed = currentPane()->editor();
    const IEditor& e = currentPane()->adapter();
    const bool inSel = d->inSelection() && ed->send(SCI_GETSELECTIONSTART) != ed->send(SCI_GETSELECTIONEND);
    const intptr_t selStart = inSel ? ed->send(SCI_GETSELECTIONSTART) : 0, selEnd = inSel ? ed->send(SCI_GETSELECTIONEND) : -1;
    const bool readOnly = ed->send(SCI_GETREADONLY) != 0;

    switch (a) {
    case Action::FindNext:
        findNextInCurrent(o, true);
        break;
    case Action::Count: {
        const int n = static_cast<int>(findAll(e, o, selStart, selEnd).size());
        d->setStatus(tr("Count: %1 match%2.").arg(n).arg(n == 1 ? QString() : QStringLiteral("es")), n ? Status::Normal : Status::Failed);
        break;
    }
    case Action::FindAllCurrent: {
        Buffer* b = currentBuffer();
        const int n = b ? findAllToPanel(o, {b}, inSel) : 0;
        d->setStatus(tr("Find All in Current Document: %1 hit%2").arg(n).arg(n == 1 ? QString() : QStringLiteral("s")), n ? Status::Normal : Status::Failed);
        break;
    }
    case Action::FindAllOpen: {
        const int n = findAllToPanel(o, openBuffersInTabOrder(), false);
        d->setStatus(tr("Find All in All Opened Documents: %1 hit%2").arg(n).arg(n == 1 ? QString() : QStringLiteral("s")), n ? Status::Normal : Status::Failed);
        break;
    }
    case Action::Replace: {
        if (readOnly) {
            d->setStatus(tr("Replace: Cannot replace text. The current document is read only."), Status::Failed);
            break;
        }
        // upstream processReplace: replace the current selection if it is a match, then find the next one
        const intptr_t s = ed->send(SCI_GETSELECTIONSTART), en = ed->send(SCI_GETSELECTIONEND);
        SearchOptions down = o;
        down.directionDown = true;
        down.wrap = false;
        if (s != en) {
            const auto m = findNext(e, down, s, FindNextType::FindNextForReplace);
            if (m && m->start == s && m->end == en) {
                const intptr_t after = replaceMatch(e, o, *m);
                ed->send(SCI_SETSEL, static_cast<uptr_t>(after), after);
                if (!o.directionDown)
                    ed->send(SCI_SETSEL, static_cast<uptr_t>(m->start), m->start);
            }
        }
        if (findNextInCurrent(o, true))
            d->setStatus(tr("Replace: 1 occurrence was replaced. The next occurrence found."));
        else
            d->setStatus(tr("Replace: no more occurrence found."), Status::Failed);
        break;
    }
    case Action::ReplaceAll: {
        if (readOnly) {
            d->setStatus(tr("Replace All: Cannot replace text. The current document is read only."), Status::Failed);
            break;
        }
        const int n = replaceAll(e, o, selStart, selEnd);
        d->setStatus(tr("Replace All: %1 occurrence%2 replaced in %3.").arg(n).arg(n == 1 ? tr(" was") : tr("s were"), inSel ? tr("selected text") : tr("entire file")),
                     n ? Status::Normal : Status::Failed);
        break;
    }
    case Action::ReplaceAllOpen: {
        int total = 0;
        for (Buffer* b : openBuffersInTabOrder()) {
            if (b->isReadOnly())
                continue;
            EditorPane* p = paneShowing(b->id);
            total += (p && p->current() == b->id) ? replaceAll(p->adapter(), o) : withDocument(b, [&o](const IEditor& x) { return replaceAll(x, o); });
        }
        d->setStatus(tr("Replace in Opened Files: %1 occurrence%2 replaced.").arg(total).arg(total == 1 ? tr(" was") : tr("s were")),
                     total ? Status::Normal : Status::Failed);
        break;
    }
    case Action::FindInFiles:
    case Action::ReplaceInFiles: {
        const bool replacing = a == Action::ReplaceInFiles;
        FindInFilesOptions fo;
        fo.directory = QDir::fromNativeSeparators(d->directory()).toStdString();
        fo.filters = d->filters().isEmpty() ? "*.*" : d->filters().toStdString();
        fo.recursive = d->recursive();
        fo.includeHidden = d->includeHidden();
        if (fo.directory.empty() || !pathExists(fo.directory, PathKind::Directory)) {
            d->setStatus(tr("Find in Files: The directory is not valid."), Status::Failed);
            break;
        }
        if (replacing) {
            const auto t = m_nativeLang.messageBox("ReplaceInFiles", "Are you sure you want to replace all occurrences in :\n\"$STR_REPLACE1$\"\nfor file type :\n$STR_REPLACE2$",
                                                   "Are you sure?", 0, fo.directory);
            QString msg = q(t.message);
            msg.replace(QStringLiteral("$STR_REPLACE1$"), q(fo.directory)).replace(QStringLiteral("$STR_REPLACE2$"), q(fo.filters));
            if (QMessageBox::question(this, q(t.title), msg) != QMessageBox::Yes)
                break;
        }
        int n = runFindInFiles(fo, o, replacing);
        if (n >= 0)
            d->setStatus(replacing ? tr("Replace in Files: %1 occurrence%2 replaced.").arg(n).arg(n == 1 ? tr(" was") : tr("s were"))
                                   : tr("Find in Files: %1 hit%2").arg(n).arg(n == 1 ? QString() : QStringLiteral("s")),
                         n ? Status::Normal : Status::Failed);
        else
            d->setStatus(tr("The search was cancelled."), Status::Warning);
        break;
    }
    case Action::MarkAll: {
        if (d->purgeBeforeMark()) {
            ed->send(SCI_SETINDICATORCURRENT, kFoundStyle);
            ed->send(SCI_INDICATORCLEARRANGE, 0, ed->send(SCI_GETLENGTH));
            if (d->bookmarkLine())
                ed->send(SCI_MARKERDELETEALL, kMarkBookmark);
        }
        const auto matches = findAll(e, o, selStart, selEnd);
        ed->send(SCI_SETINDICATORCURRENT, kFoundStyle);
        for (const SearchMatch& m : matches) {
            ed->send(SCI_INDICATORFILLRANGE, static_cast<uptr_t>(m.start), m.end - m.start);
            if (d->bookmarkLine())
                ed->send(SCI_MARKERADD, static_cast<uptr_t>(ed->send(SCI_LINEFROMPOSITION, static_cast<uptr_t>(m.start))), kMarkBookmark);
        }
        const int n = static_cast<int>(matches.size());
        d->setStatus(tr("Mark: %1 match%2.").arg(n).arg(n == 1 ? QString() : QStringLiteral("es")), n ? Status::Normal : Status::Failed);
        break;
    }
    case Action::ClearMarks:
        ed->send(SCI_SETINDICATORCURRENT, kFoundStyle);
        ed->send(SCI_INDICATORCLEARRANGE, 0, ed->send(SCI_GETLENGTH));
        d->setStatus({});
        break;
    case Action::CopyMarked:
        m_actions->trigger(IDM_SEARCH_MARKEDTOCLIP);
        break;
    }
}

// Find / Replace in Files with a cancellable progress window; the event loop keeps running
// between files so the UI does not freeze. Returns the hits / replacements, -1 if cancelled.
int MainWindow::runFindInFiles(const FindInFilesOptions& fo, const SearchOptions& o, bool replacing)
{
    QProgressDialog progress(replacing ? tr("Replace in Files progress...") : tr("Find in Files progress..."), tr("Cancel"), 0, 0, this);
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(500);
    bool cancelled = false;
    QElapsedTimer pump;
    pump.start();
    auto onProgress = [&](size_t index, size_t total, const std::string& file) {
        if (pump.elapsed() > 50 || index == 0) {
            progress.setMaximum(static_cast<int>(total));
            progress.setValue(static_cast<int>(index));
            progress.setLabelText(QDir::toNativeSeparators(q(file)));
            QCoreApplication::processEvents();
            pump.restart();
        }
        cancelled = progress.wasCanceled();
        return !cancelled;
    };

    // listing can be long too (10k files): cancellable through the same dialog
    progress.setLabelText(tr("Collecting files..."));
    QCoreApplication::processEvents();
    const std::vector<std::string> files = listFilesToSearch(fo);
    if (progress.wasCanceled())
        return -1;

    if (replacing) {
        // documents open in the editor are replaced in the editor (unsaved changes kept), others on disk
        std::vector<std::string> onDisk;
        int total = 0;
        for (const auto& f : files) {
            if (Buffer* b = m_files->findByPath(f)) {
                if (!b->isReadOnly()) {
                    EditorPane* p = paneShowing(b->id);
                    total += (p && p->current() == b->id) ? replaceAll(p->adapter(), o) : withDocument(b, [&o](const IEditor& x) { return replaceAll(x, o); });
                }
            } else {
                onDisk.push_back(f);
            }
        }
        for (const auto& f : onDisk)
            watchFile(f, false);
        total += replaceInFiles(*m_files, onDisk, o, onProgress);
        for (const auto& f : onDisk)
            watchFile(f, true);
        return cancelled ? -1 : total;
    }

    const std::vector<LineHit> hits = findInFiles(*m_files, files, o, onProgress);
    SearchResultsPanel* panel = searchResults(true);
    panel->beginSearch(o.findWhat);
    int total = 0;
    std::string currentFile;
    int fileHits = 0;
    for (const LineHit& h : hits) {
        if (h.file != currentFile) {
            if (!currentFile.empty())
                panel->endFile(fileHits);
            currentFile = h.file;
            fileHits = 0;
            panel->addFile(QDir::toNativeSeparators(q(h.file)).toStdString());
        }
        for (const auto& [s, en] : h.ranges) {
            panel->addHit(h.file, h.line, h.text, s, en, {static_cast<intptr_t>(s), static_cast<intptr_t>(en)}, h.line, true);
            ++fileHits;
            ++total;
        }
    }
    if (!currentFile.empty())
        panel->endFile(fileHits);
    panel->endSearch(total, static_cast<int>(files.size()), true);
    return cancelled ? -1 : total;
}

void MainWindow::registerFindCommands()
{
    ActionRegistry* r = m_actions;
    auto selectedOrWord = [this]() -> QString {
        ScintillaEditBase* e = currentPane()->editor();
        auto s = e->send(SCI_GETSELECTIONSTART), en = e->send(SCI_GETSELECTIONEND);
        if (s == en)
            return {};
        const std::string t = currentPane()->adapter().textRange(s, en);
        return t.find_first_of("\r\n") == std::string::npos ? q(t) : QString();
    };
    auto showTab = [this, selectedOrWord](FindReplaceDialog::Tab t) {
        FindReplaceDialog* d = findDialog();
        if (t == FindReplaceDialog::FindInFilesTab && d->directory().isEmpty())
            if (const Buffer* b = currentBuffer(); b && !b->isUntitled())
                d->setDirectory(QFileInfo(q(b->path)).absolutePath());
        d->showTab(t, selectedOrWord());
    };
    r->setHandler(IDM_SEARCH_FIND, {[showTab] { showTab(FindReplaceDialog::FindTab); }});
    r->setHandler(IDM_SEARCH_REPLACE, {[showTab] { showTab(FindReplaceDialog::ReplaceTab); }});
    r->setHandler(IDM_SEARCH_FINDINFILES, {[showTab] { showTab(FindReplaceDialog::FindInFilesTab); }});
    r->setHandler(IDM_SEARCH_MARK, {[showTab] { showTab(FindReplaceDialog::MarkTab); }});
    for (bool next : {true, false})
        r->setHandler(next ? IDM_SEARCH_FINDNEXT : IDM_SEARCH_FINDPREV, {[this, next] {
            SearchOptions o = findDialog()->options();
            o.directionDown = next;
            if (o.findWhat.empty())
                m_actions->trigger(IDM_SEARCH_FIND);
            else
                findNextInCurrent(o, true);
        }});
    for (bool next : {true, false})
        r->setHandler(next ? IDM_SEARCH_SETANDFINDNEXT : IDM_SEARCH_SETANDFINDPREV, {[this, next, selectedOrWord] {
            QString t = selectedOrWord();
            if (t.isEmpty()) { // the word at the caret
                ScintillaEditBase* e = currentPane()->editor();
                const auto c = e->send(SCI_GETCURRENTPOS);
                t = q(currentPane()->adapter().textRange(e->send(SCI_WORDSTARTPOSITION, static_cast<uptr_t>(c), 1), e->send(SCI_WORDENDPOSITION, static_cast<uptr_t>(c), 1)));
            }
            if (t.isEmpty())
                return;
            FindReplaceDialog* d = findDialog();
            SearchOptions o = d->options();
            o.findWhat = t.toStdString();
            d->setOptions(o);
            o.directionDown = next;
            findNextInCurrent(o, false);
        }});
    for (bool next : {true, false}) // volatile: the selection / word, without touching the dialog
        r->setHandler(next ? IDM_SEARCH_VOLATILE_FINDNEXT : IDM_SEARCH_VOLATILE_FINDPREV, {[this, next, selectedOrWord] {
            QString t = selectedOrWord();
            ScintillaEditBase* e = currentPane()->editor();
            if (t.isEmpty()) {
                const auto c = e->send(SCI_GETCURRENTPOS);
                t = q(currentPane()->adapter().textRange(e->send(SCI_WORDSTARTPOSITION, static_cast<uptr_t>(c), 1), e->send(SCI_WORDENDPOSITION, static_cast<uptr_t>(c), 1)));
            }
            if (t.isEmpty())
                return;
            SearchOptions o;
            o.findWhat = t.toStdString();
            o.matchCase = m_config.gui.markAllMatchCase;
            o.wholeWord = m_config.gui.markAllWholeWord;
            o.directionDown = next;
            findNextInCurrent(o, false);
        }});
    r->setHandler(IDM_SEARCH_GOTONEXTFOUND, {[this] { searchResults(false)->gotoNextResult(true); }, [this] { return m_searchResults && m_searchResults->searchCount() > 0; }});
    r->setHandler(IDM_SEARCH_GOTOPREVFOUND, {[this] { searchResults(false)->gotoNextResult(false); }, [this] { return m_searchResults && m_searchResults->searchCount() > 0; }});
    r->setHandler(IDM_FOCUS_ON_FOUND_RESULTS, {[this] {
        searchResults(true)->view()->setFocus();
    }});
}

} // namespace npp::ui
