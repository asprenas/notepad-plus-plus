// Search results panel (phase 3, P3-02): port of upstream Finder. A read-only Scintilla view
// with the Notepad++ "searchResult" lexer; same text layout as upstream:
//   Search "foo" (3 hits in 2 files of 5 searched)
//     C:\path\a.txt (2 hits)
//   \tLine  10: text with foo
// New searches are inserted at the top; previous searches are folded.
#pragma once

#include "core/lang/LangType.h"
#include "core/search/Search.h"

#include <QWidget>

#include <Scintilla.h>

#include <string>
#include <vector>

class ScintillaEditBase;

namespace npp::core {
class IEditor;
class LanguageRegistry;
class StyleRegistry;
struct LanguageApplyOptions;
class NativeLanguage;
}

namespace npp::ui {

class EditorAdapter;

class SearchResultsPanel final : public QWidget {
    Q_OBJECT
public:
    struct Hit {
        std::string file;                                  // UTF-8 path ("" for header lines)
        size_t      line = 0;                              // 1-based line in the file
        std::vector<std::pair<intptr_t, intptr_t>> ranges; // byte ranges in the document (or in the line)
        bool        lineRelative = false;                  // ranges relative to the line start (Find in Files)
        intptr_t    lineStartInDoc = -1;                   // document position of the line start (not lineRelative)
        size_t      prefixLen = 0;                         // length of "	Line  N: " in the panel line
    };

    explicit SearchResultsPanel(QWidget* parent = nullptr);
    ~SearchResultsPanel() override;

    void applyStyles(const core::LanguageRegistry& langs, const core::StyleRegistry& styles, const core::LanguageApplyOptions& o);
    void setNativeLanguage(const core::NativeLanguage* nl) { m_nativeLang = nl; }
    void setOneEntryPerLine(bool on) { m_oneEntryPerLine = on; }

    // Building a search (mirrors Finder::addSearchLine / addFileNameTitle / add / addFileHitCount / addSearchResultInfo)
    void beginSearch(const std::string& what);
    void addFile(const std::string& displayName);
    // `lineText` without EOL (UTF-8), match [start, end) byte offsets inside it, `docRange` in the file
    void addHit(const std::string& file, size_t line, const std::string& lineText, size_t start, size_t end,
                std::pair<intptr_t, intptr_t> docRange, size_t totalLines, bool lineRelative = false);
    void endFile(int hits);
    void endSearch(int hits, int filesSearched, bool entireNotSelection);

    void clear();
    // FindInFinderDlg: searches the text of the result lines, results as a new search on top.
    // Returns the number of hits.
    int findInResults(const core::SearchOptions& o);
    void gotoNextResult(bool next); // F4 / Shift+F4
    ScintillaEditBase* view() const noexcept { return m_view; }
    int searchCount() const noexcept { return m_searches; }
    // Hit info of a result line (nullptr for header lines)
    const Hit* hitAt(intptr_t line) const;

signals:
    void findInResultsRequested();
    // Double click / Enter / F4 on a result: open file at the range
    void activated(const QString& file, intptr_t start, intptr_t end, int line, bool lineRelative);

protected:
    bool eventFilter(QObject* o, QEvent* e) override;

private:
    void activateLine(intptr_t line);
    void addLine(const std::string& text, Hit hit, SearchResultMarkingLine marking);
    void refreshMarkings();
    std::string str(const char* id, const char* fallback) const;

    ScintillaEditBase* m_view = nullptr;
    const core::NativeLanguage* m_nativeLang = nullptr;
    bool m_oneEntryPerLine = false;

    // the whole panel (all searches): one entry per document line
    std::vector<Hit> m_hits;
    std::vector<SearchResultMarkingLine> m_markings;
    SearchResultMarkings m_markingsStruct{};

    // search being built (inserted at the top on endSearch)
    std::string m_text;
    std::vector<Hit> m_newHits;
    std::vector<SearchResultMarkingLine> m_newMarkings;
    size_t m_fileHeaderLine = 0, m_fileHeaderOffset = 0;
    intptr_t m_previousLine = -1;
    int m_filesWithHits = 0;
    int m_searches = 0;
};

} // namespace npp::ui
