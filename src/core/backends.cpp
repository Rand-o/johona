// backends.cpp — wallpaper backend implementations.

#include "backends.hpp"

#include "portalwaiter.hpp"

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusError>
#include <QDBusInterface>
#include <QDBusMessage>
#include <QDateTime>
#include <QGuiApplication>
#include <QProcess>
#include <QScreen>
#include <QUrl>

namespace johona::backends {

namespace {

constexpr qint64 kProbeCacheMs = 10000;  // 10 s probe cache
constexpr int kCallTimeoutMs = 10000;

bool probeFresh(qint64 cacheAt) {
    return QDateTime::currentMSecsSinceEpoch() - cacheAt > kProbeCacheMs;
}

}  // namespace

QDBusConnection defaultBus(const QDBusConnection::BusType type) {
    return type == QDBusConnection::SystemBus ? QDBusConnection::systemBus()
                                              : QDBusConnection::sessionBus();
}

int runProcess(const QString& program, const QStringList& args, QString* out, QString* err,
               int timeoutMs) {
    QProcess p;
    p.start(program, args);
    if (!p.waitForStarted(3000))
        return -1;
    if (!p.waitForFinished(timeoutMs)) {
        p.kill();
        p.waitForFinished(1000);
        return -1;
    }
    if (out)
        *out = QString::fromLocal8Bit(p.readAllStandardOutput()).trimmed();
    if (err)
        *err = QString::fromLocal8Bit(p.readAllStandardError()).trimmed();
    return p.exitStatus() == QProcess::NormalExit ? p.exitCode() : -1;
}

// ---------------------------------------------------------------------------
// Plasma (session D-Bus org.kde.plasmashell)
// ---------------------------------------------------------------------------

namespace {

/// The documented Plasma wallpaper API lives at /Core/Workspace
/// (org.kde.PlasmaWorkspace).  On some Plasma setups — notably headless / RDP
/// sessions where the workspace object is never exported — plasmashell
/// instead exposes the same setWallpaper/wallpaper methods on /PlasmaShell
/// (org.kde.PlasmaShell), which is the object xdg-desktop-portal-kde calls.
/// Probe both and prefer /Core/Workspace.
PlasmaBackend::Target findPlasmaTarget(const QDBusConnection& conn) {
    if (!conn.isConnected())
        return {};
    struct Candidate {
        const char* path;
        const char* iface;
        const char* marker;
    };
    const Candidate candidates[] = {
        {"/Core/Workspace", "org.kde.PlasmaWorkspace", "org.kde.PlasmaWorkspace"},
        {"/PlasmaShell", "org.kde.PlasmaShell", "setWallpaper"},
    };
    for (const auto& c : candidates) {
        QDBusInterface shell(QStringLiteral("org.kde.plasmashell"),
                             QLatin1String(c.path),
                             QStringLiteral("org.freedesktop.DBus.Introspectable"),
                             conn);
        const QDBusMessage reply = shell.call(QStringLiteral("Introspect"));
        if (reply.type() == QDBusMessage::ReplyMessage) {
            const QString xml = reply.arguments().value(0).toString();
            if (xml.contains(QLatin1String(c.iface)) &&
                xml.contains(QLatin1String(c.marker)))
                return PlasmaBackend::Target{QLatin1String(c.path),
                                             QLatin1String(c.iface)};
        }
    }
    return {};
}

}  // namespace

PlasmaBackend::PlasmaBackend(BusProvider bus) : m_bus(std::move(bus)) {}

bool PlasmaBackend::isAvailable() {
    if (!probeFresh(m_probeCacheAt))
        return m_probeCache;
    // Introspection, not Peer.Ping: a half-started plasmashell may own the
    // bus name without exporting either wallpaper object.
    m_target = findPlasmaTarget(m_bus(QDBusConnection::SessionBus));
    m_probeCache = m_target.valid();
    m_probeCacheAt = QDateTime::currentMSecsSinceEpoch();
    return m_probeCache;
}

int PlasmaBackend::screenCount(const QDBusConnection& conn) const {
    // KWin scripting first: the documented Plasma API.  KWin 5/6 exposes
    // the physical screens as the read-only `workspace.screens` list
    // (NOT `desktops` — that is the list of virtual desktops).
    QDBusInterface kwin(QStringLiteral("org.kde.KWin"), QStringLiteral("/KWin"),
                        QStringLiteral("org.kde.KWin"), conn);
    for (const char* script : {"workspace.screens.length", "screens.length"}) {
        const QDBusMessage reply =
            kwin.call(QStringLiteral("evaluateScript"), QString::fromLatin1(script));
        if (reply.type() == QDBusMessage::ReplyMessage && !reply.arguments().isEmpty()) {
            const int n = reply.arguments().first().toInt();
            if (n > 0)
                return n;
        }
    }
    // Fallback: the session's screen list.  The app always runs inside the
    // user's desktop session, so Qt knows the physical screens even when
    // KWin scripting is unavailable (e.g. RDP/headless KWin builds).
    if (QGuiApplication::instance()) {
        const int n = QGuiApplication::screens().size();
        if (n > 0)
            return n;
    }
    return 1;
}

QString PlasmaBackend::wallpaperImage(const QDBusConnection& conn,
                                      const Target& target, uint screenId) const {
    QDBusInterface shell(QStringLiteral("org.kde.plasmashell"), target.objectPath,
                         target.interfaceName, conn);
    const QDBusMessage reply = shell.call(QStringLiteral("wallpaper"), screenId);
    if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty())
        return {};
    // The a{sv} reply can arrive as a raw QDBusArgument (the QVariantMap
    // type annotation is not always applied — observed on Qt 6.9.3 behind
    // the Flatpak session-bus proxy), where .toMap() yields an empty map.
    // Handle both shapes.
    const QVariant v = reply.arguments().first();
    QVariantMap props;
    if (v.canConvert<QVariantMap>()) {
        props = v.toMap();
    } else {
        QDBusArgument arg = v.value<QDBusArgument>();
        arg >> props;
    }
    return props.value(QStringLiteral("Image")).toString();
}

SetResult PlasmaBackend::setWallpaper(const QString& imagePath) {
    SetResult result;
    const QDBusConnection conn = m_bus(QDBusConnection::SessionBus);
    if (!m_target.valid())
        m_target = findPlasmaTarget(conn);
    if (!m_target.valid()) {
        result.message = QStringLiteral("Plasma wallpaper interface not available");
        return result;
    }
    // Non-const: QDBusInterface::call is non-const in Qt 6.
    QDBusInterface shell(QStringLiteral("org.kde.plasmashell"),
                         m_target.objectPath, m_target.interfaceName, conn);
    if (!shell.isValid()) {
        result.message = QStringLiteral("Plasma workspace interface not available");
        return result;
    }

    // Qt 6's QDBusInterface::call returns a QDBusMessage (QDBusReply is
    // gone), so replies are inspected by message type.
    //
    // Every screen gets the image (multi-monitor setups).
    const int screens = screenCount(conn);
    const QString url = QUrl::fromLocalFile(imagePath).toString();
    int affected = 0;
    QString firstError;
    for (int i = 0; i < screens; i++) {
        QVariantMap props;
        props[QStringLiteral("Image")] = url;
        // The screen index is uint32 on the wire ('u'); an int would be
        // marshalled as 'i' and the call would fail with "No such method".
        const QDBusMessage reply = shell.call(
            QStringLiteral("setWallpaper"), QStringLiteral("org.kde.image"), props,
            static_cast<quint32>(i));
        if (reply.type() == QDBusMessage::ReplyMessage) {
            affected++;
        } else if (firstError.isEmpty()) {
            firstError = reply.errorMessage();
        }
    }
    result.screensAffected = affected;
    if (affected > 0) {
        result.success = true;
        result.message = QStringLiteral("Wallpaper set on %1 of %2 screen(s)")
                             .arg(affected)
                             .arg(screens);
    } else {
        result.message = QStringLiteral("Plasma rejected the wallpaper: %1")
                             .arg(firstError.isEmpty() ? QStringLiteral("no screens accepted")
                                                       : firstError);
    }
    return result;
}

QString PlasmaBackend::currentWallpaper() const {
    const QDBusConnection conn = m_bus(QDBusConnection::SessionBus);
    Target target = m_target.valid() ? m_target : findPlasmaTarget(conn);
    if (!target.valid())
        return {};
    // Check every screen: the app sets the same image on all of them, so
    // any screen that differs means drift.  Return the first differing
    // image (which will not match the desired one), or the common image
    // when all screens agree.
    const int screens = screenCount(conn);
    QString common;
    for (int i = 0; i < screens; i++) {
        const QString img = wallpaperImage(conn, target, static_cast<uint>(i));
        if (img.isEmpty())
            continue;  // screen without a configured image
        if (common.isEmpty()) {
            common = img;
        } else if (img != common) {
            return img;
        }
    }
    return common;
}

// ---------------------------------------------------------------------------
// XDG Wallpaper portal (org.freedesktop.portal.Wallpaper on
// org.freedesktop.portal.Desktop — note the capital D in the bus name).
//
// NOTE: org.freedesktop.portal.Background is a *different* portal (autostart
// / background-activity permission); it does not set wallpapers.
//
// The Wallpaper portal is asynchronous: SetWallpaperURI returns a Request
// object path and the outcome arrives as a Response(uint32, a{sv}) signal
// on that object (0 = success, 1 = cancelled, 2 = error).  The first call
// from an app triggers a one-time "Allow <app> to set backgrounds?" dialog
// (permission is then stored by the permission store); show-preview=false
// keeps later calls silent.
// ---------------------------------------------------------------------------

namespace {

constexpr int kPortalTimeoutMs = 60000;  // be patient for the one-time dialog

}  // namespace

PortalBackend::PortalBackend(BusProvider bus) : m_bus(std::move(bus)) {}

bool PortalBackend::isAvailable() {
    if (!probeFresh(m_probeCacheAt))
        return m_probeCache;
    const QDBusConnection conn = m_bus(QDBusConnection::SessionBus);
    bool ok = false;
    if (conn.isConnected()) {
        // Introspect the portal object and require the Wallpaper interface
        // (a bare Peer.Ping would be true for any portal service).
        const QDBusMessage introspect = QDBusMessage::createMethodCall(
            QStringLiteral("org.freedesktop.portal.Desktop"),
            QStringLiteral("/org/freedesktop/portal/desktop"),
            QStringLiteral("org.freedesktop.DBus.Introspectable"),
            QStringLiteral("Introspect"));
        const QDBusMessage r = conn.call(introspect, QDBus::Block, 3000);
        if (r.type() == QDBusMessage::ReplyMessage && !r.arguments().isEmpty())
            ok = r.arguments().first().toString().contains(
                QStringLiteral("org.freedesktop.portal.Wallpaper"));
    }
    m_probeCache = ok;
    m_probeCacheAt = QDateTime::currentMSecsSinceEpoch();
    return ok;
}

SetResult PortalBackend::setWallpaper(const QString& imagePath) {
    SetResult result;
    const QDBusConnection conn = m_bus(QDBusConnection::SessionBus);
    if (!conn.isConnected()) {
        result.message = QStringLiteral("Session bus not connected");
        return result;
    }

    // SetWallpaperURI(parent, uri, options).  set-on is required by the KDE
    // implementation ("background" | "lockscreen" | "both"); show-preview
    // false avoids a preview dialog on every change (the one-time permission
    // dialog still appears the first time).
    QVariantMap options;
    options[QStringLiteral("set-on")] = QStringLiteral("background");
    options[QStringLiteral("show-preview")] = false;

    QDBusInterface portal(QStringLiteral("org.freedesktop.portal.Desktop"),
                          QStringLiteral("/org/freedesktop/portal/desktop"),
                          QStringLiteral("org.freedesktop.portal.Wallpaper"), conn);
    const QDBusMessage reply = portal.call(
        QStringLiteral("SetWallpaperURI"), QString(),
        QUrl::fromLocalFile(imagePath).toString(), options);
    if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty()) {
        result.message = QStringLiteral("XDG portal rejected the wallpaper: %1")
                             .arg(reply.errorMessage());
        return result;
    }

    // The portal is asynchronous: wait for the Request::Response signal.
    const QString requestPath = reply.arguments().first().toString();
    const PortalResponseWaiter waiter(conn, requestPath, kPortalTimeoutMs);
    switch (waiter.response()) {
    case 0:
        result.success = true;
        result.screensAffected = 1;
        result.message = QStringLiteral("Wallpaper set via XDG Wallpaper portal");
        break;
    case 1:
        result.message = QStringLiteral("Wallpaper request cancelled by the user");
        break;
    default:
        result.message = QStringLiteral("XDG portal wallpaper request failed "
                                        "(response %1)")
                             .arg(waiter.response());
        break;
    }
    return result;
}

// ---------------------------------------------------------------------------
// GNOME (gsettings subprocess)
// ---------------------------------------------------------------------------

GnomeBackend::GnomeBackend(ProcessRunner run, BusProvider bus)
    : m_run(std::move(run)), m_bus(std::move(bus)) {}

bool GnomeBackend::isAvailable() {
    if (!probeFresh(m_probeCacheAt))
        return m_probeCache;
    // A running GNOME shell is required when we can check: on other
    // desktops (e.g. Plasma) gsettings still "succeeds" but nothing reads
    // the value — a silent no-op we must not advertise as a working
    // backend.  (The Flatpak manifest grants --talk-name=org.gnome.Shell so
    // this check works through the sandboxed session-bus proxy.)  Without a
    // session bus we fall back to the schema check alone.
    bool ok = false;
    const QDBusConnection conn = m_bus(QDBusConnection::SessionBus);
    if (conn.isConnected()) {
        QDBusInterface shell(QStringLiteral("org.gnome.Shell"),
                             QStringLiteral("/org/gnome/Shell"),
                             QStringLiteral("org.freedesktop.DBus.Peer"), conn);
        const QDBusMessage r = shell.call(QStringLiteral("Ping"));
        ok = (r.type() == QDBusMessage::ReplyMessage);
    } else {
        ok = true;
    }
    if (ok) {
        // The schema must also actually be queryable in this runtime.
        QString out, err;
        ok = m_run(QStringLiteral("gsettings"),
                   {QStringLiteral("list-recursively"),
                    QStringLiteral("org.gnome.desktop.background")},
                   &out, &err, 5000) == 0;
    }
    m_probeCache = ok;
    m_probeCacheAt = QDateTime::currentMSecsSinceEpoch();
    return m_probeCache;
}

SetResult GnomeBackend::setWallpaper(const QString& imagePath) {
    SetResult result;
    const QString url = QUrl::fromLocalFile(imagePath).toString();
    QString out, err;
    int rc = m_run(QStringLiteral("gsettings"),
                   {QStringLiteral("set"), QStringLiteral("org.gnome.desktop.background"),
                    QStringLiteral("picture-uri"), url},
                   &out, &err, 5000);
    if (rc != 0) {
        result.message = QStringLiteral("gsettings set picture-uri failed: %1")
                             .arg(err.isEmpty() ? QStringLiteral("(no output)") : err);
        return result;
    }
    rc = m_run(QStringLiteral("gsettings"),
               {QStringLiteral("set"), QStringLiteral("org.gnome.desktop.background"),
                QStringLiteral("picture-uri-dark"), url},
               &out, &err, 5000);
    result.success = true;
    result.screensAffected = 1;
    result.message = rc == 0
                         ? QStringLiteral("Wallpaper set via gsettings")
                         : QStringLiteral("Wallpaper set (picture-uri-dark failed: %1)")
                               .arg(err.isEmpty() ? QStringLiteral("(no output)") : err);
    return result;
}

QString GnomeBackend::currentWallpaper() const {
    QString out, err;
    const int rc = m_run(QStringLiteral("gsettings"),
                         {QStringLiteral("get"), QStringLiteral("org.gnome.desktop.background"),
                          QStringLiteral("picture-uri")},
                         &out, &err, 5000);
    if (rc != 0)
        return {};
    QString v = out;  // gsettings prints a quoted string: 'file:///...'
    if (v.startsWith(QLatin1Char('\'')) && v.endsWith(QLatin1Char('\'')))
        v = v.mid(1, v.size() - 2);
    return v;
}

// ---------------------------------------------------------------------------
// xdg-settings (subprocess, X11)
// ---------------------------------------------------------------------------

XdgSettingsBackend::XdgSettingsBackend(ProcessRunner run) : m_run(std::move(run)) {}

bool XdgSettingsBackend::isAvailable() {
    if (!probeFresh(m_probeCacheAt))
        return m_probeCache;
    const bool x11 =
        qEnvironmentVariable("XDG_SESSION_TYPE").toLower() == QLatin1String("x11");
    bool ok = false;
    if (x11) {
        QString out, err;
        ok = m_run(QStringLiteral("xdg-settings"),
                   {QStringLiteral("get"), QStringLiteral("background-url")},
                   &out, &err, 5000) == 0;
    }
    m_probeCache = ok;
    m_probeCacheAt = QDateTime::currentMSecsSinceEpoch();
    return ok;
}

SetResult XdgSettingsBackend::setWallpaper(const QString& imagePath) {
    SetResult result;
    const QString url = QUrl::fromLocalFile(imagePath).toString();
    QString out, err;
    const int rc = m_run(QStringLiteral("xdg-settings"),
                         {QStringLiteral("set"), QStringLiteral("background-url"), url},
                         &out, &err, 5000);
    if (rc == 0) {
        result.success = true;
        result.screensAffected = 1;
        result.message = QStringLiteral("Wallpaper set via xdg-settings");
    } else {
        result.message = QStringLiteral("xdg-settings set failed: %1")
                             .arg(err.isEmpty() ? QStringLiteral("(no output)") : err);
    }
    return result;
}

QString XdgSettingsBackend::currentWallpaper() const {
    QString out, err;
    const int rc = m_run(QStringLiteral("xdg-settings"),
                         {QStringLiteral("get"), QStringLiteral("background-url")},
                         &out, &err, 5000);
    return rc == 0 ? out : QString{};
}

// ---------------------------------------------------------------------------
// manager
// ---------------------------------------------------------------------------

std::vector<std::unique_ptr<IWallpaperBackend>>
makeAllBackends(BusProvider bus, ProcessRunner run) {
    // Explicit push_back: a braced initializer list would create const
    // unique_ptr elements (initializer_list), which cannot be moved.
    std::vector<std::unique_ptr<IWallpaperBackend>> v;
    v.push_back(std::make_unique<PlasmaBackend>(bus));
    v.push_back(std::make_unique<PortalBackend>(bus));
    v.push_back(std::make_unique<GnomeBackend>(run, bus));
    v.push_back(std::make_unique<XdgSettingsBackend>(run));
    return v;
}

BackendManager::BackendManager(BusProvider bus, ProcessRunner run)
    : m_bus(std::move(bus)), m_run(std::move(run)) {}

void BackendManager::setOverride(const QString& overrideId) {
    m_override = overrideId;
    m_cached.reset();
    m_cachedId.clear();
    m_cachedAt = 0;
}

std::unique_ptr<IWallpaperBackend> BackendManager::detect() const {
    // Probe order: Plasma → Portal → GNOME → xdg-settings (spec §7.3).
    for (auto& b : makeAllBackends(m_bus, m_run)) {
        if (b->isAvailable())
            return std::move(b);  // move out of the vector element
    }
    return nullptr;
}

IWallpaperBackend* BackendManager::backend() {
    if (!m_cached) {
        if (m_override != QStringLiteral("auto")) {
            for (auto& b : makeAllBackends(m_bus, m_run)) {
                if (b->id() == m_override) {
                    m_cached = std::move(b);
                    break;
                }
            }
            // Forced backend: use it even if the probe fails — the set
            // attempt will report the error (no silent auto-fallback).
        } else {
            m_cached = detect();
        }
        m_cachedId = m_cached ? m_cached->id() : QString{};
        m_cachedAt = QDateTime::currentMSecsSinceEpoch();
    }
    return m_cached.get();
}

QString BackendManager::detectedId() const {
    if (m_cachedAt == 0) {
        // Probe without caching into the mutable slot (const context).
        for (auto& b : makeAllBackends(m_bus, m_run))
            if (b->isAvailable())
                return b->id();
        return {};
    }
    return m_cachedId;
}

QHash<QString, bool> BackendManager::probeAll() const {
    QHash<QString, bool> out;
    for (auto& b : makeAllBackends(m_bus, m_run))
        out.insert(b->id(), b->isAvailable());
    return out;
}

}  // namespace johona::backends
