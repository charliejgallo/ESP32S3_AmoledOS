"""Mila - the world map (SPEC.md section 7, DESIGN.md section 4).

    /Applications/Blender.app/Contents/MacOS/Blender -b -P map.py -- --out ../../assets/map [--only map_living,...]

A vertical strip of panels, 368 px wide, the game camera at zoom 0.45 under
C.reset('map'), colour only. Every panel is a little diorama on a thick base
(a toy on the black screen). The paw-print path ENTERS at the bottom centre
(x 184, over three little steps down the base's front) and LEAVES at the top
centre (through a gap in the back wall, then over the black), so panels chain
in any order. The stones are drawn by the watch at meta extra.nodes.

Sprites: map_home, map_living (phase 1), map_stone, map_stone_done,
map_stone_locked, map_mila.
"""
import math

import bmesh
import os
import random
import sys
import time

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ml_common as C  # noqa: E402
import mapui_kit as K  # noqa: E402
from mathutils import Vector as V  # noqa: E402

ZOOM = 0.45
W = 368
PXM = C.S * ZOOM                   # 32.4 px per metre along X
PYM = 54 * ZOOM                    # 24.3 px per metre along Y
PZM = C.FLOOR_PX / C.FLOOR_M * ZOOM  # 21.4 px per metre up

HALF_W = 5.35        # the base spans x -5.35 .. 5.35 (11 px of black each side)
BASE_T = 0.5         # the base's thickness (its front face shows ~11 px)
STEP_W = 0.55        # the entry steps span x -0.55 .. 0.55
GAP_W = 0.6          # the exit gap in the back wall spans x -0.6 .. 0.6

META = {}
TIMES = {}


def px(p, ay, ax=W // 2):
    """Pixel of world point p in a panel whose world origin lands on (ax, ay)."""
    sx, sy = C.to_screen(V(p))
    return [int(round(ax + sx * ZOOM)), int(round(ay + sy * ZOOM))]


def render(out, name, objs, size, samples, anchor=V((0, 0, 0)), zoom=ZOOM, **kw):
    t = time.time()
    info = C.render_sprite(out, name, objs, anchor, passes=('color',), size=size, samples=samples, zoom=zoom, **kw)
    META[name] = dict(info)
    TIMES[name] = time.time() - t
    print('rendered %s %dx%d in %.1fs' % (name, info['w'], info['h'], TIMES[name]))
    return info


# ---------------------------------------------------------------------------
# the base every panel stands on, the steps in and the trail
# ---------------------------------------------------------------------------

def base(depth, top_key, side_key, trim_key=None, bevel=0.06, top_t=0.06, top_bevel=0.02):
    o = [K.box('base', -HALF_W, 0, -BASE_T, HALF_W, depth, -top_t + 0.03, side_key, bevel=bevel),
         K.box('floor', -HALF_W + 0.01, 0.01, -top_t, HALF_W - 0.01, depth - 0.01, 0.0, top_key, bevel=top_bevel)]
    if trim_key:
        o.append(K.box('trim', -HALF_W - 0.01, -0.02, -0.16, HALF_W + 0.01, depth, -0.08, trim_key, bevel=0.02))
    return o


def steps(key, n=3, depth=0.25, rise=0.14):
    """Little steps down the base's front at the centre: the way in."""
    o = []
    for k in range(1, n + 1):
        o.append(K.box('step%d' % k, -STEP_W, -depth * k, -BASE_T, STEP_W, -depth * (k - 1) + 0.02, -rise * k, key,
                       bevel=0.025))
    return o


def step_z(x, y, n=3, depth=0.25, rise=0.14):
    if abs(x) > STEP_W:
        return None
    k = int(math.ceil(-y / depth))
    if k < 1 or k > n:
        return None
    return -rise * k


# ---------------------------------------------------------------------------
# living room props (sample.py's, map-sized)
# ---------------------------------------------------------------------------
YARN = [(0.95, 0.40, 0.60), (0.25, 0.75, 0.78), (0.98, 0.78, 0.25), (0.62, 0.48, 0.95)]


def yarn_ball(name, c, col, r=0.32, z0=0.0, thread=True, seed=1):
    m = K.yarn('yarn_%d%d%d' % tuple(int(v * 9) for v in col), col)
    z = z0 + r
    o = [K.ellipsoid(name, (c[0], c[1], z), (r, r, r * 0.97), m, rot=(20, 35, 10))]
    rnd = random.Random(seed)
    for k in range(3):
        o.append(K.torus(name + 'w%d' % k, (c[0], c[1], z), r * 0.975, 0.016, m,
                         rot=(rnd.uniform(0, 180), rnd.uniform(0, 180), rnd.uniform(0, 180)), n=32, m=8))
    if thread:
        a = rnd.uniform(-2.6, -0.5)       # towards the camera, so it shows
        p0 = V((c[0] + r * 0.7 * math.cos(a), c[1] + r * 0.7 * math.sin(a), z0 + 0.12))
        pts = [tuple(p0), (p0.x + 0.2 * math.cos(a), p0.y + 0.2 * math.sin(a), z0 + 0.02),
               (p0.x + 0.4 * math.cos(a + 0.7), p0.y + 0.4 * math.sin(a + 0.7), z0 + 0.015),
               (p0.x + 0.6 * math.cos(a + 0.1), p0.y + 0.6 * math.sin(a + 0.1), z0 + 0.015),
               (p0.x + 0.75 * math.cos(a - 0.5), p0.y + 0.75 * math.sin(a - 0.5), z0 + 0.015)]
        o.append(K.tube(name + 'str', pts, [0.022] * 5, m))
    return o


def basket(name, c, s=1.0, with_yarn=None):
    wick = K.wood('wicker', (0.62, 0.38, 0.16), 6)
    wick2 = K.pm('wicker2', (0.80, 0.56, 0.28), rough=0.7)
    cush = K.pm('cushion', (0.92, 0.36, 0.42), rough=0.9, sheen=1.0)
    pawm = K.pm('pawpink', (1.0, 0.85, 0.88), rough=0.9)
    x, y = c
    o = [K.cone(name + 'b', (x, y, 0.0), 0.35 * s, 0.43 * s, 0.18 * s, wick),
         K.torus(name + 'rim', (x, y, 0.18 * s), 0.42 * s, 0.05 * s, wick2),
         K.ellipsoid(name + 'cu', (x, y, 0.16 * s), (0.36 * s, 0.36 * s, 0.05 * s), cush)]
    if with_yarn is None:
        o += K.paw(name + 'pp', (x, y - 0.02), math.pi / 2, 1.6 * s, pawm, z=0.2 * s, t=0.01)
    else:
        o += yarn_ball(name + 'y', c, with_yarn, r=0.3 * s, z0=0.12 * s, thread=False, seed=3)
    return o


def plant(name, c, s=1.0, seed=5):
    pot = K.pm('pot', (0.86, 0.46, 0.32), rough=0.6)
    soil = K.pm('soil', (0.2, 0.12, 0.07), rough=1)
    leaf = K.pm('leaf', (0.22, 0.62, 0.30), rough=0.5, spec=0.4)
    leaf2 = K.pm('leaf2', (0.36, 0.76, 0.38), rough=0.5, spec=0.4)
    x, y = c
    o = [K.cone(name + 'pot', (x, y, 0), 0.30 * s, 0.40 * s, 0.44 * s, pot),
         K.torus(name + 'lip', (x, y, 0.44 * s), 0.40 * s, 0.05 * s, pot),
         K.ellipsoid(name + 'soil', (x, y, 0.42 * s), (0.37 * s, 0.37 * s, 0.03 * s), soil)]
    rnd = random.Random(seed)
    for k in range(10):
        a = 2 * math.pi * k / 10 + rnd.uniform(-0.2, 0.2)
        r = rnd.uniform(0.18, 0.34) * s
        o.append(K.ellipsoid(name + 'lf%d' % k, (x + r * math.cos(a), y + r * math.sin(a),
                                                 (0.66 + rnd.uniform(0, 0.28)) * s),
                             (0.30 * s, 0.11 * s, 0.045 * s), leaf if k % 2 else leaf2,
                             rot=(0, rnd.uniform(-40, -10), math.degrees(a))))
    return o


def sofa(name, x0, y0, x1, y1):
    fab = K.pm('sofa', (0.88, 0.40, 0.34), rough=0.85, sheen=0.8)
    fab2 = K.pm('sofa2', (0.95, 0.52, 0.44), rough=0.85, sheen=0.8)
    leg = K.wood('sofaleg', (0.35, 0.22, 0.12))
    pil = K.pm('pillow', (0.98, 0.84, 0.45), rough=0.9, sheen=1)
    o = []
    for lx in (x0 + 0.1, x1 - 0.15):
        for ly in (y0 + 0.12, y1 - 0.17):
            o.append(K.box(name + 'lg%.1f%.1f' % (lx, ly), lx, ly, 0, lx + 0.06, ly + 0.06, 0.1, leg))
    o.append(K.box(name + 'base', x0 + 0.03, y0 + 0.03, 0.1, x1 - 0.03, y1 - 0.03, 0.34, fab, bevel=0.08))
    o.append(K.box(name + 'back', x0 + 0.05, y1 - 0.32, 0.3, x1 - 0.05, y1 - 0.05, 0.78, fab, bevel=0.09))
    for sx in (x0 + 0.03, x1 - 0.25):
        o.append(K.box(name + 'arm%.1f' % sx, sx, y0 + 0.06, 0.3, sx + 0.22, y1 - 0.06, 0.55, fab, bevel=0.08))
    n = 2
    cw = (x1 - x0 - 0.56) / n
    for k in range(n):
        cx = x0 + 0.28 + k * cw
        o.append(K.box(name + 'cu%d' % k, cx + 0.02, y0 + 0.1, 0.3, cx + cw - 0.02, y1 - 0.32, 0.45, fab2, bevel=0.07))
    o.append(K.ellipsoid(name + 'pil', (x0 + 0.55, y1 - 0.4, 0.6), (0.19, 0.08, 0.16), pil, rot=(-15, 0, 10)))
    return o


def armchair(name, c):
    fab = K.pm('chair', (0.40, 0.66, 0.62), rough=0.85, sheen=0.8)
    fab2 = K.pm('chair2', (0.52, 0.78, 0.72), rough=0.85, sheen=0.8)
    leg = K.wood('sofaleg', (0.35, 0.22, 0.12))
    x, y = c
    x0, x1, y0, y1 = x - 0.55, x + 0.55, y - 0.5, y + 0.5
    o = [K.box(name + 'base', x0 + 0.03, y0 + 0.03, 0.1, x1 - 0.03, y1 - 0.03, 0.34, fab, bevel=0.08),
         K.box(name + 'back', x0 + 0.05, y1 - 0.3, 0.3, x1 - 0.05, y1 - 0.04, 0.82, fab, bevel=0.1),
         K.box(name + 'cu', x0 + 0.24, y0 + 0.08, 0.3, x1 - 0.24, y1 - 0.3, 0.45, fab2, bevel=0.07)]
    for sx in (x0 + 0.03, x1 - 0.23):
        o.append(K.box(name + 'arm%.1f' % sx, sx, y0 + 0.05, 0.3, sx + 0.2, y1 - 0.05, 0.56, fab, bevel=0.08))
    for lx in (x0 + 0.1, x1 - 0.16):
        o.append(K.box(name + 'lg%.1f' % lx, lx, y0 + 0.1, 0, lx + 0.06, y0 + 0.16, 0.1, leg))
    return o


def side_table(name, c):
    wd = K.wood('table', (0.62, 0.40, 0.22))
    shade = K.pm('lampshade', (1.0, 0.92, 0.7), rough=0.9, emit=(1.0, 0.8, 0.45), es=1.6)
    brass = K.pm('brass', (0.9, 0.7, 0.35), rough=0.3, metal=1)
    book = K.pm('book', (0.25, 0.4, 0.8), rough=0.7)
    x, y = c
    o = [K.cone(name + 'top', (x, y, 0.36), 0.42, 0.42, 0.06, wd, bevel=0.015),
         K.cone(name + 'leg', (x, y, 0.0), 0.05, 0.05, 0.36, wd),
         K.cone(name + 'foot', (x, y, 0.0), 0.2, 0.18, 0.03, wd),
         K.cone(name + 'lampb', (x + 0.06, y + 0.08, 0.42), 0.08, 0.035, 0.22, brass),
         K.cone(name + 'shade', (x + 0.06, y + 0.08, 0.62), 0.17, 0.10, 0.2, shade),
         K.box(name + 'book', x - 0.3, y - 0.2, 0.42, x - 0.06, y + 0.02, 0.47, book, bevel=0.005)]
    C.add_point_light((x + 0.06, y + 0.08, 0.72), color=(1.0, 0.75, 0.45), power=40, radius=0.08)
    return o


def bookcase(name, x0, y0, x1, y1, h=1.0):
    wd = K.wood('shelf', (0.55, 0.34, 0.20))
    inside = K.pm('shelfin', (0.30, 0.18, 0.10), rough=0.8)
    o = [K.box(name + 'c', x0, y0 + 0.22, 0, x1, y1, h, wd, bevel=0.02),
         K.box(name + 'in', x0 + 0.07, y0 + 0.2, 0.07, x1 - 0.07, y0 + 0.24, h - 0.07, inside),
         K.box(name + 'sl', x0, y0, 0, x0 + 0.07, y0 + 0.24, h, wd, bevel=0.01),
         K.box(name + 'sr', x1 - 0.07, y0, 0, x1, y0 + 0.24, h, wd, bevel=0.01),
         K.box(name + 'tp', x0, y0, h - 0.07, x1, y0 + 0.24, h, wd, bevel=0.01),
         K.box(name + 'bt', x0, y0, 0, x1, y0 + 0.24, 0.07, wd, bevel=0.01)]
    o.append(K.box(name + 'mid', x0 + 0.05, y0, h / 2 - 0.03, x1 - 0.05, y0 + 0.24, h / 2 + 0.02, wd))
    cols = [(0.90, 0.30, 0.30), (0.25, 0.55, 0.92), (0.98, 0.78, 0.25), (0.35, 0.75, 0.45), (0.72, 0.45, 0.88),
            (0.98, 0.55, 0.35)]
    rnd = random.Random(9)
    for sh, z in enumerate((0.07, h / 2 + 0.02)):
        xx = x0 + 0.1
        while xx < x1 - 0.18:
            wdt = rnd.uniform(0.07, 0.12)
            hgt = rnd.uniform(0.28, 0.38) * h
            m = K.pm('bk%d' % rnd.randrange(6), cols[rnd.randrange(6)], rough=0.6)
            o.append(K.box(name + 'b%.2f' % xx, xx, y0 + 0.02, z, xx + wdt, y0 + 0.2, z + hgt, m, bevel=0.006))
            xx += wdt + 0.01
    return o


def fireplace(name, x0, x1, y, h=1.05):
    brick = K.stripes('fpbrick', (0.93, 0.62, 0.52), (0.98, 0.70, 0.60), width=0.07, axis='Z', rough=0.8)
    cream = K.pm('fpcream', (0.97, 0.93, 0.86), rough=0.6)
    soot = K.pm('fpsoot', (0.10, 0.06, 0.05), rough=1)
    fire = K.pm('fire', (1.0, 0.55, 0.15), emit=(1.0, 0.45, 0.08), es=6.0)
    fire2 = K.pm('fire2', (1.0, 0.85, 0.35), emit=(1.0, 0.80, 0.30), es=8.0)
    logs = K.wood('logs', (0.45, 0.27, 0.15), 3)
    top = K.wood('walltop', (0.16, 0.09, 0.06), 3)
    xm = (x0 + x1) / 2
    ow = (x1 - x0) * 0.26
    o = [K.box(name + 'l', x0, y - 0.4, 0, xm - ow, y, h - 0.1, brick, bevel=0.02),
         K.box(name + 'r', xm + ow, y - 0.4, 0, x1, y, h - 0.1, brick, bevel=0.02),
         K.box(name + 'm', xm - ow, y - 0.4, 0.62, xm + ow, y, h - 0.1, brick, bevel=0.02),
         K.box(name + 'in', xm - ow, y - 0.3, 0.0, xm + ow, y, 0.62, soot),
         K.box(name + 'sh', x0 - 0.1, y - 0.52, h - 0.1, x1 + 0.1, y + 0.02, h, top, bevel=0.02),
         K.box(name + 'hearth', x0 - 0.05, y - 0.62, 0, x1 + 0.05, y - 0.35, 0.06, cream, bevel=0.02)]
    for k, dx in enumerate((-0.12, 0.12)):
        o.append(K.cone(name + 'log%d' % k, (xm + dx, y - 0.2, 0.08), 0.06, 0.06, 0.5, logs, rot=(0, 90, 20 - 40 * k)))
    for k, (dx, hh, key) in enumerate(((-0.1, 0.32, fire), (0.1, 0.28, fire), (0.0, 0.4, fire), (0.0, 0.22, fire2))):
        o.append(K.ellipsoid(name + 'f%d' % k, (xm + dx, y - 0.22 + k * 0.01, 0.12 + hh / 2),
                             (0.09 if key == fire else 0.06, 0.05, hh / 2), key, seg=16, rings=10))
    C.add_point_light((xm, y - 0.45, 0.3), color=(1.0, 0.55, 0.2), power=35, radius=0.15)
    # on the mantel: a little clock and a candle
    clock = K.pm('clock', (0.98, 0.84, 0.45), rough=0.4)
    face = K.pm('clockf', (1.0, 0.99, 0.95), rough=0.5)
    o.append(K.cone(name + 'clk', (x0 + 0.3, y - 0.2, h), 0.13, 0.13, 0.08, clock, rot=(90, 0, 0), verts=24))
    o.append(K.box(name + 'clkb', x0 + 0.22, y - 0.28, h, x0 + 0.38, y - 0.12, h + 0.08, clock))
    o.append(K.cone(name + 'clkf', (x0 + 0.3, y - 0.281, h + 0.14), 0.1, 0.1, 0.01, face, rot=(90, 0, 0), verts=24))
    return o


def coffee_table(name, c, r=0.55):
    wd = K.wood('ctable', (0.52, 0.32, 0.18))
    cup = K.pm('cup', (0.99, 0.97, 0.94), rough=0.3, cc=0.5)
    tea = K.pm('tea', (0.55, 0.30, 0.15), rough=0.2)
    plate = K.pm('plate', (0.55, 0.80, 0.95), rough=0.3, cc=0.5)
    cookie = K.pm('cookie', (0.90, 0.66, 0.36), rough=0.8)
    x, y = c
    o = [K.cone(name + 'top', (x, y, 0.3), r, r, 0.07, wd, verts=40, bevel=0.02)]
    for k in range(3):
        a = 2 * math.pi * k / 3 + 0.5
        o.append(K.cone(name + 'lg%d' % k, (x + r * 0.6 * math.cos(a), y + r * 0.6 * math.sin(a), 0), 0.04, 0.05, 0.3,
                        wd))
    o.append(K.cone(name + 'cup', (x - 0.18, y + 0.02, 0.37), 0.07, 0.09, 0.12, cup))
    o.append(K.cone(name + 'tea', (x - 0.18, y + 0.02, 0.48), 0.075, 0.075, 0.005, tea))
    o.append(K.cone(name + 'pl', (x + 0.16, y - 0.05, 0.37), 0.2, 0.2, 0.02, plate, verts=32))
    for k, (dx, dy) in enumerate(((0.1, -0.08), (0.22, -0.02), (0.14, 0.05))):
        o.append(K.cone(name + 'ck%d' % k, (x + dx, y + dy, 0.39), 0.07, 0.07, 0.03, cookie, verts=16))
    return o


def floor_lamp(name, c, h=1.35):
    brass = K.pm('brass', (0.9, 0.7, 0.35), rough=0.3, metal=1)
    shade = K.pm('lampshade2', (1.0, 0.86, 0.62), rough=0.9, emit=(1.0, 0.8, 0.45), es=1.8)
    x, y = c
    C.add_point_light((x, y, h - 0.1), color=(1.0, 0.75, 0.45), power=40, radius=0.1)
    return [K.cone(name + 'b', (x, y, 0), 0.2, 0.16, 0.04, brass),
            K.cone(name + 'p', (x, y, 0.04), 0.025, 0.025, h - 0.2, brass),
            K.cone(name + 's', (x, y, h - 0.25), 0.26, 0.15, 0.3, shade)]


def rug(name, c, rx, ry, col, col2):
    m = K.pm(name, col, rough=1, sheen=1)
    m2 = K.pm(name + 'b', col2, rough=1, sheen=1)
    return [K.ellipsoid(name, (c[0], c[1], 0.0), (rx, ry, 0.014), m, seg=64),
            K.ellipsoid(name + 'i', (c[0], c[1], 0.005), (rx * 0.78, ry * 0.78, 0.014), m2, seg=64),
            K.torus(name + 'r', (c[0], c[1], 0.01), 1.0, 0.012, m, scale=(rx * 0.62, ry * 0.62, 1.0), n=64, m=6)]


def back_wall(depth, x0, x1, h, paper, wain, top, gap=True, t=0.22):
    """The back wall of a room, with the exit gap at the centre (round posts)."""
    o = []
    segs = [(x0, -GAP_W), (GAP_W, x1)] if gap else [(x0, x1)]
    for a, b in segs:
        o += [K.box('bw%.1f' % a, a, depth, 0.28, b, depth + t, h - 0.08, paper),
              K.box('bwb%.1f' % a, a - 0.01, depth - 0.012, 0, b + 0.01, depth + t, 0.3, wain, bevel=0.01),
              K.box('bwt%.1f' % a, a - 0.03, depth - 0.03, h - 0.08, b + 0.03, depth + t + 0.03, h, top, bevel=0.02)]
    if gap:
        for sx in (-1, 1):
            o.append(K.cone('post%d' % sx, (sx * (GAP_W + 0.02), depth + t / 2, 0), 0.13, 0.13, h + 0.06, wain,
                            bevel=0.04))
            o.append(K.ellipsoid('postk%d' % sx, (sx * (GAP_W + 0.02), depth + t / 2, h + 0.1), (0.15, 0.15, 0.1), top))
    return o


def side_walls(depth, h, wain, top, t=0.2):
    o = []
    for sx in (-1, 1):
        a, b = (-HALF_W, -HALF_W + t) if sx < 0 else (HALF_W - t, HALF_W)
        o += [K.box('sw%d' % sx, a, 0, 0, b, depth, h - 0.07, wain, bevel=0.01),
              K.box('swt%d' % sx, a - 0.02, -0.02, h - 0.07, b + 0.02, depth + 0.02, h, top, bevel=0.02)]
    return o


def window(x0, x1, z0, z1, y, night=False):
    frame = K.wood('wframe', (0.97, 0.95, 0.9), 3)
    if night:
        glass = K.pm('nightsky', (0.05, 0.08, 0.25), emit=(0.10, 0.16, 0.45), es=1.4)
    else:
        glass = K.pm('daysky', (0.55, 0.80, 0.98), emit=(0.55, 0.80, 0.98), es=0.9, rough=0.1)
    cur = K.pm('curtain', (0.96, 0.58, 0.68), rough=0.9, sheen=1)
    xm, zm = (x0 + x1) / 2, (z0 + z1) / 2
    o = [K.box('win', x0, y - 0.03, z0, x1, y, z1, glass)]
    for a0, a1 in ((x0 - 0.06, x0), (x1, x1 + 0.06), (xm - 0.025, xm + 0.025)):
        o.append(K.box('wf%.2f' % a0, a0, y - 0.07, z0, a1, y, z1, frame))
    for b0, b1 in ((z0 - 0.06, z0), (z1, z1 + 0.06), (zm - 0.025, zm + 0.025)):
        o.append(K.box('wh%.2f' % b0, x0 - 0.06, y - 0.07, b0, x1 + 0.06, y, b1, frame))
    o.append(K.box('sill', x0 - 0.12, y - 0.16, z0 - 0.08, x1 + 0.12, y, z0 - 0.02, frame))
    for cx in (x0 - 0.12, x1 + 0.12):
        o.append(K.ellipsoid('cur%.1f' % cx, (cx, y - 0.1, zm + 0.02), (0.09, 0.06, (z1 - z0) / 2 + 0.06), cur))
    return o


def picture(x0, x1, z0, z1, y):
    frame = K.wood('pframe', (0.98, 0.82, 0.45), 3)
    bg = K.pm('picbg', (0.60, 0.84, 0.97))
    fish = K.pm('fishp', (1.0, 0.55, 0.2), rough=0.5)
    xm, zm = (x0 + x1) / 2, (z0 + z1) / 2
    return [K.box('pic', x0, y - 0.04, z0, x1, y, z1, frame, bevel=0.01),
            K.box('picin', x0 + 0.07, y - 0.05, z0 + 0.06, x1 - 0.07, y - 0.02, z1 - 0.06, bg),
            K.ellipsoid('fishp', (xm - 0.06, y - 0.06, zm), ((x1 - x0) * 0.22, 0.01, (z1 - z0) * 0.2), fish),
            K.cone('fishpt', (xm + (x1 - x0) * 0.2, y - 0.06, zm), (z1 - z0) * 0.2, 0.0, (x1 - x0) * 0.16, fish,
                   rot=(0, 90, 0), scale=(1, 0.1, 1))]


# ---------------------------------------------------------------------------
# panels
# ---------------------------------------------------------------------------

LIVING_NODES = [(-1.9, 1.8), (1.8, 2.7), (3.4, 4.9), (0.7, 6.1), (-2.7, 6.8), (-2.4, 9.3), (0.9, 9.9), (2.9, 11.3)]


def panel_living(out, samples):
    C.reset('map')
    H, AY, D = 400, 368, 13.0
    top = K.wood('walltop', (0.16, 0.09, 0.06), 3)
    paper = K.stripes('paper', (0.30, 0.55, 0.58), (0.36, 0.62, 0.64), width=0.09, axis='X')
    wain = K.pm('wain', (0.93, 0.90, 0.84), rough=0.6)
    floor = K.planks('planks', (0.80, 0.54, 0.31))
    side = K.wood('basewood', (0.36, 0.21, 0.12), 2)
    prints = K.pm('prints', (1.0, 0.95, 0.88), rough=0.7, emit=(1.0, 0.93, 0.85), es=0.25)
    o = []
    o += base(D + 0.22, floor, side, trim_key=wain)
    o += steps(floor)
    o += back_wall(D, -HALF_W, HALF_W, 1.3, paper, wain, top)
    o += side_walls(D, 0.5, wain, top)
    o += window(-4.15, -2.85, 0.62, 1.1, D)
    o += picture(-2.05, -1.1, 0.78, 1.14, D)
    o += fireplace('fp', 1.25, 2.95, D)
    # furniture along the back wall
    o += sofa('sofa', -4.8, 11.75, -2.3, 12.97)
    o += side_table('stab', (-1.6, 12.35))
    o += bookcase('bcase', 3.45, 12.35, 4.95, 12.98)
    # the room
    o += rug('rug', (0.3, 7.95), 2.35, 1.5, (0.95, 0.60, 0.70), (0.99, 0.92, 0.88))
    o += coffee_table('ctab', (0.3, 8.0))
    o += floor_lamp('flamp', (4.75, 9.45))
    o += armchair('achair', (4.25, 8.3))
    o += plant('plant1', (4.35, 1.35), 1.05, seed=5)
    o += plant('plant2', (-4.45, 4.3), 0.9, seed=8)
    o += yarn_ball('y1', (-4.0, 1.6), YARN[0], seed=1)
    o += yarn_ball('y2', (3.2, 7.2), YARN[1], seed=2)
    o += yarn_ball('y3', (-0.9, 4.2), YARN[2], seed=4)
    o += yarn_ball('y4', (-4.3, 9.9), YARN[3], seed=6)
    o += basket('bk1', (1.35, 4.25))
    o += basket('bk2', (-4.2, 7.3), with_yarn=YARN[1])
    # the trail
    trail = [(0, -0.95), (0, -0.2), (-0.8, 0.8)] + LIVING_NODES + [(1.2, 12.0), (0.05, 12.9), (0, 13.6), (0, 16)]

    def z_at(x, y):
        if y < 0:
            return step_z(x, y)
        return 0.0
    o += K.paw_trail('pr', trail, prints, z_at, spacing=0.52, side=0.1, s=1.05, avoid=LIVING_NODES, avoid_r=0.72)
    nodes = [px((x, y, 0), AY) for x, y in LIVING_NODES]
    panel_render(out, 'map_living', o, H, AY, samples,
                 dict(nodes=nodes, entry=[W // 2, H], exit=[W // 2, 0], world='living'))


def house(x0, x1, y0, y1, dx, wall_h=2.0):
    walls = K.pm('housewall', (0.99, 0.88, 0.80), rough=0.8)
    trim = K.pm('housetrim', (0.99, 0.97, 0.93), rough=0.6)
    roof = K.stripes('roof', (0.86, 0.40, 0.40), (0.95, 0.52, 0.50), width=0.11, axis='Z', rough=0.7)
    roofin = K.pm('roofin', (0.98, 0.62, 0.70), rough=0.8)
    door = K.pm('door', (0.26, 0.62, 0.66), rough=0.5, cc=0.3)
    gold = K.pm('knob', (1.0, 0.78, 0.3), rough=0.25, metal=1.0)
    glow = K.pm('winglow', (1.0, 0.80, 0.45), emit=(1.0, 0.66, 0.28), es=1.1)
    frame = K.pm('winframe', (0.99, 0.97, 0.93), rough=0.5)
    brick = K.pm('chimney', (0.78, 0.40, 0.33), rough=0.8)
    mat_ = K.pm('doormat', (0.95, 0.55, 0.65), rough=1, sheen=1)
    xm = (x0 + x1) / 2
    ym = (y0 + y1) / 2
    o = [K.box('hwall', x0, y0, 0, x1, y1, wall_h, walls, bevel=0.03),
         K.box('hbase', x0 - 0.04, y0 - 0.04, 0, x1 + 0.04, y1 + 0.04, 0.16, trim, bevel=0.02)]
    # the roof: a gable with its ridge along X, overhanging, two cat ears on top
    ov = 0.28
    rh = 0.95
    ry = ym
    yy0, yy1 = y0 - 0.1, y1 + ov
    zz = wall_h - 0.12
    th = 0.12
    v = [(x0 - ov, yy0, zz), (x1 + ov, yy0, zz), (x1 + ov, ry, wall_h + rh), (x0 - ov, ry, wall_h + rh),
         (x0 - ov, yy1, zz), (x1 + ov, yy1, zz)]
    vt = [(a, b, c + th) for a, b, c in v]
    verts = v + vt
    faces = [(6, 7, 8, 9), (9, 8, 11, 10), (0, 3, 2, 1), (3, 4, 5, 2),
             (0, 1, 7, 6), (4, 10, 11, 5), (0, 6, 9, 3), (3, 9, 10, 4), (1, 2, 8, 7), (2, 5, 11, 8)]
    r = C.mesh_object('roof', verts, faces, roof)
    bm = bmesh.new()
    bm.from_mesh(r.data)
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    bm.to_mesh(r.data)
    bm.free()
    md = r.modifiers.new('bv', 'BEVEL')
    md.width = 0.04
    md.segments = 2
    o.append(r)
    for sx, ex in ((-1, x0 + 0.25), (1, x1 - 0.25)):
        ec = (ex, ry, wall_h + rh + th - 0.08)
        o.append(K.cone('hear%d' % sx, ec, 0.42, 0.02, 0.62, roof, rot=(0, sx * 16, 0), scale=(1, 0.5, 1)))
        o.append(K.cone('heari%d' % sx, (ec[0], ec[1] - 0.12, ec[2] + 0.06), 0.27, 0.02, 0.44, roofin,
                        rot=(0, sx * 16, 0), scale=(1, 0.3, 1)))
    # chimney on the back slope
    o.append(K.box('chim', xm + 0.35, ry + 0.25, wall_h, xm + 0.75, ry + 0.65, wall_h + rh + 0.45, brick, bevel=0.03))
    o.append(K.box('chimt', xm + 0.3, ry + 0.2, wall_h + rh + 0.45, xm + 0.8, ry + 0.7, wall_h + rh + 0.53,
                   K.pm('chimtop', (0.45, 0.24, 0.22), rough=0.8),
                   bevel=0.02))
    # the door (round top), a porch light
    dw, dh = 0.42, 1.12
    o.append(K.box('door', dx - dw, y0 - 0.04, 0.16, dx + dw, y0, dh, door, bevel=0.01))
    o.append(K.cone('doortop', (dx, y0, dh), dw, dw, 0.045, door, rot=(90, 0, 0), verts=32))
    o.append(K.torus('doorfr', (dx, y0 - 0.03, dh), dw + 0.04, 0.035, frame, rot=(90, 0, 0), arc=0.5, n=20, m=8))
    for sx in (-1, 1):
        o.append(K.box('doorj%d' % sx, dx + sx * (dw + 0.04) - 0.035, y0 - 0.06, 0.16, dx + sx * (dw + 0.04) + 0.035,
                       y0, dh, frame))
    o.append(K.ellipsoid('knob', (dx + dw - 0.12, y0 - 0.07, 0.6), (0.045, 0.03, 0.045), gold))
    o += K.paw_upright('doorpaw', (dx, y0 - 0.05, 0.95), 1.4, K.pm('doorpawm', (0.99, 0.95, 0.88)), depth=0.012)
    o.append(K.box('mat', dx - 0.4, y0 - 0.55, 0.0, dx + 0.4, y0 - 0.1, 0.03, mat_, bevel=0.012))
    o.append(K.ellipsoid('lamp', (dx + dw + 0.3, y0 - 0.08, 1.45), (0.09, 0.09, 0.12), glow))
    C.add_point_light((dx + dw + 0.3, y0 - 0.3, 1.2), color=(1.0, 0.75, 0.45), power=6, radius=0.1)
    # a round glowing window to the right, a square one to the left
    wx = dx + 1.5
    o.append(K.cone('rwin', (wx, y0 + 0.02, 1.15), 0.33, 0.33, 0.05, glow, rot=(90, 0, 0), verts=40))
    o.append(K.torus('rwinf', (wx, y0 - 0.03, 1.15), 0.35, 0.05, frame, rot=(90, 0, 0)))
    o.append(K.box('rwinx', wx - 0.33, y0 - 0.05, 1.13, wx + 0.33, y0 - 0.02, 1.17, frame))
    o.append(K.box('rwiny', wx - 0.02, y0 - 0.05, 0.82, wx + 0.02, y0 - 0.02, 1.48, frame))
    lx = wx
    # flower box under it
    o.append(K.box('fbox', lx - 0.4, y0 - 0.2, 0.62, lx + 0.4, y0, 0.74, K.wood('fbox', (0.55, 0.36, 0.22)), bevel=0.02))
    fl = [K.pm('flw1', (1.0, 0.55, 0.70)), K.pm('flw2', (1.0, 0.85, 0.35)), K.pm('flw3', (0.95, 0.95, 1.0))]
    lf = K.pm('leaf', (0.22, 0.62, 0.30), rough=0.5, spec=0.4)
    for k in range(6):
        fx = lx - 0.32 + k * 0.128
        o.append(K.ellipsoid('fbl%d' % k, (fx, y0 - 0.1, 0.78), (0.08, 0.06, 0.05), lf))
        o.append(K.ellipsoid('fbf%d' % k, (fx, y0 - 0.12, 0.84), (0.055, 0.04, 0.05), fl[k % 3]))
    return o


def bush(name, c, r=0.6, flowers=None, seed=3):
    lf = K.mottled('bushm', (0.24, 0.58, 0.28), (0.38, 0.74, 0.36), scale=4.0, rough=0.6)
    x, y = c
    o = [K.ellipsoid(name, (x, y, r * 0.75), (r, r * 0.9, r * 0.8), lf)]
    rnd = random.Random(seed)
    for k in range(2):
        a = rnd.uniform(0, 6.28)
        o.append(K.ellipsoid(name + 'b%d' % k, (x + 0.45 * r * math.cos(a), y + 0.4 * r * math.sin(a), r * 0.7),
                             (r * 0.7, r * 0.65, r * 0.6), lf))
    if flowers:
        fm = K.pm('fl_%s' % flowers[0], flowers[1], rough=0.6)
        for k in range(7):
            a = rnd.uniform(-3.0, 0.2)
            e = rnd.uniform(0.15, 1.1)
            p = V((math.cos(a) * math.cos(e), math.sin(a) * math.cos(e) * 0.9, math.sin(e) * 0.8)) * r
            o.append(K.ellipsoid(name + 'f%d' % k, (x + p.x, y + p.y, r * 0.75 + p.z), (0.08, 0.08, 0.08), fm, seg=10,
                                 rings=6))
    return o


def tree(name, c):
    trunk = K.wood('trunk', (0.50, 0.32, 0.18), 3, grain=(10, 10, 1))
    lf = K.mottled('treem', (0.28, 0.62, 0.30), (0.42, 0.78, 0.38), scale=3.0, rough=0.6)
    x, y = c
    o = [K.cone(name + 't', (x, y, 0), 0.2, 0.14, 1.3, trunk)]
    for k, (dx, dy, dz, r) in enumerate(((0, 0, 1.9, 0.95), (-0.55, -0.1, 1.55, 0.6), (0.6, 0.05, 1.6, 0.62),
                                         (0.1, -0.3, 2.45, 0.6))):
        o.append(K.ellipsoid(name + 'c%d' % k, (x + dx, y + dy, dz), (r, r * 0.9, r * 0.85), lf))
    red = K.pm('apple', (0.95, 0.30, 0.35), rough=0.3, cc=0.5)
    for k, (dx, dz) in enumerate(((-0.5, 1.75), (0.35, 1.5), (0.55, 2.05), (-0.1, 2.3))):
        o.append(K.ellipsoid(name + 'a%d' % k, (x + dx, y - 0.72, dz), (0.08, 0.08, 0.08), red, seg=12, rings=8))
    return o


def lamp_post(name, c):
    iron = K.pm('iron', (0.22, 0.24, 0.30), rough=0.4, metal=0.6)
    glow = K.pm('lampglow', (1.0, 0.85, 0.55), emit=(1.0, 0.75, 0.40), es=4.0)
    x, y = c
    C.add_point_light((x, y - 0.1, 1.45), color=(1.0, 0.75, 0.45), power=40, radius=0.1)
    return [K.cone(name + 'b', (x, y, 0), 0.12, 0.08, 0.1, iron),
            K.cone(name + 'p', (x, y, 0.1), 0.04, 0.04, 1.25, iron),
            K.ellipsoid(name + 'g', (x, y, 1.45), (0.13, 0.13, 0.16), glow),
            K.cone(name + 'h', (x, y, 1.58), 0.18, 0.02, 0.16, iron)]


def mailbox(name, c):
    post = K.wood('mbpost', (0.55, 0.36, 0.22))
    box_ = K.pm('mbox', (0.95, 0.50, 0.55), rough=0.5, cc=0.3)
    flag = K.pm('mflag', (1.0, 0.85, 0.3), rough=0.5)
    x, y = c
    return [K.box(name + 'p', x - 0.05, y - 0.05, 0, x + 0.05, y + 0.05, 0.7, post),
            K.box(name + 'b', x - 0.22, y - 0.3, 0.7, x + 0.22, y + 0.2, 0.98, box_, bevel=0.06),
            K.box(name + 'f', x + 0.22, y - 0.05, 0.85, x + 0.25, y + 0.02, 1.12, flag)]


def fence(name, x0, x1, y, key):
    o = [K.box(name + 'r', x0, y - 0.03, 0.22, x1, y + 0.03, 0.3, key, bevel=0.01)]
    n = int((x1 - x0) / 0.3)
    for k in range(n + 1):
        fx = x0 + k * (x1 - x0) / n
        o.append(K.box(name + 'p%d' % k, fx - 0.06, y - 0.04, 0, fx + 0.06, y + 0.04, 0.46, key, bevel=0.015))
        o.append(K.cone(name + 't%d' % k, (fx, y, 0.46), 0.085, 0.0, 0.1, key, verts=4, rot=(0, 0, 45),
                        scale=(1, 0.45, 1)))
    return o


def panel_home(out, samples):
    C.reset('map')
    H, AY, D = 320, 306, 11.2
    grass = K.mottled('grass', (0.34, 0.66, 0.28), (0.46, 0.76, 0.34), scale=1.2, rough=0.9)
    soil = K.mottled('soilside', (0.46, 0.30, 0.20), (0.56, 0.38, 0.25), scale=2.0, rough=0.9)
    prints = K.pm('prints', (1.0, 0.95, 0.88), rough=0.7, emit=(1.0, 0.93, 0.85), es=0.25)
    white = K.pm('fence', (0.98, 0.97, 0.94), rough=0.6)
    o = []
    o += base(D, grass, soil, bevel=0.18, top_t=0.2, top_bevel=0.18)
    # Mila's house
    HX0, HX1, HY0, HY1 = -4.4, -0.8, 5.3, 7.5
    dx = HX0 + 1.0
    o += house(HX0, HX1, HY0, HY1, dx)
    o += tree('tree', (3.6, 8.9))
    o += bush('bush1', (-4.75, 4.3), 0.45, ('pink', (1.0, 0.55, 0.72)), seed=2)
    o += bush('bush2', (-4.2, 1.7), 0.5, ('yellow', (1.0, 0.86, 0.35)), seed=4)
    o += bush('bush3', (4.3, 3.4), 0.6, ('pink', (1.0, 0.55, 0.72)), seed=6)
    o += bush('bush4', (-2.6, 9.6), 0.5, None, seed=7)
    o += bush('bush5', (1.9, 10.4), 0.45, ('white', (0.98, 0.97, 1.0)), seed=8)
    o += lamp_post('lamp', (2.2, 2.6))
    o += mailbox('mail', (-4.7, 3.1))
    o += fence('fl', -5.0, -1.6, 0.55, white)
    o += fence('fr', 1.2, 5.0, 0.55, white)
    o += yarn_ball('hy', (3.0, 6.3), YARN[0], r=0.28, seed=5)
    trail = [(dx, HY0 - 0.35), (dx + 0.4, 3.8), (-0.6, 2.8), (1.3, 4.6), (1.5, 7.2), (0.5, 9.6), (0.0, 11.0),
             (0, 14.5)]
    o += K.paw_trail('pr', trail, prints, lambda x, y: 0.0, spacing=0.52, side=0.1, s=1.05, start=0.35)
    panel_render(out, 'map_home', o, H, AY, samples,
                 dict(nodes=[], entry=None, exit=[W // 2, 0], world='home',
                      door=px((dx, HY0, 0.5), AY),
                      house=[px((HX0 - 0.3, HY0, 0), AY)[0], px((0, HY1, 3.6), AY)[1],
                             px((HX1 + 0.3, 0, 0), AY)[0], px((0, HY0 - 0.2, 0), AY)[1]]))


# ---------------------------------------------------------------------------
# panel plumbing: opaque output, the standard trail
# ---------------------------------------------------------------------------

def flatten(path):
    """Panels are packed as RGB565 without alpha: flatten onto opaque black."""
    a = K.read_png(path).astype(np.float32)
    rgb = a[..., :3] * (a[..., 3:4] / 255.0)
    C.write_png(path, np.round(rgb).astype(np.uint8))


def panel_render(out, name, o, H, AY, samples, extra):
    ex = dict(extra)
    ex['opaque'] = True
    render(out, name, o, (W, H, W // 2, AY), samples, kind='map', extra=ex)
    flatten(os.path.join(out, name + '.png'))


def std_z(x, y):
    if y < 0:
        return step_z(x, y)
    return 0.0


def std_trail(nodes, D, lead_in, lead_out, top=5.0):
    return [(0, -0.95), (0, -0.2), lead_in] + list(nodes) + [lead_out, (0.05, D - 0.1), (0, D + 0.6), (0, D + top)]


def world_panel(out, name, o, nodes, D, H, AY, samples, prints, lead_in, lead_out, z_at=std_z, world=None,
                spacing=0.52, s=1.05):
    trail = std_trail(nodes, D, lead_in, lead_out)
    o += K.paw_trail('pr', trail, prints, z_at, spacing=spacing, side=0.1, s=s, avoid=nodes, avoid_r=0.72)
    panel_render(out, name, o, H, AY, samples,
                 dict(nodes=[px((x, y, 0), AY) for x, y in nodes], entry=[W // 2, H], exit=[W // 2, 0],
                      world=world))


def box_at(name, c, w, d, h, key, z0=0.0, rotz=0.0, bevel=0.0):
    """A box centred on c (x, y), turned rotz degrees about its centre."""
    ob = K.box(name, -w / 2, -d / 2, 0, w / 2, d / 2, h, key, bevel=bevel)
    ob.location = (c[0], c[1], z0)
    ob.rotation_euler = (0, 0, math.radians(rotz))
    return ob


# ---------------------------------------------------------------------------
# kitchen: mint and white checker tiles, pale yellow tiled wall, cookie tins,
# placemats with a fish, a puddle
# ---------------------------------------------------------------------------
KITCHEN_NODES = [(1.9, 1.8), (-1.8, 2.7), (-3.4, 4.9), (-0.7, 6.1), (2.7, 6.8), (2.4, 9.3), (-0.9, 9.9), (-2.9, 11.3)]
TIN_COLS = [(0.90, 0.28, 0.30), (0.30, 0.52, 0.92), (0.96, 0.48, 0.66), (0.98, 0.74, 0.22)]


def tin(name, c, col, z0=0.0, r=0.3, h=0.4):
    body = K.pm('tin%d%d%d' % tuple(int(v * 9) for v in col), col, rough=0.25, metal=0.3, cc=0.8)
    lid = K.pm('tinl%d%d%d' % tuple(int(v * 9) for v in col), [min(1.0, v * 1.15 + 0.12) for v in col], rough=0.25,
               metal=0.3, cc=0.8)
    band = K.pm('tinband', (0.99, 0.95, 0.86), rough=0.5)
    x, y = c
    o = [K.cone(name + 'b', (x, y, z0), r, r, h, body, verts=32, bevel=0.02),
         K.cone(name + 'band', (x, y, z0 + h * 0.3), r + 0.012, r + 0.012, h * 0.32, band, verts=32),
         K.cone(name + 'lid', (x, y, z0 + h - 0.03), r + 0.035, r + 0.035, 0.1, lid, verts=32, bevel=0.035)]
    o += K.paw(name + 'p', (x, y - 0.02), math.pi / 2, 1.1, band, z=z0 + h + 0.07, t=0.008)
    return o


def placemat(name, c, r=0.46):
    edge = K.pm('matedge', (0.32, 0.58, 0.92), rough=0.7)
    inner = K.pm('matin', (0.98, 0.97, 0.94), rough=0.7)
    fish = K.pm('matfish', (1.0, 0.52, 0.18), rough=0.6)
    eye = K.pm('mateye', (0.1, 0.1, 0.15))
    x, y = c
    return [K.cone(name, (x, y, 0), r, r, 0.02, edge, verts=40),
            K.cone(name + 'i', (x, y, 0.001), r * 0.8, r * 0.8, 0.022, inner, verts=40),
            K.ellipsoid(name + 'f', (x - 0.05, y, 0.028), (0.18, 0.1, 0.006), fish, seg=20, rings=6),
            K.flat_poly(name + 't', [(x + 0.1, y), (x + 0.26, y + 0.1), (x + 0.26, y - 0.1)], fish, z=0.03),
            K.ellipsoid(name + 'e', (x - 0.15, y + 0.02, 0.034), (0.025, 0.025, 0.004), eye, seg=10, rings=4)]


def puddle(name, c, s=1.0, seed=1):
    water = K.pm('water', (0.55, 0.82, 1.0), rough=0.03, spec=1.0, cc=1.0, emit=(0.35, 0.55, 0.75), es=0.25)
    hi = K.pm('waterhi', (1, 1, 1), emit=(1, 1, 1), es=1.2)
    rnd = random.Random(seed)
    x, y = c
    o = [K.ellipsoid(name, (x, y, 0.0), (0.5 * s, 0.36 * s, 0.012), water, seg=32, rings=8)]
    for k in range(3):
        a = rnd.uniform(0, 6.28)
        o.append(K.ellipsoid(name + '%d' % k, (x + 0.3 * s * math.cos(a), y + 0.22 * s * math.sin(a), 0.0),
                             (0.28 * s, 0.22 * s, 0.012), water, seg=24, rings=6))
    o.append(K.ellipsoid(name + 'h', (x - 0.15 * s, y + 0.1 * s, 0.012), (0.12 * s, 0.035 * s, 0.003), hi,
                         rot=(0, 0, 25), seg=12, rings=4))
    return o


def counter(name, x0, x1, y0, y1, h=0.9, sink_x=None, herbs=False):
    cab = K.pm('cab', (0.96, 0.96, 0.93), rough=0.5)
    door = K.pm('cabdoor', (0.56, 0.84, 0.75), rough=0.45, cc=0.2)
    knob = K.pm('cabknob', (1.0, 0.78, 0.3), rough=0.3, metal=1)
    top = K.wood('worktop', (0.80, 0.57, 0.34), 2)
    steel = K.pm('steel', (0.78, 0.80, 0.84), rough=0.25, metal=0.8)
    o = [K.box(name + 'c', x0, y0 + 0.04, 0, x1, y1, h - 0.06, cab, bevel=0.02),
         K.box(name + 't', x0 - 0.03, y0 - 0.03, h - 0.06, x1 + 0.03, y1, h, top, bevel=0.015)]
    n = max(1, int(round((x1 - x0) / 0.6)))
    dw = (x1 - x0) / n
    for k in range(n):
        a, b = x0 + k * dw + 0.05, x0 + (k + 1) * dw - 0.05
        o.append(K.box(name + 'd%d' % k, a, y0 + 0.01, 0.1, b, y0 + 0.05, h - 0.14, door, bevel=0.015))
        o.append(K.ellipsoid(name + 'k%d' % k, ((a + b) / 2, y0, h - 0.24), (0.04, 0.03, 0.04), knob, seg=12, rings=8))
    if sink_x is not None:
        basin = K.pm('basin', (0.50, 0.55, 0.62), rough=0.2, metal=0.6)
        o.append(K.box(name + 'sink', sink_x - 0.36, y0 + 0.1, h - 0.02, sink_x + 0.36, y1 - 0.08, h + 0.006, steel,
                       bevel=0.02))
        o.append(K.box(name + 'basin', sink_x - 0.3, y0 + 0.15, h - 0.01, sink_x + 0.3, y1 - 0.13, h + 0.008, basin))
        o.append(K.tube(name + 'tap', [(sink_x, y1 - 0.07, h), (sink_x, y1 - 0.07, h + 0.32), (sink_x, y1 - 0.2, h + 0.36),
                                       (sink_x, y1 - 0.3, h + 0.24)], [0.035] * 4, steel))
    if herbs:
        pot = K.pm('herbpot', (0.86, 0.46, 0.32), rough=0.6)
        leaf = K.pm('herb', (0.30, 0.70, 0.34), rough=0.5)
        for k in range(3):
            hx = x0 + 0.2 + k * 0.25
            if hx > x1 - 0.1:
                break
            o.append(K.cone(name + 'hp%d' % k, (hx, y1 - 0.25, h), 0.08, 0.1, 0.13, pot))
            o.append(K.ellipsoid(name + 'hl%d' % k, (hx, y1 - 0.25, h + 0.2), (0.12, 0.1, 0.1), leaf))
    return o


def stove(name, x0, x1, y0, y1, h=0.9):
    body = K.pm('stove', (0.98, 0.94, 0.86), rough=0.4, cc=0.3)
    dark = K.pm('stovetop', (0.20, 0.19, 0.22), rough=0.3)
    glass = K.pm('ovenglass', (0.14, 0.12, 0.14), rough=0.1, spec=0.8)
    steel = K.pm('steel', (0.78, 0.80, 0.84), rough=0.25, metal=0.8)
    red = K.pm('redpot', (0.88, 0.25, 0.28), rough=0.3, cc=0.7)
    ring = K.pm('burner', (0.45, 0.45, 0.5), rough=0.4)
    o = [K.box(name + 'b', x0, y0, 0, x1, y1, h - 0.03, body, bevel=0.03),
         K.box(name + 't', x0 + 0.02, y0 + 0.02, h - 0.04, x1 - 0.02, y1 - 0.02, h, dark, bevel=0.01),
         K.box(name + 'ov', x0 + 0.12, y0 - 0.01, 0.15, x1 - 0.12, y0 + 0.02, h - 0.3, glass, bevel=0.01),
         K.box(name + 'hd', x0 + 0.15, y0 - 0.05, h - 0.24, x1 - 0.15, y0 - 0.01, h - 0.2, steel)]
    for i, (fx, fy) in enumerate(((0.28, 0.28), (0.72, 0.28), (0.28, 0.72), (0.72, 0.72))):
        cx, cy = x0 + (x1 - x0) * fx, y0 + (y1 - y0) * fy
        o.append(K.torus(name + 'br%d' % i, (cx, cy, h + 0.005), 0.11, 0.02, ring, n=24, m=6))
    px_, py_ = x0 + (x1 - x0) * 0.28, y0 + (y1 - y0) * 0.72
    o += [K.cone(name + 'pot', (px_, py_, h), 0.19, 0.19, 0.24, red, verts=32, bevel=0.02),
          K.cone(name + 'lid', (px_, py_, h + 0.24), 0.2, 0.19, 0.03, red, verts=32),
          K.ellipsoid(name + 'lk', (px_, py_, h + 0.29), (0.04, 0.04, 0.03), steel)]
    for sx in (-1, 1):
        o.append(K.ellipsoid(name + 'ph%d' % sx, (px_ + sx * 0.21, py_, h + 0.18), (0.05, 0.02, 0.025), red))
    return o


def fridge(name, x0, x1, y0, y1, h=1.25):
    body = K.pm('fridge', (0.68, 0.90, 0.86), rough=0.35, cc=0.5)
    line = K.pm('fridgeln', (0.42, 0.62, 0.60))
    steel = K.pm('steel', (0.78, 0.80, 0.84), rough=0.25, metal=0.8)
    heart = K.pm('magnet', (0.96, 0.40, 0.55), rough=0.4)
    fish = K.pm('matfish', (1.0, 0.52, 0.18), rough=0.6)
    o = [K.box(name, x0, y0, 0, x1, y1, h, body, bevel=0.12, segments=4),
         K.box(name + 'ln', x0 + 0.06, y0 - 0.005, h * 0.6, x1 - 0.06, y0 + 0.02, h * 0.6 + 0.025, line),
         K.box(name + 'h1', x1 - 0.22, y0 - 0.06, h * 0.66, x1 - 0.15, y0, h * 0.9, steel, bevel=0.02),
         K.box(name + 'h2', x1 - 0.22, y0 - 0.06, h * 0.3, x1 - 0.15, y0, h * 0.52, steel, bevel=0.02),
         K.ellipsoid(name + 'm1', (x0 + 0.3, y0 - 0.02, h * 0.8), (0.07, 0.02, 0.06), heart),
         K.ellipsoid(name + 'm2', (x0 + 0.45, y0 - 0.02, h * 0.42), (0.1, 0.02, 0.045), fish)]
    # a cookie jar on top
    jar = K.pm('jar', (0.96, 0.96, 1.0), rough=0.15, cc=1.0)
    ck = K.pm('cookie', (0.90, 0.66, 0.36), rough=0.8)
    xm, ym = (x0 + x1) / 2, (y0 + y1) / 2
    o += [K.cone(name + 'jar', (xm, ym, h), 0.2, 0.18, 0.3, jar, verts=24, bevel=0.03),
          K.cone(name + 'jl', (xm, ym, h + 0.3), 0.2, 0.2, 0.05, K.pm('jarlid', (0.96, 0.48, 0.62)), verts=24),
          K.cone(name + 'ck', (xm - 0.05, ym - 0.1, h + 0.08), 0.1, 0.1, 0.03, ck, rot=(70, 0, 0), verts=16)]
    return o


def fruit_cabinet(name, x0, x1, y0, y1, h=0.9):
    o = counter(name, x0, x1, y0, y1, h)
    bowl = K.pm('bowlb', (0.35, 0.60, 0.95), rough=0.3, cc=0.5)
    xm, ym = (x0 + x1) / 2, (y0 + y1) / 2
    o.append(K.cone(name + 'bowl', (xm, ym, h), 0.13, 0.26, 0.12, bowl, verts=32))
    for k, (dx, dy, col) in enumerate(((-0.08, 0.0, (0.92, 0.22, 0.25)), (0.1, 0.03, (1.0, 0.6, 0.15)),
                                        (0.0, -0.09, (0.55, 0.85, 0.3)), (0.02, 0.1, (0.95, 0.3, 0.3)))):
        m = K.pm('fruit%d' % k, col, rough=0.35, cc=0.6)
        o.append(K.ellipsoid(name + 'fr%d' % k, (xm + dx, ym + dy, h + 0.17), (0.09, 0.09, 0.09), m, seg=16, rings=10))
    o.append(K.ellipsoid(name + 'ban', (xm + 0.02, ym - 0.02, h + 0.24), (0.16, 0.05, 0.04),
                         K.pm('banana', (1.0, 0.86, 0.3), rough=0.5), rot=(0, -15, 20)))
    return o


def round_table(name, c, r=0.6, h=0.72):
    top = K.pm('ktable', (0.98, 0.96, 0.92), rough=0.4, cc=0.4)
    wd = K.wood('kleg', (0.72, 0.52, 0.32))
    x, y = c
    o = [K.cone(name + 't', (x, y, h - 0.06), r, r, 0.06, top, verts=40, bevel=0.02),
         K.cone(name + 'p', (x, y, 0.05), 0.06, 0.06, h - 0.1, wd),
         K.cone(name + 'f', (x, y, 0), 0.26, 0.22, 0.05, wd, bevel=0.01)]
    # a teapot and cups
    pot = K.pm('teapot', (0.98, 0.62, 0.72), rough=0.25, cc=0.8)
    o += [K.ellipsoid(name + 'tp', (x - 0.15, y + 0.12, h + 0.12), (0.15, 0.13, 0.12), pot),
          K.cone(name + 'tps', (x - 0.28, y + 0.08, h + 0.1), 0.03, 0.02, 0.14, pot, rot=(0, -50, 0)),
          K.ellipsoid(name + 'tpl', (x - 0.15, y + 0.12, h + 0.24), (0.05, 0.05, 0.04), pot)]
    cup = K.pm('cup', (0.99, 0.97, 0.94), rough=0.3, cc=0.5)
    for k, (dx, dy) in enumerate(((0.22, -0.15), (0.2, 0.2))):
        o.append(K.cone(name + 'c%d' % k, (x + dx, y + dy, h), 0.06, 0.08, 0.1, cup))
    for sx in (-1, 1):
        o += chair(name + 'ch%d' % sx, (x + sx * (r + 0.28), y), -sx)
    return o


def chair(name, c, face):
    """A chair whose seat faces +X (face = 1) or -X (face = -1)."""
    wd = K.pm('chair_k', (0.98, 0.80, 0.45), rough=0.5, cc=0.2)
    x, y = c
    o = [K.box(name + 's', x - 0.22, y - 0.22, 0.4, x + 0.22, y + 0.22, 0.46, wd, bevel=0.03)]
    for dx in (-0.18, 0.18):
        for dy in (-0.18, 0.18):
            o.append(K.box(name + 'l%.1f%.1f' % (dx, dy), x + dx - 0.025, y + dy - 0.025, 0, x + dx + 0.025,
                           y + dy + 0.025, 0.4, wd))
    bx = x - face * 0.2
    o.append(K.box(name + 'b', bx - 0.03, y - 0.22, 0.46, bx + 0.03, y + 0.22, 0.9, wd, bevel=0.02))
    return o


def stool(name, c):
    seat = K.pm('stoolseat', (0.92, 0.36, 0.42), rough=0.8, sheen=0.8)
    wd = K.wood('kleg', (0.72, 0.52, 0.32))
    x, y = c
    o = [K.cone(name + 's', (x, y, 0.5), 0.25, 0.25, 0.08, seat, verts=32, bevel=0.03)]
    for k in range(3):
        a = 2 * math.pi * k / 3 + 0.4
        o.append(K.cone(name + 'l%d' % k, (x + 0.16 * math.cos(a), y + 0.16 * math.sin(a), 0), 0.03, 0.03, 0.5, wd))
    return o


def trash(name, c):
    body = K.pm('trash', (0.52, 0.80, 0.72), rough=0.35, cc=0.5)
    lid = K.pm('trashlid', (0.40, 0.66, 0.60), rough=0.35, cc=0.5)
    x, y = c
    return [K.cone(name, (x, y, 0), 0.2, 0.23, 0.52, body, verts=32, bevel=0.02),
            K.cone(name + 'l', (x, y, 0.52), 0.24, 0.2, 0.07, lid, verts=32, bevel=0.02),
            K.box(name + 'p', x - 0.07, y - 0.3, 0, x + 0.07, y - 0.2, 0.04, lid)]


def cat_bowls(name, c):
    bowl = K.pm('bowlb', (0.35, 0.60, 0.95), rough=0.3, cc=0.5)
    kib = K.pm('kibble', (0.58, 0.34, 0.16), rough=0.8)
    water = K.pm('water', (0.55, 0.82, 1.0), rough=0.03, spec=1.0, cc=1.0, emit=(0.35, 0.55, 0.75), es=0.25)
    x, y = c
    return [K.cone(name + 'a', (x, y, 0), 0.17, 0.22, 0.1, bowl, verts=32),
            K.ellipsoid(name + 'ak', (x, y, 0.09), (0.18, 0.18, 0.035), kib),
            K.cone(name + 'b', (x + 0.5, y, 0), 0.17, 0.22, 0.1, K.pm('bowlw', (0.96, 0.96, 0.96), rough=0.2, cc=0.5),
                   verts=32),
            K.cone(name + 'bw', (x + 0.5, y, 0.085), 0.19, 0.19, 0.005, water, verts=32)]


def jar_shelf(name, x0, x1, z, y):
    wd = K.wood('worktop', (0.80, 0.57, 0.34), 2)
    jar = K.pm('jar', (0.96, 0.96, 1.0), rough=0.15, cc=1.0)
    o = [K.box(name, x0, y - 0.22, z, x1, y, z + 0.05, wd, bevel=0.01)]
    cols = [(0.96, 0.48, 0.62), (0.98, 0.76, 0.25), (0.35, 0.75, 0.62), (0.45, 0.60, 0.95)]
    n = int((x1 - x0) / 0.24)
    for k in range(n):
        jx = x0 + 0.14 + k * 0.24
        o.append(K.cone(name + 'j%d' % k, (jx, y - 0.11, z + 0.05), 0.08, 0.08, 0.18, jar, verts=16))
        o.append(K.cone(name + 'jl%d' % k, (jx, y - 0.11, z + 0.23), 0.085, 0.085, 0.04,
                        K.pm('jl%d' % (k % 4), cols[k % 4]), verts=16))
    return o


def wall_clock(name, x, z, y):
    rim = K.pm('clockrim', (0.96, 0.48, 0.62), rough=0.4, cc=0.4)
    face = K.pm('clockf', (1.0, 0.99, 0.95), rough=0.5)
    hand = K.pm('clockh', (0.2, 0.18, 0.22))
    return [K.cone(name, (x, y, z), 0.24, 0.24, 0.05, rim, rot=(90, 0, 0), verts=32),
            K.cone(name + 'f', (x, y - 0.03, z), 0.19, 0.19, 0.03, face, rot=(90, 0, 0), verts=32),
            K.box(name + 'h1', x - 0.012, y - 0.075, z, x + 0.012, y - 0.06, z + 0.14, hand),
            K.box(name + 'h2', x, y - 0.075, z - 0.012, x + 0.1, y - 0.06, z + 0.012, hand)]


def panel_kitchen(out, samples):
    C.reset('map')
    H, AY, D = 400, 368, 13.0
    floor = K.checker('kfloor', (0.40, 0.78, 0.64), (0.93, 0.91, 0.85), size=0.5)
    paper = K.tiles('ktile', (0.99, 0.90, 0.58), (1.0, 0.98, 0.92), w=0.2, h=0.2, plane='XZ', col2=(0.98, 0.87, 0.52))
    wain = K.pm('kwain', (0.97, 0.96, 0.92), rough=0.5)
    top = K.wood('worktop', (0.80, 0.57, 0.34), 2)
    side = K.pm('kbase', (0.45, 0.70, 0.66), rough=0.6)
    prints = K.pm('kprints', (0.95, 0.42, 0.45), rough=0.7, emit=(0.9, 0.35, 0.4), es=0.2)
    o = []
    o += base(D + 0.22, floor, side, trim_key=wain)
    o += steps(floor)
    o += back_wall(D, -HALF_W, HALF_W, 1.6, paper, wain, top)
    o += side_walls(D, 0.5, wain, top)
    o += window(-3.75, -2.45, 1.08, 1.48, D)
    o += jar_shelf('jars', -5.05, -4.1, 1.15, D)
    o += wall_clock('clk', 3.2, 1.15, D)
    # along the back wall
    o += stove('stove', -5.12, -4.1, 12.3, 12.98)
    o += counter('csink', -4.1, -2.2, 12.3, 12.98, sink_x=-3.1)
    o += counter('cherb', -2.2, -1.25, 12.3, 12.98, herbs=True)
    o += fruit_cabinet('fcab', 1.3, 2.6, 12.3, 12.98)
    o += fridge('fridge', 3.85, 5.12, 12.2, 12.98)
    o += cat_bowls('bowls', (2.9, 11.8))
    # the room
    o += round_table('table', (0.6, 8.0))
    o += stool('stool', (-2.3, 8.4))
    o += trash('trash', (4.7, 1.3))
    o += tin('t1', (-4.3, 1.6), TIN_COLS[0])
    o += tin('t2', (0.4, 3.9), TIN_COLS[1])
    o += tin('t3', (-4.4, 8.3), TIN_COLS[2])
    o += tin('t4', (4.3, 9.1), TIN_COLS[3])
    o += placemat('pm1', (-1.4, 4.4))
    o += placemat('pm2', (4.3, 5.2))
    o += tin('t5', (4.3, 5.2), (0.35, 0.78, 0.62), z0=0.025)
    o += puddle('pd1', (3.9, 11.0), 1.0, seed=2)
    o += puddle('pd2', (-3.8, 6.9), 0.8, seed=5)
    world_panel(out, 'map_kitchen', o, KITCHEN_NODES, D, H, AY, samples, prints, (0.8, 0.8), (-1.2, 12.0),
                world='kitchen')


# ---------------------------------------------------------------------------
# garden: grass, hedges, a little wooden gate, flower pots, soil circles
# ---------------------------------------------------------------------------
GARDEN_NODES = [(-2.8, 1.9), (0.4, 3.0), (3.2, 3.6), (1.8, 6.0), (-1.4, 6.4), (-3.2, 8.8), (-0.2, 10.0), (2.6, 11.4)]
FLOWERS = [(0.96, 0.40, 0.62), (1.0, 0.82, 0.25), (0.70, 0.52, 0.98), (0.96, 0.30, 0.30)]


def hedge(name, x0, x1, y0, y1, h, seed=1):
    leaf = K.mottled('hedge', (0.18, 0.46, 0.22), (0.30, 0.60, 0.28), scale=5.0, rough=0.7)
    o = [K.box(name, x0, y0, 0, x1, y1, h, leaf, bevel=0.14, segments=3)]
    rnd = random.Random(seed)
    n = int((x1 - x0) * (y1 - y0) / 0.12) + 2
    for k in range(n):
        o.append(K.ellipsoid(name + 'l%d' % k, (rnd.uniform(x0 + 0.12, x1 - 0.12), rnd.uniform(y0 + 0.1, y1 - 0.1),
                                               h - 0.02), (0.16, 0.14, 0.08), leaf, seg=12, rings=6))
    return o


def gate_leaf(name, hinge, sx, open_deg, key, w=0.62, h=0.62):
    """One leaf of a little picket gate, hinged at `hinge`, running towards
    -sx (to the gap), swung open by open_deg (towards +Y)."""
    parts = [K.box(name + 'r1', 0, -0.02, 0.14, w, 0.02, 0.2, key, bevel=0.01),
             K.box(name + 'r2', 0, -0.02, 0.44, w, 0.02, 0.5, key, bevel=0.01)]
    for k in range(4):
        px_ = 0.07 + k * (w - 0.14) / 3
        parts.append(K.box(name + 'p%d' % k, px_ - 0.045, -0.03, 0.04, px_ + 0.045, 0.03, h, key, bevel=0.012))
        parts.append(K.cone(name + 't%d' % k, (px_, 0, h), 0.064, 0.0, 0.08, key, verts=4, rot=(0, 0, 45),
                            scale=(1, 0.5, 1)))
    K.parent_all(parts, name, loc=hinge, rotz=open_deg if sx < 0 else 180 - open_deg)
    return parts


def flower_pot(name, c, col, z0=0.0, s=1.0, seed=0):
    pot = K.pm('tpot', (0.86, 0.46, 0.30), rough=0.6)
    soil = K.pm('psoil', (0.25, 0.16, 0.10), rough=1)
    leaf = K.pm('pleaf', (0.26, 0.64, 0.30), rough=0.5, spec=0.4)
    petal = K.pm('petal%d%d%d' % tuple(int(v * 9) for v in col), col, rough=0.5, sheen=0.5)
    mid = K.pm('fmid', (1.0, 0.86, 0.30), rough=0.5)
    x, y = c
    o = [K.cone(name + 'p', (x, y, z0), 0.24 * s, 0.32 * s, 0.34 * s, pot),
         K.torus(name + 'lip', (x, y, z0 + 0.34 * s), 0.32 * s, 0.045 * s, pot),
         K.ellipsoid(name + 's', (x, y, z0 + 0.33 * s), (0.29 * s, 0.29 * s, 0.03 * s), soil),
         K.cone(name + 'st', (x, y, z0 + 0.33 * s), 0.025, 0.02, 0.3 * s, leaf)]
    for sx in (-1, 1):
        o.append(K.ellipsoid(name + 'lf%d' % sx, (x + sx * 0.12 * s, y - 0.02, z0 + 0.46 * s), (0.14 * s, 0.06 * s, 0.035 * s),
                             leaf, rot=(0, sx * -25, 0)))
    fz = z0 + 0.68 * s
    for k in range(6):
        a = 2 * math.pi * k / 6
        o.append(K.ellipsoid(name + 'pt%d' % k, (x + 0.13 * s * math.cos(a), y + 0.13 * s * math.sin(a) - 0.03,
                                                 fz + 0.04 * s * math.sin(a)),
                             (0.11 * s, 0.07 * s, 0.035 * s), petal, rot=(-25, 0, math.degrees(a)), seg=16, rings=8))
    o.append(K.ellipsoid(name + 'c', (x, y - 0.03, fz + 0.02), (0.075 * s, 0.075 * s, 0.05 * s), mid, seg=16, rings=8))
    return o


def soil_circle(name, c, r=0.44, seed=1):
    soil = K.mottled('soilc', (0.28, 0.18, 0.12), (0.36, 0.24, 0.16), scale=8.0, rough=1)
    peb = K.pm('pebble', (0.90, 0.88, 0.84), rough=0.5)
    peb2 = K.pm('pebble2', (0.70, 0.70, 0.72), rough=0.5)
    x, y = c
    o = [K.ellipsoid(name, (x, y, 0.0), (r, r, 0.03), soil, seg=40, rings=8)]
    rnd = random.Random(seed)
    for k in range(12):
        a = 2 * math.pi * k / 12 + rnd.uniform(-0.1, 0.1)
        o.append(K.ellipsoid(name + 'p%d' % k, (x + (r + 0.04) * math.cos(a), y + (r + 0.04) * math.sin(a), 0.03),
                             (0.07, 0.06, 0.045), peb if k % 2 else peb2, seg=12, rings=6))
    return o


def stump(name, c):
    bark = K.wood('bark', (0.46, 0.30, 0.18), 3, grain=(8, 8, 1))
    ring = K.pm('stumptop', (0.88, 0.70, 0.46), rough=0.7)
    dark = K.pm('stumpring', (0.62, 0.44, 0.26), rough=0.7)
    red = K.pm('mushred', (0.92, 0.22, 0.24), rough=0.4, cc=0.4)
    white = K.pm('mushw', (0.98, 0.96, 0.92), rough=0.6)
    x, y = c
    o = [K.cone(name, (x, y, 0), 0.4, 0.34, 0.34, bark, verts=24),
         K.cone(name + 't', (x, y, 0.34), 0.33, 0.33, 0.012, ring, verts=32)]
    for r in (0.12, 0.22):
        o.append(K.torus(name + 'r%.2f' % r, (x, y, 0.352), r, 0.012, dark, n=32, m=6))
    mx, my = x - 0.45, y - 0.2
    o += [K.cone(name + 'ms', (mx, my, 0), 0.05, 0.045, 0.14, white),
          K.ellipsoid(name + 'mc', (mx, my, 0.15), (0.12, 0.12, 0.07), red)]
    for k in range(4):
        a = k * 1.6
        o.append(K.ellipsoid(name + 'md%d' % k, (mx + 0.07 * math.cos(a), my + 0.07 * math.sin(a) - 0.02, 0.19),
                             (0.022, 0.022, 0.012), white, seg=8, rings=4))
    return o


def watering_can(name, c):
    wd = K.stripes('crate_g', (0.76, 0.56, 0.34), (0.66, 0.46, 0.28), width=0.06, axis='Z', rough=0.7)
    can = K.pm('can', (0.30, 0.72, 0.76), rough=0.3, cc=0.5, metal=0.2)
    x, y = c
    o = [K.box(name + 'crate', x - 0.34, y - 0.28, 0, x + 0.34, y + 0.28, 0.36, wd, bevel=0.02),
         K.cone(name + 'b', (x, y, 0.36), 0.17, 0.15, 0.28, can, verts=24, bevel=0.02),
         K.tube(name + 'sp', [(x - 0.12, y - 0.04, 0.44), (x - 0.3, y - 0.12, 0.56), (x - 0.4, y - 0.16, 0.7)],
                [0.035, 0.03, 0.025], can),
         K.cone(name + 'rose', (x - 0.42, y - 0.17, 0.66), 0.03, 0.07, 0.07, can, rot=(0, -40, 0)),
         K.torus(name + 'h', (x + 0.02, y, 0.66), 0.13, 0.025, can, rot=(90, 0, 0), arc=0.5, n=16, m=8)]
    return o


def gnome(name, c, s=1.25):
    coat = K.pm('gcoat', (0.30, 0.52, 0.92), rough=0.5, cc=0.3)
    hat = K.pm('ghat', (0.92, 0.24, 0.26), rough=0.5, cc=0.3)
    skin = K.pm('gskin', (0.99, 0.80, 0.70), rough=0.6)
    beard = K.pm('gbeard', (0.98, 0.98, 0.96), rough=0.9, sheen=1)
    boot = K.pm('gboot', (0.35, 0.22, 0.14), rough=0.6)
    x, y = c
    return [K.ellipsoid(name + 'bt1', (x - 0.07 * s, y - 0.04 * s, 0.03 * s), (0.06 * s, 0.08 * s, 0.04 * s), boot),
            K.ellipsoid(name + 'bt2', (x + 0.07 * s, y - 0.04 * s, 0.03 * s), (0.06 * s, 0.08 * s, 0.04 * s), boot),
            K.cone(name + 'body', (x, y, 0.02 * s), 0.17 * s, 0.1 * s, 0.32 * s, coat),
            K.ellipsoid(name + 'beard', (x, y - 0.08 * s, 0.3 * s), (0.12 * s, 0.07 * s, 0.13 * s), beard),
            K.ellipsoid(name + 'face', (x, y - 0.02 * s, 0.43 * s), (0.1 * s, 0.09 * s, 0.09 * s), skin),
            K.ellipsoid(name + 'nose', (x, y - 0.11 * s, 0.41 * s), (0.04 * s, 0.035 * s, 0.035 * s),
                        K.pm('gnose', (0.98, 0.58, 0.55))),
            K.cone(name + 'hat', (x, y, 0.47 * s), 0.12 * s, 0.01, 0.36 * s, hat, rot=(10, -12, 0))]


def bird_bath(name, c):
    stone_ = K.mottled('bstone', (0.78, 0.78, 0.80), (0.88, 0.87, 0.86), scale=6.0, rough=0.7)
    water = K.pm('water', (0.55, 0.82, 1.0), rough=0.03, spec=1.0, cc=1.0, emit=(0.35, 0.55, 0.75), es=0.25)
    bird = K.pm('bird', (0.40, 0.62, 0.95), rough=0.5)
    beak = K.pm('beak', (1.0, 0.65, 0.2), rough=0.5)
    x, y = c
    return [K.cone(name + 'b', (x, y, 0), 0.24, 0.2, 0.06, stone_, verts=24),
            K.cone(name + 'p', (x, y, 0.06), 0.09, 0.11, 0.46, stone_, verts=24),
            K.cone(name + 'bowl', (x, y, 0.5), 0.16, 0.38, 0.12, stone_, verts=32, bevel=0.02),
            K.cone(name + 'w', (x, y, 0.6), 0.32, 0.32, 0.01, water, verts=32),
            K.ellipsoid(name + 'bd', (x + 0.27, y - 0.1, 0.7), (0.1, 0.07, 0.07), bird),
            K.ellipsoid(name + 'bh', (x + 0.2, y - 0.14, 0.78), (0.055, 0.055, 0.055), bird),
            K.cone(name + 'bk', (x + 0.15, y - 0.16, 0.78), 0.02, 0.0, 0.05, beak, rot=(0, -90, 0))]


def bench(name, x0, x1, y0, y1):
    wd = K.wood('bench', (0.80, 0.56, 0.34), 2)
    iron = K.pm('benchiron', (0.22, 0.38, 0.30), rough=0.4, metal=0.4)
    o = []
    for k in range(3):
        a = y0 + k * (y1 - y0 - 0.1) / 3
        o.append(K.box(name + 's%d' % k, x0, a, 0.4, x1, a + (y1 - y0 - 0.1) / 3 - 0.03, 0.45, wd, bevel=0.012))
    for k, z in enumerate((0.6, 0.78)):
        o.append(K.box(name + 'b%d' % k, x0, y1 - 0.08, z, x1, y1 - 0.02, z + 0.12, wd, bevel=0.012))
    for lx in (x0 + 0.12, x1 - 0.12):
        o.append(K.box(name + 'lf%.1f' % lx, lx - 0.04, y0 + 0.05, 0, lx + 0.04, y0 + 0.11, 0.4, iron))
        o.append(K.box(name + 'lb%.1f' % lx, lx - 0.04, y1 - 0.11, 0, lx + 0.04, y1 - 0.02, 0.92, iron))
        o.append(K.box(name + 'la%.1f' % lx, lx - 0.04, y0 + 0.05, 0.55, lx + 0.04, y1 - 0.05, 0.6, iron))
    return o


def plate_button(name, c):
    stone_ = K.mottled('bstone', (0.78, 0.78, 0.80), (0.88, 0.87, 0.86), scale=6.0, rough=0.7)
    top = K.pm('plbtn', (0.95, 0.88, 0.62), rough=0.4, cc=0.4)
    x, y = c
    return [K.cone(name, (x, y, 0), 0.4, 0.36, 0.06, stone_, verts=32, bevel=0.02),
            K.cone(name + 't', (x, y, 0.06), 0.24, 0.22, 0.05, top, verts=32, bevel=0.015)]


def butterfly(name, c, col):
    w = K.pm('bfly%d%d%d' % tuple(int(v * 9) for v in col), col, rough=0.5, emit=col, es=0.2)
    x, y, z = c
    return [K.ellipsoid(name + 'a', (x - 0.07, y, z), (0.08, 0.05, 0.01), w, rot=(0, 30, 20)),
            K.ellipsoid(name + 'b', (x + 0.07, y, z), (0.08, 0.05, 0.01), w, rot=(0, -30, -20))]


def panel_garden(out, samples):
    C.reset('map')
    H, AY, D = 400, 368, 12.8
    grass = K.mottled('grass', (0.34, 0.66, 0.28), (0.46, 0.76, 0.34), scale=1.2, rough=0.9)
    soil = K.mottled('soilside', (0.46, 0.30, 0.20), (0.56, 0.38, 0.25), scale=2.0, rough=0.9)
    stonek = K.mottled('gstep', (0.72, 0.72, 0.72), (0.84, 0.83, 0.80), scale=5.0, rough=0.7)
    gatek = K.wood('gate', (0.92, 0.80, 0.60), 2)
    prints = K.pm('prints', (1.0, 0.95, 0.88), rough=0.7, emit=(1.0, 0.93, 0.85), es=0.25)
    o = []
    o += base(D + 0.7, grass, soil, bevel=0.18, top_t=0.2, top_bevel=0.18)
    o += steps(stonek)
    # hedges round the garden, a gap with an open gate at the back
    o += hedge('hb1', -HALF_W, -0.78, D, D + 0.7, 0.85, seed=1)
    o += hedge('hb2', 0.78, HALF_W, D, D + 0.7, 0.85, seed=2)
    o += hedge('hl', -HALF_W, -HALF_W + 0.42, 0, D, 0.55, seed=3)
    o += hedge('hr', HALF_W - 0.42, HALF_W, 0, D, 0.55, seed=4)
    for sx in (-1, 1):
        o.append(K.box('gp%d' % sx, sx * 0.72 - 0.08, D + 0.27, 0, sx * 0.72 + 0.08, D + 0.43, 0.82, gatek, bevel=0.02))
        o.append(K.ellipsoid('gpk%d' % sx, (sx * 0.72, D + 0.35, 0.87), (0.09, 0.09, 0.07), gatek))
        o += gate_leaf('gl%d' % sx, (sx * 0.64, D + 0.35, 0), sx, 70, gatek)
    # stepping stones at the way in and at the gate
    stones = [(0.08, 0.35, 0.3), (-0.5, 1.05, 0.28), (0.95, 12.35, 0.28), (0.35, 12.95, 0.3)]
    for k, (x, y, r) in enumerate(stones):
        o.append(K.cone('ss%d' % k, (x, y, -0.02), r, r - 0.03, 0.06, stonek, verts=24, bevel=0.02))

    def z_at(x, y):
        for sx_, sy_, r in stones:
            if (x - sx_) ** 2 + (y - sy_) ** 2 < r * r:
                return 0.04
        return std_z(x, y)
    # things
    o += stump('stump', (4.35, 1.4))
    o += watering_can('wcan', (-4.3, 4.0))
    o += gnome('gnome', (4.4, 7.8))
    o += bird_bath('bbath', (4.25, 10.6))
    o += bench('bench', -4.85, -2.85, 12.05, 12.75)
    o += bush('bush1', (-4.45, 6.6), 0.5, ('pink', (1.0, 0.55, 0.72)), seed=2)
    o += bush('bush2', (4.45, 5.1), 0.45, ('yellow', (1.0, 0.86, 0.35)), seed=4)
    o += bush('bush3', (-1.1, 12.1), 0.42, ('white', (0.98, 0.97, 1.0)), seed=6)
    o += flower_pot('fp1', (-0.8, 1.4), FLOWERS[0])
    o += flower_pot('fp2', (1.5, 8.3), FLOWERS[1])
    o += flower_pot('fp3', (-2.2, 4.4), FLOWERS[2])
    o += soil_circle('sc1', (4.2, 12.05), seed=1)
    o += soil_circle('sc2', (-2.2, 10.9), seed=2)
    o += flower_pot('fp4', (-2.2, 10.9), FLOWERS[3], z0=0.02)
    o += plate_button('plate', (2.8, 9.0))
    o += puddle('pd', (-0.9, 7.9), 0.75, seed=3)
    o += butterfly('bf1', (-3.7, 6.2, 1.1), (1.0, 0.7, 0.3))
    o += butterfly('bf2', (3.6, 2.4, 1.0), (0.95, 0.55, 0.85))
    world_panel(out, 'map_garden', o, GARDEN_NODES, D, H, AY, samples, prints, (-1.2, 0.9), (1.4, 12.2), z_at=z_at,
                world='garden')


# ---------------------------------------------------------------------------
# attic: old wide boards, a board wall with a round window, trunks,
# cardboard boxes, red tape crosses
# ---------------------------------------------------------------------------
ATTIC_NODES = [(2.8, 1.9), (-0.4, 3.0), (-3.2, 3.6), (-1.8, 6.0), (1.4, 6.4), (3.2, 8.8), (0.2, 10.0), (-2.6, 11.4)]
TAPES = [(0.92, 0.25, 0.25), (0.30, 0.55, 0.92), (0.98, 0.80, 0.25), (0.35, 0.75, 0.50)]


def cbox(name, c, tape, z0=0.0, w=0.66, d=0.6, h=0.52, rotz=0.0):
    card = K.pm('card', (0.80, 0.61, 0.38), rough=0.85)
    tk = K.pm('tape%d%d%d' % tuple(int(v * 9) for v in tape), tape, rough=0.3, cc=0.4)
    lab = K.pm('label', (0.99, 0.97, 0.92), rough=0.6)
    parts = [K.box(name, -w / 2, -d / 2, 0, w / 2, d / 2, h, card, bevel=0.015),
             K.box(name + 't1', -0.07, -d / 2 - 0.006, h - 0.001, 0.07, d / 2 + 0.006, h + 0.006, tk),
             K.box(name + 't2', -0.07, -d / 2 - 0.006, h - 0.2, 0.07, -d / 2 + 0.01, h + 0.006, tk),
             K.box(name + 'sm', -w / 2, -0.004, h - 0.001, w / 2, 0.004, h + 0.004, K.pm('cardseam', (0.55, 0.40, 0.24))),
             K.box(name + 'lb', w / 2 - 0.26, -d / 2 - 0.006, 0.08, w / 2 - 0.06, -d / 2 + 0.01, 0.22, lab)]
    K.parent_all(parts, name + 'root', loc=(c[0], c[1], z0), rotz=rotz)
    return parts


def tape_cross(name, c, key, L=0.8, t=0.15):
    o = []
    for k, a in enumerate((45, -45)):
        o.append(box_at(name + '%d' % k, c, L, t, 0.012, key, rotz=a))
    return o


def trunk(name, x0, x1, y0, y1, h=0.6):
    leather = K.pm('trunk', (0.66, 0.32, 0.26), rough=0.55, cc=0.3)
    band = K.pm('trunkband', (0.30, 0.18, 0.12), rough=0.5)
    gold = K.pm('knob', (1.0, 0.78, 0.3), rough=0.25, metal=1.0)
    ym, d = (y0 + y1) / 2, (y1 - y0)
    o = [K.box(name, x0, y0, 0, x1, y1, h, leather, bevel=0.04),
         K.cone(name + 'lid', (x0, ym, h), d / 2, d / 2, x1 - x0, leather, rot=(0, 90, 0), scale=(0.45, 1, 1), verts=32)]
    for bx in (x0 + 0.22, x1 - 0.22):
        o.append(K.box(name + 'b%.1f' % bx, bx - 0.05, y0 - 0.015, 0, bx + 0.05, y1 + 0.015, h + 0.005, band))
        o.append(K.cone(name + 'bl%.1f' % bx, (bx - 0.05, ym, h), d / 2 + 0.015, d / 2 + 0.015, 0.1, band,
                        rot=(0, 90, 0), scale=(0.45, 1, 1), verts=32))
    xm = (x0 + x1) / 2
    o.append(K.box(name + 'latch', xm - 0.07, y0 - 0.03, h - 0.16, xm + 0.07, y0, h + 0.02, gold, bevel=0.01))
    return o


def sheet_chair(name, c):
    sheet = K.pm('sheet', (0.95, 0.94, 0.90), rough=0.9, sheen=1.0)
    x, y = c
    parts = [K.box(name + 'a', x - 0.55, y - 0.5, 0, x + 0.55, y + 0.5, 0.45, sheet),
             K.box(name + 'b', x - 0.5, y + 0.15, 0.3, x + 0.5, y + 0.48, 0.95, sheet),
             K.ellipsoid(name + 'l', (x - 0.47, y - 0.02, 0.55), (0.12, 0.45, 0.12), sheet),
             K.ellipsoid(name + 'r', (x + 0.47, y - 0.02, 0.55), (0.12, 0.45, 0.12), sheet),
             K.cone(name + 'skirt', (x, y, 0), 0.72, 0.6, 0.3, sheet, scale=(1, 0.8, 1))]
    return [K.merge_skin(parts, name, sheet, voxel=0.03, smooth=12, factor=0.8)]


def dress_form(name, c):
    fab = K.pm('dform', (0.96, 0.76, 0.70), rough=0.8, sheen=0.8)
    wd = K.wood('dfwood', (0.45, 0.28, 0.16))
    tape = K.pm('mtape', (1.0, 0.84, 0.3), rough=0.5)
    x, y = c
    o = []
    for k in range(3):
        a = 2 * math.pi * k / 3 + 0.3
        o.append(K.tube(name + 'leg%d' % k, [(x, y, 0.35), (x + 0.2 * math.cos(a), y + 0.2 * math.sin(a), 0.12),
                                             (x + 0.3 * math.cos(a), y + 0.3 * math.sin(a), 0.0)],
                        [0.025, 0.025, 0.025], wd))
    o.append(K.cone(name + 'pole', (x, y, 0.3), 0.025, 0.025, 0.5, wd))
    body = [K.ellipsoid(name + 'hip', (x, y, 0.85), (0.24, 0.18, 0.14), fab),
            K.ellipsoid(name + 'waist', (x, y, 1.02), (0.17, 0.13, 0.14), fab),
            K.ellipsoid(name + 'bust', (x, y - 0.02, 1.2), (0.23, 0.17, 0.14), fab)]
    o.append(K.merge_skin(body, name + 'body', fab, voxel=0.015, smooth=4))
    o.append(K.cone(name + 'neck', (x, y, 1.3), 0.05, 0.04, 0.1, wd))
    o.append(K.ellipsoid(name + 'knob', (x, y, 1.42), (0.05, 0.05, 0.04), wd))
    o.append(K.tube(name + 'tape', [(x - 0.2, y - 0.12, 1.28), (x - 0.05, y - 0.19, 1.24), (x + 0.12, y - 0.18, 1.12),
                                    (x + 0.2, y - 0.16, 0.95)], [0.018] * 4, tape))
    return o


def rocking_horse(name, c):
    body = K.pm('rhorse', (0.98, 0.95, 0.88), rough=0.4, cc=0.5)
    mane = K.pm('rmane', (0.96, 0.48, 0.64), rough=0.8, sheen=1)
    saddle = K.pm('rsaddle', (0.30, 0.55, 0.92), rough=0.4, cc=0.4)
    wd = K.wood('rocker', (0.62, 0.40, 0.22))
    eye = K.pm('reye', (0.1, 0.08, 0.1))
    x, y = c
    o = []
    for sy in (-0.14, 0.14):
        o.append(K.torus(name + 'rk%.2f' % sy, (x, y + sy, 1.05), 1.02, 0.035, wd, rot=(90, 0, 0), arc=0.14,
                         n=16, m=8))
        o[-1].rotation_euler = (math.radians(90), math.radians(90 + 25.2), 0)
    for dx in (-0.22, 0.22):
        for sy in (-0.1, 0.1):
            o.append(K.cone(name + 'lg%.2f%.2f' % (dx, sy), (x + dx, y + sy, 0.07), 0.035, 0.035, 0.32, body,
                            rot=(0, dx * 40, 0)))
    o += [K.ellipsoid(name + 'b', (x, y, 0.48), (0.32, 0.15, 0.15), body),
          K.ellipsoid(name + 'nk', (x - 0.28, y, 0.62), (0.1, 0.1, 0.16), body, rot=(0, -30, 0)),
          K.ellipsoid(name + 'hd', (x - 0.38, y, 0.76), (0.16, 0.09, 0.09), body, rot=(0, 20, 0)),
          K.ellipsoid(name + 'sd', (x + 0.02, y, 0.6), (0.14, 0.16, 0.05), saddle),
          K.ellipsoid(name + 'ey', (x - 0.4, y - 0.08, 0.8), (0.025, 0.012, 0.025), eye),
          K.tube(name + 'tl', [(x + 0.3, y, 0.52), (x + 0.42, y, 0.46), (x + 0.46, y, 0.3)], [0.04, 0.035, 0.02], mane)]
    for k in range(4):
        o.append(K.ellipsoid(name + 'mn%d' % k, (x - 0.2 - k * 0.06, y + 0.01, 0.7 + k * 0.045), (0.05, 0.05, 0.05),
                             mane))
    return o


def old_lamp(name, c, h=1.25):
    brass = K.pm('brass', (0.9, 0.7, 0.35), rough=0.3, metal=1)
    shade = K.pm('oldshade', (0.98, 0.66, 0.70), rough=0.9, emit=(1.0, 0.7, 0.55), es=1.4)
    x, y = c
    C.add_point_light((x, y, h - 0.15), color=(1.0, 0.72, 0.45), power=40, radius=0.1)
    return [K.cone(name + 'b', (x, y, 0), 0.18, 0.14, 0.05, brass),
            K.cone(name + 'p', (x, y, 0.05), 0.025, 0.025, h - 0.25, brass),
            K.cone(name + 's', (x, y, h - 0.3), 0.26, 0.14, 0.3, shade),
            K.torus(name + 'fr', (x, y, h - 0.31), 0.26, 0.025, shade, n=32, m=6)]


def book_pile(name, c, n=5):
    cols = [(0.90, 0.30, 0.30), (0.25, 0.55, 0.92), (0.98, 0.78, 0.25), (0.35, 0.75, 0.45), (0.72, 0.45, 0.88)]
    rnd = random.Random(4)
    o = []
    z = 0.0
    for k in range(n):
        m = K.pm('bk%d' % k, cols[k % 5], rough=0.6)
        hgt = rnd.uniform(0.07, 0.1)
        o.append(box_at(name + '%d' % k, c, rnd.uniform(0.45, 0.6), rnd.uniform(0.32, 0.4), hgt, m, z0=z,
                        rotz=rnd.uniform(-20, 20), bevel=0.01))
        z += hgt
    return o


def round_window(name, x, z, y, r=0.36, sky=(0.60, 0.84, 0.98)):
    frame = K.wood('rwframe', (0.97, 0.93, 0.84), 3)
    glass = K.pm('rwglass%d' % int(sky[2] * 9), sky, emit=sky, es=0.9, rough=0.1)
    return [K.cone(name, (x, y + 0.01, z), r, r, 0.04, glass, rot=(90, 0, 0), verts=40),
            K.torus(name + 'f', (x, y - 0.04, z), r + 0.02, 0.05, frame, rot=(90, 0, 0)),
            K.box(name + 'x', x - r, y - 0.06, z - 0.02, x + r, y - 0.02, z + 0.02, frame),
            K.box(name + 'y', x - 0.02, y - 0.06, z - r, x + 0.02, y - 0.02, z + r, frame)]


def panel_attic(out, samples):
    C.reset('map')
    H, AY, D = 400, 368, 13.0
    floor = K.planks('aboards', (0.78, 0.57, 0.38), row=0.5, length=2.6, gap=0.022)
    boards = K.tiles('awall', (0.56, 0.40, 0.27), (0.30, 0.19, 0.12), w=0.34, h=4.0, plane='XZ',
                     col2=(0.50, 0.35, 0.23), rough=0.7, cc=0.0)
    beam = K.wood('abeam', (0.36, 0.23, 0.15), 2)
    top = K.wood('atop', (0.26, 0.16, 0.10), 3)
    side = K.wood('abase', (0.32, 0.20, 0.13), 2)
    prints = K.pm('prints', (1.0, 0.95, 0.88), rough=0.7, emit=(1.0, 0.93, 0.85), es=0.25)
    red = K.pm('xtape', (0.94, 0.18, 0.20), rough=0.35, cc=0.3)
    o = []
    o += base(D + 0.22, floor, side, trim_key=beam)
    o += steps(floor)
    o += back_wall(D, -HALF_W, HALF_W, 1.5, boards, beam, top)
    o += side_walls(D, 0.5, beam, top)
    for bx in (-4.2, -2.3, 2.3, 4.2):
        o.append(K.box('post%.1f' % bx, bx - 0.08, D - 0.08, 0, bx + 0.08, D, 1.5, beam))
    o += round_window('rw', 3.25, 0.92, D)
    o.append(K.cone('mhole', (-1.5, D - 0.004, 0.0), 0.15, 0.15, 0.01, K.pm('mhole', (0.03, 0.02, 0.02)),
                    rot=(90, 0, 0), verts=24))
    # along the back wall
    o += trunk('trunk', -4.95, -3.05, 12.2, 12.95)
    o += cbox('tb1', (-4.5, 12.55), TAPES[1], z0=0.6, w=0.6, d=0.5, h=0.42, rotz=8)
    o += cbox('tb2', (-3.55, 12.6), TAPES[2], z0=0.6, w=0.5, d=0.45, h=0.36, rotz=-6)
    o += dress_form('dform', (2.2, 12.35))
    o += tape_cross('tx2', (4.3, 12.1), red)
    o += cbox('box5', (4.3, 12.1), TAPES[3], z0=0.012)
    # the room
    o += sheet_chair('schair', (-4.2, 8.4))
    o += rocking_horse('rhorse', (4.3, 4.2))
    o += old_lamp('olamp', (-4.7, 10.4))
    o += book_pile('books', (4.55, 10.4))
    o += cbox('box1', (0.8, 1.55), TAPES[0], rotz=5)
    o += cbox('box2', (-4.45, 1.95), TAPES[1], rotz=-4)
    o += cbox('box3', (2.9, 5.0), TAPES[2], rotz=3)
    o += cbox('box4', (-1.5, 8.6), TAPES[3], rotz=-6)
    o += tape_cross('tx1', (0.2, 7.9), red)
    world_panel(out, 'map_attic', o, ATTIC_NODES, D, H, AY, samples, prints, (1.2, 0.9), (-1.4, 12.2), world='attic')


# ---------------------------------------------------------------------------
# roofs at night: terracotta tiles, chimneys and parapets, a big moon,
# glowing moon marks, crates
# ---------------------------------------------------------------------------
ROOF_NODES = [(-2.4, 1.6), (1.2, 2.2), (3.4, 4.2), (0.9, 5.8), (-2.8, 6.6), (-1.6, 9.0), (1.8, 9.6), (3.2, 11.6)]


def chimney(name, x0, x1, y0, y1, h, pots=1):
    brick = K.tiles('brick', (0.74, 0.36, 0.28), (0.45, 0.40, 0.44), w=0.24, h=0.1, offset=0.5, plane='XZ',
                    col2=(0.66, 0.31, 0.25), rough=0.8, cc=0.0)
    cap = K.pm('stonecap', (0.70, 0.68, 0.78), rough=0.6)
    potk = K.pm('cpot', (0.86, 0.48, 0.32), rough=0.6)
    smoke = K.pm('smoke', (0.78, 0.80, 0.90), rough=1.0, emit=(0.30, 0.32, 0.42), es=0.4)
    o = [K.box(name, x0, y0, 0, x1, y1, h, brick, bevel=0.02),
         K.box(name + 'c', x0 - 0.06, y0 - 0.06, h, x1 + 0.06, y1 + 0.06, h + 0.1, cap, bevel=0.02)]
    for k in range(pots):
        cx = x0 + (x1 - x0) * (k + 1) / (pots + 1)
        cy = (y0 + y1) / 2
        o.append(K.cone(name + 'p%d' % k, (cx, cy, h + 0.1), 0.13, 0.11, 0.28, potk, verts=20))
        if k == 0:
            for j, (dx, dz, r) in enumerate(((0.05, 0.55, 0.14), (0.18, 0.78, 0.18), (0.38, 0.98, 0.21))):
                o.append(K.ellipsoid(name + 'sm%d' % j, (cx + dx, cy + 0.1, h + dz), (r, r * 0.8, r * 0.8), smoke))
    return o


def antenna(name, c):
    metal = K.pm('antenna', (0.72, 0.74, 0.82), rough=0.3, metal=0.7)
    x, y = c
    o = [K.box(name + 'b', x - 0.2, y - 0.2, 0, x + 0.2, y + 0.2, 0.1, metal, bevel=0.02),
         K.cone(name + 'm', (x, y, 0.1), 0.03, 0.025, 1.4, metal)]
    for k, (z, w) in enumerate(((0.9, 0.7), (1.12, 0.55), (1.32, 0.4))):
        o.append(K.box(name + 'x%d' % k, x - w / 2, y - 0.02, z, x + w / 2, y + 0.02, z + 0.035, metal))
    return o


def water_tank(name, c):
    metal = K.pm('tank', (0.62, 0.72, 0.84), rough=0.35, metal=0.5)
    band = K.pm('tankband', (0.42, 0.50, 0.62), rough=0.35, metal=0.5)
    x, y = c
    o = [K.cone(name, (x, y, 0.28), 0.44, 0.44, 0.62, metal, verts=32),
         K.cone(name + 'l', (x, y, 0.9), 0.46, 0.08, 0.16, band, verts=32),
         K.torus(name + 'b1', (x, y, 0.45), 0.445, 0.02, band, n=32, m=6),
         K.torus(name + 'b2', (x, y, 0.72), 0.445, 0.02, band, n=32, m=6)]
    for dx in (-0.3, 0.3):
        for dy in (-0.3, 0.3):
            o.append(K.box(name + 'lg%.1f%.1f' % (dx, dy), x + dx - 0.03, y + dy - 0.03, 0, x + dx + 0.03, y + dy + 0.03,
                           0.3, band))
    return o


def pigeon_house(name, c):
    wood = K.pm('phouse', (0.96, 0.92, 0.84), rough=0.6)
    roof = K.pm('phroof', (0.40, 0.55, 0.85), rough=0.5)
    post = K.wood('phpost', (0.50, 0.33, 0.20))
    hole = K.pm('phhole', (0.05, 0.04, 0.06))
    white = K.pm('pigeon', (0.94, 0.94, 0.98), rough=0.6)
    beak = K.pm('beak', (1.0, 0.65, 0.2), rough=0.5)
    x, y = c
    o = [K.box(name + 'p', x - 0.05, y - 0.05, 0, x + 0.05, y + 0.05, 0.6, post),
         K.box(name + 'h', x - 0.35, y - 0.25, 0.6, x + 0.35, y + 0.25, 1.0, wood, bevel=0.02),
         box_at(name + 'r1', (x, y - 0.16), 0.86, 0.4, 0.05, roof, z0=1.02, bevel=0.01),
         box_at(name + 'r2', (x, y + 0.16), 0.86, 0.4, 0.05, roof, z0=1.02, bevel=0.01)]
    o[-2].rotation_euler = (math.radians(28), 0, 0)
    o[-1].rotation_euler = (math.radians(-28), 0, 0)
    for dx in (-0.16, 0.16):
        o.append(K.cone(name + 'o%.1f' % dx, (x + dx, y - 0.25, 0.8), 0.08, 0.08, 0.01, hole, rot=(90, 0, 0), verts=20))
        o.append(K.box(name + 'pe%.1f' % dx, x + dx - 0.08, y - 0.33, 0.68, x + dx + 0.08, y - 0.25, 0.7, wood))
    o += [K.ellipsoid(name + 'bd', (x + 0.08, y - 0.05, 1.16), (0.14, 0.09, 0.08), white, rot=(0, -10, 0)),
          K.ellipsoid(name + 'bh', (x - 0.06, y - 0.08, 1.26), (0.06, 0.06, 0.06), white),
          K.cone(name + 'bk', (x - 0.11, y - 0.1, 1.26), 0.018, 0.0, 0.05, beak, rot=(0, -90, 0))]
    return o


def skylight(name, c):
    frame = K.pm('skyframe', (0.70, 0.68, 0.78), rough=0.5)
    glass = K.pm('skyglass', (1.0, 0.62, 0.30), emit=(1.0, 0.55, 0.22), es=0.6, rough=0.1)
    x, y = c
    C.add_point_light((x, y, 0.8), color=(1.0, 0.72, 0.4), power=45, radius=0.3)
    o = [K.box(name, x - 0.5, y - 0.4, 0, x + 0.5, y + 0.4, 0.28, frame, bevel=0.03),
         K.box(name + 'g', x - 0.42, y - 0.32, 0.27, x + 0.42, y + 0.32, 0.3, glass),
         K.box(name + 'x', x - 0.02, y - 0.35, 0.28, x + 0.02, y + 0.35, 0.32, frame)]
    return o


def dish(name, c):
    white = K.pm('dish', (0.94, 0.94, 0.97), rough=0.4, cc=0.3)
    metal = K.pm('antenna', (0.72, 0.74, 0.82), rough=0.3, metal=0.7)
    x, y = c
    d = K.ellipsoid(name, (x, y - 0.05, 0.55), (0.32, 0.32, 0.1), white, rot=(-55, 0, 25))
    return [K.box(name + 'b', x - 0.15, y - 0.15, 0, x + 0.15, y + 0.15, 0.08, metal),
            K.cone(name + 'p', (x, y, 0.08), 0.03, 0.03, 0.42, metal), d,
            K.tube(name + 'arm', [(x, y - 0.05, 0.55), (x - 0.1, y - 0.25, 0.7), (x - 0.12, y - 0.3, 0.76)],
                   [0.015] * 3, metal),
            K.ellipsoid(name + 'lnb', (x - 0.12, y - 0.3, 0.77), (0.035, 0.035, 0.035), metal)]


def crate(name, c, stencil, z0=0.0, s=0.7):
    wood = K.stripes('crate_r', (0.76, 0.56, 0.34), (0.68, 0.48, 0.29), width=s / 8, axis='Z', rough=0.7)
    frame = K.wood('crateframe', (0.52, 0.34, 0.20), 2)
    paint = K.pm('stencil', (0.99, 0.95, 0.82), rough=0.6)
    x, y = c
    h = s
    o = [K.box(name, x - s / 2, y - s / 2, z0, x + s / 2, y + s / 2, z0 + h, wood, bevel=0.01)]
    for sx in (-1, 1):
        o.append(K.box(name + 'v%d' % sx, x + sx * s / 2 - (0.07 if sx > 0 else 0) - 0.005 * sx, y - s / 2 - 0.02, z0,
                       x + sx * s / 2 + (0.07 if sx < 0 else 0) + 0.005 * sx, y + s / 2 + 0.02, z0 + h, frame))
    for zz in (z0, z0 + h - 0.06):       # rims along the front and back edges (no face flush with the top)
        for k, (ya, yb) in enumerate(((y - s / 2 - 0.02, y - s / 2 + 0.07), (y + s / 2 - 0.07, y + s / 2 + 0.02))):
            o.append(K.box(name + 'h%.2f%d' % (zz, k), x - s / 2 - 0.01, ya, zz, x + s / 2 + 0.01, yb, zz + 0.07, frame))
    fy = y - s / 2 - 0.025
    cz = z0 + h / 2
    if stencil == 'paw':
        o += K.paw_upright(name + 'st', (x, fy, cz - 0.02), 1.5, paint, depth=0.006)
    elif stencil == 'star':
        pts = []
        for k in range(10):
            a = math.pi / 2 + k * math.pi / 5
            r = 0.2 if k % 2 == 0 else 0.09
            pts.append((x + r * math.cos(a), cz + r * math.sin(a)))
        st = K.puffy(name + 'st', pts, paint, t=0.003, dome=0.0, bevel=0.0, levels=0)
        st.location.y = fy
        o.append(st)
    else:   # fish
        o.append(K.ellipsoid(name + 'st', (x - 0.03, fy, cz), (0.14, 0.006, 0.08), paint))
        tail = K.puffy(name + 'st2', [(x + 0.09, cz), (x + 0.2, cz + 0.08), (x + 0.2, cz - 0.08)], paint, t=0.003,
                       dome=0.0, bevel=0.0, levels=0)
        tail.location.y = fy
        o.append(tail)
    return o


def moon_mark(name, c, key, glow=True):
    o = [K.flat_poly(name, K.crescent_pts(c, 0.36, 0.2, rot=math.radians(35)), key, z=0.006)]
    if glow:
        C.add_point_light((c[0], c[1], 0.35), color=(1.0, 0.9, 0.55), power=10, radius=0.2)
    return o


def hole(name, c, rim_key):
    dark = K.pm('holedark', (0.02, 0.02, 0.035), rough=1)
    inner = K.pm('holein', (0.16, 0.10, 0.12), rough=1)
    x, y = c
    return [K.box(name, x - 0.45, y - 0.45, -0.2, x + 0.45, y + 0.45, 0.001, dark),
            K.box(name + 'in', x - 0.45, y + 0.3, -0.2, x + 0.45, y + 0.45, 0.002, inner),
            K.box(name + 'r1', x - 0.52, y - 0.52, 0, x + 0.52, y - 0.45, 0.04, rim_key, bevel=0.01),
            K.box(name + 'r2', x - 0.52, y + 0.45, 0, x + 0.52, y + 0.52, 0.04, rim_key, bevel=0.01),
            K.box(name + 'r3', x - 0.52, y - 0.45, 0, x - 0.45, y + 0.45, 0.04, rim_key, bevel=0.01),
            K.box(name + 'r4', x + 0.45, y - 0.45, 0, x + 0.52, y + 0.45, 0.04, rim_key, bevel=0.01)]


def rubber_ball(name, c, col=(0.95, 0.30, 0.38), r=0.28):
    m = K.pm('ball%d' % int(col[1] * 9), col, rough=0.3, cc=0.8)
    w = K.pm('ballw', (0.98, 0.96, 0.92), rough=0.3, cc=0.8)
    x, y = c
    return [K.ellipsoid(name, (x, y, r), (r, r, r), m),
            K.torus(name + 's', (x, y, r), r * 0.98, 0.05, w, rot=(70, 20, 0), n=32, m=8)]


def world_at(pxx, pxy, AY, y):
    """The world point at depth y that lands on panel pixel (pxx, pxy)."""
    return ((pxx - W // 2) / PXM, y, (AY - PYM * y - pxy) / PZM)


def roof_tiles(key, col, grout, w=0.52, h=0.38):
    """Overlapping terracotta roof tiles: staggered tiles, each row shaded
    from its lower edge (in the light) to its top (under the next row)."""
    if key in C._MATS:
        return key

    def build(nt, neutral):
        b = K._bsdf(nt, col, 0.5, 0.35, 0.0, 0.0, 0.3, neutral)
        if neutral:
            return b.outputs['BSDF']
        br = nt.nodes.new('ShaderNodeTexBrick')
        br.offset = 0.5
        br.offset_frequency = 2
        br.inputs['Scale'].default_value = 1.0
        br.inputs['Brick Width'].default_value = w
        br.inputs['Row Height'].default_value = h
        br.inputs['Mortar Size'].default_value = 0.012
        br.inputs['Color1'].default_value = tuple(col) + (1,)
        br.inputs['Color2'].default_value = tuple(c * 0.9 for c in col) + (1,)
        br.inputs['Mortar'].default_value = tuple(grout) + (1,)
        nt.links.new(K._coords(nt), br.inputs['Vector'])
        wv = nt.nodes.new('ShaderNodeTexWave')
        wv.wave_type = 'BANDS'
        wv.bands_direction = 'Y'
        wv.wave_profile = 'SAW'
        wv.inputs['Scale'].default_value = 1.0 / (2 * h)
        wv.inputs['Distortion'].default_value = 0.0
        nt.links.new(K._coords(nt), wv.inputs['Vector'])
        shade = K._ramp(nt, wv.outputs['Fac'], (1.15, 1.15, 1.15), (0.5, 0.5, 0.5), 0.0, 1.0)
        mix = nt.nodes.new('ShaderNodeMixRGB')
        mix.blend_type = 'MULTIPLY'
        mix.inputs['Fac'].default_value = 1.0
        nt.links.new(br.outputs['Color'], mix.inputs['Color1'])
        nt.links.new(shade, mix.inputs['Color2'])
        nt.links.new(mix.outputs['Color'], b.inputs['Base Color'])
        return b.outputs['BSDF']
    return C.mat(key, base=col, build=build)


def panel_roofs(out, samples):
    C.reset('map')
    C.set_light('roofs')            # the roofs are at night: the moonlight of the roofs kit
    H, AY, D = 440, 408, 13.4
    floor = roof_tiles('rtiles', (0.98, 0.50, 0.30), (0.36, 0.16, 0.10))
    brick = K.tiles('brick', (0.74, 0.36, 0.28), (0.45, 0.40, 0.44), w=0.24, h=0.1, offset=0.5, plane='XZ',
                    col2=(0.66, 0.31, 0.25), rough=0.8, cc=0.0)
    cap = K.pm('stonecap', (0.70, 0.68, 0.78), rough=0.6)
    plaster = K.pm('plaster', (0.52, 0.52, 0.66), rough=0.8)
    lit = K.pm('litwin', (1.0, 0.80, 0.45), emit=(1.0, 0.72, 0.35), es=2.0)
    prints = K.pm('mprints', (1.0, 0.92, 0.62), rough=0.6, emit=(1.0, 0.88, 0.5), es=1.1)
    markk = K.pm('moonmark', (1.0, 0.92, 0.55), emit=(1.0, 0.88, 0.45), es=2.6)
    o = []
    o += base(D + 0.22, floor, plaster, trim_key=cap)
    for k, wx in enumerate((-4.4, -2.6, 2.1, 3.9)):     # lit windows in the house below
        o.append(K.box('lw%d' % k, wx - 0.2, -0.012, -0.42, wx + 0.2, 0.0, -0.2, lit))
    o += steps(brick)
    o += back_wall(D, -HALF_W, HALF_W, 0.8, brick, brick, cap)
    o += side_walls(D, 0.45, brick, cap)
    # the sky: a big moon rising behind the parapet, stars
    moon = K.pm('moon', (1.0, 0.97, 0.82), emit=(1.0, 0.95, 0.75), es=2.4)
    crater = K.pm('crater', (0.9, 0.86, 0.7), emit=(0.92, 0.86, 0.62), es=1.8)
    halo1 = K.pm('halo1', (0.1, 0.1, 0.2), emit=(0.16, 0.18, 0.36), es=1.0)
    halo2 = K.pm('halo2', (0.05, 0.05, 0.1), emit=(0.08, 0.09, 0.2), es=1.0)
    mc = V(world_at(84, 50, AY, 15.2))
    o.append(K.facing_disc('moon', mc, 1.12, moon, verts=64))
    for k, (dx, dz, r) in enumerate(((-0.35, 0.3, 0.22), (0.3, -0.2, 0.16), (0.1, 0.45, 0.1), (-0.2, -0.45, 0.13))):
        o.append(K.facing_disc('cr%d' % k, mc + C.R * dx + C.UP * dz - C.F * 0.01, r, crater, verts=24))
    o.append(K.facing_disc('halo1', mc + C.F * 0.05, 1.45, halo1, verts=64))
    o.append(K.facing_disc('halo2', mc + C.F * 0.1, 1.85, halo2, verts=64))
    star = K.pm('star', (1, 1, 0.9), emit=(1, 0.97, 0.85), es=4.0)
    for k, (sx_, sy_, r) in enumerate(((168, 14, 0.05), (250, 34, 0.06), (300, 12, 0.045), (340, 48, 0.05),
                                       (218, 56, 0.04), (150, 70, 0.035), (20, 108, 0.04), (286, 76, 0.035))):
        o.append(K.facing_disc('st%d' % k, V(world_at(sx_, sy_, AY, 16.0)), r, star, verts=12))
    # along the back
    o += chimney('ch2', -4.9, -3.0, 11.9, 12.7, 1.25, pots=2)
    o += dish('dish', (-1.8, 12.2))
    o += pigeon_house('pig', (4.5, 10.0))
    # the roof
    o += chimney('ch1', 4.3, 4.95, 6.9, 7.55, 0.95)
    o += antenna('ant', (-4.5, 4.0))
    o += water_tank('tank', (4.3, 1.6))
    o += skylight('sky', (-0.4, 7.9))
    o += crate('cr1', (-0.4, 3.9), 'paw')
    o += crate('cr2', (-4.3, 8.4), 'star')
    o += crate('cr3', (2.9, 7.6), 'fish')
    o += moon_mark('mm1', (4.1, 5.6), markk)
    o += moon_mark('mm2', (-2.6, 3.95), markk)
    o += hole('hole', (0.6, 11.2), K.pm('holerim', (0.62, 0.30, 0.20), rough=0.6))
    o += rubber_ball('ball', (-3.9, 10.6))
    world_panel(out, 'map_roofs', o, ROOF_NODES, D, H, AY, samples, prints, (-0.8, 0.7), (1.3, 12.6), world='roofs')


# ---------------------------------------------------------------------------
# soon: the path climbs into soft clouds (more to come)
# ---------------------------------------------------------------------------

def cloud(name, c, w, h, key, seed=1):
    rnd = random.Random(seed)
    x, y, z = c
    parts = []
    n = max(4, int(w / 0.45))
    for k in range(n):
        t = k / (n - 1) - 0.5
        r = h * (0.62 + 0.38 * math.cos(t * math.pi)) * rnd.uniform(0.85, 1.1)
        parts.append(K.ellipsoid(name + '%d' % k, (x + t * w, y + rnd.uniform(-0.2, 0.2), z + r * 0.55),
                                 (r * 1.05, r * 0.8, r), key, seg=20, rings=12))
    parts.append(K.ellipsoid(name + 'base', (x, y, z + h * 0.35), (w * 0.55, h * 0.7, h * 0.45), key, seg=24, rings=12))
    return [K.merge_skin(parts, name, key, voxel=0.05, smooth=6)]


def sparkle(name, c, r, key):
    pts = []
    for k in range(8):
        a = math.pi / 2 + k * math.pi / 4
        rr = r if k % 2 == 0 else r * 0.3
        pts.append((c[0] + rr * math.cos(a), c[2] + rr * math.sin(a)))
    ob = K.puffy(name, pts, key, t=0.01, dome=0.02, bevel=0.0, levels=1)
    ob.location.y = c[1]
    return [ob]


def panel_soon(out, samples):
    C.reset('map')
    H, AY = 320, 292
    prints = K.pm('prints', (1.0, 0.95, 0.88), rough=0.7, emit=(1.0, 0.93, 0.85), es=0.25)
    wd = K.wood('signwood', (0.72, 0.50, 0.30), 2)
    qk = K.pm('qmark', (0.95, 0.40, 0.62), rough=0.3, cc=0.6)
    tints = [K.mottled('cl%d' % k, c0, c1, scale=1.5, rough=0.9, sheen=0.4)
             for k, (c0, c1) in enumerate((((0.62, 0.60, 0.74), (0.54, 0.52, 0.68)),
                                          ((0.56, 0.50, 0.66), (0.48, 0.44, 0.62)),
                                          ((0.46, 0.44, 0.64), (0.40, 0.38, 0.58)),
                                          ((0.36, 0.36, 0.56), (0.31, 0.31, 0.50))))]
    o = []
    # soft banks of clouds, darker and bluer towards the top; the trail walks
    # into the middle one; gaps of night between them
    rows = [(3.6, 0.7, ((-3.9, 2.0), (0.1, 2.6), (3.9, 2.0)), 0),
            (6.0, 0.9, ((-2.3, 2.4), (2.1, 2.4)), 1),
            (8.4, 1.0, ((-4.3, 1.8), (0.0, 2.8), (4.2, 1.9)), 2),
            (10.8, 1.1, ((-2.2, 2.6), (2.6, 2.4)), 3)]
    for i, (y, h, xs, t) in enumerate(rows):
        for j, (x, w) in enumerate(xs):
            o += cloud('c%d%d' % (i, j), (x, y + (j % 2) * 0.3, -0.2), w, h * (0.9 + 0.2 * ((i + j) % 2)), tints[t],
                       seed=i * 10 + j)
    # a little signpost with a question mark by the path
    sx, sy = -1.55, 1.6
    o += [K.box('spost', sx - 0.06, sy - 0.06, 0, sx + 0.06, sy + 0.06, 1.05, wd),
          K.box('sboard', sx - 0.42, sy - 0.09, 0.6, sx + 0.42, sy - 0.03, 1.12, wd, bevel=0.03)]
    q = [(sx - 0.13, sy - 0.12, 0.98), (sx - 0.08, sy - 0.12, 1.06), (sx + 0.06, sy - 0.12, 1.06),
         (sx + 0.13, sy - 0.12, 0.97), (sx + 0.05, sy - 0.12, 0.88), (sx, sy - 0.12, 0.8)]
    o.append(K.tube('q', q, [0.045] * len(q), qk))
    o.append(K.ellipsoid('qd', (sx, sy - 0.12, 0.69), (0.05, 0.04, 0.05), qk))
    gold = K.pm('sparkg', (1.0, 0.86, 0.4), emit=(1.0, 0.8, 0.3), es=2.0)
    pinkk = K.pm('sparkp', (1.0, 0.7, 0.85), emit=(1.0, 0.6, 0.8), es=1.6)
    for k, (x, y, z, r) in enumerate(((-4.2, 4.8, 1.1, 0.2), (0.1, 7.3, 1.2, 0.24), (-4.0, 9.6, 1.4, 0.17),
                                      (4.2, 4.9, 1.0, 0.16), (4.1, 9.8, 1.5, 0.2), (0.2, 12.6, 1.5, 0.15),
                                      (-2.0, 12.4, 2.0, 0.12))):
        o += sparkle('sp%d' % k, (x, y, z), r, gold if k % 2 == 0 else pinkk)
    trail = [(0, -1.2), (0, 0.4), (0.35, 1.6), (0.1, 3.0), (0, 4.5)]
    o += K.paw_trail('pr', trail, prints, lambda x, y: 0.0, spacing=0.52, side=0.1, s=1.05, start=0.1)
    panel_render(out, 'map_soon', o, H, AY, samples, dict(nodes=[], entry=[W // 2, H], exit=None, world='soon'))


# ---------------------------------------------------------------------------
# the stones and the marker
# ---------------------------------------------------------------------------
STONE_R = 0.62
STONE_H = 0.2


def stone(out, name, top, rim, pawc, samples, emit=0.0, sheen=0.0):
    C.reset('map')
    kt = K.pm(name + '_top', top, rough=0.45, cc=0.4, sheen=sheen)
    kr = K.pm(name + '_rim', rim, rough=0.4, cc=0.4)
    kp = K.pm(name + '_paw', pawc, rough=0.6, emit=pawc if emit else None, es=emit)
    o = stone_parts(kt, kr, kp)
    o.append(K.catcher('stc', (0.5, 0.5), STONE_R + 0.35))
    render(out, name, o, None, samples, anchor=C.cell(0, 0, 0), kind='map_stone',
           extra=dict(top_px=int(round(STONE_H * PZM))))


def stone_parts(kt, kr, kp):
    o = [K.cone('st', (0, 0, 0), STONE_R, STONE_R - 0.05, STONE_H, kt, verts=48, bevel=0.07),
         K.torus('strim', (0, 0, STONE_H - 0.02), STONE_R - 0.13, 0.035, kr, n=48, m=8)]
    o += K.paw('stp', (0.0, -0.02), math.pi / 2, 2.0, kp, z=STONE_H - 0.01, t=0.02, seg=20)
    for ob in o:
        ob.location.x += 0.5
        ob.location.y += 0.5
    return o


def _blur(a, sigma):
    r = int(sigma * 3)
    k = np.exp(-0.5 * (np.arange(-r, r + 1) / sigma) ** 2)
    k /= k.sum()
    p = np.pad(a, r)
    p = np.apply_along_axis(lambda v: np.convolve(v, k, 'same'), 0, p)
    p = np.apply_along_axis(lambda v: np.convolve(v, k, 'same'), 1, p)
    return p[r:-r, r:-r]


def stone_ring(out, samples):
    """The current level's glow: a warm ring on the ground round the stone
    (its back hidden by the stone) with a soft halo. Drawn over the stone and
    under map_mila, same anchor as the stones."""
    C.reset('map')
    hold = stone_parts(K.pm('h_top', (0.8, 0.8, 0.8)), K.pm('h_rim', (0.8, 0.8, 0.8)), K.pm('h_paw', (0.8, 0.8, 0.8)))
    glow = K.pm('ringglow', (1.0, 0.86, 0.45), emit=(1.0, 0.80, 0.35), es=4.0)
    ring = K.torus('ring', (0.5, 0.5, 0.03), STONE_R + 0.14, 0.055, glow, n=64, m=10)
    render(out, 'map_stone_ring', [ring], None, samples, anchor=C.cell(0, 0, 0), holdout=hold, kind='map_ring',
           margin=10, extra=dict(draw='over the current stone, under map_mila'))
    path = os.path.join(out, 'map_stone_ring.png')
    im = K.read_png(path).astype(np.float32) / 255.0
    a = im[..., 3]
    halo = np.clip(_blur(a, 3.0) * 1.6, 0, 1) * 0.7
    hc = np.array([1.0, 0.80, 0.40])
    oa = a + halo * (1 - a)
    rgb = (im[..., :3] * a[..., None] + hc * (halo * (1 - a))[..., None]) / np.maximum(oa[..., None], 1e-6)
    res = np.zeros(im.shape, np.float32)
    res[..., :3] = rgb
    res[..., 3] = oa
    C.write_png(path, np.round(np.clip(res, 0, 1) * 255).astype(np.uint8))


def marker(out, samples):
    C.reset('neutral')      # Mila is final colour under the neutral light, as in the game
    zm = 0.9
    lift = STONE_H * PZM / (C.FLOOR_PX / C.FLOOR_M * zm)     # stand on the stone's top
    o = K.mila('mk_', 'sit')
    K.parent_all(o, 'mila', loc=(0.5, 0.5, lift), rotz=0)
    o.append(K.catcher('mkc', (0.5, 0.5), 0.34, z=lift))
    render(out, 'map_mila', o, None, samples, anchor=C.cell(0, 0, 0), zoom=zm, kind='map_marker', margin=8,
           extra=dict(stands_on='map_stone'))


def main():
    a = C.args({'samples': 128})
    out = os.path.abspath(a.out)
    jobs = [
        ('map_home', lambda: panel_home(out, a.samples)),
        ('map_living', lambda: panel_living(out, a.samples)),
        ('map_kitchen', lambda: panel_kitchen(out, a.samples)),
        ('map_garden', lambda: panel_garden(out, a.samples)),
        ('map_attic', lambda: panel_attic(out, a.samples)),
        ('map_roofs', lambda: panel_roofs(out, a.samples)),
        ('map_soon', lambda: panel_soon(out, a.samples)),
        ('map_stone', lambda: stone(out, 'map_stone', (0.99, 0.94, 0.86), (0.96, 0.55, 0.66), (0.94, 0.42, 0.58),
                                    a.samples)),
        ('map_stone_done', lambda: stone(out, 'map_stone_done', (1.0, 0.68, 0.16), (1.0, 0.88, 0.45),
                                         (1.0, 0.97, 0.90), a.samples, emit=0.4)),
        ('map_stone_locked', lambda: stone(out, 'map_stone_locked', (0.33, 0.32, 0.41), (0.27, 0.26, 0.34),
                                           (0.25, 0.24, 0.31), a.samples)),
        ('map_stone_ring', lambda: stone_ring(out, a.samples)),
        ('map_mila', lambda: marker(out, a.samples)),
    ]
    t0 = time.time()
    for name, fn in jobs:
        if a.only and name not in a.only:
            continue
        fn()
    C._STATE['meta'].clear()
    C._STATE['meta'].update(META)
    C.save_meta(out)
    print('map: %d sprites in %.1fs' % (len(META), time.time() - t0))
    for k, v in TIMES.items():
        print('  %-18s %.1fs' % (k, v))


main()
