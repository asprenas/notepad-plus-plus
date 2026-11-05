// Window menu (upstream WinControls/WindowsDlg: Windows... dialog, Sort By, open documents list)
// and the Ctrl+Tab document switcher (upstream TaskListDlg, MRU or tab order).
#include "MainWindow.h"

#include "editor/EditorPane.h"
#include "menu/ActionRegistry.h"
#include "menu/CommandIds.h"

#include "core/util/PathUtil.h"

#include <Scintilla.h>
#include <ScintillaEditBase.h>

#include <QApplication>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeyEvent>
#include <QListWidget>
#include <QMenu>
#include <QPushButton>
#include <QScreen>
#include <QShortcut>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>

namespace npp::ui {

using namespace npp::core;
using namespace cmd;

namespace {

QString q(const std::string& s) { return QString::fromStdString(s); }

QMenu* menuOfAction(QAction* a)
{
    for (QObject* o : a->associatedObjects())
        if (auto* m = qobject_cast<QMenu*>(o))
            return m;
    return nullptr;
}

constexpr int kMaxWindowMenuItems = 20; // upstream WindowsMenu: IDM_WINDOW_MRU_LIMIT

} // namespace

// --- Ctrl+Tab switcher --------------------------------------------------------------------------------

class TaskListPopup final : public QListWidget {
public:
    TaskListPopup(QWidget* parent, std::function<void(int)> done) : QListWidget(parent), m_done(std::move(done))
    {
        setWindowFlags(Qt::Popup | Qt::FramelessWindowHint);
        setObjectName(QStringLiteral("taskList"));
        connect(this, &QListWidget::itemClicked, this, [this](QListWidgetItem* it) { finish(row(it)); });
    }
    void step(bool down)
    {
        const int n = count();
        if (n)
            setCurrentRow(((currentRow() + (down ? 1 : -1)) % n + n) % n);
    }
    void finish(int r)
    {
        if (m_finished)
            return;
        m_finished = true;
        hide();
        m_done(r);
        deleteLater();
    }

protected:
    void keyPressEvent(QKeyEvent* e) override
    {
        switch (e->key()) {
        case Qt::Key_Tab: step(true); return;
        case Qt::Key_Backtab: step(false); return;
        case Qt::Key_Down: step(true); return;
        case Qt::Key_Up: step(false); return;
        case Qt::Key_Escape: finish(-1); return;
        case Qt::Key_Return:
        case Qt::Key_Enter: finish(currentRow()); return;
        default: break;
        }
        QListWidget::keyPressEvent(e);
    }
    void keyReleaseEvent(QKeyEvent* e) override
    {
        if (e->key() == Qt::Key_Control) {
            finish(currentRow());
            return;
        }
        QListWidget::keyReleaseEvent(e);
    }
    void hideEvent(QHideEvent* e) override
    {
        QListWidget::hideEvent(e);
        if (!m_finished) // closed by a click elsewhere
            QMetaObject::invokeMethod(this, [this] { finish(-1); }, Qt::QueuedConnection);
    }

private:
    std::function<void(int)> m_done;
    bool m_finished = false;
};

QWidget* MainWindow::taskList() const { return m_taskList; }

std::vector<std::pair<BufferId, int>> MainWindow::switcherOrder() const
{
    std::vector<std::pair<BufferId, int>> order;
    auto open = [this](BufferId id, int view) { return m_panes[static_cast<size_t>(view)]->indexOf(id) >= 0; };
    if (m_config.gui.styleMru) {
        for (const auto& e : m_mru)
            if (open(e.first, e.second))
                order.push_back(e);
    }
    // documents never activated yet (and tab order mode): tab order, current view first
    for (const auto& [id, p] : allTabs())
        if (std::find(order.begin(), order.end(), std::make_pair(id, p->viewIndex())) == order.end())
            order.emplace_back(id, p->viewIndex());
    return order;
}

void MainWindow::noteActivated(BufferId id, int view)
{
    const auto e = std::make_pair(id, view);
    m_mru.erase(std::remove(m_mru.begin(), m_mru.end(), e), m_mru.end());
    m_mru.push_front(e);
}

void MainWindow::switchDocument(bool next)
{
    const auto order = switcherOrder();
    if (order.size() < 2)
        return;
    auto activate = [this, order](int i) {
        if (i < 0 || i >= static_cast<int>(order.size()))
            return;
        EditorPane* p = m_panes[static_cast<size_t>(order[static_cast<size_t>(i)].second)];
        p->activate(order[static_cast<size_t>(i)].first);
        setActivePane(p);
    };
    if (!m_config.gui.doTaskList) { // upstream activateNextDoc
        int cur = 0;
        for (size_t i = 0; i < order.size(); ++i)
            if (order[i].first == currentPane()->current() && order[i].second == currentPane()->viewIndex())
                cur = static_cast<int>(i);
        const int n = static_cast<int>(order.size());
        if (m_config.gui.styleMru)
            activate(next ? 1 : n - 1);
        else
            activate(((cur + (next ? 1 : -1)) % n + n) % n);
        return;
    }
    if (m_taskList) {
        m_taskList->step(next);
        return;
    }
    m_taskList = new TaskListPopup(this, [this, activate](int r) {
        m_taskList = nullptr;
        activate(r);
    });
    for (const auto& [id, view] : order)
        if (Buffer* b = buffer(id)) {
            auto* item = new QListWidgetItem(tabText(*b), m_taskList);
            item->setToolTip(b->isUntitled() ? QString() : QDir::toNativeSeparators(q(b->path)));
        }
    const int start = m_config.gui.styleMru ? (next ? 1 : static_cast<int>(order.size()) - 1) : 0;
    m_taskList->setCurrentRow(start);
    if (!m_config.gui.styleMru)
        m_taskList->step(next);
    const int w = std::min(500, std::max(250, m_taskList->sizeHintForColumn(0) + 40));
    const int h = std::min(600, m_taskList->sizeHintForRow(0) * m_taskList->count() + 8);
    m_taskList->resize(w, h);
    const QRect r = geometry();
    m_taskList->move(r.center() - QPoint(w / 2, h / 2));
    m_taskList->show();
    m_taskList->setFocus();
    // Ctrl may already be released (shortcut typed quickly / tests): then this is a single step
    if (!(QApplication::queryKeyboardModifiers() & Qt::ControlModifier) && !m_taskListKeepOpen)
        m_taskList->finish(m_taskList->currentRow());
}

// --- Windows... dialog -------------------------------------------------------------------------------------

namespace {

enum SortKey { ByName, ByPath, ByType, BySize, ByTime };

} // namespace

void MainWindow::sortTabs(int key, bool ascending)
{
    EditorPane* p = currentPane();
    std::vector<BufferId> ids = p->buffers();
    auto keyOf = [this, key](BufferId id) -> std::pair<QString, qint64> {
        const Buffer* b = buffer(id);
        if (!b)
            return {};
        switch (key) {
        case ByName: return {q(b->displayName()).toLower(), 0};
        case ByPath: return {b->isUntitled() ? q(b->displayName()).toLower() : q(b->path).toLower(), 0};
        case ByType: return {QFileInfo(q(b->displayName())).suffix().toLower(), 0};
        case BySize: return {}; // filled below (document length)
        default: return {{}, b->timestamp.value};
        }
    };
    std::vector<std::pair<BufferId, std::pair<QString, qint64>>> keyed;
    for (BufferId id : ids) {
        auto k = keyOf(id);
        if (key == BySize)
            if (Buffer* b = buffer(id)) {
                m_scratch->send(SCI_SETDOCPOINTER, 0, b->document);
                k.second = m_scratch->send(SCI_GETLENGTH);
                m_scratch->send(SCI_SETDOCPOINTER, 0, 0);
            }
        keyed.emplace_back(id, k);
    }
    std::stable_sort(keyed.begin(), keyed.end(), [ascending](const auto& a, const auto& b) {
        const bool less = a.second.first == b.second.first ? a.second.second < b.second.second : a.second.first < b.second.first;
        const bool greater = a.second.first == b.second.first ? b.second.second < a.second.second : b.second.first < a.second.first;
        return ascending ? less : greater;
    });
    for (int to = 0; to < static_cast<int>(keyed.size()); ++to) {
        const int from = p->indexOf(keyed[static_cast<size_t>(to)].first);
        if (from != to)
            p->moveTab(from, to);
    }
    updateDocumentList();
}

void MainWindow::showWindowsDialog()
{
    QDialog dlg(this);
    dlg.setObjectName(QStringLiteral("windowsDialog"));
    dlg.setWindowTitle(tr("Windows"));
    auto* tree = new QTreeWidget;
    tree->setObjectName(QStringLiteral("windowsList"));
    tree->setHeaderLabels({tr("Name"), tr("Path"), tr("Type"), tr("Size")});
    tree->setRootIsDecorated(false);
    tree->setSelectionMode(QAbstractItemView::ExtendedSelection);
    tree->setSortingEnabled(true);
    tree->sortByColumn(-1, Qt::AscendingOrder);
    auto fill = [this, tree] {
        tree->clear();
        EditorPane* p = currentPane();
        for (BufferId id : p->buffers()) {
            Buffer* b = buffer(id);
            if (!b)
                continue;
            m_scratch->send(SCI_SETDOCPOINTER, 0, b->document);
            const auto len = m_scratch->send(SCI_GETLENGTH);
            m_scratch->send(SCI_SETDOCPOINTER, 0, 0);
            auto* it = new QTreeWidgetItem(tree);
            it->setText(0, tabText(*b));
            it->setText(1, b->isUntitled() ? QString() : QDir::toNativeSeparators(QFileInfo(q(b->path)).absolutePath()));
            it->setText(2, QString::fromStdString(std::string(langInfo(b->lang).shortName)));
            it->setData(3, Qt::DisplayRole, static_cast<qlonglong>(len));
            it->setData(0, Qt::UserRole, QVariant::fromValue<qulonglong>(id));
            if (id == p->current())
                tree->setCurrentItem(it);
        }
        for (int c = 0; c < 4; ++c)
            tree->resizeColumnToContents(c);
    };
    fill();
    auto selected = [tree] {
        std::vector<BufferId> out;
        for (QTreeWidgetItem* it : tree->selectedItems())
            out.push_back(it->data(0, Qt::UserRole).toULongLong());
        return out;
    };
    auto* activate = new QPushButton(tr("&Activate"));
    auto* save = new QPushButton(tr("&Save"));
    auto* close = new QPushButton(tr("&Close window(s)"));
    auto* sort = new QPushButton(tr("Sort &tabs"));
    auto* ok = new QPushButton(tr("&OK"));
    connect(activate, &QPushButton::clicked, &dlg, [&] {
        const auto s = selected();
        if (s.size() == 1) {
            currentPane()->activate(s.front());
            dlg.accept();
        }
    });
    connect(tree, &QTreeWidget::itemActivated, &dlg, [&](QTreeWidgetItem*, int) { activate->click(); });
    connect(save, &QPushButton::clicked, &dlg, [&] {
        for (BufferId id : selected())
            if (Buffer* b = buffer(id))
                saveBuffer(b, false);
        fill();
    });
    connect(close, &QPushButton::clicked, &dlg, [&] {
        std::vector<std::pair<BufferId, EditorPane*>> list;
        for (BufferId id : selected())
            list.emplace_back(id, currentPane());
        closeBuffers(list);
        fill();
    });
    connect(sort, &QPushButton::clicked, &dlg, [&] {
        // upstream: the tabs follow the order shown in the list
        EditorPane* p = currentPane();
        for (int to = 0; to < tree->topLevelItemCount(); ++to) {
            const int from = p->indexOf(tree->topLevelItem(to)->data(0, Qt::UserRole).toULongLong());
            if (from >= 0 && from != to)
                p->moveTab(from, to);
        }
        updateDocumentList();
    });
    connect(ok, &QPushButton::clicked, &dlg, &QDialog::accept);
    connect(tree, &QTreeWidget::itemSelectionChanged, &dlg, [&] {
        const auto n = tree->selectedItems().size();
        activate->setEnabled(n == 1);
        save->setEnabled(n > 0);
        close->setEnabled(n > 0);
    });
    emit tree->itemSelectionChanged();
    auto* buttons = new QVBoxLayout;
    for (QPushButton* b : {activate, save, close, sort})
        buttons->addWidget(b);
    buttons->addStretch(1);
    buttons->addWidget(ok);
    auto* layout = new QHBoxLayout(&dlg);
    layout->addWidget(tree, 1);
    layout->addLayout(buttons);
    dlg.resize(700, 400);
    m_windowsDialog = &dlg;
    dlg.exec();
    m_windowsDialog = nullptr;
}

void MainWindow::updateWindowMenu()
{
    QMenu* menu = menuOfAction(m_actions->action(IDM_WINDOW_WINDOWS));
    if (!menu)
        return;
    QAction* first = m_actions->action(IDM_WINDOW_MRU_FIRST);
    for (QAction* a : m_windowMenuActions)
        delete a;
    m_windowMenuActions.clear();
    first->setVisible(false);
    // upstream WindowsMenu::initPopupMenu: documents of the current view, tab order
    EditorPane* p = currentPane();
    int i = 0;
    for (BufferId id : p->buffers()) {
        if (i >= kMaxWindowMenuItems)
            break;
        Buffer* b = buffer(id);
        if (!b)
            continue;
        QString text = q(buildMenuFileName(60, static_cast<unsigned>(i), b->isUntitled() ? b->displayName() : b->path));
        auto* a = new QAction(text, menu);
        a->setCheckable(true);
        a->setChecked(id == p->current());
        connect(a, &QAction::triggered, this, [this, p, id] {
            p->activate(id);
            setActivePane(p);
        });
        menu->addAction(a);
        m_windowMenuActions.push_back(a);
        ++i;
    }
}

void MainWindow::registerWindowCommands()
{
    ActionRegistry* r = m_actions;
    r->setHandler(IDM_WINDOW_WINDOWS, {[this] { showWindowsDialog(); }});
    const struct {
        int id, key;
        bool asc;
    } sorts[] = {{IDM_WINDOW_SORT_FN_ASC, ByName, true}, {IDM_WINDOW_SORT_FN_DSC, ByName, false},
                 {IDM_WINDOW_SORT_FP_ASC, ByPath, true}, {IDM_WINDOW_SORT_FP_DSC, ByPath, false},
                 {IDM_WINDOW_SORT_FT_ASC, ByType, true}, {IDM_WINDOW_SORT_FT_DSC, ByType, false},
                 {IDM_WINDOW_SORT_FS_ASC, BySize, true}, {IDM_WINDOW_SORT_FS_DSC, BySize, false},
                 {IDM_WINDOW_SORT_FD_ASC, ByTime, true}, {IDM_WINDOW_SORT_FD_DSC, ByTime, false}};
    for (const auto& s : sorts)
        r->setHandler(s.id, {[this, s] { sortTabs(s.key, s.asc); }, [this] { return currentPane()->count() > 1; }});
    r->setHandler(IDM_WINDOW_MRU_FIRST, {[] {}, [] { return false; }});

    // Ctrl+Tab / Ctrl+Shift+Tab (upstream IDC_NEXT_DOC / IDC_PREV_DOC, not in the menus)
    for (bool next : {true, false}) {
        auto* sc = new QShortcut(next ? QKeySequence(Qt::CTRL | Qt::Key_Tab) : QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Backtab), this);
        sc->setContext(Qt::WindowShortcut);
        connect(sc, &QShortcut::activated, this, [this, next] { switchDocument(next); });
    }
}

void MainWindow::connectWindowMenu()
{
    if (QMenu* menu = menuOfAction(m_actions->action(IDM_WINDOW_WINDOWS))) {
        m_windowMenuActions.clear(); // owned by the previous menu
        connect(menu, &QMenu::aboutToShow, this, &MainWindow::updateWindowMenu);
    }
}

} // namespace npp::ui
