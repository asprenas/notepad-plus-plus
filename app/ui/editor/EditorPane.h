// One editing view (upstream DocTabView + ScintillaEditView): a tab bar and ONE Scintilla editor
// whose document pointer is switched when the active tab changes, like upstream. Two panes
// (main / sub) make the split view.
#pragma once

#include "core/buffer/Buffer.h"
#include "editor/EditorAdapter.h"

#include <QTabBar>
#include <QWidget>

#include <map>
#include <memory>
#include <vector>

class ScintillaEditBase;

namespace npp::ui {

// Position of a document in a view (upstream Position / sessionFileInfo view data)
struct ViewState {
    intptr_t anchor = 0, caret = 0;
    intptr_t firstVisibleLine = 0, xOffset = 0;
    int selMode = 0;
};

class DocTabBar final : public QTabBar {
    Q_OBJECT
public:
    explicit DocTabBar(QWidget* parent = nullptr);
signals:
    void doubleClickedEmpty();
    void middleClicked(int index);
protected:
    void mouseDoubleClickEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
};

class EditorPane final : public QWidget {
    Q_OBJECT
public:
    explicit EditorPane(int viewIndex, QWidget* parent = nullptr);
    ~EditorPane() override;

    int viewIndex() const noexcept { return m_viewIndex; }
    ScintillaEditBase* editor() const noexcept { return m_editor; }
    const EditorAdapter& adapter() const noexcept { return m_adapter; }
    DocTabBar* tabBar() const noexcept { return m_tabs; }

    // Tabs
    int count() const { return static_cast<int>(m_buffers.size()); }
    core::BufferId bufferAt(int index) const;
    int indexOf(core::BufferId id) const;
    core::BufferId current() const;
    int currentIndex() const;
    // Adds a tab (does not activate unless activate == true).
    void addBuffer(const core::Buffer& b, bool activate);
    void removeBuffer(core::BufferId id);
    void activate(core::BufferId id);
    void setTabText(core::BufferId id, const QString& text, const QString& tooltip);
    const std::vector<core::BufferId>& buffers() const noexcept { return m_buffers; }

    ViewState viewState(core::BufferId id) const;
    void setViewState(core::BufferId id, const ViewState& s);

signals:
    void currentBufferChanged(core::BufferId id);       // after the document has been switched
    void closeRequested(core::BufferId id);
    void newDocumentRequested();
    void focused(int viewIndex);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void saveState(core::BufferId id);
    void restoreState(core::BufferId id);

    int m_viewIndex;
    DocTabBar* m_tabs = nullptr;
    ScintillaEditBase* m_editor = nullptr;
    EditorAdapter m_adapter;
    std::vector<core::BufferId> m_buffers; // tab order
    std::map<core::BufferId, intptr_t> m_documents;
    std::map<core::BufferId, ViewState> m_states;
    core::BufferId m_active = core::kInvalidBuffer;
    intptr_t m_emptyDocument = 0; // shown when no tab (keeps the view's own document)
    bool m_switching = false;
};

} // namespace npp::ui
