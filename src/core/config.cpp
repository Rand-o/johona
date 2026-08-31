// config.cpp — Johona configuration implementation.

#include "config.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QStandardPaths>
#include <QTimeZone>

namespace johona::config {

Paths paths() {
    Paths p;
    p.configDir =
        QDir(QStandardPaths::writableLocation(QStandardPaths::ConfigLocation)).absolutePath() +
        QStringLiteral("/johona");
    p.config = p.configDir + QStringLiteral("/config.json");
    p.themesDir =
        QDir(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation))
            .absolutePath() + QStringLiteral("/johona/themes");
    // Direct XDG_CACHE_HOME (not QStandardPaths::CacheLocation, which
    // appends the application name → would give cache/johona/johona in the
    // production binary and differ per test binary).
    const QByteArray cacheHome = qgetenv("XDG_CACHE_HOME");
    p.cacheDir =
        QDir(cacheHome.isEmpty() ? QDir::homePath() + QStringLiteral("/.cache")
                                 : QString::fromLocal8Bit(cacheHome))
            .absolutePath() + QStringLiteral("/johona");
    p.shuffleState = p.configDir + QStringLiteral("/shuffle-list.json");
    return p;
}

QVariantMap Config::toMap() const {
    return {
        {"version", version},
        {"appearance",
         QVariantMap{{"theme_mode", themeMode}}},
        {"autostart",
         QVariantMap{{"enabled", autostartEnabled},
                     {"start_scheduler_on_launch", startSchedulerOnLaunch}}},
        {"location",
         QVariantMap{{"city", city},
                     {"latitude", latitude},
                     {"longitude", longitude},
                     {"timezone", timezone}}},
        {"location_auto_update", QVariantMap{{"on_timezone_change", onTimezoneChange}}},
        {"scheduling",
         QVariantMap{{"safety_interval", safetyInterval},
                     {"daily_shuffle_enabled", dailyShuffleEnabled}}},
        {"backend", QVariantMap{{"override", backendOverride}}},
        {"theme",
         QVariantMap{{"last_applied", lastApplied},
                     {"last_applied_image", lastAppliedImage}}},
    };
}

namespace {

// Strict type checks: QVariant::canConvert() is true for many cross-type
// coercions ("soon" → 0, 3.14 → "3.14"), which would silently corrupt a
// config on a bad key.  Only accept the exact JSON type family.
bool isString(const QVariant& v) { return v.userType() == QMetaType::QString; }
bool isNumber(const QVariant& v) {
    switch (v.userType()) {
    case QMetaType::Int:
    case QMetaType::UInt:
    case QMetaType::LongLong:
    case QMetaType::ULongLong:
    case QMetaType::Double:
    case QMetaType::Float:
        return true;
    default:
        return false;
    }
}
bool isBool(const QVariant& v) { return v.userType() == QMetaType::Bool; }

QString str(const QVariantMap& m, const char* key, const QString& def) {
    const QVariant v = m.value(key);
    return isString(v) ? v.toString() : def;
}
double num(const QVariantMap& m, const char* key, double def) {
    const QVariant v = m.value(key);
    return isNumber(v) ? v.toDouble() : def;
}
bool boolean(const QVariantMap& m, const char* key, bool def) {
    const QVariant v = m.value(key);
    return isBool(v) ? v.toBool() : def;
}
int integer(const QVariantMap& m, const char* key, int def) {
    const QVariant v = m.value(key);
    return isNumber(v) ? v.toInt() : def;
}
const QVariantMap sub(const QVariantMap& m, const char* key) {
    return m.value(key).toMap();
}

}  // namespace

Config Config::fromMap(const QVariantMap& map) {
    Config c;
    c.version = integer(map, "version", 1);

    const QVariantMap appearance = sub(map, "appearance");
    c.themeMode = str(appearance, "theme_mode", "system");
    if (c.themeMode != "system" && c.themeMode != "light" && c.themeMode != "dark")
        c.themeMode = "system";

    const QVariantMap autostart = sub(map, "autostart");
    c.autostartEnabled = boolean(autostart, "enabled", false);
    c.startSchedulerOnLaunch = boolean(autostart, "start_scheduler_on_launch", true);

    const QVariantMap location = sub(map, "location");
    c.city = str(location, "city", "");
    c.latitude = num(location, "latitude", 33.4484);
    c.longitude = num(location, "longitude", -112.074);
    c.timezone = str(location, "timezone", "America/Phoenix");
    if (!isValidTimezone(c.timezone))
        c.timezone = "America/Phoenix";

    const QVariantMap autoUpdate = sub(map, "location_auto_update");
    c.onTimezoneChange = boolean(autoUpdate, "on_timezone_change", false);

    const QVariantMap scheduling = sub(map, "scheduling");
    c.safetyInterval = integer(scheduling, "safety_interval", 60);
    if (c.safetyInterval < 5)
        c.safetyInterval = 5;  // sanity floor
    if (c.safetyInterval > 3600)
        c.safetyInterval = 3600;
    c.dailyShuffleEnabled = boolean(scheduling, "daily_shuffle_enabled", true);

    const QVariantMap backend = sub(map, "backend");
    c.backendOverride = str(backend, "override", "auto");
    if (c.backendOverride != "auto" && c.backendOverride != "plasma" &&
        c.backendOverride != "portal" && c.backendOverride != "gnome" &&
        c.backendOverride != "xdg_settings")
        c.backendOverride = "auto";

    const QVariantMap theme = sub(map, "theme");
    c.lastApplied = str(theme, "last_applied", "");
    c.lastAppliedImage = str(theme, "last_applied_image", "");
    return c;
}

Config load(const QString& path) {
    const QString p = path.isEmpty() ? paths().config : path;
    QFile f(p);
    if (!f.exists() || !f.open(QIODevice::ReadOnly))
        return Config{};  // defaults
    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject())
        return Config{};  // corrupt → defaults, file left in place
    return Config::fromMap(doc.object().toVariantMap());
}

bool save(const Config& cfg, const QString& path) {
    const QString p = path.isEmpty() ? paths().config : path;
    const QDir dir = QFileInfo(p).absoluteDir();
    if (!dir.exists() && !dir.mkpath(QStringLiteral(".")))
        return false;

    // Atomic write: temp file in the same directory + rename.
    const QString tmp = p + QStringLiteral(".tmp");
    {
        QFile f(tmp);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
            return false;
        const QJsonDocument doc = QJsonDocument(QJsonObject::fromVariantMap(cfg.toMap()));
        f.write(doc.toJson(QJsonDocument::Indented));
        if (!f.flush())
            return false;
    }
    if (QFile::exists(p) && !QFile::remove(p))
        return false;
    if (!QFile::rename(tmp, p)) {
        QFile::remove(tmp);
        return false;
    }
    return true;
}

bool isValidTimezone(const QString& id) {
    return QTimeZone::availableTimeZoneIds().contains(id);
}

}  // namespace johona::config
