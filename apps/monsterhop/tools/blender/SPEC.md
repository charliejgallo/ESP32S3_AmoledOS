# Monster Hop — 3D assets rendered in Blender

Monster Hop is an action game for the watch (ESP32-S3, 368 x 448 AMOLED,
RGB565, portrait), in the manner of *Frogger: He's Back!* (1997): **Tommy**, a
chibi kid in a cap and a bright T-shirt, hops cell by cell across big 3D levels
full of monsters and picks up the **five keys** that open the exit. Four zones,
each with its monster and its colour:

| zone | monster | colour | the place |
| --- | --- | --- | --- |
| `city` — Zombie Town | zombies | **grey** (cool greys, sodium-orange lamps, a little teal) | an abandoned town: streets, sidewalks, brick buildings, wrecked cars, a junkyard, sewers |
| `castle` — Vampire Castle | vampires and their bats | **violet** (purples, lilac stone, crimson accents, candle orange) | a castle at night: flagstones, carpets, moat, battlements, stained glass |
| `desert` — Mummy Desert | mummies | **brown** (sand, ochre, terracotta, gold, turquoise accents) | dunes, an oasis, a sandstone temple and a pyramid |
| `forest` — Werewolf Woods | werewolves | **green** (deep greens, moss, teal moonlight, warm lanterns) | a forest at night: trails, a river with logs, an old mill, a moonlit clearing |

Future zones (not now, keep room in the style): a witch swamp, a skeleton
graveyard. So: **no graveyards, tombstones or skeletons** in the four zones
above, and zombies do not live in a cemetery.

**Tone: Halloween for kids.** Cartoon, chibi, bright and readable; spooky, not
scary. **No gore, no blood, no weapons that hurt.** Monsters have glowing eyes
and silly faces; the menace is in the silhouette and the pose.

Everything is **rendered in the Mac** and only **placed** by the watch: the
watch never draws a triangle. It composites pre-rendered sprites with a
**depth buffer**, so each sprite carries a depth pass and hides and is hidden
per pixel, exactly like the 3D scene would.

---

## 1. The shared module — read `mh_common.py`

Every script imports `mh_common` (next to this file). It owns **the camera, the
lights, the passes, the files and meta.json**. Do not set up cameras or suns of
your own (UI art in section 9 is the only exception), and **do not edit
mh_common.py or tools/compose.py**: several people use them at once. If you need
something they do not do, write it in your own script, or say so in your report.

```python
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import mh_common as C
a = C.args()                      # --out DIR [--only a,b] [--samples N] [--cpu]
C.reset('forest')                 # empty scene, game camera, the zone's light
C.mat('bark', base=(0.30, 0.20, 0.12), rough=0.9)            # register materials
C.mat('skin', base=(0.95, 0.72, 0.55), id=1)                 # id: region (characters)
ob = C.box('trunk', 0.42, 0.42, 0, 0.58, 0.58, 1.2, 'bark')  # or any bpy geometry + C.assign(ob, key)
C.render_sprite(a.out, 'forest_pine', [ob, ...], C.cell(0, 0, 0),
                passes=('color', 'z', 'shadow'), shadow_z=0.0, kind='prop')
C.save_meta(a.out)                # writes/merges <out>/meta.json
```

Run: `/Applications/Blender.app/Contents/MacOS/Blender -b -P <script>.py -- --out ../../assets/<dir>`.
Blender **3.3.1**, Cycles on Metal (a small sprite renders in about a second).

### The projection (fixed)

Orthographic, yaw 18.43°, elevation 44.43°, **63.25 px per metre**, chosen so
that the grid lands on whole pixels:

| in the world | on the screen (x right, y down) |
| --- | --- |
| +1 m along **X** (one cell to the right) | **(+60, +14) px** |
| +1 m along **Y** (one cell forward, away from the camera) | **(+20, −42) px** |
| **one floor** up = `C.FLOOR_M` = **0.50923 m** | **(0, −23) px** |

Metres, **Z up**. A **cell** is 1 x 1 m; cell (x, y) spans [x, x+1] x [y, y+1].
Heights come in **floors** of 0.50923 m (a crate, a step): the ground is floor
0, raised ground goes up to floor 3. The camera looks from the front-right:
you see the **top**, the **front (−Y) face** and the **right (+X) face** of a
block. The sun comes from the front-left (`C.SUN_ELEV` 55°, `C.SUN_AZ` 210°),
so tops are brightest, fronts are lit, right faces are in shade, and shadows
fall back and to the right. The same sun in every render of every zone.

`C.cell(x, y, floor)` is the world point at the centre of cell (x, y) on top
of that floor. **Build every asset at cell (0, 0), floor 0**: its anchor is
`C.cell(0, 0, 0)` = (0.5, 0.5, 0) unless said otherwise. The watch places the
anchor anywhere in the level; the sprite moves with it, pixel-exact.

### Passes and files

`C.render_sprite(out, name, objs, anchor, passes=...)` renders one sprite and
writes (all PNG, same size, fitted to the object unless `size=` is given):

| pass | file | what |
| --- | --- | --- |
| `color` | `name.png` RGBA | final colour under the zone light, antialiased alpha, denoised |
| `light` | `name.png` RGBA | the same shading on **neutral grey 0.8** under `reset('neutral')`: a surface in full sun is ~197 (calibrated); the watch colours `palette[id] * light / 196` |
| `id` | `name_id.png` L | region id × 16, no antialiasing, covers every pixel the colour/light pass covers |
| `z` | `name_z.png` L | depth along the view direction relative to the anchor: 128 = the anchor's depth, 1 step = 1/32 m, **smaller = nearer**, 255 = nothing |
| `shadow` | `name_sh.png` L | how much the object darkens the ground plane z = `shadow_z` (0 = none, 255 = black), nothing of the object itself |
| `glow` | `name_gl.png` RGB | the light the object's lamps/emitters throw on a white ground at `shadow_z`, alone (black = none): the watch adds it to the ground around. Give `size=` large enough for the light pool (fit() only sees the object) |

`meta.json` gets, per sprite: `w, h`, `ax, ay` (the pixel **corner** where the
anchor lands; the watch puts the image's top-left at anchor − (ax, ay)),
`anchor`, `kind`, `files`, plus whatever you pass in `extra={...}` (use it for
`footprint`, `height_m`, `frames_ms`, etc.). The packer crops the transparent
margins; you do not have to.

**Characters are recoloured by the watch**, so they use `light` + `id` + `z`
(+ `shadow`) under `C.reset('neutral')` (or `C.set_light('neutral')`).
**Tiles and props are final colour** under their zone light: `color` + `z`
(+ `shadow`, + `glow` for lit things).

### Seeing it in the game: `tools/compose.py`

`apps/monsterhop/tools/compose.py scene.json out.png [scale]` puts your sprites
together the way the watch will (depth-tested, anchors on the grid, palettes
for the recoloured ones). **Use it**: build a small scene with your assets on a
floor of blocks and look at it at 368 x 448 before calling anything done. The
docstring shows the scene format; `tools/blender/test_tiles.py` and the
scene in section 11 are a working example.

---

## 2. Readability rules (a 1.8" screen)

- Tommy is about **50 px tall** on screen and a cell is 60 px wide. Everything
  is seen at that size: **bold silhouettes, big shapes, strong value contrast**,
  no detail thinner than ~2 px (3 cm) that matters.
- **Chibi proportions** for every character: the head is ~40 % of the height,
  big eyes, short limbs. Monsters get **glowing eyes** (emissive, they read in
  the dark zones) and a silhouette you recognise in a blink.
- Surfaces you can stand on must read as **flat and walkable**; walls and
  obstacles as **solid**. Water, quicksand, pits must be unmistakable.
- Top faces of blocks get a **very slight bevel/darkening at the edges**
  (~0.02 m) so the grid reads faintly: players count cells.
- Keep saturated accents for things that matter (keys gold, coins yellow,
  hazards with a warning tint) and let the ground be calmer than the actors.

---

## 3. Directions and animation conventions (characters)

Four facings, by the direction the character looks:

| dir | faces | yaw of the model |
| --- | --- | --- |
| `s` | −Y, **towards the camera** (we see the face) | 0° |
| `e` | +X (to the screen right, slightly down) | +90° |
| `n` | +Y, away from the camera (we see the back) | 180° |
| `w` | −X | −90° |

**Model every character facing −Y** and rotate it about Z by the yaw (counter-
clockwise seen from above). The camera sees from the front-right, so `e` and
`n` are NOT mirrors of `w` and `s`: render all four.

**Animations are in place.** Every frame is rendered with the character's root
at the anchor `C.cell(0, 0, 0)` (the ground point between the feet). The watch
moves the anchor from cell to cell and adds the jump arc; the frames only carry
the pose (crouch, stretch, tuck, squash). Frames of an airborne pose may have
the feet above the anchor (tucked legs); never move the whole body sideways.

Frame names: `<who>_<anim>_<dir>_<nn>` (nn from 00), e.g. `tommy_hop_e_03`.
Record the playback in `extra={'anim': 'hop', 'dir': 'e', 'frame': 3,
'frames': 6, 'ms': 30}` (ms per frame the watch will use; the numbers below).

A rigid-part rig is enough at this size (head, torso, arms, legs, hands, feet
as separate objects parented to empties you rotate per frame); skinning is
welcome where it pays (a cape, a tail) but not required. Put smooth shading
and a little subdivision on organic parts; keep polygon silhouettes out.

Every character frame: `passes=('light', 'id', 'z', 'shadow')`, `shadow_z=0.0`,
`bounce_ground=0.0` (an invisible floor bounces light back up, so they sit on
the ground), `kind='char'`, samples 48.

---

## 4. Tommy (`chars.py` → `assets/chars/`)

A kid of ~11, chibi, **1.00 m tall** (head ~0.42 m across), friendly and brave:
big eyes, a small confident smile, short hair poking out under the cap, a
T-shirt, knee-length shorts or jeans, chunky sneakers. **The body renders
without a cap** (hair visible); caps are a layer (below). **The player picks
Tommy's skin tone and hair colour** (palette only), so skin and hair must be
clean regions that look right in any tone from very light to very dark and
any hair from blond to black to bright blue: shading in the light pass, no
colour baked in.

### Region ids — body

| id | region | notes |
| --- | --- | --- |
| 1 | skin | face, arms, legs |
| 2 | hair | |
| 3 | dark details | pupils, brows, mouth line, nostrils: the watch keeps them near-black |
| 4 | white details | eye whites, teeth, catchlights: kept white |
| 5 | shirt A | the T-shirt's main colour |
| 6 | shirt B | **two bands across the chest + the sleeve cuffs** (a plain shirt = A for both) |
| 7 | shirt C | **a round emblem on the chest** (front only, ~12 cm) |
| 8 | pants | shorts or jeans |
| 9 | shoes A | the sneaker upper |
| 10 | shoes B | soles, laces, toe cap |
| 11 | socks | a bit showing above the shoes |
| 12 | blush | small cheek spots (the watch tints them pink or turns them off) |
| 13-15 | spare | |

### Animations — body

| anim | dirs | frames | ms/frame | pose |
| --- | --- | --- | --- | --- |
| `idle` | n e s w | 4 | 250 | breathing bob; frame 2 blinks (eyes closed); relaxed arms |
| `hop` | n e s w | 6 | 30 | 00 crouch, 01 push-off stretch, 02 tuck (knees up, arms up), 03 tuck, 04 legs reaching down, 05 landing squash |
| `super` | n e s w | 6 | 60 | the big jump (two cells or two floors): 00 deep crouch, 01 launch with arms high, 02-03 a tight tuck (a front-flip feel: knees to chest, body pitched forward ~40° in 03), 04 open up, 05 landing crouch |
| `push` | n e s w | 4 | 120 | leaning into a crate in front of him, hands forward at chest height, legs driving (a loop) |
| `use` | n e s w | 4 | 100 | reaching forward-down and pulling a lever towards himself (00 reach, 01 grab, 02 pull, 03 back) |
| `win` | s | 8 | 100 | the level is done: jump with a fist pump, land, wave at the camera |
| `hurt` | s | 6 | 100 | caught by a monster: startled jump back, eyes wide (id 4 big), then dizzy wobble (the watch adds stars) |
| `sink` | s | 4 | 150 | sinking in water or quicksand: arms up flailing, head tilted up (a loop; the watch lowers him into the surface) |
| `fall` | s | 3 | 120 | falling down a pit: arms and legs spread, surprised face (a loop; the watch shrinks and fades him) |
| `turn` | — | 12 | — | the shop turntable: the `idle` frame 00 pose, yaw 0, 30, 60 ... 330°, **`zoom=2.0`** (twice the size), named `tommy_turn_<nn>` |

### Layers: caps, back items, hand items

The shop sells caps, back items and hand items that show in the game. Each is a
**separate sprite layer for every body frame** (same names with the layer's
prefix: `cap_beanie_hop_e_03`), rendered with the **body as hold-out**
(`holdout=body_objects`), so it only shows where it is not behind Tommy. Layers
use `light` + `id` + `z` (no shadow; the body's shadow is enough) and their own
ids (below), and must follow the frame's pose exactly (parent them to the head
or the hand/back bone). Also render each layer for the 12 `turn` frames.

| layer | styles | ids |
| --- | --- | --- |
| `cap_` | `cap` (a baseball cap, bill forward: **the default**), `back` (a baseball cap worn backwards), `beanie` (a knit beanie with a pompom), `bucket` (a bucket hat), `propeller` (a beanie with a little propeller on top), `crown` (a cartoon golden crown, the most expensive) | 1 main, 2 secondary (bill, brim, pompom, band), 3 detail (the button on top, the propeller blades, the crown's gems), 4 dark (inside of the cap/bill underside) |
| `back_` | `backpack` (a school backpack), `cape` (a short hero cape to the knees, it swings with the pose), `tank` (a ghost-hunter style backpack with a hose to a nozzle at the hip: a generic sci-fi pack, no logo), `wings` (little cartoon bat wings) | 1 main, 2 secondary (straps, lining, trim), 3 metal/detail, 4 glow (lights on the tank: emissive) |
| `hand_` | `flashlight` (in the right hand, pointing forward; its lens glows), `torch` (a wooden torch with a small flame: emissive), `balloon` (a round balloon on a string, floating ~0.5 m above the hand), `bucket` (a trick-or-treat pumpkin bucket with a handle) | 1 main, 2 secondary, 3 metal/detail, 4 glow (the lens, the flame) |

The hand item is in the **right hand**; in `push` and `use` he still holds it
(tucked against the body).

### Pets (a follower, decorative)

A pet hops one cell behind Tommy. Three: `pet_dog` (a small round puppy),
`pet_cat` (a black kitten with a big head), `pet_bat` (a baby bat that flies
~0.6 m above the ground). Each ~0.40 m tall, chibi.

| anim | dirs | frames | ms |
| --- | --- | --- | --- |
| `idle` | n e s w | 2 | 300 |
| `hop` | n e s w | 4 | 45 |
| `turn` | — | 12 | `zoom=2.0` |

Ids: 1 fur/body A, 2 fur/body B (belly, ears inside, wing membrane), 3 dark
(eyes, nose), 4 white, 5 collar, 6 collar tag.

---

## 5. Monsters (`monsters.py` → `assets/monsters/`)

All `light` + `id` + `z` + `shadow`, neutral light, chibi, glowing eyes.
The watch varies their clothes per individual through the palette.

Shared ids for monsters: 1 skin/fur/wrap A, 2 skin/fur/wrap B (shading
variety, belly, darker wraps), 3 dark details (mouth, nostrils, sockets),
4 white (teeth, fangs, eye whites), **5 eye glow (emissive, the watch keeps it
bright)**, 6 hair, 7 clothes A, 8 clothes B, 9 clothes C, 10 shoes/boots,
11 accessory A, 12 accessory B, 13-15 spare. Say in meta which ids each
monster uses.

| monster | size | look | anims (dirs n e s w unless said) |
| --- | --- | --- | --- |
| `zombie` | 1.10 m | a shambling townsperson: grey-green skin, messy hair, a torn shirt (7) with a tie or a stripe (8), trousers (9), one shoe missing; arms forward; a patch or stitches instead of wounds | `walk` 6 @ 100 ms (one cell per cycle, a shuffle), `idle` 2 @ 400, `notice` 2 @ 150 (arms up, head snaps towards the camera direction it faces), `lunge` 3 @ 80 (a quick lurch forward) |
| `vampire` | 1.15 m | a dapper chibi vampire: slicked black hair with a widow's peak, pale lilac skin, a high-collar cape (7 outer, 8 red inner), a waistcoat (9), fangs (4), red glowing eyes; he floats a little | `glide` 6 @ 90 (hovering ~0.1 m, cape swaying), `idle` 2 @ 400, `transform` 5 @ 70 (**s only**: wraps in the cape and shrinks into a puff; the watch swaps to the bat) |
| `bat` | 0.35 m wide | the vampire's bat form (and the swarm bats): round body, big ears, wings 0.6 m span, red eyes; flies at ~0.8 m (**anchor at the ground below it**) | `fly` 4 @ 60 |
| `mummy` | 1.10 m | wrapped in bandages (1 light, 2 darker overlapping wraps), one eye glowing through the wraps, a trailing bandage end, stiff arms forward | `walk` 6 @ 110, `idle` 2 @ 400, `push` 4 @ 120 (pushing a boulder in front of him) |
| `werewolf` | 1.20 m, hunched | a chibi wolf-man: grey-brown fur (1), lighter muzzle/belly (2), pointed ears, a big snout with teeth (4), torn shorts (7), claws | `idle` 2 @ 400 (sniffing), `howl` 5 @ 120 (head up to the moon, the warning before he charges), `run` 6 @ 50 (on all fours, fast), `stun` 4 @ 150 (sitting, dizzy, after hitting a wall) |

**Minions**, one per zone, smaller and faster than the zone's monster, each
with a pattern of its own. Same shared ids.

| minion | zone | size | look | anims (n e s w) |
| --- | --- | --- | --- | --- |
| `zombiedog` | city | 0.50 m | a floppy zombie mutt: patchy grey-green fur (1, 2), one ear up and one down, a tongue out (4 white teeth, 3 dark mouth), glowing eyes, a collar (11) with a tag (12); goofy, not rabid | `idle` 2 @ 400, `run` 4 @ 50 (a bounding gallop: it dashes along a lane) |
| `armor` | castle | 1.20 m | a haunted suit of armour: steel (1, 2 darker plates), eyes glowing through the visor slit (5), a plume (11), a tabard (12); **it must look exactly like the castle's static `armor` prop** (the player should not know which ones walk) | `idle` 2 @ 400 (the standing pose of the prop), `walk` 6 @ 110 (clanking stiffly) |
| `scarab` | desert | 0.40 m long | a beetle with an iridescent shell (1 shell, 2 shell stripes), dark legs (3), glowing eyes (5); they cross in lines like traffic | `crawl` 4 @ 40 |
| `crow` | forest | 0.40 m | a scruffy crow: feathers (1, 2 lighter tips), dark beak and legs (3), glowing eyes (5) | `perch` 2 @ 300 (on the ground), `fly` 4 @ 60 (at 1.0 m, **anchor on the ground below it**), `dive` 3 @ 60 (swooping from 1.0 m down to 0.3 m) |

**Bosses** (the fourth level of each zone). Same ids, bigger, 2 x 2 cells for
`brute`, `pharaoh` and `alpha`: their **anchor is the centre of the 2 x 2
block**, i.e. build them centred on (1, 1, 0) and render with anchor
`V((1, 1, 0))` (say `footprint: [2, 2]` in extra).

| boss | size | look | anims |
| --- | --- | --- | --- |
| `brute` | 2.0 m, 2x2 | a huge zombie in a torn suit and a crooked construction helmet (11), tiny head on a massive body, fists like hams | `walk` 6 @ 140, `stomp` 6 @ 100 (lifts both fists and slams the ground: the watch adds the shockwave) |
| `count` | 1.60 m, 1x1 (a wide cape) | the vampire lord: taller, a big collar, a medallion (11), a cape with a scalloped hem | `glide` 6 @ 90, `cast` 6 @ 90 (spreads the cape, eyes flare: he calls a bat swarm) |
| `pharaoh` | 1.90 m, 2x2 | a giant mummy king with a golden striped headdress and mask (11 gold, 12 blue stripes), a crook in one hand | `walk` 6 @ 130, `whip` 6 @ 80 (lashes a long bandage forward two cells) |
| `alpha` | 2.0 m, 2x2 | the pack leader: bigger, silver-grey fur, a scar-free proud face, glowing yellow eyes, torn vest | `run` 6 @ 60, `howl` 5 @ 120, `stun` 4 @ 150 |

---

## 6. The zones: tiles, props and the zone's moving things

One script per zone: `city.py`, `castle.py`, `desert.py`, `forest.py`, writing
to `assets/tiles_<zone>/`, all under `C.reset('<zone>')`. Names are prefixed
with the zone: `city_blk_asphalt_v0`.

### 6.1 Blocks (the ground itself)

A block fills one cell and **one floor**: x, y in [0, 1], **z from −FLOOR_M to
0** (its top at the anchor's height). The watch stacks them to build terrain
up to floor 3, and puts one or two more under the ground's edges.

- `<zone>_blk_<type>_v<k>`: a **top** block, the surface you walk on, with the
  upper part of its sides (grass lip, kerb, carpet edge). **Three variants
  v0..v2** of each type (different texture seeds, a pebble, a crack, a tuft) so
  a floor does not look tiled. Features stay **4 px (0.07 m) away from the
  cell's edges**, and the edges of all variants match, so any variant sits next
  to any other.
- `<zone>_fill_<type>`: the block **under** a top block (earth, stone, brick
  courses), one variant is enough, tiling vertically and sideways with itself
  and under its top blocks.
- Passes `('color', 'z')`, `kind='tile'`, samples 64. The sides must be
  opaque; no shadow pass (blocks get their shading from the watch's ambient
  occlusion at the base of walls).
- Walls of buildings are just stacked blocks of a wall type: give the wall
  types a **window variant** (`_win`, a window on the front AND right faces,
  lit warm or boarded) as a fourth variant.

### 6.2 Surfaces

`<zone>_surf_<type>_v<k>` (3 variants): a flat 1 x 1 slab whose top is at the
anchor, to sit **0.18 m below** the ground of the cells around it (water,
sewer water, quicksand). Opaque, `('color', 'z')`, `kind='surf'`. The watch
animates a shimmer over them; keep them calm and mid-dark with a clear colour
difference from any ground. Plus `<zone>_bridge_x` / `_bridge_y` (a walkable
plank or grate deck at z = 0 spanning the cell, rails optional, **x/y = the
direction you walk across it**) where the zone has water.

### 6.3 Props (static, stand on a cell)

`<zone>_<prop>`: `('color', 'z', 'shadow')`, `shadow_z=0.0`, `kind='prop'`;
lit props also `'glow'` (with a `size=` big enough for the pool). Built on
cell (0, 0) floor 0, **inside the cell's footprint** (x, y within 0.05..0.95)
unless it is a multi-cell prop: then build it over cells (0..w−1, 0..d−1),
keep the anchor `C.cell(0, 0, 0)` and put `footprint: [w, d]` in extra.
Things with a direction come in `_x` and `_y` versions (a car along X and
along Y). Put `height_m` in extra. Heights: most props 0.4-2.5 m; nothing
taller than 3.2 m (the screen is short).

### 6.4 The zone's moving things

Rendered the same way (`color` + `z`, + `shadow`), anchored at the cell they
occupy; `kind='dyn'`. The watch moves them. Listed per zone below.

### 6.5 The four zones

**city — Zombie Town (grey).** Blocks: `asphalt` (dark grey road),
`asphalt_line` (+ a dashed white lane line running **along X** through the
cell's middle), `crosswalk` (white bars across, running along Y), `sidewalk`
(light grey pavers), `concrete`, `grass` (dead, patchy grey-green lawn),
`brick` (red-grey brick building wall, `_win` = a window lit sodium orange or
boarded), `roof` (gravel rooftop with a low parapet lip), `scrap` (a junkyard
block of crushed metal). Fills: `earth`, `brick`, `concrete`. Surfaces:
`sewer` (murky grey-green water). Bridges: `grate` (a steel grate deck, `_x`,
`_y`). Props: `car_x`, `car_y` (a wrecked, rusty 70s sedan, flat tyre,
**footprint 2x1 / 1x2**), `lamp` (a street lamp, bent, lit sodium orange:
glow), `hydrant`, `trashcan`, `dumpster`, `cone`, `barricade_x`/`_y` (a
striped road barrier), `busstop` (a bus stop sign and bench), `mailbox`,
`deadtree`, `bench_x`/`_y`, `phonebooth`, `tires` (a stack of tyres),
`newsbox`, `sign_x` (a bent street sign, no text), `fence_x`/`_y` (a broken
chain-link fence section). Moving: `runcar_e_<nn>` / `runcar_w_<nn>` (a
runaway old car rolling along X, **recoloured**: `light` + `id` + `z` +
`shadow` under neutral light, ids 1 paint A, 2 paint B, 3 glass, 4 chrome,
5 black trim, 6 tyres, 7 lights, 8 rust; 4 frames of wheel spin @ 60 ms,
footprint 2x1), `vent_<nn>` (a manhole cover on the ground and a burst of
steam: 00 idle cover, 01-05 the burst rising, 06-07 fading; the steam
semi-transparent), `gate_<nn>` (**the exit**: a chain-link gate on posts with
a padlock, closed 00, opening 01-05, open 06 with a soft glow), `crate` (a
wooden crate, pushable, exactly one floor tall, footprint 1x1).

**castle — Vampire Castle (violet).** Blocks: `flagstone` (irregular lilac
stone flags), `carpet` (a crimson-purple carpet with a gold border, the
border along the cell's X edges), `stone` (castle wall ashlar, `_win` = a
tall stained-glass window glowing violet-blue on both visible faces),
`battlement` (a wall top with merlons), `woodfloor` (dark planks), `rug`
(a round ornate rug on flagstone). Fills: `stone`, `rock`. Surfaces: `moat`
(dark violet water). Bridges: `wood` (`_x`, `_y`: a drawbridge-style plank
deck with chains). Props: `candelabra` (lit, glow), `armor` (a suit of armour
on a stand), `pillar` (a gothic column, 3 m), `coffin` (a closed coffin on the
floor, decorative), `gargoyle` (on a plinth), `banner` (a hanging banner on a
pole stand), `throne`, `bookshelf_x`/`_y`, `ironfence_x`/`_y`, `roses` (a
violet rose bush), `clock` (a grandfather clock), `chandelier_stand` (a
standing candle tree, lit). Moving: `platform_<nn>` (a floating stone slab
1x1, one floor thick, runes glowing faintly; 2 frames of glow pulse),
`spikes_<nn>` (a floor trap flush with the ground: 00 holes, 01 tips, 02 fully
out — steel spikes, shiny, a warning red rim), `gate_<nn>` (**the exit**: a
portcullis in a stone arch, closed 00, rising 01-05, open 06 with a violet
glow), `crate` (a heavy stone block with a carved rune, pushable),
`coffin_open_<nn>` (the boss's coffin: 00 closed, 01-04 the lid sliding open,
with a red glow inside).

**desert — Mummy Desert (brown).** Blocks: `sand` (fine dunes ripples),
`sandstone` (large square sandstone floor tiles), `brick` (pyramid limestone
blocks, `_win` = a carved hieroglyph panel instead of a window: abstract
birds, eyes, suns — no real text), `cracked` (cracked sandstone), `oasis`
(green grass by the water), `gold` (a gilded temple floor). Fills: `sand`,
`sandstone`, `brick`. Surfaces: `water` (turquoise oasis water), `quicksand`
(swirly lighter sand, darker in the middle: **it must read as dangerous**).
Bridges: `reed` (`_x`, `_y`: a reed mat bridge). Props: `cactus`,
`cactus_small`, `palm`, `obelisk` (3 m), `statue` (a seated pharaoh statue),
`urn`, `sphinx` (a sphinx head and paws, footprint 2x2), `brazier` (a bronze
bowl on a tripod with fire, lit: glow), `column` (an Egyptian lotus column,
3 m), `scarab` (a scarab statue), `rock`, `tent` (a striped desert tent,
footprint 2x2), `camel` (a cute sitting camel, footprint 2x1). Moving:
`boulder_x_<nn>` and `boulder_y_<nn>` (a carved round stone ball ~0.9 m
rolling along X / along Y: 8 frames of roll @ 50 ms each), `dartwall` (a `brick` block
variant with three dark holes on its front face and on its right face: a
tile, `kind='tile'`), `dart_x` / `dart_y` (a small dart flying along X / Y at
0.5 m height, a streak behind it), `gate_<nn>` (**the exit**: a stone door in
a temple frame with a sun disk, sliding up, closed 00, 01-05, open 06 with a
golden glow), `crate` (a sandstone block, pushable).

**forest — Werewolf Woods (green).** Blocks: `grass` (lush, moonlit, a few
flowers in some variants), `path` (a dirt trail), `moss` (mossy stone slabs),
`rock` (a grey rock cliff block), `plank` (a wooden deck), `mud`. Fills:
`earth`, `rock`. Surfaces: `river` (dark teal water with a lighter flow
streak along X). Bridges: `plank` (`_x`, `_y`). Props: `pine` (3 m), `oak`
(a round big-canopy tree 3 m, footprint 1x1 but the canopy may overhang
0.3 m), `bush`, `mushroom` (a giant glowing mushroom, lit teal: glow),
`stump`, `log_x`/`_y` (a fallen log, footprint 2x1 / 1x2), `rock`,
`cabin` (a woodcutter's cabin, windows lit warm, footprint 2x2, glow),
`fence_x`/`_y` (a wooden rail fence), `lantern` (a lantern on a post, lit:
glow), `flowers`, `fern`, `shrine` (a small stone moon shrine), `mill` (a
water-mill wheel housing, footprint 2x2). Moving: `logfloat_<part>` (logs
floating on the river, lying along X: `logfloat_w`, `logfloat_m`,
`logfloat_e` = the west end, a middle piece, the east end, each exactly one
cell long so the watch builds logs of any length; their top at the anchor,
floating at z = −0.10), `lily_<nn>` (a big lily pad you can stand on: 00
floating, 01-03 sinking under), `beartrap_<nn>` (00 open, 01 snapped — a
cartoon trap, not bloody), `gate_<nn>` (**the exit**: a gate of woven
branches with a silver moon emblem, closed 00, opening 01-05, open 06 with a
soft silver glow), `crate` (a wooden crate, pushable).

Put a `zone.png` contact sheet next to each zone's sprites (every sprite on a
dark background with its name) and a `compose` preview (section 11).

---

## 7. Common objects and effects (`objects.py` → `assets/objects/`)

`color` + `z` (+ `shadow`), neutral light unless said, `kind='dyn'`:

| name | frames | what |
| --- | --- | --- |
| `key_<nn>` | 8 @ 80 | a big golden key floating at 0.45 m, spinning a full turn around Z, a soft glow; **anchor on the ground below** |
| `coin_<nn>` | 8 @ 70 | a gold coin with a star, floating at 0.35 m, spinning |
| `heart_<nn>` | 8 @ 80 | an extra life: a red glossy heart, floating, spinning |
| `hourglass_<nn>` | 8 @ 80 | extra time: a small hourglass with blue sand, floating, spinning |
| `chest_<nn>` | 4 @ 80 | a treasure chest: 00 closed, 01-02 opening, 03 open with a golden glow inside |
| `lever_<nn>` | 3 | a lever on a small stone base: 00 left, 01 middle, 02 right (it faces −Y: you pull it from the front) |
| `lantern_off`, `lantern_on_<nn>` | 1 + 3 @ 120 | **the checkpoint**: a jack-o'-lantern on a short post; unlit, then lit with a flickering flame inside (glow on the ground) |
| `fx_dust_<nn>` | 5 @ 40 | a landing puff of dust, flat, grey-beige (semi-transparent alpha) |
| `fx_splash_<nn>` | 6 @ 50 | a water splash (white-teal) |
| `fx_poof_<nn>` | 6 @ 60 | a cartoon smoke poof (lilac-white), when Tommy is caught |
| `fx_sparkle_<nn>` | 6 @ 50 | golden sparkles bursting up, when a key is picked |
| `fx_bubbles_<nn>` | 4 @ 120 | bubbles on a surface (sinking) |

Effects need no `z` realism: give them a flat depth (their own geometry is
enough).

---

## 8. Region ids — the rest

Pets: section 4. Monsters: section 5. City runaway car: section 6.5.

---

## 9. UI art (`ui.py` → `assets/ui/`) — free camera

Not composited with the game, so these may use their own camera (perspective
is fine) and the `map` light. Final colour, RGBA.

- `map`: **the world map**, 368 x 900 px (scrolls vertically; zone 1 at the
  bottom). A diorama in the game's style: the four zones as regions of one
  small continent (the grey ruined town at the bottom, the violet castle on a
  hill, the brown desert with its pyramid, the green woods), linked by a
  winding path; up top, two **locked regions under a thick magic fog** (a
  swamp with a hint of a witch's hut, a graveyard hill) so they read as "coming
  soon". Put **4 level spots per zone** along the path (small round stone
  pads) and record their pixel centres in meta (`extra={'levels': {'city':
  [[x, y], ...], ...}}`), plus a spot for each locked region.
- `logo`: "MONSTER HOP" in chunky 3D letters (Blender's built-in font,
  extruded, bevelled), green-to-purple, with a little bat and a key; ~340 x
  130 px, transparent background.
- `emblem_<zone>`: 96 x 96 medallions for the zone picker: `city` (a zombie
  hand), `castle` (a bat), `desert` (a pyramid with an eye), `forest` (a
  wolf head and a moon), `swamp` (a witch hat), `graveyard` (a cartoon skull —
  fine here, it is the future zone's sign), `lock` (a padlock).

---

## 10. Deliverables and budget

**Two phases.** First a **style sample**: a small subset of your assets, final
quality, in a `compose` scene at 368 x 448 (plus a 2x enlargement), so the look
is approved before anything is rendered in bulk. Stop after the sample and
report; the full run comes after the answer. What each sample holds is in the
brief you were given.

Then, for each script:

1. The script in `apps/monsterhop/tools/blender/`, re-runnable headless, the
   only source of truth (no hand-made .blend). `--only name1,name2` should
   re-render a subset (so a fix does not cost a full run).
2. The renders in `apps/monsterhop/assets/<dir>/` with `meta.json`.
3. A contact sheet (`<dir>/_sheet.png` or one per group) and at least one
   `compose` scene (`<dir>/_scene.json` + `_scene.png`) showing the assets in a
   game situation at 368 x 448.
4. A short report: what was rendered (counts), sizes (largest sprite), render
   time, anything that deviates from this spec and why.

Sizes to respect: a character frame fits in **96 x 112** px (bosses 192 x
200); a block is ~88 x 86; props within 200 x 260 (multi-cell props up to 260
x 300). Every pixel is PSRAM on the watch. **Do not render at higher
resolution** than the projection (only `turn` uses zoom 2).

Samples: 48 for characters, 64 for tiles and props; iterate at `--samples 12`.
Several scripts render at once on the same GPU: be patient, render subsets
while you iterate.

---

## 11. A compose scene to start from

```json
{"assets": ["tiles_forest", "chars"],
 "size": [368, 448], "look_at": [4, 5, 0], "bg": [12, 20, 16],
 "palettes": {"tommy": {"1": [245, 190, 150], "2": [90, 55, 30], "3": [25, 20, 20],
     "4": [255, 255, 255], "5": [40, 120, 245], "6": [250, 250, 250], "7": [250, 200, 40],
     "8": [50, 60, 110], "9": [230, 40, 40], "10": [245, 245, 245], "11": [255, 255, 255],
     "12": [250, 150, 150]}},
 "grid": {"w": 9, "h": 12, "floor": "forest_blk_grass_v0", "fill": "forest_fill_earth",
          "heights": ["000000000", "000000000", "000011000", "000012000", "000000000",
                      "000000000", "000000000", "000000000", "000000330", "000000330",
                      "000000000", "000000000"]},
 "items": [["tommy_idle_s_00", 3, 2, 0, {"palette": "tommy"}],
           ["forest_pine", 6, 6, 0]]}
```

Paths in `assets` are relative to the scene file. A `.` in `heights` is a pit.

---

## 12. Decisions after the style samples (2026-09-22) — they override the above

- **Approved as it is.** Keep the look of your sample for the whole set.
- **Tommy's cap:** the bill must read from all four facings — longer and a
  little lower, still clear of the eyes; the backwards cap shows its bill
  behind. Brows are id 2 (hair colour), not id 3.
- **Skins are palette only**, and now include odd ones sold in the shop
  (martian green, a translucent ghost, zombie grey-green, pumpkin orange,
  robot silver, glowing lava, a rainbow cycle): keep skin a clean region.
- **Bridges** are named `<zone>_bridge_x` and `<zone>_bridge_y` (x/y = the
  direction you walk across).
- **Floating logs** (`forest_logfloat_*`): their top sits at **z = -0.02**
  (16 cm above the water), not -0.10.
- **The castle's standing armours** are the `armor` minion's `idle` frames
  drawn by the watch (some walk, some never do): no `castle_armor` prop.
- **Castle carpet:** also `castle_blk_carpet_y` (the runner along Y).
- **Album cards:** every monster, minion and boss also renders
  `card_<name>` — its `idle` (or first) pose facing `s`, `zoom=2.0` (bosses
  1.2), `light` + `id` passes, `kind='card'`: the house's sticker album
  shows them, coloured by the watch.
- **Collectibles (objects.py):** `sticker_<nn>` 8 @ 80 — a small floating
  foil card with a sparkle, spinning (one is hidden in each level); and UI
  icons `trophy_gold`, `trophy_silver`, `trophy_bronze`, `medal` (96 x 96).
- **The map (ui.py):** Tommy's **house** at the very bottom, before the
  zombie town, as the first spot of the path (`levels` meta gets a
  `house` point); and `house` — a 368 x 200 picture of the house for the
  hub screen (wardrobe, shop, album, trophies, stats, play with a friend).
