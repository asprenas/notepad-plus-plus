// Phase 6 performance measurements (docs/reference/performance.md), run by tools/perf (not part of the
// regular CTest run: the 500 MB case needs time and memory).
//   test_ui_perf manyTabs        200 documents: open time, tab switch time (avg / max)
//   test_ui_perf largeFile       NPP_PERF_LARGE_FILE=<file>: open, scroll to end, search, go to top
// Results are printed as "PERF <name> <value> <unit>" lines.
#include "MainWindow.h"
#include "editor/EditorPane.h"
#include "menu/ActionRegistry.h"
#include "menu/CommandIds.h"
#include "search/FindReplaceDialog.h"

#include "core/search/Search.h"

#include <Scintilla.h>
#include <ScintillaEditBase.h>

#include <QApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

#include <algorithm>
#include <cstring>
#include <cstdio>

using namespace npp::core;
using namespace npp::ui;

namespace {
void perf(const char* name, double value, const char* unit)
{
    std::printf("PERF %-32s %10.1f %s\n", name, value, unit);
    std::fflush(stdout);
}
} // namespace

class TestPerf : public QObject {
    Q_OBJECT
    QTemporaryDir m_user, m_work;
    std::unique_ptr<MainWindow> m_w;

private slots:
    void initTestCase()
    {
        qputenv("NPP_QT_USER_DIR", m_user.path().toUtf8());
        qputenv("NPP_DATA_DIR", (QCoreApplication::applicationDirPath() + QStringLiteral("/data")).toUtf8());
        initRegexLocale();
    }
    void init()
    {
        CommandLine c;
        c.noSession = true;
        m_w = std::make_unique<MainWindow>(c);
        m_w->resize(1200, 800);
        m_w->show();
        QVERIFY(QTest::qWaitForWindowExposed(m_w.get()));
    }
    void cleanup()
    {
        m_w->shutdownForTest();
        m_w.reset();
    }

    void manyTabs()
    {
        // 200 source-like files of ~20 KB
        QByteArray body;
        for (int l = 0; l < 500; ++l)
            body += "int function_" + QByteArray::number(l) + "(int a, int b) { return a + b; } // line\n";
        QStringList files;
        for (int i = 0; i < 200; ++i) {
            const QString f = m_work.filePath(QStringLiteral("file%1.cpp").arg(i));
            QFile out(f);
            QVERIFY(out.open(QIODevice::WriteOnly));
            out.write(body);
            files << f;
        }
        QElapsedTimer t;
        t.start();
        for (const QString& f : files)
            QVERIFY(m_w->openFile(f, {}, true));
        QCoreApplication::processEvents();
        perf("tabs200.open_all", t.elapsed(), "ms");
        QVERIFY(m_w->pane(0)->count() >= 200);

        // switch through every tab (Ctrl+Tab), painting each one
        qint64 worst = 0, total = 0;
        const int switches = 400;
        for (int i = 0; i < switches; ++i) {
            QElapsedTimer s;
            s.start();
            m_w->actions()->trigger(npp::ui::cmd::IDM_VIEW_TAB_NEXT);
            m_w->pane(0)->editor()->repaint();
            QCoreApplication::processEvents();
            const qint64 ns = s.nsecsElapsed();
            worst = std::max(worst, ns);
            total += ns;
        }
        perf("tabs200.switch_avg", total / 1e6 / switches, "ms");
        perf("tabs200.switch_max", worst / 1e6, "ms");
        QVERIFY2(worst / 1e6 < 100, "a tab switch took more than 100 ms");
    }

    void largeFile()
    {
        const QString file = qEnvironmentVariable("NPP_PERF_LARGE_FILE");
        if (file.isEmpty())
            QSKIP("set NPP_PERF_LARGE_FILE");
        QElapsedTimer t;
        t.start();
        Buffer* b = m_w->openFile(file, {}, true);
        QVERIFY(b);
        QCoreApplication::processEvents();
        perf("large.open", t.elapsed(), "ms");
        QVERIFY(b->largeFile);
        ScintillaEditBase* e = m_w->pane(0)->editor();

        t.restart();
        e->send(SCI_DOCUMENTEND);
        e->repaint();
        QCoreApplication::processEvents();
        perf("large.scroll_to_end", t.elapsed(), "ms");

        // a word present only at the end of the file: search from the top through 500 MB
        const char* marker = "\r\nUNIQUE_MARKER_AT_END\r\n";
        e->send(SCI_APPENDTEXT, std::strlen(marker), reinterpret_cast<sptr_t>(marker));
        e->send(SCI_DOCUMENTSTART);
        SearchOptions o;
        o.findWhat = "UNIQUE_MARKER_AT_END";
        o.wrap = false;
        t.restart();
        QVERIFY(m_w->findNextInCurrentForTest(o));
        e->repaint();
        QCoreApplication::processEvents();
        perf("large.find_at_end", t.elapsed(), "ms");
        QVERIFY(e->send(SCI_GETSELECTIONSTART) > 400 * 1024 * 1024);

        t.restart();
        e->send(SCI_DOCUMENTSTART);
        e->repaint();
        QCoreApplication::processEvents();
        perf("large.scroll_to_start", t.elapsed(), "ms");
        e->send(SCI_SETSAVEPOINT); // no "save changes?" on cleanup
    }
};

QTEST_MAIN(TestPerf)
#include "test_perf.moc"
