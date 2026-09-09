# 2043 — The battle of Ceres

A vertical shoot-'em-up for AmoledOS, a tribute to Capcom's **1943**: an energy
bar that drains by itself, capsules dropped by a formation when it falls as a
whole, and a different boss at the end of each planet.

```bash
# in the simulator, without the board
cd sim && cmake --build build -j8
AOS_SIM_VIEW=demo.2043 ./build/amoledos_sim

# the .so for the microSD
source ~/esp/esp-idf/export.sh
cd apps/g2043
idf.py -G 'Unix Makefiles' set-target esp32s3     # first time only
idf.py so                                          # build/g2043.so
cp build/g2043.so /Volumes/<sd>/apps/
```

## The two controls

On opening, the game asks how you want to play. In both cases **the trigger is
the side button** (the board's BOOT):

| | Move | Fire |
| --- | --- | --- |
| **TOUCH** | drag your finger; the ship flies 13 px above it so as not to cover it | side button |
| **SENSOR** | tilt the board; a touch on the screen recalibrates the zero | side button |

The tilt maps to an **absolute position**, not to a velocity: with the board
level the ship returns to its place by itself (bottom centre), and about 260
milli-g are enough to reach the edge. With a velocity, a zero that is only
slightly off leaves the ship drifting into the wall until you correct with the
thumb; this way that cannot happen.

In sensor mode the axes depend on how the board ends up mounted, so the menu has
three switches — `INV X`, `INV Y` and `EJES XY` — that are stored in the
preferences. If the ship goes the wrong way when you try it on the board, you
fix it there without recompiling.

The `AUTO` chip leaves the firing automatic, for playing without the button.

**Double tap of the button = barrel roll.** Two quick presses and the ship spins
on its axis: invulnerable for 22 frames, with 70 of cooldown. It is the
replacement for 1943's "loop".

While flying, the side button is only the trigger: it does not go back and does
not go to the watch, because holding it down for a second in the middle of a
firefight would drop you out of the game. To leave there is the swipe-right
gesture (outside of flight) or the pause.

## How it is put together

| File | What it does |
| --- | --- |
| `gx_pixel.[ch]` | RGB565 buffer, primitives, ASCII sprites, 5x7 font, integer trigonometry |
| `gx_art.[ch]` | the ship and the seven enemies, as ASCII art |
| `gx_world.[ch]` | the planets: gradient, scenery and the wave table |
| `gx_foe.[ch]` | enemy behaviour and the three bosses |
| `g2043.[ch]` | the app: input, HUD, collisions, state machine |

**The grid is 184x224**, exactly half the screen. A single `lv_canvas` that LVGL
stretches x2 with nearest neighbour (`lv_image_set_inner_align(STRETCH)` +
`antialias(false)`). The buffer is 82 KB and comes from `malloc()`, not
`lv_malloc()`: on the board LVGL's pool is 64 KB and this has to land in PSRAM.

Everything is redrawn in full every frame, at 33 ms. On the desktop the game
takes 0.3 ms per frame and the rest is LVGL; **how much the stretch costs on the
board is unmeasured**, and it is the first thing to look at once the hardware is
there. For that, the pause sign has an `FPS` switch that shows the real frames
per second at the top left. If it does not keep up, the way out is dropping the
grid to 92x112 (x4 stretch, a quarter of the pixels of its own) or raising
`G_FRAME_MS`.

Positions go in **fixed point of 1/16 of a pixel** (`FX()` / `UNFX()`) and
angles in **brads** (256 per turn, `gx_sin`/`gx_cos`/`gx_atan2`). libm is not
used: every floating-point symbol would have to be exported from the firmware.

## How a planet is added

All the content lives in tables. A new planet is two things:

1. **A wave table** in `gx_world.c`. Each row is `{ frame, type, how many,
   formation, column%, gift }`. If the formation falls as a whole before
   escaping, it drops the capsule in the `gift` field (`1 + PU_*`); if a single
   ship escapes, there is no prize. Just like in 1943.

2. **A row in `gx_levels[]`**: name, sky gradient, scenery colours, background
   style (`BG_DUST` / `BG_OCEAN` / `BG_BELT`), duration in frames until the
   boss, scroll speed and which boss closes it.

Nothing else. `gx_level_count` comes from the `sizeof` and the state machine
chains the planets by itself.

A new **background style** is two `case`s: one in `scenery_spawn()` and another
in `gx_bg_draw()`.

A new **boss** is four functions and a row in `gx_bosses[]`:

| Function | What it does |
| --- | --- |
| `init` | shares the stamina out among the parts and puts it above the screen |
| `think` | moves it and decides when it fires |
| `draw` | draws it with primitives, not with a bitmap: the parts move |
| `hitbox` | where each part is. **Radius 0 = it cannot be hit right now** |

The health bar is the sum of the parts plus the core, and the core is always
part number `parts`. The rule that "you have to bring down the pods first" comes
out of `hitbox` returning radius 0 for the core while a part is still alive;
there is no special case in the engine.

## The three bosses

| Planet | Boss | The trick |
| --- | --- | --- |
| Red Titan (`TITAN ROJO`) | **Crimson Guardian** (`GUARDIAN CARMESI`) | a cruiser with two pods; the core is armoured until both fall, and then it opens and fires in a fan |
| Sea of Neptune (`MAR DE NEPTUNO`) | **Orbital Kraken** (`KRAKEN ORBITAL`) | four arms that undulate and hurt on contact; it throws orbs that correct their course and every so often it throws itself at you |
| Belt of Ceres (`CINTURON DE CERES`) | **Belt Core** (`NUCLEO DEL CINTURON`) | a rotating fortress with four blocks; when all of them fall, the core opens and fires a spiral |

## Weapons

| Capsule | Weapon | How it behaves |
| --- | --- | --- |
| `D` | double | four straight bullets, high rate of fire |
| `T` | triple | a fan of three, of five at level 3 |
| `L` | laser | pierces, damage 2 |
| `O` | wave | slow and wide, damage 4, the best against bosses |
| `E` | energy | +90 of the bar |
| `S` | shield | takes one hit |
| `1` | extra ship | |

Picking up the same capsule raises the weapon's level up to 3. Dying takes you
back to the factory weapon, as it should.

## Simulator shortcuts

They only exist inside the simulator (`AOS_SIM_BUILTIN`); on the board there are
no environment variables.

| Variable | What for |
| --- | --- |
| `G2043_LEVEL=2` | starts straight on that planet, with automatic fire |
| `G2043_BOSS=1` | on top of that, jumps straight to the boss |
| `G2043_TEST=pu` | drops one capsule of each type |
| `G2043_TEST=over` | a single ship and little energy, to reach the end sign |
| `G2043_TRACE=1` | prints wave, enemies alive and milliseconds per frame |

In the simulator the side button is the **space bar** (with the full
press/release pair, so sustained fire really is tested) and the tilt comes from
the **mouse position**.
