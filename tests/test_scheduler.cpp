// test_scheduler.cpp — one-shot boundary scheduler: arming, past-time
// async fire, cancel, and tickSafety reschedule semantics.

#include <QtTest>

#include <QSignalSpy>

#include "scheduler.hpp"

using namespace johona;

class TestScheduler : public QObject {
    Q_OBJECT

private slots:
    void schedule_future_fires();
    void schedule_invalid_disarms();
    void schedule_past_firesQueued();
    void cancel_stops();
    void tickSafety_earlierReschedules();
    void tickSafety_laterKeepsArmed();
    void tickSafety_armsWhenNone();
    void tickSafety_invalidKeeps();

private:
    static QDateTime inMs(qint64 ms) {
        return QDateTime::currentDateTime().addMSecs(ms);
    }
};

void TestScheduler::schedule_future_fires() {
    Scheduler s;
    QSignalSpy spy(&s, &Scheduler::boundaryReached);
    const QDateTime when = inMs(300);
    s.scheduleNext(when, QStringLiteral("dusk"));

    QVERIFY(s.isArmed());
    QCOMPARE(s.scheduledFor(), when);
    QCOMPARE(s.scheduledLabel(), QString("dusk"));

    QVERIFY(spy.wait(3000));
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.first().first().toString(), QString("dusk"));
}

void TestScheduler::schedule_invalid_disarms() {
    Scheduler s;
    s.scheduleNext(inMs(5000), QStringLiteral("x"));
    QVERIFY(s.isArmed());
    s.scheduleNext(QDateTime(), QString());
    QVERIFY(!s.isArmed());
    QVERIFY(s.scheduledLabel().isEmpty());
}

void TestScheduler::schedule_past_firesQueued() {
    Scheduler s;
    QSignalSpy spy(&s, &Scheduler::boundaryReached);
    s.scheduleNext(QDateTime::currentDateTime().addSecs(-10), QStringLiteral("late"));
    // Fires asynchronously (queued) to avoid re-entrancy.
    QVERIFY(spy.wait(1000));
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.first().first().toString(), QString("late"));
}

void TestScheduler::cancel_stops() {
    Scheduler s;
    QSignalSpy spy(&s, &Scheduler::boundaryReached);
    s.scheduleNext(inMs(2000), QStringLiteral("x"));
    s.cancel();
    QVERIFY(!s.isArmed());
    QTest::qWait(2500);
    QCOMPARE(spy.count(), 0);
}

void TestScheduler::tickSafety_earlierReschedules() {
    Scheduler s;
    const QDateTime later = inMs(10000);
    const QDateTime earlier = inMs(2000);
    s.scheduleNext(later, QStringLiteral("later"));
    s.setBoundaryProvider([earlier] {
        return std::pair{earlier, QStringLiteral("earlier")};
    });
    s.tickSafety();
    QCOMPARE(s.scheduledFor(), earlier);
    QCOMPARE(s.scheduledLabel(), QString("earlier"));
}

void TestScheduler::tickSafety_laterKeepsArmed() {
    Scheduler s;
    const QDateTime earlier = inMs(2000);
    const QDateTime later = inMs(10000);
    s.scheduleNext(earlier, QStringLiteral("earlier"));
    s.setBoundaryProvider([later] {
        return std::pair{later, QStringLiteral("later")};
    });
    s.tickSafety();
    // The later boundary does NOT displace the earlier armed one.
    QCOMPARE(s.scheduledFor(), earlier);
    QCOMPARE(s.scheduledLabel(), QString("earlier"));
}

void TestScheduler::tickSafety_armsWhenNone() {
    Scheduler s;
    const QDateTime when = inMs(3000);
    s.setBoundaryProvider([when] {
        return std::pair{when, QStringLiteral("boundary")};
    });
    QVERIFY(!s.isArmed());
    s.tickSafety();
    QVERIFY(s.isArmed());
    QCOMPARE(s.scheduledFor(), when);
}

void TestScheduler::tickSafety_invalidKeeps() {
    Scheduler s;
    const QDateTime when = inMs(3000);
    s.scheduleNext(when, QStringLiteral("keep"));
    s.setBoundaryProvider([] { return std::pair{QDateTime(), QString()}; });
    s.tickSafety();
    QCOMPARE(s.scheduledFor(), when);
}

QTEST_GUILESS_MAIN(TestScheduler)
#include "test_scheduler.moc"
