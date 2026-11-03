// Phase 3 UI tests: Find / Replace dialog actions, search results panel, Find in Files
// (incl. 10k files: cancellable, event loop keeps running), Edit / Search menu commands.
#include "MainWindow.h"
#include "editor/EditorPane.h"
#include "menu/ActionRegistry.h"
#include "menu/CommandIds.h"
#include "search/FindReplaceDialog.h"
#include "search/SearchResultsPanel.h"

#include "core/search/Search.h"
#include "core/shortcut/Shortcuts.h"

#include <SciLexer.h>
#include <Scintilla.h>
#include <ScintillaEditBase.h>

#include <QApplication>
#include <QClipboard>
#include <QDateTime>
#include <QDir>
#include <QFontDatabase>
#include <QFontMetrics>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QPushButton>
#include <QProgressDialog>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>

using namespace npp::core;
using namespace npp::ui;
using namespace npp::ui::cmd;
using Action = FindReplaceDialog::Action;

namespace {

void writeAll(const QString& path, const QByteArray& bytes)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    f.open(QIODevice::WriteOnly);
    f.write(bytes);
}

QByteArray readAll(const QString& path)
{
    QFile f(path);
    f.open(QIODevice::ReadOnly);
    return f.readAll();
}

std::string text(ScintillaEditBase* e)
{
    const auto len = e->send(SCI_GETLENGTH);
    std::string s(static_cast<size_t>(len), '\0');
    e->send(SCI_GETTEXT, static_cast<uptr_t>(len + 1), reinterpret_cast<sptr_t>(s.data()));
    return s;
}

} // namespace

class TestFind : public QObject {
    Q_OBJECT
    QTemporaryDir m_user, m_work;
    std::unique_ptr<MainWindow> m_w;

    ScintillaEditBase* ed() { return m_w->pane(0)->editor(); }
    FindReplaceDialog* dlg() { return m_w->findDialog(); }
    void setText(const char* t)
    {
        ed()->send(SCI_SETTEXT, 0, reinterpret_cast<sptr_t>(t));
        ed()->send(SCI_SETSEL, 0, 0);
    }
    void run(Action a) { emit dlg()->actionRequested(a); }
    SearchOptions opts(const char* find, const char* repl = "", SearchType t = SearchType::Normal)
    {
        SearchOptions o;
        o.findWhat = find;
        o.replaceWith = repl;
        o.type = t;
        return o;
    }

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
    }
    void cleanup() { m_w.reset(); }

    void findNextWrapsAndReportsStatus()
    {
        setText("abc foo def foo");
        dlg()->setOptions(opts("foo"));
        run(Action::FindNext);
        QCOMPARE(ed()->send(SCI_GETSELECTIONSTART), sptr_t(4));
        run(Action::FindNext);
        QCOMPARE(ed()->send(SCI_GETSELECTIONSTART), sptr_t(12));
        run(Action::FindNext); // wraps
        QCOMPARE(ed()->send(SCI_GETSELECTIONSTART), sptr_t(4));
        QVERIFY(dlg()->status().contains(QStringLiteral("1st occurrence from the top")));
        dlg()->setOptions(opts("zzz"));
        run(Action::FindNext);
        QVERIFY(dlg()->status().contains(QStringLiteral("Can't find")));
        // menu Find Previous uses the dialog options
        dlg()->setOptions(opts("foo"));
        ed()->send(SCI_SETSEL, 15, 15);
        QVERIFY(m_w->actions()->trigger(IDM_SEARCH_FINDPREV));
        QCOMPARE(ed()->send(SCI_GETSELECTIONSTART), sptr_t(12));
    }

    void countAndRegex()
    {
        setText("a1 b22 c333");
        dlg()->setOptions(opts("\\d+", "", SearchType::Regex));
        run(Action::Count);
        QCOMPARE(dlg()->status(), QStringLiteral("Count: 3 matches."));
    }

    void replaceAndReplaceAll()
    {
        setText("cat cat cat");
        dlg()->showTab(FindReplaceDialog::ReplaceTab);
        dlg()->setOptions(opts("cat", "dog"));
        run(Action::Replace); // selects first
        run(Action::Replace); // replaces it, selects next
        QCOMPARE(text(ed()), std::string("dog cat cat"));
        run(Action::ReplaceAll);
        QCOMPARE(text(ed()), std::string("dog dog dog"));
        QVERIFY(dlg()->status().startsWith(QStringLiteral("Replace All: 2 occurrences were replaced")));
        ed()->send(SCI_UNDO);
        QCOMPARE(text(ed()), std::string("dog cat cat"));
        // regex with back references and case conversion
        setText("john smith");
        dlg()->setOptions(opts("(\\w+) (\\w+)", "\\U$2\\E, $1", SearchType::Regex));
        run(Action::ReplaceAll);
        QCOMPARE(text(ed()), std::string("SMITH, john"));
    }

    void replaceAllInSelection()
    {
        setText("x x x x");
        ed()->send(SCI_SETSEL, 2, 5);
        dlg()->setOptions(opts("x", "y"));
        dlg()->setInSelection(true);
        run(Action::ReplaceAll);
        dlg()->setInSelection(false);
        QCOMPARE(text(ed()), std::string("x y y x"));
    }

    void findAllInDocumentsToPanel()
    {
        setText("one\ntwo foo\nthree foo foo");
        Buffer* first = m_w->currentBuffer();
        m_w->actions()->trigger(IDM_FILE_NEW);
        setText("foo here");
        dlg()->setOptions(opts("foo"));
        run(Action::FindAllOpen);
        SearchResultsPanel* p = m_w->searchResults(false);
        const std::string panel = text(p->view());
        QVERIFY2(panel.find("Search \"foo\" (4 hits in 2 files of 2 searched)") == 0, panel.c_str());
        QVERIFY(panel.find("(3 hits)") != std::string::npos);
        QVERIFY(panel.find("\tLine 2: two foo") != std::string::npos);
        // F4 navigates to the first hit (in the first document)
        p->view()->send(SCI_GOTOPOS, 0);
        QVERIFY(m_w->actions()->trigger(IDM_SEARCH_GOTONEXTFOUND));
        QCOMPARE(m_w->currentBuffer(), first);
        QCOMPARE(ed()->send(SCI_GETSELECTIONSTART), sptr_t(8));
        QCOMPARE(ed()->send(SCI_GETSELECTIONEND), sptr_t(11));
        // the hits are coloured by the searchResult lexer
        p->view()->send(SCI_COLOURISE, 0, -1);
        const auto lineStart = p->view()->send(SCI_POSITIONFROMLINE, 2);
        QCOMPARE(p->view()->send(SCI_GETSTYLEAT, static_cast<uptr_t>(lineStart + 13)), sptr_t(SCE_SEARCHRESULT_WORD2SEARCH));
        // a second search goes on top
        dlg()->setOptions(opts("two"));
        run(Action::FindAllCurrent);
        QCOMPARE(p->searchCount(), 2);
        QVERIFY(text(p->view()).find("Search \"two\"") == 0);
    }

    void markWithBookmarks()
    {
        setText("a\nfoo\nb\nfoo");
        dlg()->showTab(FindReplaceDialog::MarkTab);
        dlg()->setOptions(opts("foo"));
        dlg()->setMarkOptions(true, true);
        run(Action::MarkAll);
        QCOMPARE(dlg()->status(), QStringLiteral("Mark: 2 matches."));
        QCOMPARE(ed()->send(SCI_INDICATORVALUEAT, 31, 2), sptr_t(1));
        QVERIFY(ed()->send(SCI_MARKERGET, 1) & (1 << 20));
        QVERIFY(ed()->send(SCI_MARKERGET, 3) & (1 << 20));
        run(Action::CopyMarked);
        QCOMPARE(QApplication::clipboard()->text(), QStringLiteral("foo\r\nfoo\r\n"));
        run(Action::ClearMarks);
        QCOMPARE(ed()->send(SCI_INDICATORVALUEAT, 31, 2), sptr_t(0));
    }

    void findInFilesAndReplaceInFiles()
    {
        const QString root = m_work.filePath(QStringLiteral("fif"));
        writeAll(root + QStringLiteral("/a.txt"), "needle 1\nnothing\n");
        writeAll(root + QStringLiteral("/sub/b.cpp"), "x needle y needle\n");
        writeAll(root + QStringLiteral("/sub/c.log"), "needle\n");
        writeAll(root + QStringLiteral("/utf16.txt"), QByteArray("\xFF\xFE", 2) + QByteArray("n\0e\0e\0d\0l\0e\0", 12));
        dlg()->showTab(FindReplaceDialog::FindInFilesTab);
        dlg()->setDirectory(root);
        dlg()->setFilters(QStringLiteral("*.txt *.cpp"));
        dlg()->setOptions(opts("needle"));
        run(Action::FindInFiles);
        QVERIFY2(dlg()->status() == QStringLiteral("Find in Files: 4 hits"), qPrintable(dlg()->status()));
        const std::string panel = text(m_w->searchResults(false)->view());
        QVERIFY(panel.find("(4 hits in 3 files of 3 searched)") != std::string::npos);
        // opening a result jumps into the file
        m_w->searchResults(false)->view()->send(SCI_GOTOPOS, 0);
        m_w->actions()->trigger(IDM_SEARCH_GOTONEXTFOUND);
        QVERIFY(m_w->currentBuffer() && !m_w->currentBuffer()->isUntitled());
        QCOMPARE(QString::fromStdString(m_w->pane(0)->adapter().textRange(ed()->send(SCI_GETSELECTIONSTART), ed()->send(SCI_GETSELECTIONEND))),
                 QStringLiteral("needle"));

        // Replace in Files: confirmation box answered automatically
        QTimer::singleShot(200, [] {
            if (auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget()))
                box->button(QMessageBox::Yes)->click();
        });
        dlg()->setOptions(opts("needle", "pin"));
        run(Action::ReplaceInFiles);
        QCOMPARE(readAll(root + QStringLiteral("/sub/b.cpp")), QByteArray("x pin y pin\n"));
        QCOMPARE(readAll(root + QStringLiteral("/sub/c.log")), QByteArray("needle\n")); // filtered out
        QCOMPARE(readAll(root + QStringLiteral("/utf16.txt")), QByteArray("\xFF\xFE", 2) + QByteArray("p\0i\0n\0", 6)); // own encoding kept
    }

    // 10 000 files: the event loop keeps running (timer ticks) and the search can be cancelled
    void findInFilesLargeFolderIsResponsiveAndCancellable()
    {
        const QString root = m_work.filePath(QStringLiteral("big"));
        QDir().mkpath(root);
        for (int i = 0; i < 10000; ++i)
            writeAll(QStringLiteral("%1/d%2/f%3.txt").arg(root).arg(i / 500).arg(i), i % 1000 == 0 ? "hit me\n" : "nothing here\n");
        dlg()->showTab(FindReplaceDialog::FindInFilesTab);
        dlg()->setDirectory(root);
        dlg()->setFilters(QStringLiteral("*.*"));
        dlg()->setOptions(opts("hit"));

        // heartbeat through posted events (the queue that also carries paint / input processing):
        // measures the longest time the event loop did not run
        int ticks = 0;
        qint64 maxGap = 0;
        bool beating = true;
        QElapsedTimer sinceTick, t;
        sinceTick.start();
        t.start();
        std::function<void()> beat = [&] {
            ++ticks;
            const qint64 g = sinceTick.restart();
            if (g > maxGap) { maxGap = g; qInfo("gap %lld ending at %lld", g, QDateTime::currentMSecsSinceEpoch() % 100000); }
            if (beating)
                QMetaObject::invokeMethod(this, beat, Qt::QueuedConnection);
        };
        QMetaObject::invokeMethod(this, beat, Qt::QueuedConnection);
        t.restart();
        run(Action::FindInFiles);
        const qint64 ms = t.elapsed();
        beating = false;
        QCoreApplication::processEvents();
        QVERIFY2(dlg()->status() == QStringLiteral("Find in Files: 10 hits"), qPrintable(dlg()->status()));
        maxGap = std::max(maxGap, sinceTick.elapsed());
        qInfo("10k files searched in %lld ms, %d event loop runs, longest pause %lld ms", ms, ticks, maxGap);
        QVERIFY2(maxGap < 500, "the UI did not process events for more than 0.5 s during Find in Files");

        // cancel: press Cancel on the progress window shortly after start
        QTimer canceller; // cancels as soon as the progress window exists (it may stay hidden on fast machines)
        connect(&canceller, &QTimer::timeout, [&canceller] {
            for (QWidget* w : QApplication::topLevelWidgets())
                if (auto* p = qobject_cast<QProgressDialog*>(w)) {
                    p->cancel();
                    canceller.stop();
                }
        });
        canceller.start(5);
        dlg()->setOptions(opts("nothing")); // 9990 hits: long enough to be cancelled
        run(Action::FindInFiles);
        QVERIFY2(dlg()->status() == QStringLiteral("The search was cancelled."), qPrintable(dlg()->status()));
    }

    // A macro recorded in the original Notepad++ (tools/macro-compat/record-in-npp.ps1) replays
    // with the same result; recording the same actions here gives the same steps.
    void macroRecordedInOriginalReplays()
    {
        QFile f(QStringLiteral(NPP_TEST_DATA_DIR "/macros/recorded-in-npp.json"));
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QJsonObject j = QJsonDocument::fromJson(f.readAll()).object();
        qInfo("%s", qPrintable(j.value(QStringLiteral("source")).toString()));
        ShortcutsFile sc;
        QVERIFY(sc.loadFromString(j.value(QStringLiteral("shortcutsXml")).toString().toStdString()));
        QCOMPARE(sc.macros.size(), size_t(1));
        const std::string input = j.value(QStringLiteral("input")).toString().toStdString();

        ed()->send(SCI_SETEOLMODE, SC_EOL_CRLF); // the original's new document
        setText(input.c_str());
        m_w->playMacro(sc.macros[0].steps);
        QCOMPARE(QString::fromStdString(text(ed())), j.value(QStringLiteral("expected")).toString());

        // record the same actions in the port
        setText(input.c_str());
        auto* a = m_w->actions();
        a->trigger(IDM_MACRO_STARTRECORDINGMACRO);
        a->trigger(IDM_EDIT_TRIMTRAILING);
        a->trigger(IDM_EDIT_SORTLINES_LEXICOGRAPHIC_ASCENDING);
        ed()->send(SCI_DOCUMENTEND);
        ed()->send(SCI_REPLACESEL, 0, reinterpret_cast<sptr_t>("zucchini"));
        ed()->send(SCI_DOCUMENTSTART);
        ed()->send(SCI_LINEEND);
        ed()->send(SCI_REPLACESEL, 0, reinterpret_cast<sptr_t>(" (first)"));
        dlg()->showTab(FindReplaceDialog::ReplaceTab);
        SearchOptions o = opts("^(\\w)(\\w*)", "\\U$1\\E$2", SearchType::Regex);
        o.wrap = true;
        dlg()->setOptions(o);
        dlg()->setInSelection(false);
        run(Action::ReplaceAll);
        a->trigger(IDM_EDIT_UPPERCASE);
        a->trigger(IDM_MACRO_STOPRECORDINGMACRO);
        QCOMPARE(QString::fromStdString(text(ed())), j.value(QStringLiteral("expected")).toString());
        const auto& mine = m_w->recordedMacro();
        // the last original step is UPPERCASE sent without a selection: that menu item is disabled
        // here (no selection), so it is not part of the port's recording
        auto orig = sc.macros[0].steps;
        QCOMPARE(orig.back().wParam, (long long)IDM_EDIT_UPPERCASE);
        orig.pop_back();
        QCOMPARE(mine.size(), orig.size());
        for (size_t i = 0; i < orig.size(); ++i) {
            QCOMPARE(mine[i].type, orig[i].type);
            QCOMPARE(mine[i].message, orig[i].message);
            QCOMPARE(mine[i].wParam, orig[i].wParam);
            QCOMPARE(mine[i].lParam, orig[i].lParam);
            QCOMPARE(mine[i].sParam, orig[i].sParam);
        }
    }

    void highlighters()
    {
        m_w->show(); // visible range is needed for the smart / URL highlighting
        setText("foo bar foo\nvisit https://example.org/x?a=1 now\nfoobar");
        ed()->send(SCI_SETSEL, 0, 3); // selects the word "foo"
        QCoreApplication::processEvents();
        QCOMPARE(ed()->send(SCI_INDICATORVALUEAT, 29, 9), sptr_t(1));  // smart highlighting: 2nd "foo"
        QCOMPARE(ed()->send(SCI_INDICATORVALUEAT, 29, 46), sptr_t(0)); // not inside "foobar" (whole word)
        const auto urlStart = ed()->send(SCI_POSITIONFROMLINE, 1) + 6;
        QCOMPARE(ed()->send(SCI_INDICATORVALUEAT, 8, static_cast<uptr_t>(urlStart + 3)) != 0, true); // URL indicator
        QCOMPARE(ed()->send(SCI_INDICATORVALUEAT, 8, static_cast<uptr_t>(urlStart - 2)), sptr_t(0));

        m_w->actions()->trigger(IDM_LANG_XML);
        setText("<root><item a=\"1\">x</item></root>");
        ed()->send(SCI_COLOURISE, 0, -1);
        ed()->send(SCI_GOTOPOS, 8); // inside <item ...>
        QCoreApplication::processEvents();
        QCOMPARE(ed()->send(SCI_INDICATORVALUEAT, 27, 7), sptr_t(1));  // open tag highlighted
        QCOMPARE(ed()->send(SCI_INDICATORVALUEAT, 27, 21), sptr_t(1)); // close tag </item>
        QCOMPARE(ed()->send(SCI_INDICATORVALUEAT, 26, 13), sptr_t(1)); // attribute a="1"
    }

    void autoCompletionWithShippedApis()
    {
        m_w->actions()->trigger(IDM_LANG_CPP);
        setText("int x = prin");
        ed()->send(SCI_DOCUMENTEND);
        QVERIFY(m_w->actions()->trigger(IDM_EDIT_AUTOCOMPLETE));
        QVERIFY(ed()->send(SCI_AUTOCACTIVE)); // cpp.xml of installer/APIs
        ed()->send(SCI_AUTOCCANCEL);
        setText("printf(");
        ed()->send(SCI_DOCUMENTEND);
        QVERIFY(m_w->actions()->trigger(IDM_EDIT_FUNCCALLTIP));
        QVERIFY(ed()->send(SCI_CALLTIPACTIVE));
    }

    void historyIsSaved()
    {
        dlg()->setOptions(opts("remember me"));
        run(Action::Count);
        m_w->shutdownForTest();
        FindHistory h;
        dlg()->saveHistory(h);
        QCOMPARE(h.finds.front(), std::string("remember me"));
    }

    void editMenuCommands()
    {
        ed()->send(SCI_SETEOLMODE, SC_EOL_CRLF); // lines are split on the document EOL
        setText("b\r\na\r\nc");
        QVERIFY(m_w->actions()->trigger(IDM_EDIT_SORTLINES_LEXICOGRAPHIC_DESCENDING));
        QCOMPARE(text(ed()), std::string("c\r\nb\r\na"));
        ed()->send(SCI_SETSEL, 0, 1);
        QVERIFY(m_w->actions()->trigger(IDM_EDIT_UPPERCASE));
        QCOMPARE(text(ed()), std::string("C\r\nb\r\na"));
        // C++ comment toggle
        m_w->actions()->trigger(IDM_LANG_CPP);
        setText("int a;");
        QVERIFY(m_w->actions()->trigger(IDM_EDIT_BLOCK_COMMENT));
        QCOMPARE(text(ed()), std::string("// int a;"));
    }

    void markStylesAndBookmarks()
    {
        setText("foo bar foo\nline2\nfoo");
        ed()->send(SCI_SETSEL, 0, 3);
        QVERIFY(m_w->actions()->trigger(IDM_SEARCH_MARKALLEXT1));
        QCOMPARE(ed()->send(SCI_INDICATORVALUEAT, 25, 8), sptr_t(1));
        QVERIFY(m_w->actions()->trigger(IDM_SEARCH_STYLE1TOCLIP));
        QCOMPARE(QApplication::clipboard()->text(), QStringLiteral("foo\r\nfoo\r\nfoo\r\n"));
        QVERIFY(m_w->actions()->trigger(IDM_SEARCH_UNMARKALLEXT1));
        QCOMPARE(ed()->send(SCI_INDICATORVALUEAT, 25, 8), sptr_t(0));

        ed()->send(SCI_MARKERADD, 1, 20);
        QVERIFY(m_w->actions()->trigger(IDM_SEARCH_INVERSEMARKS));
        QVERIFY(m_w->actions()->trigger(IDM_SEARCH_DELETEMARKEDLINES));
        QCOMPARE(text(ed()), std::string("line2\n"));

        setText("f(a[1])");
        ed()->send(SCI_GOTOPOS, 1);
        QVERIFY(m_w->actions()->trigger(IDM_SEARCH_GOTOMATCHINGBRACE));
        QCOMPARE(ed()->send(SCI_GETCURRENTPOS), sptr_t(6));
    }
};

QTEST_MAIN(TestFind)
#include "test_find.moc"
