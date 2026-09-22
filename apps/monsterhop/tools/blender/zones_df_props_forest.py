"""Monster Hop - Werewolf Woods props and moving things (procedural meshes),
used by forest.py. Built on cell (0, 0), floor 0, base centred on (0.5, 0.5).
"""
import math

import numpy as np

import mh_common as C
import zones_df_lib as L
import zones_df_tex as T
from zones_df_tex import rgb

Z = 'forest'


def _mats():
    if getattr(_mats, 'done', False):
        return
    _mats.done = True
    L.attr_mat('f_vcol', rough=0.8, spec=0.25)
    L.attr_mat('f_vcol_leaf', rough=0.7, spec=0.3)
    L.noise_mat('f_bark', (112, 78, 54), (78, 52, 36), scale=24, rough=0.9, spec=0.15, bump=0.5,
                aniso=(3.0, 3.0, 0.6))
    L.noise_mat('f_wood', (136, 96, 62), (104, 72, 46), scale=20, rough=0.8, spec=0.2, bump=0.3,
                aniso=(1.0, 1.0, 0.25))
    L.flat('f_iron', (56, 58, 64), rough=0.45, metal=0.8, spec=0.5)
    L.flat('f_glass', (255, 214, 140), rough=0.3, emit=(255, 176, 84), emit_strength=4.0)
    L.noise_mat('f_stone', (170, 172, 168), (130, 134, 132), scale=9, rough=0.85, bump=0.3)
    L.noise_mat('f_stem', (232, 234, 214), (204, 210, 190), scale=12, rough=0.6, spec=0.3, bump=0.2,
                emit=(120, 220, 200), emit_strength=0.08)
    L.flat('f_cap', (34, 150, 170), rough=0.45, spec=0.4, emit=(40, 200, 210), emit_strength=0.35)
    L.flat('f_spot', (220, 255, 250), rough=0.4, emit=(160, 255, 240), emit_strength=2.2)
    L.flat('f_gill', (40, 130, 130), rough=0.6, emit=(90, 240, 220), emit_strength=1.3)
    L.flat('f_foam', (120, 196, 196), rough=0.7, spec=0.2)


# ---------------------------------------------------------------------------
# pine: stacked star-shaped skirts
# ---------------------------------------------------------------------------

def pine_tier(name, zc, R, h, npts, rng, rot, light, dark):
    nth = npts * 8
    nr = 7
    th = np.linspace(0, 2 * math.pi, nth, endpoint=False) + rot
    pt = np.abs(np.cos(npts * (th - rot) / 2)) ** 0.8                     # 1 at the points
    verts = [(0.5, 0.5, zc + h)]
    vc = [light * 1.05]
    for j in range(1, nr + 1):
        s = j / nr
        for i, t in enumerate(th):
            r = R * s * (0.74 + 0.26 * pt[i] ** (0.5 + s))
            z = zc + h * (1 - s ** 1.15) - 0.10 * s ** 2 * pt[i] + 0.03 * s * (1 - pt[i])
            verts.append((0.5 + r * math.cos(t), 0.5 + r * math.sin(t), z))
            k = (1 - s) * 0.55 + 0.45 * pt[i] * s * 0.3 + rng.uniform(-0.05, 0.05)
            vc.append(T.lerp(dark, light, min(1.0, max(0.0, k + 0.35))))
    faces = [(0, 1 + (i + 1) % nth, 1 + i) for i in range(nth)]
    for j in range(nr - 1):
        a0 = 1 + j * nth
        b0 = a0 + nth
        for i in range(nth):
            i1 = (i + 1) % nth
            faces.append((a0 + i, a0 + i1, b0 + i1, b0 + i))
    # underside back to the trunk (dark)
    c = len(verts)
    verts.append((0.5, 0.5, zc + 0.05))
    vc.append(dark * 0.35)
    rim0 = 1 + (nr - 1) * nth
    for i in range(nth):
        faces.append((c, rim0 + i, rim0 + (i + 1) % nth))
    return L.mesh(name, verts, faces, 'f_vcol_leaf', smooth=True, cols=vc)


def pine():
    rng = np.random.default_rng(11)
    objs = []
    path = L.catmull([(0.5, 0.5, 0.0), (0.5, 0.5, 1.2), (0.51, 0.49, 2.6)], 12)
    rad = np.linspace(0.12, 0.035, 12)
    rad[0] = 0.15
    objs.append(L.tube('pine_trunk', 'f_bark', path, rad, n=12))
    light, dark = rgb(56, 138, 106), rgb(14, 62, 46)
    tiers = [(0.50, 0.66, 0.75), (0.98, 0.56, 0.68), (1.42, 0.46, 0.62), (1.84, 0.35, 0.58), (2.22, 0.24, 0.56),
             (2.56, 0.13, 0.46)]
    for k, (zc, R, h) in enumerate(tiers):
        objs.append(pine_tier('tier%d' % k, zc, R, h, 9 if k < 3 else 7, rng, rng.uniform(0, 1), light, dark))
    return objs, 3.02


# ---------------------------------------------------------------------------
# oak: a trunk with roots, branches, a canopy of lumps
# ---------------------------------------------------------------------------

def oak():
    rng = np.random.default_rng(12)
    objs = []
    path = L.catmull([(0.5, 0.5, 0.0), (0.49, 0.5, 0.7), (0.5, 0.52, 1.35)], 10)
    objs.append(L.tube('oak_trunk', 'f_bark', path, np.linspace(0.19, 0.13, 10), n=16))
    for k in range(5):
        t = 2 * math.pi * k / 5 + 0.3
        d = np.array([math.cos(t), math.sin(t), 0])
        p = [np.array([0.5, 0.5, 0.35]) + d * 0.05, np.array([0.5, 0.5, 0.12]) + d * 0.2,
             np.array([0.5, 0.5, 0.0]) + d * 0.32]
        objs.append(L.tube('root%d' % k, 'f_bark', L.catmull(p, 6), np.linspace(0.08, 0.03, 6), n=8))
    lumps = [((0.5, 0.52, 2.35), 0.52), ((0.2, 0.46, 2.05), 0.38), ((0.8, 0.46, 2.05), 0.38),
             ((0.48, 0.2, 2.05), 0.38), ((0.52, 0.82, 2.2), 0.4), ((0.28, 0.72, 2.55), 0.36),
             ((0.74, 0.28, 2.5), 0.36), ((0.5, 0.5, 2.72), 0.34), ((0.72, 0.72, 2.3), 0.34),
             ((0.26, 0.26, 2.3), 0.34)]
    for (cx, cy, cz), r in lumps:
        if r < 0.38:
            continue
        ctrl = [(0.5, 0.52, 1.3), ((0.5 + cx) / 2, (0.52 + cy) / 2, 1.6 + (cz - 1.6) * 0.4), (cx, cy, cz - r * 0.6)]
        objs.append(L.tube('br', 'f_bark', L.catmull(ctrl, 6), np.linspace(0.08, 0.04, 6), n=8))
    light, dark, deep = rgb(76, 158, 100), rgb(34, 100, 60), rgb(16, 58, 40)

    def colfun(P):
        h = np.clip((P[:, 2] - 1.6) / 1.4, 0, 1)
        nz = np.sin(P @ np.array([7.1, 5.3, 6.7])) * 0.5 + 0.5
        c = T.lerp(deep, dark, np.clip(h * 1.8, 0, 1))
        c = T.lerp(c, light, np.clip((h - 0.45) * 1.6 + 0.25 * nz, 0, 1))
        return c
    for k, (c, r) in enumerate(lumps):
        objs.append(L.blob('lump%d' % k, 'f_vcol_leaf', c, r, rng, amp=0.16, freq=3.0, subdiv=3, colfun=colfun))
    return objs, 3.06


# ---------------------------------------------------------------------------
# giant glowing mushroom (lit teal)
# ---------------------------------------------------------------------------

def mushroom_one(tag, cx, cy, s, bend, rng, spots=8):
    objs = []
    prof = [(0.11, 0.0), (0.12, 0.05), (0.095, 0.2), (0.085, 0.5), (0.09, 0.72), (0.0, 0.74)]
    stem = L.lathe(tag + 'stem', 'f_stem', [(r * s, z * s) for r, z in prof], n=18, center=(cx, cy))
    for v in stem.data.vertices:
        t = v.co.z / (0.74 * s)
        v.co.x += bend[0] * t * t * s
        v.co.y += bend[1] * t * t * s
    stem.data.update()
    objs.append(stem)
    top = (cx + bend[0] * s, cy + bend[1] * s)
    rx, rz, z0 = 0.44 * s, 0.33 * s, 0.70 * s
    prof = [(rx * math.sin(p), z0 + rz * math.cos(p)) for p in np.linspace(math.pi / 2 + 0.25, 0, 14)]
    prof = [(p[0], p[1]) for p in prof] + [(0.0, z0 + rz)]
    objs.append(L.lathe(tag + 'cap', 'f_cap', prof, n=32, center=top, cap_bot=False))
    rim_r, rim_z = prof[0]
    gills = [(0.07 * s, z0 + 0.02 * s), (rim_r * 0.7, rim_z + 0.02 * s), (rim_r * 0.98, rim_z + 0.005)]
    objs.append(L.lathe(tag + 'gill', 'f_gill', gills, n=32, center=top, cap_top=False, cap_bot=False))
    for k in range(spots):
        ph = rng.uniform(0.25, 1.35)
        t = rng.uniform(0, 2 * math.pi)
        p = (top[0] + rx * math.sin(ph) * math.cos(t), top[1] + rx * math.sin(ph) * math.sin(t),
             z0 + rz * math.cos(ph))
        objs.append(L.blob(tag + 'spot%d' % k, 'f_spot', p, rng.uniform(0.045, 0.065) * s, rng, amp=0.05,
                           subdiv=2, scale=(1, 1, 0.5)))
    return objs


def mushroom():
    rng = np.random.default_rng(13)
    objs = mushroom_one('big', 0.5, 0.52, 1.35, (-0.05, 0.03), rng, 9)
    objs += mushroom_one('sm1', 0.8, 0.3, 0.45, (0.04, -0.02), rng, 3)
    objs += mushroom_one('sm2', 0.25, 0.25, 0.32, (-0.03, -0.03), rng, 2)
    lamps = [C.add_point_light((0.47, 0.54, 0.72), color=(0.35, 1.0, 0.86), power=22.0, radius=0.2, name='cap_glow'),
             C.add_point_light((0.8, 0.3, 0.2), color=(0.35, 1.0, 0.86), power=1.2, radius=0.05, name='sm_glow')]
    return objs, 1.45, lamps


# ---------------------------------------------------------------------------
# lantern on a post (lit warm)
# ---------------------------------------------------------------------------

def lantern():
    rng = np.random.default_rng(14)
    objs = [C.box('post', 0.34, 0.45, 0.0, 0.44, 0.55, 1.56, 'f_wood'),
            C.box('post_cap', 0.325, 0.435, 1.56, 0.455, 0.565, 1.6, 'f_iron'),
            C.box('arm', 0.44, 0.475, 1.44, 0.74, 0.525, 1.49, 'f_wood'),
            C.box('brace', 0.44, 0.48, 1.30, 0.47, 0.52, 1.44, 'f_wood')]
    # the brace as a diagonal strut
    objs.append(L.tube('strut', 'f_wood', [(0.44, 0.5, 1.26), (0.6, 0.5, 1.45)], [0.018, 0.018], n=6))
    cx, cy = 0.66, 0.5
    objs.append(L.tube('hook', 'f_iron', [(cx, cy, 1.44), (cx, cy, 1.36)], [0.008, 0.008], n=6))
    objs.append(L.lathe('ring', 'f_iron', [(0.022, 1.335), (0.03, 1.35), (0.022, 1.365), (0.0, 1.37)], n=10,
                        center=(cx, cy)))
    hx, z0, z1 = 0.085, 1.07, 1.28
    objs.append(C.box('glass', cx - hx + 0.01, cy - hx + 0.01, z0 + 0.02, cx + hx - 0.01, cy + hx - 0.01, z1 - 0.01,
                      'f_glass'))
    for sx in (-1, 1):
        for sy in (-1, 1):
            objs.append(C.box('corner', cx + sx * hx - 0.012, cy + sy * hx - 0.012, z0, cx + sx * hx + 0.012,
                              cy + sy * hx + 0.012, z1, 'f_iron'))
    objs.append(C.box('base', cx - hx - 0.015, cy - hx - 0.015, z0 - 0.03, cx + hx + 0.015, cy + hx + 0.015, z0 + 0.02,
                      'f_iron'))
    objs.append(L.mesh('roof', [(cx - hx - 0.025, cy - hx - 0.025, z1), (cx + hx + 0.025, cy - hx - 0.025, z1),
                                (cx + hx + 0.025, cy + hx + 0.025, z1), (cx - hx - 0.025, cy + hx + 0.025, z1),
                                (cx, cy, z1 + 0.07)],
                       [(0, 1, 4), (1, 2, 4), (2, 3, 4), (3, 0, 4), (3, 2, 1, 0)], 'f_iron'))
    for k in range(4):
        t = 2 * math.pi * k / 4 + 0.4
        objs.append(L.blob('st%d' % k, 'f_stone', (0.39 + 0.1 * math.cos(t), 0.5 + 0.1 * math.sin(t), 0.02),
                           rng.uniform(0.05, 0.07), rng, amp=0.2, subdiv=2, flat_below=0.0, scale=(1, 1, 0.7)))
    # the glass is the emitter and the base plate a thin grid: neither blocks
    # the lantern's own light (or the pool under it would be black)
    for ob in objs:
        if ob.name.startswith(('glass', 'base')):
            ob.visible_shadow = False
    lamps = [C.add_point_light((cx, cy, 1.17), color=(1.0, 0.66, 0.32), power=38.0, radius=0.05, name='lantern')]
    return objs, 1.65, lamps


# ---------------------------------------------------------------------------
# floating logs (moving): west end, middle, east end, one cell each
# ---------------------------------------------------------------------------

LOG_A, LOG_B, LOG_ZC = 0.25, 0.22, -0.22        # half-width (y), half-height, centre: top at z = 0
FLOAT_Z = -0.02                                 # the watch puts the anchor (the log's top) here (SPEC 12)
WATER = -0.18 - FLOAT_Z                         # the river surface relative to the log's anchor
CUT = WATER - 0.10                              # below it nothing shows: cut the mesh


def bark_tex():
    """Bark around the log: u = x (1 m, periodic), v = angle / 2 pi (v = 0.25
    is the top). Deep ridges along the log, moss patches on top."""
    w, h = T.TEX, int(T.TEX * 1.5)
    u = (np.arange(w) + 0.5) / w
    v = (np.arange(h) + 0.5) / h
    U, Vv = np.meshgrid(u, v)
    sh = U.shape
    ridge = np.sin(2 * math.pi * 26 * Vv + 1.5 * T.fnoise(sh, 0.03, 1100, aniso=(4.0, 0.6)))
    n = T.fnoise(sh, 0.03, 1101, aniso=(4.0, 0.7))
    col = T.lerp(rgb(92, 60, 40), rgb(146, 104, 70), T.sstep(-0.6, 0.9, ridge * 0.7 + 0.4 * n))
    cr = T.fnoise(sh, 0.012, 1102, aniso=(6.0, 0.4))
    col = col * (1 - 0.5 * T.sstep(1.1, 1.9, cr))[..., None]
    dv = np.abs(np.mod(Vv - 0.25 + 0.5, 1.0) - 0.5)            # angular distance from the top
    col = col * (0.72 + 0.28 * T.sstep(0.32, 0.12, dv))[..., None]
    mm = T.fnoise(sh, 0.035, 1103)
    moss = T.sstep(0.13, 0.07, dv + 0.03 * mm) * T.sstep(0.5, 1.1, mm)
    col = T.lerp(col, T.lerp(rgb(64, 130, 64), rgb(96, 164, 84), T.sstep(-1, 1, T.fnoise(sh, 0.008, 1104))), moss * 0.9)
    hgt = 0.006 * ridge - 0.006 * T.sstep(1.1, 1.9, cr) + 0.005 * moss
    return T.Tex(col, hgt)


def rings_tex():
    n = 160
    s = (np.arange(n) + 0.5) / n
    S, Tt = np.meshgrid(s, s)
    r = np.hypot(S - 0.5, Tt - 0.5) * 2
    sh = S.shape
    wob = 0.03 * T.fnoise(sh, 0.05, 1111)
    ring = np.sin(2 * math.pi * 6 * (r + wob))
    col = T.lerp(rgb(214, 172, 118), rgb(176, 128, 82), T.sstep(0.2, 1.0, ring) * 0.8)
    col = T.lerp(col, rgb(160, 110, 70), T.sstep(0.12, 0.0, r))
    crack = T.cover(T.sd_polyline(S, Tt, [(0.5, 0.5), (0.62, 0.58), (0.74, 0.6), (0.86, 0.66)], 0.006))
    col = col * (1 - 0.5 * crack)[..., None]
    col = T.lerp(col, rgb(90, 60, 40), T.sstep(0.84, 0.9, r))
    return T.Tex(col, 0.002 * ring - 0.004 * crack)


def log_mesh(name, x0, x1, taper_w=False, nth=36):
    xs = list(np.linspace(x0, x1, 9))
    if taper_w:
        xs = list(x0 + (0.14) * (1 - np.cos(np.linspace(0, math.pi / 2, 7)))) + list(np.linspace(x0 + 0.16, x1, 7))
        xs = sorted(set(round(x, 4) for x in xs))
    verts, faces, uvs = [], [], []
    th = np.linspace(0, 2 * math.pi, nth + 1)
    for x in xs:
        f = 1.0
        if taper_w:
            d = (x0 + 0.14 - x) / 0.14
            f = math.sqrt(max(0.0, 1 - max(0.0, d) ** 2)) if d > 0 else 1.0
            f = max(f, 0.06)
        for t in th:
            rib = 1 + 0.025 * math.cos(11 * t)
            y = 0.5 + LOG_A * f * rib * math.cos(t)
            z = LOG_ZC + LOG_B * f * rib * math.sin(t)
            if taper_w:
                z = LOG_ZC + (z - LOG_ZC)
            verts.append((x, y, max(z, CUT)))
    m = nth + 1
    for i in range(len(xs) - 1):
        for j in range(nth):
            a0 = i * m + j
            faces.append((a0, a0 + m, a0 + m + 1, a0 + 1))
            uvs += [(xs[i], th[j] / (2 * math.pi)), (xs[i + 1], th[j] / (2 * math.pi)),
                    (xs[i + 1], th[j + 1] / (2 * math.pi)), (xs[i], th[j + 1] / (2 * math.pi))]
    ob = L.mesh(name, verts, faces, 'f_logbark', uvs=uvs, smooth=True)
    return ob, xs


def foam_strip(name, x0, x1, side):
    """A thin pale band on the water just outside the log's waterline."""
    wl = LOG_A * math.sqrt(1 - ((WATER - LOG_ZC) / LOG_B) ** 2)
    y_in, y_out = 0.5 + side * (wl - 0.01), 0.5 + side * (wl + 0.022)
    z = WATER + 0.006
    vs = [(x0, y_in, z), (x1, y_in, z), (x1, y_out, z), (x0, y_out, z)]
    return L.mesh(name, vs, [(0, 1, 2, 3)], 'f_foam')


def logfloat(part):
    if 'f_logbark' not in C._MATS:
        L.tile_mat('f_logbark', bark_tex(), 'uv', rough=0.85, spec=0.2, bump=1.0)
        L.tile_mat('f_logring', rings_tex(), 'uv', rough=0.7, spec=0.25, bump=1.0)
    objs = []
    if part == 'm':
        ob, _ = log_mesh('log', 0.0, 1.0)
        objs += [ob, foam_strip('fa', 0, 1, -1), foam_strip('fb', 0, 1, 1)]
    elif part == 'w':
        ob, _ = log_mesh('log', 0.08, 1.0, taper_w=True)
        objs += [ob, foam_strip('fa', 0.12, 1, -1), foam_strip('fb', 0.12, 1, 1)]
        # the foam wrapping round the rounded west end
        wl = LOG_A * math.sqrt(1 - ((WATER - LOG_ZC) / LOG_B) ** 2)
        vs, fs = [], []
        th = np.linspace(math.pi / 2, 3 * math.pi / 2, 13)
        for t in th:
            for rr in (wl - 0.01, wl + 0.022):
                vs.append((0.22 + 0.13 / wl * rr * math.cos(t), 0.5 + rr * math.sin(t), WATER + 0.006))
        for i in range(len(th) - 1):
            fs.append((2 * i, 2 * i + 2, 2 * i + 3, 2 * i + 1))
        objs.append(L.mesh('fc', vs, fs, 'f_foam'))
    else:
        x1 = 0.9
        ob, _ = log_mesh('log', 0.0, x1)
        objs += [ob, foam_strip('fa', 0, x1 + 0.03, -1), foam_strip('fb', 0, x1 + 0.03, 1)]
        # the sawn end: a disc with the growth rings
        nth = 36
        th = np.linspace(0, 2 * math.pi, nth, endpoint=False)
        vs = [(x1, 0.5, LOG_ZC)]
        uv_c = []
        for t in th:
            y = 0.5 + LOG_A * math.cos(t) * 0.99
            z = max(LOG_ZC + LOG_B * math.sin(t) * 0.99, CUT)
            vs.append((x1 + 0.001, y, z))
        fs, uvs = [], []
        for i in range(nth):
            a0, b0 = 1 + i, 1 + (i + 1) % nth
            fs.append((0, a0, b0))
            for k in (0, a0, b0):
                vy, vz = vs[k][1], vs[k][2]
                uvs.append((0.5 + (vy - 0.5) / LOG_A * 0.5, 0.5 + (vz - LOG_ZC) / LOG_B * 0.5))
        objs.append(L.mesh('end', vs, fs, 'f_logring', uvs=uvs, recalc=False))
        vs = [(x1 + 0.005, 0.5 - 0.2, WATER + 0.006), (x1 + 0.04, 0.5 - 0.2, WATER + 0.006),
              (x1 + 0.04, 0.5 + 0.2, WATER + 0.006), (x1 + 0.005, 0.5 + 0.2, WATER + 0.006)]
        objs.append(L.mesh('fe', vs, [(0, 1, 2, 3)], 'f_foam'))
    return objs


# ---------------------------------------------------------------------------
# more props
# ---------------------------------------------------------------------------

def _mats2():
    if getattr(_mats2, 'done', False):
        return
    _mats2.done = True
    L.stripe_mat('f_logwall', (138, 96, 62), (98, 66, 44), axis='Z', freq=10, rough=0.85, spec=0.2)
    L.stripe_mat('f_shingle', (104, 70, 66), (80, 52, 50), axis='Z', freq=16, rough=0.8, spec=0.2)
    L.stripe_mat('f_boards', (150, 106, 70), (122, 84, 56), axis='X', freq=12, rough=0.85, spec=0.2)
    L.stripe_mat('f_boards_y', (150, 106, 70), (122, 84, 56), axis='Y', freq=12, rough=0.85, spec=0.2)
    L.flat('f_darkwood', (70, 46, 32), rough=0.8)
    L.flat('f_door', (104, 66, 42), rough=0.8)
    L.flat('f_win', (255, 206, 120), rough=0.3, emit=(255, 170, 80), emit_strength=3.5)
    L.flat('f_berry', (220, 50, 70), rough=0.4, spec=0.5)
    L.flat('f_petal_w', (246, 246, 236), rough=0.5)
    L.flat('f_petal_l', (190, 160, 250), rough=0.5)
    L.flat('f_petal_y', (255, 214, 80), rough=0.5)
    L.flat('f_petal_p', (250, 150, 190), rough=0.5)
    L.flat('f_leaf', (60, 140, 80), rough=0.7)
    L.flat('f_moon', (200, 226, 255), rough=0.3, emit=(170, 210, 255), emit_strength=2.0)
    L.flat('f_silver', (214, 222, 236), rough=0.25, metal=0.9, spec=0.6, emit=(150, 190, 255), emit_strength=0.4)
    L.flat('f_steel', (150, 156, 164), rough=0.35, metal=0.7, spec=0.5)
    L.flat('f_pan', (238, 120, 40), rough=0.5)
    L.noise_mat('f_lily', (70, 150, 72), (96, 176, 84), scale=14, rough=0.4, spec=0.5, bump=0.2)
    L.veil_mat('f_gateveil', (120, 180, 255), strength=1.1, alpha=0.5)
    L.attr_mat('f_vcol_rock', rough=0.85, spec=0.2, bump_noise=0.6, bump_scale=30)


def bush():
    rng = np.random.default_rng(31)
    light, dark = rgb(70, 150, 96), rgb(24, 80, 50)

    def colfun(P):
        h = np.clip(P[:, 2] / 0.8, 0, 1)
        return T.lerp(dark, light, np.clip(h * 1.3 - 0.1, 0, 1))
    objs = []
    for c, r in (((0.5, 0.5, 0.36), 0.3), ((0.28, 0.46, 0.26), 0.22), ((0.72, 0.44, 0.26), 0.22),
                 ((0.5, 0.7, 0.3), 0.24), ((0.5, 0.3, 0.24), 0.2)):
        objs.append(L.blob('b', 'f_vcol_leaf', c, r, rng, amp=0.15, freq=3.5, subdiv=3, colfun=colfun,
                           flat_below=0.0))
    for k in range(9):
        t = rng.uniform(0, 2 * math.pi)
        z = rng.uniform(0.25, 0.55)
        objs.append(L.blob('berry', 'f_berry', (0.5 + 0.3 * math.cos(t) * 0.95, 0.5 + 0.28 * math.sin(t) * 0.95, z),
                           0.03, rng, amp=0, subdiv=1))
    return objs, 0.66


def rings_disc(name, cx, cy, cz, r, normal):
    """A flat disc with the growth-ring texture facing +X, -Y or +Z."""
    n = 28
    vs, fs, uvs = [(0, 0, 0)], [], []
    for i in range(n):
        t = 2 * math.pi * i / n
        vs.append((r * math.cos(t), r * math.sin(t), 0.0))
    for i in range(n):
        a0, b0 = 1 + i, 1 + (i + 1) % n
        fs.append((0, a0, b0))
        for k in (0, a0, b0):
            uvs.append((0.5 + vs[k][0] / r * 0.5, 0.5 + vs[k][1] / r * 0.5))
    ob = L.mesh(name, vs, fs, 'f_logring', uvs=uvs, recalc=False)
    from mathutils import Matrix
    R = {'+z': Matrix.Identity(4), '+x': Matrix.Rotation(math.pi / 2, 4, 'Y'),
         '-y': Matrix.Rotation(math.pi / 2, 4, 'X')}[normal]
    L.transform(ob, Matrix.Translation((cx, cy, cz)) @ R)
    return ob


def _log_mats():
    if 'f_logbark' not in C._MATS:
        L.tile_mat('f_logbark', bark_tex(), 'uv', rough=0.85, spec=0.2, bump=1.0)
        L.tile_mat('f_logring', rings_tex(), 'uv', rough=0.7, spec=0.25, bump=1.0)


def stump():
    rng = np.random.default_rng(32)
    _log_mats()
    prof = [(0.34, 0.0), (0.27, 0.06), (0.24, 0.15), (0.23, 0.44), (0.0, 0.44)]
    objs = [L.lathe('stump', 'f_bark', prof, n=28, rfun=lambda th, z: 1 + 0.05 * math.cos(9 * th), cap_top=False)]
    objs.append(rings_disc('top', 0.5, 0.5, 0.441, 0.225, '+z'))
    for k in range(4):
        t = 2 * math.pi * k / 4 + 0.6
        d = np.array([math.cos(t), math.sin(t), 0])
        p = [np.array([0.5, 0.5, 0.2]) + d * 0.2, np.array([0.5, 0.5, 0.06]) + d * 0.32, np.array([0.5, 0.5, 0.0]) + d * 0.42]
        objs.append(L.tube('root', 'f_bark', L.catmull(p, 6), np.linspace(0.07, 0.025, 6), n=8))
    objs += mushroom_one('sm', 0.76, 0.34, 0.26, (0.02, -0.02), rng, 2)
    return objs, 0.5


def fallen_log(along):
    """A fallen log lying on the ground over two cells (along X or Y), a
    sawn end facing the camera, a branch stub, moss on top."""
    _log_mats()
    r = 0.25
    x0, x1 = 0.08, 1.92
    nth = 32
    xs = np.linspace(x0, x1, 17)
    verts, faces, uvs = [], [], []
    th = np.linspace(0, 2 * math.pi, nth + 1)
    for x in xs:
        for t in th:
            rib = 1 + 0.03 * math.cos(11 * t)
            verts.append((x, 0.5 + r * rib * math.cos(t), r * 0.92 + r * rib * math.sin(t)))
    m = nth + 1
    for i in range(len(xs) - 1):
        for j in range(nth):
            a0 = i * m + j
            faces.append((a0, a0 + m, a0 + m + 1, a0 + 1))
            uvs += [(xs[i], th[j] / (2 * math.pi)), (xs[i + 1], th[j] / (2 * math.pi)),
                    (xs[i + 1], th[j + 1] / (2 * math.pi)), (xs[i], th[j + 1] / (2 * math.pi))]
    objs = [L.mesh('log', verts, faces, 'f_logbark', uvs=uvs, smooth=True)]
    objs.append(rings_disc('end1', x1 + 0.001, 0.5, r * 0.92, r * 0.98, '+x'))
    objs.append(rings_disc('end0', x0 - 0.001, 0.5, r * 0.92, r * 0.98, '+x'))
    objs.append(L.tube('stub', 'f_bark', [(0.7, 0.5, 0.35), (0.62, 0.34, 0.62), (0.6, 0.3, 0.7)], [0.06, 0.045, 0.03], n=8))
    rng = np.random.default_rng(33)
    objs += mushroom_one('lm', 1.35, 0.3, 0.22, (0.0, -0.02), rng, 2)
    if along == 'y':
        for ob in objs:
            L.rotate_z(ob, -90, center=(0.5, 0.5))
            # rotated to run from y = 1.5 down to -0.5: shift into cells (0, 0..1)
            for v in ob.data.vertices:
                v.co.y += 1.0
            ob.data.update()
    return objs, 0.5


def mossy_rock():
    rng = np.random.default_rng(34)
    grey, grey2, moss = rgb(140, 146, 148), rgb(112, 118, 122), rgb(84, 150, 70)

    def colfun(P):
        h = P[:, 2]
        nz = np.sin(P @ np.array([13.1, 9.7, 11.3])) * 0.5 + 0.5
        c = T.lerp(grey2, grey, np.clip(h / 0.5 + 0.2 * nz, 0, 1))
        return T.lerp(c, moss, np.clip((h - 0.32) * 8 + (nz - 0.5) * 1.5, 0, 1))
    return [L.blob('rk', 'f_vcol_rock', (0.5, 0.52, 0.22), 1.0, rng, amp=0.2, freq=2.2, subdiv=4,
                   scale=(0.38, 0.33, 0.34), flat_below=0.0, colfun=colfun),
            L.blob('rk2', 'f_vcol_rock', (0.78, 0.32, 0.08), 1.0, rng, amp=0.2, freq=3, subdiv=3,
                   scale=(0.13, 0.11, 0.12), flat_below=0.0, colfun=colfun)], 0.58


def cabin():
    """The woodcutter's cabin, 2 x 2: log walls, a shingle roof along X, a
    stone chimney, a door and warm lit windows on the front and right."""
    rng = np.random.default_rng(35)
    B = lambda n, k, *c: C.box(n, *c, k)  # noqa: E731
    objs = [B('found', 'f_stone', 0.1, 0.15, 0.0, 1.9, 1.85, 0.2)]
    W, z0, z1 = 0.13, 0.2, 1.45
    objs += [B('wf', 'f_logwall', 0.18, 0.22, z0, 1.82, 0.22 + W, z1),
             B('wb', 'f_logwall', 0.18, 1.78 - W, z0, 1.82, 1.78, z1),
             B('wl', 'f_logwall', 0.18, 0.22, z0, 0.18 + W, 1.78, z1),
             B('wr', 'f_logwall', 1.82 - W, 0.22, z0, 1.82, 1.78, z1)]
    # log ends poking out at the corners
    for (cx, cy) in ((0.18, 0.22), (1.82, 0.22), (1.82, 1.78)):
        for k in range(6):
            z = z0 + 0.1 + k * 0.2
            objs.append(L.tube('le', 'f_darkwood', [(cx - 0.06, cy, z), (cx + 0.06, cy, z)], [0.055, 0.055], n=10))
    # windows: front and right, each glass + frame cross + a flower box
    wins = []
    g = B('glass_f', 'f_win', 0.5, 0.2, 0.62, 0.86, 0.23, 1.02)
    wins.append(g)
    objs += [g, B('fr1', 'f_darkwood', 0.47, 0.19, 0.6, 0.89, 0.215, 0.64),
             B('fr2', 'f_darkwood', 0.47, 0.19, 1.0, 0.89, 0.215, 1.04),
             B('fr3', 'f_darkwood', 0.665, 0.188, 0.62, 0.695, 0.21, 1.02),
             B('fr4', 'f_darkwood', 0.5, 0.188, 0.805, 0.86, 0.21, 0.835),
             B('box', 'f_darkwood', 0.47, 0.13, 0.52, 0.89, 0.22, 0.6)]
    g2 = B('glass_r', 'f_win', 1.8, 0.7, 0.62, 1.83, 1.12, 1.02)
    wins.append(g2)
    objs += [g2, B('fr5', 'f_darkwood', 1.805, 0.67, 0.6, 1.83, 1.15, 0.64),
             B('fr6', 'f_darkwood', 1.805, 0.67, 1.0, 1.83, 1.15, 1.04),
             B('fr7', 'f_darkwood', 1.812, 0.895, 0.62, 1.834, 0.925, 1.02),
             B('box2', 'f_darkwood', 1.8, 0.67, 0.52, 1.88, 1.15, 0.6)]
    for k in range(5):
        for (bx, by) in ((0.52 + k * 0.085, 0.175), (1.84, 0.71 + k * 0.1)):
            objs.append(L.blob('fl', ['f_petal_p', 'f_petal_y', 'f_petal_w'][k % 3], (bx, by, 0.63), 0.035, rng,
                               amp=0.1, subdiv=1))
    objs += [B('door', 'f_door', 1.12, 0.195, 0.2, 1.46, 0.23, 1.22),
             B('doorframe', 'f_darkwood', 1.09, 0.19, 1.2, 1.49, 0.225, 1.26),
             B('step', 'f_stone', 1.06, 0.06, 0.0, 1.52, 0.22, 0.12)]
    objs.append(L.blob('knob', 'f_steel', (1.42, 0.188, 0.72), 0.02, rng, amp=0, subdiv=1))
    # the roof: two slopes along X, overhanging, with gable ends
    ridge, eave = 2.35, 1.38
    for sy in (-1, 1):
        vs = [(0.05, 1.0, ridge), (1.95, 1.0, ridge), (1.95, 1.0 + sy * 0.95, eave), (0.05, 1.0 + sy * 0.95, eave)]
        roof = L.mesh('roof', vs, [(0, 1, 2, 3)], 'f_shingle')
        L.solidify(roof, 0.06)
        objs.append(roof)
    for xe in (0.18, 1.82):
        objs.append(L.mesh('gable', [(xe, 0.22, z1), (xe, 1.78, z1), (xe, 1.0, ridge - 0.04)], [(0, 1, 2)], 'f_boards_y'))
    objs.append(L.tube('ridge', 'f_darkwood', [(0.03, 1.0, ridge + 0.03), (1.97, 1.0, ridge + 0.03)], [0.04, 0.04], n=8))
    objs.append(B('chim', 'f_stone', 0.34, 1.22, 1.5, 0.6, 1.48, 2.62))
    objs.append(B('chimcap', 'f_darkwood', 0.32, 1.2, 2.62, 0.62, 1.5, 2.66))
    for ob in wins:
        ob.visible_shadow = False
    lamps = [C.add_point_light((0.68, 0.55, 0.85), color=(1.0, 0.64, 0.3), power=26.0, radius=0.1, name='in1'),
             C.add_point_light((1.5, 0.9, 0.85), color=(1.0, 0.64, 0.3), power=26.0, radius=0.1, name='in2')]
    return objs, 2.66, lamps


def fence(along):
    """A wooden rail fence section across the cell (rails end at the cell's
    edges so sections join), one post in the middle."""
    objs = [C.box('post', 0.45, 0.45, 0.0, 0.55, 0.55, 0.86, 'f_wood'),
            L.mesh('ptop', [(0.45, 0.45, 0.86), (0.55, 0.45, 0.86), (0.55, 0.55, 0.86), (0.45, 0.55, 0.86), (0.5, 0.5, 0.93)],
                   [(0, 1, 4), (1, 2, 4), (2, 3, 4), (3, 0, 4)], 'f_wood')]
    for z, sag in ((0.34, 0.01), (0.68, 0.015)):
        objs.append(L.tube('rail', 'f_wood', L.catmull([(0.0, 0.5, z), (0.25, 0.5, z - sag), (0.5, 0.5, z),
                                                        (0.75, 0.5, z - sag), (1.0, 0.5, z)], 12), [0.035] * 12, n=8))
    if along == 'y':
        for ob in objs:
            L.rotate_z(ob, 90)
    return objs, 0.93


def flowers():
    rng = np.random.default_rng(36)
    objs = []
    for k in range(6):
        t = rng.uniform(0, 2 * math.pi)
        objs.append(L.blob('lf', 'f_leaf', (0.5 + 0.18 * math.cos(t), 0.5 + 0.15 * math.sin(t), 0.03), 0.07, rng,
                           amp=0.1, subdiv=2, scale=(1.4, 0.7, 0.35)))
    keys = ['f_petal_w', 'f_petal_l', 'f_petal_y', 'f_petal_w', 'f_petal_l', 'f_petal_p', 'f_petal_y', 'f_petal_w', 'f_petal_l']
    for k, key in enumerate(keys):
        cx, cy = 0.5 + rng.uniform(-0.25, 0.25), 0.5 + rng.uniform(-0.22, 0.22)
        h = rng.uniform(0.18, 0.34)
        objs.append(L.tube('stem', 'f_leaf', [(cx, cy, 0.0), (cx + 0.02, cy, h)], [0.008, 0.006], n=5))
        for pk in range(5):
            t = 2 * math.pi * pk / 5
            objs.append(L.blob('pet', key, (cx + 0.02 + 0.026 * math.cos(t), cy + 0.026 * math.sin(t), h), 0.022, rng,
                               amp=0, subdiv=1, scale=(1, 1, 0.45)))
        objs.append(L.blob('mid', 'f_petal_y' if key != 'f_petal_y' else 'f_door', (cx + 0.02, cy, h + 0.008), 0.014, rng,
                           amp=0, subdiv=1))
    return objs, 0.36


def fern():
    rng = np.random.default_rng(37)
    objs = []
    nf = 9
    for i in range(nf):
        phi = 2 * math.pi * i / nf + rng.uniform(-0.2, 0.2)
        Lf = rng.uniform(0.45, 0.58)
        up, droop = rng.uniform(1.0, 1.4), rng.uniform(1.0, 1.4)
        n = 22
        t = np.linspace(0, 1, n)
        h = np.array([math.cos(phi), math.sin(phi), 0.0])
        spine = np.array([0.5, 0.5, 0.0])[None, :] + np.outer(Lf * t * 0.8, h) + np.outer(Lf * (up * t - droop * t * t), [0, 0, 1])
        tan = np.gradient(spine, axis=0)
        tan /= np.linalg.norm(tan, axis=1, keepdims=True)
        side = np.cross(tan, [0, 0, 1])
        side /= np.linalg.norm(side, axis=1, keepdims=True) + 1e-9
        w = 0.09 * np.sin(math.pi * t) ** 0.8 * (0.55 + 0.45 * np.abs(np.cos(math.pi * 11 * t)))
        verts, faces, vc = [], [], []
        c0, c1 = rgb(46, 120, 66), rgb(110, 186, 104)
        for k in range(n):
            dz = np.array([0, 0, -0.35 * w[k]])
            verts += [tuple(spine[k] - side[k] * w[k] + dz), tuple(spine[k] + np.array([0, 0, 0.006])),
                      tuple(spine[k] + side[k] * w[k] + dz)]
            ck = T.lerp(c0, c1, t[k])
            vc += [ck * 0.8, ck, ck * 0.8]
        for k in range(n - 1):
            a0 = 3 * k
            faces += [(a0, a0 + 1, a0 + 4, a0 + 3), (a0 + 1, a0 + 2, a0 + 5, a0 + 4)]
        objs.append(L.mesh('frond%d' % i, verts, faces, 'f_vcol_leaf', smooth=True, cols=vc, recalc=False))
    return objs, 0.5


def crescent(name, key, c, r, face):
    """A crescent moon polygon on a face ('-y' or '+x') centred at c."""
    pts = []
    for t in np.linspace(-math.pi / 2, math.pi / 2, 12):
        pts.append((r * math.cos(t), r * math.sin(t)))
    for t in np.linspace(math.pi / 2, -math.pi / 2, 12):
        pts.append((r * 0.35 + r * 0.72 * math.cos(t) * 0.8, r * 0.85 * math.sin(t)))
    vs = []
    for u, v in pts:
        if face == '-y':
            vs.append((c[0] + u, c[1], c[2] + v))
        else:
            vs.append((c[0], c[1] - u, c[2] + v))
    cen = len(vs)
    faces = []
    for i in range(11):
        faces.append((i, i + 1, 23 - i - 1, 23 - i))
    return L.mesh(name, vs, faces, key, recalc=False)


def shrine():
    """A small stone moon shrine: a lantern-like stone box on a post, a
    crescent moon cut in it glowing pale silver."""
    rng = np.random.default_rng(38)
    objs = [C.box('base', 0.22, 0.22, 0.0, 0.78, 0.78, 0.12, 'f_stone'),
            C.box('base2', 0.3, 0.3, 0.12, 0.7, 0.7, 0.2, 'f_stone'),
            C.box('post', 0.42, 0.42, 0.2, 0.58, 0.58, 0.66, 'f_stone'),
            C.box('box', 0.31, 0.31, 0.66, 0.69, 0.69, 1.0, 'f_stone')]
    objs.append(L.mesh('roof', [(0.24, 0.24, 1.0), (0.76, 0.24, 1.0), (0.76, 0.76, 1.0), (0.24, 0.76, 1.0), (0.5, 0.5, 1.2)],
                       [(0, 1, 4), (1, 2, 4), (2, 3, 4), (3, 0, 4), (3, 2, 1, 0)], 'f_stone'))
    objs.append(L.blob('knob', 'f_stone', (0.5, 0.5, 1.22), 0.035, rng, amp=0, subdiv=2))
    objs.append(crescent('moon_f', 'f_moon', (0.49, 0.308, 0.83), 0.1, '-y'))
    objs.append(crescent('moon_r', 'f_moon', (0.692, 0.51, 0.83), 0.1, '+x'))
    for k in range(5):
        t = rng.uniform(0, 2 * math.pi)
        objs.append(L.blob('moss', 'f_leaf', (0.5 + 0.28 * math.cos(t), 0.5 + 0.28 * math.sin(t), 0.12), 0.04, rng,
                           amp=0.2, subdiv=2, scale=(1.3, 1.3, 0.5)))
    return objs, 1.24


def mill():
    """A water-mill, 2 x 2: stone ground storey, plank upper storey, roof
    along Y, and the big wheel on its right (+X) side."""
    from mathutils import Matrix
    rng = np.random.default_rng(39)
    B = lambda n, k, *c: C.box(n, *c, k)  # noqa: E731
    objs = [B('stone', 'f_stone', 0.12, 0.2, 0.0, 1.5, 1.8, 0.7),
            B('boards', 'f_boards', 0.16, 0.24, 0.7, 1.46, 1.76, 1.6),
            B('door', 'f_door', 0.55, 0.19, 0.0, 0.9, 0.21, 0.62),
            B('win', 'f_darkwood', 0.95, 0.225, 0.95, 1.25, 0.245, 1.3),
            B('win2', 'f_darkwood', 1.455, 0.6, 0.95, 1.475, 0.95, 1.3)]
    ridge, eave = 2.5, 1.55
    for sx in (-1, 1):
        vs = [(0.81, 0.1, ridge), (0.81, 1.9, ridge), (0.81 + sx * 0.8, 1.9, eave), (0.81 + sx * 0.8, 0.1, eave)]
        roof = L.mesh('roof', vs, [(0, 1, 2, 3)], 'f_shingle')
        L.solidify(roof, 0.06)
        objs.append(roof)
    for ye in (0.24, 1.76):
        objs.append(L.mesh('gable', [(0.16, ye, 1.6), (1.46, ye, 1.6), (0.81, ye, ridge - 0.04)], [(0, 1, 2)], 'f_boards'))
    # the wheel (axis along X) at x = 1.72, centre (1.0, 0.78)
    cx, cy, cz, R = 1.72, 1.0, 0.8, 0.72
    for dx in (-0.1, 0.1):
        th = np.linspace(0, 2 * math.pi, 37)
        ring = [(cx + dx, cy + R * math.cos(t), cz + R * math.sin(t)) for t in th]
        objs.append(L.tube('rim', 'f_darkwood', ring, [0.035] * len(ring), n=8, cap=False))
    for k in range(8):
        t = 2 * math.pi * k / 8
        objs.append(L.tube('spoke', 'f_wood', [(cx, cy, cz), (cx, cy + R * math.cos(t), cz + R * math.sin(t))],
                           [0.025, 0.025], n=6))
    for k in range(16):
        t = 2 * math.pi * k / 16
        pad = C.box('pad', -0.12, -0.02, -0.1, 0.12, 0.02, 0.1, 'f_wood')
        L.transform(pad, Matrix.Translation((cx, cy + (R - 0.05) * math.cos(t), cz + (R - 0.05) * math.sin(t)))
                    @ Matrix.Rotation(t, 4, 'X'))
        objs.append(pad)
    objs.append(L.tube('axle', 'f_steel', [(1.46, cy, cz), (cx + 0.16, cy, cz)], [0.05, 0.05], n=10))
    return objs, 2.5


# ---------------------------------------------------------------------------
# moving things
# ---------------------------------------------------------------------------

LILY_POSE = [(0.0, 0.0), (-0.02, 6.0), (-0.05, 12.0), (-0.1, 18.0)]


def lily(frame):
    from mathutils import Matrix
    rng = np.random.default_rng(40)
    n = 40
    R = 0.4
    vs, fs, cols = [(0.5, 0.5, 0.0)], [], []
    notch = 0.32
    for i in range(n + 1):
        t = notch / 2 + (2 * math.pi - notch) * i / n
        rr = R * (1 + 0.02 * math.sin(7 * t))
        vs.append((0.5 + rr * math.cos(t - math.pi / 2 - 0.9), 0.5 + rr * math.sin(t - math.pi / 2 - 0.9), 0.0))
    for i in range(n):
        fs.append((0, 1 + i, 2 + i))
    pad = L.mesh('pad', vs, fs, 'f_lily', recalc=False)
    L.solidify(pad, 0.025)
    objs = [pad]
    for k in range(7):
        t = 2 * math.pi * k / 7 + 0.2
        objs.append(L.tube('vein', 'f_leaf', [(0.5, 0.5, 0.004), (0.5 + 0.33 * math.cos(t), 0.5 + 0.33 * math.sin(t), 0.004)],
                           [0.006, 0.004], n=4))
    for k in range(8):
        t = 2 * math.pi * k / 8
        objs.append(L.blob('pet', 'f_petal_p', (0.64 + 0.045 * math.cos(t), 0.62 + 0.045 * math.sin(t), 0.05), 0.045, rng,
                           amp=0, subdiv=2, scale=(1.2, 0.6, 0.5)))
    objs.append(L.blob('mid', 'f_petal_y', (0.64, 0.62, 0.07), 0.03, rng, amp=0, subdiv=2))
    dz, tilt = LILY_POSE[frame]
    M = Matrix.Translation((0.5, 0.5, dz)) @ Matrix.Rotation(math.radians(tilt), 4, 'X') @ Matrix.Translation((-0.5, -0.5, 0))
    for ob in objs:
        L.transform(ob, M)
    if frame > 0:
        th = np.linspace(0, 2 * math.pi, 41)
        ring = [(0.5 + (R + 0.03) * math.cos(t), 0.5 + (R + 0.03) * math.sin(t), 0.004) for t in th]
        objs.append(L.tube('ripple', 'f_foam', ring, [0.01] * len(ring), n=6, cap=False))
    return objs


def beartrap(frame):
    """A cartoon trap: an orange pan, two steel jaws with round blunt teeth,
    open flat (00) or snapped shut upright (01). No blood, no spikes."""
    from mathutils import Matrix
    rng = np.random.default_rng(41)
    objs = [L.lathe('plate', 'f_steel', [(0.0, 0.0), (0.12, 0.0), (0.12, 0.03), (0.0, 0.03)], n=24),
            L.lathe('pan', 'f_pan', [(0.0, 0.03), (0.09, 0.03), (0.09, 0.045), (0.0, 0.05)], n=24),
            L.tube('hinge', 'f_steel', [(0.2, 0.5, 0.03), (0.8, 0.5, 0.03)], [0.022, 0.022], n=8)]
    R = 0.27
    for side in (-1, 1):
        jaw = []
        th = np.linspace(0, math.pi, 25)
        arc = [(0.5 + R * math.cos(t), 0.5 + side * R * math.sin(t), 0.03) for t in th]
        jaw.append(L.tube('jaw', 'f_steel', arc, [0.026] * len(arc), n=8, cap=True))
        for k in range(7):
            t = math.pi * (k + 0.5) / 7
            jaw.append(L.blob('tooth', 'f_steel', (0.5 + (R - 0.03) * math.cos(t), 0.5 + side * (R - 0.03) * math.sin(t), 0.055),
                              0.03, rng, amp=0, subdiv=2, scale=(0.8, 0.8, 1.3)))
        if frame == 1:
            M = Matrix.Translation((0.5, 0.5, 0.03)) @ Matrix.Rotation(side * math.radians(82), 4, 'X') @ \
                Matrix.Translation((-0.5, -0.5, -0.03))
            for ob in jaw:
                L.transform(ob, M)
        objs += jaw
    # a chain to a stake
    for k in range(4):
        c = (0.84 + k * 0.035, 0.36 - k * 0.02, 0.02)
        objs.append(L.lathe('link', 'f_steel', [(0.012, -0.01), (0.02, 0.0), (0.012, 0.01)], n=10, center=(c[0], c[1])))
        L.transform(objs[-1], Matrix.Translation((0, 0, c[2])))
    objs.append(L.tube('stake', 'f_wood', [(0.95, 0.28, 0.0), (0.95, 0.28, 0.12)], [0.025, 0.02], n=6))
    return objs


GATE_ANG = [0, 15, 32, 50, 66, 80, 90]


def forest_gate(frame):
    """The exit: two log posts, an arch of bent branches with a silver
    crescent, two woven-branch leaves swinging back (+Y); 06 open with a soft
    silver glow."""
    from mathutils import Matrix
    rng = np.random.default_rng(42)
    objs = []
    for x in (0.1, 0.9):
        objs.append(L.tube('post', 'f_bark', [(x, 0.5, 0.0), (x, 0.5, 1.8)], [0.075, 0.065], n=12))
    arch = L.catmull([(0.1, 0.5, 1.7), (0.2, 0.5, 2.02), (0.5, 0.5, 2.16), (0.8, 0.5, 2.02), (0.9, 0.5, 1.7)], 20)
    objs.append(L.tube('arch', 'f_bark', arch, [0.05] * 20, n=10))
    arch2 = arch + np.array([0, 0.0, -0.07])
    objs.append(L.tube('arch2', 'f_bark', arch2 * np.array([1, 1, 1]) + np.array([0, 0.03, 0]), [0.03] * 20, n=8))
    moon = crescent('moon', 'f_silver', (0.5, 0.44, 2.2), 0.13, '-y')
    L.solidify(moon, 0.02)
    objs.append(moon)
    ang = math.radians(GATE_ANG[frame])
    for side in (-1, 1):
        hx = 0.5 + side * 0.4                         # the hinge at the post
        leaf = []
        w = 0.4
        x_in = -side * w                              # towards the middle, in the hinge frame
        for z in (0.12, 1.5):
            leaf.append(L.tube('rail', 'f_bark', [(0, 0, z), (x_in, 0, z)], [0.03, 0.03], n=6))
        leaf.append(L.tube('stile', 'f_bark', [(x_in, 0, 0.12), (x_in, 0, 1.5)], [0.03, 0.03], n=6))
        for k in range(6):
            z = 0.28 + k * 0.21
            pts = [(x_in * i / 10, 0.025 * math.sin(i * math.pi / 2 + k), z + 0.02 * math.sin(i * 1.3)) for i in range(11)]
            leaf.append(L.tube('weave', 'f_wood', pts, [0.018] * 11, n=6))
        for k in range(3):
            x = x_in * (k + 1) / 4
            leaf.append(L.tube('stake', 'f_wood', [(x, 0.01, 0.12), (x, 0.01, 1.58)], [0.016, 0.012], n=6))
        # swing about the hinge's vertical axis, the free end going to +Y
        M = Matrix.Translation((hx, 0.5, 0.0)) @ Matrix.Rotation(side * ang, 4, 'Z')
        for ob in leaf:
            L.transform(ob, M)
        objs += leaf
    lamps = []
    if frame == 6:
        vs = [(0.16, 0.52, 0.0), (0.84, 0.52, 0.0), (0.84, 0.52, 1.9), (0.16, 0.52, 1.9)]
        objs.append(L.mesh('veil', vs, [(0, 1, 2, 3)], 'f_gateveil', cols=[(1, 0, 0), (1, 0, 0), (0.1, 0, 0), (0.1, 0, 0)],
                           recalc=False))
        lamps.append(C.add_point_light((0.5, 0.55, 0.9), color=(0.75, 0.85, 1.0), power=26.0, radius=0.3, name='gate'))
    return objs, lamps


def wood_crate():
    """A wooden crate, one floor tall: frame battens, board panels, diagonal
    braces on every visible face."""
    Hh = C.FLOOR_M
    m, b = 0.03, 0.06
    objs = [C.box('core', m + 0.02, m + 0.02, 0.02, 1 - m - 0.02, 1 - m - 0.02, Hh - 0.02, 'f_boards')]
    x0, x1, y0, y1, z0, z1 = m, 1 - m, m, 1 - m, 0.0, Hh
    # the 12 edges
    for (a0, a1) in (((x0, y0), (x1, y0)), ((x0, y1), (x1, y1))):
        for z in (z0, z1 - b):
            objs.append(C.box('ex', a0[0], a0[1], z, a1[0], a0[1] + b, z + b, 'f_wood'))
    for x in (x0, x1 - b):
        for z in (z0, z1 - b):
            objs.append(C.box('ey', x, y0, z, x + b, y1, z + b, 'f_wood'))
    for x in (x0, x1 - b):
        for y in (y0, y1 - b):
            objs.append(C.box('ez', x, y, z0, x + b, y + b, z1, 'f_wood'))
    # braces: front (-Y) and right (+X) and top
    objs.append(L.tube('bf', 'f_wood', [(x0 + b, y0 - 0.005, z0 + b), (x1 - b, y0 - 0.005, z1 - b)], [0.028, 0.028], n=4))
    objs.append(L.tube('br', 'f_wood', [(x1 + 0.005, y0 + b, z0 + b), (x1 + 0.005, y1 - b, z1 - b)], [0.028, 0.028], n=4))
    objs.append(L.tube('bt', 'f_wood', [(x0 + b, y0 + b, z1 + 0.005), (x1 - b, y1 - b, z1 + 0.005)], [0.028, 0.028], n=4))
    return objs


def plank_bridge(direction):
    """A walkable deck at z = 0: five boards across the way you walk, two
    stringers under them running with it (they join the next bridge cell)."""
    objs = []
    for k in range(5):
        x = 0.02 + k * 0.196
        objs.append(C.box('board', x, 0.06, -0.05, x + 0.17, 0.94, 0.0, 'f_wood'))
    for y in (0.16, 0.84):
        objs.append(C.box('stringer', 0.0, y - 0.05, -0.16, 1.0, y + 0.05, -0.05, 'f_darkwood'))
    for k in range(5):
        x = 0.02 + k * 0.196 + 0.085
        for y in (0.16, 0.84):
            objs.append(C.box('nail', x - 0.012, y - 0.012, 0.0, x + 0.012, y + 0.012, 0.004, 'f_iron'))
    if direction == 'y':
        for ob in objs:
            L.rotate_z(ob, 90)
    return objs


# ---------------------------------------------------------------------------

PROPS = [('pine', pine, None), ('oak', oak, {'canopy_overhang_m': 0.3}), ('bush', bush, None),
         ('stump', stump, None), ('log_x', lambda: fallen_log('x'), {'footprint': [2, 1]}),
         ('log_y', lambda: fallen_log('y'), {'footprint': [1, 2]}), ('rock', mossy_rock, None),
         ('fence_x', lambda: fence('x'), {'along': 'x'}), ('fence_y', lambda: fence('y'), {'along': 'y'}),
         ('flowers', flowers, None), ('fern', fern, None), ('shrine', shrine, None),
         ('mill', mill, {'footprint': [2, 2]})]
LIT = [('mushroom', mushroom, ((0.5, 0.52), 1.7), None), ('lantern', lantern, ((0.66, 0.5), 2.0), None),
       ('cabin', cabin, ((1.0, 0.7), 2.0), {'footprint': [2, 2]})]


def run(a, sample):
    _mats()
    _mats2()
    for nm, fn, ex in PROPS:
        name = '%s_%s' % (Z, nm)
        if L.wanted(a, name, sample):
            objs, hm = fn()
            L.render_prop(a, name, objs, hm, extra=ex)
    for nm, fn, glow, ex in LIT:
        name = '%s_%s' % (Z, nm)
        if L.wanted(a, name, sample):
            objs, hm, lamps = fn()
            L.render_prop(a, name, objs, hm, glow=glow, lights=lamps, extra=ex)
    for part in ('w', 'm', 'e'):
        name = '%s_logfloat_%s' % (Z, part)
        if L.wanted(a, name, sample):
            L.sprite(a, name, logfloat(part), shadow_z=WATER,
                     extra=dict(part=part, float_z=FLOAT_Z, height_m=0.2, footprint=[1, 1]))
    for f in range(4):
        name = '%s_lily_%02d' % (Z, f)
        if L.wanted(a, name, sample):
            L.sprite(a, name, lily(f), shadow_z=-0.01,
                     extra=dict(anim='sink', frame=f, frames=4, float_z=-0.17, standable=(f == 0),
                                note='anchor = the pad top; the watch puts it 1 cm above the water'))
    for f in range(2):
        name = '%s_beartrap_%02d' % (Z, f)
        if L.wanted(a, name, sample):
            L.sprite(a, name, beartrap(f), extra=dict(state=['open', 'snapped'][f], height_m=0.3 if f else 0.08))
    for f in range(7):
        name = '%s_gate_%02d' % (Z, f)
        if L.wanted(a, name, sample):
            objs, lamps = forest_gate(f)
            L.sprite(a, name, objs, glow=((0.5, 0.6), 1.6) if f == 6 else None, lights=lamps,
                     extra=dict(anim='open', frame=f, frames=7, height_m=2.33, exit=True,
                                states={'0': 'closed', '6': 'open'}))
    name = '%s_crate' % Z
    if L.wanted(a, name, sample):
        L.sprite(a, name, wood_crate(), extra=dict(footprint=[1, 1], height_m=round(C.FLOOR_M, 4), pushable=True))
    for d in ('x', 'y'):
        name = '%s_bridge_%s' % (Z, d)
        if L.wanted(a, name, sample):
            L.sprite(a, name, plank_bridge(d), kind='bridge', shadow_z=-0.18,
                     extra=dict(walk=d, deck_z=0.0, over='forest_surf_river'))
