// suncalc.hpp — C++ port of SunCalc v1.9.0 (https://github.com/mourner/suncalc)
//
// Original work:
//   (c) 2011-2015, Vladimir Agafonkin
//   SunCalc is a JavaScript library for calculating sun/moon position and
//   light phases.
//
// License: BSD-2-Clause (per the LICENSE file of mourner/suncalc v1.9.0).
// See third-party/SUNCALC-LICENSE for the full text.
//
// This port covers the sun calculations only (getPosition, getTimes), which
// is all Johona needs.  The port is line-for-line faithful to the v1.9.0
// JavaScript: same constants, same formula ordering, same floating-point
// types (double == JS number), so results match the reference test vectors
// exactly (see tests/test_suncalc.cpp, ported 1:1 from v1/test.js).
//
// All times are Unix-epoch milliseconds (double), matching JS `valueOf()`.

#pragma once

namespace johona::suncalc {

/// Sun position (azimuth and altitude in radians), like SunCalc.getPosition.
struct SunPosition {
    double azimuth = 0.0;   // 0 = south, PI/2 = west (JS convention)
    double altitude = 0.0;  // radians above the horizon
};

/// Sun phase times (Unix epoch ms).  A value of NaN means the crossing
/// does not exist on that date (polar day/night) — the same condition
/// that produces an "Invalid Date" in the JavaScript original.
struct SunTimes {
    double solarNoon = 0.0;
    double nadir = 0.0;
    double sunrise = 0.0;       // -0.833 deg
    double sunset = 0.0;        // -0.833 deg
    double sunriseEnd = 0.0;    // -0.3 deg
    double sunsetStart = 0.0;   // -0.3 deg
    double dawn = 0.0;          // -6 deg (civil twilight)
    double dusk = 0.0;          // -6 deg (civil twilight)
    double nauticalDawn = 0.0;  // -12 deg
    double nauticalDusk = 0.0;  // -12 deg
    double nightEnd = 0.0;      // -18 deg
    double night = 0.0;         // -18 deg
    double goldenHourEnd = 0.0; // +6 deg (morning)
    double goldenHour = 0.0;    // +6 deg (evening)
};

/// Sun position for the given instant (epoch ms) and location.
/// Port of SunCalc.getPosition(date, lat, lng).
SunPosition getPosition(double epochMs, double lat, double lng);

/// Sun phase times for the date containing the given instant (epoch ms).
/// Port of SunCalc.getTimes(date, lat, lng, height) — pass height = 0 for
/// the standard (no observer-height correction) times.
SunTimes getTimes(double epochMs, double lat, double lng, double height = 0.0);

/// True when a SunTimes field represents a missing crossing.
inline bool isMissing(double t) { return t != t; }  // NaN check

}  // namespace johona::suncalc
