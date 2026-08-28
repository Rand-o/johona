// previewwidget.hpp — wallpaper preview with WDD's cross-fade technique
// (spec §11.1).
//
//  - In-memory QHash<path, QImage> cache; no disk thumbnail cache.
//  - Decodes happen on QThreadPool workers: the original image scaled
//    (high quality) to at most 2× the widget's device-pixel size — WDD
//    caps at full screen size; 2×-widget is visually identical here and
//    ~4× lighter on 4K displays.
//  - LRU byte cap (512 MB) as a backstop; the cache is cleared on theme
//    switch.
//  - Cross-fade: back + front image, 600 ms, sine ease-in-out
//    (sin((p−0.5)·π)/2 + 0.5 — WDD's exact curve), ~60 fps timer,
//    paintEvent with QPainter::setOpacity, cover-crop to the widget.

#pragma once

#include <QElapsedTimer>
#include <QHash>
#include <QImage>
#include <QQueue>
#include <QTimer>
#include <QWidget>

namespace johona::gui {

class PreviewWidget : public QWidget {
    Q_OBJECT
public:
    explicit PreviewWidget(QWidget* parent = nullptr);

    /// Show `images` (one per sun segment: sunrise, day, sunset, night —
    /// empty entries are fine).  The currently-selected segment is
    /// cross-faded in; `category` names it ("sunrise"/"day"/"sunset"/
    /// "night") and is reported via categoryChanged().
    void setImages(const QStringList& images, const QString& category = {});
    /// Clear the cache and the current images (theme switch).
    void clear();

    QSize sizeHint() const override;

signals:
    void categoryChanged(const QString& category);
    /// Emitted when a decode finishes (path, success).
    void imageReady(const QString& path, bool ok);

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    void startFade(const QImage& front);
    void requestDecode(const QString& path);
    QImage coverCrop(const QImage& img) const;
    void evictLru();

    QImage m_back;
    QImage m_front;
    double m_opacity = 1.0;
    QTimer m_fadeTimer;
    QElapsedTimer m_fadeTimerMs;
    static constexpr int kFadeMs = 600;

    QHash<QString, QImage> m_cache;
    QQueue<QString> m_lru;
    qint64 m_cacheBytes = 0;
    static constexpr qint64 kMaxCacheBytes = 512LL * 1024 * 1024;

    QString m_currentPath;
    QStringList m_pending;  // paths requested, not yet decoded
};

}  // namespace johona::gui
