"""Mila - the kits of the first three worlds: living room, kitchen, garden.

    Blender -b -P worlds_a.py -- --out ../../assets --world living,kitchen,garden
                                  [--phase 1|2] [--only name,name] [--samples N]

--out is the assets directory: each world writes into <out>/<world>/ (with
its meta.json). Phase 1 is the style sample (a subset); phase 2 everything.
Everything is built at cell (0, 0), floor 0, anchored at C.cell(0, 0, 0).
See SPEC.md section 5 and DESIGN.md.
"""
import math
import os
import random
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import bpy  # noqa: E402
import ml_common as C  # noqa: E402
import wa_lib as L  # noqa: E402
from wa_lib import pm, wood, box, rbox, cone, ellipsoid, torus, disc, tube, prism  # noqa: E402

FM = C.FLOOR_M       # a wall's height, 0.504 m
E = 0.012            # walls and floors reach this far past their cell: no seams
APRON = -0.085       # the floor tiles' hidden apron (below the depth bias)


def apron(name, key):
    """A plate under the tile, 1 px wider than the cell, deep enough that the
    neighbour's surface always wins the depth test: the cell's edge pixels
    become fully opaque (no dark antialiasing seams between tiles)."""
    return box(name, -0.015, -0.015, APRON - 0.02, 1.015, 1.015, APRON, key)


# ===========================================================================
# LIVING ROOM (from sample.py, approved)
# ===========================================================================
YARN = [(0.95, 0.40, 0.60), (0.25, 0.75, 0.78), (0.98, 0.78, 0.25), (0.62, 0.48, 0.95)]


def living_floor(v):
    under = pm('lv_under', (0.18, 0.10, 0.06), rough=0.9)
    o = [apron('ap', under), box('u', 0, 0, -0.06, 1, 1, -0.012, under)]
    rnd = random.Random(1000 + v * 17)
    # v1..v3: some boards end inside the cell (a joint)
    joints = {0: {}, 1: {1: 0.36, 3: 0.72}, 2: {0: 0.62, 2: 0.28}, 3: {1: 0.8, 2: 0.45, 3: 0.15}}[v]
    for k in range(4):
        y0 = k * 0.25 + 0.006
        y1 = (k + 1) * 0.25 - 0.006
        cuts = [0.003] + ([joints[k]] if k in joints else []) + [0.997]
        for j in range(len(cuts) - 1):
            shade = rnd.uniform(0.9, 1.08)
            col = (0.78 * shade, 0.52 * shade, 0.30 * shade)
            m = wood('lv_plank_%d_%d_%d' % (v, k, j), col,
                     offset=(rnd.uniform(0, 9), rnd.uniform(0, 9), rnd.uniform(0, 9)))
            x0 = cuts[j] + (0.004 if j > 0 else 0.0)
            x1 = cuts[j + 1] - (0.004 if j < len(cuts) - 2 else 0.0)
            o.append(box('p%d%d' % (k, j), x0, y0, -0.03, x1, y1, 0.0, m, bevel=0.004))
    return o


def living_wall(v):
    paper = pm('lv_paper', (0.30, 0.55, 0.58), rough=0.8)
    wain = pm('lv_wain', (0.93, 0.90, 0.84), rough=0.6)
    top = pm('lv_walltop', (0.16, 0.09, 0.06), rough=0.55, spec=0.35, cc=0.15,
             tex=L.tex_twood(fx=1.0, fy=12.0, dark=0.72))
    o = [box('w', -E, 0, 0.16, 1 + E, 1, FM - 0.05, paper),
         box('b', -E, -0.006, 0, 1 + E, 1, 0.17, wain),
         box('t', -E, -E, FM - 0.05, 1 + E, 1 + E, FM, top)]
    if v == 1:
        # a mouse hole in the wainscot: a dark arch
        dark = pm('lv_hole', (0.03, 0.02, 0.02), rough=1.0)
        arch = [(0.53, 0.0), (0.71, 0.0)] + [(0.62 + 0.09 * math.cos(math.pi * i / 14), 0.115 * math.sin(math.pi * i / 14))
                                             for i in range(1, 14)]
        o.append(prism_front('hole', arch, -0.009, dark))
    return o


def prism_front(name, pts_xz, y, key, depth=0.004):
    """A flat shape on a wall front (points in x, z), just in front of y."""
    n = len(pts_xz)
    v = [(x, y, z) for x, z in pts_xz] + [(x, y + depth, z) for x, z in pts_xz]
    f = [tuple(range(n)), tuple(range(2 * n - 1, n - 1, -1))]
    for i in range(n):
        j = (i + 1) % n
        f.append((j, i, n + i, n + j))
    return C.mesh_object(name, v, f, key)


def living_armchair():
    fab = pm('lv_chair', (0.36, 0.45, 0.56), rough=0.85, sheen=0.8)
    fab2 = pm('lv_chair2', (0.47, 0.57, 0.68), rough=0.85, sheen=0.8)
    leg = wood('lv_sofaleg', (0.35, 0.22, 0.12))
    pil = pm('lv_pillow_c', (0.98, 0.80, 0.62), rough=0.9, sheen=1.0)
    o = []
    for lx in (0.10, 0.85):
        for ly in (0.10, 0.85):
            o.append(box('lg%.1f%.1f' % (lx, ly), lx, ly, 0, lx + 0.05, ly + 0.05, 0.1, leg))
    o.append(box('base', 0.07, 0.06, 0.08, 0.93, 0.94, 0.32, fab, bevel=0.06))
    o.append(box('back', 0.08, 0.64, 0.28, 0.92, 0.93, 0.76, fab, bevel=0.08))
    for sx in (0.07, 0.75):
        o.append(box('arm%.2f' % sx, sx, 0.08, 0.28, sx + 0.18, 0.93, 0.54, fab, bevel=0.07))
    o.append(box('cu', 0.24, 0.10, 0.28, 0.76, 0.70, 0.42, fab2, bevel=0.06))
    # a peach pillow leaning on the back
    o.append(ellipsoid('pil', (0.50, 0.60, 0.57), (0.17, 0.06, 0.14), pil, rot=(-18, 0, -6)))
    return o


def living_side_table():
    wd = wood('lv_table', (0.62, 0.40, 0.22))
    shade = pm('lv_lampshade', (1.0, 0.92, 0.7), rough=0.9, emit=(1.0, 0.8, 0.45), es=1.2)
    brass = pm('lv_brass', (0.9, 0.7, 0.35), rough=0.3, metal=1.0)
    book = pm('lv_book', (0.25, 0.4, 0.8), rough=0.7)
    c = (0.5, 0.5)
    o = [cone('top', (c[0], c[1], 0.34), 0.45, 0.45, 0.06, wd, bevel=0.012),
         cone('leg', (c[0], c[1], 0.0), 0.05, 0.05, 0.34, wd),
         cone('foot', (c[0], c[1], 0.0), 0.2, 0.18, 0.03, wd),
         cone('lampb', (c[0] + 0.08, c[1] + 0.08, 0.40), 0.07, 0.03, 0.2, brass),
         cone('shade', (c[0] + 0.08, c[1] + 0.08, 0.57), 0.14, 0.08, 0.16, shade),
         box('book', c[0] - 0.24, c[1] - 0.2, 0.40, c[0] - 0.02, c[1] + 0.02, 0.44, book, bevel=0.005)]
    # the lamp: a soft spot pointing down (the shade), so its pool on the
    # floor fades out inside the glow sprite; no shadows (see glow pass)
    lt = bpy.data.lights.new('lamp', 'SPOT')
    lt.color = (1.0, 0.75, 0.45)
    lt.energy = 5.0
    lt.shadow_soft_size = 0.08
    lt.spot_size = math.radians(115)
    lt.spot_blend = 1.0
    lt.use_shadow = False
    lt.cycles.cast_shadow = False
    lamp = bpy.data.objects.new('lamp', lt)
    lamp.location = (c[0] + 0.08, c[1] + 0.08, 0.60)
    C.link(lamp)
    return o, [lamp]


def yarn(v, in_basket=False):
    col = YARN[v]
    m = pm('lv_yarn%d' % v, col, rough=0.9, sheen=0.8, spec=0.2, tex=L.tex_yarn())
    z = 0.30 + (0.07 if in_basket else 0.0)
    c = (0.5, 0.5)
    o = [ellipsoid('ball', (c[0], c[1], z), (0.30, 0.30, 0.29), m, rot=(20, 35, 10 + 40 * v))]
    rnd = random.Random(77 + v)
    for k in range(3):
        o.append(torus('w%d' % k, (c[0], c[1], z), 0.293, 0.012, m,
                       rot=(rnd.uniform(0, 180), rnd.uniform(0, 180), rnd.uniform(0, 180))))
    if not in_basket:
        # a loose thread on the floor, trailing to the front-right
        a = math.radians((-55, -20, -140, -80)[v])
        p0 = (c[0] + 0.2 * math.cos(a), c[1] + 0.2 * math.sin(a), 0.1)
        pts = [p0, (c[0] + 0.30 * math.cos(a), c[1] + 0.30 * math.sin(a), 0.02),
               (c[0] + 0.38 * math.cos(a + 0.45), c[1] + 0.38 * math.sin(a + 0.45), 0.012),
               (c[0] + 0.45 * math.cos(a + 0.15), c[1] + 0.45 * math.sin(a + 0.15), 0.012)]
        o.append(tube('str', pts, [0.012, 0.012, 0.011, 0.01], m))
    return o


def living_target():
    wick = wood('lv_wicker', (0.62, 0.38, 0.16), scale=6)
    wick2 = pm('lv_wicker2', (0.80, 0.56, 0.28), rough=0.7)
    cush = pm('lv_cushion', (0.92, 0.36, 0.42), rough=0.9, sheen=1.0)
    paw = pm('lv_paw', (1.0, 0.85, 0.88), rough=0.9)
    c = (0.5, 0.5)
    o = [cone('b', (c[0], c[1], 0.0), 0.35, 0.43, 0.16, wick),
         torus('rim', (c[0], c[1], 0.16), 0.42, 0.04, wick2),
         ellipsoid('cu', (c[0], c[1], 0.14), (0.36, 0.36, 0.045), cush),
         ellipsoid('pp', (c[0], c[1] - 0.04, 0.183), (0.09, 0.075, 0.01), paw)]
    for k, (dx, dy) in enumerate(((-0.1, 0.07), (-0.035, 0.12), (0.035, 0.12), (0.1, 0.07))):
        o.append(ellipsoid('pt%d' % k, (c[0] + dx, c[1] + dy, 0.18), (0.033, 0.033, 0.01), paw))
    return o


# ===========================================================================
# KITCHEN
# ===========================================================================
K_MINT = (0.40, 0.76, 0.60)
K_WHITE = (0.86, 0.86, 0.82)
TINS = [(0.86, 0.16, 0.18), (0.18, 0.42, 0.88), (0.98, 0.74, 0.12), (0.62, 0.40, 0.88)]


def kitchen_tile_mats(v, wet=False):
    """The tile materials of floor variant v (a little tone per tile)."""
    rnd = random.Random(500 + v)
    keys = []
    for i in range(4):
        base = K_MINT if i in (0, 3) else K_WHITE
        k = rnd.uniform(0.97, 1.03) if v else 1.0
        col = tuple(min(1, c * k) for c in base)
        key = 'kt_tile_%d_%d%s' % (v, i, '_wet' if wet else '')
        if wet:
            wet_tile_mat(key, col, v)
        else:
            pm(key, col, rough=0.28, spec=0.45, cc=0.35)
        keys.append(key)
    return keys


def kitchen_floor(v, wet=False):
    grout = pm('kt_grout', (0.70, 0.70, 0.66), rough=0.9)
    o = [apron('ap', grout), box('g', 0, 0, -0.05, 1, 1, -0.008, grout)]
    keys = kitchen_tile_mats(v, wet)
    # 2 x 2 tiles: top-left and bottom-right mint, so any cell order checkers
    spots = [(0, 1), (1, 1), (0, 0), (1, 0)]      # TL, TR, BL, BR (y up = back)
    for i, (ix, iy) in enumerate(spots):
        x0 = ix * 0.5 + (0.009 if ix == 0 else 0.004)
        x1 = (ix + 1) * 0.5 - (0.009 if ix == 1 else 0.004)
        y0 = iy * 0.5 + (0.009 if iy == 0 else 0.004)
        y1 = (iy + 1) * 0.5 - (0.009 if iy == 1 else 0.004)
        o.append(box('t%d' % i, x0, y0, -0.03, x1, y1, 0.0, keys[i], bevel=0.009))
    if wet:
        o += puddle_glints(v)
    return o


def wet_tile_mat(key, col, v=0):
    """A tile under a puddle: inside a wobbly round mask (world space, cell
    centred) the colour goes blue, the surface glassy, the edge a darker
    line of water."""
    water = (0.30, 0.62, 0.95)
    rim = (0.16, 0.42, 0.85)
    seed = 2.1 * v
    cx, cy = ((0.5, 0.5), (0.53, 0.47))[v % 2]

    def build(nt, neutral):
        b = nt.nodes.new('ShaderNodeBsdfPrincipled')
        b.inputs['Specular'].default_value = 0.6
        b.inputs['Clearcoat'].default_value = 1.0
        b.inputs['Clearcoat Roughness'].default_value = 0.03
        x, y, z = L._world_xyz(nt)
        dx = L._math(nt, 'SUBTRACT', x, cx)
        dy = L._math(nt, 'SUBTRACT', y, cy)
        d = L._math(nt, 'SQRT', L._math(nt, 'ADD', L._math(nt, 'MULTIPLY', dx, dx),
                                         L._math(nt, 'MULTIPLY', dy, dy)))
        ang = L._math(nt, 'ARCTAN2', dy, dx)
        wob = L._math(nt, 'ADD', L._math(nt, 'MULTIPLY', L._math(nt, 'SINE', L._math(nt, 'MULTIPLY', ang, 3.0)), 0.035),
                      L._math(nt, 'MULTIPLY', L._math(nt, 'SINE', L._math(nt, 'ADD', L._math(nt, 'MULTIPLY', ang, 5.0), 1.3 + seed)), 0.022))
        r = L._math(nt, 'SUBTRACT', d, wob)
        R = 0.40 if v % 2 == 0 else 0.38
        inside = L._math(nt, 'LESS_THAN', r, R)
        edge = L._math(nt, 'MULTIPLY', inside, L._math(nt, 'GREATER_THAN', r, R - 0.025))
        tile = (0.8, 0.8, 0.8) if neutral else col
        wet_col = tuple(0.35 * t + 0.65 * w for t, w in zip(tile, water))
        mix = nt.nodes.new('ShaderNodeMixRGB')
        mix.inputs['Color1'].default_value = tuple(tile) + (1,)
        mix.inputs['Color2'].default_value = tuple(wet_col) + (1,)
        nt.links.new(inside, mix.inputs['Fac'])
        mix2 = nt.nodes.new('ShaderNodeMixRGB')
        nt.links.new(mix.outputs[0], mix2.inputs['Color1'])
        mix2.inputs['Color2'].default_value = tuple(rim) + (1,)
        nt.links.new(edge, mix2.inputs['Fac'])
        nt.links.new(mix2.outputs[0], b.inputs['Base Color'])
        rough = L._math(nt, 'MULTIPLY_ADD', inside, -0.24, 0.28)
        nt.links.new(rough, b.inputs['Roughness'])
        return b.outputs['BSDF']
    C.mat(key, base=col, build=build)
    return key


def puddle_glints(v, z=0.003):
    """Cartoon highlights on the water: a curved streak and two dots."""
    g = pm('kt_glint', (1, 1, 1), rough=0.2, emit=(1, 1, 1), es=1.6)
    sh = (0.0, 0.0) if v == 0 else (0.04, -0.03)
    o = []
    pts = []
    for i in range(6):
        a = math.radians(120 + i * 14)
        pts.append((0.5 + sh[0] + 0.25 * math.cos(a), 0.5 + sh[1] + 0.22 * math.sin(a), z))
    o.append(tube('gl', pts, [0.010, 0.016, 0.018, 0.018, 0.014, 0.008], g))
    for k, (dx, dy, r) in enumerate(((0.13, 0.20, 0.028), (0.20, 0.13, 0.018))):
        o.append(ellipsoid('gd%d' % k, (0.5 + sh[0] - dx, 0.5 + sh[1] + dy, z), (r, r, 0.004), g, seg=16, rings=8))
    o.append(ellipsoid('gd3', (0.5 + sh[0] + 0.22, 0.5 + sh[1] - 0.2, z), (0.022, 0.022, 0.004), g, seg=16, rings=8))
    return o


def kitchen_wall(v):
    tile = pm('kt_walltile', (0.98, 0.84, 0.46), rough=0.35, spec=0.5, cc=0.3,
              tex=L.tex_tiles((0.97, 0.95, 0.90), bw=0.25, rh=0.145, mortar=0.014, var=0.05))
    top = pm('kt_worktop', (0.27, 0.155, 0.08), rough=0.5, spec=0.35, cc=0.2,
             tex=L.tex_twood(fx=1.0, fy=10.0, dark=0.8, seed=2.0))
    edge = pm('kt_edge', (0.40, 0.23, 0.11), rough=0.5, spec=0.35)
    base = pm('kt_skirt', (0.93, 0.93, 0.90), rough=0.5)
    o = [box('w', -E, 0, 0.04, 1 + E, 1, FM - 0.07, tile),
         box('s', -E, -0.004, 0, 1 + E, 1, 0.045, base),
         box('e', -E, -0.022, FM - 0.075, 1 + E, 1 + E, FM - 0.02, edge),
         box('t', -E, -0.022, FM - 0.03, 1 + E, 1 + E, FM, top)]
    if v == 1:
        # a row of blue decor tiles (a little flower in a diamond) across the front
        blue = pm('kt_decor', (0.22, 0.45, 0.88), rough=0.3, spec=0.5, cc=0.3)
        wh = pm('kt_decorw', (0.98, 0.97, 0.94), rough=0.4)
        z = 0.22
        for k in range(4):
            x = 0.125 + k * 0.25
            o.append(prism_front('dm%d' % k, [(x - 0.07, z), (x, z - 0.07), (x + 0.07, z), (x, z + 0.07)], -0.004, blue))
            o.append(prism_front('dc%d' % k, [(x - 0.022, z), (x, z - 0.022), (x + 0.022, z), (x, z + 0.022)], -0.007, wh))
    return o


def kitchen_fridge():
    body = pm('kt_fridge', (0.52, 0.74, 0.88), rough=0.25, spec=0.5, cc=0.6)
    chrome = pm('kt_chrome', (0.85, 0.86, 0.9), rough=0.15, metal=1.0)
    groove = pm('kt_groove', (0.20, 0.30, 0.38), rough=0.6)
    paper = pm('kt_note', (0.98, 0.97, 0.92), rough=0.9)
    ink = pm('kt_ink', (0.9, 0.35, 0.45), rough=0.9)
    H = 0.84
    o = [rbox('body', 0.07, 0.08, 0.03, 0.93, 0.92, H, body, 0.08),
         box('feet', 0.12, 0.14, 0.0, 0.88, 0.86, 0.04, groove),
         box('split', 0.075, 0.074, 0.585, 0.925, 0.09, 0.600, groove)]
    for z0, z1 in ((0.63, 0.78), (0.36, 0.54)):
        o.append(rbox('h%.2f' % z0, 0.78, 0.035, z0, 0.83, 0.075, z1, chrome, 0.018))
    # a drawing held by a magnet (Mila's portrait, by a child) + magnets
    o.append(box('note', 0.22, 0.072, 0.20, 0.44, 0.078, 0.47, paper))
    o.append(ellipsoid('cat', (0.33, 0.070, 0.31), (0.05, 0.004, 0.045), pm('kt_black', (0.03, 0.03, 0.04), rough=0.6)))
    for sx in (-1, 1):
        o.append(cone('ear%d' % sx, (0.33 + sx * 0.03, 0.070, 0.34), 0.018, 0.0, 0.04,
                      pm('kt_black', (0.03, 0.03, 0.04)), rot=(90, 0, 0), scale=(1, 1, 1)))
    o.append(ellipsoid('heart', (0.39, 0.070, 0.42), (0.018, 0.004, 0.016), ink))
    for k, (mx, mz, col) in enumerate(((0.33, 0.47, (0.95, 0.3, 0.3)), (0.60, 0.42, (0.98, 0.78, 0.2)),
                                       (0.55, 0.25, (0.35, 0.8, 0.5)), (0.66, 0.70, (0.95, 0.5, 0.75)))):
        o.append(ellipsoid('mg%d' % k, (mx, 0.074, mz), (0.028, 0.014, 0.028),
                           pm('kt_mag%d' % k, col, rough=0.3, cc=0.5), seg=16, rings=10))
    # a little pot of basil on top
    pot = pm('kt_pot', (0.82, 0.44, 0.28), rough=0.6)
    leaf = pm('kt_basil', (0.28, 0.66, 0.26), rough=0.5, spec=0.4)
    o.append(cone('pot', (0.70, 0.62, H - 0.005), 0.065, 0.085, 0.09, pot))
    rnd = random.Random(3)
    for k in range(7):
        a = 2 * math.pi * k / 7
        o.append(ellipsoid('lf%d' % k, (0.70 + 0.05 * math.cos(a), 0.62 + 0.05 * math.sin(a), H + 0.12 + rnd.uniform(-0.02, 0.03)),
                           (0.055, 0.035, 0.02), leaf, rot=(0, -25, math.degrees(a))))
    return o


def kitchen_stove():
    enamel = pm('kt_enamel', (0.95, 0.92, 0.84), rough=0.3, spec=0.5, cc=0.5)
    dark = pm('kt_burner', (0.10, 0.10, 0.11), rough=0.45, spec=0.4)
    iron = pm('kt_iron', (0.18, 0.18, 0.19), rough=0.4, metal=0.6)
    glass = pm('kt_glass', (0.05, 0.07, 0.10), rough=0.08, spec=0.8, cc=1.0)
    chrome = pm('kt_chrome', (0.85, 0.86, 0.9), rough=0.15, metal=1.0)
    red = pm('kt_kettle', (0.88, 0.20, 0.20), rough=0.25, spec=0.5, cc=0.8)
    H = 0.58
    o = [rbox('body', 0.06, 0.08, 0.05, 0.94, 0.92, H, enamel, 0.05)]
    for fx in (0.12, 0.82):
        for fy in (0.14, 0.80):
            o.append(cone('ft%.1f%.1f' % (fx, fy), (fx + 0.03, fy + 0.03, 0), 0.035, 0.03, 0.06, dark))
    # backsplash with a little clock
    o.append(rbox('back', 0.06, 0.82, H - 0.02, 0.94, 0.92, H + 0.16, enamel, 0.03))
    o.append(cone('clock', (0.5, 0.815, H + 0.07), 0.05, 0.05, 0.01, pm('kt_clockface', (1, 1, 0.95)), rot=(90, 0, 0)))
    for k, cx in enumerate((0.22, 0.38, 0.62, 0.78)):
        o.append(cone('knob%d' % k, (cx, 0.82, H + 0.07), 0.025, 0.022, 0.03, iron, rot=(90, 0, 0)))
    # hob: four burners with grates
    for k, (bx, by) in enumerate(((0.29, 0.31), (0.71, 0.31), (0.29, 0.62), (0.71, 0.62))):
        r = 0.13 if k < 2 else 0.11
        o.append(cone('bu%d' % k, (bx, by, H - 0.005), r, r, 0.012, dark))
        o.append(torus('gr%d' % k, (bx, by, H + 0.012), r * 0.72, 0.009, iron))
        for a in (0, 90):
            ca, sa = math.cos(math.radians(a)), math.sin(math.radians(a))
            o.append(tube('gx%d_%d' % (k, a), [(bx - ca * r * 0.9, by - sa * r * 0.9, H + 0.014),
                                               (bx, by, H + 0.016),
                                               (bx + ca * r * 0.9, by + sa * r * 0.9, H + 0.014)],
                          [0.008, 0.008, 0.008], iron, res=2, bev=2))
    # oven door with a window and a handle
    o.append(rbox('door', 0.13, 0.066, 0.11, 0.87, 0.10, 0.47, enamel, 0.02))
    o.append(rbox('win', 0.24, 0.058, 0.18, 0.76, 0.08, 0.36, glass, 0.02))
    o.append(tube('handle', [(0.22, 0.05, 0.43), (0.24, 0.035, 0.43), (0.76, 0.035, 0.43), (0.78, 0.05, 0.43)],
                  [0.012, 0.012, 0.012, 0.012], chrome, res=6, bev=3))
    # a red kettle on the front-right burner
    kx, ky, kz = 0.71, 0.33, H + 0.02
    o.append(ellipsoid('kb', (kx, ky, kz + 0.10), (0.13, 0.13, 0.11), red))
    o.append(cone('kbase', (kx, ky, kz), 0.10, 0.12, 0.06, red))
    o.append(ellipsoid('klid', (kx, ky, kz + 0.19), (0.07, 0.07, 0.03), red))
    o.append(ellipsoid('kknob', (kx, ky, kz + 0.225), (0.022, 0.022, 0.018), dark))
    o.append(tube('kspout', [(kx - 0.10, ky - 0.02, kz + 0.09), (kx - 0.17, ky - 0.03, kz + 0.14),
                             (kx - 0.20, ky - 0.03, kz + 0.19)], [0.03, 0.022, 0.016], red))
    o.append(tube('khandle', [(kx - 0.07, ky + 0.02, kz + 0.19), (kx - 0.03, ky + 0.02, kz + 0.29),
                              (kx + 0.05, ky + 0.02, kz + 0.29), (kx + 0.09, ky + 0.02, kz + 0.18)],
                  [0.016, 0.016, 0.016, 0.016], dark))
    return o


def cookie_tin(v):
    col = TINS[v]
    body = pm('kt_tin%d' % v, col, rough=0.25, spec=0.5, cc=0.7)
    lid = pm('kt_tinlid%d' % v, tuple(min(1, c * 1.08) for c in col), rough=0.22, spec=0.5, cc=0.8)
    band = pm('kt_tinband', (0.97, 0.95, 0.90), rough=0.3, spec=0.5, cc=0.6)
    cookie = pm('kt_cookie', (0.86, 0.58, 0.30), rough=0.8, tex=L.tex_tnoise((0.95, 0.70, 0.40), f=18, bump=0.3))
    choc = pm('kt_choc', (0.22, 0.11, 0.06), rough=0.5)
    c = (0.5, 0.5)
    o = [cone('body', (c[0], c[1], 0.0), 0.31, 0.31, 0.38, body, bevel=0.015, verts=48),
         cone('band', (c[0], c[1], 0.13), 0.314, 0.314, 0.08, band, verts=48),
         cone('lid', (c[0], c[1], 0.36), 0.325, 0.325, 0.08, lid, bevel=0.025, verts=48)]
    # the cookie on the lid: the tin's emblem, seen from above
    o.append(cone('ck', (c[0], c[1] - 0.02, 0.44), 0.17, 0.165, 0.025, cookie, bevel=0.01, verts=40))
    rnd = random.Random(40 + v)
    for k, (dx, dy) in enumerate(((-0.07, 0.05), (0.06, 0.07), (0.0, -0.02), (-0.06, -0.08), (0.08, -0.05), (0.02, 0.11))):
        o.append(ellipsoid('ch%d' % k, (c[0] + dx, c[1] - 0.02 + dy, 0.466), (0.024, 0.022, 0.012), choc,
                           rot=(0, 0, rnd.uniform(0, 180)), seg=12, rings=8))
    # dots on the band in the tin's colour
    for k in range(16):
        a = 2 * math.pi * (k + 0.5) / 16
        o.append(ellipsoid('dt%d' % k, (c[0] + 0.316 * math.cos(a), c[1] + 0.316 * math.sin(a), 0.17),
                           (0.018, 0.018, 0.018), body, seg=12, rings=8))
    return o


def kitchen_target():
    mat = pm('kt_mat', (0.96, 0.42, 0.28), rough=0.85, sheen=0.6)
    cream = pm('kt_matfish', (1.0, 0.93, 0.80), rough=0.85)
    dark = pm('kt_mateye', (0.25, 0.10, 0.08), rough=0.8)
    o = []
    n = 96
    pts = [(0.5 + 0.43 * (1 + 0.035 * math.cos(16 * TA)) * math.cos(TA), 0.5 + 0.43 * (1 + 0.035 * math.cos(16 * TA)) * math.sin(TA))
           for TA in (2 * math.pi * i / n for i in range(n))]
    o.append(prism('mat', pts, 0.0, 0.014, mat, bevel=0.004, smooth=True))
    o.append(torus('stitch', (0.5, 0.5, 0.014), 0.365, 0.006, cream, n=64, m=6, scale=(1, 1, 0.5)))
    # a fish facing left: body, tail, eye, a fin
    fb = [(0.46 + 0.20 * math.cos(a), 0.50 + 0.11 * math.sin(a) * (1 - 0.25 * math.cos(a)))
          for a in (2 * math.pi * i / 48 for i in range(48))]
    o.append(prism('fish', fb, 0.014, 0.020, cream, smooth=True))
    o.append(prism('tail', [(0.63, 0.50), (0.78, 0.40), (0.75, 0.50), (0.78, 0.60)], 0.014, 0.020, cream))
    o.append(ellipsoid('eye', (0.34, 0.52, 0.021), (0.022, 0.022, 0.003), dark, seg=16, rings=8))
    o.append(tube('gill', [(0.41, 0.44, 0.021), (0.43, 0.50, 0.021), (0.41, 0.56, 0.021)], [0.006, 0.006, 0.006], mat, res=4, bev=2))
    return o


# ===========================================================================
# GARDEN
# ===========================================================================
GRASS = [(0.24, 0.48, 0.13), (0.34, 0.60, 0.17), (0.44, 0.70, 0.22), (0.56, 0.78, 0.30)]
HEDGE_TOP = [(0.025, 0.12, 0.045), (0.05, 0.20, 0.07), (0.09, 0.30, 0.10), (0.17, 0.42, 0.15)]
HEDGE_FRONT = [(0.05, 0.20, 0.07), (0.10, 0.32, 0.11), (0.17, 0.44, 0.16), (0.28, 0.58, 0.23)]
FLOWERS = [(0.92, 0.16, 0.20), (1.0, 0.80, 0.12), (0.62, 0.40, 0.92), (0.28, 0.52, 0.98)]


def grass_mat():
    return pm('gd_grass', GRASS[1], rough=0.85, spec=0.2, tex=L.tex_leaves(GRASS, f=34, bump=0.5, dist=0.01, fine=90))


def stone_mat(key='gd_stone', col=(0.50, 0.48, 0.45)):
    return pm(key, col, rough=0.75, spec=0.3, tex=L.tex_tnoise(tuple(c * 1.12 for c in col), f=14, bump=0.25, seed=4.0))


def garden_floor(v):
    soil = pm('gd_soil', (0.22, 0.14, 0.08), rough=1.0)
    g = grass_mat()
    o = [apron('ap', soil), box('turf', 0.003, 0.003, -0.06, 0.997, 0.997, 0.0, g, bevel=0.025)]
    if v == 1:
        st = stone_mat()
        c = (0.46, 0.52)
        # flush with the grass (top at z 0.01): targets and plates cover it
        o.append(disc('st', (c[0], c[1], -0.02), 0.23, 0.03, st, jitter=0.09, seed=v, bevel=0.012))
    if v == 2:
        o += daisies(7, 21)
    if v == 3:
        o += tufts(5, 31)
    return o


def tufts(n, seed):
    """Little clumps of longer grass (kept inside the cell, low)."""
    gm = pm('gd_tuft', GRASS[2], rough=0.7, spec=0.3)
    gm2 = pm('gd_tuft2', GRASS[3], rough=0.7, spec=0.3)
    rnd = random.Random(seed)
    o = []
    for k in range(n):
        x, y = rnd.uniform(0.18, 0.82), rnd.uniform(0.18, 0.82)
        for j in range(6):
            a = 2 * math.pi * j / 6 + rnd.uniform(-0.3, 0.3)
            o.append(cone('tf%d%d' % (k, j), (x + 0.012 * math.cos(a), y + 0.012 * math.sin(a), 0.0), 0.014, 0.0,
                          rnd.uniform(0.05, 0.08), gm if j % 2 else gm2, verts=6,
                          rot=(math.degrees(0.45 * math.sin(a)) * -1, math.degrees(0.45 * math.cos(a)), 0)))
    return o


def daisies(n, seed):
    wh = pm('gd_daisy', (0.98, 0.97, 0.93), rough=0.7)
    ye = pm('gd_daisyc', (1.0, 0.78, 0.15), rough=0.6)
    rnd = random.Random(seed)
    o = []
    for k in range(n):
        x, y = rnd.uniform(0.12, 0.88), rnd.uniform(0.12, 0.88)
        o.append(ellipsoid('dz%d' % k, (x, y, 0.004), (0.028, 0.028, 0.006), wh, seg=12, rings=6))
        o.append(ellipsoid('dc%d' % k, (x, y, 0.009), (0.011, 0.011, 0.005), ye, seg=10, rings=6))
    return o


def garden_wall(v):
    top = pm('gd_hedgetop', HEDGE_TOP[1], rough=0.65, spec=0.35,
             tex=L.tex_leaves(HEDGE_TOP, f=13, bump=1.2, dist=0.03, fine=38, seed=1.0))
    front = pm('gd_hedge', HEDGE_FRONT[1], rough=0.65, spec=0.35,
               tex=L.tex_leaves(HEDGE_FRONT, f=13, bump=1.2, dist=0.03, kz=13.0, fine=38, seed=2.0,
                                zshade=(0.0, FM, 0.55)))
    soil = pm('gd_bed', (0.30, 0.19, 0.10), rough=1.0, tex=L.tex_tnoise((0.40, 0.26, 0.14), f=30, bump=0.4))
    o = [box('bed', -E, -0.02, 0.0, 1 + E, 1, 0.035, soil),
         box('h', -E, 0.0, 0.02, 1 + E, 1 + E, FM - 0.03, front),
         box('t', -E, -0.008, FM - 0.035, 1 + E, 1 + E, FM, top)]
    if v == 1:
        o += hedge_flowers()
    return o


def hedge_flowers():
    pk = pm('gd_hflower', (0.98, 0.62, 0.80), rough=0.6)
    rnd = random.Random(8)
    o = []
    for k in range(9):
        x, y = rnd.uniform(0.08, 0.92), rnd.uniform(0.08, 0.92)
        o.append(ellipsoid('hf%d' % k, (x, y, FM + 0.004), (0.03, 0.03, 0.012), pk, seg=12, rings=6))
    for k in range(5):
        x, z = rnd.uniform(0.08, 0.92), rnd.uniform(0.12, 0.42)
        o.append(ellipsoid('hff%d' % k, (x, -0.006, z), (0.03, 0.01, 0.03), pk, seg=12, rings=6))
    return o


def gnome():
    moss = pm('gd_moss', (0.40, 0.64, 0.24), rough=0.9, tex=L.tex_tnoise((0.52, 0.74, 0.30), f=20, bump=0.4, seed=6.0))
    stone = stone_mat('gd_plinth', (0.60, 0.58, 0.54))
    coat = pm('gd_coat', (0.22, 0.44, 0.85), rough=0.6, sheen=0.4)
    hat = pm('gd_hat', (0.90, 0.16, 0.16), rough=0.5, spec=0.4, cc=0.3)
    skin = pm('gd_skin', (0.98, 0.76, 0.62), rough=0.6)
    beard = pm('gd_beard', (0.97, 0.96, 0.93), rough=0.9, sheen=1.0)
    nose = pm('gd_nose', (0.96, 0.52, 0.48), rough=0.5)
    boot = pm('gd_boot', (0.28, 0.16, 0.08), rough=0.6)
    belt = pm('gd_belt', (0.25, 0.14, 0.07), rough=0.6)
    gold = pm('gd_gold', (1.0, 0.78, 0.25), rough=0.3, metal=1.0)
    eye = pm('gd_eye', (0.05, 0.04, 0.04), rough=0.3)
    cx, cy = 0.5, 0.55
    o = [disc('plinth', (0.5, 0.5, 0.0), 0.44, 0.07, stone, jitter=0.03, seed=3, bevel=0.02),
         disc('moss', (0.5, 0.5, 0.068), 0.40, 0.012, moss, jitter=0.06, seed=5)]
    z0 = 0.08
    for sx in (-1, 1):
        o.append(ellipsoid('boot%d' % sx, (cx + sx * 0.08, cy - 0.06, z0 + 0.04), (0.07, 0.10, 0.05), boot))
    o.append(cone('coat', (cx, cy, z0 + 0.02), 0.20, 0.13, 0.30, coat))
    o.append(torus('belt', (cx, cy, z0 + 0.15), 0.165, 0.022, belt, scale=(1, 1, 0.9)))
    o.append(box('buckle', cx - 0.035, cy - 0.19, z0 + 0.12, cx + 0.035, cy - 0.17, z0 + 0.18, gold, bevel=0.006))
    for sx in (-1, 1):
        o.append(ellipsoid('arm%d' % sx, (cx + sx * 0.16, cy - 0.04, z0 + 0.22), (0.05, 0.05, 0.11), coat, rot=(20, sx * -25, 0)))
        o.append(ellipsoid('hand%d' % sx, (cx + sx * 0.18, cy - 0.10, z0 + 0.14), (0.04, 0.04, 0.04), skin))
    hz = z0 + 0.42
    o.append(ellipsoid('head', (cx, cy, hz), (0.13, 0.12, 0.12), skin))
    o.append(ellipsoid('beard', (cx, cy - 0.07, hz - 0.08), (0.13, 0.08, 0.13), beard, rot=(-15, 0, 0)))
    o.append(ellipsoid('mst1', (cx - 0.05, cy - 0.12, hz - 0.03), (0.06, 0.03, 0.03), beard, rot=(0, 20, 0)))
    o.append(ellipsoid('mst2', (cx + 0.05, cy - 0.12, hz - 0.03), (0.06, 0.03, 0.03), beard, rot=(0, -20, 0)))
    o.append(ellipsoid('nose', (cx, cy - 0.14, hz + 0.0), (0.045, 0.04, 0.04), nose))
    for sx in (-1, 1):
        o.append(ellipsoid('eye%d' % sx, (cx + sx * 0.05, cy - 0.105, hz + 0.045), (0.016, 0.01, 0.02), eye, seg=12, rings=8))
    o.append(cone('hat', (cx, cy + 0.01, hz + 0.05), 0.15, 0.004, 0.36, hat, rot=(10, -8, 0)))
    o.append(torus('brim', (cx, cy + 0.01, hz + 0.055), 0.14, 0.03, hat, rot=(10, -8, 0)))
    # two toadstools and a few tufts on the plinth
    cap = pm('gd_toad', (0.92, 0.20, 0.18), rough=0.4, cc=0.4)
    stem = pm('gd_toadstem', (0.98, 0.95, 0.88), rough=0.7)
    for k, (tx, ty, s) in enumerate(((0.20, 0.30, 1.0), (0.80, 0.34, 0.8))):
        o.append(cone('ts%d' % k, (tx, ty, 0.07), 0.025 * s, 0.022 * s, 0.07 * s, stem))
        o.append(ellipsoid('tc%d' % k, (tx, ty, 0.07 + 0.07 * s), (0.07 * s, 0.07 * s, 0.045 * s), cap))
        for j in range(4):
            a = 2 * math.pi * j / 4 + k
            o.append(ellipsoid('td%d%d' % (k, j), (tx + 0.04 * s * math.cos(a), ty + 0.04 * s * math.sin(a), 0.07 + 0.105 * s),
                               (0.012 * s, 0.012 * s, 0.006 * s), stem, seg=8, rings=6))
    return o


def flower_bush():
    lv = pm('gd_bush', HEDGE_FRONT[2], rough=0.7, spec=0.3,
            tex=L.tex_leaves([(0.08, 0.28, 0.09), (0.15, 0.42, 0.14), (0.24, 0.56, 0.20), (0.36, 0.68, 0.28)],
                             f=26, bump=1.0, dist=0.02, kz=26.0, fine=60, seed=5.0))
    pk = pm('gd_bflower', (0.98, 0.50, 0.72), rough=0.6, spec=0.3)
    wh = pm('gd_bflower2', (1.0, 0.92, 0.95), rough=0.6)
    ye = pm('gd_daisyc', (1.0, 0.78, 0.15), rough=0.6)
    o = [ellipsoid('b0', (0.5, 0.53, 0.33), (0.43, 0.40, 0.34), lv),
         ellipsoid('b1', (0.30, 0.40, 0.25), (0.22, 0.22, 0.24), lv),
         ellipsoid('b2', (0.70, 0.40, 0.25), (0.22, 0.22, 0.24), lv),
         ellipsoid('b3', (0.50, 0.30, 0.22), (0.26, 0.22, 0.22), lv),
         ellipsoid('b4', (0.42, 0.62, 0.55), (0.24, 0.22, 0.18), lv),
         ellipsoid('b5', (0.62, 0.58, 0.52), (0.22, 0.22, 0.18), lv)]
    # flowers on the visible surface (top and front), sampled on the dome
    rnd = random.Random(12)
    k = 0
    tries = 0
    while k < 26 and tries < 500:
        tries += 1
        th = rnd.uniform(0, 2 * math.pi)
        ph = rnd.uniform(0.05, 1.35)
        nx, ny, nz = math.sin(ph) * math.cos(th), math.sin(ph) * math.sin(th), math.cos(ph)
        if ny > 0.55:          # hidden behind the top
            continue
        x, y, z = 0.5 + 0.44 * nx, 0.53 + 0.41 * ny, 0.33 + 0.36 * nz
        if z < 0.10:
            continue
        m = pk if k % 4 else wh
        o.append(ellipsoid('fl%d' % k, (x, y, z), (0.036, 0.036, 0.024), m, seg=12, rings=8))
        o.append(ellipsoid('fc%d' % k, (x + nx * 0.012, y + ny * 0.012, z + nz * 0.018), (0.012, 0.012, 0.01), ye, seg=8, rings=6))
        k += 1
    return o


def flower_pot(v):
    terra = pm('gd_terra', (0.82, 0.40, 0.22), rough=0.65, spec=0.3)
    band = pm('gd_band%d' % v, FLOWERS[v], rough=0.4, spec=0.4, cc=0.4)
    soil = pm('gd_potsoil', (0.22, 0.13, 0.07), rough=1.0)
    stem = pm('gd_stem', (0.22, 0.55, 0.18), rough=0.6)
    leaf = pm('gd_leaf', (0.26, 0.62, 0.22), rough=0.5, spec=0.4)
    fl = pm('gd_flower%d' % v, FLOWERS[v], rough=0.45, spec=0.4, sheen=0.3)
    c = (0.5, 0.5)
    o = [cone('pot', (c[0], c[1], 0.0), 0.18, 0.265, 0.34, terra, verts=40),
         cone('lip', (c[0], c[1], 0.30), 0.30, 0.30, 0.10, terra, bevel=0.025, verts=40),
         cone('band', (c[0], c[1], 0.325), 0.304, 0.304, 0.05, band, verts=40),
         cone('soil', (c[0], c[1], 0.35), 0.265, 0.265, 0.03, soil, verts=40)]
    rnd = random.Random(90 + v)
    S0 = 0.37           # the soil's top
    if v == 0:          # tulips
        heads = [(-0.11, -0.06, 0.64), (0.10, -0.08, 0.62), (0.0, 0.07, 0.71), (-0.13, 0.11, 0.62), (0.13, 0.08, 0.67)]
        for k in range(5):
            a = 2 * math.pi * k / 5 + 0.3
            o.append(ellipsoid('lf%d' % k, (c[0] + 0.13 * math.cos(a), c[1] + 0.13 * math.sin(a), S0 + 0.10),
                               (0.045, 0.17, 0.014), leaf, rot=(58, 0, math.degrees(a) - 90), seg=16, rings=8))
        for k, (dx, dy, h) in enumerate(heads):
            top = (c[0] + dx, c[1] + dy, h)
            o.append(tube('st%d' % k, [(c[0] + dx * 0.4, c[1] + dy * 0.4, S0), (c[0] + dx * 0.85, c[1] + dy * 0.85, S0 + 0.14), top],
                          [0.014, 0.013, 0.012], stem, res=6, bev=3))
            for j in range(3):
                a = 2 * math.pi * j / 3 + k
                o.append(ellipsoid('pt%d%d' % (k, j), (top[0] + 0.026 * math.cos(a), top[1] + 0.026 * math.sin(a), top[2] + 0.03),
                                   (0.052, 0.045, 0.092), fl, rot=(0, 0, math.degrees(a)), seg=16, rings=10))
    elif v == 3:        # blue hydrangea: three round heads of little florets
        fl2 = pm('gd_flower3b', (0.62, 0.78, 1.0), rough=0.45, spec=0.4)
        for k in range(4):
            a = 2 * math.pi * k / 4 + 0.6
            o.append(ellipsoid('lf%d' % k, (c[0] + 0.18 * math.cos(a), c[1] + 0.18 * math.sin(a), S0 + 0.06),
                               (0.08, 0.14, 0.016), leaf, rot=(35, 0, math.degrees(a) - 90), seg=16, rings=8))
        for k, (dx, dy, h, r) in enumerate(((-0.11, -0.06, 0.56, 0.13), (0.12, -0.05, 0.57, 0.12), (0.0, 0.09, 0.62, 0.13))):
            hc = (c[0] + dx, c[1] + dy, h)
            o.append(ellipsoid('hd%d' % k, hc, (r * 0.9, r * 0.9, r * 0.75), fl))
            for j in range(16):
                th = 2 * math.pi * rnd.random()
                ph = math.acos(rnd.uniform(0.0, 1.0))
                n = (math.sin(ph) * math.cos(th), math.sin(ph) * math.sin(th), math.cos(ph))
                o.append(ellipsoid('fz%d%d' % (k, j), (hc[0] + n[0] * r * 0.85, hc[1] + n[1] * r * 0.85, hc[2] + n[2] * r * 0.7),
                                   (0.035, 0.035, 0.03), fl if j % 3 else fl2, seg=10, rings=6))
    else:               # daisy-like heads facing up and a little to the front
        cen = pm('gd_centre%d' % v, ((0.45, 0.25, 0.10) if v == 1 else (1.0, 0.82, 0.25)), rough=0.7)
        heads = [(-0.11, -0.07, 0.62), (0.12, -0.04, 0.65), (0.0, 0.10, 0.71)] if v == 1 else \
                [(-0.12, -0.08, 0.58), (0.13, -0.08, 0.60), (0.0, 0.0, 0.68), (-0.10, 0.11, 0.63), (0.12, 0.10, 0.62)]
        rr = 0.115 if v == 1 else 0.085
        npet = 12 if v == 1 else 6
        for k in range(4):
            a = 2 * math.pi * k / 4 + 0.6
            o.append(ellipsoid('lf%d' % k, (c[0] + 0.17 * math.cos(a), c[1] + 0.17 * math.sin(a), S0 + 0.05),
                               (0.06, 0.13, 0.014), leaf, rot=(40, 0, math.degrees(a) - 90), seg=16, rings=8))
        for k, (dx, dy, h) in enumerate(heads):
            top = (c[0] + dx, c[1] + dy, h)
            o.append(tube('st%d' % k, [(c[0] + dx * 0.3, c[1] + dy * 0.3, S0), (c[0] + dx * 0.85, c[1] + dy * 0.85, S0 + 0.14), top],
                          [0.014, 0.013, 0.012], stem, res=6, bev=3))
            parts = [ellipsoid('ce%d' % k, (0, 0, 0.014), (rr * 0.42, rr * 0.42, 0.026), cen, seg=16, rings=8)]
            for j in range(npet):
                a = 2 * math.pi * j / npet
                pw = 0.2 if v == 1 else 0.36
                parts.append(ellipsoid('p%d%d' % (k, j), (rr * 0.70 * math.cos(a), rr * 0.70 * math.sin(a), 0.0),
                                       (rr * 0.52, rr * pw, 0.014), fl, rot=(0, 0, math.degrees(a)), seg=12, rings=6))
            root = bpy.data.objects.new('head%d' % k, None)
            C.link(root)
            for p in parts:
                p.parent = root
            root.location = top
            root.rotation_euler = (math.radians(-35), 0, math.radians(rnd.uniform(-20, 20)))
            o += parts
    return o


def garden_target():
    soil = pm('gd_tsoil', (0.34, 0.21, 0.11), rough=0.95,
              tex=L.tex_tnoise((0.44, 0.29, 0.16), f=26, bump=0.8, dist=0.015, seed=9.0))
    peb = [pm('gd_peb%d' % k, col, rough=0.5, spec=0.4) for k, col in
           enumerate(((0.96, 0.95, 0.92), (0.88, 0.86, 0.82), (0.98, 0.93, 0.84)))]
    sprout = pm('gd_sprout', (0.40, 0.80, 0.25), rough=0.5, spec=0.4)
    o = [disc('soil', (0.5, 0.5, -0.005), 0.37, 0.028, soil, jitter=0.03, seed=2, bevel=0.012)]
    n = 17
    rnd = random.Random(33)
    for k in range(n):
        a = 2 * math.pi * k / n + rnd.uniform(-0.05, 0.05)
        r = 0.41 + rnd.uniform(-0.01, 0.01)
        s = rnd.uniform(0.85, 1.1)
        o.append(ellipsoid('pb%d' % k, (0.5 + r * math.cos(a), 0.5 + r * math.sin(a), 0.018),
                           (0.058 * s, 0.047 * s, 0.03 * s), peb[k % 3], rot=(0, 0, math.degrees(a) + 90), seg=16, rings=10))
    o.append(tube('spst', [(0.5, 0.5, 0.02), (0.5, 0.5, 0.06), (0.505, 0.5, 0.09)], [0.008, 0.008, 0.007], sprout, res=4, bev=2))
    for sx in (-1, 1):
        o.append(ellipsoid('spl%d' % sx, (0.5 + sx * 0.035, 0.5, 0.10), (0.04, 0.02, 0.008), sprout, rot=(0, sx * -25, 0), seg=12, rings=6))
    return o


def garden_plate(down):
    stone = stone_mat('gd_platestone', (0.58, 0.56, 0.53))
    ring = pm('gd_platering', (0.50, 0.49, 0.47), rough=0.8)
    if down:
        btn = pm('gd_btn_dn', (0.36, 0.86, 0.40), rough=0.35, spec=0.5, cc=0.5, emit=(0.30, 0.95, 0.35), es=0.35)
    else:
        btn = pm('gd_btn_up', (0.95, 0.36, 0.30), rough=0.35, spec=0.5, cc=0.5)
    cream = pm('gd_btnflower', (1.0, 0.96, 0.86), rough=0.6)
    ye = pm('gd_btncentre', (1.0, 0.80, 0.20), rough=0.5)
    o = [cone('base', (0.5, 0.5, -0.01), 0.37, 0.35, 0.045, stone, bevel=0.012, verts=48),
         cone('well', (0.5, 0.5, 0.030), 0.255, 0.255, 0.006, ring, verts=48)]
    top = 0.050 if down else 0.105
    o.append(cone('btn', (0.5, 0.5, 0.02), 0.235, 0.225, top - 0.02, btn, bevel=0.022 if not down else 0.01, verts=48))
    for j in range(5):
        a = 2 * math.pi * j / 5 + math.pi / 2
        o.append(ellipsoid('pe%d' % j, (0.5 + 0.075 * math.cos(a), 0.5 + 0.075 * math.sin(a), top),
                           (0.065, 0.042, 0.006), cream, rot=(0, 0, math.degrees(a)), seg=16, rings=6))
    o.append(ellipsoid('pc', (0.5, 0.5, top + 0.003), (0.04, 0.04, 0.007), ye, seg=16, rings=6))
    return o


def garden_gate(axis, frame):
    """A little white picket gate between two posts, in a hedge line.
    h: the hedge runs left-right, Mila crosses up-down; the leaves swing back.
    v: the hedge runs up-down (the camera sees the gate edge-on), so it is
    chunkier: thick pickets under a wide top rail that reads from above;
    the leaves swing to the left. Frames 00 closed .. 03 open."""
    paint = pm('gd_gate', (0.95, 0.93, 0.87), rough=0.5, spec=0.35)
    post = pm('gd_post', (0.93, 0.90, 0.83), rough=0.5, spec=0.35)
    iron = pm('gd_hinge', (0.25, 0.25, 0.27), rough=0.4, metal=0.6)
    ang = (0.0, 32.0, 62.0, 88.0)[frame]
    chunky = axis == 'v'
    th = 0.10 if chunky else 0.05           # picket thickness (across the gate)
    ps = 0.16 if chunky else 0.12           # post size
    o = []
    for px in (0.02, 0.98 - ps):
        o.append(rbox('post%.2f' % px, px, 0.5 - ps / 2, 0.0, px + ps, 0.5 + ps / 2, 0.58, post, 0.02))
        o.append(ellipsoid('fin%.2f' % px, (px + ps / 2, 0.50, 0.62), (ps * 0.42, ps * 0.42, 0.05), post))
    for side in (-1, 1):
        hx = 0.02 + ps if side < 0 else 0.98 - ps          # hinge x
        leaf = []
        w = 0.5 - (0.02 + ps) - 0.006
        n = 3
        pw = (w - 0.02) / n - 0.025
        for k in range(n):
            u0 = 0.02 + k * (w - 0.02) / n
            u1 = u0 + pw
            x0, x1 = (hx + u0, hx + u1) if side < 0 else (hx - u1, hx - u0)
            if chunky:
                leaf.append(box('pk%d%d' % (side, k), x0, 0.5 - th / 2, 0.05, x1, 0.5 + th / 2, 0.42, paint, bevel=0.012))
            else:
                h = 0.40 + 0.05 * (k / 2.0)
                leaf.append(box('pk%d%d' % (side, k), x0, 0.5 - th / 2, 0.05, x1, 0.5 + th / 2, h, paint, bevel=0.01))
                leaf.append(ellipsoid('pkc%d%d' % (side, k), ((x0 + x1) / 2, 0.50, h), (pw / 2, th / 2, 0.045), paint,
                                      seg=16, rings=8))
        x0, x1 = (hx + 0.005, hx + w) if side < 0 else (hx - w, hx - 0.005)
        if chunky:
            # a wide rounded top rail and a low rail
            leaf.append(rbox('top%d' % side, x0, 0.5 - 0.075, 0.41, x1, 0.5 + 0.075, 0.47, paint, 0.02))
            leaf.append(box('rl%d' % side, x0, 0.5 + th / 2, 0.12, x1, 0.5 + th / 2 + 0.03, 0.17, paint, bevel=0.008))
            hy = 0.5 + th / 2
        else:
            for z in (0.13, 0.33):
                leaf.append(box('rl%d%.2f' % (side, z), x0, 0.5 + th / 2, z, x1, 0.5 + th / 2 + 0.03, z + 0.05, paint,
                                bevel=0.008))
            hy = 0.5 + th / 2
        for z in (0.15, 0.35):
            hx0 = hx - 0.02 if side < 0 else hx - 0.03
            leaf.append(box('hg%d%.2f' % (side, z), hx0, hy - 0.005, z - 0.005, hx0 + 0.05, hy + 0.035, z + 0.035, iron))
        if ang:
            L.rotate_group(leaf, (hx, hy + 0.015, 0.0), ang if side < 0 else -ang)
        o += leaf
    if axis == 'v':
        L.rotate_group(o, (0.5, 0.5, 0.0), 90)
    return o


# ---------------------------------------------------------------------------
# phase 2: the rest of the furniture
# ---------------------------------------------------------------------------

def living_plant():
    """sample.py's potted plant."""
    pot = pm('lv_pot', (0.80, 0.42, 0.28), rough=0.6)
    soil = pm('lv_soil', (0.2, 0.12, 0.07), rough=1)
    leaf = pm('lv_leaf', (0.22, 0.62, 0.30), rough=0.5, spec=0.4)
    leaf2 = pm('lv_leaf2', (0.32, 0.72, 0.36), rough=0.5, spec=0.4)
    c = (0.5, 0.5)
    o = [cone('pot', (c[0], c[1], 0), 0.32, 0.42, 0.46, pot),
         torus('lip', (c[0], c[1], 0.46), 0.41, 0.05, pot),
         ellipsoid('soil', (c[0], c[1], 0.44), (0.38, 0.38, 0.03), soil)]
    rnd = random.Random(5)
    for k in range(9):
        a = 2 * math.pi * k / 9 + rnd.uniform(-0.2, 0.2)
        r = rnd.uniform(0.2, 0.34)
        o.append(ellipsoid('lf%d' % k, (c[0] + r * math.cos(a), c[1] + r * math.sin(a), 0.66 + rnd.uniform(0, 0.25)),
                           (0.28, 0.1, 0.04), leaf if k % 2 else leaf2, rot=(0, rnd.uniform(-40, -10), math.degrees(a))))
    return o


def living_bookcase():
    """sample.py's low bookcase, deeper so it fills the cell."""
    wd = wood('lv_shelf', (0.55, 0.34, 0.20))
    dark = pm('lv_shelfin', (0.20, 0.12, 0.07), rough=0.9)
    cols = [(0.85, 0.2, 0.2), (0.2, 0.5, 0.9), (0.95, 0.75, 0.2), (0.3, 0.7, 0.4), (0.7, 0.4, 0.8), (0.95, 0.5, 0.3)]
    o = [box('back', 0.06, 0.42, 0.06, 0.94, 0.94, 0.845, wd),
         box('in', 0.10, 0.38, 0.06, 0.90, 0.425, 0.845, dark),
         box('top', 0.04, 0.08, 0.84, 0.96, 0.95, 0.90, wd, bevel=0.012),
         box('bot', 0.04, 0.08, 0.0, 0.96, 0.95, 0.07, wd, bevel=0.01),
         box('mid', 0.06, 0.10, 0.44, 0.94, 0.45, 0.48, wd, bevel=0.006)]
    for sx in (0.04, 0.90):
        o.append(box('side%.2f' % sx, sx, 0.08, 0.0, sx + 0.06, 0.95, 0.85, wd, bevel=0.01))
    rnd = random.Random(9)
    for sh, z in enumerate((0.07, 0.48)):
        xx = 0.12
        while xx < 0.84:
            wdt = rnd.uniform(0.05, 0.09)
            hgt = rnd.uniform(0.26, 0.34)
            m = pm('lv_bk%d' % rnd.randrange(6), cols[rnd.randrange(6)], rough=0.6)
            o.append(box('b%d%.2f' % (sh, xx), xx, 0.14, z, xx + wdt, 0.40, z + hgt, m, bevel=0.004))
            xx += wdt + 0.008
    # a little cactus on top
    o.append(cone('cpot', (0.75, 0.62, 0.90), 0.06, 0.075, 0.08, pm('lv_pot', (0.80, 0.42, 0.28))))
    o.append(ellipsoid('cact', (0.75, 0.62, 1.0 - 0.03), (0.045, 0.045, 0.07), pm('lv_cactus', (0.35, 0.68, 0.40), rough=0.5)))
    return o


def living_record_player():
    wd = wood('lv_cab', (0.66, 0.44, 0.25))
    panel = wood('lv_cabdoor', (0.72, 0.50, 0.30), offset=(3, 1, 2))
    brass = pm('lv_brass', (0.9, 0.7, 0.35), rough=0.3, metal=1.0)
    case = pm('lv_rpcase', (0.93, 0.86, 0.72), rough=0.45, spec=0.4, cc=0.3)
    teal = pm('lv_rptrim', (0.30, 0.62, 0.64), rough=0.45)
    vinyl = pm('lv_vinyl', (0.04, 0.04, 0.05), rough=0.25, spec=0.6, cc=0.6)
    label = pm('lv_label', (0.92, 0.28, 0.30), rough=0.5)
    chrome = pm('kt_chrome', (0.85, 0.86, 0.9), rough=0.15, metal=1.0)
    H = 0.46
    o = [box('cab', 0.06, 0.08, 0.07, 0.94, 0.94, H, wd, bevel=0.02)]
    for lx in (0.10, 0.84):
        for ly in (0.12, 0.84):
            o.append(cone('lg%.2f%.2f' % (lx, ly), (lx + 0.03, ly + 0.03, 0), 0.025, 0.02, 0.08, wd))
    for k, (x0, x1) in enumerate(((0.10, 0.49), (0.51, 0.90))):
        o.append(box('door%d' % k, x0, 0.065, 0.11, x1, 0.08, H - 0.05, panel, bevel=0.006))
        kx = x1 - 0.05 if k == 0 else x0 + 0.05
        o.append(ellipsoid('knob%d' % k, (kx, 0.055, 0.28), (0.018, 0.015, 0.018), brass, seg=12, rings=8))
    # the record player
    px0, py0, px1, py1 = 0.16, 0.18, 0.84, 0.80
    o.append(rbox('case', px0, py0, H, px1, py1, H + 0.10, case, 0.025))
    o.append(box('trim', px0 + 0.01, py0 - 0.002, H + 0.02, px1 - 0.01, py0 + 0.01, H + 0.045, teal))
    o.append(cone('platter', (0.43, 0.50, H + 0.10), 0.22, 0.22, 0.012, chrome, verts=48))
    o.append(cone('disc', (0.43, 0.50, H + 0.11), 0.21, 0.21, 0.008, vinyl, verts=48))
    o.append(torus('groove', (0.43, 0.50, H + 0.118), 0.14, 0.004, pm('lv_groove', (0.18, 0.18, 0.2), rough=0.3), m=6))
    o.append(cone('lab', (0.43, 0.50, H + 0.118), 0.07, 0.07, 0.004, label, verts=32))
    o.append(cone('arm0', (0.74, 0.68, H + 0.10), 0.03, 0.03, 0.04, chrome))
    o.append(tube('arm', [(0.74, 0.68, H + 0.15), (0.72, 0.52, H + 0.15), (0.62, 0.40, H + 0.135)], [0.009, 0.009, 0.009], chrome, res=6, bev=3))
    o.append(box('head', 0.59, 0.37, H + 0.12, 0.64, 0.42, H + 0.145, pm('kt_black', (0.03, 0.03, 0.04))))
    for k, kx in enumerate((0.70, 0.78)):
        o.append(cone('kn%d' % k, (kx, 0.26, H + 0.10), 0.022, 0.02, 0.02, teal))
    return o


def living_sofa():
    """sample.py's 2-cell sofa, cells (0, 0) and (1, 0)."""
    fab = pm('lv_sofa', (0.85, 0.36, 0.30), rough=0.85, sheen=0.8)
    fab2 = pm('lv_sofa2', (0.93, 0.48, 0.40), rough=0.85, sheen=0.8)
    leg = wood('lv_sofaleg', (0.35, 0.22, 0.12))
    pil = pm('lv_pillow', (0.98, 0.84, 0.45), rough=0.9, sheen=1)
    x0, y0, x1, y1 = 0.02, 0.05, 1.98, 0.95
    o = []
    for lx in (x0 + 0.1, x1 - 0.15):
        for ly in (y0 + 0.12, y1 - 0.17):
            o.append(box('lg%.1f%.1f' % (lx, ly), lx, ly, 0, lx + 0.05, ly + 0.05, 0.1, leg))
    o.append(box('base', x0 + 0.03, y0 + 0.03, 0.1, x1 - 0.03, y1 - 0.03, 0.32, fab, bevel=0.06))
    o.append(box('back', x0 + 0.05, y1 - 0.30, 0.3, x1 - 0.05, y1 - 0.06, 0.72, fab, bevel=0.07))
    for sx in (x0 + 0.05, x1 - 0.2):
        o.append(box('arm%.2f' % sx, sx, y0 + 0.08, 0.3, sx + 0.15, y1 - 0.06, 0.5, fab, bevel=0.06))
    for k in range(2):
        cx = x0 + 0.25 + k * 0.72 + 0.04
        o.append(box('cu%d' % k, cx, y0 + 0.1, 0.3, cx + 0.64, y1 - 0.3, 0.42, fab2, bevel=0.06))
    o.append(ellipsoid('pil', (x0 + 0.42, y1 - 0.36, 0.55), (0.15, 0.06, 0.13), pil, rot=(-15, 0, 10)))
    return o


def kitchen_cabinet():
    body = pm('kt_cabinet', (0.45, 0.72, 0.68), rough=0.4, spec=0.45, cc=0.3)
    top = pm('kt_worktop', (0.27, 0.155, 0.08), rough=0.5, spec=0.35, cc=0.2,
             tex=L.tex_twood(fx=1.0, fy=10.0, dark=0.8, seed=2.0))
    chrome = pm('kt_chrome', (0.85, 0.86, 0.9), rough=0.15, metal=1.0)
    bowl = pm('kt_bowl', (0.97, 0.96, 0.93), rough=0.2, spec=0.5, cc=0.6)
    bowlband = pm('kt_bowlband', (0.25, 0.48, 0.88), rough=0.3)
    orange = pm('kt_orange', (1.0, 0.52, 0.10), rough=0.5, spec=0.4)
    apple = pm('kt_apple', (0.86, 0.14, 0.16), rough=0.35, spec=0.5, cc=0.4)
    banana = pm('kt_banana', (1.0, 0.85, 0.25), rough=0.5)
    stem = pm('kt_stem', (0.35, 0.22, 0.10), rough=0.8)
    lf = pm('kt_leafy', (0.30, 0.66, 0.26), rough=0.5)
    H = 0.52
    o = [rbox('body', 0.07, 0.10, 0.05, 0.93, 0.92, H - 0.04, body, 0.03),
         box('plinth', 0.10, 0.14, 0.0, 0.90, 0.88, 0.06, pm('kt_burner', (0.10, 0.10, 0.11))),
         box('top', 0.05, 0.07, H - 0.045, 0.95, 0.95, H, top, bevel=0.008)]
    o.append(rbox('drawer', 0.12, 0.085, H - 0.16, 0.88, 0.11, H - 0.07, body, 0.012))
    for k, (x0, x1) in enumerate(((0.12, 0.49), (0.51, 0.88))):
        o.append(rbox('door%d' % k, x0, 0.085, 0.09, x1, 0.11, H - 0.19, body, 0.012))
        kx = x1 - 0.06 if k == 0 else x0 + 0.06
        o.append(rbox('h%d' % k, kx - 0.012, 0.065, 0.20, kx + 0.012, 0.09, 0.28, chrome, 0.008))
    o.append(rbox('hd', 0.42, 0.065, H - 0.125, 0.58, 0.09, H - 0.105, chrome, 0.008))
    # the fruit bowl
    bx, by, bz = 0.50, 0.52, H
    o.append(cone('bowl', (bx, by, bz), 0.15, 0.27, 0.13, bowl, verts=40))
    o.append(torus('bband', (bx, by, bz + 0.09), 0.245, 0.012, bowlband))
    fr = [((-0.09, 0.02, 0.17), 0.085, orange), ((0.08, 0.05, 0.17), 0.085, orange), ((0.0, -0.08, 0.16), 0.08, apple),
          ((-0.01, 0.10, 0.22), 0.08, apple), ((0.10, -0.06, 0.20), 0.075, orange)]
    for k, ((dx, dy, dz), r, m) in enumerate(fr):
        o.append(ellipsoid('fr%d' % k, (bx + dx, by + dy, bz + dz), (r, r, r * 0.95), m))
        if m == apple:
            o.append(tube('stm%d' % k, [(bx + dx, by + dy, bz + dz + r * 0.8), (bx + dx + 0.01, by + dy, bz + dz + r + 0.03)],
                          [0.007, 0.006], stem, res=2, bev=2))
            o.append(ellipsoid('al%d' % k, (bx + dx + 0.03, by + dy, bz + dz + r + 0.02), (0.03, 0.012, 0.008), lf, rot=(0, -20, 0)))
    o.append(tube('banana', [(bx - 0.17, by - 0.05, bz + 0.20), (bx - 0.05, by - 0.12, bz + 0.25), (bx + 0.09, by - 0.10, bz + 0.25),
                             (bx + 0.18, by - 0.02, bz + 0.22)], [0.03, 0.04, 0.04, 0.028], banana, res=8))
    return o


def kitchen_stool():
    """A stool on a round rag rug (the rug makes the cell read as taken)."""
    rug = pm('kt_rug', (0.30, 0.52, 0.86), rough=1.0, sheen=1.0)
    rug2 = pm('kt_rug2', (0.97, 0.93, 0.85), rough=1.0, sheen=1.0)
    wd = wood('kt_stoolwood', (0.72, 0.48, 0.26))
    seat = pm('kt_seat', (0.92, 0.28, 0.30), rough=0.6, sheen=0.5)
    o = []
    for k, (r, m) in enumerate(((0.44, rug), (0.36, rug2), (0.28, rug), (0.20, rug2))):
        o.append(cone('rug%d' % k, (0.5, 0.5, 0.0), r, r, 0.012 + 0.002 * k, m, verts=48, bevel=0.004))
    c = (0.5, 0.52)
    for k in range(4):
        a = math.pi / 4 + k * math.pi / 2
        bx, by = c[0] + 0.22 * math.cos(a), c[1] + 0.22 * math.sin(a)
        tx, ty = c[0] + 0.15 * math.cos(a), c[1] + 0.15 * math.sin(a)
        o.append(tube('leg%d' % k, [(bx, by, 0.02), (tx, ty, 0.46)], [0.024, 0.02], wd, res=2, bev=3))
    o.append(torus('ring', (c[0], c[1], 0.20), 0.19, 0.012, wd))
    o.append(cone('seatw', (c[0], c[1], 0.44), 0.21, 0.21, 0.05, wd, bevel=0.012))
    o.append(ellipsoid('cush', (c[0], c[1], 0.50), (0.20, 0.20, 0.045), seat))
    # a little milk bottle on the rug
    glass = pm('kt_milk', (0.97, 0.97, 0.95), rough=0.2, spec=0.5, cc=0.6)
    cap = pm('kt_cap', (0.25, 0.48, 0.88), rough=0.4)
    o.append(cone('milk', (0.20, 0.30, 0.01), 0.045, 0.045, 0.14, glass, bevel=0.01))
    o.append(cone('milkn', (0.20, 0.30, 0.15), 0.045, 0.025, 0.05, glass))
    o.append(cone('milkc', (0.20, 0.30, 0.195), 0.028, 0.028, 0.02, cap))
    return o


def kitchen_bin():
    """A pedal bin next to a wooden crate of vegetables."""
    binm = pm('kt_bin', (0.98, 0.78, 0.30), rough=0.3, spec=0.5, cc=0.5)
    chrome = pm('kt_chrome', (0.85, 0.86, 0.9), rough=0.15, metal=1.0)
    dark = pm('kt_burner', (0.10, 0.10, 0.11))
    crate = wood('kt_crate', (0.78, 0.58, 0.34))
    o = [cone('bin', (0.30, 0.56, 0.0), 0.21, 0.23, 0.50, binm, bevel=0.015, verts=40),
         cone('lid', (0.30, 0.56, 0.50), 0.235, 0.22, 0.05, chrome, bevel=0.015, verts=40),
         ellipsoid('knob', (0.30, 0.56, 0.56), (0.05, 0.03, 0.02), dark),
         box('pedal', 0.24, 0.30, 0.0, 0.36, 0.36, 0.035, chrome, bevel=0.008)]
    cx0, cy0, cx1, cy1 = 0.52, 0.10, 0.95, 0.62
    for k, z in enumerate((0.02, 0.12, 0.22)):
        o.append(box('fs%d' % k, cx0, cy0, z, cx1, cy0 + 0.025, z + 0.07, crate, bevel=0.006))
        o.append(box('bs%d' % k, cx0, cy1 - 0.025, z, cx1, cy1, z + 0.07, crate, bevel=0.006))
    for x in (cx0, cx1 - 0.03):
        o.append(box('ps%.2f' % x, x, cy0, 0.0, x + 0.03, cy1, 0.30, crate, bevel=0.006))
    o.append(box('cb', cx0, cy0, 0.0, cx1, cy1, 0.03, crate))
    carrot = pm('kt_carrot', (1.0, 0.45, 0.08), rough=0.5)
    leafy = pm('kt_leafy', (0.30, 0.66, 0.26), rough=0.5)
    pot = pm('kt_potato', (0.80, 0.62, 0.38), rough=0.8)
    rnd = random.Random(4)
    for k in range(6):
        o.append(ellipsoid('po%d' % k, (rnd.uniform(cx0 + 0.08, cx1 - 0.08), rnd.uniform(cy0 + 0.08, cy1 - 0.08), 0.18),
                           (0.07, 0.055, 0.05), pot, rot=(0, 0, rnd.uniform(0, 180))))
    for k in range(4):
        x, y = cx0 + 0.09 + k * 0.09, cy0 + 0.12 + (k % 2) * 0.2
        o.append(cone('ca%d' % k, (x, y, 0.20), 0.03, 0.004, 0.2, carrot, rot=(80, 0, 20 + k * 10), verts=12))
        o.append(ellipsoid('cl%d' % k, (x, y + 0.02, 0.26), (0.03, 0.05, 0.06), leafy))
    return o


def kitchen_counter():
    """A 2-cell counter with a sink (right) and a dish rack (left)."""
    body = pm('kt_cabinet', (0.45, 0.72, 0.68), rough=0.4, spec=0.45, cc=0.3)
    top = pm('kt_worktop', (0.27, 0.155, 0.08), rough=0.5, spec=0.35, cc=0.2,
             tex=L.tex_twood(fx=1.0, fy=10.0, dark=0.8, seed=2.0))
    chrome = pm('kt_chrome', (0.85, 0.86, 0.9), rough=0.15, metal=1.0)
    steel = pm('kt_steel', (0.70, 0.72, 0.75), rough=0.25, metal=0.9)
    water = pm('kt_water', (0.45, 0.72, 0.95), rough=0.05, spec=0.8, cc=1.0)
    plate = pm('kt_bowl', (0.97, 0.96, 0.93), rough=0.2, spec=0.5, cc=0.6)
    plateb = pm('kt_bowlband', (0.25, 0.48, 0.88), rough=0.3)
    soap = pm('kt_soap', (0.95, 0.45, 0.65), rough=0.3, cc=0.6)
    H = 0.52
    o = [rbox('body', 0.04, 0.10, 0.05, 1.96, 0.92, H - 0.04, body, 0.03),
         box('plinth', 0.07, 0.14, 0.0, 1.93, 0.88, 0.06, pm('kt_burner', (0.10, 0.10, 0.11)))]
    # worktop with the sink hole (four slabs around it)
    sx0, sy0, sx1, sy1 = 1.18, 0.24, 1.78, 0.76
    o += [box('t0', 0.02, 0.07, H - 0.045, sx0, 0.95, H, top),
          box('t1', sx1, 0.07, H - 0.045, 1.98, 0.95, H, top),
          box('t2', sx0, 0.07, H - 0.045, sx1, sy0, H, top),
          box('t3', sx0, sy1, H - 0.045, sx1, 0.95, H, top)]
    o.append(box('basin', sx0, sy0, H - 0.16, sx1, sy1, H - 0.14, steel))
    for (a0, b0, a1, b1) in ((sx0, sy0, sx1, sy0 + 0.02), (sx0, sy1 - 0.02, sx1, sy1), (sx0, sy0, sx0 + 0.02, sy1), (sx1 - 0.02, sy0, sx1, sy1)):
        o.append(box('bw%.2f%.2f' % (a0, b0), a0, b0, H - 0.16, a1, b1, H - 0.001, steel))
    o.append(box('water', sx0 + 0.02, sy0 + 0.02, H - 0.15, sx1 - 0.02, sy1 - 0.02, H - 0.09, water))
    o.append(box('rimf', sx0 - 0.02, sy0 - 0.02, H - 0.005, sx1 + 0.02, sy0, H + 0.008, steel))
    # faucet at the back
    fx, fy = (sx0 + sx1) / 2, 0.86
    o.append(cone('fbase', (fx, fy, H), 0.04, 0.035, 0.05, chrome))
    o.append(tube('spout', [(fx, fy, H + 0.04), (fx, fy, H + 0.24), (fx, fy - 0.08, H + 0.30), (fx, fy - 0.16, H + 0.26),
                            (fx, fy - 0.17, H + 0.20)], [0.022, 0.022, 0.02, 0.02, 0.018], chrome, res=8))
    for sx in (-1, 1):
        o.append(cone('tap%d' % sx, (fx + sx * 0.11, fy, H), 0.025, 0.025, 0.07, chrome))
        o.append(ellipsoid('tk%d' % sx, (fx + sx * 0.11, fy, H + 0.085), (0.035, 0.035, 0.02), pm('kt_tap%d' % sx, (0.9, 0.25, 0.25) if sx < 0 else (0.25, 0.45, 0.9), rough=0.3)))
    o.append(cone('soap', (1.88, 0.74, H), 0.04, 0.04, 0.12, soap, bevel=0.01))
    o.append(cone('pump', (1.88, 0.74, H + 0.12), 0.012, 0.012, 0.04, chrome))
    # doors and handles
    for k, (x0, x1) in enumerate(((0.08, 0.50), (0.52, 0.96), (1.04, 1.48), (1.50, 1.92))):
        o.append(rbox('door%d' % k, x0, 0.085, 0.09, x1, 0.11, H - 0.08, body, 0.012))
        kx = x1 - 0.06 if k % 2 == 0 else x0 + 0.06
        o.append(rbox('h%d' % k, kx - 0.012, 0.065, 0.24, kx + 0.012, 0.09, 0.34, chrome, 0.008))
    # a dish rack with plates on the left cell
    o.append(box('rack', 0.20, 0.30, H, 0.80, 0.78, H + 0.03, chrome, bevel=0.005))
    for k in range(5):
        x = 0.30 + k * 0.10
        o.append(cone('pl%d' % k, (x, 0.55, H + 0.03), 0.17, 0.17, 0.018, plate, rot=(0, 90 - 12, 0), verts=40))
        o.append(cone('pc%d' % k, (x + 0.002, 0.55, H + 0.03), 0.12, 0.12, 0.02, plateb, rot=(0, 90 - 12, 0), verts=32))
    o.append(cone('cup', (0.68, 0.40, H + 0.03), 0.06, 0.07, 0.12, pm('kt_cup', (0.98, 0.72, 0.20), rough=0.3, cc=0.5)))
    return o


def garden_stump():
    bark = pm('gd_bark', (0.36, 0.22, 0.12), rough=0.9, tex=L.tex_tnoise((0.46, 0.30, 0.17), f=30, bump=0.9, dist=0.02, seed=11.0, kz=60))
    rings = pm('gd_rings', (0.86, 0.66, 0.42), rough=0.7)
    ring2 = pm('gd_rings2', (0.70, 0.50, 0.30), rough=0.7)
    moss = pm('gd_moss', (0.40, 0.64, 0.24), rough=0.9)
    cap = pm('gd_toad', (0.92, 0.20, 0.18), rough=0.4, cc=0.4)
    stem = pm('gd_toadstem', (0.98, 0.95, 0.88), rough=0.7)
    c = (0.5, 0.52)
    o = [cone('trunk', (c[0], c[1], 0.0), 0.36, 0.31, 0.40, bark, verts=40)]
    for k in range(5):
        a = 2 * math.pi * k / 5 + 0.4
        o.append(ellipsoid('root%d' % k, (c[0] + 0.33 * math.cos(a), c[1] + 0.33 * math.sin(a), 0.05),
                           (0.16, 0.08, 0.08), bark, rot=(0, -20, math.degrees(a))))
    o.append(cone('top', (c[0], c[1], 0.40), 0.30, 0.30, 0.012, rings, verts=40))
    for k, r in enumerate((0.24, 0.17, 0.10, 0.04)):
        o.append(torus('ring%d' % k, (c[0], c[1], 0.412), r, 0.006, ring2, m=6))
    o.append(ellipsoid('moss', (c[0] - 0.18, c[1] - 0.16, 0.30), (0.14, 0.08, 0.10), moss))
    for k, (dx, dy, s) in enumerate(((0.36, -0.28, 1.0), (0.28, -0.38, 0.7))):
        o.append(cone('ms%d' % k, (c[0] + dx * 0.9, c[1] + dy * 0.9, 0.0), 0.022 * s, 0.02 * s, 0.07 * s, stem))
        o.append(ellipsoid('mc%d' % k, (c[0] + dx * 0.9, c[1] + dy * 0.9, 0.07 * s), (0.06 * s, 0.06 * s, 0.04 * s), cap))
    # an axe-less, friendly touch: a little bird on the stump
    bird = pm('gd_bird', (0.35, 0.60, 0.95), rough=0.5)
    belly = pm('gd_birdb', (1.0, 0.88, 0.70), rough=0.6)
    o += bird_model((c[0] + 0.1, c[1] + 0.05, 0.41), bird, belly, 1.0)
    return o


def bird_model(p, body, belly, s):
    beak = pm('gd_beak', (1.0, 0.65, 0.15), rough=0.5)
    eye = pm('gd_eye', (0.05, 0.04, 0.04), rough=0.3)
    x, y, z = p
    return [ellipsoid('bb', (x, y, z + 0.06 * s), (0.07 * s, 0.09 * s, 0.06 * s), body, rot=(-10, 0, 0)),
            ellipsoid('bbe', (x, y - 0.03 * s, z + 0.05 * s), (0.055 * s, 0.06 * s, 0.045 * s), belly),
            ellipsoid('bh', (x, y - 0.07 * s, z + 0.12 * s), (0.05 * s, 0.05 * s, 0.05 * s), body),
            cone('bk', (x, y - 0.115 * s, z + 0.12 * s), 0.015 * s, 0.0, 0.035 * s, beak, rot=(90, 0, 0), verts=8),
            ellipsoid('be1', (x - 0.03 * s, y - 0.105 * s, z + 0.135 * s), (0.009 * s, 0.006 * s, 0.01 * s), eye, seg=8, rings=6),
            ellipsoid('be2', (x + 0.03 * s, y - 0.105 * s, z + 0.135 * s), (0.009 * s, 0.006 * s, 0.01 * s), eye, seg=8, rings=6),
            ellipsoid('bt', (x, y + 0.10 * s, z + 0.09 * s), (0.03 * s, 0.07 * s, 0.015 * s), body, rot=(25, 0, 0))]


def garden_watering():
    """A green watering can on a wooden crate."""
    crate = wood('gd_crate', (0.80, 0.60, 0.36))
    dark = pm('gd_crateins', (0.30, 0.20, 0.11), rough=0.9)
    can = pm('gd_can', (0.25, 0.66, 0.50), rough=0.35, spec=0.5, cc=0.5)
    rose = pm('gd_rose', (0.85, 0.72, 0.30), rough=0.3, metal=0.8)
    x0, y0, x1, y1, H = 0.06, 0.08, 0.94, 0.92, 0.36
    o = [box('in', x0 + 0.03, y0 + 0.03, 0.0, x1 - 0.03, y1 - 0.03, H - 0.01, dark)]
    for k, z in enumerate((0.0, 0.12, 0.24)):
        o.append(box('f%d' % k, x0, y0, z, x1, y0 + 0.03, z + 0.10, crate, bevel=0.008))
        o.append(box('b%d' % k, x0, y1 - 0.03, z, x1, y1, z + 0.10, crate, bevel=0.008))
        o.append(box('l%d' % k, x0, y0, z, x0 + 0.03, y1, z + 0.10, crate, bevel=0.008))
        o.append(box('r%d' % k, x1 - 0.03, y0, z, x1, y1, z + 0.10, crate, bevel=0.008))
    for k in range(5):
        xx = x0 + 0.02 + k * (x1 - x0 - 0.04) / 5
        o.append(box('s%d' % k, xx, y0, H - 0.03, xx + (x1 - x0 - 0.04) / 5 - 0.015, y1, H, crate, bevel=0.006))
    c = (0.48, 0.54, H)
    o.append(cone('can', c, 0.17, 0.15, 0.26, can, bevel=0.02, verts=40))
    o.append(ellipsoid('cantop', (c[0], c[1], H + 0.26), (0.15, 0.15, 0.04), can))
    o.append(tube('spout', [(c[0] - 0.12, c[1] - 0.02, H + 0.06), (c[0] - 0.26, c[1] - 0.05, H + 0.20), (c[0] - 0.34, c[1] - 0.07, H + 0.30)],
                  [0.03, 0.022, 0.018], can))
    o.append(cone('rose', (c[0] - 0.36, c[1] - 0.075, H + 0.29), 0.035, 0.05, 0.04, rose, rot=(0, -50, 0)))
    o.append(tube('handle', [(c[0] + 0.08, c[1] + 0.02, H + 0.29), (c[0] + 0.02, c[1] + 0.02, H + 0.40), (c[0] + 0.14, c[1] + 0.02, H + 0.40),
                             (c[0] + 0.17, c[1] + 0.02, H + 0.18)], [0.02, 0.02, 0.02, 0.02], can))
    return o


def garden_birdbath():
    stone = stone_mat('gd_bbstone', (0.72, 0.70, 0.66))
    water = pm('gd_bbwater', (0.40, 0.70, 0.95), rough=0.05, spec=0.8, cc=1.0)
    bird = pm('gd_bird2', (0.95, 0.55, 0.25), rough=0.5)
    belly = pm('gd_birdb', (1.0, 0.88, 0.70), rough=0.6)
    c = (0.5, 0.5)
    o = [cone('plinth', (c[0], c[1], 0.0), 0.44, 0.42, 0.05, stone, bevel=0.015, verts=48),
         cone('foot', (c[0], c[1], 0.05), 0.20, 0.13, 0.08, stone, verts=32),
         cone('col', (c[0], c[1], 0.12), 0.09, 0.08, 0.34, stone, verts=32),
         cone('bowl', (c[0], c[1], 0.44), 0.14, 0.38, 0.12, stone, verts=48),
         torus('lip', (c[0], c[1], 0.56), 0.37, 0.03, stone),
         cone('water', (c[0], c[1], 0.52), 0.34, 0.34, 0.03, water, verts=48)]
    o += bird_model((c[0] + 0.22, c[1] + 0.12, 0.575), bird, belly, 1.0)
    # flowers round the plinth
    pk = pm('gd_bflower', (0.98, 0.50, 0.72), rough=0.6, spec=0.3)
    ye = pm('gd_daisyc', (1.0, 0.78, 0.15), rough=0.6)
    leaf = pm('gd_leaf', (0.26, 0.62, 0.22), rough=0.5, spec=0.4)
    for k in range(7):
        a = 2 * math.pi * k / 7 + 0.2
        x, y = c[0] + 0.33 * math.cos(a), c[1] + 0.33 * math.sin(a)
        o.append(ellipsoid('fl%d' % k, (x, y, 0.08), (0.05, 0.05, 0.03), leaf, seg=12, rings=6))
        o.append(ellipsoid('fp%d' % k, (x, y, 0.105), (0.03, 0.03, 0.015), pk if k % 2 else ye, seg=12, rings=6))
    return o


def garden_bench():
    """A 2-cell garden bench, painted sage green, with a cushion."""
    paint = pm('gd_bench', (0.55, 0.70, 0.52), rough=0.5, spec=0.35)
    iron = pm('gd_benchleg', (0.22, 0.26, 0.24), rough=0.45, metal=0.5)
    cush = pm('gd_cushion', (0.98, 0.62, 0.62), rough=0.9, sheen=1.0)
    x0, x1 = 0.08, 1.92
    o = []
    for lx in (0.14, 1.0, 1.86):
        o.append(box('legf%.2f' % lx, lx - 0.04, 0.16, 0.0, lx + 0.04, 0.24, 0.42, iron, bevel=0.01))
        o.append(box('legb%.2f' % lx, lx - 0.04, 0.74, 0.0, lx + 0.04, 0.82, 0.86, iron, bevel=0.01))
        o.append(box('legs%.2f' % lx, lx - 0.03, 0.16, 0.36, lx + 0.03, 0.82, 0.41, iron))
    for k in range(4):
        y = 0.14 + k * 0.155
        o.append(box('slat%d' % k, x0, y, 0.40, x1, y + 0.13, 0.46, paint, bevel=0.015))
    for k, z in enumerate((0.56, 0.70)):
        o.append(box('back%d' % k, x0, 0.76, z, x1, 0.83, z + 0.11, paint, bevel=0.015))
    for sx in (0.02, 1.86):
        o.append(box('arm%.2f' % sx, sx, 0.14, 0.58, sx + 0.12, 0.86, 0.63, paint, bevel=0.015))
        o.append(box('armp%.2f' % sx, sx + 0.03, 0.14, 0.40, sx + 0.09, 0.20, 0.60, iron))
    o.append(rbox('cush', 0.26, 0.20, 0.45, 0.86, 0.72, 0.52, cush, 0.03))
    # a straw hat left on the bench
    straw = pm('gd_straw', (0.96, 0.82, 0.50), rough=0.8)
    band = pm('gd_hatband', (0.30, 0.50, 0.90), rough=0.5)
    o.append(cone('brim', (1.40, 0.45, 0.46), 0.22, 0.22, 0.015, straw, verts=40, bevel=0.006))
    o.append(ellipsoid('crown', (1.40, 0.45, 0.48), (0.12, 0.12, 0.08), straw))
    o.append(torus('band', (1.40, 0.45, 0.49), 0.118, 0.016, band))
    return o


def garden_puddle(v):
    """A rain puddle on the grass: a full tile (grass + water, a muddy rim)."""
    o = garden_floor(0)
    water = pm('gd_water', (0.36, 0.64, 0.92), rough=0.05, spec=0.8, cc=1.0)
    mud = pm('gd_mud', (0.40, 0.28, 0.16), rough=0.9)
    o.append(disc('mud', (0.5, 0.5, -0.02), 0.41, 0.028, mud, jitter=0.10, seed=7 + v, bevel=0.01))
    o.append(disc('water', (0.5, 0.5, -0.02), 0.37, 0.030, water, jitter=0.10, seed=7 + v))
    o += puddle_glints(v, z=0.011)
    return o


# ===========================================================================
# the sprite lists
# ===========================================================================

def render_all(a, world, phase, only):
    out = os.path.join(os.path.abspath(a.out), world)
    C.reset(world, cpu=a.cpu)
    S = a.samples
    jobs = []     # (name, builder, mode, extra)

    def tile(name, fn, extra=None):
        jobs.append((name, fn, 'tile', extra))

    def prop(name, fn, extra=None):
        jobs.append((name, fn, 'prop', extra))

    def thing(name, fn, extra=None):
        jobs.append((name, fn, 'obj', extra))

    nv = 2 if phase == 1 else 4
    nw = 1 if phase == 1 else 2
    p2 = phase >= 2

    def prop2(name, fn):
        jobs.append((name, fn, 'prop', dict(footprint=[2, 1])))

    if world == 'living':
        for v in range(nv):
            tile('living_floor_v%d' % v, lambda v=v: living_floor(v))
        for v in range(nw):
            prop('living_wall_v%d' % v, lambda v=v: living_wall(v))
        prop('living_prop_armchair', living_armchair)
        prop('living_prop_table', living_side_table)
        if p2:
            prop('living_prop_plant', living_plant)
            prop('living_prop_bookcase', living_bookcase)
            prop('living_prop_records', living_record_player)
            prop2('living_prop2_sofa', living_sofa)
        for v in range(nv):
            thing('living_obj_v%d' % v, lambda v=v: yarn(v), dict(color=list(YARN[v])))
        for v in range(nv):
            jobs.append(('living_obj_on_v%d' % v, lambda v=v: yarn(v, True), 'on', None))
        tile('living_target', living_target)
    elif world == 'kitchen':
        for v in range(nv):
            tile('kitchen_floor_v%d' % v, lambda v=v: kitchen_floor(v))
        for v in range(nw):
            prop('kitchen_wall_v%d' % v, lambda v=v: kitchen_wall(v))
        prop('kitchen_prop_fridge', kitchen_fridge)
        prop('kitchen_prop_stove', kitchen_stove)
        if p2:
            prop('kitchen_prop_cabinet', kitchen_cabinet)
            prop('kitchen_prop_stool', kitchen_stool)
            prop('kitchen_prop_bin', kitchen_bin)
            prop2('kitchen_prop2_counter', kitchen_counter)
        for v in range(nv):
            thing('kitchen_obj_v%d' % v, lambda v=v: cookie_tin(v), dict(color=list(TINS[v])))
        tile('kitchen_target', kitchen_target)
        for v in range(nw):
            tile('kitchen_wet_v%d' % v, lambda v=v: kitchen_floor(v, wet=True))
    elif world == 'garden':
        for v in range(nv):
            tile('garden_floor_v%d' % v, lambda v=v: garden_floor(v))
        for v in range(nw):
            prop('garden_wall_v%d' % v, lambda v=v: garden_wall(v))
        prop('garden_prop_gnome', gnome)
        prop('garden_prop_bush', flower_bush)
        if p2:
            prop('garden_prop_stump', garden_stump)
            prop('garden_prop_watering', garden_watering)
            prop('garden_prop_birdbath', garden_birdbath)
            prop2('garden_prop2_bench', garden_bench)
            tile('garden_wet_v0', lambda: garden_puddle(0))
        for v in range(nv):
            thing('garden_obj_v%d' % v, lambda v=v: flower_pot(v), dict(color=list(FLOWERS[v])))
        tile('garden_target', garden_target)
        tile('garden_plate_up', lambda: garden_plate(False))
        tile('garden_plate_down', lambda: garden_plate(True))
        frames = (0, 3) if phase == 1 else (0, 1, 2, 3)
        for ax in (('h',) if phase == 1 else ('h', 'v')):
            for f in frames:
                jobs.append(('garden_gate_%s_%02d' % (ax, f), lambda ax=ax, f=f: garden_gate(ax, f), 'gate',
                             dict(anim='gate', axis=ax, frame=f, frames=4)))

    t_world = time.time()
    for name, fn, mode, extra in jobs:
        if only and name not in only:
            continue
        t0 = time.time()
        res = fn()
        objs, lamps = (res if isinstance(res, tuple) else (res, []))
        objs = [o for o in objs if o.type == 'MESH']
        anchor = C.cell(0, 0, 0)
        if mode == 'tile':
            C.render_sprite(out, name, objs, anchor, passes=('color', 'z'), samples=S, kind='tile', extra=extra)
        elif mode == 'prop':
            if lamps:
                w, h, ax, ay = C.fit(objs, anchor, 3, 0.0)
                gx, gy = 100, 76     # the lamp's pool on the floor: +-1.4 m around
                left, right = max(ax, gx), max(w - ax, gx)
                up, down = max(ay, gy), max(h - ay, gy)
                W, H, AX, AY = left + right, up + down, left, up
                C.render_sprite(out, name, objs, anchor, passes=('color', 'z', 'shadow', 'glow'),
                                size=(W, H, AX, AY), samples=S, shadow_z=0.0, kind='prop', extra=extra)
                fade_glow(os.path.join(out, name + '_gl.png'), AX, AY)
            else:
                C.render_sprite(out, name, objs, anchor, passes=('color', 'z', 'shadow'), samples=S,
                                shadow_z=0.0, kind='prop', extra=extra)
        elif mode == 'obj':
            C.render_sprite(out, name, objs, anchor, passes=('color', 'z'), samples=S, kind='obj', extra=extra)
            C.render_sprite(out, name + '_sh', objs, anchor, passes=('shadow',), shadow_z=0.0, kind='shadow',
                            extra=dict(of=name))
        elif mode == 'on':
            C.render_sprite(out, name, objs, anchor, passes=('color', 'z'), samples=S, kind='obj_on', extra=extra)
        elif mode == 'gate':
            # frames <kit>_gate_<axis>_NN (colour + z) and their shadow sheet
            # <kit>_gate_<axis>_sh_NN (shadow only, like the objects')
            C.render_sprite(out, name, objs, anchor, passes=('color', 'z'), samples=S, kind='gate', extra=extra)
            shn = name[:-3] + '_sh' + name[-3:]
            C.render_sprite(out, shn, objs, anchor, passes=('shadow',), shadow_z=0.0, kind='shadow',
                            extra=dict(of=name, **extra))
        L.clear_scene_objects()
        print('[worlds_a] %-28s %5.1f s' % (name, time.time() - t0), flush=True)
    C.save_meta(out)
    print('[worlds_a] %s done in %.1f s' % (world, time.time() - t_world), flush=True)


def fade_glow(path, ax, ay):
    """Fade the glow to black towards the sprite's border (the shade's own
    emission leaves a faint tail that would end in a visible edge)."""
    import numpy as np
    img = bpy.data.images.load(path, check_existing=False)
    img.colorspace_settings.name = 'Non-Color'
    w, h = img.size
    a = np.empty(w * h * 4, np.float32)
    img.pixels.foreach_get(a)
    bpy.data.images.remove(img)
    a = a.reshape(h, w, 4)[::-1, :, :3]
    yy, xx = np.mgrid[0:h, 0:w].astype(np.float32)
    rx = np.maximum(ax, w - ax)
    ry = np.maximum(ay, h - ay)
    r = np.sqrt(((xx + 0.5 - ax) / rx) ** 2 + ((yy + 0.5 - ay) / ry) ** 2)
    t = np.clip((1.0 - r) / 0.35, 0.0, 1.0)
    win = t * t * (3 - 2 * t)
    C.write_png(path, np.round(np.clip(a * win[..., None], 0, 1) * 255))


def main():
    import argparse
    a = C.args({'samples': 64})
    argv = sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else []
    ap = argparse.ArgumentParser(allow_abbrev=False)
    ap.add_argument('--world', default='living,kitchen,garden')
    ap.add_argument('--phase', type=int, default=1)
    b, _ = ap.parse_known_args(argv)
    for w in b.world.split(','):
        render_all(a, w, b.phase, a.only)


main()
