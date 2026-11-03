// Edit menu text operations (core/editor/TextOps): input / output pairs incl. Hangul and emoji.
#include "core/editor/AutoIndent.h"
#include "core/editor/ColumnOps.h"
#include "core/editor/CommentOps.h"
#include "core/editor/Sorters.h"
#include "core/editor/TextOps.h"
#include "core/search/Search.h"

#include <Scintilla.h>
#include <ScintillaEditBase.h>

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

std::string u8(const char* s) { return s; }

} // namespace

class TestTextOps : public QObject {
    Q_OBJECT
    ScintillaEditBase* m_ed = nullptr;
    std::unique_ptr<IEditor> m_e;

    const IEditor& e() { return *m_e; }
    void setText(const std::string& t, int eolMode = SC_EOL_LF)
    {
        m_e->send(SCI_SETCODEPAGE, SC_CP_UTF8);
        m_e->send(SCI_SETEOLMODE, static_cast<uintptr_t>(eolMode));
        m_e->sendStr(SCI_SETTEXT, 0, t);
        m_e->send(SCI_SETSEL, 0, 0);
        m_e->send(SCI_SETTABWIDTH, 4);
        m_e->send(SCI_EMPTYUNDOBUFFER);
    }
    std::string text() { return m_e->text(); }

private slots:
    void initTestCase()
    {
        initRegexLocale();
        m_ed = new ScintillaEditBase;
        m_e = std::make_unique<SciAdapter>(*m_ed);
    }
    void cleanupTestCase()
    {
        m_e.reset();
        delete m_ed;
    }

    void caseConversion_data()
    {
        QTest::addColumn<int>("kind");
        QTest::addColumn<QString>("in");
        QTest::addColumn<QString>("out");
        QTest::newRow("upper") << int(TextCase::Upper) << "abc 한글 ß é😀" << "ABC 한글 ß É😀";
        QTest::newRow("lower") << int(TextCase::Lower) << "ABC ÉÇ" << "abc éç";
        QTest::newRow("proper force") << int(TextCase::ProperForce) << "hELLO wORLD it's o'neil" << "Hello World It's O'neil";
        QTest::newRow("proper blend") << int(TextCase::ProperBlend) << "hELLO wORLD McDonald" << "HELLO WORLD McDonald";
        QTest::newRow("sentence force") << int(TextCase::SentenceForce) << "hello WORLD. this is i ok? yes! so i.\n\nnew" << "Hello world. This is I ok? Yes! So i.\n\nNew";
        QTest::newRow("sentence blend") << int(TextCase::SentenceBlend) << "hello WORLD. foo" << "Hello WORLD. Foo";
        QTest::newRow("sentence decimal") << int(TextCase::SentenceForce) << "pi is 3.14 ok" << "Pi is 3.14 ok";
        QTest::newRow("invert") << int(TextCase::Invert) << "aBc 1" << "AbC 1";
    }
    void caseConversion()
    {
        QFETCH(int, kind);
        QFETCH(QString, in);
        QFETCH(QString, out);
        QCOMPARE(QString::fromStdString(changeCaseUtf8(in.toStdString(), static_cast<TextCase>(kind))), out);
    }

    void randomCaseKeepsLetters()
    {
        const std::string r = changeCaseUtf8("abcdefghij", TextCase::Random);
        QCOMPARE(QString::fromStdString(r).toLower(), QStringLiteral("abcdefghij"));
    }

    void convertSelectionAndMultiSelection()
    {
        setText("abc def ghi");
        m_e->send(SCI_SETSEL, 4, 7);
        convertSelectedTextTo(e(), TextCase::Upper);
        QCOMPARE(text(), u8("abc DEF ghi"));
        // multi-selection: both ranges converted
        m_e->send(SCI_SETSELECTION, 3, 0);
        m_e->send(SCI_ADDSELECTION, 11, 8);
        convertSelectedTextTo(e(), TextCase::Upper);
        QCOMPARE(text(), u8("ABC DEF GHI"));
        QCOMPARE(m_e->send(SCI_GETSELECTIONS), intptr_t(2));
    }

    void sorting_data()
    {
        QTest::addColumn<int>("kind");
        QTest::addColumn<bool>("desc");
        QTest::addColumn<QString>("in");
        QTest::addColumn<QString>("out");
        QTest::newRow("lex") << int(SortKind::Lexicographic) << false << "b\nB\na\n가\nA" << "A\nB\na\nb\n가";
        QTest::newRow("lex desc") << int(SortKind::Lexicographic) << true << "b\na\nc" << "c\nb\na";
        QTest::newRow("lex icase") << int(SortKind::LexicographicIgnoreCase) << false << "b\nA\nC" << "A\nb\nC";
        QTest::newRow("integer") << int(SortKind::Integer) << false << "10\n9\n-3\nx2\nx10" << "-3\n9\n10\nx2\nx10";
        QTest::newRow("decimal dot") << int(SortKind::DecimalDot) << false << "1.5\n\n-2.25\n1.25" << "\n-2.25\n1.25\n1.5";
        QTest::newRow("decimal comma") << int(SortKind::DecimalComma) << true << "1,5\n2,25\n1,25" << "2,25\n1,5\n1,25";
        QTest::newRow("length") << int(SortKind::Length) << false << "ccc\na\nbb" << "a\nbb\nccc";
        QTest::newRow("reverse") << int(SortKind::Reverse) << false << "1\n2\n3" << "3\n2\n1";
    }
    void sorting()
    {
        QFETCH(int, kind);
        QFETCH(bool, desc);
        QFETCH(QString, in);
        QFETCH(QString, out);
        setText(in.toStdString());
        QCOMPARE(sortSelectedLines(e(), static_cast<SortKind>(kind), desc), intptr_t(-1));
        QCOMPARE(QString::fromStdString(text()), out);
    }

    void sortSelectedLinesOnly()
    {
        setText("z\nc\nb\na\ny", SC_EOL_CRLF);
        setText("z\r\nc\r\nb\r\na\r\ny", SC_EOL_CRLF);
        m_e->send(SCI_SETSEL, 3, 12); // lines 1..3
        sortSelectedLines(e(), SortKind::Lexicographic, false);
        QCOMPARE(text(), u8("z\r\na\r\nb\r\nc\r\ny"));
    }

    void sortNumericError()
    {
        setText("1\n2\n-\n3");
        // "-" is not a number: upstream reports the line (1-based)
        QCOMPARE(sortSelectedLines(e(), SortKind::DecimalDot, false), intptr_t(3));
        QCOMPARE(text(), u8("1\n2\n-\n3"));
    }

    void sortRectangularColumns()
    {
        setText("x3\ny1\nz2");
        m_e->send(SCI_SETRECTANGULARSELECTIONANCHOR, 1);
        m_e->send(SCI_SETRECTANGULARSELECTIONCARET, 9); // line 2 col 2
        QVERIFY(m_e->send(SCI_GETSELECTIONS) > 1);
        sortSelectedLines(e(), SortKind::Lexicographic, false);
        QCOMPARE(text(), u8("y1\nz2\nx3"));
    }

    void duplicates()
    {
        setText("a\nb\na\nc\nb");
        removeAnyDuplicateLines(e());
        QCOMPARE(text(), u8("a\nb\nc"));

        setText("a\na\nb\nb\nb\na\nc\nc");
        removeConsecutiveDuplicateLines(e());
        QCOMPARE(text(), u8("a\nb\na\nc"));
    }

    void emptyLines()
    {
        setText("a\n\n\nb\n  \t\nc\n");
        removeEmptyLines(e(), false);
        QCOMPARE(text(), u8("a\nb\n  \t\nc"));
        setText("a\n\n  \nb\n \n");
        removeEmptyLines(e(), true);
        QCOMPARE(text(), u8("a\nb"));
    }

    void blankLinesAboveBelow()
    {
        setText("one\ntwo");
        m_e->send(SCI_GOTOPOS, 5);
        insertNewLineAboveCurrentLine(e());
        QCOMPARE(text(), u8("one\n\ntwo"));
        QCOMPARE(m_e->send(SCI_GETCURRENTPOS), intptr_t(4));
        insertNewLineBelowCurrentLine(e());
        QCOMPARE(text(), u8("one\n\n\ntwo"));
    }

    void trims()
    {
        setText("  a  \n\tb\t\n c ");
        doTrim(e(), TrimPart::Tail);
        QCOMPARE(text(), u8("  a\n\tb\n c"));
        doTrim(e(), TrimPart::Head);
        QCOMPARE(text(), u8("a\nb\nc"));
        setText("  x  \n  y  ");
        doTrim(e(), TrimPart::Both);
        QCOMPARE(text(), u8("x\ny"));
        setText(" 한 \n 글 ");
        trimAll(e());
        QCOMPARE(text(), u8("한 글"));
    }

    void eolToSpace()
    {
        setText("a\nb\nc");
        eol2ws(e());
        QCOMPARE(text(), u8("a b c"));
    }

    void tabsAndSpaces()
    {
        setText("\ta\tb\n  x\t\n");
        wsTabConvert(e(), SpaceTab::Tab2Space);
        QCOMPARE(text(), u8("    a   b\n  x \n"));
        setText("        a       b");
        wsTabConvert(e(), SpaceTab::Space2TabAll);
        QCOMPARE(text(), u8("\t\ta\t\tb"));
        setText("        a       b");
        wsTabConvert(e(), SpaceTab::Space2TabLeading);
        QCOMPARE(text(), u8("\t\ta       b"));
        setText("한\tb");
        wsTabConvert(e(), SpaceTab::Tab2Space);
        QCOMPARE(text(), u8("한   b"));
    }

    void lineComments()
    {
        const CommentSymbols cpp{"//", "/*", "*/", L_CPP};
        setText("int a;\n  int b;\n\nint c;");
        m_e->send(SCI_SETSEL, 0, 22);
        QVERIFY(doBlockComment(e(), cpp, CommentMode::Comment));
        QCOMPARE(text(), u8("// int a;\n  // int b;\n\n// int c;"));
        QVERIFY(doBlockComment(e(), cpp, CommentMode::Toggle));
        QCOMPARE(text(), u8("int a;\n  int b;\n\nint c;"));
        // uncomment without the space after the symbol, case insensitive (REM)
        const CommentSymbols bat{"REM", "", "", L_BATCH};
        setText("rem x\nREMy");
        m_e->send(SCI_SETSEL, 0, 10);
        QVERIFY(doBlockComment(e(), bat, CommentMode::Uncomment));
        QCOMPARE(text(), u8("x\ny"));
    }

    void streamComments()
    {
        const CommentSymbols cpp{"//", "/*", "*/", L_CPP};
        setText("a b c");
        m_e->send(SCI_SETSEL, 2, 3);
        QVERIFY(doStreamComment(e(), cpp));
        QCOMPARE(text(), u8("a /* b */ c"));
        m_e->send(SCI_SETSEL, 5, 5);
        QVERIFY(undoStreamComment(e(), cpp));
        QCOMPARE(text(), u8("a b c"));
        // no line comment symbol (HTML/XML): advanced single line mode
        const CommentSymbols xml{"", "<!--", "-->", L_XML};
        setText("<a/>\n<b/>");
        m_e->send(SCI_SETSEL, 0, 9);
        QVERIFY(doBlockComment(e(), xml, CommentMode::Comment));
        QCOMPARE(text(), u8("<!-- <a/> -->\n<!-- <b/> -->"));
        m_e->send(SCI_SETSEL, 0, 27);
        QVERIFY(doBlockComment(e(), xml, CommentMode::Toggle));
        QCOMPARE(text(), u8("<a/>\n<b/>"));
    }

    void udlSymbols()
    {
        const auto s = udlCommentSymbols("00# 01 02 03{* 04*}");
        QCOMPARE(s.line, std::string("#"));
        QCOMPARE(s.start, std::string("{*"));
        QCOMPARE(s.end, std::string("*}"));
    }

    void autoIndent()
    {
        // C-like: newline after "{" indents one level, "}" aligns with the "{" line
        setText("int f() {");
        m_e->send(SCI_SETTABWIDTH, 4);
        m_e->send(SCI_SETUSETABS, 0);
        m_e->send(SCI_DOCUMENTEND);
        m_e->send(SCI_NEWLINE);
        maintainIndentation(e(), L_CPP, kAutoIndentAdvanced, '\n');
        QCOMPARE(text(), u8("int f() {\n    "));
        m_e->send(SCI_REPLACESEL, 0, reinterpret_cast<intptr_t>("x;"));
        m_e->send(SCI_NEWLINE);
        maintainIndentation(e(), L_CPP, kAutoIndentAdvanced, '\n');
        m_e->send(SCI_REPLACESEL, 0, reinterpret_cast<intptr_t>("}"));
        maintainIndentation(e(), L_CPP, kAutoIndentAdvanced, '}');
        QCOMPARE(text(), u8("int f() {\n    x;\n}"));
        // if without braces: next line indented, the one after back
        setText("    if (a)");
        m_e->send(SCI_DOCUMENTEND);
        m_e->send(SCI_NEWLINE);
        maintainIndentation(e(), L_CPP, kAutoIndentAdvanced, '\n');
        QCOMPARE(text(), u8("    if (a)\n        "));
        // basic mode keeps the previous indentation
        setText("  abc");
        m_e->send(SCI_DOCUMENTEND);
        m_e->send(SCI_NEWLINE);
        maintainIndentation(e(), L_TEXT, kAutoIndentBasic, '\n');
        QCOMPARE(text(), u8("  abc\n  "));
    }

    void columnEditor()
    {
        QCOMPARE(formatColumnNumber(5, NumBase::Dec, 3, Leading::Zeros), u8("005"));
        QCOMPARE(formatColumnNumber(5, NumBase::Dec, 3, Leading::Spaces), u8("  5"));
        QCOMPARE(formatColumnNumber(5, NumBase::Dec, 3, Leading::None), u8("5  "));
        QCOMPARE(formatColumnNumber(255, NumBase::HexUpper, 2, Leading::None), u8("FF"));
        QCOMPARE(formatColumnNumber(5, NumBase::Bin, 4, Leading::Zeros), u8("0101"));
        // no column selection: from the caret column of the caret line to the last line
        setText("ab\ncd\n\nef");
        m_e->send(SCI_GOTOPOS, 1);
        columnInsertText(e(), "|");
        QCOMPARE(text(), u8("a|b\nc|d\n |\ne|f"));
        // rectangular selection, numbers 8, 9, 10 (2 digits, leading zeros)
        setText("x\nx\nx");
        m_e->send(SCI_SETRECTANGULARSELECTIONANCHOR, 1);
        m_e->send(SCI_SETRECTANGULARSELECTIONCARET, 5);
        columnInsertNumbers(e(), 8, 1, 1, NumBase::Dec, Leading::Zeros);
        QCOMPARE(text(), u8("x08\nx09\nx10"));
        // repeat 2
        setText("\n\n\n");
        m_e->send(SCI_GOTOPOS, 0);
        columnInsertNumbers(e(), 1, 1, 2, NumBase::Dec, Leading::None);
        QCOMPARE(text(), u8("1\n1\n2\n2"));
    }

    void charsInRange()
    {
        setText("abc\xC3\xA9 x");
        QVERIFY(findCharInRange(e(), 128, 255, 0, true, true));
        QCOMPARE(m_e->send(SCI_GETSELECTIONSTART), intptr_t(3));
        QVERIFY(!findCharInRange(e(), 0, 0, 0, true, true));
    }

    void undoIsOneAction()
    {
        setText("c\nb\na");
        sortSelectedLines(e(), SortKind::Lexicographic, false);
        m_e->send(SCI_UNDO);
        QCOMPARE(text(), u8("c\nb\na"));
    }
};

QTEST_MAIN(TestTextOps)
#include "test_textops.moc"
