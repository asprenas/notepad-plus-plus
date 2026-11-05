// Auto-completion / call tips / auto-insert (core/editor/AutoCompletion, port of upstream).
#include "core/editor/AutoCompletion.h"
#include "core/editor/IEditor.h"
#include "core/util/PathUtil.h"

#include <SciLexer.h>
#include <Scintilla.h>
#include <ScintillaEditBase.h>

#include <QTemporaryDir>
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

const char* kApi = R"XML(<?xml version="1.0"?>
<NotepadPlus><AutoComplete language="Test">
  <Environment ignoreCase="yes" startFunc="(" stopFunc=")" paramSeparator="," terminal=";"/>
  <KeyWord name="printf" func="yes">
    <Overload retVal="int" descr="formatted output"><Param name="const char* format"/><Param name="..."/></Overload>
  </KeyWord>
  <KeyWord name="pow" func="yes">
    <Overload retVal="double"><Param name="double x"/><Param name="double y"/></Overload>
    <Overload retVal="float"><Param name="float x"/><Param name="float y"/><Param name="float z"/></Overload>
  </KeyWord>
  <KeyWord name="private"/>
</AutoComplete></NotepadPlus>)XML";

} // namespace

class TestAutoCompletion : public QObject {
    Q_OBJECT
    ScintillaEditBase* m_ed = nullptr;
    std::unique_ptr<IEditor> m_e;
    std::unique_ptr<AutoCompletion> m_ac;
    QTemporaryDir m_dir;

    void setText(const char* t, intptr_t caret = -1)
    {
        m_e->sendStr(SCI_SETTEXT, 0, t);
        m_e->send(SCI_GOTOPOS, static_cast<uintptr_t>(caret < 0 ? m_e->send(SCI_GETLENGTH) : caret));
    }
    std::string callTip() const { return m_e->send(SCI_CALLTIPACTIVE) ? "shown" : "hidden"; }
    // type a character like the user: insert + SCN_CHARADDED processing
    void type(char c, const MatchedPairConf& conf = {})
    {
        const char s[2] = {c, 0};
        m_e->send(SCI_REPLACESEL, 0, reinterpret_cast<intptr_t>(s));
        m_ac->insertMatchedChars(c, conf);
        m_ac->update(c);
    }

private slots:
    void initTestCase()
    {
        m_ed = new ScintillaEditBase;
        m_ed->resize(600, 400);
        m_e = std::make_unique<SciAdapter>(*m_ed);
        m_e->send(SCI_SETCODEPAGE, SC_CP_UTF8);
        m_ac = std::make_unique<AutoCompletion>(*m_e);
        QFile f(m_dir.filePath(QStringLiteral("test.xml")));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(kApi);
        f.close();
        QVERIFY(m_ac->setLanguage(L_CPP, m_dir.path().toStdString(), "test"));
    }
    void cleanupTestCase()
    {
        m_ac.reset();
        m_e.reset();
        delete m_ed;
    }

    void apiFile()
    {
        ApiFile api;
        QVERIFY(api.loadFromString(kApi));
        QCOMPARE(api.keywords.size(), size_t(3));
        QCOMPARE(api.keywords[1].overloads.size(), size_t(2));
        QVERIFY(api.keywords[0].func && !api.keywords[2].func);
    }

    void functionCompletionList()
    {
        setText("pr");
        QVERIFY(m_ac->showApiComplete());
        QVERIFY(m_e->send(SCI_AUTOCACTIVE));
        m_e->send(SCI_AUTOCCOMPLETE);
        QCOMPARE(m_e->text(), std::string("printf"));
    }

    void wordCompletionAutoInserts()
    {
        setText("alphabet beta al");
        QVERIFY(m_ac->showWordComplete(true)); // single candidate: inserted directly
        QCOMPARE(m_e->text(), std::string("alphabet beta alphabet"));
    }

    void callTipAndOverloads()
    {
        setText("x = pow");
        type('(');
        QCOMPARE(callTip(), std::string("shown"));
        type('1');
        type(',');
        type('2');
        type(',');      // third parameter: only the 2nd overload has one
        QCOMPARE(callTip(), std::string("shown"));
        m_ac->callTipClick(2);
        type(')');
        type(';');
        QCOMPARE(callTip(), std::string("hidden"));
    }

    void autoInsertPairs()
    {
        MatchedPairConf conf;
        conf.parentheses = conf.doubleQuotes = conf.htmlXmlTag = true;
        conf.userPairs = {{'<', '>'}};
        setText("");
        type('(', conf);
        QCOMPARE(m_e->text(), std::string("()"));
        type(')', conf); // types over the inserted one
        QCOMPARE(m_e->text(), std::string("()"));
        QCOMPARE(m_e->send(SCI_GETCURRENTPOS), intptr_t(2));
        type(' ', conf);
        type('"', conf);
        QCOMPARE(m_e->text(), std::string("() \"\""));
        type('<', conf); // user pair: only before a blank (here the closing quote follows)
        QCOMPARE(m_e->text(), std::string("() \"<\""));
        m_e->send(SCI_DOCUMENTEND);
        type('<', conf);
        QCOMPARE(m_e->text(), std::string("() \"<\"<>"));
    }

    void closeXmlTag()
    {
        AutoCompletion xml(*m_e);
        xml.setLanguage(L_XML, m_dir.path().toStdString(), "none");
        MatchedPairConf conf;
        conf.htmlXmlTag = true;
        m_e->sendStr(SCI_SETTEXT, 0, "<item a=\"1\"");
        m_e->send(SCI_GOTOPOS, static_cast<uintptr_t>(m_e->send(SCI_GETLENGTH)));
        m_e->send(SCI_REPLACESEL, 0, reinterpret_cast<intptr_t>(">"));
        xml.insertMatchedChars('>', conf);
        QCOMPARE(m_e->text(), std::string("<item a=\"1\"></item>"));
        m_e->sendStr(SCI_SETTEXT, 0, "<br/");
        m_e->send(SCI_GOTOPOS, 4);
        m_e->send(SCI_REPLACESEL, 0, reinterpret_cast<intptr_t>(">"));
        xml.insertMatchedChars('>', conf);
        QCOMPARE(m_e->text(), std::string("<br/>"));
    }
};

QTEST_MAIN(TestAutoCompletion)
#include "test_autocompletion.moc"
