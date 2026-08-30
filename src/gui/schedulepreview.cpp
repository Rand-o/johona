// schedulepreview.cpp — see schedulepreview.hpp (redesign mockup timeline).

#include "schedulepreview.hpp"

#include <algorithm>
#include <QDateTime>
#include <QFileInfo>
#include <QFontMetrics>
#include <QImageReader>
#include <QHBoxLayout>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QPen>
#include <QPointer>
#include <QThreadPool>
#include <QVBoxLayout>

#include "imageworkers.hpp"
#include "style.hpp"
#include "themes.hpp"

namespace johona::gui {

namespace {

// Geometry (px) — mockup .timeline: 54 px tall, 14 px ruler, bands at
// top 17 px with 26 px height.
constexpr int kHeadH = 20;
constexpr int kRulerH = 14;
constexpr int kBandTop = 17;
constexpr int kBandH = 26;
constexpr int kBarH = 54;
constexpr int kSpacing = 6;
constexpr int kFootH = 16;
constexpr int kWidgetH = kHeadH + kSpacing + kBarH + kSpacing + kFootH;  // 102

constexpr int kThumbPx = 16;
constexpr double kThumbRadius = 3.5;
constexpr int kThumbCachePx = kThumbPx * 4;  // 4× headroom for HiDPI
constexpr int kTickMs = 60000;

// (band fill, band border, legend fill, legend border) — mockup tints.
struct SegColor {
    QColor bandFill;
    QColor bandBorder;
    QColor legendFill;
    QColor legendBorder;
};
const SegColor& segColor(const QString& type) {
    static const QHash<QString, SegColor> colors = {
        {"night",
         {QColor(0x55, 0x66, 0x88, 38), QColor(0x55, 0x66, 0x88, 128),
          QColor(0x55, 0x66, 0x88, 77), QColor(0x55, 0x66, 0x88, 166)}},
        {"sunrise",
         {QColor(0xF5, 0xC2, 0x6B, 46), QColor(0xF5, 0xC2, 0x6B, 166),
          QColor(0xF5, 0xC2, 0x6B, 89), QColor(0xF5, 0xC2, 0x6B, 204)}},
        {"day",
         {QColor(0x7E, 0xC8, 0xF0, 38), QColor(0x7E, 0xC8, 0xF0, 153),
          QColor(0x7E, 0xC8, 0xF0, 77), QColor(0x7E, 0xC8, 0xF0, 179)}},
        {"sunset",
         {QColor(0xF0, 0x95, 0x5A, 46), QColor(0xF0, 0x95, 0x5A, 166),
          QColor(0xF0, 0x95, 0x5A, 89), QColor(0xF0, 0x95, 0x5A, 204)}},
    };
    static const SegColor neutral{
        QColor(0x7E, 0xC8, 0xF0, 38), QColor(0x7E, 0xC8, 0xF0, 153),
        QColor(0x7E, 0xC8, 0xF0, 77), QColor(0x7E, 0xC8, 0xF0, 179)};
    auto it = colors.constFind(type);
    return it == colors.constEnd() ? neutral : it.value();
}

QString wallClock(double epochMs, const QTimeZone& tz) {
    return QDateTime::fromMSecsSinceEpoch(epochMs, tz).time().toString("HH:mm");
}

const std::vector<int>& listFor(solar::Category c,
                                const solar::ThemeImageLists& l) {
    static const std::vector<int> empty;
    switch (c) {
    case solar::Category::Sunrise: return l.sunrise;
    case solar::Category::Day: return l.day;
    case solar::Category::Sunset: return l.sunset;
    case solar::Category::Night: return l.night;
    }
    return empty;
}

const char* segmentName(solar::Category c) {
    switch (c) {
    case solar::Category::Sunrise: return "sunrise";
    case solar::Category::Day: return "day";
    case solar::Category::Sunset: return "sunset";
    case solar::Category::Night: return "night";
    }
    return "day";
}

struct ComputeResult {
    SchedulePreview::ScheduleData data;
    QString error;
};

/// One image's (clamped) display window on the day bar, for today's and
/// yesterday's segments (kWallpaper day_windows parity): each effective
/// window is divided equally among its images, windows are clamped to
/// [day 00:00, day+1 00:00), sorted by (start, image).
ComputeResult computeSchedule(const config::Config& cfg,
                              const QString& themeDir, double nowMs) {
    ComputeResult r;
    const QByteArray tzId = cfg.timezone.toUtf8();
    QTimeZone tz(tzId.constData());
    if (!tz.isValid()) {
        r.error = QStringLiteral("Invalid timezone: %1").arg(cfg.timezone);
        return r;
    }
    auto dataOpt = themes::loadThemeData(themeDir);
    if (!dataOpt) {
        r.error = QStringLiteral("No theme.json in %1")
                      .arg(QFileInfo(themeDir).fileName());
        return r;
    }
    const solar::ThemeImageLists lists{dataOpt->sunriseImageList,
                                       dataOpt->dayImageList,
                                       dataOpt->sunsetImageList,
                                       dataOpt->nightImageList};

    r.data.tz = tz;
    r.data.nowMs = nowMs;
    r.data.day = QDateTime::fromMSecsSinceEpoch(nowMs, tz).date();
    r.data.segments =
        solar::segmentsForDay(r.data.day, tz, cfg.latitude, cfg.longitude);
    if (!r.data.segments.complete()) {
        r.error = QStringLiteral("Schedule unavailable (polar day)");
        return r;
    }
    r.data.dayStartMs =
        QDateTime(r.data.day, QTime(0, 0), tz).toMSecsSinceEpoch();
    r.data.dayEndMs =
        QDateTime(r.data.day.addDays(1), QTime(0, 0), tz).toMSecsSinceEpoch();

    auto collect = [&](const solar::Segments& seg) {
        for (const solar::Window& win : solar::effectiveWindows(seg, lists)) {
            const std::vector<int>& list = listFor(win.category, lists);
            if (list.empty() || win.end <= win.start)
                continue;
            const double dur =
                (win.end - win.start) / static_cast<double>(list.size());
            for (std::size_t i = 0; i < list.size(); i++) {
                const double s = win.start + dur * static_cast<double>(i);
                const double e = win.start + dur * (static_cast<double>(i) + 1.0);
                if (e <= r.data.dayStartMs || s >= r.data.dayEndMs)
                    continue;
                SchedulePreview::Entry ent;
                ent.startMs = std::max(s, r.data.dayStartMs);
                ent.endMs = std::min(e, r.data.dayEndMs);
                ent.imageValue = list[i];
                ent.listSize = static_cast<int>(list.size());
                ent.segment = segmentName(win.category);
                ent.path = themes::imageFileFor(themeDir, *dataOpt, list[i]);
                r.data.entries.push_back(std::move(ent));
            }
        }
    };
    collect(r.data.segments);
    const solar::Segments prev = solar::segmentsForDay(
        r.data.day.addDays(-1), tz, cfg.latitude, cfg.longitude);
    if (prev.complete())
        collect(prev);  // last night's images that run past midnight

    std::sort(r.data.entries.begin(), r.data.entries.end(),
              [](const SchedulePreview::Entry& a,
                 const SchedulePreview::Entry& b) {
                  if (a.startMs != b.startMs)
                      return a.startMs < b.startMs;
                  return a.imageValue < b.imageValue;
              });
    return r;
}

/// Decode one schedule thumbnail (long edge ≤ kThumbCachePx).  The
/// downscale happens inside the decoder (setScaledSize), so no
/// full-resolution buffer is ever allocated.  Worker.
QImage decodeScheduleThumb(const QString& path) {
    QImageReader reader(path);
    reader.setAutoTransform(true);
    const QSize sz = reader.size();
    if (sz.isValid()) {
        if (sz.width() >= sz.height())
            reader.setScaledSize(QSize(kThumbCachePx, -1));
        else
            reader.setScaledSize(QSize(-1, kThumbCachePx));
    }
    return reader.read();
}

// Legend geometry — shared by sizeHint() and paintEvent() so the widget is
// always allocated exactly as wide as its content (a fixed width clips the
// leftmost swatch when the font is wider than expected).
constexpr int kLegendSw = 10;
constexpr double kLegendSwRadius = 3;
constexpr int kLegendGap = 5;
constexpr int kLegendPad = 12;
const char* kLegendNames[4] = {"night", "sunrise", "day", "sunset"};
const char* kLegendLabels[4] = {"Night", "Sunrise", "Day", "Sunset"};

int legendWidth(const QFontMetrics& fm) {
    int total = 0;
    for (int i = 0; i < 4; i++)
        total += kLegendSw + kLegendGap + fm.horizontalAdvance(kLegendLabels[i]);
    return total + kLegendPad * 3 + 2;  // +2 px left-edge safety
}

/// "America/Phoenix · 33.4484°N, 112.0740°W" (mockup footer right).
QString locationText(const config::Config& cfg) {
    const QString lat =
        QString::number(qAbs(cfg.latitude), 'f', 4) +
        (cfg.latitude >= 0 ? QStringLiteral("°N") : QStringLiteral("°S"));
    const QString lon =
        QString::number(qAbs(cfg.longitude), 'f', 4) +
        (cfg.longitude >= 0 ? QStringLiteral("°E") : QStringLiteral("°W"));
    return QStringLiteral("%1 · %2, %3").arg(cfg.timezone, lat, lon);
}

}  // namespace

// ── Legend ───────────────────────────────────────────────────────────────

class ScheduleLegend : public QWidget {
public:
    explicit ScheduleLegend(QWidget* parent = nullptr) : QWidget(parent) {
        setFixedHeight(kHeadH);
    }

    QSize sizeHint() const override {
        QFont f;
        f.setPixelSize(11);  // 10.5 px mockup
        return QSize(legendWidth(QFontMetrics(f)), kHeadH);
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const QPalette pal = palette();
        QFont f;
        f.setPixelSize(11);
        p.setFont(f);
        const QFontMetrics fm(f);

        int widths[4];
        int total = 0;
        for (int i = 0; i < 4; i++) {
            widths[i] =
                kLegendSw + kLegendGap + fm.horizontalAdvance(kLegendLabels[i]);
            total += widths[i];
        }
        total += kLegendPad * 3;
        int x = width() - total;
        const int y = (height() - kLegendSw) / 2;
        for (int i = 0; i < 4; i++) {
            const SegColor c = segColor(kLegendNames[i]);
            QPainterPath path;
            path.addRoundedRect(QRectF(x, y, kLegendSw, kLegendSw),
                                kLegendSwRadius, kLegendSwRadius);
            p.fillPath(path, c.legendFill);
            p.setPen(QPen(c.legendBorder, 1));
            p.drawPath(path);
            x += kLegendSw + kLegendGap;
            p.setPen(pal.color(QPalette::PlaceholderText));
            p.drawText(x, y + kLegendSw - 2, kLegendLabels[i]);
            x += widths[i] - kLegendSw - kLegendGap + kLegendPad;
        }
    }
};

// ── Bar (ruler + segment bands + marker) ─────────────────────────────────

class ScheduleBar : public QWidget {
public:
    explicit ScheduleBar(SchedulePreview* owner)
        : QWidget(owner), m_owner(owner) {
        setFixedHeight(kBarH);
        setAutoFillBackground(false);
    }

protected:
    void mouseMoveEvent(QMouseEvent* event) override {
        m_owner->showEntryAt(event->position().toPoint().x());
        QWidget::mouseMoveEvent(event);
    }
    void leaveEvent(QEvent* event) override {
        m_owner->resetFooter();
        QWidget::leaveEvent(event);
    }

    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const int w = width(), h = height();
        const QPalette pal = palette();
        const auto& tok = style::current();
        const auto state = m_owner->m_state;

        const QRectF outer = QRectF(0.5, 0.5, w - 1, h - 1);
        QPainterPath bg;
        bg.addRoundedRect(outer, 6, 6);
        p.fillPath(bg, QColor(tok.frameBg));
        p.setPen(QPen(QColor(tok.midlight), 1));
        p.drawPath(bg);

        if (state != SchedulePreview::State::Ready) {
            QString msg;
            switch (state) {
            case SchedulePreview::State::Empty:
                msg = QStringLiteral("Select a theme to see its schedule");
                break;
            case SchedulePreview::State::Loading:
                msg = QStringLiteral("Computing schedule…");
                break;
            case SchedulePreview::State::Error:
                msg = QStringLiteral("Schedule unavailable");
                break;
            case SchedulePreview::State::Ready:
                break;
            }
            p.setClipPath(bg);
            p.setPen(pal.color(QPalette::PlaceholderText));
            QFont f;
            f.setPixelSize(11);
            p.setFont(f);
            p.drawText(rect(), Qt::AlignCenter, msg);
            return;
        }

        p.setClipPath(bg);

        const SchedulePreview::ScheduleData& sch = m_owner->m_data;
        const double span = sch.dayEndMs - sch.dayStartMs;
        auto xFor = [this, span](double ms) {
            return static_cast<int>((ms - m_owner->m_data.dayStartMs) /
                                    span * width());
        };

        const QColor midLight(tok.midlight);
        const QColor mid(tok.mid);

        // ── hour ruler ──────────────────────────────────────────────────
        p.setPen(QPen(midLight, 1));
        p.drawLine(0, kRulerH - 1, w, kRulerH - 1);
        QFont f;
        f.setPixelSize(9);
        p.setFont(f);
        for (int hour = 0; hour <= 24; hour++) {
            const int x = xFor(sch.dayStartMs + hour * 3600000.0);
            const bool major = hour % 3 == 0;
            p.setPen(QPen(major ? mid : midLight, 1));
            p.drawLine(x, kRulerH - 1 - (major ? 7 : 4), x, kRulerH - 1);
            if (major && hour < 24) {
                p.setPen(pal.color(QPalette::PlaceholderText));
                p.drawText(x + 3, kRulerH - 2,
                           QStringLiteral("%1").arg(hour, 2, 10, QChar('0')));
            }
        }

        // ── segment bands (grouped consecutive entries) ─────────────────
        struct Band {
            int first;
            int last;
        };
        QList<Band> bands;
        for (std::size_t i = 0; i < sch.entries.size(); i++) {
            if (bands.isEmpty() ||
                sch.entries[i].segment != sch.entries[bands.last().last].segment)
                bands.append({static_cast<int>(i), static_cast<int>(i)});
            else
                bands.last().last = static_cast<int>(i);
        }

        QFont labelFont;
        labelFont.setPixelSize(10);  // 9.5 px mockup
        const QFontMetrics labelFm(labelFont);

        for (const Band& b : bands) {
            const auto& e1 = sch.entries[b.first];
            const auto& e2 = sch.entries[b.last];
            const int x1 = xFor(e1.startMs);
            const int x2 = xFor(e2.endMs);
            if (x2 - x1 < 4)
                continue;
            const QRectF bandR(x1, kBandTop, x2 - x1, kBandH);
            const SegColor c = segColor(e1.segment);
            QPainterPath path;
            path.addRoundedRect(bandR, 4, 4);
            p.fillPath(path, c.bandFill);
            p.setPen(QPen(c.bandBorder, 1));
            p.drawPath(path);

            // 16 px rounded thumbnails at each image's window.
            const int ty = kBandTop + (kBandH - kThumbPx) / 2;
            for (int j = b.first; j <= b.last; j++) {
                const auto& e = sch.entries[j];
                const int ex1 = xFor(e.startMs);
                const int ex2 = xFor(e.endMs);
                if (ex2 - ex1 < 2)
                    continue;
                const double cx = (ex1 + ex2) / 2.0;
                const int tx =
                    qBound<int>(x1 + 3, static_cast<int>(cx) - kThumbPx / 2,
                                x2 - 3 - kThumbPx);
                if (tx < x1 + 3 || tx + kThumbPx > x2 - 3)
                    continue;
                auto pmIt = m_owner->m_pixmaps.constFind(e.path);
                if (pmIt != m_owner->m_pixmaps.constEnd()) {
                    // "Cover" fill: scale to expand over the square and
                    // center-crop with the rounded clip.
                    const QPixmap scaled = pmIt.value().scaled(
                        kThumbPx, kThumbPx,
                        Qt::KeepAspectRatioByExpanding,
                        Qt::SmoothTransformation);
                    QPainterPath tp;
                    tp.addRoundedRect(QRectF(tx, ty, kThumbPx, kThumbPx),
                                      kThumbRadius, kThumbRadius);
                    p.save();
                    p.setClipPath(tp);
                    const int ox = tx + (kThumbPx - scaled.width()) / 2;
                    const int oy = ty + (kThumbPx - scaled.height()) / 2;
                    p.drawPixmap(ox, oy, scaled);
                    p.restore();
                    p.setPen(QPen(QColor(0, 0, 0, 102), 1));  // rgba(0,0,0,.4)
                    p.drawPath(tp);
                } else {
                    p.setPen(QPen(mid, 1));
                    p.drawRoundedRect(QRectF(tx, ty, kThumbPx, kThumbPx),
                                      kThumbRadius, kThumbRadius);
                }
                // Time range (only when there is room next to the chip).
                const QString range =
                    QStringLiteral("%1–%2")
                        .arg(wallClock(e.startMs, sch.tz),
                             wallClock(e.endMs, sch.tz));
                const int lw = labelFm.horizontalAdvance(range);
                if (tx + kThumbPx + 6 + lw <= x2 - 4) {
                    const QColor wt = pal.color(QPalette::WindowText);
                    p.setPen(QColor(wt.red(), wt.green(), wt.blue(), 217));
                    p.setFont(labelFont);
                    const QRectF textR(tx + kThumbPx + 6, kBandTop,
                                       x2 - (tx + kThumbPx + 6) - 4, kBandH);
                    p.drawText(textR, Qt::AlignVCenter | Qt::AlignLeft, range);
                }
            }
        }

        // ── current-time marker (2 px line + dot + time chip) ───────────
        if (m_owner->m_nowMs > 0.0) {
            const int mx = xFor(m_owner->m_nowMs);
            const QColor hl = pal.color(QPalette::Highlight);
            p.setPen(QPen(hl, 2));
            p.drawLine(mx, 0, mx, h);
            p.setPen(QPen(pal.color(QPalette::Base), 2));
            p.setBrush(hl);
            p.drawEllipse(QPointF(mx, 7), 4, 4);
            const QString label = wallClock(m_owner->m_nowMs, sch.tz);
            QFont cf;
            cf.setPixelSize(9);
            cf.setBold(true);
            p.setFont(cf);
            const int tw = QFontMetrics(cf).horizontalAdvance(label) + 10;
            const int cx = (mx + 7 + tw < w) ? mx + 7 : mx - 7 - tw;
            const QRectF chip(cx, 1, tw, 13);
            QPainterPath cp;
            cp.addRoundedRect(chip, 3, 3);
            p.fillPath(cp, hl);
            p.setPen(pal.color(QPalette::HighlightedText));
            p.drawText(chip, Qt::AlignCenter, label);
        }
    }

private:
    SchedulePreview* m_owner;
};

// ── SchedulePreview ──────────────────────────────────────────────────────

SchedulePreview::SchedulePreview(QWidget* parent)
    : QWidget(parent), m_timer(this) {
    setFixedHeight(kWidgetH);
    setMinimumWidth(400);

    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(kSpacing);

    auto* head = new QHBoxLayout();
    head->setContentsMargins(0, 0, 0, 0);
    head->setSpacing(8);
    auto* title = new QLabel(QStringLiteral("Today's schedule"), this);
    QFont tf = title->font();
    tf.setPixelSize(13);  // 12.5 px mockup
    tf.setWeight(QFont::Bold);
    title->setFont(tf);
    head->addWidget(title);
    head->addStretch(1);
    m_legend = new ScheduleLegend(this);
    head->addWidget(m_legend);
    lay->addLayout(head);

    m_bar = new ScheduleBar(this);
    lay->addWidget(m_bar);

    auto* foot = new QHBoxLayout();
    foot->setContentsMargins(0, 0, 0, 0);
    foot->setSpacing(10);
    m_footNow = new QLabel(QString(), this);
    m_footLoc = new QLabel(QString(), this);
    QFont ff;
    ff.setPixelSize(11);
    m_footNow->setFont(ff);
    m_footLoc->setFont(ff);
    QPalette fpal = m_footNow->palette();
    fpal.setColor(QPalette::WindowText, fpal.color(QPalette::PlaceholderText));
    m_footNow->setPalette(fpal);
    m_footLoc->setPalette(fpal);
    m_footLoc->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    foot->addWidget(m_footNow, 1);
    foot->addWidget(m_footLoc);
    lay->addLayout(foot);

    m_timer.setInterval(kTickMs);
    connect(&m_timer, &QTimer::timeout, this, &SchedulePreview::onTick);
    m_timer.start();
}

QSize SchedulePreview::sizeHint() const {
    return QSize(800, kWidgetH);
}

void SchedulePreview::refresh(const config::Config& cfg,
                              const QString& themeDir) {
    m_cfg = cfg;
    m_themeDir = themeDir;
    ++m_token;
    m_state = State::Loading;
    m_data = ScheduleData{};
    m_nowMs = 0.0;
    m_pixmaps.clear();
    m_footNow->setText(QString());
    m_footLoc->setText(locationText(cfg));
    m_bar->update();

    const int token = m_token;
    const double nowMs = QDateTime::currentMSecsSinceEpoch();
    QPointer<SchedulePreview> guard = this;
    imageDecodePool()->start(
        [guard, token, cfg, themeDir, nowMs]() {
            const ComputeResult res = computeSchedule(cfg, themeDir, nowMs);
            if (!guard)
                return;
            QMetaObject::invokeMethod(
                guard.data(),
                [guard, token, data = std::move(res.data),
                 error = std::move(res.error)]() mutable {
                    if (guard)
                        guard->onScheduleReady(token, std::move(data),
                                               std::move(error));
                },
                Qt::QueuedConnection);
        });
}

void SchedulePreview::refreshNow() {
    if (m_state != State::Ready)
        return;
    m_nowMs = QDateTime::currentMSecsSinceEpoch();
    updateFooter();
    m_bar->update();
}

void SchedulePreview::clear() {
    ++m_token;
    m_state = State::Empty;
    m_data = ScheduleData{};
    m_nowMs = 0.0;
    m_pixmaps.clear();
    m_themeDir.clear();
    m_footNow->setText(QString());
    m_footLoc->setText(QString());
    m_bar->setToolTip(QString());
    m_bar->update();
}

void SchedulePreview::onTick() {
    if (m_state != State::Ready)
        return;
    const double now = QDateTime::currentMSecsSinceEpoch();
    const QDate nowDate =
        QDateTime::fromMSecsSinceEpoch(now, m_data.tz).date();
    if (nowDate != m_data.day) {
        // Calendar date changed (midnight, or DST day): recompute.
        if (!m_themeDir.isEmpty())
            refresh(m_cfg, m_themeDir);
        return;
    }
    m_nowMs = now;
    updateFooter();
    m_bar->update();
}

void SchedulePreview::onScheduleReady(int token, ScheduleData data,
                                      QString error) {
    if (token != m_token)
        return;  // superseded
    if (!error.isEmpty()) {
        m_state = State::Error;
        m_data = ScheduleData{};
        m_footNow->setText(QString());
        m_footLoc->setText(locationText(m_cfg));
        m_bar->setToolTip(error);
        m_bar->update();
        return;
    }
    m_data = std::move(data);
    m_nowMs = m_data.nowMs;
    m_state = State::Ready;
    m_bar->setToolTip(QString());

    // Thumbnails for all entries (dedup, keep order) — one worker.
    QStringList paths;
    for (const auto& e : m_data.entries)
        if (!e.path.isEmpty() && !paths.contains(e.path))
            paths << e.path;
    if (!paths.isEmpty()) {
        const int thumbToken = m_token;
        QPointer<SchedulePreview> guard = this;
        imageDecodePool()->start(
            [guard, thumbToken, paths]() {
                QHash<QString, QImage> thumbs;
                for (const QString& p : paths)
                    thumbs.insert(p, decodeScheduleThumb(p));
                if (!guard)
                    return;
                QMetaObject::invokeMethod(
                    guard.data(),
                    [guard, thumbToken, thumbs = std::move(thumbs)]() mutable {
                        if (guard)
                            guard->onThumbsReady(thumbToken,
                                                 std::move(thumbs));
                    },
                    Qt::QueuedConnection);
            });
    }
    updateFooter();
    m_bar->update();
}

void SchedulePreview::onThumbsReady(int token, QHash<QString, QImage> thumbs) {
    if (token != m_token)
        return;  // superseded
    for (auto it = thumbs.constBegin(); it != thumbs.constEnd(); ++it) {
        const QPixmap pm = QPixmap::fromImage(it.value());
        if (!pm.isNull())
            m_pixmaps.insert(it.key(), pm);
    }
    m_bar->update();
}

QString SchedulePreview::entryText(const Entry& e) const {
    QString text = QStringLiteral("%1–%2  ·  image %3 of %4")
                       .arg(wallClock(e.startMs, m_data.tz))
                       .arg(wallClock(e.endMs, m_data.tz))
                       .arg(e.imageValue)
                       .arg(e.listSize);
    if (!e.path.isEmpty())
        text += QStringLiteral("  ·  %1").arg(QFileInfo(e.path).fileName());
    return text;
}

void SchedulePreview::updateFooter() {
    if (m_state != State::Ready) {
        m_footNow->setText(QString());
        return;
    }
    auto e = entryAt(static_cast<int>(xFor(m_nowMs)));
    m_footNow->setText(e ? QStringLiteral("Now: %1").arg(entryText(*e))
                         : QString());
}

void SchedulePreview::showEntryAt(int x) {
    if (m_state != State::Ready)
        return;
    auto e = entryAt(x);
    if (e) {
        const QString text = entryText(*e);
        m_footNow->setText(text);
        m_bar->setToolTip(text);
    } else {
        resetFooter();
    }
}

void SchedulePreview::resetFooter() {
    m_bar->setToolTip(QString());
    updateFooter();
}

double SchedulePreview::xFor(double epochMs) const {
    const double span = m_data.dayEndMs - m_data.dayStartMs;
    if (span <= 0)
        return 0.0;
    return (epochMs - m_data.dayStartMs) / span * width();
}

std::optional<SchedulePreview::Entry> SchedulePreview::entryAt(int x) const {
    if (m_state != State::Ready)
        return std::nullopt;
    const double span = m_data.dayEndMs - m_data.dayStartMs;
    const double t = m_data.dayStartMs +
                     x / static_cast<double>(qMax(width(), 1)) * span;
    for (const auto& e : m_data.entries)
        if (e.startMs <= t && t < e.endMs)
            return e;
    return std::nullopt;
}

}  // namespace johona::gui
