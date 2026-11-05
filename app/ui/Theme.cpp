// Light / dark / follow-system mode (phase 4, P4-05). Upstream's Win32 dark mode is replaced by
// the Fusion style with a palette built from upstream's dark color tones (NppDarkMode.cpp
// darkColors + tone offsets, or the customized colors). Like upstream, switching the mode also
// switches the editor theme: DarkMode darkThemeName / lightThemeName (empty = stylers.xml).
#include "MainWindow.h"
#include "plugins/PluginHost.h"

#include "docking/DockManager.h"
#include "editor/EditorPane.h"

#include "core/util/PathUtil.h"

#include <algorithm>

#include <QApplication>
#include <QPalette>
#include <QStyle>
#include <QStyleFactory>
#include <QStyleHints>

namespace npp::ui {

using namespace npp::core;

namespace {

struct DarkColors {
    int background, softerBackground, hotBackground, pureBackground, errorBackground, text, darkerText, disabledText, linkText,
        edge, hotEdge, disabledEdge;
};

// 0xRRGGBB values of upstream darkColors and its tones
DarkColors toneColors(int tone)
{
    DarkColors c{0x202020, 0x383838, 0x454545, 0x202020, 0xB00000, 0xE0E0E0, 0xC0C0C0, 0x808080, 0xFFFF00, 0x646464, 0x9B9B9B, 0x484848};
    int offset = 0;
    switch (tone) {
    case 1: offset = 0x100000; break; // red
    case 2: offset = 0x001000; break; // green
    case 3: offset = 0x000020; break; // blue
    case 4: offset = 0x100020; break; // purple
    case 5: offset = 0x001020; break; // cyan
    case 6: offset = 0x101000; break; // olive
    default: return c;                // black
    }
    for (int* v : {&c.background, &c.softerBackground, &c.hotBackground, &c.pureBackground, &c.hotEdge, &c.disabledEdge})
        *v += offset;
    c.edge += 0x1C1C1C + offset;
    return c;
}

QColor rgb(int v) { return QColor((v >> 16) & 0xFF, (v >> 8) & 0xFF, v & 0xFF); }
// config.xml stores COLORREF (0x00BBGGRR)
int fromColorRef(int v) { return ((v & 0xFF) << 16) | (v & 0xFF00) | ((v >> 16) & 0xFF); }

} // namespace

bool MainWindow::isDarkMode() const
{
    const GuiConfig& g = m_config.gui;
    if (g.darkFollowSystem)
        return QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark;
    return g.darkModeEnabled;
}

void MainWindow::applyTheme()
{
    const GuiConfig& g = m_config.gui;
    const bool dark = isDarkMode();
    if (!m_defaultStyleName.isEmpty() || !dark) {
        if (m_defaultStyleName.isEmpty())
            m_defaultStyleName = QApplication::style()->name();
    }
    if (dark) {
        DarkColors c = toneColors(g.darkModeColorTone);
        if (g.darkModeColorTone == 32) { // customized
            c.background = c.pureBackground = fromColorRef(g.darkCustomColorMain);
            c.softerBackground = fromColorRef(g.darkCustomColorActive);
            c.hotBackground = fromColorRef(g.darkCustomColorMenuHotTrack);
            c.errorBackground = fromColorRef(g.darkCustomColorError);
            c.text = fromColorRef(g.darkCustomColorText);
            c.darkerText = fromColorRef(g.darkCustomColorDarkText);
            c.disabledText = fromColorRef(g.darkCustomColorDisabledText);
            c.linkText = fromColorRef(g.darkCustomColorLinkText);
            c.edge = fromColorRef(g.darkCustomColorEdge);
            c.hotEdge = fromColorRef(g.darkCustomColorHotEdge);
            c.disabledEdge = fromColorRef(g.darkCustomColorDisabledEdge);
        }
        QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
        QPalette p;
        p.setColor(QPalette::Window, rgb(c.pureBackground));
        p.setColor(QPalette::WindowText, rgb(c.text));
        p.setColor(QPalette::Base, rgb(c.softerBackground));
        p.setColor(QPalette::AlternateBase, rgb(c.background));
        p.setColor(QPalette::Text, rgb(c.text));
        p.setColor(QPalette::Button, rgb(c.softerBackground));
        p.setColor(QPalette::ButtonText, rgb(c.text));
        p.setColor(QPalette::ToolTipBase, rgb(c.softerBackground));
        p.setColor(QPalette::ToolTipText, rgb(c.text));
        p.setColor(QPalette::Highlight, rgb(c.hotEdge));
        p.setColor(QPalette::HighlightedText, rgb(0xFFFFFF));
        p.setColor(QPalette::Link, rgb(c.linkText));
        p.setColor(QPalette::Light, rgb(c.hotEdge));
        p.setColor(QPalette::Midlight, rgb(c.edge));
        p.setColor(QPalette::Mid, rgb(c.edge));
        p.setColor(QPalette::Dark, rgb(c.disabledEdge));
        p.setColor(QPalette::Shadow, rgb(0x000000));
        p.setColor(QPalette::PlaceholderText, rgb(c.disabledText));
        for (auto role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText})
            p.setColor(QPalette::Disabled, role, rgb(c.disabledText));
        QApplication::setPalette(p);
    } else {
        // Light mode: a new style / palette re-polishes every widget, which costs most of the
        // start-up time of this function; only change what differs (leaving dark mode)
        const QString style = m_defaultStyleName.isEmpty() ? QStringLiteral("Fusion") : m_defaultStyleName;
        if (QApplication::style()->name().compare(style, Qt::CaseInsensitive) != 0 || m_uiPaletteDark) {
            QApplication::setStyle(QStyleFactory::create(style));
            QApplication::setPalette(QApplication::style()->standardPalette());
        }
    }
    m_uiPaletteDark = dark;
    m_dock->applyPalette(QApplication::palette());

    // editor theme of the mode
    if (dark != m_themeIsDark || !m_themeApplied) {
        const std::string name = dark ? g.darkThemeName : g.lightThemeName;
        std::string path;
        if (!name.empty()) {
            for (const std::string& dir : {pathAppend(m_paths.userDir(), "themes"), pathAppend(m_dataDir, "themes")})
                if (pathExists(pathAppend(dir, name), PathKind::File)) {
                    path = pathAppend(dir, name);
                    break;
                }
        }
        if (path.empty())
            path = pathExists(m_paths.stylersXml(), PathKind::File) ? m_paths.stylersXml() : pathAppend(m_dataDir, "stylers.model.xml");
        if (m_themeApplied || !name.empty()) {
            m_styles.load(path);
            m_currentThemePath = path;
            for (EditorPane* p : m_panes)
                if (Buffer* b = buffer(p->current()))
                    applyLanguageToPane(p, b);
        }
        // upstream NppDarkMode::getToolbarIconInfo: each mode keeps its own toolbar settings
        // (DarkMode dark* / light*); <ToolBar> mirrors the ones of the current mode
        if (m_themeApplied)
            syncToolbarToMode(m_themeIsDark);
        syncToolbarFromMode(dark);
        const bool changed = m_themeApplied && m_themeIsDark != dark;
        m_themeIsDark = dark;
        if (changed)
            pluginNotify(NPPN_DARKMODECHANGED);
        m_themeApplied = true;
    }
    if (m_toolbar)
        buildToolbar(); // icons follow the mode
    for (EditorPane* p : m_panes)
        for (BufferId id : p->buffers())
            if (const Buffer* b = buffer(id))
                refreshTabDecorations(*b);
}

namespace {
constexpr const char* kIconSets[] = {"small", "large", "small2", "large2", "standard"};
}

void MainWindow::syncToolbarFromMode(bool dark)
{
    GuiConfig& g = m_config.gui;
    g.toolbarIconSet = kIconSets[std::clamp(dark ? g.darkToolBarIconSet : g.lightToolBarIconSet, 0, 4)];
    g.toolbarFluentColor = dark ? g.darkTbFluentColor : g.lightTbFluentColor;
    g.toolbarFluentCustomColor = dark ? g.darkTbFluentCustomColor : g.lightTbFluentCustomColor;
    g.toolbarFluentMono = dark ? g.darkTbFluentMono : g.lightTbFluentMono;
}

void MainWindow::syncToolbarToMode(bool dark)
{
    GuiConfig& g = m_config.gui;
    int set = 4;
    for (int i = 0; i < 5; ++i)
        if (g.toolbarIconSet == kIconSets[i])
            set = i;
    (dark ? g.darkToolBarIconSet : g.lightToolBarIconSet) = set;
    (dark ? g.darkTbFluentColor : g.lightTbFluentColor) = g.toolbarFluentColor;
    (dark ? g.darkTbFluentCustomColor : g.lightTbFluentCustomColor) = g.toolbarFluentCustomColor;
    (dark ? g.darkTbFluentMono : g.lightTbFluentMono) = g.toolbarFluentMono;
}

void MainWindow::connectSystemTheme()
{
    connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged, this, [this] {
        if (m_config.gui.darkFollowSystem)
            applyTheme();
    });
}

} // namespace npp::ui
