// Run > Run... dialog (upstream RunDlg) and the user commands of shortcuts.xml <UserDefinedCommands>
// listed in the Run menu.
#include "MainWindow.h"

#include "DialogTranslator.h"
#include "editor/EditorPane.h"
#include "menu/ActionRegistry.h"
#include "menu/CommandIds.h"

#include "core/run/RunCommand.h"
#include "core/util/PathUtil.h"

#include <Scintilla.h>
#include <ScintillaEditBase.h>

#include <QComboBox>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QDialog>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QProcess>
#include <QPushButton>
#include <QStandardPaths>
#include <QUrl>

#include <map>

namespace npp::ui {

using namespace npp::core;
using namespace cmd;

namespace {

QString q(const std::string& s) { return QString::fromStdString(s); }

QMenu* menuOfAction(QAction* a)
{
    for (QObject* o : a->associatedObjects())
        if (auto* m = qobject_cast<QMenu*>(o))
            return m;
    return nullptr;
}

constexpr int IDC_BUTTON_FILE_BROWSER = 1901, IDC_COMBO_RUN_PATH = 1902, IDC_MAINTEXT_STATIC = 1903,
              IDC_BUTTON_SAVE = 1904, IDC_BUTTON_VARIABLES = 1905, IDOK = 1, IDCANCEL = 2;

const char* const kRunVariables[] = {"FULL_CURRENT_PATH", "CURRENT_DIRECTORY", "FILE_NAME", "NAME_PART",
                                     "EXT_PART", "CURRENT_WORD", "NPP_DIRECTORY", "NPP_FULL_FILE_PATH",
                                     "CURRENT_LINE", "CURRENT_COLUMN", "CURRENT_LINESTR"};

} // namespace

RunVariables MainWindow::runVariables()
{
    RunVariables v;
    Buffer* b = currentBuffer();
    ScintillaEditBase* e = currentPane()->editor();
    if (b)
        v.fullCurrentPath = b->isUntitled() ? b->displayName() : QDir::toNativeSeparators(q(b->path)).toStdString();
    const auto pos = e->send(SCI_GETCURRENTPOS);
    v.currentLine = e->send(SCI_LINEFROMPOSITION, static_cast<uptr_t>(pos));
    v.currentColumn = e->send(SCI_GETCOLUMN, static_cast<uptr_t>(pos));
    const auto ls = e->send(SCI_POSITIONFROMLINE, static_cast<uptr_t>(v.currentLine));
    const auto le = e->send(SCI_POSITIONFROMLINE, static_cast<uptr_t>(v.currentLine + 1));
    v.currentLineStr = currentPane()->adapter().textRange(ls, le > ls ? le : e->send(SCI_GETLENGTH));
    auto s = e->send(SCI_GETSELECTIONSTART), en = e->send(SCI_GETSELECTIONEND);
    if (s == en) { // upstream getSelectedTextToWChar(expand = true): the word at the caret
        s = e->send(SCI_WORDSTARTPOSITION, static_cast<uptr_t>(pos), 1);
        en = e->send(SCI_WORDENDPOSITION, static_cast<uptr_t>(pos), 1);
    }
    v.currentWord = currentPane()->adapter().textRange(s, en);
    v.nppFullFilePath = QDir::toNativeSeparators(QCoreApplication::applicationFilePath()).toStdString();
    return v;
}

QString MainWindow::expandRunCommand(const QString& command)
{
    // upstream Command::run: environment variables first (ExpandEnvironmentStrings), then $(...)
    return q(expandRunVariables(expandEnvironment(command.toStdString()), runVariables()));
}

bool MainWindow::runCommandLine(const QString& command)
{
    const SplitCommand sc = splitRunCommand(command.trimmed().toStdString());
    const QString program = q(expandRunVariables(expandEnvironment(sc.program), runVariables()));
    const QString args = q(expandRunVariables(expandEnvironment(sc.args), runVariables()));
    if (program.isEmpty())
        return false;
    // ShellExecute "open": documents, folders and URLs open in their associated application
    const bool url = program.contains(QStringLiteral("://"));
    const QFileInfo fi(program);
    const QString exe = url ? QString() : (fi.isAbsolute() || program.contains(QLatin1Char('/')) || program.contains(QLatin1Char('\\'))
                                               ? (fi.isExecutable() && fi.isFile() ? program : QString())
                                               : QStandardPaths::findExecutable(program));
    if (exe.isEmpty()) {
        if (url || fi.exists())
            return QDesktopServices::openUrl(url ? QUrl(program) : QUrl::fromLocalFile(fi.absoluteFilePath()));
        QMessageBox::warning(this, tr("Run"), tr("The program \"%1\" could not be found.").arg(program));
        return false;
    }
    QProcess p;
    p.setProgram(exe);
#if defined(Q_OS_WIN)
    p.setNativeArguments(args); // the command line exactly as typed, like ShellExecute
#else
    p.setArguments(QProcess::splitCommand(args));
#endif
    p.setWorkingDirectory(QDir::currentPath());
    const bool ok = p.startDetached();
    if (!ok)
        QMessageBox::warning(this, tr("Run"), tr("\"%1\" could not be started.").arg(exe));
    return ok;
}

void MainWindow::showRunDialog()
{
    QDialog dlg(this);
    dlg.setObjectName(QStringLiteral("Run"));
    dlg.setWindowTitle(tr("Run..."));
    auto* label = new QLabel(tr("The Program to Run"));
    setControlId(label, IDC_MAINTEXT_STATIC);
    auto* combo = new QComboBox;
    combo->setEditable(true);
    combo->addItems(m_runHistory);
    combo->setCurrentText(m_runHistory.isEmpty() ? QString() : m_runHistory.first());
    combo->setMinimumWidth(420);
    setControlId(combo, IDC_COMBO_RUN_PATH);
    auto* browse = new QPushButton(QStringLiteral("..."));
    setControlId(browse, IDC_BUTTON_FILE_BROWSER);
    auto* vars = new QPushButton(QStringLiteral("+"));
    vars->setToolTip(tr("Insert a variable"));
    setControlId(vars, IDC_BUTTON_VARIABLES);
    auto* varMenu = new QMenu(vars);
    for (const char* v : kRunVariables)
        varMenu->addAction(QString::fromLatin1(v), &dlg, [combo, v] {
            combo->setCurrentText(combo->currentText() + QStringLiteral("$(%1)").arg(QLatin1String(v)));
        });
    vars->setMenu(varMenu);
    auto* run = new QPushButton(tr("Run"));
    run->setDefault(true);
    setControlId(run, IDOK);
    auto* save = new QPushButton(tr("Save..."));
    setControlId(save, IDC_BUTTON_SAVE);
    auto* cancel = new QPushButton(tr("Cancel"));
    setControlId(cancel, IDCANCEL);
    connect(browse, &QPushButton::clicked, &dlg, [&dlg, combo] {
        const QString f = QFileDialog::getOpenFileName(&dlg, tr("Program to run"));
        if (!f.isEmpty())
            combo->setCurrentText(f.contains(QLatin1Char(' ')) ? QStringLiteral("\"%1\"").arg(QDir::toNativeSeparators(f))
                                                               : QDir::toNativeSeparators(f));
    });
    connect(run, &QPushButton::clicked, &dlg, [this, &dlg, combo] {
        const QString c = combo->currentText().trimmed();
        if (c.isEmpty())
            return;
        m_runHistory.removeAll(c);
        m_runHistory.prepend(c);
        dlg.accept();
        runCommandLine(c);
    });
    connect(save, &QPushButton::clicked, &dlg, [this, &dlg, combo] {
        const QString c = combo->currentText().trimmed();
        if (c.isEmpty())
            return;
        bool ok = false;
        const QString name = QInputDialog::getText(&dlg, tr("Save"), tr("Name:"), QLineEdit::Normal, {}, &ok).trimmed();
        if (!ok || name.isEmpty())
            return;
        addUserCommand(name, c);
        dlg.accept();
    });
    connect(cancel, &QPushButton::clicked, &dlg, &QDialog::reject);
    auto* grid = new QGridLayout(&dlg);
    grid->addWidget(label, 0, 0, 1, 3);
    grid->addWidget(combo, 1, 0);
    grid->addWidget(vars, 1, 1);
    grid->addWidget(browse, 1, 2);
    auto* buttons = new QHBoxLayout;
    buttons->addStretch(1);
    buttons->addWidget(run);
    buttons->addWidget(save);
    buttons->addWidget(cancel);
    grid->addLayout(buttons, 2, 0, 1, 3);
    translateDialog(&dlg, dialogLanguage(), "Run");
    m_runDialog = &dlg;
    dlg.exec();
    m_runDialog = nullptr;
}

void MainWindow::addUserCommand(const QString& name, const QString& command)
{
    UserCommand uc;
    uc.name = name.toStdString();
    uc.command = command.toStdString();
    m_shortcuts.userCommands.push_back(uc);
    std::error_code ec;
    fs::create_directories(pathFromUtf8(m_paths.userDir()), ec);
    m_shortcuts.save(m_paths.shortcutsXml());
    recordShortcutsXmlHmac(); // written by Notepad++ itself: trusted
    rebuildRunMenu();
}

void MainWindow::rebuildRunMenu()
{
    for (const QPointer<QObject>& o : m_userCommandObjects)
        delete o.data();
    m_userCommandObjects.clear();
    QAction* validate = m_actions->action(IDM_EXECUTE_VALIDATE_SHORTCUTSXML);
    QMenu* menu = menuOfAction(validate);
    if (!menu)
        return;
    // upstream: user commands after "Run...", sub menus by FolderName
    QAction* insertBefore = nullptr;
    const QList<QAction*> acts = menu->actions();
    for (int i = 0; i < acts.size(); ++i)
        if (acts[i] == validate && i > 0 && acts[i - 1]->isSeparator())
            insertBefore = acts[i - 1];
    if (!insertBefore)
        insertBefore = validate;
    std::map<std::string, QMenu*> folders;
    for (size_t i = 0; i < m_shortcuts.userCommands.size(); ++i) {
        const UserCommand& uc = m_shortcuts.userCommands[i];
        auto* a = new QAction(q(uc.name), menu);
        if (const auto k = toQtKey(uc.combo))
            a->setShortcut(QKeySequence(*k));
        a->setShortcutContext(Qt::WindowShortcut);
        addAction(a);
        connect(a, &QAction::triggered, this, [this, i] {
            if (i < m_shortcuts.userCommands.size() && shortcutsXmlTrusted()) // upstream shortcuts.xml security check
                runCommandLine(q(m_shortcuts.userCommands[i].command));
        });
        m_userCommandObjects.emplace_back(a);
        if (uc.folder.empty()) {
            menu->insertAction(insertBefore, a);
        } else {
            QMenu*& sub = folders[uc.folder];
            if (!sub) {
                sub = new QMenu(q(uc.folder), menu);
                menu->insertMenu(insertBefore, sub);
                m_userCommandObjects.emplace_back(sub);
            }
            sub->addAction(a);
        }
    }
    if (!m_shortcuts.userCommands.empty())
        m_userCommandObjects.emplace_back(menu->insertSeparator(insertBefore));
}

void MainWindow::registerRunCommands()
{
    m_actions->setHandler(IDM_EXECUTE, {[this] { showRunDialog(); }});
}

} // namespace npp::ui
