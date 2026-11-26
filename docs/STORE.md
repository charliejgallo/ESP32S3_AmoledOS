# The Store

From v0.18.0 the portal has a **Store** page (`http://<watch>.local/tienda`,
under Apps in the side menu): the apps and watchfaces of the latest published
release, with their icons, pictures and a line about each, and a button that
installs, updates or removes them. The watch does the downloading: it fetches
every file from the GitHub release, checks it, and puts it where it goes —
the card for apps, the flash for watchfaces. No `apps.zip` to unpack and copy.

The firmware itself is updated from the watch, in Settings → Update
([UPDATE.md](UPDATE.md)).

<p align="center">
  <img src="img/portal-tienda-ficha.png" width="640" alt="The Store's page for Golf: its icon, three pictures, the line about it, Remove, and its four files each with a tick">
</p>

## Using it

- **The list.** Each app with its icon (drawn from the same bytes the
  launcher draws), its name and a line in the watch's language, its size,
  and what it is on this watch: nothing, **Installed**, **Update**, or
  **Copied by hand**. Chips filter by kind of app, by what is installed and
  by what has an update; the box searches names and descriptions.
- **An app's page** — a tap on its row, or `/tienda#<id>` as a link — has its
  pictures, what it is, every file it is made of with a tick for those the
  watch has as published, and **Remove**.
- **Install / Update** puts the app in a queue; the watch takes them one at a
  time and the page shows how far it is. **Update all** queues every update.
- **Restart.** The card's apps and the watchfaces are loaded when the watch
  starts, so after an install the page says so and offers the restart. A
  page of an app in the portal (Weather's, Radio's...) needs no restart.
- **Copied by hand.** An app that is on the card with no record of where it
  came from — every app copied from `apps.zip` before the Store — shows as
  such. **Check them** has the watch hash those files once (about two minutes
  for a full card) and take as installed each one that is byte for byte the
  catalogue's. One that is not shows **Update**.

What an app saved (records, settings, its files in its folders) is not part
of it and stays through updates and removals.

## What the watch checks

The page decides what an install is: it sends the watch, for each file, where
it goes, its size, its SHA-256 and its address. The watch does not take the
page's word for it:

- **Only the release's addresses**: `https://github.com/charliejgallo/ESP32S3_AmoledOS/releases/download/…`,
  or the test source's host while one is set (below).
- **Only where a piece may go**: `apps/<name>.so|.html|.pak[.N]` on the card,
  `faces/face_<x>.so|.html` in the flash, `lang/<xx>/<id>.lang` on the card.
  Plain lower-case names, nothing starting with a dot, no `..`.
- **Only the published file**: each one is downloaded to `<name>.p`, hashed
  as it arrives, and renamed over the old one only when its size and SHA-256
  are the catalogue's. A cut or a wrong file leaves the old one as it was.

None of this is a new door — whoever reaches the portal can already put any
file on the card with `/api/upload` — but it keeps a broken page, or a
download tampered with on its way, from writing where nothing should be.

What none of it is: a review of what the code does. An app is native code
with every permission of the system; the Store's apps are this project's own
release, the same files as `apps.zip`.

**Receipts.** What the Store installed is written down in
`/sdcard/.store/installed.txt`, a line per file: where, its size, its hash. A
file whose receipt has the catalogue's hash, and whose size is still the
receipt's, is the catalogue's; anything else is looked at again. The
catalogue fetched last is kept beside it, `/sdcard/.store/catalog.json`, and
fetched again when the page opens and it is older than a day, or with **Check
for new ones**.

## What a release carries

`tools/gen_store.py` builds it from the release's own `apps.zip` and
`lang.zip`:

```bash
python3 tools/gen_store.py --release v0.18.0 --apps <apps.zip unzipped> --lang <lang.zip unzipped> --out build/store
gh release upload v0.18.0 build/store/*
```

`build/store` is flat, the way a release keeps its files: `amoledos-store.json`
and one file per piece — `clima.so`, `clima.html`, `monsterhop.pak.1`,
`face_agua.so`, `lang-en-aos.clima.lang` (two languages share a name, so the
language goes in front). For v0.17.0's apps that is 168 files, 32.7 MB, and a
catalogue of 63 KB.

```json
{"schema":1,"release":"v0.18.0","date":"…","abi":2,"repo":"charliejgallo/ESP32S3_AmoledOS",
 "cats":[{"id":"juegos","name":{"es":"Juegos","en":"Games","de":"Spiele"}}, …],
 "items":[{"kind":"app","id":"demo.golf","file":"golf","cat":"juegos",
           "name":{"es":"Golf","en":"Golf","de":"Golf"},"about":{"es":"…","en":"…","de":"…"},
           "icon":"<AIC bytes, base64>","colors":["1E6B34","0E3A5C"],
           "shots":["https://raw.githubusercontent.com/…/v0.18.0/docs/img/app-golf-aim.png", …],
           "size":1290000,
           "files":[{"dest":"apps/golf.so","url":"…/golf.so","size":148908,"sha256":"…"},
                    {"dest":"apps/golf.pak", …}, {"dest":"lang/en/demo.golf.lang", …}]}, …]}
```

Where each part comes from:

| | |
|---|---|
| `store/catalog.json` | the category, the line about it in three languages, the prefix of its pictures in `docs/img`: what the code does not say |
| the simulator | the name and the icon, as the app says them of itself (`AOS_SIM_STOREDUMP`) |
| `sim/sim_fs/lang/{en,de}/_sistema.lang` | the names in English and German |
| `apps.zip`, `lang.zip` | the files themselves |

A new app goes into `store/catalog.json` with its line; `gen_store.py` says
which `.so` of the card it does not describe. `abi` is the firmware's
`AOS_ABI_VERSION`: a watch whose own is another is told to update the
firmware first, and the buttons are off.

## Trying it without publishing

```bash
python3 tools/gen_store.py --release v0.18.0-test --apps … --lang … --out /tmp/store \
    --base http://<mac>:18395/ --pictures https://raw.githubusercontent.com/charliejgallo/ESP32S3_AmoledOS/main/docs/img/
(cd /tmp/store && python3 -m http.server 18395)
curl -X POST http://<watch>/api/store --data-urlencode "fuente=http://<mac>:18395/amoledos-store.json" -d accion=refrescar
curl -X POST http://<watch>/api/store -d fuente=           # back to the official one
```

While a test source is set the page says so, and only then may files come
from that host, over plain `http://` too. Changing the source drops the
catalogue kept from the other one.

## From a script

| Call | |
|---|---|
| `GET /api/store` | the state: `busy`, `job` (refresh, install, verify), `item`, `piece`, `index`/`count`, `got`/`total`, `jobs_done` (counts up), `last_ok`, `last`, `restart`, `catalog`, `catalog_age_s`, `source`, `abi_here` |
| `GET /api/store/catalog` | the catalogue kept on the card |
| `POST /api/store` | `accion=refrescar` (fetch the catalogue), `cancelar`; `fuente=<address>` (empty: the official one) |
| `POST /api/store/install` | text: `item <id>`, then a line per file, `dest⇥size⇥sha256⇥url` |
| `POST /api/store/verify` | text: `dest⇥size⇥sha256` per file copied by hand; a receipt for each that matches |
| `POST /api/store/look` | text: a `dest` per line; answers `dest⇥size⇥sha256` (size −1: not there; sha `-`: no receipt) |
| `POST /api/store/remove` | text: a `dest` per line; the files go, and their receipts. A watchface in use is taken off first with `/api/esferas/quitar` |

One job at a time; a second is answered `409`. Removing and looking are
immediate.

## Measured on the board (2026-11-25)

| | |
|---|---|
| The catalogue, 64.5 KB, from the local network | 0.86 s |
| A small app (Dice: `.so` and two translations) | 1.9 s |
| Golf, 1.26 MB with its pack | 12.7 s |
| A watchface into the flash | 2.6 s |
| Eight watchfaces' translations, queued from the page | 38 s |
| Checking a card copied by hand: 150 files, 32 MB | 119 s, once |
| Free internal RAM, idle, with the Store in the firmware | 156 KB, as before |
| Firmware, the page included | +36 KB |

These were measured against a test source on the local network. From
GitHub each file adds its TLS handshakes and two redirects, a second or two
(not measured for the Store yet; the firmware's 4 MB came at 82 KB/s).
