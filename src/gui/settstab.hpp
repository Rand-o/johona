// settstab.hpp — Settings tab (spec §11.2): scheduler, backend, location,
// appearance, autostart.  Save → atomic config write + hot-reload of the
// running scheduler.

#pragma once

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QWidget>

#include "engine.hpp"
#include "location.hpp"

namespace johona::gui {

class SettingsTab : public QWidget {
    Q_OBJECT
public:
    SettingsTab(Engine* engine, location::LocationManager* locationManager,
                QWidget* parent = nullptr);

    /// Load values from the engine's current config.
    void reload();

signals:
    /// Emitted after a successful save (GUI hot-reloads the engine).
    void settingsSaved();
    void statusMessage(const QString& message);

private slots:
    void onSave();
    void onAutoDetect();
    void onBackendChanged(int index);

private:
    config::Config collect() const;
    void setControlsEnabled(bool on);

    Engine* m_engine;
    location::LocationManager* m_location;

    // Scheduler
    QSpinBox* m_safetyInterval;
    QCheckBox* m_dailyShuffle;
    QCheckBox* m_startOnLaunch;

    // Backend
    QComboBox* m_backend;
    QLabel* m_backendHint;

    // Location
    QLineEdit* m_city;
    QDoubleSpinBox* m_latitude;
    QDoubleSpinBox* m_longitude;
    QComboBox* m_timezone;
    QCheckBox* m_tzAutoUpdate;
    QPushButton* m_detectBtn;

    // Appearance
    QComboBox* m_appearance;

    // Autostart
    QCheckBox* m_autostart;

    QPushButton* m_saveBtn;
};

}  // namespace johona::gui
