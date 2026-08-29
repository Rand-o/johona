// portalwaiter.hpp — blocks until an XDG portal Request object emits
// Response.
//
// The XDG portals are asynchronous: a portal method call returns a Request
// object path and the outcome arrives later as a
// Response(uint32 response, a{sv} results) signal on that object
// (0 = success, 1 = cancelled, 2 = error).

#pragma once

#include <QDBusArgument>
#include <QDBusConnection>
#include <QEventLoop>
#include <QObject>
#include <QString>
#include <QTimer>

namespace johona::backends {

/// Blocks the calling thread until the portal Request object at
/// requestPath emits Response or the timeout fires.  response() then
/// returns 0 (success), 1 (cancelled) or 2 (error / timeout / connect
/// failure).  Must be used from a thread with a running event loop (the
/// engine thread in production, the test thread in tests).
class PortalResponseWaiter : public QObject {
    Q_OBJECT

public:
    // conn by value: QDBusConnection is a cheap refcounted handle, and
    // connect() is non-const in Qt 6.
    PortalResponseWaiter(QDBusConnection conn, const QString& requestPath,
                         int timeoutMs) {
        // The slot name is copied by QDBusConnection, so a temporary's
        // constData() is safe.
        const QByteArray slotName = "onResponse(uint, QDBusArgument)";
        const bool connected = conn.connect(
            QStringLiteral("org.freedesktop.portal.Desktop"), requestPath,
            QStringLiteral("org.freedesktop.portal.Request"),
            QStringLiteral("Response"), this, slotName.constData());
        if (!connected)
            return;  // m_response stays 2 (error)
        m_timer.setSingleShot(true);
        m_timer.setInterval(timeoutMs);
        connect(&m_timer, &QTimer::timeout, this, &PortalResponseWaiter::onTimeout);
        m_timer.start();
        m_loop.exec();
    }

    uint response() const { return m_response; }

private slots:
    void onResponse(uint response, QDBusArgument) {
        m_response = response;
        m_loop.quit();
    }
    void onTimeout() { m_loop.quit(); }

private:
    QEventLoop m_loop;
    QTimer m_timer;
    uint m_response = 2;
};

}  // namespace johona::backends
