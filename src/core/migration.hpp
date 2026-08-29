// migration.hpp — one-time data migration from kWallpaper (spec §9.4).
//
// Trigger: first launch, when the old `~/.var/app/top.spelunk.kwallpaper/`
// exists and no Johona config exists yet.  Idempotent — skipped once a
// Johona config is present.  The old dir is left untouched (the Flatpak
// manifest grants it read-only).
//
//   Old (`~/.var/app/top.spelunk.kwallpaper/…`)   New
//   config/kwallpaper/config.json                 config.json (schema-mapped, v1)
//   config/kwallpaper/themes/                     $XDG_DATA_HOME/johona/themes/
//   config/kwallpaper/shuffle-list.json           shuffle-list.json
//
// The old cache dir (thumbnails, schedule backups) is NOT migrated.

#pragma once

#include <QStringList>

#include "config.hpp"

namespace johona::migration {

struct Report {
    bool ran = false;
    bool configConverted = false;
    int themesCopied = 0;
    bool shuffleCopied = false;
    QStringList logLines;
    QStringList errors;

    QString summary() const;
};

/// The old app's base dir.  Inside the Flatpak sandbox $HOME is
/// ~/.var/app/top.spelunk.johona, so the real home is derived by stripping
/// that suffix; on the host $HOME is already the real home.
QString oldAppBaseDir();

/// Run the migration if the trigger conditions hold (old dir exists, no
/// Johona config yet).  Returns an empty report (ran == false) otherwise.
Report migrateIfNeeded();

/// Explicit migration from `oldBase` to `targets` (for tests).
Report migrate(const QString& oldBase, const config::Paths& targets);

}  // namespace johona::migration
