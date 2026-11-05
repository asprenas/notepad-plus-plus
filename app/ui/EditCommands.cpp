// Edit menu commands of phase 3 (P3-06): text operations from core/editor/TextOps and the
// UI-side commands (clipboard, selection, multi-select, date/time, on-selection actions).
#include "MainWindow.h"

#include "editor/EditorPane.h"
#include "menu/ActionRegistry.h"
#include "menu/CommandIds.h"

#include "core/editor/CommentOps.h"
#include "core/editor/Sorters.h"
#include "core/editor/TextOps.h"
#include "core/util/PathUtil.h"

#include <Scintilla.h>
#include <ScintillaEditBase.h>

#include <QApplication>
#include <QClipboard>
#include <QCollator>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QLocale>
#include <QMessageBox>
#include <QMimeData>
#include <QProcess>
#include <QUrl>

namespace npp::ui {

using namespace npp::core;
using namespace cmd;

namespace {

QString q(const std::string& s) { return QString::fromStdString(s); }

// Upstream clipboard format names for binary content (clipboardFormats.h)
constexpr char kMimeBinaryLength[] = "application/x-notepadpp-binary-length";

// Sort lines in locale order (upstream SortLocale: CompareStringEx with the user locale)
class LocaleSorter final : public ISorter {
public:
    explicit LocaleSorter(bool descending) : ISorter(descending, 0, 0)
    {
        m_collator.setNumericMode(true);
        m_collator.setCaseSensitivity(Qt::CaseInsensitive);
    }
    void sort(std::vector<std::u16string>& lines) override
    {
        std::stable_sort(lines.begin(), lines.end(), [this](const std::u16string& a, const std::u16string& b) {
            const int c = m_collator.compare(QString::fromStdU16String(a), QString::fromStdU16String(b));
            return isDescending() ? c > 0 : c < 0;
        });
    }

private:
    QCollator m_collator{QLocale()};
};

} // namespace

void MainWindow::registerEditOpsCommands()
{
    ActionRegistry* r = m_actions;
    auto ed = [this]() -> const IEditor& { return currentPane()->adapter(); };
    auto sci = [this](unsigned msg, uptr_t w = 0, sptr_t l = 0) { return currentPane()->editor()->send(msg, w, l); };
    auto writable = [sci] { return sci(SCI_GETREADONLY) == 0; };
    auto hasSel = [sci] { return sci(SCI_GETSELECTIONEMPTY) == 0; };

    // --- Convert Case to
    const std::pair<int, TextCase> cases[] = {
        {IDM_EDIT_UPPERCASE, TextCase::Upper},           {IDM_EDIT_LOWERCASE, TextCase::Lower},
        {IDM_EDIT_PROPERCASE_FORCE, TextCase::ProperForce}, {IDM_EDIT_PROPERCASE_BLEND, TextCase::ProperBlend},
        {IDM_EDIT_SENTENCECASE_FORCE, TextCase::SentenceForce}, {IDM_EDIT_SENTENCECASE_BLEND, TextCase::SentenceBlend},
        {IDM_EDIT_INVERTCASE, TextCase::Invert},         {IDM_EDIT_RANDOMCASE, TextCase::Random},
    };
    for (auto [id, c] : cases)
        r->setHandler(id, {[ed, c] { convertSelectedTextTo(ed(), c); }, [hasSel, writable] { return hasSel() && writable(); }});

    // --- Line Operations: sorting
    struct SortCmd {
        int id;
        SortKind kind;
        bool descending;
    };
    const SortCmd sorts[] = {
        {IDM_EDIT_SORTLINES_LEXICOGRAPHIC_ASCENDING, SortKind::Lexicographic, false},
        {IDM_EDIT_SORTLINES_LEXICOGRAPHIC_DESCENDING, SortKind::Lexicographic, true},
        {IDM_EDIT_SORTLINES_LEXICO_CASE_INSENS_ASCENDING, SortKind::LexicographicIgnoreCase, false},
        {IDM_EDIT_SORTLINES_LEXICO_CASE_INSENS_DESCENDING, SortKind::LexicographicIgnoreCase, true},
        {IDM_EDIT_SORTLINES_INTEGER_ASCENDING, SortKind::Integer, false},
        {IDM_EDIT_SORTLINES_INTEGER_DESCENDING, SortKind::Integer, true},
        {IDM_EDIT_SORTLINES_DECIMALCOMMA_ASCENDING, SortKind::DecimalComma, false},
        {IDM_EDIT_SORTLINES_DECIMALCOMMA_DESCENDING, SortKind::DecimalComma, true},
        {IDM_EDIT_SORTLINES_DECIMALDOT_ASCENDING, SortKind::DecimalDot, false},
        {IDM_EDIT_SORTLINES_DECIMALDOT_DESCENDING, SortKind::DecimalDot, true},
        {IDM_EDIT_SORTLINES_LENGTH_ASCENDING, SortKind::Length, false},
        {IDM_EDIT_SORTLINES_LENGTH_DESCENDING, SortKind::Length, true},
        {IDM_EDIT_SORTLINES_REVERSE_ORDER, SortKind::Reverse, false},
        {IDM_EDIT_SORTLINES_RANDOMLY, SortKind::Random, false},
    };
    for (const SortCmd& s : sorts)
        r->setHandler(s.id, {[this, ed, s] {
            const intptr_t failed = sortSelectedLines(ed(), s.kind, s.descending);
            if (failed > 0) {
                const auto t = m_nativeLang.messageBox("SortingError", "Unable to perform numeric sorting due to line $INT_REPLACE$.",
                                                       "Sorting Error", static_cast<int>(failed));
                QMessageBox::information(this, q(t.title), q(t.message));
            }
        }, writable});
    for (bool desc : {false, true})
        r->setHandler(desc ? IDM_EDIT_SORTLINES_LOCALE_DESCENDING : IDM_EDIT_SORTLINES_LOCALE_ASCENDING, {[ed, desc] {
            const IEditor& e = ed();
            intptr_t from = 0, to = e.send(SCI_GETLINECOUNT) - 1;
            if (e.send(SCI_GETSELECTIONSTART) != e.send(SCI_GETSELECTIONEND)) {
                const auto range = selectionLinesRange(e);
                from = range.first;
                to = range.second;
            }
            LocaleSorter sorter(desc);
            e.send(SCI_BEGINUNDOACTION);
            sortLines(e, from, to, sorter);
            e.send(SCI_ENDUNDOACTION);
        }, writable});

    // --- Line Operations: others
    r->setHandler(IDM_EDIT_REMOVE_ANY_DUP_LINES, {[ed] { removeAnyDuplicateLines(ed()); }, writable});
    r->setHandler(IDM_EDIT_REMOVE_CONSECUTIVE_DUP_LINES, {[ed] { removeConsecutiveDuplicateLines(ed()); }, writable});
    r->setHandler(IDM_EDIT_REMOVEEMPTYLINES, {[ed] { removeEmptyLines(ed(), false); }, writable});
    r->setHandler(IDM_EDIT_REMOVEEMPTYLINESWITHBLANK, {[ed] { removeEmptyLines(ed(), true); }, writable});
    r->setHandler(IDM_EDIT_BLANKLINEABOVECURRENT, {[ed] { insertNewLineAboveCurrentLine(ed()); }, writable});
    r->setHandler(IDM_EDIT_BLANKLINEBELOWCURRENT, {[ed] { insertNewLineBelowCurrentLine(ed()); }, writable});

    // --- Comment / Uncomment (symbols from langs.xml or the UDL comment keywords)
    auto symbols = [this]() {
        CommentSymbols s;
        const Buffer* b = currentBuffer();
        if (!b)
            return s;
        if (b->lang == L_USER) {
            if (const UserLanguage* u = m_udls.find(b->userLangName))
                s = udlCommentSymbols(u->keywordLists[kUdlCommentsKeywordList]);
        } else if (const LanguageDef* d = m_langs.find(b->lang)) {
            s.line = d->commentLine;
            s.start = d->commentStart;
            s.end = d->commentEnd;
        }
        s.lang = b->lang;
        return s;
    };
    r->setHandler(IDM_EDIT_BLOCK_COMMENT, {[ed, symbols] { doBlockComment(ed(), symbols(), CommentMode::Toggle); }, writable});
    r->setHandler(IDM_EDIT_BLOCK_COMMENT_SET, {[ed, symbols] { doBlockComment(ed(), symbols(), CommentMode::Comment); }, writable});
    r->setHandler(IDM_EDIT_BLOCK_UNCOMMENT, {[ed, symbols] { doBlockComment(ed(), symbols(), CommentMode::Uncomment); }, writable});
    r->setHandler(IDM_EDIT_STREAM_COMMENT, {[ed, symbols] { doStreamComment(ed(), symbols()); }, writable});
    r->setHandler(IDM_EDIT_STREAM_UNCOMMENT, {[ed, symbols] { undoStreamComment(ed(), symbols(), true); }, writable});

    // --- Blank Operations
    r->setHandler(IDM_EDIT_TRIMTRAILING, {[ed] { doTrim(ed(), TrimPart::Tail); }, writable});
    r->setHandler(IDM_EDIT_TRIMLINEHEAD, {[ed] { doTrim(ed(), TrimPart::Head); }, writable});
    r->setHandler(IDM_EDIT_TRIM_BOTH, {[ed] { doTrim(ed(), TrimPart::Both); }, writable});
    r->setHandler(IDM_EDIT_EOL2WS, {[ed, sci] {
        sci(SCI_BEGINUNDOACTION);
        eol2ws(ed());
        sci(SCI_ENDUNDOACTION);
    }, writable});
    r->setHandler(IDM_EDIT_TRIMALL, {[ed] { trimAll(ed()); }, writable});
    r->setHandler(IDM_EDIT_TAB2SW, {[ed] { wsTabConvert(ed(), SpaceTab::Tab2Space); }, writable});
    r->setHandler(IDM_EDIT_SW2TAB_LEADING, {[ed] { wsTabConvert(ed(), SpaceTab::Space2TabLeading); }, writable});
    r->setHandler(IDM_EDIT_SW2TAB_ALL, {[ed] { wsTabConvert(ed(), SpaceTab::Space2TabAll); }, writable});

    // --- Insert date / time (upstream: TIME DATE unless "reverse default order")
    auto insertText = [sci](const QString& s) {
        const QByteArray utf8 = s.toUtf8();
        sci(SCI_BEGINUNDOACTION);
        sci(SCI_REPLACESEL, 0, reinterpret_cast<sptr_t>(""));
        sci(SCI_ADDTEXT, static_cast<uptr_t>(utf8.size()), reinterpret_cast<sptr_t>(utf8.constData()));
        sci(SCI_ENDUNDOACTION);
    };
    for (bool shortFmt : {true, false})
        r->setHandler(shortFmt ? IDM_EDIT_INSERT_DATETIME_SHORT : IDM_EDIT_INSERT_DATETIME_LONG, {[this, insertText, shortFmt] {
            const QDateTime now = QDateTime::currentDateTime();
            const QLocale loc;
            const QString date = loc.toString(now.date(), shortFmt ? QLocale::ShortFormat : QLocale::LongFormat);
            const QString time = loc.toString(now.time(), QLocale::ShortFormat);
            insertText(m_config.gui.dateTimeReverseOrder ? date + QLatin1Char(' ') + time : time + QLatin1Char(' ') + date);
        }, writable});
    r->setHandler(IDM_EDIT_INSERT_DATETIME_CUSTOMIZED, {[this, insertText] {
        // upstream format letters follow the Win32 date/time pictures, same as Qt's for the common ones
        insertText(QLocale().toString(QDateTime::currentDateTime(), q(m_config.gui.dateTimeFormat)));
    }, writable});

    // --- Copy to clipboard: all names / paths
    auto copyAll = [this](bool paths) {
        QStringList out;
        for (const auto& [id, p] : allTabs())
            if (const Buffer* b = buffer(id)) {
                const QString s = paths && !b->isUntitled() ? QDir::toNativeSeparators(q(b->path)) : q(b->displayName());
                if (!out.contains(s))
                    out << s;
            }
        QApplication::clipboard()->setText(out.join(QStringLiteral("\r\n")));
    };
    r->setHandler(IDM_EDIT_COPY_ALL_NAMES, {[copyAll] { copyAll(false); }});
    r->setHandler(IDM_EDIT_COPY_ALL_PATHS, {[copyAll] { copyAll(true); }});

    // --- Read-only for all documents
    auto setAllReadOnly = [this](bool ro) {
        for (const auto& b : m_files->buffers()) {
            b->userReadOnly = ro;
            refreshBuffer(b.get());
        }
        for (EditorPane* p : m_panes)
            if (const Buffer* b = buffer(p->current()))
                p->editor()->send(SCI_SETREADONLY, b->isReadOnly());
    };
    r->setHandler(IDM_EDIT_SETREADONLYFORALLDOCS, {[setAllReadOnly] { setAllReadOnly(true); }});
    r->setHandler(IDM_EDIT_CLEARREADONLYFORALLDOCS, {[setAllReadOnly] { setAllReadOnly(false); }});
    r->setHandler(IDM_EDIT_TOGGLESYSTEMREADONLY, {[this] {
        Buffer* b = currentBuffer();
        if (!b || b->isUntitled())
            return;
        QFile f(q(b->path));
        const auto perms = f.permissions();
        const bool ro = !(perms & QFileDevice::WriteOwner);
        f.setPermissions(ro ? perms | QFileDevice::WriteOwner | QFileDevice::WriteUser
                            : perms & ~(QFileDevice::WriteOwner | QFileDevice::WriteUser | QFileDevice::WriteGroup | QFileDevice::WriteOther));
        m_files->checkFileState(*b);
        for (EditorPane* p : m_panes)
            if (p->current() == b->id)
                p->editor()->send(SCI_SETREADONLY, b->isReadOnly());
        refreshBuffer(b);
    }, [this] { const Buffer* b = currentBuffer(); return b && !b->isUntitled(); },
       [this] { const Buffer* b = currentBuffer(); return b && b->fileReadOnly; }});

    // --- Begin/End select
    for (bool column : {false, true})
        r->setHandler(column ? IDM_EDIT_BEGINENDSELECT_COLUMNMODE : IDM_EDIT_BEGINENDSELECT, {[this, sci, column] {
            const auto cur = sci(SCI_GETCURRENTPOS);
            if (m_beginSelectPos < 0) {
                m_beginSelectPos = cur;
                m_beginSelectColumn = column;
            } else {
                sci(SCI_CHANGESELECTIONMODE, column ? SC_SEL_RECTANGLE : SC_SEL_STREAM);
                sci(column ? SCI_SETANCHOR : SCI_SETSEL, static_cast<uptr_t>(m_beginSelectPos), cur);
                m_beginSelectPos = -1;
            }
        }, [this, column] { return m_beginSelectPos < 0 || m_beginSelectColumn == column; },
           [this, column] { return m_beginSelectPos >= 0 && m_beginSelectColumn == column; }});

    // --- Multi-select
    struct MultiCmd {
        int id;
        int flags;
        bool all;
    };
    const MultiCmd multi[] = {
        {IDM_EDIT_MULTISELECTALL, 0, true}, {IDM_EDIT_MULTISELECTALLMATCHCASE, SCFIND_MATCHCASE, true},
        {IDM_EDIT_MULTISELECTALLWHOLEWORD, SCFIND_WHOLEWORD, true}, {IDM_EDIT_MULTISELECTALLMATCHCASEWHOLEWORD, SCFIND_MATCHCASE | SCFIND_WHOLEWORD, true},
        {IDM_EDIT_MULTISELECTNEXT, 0, false}, {IDM_EDIT_MULTISELECTNEXTMATCHCASE, SCFIND_MATCHCASE, false},
        {IDM_EDIT_MULTISELECTNEXTWHOLEWORD, SCFIND_WHOLEWORD, false}, {IDM_EDIT_MULTISELECTNEXTMATCHCASEWHOLEWORD, SCFIND_MATCHCASE | SCFIND_WHOLEWORD, false},
    };
    for (const MultiCmd& m : multi)
        r->setHandler(m.id, {[this, sci, m] {
            m_multiSelectFlag = m.flags;
            if (m.all && sci(SCI_GETSELECTIONSTART) == sci(SCI_GETSELECTIONEND)) { // expandWordSelection
                const auto caret = sci(SCI_GETCURRENTPOS);
                const auto s = sci(SCI_WORDSTARTPOSITION, static_cast<uptr_t>(caret), 1), e = sci(SCI_WORDENDPOSITION, static_cast<uptr_t>(caret), 1);
                if (s != e)
                    sci(SCI_SETSEL, static_cast<uptr_t>(s), e);
            }
            sci(SCI_TARGETWHOLEDOCUMENT);
            sci(SCI_SETSEARCHFLAGS, static_cast<uptr_t>(m.flags));
            sci(m.all ? SCI_MULTIPLESELECTADDEACH : SCI_MULTIPLESELECTADDNEXT);
        }});
    r->setHandler(IDM_EDIT_MULTISELECTUNDO, {[sci] {
        const auto n = sci(SCI_GETSELECTIONS);
        if (n > 0)
            sci(SCI_DROPSELECTIONN, static_cast<uptr_t>(n - 1));
    }});
    r->setHandler(IDM_EDIT_MULTISELECTSSKIP, {[this, sci] {
        sci(SCI_TARGETWHOLEDOCUMENT);
        sci(SCI_SETSEARCHFLAGS, static_cast<uptr_t>(m_multiSelectFlag));
        sci(SCI_MULTIPLESELECTADDNEXT);
        const auto n = sci(SCI_GETSELECTIONS);
        if (n > 1)
            sci(SCI_DROPSELECTIONN, static_cast<uptr_t>(n - 2));
    }});

    // --- On Selection
    auto selectedText = [this, sci]() -> QString {
        if (sci(SCI_GETSELECTIONS) != 1)
            return {};
        return q(currentPane()->adapter().textRange(sci(SCI_GETSELECTIONSTART), sci(SCI_GETSELECTIONEND))).trimmed();
    };
    auto selectedPath = [this, selectedText]() -> QString {
        QString s = selectedText();
        if (s.isEmpty() || s.contains(QLatin1Char('\n')))
            return {};
        QFileInfo fi(s);
        if (fi.isRelative())
            if (const Buffer* b = currentBuffer(); b && !b->isUntitled())
                fi.setFile(QFileInfo(q(b->path)).absoluteDir(), s);
        return fi.exists() ? fi.absoluteFilePath() : QString();
    };
    r->setHandler(IDM_EDIT_OPENSELECTEDFILETOEDIT, {[this, selectedPath] {
        const QString p = selectedPath();
        if (!p.isEmpty() && QFileInfo(p).isFile())
            openFile(p);
    }, [selectedPath] { return !selectedPath().isEmpty(); }});
    r->setHandler(IDM_EDIT_OPENSELECTEDFILEFOLDERINEXPLORER, {[selectedPath] {
        const QString p = selectedPath();
        if (p.isEmpty())
            return;
#if defined(Q_OS_WIN)
        QProcess::startDetached(QStringLiteral("explorer.exe"), {QStringLiteral("/select,") + QDir::toNativeSeparators(p)});
#else
        QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(p).isDir() ? p : QFileInfo(p).absolutePath()));
#endif
    }, [selectedPath] { return !selectedPath().isEmpty(); }});
    r->setHandler(IDM_EDIT_SEARCHONINTERNET, {[this, selectedText] {
        const QString word = selectedText();
        if (word.isEmpty())
            return;
        const GuiConfig& g = m_config.gui;
        QString url;
        switch (g.searchEngineChoice) { // upstream se_custom=0, se_duckDuckGo=1, se_google=2, se_bing=3, se_yahoo=4, se_stackoverflow=5
        case 0: {
            url = q(g.searchEngineCustom).simplified().remove(QLatin1Char(' '));
            if (!url.startsWith(QLatin1String("http://")) && !url.startsWith(QLatin1String("https://")))
                url = QStringLiteral("https://www.google.com/search?q=$(CURRENT_WORD)");
            break;
        }
        case 1: case 3: url = QStringLiteral("https://duckduckgo.com/?q=$(CURRENT_WORD)"); break;
        case 4: url = QStringLiteral("https://search.yahoo.com/search?q=$(CURRENT_WORD)"); break;
        case 5: url = QStringLiteral("https://stackoverflow.com/search?q=$(CURRENT_WORD)"); break;
        default: url = QStringLiteral("https://www.google.com/search?q=$(CURRENT_WORD)"); break;
        }
        url.replace(QStringLiteral("$(CURRENT_WORD)"), QString::fromLatin1(QUrl::toPercentEncoding(word)));
        QDesktopServices::openUrl(QUrl(url));
    }, [selectedText] { return !selectedText().isEmpty(); }});
    r->setHandler(IDM_EDIT_REDACT_SELECTION, {[sci] {
        const bool bullet = QGuiApplication::keyboardModifiers() & Qt::ShiftModifier;
        const std::string mask = bullet ? "\xE2\x97\x8F" : "\xE2\x96\x88"; // ● or █
        sci(SCI_BEGINUNDOACTION);
        const auto n = sci(SCI_GETSELECTIONS);
        // from the last selection backwards so earlier positions stay valid
        std::vector<std::pair<intptr_t, intptr_t>> sels;
        for (intptr_t i = 0; i < n; ++i)
            sels.emplace_back(sci(SCI_GETSELECTIONNSTART, static_cast<uptr_t>(i)), sci(SCI_GETSELECTIONNEND, static_cast<uptr_t>(i)));
        std::sort(sels.begin(), sels.end(), [](auto a, auto b) { return a.first > b.first; });
        for (auto [start, end] : sels) {
            const auto count = sci(SCI_COUNTCHARACTERS, static_cast<uptr_t>(start), end);
            std::string out;
            for (intptr_t j = 0; j < count; ++j) {
                const char c = static_cast<char>(sci(SCI_GETCHARAT, static_cast<uptr_t>(sci(SCI_POSITIONRELATIVE, static_cast<uptr_t>(start), j))));
                out += (c == '\r' || c == '\n') ? std::string(1, c) : mask;
            }
            sci(SCI_SETTARGETRANGE, static_cast<uptr_t>(start), end);
            sci(SCI_REPLACETARGET, out.size(), reinterpret_cast<sptr_t>(out.c_str()));
        }
        sci(SCI_ENDUNDOACTION);
    }, [hasSel, writable] { return hasSel() && writable(); }});

    // --- Paste Special
    for (bool html : {true, false})
        r->setHandler(html ? IDM_EDIT_PASTE_AS_HTML : IDM_EDIT_PASTE_AS_RTF, {[sci, html] {
            const QMimeData* m = QApplication::clipboard()->mimeData();
            QByteArray data;
            if (html && m->hasHtml())
                data = m->html().toUtf8();
            else if (!html)
                for (const char* f : {"text/rtf", "application/rtf", "Rich Text Format"})
                    if (m->hasFormat(QLatin1String(f))) {
                        data = m->data(QLatin1String(f));
                        break;
                    }
            if (!data.isEmpty())
                sci(SCI_REPLACESEL, 0, reinterpret_cast<sptr_t>(data.constData()));
        }, writable});
    // Binary copy keeps NUL characters: the byte length travels next to the text
    auto copyBinary = [this, sci](bool cut) {
        const auto s = sci(SCI_GETSELECTIONSTART), e = sci(SCI_GETSELECTIONEND);
        if (s == e)
            return;
        const std::string bytes = currentPane()->adapter().textRange(s, e);
        auto* md = new QMimeData;
        md->setText(QString::fromUtf8(bytes.data(), static_cast<qsizetype>(bytes.size())));
        md->setData(QLatin1String(kMimeBinaryLength), QByteArray(bytes.data(), static_cast<qsizetype>(bytes.size())));
        QApplication::clipboard()->setMimeData(md);
        if (cut)
            sci(SCI_REPLACESEL, 0, reinterpret_cast<sptr_t>(""));
    };
    r->setHandler(IDM_EDIT_COPY_BINARY, {[copyBinary] { copyBinary(false); }, hasSel});
    r->setHandler(IDM_EDIT_CUT_BINARY, {[copyBinary] { copyBinary(true); }, [hasSel, writable] { return hasSel() && writable(); }});
    r->setHandler(IDM_EDIT_PASTE_BINARY, {[sci] {
        const QMimeData* m = QApplication::clipboard()->mimeData();
        const QByteArray data = m->hasFormat(QLatin1String(kMimeBinaryLength)) ? m->data(QLatin1String(kMimeBinaryLength)) : m->text().toUtf8();
        sci(SCI_BEGINUNDOACTION);
        sci(SCI_REPLACESEL, 0, reinterpret_cast<sptr_t>(""));
        sci(SCI_ADDTEXT, static_cast<uptr_t>(data.size()), reinterpret_cast<sptr_t>(data.constData()));
        sci(SCI_ENDUNDOACTION);
    }, writable});
}

} // namespace npp::ui
