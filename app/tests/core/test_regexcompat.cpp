// Regex compatibility with the original Notepad++ (phase 3 completion criterion).
// cases.json: tools/regex-compat/gen-cases.py; expected.json: extracted once from the original
// Windows Notepad++ by tools/regex-compat/extract-from-npp.ps1 (same flags and loops).
#include "core/search/Search.h"

#include <Scintilla.h>
#include <ScintillaEditBase.h>

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
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

QJsonDocument load(const char* name)
{
    QFile f(QStringLiteral(NPP_TEST_DATA_DIR "/regex/") + QLatin1String(name));
    if (!f.open(QIODevice::ReadOnly))
        qFatal("cannot open %s", name);
    return QJsonDocument::fromJson(f.readAll());
}

} // namespace

class TestRegexCompat : public QObject {
    Q_OBJECT
    ScintillaEditBase* m_ed = nullptr;
    std::unique_ptr<IEditor> m_e;

private slots:
    void initTestCase()
    {
        initRegexLocale();
        m_ed = new ScintillaEditBase;
        m_e = std::make_unique<SciAdapter>(*m_ed);
        m_e->send(SCI_SETCODEPAGE, SC_CP_UTF8);
    }
    void cleanupTestCase()
    {
        m_e.reset();
        delete m_ed;
    }

    void compat_data()
    {
        QTest::addColumn<QJsonObject>("c");
        QTest::addColumn<QJsonObject>("x");
        const QJsonArray cases = load("cases.json").array();
        const QJsonObject expected = load("expected.json").object();
        qInfo("expected results: %s", qPrintable(expected.value(QStringLiteral("source")).toString()));
        const QJsonArray results = expected.value(QStringLiteral("results")).toArray();
        QCOMPARE(results.size(), cases.size());
        QVERIFY(cases.size() >= 100);
        for (qsizetype i = 0; i < cases.size(); ++i) {
            const QJsonObject c = cases[i].toObject();
            QCOMPARE(results[i].toObject().value(QStringLiteral("name")), c.value(QStringLiteral("name")));
            QTest::newRow(qPrintable(c.value(QStringLiteral("name")).toString())) << c << results[i].toObject();
        }
    }

    void compat()
    {
        QFETCH(QJsonObject, c);
        QFETCH(QJsonObject, x);
        auto str = [&c](const char* k) { return c.value(QLatin1String(k)).toString().toStdString(); };
#if !defined(_WIN32)
        if (c.value(QStringLiteral("windowsCodepageDependent")).toBool())
            QSKIP("result of the original depends on the Windows ANSI code page (Win32 locale traits); Unicode case mapping here");
#endif
        SearchOptions o;
        o.findWhat = str("find");
        o.replaceWith = str("replace");
        const QString mode = c.value(QStringLiteral("mode")).toString();
        o.type = mode == QLatin1String("regex") ? SearchType::Regex : mode == QLatin1String("extended") ? SearchType::Extended : SearchType::Normal;
        o.matchCase = c.value(QStringLiteral("matchCase")).toBool();
        o.wholeWord = c.value(QStringLiteral("wholeWord")).toBool();
        o.dotMatchesNewline = c.value(QStringLiteral("dotMatchesNewline")).toBool();

        // the port's extended conversion gives the strings upstream passes to Scintilla
        QCOMPARE(searchPattern(o), str("engineFind"));
        if (o.type == SearchType::Extended)
            QCOMPARE(convertExtended(o.replaceWith), str("engineReplace"));

        m_e->sendStr(SCI_SETTEXT, 0, str("text"));
        const auto matches = findAll(*m_e, o);
        QJsonArray got;
        for (const SearchMatch& m : matches)
            got.append(QJsonArray{static_cast<qint64>(m.start), static_cast<qint64>(m.end)});
        QCOMPARE(QJsonDocument(got).toJson(QJsonDocument::Compact), QJsonDocument(x.value(QStringLiteral("matches")).toArray()).toJson(QJsonDocument::Compact));

        m_e->sendStr(SCI_SETTEXT, 0, str("text"));
        const int n = replaceAll(*m_e, o);
        QCOMPARE(n, x.value(QStringLiteral("replaceCount")).toInt());
        QCOMPARE(QString::fromStdString(m_e->text()), x.value(QStringLiteral("replaced")).toString());
    }
};

QTEST_MAIN(TestRegexCompat)
#include "test_regexcompat.moc"
