// Sample plugin with a Qt user interface (phase 7, P7-06): a dockable "Document Info" panel.
//
// Shows: registerPanel / showPanel with a QWidget (the plugin is linked against the same Qt as the
// application), a check mark that follows the panel visibility, NPPM_* messages through
// nppMessage (language name), a toolbar button (addToolbarIcon with icon files shipped next to
// the library) and updates driven by NPPN_* / SCN_* notifications.
//
// Plugins > Document Info:
//   Document Info panel   (check)   Ctrl+Alt+Shift+I
//   ---
//   About...
#define NPP_PLUGIN_IMPLEMENTATION
#include <npp_plugin_api.h>
#include <npp_msgs.h>
#include <Scintilla.h>

#include <QEvent>
#include <QDir>
#include <QFileInfo>
#include <QFormLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPointer>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

namespace {

const NppApi* g_api = nullptr;
NppPluginHandle g_self = nullptr;
constexpr const char* kPanelId = "info";

class InfoPanel final : public QWidget {
public:
    InfoPanel()
    {
        auto* form = new QFormLayout;
        auto add = [&](const char* name, const QString& label) {
            auto* l = new QLabel;
            l->setObjectName(QString::fromLatin1(name));
            l->setTextInteractionFlags(Qt::TextSelectableByMouse);
            l->setWordWrap(true);
            form->addRow(label, l);
            return l;
        };
        m_name = add("name", tr("Name:"));
        m_folder = add("folder", tr("Folder:"));
        m_language = add("language", tr("Language:"));
        m_length = add("length", tr("Length:"));
        m_lines = add("lines", tr("Lines:"));
        m_words = add("words", tr("Words:"));
        auto* top = new QVBoxLayout(this);
        top->addLayout(form);
        top->addStretch();
        m_timer.setSingleShot(true);
        m_timer.setInterval(150);
        QObject::connect(&m_timer, &QTimer::timeout, this, [this] { refresh(); });
    }

    void scheduleRefresh() { m_timer.start(); }

    void refresh()
    {
        if (!g_api)
            return;
        char path[4096] = {};
        g_api->currentFilePath(path, sizeof path);
        const QFileInfo fi(QString::fromUtf8(path));
        m_name->setText(fi.fileName().isEmpty() ? QString::fromUtf8(path) : fi.fileName());
        m_folder->setText(fi.isAbsolute() ? QDir::toNativeSeparators(fi.absolutePath()) : QString());
        int lang = 0;
        g_api->nppMessage(NPPM_GETCURRENTLANGTYPE, 0, reinterpret_cast<intptr_t>(&lang));
        char langName[256] = {};
        if (g_api->nppMessage(NPPM_GETLANGUAGENAME, static_cast<uintptr_t>(lang), 0) < static_cast<intptr_t>(sizeof langName))
            g_api->nppMessage(NPPM_GETLANGUAGENAME, static_cast<uintptr_t>(lang), reinterpret_cast<intptr_t>(langName));
        m_language->setText(QString::fromUtf8(langName));
        const intptr_t length = g_api->sendToEditor(NPP_VIEW_CURRENT, SCI_GETLENGTH, 0, 0);
        m_length->setText(QString::number(length));
        m_lines->setText(QString::number(g_api->sendToEditor(NPP_VIEW_CURRENT, SCI_GETLINECOUNT, 0, 0)));
        // words: the document text through SCI_GETCHARACTERPOINTER (no copy)
        const auto* text = reinterpret_cast<const char*>(g_api->sendToEditor(NPP_VIEW_CURRENT, SCI_GETCHARACTERPOINTER, 0, 0));
        long words = 0;
        bool inWord = false;
        for (intptr_t i = 0; text && i < length; ++i) {
            const unsigned char c = static_cast<unsigned char>(text[i]);
            const bool space = c == ' ' || c == '\t' || c == '\r' || c == '\n';
            if (!space && !inWord)
                ++words;
            inWord = !space;
        }
        m_words->setText(QString::number(words));
    }

private:
    QLabel *m_name, *m_folder, *m_language, *m_length, *m_lines, *m_words;
    QTimer m_timer;
};

QPointer<InfoPanel> g_panel;

void togglePanel();
void about();

NppMenuItem g_items[] = {
    {"Document Info panel", togglePanel, 0, {1, 1, 1, 'I'}, 0},
    {nullptr, nullptr, 0, {}, 0},
    {"About...", about, 0, {}, 0},
};

// Check mark of the menu item = panel visibility (also when the panel's close button is used)
class VisibilityWatcher final : public QObject {
public:
    using QObject::QObject;
    bool eventFilter(QObject* o, QEvent* e) override
    {
        if (g_api && (e->type() == QEvent::Show || e->type() == QEvent::Hide)) {
            g_api->setMenuItemCheck(g_items[0].cmdId, e->type() == QEvent::Show);
            if (e->type() == QEvent::Show && g_panel)
                g_panel->refresh();
        }
        return QObject::eventFilter(o, e);
    }
};

void togglePanel()
{
    if (!g_panel)
        return;
    g_api->showPanel(g_self, kPanelId, g_api->isPanelVisible(g_self, kPanelId) ? 0 : 1);
}

void about()
{
    QMessageBox::about(static_cast<QWidget*>(g_api->mainWindow()), QStringLiteral("Document Info"),
                       QStringLiteral("Document Info 1.0\nSample Qt plugin of Notepad++ Qt (plugin API %1).\nQt %2")
                           .arg(NPP_PLUGIN_API_VERSION)
                           .arg(QString::fromLatin1(qVersion())));
}

} // namespace

extern "C" {

NPP_EXPORT const NppPluginInfo* npp_plugin_info(void)
{
    static const NppPluginInfo info{NPP_PLUGIN_API_VERSION, "Document Info", "1.0"};
    return &info;
}

NPP_EXPORT int npp_plugin_init(const NppApi* api, NppPluginHandle self)
{
    if (!NPP_API_HAS(api, registerPanel) || !NPP_API_HAS(api, addToolbarIcon))
        return 1;
    g_api = api;
    g_self = self;
    // The panel is registered at start-up so that the dock layout of the last session restores it.
    auto* panel = new InfoPanel; // owned by the application from now on
    panel->installEventFilter(new VisibilityWatcher(panel));
    g_panel = panel;
    return api->registerPanel(self, kPanelId, "Document Info", panel) ? 0 : 2;
}

NPP_EXPORT int npp_plugin_menu_items(NppMenuItem** items)
{
    *items = g_items;
    return static_cast<int>(sizeof g_items / sizeof g_items[0]);
}

NPP_EXPORT void npp_plugin_notify(const NppNotification* n)
{
    if (n->code == NPPN_TBMODIFICATION && g_api) {
        // toolbar button from the icons next to the library
        char file[4096] = {};
        g_api->pluginFilePath(g_self, file, sizeof file);
        const QString dir = QFileInfo(QString::fromUtf8(file)).absolutePath();
        const QByteArray light = (dir + QStringLiteral("/docinfo.ico")).toUtf8();
        const QByteArray dark = (dir + QStringLiteral("/docinfo_dark.ico")).toUtf8();
        g_api->addToolbarIcon(g_items[0].cmdId, light.constData(), dark.constData());
        return;
    }
    if (!g_panel || !g_panel->isVisible())
        return;
    switch (n->code) {
    case NPPN_READY:
    case NPPN_BUFFERACTIVATED:
    case NPPN_FILESAVED:
    case NPPN_LANGCHANGED:
        g_panel->scheduleRefresh();
        break;
    case SCN_MODIFIED: {
        const auto* scn = static_cast<const SCNotification*>(n->scn);
        if (scn->modificationType & (SC_MOD_INSERTTEXT | SC_MOD_DELETETEXT))
            g_panel->scheduleRefresh();
        break;
    }
    default:
        break;
    }
}

NPP_EXPORT void npp_plugin_shutdown(void)
{
    g_api = nullptr; // the panel itself belongs to the application
}

} // extern "C"
