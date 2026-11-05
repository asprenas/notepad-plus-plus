// Style Configurator (phase 4, P4-03): port of upstream WordStyleDlg. Edits a working copy of the
// current theme (stylers.xml or themes/*.xml); changes are previewed in the editors at once;
// "Save && Close" writes the theme file, Cancel restores the previous styles.
#pragma once

#include "core/config/GuiConfig.h"
#include "core/lang/StyleRegistry.h"

#include <QDialog>

#include <functional>
#include <string>

class QCheckBox;
class QComboBox;
class QFontComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QPushButton;

namespace npp::core {
class LanguageRegistry;
class NativeLanguage;
}

namespace npp::ui {

class StyleConfigurator final : public QDialog {
    Q_OBJECT
public:
    struct Context {
        core::StyleRegistry* styles = nullptr;          // the registry used by the editors
        std::string* currentThemePath = nullptr;        // file the registry was loaded from
        core::GuiConfig* gui = nullptr;
        const core::LanguageRegistry* langs = nullptr;
        const core::NativeLanguage* nativeLang = nullptr;
        std::string userThemesDir, dataThemesDir, userStylersXml, modelStylersXml;
        std::function<void()> apply;                   // re-apply styles to the editors
    };
    StyleConfigurator(const Context& ctx, QWidget* parent = nullptr);

    // For tests
    void selectLanguage(const QString& name);           // "Global Styles" or a lexer name
    void selectStyle(const QString& name);
    void setForeground(int rgb);
    void setBold(bool on);
    void setTheme(const QString& themeName);            // as listed in the theme combo
    bool saveAndClose();
    void cancel();
    QString currentThemeFile() const { return QString::fromStdString(m_themePath); }

private:
    void loadTheme(const std::string& path);
    void fillLanguages();
    void fillStyles();
    void showStyle();
    void styleChanged();
    core::Style* currentStyle();
    core::LexerStyler* currentLexer();
    void translate();

    Context m_ctx;
    std::string m_backupXml;          // registry before opening (Cancel)
    std::string m_backupPath;
    std::string m_themePath;
    bool m_loading = false;

    QComboBox* m_theme = nullptr;
    QListWidget *m_languages = nullptr, *m_stylesList = nullptr;
    QPushButton *m_fg = nullptr, *m_bg = nullptr;
    QFontComboBox* m_font = nullptr;
    QComboBox* m_fontSize = nullptr;
    QCheckBox *m_bold = nullptr, *m_italic = nullptr, *m_underline = nullptr;
    QLineEdit *m_defExt = nullptr, *m_userExt = nullptr;
    QPlainTextEdit *m_defKeywords = nullptr, *m_userKeywords = nullptr;
    QCheckBox* m_override[7] = {};
    QLabel* m_desc = nullptr;
};

} // namespace npp::ui
