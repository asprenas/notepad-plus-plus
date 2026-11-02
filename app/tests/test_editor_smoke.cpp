// Phase-0 smoke tests: Scintilla Qt + Boost regex + Lexilla + upstream model files.
// Runs headless (QT_QPA_PLATFORM=offscreen is set by CTest).

#include "editor/LanguageConfig.h"

#include <ILexer.h>
#include <Lexilla.h>
#include <Scintilla.h>
#include <ScintillaEditBase.h>

#include <QApplication>

#include <cstdio>
#include <cstring>
#include <string>

namespace {

int g_failures = 0;

void check(bool ok, const char* what)
{
    std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok)
        ++g_failures;
}

sptr_t search(ScintillaEditBase& e, const char* pattern)
{
    e.send(SCI_SETSEARCHFLAGS, SCFIND_REGEXP);
    e.send(SCI_SETTARGETRANGE, 0, e.send(SCI_GETLENGTH));
    return e.send(SCI_SEARCHINTARGET, std::strlen(pattern), reinterpret_cast<sptr_t>(pattern));
}

} // namespace

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);

    ScintillaEditBase e;
    e.send(SCI_SETCODEPAGE, SC_CP_UTF8);
    const char text[] = "int value = 42;\n// 한글 123 테스트\n";
    e.send(SCI_SETTEXT, 0, reinterpret_cast<sptr_t>(text));
    check(e.send(SCI_GETLENGTH) == static_cast<sptr_t>(std::strlen(text)), "UTF-8 text round-trip length");

    // Lookahead is supported by Boost.Regex but not by Scintilla's built-in RESearch:
    // a match proves SCI_OWNREGEX + boostregex is wired in.
    const sptr_t pos = search(e, "\\d+(?=;)");
    check(pos == 12 && e.send(SCI_GETTARGETEND) == 14, "Boost regex lookahead (\\d+(?=;))");

    // Unicode-aware match on Korean text.
    check(search(e, "한글\\s+\\d+") >= 0, "Boost regex on UTF-8 Korean text");

    // Lexilla static linkage.
    Scintilla::ILexer5* lexer = CreateLexer("cpp");
    check(lexer != nullptr, "Lexilla CreateLexer(\"cpp\")");
    Scintilla::ILexer5* udl = CreateLexer("user");
    check(udl != nullptr, "Lexilla CreateLexer(\"user\") (Notepad++ UDL lexer)");
    if (udl)
        udl->Release();
    if (lexer)
        lexer->Release();

    // Upstream model files
    npp::ui::LanguageConfig cfg;
    std::string error;
    const bool loaded = cfg.load(NPP_TEST_DATA_DIR, &error);
    check(loaded, ("load langs/stylers model files " + error).c_str());
    if (loaded) {
        check(cfg.languageForFile("/tmp/a.cpp") == "cpp", "extension .cpp -> cpp");
        check(cfg.languageForFile("/tmp/b.PY") == "python", "extension .PY -> python (case-insensitive)");
        check(cfg.languageForFile("/tmp/c.unknownext") == "normal", "unknown extension -> normal");
        check(npp::ui::LanguageConfig::lexerNameFor("html") == "hypertext", "html -> hypertext lexer");

        // Same result as upstream Notepad++: "int" is a type1 keyword -> SCE_C_WORD2 (TYPE WORD),
        // "return" an instre1 keyword -> SCE_C_WORD (INSTRUCTION WORD).
        const char code[] = "int f() { return 0; }";
        e.send(SCI_SETTEXT, 0, reinterpret_cast<sptr_t>(code));
        cfg.apply(&e, "cpp");
        check(e.send(SCI_GETSTYLEAT, 0) == 16 /* SCE_C_WORD2 */, "apply(cpp): 'int' styled as TYPE WORD");
        check(e.send(SCI_GETSTYLEAT, 10) == 5 /* SCE_C_WORD */, "apply(cpp): 'return' styled as INSTRUCTION WORD");
    }

    std::printf("%d failure(s)\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
