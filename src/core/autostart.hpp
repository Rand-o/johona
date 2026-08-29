// autostart.hpp — host autostart entry (spec §11.2).
//
// Writes/removes `~/.config/autostart/top.spelunk.johona.desktop` with
// `Exec=flatpak run top.spelunk.johona`.  Inside the Flatpak sandbox
// `~/.config` is private, so the manifest grants
// `--filesystem=xdg-config/autostart` to reach the host autostart dir.

#pragma once

#include <QString>

namespace johona::autostart {

const char APP_ID[] = "top.spelunk.johona";

/// The .desktop file path (GenericConfigLocation/autostart/...).
QString desktopFilePath();

/// Write (enabled) or remove (disabled) the autostart entry.
bool setEnabled(bool enabled);

bool isEnabled();

}  // namespace johona::autostart
