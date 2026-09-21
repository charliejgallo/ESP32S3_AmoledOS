# Neon Snakes

Neon snakes eat neon fruit on a screen that is black everywhere else. No
score and no HUD: the snakes, the fruit and the frame of the arena.

| | Normal | Battle |
| --- | --- | --- |
| Arena | 21 x 24 cells of 16 px | 34 x 40 cells of 10 px: the camera further away |
| Snakes | yours | four: you and three bots, or you, the paired watch and two bots |
| Fruit | one at a time | nine on the board, plus the sparks a dead snake leaves |
| Dying | against a wall or yourself: AGAIN / MENU | the snake flashes, leaves every other segment as sparks of its colour, and comes back after ~3 s |
| Speed | 150 ms a step, down to 85 as it grows | 125 ms a step |

Seven fruits (cherry, apple, banana, grapes, strawberry, orange, melon), each
worth one to three segments; a spark is worth one. When your snake appears or
comes back, a ring in its colour marks which one is yours for a couple of
seconds.

**Steering**, chosen on the menu and kept (`neon_ctrl`):

- *Touch*: swipe the way you want to go (one drag can turn several times,
  every 22 px), or tap on the side of the head you want to turn to. The chip's
  own swipe gesture is heard too, for the fast ones the CST816 swallows.
- *Tilt*: past 0.17 g on the stronger axis the snake turns that way. The zero
  is how the watch is held when the game starts; a tap levels it again.

The button (or back) pauses. Two turns can be queued ahead of the step, so a
quick U-turn is not lost.

## Two watches

Battle -> MULTIPLAYER, with the watches paired in Link. The lower MAC is the
host and the match is **lockstep**, as in Truco: both watches run the same
engine from the same seed; on every step the host decides where both humans
turn (its own input and the guest's TURN frames) and sends that STEP on the
reliable channel before applying it, and the guest applies exactly the STEPs
it gets, in order. Each STEP carries the host's hash of the board, and the
guest logs `DESYNC` if its own differs (never seen: 20,000 steps in the bench,
minutes of two simulators).

The guest applies STEPs as they arrive, with no inbox of its own: if it falls
behind, the link's receive ring fills and stops acknowledging, and the host,
which does not step with more than eight frames unacknowledged, waits. (The
first version had a 16-frame inbox that dropped a step when a slow window got
behind, and the guest froze on "step 147 arrived, expected 146".)

**On the boards the START can overtake the HELLO.** START travels on the
reliable channel and the host's HELLO on the fast one, and on two real
watches the START arrived first: the guest went into the match without the
host's MAC or nonce, the beacon check compared against zeros and the late
HELLO looked like the host re-entering - "charlie left the game" six seconds
in, with the host still playing. The simulator never showed it. Now the
partner's MAC comes from Link (`aos_hal_link_partner()`) and the host's nonce
from the START itself.

Measured on two boards (2026-09-21, v0.4.7 host and v0.4.4 guest): a minute
of play, 470 STEPs sent and acknowledged, 0 retransmissions, 13 turns from the
guest, no DESYNC, 26-28 fps on both, the same board on both screens.

The other watch leaving is caught three ways: a BYE frame, its beacon no
longer offering `neon` for 6 s, and on the guest no frame for 4 s.

## How it is drawn

`ns_art.c` draws every sprite by code when a mode opens: each fruit and each
part of a snake (6 bends and 4 tails in two stripe colours, 4 heads with eyes
and tongue, the white of the flash) is a list of discs, capsules, cones,
ellipses and arcs in cell units, shaded from their distance field - a white
core, a one-pixel rim, a glow over half a cell. Measured on the board: 575 ms
for Normal (at open) and 392 ms for Battle (when chosen).

The screen is a 368x448 RGB565 canvas shown 1:1. Because the background is
black everywhere, glows are baked into the sprites and combined by taking the
brighter channel. A sprite is 2x2 cells centred on its own, so a cell can be
repainted from its 3x3 neighbourhood alone; `ns_draw.c` repaints only the
blocks around what the engine marked (a head, a tail, a fruit) plus the
effects and the fruits' pulse, and hands LVGL a handful of rectangles.
Measured: 1.6 % of the screen per frame in Normal and ~6 % in Battle in the
bench; **28 fps** on the board in Battle, pushing 2 %.

## Files

| | |
| --- | --- |
| `main/ns_game.c` | the rules: deterministic, integers only, no LVGL; the bots |
| `main/ns_art.c` | the sprites and the title's tube letters |
| `main/ns_draw.c` | the compositor |
| `main/neon.c` | menus, input, the link, the timer, the icon |
| `tools/ns_harness.c` | the bench |

```bash
cc -O1 -I../main tools/ns_harness.c main/ns_game.c main/ns_art.c main/ns_draw.c -lm -o /tmp/nsh
/tmp/nsh                              # rules, compositor vs full repaint, lockstep
/tmp/nsh shot title|normal|combat out.ppm [steps]
```

The bench checks the grid against the snakes after every step, draws every
frame both ways (the marked blocks, and from scratch) and compares them to
the pixel, and runs two engines on the same random directions for 20,000
steps comparing hashes.

## Development switches

```bash
NS_MODE=normal|solo|combat|multi   straight to a game or a screen
NS_AUTO=1                          the bot steers your snake as well
NS_TILT=1                          steering by tilt
```

Two simulators as two watches:

```bash
AOS_SIM_LINK_PORT=47000 AOS_SIM_LINK_PARTNER=47001 NS_MODE=multi NS_AUTO=1 AOS_SIM_VIEW=demo.neon ./build/amoledos_sim
AOS_SIM_LINK_PORT=47001 AOS_SIM_LINK_PARTNER=47000 NS_MODE=multi NS_AUTO=1 AOS_SIM_VIEW=demo.neon ./build/amoledos_sim
```

On the Mac the window that is not in front is throttled to 3-7 fps; that is
macOS, not the game, and it is how the inbox bug above showed up.
