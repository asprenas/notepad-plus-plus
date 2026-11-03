// Command handlers of the main menu (phase 2 MVP subset). Commands without a handler stay
// visible but disabled (see ActionRegistry::unimplementedMenuCommands).
#include "MainWindow.h"

#include "docking/DockManager.h"
#include "editor/EditorPane.h"
#include "menu/ActionRegistry.h"
#include "menu/CommandIds.h"

#include "core/encoding/EncodingMapper.h"
#include "core/util/PathUtil.h"

#include <Scintilla.h>
#include <ScintillaEditBase.h>

#include <QApplication>
#include <QClipboard>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QInputDialog>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QProcess>
#include <QSplitter>
#include <QStatusBar>
#include <QStyle>
#include <QToolBar>
#include <QUrl>

#include <algorithm>

namespace npp::ui {

using namespace npp::core;
using namespace cmd;

namespace {

QString q(const std::string& s) { return QString::fromStdString(s); }
const std::string kPanelDocList = "npp.documentList", kPanelFolder = "npp.folderAsWorkspace";

struct LangCmd {
    int id;
    LangType lang;
};
constexpr LangCmd kLangCommands[] = {
#define NPP_LANGCMD(id, lang) {id, lang},
#include "menu/LanguageCommands.inc"
#undef NPP_LANGCMD
};

} // namespace

void MainWindow::registerCommands()
{
    registerFileCommands();
    registerEditCommands();
    registerViewCommands();
    registerFormatCommands();
    registerLanguageCommands();
    registerMiscCommands();

    const auto menus = m_actions->buildMenuBar(menuBar(), &m_nativeLang, m_config.gui.langMenuCompact);
    m_actions->applyShortcuts(&m_shortcuts);
    for (const auto& [id, menu] : menus)
        connect(menu, &QMenu::aboutToShow, m_actions, &ActionRegistry::updateStates);

    if (const auto it = menus.find("file"); it != menus.end()) {
        m_recentMenu = new QMenu(miscString("recent-files", "Recent Files"), this);
        QAction* exitAction = m_actions->action(IDM_FILE_EXIT);
        it->second->insertMenu(exitAction, m_recentMenu);
        it->second->insertSeparator(exitAction);
        updateRecentMenu();
    }

    // Toolbar (upstream standard icons subset)
    QToolBar* tb = addToolBar(tr("Toolbar"));
    tb->setObjectName(QStringLiteral("mainToolbar"));
    const std::pair<int, QStyle::StandardPixmap> tools[] = {
        {IDM_FILE_NEW, QStyle::SP_FileIcon},          {IDM_FILE_OPEN, QStyle::SP_DialogOpenButton},
        {IDM_FILE_SAVE, QStyle::SP_DialogSaveButton}, {IDM_FILE_CLOSE, QStyle::SP_DialogCloseButton},
        {0, {}},
        {IDM_EDIT_UNDO, QStyle::SP_ArrowBack},        {IDM_EDIT_REDO, QStyle::SP_ArrowForward},
        {0, {}},
        {IDM_VIEW_WRAP, QStyle::SP_BrowserReload},
    };
    for (auto [id, icon] : tools) {
        if (!id) {
            tb->addSeparator();
            continue;
        }
        QAction* a = m_actions->action(id);
        if (a->icon().isNull())
            a->setIcon(style()->standardIcon(icon));
        tb->addAction(a);
    }
    tb->setVisible(m_config.gui.toolbarShow);
    m_actions->updateStates();
}

// ------------------------------------------------------------------------------------------------
// File
// ------------------------------------------------------------------------------------------------

void MainWindow::registerFileCommands()
{
    ActionRegistry* r = m_actions;
    auto hasDoc = [this] { return currentBuffer() != nullptr; };
    auto hasFile = [this] { const Buffer* b = currentBuffer(); return b && !b->isUntitled(); };

    r->setHandler(IDM_FILE_NEW, {[this] { showBuffer(m_files->newDocument(), currentPane()); }});
    r->setHandler(IDM_FILE_OPEN, {[this] {
        QString dir = q(m_config.gui.openSaveDir == 2 ? m_config.gui.defaultDir : m_config.gui.lastUsedDir);
        if (const Buffer* b = currentBuffer(); b && !b->isUntitled() && m_config.gui.openSaveDir == 0)
            dir = QFileInfo(q(b->path)).absolutePath();
        const QStringList files = QFileDialog::getOpenFileNames(this, tr("Open"), dir, tr("All types (*.*)"));
        if (!files.isEmpty())
            m_config.gui.lastUsedDir = QFileInfo(files.first()).absolutePath().toStdString();
        for (const QString& f : files)
            openFile(f);
    }});
    r->setHandler(IDM_FILE_RELOAD, {[this] {
        Buffer* b = currentBuffer();
        if (!b || b->isUntitled())
            return;
        if (b->dirty) {
            const auto t = m_nativeLang.messageBox("DoReloadOrNot", "Are you sure you want to reload the current file and lose the changes made in Notepad++?", "Reload");
            if (QMessageBox::question(this, q(t.title), q(t.message)) != QMessageBox::Yes)
                return;
        }
        reloadBuffer(b);
    }, hasFile});
    r->setHandler(IDM_FILE_SAVE, {[this] { saveBuffer(currentBuffer(), false); }, hasDoc});
    r->setHandler(IDM_FILE_SAVEAS, {[this] { saveBuffer(currentBuffer(), true); }, hasDoc});
    r->setHandler(IDM_FILE_SAVECOPYAS, {[this] { saveBuffer(currentBuffer(), false, true); }, hasDoc});
    r->setHandler(IDM_FILE_SAVEALL, {[this] {
        for (const auto& [id, p] : allTabs())
            if (Buffer* b = buffer(id); b && b->dirty)
                if (!saveBuffer(b, false))
                    return;
    }});
    r->setHandler(IDM_FILE_RENAME, {[this] {
        Buffer* b = currentBuffer();
        if (!b)
            return;
        if (b->isUntitled()) {
            bool ok = false;
            const QString name = QInputDialog::getText(this, tr("Rename"), tr("New name"), QLineEdit::Normal, q(b->untitledName), &ok);
            if (ok && !name.trimmed().isEmpty()) {
                b->untitledName = name.trimmed().toStdString();
                refreshBuffer(b);
            }
            return;
        }
        const QString to = QFileDialog::getSaveFileName(this, tr("Rename"), q(b->path));
        if (to.isEmpty() || to == q(b->path))
            return;
        watchFile(b->path, false);
        if (!QFile::rename(q(b->path), to)) {
            watchFile(b->path, true);
            QMessageBox::warning(this, QStringLiteral("Notepad++"), tr("The file cannot be renamed."));
            return;
        }
        b->path = absolutePath(to.toStdString());
        watchFile(b->path, true);
        refreshBuffer(b);
    }, hasDoc});
    r->setHandler(IDM_FILE_DELETE, {[this] {
        Buffer* b = currentBuffer();
        if (!b || b->isUntitled())
            return;
        const auto t = m_nativeLang.messageBox("DeleteFileTitle", "The file \"$STR_REPLACE$\" will be moved to your Recycle Bin and this document will be closed.\nContinue?", "Delete file", 0, b->path);
        if (QMessageBox::question(this, q(t.title), q(t.message)) != QMessageBox::Yes)
            return;
        const std::string path = b->path;
        watchFile(path, false);
        if (!QFile::moveToTrash(q(path))) {
            watchFile(path, true);
            QMessageBox::warning(this, QStringLiteral("Notepad++"), tr("The file cannot be moved to the Recycle Bin."));
            return;
        }
        for (EditorPane* p : m_panes)
            if (p->indexOf(b->id) >= 0)
                closeBuffer(b->id, p, false);
    }, hasFile});

    r->setHandler(IDM_FILE_CLOSE, {[this] { closeBuffer(currentPane()->current(), currentPane()); }, hasDoc});
    r->setHandler(IDM_FILE_CLOSEALL, {[this] {
        auto tabs = allTabs();
        std::reverse(tabs.begin(), tabs.end());
        closeBuffers(tabs);
    }});
    auto closeWhere = [this](auto pred) {
        EditorPane* p = currentPane();
        const BufferId cur = p->current();
        const int curIdx = p->currentIndex();
        std::vector<std::pair<BufferId, EditorPane*>> list;
        const auto ids = p->buffers();
        for (int i = 0; i < static_cast<int>(ids.size()); ++i)
            if (ids[static_cast<size_t>(i)] != cur && pred(i, curIdx, buffer(ids[static_cast<size_t>(i)])))
                list.emplace_back(ids[static_cast<size_t>(i)], p);
        closeBuffers(list);
    };
    r->setHandler(IDM_FILE_CLOSEALL_BUT_CURRENT, {[closeWhere] { closeWhere([](int, int, Buffer*) { return true; }); }});
    r->setHandler(IDM_FILE_CLOSEALL_TOLEFT, {[closeWhere] { closeWhere([](int i, int c, Buffer*) { return i < c; }); }});
    r->setHandler(IDM_FILE_CLOSEALL_TORIGHT, {[closeWhere] { closeWhere([](int i, int c, Buffer*) { return i > c; }); }});
    r->setHandler(IDM_FILE_CLOSEALL_UNCHANGED, {[closeWhere, this] {
        if (Buffer* b = currentBuffer(); b && !b->dirty) {
            closeWhere([](int, int, Buffer* x) { return x && !x->dirty; });
            closeBuffer(b->id, currentPane());
        } else {
            closeWhere([](int, int, Buffer* x) { return x && !x->dirty; });
        }
    }});
    r->setHandler(IDM_FILE_RESTORELASTCLOSEDFILE, {[this] {
        while (!m_lastClosed.empty()) {
            const std::string p = m_lastClosed.front();
            m_lastClosed.pop_front();
            if (pathExists(p, PathKind::File)) {
                openFile(q(p));
                return;
            }
        }
    }, [this] { return !m_lastClosed.empty(); }});
    r->setHandler(IDM_FILE_OPEN_FOLDER, {[this] {
        if (const Buffer* b = currentBuffer(); b && !b->isUntitled()) {
#if defined(Q_OS_WIN)
            QProcess::startDetached(QStringLiteral("explorer.exe"), {QStringLiteral("/select,") + QDir::toNativeSeparators(q(b->path))});
#else
            QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(q(b->path)).absolutePath()));
#endif
        }
    }, hasFile});
    r->setHandler(IDM_FILE_OPEN_CMD, {[this] {
        const Buffer* b = currentBuffer();
        const QString dir = b && !b->isUntitled() ? QFileInfo(q(b->path)).absolutePath() : QDir::homePath();
#if defined(Q_OS_WIN)
        QProcess::startDetached(QStringLiteral("cmd.exe"), {QStringLiteral("/K"), QStringLiteral("cd /d \"%1\"").arg(QDir::toNativeSeparators(dir))});
#else
        for (const char* term : {"x-terminal-emulator", "gnome-terminal", "konsole", "xterm"})
            if (QProcess::startDetached(QString::fromLatin1(term), {}, dir))
                break;
#endif
    }});
    r->setHandler(IDM_FILE_OPEN_POWERSHELL, {[this] {
        const Buffer* b = currentBuffer();
        const QString dir = b && !b->isUntitled() ? QFileInfo(q(b->path)).absolutePath() : QDir::homePath();
#if defined(Q_OS_WIN)
        QProcess::startDetached(QStringLiteral("powershell.exe"), {QStringLiteral("-NoExit")}, dir);
#else
        QProcess::startDetached(QStringLiteral("pwsh"), {}, dir);
#endif
    }});
    r->setHandler(IDM_FILE_OPEN_DEFAULT_VIEWER, {[this] {
        if (const Buffer* b = currentBuffer(); b && !b->isUntitled())
            QDesktopServices::openUrl(QUrl::fromLocalFile(q(b->path)));
    }, hasFile});
    r->setHandler(IDM_FILE_CONTAININGFOLDERASWORKSPACE, {[this] {
        if (const Buffer* b = currentBuffer(); b && !b->isUntitled() && m_setWorkspaceRoot)
            m_setWorkspaceRoot(QFileInfo(q(b->path)).absolutePath());
    }, hasFile});
    r->setHandler(IDM_FILE_OPENFOLDERASWORKSPACE, {[this] {
        const QString dir = QFileDialog::getExistingDirectory(this, tr("Open Folder as Workspace"));
        if (!dir.isEmpty() && m_setWorkspaceRoot)
            m_setWorkspaceRoot(dir);
    }});
    r->setHandler(IDM_FILE_LOADSESSION, {[this] {
        const QString f = QFileDialog::getOpenFileName(this, tr("Load Session"), {}, tr("All types (*.*)"));
        if (!f.isEmpty())
            loadSessionFrom(f);
    }});
    r->setHandler(IDM_FILE_SAVESESSION, {[this] {
        const QString f = QFileDialog::getSaveFileName(this, tr("Save Session"), {}, tr("All types (*.*)"));
        if (!f.isEmpty())
            saveSessionTo(f);
    }});
    r->setHandler(IDM_OPEN_ALL_RECENT_FILE, {[this] {
        const auto files = m_config.history.files;
        for (const auto& f : files)
            if (!m_files->findByPath(f) && pathExists(f, PathKind::File))
                openFile(q(f));
    }, [this] { return !m_config.history.files.empty(); }});
    r->setHandler(IDM_CLEAN_RECENT_FILE_LIST, {[this] {
        m_config.history.files.clear();
        updateRecentMenu();
    }, [this] { return !m_config.history.files.empty(); }});
    r->setHandler(IDM_FILE_EXIT, {[this] { close(); }});
}

// ------------------------------------------------------------------------------------------------
// Edit
// ------------------------------------------------------------------------------------------------

void MainWindow::registerEditCommands()
{
    ActionRegistry* r = m_actions;
    auto sci = [this](unsigned msg, uptr_t w = 0, sptr_t l = 0) { return currentPane()->editor()->send(msg, w, l); };
    auto simple = [this, r, sci](int id, unsigned msg, std::function<bool()> enabled = {}) {
        r->setHandler(id, {[sci, msg] { sci(msg); }, std::move(enabled)});
    };
    auto hasSel = [sci] { return sci(SCI_GETSELECTIONEMPTY) == 0; };
    auto writable = [sci] { return sci(SCI_GETREADONLY) == 0; };

    simple(IDM_EDIT_UNDO, SCI_UNDO, [sci] { return sci(SCI_CANUNDO) != 0; });
    simple(IDM_EDIT_REDO, SCI_REDO, [sci] { return sci(SCI_CANREDO) != 0; });
    // Without a selection upstream cuts / copies the current line
    r->setHandler(IDM_EDIT_CUT, {[sci] { sci(SCI_GETSELECTIONEMPTY) ? sci(SCI_LINECUT) : sci(SCI_CUT); }, writable});
    r->setHandler(IDM_EDIT_COPY, {[sci] { sci(SCI_GETSELECTIONEMPTY) ? sci(SCI_LINECOPY) : sci(SCI_COPY); }});
    simple(IDM_EDIT_PASTE, SCI_PASTE, [sci] { return sci(SCI_CANPASTE) != 0; });
    simple(IDM_EDIT_DELETE, SCI_CLEAR, writable);
    simple(IDM_EDIT_SELECTALL, SCI_SELECTALL);
    simple(IDM_EDIT_DUP_LINE, SCI_LINEDUPLICATE, writable);
    simple(IDM_EDIT_LINE_UP, SCI_MOVESELECTEDLINESUP, writable);
    simple(IDM_EDIT_LINE_DOWN, SCI_MOVESELECTEDLINESDOWN, writable);
    simple(IDM_EDIT_JOIN_LINES, SCI_LINESJOIN, writable);
    simple(IDM_EDIT_UPPERCASE, SCI_UPPERCASE, hasSel);
    simple(IDM_EDIT_LOWERCASE, SCI_LOWERCASE, hasSel);
    simple(IDM_EDIT_INS_TAB, SCI_TAB, writable);
    simple(IDM_EDIT_RMV_TAB, SCI_BACKTAB, writable);
    r->setHandler(IDM_EDIT_SPLIT_LINES, {[sci] {
        sci(SCI_TARGETFROMSELECTION);
        sci(SCI_LINESSPLIT, 0);
    }, writable});
    r->setHandler(IDM_EDIT_REMOVEEMPTYLINES, {[this] {
        ScintillaEditBase* e = currentPane()->editor();
        e->send(SCI_BEGINUNDOACTION);
        for (intptr_t line = e->send(SCI_GETLINECOUNT) - 1; line >= 0; --line)
            if (e->send(SCI_GETLINEENDPOSITION, static_cast<uptr_t>(line)) == e->send(SCI_POSITIONFROMLINE, static_cast<uptr_t>(line))
                && e->send(SCI_GETLINECOUNT) > 1)
                e->send(SCI_DELETERANGE, static_cast<uptr_t>(e->send(SCI_POSITIONFROMLINE, static_cast<uptr_t>(line))),
                        e->send(SCI_POSITIONFROMLINE, static_cast<uptr_t>(line + 1)) - e->send(SCI_POSITIONFROMLINE, static_cast<uptr_t>(line)));
        e->send(SCI_ENDUNDOACTION);
    }, writable});
    r->setHandler(IDM_EDIT_TOGGLEREADONLY, {[this] {
        if (Buffer* b = currentBuffer()) {
            b->userReadOnly = !b->userReadOnly;
            for (EditorPane* p : m_panes)
                if (p->current() == b->id)
                    p->editor()->send(SCI_SETREADONLY, b->isReadOnly());
            refreshBuffer(b);
        }
    }, {}, [this] { const Buffer* b = currentBuffer(); return b && b->userReadOnly; }});
    auto copyText = [this](int what) {
        const Buffer* b = currentBuffer();
        if (!b)
            return;
        const QString full = b->isUntitled() ? q(b->untitledName) : QDir::toNativeSeparators(q(b->path));
        const QString text = what == 0 ? full : what == 1 ? QFileInfo(full).fileName() : QDir::toNativeSeparators(QFileInfo(full).absolutePath());
        QApplication::clipboard()->setText(text);
    };
    r->setHandler(IDM_EDIT_RTL, {[sci] { sci(SCI_SETBIDIRECTIONAL, SC_BIDIRECTIONAL_R2L); }, {},
                                 [sci] { return sci(SCI_GETBIDIRECTIONAL) == SC_BIDIRECTIONAL_R2L; }});
    r->setHandler(IDM_EDIT_LTR, {[sci] { sci(SCI_SETBIDIRECTIONAL, SC_BIDIRECTIONAL_DISABLED); }, {},
                                 [sci] { return sci(SCI_GETBIDIRECTIONAL) != SC_BIDIRECTIONAL_R2L; }});
    r->setHandler(IDM_EDIT_FULLPATHTOCLIP, {[copyText] { copyText(0); }});
    r->setHandler(IDM_EDIT_FILENAMETOCLIP, {[copyText] { copyText(1); }});
    r->setHandler(IDM_EDIT_CURRENTDIRTOCLIP, {[copyText] { copyText(2); }});
    r->setHandler(IDM_SEARCH_GOTOLINE, {[this, sci] {
        bool ok = false;
        const int max = static_cast<int>(sci(SCI_GETLINECOUNT));
        const int line = QInputDialog::getInt(this, tr("Go To..."), tr("Line (1 - %1)").arg(max),
                                              static_cast<int>(sci(SCI_LINEFROMPOSITION, static_cast<uptr_t>(sci(SCI_GETCURRENTPOS)))) + 1,
                                              1, max, 1, &ok);
        if (ok) {
            sci(SCI_ENSUREVISIBLE, static_cast<uptr_t>(line - 1));
            sci(SCI_GOTOLINE, static_cast<uptr_t>(line - 1));
        }
    }});
    r->setHandler(IDM_SEARCH_TOGGLE_BOOKMARK, {[sci] {
        const auto line = sci(SCI_LINEFROMPOSITION, static_cast<uptr_t>(sci(SCI_GETCURRENTPOS)));
        if (sci(SCI_MARKERGET, static_cast<uptr_t>(line)) & (1 << 20))
            sci(SCI_MARKERDELETE, static_cast<uptr_t>(line), 20);
        else
            sci(SCI_MARKERADD, static_cast<uptr_t>(line), 20);
    }});
    r->setHandler(IDM_SEARCH_CLEAR_BOOKMARKS, {[sci] { sci(SCI_MARKERDELETEALL, 20); }});
}

// ------------------------------------------------------------------------------------------------
// View
// ------------------------------------------------------------------------------------------------

void MainWindow::registerViewCommands()
{
    ActionRegistry* r = m_actions;
    auto toggleAll = [this](bool& flag, unsigned msg, uptr_t on, uptr_t off) {
        flag = !flag;
        for (EditorPane* p : m_panes)
            p->editor()->send(msg, flag ? on : off);
    };
    GuiConfig& g = m_config.gui;
    r->setHandler(IDM_VIEW_WRAP, {[toggleAll, &g] { toggleAll(g.wrap, SCI_SETWRAPMODE, SC_WRAP_WORD, SC_WRAP_NONE); }, {}, [&g] { return g.wrap; }});
    r->setHandler(IDM_VIEW_ALL_CHARACTERS, {[this, &g] {
        const bool on = !(g.whiteSpaceShow && g.eolShow);
        g.whiteSpaceShow = g.eolShow = on;
        for (EditorPane* p : m_panes) {
            p->editor()->send(SCI_SETVIEWWS, on ? SCWS_VISIBLEALWAYS : SCWS_INVISIBLE);
            p->editor()->send(SCI_SETVIEWEOL, on);
        }
    }, {}, [&g] { return g.whiteSpaceShow && g.eolShow; }});
    r->setHandler(IDM_VIEW_TAB_SPACE, {[toggleAll, &g] { toggleAll(g.whiteSpaceShow, SCI_SETVIEWWS, SCWS_VISIBLEALWAYS, SCWS_INVISIBLE); }, {}, [&g] { return g.whiteSpaceShow; }});
    r->setHandler(IDM_VIEW_EOL, {[toggleAll, &g] { toggleAll(g.eolShow, SCI_SETVIEWEOL, 1, 0); }, {}, [&g] { return g.eolShow; }});
    r->setHandler(IDM_VIEW_INDENT_GUIDE, {[toggleAll, &g] { toggleAll(g.indentGuideLine, SCI_SETINDENTATIONGUIDES, SC_IV_LOOKBOTH, SC_IV_NONE); }, {}, [&g] { return g.indentGuideLine; }});
    r->setHandler(IDM_VIEW_WRAP_SYMBOL, {[toggleAll, &g] { toggleAll(g.wrapSymbolShow, SCI_SETWRAPVISUALFLAGS, SC_WRAPVISUALFLAG_END, SC_WRAPVISUALFLAG_NONE); }, {}, [&g] { return g.wrapSymbolShow; }});
    r->setHandler(IDM_VIEW_ZOOMIN, {[this] { currentPane()->editor()->send(SCI_ZOOMIN); }});
    r->setHandler(IDM_VIEW_ZOOMOUT, {[this] { currentPane()->editor()->send(SCI_ZOOMOUT); }});
    r->setHandler(IDM_VIEW_ZOOMRESTORE, {[this] { currentPane()->editor()->send(SCI_SETZOOM, 0); }});
    r->setHandler(IDM_VIEW_ALWAYSONTOP, {[this] {
        setWindowFlag(Qt::WindowStaysOnTopHint, !(windowFlags() & Qt::WindowStaysOnTopHint));
        show();
    }, {}, [this] { return bool(windowFlags() & Qt::WindowStaysOnTopHint); }});
    r->setHandler(IDM_VIEW_FULLSCREENTOGGLE, {[this] { isFullScreen() ? showNormal() : showFullScreen(); }, {}, [this] { return isFullScreen(); }});
    r->setHandler(IDM_VIEW_DOCLIST, {[this] { m_dock->isPanelVisible(kPanelDocList) ? m_dock->hidePanel(kPanelDocList) : m_dock->showPanel(kPanelDocList); }});
    r->setHandler(IDM_VIEW_FILEBROWSER, {[this] { m_dock->isPanelVisible(kPanelFolder) ? m_dock->hidePanel(kPanelFolder) : m_dock->showPanel(kPanelFolder); }});

    // Tabs / split view
    auto tabCount = [this] { return currentPane()->count(); };
    r->setHandler(IDM_VIEW_TAB_NEXT, {[this] {
        EditorPane* p = currentPane();
        if (p->count() > 1)
            p->activate(p->bufferAt((p->currentIndex() + 1) % p->count()));
    }});
    r->setHandler(IDM_VIEW_TAB_PREV, {[this] {
        EditorPane* p = currentPane();
        if (p->count() > 1)
            p->activate(p->bufferAt((p->currentIndex() + p->count() - 1) % p->count()));
    }});
    for (int i = 0; i < 9; ++i)
        r->setHandler(IDM_VIEW_TAB1 + i, {[this, i] {
            EditorPane* p = currentPane();
            const int idx = i == 8 ? p->count() - 1 : i; // TAB9 = last tab
            if (idx < p->count())
                p->activate(p->bufferAt(idx));
        }});
    auto activateAt = [this](auto index) {
        EditorPane* p = currentPane();
        if (p->count() > 0)
            p->activate(p->bufferAt(index(p)));
    };
    r->setHandler(IDM_VIEW_TAB_START, {[activateAt] { activateAt([](EditorPane*) { return 0; }); }});
    r->setHandler(IDM_VIEW_TAB_END, {[activateAt] { activateAt([](EditorPane* p) { return p->count() - 1; }); }});
    auto moveTab = [this](auto target) {
        EditorPane* p = currentPane();
        const int from = p->currentIndex();
        const int to = std::clamp(target(p, from), 0, p->count() - 1);
        if (from >= 0 && to != from)
            p->tabBar()->moveTab(from, to);
    };
    r->setHandler(IDM_VIEW_GOTO_START, {[moveTab] { moveTab([](EditorPane*, int) { return 0; }); }});
    r->setHandler(IDM_VIEW_GOTO_END, {[moveTab] { moveTab([](EditorPane* p, int) { return p->count() - 1; }); }});
    r->setHandler(IDM_VIEW_TAB_MOVEFORWARD, {[moveTab] { moveTab([](EditorPane*, int i) { return i + 1; }); }});
    r->setHandler(IDM_VIEW_TAB_MOVEBACKWARD, {[moveTab] { moveTab([](EditorPane*, int i) { return i - 1; }); }});

    auto viewIn = [this](const char* program) {
        const Buffer* b = currentBuffer();
        if (!b || b->isUntitled())
            return;
        const QString url = QUrl::fromLocalFile(q(b->path)).toString();
        if (!program || !QProcess::startDetached(QString::fromLatin1(program), {url}))
            QDesktopServices::openUrl(QUrl(url)); // fall back to the default browser
    };
    auto fileOpen = [this] { const Buffer* b = currentBuffer(); return b && !b->isUntitled(); };
#if defined(Q_OS_WIN)
    const char* firefox = "firefox.exe"; const char* chrome = "chrome.exe"; const char* edge = "msedge.exe";
#else
    const char* firefox = "firefox"; const char* chrome = "google-chrome"; const char* edge = "microsoft-edge";
#endif
    r->setHandler(IDM_VIEW_IN_FIREFOX, {[viewIn, firefox] { viewIn(firefox); }, fileOpen});
    r->setHandler(IDM_VIEW_IN_CHROME, {[viewIn, chrome] { viewIn(chrome); }, fileOpen});
    r->setHandler(IDM_VIEW_IN_EDGE, {[viewIn, edge] { viewIn(edge); }, fileOpen});
    r->setHandler(IDM_VIEW_IN_IE, {[viewIn] { viewIn(nullptr); }, fileOpen}); // IE is gone: default browser

    r->setHandler(IDM_VIEW_ZOOM_SYNC, {[this, &g] {
        g.zoomSync = !g.zoomSync;
        if (g.zoomSync)
            otherPane()->editor()->send(SCI_SETZOOM, static_cast<uptr_t>(currentPane()->editor()->send(SCI_GETZOOM)));
    }, {}, [&g] { return g.zoomSync; }});
    for (EditorPane* p : m_panes)
        connect(p->editor(), &ScintillaEditBase::zoom, this, [this, p, &g](int z) {
            EditorPane* other = m_panes[static_cast<size_t>(1 - p->viewIndex())];
            if (g.zoomSync && other->editor()->send(SCI_GETZOOM) != z)
                other->editor()->send(SCI_SETZOOM, static_cast<uptr_t>(z));
        });

    // Distraction free / Post-it: hide the window chrome (upstream restores it on toggle)
    auto windowChrome = [this](bool hide, bool postIt) {
        menuBar()->setVisible(!hide && m_config.gui.menuBarShow);
        statusBar()->setVisible(!hide && m_config.gui.statusBarShow);
        for (QToolBar* tb : findChildren<QToolBar*>())
            tb->setVisible(!hide && m_config.gui.toolbarShow);
        for (EditorPane* p : m_panes)
            p->tabBar()->setVisible(!hide);
        if (postIt) {
            setWindowFlag(Qt::FramelessWindowHint, hide);
            setWindowFlag(Qt::WindowStaysOnTopHint, hide);
            show();
        } else {
            hide ? showFullScreen() : showNormal();
        }
    };
    r->setHandler(IDM_VIEW_DISTRACTIONFREE, {[this, windowChrome] {
        m_distractionFree = !m_distractionFree;
        windowChrome(m_distractionFree, false);
    }, {}, [this] { return m_distractionFree; }});
    r->setHandler(IDM_VIEW_POSTIT, {[this, windowChrome] {
        m_postIt = !m_postIt;
        windowChrome(m_postIt, true);
    }, {}, [this] { return m_postIt; }});

    r->setHandler(IDM_VIEW_GOTO_ANOTHER_VIEW, {[this] { moveOrCloneToOtherView(false); }, [tabCount] { return tabCount() > 0; }});
    r->setHandler(IDM_VIEW_CLONE_TO_ANOTHER_VIEW, {[this] { moveOrCloneToOtherView(true); }, [tabCount] { return tabCount() > 0; }});
    r->setHandler(IDM_VIEW_SWITCHTO_OTHER_VIEW, {[this] {
        if (otherPane()->count() > 0)
            setActivePane(otherPane());
    }, [this] { return otherPane()->count() > 0; }});
    r->setHandler(IDM_VIEW_GOTO_NEW_INSTANCE, {[this] {
        const Buffer* b = currentBuffer();
        if (!b || b->isUntitled() || b->dirty)
            return;
        const QString path = q(b->path);
        if (QProcess::startDetached(QCoreApplication::applicationFilePath(), {QStringLiteral("-multiInst"), QStringLiteral("-nosession"), path}))
            closeBuffer(b->id, currentPane());
    }, [this] { const Buffer* b = currentBuffer(); return b && !b->isUntitled() && !b->dirty; }});
    r->setHandler(IDM_VIEW_LOAD_IN_NEW_INSTANCE, {[this] {
        if (const Buffer* b = currentBuffer(); b && !b->isUntitled())
            QProcess::startDetached(QCoreApplication::applicationFilePath(), {QStringLiteral("-multiInst"), QStringLiteral("-nosession"), q(b->path)});
    }, [this] { const Buffer* b = currentBuffer(); return b && !b->isUntitled(); }});
}

// ------------------------------------------------------------------------------------------------
// Encoding / EOL
// ------------------------------------------------------------------------------------------------

void MainWindow::registerFormatCommands()
{
    ActionRegistry* r = m_actions;
    auto cur = [this] { return currentBuffer(); };

    // EOL conversion
    const std::pair<int, EolType> eols[] = {{IDM_FORMAT_TODOS, EolType::Windows}, {IDM_FORMAT_TOUNIX, EolType::Unix}, {IDM_FORMAT_TOMAC, EolType::Mac}};
    for (auto [id, eol] : eols)
        r->setHandler(id, {[this, eol] {
            Buffer* b = currentBuffer();
            if (!b)
                return;
            ScintillaEditBase* e = currentPane()->editor();
            e->send(SCI_CONVERTEOLS, static_cast<uptr_t>(scintillaEolMode(eol)));
            e->send(SCI_SETEOLMODE, static_cast<uptr_t>(scintillaEolMode(eol)));
            if (b->eol != eol) {
                b->eol = eol;
                b->dirty = true;
            }
            refreshBuffer(b);
        }, {}, [cur, eol] { const Buffer* b = cur(); return b && b->eol == eol; }});

    // "Encode in": reinterpret the file bytes (reload)
    const std::pair<int, UniMode> modes[] = {
        {IDM_FORMAT_ANSI, UniMode::Ansi},          {IDM_FORMAT_AS_UTF_8, UniMode::Utf8},
        {IDM_FORMAT_UTF_8, UniMode::Utf8Bom},      {IDM_FORMAT_UTF_16BE, UniMode::Utf16BE},
        {IDM_FORMAT_UTF_16LE, UniMode::Utf16LE},
    };
    for (auto [id, mode] : modes)
        r->setHandler(id, {[this, mode] {
            Buffer* b = currentBuffer();
            if (!b)
                return;
            if (b->isUntitled() || (b->dirty && QMessageBox::question(this, QStringLiteral("Notepad++"),
                    tr("The current document has unsaved changes. Changing the encoding reloads the file and loses them. Continue?")) != QMessageBox::Yes)) {
                if (b->isUntitled()) { // nothing to reinterpret: just set the mode
                    b->uniMode = mode;
                    b->encoding = -1;
                    refreshBuffer(b);
                }
                return;
            }
            reloadBuffer(b, -1, mode);
        }, {}, [cur, mode] { const Buffer* b = cur(); return b && b->encoding == -1 && b->uniMode == mode; }});

    // "Convert to": keep the text, change how it will be written
    const std::pair<int, UniMode> conv[] = {
        {IDM_FORMAT_CONV2_ANSI, UniMode::Ansi},       {IDM_FORMAT_CONV2_AS_UTF_8, UniMode::Utf8},
        {IDM_FORMAT_CONV2_UTF_8, UniMode::Utf8Bom},   {IDM_FORMAT_CONV2_UTF_16BE, UniMode::Utf16BE},
        {IDM_FORMAT_CONV2_UTF_16LE, UniMode::Utf16LE},
    };
    for (auto [id, mode] : conv)
        r->setHandler(id, {[this, mode] {
            Buffer* b = currentBuffer();
            if (!b || (b->uniMode == mode && b->encoding == -1))
                return;
            b->uniMode = mode;
            b->encoding = -1;
            b->dirty = true;
            refreshBuffer(b);
        }});

    // Character sets (Encoding > Character sets > ...)
    const int count = static_cast<int>(EncodingMapper::table().size());
    for (int i = 0; i < count; ++i)
        r->setHandler(IDM_FORMAT_ENCODE + i, {[this, i] {
            Buffer* b = currentBuffer();
            if (!b)
                return;
            const int enc = EncodingMapper::encodingFromIndex(i);
            if (b->isUntitled()) {
                b->encoding = enc;
                refreshBuffer(b);
                return;
            }
            if (b->dirty && QMessageBox::question(this, QStringLiteral("Notepad++"),
                    tr("The current document has unsaved changes. Changing the encoding reloads the file and loses them. Continue?")) != QMessageBox::Yes)
                return;
            reloadBuffer(b, enc);
        }, {}, [cur, i] { const Buffer* b = cur(); return b && b->encoding != -1 && b->encoding == EncodingMapper::encodingFromIndex(i); }});
}

// ------------------------------------------------------------------------------------------------
// Language
// ------------------------------------------------------------------------------------------------

void MainWindow::registerLanguageCommands()
{
    for (const LangCmd& lc : kLangCommands) {
        const LangType lang = lc.lang;
        m_actions->setHandler(lc.id, {[this, lang] { setLanguage(currentBuffer(), lang); }, {},
                                      [this, lang] { const Buffer* b = currentBuffer(); return b && b->lang == lang; }});
    }
    // User defined languages: IDM_LANG_USER + 1 + n (upstream order of the UDL list)
    const auto& udls = m_udls.languages();
    for (size_t i = 0; i < udls.size() && i < static_cast<size_t>(IDM_LANG_USER_LIMIT - IDM_LANG_USER); ++i) {
        const std::string name = udls[i].name;
        QAction* a = m_actions->action(IDM_LANG_USER + 1 + static_cast<int>(i));
        a->setText(q(name));
        m_actions->setHandler(IDM_LANG_USER + 1 + static_cast<int>(i), {[this, name] { setLanguage(currentBuffer(), L_USER, name); }, {},
            [this, name] { const Buffer* b = currentBuffer(); return b && b->lang == L_USER && b->userLangName == name; }});
    }
}

// ------------------------------------------------------------------------------------------------
// Misc (Settings / ? )
// ------------------------------------------------------------------------------------------------

void MainWindow::registerMiscCommands()
{
    ActionRegistry* r = m_actions;
    r->setHandler(IDM_ABOUT, {[this] {
        QMessageBox::about(this, tr("About Notepad++ Qt"),
                           tr("Notepad++ Qt %1\nQt port of Notepad++ %2\nQt %3")
                               .arg(QCoreApplication::applicationVersion(), QStringLiteral("8.9.8.1"), QString::fromLatin1(qVersion())));
    }});
    r->setHandler(IDM_HOMESWEETHOME, {[] { QDesktopServices::openUrl(QUrl(QStringLiteral("https://notepad-plus-plus.org/"))); }});
    r->setHandler(IDM_PROJECTPAGE, {[] { QDesktopServices::openUrl(QUrl(QStringLiteral("https://github.com/asprenas/notepad-plus-plus"))); }});
    r->setHandler(IDM_SETTING_EDITCONTEXTMENU, {[this] { openFile(q(m_paths.contextMenuXml())); }});
}

} // namespace npp::ui
