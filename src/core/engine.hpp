// engine.hpp — orchestration: theme selection, image selection, backend
// apply, persistence, and scheduling (spec §9.5, §6).
//
// The engine is the single writer for both config.json (theme.last_applied*)
// and shuffle-list.json, and it persists only after the wallpaper set
// succeeds (a failed change retries the same theme on the next attempt).
//
// Thread model (spec §6/§11.4): the engine's methods are synchronous and
// blocking (D-Bus, subprocesses, file I/O).  The GUI moves the engine to a
// dedicated QThread and calls it via queued invocations; results are
// delivered via signals.  Tests run it on the test thread directly.
// State-mutating operations are serialized with an internal mutex so a
// cycle and a manual apply can never overlap.

#pragma once

#include <QDateTime>
#include <QMutex>
#include <QObject>
#include <QThread>
#include <QTimer>

#include "backends.hpp"
#include "clock.hpp"
#include "config.hpp"
#include "dbusrelay.hpp"
#include "scheduler.hpp"
#include "shuffle.hpp"
#include "solar.hpp"

namespace johona {

struct ApplyOutcome {
    bool success = false;
    bool skipped = false;  // skip-if-unchanged (wallpaper already correct)
    QString themeName;
    QString imagePath;
    QString category;
    QString message;
};

class Engine : public QObject {
    Q_OBJECT
public:
    struct Deps {
        config::Config config;
        std::unique_ptr<backends::BackendManager> backends;
        std::unique_ptr<Scheduler> scheduler;
        backends::BusProvider bus = backends::defaultBus;  // for login1
        std::shared_ptr<Clock> clock = std::make_shared<SystemClock>();
        /// Delay before the one-time retry of a failed wallpaper set
        /// (spec §6: 5 s).  Injectable so tests don't sleep for real.
        int retryDelayMs = 5000;
    };

    explicit Engine(Deps deps, QObject* parent = nullptr);

    /// Move the engine (and its Scheduler, which is a plain unique_ptr
    /// member, not a QObject child) to a thread together.  Shadows
    /// QObject::moveToThread (non-virtual in Qt 6); call via Engine*.
    bool moveToThread(QThread* thread);

    /// Start the scheduler: wire the boundary provider, start the safety
    /// tick, subscribe to login1, run the initial cycle (advances the daily
    /// shuffle on a new day), arm the first one-shot.
    void start();
    /// Stop the safety tick and cancel the one-shot.
    void stop();
    bool isRunning() const { return m_running; }

    /// Apply the current theme's image for now (no daily advance).
    ApplyOutcome applyNow();
    /// Apply a specific theme (by name or path); with daily shuffle enabled
    /// this also rebuilds the shuffle list with the theme at index 0
    /// (kWallpaper parity), persisted after success.
    ApplyOutcome applyTheme(const QString& themeName);
    /// Manual "Next wallpaper" (spec §9.5): advance the shuffle list by one
    /// and apply that theme's image for the current moment.
    ApplyOutcome advanceShuffle();

    /// One scheduler cycle: apply the current image for now.  Public for
    /// tests and for the scheduler tab's manual "run now".
    ApplyOutcome runCycle();

    config::Config config() const;
    /// Update config, backend override and safety interval; reschedules.
    /// The caller is responsible for persisting the config.
    void setConfig(const config::Config& config);
    QString lastError() const { return m_lastError; }

    /// id → available, for the Settings UI (spec §11.2).
    QHash<QString, bool> probeBackends() const;
    /// Display name of the active backend ("none" when unavailable).
    QString activeBackendName() const;

    /// The next wallpaper-change instant + label (for the DayBar / status
    /// block); invalid when unknown.
    std::pair<QDateTime, QString> nextChange() const;

    /// Run one safety tick (spec §6).  Public for tests.
    void runSafetyTick();
    /// Run the login1 resume handler (spec §6).  Public for tests.
    void handleResume();

    config::Paths paths() const { return m_paths; }

signals:
    void applied(const QString& themeName, const QString& imagePath, const QString& category);
    void statusChanged(const QString& message);
    void errorOccurred(const QString& message);
    /// Scheduler start/stop changed (GUI: tray + scheduler tab).
    void runningChanged(bool running);
    /// Every scheduler/engine run emits a log line (GUI Scheduler tab).
    void logMessage(const QString& message);

private:
    ApplyOutcome applyCurrent(bool dailyAdvance, const QString& forcedTheme = {});
    QString resolveCurrentThemePath() const;
    std::pair<QDateTime, QString> nextBoundary() const;
    void reschedule();
    void onSafetyTick();
    void onSleepStateChanged(bool preparing);

    QTimeZone timezone() const;
    QDateTime nowLocal() const;
    double nowMs() const;
    qint64 monotonicMs() const;
    QString todayString() const;
    solar::ThemeImageLists listsFor(const QString& themePath) const;

    config::Config m_config;
    config::Paths m_paths;
    std::unique_ptr<backends::BackendManager> m_backends;
    std::unique_ptr<Scheduler> m_scheduler;
    std::unique_ptr<dbus::BoolRelay> m_sleepRelay;  // login1 PrepareForSleep
    backends::BusProvider m_bus;
    std::shared_ptr<Clock> m_clock;
    int m_retryDelayMs = 5000;
    // Created lazily on the thread that starts the scheduler (the Engine is
    // constructed on the GUI thread but runs on the engine thread; Qt timers
    // may only be started from their own thread).
    QTimer* ensureSafetyTimer();
    QTimer* m_safetyTimer = nullptr;
    QMutex m_opMutex;  // serializes state-mutating operations
    QMutex m_configMutex;  // guards m_config (fast GUI reads)

    bool m_running = false;
    QDateTime m_lastWall;
    qint64 m_lastMono = 0;
    QDate m_lastDate;
    QString m_lastSystemTz;
    bool m_pendingFailure = false;  // failed set deferred to the next tick
    QString m_lastError;
};

}  // namespace johona
