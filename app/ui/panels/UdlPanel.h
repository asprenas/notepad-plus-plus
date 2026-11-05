// User Defined Language dialog (upstream ScintillaComponent/UserDefineDialog), docked through
// DockManager: language selection / create / save as / import / export / rename / remove, and the
// four pages (Folder & Default, Keywords Lists, Comment & Number, Operators & Delimiters) with
// the Styler dialog. Edits apply immediately to the in-memory UDL (upstream behaviour) and are
// written to userDefineLang.xml / userDefineLangs/*.xml.
#pragma once

#include "core/lang/UserLanguage.h"

#include <QWidget>

#include <functional>

class QCheckBox;
class QComboBox;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QRadioButton;
class QTabWidget;

namespace npp::core {
class NativeLanguage;
}

namespace npp::ui {

class UdlPanel final : public QWidget {
    Q_OBJECT
public:
    UdlPanel(core::UserLanguageRegistry& registry, QWidget* parent = nullptr);

    void setNativeLanguage(const core::NativeLanguage* nl);
    void reloadLanguages(const QString& select = {});
    QString currentName() const;
    void selectLanguage(const QString& name);

    // Operations (also the buttons)
    bool createLanguage(const QString& name, bool copyCurrent);
    bool renameCurrent(const QString& newName);
    bool removeCurrent();
    bool importFile(const QString& path);  // returns false if no UDL in the file
    bool exportCurrent(const QString& path);

    // Field access by upstream control id (tests): QPlainTextEdit / QLineEdit text
    QWidget* control(int id) const;
    QTabWidget* tabs() const noexcept { return m_tabs; }

    // Styler dialog for SCE_USER_STYLE_* `styleId` (modal)
    void editStyle(int styleId);
    std::function<bool(QWidget*)> execStyler; // tests: replaces the modal exec

signals:
    void languageChanged(const QString& name); // keywords / styles / settings edited
    void languagesChanged();                   // list changed (create, rename, remove, import)
    void saveRequested();                      // write the UDL files

private:
    core::UserLanguage* current();
    void load(); // current UDL -> controls
    void store(); // controls -> current UDL
    void commit();
    QWidget* buildFolderPage();
    QWidget* buildKeywordsPage();
    QWidget* buildCommentPage();
    QWidget* buildOperatorsPage();
    QPlainTextEdit* edit(int id, bool multiline = false);
    QPushButton* styler(int id, int styleId);

    core::UserLanguageRegistry& m_reg;
    const core::NativeLanguage* m_nl = nullptr;
    QComboBox* m_combo = nullptr;
    QLineEdit* m_ext = nullptr;
    QCheckBox* m_ignoreCase = nullptr;
    QTabWidget* m_tabs = nullptr;
    QCheckBox* m_foldCompact = nullptr;
    QCheckBox* m_foldComments = nullptr;
    QCheckBox* m_prefix[8] = {};
    QRadioButton* m_lineCommentPos[3] = {};
    QRadioButton* m_decimal[3] = {};
    std::vector<QWidget*> m_controls;
    QList<QWidget*> m_editors;
    bool m_loading = false;
};

} // namespace npp::ui
