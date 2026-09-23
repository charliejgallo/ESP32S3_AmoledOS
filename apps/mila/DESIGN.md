# Mila — game design

Mila is a Sokoban for the watch (ESP32-S3, 368 x 448 AMOLED). **Mila**, a
sweet black kitten with amber-yellow eyes, pushes things back to their place
around the house, one cell at a time. Between levels she lives in her
**casita**, where you pet her and play with the toys you bought. Art is
rendered in Blender (tools/blender/SPEC.md) and composited by the watch, the
recipe of Golf, Turbo and Monster Hop.

Decisions taken with the user on 2026-09-23 (the style sample in
`assets/_sample/board/index.html`):

- Camera **A**, straight: up on the screen is up on the grid. **72 x 54 px per
  cell**, Mila ~58 px tall; about 5 x 7 cells around her.
- A level opens on the **whole level** with its goal; a tap zooms onto Mila
  and the camera follows her from then on.
- **5 worlds x 8 levels = 40**, each world with its mechanic.
- Shop: **hats**, **collars and scarves** (a collar hides under her head from
  the front, a scarf shows from everywhere), **toys** for the casita.
- Coins from levels (first solve + every new star; some hats also come with
  finishing a world) **plus a daily gift** Mila leaves in the casita.
- ESP-NOW link: **visits** (the friend's Mila comes to your casita) and a
  **race** on the same level.
- The app is called **Mila**.

## 1. Rules

The grid holds, per cell, a terrain and at most one thing.

| terrain | Mila | objects |
| --- | --- | --- |
| floor | walks | pushed onto it |
| wall / furniture | no | no |
| target | walks | pushed onto it; the level is won when **every target holds an object** |
| wet floor (kitchen) | walks, no slip | **slides on** in the push direction until the next cell is not free, or it leaves the wet floor |
| plate (garden) | walks (holds it down) | holds it down |
| gate (garden) | passes while open | passes while open |
| cat flap (attic) | passes **along the flap's axis** only | never |
| hole (roofs) | never | falls in and **fills it** (the cell becomes floor) |

Things:

- **object** (`$`): pushed one cell. Never two at a time, never pulled.
- **ball** (`b`, roofs): pushed, then **rolls** on until the next cell is not
  free (a wall, a thing, a closed gate, a flap) or it drops into a hole. A
  ball resting on a target counts as an object there.

**Gates are open while every plate of the level is pressed** (by Mila or by a
thing). A gate never closes on an occupied cell: it waits until the cell is
empty. There may be more objects than targets (spare ones fill holes).

Undo is unlimited (the whole move history is kept: a move is 1 byte of
direction plus what it changed); restart is an undo to the start.

Score: **moves** (steps, pushes included). The par is the minimum number of
moves, computed by the solver when the levels are packed. Stars: ★ solved,
★★ within par + 25 % (at least par + 4), ★★★ at par.

## 2. Controls

- **Swipe**: one cell (push if something is ahead). Holding the finger after a
  swipe repeats the step every 180 ms.
- **Tap a free cell**: Mila walks there by herself along the shortest path,
  never pushing (nothing if it cannot be reached).
- **BOOT short**: undo. **BOOT long**: pause (restart, levels, casita).
- **Touch and hold Mila**: the whole-level view while the finger stays down.
- Furniture in front of Mila draws her silhouette on top (Monster Hop's x-ray).

## 3. Worlds

| # | id | the place | things | targets | mechanic |
| --- | --- | --- | --- | --- | --- |
| 1 | `living` | the living room | yarn balls | baskets | — (classic, with a tutorial) |
| 2 | `kitchen` | the kitchen | cookie tins | placemats | wet floor |
| 3 | `garden` | the garden | flower pots | soil circles | plates and gates (+ puddles) |
| 4 | `attic` | the attic | cardboard boxes | tape crosses | cat flaps (+ plates and gates) |
| 5 | `roofs` | the roofs at night | crates, balls | glowing moon marks | holes and rolling balls (+ flaps, puddles) |

Each world teaches its mechanic in levels 1-2, then mixes it with what came
before; level 8 is the hardest of the world. Pushes (minimum): 1-3 in the
tutorial levels, up to 40-60 in the last world.

## 4. The map, built to grow

**Nothing about the worlds is hard-coded.** The pack carries a `worlds` table
(built by `tools/levels/worlds.py`) and the code reads the number of worlds,
their levels and their art from it. Adding a world later is adding data and
art, plus code only if it brings a new mechanic.

The map is a **vertical strip of panels** scrolled with the finger:

```
  ┌───────────────┐
  │  (next world) │  a "coming soon" panel while there is none
  ├───── ● ───────┤  every panel joins the next at the same point
  │    roofs      │    (top centre / bottom centre), so they chain
  ├───── ● ───────┤    in any order
  │    attic      │
  │     ...       │
  │    living     │  8 level nodes on a paw-print path across the panel
  ├───── ● ───────┤
  │  the casita   │  the bottom: Mila's home (the hub)
  └───────────────┘
```

- A panel is one pre-rendered picture (`map_<world>`, 368 px wide, any
  height). Its **nodes** (where each level's stone sits) come with it in the
  art's meta (`extra.nodes`), so a world with 12 levels only needs a panel
  with 12 nodes.
- A locked world shows its panel dimmed, with a lock and the stars it needs.
- The scroll starts at the last world played.

The `worlds` table, per world, in order:

| field | |
| --- | --- |
| `id` | stable text id (`living`...): progress is saved under it, so worlds can be inserted or reordered without breaking saves |
| `name` | i18n key of the title |
| `kit` | the art prefix of its tiles (`living_floor_v0`...): a new world may reuse a kit |
| `panel` | its map panel |
| `need` | stars needed to open it |
| `music` | the tune |
| `levels` | the level names (1 to 16) |
| `mech` | bit mask of the mechanics it uses (the tutorial hints) |
| `gift` | the hat given when it is finished, or none |

**Saves** (prefs, text): `ml_s_<world id>` = one digit of stars per level
(`"33210000"`), `ml_coins`, `ml_own` (the items bought), `ml_wear`,
`ml_gift_day`, `ml_last` (world id and level). Nothing counts levels globally
("40 stars"): totals are summed from the table.

**The link** names a level by **world id + level index** and checks the
pack's hash in the hello: two watches with different packs do not race.

## 5. Levels

Written in `tools/levels/<world>.py` as text grids, checked and solved by
`tools/soko.py` (the solver also gives the par), packed as `lvl_<world>_<n>`.

| char | | char | |
| --- | --- | --- | --- |
| `#` | wall | `-` | outside (nothing drawn) |
| ` ` | floor | `.` | target |
| `$` | object | `*` | object on a target |
| `@` | Mila | `+` | Mila on a target |
| `~` | wet floor | `_` | plate |
| `G` | gate | `o` | hole |
| `v` | cat flap, crossed up/down | `h` | cat flap, crossed left/right |
| `b` | ball | `B` | ball on a target |

Other combinations (an object on a plate at the start...) go in a list of
extras next to the grid. **Furniture** is automatic: an inner wall cell with
three or four floor neighbours becomes a piece of furniture of the world
(chosen by a hash of the cell), two such cells side by side a two-cell piece;
a level may name its pieces instead. Limits: 16 x 16 cells, 8 things.

## 6. The casita

The hub, drawn 1.5 x bigger than the levels. No bars, nothing to keep up:

- Mila wanders on her own: sits at the window, sleeps in her bed, eats,
  grooms, gets into the box.
- **Tap her**: she meows, purrs (hearts) or rolls belly-up.
- **Drag the mouse or the feather**: she chases and pounces. **Tap a toy**:
  she uses it (scratching post, ball, tunnel, fish bowl...).
- The toys bought appear in their spot. The daily gift is a little parcel by
  her bed (once a day, a few coins).
- Buttons: **Play** (the map), **Shop**, **Settings** (sound, stats).
- **Visit** (link): the friend's Mila, wearing her outfit, walks in through
  the door and the two play together; both watches show their own casita
  with the guest.

## 7. The engine (outline)

Monster Hop's engine, trimmed: `.so` + `mila.pak` on the card, the worker on
core 0 renders frames into PSRAM, the LVGL timer blits them.

- The level's static layer (floor, walls, furniture, targets) goes into the
  same kind of background cache as Monster Hop's (colour + depth); moving
  things (Mila, objects, balls, gates, flaps) are sprites depth-tested
  against it.
- The whole-level view and the zoom: a half-scale picture of the level is
  built once at load (360 KB for a 12 x 12 level); the zoom samples it and
  cross-fades into the full-scale view at the end.
- Mila's hats and collars are layers over her frames, coloured through
  palettes (Tommy's caps).
