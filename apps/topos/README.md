# Topos

A whack-a-mole. Nine holes in a lawn; moles pop up and you tap them before they
duck back in. A mole in a hard hat takes two taps (the first knocks the hat
off), a golden one is worth a lot, and a bomb must not be touched.

---

## The three modes

| | Classic | Survival | Frenzy |
| --- | --- | --- | --- |
| Ends | after 60 s | with no hearts left | after 30 s |
| Difficulty climbs with | the clock, all the way in 60 s | the level: one every 8 moles | the clock, starting a third of the way up |
| A bomb | −25 points | a heart | −25 points and −2 s |
| A mole that gets away | breaks the streak | a heart | breaks the streak |
| A golden mole | 50 × combo, +3 s | 50 × combo, and a heart back (or +50) | 50 × combo, +2 s |
| Combo | ×2 at 5 in a row, up to ×4 | the same | every 4, up to ×5 |

A tap on an empty hole breaks the streak too; a tap outside the holes costs
nothing. After a bomb the mallet is useless for 0.7 s.

What the difficulty moves is all in `params()` in `main/tp_game.c`, as a
function of a single 0..1000 value: how long a mole waits before taunting
(1450 → 560 ms), the gap between appearances (1000 → 340 ms), how fast they
rise, how many at once (1 → 3; frenzy one more), and the odds of a hard hat
(0 → 30 %), a bomb (0 → 20 %) and a golden one. In classic that means bombs
from second 5, hard hats from 7, two moles at once from 20 and three from 40.

## Every state has its sprite

| Who | Appears | Waits | Nobody touched it | Touched |
| --- | --- | --- | --- | --- |
| Mole | pops out with wide eyes and an "o" | glances left and right, blinks | tongue out and a wiggle, then sinks — and it can still be hit while it taunts | squashed, eyes in X, three stars, then sinks dizzy |
| Hard hat | the same, under the hat | smug: half-closed eyes and a smirk | the same taunt, hat on | 1st: the hat flies off spinning, the mole is startled and sweats; then furious, teeth clenched. 2nd: as a mole |
| Golden | gold fur | twinkles around it | goes, and costs nothing | as a mole, with the bonus |
| Bomb | rises with its fuse lit | the fuse burns down; the last 45 % blinks red and shakes | the fuse goes out: dull and sooty, a puff of smoke, sinks | flash, a fireball in three layers, a shock ring, debris, smoke |

Plus the mallet (on target at once, then lifting), the impact rays, the clang
on the hat, a puff of dust for a tap on nothing, the dirt thrown up by
anything coming out, and the score popups.

## How it is drawn

**The lawn does not move.** Grass, holes and mounds are painted once into a
background buffer; what changes is what comes out of the holes, the effects
and the mallet. That is the dirty-rectangle scheme of arkanos and Claude Jump,
and it is what keeps a full field at 30 fps: the test bench measures 9–16 % of
the field pushed per frame.

**The compositor goes one step further.** Everything on the field is a *slot*,
and a slot is drawn from its `tp_dp_t` (16 bytes) and nothing else. If a
slot's parameters did not change, it costs nothing — most of the time most
moles are just standing there. If they did, the union of its old and new box
is *rebuilt*: restored from the background and every slot touching it
repainted inside it, in z-order. That is what lets a blast cover a
neighbouring mole or a popup float over one without leaving a trail.

**Boxes are measured, not declared.** The same painting code runs with no
buffer (`pen_t` in `tp_draw.c`) and adds up the rectangles it would touch, so a
box cannot miss a pixel its drawing puts down.

**The lip.** The buffer carries, besides its clip, a floor per column: the
front edge of the hole's opening. Everything an occupant draws goes through
it, so the mound painted in the background stays in front and the mole comes
*out of* the hole. The paws are drawn without it, over the rim.

**Sprites are rendered once, when the app opens** (`tp_art.c`, ~60 KB of
PSRAM, freed on close). Bodies, hat, bomb and mallet are shapes lit from the
upper left, with the light quantised into four tones and an outline — the hat
at eight angles and the mole in brown and gold come out of the same code.
Eyes, brows, mouths, paws, stars and icons are hand-drawn ASCII, because there
a single pixel carries the expression. Look at them after touching anything:

```bash
cd apps/topos/tools
cc -O1 -I../main tp_sheet.c ../main/tp_pixel.c ../main/tp_art.c -o /tmp/tps
/tmp/tps /tmp/sheet.ppm && python3 ../../../tools/ppm2png.py /tmp/sheet.ppm
```

**No word goes on the canvas.** Its 5x7 font has no accents; the score and the
popups are numbers and signs (`+20`, `X3`, `+3S`). Everything that is a word
is an LVGL label wrapped in `_()`.

## The test bench

```bash
cd apps/topos/tools
cc -O1 -I../main tp_harness.c ../main/tp_pixel.c ../main/tp_art.c \
   ../main/tp_game.c ../main/tp_draw.c -o /tmp/tph
/tmp/tph verify 20000                 # the check, below
/tmp/tph scene /tmp/s.ppm 2 20000     # the screen after 20 s of frenzy, x2
/tmp/tph scene /tmp/t.ppm title       # the title's scene
/tmp/tph strip /tmp/b.ppm boom        # mole | whack | helmet | gold | bomb | boom
```

`verify` has the bot play the three modes (and the title idle) with a jittery
frame time and, after **every** frame, compares the incremental drawing with a
from-scratch one; every five frames it paints each slot alone on a sentinel
canvas and fails if a pixel lands outside its box. Those are the two ways a
dirty-rectangle scheme goes wrong, and on the board both look the same: a
trail stuck on the screen. The strips are how the animations were reviewed.

## Simulator switches

A `getenv()` in `create()`, inside `#ifdef AOS_SIM_BUILTIN`.

| | |
| --- | --- |
| `TP_AUTO=1` | the bot plays and starts over after each game |
| `TP_MODE=0\|1\|2` | straight into classic, survival or frenzy |
| `TP_FPS=1` | frames per second in the score's strip |
| `TP_REC=500` | fake records (500, 1000, 1500), to see them on the title |
| `TP_SCREEN=pause\|over` | straight into that panel, for the layout audit |

```bash
cd sim
AOS_SIM_AUDIT_MS=3500 AOS_SIM_AUDIT=de/demo.topos.over TP_SCREEN=over \
    AOS_SIM_VIEW=demo.topos ./build/amoledos_sim
```

## Preferences

| Key | What it stores |
| --- | --- |
| `tp_hi0`, `tp_hi1`, `tp_hi2` | the record of each mode |
| `tp_sfx` | sound |
| `tp_fps` | the frames-per-second counter (a chip in the pause panel) |

## Screen limits that shaped the layout

- **The bottom row of holes ends at row 382 of the screen**, inside the touch
  panel's comfortable window (56..390, see `aos_hal.h`). Below it there is
  only grass on the field, and on the title a line of help that is read, not
  touched.
- **Pause is the whole top-left corner** (100 × 60), not just its icon: a small
  target at the edge of the glass. The icon itself sits 12 px in from the
  edge of the buffer, where the 38 px radius of the glass no longer bites.
- **Taps count on `PRESSED`**, not on release: a whack-a-mole is won the moment
  the finger lands. The app asks for `NO_SWIPE`, so a sloppy tap that slides
  does not pause the game; a swipe right leaves from the title only.
