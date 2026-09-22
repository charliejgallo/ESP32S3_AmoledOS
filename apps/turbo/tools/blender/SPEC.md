# Turbo — 3D assets rendered in Blender

Turbo is an arcade racing game for the watch (ESP32-S3, 368x448 AMOLED,
RGB565, portrait). The road is drawn by the watch in **pseudo-3D** (scanline by
scanline, like OutRun / Cruis'n USA); everything standing on or beside the road
(the cars, the traffic, the scenery) is a **sprite rendered in Blender** and
scaled by the watch with its distance. The reference look is Cruis'n USA: a
red wedge supercar seen from behind and above, a highway with concrete
overpasses, three lanes of traffic.

Same pipeline as Golf (`apps/golf/tools/blender/`, read `SPEC.md` and
`golfer.py` there: Cycles on Metal, `film_transparent`, Standard view
transform, EXR -> PNG, denoised shade pass, id pass with no antialiasing):

    /Applications/Blender.app/Contents/MacOS/Blender -b -P cars.py  -- --out ../../assets/cars
    /Applications/Blender.app/Contents/MacOS/Blender -b -P props.py -- --out ../../assets/props

Blender **3.3.1**. Everything procedural and re-runnable headless; a script is
the source of truth, never a hand-made .blend.

---

## World units and the game camera

Metres. **Z up. The road runs along +Y.** X is to the right. The road surface
is Z = 0.

The watch projects the world with a pinhole camera that has **no pitch**
(vertical lines stay vertical, the way pseudo-3D engines work); the horizon is
placed with a lens shift instead:

| | |
| --- | --- |
| camera position | (0, 0, **2.0**) |
| looks along | +Y, **no pitch, no roll** (Blender rotation X = 90°, Y = 0, Z = 0) |
| image | 368 x 448 |
| focal length | **300 px** on both axes (sensor fit HORIZONTAL, sensor 36 mm, lens = 36 * 300 / 368 = 29.348 mm) |
| principal point | **(184, 150)** px from the top-left: the horizon of a flat road is the row y = 150. In Blender: shift_x = 0, shift_y = -(224 - 150) / 368 = **-0.2011** (with sensor fit HORIZONTAL the shift is in units of the width, and a positive shift_y moves the horizon down; cars.py and props.py check it with world_to_camera_view) |

Check it in the script with `world_to_camera_view`: (0, 50, 2.0) must land on
y = 150; (0.95, 3.2, 0) on (273, 337.5); (0, 3.2, 0) on (184, 337.5).

**Do not change this camera** without telling the engine side: the player's
car is drawn at a fixed place on the screen and must sit on the road the
watch draws.

Sun: from above and slightly behind-left of the camera (a direction coming
from roughly (-X, -Y), elevation ~55°), warm white, plus a soft sky light
(blue-ish world, low strength) so that the backs of things are lit and shadows
fall forward-right. Same sun for every render (cars and props) so the scene
matches.

## Region ids (vehicles)

Vehicles are NOT rendered in final colour. Each frame is a **lighting pass**
on neutral light-grey (0.8 albedo; a surface in full sun comes out ~196 of
255, speculars may go up to 255) and a **region-id pass**; the watch colours
each pixel as `palette[id] * light / 196`. A red car and a blue car are the
same render; the garage sells paint without new renders.

| id | region | notes |
| --- | --- | --- |
| 0 | empty | |
| 1 | paint A | the body |
| 2 | paint B | accent: stripes, a two-tone roof, side strakes, a livery band; **every car must have some id 2 area** so two-tone liveries read (a plain car = same colour for A and B) |
| 3 | glass | windows; keep it glossy so the lighting pass shows reflections of the sky |
| 4 | chrome / bright trim | bumpers of old cars, exhaust tips, mirror caps if chrome, badges (no real brand badges) |
| 5 | black trim | rubber, plastic, grilles, louvres, diffuser, wipers, mirrors if black |
| 6 | tyre | |
| 7 | rim | wheels |
| 8 | tail / brake light | the watch lights it up when braking |
| 9 | headlight / front lamps | also rally spot lamps |
| 10 | licence plate | plain, no text |
| 11 | interior / seats | seen through glass and in open cars/pickup beds |
| 12 | underbody / exhaust dark | the dark underside, exhaust pipes |
| 13 | indicator / amber | turn signals, side markers |
| 14 | roll bar / rack / extra | accessories (pickup roll bar, roof rack), colourable |
| 15 | spare | |

### Passes, per frame (8-bit PNG, same size)

| file | contents |
| --- | --- |
| `<name>_shade.png` | RGBA: RGB = light on neutral grey materials (diffuse + gloss where the material is glossy: paint, glass, chrome; matte on tyres and plastic), soft shadows, AO. A = coverage, antialiased. Film transparent, no ground. Standard view transform (NOT Filmic). |
| `<name>_id.png` | L: id * 16. No antialiasing (filter width ~0.01 px). Must cover every pixel the shade pass covers with alpha > 0. |
| `<name>_shadow.png` | L: how much the vehicle darkens the road there 0..255 (shadow catcher plane at Z = 0), nothing of the car itself. Soft, a little contact darkness under the tyres. |

## The player's cars: `near` renders

Four cars, **generic, no brand badges, no real names** (arcade 80s/90s):

| key | car | stats (for the look) |
| --- | --- | --- |
| `wedge` | 80s mid-engine wedge supercar: very low and wide, side strakes on the doors (paint B), wide flat rear deck, full-width louvred tail-light panel (black louvres id 5 over lights id 8), pop-up headlights closed, big rear tyres. The hero car of the reference photo. | top speed |
| `muscle` | 70s American fastback muscle car: long hood with a scoop, two racing stripes over hood/roof/trunk (paint B), chrome bumpers, ducktail spoiler, twin round tail lights per side, fat tyres. | acceleration |
| `rally` | 80s group-B style hot hatch: short, boxy, huge flared arches, roof vent, rear wing, a livery band (paint B), four rally spot lamps on the front (id 9), mud flaps (id 5). | grip |
| `pickup` | 80s/90s compact 4x4 pickup, lifted, big off-road tyres, roll bar in the bed with two lamps (id 14 + 9), step bumper (chrome), tailgate, two-tone lower body (paint B). | tough, slow |

Car placement for the `near` renders: centred on X = 0, wheels on Z = 0,
**rear bumper's rearmost point at Y = 3.2**, nose towards +Y. Real sizes
(wedge ~4.5 x 2.0 m, 1.13 m tall; muscle ~4.7 x 1.9 x 1.3; rally ~3.9 x 1.8 x
1.4; pickup ~4.7 x 1.8 x 1.8 lifted). With the game camera the wedge's rear
wheels touch the screen around y = 337 and it is ~180 px wide: big, like the
reference photo.

Frames: **yaw** of the car around the vertical axis through the car's
centre, **-24, -16, -8, 0, +8, +16, +24 degrees** (7 frames; positive = nose
turned to the right, +X). The watch shows them when steering and in curves.
Files: `near_<car>_y<k>_{shade,id,shadow}.png`, k = 0..6 (k = 3 is straight).
Render the full 368 x 448 frame; the packer crops.

## Every vehicle from further: `far` renders

The same vehicle seen further away is scaled by the watch. These renders are
what traffic, rivals and the other watch's car are drawn from.

Vehicles: the four player cars above **plus four traffic vehicles**:

| key | vehicle |
| --- | --- |
| `sedan` | a plain 90s family sedan |
| `compact` | a small 90s hatchback |
| `van` | a boxy minivan / delivery van |
| `truck` | a box truck (cab + tall cargo box), ~7.5 m long, 3.3 m tall; the box's rear is doors (paint B) |

Added in v0.4.12, traffic only (far views, no near frames):

| key | vehicle |
| --- | --- |
| `hearse` | a long 1980s American hearse on a station-wagon body: black paint A, a chrome **landau bar** (id 4) on each rear side panel, curtained rear side windows (curtains = id 11), a thin paint-B pinstripe, chrome bumpers, ~5.9 m long, 2.0 m wide, 1.5 m tall |

Traffic vehicles use the same region ids (1/2 are their paint; the watch
picks random colours).

Setup: camera height 2.0, no pitch, looking +Y; **the vehicle's rear-centre
ground point at (0, 12, 0)**; focal **1200 px** (4x the game: at 12 m that
is 100 px per metre at the vehicle's rear plane). Three views, moving the
**camera** sideways and keeping the principal point where it frames the
vehicle (use shift, not camera yaw):

| view | camera X | shows |
| --- | --- | --- |
| `l` | +3.5 | vehicle is to the camera's LEFT: its rear and right flank |
| `c` | 0 | straight behind |
| `r` | -3.5 | vehicle to the RIGHT: its rear and left flank |

Image size: large enough for the vehicle + its shadow with a few px margin
(e.g. 288 x 256 for cars, 384 x 416 for the truck); the packer crops. Files:
`far_<key>_<view>_{shade,id,shadow}.png`.

## Metadata

`meta.json` next to the PNGs: for every render its image size, and the pixel
where a reference ground point lands:

- `near_*`: the pixel of (0, 3.2, 0) (the rear bumper's ground point, before
  yaw) — should be (184, 337.5) for all frames.
- `far_*`: the pixel of (0, 12, 0) and `ppm: 100`.

Also per vehicle: length, width, height (metres).

---

## Props: the scenery (`props.py`)

Baked in **final colour** (no palette): RGBA PNG with antialiased alpha, plus
an L `_shadow.png` (optional, only for things that cast a shadow on the road
side, e.g. trees), rendered with the same sun.

Camera for props: height 2.0, no pitch, looking +Y, the prop's base-centre at
**(0, 40, 0)**, orthographic-looking telephoto (focal chosen so the prop
fills its image: record `ppm`, pixels per metre at the prop's base plane).
Image sized to the prop (e.g. a palm 160 x 320, a building 256 x 384) — no
prop taller than **384 px** or wider than 512 px. Props on the LEFT side of
the road are the watch's mirror of the right-side render, so asymmetric
things (a street lamp's arm, a sign) are modelled for the **right side of the
road** (arm reaching towards -X, the road). meta.json: size, `ppm`, anchor
pixel (the base-centre ground point), real width and height in metres.

Colours: realistic, a little saturated, arcade-bright. Keep silhouettes
bold: most props are seen 20-120 px tall.

| stage | props |
| --- | --- |
| **common** | `checkpoint` (an arch spanning the road: two pylons ~14 m apart and a banner beam 6 m up, bright; NO text — the watch writes words), `finish` (same, chequered banner), `cone`, `sign_curve_l` (yellow chevron board pointing left; right = mirror), `barrier` (a 2 m concrete jersey barrier block) |
| **city** (daytime highway, the reference photo) | `overpass` (a concrete highway bridge crossing the road: deck 26 m wide total span over the road, deck underside 6 m up, rusty green steel girders on the side facing us like the photo, pillars on both sides outside the road; its anchor = centre of the road under it), `lamp` (tall highway street light, curved arm), `tower_brick`, `tower_glass`, `tree_round`, `billboard` (blank coloured panel, no text), `sign_gantry` (green overhead highway sign on a truss spanning 18 m, blank panels) |
| **coast** | `palm`, `palm_tall`, `rock_cliff` (a sea-side cliff chunk ~15 m tall), `lighthouse`, `beach_hut`, `guardrail` (a 4 m section) |
| **desert** | `saguaro`, `saguaro_small`, `butte` (a red sandstone mesa, ~60 m tall, seen far), `rock_red`, `dead_tree`, `diner_sign` (a tall roadside sign on a pole, no text) |
| **mountain** (snow, played at NIGHT) | `pine_snow`, `pine`, `rock_snow`, `snowbank`, `cabin` (log cabin, windows lit warm), `lamp_night` (a road lamp that is ON: bright head, warm light pool baked into its alpha as a soft glow) — render the night props with a dim moonlit blue sun and warm emitters so they sit in a night scene |
| **halloween** (added v0.4.12: a night road through a haunted valley, NIGHT lighting like the mountain) | `dead_tree_twisted` (a gnarled leafless tree, crooked branches like claws, ~9 m), `pumpkins` (a cluster of 3 carved jack-o'-lanterns, glowing orange from inside, the glow baked into colour + alpha, ~1.2 m wide), `tombstones` (a cluster of 3-4 crooked gravestones and a cross, mossy, ~2 m wide), `cemetery_fence` (a 5 m section of wrought-iron fence with spiked bars between two stone posts, turned ~35° like the guardrail so its face shows), `scarecrow` (a ragged scarecrow on a post, sack head with a stitched face and a pointed hat, ~2.5 m), `haunted_house` (a crooked Victorian mansion with a tower, a few windows lit warm orange, dark roof, on a small rise, ~16 m tall, seen from far), `gas_lamp` (an old cast-iron street lamp, its flame ON with a sickly green-white glow baked like `lamp_night`, modelled for the RIGHT side) |
| **tunnels** (added v0.4.12: an alpine gorge in DAYLIGHT with road tunnels) | `tunnel_portal` (**a rock mountain face with a road tunnel entrance**: a concrete arch portal ~16 m wide and ~7 m high at the crown, its opening TRANSPARENT (alpha 0) so the watch sees the tunnel it draws behind; the rock face around it big, ~60 m wide and ~30 m tall, so it hides the sky around the opening; the anchor is the road centre at the portal's base plane; the arch's inner edge must be where the watch's tunnel walls are: walls at x = ±8 m, crown 7 m up), `rock_granite` (a grey granite outcrop with lichen, ~10 m), `waterfall_cliff` (a grey cliff with a white waterfall and a pool, ~18 m tall), `pylon` (a steel electricity pylon, lattice, ~25 m, from the dam's power line), and reuse `pine` and `guardrail` from the mountain / coast sets (daylight versions: render `pine_day` and `guardrail` if the mountain pine is night-lit) |
| **space** (the bonus stage: a highway floating in space) | `asteroid_a`, `asteroid_b`, `crystal` (a glowing crystal spire, emissive cyan/magenta), `ring_gate` (a neon ring the road passes through, ~16 m diameter, anchor at the road centre under it), `satellite`, `beacon` (a pylon with a pulsing light) |

## Backdrops (`props.py` too)

One panorama per stage, drawn behind the road above the horizon and scrolled
sideways by the watch in curves (it wraps around). **1024 x 160** RGBA (alpha
where the sky shows through; the watch draws the sky gradient behind it),
rendered with a 360° panoramic camera (Cycles equirectangular, only the band
from the horizon up ~16° — the horizon row must be the bottom row of the
image) or by laying out distant geometry. Files `bg_<stage>.png`:

| stage | backdrop |
| --- | --- |
| city | skyline of towers at mid distance, hazy, a few hills |
| coast | the sea's horizon with islands and a far headland (sea fills the bottom rows, opaque) |
| desert | mesas and buttes, red rock, heat haze |
| mountain | snowy peaks at night under a moon, dark blue |
| space | a big ringed planet and nebula wisps (alpha where space is, the watch draws stars) |
| halloween | a huge full ORANGE moon low in a violet night sky with thin clouds, a castle on a crag silhouetted against it, a ragged line of dead forest and hills; bats as tiny silhouettes (alpha where the sky shows) |
| tunnels | an alpine gorge by day: steep rock walls, snowy peaks far away, a concrete arch DAM spanning the gorge with a lake glint, pine slopes (alpha where the sky shows) |

Also `sky.json`: per stage the sky gradient (top colour, horizon colour), the
fog colour, and the ground colours the watch should use for the roadside
(two alternating shades), sand/snow, etc., chosen to match the renders.

## Look

Arcade-real, like a mid-90s arcade racer re-rendered today: clean shapes,
strong silhouettes, glossy paint, readable at 60 px wide. Check every change
by colouring a preview (a sample palette per car) and LOOKING at it, at full
size and at 1/4 size.
