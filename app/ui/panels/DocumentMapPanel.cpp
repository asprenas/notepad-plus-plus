#include "DocumentMapPanel.h"

#include <Scintilla.h>
#include <ScintillaEditBase.h>

#include <QMouseEvent>
#include <QPainter>
#include <QVBoxLayout>
#include <QWheelEvent>

#include <algorithm>

namespace npp::ui {

class DocumentMapPanel::Zone final : public QWidget {
public:
    explicit Zone(QWidget* parent) : QWidget(parent)
    {
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setAttribute(Qt::WA_NoSystemBackground);
    }
    QRect zone;
    QColor colour{0xFF, 0x80, 0x00};

protected:
    void paintEvent(QPaintEvent*) override
    {
        if (zone.isEmpty())
            return;
        QPainter p(this);
        QColor fill = colour;
        fill.setAlpha(50); // upstream default document peeker transparency
        p.fillRect(zone, fill);
        p.setPen(colour);
        p.drawRect(zone.adjusted(0, 0, -1, -1));
    }
};

namespace {

using uptr = uintptr_t;

intptr_t send(ScintillaEditBase* e, unsigned int m, uptr w = 0, intptr_t l = 0) { return e->send(m, w, l); }

} // namespace

DocumentMapPanel::DocumentMapPanel(QWidget* parent) : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    m_map = new ScintillaEditBase(this);
    m_map->setObjectName("documentMapView");
    m_map->setFocusPolicy(Qt::NoFocus);
    layout->addWidget(m_map);

    send(m_map, SCI_SETZOOM, static_cast<uptr>(-10));
    for (int m = 0; m < 5; ++m)
        send(m_map, SCI_SETMARGINWIDTHN, static_cast<uptr>(m), 0);
    send(m_map, SCI_SETVSCROLLBAR, 0);
    send(m_map, SCI_SETHSCROLLBAR, 0);
    send(m_map, SCI_SETCARETSTYLE, CARETSTYLE_INVISIBLE);
    send(m_map, SCI_SETCARETLINEVISIBLE, 0);
    send(m_map, SCI_SETCURSOR, static_cast<uptr>(SC_CURSORARROW));
    send(m_map, SCI_USEPOPUP, SC_POPUP_NEVER);
    send(m_map, SCI_SETENDATLASTLINE, 0);

    m_overlay = new Zone(m_map);
    m_map->viewport()->installEventFilter(this);
    m_map->installEventFilter(this);
}

void DocumentMapPanel::setSource(ScintillaEditBase* editor, intptr_t document)
{
    m_editor = editor;
    if (document != m_document) {
        m_document = document;
        send(m_map, SCI_SETDOCPOINTER, 0, document); // 0: own empty document
    }
    refreshStyles();
    syncFolding();
    sync();
}

void DocumentMapPanel::refreshStyles()
{
    if (!m_editor)
        return;
    ScintillaEditBase* e = m_editor;
    for (uptr s = 0; s < 256; ++s) {
        send(m_map, SCI_STYLESETFORE, s, send(e, SCI_STYLEGETFORE, s));
        send(m_map, SCI_STYLESETBACK, s, send(e, SCI_STYLEGETBACK, s));
        send(m_map, SCI_STYLESETBOLD, s, send(e, SCI_STYLEGETBOLD, s));
        send(m_map, SCI_STYLESETITALIC, s, send(e, SCI_STYLEGETITALIC, s));
        send(m_map, SCI_STYLESETEOLFILLED, s, send(e, SCI_STYLEGETEOLFILLED, s));
        send(m_map, SCI_STYLESETSIZEFRACTIONAL, s, send(e, SCI_STYLEGETSIZEFRACTIONAL, s));
        send(m_map, SCI_STYLESETVISIBLE, s, send(e, SCI_STYLEGETVISIBLE, s));
        char font[128] = {};
        if (send(e, SCI_STYLEGETFONT, s, 0) < static_cast<intptr_t>(sizeof font)) {
            send(e, SCI_STYLEGETFONT, s, reinterpret_cast<intptr_t>(font));
            send(m_map, SCI_STYLESETFONT, s, reinterpret_cast<intptr_t>(font));
        }
    }
    send(m_map, SCI_SETTABWIDTH, static_cast<uptr>(send(e, SCI_GETTABWIDTH)));
    // overlay: orange on light themes, white on dark ones (Scintilla colours are 0xBBGGRR)
    const intptr_t bg = send(e, SCI_STYLEGETBACK, STYLE_DEFAULT);
    const int r = static_cast<int>(bg & 0xFF), g = static_cast<int>((bg >> 8) & 0xFF), b = static_cast<int>((bg >> 16) & 0xFF);
    m_overlay->colour = (r * 299 + g * 587 + b * 114) / 1000 < 128 ? QColor(Qt::white) : QColor(0xFF, 0x80, 0x00);
    m_overlay->update();
}

void DocumentMapPanel::syncFolding()
{
    if (!m_editor)
        return;
    ScintillaEditBase* e = m_editor;
    send(m_map, SCI_SETWRAPMODE, static_cast<uptr>(send(e, SCI_GETWRAPMODE)));
    const intptr_t lines = send(e, SCI_GETLINECOUNT);
    if (send(e, SCI_GETALLLINESVISIBLE)) {
        if (!send(m_map, SCI_GETALLLINESVISIBLE))
            send(m_map, SCI_SHOWLINES, 0, lines - 1);
        return;
    }
    for (intptr_t l = 0; l < lines;) {
        const bool visible = send(e, SCI_GETLINEVISIBLE, static_cast<uptr>(l));
        intptr_t end = l;
        while (end + 1 < lines && bool(send(e, SCI_GETLINEVISIBLE, static_cast<uptr>(end + 1))) == visible)
            ++end;
        send(m_map, visible ? SCI_SHOWLINES : SCI_HIDELINES, static_cast<uptr>(l), end);
        l = end + 1;
    }
}

void DocumentMapPanel::sync()
{
    if (!m_editor || !isVisible()) {
        m_zone = {};
        return;
    }
    ScintillaEditBase* e = m_editor;
    if (send(m_map, SCI_GETWRAPMODE) != send(e, SCI_GETWRAPMODE)
        || bool(send(m_map, SCI_GETALLLINESVISIBLE)) != bool(send(e, SCI_GETALLLINESVISIBLE)))
        syncFolding();

    const intptr_t firstDisplay = send(e, SCI_GETFIRSTVISIBLELINE);
    const intptr_t onScreen = std::max<intptr_t>(1, send(e, SCI_LINESONSCREEN));
    const intptr_t firstDoc = send(e, SCI_DOCLINEFROMVISIBLE, static_cast<uptr>(firstDisplay));
    const intptr_t lastDoc = send(e, SCI_DOCLINEFROMVISIBLE, static_cast<uptr>(firstDisplay + onScreen - 1));

    const intptr_t mapTop = send(m_map, SCI_VISIBLEFROMDOCLINE, static_cast<uptr>(firstDoc));
    const intptr_t lastLine = std::max<intptr_t>(0, send(m_map, SCI_GETLINECOUNT) - 1);
    intptr_t mapBottom = send(m_map, SCI_VISIBLEFROMDOCLINE, static_cast<uptr>(std::min(lastDoc, lastLine)))
                         + send(m_map, SCI_WRAPCOUNT, static_cast<uptr>(std::min(lastDoc, lastLine)));
    const intptr_t mapOnScreen = std::max<intptr_t>(1, send(m_map, SCI_LINESONSCREEN));
    const intptr_t mapTotal = send(m_map, SCI_VISIBLEFROMDOCLINE, static_cast<uptr>(lastLine))
                              + send(m_map, SCI_WRAPCOUNT, static_cast<uptr>(lastLine));
    const intptr_t editorTotal = std::max<intptr_t>(1, send(e, SCI_VISIBLEFROMDOCLINE, static_cast<uptr>(lastLine))
                                                           + send(e, SCI_WRAPCOUNT, static_cast<uptr>(lastLine)) - onScreen);
    // Map scrolled proportionally to the editor, so the zone reaches the map bottom at the document end.
    intptr_t mapFirst = 0;
    if (mapTotal > mapOnScreen)
        mapFirst = std::clamp<intptr_t>(firstDisplay * (mapTotal - mapOnScreen) / editorTotal, 0, mapTotal - mapOnScreen);
    send(m_map, SCI_SETFIRSTVISIBLELINE, static_cast<uptr>(mapFirst));
    mapFirst = send(m_map, SCI_GETFIRSTVISIBLELINE);

    const int h = static_cast<int>(send(m_map, SCI_TEXTHEIGHT, 0));
    const int top = static_cast<int>((mapTop - mapFirst) * h);
    const int bottom = static_cast<int>((mapBottom - mapFirst + 1) * h);
    m_zone = QRect(0, top, m_map->viewport()->width(), std::max(h, bottom - top));
    m_overlay->setGeometry(m_map->viewport()->geometry());
    m_overlay->zone = m_zone;
    m_overlay->raise();
    m_overlay->update();
}

void DocumentMapPanel::scrollEditorTo(int y)
{
    if (!m_editor)
        return;
    ScintillaEditBase* e = m_editor;
    const int h = std::max(1, static_cast<int>(send(m_map, SCI_TEXTHEIGHT, 0)));
    const intptr_t mapLine = send(m_map, SCI_GETFIRSTVISIBLELINE) + std::max(0, y) / h;
    const intptr_t docLine = send(m_map, SCI_DOCLINEFROMVISIBLE, static_cast<uptr>(mapLine));
    const intptr_t onScreen = send(e, SCI_LINESONSCREEN);
    const intptr_t display = send(e, SCI_VISIBLEFROMDOCLINE, static_cast<uptr>(docLine));
    send(e, SCI_SETFIRSTVISIBLELINE, static_cast<uptr>(std::max<intptr_t>(0, display - onScreen / 2)));
    sync();
}

bool DocumentMapPanel::eventFilter(QObject* o, QEvent* ev)
{
    if (o == m_map->viewport() || o == m_map) {
        switch (ev->type()) {
        case QEvent::MouseButtonPress:
        case QEvent::MouseButtonDblClick: {
            auto* me = static_cast<QMouseEvent*>(ev);
            if (me->button() == Qt::LeftButton) {
                m_dragging = true;
                scrollEditorTo(static_cast<int>(me->position().y()));
            }
            return true;
        }
        case QEvent::MouseMove:
            if (m_dragging)
                scrollEditorTo(static_cast<int>(static_cast<QMouseEvent*>(ev)->position().y()));
            return true;
        case QEvent::MouseButtonRelease:
            m_dragging = false;
            return true;
        case QEvent::Wheel:
            if (m_editor)
                QCoreApplication::sendEvent(m_editor->viewport(), ev);
            return true;
        case QEvent::KeyPress:
        case QEvent::KeyRelease:
        case QEvent::ContextMenu:
        case QEvent::DragEnter:
        case QEvent::Drop:
            return true;
        case QEvent::Resize:
            QMetaObject::invokeMethod(this, &DocumentMapPanel::sync, Qt::QueuedConnection);
            break;
        default:
            break;
        }
    }
    return QWidget::eventFilter(o, ev);
}

void DocumentMapPanel::resizeEvent(QResizeEvent* e)
{
    QWidget::resizeEvent(e);
    sync();
}

} // namespace npp::ui
