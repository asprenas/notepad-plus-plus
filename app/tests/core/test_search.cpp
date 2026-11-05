#include "core/buffer/FileManager.h"
#include "core/search/Search.h"
#include "core/util/PathUtil.h"

#include <BoostRegexSearch.h>
#include <Scintilla.h>
#include <ScintillaEditBase.h>

#include <QApplication>
#include <QCoreApplication>
#include <QTest>

using namespace npp::core;

namespace {

class SciAdapter final : public IEditor {
public:
    explicit SciAdapter(ScintillaEditBase& e) : m_e(e) {}
    intptr_t send(unsigned int m, uintptr_t w, intptr_t l) const override { return m_e.send(m, w, l); }
private:
    ScintillaEditBase& m_e;
};

SearchOptions regex(const char* find, const char* repl = "")
{
    SearchOptions o;
    o.type = SearchType::Regex;
    o.findWhat = find;
    o.replaceWith = repl;
    o.matchCase = true;
    return o;
}

} // namespace

class TestSearch : public QObject {
    Q_OBJECT
private:
    ScintillaEditBase* m_ed = nullptr;
    std::unique_ptr<IEditor> m_e;

    void setText(const char* t)
    {
        m_e->send(SCI_SETCODEPAGE, SC_CP_UTF8);
        m_e->sendStr(SCI_SETTEXT, 0, t);
    }

private slots:
    void initTestCase()
    {
        m_ed = new ScintillaEditBase;
        m_e = std::make_unique<SciAdapter>(*m_ed);
    }
    void cleanupTestCase()
    {
        m_e.reset();
        delete m_ed;
    }

    void extendedMode()
    {
        QCOMPARE(convertExtended("a\\tb\\n\\r"), std::string("a\tb\n\r"));
        QCOMPARE(convertExtended("\\x41\\d066\\o103\\b01000100"), std::string("ABCD"));
        QCOMPARE(convertExtended("\\u00e9\\uD55C"), std::string("é한"));
        QCOMPARE(convertExtended("\\q\\\\"), std::string("\\q\\"));
        QCOMPARE(convertExtended("\\0"), std::string(1, '\0'));
        QCOMPARE(convertExtended("\\x4"), std::string("\\x4")); // too short: literal
    }

    void flags()
    {
        SearchOptions o = regex("a");
        o.wholeWord = true;
        o.dotMatchesNewline = true;
        const int f = searchFlags(o);
        QVERIFY(f & SCFIND_REGEXP);
        QVERIFY(f & SCFIND_MATCHCASE);
        QVERIFY(f & SCFIND_WHOLEWORD);
        QVERIFY(f & SCFIND_REGEXP_DOTMATCHESNL);
    }

    void findNextWrapAndBackward()
    {
        setText("one two one two");
        SearchOptions o;
        o.findWhat = "two";
        auto m = findNext(*m_e, o, 0);
        QVERIFY(m);
        QCOMPARE(m->start, intptr_t(4));
        m = findNext(*m_e, o, 13);
        QVERIFY(m && m->wrapped);
        QCOMPARE(m->start, intptr_t(4));
        o.wrap = false;
        QVERIFY(!findNext(*m_e, o, 13));
        o.directionDown = false;
        m = findNext(*m_e, o, 15);
        QCOMPARE(m->start, intptr_t(12));
        o.findWhat = "TWO";
        o.directionDown = true;
        QVERIFY(findNext(*m_e, o, 0)); // case-insensitive by default
        o.matchCase = true;
        QVERIFY(!findNext(*m_e, o, 0));
    }

    // Boost.Regex features used by Notepad++ users: pattern, text, expected count
    void regexFind_data()
    {
        QTest::addColumn<QByteArray>("pattern");
        QTest::addColumn<QByteArray>("text");
        QTest::addColumn<int>("count");
        auto r = [](const char* n, const char* p, const char* t, int c) { QTest::newRow(n) << QByteArray(p) << QByteArray(t) << c; };
        r("digits", "\\d+", "a1 b22 c333", 3);
        r("lookahead", "\\d+(?=;)", "1; 2, 3;", 2);
        r("lookbehind", "(?<=\\$)\\d+", "$5 6 $7", 2);
        r("neg lookahead", "foo(?!bar)", "foobar foobaz", 1);
        r("non greedy", "<.+?>", "<a><b>", 2);
        r("greedy", "<.+>", "<a><b>", 1);
        r("word boundary", "\\bcat\\b", "cat concat cat.", 2);
        r("line anchors", "^x", "x\nx\ny", 2);
        r("end anchor", "y$", "y\nay\nb", 2);
        r("dot no newline", "a.b", "a\nb axb", 1);
        r("class", "[A-Z]{2}", "AB cd EF", 2);
        r("hangul range", "[가-힣]+", "abc 한글 def 테스트", 2);
        r("unicode escape", "\\x{D55C}", "한국", 1);
        r("alternation", "cat|dog", "cat dog cow", 2);
        r("backref", "(\\w)\\1", "aa bc dd", 2);
        r("named group", "(?<y>\\d{4})-\\k<y>", "2026-2026 2025-2026", 1);
        r("atomic", "(?>a+)b", "aaab aaa", 1);
        r("possessive", "a++b", "aab", 1);
        r("inline icase", "(?i)HELLO", "hello Hello", 2);
        r("whitespace", "\\s+", "a  b\tc", 2);
        r("posix class", "[[:digit:]]+", "x12y3", 2);
        r("empty lines", "^$", "a\n\nb\n\n", 3);
        r("crlf", "\\r\\n", "a\r\nb\r\n", 2);
        r("R any newline", "a\\Rb", "a\r\nb a\nb", 2);
        r("K reset", "foo\\Kbar", "foobar", 1);
        r("conditional", "(<)?\\w+(?(1)>)", "<a> b", 2);
        r("quantifier range", "x{2,3}", "x xx xxxx", 2);
        r("escaped dot", "\\.txt", "a.txt btxt", 1);
        r("start of text", "\\Aab", "ab ab", 1);
        r("hex escape", "\\x41", "ABA", 2);
    }

    void regexFind()
    {
        QFETCH(QByteArray, pattern);
        QFETCH(QByteArray, text);
        QFETCH(int, count);
        setText(text.constData());
        QCOMPARE(countMatches(*m_e, regex(pattern.constData())), count);
    }

    void regexReplace_data()
    {
        QTest::addColumn<QByteArray>("pattern");
        QTest::addColumn<QByteArray>("repl");
        QTest::addColumn<QByteArray>("text");
        QTest::addColumn<QByteArray>("result");
        auto r = [](const char* n, const char* p, const char* rp, const char* t, const char* res) {
            QTest::newRow(n) << QByteArray(p) << QByteArray(rp) << QByteArray(t) << QByteArray(res);
        };
        r("dollar groups", "(\\w+) (\\w+)", "$2 $1", "hello world", "world hello");
        r("backslash groups", "(\\d)(\\d)", "\\2\\1", "12 34", "21 43");
        r("named", "(?<a>x)", "[$+{a}]", "x", "[x]");
        r("upper", "(\\w+)", "\\U$1", "abc", "ABC");
        r("lower", "(\\w+)", "\\L$1", "ABC", "abc");
        r("upper first", "(\\w)(\\w*)", "\\u$1$2", "abc", "Abc");
        r("whole match", "\\d+", "<$0>", "a1b22", "a<1>b<22>");
        r("conditional repl", "(a)|b", "(?1A:B)", "ab", "AB");
        r("insert at line start", "^", "> ", "a\nb", "> a\n> b");
        r("append at line end", "$", ";", "a\nb", "a;\nb;");
        r("crlf kept one", "^", "#", "a\r\nb", "#a\r\n#b");
        r("delete empty lines", "^\\R", "", "a\n\nb\n", "a\nb\n");
        r("trim trailing", "[ \\t]+$", "", "a  \nb\t", "a\nb");
        r("newline repl", ",", "\\n", "a,b", "a\nb");
        r("hangul", "한(\\w)", "[$1]", "한글 한국", "[글] [국]");
    }

    void regexReplace()
    {
        QFETCH(QByteArray, pattern);
        QFETCH(QByteArray, repl);
        QFETCH(QByteArray, text);
        QFETCH(QByteArray, result);
        setText(text.constData());
        replaceAll(*m_e, regex(pattern.constData(), repl.constData()));
        QCOMPARE(QByteArray::fromStdString(m_e->text()), result);
    }

    void normalAndExtendedReplace()
    {
        setText("a.b.c");
        SearchOptions o;
        o.findWhat = ".";
        o.replaceWith = "\\t";
        QCOMPARE(replaceAll(*m_e, o), 2);
        QCOMPARE(m_e->text(), std::string("a\\tb\\tc")); // normal mode: literal
        setText("a.b.c");
        o.type = SearchType::Extended;
        QCOMPARE(replaceAll(*m_e, o), 2);
        QCOMPARE(m_e->text(), std::string("a\tb\tc"));
        setText("x");
        o.findWhat = "\\x78";
        o.replaceWith = "\\u00e9";
        replaceAll(*m_e, o);
        QCOMPARE(m_e->text(), std::string("é"));
        // single replace + undo as one action
        setText("aaa");
        SearchOptions n;
        n.findWhat = "a";
        n.replaceWith = "bb";
        QCOMPARE(replaceAll(*m_e, n), 3);
        m_e->send(SCI_UNDO);
        QCOMPARE(m_e->text(), std::string("aaa"));
    }

    void findAndReplaceInFiles()
    {
        const std::string dir = utf8FromPath(fs::temp_directory_path()) + "/npp-core-fif-" + std::to_string(QCoreApplication::applicationPid());
        { std::error_code ec_; fs::remove_all(pathFromUtf8(dir), ec_); }
        fs::create_directories(pathFromUtf8(dir + "/sub"));
        fs::create_directories(pathFromUtf8(dir + "/node_modules"));
        QVERIFY(writeFileContent(dir + "/a.cpp", "int main() {\n  return 0; // needle\n}\n"));
        QVERIFY(writeFileContent(dir + "/b.h", "// needle needle\n"));
        QVERIFY(writeFileContent(dir + "/c.txt", "needle\n"));
        QVERIFY(writeFileContent(dir + "/sub/d.cpp", "needle in sub\n"));
        QVERIFY(writeFileContent(dir + "/node_modules/e.cpp", "needle in node_modules\n"));
        // CP949 file: must stay CP949 after replace
        QVERIFY(writeFileContent(dir + "/k.cpp", std::string("\xC7\xD1\xB1\xDB needle\n"))); // "한글 needle"

        FindInFilesOptions fo;
        fo.directory = dir;
        fo.filters = "*.cpp *.h !+\\node_modules";
        auto files = listFilesToSearch(fo);
        QCOMPARE(files.size(), size_t(4)); // a.cpp b.h k.cpp sub/d.cpp
        fo.recursive = false;
        QCOMPARE(listFilesToSearch(fo).size(), size_t(3));
        fo.filters = "";
        QCOMPARE(listFilesToSearch(fo).size(), size_t(4)); // all files, top level

        FileManager fm(*m_e, {});
        SearchOptions o;
        o.findWhat = "needle";
        fo.recursive = true;
        fo.filters = "*.cpp *.h !+\\node_modules";
        files = listFilesToSearch(fo);
        const auto hits = findInFiles(fm, files, o);
        size_t ranges = 0;
        for (const auto& h : hits)
            ranges += h.ranges.size();
        QCOMPARE(ranges, size_t(5));
        const auto it = std::find_if(hits.begin(), hits.end(), [](const LineHit& h) { return h.file.find("b.h") != std::string::npos; });
        QVERIFY(it != hits.end());
        QCOMPARE(it->line, size_t(1));
        QCOMPARE(it->ranges.size(), size_t(2));
        QCOMPARE(it->ranges[1], std::make_pair(size_t(10), size_t(16)));
        QVERIFY(fm.buffers().empty()); // files were closed again

        // cancel after first file
        size_t calls = 0;
        findInFiles(fm, files, o, [&calls](size_t, size_t, const std::string&) { return ++calls < 2; });
        QCOMPARE(calls, size_t(2));

        LoadOptions cp949;
        o.replaceWith = "pin";
        QCOMPARE(replaceInFiles(fm, files, o), 5);
        QCOMPARE(readFileContent(dir + "/k.cpp").value(), std::string("\xC7\xD1\xB1\xDB pin\n"));
        QCOMPARE(readFileContent(dir + "/b.h").value(), std::string("// pin pin\n"));
        { std::error_code ec_; fs::remove_all(pathFromUtf8(dir), ec_); }
    }
};

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    initRegexLocale();
    TestSearch t;
    return QTest::qExec(&t, argc, argv);
}

#include "test_search.moc"
