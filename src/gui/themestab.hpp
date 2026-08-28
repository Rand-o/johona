// themestab.hpp — Themes tab (spec §11.1): theme list, import/apply/delete,
// preview (cross-fade) and the 24 h schedule preview.

#pragma once

#include <QListWidget>
#include <QPushButton>
#include <QWidget>

#include "engine.hpp"

namespace johona::gui {

class PreviewWidget;
class SchedulePreview;

class ThemesTab : public QWidget {
    Q_OBJECT
public:
    ThemesTab(Engine* engine, QWidget* parent = nullptr);

    /// Reload the theme list (after import/delete/migration).
    void refresh();
    /// Rebuild the preview + schedule for the selected theme (after a
    /// settings save changes the location).
    void rebuildPreview();

signals:
    /// A theme was applied (for the status bar / scheduler tab).
    void themeApplied(const QString& themeName);
    /// Request a status-bar message.
    void statusMessage(const QString& message);

private slots:
    void onImport();
    void onApply();
    void onDelete();
    void onNext();
    void onSelectionChanged();
    void onApplied(const QString& themeName, const QString& imagePath,
                   const QString& category);

private:
    QString selectedThemePath() const;
    void setBusy(bool busy);
    void updatePreview();
    void rebuildSchedule();

    Engine* m_engine;
    QListWidget* m_list;
    QPushButton* m_importBtn;
    QPushButton* m_applyBtn;
    QPushButton* m_deleteBtn;
    QPushButton* m_nextBtn;
    PreviewWidget* m_preview;
    SchedulePreview* m_schedule;
};

}  // namespace johona::gui
