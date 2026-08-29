// mainwindow.hpp — main window shell (kWallpaper WallpaperWindow parity):
// menu bar, three tabs, system tray, status bar, appearance (Breeze
// palettes), window-state persistence, date/TZ-change hooks.

#pragma once

#include <QMainWindow>
#include <QPalette>
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
    /// tray Show).
    void showAndActivate();

protected:
    /// With a tray available, closing hides to the tray (Quit is in the
    /// tray menu); without one, the app quits.
    void closeEvent(QCloseEvent* event) override;
    void showEvent(QShowEvent* event) override;

private slots:
    void onEngineStatus(const QString& message);
    void onEngineError(const QString& message);
    void onEngineLog(const QString& message);
    void onRunningChanged(bool running);
    void onTabStatus(const QString& message);
    void onSettingsSaved();
    void onSchemeChanged(const QString& mode);
    void onDateCheck();
    void onTzCheck();
    void onTrayActivated(QSystemTrayIcon::ActivationReason reason);
    void onToggleScheduler();
    void onNextWallpaper();
    void onImport();
    void onAbout();

private:
    void setupMenus();
    void setupTabs();
    void setupTray();
    void applyAppearance();
    void updateTrayIcon();
    void updateSchedulerUi(bool running);
    void startScheduler();
    void stopScheduler();

    Engine* m_engine;
    location::LocationManager* m_location;
    migration::Report m_report;

    QTabWidget* m_tabs;
    ThemesTab* m_themesTab;
    SettingsTab* m_settingsTab;
    SchedulerTab* m_schedulerTab;

    QAction* m_menuStart = nullptr;
    QAction* m_menuStop = nullptr;
    QSystemTrayIcon* m_tray = nullptr;
    QAction* m_trayStatus = nullptr;
    QAction* m_trayToggle = nullptr;
    QAction* m_trayNext = nullptr;

    QPalette m_systemPalette;  // snapshot taken before any scheme is applied

    QDate m_lastDate;
    QTimer m_dateTimer;  // 30 s: date-change hook (schedule preview)
    QTimer m_tzTimer;    // 5 min: opt-in TZ-change hook
};

}  // namespace johona::gui
