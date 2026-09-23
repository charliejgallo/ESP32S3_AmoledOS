# Mila

A Sokoban with a black kitten. Mila, sweet and small with big amber eyes,
pushes things back to their place around the house, one cell at a time:
yarn balls into their baskets, cookie tins onto their placemats, flower pots
onto their soil. Between levels she lives in her casita, where you pet her
and play with the toys you bought. Every tile, piece of furniture, toy, hat
and pose of hers is modelled in Blender and rendered to sprites that the
watch sorts by depth.

| | | |
|---|---|---|
| <img src="../../docs/img/app-mila-overview.png" width="200"><br>A level opens on the whole room with its goal and its par: here the kitchen, with puddles that make the tins slide. | <img src="../../docs/img/app-mila-play.png" width="200"><br>A tap and the camera flies down onto Mila: the garden, flower pots, a soil circle and a gate that opens while a plate holds a pot. | <img src="../../docs/img/app-mila-peek.png" width="200"><br>A finger held on Mila shows the whole room again, things where they are now: the rooftops, with a hole a crate can fill. |
| <img src="../../docs/img/app-mila-casita.png" width="200"><br>Her casita, the hub: she wanders, sleeps, grooms and plays with the toys you bought. | <img src="../../docs/img/app-mila-map.png" width="200"><br>The map: a strip of panels, one per world, that chain at their centres; Mila stands on the current level. | <img src="../../docs/img/app-mila-shop.png" width="200"><br>The shop, on a light background so her black fur shows: hats, collars and scarves in eight colours, and toys. |

| World | Things → targets | What is new |
| --- | --- | --- |
| The living room | yarn balls → baskets | the classic rules, with a gentle start |
| The kitchen | cookie tins → placemats | wet floor: a pushed tin slides on until something stops it |
| The garden | flower pots → soil circles | plates and gates: gates stay open while every plate holds something |
| The attic | cardboard boxes → tape crosses | cat flaps: Mila goes through, boxes do not |
| The rooftops at night | crates → moon marks | holes that a crate fills, and balls that roll until they hit something |

Eight levels per world, 40 in all. Every level is checked by a solver that
runs the game's own rules (`tools/solve.c` on `main/ml_rules.c`), and the
solver's shortest solution is the level's **par**: three stars at par, two
within a quarter more, one for solving it. Worlds open with stars.

## Playing

- **Swipe** to step one cell; a thing in the way is pushed. Keep the finger
  down after a swipe and Mila keeps walking.
- **Tap a cell** and Mila walks there by the shortest way, never pushing.
- **Undo** as far back as you like: the button at the bottom left, or the
  BOOT button. **Restart** at the bottom right. A long BOOT press pauses.
- A level opens on the whole room with its goal. A tap flies the camera
  down onto Mila, and from then on it follows her. **Touch and hold Mila**
  to see the whole room again.
- When furniture hides her, her outline shows through it.

## The casita

Mila's home is the hub: no bars to fill, nothing to keep up. She wanders on
her own, sits by the window, sleeps in her bed, eats, grooms, and plays with
her toys. Tap her and she meows, purrs or rolls over. Drag the mouse, the
ball or the feather and she chases it. Once a day there is a little parcel
by her bed with a few coins.

The shop sells hats (a bow, a party hat, a beret, bunny ears, a crown...),
collars and scarves, and toys for the casita (a scratching post, a tunnel,
a fish bowl, a window hammock...). Hats and scarves come in eight colours,
free once bought. Finishing a world gives one of them as a present.

## Two watches

With the watches paired in Link, the casita shows a friend button:

- **A visit:** your friend's Mila, in her own outfit, comes in through the
  door and plays with yours. Each watch shows its own casita with the guest.
- **A race:** both play the same level, picked among those open on both;
  a pill under the counters shows how far the other one is. The first to
  solve it wins.

Both watches need the same `mila.pak`: they compare it before playing.

<img src="../../docs/img/app-mila-visit.png" width="200">

## Installing

`mila.so` and `mila.pak` (6.6 MB, one part) go in `/apps` on the card, and
the English and German catalogues are embedded in the firmware from v0.5.5
(or come in `lang.zip` for older ones). `mila.so` needs firmware **v0.4.10**
or later. On a board, `apps/mila_dev.txt` can say `unlock` (every level
open) or `level=roofs,8` (open straight into that level); leave it empty
after measuring. In a level the log says, every five seconds, the mode,
the moves and the frames per second.

## How it is built

The engine is Monster Hop's: the art in one pack on the card, a worker on
core 0 that renders every animated screen into PSRAM frames, depth-tested
sprites over a background cache, and the LVGL panels on top.

- The camera looks straight up the grid: one cell is 72 x 54 px, so up on
  the screen is up in the level and a swipe means what it looks like.
- The level's static layer (floor, walls, furniture, targets, their shadows
  and lamp light) is drawn into the cache once; Mila, the things, gates and
  flaps move over it. The whole-level view is the same sprites box-filtered
  down to fit, and the zoom samples it until the real frame fades in.
- **The worlds are data.** `tools/levels/worlds.py` lists them; the pack
  carries that table, the levels as text and each world's map panel, and
  the code reads the number of worlds, their names in three languages and
  their levels from it. Progress is saved by world id. A new world that uses
  the existing mechanics is a new entry, a level file, a kit of tiles and a
  map panel, with no change to the code. See `DESIGN.md`.

## Tools

| | |
| --- | --- |
| `tools/blender/` | the art: `ml_common.py` (camera, passes) and `SPEC.md` (what each asset must be), one script per part |
| `tools/solve.c` | the solver, on the game's own rules |
| `tools/gen.py` | level candidates for a world and level, generated and scored by the solver |
| `tools/levels/` | the worlds table and the levels |
| `tools/levels.py` | solves every level and writes them for the pack |
| `tools/pack_assets.py` | the renders, the levels and the table into `assets/mila.pak` |
| `tools/compose.py` | puts sprites together the way the watch does, for previews |

In the simulator: `ML_LEVEL=kitchen,3` opens that level, `ML_SCREEN=map`,
`shop` or `settings` opens that screen, `ML_UNLOCK=1` opens everything,
`ML_COINS=500` fills the purse and `ML_LOADER=1` keeps the loading screen up
(the simulator loads too fast to see it). Two simulators with
`AOS_SIM_LINK_PORT` / `AOS_SIM_LINK_PARTNER` and `ML_LINK=visit` or
`ML_LINK=race` go straight into a visit or a race.
