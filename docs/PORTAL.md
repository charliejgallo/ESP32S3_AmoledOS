# The web portal

Embedded in the binary, served by `esp_http_server` on port 80 whenever the
board is on a network or its own setup access point is up. Everything lives in
`components/aos_web/`: one HTML file per page, one shared stylesheet and one
shared script, and `aos_web.c` with the handlers.

## Pages

| URL | What it is |
|---|---|
| `/` | **Home.** Battery, network, memory and system cards fed by `/api/status`; wake / screen off / sync time / restart; the firmware (OTA) upload. |
| `/ajustes` | **Settings.** The same controls as the watch's Settings app: brightness, watchface, launcher style, always-on and its brightness, volume, the four power switches, time zone, set the time, language, wifi / Bluetooth, notifications. Each control applies on release. |
| `/alarmas` | **Alarms.** The six slots of the Alarms app: time, on/off, delete, add. A change is stored in NVS and the alarm service reloads on its next tick, redrawing the app if it is open. |
| `/pantalla` | **Screen.** A live capture of the panel, with the controls to drive it from the browser: wake, off, back, watchface, menu, a toast, and a button per app to open it. For testing without the watch on the wrist. |
| `/archivos` | **Files.** The card by folder (apps, photos, music, recordings): upload by drag, download, delete, listen, and a preview for photos. Shows the free space. A fifth tab, **Card**, is an explorer of the whole card: any folder, breadcrumbs, new folder, delete an empty one. |
| `/registro` | **Log.** The ESP_LOG output, tailed over wifi from a 16 KB ring in PSRAM. Filter, pause, save as text. |
| `/wifi` `/ap` `/red` | Connect to a network, configure the setup access point, read the network surveys. |
| `/clima` `/cotiz` `/sensores` `/remoto` | Per-app configuration: weather location, exchange rates, Home Assistant sensors, the remote-control profile. |

All pages share `aos.js` (language, the navigation ribbon, the live status
strip, helpers) and `aos.css` (tokens for dark and light, following the
system theme). Spanish is written in the HTML; `en` and `de` dictionaries ride
in each page and are chosen by the **watch's** language, not the browser's.

## API

Form-encoded bodies for POST, JSON back. Everything the pages use:

```
GET  /api/status                 the whole state; the strip polls it every 15 s
GET  /api/ajustes                the settings, with the list of watchfaces
POST /api/ajustes                any subset: brillo volumen aod aod_brillo esfera
                                 menu ahorro cuidar panel_slp chip_slp tz wifi bt
                                 notif notif_sonido llamadas
POST /api/accion                 que=despertar|apagar|volver|inicio|menu|beep|
                                 sync_hora | hora&epoch=N | abrir&id=X | toast&texto=T
GET  /api/apps                   the launcher's apps, and which one is open
GET  /api/alarmas   POST /api/alarmas   i=N&minuto=M&on=0|1  (minuto=-1 clears)
GET  /api/log?desde=N            the ring from offset N; X-Desde / X-Hasta headers
GET  /api/captura[?sin_despertar=1]   the screen, as BMP
GET  /api/lang   POST /api/lang  language
GET  /api/list?dir=  POST /api/upload  GET /api/download  POST /api/delete  POST /api/mkdir
                                 dir is one of apps photos music recordings redes lang,
                                 or sd / sd/<path> for the explorer (validated piece by
                                 piece: no dot-files, plain ASCII, nothing FAT forbids);
                                 the list flags folders with "dir":true
POST /api/ota    POST /api/ota/restart
GET  /api/pmu?...                the power experiments (docs/POWER.md)
```

## What it costs the board

The rule for this component: **the browser does the work.** A page is bytes in
flash sent as they are; the JSON answers are built on the server task's stack
in pieces of a few hundred bytes and streamed in chunks; no handler keeps a
static buffer in internal RAM. The one allocation the portal makes -the log
ring and its line buffer- is in PSRAM, so the whole feature set adds a mutex
and a few pointers to internal RAM. Moving the PM dump of `/api/pmu` from a
static array to PSRAM took 2 KB *off* the static internal footprint.

What is NOT free: the server itself (one task with an 8 KB stack and its
sockets) — that already existed, and it stops when there is no network.
`/api/captura` borrows 322 KB of PSRAM while the capture is taken, and the
Screen page's automatic refresh asks for one every few seconds; cut it when
nobody is looking.

## Thread safety

The server runs in its own task. Anything that touches LVGL is **never** done
from a handler: it is written down through `aos_ui_request_*()` and applied by
`aos_ui_tick()` with the lock held (open an app, back / home / menu, watchface,
launcher style, toast, language). Settings that end in a panel command
(brightness) or in the HAL's state are applied under `aos_hal_lock()`.

## Working on it without the board

```bash
python3 tools/portal_dev_server.py        # http://localhost:8088
```

serves the same files with the same API against `sim/sim_fs`, with invented
values for what the board would measure and a log that grows on its own. The
settings it saves go to the same `prefs.txt` the simulator reads.
