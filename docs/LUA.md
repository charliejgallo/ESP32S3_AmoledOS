# Lua on AmoledOS

An app is normally a `.so`: ESP-IDF's toolchain, two compilation passes for
the symbol table, and a stray pointer reboots the watch. A Lua script is a
text file on the card that runs when you open it, and a mistake in it is a
message with a line number.

    /sdcard/lua/cubo.lua   ->   the Lua app lists it, you tap it, it runs

The interpreter is Lua 5.4.8, built into `lua.so` (144 KB). Its `.text` runs
from PSRAM through the MMU and its heap is in PSRAM too, so a running script
costs the internal RAM of a `malloc` or two. What it costs is in **Numbers**
below, measured on the board.

## What a script is

Five functions, all optional. Anything else in the file is yours.

```lua
function init()             -- once, before the first frame
function tick(dt)           -- every frame; dt in milliseconds
function draw()             -- every frame, after tick
function touch(x, y, ev)    -- ev is "down", "move" or "up"
function gesture(ev, x, y, a, b, c)   -- since v0.6.0, see "Gestures"
```

Values that live between frames go in the chunk, as locals:

```lua
local n = 0
function draw()
    n = n + 1
    aos.clear(0x001018)
    aos.text(10, 10, "HOLA", 0xFFFFFF, 2)
end
```

A script draws into **184 x 224**, which the app scales x2 onto the watch's
368 x 448. Coordinates are always the script's, the finger's included: a
script never learns the screen is twice its buffer.

## A script is an app

A `.lua` on the card is also an entry in the launcher of its own, beside the
apps written in C. The module tells the loader how many apps it brings and
describes each one, so the interpreter is paid for once and every script after
that is data — the same bargain CHATARRA made with its `.rodata`.

Two things a script can say about itself:

```lua
-- @name Cubo
```

anywhere in its first few lines, which is the name the launcher shows. Without
it the file name is used, minus the extension, which in a launcher full of
Burbujas and Pixel Art reads like a mistake.

And an **icon**: a `.aic` file next to the script with the same name
(`cubo.lua` → `cubo.aic`) becomes its launcher icon, with no firmware and no
reflashing. You write it as a handful of shapes in a text file and assemble it
with the tool — no toolchain involved:

```bash
python3 tools/aic.py asm cubo.aic.txt        # -> cubo.aic
```

`apps/lua/scripts/cubo.aic.txt` and `hola.aic.txt` are the two in the
screenshot — four and three shapes, written by hand — and [ICONS.md](ICONS.md)
has the format. `/sdcard/icons/lua.cubo.aic` works
too and wins, because that is the firmware's own override. Without either, the
script gets a play glyph and a colour picked from its name, so that fifteen
scripts are not fifteen identical tiles.

The entries are read **at boot**, like the `.so` apps: a script saved now is
in the list inside the Lua app immediately, and in the launcher after a
restart. The first sixteen scripts get an entry; the rest still run, from the
list.

One curiosity worth knowing: the launcher translates app names through the
same catalogue the system uses, so a `@name` that happens to match one of its
strings gets translated. A script called `Hola` shows as `Hello` on an English
watch, because `Hola` is `hello_app`'s name.

## The `aos` table

This is all of it. There is no `io`, no `os`, no `package` and no `debug` —
their sources are not compiled into the binary, so it is not a switch anyone
can flip by accident. `string`, `table`, `math`, `utf8` and `coroutine` are
all there.

| | |
|---|---|
| `aos.W`, `aos.H` | 184 and 224 |
| `aos.clear(c)` | fills everything |
| `aos.pixel(x, y, c)` | |
| `aos.rect(x, y, w, h, c)` | filled |
| `aos.frame(x, y, w, h, c)` | outline |
| `aos.line(x0, y0, x1, y1, c)` | |
| `aos.disc(cx, cy, r, c)` / `aos.ring(...)` | filled / outline |
| `aos.text(x, y, s, c [, scale])` | 5x7, upper case, digits and signs |
| `aos.shade(x, y, w, h, f)` | darkens (f<0) or lightens (f>0), in sixteenths |
| `aos.touch()` | `x, y, down` — where the finger is, now |
| `aos.fingers()` | `n, id1, x1, y1, id2, x2, y2` — every finger down, each with an id that stays while it does (v0.6.0) |
| `aos.ms()` | milliseconds since the script started |
| `aos.beep(hz, ms)` | |
| `aos.background()` | freezes what is drawn as the background; see below |
| `aos.stats()` | `script_ms, screen_ms, frame_ms, rows` of the last frame |

Colours are `0xRRGGBB`, which is readable in a script; the buffer's RGB565 is
not the script's problem.

`aos.text` writes with the 5x7 font the games use: ASCII, upper case, digits
and a few signs. **Nothing a person reads in their own language goes through
it** — that is a label, and labels are the app's business, not a script's.

## Gestures

Since v0.6.0 the watch reads two fingers (docs/GESTURES.md), and a script
that defines `gesture()` gets what they mean, already filtered for what the
touch chip gets wrong:

| `ev` | `x, y` | `a, b, c` |
|---|---|---|
| `"tap"`, `"double"`, `"long"` | where | |
| `"drag"` | the finger | `dx, dy` since the last one |
| `"release"` | where it lifted | `vx, vy`, speed per second: fling or not |
| `"pinchstart"` | the point between the fingers | |
| `"pinch"` | the point between the fingers | `scale` (multiply your zoom by it), `dx, dy` of that point |
| `"pinchend"` | | |

Everything is in the script's pixels, like `touch()`. A pinch never turns
into a drag until every finger is up. A script with `gesture()` also turns
the back swipe off (a pinch is a long drag); the side button still leaves.

`aos.fingers()` is the other way: where each finger is right now, for a
script that wants two thumbs on two controls rather than a zoom. The ids
say which is which. `gestos.lua` in `apps/lua/scripts/` does both: a star
field you pinch and drag, and a ring under each finger.

## Rules the app enforces, and why

- **Every call into Lua goes through `lua_pcall`.** An error stops the script
  and puts the message on screen, with its line. The watch carries on.
- **A script that does not come back is cut** at 400 ms per call, by a count
  hook. This matters more than it sounds: the script runs in the LVGL task, so
  a `while true do end` would freeze the interface until the watchdog panicked
  — worse than a crash, because the watch hangs with the last frame on screen.
- **Nesting is bounded** by `LUAI_MAXCCALLS=40`, set in the app's CMakeLists.
  The parser recurses in C and the LVGL task has 16 KB of stack: measured,
  66 levels of nested parentheses survive and 68 panicked the board. With the
  guard, a thousand of them give `C stack overflow` as an ordinary Lua error.
  It does not limit a script's own recursion — that counter counts C levels,
  and a Lua function calling itself a thousand times still works.
- **Arguments are checked.** Every number goes through `luaL_checkinteger`,
  which raises a Lua error rather than believing it, and coordinates are
  clamped before they reach the drawing engine.

## Numbers

Measured on the board (ESP32-S3 at 240 MHz, v2), with `tools/captura.py` and
the machine quiet.

**The interpreter itself**: 200,000 turns of an arithmetic loop in 105 ms,
about **1.9 M turns a second**, with the code running from PSRAM. A bare state
with the six libraries is 15 KB of Lua heap; because `lua_Alloc` points at
`MALLOC_CAP_SPIRAM`, a state grown to 77 KB costs **52 bytes** of internal
RAM. With the default allocator that same state took the free executable RAM
from 107 K down to 56.8 K, so this is not a detail.

**A frame**, with `scripts/cubo.lua`: a wireframe cube whose two rotations,
perspective divide and twelve lines are all done in Lua, per vertex, per
frame. Tap to add another cube.

| cubes | vertices/frame | script | upscale | push | frame | fps |
|---|---|---|---|---|---|---|
| 1 | 8 | 3 ms | 8 ms | 25 ms | 40 ms | 25 |
| 8 | 64 | 8 ms | 9 ms | 27 ms | 46 ms | 21 |
| 16 | 128 | 13 ms | 8 ms | 26 ms | 50 ms | 20 |
| 24 | 192 | 19 ms | 8 ms | 26 ms | 55 ms | 18 |

**For a script that clears, the screen costs about 34 ms whatever else it
does**, and 26 of those are the panel: the frame is pushed with
`aos_hal_display_blit()`, the same way the Video app pushes its frames,
because a full-screen canvas through LVGL costs about 95 ms. Going through
LVGL instead measured 83 ms a frame — 12 fps against 25.

### Not clearing is the other half

The frame buffer survives between frames, and the app pushes **only the rows
that changed**. It works out which ones by itself: every primitive marks the
box it touched, the boxes are merged into bands of rows, and only those go to
the panel. A script does not call anything and does not know it is happening.

`aos.clear()` marks everything, so a script that clears every frame pays what
it always did — that is `cubo.lua`, and nothing about it changed. A script
that instead erases its own old positions pays for those:

| | rows | script | upscale | push | frame | fps |
|---|---|---|---|---|---|---|
| `cubo.lua`, clears every frame | 224/224 | 3 ms | 8 ms | 27 ms | 40 ms | 25 |
| `pelota.lua`, four balls over a background | 76/224 | 0 ms | 2 ms | 9 ms | **21 ms** | **47** |

Both measured on the board. The 20 ms is the frame timer's own period, so the
second one is really 12 ms of work and could go faster if the timer let it.

The bands are **full width**. `aos_hal_display_blit()` takes a packed buffer,
and a sub-rectangle of a 368-wide frame is not packed — its rows sit 368
pixels apart — so a band goes out straight from the frame buffer with no
staging copy and no extra memory. What that costs is width: a ball in the
middle pushes its whole rows, 368 pixels wide instead of 40. What it saves is
every row nothing touched, and since the cost is bytes over SPI, that is most
of it.

### `aos.background()`, so you never erase

The script does not have to un-draw anything. Paint the world once, freeze it,
and from then on the app puts back whatever the script drew in the frame
before — copying those rectangles out of the frozen copy at the start of the
next one:

```lua
function init()
    aos.clear(0x05050C)
    for y = 22, aos.H - 1, 16 do aos.rect(0, y, aos.W, 1, 0x121A2A) end
    for x = 0, aos.W - 1, 16 do aos.rect(x, 22, 1, aos.H - 22, 0x121A2A) end
    aos.background()                       -- this is the world
end

function draw()
    aos.disc(x, y, r, 0x00E5FF)            -- and this is transient
end
```

That is `pelota.lua`, and the grid is the point: erasing by painting the
background colour over the old position — which is the other way of doing it,
and what that script did before — would leave rectangular holes in it. A drawn
background can only be restored, not repainted.

The contract it brings: **after the call, anything drawn lasts one frame**.
Something meant to stay goes on the buffer before the call, or the call is
made again to freeze it in — which is also how a script changes its world
between levels.

It costs 82 KB of PSRAM, allocated the first time it is asked for, so a script
that never calls it pays nothing; internal RAM does not move. It returns
`false` if there is no memory, and a script can carry on without one by
erasing for itself.

So **the interpreter is not the ceiling, the panel is**. At 20 fps a script
has some 30 ms a frame before it becomes the slower half, and 30 ms is around
57,000 VM instructions. Projecting 192 vertices and drawing 288 lines uses
19 of them.

One consequence to know about: what the blit pushes is invisible to
`/api/captura` and to the simulator, exactly as in the Video app. The line at
the top right of a running script is an LVGL label the app draws after the
blit, which is why it does show up in a capture.

## The `/lua` page

The portal has an editor, which is what makes the loop short. It lists the
scripts on the card, opens one, saves it back, and below the text it shows a
console with what the watch is doing:

    on the watch: cubo.lua
    running, no errors

or, when something broke,

    on the watch: hola.lua
    hola.lua:8: attempt to index a nil value (global 'LETRERO')

**Saving is the whole step.** While a script is running, the app watches the
file it came from and reloads it when it changes — so you save in the browser
and look at the watch. No backing out, no tapping again. It is checked once a
second; FAT keeps the time to the nearest two seconds, so the size counts too,
and a save that changes neither the length nor the second is missed. Saving
again picks it up.

The console is a file, not an endpoint: the app writes `_estado.txt` into the
same folder, with the running script on the first line and its error on the
rest, and the page reads it with the same `/api/download` that serves the
scripts. The firmware never learns what a Lua error looks like — the same
arrangement as the `.pato` scripts and the `.pix` drawings.

`Ctrl`/`Cmd`+`S` saves, because that is the key one presses without thinking
when the file lives on another machine.

## Writing one

The simulator runs the same interpreter and the same scripts, and there the
canvas goes through LVGL, so everything is visible on the Mac:

```bash
cp mi_script.lua sim/sim_fs/lua/
cd sim && cmake --build build -j8
LUA_RUN=mi_script.lua AOS_SIM_VIEW=aos.lua ./build/amoledos_sim
```

`LUA_RUN` opens straight into a script instead of the list. On the board
`getenv` always returns NULL, so the switch costs nothing and travels in the
same binary.

To put one on the watch, no rebuild is needed — the script is data:

```bash
curl -X POST "http://<ip>/api/upload?dir=sd/lua&name=mi_script.lua" \
     --data-binary @mi_script.lua
```

and open the app again.
