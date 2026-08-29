// appicons.hpp — embedded app/tray icons (SVG, rendered on demand).
//
// The same artwork ships as files under data/icons/ for the Flatpak
// install; the embedded copies keep the dev build (run from the build
// tree) icon-complete without any resource step.

#pragma once

#include <QIcon>
#include <QPixmap>
#include <QPainter>
#include <QSvgRenderer>

namespace johona::gui {

inline const char kAppIconSvg[] = R"SVG(<?xml version="1.0" encoding="utf-8"?>
<svg xmlns="http://www.w3.org/2000/svg" width="512" height="512" viewBox="0 0 512 512">
  <circle cx="256" cy="256" r="112" fill="#F8C156"/>
  <g stroke="#F8C156" stroke-width="30" stroke-linecap="round">
    <line x1="256" y1="64" x2="256" y2="116"/>
    <line x1="256" y1="396" x2="256" y2="448"/>
    <line x1="64" y1="256" x2="116" y2="256"/>
    <line x1="396" y1="256" x2="448" y2="256"/>
    <line x1="120" y1="120" x2="160" y2="160"/>
    <line x1="352" y1="352" x2="392" y2="392"/>
    <line x1="392" y1="120" x2="352" y2="160"/>
    <line x1="160" y1="352" x2="120" y2="392"/>
  </g>
</svg>)SVG";

/// Tray icon for light UI mode (dark glyph on light backgrounds).
inline const char kTrayLightSvg[] = R"SVG(<?xml version="1.0" encoding="utf-8"?>
<svg xmlns="http://www.w3.org/2000/svg" width="512" height="512" viewBox="0 0 512 512">
  <circle cx="256" cy="256" r="112" fill="#3B4252"/>
  <g stroke="#3B4252" stroke-width="30" stroke-linecap="round">
    <line x1="256" y1="64" x2="256" y2="116"/>
    <line x1="256" y1="396" x2="256" y2="448"/>
    <line x1="64" y1="256" x2="116" y2="256"/>
    <line x1="396" y1="256" x2="448" y2="256"/>
    <line x1="120" y1="120" x2="160" y2="160"/>
    <line x1="352" y1="352" x2="392" y2="392"/>
    <line x1="392" y1="120" x2="352" y2="160"/>
    <line x1="160" y1="352" x2="120" y2="392"/>
  </g>
</svg>)SVG";

/// Tray icon for dark UI mode (light glyph on dark backgrounds).
inline const char kTrayDarkSvg[] = R"SVG(<?xml version="1.0" encoding="utf-8"?>
<svg xmlns="http://www.w3.org/2000/svg" width="512" height="512" viewBox="0 0 512 512">
  <circle cx="256" cy="256" r="112" fill="#E8EAED"/>
  <g stroke="#E8EAED" stroke-width="30" stroke-linecap="round">
    <line x1="256" y1="64" x2="256" y2="116"/>
    <line x1="256" y1="396" x2="256" y2="448"/>
    <line x1="64" y1="256" x2="116" y2="256"/>
    <line x1="396" y1="256" x2="448" y2="256"/>
    <line x1="120" y1="120" x2="160" y2="160"/>
    <line x1="352" y1="352" x2="392" y2="392"/>
    <line x1="392" y1="120" x2="352" y2="160"/>
    <line x1="160" y1="352" x2="120" y2="392"/>
  </g>
</svg>)SVG";

inline QIcon svgIcon(const char* svg, int size = 128) {
    QPixmap pix(size, size);
    pix.fill(Qt::transparent);
    const QByteArray contents(svg);  // QByteArray ctor = SVG *contents*
    QSvgRenderer renderer(contents);  // (the QString ctor is a file name!)
    QPainter p(&pix);
    renderer.render(&p);
    p.end();
    return QIcon(pix);
}

// ── Fallback glyphs ────────────────────────────────────────────────────
// kWallpaper uses QIcon::fromTheme (Breeze) for every icon.  In the KDE
// Flatpak runtime the Breeze theme is present, so fromTheme normally wins;
// these embedded 16 px monochrome glyphs (currentColor-style, drawn in
// #d0d3d6) keep the dev build / other icon themes complete.

inline const char kFallbackImageSvg[] = R"SVG(<?xml version="1.0" encoding="utf-8"?>
<svg xmlns="http://www.w3.org/2000/svg" width="16" height="16" viewBox="0 0 16 16">
  <g fill="none" stroke="#d0d3d6" stroke-width="1.2">
    <rect x="1.5" y="2.5" width="13" height="11" rx="1"/>
    <circle cx="5.5" cy="6.5" r="1.4" fill="#d0d3d6" stroke="none"/>
    <path d="M2.5 12.5 L6.5 8.5 L9 11 L11 9 L13.5 12.5"/>
  </g>
</svg>)SVG";

inline const char kFallbackConfigureSvg[] = R"SVG(<?xml version="1.0" encoding="utf-8"?>
<svg xmlns="http://www.w3.org/2000/svg" width="16" height="16" viewBox="0 0 16 16">
  <g fill="none" stroke="#d0d3d6" stroke-width="1.2">
    <line x1="2" y1="4.5" x2="14" y2="4.5"/>
    <line x1="2" y1="8" x2="14" y2="8"/>
    <line x1="2" y1="11.5" x2="14" y2="11.5"/>
    <circle cx="10.5" cy="4.5" r="1.8" fill="#313437"/>
    <circle cx="5" cy="8" r="1.8" fill="#313437"/>
    <circle cx="11" cy="11.5" r="1.8" fill="#313437"/>
  </g>
</svg>)SVG";

inline const char kFallbackClockSvg[] = R"SVG(<?xml version="1.0" encoding="utf-8"?>
<svg xmlns="http://www.w3.org/2000/svg" width="16" height="16" viewBox="0 0 16 16">
  <g fill="none" stroke="#d0d3d6" stroke-width="1.2">
    <circle cx="8" cy="9" r="6"/>
    <line x1="8" y1="9" x2="8" y2="5.5"/>
    <line x1="8" y1="9" x2="10.5" y2="10"/>
    <line x1="6.5" y1="1.5" x2="9.5" y2="1.5"/>
  </g>
</svg>)SVG";

inline const char kFallbackImportSvg[] = R"SVG(<?xml version="1.0" encoding="utf-8"?>
<svg xmlns="http://www.w3.org/2000/svg" width="16" height="16" viewBox="0 0 16 16">
  <g fill="none" stroke="#d0d3d6" stroke-width="1.2">
    <path d="M2 10 V13.5 H14 V10"/>
    <line x1="8" y1="2" x2="8" y2="9"/>
    <path d="M5 6.5 L8 9.5 L11 6.5"/>
  </g>
</svg>)SVG";

inline const char kFallbackApplySvg[] = R"SVG(<?xml version="1.0" encoding="utf-8"?>
<svg xmlns="http://www.w3.org/2000/svg" width="16" height="16" viewBox="0 0 16 16">
  <path d="M2.5 8.5 L6 12 L13.5 4" fill="none" stroke="#d0d3d6" stroke-width="1.6" stroke-linecap="round" stroke-linejoin="round"/>
</svg>)SVG";

inline const char kFallbackTrashSvg[] = R"SVG(<?xml version="1.0" encoding="utf-8"?>
<svg xmlns="http://www.w3.org/2000/svg" width="16" height="16" viewBox="0 0 16 16">
  <g fill="none" stroke="#d0d3d6" stroke-width="1.2">
    <line x1="2.5" y1="4" x2="13.5" y2="4"/>
    <path d="M4 4 L4.8 13.5 H11.2 L12 4"/>
    <path d="M6.5 4 V2 H9.5 V4"/>
  </g>
</svg>)SVG";

inline const char kFallbackSaveSvg[] = R"SVG(<?xml version="1.0" encoding="utf-8"?>
<svg xmlns="http://www.w3.org/2000/svg" width="16" height="16" viewBox="0 0 16 16">
  <g fill="none" stroke="#d0d3d6" stroke-width="1.2">
    <path d="M2.5 2.5 H11 L13.5 5 V13.5 H2.5 Z"/>
    <path d="M5 2.5 V6 H11 V4"/>
    <rect x="5" y="9" width="6" height="4.5"/>
  </g>
</svg>)SVG";

inline const char kFallbackRefreshSvg[] = R"SVG(<?xml version="1.0" encoding="utf-8"?>
<svg xmlns="http://www.w3.org/2000/svg" width="16" height="16" viewBox="0 0 16 16">
  <g fill="none" stroke="#d0d3d6" stroke-width="1.3" stroke-linecap="round">
    <path d="M13 8 A5 5 0 1 1 11.5 4.5"/>
    <path d="M11.5 1.5 V4.5 H14.5"/>
  </g>
</svg>)SVG";

inline const char kFallbackPlaySvg[] = R"SVG(<?xml version="1.0" encoding="utf-8"?>
<svg xmlns="http://www.w3.org/2000/svg" width="16" height="16" viewBox="0 0 16 16">
  <path d="M4.5 2.5 L13 8 L4.5 13.5 Z" fill="#d0d3d6"/>
</svg>)SVG";

inline const char kFallbackStopSvg[] = R"SVG(<?xml version="1.0" encoding="utf-8"?>
<svg xmlns="http://www.w3.org/2000/svg" width="16" height="16" viewBox="0 0 16 16">
  <rect x="3.5" y="3.5" width="9" height="9" fill="#d0d3d6"/>
</svg>)SVG";

inline const char kFallbackExitSvg[] = R"SVG(<?xml version="1.0" encoding="utf-8"?>
<svg xmlns="http://www.w3.org/2000/svg" width="16" height="16" viewBox="0 0 16 16">
  <g fill="none" stroke="#d0d3d6" stroke-width="1.2">
    <path d="M6 2.5 H2.5 V13.5 H6"/>
    <line x1="8" y1="8" x2="13.5" y2="8"/>
    <path d="M11 5.5 L13.5 8 L11 10.5"/>
  </g>
</svg>)SVG";

inline const char kFallbackAboutSvg[] = R"SVG(<?xml version="1.0" encoding="utf-8"?>
<svg xmlns="http://www.w3.org/2000/svg" width="16" height="16" viewBox="0 0 16 16">
  <g fill="none" stroke="#d0d3d6" stroke-width="1.2">
    <circle cx="8" cy="8" r="6"/>
    <line x1="8" y1="7" x2="8" y2="11.5"/>
    <circle cx="8" cy="4.8" r="0.8" fill="#d0d3d6" stroke="none"/>
  </g>
</svg>)SVG";

inline const char kFallbackWindowSvg[] = R"SVG(<?xml version="1.0" encoding="utf-8"?>
<svg xmlns="http://www.w3.org/2000/svg" width="16" height="16" viewBox="0 0 16 16">
  <g fill="none" stroke="#d0d3d6" stroke-width="1.2">
    <rect x="2" y="3" width="12" height="10" rx="1"/>
    <line x1="2" y1="6" x2="14" y2="6"/>
  </g>
</svg>)SVG";

/// QIcon::fromTheme with an embedded fallback (kWallpaper parity: the
/// Breeze theme provides the real icons in the KDE runtime).
inline QIcon themeIcon(const QString& name, const char* fallbackSvg) {
    QIcon icon = QIcon::fromTheme(name);
    if (icon.isNull())
        icon = svgIcon(fallbackSvg, 32);
    return icon;
}

}  // namespace johona::gui
