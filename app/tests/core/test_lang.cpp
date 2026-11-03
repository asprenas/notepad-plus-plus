// lang module tests. Uses a real Scintilla editor (ScintillaEditBase, offscreen) through IEditor.
#include "core/editor/EditorLanguage.h"
#include "core/lang/LangType.h"
#include "core/lang/LanguageRegistry.h"
#include "core/lang/LexerSpecs.h"
#include "core/lang/StyleRegistry.h"
#include "core/lang/UserLanguage.h"
#include "core/util/PathUtil.h"

#include <SciLexer.h>
#include <Scintilla.h>
#include <ScintillaEditBase.h>

#include <pugixml.hpp>

#include <QApplication>
#include <QTest>

using namespace npp::core;

namespace {

const std::string kSrc = NPP_LEGACY_SRC_DIR;

class SciAdapter final : public IEditor {
public:
    explicit SciAdapter(ScintillaEditBase& e) : m_e(e) {}
    intptr_t send(unsigned int m, uintptr_t w, intptr_t l) const override { return m_e.send(m, w, l); }
private:
    ScintillaEditBase& m_e;
};

struct Fixture {
    LanguageRegistry langs;
    StyleRegistry styles;
    Fixture()
    {
        langs.load(kSrc + "/langs.model.xml");
        styles.load(kSrc + "/stylers.model.xml");
    }
};

Fixture& fx()
{
    static Fixture f;
    return f;
}

} // namespace

class TestLang : public QObject {
    Q_OBJECT
private slots:
    void langTypeTable()
    {
        QCOMPARE(kLangCount, 97);
        QCOMPARE(static_cast<int>(L_CPP), 3);       // values are persisted / plugin API
        QCOMPARE(static_cast<int>(L_JSON5), 86);
        QCOMPARE(std::string(langInfo(L_CPP).name), std::string("cpp"));
        QCOMPARE(std::string(langInfo(L_HTML).lexer), std::string("hypertext"));
        QCOMPARE(langTypeFromName("JavaScript.js").value(), L_JAVASCRIPT);
        QCOMPARE(langTypeFromShortName("C++").value(), L_CPP);
        QCOMPARE(langTypeFromShortName("None (Normal Text)").value(), L_TEXT);
        QCOMPARE(keywordClassFromName("type1"), 2);
        QCOMPARE(keywordClassFromName("substyle8"), 16);
        QCOMPARE(keywordClassFromName("3"), 3);
    }

    void firstLineDetection()
    {
        QCOMPARE(detectLanguageFromTextBeginning("#!/usr/bin/env python3\nprint()"), L_PYTHON);
        QCOMPARE(detectLanguageFromTextBeginning("#!/bin/bash\n"), L_BASH);
        QCOMPARE(detectLanguageFromTextBeginning("\xEF\xBB\xBF<?xml version=\"1.0\"?>"), L_XML);
        QCOMPARE(detectLanguageFromTextBeginning("  \n<!DOCTYPE html>"), L_HTML);
        QCOMPARE(detectLanguageFromTextBeginning("<?= $x ?>"), L_PHP);
        QCOMPARE(detectLanguageFromTextBeginning("plain text"), L_TEXT);
    }

    void languageRegistry()
    {
        const LanguageRegistry& r = fx().langs;
        QVERIFY(r.languages().size() >= 90);
        QCOMPARE(r.languageForFile("/x/a.CPP"), L_CPP);
        QCOMPARE(r.languageForFile("/x/b.py"), L_PYTHON);
        QCOMPARE(r.languageForFile("/x/Makefile"), L_MAKEFILE);
        QCOMPARE(r.languageForFile("/x/CMakeLists.txt"), L_CMAKE);
        QCOMPARE(r.languageForFile("/x/unknown.zzz"), L_TEXT);
        QVERIFY(r.wordList(L_CPP, 2).find("int") != std::string::npos);
        QCOMPARE(r.find(L_CPP)->commentLine, std::string("//"));
        LanguageRegistry copy = r;
        copy.addUserExtensions(L_CPP, "zzz");
        QCOMPARE(copy.languageForFile("/x/unknown.zzz"), L_CPP);
    }

    void stylersRoundTrip()
    {
        const std::string original = readFileContent(kSrc + "/stylers.model.xml").value();
        StyleRegistry s;
        QVERIFY(s.loadFromString(original));
        QVERIFY(s.lexer("cpp") != nullptr);
        const Style* instr = s.lexer("cpp")->findById(5);
        QVERIFY(instr);
        QCOMPARE(instr->fg, 0x0000FF);
        QCOMPARE(instr->fontStyle, 1);
        QCOMPARE(instr->keywordClass, 0);
        QVERIFY(s.globalStyle(32) != nullptr);

        // Saving without changes must keep every attribute value
        const std::string saved = s.saveToString();
        pugi::xml_document a, b;
        QVERIFY(a.load_string(original.c_str()));
        QVERIFY(b.load_string(saved.c_str()));
        int checked = 0;
        for (pugi::xml_node lt = a.child("NotepadPlus").child("LexerStyles").child("LexerType"); lt;
             lt = lt.next_sibling("LexerType")) {
            pugi::xml_node lb = b.child("NotepadPlus").child("LexerStyles").find_child_by_attribute("LexerType", "name",
                                                                                                       lt.attribute("name").value());
            QVERIFY(lb);
            pugi::xml_node wb = lb.child("WordsStyle");
            for (pugi::xml_node wa = lt.child("WordsStyle"); wa; wa = wa.next_sibling("WordsStyle"), wb = wb.next_sibling("WordsStyle")) {
                QVERIFY(wb);
                for (pugi::xml_attribute at = wa.first_attribute(); at; at = at.next_attribute()) {
                    QCOMPARE(QString(wb.attribute(at.name()).value()).toUpper(), QString(at.value()).toUpper());
                    ++checked;
                }
            }
        }
        QVERIFY(checked > 10000);

        // Modification is written back
        s.lexer("cpp")->styles[0].fg = 0x123456;
        StyleRegistry again;
        QVERIFY(again.loadFromString(s.saveToString()));
        QCOMPARE(again.lexer("cpp")->styles[0].fg, 0x123456);
    }

    void everyLanguageApplies()
    {
        ScintillaEditBase e;
        SciAdapter ed(e);
        e.send(SCI_SETTEXT, 0, reinterpret_cast<sptr_t>("text 123 ( ) { } <a> 'q' \"s\" // c\n# x\n"));
        int withLexer = 0;
        for (int i = 0; i < kLangCount; ++i) {
            const auto lang = static_cast<LangType>(i);
            if (lang == L_USER || lang == L_EXTERNAL)
                continue;
            applyLanguage(ed, lang, fx().langs, fx().styles);
            if (e.send(SCI_GETLEXER) > 1) // SCLEX_NULL = 1
                ++withLexer;
            else
                QVERIFY2(std::string(lexerSpec(lang).lexer) == "null", langInfo(lang).name.data());
        }
        QVERIFY(withLexer >= 90);
    }

    void styleColoursApplied()
    {
        ScintillaEditBase e;
        SciAdapter ed(e);
        applyLanguage(ed, L_CPP, fx().langs, fx().styles);
        QCOMPARE(e.send(SCI_STYLEGETFORE, SCE_C_WORD), sptr_t(0xFF0000)); // stylers "0000FF" (RGB) -> BGR
        QCOMPARE(e.send(SCI_STYLEGETBOLD, SCE_C_WORD), sptr_t(1));
        QCOMPARE(e.send(SCI_GETSUBSTYLESLENGTH, SCE_C_IDENTIFIER), sptr_t(8));

        LanguageApplyOptions o;
        o.override.fg = true; // global foreground override -> keywords use the default style colour
        applyLanguage(ed, L_CPP, fx().langs, fx().styles, o);
        QCOMPARE(e.send(SCI_STYLEGETFORE, SCE_C_WORD), e.send(SCI_STYLEGETFORE, STYLE_DEFAULT));
    }

    // Reference behaviour: tokens styled like upstream Notepad++ (same lexers, same keyword set
    // mapping as the upstream setXxxLexer functions, same langs/stylers model files).
    void referenceTokenStyles_data()
    {
        QTest::addColumn<int>("lang");
        QTest::addColumn<QByteArray>("code");
        QTest::addColumn<int>("pos");
        QTest::addColumn<int>("style");
        auto row = [](const char* name, LangType l, const char* code, int pos, int style) {
            QTest::newRow(name) << static_cast<int>(l) << QByteArray(code) << pos << style;
        };
        row("cpp type1", L_CPP, "int x; return 0;", 0, SCE_C_WORD2);
        row("cpp instre1", L_CPP, "int x; return 0;", 7, SCE_C_WORD);
        row("c", L_C, "int x;", 0, SCE_C_WORD2);
        row("java", L_JAVA, "int a;", 0, SCE_C_WORD2);
        row("cs", L_CS, "int a;", 0, SCE_C_WORD2);
        row("swift", L_SWIFT, "func a() {}", 0, SCE_C_WORD);
        row("go", L_GOLANG, "func main() {}", 0, SCE_C_WORD);
        row("python instre1", L_PYTHON, "def f():\n    print(1)", 0, SCE_P_WORD);
        row("python instre2", L_PYTHON, "def f():\n    print(1)", 13, SCE_P_WORD2);
        row("js function", L_JAVASCRIPT, "function f() { const x = new Object(); }", 0, SCE_C_WORD);
        row("js const", L_JAVASCRIPT, "function f() { const x = new Object(); }", 15, SCE_C_WORD);
        row("js type1", L_JAVASCRIPT, "function f() { const x = new Object(); }", 29, SCE_C_WORD2);
        row("typescript", L_TYPESCRIPT, "interface A {}", 0, SCE_C_WORD);
        row("rust", L_RUST, "fn main() {}", 0, SCE_RUST_WORD);
        row("ruby", L_RUBY, "def a\nend", 0, SCE_RB_WORD);
        row("perl", L_PERL, "my $x;", 0, SCE_PL_WORD);
        row("batch", L_BATCH, "echo hi", 0, SCE_BAT_WORD);
        row("bash", L_BASH, "echo hi", 0, SCE_SH_WORD);
        row("sql", L_SQL, "select 1", 0, SCE_SQL_WORD);
        row("html tag", L_HTML, "<div class=\"a\"></div>", 1, SCE_H_TAG);
        row("html attribute", L_HTML, "<div class=\"a\"></div>", 5, SCE_H_ATTRIBUTE);
        row("html embedded js", L_HTML, "<script>var x;</script>", 8, SCE_HJ_KEYWORD);
        row("php", L_PHP, "<?php echo 1; ?>", 6, SCE_HPHP_WORD);
        row("xml", L_XML, "<a b=\"1\"/>", 1, SCE_H_TAG);
        row("json", L_JSON, "{\"a\": true}", 6, SCE_JSON_KEYWORD);
        row("powershell", L_POWERSHELL, "function f {}", 0, SCE_POWERSHELL_KEYWORD);
        row("tcl", L_TCL, "proc a {} {}", 0, SCE_TCL_WORD);
        row("css", L_CSS, "a { color: red; }", 4, SCE_CSS_IDENTIFIER);
        row("ini", L_INI, "[sec]\na=1", 0, SCE_PROPS_SECTION);
        row("vb", L_VB, "dim a", 0, SCE_B_KEYWORD);
        row("pascal", L_PASCAL, "begin end", 0, SCE_PAS_WORD);
        row("haskell", L_HASKELL, "module A where", 9, SCE_HA_KEYWORD);
        row("yaml", L_YAML, "a: true", 3, SCE_YAML_KEYWORD);
    }

    void referenceTokenStyles()
    {
        QFETCH(int, lang);
        QFETCH(QByteArray, code);
        QFETCH(int, pos);
        QFETCH(int, style);
        ScintillaEditBase e;
        SciAdapter ed(e);
        e.send(SCI_SETTEXT, 0, reinterpret_cast<sptr_t>(code.constData()));
        applyLanguage(ed, static_cast<LangType>(lang), fx().langs, fx().styles);
        QCOMPARE(static_cast<int>(e.send(SCI_GETSTYLEAT, pos)), style);
    }

    void langsXmlRoundTrip()
    {
        LanguageRegistry r = fx().langs;
        LanguageRegistry back;
        QVERIFY(back.loadFromString(r.saveToString()));
        QCOMPARE(back.languages().size(), r.languages().size());
        for (const auto& l : r.languages())
            QVERIFY(back.find(l.type)->keywords == l.keywords);
        r.findMutable(L_CPP)->keywords[0] += " myKeyword";
        r.findMutable(L_CPP)->commentLine = "##";
        QVERIFY(back.loadFromString(r.saveToString()));
        QVERIFY(back.wordList(L_CPP, 0).find("myKeyword") != std::string::npos);
        QCOMPARE(back.find(L_CPP)->commentLine, std::string("##"));
        r.addUserExtensions(L_CPP, "zzz"); // stylers.xml user extensions stay out of langs.xml
        QVERIFY(r.saveToString().find("zzz") == std::string::npos);
    }

    void udlRoundTrip()
    {
        UserLanguageRegistry udls;
        udls.loadAll({}, NPP_ROOT_DIR "/PowerEditor/bin/userDefineLangs");
        const std::string xml = userLanguagesToXml(udls.languages());
        UserLanguageRegistry back;
        QCOMPARE(back.loadFromString(xml), static_cast<int>(udls.languages().size()));
        for (const auto& u : udls.languages()) {
            const UserLanguage* b = back.find(u.name);
            QVERIFY(b);
            QCOMPARE(b->ext, u.ext);
            QVERIFY(b->keywordLists == u.keywordLists);
            QVERIFY(b->isPrefix == u.isPrefix);
            QCOMPARE(b->caseIgnored, u.caseIgnored);
            for (const Style& s : u.styles) {
                QCOMPARE(b->style(s.styleId)->fg, s.fg);
                QCOMPARE(b->style(s.styleId)->fontStyle, s.fontStyle);
                QCOMPARE(b->style(s.styleId)->nesting, s.nesting);
            }
        }
        QCOMPARE(userLanguagesToXml(back.languages()), xml);
    }

    void userDefinedLanguage()
    {
        UserLanguageRegistry udls;
        const int n = udls.loadAll({}, NPP_ROOT_DIR "/PowerEditor/bin/userDefineLangs");
        QCOMPARE(n, 2);
        const UserLanguage* md = udls.find("Markdown (preinstalled)");
        QVERIFY(md);
        QVERIFY(!md->ext.empty());
        QCOMPARE(md->styles.size(), size_t(kUdlStyleCount));
        QVERIFY(udls.findForFile("/x/readme.md") != nullptr);

        const UserLexerConfig cfg = buildUserLexerConfig(*md);
        QCOMPARE(cfg.keywordSets.size(), size_t(kUdlKeywordListCount - 13)); // 13 lists go to properties

        ScintillaEditBase e;
        SciAdapter ed(e);
        e.send(SCI_SETTEXT, 0, reinterpret_cast<sptr_t>("# Title\n\n*emphasis* and **strong**\n"));
        applyUserLanguage(ed, *md, fx().styles);
        QCOMPARE(e.send(SCI_GETLEXER), sptr_t(SCLEX_USER));
        QVERIFY(e.send(SCI_GETSTYLEAT, 0) != 0); // the heading is styled by the UDL
    }
};

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    TestLang t;
    return QTest::qExec(&t, argc, argv);
}

#include "test_lang.moc"
