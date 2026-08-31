// config.hpp — Johona configuration (v1 schema), XDG paths, atomic writes.
//
// Schema (see docs/superpowers/specs/2026-08-27-johona-wallpaper-design.md
// §9.2):
//
//   {
//     "version": 1,
//     "appearance":   { "theme_mode": "system" },
//     "autostart":    { "enabled": false, "start_scheduler_on_launch": true },
//     "location":     { "city": "", "latitude": 33.4484, "longitude": -112.074,
//                       "timezone": "America/Phoenix" },
//     "location_auto_update": { "on_timezone_change": false },
//     "scheduling":   { "safety_interval": 60,
//                       "daily_shuffle_enabled": true },
//     "backend":      { "override": "auto" },
//     "theme":        { "last_applied": "", "last_applied_image": "" }
//   }
//
// There is deliberately no `suntime_model` field: the legacy time model is
// removed entirely (WDD sun-segment model only).

#pragma once

#include <QString>
#include <QVariant>

namespace johona::config {

/// XDG-based paths.  Under the Flatpak sandbox these resolve to
/// ~/.var/app/top.spelunk.johona/{config,data,cache}/... automatically.
struct Paths {
    QString configDir;   // $XDG_CONFIG_HOME/johona
    QString config;      // configDir/config.json
    QString themesDir;   // $XDG_DATA_HOME/johona/themes
    QString cacheDir;    // $XDG_CACHE_HOME/johona
    QString shuffleState; // configDir/shuffle-list.json
};

Paths paths();

struct Config {
    int version = 1;

    // appearance
    QString themeMode = "system";  // "system" | "light" | "dark"

    // autostart
    bool autostartEnabled = false;
    bool startSchedulerOnLaunch = true;

    // location
    QString city;
    double latitude = 33.4484;   // Phoenix, AZ (kWallpaper default)
    double longitude = -112.074;
    QString timezone = "America/Phoenix";

    // location_auto_update
    bool onTimezoneChange = false;  // opt-in, default OFF

    // scheduling
    int safetyInterval = 60;  // seconds
    bool dailyShuffleEnabled = true;

    // backend
    QString backendOverride = "auto";  // auto|plasma|portal|gnome|xdg_settings

    // theme
    QString lastApplied;        // theme name
    QString lastAppliedImage;   // image file name

    QVariantMap toMap() const;
    static Config fromMap(const QVariantMap& map);  // fills missing keys with defaults
};

/// Load the config from `path` (or the default path when empty).  Missing
/// file or file → defaults.  Corrupt file → defaults (the corrupt file is
/// left in place).  Missing keys are filled from defaults.
Config load(const QString& path = {});

/// Save atomically (temp file + rename).  Returns false on I/O failure.
bool save(const Config& cfg, const QString& path = {});

/// Validate a timezone id against QTimeZone::availableTimeZoneIds.
bool isValidTimezone(const QString& id);

}  // namespace johona::config
