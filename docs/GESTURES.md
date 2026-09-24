# Two fingers and gestures

**Target: v0.6.0.** A pinch on a watch whose touch chip, by every account,
could not see a second finger. This file tells how it was found, what was
measured, how each fault of the chip is filtered, and what is left.
Every number here was measured on the board (Waveshare
ESP32-S3-Touch-AMOLED-1.8 **v2**, CO5300 + CST820) unless it says otherwise.

| Phase | What | State |
| --- | --- | --- |
| 0 | Probe: does the CST820 report a second finger? | done, 2026-09-24 — **yes** |
| 1 | HAL: both fingers on every read, `aos_hal_touch_frame()` | done |
| 2 | Recogniser: `aos_gesture.h` (tap, double tap, long press, drag + fling, pinch) | done, tuned in the simulator |
| 3 | Simulator: Option-drag, `pinch:` / `drag:` script steps, 14 Hz ration | done |
| 4 | Photos: native-resolution decode, zoom, pan, fling, paging | done, on the watch for testing |
| 5 | 3D viewer app | planned (**v0.6.1**) |
| 6 | The rest of v0.6.0 (below) | in progress |

### v0.6.0 scope (agreed 2026-09-24)

| # | Item | State |
| --- | --- | --- |
| B1 | Fingers one by one for pads: `aos_touch_points()` (id per finger, dropout hold, jump filter) | **done**: a "stick" finger kept its id for 10 s while three "fire" fingers came and went |
| B2 | Drawing without stealing touch samples | **done**: the touch task — 73 samples/s whatever the screen draws |
| B3 | Gestures in Lua (`aos.gesture`) | open |
| B4 | Settings → Touch → Try gestures | **done** |
| B5 | The CST820's scan period (0xEE) and config registers | **answered**: the CST820 does not implement the CST816's configuration (0xEE reads 00, most of 0xEC..0xFE read 0); nothing to tune, the real limit was ours (B2) |
| A1 | Doom: stick and FIRE at once (B1) | written, on the watch |
| A2 | Pixel Art: pinch to zoom the canvas, two fingers to pan | written, on the watch (sim: x2.7, the first finger's stroke taken back) |
| A3 | Control PC: the mouse face's surface is a touchpad (move, tap, two-finger scroll, two-finger tap = right click, pinch = cmd+=/-) | written, on the watch |
| A4 | Mila: pinch in = the whole room (stays), pinch out or tap = back to her | written, on the watch |
| A5 | Buscaminas: 16x16 and 20x20 boards, with zoom and drag | written, on the watch |
| A6 | Golf: pinch and drag on the aiming map | open |
| A7 | Laberinto: bigger mazes, with zoom | open |

## Phase 0 — the probe

### The question

The CST820's datasheet (and the CST816S/D's) promises "single-point gestures
and real two-point operation". The only register map anyone published (the
CST816S register declaration) documents `0x02 FingerNum` as "0: no finger,
1: one finger" and a single X/Y pair at `0x03..0x06`. Every driver found
(Espressif's `esp_lcd_touch_cst816s`, InfiniTime, Linux `hynitron-cst816x`,
RIOT) reads one point; the PineTime wiki and RIOT say the chip "always
returns a single finger". One project (OpenWatchFace, 2026-09-22) probes for a
second point at `0x09..0x0C` — the FocalTech 6-byte stride — untested. No
public pinch on this family existed.

### The method

A fork of the raw touch view (Settings → Touch → Raw view) swapped the
driver's read — 5 bytes from `0x02` — for a burst of `0x00..0x0E`, in the same
transaction slot, from the same task (a second poller on the I2C bus is what
set off watchdogs once before). Every change of the registers went to
`/api/log`; a pink dot was drawn where the "second point" said.

### What the chip does

```
reg   one finger            two fingers
0x01  00                    00            gesture id (never set while we poll)
0x02  01                    01   (!)      finger count: NEVER above 1
0x03  XH  point 1           XH
0x04  XL                    XL
0x05  YH                    YH
0x06  YL                    YL
0x07  00                    XH  point 2
0x08  00                    XL
0x09  00                    YH
0x0A  00                    YL
0x0B  FF                    1x            echo of point 2's Y (high nibble 1)
0x0C  FF                    YL
0x0D  FF                    00
0x0E  FF                    00
```

**The second finger lives at `0x07..0x0A`, same format as point 1, and
`0x02` keeps saying 1.** That is why no driver sees it: they all read the
count, find 1, and stop. The OpenWatchFace probe looks two bytes too far and
reads half of point 2 and half of the echo (the first version of our probe
did the same, and its pink dot slid along the diagonal: x = y = the echo).

Logs, one line per change (`tp <0x02> x1,y1 x2,y2 d<distance>`):

```
two fingers, one top one bottom       tp 1 182,440 179,70
pinch ↕ closing then opening          tp 1 165,238 164,320 d82  ...  tp 1 181,9 185,439 d430
pinch ↔ opening                       tp 1 110,256 277,263 d167 ...  tp 1 18,257 355,266 d337
pinch ↖↘ closing and opening          tp 1 176,280 171,255 d25  ...  tp 1 280,439 75,9 d476
still, 2.5 s                          tp 1 137,82 137,422 d340   (one line: not one change)
```

### Quality, measured

| Case | Result |
| --- | --- |
| pinch vertical, horizontal, ↖↘ | clean: each point follows its finger, distance 25..530 px |
| two fingers still | no jitter at all (d = 340 for 2.5 s without a single change) |
| pinch ↗↙ | **the X of the two points get swapped** (fingers bottom-left and top-right read (252,441) and (15,3)) or collapse to the same X, and point 2 drops out for up to **0.9 s** |
| lifting the first of two fingers | the other one stays **only in slot 2 with `0x02` = 0** for ~200 ms: LVGL sees a release with a finger still down |
| the sample where a finger lifts | point 1 jumps to garbage once ((65,426) → (299,443)) |
| a finger landing | the first sample of point 2 is sometimes garbage (1,447) |

The ↗↙ swap is what a self-capacitance chip does: it measures rows and columns
separately, so with two fingers it knows two X and two Y but not which goes
with which. Distance and midpoint are the same for both pairings, which is
why a pinch survives it and a two-finger rotation would not.

### Rate — first wrong, then measured

- **First reading, wrong: "the chip gives ~14 Hz".** The raw view logged a
  new point every ~70 ms while LVGL read at 27.5/s. It was our own limit:
  the chip was read from LVGL's task, on LVGL's clock, and the raw view's
  logging timer missed samples while the screen redrew.
- **LVGL's read rate is whatever the screen lets it be**: 88 reads/s on a
  light screen, 27 on the raw view, **11 while Photos zoomed** a photo.
- **Measured with the touch task (Phase 6, B2): the CST820 delivers ~73 new
  samples a second** with a finger moving (60..77 with two), read at ~76/s
  — its INT line pulses at that rate. The datasheet's ">100 Hz" is the scan;
  the reports come at ~75 Hz. Five times what we first believed.
- Its configuration registers (0xEC..0xFE) mostly read 0 on the CST820
  (`CST820 id B7 proj 41 fw 02 | 0xEC..0xFE: 01 01 00 00 00 00 00 00 00 00 00
  00 00 00 70 00 00 17 FF`): no scan period to tune, and no 10 s long-press
  reset (0xFC = 0; two fingers held 14 s, nothing happened). IrqCtl 0xFA =
  0x70: INT pulses on touch, on change and on motion.

## Phase 1 — the HAL

`components/aos_hal/aos_hal_esp32.c`, "Two fingers".

- The driver's `read_data` is swapped once, at start, for ours. On the CST820
  it bursts `0x00..0x0E` (15 bytes, ~0.5 ms at 400 kHz) where the driver read
  5, and hands point 1 to LVGL exactly as the driver did: **LVGL still sees a
  single pointer and nothing that exists today changes.**
- Both points go to an `aos_touch_frame_t`:

  ```c
  typedef struct {
      uint8_t  count;        /* 0, 1 or 2 */
      int16_t  x[2], y[2];   /* digitiser coordinates, [0] first */
      uint32_t seq;          /* +1 per NEW sample */
      uint32_t t_ms;         /* when it was read */
  } aos_touch_frame_t;
  bool aos_hal_touch_frame(aos_touch_frame_t *out);
  ```

- The count is derived, not read: `0x02 ≥ 1` and point 2 set → 2; `0x02 ≥ 1`
  → 1 (point 1); `0x02 = 0` and point 2 set → 1 (point 2, the finger left
  behind).
- `seq` moves only when `0x02..0x0A` change, so a consumer sees each chip
  sample once however often LVGL reads.
- Any other controller (the v1's FT3168, documented as two-point) keeps the
  driver's read and the frame copies up to two points from it. **Untested**:
  nobody here has a v1.
- `aos_hal_touch_regs()` gives the raw view its 15 registers.

New symbols, nothing removed or reshaped: **no app needs rebuilding**.

## Phase 2 — the recogniser

`components/aos_ui/aos_gesture.c`, API in `include/aos_gesture.h`.

```c
aos_gesture_attach(area, 0, on_gesture, me);
/* TAP, DOUBLE_TAP, LONG_PRESS, DRAG_BEGIN/DRAG/DRAG_END (fling vx,vy),
 * PINCH_BEGIN/PINCH/PINCH_END (scale per event, centre, centre motion) */
```

It reads frames, not LVGL's pointer, from a 10 ms timer that only runs while
a touch that STARTED on the object is in flight. Each rule answers a
measurement:

| Rule | Because |
| --- | --- |
| a second finger counts after 2 samples in a row | the first sample of point 2 can be garbage |
| during a pinch, one-finger samples are ignored; when the second finger returns the pinch re-anchors (no jump) | point 2 drops out for up to 0.9 s on ↗↙ |
| scale = distance ratio, position = midpoint | both are the same for either pairing of the swapped X |
| a midpoint that jumps > 90 px in one sample re-anchors | the collapsed-X case moves the midpoint, fingers do not |
| after two fingers nothing is a drag until every finger is up | point 1 is garbage the sample a finger lifts; and a model spinning when you let go of a zoom is worse than one that waits |
| a press waits up to 300 ms for its first sample with a finger | LVGL's press can arrive before the frame that carries it (seen in the simulator) |
| fling speed over the last ~160 ms; 0 if the last move is > 150 ms old | samples are 70 ms apart; a finger that rested before lifting must not fling |
| tap ≤ 300 ms and < 10 px; double tap within 300 ms and 48 px; long press 550 ms | the usual, with a 70 ms sample grid |

Attaching makes the object clickable, not scrollable and not chained to a
scrollable parent. Apps that drag want `AOS_APP_FLAG_NO_SWIPE |
AOS_APP_FLAG_LONG_DRAG` while the gesture area is up (Photos sets them only
while a photo is on screen).

## Phase 3 — the simulator

- **Option + drag** = two fingers, the second mirrored around the centre of
  the screen (as the iOS Simulator does); **Option + Shift** freezes the gap
  and both move together.
- Script steps: `pinch:CXxCY:D0:D1:MS` (two fingers side by side, distance
  D0 → D1) and `drag:X0xY0:X1xY1:MS` (one finger, released at the end, so it
  flings at its real speed).
- **Samples are rationed to 14 Hz** (`AOS_SIM_TOUCH_HZ`, 0 = unrationed):
  what feels smooth at 100 samples/s stutters on the watch. The ration is also
  what exposed the press-before-sample race above.

```bash
AOS_SIM_VIEW=aos.photos \
AOS_SIM_KEYS="ms:700,tap:184x138,pinch:184x200:60:300:900,drag:100x300:300x300:250" \
./build/amoledos_sim
# [photos] gesture 7 at 184,200 ...  zoom 100% of fit      PINCH_BEGIN
# [photos] gesture 9 at 184,200 ...  zoom 374% of fit      PINCH_END
# [photos] gesture 6 at 276,300 v=795,0                    DRAG_END, fling
```

Six runs in a row gave 304..374 % (the pinch anchors on its second sample,
which lands anywhere in the first 70 ms) and a fling of 795..805 px/s for a
drag of 800 px/s.

## Phase 4 — Photos

- A photo is decoded **once, at its own resolution** (up to 2 MB of RGB565:
  ~1024×1024), into a canvas scaled around its top-left corner; before it was
  decoded pre-shrunk to the screen, and zooming had nothing to show.
- Pinch zooms about the point between the fingers (up to 4 screen px per
  photo px, or twice the whole-photo view if that is closer), drag pans and
  flings, double tap goes to 3× there and back, tap shows or hides the
  controls, a sideways fling with the whole photo in view changes photo.
- What is on screen eases towards the target every 16 ms, because the target
  only moves ~14 times a second. Antialiasing is off while moving and back on
  at rest.
- Every finished gesture is one line in `/api/log`
  (`photos: gesture <type> at x,y v=vx,vy zoom N% of fit`), so a test on the
  watch can be read from the Mac.
- **Open:** how many frames per second a zoomed 1 MB photo draws at on the
  board, and whether it steals touch samples while it does (see Rate).

## Phase 6 — the touch task (B2)

`aos_hal_esp32.c`, "The touch task". The CST820 is now read by a task of its
own, pinned to LVGL's core with a higher priority, woken by the chip's INT
line (and by a timeout — 20 ms with a finger down, 100 ms without — so a
lost pulse never leaves the screen deaf). It is the only thing that talks to
the chip: the burst, the keep-awake write every 5 s, the diagnostics. LVGL's
read hands over the latest point 1 without touching the bus. The HAL keeps
the last 16 samples (`aos_hal_touch_frames()`) and the recogniser and
`aos_touch_points()` consume every one of them.

| | before | after |
| --- | --- | --- |
| new samples/s, finger moving | ≤ LVGL's reads (11..27 on busy screens) | **73** |
| a pinch in the simulator, unrationed 80 → 260 px | ×2.45 at 14 Hz | ×3.15 (true ×3.25) |
| "the touch feels..." | | "re suave" (the user, 2026-09-24) |

The simulator still rations to 14 Hz by default (`AOS_SIM_TOUCH_HZ`), now as
a worst case rather than the chip's rate.

## Phase 5 — the 3D viewer (planned, v0.6.1)

- **Files:** binary STL straight from the card, and a portal page (`/3d`)
  that takes STL, OBJ or GLB, reduces it in the browser to a triangle budget
  and uploads a compact format of our own (with colours).
- **Drawing:** a software rasteriser (flat shading, z-buffer) in a worker on
  core 0 blitting to the panel, as Doom (35 fps) and Turbo do — never in
  LVGL's task, or the touch loses samples. Wireframe or reduced detail while a
  finger moves, full detail at rest.
- **Gestures:** drag = orbit with inertia, pinch = zoom about the centre,
  two-finger move = pan, double tap = frame the model, tap = info overlay.

## The chip's configuration registers

From the CST816S register declaration (Waveshare), the only published map of
this family's configuration; whether the CST820 honours each one is what the
Try-gestures screen measures:

| Reg | Name | Datasheet | Why it matters here |
| --- | --- | --- | --- |
| 0xEC | MotionMask | continuous LR/UD scroll, double click | gesture engine |
| 0xED | IrqPluseWidth | 0.1 ms units, default 10 | |
| **0xEE** | **NorScanPer** | **scan period, 10 ms units, 1..30, default 1** | the datasheet promises >100 Hz; we measure ~14 Hz |
| 0xF9 | AutoSleepTime | seconds, default 2 | we already disable sleep (0xFE) |
| 0xFA | IrqCtl | EnTest, EnTouch, EnChange, EnMotion | when INT pulses |
| 0xFB | AutoReset | reset if touched with no gesture for N s | a thumb resting on Doom's stick |
| **0xFC** | **LongPressTime** | **reset after a long press of N s, default 10** | same: a held stick or FIRE |
| 0xFE | DisAutoSleep | non-zero = never sleep | written at start and every 5 s |

Try gestures logs `0xEC..0xFE` on opening, cycles 0xEE through 10, 20, 30,
50, 70 and 100 ms with its button (showing the chip's measured rate), and
puts back what the chip had on closing.

## Phase 6 — after

- The scan-period register (`0xEE` on the CST816S map): can the chip go
  faster than 14 Hz, and what does it cost in power?
- Other apps that would take a pinch: Pixel Art (zoom the canvas), Golf's
  map, Chatarra's map, the Lua API (`aos.gesture`).
- The ↗↙ diagonal: nothing to do in software beyond what is done; worth
  saying in the app guide.
- v0.6.0 release: the usual seven assets.
