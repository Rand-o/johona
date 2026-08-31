// engine.cpp — orchestration implementation.

#include "engine.hpp"

#include <QDBusConnection>
#include <QDir>
#include <QFileInfo>
#include <QMetaObject>
#include <QTimeZone>
#include <QUrl>

#include "solar.hpp"
#include "themes.hpp"

namespace johona {

namespace {

bool sameShuffleState(const shuffle::ShuffleState& a, const shuffle::ShuffleState& b) {
    return a.shuffleList == b.shuffleList && a.currentIndex == b.currentIndex &&
           a.lastUsedDate == b.lastUsedDate;
}

/// True when `current` (as reported by IWallpaperBackend::currentWallpaper)
/// points at the same file as `desiredPath`.  Backends report file:// URLs
/// (Plasma, gsettings, xdg-settings); bare paths are handled too.
bool sameWallpaperFile(const QString& current, const QString& desiredPath) {
    const QString cur = current.trimmed();
    if (cur.isEmpty())
        return false;
    const QUrl u(cur);
    const QString curPath = u.isLocalFile() ? u.toLocalFile() : cur;
    return QFileInfo(curPath).absoluteFilePath() ==
           QFileInfo(desiredPath).absoluteFilePath();
}

}  // namespace

Engine::Engine(Deps deps, QObject* parent)
    : QObject(parent),
      m_config(std::move(deps.config)),
      m_paths(config::paths()),
      m_backends(std::move(deps.backends) ? std::move(deps.backends)
                                          : std::make_unique<backends::BackendManager>()),
      m_scheduler(std::move(deps.scheduler) ? std::move(deps.scheduler)
                                            : std::make_unique<Scheduler>()),
      m_bus(std::move(deps.bus)),
      m_clock(std::move(deps.clock)),
      m_retryDelayMs(std::max(0, deps.retryDelayMs)) {
    if (!m_clock)
        m_clock = std::make_shared<SystemClock>();
    if (!m_bus)
        m_bus = backends::defaultBus;
    m_backends->setOverride(m_config.backendOverride);
}

bool Engine::moveToThread(QThread* thread) {
    // The Scheduler is a unique_ptr member (no QObject parent), so the base
    // move would leave it on the constructing thread while its methods run
    // on the new one — move it explicitly alongside the engine.
    if (m_scheduler)
        m_scheduler->moveToThread(thread);
    return QObject::moveToThread(thread);
}

// ---------------------------------------------------------------------------
// time helpers (injectable clock)
// ---------------------------------------------------------------------------

QTimeZone Engine::timezone() const {
    QTimeZone tz(m_config.timezone.toUtf8());
    return tz.isValid() ? tz : QTimeZone();
}

QDateTime Engine::nowLocal() const {
    return m_clock->now().toTimeZone(timezone());
}

double Engine::nowMs() const {
    // Via the injectable clock so tests are deterministic (spec §13);
    // SystemClock gives the real current time in production.
    return static_cast<double>(m_clock->now().toMSecsSinceEpoch());
}

qint64 Engine::monotonicMs() const { return m_clock->monotonicMs(); }

QString Engine::todayString() const {
    return nowLocal().date().toString(QStringLiteral("yyyy-MM-dd"));
}

solar::ThemeImageLists Engine::listsFor(const QString& themePath) const {
    solar::ThemeImageLists lists;
    if (auto data = themes::loadThemeData(themePath)) {
        lists.sunrise = data->sunriseImageList;
        lists.day = data->dayImageList;
        lists.sunset = data->sunsetImageList;
        lists.night = data->nightImageList;
    }
    return lists;
}

// ---------------------------------------------------------------------------
// theme resolution
// ---------------------------------------------------------------------------

QString Engine::resolveCurrentThemePath() const {
    const auto themes = themes::discoverThemes(m_paths.themesDir);
    if (themes.empty())
        return {};

    if (m_config.dailyShuffleEnabled) {
        const auto shuf = shuffle::loadShuffleState(m_paths.shuffleState);
        if (shuf.valid() && QFileInfo::exists(shuf.currentTheme()))
            return shuf.currentTheme();
        return themes.front().path;
    }
    if (!m_config.lastApplied.isEmpty()) {
        const QString p = m_paths.themesDir + '/' + m_config.lastApplied;
        if (QFileInfo::exists(p))
            return p;
    }
    return themes.front().path;
}

// ---------------------------------------------------------------------------
// apply
// ---------------------------------------------------------------------------

ApplyOutcome Engine::applyCurrent(bool dailyAdvance, const QString& forcedTheme) {
    ApplyOutcome out;

    const bool opMutexHeld = m_opMutex.tryLock();
    if (!opMutexHeld) {
        out.message = QStringLiteral("An apply is already in progress");
        return out;
    }
    struct Unlocker {
        Engine* e;
        bool held;
        ~Unlocker() {
            if (held)
                e->m_opMutex.unlock();
        }
    } unlocker{this, opMutexHeld};

    const auto themes = themes::discoverThemes(m_paths.themesDir);
    if (themes.empty()) {
        out.message = QStringLiteral("No themes found in %1").arg(m_paths.themesDir);
        m_lastError = out.message;
        emit logMessage(out.message);
        return out;
    }
    std::vector<QString> paths;
    paths.reserve(themes.size());
    for (const auto& t : themes)
        paths.push_back(t.path);

    const bool shuffleEnabled = m_config.dailyShuffleEnabled;
    auto shuf = shuffle::loadShuffleState(m_paths.shuffleState);
    bool shuffleDirty = false;

    // 1. Pick the theme.
    QString themePath;
    if (!forcedTheme.isEmpty()) {
        themePath = QFileInfo(forcedTheme).isAbsolute()
                        ? QFileInfo(forcedTheme).absoluteFilePath()
                        : m_paths.themesDir + '/' + forcedTheme;
        if (!QFileInfo::exists(themePath)) {
            out.message = QStringLiteral("Theme not found: %1").arg(forcedTheme);
            m_lastError = out.message;
            emit logMessage(out.message);
            return out;
        }
    } else if (shuffleEnabled) {
        const QString today = todayString();
        // Advance only a valid, fresh list; an empty/stale state is rebuilt
        // in step 4 with the applied theme at index 0 (advancing a stale
        // list would pick a random index-0 theme that was not applied).
        if (dailyAdvance && shuf.valid() && shuffle::sameSet(shuf.shuffleList, paths) &&
            shuffle::dayPassed(shuf, today)) {
            shuf = shuffle::advanceShuffle(shuf, paths, today);
            shuffleDirty = true;
        }
        themePath = shuf.valid() ? shuf.currentTheme() : themes.front().path;
        if (!QFileInfo::exists(themePath))
            themePath = themes.front().path;  // shuffle entry deleted
    } else {
        if (!m_config.lastApplied.isEmpty()) {
            const QString p = m_paths.themesDir + '/' + m_config.lastApplied;
            themePath = QFileInfo::exists(p) ? p : themes.front().path;
        } else {
            themePath = themes.front().path;
        }
    }
    const QString themeName = QFileInfo(themePath).fileName();

    // 2. Select the image for now.
    auto data = themes::loadThemeData(themePath);
    if (!data) {
        out.message = QStringLiteral("Could not load theme data for %1").arg(themeName);
        m_lastError = out.message;
        emit logMessage(out.message);
        return out;
    }
    const solar::ThemeImageLists lists = listsFor(themePath);
    const double now = nowMs();
    const auto seg =
        solar::segmentsForNow(now, nowLocal().date(), timezone(), m_config.latitude,
                              m_config.longitude, lists);
    const auto sel = solar::imageAt(now, seg, lists);
    if (!sel) {
        out.message = QStringLiteral("No image selected for the current time (theme %1)")
                          .arg(themeName);
        m_lastError = out.message;
        emit logMessage(out.message);
        return out;
    }
    const QString imagePath = themes::imageFileFor(themePath, *data, sel->imageValue);
    if (imagePath.isEmpty()) {
        out.message = QStringLiteral("Image file for value %1 not found in theme %2")
                          .arg(sel->imageValue)
                          .arg(themeName);
        m_lastError = out.message;
        emit logMessage(out.message);
        return out;
    }
    out.themeName = themeName;
    out.themeDisplayName = themes::prettyThemeName(data->displayName, themeName);
    out.imagePath = QFileInfo(imagePath).absoluteFilePath();
    out.category = solar::categoryName(sel->category);

    // 3. Set the wallpaper (skip-if-unchanged), with one 5 s retry
    //    (spec §6: a failed set is retried exactly once after 5 s; if it
    //    fails again it defers to the next safety tick).
    bool unchanged = !m_config.lastAppliedImage.isEmpty() &&
                     QFileInfo(m_config.lastAppliedImage).absoluteFilePath() ==
                         QFileInfo(out.imagePath).absoluteFilePath();
    if (unchanged) {
        // The config says this image is current, but the desktop can change
        // underneath us (another app, a manual change, a set that was lost).
        // Verify against the live wallpaper when the backend can report it;
        // an empty result means "unknown" (e.g. the portal backend) and
        // keeps the config-based skip.
        if (auto* backend = m_backends->backend()) {
            const QString cur = backend->currentWallpaper();
            if (!cur.isEmpty() && !sameWallpaperFile(cur, out.imagePath))
                unchanged = false;
        }
    }
    if (unchanged) {
        out.skipped = true;
    } else {
        auto backend = m_backends->backend();
        if (!backend) {
            out.message = QStringLiteral("No wallpaper backend is available");
            m_lastError = out.message;
            emit logMessage(out.message);
            return out;
        }
        auto r = backend->setWallpaper(out.imagePath);
        if (!r.success) {
            // Retry exactly once after the configured delay (spec §6: 5 s).
            QThread::msleep(m_retryDelayMs);
            r = backend->setWallpaper(out.imagePath);
            if (!r.success) {
                m_pendingFailure = true;  // defer to the next safety tick
                out.message =
                    QStringLiteral("Wallpaper set failed (%1): %2 — will retry on the "
                                   "next safety tick")
                        .arg(backend->id(), r.message);
                m_lastError = out.message;
                emit logMessage(out.message);
                return out;
            }
            emit logMessage(QStringLiteral("Retry succeeded (%1)").arg(backend->id()));
        }

        // Persist config only now that the wallpaper is up.
        m_config.lastApplied = themeName;
        m_config.lastAppliedImage = out.imagePath;
        config::save(m_config, m_paths.config);
    }

    // 4. Persist shuffle state (persist-after-success).
    if (shuffleEnabled) {
        // (Re)build when this was a forced apply (kWallpaper parity: applied
        // theme at index 0, the rest shuffled) or when the stored state is
        // empty/stale — the latter keeps the state file consistent with the
        // applied theme on first run and after the theme set changes.
        const bool stale =
            !shuf.valid() || !shuffle::sameSet(shuf.shuffleList, paths);
        if (!forcedTheme.isEmpty() || stale) {
            const QString absTheme = QFileInfo(themePath).absoluteFilePath();
            std::vector<QString> others;
            for (const QString& p : paths)
                if (p != absTheme)
                    others.push_back(p);
            shuffle::fisherYates(others);
            shuf.shuffleList.clear();
            shuf.shuffleList.push_back(absTheme);
            for (const QString& p : others)
                shuf.shuffleList.push_back(p);
            shuf.currentIndex = 0;
            shuf.lastUsedDate = todayString();
            shuffleDirty = true;
        }
        if (shuffleDirty &&
            !sameShuffleState(shuf, shuffle::loadShuffleState(m_paths.shuffleState)))
            shuffle::saveShuffleState(shuf, m_paths.shuffleState);
    }

    out.success = true;
    out.message = out.skipped
                      ? QStringLiteral("Wallpaper already current (%1)")
                            .arg(QFileInfo(out.imagePath).fileName())
                      : QStringLiteral("Applied %1 (%2, %3)")
                            .arg(themeName, QFileInfo(out.imagePath).fileName(),
                                 out.category);
    m_pendingFailure = false;
    emit applied(themeName, out.themeDisplayName, out.imagePath, out.category);
    emit statusChanged(out.message);
    emit logMessage(out.message);
    return out;
}

ApplyOutcome Engine::applyNow() { return applyCurrent(false); }

ApplyOutcome Engine::applyTheme(const QString& themeName) {
    return applyCurrent(false, themeName);
}

ApplyOutcome Engine::runCycle() { return applyCurrent(false); }

ApplyOutcome Engine::advanceShuffle() {
    ApplyOutcome out;
    if (!m_config.dailyShuffleEnabled) {
        out.message = QStringLiteral("Daily shuffle is disabled");
        m_lastError = out.message;
        emit logMessage(out.message);
        return out;
    }
    const auto themes = themes::discoverThemes(m_paths.themesDir);
    if (themes.empty()) {
        out.message = QStringLiteral("No themes found");
        m_lastError = out.message;
        emit logMessage(out.message);
        return out;
    }
    std::vector<QString> paths;
    for (const auto& t : themes)
        paths.push_back(t.path);

    auto shuf = shuffle::loadShuffleState(m_paths.shuffleState);
    shuf = shuffle::advanceShuffle(shuf, paths, todayString());
    const QString themePath = shuf.currentTheme();

    // Apply the forced theme, then persist the advanced shuffle state
    // (applyCurrent would otherwise rebuild the list with the theme at
    // index 0 — the manual advance wants the advanced list).
    out = applyCurrent(false, themePath);
    if (out.success)
        shuffle::saveShuffleState(shuf, m_paths.shuffleState);
    return out;
}

// ---------------------------------------------------------------------------
// scheduling
// ---------------------------------------------------------------------------

std::pair<QDateTime, QString> Engine::nextBoundary() const {
    const auto themePath = resolveCurrentThemePath();
    if (themePath.isEmpty())
        return {{}, {}};
    const solar::ThemeImageLists lists = listsFor(themePath);
    const double now = nowMs();
    const auto seg =
        solar::segmentsForNow(now, nowLocal().date(), timezone(), m_config.latitude,
                              m_config.longitude, lists);
    // Pass the image *value* (theme.json number), not the list position:
    // nextChangeTime() locates the value in the category lists.  Passing
    // the position instead makes it match a different image in another
    // list (e.g. position 1 vs. value 1 in the night list), which can
    // re-arm a boundary hours away and skip the remaining changes of the
    // day.
    int currentImage = -1;
    if (auto sel = solar::imageAt(now, seg, lists))
        currentImage = sel->imageValue;
    auto next = solar::nextChangeTime(
        now, seg, lists,
        [this](const QDate& d) {
            return solar::segmentsForDay(d, timezone(), m_config.latitude,
                                         m_config.longitude);
        },
        currentImage);
    if (!next)
        return {{}, {}};

    // Label: the category of the window starting at the boundary (or the
    // window containing it, for an internal image change).
    const auto windows = solar::effectiveWindows(seg, lists);
    QString label = QStringLiteral("next change");
    for (const auto& w : windows)
        if (qAbs(w.start - *next) < 1000.0) {
            label = solar::categoryName(w.category);
            break;
        }
    if (label == QStringLiteral("next change"))
        for (const auto& w : windows)
            if (*next >= w.start && *next < w.end) {
                label = QStringLiteral("%1 (next image)").arg(solar::categoryName(w.category));
                break;
            }
    return {QDateTime::fromMSecsSinceEpoch(static_cast<qint64>(*next), timezone()), label};
}

std::pair<QDateTime, QString> Engine::nextChange() const { return nextBoundary(); }

void Engine::runSafetyTick() { onSafetyTick(); }

void Engine::reschedule() {
    const auto [when, label] = nextBoundary();
    m_scheduler->scheduleNext(when, label);
}

void Engine::onSafetyTick() {
    const auto now = nowLocal();
    const qint64 mono = monotonicMs();

    // Monotonic-vs-wall drift beyond 2 s → clock jump (spec §6).
    bool clockJump = false;
    if (m_lastWall.isValid()) {
        const qint64 wallDelta = m_lastWall.msecsTo(now);
        const qint64 monoDelta = mono - m_lastMono;
        if (qAbs(wallDelta - monoDelta) > 2000)
            clockJump = true;
    }
    m_lastWall = now;
    m_lastMono = mono;

    if (clockJump) {
        emit statusChanged(QStringLiteral("Clock jump detected; recomputing schedule"));
        emit logMessage(QStringLiteral("Clock jump detected; recomputing schedule"));
        const auto out = applyCurrent(false);
        if (out.success)
            reschedule();
        return;
    }

    // Missed one-shot / overdue boundary (spec §6).
    if (m_scheduler->isArmed() && now >= m_scheduler->scheduledFor()) {
        emit logMessage(QStringLiteral("Safety tick: boundary overdue, running cycle"));
        const auto out = applyCurrent(false);
        if (out.success)
            reschedule();
        return;
    }

    // Local date changed → daily shuffle advance (missed midnight).
    if (m_lastDate.isValid() && now.date() != m_lastDate) {
        m_lastDate = now.date();
        if (m_config.dailyShuffleEnabled) {
            emit logMessage(QStringLiteral("Safety tick: new day, advancing shuffle"));
            const auto out = applyCurrent(true);
            if (out.success)
                reschedule();
        } else {
            reschedule();
        }
        return;
    }
    m_lastDate = now.date();

    // System timezone changed (opt-in) → re-detect location + reschedule.
    if (m_config.onTimezoneChange) {
        const QString sysTz = QTimeZone::systemTimeZone().id().constData();
        if (!m_lastSystemTz.isEmpty() && sysTz != m_lastSystemTz) {
            emit statusChanged(
                QStringLiteral("System timezone changed to %1; updating location")
                    .arg(sysTz));
            emit logMessage(QStringLiteral("System timezone changed to %1")
                                .arg(sysTz));
            m_lastSystemTz = sysTz;
            m_config.timezone = sysTz;
            config::save(m_config, m_paths.config);
            reschedule();
            return;
        }
        m_lastSystemTz = sysTz;
    }

    // Pending failure from a retried set (spec §6) → run cycle.
    if (m_pendingFailure) {
        emit logMessage(QStringLiteral("Safety tick: retrying failed wallpaper set"));
        const auto out = applyCurrent(false);
        if (out.success)
            reschedule();
        return;
    }

    // Live wallpaper verification (drift detection): the desktop can change
    // underneath us — a manual change, another app, a set that was lost.
    // When the backend can report the wallpaper actually showing and it
    // differs from the image we last applied, re-apply.  An empty report
    // means "unknown" (e.g. the portal backend) → no-op, as before.
    if (!m_config.lastAppliedImage.isEmpty()) {
        if (auto* backend = m_backends->backend()) {
            const QString cur = backend->currentWallpaper();
            if (!cur.isEmpty() && !sameWallpaperFile(cur, m_config.lastAppliedImage)) {
                emit logMessage(QStringLiteral(
                    "Safety tick: wallpaper out of sync, re-applying %1")
                                    .arg(QFileInfo(m_config.lastAppliedImage).fileName()));
                const auto out = applyCurrent(false);
                if (out.success)
                    reschedule();
                return;
            }
        }
    }

    // Otherwise: no-op.  No re-apply (unlike the old per-minute cycle).
}

void Engine::onSleepStateChanged(bool preparing) {
    emit statusChanged(preparing
                           ? QStringLiteral("System preparing to sleep")
                           : QStringLiteral("System resumed; re-evaluating schedule"));
    emit logMessage(preparing ? QStringLiteral("System preparing to sleep")
                              : QStringLiteral("System resumed; re-evaluating schedule"));
    if (!preparing)
        handleResume();
}

void Engine::handleResume() {
    // Re-check immediately if overdue (spec §6).
    const auto now = nowLocal();
    if (m_scheduler->isArmed() && now >= m_scheduler->scheduledFor()) {
        const auto out = applyCurrent(false);
        if (out.success)
            reschedule();
        return;
    }
    if (m_config.dailyShuffleEnabled) {
        const auto shuf = shuffle::loadShuffleState(m_paths.shuffleState);
        if (shuffle::dayPassed(shuf, todayString())) {
            const auto out = applyCurrent(true);
            if (out.success)
                reschedule();
            return;
        }
    }
    m_scheduler->tickSafety();
}

// ---------------------------------------------------------------------------
// lifecycle / config
// ---------------------------------------------------------------------------

QTimer* Engine::ensureSafetyTimer() {
    if (!m_safetyTimer) {
        m_safetyTimer = new QTimer(this);
        connect(m_safetyTimer, &QTimer::timeout, this, [this] { onSafetyTick(); });
    }
    return m_safetyTimer;
}

void Engine::start() {
    if (m_running)
        return;
    m_running = true;
    emit runningChanged(true);

    m_scheduler->setBoundaryProvider([this] { return nextBoundary(); });
    connect(m_scheduler.get(), &Scheduler::boundaryReached, this, [this](const QString& label) {
        emit logMessage(QStringLiteral("Boundary reached: %1").arg(label));
        const auto out = applyCurrent(false);
        if (out.success)
            reschedule();
        else
            emit statusChanged(out.message);
    });

    // The periodic cycle task is optional (kWallpaper "Enable cycle task");
    // the event-driven one-shots below run regardless.
    auto* safety = ensureSafetyTimer();
    safety->setInterval(std::max(5, m_config.safetyInterval) * 1000);
    if (m_config.cycleEnabled)
        safety->start();

    // login1 PrepareForSleep (session bus) — re-evaluate on resume.
    // Non-const: QDBusConnection::connect is non-const in Qt 6.
    QDBusConnection conn = m_bus(QDBusConnection::SessionBus);
    if (conn.isConnected()) {
        m_sleepRelay = std::make_unique<dbus::BoolRelay>(this);
        m_sleepRelay->setHandler(
            [this](bool preparing) { onSleepStateChanged(preparing); });
        conn.connect(QStringLiteral("org.freedesktop.login1"),
                     QStringLiteral("/org/freedesktop/login1"),
                     QStringLiteral("org.freedesktop.login1.Manager"),
                     QStringLiteral("PrepareForSleep"), m_sleepRelay.get(),
                     SLOT(onFired(bool)));
    }

    m_lastWall = nowLocal();
    m_lastMono = monotonicMs();
    m_lastDate = nowLocal().date();
    m_lastSystemTz = QTimeZone::systemTimeZone().id();

    // Initial cycle: sync the wallpaper to the current moment (advances the
    // daily shuffle on a new day), then arm the first one-shot.
    const auto out = applyCurrent(m_config.dailyShuffleEnabled);
    if (out.success) {
        reschedule();
    } else {
        emit errorOccurred(out.message);
    }
}

void Engine::stop() {
    if (!m_running)
        return;
    m_running = false;
    if (m_safetyTimer)
        m_safetyTimer->stop();
    m_scheduler->cancel();
    emit runningChanged(false);
}

config::Config Engine::config() const {
    // Fast path: a dedicated mutex so the GUI never blocks on an in-flight
    // apply (m_opMutex is held for the whole D-Bus/subprocess round trip).
    QMutexLocker locker(const_cast<QMutex*>(&m_configMutex));
    return m_config;
}

void Engine::setConfig(const config::Config& config) {
    {
        QMutexLocker op(&m_opMutex);
        QMutexLocker cfg(&m_configMutex);
        m_config = config;
    }
    m_backends->setOverride(m_config.backendOverride);
    if (m_safetyTimer) {
        m_safetyTimer->setInterval(std::max(5, m_config.safetyInterval) * 1000);
        if (m_running) {
            if (m_config.cycleEnabled)
                m_safetyTimer->start();
            else
                m_safetyTimer->stop();
        }
    }
    if (m_running)
        reschedule();
}

QHash<QString, bool> Engine::probeBackends() const {
    return m_backends->probeAll();
}

QString Engine::activeBackendName() const {
    if (auto* b = m_backends->backend())
        return b->displayName();
    return QStringLiteral("none");
}

}  // namespace johona
