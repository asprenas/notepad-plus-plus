#include "core/buffer/FileManager.h"
#include "core/encoding/UnicodeConverter.h"
#include "core/lang/LanguageRegistry.h"
#include "core/lang/UserLanguage.h"
#include "core/util/PathUtil.h"

#include <Scintilla.h>
#include <ScintillaEditBase.h>

#include <QApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QCoreApplication>
#include <QTest>

#include <thread>

using namespace npp::core;

namespace {

class SciAdapter final : public IEditor {
public:
    explicit SciAdapter(ScintillaEditBase& e) : m_e(e) {}
    intptr_t send(unsigned int m, uintptr_t w, intptr_t l) const override { return m_e.send(m, w, l); }
private:
    ScintillaEditBase& m_e;
};

const std::string kEnc = NPP_TEST_DATA_DIR "/encodings/";

std::string tempDir()
{
    // per process: test runs of several build directories may overlap (local CI)
    const std::string d = utf8FromPath(fs::temp_directory_path()) + "/npp-core-buffer-테스트-"
                          + std::to_string(QCoreApplication::applicationPid());
    fs::create_directories(pathFromUtf8(d));
    return d;
}

} // namespace

class TestBuffer : public QObject {
    Q_OBJECT
private:
    ScintillaEditBase* m_editor = nullptr;
    std::unique_ptr<SciAdapter> m_scratch;
    LanguageRegistry m_langs;
    UserLanguageRegistry m_udls;

private slots:
    void initTestCase()
    {
        m_editor = new ScintillaEditBase;
        m_scratch = std::make_unique<SciAdapter>(*m_editor);
        QVERIFY(m_langs.load(NPP_LEGACY_SRC_DIR "/langs.model.xml"));
        m_udls.loadAll({}, NPP_ROOT_DIR "/PowerEditor/bin/userDefineLangs");
    }
    void cleanupTestCase()
    {
        m_scratch.reset();
        delete m_editor;
        fs::remove_all(pathFromUtf8(tempDir()));
    }

    void eolDetection()
    {
        QCOMPARE(detectEol("a\r\nb", 4), EolType::Windows);
        QCOMPARE(detectEol("a\rb", 3), EolType::Mac);
        QCOMPARE(detectEol("a\nb\r\n", 5), EolType::Unix);
        QCOMPARE(detectEol("abc", 3, EolType::Unix), EolType::Unix);
        QCOMPARE(FileTime::fromParts(FileTime{123456789012345}.low(), FileTime{123456789012345}.high()).value,
                 int64_t(123456789012345));
    }

    // Every sample: load (detection or forced code page), UTF-8 inside Scintilla, byte exact save.
    void encodingsRoundTrip_data()
    {
        QTest::addColumn<QString>("file");
        QTest::addColumn<int>("codePage");
        QFile f(QString::fromStdString(kEnc + "manifest.json"));
        QVERIFY(f.open(QIODevice::ReadOnly));
        for (const auto& v : QJsonDocument::fromJson(f.readAll()).array())
            QTest::newRow(qPrintable(v.toObject()["file"].toString()))
                << v.toObject()["file"].toString() << v.toObject()["codepage"].toInt();
    }

    void encodingsRoundTrip()
    {
        QFETCH(QString, file);
        QFETCH(int, codePage);
        FileManager fm(*m_scratch, {}, &m_langs);
        LoadOptions o;
        o.encodingOverride = codePage; // -1 for Unicode samples: detection
        LoadError err;
        Buffer* b = fm.loadFile(kEnc + file.toStdString(), o, &err);
        QVERIFY(b);
        QCOMPARE(err, LoadError::None);
        QCOMPARE(fm.utf8Content(*b), readFileContent(kEnc + file.toStdString() + ".utf8").value());
        QCOMPARE(fm.encodedContent(*b), readFileContent(kEnc + file.toStdString()).value());
        QCOMPARE(b->eol, EolType::Windows);

        // and through a real save
        const std::string out = tempDir() + "/rt-" + file.toStdString();
        QCOMPARE(fm.save(*b, out, true), SaveStatus::Ok);
        QCOMPARE(readFileContent(out).value(), readFileContent(kEnc + file.toStdString()).value());
    }

    void detectionWithoutOverride()
    {
        FileManager fm(*m_scratch, {}, &m_langs);
        Buffer* sjis = fm.loadFile(kEnc + "shift_jis.txt");
        QVERIFY(sjis);
        QCOMPARE(sjis->encoding, 932);
        QCOMPARE(fm.utf8Content(*sjis), readFileContent(kEnc + "shift_jis.txt.utf8").value());
        Buffer* u16 = fm.loadFile(kEnc + "utf16le-bom.txt");
        QCOMPARE(u16->uniMode, UniMode::Utf16LE);
        QCOMPARE(u16->encoding, -1);
        Buffer* ascii = fm.loadFile(kEnc + "ascii.txt");
        QCOMPARE(ascii->uniMode, UniMode::Utf8); // openAnsiAsUtf8 (upstream default)
    }

    void newDocumentsAndSave()
    {
        FileManagerSettings s;
        s.newDocEol = EolType::Unix;
        FileManager fm(*m_scratch, s, &m_langs);
        Buffer* a = fm.newDocument();
        Buffer* b = fm.newDocument();
        QCOMPARE(a->untitledName, std::string("new 1"));
        QCOMPARE(b->untitledName, std::string("new 2"));
        fm.close(*a);
        QCOMPARE(fm.newDocument()->untitledName, std::string("new 1")); // lowest free number

        // write text through the document, then "Save As"
        {
            m_scratch->send(SCI_SETDOCPOINTER, 0, b->document);
            m_scratch->sendStr(SCI_SETTEXT, 0, "print('한글')\n");
            m_scratch->send(SCI_SETDOCPOINTER, 0, 0);
        }
        b->dirty = true;
        const std::string path = tempDir() + "/script.py";
        QCOMPARE(fm.save(*b, path), SaveStatus::Ok);
        QVERIFY(!b->isUntitled());
        QVERIFY(!b->dirty);
        QCOMPARE(b->status, DocStatus::Regular);
        QCOMPARE(readFileContent(path).value(), std::string("print('한글')\n"));
        QVERIFY(fm.findByPath(path) == b);
        QVERIFY(fm.loadFile(path) == b); // already open

        // switch to UTF-16 BE with BOM and save again
        b->uniMode = UniMode::Utf16BE;
        QCOMPARE(fm.save(*b, path), SaveStatus::Ok);
        QCOMPARE(readFileContent(path).value(), encodeFromUtf8("print('한글')\n", UniMode::Utf16BE));
        QVERIFY(fm.reload(*b));
        QCOMPARE(b->uniMode, UniMode::Utf16BE);
        QCOMPARE(fm.utf8Content(*b), std::string("print('한글')\n"));
    }

    void externalChanges()
    {
        FileManager fm(*m_scratch, {}, &m_langs);
        const std::string path = tempDir() + "/watched.txt";
        QVERIFY(writeFileContent(path, "one"));
        Buffer* b = fm.loadFile(path);
        QVERIFY(b);
        QCOMPARE(fm.checkFileState(*b), FileChange::None);

        // modify with a newer timestamp
        QVERIFY(writeFileContent(path, "two"));
        fs::last_write_time(pathFromUtf8(path), fs::file_time_type::clock::now() + std::chrono::seconds(5));
        QCOMPARE(fm.checkFileState(*b), FileChange::Modified);
        QCOMPARE(fm.checkFileState(*b), FileChange::None); // timestamp adopted
        QVERIFY(fm.reload(*b));
        QCOMPARE(fm.utf8Content(*b), std::string("two"));

        fs::remove(pathFromUtf8(path));
        QCOMPARE(fm.checkFileState(*b), FileChange::Deleted);
        QCOMPARE(b->status, DocStatus::Deleted);
        QVERIFY(writeFileContent(path, "back"));
        QCOMPARE(fm.checkFileState(*b), FileChange::Restored);
    }

    void backups()
    {
        FileManager fm(*m_scratch, {}, &m_langs);
        Buffer* b = fm.newDocument();
        m_scratch->send(SCI_SETDOCPOINTER, 0, b->document);
        m_scratch->sendStr(SCI_SETTEXT, 0, "unsaved 내용");
        m_scratch->send(SCI_SETDOCPOINTER, 0, 0);

        std::tm t{};
        t.tm_year = 126; t.tm_mon = 10; t.tm_mday = 2; t.tm_hour = 9; t.tm_min = 5; t.tm_sec = 7;
        QCOMPARE(FileManager::backupFileName(*b, std::mktime(&t)), std::string("new 1@2026-11-02_090507"));

        const std::string dir = tempDir() + "/backup";
        b->dirty = false;
        QVERIFY(fm.backup(*b, dir));
        QVERIFY(b->backupPath.empty()); // clean: nothing written
        b->dirty = true;
        QVERIFY(fm.backup(*b, dir));
        QVERIFY(pathExists(b->backupPath, PathKind::File));
        QCOMPARE(readFileContent(b->backupPath).value(), std::string("unsaved 내용"));

        // session restore of the untitled document from its backup
        FileManager fm2(*m_scratch, {}, &m_langs);
        Buffer* r = fm2.loadBackup(b->backupPath, {}, "new 1", -1);
        QVERIFY(r);
        QVERIFY(r->dirty);
        QCOMPARE(r->untitledName, std::string("new 1"));
        QCOMPARE(fm2.utf8Content(*r), std::string("unsaved 내용"));

        const std::string backupFile = b->backupPath;
        b->dirty = false;
        QVERIFY(fm.backup(*b, dir)); // clean again -> backup deleted
        QVERIFY(!pathExists(backupFile));
    }

    void languageDetection()
    {
        FileManager fm(*m_scratch, {}, &m_langs, &m_udls);
        const std::string d = tempDir();
        QVERIFY(writeFileContent(d + "/a.py", "x = 1\n"));
        QVERIFY(writeFileContent(d + "/runme", "#!/bin/bash\necho hi\n"));
        QVERIFY(writeFileContent(d + "/readme.md", "# title\n"));
        QCOMPARE(fm.loadFile(d + "/a.py")->lang, L_PYTHON);
        QCOMPARE(fm.loadFile(d + "/runme")->lang, L_BASH);
        Buffer* md = fm.loadFile(d + "/readme.md");
        QCOMPARE(md->lang, L_USER);
        QCOMPARE(md->userLangName, std::string("Markdown (preinstalled)"));
        LoadError err;
        QVERIFY(!fm.loadFile(d + "/missing.txt", {}, &err));
        QCOMPARE(err, LoadError::NotFound);
    }
};

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    TestBuffer t;
    return QTest::qExec(&t, argc, argv);
}

#include "test_buffer.moc"
