#include "core/config/AppPaths.h"
#include "core/config/ConfigFile.h"
#include "core/config/ContextMenuConfig.h"
#include "core/session/Session.h"
#include "core/shortcut/Shortcuts.h"
#include "core/util/PathUtil.h"

#include <pugixml.hpp>

#include <QTest>

using namespace npp::core;

namespace {

const std::string kSample = NPP_TEST_DATA_DIR "/configs/sample1/";

std::string read(const std::string& p) { return readFileContent(p).value_or(""); }

// Every element / attribute / text of `orig` must exist with the same value in `saved`.
// Siblings are matched by tag + "name" attribute when present, else by position among same tag.
bool containsXml(const pugi::xml_node& orig, const pugi::xml_node& saved, std::string& where)
{
    for (pugi::xml_attribute a = orig.first_attribute(); a; a = a.next_attribute()) {
        if (std::string(saved.attribute(a.name()).value()) != a.value() || !saved.attribute(a.name())) {
            where = std::string(orig.name()) + "@" + a.name();
            return false;
        }
    }
    const pugi::xml_node t = orig.first_child();
    if (t && t.type() == pugi::node_pcdata && std::string(t.value()) != saved.first_child().value()) {
        where = std::string(orig.name()) + " text";
        return false;
    }
    std::map<std::string, int> seen;
    for (pugi::xml_node c = orig.first_child(); c; c = c.next_sibling()) {
        if (c.type() != pugi::node_element)
            continue;
        pugi::xml_node match;
        if (const char* nm = c.attribute("name").value(); *nm) {
            for (pugi::xml_node s = saved.child(c.name()); s; s = s.next_sibling(c.name()))
                if (std::string(s.attribute("name").value()) == nm) { match = s; break; }
        } else {
            int idx = seen[c.name()]++;
            for (match = saved.child(c.name()); match && idx > 0; --idx)
                match = match.next_sibling(c.name());
        }
        if (!match) {
            where = std::string("missing <") + c.name() + ">";
            return false;
        }
        if (!containsXml(c, match, where))
            return false;
    }
    return true;
}

} // namespace

class TestConfig : public QObject {
    Q_OBJECT
private slots:
    void configParse()
    {
        ConfigFile cfg;
        QVERIFY(cfg.loadFromString(read(kSample + "config.xml")));
        const GuiConfig& g = cfg.gui;
        QCOMPARE(g.toolbarIconSet, std::string("small"));
        QVERIFY(!g.statusBarShow);
        QVERIFY(!g.splitterVertical);
        QVERIFY(!g.userDefineDlgDocked);
        QVERIFY(g.tabReplacedBySpace);
        QCOMPARE(g.tabSize, 2);
        QVERIFY(g.tabDoubleClickToClose);
        QCOMPARE(g.appWidth, 1400);
        QVERIFY(g.appMaximized);
        QCOMPARE(g.fileAutoDetection, cdEnabledNew | cdAutoUpdate | cdGo2end);
        QCOMPARE(g.maintainIndent, 1);
        QCOMPARE(g.newDocFormat, 2);
        QCOMPARE(g.autocFromLen, 3);
        QVERIFY(g.autoInsertParentheses);
        QVERIFY(!g.autoInsertBrackets);
        QCOMPARE(g.autoInsertUserPairs.size(), size_t(1));
        QCOMPARE(g.autoInsertUserPairs[0], std::make_pair('<', '>'));
        QCOMPARE(g.largeFileSizeMB, 300);
        QVERIFY(g.darkModeEnabled);
        QCOMPARE(g.darkThemeName, std::string("Zenburn.xml"));
        QVERIFY(g.darkFollowSystem);
        QCOMPARE(g.colorPickerCustomColors[1], 65280);
        // ScintillaPrimaryView
        QVERIFY(g.lineNumberMargin);
        QVERIFY(!g.bookmarkMargin);
        QCOMPARE(g.folderMarkStyle, std::string("circle"));
        QCOMPARE(g.lineWrapMethod, std::string("indent"));
        QVERIFY(g.wrap);
        QVERIFY(g.whiteSpaceShow);
        QCOMPARE(g.edgeMultiColumnPos, std::string("80 120"));
        QCOMPARE(g.zoom, 2);
        // defaults for entries absent from the file
        QVERIFY(g.smartHighlight);
        QCOMPARE(g.caretBlinkRate, 600);
        QCOMPARE(g.uriSchemes.substr(0, 6), std::string("svn://"));

        QCOMPARE(cfg.history.maxFiles, 15);
        QCOMPARE(cfg.history.files.size(), size_t(2));
        QCOMPARE(cfg.history.files[0], std::string("C:\\Users\\tester\\Documents\\노트.txt"));
        QCOMPARE(cfg.findHistory.searchMode, 2);
        QVERIFY(cfg.findHistory.matchCase);
        QCOMPARE(cfg.findHistory.finds, (std::vector<std::string>{"한글", "\\d+(?=;)"}));
        QCOMPARE(cfg.findHistory.replaces, (std::vector<std::string>{"$1"}));
    }

    void configRoundTripPreservesEverything()
    {
        const std::string original = read(kSample + "config.xml");
        ConfigFile cfg;
        QVERIFY(cfg.loadFromString(original));
        const std::string saved = cfg.saveToString();

        pugi::xml_document a, b;
        QVERIFY(a.load_string(original.c_str()));
        QVERIFY(b.load_string(saved.c_str()));
        std::string where;
        QVERIFY2(containsXml(a.child("NotepadPlus"), b.child("NotepadPlus"), where), where.c_str());

        // Models are identical after a second load
        ConfigFile again;
        QVERIFY(again.loadFromString(saved));
        QCOMPARE(again.saveToString(), saved);
    }

    void configModifyAndNewFile()
    {
        ConfigFile cfg;
        const std::string path = utf8FromPath(fs::temp_directory_path()) + "/npp-core-config-test.xml";
        fs::remove(pathFromUtf8(path));
        QVERIFY(cfg.load(path));
        QVERIFY(cfg.isNew());
        QCOMPARE(cfg.gui.tabSize, 4); // upstream default
        cfg.gui.tabSize = 8;
        cfg.gui.darkModeEnabled = true;
        cfg.history.add("/tmp/a.txt");
        cfg.history.add("/tmp/b.txt");
        cfg.history.add("/tmp/a.txt"); // moves to front
        FindHistory::push(cfg.findHistory.finds, "needle", cfg.findHistory.maxFind);
        QVERIFY(cfg.save(path));

        ConfigFile back;
        QVERIFY(back.load(path));
        QVERIFY(!back.isNew());
        QCOMPARE(back.gui.tabSize, 8);
        QVERIFY(back.gui.darkModeEnabled);
        QCOMPARE(back.history.files, (std::vector<std::string>{"/tmp/a.txt", "/tmp/b.txt"}));
        QCOMPARE(back.findHistory.finds, (std::vector<std::string>{"needle"}));
        fs::remove(pathFromUtf8(path));
    }

    void autoDetectionStrings()
    {
        for (const char* s : {"yes", "auto", "Update2End", "autoUpdate2End", "yesOld", "autoOld", "Update2EndOld",
                              "autoUpdate2EndOld", "no"})
            QCOMPARE(GuiConfig::fileAutoDetectionToString(GuiConfig::fileAutoDetectionFromString(s)), std::string(s));
    }

    void session()
    {
        const auto s = loadSession(kSample + "session.xml");
        QVERIFY(s.has_value());
        QCOMPARE(s->activeView, 1);
        QCOMPARE(s->mainView.activeIndex, 1);
        QCOMPARE(s->mainView.files.size(), size_t(2));
        const SessionFile& f = s->mainView.files[0];
        QCOMPARE(f.filename, std::string("C:\\src\\main.cpp"));
        QCOMPARE(f.lang, std::string("C++"));
        QCOMPARE(f.startPos, 345LL);
        QCOMPARE(f.originalFileLastModifTimestamp, 3254071808u);
        QVERIFY(f.tabPinned);
        QCOMPARE(f.tabColourId, 2);
        QCOMPARE(f.marks, (std::vector<size_t>{3, 42}));
        QCOMPARE(f.folds, (std::vector<size_t>{7}));
        QCOMPARE(f.map.kByteInDoc, 512LL);
        QCOMPARE(f.extraAttributes.at("futureAttr"), std::string("x"));
        QVERIFY(s->mainView.files[1].untitledTabRenamed);
        QCOMPARE(s->mainView.files[1].encoding, 949);
        QCOMPARE(s->subView.files[0].filename, std::string("/home/tester/한글 경로/script.py"));
        QVERIFY(s->subView.files[0].userReadOnly);
        QCOMPARE(s->fileBrowserRoots.size(), size_t(1));
        QCOMPARE(s->fileBrowserRoots[0].expanded, (std::vector<std::string>{"C:\\src\\lib"}));

        // round trip
        const std::string out = serializeSession(*s);
        const auto again = parseSession(out);
        QVERIFY(again.has_value());
        QCOMPARE(serializeSession(*again), out);
        pugi::xml_document a, b;
        QVERIFY(a.load_string(read(kSample + "session.xml").c_str()));
        QVERIFY(b.load_string(out.c_str()));
        std::string where;
        QVERIFY2(containsXml(a.child("NotepadPlus"), b.child("NotepadPlus"), where), where.c_str());
    }

    void shortcuts()
    {
        ShortcutsFile sc;
        QVERIFY(sc.load(kSample + "shortcuts.xml"));
        QCOMPARE(sc.internalCommands.size(), size_t(2));
        QCOMPARE(sc.internalCommands[0].id, 41001);
        QVERIFY(sc.internalCommands[0].combo.ctrl && sc.internalCommands[0].combo.alt);
        QCOMPARE(sc.internalCommands[1].nth, 1);
        QCOMPARE(sc.macros.size(), size_t(2));
        QCOMPARE(sc.macros[0].steps.size(), size_t(2));
        // Old macro: "a", CR, LF, "b" -> "a", NEWLINE, "b"
        const auto& old = sc.macros[1];
        QCOMPARE(old.folder, std::string("Legacy"));
        QCOMPARE(old.steps.size(), size_t(3));
        QCOMPARE(old.steps[1].message, 2329);
        QCOMPARE(sc.userCommands[0].command, std::string("https://www.google.com/search?q=$(CURRENT_WORD)"));
        QCOMPARE(sc.userCommands[0].folder, std::string("Web"));
        QCOMPARE(sc.pluginCommands[0].moduleName, std::string("ComparePlugin.dll"));
        QCOMPARE(sc.scintillaKeys[0].combos.size(), size_t(2));
        QVERIFY(sc.scintillaKeys[0].combos[1].shift);

        ShortcutsFile again;
        QVERIFY(again.loadFromString(sc.saveToString()));
        QCOMPARE(again.saveToString(), sc.saveToString());
    }

    void keyConversion()
    {
        const KeyCombo ctrlShiftF{true, false, true, 0x46};
        const auto q = toQtKey(ctrlShiftF);
        QVERIFY(q.has_value());
        QCOMPARE(q->key(), Qt::Key_F);
        QVERIFY(q->keyboardModifiers().testFlag(Qt::ControlModifier));
        QCOMPARE(fromQtKey(*q).value(), ctrlShiftF);
        QCOMPARE(keyComboToString(ctrlShiftF), std::string("Ctrl+Shift+F"));

        // every VK code that maps to Qt must map back
        int mapped = 0;
        for (int vk = 1; vk < 256; ++vk) {
            const KeyCombo k{false, true, false, vk};
            if (const auto qk = toQtKey(k)) {
                ++mapped;
                const auto back = fromQtKey(*qk);
                QVERIFY2(back.has_value() && back->key == vk, qPrintable(QString::number(vk)));
            }
        }
        QVERIFY(mapped > 90);
        QCOMPARE(toQtKey({false, false, false, 0x6B})->keyboardModifiers().testFlag(Qt::KeypadModifier), true);
        QCOMPARE(keyComboToString({false, false, false, 0x70}), std::string("F1"));
    }

    void contextMenu()
    {
        ContextMenuConfig cm;
        QVERIFY(cm.load(kSample + "contextMenu.xml"));
        QCOMPARE(cm.items.size(), size_t(6));
        QCOMPARE(cm.items[0].menuEntryName, std::string("Edit"));
        QVERIFY(cm.items[2].isSeparator());
        QCOMPARE(cm.items[3].folderName, std::string("Style token"));
        QCOMPARE(cm.items[3].itemNameAs, std::string("Using 1st Style"));
        QCOMPARE(cm.items[4].pluginCommandItemName, std::string("Base64 Encode"));
        QCOMPARE(cm.items[5].extraAttributes.at("customFlag"), std::string("kept"));
        ContextMenuConfig again;
        QVERIFY(again.loadFromString(cm.saveToString()));
        QCOMPARE(again.saveToString(), cm.saveToString());
    }

    void appPaths()
    {
        const std::string base = utf8FromPath(fs::temp_directory_path()) + "/npp-core-paths";
        { std::error_code ec_; fs::remove_all(pathFromUtf8(base), ec_); }
        const std::string appDir = base + "/app", userDir = base + "/user", cloudDir = base + "/cloud",
                          settingsDir = base + "/settings";
        for (const auto& d : {appDir, userDir, cloudDir, settingsDir})
            fs::create_directories(pathFromUtf8(d));

        AppPaths::Options o;
        o.appDir = appDir;
        o.userDirOverride = userDir;
        AppPaths p = AppPaths::resolve(o);
        QCOMPARE(p.userDir(), userDir);
        QVERIFY(!p.isPortable());
        QVERIFY(pathExists(p.pluginConfigDir(), PathKind::Directory));
        QCOMPARE(p.configXml(), pathAppend(userDir, "config.xml"));

        // cloud
        fs::create_directories(pathFromUtf8(userDir + "/cloud"));
        QVERIFY(writeFileContent(userDir + "/cloud/choice", cloudDir));
        p = AppPaths::resolve(o);
        QVERIFY(p.isCloud());
        QCOMPARE(p.userDir(), cloudDir);
        QCOMPARE(p.sessionDir(), userDir); // session never on cloud

        // portable
        QVERIFY(writeFileContent(appDir + "/doLocalConf.xml", ""));
        o.userDirOverride.clear();
        p = AppPaths::resolve(o);
        QVERIFY(p.isPortable());
        QCOMPARE(p.userDir(), appDir);

        // -settingsDir wins
        o.settingsDirArg = settingsDir;
        p = AppPaths::resolve(o);
        QCOMPARE(p.userDir(), settingsDir);
        QCOMPARE(p.sessionDir(), settingsDir);
        { std::error_code ec_; fs::remove_all(pathFromUtf8(base), ec_); }
    }
};

QTEST_GUILESS_MAIN(TestConfig)
#include "test_config.moc"
