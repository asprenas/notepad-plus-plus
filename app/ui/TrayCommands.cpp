// System tray icon (upstream WinControls/TrayIcon, config.xml <GUIConfig name="TrayIcon">):
// 0 none, 1 minimize to tray, 2 close to tray, 3 both (upstream SysTrayAction); -systemtray.
#include "MainWindow.h"

#include "menu/ActionRegistry.h"
#include "menu/CommandIds.h"
#include "editor/EditorPane.h"

#include <Scintilla.h>

#include <QApplication>
#include <QCloseEvent>
#include <QMenu>
#include <QSystemTrayIcon>
#include <QTimer>

namespace npp::ui {

using namespace cmd;

namespace {

constexpr int kStaMinimize = 1, kStaClose = 2, kStaMinimizeClose = 3;

} // namespace

void MainWindow::hideToTray()
{
    if (!QSystemTrayIcon::isSystemTrayAvailable() && !m_forceTray) {
        showMinimized(); // no tray (some Linux desktops): hiding would leave no way back
        return;
    }
    if (!m_tray) {
        m_tray = new QSystemTrayIcon(windowIcon().isNull() ? QApplication::windowIcon() : windowIcon(), this);
        m_tray->setToolTip(windowTitle());
        auto* menu = new QMenu(this);
        for (int id : {IDM_SYSTRAYPOPUP_ACTIVATE, 0, IDM_SYSTRAYPOPUP_NEWDOC, IDM_SYSTRAYPOPUP_NEW_AND_PASTE,
                       IDM_SYSTRAYPOPUP_OPENFILE, IDM_SEARCH_FINDINFILES, 0, IDM_SYSTRAYPOPUP_CLOSE})
            if (id)
                menu->addAction(m_actions->action(id));
            else
                menu->addSeparator();
        m_tray->setContextMenu(menu);
        connect(m_tray, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason r) {
            if (r == QSystemTrayIcon::Trigger || r == QSystemTrayIcon::DoubleClick)
                m_actions->trigger(IDM_SYSTRAYPOPUP_ACTIVATE);
        });
    }
    m_tray->show();
    hide();
}

void MainWindow::restoreFromTray()
{
    m_config.gui.appMaximized ? showMaximized() : showNormal();
    raise();
    activateWindow();
}

void MainWindow::changeEvent(QEvent* e)
{
    QMainWindow::changeEvent(e);
    if (e->type() != QEvent::WindowStateChange || !(windowState() & Qt::WindowMinimized))
        return;
    const int mode = m_config.gui.minimizeToTray;
    if (mode == kStaMinimize || mode == kStaMinimizeClose)
        QTimer::singleShot(0, this, &MainWindow::hideToTray); // not inside the state change
}

bool MainWindow::closeToTray(QCloseEvent* event)
{
    // the title bar close button (upstream SC_CLOSE) goes to the tray, File > Exit does not
    const int mode = m_config.gui.minimizeToTray;
    if (m_exitRequested || m_exiting || !(mode == kStaClose || mode == kStaMinimizeClose))
        return false;
    event->ignore();
    hideToTray();
    return true;
}

void MainWindow::registerTrayCommands()
{
    ActionRegistry* r = m_actions;
    r->setHandler(IDM_SYSTRAYPOPUP_ACTIVATE, {[this] { restoreFromTray(); }});
    r->setHandler(IDM_SYSTRAYPOPUP_NEWDOC, {[this] {
        restoreFromTray();
        m_actions->trigger(IDM_FILE_NEW);
    }});
    r->setHandler(IDM_SYSTRAYPOPUP_NEW_AND_PASTE, {[this] {
        restoreFromTray();
        const core::Buffer* b = currentBuffer();
        if (!b || !b->isUntitled() || currentPane()->adapter().send(SCI_GETLENGTH) != 0)
            m_actions->trigger(IDM_FILE_NEW);
        m_actions->trigger(IDM_EDIT_PASTE);
    }});
    r->setHandler(IDM_SYSTRAYPOPUP_OPENFILE, {[this] {
        restoreFromTray();
        m_actions->trigger(IDM_FILE_OPEN);
    }});
    r->setHandler(IDM_SYSTRAYPOPUP_CLOSE, {[this] {
        if (m_tray)
            m_tray->hide();
        if (!isVisible()) { // upstream: closes Notepad++ when it is in the tray
            m_exitRequested = true;
            close();
        }
    }});
    if (m_cmd.systemTray) // -systemtray: start in the tray
        QTimer::singleShot(0, this, &MainWindow::hideToTray);
}

} // namespace npp::ui
