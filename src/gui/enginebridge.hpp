// enginebridge.hpp — invoke blocking engine/location calls on their owning
// thread (spec §11.4).  The GUI thread never blocks on D-Bus or file I/O:
// every call is queued to the engine thread, and results come back through
// a QFuture (or a signal for void calls).

#pragma once

#include <QFuture>
#include <QMetaObject>
#include <QPointer>
#include <QPromise>

#include <functional>
#include <memory>

namespace johona::bridge {

/// Queue `fn` for execution on `obj`'s thread.  The returned future
/// completes with the result once the call has run.
template <typename T>
QFuture<T> call(QObject* obj, std::function<T()> fn) {
    auto promise = std::make_unique<QPromise<T>>();
    QFuture<T> future = promise->future();
    QPointer<QObject> guard = obj;
    QMetaObject::invokeMethod(
        obj,
        [p = promise.get(), fn = std::move(fn), guard]() {
            if (guard)
                p->addResult(fn());
            else
                p->addResult(T{});  // engine gone (shutdown) — complete empty
            // finish() completes the future so QFuture::then continuations
            // run.  Do NOT rely on the QPromise destructor: its
            // cancelAndFinish() marks the future *canceled*, and Qt's
            // continuation machinery then propagates the cancellation
            // instead of invoking the callback (the future would never
            // deliver a result).
            p->finish();
            delete p;
        },
        Qt::QueuedConnection);
    promise.release();  // the queued lambda owns it now
    return future;
}

/// Queue a fire-and-forget call on `obj`'s thread.
inline void call(QObject* obj, std::function<void()> fn) {
    QPointer<QObject> guard = obj;
    QMetaObject::invokeMethod(
        obj, [fn = std::move(fn), guard]() {
            if (guard)
                fn();
        },
        Qt::QueuedConnection);
}

}  // namespace johona::bridge
