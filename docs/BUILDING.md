# Building

Two things build from this repository: the **firmware** for the board, and a
**desktop simulator** that runs the same UI code with SDL2. Neither needs the
other.

## Requirements

| For | Install |
| --- | --- |
| Firmware | ESP-IDF **v5.5** (`~/esp/esp-idf`) |
| Simulator | `brew install sdl2 cmake` |
| Fonts | `brew install node && npm install -g lv_font_conv` (only to regenerate) |

## Firmware

```bash
source ~/esp/esp-idf/export.sh
idf.py set-target esp32s3
idf.py build
```

**The symbol table needs two passes.** Dynamic apps resolve LVGL, the HAL and
libc against a table generated from the build's own static libraries, so the
libraries have to exist before the table can be written:

```bash
idf.py build                    # first pass, so the .a files exist
python3 tools/gen_symbols.py    # writes components/aos_dynapp/aos_symbols.c
idf.py build                    # second pass, now with the table
```

Flash and monitor:

```bash
idf.py -p /dev/cu.usbmodem* flash monitor
```

The firmware is ~3.0 MB in a 5 MB partition.

## Simulator

Reproduces the 368x448 display with the mouse acting as a finger.

```bash
cd sim
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j8
./build/amoledos_sim
```

It compiles the real `aos_ui`, `aos_apps` and all 21 dynamic apps (with
`AOS_SIM_BUILTIN`, so `AOS_APP_ENTRY` self-registers them instead of exporting
the `.so` symbol).

### Keyboard

**Case matters**: `w` opens the watchface picker, `W` toggles WiFi.

| Key | Effect |
| --- | --- |
| `ESC` / backspace / left arrow | back |
| `h` / Home | go to the clock |
| `m` / up arrow | open the menu |
| `l` / `g` / `p` | menu as list / grid / honeycomb |
| `1`..`9` | open app N from the menu |
| space | the board's BOOT button (press and release) |
| `i` | simulated posture (flat / on edge / face down / in hand) |
| `w` | watchface picker |
| `t` | rotate through the installed languages |
| `a` | enter and leave dimmed mode |
| `n` | a notification arrives from the simulated phone |
| `N` | the flood of those already pending on connecting |
| `r` | five messages in a row from the same person (a burst) |
| `b` / `B` | toggle Bluetooth / start pairing |
| `W` | toggle WiFi |

### Jumping straight to a screen

```bash
AOS_SIM_VIEW=launcher  ./build/amoledos_sim     # also grid or honeycomb
AOS_SIM_VIEW=aos.timer ./build/amoledos_sim     # any app id
```

Some screens hang off `lv_layer_top` and cannot be reached reproducibly with a
key script, so they have switches of their own:

| Variable | Opens |
| --- | --- |
| `AOS_SIM_AP=1` / `=2` | the setup access point up / its password-and-QR screen |
| `AOS_SIM_BT=1` / `=2` / `=3` | Settings' pairing screen / category filter / the pairing overlay |

### Scripted navigation

For reproducing a sequence hands-free — useful for verifying a change and for
taking screenshots always in the same state:

```bash
AOS_SIM_KEYS="swipe:up,2,swipe:right,esc" ./build/amoledos_sim
```

| Token | What it does |
| --- | --- |
| `esc`, `home`, `up`, `left`, `back` | a control key |
| a single character (`2`, `m`, `p`) | a printable key |
| `swipe:up\|down\|left\|right` | a finger swipe |
| `tap` or `tap:<x>x<y>` | a touch (e.g. `tap:328x392`) |
| `hold:<x>x<y>:<ms>` | press and hold |
| `tilt:<x>,<y>` | tilt the board (both between -0.5 and 0.5) |
| `ms:<n>` | spacing before the following steps |

Swipes and taps go in through a virtual pointer input device, that is, by the
same path as a real finger on the board.

### Layout checking

```bash
AOS_SIM_LAYOUT=1 ./build/amoledos_sim     # every text's transformed box
AOS_SIM_AUDIT=<label> ./build/amoledos_sim  # only the problems, one per line
./tools/audit_layout.sh es en de          # every app, every language
```

The audit reports four things: text clipped inside its own box, text off the
368x448 screen, text overflowing its container, and **clickable objects below
the touch ceiling** — the last one being invisible in the simulator, where the
mouse reaches everywhere.

A control that sits low but still leaves a usable strip is listed separately,
as `LOWEDGE`, and does **not** count as a problem. Several of them are in
everyday use on the board: with the touch calibrated, 40% of a button is plenty
of target. The list is there so that if something does feel unresponsive you
know where to look — and the first suspect is the calibration, not the layout.

> **`audit_layout.sh` is a sweep, not a spot check.** It opens 47 screens per
> language, one process each, and takes minutes; it is for adding a language,
> touching the theme or a shared widget, and for release checks. To look at one
> screen, open that screen — same check, same output, one second:
>
> ```bash
> cd sim
> AOS_SIM_AUDIT=es/aos.settings AOS_SIM_VIEW=aos.settings ./build/amoledos_sim
> ```
>
> `AOS_SIM_KEYS` gets you to a screen that needs navigating to, and
> `AOS_SIM_AUDIT_MS` waits longer if it takes a while to assemble.

`AOS_SIM_POS=x,y` pins the window at a known place; the simulator prints the
exact `screencapture -R` command to crop it.

## Dynamic apps

```bash
cd apps/flappy
idf.py set-target esp32s3        # first time only
idf.py so                        # produces build/flappy.so
cp build/flappy.so /Volumes/<sd>/apps/
```

In practice, use the script — it does three things that are easy to forget:

```bash
./tools/build_apps.sh            # all of them
./tools/build_apps.sh gemas      # just one
```

1. It deletes `build/so_objs` first. `project_so()` declares its objects with
   `DEPENDS` on the `.c` alone, so a change in `aos_app.h` does *not* trigger a
   rebuild: `idf.py so` relinks a stale object and announces "Build Shared
   Object" as if nothing were wrong.
2. It checks the ABI number baked into the `.so` against the firmware's.
3. It checks that every undefined symbol is in the firmware's table — otherwise
   the app fails only when it is loaded on the board.

It also derives each app's LVGL configuration from the firmware's. That is not
tidiness: `lv_global_t` has fields under `#if` guards, so a mismatched config
makes every inline LVGL function the app compiles write into the wrong field —
silent corruption that neither heap poisoning nor a stack watchpoint detects.

Uploading over WiFi, without taking the card out:

```bash
./tools/install_apps.sh 192.168.1.116          # all of them
./tools/install_apps.sh 192.168.1.116 gemas    # just one
```

The firmware loads the `.so` files once at startup, so the script restarts the
board when it finishes.

## Updating the firmware over WiFi

Since v0.1.2 the firmware goes over the network too, so the cable is only
needed the first time:

```bash
idf.py build
./tools/install_fw.sh amoledos.local
```

It POSTs `build/amoledos.bin` — the app alone, **not** the merged image — to
`/api/ota`, which writes it into whichever of the two 5 MB slots is idle and
points the bootloader at it. The same file can be dropped on the portal's front
page, under *Firmware*.

**It does not touch NVS.** The wifi, the language, the watchface and the apps'
data survive, which is the whole difference against flashing `amoledos-full.bin`
over USB.

> **The image boots on trial.** With `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE` an
> image that has just been installed has to confirm itself: `main.c` does that
> at 30 seconds of uptime, and if it never gets there —a panic in
> `aos_hal_init()`, a watchdog while the `.so` files load, a screen that never
> draws— the next restart goes back to the previous image by itself. That is
> why a bad build over the air does not need the cable to recover.
>
> The confirmation deliberately does **not** wait for the network: an image is
> not broken because the router is down.

The one thing it will not do is change the partition table or the bootloader.
Those are still USB, and so is the first install on a fresh board.

## Other tools

| Tool | What it does |
| --- | --- |
| `tools/install_fw.sh` | pushes the firmware over WiFi (`/api/ota`), keeping NVS |
| `tools/gen_symbols.py` | generates the symbol table the apps resolve against |
| `tools/gen_fonts.py` | regenerates Montserrat with the Latin-1 supplement |
| `tools/gen_lang.py` | extracts, checks and embeds the translation catalogues |
| `tools/install_lang.sh <ip> <code>` | uploads a language pack over WiFi |
| `tools/captura.py <ip> [out.png]` | takes a screenshot of the board over HTTP |
| `tools/ppm2png.py` | converts the games' PPM dumps to PNG |
| `tools/portal_dev_server.py` | serves the web portal against a local folder |

`tools/captura.py` wakes the screen first by default. Without that you nearly
always photograph the always-on face, because the watch dims after a minute;
`--sin-despertar` takes it exactly as it is.
