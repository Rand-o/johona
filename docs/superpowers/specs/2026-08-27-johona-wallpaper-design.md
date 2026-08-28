# Johona Wallpaper — Design Specification

| | |
|---|---|
| **Status** | Approved (brainstorming complete) |
| **Date** | 2026-08-27 |
| **App ID** | `top.spelunk.johona` |
| **Binary** | `johona` |
| **Supersedes** | kWallpaper 1.1.0 (`top.spelunk.kwallpaper`, Python/PyQt6) |

## 1. Background

kWallpaper 1.1.0 is a Python/PyQt6 Flatpak app that changes the desktop
wallpaper by time-of-day, driven by `.ddw` (WinDynamicDesktop) theme zip
files. It works, but it is Plasma-only, uses the `astral` library for sun
math (which produces different boundary times than WinDynamicDesktop's own
`suncalc`-based timing), shells out to `gdbus` for every D-Bus call and
parses stdout with regexes, and carries a legacy fixed-offset time model
alongside the WDD sun-segment model.

Johona Wallpaper is a C++/Qt 6 rewrite that keeps kWallpaper's feature set,
makes timing WDD-identical by porting `suncalc` (the algorithm WDD actually
uses) directly, and generalizes wallpaper application to any Linux desktop
through pluggable backends.

## 2. Goals and non-goals

### Goals

- Feature parity with kWallpaper 1.1.0 (enumerated in §3).
- WDD-identical sun-segment timing via a faithful C++ port of
  [mourner/suncalc](https://github.com/mourner/suncalc) (MIT) — **fully
  offline**: pure math from lat/lon + system clock + local tzdata. The only
  optional network use in the whole app is one-time location auto-detect.
- Multi-DE wallpaper backends (Plasma D-Bus, XDG portal, GNOME gsettings,
  xdg-settings) instead of Plasma-only.
- Event-driven scheduler (WDD-style) instead of per-minute re-apply.
- Headless, display-independent core library so all logic is unit-testable.
- Flatpak-only distribution, with a one-time data migration from the old
  `top.spelunk.kwallpaper` Flatpak data dir.

### Non-goals

- **No CLI.** The old app's CLI is deliberately dropped; everything is
  reachable from the GUI.
- **No legacy time model.** No `suntime_model` config field, no GUI
  selector, no legacy fallback anywhere (including polar handling, which
  uses WDD's four polar states exclusively).
- **No WDD "user-provided sunrise/sunset times" mode** (not part of
  kWallpaper parity).
- No animated/live wallpapers — static JPEG/PNG only.
- No Flathub publication — the manifest is kept Flathub-ready as a future
  option, but distribution is a local `--user` bundle.
- No Windows/macOS support.

## 3. Feature parity carried over from kWallpaper 1.1.0

- `.ddw`/`.zip` theme import (validated: every image referenced by
  `theme.json` must exist; rejection lists **all** missing images; no
  partial imports), browse, delete.
- WDD sun-segment time model: dawn (−6°) → goldenHourEnd (+6°) →
  goldenHour (+6°) → dusk (−6°) → next dawn; each segment divided evenly
  among its images; four categories (sunrise/day/sunset/night).
- Daily theme shuffle (persist-after-success semantics, date-change
  detection, missed-midnight recovery).
- "Apply now" (picks the image for the current moment).
- 24-hour schedule-preview timeline with per-frame thumbnails +
  current-time marker (kept — a distinctive kWallpaper feature).
- Cross-fade preview using the WDD technique.
- Scheduler tab: start/stop, status, live event log.
- Settings: safety-tick interval, daily-shuffle toggle,
  start-scheduler-on-launch, location (city/lat/lon/timezone + auto-detect),
  appearance (system/light/dark), autostart.
- System tray (start/stop, show/hide, light/dark icons), single-instance.

### Deliberate differences from kWallpaper

| Area | kWallpaper 1.1.0 | Johona |
|---|---|---|
| Stack | Python / PyQt6 | C++20 / Qt 6 |
| Desktops | Plasma 6 only | Any Linux DE (pluggable backends) |
| Sun math | `astral` | `suncalc` port (WDD-identical, fully offline) |
| Time model | legacy + sun, selectable | WDD sun-segment model only |
| CLI | yes | no |
| Scheduler | interval cycle (re-apply every 60 s) | event-driven one-shot + safety tick |
| Preview | disk thumbnail cache (1080p–4K) | in-memory decode, no disk cache |
| D-Bus | `gdbus` subprocess per call + regex parsing | persistent QtDBus connections, structured results |
| Single instance | lockfile + signal | `QLocalServer` |
| Location auto-detect | KDE-specific `kreadconfig5` | Geoclue2 D-Bus (generic) |
| Paths | `~/.var/app/top.spelunk.kwallpaper/…` | XDG / Flatpak per-app under `top.spelunk.johona` |
| Config writes | plain writes | atomic (temp file + rename) |

### Approved improvements ("do it better")

- Persistent QtDBus connections instead of `gdbus` subprocess-per-call +
  regex parsing.
- Structured results instead of stdout/stderr capture hacks.
- Atomic config/state writes (temp file + rename).
- `QLocalServer` single-instance instead of lockfile + signal.
- Location auto-detect via Geoclue2 D-Bus (generic) instead of
  KDE-specific `kreadconfig5`.
- **New feature:** auto-update location when the system timezone changes
  (opt-in toggle, **default OFF**).
- **New feature:** tray menu **Next wallpaper** — when daily shuffle is
  enabled, advance to the next theme in the shuffle list and apply it
  (`advanceShuffle()`, §9.5; tray item in §11).

## 4. Architecture

Headless core library + thin GUI. All logic lives in a `johona-core` static
CMake library (Qt Core/Gui/DBus only — **no Widgets**) so it is testable
without a display. The GUI is a thin Qt Widgets layer.

```
johona/
├── CMakeLists.txt
├── src/
│   ├── main.cpp                  # entry, single-instance, app bootstrap
│   ├── core/                     # johona-core static lib (headless)
│   │   ├── suncalc/              # C++ port of mourner/suncalc (MIT) — pure math
│   │   ├── solar/                # Segments, polar states, image timing, next-change
│   │   ├── config/               # JSON config, XDG paths, validate, atomic writes
│   │   ├── themes/               # discover / import (.ddw/.zip via libzip) / validate / delete
│   │   ├── shuffle/              # shuffle-list state (single writer, persist-after-success)
│   │   ├── backends/             # IWallpaperBackend + plasma/portal/gnome/xdgsettings + manager
│   │   ├── location/             # LocationManager: Geoclue2, TZ watch, tz→coords fallback table
│   │   ├── scheduler/            # event-driven: one-shot + 60s safety + logind + clock-jump
│   │   ├── engine/               # high-level ops: applyTheme / cycle / nextChange / import / delete
│   │   └── migration/            # one-time kwallpaper data migration
│   └── gui/                      # Qt Widgets (thin)
│       ├── MainWindow, ThemesPage, SettingsPage, SchedulerPage
│       ├── PreviewWidget         # cross-fade, in-memory screen-capped cache
│       ├── SchedulePreview       # 24h timeline
│       └── TrayIcon
├── tests/                        # Qt Test — link against johona-core
├── flatpak/                      # manifest, build script
├── data/                         # .desktop, autostart .desktop, metainfo, icons (app + tray light/dark)
├── resources/tz_coordinates.json # timezone → representative coordinates table
└── README.md
```

**Icons.** Reuse the existing kWallpaper icon set (app icon + light/dark
tray icons) from the kwallpaper repo's `icons/` directory — no new icon
artwork for this project.

**Data flow.** User action (Apply) → `engine.applyTheme()` → `solar` picks
the image for the current moment → a `backend` sets the wallpaper → config +
shuffle state are persisted **only after success** → GUI updated via Qt
signals. The scheduler's one-shot timer fires → `engine.cycle()` → same
path. All blocking work (D-Bus, file I/O, image decode, zip extraction)
runs off the GUI thread (QThreadPool workers / the scheduler's own thread).

**CMake targets.** `johona-core` (static lib) + `johona` (GUI exe) + test
binaries. Dependencies: Qt 6 (Core, Gui, Widgets, DBus, Test) + libzip.
JSON is handled by Qt's built-in `QJsonDocument` (no extra JSON library).
C++20, CMake + Ninja.

## 5. Solar engine

### 5.1 `suncalc/` — the algorithm

Faithful C++ port of mourner/suncalc (MIT license — include the license
header/attribution in the ported files). Pure math, zero dependencies.
Exposes sun position and an altitude-crossing solver for **any** angle
(the angles needed are −6°, +6°, and −0.833°). The ported suncalc test
vectors are included 1:1 as the unit-test suite (`test_suncalc`) — this is
the correctness anchor for everything above it.

### 5.2 `solar/` — the WDD segment model

- `Segments { day, dawn(−6°), goldenHourEnd(+6°), goldenHour(+6°), dusk(−6°),
  nextDawn }` — each an optional local datetime (null when the crossing
  doesn't exist that day).
- Segments are computed at **UTC noon of the target date** (WDD's trick,
  which avoids a suncalc date-rollover bug), then converted to local
  wall-clock time via Qt `QTimeZone` (robust IANA + DST handling).
- **Polar states** — WDD's four, handled explicitly (no legacy fallback):
  - **PolarDay** — day images span the full 24 h.
  - **PolarNight** — night images span the full 24 h.
  - **CivilPolarDay** — no dawn/dusk crossing → the night segment is
    skipped.
  - **CivilPolarNight** — no golden-hour crossing → the day segment
    collapses to noon.
- `imageAt(now, seg, theme)` → (category, image index): the segment is
  divided evenly among its images.
- `nextChangeTime(now, seg, theme, currentImage)` → the exact next image
  boundary (used to arm the scheduler's one-shot timer).
- **WDD quirk, preserved:** if `sunriseImageList == dayImageList`, the Day
  segment starts at true sunrise (not +6°); same for sunset.
- WDD's "user-provided sunrise/sunset times" mode is **out of scope** for
  Johona (not in kWallpaper parity).

## 6. Scheduler (event-driven, WDD-style)

- **One-shot `QTimer`** armed at the exact next image boundary → fires →
  `engine.cycle()` → apply image → recompute → re-arm. On start (and after
  any theme change — including a manual Apply or the tray's **Next
  wallpaper**), the scheduler first runs one immediate cycle so the
  wallpaper is synced to the current moment, then arms the one-shot.
- **Safety `QTimer`** — interval = `scheduling.safety_interval` (default
  60 s). Each tick acts **only if needed**:
  - `now >= nextUpdateTime` (missed one-shot / clock jumped) → run cycle;
  - local date changed → daily shuffle advance;
  - system timezone changed → `LocationManager` re-detects + reschedules
    (only when the opt-in toggle is on);
  - monotonic-vs-wall-clock drift beyond a **2 s threshold** → clock jump →
    recompute (the scheduler tracks (monotonic, wall) pairs across ticks);
  - otherwise no-op. **No re-apply** — unlike the old per-minute cycle.
- **Suspend/resume hook:** subscribe to `org.freedesktop.login1`
  `PrepareForSleep`/`Resume` (system D-Bus); on resume, re-check
  immediately if overdue.
- **Failure retry:** a failed wallpaper set is retried exactly **once**
  after **5 s**; if it fails again, it defers to the next safety tick.
- **Re-entrancy lock:** a cycle and a manual apply can never overlap.
- **Event log:** every run emits a log line → GUI Scheduler tab via signal.
- Runs on a dedicated background `QThread` with its own event loop; the GUI
  talks to it via queued signals.

## 7. Wallpaper backends

### 7.1 Abstraction

```cpp
struct SetResult { bool success; QString message; int screensAffected; };

class IWallpaperBackend {
    virtual QString id() const = 0;            // "plasma" | "portal" | "gnome" | "xdg_settings"
    virtual QString displayName() const = 0;
    virtual bool isAvailable() = 0;            // live probe, cheap, briefly cached
    virtual SetResult setWallpaper(const QString& imagePath) = 0;
    virtual QString currentWallpaper() const { return {}; }  // optional
};
```

D-Bus backends use **persistent `QDBusConnection`s** (session + system
bus). Subprocess backends use `QProcess` with a 5 s timeout, run on worker
threads.

### 7.2 Backends

1. **`PlasmaBackend`** (session D-Bus `org.kde.plasmashell`)
   - Probe: `Peer.Ping`.
   - Set: screen count via `org.kde.PlasmaShell.evaluateScript("desktops().length")`,
     then per screen `org.kde.PlasmaShell.setWallpaper("org.kde.image",
     {Image: <file://…>}, N)` as typed QtDBus calls. Success if ≥ 1 screen
     accepted; report the count.
   - Current: `org.kde.PlasmaShell.wallpaper(0)`, with a kreadconfig6/5
     subprocess fallback.
2. **`PortalBackend`** (session D-Bus `org.freedesktop.portal.Background`)
   - Probe: `ListInterfaces` on `/org/freedesktop/portal/desktop`.
   - Set: `SetBackground(token, parent, path, options)` first — themes live
     in the app's per-app dir, which is a real host path
     (`~/.var/app/top.spelunk.johona/…`) readable by the unsandboxed
     portal; fall back to `WriteBackground(token, parent, fd, options)`
     (Unix-fd passing) if the path-based call fails.
   - Current: not supported → the GUI shows the last-applied value from
     config.
   - Some implementations (GNOME) may show a one-time consent prompt —
     expected behavior.
3. **`GnomeBackend`** (`gsettings` subprocess)
   - Probe: `gsettings` exists **and** the `org.gnome.desktop.background`
     schema is queryable (an honest probe: if the Flatpak runtime lacks the
     schema it reports unavailable and detection skips it — on GNOME the
     portal is the primary path anyway).
   - Set: `gsettings set org.gnome.desktop.background picture-uri file://…`
     (+ `picture-uri-dark`).
   - Current: `gsettings get … picture-uri`.
4. **`XdgSettingsBackend`** (`xdg-settings` subprocess, X11)
   - Probe: `XDG_SESSION_TYPE=x11` **and** `xdg-settings get
     background-url` succeeds.
   - Set: `xdg-settings set background-url file://…`.
   - Current: `xdg-settings get background-url`.

### 7.3 `BackendManager`

- User override (`config` `backend.override`) if not `auto` → else probe
  order **Plasma → Portal → GNOME → xdg-settings**; first available wins.
- Detection at startup + on override change; cached, lazily re-probed.
- **No silent auto-fallback on set failure** — failures log a clear error
  naming the backend and pointing at the Settings override.
- Settings UI: "Auto (detected: …)" combo; forcing an unavailable backend
  shows a warning.

### 7.4 Testability

Backends take an injectable D-Bus connection provider + process runner so
tests run against mocks.

## 8. Themes

- `discoverThemes()` → (name, path) per subdirectory of the themes dir
  containing a `.json`.
- `importTheme(zipPath)`:
  1. Validate extension (`.ddw`/`.zip`).
  2. Extract to a **temp** dir (libzip).
  3. Locate `theme.json` (root `*.json`, else recursive).
  4. Parse + `normalizeImageLists`.
  5. **Validate every referenced image exists; on failure reject listing
     ALL missing images and clean up the temp dir (no partial import).**
  6. Move into the themes dir (reject an existing name).
  7. Return metadata (displayName, imageCredits, the four image lists).
- `deleteTheme(nameOrPath)`: remove the dir; safety — refuse paths outside
  the themes dir.
- `imageFilesFor(themeDir, themeData)`: ordered image list from the
  `imageFilename` glob pattern (`*` → number) with numbered-file fallback,
  numerically sorted — the **same list** import validation checks, so
  selection and validation can never disagree.
- `theme.json` (WDD format): `displayName`, `imageCredits`,
  `imageFilename` (pattern with `*`), `sunriseImageList`, `dayImageList`,
  `sunsetImageList`, `nightImageList` (optional `dayHighlight`/
  `nightHighlight`).
- Image formats: JPEG/PNG (Qt native); WebP/GIF if the runtime ships the
  plugins.

## 9. Config and state

### 9.1 Paths (XDG → Flatpak per-app)

| What | Path |
|---|---|
| Config | `$XDG_CONFIG_HOME/johona/config.json` |
| Themes | `$XDG_DATA_HOME/johona/themes/` |
| Cache | `$XDG_CACHE_HOME/johona/` |
| Shuffle state | `$XDG_CONFIG_HOME/johona/shuffle-list.json` |

Under the Flatpak sandbox these resolve to
`~/.var/app/top.spelunk.johona/{config,data,cache}/…`.

### 9.2 Johona config schema (v1)

```json
{
  "version": 1,
  "appearance":   { "theme_mode": "system" },
  "autostart":    { "enabled": false, "start_scheduler_on_launch": true },
  "location":     { "city": "", "latitude": 33.4484, "longitude": -112.074,
                    "timezone": "America/Phoenix" },
  "location_auto_update": { "on_timezone_change": false },
  "scheduling":   { "safety_interval": 60, "daily_shuffle_enabled": true },
  "backend":      { "override": "auto" },
  "theme":        { "last_applied": "", "last_applied_image": "" }
}
```

- `theme_mode`: `system` | `light` | `dark`.
- `backend.override`: `auto` | `portal` | `plasma` | `gnome` | `xdg_settings`.
- **No `suntime_model`** — the legacy model is removed entirely.
- `on_timezone_change` defaults to **false** (opt-in).
- Load/save validate and fill missing keys from defaults; **atomic writes**
  (temp file + rename).

### 9.3 Shuffle state (`shuffle-list.json` in the config dir)

```json
{ "shuffle_list": ["<theme paths>"], "current_index": 0, "last_used_date": "" }
```

- The engine is the **single writer**.
- State is persisted **only after the wallpaper set succeeds** (a failed
  change retries the same theme).
- Date-change detection drives the daily advance; a missed midnight is
  picked up on the next safety tick.
- When `current_index` reaches the end of the list, the list is reshuffled
  (Fisher–Yates) and the index wraps to 0.
- Atomic writes.

### 9.4 Migration (one-time, from kWallpaper)

Trigger: first launch, when `~/.var/app/top.spelunk.kwallpaper/` exists
**and** no Johona config exists yet. Idempotent — skipped once a Johona
config is present.

Copied from the old app (exact old locations, per kWallpaper `config.py`):

| Old (`~/.var/app/top.spelunk.kwallpaper/…`) | New |
|---|---|
| `config/kwallpaper/config.json` | `config.json` (schema-mapped, v1) |
| `config/kwallpaper/themes/` | `$XDG_DATA_HOME/johona/themes/` |
| `config/kwallpaper/shuffle-list.json` | `shuffle-list.json` |

The old dir is left **untouched**; the migration is logged to the event
log. The old cache dir (thumbnails, schedule backups) is **not** migrated.
Requires manifest permission `--filesystem=~/.var/app/top.spelunk.kwallpaper:ro`.

**Old kWallpaper v2 schema → Johona v1 mapping:**

| Old (v2) | Johona (v1) |
|---|---|
| `appearance.theme_mode` | `appearance.theme_mode` |
| `autostart.enabled` | `autostart.enabled` |
| `autostart.start_scheduler_on_launch` | `autostart.start_scheduler_on_launch` |
| `location.latitude` / `location.longitude` / `location.timezone` | `location.*` (new `city` defaults `""`) |
| `scheduling.daily_shuffle_enabled` | `scheduling.daily_shuffle_enabled` |
| `scheduling.safety_interval` | `scheduling.safety_interval` (keep the user's value; old default was 600, new default 60) |
| `theme.last_applied`, `theme.last_applied_image` | `theme.*` |
| **DROPPED:** `scheduling.cycle_interval`, `scheduling.run_cycle`, `scheduling.suntime_model` | — |
| **NEW (defaults):** | `location_auto_update.on_timezone_change=false`, `backend.override="auto"` |

Any other old field not in the mapping (e.g. the vestigial
`scheduling.daily_change_time` validation key) is dropped.

### 9.5 Engine (`engine/`)

High-level operations shared by the GUI and the scheduler:
`applyTheme(name?)`, `cycle()`, `nextChangeTime()`, `advanceShuffle()`,
`importTheme()`, `deleteTheme()`, `setWallpaper()`.

- `advanceShuffle()` (backing the tray **Next wallpaper** item): advance
  the shuffle list by one — `current_index = (current_index + 1) mod
  len(shuffle_list)`, `last_used_date` = today — and apply that theme's
  image for the current moment. Same invariants as a daily advance: the
  engine is the single writer, state persists only after the wallpaper set
  succeeds (a failed set leaves the index unchanged and retries), and the
  op is serialized with the scheduler. Works whether or not the scheduler
  is running.

- Owns config read-modify-write + shuffle state atomically.
- Serializes state-mutating operations (internal mutex) so a GUI Apply and
  a scheduler cycle can never race.

## 10. LocationManager (`core/location/`)

- `current()` → (lat, lon, timezone) from config.
- `detect()` priority:
  1. **Geoclue2** (system D-Bus `org.freedesktop.GeoClue2`) — if it returns
     a fix (request bounded by a 10 s timeout) → its lat/lon + timezone.
  2. **Timezone → coordinates lookup** — bundled
     `resources/tz_coordinates.json` mapping each IANA zone to
     representative coordinates (major city) — the offline fallback.
  3. Keep the current location + log a warning that it may be stale.
- **TZ watch:** on startup + each 60 s safety tick, read the current IANA
  timezone — primary: the `TZ` environment variable (Flatpak sets it to the
  host zone); fallback: `org.freedesktop.timedate1` system D-Bus. Compare
  to config; if changed **and** `location_auto_update.on_timezone_change`
  is on → `detect()` → if updated: save config, emit signal, scheduler
  recomputes segments + re-arms the one-shot, GUI event-log entry
  ("Timezone changed to X — location updated"). **DST transitions do not
  trigger this** (the zone string is unchanged, only the offset).
- Settings toggle "Auto-update location when the system timezone changes",
  **default OFF**, always logged when it acts.

## 11. GUI (Qt Widgets)

- **MainWindow**: tabs Themes / Settings / Scheduler. Title
  "Johona Wallpaper".
- **Single instance:** `QLocalServer` per-user socket; a second launch
  connects, sends `activate`, and exits; the running instance shows/raises
  its window.
- **Appearance:** `system` follows the DE platform theme natively (Breeze
  on KDE, GTK theme on GNOME — zero custom palette code); `light`/`dark`
  force Fusion + the matching Qt palette.
- **System tray:** `QSystemTrayIcon` with bundled light/dark icons chosen
  by effective mode; menu: Start/Stop scheduler, **Next wallpaper**
  (visible only when `scheduling.daily_shuffle_enabled` is on; disabled
  when the shuffle list is empty; runs `advanceShuffle()` in a worker —
  §9.5), Show/Hide, Quit; double-click shows the window; degrades
  gracefully to window-only when no tray is available (e.g. stock GNOME).

### 11.1 Themes tab

- Theme list (name + image count).
- **Import** (file dialog → `.ddw`/`.zip`), **Apply**, **Delete** (with
  confirmation). All run engine ops on QThreadPool workers with busy
  states; import failure shows a dialog listing every missing image.
- **PreviewWidget** (WDD technique): in-memory `QHash<path, QImage>`
  cache, **no disk thumbnail cache**. Decode on worker threads: the
  original scaled with a high-quality transform to a cap of **2× the
  widget's device-pixel size** (WDD's technique with a deliberate memory
  tuning — WDD caps at full screen size; 2×-widget is visually identical in
  the widget and ~4× lighter on 4K displays). Cache cleared on theme
  switch; an LRU byte cap (**512 MB**) as backstop. Cross-fade: back +
  front image, **600 ms, sine ease-in-out**
  (`sin((p−0.5)·π)/2 + 0.5`, WDD's exact curve), ~60 fps `QTimer`,
  `paintEvent` with `QPainter::setOpacity`, cover-crop to the widget.
- **SchedulePreview**: 24 h timeline — colored segment bands
  (sunrise/day/sunset/night), small cover-cropped thumbnails at each
  image's display time (in-memory, capped at ~2× thumbnail size), and a
  current-time marker (1-minute timer). Recomputed on theme selection,
  settings save, and date change.

### 11.2 Settings tab

- **Scheduler** — safety-tick interval (s), daily-shuffle check,
  start-scheduler-on-launch check.
- **Wallpaper backend** — Auto (detected: …) / Plasma / Portal / GNOME /
  xdg-settings; warning if the forced backend is unavailable.
- **Location** — city, lat, lon, timezone (validated against
  `QTimeZone::availableTimeZoneIds`), Auto-detect button (Geoclue2),
  auto-update-on-TZ-change check (default OFF).
- **Appearance** — system/light/dark.
- **Autostart** — "start at login" writes the host
  `~/.config/autostart/top.spelunk.johona.desktop` with
  `Exec=flatpak run top.spelunk.johona`.
- **Save** → atomic config write + hot-reload of the running scheduler
  (interval, shuffle, backend, and location all take effect without a
  restart).

### 11.3 Scheduler tab

- **Start/Stop.**
- **Status block:** running state, next change time (human-readable, from
  `nextChangeTime()`), active backend, last-applied theme/image.
- **Event log:** read-only `QPlainTextEdit`, live-fed by the scheduler log
  signal, capped at ~1000 lines.

### 11.4 GUI threading and failure display

- The GUI thread renders only; every engine op runs in a QThreadPool
  worker with results delivered via signals; preview and schedule-preview
  decodes also run on workers (the GUI thread never touches a
  full-resolution decode).
- Failures: status-line message + event-log entry; import failures get a
  dialog; backend failures name the backend and point at Settings.

## 12. Flatpak packaging

- Manifest: `flatpak/top.spelunk.johona.yaml` (flatpak-builder). Runtime
  **org.kde.Platform** (Qt 6 built in — pragmatic choice for a Qt 6 app
  with a Plasma backend), SDK org.kde.Sdk. CMake build; deps: libzip + Qt 6
  from the runtime.
- Finish-args:
  - `--share=ipc`, `--socket=fdo` (session D-Bus: Plasma + portal)
  - `--socket=wayland`, `--socket=x11` (GUI on both protocols)
  - `--device=dri` (GPU rendering)
  - `--talk-name=org.kde.plasmashell` (Plasma backend)
  - `--system-talk-name=org.freedesktop.login1` (suspend/resume hook)
  - `--system-talk-name=org.freedesktop.timedate1` (TZ watch)
  - `--system-talk-name=org.freedesktop.GeoClue2` (location detect)
  - `--filesystem=~/.var/app/top.spelunk.kwallpaper:ro` (one-time
    migration, read-only)
  - `--filesystem=xdg-config/autostart` (write the **host** autostart
    entry — inside the sandbox `~/.config` is private, so without this the
    start-at-login toggle can't reach the host autostart dir)
- Build: `flatpak/build.sh` → local `--user` bundle (parity with the
  Python app's flow); the manifest is kept Flathub-ready as a future
  option.
- AppStream: `data/top.spelunk.johona.metainfo.xml`.

## 13. Testing

- Framework: Qt Test (`Qt6::QtTest`) + CTest. No new dependencies.
- **Injectable clock:** solar/scheduler/engine take a `Clock` interface
  (system default; fixed/steppable in tests) for deterministic time logic.

**Core suites** (headless, link `johona-core`):

| Suite | Coverage |
|---|---|
| `test_suncalc` | ported suncalc test vectors (correctness anchor) |
| `test_solar` | segments on normal/DST/polar/civil-polar days; `imageAt` at every boundary; `nextChangeTime` ordering; duplicate-list quirk |
| `test_config` | round-trips, validation, defaults, atomic-write integrity |
| `test_themes` | valid/invalid archives, missing-image rejection, duplicate names, delete-path safety, image ordering |
| `test_shuffle` | day rollover, exhaustion reshuffle, persist-after-success (failed set leaves state untouched) |
| `test_engine` | apply/cycle with a mock backend (success + failure paths); `advanceShuffle()` (index wrap, persist-after-success) |
| `test_backends` | probes and call shapes against mocked D-Bus connections / process runners |
| `test_scheduler` | one-shot arm/re-arm, safety-tick no-op-unless-overdue, date change, clock jump, resume hook, bounded retry, re-arm after manual theme change |
| `test_location` | TZ-change detection, Geoclue2 (mocked), tz→coords fallback |
| `test_migration` | schema mapping, copy, idempotency |

- **GUI:** light smoke tests on the offscreen platform (windows/tabs
  construct, import flow with a temp config) — kept minimal since the core
  is fully covered headless.
- **CI:** GitHub Actions — build + ctest on Ubuntu (offscreen Qt).

## 14. Toolchain

C++20, Qt 6 (latest stable), CMake + Ninja. Local dev deps (Fedora):
`qt6-qtbase-devel`, `libzip-devel`, `cmake`, `ninja`.
