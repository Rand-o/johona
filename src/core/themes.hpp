// themes.hpp — theme discovery, import (.ddw/.zip via libzip), validation,
// delete, and image-file resolution.
//
// Mirrors kWallpaper 1.1.0 semantics (themes.py / selection.py):
//  - theme.json (WDD format): displayName, imageCredits, imageFilename
//    (pattern with `*`), sunriseImageList, dayImageList, sunsetImageList,
//    nightImageList.
//  - image values are 1-based positions into the ordered file list from
//    imageFilesFor() (glob the imageFilename pattern; numbered-file
//    fallback; numeric sort) — the same list import validation checks, so
//    selection and validation can never disagree.
//  - import validates every referenced image; a rejected import lists ALL
//    missing images and leaves no partial theme behind.

#pragma once

#include <optional>
#include <QString>
#include <QVariant>
#include <vector>

namespace johona::themes {

/// Parsed + normalized theme.json.
struct ThemeData {
    QString displayName;
    QString imageCredits;
    QString imageFilename = QStringLiteral("*.*");
    std::vector<int> sunriseImageList;
    std::vector<int> dayImageList;
    std::vector<int> sunsetImageList;
    std::vector<int> nightImageList;
    QVariantMap raw;  // full parsed JSON (for forward compatibility)
};

struct ThemeInfo {
    QString name;  // directory name
    QString path;  // absolute directory path
    QString displayName;
    int imageCount = 0;  // total distinct referenced images
};

struct ImportResult {
    bool success = false;
    QString themePath;      // on success: the new theme directory
    QString displayName;
    QString message;        // human-readable failure reason
    std::vector<QString> missingImages;  // every missing image (reject-all)
};

struct DeleteResult {
    bool success = false;
    QString message;
};

/// (name, path) for every subdirectory of `themesDir` containing a .json.
std::vector<ThemeInfo> discoverThemes(const QString& themesDir);

/// Load + normalize theme.json from a theme directory (root *.json first,
/// then recursive theme.json).  nullopt when absent/unparseable.
std::optional<ThemeData> loadThemeData(const QString& themeDir);

/// Apply the kWallpaper "Tahoe" normalization: move image 1 from
/// nightImageList to sunriseImageList for the specific 24 h Tahoe theme
/// shape (night list == {14,15,16[,1]}).
void normalizeImageLists(ThemeData& data);

/// Ordered image file list for a theme directory (see header comment).
std::vector<QString> imageFilesFor(const QString& themeDir, const ThemeData& data);

/// The file for a 1-based image value (wraps around past the end, as in
/// kWallpaper).  Empty string when no files exist.
QString imageFileFor(const QString& themeDir, const ThemeData& data, int imageValue);

/// Verify every referenced image exists.  Returns one line per missing
/// image (empty when valid).
std::vector<QString> validateThemeImages(const QString& themeDir, const ThemeData& data);

/// Import a .ddw/.zip theme into `themesDir` (see header for the steps).
ImportResult importTheme(const QString& zipPath, const QString& themesDir);

/// Delete a theme by name or path.  Refuses paths outside `themesDir`.
DeleteResult deleteTheme(const QString& nameOrPath, const QString& themesDir);

/// Human-readable theme name for display (tray tooltip, etc.): the
/// theme.json `displayName` with any trailing year (" 2023", " 2025-1")
/// stripped; falls back to the directory name when displayName is empty.
/// A mid-name year ("Chicago 2026 Mix") is part of the name and stays.
QString prettyThemeName(const QString& displayName, const QString& dirName);

}  // namespace johona::themes
