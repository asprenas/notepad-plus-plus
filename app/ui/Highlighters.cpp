// Highlighting done on every caret / scroll / content change (phase 3, P3-08): brace matching
// (Notepad_plus::braceMatch), smart highlighting (SmartHighlighter), XML/HTML tag matching
// (core XmlTagMatcher), clickable links (Notepad_plus::addHotSpot + core isUrl).
#include "MainWindow.h"

#include "editor/EditorPane.h"

#include "core/editor/UrlDetector.h"
#include "core/editor/XmlTagMatcher.h"
#include "core/search/Search.h"

#include <Scintilla.h>
#include <ScintillaEditBase.h>

#include <QDesktopServices>
#include <QFileInfo>
#include <QUrl>

#include <algorithm>

namespace npp::ui {

using namespace npp::core;

namespace {

constexpr int kSmartStyle = 29;  // SCE_UNIVERSAL_FOUND_STYLE_SMART
constexpr int kUrlIndic = 8;     // URL_INDIC
constexpr int kMaxLineHighlight = 400;

void clearIndicator(ScintillaEditBase* e, int indic)
{
    e->send(SCI_SETINDICATORCURRENT, static_cast<uptr_t>(indic));
    e->send(SCI_INDICATORCLEARRANGE, 0, e->send(SCI_GETLENGTH));
}

void visibleRange(ScintillaEditBase* e, intptr_t& start, intptr_t& end)
{
    const auto first = e->send(SCI_GETFIRSTVISIBLELINE);
    const auto lines = std::min<intptr_t>(e->send(SCI_LINESONSCREEN), kMaxLineHighlight) + 1;
    const auto firstDoc = e->send(SCI_DOCLINEFROMVISIBLE, static_cast<uptr_t>(first));
    const auto lastDoc = e->send(SCI_DOCLINEFROMVISIBLE, static_cast<uptr_t>(first + lines));
    start = e->send(SCI_POSITIONFROMLINE, static_cast<uptr_t>(firstDoc));
    end = e->send(SCI_GETLINEENDPOSITION, static_cast<uptr_t>(lastDoc));
}

} // namespace

void MainWindow::braceMatch(EditorPane* p)
{
    ScintillaEditBase* e = p->editor();
    const auto caret = e->send(SCI_GETCURRENTPOS);
    const auto len = e->send(SCI_GETLENGTH);
    intptr_t atCaret = -1, opposite = -1;
    auto isBrace = [](intptr_t c) { return c > 0 && std::string_view("[](){}").find(static_cast<char>(c)) != std::string_view::npos; };
    if (len > 0 && caret > 0 && isBrace(e->send(SCI_GETCHARAT, static_cast<uptr_t>(caret - 1))))
        atCaret = caret - 1;
    if (len > 0 && atCaret < 0 && isBrace(e->send(SCI_GETCHARAT, static_cast<uptr_t>(caret))))
        atCaret = caret;
    if (atCaret >= 0)
        opposite = e->send(SCI_BRACEMATCH, static_cast<uptr_t>(atCaret), 0);
    if (atCaret != -1 && opposite == -1) {
        e->send(SCI_BRACEBADLIGHT, static_cast<uptr_t>(atCaret));
        e->send(SCI_SETHIGHLIGHTGUIDE, 0);
    } else {
        e->send(SCI_BRACEHIGHLIGHT, static_cast<uptr_t>(atCaret), opposite);
        if (e->send(SCI_GETINDENTATIONGUIDES) != SC_IV_NONE && atCaret != -1) {
            const auto c1 = e->send(SCI_GETCOLUMN, static_cast<uptr_t>(atCaret)), c2 = e->send(SCI_GETCOLUMN, static_cast<uptr_t>(opposite));
            e->send(SCI_SETHIGHLIGHTGUIDE, static_cast<uptr_t>(std::min(c1, c2)));
        }
    }
}

void MainWindow::smartHighlight(EditorPane* p)
{
    ScintillaEditBase* e = p->editor();
    clearIndicator(e, kSmartStyle);
    const GuiConfig& g = m_config.gui;
    EditorPane* other = m_panes[static_cast<size_t>(1 - p->viewIndex())];
    const bool otherView = g.smartHighlightOnAnotherView && other->isVisible();
    if (otherView && other->current() != p->current())
        clearIndicator(other->editor(), kSmartStyle);
    if (!g.smartHighlight || e->send(SCI_GETSELECTIONEMPTY))
        return;
    const bool wordOnly = g.smartHighlightUseFindSettings ? m_config.findHistory.matchWord : g.smartHighlightWholeWord;
    const bool matchCase = g.smartHighlightUseFindSettings ? m_config.findHistory.matchCase : g.smartHighlightMatchCase;
    const auto caret = e->send(SCI_GETCURRENTPOS);
    const auto selStart = e->send(SCI_GETSELECTIONSTART), selEnd = e->send(SCI_GETSELECTIONEND);
    if (wordOnly) { // the selection must be exactly a word
        const auto ws = e->send(SCI_WORDSTARTPOSITION, static_cast<uptr_t>(caret), 1);
        const auto we = e->send(SCI_WORDENDPOSITION, static_cast<uptr_t>(ws), 1);
        if (ws == we || ws != selStart || we != selEnd)
            return;
    } else {
        const auto line = e->send(SCI_LINEFROMPOSITION, static_cast<uptr_t>(caret));
        if (selEnd - selStart > e->send(SCI_LINELENGTH, static_cast<uptr_t>(line)))
            return;
    }
    SearchOptions o;
    o.findWhat = p->adapter().textRange(selStart, selEnd);
    o.matchCase = matchCase;
    o.wholeWord = wordOnly;
    auto highlightIn = [&o](EditorPane* pane) {
        ScintillaEditBase* ed = pane->editor();
        const auto ts = ed->send(SCI_GETTARGETSTART), te = ed->send(SCI_GETTARGETEND);
        const auto flags = ed->send(SCI_GETSEARCHFLAGS);
        intptr_t s = 0, en = 0;
        visibleRange(ed, s, en);
        ed->send(SCI_SETINDICATORCURRENT, kSmartStyle);
        for (const SearchMatch& m : findAll(pane->adapter(), o, s, en))
            ed->send(SCI_INDICATORFILLRANGE, static_cast<uptr_t>(m.start), m.end - m.start);
        ed->send(SCI_SETTARGETRANGE, static_cast<uptr_t>(ts), te);
        ed->send(SCI_SETSEARCHFLAGS, static_cast<uptr_t>(flags));
    };
    highlightIn(p);
    if (otherView)
        highlightIn(other);
}

void MainWindow::addHotSpots(EditorPane* p)
{
    ScintillaEditBase* e = p->editor();
    const int urlAction = m_config.gui.styleUrl; // 0 none, 1 no underline, 2 underline (fg)
    e->send(SCI_INDICSETSTYLE, kUrlIndic, urlAction == 1 ? INDIC_HIDDEN : INDIC_PLAIN);
    e->send(SCI_INDICSETHOVERSTYLE, kUrlIndic, INDIC_EXPLORERLINK);
    e->send(SCI_INDICSETALPHA, kUrlIndic, 70);
    e->send(SCI_INDICSETFLAGS, kUrlIndic, SC_INDICFLAG_VALUEFORE);
    intptr_t start = 0, end = 0;
    visibleRange(e, start, end);
    if (start >= end)
        return;
    e->send(SCI_SETINDICATORCURRENT, kUrlIndic);
    const Buffer* b = buffer(p->current());
    const bool allowed = b && !(b->largeFile && !m_config.gui.largeFileAllowClickableLink);
    if (urlAction == 0 || !allowed) {
        e->send(SCI_INDICATORCLEARRANGE, static_cast<uptr_t>(start), end - start);
        return;
    }
    e->send(SCI_SETINDICATORVALUE, static_cast<uptr_t>(e->send(SCI_STYLEGETFORE, STYLE_DEFAULT)));
    const std::u16string wide = QString::fromStdString(p->adapter().textRange(start, end)).toStdU16String();
    const int wideLen = static_cast<int>(wide.size());
    int startWide = 0;
    intptr_t startEncoded = 0;
    while (startWide < wideLen) {
        int lenWide = 0;
        const bool r = isUrl(wide.data(), wideLen, startWide, &lenWide);
        if (lenWide <= 0)
            break;
        const intptr_t lenEncoded = QString::fromStdU16String(wide.substr(static_cast<size_t>(startWide), static_cast<size_t>(lenWide))).toUtf8().size();
        e->send(r ? SCI_INDICATORFILLRANGE : SCI_INDICATORCLEARRANGE, static_cast<uptr_t>(start + startEncoded), lenEncoded);
        startWide += lenWide;
        startEncoded += lenEncoded;
    }
}

void MainWindow::updateHighlights(EditorPane* p)
{
    if (m_highlighting)
        return;
    m_highlighting = true;
    braceMatch(p);
    smartHighlight(p);
    if (const Buffer* b = buffer(p->current()); b && m_config.gui.tagsMatchHighlight)
        XmlTagMatcher(p->adapter(), b->lang, QFileInfo(QString::fromStdString(b->path)).suffix().compare(QLatin1String("xhtml"), Qt::CaseInsensitive) == 0)
            .tagMatch(m_config.gui.tagAttrHighlight);
    addHotSpots(p);
    m_highlighting = false;
}

void MainWindow::connectHighlighters(EditorPane* p)
{
    ScintillaEditBase* e = p->editor();
    setExtraUrlSchemes(QString::fromStdString(m_config.gui.uriSchemes).toStdU16String());
    connect(e, &ScintillaEditBase::updateUi, this, [this, p](Scintilla::Update u) {
        if (static_cast<int>(u) & (SC_UPDATE_CONTENT | SC_UPDATE_SELECTION | SC_UPDATE_V_SCROLL))
            updateHighlights(p);
    });
    // Double click on a link opens it (NppNotification SCN_DOUBLECLICK)
    connect(e, &ScintillaEditBase::doubleClick, this, [e](Scintilla::Position pos, Scintilla::Position) {
        if (!(e->send(SCI_INDICATORALLONFOR, static_cast<uptr_t>(pos)) & (1 << kUrlIndic)))
            return;
        const auto s = e->send(SCI_INDICATORSTART, kUrlIndic, pos), en = e->send(SCI_INDICATOREND, kUrlIndic, pos);
        if (pos < s || pos > en)
            return;
        e->send(SCI_SETSEL, static_cast<uptr_t>(pos), pos);
        std::string url(static_cast<size_t>(en - s), '\0');
        Sci_TextRangeFull tr{{s, en}, url.data()};
        std::string buf(static_cast<size_t>(en - s) + 1, '\0');
        tr.lpstrText = buf.data();
        e->send(SCI_GETTEXTRANGEFULL, 0, reinterpret_cast<sptr_t>(&tr));
        QDesktopServices::openUrl(QUrl(QString::fromUtf8(buf.c_str())));
    });
}

} // namespace npp::ui
