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

Four functions, all optional. Anything else in the file is yours.

```lua
function init()             -- once, before the first frame
function tick(dt)           -- every frame; dt in milliseconds
function draw()             -- every frame, after tick
function touch(x, y, ev)    -- ev is "down", "move" or "up"
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
| `aos.ms()` | milliseconds since the script started |
| `aos.beep(hz, ms)` | |
| `aos.stats()` | `script_ms, screen_ms, frame_ms` of the last frame |

Colours are `0xRRGGBB`, which is readable in a script; the buffer's RGB565 is
not the script's problem.

`aos.text` writes with the 5x7 font the games use: ASCII, upper case, digits
and a few signs. **Nothing a person reads in their own language goes through
it** — that is a label, and labels are the app's business, not a script's.

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

**The screen costs about 34 ms whatever the script does**, and 26 of those are
the panel: the frame is pushed with `aos_hal_display_blit()`, the same way the
Video app pushes its frames, because a full-screen canvas through LVGL costs
about 95 ms. Going through LVGL instead measured 83 ms a frame — 12 fps
against 25.

So **the interpreter is not the ceiling, the panel is**. At 20 fps a script
has some 30 ms a frame before it becomes the slower half, and 30 ms is around
57,000 VM instructions. Projecting 192 vertices and drawing 288 lines uses
19 of them.

One consequence to know about: what the blit pushes is invisible to
`/api/captura` and to the simulator, exactly as in the Video app. The line at
the top right of a running script is an LVGL label the app draws after the
blit, which is why it does show up in a capture.

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
