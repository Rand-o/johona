// solar.hpp — WDD sun-segment model (Segments, polar states, image timing)
//
// Implements the segment model used by WinDynamicDesktop
// (t1m0thyj/WinDynamicDesktop, MPL-2.0): four sun segments per day —
//
//     dawn (-6 deg) -> goldenHourEnd (+6 deg) -> goldenHour (+6 deg) ->
//     dusk (-6 deg) -> next dawn
//
// each divided evenly among its theme images — plus WDD's four polar
// states and its 2-segment (day/night) mode for themes with empty
// sunrise/sunset image lists.
//
// All times are Unix-epoch milliseconds (double).  Boundaries that do not
// exist on a given date (polar day/night) are std::nullopt.

#pragma once

#include <optional>
#include <vector>

#include <QDate>
#include <QTimeZone>

#include "suncalc.hpp"

namespace johona::solar {

/// WDD's four polar states (WinDynamicDesktop `PolarPeriod`).
enum class PolarState {
    None,             // normal day
    PolarDay,         // sun never sets: sunrise/sunset missing, up at noon
    PolarNight,       // sun never rises: sunrise/sunset missing, down at noon
    CivilPolarDay,    // sun sets below 0 deg but never below -6 deg (no dawn/dusk)
    CivilPolarNight,  // sun rises above -6 deg but never above +6 deg (no golden hour)
};

inline bool isTotalPolar(PolarState p) {
    return p == PolarState::PolarDay || p == PolarState::PolarNight;
}

/// The four wallpaper categories (WDD `DaySegment`, minus the polar aliases).
enum class Category { Sunrise, Day, Sunset, Night };

const char* categoryName(Category c);

/// One day's sun-segment boundaries (epoch ms, UTC).
///
/// Computed at local noon of `day` (WDD's trick, which avoids a suncalc
/// date-rollover bug), then expressed as absolute instants.
struct Segments {
    QDate day;  // local calendar date this set represents

    PolarState polar = PolarState::None;
    double solarNoon = 0.0;

    std::optional<double> sunrise;        // -0.833 deg crossing (morning)
    std::optional<double> sunset;         // -0.833 deg crossing (evening)
    std::optional<double> dawn;           // -6 deg crossing (morning)
    std::optional<double> goldenHourEnd;  // +6 deg crossing (morning)
    std::optional<double> goldenHour;     // +6 deg crossing (evening)
    std::optional<double> dusk;           // -6 deg crossing (evening)

    // Next-day boundaries needed to close the night window.
    std::optional<double> nextDawn;       // following day's dawn
    std::optional<double> nextSunrise;    // following day's sunrise

    /// True when the four-segment model is fully defined (all crossings
    /// exist and are strictly ordered).
    bool complete() const;
};

/// The four image lists of a theme (values from theme.json).
struct ThemeImageLists {
    std::vector<int> sunrise;
    std::vector<int> day;
    std::vector<int> sunset;
    std::vector<int> night;

    bool empty() const {
        return sunrise.empty() && day.empty() && sunset.empty() && night.empty();
    }
    /// WDD's 2-segment mode trigger: an empty sunrise or sunset list.
    bool twoSegment() const { return sunrise.empty() || sunset.empty(); }
};

/// One effective image window: the category displays its images evenly
/// divided over [start, end).
struct Window {
    Category category;
    double start;  // inclusive, epoch ms
    double end;    // exclusive, epoch ms
};

/// A selected image: category + position in that category's list.
struct ImageSelection {
    Category category;
    int index = 0;       // position in the category's image list
    int imageValue = 0;  // the theme.json value at that position
};

/// Compute the segments for the local calendar date `day` at (lat, lon) in
/// timezone `tz`.  Faithful to WDD's GetSolarData:
///  - phases computed at local noon of `day` (WDD's UTC-noon workaround);
///  - PolarDay/PolarNight when sunrise or sunset is missing (decided by the
///    sun's altitude at solar noon);
///  - CivilPolarDay when dawn and dusk are missing (dawn := solarNoon - 12 h,
///    dusk := next day's solarNoon - 12 h);
///  - CivilPolarNight when the golden-hour crossings are missing
///    (goldenHourEnd := goldenHour := solarNoon).
Segments segmentsForDay(const QDate& day, const QTimeZone& tz, double lat, double lon);

/// The segments that own the instant `nowMs` (epoch ms).
///
/// The night window wraps midnight, so instants before the day's first
/// boundary belong to the previous day's set.  In 2-segment mode (empty
/// sunrise/sunset list) the first boundary is the sunrise crossing;
/// otherwise it is dawn.
Segments segmentsForNow(double nowMs, const QDate& todayLocal, const QTimeZone& tz,
                        double lat, double lon, const ThemeImageLists& lists);

/// The effective image windows for a theme on the given day (see
/// effectiveWindows documentation in solar.cpp for the exact rules,
/// including the WDD duplicate-list quirk and the 2-segment mode).
std::vector<Window> effectiveWindows(const Segments& seg, const ThemeImageLists& lists);

/// Select the image displayed at `nowMs`.  Returns nullopt when no window
/// covers the instant (e.g. an empty list in a polar state).
std::optional<ImageSelection> imageAt(double nowMs, const Segments& seg,
                                      const ThemeImageLists& lists);

/// Every image-change instant of the day: each window's start, its internal
/// image boundaries, and its end.  Sorted ascending.
std::vector<double> allBoundaries(const Segments& seg, const ThemeImageLists& lists);

/// The next wallpaper-change instant strictly after `nowMs`.
///
/// `segmentsProvider` is used to walk forward day by day when `nowMs` is at
/// or after this day's last boundary (delayed run / clock jump); the walk is
/// bounded (8 days).  `currentIndex` is the position of the currently
/// displayed image in its category list (or -1); when its display window
/// still lies ahead of `nowMs`, that window's end is returned (keeps
/// re-arming correct after a delayed run).
std::optional<double> nextChangeTime(double nowMs, const Segments& seg,
                                     const ThemeImageLists& lists,
                                     const std::function<Segments(const QDate&)>& segmentsProvider,
                                     int currentIndex = -1);

/// Format an epoch-ms instant as a local wall-clock string (for logs/UI).
QString formatLocal(double epochMs, const QTimeZone& tz);

}  // namespace johona::solar
