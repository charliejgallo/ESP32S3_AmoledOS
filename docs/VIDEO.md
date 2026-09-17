# Video

Playing a film on the watch, from the card, with sound. What the board can
do, measured, and what the design had to be for it to do it. The app is
[`apps/video/`](../apps/video/), the converter is
[`tools/video_convert.sh`](../tools/video_convert.sh), and the two pieces of
firmware it needed are the JPEG decoder (`espressif/esp_new_jpeg`, exported to
the apps) and one background task per app (`aos_hal_worker_*`).

## What plays

An **AVI of MJPEG frames at exactly 368x448**, and beside it a **16-bit PCM
WAV** with the same name. The converter makes both from anything ffmpeg reads:

```bash
./tools/video_convert.sh clip.mp4              # clip.avi + clip.wav next to it
./tools/video_convert.sh clip.mp4 12 5 out/    # fps, JPEG quality (2 best .. 31), folder
```

Upload both to the card's `videos` folder from the portal (Files → Videos), or
with `curl -X POST "http://<board>/api/upload?dir=videos&name=clip.avi"
--data-binary @clip.avi`. The app lists the `.avi` files; tap one to play,
tap the picture to pause, swipe back to stop. It loops.

Why this format and not something cleverer:

- **MJPEG**: every frame is its own JPEG, so a frame that is late is skipped
  with a seek and nothing smears; the decoder is the one the board has
  (below); and there is no state to keep between frames, which is what makes
  the worker below trivial.
- **The screen's exact size**: the decoder writes the frame straight into the
  buffer the panel is fed from. No scaling on the watch, which the numbers
  below say it could not afford.
- **Two files**: the sound goes to the firmware's player
  (`aos_hal_player_play`), which takes a path to a WAV and already drives the
  ES8311 from its own task. The app never touches audio hardware; it asks the
  player where it is and follows.
- **Mono 16 kHz**: 32 KB/s of card, next to the picture's 200-400 KB/s.

## The decoder

The photo viewer decodes with LVGL's TJPGD, which is small and portable and
decodes in blocks during the drawing. For video that is too slow, and the
alternative is Espressif's `esp_new_jpeg`: a prebuilt library with the S3's
SIMD instructions in its colour conversion and IDCT, and RGB565 output. It
lives in the firmware (a prebuilt `.a` cannot be linked into a `.so`) and its
eight entry points are exported to the apps through the symbol table
(`EXTRA_SYMBOLS` in `tools/gen_symbols.py`, since a prebuilt library never
shows up under `build/` for the generator to walk). It costs 77 KB of flash.

Measured on the board with `/api/jpegbench?file=<photo>&n=5`, which decodes a
JPEG from the photos folder repeatedly and reports the times, the memory and a
fingerprint of the output (a decode that wrote nothing cannot pass for fast):

| 368x448 JPEG | bytes | esp_new_jpeg | TJPGD (photo viewer) |
| --- | --- | --- | --- |
| `testsrc`, flat colours | 15 KB | 21 ms | — |
| mandelbrot zoom, quality 5 | 42 KB | (4:4:4: refused, see below) | — |
| game of life, all noise | 89 KB | 67 ms | ~230 ms (415 ms with the 187 ms read) |

- **Internal RAM while open: 8.5 KB**, nothing leaked after close. The output
  buffer (330 KB) is the app's, in PSRAM, 16-byte aligned as the library
  demands.
- **It refuses 4:4:4 JPEGs** (`JPEG_ERR_UNSUPPORT_FMT`, -6). ffmpeg writes
  4:4:4 for an RGB source unless told otherwise, so the converter forces
  `-pix_fmt yuvj420p`, which is also the cheapest to decode. The photo viewer
  keeps TJPGD for exactly this reason: it decodes whatever a phone produces.
- **The card is slower than the decoder.** 88 KB read in 187 ms, 42 KB in
  81 ms: about 470 KB/s, whether the destination is PSRAM or internal RAM
  (measured both; the SDMMC bounce buffer is not what limits it). A 27 KB
  frame is 57 ms of reading before a byte is decoded.

## Where the time goes, and the worker

The first player decoded in the LVGL task, from an `lv_timer`, like every
app here draws. It played, and it measured, with `reloj.avi` (15 fps, 12 KB
frames, `testsrc`) and the stats line the app keeps at the top of the screen:

| where the work runs | read | decode | result |
| --- | --- | --- | --- |
| LVGL task, sound off | 27 ms | 20 ms | 7.7 fps, UI starved |
| LVGL task, sound on | 40→90 ms | 30→40 ms | 5-6 fps, UI starved |
| worker on core 1, priority 3 | 100 ms | 20 ms | 8 fps, UI fine |
| worker, priority 5, frame through an LVGL canvas | 37 ms | 26 ms | 10 fps, frames 250 ms late |
| worker, priority 5, frame blitted to the panel | 30 ms | 20 ms | **15.0 fps, nothing skipped** |

Four things in that table:

1. **Read + decode + LVGL's own render of a full screen do not fit in a
   frame period** when they share one task. The render alone is the blit of a
   330 KB canvas into the draw buffer plus a 16.5 ms push over QSPI.
2. **With that task saturated, everything else waited behind it**: touch,
   the back swipe, the portal's screen capture ("the interface did not answer
   in 5 s"). An app that hogs the LVGL task is an app the watch cannot leave.
3. **A worker below LVGL's priority reads slowly**, sound or no sound: at
   priority 3 a 12 KB read took 100 ms instead of 27. Every SDMMC transaction
   ends in a wait, and when the data arrived the worker had to wait for LVGL
   to finish rendering on its core. At 5, level with the player, the read is
   back to 30-37 ms and the UI does not feel it: the worker is pinned to
   core 1 and LVGL, which floats, keeps core 0.
4. **LVGL's render of a full-screen canvas is the last wall.** With the
   worker idle a third of the time (it had frames ready and nowhere to put
   them) the frames still reached the screen 250 ms late at 10 fps: pointing
   the canvas at the new buffer and invalidating cost about 95 ms a frame in
   LVGL's software blit plus flush. So the frame now goes **straight to the
   panel** (`aos_hal_display_blit`, big-endian RGB565 as the decoder can
   write it), over the same QSPI the LVGL port flushes through, under the
   LVGL lock; the push is the 16.5 ms already measured for a full screen. The
   stats line and the progress bar travel INSIDE the frame: LVGL renders the
   text into a small hidden canvas when it changes, and the app copies it
   into each frame (byte-swapped) before the blit. As LVGL objects they were
   wiped by every blit and redrawn a refresh later, a visible flicker. The
   portal's capture shows black during playback for the same reason: it
   snapshots LVGL's pixels, not the panel.

So reading and decoding moved to a **worker**: the one background task an
app may have, new to the HAL with this branch (`aos_hal_worker_start`,
`_stop`, `_should_stop`, `_sleep`; see the comment block in `aos_hal.h` for
the rules). It is pinned to core 1, away from WiFi and Bluetooth. The app's
function loops on its own, polls `should_stop()` and returns; `stop()` waits
for it. No queues and no semaphores are handed out on purpose: the handshake
is plain flags in the app's memory, one writer each.

The player's ring is four slots, each a compressed frame plus a decoded
RGB565 frame in PSRAM (about 2 MB in all). The worker owns a slot from FREE
to READY: seek past the frames the UI said to skip, read one, decode it, mark
it. The LVGL timer owns it from READY on: when the frame's time comes it
blits the buffer to the panel, marks the one that was on screen FREE, and
if it is running late tells the worker which frame to read next (with the
lap it means, since a skip asked for at the end of the file must not carry
into the next lap). The frame on screen stays SHOWN, since the panel's DMA
may still be reading it, until the next replaces it.

**The audio is the clock.** The player reports its position in whole seconds;
the app keeps a millisecond clock and re-anchors it each time that second
ticks over, which is the one moment the position is exact. A frame is shown
when its time comes and never before; a dropped frame is invisible, a click
in the sound is not, so the picture follows the sound and not the other way.
Without a WAV the clock is the uptime.

In the simulator the worker is a pthread and only reads: the JPEG is drawn
through LVGL's TJPGD from memory, which has to happen in the LVGL thread
(`LV_USE_FS_MEMFS` is on in `sim/lv_conf.h` for that). Same source, same
screens, 15 fps on the Mac.

## What it does now

From the board's log, worker at priority 5, frames blitted to the panel,
sound on, ten seconds into each clip:

| clip | frames | per frame | read | decode | worker idle | shown late by | skipped | fps |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| `testsrc`, 15 fps | 12 KB | 66 ms | 30 ms | 20 ms | 15 ms | 1-3 ms | 0 | 15.0 |
| `testsrc`, 12 fps | 12 KB | 83 ms | 31 ms | 20 ms | 29 ms | 2-3 ms | 0 | 12.0 |
| mandelbrot zoom, 12 fps | 27 KB | 83 ms | 50-70 ms | 25-32 ms | 0-14 ms | 2-3 ms, then 150 as the reads climb | 9 | 10.8-12.6 |

The mandelbrot clip is the ceiling: at 27 KB a frame the worker's read plus
decode is the whole period, it has no idle time and the first lap starts
late until it catches up. Busier pictures than that want a lower quality
(`-q:v 7`) or 10 fps. The converter's default is 12 fps, quality 5, which
plays every frame of ordinary footage; a clean, flat picture plays at 15.

## The stats line

While it plays, the line at the top says `<fps> fps  r<read> d<decode> ms
-<skipped>  <frame>/<frames>`: frames shown per second, the average read and
decode time of the frames shown, how many were skipped since the start, and
where it is. The same line goes to the log (`/api/log`) every two seconds,
which is where the numbers on this page came from; the portal's capture
works during playback too, now that the LVGL task is free.

## Traps met on the way

- **`idf.py reconfigure` re-solves every dependency.** Adding
  `esp_new_jpeg` to the manifest moved LVGL from 9.5.0 to 9.6.0 on its own,
  the symbol table stopped linking (five LVGL functions gone), and the apps
  and the simulator were still on 9.5.0: the silent-corruption case
  `build_apps.sh` warns about. LVGL is now pinned to `==9.5.0` in the
  firmware's and every app's manifest; moving it is a deliberate step for all
  three at once.
- **An `fseek` to where the file already is costs the same as any seek**:
  it was the first suspect for read times that grew with the position in the
  file (41 ms at frame 30, 94 ms at frame 170). The reader now only seeks when
  the file is not already there. The growth turned out to be the sound
  (above): it was gone with the WAV removed.
- **`setvbuf` is not in the symbol table**, and it would not have helped: a
  frame is one `fread` of tens of KB, which newlib hands to the filesystem in
  one go; stdio's small buffer only serves the 8-byte chunk headers.
- **With the sound on, big frames read slower the further into the clip
  they are** (52 ms at the start of the mandelbrot clip, 70 ms near its end;
  flat 31 ms for the 12 KB clip, sound or not). The player task reads its WAV
  in 4 KB pieces between the video's reads, so the card alternates between
  two files; the reader itself never seeks. Not understood yet. The
  experiment to run: a bigger read in `player_task` (16 KB instead of 4),
  which quarters the interleaving, measured with the same clip.
- **A whole frame in one `esp_lcd_panel_draw_bitmap` call fails.** The SPI
  bus is created for the LVGL port's strip (20 rows, 14.7 KB), and a 330 KB
  transaction fails after its first chunk: `spi transmit (queue) color
  failed` per frame, the first 55 rows on the panel and the rest black. Worse,
  the board later panicked inside the SPI driver's interrupt (`spi_intr` →
  `spi_bus_lock_bg_exit`), with the failed queue attempts the only unusual
  thing going on. The blit now goes in strips of `AOS_DRAW_ROWS`, like the
  port's flush, and the app counts a failed blit as an error in its stats.
- **`lv_snapshot` does not see the panel.** The portal's capture, and the
  simulator's `AOS_SIM_SHOT`, take LVGL's own pixels; a frame blitted past
  LVGL is black in both. The pictures on the README are from the simulator,
  which draws through LVGL.
- **A skip asked for at the end of the file carried into the next lap**:
  the UI set "skip to frame 145" a moment after the worker had rewound, and
  the worker skipped the whole next lap (158 frames, two seconds of header
  reads) before the clock restarted. The request now names the lap it is
  for.
- **The photos benchmark's mandelbrot JPEG is 4:4:4** and the decoder refused
  it; the same picture as 4:2:0 in the video decodes fine. Always check the
  pixel format before blaming the decoder.
