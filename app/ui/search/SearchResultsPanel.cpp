#include "SearchResultsPanel.h"

#include "editor/EditorAdapter.h"

#include "core/editor/EditorLanguage.h"
#include "core/i18n/NativeLanguage.h"
#include "core/lang/StyleRegistry.h"

#include <ScintillaEditBase.h>

#include <QKeyEvent>
#include <QVBoxLayout>

#include <cstdio>

namespace npp::ui {

using namespace npp::core;

namespace {

int digits(size_t n)
{
    int d = 1;
    while (n >= 10) {
        n /= 10;
        ++d;
    }
    return d;
}

std::string commafy(long long n)
{
    std::string s = std::to_string(n < 0 ? -n : n);
    for (int i = static_cast<int>(s.size()) - 3; i > 0; i -= 3)
        s.insert(static_cast<size_t>(i), ",");
    return n < 0 ? "-" + s : s;
}

void replaceAll(std::string& s, std::string_view from, const std::string& to)
{
    for (size_t p = s.find(from); p != std::string::npos; p = s.find(from, p + to.size()))
        s.replace(p, from.size(), to);
}

} // namespace

SearchResultsPanel::SearchResultsPanel(QWidget* parent) : QWidget(parent), m_view(new ScintillaEditBase(this))
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_view);
    m_view->send(SCI_SETCODEPAGE, SC_CP_UTF8);
    m_view->send(SCI_SETREADONLY, 1);
    m_view->send(SCI_SETUNDOCOLLECTION, 0);
    m_view->send(SCI_SETCARETLINEVISIBLE, 1);
    m_view->send(SCI_SETCARETLINEVISIBLEALWAYS, 1);
    m_view->send(SCI_SETMARGINWIDTHN, 0, 0);
    m_view->send(SCI_SETMARGINWIDTHN, 1, 0);
    // folding margin (one fold per search / per file)
    m_view->send(SCI_SETMARGINTYPEN, 2, SC_MARGIN_SYMBOL);
    m_view->send(SCI_SETMARGINMASKN, 2, SC_MASK_FOLDERS);
    m_view->send(SCI_SETMARGINWIDTHN, 2, 14);
    m_view->send(SCI_SETMARGINSENSITIVEN, 2, 1);
    m_view->send(SCI_SETAUTOMATICFOLD, SC_AUTOMATICFOLD_CLICK);
    const std::pair<int, int> markers[] = {
        {SC_MARKNUM_FOLDEROPEN, SC_MARK_BOXMINUS}, {SC_MARKNUM_FOLDER, SC_MARK_BOXPLUS}, {SC_MARKNUM_FOLDERSUB, SC_MARK_VLINE},
        {SC_MARKNUM_FOLDERTAIL, SC_MARK_LCORNER}, {SC_MARKNUM_FOLDEREND, SC_MARK_BOXPLUSCONNECTED},
        {SC_MARKNUM_FOLDEROPENMID, SC_MARK_BOXMINUSCONNECTED}, {SC_MARKNUM_FOLDERMIDTAIL, SC_MARK_TCORNER}};
    for (auto [n, s] : markers) {
        m_view->send(SCI_MARKERDEFINE, static_cast<uptr_t>(n), s);
        m_view->send(SCI_MARKERSETFORE, static_cast<uptr_t>(n), 0xFFFFFF);
        m_view->send(SCI_MARKERSETBACK, static_cast<uptr_t>(n), 0x808080);
    }
    m_view->installEventFilter(this);
    connect(m_view, &ScintillaEditBase::doubleClick, this, [this](Scintilla::Position, Scintilla::Position line) {
        activateLine(line);
    });
    refreshMarkings();
}

SearchResultsPanel::~SearchResultsPanel() = default;

void SearchResultsPanel::applyStyles(const LanguageRegistry& langs, const StyleRegistry& styles, const LanguageApplyOptions& o)
{
    EditorAdapter a(m_view);
    applyLanguage(a, L_SEARCHRESULT, langs, styles, o);
    m_view->send(SCI_SETPROPERTY, reinterpret_cast<uptr_t>("fold"), reinterpret_cast<sptr_t>("1"));
    refreshMarkings();
    if (const LexerStyler* ls = styles.lexer("searchResult"))
        for (const Style& s : ls->styles)
            if (s.styleId == 6 && s.bg >= 0) // "Current line background colour"
                m_view->send(SCI_SETCARETLINEBACK, static_cast<uptr_t>(Style::scintillaColour(s.bg)));
}

std::string SearchResultsPanel::str(const char* id, const char* fallback) const
{
    return m_nativeLang ? m_nativeLang->localizedString(id, fallback) : std::string(fallback);
}

void SearchResultsPanel::refreshMarkings()
{
    m_markingsStruct._length = static_cast<intptr_t>(m_markings.size());
    m_markingsStruct._markings = m_markings.empty() ? nullptr : m_markings.data();
    char addr[32];
    std::snprintf(addr, sizeof addr, "%p", static_cast<void*>(&m_markingsStruct));
    m_view->send(SCI_SETPROPERTY, reinterpret_cast<uptr_t>("@MarkingsStruct"), reinterpret_cast<sptr_t>(addr));
}

void SearchResultsPanel::addLine(const std::string& text, Hit hit, SearchResultMarkingLine marking)
{
    m_text += text;
    m_newHits.push_back(std::move(hit));
    m_newMarkings.push_back(std::move(marking));
}

void SearchResultsPanel::beginSearch(const std::string& what)
{
    m_text.clear();
    m_newHits.clear();
    m_newMarkings.clear();
    m_filesWithHits = 0;
    std::string oneLine = what;
    std::erase(oneLine, '\r');
    std::erase(oneLine, '\n');
    std::string title = str("find-result-title", "");
    if (title.empty())
        title = "Search";
    addLine(title + " \"" + oneLine + "\" \r\n", {}, {});
    m_previousLine = -1;
}

void SearchResultsPanel::addFile(const std::string& displayName)
{
    m_fileHeaderOffset = m_text.size() + 2 + displayName.size();
    addLine("  " + displayName + "\r\n", {}, {});
    m_previousLine = -1;
}

void SearchResultsPanel::addHit(const std::string& file, size_t line, const std::string& lineText, size_t start, size_t end,
                                std::pair<intptr_t, intptr_t> docRange, size_t totalLines, bool lineRelative)
{
    // Finder::foundLine: "\tLine " + right aligned number + ": " + text
    std::string prefix = str("find-result-line-prefix", "");
    if (prefix.empty())
        prefix = "Line";
    std::string header = "\t" + prefix + " ";
    header.append(static_cast<size_t>(digits(totalLines) - digits(line)), ' ');
    header += std::to_string(line) + ": ";
    const auto segStart = static_cast<intptr_t>(header.size() + start), segEnd = static_cast<intptr_t>(header.size() + end);

    const bool repeated = m_oneEntryPerLine && m_previousLine == static_cast<intptr_t>(line);
    m_previousLine = static_cast<intptr_t>(line);
    if (repeated && !m_newHits.empty()) { // one entry per found line: add the segment to the previous line
        m_newMarkings.back()._segmentPostions.emplace_back(segStart, segEnd);
        m_newHits.back().ranges.push_back(docRange);
        return;
    }
    std::string text = header + lineText;
    if (text.size() >= SC_SEARCHRESULT_LINEBUFFERMAXLENGTH) { // long lines are cut like upstream
        size_t cut = SC_SEARCHRESULT_LINEBUFFERMAXLENGTH - 7;
        while (cut > 0 && (static_cast<unsigned char>(text[cut]) & 0xC0) == 0x80)
            --cut;
        text = text.substr(0, cut) + " ...";
    }
    SearchResultMarkingLine m;
    m._segmentPostions.emplace_back(segStart, segEnd);
    addLine(text + "\r\n", {file, line, {docRange}}, std::move(m));
}

void SearchResultsPanel::endFile(int hits)
{
    std::string text = str("find-result-hits", "");
    if (text.empty())
        text = hits == 1 ? "(1 hit)" : "(" + std::to_string(hits) + " hits)";
    else
        replaceAll(text, "$INT_REPLACE$", std::to_string(hits));
    m_text.insert(m_fileHeaderOffset, " " + text);
    ++m_filesWithHits;
}

void SearchResultsPanel::endSearch(int hits, int filesSearched, bool entire)
{
    std::string text = str(entire ? "find-result-title-info" : "find-result-title-info-selections", "");
    if (text.empty()) {
        std::string where = entire ? "file" : "selection";
        if (m_filesWithHits != 1)
            where += "s";
        text = "(" + commafy(hits) + (hits == 1 ? " hit" : " hits") + " in " + commafy(m_filesWithHits) + " " + where + " of "
               + commafy(filesSearched) + " searched)";
    } else {
        replaceAll(text, "$INT_REPLACE1$", commafy(hits));
        replaceAll(text, "$INT_REPLACE2$", commafy(m_filesWithHits));
        replaceAll(text, "$INT_REPLACE3$", commafy(filesSearched));
    }
    const size_t eol = m_text.find(" \r\n"); // end of the "Search "..." " line
    m_text.insert(eol + 1, text);

    // insert the new search at the top
    m_view->send(SCI_SETREADONLY, 0);
    m_view->send(SCI_INSERTTEXT, 0, reinterpret_cast<sptr_t>(m_text.c_str()));
    m_view->send(SCI_SETREADONLY, 1);
    m_hits.insert(m_hits.begin(), m_newHits.begin(), m_newHits.end());
    m_markings.insert(m_markings.begin(), m_newMarkings.begin(), m_newMarkings.end());
    refreshMarkings();
    m_view->send(SCI_COLOURISE, 0, -1);
    const auto newLines = static_cast<intptr_t>(m_newHits.size());
    if (m_searches > 0) // fold the previous search
        m_view->send(SCI_FOLDLINE, static_cast<uptr_t>(newLines), SC_FOLDACTION_CONTRACT);
    m_view->send(SCI_GOTOPOS, 0);
    ++m_searches;
    m_text.clear();
    m_newHits.clear();
    m_newMarkings.clear();
}

void SearchResultsPanel::clear()
{
    m_view->send(SCI_SETREADONLY, 0);
    m_view->send(SCI_CLEARALL);
    m_view->send(SCI_SETREADONLY, 1);
    m_hits.clear();
    m_markings.clear();
    m_searches = 0;
    refreshMarkings();
}

const SearchResultsPanel::Hit* SearchResultsPanel::hitAt(intptr_t line) const
{
    if (line < 0 || static_cast<size_t>(line) >= m_hits.size() || m_hits[static_cast<size_t>(line)].ranges.empty())
        return nullptr;
    return &m_hits[static_cast<size_t>(line)];
}

void SearchResultsPanel::activateLine(intptr_t line)
{
    const Hit* h = hitAt(line);
    if (!h) { // header: toggle its fold
        if (m_view->send(SCI_GETFOLDLEVEL, static_cast<uptr_t>(line)) & SC_FOLDLEVELHEADERFLAG)
            m_view->send(SCI_TOGGLEFOLD, static_cast<uptr_t>(line));
        return;
    }
    // with several hits on the line, the one under the caret (else the first)
    const auto& r = h->ranges.front();
    emit activated(QString::fromStdString(h->file), r.first, r.second, static_cast<int>(h->line), h->lineRelative);
}

void SearchResultsPanel::gotoNextResult(bool next)
{
    const auto count = static_cast<intptr_t>(m_hits.size());
    if (count == 0)
        return;
    intptr_t line = m_view->send(SCI_LINEFROMPOSITION, static_cast<uptr_t>(m_view->send(SCI_GETCURRENTPOS)));
    for (intptr_t i = 0; i < count; ++i) {
        line = (line + (next ? 1 : count - 1)) % count;
        if (hitAt(line)) {
            m_view->send(SCI_ENSUREVISIBLE, static_cast<uptr_t>(line));
            m_view->send(SCI_GOTOLINE, static_cast<uptr_t>(line));
            activateLine(line);
            return;
        }
    }
}

bool SearchResultsPanel::eventFilter(QObject* o, QEvent* e)
{
    if (o == m_view && e->type() == QEvent::KeyPress) {
        auto* k = static_cast<QKeyEvent*>(e);
        if (k->key() == Qt::Key_Return || k->key() == Qt::Key_Enter) {
            activateLine(m_view->send(SCI_LINEFROMPOSITION, static_cast<uptr_t>(m_view->send(SCI_GETCURRENTPOS))));
            return true;
        }
    }
    return QWidget::eventFilter(o, e);
}

} // namespace npp::ui
