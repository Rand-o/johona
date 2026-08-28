// previewwidget.cpp — see previewwidget.hpp.

#include "previewwidget.hpp"

#include <QFuture>
#include <QPainter>
#include <QtConcurrent/QtConcurrent>

#include <cmath>

namespace johona::gui {

namespace {

int categoryIndex(const QString& category) {
    if (category == QLatin1String("sunrise"))
        return 0;
    if (category == QLatin1String("day"))
        return 1;
    if (category == QLatin1String("sunset"))
        return 2;
    if (category == QLatin1String("night"))
        return 3;
    return -1;
}

/// WDD's exact cross-fade curve: sine ease-in-out.
double sineEaseInOut(double p) { return std::sin((p - 0.5) * M_PI) / 2.0 + 0.5; }

}  // namespace

PreviewWidget::PreviewWidget(QWidget* parent) : QWidget(parent) {
    setMinimumSize(320, 180);
    setAutoFillBackground(true);
    QPalette pal = palette();
    pal.setColor(QPalette::Window, QColor(24, 26, 32));
    setPalette(pal);

    // ~60 fps fade ticks over 600 ms (WDD's curve, spec §11.1).
    m_fadeTimer.setInterval(16);
    connect(&m_fadeTimer, &QTimer::timeout, this, [this]() {
        const double p =
            std::min(1.0, static_cast<double>(m_fadeTimerMs.elapsed()) / kFadeMs);
        m_opacity = sineEaseInOut(p);
        if (p >= 1.0) {
            m_fadeTimer.stop();
            m_back = QImage();
            m_opacity = 1.0;
        }
        update();
    });
}

void PreviewWidget::paintEvent(QPaintEvent*) {
    QPainter p(this);
    if (m_back.isNull())
        return;
    p.drawImage(rect(), coverCrop(m_back));
    if (!m_front.isNull() && m_opacity < 1.0) {
        p.setOpacity(m_opacity);
        p.drawImage(rect(), coverCrop(m_front));
        p.setOpacity(1.0);
    } else if (!m_front.isNull()) {
        p.drawImage(rect(), coverCrop(m_front));
    }
}

void PreviewWidget::resizeEvent(QResizeEvent*) {
    update();
}

QSize PreviewWidget::sizeHint() const { return QSize(640, 360); }

void PreviewWidget::setImages(const QStringList& images, const QString& category) {
    const int idx = categoryIndex(category);
    if (idx < 0 || idx >= images.size() || images[idx].isEmpty()) {
        if (category != m_currentPath)
            emit categoryChanged(category);
        return;
    }
    const QString path = images[idx];
    if (path == m_currentPath)
        return;
    m_currentPath = path;
    emit categoryChanged(category);
    requestDecode(path);
}

void PreviewWidget::clear() {
    m_fadeTimer.stop();
    m_cache.clear();
    m_lru.clear();
    m_cacheBytes = 0;
    m_pending.clear();
    m_back = QImage();
    m_front = QImage();
    m_opacity = 1.0;
    m_currentPath.clear();
    update();
}

void PreviewWidget::startFade(const QImage& front) {
    if (front.isNull())
        return;
    m_back = m_front.isNull() ? QImage() : m_front;
    m_front = front;
    m_opacity = m_back.isNull() ? 1.0 : 0.0;
    if (m_back.isNull()) {
        update();
        return;
    }
    // 600 ms, ~60 fps, WDD's sine curve.
    m_fadeTimerMs.restart();
    m_fadeTimer.start(16);
    update();
}

void PreviewWidget::requestDecode(const QString& path) {
    auto it = m_cache.constFind(path);
    if (it != m_cache.constEnd()) {
        startFade(it.value());
        emit imageReady(path, true);
        return;
    }
    if (m_pending.contains(path))
        return;
    m_pending.append(path);

    // Decode + downscale on a worker thread.  Target: at most 2× the
    // widget's device-pixel size (spec §11.1).
    const double dpr = devicePixelRatioF() > 0 ? devicePixelRatioF() : 1.0;
    QSize target(std::max(1, int(size().width() * dpr * 2)),
                 std::max(1, int(size().height() * dpr * 2)));

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
        if (img.isNull()) {
            emit imageReady(path, false);
            return;
        }
        // LRU insert.
        if (m_cache.contains(path)) {
            m_cacheBytes -= qint64(m_cache[path].sizeInBytes());
            m_lru.removeAll(path);
        }
        m_cache.insert(path, img);
        m_lru.enqueue(path);
        m_cacheBytes += qint64(img.sizeInBytes());
        evictLru();

        if (path == m_currentPath)
            startFade(img);
        emit imageReady(path, true);
    });
}

void PreviewWidget::evictLru() {
    while (m_cacheBytes > kMaxCacheBytes && !m_lru.isEmpty()) {
        const QString oldest = m_lru.dequeue();
        const auto it = m_cache.find(oldest);
        if (it == m_cache.constEnd())
            continue;
        m_cacheBytes -= qint64(it.value().sizeInBytes());
        m_cache.erase(it);
    }
}

QImage PreviewWidget::coverCrop(const QImage& img) const {
    const QSize s = size();
    if (img.isNull() || s.isEmpty())
        return img;
    // Scale to cover, then center-crop to the widget size.
    const QSize scaled = img.size().scaled(s, Qt::KeepAspectRatioByExpanding);
    QImage cropped =
        (scaled == img.size()) ? img : img.scaled(scaled, Qt::IgnoreAspectRatio,
                                                  Qt::SmoothTransformation);
    const int x = (cropped.width() - s.width()) / 2;
    const int y = (cropped.height() - s.height()) / 2;
    return cropped.copy(x, y, s.width(), s.height());
}

}  // namespace johona::gui
