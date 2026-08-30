// themestab.hpp — Themes page (redesign mockup): header (title + count +
// pill search + Import), single-column theme card list (QListWidget +
// custom delegate, worker-pool 16:9 thumbnails), and the right panel
// (name/meta + delete/apply, cross-fade preview with overlay chip,
// "Today's schedule" card).

#pragma once

#include <QHash>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPixmap>
#include <QPushButton>
#include <QSet>
#include <QStringList>
#include <QWidget>

#include "config.hpp"
#include "engine.hpp"

namespace johona::gui {

class PreviewWidget;
class SchedulePreview;

class ThemesTab : public QWidget {
    Q_OBJECT
public:
    ThemesTab(Engine* engine, const config::Paths& paths,
              QWidget* parent = nullptr);

    /// Reload the theme list (after import/delete/migration).
    void refresh();
    /// Rebuild the preview + schedule for the selected theme (after a
    /// settings save changes the location).
    void rebuildPreview();
    /// Scheduler running state: Delete is disabled (with the red warning)
    /// while the scheduler runs (kWallpaper parity).
    void setSchedulerRunning(bool running);
    /// Start/stop the preview slideshow with page visibility.
    void setTabVisible(bool visible);

    /// Menu "Import Theme…" target.
    void onImport() { importTheme(); }

signals:
    /// A status-bar message request.
    void statusMessage(const QString& message);

private slots:
    void onApply();
    void onDelete();
    void onSelectionChanged();
    void onSearchTextChanged(const QString& text);

private:
    void importTheme();
    QString selectedThemePath() const;
    void setBusy(QPushButton* btn, bool busy);
    void refreshSchedule();
    QStringList imagesFor(const QString& themePath) const;
    void applyFilter();
    void updateEmptyState();
    void requestThumbs();
    void onThumbsReady(int token, QHash<QString, QPixmap> thumbs);
    bool eventFilter(QObject* obj, QEvent* event) override;

    Engine* m_engine;
    config::Paths m_paths;

    // Header
    QLabel* m_countLabel;
    QLineEdit* m_search;
    QPushButton* m_importBtn;

    // Card list
    QListWidget* m_list;
    QLabel* m_emptyLabel;
    QHash<QString, QPixmap> m_thumbs;  // theme path → 16:9 card thumb
    int m_thumbToken = 0;
    QSet<QString> m_thumbLoading;

    // Right panel
    QLabel* m_nameLabel;
    QLabel* m_metaLabel;
    QLabel* m_deleteWarning;
    QPushButton* m_deleteBtn;
    QPushButton* m_applyBtn;
    PreviewWidget* m_preview;
    SchedulePreview* m_schedule;

    mutable QHash<QString, QStringList> m_imageCache;
};

}  // namespace johona::gui
