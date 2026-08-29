// suncalc.cpp — C++ port of SunCalc v1.9.0 (https://github.com/mourner/suncalc)
//
// Original work:
//   (c) 2011-2015, Vladimir Agafonkin
//   SunCalc is a JavaScript library for calculating sun/moon position and
//   light phases.
//
// License: BSD-2-Clause (per the LICENSE file of mourner/suncalc v1.9.0).
// See third-party/SUNCALC-LICENSE for the full text.
//
// Sun calculations are based on the formulas from
// http://aa.quae.nl/en/reken/zonpositie.html (as used by the original).
//
// Line-for-line port of the v1.9.0 JavaScript: identical constants,
// identical formula structure, double precision throughout (JS `number`
// is an IEEE-754 double).

#include "suncalc.hpp"

#include <cmath>

namespace johona::suncalc {

namespace {

// shortcuts for easier to read formulas (as in the original)
constexpr double PI = 3.141592653589793238462643383279502884;
constexpr double rad = PI / 180.0;

// date/time constants and conversions
constexpr double dayMs = 1000.0 * 60.0 * 60.0 * 24.0;
constexpr double J1970 = 2440588.0;
constexpr double J2000 = 2451545.0;

double toJulian(double epochMs) { return epochMs / dayMs - 0.5 + J1970; }
double fromJulian(double j) { return (j + 0.5 - J1970) * dayMs; }
double toDays(double epochMs) { return toJulian(epochMs) - J2000; }

// general calculations for position
constexpr double e = rad * 23.4397;  // obliquity of the Earth

double rightAscension(double l, double b) {
    return std::atan2(std::sin(l) * std::cos(e) - std::tan(b) * std::sin(e), std::cos(l));
}
double declination(double l, double b) {
    return std::asin(std::sin(b) * std::cos(e) + std::cos(b) * std::sin(e) * std::sin(l));
}

double azimuthAngle(double H, double phi, double dec) {
    return std::atan2(std::sin(H), std::cos(H) * std::sin(phi) - std::tan(dec) * std::cos(phi));
}
double altitudeAngle(double H, double phi, double dec) {
    return std::asin(std::sin(phi) * std::sin(dec) + std::cos(phi) * std::cos(dec) * std::cos(H));
}

double siderealTime(double d, double lw) { return rad * (280.16 + 360.9856235 * d) - lw; }

// general sun calculations
double solarMeanAnomaly(double d) { return rad * (357.5291 + 0.98560028 * d); }

double eclipticLongitude(double M) {
    const double C = rad * (1.9148 * std::sin(M) + 0.02 * std::sin(2 * M) + 0.0003 * std::sin(3 * M));
    const double P = rad * 102.9372;  // perihelion of the Earth
    return M + C + P + PI;
}

struct SunCoords {
    double dec;
    double ra;
};

SunCoords sunCoords(double d) {
    const double M = solarMeanAnomaly(d);
    const double L = eclipticLongitude(M);
    return {declination(L, 0), rightAscension(L, 0)};
}

// sun times configuration (angle, rise field index, set field index)
// (angle, morning name, evening name) — as in the original
struct TimeDef {
    double angle;
    int rise;  // field index in SunTimes
    int set;
};

// field indices matching SunTimes declaration order
enum F {
    F_SOLAR_NOON = 0,
    F_NADIR,
    F_SUNRISE,
    F_SUNSET,
    F_SUNRISE_END,
    F_SUNSET_START,
    F_DAWN,
    F_DUSK,
    F_NAUTICAL_DAWN,
    F_NAUTICAL_DUSK,
    F_NIGHT_END,
    F_NIGHT,
    F_GOLDEN_HOUR_END,
    F_GOLDEN_HOUR,
};

constexpr TimeDef kTimes[] = {
    {-0.833, F_SUNRISE, F_SUNSET},
    {-0.3, F_SUNRISE_END, F_SUNSET_START},
    {-6, F_DAWN, F_DUSK},
    {-12, F_NAUTICAL_DAWN, F_NAUTICAL_DUSK},
    {-18, F_NIGHT_END, F_NIGHT},
    {6, F_GOLDEN_HOUR_END, F_GOLDEN_HOUR},
};

constexpr double J0 = 0.0009;

double julianCycle(double d, double lw) { return std::round(d - J0 - lw / (2 * PI)); }
double approxTransit(double Ht, double lw, double n) { return J0 + (Ht + lw) / (2 * PI) + n; }
double solarTransitJ(double ds, double M, double L) {
    return J2000 + ds + 0.0053 * std::sin(M) - 0.0069 * std::sin(2 * L);
}

double hourAngle(double h, double phi, double d) {
    return std::acos((std::sin(h) - std::sin(phi) * std::sin(d)) / (std::cos(phi) * std::cos(d)));
}
double observerAngle(double height) { return -2.076 * std::sqrt(height) / 60; }

// returns set time for the given sun altitude
double getSetJ(double h, double lw, double phi, double dec, double n, double M, double L) {
    const double w = hourAngle(h, phi, dec);
    const double a = approxTransit(w, lw, n);
    return solarTransitJ(a, M, L);
}

}  // namespace

SunPosition getPosition(double epochMs, double lat, double lng) {
    const double lw = rad * -lng;
    const double phi = rad * lat;
    const double d = toDays(epochMs);

    const SunCoords c = sunCoords(d);
    const double H = siderealTime(d, lw) - c.ra;

    return {azimuthAngle(H, phi, c.dec), altitudeAngle(H, phi, c.dec)};
}

SunTimes getTimes(double epochMs, double lat, double lng, double height) {
    height = height ? height : 0.0;

    const double lw = rad * -lng;
    const double phi = rad * lat;

    const double dh = observerAngle(height);

    const double d = toDays(epochMs);
    const double n = julianCycle(d, lw);
    const double ds = approxTransit(0, lw, n);

    const double M = solarMeanAnomaly(ds);
    const double L = eclipticLongitude(M);
    const double dec = declination(L, 0);

    const double Jnoon = solarTransitJ(ds, M, L);

    double fields[] = {
        fromJulian(Jnoon),       // solarNoon
        fromJulian(Jnoon - 0.5), // nadir
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    };

    for (const TimeDef& time : kTimes) {
        const double h0 = (time.angle + dh) * rad;

        const double Jset = getSetJ(h0, lw, phi, dec, n, M, L);
        const double Jrise = Jnoon - (Jset - Jnoon);

        fields[time.rise] = fromJulian(Jrise);
        fields[time.set] = fromJulian(Jset);
    }

    SunTimes r;
    r.solarNoon = fields[F_SOLAR_NOON];
    r.nadir = fields[F_NADIR];
    r.sunrise = fields[F_SUNRISE];
    r.sunset = fields[F_SUNSET];
    r.sunriseEnd = fields[F_SUNRISE_END];
    r.sunsetStart = fields[F_SUNSET_START];
    r.dawn = fields[F_DAWN];
    r.dusk = fields[F_DUSK];
    r.nauticalDawn = fields[F_NAUTICAL_DAWN];
    r.nauticalDusk = fields[F_NAUTICAL_DUSK];
    r.nightEnd = fields[F_NIGHT_END];
    r.night = fields[F_NIGHT];
    r.goldenHourEnd = fields[F_GOLDEN_HOUR_END];
    r.goldenHour = fields[F_GOLDEN_HOUR];
    return r;
}

}  // namespace johona::suncalc
