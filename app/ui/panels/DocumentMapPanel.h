// Document Map panel (upstream WinControls/DocumentMap): a zoomed out second Scintilla view on the
// document of the active editor, with the area visible in the editor drawn over it. Click / drag
// scrolls the editor; wrapping and hidden (folded) lines follow the editor.
#pragma once

#include <QPointer>
#include <QWidget>

class ScintillaEditBase;

namespace npp::ui {

class DocumentMapPanel final : public QWidget {
    Q_OBJECT
public:
    explicit DocumentMapPanel(QWidget* parent = nullptr);

    // Shows `document` (Scintilla document pointer) of `editor`; styles are copied from the editor.
    void setSource(ScintillaEditBase* editor, intptr_t document);
    // Follows the editor's scroll position / wrapping / folding (cheap, call on every editor update).
    void sync();
    // Re-copies the editor styles (theme change).
    void refreshStyles();

    ScintillaEditBase* map() const noexcept { return m_map; }
    // Visible area rectangle in map coordinates (tests).
    QRect viewZone() const noexcept { return m_zone; }
    // Scrolls the editor so that the map line at y (map widget coordinates) is centered.
    void scrollEditorTo(int y);

protected:
    bool eventFilter(QObject* o, QEvent* e) override;
    void resizeEvent(QResizeEvent* e) override;

private:
    void syncFolding();
    class Zone;
    ScintillaEditBase* m_map = nullptr;
    Zone* m_overlay = nullptr;
    QPointer<ScintillaEditBase> m_editor;
    intptr_t m_document = 0;
    QRect m_zone;
    bool m_dragging = false;
};

} // namespace npp::ui
