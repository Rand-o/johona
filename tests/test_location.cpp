// test_location.cpp — LocationManager: tz table lookup, detect() priority,
// TZ env handling (with a disconnected mock D-Bus bus).

#include <QtTest>

#include <cmath>

#include "location.hpp"

using namespace johona;

namespace {

/// A lazy peer connection that never actually connects: isConnected() is
/// false, so every D-Bus probe short-circuits (no geoclue2 / timedate1).
backends::BusProvider mockBus() {
    return [](QDBusConnection::BusType) {
        return QDBusConnection::connectToPeer(QStringLiteral("nonexistent"),
                                              QStringLiteral("/nonexistent"));
    };
}

QString tzTablePath() {
    // Copied into the build tree by CMake (mirrors the installed layout).
    return QString(JOHONA_BUILD_DIR) +
           QStringLiteral("/share/johona/tz_coordinates.json");
}

}  // namespace

class TestLocation : public QObject {
    Q_OBJECT

private slots:
    void tzTable_loads();
    void tzTable_missingPath_empty();
    void current_fromConfig();
    void detect_timezoneLookup();
    void detect_keptCurrent();
    void systemTimezone_tzEnv();

private:
    location::LocationManager makeManager() {
        location::LocationManager::Deps deps;
        deps.bus = mockBus();
        deps.tzTablePath = tzTablePath();
        return location::LocationManager(std::move(deps));
    }
};

void TestLocation::tzTable_loads() {
    auto mgr = makeManager();
    const auto table = mgr.tzTable();
    QVERIFY(table.size() > 100);  // 427 IANA zones

    QVERIFY(table.contains("America/Phoenix"));
    const auto phoenix = table.value("America/Phoenix");
    QVERIFY(std::abs(phoenix.latitude - 33.4484) < 0.01);
    QVERIFY(std::abs(phoenix.longitude - (-112.074)) < 0.01);

    QVERIFY(table.contains("Europe/Berlin"));
    QVERIFY(std::abs(table.value("Europe/Berlin").latitude - 52.5) < 0.01);
}

void TestLocation::tzTable_missingPath_empty() {
    location::LocationManager::Deps deps;
    deps.bus = mockBus();
    deps.tzTablePath = QStringLiteral("/nonexistent/tz_coordinates.json");
    const location::LocationManager mgr(std::move(deps));
    QVERIFY(mgr.tzTable().isEmpty());
}

void TestLocation::current_fromConfig() {
    auto mgr = makeManager();
    config::Config cfg;
    cfg.city = "Tucson";
    cfg.latitude = 32.2226;
    cfg.longitude = -110.9747;
    cfg.timezone = "America/Phoenix";

    const location::Location loc = mgr.current(cfg);
    QCOMPARE(loc.city, QString("Tucson"));
    QCOMPARE(loc.latitude, 32.2226);
    QCOMPARE(loc.longitude, -110.9747);
    QCOMPARE(loc.timezone, QString("America/Phoenix"));
}

void TestLocation::detect_timezoneLookup() {
    auto mgr = makeManager();
    config::Config cfg;
    cfg.timezone = "America/Phoenix";

    QString source;
    const location::Location loc = mgr.detect(cfg, &source);
    QCOMPARE(source, QString("timezone-lookup"));
    QVERIFY(std::abs(loc.latitude - 33.4484) < 0.01);
    QVERIFY(std::abs(loc.longitude - (-112.074)) < 0.01);
    QCOMPARE(loc.timezone, QString("America/Phoenix"));
}

void TestLocation::detect_keptCurrent() {
    auto mgr = makeManager();
    config::Config cfg;
    cfg.city = "Kept";
    cfg.latitude = 1.5;
    cfg.longitude = 2.5;
    cfg.timezone = "Mars/Olympus";  // not in the tz table

    QString source;
    const location::Location loc = mgr.detect(cfg, &source);
    QCOMPARE(source, QString("kept-current"));
    QCOMPARE(loc.city, QString("Kept"));
    QCOMPARE(loc.latitude, 1.5);
    QCOMPARE(loc.longitude, 2.5);
}

void TestLocation::systemTimezone_tzEnv() {
    auto mgr = makeManager();

    qputenv("TZ", "America/Phoenix");
    QCOMPARE(mgr.systemTimezone(), QString("America/Phoenix"));

    qputenv("TZ", ":Europe/Berlin");  // colon prefix form
    QCOMPARE(mgr.systemTimezone(), QString("Europe/Berlin"));

    // No TZ env, no D-Bus → the system default.
    unsetenv("TZ");
    QCOMPARE(mgr.systemTimezone(),
             QString(QTimeZone::systemTimeZone().id().constData()));
}

QTEST_GUILESS_MAIN(TestLocation)
#include "test_location.moc"
