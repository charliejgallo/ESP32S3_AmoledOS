# Monster Hop — zones for later

Two zones were planned from the start and kept for later: a **witch swamp**
and a **skeleton graveyard**. The art of the first four stays clear of them
on purpose (no graveyards, tombstones or skeletons anywhere, see
`tools/blender/SPEC.md`), the world map already shows them as two locked
spots beyond the forest, and the album and the pack already have their
emblems (`emblem_swamp`, `emblem_graveyard`). Each zone below can be built as
it stands: the look, what Blender has to model, what is new for the rules,
and then the list of places in the code a fifth and sixth zone touch.

## The Witch Swamp

**The idea.** A bog at dusk under a green moon: black water, boardwalks on
stilts, crooked cypress trees hung with moss, a witch's hut on chicken legs,
cauldrons bubbling on the banks, will-o'-the-wisps drifting in the fog. Teal
and murky green with purple light; the glow pass does the wisps and the
cauldrons.

- **Blocks.** Mud, moss, boardwalk planks (along X and Y), bog water (a
  surface, like the moat) and **bog** as quicksand (`%`, `CK_QUICK`, already
  in the rules: it sinks you if you stand still).
- **Props.** Cypress with moss (three shapes), a cauldron (lit, glow), toadstools,
  a broom leaning on a fence, a pumpkin lantern on a stake, a spellbook
  lectern, the hut on legs (2×2), bottles on a shelf, a crooked signpost.
- **Monsters.** **Witches** flying on brooms in straight passes over
  everything (a new flier: a lane like the bats, but one that swoops down to
  Tommy's height in the middle of its run). **Toads** as the minion, hopping
  on the grid like Tommy, one cell every so often, towards him. Lily pads
  (already in the forest) and floating logs cross the water.
- **Hazards.** Poison bubbles that burst from the water on a timer (the steam
  vent's rule with a new sprite), bog, and cauldrons that spit a splash over
  the next cell.
- **Lair: the Witch Queen.** She circles her cauldron and every few seconds
  turns the cells around her into bog for a moment (a timed change of cells,
  like the lever groups, but on a clock).
- **Music.** A slow 6/8 with a low drone and a harpsichord-like pulse wave.

## The Skeleton Graveyard

**The idea.** A hilltop cemetery under a full moon: iron gates, crooked
tombstones, crypts, dead trees, candles on graves, mist along the ground. The
coolest zone: moonlit blue-grey with warm candlelight.

- **Blocks.** Grave dirt, flagstone paths, crypt stone (walls with doors),
  grass with mist, a church floor for the lair.
- **Props.** Tombstones (four shapes), crosses, an angel statue, a crypt
  front, iron fence (X and Y, joining like the city's), a dead tree, candles,
  a lantern on a post, a shovel in a mound, a bell tower (tall, 2×2).
- **Monsters.** **Skeletons** that walk a path and **fall apart** when they
  bump into something, lying as a harmless pile for a few seconds before
  they stand up again (the armour's pause, with its own animation).
  **Ghosts** that float through walls on straight lines and show through the
  scenery with the x-ray silhouette the renderer already draws. The minion:
  **bony hands** that come up from a grave when Tommy passes next to it.
- **Hazards.** **Open graves** (pits, `.`), which a crate fills like any
  hole; tombstones that topple onto the next cell when Tommy walks by (a
  one-shot trap); the bell, whose toll wakes every skeleton in sight.
- **Lair: the Skeleton King.** On a crypt in the middle of the lair, he
  raises skeletons from the graves around him, two at a time, and the keys
  are on the graves.
- **Music.** A minor waltz like the castle's, with a xylophone for the bones.

## What adding a zone touches

The first four zones are numbered 0-3 and the test zone is 4. **Numbers that
are stored or sent are forever**, so new zones go after the test zone
(`ZONE_SWAMP = 5`, `ZONE_GRAVEYARD = 6`) and new levels at the end of the
table (16-23), so the stars and records already saved keep their keys.

| Where | What |
| --- | --- |
| `main/mh_level.h` | the two zones after `ZONE_TEST`, before `ZONE_N`; new monsters at the end of `MON_*` (the level format stores the number) |
| `tools/levels.py` | `ZONES` and `MONSTERS` in the same order; a `tools/levels/swamp.py` and `graveyard.py` with their `zone()` legend, as the other four. `levels.py` checks reach, keys, the sticker and that nothing stands on a prop |
| `main/mh_prog.h` | `MH_NLEVELS` from 16 to 24 (the stars, records and sticker bits are per level; the stickers fit in the 32-bit mask) |
| `main/monsterhop.c` | `level_table()`, `mha_level_title()`, `mha_zone_title()`, `mha_zone_need()` (the stars to open each: 30 and 38 are in step with 6/14/22), and `music_for()` |
| `main/mh_prog.c` | the zone trophies (`TR_ZONE_*`, add two at the end of the enum), the boss loop (`z < 4`), "all the stars" and the album's count are derived from `MH_NLEVELS` except two texts: "Las 48 estrellas" and "Encuentra las 16 figuritas" |
| `main/mh_ui.c` | `coins_text()` prints `/48`; the album is a 4×4 grid (a second page or a scroll); the map's spots come from the `map_spots` blob, and the two locked spots become eight level pads each |
| `main/mh_world.c` | a row per zone in `s_looks` (sky, tint, fog) |
| `main/mh_cast.c` | the zone's name for its objects (`<zone>_gate`, `_crate`, `_spikes`...) and the new monsters' animations in `mon_anims()` |
| `main/mh_game.c`, `mh_scene.c` | only the new behaviours: the swooping flier, hopping toads, skeletons that fall apart, ghosts through walls, graves that grab, timed bog |
| `main/mh_audio.c` | a theme per zone (`MUS_*`) |
| `main/mh_link.c` | the HELLO carries the open levels in a `uint16_t`: with 24 levels it needs 32 bits, so **`PROTO` goes to 2** and both watches need the same `monsterhop.so` |
| `tools/blender/` | the art, by subagents with `SPEC.md` as for the first four: tiles, props, monsters, a boss, a `card_<monster>` per new sticker, and the world map re-rendered with the two zones open |
| `tools/pack_assets.py` | nothing: it packs every `assets/*/meta.json`; the pak grows by ~3 MB per zone and more parts appear in `assets/card/` by themselves |

**Memory.** A level holds its zone's blocks and props, its monsters and the
common objects: 3.4-3.6 MB for the first four zones, which leaves 888 KB of
PSRAM free while playing. Keep a new zone's level at that size or below (a
prop is 5-50 KB with its shadow and up to 100 KB with a glow pass; a monster
with all its animations in four facings is ~380 KB, so a level's mix of
monsters is the big number), or the free memory falls under what the
capture and the link need.
