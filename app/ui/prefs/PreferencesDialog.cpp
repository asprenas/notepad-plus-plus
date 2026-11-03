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
        for (const CtrlDef& c : def.ctrls) {
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
            ctrl->setGeometry(dx(c.x - left), dy(c.y), dx(c.w), std::max(dy(c.h), c.kind == Combo || c.kind == ComboEdit || c.kind == Edit ? fm.height() + 8 : 0));
            if (c.kind == Check || c.kind == Radio) // let texts longer than the rc box (translations) show
                ctrl->resize(std::max(ctrl->width(), ctrl->sizeHint().width()), std::max(ctrl->height(), ctrl->sizeHint().height()));
            if (c.flags & kPrefDisabled)
                ctrl->setEnabled(false);
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
                b->setText(text);
                b->resize(std::max(b->width(), b->sizeHint().width()), b->height());
            } else if (auto* g = qobject_cast<QGroupBox*>(w)) {
                g->setTitle(labelText(text));
            } else if (auto* l = qobject_cast<QLabel*>(w)) {
                l->setText(labelText(text));
            }
        }
    }
    setLayoutDirection(nl && nl->isRTL() ? Qt::RightToLeft : Qt::LeftToRight);
}

} // namespace npp::ui
