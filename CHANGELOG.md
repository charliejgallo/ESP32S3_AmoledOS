# Changelog

Newest first. Versions are git tags; what is above the latest tag is on
`main` and not yet in a release.

## On `main`, not yet released

- **Dirty rows for the Lua blit.** The frame buffer survives between frames
  and the app now pushes only the rows the script touched: every primitive
  marks the box it draws into, the boxes are merged into full-width bands, and
  only those go to the panel. A script does not call anything for this. A
  script that calls `aos.clear()` marks everything and costs exactly what it
  did before (`cubo.lua`: 224/224 rows, 40 ms, 25 fps); one that erases its own
  old positions pays for those (`pelota.lua`: 67/224, 20 ms, 50 fps — which is
  the frame timer's period, so it is really 12 ms of work). `aos.stats()`
  returns the row count as a fourth value, and `pelota.lua` is the example.
- **`AOS_MAX_APPS` 48 → 80, and it says so when it fills up.** Since a module
  can declare several apps, twenty built-in plus a full card came to exactly
  48 and the forty-ninth — a Lua script — was refused by
  `aos_ui_register_app()` **with no message at all**, which looked from the
  outside like an app that had failed to build. The same shape of bug as
  `MAX_DYNAPPS` at 16 with 17 apps and `MAX_SIM_APPS` at 12 with 14. Both the
  launcher and the loader log the refusal now, and the Lua app logs how many
  scripts the boot scan found.

- `hola.lua` gets an icon too (`apps/lua/scripts/hola.aic.txt`): the cyan disc
  the script itself draws, with two rings around it for the pulse. It is the
  first blob written with the assembler that uses `AIC_RING` and a literal
  colour, so between it and the cube the four opcodes a script is likely to
  want are exercised.

## v0.3.16 — 2026-09-17

Tools and documentation only: nothing under `components/`, `main/` or any
app's sources moved, so the firmware is v0.3.15's code with a different
version string and `apps.zip` is the same one.

- **`tools/aic.py asm`**: the assembler for icons. The firmware has read
  `.aic` files from the card since v0.3.8 and a Lua script has been a launcher
  app since v0.3.15, but there was no way to *make* one of those files without
  ESP-IDF — the icon had to be C macros compiled into a `.so`. It now takes
  the same macros from a text file, so a `.c` with an icon in it is valid
  input as it stands, and it accepts what `dump` prints as well.
- **`aic.py selftest`** checks that the two are inverses: every `.aic` in the
  tree and every icon written as C macros, dumped, assembled and compared byte
  for byte. 50 blobs, 0 failing.
- Writing it found a bug in `aic.py`: the RECT radius was read as unsigned,
  but `radius_px()` in the firmware reads 255 as `LV_RADIUS_CIRCLE` and the
  rest back as `int8_t`, so `dump` printed the `AIC_DIV(38)` of six icons as
  `218`. Both sides say signed now.
- `cubo.lua` gets an icon of its own (`apps/lua/scripts/cubo.aic.txt`).

## v0.3.15 — 2026-09-17

- **Lua on the watch.** A Lua 5.4.8 interpreter as a dynamic app
  (`apps/lua/`, 148 KB): a `.lua` file in `/sdcard/lua` runs when you open it,
  with no toolchain, no symbol table and no reboot to install it. A mistake is
  a message with the file, the line and the name of the variable — never a
  reset. [docs/LUA.md](docs/LUA.md).
- **A script is an app.** `aos_app.h` gains two OPTIONAL entry points,
  `aos_app_count()` and `aos_app_init_at()`, with the macro
  `AOS_APP_ENTRY_MANY`: a module can declare several apps and the loader
  registers them all from one `dlopen`. The Lua module declares one per
  script, so each `.lua` is a launcher entry with its own name (`-- @name`)
  and its own icon (an `.aic` beside it). The ABI does not move — the 26
  existing `.so` load untouched — and a multi-app module still works on a
  firmware that predates this. `MAX_DYNAPPS` 32 → 48.
- **The `/lua` page** in the portal: the scripts on the card, an editor, and a
  console with the running script and its error. While a script runs the app
  watches the file it came from and reloads it when it changes, so saving in
  the browser is the whole step.
- **What it costs, measured on the board.** 1.9 M loop turns a second with the
  code running from PSRAM, and **52 bytes** of internal RAM for a state whose
  heap is 77 KB, because `lua_Alloc` allocates out of PSRAM; with the default
  allocator the same state took 50 KB of the scarce kind. A frame of the cube
  bench is 3 ms of script, 8 of upscaling and 26 of pushing pixels: the panel
  is the ceiling, not the interpreter.
- **Two flags without which none of it works**, both with the measurement in
  the CMakeLists: `LUA_32BITS` (the S3 has no double-precision FPU, and
  without it 45 symbols were missing) and `LUAI_MAXCCALLS=40` (the factory
  guard of 200 asks for ~21 KB of C stack and the LVGL task has 16 — measured,
  66 levels of nested parentheses survived and 68 panicked the board).
- Symbol table 2672 → 2705, the new ones being `setjmp`/`longjmp` — which are
  the reason an interpreter cannot be "just an app" — the `heap_caps_*`
  allocator and a handful of libc that was missing anyway.
- `aos.js` gains `data-t-html` for translated strings that carry markup: the
  `<code>` tags in several help panels came out as literal text in English and
  German, which `/pato` had been doing since it existed.
- APP-GUIDE: the bottom strip of the screen loses about ten pixels at each end
  to the corner radius, the same bite already written down for the top.

## v0.3.14 — 2026-09-16

- **Steps, done properly.** A new detector (`aos_step_detect.c`, pure C,
  shared with a desktop bench) works on the magnitude of the acceleration
  with an adaptive threshold and a rhythm gate, so the watch counts the same
  on the wrist and in a pocket and a grab of it counts nothing. Tuned
  against recorded, counted walks in `tools/steps/`: 98 for 100 steps in a
  pocket where the old fixed threshold said 124, 98 on the wrist, 0 at the
  desk. `/api/imu` serves the last three minutes of the accelerometer for
  the next tuning. [docs/STEPS.md](docs/STEPS.md).
- **The count is kept**: today's steps survive a restart (NVS, every five
  minutes and at midnight), the day is cut by the clock, seven days of
  history, a goal changed by tapping the ring. `/api/status` publishes
  `steps` and `steps_goal`.
- **Activity redesigned** around it: the ring against the goal, distance,
  the week as bars. The level bubble is gone; the Level app has it.
- **The board's IMU is a QMI8658C**: no hardware pedometer, and its
  interrupt goes to the expander, which cannot wake the ESP32. Written up in
  HARDWARE.md so nobody plans around a feature the part does not have.

## v0.3.13 — 2026-09-16

- **Video**, a player of MJPEG AVIs from the card with sound, as a dynamic
  app (`apps/video/`, 14 KB, ABI 2). `tools/video_convert.sh` turns anything
  ffmpeg reads into a 368x448 AVI plus a mono WAV; the portal's Files page
  got a Videos folder for the pair. The sound goes to the firmware's player
  and is the clock the frames follow: a late frame is skipped with a seek, a
  click in the sound is never traded for a picture. Tap to pause, swipe back
  to stop. The numbers and the design are in [docs/VIDEO.md](docs/VIDEO.md).
- **The firmware carries Espressif's `esp_new_jpeg`** (77 KB of flash, 8.5 KB
  of internal RAM while open) and exports its decoder to the apps through the
  symbol table: 4:2:0 JPEGs to RGB565 three times faster than LVGL's TJPGD.
  `/api/jpegbench?file=<photo>&n=5` measures it on any photo on the card. The
  photo viewer keeps TJPGD, which decodes the 4:4:4 JPEGs the new one refuses.
- **One background task per app**: `aos_hal_worker_start/stop/should_stop/
  sleep`, pinned to the second core at the player's priority, a pthread in
  the simulator. Reading and decoding a frame is 50-90 ms on the board, and
  in LVGL's task that starved the touch, the back swipe and the portal's
  capture; in the worker the UI does not feel it. The contract is short and
  in `aos_hal.h`.
- **A direct blit to the panel**, `aos_hal_display_blit`: big-endian RGB565
  straight over the QSPI, past LVGL's render, under the LVGL lock. Through
  a canvas a full frame cost about 95 ms of LVGL's time and the video was
  stuck at 10 fps with the decoder idle; blitted, it plays at 15 with nothing
  skipped. LVGL's snapshot (the portal's capture) does not see it.
- **LVGL pinned to 9.5.0** in the firmware's manifest and every app's.
  `idf.py reconfigure` had re-solved it to 9.6.0 on its own when the manifest
  changed, with the apps and the simulator still on 9.5.0: the silent
  corruption `build_apps.sh` warns about, caught this time at link.
- The reader of AVIs seeks only when the file is not already where it wants
  it: an `fseek` to the current position is not free on FatFS.

## v0.3.12 — 2026-09-16

Small on purpose: the firmware is identical to v0.3.11 but for the version
string, and the release exists so that `apps.zip` matches the source.

- **Burbujas: a long chain sounds like one.** Six bubbles burst at once, or
  four dropped, now play a seven-note flourish that climbs two octaves and
  holds, with the falling ones answering underneath. It replaces the note per
  bubble rather than adding to it: the HAL's tone queue is sixteen notes deep
  and drops what does not fit, so a chain of twenty would eat the queue and
  leave the next shot silent.
- The README's table of dynamic apps gained the row it was missing, with the
  title, the aiming guide and time attack.

## v0.3.11 — 2026-09-15

- **Burbujas**, a bubble shooter, as a dynamic app (`apps/burbujas/`, 40 KB,
  ABI 2). A hexagonal board hangs from the ceiling, you aim by dragging -the
  dotted guide is the shot itself, run ahead of time through the same
  stepping function- and three of a colour burst, taking down whatever was
  hanging from them. Three modes: endless, generated levels where the ceiling
  comes down instead, and two minutes against the clock. Bombs and rainbow
  bubbles are earned by bursting six or dropping four.
- **It is the first app that needed nothing from the firmware at all**: the
  icon travels inside the `.so` (v0.3.8) and the name is translated by one
  line in the card's `_sistema.lang`. Installing it is copying one file.
- The still board lives in the BACKGROUND rather than in a slot per bubble,
  which is what keeps a full field cheap: 9-18 % of the field pushed per
  frame. Written up in `APP-GUIDE.md` 6.3.
- Level 1 starts with three rows and not four. Measured on the watch and
  reproduced on the bench (`bb_harness poke`, which shoots at four fixed
  points): shots that burst nothing stack downwards 19 px at a time, and with
  four rows a bad streak reached the line in seven shots without the ceiling
  coming down once. The level you play before knowing how the thing aims now
  has a row of headroom.

## v0.3.10 — 2026-09-15

Two more apps bring their own icon, and `apps.zip` is fresh.

- **`hello_app`**, the template every new app starts from, draws a speech
  bubble with a face: RECTs, INTO/OUT and an ARC, none of it in the firmware.
  Copy the template and the pattern comes with it.
- **`escaner`** carries its radar as the very bytes of the firmware's
  `ICON_RADAR` table, so it no longer depends on `AOS_ICON_RADAR` being there.
  Verified byte for byte on the board through `/api/icons?id=aos.netscan`.
- `APP-API.md`'s "The icon" example is now the template's real code.
- Firmware identical to v0.3.9 but for the version string.

## v0.3.9 — 2026-09-15

- **A panic on the idle timeout, fixed.** The housekeeping task turning the
  screen off wrote the panel's brightness register while the LVGL task was
  flushing pixels over the same SPI device; esp_lcd's bus lock is per device,
  not per task, and the second releaser tripped `assert` in
  `spi_device_release_bus()` - or, under load, lost the flush's completion
  and the LVGL task hung until the task watchdog. Every brightness write now
  takes the LVGL lock, as the panel sleep commands already did. Reproduced
  and closed with an A/B (`/api/mem?spin=N`, `tools/spi_stress.sh`): the
  unlocked build reboots in 5,000 writes while the launcher scrolls, the
  locked one runs clean. `POWER.md` 5.9.
- The portal's server can be starved of sockets by a few hundred
  back-to-back connections; documented in `PORTAL.md`, not fixed.

## v0.3.8 — 2026-09-15

Icons as data — a dynamic app no longer needs a firmware reflash to have an
icon of its own.

### Icons

- **Why.** An icon was a `switch` case in `aos_icon.c`, picked by an enum
  baked into the firmware. Every new `.so` app that wanted a proper icon
  meant a firmware build and a reflash for that alone, which defeated the
  point of loading apps from the card. Now the icon travels with the app.
- **AIC**, a byte format for icons (`aos_icon_ops.h`): a few dozen bytes of
  shapes with percent coordinates, an append-only palette, a 256-byte cap,
  written in C with `AIC_*` macros. One interpreter in `aos_icon.c` turns a
  blob into the same LVGL objects the hand-written code created.
- **Three sources, one format, in this order:** a file `/sdcard/icons/<id>.aic`
  (SPIFFS without a card), the blob the app registered from `init()` with
  `aos_icon_set_ops()`, the firmware's own table. Then the glyph, as before.
  `AOS_ABI_VERSION` stays at 2: every `.so` on the card keeps loading.
- **The firmware's 36 icons are tables now** (`aos_icon_tables.c`), derived
  by `tools/aic_gen.py` from what the old switch drew at 66, 74 and 82 px:
  34 pixel-identical, 9 within a pixel at one size. The switch is gone:
  −20.5 KB of `.flash.text`, 2.5 KB of tables. `tools/icon_golden/` keeps the
  original pixels and `tools/icon_bench.sh --golden` checks against them.
- **Portal:** `/iconos` draws every icon in the browser from the watch's own
  bytes, shows where each comes from, and uploads, removes or downloads the
  `.aic` per app; the launcher redraws on the next tick, no restart.
  `GET /api/icons[?id=]`.
- Topos is the first app that brings its icon. The Pato goma symbol
  `aos_app_pato_get` was missing from the symbol table and is back.
- Internal RAM unchanged in every phase; the two registries take 26 KB of
  PSRAM. The whole feature, page included, is 8.3 KB of binary.

## v0.3.7 — 2026-09-14

Pato goma — the USB port put to work as a scriptable keyboard and mouse.

### Pato goma

- A new built-in app (`aos.pato`) that plays little keyboard-and-mouse
  scripts over USB onto the computer the watch is plugged into. You write
  them in the portal's new `/pato` page — a step builder and a raw
  DuckyScript-like text view of the same format
  (`STRING`/`KEY`/`DELAY`/`MOUSE`/`SCROLL`/`CLICK`/`REPEAT`/`#`) — pick one
  on the watch and confirm, with a preview, before anything is sent.
- The steps play out one action per LVGL timer tick, so a long script does
  not freeze the screen and the Stop button always answers. A big mouse move
  is split into 120-px reports at parse time.
- The list refreshes itself: a script saved from the portal shows up without
  leaving and re-entering the app.
- New `AOS_ICON_DUCK`, English and German strings, a `/pato` portal page.
- **For education and demonstration only**, to show the HID capabilities of
  the ESP32-S3 in AmoledOS. We take no responsibility for the scripts third
  parties run with it, nor for any misuse they may give it.

## v0.3.6 — 2026-09-14

The USB port. Two days of building, a day and a night of use, and every
measurement in [docs/USB.md](docs/USB.md); [docs/HANDOFF-USB.md](docs/HANDOFF-USB.md)
for building on it.

### USB

- The dynamic apps can use the port: `aos_usb` joins the exported libraries
  of `tools/gen_symbols.py` (2584 → 2646 symbols), since `aos_hal_usb_*`
  live there and not in `aos_hal`. The ABI did not move.
- `docs/USB.md`: what the USB-C port can become. The schematic read (D+/D-
  straight to GPIO19/20, VBUS only into the charger, 5.1 K pull-downs on CC,
  a second copy of the data lines on solder pads through 22 Ω), the
  constraints (one PHY shared by the Serial-JTAG and the OTG controller, no
  5 V for a peripheral, full speed, internal RAM for transfer buffers, the
  card has one owner), a catalogue of 12 device-mode and 12 host-mode
  functions rated by cost and value, three host-mode test rigs, the
  measurements to take and a plan in four phases.
- `components/aos_usb`: the switch. Three states, console (boot default),
  device (TinyUSB, one CDC port with the console on it) and host (the USB
  Host Library with an inspector that logs every device's descriptors and
  keeps them for the portal). Holds a `NO_LIGHT_SLEEP` lock while the OTG
  side is on and flips the PHY mux back to the Serial-JTAG on leaving, with
  the pad down for 100 ms so the Mac sees a detach (without it the
  Serial-JTAG never came back). Nothing runs at boot. Measured on the
  board: device mode costs 4.5 KB of internal RAM, host mode 11.6 KB, both
  returned in full; the Serial-JTAG is back 0.6 s after leaving either.
- Two things esp_tinyusb / the host library do not say: `tinyusb_console_deinit()`
  reopens `/dev/uart/-1` on a Serial-JTAG console and leaves `stdout` NULL
  (a panic on the next log line; `aos_usb` redirects the streams itself,
  back to `/dev/console`), and `usb_host_install()` fails with
  `ESP_ERR_NOT_FOUND` from the httpd task because that core has no free
  level-1 interrupt (it is installed from a task pinned to core 1).
- `/api/usb[?mode=console|device|host][&console=0|1]`: drives the switch
  from the portal and answers the mode, the devices seen in host mode and
  the heap figures around each switch (tests T1-T4 of the document).
- **Disk mode** (`/api/usb?mode=disk`): the microSD as a USB drive of the
  computer, CDC console alongside. The Mac mounts it in 9-12 s, writes at
  776 KB/s (8 KB MSC buffer; 74 KB/s with the 512 B default), and on eject
  the card goes back to the watch on its own. `aos_hal_sd_release/reclaim/
  mark_mounted` move the card between the BSP mount and esp_tinyusb's MSC
  storage. While the computer has it the watch has no card.
- The Mac's sleep no longer kills the USB network: TinyUSB's device events
  are logged and the network is re-armed on every attach; survived a night.
  The core dump partition is 512 K (a dump is ~330 KB); panics print on the
  console again, and go to flash too.
- The portal stays up while the USB network is up: `main.c` stopped it
  whenever WiFi was down and no access point was up, so switching WiFi off
  from the portal over the cable took the portal down with it (found in
  the week of use, 2026-09-13).
- **Gamepad** (D5): a gamepad report in the HID interface,
  `aos_hal_usb_gamepad` in the HAL, `?pad=` in `/api/usb`, and a "Pad"
  face in Control PC: cross, A/B/X/Y, shoulders, select and start, the
  side button as A, and the tilt as the left stick behind a switch with
  "Centre" for the rest position. macOS lists the Game Pad usage beside
  the keyboard, mouse and consumer control ones.
- The README has a section on the USB port with the pictures of Control
  PC's faces and Settings → USB; the Settings options are shorter (the
  English was cut in the dropdown) and the address sits on its own line.
- **MIDI** (D7): a USB-MIDI port in keys mode, `aos_hal_usb_midi_note/cc/
  bend` in the HAL, `?midi=` in `/api/usb`, and a "MIDI" face in Control
  PC: an octave of keys, octave up and down, and the accelerometer's roll
  as pitch bend behind a switch. Verified with a MIDI listener on the Mac.
- **Mouse by tilt** (D4): a mouse report in the HID interface,
  `aos_hal_usb_mouse/click` in the HAL, `?mouse=` and `?click=` in
  `/api/usb`, and a "Tilt mouse" face in Control PC: the gyroscope's rates
  move the computer's pointer fifty times a second with a dead band, three
  speeds, invert/swap switches for the wrist, tap for a click and hold for
  a right click. Verified through the API by reading the Mac's cursor.
- **`amoledos.local` over USB**: the USB network interface is registered
  with the mDNS responder (and brings it up when WiFi never did), so the
  name resolves to 192.168.7.1 on the cable beside the WiFi address. The
  board's non-existent Ethernet gives up its mDNS slot for it.
- **`/usb` in the portal**: the four modes as options that apply on click,
  the state of each side (keyboard, the USB network, whose the card is, a
  pendrive), a key pad and a text box that type on the computer, the
  devices seen in host mode with the pendrive's files, and the diagnostics
  (breadcrumb, OTG registers, TinyUSB's log, the last core dump). `/api/usb`
  reports `hid_ready`. Verified in a browser: switching modes from the
  page and pressing its vol+ moved the Mac's volume.
- **USB network** (D6): in keys mode the watch is also a network interface
  of the computer (CDC-NCM): its DHCP server hands out 192.168.7.2 and the
  portal, the log and the API answer at http://192.168.7.1 over the cable
  with no WiFi. Ping 1.5 ms, downloads 2.2x faster than over WiFi. The S3's
  five IN endpoints do not fit CDC + HID + NCM, so keys mode carries the
  keyboard and the network and the CDC serial port lives in disk mode.
- **Keyboard mode** (D3): device mode is a CDC + HID composite, the watch a
  keyboard and media controller of the computer. `aos_hal_usb_*` in the
  HAL (mode, a named key, typing, the card's whereabouts), the **Control
  PC** app (music, slides, cmd+tab, esc/space/enter, and the switch into
  keyboard mode when the port is something else) and a **USB section in
  Settings** (the four modes as a dropdown, a status line). Verified on the
  Mac by reading its volume around the presses. 21 new strings, in English
  and German.
- **Host mode** gained the pendrive: `usb_host_msc` mounts it at `/usb`,
  the portal's explorer reaches it as `dir=usb`, and
  `/api/usb?cp=<name>&from=<dir>&to=<dir>` copies files between any two
  folders, timed. Parked after T4 (docs/USB.md, "Where host mode stands"):
  powering a peripheral costs the watch its portability.
- Found on the way, each one a reboot: the MSC class with no driver behind
  it panics on TEST UNIT READY (device mode now has a CDC-only
  descriptor); the PHY mux survives a software reset (`aos_usb_init()`
  puts it back at boot); TinyUSB's log at level 2 prints from the ISR and
  the interrupt watchdog fires under load; reading the OTG registers with
  the peripheral's clock gated hangs the bus with no panic (the register
  dump is now OTG-modes only); macOS needs 9-15 s to register the device.
- Tooling: a `coredump` partition in the free tail of the flash with
  `/api/coredump`, silent-reboot panics so they are written and not printed
  into a dead console, RTC-memory breadcrumbs (`boot_step` in `/api/usb`)
  and TinyUSB's own log in a ring (`/api/usb?tusblog=1`).
- `main/idf_component.yml`: `esp_tinyusb` 2.2.1, `usb_host_msc` 1.2.0,
  `usb_host_hid` 1.2.1, `usb_host_cdc_acm` 2.4.1, `usb_host_uvc` 2.5.2.
  With the classes off the binary does not move.
- `sdkconfig.defaults`: `CONFIG_TINYUSB_CDC_ENABLED=y` and the device
  strings ("AmoledOS", "AmoledOS watch").

### Also (on `main` before the branch)

- `aos_hal_esp32.c`: the unused `s_media_enabled` is gone, together with the
  "WAITING ON HARDWARE" comment above it, left over from the BLE HID plan.
  Media control ended up on AMS and its on/off state lives in `aos_ble`, so
  nothing ever touched the variable: a warning since the first commit, which
  incremental builds hide. The simulator keeps its own, which it does use.
- `docs/ROADMAP.md`: Android phones, starting with music controls over BLE
  HID. Planned, not scheduled. The comments in `aos_hal.h` and Control BT
  that still gave BLE HID as the way the music is controlled now say AMS,
  and point there.
- `/api/apps`: whether an app came from the card is asked to `aos_dynapp`.
  It was guessed from the id, and twelve of the card's apps are `aos.*`
  (clima, dados, pixel, truco...), so the portal's Screen page showed them
  as built-in.

## v0.3.5 — 2026-09-12

### Topos, a whack-a-mole

`apps/topos/`, a dynamic app. Nine holes in a lawn; moles pop up and you tap
them. A mole in a hard hat takes two taps, a golden one is worth a lot, a bomb
must not be touched. Three modes with a difficulty that climbs as you play:
classic (60 s), survival (three hearts, a level every eight moles) and frenzy
(30 s, several at once, combos up to ×5). A record per mode.

- Every state has its own sprite: a mole peeks out, glances about, taunts
  with its tongue out when nobody hits it, and goes dizzy when whacked; the
  hard hat flies off spinning; a bomb burns its fuse, blinks red at the end,
  and either fizzles out or blows up. Bodies, hat, bomb and mallet are
  rendered when the app opens, lit and quantised into four tones; faces and
  paws are hand-drawn.
- The lawn never moves, so it runs on dirty rectangles, and the compositor
  goes one step past Claude Jump's: a slot that did not change costs nothing,
  and a changed one has its area rebuilt from the background plus everything
  touching it. `tools/tp_harness.c` compares every frame with a full redraw.
- English and German from the first commit.
- The firmware gets its icon, `AOS_ICON_MOLE` (a mole peeking out of its
  hole). Until the watch runs a build with it, the app shows an empty circle
  in the menu.
- Measured on the board: the sprites render in 22 ms when the app opens, and
  frenzy under a stream of taps holds 29 frames per second, LVGL's ceiling.

### Also

- `aos_dynapp.c`: `pool_init()` is compiled only in a build without
  `CONFIG_ELF_LOADER_TEXT_PSRAM_MMU`, the one that still sets the 48 K
  reservation aside. It had been a dead-code warning since v0.3.4.
- The apps' `.so` files are the ones of v0.3.4, plus `topos.so`.

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
