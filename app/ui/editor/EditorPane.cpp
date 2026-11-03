#include "EditorPane.h"

#include <Scintilla.h>
#include <ScintillaEditBase.h>

#include <QEvent>
#include <QMouseEvent>
#include <QVBoxLayout>

namespace npp::ui {

DocTabBar::DocTabBar(QWidget* parent) : QTabBar(parent)
{
    setDocumentMode(true);
    setMovable(true);
    setTabsClosable(true);
    setExpanding(false);
    setUsesScrollButtons(true);
    setElideMode(Qt::ElideNone);
}

void DocTabBar::mouseDoubleClickEvent(QMouseEvent* e)
{
    if (tabAt(e->position().toPoint()) < 0)
        emit doubleClickedEmpty();
    QTabBar::mouseDoubleClickEvent(e);
}

void DocTabBar::mouseReleaseEvent(QMouseEvent* e)
{
    if (e->button() == Qt::MiddleButton) {
        const int i = tabAt(e->position().toPoint());
        if (i >= 0)
            emit middleClicked(i);
    }
    QTabBar::mouseReleaseEvent(e);
}

EditorPane::EditorPane(int viewIndex, QWidget* parent)
    : QWidget(parent), m_viewIndex(viewIndex), m_tabs(new DocTabBar(this)), m_editor(new ScintillaEditBase(this)),
      m_adapter(m_editor)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(m_tabs);
    layout->addWidget(m_editor, 1);

    m_emptyDocument = m_editor->send(SCI_GETDOCPOINTER);
    m_editor->send(SCI_ADDREFDOCUMENT, 0, m_emptyDocument);
    m_editor->installEventFilter(this);

    connect(m_tabs, &QTabBar::currentChanged, this, [this](int index) {
        if (m_switching || index < 0 || index >= count())
            return;
        activate(bufferAt(index));
    });
    connect(m_tabs, &QTabBar::tabMoved, this, [this](int from, int to) {
        const auto id = m_buffers[static_cast<size_t>(from)];
        m_buffers.erase(m_buffers.begin() + from);
        m_buffers.insert(m_buffers.begin() + to, id);
    });
    connect(m_tabs, &QTabBar::tabCloseRequested, this, [this](int index) { emit closeRequested(bufferAt(index)); });
    connect(m_tabs, &DocTabBar::middleClicked, this, [this](int index) { emit closeRequested(bufferAt(index)); });
    connect(m_tabs, &DocTabBar::doubleClickedEmpty, this, &EditorPane::newDocumentRequested);
}

EditorPane::~EditorPane()
{
    m_editor->send(SCI_SETDOCPOINTER, 0, m_emptyDocument);
    m_editor->send(SCI_RELEASEDOCUMENT, 0, m_emptyDocument);
}

bool EditorPane::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_editor && event->type() == QEvent::FocusIn)
        emit focused(m_viewIndex);
    return QWidget::eventFilter(watched, event);
}

core::BufferId EditorPane::bufferAt(int index) const
{
    return (index >= 0 && index < count()) ? m_buffers[static_cast<size_t>(index)] : core::kInvalidBuffer;
}

int EditorPane::indexOf(core::BufferId id) const
{
    for (int i = 0; i < count(); ++i)
        if (m_buffers[static_cast<size_t>(i)] == id)
            return i;
    return -1;
}

core::BufferId EditorPane::current() const { return m_active; }
int EditorPane::currentIndex() const { return indexOf(m_active); }

void EditorPane::addBuffer(const core::Buffer& b, bool activateIt)
{
    if (indexOf(b.id) >= 0) {
        if (activateIt)
            activate(b.id);
        return;
    }
    m_buffers.push_back(b.id);
    m_documents[b.id] = b.document;
    m_editor->send(SCI_ADDREFDOCUMENT, 0, b.document); // the view keeps the document alive
    m_switching = true;
    m_tabs->addTab(QString::fromStdString(b.displayName()));
    m_switching = false;
    if (activateIt || count() == 1)
        activate(b.id);
}

void EditorPane::removeBuffer(core::BufferId id)
{
    const int index = indexOf(id);
    if (index < 0)
        return;
    const bool wasActive = id == m_active;
    if (wasActive) {
        m_editor->send(SCI_SETDOCPOINTER, 0, m_emptyDocument);
        m_active = core::kInvalidBuffer;
    }
    m_buffers.erase(m_buffers.begin() + index);
    m_editor->send(SCI_RELEASEDOCUMENT, 0, m_documents[id]);
    m_documents.erase(id);
    m_states.erase(id);
    m_switching = true;
    m_tabs->removeTab(index);
    m_switching = false;
    if (wasActive && count() > 0)
        activate(bufferAt(std::min(index, count() - 1)));
    else if (wasActive)
        emit currentBufferChanged(core::kInvalidBuffer);
}

void EditorPane::saveState(core::BufferId id)
{
    if (id == core::kInvalidBuffer)
        return;
    ViewState s;
    s.anchor = m_editor->send(SCI_GETANCHOR);
    s.caret = m_editor->send(SCI_GETCURRENTPOS);
    s.firstVisibleLine = m_editor->send(SCI_DOCLINEFROMVISIBLE, m_editor->send(SCI_GETFIRSTVISIBLELINE));
    s.xOffset = m_editor->send(SCI_GETXOFFSET);
    s.selMode = static_cast<int>(m_editor->send(SCI_GETSELECTIONMODE));
    m_states[id] = s;
}

void EditorPane::restoreState(core::BufferId id)
{
    const auto it = m_states.find(id);
    const ViewState s = it == m_states.end() ? ViewState{} : it->second;
    m_editor->send(SCI_SETSELECTIONMODE, static_cast<uintptr_t>(s.selMode));
    m_editor->send(SCI_SETSEL, static_cast<uintptr_t>(s.anchor), s.caret);
    m_editor->send(SCI_SETFIRSTVISIBLELINE, static_cast<uintptr_t>(m_editor->send(SCI_VISIBLEFROMDOCLINE, s.firstVisibleLine)));
    m_editor->send(SCI_SETXOFFSET, static_cast<uintptr_t>(s.xOffset));
}

void EditorPane::activate(core::BufferId id)
{
    const int index = indexOf(id);
    if (index < 0)
        return;
    if (id != m_active) {
        saveState(m_active);
        m_editor->send(SCI_SETDOCPOINTER, 0, m_documents[id]);
        m_active = id;
        restoreState(id);
    }
    m_switching = true;
    m_tabs->setCurrentIndex(index);
    m_switching = false;
    emit currentBufferChanged(id);
}

void EditorPane::setTabText(core::BufferId id, const QString& text, const QString& tooltip)
{
    const int index = indexOf(id);
    if (index < 0)
        return;
    m_tabs->setTabText(index, text);
    m_tabs->setTabToolTip(index, tooltip);
}

ViewState EditorPane::viewState(core::BufferId id) const
{
    if (id == m_active) {
        auto* self = const_cast<EditorPane*>(this);
        self->saveState(id);
    }
    const auto it = m_states.find(id);
    return it == m_states.end() ? ViewState{} : it->second;
}

void EditorPane::setViewState(core::BufferId id, const ViewState& s)
{
    m_states[id] = s;
    if (id == m_active)
        restoreState(id);
}

} // namespace npp::ui
