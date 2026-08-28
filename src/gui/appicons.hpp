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

}  // namespace johona::gui
