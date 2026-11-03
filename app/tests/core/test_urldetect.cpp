// URL detection against upstream's own test files (PowerEditor/Test/UrlDetection): each
// "u <text> u" line is followed by "m <mask> m" ('1' = position inside a detected URL).
#include "core/editor/UrlDetector.h"

#include <QFile>
#include <QMap>
#include <QStringList>
#include <QTest>

using namespace npp::core;

class TestUrlDetect : public QObject {
    Q_OBJECT
private slots:
    void upstreamCases_data()
    {
        QTest::addColumn<QString>("line");
        QTest::addColumn<QString>("mask");
        int n = 0;
        for (const char* name : {"verifyUrlDetection_1a", "verifyUrlDetection_1b"}) {
            QFile f(QStringLiteral(NPP_ROOT_DIR "/PowerEditor/Test/UrlDetection/") + QLatin1String(name));
            QVERIFY(f.open(QIODevice::ReadOnly));
            QStringList lines = QString::fromUtf8(f.readAll()).split(QLatin1Char('\n'));
            for (QString& l : lines)
                if (l.endsWith(QLatin1Char('\r')))
                    l.chop(1);
            // <name>.expected.result: "KO u <line> u / ok m <wanted> m / ko m <what upstream does> m"
            QMap<QString, QString> upstreamActual;
            QFile r(f.fileName() + QStringLiteral(".expected.result"));
            if (r.open(QIODevice::ReadOnly)) {
                const QStringList rl = QString::fromUtf8(r.readAll()).split(QLatin1Char('\n'));
                for (int i = 0; i + 2 < rl.size(); ++i)
                    if (rl[i].startsWith(QLatin1String("KO\t")) && rl[i + 2].startsWith(QLatin1String("ko\t")))
                        upstreamActual[rl[i].mid(3).trimmed()] = rl[i + 2].mid(3).trimmed();
            }
            for (int i = 0; i + 1 < lines.size(); ++i) {
                const QString& u = lines[i];
                const QString& m = lines[i + 1];
                if (u.startsWith(QLatin1String("u ")) && u.endsWith(QLatin1String(" u")) && m.startsWith(QLatin1String("m ")) && m.endsWith(QLatin1String(" m"))) {
                    // known upstream limitations: compare with what the original actually detects
                    const QString expected = upstreamActual.value(u.trimmed(), m);
                    QTest::newRow(qPrintable(QStringLiteral("%1:%2 %3").arg(QLatin1String(name)).arg(i + 1).arg(u.mid(2, 40)))) << u << expected;
                    ++n;
                }
            }
        }
        QVERIFY(n > 50);
    }

    // Notepad_plus::addHotSpot: scan the text; positions of detected URLs are indicated
    void upstreamCases()
    {
        QFETCH(QString, line);
        QFETCH(QString, mask);
        const std::u16string text = line.toStdU16String();
        QString got(line.size(), QLatin1Char('0'));
        int start = 0;
        const int len = static_cast<int>(text.size());
        while (start < len) {
            int seg = 0;
            if (isUrl(text.data(), len, start, &seg))
                for (int k = start; k < start + seg; ++k)
                    got[k] = QLatin1Char('1');
            start += std::max(1, seg);
        }
        QCOMPARE(got.mid(2, mask.size() - 4), mask.mid(2, mask.size() - 4));
    }
};

QTEST_MAIN(TestUrlDetect)
#include "test_urldetect.moc"
