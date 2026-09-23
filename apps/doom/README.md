# Doom

Yes, it runs Doom. Chocolate Doom, through
[doomgeneric](https://github.com/ozkl/doomgeneric), as an ordinary app from the
card: a 408 KB `.so` that the firmware loads like any other, with no line of
the firmware changed for it. E1M1 runs at **35 fps on the board** (measured
from the start of the level), which is Doom's own tic rate and so its
ceiling; with the cap lifted the renderer did 43-61 fps in the attract demos.

| | | |
|---|---|---|
| <img src="../../docs/img/app-doom-demo.png" width="200"><br>The attract demo, as Doom plays it on start-up. The picture is 320x200 scaled to 368x230. | <img src="../../docs/img/app-doom-e1m1.png" width="200"><br>E1M1, Hangar. The pad sits below the picture, above where the glass stops reading reliably. | <img src="../../docs/img/app-doom-map.png" width="200"><br>The automap, from the MAP button. |

## Setting it up

The game data is not in the app and not in the repository: copy a WAD to the
card's `doom/` folder. The portal's file explorer does it (`sd/doom`, the
folder is created on the first run too), or by hand:

```bash
curl -X POST "http://<watch>/api/mkdir?dir=sd&name=doom"
curl -X POST "http://<watch>/api/upload?dir=sd/doom&name=doom1.wad" --data-binary @doom1.wad
```

The shareware `DOOM1.WAD` (episode 1, 4.2 MB, freely distributable) is what it
was tested with. The full games are searched first, in this order: `doom.wad`,
`doom2.wad`, `doomu.wad`, `plutonia.wad`, `tnt.wad`, then `doom1.wad` and the
Freedoom IWADs. The portal takes uploads up to 8 MB; a bigger WAD goes through
the card in USB disk mode or a card reader.

The config (`default.cfg`) and the saves (`saves/`) land in the same folder.

## Playing

- **The stick**, bottom left, appears where the thumb lands. It is analogue:
  a small push turns and walks slowly, the edge runs and turns like the
  keyboard's fast turn. It always runs (Doom's old `joyb_speed 31`).
- **FIRE** fires, and so does **pressing the picture**. The touch is one
  finger, so walking and shooting at once takes the **side button**: it fires
  while held.
- **USE** opens doors and presses switches.
- **MENU** is Doom's own menu (Escape). In a menu the stick moves the cursor,
  FIRE picks (Enter), USE goes back; on a yes/no question FIRE is *yes* and
  USE is *no*.
- **MAP** is the automap, **GUN** the next weapon.
- Saving: the slot gets a name (`SLOT 1`...) and one more FIRE confirms it,
  since there is no keyboard.
- **Quit Game** in Doom's menu returns to the watch. Swiping back is off
  while playing; if the game ever stopped answering, holding the side button
  for five seconds with no finger on the glass leaves.

Sound effects play through the speaker. There is no music yet.

## How it is put together

```
main/doom.c          the app: the pad, the blit, the life cycle (LVGL's task)
main/doom_port.h     the seam between the two sides, nothing but plain memory
main/port/           the platform layer doomgeneric asks for
    dg_system.c      the engine's life: worker, exit(), memory, files, stdout
    dg_video.c       I_VideoBuffer -> RGB565 in three frame slots
    dg_input.c       the pad as keys, the stick as a mouse
    dg_sound.c       the effects mixer
    dg_compat.h      what the engine sees instead of the C library
main/doomgeneric/    the engine, vendored (see below)
```

**Two tasks, two cores.** The engine runs in the app's worker on core 0
(`aos_hal_worker_start_on`), and never touches LVGL or the SPI. Each finished
frame is scaled into one of three slots; LVGL's task, pinned to core 1 with the
panel, takes the newest one every 8 ms and pushes it straight to the panel
(`aos_hal_display_blit`: 368x230 is half the bytes of the full screen the
Video app measured at 16.5 ms). The slots change hands with a
compare-and-swap, and a frame nobody took yet is simply overwritten by a newer
one, so the engine never waits on the display. The pad below is plain LVGL
objects that only redraw when a button lights.

**exit() is a longjmp.** Chocolate Doom has no way out but `exit()`, from the
Quit menu, from `I_Error` or anywhere else; on the watch there is no process to
end. `dg_compat.h`, included by every engine file through `doomtype.h`,
reroutes it to a `longjmp` back to the top of the worker, and so does a stop
asked by the app when it closes. From there every block the engine allocated
and every file it opened is given back: the heap is not the `.so`'s and would
outlive it. Measured: after playing, the PSRAM free is within 200 bytes of
what it was before opening the app.

**Memory.** The engine's heap is PSRAM, always (small blocks would otherwise go
to internal RAM, `CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL`). Doom's zone takes what
the PSRAM gives in one block, up to 6 MB and leaving 768 KB for the watch: 5.25
MB on a v0.5.5 watch, more than DOS Doom ever had. The worker's stack is 16 KB
of internal RAM; its measured peak is 3.9 KB.

**The stick is a mouse.** Doom adds a mouse's motion to the tic it is building
(`angleturn -= mousex * 8`, `forward += mousey`), so posting one `ev_mouse` per
tic makes an analogue stick with no change to the game. In the menus and on
the screens between levels the stick becomes the arrow keys, with auto-repeat.

**One frame per tic.** `TryRunTics` returns to redraw after a tic's worth of
waiting even when no tic ran, and drawing the same state again is a whole core
for nothing: 46 frames a second for 35 tics. The loop now draws only when
`gametic` moved.

## The engine

`main/doomgeneric/` is doomgeneric at
`dcb7a8dbc7a16ce3dda29382ac9aae9d77d21284`, only the files the watch uses
(the SDL, X11, Allegro and Windows back ends and the original `i_video.c` are
left out). Every change is marked `AmoledOS:` in the source:

| File | Change |
| --- | --- |
| `doomtype.h` | includes `port/dg_compat.h` |
| `doomfeatures.h`, `i_sound.c` | sound on, through `port/dg_sound.c`, no SDL_mixer |
| `i_system.c` | the zone from `dg_zone_alloc()`; `I_Quit` and `I_Error` leave through the port |
| `m_config.c` | config and saves in `<card>/doom/`, the saves folder without a dot |
| `m_controls.c` | always run; `[` and `]` bound to previous/next weapon |
| `m_menu.c` | a name for an empty save slot; `dg_menu_state()` for the pad |
| `d_main.c` | no ENDOOM; draw only when a tic ran |
| `doomgeneric.c` | no 1 MB RGBA screen |
| `i_input.c` | the stick, once per tic |
| `v_video.c` | a float compare instead of a double one |

The simulator builds the engine and the port as a library of their own
(`sim/CMakeLists.txt`), without our warnings, and the app on top. In the
simulator the engine's globals live as long as the process, so Doom runs once
per simulator run; on the board every open is a fresh `dlopen`.

## Traps

- **A byte swap that called itself.** `sha1.c` shifts bytes around and gcc
  turns that into `__bswapsi2`, which the firmware does not lend; the port
  supplies it. Written with the same shifts, gcc recognised the idiom inside
  the replacement too and compiled it into a call to itself. The recursion ate
  the worker's stack and then the kernel's lists beside it, and the board
  died three different ways, always on the *other* core: a corrupted TCB in
  the tick interrupt, the scheduler reading `0xa5a5a5a5`, an `assert` in the
  SPI driver. The give-away was the backtrace: one address of the `.so`
  repeated with frames 32 bytes apart. It is built at `-O0` now, and
  `objdump` shows no call in it.
- **`build_apps.sh` checks every undefined symbol**, and it caught the only
  two the engine needed from outside: that `__bswapsi2` and a `double`
  compare in `v_video.c`. Everything else is in the firmware's table, so the
  app works on any firmware from v0.4.10 (the worker on a chosen core).
- **The screenshot does not see the game.** The portal's capture and the
  simulator's shot read LVGL's pixels, and the picture goes to the panel past
  LVGL. The images above are from the simulator, which draws through a canvas.

## Licence

Doom's source code, and so this app, is under the **GNU GPL v2**
(`main/doomgeneric/LICENSE`); the rest of AmoledOS is MIT. The WAD files are
not part of it: the shareware one is freely distributable, the commercial
ones are not.
