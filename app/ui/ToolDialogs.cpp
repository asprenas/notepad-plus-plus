// Small search / edit dialogs of phase 3: Go To (line / offset, upstream GoToLineDlg), Find
// characters in range (FindCharsInRangeDlg), Column editor (ColumnEditorDlg), incremental search
// bar (FindIncrementDlg) and "Find in these search results" (FindInFinderDlg).
#include "MainWindow.h"

#include "editor/EditorPane.h"
#include "menu/ActionRegistry.h"
#include "menu/CommandIds.h"
#include "search/FindReplaceDialog.h"
#include "search/SearchResultsPanel.h"

#include "core/editor/ColumnOps.h"
#include "core/search/Search.h"

#include <Scintilla.h>
#include <ScintillaEditBase.h>

#include <QApplication>
#include <QButtonGroup>
#include <QComboBox>
#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPushButton>
#include <QRadioButton>
#include <QSpinBox>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>

namespace npp::ui {

using namespace npp::core;
using namespace cmd;

namespace {

constexpr int kIncStyle = 28; // SCE_UNIVERSAL_FOUND_STYLE_INC

QString q(const std::string& s) { return QString::fromStdString(s); }

// upstream getNumericFieldValueFromText: number in the column editor's base, -1 if invalid
long long parseInBase(const QString& text, NumBase b)
{
    const int base = b == NumBase::Dec ? 10 : (b == NumBase::Hex || b == NumBase::HexUpper) ? 16 : b == NumBase::Oct ? 8 : 2;
    if (text.trimmed().isEmpty())
        return 0;
    bool ok = false;
    const qulonglong v = text.trimmed().toULongLong(&ok, base);
    return ok && v <= 0x7FFFFFFF ? static_cast<long long>(v) : -1;
}

} // namespace

// ------------------------------------------------------------------------------------------------
// Go To
// ------------------------------------------------------------------------------------------------

void MainWindow::showGoToDialog()
{
    ScintillaEditBase* e = currentPane()->editor();
    QDialog dlg(this);
    dlg.setWindowTitle(tr("Go To..."));
    auto* lineMode = new QRadioButton(tr("&Line"));
    auto* offsetMode = new QRadioButton(tr("&Offset"));
    (m_goToOffsetMode ? offsetMode : lineMode)->setChecked(true);
    auto* here = new QLabel, *max = new QLabel;
    auto* value = new QLineEdit;
    value->setValidator(nullptr);
    auto refresh = [&] {
        const bool line = lineMode->isChecked();
        const auto pos = e->send(SCI_GETCURRENTPOS);
        here->setText(QString::number(line ? e->send(SCI_LINEFROMPOSITION, static_cast<uptr_t>(pos)) + 1 : pos));
        max->setText(QString::number(line ? e->send(SCI_GETLINECOUNT) : e->send(SCI_GETLENGTH)));
    };
    connect(lineMode, &QRadioButton::toggled, &dlg, refresh);
    refresh();
    auto* form = new QFormLayout(&dlg);
    auto* modes = new QHBoxLayout;
    modes->addWidget(lineMode);
    modes->addWidget(offsetMode);
    form->addRow(modes);
    form->addRow(tr("You are here :"), here);
    form->addRow(tr("You want to go to :"), value);
    form->addRow(tr("You can't go further than :"), max);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("Go"));
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    if (dlg.exec() != QDialog::Accepted)
        return;
    m_goToOffsetMode = offsetMode->isChecked();
    bool ok = false;
    const long long n = value->text().trimmed().toLongLong(&ok);
    if (!ok || n < 0)
        return;
    goTo(n, m_goToOffsetMode);
}

void MainWindow::goTo(long long n, bool offset)
{
    ScintillaEditBase* e = currentPane()->editor();
    if (!offset) {
        e->send(SCI_ENSUREVISIBLE, static_cast<uptr_t>(n - 1));
        e->send(SCI_GOTOLINE, static_cast<uptr_t>(n - 1));
    } else {
        intptr_t pos = 0;
        if (n > 0) // not into a multi-byte character or a CR/LF pair
            pos = e->send(SCI_POSITIONAFTER, static_cast<uptr_t>(e->send(SCI_POSITIONBEFORE, static_cast<uptr_t>(n))));
        e->send(SCI_ENSUREVISIBLE, static_cast<uptr_t>(e->send(SCI_LINEFROMPOSITION, static_cast<uptr_t>(pos))));
        e->send(SCI_GOTOPOS, static_cast<uptr_t>(pos));
    }
    e->setFocus();
}

// ------------------------------------------------------------------------------------------------
// Find characters in range
// ------------------------------------------------------------------------------------------------

void MainWindow::showFindCharsInRange()
{
    if (!m_charRangeDialog) {
        auto* dlg = new QDialog(this);
        m_charRangeDialog = dlg;
        dlg->setWindowTitle(tr("Find characters in range..."));
        auto* nonAscii = new QRadioButton(tr("Non-ASCII Characters (128-255)"));
        auto* ascii = new QRadioButton(tr("ASCII Characters (0-127)"));
        auto* custom = new QRadioButton(tr("My Range:"));
        nonAscii->setChecked(true);
        auto* from = new QSpinBox, *to = new QSpinBox;
        from->setRange(0, 255);
        to->setRange(0, 255);
        to->setValue(255);
        auto* up = new QRadioButton(tr("Up")), *down = new QRadioButton(tr("Down"));
        down->setChecked(true);
        auto* wrap = new QCheckBox(tr("Wrap around"));
        wrap->setChecked(true);
        auto* findBtn = new QPushButton(tr("Find"));
        auto* close = new QPushButton(tr("Close"));
        auto* grid = new QGridLayout(dlg);
        grid->addWidget(nonAscii, 0, 0, 1, 3);
        grid->addWidget(ascii, 1, 0, 1, 3);
        grid->addWidget(custom, 2, 0);
        grid->addWidget(from, 2, 1);
        grid->addWidget(to, 2, 2);
        auto* dirBox = new QGroupBox(tr("Direction"));
        auto* dirLayout = new QHBoxLayout(dirBox);
        dirLayout->addWidget(up);
        dirLayout->addWidget(down);
        grid->addWidget(dirBox, 3, 0, 1, 2);
        grid->addWidget(wrap, 3, 2);
        grid->addWidget(findBtn, 0, 3);
        grid->addWidget(close, 1, 3);
        connect(close, &QPushButton::clicked, dlg, &QDialog::hide);
        connect(findBtn, &QPushButton::clicked, this, [this, nonAscii, ascii, from, to, down, wrap] {
            unsigned char b = 128, en = 255;
            if (ascii->isChecked()) {
                b = 0;
                en = 127;
            } else if (!nonAscii->isChecked()) {
                b = static_cast<unsigned char>(std::min(from->value(), to->value()));
                en = static_cast<unsigned char>(std::max(from->value(), to->value()));
            }
            const IEditor& e = currentPane()->adapter();
            const intptr_t start = down->isChecked() ? e.send(SCI_GETSELECTIONEND) : e.send(SCI_GETSELECTIONSTART);
            if (!findCharInRange(e, b, en, start, down->isChecked(), wrap->isChecked()))
                QApplication::beep();
        });
    }
    m_charRangeDialog->show();
    m_charRangeDialog->raise();
    m_charRangeDialog->activateWindow();
}

// ------------------------------------------------------------------------------------------------
// Column editor
// ------------------------------------------------------------------------------------------------

void MainWindow::showColumnEditor()
{
    QDialog dlg(this);
    dlg.setWindowTitle(tr("Column / Multi-Selection Editor"));
    auto* textMode = new QRadioButton(tr("Text to Insert"));
    auto* numMode = new QRadioButton(tr("Number to Insert"));
    (m_columnEditorNumbers ? numMode : textMode)->setChecked(true);
    auto* text = new QLineEdit;
    auto* initial = new QLineEdit(QStringLiteral("1")), *increase = new QLineEdit(QStringLiteral("1")), *repeat = new QLineEdit(QStringLiteral("1"));
    auto* leading = new QComboBox;
    leading->addItems({tr("None"), tr("Zeros"), tr("Spaces")});
    auto* dec = new QRadioButton(tr("Dec")), *hex = new QRadioButton(tr("Hex")), *oct = new QRadioButton(tr("Oct")), *bin = new QRadioButton(tr("Bin"));
    auto* upper = new QCheckBox(tr("Uppercase hex"));
    dec->setChecked(true);
    auto* form = new QFormLayout(&dlg);
    form->addRow(textMode);
    form->addRow(text);
    form->addRow(numMode);
    form->addRow(tr("Initial number :"), initial);
    form->addRow(tr("Increase by :"), increase);
    form->addRow(tr("Repeat :"), repeat);
    form->addRow(tr("Leading :"), leading);
    auto* formats = new QHBoxLayout;
    for (QWidget* w : std::initializer_list<QWidget*>{dec, hex, oct, bin, upper})
        formats->addWidget(w);
    form->addRow(tr("Format"), formats);
    auto* status = new QLabel;
    form->addRow(status);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, [&] {
        const IEditor& e = currentPane()->adapter();
        if (textMode->isChecked()) {
            dlg.accept();
            columnInsertText(e, text->text().toStdString());
            return;
        }
        const NumBase base = hex->isChecked() ? (upper->isChecked() ? NumBase::HexUpper : NumBase::Hex)
                             : oct->isChecked() ? NumBase::Oct : bin->isChecked() ? NumBase::Bin : NumBase::Dec;
        const long long i = parseInBase(initial->text(), base), inc = parseInBase(increase->text(), base), rep = parseInBase(repeat->text(), base);
        if (i < 0 || inc < 0 || rep < 0) {
            status->setText(tr("Invalid number for the selected format."));
            return;
        }
        dlg.accept();
        columnInsertNumbers(e, static_cast<size_t>(i), static_cast<size_t>(inc), static_cast<size_t>(rep), base,
                            static_cast<Leading>(leading->currentIndex()));
    });
    dlg.exec();
    m_columnEditorNumbers = numMode->isChecked();
    currentPane()->editor()->setFocus();
}

// ------------------------------------------------------------------------------------------------
// Incremental search (FindIncrementDlg): typing searches from the start of the selection
// ------------------------------------------------------------------------------------------------

void MainWindow::showIncrementalSearch()
{
    if (!m_incBar) {
        m_incBar = new QToolBar(tr("Incremental Search"), this);
        m_incBar->setObjectName(QStringLiteral("incrementalSearch"));
        addToolBar(Qt::BottomToolBarArea, m_incBar);
        auto* close = new QToolButton;
        close->setText(QStringLiteral("✕"));
        m_incBar->addWidget(close);
        m_incBar->addWidget(new QLabel(tr("Find :")));
        m_incEdit = new QLineEdit;
        m_incEdit->setMinimumWidth(240);
        m_incBar->addWidget(m_incEdit);
        auto* prev = new QToolButton, *next = new QToolButton;
        prev->setText(QStringLiteral("<"));
        next->setText(QStringLiteral(">"));
        m_incBar->addWidget(prev);
        m_incBar->addWidget(next);
        m_incHighlight = new QCheckBox(tr("Highlight all"));
        m_incMatchCase = new QCheckBox(tr("Match case"));
        m_incBar->addWidget(m_incHighlight);
        m_incBar->addWidget(m_incMatchCase);
        m_incStatus = new QLabel;
        m_incBar->addWidget(m_incStatus);
        connect(close, &QToolButton::clicked, this, [this] { hideIncrementalSearch(); });
        connect(m_incEdit, &QLineEdit::textEdited, this, [this] { incrementalSearch(IncFirst, true); });
        connect(next, &QToolButton::clicked, this, [this] { incrementalSearch(IncNext, true); });
        connect(prev, &QToolButton::clicked, this, [this] { incrementalSearch(IncNext, false); });
        connect(m_incHighlight, &QCheckBox::toggled, this, [this] { incrementalSearch(IncHighlightOnly, true); });
        connect(m_incMatchCase, &QCheckBox::toggled, this, [this] { incrementalSearch(IncFirst, true); });
        m_incEdit->installEventFilter(this);
    }
    m_incBar->show();
    m_incEdit->setFocus();
    m_incEdit->selectAll();
}

void MainWindow::hideIncrementalSearch()
{
    if (!m_incBar)
        return;
    ScintillaEditBase* e = currentPane()->editor();
    e->send(SCI_SETINDICATORCURRENT, kIncStyle);
    e->send(SCI_INDICATORCLEARRANGE, 0, e->send(SCI_GETLENGTH));
    m_incBar->hide();
    e->setFocus();
}

bool MainWindow::eventFilter(QObject* o, QEvent* ev)
{
    if (o == m_incEdit && ev->type() == QEvent::KeyPress) {
        auto* k = static_cast<QKeyEvent*>(ev);
        if (k->key() == Qt::Key_Return || k->key() == Qt::Key_Enter) {
            incrementalSearch(IncNext, !(k->modifiers() & Qt::ShiftModifier));
            return true;
        }
        if (k->key() == Qt::Key_Escape) {
            hideIncrementalSearch();
            return true;
        }
    }
    return QMainWindow::eventFilter(o, ev);
}

void MainWindow::incrementalSearch(IncType type, bool down)
{
    ScintillaEditBase* e = currentPane()->editor();
    const IEditor& ed = currentPane()->adapter();
    SearchOptions o;
    o.findWhat = m_incEdit->text().toStdString();
    o.matchCase = m_incMatchCase->isChecked();
    o.directionDown = down;
    o.wrap = true;
    e->send(SCI_SETINDICATORCURRENT, kIncStyle);
    e->send(SCI_INDICATORCLEARRANGE, 0, e->send(SCI_GETLENGTH));
    if (o.findWhat.empty()) {
        m_incStatus->clear();
        e->send(SCI_SETSEL, static_cast<uptr_t>(e->send(SCI_GETSELECTIONSTART)), e->send(SCI_GETSELECTIONSTART));
        return;
    }
    if (type != IncHighlightOnly) {
        // first incremental: from the start of the current selection (the match grows as you type)
        const intptr_t from = type == IncFirst ? e->send(SCI_GETSELECTIONSTART)
                                               : (down ? e->send(SCI_GETSELECTIONEND) : e->send(SCI_GETSELECTIONSTART));
        const auto m = findNext(ed, o, from);
        if (m) {
            e->send(SCI_ENSUREVISIBLE, static_cast<uptr_t>(e->send(SCI_LINEFROMPOSITION, static_cast<uptr_t>(m->start))));
            e->send(SCI_SETSEL, static_cast<uptr_t>(m->start), m->end);
            e->send(SCI_SCROLLRANGE, static_cast<uptr_t>(m->end), m->start);
            m_incStatus->setText(m->wrapped ? tr("Reached the end of the document, continued from the start") : QString());
            m_incEdit->setStyleSheet(QString());
        } else {
            m_incStatus->setText(tr("Phrase not found"));
            m_incEdit->setStyleSheet(QStringLiteral("background: #ffc0c0;"));
        }
    }
    if (m_incHighlight->isChecked()) {
        const auto all = findAll(ed, o);
        e->send(SCI_SETINDICATORCURRENT, kIncStyle);
        for (const SearchMatch& m : all)
            e->send(SCI_INDICATORFILLRANGE, static_cast<uptr_t>(m.start), m.end - m.start);
        m_incStatus->setText(tr("%1 matches").arg(all.size()));
    }
}

// ------------------------------------------------------------------------------------------------
// Commands
// ------------------------------------------------------------------------------------------------

void MainWindow::registerToolDialogs()
{
    ActionRegistry* r = m_actions;
    r->setHandler(IDM_SEARCH_GOTOLINE, {[this] { showGoToDialog(); }});
    r->setHandler(IDM_SEARCH_FINDCHARINRANGE, {[this] { showFindCharsInRange(); }});
    r->setHandler(IDM_EDIT_COLUMNMODE, {[this] { showColumnEditor(); }, [this] { return currentPane()->editor()->send(SCI_GETREADONLY) == 0; }});
    r->setHandler(IDM_SEARCH_FINDINCREMENT, {[this] { showIncrementalSearch(); }});
}

} // namespace npp::ui
