// clock.hpp — injectable clock (spec §13): system default; fixed/steppable
// in tests for deterministic time logic.

#pragma once

#include <QDateTime>
#include <chrono>

namespace johona {

class Clock {
public:
    virtual ~Clock() = default;
    /// Current local wall-clock time (system timezone).
    virtual QDateTime now() const = 0;
    /// Monotonic milliseconds (for drift/clock-jump detection).
    virtual qint64 monotonicMs() const = 0;
};

class SystemClock : public Clock {
public:
    QDateTime now() const override { return QDateTime::currentDateTime(); }
    qint64 monotonicMs() const override {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
                   std::chrono::steady_clock::now().time_since_epoch())
            .count();
    }
};

/// Fixed clock for tests: advance() moves both wall and monotonic time.
class FixedClock : public Clock {
public:
    explicit FixedClock(QDateTime initial) : m_now(std::move(initial)) {
        m_mono = 0;
    }
    QDateTime now() const override { return m_now; }
    qint64 monotonicMs() const override { return m_mono; }
    void advance(int seconds) {
        m_now = m_now.addSecs(seconds);
        m_mono += seconds * 1000;
    }
    void set(QDateTime t) {
        m_now = std::move(t);
    }

private:
    QDateTime m_now;
    qint64 m_mono = 0;
};

}  // namespace johona
