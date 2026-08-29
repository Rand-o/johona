// dbusrelay.hpp — functor-based D-Bus signal relay.
//
// The SDK's QtDBus headers lack the functor `QDBusConnection::connect`
// overload, so D-Bus signals are connected old-style to a small relay
// QObject whose slot forwards to a std::function handler.  The relay is a
// QObject (not a signal-only object) so it can be parented and destroyed
// with its owner, which also disconnects the D-Bus signal.

#pragma once

#include <QObject>
#include <functional>

namespace johona::dbus {

/// Relays a D-Bus signal carrying a single bool (e.g. login1
/// PrepareForSleep) to a handler.
class BoolRelay : public QObject {
    Q_OBJECT
public:
    using Handler = std::function<void(bool)>;
    explicit BoolRelay(QObject* parent = nullptr) : QObject(parent) {}
    void setHandler(Handler h) { m_handler = std::move(h); }

Q_SIGNALS:
    void fired(bool value);

private Q_SLOTS:
    void onFired(bool value) {
        if (m_handler)
            m_handler(value);
    }

private:
    Handler m_handler;
};

/// Relays a D-Bus signal carrying a single string (e.g. login1
/// SessionStopped / ActiveChanged) to a handler.
class StringRelay : public QObject {
    Q_OBJECT
public:
    using Handler = std::function<void(const QString&)>;
    explicit StringRelay(QObject* parent = nullptr) : QObject(parent) {}
    void setHandler(Handler h) { m_handler = std::move(h); }

Q_SIGNALS:
    void fired(const QString& value);

private Q_SLOTS:
    void onFired(const QString& value) {
        if (m_handler)
            m_handler(value);
    }

private:
    Handler m_handler;
};

/// Relays a D-Bus signal carrying four doubles (Geoclue2
/// org.freedesktop.GeoClue2.Client.LocationUpdated) to a handler.
class LocationRelay : public QObject {
    Q_OBJECT
public:
    using Handler = std::function<void(double, double, double, double)>;
    explicit LocationRelay(QObject* parent = nullptr) : QObject(parent) {}
    void setHandler(Handler h) { m_handler = std::move(h); }

Q_SIGNALS:
    void fired(double latitude, double longitude, double altitude, double accuracy);

private Q_SLOTS:
    void onFired(double latitude, double longitude, double altitude, double accuracy) {
        if (m_handler)
            m_handler(latitude, longitude, altitude, accuracy);
    }

private:
    Handler m_handler;
};

}  // namespace johona::dbus
