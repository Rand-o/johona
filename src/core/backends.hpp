// backends.hpp — pluggable wallpaper backends (spec §7).
//
//  - PlasmaBackend:    session D-Bus org.kde.plasmashell, /Core/Workspace
//                      (org.kde.PlasmaWorkspace) with fallback to /PlasmaShell
//                      (org.kde.PlasmaShell — the object xdg-desktop-portal-kde
//                      uses; present on headless/RDP Plasma sessions where
//                      /Core/Workspace is not exported)
//  - PortalBackend:    session D-Bus org.freedesktop.portal.Wallpaper
//                      (org.freedesktop.portal.Desktop, SetWallpaperURI)
//  - GnomeBackend:     `gsettings` subprocess (org.gnome.desktop.background)
//  - XdgSettingsBackend: `xdg-settings` subprocess (X11)
//
// D-Bus backends use persistent QDBusConnections; subprocess backends use
// QProcess with a 5 s timeout.  Both D-Bus connection and process execution
// are injectable so tests run against mocks (spec §7.4).

#pragma once

#include <QDBusConnection>
#include <QHash>
#include <QString>
#include <memory>
#include <vector>

namespace johona::backends {

struct SetResult {
    bool success = false;
    QString message;
    int screensAffected = 0;
};

class IWallpaperBackend {
public:
    virtual ~IWallpaperBackend() = default;
    /// Stable id: "plasma" | "portal" | "gnome" | "xdg_settings".
    virtual QString id() const = 0;
    virtual QString displayName() const = 0;
    /// Live probe (cheap, result briefly cached).
    virtual bool isAvailable() = 0;
    virtual SetResult setWallpaper(const QString& imagePath) = 0;
    /// Optional; empty string when unsupported.
    virtual QString currentWallpaper() const { return {}; }
};

/// Injectable D-Bus connection provider (default: QDBusConnection::sessionBus/systemBus).
using BusProvider = std::function<QDBusConnection(const QDBusConnection::BusType)>;
/// Injectable process runner (default: QProcess with timeout).
/// Returns the exit code, or -1 on spawn/timeout failure.
using ProcessRunner =
    std::function<int(const QString& program, const QStringList& args, QString* stdout,
                      QString* stderr, int timeoutMs)>;

QDBusConnection defaultBus(const QDBusConnection::BusType type);
int runProcess(const QString& program, const QStringList& args, QString* out, QString* err,
               int timeoutMs = 5000);

class PlasmaBackend : public IWallpaperBackend {
public:
    explicit PlasmaBackend(BusProvider bus = defaultBus);
    QString id() const override { return "plasma"; }
    QString displayName() const override { return "Plasma"; }
    bool isAvailable() override;
    SetResult setWallpaper(const QString& imagePath) override;
    QString currentWallpaper() const override;

    // Which plasma object to call (set by the probe; see findPlasmaTarget).
    struct Target {
        QString objectPath;
        QString interfaceName;
        bool valid() const { return !objectPath.isEmpty(); }
    };

private:
    /// Number of screens to (de)set: KWin scripting first (the documented
    /// Plasma API), then the session's Qt screen list, then 1.
    int screenCount(const QDBusConnection& conn) const;
    /// The Image URL of one screen ("" when unavailable).
    QString wallpaperImage(const QDBusConnection& conn, const Target& target,
                           uint screenId) const;

    BusProvider m_bus;
    mutable Target m_target;
    mutable qint64 m_probeCacheAt = 0;
    mutable bool m_probeCache = false;
};

class PortalBackend : public IWallpaperBackend {
public:
    explicit PortalBackend(BusProvider bus = defaultBus);
    QString id() const override { return "portal"; }
    QString displayName() const override { return "XDG portal"; }
    bool isAvailable() override;
    SetResult setWallpaper(const QString& imagePath) override;
    // Current wallpaper is not supported by the portal API.

private:
    BusProvider m_bus;
    mutable qint64 m_probeCacheAt = 0;
    mutable bool m_probeCache = false;
};

class GnomeBackend : public IWallpaperBackend {
public:
    explicit GnomeBackend(ProcessRunner run = runProcess, BusProvider bus = defaultBus);
    QString id() const override { return "gnome"; }
    QString displayName() const override { return "GNOME (gsettings)"; }
    bool isAvailable() override;
    SetResult setWallpaper(const QString& imagePath) override;
    QString currentWallpaper() const override;

private:
    ProcessRunner m_run;
    BusProvider m_bus;
    mutable qint64 m_probeCacheAt = 0;
    mutable bool m_probeCache = false;
};

class XdgSettingsBackend : public IWallpaperBackend {
public:
    explicit XdgSettingsBackend(ProcessRunner run = runProcess);
    QString id() const override { return "xdg_settings"; }
    QString displayName() const override { return "xdg-settings (X11)"; }
    bool isAvailable() override;
    SetResult setWallpaper(const QString& imagePath) override;
    QString currentWallpaper() const override;

private:
    ProcessRunner m_run;
    mutable qint64 m_probeCacheAt = 0;
    mutable bool m_probeCache = false;
};

/// Probe order: Plasma → Portal → GNOME → xdg-settings (spec §7.3).
std::vector<std::unique_ptr<IWallpaperBackend>>
makeAllBackends(BusProvider bus, ProcessRunner run);

class BackendManager {
public:
    BackendManager(BusProvider bus = defaultBus, ProcessRunner run = runProcess);

    /// "auto" | "plasma" | "portal" | "gnome" | "xdg_settings".
    void setOverride(const QString& overrideId);
    QString overrideId() const { return m_override; }

    /// The active backend (auto-detected, or the forced one).  Cached;
    /// re-probed lazily.  Non-owning; nullptr when nothing is available.
    IWallpaperBackend* backend();
    /// Id of the auto-detected backend ("" when none).
    QString detectedId() const;
    /// id → available, for the Settings UI.
    QHash<QString, bool> probeAll() const;

private:
    std::unique_ptr<IWallpaperBackend> detect() const;

    BusProvider m_bus;
    ProcessRunner m_run;
    QString m_override = "auto";
    mutable std::unique_ptr<IWallpaperBackend> m_cached;
    mutable QString m_cachedId;  // "" = not probed
    mutable qint64 m_cachedAt = 0;
};

}  // namespace johona::backends
