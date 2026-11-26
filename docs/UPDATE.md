# Updating from the watch

From v0.18.0 the watch updates its own firmware: **Settings → System →
Update**. It reads a small file that every release carries, says whether the
release is newer than what runs, and downloads and installs it when asked. No
computer, no cable, no portal.

The portal's way (`tools/install_fw.sh`, or the image dropped on its front
page) is still there and is what a build of your own goes in by
([BUILDING.md](BUILDING.md)).

## Using it

The page looks at the latest release when it opens (unless it did in the last
hour) and shows one of:

- **Up to date**, with the latest published version.
- **A new version**, its size and a few lines about it in the watch's
  language, and **Download and install**. The bar shows how far it is; the
  watch goes on working meanwhile, and leaving the page does not stop it.
  **Cancel** stops it, and the watch is as it was.
- **Ready**: **hold** the button to restart into it. Nothing restarts by
  itself, and a brush of the screen does not either.

Under it, **Previous version**: the one in the other half of the flash, which
a hold makes the one that starts at the next restart.

When the new version needs other apps (its app interface, `AOS_ABI_VERSION`,
is not this one's), an orange line says so before installing: the `.so` files
on the card have to follow it.

## What keeps it safe

- **Only the latest published release.** The watch asks for
  `releases/latest/download/amoledos-update.json`, which GitHub answers with
  the newest release that is neither a draft nor a pre-release.
- **The image is the one published.** The manifest gives its size and its
  SHA-256; the image is hashed as it arrives, and the watch points the
  bootloader at it only when both match. `esp_ota_end()` checks the image's
  own checksum on top. A download that is cut, refused or damaged leaves what
  runs, and what boots, untouched.
- **It starts on trial**, like an image from the portal: it confirms itself
  after 30 s up, and a crash before that brings the previous one back at the
  next restart.
- **Nothing on the card or in the settings is touched.** It writes the idle
  OTA slot and nothing else.
- **The firmware is not signed**, on purpose, as with the portal's OTA
  ([SECURITY.md](SECURITY.md)): the update is as trustworthy as the GitHub
  release it comes from. The manifest is read over https; so is the image,
  unless a test source says otherwise (below).

## What a release carries

`amoledos-update.json`, beside `amoledos.bin`, written by
`tools/gen_update.py` from the very `amoledos.bin` that is uploaded:

```json
{
 "schema": 1,
 "ver": "v0.18.0",
 "date": "2026-11-26",
 "abi": 2,
 "url": "https://github.com/charliejgallo/ESP32S3_AmoledOS/releases/download/v0.18.0/amoledos.bin",
 "size": 4102432,
 "sha256": "…",
 "notes_es": "…", "notes_en": "…", "notes_de": "…"
}
```

```bash
python3 tools/gen_update.py --ver v0.18.0 --notes-dir notes/   # es.txt, en.txt, de.txt
```

It is one flat object on purpose: the firmware reads its keys without a JSON
library. A watch on a release that has no such file says the latest release
has no update for it. "Newer" follows the version the firmware was built with
(`git describe`): a release is newer than its own test tags (`v0.18.0` after
`v0.18.0-rc.1`), and not than a build made some commits after it.

## Trying it without publishing

`tools/update_test_server.py` serves a manifest and an image the way GitHub
does, redirects included; or any folder with a manifest from
`gen_update.py --url http://<this machine>:<port>/amoledos.bin` served with
`python3 -m http.server`. The watch is pointed at it from the portal:

```bash
curl -X POST http://<watch>/api/update --data-urlencode "fuente=http://<mac>:18394/amoledos-update.json" -d accion=buscar
curl http://<watch>/api/update
curl -X POST http://<watch>/api/update -d accion=instalar
curl -X POST http://<watch>/api/update -d accion=reiniciar
curl -X POST http://<watch>/api/update -d fuente=          # back to the official one
```

While a test source is set, the page says so in yellow, and only then may
the image come over plain `http://`. `tools/update_harness.c` runs the same
code on the Mac against GitHub and against the test server (26 cases).

## From the portal

| Call | |
|---|---|
| `GET /api/update` | `running`, `slot`, `trial`, `state` (idle, checking, downloading, ready, failed), `error`, `why`, the offered `ver`, `size`, `abi` and `abi_here`, `got`/`total`, `previous`, `source` |
| `POST /api/update` | `accion=buscar` (check), `instalar`, `cancelar`, `reiniciar` (only when ready), `volver` (the other slot boots next); `fuente=<address>` |

## Measured on the board (2026-11-25)

| | |
|---|---|
| The manifest, from the local network / from GitHub's 404 | 32 ms / under 1 s |
| Looking at the other slot (its image hashed whole) | 270 ms, once a boot |
| 4.08 MB from GitHub over TLS, two redirects | 48 s, 82 KB/s |
| 4.10 MB from the local network | 42 s, ~100 KB/s |
| Free internal RAM, lowest during a download | 139–147 KB (157 before) |
| Firmware size of the feature | +16 KB |

The speed is the flash's: each sector is erased as its data arrives, so the
watchdog never sees a long erase (the lesson of v0.14.0).

## A watch with the date of 1970

A certificate is checked against the date. On 2026-11-25 the watch showed the
right time with the date at 1 January 1970: the RTC had kept the time of day
and lost the year, the preference said the clock had been set, and nothing
synchronises over NTP by itself after a restart. The update's jobs now sync
first when the date is impossible (before 2025-11-24), and the RTC is written
back with it.
