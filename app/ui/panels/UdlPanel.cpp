#include "UdlPanel.h"

#include "DialogTranslator.h"

#include "core/i18n/NativeLanguage.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFontComboBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QTabWidget>
#include <QVBoxLayout>

#include <SciLexer.h>

namespace npp::ui {

using namespace npp::core;

namespace {

QString qs(const std::string& s) { return QString::fromUtf8(s.data(), static_cast<qsizetype>(s.size())); }
std::string u8(const QString& s) { return s.toUtf8().toStdString(); }

// SCE_USER_KWLIST_*
enum { KW_COMMENTS = 0, KW_NUM_PREFIX1, KW_NUM_PREFIX2, KW_NUM_EXTRAS1, KW_NUM_EXTRAS2, KW_NUM_SUFFIX1, KW_NUM_SUFFIX2,
       KW_NUM_RANGE, KW_OPERATORS1, KW_OPERATORS2, KW_CODE1_OPEN, KW_CODE1_MIDDLE, KW_CODE1_CLOSE, KW_CODE2_OPEN,
       KW_CODE2_MIDDLE, KW_CODE2_CLOSE, KW_COMMENT_OPEN, KW_COMMENT_MIDDLE, KW_COMMENT_CLOSE, KW_KEYWORDS1,
       KW_DELIMITERS = 27 };
// SCE_USER_STYLE_*
enum { ST_DEFAULT = 0, ST_COMMENT, ST_COMMENTLINE, ST_NUMBER, ST_KEYWORD1, ST_OPERATOR = 12, ST_CODE1, ST_CODE2,
       ST_FOLDCOMMENT, ST_DELIMITER1 };

// upstream UserDefineResource.h control ids
constexpr int kKeywordBase[8] = {22100, 22200, 22300, 22400, 22450, 22500, 22550, 22600};
constexpr int kDelimiterBase[8] = {24200, 24300, 24400, 24450, 24500, 24550, 24600, 24650};
constexpr int kFolderBase[3] = {21200, 21300, 21400};
constexpr int IDC_LANGNAME_COMBO = 20006, IDC_EXT_EDIT = 20008, IDC_IGNORECASE = 20012, IDC_RENAME = 20002,
              IDC_CREATE = 20003, IDC_REMOVE = 20004, IDC_SAVEAS = 20005, IDC_IMPORT = 20015, IDC_EXPORT = 20016;
constexpr int IDC_FOLD_COMPACT = 21106, IDC_DEFAULT_STYLER = 21102;
constexpr int IDC_FOLDING_OF_COMMENTS = 23001, IDC_ALLOW_ANYWHERE = 23004;
constexpr int IDC_COMMENTLINE_OPEN = 23320, IDC_COMMENTLINE_CONTINUE = 23321, IDC_COMMENTLINE_CLOSE = 23322,
              IDC_COMMENT_OPEN = 23120, IDC_COMMENT_CLOSE = 23121;
constexpr int kNumberEdits[7] = {23231, 23233, 23235, 23237, 23239, 23241, 23243}; // prefix1 .. range
constexpr int IDC_DOT_RADIO = 23245, IDC_OPERATOR1 = 24114, IDC_OPERATOR2 = 24115;

// Styler dialog nesting check boxes (upstream nestingMapper)
const struct {
    int id;
    int mask;
    const char* text;
} kNesting[] = {
    {25008, SCE_USER_MASK_NESTING_DELIMITER1, "Delimiter 1"}, {25009, SCE_USER_MASK_NESTING_DELIMITER2, "Delimiter 2"},
    {25010, SCE_USER_MASK_NESTING_DELIMITER3, "Delimiter 3"}, {25011, SCE_USER_MASK_NESTING_DELIMITER4, "Delimiter 4"},
    {25012, SCE_USER_MASK_NESTING_DELIMITER5, "Delimiter 5"}, {25013, SCE_USER_MASK_NESTING_DELIMITER6, "Delimiter 6"},
    {25014, SCE_USER_MASK_NESTING_DELIMITER7, "Delimiter 7"}, {25015, SCE_USER_MASK_NESTING_DELIMITER8, "Delimiter 8"},
    {25016, SCE_USER_MASK_NESTING_COMMENT, "Comment"}, {25017, SCE_USER_MASK_NESTING_COMMENT_LINE, "Comment line"},
    {25018, SCE_USER_MASK_NESTING_KEYWORD1, "Keyword 1"}, {25019, SCE_USER_MASK_NESTING_KEYWORD2, "Keyword 2"},
    {25020, SCE_USER_MASK_NESTING_KEYWORD3, "Keyword 3"}, {25021, SCE_USER_MASK_NESTING_KEYWORD4, "Keyword 4"},
    {25022, SCE_USER_MASK_NESTING_KEYWORD5, "Keyword 5"}, {25023, SCE_USER_MASK_NESTING_KEYWORD6, "Keyword 6"},
    {25024, SCE_USER_MASK_NESTING_KEYWORD7, "Keyword 7"}, {25025, SCE_USER_MASK_NESTING_KEYWORD8, "Keyword 8"},
    {25026, SCE_USER_MASK_NESTING_OPERATORS1, "Operator 1"}, {25027, SCE_USER_MASK_NESTING_OPERATORS2, "Operator 2"},
    {25028, SCE_USER_MASK_NESTING_NUMBERS, "Numbers"},
};

QString text(QWidget* w)
{
    if (auto* e = qobject_cast<QPlainTextEdit*>(w))
        return e->toPlainText();
    if (auto* l = qobject_cast<QLineEdit*>(w))
        return l->text();
    return {};
}

void setText(QWidget* w, const QString& t)
{
    if (auto* e = qobject_cast<QPlainTextEdit*>(w))
        e->setPlainText(t);
    else if (auto* l = qobject_cast<QLineEdit*>(w))
        l->setText(t);
}

// keyword lists are stored with spaces; the edit boxes may contain line breaks
std::string flat(const QString& s)
{
    QString t = s;
    t.replace(QLatin1Char('\r'), QLatin1Char(' ')).replace(QLatin1Char('\n'), QLatin1Char(' '));
    return u8(t.simplified());
}

QLabel* label(const QString& t, int id)
{
    auto* l = new QLabel(t);
    setControlId(l, id);
    return l;
}

} // namespace

UdlPanel::UdlPanel(UserLanguageRegistry& registry, QWidget* parent) : QWidget(parent), m_reg(registry)
{
    setObjectName(QStringLiteral("UserDefine"));
    auto* root = new QVBoxLayout(this);

    auto* top = new QGridLayout;
    m_combo = new QComboBox;
    setControlId(m_combo, IDC_LANGNAME_COMBO);
    m_combo->setMinimumWidth(180);
    auto* create = new QPushButton(tr("Create new..."));
    auto* saveAs = new QPushButton(tr("Save As..."));
    auto* import = new QPushButton(tr("Import..."));
    auto* exportB = new QPushButton(tr("Export..."));
    auto* rename = new QPushButton(tr("Rename"));
    auto* remove = new QPushButton(tr("Remove"));
    for (auto [b, id] : {std::pair{create, IDC_CREATE}, {saveAs, IDC_SAVEAS}, {import, IDC_IMPORT}, {exportB, IDC_EXPORT},
                         {rename, IDC_RENAME}, {remove, IDC_REMOVE}})
        setControlId(b, id);
    m_ext = new QLineEdit;
    setControlId(m_ext, IDC_EXT_EDIT);
    m_ignoreCase = new QCheckBox(tr("Ignore case"));
    setControlId(m_ignoreCase, IDC_IGNORECASE);
    top->addWidget(label(tr("User language:"), 20007), 0, 0);
    top->addWidget(m_combo, 0, 1);
    top->addWidget(rename, 0, 2);
    top->addWidget(create, 0, 3);
    top->addWidget(remove, 0, 4);
    top->addWidget(saveAs, 0, 5);
    top->addWidget(import, 1, 2);
    top->addWidget(exportB, 1, 3);
    top->addWidget(label(tr("Ext.:"), 20009), 1, 0);
    top->addWidget(m_ext, 1, 1);
    top->addWidget(m_ignoreCase, 1, 4, 1, 2);
    root->addLayout(top);

    m_tabs = new QTabWidget;
    m_tabs->addTab(buildFolderPage(), tr("Folder && Default"));
    m_tabs->addTab(buildKeywordsPage(), tr("Keywords Lists"));
    m_tabs->addTab(buildCommentPage(), tr("Comment && Number"));
    m_tabs->addTab(buildOperatorsPage(), tr("Operators && Delimiters"));
    root->addWidget(m_tabs, 1);

    connect(m_combo, &QComboBox::currentIndexChanged, this, [this] { load(); });
    connect(create, &QPushButton::clicked, this, [this] {
        bool ok = false;
        const QString n = QInputDialog::getText(this, tr("Create New Language..."), tr("Name:"), QLineEdit::Normal, {}, &ok).trimmed();
        if (ok && !n.isEmpty() && !createLanguage(n, false))
            QMessageBox::warning(this, tr("UDL Error"), tr("This name is used by another language,\nplease give another one."));
    });
    connect(saveAs, &QPushButton::clicked, this, [this] {
        if (!current())
            return;
        bool ok = false;
        const QString n = QInputDialog::getText(this, tr("Save Current Language Name As..."), tr("Name:"), QLineEdit::Normal, {}, &ok).trimmed();
        if (ok && !n.isEmpty() && !createLanguage(n, true))
            QMessageBox::warning(this, tr("UDL Error"), tr("This name is used by another language,\nplease give another one."));
    });
    connect(rename, &QPushButton::clicked, this, [this] {
        if (!current())
            return;
        bool ok = false;
        const QString n = QInputDialog::getText(this, tr("Rename Current Language Name"), tr("Name:"), QLineEdit::Normal,
                                                currentName(), &ok).trimmed();
        if (ok && !n.isEmpty() && n != currentName() && !renameCurrent(n))
            QMessageBox::warning(this, tr("UDL Error"), tr("This name is used by another language,\nplease give another one."));
    });
    connect(remove, &QPushButton::clicked, this, [this] {
        if (current() && QMessageBox::question(this, tr("Remove the current language"), tr("Are you sure?")) == QMessageBox::Yes)
            removeCurrent();
    });
    connect(import, &QPushButton::clicked, this, [this] {
        const QString f = QFileDialog::getOpenFileName(this, tr("Import"), {}, tr("UDL (*.xml);;All types (*.*)"));
        if (!f.isEmpty() && !importFile(f))
            QMessageBox::warning(this, tr("Import"), tr("Failed to import."));
    });
    connect(exportB, &QPushButton::clicked, this, [this] {
        if (!current())
            return;
        const QString f = QFileDialog::getSaveFileName(this, tr("Export"), currentName() + QStringLiteral(".xml"), tr("UDL (*.xml)"));
        if (!f.isEmpty() && !exportCurrent(f))
            QMessageBox::warning(this, tr("Export"), tr("Failed to export."));
    });
    connect(m_ext, &QLineEdit::textEdited, this, [this] { commit(); });
    connect(m_ignoreCase, &QCheckBox::toggled, this, [this] { commit(); });
    reloadLanguages();
}

void UdlPanel::setNativeLanguage(const NativeLanguage* nl)
{
    m_nl = nl;
    if (!nl)
        return;
    translateDialog(this, nl, "UserDefine");
    const char* pages[] = {"Folder", "Keywords", "Comment", "Operator"};
    for (int i = 0; i < 4; ++i) {
        translateDialog(m_tabs->widget(i), nl, pages[i], "");
        const std::string t = nl->dialogAttribute(pages[i], "title");
        if (!t.empty())
            m_tabs->setTabText(i, controlText(t, true));
    }
}

QWidget* UdlPanel::control(int id) const
{
    for (QWidget* w : m_controls)
        if (w->objectName() == QString::number(id))
            return w;
    return findChild<QWidget*>(QString::number(id));
}

QPlainTextEdit* UdlPanel::edit(int id, bool multiline)
{
    auto* e = new QPlainTextEdit;
    setControlId(e, id);
    e->setTabChangesFocus(true);
    if (!multiline)
        e->setFixedHeight(e->fontMetrics().height() * 2 + 10);
    connect(e, &QPlainTextEdit::textChanged, this, [this] { commit(); });
    m_controls.push_back(e);
    return e;
}

QPushButton* UdlPanel::styler(int id, int styleId)
{
    auto* b = new QPushButton(tr("Styler"));
    setControlId(b, id);
    connect(b, &QPushButton::clicked, this, [this, styleId] { editStyle(styleId); });
    return b;
}

QWidget* UdlPanel::buildFolderPage()
{
    auto* page = new QWidget;
    auto* v = new QVBoxLayout(page);
    auto* def = new QHBoxLayout;
    def->addWidget(label(tr("Default style"), 21101));
    def->addWidget(styler(IDC_DEFAULT_STYLER, ST_DEFAULT));
    m_foldCompact = new QCheckBox(tr("Fold compact (fold empty lines too)"));
    setControlId(m_foldCompact, IDC_FOLD_COMPACT);
    connect(m_foldCompact, &QCheckBox::toggled, this, [this] { commit(); });
    def->addWidget(m_foldCompact);
    def->addStretch(1);
    v->addLayout(def);
    const char* titles[3] = {"Folding in code 1 style", "Folding in code 2 style (separators needed)", "Folding in comment style"};
    const int styles[3] = {ST_CODE1, ST_CODE2, ST_FOLDCOMMENT};
    auto* row = new QHBoxLayout;
    for (int g = 0; g < 3; ++g) {
        const int base = kFolderBase[g];
        auto* box = new QGroupBox(tr(titles[g]));
        setControlId(box, base + 20);
        auto* f = new QFormLayout(box);
        f->addRow(label(tr("Open:"), base + 24), edit(base + 21, true));
        f->addRow(label(tr("Middle:"), base + 25), edit(base + 22, true));
        f->addRow(label(tr("Close:"), base + 26), edit(base + 23, true));
        f->addRow(styler(base + 27, styles[g]));
        row->addWidget(box);
    }
    v->addLayout(row, 1);
    return page;
}

QWidget* UdlPanel::buildKeywordsPage()
{
    auto* page = new QWidget;
    auto* grid = new QGridLayout(page);
    const char* names[8] = {"1st Group", "2nd Group", "3rd Group", "4th Group", "5th Group", "6th Group", "7th Group", "8th Group"};
    for (int k = 0; k < 8; ++k) {
        const int base = kKeywordBase[k];
        auto* box = new QGroupBox(tr(names[k]));
        setControlId(box, base + 1);
        auto* v = new QVBoxLayout(box);
        auto* bar = new QHBoxLayout;
        bar->addWidget(styler(base + 22, ST_KEYWORD1 + k));
        m_prefix[k] = new QCheckBox(tr("Prefix mode"));
        setControlId(m_prefix[k], base + 21);
        connect(m_prefix[k], &QCheckBox::toggled, this, [this] { commit(); });
        bar->addWidget(m_prefix[k]);
        bar->addStretch(1);
        v->addLayout(bar);
        v->addWidget(edit(base + 20, true), 1);
        grid->addWidget(box, k / 4, k % 4);
    }
    return page;
}

QWidget* UdlPanel::buildCommentPage()
{
    auto* page = new QWidget;
    auto* v = new QVBoxLayout(page);
    auto* posBox = new QGroupBox(tr("Line comment position"));
    setControlId(posBox, 23003);
    auto* pos = new QHBoxLayout(posBox);
    auto* posGroup = new QButtonGroup(page);
    const char* posNames[3] = {"Allow anywhere", "Force at beginning of line", "Allow preceding whitespace"};
    for (int i = 0; i < 3; ++i) {
        m_lineCommentPos[i] = new QRadioButton(tr(posNames[i]));
        setControlId(m_lineCommentPos[i], IDC_ALLOW_ANYWHERE + i);
        posGroup->addButton(m_lineCommentPos[i], i);
        pos->addWidget(m_lineCommentPos[i]);
        connect(m_lineCommentPos[i], &QRadioButton::toggled, this, [this](bool on) {
            if (on)
                commit();
        });
    }
    m_foldComments = new QCheckBox(tr("Allow folding of comments"));
    setControlId(m_foldComments, IDC_FOLDING_OF_COMMENTS);
    connect(m_foldComments, &QCheckBox::toggled, this, [this] { commit(); });
    pos->addWidget(m_foldComments);
    v->addWidget(posBox);

    auto* row = new QHBoxLayout;
    auto* lineBox = new QGroupBox(tr("Comment line style"));
    setControlId(lineBox, 23301);
    auto* lf = new QFormLayout(lineBox);
    lf->addRow(label(tr("Open"), 23323), edit(IDC_COMMENTLINE_OPEN));
    lf->addRow(label(tr("Continue character"), 23324), edit(IDC_COMMENTLINE_CONTINUE));
    lf->addRow(label(tr("Close"), 23325), edit(IDC_COMMENTLINE_CLOSE));
    lf->addRow(styler(23326, ST_COMMENTLINE));
    row->addWidget(lineBox);
    auto* comBox = new QGroupBox(tr("Comment style"));
    setControlId(comBox, 23101);
    auto* cf = new QFormLayout(comBox);
    cf->addRow(label(tr("Open"), 23122), edit(IDC_COMMENT_OPEN));
    cf->addRow(label(tr("Close"), 23123), edit(IDC_COMMENT_CLOSE));
    cf->addRow(styler(23124, ST_COMMENT));
    row->addWidget(comBox);
    v->addLayout(row);

    auto* numBox = new QGroupBox(tr("Number style"));
    setControlId(numBox, 23201);
    auto* nf = new QGridLayout(numBox);
    const char* numNames[7] = {"Prefix 1", "Prefix 2", "Extras 1", "Extras 2", "Suffix 1", "Suffix 2", "Range:"};
    for (int i = 0; i < 7; ++i) {
        nf->addWidget(label(tr(numNames[i]), kNumberEdits[i] - 1), i / 2, (i % 2) * 2);
        nf->addWidget(edit(kNumberEdits[i]), i / 2, (i % 2) * 2 + 1);
    }
    auto* decBox = new QGroupBox(tr("Decimal separator"));
    setControlId(decBox, 23244);
    auto* dec = new QHBoxLayout(decBox);
    auto* decGroup = new QButtonGroup(page);
    const char* decNames[3] = {"Dot", "Comma", "Both"};
    for (int i = 0; i < 3; ++i) {
        m_decimal[i] = new QRadioButton(tr(decNames[i]));
        setControlId(m_decimal[i], IDC_DOT_RADIO + i);
        decGroup->addButton(m_decimal[i], i);
        dec->addWidget(m_decimal[i]);
        connect(m_decimal[i], &QRadioButton::toggled, this, [this](bool on) {
            if (on)
                commit();
        });
    }
    nf->addWidget(decBox, 3, 2, 1, 2);
    nf->addWidget(styler(23220, ST_NUMBER), 4, 0);
    v->addWidget(numBox);
    v->addStretch(1);
    return page;
}

QWidget* UdlPanel::buildOperatorsPage()
{
    auto* inner = new QWidget;
    auto* v = new QVBoxLayout(inner);
    auto* opBox = new QGroupBox(tr("Operators style"));
    setControlId(opBox, 24101);
    auto* of = new QFormLayout(opBox);
    of->addRow(label(tr("Operators 1"), 24116), edit(IDC_OPERATOR1));
    of->addRow(label(tr("Operators 2 (separators required)"), 24117), edit(IDC_OPERATOR2));
    of->addRow(styler(24113, ST_OPERATOR));
    v->addWidget(opBox);
    auto* grid = new QGridLayout;
    for (int d = 0; d < 8; ++d) {
        const int base = kDelimiterBase[d];
        auto* box = new QGroupBox(tr("Delimiter %1 style").arg(d + 1));
        setControlId(box, base + 1);
        auto* f = new QFormLayout(box);
        f->addRow(label(tr("Open:"), base + 20), edit(base + 17));
        f->addRow(label(tr("Escape:"), base + 21), edit(base + 18));
        f->addRow(label(tr("Close:"), base + 22), edit(base + 19));
        f->addRow(styler(base + 23, ST_DELIMITER1 + d));
        grid->addWidget(box, d / 2, d % 2);
    }
    v->addLayout(grid);
    auto* scroll = new QScrollArea;
    scroll->setWidget(inner);
    scroll->setWidgetResizable(true);
    return scroll;
}

void UdlPanel::reloadLanguages(const QString& select)
{
    const QString keep = select.isEmpty() ? m_combo->currentText() : select;
    {
        const QSignalBlocker b(m_combo);
        m_combo->clear();
        for (const UserLanguage& u : m_reg.languages())
            m_combo->addItem(qs(u.name));
        const int i = m_combo->findText(keep);
        m_combo->setCurrentIndex(i >= 0 ? i : (m_combo->count() ? 0 : -1));
    }
    load();
}

QString UdlPanel::currentName() const { return m_combo->currentText(); }

void UdlPanel::selectLanguage(const QString& name)
{
    const int i = m_combo->findText(name);
    if (i >= 0)
        m_combo->setCurrentIndex(i);
}

UserLanguage* UdlPanel::current() { return m_combo->currentIndex() >= 0 ? m_reg.findMutable(u8(m_combo->currentText())) : nullptr; }

void UdlPanel::load()
{
    m_loading = true;
    UserLanguage* u = current();
    m_tabs->setEnabled(u);
    m_ext->setEnabled(u);
    m_ignoreCase->setEnabled(u);
    const UserLanguage empty;
    const UserLanguage& l = u ? *u : empty;
    m_ext->setText(qs(l.ext));
    m_ignoreCase->setChecked(l.caseIgnored);
    m_foldCompact->setChecked(l.foldCompact);
    m_foldComments->setChecked(l.allowFoldOfComments);
    m_lineCommentPos[std::clamp(l.forcePureLC, 0, 2)]->setChecked(true);
    m_decimal[std::clamp(l.decimalSeparator, 0, 2)]->setChecked(true);
    for (int g = 0; g < 3; ++g)
        for (int k = 0; k < 3; ++k)
            setText(control(kFolderBase[g] + 21 + k), qs(l.keywordLists[static_cast<size_t>(KW_CODE1_OPEN + g * 3 + k)]));
    for (int k = 0; k < 8; ++k) {
        setText(control(kKeywordBase[k] + 20), qs(l.keywordLists[static_cast<size_t>(KW_KEYWORDS1 + k)]));
        m_prefix[k]->setChecked(l.isPrefix[static_cast<size_t>(k)]);
    }
    const std::string& comments = l.keywordLists[KW_COMMENTS];
    const int commentIds[kUdlCommentFields] = {IDC_COMMENTLINE_OPEN, IDC_COMMENTLINE_CONTINUE, IDC_COMMENTLINE_CLOSE,
                                               IDC_COMMENT_OPEN, IDC_COMMENT_CLOSE};
    for (int i = 0; i < kUdlCommentFields; ++i)
        setText(control(commentIds[i]), qs(udlFieldOf(comments, i)));
    for (int i = 0; i < 7; ++i)
        setText(control(kNumberEdits[i]), qs(l.keywordLists[static_cast<size_t>(KW_NUM_PREFIX1 + i)]));
    setText(control(IDC_OPERATOR1), qs(l.keywordLists[KW_OPERATORS1]));
    setText(control(IDC_OPERATOR2), qs(l.keywordLists[KW_OPERATORS2]));
    for (int d = 0; d < 8; ++d)
        for (int k = 0; k < 3; ++k)
            setText(control(kDelimiterBase[d] + 17 + k), qs(udlFieldOf(l.keywordLists[KW_DELIMITERS], d * 3 + k)));
    m_loading = false;
}

void UdlPanel::store()
{
    UserLanguage* u = current();
    if (!u)
        return;
    u->ext = u8(m_ext->text().trimmed());
    u->caseIgnored = m_ignoreCase->isChecked();
    u->foldCompact = m_foldCompact->isChecked();
    u->allowFoldOfComments = m_foldComments->isChecked();
    for (int i = 0; i < 3; ++i) {
        if (m_lineCommentPos[i]->isChecked())
            u->forcePureLC = i;
        if (m_decimal[i]->isChecked())
            u->decimalSeparator = i;
    }
    for (int g = 0; g < 3; ++g)
        for (int k = 0; k < 3; ++k)
            u->keywordLists[static_cast<size_t>(KW_CODE1_OPEN + g * 3 + k)] = flat(text(control(kFolderBase[g] + 21 + k)));
    for (int k = 0; k < 8; ++k) {
        u->keywordLists[static_cast<size_t>(KW_KEYWORDS1 + k)] = u8(text(control(kKeywordBase[k] + 20)));
        u->isPrefix[static_cast<size_t>(k)] = m_prefix[k]->isChecked();
    }
    const int commentIds[kUdlCommentFields] = {IDC_COMMENTLINE_OPEN, IDC_COMMENTLINE_CONTINUE, IDC_COMMENTLINE_CLOSE,
                                               IDC_COMMENT_OPEN, IDC_COMMENT_CLOSE};
    std::vector<std::string> c;
    for (int id : commentIds)
        c.push_back(flat(text(control(id))));
    u->keywordLists[KW_COMMENTS] = udlEncodeFields(c);
    for (int i = 0; i < 7; ++i)
        u->keywordLists[static_cast<size_t>(KW_NUM_PREFIX1 + i)] = flat(text(control(kNumberEdits[i])));
    u->keywordLists[KW_OPERATORS1] = flat(text(control(IDC_OPERATOR1)));
    u->keywordLists[KW_OPERATORS2] = flat(text(control(IDC_OPERATOR2)));
    std::vector<std::string> d;
    for (int i = 0; i < 8; ++i)
        for (int k = 0; k < 3; ++k)
            d.push_back(flat(text(control(kDelimiterBase[i] + 17 + k))));
    u->keywordLists[KW_DELIMITERS] = udlEncodeFields(d);
}

void UdlPanel::commit()
{
    if (m_loading || !current())
        return;
    store();
    emit languageChanged(currentName());
    emit saveRequested();
}

bool UdlPanel::createLanguage(const QString& name, bool copyCurrent)
{
    UserLanguage u;
    if (copyCurrent && current())
        u = *current();
    u.name = u8(name);
    u.sourceFile.clear(); // new: userDefineLang.xml
    if (!copyCurrent) {
        u.udlVersion = "2.1";
        // default style of upstream UserLangContainer: plain black on white
        for (int i = 0; i < kUdlStyleCount; ++i) {
            Style s;
            s.styleId = i;
            s.fg = 0x000000;
            s.bg = 0xFFFFFF;
            s.colorStyle = 1;
            s.fontStyle = 0;
            u.styles.push_back(s);
        }
    }
    if (!m_reg.add(std::move(u)))
        return false;
    emit languagesChanged();
    reloadLanguages(name);
    emit saveRequested();
    return true;
}

bool UdlPanel::renameCurrent(const QString& newName)
{
    const QString old = currentName();
    if (!m_reg.rename(u8(old), u8(newName)))
        return false;
    emit languagesChanged();
    reloadLanguages(newName);
    emit saveRequested();
    return true;
}

bool UdlPanel::removeCurrent()
{
    if (!m_reg.remove(u8(currentName())))
        return false;
    emit languagesChanged();
    reloadLanguages();
    emit saveRequested();
    return true;
}

bool UdlPanel::importFile(const QString& path)
{
    UserLanguageRegistry tmp;
    if (tmp.loadFile(u8(path)) <= 0)
        return false;
    QString last;
    for (UserLanguage u : tmp.languages()) {
        u.sourceFile.clear();
        std::string name = u.name;
        for (int n = 2; m_reg.find(name); ++n) // upstream keeps both: the imported one gets a new name
            name = u.name + " (" + std::to_string(n) + ")";
        u.name = name;
        last = qs(name);
        m_reg.add(std::move(u));
    }
    emit languagesChanged();
    reloadLanguages(last);
    emit saveRequested();
    return true;
}

bool UdlPanel::exportCurrent(const QString& path)
{
    const UserLanguage* u = current();
    return u && saveUserLanguages({*u}, u8(path));
}

void UdlPanel::editStyle(int styleId)
{
    UserLanguage* u = current();
    if (!u)
        return;
    Style st;
    st.styleId = styleId;
    if (const Style* s = u->style(styleId))
        st = *s;

    QDialog dlg(this);
    dlg.setObjectName(QStringLiteral("StylerDialog"));
    dlg.setWindowTitle(tr("Styler Dialog"));
    auto* fontBox = new QGroupBox(tr("Font options"));
    setControlId(fontBox, 25030);
    auto* font = new QFontComboBox;
    font->setEditable(true);
    font->setCurrentText(qs(st.fontName));
    auto* size = new QSpinBox;
    size->setRange(0, 72);
    size->setSpecialValueText(QStringLiteral(" "));
    size->setValue(std::max(0, st.fontSize));
    auto* bold = new QCheckBox(tr("Bold"));
    auto* italic = new QCheckBox(tr("Italic"));
    auto* underline = new QCheckBox(tr("Underline"));
    setControlId(bold, 25001);
    setControlId(italic, 25002);
    setControlId(underline, 25003);
    const int fs = std::max(0, st.fontStyle);
    bold->setChecked(fs & 1);
    italic->setChecked(fs & 2);
    underline->setChecked(fs & 4);
    auto* ff = new QFormLayout(fontBox);
    ff->addRow(label(tr("Name:"), 25031), font);
    ff->addRow(label(tr("Size:"), 25032), size);
    ff->addRow(bold);
    ff->addRow(italic);
    ff->addRow(underline);
    // colours (-1 = transparent / not set)
    int fg = st.fg, bg = st.bg;
    auto colourButton = [&dlg](int& c, int id, int transparentId, const QString& title) {
        auto* row = new QWidget;
        auto* h = new QHBoxLayout(row);
        h->setContentsMargins(0, 0, 0, 0);
        auto* l = label(title, id);
        auto* b = new QPushButton;
        b->setFixedWidth(60);
        auto* t = new QCheckBox(QObject::tr("Transparent"));
        setControlId(t, transparentId);
        auto paint = [b, &c] { b->setStyleSheet(c < 0 ? QString() : QStringLiteral("background:#%1").arg(c, 6, 16, QLatin1Char('0'))); };
        paint();
        t->setChecked(c < 0);
        QObject::connect(b, &QPushButton::clicked, &dlg, [&dlg, &c, t, paint] {
            const QColor q = QColorDialog::getColor(c < 0 ? QColor(Qt::black) : QColor(QRgb(c)), &dlg);
            if (q.isValid()) {
                c = static_cast<int>(q.rgb() & 0xFFFFFF);
                t->setChecked(false);
                paint();
            }
        });
        QObject::connect(t, &QCheckBox::toggled, &dlg, [&c, paint](bool on) {
            if (on)
                c = -1;
            paint();
        });
        h->addWidget(l);
        h->addWidget(b);
        h->addWidget(t);
        return row;
    };
    auto* colours = new QVBoxLayout;
    colours->addWidget(colourButton(fg, 25006, 25033, tr("Foreground color")));
    colours->addWidget(colourButton(bg, 25007, 25034, tr("Background color")));
    auto* nestBox = new QGroupBox(tr("Nesting"));
    setControlId(nestBox, 25029);
    auto* ng = new QGridLayout(nestBox);
    std::vector<std::pair<QCheckBox*, int>> nest;
    int i = 0;
    for (const auto& n : kNesting) {
        auto* c = new QCheckBox(tr(n.text));
        setControlId(c, n.id);
        c->setChecked(st.nesting & n.mask);
        ng->addWidget(c, i % 7, i / 7);
        nest.emplace_back(c, n.mask);
        ++i;
    }
    // upstream: nesting only for delimiters and comments
    nestBox->setEnabled(styleId >= ST_DELIMITER1 || styleId == ST_COMMENT || styleId == ST_COMMENTLINE);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    auto* layout = new QGridLayout(&dlg);
    layout->addWidget(fontBox, 0, 0);
    layout->addLayout(colours, 1, 0);
    layout->addWidget(nestBox, 0, 1, 2, 1);
    layout->addWidget(buttons, 2, 0, 1, 2);
    if (m_nl)
        translateDialog(&dlg, m_nl, "StylerDialog");
    const bool accepted = execStyler ? execStyler(&dlg) : dlg.exec() == QDialog::Accepted;
    if (!accepted)
        return;
    st.fontName = u8(font->currentText().trimmed());
    st.fontSize = size->value() > 0 ? size->value() : -1;
    st.fontStyle = (bold->isChecked() ? 1 : 0) | (italic->isChecked() ? 2 : 0) | (underline->isChecked() ? 4 : 0);
    st.fg = fg;
    st.bg = bg;
    st.nesting = 0;
    for (const auto& [c, mask] : nest)
        if (c->isChecked())
            st.nesting |= mask;
    bool found = false;
    for (Style& s : u->styles)
        if (s.styleId == styleId) {
            const std::string name = s.name;
            s = st;
            s.name = name;
            found = true;
        }
    if (!found)
        u->styles.push_back(st);
    emit languageChanged(currentName());
    emit saveRequested();
}

} // namespace npp::ui
