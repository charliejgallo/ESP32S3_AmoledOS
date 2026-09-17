# Writing a dynamic app, end to end

The long form of [APP-API.md](APP-API.md). That page is the contract - the
callbacks, the flags, the icon, translation in five lines. This one is how
the twenty-three apps in [`apps/`](../apps/) were actually written: the
workflow, the drawing techniques and what each costs on the board, how to
fetch data and how to be configured from the portal, how to test without the
watch, and every trap that bit along the way. Everything measured is marked
as such; the rest is a rule that came out of a measurement.

A dynamic app is a `.so` on the microSD. The firmware loads it at boot, and
adding one - icon included - never means rebuilding or reflashing the watch.
Three things still do: a **symbol** the firmware does not export (section
13), a **new HAL capability**, and a **portal page** to configure it
(section 10). Know which of those you need before you start.

Before that, one question worth asking once: **maybe your app should not be
dynamic.** If it needs nothing the firmware lacks and should work with the
card out, a built-in app in `components/aos_apps/` is cheaper to keep in
step. The calendar is that case. Dynamic is for what brings its own world: a
game, data from the internet, a configuration page of its own.

---

## 1. What an app is

A `.so` in `/sdcard/apps/`. The firmware `dlopen()`s it, looks up two exported
functions, checks the ABI number and registers it in the launcher like any
built-in app.

The app **links against nothing**: every call into LVGL, the AmoledOS API and
libc is left unresolved in the `.so`, and the firmware fills them in at load
from its symbol table. A `.so` runs from 1 KB (hello) to ~50 KB (a game with
generated sprites).

```
apps/
  common.cmake          shared build configuration, do not touch
  hello_app/            the template
  ...
  my_app/               <- yours
    CMakeLists.txt
    sdkconfig.defaults
    main/
      CMakeLists.txt
      idf_component.yml
      my_app.c
```

### Before you write any of this: does it have to be C?

Since v0.3.15 there is a Lua interpreter on the card, and a `.lua` file in
`/sdcard/lua` is an app of the launcher like any other — its own name, its own
icon, opened from the same grid. No toolchain, no symbol table, no reboot to
install, and a mistake is a message with a line number instead of a watchdog
reset.

What you give up is reach: a script draws into a 184x224 buffer through about
a dozen primitives and has no LVGL, no network and no files of its own. What
you gain is the edit loop — the portal's `/lua` page saves and the watch
reloads the running script by itself.

The speed is usually not the reason to choose C. Measured on the board, the
interpreter does some 1.9 M loop turns a second and a frame of the cube bench
is 3 ms of script against 26 of pushing pixels at the panel. If your app is a
small game or a visual, try it in Lua first; if it needs LVGL widgets, HTTP, a
worker task or the microSD, it is a `.so`. [LUA.md](LUA.md) has the whole API.

A module can also bring **several apps** (`AOS_APP_ENTRY_MANY`), which is how
one `.so` turns every script on the card into a launcher entry; the contract
and its two traps are in [APP-API.md](APP-API.md).

## 2. Start from the template

```bash
cd apps
cp -r hello_app my_app
cd my_app
mv main/hello_app.c main/my_app.c
```

Change three things: `project(my_app)` and `project_so(my_app)` in
`CMakeLists.txt`, `SRCS "my_app.c"` in `main/CMakeLists.txt`, and the `id` /
`name` in the `.c`. The template already brings its own icon, marks its
strings for translation and registers its event codes one by one, so the
pattern comes with the copy. Do not touch the `include(elf_loader)` line:
`apps/common.cmake` points it at the repository's copy, which is the one
that compiles the `.so` with `-Os -ffunction-sections`; without it the `.so`
comes out in `-O0` and twice the size.

Then, before writing anything else:

```bash
cd ../../sim && cmake -B build && cmake --build build -j8
AOS_SIM_VIEW=demo.my_app ./build/amoledos_sim
```

The simulator compiles `apps/*/main/*.c` into itself (`AOS_APP_ENTRY` registers
the app at startup under `AOS_SIM_BUILTIN`), so the same source runs on the
desktop with the mouse as the finger. Design there; the board is for measuring.

## 3. Life cycle, beyond the table

The callbacks and flags are in [APP-API.md](APP-API.md). What that table does
not say:

- **Animations get their own `lv_timer_create()` in `create()`, deleted in
  `destroy()`.** `tick()` is five times a second and for coarse work.
- **`self` is the runtime's copy, not a duplicate.** `app->create(app, ...)`
  is called with the entry of the runtime's table, so `KEEP_AWAKE` and
  `FULLSCREEN` can be changed **from `create()`** through `self->desc.flags`
  when the decision depends on configuration and not on code (Remoto keeps
  the screen on only if the profile has gestures). `NO_SWIPE` cannot: the
  gesture handler may run before your `create()` has, so it goes in `init()`.
- **The physical button** reports `AOS_BUTTON_PRESS` on press and `CLICK` or
  `LONG` on release, so a game can hold fire. Return `false` to let the
  runtime do the usual (click = back, long = clock). **If you keep the long
  press too, give the user another way out** (2043 uses its pause card) or
  they are locked in. It runs with the LVGL lock held but from the HAL's
  background task: note it in the context, act on the next frame.
- **`BACKGROUND` keeps your `.so` loaded for good**, because a background app
  is not destroyed on exit and an undestroyed app is never unloaded. If you
  only need to stay alive *sometimes* - while recording, while a countdown
  runs - set and clear the flag live through `self->desc.flags`, from the
  paths that start and finish the work **and from your timer**, because the
  work may finish on its own. The Recorder held 38 KB of code this way until
  it did that.

## 4. What you can use

**All of LVGL**: widgets, styles, animations, timers, `lv_malloc` / `lv_free`.

**A background task**, one per app, for work bigger than a frame:
`aos_hal_worker_start/stop/should_stop/sleep`. The contract is in
[APP-API.md](APP-API.md) and the app that needed it, with the ring of frames
it builds on it, in [VIDEO.md](VIDEO.md). Until then everything ran in LVGL's
task, and it still does for every app but Video.

**The JPEG decoder** the firmware carries (`esp_jpeg_dec.h`, Espressif's
`esp_new_jpeg`): 4:2:0 and 4:2:2 JPEGs to RGB565, three times faster than
LVGL's TJPGD, 8.5 KB of internal RAM while open. Numbers in VIDEO.md.

**The HAL** (`aos_hal.h`), some 80 entry points:

| Area | Examples |
| --- | --- |
| battery | `aos_hal_battery_read()` |
| IMU | `aos_hal_imu_read()`, `aos_hal_imu_steps()`, `aos_hal_imu_orientation()` |
| time | `aos_hal_time_now()`, `aos_hal_time_set()` |
| display | `aos_hal_brightness_set()`, `aos_hal_display_state()` |
| preferences | `aos_hal_pref_get_i32/set_i32/get_str/set_str` |
| sound | `aos_hal_beep()`, `aos_hal_volume_get/set()` |
| player | `aos_hal_player_play/pause/resume/stop/status()` |
| recording | `aos_hal_rec_start/pause/resume/stop/status/peaks()` |
| microphone | `aos_hal_mic_open/close/read/available/status()`, `aos_hal_mic_gain_set/get()` |
| network | `aos_hal_net_state()`, `aos_hal_net_ssid()` |
| internet | `aos_hal_http_get()` and friends (section 9) |
| files | `aos_hal_path_photos()`, `_music()`, `_recordings()`, `_data()` |
| system | `aos_hal_uptime_ms()`, `aos_hal_heap_info()`, `aos_hal_log()` |
| USB | `aos_hal_usb_mode/mode_set/busy/keys_ready()`, `aos_hal_usb_key("volup")`, `_type()`, `_mouse/click()`, `_gamepad()`, `_midi_note/cc/bend()` - see [HANDOFF-USB.md](HANDOFF-USB.md) section 4 |

**The UI runtime** (`aos_ui.h`): `aos_ui_toast()`, `aos_ui_back()`,
`aos_ui_open()`, `aos_ui_take_gesture()`. **The theme** (`aos_theme.h`): the
palette, the fonts by role (`aos_font_huge/title/body/small`) and helpers such
as `aos_label_boxed()`, `aos_button()`, `aos_page()`.

**libc and libm**: `snprintf`, `memcpy`, `strcmp`, `atoi`, `malloc`, `sinf`,
`sqrtf`, `atan2f`, and the compiler helpers (`__divsf3`, `__udivdi3`...). The
full list is `EXTRA_SYMBOLS` in `tools/gen_symbols.py`; `build_apps.sh` tells
you at build time if you use something that is not there.

State between sessions goes in preferences, **with a prefix of your own**:
`aos_hal_pref_set_i32("myapp_best", value)`.

## 5. Memory: where each thing lives

The board has three memories with different prices, and they run out for
different reasons.

| What | Where | How much | Notes |
| --- | --- | --- | --- |
| your **code** (`.text`) | PSRAM, mapped onto the instruction bus by the MMU | as much PSRAM as there is, in 64 KB blocks | the loader does it; there is no size an app has to fit any more |
| your **large buffers** (`malloc`) | PSRAM | ~8 MB | canvases, tables, anything heavy |
| your **small allocations** | internal RAM | scarce | what `malloc` hands out below 1 KB |
| LVGL's own objects | PSRAM | | the firmware wraps `lv_malloc_core`; hundreds of objects no longer cost internal RAM, only draw time |

Measured on three games (Claudito, Claude Jump, 2043): the same frames per
second from PSRAM through the 16 KB instruction cache as from internal RAM.
Keep the `-Os` the build sets: a smaller `.text` misses the cache less and
loads faster. Adding `-Os -ffunction-sections -fdata-sections` to the `.so`
build took the nineteen apps of the time from 342 to 217 KB without touching
a line - the optimisation level in the app's `sdkconfig` does nothing to the
`.so`; those flags live in `components/elf_loader/elf_loader.cmake`.

**Large buffers go through `malloc()`**, which lands in PSRAM
(`CONFIG_SPIRAM_USE_MALLOC`). With `LV_DRAW_BUF_STRIDE_ALIGN=1` and
`LV_DRAW_BUF_ALIGN=4`, a plain `malloc` already serves as a canvas buffer.

**Built-in apps** with a large static structure send it to PSRAM with
`AOS_BSS_PSRAM static my_state_t s_state;` (`EXT_RAM_BSS_ATTR` on the board,
nothing in the simulator). Not for anything an ISR or DMA touches, and not
for what is walked every frame: PSRAM goes through the cache. Dynamic apps
do not need it: their `.bss` goes to PSRAM with their `.text`.

## 6. Drawing, and what it costs

The screen is 368x448, the renderer is software, and a full-screen redraw is
~25 ms (46 with a canvas on top): about 20 fps of ceiling. Four techniques,
each right for a different kind of motion.

### 6.1 LVGL objects, when almost everything stands still

Every thing is an `lv_obj` / `lv_image` and LVGL repaints only what moved.
Gemas is this: a match-three board is still but for six cells. It is also the
natural choice for any interface app.

**The other half of the rule: reuse objects, do not rebuild them.** The
comfortable way to refresh a list is `lv_obj_clean()` and create again. On the
Mac that is 1 ms. Measured on the board, rebuilding Clima's twelve hourly
cells and seven weekly rows - some 80 objects - is **111-124 ms with the LVGL
thread blocked**. Building them once and then setting text, image source and
size: **18-31 ms**. The simulator will never tell you this. Hide spare cells
with `LV_OBJ_FLAG_HIDDEN` instead of deleting them.

### 6.2 A canvas, when everything moves

A small RGB565 `lv_canvas` you draw pixel by pixel, shown at full screen. Claudito
(92x112, x4) and 2043 (184x224, x2) do this.

**Never `LV_IMAGE_ALIGN_STRETCH`.** Measured: **129 ms per frame** (7 fps).
`lv_draw_sw_transform` builds a per-pixel alpha plane for an RGB565 source and
composites with blending instead of copying - ~190 cycles per pixel.

Upscale by an integer factor **yourself** and give LVGL a 1:1 canvas, which for
it is a flat copy:

```c
#define W 92
#define H 112
#define SCALE 4

uint16_t *buf = malloc(W * H * 2);                    /* malloc, not lv_malloc */
uint16_t *big = malloc(W * H * SCALE * SCALE * 2);    /* ~330 KB, PSRAM */

lv_obj_t *cv = lv_canvas_create(root);
lv_canvas_set_buffer(cv, big, W * SCALE, H * SCALE, LV_COLOR_FORMAT_RGB565);
lv_obj_set_size(cv, W * SCALE, H * SCALE);            /* 1:1, no STRETCH */
lv_image_set_antialias(cv, false);
```

and per frame, before invalidating:

```c
/* one write per source pixel; the repeated rows are memcpy */
static void expand(const uint16_t *src, uint16_t *dst)
{
    const int dw = W * SCALE;
    for (int y = 0; y < H; y++) {
        uint16_t       *row = dst + (size_t)y * SCALE * dw;
        const uint16_t *s   = src + (size_t)y * W;
        for (int x = 0; x < W; x++) {
            uint16_t c = s[x];
            for (int k = 0; k < SCALE; k++) row[x * SCALE + k] = c;
        }
        for (int k = 1; k < SCALE; k++)
            memcpy(row + (size_t)k * dw, row, (size_t)dw * 2);
    }
}
```

Claudito went from 7 to **26 fps**, 2043 from 7 to **15**. Pick a size that
divides 368x448 exactly.

### 6.3 A canvas with dirty rectangles, when little moves

The above still upscales and invalidates the whole screen every frame. With a
still background and a few moving things - a breakout, a board, a panel with
needles - pay only for what changed. Arkanos does this, and
`apps/arkanos/main/ak_pixel.c` is written to be copied.

Two small buffers: `bg`, the still background, redrawn only when it changes,
and `fb`, the frame. Per frame:

1. restore from `bg` into `fb` the rectangles dirtied last frame;
2. draw what moves, **each thing recording its rectangle**;
3. upscale and `lv_obj_invalidate_area()` the union of the two sets, nothing
   else. The area is in absolute screen coordinates; with the canvas at the
   root's origin that is `x1 = coords.x1 + x * SCALE`.

**Measured in Arkanos: 3 % of the screen per frame on average**, against 2043's
100 %.

It works for a game that **scrolls**, too, if the background does not: Claude
Jump's camera climbs all the time, but the sky is fixed in screen coordinates
(as in the original Doodle Jump) and only platforms and objects move, so the
engine pushes 6-23 % of the screen per frame. The price is **no parallax**: a
background that moves, however slowly, dirties everything and the game is back
at 15 fps. Decide that before drawing anything.

Rules that come with it: no screen shake by moving the canvas (two screens
invalidated); no animated backgrounds; the background must be repaintable
**per rectangle** (Arkanos keeps its stars in a table so that a repaint gives
the same sky); one rectangle per object, spanning old and new position, or the
dirty list overflows and starts merging everything with everything; and the
trap: **anything that moves without recording its rectangle leaves a trail**.
Nothing crashes, it just looks dirty.

Against that trap, copy `apps/arkanos/tools/ak_harness.c`: the game does not
depend on LVGL, so it compiles with plain `cc` and plays tens of thousands of
frames drawing each one **twice** - the fast path and a full redraw from the
background - and comparing buffers. A missing rectangle shows up in the first
frame it happens, with coordinates.

**When almost everything is still, go one step further and do not redraw
what did not change.** Topos's nine moles are *slots* drawn from a 16-byte
struct and nothing else; if the struct did not change (`memcmp`), the slot
costs nothing, and if it did, the union of its old and new box is rebuilt
from the background plus every slot that touches it, in order. Two rules make
it exact: a slot draws only from its parameters, and **its box is measured by
running the same drawing code with no buffer** (a "pen" that sums rectangles
instead of painting: `pen_t` in `apps/topos/main/tp_draw.c`), so it cannot
miss a pixel. `apps/topos/tools/tp_harness.c` checks every frame against a
full redraw.

**And if what is still is MOST of the screen, make it the background.** In
`burbujas` the board is up to sixty bubbles hanging there, and none of them is
a slot: they live in the background buffer, like Arkanos's sky. When a cell
changes, the game records *that cell's* rectangle in a list of its own and the
compositor repaints that patch of the background — the tiled backdrop plus
every bubble that reaches into it, clipped — before composing the slots on
top. Slots are left for what actually moves: the shot, the aiming guide, the
bursts, the falls and the launcher. Measured: 9 % of the field pushed per
frame in one mode and 18 % in another.

Two conditions make it exact, and both are easy to break:

- **The background must repaint per rectangle to the same pixels a full
  repaint gives.** That is why the backdrop is procedural; drawn at random it
  would leave a seam around every burst.
- **Repainting a rectangle must redraw everything that touches it**, not just
  the cell that changed: with 22 px bubbles 19 px apart, the neighbours reach
  three pixels in.

When the whole board does move — a row coming in, the ceiling coming down —
there is nothing to save: mark the entire background for the ten or so frames
the slide lasts and pay a full screen per frame while it does.

### 6.4 An ARGB8888 canvas, when the piece has to blend with the background

The three techniques above give opaque pieces. Rounded corners, a shadow, any
soft edge - a card on a green cloth - would need the background painted inside
the piece and matched, and a gradient behind it is enough to make the seam
show. The way out is a canvas in `LV_COLOR_FORMAT_ARGB8888`, which the software
renderer both draws into and blends from:

```c
void *buf = malloc((size_t)W * H * 4);                    /* PSRAM */
lv_obj_t *cv = lv_canvas_create(parent);
lv_canvas_set_buffer(cv, buf, W, H, LV_COLOR_FORMAT_ARGB8888);
lv_obj_set_size(cv, W, H);
lv_obj_set_style_radius(cv, 0, 0);                        /* the theme adds one */
lv_image_set_antialias(cv, false);
lv_canvas_fill_bg(cv, lv_color_hex(0), LV_OPA_TRANSP);    /* all transparent */
```

then draw with `lv_canvas_init_layer()` and the usual primitives;
`lv_draw_rect_dsc_t` has `shadow_width` / `shadow_offset_*` / `shadow_opa`, so
the shadow comes out of the same `lv_draw_rect`. Four bytes per pixel instead
of two: Truco's six cards are 31 KB each in PSRAM, nothing; a full screen
stays RGB565.

**And the piece is an object.** Moving a card is `lv_obj_set_pos()` and LVGL
invalidates old and new area on its own. It is the cheap way to animate a
few large things over a still background - the counterpart of dirty
rectangles, which are for many small ones.

### 6.5 Sprites drawn by code

Gemas generates its eleven gems when the app opens instead of shipping
bitmaps: each is a convex polygon, and for each pixel the ray from the centre
is tested against the edges. ~80 KB of RAM, zero flash, and colour and size
vary without redoing assets. If you need alpha, **RGB565A8** is the format
LVGL blends without converting: colour and alpha in separate planes, `stride
= w*2`, the alpha plane starting at `w*h*2` with half the stride.

### 6.6 What is expensive in LVGL

- **Scaling or rotating an image** draws the object into a separate layer and
  transforms it. Six objects, fine; sixty-four, not. To animate a blend use
  **opacity**, which is a mix inside the same blit.
- **An object with children** transforms the whole subtree. A badge over an
  animated sprite should be a **sibling**, not a child.
- **A repeated background** is one image with `LV_IMAGE_ALIGN_TILE`, not N
  objects.
- **Creating and destroying objects** is expensive on its own (6.1).
- **A layer is a risk, not only a cost.** The firmware once ran LVGL on its own
  64 KB heap; when a layer buffer did not fit, `lv_draw_layer_alloc_buf`
  returned NULL and LVGL **retried forever**: 100 % CPU, watchdog, frozen
  screen. LVGL now allocates from the system heap with PSRAM behind it, but
  the lesson stands.

Measured on the board with Clima, for scale:

| | Mac | ESP32-S3 |
| --- | --- | --- |
| generate a 104x104 antialiased sprite by code | 1 ms | 24 ms |
| rebuild 80 list objects (`lv_obj_clean` + create) | 1 ms | 111-124 ms |
| refill those same 80 objects | 0 ms | 18-31 ms |
| a 1.7 KB GET over WiFi | ~950 ms | ~1000 ms |
| flush an opaque 368x368 canvas (Vida) | ~1 ms | 35 ms |
| the same, with a label refreshing every frame beside it | ~1 ms | 47 ms |

The rule that falls out: **on the Mac everything in LVGL looks free**.
Multiply by twenty or thirty before deciding that something fits.

## 7. The icon

It travels with the app, as a few dozen bytes of shapes handed over from
`init()`. [APP-API.md](APP-API.md) has the recipe and `apps/hello_app` is the
worked example; [ICONS.md](ICONS.md) has the format. Two rules from here:
the shapes go in white and the colour is the gradient's, so `color_a` /
`color_b` have to be dark (Dados started with a light grey gradient and its
white cube vanished); and an arrowhead is stacked rectangles that **share the
base edge**, each step's width deciding how far it reaches - aligned by the
tip they come out as a barbell (Conversor).

## 8. Translation

The essentials, with the rest in [I18N.md](I18N.md):

- **Everything visible goes through `_()`**, and the catalogue key is the
  Spanish string itself. `aos_i18n.h` is not pulled in by `aos_app.h`;
  include it. Without a pack loaded `aos_tr()` returns its argument, so
  marking too much breaks nothing and **marking too little is invisible until
  someone looks at the screen in another language**.
- **Always in pairs.** `N_()` in a `static const` table only marks (a function
  call cannot sit in a static initialiser); `_()` where the string is drawn
  translates. `N_` alone leaves a key nobody looks up; `_` alone on a table
  entry does not compile, which at least tells you.
- **The app's name goes in the system catalogue**, not in the app's: the
  launcher draws "Gemas" without having opened Gemas. The app does nothing
  about catalogues; the runtime loads its own on open and frees it on close.
- **Not translated:** `aos_hal_log()` text, POSIX TZ strings, URLs and host
  names, and proper nouns. Do not half-wrap.

The circuit:

```bash
python3 tools/gen_lang.py template en     # extracts and updates the template
python3 tools/gen_lang.py check en        # catalogue against code
python3 tools/gen_lang.py unmarked        # interface text you did not wrap
./tools/install_lang.sh <ip> en           # upload over WiFi
./tools/audit_layout.sh es en de xx       # does it fit in 368 px, in four languages
```

`template` never overwrites a translation: it adds new keys and flags the
orphans. A whole new language is the same command with another code.

## 9. Data from the internet

A dynamic app **cannot do networking on its own**: no sockets in the symbol
table, no tasks, and a request from a callback would freeze the screen for
the seconds it takes. The HAL does it in a task of its own; the app asks from
its `tick`:

```c
/* on open, or when the user taps refresh */
ctx->req = aos_hal_http_get("http://example.com/data.json", 4096);

/* in tick(), five times a second */
if (ctx->req > 0 && aos_hal_http_state(ctx->req) != AOS_HTTP_BUSY) {
    if (aos_hal_http_state(ctx->req) == AOS_HTTP_DONE) {
        const char *body = aos_hal_http_body(ctx->req);
        int         len  = aos_hal_http_len(ctx->req);
        /* ... parse ... */
    }
    aos_hal_http_release(ctx->req);     /* always */
    ctx->req = 0;
}
```

Rules with no exceptions:

- **`release()` always**, also in `destroy()` with a request in flight (the HAL
  task finishes and cleans up; the body is invalid from that instant).
- **The body is valid until `release()`.** Copy it if you need to keep it.
- **`http://` only.** No TLS, on purpose: mbedTLS competes for exactly the
  internal RAM that is scarce. A proxy on a home server is the recommended
  way to reach services that only speak HTTPS.
- **Three slots in the whole system.** Do not leave requests unreleased.
- Check `aos_hal_net_state() == AOS_NET_CONNECTED` before asking.

A 1.7 KB request takes **about a second** on the board (measured with
Open-Meteo). Show something meanwhile - a saved copy, a message - rather than
an empty screen.

**POST, headers and body.** `aos_hal_http_get()` is the shortcut of
`aos_hal_http_request(method, url, headers, body, content_type, max_bytes)`,
which exists for REST APIs such as Home Assistant's. Headers and body are
copied when the request is launched, so they can live on the caller's stack;
`Content-Length` is set by the HAL, also for an empty body (some servers
answer 411 without it). Credentials in an `Authorization: Bearer` header
travel in the clear: acceptable on your own LAN against your own server, not
against anything on the internet - and the app never writes them; the user
enters them in the portal, which keeps them in NVS.

**One request for many values.** With one request per second and three
slots, fifteen values fetched one by one are fifteen seconds of refresh. Remoto
shows up to forty entities and fetches them **all in one request**, asking the
server to join them (Home Assistant's `/api/template` returns `on|off|128` as
plain text; one `strchr` splits it). Before writing the loop that asks N
times, look for the endpoint that answers N things at once. It almost always
exists.

**Choosing the URL is half the work.** Prefer a service that serves `http://`;
ask for `HTTP/1.0` (the HAL does), so the server closes instead of chunking;
if it can give times as unixtime, ask (Open-Meteo has `timeformat=unixtime`:
local time becomes an addition instead of ISO parsing and time zones); ask
only for the fields you will show.

**JSON without cJSON.** `apps/clima/main/wx_api.c` has a ~120-line reader that
builds no tree: it looks for `"key":` inside a bounded byte range. Copying it
is cheaper than adding cJSON to the firmware's symbol table. Two traps: keys
repeat across blocks (`temperature_2m` is in `current_units`, `current` and
`hourly` - bound the block first), and tenths and large integers do not mix
(`11.4` as `114` avoids floating point, but a unixtime times ten overflows an
`int32`: times get their own reader).

The simulator has real networking - `aos_http.c` is the same file on both
platforms - so a data app is designed entirely without the board.

## 10. Configuring the app from the portal

If the user has to **type** something, do not reach for `lv_keyboard`: on 368
px its keys are 30 px and the space bar is the wide key in the last row. The
first Clima test on hardware saved the city as `Buenos` with the coordinates
of a town in Texas because the space could not be hit. The way out is the web
portal the firmware already serves ([PORTAL.md](PORTAL.md)), with the
computer's keyboard. The mould is `/clima`:

1. **An embedded `.html`** (`EMBED_TXTFILES` in `components/aos_web/CMakeLists.txt`)
   and three handlers: the page, `GET /api/whatever` returning the state, `POST`
   saving it. **Start from a copy of `clima.html`**: every page hooks into the
   shared base (below).
2. **Portal and app talk through preferences.** The firmware does not know
   your `.so`; the only thing they share is a few NVS keys with your prefix.
3. **To notice a change with the app open, re-read the preference every few
   seconds from `tick` and compare.** Crude, and right: any notification
   mechanism between firmware and a `.so` would cost far more, and there is
   nobody to notify when the app is not loaded.
4. **Whatever is heavy and can be done in the browser, do it there.** Clima's
   city search runs in the browser against Open-Meteo (its API sends
   `Access-Control-Allow-Origin: *`) and the board receives a name and two
   integers. The firmware learns nothing about the service.

**Every page hooks into the shared base.** `/aos.css` and `/aos.js` give the
pages one look, the navigation ribbon, the live status strip and the watch's
language. The minimum:

```html
<link rel="stylesheet" href="/aos.css">
...
<div class="wrap">
  <div class="top"><h1 data-t="titulo">My page</h1></div>
  <p class="sub" data-t="bajada">What it does.</p>
  <nav class="paginas"></nav>     <!-- filled by aos.js, do not write it -->
  ...
</div>
<script src="/aos.js"></script>
<script>
const TR = { en: { titulo: "My page", ... }, de: { ... } };
const $ = aos.$;
aos.init(TR).then(load);
</script>
```

Add the page to the page list in `aos.js`: one line, and the reason every
page now links to every other. **The browser's languages are not the
firmware's**: in the firmware the key is the Spanish phrase and
`gen_lang.py` extracts it; in a page the keys are short invented names
(`data-t="btn_save"`), the Spanish is what is written in the HTML, and `TR`
carries `en` and `de` by hand. The watch decides the language (`GET
/api/lang`), not the browser. Placeholders need `data-t-ph`; numbers need
`aos.idioma()` for the locale.

Two JavaScript rules that already bit: **every string from outside goes
through `aos.esc()` or `textContent`, never raw into `innerHTML`** (a
neighbour's SSID with tags executed in `wifi.html`; "outside" is also an
entity name, a file name, a city); and **check `r.ok`, not only the body** (a
400 from `esp_http_server` carries its own text, not `{"ok":false}`, and the
page said "Saved" when the firmware had refused).

**If the configuration does not fit a preference** (NVS strings top out at a
few KB; Remoto's profile passes ten), the split is: the big part to a file on
the card (`aos_hal_path_data()`; the portal writes a temporary and renames, so
a cut connection leaves the old file in place; the app reads it with
`fopen`), the small and the secret to NVS. The firmware never understands the
file. For "something changed", one integer NVS key the portal **increments**
on every save; the app re-reads it from its timer and reloads when it moves.
Send decimals multiplied (coordinates as degrees x 10000) so the firmware
never parses or prints a decimal point.

**Test a page without flashing:** `tools/portal_dev_server.py` serves the same
`.html` files and answers the same endpoints against a local folder - by
default `sim/sim_fs`, the one the simulator uses, with the same `prefs.txt`.
Page open in the browser, simulator beside it: save, and the app reloads.
That is the whole "portal writes, app notices" path without the board. Add
your page there too; it is twenty lines.

## 11. Build and install

```bash
./tools/build_apps.sh my_app          # or with no argument, all of them
./tools/install_apps.sh <ip>          # over WiFi; ALSO restarts the board
./tools/install_lang.sh <ip> en de    # if you touched text
```

`build_apps.sh` deletes the old objects, derives the LVGL configuration from
the firmware's, checks the ABI number baked into the `.so` and checks every
undefined symbol against the firmware's table. **Always build with it**, never
with `idf.py so` by hand: the step that compiles the `.so` declares its
objects with `DEPENDS` on the `.c` alone, so a change in `aos_app.h` or
`aos_hal.h` does **not** recompile them - it relinks the old object and prints
"Build Shared Object" as if all were well.

**Compiling is not installing, and the symptom misleads.** `build_apps.sh`
leaves the `.so` files in `apps/*/build/`. Skip `install_apps.sh` and the board
keeps the old binaries: the firmware looks updated and the apps do not, which
looks like an app problem and is a deployment one. The script restarts the
board because the `.so` files are loaded once, at boot.

## 12. Testing without the board

```bash
cd sim
cmake -B build && cmake --build build -j8
AOS_SIM_VIEW=demo.my_app ./build/amoledos_sim
```

| Variable | For |
| --- | --- |
| `AOS_SIM_VIEW=<id>` | start straight in your app |
| `AOS_SIM_KEYS="ms:500,tap:180x120,swipe:right,esc"` | hands-free input sequence (512 characters at most) |
| `pose:3` inside `AOS_SIM_KEYS` | the board's posture: 0 flat, 1 on edge, 2 face down, 3 **in the hand** |
| `tilt:12x-8` inside `AOS_SIM_KEYS` | tilt in hundredths, -50..50; what the mouse otherwise gives; works while a finger is held (`hold:`) |
| `AOS_SIM_SHOT=out.ppm` | dump the screen after 1.5 s and exit; `tools/ppm2png.py` turns it into a PNG |
| `AOS_SIM_LAYOUT=2000` | report every text's box and its drift from the centre |
| `AOS_SIM_BEEP_LOG=1` | print the beeps instead of playing them |

Keys: `ESC` back, `m` menu, `h` clock, `1`-`9` open app N, `a` dimmed, `w`
faces, `i` posture, **space = the physical button**. Tilt comes from the mouse
position. Posture 3, "in the hand", is the one derived from axes measured on
the board and the one to use for anything driven by moving the watch.

**Give your app development switches.** On the board `getenv()` always returns
NULL, so they are free and harmless: `GEMAS_TEST=5` serves a five-in-a-row,
`CLIMA_DEMO=1` invents data across the eight icons without the network,
`ARK_AUTO=1` lets the paddle play itself. One `getenv()` in `create()`. Check
the first character, not just the pointer: an exported empty variable is an
empty string, not NULL.

**AddressSanitizer is the most valuable thing about having a simulator.** On
the board a dangling pointer is a whole-firmware reset with no stack and no
apparent relation to what you just touched. On the Mac:

```bash
cd sim
cmake -B build-asan -DCMAKE_BUILD_TYPE=Debug \
      -DCMAKE_C_FLAGS="-fsanitize=address -fno-omit-frame-pointer -g" \
      -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address"
cmake --build build-asan -j8
AOS_SIM_VIEW=<your.id> AOS_SIM_KEYS="..." ./build-asan/amoledos_sim
```

gives both stacks: where it was read and where it had been freed. Drive a
long script through the whole app **and make it exit the app**, which is when
everything is destroyed and where these bugs live.

## 13. Before copying to the card

`./tools/build_apps.sh my_app` does the checks. By hand: `xtensa-esp-elf-nm -D
-u apps/my_app/build/my_app.so` - every name must be in
`components/aos_dynapp/aos_symbols.c` or in `elf_loader`'s libc table. If one
is missing, add it to `EXTRA_SYMBOLS` in `tools/gen_symbols.py` and
regenerate (`idf.py build && python3 tools/gen_symbols.py && idf.py build`) -
that is a firmware change. A missing `lv_font_montserrat_*` is not a missing
export: those fonts **no longer exist** (section 14).

If you touched text: `gen_lang.py unmarked`, `gen_lang.py check en`,
`audit_layout.sh es en de xx`. And once on the board, the only check that sees
the real screen: `python3 tools/captura.py <ip> /tmp/my_app.png`, one of every
screen in the longest language (German). The audits measure whether text
fits, not whether it is well placed.

## 14. Known traps

All measured, and each is written up next to the code that deals with it.

### The build

**Your app's LVGL configuration must match the firmware's.** The app compiles
LVGL's headers with *its* sdkconfig but calls the firmware's functions, and
`lv_global_t` has conditional fields (`LV_USE_OS`, `LV_USE_STDLIB_MALLOC`,
`LV_USE_FS_POSIX`...). If they differ, every inline LVGL function compiled
into your app writes the wrong field: silent corruption that blows up much
later, in another task. `build_apps.sh` derives the configuration from the
firmware; and if a `CONFIG_LV_*` of the firmware changes, **every app is
rebuilt and copied again** - reflashing is not enough.

**`lv_font_montserrat_*` no longer exists.** LVGL's own fonts are off and out
of the symbol table; `aos_montserrat_*` from `components/aos_fonts` replace
them, with Latin-1 (accents, tildes, umlauts). Old code with
`&lv_font_montserrat_16` **compiles and then does not load**. Use the role
pointers `aos_font_huge/title/body/small`, or `&aos_montserrat_20` for a fixed
size.

**The simulator and ESP-IDF are different compilers, and the second one
rules.** The Mac is clang without `-Werror`; the board is GCC with IDF's
warnings as errors. The first to bite is `-Werror=format-truncation`: to GCC a
`%d` is up to 11 characters whatever your number is, so `char buf[8];
snprintf(buf, sizeof buf, "%d", day)` compiles on the Mac and stops the build
on the board. `char buf[16]`. Build with the IDF before calling the app done.

**Apps compile with `-Og` for the objects that matter; the firmware with `-O2`.**
It does not matter for UI; for a computing loop it does (a large share of the
tuner's 284 ms of pitch analysis). Raise the optimisation **for the file that
computes**, not the whole app:

```cmake
set_source_files_properties(my_dsp.c PROPERTIES COMPILE_OPTIONS "-O2"
    DIRECTORY "${CMAKE_CURRENT_LIST_DIR}")
```

**`mode_t` is a libc typedef on macOS**: as a variable name the app does not
compile in the simulator.

### Touch and gestures

**The touch does not reach every pixel the display draws.** A touch can land
anywhere between y = 24 and y = 410 (x 16..352); between y = 56 and 390 the
precision is the calibration's, outside it a finger against the rim lands
exactly on the edge row. Put nothing you must be able to tap - the back
button above all - beyond that, and use the bottom strip for text that is
only read (a score, a status line) - but keep that text away from the two
ends, because the corner takes its bite there too: a status line drawn ten
pixels above the bottom edge lost about two characters at each end on the
board and was perfect in the simulator (the Lua bench, whose last word was
missing in a photograph of the watch). Twelve rows further up and ten pixels
in from each side clears it. In the simulator the mouse reaches
everywhere, so this is invisible there; `tools/audit_layout.sh` flags any
clickable that ends beyond the limit. And at the **top** the problem is the
glass: 38 px corner radius plus bezel eat ~10 px each side of the first rows
(Claude Jump's score read "_9M" on the board and was perfect in the
simulator). See [HARDWARE.md](HARDWARE.md).

**Every `lv_obj` is born clickable.** In LVGL 9 the constructor sets
`LV_OBJ_FLAG_CLICKABLE`, so any decoration eats the touch meant for its
container. Strip it from decorations, or put a transparent listening layer
over a large touch area.

**The back gesture breaks any app that drags a finger.** The runtime fires at
50 px of drag (`LV_INDEV_DEF_GESTURE_LIMIT`) and calls
`lv_indev_wait_release()`, which cuts the press in progress. `AOS_APP_FLAG_NO_SWIPE`
stops it from *acting*; if your drag continues **past those 50 px** (a car
across a board), add `AOS_APP_FLAG_LONG_DRAG` too, or the release never
reaches you. And listen for the gesture **on the root**, with
`lv_obj_remove_flag(root, LV_OBJ_FLAG_GESTURE_BUBBLE)`: LVGL walks up the
parents while it finds the bubble flag and delivers to the first that lacks
it, so a transparent layer at the back of the z-order - a sibling, not an
ancestor - never sees it.

**On the v2 board a fast swipe does not reach LVGL at all.** The CST816
detects gestures itself and stops sending intermediate points while it does,
so LVGL never accumulates its 50 px. Ask for the chip's gesture too, from
your timer: `aos_ui_take_gesture()` returns `AOS_TOUCH_GESTURE_LEFT/RIGHT/...`
for the front app if it asked for `NO_SWIPE`, consumed on the first call.
Both paths can arrive for a slow swipe: ignore a gesture within 400 ms of the
previous one, or a swipe turns two pages.

**Inside an app, vertical swipes do not exist** unless you ask for `NO_SWIPE`:
the runtime only acts on up/down with no app open (launcher up, launcher
down). With it you also own the exit - the calendar keeps right-swipe as
"leave", executed **deferred** from its tick, because `aos_ui_back()` destroys
the app.

### Events and life cycle

**A callback registered with `LV_EVENT_ALL` runs with your context already
freed.** The runtime calls your `destroy()` and *then* deletes the root, so
everything LVGL emits while deleting - `LV_EVENT_DELETE`, `PRESS_LOST` if a
finger was down - reaches callbacks whose context is gone. Register the codes
you use, and if `destroy()` frees the context, `lv_obj_clean(root)` first,
with a `closing` flag so the last events do nothing.

**Leaving the app is deferred.** `aos_ui_back()` destroys the app, so calling
it from an event callback destroys the object dispatching the event. Set a
flag, act at the top of the next timer tick, touch nothing afterwards. And if
your app implements `back()`, `aos_ui_back()` consults it first: an app that
always returns `true` can never be closed, without any error.

**`lv_obj_remove_style_all(root)` in `create()` deletes the root's SIZE.** In
LVGL 9 width and height are local style properties; wiping the container the
runtime gave you leaves it content-sized. Nothing fails: a small rectangle
top-left, the rest black, which reads as "the flush broke" and sends you to
the wrong place (Truco lost three rounds to it). The runtime has already done
the `remove_style_all` and `set_size`; in `create()` just paint. To start
clean, `aos_page(root)` makes a 100 % child.

**Timers that start at zero.** A `uint32_t last_time` in a freshly zeroed
context makes `now - 0` the whole uptime, and "more than a minute ago" is
true on the first tick of every open. Initialise them from the clock in
`create()`.

**A local copy of data must know what it corresponds to.** The first Clima
cached the last forecast on the card without recording which city; changing
place offline showed the old forecast under the new name. The copy carries
its coordinates and discards itself when they differ.

### LVGL specifics

**`lv_label_set_text_fmt()` does not print floats.** LVGL's own light
formatter knows `%d %u %x %s %c` and not `%f`: it prints the letter "f"
(`max 69 now f`, in the sound meter). Format with `snprintf()` - libc's, in
the table - then `lv_label_set_text()`. Better still, keep the number in
hundredths and print `%d.%02d`: printing a `float` promotes to `double` and
drags in `__extendsfdf2`, which the S3's single-precision FPU does not have.

**Repainting costs more than computing.** LVGL **does not compare**: setting a
label to the same text or a style to the same colour invalidates and redraws
just the same. Measured twice: in Vida, dropping the counter to 3 updates a
second saved 12 ms per frame; in the tuner, writing only what changed and
repainting 4 times a second instead of 10 took drawing from **77.6 to 35.5
ms**. Keep the last value written and compare; paint at the rate the data
changes, not the timer's.

**A label refreshing every frame beside a canvas costs almost as much as
enlarging the canvas.** An opaque canvas is detected as a cover and LVGL draws
from it; a counter outside it is not under anything, and every
`lv_label_set_text()` is a separate invalid area with background and glyphs.
Telemetry refreshes by the clock, not by the frame; at 60 Hz it belongs
inside the canvas.

**More than 32 invalidations in one frame repaint the whole screen.** When
`lv_inv_area`'s buffer (`LV_INV_BUF_SIZE`, 32) fills, LVGL replaces it with
the full screen. The launcher's fade wrote the opacity of two children in
every row on every scroll event - 54 invalidations for 27 apps, so every scroll
frame was a full screen. Read the style before writing and count what you
invalidate.

**Scaling a label off-centres it**: `transform_scale` scales from the top-left
corner; set both pivots to `lv_pct(50)`. **`lv_obj_align_to()` is not
recomputed** when the text changes length; use a fixed box with the text
centred. **Degree signs and the like are two-byte UTF-8**: `"\xC2\xB0"`.

**`lv_roller` sets its own height, and forcing it clips the neighbours.** Three
rows with the small font are ~90 px, not the ~66 you get adding line heights;
a smaller container overflows, a forced `lv_obj_set_size()` hides the
neighbouring options, which is the one thing a roller is for. Measure its
real height first. Conversor ended up picking units from a full-screen list.

**`lv_draw_label()` keeps the POINTER to the text, not the text.** Drawing on
a canvas with `lv_canvas_init_layer()` + primitives + `finish_layer()` queues
tasks and runs them at `finish_layer`; `lv_draw_label` copies the descriptor
**minus the string** unless `text_local = 1` and `text_length` are set. A
stack buffer formatted with `snprintf` is gone when the task runs: broken or
repeated glyphs, no crash (Buscaminas). Use static literals, or set both
fields.

**`lv_canvas_finish_layer()` invalidates the WHOLE canvas.** With the
primitives you can skip *drawing* what did not change, not *flushing* it. For
an app that only redraws on a tap that is fine; for one that animates, the
only way to bound the flush is to write pixels into the buffer yourself and
`lv_obj_invalidate_area()`, as Arkanos and Laberinto do.

**A JPEG in an `lv_image` is re-decoded EVERY frame.** LVGL's decoder streams
it as `LV_COLOR_FORMAT_RAW`, rewinding the file each time: **2950 ms per
frame** for a 368x448 photo from the card, and no cache size helps because a
bitmap never exists. Decode once into an `lv_canvas` and show that 1:1
(`aos_app_photos.c`).

### Audio

**The speaker and the microphone cannot run at once.** Same ES8311 on the same
I2S channels: while a capture is open - the recorder, or `aos_hal_mic_open()`
- `aos_hal_beep()` and `aos_hal_play_file()` do not sound, silently. Treat
them as exclusive modes and allow ~200 ms to switch.

**`aos_hal_mic_open()` returns before there is audio**, and the first opener
sets the sample rate. `aos_hal_mic_read()` returns 0 until
`aos_hal_mic_status().open` is true; that is start-up, not "no microphone".
If the recorder is already running at 16 kHz and you ask for 32, you get 16:
read `sample_rate` from the status and compute with that, or a pitch
detector gives a clean, convincing, wrong answer.

### Numbers, time and the HAL

**No `double`, no 64-bit division, in hot code.** The FPU is single precision;
a `double` is software emulation and dividing a `uint64_t` drags in
`__udivdi3`. Both are exported, neither is free. `aos_hal_uptime_ms()` is 64
bits: subtract first, then narrow to 32. Most games run on integers with
positions in 1/16 of a pixel and angles in 256ths of a turn.

**Not every HAL getter is cheap.** `aos_hal_time_now()` is `time()` plus
`localtime_r()`, nothing; `aos_hal_time_is_valid()` **goes to NVS** on every
call. Five times a second from `tick` is free in the simulator and not on the
board. Cache it and re-ask only while it is still `false`.

**Dates by integers, not `mktime()`.** A 32-bit `time_t` ends in 2038. Leap
years by the rule of 400, month length by table, Sakamoto for the weekday: ten
lines, in `aos_app_calendar.c`, checked against 4,680 dates.

**Another time zone is TZ by hand, not `aos_hal_timezone_set()`**, which also
saves the zone to NVS and would change the watch's own time. Copy `getenv("TZ")`
to a buffer of your own (`setenv` may move the pointer), set the city's zone,
`tzset()`, `localtime_r()`, restore - in a batch, once a minute, because every
`tzset()` re-parses the DST rule. There is no zone database on the board,
only newlib's POSIX parser: rules are written out (`"CET-1CEST,M3.5.0,M10.5.0/3"`),
not IANA names (`aos_app_worldclock.c`).

### The platform

**No memory isolation.** The ESP32-S3 has no per-process protection: write
out of bounds and the whole firmware goes down. An app is closer to a kernel
module than to a phone app. That is what AddressSanitizer in the simulator
is for.

**The ABI number.** `AOS_ABI_VERSION` is in `aos_app.h` (2 today). If the
contract changes, it goes up and the loader refuses old `.so` files with a
clear message. And a field added to `aos_app_t` goes at the **end**: in the
middle it shifts every callback after it, and an old `.so` writes one where
the firmware reads another - which does not fail, it does something odd in
silence.

**Watch the stack.** The LVGL task has 16 KB of stack and your drawing code
runs in it. An overflow is not a clean error: the stack pointer falls into
the task's own TCB and the board dies later, in the scheduler or a `printf`,
with no apparent relation to the drawing.

**Every table cap in this system discards in silence** - it did, and now most
of them log. `MAX_DYNAPPS` sat at 16 with 17 `.so` files on the card and the
seventeenth just did not appear in the launcher, after building, uploading
and flashing correctly. It is 32 now on the board and in the simulator, and
both shout when full. If your app does not show up, that log line is the
first thing to look for.

**An app that registers fine and goes black when opened is the loader, not
your app.** Exact symptom: boot registers it with the right id and name, no
error, no restart, black screen on open. That is `.rodata` relocated to the
wrong address: `init()` only copies pointers and passes, `create()` reads
through them and draws nothing. It happened once, to eight of nineteen apps,
from a rounding of the `.text` size in `esp_elf.c`; it is fixed, and the
short check if it ever returns is `objdump -h` - does `.rodata` start inside
`.text` rounded to 4.

**A coloured background shows transfer errors that black hides.** Until Truco
every app and face had a black background - on an AMOLED, the pixel off - and
a pixel that reaches the panel wrong over black looks like one that is right.
The first large green cloth revealed the QSPI bus, pushed from 40 to 80 MHz,
corrupting single pixels along the path of the moving cards, and LVGL never
touching them again. It runs at 40 MHz now. If your app has a light
background and you see specks: count the simulator's pixels before blaming
your drawing (a screenshot, then look for pixels darker than both
neighbours); if the simulator has none, it is not you.

**Internal RAM running short does not error: it shows garbage, or reboots.**
The SPI driver needs a bounce buffer in internal RAM per transaction when the
draw buffer is in PSRAM; when that allocation failed, the flush of that strip
silently did not happen and the screen kept whatever was there before - the
menu painted over an app, stripes at the top - with only the log to say so
(`spi_master: Failed to allocate priv TX buffer`). Worse, the LVGL port ignores
`esp_lcd_panel_draw_bitmap()`'s return, so a flush that never queued never
completes, LVGL spins in `wait_for_flushing()` and the watchdog reboots. The
draw buffer is in internal RAM now (`buff_dma`) and LVGL's objects are in
PSRAM, so both paths are far away; but if `heap_int` in the heartbeat ever
falls far below its usual 160-185 KB, this is what it will look like. Spend
less internal RAM: a screen with many identical elements is a canvas, not an
object per element.

## 15. Checklist

- [ ] unique `id` with a prefix of your own
- [ ] `destroy` frees everything `create` allocated, `lv_timer`s included
- [ ] callbacks registered by code, not `LV_EVENT_ALL`
- [ ] if `destroy` frees the context, `lv_obj_clean(root)` comes first
- [ ] if it drags a finger: `NO_SWIPE`, the gesture heard on the root, `aos_ui_take_gesture()` polled, `LONG_DRAG` if the drag passes 50 px
- [ ] if it keeps the long press, there is another way out
- [ ] large buffers by `malloc()`, not `lv_malloc()`
- [ ] tried in the simulator, and if it is a game, for a good while
- [ ] `./tools/build_apps.sh my_app` clean: the simulator is another compiler and forgives what the board does not
- [ ] preference keys carry your prefix
- [ ] every `aos_hal_http_get()` / `_request()` has its `release()`, also in `destroy`
- [ ] whatever refreshes more than once is refilled, not rebuilt
- [ ] development `getenv()`s check the first character
- [ ] if it brings an icon: `desc.id` before `aos_icon_set_ops()`, the log says `brought its icon`, it shows in the launcher
- [ ] time counters in the context start from the clock
- [ ] a local copy of data knows what it corresponds to
- [ ] run once under AddressSanitizer with a script that exits the app
- [ ] measured on the board, not only in the simulator: LVGL costs 20-30x there
- [ ] if it keeps the screen on or reads sensors, it is justified
- [ ] anything recomputed every frame over a list compares before writing a style, and invalidates under 32 objects a frame
- [ ] nothing clickable beyond the touch window; checked with `audit_layout.sh`, not by eye
- [ ] no `lv_font_montserrat_*`: `aos_font_*` or `aos_montserrat_*`
- [ ] every visible string in `_()`, `gen_lang.py unmarked` clean, every `N_()` paired with a `_()`, the app's name in `_sistema.lang`
- [ ] `gen_lang.py check <lang>` clean, `audit_layout.sh es en de xx` without regressions
- [ ] if it draws with a bitmap font of its own, those strings are transliterated and widths are counted in characters, not bytes
- [ ] installed with `install_apps.sh`, not only built
- [ ] if it adds a portal page: `/aos.css` and `/aos.js`, listed in `aos.js`, `TR` with `en` and `de`, outside text through `aos.esc()` or `textContent`, `r.ok` checked

## Quick reference

| File | What it is |
| --- | --- |
| `components/aos_ui/include/aos_app.h` | the app model, flags, `AOS_APP_ENTRY` |
| `components/aos_hal/include/aos_hal.h` | the whole hardware API |
| `components/aos_ui/include/aos_ui.h` | toast, navigation, gestures |
| `components/aos_ui/include/aos_theme.h` | palette, fonts by role, UI helpers |
| `components/aos_ui/include/aos_icon_ops.h` | the AIC icon format, the `AIC_*` macros, `aos_icon_set_ops()` |
| `components/aos_ui/aos_icon_tables.c` | the firmware's icons as tables; copy one from here |
| `components/aos_ui/include/aos_i18n.h` | `_()`, `N_()`, `C_()`, `NC_()` |
| `components/aos_web/` | the portal: embedded pages and endpoints; `aos.css` and `aos.js` are what they share |
| `apps/hello_app/` | the template: icon, translation, event codes, all in place |
| `apps/arkanos/main/ak_pixel.c` · `tools/ak_harness.c` | dirty rectangles and the harness that checks them |
| `apps/clima/main/wx_api.c` | the JSON reader without a tree |
| `tools/build_apps.sh` · `install_apps.sh` · `install_lang.sh` | build, verify, install |
| `tools/gen_symbols.py` | the firmware's symbol table (`EXTRA_SYMBOLS`) |
| `tools/gen_lang.py` · `audit_layout.sh` · `captura.py` | translation, layout and the real screen |
