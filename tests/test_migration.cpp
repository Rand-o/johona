// test_migration.cpp — kWallpaper → Johona one-time migration: schema
// mapping, theme copy, path remapping, idempotency, old-dir preservation.

#include <QtTest>

#include "migration.hpp"
#include "shuffle.hpp"

using namespace johona;

namespace {
const char* kThemeJson = R"({"displayName":"Solar","imageFilename":"img_*.jpg",
    "sunriseImageList":[1],"dayImageList":[2],"sunsetImageList":[],
    "nightImageList":[]})";
}  // namespace

class TestMigration : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void migrate_full();
    void migrate_staleImage_cleared();
    void migrate_secondRun_skipsExistingThemes();
    void migrate_noOldConfig_writesDefaults();
    void migrate_oldDirMissing();
    void oldAppBaseDir_hostAndSandbox();

private:
    QTemporaryDir m_tmp;
    QString m_tmpBase;

    /// Per-test isolation: unique old base + target paths.
    struct Fixture {
        QString oldBase;
        QString oldThemes;
        config::Paths targets;
    };

    Fixture fixture(const QString& tag) {
        Fixture f;
        f.oldBase = m_tmpBase + "/old_" + tag;
        f.oldThemes = f.oldBase + "/config/kwallpaper/themes";
        f.targets.config = m_tmpBase + "/new_" + tag + "/cfg/johona/config.json";
        f.targets.themesDir = m_tmpBase + "/new_" + tag + "/data/johona/themes";
        f.targets.shuffleState =
            m_tmpBase + "/new_" + tag + "/cfg/johona/shuffle-list.json";
        return f;
    }

    void makeOldTheme(const Fixture& f) {
        QDir().mkpath(f.oldThemes + "/solar");
        {
            QFile file(f.oldThemes + "/solar/theme.json");
            file.open(QIODevice::WriteOnly);
            file.write(kThemeJson);
        }
        for (int i = 1; i <= 2; i++) {
            QFile file(f.oldThemes + QStringLiteral("/solar/img_%1.jpg").arg(i));
            file.open(QIODevice::WriteOnly);
            file.write("x");
        }
    }

    void writeOldConfig(const Fixture& f, const QJsonObject& obj) {
        QDir().mkpath(f.oldBase + "/config/kwallpaper");
        QFile file(f.oldBase + "/config/kwallpaper/config.json");
        file.open(QIODevice::WriteOnly);
        file.write(QJsonDocument(obj).toJson());
    }
};

void TestMigration::initTestCase() {
    QVERIFY(m_tmp.isValid());
    m_tmpBase = m_tmp.path();
}

void TestMigration::migrate_full() {
    const Fixture f = fixture("full");
    makeOldTheme(f);
    QJsonObject old;
    old["version"] = 2;
    old["appearance"] = QJsonObject{{"theme_mode", "dark"}};
    old["autostart"] =
        QJsonObject{{"enabled", true}, {"start_scheduler_on_launch", false}};
    old["location"] = QJsonObject{
        {"latitude", 32.2226}, {"longitude", -110.9747},
        {"timezone", "America/Phoenix"}, {"city", "Tucson"}};
    old["scheduling"] = QJsonObject{
        {"cycle_interval", 60},          // dropped (legacy)
        {"run_cycle", false},            // dropped (legacy)
        {"daily_shuffle_enabled", false},
        {"safety_interval", 120},
        {"suntime_model", "legacy"}};    // dropped (legacy)
    old["theme"] = QJsonObject{
        {"last_applied", "solar"},
        {"last_applied_image", f.oldThemes + "/solar/img_2.jpg"}};
    writeOldConfig(f, old);

    // Shuffle state referencing the old themes dir.
    {
        QFile file(f.oldBase + "/config/kwallpaper/shuffle-list.json");
        file.open(QIODevice::WriteOnly);
        file.write(QJsonDocument(QJsonObject{
                         {"shuffle_list", QJsonArray{f.oldThemes + "/solar"}},
                         {"current_index", 0},
                         {"last_used_date", "2026-01-01"}})
                        .toJson());
    }

    const migration::Report report = migration::migrate(f.oldBase, f.targets);
    QVERIFY(report.ran);
    QVERIFY(report.configConverted);
    QCOMPARE(report.themesCopied, 1);
    QVERIFY(report.shuffleCopied);
    QVERIFY2(report.errors.isEmpty(), qPrintable(report.errors.join("; ")));

    // Config mapped to v1.
    const config::Config cfg = config::load(f.targets.config);
    QCOMPARE(cfg.themeMode, QString("dark"));
    QVERIFY(cfg.autostartEnabled);
    QVERIFY(!cfg.startSchedulerOnLaunch);
    QCOMPARE(cfg.latitude, 32.2226);
    QCOMPARE(cfg.longitude, -110.9747);
    QCOMPARE(cfg.timezone, QString("America/Phoenix"));
    QCOMPARE(cfg.city, QString("Tucson"));
    QVERIFY(!cfg.dailyShuffleEnabled);
    QCOMPARE(cfg.safetyInterval, 120);
    QCOMPARE(cfg.lastApplied, QString("solar"));
    // Image path remapped to the new themes dir.
    QCOMPARE(cfg.lastAppliedImage, f.targets.themesDir + "/solar/img_2.jpg");

    // Theme copied (old dir untouched).
    QVERIFY(QFileInfo::exists(f.targets.themesDir + "/solar/theme.json"));
    QVERIFY(QFileInfo::exists(f.targets.themesDir + "/solar/img_2.jpg"));
    QVERIFY(QFileInfo::exists(f.oldThemes + "/solar/theme.json"));

    // Shuffle state remapped.
    const shuffle::ShuffleState shuf =
        shuffle::loadShuffleState(f.targets.shuffleState);
    QCOMPARE(shuf.shuffleList.size(), static_cast<size_t>(1));
    QCOMPARE(shuf.shuffleList[0], f.targets.themesDir + "/solar");
    QCOMPARE(shuf.currentIndex, 0);
    QCOMPARE(shuf.lastUsedDate, QString("2026-01-01"));
}

void TestMigration::migrate_staleImage_cleared() {
    const Fixture f = fixture("stale");
    makeOldTheme(f);
    QJsonObject old;
    old["version"] = 2;
    old["theme"] = QJsonObject{
        {"last_applied", "solar"},
        // References an image that does not exist in the old theme.
        {"last_applied_image", f.oldThemes + "/solar/img_99.jpg"}};
    writeOldConfig(f, old);

    const migration::Report report = migration::migrate(f.oldBase, f.targets);
    QVERIFY(report.configConverted);

    const config::Config cfg = config::load(f.targets.config);
    QCOMPARE(cfg.lastApplied, QString("solar"));
    // Stale image path cleared (would defeat skip-if-unchanged).
    QVERIFY(cfg.lastAppliedImage.isEmpty());
}

void TestMigration::migrate_secondRun_skipsExistingThemes() {
    const Fixture f = fixture("second");
    makeOldTheme(f);
    QJsonObject old;
    old["version"] = 2;
    writeOldConfig(f, old);

    const migration::Report first = migration::migrate(f.oldBase, f.targets);
    QCOMPARE(first.themesCopied, 1);

    const migration::Report second = migration::migrate(f.oldBase, f.targets);
    QCOMPARE(second.themesCopied, 0);  // already present → skipped
    QVERIFY(QFileInfo::exists(f.targets.themesDir + "/solar/theme.json"));
}

void TestMigration::migrate_noOldConfig_writesDefaults() {
    const Fixture f = fixture("noconf");
    // Old base exists but was never configured.
    QDir().mkpath(f.oldBase + "/config/kwallpaper");
    const migration::Report report = migration::migrate(f.oldBase, f.targets);
    QVERIFY(report.ran);
    QVERIFY(!report.configConverted);
    // A defaults config is written so the trigger does not fire again.
    QVERIFY(QFileInfo::exists(f.targets.config));
    const config::Config cfg = config::load(f.targets.config);
    QCOMPARE(cfg.themeMode, QString("system"));
}

void TestMigration::migrate_oldDirMissing() {
    const Fixture f = fixture("missing");
    // Old base does not exist at all: nothing to copy, defaults written.
    const migration::Report report = migration::migrate(f.oldBase, f.targets);
    QVERIFY(report.ran);
    QVERIFY(!report.configConverted);
    QCOMPARE(report.themesCopied, 0);
    QVERIFY(QFileInfo::exists(f.targets.config));
}

void TestMigration::oldAppBaseDir_hostAndSandbox() {
    const QString home = m_tmpBase + "/home";
    qputenv("HOME", home.toUtf8());
    QCOMPARE(migration::oldAppBaseDir(),
             home + QStringLiteral("/.var/app/top.spelunk.kwallpaper"));

    // Inside the Flatpak sandbox: $HOME ends in the app's .var dir.
    const QString sandboxHome = home + "/.var/app/top.spelunk.johona";
    qputenv("HOME", sandboxHome.toUtf8());
    QCOMPARE(migration::oldAppBaseDir(),
             home + QStringLiteral("/.var/app/top.spelunk.kwallpaper"));
}

QTEST_GUILESS_MAIN(TestMigration)
#include "test_migration.moc"
