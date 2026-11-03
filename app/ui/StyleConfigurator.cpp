#include "StyleConfigurator.h"

#include "core/i18n/NativeLanguage.h"
#include "core/lang/LangType.h"
#include "core/lang/LanguageRegistry.h"
#include "core/util/PathUtil.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileInfo>
#include <QFontComboBox>
#include <QFontDatabase>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>

namespace npp::ui {

using namespace npp::core;

namespace {

constexpr char kGlobalStyles[] = "Global Styles";
constexpr int kStyleDefault = 32; // STYLE_DEFAULT
const char* const kOverrideNames[7] = {"Force foreground color for all styles", "Force background color for all styles",
                                       "Force font choice for all styles", "Force font size choice for all styles",
                                       "Force bold choice for all styles", "Force italic choice for all styles",
                                       "Force underline choice for all styles"};

void setSwatch(QPushButton* b, int rgb)
{
    if (rgb < 0) {
        b->setStyleSheet(QString());
        b->setText(QStringLiteral("…"));
    } else {
        b->setText(QString());
        b->setStyleSheet(QStringLiteral("background: #%1; min-width: 40px;").arg(rgb, 6, 16, QLatin1Char('0')));
    }
}

} // namespace

StyleConfigurator::StyleConfigurator(const Context& ctx, QWidget* parent) : QDialog(parent), m_ctx(ctx)
{
    setObjectName(QStringLiteral("StyleConfig"));
    setWindowTitle(tr("Style Configurator"));
    m_backupXml = m_ctx.styles->saveToString();
    m_backupPath = *m_ctx.currentThemePath;
    m_themePath = m_backupPath;

    m_theme = new QComboBox;
    m_theme->addItem(QStringLiteral("Default (stylers.xml)"), QString());
    for (const std::string& dir : {m_ctx.dataThemesDir, m_ctx.userThemesDir})
        for (const auto& [name, path] : StyleRegistry::themesIn(dir))
            if (m_theme->findText(QString::fromStdString(name)) < 0)
                m_theme->addItem(QString::fromStdString(name), QString::fromStdString(path));
    for (int i = 1; i < m_theme->count(); ++i)
        if (QFileInfo(m_theme->itemData(i).toString()) == QFileInfo(QString::fromStdString(m_themePath)))
            m_theme->setCurrentIndex(i);
    connect(m_theme, &QComboBox::activated, this, [this](int i) {
        const QString p = m_theme->itemData(i).toString();
        loadTheme(p.isEmpty() ? (pathExists(m_ctx.userStylersXml, PathKind::File) ? m_ctx.userStylersXml : m_ctx.modelStylersXml) : p.toStdString());
    });

    m_languages = new QListWidget;
    m_stylesList = new QListWidget;
    connect(m_languages, &QListWidget::currentRowChanged, this, [this] { fillStyles(); });
    connect(m_stylesList, &QListWidget::currentRowChanged, this, [this] { showStyle(); });

    m_fg = new QPushButton;
    m_bg = new QPushButton;
    for (QPushButton* b : {m_fg, m_bg})
        connect(b, &QPushButton::clicked, this, [this, b] {
            Style* s = currentStyle();
            if (!s)
                return;
            const int cur = b == m_fg ? s->fg : s->bg;
            const QColor c = QColorDialog::getColor(cur >= 0 ? QColor::fromRgb(static_cast<QRgb>(cur)) : Qt::black, this);
            if (!c.isValid())
                return;
            (b == m_fg ? s->fg : s->bg) = static_cast<int>(c.rgb() & 0xFFFFFF);
            showStyle();
            styleChanged();
        });
    m_font = new QFontComboBox;
    m_font->setEditable(true);
    m_fontSize = new QComboBox;
    m_fontSize->setEditable(true);
    m_fontSize->addItem(QString());
    for (int sz : {5, 6, 7, 8, 9, 10, 11, 12, 14, 16, 18, 20, 22, 24, 26, 28})
        m_fontSize->addItem(QString::number(sz));
    m_bold = new QCheckBox(tr("Bold"));
    m_italic = new QCheckBox(tr("Italic"));
    m_underline = new QCheckBox(tr("Underline"));
    connect(m_font, &QComboBox::currentTextChanged, this, [this](const QString& f) {
        if (m_loading || !currentStyle())
            return;
        currentStyle()->fontName = f.toStdString();
        styleChanged();
    });
    connect(m_fontSize, &QComboBox::currentTextChanged, this, [this](const QString& t) {
        if (m_loading || !currentStyle())
            return;
        currentStyle()->fontSize = t.isEmpty() ? -1 : t.toInt();
        styleChanged();
    });
    for (QCheckBox* c : {m_bold, m_italic, m_underline})
        connect(c, &QCheckBox::toggled, this, [this] {
            Style* s = currentStyle();
            if (m_loading || !s)
                return;
            s->fontStyle = (m_bold->isChecked() ? 1 : 0) | (m_italic->isChecked() ? 2 : 0) | (m_underline->isChecked() ? 4 : 0);
            styleChanged();
        });
    m_defExt = new QLineEdit;
    m_defExt->setReadOnly(true);
    m_userExt = new QLineEdit;
    connect(m_userExt, &QLineEdit::editingFinished, this, [this] {
        if (LexerStyler* l = currentLexer()) {
            l->ext = m_userExt->text().trimmed().toStdString();
            styleChanged();
        }
    });
    m_defKeywords = new QPlainTextEdit;
    m_defKeywords->setReadOnly(true);
    m_userKeywords = new QPlainTextEdit;
    connect(m_userKeywords, &QPlainTextEdit::textChanged, this, [this] {
        Style* s = currentStyle();
        if (m_loading || !s)
            return;
        s->keywords = m_userKeywords->toPlainText().toStdString();
        styleChanged();
    });
    m_desc = new QLabel;
    m_desc->setObjectName(QStringLiteral("npp-data")); // style name
    m_desc->setWordWrap(true);

    // layout (upstream WordStyleDlg: theme on top, language / style lists left, settings right)
    auto* colours = new QGroupBox(tr("Color Style"));
    colours->setObjectName(QStringLiteral("2212"));
    auto* cl = new QFormLayout(colours);
    auto* fgLabel = new QLabel(tr("Foreground color"));
    fgLabel->setObjectName(QStringLiteral("2206"));
    auto* bgLabel = new QLabel(tr("Background color"));
    bgLabel->setObjectName(QStringLiteral("2207"));
    cl->addRow(fgLabel, m_fg);
    cl->addRow(bgLabel, m_bg);
    auto* fonts = new QGroupBox(tr("Font Style"));
    fonts->setObjectName(QStringLiteral("2213"));
    auto* fl = new QGridLayout(fonts);
    auto* fontLabel = new QLabel(tr("Font name:"));
    fontLabel->setObjectName(QStringLiteral("2208"));
    auto* sizeLabel = new QLabel(tr("Font size:"));
    sizeLabel->setObjectName(QStringLiteral("2209"));
    fl->addWidget(fontLabel, 0, 0);
    fl->addWidget(m_font, 0, 1, 1, 3);
    fl->addWidget(sizeLabel, 1, 0);
    fl->addWidget(m_fontSize, 1, 1);
    fl->addWidget(m_bold, 2, 0);
    fl->addWidget(m_italic, 2, 1);
    fl->addWidget(m_underline, 2, 2);
    m_bold->setObjectName(QStringLiteral("2204"));
    m_italic->setObjectName(QStringLiteral("2205"));
    m_underline->setObjectName(QStringLiteral("2218"));
    auto* overrides = new QGroupBox;
    auto* ol = new QVBoxLayout(overrides);
    bool GuiConfig::*fields[7] = {&GuiConfig::globalOverrideFg, &GuiConfig::globalOverrideBg, &GuiConfig::globalOverrideFont,
                                  &GuiConfig::globalOverrideFontSize, &GuiConfig::globalOverrideBold, &GuiConfig::globalOverrideItalic,
                                  &GuiConfig::globalOverrideUnderline};
    for (int i = 0; i < 7; ++i) {
        m_override[i] = new QCheckBox(tr(kOverrideNames[i]));
        m_override[i]->setObjectName(QString::number(2226 + i));
        ol->addWidget(m_override[i]);
        bool GuiConfig::*f = fields[i];
        connect(m_override[i], &QCheckBox::toggled, this, [this, f](bool on) {
            if (m_loading)
                return;
            m_ctx.gui->*f = on;
            styleChanged();
        });
    }
    auto* extForm = new QGridLayout;
    auto* defExtLabel = new QLabel(tr("Default ext.:"));
    defExtLabel->setObjectName(QStringLiteral("2214"));
    auto* userExtLabel = new QLabel(tr("User ext.:"));
    userExtLabel->setObjectName(QStringLiteral("2216"));
    extForm->addWidget(defExtLabel, 0, 0);
    extForm->addWidget(m_defExt, 0, 1);
    extForm->addWidget(userExtLabel, 0, 2);
    extForm->addWidget(m_userExt, 0, 3);
    auto* kwForm = new QGridLayout;
    auto* defKwLabel = new QLabel(tr("Default keywords"));
    defKwLabel->setObjectName(QStringLiteral("2219"));
    auto* userKwLabel = new QLabel(tr("User-defined keywords"));
    userKwLabel->setObjectName(QStringLiteral("2221"));
    kwForm->addWidget(defKwLabel, 0, 0);
    kwForm->addWidget(userKwLabel, 0, 1);
    kwForm->addWidget(m_defKeywords, 1, 0);
    kwForm->addWidget(m_userKeywords, 1, 1);

    auto* right = new QVBoxLayout;
    right->addWidget(m_desc);
    auto* groups = new QHBoxLayout;
    groups->addWidget(colours);
    groups->addWidget(fonts, 1);
    right->addLayout(groups);
    right->addWidget(overrides);
    right->addLayout(extForm);
    right->addLayout(kwForm, 1);

    auto* lists = new QGridLayout;
    auto* langLabel = new QLabel(tr("Language:"));
    langLabel->setObjectName(QStringLiteral("2225"));
    auto* styleLabel = new QLabel(tr("Style:"));
    styleLabel->setObjectName(QStringLiteral("2211"));
    lists->addWidget(langLabel, 0, 0);
    lists->addWidget(styleLabel, 0, 1);
    lists->addWidget(m_languages, 1, 0);
    lists->addWidget(m_stylesList, 1, 1);

    auto* themeRow = new QHBoxLayout;
    auto* themeLabel = new QLabel(tr("Select theme: "));
    themeLabel->setObjectName(QStringLiteral("2306"));
    themeRow->addWidget(themeLabel);
    themeRow->addWidget(m_theme, 1);
    auto* buttons = new QDialogButtonBox;
    QPushButton* save = buttons->addButton(tr("Save && Close"), QDialogButtonBox::AcceptRole);
    save->setObjectName(QStringLiteral("2301"));
    QPushButton* cancelBtn = buttons->addButton(QDialogButtonBox::Cancel);
    cancelBtn->setObjectName(QStringLiteral("2"));
    connect(save, &QPushButton::clicked, this, [this] { saveAndClose(); });
    connect(cancelBtn, &QPushButton::clicked, this, [this] { cancel(); });

    auto* body = new QHBoxLayout;
    body->addLayout(lists);
    body->addLayout(right, 1);
    auto* root = new QVBoxLayout(this);
    root->addLayout(themeRow);
    root->addLayout(body, 1);
    root->addWidget(buttons);
    resize(980, 560);

    fillLanguages();
    translate();
}

void StyleConfigurator::translate()
{
    const NativeLanguage* nl = m_ctx.nativeLang;
    const auto d = nl ? nl->dialog("StyleConfig") : std::nullopt;
    if (!d)
        return;
    if (!d->title.empty())
        setWindowTitle(QString::fromStdString(d->title));
    for (const auto& [id, text] : d->items)
        if (QWidget* w = findChild<QWidget*>(QString::number(id))) {
            const QString t = QString::fromStdString(text);
            if (auto* b = qobject_cast<QAbstractButton*>(w))
                b->setText(t);
            else if (auto* l = qobject_cast<QLabel*>(w))
                l->setText(t);
            else if (auto* g = qobject_cast<QGroupBox*>(w))
                g->setTitle(t);
        }
}

void StyleConfigurator::loadTheme(const std::string& path)
{
    m_ctx.styles->load(path);
    m_themePath = path;
    *m_ctx.currentThemePath = path;
    if (m_ctx.apply)
        m_ctx.apply();
    fillLanguages();
}

void StyleConfigurator::fillLanguages()
{
    const QString cur = m_languages->currentItem() ? m_languages->currentItem()->text() : QString::fromLatin1(kGlobalStyles);
    QSignalBlocker block(m_languages);
    m_languages->clear();
    m_languages->addItem(QString::fromLatin1(kGlobalStyles));
    for (const LexerStyler& l : m_ctx.styles->lexers())
        (new QListWidgetItem(QString::fromStdString(l.desc.empty() ? l.name : l.desc), m_languages))->setData(Qt::UserRole, QString::fromStdString(l.name));
    m_languages->item(0)->setData(Qt::UserRole, QString::fromLatin1(kGlobalStyles));
    const auto found = m_languages->findItems(cur, Qt::MatchExactly);
    m_languages->setCurrentItem(found.isEmpty() ? m_languages->item(0) : found.first());
    fillStyles();
}

LexerStyler* StyleConfigurator::currentLexer()
{
    if (!m_languages->currentItem())
        return nullptr;
    return m_ctx.styles->lexer(m_languages->currentItem()->data(Qt::UserRole).toString().toStdString());
}

Style* StyleConfigurator::currentStyle()
{
    const int row = m_stylesList->currentRow();
    if (row < 0)
        return nullptr;
    if (LexerStyler* l = currentLexer())
        return row < static_cast<int>(l->styles.size()) ? &l->styles[static_cast<size_t>(row)] : nullptr;
    auto& g = m_ctx.styles->globalStyles();
    return row < static_cast<int>(g.size()) ? &g[static_cast<size_t>(row)] : nullptr;
}

void StyleConfigurator::fillStyles()
{
    QSignalBlocker block(m_stylesList);
    m_stylesList->clear();
    if (LexerStyler* l = currentLexer()) {
        for (const Style& s : l->styles)
            m_stylesList->addItem(QString::fromStdString(s.name));
        QString exts;
        if (const auto t = langTypeFromName(l->name))
            if (const LanguageDef* def = m_ctx.langs->find(*t))
                exts = QString::fromStdString(def->extAttribute);
        m_defExt->setText(exts);
        m_userExt->setText(QString::fromStdString(l->ext));
    } else {
        for (const Style& s : m_ctx.styles->globalStyles())
            m_stylesList->addItem(QString::fromStdString(s.name));
        m_defExt->clear();
        m_userExt->clear();
    }
    const bool lexer = currentLexer() != nullptr;
    m_defExt->setEnabled(lexer);
    m_userExt->setEnabled(lexer);
    m_stylesList->setCurrentRow(0);
    showStyle();
}

void StyleConfigurator::showStyle()
{
    m_loading = true;
    const Style* s = currentStyle();
    const bool has = s != nullptr;
    for (QWidget* w : std::initializer_list<QWidget*>{m_fg, m_bg, m_font, m_fontSize, m_bold, m_italic, m_underline})
        w->setEnabled(has);
    if (s) {
        setSwatch(m_fg, s->fg);
        setSwatch(m_bg, s->bg);
        m_font->setCurrentText(QString::fromStdString(s->fontName));
        m_fontSize->setCurrentText(s->fontSize > 0 ? QString::number(s->fontSize) : QString());
        m_bold->setChecked(s->fontStyle > 0 && (s->fontStyle & 1));
        m_italic->setChecked(s->fontStyle > 0 && (s->fontStyle & 2));
        m_underline->setChecked(s->fontStyle > 0 && (s->fontStyle & 4));
        m_userKeywords->setPlainText(QString::fromStdString(s->keywords));
        m_userKeywords->setEnabled(s->keywordClass >= 0);
        QString def;
        if (const LexerStyler* l = currentLexer(); l && s->keywordClass >= 0)
            if (const auto t = langTypeFromName(l->name))
                if (const LanguageDef* ld = m_ctx.langs->find(*t); ld && s->keywordClass < static_cast<int>(ld->keywords.size()))
                    def = QString::fromStdString(ld->keywords[static_cast<size_t>(s->keywordClass)]);
        m_defKeywords->setPlainText(def);
        m_desc->setText(QString::fromStdString(s->name));
    }
    // global override: only on the "Global override" style (upstream)
    const bool globalOverride = s && !currentLexer() && s->name == "Global override";
    bool GuiConfig::*fields[7] = {&GuiConfig::globalOverrideFg, &GuiConfig::globalOverrideBg, &GuiConfig::globalOverrideFont,
                                  &GuiConfig::globalOverrideFontSize, &GuiConfig::globalOverrideBold, &GuiConfig::globalOverrideItalic,
                                  &GuiConfig::globalOverrideUnderline};
    for (int i = 0; i < 7; ++i) {
        m_override[i]->setChecked(m_ctx.gui->*fields[i]);
        m_override[i]->setEnabled(globalOverride);
    }
    m_loading = false;
}

void StyleConfigurator::styleChanged()
{
    if (m_loading)
        return;
    if (m_ctx.apply)
        m_ctx.apply();
}

void StyleConfigurator::selectLanguage(const QString& name)
{
    for (int i = 0; i < m_languages->count(); ++i)
        if (m_languages->item(i)->data(Qt::UserRole).toString() == name || m_languages->item(i)->text() == name)
            m_languages->setCurrentRow(i);
}

void StyleConfigurator::selectStyle(const QString& name)
{
    const auto found = m_stylesList->findItems(name, Qt::MatchExactly);
    if (!found.isEmpty())
        m_stylesList->setCurrentItem(found.first());
}

void StyleConfigurator::setForeground(int rgb)
{
    if (Style* s = currentStyle()) {
        s->fg = rgb;
        showStyle();
        styleChanged();
    }
}

void StyleConfigurator::setBold(bool on) { m_bold->setChecked(on); }

void StyleConfigurator::setTheme(const QString& themeName)
{
    const int i = m_theme->findText(themeName);
    if (i < 0)
        return;
    m_theme->setCurrentIndex(i);
    emit m_theme->activated(i);
}

// Save && Close: theme files of the program folder are read-only for users: saved in the user's
// themes folder (upstream does the same for themes it cannot write)
bool StyleConfigurator::saveAndClose()
{
    std::string target = m_themePath;
    if (target.empty() || target == m_ctx.modelStylersXml)
        target = m_ctx.userStylersXml;
    else if (!m_ctx.dataThemesDir.empty() && QFileInfo(QString::fromStdString(target)).absolutePath() == QFileInfo(QString::fromStdString(m_ctx.dataThemesDir + "/x")).absolutePath())
        target = pathAppend(m_ctx.userThemesDir, QFileInfo(QString::fromStdString(target)).fileName().toStdString());
    std::error_code ec;
    fs::create_directories(pathFromUtf8(target).parent_path(), ec);
    if (!m_ctx.styles->save(target)) {
        QMessageBox::warning(this, QStringLiteral("Notepad++"), tr("The theme could not be saved:\n%1").arg(QString::fromStdString(target)));
        return false;
    }
    *m_ctx.currentThemePath = target;
    m_backupXml = m_ctx.styles->saveToString();
    m_backupPath = target;
    accept();
    return true;
}

void StyleConfigurator::cancel()
{
    m_ctx.styles->loadFromString(m_backupXml);
    *m_ctx.currentThemePath = m_backupPath;
    if (m_ctx.apply)
        m_ctx.apply();
    reject();
}

} // namespace npp::ui
