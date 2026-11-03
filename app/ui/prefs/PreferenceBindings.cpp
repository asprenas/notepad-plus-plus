// Control <-> setting bindings of the Preferences pages (upstream preferenceDlg.cpp sub dialogs).
// Keys are page tag + upstream control ID (IDs repeat across pages).
#include "PreferencesDialog.h"

#include "core/config/ConfigFile.h"
#include "core/encoding/EncodingMapper.h"
#include "core/i18n/NativeLanguage.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFileDialog>
#include <QFontDatabase>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QRadioButton>
#include <QSlider>

#include <algorithm>

namespace npp::ui {

using namespace npp::core;

struct PreferenceBinder {
    PreferencesDialog& d;
    GuiConfig& g;
    QString page;

    template <class W>
    W* w(int id) const { return qobject_cast<W*>(d.control(page, id)); }
    void add(std::function<void()> load, std::function<void()> store) { d.m_bindings.push_back({std::move(load), std::move(store)}); }
    void notify(QCheckBox* c) const { if (c) QObject::connect(c, &QCheckBox::toggled, &d, [&d = this->d] { d.changed(); }); }
    void notify(QRadioButton* r) const { if (r) QObject::connect(r, &QRadioButton::toggled, &d, [&d = this->d](bool on) { if (on) d.changed(); }); }
    void notify(QLineEdit* e) const { if (e) QObject::connect(e, &QLineEdit::editingFinished, &d, [&d = this->d] { d.changed(); }); }
    void notify(QComboBox* c) const { if (c) QObject::connect(c, &QComboBox::currentIndexChanged, &d, [&d = this->d] { d.changed(); }); }
    void notify(QSlider* s) const { if (s) QObject::connect(s, &QSlider::valueChanged, &d, [&d = this->d] { d.changed(); }); }

    // bool field <-> check box (invert: "Hide" boxes on "show" fields)
    void check(int id, bool GuiConfig::*f, bool invert = false)
    {
        auto* c = w<QCheckBox>(id);
        if (!c)
            return;
        add([=, &g = this->g] { c->setChecked((g.*f) != invert); }, [=, &g = this->g] { g.*f = c->isChecked() != invert; });
        notify(c);
    }
    // int / string field <-> radio buttons
    template <class T>
    void radios(T GuiConfig::*f, std::initializer_list<std::pair<int, T>> map)
    {
        std::vector<std::pair<QRadioButton*, T>> rs;
        for (auto& [id, v] : map)
            if (auto* r = w<QRadioButton>(id))
                rs.emplace_back(r, v);
        add([=, &g = this->g] {
            for (auto& [r, v] : rs)
                r->setChecked(g.*f == v);
        }, [=, &g = this->g] {
            for (auto& [r, v] : rs)
                if (r->isChecked())
                    g.*f = v;
        });
        for (auto& [r, v] : rs)
            notify(r);
    }
    void editInt(int id, int GuiConfig::*f, int min, int max)
    {
        auto* e = w<QLineEdit>(id);
        if (!e)
            return;
        add([=, &g = this->g] { e->setText(QString::number(g.*f)); }, [=, &g = this->g] {
            bool ok = false;
            const int v = e->text().toInt(&ok);
            if (ok)
                g.*f = std::clamp(v, min, max);
        });
        notify(e);
    }
    void editStr(int id, std::string GuiConfig::*f)
    {
        auto* e = w<QLineEdit>(id);
        if (!e)
            return;
        add([=, &g = this->g] { e->setText(QString::fromStdString(g.*f)); }, [=, &g = this->g] { g.*f = e->text().toStdString(); });
        notify(e);
    }
    void slider(int id, int GuiConfig::*f, int min, int max)
    {
        auto* s = w<QSlider>(id);
        if (!s)
            return;
        s->setRange(min, max);
        add([=, &g = this->g] { s->setValue(g.*f); }, [=, &g = this->g] { g.*f = s->value(); });
        notify(s);
    }
    void enableIf(int id, std::function<bool()> cond)
    {
        if (QWidget* x = d.control(page, id))
            add([x, cond] { x->setEnabled(cond()); }, {});
    }
    void browseDir(int buttonId, int editId)
    {
        auto* b = w<QPushButton>(buttonId);
        auto* e = w<QLineEdit>(editId);
        if (!b || !e)
            return;
        QObject::connect(b, &QPushButton::clicked, &d, [&d = this->d, e] {
            const QString dir = QFileDialog::getExistingDirectory(&d, QString(), e->text());
            if (!dir.isEmpty()) {
                e->setText(QDir::toNativeSeparators(dir));
                d.changed();
            }
        });
    }
};

void PreferencesDialog::setupBindings()
{
    ConfigFile& cfg = *m_ctx.config;
    GuiConfig& g = cfg.gui;
    auto b = [&](const char* tag) { return PreferenceBinder{*this, g, QString::fromLatin1(tag)}; };

    // --- General
    {
        auto p = b("Global");
        p.check(6122, &GuiConfig::menuBarShow, true);
        p.check(6132, &GuiConfig::hideMenuRightShortcuts);
        p.check(6134, &GuiConfig::statusBarShow, true);
        if (auto* lang = p.w<QComboBox>(6124); lang && m_ctx.availableLanguages) {
            const auto langs = m_ctx.availableLanguages();
            lang->addItem(QStringLiteral("English"), QString());
            for (const auto& [name, file] : langs)
                if (QString::fromStdString(file).compare(QLatin1String("english.xml"), Qt::CaseInsensitive) != 0)
                    lang->addItem(QString::fromStdString(name), QString::fromStdString(file));
            p.add([=, this] {
                const QString cur = m_ctx.currentLanguageFile ? QString::fromStdString(m_ctx.currentLanguageFile()) : QString();
                const int i = lang->findData(cur, Qt::UserRole, Qt::MatchFixedString);
                lang->setCurrentIndex(i < 0 ? 0 : i);
            }, {});
            connect(lang, &QComboBox::activated, this, [this, lang] {
                if (m_ctx.setLanguageFile)
                    m_ctx.setLanguageFile(lang->currentData().toString().toStdString());
                translate();
                emit settingsChanged();
            });
        }
    }

    // --- Toolbar
    {
        auto p = b("Toolbar");
        p.check(6102, &GuiConfig::toolbarShow, true);
        p.radios<std::string>(&GuiConfig::toolbarIconSet, {{6103, "small"}, {6104, "large"}, {6129, "small2"}, {6130, "large2"}, {6105, "standard"}});
        p.radios<bool>(&GuiConfig::toolbarFluentMono, {{6012, true}, {6013, false}});
        p.radios<int>(&GuiConfig::toolbarFluentColor, {{6015, 0}, {6016, 1}, {6017, 2}, {6018, 3}, {6019, 4}, {6020, 5}, {6021, 6}, {6022, 7}, {6023, 8}, {6024, 9}});
    }

    // --- Tab Bar
    {
        auto p = b("Tabbar");
        p.check(6118, &GuiConfig::tabHide);
        p.check(6120, &GuiConfig::tabVertical);
        p.check(6119, &GuiConfig::tabMultiLine);
        p.check(6108, &GuiConfig::tabDragAndDrop, true);
        p.check(6113, &GuiConfig::tabDoubleClickToClose);
        p.check(6121, &GuiConfig::tabQuitOnEmpty);
        p.editInt(6137, &GuiConfig::tabCompactLabelLen, 0, 200);
        p.check(6107, &GuiConfig::tabReduce);
        p.check(6109, &GuiConfig::tabDrawInactiveTab);
        p.check(6110, &GuiConfig::tabDrawTopBar);
        p.check(6112, &GuiConfig::tabCloseButton);
        p.check(6115, &GuiConfig::tabPinButton);
        p.check(6135, &GuiConfig::tabShowOnlyPinnedButton);
        p.check(6111, &GuiConfig::tabButtonsOnInactiveTabs);
        p.enableIf(6135, [&g] { return g.tabPinButton; });
    }

    // --- Editing 1
    {
        auto p = b("Scintillas");
        p.radios<int>(&GuiConfig::currentLineIndicator, {{6652, 0}, {6653, 1}, {6654, 2}});
        p.slider(6656, &GuiConfig::currentLineFrameWidth, 1, 6);
        p.enableIf(6656, [&g] { return g.currentLineIndicator == 2; });
        if (auto* c = p.w<QComboBox>(6218)) {
            c->addItems({QStringLiteral("0"), QStringLiteral("1"), QStringLiteral("2"), QStringLiteral("3"), tr("Block"), tr("Block After")});
            p.add([=, &g] { c->setCurrentIndex(std::clamp(g.caretWidth, 0, 5)); }, [=, &g] { g.caretWidth = c->currentIndex(); });
            p.notify(c);
        }
        // blink rate: slider from slow (left) to fast (right) like upstream (0 = no blink)
        if (auto* s = p.w<QSlider>(6220)) {
            s->setRange(0, 2450);
            p.add([=, &g] { s->setValue(g.caretBlinkRate <= 0 ? 0 : 2500 - std::clamp(g.caretBlinkRate, 50, 2500)); },
                [=, &g] { g.caretBlinkRate = s->value() == 0 ? 0 : 2500 - s->value(); });
            p.notify(s);
        }
        p.radios<std::string>(&GuiConfig::lineWrapMethod, {{6228, "default"}, {6229, "aligned"}, {6230, "indent"}});
        p.check(6215, &GuiConfig::smoothFont);
        p.check(6245, &GuiConfig::virtualSpace);
        p.check(6246, &GuiConfig::enableFoldCmdToggable);
        p.check(6239, &GuiConfig::rightClickKeepsSelection);
        p.check(6214, &GuiConfig::lineCopyCutWithoutSelection);
        p.check(6225, &GuiConfig::selectedTextFgSingleColor);
        p.check(6236, &GuiConfig::scrollBeyondLastLine);
        p.check(6234, &GuiConfig::disableAdvancedScrolling);
        p.check(6266, &GuiConfig::disableSelectedTextDragDrop);
    }

    // --- Editing 2
    {
        auto p = b("Scintillas2");
        p.check(6522, &GuiConfig::multiSelection);
        p.check(6523, &GuiConfig::columnSelToMultiEdit);
        p.enableIf(6523, [&g] { return g.multiSelection; });
        p.radios<int>(&GuiConfig::eolMode, {{6248, 1}, {6249, 0}});
        p.radios<int>(&GuiConfig::npcMode, {{6254, 1}, {6255, 2}});
        p.check(6256, &GuiConfig::npcCustomColor);
        p.check(6258, &GuiConfig::npcIncludeCcUniEol);
        p.check(6259, &GuiConfig::npcNoInputC0);
        for (int id : {6251, 6257})
            if (auto* btn = p.w<QPushButton>(id))
                connect(btn, &QPushButton::clicked, this, [this] { if (m_ctx.openStyleConfigurator) m_ctx.openStyleConfigurator(); });
    }

    // --- Dark Mode
    {
        auto p = b("DarkMode");
        auto* light = p.w<QRadioButton>(7131), *dark = p.w<QRadioButton>(7132), *follow = p.w<QRadioButton>(7133);
        if (light && dark && follow) {
            p.add([=, &g] {
                follow->setChecked(g.darkFollowSystem);
                dark->setChecked(!g.darkFollowSystem && g.darkModeEnabled);
                light->setChecked(!g.darkFollowSystem && !g.darkModeEnabled);
            }, [=, &g] {
                g.darkFollowSystem = follow->isChecked();
                if (!g.darkFollowSystem)
                    g.darkModeEnabled = dark->isChecked();
            });
            for (auto* r : {light, dark, follow})
                p.notify(r);
#if !defined(Q_OS_WIN)
            follow->setText(tr("Follow system"));
#endif
        }
        p.radios<int>(&GuiConfig::darkModeColorTone, {{7102, 0}, {7103, 1}, {7104, 2}, {7105, 3}, {7107, 4}, {7108, 5}, {7109, 6}, {7115, 32}});
        if (auto* reset = p.w<QPushButton>(7130))
            connect(reset, &QPushButton::clicked, this, [this, &g] {
                const GuiConfig def;
                g.darkCustomColorTop = def.darkCustomColorTop;
                g.darkCustomColorMenuHotTrack = def.darkCustomColorMenuHotTrack;
                g.darkCustomColorActive = def.darkCustomColorActive;
                g.darkCustomColorMain = def.darkCustomColorMain;
                g.darkCustomColorError = def.darkCustomColorError;
                g.darkCustomColorText = def.darkCustomColorText;
                g.darkCustomColorDarkText = def.darkCustomColorDarkText;
                g.darkCustomColorDisabledText = def.darkCustomColorDisabledText;
                g.darkCustomColorLinkText = def.darkCustomColorLinkText;
                g.darkCustomColorEdge = def.darkCustomColorEdge;
                g.darkCustomColorHotEdge = def.darkCustomColorHotEdge;
                g.darkCustomColorDisabledEdge = def.darkCustomColorDisabledEdge;
                emit settingsChanged();
            });
    }

    // --- Margins / Border / Edge
    {
        auto p = b("MarginsBorderEdge");
        p.radios<std::string>(&GuiConfig::folderMarkStyle, {{6202, "simple"}, {6203, "arrow"}, {6204, "circle"}, {6205, "box"}, {6226, "none"}});
        p.slider(6233, &GuiConfig::borderWidth, 0, 30);
        p.check(6235, &GuiConfig::borderEdge, true);
        p.editStr(6238, &GuiConfig::edgeMultiColumnPos);
        p.check(6213, &GuiConfig::edgeBgMode);
        // change history: 0 off, 1 margin, 2 indicator, 3 both
        auto* margin = p.w<QCheckBox>(6223), *indic = p.w<QCheckBox>(6296);
        if (margin && indic) {
            p.add([=, &g] {
                margin->setChecked(g.changeHistory & 1);
                indic->setChecked(g.changeHistory & 2);
            }, [=, &g] { g.changeHistory = (margin->isChecked() ? 1 : 0) | (indic->isChecked() ? 2 : 0); });
            p.notify(margin);
            p.notify(indic);
        }
        p.check(6206, &GuiConfig::lineNumberMargin);
        p.radios<bool>(&GuiConfig::lineNumberDynamicWidth, {{6292, true}, {6293, false}});
        p.slider(6240, &GuiConfig::paddingLeft, 0, 30);
        p.slider(6241, &GuiConfig::paddingRight, 0, 30);
        p.slider(6224, &GuiConfig::distractionFreeDivPart, 3, 9);
        p.check(6207, &GuiConfig::bookmarkMargin);
    }

    // --- New Document
    {
        auto p = b("NewDoc");
        p.radios<int>(&GuiConfig::newDocFormat, {{6402, 0}, {6404, 1}, {6403, 2}});
        p.check(6420, &GuiConfig::newDocOpenAnsiAsUtf8);
        // encoding: UniMode values (0 ANSI, 1 UTF-8 BOM, 2 UTF-16BE, 3 UTF-16LE, 4 UTF-8); 6421 = other code page
        auto* other = p.w<QRadioButton>(6421);
        auto* cps = p.w<QComboBox>(6422);
        const std::pair<int, int> encs[] = {{6406, 0}, {6407, 4}, {6408, 1}, {6409, 2}, {6410, 3}};
        if (cps) {
            for (size_t i = 0; i < EncodingMapper::table().size(); ++i) {
                const std::string_view a = EncodingMapper::table()[i].aliasList;
                cps->addItem(QString::fromStdString(std::string(a.substr(0, a.find(' ')))), EncodingMapper::table()[i].codePage);
            }
        }
        p.add([=, &g, this] {
            for (auto [id, v] : encs)
                if (auto* r = PreferenceBinder{*this, g, QStringLiteral("NewDoc")}.w<QRadioButton>(id))
                    r->setChecked(g.newDocCodepage == -1 && g.newDocEncoding == v);
            if (other)
                other->setChecked(g.newDocCodepage != -1);
            if (cps) {
                const int i = cps->findData(g.newDocCodepage);
                cps->setCurrentIndex(std::max(0, i));
                cps->setEnabled(g.newDocCodepage != -1);
            }
        }, [=, &g, this] {
            for (auto [id, v] : encs)
                if (auto* r = PreferenceBinder{*this, g, QStringLiteral("NewDoc")}.w<QRadioButton>(id); r && r->isChecked()) {
                    g.newDocEncoding = v;
                    g.newDocCodepage = -1;
                }
            if (other && other->isChecked() && cps)
                g.newDocCodepage = cps->currentData().toInt();
        });
        for (auto [id, v] : encs)
            p.notify(p.w<QRadioButton>(id));
        p.notify(other);
        p.notify(cps);
        if (auto* lang = p.w<QComboBox>(6412); lang && m_ctx.languageNames) {
            for (const auto& n : m_ctx.languageNames())
                lang->addItem(QString::fromStdString(n));
            p.add([=, &g] { lang->setCurrentIndex(std::clamp(g.newDocLang, 0, lang->count() - 1)); }, [=, &g] { g.newDocLang = lang->currentIndex(); });
            p.notify(lang);
        }
        p.check(6432, &GuiConfig::newDocAddOnStartup);
        p.check(6433, &GuiConfig::newDocUseContentAsTabName);
    }

    // --- Default Directory
    {
        auto p = b("DefaultDir");
        p.radios<int>(&GuiConfig::openSaveDir, {{6414, 0}, {6415, 1}, {6416, 2}});
        p.editStr(6417, &GuiConfig::defaultDir);
        p.enableIf(6417, [&g] { return g.openSaveDir == 2; });
        p.enableIf(6418, [&g] { return g.openSaveDir == 2; });
        p.browseDir(6418, 6417);
        p.check(6431, &GuiConfig::folderDroppedOpenFiles);
    }

    // --- Recent Files History
    {
        auto p = b("RecentFilesHistory");
        p.check(6305, &GuiConfig::checkHistoryFiles, true);
        RecentFiles& h = cfg.history;
        if (auto* e = p.w<QLineEdit>(6461)) {
            p.add([=, &h] { e->setText(QString::number(h.maxFiles)); }, [=, &h] { h.maxFiles = std::clamp(e->text().toInt(), 0, 30); });
            p.notify(e);
        }
        if (auto* c = p.w<QCheckBox>(6424)) {
            p.add([=, &h] { c->setChecked(h.inSubMenu); }, [=, &h] { h.inSubMenu = c->isChecked(); });
            p.notify(c);
        }
        auto* only = p.w<QRadioButton>(6425), *full = p.w<QRadioButton>(6426), *custom = p.w<QRadioButton>(6427);
        auto* len = p.w<QLineEdit>(6463);
        if (only && full && custom && len) {
            p.add([=, &h] {
                only->setChecked(h.customLength == 0);
                full->setChecked(h.customLength == RecentFiles::kShowFullPath);
                custom->setChecked(h.customLength > 0);
                len->setText(QString::number(h.customLength > 0 ? h.customLength : 100));
                len->setEnabled(h.customLength > 0);
            }, [=, &h] {
                h.customLength = only->isChecked() ? 0 : full->isChecked() ? RecentFiles::kShowFullPath : std::max(1, len->text().toInt());
            });
            for (auto* r : {only, full, custom})
                p.notify(r);
            p.notify(len);
        }
    }

    // --- Language (menu) / Indentation
    {
        auto p = b("Language");
        p.check(6507, &GuiConfig::langMenuCompact);
        p.check(6335, &GuiConfig::backslashIsEscapeForSql);
    }
    {
        auto p = b("Indentation");
        p.editInt(6513, &GuiConfig::tabSize, 1, 32);
        p.radios<bool>(&GuiConfig::tabReplacedBySpace, {{6311, false}, {6302, true}});
        p.check(6512, &GuiConfig::backspaceUnindent);
        p.radios<int>(&GuiConfig::maintainIndent, {{7162, 0}, {7163, 1}, {7164, 2}});
        if (auto* list = p.w<QListWidget>(6509)) { // "[Default]" + languages: per language tab settings are in langs.xml
            list->addItem(tr("[Default]"));
            list->setCurrentRow(0);
        }
        if (auto* def = p.w<QCheckBox>(6510))
            def->setEnabled(false);
    }

    // --- Highlighting
    {
        auto p = b("Highlighting");
        p.check(6352, &GuiConfig::markAllMatchCase);
        p.check(6353, &GuiConfig::markAllWholeWord);
        p.check(6327, &GuiConfig::tagsMatchHighlight);
        p.check(6328, &GuiConfig::tagAttrHighlight);
        p.check(6330, &GuiConfig::highlightNonHtmlZone);
        p.enableIf(6328, [&g] { return g.tagsMatchHighlight; });
        p.enableIf(6330, [&g] { return g.tagsMatchHighlight; });
        p.check(6326, &GuiConfig::smartHighlight);
        p.check(6340, &GuiConfig::smartHighlightOnAnotherView);
        p.check(6332, &GuiConfig::smartHighlightMatchCase);
        p.check(6338, &GuiConfig::smartHighlightWholeWord);
        p.check(6339, &GuiConfig::smartHighlightUseFindSettings);
        for (int id : {6340, 6332, 6338, 6339})
            p.enableIf(id, [&g] { return g.smartHighlight; });
    }

    // --- Print (used by the printing of phase 5)
    {
        auto p = b("Print");
        p.check(6601, &GuiConfig::printLineNumber);
        p.check(6616, &GuiConfig::printFormFeedPageBreak);
        p.radios<int>(&GuiConfig::printOption, {{6603, 0}, {6604, 1}, {6605, 2}, {6606, 3}});
        p.editInt(6609, &GuiConfig::printMarginTop, 0, 1000);
        p.editInt(6608, &GuiConfig::printMarginLeft, 0, 1000);
        p.editInt(6610, &GuiConfig::printMarginRight, 0, 1000);
        p.editInt(6611, &GuiConfig::printMarginBottom, 0, 1000);
        p.editStr(6701, &GuiConfig::printHeaderLeft);
        p.editStr(6702, &GuiConfig::printHeaderMiddle);
        p.editStr(6703, &GuiConfig::printHeaderRight);
        p.editStr(6712, &GuiConfig::printFooterLeft);
        p.editStr(6713, &GuiConfig::printFooterMiddle);
        p.editStr(6714, &GuiConfig::printFooterRight);
        const QStringList fonts = QFontDatabase::families();
        auto fontCombo = [&](int nameId, int sizeId, std::string GuiConfig::*name, int GuiConfig::*size) {
            if (auto* c = p.w<QComboBox>(nameId)) {
                c->addItem(QString());
                c->addItems(fonts);
                p.add([=, &g] { c->setCurrentText(QString::fromStdString(g.*name)); }, [=, &g] { g.*name = c->currentText().toStdString(); });
                p.notify(c);
            }
            if (auto* c = p.w<QComboBox>(sizeId)) {
                c->addItem(QString());
                for (int s = 6; s <= 14; ++s)
                    c->addItem(QString::number(s));
                p.add([=, &g] { c->setCurrentText(g.*size ? QString::number(g.*size) : QString()); }, [=, &g] { g.*size = c->currentText().toInt(); });
                p.notify(c);
            }
        };
        fontCombo(6704, 6705, &GuiConfig::printHeaderFontName, &GuiConfig::printHeaderFontSize);
        fontCombo(6715, 6716, &GuiConfig::printFooterFontName, &GuiConfig::printFooterFontSize);
        auto styleChecks = [&](int boldId, int italicId, int GuiConfig::*style) {
            auto* bold = p.w<QCheckBox>(boldId), *italic = p.w<QCheckBox>(italicId);
            if (!bold || !italic)
                return;
            p.add([=, &g] { bold->setChecked(g.*style & 1); italic->setChecked(g.*style & 2); },
                [=, &g] { g.*style = (bold->isChecked() ? 1 : 0) | (italic->isChecked() ? 2 : 0); });
            p.notify(bold);
            p.notify(italic);
        };
        styleChecks(6706, 6707, &GuiConfig::printHeaderFontStyle);
        styleChecks(6717, 6718, &GuiConfig::printFooterFontStyle);
        if (auto* vars = p.w<QComboBox>(6724)) // header / footer variables
            vars->addItems({QStringLiteral("$(FULL_CURRENT_PATH)"), QStringLiteral("$(FILE_NAME)"), QStringLiteral("$(SHORT_DATE)"),
                            QStringLiteral("$(LONG_DATE)"), QStringLiteral("$(TIME)"), QStringLiteral("$(CURRENT_PRINTING_PAGE)")});
    }

    // --- Searching
    {
        auto p = b("Searching");
        p.editInt(6911, &GuiConfig::inSelectionAutocheckThreshold, 0, 1 << 20);
        p.check(6913, &GuiConfig::fillDirFieldFromActiveDoc);
        p.check(6908, &GuiConfig::fillFindFieldWithSelected);
        p.editInt(6915, &GuiConfig::fillFindWhatThreshold, 1, 1 << 20);
        p.check(6909, &GuiConfig::fillFindFieldSelectCaret);
        p.check(6902, &GuiConfig::monospacedFontFindDlg);
        p.check(6903, &GuiConfig::findDlgAlwaysVisible);
        p.check(6904, &GuiConfig::confirmReplaceInAllOpenDocs);
        p.check(6905, &GuiConfig::replaceStopsWithoutFindingNext);
        p.check(6906, &GuiConfig::finderShowOnlyOneEntryPerFoundLine);
        p.check(6917, &GuiConfig::fifIgnoreUnsavedChanges);
    }

    // --- Backup
    {
        auto p = b("Backup");
        p.check(6309, &GuiConfig::rememberLastSession);
        p.check(6818, &GuiConfig::snapshotMode);
        if (auto* e = p.w<QLineEdit>(6820)) { // seconds in the UI, milliseconds in config.xml
            p.add([=, &g] { e->setText(QString::number(g.snapshotBackupTiming / 1000)); },
                [=, &g] { g.snapshotBackupTiming = std::max(1, e->text().toInt()) * 1000; });
            p.notify(e);
        }
        p.enableIf(6818, [&g] { return g.rememberLastSession; });
        p.enableIf(6820, [&g] { return g.rememberLastSession && g.snapshotMode; });
        p.check(6825, &GuiConfig::keepSessionAbsentFileEntries);
        p.radios<int>(&GuiConfig::backupAction, {{6315, 0}, {6316, 1}, {6317, 2}});
        p.check(6804, &GuiConfig::backupUseCustomDir);
        p.editStr(6805, &GuiConfig::backupDir);
        p.enableIf(6805, [&g] { return g.backupUseCustomDir; });
        p.enableIf(6806, [&g] { return g.backupUseCustomDir; });
        p.browseDir(6806, 6805);
        if (auto* path = p.w<QLineEdit>(6823))
            path->setReadOnly(true);
    }

    // --- Auto-Completion
    {
        auto p = b("AutoCompletion");
        auto* enable = p.w<QCheckBox>(6808);
        auto* fn = p.w<QRadioButton>(6809), *word = p.w<QRadioButton>(6810), *both = p.w<QRadioButton>(6816);
        if (enable && fn && word && both) {
            p.add([=, &g] {
                enable->setChecked(g.autocAction != 0);
                fn->setChecked(g.autocAction == 1);
                word->setChecked(g.autocAction == 2);
                both->setChecked(g.autocAction == 3 || g.autocAction == 0);
                for (auto* r : {fn, word, both})
                    r->setEnabled(g.autocAction != 0);
            }, [=, &g] { g.autocAction = !enable->isChecked() ? 0 : fn->isChecked() ? 1 : word->isChecked() ? 2 : 3; });
            p.notify(enable);
            for (auto* r : {fn, word, both})
                p.notify(r);
        }
        p.check(6872, &GuiConfig::autocBrief);
        p.check(6815, &GuiConfig::autocFuncParams);
        p.slider(6873, &GuiConfig::autocFromLen, 1, 9);
        p.check(6870, &GuiConfig::autocInsertWithTab);
        p.check(6871, &GuiConfig::autocInsertWithEnter);
        p.check(6824, &GuiConfig::autocIgnoreNumbers);
        p.check(6852, &GuiConfig::autoInsertParentheses);
        p.check(6853, &GuiConfig::autoInsertBrackets);
        p.check(6854, &GuiConfig::autoInsertCurlyBrackets);
        p.check(6855, &GuiConfig::autoInsertDoubleQuotes);
        p.check(6856, &GuiConfig::autoInsertQuotes);
        p.check(6857, &GuiConfig::autoInsertHtmlXmlTag);
        const std::pair<int, int> pairs[] = {{6861, 6862}, {6864, 6865}, {6867, 6868}};
        std::vector<std::pair<QLineEdit*, QLineEdit*>> edits;
        for (auto [o, c] : pairs)
            if (auto* eo = p.w<QLineEdit>(o), *ec = p.w<QLineEdit>(c); eo && ec) {
                eo->setMaxLength(1);
                ec->setMaxLength(1);
                edits.emplace_back(eo, ec);
                p.notify(eo);
                p.notify(ec);
            }
        p.add([=, &g] {
            for (size_t i = 0; i < edits.size(); ++i) {
                const bool has = i < g.autoInsertUserPairs.size();
                edits[i].first->setText(has ? QString(QChar(g.autoInsertUserPairs[i].first)) : QString());
                edits[i].second->setText(has ? QString(QChar(g.autoInsertUserPairs[i].second)) : QString());
            }
        }, [=, &g] {
            g.autoInsertUserPairs.clear();
            for (auto [eo, ec] : edits)
                if (!eo->text().isEmpty() && !ec->text().isEmpty() && eo->text()[0].unicode() < 128 && ec->text()[0].unicode() < 128)
                    g.autoInsertUserPairs.emplace_back(static_cast<char>(eo->text()[0].unicode()), static_cast<char>(ec->text()[0].unicode()));
        });
    }

    // --- Multi-Instance & Date
    {
        auto p = b("MultiInstance");
        p.radios<int>(&GuiConfig::multiInstSetting, {{6154, 0}, {6152, 1}, {6153, 2}});
        p.check(6175, &GuiConfig::dateTimeReverseOrder);
        p.editStr(6173, &GuiConfig::dateTimeFormat);
        p.check(6183, &GuiConfig::keepClipboardHistoryPanel);
        p.check(6184, &GuiConfig::keepDocumentListPanel);
        p.check(6185, &GuiConfig::keepCharacterPanel);
        p.check(6186, &GuiConfig::keepFolderAsWorkspacePanel);
        p.check(6187, &GuiConfig::keepProjectPanels);
        p.check(6188, &GuiConfig::keepDocumentMapPanel);
        p.check(6189, &GuiConfig::keepFunctionListPanel);
        p.check(6190, &GuiConfig::keepPluginPanels);
    }

    // --- Delimiter (same IDs as some Editing 2 controls: keyed by page)
    {
        auto p = b("Delimiter");
        p.radios<bool>(&GuiConfig::wordCharDefault, {{6162, true}, {6163, false}});
        p.editStr(6164, &GuiConfig::customWordChars);
        p.enableIf(6164, [&g] { return !g.wordCharDefault; });
        auto delim = [&](int id, int GuiConfig::*f) {
            if (auto* e = p.w<QLineEdit>(id)) {
                e->setMaxLength(1);
                p.add([=, &g] { e->setText(QString(QChar(g.*f))); }, [=, &g] {
                    if (!e->text().isEmpty())
                        g.*f = e->text()[0].unicode();
                });
                p.notify(e);
            }
        };
        delim(6253, &GuiConfig::leftmostDelimiter);
        delim(6254, &GuiConfig::rightmostDelimiter);
        p.check(6256, &GuiConfig::delimiterOnEntireDocument);
    }

    // --- Performance
    {
        auto p = b("Performance");
        p.check(7143, &GuiConfig::largeFileEnabled);
        p.editInt(7145, &GuiConfig::largeFileSizeMB, 1, 2046);
        p.check(7150, &GuiConfig::largeFileDeactivateWordWrap);
        p.check(7148, &GuiConfig::largeFileAllowAutoCompletion);
        p.check(7149, &GuiConfig::largeFileAllowSmartHilite);
        p.check(7147, &GuiConfig::largeFileAllowBraceMatch);
        p.check(7151, &GuiConfig::largeFileAllowClickableLink);
        p.check(7152, &GuiConfig::largeFileSuppress2GBWarning);
        for (int id : {7145, 7150, 7148, 7149, 7147, 7151})
            p.enableIf(id, [&g] { return g.largeFileEnabled; });
    }

    // --- Cloud & Link
    {
        auto p = b("Cloud");
        // cloud settings path: kept by the application (settings folder choice, phase 6)
        if (auto* none = p.w<QRadioButton>(6263))
            none->setChecked(true);
        p.browseDir(6269, 6268);
        auto* enable = p.w<QCheckBox>(6319), *noUnderline = p.w<QCheckBox>(6320), *fullbox = p.w<QCheckBox>(6350);
        if (enable && noUnderline) {
            // styleUrl: 0 disabled, 1 no underline, 2 underline (upstream urlMode, full box variants 3/4)
            p.add([=, &g] {
                enable->setChecked(g.styleUrl != 0);
                noUnderline->setChecked(g.styleUrl == 1 || g.styleUrl == 3);
                if (fullbox)
                    fullbox->setChecked(g.styleUrl >= 3);
                noUnderline->setEnabled(g.styleUrl != 0);
                if (fullbox)
                    fullbox->setEnabled(g.styleUrl != 0);
            }, [=, &g] {
                if (!enable->isChecked())
                    g.styleUrl = 0;
                else
                    g.styleUrl = (noUnderline->isChecked() ? 1 : 2) + (fullbox && fullbox->isChecked() ? 2 : 0);
            });
            p.notify(enable);
            p.notify(noUnderline);
            p.notify(fullbox);
        }
        p.editStr(6265, &GuiConfig::uriSchemes);
    }

    // --- Search Engine
    {
        auto p = b("SearchEngine");
        p.radios<int>(&GuiConfig::searchEngineChoice, {{6276, 0}, {6272, 1}, {6273, 2}, {6275, 4}, {6279, 5}});
        p.editStr(6277, &GuiConfig::searchEngineCustom);
        p.enableIf(6277, [&g] { return g.searchEngineChoice == 0; });
    }

    // --- MISC.
    {
        auto p = b("MISC");
        if (auto* c = p.w<QComboBox>(6347)) { // file status auto-detection
            c->addItems({tr("Enable for all opened files"), tr("Enable for current file only"), tr("Disable")});
            p.add([=, &g] { c->setCurrentIndex(g.fileAutoDetection == cdDisabled ? 2 : (g.fileAutoDetection & cdEnabledOld) ? 1 : 0); },
                [=, &g] {
                    const int keep = g.fileAutoDetection & (cdAutoUpdate | cdGo2end);
                    g.fileAutoDetection = c->currentIndex() == 2 ? cdDisabled : (c->currentIndex() == 1 ? cdEnabledOld : cdEnabledNew) | keep;
                });
            p.notify(c);
        }
        auto* silent = p.w<QCheckBox>(6313), *toEnd = p.w<QCheckBox>(6325);
        if (silent && toEnd) {
            p.add([=, &g] {
                silent->setChecked(g.fileAutoDetection & cdAutoUpdate);
                toEnd->setChecked(g.fileAutoDetection & cdGo2end);
                silent->setEnabled(g.fileAutoDetection != cdDisabled);
                toEnd->setEnabled(g.fileAutoDetection != cdDisabled);
            }, [=, &g] {
                if (g.fileAutoDetection == cdDisabled)
                    return;
                g.fileAutoDetection = (g.fileAutoDetection & (cdEnabledOld | cdEnabledNew)) | (silent->isChecked() ? cdAutoUpdate : 0)
                                      | (toEnd->isChecked() ? cdGo2end : 0);
            });
            p.notify(silent);
            p.notify(toEnd);
        }
        p.check(6114, &GuiConfig::doTaskList);
        p.check(6117, &GuiConfig::styleMru);
        p.check(6345, &GuiConfig::docPeekOnTab);
        p.check(6346, &GuiConfig::docPeekOnMap);
        if (auto* c = p.w<QComboBox>(6307)) { // system tray action
            c->addItems({tr("No to systray"), tr("Minimize to systray"), tr("Close to systray"), tr("Minimize / Close to systray")});
            p.add([=, &g] { c->setCurrentIndex(std::clamp(g.minimizeToTray, 0, 3)); }, [=, &g] { g.minimizeToTray = c->currentIndex(); });
            p.notify(c);
        }
        if (auto* c = p.w<QComboBox>(6362)) { // rendering technology: Qt chooses the backend
            c->addItem(tr("Default"));
            c->setEnabled(false);
        }
        if (auto* c = p.w<QComboBox>(6364)) { // auto-updater: updates are not handled by the Qt port
            c->addItem(tr("Disabled"));
            c->setEnabled(false);
        }
        p.check(6360, &GuiConfig::muteSounds);
        p.check(6334, &GuiConfig::detectEncoding);
        p.check(6331, &GuiConfig::shortTitlebar);
        p.check(6361, &GuiConfig::saveAllConfirm);
        p.check(6366, &GuiConfig::fawSymlinkAllowed);
        p.editStr(6336, &GuiConfig::workspaceExt);
        p.editStr(6321, &GuiConfig::sessionExt);
    }
}

} // namespace npp::ui
