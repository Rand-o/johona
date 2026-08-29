// scheduler.cpp — one-shot boundary scheduler implementation.

#include "scheduler.hpp"

#include <QMetaObject>
#include <QThread>

#include <algorithm>
#include <limits>

namespace johona {

Scheduler::Scheduler(QObject* parent) : QObject(parent) {}

QTimer* Scheduler::ensureTimer() {
    if (!m_timer) {
        m_timer = new QTimer(this);
        m_timer->setSingleShot(true);
        connect(m_timer, &QTimer::timeout, this, [this] {
            emit boundaryReached(m_label);
        });
    }
    return m_timer;
}

void Scheduler::setBoundaryProvider(BoundaryProvider provider) {
    m_provider = std::move(provider);
}

void Scheduler::scheduleNext(const QDateTime& when, const QString& label) {
    m_label = label;
    if (!when.isValid()) {
        if (m_timer)
            m_timer->stop();
        m_scheduledFor = QDateTime();
        return;
    }
    m_scheduledFor = when;
    qint64 ms = QDateTime::currentDateTime().msecsTo(when);
    if (ms < 0)
        ms = 0;
    if (ms == 0) {
        // Fire asynchronously to avoid re-entrancy in the caller.
        QMetaObject::invokeMethod(
            this, [this] { emit boundaryReached(m_label); }, Qt::QueuedConnection);
    } else {
        // Qt 6 QTimer caps at INT_MAX ms (≈ 24.8 days); boundaries are days
        // away, so clamp and rely on the safety tick for anything further.
        ensureTimer()->start(static_cast<int>(
            std::min<qint64>(ms, std::numeric_limits<int>::max())));
    }
}

void Scheduler::cancel() {
    if (m_timer)
        m_timer->stop();
    m_scheduledFor = QDateTime();
    m_label.clear();
}

void Scheduler::tickSafety() {
    if (!m_provider)
        return;
    const auto [next, label] = m_provider();
    if (!next.isValid())
        return;
    if (!m_scheduledFor.isValid() || next < m_scheduledFor)
        scheduleNext(next, label);
}

}  // namespace johona
