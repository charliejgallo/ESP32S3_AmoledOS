# Changelog

Newest first. Versions are git tags; what is above the latest tag is on
`main` and not yet in a release.

## v0.3.1 — 2026-09-11

### Pixel Art

- **A drawing app** (`apps/pixel`, dynamic): eight canvases of 8x8 or 16x16
  cells and a 32-colour palette; pencil, flood fill, colour picker and undo;
  up to 16 frames per canvas, duplicated and retouched into an animation that
  plays on the watch. **Exports to the card**: one frame as an indexed PNG,
  all of them as a looping GIF, both from encoders written for the app
  (`px_export.c`, verified byte by byte by `apps/pixel/tools/px_harness.c`).
  The `.pix` files live in `/pixel` on the card and are re-read when they
  change on disk. On first run it seeds four samples: a black kitten walking
  through a meadow (16x16, four frames), a beating heart, a winking face and
  a checkerboard.
- **Portal page `/pixel`**: the same canvases drawn with a mouse, frames and
  all, saved back to the watch through the generic file API (`dir=pixel`);
  PNG and GIF downloads generated in the browser.
- New menu icon `AOS_ICON_PIXEL`; the English pack embedded in the firmware
  regenerated (and two alarm strings that had lost their translation put back).
- The other 21 apps are untouched: no symbol was added to the table and
  `AOS_ABI_VERSION` stays at 2, so their `.so` files are the v0.3.0 ones.

## v0.3.0 — 2026-09-09

The portal grows from a file manager into the place to drive and test the
watch from a browser. Everything here was built in a fork, tested on the
board over OTA, and merged back. The rule for the whole batch: the
browser does the work, nothing new is static in internal RAM, nothing new is
a task. Measured with `idf.py size`, the static internal footprint went
**down** 1.9 KB (the PM dump of `/api/pmu` was a 2 KB static; it is a PSRAM
allocation now). See [docs/PORTAL.md](docs/PORTAL.md).

### The web portal

- **Home** (`/`): battery, network, memory and system cards from `/api/status`,
  which now also carries the network, the phone, the card's free space, the
  open app and the time; wake, screen off, sync time, restart; the firmware
  upload moved here from Files.
- **Settings** (`/ajustes`): every control of the watch's Settings app
  through `GET/POST /api/ajustes`: brightness, watchface, launcher style,
  always-on and its brightness, volume, the four power switches, time zone
  with a list of common zones, set the time from the internet or from the
  browser, language, wifi, Bluetooth, notifications. Applied on release.
- **Alarms** (`/alarmas`): the six slots, with time, on/off, days of the week
  and shortcuts for weekdays / weekend / every day.
- **Screen** (`/pantalla`): a live capture with the controls to drive the
  UI from the browser: open any app, back, watchface, menu, a toast.
- **Log** (`/registro`): the ESP_LOG output tailed over wifi from a 16 KB
  ring in PSRAM (`aos_log.c`), for when the USB console is gone.
- **Files** (`/archivos`): the card's free space, a photo preview, and a
  **Card** tab that explores the whole card: any folder, breadcrumbs, new
  folder, delete an empty one.
- Shared shell: navigation in three groups that scrolls sideways on phones,
  a live status strip on every page, a light theme following the system,
  and the controls the pages were missing (search, range, switch).
- `tools/portal_dev_server.py` serves all of it against `sim/sim_fs`.

### Firmware

- `aos_ui_request_open/nav/watchface/launcher_style/toast()`: notes the
  server task leaves and `aos_ui_tick()` applies with the lock held. This
  is how anything outside the UI task changes the UI.
- `aos_hal_sd_usage()` implemented (it was a stub), `aos_hal_path_sd_root()`.
- Alarms repeat by day of the week: the stored value gains a seven-bit mask
  above the enabled bit (zero reads as every day, so old cards keep
  working); the watch's editor gets a row of day buttons, tapping an
  alarm's time reopens it, rows show the days' initials.
  `aos_alarm_get()/set()` for the portal, with a reload flag the service
  applies on its tick.

## v0.2.0 — 2026-09-09

Power: the AXP2101 programmed and polled, the panel put to sleep, the CPU
scaled, light sleep on battery with the screen off, and the night-on-battery
recorder. See [docs/POWER.md](docs/POWER.md).

## v0.1.2

The first public release: seven watchfaces, eighteen built-in apps,
twenty-one dynamic apps, the portal, iPhone notifications over BLE, OTA.
