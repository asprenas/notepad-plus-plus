// Preferences dialog (phase 4, P4-01/02): the 24 upstream pages generated from preference.rc
// (PreferencePages.inc: control IDs, kinds, texts, positions in dialog units), translated by ID
// from nativeLang <Dialog><Preference>, controls bound to GuiConfig (PreferenceBindings.cpp).
// Changes apply immediately (settingsChanged) like upstream; config.xml is written on exit.
#pragma once

#include <QDialog>

#include <functional>
#include <map>
#include <string>
#include <vector>

class QListWidget;
class QStackedWidget;
class QWidget;

namespace npp::core {
struct GuiConfig;
class ConfigFile;
class NativeLanguage;
}

namespace npp::ui {

class PreferencesDialog final : public QDialog {
    Q_OBJECT
public:
    // Hooks to the application for controls that are not plain GuiConfig fields
    struct Context {
        core::ConfigFile* config = nullptr;
        const core::NativeLanguage* nativeLang = nullptr;
        std::function<std::vector<std::pair<std::string, std::string>>()> availableLanguages; // (name, file)
        std::function<std::string()> currentLanguageFile;
        std::function<void(const std::string& file)> setLanguageFile;
        std::function<std::vector<std::string>()> languageNames;   // all languages (menu names) for Language / NewDoc
        std::function<void()> openStyleConfigurator;
    };

    PreferencesDialog(const Context& ctx, QWidget* parent = nullptr);

    void showPage(const QString& tag);
    QWidget* control(const QString& pageTag, int id) const;
    QStringList pageTags() const;
    void reload();     // config -> controls
    void translate();  // texts from nativeLang

signals:
    void settingsChanged();

private:
    struct Page {
        QString tag, englishTitle;
        QWidget* widget = nullptr;
        std::map<int, QWidget*> controls;
        std::map<int, QString> englishTexts;
    };
    void buildPages();
    void setupBindings();
    void changed();

    Context m_ctx;
    QListWidget* m_list = nullptr;
    QStackedWidget* m_stack = nullptr;
    std::vector<Page> m_pages;
    bool m_loading = false;

    struct Binding {
        std::function<void()> load;   // config -> widget
        std::function<void()> store;  // widget -> config
    };
    std::vector<Binding> m_bindings;
    friend struct PreferenceBinder;
};

} // namespace npp::ui
