// scheduler.hpp — event-driven one-shot boundary scheduler (spec §8).
//
//  - scheduleNext(): one-shot timer at the computed boundary.
//  - tickSafety(): re-evaluate the next boundary (wake-from-suspend, login,
//    clock jump); reschedules only when the recomputed boundary is earlier.
//  - A periodic safety tick (engine-side, default 60 s) also detects clock
//    jumps (tick gap >> interval) and triggers tickSafety().

#pragma once

#include <QDateTime>
#include <QObject>
#include <QTimer>
#include <functional>
#include <utility>

namespace johona {

class Scheduler : public QObject {
    Q_OBJECT
public:
    /// Recomputes the next (time, label) boundary; invalid time = none.
    using BoundaryProvider = std::function<std::pair<QDateTime, QString>()>;

    explicit Scheduler(QObject* parent = nullptr);

    void setBoundaryProvider(BoundaryProvider provider);

    /// Arm the one-shot timer.  Invalid `when` cancels.  A past `when`
    /// fires asynchronously (queued) to avoid re-entrancy.
    void scheduleNext(const QDateTime& when, const QString& label);
    void cancel();

    /// Re-evaluate via the boundary provider; reschedule when the new
    /// boundary is earlier than the armed one (or none is armed).
    void tickSafety();

    QDateTime scheduledFor() const { return m_scheduledFor; }
    QString scheduledLabel() const { return m_label; }
    bool isArmed() const { return m_scheduledFor.isValid(); }

signals:
    void boundaryReached(const QString& label);

private:
    // Created lazily on the thread that first arms the scheduler: the
    // Scheduler object itself is constructed on the GUI thread (inside
    // Engine's ctor) but armed from the engine thread, and Qt timers may
    // only be started from their own thread.
    QTimer* ensureTimer();
    QTimer* m_timer = nullptr;
    BoundaryProvider m_provider;
    QDateTime m_scheduledFor;
    QString m_label;
};

}  // namespace johona
