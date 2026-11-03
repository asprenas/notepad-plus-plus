// Phase 4 UI tests: Preferences pages (bindings, save / reload), UI language switch with dialog
// translations, light / dark mode, Style Configurator, Shortcut Mapper.
#include "MainWindow.h"
#include "editor/EditorPane.h"
#include "menu/ActionRegistry.h"
#include "menu/CommandIds.h"
#include "prefs/PreferencesDialog.h"

#include "core/config/ConfigFile.h"

#include <Scintilla.h>
#include <ScintillaEditBase.h>

#include <QApplication>
#include <QCheckBox>
#include <QDir>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QMenuBar>
#include <QRadioButton>
#include <QTemporaryDir>
#include <QTest>

using namespace npp::core;
using namespace npp::ui;
using namespace npp::ui::cmd;

class TestSettings : public QObject {
    Q_OBJECT
    QTemporaryDir m_user;
    std::unique_ptr<MainWindow> m_w;

    void makeWindow()
    {
        m_w.reset();
        CommandLine c;
        c.noSession = true;
        m_w = std::make_unique<MainWindow>(c);
    }

private slots:
    void initTestCase()
    {
        qputenv("NPP_QT_USER_DIR", m_user.path().toUtf8());
        qputenv("NPP_DATA_DIR", (QCoreApplication::applicationDirPath() + QStringLiteral("/data")).toUtf8());
    }
    void init() { makeWindow(); }
    void cleanup() { m_w.reset(); }

    void allPagesExist()
    {
        PreferencesDialog* d = m_w->preferencesDialog();
#if defined(Q_OS_WIN)
        QCOMPARE(d->pageTags().size(), 24);
#else
        QCOMPARE(d->pageTags().size(), 23); // File Association is Windows only
#endif
        QVERIFY(d->control(QStringLiteral("Global"), 6124));
        QVERIFY(d->control(QStringLiteral("MISC"), 6331));
    }

    void screenshots()
    {
        const QString out = qEnvironmentVariable("NPP_SCREENSHOT_DIR");
        if (out.isEmpty())
            QSKIP("set NPP_SCREENSHOT_DIR to write the page screenshots");
        m_w->setUiLanguage("korean.xml");
        PreferencesDialog* d = m_w->preferencesDialog();
        d->translate();
        for (const QString& tag : d->pageTags()) {
            d->showPage(tag);
            QCoreApplication::processEvents();
            d->grab().save(QDir(out).filePath(QStringLiteral("pref-%1.png").arg(tag)));
        }
    }
};

QTEST_MAIN(TestSettings)
#include "test_settings.moc"
