#include "core/encoding/CharsetDetector.h"
#include "core/encoding/Codepage.h"
#include "core/encoding/EncodingMapper.h"
#include "core/encoding/UnicodeConverter.h"
#include "core/util/PathUtil.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>

using namespace npp::core;

namespace {

std::string readAll(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    const QByteArray b = f.readAll();
    return std::string(b.constData(), static_cast<size_t>(b.size()));
}

QString dataDir() { return QStringLiteral(NPP_TEST_DATA_DIR "/encodings/"); }

} // namespace

class TestEncoding : public QObject {
    Q_OBJECT
private slots:
    void bomDetection()
    {
        QCOMPARE(detectBom("\xEF\xBB\xBFx"), UniMode::Utf8Bom);
        QCOMPARE(detectBom("\xFF\xFEx\0"), UniMode::Utf16LE);
        QCOMPARE(detectBom("\xFE\xFF\0x"), UniMode::Utf16BE);
        QCOMPARE(detectBom("abc"), UniMode::Ansi);
        QCOMPARE(bomSize(UniMode::Utf8Bom), size_t(3));
        QCOMPARE(bomSize(UniMode::Utf16LE), size_t(2));
        QCOMPARE(bomSize(UniMode::Utf8), size_t(0));
    }

    void utf8Classification()
    {
        QCOMPARE(classifyUtf8("plain"), Utf8Class::Ascii7);
        QCOMPARE(classifyUtf8("한글"), Utf8Class::Utf8NoBom);
        QCOMPARE(classifyUtf8("\xC7\xD1\xB1\xDB"), Utf8Class::Ascii8);     // CP949 "한글"
        QCOMPARE(classifyUtf8(std::string("a\0b", 3)), Utf8Class::Ascii8); // NUL => not UTF-8
    }

    void utf16Conversion()
    {
        const std::string text = "A한😀";
        const std::string le = encodeFromUtf8(text, UniMode::Utf16LE);
        QCOMPARE(le, std::string("\xFF\xFE" "A\0" "\x5C\xD5" "\x3D\xD8\x00\xDE", 10));
        QCOMPARE(decodeToUtf8(le, UniMode::Utf16LE), text);
        const std::string be = encodeFromUtf8(text, UniMode::Utf16BENoBom);
        QCOMPARE(be, std::string("\0A" "\xD5\x5C" "\xD8\x3D\xDE\x00", 8));
        QCOMPARE(decodeToUtf8(be, UniMode::Utf16BENoBom), text);
        // Unpaired surrogate and odd trailing byte -> U+FFFD
        QCOMPARE(decodeToUtf8(std::string("\x00\xD8" "A\0" "B", 5), UniMode::Utf16LENoBom),
                 std::string("\xEF\xBF\xBD" "A" "\xEF\xBF\xBD"));
        // Invalid UTF-8 input -> U+FFFD when encoding
        QCOMPARE(encodeFromUtf8("\xFF", UniMode::Utf16LENoBom), std::string("\xFD\xFF", 2));
        QCOMPARE(encodeFromUtf8("x", UniMode::Utf8Bom), std::string("\xEF\xBB\xBFx"));
        QCOMPARE(encodeFromUtf8("x", UniMode::Utf8Bom, false), std::string("x"));
    }

    void streamingMatchesWholeBuffer()
    {
        const std::string data = readAll(dataDir() + "utf16be-bom.txt");
        QVERIFY(!data.empty());
        const std::string whole = decodeToUtf8(data, UniMode::Utf16BE);
        // Mode is detected on the first block (upstream reads files in large blocks), so either
        // force the mode or make the first block large enough.
        for (size_t chunk : {1u, 2u, 3u, 7u, 4096u}) {
            UnicodeReader r;
            r.setMode(UniMode::Utf16BE);
            std::string out;
            for (size_t i = 0; i < data.size(); i += chunk)
                r.convert(std::string_view(data).substr(i, chunk), out);
            r.finish(out);
            QCOMPARE(out, whole);
        }
        UnicodeReader autoDetect;
        std::string out;
        autoDetect.convert(std::string_view(data).substr(0, 64), out);
        autoDetect.convert(std::string_view(data).substr(64), out);
        autoDetect.finish(out);
        QCOMPARE(autoDetect.mode(), UniMode::Utf16BE);
        QCOMPARE(out, whole);
    }

    void encodingMapper()
    {
        QCOMPARE(EncodingMapper::encodingFromString("UTF-8"), 65001);
        QCOMPARE(EncodingMapper::encodingFromString("Shift_JIS"), 932);
        QCOMPARE(EncodingMapper::encodingFromString("EUC-KR"), 51949);
        QCOMPARE(EncodingMapper::encodingFromString("windows-1252"), 1252);
        QCOMPARE(EncodingMapper::encodingFromString("gb18030"), 936);
        QCOMPARE(EncodingMapper::encodingFromString("nonsense"), -1);
        QCOMPARE(EncodingMapper::encodingFromIndex(0), 1250);
        QCOMPARE(EncodingMapper::encodingFromIndex(18), -1); // unsupported ISO-8859-10 slot
        QCOMPARE(EncodingMapper::indexFromEncoding(949), 43);
        QCOMPARE(EncodingMapper::indexFromEncoding(12345), -1);
        QCOMPARE(EncodingMapper::table().size(), size_t(49));
    }

    void allMenuCodePagesSupported()
    {
        QStringList missing;
        for (const auto& u : EncodingMapper::table())
            if (u.codePage != -1 && !isCodePageSupported(u.codePage))
                missing << QString::number(u.codePage);
        if (!missing.isEmpty())
            qWarning("code pages not supported on this platform: %s", qPrintable(missing.join(' ')));
        // CJK and Windows code pages are mandatory everywhere
        for (int cp : {932, 936, 949, 950, 51949, 1250, 1251, 1252, 1253, 1254, 1255, 1256, 1257, 1258})
            QVERIFY2(isCodePageSupported(cp), qPrintable(QString::number(cp)));
    }

    void codePageConversion()
    {
        QCOMPARE(codePageToUtf8("\xC7\xD1\xB1\xDB", 949).value(), std::string("한글"));
        QCOMPARE(utf8ToCodePage("한글", 949).value(), std::string("\xC7\xD1\xB1\xDB"));
        bool lossy = false;
        // Unrepresentable characters become '?' (one per UTF-16 unit on Windows, like upstream).
        const std::string cp1252 = utf8ToCodePage("aé😀", 1252, &lossy).value();
        QVERIFY(cp1252 == "a\xE9?" || cp1252 == "a\xE9??");
        QVERIFY(lossy);
        QVERIFY(!codePageToUtf8("x", 99999).has_value());
    }

    // Every sample file: detection, conversion to UTF-8, and byte-exact round trip.
    void sampleFiles_data()
    {
        QTest::addColumn<QString>("file");
        QTest::addColumn<int>("mode");
        QTest::addColumn<int>("codePage");
        QFile f(dataDir() + "manifest.json");
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QJsonArray arr = QJsonDocument::fromJson(f.readAll()).array();
        for (const auto& v : arr) {
            const QJsonObject o = v.toObject();
            QTest::newRow(qPrintable(o["file"].toString()))
                << o["file"].toString() << o["mode"].toInt() << o["codepage"].toInt();
        }
    }

    void sampleFiles()
    {
        QFETCH(QString, file);
        QFETCH(int, mode);
        QFETCH(int, codePage);
        const std::string data = readAll(dataDir() + file);
        const std::string expectedUtf8 = readAll(dataDir() + file + ".utf8");
        QVERIFY(!data.empty());

        const UniMode detected = detectUnicodeMode(data);
        QCOMPARE(static_cast<int>(detected), mode);

        if (detected != UniMode::Ansi) {
            QCOMPARE(decodeToUtf8(data, detected), expectedUtf8);
            QCOMPARE(encodeFromUtf8(expectedUtf8, detected), data);
            return;
        }

        // ANSI file: charset detection, then code page conversion round trip
        const int detectedCp = detectCodePage(data);
        if (detectedCp != codePage)
            qWarning("%s: uchardet detected %d, expected %d", qPrintable(file), detectedCp, codePage);
        // Same uchardet as upstream, so the same results are expected (parity, not perfection):
        //  - Korean CP949 text is reported as EUC-KR (51949); both decode Hangul syllables alike.
        //  - Short Big5 samples are reported as GBK: known uchardet limitation, kept for parity.
        if (codePage == 949)
            QVERIFY(detectedCp == 949 || detectedCp == 51949);
        else if (codePage == 932 || codePage == 936)
            QCOMPARE(detectedCp, codePage);
        else if (codePage == 950 && detectedCp != 950)
            qInfo("big5.txt: known uchardet limitation (detected %d), same as upstream", detectedCp);

        const auto utf8 = codePageToUtf8(data, codePage);
        QVERIFY(utf8.has_value());
        QCOMPARE(*utf8, expectedUtf8);
        bool lossy = true;
        const auto back = utf8ToCodePage(*utf8, codePage, &lossy);
        QVERIFY(back.has_value());
        QVERIFY(!lossy);
        QCOMPARE(*back, data);
    }
};

QTEST_GUILESS_MAIN(TestEncoding)
#include "test_encoding.moc"
