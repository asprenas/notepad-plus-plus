#include "FunctionListPanel.h"

#include <QAction>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLineEdit>
#include <QMenu>
#include <QStyle>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace npp::ui {

namespace {

constexpr int kPosRole = Qt::UserRole;      // leaf: position (qlonglong), others: -1
constexpr int kKindRole = Qt::UserRole + 1; // 0 root, 1 class node, 2 leaf

QString qs(const std::string& s) { return QString::fromUtf8(s.data(), static_cast<qsizetype>(s.size())); }

void sortChildren(QTreeWidgetItem* item)
{
    QList<QTreeWidgetItem*> kids = item->takeChildren();
    std::stable_sort(kids.begin(), kids.end(), [](QTreeWidgetItem* a, QTreeWidgetItem* b) {
        return QString::compare(a->text(0), b->text(0), Qt::CaseInsensitive) < 0;
    });
    item->addChildren(kids);
    for (QTreeWidgetItem* k : kids)
        sortChildren(k);
}

} // namespace

FunctionListPanel::FunctionListPanel(QWidget* parent) : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(2);

    auto* bar = new QHBoxLayout;
    bar->setContentsMargins(2, 2, 2, 0);
    m_filter = new QLineEdit;
    m_filter->setClearButtonEnabled(true);
    m_filter->setPlaceholderText(tr("Search"));
    m_filter->setObjectName("functionListFilter");
    m_sortButton = new QToolButton;
    m_sortButton->setCheckable(true);
    m_sortButton->setText(QStringLiteral("A↓Z"));
    m_sortButton->setToolTip(tr("Sort"));
    auto* reload = new QToolButton;
    reload->setIcon(style()->standardIcon(QStyle::SP_BrowserReload));
    reload->setToolTip(tr("Reload"));
    auto* prefs = new QToolButton;
    prefs->setText(QStringLiteral("…"));
    prefs->setToolTip(tr("Preferences"));
    prefs->setPopupMode(QToolButton::InstantPopup);
    auto* prefsMenu = new QMenu(prefs);
    QAction* sortDefault = prefsMenu->addAction(tr("Sort functions (A to Z) by default"));
    sortDefault->setCheckable(true);
    connect(prefsMenu, &QMenu::aboutToShow, this, [this, sortDefault] { sortDefault->setChecked(m_sortByDefault); });
    connect(sortDefault, &QAction::toggled, this, [this](bool on) {
        if (on != m_sortByDefault) {
            m_sortByDefault = on;
            emit sortByDefaultChanged(on);
        }
    });
    prefs->setMenu(prefsMenu);
    bar->addWidget(m_filter, 1);
    bar->addWidget(m_sortButton);
    bar->addWidget(reload);
    bar->addWidget(prefs);
    layout->addLayout(bar);

    m_tree = new QTreeWidget;
    m_tree->setHeaderHidden(true);
    m_tree->setObjectName("functionListTree");
    layout->addWidget(m_tree, 1);

    connect(m_filter, &QLineEdit::textChanged, this, [this] { rebuild(); });
    connect(m_sortButton, &QToolButton::toggled, this, [this](bool on) {
        m_sorted = on;
        rebuild();
    });
    connect(reload, &QToolButton::clicked, this, &FunctionListPanel::reloadRequested);
    auto go = [this](QTreeWidgetItem* item) {
        if (!item)
            return;
        const qlonglong pos = item->data(0, kPosRole).toLongLong();
        if (pos >= 0)
            emit gotoPosition(static_cast<intptr_t>(pos));
    };
    connect(m_tree, &QTreeWidget::itemActivated, this, [go](QTreeWidgetItem* item, int) { go(item); });
    connect(m_tree, &QTreeWidget::itemClicked, this, [go](QTreeWidgetItem* item, int) { go(item); });
}

void FunctionListPanel::saveState()
{
    if (m_key.isEmpty() || !m_tree->topLevelItemCount())
        return;
    TreeState st;
    st.filter = m_filter->text();
    st.sorted = m_sorted;
    QTreeWidgetItem* root = m_tree->topLevelItem(0);
    st.rootCollapsed = !root->isExpanded();
    if (st.filter.isEmpty())
        for (int i = 0; i < root->childCount(); ++i)
            if (root->child(i)->childCount() && !root->child(i)->isExpanded())
                st.collapsed.insert(root->child(i)->text(0));
    m_states.insert(m_key, st);
}

void FunctionListPanel::setResult(const QString& key, const QString& rootName,
                                  const std::vector<core::FoundFunction>& found, bool parsed)
{
    saveState();
    m_key = key;
    m_rootName = rootName;
    m_found = found;
    m_parsed = parsed;

    const auto it = m_states.constFind(key);
    {
        const QSignalBlocker b1(m_filter), b2(m_sortButton);
        m_filter->setText(it != m_states.constEnd() ? it->filter : QString());
        m_sorted = it != m_states.constEnd() ? it->sorted : m_sortByDefault;
        m_sortButton->setChecked(m_sorted);
    }
    rebuild();
    if (it != m_states.constEnd() && m_tree->topLevelItemCount()) {
        QTreeWidgetItem* root = m_tree->topLevelItem(0);
        root->setExpanded(!it->rootCollapsed);
        if (it->filter.isEmpty())
            for (int i = 0; i < root->childCount(); ++i)
                if (it->collapsed.contains(root->child(i)->text(0)))
                    root->child(i)->setExpanded(false);
    }
}

void FunctionListPanel::rebuild()
{
    m_tree->clear();
    if (!m_parsed)
        return;
    const QString filter = m_filter->text();
    auto* root = new QTreeWidgetItem(m_tree, QStringList{m_rootName});
    root->setData(0, kPosRole, -1);
    root->setData(0, kKindRole, 0);
    root->setIcon(0, style()->standardIcon(QStyle::SP_FileIcon));
    QHash<QString, QTreeWidgetItem*> nodes;
    for (const core::FoundFunction& f : m_found) {
        const QString name = qs(f.name);
        if (!filter.isEmpty() && !name.contains(filter, Qt::CaseInsensitive))
            continue;
        QTreeWidgetItem* parent = root;
        if (!f.className.empty()) {
            const QString cls = qs(f.className);
            QTreeWidgetItem*& node = nodes[cls];
            if (!node) {
                node = new QTreeWidgetItem(root, QStringList{cls});
                node->setData(0, kPosRole, static_cast<qlonglong>(f.classPos));
                node->setData(0, kKindRole, 1);
                node->setIcon(0, style()->standardIcon(QStyle::SP_DirIcon));
            }
            parent = node;
        }
        auto* leaf = new QTreeWidgetItem(parent, QStringList{name});
        leaf->setData(0, kPosRole, static_cast<qlonglong>(f.pos));
        leaf->setData(0, kKindRole, 2);
    }
    if (m_sorted)
        sortChildren(root);
    m_tree->expandAll();
}

void FunctionListPanel::markLine(intptr_t line, const std::function<intptr_t(intptr_t)>& lineOf)
{
    if (!m_tree->topLevelItemCount())
        return;
    QTreeWidgetItem* best = nullptr;
    intptr_t bestLine = -1;
    std::function<void(QTreeWidgetItem*)> walk = [&](QTreeWidgetItem* it) {
        for (int i = 0; i < it->childCount(); ++i) {
            QTreeWidgetItem* c = it->child(i);
            if (c->childCount()) {
                walk(c);
                continue;
            }
            const qlonglong pos = c->data(0, kPosRole).toLongLong();
            if (pos < 0)
                continue;
            const intptr_t l = lineOf(static_cast<intptr_t>(pos));
            if (line >= l && l > bestLine) {
                bestLine = l;
                best = c;
            }
        }
    };
    walk(m_tree->topLevelItem(0));
    const QSignalBlocker block(m_tree);
    m_tree->setCurrentItem(best ? best : m_tree->topLevelItem(0));
}

void FunctionListPanel::setSortByDefault(bool sort) { m_sortByDefault = sort; }

void FunctionListPanel::setSorted(bool sorted) { m_sortButton->setChecked(sorted); }

void FunctionListPanel::setFilter(const QString& text) { m_filter->setText(text); }

QStringList FunctionListPanel::visibleEntries() const
{
    QStringList out;
    if (!m_tree->topLevelItemCount())
        return out;
    QTreeWidgetItem* root = m_tree->topLevelItem(0);
    for (int i = 0; i < root->childCount(); ++i) {
        QTreeWidgetItem* c = root->child(i);
        if (c->data(0, kKindRole).toInt() == 1)
            for (int j = 0; j < c->childCount(); ++j)
                out << c->text(0) + "::" + c->child(j)->text(0);
        else
            out << c->text(0);
    }
    return out;
}

} // namespace npp::ui
