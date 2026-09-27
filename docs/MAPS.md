# Maps

A street map on the watch: OpenStreetMap's data as vector tiles from
[OpenFreeMap](https://openfreemap.org/), drawn by the watch itself in a dark
style for the AMOLED. Pinch and drag, zones saved from the portal, whole zones
downloaded to the card for use without a connection, and a search with an old
phone's keypad. The app is `apps/mapas` (`demo.mapas`); the portal page is
`/mapas`.

Everything below was measured on the board (v2, charlie.local) over Buenos
Aires, the densest case at hand, unless it says the simulator.

## Why vector tiles, and why OpenFreeMap

| | raster (tile.openstreetmap.org) | vector (OpenFreeMap) |
| --- | --- | --- |
| key, account | none, but a usage policy | none |
| downloading a zone for offline use | **forbidden** by the tile usage policy | allowed ("no limits on requests") |
| zoom between levels | a blurred picture | redrawn sharp at any scale |
| style | theirs, light | ours: near-black for the AMOLED |
| same data online, in the cache, in the packs | yes | yes |
| decoding on the watch | PNG (LVGL has lodepng) | protobuf + our own rasteriser |

The offline requirement settled it: bulk downloads from the OSM tile servers
are against their policy, and OpenFreeMap exists precisely to be used without
limits. The price is drawing the map on the watch, which is most of this
document.

OpenFreeMap serves the OpenMapTiles schema up to z14. The TileJSON at
`https://tiles.openfreemap.org/planet` names the current build
(`.../planet/20260913_164504_pt/{z}/{x}/{y}.pbf`); the app reads it once and
keeps the template in a preference. Tiles come uncompressed when the request
does not ask for gzip, which the HAL's client does not: nothing to inflate.

### What a tile weighs

| tile (Buenos Aires) | bytes | of which |
| --- | --- | --- |
| z14 5534/9872, the centre | 501 936 | **poi 386 895 (77 %)**, building 36 414, housenumber 29 419, transportation 25 019 |
| z12 1383/2468 | 248 035 | transportation 174 363, place 24 052, poi 22 139 |
| z10 | 131 700 | |

The watch draws neither points of interest nor house numbers, so those layers
go before anything is stored: a tile is a list of layers, and dropping some is
copying bytes, not re-encoding (`mp_mvt_strip`). The z14 tile above goes to the
card as ~85 KB. The POIs are not lost for search: the portal reads their names
into the zone's index before stripping them.

## The pieces

```
 portal /mapas (browser)                         watch (apps/mapas)
 ──────────────────────                          ──────────────────
 MapLibre + OpenFreeMap style                    mapas.c    UI, gestures, network, frames
 Photon search                                   mp_render  tiles -> pixels, labels
 zones.txt, goto.txt   ──/api/upload dir=maps──▶ mp_store   RAM -> packs -> card cache -> network
 <zone>.amp  (tiles, stripped)                   mp_mvt     protobuf -> geometry by class
 <zone>.idx  (names)                             mp_draw    polygons, AA lines, glyph atlas
                                                 mp_search  .idx and Photon
```

`tools/map_pack.py` writes the same `.amp` and `.idx` from the command line,
for zones too big for the portal or straight to a card.

## Drawing

### A buffer bigger than the screen

The worker (core 0, priority 3) renders the map into a 560 x 640 buffer, the
screen plus 96 px all round, and there are two of them: LVGL's side shows one
while the worker fills the other. Every frame, LVGL's side cuts the screen out
of the newest buffer, moved by the pan or scaled (nearest pixel) by a pinch or
a zoom animation, and pushes it. A pan never waits for a render; the worker
starts a new buffer when the screen gets within 12 px of the edge, when a zoom
has been still for 140 ms or has drifted more than 0.55 of a level, and when a
tile arrives.

### Pushing the frame

| | compose | blit | pan |
| --- | --- | --- | --- |
| whole frame composed in PSRAM, one blit | 22.9 ms | 28.0 ms | 18 fps |
| **two 16-row strips of internal RAM, in turns** | 15.7-17.7 ms | 13.2-14.7 ms | **28-31 fps** |

With strips (as the Cameras app does), the panel's DMA sends one strip while
the next is cut out of the buffer, and the source of the DMA is internal RAM.
A pinch, which scales, runs at 29 fps. 23 KB of internal RAM.

### The renderer, and what it cost

Tiles are decoded once into int16 points grouped by drawing class (water,
parks, each road class, rail, labels...), so the renderer paints class by
class across all the tiles on screen: the roads of one tile go over the water
of the next. Each tile is clipped to its own square, which also hides the
buffer of features every MVT tile repeats past its edge.

Render times of one buffer, centre of Buenos Aires, as the fixes went in:

| | z13 (four z12 tiles) | z12.3-12.8 | z15 |
| --- | --- | --- | --- |
| first version | 517-560 ms | | |
| bounding box per feature, off-buffer features skipped | | 316-379 ms | 57-74 ms |
| residential areas not drawn | 381-567 ms | 113-168 ms | 67-70 ms |
| thin lines (≤ 2.5 px) Wu-style instead of capsules | **238-318 ms** | **113-151 ms** | **67-74 ms** |

- **Residential areas** were the dearest class at z12 and their colour was a
  shade off the land's: they are not drawn.
- **Thin lines**: a road is a chain of capsules with anti-aliased edges, and a
  capsule costs a square root per edge pixel. At z13 the minor streets of a
  city are ~28 000 segments of about one pixel; drawn one step per pixel along
  the long axis with the coverage of each pixel across it, as Wu draws lines,
  they went from 115-230 ms to 96-130.
- **A full-buffer fill** (the river, open water) is ~45 ms however it is
  written: PSRAM's write rate is the wall, not the number of stores.
- **Labels** are placed on a 5 px occupancy grid, highest rank first; street
  names follow the street, glyph by glyph, rotated with bilinear sampling from
  an atlas of Montserrat baked when the app opens (three sizes, Latin-1, a
  2 px halo; 0.53 s at start). The screen's buttons, the scale and the
  attribution are reserved on the grid.

### When a tile is missing

The nearest ancestor already in RAM is drawn enlarged in its place (up to six
levels up), and only if there is none, the parent or grandparent is looked for
on the card. A z12 tile of a city centre is 0.4 s of card, and looking for it
before the ones already in memory made the first zoom-in render take 620 ms.

## The network

| | measured |
| --- | --- |
| a 132 KB tile, alone | 405 ms, 333 KB/s |
| two tiles at once, TLS | 13 KB/s; one cut after 20 s; `wifi:bcn_timeout` |
| the TileJSON, cold handshake | 1.5 s |

**One download at a time.** Two TLS downloads of hundreds of kilobytes filled
the WiFi's 16 receive buffers faster than TLS emptied them, and the beacons
were what got dropped: the access point gave up on the watch and it
reconnected. One at a time is 25 times faster.

**Power save off while downloading** (`aos_hal_net_low_latency`), and for 4 s
after, for the next tile.

**A cut download still says 200.** The HAL asks for HTTP/1.0, where a response
ends when the socket closes, so a dropped connection hands the app "200 and
fewer bytes". The first version cached a 2 KB piece of a 240 KB tile. A tile
is now accepted only if it parses to its very end as a list of layers
(`mp_mvt_complete`); a cut lands inside a layer. The body ceiling is 1.1 MB
and a tile at the ceiling is refused too.

## On the card: /maps

| | written by | read by |
| --- | --- | --- |
| `zones.txt` | the portal; "Save this view" appends | the list (≡) |
| `goto.txt` | the portal's "Show it on the watch" | the worker, every 1.5 s; then deleted |
| `<zone>.amp`, `<zone>.2.amp`... | the portal, `tools/map_pack.py` | `mp_store` |
| `<zone>.idx` | the same | the search |
| `cache/z/x/y.mvt` | the app, for every tile seen online | `mp_store` |
| `bench.txt` | you, to time a scripted pan and zoom | the app, which deletes it |

`zones.txt` is one zone per line, tab-separated: name, latitude and longitude
in millionths of a degree, zoom in tenths (the 256 px convention; MapLibre's
zoom plus one). `goto.txt` is the same without the name.

`.amp` (little-endian): `"AMP1"`, tile count, min and max zoom, flags, the box
(W S E N x 1e6), the name (32 bytes, UTF-8), the index offset (64), then one
20-byte entry per tile `{z, pad[3], x, y, offset, length}` sorted by
(z, x, y), then the stripped tiles. The app keeps each pack's index in RAM
(20 bytes a tile) and binary-searches it. A zone bigger than one upload (the
portal takes 8 MB) is several packs with the same name; the list shows one
row.

`.idx`: one name per line, `key \t name \t kind \t lat \t lon`, sorted by key
(lower case, no accents). Kinds: `lugar`, `calle`, `agua`, `parque`, or the
OSM class of a point of interest (`restaurant`, `school`, `bus`...).

A neighbourhood (Palermo Soho, ~2 km across, z6-14) is 25 tiles, 3.2 MB of
pack and a 450 KB index with 7 403 names. The low zooms are most of the pack:
z3-5 alone were 800 KB of continent, which is why the portal starts at z6.

## The portal page

`/mapas` loads MapLibre GL and OpenFreeMap's "liberty" style from the
internet, so it needs a connection on the computer (not on the watch). It
searches places with Photon, saves the view as a zone, sends it to the watch
(`goto.txt` and `abrir`), and downloads what the map shows: six tiles at a
time from OpenFreeMap, the names read from each tile before its POI layer is
dropped, the packs built and uploaded part by part, the index after, and the
zone added to `zones.txt`.

**The size is measured, not guessed.** Before a download the page fetches a
sample spread over the zone (12 tiles of the deepest zoom, 6 of the next, 3
of the one after, one of each low level), strips them as the download will,
and extrapolates; the index is counted from the sample's names and halved,
because an avenue is in dozens of tiles and once in the index. The first
version multiplied the tile count by 130 KB, measured over Palermo, where the
heavy low zooms are most of a small zone: it said 35 MB for the city of
Buenos Aires (15.6 real) and 202 MB for Greater Buenos Aires (33.8 real).
Checked against those two downloads, the sample says 15.1 and 31.5; on
zones taller than those it came out up to ~15 % short. The downloads
themselves were right: every tile of the box was in the packs, and the ones
compared byte by byte matched OpenFreeMap's, stripped.

**Wake the watch first.** A 3.2 MB upload to a watch asleep with its screen
off was cut after a minute; awake, it took 20 s. The page sends
`que=despertar` before every file.

## Search

The ≡ menu has "Search": a 3 x 4 keypad of 112 x 66 px keys, as on a phone
before touch screens. A key's letters come in turn when it is tapped again
within a second; its letters are shown under the text with the one that would
go in marked. ⌫ deletes (a long press clears), 0 is the space, ✓ searches.

The worker reads every `.idx` in 16 KB chunks and keeps the 22 best matches:
the query at the start of the name first, then at the start of a word, then
anywhere, places before streets before points of interest, shorter names
first. With a connection, Photon (`photon.komoot.io`, biased to the map's
centre) adds up to eight more, marked with the WiFi sign; it knows streets
with their numbers, which the index does not. Picking a result moves the map
there with a pin and the name.

## Traps met on the way

- **A `malloc()` of 1 KB or less goes to internal RAM** on this configuration
  (`CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=1024`). The first build baked each
  glyph mask with its own malloc of 600-1000 bytes: three fonts took the
  internal RAM the worker's stack needed and the worker did not start. All the
  app's memory now goes through `mp_malloc` (`heap_caps_malloc` with
  `MALLOC_CAP_SPIRAM`), and each font is one block.
- **The portal's injected touches (`/api/mem?tap=`) do not reach
  `aos_gesture`**: they go into LVGL's input device, and the recogniser reads
  the touch task. The bench (`bench.txt`) drives the view from the app
  instead.
- **Blitted frames are invisible to `/api/captura`**, as in every app that
  pushes to the panel: judge the screen by eye or in the simulator.

## Not done yet

- There is no GPS on the board: "where am I" is a zone, Clima's city, or the
  last view.
- Street names at z13 cost 80-95 ms of the first render; placing fewer
  candidates would help.
- Asking for gzip would halve the downloads, with an inflater in the app.
- Points of interest are only in the search, not on the map.
