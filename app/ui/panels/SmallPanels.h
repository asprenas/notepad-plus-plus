// Character Panel (upstream AnsiCharPanel: ASCII / ANSI table with HTML entities) and
// Clipboard History panel (upstream ClipboardHistoryPanel).
#pragma once

#include <QStringList>
#include <QWidget>

class QListWidget;
class QTableWidget;

namespace npp::ui {

class CharacterPanel final : public QWidget {
    Q_OBJECT
public:
    explicit CharacterPanel(QWidget* parent = nullptr);
    // Code page of the document (Windows code page number; the table and the inserted text follow it).
    void setCodePage(int codePage);
    int codePage() const noexcept { return m_codePage; }
    QTableWidget* table() const { ensureFilled(); return m_table; }
    // UTF-8 text inserted for a cell (column 2 = the character itself, others = the cell text).
    QByteArray textFor(int row, int column) const;

signals:
    void insertRequested(const QByteArray& utf8);

protected:
    void showEvent(QShowEvent* e) override;

private:
    // The 256 rows are filled when the panel is shown or read (start-up time: ~120 ms)
    void ensureFilled() const;
    void fill();
    QTableWidget* m_table = nullptr;
    int m_codePage = -2;
    int m_filledFor = -3;
};

class ClipboardHistoryPanel final : public QWidget {
    Q_OBJECT
public:
    explicit ClipboardHistoryPanel(QWidget* parent = nullptr);
    void addEntry(const QString& text); // newest first, duplicates moved to the top
    const QStringList& entries() const noexcept { return m_entries; }
    QListWidget* list() const noexcept { return m_list; }

signals:
    void insertRequested(const QString& text);

private:
    QListWidget* m_list = nullptr;
    QStringList m_entries;
};

} // namespace npp::ui
