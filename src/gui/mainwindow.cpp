// mainwindow.cpp — see mainwindow.hpp (kWallpaper WallpaperWindow parity).

#include "mainwindow.hpp"

#include <QApplication>
#include <QCloseEvent>
#include <QDate>
#include <QFileDialog>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QSettings>
#include <QShowEvent>
#include <QStatusBar>

#include "appicons.hpp"
#include "enginebridge.hpp"
#include "previewwidget.hpp"
#include "schedulepreview.hpp"
#include "schedulertab.hpp"
#include "settstab.hpp"
#include "themestab.hpp"

namespace johona::gui {

namespace {

// ── Breeze palettes (kWallpaper _breeze_light/_breeze_dark parity) ─────

QPalette breezeLight() {
    QPalette p;
    p.setColor(QPalette::Window, QColor("#eff0f1"));
    p.setColor(QPalette::WindowText, QColor("#31363b"));
    p.setColor(QPalette::Base, QColor("#fcfcfc"));
    p.setColor(QPalette::AlternateBase, QColor("#eff0f1"));
    p.setColor(QPalette::Text, QColor("#31363b"));
    p.setColor(QPalette::Button, QColor("#eff0f1"));
    p.setColor(QPalette::ButtonText, QColor("#31363b"));
    p.setColor(QPalette::Highlight, QColor("#3daee9"));
    p.setColor(QPalette::HighlightedText, QColor("#fcfcfc"));
    p.setColor(QPalette::ToolTipBase, QColor("#eff0f1"));
    p.setColor(QPalette::ToolTipText, QColor("#31363b"));
    p.setColor(QPalette::Link, QColor("#2980b9"));
    p.setColor(QPalette::Mid, QColor("#c8cbce"));
    p.setColor(QPalette::PlaceholderText, QColor("#8e9297"));
    const QPalette::ColorGroup d = QPalette::Disabled;
    p.setColor(d, QPalette::WindowText, QColor("#a0a1a3"));
    p.setColor(d, QPalette::Text, QColor("#a0a1a3"));
    p.setColor(d, QPalette::ButtonText, QColor("#a0a1a3"));
    return p;
}

QPalette breezeDark() {
    QPalette p;
    p.setColor(QPalette::Window, QColor("#31363b"));
    p.setColor(QPalette::WindowText, QColor("#eff0f1"));
    p.setColor(QPalette::Base, QColor("#232629"));
    p.setColor(QPalette::AlternateBase, QColor("#31363b"));
    p.setColor(QPalette::Text, QColor("#eff0f1"));
    p.setColor(QPalette::Button, QColor("#31363b"));
    p.setColor(QPalette::ButtonText, QColor("#eff0f1"));
    p.setColor(QPalette::Highlight, QColor("#3daee9"));
    p.setColor(QPalette::HighlightedText, QColor("#eff0f1"));
    p.setColor(QPalette::ToolTipBase, QColor("#31363b"));
    p.setColor(QPalette::ToolTipText, QColor("#eff0f1"));
    p.setColor(QPalette::Link, QColor("#2980b9"));
    p.setColor(QPalette::Mid, QColor("#464b50"));
    p.setColor(QPalette::PlaceholderText, QColor("#7f8487"));
    const QPalette::ColorGroup d = QPalette::Disabled;
    p.setColor(d, QPalette::WindowText, QColor("#6e7174"));
    p.setColor(d, QPalette::Text, QColor("#6e7174"));
    p.setColor(d, QPalette::ButtonText, QColor("#6e7174"));
    return p;
}

}  // namespace

MainWindow::MainWindow(Engine* engine,
                       location::LocationManager* locationManager,
                       const migration::Report& report)
    : m_engine(engine), m_location(locationManager), m_report(report) {
    setWindowTitle(QStringLiteral("Johona Wallpaper"));
    setWindowIcon(svgIcon(kAppIconSvg));

    // Snapshot the system palette BEFORE applying any scheme.
    m_systemPalette = QApplication::palette();

    resize(1200, 700);
    setMinimumSize(800, 500);

    setupMenus();
    setupTabs();
    setupTray();
    applyAppearance();

    statusBar()->showMessage(QStringLiteral("Ready"));

    // Restore the window geometry (kWallpaper QSettings parity).
    QSettings settings;
    restoreGeometry(settings.value(QStringLiteral("window/geometry"))
                        .toByteArray());
    const QString state =
        settings.value(QStringLiteral("window/state")).toString();
    if (!state.isEmpty())
        setWindowState(static_cast<Qt::WindowState>(state.toULong()));

    // ── engine → GUI ────────────────────────────────────────────────────
    connect(m_engine, &Engine::statusChanged, this,
            &MainWindow::onEngineStatus);
    connect(m_engine, &Engine::errorOccurred, this,
            &MainWindow::onEngineError);
    connect(m_engine, &Engine::logMessage, this, &MainWindow::onEngineLog);
    connect(m_engine, &Engine::runningChanged, this,
            &MainWindow::onRunningChanged);

    // ── tabs → window ───────────────────────────────────────────────────
    connect(m_themesTab, &ThemesTab::statusMessage, this,
            &MainWindow::onTabStatus);
    connect(m_settingsTab, &SettingsTab::settingsSaved, this,
            &MainWindow::onSettingsSaved);
    connect(m_settingsTab, &SettingsTab::schemeChanged, this,
            &MainWindow::onSchemeChanged);
    connect(m_settingsTab, &SettingsTab::statusMessage, this,
            &MainWindow::onTabStatus);
    connect(m_schedulerTab, &SchedulerTab::startRequested, this,
            &MainWindow::startScheduler);
    connect(m_schedulerTab, &SchedulerTab::stopRequested, this,
            &MainWindow::stopScheduler);
    connect(m_tabs, &QTabWidget::currentChanged, this,
            [this](int index) {
                if (index >= 0)
                    m_themesTab->setTabVisible(index == 0);
            });

    // Initial scheduler state (the engine may have hot-started).
    auto fut = bridge::call<bool>(m_engine,
                                  [this] { return m_engine->isRunning(); });
    fut.then(this, [this](bool running) { updateSchedulerUi(running); });

    // ── periodic hooks ──────────────────────────────────────────────────
    m_lastDate = QDate::currentDate();
    m_dateTimer.setInterval(30000);
    connect(&m_dateTimer, &QTimer::timeout, this, &MainWindow::onDateCheck);
    m_dateTimer.start();

    m_tzTimer.setInterval(5 * 60 * 1000);
    connect(&m_tzTimer, &QTimer::timeout, this, &MainWindow::onTzCheck);
    m_tzTimer.start();

    if (m_report.ran)
        statusBar()->showMessage(
            QStringLiteral("Migrated kWallpaper data: %1")
                .arg(m_report.summary()),
            8000);
}

void MainWindow::setupMenus() {
    auto* fileMenu = menuBar()->addMenu(QStringLiteral("File"));
    auto* importAct = fileMenu->addAction(
        themeIcon(QStringLiteral("document-import"), kFallbackImportSvg),
        QStringLiteral("Import Theme…"));
    importAct->setShortcut(QKeySequence(QStringLiteral("Ctrl+I")));
    connect(importAct, &QAction::triggered, this, &MainWindow::onImport);
    fileMenu->addSeparator();
    auto* quitAct = fileMenu->addAction(
        themeIcon(QStringLiteral("application-exit"), kFallbackExitSvg),
        QStringLiteral("Quit"));
    quitAct->setShortcut(QKeySequence(QStringLiteral("Ctrl+Q")));
    connect(quitAct, &QAction::triggered, qApp, &QApplication::quit);

    auto* schedMenu = menuBar()->addMenu(QStringLiteral("Scheduler"));
    m_menuStart = schedMenu->addAction(
        themeIcon(QStringLiteral("media-playback-start"), kFallbackPlaySvg),
        QStringLiteral("Start"));
    connect(m_menuStart, &QAction::triggered, this,
            &MainWindow::startScheduler);
    m_menuStop = schedMenu->addAction(
        themeIcon(QStringLiteral("media-playback-stop"), kFallbackStopSvg),
        QStringLiteral("Stop"));
    m_menuStop->setEnabled(false);
    connect(m_menuStop, &QAction::triggered, this,
            &MainWindow::stopScheduler);

    auto* helpMenu = menuBar()->addMenu(QStringLiteral("Help"));
    auto* aboutAct = helpMenu->addAction(
        themeIcon(QStringLiteral("help-about"), kFallbackAboutSvg),
        QStringLiteral("About…"));
    connect(aboutAct, &QAction::triggered, this, &MainWindow::onAbout);
}

void MainWindow::setupTabs() {
    m_tabs = new QTabWidget(this);
    m_themesTab = new ThemesTab(m_engine, m_engine->paths());
    m_settingsTab = new SettingsTab(m_engine, m_location);
    m_schedulerTab = new SchedulerTab(m_engine);
    m_tabs->addTab(
        m_themesTab,
        themeIcon(QStringLiteral("preferences-desktop-wallpaper"),
                  kFallbackImageSvg),
        QStringLiteral("Themes"));
    m_tabs->addTab(
        m_settingsTab,
        themeIcon(QStringLiteral("configure"), kFallbackConfigureSvg),
        QStringLiteral("Settings"));
    m_tabs->addTab(
        m_schedulerTab,
        themeIcon(QStringLiteral("chronometer"), kFallbackClockSvg),
        QStringLiteral("Scheduler"));
    setCentralWidget(m_tabs);

    m_themesTab->refresh();
}

void MainWindow::setupTray() {
    if (!QSystemTrayIcon::isSystemTrayAvailable())
        return;
    m_tray = new QSystemTrayIcon(this);
    m_tray->setToolTip(QStringLiteral("Johona Wallpaper"));
    updateTrayIcon();

    auto* menu = new QMenu(this);
    m_trayStatus = menu->addAction(QStringLiteral("Scheduler: Stopped"));
    m_trayStatus->setEnabled(false);
    menu->addSeparator();
    m_trayToggle = menu->addAction(
        themeIcon(QStringLiteral("media-playback-start"), kFallbackPlaySvg),
        QStringLiteral("Start Scheduler"));
    connect(m_trayToggle, &QAction::triggered, this,
            &MainWindow::onToggleScheduler);
    m_trayNext = menu->addAction(
        themeIcon(QStringLiteral("view-refresh"), kFallbackRefreshSvg),
        QStringLiteral("Next wallpaper"));
    connect(m_trayNext, &QAction::triggered, this,
            &MainWindow::onNextWallpaper);
    menu->addSeparator();
    auto* showAct = menu->addAction(
        themeIcon(QStringLiteral("window-new"), kFallbackWindowSvg),
        QStringLiteral("Show"));
    connect(showAct, &QAction::triggered, this,
            &MainWindow::showAndActivate);
    auto* quitAct = menu->addAction(
        themeIcon(QStringLiteral("application-exit"), kFallbackExitSvg),
        QStringLiteral("Quit"));
    connect(quitAct, &QAction::triggered, qApp, &QApplication::quit);

    m_tray->setContextMenu(menu);
    connect(m_tray, &QSystemTrayIcon::activated, this,
            &MainWindow::onTrayActivated);
    m_tray->show();
}

void MainWindow::updateTrayIcon() {
    if (!m_tray)
        return;
    // Light UI → dark glyph; dark UI → light glyph.
    const QString mode = m_engine->config().themeMode;
    m_tray->setIcon(mode == "light" ? svgIcon(kTrayLightSvg)
                                    : svgIcon(kTrayDarkSvg));
}

void MainWindow::applyAppearance() {
    const QString mode = m_engine->config().themeMode;
    if (mode == "dark")
        QApplication::setPalette(breezeDark());
    else if (mode == "light")
        QApplication::setPalette(breezeLight());
    else
        QApplication::setPalette(m_systemPalette);
    updateTrayIcon();
}

void MainWindow::showAndActivate() {
    show();
    raise();
    activateWindow();
}

void MainWindow::closeEvent(QCloseEvent* event) {
    if (m_tray && m_tray->isVisible()) {
        hide();  // keep running in the tray (Quit is in the tray menu)
        event->ignore();
        return;
    }
    QSettings settings;
    settings.setValue(QStringLiteral("window/geometry"), saveGeometry());
    settings.setValue(QStringLiteral("window/state"),
                      static_cast<quint64>(windowState()));
    QMainWindow::closeEvent(event);
}

void MainWindow::showEvent(QShowEvent* event) {
    QMainWindow::showEvent(event);
    if (m_tabs->currentIndex() == 0)
        m_themesTab->setTabVisible(true);
}

// ── engine → GUI ─────────────────────────────────────────────────────────

void MainWindow::onEngineStatus(const QString& message) {
    statusBar()->showMessage(message, 5000);
}

void MainWindow::onEngineError(const QString& message) {
    statusBar()->showMessage(message, 8000);
}

void MainWindow::onEngineLog(const QString& message) {
    m_schedulerTab->appendLog(message);
}

void MainWindow::onRunningChanged(bool running) {
    updateSchedulerUi(running);
}

void MainWindow::updateSchedulerUi(bool running) {
    m_menuStart->setEnabled(!running);
    m_menuStop->setEnabled(running);
    if (m_tray) {
        m_trayStatus->setText(running ? QStringLiteral("Scheduler: Running")
                                      : QStringLiteral("Scheduler: "
                                                        "Stopped"));
        m_trayToggle->setText(running ? QStringLiteral("Stop Scheduler")
                                      : QStringLiteral("Start Scheduler"));
    }
    m_themesTab->setSchedulerRunning(running);
    m_schedulerTab->setRunning(running);
}

void MainWindow::startScheduler() {
    bridge::call(m_engine, [this] { m_engine->start(); });
}

void MainWindow::stopScheduler() {
    bridge::call(m_engine, [this] { m_engine->stop(); });
}

void MainWindow::onToggleScheduler() {
    auto fut = bridge::call<bool>(
        m_engine, [this] { return m_engine->isRunning(); });
    fut.then(this, [this](bool running) {
        if (running)
            stopScheduler();
        else
            startScheduler();
    });
}

void MainWindow::onNextWallpaper() {
    auto future = bridge::call<ApplyOutcome>(
        m_engine, [this] { return m_engine->advanceShuffle(); });
    future.then(this, [this](ApplyOutcome out) {
        if (out.success)
            statusBar()->showMessage(
                QStringLiteral("Applied: %1").arg(out.themeName), 5000);
        else
            statusBar()->showMessage(out.message, 8000);
    });
}

// ── tabs → window ────────────────────────────────────────────────────────

void MainWindow::onTabStatus(const QString& message) {
    statusBar()->showMessage(message, 5000);
}

void MainWindow::onSettingsSaved() {
    // The schedule preview depends on the location.
    m_themesTab->rebuildPreview();
}

void MainWindow::onSchemeChanged(const QString& mode) {
    Q_UNUSED(mode);
    applyAppearance();
}

void MainWindow::onImport() {
    m_themesTab->onImport();
}

void MainWindow::onAbout() {
    QMessageBox::about(
        this, QStringLiteral("About Johona Wallpaper"),
        QStringLiteral(
            "<h3>Johona Wallpaper 1.0.0</h3>"
            "<p>A Flatpak wallpaper scheduler for Linux: it switches "
            "wallpaper themes with the sun (dawn → golden hour → day → "
            "golden hour → dusk → night), with optional daily shuffling "
            "and multi-desktop backends (Plasma, XDG portal, GNOME, "
            "xdg-settings).</p>"
            "<p>Inspired by <b>WinDynamicDesktop</b> "
            "(t1m0thyj/WinDynamicDesktop, MPL-2.0).<br>"
            "Solar calculations: <b>suncalc</b> v1.9.0 "
            "(mourner/suncalc, BSD-2-Clause).</p>"));
}

// ── periodic hooks ───────────────────────────────────────────────────────

void MainWindow::onDateCheck() {
    const QDate today = QDate::currentDate();
    if (today != m_lastDate) {
        m_lastDate = today;
        m_themesTab->rebuildPreview();
    }
}

void MainWindow::onTzCheck() {
    // Opt-in only (location_auto_update.on_timezone_change).
    const config::Config cfg = m_engine->config();
    if (!cfg.onTimezoneChange)
        return;
    auto fut = bridge::call<bool>(m_engine, [this] {
        config::Config c = m_engine->config();
        const bool changed =
            m_location->checkTimezoneChange(c, m_engine->paths());
        if (changed)
            m_engine->setConfig(c);
        return changed;
    });
    fut.then(this, [this](bool changed) {
        if (!changed)
            return;
        m_settingsTab->reload();
        m_themesTab->rebuildPreview();
        statusBar()->showMessage(
            QStringLiteral("System timezone changed — location updated"),
            8000);
    });
}

void MainWindow::onTrayActivated(QSystemTrayIcon::ActivationReason reason) {
    if (reason == QSystemTrayIcon::Trigger)
        if (isVisible())
            hide();
        else
            showAndActivate();
}

}  // namespace johona::gui
