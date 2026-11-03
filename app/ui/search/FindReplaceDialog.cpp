#include "FindReplaceDialog.h"

#include "core/config/ConfigFile.h"
#include "core/i18n/NativeLanguage.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFileDialog>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QShortcut>
#include <QTabBar>
#include <QVBoxLayout>

namespace npp::ui {

using namespace npp::core;

namespace {

QComboBox* historyCombo()
{
    auto* c = new QComboBox;
    c->setEditable(true);
    c->setInsertPolicy(QComboBox::NoInsert);
    c->setMinimumWidth(320);
    c->setMaxCount(30);
    return c;
}

void pushFront(QComboBox* c, const QString& text)
{
    if (text.isEmpty())
        return;
    const int i = c->findText(text, Qt::MatchExactly | Qt::MatchCaseSensitive);
    if (i >= 0)
        c->removeItem(i);
    c->insertItem(0, text);
    c->setCurrentIndex(0);
}

QStringList items(const QComboBox* c)
{
    QStringList out;
    for (int i = 0; i < c->count(); ++i)
        out << c->itemText(i);
    return out;
}

} // namespace

FindReplaceDialog::FindReplaceDialog(QWidget* parent) : QDialog(parent)
{
    setWindowTitle(tr("Find"));
    setModal(false);
    setObjectName(QStringLiteral("FindReplaceDialog"));

    m_tabs = new QTabBar;
    for (const char* t : {"Find", "Replace", "Find in Files", "Find in Projects", "Mark"})
        m_tabs->addTab(tr(t));
    m_tabs->setTabVisible(FindInProjectsTab, false); // Project panels arrive in phase 5

    m_find = historyCombo();
    m_replace = historyCombo();
    m_filters = historyCombo();
    m_dir = historyCombo();
    m_findLabel = new QLabel(tr("Find what :"));
    m_replaceLabel = new QLabel(tr("Replace with :"));
    m_filtersLabel = new QLabel(tr("Filters :"));
    m_dirLabel = new QLabel(tr("Directory :"));
    m_browse = new QPushButton(QStringLiteral("..."));
    m_browse->setFixedWidth(30);
    connect(m_browse, &QPushButton::clicked, this, [this] {
        const QString d = QFileDialog::getExistingDirectory(this, tr("Select a folder to search from"), m_dir->currentText());
        if (!d.isEmpty())
            m_dir->setCurrentText(QDir::toNativeSeparators(d));
    });

    auto* fields = new QGridLayout;
    fields->addWidget(m_findLabel, 0, 0);
    fields->addWidget(m_find, 0, 1, 1, 2);
    fields->addWidget(m_replaceLabel, 1, 0);
    fields->addWidget(m_replace, 1, 1, 1, 2);
    fields->addWidget(m_filtersLabel, 2, 0);
    fields->addWidget(m_filters, 2, 1, 1, 2);
    fields->addWidget(m_dirLabel, 3, 0);
    fields->addWidget(m_dir, 3, 1);
    fields->addWidget(m_browse, 3, 2);

    m_backward = new QCheckBox(tr("Backward direction"));
    m_wholeWord = new QCheckBox(tr("Match whole word only"));
    m_matchCase = new QCheckBox(tr("Match case"));
    m_wrap = new QCheckBox(tr("Wrap around"));
    m_inSelection = new QCheckBox(tr("In selection"));
    m_recursive = new QCheckBox(tr("In all sub-folders"));
    m_hidden = new QCheckBox(tr("In hidden folders"));
    m_followDoc = new QCheckBox(tr("Follow current doc."));
    m_bookmarkLine = new QCheckBox(tr("Bookmark line"));
    m_purge = new QCheckBox(tr("Purge for each search"));
    auto* checks = new QVBoxLayout;
    for (QCheckBox* c : {m_bookmarkLine, m_purge, m_backward, m_wholeWord, m_matchCase, m_wrap, m_inSelection, m_followDoc, m_recursive, m_hidden})
        checks->addWidget(c);
    checks->addStretch();

    auto* modeBox = new QGroupBox(tr("Search Mode"));
    m_normal = new QRadioButton(tr("Normal"));
    m_extended = new QRadioButton(tr("Extended (\\n, \\r, \\t, \\0, \\x...)"));
    m_regex = new QRadioButton(tr("Regular expression"));
    m_dotNewline = new QCheckBox(tr(". matches newline"));
    m_normal->setChecked(true);
    auto* modeLayout = new QGridLayout(modeBox);
    modeLayout->addWidget(m_normal, 0, 0);
    modeLayout->addWidget(m_extended, 1, 0);
    modeLayout->addWidget(m_regex, 2, 0);
    modeLayout->addWidget(m_dotNewline, 2, 1);
    connect(m_regex, &QRadioButton::toggled, this, [this](bool on) {
        m_dotNewline->setEnabled(on);
        updateForTab();
    });
    m_dotNewline->setEnabled(false);

    auto* buttons = new QVBoxLayout;
    const std::pair<const char*, Action> defs[] = {
        {"Find Next", Action::FindNext}, {"Count", Action::Count},
        {"Find All in Current Document", Action::FindAllCurrent}, {"Find All in All Opened Documents", Action::FindAllOpen},
        {"Replace", Action::Replace}, {"Replace All", Action::ReplaceAll}, {"Replace All in All Opened Documents", Action::ReplaceAllOpen},
        {"Find All", Action::FindInFiles}, {"Replace in Files", Action::ReplaceInFiles},
        {"Mark All", Action::MarkAll}, {"Clear all marks", Action::ClearMarks}, {"Copy Marked Text", Action::CopyMarked},
    };
    for (auto [text, a] : defs)
        buttons->addWidget(button(tr(text), a));
    auto* close = new QPushButton(tr("Close"));
    connect(close, &QPushButton::clicked, this, &QDialog::hide);
    buttons->addWidget(close);
    buttons->addStretch();

    m_status = new QLabel;
    m_status->setWordWrap(true);

    auto* left = new QVBoxLayout;
    left->addLayout(fields);
    auto* lower = new QHBoxLayout;
    lower->addLayout(checks);
    lower->addWidget(modeBox, 0, Qt::AlignBottom);
    left->addLayout(lower);
    auto* body = new QHBoxLayout;
    body->addLayout(left, 1);
    body->addLayout(buttons);
    auto* root = new QVBoxLayout(this);
    root->addWidget(m_tabs);
    root->addLayout(body);
    root->addWidget(m_status);

    connect(m_tabs, &QTabBar::currentChanged, this, &FindReplaceDialog::updateForTab);
    connect(m_find->lineEdit(), &QLineEdit::returnPressed, this, [this] {
        const Tab t = currentTab();
        emit actionRequested(t == FindInFilesTab ? Action::FindInFiles : t == MarkTab ? Action::MarkAll : Action::FindNext);
    });
    updateForTab();
}

QPushButton* FindReplaceDialog::button(const QString& text, Action a)
{
    auto* b = new QPushButton(text);
    b->setAutoDefault(false);
    connect(b, &QPushButton::clicked, this, [this, a] { emit actionRequested(a); });
    m_buttons[a] = b;
    return b;
}

void FindReplaceDialog::updateForTab()
{
    const Tab t = currentTab();
    const bool replace = t == ReplaceTab || t == FindInFilesTab;
    const bool files = t == FindInFilesTab;
    const bool mark = t == MarkTab;
    m_replaceLabel->setVisible(replace);
    m_replace->setVisible(replace);
    for (QWidget* w : std::initializer_list<QWidget*>{m_filtersLabel, m_filters, m_dirLabel, m_dir, m_browse, m_recursive, m_hidden, m_followDoc})
        w->setVisible(files);
    m_bookmarkLine->setVisible(mark);
    m_purge->setVisible(mark);
    m_backward->setVisible(!files && !mark);
    m_wrap->setVisible(!files);
    m_inSelection->setVisible(!files);
    auto show = [this](Action a, bool on) { m_buttons[a]->setVisible(on); };
    show(Action::FindNext, t == FindTab || t == ReplaceTab);
    show(Action::Count, t == FindTab);
    show(Action::FindAllCurrent, t == FindTab);
    show(Action::FindAllOpen, t == FindTab);
    show(Action::Replace, t == ReplaceTab);
    show(Action::ReplaceAll, t == ReplaceTab);
    show(Action::ReplaceAllOpen, t == ReplaceTab);
    show(Action::FindInFiles, files);
    show(Action::ReplaceInFiles, files);
    show(Action::MarkAll, mark);
    show(Action::ClearMarks, mark);
    show(Action::CopyMarked, mark);
    static const char* titles[] = {"Find", "Replace", "Find in Files", "Find in Projects", "Mark"};
    setWindowTitle(m_tabs->tabText(t).isEmpty() ? tr(titles[t]) : m_tabs->tabText(t));
}

void FindReplaceDialog::showTab(Tab t, const QString& findText)
{
    m_tabs->setCurrentIndex(t);
    if (!findText.isEmpty())
        m_find->setCurrentText(findText);
    show();
    raise();
    activateWindow();
    m_find->setFocus();
    m_find->lineEdit()->selectAll();
}

FindReplaceDialog::Tab FindReplaceDialog::currentTab() const { return static_cast<Tab>(m_tabs->currentIndex()); }

SearchOptions FindReplaceDialog::options() const
{
    SearchOptions o;
    o.findWhat = m_find->currentText().toStdString();
    o.replaceWith = m_replace->currentText().toStdString();
    o.type = m_regex->isChecked() ? SearchType::Regex : m_extended->isChecked() ? SearchType::Extended : SearchType::Normal;
    o.matchCase = m_matchCase->isChecked();
    o.wholeWord = m_wholeWord->isChecked() && o.type != SearchType::Regex;
    o.dotMatchesNewline = m_dotNewline->isChecked();
    o.wrap = m_wrap->isChecked();
    o.directionDown = !m_backward->isChecked();
    return o;
}

void FindReplaceDialog::setOptions(const SearchOptions& o)
{
    m_find->setCurrentText(QString::fromStdString(o.findWhat));
    m_replace->setCurrentText(QString::fromStdString(o.replaceWith));
    (o.type == SearchType::Regex ? m_regex : o.type == SearchType::Extended ? m_extended : m_normal)->setChecked(true);
    m_matchCase->setChecked(o.matchCase);
    m_wholeWord->setChecked(o.wholeWord);
    m_dotNewline->setChecked(o.dotMatchesNewline);
    m_wrap->setChecked(o.wrap);
    m_backward->setChecked(!o.directionDown);
}

bool FindReplaceDialog::inSelection() const { return m_inSelection->isChecked(); }
void FindReplaceDialog::setInSelection(bool on) { m_inSelection->setChecked(on); }
QString FindReplaceDialog::directory() const { return m_dir->currentText(); }
void FindReplaceDialog::setDirectory(const QString& dir) { m_dir->setCurrentText(QDir::toNativeSeparators(dir)); }
QString FindReplaceDialog::filters() const { return m_filters->currentText(); }
void FindReplaceDialog::setFilters(const QString& f) { m_filters->setCurrentText(f); }
bool FindReplaceDialog::recursive() const { return m_recursive->isChecked(); }
bool FindReplaceDialog::includeHidden() const { return m_hidden->isChecked(); }
bool FindReplaceDialog::bookmarkLine() const { return m_bookmarkLine->isChecked(); }
bool FindReplaceDialog::purgeBeforeMark() const { return m_purge->isChecked(); }
void FindReplaceDialog::setMarkOptions(bool bookmark, bool purge)
{
    m_bookmarkLine->setChecked(bookmark);
    m_purge->setChecked(purge);
}

void FindReplaceDialog::loadHistory(const FindHistory& h)
{
    auto fill = [](QComboBox* c, const std::vector<std::string>& v) {
        c->clear();
        for (const auto& s : v)
            c->addItem(QString::fromStdString(s));
    };
    fill(m_find, h.finds);
    fill(m_replace, h.replaces);
    fill(m_filters, h.filters);
    fill(m_dir, h.paths);
    m_wholeWord->setChecked(h.matchWord);
    m_matchCase->setChecked(h.matchCase);
    m_wrap->setChecked(h.wrap);
    m_backward->setChecked(!h.directionDown);
    m_recursive->setChecked(h.fifRecursive);
    m_hidden->setChecked(h.fifInHiddenFolder);
    m_followDoc->setChecked(h.fifFilterFollowsDoc);
    (h.searchMode == FindHistory::Regex ? m_regex : h.searchMode == FindHistory::Extended ? m_extended : m_normal)->setChecked(true);
    m_dotNewline->setChecked(h.dotMatchesNewline);
    m_bookmarkLine->setChecked(h.bookmarkLine);
    m_purge->setChecked(h.purge);
    if (m_filters->currentText().isEmpty())
        m_filters->setCurrentText(QStringLiteral("*.*"));
}

void FindReplaceDialog::saveHistory(FindHistory& h) const
{
    auto take = [](const QComboBox* c, std::vector<std::string>& v, int max) {
        v.clear();
        for (const QString& s : items(c))
            if (static_cast<int>(v.size()) < max)
                v.push_back(s.toStdString());
    };
    take(m_find, h.finds, h.maxFind);
    take(m_replace, h.replaces, h.maxReplace);
    take(m_filters, h.filters, h.maxFilter);
    take(m_dir, h.paths, h.maxPath);
    h.matchWord = m_wholeWord->isChecked();
    h.matchCase = m_matchCase->isChecked();
    h.wrap = m_wrap->isChecked();
    h.directionDown = !m_backward->isChecked();
    h.fifRecursive = m_recursive->isChecked();
    h.fifInHiddenFolder = m_hidden->isChecked();
    h.fifFilterFollowsDoc = m_followDoc->isChecked();
    h.searchMode = m_regex->isChecked() ? FindHistory::Regex : m_extended->isChecked() ? FindHistory::Extended : FindHistory::Normal;
    h.dotMatchesNewline = m_dotNewline->isChecked();
    h.bookmarkLine = m_bookmarkLine->isChecked();
    h.purge = m_purge->isChecked();
}

void FindReplaceDialog::addToHistory()
{
    const QString f = m_find->currentText(), r = m_replace->currentText(), fl = m_filters->currentText(), d = m_dir->currentText();
    pushFront(m_find, f);
    if (m_replace->isVisible())
        pushFront(m_replace, r);
    if (currentTab() == FindInFilesTab) {
        pushFront(m_filters, fl);
        pushFront(m_dir, d);
    }
}

void FindReplaceDialog::setStatus(const QString& text, Status s)
{
    m_status->setText(text);
    m_status->setStyleSheet(s == Status::Failed ? QStringLiteral("color: #c00000;")
                            : s == Status::Warning ? QStringLiteral("color: #b06000;") : QString());
}

QString FindReplaceDialog::status() const { return m_status->text(); }

void FindReplaceDialog::translate(const NativeLanguage* nl)
{
    Q_UNUSED(nl); // the <Dialog><Find> translation block is applied with the other dialogs (phase 4)
}

} // namespace npp::ui
