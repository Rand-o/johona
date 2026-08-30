// previewwidget.cpp — see previewwidget.hpp (kWallpaper cross-fade parity,
// restyled to the redesign mockup: dark rounded letterbox + overlay chip).

#include "previewwidget.hpp"

#include "imageworkers.hpp"
#include "style.hpp"

#include <QHideEvent>
#include <QImageReader>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QPen>
#include <QPointer>
#include <QShowEvent>
#include <QThreadPool>

namespace johona::gui {

namespace {

/// Decode `path` and scale its long edge down to `targetLongEdge`
/// (smaller images are kept as-is).  Runs on a worker thread.
///
/// The downscale happens inside the decoder (setScaledSize, verified
/// working on this Qt 6.9.3, including the -1 aspect-ratio sentinel), so
/// no full-resolution buffer is ever allocated.
QImage decodeThumb(const QString& path, int targetLongEdge) {
    QImageReader reader(path);
    reader.setAutoTransform(true);
    const QSize sz = reader.size();
    if (sz.isValid()) {
        if (sz.width() >= sz.height())
            reader.setScaledSize(QSize(targetLongEdge, -1));
        else
            reader.setScaledSize(QSize(-1, targetLongEdge));
    }
    return reader.read();
}

// Mockup .preview constants.
constexpr int kRadius = 8;
const QColor kLetterbox("#0a0d14");

}  // namespace

PreviewWidget::PreviewWidget(QWidget* parent)
    : QWidget(parent), m_anim(this, "blendValue", this), m_timer(this) {
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setMinimumSize(320, 140);

    m_anim.setDuration(1200);  // 1.2 s cross-fade
    m_anim.setEasingCurve(QEasingCurve::InOutQuad);
    connect(&m_anim, &QPropertyAnimation::finished, this,
            &PreviewWidget::onFadeDone);

    m_timer.setInterval(2700);  // 1.5 s hold + 1.2 s fade = 2.7 s per frame
    connect(&m_timer, &QTimer::timeout, this, &PreviewWidget::advance);
}

QSize PreviewWidget::sizeHint() const { return QSize(640, 360); }

void PreviewWidget::setImages(const QStringList& paths) {
    m_timer.stop();
    m_anim.stop();
    ++m_token;  // cancel in-flight loads from the old list
    m_paths = paths;
    m_idx = 0;
    m_blend = 0.0;
    m_raw.clear();
    m_lru.clear();
    m_rawBytes = 0;
    m_scaled.clear();
    m_loading.clear();
    if (!m_paths.isEmpty())
        requestEager();
    update();
}

void PreviewWidget::setThemeName(const QString& name) {
    if (m_name == name)
        return;
    m_name = name;
    update();
}

void PreviewWidget::clear() {
    m_running = false;
    m_name.clear();
    setImages({});
}

void PreviewWidget::start() {
    m_running = true;
    if (m_paths.size() > 1)
        m_timer.start();
}

void PreviewWidget::stop() {
    m_running = false;
    m_timer.stop();
    m_anim.stop();
}

void PreviewWidget::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    if (!m_paths.isEmpty())
        requestEager();
    if (m_running && m_paths.size() > 1)
        m_timer.start();
}

void PreviewWidget::hideEvent(QHideEvent* event) {
    // Free the decoded caches while invisible (the biggest memory use);
    // showEvent re-primes from disk in well under a second.
    m_anim.stop();
    m_timer.stop();
    ++m_token;
    m_raw.clear();
    m_lru.clear();
    m_rawBytes = 0;
    m_scaled.clear();
    m_loading.clear();
    m_blend = 0.0;
    QWidget::hideEvent(event);
}

int PreviewWidget::desiredThumbSize() const {
    // Adaptive long-edge: max(960, 1.0× the PHYSICAL widget long-edge),
    // capped at 2160.  Before the first layout the 960 floor applies.
    double dpr = devicePixelRatioF();
    if (dpr <= 0)
        dpr = 1.0;
    const int physLongEdge =
        static_cast<int>(qMax(width(), height()) * dpr);
    return qMax(kThumbMin, qMin(kThumbMax, physLongEdge));
}

void PreviewWidget::request(int idx) {
    if (idx < 0 || idx >= m_paths.size())
        return;
    if (m_scaled.contains(idx) || m_loading.contains(idx))
        return;
    m_loading.insert(idx);
    const int token = m_token;
    const QString path = m_paths[idx];
    const int target = desiredThumbSize();
    QPointer<PreviewWidget> guard = this;
    imageDecodePool()->start(
        [guard, token, idx, path, target]() {
            QImage img = decodeThumb(path, target);
            if (!guard)
                return;
            QMetaObject::invokeMethod(
                guard.data(),
                [guard, token, idx, img = std::move(img)]() mutable {
                    if (guard)
                        guard->onThumbReady(token, idx, std::move(img));
                },
                Qt::QueuedConnection);
        });
}

void PreviewWidget::requestEager() {
    const int n = m_paths.size();
    if (n == 0)
        return;
    for (int off = 0; off <= kEagerAhead; off++)
        request((m_idx + off) % n);
}

QSet<int> PreviewWidget::keepSet() const {
    const int n = m_paths.size();
    if (n == 0)
        return {};
    return {m_idx, (m_idx + 1) % n};
}

qint64 PreviewWidget::imageBytes(const QImage& img) {
    return img.sizeInBytes();  // total decoded bytes (w * h * bytesPerPixel)
}

void PreviewWidget::onThumbReady(int token, int idx, QImage img) {
    if (token != m_token)
        return;  // superseded (theme switched)
    m_loading.remove(idx);
    if (img.isNull())
        return;
    // LRU insert: move to the end, evict oldest beyond the byte budget.
    if (m_raw.contains(idx)) {
        m_rawBytes -= imageBytes(m_raw[idx]);
        m_lru.removeOne(idx);
    }
    m_raw.insert(idx, img);
    m_lru.append(idx);
    m_rawBytes += imageBytes(img);
    while (m_rawBytes > kMaxCacheBytes && m_lru.size() > 1) {
        const int oldest = m_lru.takeFirst();
        m_rawBytes -= imageBytes(m_raw[oldest]);
        m_raw.remove(oldest);
    }
    if (keepSet().contains(idx))
        m_scaled.insert(idx, scaleToWidget(img));
    update();
}

QPixmap PreviewWidget::scaleToWidget(const QImage& img) const {
    // Scale to the widget's PHYSICAL pixel size (HiDPI-correct): the result
    // is always a pure downsample of the (oversampled) thumbnail.
    double dpr = devicePixelRatioF();
    if (dpr <= 0)
        dpr = 1.0;
    const int pw = static_cast<int>(width() * dpr);
    const int ph = static_cast<int>(height() * dpr);
    if (pw <= 0 || ph <= 0)
        return QPixmap();
    QPixmap pm = QPixmap::fromImage(img);
    QPixmap out = pm.scaled(pw, ph, Qt::KeepAspectRatio,
                            Qt::SmoothTransformation);
    if (qFuzzyCompare(dpr, 1.0) == false)
        out.setDevicePixelRatio(dpr);
    return out;
}

QPixmap PreviewWidget::scaledFor(int idx) {
    if (idx < 0 || idx >= m_paths.size())
        return QPixmap();
    auto it = m_scaled.constFind(idx);
    if (it != m_scaled.constEnd())
        return it.value();
    auto rawIt = m_raw.constFind(idx);
    if (rawIt != m_raw.constEnd()) {
        const QPixmap pm = scaleToWidget(rawIt.value());
        m_scaled.insert(idx, pm);
        return pm;
    }
    if (!m_loading.contains(idx))
        request(idx);  // evicted or never started: restart the pipeline
    return QPixmap();
}

void PreviewWidget::pruneScaled() {
    const QSet<int> keep = keepSet();
    const QList<int> keys = m_scaled.keys();
    for (int idx : keys)
        if (!keep.contains(idx))
            m_scaled.remove(idx);
}

void PreviewWidget::advance() {
    const int n = m_paths.size();
    if (n < 2)
        return;
    const int nxt = (m_idx + 1) % n;
    request(nxt);
    requestEager();
    // Seamless transition: only fade when the next frame is already
    // decoded; otherwise swap instantly (fading to an empty background
    // would flash black).
    if (!scaledFor(nxt).isNull()) {
        m_anim.stop();
        m_anim.setStartValue(0.0);
        m_anim.setEndValue(1.0);
        m_anim.start();
    } else {
        m_idx = nxt;
        m_blend = 0.0;
        pruneScaled();
        update();
    }
}

void PreviewWidget::onFadeDone() {
    const int n = m_paths.size();
    if (n == 0)
        return;
    m_idx = (m_idx + 1) % n;
    m_blend = 0.0;
    pruneScaled();
    update();
}

void PreviewWidget::setBlendValue(double v) {
    m_blend = v;
    update();
}

void PreviewWidget::resizeEvent(QResizeEvent* event) {
    // Re-scale the keep set once, not per paint.
    const QSet<int> keep = keepSet();
    m_scaled.clear();
    for (int idx : std::as_const(keep)) {
        auto rawIt = m_raw.constFind(idx);
        if (rawIt != m_raw.constEnd())
            m_scaled.insert(idx, scaleToWidget(rawIt.value()));
    }
    QWidget::resizeEvent(event);

    // The widget grew: re-request at the higher resolution (a no-op
    // resize costs nothing — smaller thumbs are kept, downscaling is free).
    const int target = desiredThumbSize();
    if (target > m_thumbSize) {
        m_thumbSize = target;
        if (!m_paths.isEmpty()) {
            m_scaled.clear();
            requestEager();
        }
    }
}

void PreviewWidget::paintEvent(QPaintEvent* event) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const QPalette pal = palette();
    const QRectF outer = rect().adjusted(0.5, 0.5, -0.5, -0.5);
    const double radius = qMin<double>(kRadius, qMin(outer.width(), outer.height()) / 2);

    // Rounded clip: everything (letterbox, images, chip) stays inside.
    QPainterPath clip;
    clip.addRoundedRect(outer, radius, radius);
    painter.setClipPath(clip);

    const QColor frameOutline(style::current().frameOutline);

    // Dark letterbox background (mockup .preview).
    painter.fillRect(outer, kLetterbox);

    const int n = m_paths.size();
    const double blend = m_blend;

    if (n == 0) {
        painter.setPen(pal.color(QPalette::PlaceholderText));
        QFont f = painter.font();
        f.setPixelSize(12);
        painter.setFont(f);
        painter.drawText(outer, Qt::AlignCenter,
                         QStringLiteral("Select a theme to preview"));
        painter.setClipPath(QPainterPath());
        painter.setPen(QPen(frameOutline, 1));
        painter.drawRoundedRect(outer, radius, radius);
        return;
    }

    QPixmap cur = scaledFor(m_idx);
    if (cur.isNull()) {
        // Fast placeholder until the first background load arrives.
        painter.setPen(pal.color(QPalette::PlaceholderText));
        QFont f = painter.font();
        f.setPixelSize(12);
        painter.setFont(f);
        painter.drawText(outer, Qt::AlignCenter,
                         QStringLiteral("Loading preview…"));
        painter.setClipPath(QPainterPath());
        painter.setPen(QPen(frameOutline, 1));
        painter.drawRoundedRect(outer, radius, radius);
        return;
    }

    // The widget-sized pixmaps are HiDPI: their width()/height() are
    // physical pixels while rect() is logical — center with the logical size.
    double dpr = cur.devicePixelRatio();
    if (dpr <= 0)
        dpr = 1.0;
    const int cx = (width() - static_cast<int>(cur.width() / dpr)) / 2;
    const int cy = (height() - static_cast<int>(cur.height() / dpr)) / 2;
    painter.setOpacity(1.0 - blend);
    painter.drawPixmap(cx, cy, cur);

    if (blend > 0.001) {
        QPixmap nxt = scaledFor((m_idx + 1) % n);
        if (!nxt.isNull()) {
            double ndpr = nxt.devicePixelRatio();
            if (ndpr <= 0)
                ndpr = 1.0;
            const int nx = (width() - static_cast<int>(nxt.width() / ndpr)) / 2;
            const int ny =
                (height() - static_cast<int>(nxt.height() / ndpr)) / 2;
            painter.setOpacity(blend);
            painter.drawPixmap(nx, ny, nxt);
        }
    }
    painter.setOpacity(1.0);

    // ── bottom-left glassy overlay chip (mockup .pv-overlay) ────────────
    if (!m_name.isEmpty()) {
        QFont f = painter.font();
        f.setPixelSize(11);
        QFont fBold = f;
        fBold.setWeight(QFont::DemiBold);
        const QFontMetrics fm(f);
        const QFontMetrics fmB(fBold);
        const QString countText =
            QStringLiteral("%1 / %2").arg(currentIndex()).arg(n);
        const int padX = 11, padY = 5, gap = 9, barW = 44, barH = 3;
        const int textH = fm.height();
        const int chipW = padX * 2 + fmB.horizontalAdvance(m_name) + gap +
                          fm.horizontalAdvance(countText) + gap + barW;
        const int chipH = padY * 2 + textH;
        const int chipX = 10;
        const int chipY = height() - 10 - chipH;
        const QRectF chipR(chipX, chipY, chipW, chipH);

        QPainterPath chipPath;
        chipPath.addRoundedRect(chipR, 7, 7);
        painter.fillPath(chipPath, QColor(8, 12, 18, 148));  // rgba(8,12,18,.58)
        painter.setPen(QPen(QColor(255, 255, 255, 36), 1));  // rgba(255,255,255,.14)
        painter.drawPath(chipPath);

        const int ty = chipY + padY;
        painter.setPen(Qt::white);
        painter.setFont(fBold);
        painter.drawText(chipX + padX, ty + fm.ascent(), m_name);
        int tx = chipX + padX + fmB.horizontalAdvance(m_name) + gap;
        painter.setPen(QColor(255, 255, 255, 217));  // .85
        painter.setFont(f);
        painter.drawText(tx, ty + fm.ascent(), countText);
        tx += fm.horizontalAdvance(countText) + gap;

        // 44 px progress bar (blue fill).
        const QRectF barR(tx, chipY + chipH / 2.0 - barH / 2.0, barW, barH);
        QPainterPath barPath;
        barPath.addRoundedRect(barR, 2, 2);
        painter.fillPath(barPath, QColor(255, 255, 255, 71));  // .28
        const double frac =
            static_cast<double>(currentIndex()) / static_cast<double>(n);
        if (frac > 0.0) {
            const double fw = barW * frac;
            QPainterPath fillPath;
            fillPath.addRoundedRect(
                QRectF(barR.x(), barR.y(), qMax(fw, 4.0), barH), 2, 2);
            painter.fillPath(fillPath, QColor("#3daee9"));
        }
    }

    // 1 px frame-outline border (mockup .preview).
    painter.setClipPath(QPainterPath());
    painter.setPen(QPen(frameOutline, 1));
    painter.drawRoundedRect(outer, radius, radius);
}

}  // namespace johona::gui
