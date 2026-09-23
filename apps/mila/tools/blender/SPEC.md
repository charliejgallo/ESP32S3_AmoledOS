# Mila — 3D assets rendered in Blender

Mila is a Sokoban for the watch (ESP32-S3, 368 x 448 AMOLED, RGB565,
portrait). **Mila**, a sweet black kitten with big amber-yellow eyes, pushes
things back to their place around the house, one cell at a time, in five
worlds (living room, kitchen, garden, attic, roofs at night). Between levels
she lives in her **casita**, where the player pets her and plays with the toys
bought in the shop, which also sells **hats**, **collars** and **scarves**.
Read `../../DESIGN.md` for the game.

**The style is approved**: `../../assets/_sample/board/index.html` and the
code that made it, `sample.py` next to this file (Mila's model, the living
room kit, the casita). Start from that code; improve it, do not reinvent it.

**Tone: cosy, sweet, toy-like.** Soft rounded shapes, warm pastel colours with
a few saturated accents, clean materials (no grime), gentle light. Chibi
proportions. Nothing scary, nothing sharp.

Everything is **rendered in the Mac** and only **placed** by the watch: the
watch never draws a triangle. It composites pre-rendered sprites with a
**depth buffer**, so each sprite carries a depth pass and hides and is hidden
per pixel, exactly like the 3D scene would.

---

## 1. The shared module — `ml_common.py`

Every script imports `ml_common` (next to this file). It owns **the camera, the
lights, the passes, the files and meta.json**. Do not set up cameras or suns
of your own (the UI art of section 9 is the only exception), and **do not edit
ml_common.py or ../compose.py**: several people use them at once. If you need
something they do not do, write it in your own script, or say so in your
report.

```python
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ml_common as C
a = C.args()                      # --out DIR [--only a,b] [--samples N] [--cpu]
C.reset('living')                 # empty scene, game camera, the world's light
C.mat('wood', base=(0.78, 0.52, 0.30), rough=0.6)           # register materials
C.mat('scarf', base=(0.9, 0.2, 0.3), id=1)                  # id: region (layers)
ob = C.box('wall', 0, 0, 0, 1, 1, C.FLOOR_M, 'wood', bevel=0.01)   # or any bpy geometry + C.assign(ob, key)
C.render_sprite(a.out, 'living_wall', [ob], C.cell(0, 0, 0),
                passes=('color', 'z', 'shadow'), shadow_z=0.0, kind='prop')
C.save_meta(a.out)                # writes/merges <out>/meta.json
```

Run: `/Applications/Blender.app/Contents/MacOS/Blender -b -P <script>.py -- --out ../../assets/<dir>`.
Blender **3.3.1**, Cycles on Metal (a small sprite renders in about a second).
`test_common.py` is a two-sprite example. **Materials go through `C.mat()` +
`C.assign()`** (or `build=` for node materials, see ml_common's docstring):
that is how every pass gets the right variant. sample.py's materials are
plain Blender materials; port them to `C.mat(..., build=...)`.

### The projection (fixed)

Orthographic, **looking straight along +Y** (no yaw), elevation 48.59°, **72 px
per metre**, chosen so that the grid lands on whole pixels:

| in the world | on the screen (x right, y down) |
| --- | --- |
| +1 m along **X** (one cell to the right) | **(+72, 0) px** |
| +1 m along **Y** (one cell up the screen, away from the camera) | **(0, −54) px** |
| **one floor** up = `C.FLOOR_M` = **0.50395 m** (the height of a wall) | **(0, −24) px** |

Metres, **Z up**. A **cell** is 1 x 1 m; cell (x, y) spans [x, x+1] x [y, y+1].
You see the **top** and the **front (−Y) face** of a block, never its sides.
The sun comes from the front-left (`C.SUN_ELEV` 55°, `C.SUN_AZ` 215°): tops
brightest, fronts lit, shadows fall back and to the right. The same sun in
every render.

`C.cell(x, y, floor)` is the world point at the centre of cell (x, y) on top
of that floor. **Build every asset at cell (0, 0), floor 0**: its anchor is
`C.cell(0, 0, 0)` = (0.5, 0.5, 0) unless said otherwise. The watch places the
anchor anywhere; the sprite moves with it, pixel-exact.

Two other scales, same camera: the **casita** is drawn at `zoom=C.CASITA_ZOOM`
(1.5: 108 px per metre) and the **shop's turntable** at `zoom=C.SHOP_ZOOM`
(3.0). Pass `zoom=` to `render_sprite`; everything else is the same.

### Passes and files

`C.render_sprite(out, name, objs, anchor, passes=...)` renders one sprite and
writes (all PNG, same size, fitted to the object unless `size=` is given):

| pass | file | what |
| --- | --- | --- |
| `color` | `name.png` RGBA | final colour under the world light, antialiased alpha, denoised |
| `light` | `name.png` RGBA | the same shading on **neutral grey 0.8** under `reset('neutral')`: the watch colours `palette[id] * light / 196` |
| `id` | `name_id.png` L | region id × 16, no antialiasing, covers every pixel the colour/light pass covers |
| `z` | `name_z.png` L | depth along the view direction relative to the anchor: 128 = the anchor's depth, 1 step = 1/32 m, **smaller = nearer**, 255 = nothing |
| `shadow` | `name_sh.png` L | how much the object darkens the ground plane z = `shadow_z` (0 = none, 255 = black), nothing of the object itself |
| `glow` | `name_gl.png` RGB | the light the object's lamps/emitters throw on a white ground at `shadow_z`, alone (black = none): the watch adds it around. Give `size=` large enough for the pool |

`meta.json` gets, per sprite: `w, h`, `ax, ay` (the pixel corner where the
anchor lands), `anchor`, `kind`, `files`, plus whatever you pass in
`extra={...}` (`footprint`, `frames_ms`, `nodes`...). The packer crops the
transparent margins.

**Mila's body** is final colour: `color` + `z` + `shadow` under
`C.reset('neutral')` (she is always black). **Her layers** (hats, collars,
scarves) are recoloured by the watch: `light` + `id` + `z`. **Tiles and props**
are final colour under their world light: `color` + `z` (+ `shadow`, + `glow`
for lit things).

### Seeing it in the game: `../compose.py`

`python3 ../compose.py scene.json out.png` puts your sprites together the way
the watch will (depth-tested, anchors on the grid, palettes for the
recoloured ones). Its docstring shows the scene format. **Use it**: build a
small scene with your assets and look at it at 368 x 448 before calling
anything done.

---

## 2. Readability rules (a 1.8" screen)

- A cell is 72 x 54 px; Mila is ~58 px tall. Bold silhouettes, big shapes,
  strong value contrast; nothing thinner than ~2 px (3 cm) that matters.
- **Mila is black on a black screen**: keep sample.py's cool rim sheen (the
  fur's facing-ratio emission) and never put her on very dark floors. Floors
  are mid-to-light; wall **tops** are darker than the floor so the maze reads.
- Floors read as flat and walkable; walls and furniture as solid and **filling
  their cell** (a blocked cell must look blocked: footprint ≥ 0.85 m).
- **Targets** must be unmistakable at a glance, even with Mila standing on
  them and in the whole-level view (half scale): a strong shape and colour
  that differs from the floor in every world.
- Pushable objects: the most saturated things in the level, round-ish and
  friendly, footprint ~0.7 m, height 0.45-0.7 m.
- Tall furniture hides what is behind it; keep blockers ≤ 1.0 m tall (the
  watch draws Mila's silhouette through, but less is better).
- Top faces get a slight bevel so the grid reads faintly: players count cells.

---

## 3. Directions and animation conventions

| dir | faces | yaw of the model |
| --- | --- | --- |
| `s` | −Y, towards the camera (we see her face) | 0° |
| `e` | +X, screen right | **+62°** (turned a bit towards the camera, as in the sample) |
| `n` | +Y, away from the camera (we see her back) | 180° |
| `w` | −X, screen left | **−62°** |

**Model Mila facing −Y** and rotate her about Z by the yaw. `e` and `w` look
alike but the light comes from the left and some hats are asymmetric:
**render all four**, never mirror.

**Animations are in place.** Every frame is rendered with Mila's root at the
anchor `C.cell(0, 0, 0)` (the ground point under her middle). The watch moves
the anchor from cell to cell; the frames carry the pose only. Frame names:
`<who>_<anim>_<dir>_<nn>` (nn from 00), e.g. `mila_walk_e_03`; record the
playback in `extra={'anim': 'walk', 'dir': 'e', 'frame': 3, 'frames': 6,
'ms': 40}`.

---

## 4. Mila (`mila.py` → `assets/mila/`)

Start from `mila()` in sample.py (fused skin, rim sheen, amber eyes with a
slit pupil and a glint, pink inner ears and nose, blush, whiskers). About
**0.62 m** tall at game scale with the ears; the head is ~55 % of her. Turn
the primitives into a **rig** (the skin can be re-fused per frame: it is
cheap) so poses are clean. She needs a small **mouth** for `meow` (a dark
"w" line, open for the meow). Blink = eyelids of fur colour over the eyes.

### Game scale (zoom 1, `C.reset('neutral')`)

| anim | dirs | frames | ms | pose |
| --- | --- | --- | --- | --- |
| `idle` | n e s w | 4 | 250 | breathing, tail sways; frame 2 blinks |
| `walk` | n e s w | 6 | 40 | a trot cycle covering one cell (the watch slides her 1 m over the 6 frames) |
| `push` | n e s w | 6 | 50 | head-butting the thing in front (as in the sample: head low and forward, hips up, back legs driving), a loop; the thing's near face is 0.15 m in front of her nose |
| `win` | s | 8 | 90 | level done: a happy hop, lands, sits, tail up, eyes closed smiling |
| `yawn` | s | 8 | 120 | idle for a while: stretches, a big yawn (mouth open), sits back |

Every frame: `passes=('color', 'z', 'shadow')`, `shadow_z=0.0`,
`bounce_ground=0.0`, `kind='char'`, samples 64.

### Casita scale (`zoom=C.CASITA_ZOOM`, same model, same light)

| anim | dirs | frames | ms | pose |
| --- | --- | --- | --- | --- |
| `c_walk` | n e s w | 6 | 70 | calm walk |
| `c_run` | e w | 6 | 45 | a gallop (chasing) |
| `c_sit` | n e s w | 4 | 300 | sitting, tail swish; frame 2 blinks |
| `c_sleep` | s | 4 | 450 | curled up, breathing; fits a circle of radius 0.34 m (her bed) |
| `c_belly` | s | 6 | 140 | rolled on her back, paws wiggling (catnip, petting) |
| `c_pounce` | e w | 8 | 70 | crouch, butt wiggle, leap forward ~0.6 m (in place: the watch moves her), land |
| `c_bat` | e w | 6 | 70 | swats with a front paw, sitting |
| `c_jump` | e w | 6 | 60 | a hop straight up swiping (the feather) |
| `c_scratch` | n | 6 | 90 | standing on her hind legs, front paws on a post 0.25 m in front at z 0.35-0.6, scratching |
| `c_eat` | n | 4 | 200 | head down into a bowl 0.2 m in front |
| `c_groom` | s | 8 | 120 | licks a paw, wipes her face |
| `c_meow` | s | 4 | 110 | a meow, mouth open |
| `c_purr` | s | 6 | 150 | eyes closed, blissful, leaning into a pet |
| `c_peek` | s | 4 | 250 | sitting low (a box or the tunnel's end hides her below z 0.3): head and front paws up, looking around |
| `c_lie` | s | 4 | 300 | a loaf on a hammock (her belly at z 0), tail hanging over the edge |

### Shop scale (`zoom=C.SHOP_ZOOM`)

`turn`: the `c_sit` frame 00 pose, yaw 0, 30, 60 ... 330°, named
`mila_turn_<nn>` (12 frames).

### Layers: hats and neck items

Each item is a **separate sprite for every Mila frame** above (game, casita,
turn): same names with the item's prefix, `hat_party_walk_e_03`,
`neck_bell_c_sleep_s_02`, `hat_crown_turn_05`. Render them with **Mila's body
as hold-out** (`holdout=body_objects`) so a layer only shows where it is not
behind her, following the pose exactly (parent them to the head / neck).
Layers use `light` + `id` + `z` under `C.set_light('neutral')`, no shadow.

| item | what | ids |
| --- | --- | --- |
| `hat_bow` | a bow on her right ear (the sample's) | 1 bow, 2 knot |
| `hat_party` | a party cone with a pompom (sample) | 1 cone, 2 pompom and rim, 3 dots |
| `hat_crown` | a little golden crown with a gem (sample) | 1 gold, 3 gem |
| `hat_beret` | a French beret, tilted, with a stalk | 1 felt, 2 stalk |
| `hat_beanie` | a knit beanie with ear holes and a pompom | 1 knit, 2 cuff and pompom |
| `hat_flower` | a big flower behind her left ear | 1 petals, 2 centre, 3 leaf |
| `hat_bunny` | a headband with bunny ears | 1 ears outside + band, 2 ears inside |
| `hat_witch` | a tiny witch hat with a band and buckle | 1 hat, 2 band, 3 buckle |
| `neck_bell` | a collar with a round bell | 1 collar, 3 bell |
| `neck_fish` | a collar with a fish-shaped tag | 1 collar, 3 tag |
| `neck_pearls` | a string of pearls | 1 pearls |
| `neck_bandana` | a triangular scarf, knot at the back, point on her chest | 1 cloth |
| `neck_dots` | the same scarf with polka dots | 1 cloth, 2 dots |
| `neck_bow` | a big bow tie under her chin | 1 bow, 2 knot |

Ids 1-3 above; id 4 is always **dark** (insides, undersides). Write
`assets/mila/palettes.json` with a default palette per item
(`{"hat_party": {"1": [90,215,165], "2": [250,250,250], "3": [255,120,160]}, ...}`).
Scarves and the bow tie **must show from the front** (on her chest, below the
chin): that is why they exist. Collars may hide in front; their bell/tag
must show.

---

## 5. The worlds (`worlds_<a|b>.py` → `assets/<world>/`)

Five kits, one per world, **final colour under the world's light**
(`C.reset('<world>')`). Names start with the world id.

| world | floor | walls (1 floor high) | blockers (furniture) | object (4 variants) | target | mechanic pieces |
| --- | --- | --- | --- | --- | --- | --- |
| `living` | warm wooden planks (sample) | teal wallpaper front, white wainscot, dark walnut top (sample) | plant pot, bookcase, side table with lamp, armchair, record player on a cabinet; 2-cell sofa (sample) | yarn balls (pink, teal, yellow, lilac) with a loose thread | wicker basket with a rose cushion and a paw print (sample) | — |
| `kitchen` | mint and white checker tiles | pale yellow tiles, a wooden worktop edge on top | fridge (short, ≤ 1 m), stove, cabinet with a fruit bowl, stool, trash can; 2-cell counter with a sink | cookie tins (round, 4 colours with lids) | a round placemat with a fish print | `kitchen_wet_v0..1`: a shiny water puddle on the tiles |
| `garden` | grass with a few stone steps | trimmed hedges (top: leafy dark green) | tree stump, watering can on a crate, gnome, bird bath, bush with flowers; 2-cell bench | terracotta pots with flowers (4 flowers) | a round patch of dark soil ringed with pebbles | `garden_wet_v0` (a puddle), `garden_plate_up`, `garden_plate_down` (a round stone button), `garden_gate_h_00..03` and `garden_gate_v_00..03` (a little wooden gate, closed at 00, open at 03, `h` = in a wall that runs left-right, `v` = up-down) |
| `attic` | old wide boards, warm | stacked trunks and crates, dusty (top: darker wood) | old armchair with a sheet, lamp, dress form, rocking horse, stack of books; 2-cell trunk | cardboard boxes (4 labels/tapes) | a tape cross on the floor, bright red | `attic_flap_h_00..03`, `attic_flap_v_00..03` (a wall cell with a cat flap in it, crossed left-right / up-down; 00 still, 01-03 swinging), `attic_plate_*`, `attic_gate_*` as in the garden (an old iron gate) |
| `roofs` | terracotta roof tiles, night, a little moonlight on the edges | chimneys and parapets (top: brick) | TV antenna base, water tank, pigeon house, skylight (raised), satellite dish; 2-cell chimney | wooden crates (4 stencils) | a glowing moon mark painted on the tiles (with `glow`) | `roofs_hole` (a gap in the roof: the floor level is 1 floor lower, dark inside, the rims visible), `roofs_hole_crate_v0..3` (a crate sunk flush in a hole: it is floor now), `roofs_ball_v0..1` (a rubber ball, 2 colours, and `roofs_ball_roll_v0_00..07` rolling frames), `roofs_wet_v0` (a rain puddle), `roofs_flap_h/v_00..03` (a flap in a chimney wall) |

For every world:

- `<w>_floor_v0..v3` (kind `tile`, `color` + `z`): four variants that tile
  seamlessly in any order.
- `<w>_wall_v0..v1` (kind `prop`, `color` + `z` + `shadow`): one cell, one
  floor high. Walls sit side by side and must join without seams (their
  fronts only show where there is no wall in front of them).
- `<w>_prop_<name>` (1 cell) and `<w>_prop2_<name>` (2 cells along +X, anchor
  at the **left** cell's centre, `extra={'footprint': [2, 1]}`): `color` + `z`
  + `shadow`.
- `<w>_obj_v0..v3`: the pushable things (`color` + `z`), their shadows in a
  **separate** sprite each (`<w>_obj_v0_sh`, `passes=('shadow',)` via the
  `shadow` pass: the watch moves it with the object). If a thing looks
  different on its target (a yarn ball sits in the basket), also
  `<w>_obj_on_v0..v3` (anchored at the target cell).
- `<w>_target` (kind `tile`, flat, `color` + `z`).
- the mechanic pieces in the table, `color` + `z` (+ `shadow` for gates and
  flaps).

## 6. The casita (`casita.py` → `assets/casita/`)

All at `zoom=C.CASITA_ZOOM` under `C.reset('casita')`. The room, in metres:
floor x 0 .. 3.4, y 0 .. 3.0; the back wall at y = 3.0, 2.0 m tall; no side
walls (a diorama on the black screen). Start from sample.py's `do_casita`.

| sprite | what | where (anchor) |
| --- | --- | --- |
| `casita_room` | floor, back wall with wallpaper, a door at x 0.3-1.0 (for the visitor), a window at x 1.9-3.1 (night sky; also `casita_room_day`), a shelf, a picture: one big picture | (0, 0, 0), `color` + `z` |
| `casita_door_00..03` | the door opening | on the wall |
| `casita_rug`, `casita_bed`, `casita_bowls` | always there: rug centred (1.7, 1.35); a donut bed at (0.6, 2.3), inner radius 0.36 m, floor of the bed z 0.08; food + water bowls at (0.45, 0.45), (0.9, 0.4) | their spot |
| `casita_gift` | a small wrapped parcel (the daily gift), + `casita_gift_open_00..03` | by the bed (1.1, 2.2) |

Toys (the shop's third tab), each at its **fixed spot**, `color` + `z` +
`shadow`, plus a **thumbnail** `icon_toy_<name>` (80 x 80, zoom 3, free
framing, transparent) for the shop:

| toy | spot | notes |
| --- | --- | --- |
| `toy_mouse` | free, starts (2.5, 1.8) | grey plush mouse; `toy_mouse_00..03` wobble |
| `toy_feather` | free | a feather on a string, as if hanging from a stick above (the player drags it); `00..03` sway |
| `toy_yarn` | free, (1.1, 0.95) | a yarn ball; `toy_yarn_roll_00..07` |
| `toy_ball` | free, (2.2, 1.0) | a ball with a bell; `toy_ball_roll_00..07` |
| `toy_post` | (3.0, 2.4) | scratching post: carpet base, rope pole, top platform at z 1.1 |
| `toy_box` | (2.9, 0.6) | open cardboard box 0.7 x 0.5, walls 0.4 m: Mila sits inside (`c_peek`) |
| `toy_tunnel` | (1.6, 2.5), along X, 0.9 m long | a crinkly fabric tunnel, openings radius 0.2 |
| `toy_fishbowl` | (0.35, 1.5) | a bowl on a low stool, two fish; `toy_fishbowl_00..07` (the fish swim) |
| `toy_hammock` | under the window, (2.5, 2.75), bed at z 0.55 | a window hammock (fleece, on a frame); Mila lies on it (`c_lie`) |
| `toy_catnip` | (3.15, 1.4) | a catnip plant in a pot |

## 7. The map (`map.py` → `assets/map/`)

A vertical strip of **panels**, each one picture 368 px wide (the height is
yours, 320-440 px), `color` only, the same camera at `zoom=0.45` under
`C.reset('map')`. Every panel is a little diorama of its world (a room of the
house cut open, the garden, the roofs under the moon) with **8 level stones**
on a path of paw prints; the path **enters at the bottom centre (x 184) and
leaves at the top centre**, so panels chain in any order and new worlds can
be added above. Give the stones' pixel centres in
`extra={'nodes': [[x, y], ...], 'entry': [184, h], 'exit': [184, 0]}`.

| panel | what |
| --- | --- |
| `map_home` | the bottom: the outside of Mila's little house (a door, a window with light), the path starting at the door. No stones. |
| `map_living`, `map_kitchen`, `map_garden`, `map_attic`, `map_roofs` | the five worlds, 8 stones each |
| `map_soon` | the top: clouds / fog over the path ("more to come"), no stones |
| `map_stone`, `map_stone_done`, `map_stone_locked` | the level stone (a round paw-print stone ~40 px), drawn by the watch over the nodes |
| `map_mila` | a small Mila marker (sitting, ~44 px) standing on a stone |

## 8. UI art (`ui.py` → `assets/ui/`) — free camera allowed

`color` only, transparent background, any camera and light that flatters:

- `ui_logo` (~300 x 120): the word **Mila** in round friendly letters with cat
  ears on the M and a tail curling off the a, plus a tiny paw.
- `ui_emblem_<world>` (72 x 72): a round badge per world with its object
  (yarn ball, cookie tin, flower pot, cardboard box, crate under a moon), and
  `ui_emblem_soon`.
- icons, 40 x 40, one clear shape each: `icon_coin`, `icon_star`,
  `icon_star_empty`, `icon_paw`, `icon_undo`, `icon_restart`, `icon_home`,
  `icon_shop`, `icon_gear`, `icon_gift`, `icon_lock`, `icon_heart`,
  `icon_play`, `icon_tab_hat`, `icon_tab_neck`, `icon_tab_toy`, `icon_link`.

## 9. Deliverables, budget, and the sample first

- Output dirs: `assets/mila`, `assets/living`, `assets/kitchen`,
  `assets/garden`, `assets/attic`, `assets/roofs`, `assets/casita`,
  `assets/map`, `assets/ui` (each with its meta.json). Scripts in this
  directory. Files starting with `_` (sheets, test scenes) are not packed.
- Memory: every sprite is 4 bytes a pixel on the watch (3 for layers). A
  world kit ≤ 1.2 MB; Mila's game frames ≤ 1.0 MB, her casita frames ≤ 2.5 MB.
  Crop is automatic; keep frames tight (no oversized `size=`).
- **Phase 1, the sample**: render a small subset (listed in your task), put it
  together with `../compose.py` in a 368 x 448 scene at `assets/<dir>/_sample.png`
  (and a 2x nearest-neighbour copy), and **stop there**: report what you made
  and any question. The rest is rendered only after the user approves.
- Phase 2: everything, then a final `_sample.png` / `_sheet.png` and a report
  (names, counts, sizes, render time, anything the engine must know).
