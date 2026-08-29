// settstab.cpp — see settstab.hpp (kWallpaper SettingsPage parity).

#include "settstab.hpp"

#include <QCompleter>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QVBoxLayout>

#include "appicons.hpp"
#include "autostart.hpp"
#include "enginebridge.hpp"

namespace johona::gui {

namespace {

const char* kBackendIds[] = {"auto", "plasma", "portal", "gnome",
                             "xdg_settings"};

}  // namespace

SettingsTab::SettingsTab(Engine* engine,
                         location::LocationManager* locationManager,
                         QWidget* parent)
    : QWidget(parent), m_engine(engine), m_location(locationManager) {
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);

    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    outer->addWidget(scroll, 1);

    auto* body = new QWidget();
    auto* col = new QVBoxLayout(body);
    col->setSpacing(16);

    // ── Scheduler ───────────────────────────────────────────────────────
    auto* sg = new QGroupBox(QStringLiteral("Scheduler"), body);
    col->addWidget(sg);
    auto* sf = new QFormLayout(sg);
    m_interval = new QSpinBox(sg);
    m_interval->setRange(1, 3600);
    m_interval->setSuffix(QStringLiteral(" s"));
    sf->addRow(QStringLiteral("Cycle interval:"), m_interval);
    m_runCycle = new QCheckBox(
        QStringLiteral("Enable cycle task (runs every interval)"), sg);
    sf->addRow(m_runCycle);
    m_dailyShuffle =
        new QCheckBox(QStringLiteral("Enable daily theme shuffle"), sg);
    sf->addRow(m_dailyShuffle);
    m_startOnLaunch = new QCheckBox(
        QStringLiteral("Start scheduler on app launch"), sg);
    sf->addRow(m_startOnLaunch);

    // ── Location ────────────────────────────────────────────────────────
    auto* lg = new QGroupBox(QStringLiteral("Location"), body);
    col->addWidget(lg);
    auto* lf = new QFormLayout(lg);
    m_timezone = new QLineEdit(lg);
    {
        QStringList zones;
        for (const QByteArray& id : QTimeZone::availableTimeZoneIds())
            zones << QString::fromUtf8(id);
        m_timezone->setCompleter(new QCompleter(zones, m_timezone));
    }
    lf->addRow(QStringLiteral("Timezone:"), m_timezone);

    auto* latLonRow = new QHBoxLayout();
    m_lat = new QDoubleSpinBox(lg);
    m_lat->setRange(-90.0, 90.0);
    m_lat->setDecimals(4);
    latLonRow->addWidget(m_lat);
    latLonRow->addWidget(new QLabel(QStringLiteral("Latitude"), lg));
    m_lon = new QDoubleSpinBox(lg);
    m_lon->setRange(-180.0, 180.0);
    m_lon->setDecimals(4);
    latLonRow->addWidget(m_lon);
    latLonRow->addWidget(new QLabel(QStringLiteral("Longitude"), lg));
    m_autoDetectBtn = new QPushButton(
        themeIcon(QStringLiteral("view-refresh"), kFallbackRefreshSvg),
        QStringLiteral("Auto-detect"), lg);
    m_autoDetectBtn->setToolTip(
        QStringLiteral("Detect the current location (Geoclue2, then "
                       "timezone lookup)"));
    connect(m_autoDetectBtn, &QPushButton::clicked, this,
            &SettingsTab::onAutoDetect);
    latLonRow->addWidget(m_autoDetectBtn);
    lf->addRow(latLonRow);

    // ── Wallpaper backend (Johona: multi-desktop) ───────────────────────
    auto* bg = new QGroupBox(QStringLiteral("Wallpaper backend"), body);
    col->addWidget(bg);
    auto* bf = new QFormLayout(bg);
    m_backend = new QComboBox(bg);
    m_backend->addItem(QStringLiteral("Auto (detected)"), "auto");
    m_backend->addItem(QStringLiteral("Plasma (KDE)"), "plasma");
    m_backend->addItem(QStringLiteral("XDG Background portal"), "portal");
    m_backend->addItem(QStringLiteral("GNOME (gsettings)"), "gnome");
    m_backend->addItem(QStringLiteral("xdg-settings"), "xdg_settings");
    bf->addRow(QStringLiteral("Backend:"), m_backend);
    auto* backendHint = new QLabel(
        QStringLiteral("Auto uses the first available desktop backend "
                       "(Plasma, portal, GNOME, xdg-settings)."),
        bg);
    QPalette hpal = backendHint->palette();
    hpal.setColor(QPalette::WindowText,
                  hpal.color(QPalette::PlaceholderText));
    backendHint->setPalette(hpal);
    bf->addRow(QString(), backendHint);

    // ── Appearance ──────────────────────────────────────────────────────
    auto* ag = new QGroupBox(QStringLiteral("Appearance"), body);
    col->addWidget(ag);
    auto* af = new QFormLayout(ag);
    m_autostart =
        new QCheckBox(QStringLiteral("Start automatically at login"), ag);
    connect(m_autostart, &QCheckBox::toggled, this,
            &SettingsTab::onAutostartToggled);
    af->addRow(m_autostart);
    m_scheme = new QComboBox(ag);
    m_scheme->addItem(QStringLiteral("System"));
    m_scheme->addItem(QStringLiteral("Breeze Light"));
    m_scheme->addItem(QStringLiteral("Breeze Dark"));
    m_scheme->setToolTip(
        QStringLiteral("Override the colour scheme or follow the system "
                       "KDE theme"));
    connect(m_scheme, &QComboBox::currentIndexChanged, this,
            &SettingsTab::onSchemeChanged);
    af->addRow(QStringLiteral("Color scheme:"), m_scheme);

    col->addStretch();
    scroll->setWidget(body);

    // ── Save ────────────────────────────────────────────────────────────
    auto* row = new QHBoxLayout();
    row->addStretch();
    m_saveBtn = new QPushButton(
        themeIcon(QStringLiteral("document-save"), kFallbackSaveSvg),
        QStringLiteral("Save Settings"), this);
    m_saveBtn->setShortcut(QKeySequence(QStringLiteral("Ctrl+S")));
    connect(m_saveBtn, &QPushButton::clicked, this, &SettingsTab::onSave);
    row->addWidget(m_saveBtn);
    outer->addLayout(row);

    reload();
}

void SettingsTab::reload() {
    const config::Config c = m_engine->config();
    m_interval->setValue(c.safetyInterval);
    m_runCycle->setChecked(c.cycleEnabled);
    m_dailyShuffle->setChecked(c.dailyShuffleEnabled);
    m_startOnLaunch->setChecked(c.startSchedulerOnLaunch);
    m_timezone->setText(c.timezone);
    m_lat->setValue(c.latitude);
    m_lon->setValue(c.longitude);
    m_backend->blockSignals(true);
    int bi = 0;
    for (int i = 0; i < 5; i++)
        if (QString(kBackendIds[i]) == c.backendOverride)
            bi = i;
    m_backend->setCurrentIndex(bi);
    m_backend->blockSignals(false);
    m_scheme->blockSignals(true);
    m_scheme->setCurrentIndex(c.themeMode == "light"
                                  ? 1
                                  : c.themeMode == "dark" ? 2 : 0);
    m_scheme->blockSignals(false);
    m_autostart->blockSignals(true);
    m_autostart->setChecked(c.autostartEnabled);
    m_autostart->blockSignals(false);
}

config::Config SettingsTab::collect() const {
    config::Config c = m_engine->config();  // read-modify-write
    c.safetyInterval = m_interval->value();
    c.cycleEnabled = m_runCycle->isChecked();
    c.dailyShuffleEnabled = m_dailyShuffle->isChecked();
    c.startSchedulerOnLaunch = m_startOnLaunch->isChecked();
    c.timezone = m_timezone->text().trimmed();
    c.latitude = m_lat->value();
    c.longitude = m_lon->value();
    c.backendOverride = m_backend->currentData().toString();
    c.themeMode = m_scheme->currentIndex() == 1
                      ? QStringLiteral("light")
                      : m_scheme->currentIndex() == 2 ? QStringLiteral("dark")
                                                       : QStringLiteral("system");
    c.autostartEnabled = m_autostart->isChecked();
    return c;
}

void SettingsTab::onSave() {
    config::Config c = collect();
    if (!config::isValidTimezone(c.timezone)) {
        QMessageBox::warning(
            this, QStringLiteral("Invalid Timezone"),
            QStringLiteral("'%1' is not a valid IANA timezone id (e.g. "
                           "America/Phoenix).")
                .arg(c.timezone));
        return;
    }
    if (!config::save(c)) {
        QMessageBox::warning(this, QStringLiteral("Error"),
                             QStringLiteral("Could not save settings."));
        return;
    }
    // Hot-reload the running engine (cycle interval, shuffle, backend…).
    bridge::call(m_engine, [this, c] { m_engine->setConfig(c); });
    emit settingsSaved();
    emit statusMessage(QStringLiteral("Settings saved"));
}

void SettingsTab::onAutoDetect() {
    m_autoDetectBtn->setEnabled(false);
    auto future = bridge::call<location::Location>(
        m_location, [this] {
            QString source;
            const location::Location loc =
                m_location->detect(m_engine->config(), &source);
            qInfo() << "Location detected via" << source << ":" << loc.latitude
                    << loc.longitude << loc.timezone;
            return loc;
        });
    future.then(this, [this](location::Location loc) {
        m_autoDetectBtn->setEnabled(true);
        m_lat->setValue(loc.latitude);
        m_lon->setValue(loc.longitude);
        if (!loc.timezone.isEmpty())
            m_timezone->setText(loc.timezone);
        emit statusMessage(QStringLiteral("Location detected: %1, %2 (%3)")
                               .arg(loc.latitude, 0, 'f', 4)
                               .arg(loc.longitude, 0, 'f', 4)
                               .arg(loc.timezone));
    });
}

void SettingsTab::onSchemeChanged(int index) {
    const QString name = index == 1
                             ? QStringLiteral("light")
                             : index == 2 ? QStringLiteral("dark")
                                          : QStringLiteral("system");
    // The palette itself is applied by the main window (it owns the
    // system-palette snapshot); persist the preference immediately.
    config::Config c = m_engine->config();
    c.themeMode = name;
    config::save(c);
    bridge::call(m_engine, [this, c] { m_engine->setConfig(c); });
    emit schemeChanged(name);
}

void SettingsTab::onAutostartToggled(bool enabled) {
    // Apply immediately (kWallpaper parity).
    autostart::setEnabled(enabled);
    config::Config c = m_engine->config();
    c.autostartEnabled = enabled;
    config::save(c);
    bridge::call(m_engine, [this, c] { m_engine->setConfig(c); });
}

}  // namespace johona::gui
