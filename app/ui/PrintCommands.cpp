// File > Print... / Print Now (upstream ScintillaComponent/Printer.cpp) and print preview:
// pages rendered by Scintilla SCI_FORMATRANGEFULL on the printer (the Qt platform layer paints
// with the active QPainter of the device), header / footer from config.xml <GUIConfig name="Print">.
#include "MainWindow.h"

#include "editor/EditorPane.h"
#include "menu/ActionRegistry.h"
#include "menu/CommandIds.h"

#include "core/run/RunCommand.h"

#include <Scintilla.h>
#include <ScintillaEditBase.h>

#include <QDateTime>
#include <QFileInfo>
#include <QLocale>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QPrintDialog>
#include <QPrinter>
#include <QPrintPreviewDialog>

namespace npp::ui {

using namespace npp::core;
using namespace cmd;

namespace {

QString q(const std::string& s) { return QString::fromStdString(s); }

constexpr int kFontStyleBold = 1, kFontStyleItalic = 2; // upstream FONTSTYLE_*
constexpr int kMarginLineNumber = 0;

QFont printFont(const std::string& name, int size, int style)
{
    QFont f(name.empty() ? QStringLiteral("Arial") : q(name));
    f.setPointSize(size ? size : 9);
    f.setBold(style & kFontStyleBold);
    f.setItalic(style & kFontStyleItalic);
    return f;
}

} // namespace

QString MainWindow::expandPrintVariables(const std::string& text)
{
    if (text.empty())
        return {};
    // upstream: $(SHORT_DATE) / $(LONG_DATE) / $(TIME), then the Run variables
    const QDateTime now = QDateTime::currentDateTime();
    const QLocale loc;
    QString s = q(text);
    s.replace(QStringLiteral("$(SHORT_DATE)"), loc.toString(now.date(), QLocale::ShortFormat));
    s.replace(QStringLiteral("$(LONG_DATE)"), loc.toString(now.date(), QLocale::LongFormat));
    s.replace(QStringLiteral("$(TIME)"), loc.toString(now.time(), QLocale::ShortFormat));
    return q(expandRunVariables(s.toStdString(), runVariables()));
}

int MainWindow::printDocument(QPrinter& printer, bool selectionOnly)
{
    ScintillaEditBase* e = currentPane()->editor();
    const GuiConfig& g = m_config.gui;
    if (g.printMarginLeft || g.printMarginTop || g.printMarginRight || g.printMarginBottom)
        printer.setPageMargins(QMarginsF(g.printMarginLeft, g.printMarginTop, g.printMarginRight, g.printMarginBottom),
                               QPageLayout::Millimeter);
    if (Buffer* b = currentBuffer())
        printer.setDocName(b->isUntitled() ? q(b->displayName()) : q(b->path));

    QPainter painter;
    if (!painter.begin(&printer))
        return -1;

    const QFont headerFont = printFont(g.printHeaderFontName, g.printHeaderFontSize, g.printHeaderFontStyle);
    const QFont footerFont = printFont(g.printFooterFontName, g.printFooterFontSize, g.printFooterFontStyle);
    const int headerLineHeight = QFontMetrics(headerFont, &printer).height();
    const int footerLineHeight = QFontMetrics(footerFont, &printer).height();
    const int printMarge = footerLineHeight + footerLineHeight / 2;

    const QRect area(QPoint(0, 0), printer.pageLayout().paintRectPixels(printer.resolution()).size());
    QRect rc = area.adjusted(printMarge, printMarge, -printMarge, -printMarge);

    const QString hl = expandPrintVariables(g.printHeaderLeft), hm = expandPrintVariables(g.printHeaderMiddle),
                  hr = expandPrintVariables(g.printHeaderRight);
    const QString fl = expandPrintVariables(g.printFooterLeft), fm = expandPrintVariables(g.printFooterMiddle),
                  fr = expandPrintVariables(g.printFooterRight);
    const bool header = !(hl.isEmpty() && hm.isEmpty() && hr.isEmpty());
    const bool footer = !(fl.isEmpty() && fm.isEmpty() && fr.isEmpty());
    if (header)
        rc.setTop(rc.top() + headerLineHeight + headerLineHeight / 2);
    if (footer)
        rc.setBottom(rc.bottom() - (footerLineHeight + footerLineHeight / 2));

    // range: whole document or the selection
    intptr_t printed = 0, end = e->send(SCI_GETLENGTH);
    if (selectionOnly) {
        printed = e->send(SCI_GETSELECTIONSTART);
        end = e->send(SCI_GETSELECTIONEND);
    }

    const auto lineNumberWidth = e->send(SCI_GETMARGINWIDTHN, kMarginLineNumber);
    if (!g.printLineNumber)
        e->send(SCI_SETMARGINWIDTHN, kMarginLineNumber, 0);
    e->send(SCI_SETPRINTCOLOURMODE, static_cast<uptr_t>(g.printOption));

    const int fromPage = printer.fromPage(), toPage = printer.toPage();
    const bool pageRange = printer.printRange() == QPrinter::PageRange && fromPage > 0;
    auto drawParts = [&painter](const QFont& font, const QRect& box, const QString& l, const QString& m, const QString& r,
                                const QString& page) {
        painter.setClipping(false);
        painter.setFont(font);
        painter.setPen(Qt::black);
        auto withPage = [&page](QString s) { return s.replace(QStringLiteral("$(CURRENT_PRINTING_PAGE)"), page); };
        if (!l.isEmpty())
            painter.drawText(box.adjusted(5, 0, 0, 0), Qt::AlignLeft | Qt::AlignVCenter, withPage(l));
        if (!m.isEmpty())
            painter.drawText(box, Qt::AlignHCenter | Qt::AlignVCenter, withPage(m));
        if (!r.isEmpty())
            painter.drawText(box, Qt::AlignRight | Qt::AlignVCenter, withPage(r));
    };

    int pageNum = 1, pagesOut = 0;
    while (printed < end) {
        const bool printPage = !pageRange || (pageNum >= fromPage && pageNum <= toPage);
        const QString pageString = QString::number(pageNum);
        if (printPage) {
            if (pagesOut > 0)
                printer.newPage();
            ++pagesOut;
            if (header) {
                const QRect box(rc.left(), rc.top() - headerLineHeight - headerLineHeight / 2, rc.width(), headerLineHeight);
                drawParts(headerFont, box, hl, hm, hr, pageString);
                painter.drawLine(rc.left(), rc.top() - headerLineHeight / 4, rc.right(), rc.top() - headerLineHeight / 4);
            }
        }
        Sci_RangeToFormatFull frm{};
        frm.hdc = static_cast<QPaintDevice*>(&printer);
        frm.hdcTarget = static_cast<QPaintDevice*>(&printer);
        frm.rc = {rc.left(), rc.top(), rc.right(), rc.bottom()};
        frm.rcPage = {area.left(), area.top(), area.right(), area.bottom()};
        frm.chrg.cpMin = printed;
        frm.chrg.cpMax = end;
        intptr_t formFeed = -1;
        if (g.printFormFeedPageBreak) { // page break before a form feed character
            Sci_TextToFindFull ttf{};
            ttf.chrg = frm.chrg;
            ttf.lpstrText = "\f";
            formFeed = e->send(SCI_FINDTEXTFULL, SCFIND_NONE, reinterpret_cast<intptr_t>(&ttf));
            if (formFeed > 0)
                frm.chrg.cpMax = formFeed - 1;
            else if (formFeed == 0)
                frm.chrg.cpMax = 0;
        }
        const intptr_t next = e->send(SCI_FORMATRANGEFULL, printPage, reinterpret_cast<intptr_t>(&frm));
        printed = (formFeed != -1 && next <= formFeed) ? formFeed + 1 : std::max(next, printed + 1);
        if (printPage && footer) {
            const QRect box(rc.left(), rc.bottom() + footerLineHeight / 2, rc.width(), footerLineHeight);
            painter.setClipping(false);
            painter.setPen(Qt::black);
            painter.drawLine(rc.left(), rc.bottom() + footerLineHeight / 4, rc.right(), rc.bottom() + footerLineHeight / 4);
            drawParts(footerFont, box, fl, fm, fr, pageString);
        }
        ++pageNum;
        if (pageRange && pageNum > toPage)
            break;
    }
    e->send(SCI_FORMATRANGEFULL, 0, 0); // releases Scintilla's cached layout of the printer
    painter.end();
    if (!g.printLineNumber)
        e->send(SCI_SETMARGINWIDTHN, kMarginLineNumber, lineNumberWidth);
    return pagesOut;
}

void MainWindow::registerPrintCommands()
{
    ActionRegistry* r = m_actions;
    r->setHandler(IDM_FILE_PRINT, {[this] {
        QPrinter printer(QPrinter::HighResolution);
        QPrintDialog dlg(&printer, this);
        ScintillaEditBase* e = currentPane()->editor();
        const bool hasSelection = !e->send(SCI_GETSELECTIONEMPTY);
        dlg.setOption(QAbstractPrintDialog::PrintSelection, hasSelection);
        dlg.setOption(QAbstractPrintDialog::PrintPageRange, true);
        if (hasSelection)
            printer.setPrintRange(QPrinter::Selection);
        if (dlg.exec() != QDialog::Accepted)
            return;
        if (printDocument(printer, printer.printRange() == QPrinter::Selection) < 0) {
            const auto t = m_nativeLang.messageBox("PrintError", "Cannot start printer document.", "");
            QMessageBox::warning(this, q(t.title), q(t.message));
        }
    }});
    r->setHandler(IDM_FILE_PRINTNOW, {[this] { // default printer, no dialog
        QPrinter printer(QPrinter::HighResolution);
        if (printDocument(printer, false) < 0) {
            const auto t = m_nativeLang.messageBox("PrintError", "Cannot start printer document.", "");
            QMessageBox::warning(this, q(t.title), q(t.message));
        }
    }});
}

void MainWindow::showPrintPreview()
{
    QPrinter printer(QPrinter::HighResolution);
    QPrintPreviewDialog dlg(&printer, this);
    dlg.setObjectName(QStringLiteral("printPreview"));
    connect(&dlg, &QPrintPreviewDialog::paintRequested, this, [this](QPrinter* p) { printDocument(*p, false); });
    dlg.resize(900, 700);
    dlg.exec();
}

void MainWindow::addPrintPreviewAction()
{
    // Not in the upstream menu: File > Print Preview... after Print Now
    QAction* now = m_actions->action(IDM_FILE_PRINTNOW);
    QMenu* menu = nullptr;
    for (QObject* o : now->associatedObjects())
        if ((menu = qobject_cast<QMenu*>(o)))
            break;
    if (!menu)
        return;
    const QList<QAction*> acts = menu->actions();
    const int i = static_cast<int>(acts.indexOf(now));
    auto* preview = new QAction(tr("Print Pre&view..."), menu);
    preview->setObjectName(QStringLiteral("printPreviewAction"));
    connect(preview, &QAction::triggered, this, &MainWindow::showPrintPreview);
    menu->insertAction(i + 1 < acts.size() ? acts[i + 1] : nullptr, preview);
}

} // namespace npp::ui
