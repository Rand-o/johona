// mainwindow.cpp — see mainwindow.hpp (redesign mockup shell).

#include "mainwindow.hpp"

#include <QApplication>
#include <QButtonGroup>
#include <QCloseEvent>
#include <QDate>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QSettings>
#include <QShowEvent>
#include <QStatusBar>
#include <QVBoxLayout>

#include "appicons.hpp"
#include "enginebridge.hpp"
#include "themes.hpp"
#include "schedulepreview.hpp"
#include "schedulertab.hpp"
#include "settstab.hpp"
#include "style.hpp"
#include "themestab.hpp"
#include "widgets.hpp"

namespace johona::gui {

namespace {

// ── Breeze palettes (mockup/redesign.html Breeze 6.7 tokens) ────────────
// Aligned to the mockup's exact light/dark token values (the redesign's
// single source of truth); "system" mode still uses the system palette
// snapshot taken at startup.

QPalette breezeLight() {
    QPalette p;
    p.setColor(QPalette::Window, QColor("#eff0f1"));
    p.setColor(QPalette::WindowText, QColor("#232629"));
    p.setColor(QPalette::Base, QColor("#ffffff"));
    p.setColor(QPalette::AlternateBase, QColor("#f7f7f7"));
    p.setColor(QPalette::Text, QColor("#232629"));
    p.setColor(QPalette::Button, QColor("#fcfcfc"));
    p.setColor(QPalette::ButtonText, QColor("#232629"));
    p.setColor(QPalette::Highlight, QColor("#3daee9"));
    p.setColor(QPalette::HighlightedText, QColor("#ffffff"));
    p.setColor(QPalette::ToolTipBase, QColor("#eff0f1"));
    p.setColor(QPalette::ToolTipText, QColor("#232629"));
    p.setColor(QPalette::Link, QColor("#2980b9"));
    p.setColor(QPalette::Mid, QColor("#b8babd"));
    p.setColor(QPalette::Midlight, QColor("#c8cbce"));
    p.setColor(QPalette::PlaceholderText, QColor("#80848a"));
    const QPalette::ColorGroup d = QPalette::Disabled;
    p.setColor(d, QPalette::WindowText, QColor("#a0a1a3"));
    p.setColor(d, QPalette::Text, QColor("#a0a1a3"));
    p.setColor(d, QPalette::ButtonText, QColor("#a0a1a3"));
    return p;
}

QPalette breezeDark() {
    QPalette p;
    p.setColor(QPalette::Window, QColor("#202326"));
    p.setColor(QPalette::WindowText, QColor("#fcfcfc"));
    p.setColor(QPalette::Base, QColor("#141618"));
    p.setColor(QPalette::AlternateBase, QColor("#1d1f22"));
    p.setColor(QPalette::Text, QColor("#fcfcfc"));
    p.setColor(QPalette::Button, QColor("#292c30"));
    p.setColor(QPalette::ButtonText, QColor("#fcfcfc"));
    p.setColor(QPalette::Highlight, QColor("#3daee9"));
    p.setColor(QPalette::HighlightedText, QColor("#fcfcfc"));
    p.setColor(QPalette::ToolTipBase, QColor("#202326"));
    p.setColor(QPalette::ToolTipText, QColor("#fcfcfc"));
    p.setColor(QPalette::Link, QColor("#1d99f3"));
    p.setColor(QPalette::Mid, QColor("#4d5154"));
    p.setColor(QPalette::Midlight, QColor("#54585c"));
    p.setColor(QPalette::PlaceholderText, QColor("#8e9297"));
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
    setWindowIcon(appIcon());

    // Snapshot the system palette BEFORE applying any scheme.
    m_systemPalette = QApplication::palette();

    resize(1200, 700);
    setMinimumSize(800, 500);

    setupUi();
    setupTray();
    applyAppearance();

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
    connect(m_engine, &Engine::applied, this,
            [this](const QString&, const QString& displayName, const QString&,
                   const QString&) { updateTrayTooltip(displayName); });

    // ── pages → window ──────────────────────────────────────────────────
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
    connect(m_schedulerTab, &SchedulerTab::nextRequested, this,
            &MainWindow::onNextWallpaper);

    // Initial scheduler UI state (sidebar card, tray, menu toggle).  If
    // the engine hot-starts, runningChanged(true) updates it again.
    updateSchedulerUi(false);

    // Initial scheduler state (the engine may have hot-started).
    refreshStatusInfo();

    // Initial tray tooltip: pretty name of the last-applied theme (the
    // engine thread does the theme.json read; the applied signal keeps
    // the tooltip current afterwards).
    bridge::call<QString>(m_engine, [this]() {
        const config::Config cfg = m_engine->config();
        if (cfg.lastApplied.isEmpty())
            return QString();
        const QString dir =
            m_engine->paths().themesDir + QStringLiteral("/") + cfg.lastApplied;
        if (auto data = themes::loadThemeData(dir))
            return themes::prettyThemeName(data->displayName, cfg.lastApplied);
        return themes::prettyThemeName(QString(), cfg.lastApplied);
    }).then(this, [this](QString name) { updateTrayTooltip(name); });

    // ── periodic hooks ──────────────────────────────────────────────────
    m_lastDate = QDate::currentDate();
    m_dateTimer.setInterval(30000);
    connect(&m_dateTimer, &QTimer::timeout, this, &MainWindow::onDateCheck);
    m_dateTimer.start();

    m_tzTimer.setInterval(5 * 60 * 1000);
    connect(&m_tzTimer, &QTimer::timeout, this, &MainWindow::onTzCheck);
    m_tzTimer.start();

    m_statusTimer.setInterval(60000);
    connect(&m_statusTimer, &QTimer::timeout, this,
            &MainWindow::onRefreshStatus);
    m_statusTimer.start();

    if (m_report.ran)
        m_statusMsg->showMessage(
            QStringLiteral("Migrated kWallpaper data: %1")
                .arg(m_report.summary()),
            8000);
}

void MainWindow::setupUi() {
    auto* central = new QWidget(this);
    auto* root = new QVBoxLayout(central);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // ── title bar (replaces the QMenuBar; the WM draws the frame) ──────
    auto* titlebar = new QWidget(central);
    titlebar->setProperty("cssClass", "titlebar");
    titlebar->setFixedHeight(40);
    auto* tl = new QHBoxLayout(titlebar);
    tl->setContentsMargins(10, 0, 4, 0);
    tl->setSpacing(0);
    auto* titleIcon = new QLabel(titlebar);
    titleIcon->setPixmap(appIcon().pixmap(22, 22));
    titleIcon->setFixedSize(22, 22);
    tl->addWidget(titleIcon);
    auto* tbTitle = new QLabel(QStringLiteral("Johona Wallpaper"), titlebar);
    {
        QFont f;
        f.setPixelSize(13);
        tbTitle->setFont(f);
    }
    tbTitle->setContentsMargins(8, 0, 10, 0);
    tl->addWidget(tbTitle);
    tl->addStretch(1);

    // Hamburger menu (mockup #menu-pop contents).
    m_menu = new QMenu(titlebar);
    auto* importAct = m_menu->addAction(
        colorIcon(kImportSvg, QColor("#80848a"), 24).pixmap(15, 15),
        QStringLiteral("Import Theme…"));
    importAct->setShortcut(QKeySequence(QStringLiteral("Ctrl+I")));
    connect(importAct, &QAction::triggered, this, &MainWindow::onImport);
    m_menu->addSeparator();
    m_menuToggle = m_menu->addAction(
        colorIcon(kPlayFilledSvg, QColor("#80848a"), 24).pixmap(15, 15),
        QStringLiteral("Start Scheduler"));
    connect(m_menuToggle, &QAction::triggered, this,
            &MainWindow::onToggleScheduler);
    auto* nextAct = m_menu->addAction(
        colorIcon(kRefreshSvg, QColor("#80848a"), 24).pixmap(15, 15),
        QStringLiteral("Next wallpaper"));
    connect(nextAct, &QAction::triggered, this, &MainWindow::onNextWallpaper);
    m_menu->addSeparator();
    auto* prefsAct = m_menu->addAction(
        colorIcon(kNavSettingsSvg, QColor("#80848a"), 24).pixmap(15, 15),
        QStringLiteral("Preferences"));
    connect(prefsAct, &QAction::triggered, this, [this]() { showPage(2); });
    auto* aboutAct = m_menu->addAction(
        colorIcon(kAboutSvg, QColor("#80848a"), 24).pixmap(15, 15),
        QStringLiteral("About Johona…"));
    connect(aboutAct, &QAction::triggered, this, &MainWindow::onAbout);
    m_menu->addSeparator();
    auto* quitAct = m_menu->addAction(
        colorIcon(kExitSvg, QColor("#80848a"), 24).pixmap(15, 15),
        QStringLiteral("Quit"));
    quitAct->setShortcut(QKeySequence(QStringLiteral("Ctrl+Q")));
    connect(quitAct, &QAction::triggered, qApp, &QApplication::quit);

    m_menuBtn = new QToolButton(titlebar);
    m_menuBtn->setFixedSize(32, 32);
    m_menuBtn->setToolTip(QStringLiteral("Application menu"));
    m_menuBtn->setPopupMode(QToolButton::InstantPopup);
    m_menuBtn->setMenu(m_menu);
    tl->addWidget(m_menuBtn);
    root->addWidget(titlebar);

    // ── body: sidebar + pages ───────────────────────────────────────────
    auto* body = new QWidget(central);
    auto* bl = new QHBoxLayout(body);
    bl->setContentsMargins(0, 0, 0, 0);
    bl->setSpacing(0);

    auto* sidebar = new QWidget(body);
    sidebar->setProperty("cssClass", "sidebar");
    sidebar->setFixedWidth(220);
    auto* sl = new QVBoxLayout(sidebar);
    sl->setContentsMargins(12, 14, 12, 12);
    sl->setSpacing(0);

    // Brand header (mockup .brand).
    auto* brand = new QWidget(sidebar);
    auto* brandL = new QHBoxLayout(brand);
    brandL->setContentsMargins(8, 4, 8, 16);
    brandL->setSpacing(10);
    auto* brandIcon = new QFrame(brand);
    brandIcon->setProperty("cssClass", "brand-icon");
    brandIcon->setFixedSize(36, 36);
    auto* bil = new QHBoxLayout(brandIcon);
    bil->setContentsMargins(0, 0, 0, 0);
    auto* biLbl = new QLabel(brandIcon);
    biLbl->setPixmap(appIcon().pixmap(22, 22));
    bil->addWidget(biLbl, 0, Qt::AlignCenter);
    brandL->addWidget(brandIcon);
    auto* brandText = new QVBoxLayout();
    brandText->setSpacing(1);
    auto* bn = new QLabel(QStringLiteral("Johona"), brand);
    {
        QFont f;
        f.setPixelSize(15);
        f.setWeight(QFont::Bold);
        bn->setFont(f);
    }
    auto* bs = new QLabel(QStringLiteral("Wallpaper Scheduler"), brand);
    bs->setProperty("cssClass", "muted");
    {
        QFont f;
        f.setPixelSize(11);
        bs->setFont(f);
    }
    brandText->addWidget(bn);
    brandText->addWidget(bs);
    brandL->addLayout(brandText);
    sl->addWidget(brand);

    // Nav (mockup .nav).
    auto* nav = new QButtonGroup(this);
    nav->setExclusive(true);
    m_navThemes = new NavItem(kNavThemesSvg, QStringLiteral("Themes"),
                              sidebar);
    m_navScheduler = new NavItem(kNavSchedulerSvg,
                                 QStringLiteral("Scheduler"), sidebar);
    m_navSettings = new NavItem(kNavSettingsSvg,
                                QStringLiteral("Settings"), sidebar);
    nav->addButton(m_navThemes, 0);
    nav->addButton(m_navScheduler, 1);
    nav->addButton(m_navSettings, 2);
    m_navThemes->setChecked(true);
    connect(nav, &QButtonGroup::idClicked, this, &MainWindow::showPage);
    auto* navLay = new QVBoxLayout();
    navLay->setSpacing(2);
    navLay->addWidget(m_navThemes);
    navLay->addWidget(m_navScheduler);
    navLay->addWidget(m_navSettings);
    sl->addLayout(navLay);

    sl->addStretch(1);

    // Scheduler status card (mockup .side-status).
    auto* card = new QFrame(sidebar);
    card->setProperty("cssClass", "card");
    auto* cl = new QVBoxLayout(card);
    cl->setContentsMargins(12, 10, 12, 10);
    cl->setSpacing(0);
    auto* row = new QHBoxLayout();
    row->setSpacing(7);
    m_sideDot = new StatusDot(8, card);
    row->addWidget(m_sideDot);
    m_sideTitle = new QLabel(QStringLiteral("Scheduler stopped"), card);
    {
        QFont f;
        f.setPixelSize(13);  // 12.5 px mockup
        f.setWeight(QFont::DemiBold);
        m_sideTitle->setFont(f);
    }
    row->addWidget(m_sideTitle);
    row->addStretch(1);
    cl->addLayout(row);
    m_sideSub =
        new QLabel(QStringLiteral("Press Start to apply wallpapers"), card);
    m_sideSub->setProperty("cssClass", "muted");
    {
        QFont f;
        f.setPixelSize(11);
        m_sideSub->setFont(f);
    }
    m_sideSub->setWordWrap(true);
    m_sideSub->setContentsMargins(15, 4, 0, 9);
    cl->addWidget(m_sideSub);
    m_sideToggle = new QPushButton(card);
    m_sideToggle->setProperty("cssClass", "small");
    connect(m_sideToggle, &QPushButton::clicked, this,
            &MainWindow::onToggleScheduler);
    cl->addWidget(m_sideToggle);
    sl->addWidget(card);

    bl->addWidget(sidebar);

    // Pages.
    m_pages = new QStackedWidget(body);
    m_themesTab = new ThemesTab(m_engine, m_engine->paths());
    m_schedulerTab = new SchedulerTab(m_engine);
    m_settingsTab = new SettingsTab(m_engine, m_location);
    m_pages->addWidget(m_themesTab);    // 0
    m_pages->addWidget(m_schedulerTab); // 1
    m_pages->addWidget(m_settingsTab);  // 2
    bl->addWidget(m_pages, 1);
    root->addWidget(body, 1);

    setCentralWidget(central);

    // ── status bar (mockup #statusbar) ──────────────────────────────────
    auto* sb = statusBar();
    sb->setFixedHeight(27);
    m_statusMsg = new StatusMessageLabel(sb);
    sb->addWidget(m_statusMsg, 1);

    auto* right = new QWidget(sb);
    auto* rh = new QHBoxLayout(right);
    rh->setContentsMargins(0, 0, 0, 0);
    rh->setSpacing(12);
    auto* nextItem = new QWidget(right);
    auto* nh = new QHBoxLayout(nextItem);
    nh->setContentsMargins(0, 0, 0, 0);
    nh->setSpacing(5);
    auto* clockIcon = new QLabel(nextItem);
    clockIcon->setPixmap(
        colorIcon(kNavSchedulerSvg, QColor("#80848a"), 24).pixmap(12, 12));
    nh->addWidget(clockIcon);
    m_sbNext = new QLabel(QStringLiteral("Next —"), nextItem);
    nh->addWidget(m_sbNext);
    rh->addWidget(nextItem);
    auto addSep = [right]() {
        auto* s = new QWidget(right);
        s->setProperty("cssClass", "sb-sep");
        s->setFixedSize(1, 13);
        return s;
    };
    rh->addWidget(addSep());
    m_sbBackend = new QLabel(QStringLiteral("no backend"), right);
    rh->addWidget(m_sbBackend);
    rh->addWidget(addSep());
    rh->addWidget(new QLabel(QStringLiteral("v1.0.0"), right));
    sb->addPermanentWidget(right);

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
    connect(showAct, &QAction::triggered, this, &MainWindow::showAndActivate);
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
    m_tray->setIcon(mode == "light" ? trayIconLight() : trayIconDark());
}

void MainWindow::updateTrayTooltip(const QString& themeDisplayName) {
    if (!m_tray)
        return;
    m_tray->setToolTip(themeDisplayName.trimmed().isEmpty()
                           ? QStringLiteral("Johona Wallpaper")
                           : QStringLiteral("Johona — %1")
                                 .arg(themeDisplayName.trimmed()));
}

void MainWindow::updateTitleBarIcon() {
    const auto& tok = style::current();
    m_menuBtn->setIcon(
        colorIcon(kMenuHamburgerSvg, QColor(tok.windowText), 24).pixmap(19, 19));
}

void MainWindow::applyAppearance() {
    const QString mode = m_engine->config().themeMode;
    if (mode == "dark") {
        QApplication::setPalette(breezeDark());
        style::setTokens(style::dark());
    } else if (mode == "light") {
        QApplication::setPalette(breezeLight());
        style::setTokens(style::light());
    } else {
        QApplication::setPalette(m_systemPalette);
        style::setTokens(
            style::tokensFor(m_systemPalette.color(QPalette::Window)));
    }
    qApp->setStyleSheet(style::buildStyleSheet(style::current()));
    updateTrayIcon();
    updateTitleBarIcon();
    m_schedulerTab->refreshThemeColors();
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
    if (m_pages->currentIndex() == 0)
        m_themesTab->setTabVisible(true);
}

// ── engine → GUI ─────────────────────────────────────────────────────────

void MainWindow::onEngineStatus(const QString& message) {
    m_statusMsg->showMessage(message, 5000);
}

void MainWindow::onEngineError(const QString& message) {
    m_statusMsg->showMessage(message, 8000);
}

void MainWindow::onEngineLog(const QString& message) {
    m_schedulerTab->appendLog(message);
}

void MainWindow::onRunningChanged(bool running) {
    updateSchedulerUi(running);
    refreshStatusInfo();  // next-change info changed with the state
}

void MainWindow::updateSchedulerUi(bool running) {
    m_statusInfo.running = running;

    // Sidebar status card.
    m_sideDot->setOn(running);
    m_sideTitle->setText(running ? QStringLiteral("Scheduler running")
                                 : QStringLiteral("Scheduler stopped"));
    const auto& tok = style::current();
    if (running) {
        m_sideToggle->setProperty("cssClass", "small");
        m_sideToggle->setIcon(
            colorIcon(kStopFilledSvg, QColor(tok.windowText), 24).pixmap(15, 15));
        m_sideToggle->setText(QStringLiteral("Stop"));
    } else {
        m_sideToggle->setProperty("cssClass", "small primary");
        m_sideToggle->setIcon(
            colorIcon(kPlayFilledSvg, Qt::white, 24).pixmap(15, 15));
        m_sideToggle->setText(QStringLiteral("Start"));
    }
    m_sideToggle->style()->unpolish(m_sideToggle);
    m_sideToggle->style()->polish(m_sideToggle);

    // Tray + hamburger menu.
    if (m_tray) {
        m_trayStatus->setText(running ? QStringLiteral("Scheduler: Running")
                                      : QStringLiteral("Scheduler: "
                                                        "Stopped"));
        m_trayToggle->setText(running ? QStringLiteral("Stop Scheduler")
                                      : QStringLiteral("Start Scheduler"));
        m_trayToggle->setIcon(
            running ? themeIcon(QStringLiteral("media-playback-stop"),
                                kFallbackStopSvg)
                    : themeIcon(QStringLiteral("media-playback-start"),
                                kFallbackPlaySvg));
    }
    m_menuToggle->setText(running ? QStringLiteral("Stop Scheduler")
                                  : QStringLiteral("Start Scheduler"));
    m_menuToggle->setIcon(
        running ? colorIcon(kStopFilledSvg, QColor("#80848a"), 24).pixmap(15, 15)
                : colorIcon(kPlayFilledSvg, QColor("#80848a"), 24).pixmap(15, 15));

    // Pages.
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
    if (m_statusInfo.running)
        stopScheduler();
    else
        startScheduler();
}

void MainWindow::onNextWallpaper() {
    auto future = bridge::call<ApplyOutcome>(
        m_engine, [this] { return m_engine->advanceShuffle(); });
    future.then(this, [this](ApplyOutcome out) {
        if (out.success)
            m_statusMsg->showMessage(
                QStringLiteral("Applied: %1").arg(out.themeName), 5000);
        else
            m_statusMsg->showMessage(out.message, 8000);
    });
}

// ── pages → window ───────────────────────────────────────────────────────

void MainWindow::onTabStatus(const QString& message) {
    m_statusMsg->showMessage(message, 5000);
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

// ── status info (sidebar + status bar) ───────────────────────────────────

void MainWindow::refreshStatusInfo() {
    auto fut = bridge::call<StatusInfo>(m_engine, [this]() {
        StatusInfo info;
        info.running = m_engine->isRunning();
        const auto [when, label] = m_engine->nextChange();
        if (when.isValid()) {
            info.nextTime = when.time().toString("HH:mm");
            info.nextLabel = label;
        } else {
            info.nextTime = QStringLiteral("—");
        }
        info.backend = m_engine->activeBackendName();
        return info;
    });
    fut.then(this, [this](StatusInfo info) {
        m_statusInfo = info;
        applyStatusInfo(info);
    });
}

void MainWindow::applyStatusInfo(const StatusInfo& info) {
    if (info.running) {
        if (info.nextLabel.isEmpty() ||
            info.nextLabel == QStringLiteral("next change"))
            m_sideSub->setText(
                QStringLiteral("Next change %1").arg(info.nextTime));
        else
            m_sideSub->setText(QStringLiteral("Next change %1 · %2")
                                   .arg(info.nextTime, info.nextLabel));
        m_sbNext->setText(QStringLiteral("Next %1").arg(info.nextTime));
    } else {
        m_sideSub->setText(
            QStringLiteral("Press Start to apply wallpapers"));
        m_sbNext->setText(QStringLiteral("Next —"));
    }
    m_sbBackend->setText(
        info.backend.isEmpty() || info.backend == "none"
            ? QStringLiteral("no backend")
            : QStringLiteral("%1 backend").arg(info.backend));
}

void MainWindow::onRefreshStatus() {
    refreshStatusInfo();
}

// ── navigation ───────────────────────────────────────────────────────────

void MainWindow::showPage(int index) {
    m_pages->setCurrentIndex(index);
    m_themesTab->setTabVisible(index == 0);
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
        m_statusMsg->showMessage(
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
