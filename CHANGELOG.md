# Changelog

Newest first. Versions are git tags; what is above the latest tag is on
`main` and not yet in a release.

## v0.3.4 — 2026-09-12

### The RAM audit, and the apps' code in PSRAM

Measured on the board with `/api/mem` (heaps by region, task stacks with
peaks, the owner of every internal block by call stack, a render benchmark,
an injected tap): where the 279 K of internal RAM in use went, what each
screen cost, and what could move. `docs/RAM-AUDIT.md` has all of it.

- **LVGL's objects, styles and layers live in PSRAM** (`aos_lvmem.c`, a
  linker wrap of `lv_malloc_core`, no LVGL config change, apps untouched).
  The launcher alone used to put 48 K of small blocks in the executable heap
  and pulverise it on every screen change; the watchface renders in the same
  115 ms with and without.
- **The apps' code runs from PSRAM** (`CONFIG_ELF_LOADER_TEXT_PSRAM_MMU`,
  `elf_loader/src/soc/esp_elf_esp32s3.c`): the loader gets a 64 KB-aligned
  PSRAM block and `esp_mmu_map()` gives it an executable alias on the
  instruction bus; `dlsym` now hands out the alias too. The 48 K reservation
  is gone. claudito, Claude Jump and 2043 keep their frame rate.
- The DMA reserve is 16 K (it was carved out of the executable heap and never
  used), assertion messages of cache-off code no longer live in DRAM (6.8 K),
  PHY strings and the SPI and I2C ISRs are in flash, IPv6 is off, our
  components' `.bss` is in PSRAM (`main/aos_psram.lf`), the LVGL task stack
  is 16 K (measured peak 9.1 K), the tone task's stack is in PSRAM.
- Stacks that ran out of margin: `main` 10 K, `sys_evt` 3 K, `nimble_host`
  5 K, `aos_hk` 4 K, `aos_tone` 4 K.
- Result, watchface idle with WiFi and BLE up: general executable heap
  **140 K free with 131 K in one block** (v0.3.3: 30 K / 22 K); after a day
  of use it never went below 125 K.
- Rejected with evidence: the ROM flash driver (corrupts the heap on this
  board), heap task tracking (deadlocks esp_timer), mDNS's `.bss` in PSRAM
  (its TCB), the http/player/mic stacks in PSRAM (they reach NVS), the heap
  allocator in flash (+16 % on every full render).
- Ajustes → SISTEMA no longer reports the reservation: its first line says
  how many apps are loaded and that their code is in PSRAM. `/api/mem` stays
  in the portal (PORTAL.md); `tools/ram_audit/` has the scripts. README,
  ARCHITECTURE, HARDWARE, APP-API and BUILDING describe the new model.
- The English and German catalogs get the power and settings strings that
  had stayed in Spanish since v0.3.0 (POWER, Battery care, Forget phone, the
  charger states, the battery toasts, "Hold to power off"), and keep the
  raw-view strings of v0.3.3, which the audit branch had embedded from a
  stale pack. The apps' `.so` files are the ones of v0.3.3, unchanged.

## v0.3.3 — 2026-09-11

### The touch window, audited end to end

Why a 448-row panel only answers between y = 55 and y = 395, checked at
every step from the chip to LVGL instead of measured from the outside:
`esp_lcd_touch_cst816s` reads the 12-bit X/Y as they come, `esp_lcd_touch`
has every mirror/swap flag off, the LVGL port multiplies by 1, our wrapper
clamps only to 0..447 and the display rotation is 0. Nothing downstream of
the CST820 clips a coordinate. The 16 px X gap the BSP sets is an offset in
the CO5300's memory addressing, invisible to the touch, and never needed
compensating — the old comment saying so was wrong.

Then the board was measured with the new raw view, and the chip was
acquitted: it reports 1..447 and reaches those values with the finger
against the bezel, not before (dragging to the edge read 441; only a tap at
the rim read 447). The digitiser already stretches its coordinates so that
the rim is the last pixel; the five-cross fit measures that stretch and
inverts it (a = 0.81, b = 29 here, twice), parking the rim at y = 30 and
390. **The dead bands were made by the calibration.** The map now keeps the
fit between two anchor rows (60..350) and ramps from each to the bezel, so
touches land anywhere in 24..410 (16..352 in X), continuously, and a
control that contains the landing row is reachable from the rim.

- **Calibration no longer wipes itself first.** It used to save the identity
  before measuring; an attempt cut short by the button or a reboot left the
  panel uncalibrated. Raw mode alone bypasses the fit while measuring.
- **A rejected fit is now said out loud** and keeps the previous
  calibration. It used to fall back to the identity under a "Touch
  calibrated" toast, and with the acceptance floor at 0.7 a real measurement
  of this panel (0.76) sat 0.06 from being thrown away. The floor is 0.5.
- The two top crosses moved from y = 55 — exactly where raw Y saturates,
  the same trap fixed at the bottom in v0.3.0 — to `AOS_TOUCH_Y_MIN + 40`.
- The five raw points of every calibration go to the log, the loaded fit is
  logged at boot, and `/api/status` reports it (`touch_cal`, `cal_*`).
- **Ajustes → TÁCTIL → Ver crudo**: the live raw point, the dot where the
  map puts it, a 50 px ruler and the extremes seen; logged on close. Swipe
  navigation is off while it and the calibration are up (a sweep is a long
  drag). `AOS_SIM_TOUCH=1` / `=2` open them in the simulator.
- `AOS_TOUCH_LAND_TOP/BOTTOM/LEFT/RIGHT` in aos_hal.h, and the layout audit
  no longer calls a control that contains a landing row untouchable.

### The v0.3.1 / v0.3.2 relocations, undone

They were made against the calibration's artefact, so the apps get their
room back: Pixel Art's canvas is 288 px again (bar at y = 8), Buscaminas's
board 330 px (bar at y = 8), Laberinto 15 rows (chips at y = 26), Vida at
x4 (bar at y = 6); the pause strips of Arkanos and Claude Jump are the HUD
again; the Conversor's keypad, the Calendario's header (rows 46 px), Clima's
head and search box, Remoto's pages and Claudito's name-hold and action
buttons are where they were. A bar at y = 8..48 contains the landing row
(24), so a finger against the top rim reaches it. What v0.3.2 fixed for
real stays: the tuner's noise page, whose "clear peak" had never been on
screen, keeps the x beside the peak.

## v0.3.2 — 2026-09-11

### Every control below the touch floor

The digitiser reports nothing above y = 55 (see v0.3.1), and once the panel
is calibrated a button drawn in the first rows is dead. It had gone unnoticed
because an uncalibrated panel reported those touches 50 px too high — the
bars "worked" by accident, and the recalibration after the NVS wipe of
v0.3.0 took that away. Measured with nine taps on Buscaminas's bar: all
`y = 55`.

- The layout audit now checks the top the way it checks the bottom:
  `UNTOUCHABLE` for a control with almost nothing left below y = 56,
  `HIGHEDGE` for a wide one that merely crosses it. It found eleven screens.
- Moved: the bars of **Buscaminas** (board 288 px), **Vida** (grid at x3,
  276 px), **Laberinto** (one row fewer, 14x15) and **Conversor** (family
  button, rows and keypad compressed); the tabs of the **Afinador** and the
  pages of **Remoto**; the header of the **Calendario** (rows 42 px); the
  head of **Clima**; the pause strips of **Arkanos** and **Claude Jump**
  (down to y = 84, the paddle and the critter only read x); Claudito's
  name-hold (to the top of the stage) and its action buttons (32 px taller,
  upwards — the bar left 11 px to touch).
- The other apps were already clean, Pixel Art included.

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
- Measured on the board: with the menu's forty icons, BLE and wifi up, the
  app must fit in what internal RAM is left or the **microSD driver runs out
  of DMA memory** and every file operation fails. So a gallery slot is one
  canvas with everything painted into it, the palette is one canvas, the menu
  is built only while open, files are read before any object exists, and a
  failed save backs off ten seconds instead of retrying every tick.
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
