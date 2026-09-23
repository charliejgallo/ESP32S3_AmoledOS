"""Mila - the attic and roofs kits (SPEC.md section 5).

    Blender -b -P worlds_b.py -- --out ../../assets [--world attic,roofs] [--phase 1] [--only a,b]

--out is the assets root: sprites go to <out>/attic and <out>/roofs.
Everything is built at cell (0, 0) in world metres (see ml_common) and
rendered one sprite at a time in final colour under the world's light.
"""
import math
import os
import random
import sys
import time

import bpy
import mathutils

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ml_common as C          # noqa: E402
import wb_lib as L             # noqa: E402
from wb_lib import box, cbox, ellipsoid, cone, torus, tube, prism, annulus  # noqa: E402

H = C.FLOOR_M                  # a wall's height (one floor)

# ===========================================================================
# ATTIC
# ===========================================================================

A_BOARD = (0.87, 0.66, 0.43)


def attic_mats():
    for k in range(6):
        rnd = random.Random(40 + k)
        s = rnd.uniform(0.9, 1.07)
        col = (A_BOARD[0] * s, A_BOARD[1] * s * rnd.uniform(0.97, 1.02), A_BOARD[2] * s * rnd.uniform(0.94, 1.04))
        L.wood('a_board%d' % k, col, scale=1.2, offset=(rnd.uniform(0, 9), rnd.uniform(0, 9), 0), dark=0.84,
               lines=0.12, rough=0.62, cc=0.05)
    L.solid('a_gap', (0.24, 0.14, 0.08), rough=0.9, spec=0.2)
    L.solid('a_nail', (0.30, 0.26, 0.24), rough=0.35, metal=0.6)
    L.solid('a_knot', (0.52, 0.32, 0.17), rough=0.7)
    L.wood('a_top', (0.30, 0.17, 0.10), scale=1.5, dark=0.8, lines=0.1)
    L.wood('a_top2', (0.27, 0.155, 0.095), scale=1.5, dark=0.8, lines=0.1, offset=(3, 1, 0))
    L.solid('a_topgap', (0.12, 0.07, 0.04), rough=0.9)
    L.mottled('a_trunk', (0.22, 0.44, 0.45), var=0.07, scale=9, rough=0.75, sheen=0.4)
    L.mottled('a_trunk2', (0.62, 0.30, 0.24), var=0.07, scale=9, rough=0.75, sheen=0.4)
    L.wood('a_slat', (0.78, 0.56, 0.32), scale=2)
    L.solid('a_leather', (0.46, 0.26, 0.14), rough=0.6)
    L.solid('a_brass', (0.98, 0.74, 0.34), rough=0.28, metal=1.0)
    L.solid('a_dark', (0.08, 0.05, 0.04), rough=0.8)
    L.wood('a_crate', (0.84, 0.66, 0.42), scale=2, dark=0.82)
    L.wood('a_crate2', (0.78, 0.58, 0.36), scale=2, dark=0.82, offset=(5, 2, 0))
    L.wood('a_post', (0.58, 0.40, 0.24), scale=2)
    L.solid('a_crate_in', (0.26, 0.16, 0.09), rough=0.9)
    L.solid('a_tape', (0.97, 0.04, 0.07), rough=0.38, spec=0.5, cc=0.25)
    L.mottled('a_card', (0.80, 0.60, 0.38), var=0.05, scale=14, rough=0.85, spec=0.25)
    L.solid('a_card_dk', (0.50, 0.35, 0.20), rough=0.9)
    L.solid('a_label', (0.98, 0.96, 0.92), rough=0.5)
    L.solid('a_sheet', (0.93, 0.88, 0.80), rough=0.92, sheen=1.0, spec=0.3)
    L.solid('a_foot', (0.40, 0.24, 0.13), rough=0.5)
    L.solid('a_horse', (0.98, 0.95, 0.90), rough=0.35, cc=0.4, spec=0.5)
    L.solid('a_dapple', (0.72, 0.72, 0.78), rough=0.4, cc=0.3)
    L.solid('a_mane', (0.56, 0.32, 0.17), rough=0.8, sheen=0.6)
    L.solid('a_saddle', (0.90, 0.22, 0.30), rough=0.4, cc=0.3)
    L.solid('a_rocker', (0.86, 0.30, 0.26), rough=0.35, cc=0.4)
    L.solid('a_eye', (0.03, 0.02, 0.02), rough=0.2, spec=0.8)
    for k, col in enumerate(((0.92, 0.50, 0.42), (0.98, 0.86, 0.60), (0.52, 0.72, 0.76))):
        L.solid('a_rug%d' % k, col, rough=1.0, sheen=1.0, spec=0.2)
    L.wood('a_board_door', (0.90, 0.84, 0.72), scale=2, dark=0.9, lines=0.08)
    L.solid('a_flapframe', (0.20, 0.72, 0.64), rough=0.3, cc=0.4, spec=0.5)
    L.solid('a_flap', (0.72, 0.92, 0.86), rough=0.3, cc=0.5, spec=0.5)
    L.solid('a_flappaw', (1.0, 1.0, 1.0), rough=0.5)
    L.solid('a_hole', (0.05, 0.035, 0.03), rough=1.0)
    L.solid('a_shade', (1.0, 0.72, 0.66), rough=0.9, sheen=1.0, emit=(1.0, 0.62, 0.45), es=0.9)
    L.solid('a_bulb', (1.0, 0.9, 0.7), emit=(1.0, 0.85, 0.6), es=4.0)
    L.solid('a_form', (0.96, 0.74, 0.70), rough=0.85, sheen=1.0, spec=0.3)
    L.solid('a_mtape', (1.0, 0.84, 0.20), rough=0.5, cc=0.2)
    L.solid('a_iron', (0.20, 0.19, 0.23), rough=0.35, metal=0.7)
    L.solid('a_pages', (0.97, 0.93, 0.82), rough=0.8)
    L.solid('a_cup', (0.98, 0.97, 0.95), rough=0.25, cc=0.6)
    L.solid('a_tea', (0.55, 0.28, 0.12), rough=0.1, spec=0.8)
    L.solid('a_verdigris', (0.36, 0.72, 0.62), rough=0.4, metal=0.4, cc=0.3)
    L.solid('a_plate_mark', (0.98, 0.86, 0.50), rough=0.3, metal=1.0)
    for k, col in enumerate(((0.80, 0.22, 0.22), (0.18, 0.52, 0.56), (0.95, 0.68, 0.20), (0.22, 0.30, 0.58),
                             (0.36, 0.58, 0.30), (0.58, 0.30, 0.52), (0.92, 0.50, 0.38))):
        L.solid('a_bk%d' % k, col, rough=0.6, sheen=0.3)
    for k, col in enumerate(((0.30, 0.78, 0.70), (1.0, 0.80, 0.25), (0.98, 0.52, 0.64), (0.98, 0.97, 0.92))):
        L.solid('a_stk%d' % k, col, rough=0.6)


A_TAPES = [((0.12, 0.74, 0.66), 'heart'), ((1.00, 0.72, 0.08), 'star'),
           ((0.22, 0.46, 0.98), 'fish'), ((0.98, 0.36, 0.62), 'paw')]


def attic_floor(v, name='af'):
    """Three wide old boards along X; they end at the cell's edges (the grid
    reads as a column of joints); nails at the ends, a knot or a joint here
    and there."""
    rnd = random.Random(1000 + v)
    o = [box(name + 'u', -0.02, -0.02, -0.07, 1.02, 1.02, -0.015, 'a_gap')]
    splits = {0: [], 1: [(1, 0.42)], 2: [(0, 0.63), (2, 0.30)], 3: [(2, 0.55)]}[v % 4]
    for k in range(3):
        y0 = k / 3.0 + 0.007
        y1 = (k + 1) / 3.0 - 0.007
        xs = [0.0] + sorted(s for (b, s) in splits if b == k) + [1.0]
        for j in range(len(xs) - 1):
            x0, x1 = xs[j] + 0.004, xs[j + 1] - 0.004
            key = 'a_board%d' % rnd.randrange(6)
            o.append(box(name + 'b%d%d' % (k, j), x0, y0, -0.045, x1, y1, 0.0, key, bevel=0.007))
            yc = (y0 + y1) / 2
            for xn in (x0 + 0.045, x1 - 0.045):
                o.append(cone(name + 'n%d%d%.2f' % (k, j, xn), (xn, yc, -0.004), 0.016, 0.014, 0.006, 'a_nail',
                              verts=12))
    # knots
    for q in range(1 + v % 2):
        k = rnd.randrange(3)
        cx, cy = rnd.uniform(0.2, 0.8), k / 3.0 + 1 / 6.0 + rnd.uniform(-0.06, 0.06)
        o.append(ellipsoid(name + 'kn%d' % q, (cx, cy, -0.002), (0.035, 0.018, 0.004), 'a_knot', seg=16, rings=6))
    return o


def attic_top(name, x0, y0, x1, y1, z0):
    """The dark wooden lid on top of a wall: three planks along X."""
    o = [box(name + 'tu', x0, y0, z0, x1, y1, H - 0.012, 'a_topgap')]
    for k in range(3):
        a = y0 + (y1 - y0) * k / 3.0 + 0.006
        b = y0 + (y1 - y0) * (k + 1) / 3.0 - 0.006
        o.append(box(name + 'tp%d' % k, x0, a, z0 + 0.006, x1, b, H, 'a_top' if k != 1 else 'a_top2',
                     bevel=0.008))
    return o


def attic_wall(v, name='aw'):
    o = attic_top(name, -0.015, -0.015, 1.015, 1.0, H - 0.05)
    zt = H - 0.05
    if v == 0:
        # an old steamer trunk: teal canvas, pale slats, leather straps, brass
        o.append(box(name + 'tr', 0.012, 0.02, 0.0, 0.988, 1.0, zt, 'a_trunk', bevel=0.012))
        for z in (0.09, 0.27):
            o.append(box(name + 'sl%.2f' % z, 0.02, 0.0, z, 0.98, 0.03, z + 0.055, 'a_slat', bevel=0.006))
        for x in (0.22, 0.72):
            o.append(box(name + 'st%.2f' % x, x, -0.006, 0.0, x + 0.06, 0.03, zt, 'a_leather', bevel=0.004))
        for x in (0.012, 0.908):
            for z in (0.0, zt - 0.08):
                o.append(box(name + 'bc%.2f%.2f' % (x, z), x, -0.004, z, x + 0.08, 0.03, z + 0.08, 'a_brass',
                             bevel=0.008))
        o.append(box(name + 'latch', 0.45, -0.012, zt - 0.17, 0.55, 0.03, zt - 0.03, 'a_brass', bevel=0.01))
        o.append(box(name + 'key', 0.49, -0.016, zt - 0.13, 0.51, 0.0, zt - 0.08, 'a_dark'))
    else:
        # two pine crates side by side
        for i, (x0, x1) in enumerate(((0.012, 0.494), (0.506, 0.988))):
            o.append(box(name + 'ci%d' % i, x0, 0.03, 0.0, x1, 1.0, zt, 'a_crate_in'))
            for k in range(3):
                z0 = k * zt / 3 + 0.008
                z1 = (k + 1) * zt / 3 - 0.008
                o.append(box(name + 'cs%d%d' % (i, k), x0 + 0.02, 0.005, z0, x1 - 0.02, 0.04, z1,
                             'a_crate' if i == 0 else 'a_crate2', bevel=0.006))
            for xp in (x0, x1 - 0.055):
                o.append(box(name + 'cp%d%.2f' % (i, xp), xp, -0.008, 0.0, xp + 0.055, 0.05, zt, 'a_post',
                             bevel=0.006))
        # a rope handle on the left crate, a painted star on the right one
        o.append(torus(name + 'rope', (0.25, -0.004, zt * 0.62), 0.05, 0.011, 'a_leather', rot=(90, 0, 0),
                       scale=(1, 1, 0.7)))
        o.append(prism(name + 'star', L.star(0.075, 0.033), 0.0, 0.006, 'a_saddle',
                       xform=L.front_xform(0.75, 0.005, zt * 0.52)))
    return o


def cardboard(v, name='ab'):
    col, icon = A_TAPES[v]
    tk = 'a_tape_v%d' % v
    L.solid(tk, col, rough=0.3, spec=0.5, cc=0.35)
    W, D, Hb = 0.74, 0.68, 0.54
    x0, y0 = 0.5 - W / 2, 0.5 - D / 2
    o = [box(name + 'body', x0, y0, 0.0, x0 + W, y0 + D, Hb, 'a_card', bevel=0.035, seg=3)]
    # flap seam along Y on the top, under the tape
    o.append(box(name + 'seam', 0.495, y0 + 0.02, Hb - 0.004, 0.505, y0 + D - 0.02, Hb + 0.002, 'a_card_dk'))
    tw = 0.16
    o.append(box(name + 'tt', 0.5 - tw / 2, y0 + 0.01, Hb - 0.01, 0.5 + tw / 2, y0 + D - 0.01, Hb + 0.007, tk,
                 bevel=0.003))
    o.append(box(name + 'tf', 0.5 - tw / 2, y0 - 0.007, Hb - 0.20, 0.5 + tw / 2, y0 + 0.02, Hb - 0.02, tk,
                 bevel=0.003))
    # a label on the front with the icon; the same icon printed on the top
    shapes = {'heart': L.heart(0.15), 'star': L.star(0.085, 0.038),
              'fish': L.fish(0.22), 'paw': L.paw(0.20)}
    lx, lz = 0.5 + 0.19, 0.23
    o.append(prism(name + 'lab', L.roundrect(0.19, 0.17, 0.03), 0.0, 0.006, 'a_label',
                   xform=L.front_xform(lx, y0, lz)))
    sh = shapes[icon]
    polys = sh if isinstance(sh[0][0], (list, tuple)) else [sh]
    o.append(prism(name + 'ico', [L.transform2(p, 0.75, 0.75) for p in polys], 0.0, 0.011, tk,
                   xform=L.front_xform(lx, y0, lz)))
    o.append(prism(name + 'top_ico', [L.transform2(p, 1.05, 1.05) for p in polys], Hb - 0.002, Hb + 0.003, tk,
                   xform=L.top_xform(0.5 - 0.20, 0.5 + 0.08, 0.0)))
    # handle slots on the front (a dark rounded slot)
    o.append(prism(name + 'slot', L.roundrect(0.13, 0.04, 0.02), 0.0, 0.004, 'a_card_dk',
                   xform=L.front_xform(0.5 - 0.20, y0, Hb - 0.12)))
    return o


def tape_cross(name, z):
    """Two strips of red tape crossing, torn zig-zag ends."""
    o = []
    Lh, w = 0.47, 0.075
    pts = [(-Lh, -w), (Lh, -w), (Lh + 0.02, -w * 0.33), (Lh - 0.01, 0.0), (Lh + 0.02, w * 0.33), (Lh, w),
           (-Lh, w), (-Lh - 0.02, w * 0.33), (-Lh + 0.01, 0.0), (-Lh - 0.02, -w * 0.33)]
    for k, a in enumerate((45, -45)):
        o.append(prism(name + 'x%d' % k, L.transform2(pts, rot=a, dx=0.5, dy=0.5), z + 0.002 * k,
                       z + 0.006 + 0.002 * k, 'a_tape'))
    return o


def armchair_sheet(name='ach'):
    """An old armchair under a dust sheet: soft folds to the floor, little
    wooden feet peeking out."""
    parts = [box(name + 's', 0.07, 0.10, 0.05, 0.93, 0.86, 0.44, None),
             box(name + 'bk', 0.09, 0.60, 0.36, 0.91, 0.90, 0.90, None),
             box(name + 'al', 0.04, 0.12, 0.30, 0.24, 0.88, 0.62, None),
             box(name + 'ar', 0.76, 0.12, 0.30, 0.96, 0.88, 0.62, None),
             ellipsoid(name + 'cu', (0.5, 0.40, 0.46), (0.28, 0.24, 0.07), None),
             ellipsoid(name + 'tb', (0.5, 0.76, 0.88), (0.40, 0.14, 0.08), None)]
    for x in (0.10, 0.90):
        parts.append(ellipsoid(name + 'ah%.1f' % x, (x, 0.50, 0.60), (0.11, 0.38, 0.07), None))
    sk = L.skin(parts, name + 'skin', 'a_sheet', voxel=0.022, iters=10)
    # folds: a displace along the normal with a soft stretched noise
    tex = bpy.data.textures.new(name + 'tx', 'CLOUDS')
    tex.noise_scale = 0.12
    dm = sk.modifiers.new('folds', 'DISPLACE')
    dm.texture = tex
    dm.strength = 0.05
    dm.mid_level = 0.5
    o = [sk]
    for x in (0.12, 0.88):
        for y in (0.14, 0.82):
            o.append(cone(name + 'ft%.1f%.1f' % (x, y), (x, y, 0.0), 0.035, 0.045, 0.07, 'a_foot', verts=14))
    return o


def rag_rug(name, cx, cy, R=0.47):
    o = [cone(name + 'c', (cx, cy, 0.0), 0.09, 0.09, 0.012, 'a_rug1', verts=24)]
    r = 0.105
    k = 0
    while r < R:
        o.append(torus(name + 'r%d' % k, (cx, cy, 0.011), r, 0.024, 'a_rug%d' % (k % 3), scale=(1, 1, 0.45),
                       n=int(40 + r * 60), m=8))
        r += 0.047
        k += 1
    return o


def rocking_horse(name='arh'):
    o = rag_rug(name + 'rug', 0.5, 0.5)
    # rockers: two red arcs along X
    for y in (0.37, 0.63):
        pts = [(x, y, 0.035 + 0.55 * (x - 0.5) ** 2) for x in (0.06, 0.25, 0.5, 0.75, 0.94)]
        o.append(tube(name + 'rk%.2f' % y, pts, [0.03] * 5, 'a_rocker'))
    for x in (0.30, 0.70):
        for y in (0.39, 0.61):
            bx = 0.5 + (x - 0.5) * 0.85
            o.append(tube(name + 'lg%.1f%.1f' % (x, y), [(x, y, 0.06), ((x + bx) / 2, (y + 0.5) / 2, 0.24),
                                                          (bx, 0.5 + (y - 0.5) * 0.4, 0.42)],
                          [0.03, 0.028, 0.03], 'a_horse'))
    o.append(ellipsoid(name + 'body', (0.53, 0.5, 0.47), (0.25, 0.13, 0.12), 'a_horse'))
    o.append(ellipsoid(name + 'neck', (0.30, 0.5, 0.61), (0.08, 0.075, 0.15), 'a_horse', rot=(0, -28, 0)))
    o.append(ellipsoid(name + 'head', (0.20, 0.5, 0.74), (0.13, 0.075, 0.075), 'a_horse', rot=(0, 28, 0)))
    o.append(ellipsoid(name + 'snout', (0.105, 0.5, 0.685), (0.055, 0.062, 0.05), 'a_horse'))
    for s in (-1, 1):
        o.append(cone(name + 'ear%d' % s, (0.265, 0.5 + s * 0.035, 0.80), 0.028, 0.004, 0.08, 'a_horse',
                      rot=(s * -12, 20, 0), verts=12))
    o.append(ellipsoid(name + 'eye', (0.19, 0.43, 0.765), (0.018, 0.012, 0.022), 'a_eye', seg=12, rings=8))
    o.append(ellipsoid(name + 'nost', (0.07, 0.445, 0.69), (0.01, 0.008, 0.012), 'a_eye', seg=10, rings=6))
    # bridle and saddle
    o.append(torus(name + 'bri', (0.12, 0.5, 0.69), 0.066, 0.011, 'a_saddle', rot=(0, 80, 0),
                   scale=(1.0, 1.0, 1.0)))
    o.append(ellipsoid(name + 'sad', (0.55, 0.5, 0.575), (0.11, 0.14, 0.035), 'a_saddle'))
    o.append(ellipsoid(name + 'blk', (0.55, 0.5, 0.555), (0.13, 0.145, 0.02), 'a_rug1'))
    # mane and tail
    for k in range(5):
        t = k / 4.0
        p = (0.33 - 0.12 * t, 0.5, 0.70 + 0.10 * t)
        o.append(ellipsoid(name + 'mn%d' % k, (p[0] + 0.03, 0.5, p[2] - 0.0), (0.035, 0.03, 0.045), 'a_mane',
                           rot=(0, -30, 0), seg=14, rings=8))
    o.append(tube(name + 'tail', [(0.77, 0.5, 0.52), (0.84, 0.5, 0.50), (0.88, 0.5, 0.40), (0.86, 0.5, 0.30)],
                  [0.03, 0.04, 0.035, 0.02], 'a_mane'))
    # dapples on the near side
    rnd = random.Random(3)
    for k in range(5):
        o.append(ellipsoid(name + 'dp%d' % k, (rnd.uniform(0.38, 0.70), 0.378, rnd.uniform(0.42, 0.53)),
                           (0.028, 0.006, 0.022), 'a_dapple', seg=12, rings=6))
    return o


def _posts(world, name, y0, y1, pw, zt):
    """The two wall-height posts at the ends of a flap or gate cell."""
    o = []
    for (a, b) in ((-0.015, pw), (1 - pw, 1.015)):
        if world == 'attic':
            o += attic_top(name + 'p%.2f' % a, a, y0 - 0.015, b, y1 + 0.015, zt)
            o.append(box(name + 'pb%.2f' % a, max(a, 0.0), y0, 0.0, min(b, 1.0), y1, zt, 'a_trunk2', bevel=0.01))
            o.append(box(name + 'pc%.2f' % a, max(a, 0.0) + 0.03, y0 - 0.006, 0.05, min(b, 1.0) - 0.03, y0 + 0.01,
                         zt - 0.05, 'a_slat', bevel=0.005))
        else:
            o += roofs_post(name + 'p%.2f' % a, max(a, 0.0), min(b, 1.0), y0, y1)
    return o


def roofs_post(name, x0, x1, y0, y1):
    """A brick pier one floor high (the ends of a chimney-wall flap)."""
    cap = 0.06
    zc = H - cap
    o = [box(name + 'core', x0, y0 + 0.02, 0.0, x1, y1, zc, 'r_brick1')]
    ch = zc / 4.0
    for c in range(4):
        z0, z1 = c * ch + 0.008, (c + 1) * ch - 0.008
        if c % 2 == 0:
            spans = [(x0 + 0.008, x1 - 0.008)]
        else:
            m = (x0 + x1) / 2
            spans = [(x0 + 0.008, m - 0.008), (m + 0.008, x1 - 0.008)]
        for j, (a, b) in enumerate(spans):
            o.append(box(name + 'b%d%d' % (c, j), a, y0, z0, b, y0 + 0.03, z1, 'r_brick%d' % ((c + j) % 3),
                         bevel=0.006))
    o.append(box(name + 'bed', x0, y0 - 0.015, zc - 0.005, x1, y1 + 0.015, H - 0.012, 'r_mortar_dk'))
    o.append(box(name + 'cap', x0 + 0.006, y0 - 0.015, zc, x1 - 0.006, y1 + 0.015, H, 'r_cap0', bevel=0.008))
    return o


def attic_flap(orient, ang, name='afl', world='attic'):
    """A wall cell with a cat flap: two posts (wall height, the dark lid on
    top) and between them a lower pale board with the flap in it.
    Built for 'v' (the wall runs left-right, Mila crosses up/down); 'h' is
    the same turned 90 degrees."""
    o = []
    y0, y1 = 0.30, 0.70        # the partition's thickness
    pw = 0.22                  # post width
    zt = H - 0.05
    P = 'a_' if world == 'attic' else 'r_'
    o += _posts(world, name, y0, y1, pw, zt)
    # the board with an opening (ox0..ox1, oz0..oz1)
    bx0, bx1, bz = pw, 1 - pw, 0.40
    ox0, ox1, oz0, oz1 = 0.355, 0.645, 0.035, 0.305
    for (a, b, c, d) in ((bx0, ox0, 0.0, bz), (ox1, bx1, 0.0, bz), (ox0, ox1, oz1, bz), (ox0, ox1, 0.0, oz0)):
        o.append(box(name + 'bd%.2f%.2f' % (a, c), a, y0 + 0.04, c, b, y1 - 0.04, d, P + 'board_door'))
    o.append(box(name + 'bdt', bx0 - 0.005, y0 + 0.03, bz, bx1 + 0.005, y1 - 0.03, bz + 0.035, P + 'flapframe', bevel=0.01))
    # the flap's frame on both faces (a mint ring) with a little hood on top
    fr_out = L.roundrect(ox1 - ox0 + 0.09, oz1 - oz0 + 0.08, 0.05)
    fr_in = L.roundrect(ox1 - ox0 - 0.005, oz1 - oz0 - 0.005, 0.03)
    cx, cz = (ox0 + ox1) / 2, (oz0 + oz1) / 2
    for side, yface, sgn in (('f', y0 + 0.04, 1), ('b', y1 - 0.04, -1)):
        if sgn > 0:
            xf = L.front_xform(cx, yface, cz)
        else:
            xf = mathutils.Matrix.Translation((cx, yface, cz)) @ mathutils.Matrix.Rotation(math.radians(-90), 4, 'X')
        o.append(_ring_prism(name + 'fr' + side, fr_out, fr_in, 0.0, 0.035, P + 'flapframe', xf))
        ya, yb = (yface - 0.05, yface) if sgn > 0 else (yface, yface + 0.05)
        o.append(box(name + 'hd' + side, ox0 - 0.03, ya, oz1 + 0.035, ox1 + 0.03, yb, oz1 + 0.075, P + 'flapframe',
                     bevel=0.012))
    # the flap: hinged at the top edge of the opening, in the board's middle
    fl = prism(name + 'flap', L.roundrect(ox1 - ox0 - 0.02, oz1 - oz0 - 0.015, 0.025), -0.012, 0.012, P + 'flap',
               xform=mathutils.Matrix.Translation((0, 0, -(oz1 - oz0) / 2 + 0.0)) @
               mathutils.Matrix.Rotation(math.radians(90), 4, 'X'), bevel=0.004)
    pawp = prism(name + 'fpaw', L.paw(0.14), 0.0, 0.004, P + 'flappaw',
                 xform=mathutils.Matrix.Translation((0, -0.013, -(oz1 - oz0) / 2 - 0.01)) @
                 mathutils.Matrix.Rotation(math.radians(90), 4, 'X'))
    pawb = prism(name + 'fpawb', L.paw(0.14), 0.0, 0.004, P + 'flappaw',
                 xform=mathutils.Matrix.Translation((0, 0.017, -(oz1 - oz0) / 2 - 0.01)) @
                 mathutils.Matrix.Rotation(math.radians(90), 4, 'X'))
    hinge = bpy.data.objects.new(name + 'hinge', None)
    C.link(hinge)
    hinge.location = (cx, (y0 + y1) / 2, oz1 - 0.005)
    for p in (fl, pawp, pawb):
        p.parent = hinge
    hinge.rotation_euler = (math.radians(ang), 0, 0)
    o += [fl, pawp, pawb]
    if orient == 'h':
        L.rot_about([ob for ob in o if ob.parent is None] + [hinge], 0.5, 0.5, 90)
    bpy.context.view_layer.update()
    return o


def _ring_prism(name, outer, inner, zb, zt, key, xform):
    """A flat ring between two closed polylines with the same point count."""
    n = len(outer)
    assert len(inner) == n
    verts, faces = [], []
    for (x, y) in outer:
        verts += [(x, y, zb), (x, y, zt)]
    for (x, y) in inner:
        verts += [(x, y, zb), (x, y, zt)]
    for i in range(n):
        j = (i + 1) % n
        ob_, ot = 2 * i, 2 * i + 1
        jb, jt = 2 * j, 2 * j + 1
        ib_, it = 2 * (n + i), 2 * (n + i) + 1
        kb, kt = 2 * (n + j), 2 * (n + j) + 1
        faces.append((ot, jt, kt, it))        # top
        faces.append((ob_, ib_, kb, jb))      # bottom
        faces.append((ob_, jb, jt, ot))       # outer side
        faces.append((ib_, it, kt, kb))       # inner side
    verts = [tuple(xform @ mathutils.Vector(v)) for v in verts]
    ob = C.mesh_object(name, verts, faces, key)
    for p in ob.data.polygons:
        p.use_smooth = True
    ob.data.use_auto_smooth = True
    return ob


# ===========================================================================
# ROOFS
# ===========================================================================

R_TILE = (1.00, 0.50, 0.30)


def roofs_mats():
    for k in range(6):
        rnd = random.Random(70 + k)
        s = rnd.uniform(0.9, 1.06) if k else 1.0
        col = (min(1, R_TILE[0] * s), R_TILE[1] * s * rnd.uniform(0.95, 1.04), R_TILE[2] * s * rnd.uniform(0.92, 1.06))
        L.mottled('r_tile%d' % k, col, var=0.06, scale=11, rough=0.42, spec=0.55, cc=0.35,
                  offset=(rnd.uniform(0, 9), rnd.uniform(0, 9), 0))
    L.solid('r_under', (0.40, 0.17, 0.13), rough=0.9, spec=0.2)
    L.solid('r_mortar', (0.66, 0.64, 0.66), rough=0.95, spec=0.2)
    L.solid('r_mortar_dk', (0.24, 0.19, 0.20), rough=0.95, spec=0.2)
    for k, col in enumerate(((0.80, 0.36, 0.27), (0.72, 0.30, 0.23), (0.86, 0.44, 0.31))):
        L.mottled('r_brick%d' % k, col, var=0.08, scale=18, rough=0.8, spec=0.3, offset=(k * 3, k, 0))
    for k, col in enumerate(((0.34, 0.15, 0.13), (0.29, 0.13, 0.11), (0.38, 0.18, 0.14))):
        L.mottled('r_cap%d' % k, col, var=0.08, scale=14, rough=0.75, spec=0.35, offset=(k * 3, k, 0))
    L.solid('r_frame', (0.95, 0.93, 0.86), rough=0.5, cc=0.2)
    L.solid('r_window', (1.0, 0.78, 0.40), rough=0.3, emit=(1.0, 0.70, 0.30), es=1.3)
    L.solid('r_curtain', (0.95, 0.50, 0.58), rough=0.9, sheen=1.0, emit=(0.9, 0.35, 0.3), es=0.6)
    L.solid('r_moon', (1.0, 0.92, 0.60), rough=0.4, emit=(1.0, 0.86, 0.45), es=1.4)
    L.solid('r_hole', (0.035, 0.04, 0.075), rough=1.0, spec=0.1)
    L.wood('r_beam', (0.30, 0.20, 0.16), scale=2, dark=0.8)
    L.wood('r_beam_dk', (0.13, 0.10, 0.11), scale=2, dark=0.8)
    L.wood('r_rim', (0.70, 0.55, 0.44), scale=2, dark=0.85)
    L.wood('r_pine', (0.92, 0.76, 0.52), scale=2, dark=0.85, lines=0.1)
    L.solid('r_crate_in', (0.25, 0.17, 0.12), rough=0.9)
    L.solid('r_water', (0.30, 0.42, 0.70), rough=0.04, spec=1.0, cc=1.0, alpha=0.62)
    L.solid('r_water_hi', (0.85, 0.92, 1.0), rough=0.1, emit=(0.70, 0.80, 1.0), es=1.2, alpha=0.9)
    L.solid('r_water_moon', (1.0, 0.95, 0.75), rough=0.1, emit=(1.0, 0.92, 0.65), es=1.6, alpha=0.85)
    L.solid('r_skyframe', (0.92, 0.92, 0.96), rough=0.45, cc=0.3)
    L.solid('r_skyglass', (1.0, 0.80, 0.45), rough=0.08, spec=0.9, cc=1.0, emit=(1.0, 0.72, 0.32), es=0.8)
    L.wood('r_dove_base', (0.62, 0.44, 0.30), scale=2)
    L.solid('r_dove', (1.0, 0.90, 0.66), rough=0.5, cc=0.2)
    L.solid('r_dove_trim', (0.98, 0.97, 0.94), rough=0.5)
    L.wood('r_dove_door', (0.62, 0.36, 0.22), scale=2)
    L.mottled('r_dove_roof', (0.92, 0.36, 0.30), var=0.05, scale=10, rough=0.5, cc=0.3)
    L.solid('r_dove_hole', (0.05, 0.05, 0.08), rough=1.0)
    L.solid('r_pigeon', (0.72, 0.72, 0.84), rough=0.7, sheen=0.8)
    L.solid('r_pigeon_neck', (0.40, 0.66, 0.64), rough=0.3, cc=0.6)
    L.solid('r_beak', (1.0, 0.62, 0.25), rough=0.5)
    L.solid('r_dark', (0.04, 0.03, 0.04), rough=0.4)
    L.mottled('r_board_door', (0.80, 0.40, 0.29), var=0.08, scale=18, rough=0.8, spec=0.3)
    L.solid('r_flapframe', (0.20, 0.80, 0.70), rough=0.3, cc=0.4, spec=0.5)
    L.solid('r_flap', (0.72, 0.95, 0.88), rough=0.3, cc=0.5, spec=0.5)
    L.solid('r_flappaw', (1.0, 1.0, 1.0), rough=0.5)
    L.solid('r_metal', (0.78, 0.80, 0.86), rough=0.3, metal=0.8)
    L.solid('r_concrete', (0.62, 0.62, 0.68), rough=0.9, spec=0.2)
    L.solid('r_beacon', (1.0, 0.25, 0.2), emit=(1.0, 0.2, 0.15), es=3.0)
    L.solid('r_tank', (0.52, 0.80, 0.82), rough=0.45, metal=0.3, cc=0.3)
    L.solid('r_tank_band', (0.36, 0.56, 0.62), rough=0.4, metal=0.5)
    L.solid('r_dish', (0.96, 0.96, 0.98), rough=0.4, cc=0.3)
    L.mottled('r_pot', (0.90, 0.46, 0.30), var=0.06, scale=12, rough=0.5, cc=0.3)
    L.solid('r_ball_dot', (1.0, 1.0, 1.0), rough=0.3, cc=0.5)


R_CRATES = [((0.96, 0.26, 0.24), 'fish'), ((0.10, 0.74, 0.68), 'star'),
            ((1.00, 0.76, 0.10), 'heart'), ((0.62, 0.42, 0.98), 'paw')]
R_BALLS = [((1.0, 0.10, 0.12), (1.0, 0.84, 0.10)), ((0.18, 0.46, 1.0), (0.98, 0.98, 0.96))]

TILE_A = 0.15       # depth of a tile's rounded front edge
TILE_W = 0.5


def _tile_poly(xa, xb, xc, y0, y1, n=14):
    """A beaver-tail tile centred at xc, TILE_W wide (minus a gap), clipped to
    [xa, xb]; its rounded front edge starts at y0, straight back edge at y1."""
    hw = TILE_W / 2 - 0.012
    lo, hi = max(xa, xc - hw), min(xb, xc + hw)
    pts = []
    for k in range(n + 1):
        x = lo + (hi - lo) * k / n
        u = max(-1.0, min(1.0, (x - xc) / hw))
        y = y0 + TILE_A * (1 - math.sqrt(max(0.0, 1 - u * u)))
        pts.append((x, y))
    pts += [(hi, y1), (lo, y1)]
    return pts


def roofs_floor(v, name='rf'):
    """Terracotta beaver-tail tiles, 2 x 2 a cell, the back row staggered
    (its half tiles meet the neighbour's halves); moonlight on the glaze."""
    rnd = random.Random(2000 + v)
    o = [box(name + 'u', -0.02, -0.02, -0.07, 1.02, 1.02, 0.0, 'r_under')]
    zf, zb_ = 0.030, 0.006

    def ztop(y0, y1):
        return lambda x, y: zf - (zf - zb_) * (y - y0) / (y1 - y0)
    rows = [(0.0, 0.5, (0.25, 0.75)), (0.5, 1.0, (0.0, 0.5, 1.0))]
    for r, (y0, y1, xcs) in enumerate(rows):
        for xc in xcs:
            half = xc in (0.0, 1.0)
            key = 'r_tile0' if half else 'r_tile%d' % rnd.randrange(1, 6)
            pts = _tile_poly(0.0, 1.0, xc, y0, y1)
            cut = []
            if xc == 0.0:
                cut = [('x', 0.0)]
            elif xc == 1.0:
                cut = [('x', 1.0)]
            o.append(prism(name + 't%d%.2f' % (r, xc), pts, -0.01, ztop(y0, y1), key, bevel=0.006, cut=cut))
    return o


def roofs_wall(v, name='rw'):
    """A brick parapet one floor high: a dark brick coping on top, courses of
    bricks on the front; v1 has a little lit window."""
    o = []
    cap = 0.06
    zc = H - cap
    o.append(box(name + 'core', 0.0, 0.02, 0.0, 1.0, 1.0, zc, 'r_mortar'))
    rnd = random.Random(3000 + v)
    ch = zc / 4.0
    for c in range(4):
        z0, z1 = c * ch + 0.008, (c + 1) * ch - 0.008
        xs = [0.0, 0.25, 0.5, 0.75, 1.0] if c % 2 == 0 else [0.0, 0.125, 0.375, 0.625, 0.875, 1.0]
        for j in range(len(xs) - 1):
            a, b = xs[j], xs[j + 1]
            cut = []
            aa, bb = a + 0.008, b - 0.008
            if a == 0.0 and c % 2 == 1:
                aa, cut = 0.0, [('x', 0.0)]
            if b == 1.0 and c % 2 == 1:
                bb, cut = 1.0, [('x', 1.0)]
            key = 'r_brick%d' % (0 if cut else rnd.randrange(3))
            o.append(cbox(name + 'b%d%d' % (c, j), aa, 0.0, z0, bb, 0.03, z1, key, 0.006, cut))
    # coping: a dark mortar bed and bricks laid flat in rows along X; the odd
    # rows are staggered and their half bricks meet the neighbour's halves
    o.append(box(name + 'bed', 0.0, -0.015, zc - 0.005, 1.0, 1.0, H - 0.012, 'r_mortar_dk'))
    for r in range(4):
        y0 = r / 4.0 + 0.007 if r else -0.015
        y1 = (r + 1) / 4.0 - 0.007
        if r % 2 == 0:
            spans = [(0.008, 0.492, ()), (0.508, 0.992, ())]
        else:
            spans = [(0.0, 0.242, (('x', 0.0),)), (0.258, 0.742, ()), (0.758, 1.0, (('x', 1.0),))]
        for j, (a_, b_, cut) in enumerate(spans):
            key = 'r_cap%d' % (0 if cut else rnd.randrange(3))
            o.append(cbox(name + 'c%d%d' % (r, j), a_, y0, zc, b_, y1, H, key, 0.008, cut))
    if v == 1:
        o += _window(name + 'win', 0.5, 0.075)
    return o


def _arch(w, hs, n=12):
    """An arched window outline: a w wide rectangle hs high with a half circle on top."""
    r = w / 2
    pts = [(-r, 0.0), (r, 0.0)]
    for k in range(n + 1):
        a = math.pi * k / n
        pts.append((r * math.cos(a), hs + r * math.sin(a)))
    return pts


def _window(name, cx, z0):
    o = []
    o.append(prism(name + 'fr', _arch(0.34, 0.16), -0.012, 0.035, 'r_frame', xform=L.front_xform(cx, 0.03, z0 - 0.02),
                   bevel=0.006))
    o.append(prism(name + 'gl', _arch(0.26, 0.16), 0.0, 0.04, 'r_window', xform=L.front_xform(cx, 0.03, z0 + 0.0)))
    # curtains, the cross bars, the sill
    for s in (-1, 1):
        o.append(prism(name + 'cu%d' % s, L.ellipse(0.035, 0.10, 20, s * 0.11, 0.24), 0.0, 0.046, 'r_curtain',
                       xform=L.front_xform(cx, 0.03, z0)))
    o.append(box(name + 'mv', cx - 0.012, -0.022, z0, cx + 0.012, 0.02, z0 + 0.29, 'r_frame', bevel=0.004))
    o.append(box(name + 'mh', cx - 0.13, -0.022, z0 + 0.15, cx + 0.13, 0.02, z0 + 0.174, 'r_frame', bevel=0.004))
    o.append(box(name + 'sill', cx - 0.21, -0.05, z0 - 0.045, cx + 0.21, 0.03, z0 - 0.01, 'r_frame', bevel=0.008))
    C.add_point_light((cx, -0.16, z0 + 0.14), color=(1.0, 0.70, 0.35), power=1.2, radius=0.05, name=name + 'lt')
    return o


def crate(v, name='rc', top_only=False, W=0.72, Hc=0.56, z_top=None):
    """A pine crate: pale slats, a painted frame, a stencil on the front and
    on the top. top_only: just the top (a crate sunk in a hole)."""
    col, icon = R_CRATES[v]
    pk = 'r_paint_v%d' % v
    L.solid(pk, col, rough=0.45, spec=0.4, cc=0.2)
    zt = Hc if z_top is None else z_top
    x0, y0 = 0.5 - W / 2, 0.5 - W / 2
    x1, y1 = x0 + W, y0 + W
    o = []
    fb = 0.075               # frame board width
    zb = zt - 0.035
    if not top_only:
        o.append(box(name + 'in', x0 + 0.01, y0 + 0.01, 0.0, x1 - 0.01, y1 - 0.01, zt - 0.01, 'r_crate_in'))
        # front slats
        for k in range(3):
            z0 = fb + k * (zt - 2 * fb) / 3 + 0.006
            z1 = fb + (k + 1) * (zt - 2 * fb) / 3 - 0.006
            o.append(box(name + 'fs%d' % k, x0 + fb, y0, z0, x1 - fb, y0 + 0.03, z1, 'r_pine', bevel=0.005))
        # front frame
        o.append(box(name + 'fl', x0, y0 - 0.012, 0.0, x0 + fb, y0 + 0.03, zb, pk, bevel=0.01))
        o.append(box(name + 'fr', x1 - fb, y0 - 0.012, 0.0, x1, y0 + 0.03, zb, pk, bevel=0.01))
        o.append(box(name + 'sd', x0, y0 + 0.02, 0.0, x1, y1, zb, 'r_crate_in'))
        o.append(box(name + 'fb', x0 + fb - 0.01, y0 - 0.008, 0.0, x1 - fb + 0.01, y0 + 0.03, fb, pk, bevel=0.01))
        o.append(box(name + 'ft', x0 + fb - 0.01, y0 - 0.008, zt - fb, x1 - fb + 0.01, y0 + 0.03, zt, pk, bevel=0.01))
    # top: slats along X inside a painted border
    zb = zt - 0.035
    for k in range(3):
        a = y0 + fb + k * (W - 2 * fb) / 3 + 0.006
        b = y0 + fb + (k + 1) * (W - 2 * fb) / 3 - 0.006
        o.append(box(name + 'ts%d' % k, x0 + fb - 0.01, a, zb, x1 - fb + 0.01, b, zt - 0.006, 'r_pine', bevel=0.005))
    if top_only:
        o.append(box(name + 'tin', x0 + 0.01, y0 + 0.01, zb - 0.05, x1 - 0.01, y1 - 0.01, zb + 0.004, 'r_crate_in'))
    o.append(box(name + 'bl', x0, y0, zb, x0 + fb, y1, zt, pk, bevel=0.01))
    o.append(box(name + 'br', x1 - fb, y0, zb, x1, y1, zt, pk, bevel=0.01))
    o.append(box(name + 'bf', x0 + fb - 0.01, y0, zb, x1 - fb + 0.01, y0 + fb, zt, pk, bevel=0.01))
    o.append(box(name + 'bb', x0 + fb - 0.01, y1 - fb, zb, x1 - fb + 0.01, y1, zt, pk, bevel=0.01))
    shapes = {'heart': L.heart(0.17), 'star': L.star(0.11, 0.048),
              'fish': L.fish(0.26), 'paw': L.paw(0.22)}
    sh = shapes[icon]
    polys = sh if isinstance(sh[0][0], (list, tuple)) else [sh]
    o.append(prism(name + 'tst', polys, zt - 0.007, zt - 0.003, pk, xform=L.top_xform(0.5, 0.5, 0.0)))
    if not top_only:
        o.append(prism(name + 'fst', [L.transform2(p, 0.85, 0.85) for p in polys], 0.0, 0.004, pk,
                       xform=L.front_xform(0.5, y0 - 0.0005, zt / 2)))
    return o


def ball(v, name='rb', rot=(62, -20, 0)):
    a, b = R_BALLS[v]
    key = 'r_ball_v%d' % v
    L.banded(key, a, b, axis=2, half=0.3, rough=0.25, spec=0.6, cc=0.6)
    r = 0.25
    ob = ellipsoid(name, (0.5, 0.5, r), (r, r, r), key, seg=40, rings=24)
    # the stripe crosses the ball diagonally; one of its pole dots faces the camera
    ax_ = mathutils.Vector((0.63, 0.18, 0.76)).normalized()
    ob.rotation_euler = ax_.to_track_quat('Z', 'Y').to_euler()
    o = [ob]
    for sz in (1, -1):         # white dots at the stripe's poles: they show the roll
        d = ellipsoid(name + 'dot%d' % sz, (0, 0, 0.985 * sz), (0.26, 0.26, 0.05), 'r_ball_dot', seg=20, rings=8)
        d.parent = ob
        o.append(d)
    bpy.context.view_layer.update()
    return o


def roofs_hole(name='rh', with_crate=None):
    """A gap in the roof: tile-coloured rims, the cut of the roof (tiles,
    batten) on the far side, darkness one floor down. The near half of the
    pit is not modelled: the cell in front hides it anyway."""
    o = []
    rim = 0.07
    inner0, inner1 = rim, 1 - rim
    zr = 0.02
    # rims: flat frame around the opening, the tiles' cut edge
    o.append(cbox(name + 'rf', 0.0, 0.0, -0.06, 1.0, inner0, zr, 'r_tile0', 0.008, [('x', 0.0), ('x', 1.0)]))
    o.append(cbox(name + 'rb', 0.0, inner1, -0.06, 1.0, 1.0, zr, 'r_tile0', 0.008, [('x', 0.0), ('x', 1.0)]))
    o.append(cbox(name + 'rl', 0.0, inner0, -0.06, inner0, inner1, zr, 'r_tile0', 0.008, [('x', 0.0)]))
    o.append(cbox(name + 'rr', inner1, inner0, -0.06, 1.0, inner1, zr, 'r_tile0', 0.008, [('x', 1.0)]))
    # the cut of the roof on the far side: a batten and a beam end
    o.append(box(name + 'bat', inner0, inner1 - 0.03, -0.12, inner1, inner1, -0.06, 'r_beam'))
    o.append(box(name + 'wall', inner0, inner1 - 0.005, -H, inner1, inner1, -0.12, 'r_beam_dk'))
    # the floor below (dark); only the part you can see through the opening
    yv = inner0 + (zr + H) * (0.6614378 / 0.75) - 0.02
    o.append(box(name + 'bot', inner0, yv, -H - 0.02, inner1, inner1, -H, 'r_hole'))
    # a rafter crossing below, catching a little moonlight
    o.append(box(name + 'raf', inner0, yv + 0.06, -H + 0.0, inner1, yv + 0.16, -H + 0.06, 'r_beam_dk', bevel=0.01))
    if with_crate is not None:
        o += crate(with_crate, name + 'cr', top_only=True, W=0.84, z_top=0.0)
    return o


def puddle(name='rp'):
    """A rain puddle over the tiles: a clear blue sheet with the moon in it."""
    o = roofs_floor(0, name + 'fl')
    rnd = random.Random(11)
    pts = []
    n = 40
    for k in range(n):
        a = 2 * math.pi * k / n
        r = 0.40 + 0.035 * math.sin(3 * a + 0.5) + 0.025 * math.sin(5 * a + 1.3)
        pts.append((0.5 + r * math.cos(a) * 1.05, 0.5 + r * math.sin(a) * 0.95))
    o.append(prism(name + 'w', pts, 0.0, 0.036, 'r_water', smooth=True))
    # the moon's reflection and two glints
    o.append(prism(name + 'm', L.ellipse(0.07, 0.07, 24, 0.62, 0.62), 0.036, 0.038, 'r_water_moon'))
    for (cx, cy, rx, rot) in ((0.32, 0.40, 0.10, 20), (0.44, 0.30, 0.05, 20)):
        o.append(prism(name + 'g%.2f' % cx, L.ellipse(rx, 0.012, 16, cx, cy, rot), 0.036, 0.038, 'r_water_hi'))
    return o


def moon_mark(name='rm', z=0.036):
    """The target: a glowing painted ring with a fat crescent moon and two
    little stars."""
    o = [annulus(name + 'ring', 0.5, 0.5, 0.35, 0.43, z, z + 0.004, 'r_moon', n=56)]
    o.append(prism(name + 'moon', L.transform2(L.crescent(0.25, 0.21, 0.13, rot=35), dx=0.47, dy=0.47), z,
                   z + 0.004, 'r_moon'))
    for (cx, cy, s) in ((0.66, 0.36, 0.07), (0.30, 0.66, 0.05)):
        o.append(prism(name + 'st%.2f' % cx, L.star(s, s * 0.45, cx=cx, cy=cy), z, z + 0.004, 'r_moon'))
    C.add_point_light((0.5, 0.5, 0.30), color=(1.0, 0.88, 0.55), power=1.0, radius=0.2, name=name + 'lt')
    return o


def skylight(name='rsk'):
    """A raised skylight: a white curb, a sloping glass lid lit warm from
    inside, the frame's cross bars."""
    o = [box(name + 'curb', 0.06, 0.06, 0.0, 0.94, 0.94, 0.18, 'r_skyframe', bevel=0.02)]
    z_f, z_b = 0.20, 0.36

    def zt(x, y):
        return z_f + (z_b - z_f) * (y - 0.08) / 0.84
    o.append(prism(name + 'lid', [(0.08, 0.08), (0.92, 0.08), (0.92, 0.92), (0.08, 0.92)], 0.17,
                   lambda x, y: zt(x, y) - 0.03, 'r_skyframe', bevel=0.012))
    o.append(prism(name + 'gl', [(0.14, 0.14), (0.86, 0.14), (0.86, 0.86), (0.14, 0.86)], 0.17,
                   lambda x, y: zt(x, y) - 0.012, 'r_skyglass'))
    # frame bars on the glass
    for (a, b, c, d) in ((0.08, 0.08, 0.92, 0.15), (0.08, 0.85, 0.92, 0.92), (0.08, 0.08, 0.15, 0.92),
                         (0.85, 0.08, 0.92, 0.92), (0.475, 0.08, 0.525, 0.92), (0.08, 0.475, 0.92, 0.525)):
        o.append(prism(name + 'fb%.2f%.2f' % (a, b), [(a, b), (c, b), (c, d), (a, d)], 0.17,
                       lambda x, y: zt(x, y) + 0.0, 'r_skyframe', bevel=0.006))
    C.add_point_light((0.5, 0.5, 0.52), color=(1.0, 0.72, 0.36), power=1.5, radius=0.3, name=name + 'lt')
    return o


def pigeon_house(name='rdv'):
    """A little dovecote on a wooden plinth, its gable to the camera with two
    round doors and a perch, a pigeon dozing on the plinth."""
    o = [box(name + 'pl', 0.05, 0.08, 0.0, 0.95, 0.92, 0.14, 'r_dove_base', bevel=0.02)]
    o.append(cone(name + 'post', (0.5, 0.6, 0.14), 0.05, 0.05, 0.30, 'r_dove_trim', verts=16))
    hx0, hx1, hy0, hy1, hz0, hz1 = 0.24, 0.76, 0.44, 0.84, 0.42, 0.70
    o.append(box(name + 'fl', hx0 - 0.05, hy0 - 0.07, hz0 - 0.03, hx1 + 0.05, hy1 + 0.02, hz0 + 0.004,
                 'r_dove_trim', bevel=0.01))
    xm = (hx0 + hx1) / 2
    ridge = hz1 + 0.22
    # the gable: a pentagon wall (front and back), extruded along Y
    pent = [(hx0, hz0 + 0.01), (hx1, hz0 + 0.01), (hx1, hz1), (xm, ridge - 0.02), (hx0, hz1)]
    M = mathutils.Matrix(((1, 0, 0, 0), (0, 0, -1, hy0), (0, 1, 0, 0), (0, 0, 0, 1)))
    o.append(prism(name + 'gab', pent, -(hy1 - hy0), 0.0, 'r_dove', xform=M, bevel=0.008))
    # roof slopes (left and right), ridge along Y, overhanging the gable
    for s_ in (-1, 1):
        ex = xm + s_ * (hx1 - hx0) / 2 + s_ * 0.07
        ez = hz1 - 0.05
        poly = [(xm, ridge), (ex, ez), (ex, ez + 0.035), (xm, ridge + 0.035)]
        if s_ < 0:
            poly = list(reversed(poly))
        o.append(prism(name + 'rf%d' % s_, poly, -(hy1 - hy0) - 0.05, 0.025, 'r_dove_roof', xform=M, bevel=0.01))
    # two round doors with white rims, a perch under them
    # an arched door with a wooden surround, a little round window above
    o.append(prism(name + 'drf', _arch(0.17, 0.07), 0.0, 0.012, 'r_dove_door',
                   xform=L.front_xform(xm, hy0, hz0 + 0.045), bevel=0.004))
    o.append(prism(name + 'door', _arch(0.115, 0.06), 0.0, 0.016, 'r_dove_hole',
                   xform=L.front_xform(xm, hy0, hz0 + 0.06)))
    o.append(annulus(name + 'wr', 0, 0, 0.032, 0.052, 0.0, 0.012, 'r_dove_door', n=24))
    o[-1].rotation_euler = (math.radians(90), 0, 0)
    o[-1].location = (xm, hy0, ridge - 0.10)
    o.append(prism(name + 'win', L.circle(0.033, 20), 0.0, 0.008, 'r_window',
                   xform=L.front_xform(xm, hy0, ridge - 0.10)))
    o.append(tube(name + 'perch', [(xm - 0.11, hy0 - 0.06, hz0 + 0.055), (xm, hy0 - 0.06, hz0 + 0.055),
                                   (xm + 0.11, hy0 - 0.06, hz0 + 0.055)], [0.013] * 3, 'r_dove_trim'))
    # the pigeon, dozing on the plinth, front left
    px, py = 0.27, 0.22
    o.append(ellipsoid(name + 'pb', (px, py, 0.235), (0.12, 0.095, 0.095), 'r_pigeon', rot=(0, 10, 0)))
    o.append(ellipsoid(name + 'pn', (px + 0.075, py - 0.015, 0.31), (0.062, 0.062, 0.062), 'r_pigeon_neck'))
    o.append(ellipsoid(name + 'ph', (px + 0.095, py - 0.025, 0.37), (0.056, 0.053, 0.053), 'r_pigeon'))
    o.append(cone(name + 'pbk', (px + 0.15, py - 0.035, 0.362), 0.014, 0.0, 0.04, 'r_beak', rot=(0, 90, 0),
                  verts=10))
    o.append(ellipsoid(name + 'pw', (px - 0.03, py - 0.045, 0.25), (0.085, 0.05, 0.055), 'r_pigeon_neck',
                       rot=(0, 15, 0)))
    o.append(tube(name + 'pe', [(px + 0.08, py - 0.075, 0.385), (px + 0.10, py - 0.079, 0.379),
                                (px + 0.12, py - 0.075, 0.385)], [0.0045] * 3, 'r_dark'))
    return o



# ---------------------------------------------------------------------------
# attic: more props, the 2-cell trunk, the plate and the gate
# ---------------------------------------------------------------------------

def attic_lamp(name='alp'):
    """A little bedside cabinet with a fringed pink lamp (it glows) and books."""
    o = [box(name + 'cab', 0.10, 0.16, 0.0, 0.90, 0.86, 0.40, 'a_post', bevel=0.02),
         box(name + 'top', 0.07, 0.13, 0.40, 0.93, 0.89, 0.45, 'a_top', bevel=0.012),
         box(name + 'drw', 0.17, 0.145, 0.19, 0.83, 0.17, 0.35, 'a_slat', bevel=0.01),
         ellipsoid(name + 'knob', (0.5, 0.135, 0.27), (0.028, 0.02, 0.028), 'a_brass', seg=14, rings=8)]
    for x in (0.13, 0.84):
        o.append(box(name + 'ft%.2f' % x, x, 0.18, 0.0, x + 0.03, 0.21, 0.04, 'a_foot'))
    lx, ly = 0.62, 0.56
    o.append(cone(name + 'lb', (lx, ly, 0.45), 0.10, 0.06, 0.05, 'a_brass'))
    o.append(cone(name + 'ls', (lx, ly, 0.50), 0.022, 0.022, 0.18, 'a_brass', verts=12))
    o.append(ellipsoid(name + 'bulb', (lx, ly, 0.70), (0.04, 0.04, 0.05), 'a_bulb', seg=12, rings=8))
    o.append(cone(name + 'sh', (lx, ly, 0.64), 0.21, 0.12, 0.22, 'a_shade', verts=36))
    o.append(torus(name + 'fr', (lx, ly, 0.645), 0.205, 0.018, 'a_brass', n=36, m=8))
    for k in range(14):                      # the fringe: little bobbles
        a = 2 * math.pi * k / 14
        o.append(ellipsoid(name + 'fb%d' % k, (lx + 0.21 * math.cos(a), ly + 0.21 * math.sin(a), 0.615),
                           (0.018, 0.018, 0.026), 'a_shade', seg=8, rings=6))
    # two books lying on the cabinet, left
    o.append(box(name + 'b1', 0.14, 0.28, 0.45, 0.42, 0.66, 0.50, 'a_bk1', bevel=0.006))
    o.append(box(name + 'p1', 0.15, 0.275, 0.455, 0.41, 0.64, 0.495, 'a_pages'))
    o.append(box(name + 'b2', 0.17, 0.31, 0.50, 0.40, 0.62, 0.545, 'a_bk0', bevel=0.006))
    o.append(box(name + 'p2', 0.18, 0.305, 0.505, 0.39, 0.60, 0.54, 'a_pages'))
    C.add_point_light((lx, ly, 0.72), color=(1.0, 0.72, 0.45), power=4.0, radius=0.05, name=name + 'lt')
    return o


def dress_form(name='adf'):
    """A dressmaker's form on a round wooden stand, a yellow tape measure
    round its neck, a pincushion at its foot."""
    o = [cone(name + 'base', (0.5, 0.5, 0.0), 0.42, 0.40, 0.05, 'a_post', verts=48),
         torus(name + 'brim', (0.5, 0.5, 0.05), 0.40, 0.014, 'a_top', n=48, m=8)]
    for k in range(3):
        a_ = math.radians(90 + 120 * k)
        fx, fy = 0.5 + 0.22 * math.cos(a_), 0.5 + 0.22 * math.sin(a_)
        o.append(tube(name + 'leg%d' % k, [(fx, fy, 0.05), ((fx + 0.5) / 2, (fy + 0.5) / 2, 0.16), (0.5, 0.5, 0.30)],
                      [0.026, 0.026, 0.028], 'a_post'))
    o.append(cone(name + 'pole', (0.5, 0.5, 0.28), 0.03, 0.03, 0.18, 'a_post', verts=12))
    # the torso: a smooth lathe (hips, waist, bust, shoulders), flattened front to back
    prof = [(0.00, 0.43), (0.12, 0.435), (0.19, 0.47), (0.21, 0.52), (0.18, 0.58), (0.155, 0.63),
            (0.18, 0.69), (0.22, 0.745), (0.21, 0.79), (0.17, 0.835), (0.08, 0.865), (0.055, 0.88),
            (0.055, 0.905), (0.0, 0.91)]
    seg = 40
    verts, faces = [], []
    for (r, z) in prof:
        for j in range(seg):
            t = 2 * math.pi * j / seg
            verts.append((0.5 + r * math.cos(t), 0.5 + 0.72 * r * math.sin(t), z))
    for i in range(len(prof) - 1):
        for j in range(seg):
            k = (j + 1) % seg
            faces.append((i * seg + j, i * seg + k, (i + 1) * seg + k, (i + 1) * seg + j))
    body = C.mesh_object(name + 'body', verts, faces, 'a_form', smooth=True)
    sd = body.modifiers.new('sub', 'SUBSURF')
    sd.levels = 1
    sd.render_levels = 1
    o.append(body)
    o.append(ellipsoid(name + 'cap', (0.5, 0.5, 0.925), (0.06, 0.06, 0.035), 'a_post', seg=16, rings=8))
    o.append(ellipsoid(name + 'knob', (0.5, 0.5, 0.965), (0.026, 0.026, 0.026), 'a_brass', seg=12, rings=8))
    o.append(torus(name + 'tp', (0.5, 0.5, 0.88), 0.075, 0.013, 'a_mtape', rot=(-8, 0, 0), n=28, m=8))
    for s_, x in ((-1, 0.43), (1, 0.57)):
        o.append(tube(name + 'te%d' % s_, [(0.5 + s_ * 0.055, 0.44, 0.87), (x + s_ * 0.02, 0.33, 0.80),
                                           (x + s_ * 0.03, 0.33, 0.70), (x + s_ * 0.025, 0.35, 0.60 - 0.06 * s_)],
                      [0.014] * 4, 'a_mtape'))
    o.append(ellipsoid(name + 'pin', (0.24, 0.30, 0.09), (0.075, 0.075, 0.05), 'a_saddle', seg=16, rings=8))
    return o


def book_stacks(name='abs'):
    """Three stacks of old books, a teacup on the tallest one."""
    rnd = random.Random(21)
    o = []
    tops = []
    for si, (cx, cy, n) in enumerate(((0.29, 0.65, 6), (0.71, 0.62, 4), (0.50, 0.29, 3))):
        z = 0.0
        for k in range(n):
            w = rnd.uniform(0.33, 0.40)
            d = rnd.uniform(0.26, 0.32)
            h = rnd.uniform(0.05, 0.085)
            dx, dy = rnd.uniform(-0.03, 0.03), rnd.uniform(-0.02, 0.02)
            x0, y0 = cx - w / 2 + dx, cy - d / 2 + dy
            key = 'a_bk%d' % rnd.randrange(7)
            o.append(box(name + 'c%d%d' % (si, k), x0, y0, z, x0 + w, y0 + d, z + h, key, bevel=0.007))
            if k % 2 == 0:       # pages towards the camera, spines towards the camera on the others
                o.append(box(name + 'p%d%d' % (si, k), x0 + 0.012, y0 - 0.002, z + 0.009, x0 + w - 0.02, y0 + d - 0.02,
                             z + h - 0.009, 'a_pages'))
            else:
                o.append(box(name + 's%d%d' % (si, k), x0 + 0.03, y0 - 0.003, z + h * 0.35, x0 + w - 0.03, y0 + 0.01,
                             z + h * 0.65, 'a_plate_mark'))
            z += h
        tops.append((cx, cy, z))
    cx, cy, z = tops[0]
    o.append(cone(name + 'sau', (cx, cy, z), 0.075, 0.085, 0.012, 'a_cup'))
    o.append(cone(name + 'cup', (cx, cy, z + 0.012), 0.04, 0.055, 0.06, 'a_cup'))
    o.append(cone(name + 'tea', (cx, cy, z + 0.064), 0.05, 0.05, 0.004, 'a_tea'))
    o.append(torus(name + 'hdl', (cx + 0.062, cy, z + 0.045), 0.022, 0.008, 'a_cup', rot=(90, 0, 0), n=16, m=6))
    return o


def big_trunk(name='atk'):
    """The 2-cell prop: a big domed steamer trunk with travel stickers."""
    x0, x1, y0, y1 = 0.08, 1.92, 0.14, 0.86
    zb = 0.40
    o = [box(name + 'body', x0, y0, 0.0, x1, y1, zb, 'a_trunk2', bevel=0.02)]
    rl = (y1 - y0) / 2
    lid = cone(name + 'lid', (x0, 0.5, zb), rl, rl, x1 - x0, 'a_trunk2', rot=(0, 90, 0), verts=40,
               scale=(0.55, 1.0, 1.0))
    o.append(lid)
    for x in (0.30, 0.95, 1.60):
        o.append(box(name + 'sl%.2f' % x, x, y0 - 0.012, 0.0, x + 0.10, y0 + 0.02, zb, 'a_post', bevel=0.008))
        o.append(cone(name + 'ls%.2f' % x, (x, 0.5, zb), rl + 0.014, rl + 0.014, 0.10, 'a_post', rot=(0, 90, 0),
                      verts=40, scale=(0.56, 1.0, 1.0)))
    for x in (0.62, 1.30):            # leather straps over the lid
        o.append(cone(name + 'lt%.2f' % x, (x, 0.5, zb), rl + 0.02, rl + 0.02, 0.07, 'a_leather', rot=(0, 90, 0),
                      verts=40, scale=(0.57, 1.0, 1.0)))
    for x in (x0 + 0.005, x1 - 0.105):    # brass caps on the lid's ends
        o.append(cone(name + 'lc%.2f' % x, (x, 0.5, zb), rl + 0.016, rl + 0.016, 0.10, 'a_brass', rot=(0, 90, 0),
                      verts=40, scale=(0.565, 1.0, 1.0)))
    o.append(box(name + 'rim', x0 - 0.01, y0 - 0.012, zb - 0.03, x1 + 0.01, y0 + 0.02, zb + 0.01, 'a_leather',
                 bevel=0.006))
    for x in (x0, x1 - 0.09):
        for z in (0.0, zb - 0.10):
            o.append(box(name + 'bc%.2f%.2f' % (x, z), x, y0 - 0.016, z, x + 0.09, y0 + 0.02, z + 0.10, 'a_brass',
                         bevel=0.008))
    o.append(box(name + 'latch', 0.92, y0 - 0.03, zb - 0.14, 1.08, y0 + 0.02, zb + 0.04, 'a_brass', bevel=0.012))
    o.append(box(name + 'key', 0.99, y0 - 0.034, zb - 0.10, 1.01, y0, zb - 0.04, 'a_dark'))
    # travel stickers on the front
    o.append(prism(name + 'st1', L.circle(0.09, 28), 0.0, 0.006, 'a_stk0', xform=L.front_xform(0.62, y0 - 0.001, 0.2)))
    o.append(prism(name + 'st1s', L.star(0.055, 0.024), 0.006, 0.01, 'a_stk3',
                   xform=L.front_xform(0.62, y0 - 0.001, 0.2)))
    o.append(prism(name + 'st2', L.roundrect(0.20, 0.13, 0.02), 0.0, 0.006, 'a_stk1',
                   xform=L.front_xform(1.30, y0 - 0.001, 0.22)))
    o.append(prism(name + 'st2h', L.heart(0.07), 0.006, 0.01, 'a_stk2', xform=L.front_xform(1.30, y0 - 0.001, 0.215)))
    o.append(prism(name + 'st3', L.ellipse(0.10, 0.065, 28), 0.0, 0.006, 'a_stk2',
                   xform=L.front_xform(1.76, y0 - 0.001, 0.17)))
    return o


def attic_plate(down, name='apl'):
    """A round brass-rimmed plate with a paw in the middle; `down` = pressed
    flush. A full tile (the floor boards under it)."""
    o = attic_floor(0, name + 'f')
    o.append(annulus(name + 'rim', 0.5, 0.5, 0.29, 0.37, -0.01, 0.014, 'a_brass', n=48))
    o.append(cone(name + 'well', (0.5, 0.5, -0.02), 0.30, 0.30, 0.025, 'a_iron', verts=48))
    zt = 0.012 if down else 0.075
    o.append(cone(name + 'disc', (0.5, 0.5, -0.02), 0.28, 0.27, zt + 0.02, 'a_verdigris', verts=48))
    o.append(prism(name + 'paw', L.paw(0.30), zt - 0.001, zt + 0.006, 'a_plate_mark',
                   xform=L.top_xform(0.5, 0.49, 0.0)))
    return o


def attic_gate(orient, ang, name='agt'):
    """An old iron gate in a wall line: two posts, two leaves with brass
    finials, swinging away from the camera (00 shut .. 03 open). Built for
    'h' (the wall runs left-right); 'v' is it turned 90 degrees."""
    y0, y1 = 0.36, 0.64
    pw = 0.15
    zt = H - 0.05
    o = _posts('attic', name, y0, y1, pw, zt)
    lw = 0.5 - pw               # a leaf's width
    for side in (-1, 1):
        hx = pw if side < 0 else 1 - pw
        piv = bpy.data.objects.new(name + 'hg%d' % side, None)
        C.link(piv)
        piv.location = (hx, 0.5, 0.0)
        parts = []
        d = 1 if side < 0 else -1          # the leaf runs from the hinge towards the middle
        xa, xb = (0.0, lw - 0.012) if d > 0 else (-(lw - 0.012), 0.0)
        parts.append(box(name + 'rb%d' % side, xa, -0.018, 0.04, xb, 0.018, 0.075, 'a_iron', bevel=0.006))
        parts.append(box(name + 'rt%d' % side, xa, -0.018, 0.36, xb, 0.018, 0.395, 'a_iron', bevel=0.006))
        parts.append(box(name + 'rm%d' % side, xa, -0.014, 0.21, xb, 0.014, 0.235, 'a_iron', bevel=0.005))
        for k in range(4):
            x = xa + (xb - xa) * (k + 0.5) / 4
            parts.append(box(name + 'bar%d%d' % (side, k), x - 0.014, -0.014, 0.02, x + 0.014, 0.014, 0.44, 'a_iron',
                             bevel=0.005))
            parts.append(ellipsoid(name + 'fin%d%d' % (side, k), (x, 0.0, 0.455), (0.026, 0.026, 0.034), 'a_brass',
                                   seg=12, rings=8))
        # a curl in the middle panel
        cxm = (xa + xb) / 2
        parts.append(torus(name + 'curl%d' % side, (cxm, 0.0, 0.30), 0.045, 0.011, 'a_iron', rot=(90, 0, 0), n=24, m=6))
        for p_ in parts:
            p_.parent = piv
        piv.rotation_euler = (0, 0, math.radians(ang * d))
        o += parts
        o.append(piv)
    if orient == 'v':
        L.rot_about([ob for ob in o if ob.parent is None], 0.5, 0.5, 90)
    bpy.context.view_layer.update()
    return [ob for ob in o if ob.type == 'MESH']


# ---------------------------------------------------------------------------
# roofs: more props and the 2-cell chimney
# ---------------------------------------------------------------------------

def antenna(name='ran'):
    """A TV antenna on a concrete block, a little red light on top."""
    o = [box(name + 'base', 0.07, 0.07, 0.0, 0.93, 0.93, 0.14, 'r_concrete', bevel=0.02)]
    for (x, y) in ((0.16, 0.16), (0.84, 0.16), (0.16, 0.84), (0.84, 0.84)):
        o.append(cone(name + 'bolt%.1f%.1f' % (x, y), (x, y, 0.14), 0.025, 0.02, 0.02, 'r_metal', verts=10))
    o.append(cone(name + 'plate', (0.5, 0.5, 0.14), 0.16, 0.14, 0.03, 'r_metal', verts=24))
    o.append(cone(name + 'mast', (0.5, 0.5, 0.17), 0.028, 0.022, 0.76, 'r_metal', verts=14))
    for z, L_ in ((0.52, 0.40), (0.66, 0.33), (0.80, 0.26)):
        o.append(tube(name + 'el%.2f' % z, [(0.5 - L_, 0.5, z), (0.5, 0.5, z), (0.5 + L_, 0.5, z)], [0.016] * 3,
                      'r_metal'))
        for sx in (-1, 1):
            o.append(ellipsoid(name + 'tip%.2f%d' % (z, sx), (0.5 + sx * L_, 0.5, z), (0.022, 0.022, 0.022), 'r_metal',
                               seg=10, rings=6))
    o.append(tube(name + 'boom', [(0.5, 0.36, 0.72), (0.5, 0.5, 0.72), (0.5, 0.64, 0.72)], [0.016] * 3, 'r_metal'))
    o.append(ellipsoid(name + 'light', (0.5, 0.5, 0.955), (0.032, 0.032, 0.032), 'r_beacon', seg=14, rings=8))
    return o


def water_tank(name='rwt'):
    """A round water tank on a little iron stand, banded, a ladder in front."""
    o = [box(name + 'pl', 0.08, 0.08, 0.12, 0.92, 0.92, 0.17, 'r_metal', bevel=0.012)]
    for (x, y) in ((0.14, 0.14), (0.86, 0.14), (0.14, 0.86), (0.86, 0.86)):
        o.append(box(name + 'lg%.1f%.1f' % (x, y), x - 0.035, y - 0.035, 0.0, x + 0.035, y + 0.035, 0.12, 'r_metal',
                     bevel=0.008))
    o.append(cone(name + 'tank', (0.5, 0.52, 0.17), 0.40, 0.40, 0.52, 'r_tank', verts=48))
    o.append(cone(name + 'roof', (0.5, 0.52, 0.69), 0.41, 0.06, 0.16, 'r_tank', verts=48))
    o.append(cone(name + 'vent', (0.5, 0.52, 0.84), 0.05, 0.03, 0.07, 'r_tank_band', verts=16))
    for z in (0.28, 0.56):
        o.append(torus(name + 'band%.2f' % z, (0.5, 0.52, z), 0.405, 0.014, 'r_tank_band', n=56, m=8))
    # ladder on the front
    for x in (0.40, 0.60):
        o.append(box(name + 'lr%.2f' % x, x - 0.014, 0.075, 0.12, x + 0.014, 0.105, 0.74, 'r_metal', bevel=0.004))
    for z in (0.26, 0.38, 0.50, 0.62):
        o.append(box(name + 'rg%.2f' % z, 0.40, 0.08, z, 0.60, 0.10, z + 0.022, 'r_metal', bevel=0.004))
    return o


def sat_dish(name='rsd'):
    """A satellite dish tilted towards the camera on a block."""
    o = [box(name + 'base', 0.10, 0.12, 0.0, 0.90, 0.88, 0.14, 'r_concrete', bevel=0.02),
         cone(name + 'post', (0.5, 0.56, 0.14), 0.04, 0.035, 0.28, 'r_metal', verts=14)]
    # the dish: a shallow paraboloid (open side up), then tilted to face -Y
    R, f = 0.40, 0.50
    rings, seg = 8, 40
    verts, faces = [(0, 0, 0)], []
    for i in range(1, rings + 1):
        r = R * i / rings
        for j in range(seg):
            a = 2 * math.pi * j / seg
            verts.append((r * math.cos(a), r * math.sin(a), r * r / (4 * f)))
    for j in range(seg):
        faces.append((0, 1 + j, 1 + (j + 1) % seg))
    for i in range(1, rings):
        b0, b1 = 1 + (i - 1) * seg, 1 + i * seg
        for j in range(seg):
            k = (j + 1) % seg
            faces.append((b0 + j, b1 + j, b1 + k, b0 + k))
    dish = C.mesh_object(name + 'dish', verts, faces, 'r_dish', smooth=True)
    sol = dish.modifiers.new('sol', 'SOLIDIFY')
    sol.thickness = 0.025
    rimz = R * R / (4 * f)
    rim = torus(name + 'rim', (0, 0, rimz), R, 0.012, 'r_dish', n=48, m=6)
    arm = tube(name + 'arm', [(0, -R * 0.95, rimz), (0, -R * 0.5, f * 0.45), (0, 0, f * 0.62)], [0.012] * 3, 'r_metal')
    lnb = cone(name + 'lnb', (0, 0, f * 0.55), 0.035, 0.03, 0.09, 'r_metal', verts=12)
    piv = bpy.data.objects.new(name + 'piv', None)
    C.link(piv)
    piv.location = (0.5, 0.56, 0.44)
    for p_ in (dish, rim, arm, lnb):
        p_.parent = piv
    piv.rotation_euler = (math.radians(40), 0, 0)
    o += [dish, rim, arm, lnb]
    o.append(ellipsoid(name + 'hub', (0.5, 0.56, 0.44), (0.06, 0.06, 0.06), 'r_metal', seg=14, rings=8))
    bpy.context.view_layer.update()
    return o


def chimney2(name='rch'):
    """The 2-cell prop: a brick chimney stack with two terracotta pots."""
    x0, x1, y0, y1 = 0.06, 1.94, 0.16, 0.84
    zc = 0.66
    o = [box(name + 'core', x0, y0 + 0.02, 0.0, x1, y1, zc, 'r_mortar')]
    rnd = random.Random(9)
    ch = zc / 6.0
    bw = (x1 - x0) / 7.0
    for c in range(6):
        z0, z1 = c * ch + 0.008, (c + 1) * ch - 0.008
        off = 0.0 if c % 2 == 0 else bw / 2
        xs = [x0] + [x0 + off + bw * k for k in range(1, 8) if x0 + off + bw * k < x1 - 0.02] + [x1]
        for j in range(len(xs) - 1):
            o.append(box(name + 'b%d%d' % (c, j), xs[j] + 0.008, y0, z0, xs[j + 1] - 0.008, y0 + 0.03, z1,
                         'r_brick%d' % rnd.randrange(3), bevel=0.006))
    o.append(box(name + 'cap', x0 - 0.04, y0 - 0.04, zc, x1 + 0.04, y1 + 0.04, zc + 0.07, 'r_cap0', bevel=0.015))
    for px in (0.60, 1.40):
        o.append(cone(name + 'pot%.1f' % px, (px, 0.5, zc + 0.07), 0.13, 0.10, 0.22, 'r_pot', verts=32))
        o.append(torus(name + 'pr%.1f' % px, (px, 0.5, zc + 0.29), 0.10, 0.022, 'r_pot', n=32, m=8))
        o.append(cone(name + 'ph%.1f' % px, (px, 0.5, zc + 0.26), 0.085, 0.085, 0.03, 'r_hole', verts=24))
    return o


def ball_roll(v, frame, name='rbr'):
    """The ball turned by frame * 45 degrees about a diagonal horizontal
    axis (reads as rolling both across and up the screen)."""
    o = ball(v, name)
    ob = o[0]
    base = ob.rotation_euler.to_quaternion()
    q = mathutils.Quaternion(mathutils.Vector((0.7071, -0.7071, 0.0)), math.radians(45 * frame))
    ob.rotation_mode = 'QUATERNION'
    ob.rotation_quaternion = q @ base
    bpy.context.view_layer.update()
    return o


def puddle_v(v, name='rp'):
    """Puddle variants: another outline, another floor variant under it."""
    o = roofs_floor(v % 2, name + 'fl')
    pts = []
    n = 40
    ph = [0.5, 2.1][v % 2]
    for k in range(n):
        a = 2 * math.pi * k / n
        r = 0.40 + 0.035 * math.sin(3 * a + ph) + 0.025 * math.sin(5 * a + 1.3 + ph)
        pts.append((0.5 + r * math.cos(a) * 1.05, 0.5 + r * math.sin(a) * 0.95))
    o.append(prism(name + 'w', pts, 0.0, 0.036, 'r_water', smooth=True))
    mx, my = [(0.62, 0.62), (0.36, 0.60)][v % 2]
    o.append(prism(name + 'm', L.ellipse(0.07, 0.07, 24, mx, my), 0.036, 0.038, 'r_water_moon'))
    for (cx, cy, rx) in ([((0.32, 0.40, 0.10), (0.44, 0.30, 0.05)), ((0.60, 0.36, 0.10), (0.70, 0.46, 0.05))][v % 2]):
        o.append(prism(name + 'g%.2f' % cx, L.ellipse(rx, 0.012, 16, cx, cy, 20), 0.036, 0.038, 'r_water_hi'))
    return o


# ===========================================================================
# the sprite lists
# ===========================================================================

FLAP_ANG = [0, -25, -50, -75]   # 00 still, 01..03 swinging open towards the camera (the engine plays 1 2 3 3 2 1)
GATE_ANG = [0, 30, 60, 88]      # 00 shut .. 03 open


def attic_jobs(phase):
    P1 = phase == 1
    J = []
    for v in range(2 if P1 else 4):
        J.append(('attic_floor_v%d' % v, lambda v=v: attic_floor(v), dict(kind='tile')))
    for v in range(1 if P1 else 2):
        J.append(('attic_wall_v%d' % v, lambda v=v: attic_wall(v), dict(kind='prop', shadow=True)))
    props = [('armchair', armchair_sheet, {}), ('horse', rocking_horse, {})]
    if not P1:
        props += [('lamp', attic_lamp, dict(glow=40)), ('dressform', dress_form, {}), ('books', book_stacks, {})]
    for n, fn, kw in props:
        J.append(('attic_prop_' + n, fn, dict(kind='prop', shadow=True, samples=64, **kw)))
    if not P1:
        J.append(('attic_prop2_trunk', big_trunk, dict(kind='prop', shadow=True, samples=64,
                                                         extra={'footprint': [2, 1]})))
    for v in range(2 if P1 else 4):
        J.append(('attic_obj_v%d' % v, lambda v=v: cardboard(v), dict(kind='obj', sh_sprite=True, samples=64)))
    J.append(('attic_target', lambda: attic_floor(0) + tape_cross('tx', 0.0), dict(kind='tile')))
    for d in ('v', 'h'):
        for f in range(1 if P1 else 4):
            J.append(('attic_flap_%s_%02d' % (d, f), lambda d=d, f=f: attic_flap(d, FLAP_ANG[f]),
                      dict(kind='prop', shadow=True, extra={'anim': 'flap', 'frame': f, 'frames': 4})))
    if not P1:
        J.append(('attic_plate_up', lambda: attic_plate(False), dict(kind='tile')))
        J.append(('attic_plate_down', lambda: attic_plate(True), dict(kind='tile')))
        for d in ('h', 'v'):
            for f in range(4):
                J.append(('attic_gate_%s_%02d' % (d, f), lambda d=d, f=f: attic_gate(d, GATE_ANG[f]),
                          dict(kind='prop', shadow=True, extra={'anim': 'gate', 'frame': f, 'frames': 4})))
    return J


def roofs_jobs(phase):
    P1 = phase == 1
    J = []
    for v in range(2 if P1 else 4):
        J.append(('roofs_floor_v%d' % v, lambda v=v: roofs_floor(v), dict(kind='tile')))
    for v in range(2):
        J.append(('roofs_wall_v%d' % v, lambda v=v: roofs_wall(v), dict(kind='prop', shadow=True)))
    props = [('skylight', skylight, dict(glow=40)), ('dovecote', pigeon_house, {})]
    if not P1:
        props += [('antenna', antenna, {}), ('tank', water_tank, {}), ('dish', sat_dish, {})]
    for n, fn, kw in props:
        J.append(('roofs_prop_' + n, fn, dict(kind='prop', shadow=True, samples=64, **kw)))
    if not P1:
        J.append(('roofs_prop2_chimney', chimney2, dict(kind='prop', shadow=True, samples=64,
                                                         extra={'footprint': [2, 1]})))
    for v in range(2 if P1 else 4):
        J.append(('roofs_obj_v%d' % v, lambda v=v: crate(v), dict(kind='obj', sh_sprite=True, samples=64)))
    J.append(('roofs_target', lambda: roofs_floor(0, 'tf') + moon_mark(), dict(kind='tile', glow=40)))
    J.append(('roofs_hole', roofs_hole, dict(kind='tile')))
    for v in range(1 if P1 else 4):
        J.append(('roofs_hole_crate_v%d' % v, lambda v=v: roofs_hole('rhc', with_crate=v), dict(kind='tile')))
    for v in range(1 if P1 else 2):
        J.append(('roofs_ball_v%d' % v, lambda v=v: ball(v), dict(kind='obj', sh_sprite=True, samples=64)))
        if not P1:
            for f in range(8):
                J.append(('roofs_ball_roll_v%d_%02d' % (v, f), lambda v=v, f=f: ball_roll(v, f),
                          dict(kind='obj', samples=64, extra={'anim': 'roll', 'frame': f, 'frames': 8})))
    J.append(('roofs_wet_v0', puddle, dict(kind='tile', samples=64)))
    if not P1:
        J.append(('roofs_wet_v1', lambda: puddle_v(1), dict(kind='tile', samples=64)))
        for d in ('v', 'h'):
            for f in range(4):
                J.append(('roofs_flap_%s_%02d' % (d, f), lambda d=d, f=f: attic_flap(d, FLAP_ANG[f], 'rfl', 'roofs'),
                          dict(kind='prop', shadow=True, extra={'anim': 'flap', 'frame': f, 'frames': 4})))
    return J


def main():
    a = C.args()
    argv = sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else []
    opt = {'world': 'attic,roofs', 'phase': '1'}
    for i, s in enumerate(argv):
        if s in ('--world', '--phase') and i + 1 < len(argv):
            opt[s[2:]] = argv[i + 1]
    phase = int(opt['phase'])
    root = os.path.abspath(a.out)
    t_all = time.time()
    for world in opt['world'].split(','):
        C.reset(world, cpu=a.cpu)
        (attic_mats if world == 'attic' else roofs_mats)()
        jobs = (attic_jobs if world == 'attic' else roofs_jobs)(phase)
        out = os.path.join(root, world)
        for name, fn, kw in jobs:
            if a.only and name not in a.only:
                continue
            kw = dict(kw)
            if a.samples != 48 and 'samples' not in kw:
                kw['samples'] = a.samples
            objs = fn()
            info = L.shoot(out, name, objs, **kw)
            if 'gl' in info['files']:
                L.fade_edges(os.path.join(out, info['files']['gl']))
        C.save_meta(out)
    print('TOTAL %.1fs for %d sprites' % (time.time() - t_all, len(L.TIMES)))


main()
