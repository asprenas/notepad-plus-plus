#include "core/util/PathUtil.h"
#include "core/util/StringUtil.h"

#include <QTest>

using namespace npp::core;

class TestUtil : public QObject {
    Q_OBJECT
private slots:
    void caseHelpers()
    {
        QVERIFY(iequalsAscii("Hello", "hELLO"));
        QVERIFY(!iequalsAscii("Hello", "Hell"));
        QVERIFY(istartsWithAscii("UTF-8", "utf"));
        QVERIFY(iendsWithAscii("file.CPP", ".cpp"));
        QCOMPARE(utf8Lower("ÄÖÜ ABC"), std::string("äöü abc"));
        QCOMPARE(utf8Upper("straße"), std::string("STRASSE"));
        QVERIFY(iequalsUtf8("Ελληνικά", "ΕΛΛΗΝΙΚΆ"));
    }

    void utf8Validation()
    {
        QVERIFY(isValidUtf8("plain ascii"));
        QVERIFY(isValidUtf8("한글 😀"));
        QVERIFY(!isValidUtf8("\xC3"));             // truncated
        QVERIFY(!isValidUtf8("\xC0\xAF"));         // overlong '/'
        QVERIFY(!isValidUtf8("\xED\xA0\x80"));     // UTF-16 surrogate
        QVERIFY(!isValidUtf8("\xF4\x90\x80\x80")); // > U+10FFFF
        QCOMPARE(utf8CodePointCount("한글😀a"), size_t(4));
        std::string s;
        appendUtf8(s, U'한');
        appendUtf8(s, U'😀');
        appendUtf8(s, 0xD800); // invalid -> U+FFFD
        QCOMPARE(s, std::string("한😀\xEF\xBF\xBD"));
    }

    void splitJoinTrim()
    {
        QCOMPARE(split("a,,b", ","), (std::vector<std::string>{"a", "", "b"}));
        QCOMPARE(tokenize("  a  b ", ' '), (std::vector<std::string>{"a", "b"}));
        QCOMPARE(join({"x", "y", "z"}, ", "), std::string("x, y, z"));
        QCOMPARE(trimmed(" \t text \t"), std::string("text"));
        QCOMPARE(replaceAll("aaa", "a", "bb"), std::string("bbbbbb"));
        QCOMPARE(takeWhileAdmissible("123abc", "0123456789"), std::string("123"));
    }

    void listsAndNumbers()
    {
        QVERIFY(isInList("UTF8", "utf-8 utf8"));
        QVERIFY(!isInList("utf", "utf-8 utf8"));
        std::vector<size_t> v;
        QVERIFY(parseNumberList("4 8  12", v));
        QCOMPARE(v, (std::vector<size_t>{4, 8, 12}));
        QVERIFY(!parseNumberList("4,8", v));
        QCOMPARE(parseInt(" -42 ").value(), -42LL);
        QVERIFY(!parseInt("4x").has_value());
        QVERIFY(isNumber("-12"));
        QVERIFY(!isNumber("1.5"));
        QCOMPARE(nbDigitsFromNbLines(9), 1);
        QCOMPARE(nbDigitsFromNbLines(10), 2);
        QCOMPARE(nbDigitsFromNbLines(12'000'000), 8);
        QCOMPARE(commafy(1234567), std::string("1,234,567"));
    }

    void menuStrings()
    {
        QCOMPARE(purgeMenuItemString("&Open...\tCtrl+O"), std::string("Open..."));
        QCOMPARE(purgeMenuItemString("&Open", true), std::string("&Open"));
        QCOMPARE(buildMenuFileName(0, 0, "/tmp/a&b.txt"), std::string("&1: a&&b.txt"));
        QCOMPARE(buildMenuFileName(0, 9, "/tmp/x.txt"), std::string("1&0: x.txt"));
        QCOMPARE(buildMenuFileName(0, 11, "/tmp/x.txt"), std::string("1&2: x.txt"));
        QCOMPARE(buildMenuFileName(0, 3, "/tmp/x.txt", false), std::string("&x.txt"));
    }

    void paths()
    {
        QCOMPARE(fileNameOf("dir/sub/file.tar.gz"), std::string("file.tar.gz"));
        QCOMPARE(extensionOf("dir/file.CPP"), std::string("CPP"));
        QCOMPARE(extensionOf("Makefile"), std::string());
        QCOMPARE(utf8FromPath(pathFromUtf8("폴더/파일.txt")), std::string("폴더/파일.txt"));
        QCOMPARE(parentPath("a/b/c.txt"), std::string("a/b"));
#if defined(Q_OS_WIN)
        QCOMPARE(pathAppend("C:\\dir", "file"), std::string("C:\\dir\\file"));
        QCOMPARE(pathAppend("C:\\dir\\", "\\file"), std::string("C:\\dir\\file"));
#else
        QCOMPARE(pathAppend("/dir", "file"), std::string("/dir/file"));
        QCOMPARE(pathAppend("/dir/", "/file"), std::string("/dir/file"));
#endif
        QVERIFY(isUncPath("\\\\server\\share"));
        QVERIFY(!isUncPath("C:\\dir"));
    }

    void wildcards()
    {
        QVERIFY(wildcardMatch("main.cpp", "*.cpp"));
        QVERIFY(wildcardMatch("MAIN.CPP", "*.cpp"));
        QVERIFY(wildcardMatch("main.h", "*.cpp;*.h"));
        QVERIFY(wildcardMatch("a1.txt", "a?.txt"));
        QVERIFY(!wildcardMatch("a12.txt", "a?.txt"));
        QVERIFY(wildcardMatch("anything", "*"));
        QVERIFY(matchInList("x.cpp", {"*.cpp", "*.h"}));
        QVERIFY(!matchInList("x_test.cpp", {"*.cpp", "!*_test.cpp"}));
        QVERIFY(matchInExcludeDirList("node_modules", {"*.js", "!+\\node_modules"}, 3));
        QVERIFY(matchInExcludeDirList("build", {"!/build"}, 1));
        QVERIFY(!matchInExcludeDirList("build", {"!/build"}, 2));
        QVERIFY(allPatternsAreExclusion({"!*.o", "!*.obj"}));
        QVERIFY(!allPatternsAreExclusion({"!*.o", "*.cpp"}));
    }

    void fileContentAndExistence()
    {
        const std::string dir = utf8FromPath(fs::temp_directory_path()) + "/npp-core-test-한글";
        fs::create_directories(pathFromUtf8(dir));
        const std::string file = dir + "/내용.txt";
        QVERIFY(writeFileContent(file, std::string("a\0b", 3)));
        QCOMPARE(readFileContent(file).value(), std::string("a\0b", 3));
        QVERIFY(pathExists(file, PathKind::File));
        QVERIFY(pathExists(dir, PathKind::Directory));
        QVERIFY(!pathExists(file, PathKind::Directory));
        bool timedOut = true;
        QVERIFY(pathExists(file, PathKind::Any, std::chrono::milliseconds(2000), &timedOut));
        QVERIFY(!timedOut);
        { std::error_code ec_; fs::remove_all(pathFromUtf8(dir), ec_); }
        QVERIFY(!pathExists(file));
    }

    void environment()
    {
        qputenv("NPP_TEST_VAR", "value");
        QCOMPARE(expandEnvironment("%NPP_TEST_VAR%/x"), std::string("value/x"));
        QCOMPARE(expandEnvironment("%NPP_UNDEFINED_VAR%"), std::string("%NPP_UNDEFINED_VAR%"));
#if !defined(Q_OS_WIN)
        QCOMPARE(expandEnvironment("$NPP_TEST_VAR/${NPP_TEST_VAR}"), std::string("value/value"));
#endif
    }

    void compacting()
    {
        const std::string longPath = "/very/long/directory/structure/for/testing/file.txt";
        const std::string c = compactPath(longPath, 30);
        QVERIFY(utf8CodePointCount(c) <= 30);
        QVERIFY(c.find("...") != std::string::npos);
        QVERIFY(c.size() >= 8 && c.compare(c.size() - 8, 8, "file.txt") == 0);
        QCOMPARE(compactPath("/short.txt", 30), std::string("/short.txt"));
    }
};

QTEST_GUILESS_MAIN(TestUtil)
#include "test_util.moc"
