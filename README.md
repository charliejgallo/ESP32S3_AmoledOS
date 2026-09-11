# AmoledOS

A smartwatch firmware for the **Waveshare ESP32-S3-Touch-AMOLED-1.8** — a
368x448 AMOLED you can hold in your hand. Seven watchfaces, eighteen built-in
apps, twenty-two more loaded from the microSD as shared objects, a web portal,
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

<p align="center">
  <img src="docs/img/photo-claudito.jpg" width="220" alt="Claudito running on the board, in a printed case">
  <img src="docs/img/photo-arkanos.jpg" width="220" alt="Arkanos running on the board">
  <img src="docs/img/photo-case.jpg" width="220" alt="The analog watchface, in the TPU case">
</p>

<p align="center"><em>And the thing itself. The grey case is printed in TPU —
the STLs are on
<a href="https://www.printables.com/model/1837479-waveshare-esp32-s3-touch-amoled-18-tpu-case">Printables</a>,
in a plain and a keychain version.</em></p>

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

**Battery.** The AXP2101 is programmed rather than left at its factory
values: the cell charges at 0.5 C to 4.1 V with a proper termination current
("battery care", a switch), the watch powers itself off cleanly at 3 % instead
of running the cell down to the PMU's 2.6 V cut, and the power key works: a
click is the screen switch. With the screen off the CPU drops to 80 MHz, WiFi
goes to its deepest modem sleep and, on battery, the chip light-sleeps between
wake-ups — 58 % of the time, measured. Seven of the PMU's regulators feed
nothing on this board and are off. The Battery app shows the charger's stage,
the board temperature from the thermistor next to the PMU, drain in %/h with
hours left, time on battery, charge cycles and why the PMU last powered off;
`/api/status` serves the same to a home-automation poller. The whole
investigation, what the datasheet and the schematic say and what the board
said back, is in [docs/POWER.md](docs/POWER.md).

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
plot, and the whole remote-control profile. It also carries every switch of the
Settings app, a live view of the screen with the controls to drive it from the
browser, and the log tailed over wifi. See [docs/PORTAL.md](docs/PORTAL.md).

| Notification | Setup AP |
|---|---|
| <img src="docs/img/notification.png" width="220"> | <img src="docs/img/setup-ap.png" width="220"> |

## The apps

Forty of them, in two families that differ in where the code lives, not
in what they are allowed to do.

### Built into the firmware

Eighteen ship inside the binary. They are the ones the watch cannot be without
— if the microSD is out, these still work.

| | | |
|---|---|---|
| <img src="docs/img/int-activity.png" width="200"><br>**Actividad** — steps and movement from the QMI8658, with the day's history. | <img src="docs/img/int-stopwatch.png" width="200"><br>**Cronómetro** — laps, and it keeps counting with the screen off. | <img src="docs/img/int-timer.png" width="200"><br>**Temporizador** — countdown with presets, and it rings through the speaker. |
| <img src="docs/img/int-pomodoro.png" width="200"><br>**Pomodoro** — work and break cycles, with the day's tally kept across restarts. | <img src="docs/img/int-worldclock.png" width="200"><br>**Reloj mundial** — several cities at once, each with its own offset. | <img src="docs/img/int-alarm.png" width="200"><br>**Alarmas** — up to six, each on its own days of the week, checked by a service that runs whatever app is open; also editable from the portal. |
| <img src="docs/img/int-calendar.png" width="200"><br>**Calendario** — the month, drawn with the week starting on Monday. | <img src="docs/img/int-notifs.png" width="200"><br>**Notificaciones** — the iPhone's, over ANCS: history, per-category filter and actions. | <img src="docs/img/int-btremote.png" width="200"><br>**Control BT** — the phone's music over AMS: title, artist, album and transport. |
| <img src="docs/img/int-music.png" width="200"><br>**Música** — plays WAV from the card through the ES8311 codec. | <img src="docs/img/int-photos.png" width="200"><br>**Fotos** — JPEG, PNG and BMP from the card, decoded and scaled to the screen. | <img src="docs/img/int-flashlight.png" width="200"><br>**Linterna** — the panel at full white, which on an AMOLED is the only way to make light. |
| <img src="docs/img/int-level.png" width="200"><br>**Nivel** — a spirit level off the accelerometer, with the bubble and the angle in degrees. | <img src="docs/img/int-calc.png" width="200"><br>**Calculadora** — four operations, sized for a thumb rather than for density. | <img src="docs/img/int-convert.png" width="200"><br>**Conversor** — units across several families, with the keypad shared with the calculator. |
| <img src="docs/img/int-battery.png" width="200"><br>**Batería** — what the AXP2101 reports: charge, voltage and whether it is charging. | <img src="docs/img/app-life.png" width="200"><br>**Vida** — Conway's Game of Life and Langton's ant on a 92x92 grid. | <img src="docs/img/settings-en.png" width="200"><br>**Ajustes** — brightness, always-on, language, wifi, bluetooth, watchface and the touch calibration. |

### Loaded from the microSD

Twenty-two more live in [`apps/`](apps/) and are loaded from `/sdcard/apps` as
`.so` files at startup. The same source builds into the simulator, so they are
designed on a laptop and copied to the card without changing a line — and a new
one needs no firmware rebuild.

| | | |
|---|---|---|
| <img src="docs/img/app-chatarra-map.png" width="200"><br>**Chatarra** — a turn-based robot RPG. Eight zones, 51 rooms, 64 parts drawn from descriptors rather than sprites. | <img src="docs/img/app-chatarra-battle.png" width="200"><br>Its combat: six elemental types, an effectiveness table, and the robot you fight with is one you built from parts torn off others. | <img src="docs/img/app-cjump.png" width="200"><br>**Claude Jump** — a vertical platformer with five zones, coins and sixteen costumes. |
| <img src="docs/img/app-gemas.png" width="200"><br>**Gemas** — match-three. The jewels are traced in code as convex polygons with facets, not stored as bitmaps. | <img src="docs/img/app-2043.png" width="200"><br>**2043** — a vertical shooter, an homage to Capcom's 1943, with a different boss per planet. | <img src="docs/img/app-arkanos.png" width="200"><br>**Arkanos** — brick breaking, twelve walls, and the app that introduced dirty-rectangle drawing. |
| <img src="docs/img/app-claudito.png" width="200"><br>**Claudito** — a virtual pet, entirely hand-drawn pixel art on a 92x112 grid. | <img src="docs/img/app-truco.png" width="200"><br>**Truco** — Argentine truco against the machine, with cards drawn in code and a matchstick scoreboard. | <img src="docs/img/app-atasco.png" width="200"><br>**Atasco** — a sliding block puzzle. 25 levels, each with a BFS-verified minimum move count. |
| <img src="docs/img/app-clima.png" width="200"><br>**Clima** — weather from Open-Meteo over HTTPS, with the icons drawn from shape descriptions at any size. | <img src="docs/img/app-remoto.png" width="200"><br>**Remoto** — a programmable Home Assistant remote: button pages, accelerometer gestures and a dial you turn with your wrist. | <img src="docs/img/app-sensores.png" width="200"><br>**Sensores** — up to four Home Assistant sensors with three hours of chart, sampled by the watch itself. |
| <img src="docs/img/app-tuner.png" width="200"><br>**Afinador** — a chromatic tuner (NSDF pitch detection) and a sound level meter with A weighting. | <img src="docs/img/app-recorder.png" width="200"><br>**Recorder** — voice memos to WAV on the card, with a live waveform. | <img src="docs/img/app-mines.png" width="200"><br>**Buscaminas** — minesweeper on a single canvas, because 250 LVGL objects do not fit in internal RAM. |
| <img src="docs/img/app-maze.png" width="200"><br>**Laberinto** — a ball rolling through a generated maze, driven by tilting the board. | <img src="docs/img/app-cotiz.png" width="200"><br>**Cotizaciones** — exchange rates, configured from the portal. | <img src="docs/img/app-scanner.png" width="200"><br>**Escáner** — a WiFi and LAN survey: networks around you, hosts and open ports, written to the card as NDJSON. |
| <img src="docs/img/app-flappy.png" width="200"><br>**Flappy** — one button, one bird, the usual pipes. | <img src="docs/img/app-simon.png" width="200"><br>**Simon** — the colour-and-sound memory game, each pad with its own tone. | <img src="docs/img/app-dice.png" width="200"><br>**Dados** — dice of any number of sides, rolled by shaking the watch. |
| <img src="docs/img/app-pixel.png" width="200"><br>**Pixel Art** — 8x8 and 16x16 drawings with a 32-colour palette, frames that become a looping GIF, exported to the card as PNG and GIF. The black kitten walking is one of the samples it seeds on first run. | <img src="docs/img/app-pixel-gallery.png" width="200"><br>Its gallery of eight canvases. The same files open in the portal's `/pixel` page, where they are drawn with a mouse and saved back; the watch reloads them on its own. | <img src="docs/img/app-hello.png" width="200"><br>**hello_app** — the 30-line template. It is what you copy to start one of your own; see [docs/APP-API.md](docs/APP-API.md). |

## Flash it without building

The [latest release](https://github.com/charliejgallo/ESP32S3_AmoledOS/releases/latest)
carries the firmware and the twenty-two dynamic apps already built, for the
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
apps/                 22 dynamic apps
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
| [POWER.md](docs/POWER.md) | the AXP2101, the rails, light sleep, and the measurements behind each switch |
| [PORTAL.md](docs/PORTAL.md) | the web portal: pages, API, what it costs the board, and the dev server |

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

Known gaps: MP3 — the player handles 16-bit PCM WAV only. On the power side,
the clean power-off at 3 % and the charge-cycle counter are written and
reviewed but have not yet been through a real discharge, and the night-on-
battery figure is still to be taken — [POWER.md](docs/POWER.md) section 7 is
the protocol and `tools/battery_night.py` the recorder.

## Licence

MIT — see [LICENSE](LICENSE).

`components/elf_loader/` is Espressif's, under Apache-2.0, vendored with a
small local change. LVGL and ESP-IDF are pulled in by the component manager
under their own licences.
