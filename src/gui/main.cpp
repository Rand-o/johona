// main.cpp — Johona Wallpaper entry point (spec §11).
//
//  - Single instance: per-user QLocalServer socket; a second launch sends
//    "activate" and exits, the running instance raises its window.
//  - One-time kWallpaper migration runs before the window is shown.
//  - The Engine (and LocationManager) live on a dedicated QThread; the GUI
//    calls them via queued invocations (spec §11.4).

#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QFileInfo>
#include <QLocalServer>
#include <QLocalSocket>
#include <QMessageBox>
#include <QThread>
#include <QTimer>

#include "autostart.hpp"
#include "engine.hpp"
#include "location.hpp"
#include "migration.hpp"

#include "mainwindow.hpp"

using namespace johona;

namespace {

const char* kInstanceSocket = "johona-instance";

/// True when we are the second (or later) launch: connect to the running
/// instance, ask it to activate, and report that we should exit.
bool activateRunningInstance() {
    QLocalSocket socket;
    socket.connectToServer(kInstanceSocket);
    if (!socket.waitForConnected(500))
        return false;
    socket.write("activate");
    socket.flush();
    socket.waitForBytesWritten(500);
    socket.disconnectFromServer();
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("spelunk"));
    QCoreApplication::setApplicationName(QStringLiteral("johona"));
    QCoreApplication::setApplicationVersion(QStringLiteral("1.0.0"));
    QGuiApplication::setDesktopFileName(QStringLiteral("top.spelunk.johona"));
    // Closing the window hides to the tray; only Quit exits.
    QApplication::setQuitOnLastWindowClosed(false);

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Johona Wallpaper"));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.process(app);

    // ---- single instance -------------------------------------------------
    QLocalServer server;
    if (!server.listen(kInstanceSocket)) {
        // The socket exists but is stale (crashed instance) → remove and
        // retry once; if that still fails, somebody else is running.
        QLocalServer::removeServer(kInstanceSocket);
        if (!server.listen(kInstanceSocket)) {
            if (activateRunningInstance())
                return 0;
            qWarning() << "Could not create the instance socket:"
                       << server.errorString();
            return 1;
        }
    }

    // ---- one-time migration from kWallpaper ------------------------------
    const migration::Report report = migration::migrateIfNeeded();
    if (report.ran)
        qInfo() << "Migrated kWallpaper data to Johona:" << report.summary();

    // ---- engine on a dedicated thread ------------------------------------
    // The engine's methods are synchronous/blocking (D-Bus, subprocesses,
    // file I/O); the GUI never calls them directly (spec §11.4).
    //
    // Heap-allocated: they must outlive the event loop and be deleted only
    // after the engine thread has finished (their timers are owned by that
    // thread; destroying them from the GUI thread while it runs corrupts
    // timer ownership).
    QThread engineThread;
    engineThread.setObjectName(QStringLiteral("engine"));

    Engine::Deps deps;
    deps.config = config::load();
    const bool startScheduler = deps.config.startSchedulerOnLaunch;
    auto* engine = new Engine(std::move(deps));
    engine->moveToThread(&engineThread);

    location::LocationManager::Deps locDeps;
    auto* locationManager = new location::LocationManager(locDeps);
    locationManager->moveToThread(&engineThread);

    gui::MainWindow window(engine, locationManager, report);
    QObject::connect(&server, &QLocalServer::newConnection, &window, [&server, &window]() {
        QLocalSocket* client = server.nextPendingConnection();
        if (!client)
            return;
        QObject::connect(client, &QLocalSocket::readyRead, client, [client, &window]() {
            if (client->readAll().contains("activate"))
                window.showAndActivate();
            client->deleteLater();
        });
        QObject::connect(client, &QLocalSocket::disconnected, client, &QObject::deleteLater);
    });

    engineThread.start();

    // Hot-start the scheduler when configured (after the thread is up).
    if (startScheduler)
        QMetaObject::invokeMethod(engine, &Engine::start, Qt::QueuedConnection);

    window.show();
    const int rc = app.exec();

    // ---- orderly shutdown -------------------------------------------------
    // Stop the engine on its own thread (queued, so it runs there), quit
    // the thread's event loop, and only then destroy the thread-owned
    // objects.  Bounded wait: the longest in-flight engine operation is the
    // portal response wait (60 s) plus the 5 s apply retry.
    QMetaObject::invokeMethod(engine, &Engine::stop, Qt::QueuedConnection);
    engineThread.quit();
    if (!engineThread.wait(90000))
        qWarning() << "Engine thread did not exit in time";
    delete engine;
    delete locationManager;
    return rc;
}
