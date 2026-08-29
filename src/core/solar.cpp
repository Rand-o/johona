// solar.cpp — WDD sun-segment model implementation.
//
// Segment model and polar-state handling follow WinDynamicDesktop
// (t1m0thyj/WinDynamicDesktop, MPL-2.0), SunriseSunsetService.GetSolarData
// and SolarScheduler (GetAllImageTimes / GetDaySegmentData /
// CalcNextUpdateTime).  Sun phase times come from the faithful suncalc v1.9.0
// port (see suncalc.cpp).

#include "solar.hpp"

#include <algorithm>
#include <cmath>

#include <QDateTime>
#include <QTime>

namespace johona::solar {

namespace {

constexpr double kHourMs = 3600000.0;

std::optional<double> opt(double v) {
    return suncalc::isMissing(v) ? std::nullopt : std::optional<double>(v);
}

/// Epoch ms of local noon (12:00) of `day` in timezone `tz`.
/// QDateTime has no direct "local time → epoch" conversion, so start from
/// the UTC interpretation and correct by the zone offset (re-checked once
/// for the DST-edge case).
double epochMsAtLocalNoon(const QDate& day, const QTimeZone& tz) {
    QDateTime utc(day, QTime(12, 0), QTimeZone(0));
    int offset = tz.offsetFromUtc(utc);
    utc = utc.addSecs(-offset);
    offset = tz.offsetFromUtc(utc);
    utc = utc.addSecs(-offset);
    return utc.toMSecsSinceEpoch();
}

const std::vector<int>& listFor(Category c, const ThemeImageLists& l) {
    switch (c) {
    case Category::Sunrise:
        return l.sunrise;
    case Category::Day:
        return l.day;
    case Category::Sunset:
        return l.sunset;
    case Category::Night:
        return l.night;
    }
    static const std::vector<int> empty;
    return empty;
}

}  // namespace

const char* categoryName(Category c) {
    switch (c) {
    case Category::Sunrise:
        return "sunrise";
    case Category::Day:
        return "day";
    case Category::Sunset:
        return "sunset";
    case Category::Night:
        return "night";
    }
    return "?";
}

bool Segments::complete() const {
    return dawn.has_value() && goldenHourEnd.has_value() && goldenHour.has_value() &&
           dusk.has_value() && nextDawn.has_value() && *dawn < *goldenHourEnd &&
           *goldenHourEnd < *goldenHour && *goldenHour < *dusk && *dusk < *nextDawn;
}

Segments segmentsForDay(const QDate& day, const QTimeZone& tz, double lat, double lon) {
    Segments seg;
    seg.day = day;

    // WDD's trick: compute the phases at local noon of the target date
    // (workaround for mourner/suncalc#107, a date-rollover bug when the
    // query instant is far from the date of interest).
    const double noonMs = epochMsAtLocalNoon(day, tz);

    const suncalc::SunTimes t = suncalc::getTimes(noonMs, lat, lon);
    seg.solarNoon = t.solarNoon;
    seg.sunrise = opt(t.sunrise);
    seg.sunset = opt(t.sunset);
    seg.dawn = opt(t.dawn);
    seg.goldenHourEnd = opt(t.goldenHourEnd);
    seg.goldenHour = opt(t.goldenHour);
    seg.dusk = opt(t.dusk);

    // Next-day boundaries (close the night window).
    const QDate nextDay = day.addDays(1);
    const double nextNoonMs = epochMsAtLocalNoon(nextDay, tz);
    const suncalc::SunTimes tn = suncalc::getTimes(nextNoonMs, lat, lon);
    seg.nextDawn = opt(tn.dawn);
    seg.nextSunrise = opt(tn.sunrise);
    // Fallback when the next day's crossing is missing (polar conditions):
    // use the next day's solar noon so the night window stays closed and the
    // scheduler stays armed (WDD would produce an invalid time here).
    if (!seg.nextDawn)
        seg.nextDawn = tn.solarNoon;
    if (!seg.nextSunrise)
        seg.nextSunrise = tn.solarNoon;

    // WDD's polar-state determination (GetSolarData):
    if (!seg.sunrise || !seg.sunset) {
        // Polar day/night: decide by the sun's altitude at solar noon.
        const suncalc::SunPosition pos = suncalc::getPosition(t.solarNoon, lat, lon);
        seg.polar = pos.altitude > 0.0 ? PolarState::PolarDay : PolarState::PolarNight;
    } else if (!seg.dawn && !seg.dusk) {
        // Civil polar day: no dawn/dusk crossing — skip the night segment.
        // WDD: solarTimes[0] = solarNoon - 12h; solarTimes[3] = next
        // day's solarNoon - 12h.
        seg.dawn = t.solarNoon - 12.0 * kHourMs;
        seg.dusk = tn.solarNoon - 12.0 * kHourMs;
        seg.polar = PolarState::CivilPolarDay;
    } else if (!seg.goldenHourEnd && !seg.goldenHour) {
        // Civil polar night: no golden-hour crossing — the day segment
        // collapses to noon.  WDD: solarTimes[1] = solarTimes[2] = solarNoon.
        seg.goldenHourEnd = t.solarNoon;
        seg.goldenHour = t.solarNoon;
        seg.polar = PolarState::CivilPolarNight;
    }
    return seg;
}

Segments segmentsForNow(double nowMs, const QDate& todayLocal, const QTimeZone& tz,
                        double lat, double lon, const ThemeImageLists& lists) {
    Segments today = segmentsForDay(todayLocal, tz, lat, lon);

    // Total polar states span the full 24 h around today's solar noon; no
    // day switching (WDD always uses today's data there).
    if (isTotalPolar(today.polar))
        return today;

    // The night window wraps midnight, so instants before the day's first
    // boundary belong to the previous day's set.  First boundary: dawn in
    // 4-segment mode, sunrise in 2-segment mode (the night window ends at
    // the next sunrise there).
    double first = today.solarNoon;
    if (lists.twoSegment()) {
        if (today.sunrise)
            first = *today.sunrise;
        else if (today.dawn)
            first = *today.dawn;
    } else if (today.dawn) {
        first = *today.dawn;
    }

    if (nowMs < first)
        return segmentsForDay(todayLocal.addDays(-1), tz, lat, lon);
    return today;
}

std::vector<Window> effectiveWindows(const Segments& seg, const ThemeImageLists& lists) {
    std::vector<Window> w;
    const bool hasS = !lists.sunrise.empty();
    const bool hasD = !lists.day.empty();
    const bool hasE = !lists.sunset.empty();
    const bool hasN = !lists.night.empty();
    // WDD duplicate-list quirk: a sunrise/sunset list equal to the day list
    // is absorbed into the day window (day starts at dawn / ends at dusk),
    // so the same images are not shown twice back-to-back across the
    // segment boundary.
    const bool sEq = hasS && hasD && lists.sunrise == lists.day;
    const bool eEq = hasE && hasD && lists.sunset == lists.day;

    switch (seg.polar) {
    case PolarState::PolarDay:
        // WDD: day images span the full 24 h around solar noon.
        if (hasD)
            w.push_back({Category::Day, seg.solarNoon - 12.0 * kHourMs,
                         seg.solarNoon + 12.0 * kHourMs});
        break;
    case PolarState::PolarNight:
        if (hasN)
            w.push_back({Category::Night, seg.solarNoon - 12.0 * kHourMs,
                         seg.solarNoon + 12.0 * kHourMs});
        break;
    case PolarState::CivilPolarDay:
        // dawn := midnight, dusk := next midnight (WDD).  No night window.
        if (hasS && !sEq)
            w.push_back({Category::Sunrise, *seg.dawn, *seg.goldenHourEnd});
        if (hasD)
            w.push_back({Category::Day, sEq ? *seg.dawn : *seg.goldenHourEnd,
                         eEq ? *seg.dusk : *seg.goldenHour});
        if (hasE && !eEq)
            w.push_back({Category::Sunset, *seg.goldenHour, *seg.dusk});
        break;
    case PolarState::CivilPolarNight:
        // goldenHourEnd := goldenHour := solarNoon: the day window is
        // empty (day images are never shown — WDD behavior).
        if (hasS && !sEq)
            w.push_back({Category::Sunrise, *seg.dawn, *seg.goldenHourEnd});
        if (hasE && !eEq)
            w.push_back({Category::Sunset, *seg.goldenHour, *seg.dusk});
        if (hasN)
            w.push_back({Category::Night, *seg.dusk, *seg.nextDawn});
        break;
    case PolarState::None:
        if (!seg.complete())
            break;
        if (lists.twoSegment()) {
            // WDD 2-segment mode (empty sunrise or sunset list):
            //   day   = [sunrise, sunset)
            //   night = [sunset, next sunrise)
            if (hasD && seg.sunrise && seg.sunset)
                w.push_back({Category::Day, *seg.sunrise, *seg.sunset});
            if (hasN && seg.sunset && seg.nextSunrise)
                w.push_back({Category::Night, *seg.sunset, *seg.nextSunrise});
        } else {
            // 4-segment mode (with the duplicate-list absorption above).
            if (hasS && !sEq)
                w.push_back({Category::Sunrise, *seg.dawn, *seg.goldenHourEnd});
            if (hasD)
                w.push_back({Category::Day, sEq ? *seg.dawn : *seg.goldenHourEnd,
                             eEq ? *seg.dusk : *seg.goldenHour});
            if (hasE && !eEq)
                w.push_back({Category::Sunset, *seg.goldenHour, *seg.dusk});
            if (hasN)
                w.push_back({Category::Night, *seg.dusk, *seg.nextDawn});
        }
        break;
    }
    return w;
}

std::optional<ImageSelection> imageAt(double nowMs, const Segments& seg,
                                      const ThemeImageLists& lists) {
    for (const Window& win : effectiveWindows(seg, lists)) {
        if (nowMs < win.start || nowMs >= win.end)
            continue;
        const std::vector<int>& list = listFor(win.category, lists);
        if (list.empty())
            return std::nullopt;
        const double duration = (win.end - win.start) / static_cast<double>(list.size());
        int idx = static_cast<int>((nowMs - win.start) / duration);
        idx = std::clamp(idx, 0, static_cast<int>(list.size()) - 1);
        return ImageSelection{win.category, idx, list[idx]};
    }
    return std::nullopt;
}

std::vector<double> allBoundaries(const Segments& seg, const ThemeImageLists& lists) {
    std::vector<double> bounds;
    for (const Window& win : effectiveWindows(seg, lists)) {
        const std::vector<int>& list = listFor(win.category, lists);
        if (list.empty())
            continue;
        const double duration = (win.end - win.start) / static_cast<double>(list.size());
        for (std::size_t i = 0; i <= list.size(); i++)
            bounds.push_back(win.start + duration * static_cast<double>(i));
    }
    std::sort(bounds.begin(), bounds.end());
    return bounds;
}

std::optional<double> nextChangeTime(double nowMs, const Segments& seg,
                                     const ThemeImageLists& lists,
                                     const std::function<Segments(const QDate&)>& segmentsProvider,
                                     int currentImageValue) {
    // When the currently displayed image's window still lies ahead of
    // `nowMs` (delayed run / clock skew), return that window's end — this
    // keeps re-arming correct.
    if (currentImageValue >= 0) {
        for (const Window& win : effectiveWindows(seg, lists)) {
            const std::vector<int>& list = listFor(win.category, lists);
            auto it = std::find(list.begin(), list.end(), currentImageValue);
            if (it == list.end())
                continue;
            const std::size_t i = static_cast<std::size_t>(std::distance(list.begin(), it));
            const double duration = (win.end - win.start) / static_cast<double>(list.size());
            const double winEnd = win.start + duration * static_cast<double>(i + 1);
            if (winEnd > nowMs)
                return winEnd;
        }
    }

    auto futureIn = [&](const Segments& s) -> std::optional<double> {
        std::optional<double> best;
        for (double b : allBoundaries(s, lists))
            if (b > nowMs && (!best.has_value() || b < *best))
                best = b;
        return best;
    };

    if (std::optional<double> r = futureIn(seg))
        return r;

    // `nowMs` is at/after this day's last boundary (delayed run past the
    // night end): walk forward day by day, bounded to a week.
    if (segmentsProvider) {
        for (int i = 1; i <= 8; i++) {
            const Segments s = segmentsProvider(seg.day.addDays(i));
            if (std::optional<double> r = futureIn(s))
                return r;
        }
    }
    return std::nullopt;
}

QString formatLocal(double epochMs, const QTimeZone& tz) {
    const QDateTime dt =
        QDateTime::fromSecsSinceEpoch(static_cast<qint64>(epochMs / 1000.0), tz);
    return dt.toString(QStringLiteral("yyyy-MM-dd hh:mm:ss"));
}

}  // namespace johona::solar
