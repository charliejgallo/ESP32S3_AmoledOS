# AmoledOS

A smartwatch firmware for the **Waveshare ESP32-S3-Touch-AMOLED-1.8** — a
368x448 AMOLED you can hold in your hand. Seven watchfaces, eighteen built-in
apps, twenty-one more loaded from the microSD as shared objects, a web portal,
iPhone notifications over BLE, and a desktop simulator that runs the same UI
code so you can build the whole thing without the board.

<p align="center">
  <img src="docs/img/board-watchface.png" width="220" alt="Nixie watchface, photographed from the board">
  <img src="docs/img/board-aod.png" width="220" alt="The same face in always-on mode">
  <img src="docs/img/launcher-list.png" width="220" alt="The app launcher">
</p>

<p align="center"><em>The first two are captures from the real panel over HTTP.
Everything else below comes from the simulator, which draws the same
pixels.</em></p>

---

## What it does

**Watch.** Seven interchangeable watchfaces, each with a dimmed always-on
variant. On AMOLED a black pixel is switched off, so a nearly black face costs
almost nothing to leave lit.

| | | | | | | |
|---|---|---|---|---|---|---|
| <img src="docs/img/face-digital.png" width="110"> | <img src="docs/img/face-analog.png" width="110"> | <img src="docs/img/face-nixie.png" width="110"> | <img src="docs/img/face-flip.png" width="110"> | <img src="docs/img/face-rings.png" width="110"> | <img src="docs/img/face-binary.png" width="110"> | <img src="docs/img/face-minimal.png" width="110"> |
| digital | analog | nixie | flip | rings | binary | minimal |

**Launcher.** Three styles — a vertical list with the watchOS scale-and-fade
effect, a grid, and a honeycomb.

| List | Grid | Honeycomb |
|---|---|---|
| <img src="docs/img/launcher-list.png" width="220"> | <img src="docs/img/launcher-grid.png" width="220"> | <img src="docs/img/launcher-honeycomb.png" width="220"> |

**Phone notifications.** With a paired iPhone the watch shows its
notifications, sets its own clock from it, reads its battery and controls its
music — all over BLE, on one connection, with no app on the phone side. A
notification takes the whole screen; messages from one conversation are grouped
(WhatsApp sends one per message) and calls carry working answer and reject
buttons.

| What comes from the phone | How |
| --- | --- |
| notifications | ANCS |
| the time, without WiFi | Current Time Service |
| its battery | Battery Service |
| music and its controls | AMS |

It is iPhone only: ANCS is published by iOS and Android has no standard
equivalent.

**Network onboarding.** With no stored credentials there is no way to enter
credentials, so the watch brings up its own access point and serves the form
itself. The screen shows the password in a large font and a QR code that
Android and iOS read from the camera out of the box.

**Three languages**, switchable on the device, with the packs on the microSD.
Spanish is the source language; English and German ship inside the binary.
There is also a pseudolocalisation pack for stress-testing layouts.

| Español | English | Deutsch |
|---|---|---|
| <img src="docs/img/settings-es.png" width="220"> | <img src="docs/img/settings-en.png" width="220"> | <img src="docs/img/settings-de.png" width="220"> |

**A web portal**, embedded in the binary, for uploading apps, photos, music and
recordings to the card from any browser — and for configuring the things that
are miserable to type on a 368 px screen: WiFi, the Home Assistant address and
token, the weather location, which exchange rates to watch, which sensors to
plot, and the whole remote-control profile.

| Notification | Setup AP |
|---|---|
| <img src="docs/img/notification.png" width="220"> | <img src="docs/img/setup-ap.png" width="220"> |

## The apps

Eighteen are built into the firmware: activity, stopwatch, timer, pomodoro,
world clock, alarms, calendar, music, BT control, photos, flashlight, spirit
level, calculator, unit converter, Game of Life, battery, notifications and
settings.

Twenty-one more live in [`apps/`](apps/) and are loaded from
`/sdcard/apps` as `.so` files at startup. The same source builds into the
simulator, so they are designed on a laptop and copied to the card without
changing a line.

| | | |
|---|---|---|
| <img src="docs/img/app-chatarra-map.png" width="200"><br>**Chatarra** — a turn-based robot RPG. Eight zones, 51 rooms, 64 parts drawn from descriptors rather than sprites. | <img src="docs/img/app-chatarra-battle.png" width="200"><br>Its combat: six elemental types, an effectiveness table, and the robot you fight with is one you built from parts torn off others. | <img src="docs/img/app-cjump.png" width="200"><br>**Claude Jump** — a vertical platformer with five zones, coins and sixteen costumes. |
| <img src="docs/img/app-gemas.png" width="200"><br>**Gemas** — match-three. The jewels are traced in code as convex polygons with facets, not stored as bitmaps. | <img src="docs/img/app-2043.png" width="200"><br>**2043** — a vertical shooter, an homage to Capcom's 1943, with a different boss per planet. | <img src="docs/img/app-arkanos.png" width="200"><br>**Arkanos** — brick breaking, twelve walls, and the app that introduced dirty-rectangle drawing. |
| <img src="docs/img/app-claudito.png" width="200"><br>**Claudito** — a virtual pet, entirely hand-drawn pixel art on a 92x112 grid. | <img src="docs/img/app-truco.png" width="200"><br>**Truco** — Argentine truco against the machine, with cards drawn in code and a matchstick scoreboard. | <img src="docs/img/app-atasco.png" width="200"><br>**Atasco** — a sliding block puzzle. 25 levels, each with a BFS-verified minimum move count. |
| <img src="docs/img/app-clima.png" width="200"><br>**Clima** — weather from Open-Meteo over HTTPS, with the icons drawn from shape descriptions at any size. | <img src="docs/img/app-remoto.png" width="200"><br>**Remoto** — a programmable Home Assistant remote: button pages, accelerometer gestures and a dial you turn with your wrist. | <img src="docs/img/app-sensores.png" width="200"><br>**Sensores** — up to four Home Assistant sensors with three hours of chart, sampled by the watch itself. |
| <img src="docs/img/app-tuner.png" width="200"><br>**Afinador** — a chromatic tuner (NSDF pitch detection) and a sound level meter with A weighting. | <img src="docs/img/app-recorder.png" width="200"><br>**Recorder** — voice memos to WAV on the card, with a live waveform. | <img src="docs/img/app-mines.png" width="200"><br>**Buscaminas** — minesweeper on a single canvas, because 250 LVGL objects do not fit in internal RAM. |
| <img src="docs/img/app-maze.png" width="200"><br>**Laberinto** — a ball rolling through a generated maze, driven by tilting the board. | <img src="docs/img/app-cotiz.png" width="200"><br>**Cotizaciones** — exchange rates, configured from the portal. | <img src="docs/img/app-life.png" width="200"><br>**Vida** — Conway's Game of Life and Langton's ant on a 92x92 grid. |

Plus Flappy, Simon, Dados, Escáner (a WiFi/LAN survey) and `hello_app`, the
30-line template.

## Flash it without building

The [latest release](https://github.com/charliejgallo/ESP32S3_AmoledOS/releases/latest)
carries the firmware and the twenty-one dynamic apps already built, for the
Waveshare ESP32-S3-Touch-AMOLED-1.8.

```bash
# 1. the firmware: one file, written at 0x0
esptool --chip esp32s3 -p <PORT> -b 460800 write_flash 0x0 amoledos-full.bin

# 2. the apps: unzip onto the microSD, in a folder called apps/
unzip apps.zip -d /Volumes/<sd>/apps/
```

> `amoledos-full.bin` is a **factory image**: it spans the flash from 0x0, so it
> overwrites the NVS partition and the watch comes up with no wifi credentials,
> no language, no watchface and no app data. That is what you want on a fresh
> board. To update a watch already in use, take
> `amoledos-firmware-files.zip` instead — the same build as four separate files
> that leave NVS alone.

The apps are loaded once at startup, so restart the board after copying them.
Then set the wifi up from the watch: Settings → the network screen raises an
access point and shows a QR code.

**After that first install the cable is optional.** The firmware updates over
WiFi — `./tools/install_fw.sh <board-ip>`, or drop the `.bin` on the portal's
front page — writing into the idle one of the two 5 MB app slots and leaving
NVS alone, so your wifi, language and app data survive. The new image boots on
trial and the bootloader goes back to the previous one on its own if it does
not come up. See [docs/BUILDING.md](docs/BUILDING.md).

## Quick start

```bash
# firmware
source ~/esp/esp-idf/export.sh
idf.py set-target esp32s3
idf.py build && python3 tools/gen_symbols.py && idf.py build
idf.py -p /dev/cu.usbmodem* flash monitor

# simulator — no board needed
brew install sdl2 cmake
cd sim && cmake -B build && cmake --build build -j8 && ./build/amoledos_sim
```

The symbol table needs two passes: dynamic apps resolve LVGL, the HAL and libc
against a table generated from the build's own libraries, so the libraries have
to exist first. See [docs/BUILDING.md](docs/BUILDING.md).

## How it is put together

```
main/                 startup on the board
sim/                  startup on the desktop (SDL2)
components/
  aos_hal/            the single contract with the platform
  aos_board/          AXP2101, PCF85063A, QMI8658
  aos_ui/             launcher, watchfaces, navigation, theme, i18n
  aos_apps/           the 18 built-in apps
  aos_dynapp/         .so loader and symbol table
  aos_ble/            NimBLE: ANCS, AMS, pairing
  aos_web/            the web portal, embedded in the binary
apps/                 21 dynamic apps
tools/                generators, test benches, board utilities
```

The rule that holds it up: **`aos_ui` and `aos_apps` include only LVGL and
`aos_hal.h`**, never an ESP-IDF header. That is why the same interface code
compiles for the board and for the desktop, and why every screen can be
designed, audited and screenshotted without hardware.

Three files are shared verbatim by both platforms because they contain no
platform code at all: the HTTP/TLS client, the notification policy, and the
network survey's report format.

## Documentation

| | |
| --- | --- |
| [ARCHITECTURE.md](docs/ARCHITECTURE.md) | how the pieces fit, the drawing model, and what the measurements taught |
| [HARDWARE.md](docs/HARDWARE.md) | the board, the pinout, and the quirks the datasheets do not mention |
| [BUILDING.md](docs/BUILDING.md) | firmware, simulator, dynamic apps, and every tool |
| [APP-API.md](docs/APP-API.md) | writing an app, and the things that will bite you |
| [I18N.md](docs/I18N.md) | how translation works and why the key is the Spanish string |

## A note on what is written down

Most of the comments in this repository explain *why*, not *what*, and many of
them record something that was measured rather than assumed. A few examples of
what that looks like in practice:

* The touch panel **reports nothing below y = 395**, although the display draws
  to 447. It was found by printing every touch across four apps. No calibration
  fixes it. Every screen in the project is laid out around it.
* Letting LVGL stretch a canvas costs **129 ms per frame**. Every game upscales
  by hand.
* Rebuilding a twenty-row list costs **111–124 ms** with the LVGL thread
  blocked; filling the same rows in costs 18–31.
* Dynamic apps take their code from a **48 KB contiguous reservation** claimed
  at startup, because the general heap fragments. Turning WiFi on costs ~60 KB
  of the same pool and Bluetooth ~30 KB, which makes both of those switches
  also *app* switches.
* An autocorrelation pitch detector returns 146.8 Hz for a 440 Hz tone. The
  tuner uses NSDF and picks the *first* peak over the threshold, not the
  highest.

Where something is a guess, it says so.

## Status

Running on hardware. WiFi, BLE against a real iPhone, audio in and out, the
microSD, the web portal, TLS and over-the-air updates have all been exercised
on the board — most of the measurements quoted throughout the source were taken
there.

Known gaps: MP3 — the player handles 16-bit PCM WAV only.

## Licence

MIT — see [LICENSE](LICENSE).

`components/elf_loader/` is Espressif's, under Apache-2.0, vendored with a
small local change. LVGL and ESP-IDF are pulled in by the component manager
under their own licences.
