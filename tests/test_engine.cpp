// test_engine.cpp — engine orchestration against a mock GNOME backend
// (injected process runner) and a disconnected D-Bus bus, with a fixed
// clock for determinism (spec §13).

#include <QtTest>

#include <QDBusInterface>
#include <QDBusMessage>
#include <QSignalSpy>
#include <QTimeZone>

#include "engine.hpp"
#include "solar.hpp"
#include "themes.hpp"

using namespace johona;

namespace {

const char* kThemeJson = R"({
    "displayName": "Solar",
    "imageFilename": "img_*.jpg",
    "sunriseImageList": [1],
    "dayImageList": [2, 3],
    "sunsetImageList": [4],
    "nightImageList": [5]
})";

/// Mock process runner: `gsettings list-recursively` succeeds (GNOME
/// available), `gsettings set` fails `setFailuresRemaining` times,
/// everything else (xdg-settings) fails.
struct MockRun {
    std::vector<QString> calls;
    int setFailuresRemaining = 0;

    int operator()(const QString& prog, const QStringList& args, QString* out,
                   QString* err, int) {
        calls.push_back(prog + QLatin1Char(' ') + args.join(QLatin1Char(' ')));
        if (prog == "gsettings" && args.first() == "list-recursively")
            return 0;
        if (prog == "gsettings" && args.first() == "set") {
            if (setFailuresRemaining > 0) {
                setFailuresRemaining--;
                if (err)
                    *err = "mock failure";
                return 1;
            }
            return 0;
        }
        return 1;
    }

    int setCalls() const {
        int n = 0;
        for (const QString& c : calls)
            if (c.startsWith("gsettings set "))
                n++;
        return n;
    }
};

/// A lazy peer connection that never actually connects: isConnected() is
/// false, so the Plasma/portal probes short-circuit and auto-detection
/// falls through to the (mocked) GNOME backend.
backends::BusProvider mockBus() {
    return [](QDBusConnection::BusType) {
        return QDBusConnection::connectToPeer(QStringLiteral("nonexistent"),
                                              QStringLiteral("/nonexistent"));
    };
}

}  // namespace

class TestEngine : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void applyNow_appliesAndPersists();
    void applyNow_skipUnchanged();
    void applyTheme_forcedRebuildsShuffle();
    void advanceShuffle_stepsList();
    void noThemes_fails();
    void backendFailure_retriesOnce();
    void backendFailure_defersToSafetyTick();
    void safetyTick_noop();
    void cycleDisabled_skipsSafetyTick();
    void gnomeProbe_rejectedWithoutShell();
    void nextChange_valid();
    void startStop();

private:
    void makeTheme(const QString& name) {
        const QString dir = m_themesDir + '/' + name;
        QDir().mkpath(dir);
        QFile f(dir + "/theme.json");
        f.open(QIODevice::WriteOnly);
        f.write(kThemeJson);
        for (int i = 1; i <= 5; i++) {
            QFile img(dir + QStringLiteral("/img_%1.jpg").arg(i));
            img.open(QIODevice::WriteOnly);
            img.write("x");
        }
    }

    void resetState() {
        QDir(m_themesDir).removeRecursively();
        QFile::remove(m_configPath);
        QFile::remove(m_shufflePath);
    }

    std::unique_ptr<Engine> makeEngine(MockRun* mock, QDateTime fixedNow,
                                       bool shuffleEnabled = true) {
        Engine::Deps deps;
        config::Config cfg;
        cfg.timezone = "America/Phoenix";
        cfg.latitude = 33.4484;
        cfg.longitude = -112.074;
        cfg.dailyShuffleEnabled = shuffleEnabled;
        deps.config = cfg;
        deps.backends = std::make_unique<backends::BackendManager>(
            mockBus(),
            [mock](const QString& prog, const QStringList& args, QString* out,
                   QString* err, int timeout) {
                return (*mock)(prog, args, out, err, timeout);
            });
        deps.scheduler = std::make_unique<Scheduler>();
        deps.bus = mockBus();
        deps.clock =
            std::make_shared<FixedClock>(fixedNow.toTimeZone(QTimeZone("America/Phoenix")));
        deps.retryDelayMs = 20;
        return std::make_unique<Engine>(std::move(deps));
    }

    solar::ThemeImageLists kLists() const {
        solar::ThemeImageLists l;
        l.sunrise = {1};
        l.day = {2, 3};
        l.sunset = {4};
        l.night = {5};
        return l;
    }

    QTemporaryDir m_tmp;
    QString m_themesDir;
    QString m_configPath;
    QString m_shufflePath;
};

void TestEngine::initTestCase() {
    QVERIFY(m_tmp.isValid());
    qputenv("XDG_CONFIG_HOME", (m_tmp.path() + "/cfg").toUtf8());
    qputenv("XDG_DATA_HOME", (m_tmp.path() + "/data").toUtf8());
    qputenv("XDG_CACHE_HOME", (m_tmp.path() + "/cache").toUtf8());
    m_themesDir = m_tmp.path() + "/data/johona/themes";
    m_configPath = m_tmp.path() + "/cfg/johona/config.json";
    m_shufflePath = m_tmp.path() + "/cfg/johona/shuffle-list.json";
}

void TestEngine::applyNow_appliesAndPersists() {
    resetState();
    makeTheme("solar");
    const QDateTime fixedNow(QDate(2013, 3, 5), QTime(12, 0), QTimeZone("America/Phoenix"));
    MockRun mock;
    auto engine = makeEngine(&mock, fixedNow);

    QSignalSpy appliedSpy(engine.get(), &Engine::applied);

    const ApplyOutcome out = engine->applyNow();
    QVERIFY2(out.success, qPrintable(out.message));
    QCOMPARE(out.themeName, QString("solar"));
    QVERIFY(!out.skipped);

    // Cross-check the selection with the solar API directly.
    const QTimeZone tz("America/Phoenix");
    const double nowMs = fixedNow.toTimeZone(tz).toMSecsSinceEpoch();
    const auto seg = solar::segmentsForNow(nowMs, fixedNow.toTimeZone(tz).date(), tz, 33.4484,
                                           -112.074, kLists());
    const auto sel = solar::imageAt(nowMs, seg, kLists());
    QVERIFY(sel.has_value());
    QCOMPARE(out.category, QString(solar::categoryName(sel->category)));
    QCOMPARE(out.imagePath, m_themesDir + "/solar/img_" + QString::number(sel->imageValue) + ".jpg");

    // Persisted config.
    const config::Config saved = config::load(m_configPath);
    QCOMPARE(saved.lastApplied, QString("solar"));
    QCOMPARE(saved.lastAppliedImage, out.imagePath);

    // GNOME backend: picture-uri + picture-uri-dark.
    QCOMPARE(mock.setCalls(), 2);
    QCOMPARE(appliedSpy.count(), 1);
}

void TestEngine::applyNow_skipUnchanged() {
    resetState();
    makeTheme("solar");
    const QDateTime fixedNow(QDate(2013, 3, 5), QTime(12, 0), QTimeZone("America/Phoenix"));
    MockRun mock;
    auto engine = makeEngine(&mock, fixedNow);

    const ApplyOutcome first = engine->applyNow();
    QVERIFY2(first.success, qPrintable(first.message));
    const int setsAfterFirst = mock.setCalls();

    const ApplyOutcome second = engine->applyNow();
    QVERIFY2(second.success, qPrintable(second.message));
    QVERIFY(second.skipped);
    QCOMPARE(mock.setCalls(), setsAfterFirst);  // no re-set
}

void TestEngine::applyTheme_forcedRebuildsShuffle() {
    resetState();
    makeTheme("solar");
    makeTheme("other");
    const QDateTime fixedNow(QDate(2013, 3, 5), QTime(12, 0), QTimeZone("America/Phoenix"));
    MockRun mock;
    auto engine = makeEngine(&mock, fixedNow);

    QVERIFY(engine->applyNow().success);

    const ApplyOutcome out = engine->applyTheme("other");
    QVERIFY2(out.success, qPrintable(out.message));
    QCOMPARE(out.themeName, QString("other"));

    // Shuffle list rebuilt with the forced theme at index 0, no duplicates.
    const shuffle::ShuffleState shuf = shuffle::loadShuffleState(m_shufflePath);
    QVERIFY(shuf.valid());
    QCOMPARE(shuf.currentIndex, 0);
    QCOMPARE(shuf.shuffleList.size(), static_cast<size_t>(2));
    QCOMPARE(QFileInfo(shuf.shuffleList[0]).fileName(), QString("other"));
    QCOMPARE(QFileInfo(shuf.shuffleList[1]).fileName(), QString("solar"));
}

void TestEngine::advanceShuffle_stepsList() {
    resetState();
    makeTheme("solar");
    makeTheme("other");
    const QDateTime fixedNow(QDate(2013, 3, 5), QTime(12, 0), QTimeZone("America/Phoenix"));
    MockRun mock;
    auto engine = makeEngine(&mock, fixedNow);

    QVERIFY(engine->applyNow().success);
    QString firstTheme =
        QFileInfo(shuffle::loadShuffleState(m_shufflePath).currentTheme()).fileName();

    const ApplyOutcome adv = engine->advanceShuffle();
    QVERIFY2(adv.success, qPrintable(adv.message));
    const shuffle::ShuffleState shuf = shuffle::loadShuffleState(m_shufflePath);
    QCOMPARE(shuf.currentIndex, 1);
    QCOMPARE(adv.themeName, QFileInfo(shuf.shuffleList[1]).fileName());
    QVERIFY(adv.themeName != firstTheme);

    // Wrap around back to index 0 (reshuffled).
    const ApplyOutcome adv2 = engine->advanceShuffle();
    QVERIFY2(adv2.success, qPrintable(adv2.message));
    QCOMPARE(shuffle::loadShuffleState(m_shufflePath).currentIndex, 0);
}

void TestEngine::noThemes_fails() {
    resetState();
    const QDateTime fixedNow(QDate(2013, 3, 5), QTime(12, 0), QTimeZone("America/Phoenix"));
    MockRun mock;
    auto engine = makeEngine(&mock, fixedNow);
    const ApplyOutcome out = engine->applyNow();
    QVERIFY(!out.success);
    QVERIFY(out.message.contains("No themes"));
}

void TestEngine::backendFailure_retriesOnce() {
    resetState();
    makeTheme("solar");
    const QDateTime fixedNow(QDate(2013, 3, 5), QTime(12, 0), QTimeZone("America/Phoenix"));
    MockRun mock;
    mock.setFailuresRemaining = 1;  // first `gsettings set` fails, then success
    auto engine = makeEngine(&mock, fixedNow);

    const ApplyOutcome out = engine->applyNow();
    QVERIFY2(out.success, qPrintable(out.message));
    // Attempt 1: 1 set call (fails). Retry: 2 set calls (both succeed).
    QCOMPARE(mock.setCalls(), 3);
}

void TestEngine::backendFailure_defersToSafetyTick() {
    resetState();
    makeTheme("solar");
    const QDateTime fixedNow(QDate(2013, 3, 5), QTime(12, 0), QTimeZone("America/Phoenix"));
    MockRun mock;
    mock.setFailuresRemaining = 1000;  // always fails
    auto engine = makeEngine(&mock, fixedNow);

    const ApplyOutcome out = engine->applyNow();
    QVERIFY(!out.success);
    QVERIFY(out.message.contains("next safety tick"));
    // Attempt: 1 set call + 1 retry = 2.
    QCOMPARE(mock.setCalls(), 2);
    // Nothing persisted after a failure.
    const config::Config saved = config::load(m_configPath);
    QVERIFY(saved.lastApplied.isEmpty());

    // The safety tick retries the failed set.
    engine->runSafetyTick();
    QCOMPARE(mock.setCalls(), 4);
}

void TestEngine::safetyTick_noop() {
    resetState();
    makeTheme("solar");
    const QDateTime fixedNow(QDate(2013, 3, 5), QTime(12, 0), QTimeZone("America/Phoenix"));
    MockRun mock;
    auto engine = makeEngine(&mock, fixedNow);

    QVERIFY(engine->applyNow().success);
    const int sets = mock.setCalls();

    // No pending failure, no overdue boundary, same date → no re-apply
    // (spec §6: "otherwise no-op. No re-apply").
    engine->runSafetyTick();
    QCOMPARE(mock.setCalls(), sets);
}

void TestEngine::cycleDisabled_skipsSafetyTick() {
    resetState();
    makeTheme("solar");
    const QDateTime fixedNow(QDate(2013, 3, 5), QTime(12, 0),
                            QTimeZone("America/Phoenix"));
    MockRun mock;
    mock.setFailuresRemaining = 1000;  // always fails
    auto engine = makeEngine(&mock, fixedNow);

    // 1 s safety interval, cycle task disabled.
    config::Config cfg = engine->config();
    cfg.safetyInterval = 1;
    cfg.cycleEnabled = false;
    engine->setConfig(cfg);

    engine->start();
    // Initial apply fails (mock always fails): attempt + one retry.
    QCOMPARE(mock.setCalls(), 2);

    // Wait past the safety interval: with the cycle task enabled the
    // pending failure would be retried here; disabled → no retry.
    QTest::qWait(1500);
    QCOMPARE(mock.setCalls(), 2);

    engine->stop();
}

void TestEngine::gnomeProbe_rejectedWithoutShell() {
    // Regression (headless Plasma): with a connected session bus but no
    // GNOME shell, the gsettings backend must NOT be available — gsettings
    // "succeeds" there but nothing applies the value, so the engine would
    // claim success while the wallpaper never changes.
    //
    // Requires a real message bus: a QDBusServer peer connection
    // auto-replies to every call and cannot stand in for name resolution.
    // Skipped when no session bus is reachable, or when GNOME shell is
    // actually present (i.e. on a GNOME host).
    const QDBusConnection conn = QDBusConnection::sessionBus();
    QDBusInterface busIface(QStringLiteral("org.freedesktop.DBus"),
                            QStringLiteral("/"),
                            QStringLiteral("org.freedesktop.DBus.Peer"), conn);
    if (busIface.call(QStringLiteral("Ping")).type() != QDBusMessage::ReplyMessage)
        QSKIP("no reachable session bus");
    QDBusInterface shell(QStringLiteral("org.gnome.Shell"),
                         QStringLiteral("/org/gnome/Shell"),
                         QStringLiteral("org.freedesktop.DBus.Peer"), conn);
    if (shell.call(QStringLiteral("Ping")).type() == QDBusMessage::ReplyMessage)
        QSKIP("GNOME shell present");

    MockRun mock;  // gsettings "works" (rc 0) — must not be enough
    backends::GnomeBackend backend(
        [&mock](const QString& prog, const QStringList& args, QString* out,
                QString* err, int timeout) {
            return mock(prog, args, out, err, timeout);
        },
        backends::defaultBus);
    QVERIFY(!backend.isAvailable());
    // Rejected by the bus check, not by gsettings: no process was run.
    QVERIFY(mock.calls.empty());
}

void TestEngine::nextChange_valid() {
    resetState();
    makeTheme("solar");
    const QDateTime fixedNow(QDate(2013, 3, 5), QTime(12, 0), QTimeZone("America/Phoenix"));
    MockRun mock;
    auto engine = makeEngine(&mock, fixedNow);

    const auto [when, label] = engine->nextChange();
    QVERIFY(when.isValid());
    QVERIFY(!label.isEmpty());
    QVERIFY(when > fixedNow.toTimeZone(QTimeZone("America/Phoenix")));
}

void TestEngine::startStop() {
    resetState();
    makeTheme("solar");
    const QDateTime fixedNow(QDate(2013, 3, 5), QTime(12, 0), QTimeZone("America/Phoenix"));
    MockRun mock;
    auto engine = makeEngine(&mock, fixedNow);

    QSignalSpy appliedSpy(engine.get(), &Engine::applied);

    engine->start();
    QVERIFY(engine->isRunning());
    // Initial cycle applied the wallpaper.
    QCOMPARE(appliedSpy.count(), 1);
    QCOMPARE(mock.setCalls(), 2);

    engine->stop();
    QVERIFY(!engine->isRunning());
}

QTEST_GUILESS_MAIN(TestEngine)
#include "test_engine.moc"
