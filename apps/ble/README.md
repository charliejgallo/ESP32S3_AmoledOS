# BLE

A Bluetooth LE scanner and analyser for the watch: who is around, how
strong, what each one says in its advertisements, the readings thermometers
and the like broadcast, and, for the devices that take a connection, their
GATT services. The id is `aos.ble`.

It was written for P4OS first and brought here with the same firmware API,
so the decoders and their tests are the same files. It needs the firmware's
scanner and GATT client (`aos_hal_ble_*` in `aos_hal.h`,
`components/aos_ble/aos_ble_scan.c`), which came with it. Bluetooth has to
be on; the app offers to switch it on.

It scans **only while it is open**, and keeps the screen on meanwhile (the
watch closes whatever is open when the screen dims, and a scanner is
something you look at without touching). The button leaves it, and the
radio stops listening with it.

## The screen

A row of glyphs on top, where a finger lands best: four pages and the
settings. The strip at the bottom of the glass, where a finger does not
land, says how the scan goes ("12 nearby · 42 pkt/s · 100 %").

- **Cerca** (near): a row per device, sorted by signal, name or newest, and
  filtered (named, favourites, connectable, sensors, beacons, Apple). Each
  row: what it seems to be (a glyph in its class's colour), its name or a
  label made from what it advertises ("AirPods", "Mouse (Swift Pair)",
  "Termómetro Govee"), the company or a sensor's readings, the signal now
  and its last 30 seconds as bars.
- **Radar**: everyone heard in the last 30 seconds at an estimated
  distance, on a logarithmic scale from 30 cm to 30 m. The angle means
  nothing (Bluetooth has no direction): it is the address's hash, so a dot
  keeps its place. A tap picks one; its card opens the detail, the
  magnifier the finder.
- **Sensores**: a card per device broadcasting readings in a known format:
  the temperature big, the humidity beside it, the last two hours on the
  right, the rest underneath.
- **Aire**: packets a second and devices over two minutes, what kinds of
  things, which companies, which kinds of address (public, random static,
  private resolvable, private non resolvable) and of advertisement, and how
  the signals spread.

A device opens its **detail**: name, class, company, address and its kind;
the signal (now, min, mean, max, two minutes as bars) and the rhythm (the
interval between advertisements, how many were heard, how often the bytes
changed); a sensor's or a beacon's values; and every AD structure of the
advertisement and of the scan response explained line by line, with the
raw bytes. From there:

- **Buscar** (the finder): the signal in big numbers, smoothed over the
  strongest of every two seconds, whether it is getting stronger, the last
  minute as bars, and a beep that comes faster the closer it is. Walk
  slowly and turn around: the body blocks the signal, so the side it comes
  from is the loud one.
- **Conectar** (the GATT explorer), when it advertises as connectable: its
  services and characteristics with their names, what each allows, values
  read (in words for the known UUIDs, as text, and in hex), writes (text,
  or bytes as `0x01 A0`), notifications and indications as they come, and
  "Leer todo", which reads every readable attribute in turn. No pairing:
  the store of bonds is the phone's, and a characteristic that wants an
  encrypted link says so.
- A **star** and a **name** of our own, kept in `ble/nombres.txt` on the
  card (`AA:BB:CC:DD:EE:FF|*|Heladera`), which the portal's file browser
  can edit too.

## What it decodes

`main/bl_decode.c` and `main/bl_names.c`, pure C, tested on the Mac by
`test/run.sh` (packets written by hand from each format's spec, the
simulator's neighbourhood, and a fuzzer under ASan/UBSan):

- Every AD type of the Core Specification Supplement, flags spelled out.
- Companies (the SIG's identifiers people meet), 16-bit UUIDs (services,
  characteristics, descriptors, members), well known 128-bit ones (Nordic's
  UART...), appearances.
- Sensors: BTHome v1 and v2, pvvx and ATC1441 (Xiaomi thermometers with
  custom firmware), Xiaomi's MiBeacon, Govee, Ruuvi (RAWv1 and RAWv2),
  SwitchBot, Qingping, Inkbird, Eddystone TLM. Encrypted payloads are said
  to be so, never guessed.
- Beacons: iBeacon, AltBeacon, Eddystone UID, URL, TLM and EID.
- Apple's Continuity messages (Nearby Info, Handoff, AirPods, Find My...),
  Microsoft's Swift Pair, Google's Fast Pair.
- GATT values of the known characteristics (battery, temperature,
  humidity, pressure, heart rate, Device Information, PnP ID...).

### Sensors that encrypt

A stock Xiaomi thermometer (LYWSD03MMC and its kin, MiBeacon v4/v5) and
BTHome v2 devices set up with a key advertise their readings encrypted with
AES-CCM. With the device's 16-byte key the app decrypts them
(`main/bl_crypt.c`, plain C: the firmware's mbedTLS is not in the apps'
symbol table) and they read like any other sensor.

- **The key** goes in from the device's detail ("Cargar la clave", 32 hex
  digits), or into `ble/claves.txt` on the card
  (`AA:BB:CC:DD:EE:FF|<32 hex>`). The detail says whether it matches.
- **Xiaomi's bindkey** comes from the Mi Home account the sensor is paired
  with (tools such as "Xiaomi Cloud Tokens Extractor"), or from Home
  Assistant if it already reads the sensor. MiBeacon v2/v3 (12-byte keys)
  is not supported, and the detail says so.
- **Checked** against FIPS-197 and NIST SP 800-38C (AES, CCM with 7, 8 and
  12-byte nonces), bthome.io's encryption example, and a MiBeacon v5
  packet; the simulator has a stock Xiaomi that encrypts with the test key
  00 01 02 .. 0F.

The distance is the log-distance path loss model: the power at a metre is
the beacon's calibrated one when it says it, the advertised TX power minus
41 dB when it says that, and -59 dBm otherwise; the exponent depends on the
surroundings chosen in the settings (2.0 outdoors, 2.7 a house, 3.3 an
office). Walls and bodies move it a lot: it is an estimate, and the screens
say so.

## How many

The table keeps up to **1024 devices** (P4OS keeps 2048): it starts with
room for 256 and doubles as it fills (~0.8 KB each, in PSRAM), and past
the top the one not heard for longest makes room (never a favourite).
Phones change their private address every few minutes, and each new
address is a new device: "seen in total" counts them all, and Aire says how
many were forgotten. In the simulator, `AOS_SIM_BLE_CROWD=800` adds that
many phones that change their address every minute: 1,674 packets a second
at 100 %, none lost, the table full at 1024 and forgetting the oldest.

## Settings

The cog at the end of the row: listening or paused, active or passive
scanning (active asks scannable devices for their scan response, where many
say their name; passive never transmits), the share of time listening (10
to 100 %, 100 by default), the surroundings for the distance, a minimum
signal, hiding the ones gone, the finder's sound, and a **CSV** of the
sensors: a line a minute per sensor in `ble/sensores-<day>.csv`
(`ble/sensores-sin-hora.csv` before the clock is set), while the app is
open.

If the phone or the WiFi drops while it scans, a notice on top of the page
says so. One radio serves the WiFi, the phone and the link (ESP-NOW), and
listening less of the time leaves them more air.

## Measured on the watch

With the firmware of this branch, the iPhone not connected, WiFi on
(2026-10-10). Packets over 30 s, WiFi as three downloads of a 330 KB file
from the card and 30 pings while scanning:

| Duty | Packets/s | Devices | Lost | Download | Ping avg |
|---|---|---|---|---|---|
| off | — | — | — | 105-116 KB/s | 220 ms |
| 10 % | 4 | 11 | 0 | 121-128 KB/s | 204 ms |
| 30 % | 12 | 16 | 0 | 126-134 KB/s | 208 ms |
| 60 % | 29 | 15 | 0 | 129-141 KB/s | 203 ms |
| 100 % | 48 | 16 | 0 | 125-140 KB/s | 195 ms |

Listening all the time did not slow the WiFi down measurably (the ping's
200 ms is the WiFi's power saving with the screen off, scan or no scan),
which is why the default is 100 %. Internal RAM does not move: the scanner's
rings are in PSRAM, and so is everything the app keeps. The second
connection the GATT client needs costs nothing measurable at rest (internal
free 121.7 K with it, 121.0 K without). NimBLE's host task peaks at 2.5 K of
its 5 K stack scanning at 100 %.

With the app open: internal free 121.6 K (the same), PSRAM ~490 KB. A full
redraw of any page takes 89-97 ms (the watchface takes 115-128); the radar
redraws its area five times a second.

**GATT against a real device** (a stock Xiaomi thermometer, read only):
connected, 86 attributes discovered (7 services, 39 characteristics, 40
descriptors), its model, firmware, maker and battery read, and its
temperature notified every ~6 s for a minute with the scan running at the
same time. The first tries hung: with NimBLE's default connection interval
(30-50 ms) the thermometer asked for 15-30 ms a few seconds in, and the ATT
request in flight then never got its answer, so discovery ended in the 30 s
ATT timeout 7 times in 13, whether the update was accepted or refused. The
client now connects at 15-30 ms from the start: 17 in 17, discovery in 7-13
s, with the scan at 100 % or stopped (the scan was suspected and cleared:
it adds a second or two, nothing more).

`/api/ble` on the portal drives the same HAL calls from the Mac (state,
counters, the loudest addresses, a GATT connection and its table): see the
comment at the top of `components/aos_web/aos_ble_api.c`. It reads the
scanner's ring itself, so it is for measuring with this app closed.

## Building and trying it

    tools/build_apps.sh ble
    tools/install_apps.sh <watch> ble

In the simulator, `sim/ble_sim.c` makes up a neighbourhood: one thermometer
of each format, beacons, phones, earbuds, a Mac, a mouse waiting to pair, a
watch, trackers, someone walking about and someone passing by, and two that
take a connection (a heart rate strap and an ESP32 with environmental
sensing and a UART that answers in capitals). Nothing there is anybody's
real device. The simulator's Bluetooth follows `bt_on` in its preferences.

    cd sim && cmake -B build && cmake --build build -j8
    AOS_SIM_VIEW=aos.ble ./build/amoledos_sim

The decoders' tests:

    sh apps/ble/test/run.sh

The class glyphs are a small Material Design Icons font of the app's own
(`main/bl_sym_s.c`, `main/bl_sym_l.c`), made by `tools/gen_glyphs.py`.
