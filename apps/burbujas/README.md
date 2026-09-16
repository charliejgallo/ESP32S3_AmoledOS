# Burbujas

A bubble shooter. A hexagonal board hangs from the ceiling, the launcher at
the bottom throws bubbles at it, and three or more of a colour that end up
touching burst — and whatever is left hanging from nothing falls with them.

You aim by dragging anywhere on the board: the dotted line shows the shot
with its bounces and a dashed circle shows the cell it would stick to.
Letting go shoots; letting go below the line does not. A tap on the bottom
strip swaps the bubble in the launcher for the one waiting in the pipe.

---

## The three modes

| | Classic | Levels | Time attack |
| --- | --- | --- | --- |
| Ends | when the board passes the line | the same, or when it is cleared | after two minutes |
| A new row | every few shots that burst nothing | the CEILING comes down instead | every 9 s, falling to 5 s |
| Starts with | five rows, four colours | the level's board, 3 to 6 colours | four rows, five colours |
| Gets harder by | rows pushed: five colours at 6, six at 16; the misses allowed fall 6 → 3 | the level number: more rows, more colours, fewer misses | the clock |
| Kept | the best score | the level reached | the best score |

A burst is 10 points a bubble, a drop 20 and more the bigger it is. Clearing
the board is worth 1000 in the endless modes and ends the level in Levels.

**Bombs and rainbows.** A burst of six, or four dropped at once, puts a
special into the pipe: a bomb clears everything within a bubble and a half of
where it lands, and a rainbow one takes the colour of whatever it touches.

## The levels are generated, not written down

The level number is the seed, so level 12 is the same board today and in a
month, and there is no table to maintain. Six patterns (solid, a wedge, holes,
pillars, a diamond, a lattice) with the colours clustered, and then anything
that would have started hanging from nothing is removed — a board that drops
the moment you touch it looks like a bug and plays like a gift.

## How it is drawn

**The still board is not drawn every frame: it IS the background.** Fifty
bubbles hanging there are what a bubble shooter mostly is, and none of them
cost anything per frame. When a cell changes, `bb_game.c` marks its rectangle
and `bb_draw.c` rebuilds that rectangle of the background from the backdrop
plus whatever bubbles reach into it. On top go the slots — the shot, the
guide, the bursts, the falls, the launcher — with the compositor of `topos`:
a slot is drawn from its 16-byte descriptor and nothing else, so parameters
that did not change cost nothing, and what changed is rebuilt from the
background plus every slot that touches it.

Measured by the bench: **9 % of the field pushed per frame in Levels and 18 %
in Classic**, against the 100 % of a full redraw.

**One sphere, eight bubbles.** `bb_art.c` works out once which of five tones
each of the 22x22 pixels is, lit from the upper left and quantised; a colour
is five values derived from one. The six colours, the rainbow one (the same
sphere with the palette chosen per diagonal band), the bomb and the greyed-out
board of a lost game are all that map with a different palette.

**The guide is the shot.** The dotted line, the flight and the bot all advance
through `bb_ray_step()`, one pixel at a time, so the line cannot promise a
bounce the bubble will not make.

**No word goes on the canvas.** Its 5x7 font has no accents; the score, the
level and the popups are numbers and signs. Everything that is a word is an
LVGL label wrapped in `_()`.

## The test bench

```bash
cd apps/burbujas/tools
cc -O1 -I../main bb_harness.c ../main/bb_pixel.c ../main/bb_art.c \
   ../main/bb_game.c ../main/bb_draw.c -o /tmp/bbh
/tmp/bbh verify 20000                  # the check, below
/tmp/bbh scene /tmp/s.ppm 0 9000       # the screen after 9 s of the bot
/tmp/bbh strip /tmp/b.ppm bubbles      # bubbles | pop | blast | fall
/tmp/bbh trace 1 1 26                  # shot by shot: misses, ceiling, line
/tmp/bbh poke 1 1                      # the same four aims over and over
```

`trace` and `poke` exist because a bot that aims well hides whatever only
bites a player who keeps shooting at the same place: `poke` shoots at four
fixed points, which stacks a tower of unmatched bubbles that reaches the line
in seven shots, and `trace` prints where the ceiling and the lowest bubble are
after each shot so that a game that ended can be explained rather than
guessed at.

`verify` has the bot play the three modes with a jittery frame time and, after
**every** frame, compares the incremental drawing with a from-scratch one;
every five frames it paints each slot alone on a sentinel canvas and fails if
a pixel lands outside its box. Those are the two ways a dirty-rectangle scheme
goes wrong, and on the board both look the same: a trail stuck on the screen.

It also checks the rules after every shot, which no drawing check can see:
nothing may be left hanging from nothing, no cell may hold a colour that does
not exist, and no offset row may use its eighth column — the classic
hexagonal-neighbour bug does not crash, it just makes groups come out wrong.

## Simulator switches

A `getenv()` in `create()`, inside `#ifdef AOS_SIM_BUILTIN`.

| | |
| --- | --- |
| `BB_AUTO=1` | the bot plays and starts over after each game |
| `BB_MODE=0\|1\|2` | straight into classic, levels or time attack |
| `BB_LEVEL=9` | which level the levels mode starts at |
| `BB_AIM=40x70` | hold the aim at that point, for a screenshot |
| `BB_FPS=1` | frames per second in the score's strip |
| `BB_REC=1200` | fake records, to see them on the title |
| `BB_SCREEN=pause\|over` | straight into that panel, for the layout audit |

```bash
cd sim
BB_MODE=1 BB_LEVEL=9 BB_AUTO=1 AOS_SIM_VIEW=demo.burbujas ./build/amoledos_sim
```

## The icon travels inside the .so

`BURBUJAS_ICON` in `burbujas.c` is a blob of AIC bytes (`aos_icon_ops.h`)
handed over from `init()` with `aos_icon_set_ops()`, so adding this app to a
watch is copying one file: no firmware, no reflash. It needs v0.3.8 or newer,
and `build_apps.sh` says so at build time rather than at load time.
