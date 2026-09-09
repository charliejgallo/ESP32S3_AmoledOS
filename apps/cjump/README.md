# Claude Jump

A vertical platform jumper starring the Claude Code critter. You climb by
bouncing, collect coins and spend them on costumes in the menu's shop.

The game scrolls **without repainting the screen**: the background is fixed and
only the dirty rectangles are pushed. That is the *why*; what follows is the
*how*, for working on it.

---

## Adding a costume

It is the most likely change and it is two places, both in `main/cj_skins.c`.

**1. One row in `cj_skins[]`:**

```c
{ N_("Buzo"), 320, 0x3FA9C9, 0x1E5F76 },
/*    name   price   body     shadow  */
```

- The name carries `N_()` and is translated in the shop with `_()`. **It goes
  into an LVGL label, not onto the canvas, so it may carry accents** — that is
  the difference from 2043 or arkanos, which draw their text with a bitmap font
  of their own and have their catalogues transliterated.
- Price 0 = it comes unlocked. If you add a free one, raise `CJ_SKINS_FREE` in
  `cjump.h` as well.
- The body and the shadow are the whole critter's. If the body is dark
  (luminance < 120) the eyes are drawn with white behind them automatically;
  nothing to do, but it is worth knowing why it exists: without it the Ninja was
  a grey rectangle with no face.

**2. A `case` in `accessory()`**, with the same index as the row.

And then raise `CJ_SKINS`. Since it is a 32-bit map in the `cj_own` preference,
the real ceiling is 32 costumes.

### The rule that cannot be broken

Everything the costume draws has to fit within `HERO_BOX_W x HERO_BOX_H`
(28x30): 5 px on each side of the body and 14 px above. That is the rectangle
that gets dirtied, and **whatever runs outside it leaves a trail stuck on the
screen** — nothing crashes, it just looks dirty, and it only shows up after a
while of playing.

Do not trust your eye for that. The test bench verifies it:

```bash
cd apps/cjump/tools
cc -I../main -I../../../components/aos_ui/include \
   cj_skins_harness.c ../main/cj_pixel.c ../main/cj_skins.c -o /tmp/cjsk
/tmp/cjsk /tmp/skins.ppm && sips -s format png /tmp/skins.ppm --out /tmp/skins.png
```

It draws each costume onto a canvas painted a sentinel colour and **exits with
an error if a pixel is left outside the box**, saying which and at what
coordinate. As a bonus it writes a grid with all sixteen together, which is the
only way to see whether two came out too similar.

It needs neither LVGL, nor the HAL, nor the simulator.

---

## Simulator switches

A `getenv()` in `create()`, inside `#ifdef AOS_SIM_BUILTIN`. They do not exist
on the board.

| | |
| --- | --- |
| `CJ_AUTO=1` | the critter plays itself and starts over on dying. This is what you leave running for a good while to hunt for trails from badly recorded dirty rectangles |
| `CJ_FPS=1` | frames per second and % of screen pushed, in the score |
| `CJ_COINS=500` | coins, for testing the shop without playing |
| `CJ_SKIN=11` | costume worn (and unlocked) |
| `CJ_OWN=1` | everything unlocked |
| `CJ_SHOP=1` | opens straight into the shop |
| `CJ_ZONE=3` | starts the game in that zone |
| `CJ_SCREEN=pause`&nbsp;\|&nbsp;`over` | opens straight into that panel |

`CJ_SCREEN` exists for the layout audit: `audit_layout.sh` opens each app with
`AOS_SIM_VIEW` and audits what it sees, and what it sees is the menu — the other
panels are born hidden and the auditor skips what is hidden.

```bash
cd sim
AOS_SIM_AUDIT=de/cjump.over CJ_SCREEN=over AOS_SIM_VIEW=demo.cjump ./build/amoledos_sim
```

---

## Preferences

All with the `cj_` prefix. To wipe the progress it is enough to remove them from
the simulator's `prefs.txt`, or from the portal on the board.

| Key | What it stores |
| --- | --- |
| `cj_hi` | record in metres |
| `cj_coins` | coins in your pocket |
| `cj_skin` | costume worn |
| `cj_own` | bitmap of the ones bought (the first three are forced on load) |
| `cj_ctrl` | 0 sensor, 1 finger |
| `cj_axis` | which of the sensor's four mappings |
| `cj_sfx` | sound |
| `cj_fps` | frames-per-second counter |

---

## Working on the game

| What | Where |
| --- | --- |
| Zones: sky, platform colours and difficulty | `cj_zones[]` in `cj_game.c`. Adding one is a row and raising `CJ_ZONES` |
| Jump, gravity, spring, rocket | the `#define`s in `cjump.h`, all in 1/16 of a pixel per frame |
| What a coin is worth / the bonus | `check_coins()` in `cj_game.c` and `BONUS_PER_M` in `cjump.c`. They are calibrated against the costume prices |
| How a platform is drawn | `draw_plat()` in `cj_draw.c` |

### Two screen limits to respect

Both came from the board and **the simulator does not show them**, because it is
a flat rectangle and the watch is not:

- **Nothing touchable below y=354.** The touch panel does not respond there.
  The shop's back button is an arrow at the top left precisely for that reason.
  The ~90 px at the bottom carry notices, which do not need touching.
- **20 px of side margin in the top strip.** The glass's corners have a 38 px
  radius and the bezel eats a little more; with the score against the edge it
  read as `_9M`.
