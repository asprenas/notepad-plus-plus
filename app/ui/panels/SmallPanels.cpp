#include "SmallPanels.h"

#include "core/encoding/Codepage.h"

#include <QApplication>
#include <QClipboard>
#include <QHeaderView>
#include <QKeyEvent>
#include <QListWidget>
#include <QMimeData>
#include <QShowEvent>
#include <QTableWidget>
#include <QVBoxLayout>

namespace npp::ui {

namespace {

const char* const kControlNames[33] = {"NULL", "SOH", "STX", "ETX", "EOT", "ENQ", "ACK", "BEL", "BS",  "TAB", "LF",
                                       "VT",   "FF",  "CR",  "SO",  "SI",  "DLE", "DC1", "DC2", "DC3", "DC4", "NAK",
                                       "SYN",  "ETB", "CAN", "EM",  "SUB", "ESC", "FS",  "GS",  "RS",  "US",  "Space"};

const char* htmlName(int v)
{
    switch (v) {
#define NPP_HTML_NAME(value, name) case value: return name;
#include "HtmlCharNames.inc"
#undef NPP_HTML_NAME
    default:
        return "";
    }
}

// upstream AsciiListView::getHtmlNumber (Windows-1252 characters outside Latin-1, and '-')
int htmlNumber(int v)
{
    switch (v) {
    case 45: return 8722;
    case 128: return 8364;
    case 130: return 8218;
    case 131: return 402;
    case 132: return 8222;
    case 133: return 8230;
    case 134: return 8224;
    case 135: return 8225;
    case 136: return 710;
    case 137: return 8240;
    case 138: return 352;
    case 139: return 8249;
    case 140: return 338;
    case 142: return 381;
    case 145: return 8216;
    case 146: return 8217;
    case 147: return 8220;
    case 148: return 8221;
    case 149: return 8226;
    case 150: return 8211;
    case 151: return 8212;
    case 152: return 732;
    case 153: return 8482;
    case 154: return 353;
    case 155: return 8250;
    case 156: return 339;
    case 158: return 382;
    case 159: return 376;
    default: return -1;
    }
}

class EnterFilter final : public QObject {
public:
    EnterFilter(QObject* parent, std::function<void()> f) : QObject(parent), m_f(std::move(f)) {}
protected:
    bool eventFilter(QObject* o, QEvent* e) override
    {
        if (e->type() == QEvent::KeyPress) {
            const int k = static_cast<QKeyEvent*>(e)->key();
            if (k == Qt::Key_Return || k == Qt::Key_Enter) {
                m_f();
                return true;
            }
        }
        return QObject::eventFilter(o, e);
    }
private:
    std::function<void()> m_f;
};

} // namespace

// --- Character Panel --------------------------------------------------------------------------------

CharacterPanel::CharacterPanel(QWidget* parent) : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    m_table = new QTableWidget(256, 6);
    m_table->setObjectName(QStringLiteral("characterPanelTable"));
    m_table->setHorizontalHeaderLabels({tr("Value"), tr("Hex"), tr("Character"), tr("HTML Name"), tr("HTML Decimal"),
                                        tr("HTML Hexadecimal")});
    m_table->verticalHeader()->hide();
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->verticalHeader()->setDefaultSectionSize(m_table->fontMetrics().height() + 4);
    layout->addWidget(m_table);
    connect(m_table, &QTableWidget::cellDoubleClicked, this, [this](int row, int column) {
        const QByteArray t = textFor(row, column);
        if (!t.isEmpty())
            emit insertRequested(t);
    });
    m_table->installEventFilter(new EnterFilter(m_table, [this] {
        if (m_table->currentRow() >= 0)
            emit insertRequested(textFor(m_table->currentRow(), 2));
    }));
    setCodePage(-1);
}

void CharacterPanel::setCodePage(int codePage)
{
    if (codePage == m_codePage)
        return;
    m_codePage = codePage;
    if (m_filledFor != -3 || isVisible()) // filled once: kept in step
        fill();
}

void CharacterPanel::ensureFilled() const
{
    if (m_filledFor != m_codePage)
        const_cast<CharacterPanel*>(this)->fill();
}

void CharacterPanel::showEvent(QShowEvent* e)
{
    ensureFilled();
    QWidget::showEvent(e);
}

QByteArray CharacterPanel::textFor(int row, int column) const
{
    if (row < 0 || row > 255)
        return {};
    if (column != 2) {
        ensureFilled();
        QTableWidgetItem* it = m_table->item(row, column);
        return it ? it->text().toUtf8() : QByteArray();
    }
    if (row < 128)
        return QByteArray(1, static_cast<char>(row));
    const int cp = m_codePage == -1 ? core::systemAnsiCodePage() : m_codePage;
    const char byte = static_cast<char>(row);
    if (auto u = core::codePageToUtf8(std::string_view(&byte, 1), cp))
        return QByteArray::fromStdString(*u);
    return {};
}

void CharacterPanel::fill()
{
    m_filledFor = m_codePage;
    const bool html = m_codePage == -1 || m_codePage == 1252;
    for (int i = 0; i < 256; ++i) {
        QString ch;
        if (i <= 32)
            ch = QLatin1String(kControlNames[i]);
        else if (i == 127)
            ch = QStringLiteral("DEL");
        else
            ch = QString::fromUtf8(textFor(i, 2));
        QString num, hexNum, name;
        if (html) {
            if ((i >= 32 && i <= 126 && i != 45) || i >= 160) {
                num = QStringLiteral("&#%1;").arg(i);
                hexNum = QStringLiteral("&#x%1;").arg(i, 0, 16);
            } else if (const int n = htmlNumber(i); n > -1) {
                num = QStringLiteral("&#%1;").arg(n);
                hexNum = QStringLiteral("&#x%1;").arg(n, 0, 16);
            }
            name = QString::fromUtf8(htmlName(i));
        }
        const QStringList values{QString::number(i), QStringLiteral("%1").arg(i, 2, 16, QLatin1Char('0')).toUpper(), ch,
                                 name, num, hexNum};
        for (int c = 0; c < values.size(); ++c) {
            auto* it = new QTableWidgetItem(values[c]);
            if (c < 2)
                it->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
            m_table->setItem(i, c, it);
        }
    }
    m_table->resizeColumnsToContents();
}

// --- Clipboard History ---------------------------------------------------------------------------------

ClipboardHistoryPanel::ClipboardHistoryPanel(QWidget* parent) : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    m_list = new QListWidget;
    m_list->setObjectName(QStringLiteral("clipboardHistoryList"));
    layout->addWidget(m_list);
    connect(m_list, &QListWidget::itemActivated, this, [this](QListWidgetItem* it) {
        const int row = m_list->row(it);
        if (row >= 0 && row < m_entries.size())
            emit insertRequested(m_entries[row]);
    });
    connect(QApplication::clipboard(), &QClipboard::dataChanged, this, [this] {
        const QMimeData* md = QApplication::clipboard()->mimeData();
        if (md && md->hasText())
            addEntry(md->text());
    });
}

void ClipboardHistoryPanel::addEntry(const QString& text)
{
    if (text.isEmpty())
        return;
    const int at = m_entries.indexOf(text);
    if (at == 0)
        return;
    if (at > 0) {
        m_entries.removeAt(at);
        delete m_list->takeItem(at);
    }
    m_entries.prepend(text);
    // upstream MAX_DISPLAY_LENGTH: 64 characters, line breaks shown as spaces
    QString shown = text.left(64);
    shown.replace(QLatin1Char('\r'), QLatin1Char(' ')).replace(QLatin1Char('\n'), QLatin1Char(' ')).replace(QLatin1Char('\t'), QLatin1Char(' '));
    if (text.size() > 64)
        shown += QStringLiteral("...");
    auto* item = new QListWidgetItem(shown);
    item->setToolTip(text.left(1000));
    m_list->insertItem(0, item);
    m_list->setCurrentRow(0);
}

} // namespace npp::ui
