#include "PreferencesDialog.h"

#include "core/config/ConfigFile.h"
#include "core/i18n/NativeLanguage.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFontMetrics>
#include <QFrame>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QRadioButton>
#include <QScrollArea>
#include <QSlider>
#include <QSpinBox>
#include <QStackedWidget>
#include <QVBoxLayout>

namespace npp::ui {

namespace {

enum Kind { Group, Label, Check, Radio, Combo, ComboEdit, Edit, Button, List, Slider, Spin };
enum Flags { kPrefDisabled = 1, kPrefRight = 2, kPrefCenter = 4, kPrefNumber = 8, kPrefMultiline = 16, kPrefNewGroup = 32, kPrefLine = 64 };

struct CtrlDef {
    Kind kind;
    int id;
    const char* text;
    int x, y, w, h, flags;
};
struct PageDef {
    const char* tag;
    const char* title;
    int w, h;
    std::vector<CtrlDef> ctrls;
};

const std::vector<PageDef>& pageDefs()
{
    static const std::vector<PageDef> defs = [] {
        std::vector<PageDef> v;
#define NPP_PREF_PAGE(tag, title, w, h) v.push_back({#tag, title, w, h, {}});
#define NPP_PREF_CTRL(kind, id, text, x, y, w, h, flags) v.back().ctrls.push_back({kind, id, text, x, y, w, h, flags});
#define NPP_PREF_PAGE_END()
#include "prefs/PreferencePages.inc"
#undef NPP_PREF_PAGE
#undef NPP_PREF_CTRL
#undef NPP_PREF_PAGE_END
        return v;
    }();
    return defs;
}

// Win32 "&" mnemonics -> Qt ones ("&&" stays a literal '&'); "\r" in rc strings is a line break
QString rcText(const QString& s)
{
    QString t = s;
    t.replace(QLatin1String("\\r"), QLatin1String("\n"));
    return t;
}

// QLabel without a buddy shows '&' literally: drop the Win32 mnemonic marks ("&&" -> "&")
QString labelText(QString s)
{
    s.replace(QLatin1String("&&"), QStringLiteral(""));
    s.remove(QLatin1Char('&'));
    s.replace(QLatin1Char(''), QLatin1Char('&'));
    return s;
}

// Positions that differ from the generated upstream rc (dialog units): translated texts need more
// room with Qt's fonts, and a check box cannot sit on a group box frame without the line crossing it
struct CtrlMove {
    const char* tag;
    int id, x, y, w, h;
};
constexpr CtrlMove kCtrlMoves[] = {
    {"Scintillas", 6219, 14, 114, 49, 8},   {"Scintillas", 6221, 64, 114, 16, 8},   {"Scintillas", 6222, 140, 114, 16, 8},
    {"Print", 6704, 164, 67, 126, 14},      {"Print", 6705, 294, 67, 31, 14},       {"Print", 6706, 330, 69, 47, 10},
    {"Print", 6707, 378, 69, 50, 10},       {"Print", 6715, 164, 127, 126, 14},     {"Print", 6716, 294, 127, 31, 14},
    {"Print", 6717, 330, 129, 47, 10},      {"Print", 6718, 378, 129, 50, 10},      {"Indentation", 6510, 78, 95, 122, 10},
    {"Backup", 6804, 95, 159, 200, 10},     {"Delimiter", 6257, 172, 128, 76, 8},   {"Delimiter", 6258, 132, 148, 150, 8},
    {"Delimiter", 6254, 251, 126, 14, 12},  {"Delimiter", 6255, 266, 128, 47, 8},
};

CtrlDef moved(std::string_view tag, CtrlDef c)
{
    for (const CtrlMove& m : kCtrlMoves)
        if (m.id == c.id && tag == m.tag) {
            c.x = m.x;
            c.y = m.y;
            c.w = m.w;
            c.h = m.h;
        }
    return c;
}

// Check boxes and radio buttons do not wrap their text: break it at spaces to fit the width
QString wrappedText(const QString& text, const QFontMetrics& fm, int width)
{
    QStringList out;
    for (const QString& line : text.split(QLatin1Char('\n'))) {
        QString current;
        for (const QString& word : line.split(QLatin1Char(' '), Qt::SkipEmptyParts)) {
            const QString candidate = current.isEmpty() ? word : current + QLatin1Char(' ') + word;
            if (!current.isEmpty() && fm.horizontalAdvance(candidate) > width) {
                out << current;
                current = word;
            } else {
                current = candidate;
            }
        }
        out << current;
    }
    return out.join(QLatin1Char('\n'));
}

int widestLine(const QString& text, const QFontMetrics& fm)
{
    int widest = 0;
    for (const QString& line : text.split(QLatin1Char('\n')))
        widest = std::max(widest, fm.horizontalAdvance(line));
    return widest;
}

const char kRcWidth[] = "prefRcWidth";
const char kRcHeight[] = "prefRcHeight";
const char kEnglishItems[] = "prefEnglishItems";

} // namespace

PreferencesDialog::PreferencesDialog(const Context& ctx, QWidget* parent) : QDialog(parent), m_ctx(ctx)
{
    setObjectName(QStringLiteral("PreferenceDialog"));
    setWindowTitle(tr("Preferences"));
    m_list = new QListWidget;
    m_list->setMaximumWidth(180);
    m_stack = new QStackedWidget;
    auto* close = new QDialogButtonBox(QDialogButtonBox::Close);
    close->button(QDialogButtonBox::Close)->setObjectName(QStringLiteral("6001")); // IDC_BUTTON_CLOSE
    connect(close, &QDialogButtonBox::rejected, this, &QDialog::close);
    auto* body = new QHBoxLayout;
    body->addWidget(m_list);
    body->addWidget(m_stack, 1);
    auto* root = new QVBoxLayout(this);
    root->addLayout(body, 1);
    root->addWidget(close);
    connect(m_list, &QListWidget::currentRowChanged, m_stack, &QStackedWidget::setCurrentIndex);
    buildPages();
    setupBindings();
    reload();
    translate();
    m_list->setCurrentRow(0);
    int pageW = 0, pageH = 0;
    for (const Page& p : m_pages)
        if (auto* sa = qobject_cast<QScrollArea*>(p.widget)) {
            pageW = std::max(pageW, sa->widget()->minimumWidth());
            pageH = std::max(pageH, sa->widget()->minimumHeight());
        }
    resize(m_list->maximumWidth() + pageW + 60, pageH + 90);
}

void PreferencesDialog::buildPages()
{
    // Dialog units -> pixels with the dialog font (Win32 GetDialogBaseUnits for "MS Shell Dlg 8")
    const QFontMetrics fm(font());
    const double baseX = fm.horizontalAdvance(QStringLiteral("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz")) / 52.0;
    const double baseY = fm.height();
    auto dx = [baseX](int v) { return static_cast<int>(v * baseX / 4 + 0.5); };
    auto dy = [baseY](int v) { return static_cast<int>(v * baseY / 8 + 0.5); };

    for (const PageDef& def : pageDefs()) {
#if !defined(Q_OS_WIN)
        if (std::string_view(def.tag) == "FileAssoc")
            continue; // Windows only (registry file associations)
#endif
        Page page;
        page.tag = QString::fromLatin1(def.tag);
        page.englishTitle = QString::fromUtf8(def.title);
        auto* w = new QWidget;
        w->setObjectName(page.tag);
        // the upstream page starts at x = 115 in the dialog: content is shifted left
        const int left = 0;
        w->setMinimumSize(dx(def.w), dy(def.h));
        QButtonGroup* radioGroup = nullptr;
        for (const CtrlDef& rc : def.ctrls) {
            const CtrlDef c = moved(def.tag, rc);
            QWidget* ctrl = nullptr;
            const QString text = rcText(QString::fromUtf8(c.text));
            if (c.kind != Radio && (c.flags & kPrefNewGroup))
                radioGroup = nullptr;
            switch (c.kind) {
            case Group: {
                auto* g = new QGroupBox(labelText(text), w);
                g->lower();
                ctrl = g;
                break;
            }
            case Label: {
                if (c.flags & kPrefLine) {
                    auto* f = new QFrame(w);
                    f->setFrameShape(QFrame::HLine);
                    ctrl = f;
                } else {
                    auto* l = new QLabel(labelText(text), w);
                    l->setAlignment((c.flags & kPrefRight) ? Qt::AlignRight | Qt::AlignVCenter
                                    : (c.flags & kPrefCenter) ? Qt::AlignHCenter | Qt::AlignVCenter : Qt::AlignLeft | Qt::AlignVCenter);
                    l->setWordWrap(dy(c.h) > fm.height() * 3 / 2);
                    ctrl = l;
                }
                break;
            }
            case Check:
                ctrl = new QCheckBox(text, w);
                break;
            case Radio: {
                auto* r = new QRadioButton(text, w);
                r->setAutoExclusive(false);
                if (!radioGroup || (c.flags & kPrefNewGroup))
                    radioGroup = new QButtonGroup(w);
                radioGroup->addButton(r, c.id);
                ctrl = r;
                break;
            }
            case Combo:
            case ComboEdit: {
                auto* cb = new QComboBox(w);
                cb->setEditable(c.kind == ComboEdit);
                ctrl = cb;
                break;
            }
            case Edit: {
                auto* e = new QLineEdit(w);
                ctrl = e;
                break;
            }
            case Button:
                ctrl = new QPushButton(text, w);
                break;
            case List:
                ctrl = new QListWidget(w);
                break;
            case Slider: {
                auto* s = new QSlider(Qt::Horizontal, w);
                ctrl = s;
                break;
            }
            case Spin:
                ctrl = new QSpinBox(w);
                break;
            }
            if (!ctrl)
                continue;
            const int minHeight = c.kind == Edit ? std::max(fm.height() + 8, ctrl->minimumSizeHint().height())
                                  : c.kind == Combo || c.kind == ComboEdit ? fm.height() + 8 : 0;
            ctrl->setGeometry(dx(c.x - left), dy(c.y), dx(c.w), std::max(dy(c.h), minHeight));
            ctrl->setProperty(kRcWidth, dx(c.w));
            ctrl->setProperty(kRcHeight, dy(c.h));
            if (c.kind == Check || c.kind == Radio) // let texts longer than the rc box (translations) show
                ctrl->resize(std::max(ctrl->width(), ctrl->sizeHint().width()), std::max(ctrl->height(), ctrl->sizeHint().height()));
            if (c.flags & kPrefDisabled)
                ctrl->setEnabled(false);
            if (c.id <= 0)
                ctrl->setObjectName(QStringLiteral("static")); // IDC_STATIC: fixed text, not translated upstream either
            if (c.id > 0) {
                ctrl->setObjectName(QString::number(c.id));
                page.controls[c.id] = ctrl;
                page.englishTexts[c.id] = text;
            }
        }
        auto* scroll = new QScrollArea;
        scroll->setWidget(w);
        scroll->setFrameShape(QFrame::NoFrame);
        page.widget = scroll;
        m_stack->addWidget(scroll);
        m_list->addItem(page.englishTitle);
        m_pages.push_back(std::move(page));
    }
}

QWidget* PreferencesDialog::control(const QString& pageTag, int id) const
{
    for (const Page& p : m_pages)
        if (p.tag == pageTag) {
            const auto it = p.controls.find(id);
            return it == p.controls.end() ? nullptr : it->second;
        }
    return nullptr;
}

QStringList PreferencesDialog::pageTags() const
{
    QStringList out;
    for (const Page& p : m_pages)
        out << p.tag;
    return out;
}

void PreferencesDialog::showPage(const QString& tag)
{
    for (int i = 0; i < static_cast<int>(m_pages.size()); ++i)
        if (m_pages[static_cast<size_t>(i)].tag == tag)
            m_list->setCurrentRow(i);
    show();
    raise();
    activateWindow();
}

void PreferencesDialog::reload()
{
    m_loading = true;
    for (const Binding& b : m_bindings)
        if (b.load)
            b.load();
    m_loading = false;
}

void PreferencesDialog::changed()
{
    if (m_loading)
        return;
    for (const Binding& b : m_bindings)
        if (b.store)
            b.store();
    emit settingsChanged();
    reload(); // dependent controls (enabled states)
}

void PreferencesDialog::translate()
{
    const core::NativeLanguage* nl = m_ctx.nativeLang;
    if (const auto d = nl ? nl->dialog("Preference") : std::nullopt) {
        if (!d->title.empty())
            setWindowTitle(QString::fromStdString(core::NativeLanguage::purifyMenuString(d->title)));
        if (const auto it = d->items.find(6001); it != d->items.end()) // IDC_BUTTON_CLOSE
            if (auto* close = findChild<QPushButton*>(QStringLiteral("6001")))
                close->setText(QString::fromStdString(it->second));
    }
    for (int i = 0; i < static_cast<int>(m_pages.size()); ++i) {
        Page& p = m_pages[static_cast<size_t>(i)];
        const auto d = nl ? nl->dialog(p.tag.toStdString()) : std::nullopt;
        m_list->item(i)->setText(d && !d->title.empty() ? QString::fromStdString(d->title) : p.englishTitle);
        for (auto& [id, w] : p.controls) {
            QString text = p.englishTexts[id];
            if (d)
                if (const auto it = d->items.find(id); it != d->items.end())
                    text = rcText(QString::fromStdString(it->second));
            if (auto* b = qobject_cast<QAbstractButton*>(w)) {
                const int rcWidth = b->property(kRcWidth).toInt();
                const QFontMetrics metrics = b->fontMetrics();
                b->setText(text);
                if (qobject_cast<QPushButton*>(b)) {
                    // a push button's size hint has the style's minimum width: it would blow up the small "..." / "?" buttons
                    b->resize(std::max(rcWidth, metrics.horizontalAdvance(text) + 12), b->height());
                } else {
                    // two line rc boxes wrap like upstream's multi-line buttons instead of running over their neighbours
                    if (b->sizeHint().width() > rcWidth && b->property(kRcHeight).toInt() >= 2 * metrics.height())
                        b->setText(wrappedText(text, metrics, rcWidth - (b->sizeHint().width() - widestLine(text, metrics))));
                    b->resize(std::max(rcWidth, b->sizeHint().width()), std::max(b->height(), b->sizeHint().height()));
                }
            } else if (auto* g = qobject_cast<QGroupBox*>(w)) {
                g->setTitle(labelText(text));
            } else if (auto* l = qobject_cast<QLabel*>(w)) {
                l->setText(labelText(text));
            }
        }
    }
    // MISC combo boxes: upstream <ComboBox> entries; item -> index of the upstream entry
    const auto misc = nl ? nl->dialog("MISC") : std::nullopt;
    const auto scintillas = nl ? nl->dialog("Scintillas") : std::nullopt;
    const struct { int id; std::vector<int> entries; } combos[] = {{6347, {1, 0, 2}}, {6307, {0, 1, 2, 3}}, {6364, {0}}};
    auto setItems = [this](int id, const std::function<QString(int)>& translated) {
        auto* c = qobject_cast<QComboBox*>(control(QStringLiteral("MISC"), id));
        if (!c)
            return;
        if (!c->property(kEnglishItems).isValid()) {
            QStringList english;
            for (int i = 0; i < c->count(); ++i)
                english << c->itemText(i);
            c->setProperty(kEnglishItems, english);
        }
        const QStringList english = c->property(kEnglishItems).toStringList();
        for (int i = 0; i < c->count(); ++i) {
            const QString t = translated(i);
            c->setItemText(i, t.isEmpty() ? english.value(i) : t);
        }
    };
    for (const auto& combo : combos)
        setItems(combo.id, [&](int i) {
            if (misc)
                if (const auto it = misc->comboBoxes.find(combo.id);
                    it != misc->comboBoxes.end() && static_cast<size_t>(combo.entries[static_cast<size_t>(i)]) < it->second.size())
                    return QString::fromStdString(it->second[static_cast<size_t>(combo.entries[static_cast<size_t>(i)])]);
            return QString();
        });
    setItems(6362, [&](int) { // Qt picks the renderer: only "Default", worded like the Line Wrap option
        if (scintillas)
            if (const auto it = scintillas->items.find(6228); it != scintillas->items.end())
                return QString::fromStdString(it->second);
        return QString();
    });
    setLayoutDirection(nl && nl->isRTL() ? Qt::RightToLeft : Qt::LeftToRight);
}

} // namespace npp::ui
