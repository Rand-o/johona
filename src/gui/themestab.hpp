// themestab.hpp — Themes tab (kWallpaper ThemesPage parity): theme list,
// Import…/Apply/Delete, cross-fade preview slideshow, "N images" and the
// 24 h schedule preview.

#pragma once

#include <QHash>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QSplitter>
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
    /// Start/stop the preview slideshow with tab visibility.
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

private:
    void importTheme();
    QString selectedThemePath() const;
    void setBusy(QPushButton* btn, bool busy);
    void refreshSchedule();
    QStringList imagesFor(const QString& themePath) const;

    Engine* m_engine;
    config::Paths m_paths;

    QSplitter* m_split;
    QListWidget* m_list;
    QPushButton* m_importBtn;
    QPushButton* m_applyBtn;
    QPushButton* m_deleteBtn;
    QLabel* m_deleteWarning;
    PreviewWidget* m_preview;
    QLabel* m_previewInfo;
    SchedulePreview* m_schedule;

    mutable QHash<QString, QStringList> m_imageCache;
};

}  // namespace johona::gui
