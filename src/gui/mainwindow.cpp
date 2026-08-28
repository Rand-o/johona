// mainwindow.cpp — see mainwindow.hpp.

#include "mainwindow.hpp"

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QDateTime>
#include <QMenu>
#include <QStatusBar>
#include <QStyle>
#include <QStyleHints>
#include <QTimer>

#include "appicons.hpp"
#include "enginebridge.hpp"
#include "schedulertab.hpp"
#include "settstab.hpp"
#include "themestab.hpp"

namespace johona::gui {

MainWindow::MainWindow(Engine* engine, location::LocationManager* locationManager,
                       const migration::Report& report)
    : m_engine(engine), m_location(locationManager) {
    setWindowTitle(QStringLiteral("Johona Wallpaper"));
    setWindowIcon(svgIcon(kAppIconSvg));
    resize(980, 640);

    m_tabs = new QTabWidget(this);
    m_themesTab = new ThemesTab(m_engine, m_tabs);
    m_settingsTab = new SettingsTab(m_engine, m_location, m_tabs);
    m_schedulerTab = new SchedulerTab(m_engine, m_tabs);
    m_tabs->addTab(m_themesTab, tr("Themes"));
    m_tabs->addTab(m_settingsTab, tr("Settings"));
    m_tabs->addTab(m_schedulerTab, tr("Scheduler"));
    setCentralWidget(m_tabs);

    // ---- engine signals (cross-thread, auto-queued) ----------------------
    connect(m_engine, &Engine::statusChanged, this, &MainWindow::onEngineStatus);
    connect(m_engine, &Engine::errorOccurred, this, &MainWindow::onEngineError);
    connect(m_engine, &Engine::runningChanged, this, &MainWindow::onSchedulerToggled);

    connect(m_themesTab, &ThemesTab::statusMessage, this, &MainWindow::onTabStatus);
    connect(m_settingsTab, &SettingsTab::statusMessage, this, &MainWindow::onTabStatus);
    connect(m_schedulerTab, &SchedulerTab::statusMessage, this, &MainWindow::onTabStatus);
    connect(m_settingsTab, &SettingsTab::settingsSaved, this, &MainWindow::onSettingsSaved);

    // ---- periodic hooks ----------------------------------------------------
    m_lastDate = QDateTime::currentDateTime()
                     .toTimeZone(QTimeZone(m_engine->config().timezone.toUtf8()))
                     .date();
    m_dateTimer.setInterval(30000);
    connect(&m_dateTimer, &QTimer::timeout, this, &MainWindow::onDateCheck);
    m_dateTimer.start();

    m_tzTimer.setInterval(5 * 60 * 1000);
    connect(&m_tzTimer, &QTimer::timeout, this, &MainWindow::onTzCheck);
    m_tzTimer.start();

    setupTray();
    applyAppearance();

    if (report.ran)
        statusBar()->showMessage(tr("Migrated from kWallpaper: %1")
                                     .arg(report.summary()),
                                 15000);
}

void MainWindow::showAndActivate() {
    showNormal();
    raise();
    activateWindow();
}

void MainWindow::closeEvent(QCloseEvent* event) {
    if (m_tray) {
        hide();  // keep running in the tray (Quit is in the tray menu)
        event->ignore();
        return;
    }
    event->accept();
}

// ---- appearance (spec §11) -------------------------------------------------

void MainWindow::applyAppearance() {
    const QString mode = m_engine->config().themeMode;
    if (mode == QLatin1String("light") || mode == QLatin1String("dark")) {
        if (qApp->style()->objectName() != QLatin1String("Fusion"))
            qApp->setStyle(QStringLiteral("Fusion"));
        QPalette pal = QApplication::style()->standardPalette();
        if (mode == QLatin1String("dark")) {
            pal.setColor(QPalette::Window, QColor(45, 45, 48));
            pal.setColor(QPalette::Base, QColor(31, 31, 34));
            pal.setColor(QPalette::AlternateBase, QColor(36, 36, 40));
            pal.setColor(QPalette::Text, QColor(255, 255, 255));
            pal.setColor(QPalette::Button, QColor(45, 45, 48));
            pal.setColor(QPalette::ButtonText, QColor(255, 255, 255));
            pal.setColor(QPalette::Highlight, QColor(42, 130, 218));
            pal.setColor(QPalette::HighlightedText, QColor(255, 255, 255));
            pal.setColor(QPalette::ToolTipBase, QColor(31, 31, 34));
            pal.setColor(QPalette::ToolTipText, QColor(255, 255, 255));
        } else {
            pal.setColor(QPalette::Window, QColor(240, 240, 240));
            pal.setColor(QPalette::Base, QColor(255, 255, 255));
            pal.setColor(QPalette::AlternateBase, QColor(245, 245, 245));
            pal.setColor(QPalette::Text, QColor(20, 20, 20));
            pal.setColor(QPalette::Button, QColor(240, 240, 240));
            pal.setColor(QPalette::ButtonText, QColor(20, 20, 20));
            pal.setColor(QPalette::Highlight, QColor(48, 140, 190));
            pal.setColor(QPalette::HighlightedText, QColor(255, 255, 255));
            pal.setColor(QPalette::ToolTipBase, QColor(255, 255, 220));
            pal.setColor(QPalette::ToolTipText, QColor(20, 20, 20));
        }
        qApp->setPalette(pal);
    } else {
        // "system": follow the DE platform theme natively (zero custom
        // palette code).
        qApp->setPalette(qApp->style()->standardPalette());
    }
    updateTrayIcon();
}

void MainWindow::updateTrayIcon() {
    if (!m_tray)
        return;
    const QString mode = m_engine->config().themeMode;
    const bool dark = mode == QLatin1String("dark") ||
                      (mode == QLatin1String("system") &&
                       qApp->styleHints()->colorScheme() == Qt::ColorScheme::Dark);
    m_tray->setIcon(svgIcon(dark ? kTrayDarkSvg : kTrayLightSvg, 64));
}

// ---- tray (spec §11) ---------------------------------------------------------

void MainWindow::setupTray() {
    if (!QSystemTrayIcon::isSystemTrayAvailable())
        return;  // degrade to window-only (e.g. stock GNOME)

    m_tray = new QSystemTrayIcon(this);
    updateTrayIcon();
    m_tray->setToolTip(QStringLiteral("Johona Wallpaper"));

    auto* menu = new QMenu(this);
    m_trayToggleAction = menu->addAction(tr("Stop scheduler"));
    m_trayToggleAction->setCheckable(true);
    connect(m_trayToggleAction, &QAction::triggered, this, &MainWindow::onToggleScheduler);

    m_trayNextAction = menu->addAction(tr("Next wallpaper"));
    m_trayNextAction->setVisible(
        m_engine->config().dailyShuffleEnabled);
    connect(m_trayNextAction, &QAction::triggered, this, &MainWindow::onNextWallpaper);

    menu->addSeparator();
    auto* showHide = menu->addAction(tr("Show Johona"));
    connect(showHide, &QAction::triggered, this, &MainWindow::onShowHide);
    auto* quit = menu->addAction(tr("Quit"));
    connect(quit, &QAction::triggered, qApp, &QApplication::quit);

    m_tray->setContextMenu(menu);
    connect(m_tray, &QSystemTrayIcon::activated, this, &MainWindow::onTrayActivated);
    m_tray->show();
}

void MainWindow::onTrayActivated(QSystemTrayIcon::ActivationReason reason) {
    if (reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::DoubleClick)
        showAndActivate();
}

void MainWindow::onToggleScheduler() {
    // Mirror the Scheduler tab's toggle (single source of truth: the engine).
    auto future = bridge::call<bool>(m_engine, [this]() {
        if (m_engine->isRunning()) {
            m_engine->stop();
            return false;
        }
        m_engine->start();
        return true;
    });
    future.then(this, [this](bool running) {
        if (m_trayToggleAction)
            m_trayToggleAction->setText(running ? tr("Stop scheduler")
                                                : tr("Start scheduler"));
        onTabStatus(running ? tr("Scheduler started") : tr("Scheduler stopped"));
    });
}

void MainWindow::onNextWallpaper() {
    auto future = bridge::call<ApplyOutcome>(m_engine, [this]() {
        return m_engine->advanceShuffle();
    });
    future.then(this, [this](ApplyOutcome out) {
        if (out.success)
            onTabStatus(tr("Next wallpaper: %1").arg(out.themeName));
        else
            onTabStatus(tr("Advance failed: %1").arg(out.message));
    });
}

void MainWindow::onShowHide() {
    if (isVisible())
        hide();
    else
        showAndActivate();
}

// ---- signal handlers ---------------------------------------------------------

void MainWindow::onEngineStatus(const QString& message) {
    statusBar()->showMessage(message, 8000);
}

void MainWindow::onEngineError(const QString& message) {
    statusBar()->showMessage(tr("Error: %1").arg(message), 15000);
}

void MainWindow::onTabStatus(const QString& message) {
    statusBar()->showMessage(message, 8000);
}

void MainWindow::onSchedulerToggled(bool running) {
    if (m_trayToggleAction) {
        m_trayToggleAction->setChecked(running);
        m_trayToggleAction->setText(running ? tr("Stop scheduler")
                                            : tr("Start scheduler"));
    }
}

void MainWindow::onSettingsSaved() {
    applyAppearance();
    m_themesTab->rebuildPreview();
    m_trayNextAction->setVisible(m_engine->config().dailyShuffleEnabled);
    statusBar()->showMessage(tr("Settings applied"), 5000);
}

void MainWindow::onDateCheck() {
    const QDate today = QDateTime::currentDateTime()
                            .toTimeZone(QTimeZone(m_engine->config().timezone.toUtf8()))
                            .date();
    if (today != m_lastDate) {
        m_lastDate = today;
        m_themesTab->rebuildPreview();  // new day → new schedule
    }
}

void MainWindow::onTzCheck() {
    const auto cfg = m_engine->config();
    if (!cfg.onTimezoneChange)
        return;
    auto future = bridge::call<bool>(m_location, [this]() {
        auto c = m_engine->config();
        const auto paths = m_engine->paths();
        return m_location->checkTimezoneChange(c, paths);
    });
    future.then(this, [this](bool changed) {
        if (!changed)
            return;
        m_settingsTab->reload();
        m_themesTab->rebuildPreview();
        statusBar()->showMessage(tr("System timezone changed — location updated"),
                                 10000);
    });
}

}  // namespace johona::gui
