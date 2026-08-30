// schedulepreview.hpp — 24-hour schedule timeline (redesign mockup).
//
//  Header: "Today's schedule" title + right-aligned segment legend
//          (4 rounded swatches, mockup tints)
//  Ruler:  minor ticks hourly, major every 3 h with 2-digit labels
//  Bands:  one rounded, tinted band per sun segment; 16 px rounded image
//          thumbnails at each image's display window ("HH:MM–HH:MM" where
//          it fits)
//  Marker: 2 px highlight line + white-rimmed dot + blue time chip
//  Footer: "Now: HH:MM–HH:MM · image N of M · file.jpg" left,
//          "Timezone · lat/lon" right
//
// All computation runs off the GUI thread (QThreadPool) with a version
// token for cancellation; a 60 s timer refreshes the marker and
// recomputes when the calendar date changes in the configured timezone.

#pragma once

#include <QDate>
#include <QHash>
#include <QLabel>
#include <QPixmap>
#include <QSet>
#include <QTimeZone>
#include <QTimer>
#include <QWidget>

#include <optional>
#include <vector>

#include "config.hpp"
#include "solar.hpp"

namespace johona::gui {

class ScheduleBar;     // painted timeline (schedulepreview.cpp)
class ScheduleLegend;  // right-aligned legend (schedulepreview.cpp)

class SchedulePreview : public QWidget {
    Q_OBJECT
public:
    struct Entry {
        double startMs = 0.0;  // clamped to the bar, epoch ms
        double endMs = 0.0;
        int imageValue = 0;    // 1-based position in the category list
        int listSize = 0;      // images in that category's list
        QString segment;       // "night"|"sunrise"|"day"|"sunset"
        QString path;          // "" → placeholder box
    };

    struct ScheduleData {
        QDate day;
        QTimeZone tz;
        double nowMs = 0.0;
        double dayStartMs = 0.0;  // local midnight (epoch ms)
        double dayEndMs = 0.0;    // next local midnight
        solar::Segments segments;  // today's (for segment coloring)
        std::vector<Entry> entries;
    };

    explicit SchedulePreview(QWidget* parent = nullptr);

    /// (Re)compute the schedule for this theme in a background worker.
    void refresh(const config::Config& cfg, const QString& themeDir);
    /// Move the current-time marker only (no recompute).
    void refreshNow();
    /// No theme selected.
    void clear();

    QSize sizeHint() const override;

private:
    friend class ScheduleBar;
    friend class ScheduleLegend;

    enum class State { Empty, Loading, Ready, Error };

    void onTick();
    void onScheduleReady(int token, ScheduleData data, QString error);
    void onThumbsReady(int token, QHash<QString, QImage> thumbs);
    void updateFooter();
    void showEntryAt(int x);
    void resetFooter();
    double xFor(double epochMs) const;
    std::optional<Entry> entryAt(int x) const;
    QString entryText(const Entry& e) const;

    config::Config m_cfg;
    QString m_themeDir;
    int m_token = 0;
    State m_state = State::Empty;
    ScheduleData m_data;
    double m_nowMs = 0.0;
    QHash<QString, QPixmap> m_pixmaps;  // path → ≤64 px thumbnail

    QTimer m_timer;  // 60 s: marker refresh + date-change check

    ScheduleLegend* m_legend = nullptr;
    ScheduleBar* m_bar = nullptr;
    QLabel* m_footNow = nullptr;   // left: "Now: …" (or hovered entry)
    QLabel* m_footLoc = nullptr;   // right: "Timezone · lat/lon"
};

}  // namespace johona::gui
