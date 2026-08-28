// test_suncalc.cpp — ported 1:1 from mourner/suncalc v1.9.0 test.js
// (the correctness anchor for the C++ port, spec §13).
//
// Reference: https://github.com/mourner/suncalc/blob/v1.9.0/test.js
//
// The JS tests compare `Date.toUTCString()` (second resolution), so the
// C++ assertions use a 1 s tolerance on epoch milliseconds; the position
// test uses the original 1e-15 relative margin.

#include <QtTest>

#include <cmath>

#include "suncalc.hpp"

using namespace johona::suncalc;

class TestSuncalc : public QObject {
    Q_OBJECT

private:
    static bool near(double a, double b, double margin = 1e-15) {
        return std::abs(a - b) < margin;
    }
    static QDateTime epoch(double ms) {
        return QDateTime::fromMSecsSinceEpoch(static_cast<qint64>(ms),
                                              QTimeZone::utc());
    }
    /// toUTCString() comparison (second resolution, like the JS tests).
    static bool sameSecond(double ms, const QString& isoUtc) {
        const QDateTime expected =
            QDateTime::fromString(isoUtc, Qt::ISODateWithMs).toTimeZone(QTimeZone::utc());
        return qAbs(epoch(ms).toMSecsSinceEpoch() - expected.toMSecsSinceEpoch()) < 1000;
    }

private slots:
    void testGetPosition() {
        const double date = QDateTime(QDate(2013, 3, 5), QTime(0, 0, 0),
                                      QTimeZone::utc())
                                .toMSecsSinceEpoch();
        const double lat = 50.5, lng = 30.5;

        const SunPosition pos = getPosition(date, lat, lng);
        QVERIFY2(near(pos.azimuth, -2.5003175907168385), "azimuth");
        QVERIFY2(near(pos.altitude, -0.7000406838781611), "altitude");
    }

    void testGetTimes() {
        const double date = QDateTime(QDate(2013, 3, 5), QTime(0, 0, 0),
                                      QTimeZone::utc())
                                .toMSecsSinceEpoch();
        const double lat = 50.5, lng = 30.5;

        const struct {
            const char* key;
            const char* iso;
        } testTimes[] = {
            {"solarNoon", "2013-03-05T10:10:57Z"},
            {"nadir", "2013-03-04T22:10:57Z"},
            {"sunrise", "2013-03-05T04:34:56Z"},
            {"sunset", "2013-03-05T15:46:57Z"},
            {"sunriseEnd", "2013-03-05T04:38:19Z"},
            {"sunsetStart", "2013-03-05T15:43:34Z"},
            {"dawn", "2013-03-05T04:02:17Z"},
            {"dusk", "2013-03-05T16:19:36Z"},
            {"nauticalDawn", "2013-03-05T03:24:31Z"},
            {"nauticalDusk", "2013-03-05T16:57:22Z"},
            {"nightEnd", "2013-03-05T02:46:17Z"},
            {"night", "2013-03-05T17:35:36Z"},
            {"goldenHourEnd", "2013-03-05T05:19:01Z"},
            {"goldenHour", "2013-03-05T15:02:52Z"},
        };

        const SunTimes times = getTimes(date, lat, lng);
        const auto field = [](const SunTimes& t, const char* key) -> double {
            if (qstrcmp(key, "solarNoon") == 0) return t.solarNoon;
            if (qstrcmp(key, "nadir") == 0) return t.nadir;
            if (qstrcmp(key, "sunrise") == 0) return t.sunrise;
            if (qstrcmp(key, "sunset") == 0) return t.sunset;
            if (qstrcmp(key, "sunriseEnd") == 0) return t.sunriseEnd;
            if (qstrcmp(key, "sunsetStart") == 0) return t.sunsetStart;
            if (qstrcmp(key, "dawn") == 0) return t.dawn;
            if (qstrcmp(key, "dusk") == 0) return t.dusk;
            if (qstrcmp(key, "nauticalDawn") == 0) return t.nauticalDawn;
            if (qstrcmp(key, "nauticalDusk") == 0) return t.nauticalDusk;
            if (qstrcmp(key, "nightEnd") == 0) return t.nightEnd;
            if (qstrcmp(key, "night") == 0) return t.night;
            if (qstrcmp(key, "goldenHourEnd") == 0) return t.goldenHourEnd;
            if (qstrcmp(key, "goldenHour") == 0) return t.goldenHour;
            return std::nan("");
        };

        for (const auto& tt : testTimes) {
            const double v = field(times, tt.key);
            QVERIFY2(!isMissing(v), qPrintable(QString("missing: %1").arg(tt.key)));
            QVERIFY2(sameSecond(v, tt.iso),
                     qPrintable(QString("%1: got %2, want %3")
                                    .arg(tt.key)
                                    .arg(epoch(v).toString(Qt::ISODateWithMs))
                                    .arg(tt.iso)));
        }
    }

    void testGetTimesWithHeight() {
        const double date = QDateTime(QDate(2013, 3, 5), QTime(0, 0, 0),
                                      QTimeZone::utc())
                                .toMSecsSinceEpoch();
        const double lat = 50.5, lng = 30.5, height = 2000;

        const struct {
            const char* key;
            const char* iso;
        } heightTestTimes[] = {
            {"solarNoon", "2013-03-05T10:10:57Z"},
            {"nadir", "2013-03-04T22:10:57Z"},
            {"sunrise", "2013-03-05T04:25:07Z"},
            {"sunset", "2013-03-05T15:56:46Z"},
        };

        const SunTimes times = getTimes(date, lat, lng, height);
        const auto field = [](const SunTimes& t, const char* key) -> double {
            if (qstrcmp(key, "solarNoon") == 0) return t.solarNoon;
            if (qstrcmp(key, "nadir") == 0) return t.nadir;
            if (qstrcmp(key, "sunrise") == 0) return t.sunrise;
            if (qstrcmp(key, "sunset") == 0) return t.sunset;
            return std::nan("");
        };

        for (const auto& tt : heightTestTimes) {
            const double v = field(times, tt.key);
            QVERIFY2(!isMissing(v), qPrintable(QString("missing: %1").arg(tt.key)));
            QVERIFY2(sameSecond(v, tt.iso),
                     qPrintable(QString("%1: got %2, want %3")
                                    .arg(tt.key)
                                    .arg(epoch(v).toString(Qt::ISODateWithMs))
                                    .arg(tt.iso)));
        }
    }
};

QTEST_GUILESS_MAIN(TestSuncalc)
#include "test_suncalc.moc"
