// appicons.hpp — app/tray icons (embedded PNGs via icons.qrc) plus
// on-demand SVG rendering for the UI glyphs.
//
// The app artwork ships as PNGs under src/gui/icons/, embedded via
// icons.qrc so the dev build (run from the build tree) is icon-complete
// without any resource path on disk.  The sized desktop icons ship under
// data/icons/ (hicolor layout) for the Flatpak install.

#pragma once

#include <QIcon>
#include <QPixmap>
#include <QPainter>
#include <QSvgRenderer>

namespace johona::gui {

// The app/tray artwork ships as PNGs under src/gui/icons/ and is embedded
// via icons.qrc, so the dev build (run from the build tree) is
// icon-complete without any resource path on disk.

/// Application icon (full-color sun/moon).
inline QIcon appIcon() {
    return QIcon(QStringLiteral(":/icons/johona.png"));
}

/// Tray icon for light UI mode (dark glyph on light backgrounds).
inline QIcon trayIconLight() {
    return QIcon(QStringLiteral(":/icons/johona-light.png"));
}

/// Tray icon for dark UI mode (light glyph on dark backgrounds).
inline QIcon trayIconDark() {
    return QIcon(QStringLiteral(":/icons/johona-dark.png"));
}

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

// ── Color-tintable icons (redesign mockup line glyphs) ─────────────────
// The mockup's nav/menu glyphs are monochrome line icons that must follow
// the widget state (placeholder gray → white on the active nav pill), so
// they are embedded as "currentColor" SVG templates and rendered in an
// explicit color.  (QSvgRenderer has no currentColor support.)

inline QIcon colorIcon(const char* svgTemplate, const QColor& color,
                       int size = 24) {
    QString svg = QString::fromUtf8(svgTemplate);
    svg.replace(QStringLiteral("currentColor"), color.name());
    return svgIcon(svg.toUtf8().constData(), size);
}

/// Nav: Themes (image).
inline const char kNavThemesSvg[] = R"SVG(<?xml version="1.0" encoding="utf-8"?>
<svg xmlns="http://www.w3.org/2000/svg" width="24" height="24" viewBox="0 0 24 24">
  <g fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round">
    <rect x="3.5" y="4.5" width="17" height="15" rx="2"/>
    <circle cx="9" cy="10" r="1.6"/>
    <path d="M4.5 17.5l4.5-4.5 3.5 3.5 3-3 4 4.5"/>
  </g>
</svg>)SVG";

/// Nav: Scheduler (clock).
inline const char kNavSchedulerSvg[] = R"SVG(<?xml version="1.0" encoding="utf-8"?>
<svg xmlns="http://www.w3.org/2000/svg" width="24" height="24" viewBox="0 0 24 24">
  <g fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round">
    <circle cx="12" cy="12" r="8.5"/>
    <path d="M12 7.5V12l3 2"/>
  </g>
</svg>)SVG";

/// Nav: Settings (gear, Material Design).
inline const char kNavSettingsSvg[] = R"SVG(<?xml version="1.0" encoding="utf-8"?>
<svg xmlns="http://www.w3.org/2000/svg" width="24" height="24" viewBox="0 0 24 24">
  <path fill="currentColor" d="M19.14 12.94c.04-.3.06-.61.06-.94 0-.32-.02-.64-.07-.94l2.03-1.58a.49.49 0 00.12-.61l-1.92-3.32a.49.49 0 00-.59-.22l-2.39.96a7.03 7.03 0 00-1.62-.94l-.36-2.54a.48.48 0 00-.48-.41h-3.84a.48.48 0 00-.48.41l-.36 2.54c-.59.24-1.13.57-1.62.94l-2.39-.96a.48.48 0 00-.59.22L2.74 8.87a.48.48 0 00.12.61l2.03 1.58c-.05.3-.07.62-.07.94s.02.64.07.94l-2.03 1.58a.49.49 0 00-.12.61l1.92 3.32c.12.22.37.29.59.22l2.39-.96c.5.38 1.03.7 1.62.94l.36 2.54c.05.24.24.41.48.41h3.84c.24 0 .44-.17.48-.41l.36-2.54c.59-.24 1.13-.56 1.62-.94l2.39.96c.22.08.47 0 .59-.22l1.92-3.32a.49.49 0 00-.12-.61l-2.03-1.58zM12 15.6A3.61 3.61 0 018.4 12c0-1.98 1.62-3.6 3.6-3.6s3.6 1.62 3.6 3.6-1.62 3.6-3.6 3.6z"/>
</svg>)SVG";

/// Title bar: hamburger (three lines).
inline const char kMenuHamburgerSvg[] = R"SVG(<?xml version="1.0" encoding="utf-8"?>
<svg xmlns="http://www.w3.org/2000/svg" width="24" height="24" viewBox="0 0 24 24">
  <g fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round">
    <line x1="4" y1="7" x2="20" y2="7"/>
    <line x1="4" y1="12" x2="20" y2="12"/>
    <line x1="4" y1="17" x2="20" y2="17"/>
  </g>
</svg>)SVG";

/// Search field glyph (magnifier).
inline const char kSearchSvg[] = R"SVG(<?xml version="1.0" encoding="utf-8"?>
<svg xmlns="http://www.w3.org/2000/svg" width="24" height="24" viewBox="0 0 24 24">
  <g fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round">
    <circle cx="10.5" cy="10.5" r="6"/>
    <line x1="15.2" y1="15.2" x2="20" y2="20"/>
  </g>
</svg>)SVG";

/// Check glyph (ACTIVE badge, Apply button).
inline const char kCheckSvg[] = R"SVG(<?xml version="1.0" encoding="utf-8"?>
<svg xmlns="http://www.w3.org/2000/svg" width="24" height="24" viewBox="0 0 24 24">
  <path d="M5 12.5l4.5 4.5L19 7.5" fill="none" stroke="currentColor" stroke-width="3" stroke-linecap="round" stroke-linejoin="round"/>
</svg>)SVG";

/// Play triangle (filled).
inline const char kPlayFilledSvg[] = R"SVG(<?xml version="1.0" encoding="utf-8"?>
<svg xmlns="http://www.w3.org/2000/svg" width="24" height="24" viewBox="0 0 24 24">
  <path d="M7.5 5.5v13l11-6.5z" fill="currentColor"/>
</svg>)SVG";

/// Stop square (filled, rounded).
inline const char kStopFilledSvg[] = R"SVG(<?xml version="1.0" encoding="utf-8"?>
<svg xmlns="http://www.w3.org/2000/svg" width="24" height="24" viewBox="0 0 24 24">
  <rect x="6.5" y="6.5" width="11" height="11" rx="1.5" fill="currentColor"/>
</svg>)SVG";

/// Refresh / next-wallpaper (two arrows).
inline const char kRefreshSvg[] = R"SVG(<?xml version="1.0" encoding="utf-8"?>
<svg xmlns="http://www.w3.org/2000/svg" width="24" height="24" viewBox="0 0 24 24">
  <g fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
    <path d="M4.5 12a7.5 7.5 0 0 1 13-5.1M19.5 12a7.5 7.5 0 0 1-13 5.1"/>
    <path d="M17.5 3.5v3.6h-3.6M6.5 20.5v-3.6h3.6"/>
  </g>
</svg>)SVG";

/// Trash (delete theme).
inline const char kTrashSvg[] = R"SVG(<?xml version="1.0" encoding="utf-8"?>
<svg xmlns="http://www.w3.org/2000/svg" width="24" height="24" viewBox="0 0 24 24">
  <g fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round">
    <path d="M4.5 7h15M9.5 7V5h5v2M7 7l1 13h8l1-13"/>
    <line x1="10.5" y1="11" x2="10.5" y2="16.5"/>
    <line x1="13.5" y1="11" x2="13.5" y2="16.5"/>
  </g>
</svg>)SVG";

/// Import (tray + down arrow).
inline const char kImportSvg[] = R"SVG(<?xml version="1.0" encoding="utf-8"?>
<svg xmlns="http://www.w3.org/2000/svg" width="24" height="24" viewBox="0 0 24 24">
  <g fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
    <path d="M12 4v10M8 10.5l4 4 4-4"/>
    <path d="M5 19h14"/>
  </g>
</svg>)SVG";

/// Save (document with fold).
inline const char kSaveSvg[] = R"SVG(<?xml version="1.0" encoding="utf-8"?>
<svg xmlns="http://www.w3.org/2000/svg" width="24" height="24" viewBox="0 0 24 24">
  <g fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round">
    <path d="M5 4h11l3.5 3.5V20H5z"/>
    <path d="M8 4v4.5h7V4M8 20v-6h8v6"/>
  </g>
</svg>)SVG";

/// Location pin.
inline const char kLocationSvg[] = R"SVG(<?xml version="1.0" encoding="utf-8"?>
<svg xmlns="http://www.w3.org/2000/svg" width="24" height="24" viewBox="0 0 24 24">
  <g fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round">
    <path d="M12 21s-6.5-5.4-6.5-10a6.5 6.5 0 0 1 13 0c0 4.6-6.5 10-6.5 10z"/>
    <circle cx="12" cy="10.8" r="2.4"/>
  </g>
</svg>)SVG";

/// About (info circle).
inline const char kAboutSvg[] = R"SVG(<?xml version="1.0" encoding="utf-8"?>
<svg xmlns="http://www.w3.org/2000/svg" width="24" height="24" viewBox="0 0 24 24">
  <g fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round">
    <circle cx="12" cy="12" r="8.5"/>
    <line x1="12" y1="11" x2="12" y2="16.5"/>
    <circle cx="12" cy="7.8" r="0.4" fill="currentColor"/>
  </g>
</svg>)SVG";

/// Quit (door + arrow).
inline const char kExitSvg[] = R"SVG(<?xml version="1.0" encoding="utf-8"?>
<svg xmlns="http://www.w3.org/2000/svg" width="24" height="24" viewBox="0 0 24 24">
  <g fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
    <path d="M10 4.5H5.5v15H10M6 12h9M14.5 8.5L18 12l-3.5 3.5"/>
  </g>
</svg>)SVG";

}  // namespace johona::gui
