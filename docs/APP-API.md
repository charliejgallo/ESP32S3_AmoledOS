# Writing an app

An app — built in or loaded from a `.so` — implements one contract. The runtime
hands it a 368x448 LVGL container and takes care of navigation, the status bar
and the life cycle.

The full contract is
[`components/aos_ui/include/aos_app.h`](../components/aos_ui/include/aos_app.h).
The smallest complete example is
[`apps/hello_app/`](../apps/hello_app/main/hello_app.c).

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

**Large buffers go through `malloc()`, not `lv_malloc()`.** LVGL's pool on the
board is 64 KB of internal RAM and it needs it for objects and draw buffers.
With `CONFIG_SPIRAM_USE_MALLOC` a plain `malloc()` lands in PSRAM, of which
there are 8 MB.

**No `double`, no 64-bit division, if you can avoid it.** The ESP32-S3's FPU is
single precision, so a `double` means calls into the software emulation — and
dividing a `uint64_t` drags in `__udivdi3`. Both are exported to apps, but in a
hot loop they are not free. Most of the games run entirely on integers, with
positions in 1/16 of a pixel.

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
