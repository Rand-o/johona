// mainwindow.hpp — main window shell (redesign mockup): title bar with
// hamburger menu (no QMenuBar), sidebar (brand + nav + scheduler status
// card), three pages in a QStackedWidget, slim status bar, system tray,
// appearance (Breeze palettes + mockup tokens), window-state persistence,
// date/TZ-change hooks.

#pragma once

#include <QDate>
#include <QLabel>
#include <QMenu>
#include <QMainWindow>
#include <QPalette>
#include <QPushButton>
#include <QStackedWidget>
#include <QSystemTrayIcon>
#include <QTimer>
#include <QToolButton>

#include "engine.hpp"
#include "location.hpp"
#include "migration.hpp"

namespace johona::gui {

class ThemesTab;
class SettingsTab;
class SchedulerTab;
class StatusDot;
class NavItem;
class StatusMessageLabel;

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
    void onRefreshStatus();

private:
    /// Engine-side facts for the sidebar/status bar (engine thread).
    struct StatusInfo {
        bool running = false;
        QString nextTime;   // "18:47" | "—"
        QString nextLabel;  // "Sunset" | ""
        QString backend;    // "Plasma" | "none"
    };

    void setupUi();
    void setupTray();
    void applyAppearance();
    void updateTrayIcon();
    /// Tray hover text: "Johona — <pretty theme name>" (plain app name
    /// when no theme has been applied).
    void updateTrayTooltip(const QString& themeDisplayName);
    void updateTitleBarIcon();
    void updateSchedulerUi(bool running);
    void applyStatusInfo(const StatusInfo& info);
    void refreshStatusInfo();
    void startScheduler();
    void stopScheduler();
    void showPage(int index);

    Engine* m_engine;
    location::LocationManager* m_location;
    migration::Report m_report;

    // Title bar
    QToolButton* m_menuBtn = nullptr;
    QMenu* m_menu = nullptr;
    QAction* m_menuToggle = nullptr;

    // Sidebar
    NavItem* m_navThemes = nullptr;
    NavItem* m_navScheduler = nullptr;
    NavItem* m_navSettings = nullptr;
    StatusDot* m_sideDot = nullptr;
    QLabel* m_sideTitle = nullptr;
    QLabel* m_sideSub = nullptr;
    QPushButton* m_sideToggle = nullptr;

    // Pages
    QStackedWidget* m_pages = nullptr;
    ThemesTab* m_themesTab = nullptr;
    SettingsTab* m_settingsTab = nullptr;
    SchedulerTab* m_schedulerTab = nullptr;

    // Status bar
    StatusMessageLabel* m_statusMsg = nullptr;
    QLabel* m_sbNext = nullptr;
    QLabel* m_sbBackend = nullptr;

    QSystemTrayIcon* m_tray = nullptr;
    QAction* m_trayStatus = nullptr;
    QAction* m_trayToggle = nullptr;
    QAction* m_trayNext = nullptr;

    QPalette m_systemPalette;  // snapshot taken before any scheme is applied

    QDate m_lastDate;
    QTimer m_dateTimer;   // 30 s: date-change hook (schedule preview)
    QTimer m_tzTimer;     // 5 min: opt-in TZ-change hook
    QTimer m_statusTimer; // 60 s: next-change info (sidebar/status bar)
    StatusInfo m_statusInfo;
};

}  // namespace johona::gui
