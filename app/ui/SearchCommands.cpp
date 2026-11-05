// Search / View menu commands that need no dialog (phase 3): mark styles 1-5, jumps between
// marks, styled text to clipboard, bookmarks, braces, change history, folding, hidden lines,
// synchronized scrolling. Ports of the matching Notepad_plus / ScintillaEditView functions.
#include "MainWindow.h"

#include "editor/EditorPane.h"
#include "menu/ActionRegistry.h"
#include "menu/CommandIds.h"

#include "core/search/Search.h"

#include <Scintilla.h>
#include <ScintillaEditBase.h>

#include <QApplication>
#include <QClipboard>
#include <QMessageBox>

#include <algorithm>

namespace npp::ui {

using namespace npp::core;
using namespace cmd;

namespace {

QString q(const std::string& s) { return QString::fromStdString(s); }

// SciLexer.h SCE_UNIVERSAL_* indicators
constexpr int kFoundStyle = 31, kSmartStyle = 29, kIncStyle = 28, kTagMatch = 27, kTagAttr = 26;
constexpr int kMarkExt[5] = {25, 24, 23, 22, 21};
constexpr int kMarkBookmark = 20, kMarkHideBegin = 19, kMarkHideEnd = 18;

} // namespace

// Indicators of upstream ScintillaEditView::init + colours of the stylers.xml widget styles
void MainWindow::applyIndicatorStyles(ScintillaEditBase* e)
{
    const std::pair<int, const char*> indics[] = {
        {kSmartStyle, "Smart Highlighting"}, {kFoundStyle, "Find Mark Style"}, {kIncStyle, "Incremental highlight all"},
        {kTagMatch, "Tags match highlighting"}, {kTagAttr, "Tags attribute"},
        {kMarkExt[0], "Mark Style 1"}, {kMarkExt[1], "Mark Style 2"}, {kMarkExt[2], "Mark Style 3"},
        {kMarkExt[3], "Mark Style 4"}, {kMarkExt[4], "Mark Style 5"},
    };
    for (auto [id, name] : indics) {
        e->send(SCI_INDICSETSTYLE, static_cast<uptr_t>(id), INDIC_ROUNDBOX);
        e->send(SCI_INDICSETALPHA, static_cast<uptr_t>(id), 100);
        e->send(SCI_INDICSETUNDER, static_cast<uptr_t>(id), 1);
        if (const Style* s = m_styles.globalStyleByName(name); s && s->bg >= 0)
            e->send(SCI_INDICSETFORE, static_cast<uptr_t>(id), Style::scintillaColour(s->bg));
    }
    // hidden lines markers (upstream uses images; arrows here)
    e->send(SCI_MARKERDEFINE, kMarkHideBegin, SC_MARK_ARROWDOWN);
    e->send(SCI_MARKERDEFINE, kMarkHideEnd, SC_MARK_SHORTARROW);
    e->send(SCI_SETMARGINMASKN, 1, (1 << kMarkBookmark) | (1 << kMarkHideBegin) | (1 << kMarkHideEnd));
}

void MainWindow::registerSearchMiscCommands()
{
    ActionRegistry* r = m_actions;
    auto sci = [this](unsigned msg, uptr_t w = 0, sptr_t l = 0) { return currentPane()->editor()->send(msg, w, l); };
    auto writable = [sci] { return sci(SCI_GETREADONLY) == 0; };
    auto text = [this](intptr_t s, intptr_t e) { return currentPane()->adapter().textRange(s, e); };

    // --- Style all occurrences / one token / clear
    for (int i = 0; i < 5; ++i) {
        const int style = kMarkExt[i];
        r->setHandler(IDM_SEARCH_MARKALLEXT1 + i * 2, {[this, sci, text, style] { // MARKALLEXTn ids are interleaved
            auto s = sci(SCI_GETSELECTIONSTART), e = sci(SCI_GETSELECTIONEND);
            if (s == e) { // getSelectedTextToWChar(true): expand to the word
                const auto caret = sci(SCI_GETCURRENTPOS);
                s = sci(SCI_WORDSTARTPOSITION, static_cast<uptr_t>(caret), 1);
                e = sci(SCI_WORDENDPOSITION, static_cast<uptr_t>(caret), 1);
            }
            if (s == e)
                return;
            SearchOptions o;
            o.findWhat = text(s, e);
            o.matchCase = m_config.gui.markAllMatchCase;
            o.wholeWord = m_config.gui.markAllWholeWord;
            const IEditor& ed = currentPane()->adapter();
            sci(SCI_SETINDICATORCURRENT, static_cast<uptr_t>(style));
            for (const SearchMatch& m : findAll(ed, o))
                sci(SCI_INDICATORFILLRANGE, static_cast<uptr_t>(m.start), m.end - m.start);
        }});
        r->setHandler(IDM_SEARCH_MARKONEEXT1 + i, {[sci, style] {
            auto s = sci(SCI_GETSELECTIONSTART), e = sci(SCI_GETSELECTIONEND);
            if (s == e) {
                const auto caret = sci(SCI_GETCURRENTPOS);
                s = sci(SCI_WORDSTARTPOSITION, static_cast<uptr_t>(caret), 1);
                e = sci(SCI_WORDENDPOSITION, static_cast<uptr_t>(caret), 1);
            }
            if (e > s) {
                sci(SCI_SETINDICATORCURRENT, static_cast<uptr_t>(style));
                sci(SCI_INDICATORFILLRANGE, static_cast<uptr_t>(s), e - s);
            }
        }});
        r->setHandler(IDM_SEARCH_UNMARKALLEXT1 + i * 2, {[sci, style] {
            sci(SCI_SETINDICATORCURRENT, static_cast<uptr_t>(style));
            sci(SCI_INDICATORCLEARRANGE, 0, sci(SCI_GETLENGTH));
        }});
    }
    r->setHandler(IDM_SEARCH_CLEARALLMARKS, {[sci] {
        for (int style : kMarkExt) {
            sci(SCI_SETINDICATORCURRENT, static_cast<uptr_t>(style));
            sci(SCI_INDICATORCLEARRANGE, 0, sci(SCI_GETLENGTH));
        }
    }});

    // --- Jump up / down (goToNextIndicator / goToPreviousIndicator)
    auto goToIndicator = [sci](int indic, bool next) {
        const auto docLen = sci(SCI_GETLENGTH);
        const auto position = sci(SCI_GETCURRENTPOS);
        bool inIndic = sci(SCI_INDICATORVALUEAT, static_cast<uptr_t>(indic), position) != 0;
        auto posStart = sci(SCI_INDICATORSTART, static_cast<uptr_t>(indic), position);
        auto posEnd = sci(SCI_INDICATOREND, static_cast<uptr_t>(indic), position);
        if (posStart == 0 && posEnd == docLen - 1)
            return;
        if (next) {
            if (posEnd >= docLen) {
                inIndic = sci(SCI_INDICATORVALUEAT, static_cast<uptr_t>(indic), 0) != 0;
                posEnd = sci(SCI_INDICATOREND, static_cast<uptr_t>(indic), 0);
            }
            if (inIndic) {
                posEnd = sci(SCI_INDICATOREND, static_cast<uptr_t>(indic), posEnd);
                if (posEnd >= docLen)
                    posEnd = sci(SCI_INDICATOREND, static_cast<uptr_t>(indic), 0);
            }
            const auto newPos = posEnd;
            posStart = sci(SCI_INDICATORSTART, static_cast<uptr_t>(indic), newPos);
            posEnd = sci(SCI_INDICATOREND, static_cast<uptr_t>(indic), newPos);
        } else {
            if (posStart <= 0) {
                inIndic = sci(SCI_INDICATORVALUEAT, static_cast<uptr_t>(indic), docLen - 1) != 0;
                posStart = sci(SCI_INDICATORSTART, static_cast<uptr_t>(indic), docLen - 1);
            }
            if (inIndic) {
                posStart = sci(SCI_INDICATORSTART, static_cast<uptr_t>(indic), posStart - 1);
                if (posStart <= 0)
                    posStart = sci(SCI_INDICATORSTART, static_cast<uptr_t>(indic), docLen - 1);
            }
            const auto newPos = std::max<intptr_t>(0, posStart - 1);
            posStart = sci(SCI_INDICATORSTART, static_cast<uptr_t>(indic), newPos);
            posEnd = sci(SCI_INDICATOREND, static_cast<uptr_t>(indic), newPos);
        }
        if (sci(SCI_INDICATORVALUEAT, static_cast<uptr_t>(indic), posStart)) {
            sci(SCI_ENSUREVISIBLE, static_cast<uptr_t>(sci(SCI_LINEFROMPOSITION, static_cast<uptr_t>(posEnd))));
            if (next)
                sci(SCI_SETSEL, static_cast<uptr_t>(posStart), posEnd);
            else
                sci(SCI_SETSEL, static_cast<uptr_t>(posEnd), posStart);
            sci(SCI_SCROLLCARET);
        }
    };
    for (int i = 0; i < 6; ++i) {
        const int indic = i < 5 ? kMarkExt[i] : kFoundStyle;
        r->setHandler(i < 5 ? IDM_SEARCH_GONEXTMARKER1 + i : IDM_SEARCH_GONEXTMARKER_DEF, {[goToIndicator, indic] { goToIndicator(indic, true); }});
        r->setHandler(i < 5 ? IDM_SEARCH_GOPREVMARKER1 + i : IDM_SEARCH_GOPREVMARKER_DEF, {[goToIndicator, indic] { goToIndicator(indic, false); }});
    }

    // --- Copy styled text (markedTextToClipboard)
    auto styledToClipboard = [sci, text](std::vector<int> indics, bool doAll) {
        std::vector<std::pair<intptr_t, std::string>> styled;
        bool hasEol = false;
        for (int indic : indics) {
            auto pos = sci(SCI_INDICATOREND, static_cast<uptr_t>(indic), 0);
            if (pos <= 0)
                continue;
            bool atEnd = sci(SCI_INDICATORVALUEAT, static_cast<uptr_t>(indic), 0) != 0;
            intptr_t prev = atEnd ? 0 : pos;
            do {
                if (atEnd) {
                    std::string s = text(prev, pos);
                    hasEol = hasEol || s.find_first_of("\r\n") != std::string::npos;
                    styled.emplace_back(prev, std::move(s));
                }
                atEnd = !atEnd;
                prev = pos;
                pos = sci(SCI_INDICATOREND, static_cast<uptr_t>(indic), pos);
            } while (pos != prev);
        }
        if (styled.empty())
            return;
        if (doAll)
            std::sort(styled.begin(), styled.end());
        const std::string delim = hasEol && styled.size() > 1 ? "\r\n----\r\n" : "\r\n";
        std::string joined;
        for (size_t i = 0; i < styled.size(); ++i)
            joined += (i ? delim : std::string()) + styled[i].second;
        if (styled.size() > 1)
            joined += "\r\n";
        QApplication::clipboard()->setText(q(joined));
    };
    const int styleToClip[5] = {IDM_SEARCH_STYLE1TOCLIP, IDM_SEARCH_STYLE2TOCLIP, IDM_SEARCH_STYLE3TOCLIP, IDM_SEARCH_STYLE4TOCLIP, IDM_SEARCH_STYLE5TOCLIP};
    for (int i = 0; i < 5; ++i)
        r->setHandler(styleToClip[i], {[styledToClipboard, i] { styledToClipboard({kMarkExt[i]}, false); }});
    r->setHandler(IDM_SEARCH_ALLSTYLESTOCLIP, {[styledToClipboard] { styledToClipboard({kMarkExt[0], kMarkExt[1], kMarkExt[2], kMarkExt[3], kMarkExt[4]}, true); }});
    r->setHandler(IDM_SEARCH_MARKEDTOCLIP, {[styledToClipboard] { styledToClipboard({kFoundStyle}, false); }});

    // --- Braces
    auto matchingBrace = [sci](intptr_t& atCaret, intptr_t& opposite) {
        const auto caret = sci(SCI_GETCURRENTPOS);
        const auto len = sci(SCI_GETLENGTH);
        atCaret = opposite = -1;
        auto isBrace = [](intptr_t c) { return c && std::string_view("[](){}").find(static_cast<char>(c)) != std::string_view::npos; };
        if (len > 0 && caret > 0 && isBrace(sci(SCI_GETCHARAT, static_cast<uptr_t>(caret - 1))))
            atCaret = caret - 1; // the character before the caret has priority
        if (len > 0 && atCaret < 0 && isBrace(sci(SCI_GETCHARAT, static_cast<uptr_t>(caret))))
            atCaret = caret;
        if (atCaret >= 0)
            opposite = sci(SCI_BRACEMATCH, static_cast<uptr_t>(atCaret), 0);
    };
    for (bool select : {false, true})
        r->setHandler(select ? IDM_SEARCH_SELECTMATCHINGBRACES : IDM_SEARCH_GOTOMATCHINGBRACE, {[sci, matchingBrace, select] {
            intptr_t a = -1, o = -1;
            matchingBrace(a, o);
            if (o == -1)
                return;
            if (select)
                sci(SCI_SETSEL, static_cast<uptr_t>(std::min(a, o)), std::max(a, o) + 1);
            else
                sci(SCI_GOTOPOS, static_cast<uptr_t>(o));
            sci(SCI_CHOOSECARETX);
        }});

    // --- Bookmarks
    auto present = [sci](intptr_t line) { return (sci(SCI_MARKERGET, static_cast<uptr_t>(line)) & (1 << kMarkBookmark)) != 0; };
    for (bool forward : {true, false})
        r->setHandler(forward ? IDM_SEARCH_NEXT_BOOKMARK : IDM_SEARCH_PREV_BOOKMARK, {[sci, forward] {
            const auto lineno = sci(SCI_LINEFROMPOSITION, static_cast<uptr_t>(sci(SCI_GETCURRENTPOS)));
            const unsigned msg = forward ? SCI_MARKERNEXT : SCI_MARKERPREVIOUS;
            auto next = sci(msg, static_cast<uptr_t>(forward ? lineno + 1 : lineno - 1), 1 << kMarkBookmark);
            if (next < 0)
                next = sci(msg, static_cast<uptr_t>(forward ? 0 : sci(SCI_GETLINECOUNT)), 1 << kMarkBookmark);
            if (next < 0)
                return;
            sci(SCI_ENSUREVISIBLEENFORCEPOLICY, static_cast<uptr_t>(next));
            sci(SCI_GOTOLINE, static_cast<uptr_t>(next));
            sci(SCI_CHOOSECARETX);
        }});
    auto lineWithEol = [sci, text](intptr_t line) {
        const auto b = sci(SCI_POSITIONFROMLINE, static_cast<uptr_t>(line));
        return text(b, b + sci(SCI_LINELENGTH, static_cast<uptr_t>(line)));
    };
    auto deleteLine = [sci](intptr_t line) {
        const auto b = sci(SCI_POSITIONFROMLINE, static_cast<uptr_t>(line));
        sci(SCI_MARKERDELETE, static_cast<uptr_t>(line), kMarkBookmark);
        sci(SCI_SETTARGETRANGE, static_cast<uptr_t>(b), b + sci(SCI_LINELENGTH, static_cast<uptr_t>(line)));
        sci(SCI_REPLACETARGET, 0, reinterpret_cast<sptr_t>(""));
    };
    r->setHandler(IDM_SEARCH_COPYMARKEDLINES, {[sci, present, lineWithEol] {
        std::string all;
        for (intptr_t i = sci(SCI_GETLINECOUNT) - 1; i >= 0; --i)
            if (present(i))
                all = lineWithEol(i) + all;
        QApplication::clipboard()->setText(q(all));
    }});
    r->setHandler(IDM_SEARCH_CUTMARKEDLINES, {[sci, present, lineWithEol, deleteLine] {
        std::string all;
        sci(SCI_BEGINUNDOACTION);
        for (intptr_t i = sci(SCI_GETLINECOUNT) - 1; i >= 0; --i)
            if (present(i)) {
                all = lineWithEol(i) + all;
                deleteLine(i);
            }
        sci(SCI_ENDUNDOACTION);
        QApplication::clipboard()->setText(q(all));
    }, writable});
    for (bool marked : {true, false})
        r->setHandler(marked ? IDM_SEARCH_DELETEMARKEDLINES : IDM_SEARCH_DELETEUNMARKEDLINES, {[sci, present, deleteLine, marked] {
            sci(SCI_BEGINUNDOACTION);
            for (intptr_t i = sci(SCI_GETLINECOUNT) - 1; i >= 0; --i)
                if (present(i) == marked)
                    deleteLine(i);
            sci(SCI_ENDUNDOACTION);
        }, writable});
    r->setHandler(IDM_SEARCH_PASTEMARKEDLINES, {[sci, present] {
        const QByteArray clip = QApplication::clipboard()->text().toUtf8();
        if (clip.isEmpty())
            return;
        sci(SCI_BEGINUNDOACTION);
        for (intptr_t i = sci(SCI_GETLINECOUNT) - 1; i >= 0; --i)
            if (present(i)) {
                sci(SCI_SETTARGETRANGE, static_cast<uptr_t>(sci(SCI_POSITIONFROMLINE, static_cast<uptr_t>(i))), sci(SCI_GETLINEENDPOSITION, static_cast<uptr_t>(i)));
                sci(SCI_REPLACETARGET, static_cast<uptr_t>(clip.size()), reinterpret_cast<sptr_t>(clip.constData()));
            }
        sci(SCI_ENDUNDOACTION);
    }, writable});
    r->setHandler(IDM_SEARCH_INVERSEMARKS, {[sci, present] {
        for (intptr_t i = 0; i < sci(SCI_GETLINECOUNT); ++i)
            present(i) ? sci(SCI_MARKERDELETE, static_cast<uptr_t>(i), kMarkBookmark) : sci(SCI_MARKERADD, static_cast<uptr_t>(i), kMarkBookmark);
    }});

    // --- Change history
    const int historyMask = (1 << SC_MARKNUM_HISTORY_REVERTED_TO_ORIGIN) | (1 << SC_MARKNUM_HISTORY_SAVED)
                            | (1 << SC_MARKNUM_HISTORY_MODIFIED) | (1 << SC_MARKNUM_HISTORY_REVERTED_TO_MODIFIED);
    for (bool next : {true, false})
        r->setHandler(next ? IDM_SEARCH_CHANGED_NEXT : IDM_SEARCH_CHANGED_PREV, {[sci, historyMask, next] {
            intptr_t line = -1;
            intptr_t block = sci(SCI_LINEFROMPOSITION, static_cast<uptr_t>(sci(SCI_GETCURRENTPOS)));
            const intptr_t lastLine = sci(SCI_GETLINECOUNT);
            if (next) {
                const intptr_t current = block;
                for (intptr_t i = current; i < lastLine; ++i)
                    if (sci(SCI_MARKERGET, static_cast<uptr_t>(i)) & historyMask) {
                        if (i != block) {
                            line = i;
                            break;
                        }
                        ++block;
                    }
                if (line == -1)
                    for (intptr_t i = 0; i < current + 1; ++i)
                        if (sci(SCI_MARKERGET, static_cast<uptr_t>(i)) & historyMask) {
                            line = i;
                            break;
                        }
            } else {
                for (;;) {
                    line = sci(SCI_MARKERPREVIOUS, static_cast<uptr_t>(block), historyMask);
                    if (line == -1 || line != block)
                        break;
                    --block;
                }
                if (line == -1)
                    line = sci(SCI_MARKERPREVIOUS, static_cast<uptr_t>(lastLine - 1), historyMask);
            }
            if (line != -1) {
                sci(SCI_ENSUREVISIBLEENFORCEPOLICY, static_cast<uptr_t>(line));
                sci(SCI_GOTOLINE, static_cast<uptr_t>(line));
            }
        }});
    r->setHandler(IDM_SEARCH_CLEAR_CHANGE_HISTORY, {[sci] {
        const auto pos = sci(SCI_GETCURRENTPOS);
        const auto flags = sci(SCI_GETCHANGEHISTORY);
        sci(SCI_EMPTYUNDOBUFFER);
        sci(SCI_SETCHANGEHISTORY, SC_CHANGE_HISTORY_DISABLED);
        sci(SCI_SETCHANGEHISTORY, static_cast<uptr_t>(flags));
        sci(SCI_GOTOPOS, static_cast<uptr_t>(pos));
    }});

    // --- Folding
    auto foldLine = [sci](intptr_t line, bool collapse) {
        if (sci(SCI_GETENDSTYLED) < sci(SCI_GETTEXTLENGTH))
            sci(SCI_COLOURISE, 0, -1);
        intptr_t header = line;
        if (!(sci(SCI_GETFOLDLEVEL, static_cast<uptr_t>(line)) & SC_FOLDLEVELHEADERFLAG)) {
            header = sci(SCI_GETFOLDPARENT, static_cast<uptr_t>(line));
            if (header == -1)
                return;
        }
        if ((sci(SCI_GETFOLDEXPANDED, static_cast<uptr_t>(header)) != 0) == collapse)
            sci(SCI_FOLDLINE, static_cast<uptr_t>(header), collapse ? SC_FOLDACTION_CONTRACT : SC_FOLDACTION_EXPAND);
    };
    for (bool collapse : {true, false})
        r->setHandler(collapse ? IDM_VIEW_FOLD_CURRENT : IDM_VIEW_UNFOLD_CURRENT, {[this, sci, foldLine, collapse] {
            const auto line = sci(SCI_LINEFROMPOSITION, static_cast<uptr_t>(sci(SCI_GETCURRENTPOS)));
            bool mode = collapse;
            if (m_config.gui.enableFoldCmdToggable) { // toggles the current fold state
                intptr_t header = (sci(SCI_GETFOLDLEVEL, static_cast<uptr_t>(line)) & SC_FOLDLEVELHEADERFLAG) ? line : sci(SCI_GETFOLDPARENT, static_cast<uptr_t>(line));
                mode = header == -1 || sci(SCI_GETFOLDEXPANDED, static_cast<uptr_t>(header)) != 0;
            }
            foldLine(line, mode);
        }});
    for (bool collapse : {true, false})
        r->setHandler(collapse ? IDM_VIEW_FOLDALL : IDM_VIEW_UNFOLDALL, {[sci, collapse] {
            sci(SCI_FOLDALL, (collapse ? SC_FOLDACTION_CONTRACT : SC_FOLDACTION_EXPAND) | SC_FOLDACTION_CONTRACT_EVERY_LEVEL);
            if (!collapse)
                sci(SCI_SCROLLCARET);
        }});
    for (int level = 0; level < 8; ++level)
        for (bool collapse : {true, false})
            r->setHandler((collapse ? IDM_VIEW_FOLD : IDM_VIEW_UNFOLD) + 1 + level, {[sci, foldLine, level, collapse] {
                if (sci(SCI_GETENDSTYLED) < sci(SCI_GETTEXTLENGTH))
                    sci(SCI_COLOURISE, 0, -1);
                const auto maxLine = sci(SCI_GETLINECOUNT);
                for (intptr_t line = 0; line < maxLine; ++line) {
                    const auto lvl = sci(SCI_GETFOLDLEVEL, static_cast<uptr_t>(line));
                    if ((lvl & SC_FOLDLEVELHEADERFLAG) && ((lvl - SC_FOLDLEVELBASE) & SC_FOLDLEVELNUMBERMASK) == level)
                        foldLine(line, collapse);
                }
            }});

    // --- Hide lines (markers on the lines around the hidden block; a click on the marker shows them)
    r->setHandler(IDM_VIEW_HIDELINES, {[sci] {
        auto startLine = sci(SCI_LINEFROMPOSITION, static_cast<uptr_t>(sci(SCI_GETSELECTIONSTART)));
        auto endLine = sci(SCI_LINEFROMPOSITION, static_cast<uptr_t>(sci(SCI_GETSELECTIONEND)));
        const auto nbLines = sci(SCI_GETLINECOUNT);
        if (nbLines < 3)
            return;
        if (!startLine)
            ++startLine; // the first and last lines cannot be hidden
        if (endLine == nbLines - 1)
            --endLine;
        if (startLine > endLine)
            return;
        sci(SCI_MARKERADD, static_cast<uptr_t>(startLine - 1), kMarkHideBegin);
        sci(SCI_MARKERADD, static_cast<uptr_t>(endLine + 1), kMarkHideEnd);
        sci(SCI_HIDELINES, static_cast<uptr_t>(startLine), endLine);
    }});

    // --- Synchronized scrolling
    r->setHandler(IDM_VIEW_SYNSCROLLV, {[this] {
        m_syncScrollV = !m_syncScrollV;
        if (m_syncScrollV)
            m_syncLineOffset = m_panes[0]->editor()->send(SCI_GETFIRSTVISIBLELINE) - m_panes[1]->editor()->send(SCI_GETFIRSTVISIBLELINE);
    }, {}, [this] { return m_syncScrollV; }});
    r->setHandler(IDM_VIEW_SYNSCROLLH, {[this] {
        m_syncScrollH = !m_syncScrollH;
        if (m_syncScrollH)
            m_syncXOffset = m_panes[0]->editor()->send(SCI_GETXOFFSET) - m_panes[1]->editor()->send(SCI_GETXOFFSET);
    }, {}, [this] { return m_syncScrollH; }});
    for (EditorPane* p : m_panes)
        connect(p->editor(), &ScintillaEditBase::updateUi, this, [this, p](Scintilla::Update u) {
            if (!(static_cast<int>(u) & (SC_UPDATE_V_SCROLL | SC_UPDATE_H_SCROLL)) || m_syncing || m_panes[1]->isHidden())
                return;
            EditorPane* other = m_panes[static_cast<size_t>(1 - p->viewIndex())];
            const int sign = p->viewIndex() == 0 ? 1 : -1;
            m_syncing = true;
            if (m_syncScrollV)
                other->editor()->send(SCI_SETFIRSTVISIBLELINE, static_cast<uptr_t>(std::max<intptr_t>(0, p->editor()->send(SCI_GETFIRSTVISIBLELINE) - sign * m_syncLineOffset)));
            if (m_syncScrollH)
                other->editor()->send(SCI_SETXOFFSET, static_cast<uptr_t>(std::max<intptr_t>(0, p->editor()->send(SCI_GETXOFFSET) - sign * m_syncXOffset)));
            m_syncing = false;
        });

    // --- Column mode tip
    r->setHandler(IDM_EDIT_COLUMNMODETIP, {[this] {
        const auto t = m_nativeLang.messageBox("ColumnModeTip",
            "There are 3 ways to switch to column-select mode:\n\n"
            "1. (Keyboard and Mouse)  Hold Alt while left-click dragging\n\n"
            "2. (Keyboard only)  Hold Alt+Shift while using arrow keys\n\n"
            "3. (Keyboard or Mouse)\n"
            "      Put caret at desired start of column block position, then\n"
            "       execute \"Begin/End Select in Column Mode\" command;\n"
            "      Move caret to desired end of column block position, then\n"
            "       execute \"Begin/End Select in Column Mode\" command again\n",
            "Column Mode Tip");
        QMessageBox::information(this, q(t.title), q(t.message));
    }});
}

} // namespace npp::ui
