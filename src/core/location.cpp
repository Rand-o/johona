// location.cpp — LocationManager implementation.

#include "location.hpp"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusMessage>
#include <QDir>
#include <QEventLoop>
#include <QJsonArray>
#include <QJsonObject>
#include <QFile>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QStandardPaths>
#include <QTimer>
#include <QTimeZone>

namespace johona::location {

LocationManager::LocationManager(Deps deps, QObject* parent)
    : QObject(parent),
      m_bus(std::move(deps.bus)),
      m_clock(std::move(deps.clock)),
      m_tzTablePath(std::move(deps.tzTablePath)) {
    if (!m_clock)
        m_clock = std::make_shared<SystemClock>();
}

Location LocationManager::current(const config::Config& cfg) const {
    Location loc;
    loc.latitude = cfg.latitude;
    loc.longitude = cfg.longitude;
    loc.timezone = cfg.timezone;
    loc.city = cfg.city;
    return loc;
}

// ---------------------------------------------------------------------------
// Geoclue2 (system D-Bus)
// ---------------------------------------------------------------------------

/// Parse a GeoClue2 `Location` property value (a{sv} map, possibly
/// wrapped in a tuple by some versions) into a Location.  Returns
/// nullopt when the value does not carry a usable fix.
static std::optional<Location> parseGeoClueLocation(const QVariant& v) {
    QVariantMap loc = v.toMap();
    if (loc.isEmpty() && v.typeId() == QMetaType::QVariantList) {
        const auto list = v.toList();
        if (!list.isEmpty())
            loc = list.first().toMap();
    }
    if (loc.isEmpty())
        return std::nullopt;
    Location l;
    l.latitude = loc.value(QStringLiteral("Latitude")).toDouble();
    l.longitude = loc.value(QStringLiteral("Longitude")).toDouble();
    l.timezone = loc.value(QStringLiteral("Timezone")).toString();
    return l;
}

std::optional<Location> LocationManager::geoclue2Fix(int timeoutMs) {
    const QDBusConnection conn = m_bus(QDBusConnection::SystemBus);
    if (!conn.isConnected())
        return std::nullopt;
    QDBusInterface mgr(QStringLiteral("org.freedesktop.GeoClue2"),
                       QStringLiteral("/org/freedesktop/GeoClue2/Manager"),
                       QStringLiteral("org.freedesktop.GeoClue2.Manager"), conn);
    if (!mgr.isValid())
        return std::nullopt;

    const QDBusMessage createReply = mgr.call(QStringLiteral("CreateClient"), QStringLiteral("johona"));
    if (createReply.type() != QDBusMessage::ReplyMessage)
        return std::nullopt;
    const QString clientPath =
        createReply.arguments().first().value<QDBusObjectPath>().path();
    QDBusInterface client(QStringLiteral("org.freedesktop.GeoClue2"), clientPath,
                          QStringLiteral("org.freedesktop.GeoClue2.Client"), conn);

    // Poll the client's Location property until a fix arrives or the
    // timeout elapses.  (Polling avoids the SDK QtDBus headers' missing
    // functor connect() overload and is robust across GeoClue versions.)
    std::optional<Location> fix;
    QEventLoop loop;
    QTimer pollTimer;
    pollTimer.setInterval(500);
    QObject::connect(&pollTimer, &QTimer::timeout, [&] {
        // QDBusAbstractInterface::property takes a const char*.
        if (auto l = parseGeoClueLocation(client.property("Location"))) {
            if (l->latitude != 0.0 || l->longitude != 0.0) {
                fix = std::move(l);
                loop.quit();
            }
        }
    });
    QTimer timeout;
    timeout.setSingleShot(true);
    timeout.setInterval(timeoutMs);
    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);

    client.call(QStringLiteral("Start"));
    pollTimer.start();
    timeout.start();
    loop.exec();
    pollTimer.stop();

    // Cleanup (best effort).
    client.call(QStringLiteral("Stop"));
    mgr.call(QStringLiteral("DeleteClient"), QDBusObjectPath(clientPath));
    return fix;
}

// ---------------------------------------------------------------------------
// tz → coordinates table
// ---------------------------------------------------------------------------

QString LocationManager::findTzTable() const {
    if (!m_tzTablePath.isEmpty())
        return m_tzTablePath;
    const QString appDir = QCoreApplication::applicationDirPath();
    const QStringList candidates = {
        appDir + QStringLiteral("/../share/johona/tz_coordinates.json"),
        QStandardPaths::locate(QStandardPaths::GenericDataLocation,
                               QStringLiteral("johona/tz_coordinates.json")),
        QStringLiteral("/app/share/johona/tz_coordinates.json"),  // Flatpak
    };
    for (const QString& c : candidates)
        if (QFileInfo::exists(c))
            return c;
    return {};
}

QHash<QString, Location> LocationManager::tzTable() const {
    if (m_tzTableLoaded)
        return m_tzTableCache;
    m_tzTableLoaded = true;
    const QString path = findTzTable();
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return m_tzTableCache;
    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject())
        return m_tzTableCache;
    const QJsonObject obj = doc.object();
    for (auto it = obj.constBegin(); it != obj.constEnd(); ++it) {
        const QJsonValue v = it.value();
        if (!v.isArray() || v.toArray().size() < 2)
            continue;
        const QJsonArray arr = v.toArray();
        Location loc;
        loc.latitude = arr.at(0).toDouble();
        loc.longitude = arr.at(1).toDouble();
        if (arr.size() > 2)
            loc.city = arr.at(2).toString();
        loc.timezone = it.key();
        m_tzTableCache.insert(it.key(), loc);
    }
    return m_tzTableCache;
}

// ---------------------------------------------------------------------------
// detect
// ---------------------------------------------------------------------------

Location LocationManager::detect(const config::Config& cfg, QString* source) {
    Location kept = current(cfg);

    // 1. Geoclue2.
    if (auto fix = geoclue2Fix(10000)) {
        if (fix->latitude != 0.0 || fix->longitude != 0.0) {
            Location loc = *fix;
            if (loc.timezone.isEmpty() || !QTimeZone(loc.timezone.toUtf8()).isValid())
                loc.timezone = cfg.timezone;
            if (source)
                *source = QStringLiteral("geoclue2");
            emit logMessage(QStringLiteral("Location detected via Geoclue2: %1, %2")
                                .arg(loc.latitude, 0, 'f', 4)
                                .arg(loc.longitude, 0, 'f', 4));
            return loc;
        }
    }

    // 2. Timezone → coordinates lookup (offline fallback).
    const auto table = tzTable();
    if (auto it = table.constFind(cfg.timezone); it != table.constEnd()) {
        Location loc = it.value();
        loc.timezone = cfg.timezone;
        if (source)
            *source = QStringLiteral("timezone-lookup");
        emit logMessage(QStringLiteral("Location from timezone lookup (%1): %2, %3")
                            .arg(cfg.timezone)
                            .arg(loc.city)
                            .arg(loc.latitude, 0, 'f', 4)
                            .arg(loc.longitude, 0, 'f', 4));
        return loc;
    }

    // 3. Keep the current location.
    if (source)
        *source = QStringLiteral("kept-current");
    emit logMessage(QStringLiteral(
        "Location detection failed; keeping current location (it may be stale)"));
    return kept;
}

// ---------------------------------------------------------------------------
// TZ watch
// ---------------------------------------------------------------------------

QString LocationManager::systemTimezone() {
    // Primary: the TZ environment variable (Flatpak sets it to the host zone).
    QString tz = qEnvironmentVariable("TZ");
    if (tz.startsWith(QLatin1Char(':')))
        tz.remove(0, 1);
    if (!tz.isEmpty() && QTimeZone(tz.toUtf8()).isValid())
        return tz;

    // Fallback: org.freedesktop.timedate1 (system D-Bus).
    const QDBusConnection conn = m_bus(QDBusConnection::SystemBus);
    if (conn.isConnected()) {
        const QDBusInterface td(QStringLiteral("org.freedesktop.timedate1"),
                                QStringLiteral("/org/freedesktop/timedate1"),
                                QStringLiteral("org.freedesktop.timedate1"), conn);
        if (td.isValid()) {
            const QString z = td.property("Timezone").toString();
            if (!z.isEmpty() && QTimeZone(z.toUtf8()).isValid())
                return z;
        }
    }

    // Last resort: the system default.
    return QTimeZone::systemTimeZone().id().constData();
}

bool LocationManager::checkTimezoneChange(config::Config& cfg, const config::Paths& paths) {
    const QString sysTz = systemTimezone();
    if (sysTz.isEmpty() || sysTz == cfg.timezone)
        return false;
    if (!cfg.onTimezoneChange)
        return false;  // opt-in OFF: never act

    QString source;
    const Location loc = detect(cfg, &source);

    cfg.timezone = sysTz;  // the zone itself always updates
    cfg.latitude = loc.latitude;
    cfg.longitude = loc.longitude;
    if (!loc.city.isEmpty())
        cfg.city = loc.city;
    config::save(cfg, paths.config);

    emit locationUpdated(loc, source);
    emit logMessage(QStringLiteral("Timezone changed to %1 — location updated (%2)")
                        .arg(sysTz, source));
    return true;
}

}  // namespace johona::location
