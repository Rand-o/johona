// settstab.hpp — Settings tab (kWallpaper SettingsPage parity + the
// Johona backend-override group): scheduler, location, wallpaper backend,
// appearance.  Save → atomic config write + hot-reload of the running
// engine.  The color scheme and autostart apply immediately (kWallpaper).

#pragma once

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
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
    /// Emitted after a successful save (the main window rebuilds the
    /// schedule preview, which depends on the location).
    void settingsSaved();
    /// The color scheme changed (applied immediately, kWallpaper parity).
    void schemeChanged(const QString& mode);  // "system"|"light"|"dark"
    void statusMessage(const QString& message);

private slots:
    void onSave();
    void onAutoDetect();
    void onSchemeChanged(int index);
    void onAutostartToggled(bool enabled);

private:
    config::Config collect() const;

    Engine* m_engine;
    location::LocationManager* m_location;

    // Scheduler
    QSpinBox* m_interval;
    QCheckBox* m_runCycle;
    QCheckBox* m_dailyShuffle;
    QCheckBox* m_startOnLaunch;

    // Location
    QLineEdit* m_timezone;
    QDoubleSpinBox* m_lat;
    QDoubleSpinBox* m_lon;
    QPushButton* m_autoDetectBtn;

    // Wallpaper backend (Johona)
    QComboBox* m_backend;

    // Appearance
    QCheckBox* m_autostart;
    QComboBox* m_scheme;

    QPushButton* m_saveBtn;
};

}  // namespace johona::gui
