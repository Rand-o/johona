// test_config.cpp — config v1 schema: defaults, round-trip, corruption,
// type coercion, timezone validation, XDG paths.

#include <QtTest>

#include <QFile>
#include <QStandardPaths>

#include "config.hpp"

using namespace johona::config;

class TestConfig : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() { QVERIFY(m_tmp.isValid()); }
    void defaults();
    void toMapFromMap_roundtrip();
    void fromMap_missingKeys_getDefaults();
    void fromMap_badTypes_getDefaults();
    void fromMap_invalidEnums_getDefaults();
    void saveLoad_roundtrip();
    void load_missingFile_defaults();
    void load_corruptFile_defaultsAndKept();
    void save_createsParentDirs();
    void tzValidation();
    void paths_useXdgEnv();

private:
    QTemporaryDir m_tmp;
};

static bool sameConfig(const Config& a, const Config& b) {
    return a.version == b.version && a.themeMode == b.themeMode &&
           a.autostartEnabled == b.autostartEnabled &&
           a.startSchedulerOnLaunch == b.startSchedulerOnLaunch &&
           a.city == b.city && a.latitude == b.latitude &&
           a.longitude == b.longitude && a.timezone == b.timezone &&
           a.onTimezoneChange == b.onTimezoneChange &&
           a.safetyInterval == b.safetyInterval &&
           a.cycleEnabled == b.cycleEnabled &&
           a.dailyShuffleEnabled == b.dailyShuffleEnabled &&
           a.backendOverride == b.backendOverride &&
           a.lastApplied == b.lastApplied && a.lastAppliedImage == b.lastAppliedImage;
}

void TestConfig::defaults() {
    const Config c;
    QCOMPARE(c.version, 1);
    QCOMPARE(c.themeMode, QString("system"));
    QVERIFY(!c.autostartEnabled);
    QVERIFY(c.startSchedulerOnLaunch);
    QCOMPARE(c.city, QString(""));
    QCOMPARE(c.latitude, 33.4484);
    QCOMPARE(c.longitude, -112.074);
    QCOMPARE(c.timezone, QString("America/Phoenix"));
    QVERIFY(!c.onTimezoneChange);
    QCOMPARE(c.safetyInterval, 60);
    QVERIFY(c.cycleEnabled);
    QVERIFY(c.dailyShuffleEnabled);
    QCOMPARE(c.backendOverride, QString("auto"));
    QCOMPARE(c.lastApplied, QString(""));
    QCOMPARE(c.lastAppliedImage, QString(""));
}

void TestConfig::toMapFromMap_roundtrip() {
    Config c;
    c.themeMode = "dark";
    c.autostartEnabled = true;
    c.startSchedulerOnLaunch = false;
    c.city = "Tucson";
    c.latitude = 32.2226;
    c.longitude = -110.9747;
    c.timezone = "America/Phoenix";
    c.onTimezoneChange = true;
    c.safetyInterval = 120;
    c.cycleEnabled = false;
    c.dailyShuffleEnabled = false;
    c.backendOverride = "plasma";
    c.lastApplied = "sunset_pack";
    c.lastAppliedImage = "img_001.jpg";

    const Config d = Config::fromMap(c.toMap());
    QVERIFY(sameConfig(c, d));
}

void TestConfig::fromMap_missingKeys_getDefaults() {
    const Config c = Config::fromMap({});
    const Config def;
    QVERIFY(sameConfig(c, def));
}

void TestConfig::fromMap_badTypes_getDefaults() {
    // Wrong types everywhere → every field falls back to its default.
    const QVariantMap map{
        {"version", "not-an-int"},
        {"appearance", QVariantMap{{"theme_mode", 42}}},
        {"autostart", QVariantMap{{"enabled", "yes-please"}}},
        {"location",
         QVariantMap{{"city", 3.14},
                     {"latitude", "north"},
                     {"longitude", {}},
                     {"timezone", 7}}},
        {"scheduling", QVariantMap{{"safety_interval", "soon"}}},
        {"backend", QVariantMap{{"override", true}}},
    };
    const Config c = Config::fromMap(map);
    const Config def;
    QVERIFY(sameConfig(c, def));
}

void TestConfig::fromMap_invalidEnums_getDefaults() {
    const QVariantMap map{
        {"appearance", QVariantMap{{"theme_mode", "neon"}}},
        {"backend", QVariantMap{{"override", "web"}}},
        {"location", QVariantMap{{"timezone", "Mars/Olympus"}}},
    };
    const Config c = Config::fromMap(map);
    QCOMPARE(c.themeMode, QString("system"));
    QCOMPARE(c.backendOverride, QString("auto"));
    QCOMPARE(c.timezone, QString("America/Phoenix"));
}

void TestConfig::saveLoad_roundtrip() {
    const QString path = m_tmp.path() + QStringLiteral("/cfg/config.json");
    Config c;
    c.themeMode = "light";
    c.city = "Flagstaff";
    c.latitude = 35.1983;
    c.longitude = -111.6513;
    c.safetyInterval = 90;
    c.lastApplied = "dawn_pack";

    QVERIFY(save(c, path));
    const Config d = load(path);
    QVERIFY(sameConfig(c, d));
}

void TestConfig::load_missingFile_defaults() {
    const Config c = load(m_tmp.path() + QStringLiteral("/nope/missing.json"));
    const Config def;
    QVERIFY(sameConfig(c, def));
}

void TestConfig::load_corruptFile_defaultsAndKept() {
    const QString path = m_tmp.path() + QStringLiteral("/corrupt.json");
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("{ this is not json ]]]");
    f.close();

    const Config c = load(path);
    const Config def;
    QVERIFY(sameConfig(c, def));
    // The corrupt file is left in place (for the user to inspect).
    QVERIFY(QFile::exists(path));
}

void TestConfig::save_createsParentDirs() {
    const QString path = m_tmp.path() + QStringLiteral("/a/b/c/config.json");
    QVERIFY(save(Config{}, path));
    QVERIFY(QFile::exists(path));
}

void TestConfig::tzValidation() {
    QVERIFY(isValidTimezone("America/Phoenix"));
    QVERIFY(isValidTimezone("Europe/Berlin"));
    QVERIFY(!isValidTimezone("Mars/Olympus"));
    QVERIFY(!isValidTimezone(""));
}

void TestConfig::paths_useXdgEnv() {
    const QString base = m_tmp.path();
    const bool hasCfg = qEnvironmentVariableIsSet("XDG_CONFIG_HOME");
    const bool hasData = qEnvironmentVariableIsSet("XDG_DATA_HOME");
    const bool hasCache = qEnvironmentVariableIsSet("XDG_CACHE_HOME");
    const QByteArray oldCfg = qgetenv("XDG_CONFIG_HOME");
    const QByteArray oldData = qgetenv("XDG_DATA_HOME");
    const QByteArray oldCache = qgetenv("XDG_CACHE_HOME");
    qputenv("XDG_CONFIG_HOME", (base + "/cfg").toUtf8());
    qputenv("XDG_DATA_HOME", (base + "/data").toUtf8());
    qputenv("XDG_CACHE_HOME", (base + "/cache").toUtf8());

    const Paths p = paths();
    QCOMPARE(p.configDir, base + "/cfg/johona");
    QCOMPARE(p.config, base + "/cfg/johona/config.json");
    QCOMPARE(p.themesDir, base + "/data/johona/themes");
    QCOMPARE(p.cacheDir, base + "/cache/johona");
    QCOMPARE(p.shuffleState, base + "/cfg/johona/shuffle-list.json");

    // Restore (QStandardPaths reads the environment on every call).
    if (hasCfg)
        qputenv("XDG_CONFIG_HOME", oldCfg);
    else
        unsetenv("XDG_CONFIG_HOME");
    if (hasData)
        qputenv("XDG_DATA_HOME", oldData);
    else
        unsetenv("XDG_DATA_HOME");
    if (hasCache)
        qputenv("XDG_CACHE_HOME", oldCache);
    else
        unsetenv("XDG_CACHE_HOME");
}

QTEST_GUILESS_MAIN(TestConfig)
#include "test_config.moc"
