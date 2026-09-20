# CHATARRA — a turn-based robot RPG

An open-world RPG laid out in rooms, with turn-based combat, in which the robots
— yours and your rivals' — are put together from **four interchangeable parts**:
head, torso, arms and legs. Sixteen variants of each give 65,536 robots, and not
one of them is written down anywhere.

The game loop is: walk around, fight, **rip a part off whoever you beat**, fit it
in the workshop and head out again with a different robot.

---

## 1. What to know before touching anything

### `.text` used to be the only scarce resource. It is not any more.

This section opened, for two weeks, by saying that dynamic apps take their code
from a reservation of **48 KB shared by every loaded app**, that `.rodata` goes
to PSRAM instead, and that therefore a line of code cost eight bytes of a pool
while a kilobyte of table cost nothing.

**Since v0.3.4 that is no longer true** (`docs/RAM-AUDIT.md`, section 8). The
loader maps the apps' `.text` into PSRAM through the instruction-bus MMU: the
reservation is gone, the executable heap went from 22 K free in one block to
131 K, and the games were measured on both builds at the same frame rate —
Claude Jump 29.2 against 29.0 fps, 2043 9.6 against 9.6, with run-to-run noise
larger than any difference between them.

Measured on 2026-09-20, with the team of three, the booth and the icon:

```
.text          53,803 B   to PSRAM, 64 K-aligned    (was 38,475 on 2026-09-08)
.rodata        31,495 B   to PSRAM
.data.rel.ro   15,628 B   to PSRAM
.bss            2,540 B   to PSRAM
```

The warning that used to live here — *watch the 78 %, a `BACKGROUND` app like
the Recorder pushes Chatarra out of the reservation* — **no longer applies, and
this paragraph is all that is left of it**. There is no reservation to be pushed
out of. If the old line

    N B of code did NOT fit in the reservation

ever shows up in the log, it means the firmware was built WITHOUT
`CONFIG_ELF_LOADER_TEXT_PSRAM_MMU`, and then everything the old section said
applies again word for word.

### What survives the change, and why

The number that proved the old rule still says something true: with only zone 1
the `.text` was **28,375 B**; adding zone 2 moved it by 228 bytes, and **adding
zones 3 through 8 — 36 more rooms — did not move it BY A SINGLE BYTE**. Three
quarters of the game cost no code.

That is no longer a saving, but it is still the reason the game can be checked.
Content that lives in tables is content a program can walk: `ch_map_check()`
reads all 51 rooms and finds a door standing on a wall, a chest on top of a
prop, an entity with no name. It could not have read a function. **The three
rules below stay, for that reason rather than for the pool.**

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
| `ch_ui.c` | HUD, dialogue, menu, workshop, items, team screen, register, shop, title |
| `ch_link.c` | the phone booth: the protocol and the screen for the other watch |
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
| `CH_PIEZAS=1` | full bag, items, credits **and a team of three**: for the workshop, the team screen and the swap button |
| `CH_COMBATE=<1..8>` | opens straight into a fight **in that zone's arena** |
| `CH_HERIDO=1` | both robots under a quarter of their health: for the sparks |
| `CH_MEL=<1..7>` | forces a tune, to hear it without playing up to it |
| `CH_WAV=/tmp/x.pcm` | dumps everything the synthesiser makes; `tools/pcm2wav.py` makes it playable |
| `CH_SHOT=/tmp/ch` | **one `.ppm` capture per screen change** |
| `CH_FPS=1` | frames per second and % of screen pushed |
| `CH_MUDO=1` | no beeps |
| `CH_MKV2=1` | writes a save in the OLD v2 format so the conversion can be checked without the board |

### Two simulators, for the booth

The link is UDP on 127.0.0.1, one port per instance. There is **no development
flag** to force the partner, the way Truco and Pixel Art need one: the booth
brings the radio up BEFORE it asks whether there is anybody paired, which is
both the honest order and the one the simulator answers.

```bash
cd sim
env AOS_SIM_VIEW=demo.chatarra CH_SALA=1 CH_PIEZAS=1 CH_SHOT=/tmp/a_ \
    AOS_SIM_LINK_PORT=47000 AOS_SIM_LINK_PARTNER=47001 AOS_SIM_POS=0,40 \
    AOS_SIM_KEYS="ms:3000,tap:312x216,ms:7000,tap:184x188,ms:20000" ./build/amoledos_sim &
env AOS_SIM_VIEW=demo.chatarra CH_SALA=1 CH_PIEZAS=1 CH_SHOT=/tmp/b_ \
    AOS_SIM_LINK_PORT=47001 AOS_SIM_LINK_PARTNER=47000 AOS_SIM_POS=420,40 \
    AOS_SIM_KEYS="ms:3000,tap:312x216,ms:30000" ./build/amoledos_sim &
```

`tap:312x216` is the booth in Villa Tuerca and `tap:184x188` is COMBATIR. **With
two simulators the script clock runs at about half speed** while the captures
keep wall time, which is why the waits above look so long: a step that reads as
7 s takes about 14.

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

---

## 11. Three robots, an icon of its own and a phone booth (2026-09-20)

### 11.1 A team of three

`save.yo` is still the robot that is out, and it is still read by every line of
the combat, the workshop and the HUD. The reserves went into `save.banco[2]` at
the **end** of the structure, which is the rule section 9 wrote in stone, and
`SAVE_VER` went to 4. The conversion from v3 is a copy of the prefix plus a
zeroed tail — with, in `chatarra.c`:

```c
_Static_assert(offsetof(ch_save_t, banco) == sizeof(ch_save_v3_t),
               "v3 dejo de ser un prefijo de v4: el campo nuevo no va en el medio");
```

which is the x255 lesson turned from a paragraph into a build error.

**A robot is broken when its health is zero.** There is no `roto` flag: the
state was already in the structure, and a second copy of a truth is a second
chance for the two to disagree.

Everything outside `ch_parts.c` talks in **slots**: 0 is the robot that is out,
1 and 2 the bench. Nothing else needs to know that the active one lives in a
different field.

| What | Where | Rule |
| --- | --- | --- |
| build one | team screen, ARMAR | needs one loose part of each of the four categories; takes the best of each; comes out at **your level minus two, minimum one** |
| strip one | team screen, DESARMAR | its four parts go back to the bag; needs four free slots; never the active one |
| swap | team screen, or CAMBIAR in combat | in combat it **costs the turn**, as it should |
| repair | any workshop | **the whole team**, not just the one that was out |

Two decisions worth keeping:

- **A built robot does not start at level 1.** You assemble it out of parts torn
  off opponents your own size; a level-1 robot in a level-20 dungeon is not a
  reserve, it is a second loss. Nor at your own level, or the reserve would be
  free and the robot you have been raising would stop mattering.
- **When your robot falls you choose the replacement — except over the link,
  where it is automatic.** A choice in the middle of a shared turn would be a
  message the lockstep does not have; the first one standing comes out, the same
  on both watches, because the bench never takes damage and both sides can work
  out which one that is.

The combat menu went from four cells to **six**: three columns of 56 px, the
same height as before, so the row got no shorter and the finger no smaller.
Five are buttons (ATACAR, OBJETO, CAMBIAR, ANALIZAR, HUIR) and the sixth is not
a button at all — it is the team, three lamps as long as each robot's health, in
the one place you are already looking when you decide what to do. The
sub-lists — attacks, items, the team — keep the wide 2x2, because there an
attack's name has to fit.

### 11.2 The icon lives in the `.so`

Chatarra wore a gamepad because there was no robot in the firmware's list. Since
v0.3.8 it does not have to: `aos_icon_set_ops(app, CHATARRA_ICON, sizeof
CHATARRA_ICON)` in `init()`, after `desc.id`, hands the launcher an 86-byte AIC
blob (`docs/ICONS.md`) — an aerial with a red lamp, shoulders, a head with two
yellow eyes and a grille. Every number is a percent of the icon size, so one
blob draws at 66, 74 and 82 px.

`desc.icon_vec` stays as it was: a firmware older than the call falls back to
it, and falling back to a gamepad beats falling back to nothing.

### 11.3 The phone booth

Every town has one, at the side of the square. Walk into it and the watch goes
on the air and talks to the one it is paired with: **fight, swap a robot, or
swap a part**. It is the sixth app on the link and the lessons of the other five
are in `docs/APP-GUIDE.md` section 16; what this one added:

**The radio goes up BEFORE asking whether anybody is paired.** On the board the
partner lives in NVS and either order works. In the simulator it does not — the
partner is only put there by the link's own tick — so Truco and Pixel Art each
carry a development flag to skip the question. Asking in this order needs none,
and it is the honest order anyway: you cannot ask whether somebody is on the air
with the radio off.

**The radio is not on while you play.** It goes up at the booth's door and comes
down when you leave (and in `destroy()`, for the way out that skips the door).
The link costs 4.5 KB of internal RAM and radio time, and nearly every minute of
this game is played alone — which is also what makes the booth a *place*.

**A battle between two watches sends choices, never results.** Truco's lesson:
the engine is deterministic given its dice, so both watches run the same combat
and only the two choices of each turn travel. Nobody sends "I did 14 damage";
both compute 14. Three things had to be right:

1. **The dice are the host's.** `g->rng_bt`, seeded from the frame the host
   sends, read by the five rolls that decide something and by nothing else. The
   particles and the prizes keep rolling on `g->rng`: the particles because they
   change nothing, the prizes because the WINNER works them out alone, and a
   draw one side makes and the other does not is exactly how a shared sequence
   comes apart.
2. **The host orders.** The guest sends its choice and applies nothing — its own
   tap included — until the host echoes both.
3. **A tie on speed is decided globally.** The local engine breaks a tie with
   "does the rival go first?", which is the *opposite* question on the two
   watches: the same coin would have both answering yes. The link draws "does
   the HOST go first?" instead, and each side turns that into its own answer.

And one thing that is insurance rather than design: every choice carries the two
health totals as the sender sees them, and a mismatch stops the battle with
`LOS DOS COMBATES SE DESINCRONIZARON` instead of drifting into two games that
both look fine.

The choice is **one byte** — `0..3` an attack slot, `0x10+i` an item, `0x20+s` a
robot coming out — because that is all the other watch needs: it was sent the
whole team when the battle started, and it has the same item table. Items and
running away are off over the link (greyed out, not explained after the tap).

A swap is symmetric by construction: both sides put something on the table,
neither moves until both have, and then each gives what it offered and takes
what the other did. That is the same operation seen from the two ends, so there
is no "who applies first" and no way for one to apply and the other not.

### 11.4 The one crash the booth produced, and what it is

The first battle between the two real watches ran — both started, both drew
their mirror of it, both read the same levels — and then `amoledos` **rebooted**
on the turn where both had chosen. The core dump (`/api/coredump`, then
`esp-coredump info_corefile`) says precisely where:

```
taskLVGL:  esp_lcd_panel_io_tx_color -> spi_bus_lock_bg_request
           -> req_core -> bg_enable -> spi_bus_intr_enable
wifi/ISR:  spi_intr -> spi_bus_lock_bg_exit -> bg_exit_core
           -> resume_dev_in_isr(dev_lock = 0x0)        <- LoadProhibited, excvaddr 0
```

It is a race **inside ESP-IDF's SPI bus lock**, not in this game. In
`bg_exit_core()` (`components/esp_hw_support/spi_bus_lock.c:589`), the branch
that runs when no device is acquiring calls `schedule_core()` and then
`resume_dev_in_isr(lock->acquiring_dev, ...)` — and the check that
`acquiring_dev` is not NULL is a `BUS_LOCK_DEBUG_EXECUTE_CHECK`, which compiles
to nothing in a release build. If the SPI interrupt lands in the window where
the LVGL flush is inside `req_core()` and has not published the device yet,
`resume_dev_in_isr(NULL)` dereferences address 0. That is exactly the
`excvaddr 0x0` in the dump. IDF here is release/v5.5 of 2026-08-24.

What was measured, because one crash is an anecdote:

| Condition | Result |
| --- | --- |
| Link app (`aos.link`) open on the board, radio up, still screen | 90 s, survived |
| Chatarra in the booth, radio up, only the blinking dot moving | 3 min, survived |
| Chatarra in a **battle** over the link: radio up AND the panel pushed hard | rebooted |

So the radio alone is not enough and the drawing alone is not enough: it takes
both at once, which is what makes the battle the place it shows up. The other
watch, on the same firmware and doing the same thing, did not crash.

Nothing in `ch_link.c` can fix that, and nothing in it should pretend to. What
this section is for is that the next person who sees a board reboot in a link
app looks at the SPI bus lock first instead of at the protocol.

And it is **not** systematic: a full game between the two watches was played
afterwards and finished clean — one watch at 17/40, the other at 0/36, both
back in the booth saying good fight.

<p align="center">
  <img src="../../docs/img/photo-chatarra-link-battle.jpg" width="300" alt="The same battle seen from both watches">
  <img src="../../docs/img/photo-chatarra-link-end.jpg" width="300" alt="Back in the booth afterwards, 17/40 against 0/36">
</p>

### 11.5 What this update is worth watching for

- **`.text` went from 38,475 to 49,059 B.** Over the old 48 KB reservation, and
  it does not matter: see section 1. On a firmware built without the MMU option
  it would matter a great deal.
- **A save from before today converts on load**, keeps everything, and comes up
  with a team of one.
- **The world check found the first booth placement wrong** the moment it ran:
  the one in Ciudad Malla was sitting on the cell a door from Llanura Muerta
  lands on. That is the fourth time `CH_CHECK=1` has caught something the
  compiler could not, and the first time it caught something the same day it was
  written.

---

## 12. The music: a synthesiser, not a sequencer

`ch_sound.c` was a one-voice sequencer over `aos_hal_beep()`, because that was
all the board had: one tone at a time, an API of (frequency, duration).
v0.4.3 added `aos_hal_spk_*` for the walkie — PCM in, a ring of one second in
PSRAM, a task feeding the codec 20 ms at a time. That is a different
instrument, and this file now plays it: **three voices and a drum**,
synthesised a frame at a time.

- **Lead** and **bass** are square waves by phase accumulator, at 1/4 and 1/2
  duty so two square waves can be told apart. The lead lines are the melodies
  this game always had; the bass is a short loop of roots that runs
  **independently** of the lead, which is both what chiptune does and what
  keeps the table worth having — three lines of data buy the difference between
  a tune and a piece of music.
- **Drums** are a 15-bit shift register and a string, one character per quaver:
  `K` kick, `S` snare, `h` hat, `.` nothing. `"K.h.S.h.K.h.S.h."` is the
  combat.
- Envelopes are two straight lines. A note that starts at full volume clicks;
  two milliseconds of ramp is inaudible as a ramp and removes it.

**And the consequence that decided the design.** The tone task checks
`s_spk_task` and stays quiet while the streaming speaker holds the codec
(`aos_hal_esp32.c`). So the moment this file opens the speaker, **every
`aos_hal_beep()` in the game goes silent** — the hits, the taps, the chest.
That is not a problem to work around: the effects became a **fourth voice of
the same mix**, and along the way they got an envelope, which is what stops
them sounding like a microwave. `ch_sfx()` is still the one call the game
makes; if the speaker cannot be opened it falls back to the beeper and the
game is exactly what it was.

**Confirmed on the board, and it had to be**: with the synthesiser holding the
codec, touching your own robot to open the menu still clicks. That is the one
thing the simulator could not prove — its speaker is a stub — and the one
thing that would have been invisible if it had broken, because a map with no
music and no effects sounds exactly like a map with no music.

Two details that are not obvious and are worth keeping:

- **The clock is the sample, not the frame.** The old sequencer counted frames
  because a frame was the only clock it had. Here a note lasts exactly as long
  as it says even when a frame of the game runs late, which is why the music
  does not wobble when the board is busy.
- **The ring is topped up towards a target** (150 ms) rather than filled on a
  schedule, so a slow frame is absorbed by the buffer instead of becoming a
  gap. And the speaker is opened on the first TICK, not in `create()`:
  `aos_hal_spk_open()` waits up to 800 ms for the microphone and the codec's
  own open costs about 200, and that is time better spent on the title screen
  than in front of a black one.

**What it costs, measured on the board** with `/api/mem?fps=N`, the
synthesiser holding the codec and a tune playing:

| Screen | Before the music | With the music |
| --- | ---: | ---: |
| Map (effects only, no tune) | 29.4 fps | **29.4 / 28.8 fps** |
| Combat, combat theme playing | 27.4 / 28.6 fps | **28.5-29.5 / 28.5-28.8 fps** |

Nothing. Four voices of integer arithmetic over 528 samples is below the
noise of the measurement — three consecutive samples of the same screen span
a whole frame per second on their own. Which is what the arithmetic said, and
this time the board agreed with it.

A number that is NOT a measurement of anything: the title screen reads about
8 fps, because nothing on it animates and `LV_EVENT_RENDER_READY` only fires
when LVGL actually draws. A static screen has a low frame rate by definition.

**How to hear it without the board.** The simulator's speaker is a stub that
swallows the samples, so `CH_WAV=/tmp/x.pcm` writes what the synthesiser
produced and `tools/pcm2wav.py` puts a header on it. `CH_MEL=<n>` forces a
tune so it can be listened to without playing up to it.
