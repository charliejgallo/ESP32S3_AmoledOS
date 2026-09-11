# PIXEL ART — a drawing app with frames

Eight canvases ("lienzos") of 8x8 or 16x16 cells, painted from a 32-colour
palette. A canvas holds up to 16 frames: duplicate one, move a few cells, and
the stack plays back as an animation on the watch and goes out to the card as
a looping GIF. A single frame goes out as a PNG. The same files open in the
web portal (`/pixel`), where they are drawn with a mouse and saved back; the
watch notices the change and reloads.

| | | |
|---|---|---|
| ![](../../docs/img/app-pixel-gallery.png) | ![](../../docs/img/app-pixel.png) | ![](../../docs/img/pixel-kitten.gif)<br>the kitten, as the app exports it |

## The samples

The first time the app opens with an empty folder it writes four canvases:
the **black kitten walking** (16x16, four frames: the legs alternate, the
tail wags and the flowers slide left, which is what makes it look like it is
going somewhere), a beating heart (8x8, three shades), a winking face and a
checkerboard. A preference (`px_seed`) remembers it, so deleting them is
respected. The kitten is drawn as four 16-line tables with a one-character
legend per colour in `pixel.c`; that is content, not code, and the loader
sends it to PSRAM.

## Using it

**Gallery.** Eight slots. A filled one shows its first frame and, if it is an
animation, a badge with the frame count. An empty one asks for the size.

**Editor.** The bar has: back, previous frame, the frame counter (**tap it to
play**; tap anywhere to stop), next frame, the tool button, and the menu.

- The **tool button** wears the current colour and cycles pencil → fill →
  colour picker. The picker takes the colour of the cell you tap and goes back
  to the pencil by itself.
- The **palette strip** at the bottom scrolls sideways with the finger; the
  selected swatch has a white border.
- The **menu**: play, duplicate frame, blank frame, export GIF, export PNG,
  undo (one stroke, one fill, or one clear), speed (80 to 1000 ms per frame),
  delete frame, clear frame, delete canvas (asks twice).

Everything is saved on its own: three seconds after the last change, when
leaving the editor, when the app goes to the background, and on exit.

## Files

| Where | What |
|---|---|
| `/sdcard/pixel/lienzoN.pix` | the canvas, N = 1..8 |
| `/sdcard/pixel/lienzoN.gif` | every frame, looping, 128x128 |
| `/sdcard/pixel/lienzoN-F.png` | frame F, indexed PNG, 128x128 |

Without a card the `.pix` files go to the flat data directory in SPIFFS and
there are no exports to speak of (nothing to take them away on). The portal
only sees the card.

The `.pix` format is twelve bytes of header, the palette in RGB888 and one
byte per cell per frame; it is written up in `px_file.h`. It carries its own
palette so that a file with different colours still opens: the loader maps it
onto ours by nearest colour. The portal page has the same reader and writer in
JavaScript; the firmware never parses one, it only stores and serves bytes.

## The layout, and the panel that decides it

The digitiser of this board reports nothing above y = 55 nor below y = 395
(both measured; see `AOS_TOUCH_Y_MIN` / `AOS_TOUCH_Y_MAX` in `aos_hal.h`).
The first version put its bar of buttons at y = 8..48, which the simulator
happily accepts, and on the watch every tap on it arrived as y = 55: the bar
was dead. So everything touchable lives in 56..390: the bar at 56..92, the
canvas at 96..336, the palette at 340..386; the two dead strips carry only
what is read, the status line at the top.

## How it draws, and what it cost

- **One canvas of 240x240 RGB565 shown 1:1**, 115 KB in PSRAM through
  `malloc()`; a cell is 15 px (16x16) or 30 px (8x8). Painting a cell writes that square into the buffer and calls
  `lv_obj_invalidate_area()` on it alone: LVGL blits a few hundred pixels. A
  frame change rewrites the buffer and invalidates once.
- **Nothing is destroyed from an event callback.** Both screens are built at
  `create()` and shown or hidden; leaving the app is a flag the timer applies.
  The menu is the exception: built when opened, hidden on close and deleted by
  the timer on the next tick.
- **Internal RAM is the budget, and LVGL objects are what spend it.** The
  first build on the board had ~120 objects (a box, a plus, a badge and a
  label per slot; 32 swatch objects; the menu always built) and cost 32 KB of
  internal RAM. Opened from the menu -whose forty icons already hold 26 KB-
  with BLE and wifi up, that left 8 KB, and **the microSD driver could no
  longer allocate its DMA buffers**: every read and write failed
  (`sdmmc_cmd: allocate_dma_buf: not enough mem`), the gallery showed eight
  empty slots, a new canvas could not be saved, and the autosave retried
  fifty times a second. Now a slot is one canvas with the border, the plus
  and the frame badge painted into its buffer; the palette is one canvas in
  a scrolling container; the menu exists only while open; the files are read
  **before** the objects are created; and a failed save backs off ten seconds
  and says so once. A file that exists and cannot be read shows as
  "no se lee" instead of "vacío", so the card failing is never mistaken for
  an empty slot.
- **The encoders are the app's own.** The firmware has lodepng as a decoder
  only and no GIF at all. The PNG writes stored deflate blocks (exact pixels,
  no compression, 16.5 KB for 128x128) and the GIF does real LZW with the
  width changes and the CLEAR the format asks for. `tools/px_harness.c`
  compiles them with a bare `cc`, writes both, decodes them with readers
  written from the spec, and compares every pixel. It found one thing on the
  first run — in the harness's own decoder, not the encoder: the KwKwK case
  emitted its first character first instead of last.
- **Size**: the `.so` is 32 KB, `.text` about 17 KB (a third of the 48 KB
  reservation), 88 symbols, all in the firmware's table.
- **The page and the firmware.** `/pixel` needs no handler of its own: it
  uses `/api/list`, `/api/download`, `/api/upload` and `/api/delete` with
  `dir=pixel`, which `resolve_dir()` maps to `/sdcard/pixel`. The app polls
  the file's size and mtime every three seconds while it is not dirty and
  reloads it when they differ; the gallery does the same over its eight files.

## Development switches (simulator only)

```
PX_DEMO=1    writes the samples if the folder is empty
PX_SLOT=n    opens canvas n straight away
PX_FRAME=f   ...on frame f
PX_MENU=1    ...with the menu open
PX_NEW=1     opens the size chooser
```

```bash
cd sim && AOS_SIM_VIEW=aos.pixel PX_DEMO=1 PX_SLOT=2 ./build/amoledos_sim
python3 tools/portal_dev_server.py      # then http://localhost:8088/pixel
cc -O1 -Wall -Iapps/pixel/main apps/pixel/tools/px_harness.c apps/pixel/main/px_file.c apps/pixel/main/px_export.c -o /tmp/px_harness && /tmp/px_harness /tmp/px
cc -O1 -Iapps/pixel/main apps/pixel/tools/px_convert.c apps/pixel/main/px_file.c apps/pixel/main/px_export.c -o /tmp/px_convert && /tmp/px_convert lienzo1.pix kitten.gif 12   # a .pix to GIF/PNG on the Mac
```

Mind the simulator's opening animation when scripting taps: the app slides
in from the right for the first second, so a `tap:` at 1.2 s lands on a
different cell than it says. Put `ms:1500` first.
