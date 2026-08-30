// test_themes.cpp — theme.json loading, image resolution, validation,
// discovery, import (zip), delete.

#include <QtTest>

#include <zip.h>

#include <map>
#include <string>

#include "themes.hpp"

using namespace johona::themes;

namespace {

/// Build a zip archive with the given name→content entries (libzip 1.11).
bool makeZip(const QString& path, const std::map<std::string, std::string>& entries) {
    zip_error_t ze{};
    int errnum = 0;
    zip_t* za = zip_open(path.toUtf8().constData(), ZIP_CREATE | ZIP_TRUNCATE, &errnum);
    if (!za)
        return false;
    bool ok = true;
    for (const auto& [name, content] : entries) {
        zip_source_t* src = zip_source_buffer_create(content.data(), content.size(), 0, &ze);
        if (!src) {
            ok = false;
            break;
        }
        const zip_int64_t idx = zip_file_add(za, name.c_str(), src, ZIP_FL_OVERWRITE);
        if (idx < 0) {
            zip_source_free(src);  // not consumed on failure
            ok = false;
            break;
        }
        // On success the archive owns the source (zip_close frees it).
    }
    if (ok && zip_close(za) < 0)
        ok = false;
    return ok;
}

void writeFile(const QString& path, const QString& content) {
    QFile f(path);
    f.open(QIODevice::WriteOnly | QIODevice::Truncate);
    f.write(content.toUtf8());
}

const char* kThemeJson = R"({
    "displayName": "Test Pack",
    "imageCredits": "test",
    "imageFilename": "img_*.jpg",
    "sunriseImageList": [1],
    "dayImageList": [2],
    "sunsetImageList": [3],
    "nightImageList": [4]
})";

}  // namespace

class TestThemes : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void loadThemeData_basic();
    void loadThemeData_missing();
    void imageFilesFor_numericSort();
    void imageFilesFor_numberedFallback();
    void imageFileFor_wraparound();
    void testValidateThemeImages();
    void prettyThemeName_stripsTrailingYear();
    void normalizeImageLists_tahoe();
    void testDiscoverThemes();
    void importTheme_success();
    void importTheme_missingImages_rejected();
    void importTheme_alreadyExists();
    void importTheme_badExtension();
    void deleteTheme_works();
    void deleteTheme_outsideRefused();

private:
    QString themeDir() const { return m_tmp.path() + "/themes"; }

    QTemporaryDir m_tmp;
};

void TestThemes::initTestCase() {
    QVERIFY(m_tmp.isValid());
}

void TestThemes::loadThemeData_basic() {
    const QString dir = m_tmp.path() + "/basic";
    QDir().mkpath(dir);
    writeFile(dir + "/theme.json", kThemeJson);

    auto data = loadThemeData(dir);
    QVERIFY(data.has_value());
    QCOMPARE(data->displayName, QString("Test Pack"));
    QCOMPARE(data->imageCredits, QString("test"));
    QCOMPARE(data->imageFilename, QString("img_*.jpg"));
    QCOMPARE(data->sunriseImageList.size(), static_cast<size_t>(1));
    QCOMPARE(data->sunriseImageList[0], 1);
    QCOMPARE(data->dayImageList[0], 2);
    QCOMPARE(data->sunsetImageList[0], 3);
    QCOMPARE(data->nightImageList[0], 4);
}

void TestThemes::loadThemeData_missing() {
    const QString dir = m_tmp.path() + "/empty";
    QDir().mkpath(dir);
    QVERIFY(!loadThemeData(dir).has_value());
    QVERIFY(!loadThemeData(m_tmp.path() + "/nonexistent").has_value());
}

void TestThemes::imageFilesFor_numericSort() {
    const QString dir = m_tmp.path() + "/sort";
    QDir().mkpath(dir);
    writeFile(dir + "/theme.json", kThemeJson);
    for (int i : {1, 2, 10})
        writeFile(dir + QStringLiteral("/img_%1.jpg").arg(i), "x");

    ThemeData data;
    data.imageFilename = QStringLiteral("img_*.jpg");
    const auto files = imageFilesFor(dir, data);
    QCOMPARE(files.size(), static_cast<size_t>(3));
    // Numeric (not lexicographic) order: 1, 2, 10.
    QCOMPARE(QFileInfo(files[0]).fileName(), QString("img_1.jpg"));
    QCOMPARE(QFileInfo(files[1]).fileName(), QString("img_2.jpg"));
    QCOMPARE(QFileInfo(files[2]).fileName(), QString("img_10.jpg"));
}

void TestThemes::imageFilesFor_numberedFallback() {
    const QString dir = m_tmp.path() + "/fallback";
    QDir().mkpath(dir);
    writeFile(dir + "/theme.json", kThemeJson);
    // The glob "wallpaper*.jpg" matches nothing; the numbered fallback
    // (base_"wallpaper" + _1..99 + .jpg) must be used.
    for (int i = 1; i <= 3; i++)
        writeFile(dir + QStringLiteral("/wallpaper_%1.jpg").arg(i), "x");

    ThemeData data;
    data.imageFilename = QStringLiteral("wallpaper*.jpg");
    const auto files = imageFilesFor(dir, data);
    QCOMPARE(files.size(), static_cast<size_t>(3));
    QCOMPARE(QFileInfo(files[0]).fileName(), QString("wallpaper_1.jpg"));
}

void TestThemes::imageFileFor_wraparound() {
    const QString dir = m_tmp.path() + "/wrap";
    QDir().mkpath(dir);
    writeFile(dir + "/theme.json", kThemeJson);
    for (int i = 1; i <= 3; i++)
        writeFile(dir + QStringLiteral("/img_%1.jpg").arg(i), "x");

    ThemeData data;
    data.imageFilename = QStringLiteral("img_*.jpg");
    QCOMPARE(imageFileFor(dir, data, 1), dir + "/img_1.jpg");
    QCOMPARE(imageFileFor(dir, data, 3), dir + "/img_3.jpg");
    // Wrap around past the end (Python-style non-negative modulo).
    QCOMPARE(imageFileFor(dir, data, 4), dir + "/img_1.jpg");
    QCOMPARE(imageFileFor(dir, data, 0), dir + "/img_3.jpg");
    QCOMPARE(imageFileFor(dir, data, -1), dir + "/img_2.jpg");
}

void TestThemes::testValidateThemeImages() {
    const QString dir = m_tmp.path() + "/validate";
    QDir().mkpath(dir);
    writeFile(dir + "/theme.json", kThemeJson);
    for (int i = 1; i <= 3; i++)
        writeFile(dir + QStringLiteral("/img_%1.jpg").arg(i), "x");

    ThemeData data;
    data.imageFilename = QStringLiteral("img_*.jpg");
    data.dayImageList = {2, 5};
    data.nightImageList = {4};

    const auto missing = validateThemeImages(dir, data);
    QCOMPARE(missing.size(), static_cast<size_t>(2));
    QCOMPARE(missing[0], QString("day image 5"));
    QCOMPARE(missing[1], QString("night image 4"));

    // All present → no errors.
    data.dayImageList = {2};
    data.nightImageList = {3};
    QVERIFY(validateThemeImages(dir, data).empty());
}

void TestThemes::prettyThemeName_stripsTrailingYear() {
    // Trailing year (and year-index) is stripped.
    QCOMPARE(prettyThemeName("California Highland Lakes 2023",
                             "24hr-California-Highland-Lakes-2023"),
             QString("California Highland Lakes"));
    QCOMPARE(prettyThemeName("Tahoe 2026", "x"), QString("Tahoe"));
    QCOMPARE(prettyThemeName("Bangkok 2025-1", "x"), QString("Bangkok"));
    QCOMPARE(prettyThemeName("The Great Wall 2026-2", "x"),
             QString("The Great Wall"));
    QCOMPARE(prettyThemeName("Los Angeles 2019", "x"), QString("Los Angeles"));
    QCOMPARE(prettyThemeName("Merdeka 118 2026", "x"), QString("Merdeka 118"));
    // A mid-name year is part of the name and stays.
    QCOMPARE(prettyThemeName("Chicago 2026 Mix", "x"),
             QString("Chicago 2026 Mix"));
    QCOMPARE(prettyThemeName("California Sf 2023 Mix", "x"),
             QString("California Sf 2023 Mix"));
    // No year: unchanged.
    QCOMPARE(prettyThemeName("California Highland Lakes", "x"),
             QString("California Highland Lakes"));
    // Empty displayName falls back to the directory name.
    QCOMPARE(prettyThemeName(QString(), "24hr-Foo"), QString("24hr-Foo"));
}

void TestThemes::normalizeImageLists_tahoe() {
    // The specific Tahoe 24 h shape: night == {14,15,16,1}.
    ThemeData tahoe;
    tahoe.nightImageList = {14, 15, 16, 1};
    normalizeImageLists(tahoe);
    QCOMPARE(tahoe.nightImageList.size(), static_cast<size_t>(3));
    QCOMPARE(tahoe.nightImageList[0], 14);
    QCOMPARE(tahoe.nightImageList[1], 15);
    QCOMPARE(tahoe.nightImageList[2], 16);
    QCOMPARE(tahoe.sunriseImageList.size(), static_cast<size_t>(1));
    QCOMPARE(tahoe.sunriseImageList[0], 1);

    // A different shape is left untouched.
    ThemeData other;
    other.nightImageList = {1, 2, 3};
    normalizeImageLists(other);
    QCOMPARE(other.nightImageList.size(), static_cast<size_t>(3));
    QCOMPARE(other.nightImageList[0], 1);
    QVERIFY(other.sunriseImageList.empty());
}

void TestThemes::testDiscoverThemes() {
    const QString base = themeDir();
    // Two valid themes.
    for (const char* name : {"alpha", "beta"}) {
        const QString dir = base + '/' + name;
        QDir().mkpath(dir);
        writeFile(dir + "/theme.json", kThemeJson);
    }
    // A directory without any JSON must be ignored.
    QDir().mkpath(base + "/notatheme");

    const auto themes = discoverThemes(base);
    QCOMPARE(themes.size(), static_cast<size_t>(2));
    QCOMPARE(themes[0].name, QString("alpha"));
    QCOMPARE(themes[1].name, QString("beta"));
    QCOMPARE(themes[0].displayName, QString("Test Pack"));
    QCOMPARE(themes[0].imageCount, 4);
}

void TestThemes::importTheme_success() {
    QDir(themeDir()).removeRecursively();
    const QString zip = m_tmp.path() + "/pack.zip";
    std::map<std::string, std::string> entries{{"theme.json", kThemeJson}};
    for (int i = 1; i <= 4; i++)
        entries["img_" + std::to_string(i) + ".jpg"] = "img";
    QVERIFY(makeZip(zip, entries));

    const auto result = importTheme(zip, themeDir());
    QVERIFY2(result.success, qPrintable(result.message));
    QVERIFY(QFileInfo::exists(result.themePath));
    QVERIFY(QFileInfo::exists(result.themePath + "/theme.json"));
    QVERIFY(QFileInfo::exists(result.themePath + "/img_4.jpg"));
    QCOMPARE(result.displayName, QString("Test Pack"));

    const auto themes = discoverThemes(themeDir());
    QCOMPARE(themes.size(), static_cast<size_t>(1));
    QCOMPARE(themes[0].name, QString("pack"));
}

void TestThemes::importTheme_missingImages_rejected() {
    QDir(themeDir()).removeRecursively();
    // Reference a 9th image that does not exist in the archive.
    const QString json =
        QString::fromUtf8(kThemeJson).replace("\"nightImageList\": [4]",
                                              "\"nightImageList\": [4, 9]");
    std::map<std::string, std::string> entries2{{"theme.json", json.toStdString()}};
    for (int i = 1; i <= 4; i++)
        entries2["img_" + std::to_string(i) + ".jpg"] = "img";
    const QString zip2 = m_tmp.path() + "/broken2.zip";
    QVERIFY(makeZip(zip2, entries2));

    const auto result = importTheme(zip2, themeDir());
    QVERIFY(!result.success);
    QCOMPARE(result.missingImages.size(), static_cast<size_t>(1));
    QCOMPARE(result.missingImages[0], QString("night image 9"));
    // Reject-all: no partial theme left behind.
    QVERIFY(!QFileInfo::exists(themeDir() + "/broken2"));
    QVERIFY(discoverThemes(themeDir()).empty());
}

void TestThemes::importTheme_alreadyExists() {
    QDir(themeDir()).removeRecursively();
    const QString zip = m_tmp.path() + "/dup.zip";
    std::map<std::string, std::string> entries{{"theme.json", kThemeJson}};
    for (int i = 1; i <= 4; i++)
        entries["img_" + std::to_string(i) + ".jpg"] = "img";
    QVERIFY(makeZip(zip, entries));

    QVERIFY(importTheme(zip, themeDir()).success);
    const auto second = importTheme(zip, themeDir());
    QVERIFY(!second.success);
    QVERIFY(second.message.contains("already exists"));
}

void TestThemes::importTheme_badExtension() {
    const QString txt = m_tmp.path() + "/notazip.txt";
    writeFile(txt, "hello");
    const auto result = importTheme(txt, themeDir());
    QVERIFY(!result.success);
    QVERIFY(result.message.contains("Not a theme archive"));
}

void TestThemes::deleteTheme_works() {
    const QString dir = themeDir() + "/doomed";
    QDir().mkpath(dir);
    writeFile(dir + "/theme.json", kThemeJson);

    const auto result = deleteTheme(QStringLiteral("doomed"), themeDir());
    QVERIFY2(result.success, qPrintable(result.message));
    QVERIFY(!QFileInfo::exists(dir));
}

void TestThemes::deleteTheme_outsideRefused() {
    const QString outside = m_tmp.path() + "/outside";
    QDir().mkpath(outside);
    writeFile(outside + "/theme.json", kThemeJson);

    const auto result = deleteTheme(outside, themeDir());
    QVERIFY(!result.success);
    QVERIFY(result.message.contains("outside"));
    QVERIFY(QFileInfo::exists(outside));
}

QTEST_GUILESS_MAIN(TestThemes)
#include "test_themes.moc"
