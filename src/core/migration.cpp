// migration.cpp — kWallpaper → Johona one-time migration.

#include "migration.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>

#include "shuffle.hpp"

namespace johona::migration {

namespace {

const char kOldConfigRel[] = "config/kwallpaper/config.json";
const char kOldThemesRel[] = "config/kwallpaper/themes";
const char kOldShuffleRel[] = "config/kwallpaper/shuffle-list.json";

QJsonObject readJsonObject(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject())
        return {};
    return doc.object();
}

QJsonObject section(const QJsonObject& obj, const char* key) {
    const QJsonValue v = obj.value(key);
    return v.isObject() ? v.toObject() : QJsonObject();
}

/// Recursive directory copy (Qt has removeRecursively but no
/// copyRecursively).
bool copyRecursively(const QString& src, const QString& dst, QString* error) {
    const QFileInfo srcInfo(src);
    if (srcInfo.isDir()) {
        QDir d(dst);
        if (!d.exists() && !d.mkpath(QStringLiteral(".")))
            return false;
        const auto entries =
            QDir(src).entryList(QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot, QDir::Name);
        for (const QString& name : entries) {
            if (!copyRecursively(src + '/' + name, dst + '/' + name, error))
                return false;
        }
        return true;
    }
    if (QFile::exists(dst) && !QFile::remove(dst))
        return false;
    if (!QFile::copy(src, dst)) {
        if (error)
            *error = QStringLiteral("Failed to copy %1").arg(src);
        return false;
    }
    return true;
}

/// Old kWallpaper v2 schema → Johona v1 (spec §9.4 mapping table).
/// Unmapped fields are dropped.
config::Config mapOldConfig(const QJsonObject& old, const QString& oldThemesDir,
                            const QString& newThemesDir) {
    config::Config cfg;  // all defaults

    const QJsonObject appearance = section(old, "appearance");
    if (appearance.contains("theme_mode"))
        cfg.themeMode = appearance["theme_mode"].toString();

    const QJsonObject autostart = section(old, "autostart");
    if (autostart.contains("enabled"))
        cfg.autostartEnabled = autostart["enabled"].toBool();
    if (autostart.contains("start_scheduler_on_launch"))
        cfg.startSchedulerOnLaunch = autostart["start_scheduler_on_launch"].toBool();

    const QJsonObject location = section(old, "location");
    if (location.contains("latitude"))
        cfg.latitude = location["latitude"].toDouble();
    if (location.contains("longitude"))
        cfg.longitude = location["longitude"].toDouble();
    if (location.contains("timezone"))
        cfg.timezone = location["timezone"].toString();
    if (location.contains("city"))
        cfg.city = location["city"].toString();

    const QJsonObject scheduling = section(old, "scheduling");
    if (scheduling.contains("daily_shuffle_enabled"))
        cfg.dailyShuffleEnabled = scheduling["daily_shuffle_enabled"].toBool();
    if (scheduling.contains("safety_interval"))
        cfg.safetyInterval = scheduling["safety_interval"].toInt(60);
    // DROPPED: cycle_interval, run_cycle, suntime_model (and any other
    // unmapped field).

    const QJsonObject theme = section(old, "theme");
    if (theme.contains("last_applied"))
        cfg.lastApplied = theme["last_applied"].toString();
    if (theme.contains("last_applied_image")) {
        QString img = theme["last_applied_image"].toString();
        // The themes moved to the new app dir: remap the prefix.  Existence
        // is checked on the OLD path (the copy happens after the config
        // conversion, so the remapped file does not exist yet); a stale old
        // path would defeat skip-if-unchanged, so clear it.
        const bool oldExists = QFileInfo::exists(img);
        if (!oldThemesDir.isEmpty() && img.startsWith(oldThemesDir)) {
            img = newThemesDir + img.mid(oldThemesDir.size());
        }
        cfg.lastAppliedImage = oldExists ? img : QString{};
    }
    return cfg;
}

}  // namespace

QString oldAppBaseDir() {
    QString home = qEnvironmentVariable("HOME");
    const QString suffix = QStringLiteral("/.var/app/top.spelunk.johona");
    if (home.endsWith(suffix))
        home.chop(suffix.size());
    return home + QStringLiteral("/.var/app/top.spelunk.kwallpaper");
}

Report migrateIfNeeded() {
    Report report;
    const auto paths = config::paths();
    if (QFileInfo::exists(paths.config))
        return report;  // idempotent: a Johona config already exists
    const QString oldBase = oldAppBaseDir();
    if (!QFileInfo::exists(oldBase))
        return report;
    return migrate(oldBase, paths);
}

Report migrate(const QString& oldBase, const config::Paths& targets) {
    Report report;
    report.ran = true;

    const QString oldThemesDir = oldBase + '/' + kOldThemesRel;
    const QString newThemesDir = targets.themesDir;

    // 1. Config (schema-mapped).
    const QString oldConfig = oldBase + '/' + kOldConfigRel;
    if (QFileInfo::exists(oldConfig)) {
        const QJsonObject old = readJsonObject(oldConfig);
        if (old.isEmpty()) {
            report.errors.append(QStringLiteral("Could not parse %1").arg(oldConfig));
        } else {
            config::Config cfg = mapOldConfig(old, oldThemesDir, newThemesDir);
            if (config::save(cfg, targets.config)) {
                report.configConverted = true;
                report.logLines.append(
                    QStringLiteral("Converted kWallpaper config to Johona v1"));
            } else {
                report.errors.append(QStringLiteral("Failed to write %1").arg(targets.config));
            }
        }
    } else {
        // Old app dir exists but was never configured: write defaults so the
        // trigger does not fire again.
        config::save(config::Config{}, targets.config);
        report.logLines.append(
            QStringLiteral("No kWallpaper config found; wrote Johona defaults"));
    }

    // 2. Themes (copy; the old dir is left untouched).
    const QDir oldThemes(oldThemesDir);
    if (oldThemes.exists()) {
        QDir().mkpath(newThemesDir);
        const auto entries = oldThemes.entryList(QDir::Dirs | QDir::NoDotAndDotDot,
                                                 QDir::Name);
        for (const QString& name : entries) {
            const QString src = oldThemes.absoluteFilePath(name);
            const QString dst = newThemesDir + '/' + name;
            if (QFileInfo::exists(dst)) {
                report.logLines.append(
                    QStringLiteral("Theme already present, skipped: %1").arg(name));
                continue;
            }
            QString copyError;
            if (copyRecursively(src, dst, &copyError)) {
                report.themesCopied++;
                report.logLines.append(QStringLiteral("Copied theme: %1").arg(name));
            } else {
                report.errors.append(
                    QStringLiteral("Failed to copy theme %1: %2").arg(name, copyError));
            }
        }
    } else {
        report.logLines.append(QStringLiteral("No kWallpaper themes found"));
    }

    // 3. Shuffle state (paths remapped to the new themes dir).
    const QString oldShuffle = oldBase + '/' + kOldShuffleRel;
    if (QFileInfo::exists(oldShuffle)) {
        const QJsonObject old = readJsonObject(oldShuffle);
        if (!old.isEmpty()) {
            shuffle::ShuffleState state;
            for (const auto& v : old.value("shuffle_list").toArray()) {
                QString p = v.toString();
                if (!oldThemesDir.isEmpty() && p.startsWith(oldThemesDir))
                    p = newThemesDir + p.mid(oldThemesDir.size());
                state.shuffleList.push_back(p);
            }
            state.currentIndex = old.value("current_index").toInt(0);
            state.lastUsedDate = old.value("last_used_date").toString();
            if (state.currentIndex < 0 ||
                state.currentIndex >= static_cast<int>(state.shuffleList.size()))
                state.currentIndex = 0;
            if (shuffle::saveShuffleState(state, targets.shuffleState)) {
                report.shuffleCopied = true;
                report.logLines.append(QStringLiteral("Migrated shuffle-list.json"));
            } else {
                report.errors.append(
                    QStringLiteral("Failed to write %1").arg(targets.shuffleState));
            }
        }
    }

    return report;
}

QString Report::summary() const {
    if (!ran)
        return QStringLiteral("Migration not needed (no kWallpaper data)");
    QString s = QStringLiteral("Migration complete: %1 theme(s) copied, config %2, shuffle %3")
                    .arg(themesCopied)
                    .arg(configConverted ? QStringLiteral("converted")
                                         : QStringLiteral("not converted"))
                    .arg(shuffleCopied ? QStringLiteral("migrated")
                                       : QStringLiteral("not found"));
    if (!errors.isEmpty())
        s += QStringLiteral(" | errors: %1").arg(errors.join(QStringLiteral("; ")));
    return s;
}

}  // namespace johona::migration
