# Writing an app

An app — built in or loaded from a `.so` — implements one contract. The runtime
hands it a 368x448 LVGL container and takes care of navigation, the status bar
and the life cycle.

The full contract is
[`components/aos_ui/include/aos_app.h`](../components/aos_ui/include/aos_app.h).
The smallest complete example is
[`apps/hello_app/`](../apps/hello_app/main/hello_app.c).

This page is the contract. [APP-GUIDE.md](APP-GUIDE.md) is the long form: the
workflow, the drawing techniques and what each costs on the board, fetching
data, being configured from the portal, testing without the watch, and every
trap that bit while the apps in `apps/` were written.

## The shape of an app

```c
#include "aos_app.h"
#include "aos_hal.h"
#include "aos_i18n.h"

typedef struct { lv_obj_t *label; int taps; } hello_ctx_t;

static void *hello_create(aos_app_t *self, lv_obj_t *root)
{
    hello_ctx_t *ctx = lv_malloc_zeroed(sizeof *ctx);
    ctx->label = lv_label_create(root);
    lv_label_set_text(ctx->label, _("Hola"));
    return ctx;                       /* the instance context, or NULL */
}

static void hello_destroy(aos_app_t *self, void *inst) { lv_free(inst); }

static bool hello_init(aos_app_t *app)
{
    app->desc.id      = "demo.hello";   /* unique */
    app->desc.name    = "Hola";         /* what shows in the menu */
    app->desc.icon    = LV_SYMBOL_OK;   /* an LVGL glyph, or a vector icon */
    app->desc.color_a = 0x0A84FF;       /* icon gradient */
    app->desc.color_b = 0x0040DD;
    app->create  = hello_create;
    app->destroy = hello_destroy;
    return true;
}

AOS_APP_ENTRY(hello_init);
```

`AOS_APP_ENTRY` is what makes the same source build two ways. In a normal build
it defines the `.so`'s two exported functions; in the simulator (which compiles
`apps/*/main/*.c` with `AOS_SIM_BUILTIN`) it makes the app register itself at
startup. **You design it on the Mac and copy it to the microSD without changing
a line.**

### More than one app in a module

A `.so` may bring several apps instead of one. Use `AOS_APP_ENTRY_MANY` and
answer two questions instead of filling one descriptor:

```c
static uint32_t my_count(void)                        { return n; }
static bool     my_describe(aos_app_t *app, uint32_t index) { ... }

AOS_APP_ENTRY_MANY(my_count, my_describe);
```

The loader asks the count once, reads one descriptor per app from a single
`dlopen`, and registers them all. The ABI does not move for this: a module
without it has exactly one app, as every one of them did before v0.3.15, and a
module **with** it still loads on a firmware that has never heard of it,
because the macro also defines the old `aos_app_init()` as index 0.

Two things to get right, and the second one bit:

- **The index is not the identity.** The loader writes down which app of the
  module a slot is and asks for that index again when it reopens the module,
  but by then the thing the apps are made of may have changed and the indices
  moved. Decide what an app *is* from `self->desc.id` in `create()`, which the
  runtime keeps. If the descriptors come from files, sort them, so the same
  card gives the same answer twice.
- **The descriptor's strings must outlive the call, one buffer per app.**
  `aos_ui_register_app()` does a struct copy and keeps the *pointers*. On the
  board the loader copies id and name into a slot of its own straight away, so
  a single shared buffer looks fine; in the simulator it does not, and every
  app ends up pointing at the last name written. The symptom is one line,
  `duplicate app: <id>`, and the apps missing from the launcher.

`apps/lua/main/lua_app.c` is the only user today: one app per `.lua` file on
the card.

There is a ceiling, and it is shared: `AOS_MAX_APPS` slots in the launcher for
the built-in apps and the dynamic ones together, and `MAX_DYNAPPS` for the
dynamic ones. A module that declares many apps can reach them, and both say so
in the log when they do — which they did not, the first time this happened.

## The icon

Three ways, in the order the launcher tries them:

1. **Bring your own.** Describe it as shapes with the macros of
   `aos_icon_ops.h` and hand it over from `init()`, after `desc.id`:

   ```c
   #include "aos_icon_ops.h"

   static const uint8_t HELLO_ICON[] = {          /* a speech bubble with a face */
       AIC_HEADER,
       AIC_RECT(AIC_CENTER, -16,  20, 14, 12,  3,         AIC_C_TEXT, 255),  /* tail   */
       AIC_RECT(AIC_CENTER,   0,  -6, 62, 46, 14,         AIC_C_TEXT, 255),  /* bubble */
       AIC_INTO,                                                             /* inside it: */
       AIC_RECT(AIC_TOP_MID, -11, 11,  8,  8, AIC_CIRCLE, AIC_C_BG,   255),  /* eyes   */
       AIC_RECT(AIC_TOP_MID,  11, 11,  8,  8, AIC_CIRCLE, AIC_C_BG,   255),
       AIC_ARC(AIC_CENTER, 0, 6, 30, 0, 4, 0, 360, 25, 155, 0,               /* smile  */
               AIC_C_BG, 0, AIC_C_BG, 255),
       AIC_OUT,
       AIC_END
   };

   app->desc.id       = "demo.hello";     /* before the icon: it is keyed by id */
   app->desc.icon     = LV_SYMBOL_OK;     /* the fallback */
   app->desc.icon_vec = AOS_ICON_NONE;
   aos_icon_set_ops(app, HELLO_ICON, sizeof HELLO_ICON);
   ```

   This is exactly what `apps/hello_app` does, so the template already
   shows the pattern. `apps/escaner` is the other kind of example: an icon
   the firmware also has (`AOS_ICON_RADAR`), carried by the `.so` as the same
   bytes, so the app does not depend on that enum value being there.

   Coordinates are percent of the icon size, so the one drawing serves the
   list, the grid and the honeycomb. The runtime validates and copies the
   bytes, so no firmware change and no reflash: the icon travels inside the
   `.so`. Format, opcodes and palette: [ICONS.md](ICONS.md).
2. `desc.icon_vec`, one of the firmware's `AOS_ICON_*` - for an icon that
   already exists there.
3. `desc.icon`, an LVGL glyph or two letters. Keep one as the fallback even
   with 1 or 2: it is what shows if the blob is ever refused.

`aos_icon_set_ops()` is a firmware symbol: a `.so` that calls it will not
load on a firmware older than the one that introduced it (`build_apps.sh`
reports the missing symbol at build time rather than at load).

## The callbacks

| Callback | When |
| --- | --- |
| `create(self, root)` | build the UI inside `root`; return the instance context |
| `destroy(self, inst)` | free the context — LVGL objects under `root` are deleted by the runtime |
| `show` / `hide` | back to the foreground / sent to the background |
| `back(self, inst)` | back gesture or button; return `true` if the app consumed it |
| `button(self, inst, action)` | the physical button; `true` if consumed |
| `tick(self, inst)` | ~5 times a second for as long as the app exists |

`button` runs with the LVGL lock held but **from the HAL's background task**:
note it down and act on the next frame.

`tick` keeps being called for a backgrounded app, which is how the timer, the
pomodoro and the sensor panel keep working with the screen off.

## A background task

Everything above runs in LVGL's task, and for every app before Video that
was enough. When the work is bigger than a frame (reading and decoding a JPEG
is 50 ms on the board) an app may have **one** background task:

```c
static void worker(void *arg)
{
    my_ctx_t *c = arg;
    while (!aos_hal_worker_should_stop()) {
        if (nothing_to_do(c)) { aos_hal_worker_sleep(2); continue; }
        produce_one(c);                 /* files, decoding, arithmetic */
    }
}
/* in create() or when playback starts */
aos_hal_worker_start("my_worker", worker, ctx, 8192);
/* in destroy(), before anything the worker touches is freed */
aos_hal_worker_stop();
```

The rules are in the comment block of `aos_hal.h`, and they are short: the
function never touches LVGL; it returns promptly once `should_stop()` says
so, because `stop()` waits for it; the handshake with the UI side is plain
flags in the app's own memory, one writer each; and `stop()` is called from
`destroy()`. It is pinned to the second core and does not take the UI's
turn. [VIDEO.md](VIDEO.md) has the ring of frames the Video app builds on it
and the numbers that made it necessary.

## Flags

| Flag | Effect |
| --- | --- |
| `KEEP_AWAKE` | the screen stays on while the app is in the foreground |
| `FULLSCREEN` | no status bar |
| `BACKGROUND` | the app stays alive after exit and keeps getting ticks |
| `NO_SWIPE` | the app handles the back gesture itself |
| `LONG_DRAG` | do not abort an in-flight touch at the 50 px gesture threshold |

`LONG_DRAG` exists for a specific reason. A drag longer than 50 px fires LVGL's
global gesture detection, which calls `lv_indev_wait_release()` so that
swipe-to-go-back does not also click whatever ended up under the finger. That
aborts *any* touch in flight, `NO_SWIPE` or not — so an app dragging objects
further than 50 px (rubbing Claudito with a sponge, moving a car across a board)
loses its release event halfway.

`BACKGROUND` has a cost worth knowing: the runtime does not destroy a background
app on exit, and without a destroy its `.so` is never unloaded. The Recorder
carries the flag **only while it is actually recording** for exactly that
reason — it was holding 38 KB of executable RAM permanently, after which the
large apps no longer fitted.

## Things that will bite you

These are all measured, and each is written up next to the code that deals with
it.

**Never call `aos_ui_back()` from an event callback.** It destroys the app,
which means destroying the object that is dispatching the event. Note the
request and act on the next tick. And if your app also implements `back()`,
remember `aos_ui_back()` consults it first — an app that always returns `true`
can never be closed, and the loop gives no error at all.

**Delete your own objects in `destroy()`.** The runtime calls `destroy()` and
*then* deletes the root, so any event LVGL emits during that deletion —
`LV_EVENT_DELETE`, and `LV_EVENT_PRESS_LOST` if a finger was down — reaches a
callback whose context has already been freed. Delete them yourself while the
context is still alive; the empty root the runtime inherits it deletes anyway.

**Register the event codes you use, not `LV_EVENT_ALL`.** With `ALL` you also
receive the deletion's events. AddressSanitizer found this in the simulator; on
the board it would have been an unexplained restart.

**In LVGL 9 every `lv_obj` is born clickable.** A decorative rectangle will eat
the touch meant for the container underneath it. `aos_unclickable()` strips the
flag from an object and all its children.

**A touch can land anywhere between y = 24 and y = 410** (x 16..352); between
y = 56 and 390 precision is the calibration's own, outside it a finger against
the rim lands exactly on row 24 or 410, so a control that must be reachable
from the very edge contains that row. The layout audit checks it. See
[HARDWARE.md](HARDWARE.md).
In the simulator the mouse reaches everywhere, so this is invisible there;
`tools/audit_layout.sh` checks it.

**Large buffers go through `malloc()`, not `lv_malloc()`.** With
`CONFIG_SPIRAM_USE_MALLOC` a plain `malloc()` lands in PSRAM, of which there
are 8 MB. Since the RAM audit LVGL's own allocations land there too (the
firmware wraps `lv_malloc_core`), so hundreds of LVGL objects no longer eat
internal RAM; `malloc()` stays the API for buffers because it behaves the same
in the simulator and does not depend on that wrap.

**Your code runs from PSRAM.** The loader puts the `.so`'s `.text` in PSRAM and
maps it onto the instruction bus through the MMU, so there is no size an app
has to fit in any more (until v0.3.3 every loaded app shared a 48 KB
reservation of internal RAM, and Ajustes → SISTEMA showed how much was left).
`.rodata` and `.data` go to PSRAM as before. Keep the `-Os` that
`elf_loader.cmake` sets: a smaller `.text` misses the 16 KB instruction cache
less and loads faster.

**No `double`, no 64-bit division, if you can avoid it.** The ESP32-S3's FPU is
single precision, so a `double` means calls into the software emulation — and
dividing a `uint64_t` drags in `__udivdi3`. Both are exported to apps, but in a
hot loop they are not free. Most of the games run entirely on integers, with
positions in 1/16 of a pixel.

## The USB port

Since v0.3.6 an app can use the USB-C port through the HAL, dynamic apps
included: `aos_hal_usb_mode()` / `aos_hal_usb_mode_set()` (asynchronous),
`aos_hal_usb_keys_ready()`, `aos_hal_usb_key("volup")`, `aos_hal_usb_type()`,
`aos_hal_usb_mouse()`, `aos_hal_usb_click()`, `aos_hal_usb_gamepad()`,
`aos_hal_usb_midi_note/cc/bend()`, `aos_hal_usb_card_away()`. The signatures
are in `aos_hal.h`; the patterns that work (a 500 ms poll for readiness, a
20-40 ms timer for the streams, press and release on the buttons, what to
release in `destroy`) and the hardware's limits are in
[HANDOFF-USB.md](HANDOFF-USB.md) section 4, and `aos_app_pcremote.c` is the
worked example. The simulator switches instantly and its keyboard is always
ready in keys mode, so the screens can be drawn on the Mac.

## Translation

Strings go through `_()`, and the catalogue key **is the Spanish string
itself**, gettext style. `N_()` marks a string in a static table for the
extractor; the translation happens where it is drawn.

```c
lv_label_set_text(label, _("Hola"));                 /* translated here */
static const char *names[] = { N_("Rojo"), N_("Verde") };  /* marked here */
lv_label_set_text(label, _(names[i]));               /* translated there */
```

Use `C_("context", "text")` when the same word needs two translations — "MAR" is
Tuesday *and* March, and in English they are TUE and MAR.

If your app draws text onto a **canvas** with a bitmap font of its own, that
font is ASCII and upper case only: `gen_lang.py` flags any non-ASCII in those
catalogues. The best way out is Claude Jump's — put only numbers on the canvas
and let every word be an LVGL label, which has accents and umlauts.

See [I18N.md](I18N.md).

## Building and installing

```bash
./tools/build_apps.sh myapp                    # builds and verifies
./tools/install_apps.sh 192.168.1.116 myapp    # uploads over WiFi and restarts
```

The `.so` comes out at a few KB: it exports two symbols and leaves everything it
calls unresolved. See [BUILDING.md](BUILDING.md) for what the script checks and
why.

## Development switches

On the board `getenv()` always returns NULL, so environment variables are free
and harmless. Every app of any size uses them to reach a state that would
otherwise take twenty minutes of play:

```bash
GEMAS_TEST=5 ./build/amoledos_sim        # start with a line of five served up
CJ_SHOP=1 CJ_COINS=500 ./build/amoledos_sim
CH_SALA=5 CH_NIVEL=20 ./build/amoledos_sim
ARK_AUTO=1 ./build/amoledos_sim          # the paddle plays itself
```

Each app's are listed at the bottom of its main file.
