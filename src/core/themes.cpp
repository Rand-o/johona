// themes.cpp — theme import/validation/delete implementation (libzip).

#include "themes.hpp"

#include <zip.h>

#include <algorithm>
#include <cstring>

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QTemporaryDir>

namespace johona::themes {

namespace {

bool containsValue(const std::vector<int>& v, int value) {
    return std::find(v.begin(), v.end(), value) != v.end();
}

}  // namespace

// ---------------------------------------------------------------------------
// theme.json loading / normalization
// ---------------------------------------------------------------------------

std::optional<ThemeData> loadThemeData(const QString& themeDir) {
    QDir dir(themeDir);
    if (!dir.exists())
        return std::nullopt;

    // Locate theme.json: root theme.json, then any root *.json, then a
    // recursive theme.json (covers both kWallpaper lookup orders).
    QString jsonPath;
    if (QFileInfo::exists(dir.absoluteFilePath("theme.json"))) {
        jsonPath = dir.absoluteFilePath("theme.json");
    } else {
        const auto rootJsons = dir.entryList({"*.json"}, QDir::Files, QDir::Name);
        if (!rootJsons.isEmpty()) {
            jsonPath = dir.absoluteFilePath(rootJsons.first());
        } else {
            // QDir has no recursive filter; use QDirIterator.
            QDirIterator it(dir.absolutePath(), {QStringLiteral("theme.json")},
                            QDir::Files, QDirIterator::Subdirectories);
            if (it.hasNext())
                jsonPath = it.next();
        }
    }
    if (jsonPath.isEmpty())
        return std::nullopt;

    QFile f(jsonPath);
    if (!f.open(QIODevice::ReadOnly))
        return std::nullopt;
    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject())
        return std::nullopt;

    const QJsonObject obj = doc.object();
    ThemeData data;
    data.raw = obj.toVariantMap();
    data.displayName = obj.value("displayName").toString();
    data.imageCredits = obj.value("imageCredits").toString();
    data.imageFilename = obj.value("imageFilename").toString();

    auto readList = [&](const char* key) {
        std::vector<int> out;
        for (const auto& v : obj.value(key).toArray())
            out.push_back(v.toInt());
        return out;
    };
    data.sunriseImageList = readList("sunriseImageList");
    data.dayImageList = readList("dayImageList");
    data.sunsetImageList = readList("sunsetImageList");
    data.nightImageList = readList("nightImageList");
    return data;
}

void normalizeImageLists(ThemeData& data) {
    // kWallpaper's "Tahoe" fix: some 24 h themes ship image 1 in
    // nightImageList; move it to sunriseImageList for that specific shape.
    auto& nightList = data.nightImageList;
    auto& sunriseList = data.sunriseImageList;

    const bool hasImage1InNight = containsValue(nightList, 1);
    const bool hasImage1InSunrise = containsValue(sunriseList, 1);
    const bool has141516InNight =
        containsValue(nightList, 14) && containsValue(nightList, 15) &&
        containsValue(nightList, 16);
    bool nightOnlyTahoePattern = true;
    for (int v : nightList)
        if (v != 14 && v != 15 && v != 16 && v != 1)
            nightOnlyTahoePattern = false;

    if (hasImage1InNight && !hasImage1InSunrise && has141516InNight &&
        nightOnlyTahoePattern) {
        nightList.erase(std::remove(nightList.begin(), nightList.end(), 1),
                        nightList.end());
        sunriseList.push_back(1);
        std::sort(sunriseList.begin(), sunriseList.end());
    }
}

// ---------------------------------------------------------------------------
// image file resolution (single source of truth, as in kWallpaper)
// ---------------------------------------------------------------------------

std::vector<QString> imageFilesFor(const QString& themeDir, const ThemeData& data) {
    const QString pattern =
        data.imageFilename.isEmpty() ? QStringLiteral("*.*") : data.imageFilename;
    const QFileInfo patternInfo(pattern);
    const QString base = patternInfo.completeBaseName();
    const QString ext = patternInfo.suffix().isEmpty() ? QStringLiteral(".jpg")
                                                       : patternInfo.suffix();

    QDir dir(themeDir);
    std::vector<QString> imageFiles;
    const auto globbed = dir.entryList({pattern}, QDir::Files, QDir::Name);
    if (!globbed.isEmpty()) {
        for (const QString& f : globbed)
            imageFiles.push_back(dir.absoluteFilePath(f));
    } else {
        // Numbered-file fallback: {base}_{1..99}{ext}
        for (int i = 1; i < 100; i++) {
            const QString name = QStringLiteral("%1_%2.%3").arg(base).arg(i).arg(ext);
            if (QFileInfo::exists(dir.absoluteFilePath(name)))
                imageFiles.push_back(dir.absoluteFilePath(name));
        }
    }

    // Numeric sort by the trailing _N in the stem (non-numeric stems first).
    auto idx = [](const QString& f) -> int {
        const QString stem = QFileInfo(f).completeBaseName();
        const QString last = stem.split('_').last();
        bool ok = false;
        const int v = last.toInt(&ok);
        return ok ? v : 0;
    };
    std::stable_sort(imageFiles.begin(), imageFiles.end(),
                     [&](const QString& a, const QString& b) { return idx(a) < idx(b); });
    return imageFiles;
}

QString imageFileFor(const QString& themeDir, const ThemeData& data, int imageValue) {
    const auto files = imageFilesFor(themeDir, data);
    if (files.empty())
        return {};
    const int n = static_cast<int>(files.size());
    if (imageValue >= 1 && imageValue <= n)
        return files[imageValue - 1];
    // Wrap around (Python-style non-negative modulo), as in kWallpaper.
    int r = (imageValue - 1) % n;
    if (r < 0)
        r += n;
    return files[r];
}

std::vector<QString> validateThemeImages(const QString& themeDir, const ThemeData& data) {
    const auto files = imageFilesFor(themeDir, data);
    const int count = static_cast<int>(files.size());
    std::vector<QString> missing;
    const struct {
        const char* category;
        const std::vector<int>* list;
    } cats[] = {
        {"sunrise", &data.sunriseImageList},
        {"day", &data.dayImageList},
        {"sunset", &data.sunsetImageList},
        {"night", &data.nightImageList},
    };
    for (const auto& c : cats)
        for (int value : *c.list)
            if (value > count)
                missing.push_back(
                    QStringLiteral("%1 image %2").arg(c.category).arg(value));
    if (!missing.empty() && count == 0)
        missing.push_back(QStringLiteral("(the imageFilename pattern matched no files)"));
    return missing;
}

// ---------------------------------------------------------------------------
// discovery / delete
// ---------------------------------------------------------------------------

std::vector<ThemeInfo> discoverThemes(const QString& themesDir) {
    std::vector<ThemeInfo> out;
    QDir dir(themesDir);
    if (!dir.exists())
        return out;
    for (const QFileInfo& fi : dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        std::optional<ThemeData> data = loadThemeData(fi.absoluteFilePath());
        if (!data)
            continue;
        ThemeInfo info;
        info.name = fi.fileName();
        info.path = fi.absoluteFilePath();
        info.displayName = data->displayName.isEmpty() ? info.name : data->displayName;
        int maxV = 0;
        for (int v : data->sunriseImageList)
            maxV = std::max(maxV, v);
        for (int v : data->dayImageList)
            maxV = std::max(maxV, v);
        for (int v : data->sunsetImageList)
            maxV = std::max(maxV, v);
        for (int v : data->nightImageList)
            maxV = std::max(maxV, v);
        info.imageCount = maxV;
        out.push_back(std::move(info));
    }
    std::sort(out.begin(), out.end(),
              [](const ThemeInfo& a, const ThemeInfo& b) { return a.name < b.name; });
    return out;
}

DeleteResult deleteTheme(const QString& nameOrPath, const QString& themesDir) {
    const QDir base(themesDir);
    const QString target = QFileInfo(nameOrPath).isAbsolute()
                               ? QFileInfo(nameOrPath).absoluteFilePath()
                               : base.absoluteFilePath(nameOrPath);
    // Safety: the target must be a direct subdirectory of the themes dir.
    const QString canonicalBase = base.canonicalPath();
    const QString canonicalTarget = QFileInfo(target).canonicalFilePath();
    if (canonicalBase.isEmpty() || canonicalTarget.isEmpty() ||
        QFileInfo(canonicalTarget).absolutePath() != canonicalBase)
        return {false, QStringLiteral("Refusing to delete a path outside the themes directory")};
    if (!QDir(canonicalTarget).removeRecursively())
        return {false, QStringLiteral("Failed to delete theme directory")};
    return {true, {}};
}

// ---------------------------------------------------------------------------
// import (libzip)
// ---------------------------------------------------------------------------

namespace {

bool extractZip(const QString& zipPath, const QString& outDir, QString* error) {
    // libzip 1.11: zip_open reports failure via an errno-style int.
    int errnum = 0;
    zip_t* za = zip_open(zipPath.toUtf8().constData(), ZIP_RDONLY, &errnum);
    if (!za) {
        *error = QStringLiteral("Not a readable zip archive: %1")
                     .arg(QString::fromLocal8Bit(strerror(errnum)));
        return false;
    }
    bool ok = true;
    const zip_int64_t n = zip_get_num_entries(za, 0);
    for (zip_int64_t i = 0; i < n && ok; i++) {
        const char* name = zip_get_name(za, i, 0);
        if (!name) {
            ok = false;
            break;
        }
        const QString entry = QString::fromUtf8(name);
        if (entry.endsWith('/'))
            continue;  // directory entry
        // Zip-slip protection.
        if (entry.startsWith('/') || entry == ".." || entry.contains(QStringLiteral("/../"))) {
            ok = false;
            break;
        }
        if (entry.startsWith("__MACOSX/") || entry.endsWith(QStringLiteral(".DS_Store")))
            continue;
        const QString outPath = outDir + '/' + entry;
        QDir().mkpath(QFileInfo(outPath).absolutePath());
        // libzip 1.11: zip_fopen takes a name; use the index variant.
        zip_file_t* zf = zip_fopen_index(za, static_cast<zip_uint64_t>(i), 0);
        if (!zf) {
            ok = false;
            break;
        }
        QFile f(outPath);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            zip_fclose(zf);
            ok = false;
            break;
        }
        char buf[65536];
        for (;;) {
            // libzip 1.11: zip_fread takes no offset argument.
            const zip_int64_t r = zip_fread(zf, buf, sizeof(buf));
            if (r < 0) {
                zip_fclose(zf);
                ok = false;
                break;
            }
            if (r == 0)
                break;
            f.write(buf, static_cast<qint64>(r));
        }
        zip_fclose(zf);
    }
    if (!ok) {
        zip_error_t* ze = zip_get_error(za);
        *error = QString::fromUtf8(zip_error_strerror(ze));
    }
    zip_close(za);
    return ok;
}

}  // namespace

ImportResult importTheme(const QString& zipPath, const QString& themesDir) {
    ImportResult result;
    const QFileInfo source(zipPath);
    if (!source.exists()) {
        result.message = QStringLiteral("Theme not found: %1").arg(zipPath);
        return result;
    }
    const QString suffix = source.suffix().toLower();
    if (suffix != "ddw" && suffix != "zip") {
        result.message = QStringLiteral("Not a theme archive: %1").arg(zipPath);
        return result;
    }

    QTemporaryDir tmp;
    if (!tmp.isValid()) {
        result.message = QStringLiteral("Could not create a temporary directory");
        return result;
    }
    const QString extractDir = tmp.path() + '/' + source.completeBaseName();
    if (!extractZip(zipPath, extractDir, &result.message))
        return result;  // temp dir cleaned up by QTemporaryDir

    std::optional<ThemeData> data = loadThemeData(extractDir);
    if (!data) {
        result.message = QStringLiteral("theme.json not found in extracted theme");
        return result;
    }
    normalizeImageLists(*data);

    // Validate every referenced image BEFORE committing; a rejected import
    // lists all missing images and leaves no partial theme behind.
    result.missingImages = validateThemeImages(extractDir, *data);
    if (!result.missingImages.empty()) {
        result.message = QStringLiteral("Theme is missing %1 referenced image(s)")
                             .arg(result.missingImages.size());
        return result;
    }

    const QString targetName = source.completeBaseName();
    const QString targetDir = themesDir + '/' + targetName;
    if (QFileInfo::exists(targetDir)) {
        result.message = QStringLiteral("Theme already exists: %1").arg(targetName);
        return result;
    }
    QDir().mkpath(themesDir);
    if (!QFile::rename(extractDir, targetDir)) {
        // Cross-device rename fallback: move contents.
        QDir src(extractDir);
        QDir dst(targetDir);
        if (!dst.mkpath(QStringLiteral(".")) ||
            !src.rename(src.absolutePath(), targetDir)) {
            result.message = QStringLiteral("Failed to move theme into place");
            return result;
        }
    }

    result.success = true;
    result.themePath = targetDir;
    result.displayName = data->displayName.isEmpty() ? targetName : data->displayName;
    return result;
}

}  // namespace johona::themes
