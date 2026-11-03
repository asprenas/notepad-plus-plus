// Shortcut Mapper (phase 4, P4-04): port of upstream ShortcutMapper. Tabs Main menu / Macros /
// Run commands / Plugin commands / Scintilla commands, conflict marking, modify / clear / delete,
// saved to shortcuts.xml in upstream format (VK codes; only shortcuts that differ from the
// defaults are written to <InternalCommands> / <ScintillaKeys>, like upstream).
#pragma once

#include "core/shortcut/Shortcuts.h"

#include <QDialog>

#include <functional>
#include <vector>

class QLabel;
class QLineEdit;
class QTabWidget;
class QTableWidget;

namespace npp::core {
class NativeLanguage;
}

namespace npp::ui {

class ActionRegistry;

class ShortcutMapper final : public QDialog {
    Q_OBJECT
public:
    struct Row {
        int tab = 0;               // 0 main menu, 1 macros, 2 run, 3 plugins, 4 scintilla
        int id = 0;                // command id / macro index / user command index / SCI message
        QString name, category;
        core::KeyCombo key, defaultKey;
    };
    ShortcutMapper(const core::ShortcutsFile& current, ActionRegistry* actions, const core::NativeLanguage* nl,
                   QWidget* parent = nullptr);

    // Result: shortcuts.xml model with the edited keys
    core::ShortcutsFile result() const;
    // For tests / scripting
    bool setKey(int tab, int id, const core::KeyCombo& key);
    QStringList conflicts() const;   // "name (tab) <-> name (tab)"
    const std::vector<Row>& rows() const noexcept { return m_rows; }

private:
    void fill();
    void modify(int tab, int rowIndex);
    std::vector<size_t> rowsOfTab(int tab) const;
    QString keyText(const core::KeyCombo& k) const;

    core::ShortcutsFile m_file;
    std::vector<Row> m_rows;
    QTabWidget* m_tabs = nullptr;
    QTableWidget* m_tables[5] = {};
    QLineEdit* m_filter = nullptr;
    QLabel* m_info = nullptr;
};

} // namespace npp::ui
