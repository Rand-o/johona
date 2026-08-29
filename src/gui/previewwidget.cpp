// previewwidget.cpp — see previewwidget.hpp (kWallpaper cross-fade parity).

#include "previewwidget.hpp"

#include <QHideEvent>
#include <QImageReader>
#include <QPainter>
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
/// NOTE: use scaledToWidth/scaledToHeight, not scaled(w, -1, …): the
/// -1 aspect-ratio sentinel is broken in the KDE SDK's Qt 6.9.3 build
/// (returns a null image for every input).
QImage decodeThumb(const QString& path, int targetLongEdge) {
    QImageReader reader(path);
    reader.setAutoTransform(true);
    QImage img = reader.read();
    if (img.isNull())
        return img;
    const int longEdge = qMax(img.width(), img.height());
    if (longEdge > targetLongEdge) {
        if (img.width() >= img.height())
            img = img.scaledToWidth(targetLongEdge, Qt::SmoothTransformation);
        else
            img = img.scaledToHeight(targetLongEdge, Qt::SmoothTransformation);
    }
    return img;
}

}  // namespace

PreviewWidget::PreviewWidget(QWidget* parent)
    : QWidget(parent), m_anim(this, "blendValue", this), m_timer(this) {
    setAutoFillBackground(true);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setMinimumSize(320, 200);

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

void PreviewWidget::clear() {
    m_running = false;
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
    QThreadPool::globalInstance()->start(
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
    const QPalette pal = palette();
    const QSize sz = size();

    const int n = m_paths.size();
    const double blend = m_blend;

    if (n == 0) {
        // Dashed placeholder.
        QPen pen(pal.color(QPalette::Mid));
        pen.setStyle(Qt::DashLine);
        pen.setWidth(2);
        painter.setPen(pen);
        painter.drawRoundedRect(rect().adjusted(8, 8, -8, -8), 8, 8);
        painter.setPen(pal.color(QPalette::PlaceholderText));
        painter.drawText(rect(), Qt::AlignCenter,
                         QStringLiteral("Select a theme to preview"));
        return;
    }

    QPixmap cur = scaledFor(m_idx);
    if (cur.isNull()) {
        // Fast placeholder until the first background load arrives.
        painter.fillRect(rect(), pal.color(QPalette::AlternateBase));
        painter.setPen(pal.color(QPalette::PlaceholderText));
        painter.drawText(rect(), Qt::AlignCenter,
                         QStringLiteral("Loading preview…"));
        return;
    }

    // The widget-sized pixmaps are HiDPI: their width()/height() are
    // physical pixels while sz is logical — center with the logical size.
    double dpr = cur.devicePixelRatio();
    if (dpr <= 0)
        dpr = 1.0;
    const int cx = (sz.width() - static_cast<int>(cur.width() / dpr)) / 2;
    const int cy = (sz.height() - static_cast<int>(cur.height() / dpr)) / 2;
    painter.setOpacity(1.0 - blend);
    painter.drawPixmap(cx, cy, cur);

    if (blend > 0.001) {
        QPixmap nxt = scaledFor((m_idx + 1) % n);
        if (!nxt.isNull()) {
            double ndpr = nxt.devicePixelRatio();
            if (ndpr <= 0)
                ndpr = 1.0;
            const int nx = (sz.width() - static_cast<int>(nxt.width() / ndpr)) / 2;
            const int ny =
                (sz.height() - static_cast<int>(nxt.height() / ndpr)) / 2;
            painter.setOpacity(blend);
            painter.drawPixmap(nx, ny, nxt);
        }
    }
}

}  // namespace johona::gui
