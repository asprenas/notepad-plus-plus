// Main toolbar (port of upstream ToolBar / ToolBarIcons): the 41 upstream buttons with the
// Fluent UI (regular / filled, small 16 / large 32, light / dark), standard bitmap icon sets,
// Fluent colorization, toolbarButtonsConf.xml hidden buttons and custom icon folder.
#include "MainWindow.h"
#include "plugins/PluginHost.h"

#include "menu/ActionRegistry.h"
#include "menu/CommandIds.h"

#include "core/config/ToolbarButtonsConfig.h"
#include "core/util/PathUtil.h"

#include <QAction>
#include <QBitmap>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QPalette>
#include <QStyleHints>
#include <QToolBar>

#include <cstdlib>

namespace npp::ui {

using namespace npp::core;
using namespace cmd;

namespace {

struct ToolbarButtonDef {
    int id;                // 0 = separator
    const char* files[9];  // light regular (+dis), light filled (+dis), dark regular (+dis), dark filled (+dis), standard
    const char* custom;    // custom icon name (toolbarIcons/<folder>/<name>.ico)
};

const std::vector<ToolbarButtonDef>& toolbarButtons()
{
    static const std::vector<ToolbarButtonDef> defs = {
#define NPP_TB_BUTTON(id, a, b, c, d, e, f, g, h, i, custom) {id, {a, b, c, d, e, f, g, h, i}, custom},
#define NPP_TB_SEPARATOR() {0, {}, nullptr},
#include "menu/ToolbarButtons.inc"
#undef NPP_TB_BUTTON
#undef NPP_TB_SEPARATOR
    };
    return defs;
}

// upstream toolBarStatusType order (DarkMode darkToolBarIconSet / lightToolBarIconSet)
constexpr const char* kIconSetNames[] = {"small", "large", "small2", "large2", "standard"};

int iconSetIndex(const std::string& name)
{
    for (int i = 0; i < 5; ++i)
        if (name == kIconSetNames[i])
            return i;
    return 4;
}

// upstream IconList::changeFluentIconColor: recolor the secondary (accent) colour, or every pixel (mono)
QImage colorize(QImage img, bool everything, QRgb from, QRgb to, int tolerance = 3)
{
    img = img.convertToFormat(QImage::Format_ARGB32);
    for (int y = 0; y < img.height(); ++y) {
        auto* line = reinterpret_cast<QRgb*>(img.scanLine(y));
        for (int x = 0; x < img.width(); ++x) {
            const QRgb p = line[x];
            if (qAlpha(p) == 0)
                continue;
            if (everything || (std::abs(qRed(p) - qRed(from)) <= tolerance && std::abs(qGreen(p) - qGreen(from)) <= tolerance
                               && std::abs(qBlue(p) - qBlue(from)) <= tolerance))
                line[x] = qRgba(qRed(to), qGreen(to), qBlue(to), qAlpha(p));
        }
    }
    return img;
}

} // namespace

QString MainWindow::toolbarIconSet() const
{
    return QString::fromStdString(m_config.gui.toolbarIconSet);
}

void MainWindow::buildToolbar()
{
    if (!m_toolbar) {
        m_toolbar = addToolBar(tr("Toolbar"));
        m_toolbar->setObjectName(QStringLiteral("mainToolbar"));
        m_toolbar->setMovable(false);
    }
    m_toolbar->clear();

    ToolbarButtonsConfig conf;
    conf.load(pathAppend(m_paths.userDir(), "toolbarButtonsConf.xml"));
    const std::string customDir = conf.hasIconFolder
        ? pathAppend(pathAppend(m_paths.userDir(), "toolbarIcons"), conf.iconFolderName.empty() ? "default" : conf.iconFolderName)
        : std::string();

    const GuiConfig& g = m_config.gui;
    const int set = iconSetIndex(g.toolbarIconSet);
    const bool dark = isDarkMode();
    const bool large = set == 1 || set == 3;
    const int size = large ? 32 : 16;
    const QString iconsDir = QString::fromStdString(pathAppend(m_dataDir, "icons"));

    // Fluent colour (upstream FluentColor order: default, red, green, blue, purple, cyan, olive, yellow, accent, custom)
    static constexpr QRgb kColours[] = {0, 0xE81123, 0x008B00, 0x0078D4, 0xB146C2, 0x00B7C3, 0x498205, 0xFFB900, 0, 0};
    const QRgb mainColour = dark ? 0xDEDEDE : 0x212121;
    const QRgb secondary = dark ? 0x4CC2FF : 0x0078D4;
    std::optional<QRgb> newColour;
    switch (g.toolbarFluentColor) {
    case 8: newColour = QGuiApplication::palette().color(QPalette::Accent).rgb(); break;
    case 9:
        if (g.toolbarFluentCustomColor != 0) { // COLORREF 0x00BBGGRR
            const int c = g.toolbarFluentCustomColor;
            newColour = qRgb(c & 0xFF, (c >> 8) & 0xFF, (c >> 16) & 0xFF);
            break;
        }
        [[fallthrough]];
    case 0:
        if (g.toolbarFluentMono)
            newColour = mainColour;
        break;
    default:
        if (g.toolbarFluentColor > 0 && g.toolbarFluentColor < 8)
            newColour = kColours[g.toolbarFluentColor];
        break;
    }

    auto load = [&](const QString& file) -> QPixmap {
        QImage img(file);
        if (img.isNull())
            return {};
        if (file.endsWith(QLatin1String(".bmp"), Qt::CaseInsensitive)) { // ImageList_AddMasked
            QPixmap pm = QPixmap::fromImage(img);
            pm.setMask(QBitmap::fromImage(img.createMaskFromColor(img.pixel(0, 0))));
            return pm;
        }
        if (set != 4 && newColour)
            img = colorize(img, g.toolbarFluentMono, secondary, *newColour);
        return QPixmap::fromImage(img.scaled(size, size, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    };

    const auto& defs = toolbarButtons();
    for (int index = 0; index < static_cast<int>(defs.size()); ++index) {
        const ToolbarButtonDef& d = defs[static_cast<size_t>(index)];
        if (conf.isStandardHidden(index, d.id))
            continue;
        if (d.id == 0) {
            m_toolbar->addSeparator();
            continue;
        }
        QIcon icon;
        if (!customDir.empty()) {
            const QString base = QString::fromStdString(pathAppend(customDir, d.custom));
            if (QFileInfo::exists(base + QStringLiteral(".ico"))) {
                icon.addPixmap(load(base + QStringLiteral(".ico")));
                if (QFileInfo::exists(base + QStringLiteral("_disabled.ico")))
                    icon.addPixmap(load(base + QStringLiteral("_disabled.ico")), QIcon::Disabled);
            }
        }
        if (icon.isNull()) {
            const int col = set == 4 ? 8 : (dark ? 4 : 0) + (set >= 2 ? 2 : 0);
            icon.addPixmap(load(iconsDir + QLatin1Char('/') + QString::fromLatin1(d.files[col])));
            if (col != 8 && std::string_view(d.files[col]) != d.files[col + 1])
                icon.addPixmap(load(iconsDir + QLatin1Char('/') + QString::fromLatin1(d.files[col + 1])), QIcon::Disabled);
        }
        QAction* a = m_actions->action(d.id);
        a->setIcon(icon);
        a->setIconVisibleInMenu(false); // upstream menus have no toolbar icons
        m_toolbar->addAction(a);
    }
    if (m_plugins)
        m_plugins->addToolbarButtons(m_toolbar, dark, size);
    m_toolbar->setIconSize(QSize(size, size));
    m_toolbar->setVisible(g.toolbarShow);
}

} // namespace npp::ui
