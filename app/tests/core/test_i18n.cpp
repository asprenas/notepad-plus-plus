#include "core/i18n/NativeLanguage.h"

#include <QTest>

using namespace npp::core;

namespace {
const std::string kLangDir = NPP_ROOT_DIR "/PowerEditor/installer/nativeLang";
}

class TestI18n : public QObject {
    Q_OBJECT
private slots:
    void englishIsNotLoadedByDefault()
    {
        NativeLanguage en;
        QVERIFY(!en.loadFile(kLangDir + "/english.xml"));
        QVERIFY(!en.isLoaded());
        QCOMPARE(en.menuCommand(41001, "&New"), std::string("&New")); // falls back
        QVERIFY(en.loadFile(kLangDir + "/english.xml", true));
        QCOMPARE(en.menuCommand(41001), std::string("&New"));
    }

    void korean()
    {
        NativeLanguage ko;
        QVERIFY(ko.loadFile(kLangDir + "/korean.xml"));
        QVERIFY(ko.isLoaded());
        QVERIFY(!ko.isRTL());
        QCOMPARE(ko.fileName(), std::string("korean.xml"));
        QCOMPARE(ko.languageName(), std::string("한국어"));

        QCOMPARE(ko.menuCommand(41001), std::string("새 파일(&N)"));
        QCOMPARE(ko.menuCommand(41001, {}, true), std::string("새 파일"));
        QCOMPARE(ko.menuCommand(999999, "fallback"), std::string("fallback"));
        QVERIFY(!ko.mainMenuEntry("file").empty());
        QVERIFY(!ko.subMenuEntry("file-recentFiles").empty());

        const auto find = ko.dialog("Find");
        QVERIFY(find.has_value());
        QCOMPARE(find->items.at(1), std::string("다음 찾기"));
        QCOMPARE(ko.dialogAttribute("Find", "titleReplace"), std::string("바꾸기"));
        const auto findTabs = ko.dialog("Find", "titleFind");
        QCOMPARE(findTabs->title, std::string("찾기"));

        const auto tabMenu = ko.itemNames({"Menu", "TabBar"}, "CMDID");
        QCOMPARE(tabMenu.at(41003), std::string("닫기"));

        const auto mb = ko.messageBox("NbFileToOpenImportantWarning", "default", "title", 1234);
        QVERIFY(mb.message.find("1234") != std::string::npos);
        QVERIFY(mb.message.find("$INT_REPLACE$") == std::string::npos);

        QCOMPARE(ko.localizedString("tab-untitled-string", "new "), std::string("새로운 "));
        QCOMPARE(ko.localizedString("no-such-id", "x"), std::string("x"));
    }

    void rtl()
    {
        NativeLanguage ar;
        QVERIFY(ar.loadFile(kLangDir + "/arabic.xml"));
        QVERIFY(ar.isRTL());
        QVERIFY(ar.isEditZoneRTL()); // no editZoneRTL attribute -> true (upstream)
        const char xml[] = R"(<NotepadPlus><Native-Langue name="X" filename="x.xml" RTL="yes" editZoneRTL="no"/></NotepadPlus>)";
        NativeLanguage x;
        QVERIFY(x.loadFromString(xml));
        QVERIFY(x.isRTL());
        QVERIFY(!x.isEditZoneRTL());
    }

    void messageBoxDefaults()
    {
        NativeLanguage none;
        const auto t = none.messageBox("Unknown", "Open $STR_REPLACE$ ($INT_REPLACE$)?", "", 3, "a.txt");
        QCOMPARE(t.message, std::string("Open a.txt (3)?"));
        QCOMPARE(t.title, std::string("Error")); // empty title -> common-error
    }

    void purify()
    {
        QCOMPARE(NativeLanguage::purifyMenuString("새 파일(&N)..."), std::string("새 파일"));
        QCOMPARE(NativeLanguage::purifyMenuString("Save &As..."), std::string("Save As"));
        QCOMPARE(NativeLanguage::purifyMenuString("Fish && Chips"), std::string("Fish & Chips"));
    }

    void allUpstreamLanguageFilesLoad()
    {
        const auto langs = NativeLanguage::availableLanguages(kLangDir);
        QVERIFY2(langs.size() >= 90, qPrintable(QString::number(langs.size())));
        int menusTranslated = 0;
        for (const auto& l : langs) {
            NativeLanguage nl;
            const bool loaded = nl.loadFile(l.path, true);
            QVERIFY2(loaded, l.path.c_str());
            if (!nl.menuCommand(41001).empty())
                ++menusTranslated;
        }
        QCOMPARE(menusTranslated, static_cast<int>(langs.size()));
    }
};

QTEST_GUILESS_MAIN(TestI18n)
#include "test_i18n.moc"
