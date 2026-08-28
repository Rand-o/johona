// schedulepreview.hpp — 24 h timeline of the day's wallpaper schedule
// (spec §11.1).
//
// Colored segment bands (sunrise/day/sunset/night), small cover-cropped
// thumbnails at each image's display time (in-memory, capped at ~2×
// thumbnail size), and a current-time marker driven by a 1-minute timer.
// Recomputed on theme selection, settings save, and date change.

#pragma once

#include <QColor>
#include <QElapsedTimer>
#include <QHash>
#include <QImage>
#include <QStringList>
#include <QTimer>
#include <QWidget>

#include <vector>

#include "solar.hpp"

namespace johona::gui {

class SchedulePreview : public QWidget {
    Q_OBJECT
public:
    struct Entry {
        solar::Window window;
        QString imagePath;  // file to display in this window
    };

    struct ScheduleData {
        QDate day;
        std::vector<Entry> entries;  // effective image windows, in order
        double dayStartMs = 0.0;     // local midnight (epoch ms)
        double dayEndMs = 0.0;       // next local midnight
    };

    explicit SchedulePreview(QWidget* parent = nullptr);

    /// Replace the timeline (redraws immediately).
    void setSchedule(const ScheduleData& data);
    /// Update the current-time marker.
    void setNow(double epochMs);
    /// Force a recompute of the marker (date-change hook).
    void refresh();

    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    void requestThumb(const QString& path);
    QColor colorFor(solar::Category c) const;
    double xFor(double epochMs) const;

    ScheduleData m_data;
    double m_nowMs = 0.0;
    bool m_hasNow = false;

    QHash<QString, QImage> m_thumbs;
    QStringList m_pending;

    QTimer m_tick;  // 1-minute marker refresh
    QElapsedTimer m_age;

    static constexpr int kThumbW = 56;
    static constexpr int kThumbH = 32;
};

}  // namespace johona::gui
