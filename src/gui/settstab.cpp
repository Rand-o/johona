// settstab.cpp — see settstab.hpp.

#include "settstab.hpp"

#include <QCompleter>
#include <QDateTime>
#include <QFormLayout>
#include <QFuture>
#include <QGroupBox>
#include <QMessageBox>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrent>

#include "autostart.hpp"
#include "enginebridge.hpp"

namespace johona::gui {

namespace {
const char* kBackendIds[] = {"auto", "plasma", "portal", "gnome", "xdg_settings"};
}  // namespace

SettingsTab::SettingsTab(Engine* engine, location::LocationManager* locationManager,
                         QWidget* parent)
    : QWidget(parent), m_engine(engine), m_location(locationManager) {
    // ---- Scheduler -------------------------------------------------------
    auto* schedGroup = new QGroupBox(tr("Scheduler"), this);
    auto* schedForm = new QFormLayout(schedGroup);
    m_safetyInterval = new QSpinBox(schedGroup);
    m_safetyInterval->setRange(5, 3600);
    m_safetyInterval->setSuffix(tr(" s"));
    m_safetyInterval->setSingleStep(5);
    m_dailyShuffle = new QCheckBox(tr("Shuffle to a new theme each day"), schedGroup);
    m_startOnLaunch = new QCheckBox(tr("Start the scheduler on launch"), schedGroup);
    schedForm->addRow(tr("Safety-tick interval:"), m_safetyInterval);
    schedForm->addWidget(m_dailyShuffle);
    schedForm->addWidget(m_startOnLaunch);

    // ---- Wallpaper backend ------------------------------------------------
    auto* backendGroup = new QGroupBox(tr("Wallpaper backend"), this);
    auto* backendForm = new QFormLayout(backendGroup);
    m_backend = new QComboBox(backendGroup);
    for (const char* id : kBackendIds)
        m_backend->addItem(tr(id == "auto" ? "Auto (detected)" : id), id);
    m_backendHint = new QLabel(backendGroup);
    m_backendHint->setWordWrap(true);
    m_backendHint->setStyleSheet("color: #b91c1c;");
    m_backendHint->hide();
    backendForm->addRow(tr("Backend:"), m_backend);
    backendForm->addRow(QString(), m_backendHint);

    // ---- Location ----------------------------------------------------------
    auto* locGroup = new QGroupBox(tr("Location"), this);
    auto* locForm = new QFormLayout(locGroup);
    m_city = new QLineEdit(locGroup);
    m_latitude = new QDoubleSpinBox(locGroup);
    m_latitude->setRange(-90.0, 90.0);
    m_latitude->setDecimals(4);
    m_longitude = new QDoubleSpinBox(locGroup);
    m_longitude->setRange(-180.0, 180.0);
    m_longitude->setDecimals(4);
    m_timezone = new QComboBox(locGroup);
    m_timezone->setEditable(true);
    m_timezone->setInsertPolicy(QComboBox::NoInsert);
    // availableTimeZoneIds() returns QList<QByteArray> — call it ONCE and
    // convert; an iterator range across two temporaries is undefined.
    const auto tzIdsRaw = QTimeZone::availableTimeZoneIds();
    QStringList tzIds;
    tzIds.reserve(tzIdsRaw.size());
    for (const auto& id : tzIdsRaw)
        tzIds << QString::fromUtf8(id);
    m_timezone->setCompleter(new QCompleter(tzIds, m_timezone));
    m_tzAutoUpdate = new QCheckBox(tr("Update location when the system timezone changes"),
                                   locGroup);
    m_detectBtn = new QPushButton(tr("Auto-detect…"), locGroup);
    locForm->addRow(tr("City:"), m_city);
    locForm->addRow(tr("Latitude:"), m_latitude);
    locForm->addRow(tr("Longitude:"), m_longitude);
    locForm->addRow(tr("Timezone:"), m_timezone);
    locForm->addWidget(m_tzAutoUpdate);
    locForm->addRow(QString(), m_detectBtn);

    // ---- Appearance --------------------------------------------------------
    auto* appGroup = new QGroupBox(tr("Appearance"), this);
    auto* appForm = new QFormLayout(appGroup);
    m_appearance = new QComboBox(appGroup);
    m_appearance->addItem(tr("System"), "system");
    m_appearance->addItem(tr("Light"), "light");
    m_appearance->addItem(tr("Dark"), "dark");
    appForm->addRow(tr("Theme:"), m_appearance);

    // ---- Autostart ---------------------------------------------------------
    auto* autoGroup = new QGroupBox(tr("Autostart"), this);
    m_autostart = new QCheckBox(tr("Start at login"), autoGroup);
    auto* autoLayout = new QVBoxLayout(autoGroup);
    autoLayout->addWidget(m_autostart);

    // ---- Save --------------------------------------------------------------
    m_saveBtn = new QPushButton(tr("Save"), this);
    m_saveBtn->setDefault(true);

    auto* root = new QVBoxLayout(this);
    root->addWidget(schedGroup);
    root->addWidget(backendGroup);
    root->addWidget(locGroup);
    root->addWidget(appGroup);
    root->addWidget(autoGroup);
    root->addStretch(1);
    root->addWidget(m_saveBtn, 0, Qt::AlignRight);

    connect(m_saveBtn, &QPushButton::clicked, this, &SettingsTab::onSave);
    connect(m_detectBtn, &QPushButton::clicked, this, &SettingsTab::onAutoDetect);
    connect(m_backend, &QComboBox::currentIndexChanged, this,
            &SettingsTab::onBackendChanged);

    reload();
}

void SettingsTab::reload() {
    const auto cfg = m_engine->config();
    m_safetyInterval->setValue(cfg.safetyInterval);
    m_dailyShuffle->setChecked(cfg.dailyShuffleEnabled);
    m_startOnLaunch->setChecked(cfg.startSchedulerOnLaunch);
    m_city->setText(cfg.city);
    m_latitude->setValue(cfg.latitude);
    m_longitude->setValue(cfg.longitude);
    m_timezone->blockSignals(true);
    m_timezone->setCurrentText(cfg.timezone);
    m_timezone->blockSignals(false);
    m_tzAutoUpdate->setChecked(cfg.onTimezoneChange);
    m_appearance->setCurrentText(cfg.themeMode);
    m_autostart->setChecked(autostart::isEnabled());
    for (int i = 0; i < m_backend->count(); i++)
        if (m_backend->itemData(i).toString() == cfg.backendOverride) {
            m_backend->blockSignals(true);
            m_backend->setCurrentIndex(i);
            m_backend->blockSignals(false);
            break;
        }
    onBackendChanged(m_backend->currentIndex());
}

config::Config SettingsTab::collect() const {
    config::Config cfg = m_engine->config();
    cfg.safetyInterval = m_safetyInterval->value();
    cfg.dailyShuffleEnabled = m_dailyShuffle->isChecked();
    cfg.startSchedulerOnLaunch = m_startOnLaunch->isChecked();
    cfg.city = m_city->text().trimmed();
    cfg.latitude = m_latitude->value();
    cfg.longitude = m_longitude->value();
    cfg.timezone = m_timezone->currentText().trimmed();
    cfg.onTimezoneChange = m_tzAutoUpdate->isChecked();
    cfg.themeMode = m_appearance->currentData().toString();
    cfg.backendOverride = m_backend->currentData().toString();
    return cfg;
}

void SettingsTab::onBackendChanged(int) {
    const QString id = m_backend->currentData().toString();
    if (id == QLatin1String("auto")) {
        m_backendHint->hide();
        return;
    }
    // Probe availability (engine thread — D-Bus pings).
    setControlsEnabled(false);
    auto future = bridge::call<bool>(m_engine, [this, id]() {
        return m_engine->probeBackends().value(id, false);
    });
    future.then(this, [this, id](bool available) {
        setControlsEnabled(true);
        if (available) {
            m_backendHint->hide();
        } else {
            m_backendHint->show();
            m_backendHint->setText(
                tr("This backend is not available on this system. Wallpaper "
                   "changes will fail until you pick another backend."));
        }
    });
}

void SettingsTab::setControlsEnabled(bool on) {
    m_saveBtn->setEnabled(on);
    m_detectBtn->setEnabled(on);
}

void SettingsTab::onSave() {
    const QString tz = m_timezone->currentText().trimmed();
    if (!config::isValidTimezone(tz)) {
        QMessageBox::warning(this, tr("Invalid timezone"),
                             tr("“%1” is not a valid IANA timezone (e.g. "
                                "America/Phoenix).")
                                 .arg(tz));
        return;
    }
    const config::Config cfg = collect();
    if (!config::save(cfg)) {
        QMessageBox::critical(this, tr("Save failed"),
                              tr("Could not write the config file."));
        return;
    }
    // Host autostart entry (small file write, off the GUI thread).
    const bool wantAutostart = m_autostart->isChecked();
    QThreadPool::globalInstance()->start(
        [wantAutostart]() { autostart::setEnabled(wantAutostart); });

    // Hot-reload the running scheduler (spec §11.2).
    bridge::call(m_engine, [this, cfg]() { m_engine->setConfig(cfg); });
    emit statusMessage(tr("Settings saved"));
    emit settingsSaved();
}

void SettingsTab::onAutoDetect() {
    setControlsEnabled(false);
    emit statusMessage(tr("Detecting location…"));
    auto future = bridge::call<location::Location>(m_location, [this]() {
        QString source;
        const auto loc = m_location->detect(m_engine->config(), &source);
        qInfo() << "location source:" << source;
        return loc;
    });
    future.then(this, [this](location::Location loc) {
        setControlsEnabled(true);
        if (loc.latitude == 0.0 && loc.longitude == 0.0 && loc.timezone.isEmpty()) {
            emit statusMessage(tr("Location detection failed"));
            return;
        }
        m_city->setText(loc.city);
        m_latitude->setValue(loc.latitude);
        m_longitude->setValue(loc.longitude);
        m_timezone->setCurrentText(loc.timezone);
        emit statusMessage(
            tr("Location detected — review and press Save to apply"));
    });
}

}  // namespace johona::gui
