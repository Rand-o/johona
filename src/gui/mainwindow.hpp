// mainwindow.hpp — main window shell (spec §11): tabs, tray, appearance,
// status bar, single-instance activation, date/TZ-change hooks.

#pragma once

#include <QMainWindow>
#include <QTabWidget>
#include <QSystemTrayIcon>

#include "engine.hpp"
#include "location.hpp"
#include "migration.hpp"

namespace johona::gui {

class ThemesTab;
class SettingsTab;
class SchedulerTab;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    MainWindow(Engine* engine, location::LocationManager* locationManager,
               const migration::Report& report);

    /// Show + raise + activate (single-instance "activate" message, and
    /// tray Show).  Distinct from QWidget::activateWindow so hiding to the
    /// tray is undone.
    void showAndActivate();

protected:
    /// With a tray available, closing hides to the tray (Quit is in the
    /// tray menu); without one, the app quits.
    void closeEvent(QCloseEvent* event) override;

private slots:
    void onEngineStatus(const QString& message);
    void onEngineError(const QString& message);
    void onTabStatus(const QString& message);
    void onSchedulerToggled(bool running);
    void onSettingsSaved();
    void onDateCheck();
    void onTzCheck();
    void onTrayActivated(QSystemTrayIcon::ActivationReason reason);
    void onToggleScheduler();
    void onNextWallpaper();
    void onShowHide();

private:
    void applyAppearance();
    void updateTrayIcon();
    void setupTray();

    Engine* m_engine;
    location::LocationManager* m_location;

    QTabWidget* m_tabs;
    ThemesTab* m_themesTab;
    SettingsTab* m_settingsTab;
    SchedulerTab* m_schedulerTab;

    QSystemTrayIcon* m_tray = nullptr;
    QAction* m_trayToggleAction = nullptr;
    QAction* m_trayNextAction = nullptr;

    QDate m_lastDate;
    QTimer m_dateTimer;   // 30 s: date-change hook (schedule preview)
    QTimer m_tzTimer;     // 5 min: opt-in TZ-change hook
};

}  // namespace johona::gui
