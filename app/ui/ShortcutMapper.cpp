#include "ShortcutMapper.h"

#include "DialogTranslator.h"

#include "menu/ActionRegistry.h"
#include "menu/CommandIds.h"

#include "core/i18n/NativeLanguage.h"

#include <Scintilla.h>

#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QPushButton>
#include <QTabWidget>
#include <QTableWidget>
#include <QVBoxLayout>

#include <map>

namespace npp::ui {

using namespace npp::core;

namespace {

struct DefKey {
    int vk, id;
    bool ctrl, alt, shift;
    const char* name;
};
const std::vector<DefKey>& defaultKeys()
{
    static const std::vector<DefKey> v = {
#define NPP_DEFKEY(vk, id, c, a, s, name) {vk, cmd::id, c, a, s, name},
#include "menu/DefaultShortcuts.inc"
#undef NPP_DEFKEY
    };
    return v;
}

struct SciKeyDef {
    const char* name;
    int sci;
    bool ctrl, alt, shift;
    int vk, cmd;
};
const std::vector<SciKeyDef>& scintillaKeys()
{
    static const std::vector<SciKeyDef> v = {
#define NPP_SCIKEY(name, sci, c, a, s, vk, id) {name, sci, c, a, s, vk, id},
#include "menu/ScintillaKeys.inc"
#undef NPP_SCIKEY
    };
    return v;
}

// Top-level menu of a command's action (the upstream "Category" column)
QString categoryOf(QAction* a)
{
    for (QObject* o : a->associatedObjects())
        if (auto* m = qobject_cast<QMenu*>(o)) {
            QMenu* top = m;
            for (;;) {
                QMenu* parentMenu = nullptr;
                for (QObject* p : top->menuAction()->associatedObjects())
                    if (auto* pm = qobject_cast<QMenu*>(p))
                        parentMenu = pm;
                if (!parentMenu)
                    break;
                top = parentMenu;
            }
            QString t = top->title();
            t.remove(QLatin1Char('&'));
            return t;
        }
    return {};
}

QString clean(QString s)
{
    s.replace(QLatin1String("&&"), QStringLiteral("\x01"));
    s.remove(QLatin1Char('&'));
    s.replace(QLatin1Char('\x01'), QLatin1Char('&'));
    if (s.endsWith(QLatin1String("...")))
        s.chop(3);
    return s.split(QLatin1Char('\t')).first();
}

} // namespace

ShortcutMapper::ShortcutMapper(const ShortcutsFile& current, ActionRegistry* actions, const NativeLanguage* nl, QWidget* parent)
    : QDialog(parent), m_file(current)
{
    setObjectName(QStringLiteral("ShortcutMapper"));
    setWindowTitle(tr("Shortcut mapper"));

    // Main menu: every command of the upstream winKeyDefs table
    std::map<int, KeyCombo> user;
    for (const CommandShortcut& c : current.internalCommands)
        if (c.nth == 0)
            user[c.id] = c.combo;
    for (const DefKey& k : defaultKeys()) {
        if (!k.id)
            continue;
        Row r;
        r.tab = 0;
        r.id = k.id;
        QAction* a = actions->action(k.id);
        r.name = nl && !nl->shortcutName(k.id).empty() ? QString::fromStdString(nl->shortcutName(k.id))
                 : k.name[0] ? QString::fromUtf8(k.name) : clean(a->text());
        r.category = categoryOf(a);
        r.defaultKey = {k.ctrl, k.alt, k.shift, k.vk};
        const auto it = user.find(k.id);
        r.key = it != user.end() ? it->second : r.defaultKey;
        m_rows.push_back(r);
    }
    for (size_t i = 0; i < current.macros.size(); ++i)
        m_rows.push_back({1, static_cast<int>(i), QString::fromStdString(current.macros[i].name), {}, current.macros[i].combo, {}});
    for (size_t i = 0; i < current.userCommands.size(); ++i)
        m_rows.push_back({2, static_cast<int>(i), QString::fromStdString(current.userCommands[i].name), {}, current.userCommands[i].combo, {}});
    for (size_t i = 0; i < current.pluginCommands.size(); ++i)
        m_rows.push_back({3, static_cast<int>(i), QString::fromStdString(current.pluginCommands[i].moduleName) + QStringLiteral(" #") +
                                                       QString::number(current.pluginCommands[i].internalId),
                          {}, current.pluginCommands[i].combo, {}});
    std::map<int, KeyCombo> userSci;
    for (const ScintillaKeyShortcut& s : current.scintillaKeys)
        if (!s.combos.empty())
            userSci[s.scintId] = s.combos.front();
    for (const SciKeyDef& s : scintillaKeys()) {
        Row r;
        r.tab = 4;
        r.id = s.sci;
        r.name = QString::fromLatin1(s.name);
        r.defaultKey = {s.ctrl, s.alt, s.shift, s.vk};
        const auto it = userSci.find(s.sci);
        r.key = it != userSci.end() ? it->second : r.defaultKey;
        m_rows.push_back(r);
    }

    // upstream getShortcutMapperLangStr: <Dialog><ShortcutMapper><Node name=""/>
    auto smStr = [nl](const char* node, const char* fallback) {
        return nl ? QString::fromStdString(nl->attribute({"Dialog", "ShortcutMapper", node}, "name", fallback)) : QString::fromUtf8(fallback);
    };
    m_tabs = new QTabWidget;
    const char* tabNames[5] = {"Main menu", "Macros", "Run commands", "Plugin commands", "Scintilla commands"};
    const char* tabNodes[5] = {"MainMenuTab", "MacrosTab", "RunCommandsTab", "PluginCommandsTab", "ScintillaCommandsTab"};
    for (int t = 0; t < 5; ++t) {
        auto* table = new QTableWidget(0, t == 0 ? 3 : 2);
        const QString cName = smStr("ColumnName", "Name"), cKey = smStr("ColumnShortcut", "Shortcut");
        table->setHorizontalHeaderLabels(t == 0 ? QStringList{cName, cKey, smStr("ColumnCategory", "Category")} : QStringList{cName, cKey});
        table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
        table->setSelectionBehavior(QAbstractItemView::SelectRows);
        table->setSelectionMode(QAbstractItemView::SingleSelection);
        table->setEditTriggers(QAbstractItemView::NoEditTriggers);
        table->verticalHeader()->setDefaultSectionSize(table->fontMetrics().height() + 6);
        connect(table, &QTableWidget::cellDoubleClicked, this, [this, t](int row) { modify(t, row); });
        m_tables[t] = table;
        m_tabs->addTab(table, smStr(tabNodes[t], tabNames[t]));
    }
    m_filter = new QLineEdit;
    m_filter->setPlaceholderText(tr("Filter"));
    if (nl)
        if (const auto d = nl->dialog("ShortcutMapper"); d && d->items.count(2607))
            m_filter->setPlaceholderText(controlText(d->items.at(2607), false).trimmed());
    connect(m_filter, &QLineEdit::textChanged, this, &ShortcutMapper::fill);
    m_info = new QLabel;
    m_info->setObjectName(QStringLiteral("npp-data"));
    m_noConflictText = smStr("ConflictInfoOk", "No shortcut conflicts for this item.");
    m_info->setWordWrap(true);
    auto* modifyBtn = new QPushButton(tr("Modify"));
    auto* clearBtn = new QPushButton(tr("Clear"));
    auto* resetBtn = new QPushButton(tr("Reset"));
    connect(modifyBtn, &QPushButton::clicked, this, [this] { modify(m_tabs->currentIndex(), m_tables[m_tabs->currentIndex()]->currentRow()); });
    connect(clearBtn, &QPushButton::clicked, this, [this] {
        const int t = m_tabs->currentIndex(), row = m_tables[t]->currentRow();
        const auto idx = rowsOfTab(t);
        if (row >= 0 && row < static_cast<int>(idx.size())) {
            m_rows[idx[static_cast<size_t>(row)]].key = {};
            fill();
        }
    });
    connect(resetBtn, &QPushButton::clicked, this, [this] {
        const int t = m_tabs->currentIndex(), row = m_tables[t]->currentRow();
        const auto idx = rowsOfTab(t);
        if (row >= 0 && row < static_cast<int>(idx.size())) {
            Row& r = m_rows[idx[static_cast<size_t>(row)]];
            r.key = r.defaultKey;
            fill();
        }
    });
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    auto* row = new QHBoxLayout;
    row->addWidget(m_filter, 1);
    row->addWidget(modifyBtn);
    row->addWidget(clearBtn);
    row->addWidget(resetBtn);
    auto* root = new QVBoxLayout(this);
    root->addWidget(m_tabs, 1);
    root->addWidget(m_info);
    root->addLayout(row);
    root->addWidget(buttons);
    setControlId(modifyBtn, 2602);
    setControlId(clearBtn, 2606);
    translateDialog(this, nl, "ShortcutMapper");
    if (nl)
        if (const auto d = nl->dialog("ShortcutMapperSubDialg")) { // OK / Cancel
            if (d->items.count(1))
                buttons->button(QDialogButtonBox::Ok)->setText(controlText(d->items.at(1), true));
            if (d->items.count(2))
                buttons->button(QDialogButtonBox::Cancel)->setText(controlText(d->items.at(2), true));
        }
    resize(760, 600);
    fill();
}

QString ShortcutMapper::keyText(const KeyCombo& k) const
{
    return k.isEnabled() ? QString::fromStdString(keyComboToString(k)) : QString();
}

std::vector<size_t> ShortcutMapper::rowsOfTab(int tab) const
{
    std::vector<size_t> out;
    const QString f = m_filter ? m_filter->text().trimmed() : QString();
    for (size_t i = 0; i < m_rows.size(); ++i)
        if (m_rows[i].tab == tab && (f.isEmpty() || m_rows[i].name.contains(f, Qt::CaseInsensitive)
                                     || keyText(m_rows[i].key).contains(f, Qt::CaseInsensitive)
                                     || m_rows[i].category.contains(f, Qt::CaseInsensitive)))
            out.push_back(i);
    return out;
}

QStringList ShortcutMapper::conflicts() const
{
    // Same key on two entries; Scintilla keys only conflict with menu commands that are not their own command
    static const char* tabNames[5] = {"Main menu", "Macros", "Run commands", "Plugin commands", "Scintilla commands"};
    QStringList out;
    for (size_t i = 0; i < m_rows.size(); ++i)
        for (size_t j = i + 1; j < m_rows.size(); ++j)
            if (m_rows[i].key.isEnabled() && m_rows[i].key == m_rows[j].key)
                out << QStringLiteral("%1 (%2) <-> %3 (%4)").arg(m_rows[i].name, QString::fromLatin1(tabNames[m_rows[i].tab]), m_rows[j].name,
                                                               QString::fromLatin1(tabNames[m_rows[j].tab]));
    return out;
}

void ShortcutMapper::fill()
{
    std::map<std::tuple<bool, bool, bool, int>, int> uses;
    for (const Row& r : m_rows)
        if (r.key.isEnabled())
            ++uses[{r.key.ctrl, r.key.alt, r.key.shift, r.key.key}];
    for (int t = 0; t < 5; ++t) {
        QTableWidget* table = m_tables[t];
        const auto idx = rowsOfTab(t);
        table->setRowCount(static_cast<int>(idx.size()));
        for (int i = 0; i < static_cast<int>(idx.size()); ++i) {
            const Row& r = m_rows[idx[static_cast<size_t>(i)]];
            auto* name = new QTableWidgetItem(r.name);
            auto* key = new QTableWidgetItem(keyText(r.key));
            const bool conflict = r.key.isEnabled() && uses[{r.key.ctrl, r.key.alt, r.key.shift, r.key.key}] > 1;
            if (conflict) {
                name->setForeground(Qt::red);
                key->setForeground(Qt::red);
            }
            table->setItem(i, 0, name);
            table->setItem(i, 1, key);
            if (t == 0)
                table->setItem(i, 2, new QTableWidgetItem(r.category));
        }
    }
    const QStringList c = conflicts();
    m_info->setText(c.isEmpty() ? m_noConflictText : c.join(QLatin1Char('\n')));
}

void ShortcutMapper::modify(int tab, int row)
{
    const auto idx = rowsOfTab(tab);
    if (row < 0 || row >= static_cast<int>(idx.size()))
        return;
    Row& r = m_rows[idx[static_cast<size_t>(row)]];
    QDialog dlg(this);
    dlg.setWindowTitle(r.name);
    auto* edit = new QKeySequenceEdit;
    edit->setMaximumSequenceLength(1);
    if (const auto k = toQtKey(r.key))
        edit->setKeySequence(QKeySequence(*k));
    auto* form = new QFormLayout(&dlg);
    form->addRow(tr("Shortcut :"), edit);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    if (dlg.exec() != QDialog::Accepted)
        return;
    KeyCombo k;
    if (!edit->keySequence().isEmpty())
        if (const auto c = fromQtKey(edit->keySequence()[0]))
            k = *c;
    setKey(tab, r.id, k);
}

bool ShortcutMapper::setKey(int tab, int id, const KeyCombo& key)
{
    for (Row& r : m_rows)
        if (r.tab == tab && r.id == id) {
            r.key = key;
            fill();
            return true;
        }
    return false;
}

ShortcutsFile ShortcutMapper::result() const
{
    ShortcutsFile f = m_file;
    // upstream writes only the menu commands whose shortcut differs from the default
    std::vector<CommandShortcut> extra; // nth > 0 entries are kept as they were
    for (const CommandShortcut& c : f.internalCommands)
        if (c.nth > 0)
            extra.push_back(c);
    f.internalCommands.clear();
    std::map<int, ScintillaKeyShortcut> sciUser;
    for (const ScintillaKeyShortcut& s : m_file.scintillaKeys)
        sciUser[s.scintId] = s;
    for (const Row& r : m_rows) {
        switch (r.tab) {
        case 0:
            if (!(r.key == r.defaultKey))
                f.internalCommands.push_back({r.id, r.key, 0});
            break;
        case 1: f.macros[static_cast<size_t>(r.id)].combo = r.key; break;
        case 2: f.userCommands[static_cast<size_t>(r.id)].combo = r.key; break;
        case 3: f.pluginCommands[static_cast<size_t>(r.id)].combo = r.key; break;
        case 4: {
            if (r.key == r.defaultKey) {
                sciUser.erase(r.id);
            } else {
                ScintillaKeyShortcut& s = sciUser[r.id];
                s.scintId = r.id;
                if (s.combos.empty())
                    s.combos.push_back(r.key);
                else
                    s.combos.front() = r.key;
            }
            break;
        }
        }
    }
    f.internalCommands.insert(f.internalCommands.end(), extra.begin(), extra.end());
    f.scintillaKeys.clear();
    for (auto& [id, s] : sciUser)
        f.scintillaKeys.push_back(s);
    return f;
}

} // namespace npp::ui
