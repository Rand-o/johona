// location.hpp — LocationManager (spec §10).
//
// detect() priority:
//   1. Geoclue2 (system D-Bus, 10 s bounded request)
//   2. Timezone → coordinates lookup (bundled tz_coordinates.json)
//   3. Keep the current location + log a warning
//
// TZ watch: read the current IANA zone (TZ env primary — Flatpak sets it to
// the host zone — timedate1 fallback).  A change acts only when
// location_auto_update.on_timezone_change is ON (default OFF); DST
// transitions never trigger it (the zone string is unchanged).

#pragma once

#include <optional>

#include <QObject>

#include "backends.hpp"
#include "clock.hpp"
#include "config.hpp"

namespace johona::location {

struct Location {
    double latitude = 0.0;
    double longitude = 0.0;
    QString timezone;  // IANA
    QString city;
};

class LocationManager : public QObject {
    Q_OBJECT
public:
    struct Deps {
        backends::BusProvider bus;
        std::shared_ptr<Clock> clock;
        QString tzTablePath;  // "" → default search
        // A user-provided default constructor (rather than default member
        // initializers): NSDMIs of a nested class are not available in the
        // enclosing class's complete-class context, so `Deps deps = {}`
        // would not compile.
        Deps()
            : bus(backends::defaultBus), clock(std::make_shared<SystemClock>()) {}
    };

    explicit LocationManager(Deps deps = {}, QObject* parent = nullptr);

    Location current(const config::Config& cfg) const;

    /// Detect the location (priority above).  `source` receives
    /// "geoclue2" | "timezone-lookup" | "kept-current".
    Location detect(const config::Config& cfg, QString* source = nullptr);

    /// Current IANA timezone: TZ env (Flatpak sets it to the host zone),
    /// then timedate1 system D-Bus, then the system default.
    QString systemTimezone();

    /// One TZ-watch step (spec §10): if the system zone differs from the
    /// config zone AND the opt-in toggle is on → detect() → save config →
    /// emit.  Returns true when the config changed.
    bool checkTimezoneChange(config::Config& cfg, const config::Paths& paths);

    /// Load the bundled tz → coordinates table (zone → [lat, lon, city]).
    /// Exposed for tests.
    QHash<QString, Location> tzTable() const;

signals:
    void locationUpdated(const Location& loc, const QString& source);
    void logMessage(const QString& message);

private:
    std::optional<Location> geoclue2Fix(int timeoutMs = 10000);
    QString findTzTable() const;

    backends::BusProvider m_bus;
    std::shared_ptr<Clock> m_clock;
    QString m_tzTablePath;
    mutable QHash<QString, Location> m_tzTableCache;
    mutable bool m_tzTableLoaded = false;
};

}  // namespace johona::location
