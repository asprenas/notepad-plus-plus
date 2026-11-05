// Function List: upstream PowerEditor/Test/FunctionList unit tests (unitTest + unitTest.expected.result
// per language folder and sub folder), same comparison as upstream unitTest.ps1.
#include "core/buffer/FileManager.h"
#include "core/funclist/FunctionParser.h"
#include "core/lang/LanguageRegistry.h"
#include "core/util/PathUtil.h"

#include <Scintilla.h>
#include <ScintillaEditBase.h>

#include <QApplication>
#include <QElapsedTimer>
#include <QTest>

#include <fstream>
#include <sstream>

using namespace npp::core;

namespace {

class SciAdapter final : public IEditor {
public:
    explicit SciAdapter(ScintillaEditBase& e) : m_e(e) {}
    intptr_t send(unsigned int m, uintptr_t w, intptr_t l) const override { return m_e.send(m, w, l); }
private:
    ScintillaEditBase& m_e;
};

const std::string kTests = NPP_ROOT_DIR "/PowerEditor/Test/FunctionList";
const std::string kRules = NPP_ROOT_DIR "/PowerEditor/installer/functionList";
const std::string kTestRules = NPP_ROOT_DIR "/PowerEditor/installer/filesForTesting";

std::string readAll(const fs::path& p)
{
    std::ifstream f(p, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

// unitTest.ps1: Get-Content (lines without EOL), '\r\n' -> '\n' (the JSON escapes)
std::string normalized(std::string s)
{
    std::string out;
    std::istringstream in(s);
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        if (!out.empty())
            out += '\n';
        out += line;
    }
    for (size_t p = 0; (p = out.find("\\r\\n", p)) != std::string::npos;)
        out.replace(p, 4, "\\n");
    return out;
}

} // namespace

class TestFunctionList : public QObject {
    Q_OBJECT
private:
    ScintillaEditBase* m_ed = nullptr;
    std::unique_ptr<IEditor> m_e;
    LanguageRegistry m_langs;
    std::string m_rulesDir;

private slots:
    void initTestCase()
    {
        m_ed = new ScintillaEditBase;
        m_e = std::make_unique<SciAdapter>(*m_ed);
        // upstream localUnitestLauncher.ps1: installed rules + regexGlobalTest.xml (UDL "regexGlobalTest")
        m_rulesDir = utf8FromPath(fs::temp_directory_path()) + "/npp-funclist-"
                     + std::to_string(QCoreApplication::applicationPid());
        fs::remove_all(pathFromUtf8(m_rulesDir));
        fs::copy(pathFromUtf8(kRules), pathFromUtf8(m_rulesDir), fs::copy_options::recursive);
        fs::copy_file(pathFromUtf8(kTestRules + "/regexGlobalTest.xml"), pathFromUtf8(m_rulesDir + "/regexGlobalTest.xml"));
        std::string map = readAll(pathFromUtf8(m_rulesDir + "/overrideMap.xml"));
        const size_t at = map.find("</associationMap>");
        QVERIFY(at != std::string::npos);
        map.insert(at, "<association id=\"regexGlobalTest.xml\" userDefinedLangName=\"regexGlobalTest\"/>\n");
        std::ofstream(pathFromUtf8(m_rulesDir + "/overrideMap.xml"), std::ios::binary) << map;
    }
    void cleanupTestCase()
    {
        m_e.reset();
        delete m_ed;
        std::error_code ec;
        fs::remove_all(pathFromUtf8(m_rulesDir), ec);
    }

    void upstreamUnitTests_data()
    {
        QTest::addColumn<QString>("dir");
        QTest::addColumn<QString>("lang");
        int count = 0;
        for (const auto& d : fs::directory_iterator(pathFromUtf8(kTests))) {
            if (!d.is_directory())
                continue;
            const std::string lang = utf8FromPath(d.path().filename());
            auto add = [&](const fs::path& dir, const std::string& name) {
                if (!fs::exists(dir / "unitTest"))
                    return;
                QTest::newRow(name.c_str()) << QString::fromStdString(utf8FromPath(dir)) << QString::fromStdString(lang);
                ++count;
            };
            add(d.path(), lang);
            for (const auto& sub : fs::directory_iterator(d.path()))
                if (sub.is_directory())
                    add(sub.path(), lang + "-" + utf8FromPath(sub.path().filename()));
        }
        QVERIFY(count >= 42);
    }

    void upstreamUnitTests()
    {
        QFETCH(QString, dir);
        QFETCH(QString, lang);

        FunctionParsersManager mgr;
        QVERIFY(mgr.init(m_rulesDir, {}));

        LangType type = L_TEXT;
        std::string udl;
        if (lang.startsWith("udl-")) {
            type = L_USER;
            udl = lang.mid(4).toStdString();
        } else {
            const auto t = langTypeFromName(lang.toStdString());
            QVERIFY2(t.has_value(), qPrintable(lang));
            type = *t;
        }

        FileManager fm(*m_e, {}, &m_langs);
        Buffer* b = fm.loadFile(dir.toStdString() + "/unitTest");
        QVERIFY(b);
        m_e->send(SCI_SETDOCPOINTER, 0, b->document);
        // Upstream runs these tests on a Windows checkout (core.autocrlf): CRLF documents. Some
        // rules depend on it (bash: a function body ending one character before a comment zone).
        m_e->send(SCI_CONVERTEOLS, SC_EOL_CRLF);
        std::vector<FoundFunction> found;
        const bool ok = mgr.parse(found, *m_e, type, udl);
        m_e->send(SCI_SETDOCPOINTER, 0, 0);
        QVERIFY2(ok, "no parser");

        const std::string got = normalized(functionListJson(found, "unitTest"));
        const std::string expected = normalized(readAll(pathFromUtf8(dir.toStdString() + "/unitTest.expected.result")));
        if (got != expected)
            qWarning().noquote() << "expected:\n" << QString::fromStdString(expected) << "\ngot:\n"
                                 << QString::fromStdString(got);
        QVERIFY(got == expected);
    }

    void cppSmallDocumentLf()
    {
        FunctionParsersManager mgr;
        QVERIFY(mgr.init(m_rulesDir, {}));
        m_e->send(SCI_SETCODEPAGE, SC_CP_UTF8);
        m_e->sendStr(SCI_SETTEXT, 0,
            "int top() { return 1; }\n"
            "class Foo {\npublic:\n    void bar() {}\n    int baz(int x) { return x; }\n};\n"
            "void Foo::qux() {}\n"
            "static void alpha() {}\n");
        std::vector<FoundFunction> found;
        QVERIFY(mgr.parse(found, *m_e, L_CPP));
        QCOMPARE(functionListJson(found, "a"),
                 std::string("{\"leaves\":[\"top\",\"alpha\"],\"nodes\":[{\"leaves\":[\"bar\",\"baz\",\"qux\"],\"name\":\"Foo\"}],\"root\":\"a\"}"));
    }

    void largeFileParseTime()
    {
        // upstream Notepad_plus.cpp (~9000 lines, C++ mix parser): parsed on the UI thread after a
        // 1 s pause in typing, so it has to stay well below a second
        FunctionParsersManager mgr;
        QVERIFY(mgr.init(m_rulesDir, {}));
        const std::string text = readAll(pathFromUtf8(NPP_ROOT_DIR "/PowerEditor/src/Notepad_plus.cpp"));
        QVERIFY(text.size() > 200000);
        m_e->send(SCI_SETCODEPAGE, SC_CP_UTF8);
        m_e->send(SCI_SETTEXT, 0, reinterpret_cast<intptr_t>(text.c_str()));
        QElapsedTimer t;
        t.start();
        std::vector<FoundFunction> found;
        QVERIFY(mgr.parse(found, *m_e, L_CPP));
        const auto ms = t.elapsed();
        qInfo("Notepad_plus.cpp: %d functions in %lld ms", static_cast<int>(found.size()), static_cast<long long>(ms));
        QVERIFY(found.size() > 100);
        QVERIFY2(ms < 3000, qPrintable(QString::number(ms)));
    }

    void jsonEscapesLikeNlohmann()
    {
        std::vector<FoundFunction> f{{"a\"b\\c\td\x01", "", 0, -1}, {"m", "K", 3, 1}, {"n", "K", 5, 1}};
        QCOMPARE(functionListJson(f, "r"),
                 std::string("{\"leaves\":[\"a\\\"b\\\\c\\td\\u0001\"],\"nodes\":[{\"leaves\":[\"m\",\"n\"],\"name\":\"K\"}],\"root\":\"r\"}"));
        QCOMPARE(functionListJson({}, "r"), std::string("{\"root\":\"r\"}"));
    }
};

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    TestFunctionList t;
    return QTest::qExec(&t, argc, argv);
}

#include "test_funclist.moc"
