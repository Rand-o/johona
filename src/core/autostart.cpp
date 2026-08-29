// autostart.cpp — host autostart entry implementation.

#include "autostart.hpp"

#include <QDir>
#include <QFile>
#include <QStandardPaths>

namespace johona::autostart {

QString desktopFilePath() {
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) +
           QStringLiteral("/autostart/") + APP_ID + QStringLiteral(".desktop");
}

bool setEnabled(bool enabled) {
    if (!enabled) {
        const QString path = desktopFilePath();
        return !QFileInfo::exists(path) || QFile::remove(path);
    }
    const QString path = desktopFilePath();
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    f.write(
        "[Desktop Entry]\n"
        "Type=Application\n"
        "Name=Johona Wallpaper\n"
        "Comment=Solar-segment wallpaper scheduler\n"
        "Exec=flatpak run top.spelunk.johona\n"
        "Terminal=false\n"
        "X-GNOME-Autostart-enabled=true\n");
    return true;
}

bool isEnabled() { return QFileInfo::exists(desktopFilePath()); }

}  // namespace johona::autostart
