# CHATARRA — a turn-based robot RPG

An open-world RPG laid out in rooms, with turn-based combat, in which the robots
— yours and your rivals' — are put together from **four interchangeable parts**:
head, torso, arms and legs. Sixteen variants of each give 65,536 robots, and not
one of them is written down anywhere.

The game loop is: walk around, fight, **rip a part off whoever you beat**, fit it
in the workshop and head out again with a different robot.

---

## 1. What to know before touching anything

### `.text` is the only scarce resource

Dynamic apps take their code from a reservation of **48 KB shared by every
loaded app** (`components/aos_dynapp/aos_dynapp.c`). `.rodata`, on the other
hand, is sent by the loader to PSRAM, which is 8 MB. That is:

| | costs |
| --- | --- |
| a line of code | ~8 bytes out of a 48 KB pool |
| a kilobyte of table | nothing |

Measured on 2026-09-08, with `-Os`, with the eight zones, their 51 rooms and
everything in section 8:

```
.text          38,475 B   78 % of the reservation   <- the only thing you pay for
.rodata        29,789 B   to PSRAM
.data.rel.ro   15,444 B   to PSRAM
.bss            2,540 B   to PSRAM
```

**And watch that 78 %, because it has a consequence.** The reservation is 48 KB
for ALL the apps loaded at once. With Chatarra open there are **10.6 KB** left,
which means a small app fits alongside it (`buscaminas` is 5.3 KB) but a big one
does NOT. The case that bites is an app with `AOS_APP_FLAG_BACKGROUND`: the
Recorder holds 38 KB forever once it has been opened, and after going through it
Chatarra no longer fits in the reservation. It does not crash — it falls back to
the general heap — but the log says so:

    N B of code did NOT fit in the reservation

If that line appears, the board has to be restarted before playing.

And the number that proves the rule works: with only zone 1 the `.text` was
**28,375 B**. Adding zone 2 moved it by 228 bytes — because of the couple of new
functions it needed, not because of the content — and **adding zones 3 through 8,
which are 36 more rooms, did not move it BY A SINGLE BYTE**. Three quarters of
the game cost no code.

For comparison: `g2043`, a shoot-'em-up, takes 28,208 B of `.text`. **The whole
RPG — world, combat, workshop, shop, quests, menus — costs the same as the
shoot-'em-up**, and that is no accident: it is the consequence of the three rules
below, which have to be respected if anything is added to it.

1. **The 64 parts are not 64 sprites nor 64 functions.** They are 64 descriptors
   of a few bytes each and four drawing functions that interpret them
   (`ch_parts.c`). The colour goes as an argument, just like `cjump`'s costumes.
2. **The world is a table.** Maps, props, entities, dialogue, items, attacks and
   quests are `const`. Adding a whole town **does not add a line of code**.
3. **Quests are flags and texts, not logic.** An entity carries two text pointers
   and two flags; that is enough for "I want something from you / thanks, here".
   A quest state machine would cost `.text`.

### The camera does NOT move

The world is split into rooms of **one screen**: 23 x 22 tiles of 8 px. Crossing
an edge changes room. That is not a limitation that was accepted: it is what
makes the game run at 30 fps.

With a moving camera you would have to upscale and invalidate the 165 thousand
pixels every frame — the 15 fps measured in 2043 — and the whole dirty-rectangle
technique falls apart. With fixed rooms the background stands still and per
frame you repaint the player's robot and the few creatures on patrol.

**A design consequence, not an implementation one:** any idea that moves the
whole background (parallax, screen shake, an animated backdrop) throws all of
this away. Best discarded before it is drawn.

### The touch panel does not reach the bottom

This board's CST816 **reports nothing below a real y≈354**, and in the simulator
that does not show because the mouse reaches everywhere. That is where the
screen's division comes from:

```
buffer 184 x 224, upscaled x2 to the board's 368 x 448

  y   0..175   the map / the menus / the combat   (real   0..351)  TOUCHABLE
  y 176..223   the HUD                            (real 352..447)  NOT touchable
```

Everything touchable in this game ends at **y=174 of the buffer** (348 real).
The lists are six rows of 18 px from y=64: the last one ends right there.

And that is why **the menu has no on-screen button**. It opens with the
**physical button** (like a console's START) or by **touching your own robot**.
Both are written on the title screen, because neither can be guessed.

---

## 2. The files

| File | What it is |
| --- | --- |
| `chatarra.h` | the whole model: parts, robot, world, savegame, modes |
| `ch_pixel.c/.h` | pixel-art engine with dirty rectangles (copied from `cjump`, with the palette extended to earth, grass, water, stone and wood) |
| `ch_parts.c` | the 64 parts, the 44 attacks, the items, the colour schemes and the four functions that draw a robot |
| `ch_world.c` | the tiles, the props, the nine rooms, the entities and the dialogue |
| `ch_map.c` | the room engine: background, pathfinding by breadth-first search, movement, encounters |
| `ch_battle.c` | the turn-based combat |
| `ch_ui.c` | HUD, dialogue, menu, workshop, items, stat sheet, shop, title |
| `chatarra.c` | the only thing LVGL and the HAL see: canvas, blit, touch, button, saving |

The game (everything but `chatarra.c`) **does not know that LVGL, the HAL or the
file system exist**. It can be tested without a screen.

---

## 3. How content is added to it

### A new town or dungeon

All in `ch_world.c`, and **nowhere else**:

1. A `static const char *const M_WHATEVER[ROWS]` with **22 rows of exactly 23
   characters**. Each character is a tile (see the `TILES` table).
2. Optional: a `ch_prop_t P_WHATEVER[]` with the props (trees, houses,
   machines). **A prop cannot land on an entity's tile**: the entity always draws
   its own thing, so both would be drawn.
3. A `ch_ent_t EN_WHATEVER[]` with what you interact with.
4. A row in `ch_salas[]` and a value in the rooms `enum`.

The measurements check themselves: there is a width verifier in section 5.

### A new part

In `ch_partes[]` of `ch_parts.c`: name, five stats, type, the zone from which it
appears, the drawing style (0..15) and up to two attacks. The drawing **already
exists**: it comes from the `CAB_FORMA` / `CAB_OJOS` / `CAB_ANT`, `TOR_*`,
`BRA_*` and `PIE_TIPO` tables, which are 16 bytes each. Changing a part's look
means changing a number.

### A quest

There is no quest system and none is needed. An `E_PNJ` carries:

```
p2      the flag that marks "I already solved this"
p3      the flag it wants brought to it (0 = none)
premio  the item it hands over
texto   what it says beforehand
texto2  what it says when it sees what you brought
```

The neighbour in Villa Tuerca is the complete example: he asks for the screws,
the chest in the depot drops them in your bag and lights the flag, and on
returning the neighbour pays with a Soldering Iron and 200 credits. **Zero lines
of code of its own.**

### An attack

A row in `ch_moves[]`: name, type, power, energy cost, accuracy, effect and
probability. Then it is assigned to a part by its index. Index **0 is the basic
hit** and every robot has it: that is why parts use 0 as "contributes no attack"
and the case of being left with nothing to choose never arises.

---

## 4. Simulator switches

They only exist there; on the board `getenv()` always returns NULL.

| Variable | What for |
| --- | --- |
| `CH_SALA=5` | starts straight in that room |
| `CH_NIVEL=20` | the robot's level |
| `CH_PIEZAS=1` | full bag, items and credits: for testing the workshop |
| `CH_COMBATE=1` | opens straight into a fight |
| `CH_SHOT=/tmp/ch` | **one `.ppm` capture per screen change** |
| `CH_FPS=1` | frames per second and % of screen pushed |
| `CH_MUDO=1` | no beeps |

`CH_SHOT` exists because the app dumps its own upscaled buffer, which
`tools/ppm2png.py` turns into a PNG; it comes out more faithful than a screen
grab, because it is exactly what gets handed to the panel.

```bash
cd sim
env AOS_SIM_VIEW=demo.chatarra CH_SALA=1 CH_SHOT=/tmp/ch \
    AOS_SIM_KEYS="ms:700,tap:184x232,ms:700,tap:184x144" ./build/amoledos_sim
python3 ../tools/ppm2png.py /tmp/ch*.ppm
```

---

## 5. Checks that have to be run

**The widths of the maps and the sprites.** A 24-character row in a 23-wide map,
or a 20-px-wide sprite declared as 3 tiles, produces no compile error at all: it
leaves rubbish on the screen and trails stuck to it. This catches it:

```bash
cd apps/chatarra/main
python3 - <<'EOF'
import re
src = open('ch_world.c').read(); errs = []
for m in re.finditer(r'static const char \*const (\w+)\[(\w+)\] = \{(.*?)\n\};', src, re.S):
    name, n, body = m.group(1), m.group(2), m.group(3)
    strs = re.findall(r'"((?:[^"\\]|\\.)*)"', body)
    lens = [len(re.sub(r'\\.', 'X', x)) for x in strs]
    if   name.startswith('PX_'): er, ew = 8, 8       # tiles: 8x8
    elif name.startswith('M_'):  er, ew = 22, 23     # maps: 22 rows of 23
    else:                        er, ew = int(n), None
    if len(strs) != er: errs.append('%s: %d rows, expected %d' % (name, len(strs), er))
    if ew:
        for i, l in enumerate(lens):
            if l != ew: errs.append('%s[%d]: width %d' % (name, i, l))
    elif len(set(lens)) != 1: errs.append('%s: widths %s' % (name, sorted(set(lens))))
    elif lens[0] % 8:         errs.append('%s: width %d is not a multiple of 8' % (name, lens[0]))
for e in errs: print('ERROR', e)
print(len(errs), 'problems')
EOF
```

**The width of the dialogue.** The box is 176 px minus 6 of margin on each side:
**27 characters** fit. More than that wraps by itself, and it looks bad ("STOP /
RIGHT THERE.").

```bash
python3 - <<'EOF'
import re
s = open('ch_world.c').read(); bad = []
for m in re.finditer(r'N_\(((?:\s*"(?:[^"\\]|\\.)*")+)\)', s):
    txt = ''.join(re.findall(r'"((?:[^"\\]|\\.)*)"', m.group(1)))
    for l in txt.split('\\n'):
        if len(l) > 27: bad.append((len(l), l))
for n, l in sorted(set(bad), reverse=True): print(n, repr(l))
print(len(set(bad)), 'long lines')
EOF
```

**And the usual ones**, which are in [`docs/APP-API.md`](../../docs/APP-API.md):

```bash
./tools/build_apps.sh chatarra        # the board is a different compiler, and it rules
python3 tools/gen_lang.py unmarked    # visible text left unwrapped
python3 tools/gen_lang.py check en
./tools/audit_layout.sh es en de xx
```

Careful with `audit_layout.sh`: it walks **LVGL objects**, and this game draws
everything on a canvas. The guarantee that nothing touchable falls below the
touch panel's limit comes from this README's arithmetic, not from the audit.

---

## 6. Traps that have already bitten here

**The robot's shadow was drawn on top of the text box.** It was a disc centred on
the base, which means it stuck out of the 26x40 box by its whole radius; in
combat that covered half a word of the message. Now it is flat and lives inside
the box. What leaves the box **does not go into the dirty rectangle either**, so
it also stayed stuck.

**You could not leave the app, and there was no error.** `aos_ui_back()` asks the
app's `back` callback first. Since leaving has to be deferred — it destroys the
app — the tick called `aos_ui_back()`, which re-entered the app's `back`, which
asked to leave again. An infinite, silent loop. The `saliendo` flag cuts the
cycle.

**A translated string cannot be `snprintf`'s format.** If a translation brings a
`%` along, the formatter eats it. Write `snprintf(dst, n, "%s", _("..."))`.

**A room name can coincide with the first line of a dialogue, and an automated
rewrite takes it with it.** When reflowing the dialogue to 27 characters, the
script identified them by their first line: the town's sign reads `VILLA TUERCA`,
which is **also** the room's name, so the name ended up replaced by the whole
sign. On screen it looked like the HUD writing the sign over the credits, and it
seemed like a dirty-rectangle problem. If texts are rewritten in bulk, the search
has to be anchored to the **whole block**, not to the first line.

**An 8x8 pattern that repeats turns fine detail into noise.** The first version of
the scrap had loose pixels and from two tiles away it read as snow. One big chunk
per tile reads as broken metal.

**The grass's variation has to be DETERMINISTIC.** It alternates between two
patterns according to the coordinate and **not** with the random generator: the
background is restored by rectangles every time something passes over it, so
grass drawn at random on the fly would give a different picture on every repaint
and the map would seethe under the player. It is the same reason `arkanos`'s
stars are in a table.

**GCC counts 11 characters for each `%d`.** `char l3[30]` with three `%d`
compiles on the Mac and **stops the board's build** with
`-Werror=format-truncation`.

---

## 8. What was added on 2026-09-08

Nine things, all of them game, which took the `.text` from 29,055 to 38,475 B:

| What | Why | Cost |
| --- | --- | --- |
| **Animated combat**: a projectile per type, impact rings, fourteen particles, bars that drain by themselves and a damage number on a plate | It was the biggest quality hole: the robots shook and nothing else | ~2.0 KB |
| **Music**: a one-voice sequencer over the beeper, seven melodies, a MUTE/EFFECTS/ALL setting | The device has a speaker and the game sounded like nothing | 0.6 KB |
| **Ambience**: snow, embers, dust and drips according to the zone, plus glints on water and lava | It is what separates "a drawn map" from "a place" | ~0.9 KB |
| **Parts register**: all 64 drawn, in silhouette the ones you have not seen | It gives a collecting sense to what is already the heart of the game | ~1.6 KB |
| **World map**: the 8 zones, where you are, which bosses you brought down | Across 51 rooms you get lost | ~0.7 KB |
| **Four pages of help** | The systems were only explained in the grandmother's dialogue | ~0.6 KB |
| **A typewriter effect** in the dialogue and a **transition** on changing room | Text that appears all at once gets skipped unread; a hard cut reads as two screens | ~0.9 KB |
| **Enemies that see you** in the dungeons, with an exclamation mark and their level above them | It is what makes you watch where you walk; and knowing the level saves you from losing by surprise | ~0.8 KB |
| **A workshop that compares**, a shop that describes, effectiveness on the attack buttons, criticals, visible statuses, autosave, an ending screen | Everything the game knew and did not show | ~2.3 KB |

Two decisions from that day that are best not revisited:

- **The music does not play on the map.** A one-voice buzzer repeating a loop
  while you walk for half an hour through 51 rooms becomes unbearable and the
  user turns the sound off entirely. Leaving the map silent, the combat music
  **lands**, which is exactly what you want to happen.
- **Enemies only chase in the dungeons.** If they did it in towns and on roads
  there would not be a single place to stop and look at the map, and a game that
  never lets you breathe tires you out before it gets hard.

And one new trap, expensive to find:

**A `const` table declared by an enum's ceiling gets filled with ZEROS and nobody
warns you.** `ch_items[ITEMS]` had 10 of its 17 rows written — two bulk edits had
failed silently because the pattern they were looking for no longer matched — and
the seven quest items had a **NULL name**. Opening the chest with the screws
would have drawn a null pointer. Now `ch_map_check()` verifies the three big
tables.

---

## 7. Status

**Done:** the whole game. The engine, the 64 parts, the 44 attacks with six types
and an effectiveness table, turn-based combat with status effects, the workshop,
the shop, the items, the stat sheet, saving, flag-driven quests, and **the eight
zones across 51 rooms**, from Villa Tuerca to the Summit:

| # | Zone | Road / Dungeon | Sub-boss | Type |
| --- | --- | --- | --- | --- |
| 1 | Villa Tuerca | Sendero Norte / El Desguace | Guardián | IMPACT |
| 2 | Puerto Bujía | Costa del Cangrejo / La Bodega | Capataz | ACID |
| 3 | Alto Voltio | La Cuesta / La Subestación | Ingeniera Jefa | VOLT |
| 4 | Fundición | Valle del Humo / El Horno | Maestro Fundidor | FIRE |
| 5 | Criovalle | Paso Helado / Cueva de Hielo | Guardabosque | CRYO |
| 6 | Ciudad Malla | Autopista / El Servidor | Administradora | PLASMA |
| 7 | Villa Óxido | Llanura Muerta / Cementerio de Robots | Chatarrero Mayor | IMPACT |
| 8 | Prisma | Último Tramo / La Torre | **El Campeón** | CRYO |

Each zone has its workshop, its shop, its characters, an errand with a reward,
chests and fixed enemies that go up in level. Beating the sub-boss gives a Sector
Pass that opens the **checkpoint** of the next zone: that is the whole
progression, and it is two fields of the entity table.

**Translated in full into English and German**: 422 strings per language,
`gen_lang.py check` clean in both. Everything transliterated, because the canvas
font is 5x7 and has no accents (`gen_lang.py` already verifies that for this
app).

And one thing that is not optional in this game: **the translation has to be
measured against the width of the box it lands in**, because the text is drawn by
hand and there is nothing to lay it out. Two real cases:

- `"YOUR ROBOT USES ELECTROMAGNET"` is 29 characters and **27** fit in the combat
  box. That is why the subject became `YOUR BOT` / `DEIN BOT` / `GEGNER`, and the
  German attack names are capped at **12 characters**: `DEIN BOT NUTZT
  Flammenstoss` comes to exactly 27.
- The 105 lines of dialogue in each language are cut by hand to ≤27. A longer
  line breaks nothing, but it wraps by itself and looks bad.

**Pending:**
- **Measuring it on the board.** Everything here is tested in the simulator,
  where LVGL costs twenty or thirty times less.

---

## 9. A saved structure does not grow through the middle (2026-09-08)

A user reported that the game started with six quest items at **x255**: Rusty
Anchor, Thick Fuse, Steel Mould, Flask, Master Key, Big Cog. It was not
generosity: it was a migration bug, and it is worth writing down because it is
one of the kind that gives you not a single compiler warning.

`ch_save_t` had its arrays dimensioned with the game's live constants:

```c
uint8_t obj[ITEMS];        /* ITEMS started at 11 and ended at 17 */
uint8_t piezas[MOCHILA];   /* MOCHILA started at 12 and ended at 16 */
```

and the loader migrated with a `memcpy` of `sv.largo` bytes. As long as the
fields that grow are **at the end**, that works. Here `obj[]` is **in the
middle**: on taking ITEMS from 11 to 17, everything that came after it shifted
six places, and what landed inside `obj[]` was the beginning of `piezas[]` —
which is initialised with `0xFF` because 0xFF means "empty slot". Six 0xFF bytes,
six items at x255. And the game did not complain: 255 is a perfectly valid
`uint8_t`.

The fix is three things, and none of them is enough on its own:

1. **Fixed-size arrays in the saved format**, decoupled from the enums:
   `CH_MAX_OBJ 32`, `CH_MAX_MOCHILA 16`, `CH_MAX_PIEZAS 64`, with a
   `_Static_assert` that the live constants fit. Now adding an item does not move
   a single byte of the file.
2. **SAVE_VER 3 with a field-by-field conversion**, not a byte-wise one: the v2
   structure stays declared as a mirror (`ch_save_v2_t`) and is copied field by
   field. A `memcpy` between two different formats is exactly the mistake that
   caused this.
3. **Sanitising on load**: quest items are capped at 1 and consumables at 99,
   slots outside `MOCHILA` are forced to `0xFF` and the room is validated against
   `ch_nsalas`. A damaged save should not be able to put the player in a room
   that does not exist.

The rule: **a saved format is not versioned by its size, it is versioned by its
version number, and it is converted by reading fields.** The size is good for
discarding a broken file, not for knowing what is inside it.

It was checked without the board with `CH_MKV2=1` (simulator only), which writes
an authentic v2 save with the bag full of 0xFF; the next run converts it and the
item list ends up with what it should.

## 10. Taller rows for the finger

The lists used a single geometry of 18 px of height per row. On the board that is
36 real px and with a finger you miss. Now the geometry is per screen —
`lista_geom(y0, height, rows)` — and it came out like this:

| Screen | y0 | row height | rows | reaches down to |
|---|---|---|---|---|
| Items / Shop | 46 | 21 (42 real) | 6 | 172 |
| Workshop | 62 | 21 (42 real) | 5 | 167 |
| Menu | 28 | 16 (32 real) | 9 | 172 |

The menu has nine entries and cannot be given 21 without dropping one, so space
was won by compacting the header (title at y=8, subtitle at 20, rule at 31). The
ceiling of 172 is the usual one: **below y=172 of the buffer the CST816 touch
panel reports nothing**, so a row that lands there can be seen but not touched.
