// test_solar.cpp — WDD sun-segment model: boundaries, polar states,
// window/image selection, 2-segment mode, duplicate-list absorption.

#include <QtTest>

#include <cmath>
#include <optional>

#include "solar.hpp"
#include "suncalc.hpp"

using namespace johona;

namespace {

constexpr double kSec = 1000.0;
constexpr double kHour = 3600000.0;

double localMs(const QDate& day, int hour, int minute, const QTimeZone& tz) {
    return QDateTime(day, QTime(hour, minute), tz).toMSecsSinceEpoch();
}

bool closeMs(double a, double b, double tolMs = 1.0 * kSec) {
    return std::abs(a - b) <= tolMs;
}

/// Independent polar-state expectation derived from the sun's extreme
/// altitudes (radians → degrees), cross-checking the crossing-based logic.
/// Independent WDD-faithful polar-state determination from the raw
/// suncalc crossings (missing crossing = NaN):
///   1. no sunrise/sunset  → total polar (noon altitude decides)
///   2. no dawn/dusk       → civil polar day
///   3. no golden hour     → civil polar night
solar::PolarState expectedPolarState(double noonMs, double lat, double lon) {
    const suncalc::SunTimes t = suncalc::getTimes(noonMs, lat, lon);
    if (std::isnan(t.sunrise) || std::isnan(t.sunset)) {
        const double noonAlt = suncalc::getPosition(t.solarNoon, lat, lon).altitude;
        return noonAlt > 0.0 ? solar::PolarState::PolarDay
                             : solar::PolarState::PolarNight;
    }
    if (std::isnan(t.dawn) || std::isnan(t.dusk))
        return solar::PolarState::CivilPolarDay;
    if (std::isnan(t.goldenHourEnd) || std::isnan(t.goldenHour))
        return solar::PolarState::CivilPolarNight;
    return solar::PolarState::None;
}

}  // namespace

class TestSolar : public QObject {
    Q_OBJECT

public:
    TestSolar() : m_tz("America/Phoenix") {}

private slots:
    void normalDay_segments();
    void normalDay_windowsAndImages();
    void twoSegmentMode();
    void duplicateListAbsorption();
    void polarDay();
    void polarNight();
    void civilPolarDay();
    void civilPolarNight();
    void segmentsForNow_nightWrap();
    void nextChangeTime();

private:
    // Phoenix, AZ (kWallpaper default location; no DST).
    static constexpr double kLat = 33.4484;
    static constexpr double kLon = -112.074;
    QTimeZone m_tz;
    // Svalbard, Norway (polar).
    static constexpr double kPolarLat = 78.22;
    static constexpr double kPolarLon = 15.65;
};

void TestSolar::normalDay_segments() {
    const QDate day(2013, 3, 5);
    const auto seg = solar::segmentsForDay(day, m_tz, kLat, kLon);

    QCOMPARE(seg.polar, solar::PolarState::None);
    QVERIFY(seg.complete());
    QVERIFY(seg.sunrise.has_value());
    QVERIFY(seg.sunset.has_value());
    QVERIFY(seg.dawn.has_value());
    QVERIFY(seg.goldenHourEnd.has_value());
    QVERIFY(seg.goldenHour.has_value());
    QVERIFY(seg.dusk.has_value());
    QVERIFY(seg.nextDawn.has_value());

    // Strict ordering.
    QVERIFY(*seg.dawn < *seg.sunrise);
    QVERIFY(*seg.sunrise < *seg.goldenHourEnd);
    QVERIFY(*seg.goldenHourEnd < seg.solarNoon);
    QVERIFY(seg.solarNoon < *seg.goldenHour);
    QVERIFY(*seg.goldenHour < *seg.sunset);
    QVERIFY(*seg.sunset < *seg.dusk);
    QVERIFY(*seg.dusk < *seg.nextDawn);

    // Cross-check against the raw suncalc port (same inputs → same values).
    const double noonMs = localMs(day, 12, 0, m_tz);
    const suncalc::SunTimes t = suncalc::getTimes(noonMs, kLat, kLon);
    QVERIFY(closeMs(seg.solarNoon, t.solarNoon, 1.0));
    QVERIFY(closeMs(*seg.sunrise, t.sunrise, 1.0));
    QVERIFY(closeMs(*seg.sunset, t.sunset, 1.0));
    QVERIFY(closeMs(*seg.dawn, t.dawn, 1.0));
    QVERIFY(closeMs(*seg.goldenHourEnd, t.goldenHourEnd, 1.0));
    QVERIFY(closeMs(*seg.goldenHour, t.goldenHour, 1.0));
    QVERIFY(closeMs(*seg.dusk, t.dusk, 1.0));

    // The next day's dawn is ~24 h after today's.
    QVERIFY(std::abs(*seg.nextDawn - *seg.dawn - 24.0 * kHour) < 2.0 * kHour);

    // Sanity: the independent altitude-based state agrees.
    QCOMPARE(seg.polar, expectedPolarState(noonMs, kLat, kLon));
}

void TestSolar::normalDay_windowsAndImages() {
    const QDate day(2013, 3, 5);
    const auto seg = solar::segmentsForDay(day, m_tz, kLat, kLon);
    solar::ThemeImageLists lists;
    lists.sunrise = {1, 2};
    lists.day = {3, 4, 5};
    lists.sunset = {6};
    lists.night = {7, 8, 9, 10};

    const auto wins = solar::effectiveWindows(seg, lists);
    QCOMPARE(wins.size(), static_cast<size_t>(4));
    QCOMPARE(wins[0].category, solar::Category::Sunrise);
    QCOMPARE(wins[1].category, solar::Category::Day);
    QCOMPARE(wins[2].category, solar::Category::Sunset);
    QCOMPARE(wins[3].category, solar::Category::Night);

    // Window boundaries are the segment crossings.
    QVERIFY(closeMs(wins[0].start, *seg.dawn, 1.0));
    QVERIFY(closeMs(wins[0].end, *seg.goldenHourEnd, 1.0));
    QVERIFY(closeMs(wins[1].start, *seg.goldenHourEnd, 1.0));
    QVERIFY(closeMs(wins[1].end, *seg.goldenHour, 1.0));
    QVERIFY(closeMs(wins[2].start, *seg.goldenHour, 1.0));
    QVERIFY(closeMs(wins[2].end, *seg.dusk, 1.0));
    QVERIFY(closeMs(wins[3].start, *seg.dusk, 1.0));
    QVERIFY(closeMs(wins[3].end, *seg.nextDawn, 1.0));

    // Every boundary: (2+1) + (3+1) + (1+1) + (4+1) = 14.
    QCOMPARE(solar::allBoundaries(seg, lists).size(), static_cast<size_t>(14));

    // Image selection at representative instants.
    auto sel = solar::imageAt(*seg.dawn + 1.0, seg, lists);
    QVERIFY(sel.has_value());
    QCOMPARE(sel->category, solar::Category::Sunrise);
    QCOMPARE(sel->index, 0);
    QCOMPARE(sel->imageValue, 1);

    sel = solar::imageAt(*seg.goldenHourEnd - 1.0, seg, lists);
    QVERIFY(sel.has_value());
    QCOMPARE(sel->index, 1);
    QCOMPARE(sel->imageValue, 2);

    sel = solar::imageAt(*seg.goldenHourEnd + 1.0, seg, lists);
    QVERIFY(sel.has_value());
    QCOMPARE(sel->category, solar::Category::Day);
    QCOMPARE(sel->index, 0);
    QCOMPARE(sel->imageValue, 3);

    // Solar noon is the middle of the day window → image 4 of 3,4,5.
    sel = solar::imageAt(seg.solarNoon, seg, lists);
    QVERIFY(sel.has_value());
    QCOMPARE(sel->category, solar::Category::Day);
    QCOMPARE(sel->index, 1);
    QCOMPARE(sel->imageValue, 4);

    sel = solar::imageAt(*seg.dusk + 1.0, seg, lists);
    QVERIFY(sel.has_value());
    QCOMPARE(sel->category, solar::Category::Night);
    QCOMPARE(sel->index, 0);
    QCOMPARE(sel->imageValue, 7);

    sel = solar::imageAt(*seg.nextDawn - 1.0, seg, lists);
    QVERIFY(sel.has_value());
    QCOMPARE(sel->category, solar::Category::Night);
    QCOMPARE(sel->index, 3);
    QCOMPARE(sel->imageValue, 10);
}

void TestSolar::twoSegmentMode() {
    const QDate day(2013, 3, 5);
    const auto seg = solar::segmentsForDay(day, m_tz, kLat, kLon);
    solar::ThemeImageLists lists;
    lists.day = {1, 2};
    lists.night = {3};
    QVERIFY(lists.twoSegment());

    const auto wins = solar::effectiveWindows(seg, lists);
    QCOMPARE(wins.size(), static_cast<size_t>(2));
    QCOMPARE(wins[0].category, solar::Category::Day);
    QCOMPARE(wins[1].category, solar::Category::Night);
    QVERIFY(closeMs(wins[0].start, *seg.sunrise, 1.0));
    QVERIFY(closeMs(wins[0].end, *seg.sunset, 1.0));
    QVERIFY(closeMs(wins[1].start, *seg.sunset, 1.0));
    QVERIFY(closeMs(wins[1].end, *seg.nextSunrise, 1.0));

    // Noon → second day image; dusk → night image.
    auto sel = solar::imageAt(seg.solarNoon, seg, lists);
    QVERIFY(sel.has_value());
    QCOMPARE(sel->index, 1);
    QCOMPARE(sel->imageValue, 2);

    sel = solar::imageAt(*seg.dusk, seg, lists);
    QVERIFY(sel.has_value());
    QCOMPARE(sel->category, solar::Category::Night);
    QCOMPARE(sel->imageValue, 3);
}

void TestSolar::duplicateListAbsorption() {
    const QDate day(2013, 3, 5);
    const auto seg = solar::segmentsForDay(day, m_tz, kLat, kLon);
    solar::ThemeImageLists lists;
    lists.sunrise = {1, 2};
    lists.day = {1, 2};  // equal to sunrise → absorbed into the day window
    lists.sunset = {3};
    lists.night = {4};

    const auto wins = solar::effectiveWindows(seg, lists);
    QCOMPARE(wins.size(), static_cast<size_t>(3));
    QCOMPARE(wins[0].category, solar::Category::Day);
    // The day window starts at dawn (absorption), ends at goldenHour.
    QVERIFY(closeMs(wins[0].start, *seg.dawn, 1.0));
    QVERIFY(closeMs(wins[0].end, *seg.goldenHour, 1.0));
    QCOMPARE(wins[1].category, solar::Category::Sunset);
    QCOMPARE(wins[2].category, solar::Category::Night);

    // First image right after dawn is day image 1 (not a separate sunrise).
    auto sel = solar::imageAt(*seg.dawn + 1.0, seg, lists);
    QVERIFY(sel.has_value());
    QCOMPARE(sel->category, solar::Category::Day);
    QCOMPARE(sel->imageValue, 1);

    sel = solar::imageAt(*seg.goldenHour + 1.0, seg, lists);
    QVERIFY(sel.has_value());
    QCOMPARE(sel->category, solar::Category::Sunset);
    QCOMPARE(sel->imageValue, 3);
}

void TestSolar::polarDay() {
    const QDate day(2013, 6, 21);
    const auto seg = solar::segmentsForDay(day, m_tz, kPolarLat, kPolarLon);
    QCOMPARE(seg.polar, solar::PolarState::PolarDay);
    QVERIFY(!seg.sunrise.has_value());
    QVERIFY(!seg.sunset.has_value());
    QCOMPARE(seg.polar, expectedPolarState(localMs(day, 12, 0, m_tz), kPolarLat,
                                            kPolarLon));

    solar::ThemeImageLists lists;
    lists.day = {1, 2};
    const auto wins = solar::effectiveWindows(seg, lists);
    QCOMPARE(wins.size(), static_cast<size_t>(1));
    QCOMPARE(wins[0].category, solar::Category::Day);
    // Full 24 h around solar noon.
    QVERIFY(closeMs(wins[0].start, seg.solarNoon - 12.0 * kHour, 1.0));
    QVERIFY(closeMs(wins[0].end, seg.solarNoon + 12.0 * kHour, 1.0));

    auto sel = solar::imageAt(seg.solarNoon - 11.0 * kHour, seg, lists);
    QVERIFY(sel.has_value());
    QCOMPARE(sel->index, 0);
    QCOMPARE(sel->imageValue, 1);
}

void TestSolar::polarNight() {
    const QDate day(2013, 12, 21);
    const auto seg = solar::segmentsForDay(day, m_tz, kPolarLat, kPolarLon);
    QCOMPARE(seg.polar, solar::PolarState::PolarNight);
    QVERIFY(!seg.sunrise.has_value());
    QVERIFY(!seg.sunset.has_value());
    QCOMPARE(seg.polar, expectedPolarState(localMs(day, 12, 0, m_tz), kPolarLat,
                                            kPolarLon));

    solar::ThemeImageLists lists;
    lists.night = {9};
    const auto wins = solar::effectiveWindows(seg, lists);
    QCOMPARE(wins.size(), static_cast<size_t>(1));
    QCOMPARE(wins[0].category, solar::Category::Night);
    QVERIFY(closeMs(wins[0].start, seg.solarNoon - 12.0 * kHour, 1.0));
    QVERIFY(closeMs(wins[0].end, seg.solarNoon + 12.0 * kHour, 1.0));

    auto sel = solar::imageAt(seg.solarNoon, seg, lists);
    QVERIFY(sel.has_value());
    QCOMPARE(sel->imageValue, 9);
}

void TestSolar::civilPolarDay() {
    // Mid-April at 78 N: the sun sets below 0 deg but never below -6 deg.
    const QDate day(2013, 4, 10);
    const auto seg = solar::segmentsForDay(day, m_tz, kPolarLat, kPolarLon);
    QCOMPARE(seg.polar, solar::PolarState::CivilPolarDay);
    QCOMPARE(seg.polar, expectedPolarState(localMs(day, 12, 0, m_tz), kPolarLat,
                                            kPolarLon));
    // Fallback boundaries: dawn := solarNoon - 12 h, dusk := next noon - 12 h.
    QVERIFY(seg.dawn.has_value());
    QVERIFY(seg.dusk.has_value());
    QVERIFY(closeMs(*seg.dawn, seg.solarNoon - 12.0 * kHour, 1.0));

    // No night window; the day images span dawn→dusk.
    solar::ThemeImageLists lists;
    lists.day = {1, 2};
    lists.night = {3};
    const auto wins = solar::effectiveWindows(seg, lists);
    for (const auto& w : wins)
        QVERIFY(w.category != solar::Category::Night);
}

void TestSolar::civilPolarNight() {
    // 2013-11-10 at 72 N: the sun crosses the horizon (short day) but never
    // reaches +6 deg → no golden-hour crossing → civil polar night.
    const QDate day(2013, 11, 10);
    const QTimeZone moscow("Europe/Moscow");
    const double lat = 72.0, lon = 25.0;
    const auto seg = solar::segmentsForDay(day, moscow, lat, lon);
    QCOMPARE(seg.polar, solar::PolarState::CivilPolarNight);
    QCOMPARE(seg.polar, expectedPolarState(localMs(day, 12, 0, moscow), lat, lon));
    // Fallback: goldenHourEnd := goldenHour := solarNoon (day window empty).
    QVERIFY(seg.goldenHourEnd.has_value());
    QVERIFY(seg.goldenHour.has_value());
    QVERIFY(closeMs(*seg.goldenHourEnd, seg.solarNoon, 1.0));
    QVERIFY(closeMs(*seg.goldenHour, seg.solarNoon, 1.0));

    solar::ThemeImageLists lists;
    lists.day = {1};
    lists.night = {2, 3};
    const auto wins = solar::effectiveWindows(seg, lists);
    for (const auto& w : wins)
        QVERIFY(w.category != solar::Category::Day);
    QCOMPARE(wins.size(), static_cast<size_t>(1));
    QCOMPARE(wins[0].category, solar::Category::Night);
}

void TestSolar::segmentsForNow_nightWrap() {
    solar::ThemeImageLists lists;
    lists.sunrise = {1};
    lists.day = {2};
    lists.sunset = {3};
    lists.night = {4};

    // 22:00 local on 2013-03-05: after dusk → today's set.
    const double evening = localMs(QDate(2013, 3, 5), 22, 0, m_tz);
    auto seg =
        solar::segmentsForNow(evening, QDate(2013, 3, 5), m_tz, kLat, kLon, lists);
    QCOMPARE(seg.day, QDate(2013, 3, 5));

    // 02:00 local on 2013-03-05: before dawn → previous day's set.
    const double preDawn = localMs(QDate(2013, 3, 5), 2, 0, m_tz);
    seg = solar::segmentsForNow(preDawn, QDate(2013, 3, 5), m_tz, kLat, kLon, lists);
    QCOMPARE(seg.day, QDate(2013, 3, 4));
}

void TestSolar::nextChangeTime() {
    const QDate day(2013, 3, 5);
    const auto seg = solar::segmentsForDay(day, m_tz, kLat, kLon);
    solar::ThemeImageLists lists;
    lists.sunrise = {1, 2};
    lists.day = {3, 4, 5};
    lists.sunset = {6};
    lists.night = {7, 8, 9, 10};
    auto provider = [this](const QDate& d) {
        return solar::segmentsForDay(d, m_tz, kLat, kLon);
    };

    // Mid-morning: the next change is the end of the current sunrise image
    // (image 1, shown for the first half of the sunrise window).
    const double now = *seg.dawn + 5.0 * 60000.0;
    auto next = solar::nextChangeTime(now, seg, lists, provider, /*current=*/1);
    QVERIFY(next.has_value());
    const double expected = *seg.dawn + (*seg.goldenHourEnd - *seg.dawn) / 2.0;
    QVERIFY(closeMs(*next, expected, 1.0));

    // Same instant without a current image: the first boundary after now.
    next = solar::nextChangeTime(now, seg, lists, provider, /*current=*/-1);
    QVERIFY(next.has_value());
    QVERIFY(*next >= now);
    QVERIFY(*next <= expected);

    // Past this day's last boundary (nextDawn): walk forward to the next day.
    const double late = localMs(QDate(2013, 3, 6), 7, 0, m_tz);
    next = solar::nextChangeTime(late, seg, lists, provider, /*current=*/-1);
    QVERIFY(next.has_value());
    QVERIFY(*next > late);
    QVERIFY(*next < late + 24.0 * kHour);
}

QTEST_GUILESS_MAIN(TestSolar)
#include "test_solar.moc"
