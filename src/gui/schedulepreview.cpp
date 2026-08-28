// schedulepreview.cpp — see schedulepreview.hpp.

#include "schedulepreview.hpp"

#include <QFuture>
#include <QPainter>
#include <QPainterPath>
#include <QtConcurrent/QtConcurrent>

namespace johona::gui {

namespace {
constexpr int kBarTop = 18;
constexpr int kBarH = 44;
}  // namespace

SchedulePreview::SchedulePreview(QWidget* parent) : QWidget(parent) {
    setMinimumHeight(kBarTop + kBarH + 24);
    setAutoFillBackground(true);
    QPalette pal = palette();
    pal.setColor(QPalette::Window, QColor(24, 26, 32));
    setPalette(pal);

    // 1-minute marker refresh (spec §11.1).
    m_tick.setInterval(1000);
    connect(&m_tick, &QTimer::timeout, this, [this]() {
        if (m_age.elapsed() >= 60000) {
            m_age.restart();
            setNow(QDateTime::currentMSecsSinceEpoch());
        }
    });
    m_tick.start();
    m_age.restart();
}

QSize SchedulePreview::sizeHint() const { return QSize(720, kBarTop + kBarH + 24); }

void SchedulePreview::setSchedule(const ScheduleData& data) {
    m_data = data;
    m_thumbs.clear();
    m_pending.clear();
    for (const auto& e : m_data.entries)
        if (!e.imagePath.isEmpty())
            requestThumb(e.imagePath);
    update();
}

void SchedulePreview::setNow(double epochMs) {
    m_nowMs = epochMs;
    m_hasNow = true;
    update();
}

void SchedulePreview::refresh() {
    setNow(QDateTime::currentMSecsSinceEpoch());
}

QColor SchedulePreview::colorFor(solar::Category c) const {
    switch (c) {
    case solar::Category::Sunrise:
        return QColor(248, 193, 86);   // dawn gold
    case solar::Category::Day:
        return QColor(96, 165, 250);   // day blue
    case solar::Category::Sunset:
        return QColor(249, 115, 22);   // sunset orange
    case solar::Category::Night:
        return QColor(59, 73, 123);    // night indigo
    }
    return QColor(128, 128, 128);
}

double SchedulePreview::xFor(double epochMs) const {
    if (m_data.dayEndMs <= m_data.dayStartMs)
        return 0.0;
    const double frac = (epochMs - m_data.dayStartMs) / (m_data.dayEndMs - m_data.dayStartMs);
    return std::max(0.0, std::min(1.0, frac)) * double(width());
}

void SchedulePreview::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const int barX = 4;
    const int barW = width() - 8;
    if (barW <= 0 || m_data.entries.empty())
        return;

    // Hour grid + labels.
    p.setPen(QColor(255, 255, 255, 40));
    QFont f = font();
    f.setPointSizeF(std::max(6.0, f.pointSizeF() - 2));
    p.setFont(f);
    for (int h = 0; h <= 24; h += 6) {
        const double t = m_data.dayStartMs + h * 3600000.0;
        const double x = barX + xFor(t);
        p.drawLine(QPointF(x, kBarTop - 4), QPointF(x, kBarTop + kBarH + 4));
        p.drawText(QRectF(x - 20, kBarTop + kBarH + 6, 40, 14),
                   Qt::AlignHCenter | Qt::AlignVCenter,
                   QString::number(h) + (h == 24 ? ":00" : "h"));
    }

    // Segment bands.
    for (const auto& e : m_data.entries) {
        const double x0 = barX + xFor(e.window.start);
        const double x1 = barX + xFor(e.window.end);
        if (x1 - x0 < 0.5)
            continue;
        QColor c = colorFor(e.window.category);
        p.setPen(Qt::NoPen);
        p.setBrush(c);
        p.drawRoundedRect(QRectF(x0 + 1, kBarTop, x1 - x0 - 2, kBarH), 3, 3);

        // Label when the band is wide enough.
        if (x1 - x0 > 56) {
            p.setPen(QColor(255, 255, 255, 220));
            p.setFont(font());
            p.drawText(QRectF(x0 + 4, kBarTop, x1 - x0 - 8, kBarH),
                       Qt::AlignCenter, solar::categoryName(e.window.category));
        }

        // Thumbnail at the window's midpoint.
        const double cx = (x0 + x1) / 2.0;
        const double ty = kBarTop + (kBarH - kThumbH) / 2.0;
        const QRectF tr(cx - kThumbW / 2.0, ty, kThumbW, kThumbH);
        auto it = m_thumbs.constFind(e.imagePath);
        if (it != m_thumbs.constEnd() && !it.value().isNull()) {
            const QImage img = it.value();
            const QSize scaled =
                img.size().scaled(QSize(kThumbW, kThumbH), Qt::KeepAspectRatioByExpanding);
            QImage cropped = img.scaled(scaled, Qt::IgnoreAspectRatio,
                                        Qt::SmoothTransformation);
            const int ox = (cropped.width() - kThumbW) / 2;
            const int oy = (cropped.height() - kThumbH) / 2;
            cropped = cropped.copy(ox, oy, kThumbW, kThumbH);
            p.setPen(QPen(QColor(0, 0, 0, 140), 1));
            p.drawRoundedRect(tr, 3, 3);
            p.drawImage(tr.toAlignedRect(), cropped);
        } else {
            p.setPen(QPen(QColor(255, 255, 255, 70), 1, Qt::DashLine));
            p.setBrush(QColor(255, 255, 255, 18));
            p.drawRoundedRect(tr, 3, 3);
        }
    }

    // Current-time marker.
    if (m_hasNow && m_nowMs >= m_data.dayStartMs && m_nowMs <= m_data.dayEndMs) {
        const double x = barX + xFor(m_nowMs);
        p.setPen(QPen(QColor(239, 68, 68), 2));
        p.drawLine(QPointF(x, kBarTop - 6), QPointF(x, kBarTop + kBarH + 6));
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(239, 68, 68));
        QPainterPath tri;
        tri.moveTo(x - 5, kBarTop - 6);
        tri.lineTo(x + 5, kBarTop - 6);
        tri.lineTo(x, kBarTop + 2);
        tri.closeSubpath();
        p.drawPath(tri);
    }
}

void SchedulePreview::requestThumb(const QString& path) {
    if (m_thumbs.contains(path) || m_pending.contains(path))
        return;
    m_pending.append(path);
    // ~2× thumbnail size (spec §11.1).
    const QSize target(kThumbW * 2, kThumbH * 2);
    QFuture<QImage> fut = QtConcurrent::run([path, target]() -> QImage {
        QImage img(path);
        if (img.isNull())
            return img;
        if (img.width() > target.width() || img.height() > target.height())
            img = img.scaled(target, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        return img;
    });
    fut.then(this, [this, path](QImage img) {
        m_pending.removeAll(path);
        if (!img.isNull()) {
            m_thumbs.insert(path, img);
            update();
        }
    });
}

}  // namespace johona::gui
