# Golf — 3D assets rendered in Blender

The watch (ESP32-S3, 368x448 AMOLED, RGB565) cannot run a 3D character, so
the golfer and the props are modelled and animated in Blender and rendered to
sprites. The renders are NOT final colours: the golfer comes out as a
**lighting pass** (grey) plus a **region-id pass**, and the watch colours each
pixel with the palette of the skin the player bought in the shop:

    colour = palette[id] shaded by grey

That is what makes skins cost nothing: a red shirt and a blue one are the same
render. Hats are different geometry, so each hat model is its own small layer
rendered with the body as a hold-out (occluded where the body is in front).

Blender 3.3.1 is at `/Applications/Blender.app/Contents/MacOS/Blender`
(Cycles on the M2's Metal GPU works). Everything is **procedural and
re-runnable headless**:

    /Applications/Blender.app/Contents/MacOS/Blender -b -P golfer.py -- --out ../../assets/render

No hand-made .blend files are the source of truth; a script is.

---

## World units and the swing camera

Metres. Z up. The ball sits at the origin on the ground (z = 0). The target
is straight ahead along **+Y**. Right-handed golfer: stands on the -X side of
the ball-target line, **facing +X** (towards the ball), left shoulder towards
the target. Feet centre about (-0.75, 0, 0), stance width ~0.45 m along Y.

The swing camera is shared with the watch's 3D renderer, so the golfer lands
exactly on the terrain the watch draws. **Do not change it without telling
the engine side**:

| | |
| --- | --- |
| position | (-0.16, -4.80, 2.82) |
| looks along | +Y, pitched **down 11°** (Blender rotation X = 79°, Y = 0, Z = 0) |
| vertical FOV | 50° (sensor fit VERTICAL) |
| image | 368 x 448, principal point at the centre |

With that camera the feet land around y = 395, the head around y = 235, the
horizon at y ≈ 131 and the ball at x ≈ 200. Render the full 368x448 frame;
the packer crops.

Sun: from behind-left and above the camera (a direction coming from roughly
(-X, -Y), elevation ~50°), slightly warm, plus a soft sky light, so the
golfer's back and right side are lit and the cast shadow falls forward-right.

## Passes, per frame

All PNGs at 368x448 (or the turntable size), 8-bit:

| file | contents |
| --- | --- |
| `<seq>_<nn>_shade.png` | RGBA. RGB = the lighting on neutral light-grey (0.8) materials: diffuse + a little gloss, soft shadows, ambient occlusion. A = coverage, antialiased. Film transparent, no ground visible. Standard view transform (NOT Filmic), so grey means grey. |
| `<seq>_<nn>_id.png` | L (grey). value = region id x 16 (0 = empty). No antialiasing (pixel filter ~0.01 px), so every pixel is exactly one region. Must cover at least every pixel the shade pass covers with alpha > 0. |
| `<seq>_<nn>_shadow.png` | L. How much the golfer darkens the ground there, 0..255 (a shadow-catcher plane). No golfer pixels in it. |

Hat layers, per hat model k: `hat<k>_<seq>_<nn>_shade.png` and
`hat<k>_<seq>_<nn>_id.png`, same meaning, only the hat, with the golfer's body
held out (hat pixels behind the head or the club do not appear).

The body passes are rendered **without a hat** and with hair.

## Region ids

| id | region | notes |
| --- | --- | --- |
| 0 | empty | |
| 1 | skin | face, neck, ears, forearms if short sleeves, the right hand |
| 2 | hair | short hair; visible under/around the hats and when there is none |
| 3 | shirt A | polo body |
| 4 | shirt B | polo accent: collar, sleeve cuffs, and a pattern (e.g. horizontal stripes on the body) so a skin can be striped or plain (plain = same colour in A and B) |
| 5 | trousers A | |
| 6 | trousers B | a check/tartan pattern (squares), so "plaid trousers" is a palette |
| 7 | shoes A | |
| 8 | shoes B | sole, laces or saddle panel |
| 9 | belt | |
| 10 | glove | left hand |
| 11 | club head | metal |
| 12 | shaft | |
| 13 | grip | |
| 14 | hat A | only in hat layers |
| 15 | hat B | brim, band or pompom, only in hat layers |

## Sequences

1. **`swing`**, 24 frames, swing camera. 0 = address (club behind the ball),
   0..10 the backswing to the top (the watch shows frame = power while the
   meter rises, so they must read as a smooth progression), 11..14 the
   downswing with 14 = impact (club head on the ball), 15..23 the follow-
   through to a held finish. Club: a driver-like wood (shaft ~1.1 m).
2. **`idle`**, 4 frames, swing camera: address with a small waggle / weight
   shift, loops 0-1-2-3-2-1.
3. **`cheer`**, 8 frames, swing camera: after a good shot, turns towards the
   camera a little and pumps a fist, club in the other hand.
4. **`sad`**, 6 frames: head drops, club leaning, hand on hip.
5. **`turn`**, 8 frames, a showcase camera for the shop: standing relaxed with
   the club head resting on the ground, rotated in 45° steps (0 = facing the
   camera, then turning to its left), camera a bit above chest height, whole
   body in a 184 x 280 image with a little margin.

## Metadata

`meta.json` next to the PNGs: per sequence the frame count, image size, and
for `swing` the club head's screen position per frame; for `turn` the feet
baseline.

## Look

Stylised-realistic, like a good mobile golf game: correct adult proportions
(~7.5 heads), smooth shapes (skin modifier + subdivision, metaballs, or sculpted
primitives), clothes that read as clothes (a polo with a collar and sleeves,
trousers with a crease and a belt, a glove, shoes), a readable club. At 160 px
tall every silhouette decision shows: the pose must be beautiful at that size.
Check every change by colouring a preview with a sample palette
(`preview.py`) and LOOKING at it.

## Props (a second script, `props.py`)

Trees and small things for the course, baked in colour (no palette):

| name | what |
| --- | --- |
| `tree_pine`, `tree_oak`, `tree_poplar`, `tree_palm`, `bush` | **side view** for the 3D view (camera pitched down 11° like the swing camera, orthographic, sun from behind-left), 128 px tall for the trees (bush 48), RGBA with alpha, and the trunk base's pixel in meta.json; **top view** for the map (orthographic straight down, sun from the upper-left of the image = north-west), 96 x 96, RGBA, plus a separate `_topshadow.png` (L) of the shadow it casts towards the lower right |
| `flag` | pin and flag, side view, 64 px tall, 4 frames of the flag waving |

Colours for the props: realistic, a little saturated; greens that sit well on
mown grass (the watch's fairway is around #4FA23A, rough #3C7F2C).
