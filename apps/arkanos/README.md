# ARKANOS

Brick breaking for AmoledOS. Twelve walls, falling capsules, a final boss.
A dynamic app: it produces an `arkanos.so` that gets copied to `/sdcard/apps/`.

```bash
./tools/build_apps.sh arkanos
cp apps/arkanos/build/arkanos.so /Volumes/<sd>/apps/
```

## How it plays

- **Touch**: dragging your finger anywhere over the field moves the paddle.
  Touching the screen also releases the ball.
- **Sensor**: tilt the board. The position is absolute (board level = centre)
  and a tap recalibrates the zero. The axis chip picks which of the
  accelerometer's two axes runs across the width of the screen and with what
  sign: `EJE X+`, `X-`, `Y+`, `Y-`. It is in the menu **and in the pause**,
  because if the paddle goes the wrong way you find out while playing.

  > The default is **Y-**, both halves measured on the board on 2026-08-28:
  > `imu.ax` turned out to be the **pitch** axis (tilting the screen forward and
  > back moved the paddle), so the one that moves it is `imu.ay`, and the sign
  > that feels natural is the negative one. Before having the board the code
  > assumed the opposite, and `aos_app_level.c`, `aos_app_activity.c` and
  > `apps/g2043/` still assume it. The measured axes are written down in
  > [`docs/HARDWARE.md`](../../docs/HARDWARE.md).
- **Side button**: releases the ball and, with the LASER upgrade, fires.
- **Holding the scoreboard down** (the top strip) opens the pause, which is
  where the exit-to-watch button lives. It is a hold and not a tap because your
  finger flies across the screen moving the paddle, and a brush against that
  strip used to end the game by accident. While the finger is down the strip
  lightens and the level slot reads PAUSA, so you can see the gesture was
  registered; at 400 ms (`LV_INDEV_DEF_LONG_PRESS_TIME`) it opens.

The control is chosen when the app opens and is remembered.

### Bricks

| | |
| --- | --- |
| colours | one hit |
| silver | two hits, cracking as it goes |
| gold | three hits |
| steel | unbreakable, and you do not need to break it to clear the level |
| bomb | blows up its eight neighbours, and bombs chain |
| `?` | always drops a capsule |

### Capsules

`A` wide, `L` slow ball, `T` three balls, `D` laser, `I` magnet (the ball stays
stuck until you let it go), `V` one life, `P` 500 points.

Upgrades are lost when you lose a ball, as in the original: otherwise a wide
paddle with a laser turns any mistake into a free one.

### Scoring

Each brick has its own. Breaking several in a row without the ball touching the
paddle builds a **combo** and multiplies; the `X6`, `X12`, `X20` sign tells you.
Finishing a level gives 1000 plus 250 per life. After level 12 you go back to
the first one, faster, and the lap shows in the scoreboard (`N1-2`).

## What is different about it inside

The other canvas games in this repo (2043, claudito) redraw the whole screen
every frame. An arkanoid does not need that: the wall stands still and the only
things moving are the ball, the paddle, the odd capsule and the shards of the
brick that just broke. So this one carries a **dirty-rectangle list**.

There are two 184x224 buffers:

- `bg` — sky, stars, walls and bricks. Rebuilt only when a brick changes.
- `fb` — the frame you see.

And per frame:

1. restore from `bg` into `fb` the rectangles that got dirtied last frame;
2. draw what moves, noting down every rectangle;
3. upscale x2 into `big` and invalidate **only** the union of the two sets, with
   `lv_obj_invalidate_area()`.

**Measured with `tools/ak_harness.c`: 3 % of the screen per frame on average.**
The upscaling and LVGL's drawing cost that much instead of the 165 thousand
pixels of the full screen.

Design consequences, worth bearing in mind before adding anything:

- **There is no screen shake.** Moving the canvas invalidates two whole screens.
  Hits are felt through local waves and flashes.
- **The background is not animated.** A moving sky forces a full repaint.
- **Everything that moves has to note down its rectangle**, and the background
  has to be repaintable rectangle by rectangle (which is why the stars live in a
  table and are not drawn at random on the fly).

That last point is THE possible mistake in this scheme, and on screen it looks
like a dirty trail stuck there. That is what the test bench is for.

## Test bench without a screen

`tools/ak_harness.c` compiles the game without LVGL or SDL (the game's four
modules depend only on the HAL, and of that only on the accelerometer), makes it
play itself for thousands of frames and **draws twice on each one**: through the
dirty-rectangle path and by rebuilding the whole screen. If the two buffers do
not come out identical, something moved without noting itself down.

```bash
cc -O2 -I apps/arkanos/main -I components/aos_hal/include \
   apps/arkanos/tools/ak_harness.c apps/arkanos/main/ak_*.c -o /tmp/akh

/tmp/akh 60000            # 60 thousand frames, the twelve screens and lap 2
AK_TORPE=1 /tmp/akh 30000 # it also lets itself lose, to reach the game over
/tmp/akh 900 11 /tmp/jefe # dumps .ppm captures of level 12
```

It starts by printing the **accelerometer mapping table**, which doubles as
proof that the axis chip does what it says:

```
  EJE X+ :  ax +0.2g -> derecha     ay +0.2g -> quieta
  EJE X- :  ax +0.2g -> izquierda   ay +0.2g -> quieta
  EJE Y+ :  ax +0.2g -> quieta      ay +0.2g -> derecha
  EJE Y- :  ax +0.2g -> quieta      ay +0.2g -> izquierda
```

It also prints how much gets pushed per frame, which is the number that
justifies the whole scheme.

## Simulator switches

```bash
cd sim && cmake --build build -j8
AOS_SIM_VIEW=demo.arkanos ARK_AUTO=1 ARK_FPS=1 ./build/amoledos_sim
```

| Variable | What for |
| --- | --- |
| `ARK_AUTO=1` | the paddle plays itself |
| `ARK_LEVEL=8` | starts on that level |
| `ARK_LIVES=1` | to reach the end-of-game sign quickly |
| `ARK_FPS=1` | shows frames per second and % of screen pushed |

The FPS counter can also be turned on from the pause, and **it is the first
thing to look at on the board**.

## Files

| | |
| --- | --- |
| `ak_pixel.c` | primitives, the 5x7 font and the dirty-rectangle list |
| `ak_level.c` | brick types, capsules and the twelve maps |
| `ak_play.c` | ball, paddle, collisions, capsules, state machine |
| `ak_draw.c` | background, scoreboard and everything that moves |
| `arkanos.c` | the app: buffers, LVGL signs, input and the loop |
| `tools/ak_harness.c` | the test bench above |

## Adding a level

A map 11 characters wide and up to 13 rows, plus a row in `ak_levels[]` with a
name, ball speed, capsule probability and sky colours. There is no per-level
code anywhere.

```c
static const char *const lv_mio[] = {
    "1.1.1.1.1.1",
    ".HHHHHHHHH.",
    "....BMB....",
};
...
{ "MI MURO", LV(lv_mio), 60, 28, 0x0B1026, 0x1B2350, 0x4A9DF5, 24 },
```
