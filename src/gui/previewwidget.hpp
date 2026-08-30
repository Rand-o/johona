// previewwidget.hpp — kWallpaper ImageCrossFadeWidget parity: a full-theme
// slideshow preview (redesign mockup).
//
//  - 2.7 s per image (1.5 s hold + 1.2 s cross-fade, InOutQuad)
//  - KeepAspectRatio, centered; letterbox = dark #0a0d14 (mockup .preview)
//  - Rounded 8 px clip + 1 px frame-outline border (mockup .preview)
//  - Bottom-left glassy overlay chip: theme name (bold) · "n / N" counter
//    · 44 px progress bar (blue fill) — painted, no blur dependency
//  - Adaptive decode long-edge: max(960, min(2160, physical long edge))
//  - LRU decoded-image cache with a 48 MB byte budget
//  - Decodes on QThreadPool workers; a version token cancels in-flight
//    work when the theme changes
//  - hideEvent frees the caches; showEvent re-primes the eager set

#pragma once

#include <QHash>
#include <QImage>
#include <QList>
#include <QPixmap>
#include <QPropertyAnimation>
#include <QSet>
#include <QStringList>
#include <QTimer>
#include <QWidget>

namespace johona::gui {

class PreviewWidget : public QWidget {
    Q_OBJECT
    Q_PROPERTY(double blendValue READ blendValue WRITE setBlendValue)
public:
    explicit PreviewWidget(QWidget* parent = nullptr);

    /// Replace the slideshow with these image paths (natural order) and
    /// show the first.  Empty list → "Select a theme to preview".
    void setImages(const QStringList& paths);
    /// Overlay chip label (theme display name).
    void setThemeName(const QString& name);
    void clear();
    void start();
    void stop();
    bool isRunning() const { return m_running; }

    /// 1-based index of the image currently shown (for the overlay chip).
    int currentIndex() const { return m_idx + 1; }
    int count() const { return m_paths.size(); }

    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;

private:
    double blendValue() const { return m_blend; }
    void setBlendValue(double v);

    void advance();
    void onFadeDone();
    void request(int idx);
    void requestEager();
    int desiredThumbSize() const;
    QPixmap scaleToWidget(const QImage& img) const;
    QPixmap scaledFor(int idx);
    void pruneScaled();
    void onThumbReady(int token, int idx, QImage img);
    QSet<int> keepSet() const;
    static qint64 imageBytes(const QImage& img);

    QStringList m_paths;
    QString m_name;
    int m_idx = 0;
    double m_blend = 0.0;
    bool m_running = false;
    int m_token = 0;
    int m_thumbSize = 960;

    QHash<int, QImage> m_raw;      // image idx → decoded thumb (LRU)
    QList<int> m_lru;              // oldest first
    qint64 m_rawBytes = 0;
    QHash<int, QPixmap> m_scaled;  // idx → widget-sized (keep set only)
    QSet<int> m_loading;           // idx with an in-flight decode

    QPropertyAnimation m_anim;
    QTimer m_timer;

    static constexpr qint64 kMaxCacheBytes = 48 * 1024 * 1024;
    static constexpr int kEagerAhead = 2;
    static constexpr int kThumbMin = 960;
    static constexpr int kThumbMax = 2160;
};

}  // namespace johona::gui
