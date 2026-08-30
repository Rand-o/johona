// settstab.hpp — Settings page (redesign mockup): card-based layout
// (Scheduler / Location / Wallpaper backend / Appearance) with switch
// rows, a 660 px scrollable column, and Revert/Save in the header.
// Save → atomic config write + hot-reload of the running engine. The
// color scheme and autostart apply immediately (kWallpaper parity).

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
    void updateBackendHint();

    Engine* m_engine;
    location::LocationManager* m_location;

    // Scheduler
    QSpinBox* m_interval;
    class ToggleSwitch* m_runCycle;
    class ToggleSwitch* m_dailyShuffle;
    class ToggleSwitch* m_startOnLaunch;
    class ToggleSwitch* m_autostart;

    // Location
    QLineEdit* m_timezone;
    QDoubleSpinBox* m_lat;
    QDoubleSpinBox* m_lon;
    QPushButton* m_autoDetectBtn;

    // Wallpaper backend (Johona)
    QComboBox* m_backend;
    QLabel* m_backendHint;

    // Appearance
    class SchemeCard* m_schemeSys;
    class SchemeCard* m_schemeLight;
    class SchemeCard* m_schemeDark;

    QPushButton* m_saveBtn;
    int m_backendToken = 0;
};

}  // namespace johona::gui
