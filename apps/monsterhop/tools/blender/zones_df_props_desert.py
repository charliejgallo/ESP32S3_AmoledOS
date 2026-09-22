"""Monster Hop - Mummy Desert props (procedural meshes), used by desert.py.

Every prop is built on cell (0, 0), floor 0 (its base centred on (0.5, 0.5)),
chunky and chibi so it reads at 63 px per metre.
"""
import math

import numpy as np

import mh_common as C
import zones_df_lib as L
import zones_df_tex as T
from zones_df_tex import rgb

Z = 'desert'


def catmull(ctrl, n):
    c = np.asarray(ctrl, float)
    P = np.vstack([2 * c[0] - c[1], c, 2 * c[-1] - c[-2]])
    out = []
    segs = len(c) - 1
    for i in range(n):
        t = i / (n - 1) * segs
        s = min(int(t), segs - 1)
        u = t - s
        p0, p1, p2, p3 = P[s], P[s + 1], P[s + 2], P[s + 3]
        out.append(0.5 * ((2 * p1) + (-p0 + p2) * u + (2 * p0 - 5 * p1 + 4 * p2 - p3) * u * u
                          + (-p0 + 3 * p1 - 3 * p2 + p3) * u ** 3))
    return np.array(out)


def _mats():
    if getattr(_mats, 'done', False):
        return
    _mats.done = True
    L.attr_mat('d_vcol', rough=0.75, spec=0.25)
    L.attr_mat('d_vcol_leaf', rough=0.6, spec=0.35)
    L.attr_mat('d_vcol_bark', rough=0.9, spec=0.15, bump_noise=0.4, bump_scale=60)
    L.noise_mat('d_cactus', (62, 150, 78), (96, 178, 92), scale=14, rough=0.55, spec=0.35, bump=0.25)
    L.flat('d_cactus_flower', (244, 92, 150), rough=0.5)
    L.flat('d_flower_mid', (255, 214, 60), rough=0.5)
    L.flat('d_coconut', (120, 76, 42), rough=0.6)
    L.noise_mat('d_lime', (228, 214, 180), (208, 192, 158), scale=10, rough=0.85, spec=0.2, bump=0.2)
    L.flat('d_gold', (255, 196, 70), rough=0.28, metal=1.0, spec=0.5)
    L.noise_mat('d_bronze', (170, 104, 50), (120, 72, 36), scale=18, rough=0.38, metal=0.9, spec=0.5)
    L.flat('d_coal', (40, 20, 12), rough=0.8, emit=(255, 90, 20), emit_strength=2.5)
    L.emit_attr_mat('d_flame', strength=2.6)
    L.emit_attr_mat('d_flame_core', strength=3.2)
    L.noise_mat('d_ash', (70, 50, 40), (40, 28, 22), scale=30, rough=0.9)


# ---------------------------------------------------------------------------
# palm
# ---------------------------------------------------------------------------

def palm():
    rng = np.random.default_rng(3)
    H = 2.5
    ctrl = [(0.5, 0.5, 0.0), (0.51, 0.5, 0.7), (0.45, 0.53, 1.5), (0.36, 0.56, 2.1), (0.31, 0.58, H)]
    N = 170
    path = catmull(ctrl, N)
    s = np.linspace(0, 1, N)
    rings = 16
    f = np.mod(s * rings, 1.0)
    radii = 0.085 + 0.04 * (1 - s) ** 1.5 + 0.06 * (1 - s) ** 14
    radii = radii * (1 + 0.16 * f ** 2.5)                           # each ring flares at its top
    c_lo, c_hi = rgb(128, 86, 56), rgb(196, 150, 100)
    cols = [T.lerp(c_lo, c_hi, 0.25 + 0.75 * fi ** 3) * (0.85 + 0.15 * si) for fi, si in zip(f, s)]
    objs = [L.tube('palm_trunk', 'd_vcol_bark', path, radii, n=14, cols=cols)]
    top = path[-1] + np.array([0.0, 0.0, 0.03])
    # fronds: V-shaped combs arching out and drooping
    nf = 9
    for i in range(nf):
        phi = 2 * math.pi * i / nf + rng.uniform(-0.2, 0.2) + 0.3
        Lf = rng.uniform(0.76, 0.9)
        up = rng.uniform(0.25, 0.55) if i % 3 else rng.uniform(0.55, 0.8)
        droop = rng.uniform(0.75, 1.05)
        n = 26
        t = np.linspace(0, 1, n)
        h = np.array([math.cos(phi), math.sin(phi), 0.0])
        spine = top[None, :] + np.outer(Lf * t * (1 - 0.12 * t), h) + \
            np.outer(Lf * (up * t - droop * t * t), [0, 0, 1])
        tan = np.gradient(spine, axis=0)
        tan /= np.linalg.norm(tan, axis=1, keepdims=True)
        side = np.cross(tan, [0, 0, 1])
        side /= np.linalg.norm(side, axis=1, keepdims=True) + 1e-9
        w = 0.19 * np.sin(math.pi * np.clip(t, 0, 1) ** 0.7) * (1 - 0.25 * t)
        comb = 0.62 + 0.38 * np.abs(np.cos(math.pi * 9 * t))
        w = w * comb
        w[0] = 0.02
        verts, faces, vc = [], [], []
        g_dark, g_mid, g_tip = rgb(44, 122, 44), rgb(84, 164, 58), rgb(170, 196, 70)
        shade = 1.0 if i % 2 else 0.86
        for k in range(n):
            dz = np.array([0, 0, -0.45 * w[k]])
            verts += [tuple(spine[k] - side[k] * w[k] + dz), tuple(spine[k] + np.array([0, 0, 0.012])),
                      tuple(spine[k] + side[k] * w[k] + dz)]
            ck = T.lerp(g_mid, g_tip, max(0.0, (t[k] - 0.55) / 0.45))
            vc += [g_dark * shade, ck * 1.1 * shade, g_dark * shade]
        for k in range(n - 1):
            a0 = 3 * k
            faces += [(a0, a0 + 1, a0 + 4, a0 + 3), (a0 + 1, a0 + 2, a0 + 5, a0 + 4)]
        objs.append(L.mesh('frond%d' % i, verts, faces, 'd_vcol_leaf', smooth=True, cols=vc, recalc=False))
    objs.append(L.blob('crown', 'd_vcol', top + np.array([0, 0, -0.02]), 0.1, rng, amp=0.1, subdiv=2,
                       colfun=lambda P: np.tile(rgb(96, 110, 40), (len(P), 1))))
    for k in range(3):
        t = 2 * math.pi * k / 3 + 0.5
        c = top + np.array([0.08 * math.cos(t), 0.08 * math.sin(t), -0.12])
        objs.append(L.blob('coco%d' % k, 'd_coconut', c, 0.065, rng, amp=0.05, subdiv=2))
    return objs, H + 0.3


# ---------------------------------------------------------------------------
# cactus
# ---------------------------------------------------------------------------

def _ribs(n_ribs, amp):
    return lambda th, z: 1.0 + amp * (np.cos(n_ribs * th) * 0.5 + 0.5) - amp * 0.5


def cactus():
    rng = np.random.default_rng(4)
    prof = [(0.15, 0.0), (0.165, 0.06), (0.17, 0.3), (0.165, 1.0), (0.155, 1.16), (0.13, 1.28),
            (0.09, 1.36), (0.04, 1.40), (0.0, 1.41)]
    objs = [L.lathe('cac_trunk', 'd_cactus', prof, n=40, rfun=_ribs(10, 0.14))]

    def arm(name, sgn, z0, out, z1, r):
        ctrl = [(0.5 + sgn * 0.05, 0.5, z0), (0.5 + sgn * 0.2, 0.5, z0 + 0.02),
                (0.5 + sgn * out, 0.5, z0 + 0.14), (0.5 + sgn * out, 0.5, z1)]
        path = catmull(ctrl, 30)
        n = len(path)
        # a rounded tip: extend with a cap of shrinking rings
        tipn = 6
        tip = [path[-1] + np.array([0, 0, r * math.sin(math.pi / 2 * (k + 1) / tipn)]) for k in range(tipn)]
        P = np.vstack([path, tip])
        rr = [r] * n + [r * math.cos(math.pi / 2 * (k + 1) / tipn) + 0.002 for k in range(tipn)]
        return L.tube(name, 'd_cactus', P, rr, n=24, rfun=lambda th, s: 1 + 0.14 * (0.5 + 0.5 * math.cos(8 * th)) - 0.07)
    objs.append(arm('arm_l', -1, 0.52, 0.29, 0.98, 0.095))
    objs.append(arm('arm_r', 1, 0.72, 0.28, 1.12, 0.085))
    # a pink flower on top
    top = np.array([0.5, 0.5, 1.41])
    for k in range(5):
        t = 2 * math.pi * k / 5
        c = top + np.array([0.045 * math.cos(t), 0.045 * math.sin(t), 0.01])
        objs.append(L.blob('pet%d' % k, 'd_cactus_flower', c, 0.04, rng, amp=0.05, subdiv=2, scale=(1, 1, 0.55)))
    objs.append(L.blob('pmid', 'd_flower_mid', top + np.array([0, 0, 0.03]), 0.025, rng, amp=0.0, subdiv=2))
    return objs, 1.45


# ---------------------------------------------------------------------------
# obelisk
# ---------------------------------------------------------------------------

def obelisk_tex():
    """One face of the shaft: rose granite with a recessed painted column of
    signs (reuses the hieroglyph drawings of the brick panel)."""
    import zones_df_tex_desert as D
    Wm, Hm = 0.4, 2.3
    w, h = int(Wm * T.TEX), int(Hm * T.TEX)
    xs = (np.arange(w) + 0.5) / T.TEX
    ys = (np.arange(h) + 0.5) / T.TEX
    x, y = np.meshgrid(xs, ys)
    sh = x.shape
    n = T.fnoise(sh, 0.04, 901)
    col = T.lerp(rgb(206, 100, 76), rgb(222, 124, 92), 0.5 + 0.3 * n)
    sp = T.fnoise(sh, 0.004, 902)
    col = T.lerp(col, rgb(120, 60, 50), T.sstep(1.4, 2.2, sp) * 0.8)
    col = T.lerp(col, rgb(240, 200, 180), T.sstep(1.6, 2.4, -sp) * 0.7)
    hgt = np.zeros(sh)
    # the recessed band
    cx = Wm / 2
    band = T.sd_box(x, y, cx, Hm / 2 + 0.05, 0.11, Hm / 2 - 0.2, 0.01)
    inside = T.cover(band)
    col = T.lerp(col, col * 0.8, inside)
    hgt -= 0.008 * inside
    frame = T.cover(np.abs(band) - 0.008)
    col = T.lerp(col, D.GOLD, frame * 0.9)

    def put(d, c):
        nonlocal col
        col = T.lerp(col, c, T.cover(d) * inside)
    yy = Hm - 0.35
    k = 0
    while yy > 0.35:
        kind = k % 4
        if kind == 0:
            disk, ring, rays = D._glyph_sun(x, y, cx, yy, 1.0)
            put(ring, D.GOLD)
            put(disk, D.TERRA)
        elif kind == 1:
            b, wing, head = D._glyph_bird(x, y, cx + 0.01, yy - 0.01, 1.2)
            put(b, D.TURQ_D)
            put(wing, D.TURQ)
        elif kind == 2:
            up, outline, iris, lines = D._glyph_eye(x, y, cx - 0.01, yy, 1.1)
            put(up, D.WHITE)
            put(iris, D.INK)
            put(outline, D.INK)
            put(lines, D.INK)
        else:
            put(D._glyph_waves(x, y, cx, yy, 1.1), D.TURQ)
        yy -= 0.26
        k += 1
    return T.Tex(col, hgt)


def obelisk():
    objs = [C.box('ob_plinth', 0.10, 0.10, 0.0, 0.90, 0.90, 0.16, 'd_lime'),
            C.box('ob_step', 0.18, 0.18, 0.16, 0.82, 0.82, 0.30, 'd_lime')]
    if 'd_obelisk' not in C._MATS:
        L.tile_mat('d_obelisk', obelisk_tex(), 'uv', rough=0.45, spec=0.4)
    z0, z1, zt = 0.30, 2.62, 3.0
    h0, h1 = 0.20, 0.145
    c = 0.5
    ring0 = [(c - h0, c - h0, z0), (c + h0, c - h0, z0), (c + h0, c + h0, z0), (c - h0, c + h0, z0)]
    ring1 = [(c - h1, c - h1, z1), (c + h1, c - h1, z1), (c + h1, c + h1, z1), (c - h1, c + h1, z1)]
    verts = ring0 + ring1
    faces, uvs = [], []
    for i in range(4):
        j = (i + 1) % 4
        faces.append((i, j, 4 + j, 4 + i))
        uvs += [(0, 0), (1, 0), (1, 1), (0, 1)]
    faces.append((4, 5, 6, 7))
    uvs += [(0.5, 0.99)] * 4
    shaft = L.mesh('ob_shaft', verts, faces, 'd_obelisk', uvs=uvs, recalc=False)
    objs.append(shaft)
    pv = ring1 + [(c, c, zt)]
    pf = [(0, 1, 4), (1, 2, 4), (2, 3, 4), (3, 0, 4)]
    objs.append(L.mesh('ob_tip', pv, pf, 'd_gold'))
    # a gold collar under the tip
    objs.append(C.box('ob_collar', c - h1 - 0.012, c - h1 - 0.012, z1 - 0.05, c + h1 + 0.012, c + h1 + 0.012, z1,
                      'd_gold'))
    return objs, zt


# ---------------------------------------------------------------------------
# brazier (lit)
# ---------------------------------------------------------------------------

def flame(name, base, h, r, bend, rng, n=16, key='d_flame', core=False):
    k = 14
    prof = []
    cols = []
    for i in range(k + 1):
        t = i / k
        rr = r * (t ** 0.45) * (1 - t) ** 0.9 * 1.9
        prof.append((max(rr, 0.0), base[2] + h * t))
        if core:
            c = T.lerp(np.array([1.0, 0.95, 0.70]), np.array([1.0, 0.78, 0.25]), min(1.0, t * 1.3))
        else:
            c = T.lerp(T.lerp(np.array([1.0, 0.62, 0.12]), np.array([1.0, 0.36, 0.05]), min(1.0, t * 1.5)),
                       np.array([0.75, 0.10, 0.02]), max(0.0, (t - 0.55) / 0.45))
        cols.append(c)
    ob = L.lathe(name, key, prof, n=n, center=(base[0], base[1]), cols=cols, cap_top=False)
    me = ob.data
    for v in me.vertices:
        t = (v.co.z - base[2]) / h
        v.co.x += bend[0] * t * t
        v.co.y += bend[1] * t * t
    me.update()
    return ob


def brazier():
    rng = np.random.default_rng(8)
    objs = []
    c = np.array([0.5, 0.5])
    for k in range(3):
        phi = 2 * math.pi * k / 3 + math.pi / 2 + 0.35
        d = np.array([math.cos(phi), math.sin(phi)])
        rad = [0.36, 0.33, 0.27, 0.19, 0.13]
        zz = [0.03, 0.14, 0.34, 0.52, 0.64]
        pts = [(c[0] + r * d[0], c[1] + r * d[1], z) for r, z in zip(rad, zz)]
        path = catmull(pts, 16)
        objs.append(L.tube('leg%d' % k, 'd_bronze', path, [0.026] * 16, n=10))
        objs.append(L.blob('foot%d' % k, 'd_gold', (c[0] + 0.37 * d[0], c[1] + 0.37 * d[1], 0.035), 0.04, rng,
                           amp=0.0, subdiv=2))
    # a ring tying the legs
    th = np.linspace(0, 2 * math.pi, 33)
    ring = [(c[0] + 0.265 * math.cos(t), c[1] + 0.265 * math.sin(t), 0.34) for t in th]
    objs.append(L.tube('ring', 'd_gold', ring, [0.016] * len(ring), n=8, cap=False))
    prof = [(0.0, 0.60), (0.09, 0.60), (0.2, 0.645), (0.285, 0.735), (0.33, 0.83), (0.345, 0.87),
            (0.33, 0.89), (0.30, 0.87), (0.25, 0.82), (0.12, 0.775), (0.0, 0.77)]
    objs.append(L.lathe('bowl', 'd_bronze', prof, n=32, cap_top=False, cap_bot=False))
    rim = [(c[0] + 0.343 * math.cos(t), c[1] + 0.343 * math.sin(t), 0.875) for t in th]
    objs.append(L.tube('rim', 'd_gold', rim, [0.022] * len(rim), n=8, cap=False))
    # coals and ash
    objs.append(L.lathe('ash', 'd_ash', [(0.0, 0.80), (0.27, 0.80), (0.0, 0.81)], n=24))
    for k in range(14):
        r = 0.2 * math.sqrt(rng.uniform(0, 1))
        t = rng.uniform(0, 2 * math.pi)
        objs.append(L.blob('coal%d' % k, 'd_coal', (c[0] + r * math.cos(t), c[1] + r * math.sin(t), 0.815),
                           rng.uniform(0.035, 0.055), rng, amp=0.2, subdiv=1, scale=(1, 1, 0.7)))
    # flames: orange tongues round a yellow core
    objs.append(flame('fl0', (0.5, 0.5, 0.79), 0.56, 0.14, (-0.03, 0.02), rng))
    objs.append(flame('core', (0.5, 0.5, 0.79), 0.34, 0.085, (-0.02, 0.01), rng, key='d_flame_core', core=True))
    for k in range(5):
        t = 2 * math.pi * k / 5 + 0.4
        r = 0.14
        base = (c[0] + r * math.cos(t), c[1] + r * math.sin(t), 0.79)
        objs.append(flame('fl%d' % (k + 1), base, rng.uniform(0.28, 0.38), rng.uniform(0.07, 0.09),
                          (0.07 * math.cos(t), 0.07 * math.sin(t)), rng))
    # the fire's light: one in the flames, three spilling over the rim (the
    # bowl would shadow the whole pool from a single light above it)
    lamps = [C.add_point_light((0.5, 0.5, 1.12), color=(1.0, 0.55, 0.22), power=10.0, radius=0.15, name='fire')]
    for k in range(3):
        t = 2 * math.pi * k / 3 + 0.2
        lamps.append(C.add_point_light((0.5 + 0.40 * math.cos(t), 0.5 + 0.40 * math.sin(t), 0.95),
                                       color=(1.0, 0.52, 0.2), power=7.0, radius=0.1, name='spill%d' % k))
    return objs, 1.35, lamps


# ---------------------------------------------------------------------------
# more materials (registered lazily)
# ---------------------------------------------------------------------------

def _mats2():
    if getattr(_mats2, 'done', False):
        return
    _mats2.done = True
    L.noise_mat('d_statue', (230, 180, 126), (204, 150, 104), scale=9, rough=0.8, spec=0.25, bump=0.3)
    L.stripe_mat('d_nemes', (250, 196, 64), (40, 86, 170), axis='Z', freq=14, rough=0.35, spec=0.5)
    L.stripe_mat('d_nemes_x', (250, 196, 64), (40, 86, 170), axis='X', freq=14, rough=0.35, spec=0.5)
    L.flat('d_ink', (40, 28, 24), rough=0.5)
    L.flat('d_white', (246, 240, 226), rough=0.4)
    L.flat('d_lapis', (34, 92, 190), rough=0.25, spec=0.6, metal=0.3)
    L.flat('d_turq', (40, 178, 170), rough=0.35, spec=0.5)
    L.noise_mat('d_rock', (212, 136, 86), (178, 104, 66), scale=6, rough=0.85, bump=0.5)
    L.noise_mat('d_camel', (214, 166, 110), (192, 142, 92), scale=12, rough=0.85, bump=0.2)
    L.flat('d_camel_dark', (140, 96, 60), rough=0.8)
    L.stripe_mat('d_blanket', (206, 58, 48), (246, 206, 80), axis='X', freq=9, rough=0.7)
    L.stripe_mat('d_tent', (204, 64, 50), (240, 222, 184), axis='X', freq=10, rough=0.8, spec=0.2)
    L.flat('d_tent_in', (60, 32, 26), rough=0.9)
    L.flat('d_rope', (180, 150, 100), rough=0.9)
    L.flat('d_wood', (130, 86, 52), rough=0.8)
    L.flat('d_dart', (70, 50, 36), rough=0.6)
    L.flat('d_feather', (206, 58, 48), rough=0.7)
    L.veil_mat('d_streak', (255, 236, 190), strength=1.2, alpha=0.55)
    L.veil_mat('d_gateveil', (255, 196, 90), strength=2.0, alpha=0.7)
    L.flat('d_cactus_yflower', (255, 212, 60), rough=0.5)


def _box(name, key, x0, y0, z0, x1, y1, z1):
    return C.box(name, x0, y0, z0, x1, y1, z1, key)


# --- small barrel cactus -------------------------------------------------------

def cactus_small():
    rng = np.random.default_rng(21)
    objs = []
    for (cx, cy, r, h) in ((0.46, 0.52, 0.2, 0.42), (0.7, 0.36, 0.11, 0.22)):
        prof = [(r * 0.8, 0.0)] + [(r * math.sin(math.pi * (0.15 + 0.85 * t / 10)) ** 0.7, h * t / 10)
                                   for t in range(1, 11)] + [(0.0, h)]
        objs.append(L.lathe('barrel', 'd_cactus', prof, n=36, center=(cx, cy), rfun=_ribs(12, 0.16)))
        for k in range(3):
            t = 2 * math.pi * k / 3 + rng.uniform(0, 1)
            c = (cx + r * 0.25 * math.cos(t), cy + r * 0.25 * math.sin(t), h * 0.98)
            objs.append(L.blob('fl', 'd_cactus_yflower', c, r * 0.18, rng, amp=0.05, subdiv=2, scale=(1, 1, 0.6)))
    return objs, 0.46


# --- seated pharaoh statue -------------------------------------------------------

def statue():
    rng = np.random.default_rng(22)
    S = 'd_statue'
    objs = [_box('plinth', 'd_lime', 0.1, 0.1, 0.0, 0.9, 0.9, 0.14),
            _box('throne', S, 0.22, 0.42, 0.14, 0.78, 0.86, 0.74),
            _box('back', S, 0.22, 0.74, 0.74, 0.78, 0.86, 1.3),
            _box('thighs', S, 0.3, 0.26, 0.6, 0.7, 0.62, 0.78),
            _box('shin_l', S, 0.31, 0.24, 0.14, 0.48, 0.36, 0.64),
            _box('shin_r', S, 0.52, 0.24, 0.14, 0.69, 0.36, 0.64),
            _box('foot_l', S, 0.31, 0.16, 0.14, 0.48, 0.3, 0.22),
            _box('foot_r', S, 0.52, 0.16, 0.14, 0.69, 0.3, 0.22)]
    objs.append(L.blob('torso', S, (0.5, 0.6, 1.02), 1.0, rng, amp=0.0, subdiv=3, scale=(0.2, 0.14, 0.3)))
    objs.append(L.blob('kilt', 'd_white', (0.5, 0.5, 0.76), 1.0, rng, amp=0.0, subdiv=3, scale=(0.22, 0.2, 0.08)))
    for sx in (-1, 1):
        objs.append(L.tube('arm', S, L.catmull([(0.5 + sx * 0.19, 0.6, 1.2), (0.5 + sx * 0.22, 0.52, 0.98),
                                                (0.5 + sx * 0.12, 0.34, 0.82)], 8), [0.06] * 8, n=10))
        objs.append(L.blob('hand', S, (0.5 + sx * 0.12, 0.32, 0.8), 0.055, rng, amp=0.0, subdiv=2))
    objs.append(L.blob('head', S, (0.5, 0.58, 1.48), 0.17, rng, amp=0.0, subdiv=3, scale=(1, 0.95, 1.05)))
    # nemes: a striped hood and two lappets on the chest
    objs.append(L.blob('nemes', 'd_nemes', (0.5, 0.62, 1.53), 1.0, rng, amp=0.0, subdiv=3, scale=(0.235, 0.2, 0.19)))
    for sx in (-1, 1):
        objs.append(_box('lappet', 'd_nemes', 0.5 + sx * 0.19 - 0.05, 0.47, 1.12, 0.5 + sx * 0.19 + 0.05, 0.55, 1.42))
    objs.append(_box('beard', 'd_gold', 0.47, 0.42, 1.18, 0.53, 0.47, 1.33))
    for sx in (-1, 1):
        objs.append(L.blob('eye', 'd_white', (0.5 + sx * 0.065, 0.415, 1.5), 0.03, rng, amp=0.0, subdiv=2,
                           scale=(1.2, 0.5, 0.8)))
        objs.append(L.blob('pupil', 'd_ink', (0.5 + sx * 0.065, 0.405, 1.5), 0.016, rng, amp=0.0, subdiv=2,
                           scale=(1, 0.5, 1)))
    objs.append(L.blob('cobra', 'd_gold', (0.5, 0.43, 1.66), 0.035, rng, amp=0.0, subdiv=2, scale=(0.8, 0.6, 1.3)))
    return objs, 1.75


# --- urn ---------------------------------------------------------------------------

def urn():
    terr, dark, turq, gold = rgb(200, 104, 60), rgb(60, 36, 28), rgb(40, 178, 170), rgb(246, 196, 70)
    prof, cols = [], []
    pts = [(0.0, 0.0, terr), (0.11, 0.0, terr), (0.15, 0.05, terr), (0.2, 0.16, terr), (0.225, 0.27, dark),
           (0.228, 0.285, turq), (0.228, 0.35, turq), (0.225, 0.365, dark), (0.21, 0.44, terr), (0.17, 0.53, terr),
           (0.12, 0.6, terr), (0.1, 0.64, gold), (0.13, 0.67, gold), (0.13, 0.69, gold), (0.1, 0.7, terr),
           (0.06, 0.74, terr), (0.04, 0.78, gold), (0.0, 0.8, gold)]
    for r, z, c in pts:
        prof.append((r, z))
        cols.append(c)
    return [L.lathe('urn', 'd_vcol', prof, n=28, center=(0.5, 0.5), cols=cols)], 0.8


# --- sphinx (2 x 2) -----------------------------------------------------------------

def sphinx():
    rng = np.random.default_rng(23)
    S = 'd_statue'
    objs = [_box('base', 'd_lime', 0.08, 0.08, 0.0, 1.92, 1.92, 0.14)]
    objs.append(L.blob('body', S, (1.0, 1.25, 0.58), 1.0, rng, amp=0.02, subdiv=3, scale=(0.5, 0.62, 0.42)))
    objs.append(L.blob('haunch_l', S, (0.62, 1.55, 0.42), 1.0, rng, amp=0.0, subdiv=3, scale=(0.18, 0.32, 0.26)))
    objs.append(L.blob('haunch_r', S, (1.38, 1.55, 0.42), 1.0, rng, amp=0.0, subdiv=3, scale=(0.18, 0.32, 0.26)))
    objs.append(L.blob('chest', S, (1.0, 0.72, 0.75), 1.0, rng, amp=0.0, subdiv=3, scale=(0.42, 0.3, 0.5)))
    for sx in (-1, 1):
        objs.append(L.tube('leg', S, [(1.0 + sx * 0.28, 0.75, 0.3), (1.0 + sx * 0.3, 0.45, 0.2), (1.0 + sx * 0.3, 0.2, 0.2)],
                           [0.13, 0.12, 0.12], n=14))
        objs.append(L.blob('paw', S, (1.0 + sx * 0.3, 0.2, 0.2), 1.0, rng, amp=0.0, subdiv=2, scale=(0.14, 0.12, 0.08)))
    objs.append(L.blob('head', S, (1.0, 0.62, 1.34), 0.25, rng, amp=0.0, subdiv=3, scale=(1, 0.95, 1.05)))
    objs.append(L.blob('nemes', 'd_nemes', (1.0, 0.68, 1.4), 1.0, rng, amp=0.0, subdiv=3, scale=(0.33, 0.28, 0.28)))
    for sx in (-1, 1):
        objs.append(_box('lappet', 'd_nemes', 1.0 + sx * 0.27 - 0.07, 0.5, 0.82, 1.0 + sx * 0.27 + 0.07, 0.64, 1.28))
        objs.append(L.blob('eye', 'd_white', (1.0 + sx * 0.095, 0.39, 1.38), 0.045, rng, amp=0.0, subdiv=2,
                           scale=(1.2, 0.5, 0.8)))
        objs.append(L.blob('pupil', 'd_ink', (1.0 + sx * 0.095, 0.375, 1.38), 0.024, rng, amp=0.0, subdiv=2,
                           scale=(1, 0.5, 1)))
    objs.append(L.blob('nose', S, (1.0, 0.37, 1.3), 0.04, rng, amp=0.0, subdiv=2))
    objs.append(_box('beard', 'd_gold', 0.965, 0.42, 0.98, 1.035, 0.5, 1.15))
    objs.append(L.blob('cobra', 'd_gold', (1.0, 0.41, 1.58), 0.05, rng, amp=0.0, subdiv=2, scale=(0.8, 0.6, 1.3)))
    tail = L.catmull([(1.45, 1.75, 0.3), (1.55, 1.5, 0.18), (1.55, 1.1, 0.16), (1.5, 0.95, 0.22)], 12)
    objs.append(L.tube('tail', S, tail, np.linspace(0.05, 0.03, 12), n=8))
    return objs, 1.7


# --- lotus column -------------------------------------------------------------------

def column():
    sand, turq, terr, gold, green = rgb(230, 184, 132), rgb(40, 178, 170), rgb(200, 78, 44), rgb(246, 196, 70), rgb(70, 150, 90)
    band = [(2.12, turq), (2.2, terr), (2.28, gold), (2.36, turq)]
    prof, cols = [], []

    def add(r, z, c):
        prof.append((r, z))
        cols.append(c)
    add(0.0, 0.0, sand)
    add(0.34, 0.0, sand)
    add(0.34, 0.12, sand)
    add(0.27, 0.13, sand)
    add(0.29, 0.35, sand)
    add(0.24, 2.1, sand)
    z0 = 2.1
    for z, c in band:
        add(0.24, z0 + 0.001, c)
        add(0.24, z, c)
        z0 = z
    add(0.25, 2.45, green)
    add(0.33, 2.62, green)
    add(0.4, 2.8, turq)
    add(0.41, 2.84, gold)
    add(0.0, 2.84, gold)

    def rf(th, z):
        if z < 0.13:
            return 1.0
        if z > 2.44:
            return 1.0 + 0.12 * abs(math.cos(4 * th))
        return 1.0 + 0.07 * (0.5 + 0.5 * math.cos(8 * th))
    objs = [L.lathe('col', 'd_vcol', prof, n=48, cols=cols, rfun=rf)]
    objs.append(_box('abacus', 'd_statue', 0.24, 0.24, 2.84, 0.76, 0.76, 3.0))
    return objs, 3.0


# --- scarab statue --------------------------------------------------------------------

def scarab():
    rng = np.random.default_rng(24)
    objs = [_box('plinth', 'd_lime', 0.14, 0.14, 0.0, 0.86, 0.86, 0.2),
            _box('plinth2', 'd_statue', 0.18, 0.18, 0.2, 0.82, 0.82, 0.26)]
    for sx in (-1, 1):
        objs.append(L.blob('wing', 'd_lapis', (0.5 + sx * 0.12, 0.55, 0.42), 1.0, rng, amp=0.0, subdiv=3,
                           scale=(0.14, 0.26, 0.15)))
    objs.append(L.blob('thorax', 'd_lapis', (0.5, 0.33, 0.43), 1.0, rng, amp=0.0, subdiv=3, scale=(0.2, 0.1, 0.12)))
    objs.append(L.blob('head', 'd_gold', (0.5, 0.23, 0.4), 1.0, rng, amp=0.0, subdiv=2, scale=(0.14, 0.07, 0.08)))
    for sx in (-1, 1):
        for k, yy in enumerate((0.3, 0.45, 0.62)):
            objs.append(L.tube('leg', 'd_gold', [(0.5 + sx * 0.18, yy, 0.38), (0.5 + sx * 0.28, yy - 0.03, 0.34),
                                                 (0.5 + sx * 0.3, yy - 0.06, 0.26)], [0.018] * 3, n=6))
    th = np.linspace(0, 2 * math.pi, 29)
    objs.append(L.lathe('sun', 'd_gold', [(0.0, -0.02), (0.14, -0.02), (0.14, 0.02), (0.0, 0.02)], n=28,
                        center=(0.0, 0.0)))
    sun = objs[-1]
    from mathutils import Matrix
    L.transform(sun, Matrix.Translation((0.5, 0.16, 0.66)) @ Matrix.Rotation(math.pi / 2, 4, 'X'))
    return objs, 0.82


# --- desert rock ---------------------------------------------------------------------------

def rock():
    rng = np.random.default_rng(25)
    return [L.blob('r1', 'd_rock', (0.46, 0.52, 0.2), 1.0, rng, amp=0.18, freq=2.5, subdiv=3,
                   scale=(0.36, 0.3, 0.32), flat_below=0.0),
            L.blob('r2', 'd_rock', (0.72, 0.34, 0.1), 1.0, rng, amp=0.2, freq=3, subdiv=3,
                   scale=(0.17, 0.15, 0.15), flat_below=0.0)], 0.52


# --- striped tent (2 x 2) --------------------------------------------------------------------

def tent():
    objs = []
    x0, x1, H = 0.18, 1.82, 1.55
    nx, ns = 24, 16
    verts, faces = [], []
    for i in range(nx + 1):
        x = x0 + (x1 - x0) * i / nx
        sag = 0.1 * math.sin(math.pi * i / nx)
        for j in range(ns + 1):
            sv = -1 + 2 * j / ns
            y = 1.0 + 0.8 * sv
            z = (H - sag) * (1 - abs(sv)) ** 0.85 + 0.02
            verts.append((x, y, z))
    for i in range(nx):
        for j in range(ns):
            a0 = i * (ns + 1) + j
            faces.append((a0, a0 + ns + 1, a0 + ns + 2, a0 + 1))
    roof = L.mesh('roof', verts, faces, 'd_tent', smooth=True)
    L.solidify(roof, 0.02)
    objs.append(roof)
    # the gable ends: the +X one open (a dark doorway, the flaps tied back)
    for xe, open_ in ((x0, False), (x1, True)):
        gv = [(xe, 0.2, 0.02), (xe, 1.8, 0.02), (xe, 1.0, H + 0.02)]
        objs.append(L.mesh('gable', gv, [(0, 1, 2)], 'd_tent'))
        if open_:
            dv = [(xe + 0.005, 0.72, 0.02), (xe + 0.005, 1.28, 0.02), (xe + 0.005, 1.0, 1.05)]
            objs.append(L.mesh('door', dv, [(0, 1, 2)], 'd_tent_in'))
            for sy in (-1, 1):
                fv = [(xe + 0.01, 1.0 + sy * 0.28, 0.02), (xe + 0.08, 1.0 + sy * 0.42, 0.3), (xe + 0.01, 1.0 + sy * 0.05, 0.95)]
                objs.append(L.mesh('flap', fv, [(0, 1, 2)], 'd_tent'))
    for xp in (x0 + 0.02, x1 - 0.02):
        objs.append(L.tube('pole', 'd_wood', [(xp, 1.0, 0.0), (xp, 1.0, H + 0.15)], [0.03, 0.03], n=8))
        objs.append(L.blob('knob', 'd_gold', (xp, 1.0, H + 0.17), 0.045, np.random.default_rng(1), amp=0, subdiv=2))
        for sy in (-1, 1):
            objs.append(L.tube('guy', 'd_rope', [(xp, 1.0, H + 0.08), (xp + (0.12 if xp > 1 else -0.12), 1.0 + sy * 0.9, 0.0)],
                               [0.008, 0.008], n=5))
    return objs, 1.75


# --- sitting camel (2 x 1, head to +X) ---------------------------------------------------------

def camel():
    rng = np.random.default_rng(26)
    C_ = 'd_camel'
    objs = [L.blob('body', C_, (0.85, 0.5, 0.42), 1.0, rng, amp=0.0, subdiv=3, scale=(0.55, 0.3, 0.3), flat_below=0.02),
            L.blob('hump', C_, (0.82, 0.5, 0.72), 1.0, rng, amp=0.0, subdiv=3, scale=(0.27, 0.22, 0.22))]
    for sx, sy in ((0.5, 0.29), (0.5, 0.71), (1.16, 0.3), (1.16, 0.7)):
        objs.append(L.blob('leg', C_, (sx, sy, 0.12), 1.0, rng, amp=0.0, subdiv=2, scale=(0.26, 0.11, 0.11)))
        objs.append(L.blob('hoof', 'd_camel_dark', (sx + 0.25, sy, 0.08), 1.0, rng, amp=0.0, subdiv=2,
                           scale=(0.07, 0.08, 0.07)))
    neck = L.catmull([(1.25, 0.5, 0.5), (1.45, 0.5, 0.62), (1.5, 0.5, 0.95), (1.58, 0.5, 1.12)], 12)
    objs.append(L.tube('neck', C_, neck, np.linspace(0.13, 0.09, 12), n=14))
    objs.append(L.blob('head', C_, (1.66, 0.5, 1.16), 1.0, rng, amp=0.0, subdiv=3, scale=(0.17, 0.12, 0.12)))
    objs.append(L.blob('snout', C_, (1.8, 0.5, 1.12), 1.0, rng, amp=0.0, subdiv=3, scale=(0.09, 0.09, 0.08)))
    for sy in (-1, 1):
        objs.append(L.blob('ear', 'd_camel_dark', (1.6, 0.5 + sy * 0.09, 1.28), 1.0, rng, amp=0.0, subdiv=2,
                           scale=(0.03, 0.03, 0.05)))
        objs.append(L.blob('eye', 'd_ink', (1.73, 0.5 + sy * 0.1, 1.2), 0.026, rng, amp=0.0, subdiv=2))
        objs.append(L.blob('glint', 'd_white', (1.745, 0.5 + sy * 0.113, 1.212), 0.008, rng, amp=0.0, subdiv=1))
        objs.append(L.blob('lash', 'd_ink', (1.735, 0.5 + sy * 0.104, 1.235), 1.0, rng, amp=0.0, subdiv=1,
                           scale=(0.025, 0.01, 0.006)))
    objs.append(L.blob('nostril', 'd_ink', (1.885, 0.5, 1.14), 1.0, rng, amp=0.0, subdiv=1, scale=(0.008, 0.04, 0.01)))
    objs.append(L.blob('smile', 'd_ink', (1.86, 0.5, 1.08), 1.0, rng, amp=0.0, subdiv=1, scale=(0.02, 0.05, 0.006)))
    # a saddle blanket over the hump, with tassels
    bl = L.blob('blanket', 'd_blanket', (0.82, 0.5, 0.66), 1.0, rng, amp=0.0, subdiv=3, scale=(0.3, 0.33, 0.2))
    objs.append(bl)
    for k in range(5):
        x = 0.6 + k * 0.11
        for sy in (-1, 1):
            objs.append(L.blob('tassel', 'd_gold', (x, 0.5 + sy * 0.33, 0.48), 0.022, rng, amp=0.0, subdiv=1))
    tail = L.catmull([(0.31, 0.5, 0.45), (0.25, 0.5, 0.35), (0.26, 0.5, 0.22)], 6)
    objs.append(L.tube('tail', 'd_camel_dark', tail, [0.02] * 6, n=6))
    return objs, 1.33


# ---------------------------------------------------------------------------
# moving things
# ---------------------------------------------------------------------------

def boulder_tex():
    """Equirect texture about the rolling axis: u = angle round the axis, v =
    angle from one pole. Four grooves every 90 degrees (so 8 frames of 11.25
    degrees loop) and a carved sun on each pole."""
    w, h = 256, 128
    u = (np.arange(w) + 0.5) / w
    v = (np.arange(h) + 0.5) / h
    U, Vv = np.meshgrid(u, v)
    sh = U.shape
    n = T.fnoise(sh, 0.03, 1700)
    col = T.lerp(rgb(202, 148, 100), rgb(226, 174, 122), T.sstep(-1.2, 1.2, n))
    du = np.abs(np.mod(U * 4 + 0.5, 1.0) - 0.5) / 4           # distance to the nearest groove (in turns)
    groove = T.sstep(0.012, 0.004, du) * T.sstep(0.12, 0.2, np.minimum(Vv, 1 - Vv))
    ring = T.sstep(0.012, 0.004, np.abs(Vv - 0.5) / 2)
    pole = np.minimum(Vv, 1 - Vv)
    sun = T.sstep(0.012, 0.004, np.abs(pole - 0.1) / 2) + T.sstep(0.03, 0.02, pole)
    for k in range(8):
        sun = np.maximum(sun, T.sstep(0.012, 0.004, np.abs(np.mod(U * 8 + 0.5, 1.0) - 0.5) / 8) *
                         T.sstep(0.11, 0.12, pole) * T.sstep(0.17, 0.16, pole))
    c = np.clip(groove + ring + sun, 0, 1)
    col = col * (1 - 0.45 * c)[..., None]
    return T.Tex(col, -0.01 * c + 0.002 * n)


def uv_sphere(name, key, c, r, axis, nu=48, nv=24):
    """A UV sphere whose poles lie on `axis` ('X' or 'Y'), u round the axis."""
    verts, faces, uvs = [], [], []
    for j in range(nv + 1):
        ph = math.pi * j / nv
        for i in range(nu + 1):
            th = 2 * math.pi * i / nu
            a_, b_, ax_ = r * math.sin(ph) * math.cos(th), r * math.sin(ph) * math.sin(th), r * math.cos(ph)
            if axis == 'Y':
                verts.append((c[0] + a_, c[1] + ax_, c[2] + b_))
            else:
                verts.append((c[0] + ax_, c[1] + a_, c[2] + b_))
    for j in range(nv):
        for i in range(nu):
            a0 = j * (nu + 1) + i
            faces.append((a0, a0 + 1, a0 + nu + 2, a0 + nu + 1))
            uvs += [(i / nu, j / nv), ((i + 1) / nu, j / nv), ((i + 1) / nu, (j + 1) / nv), (i / nu, (j + 1) / nv)]
    return L.mesh(name, verts, faces, key, uvs=uvs, smooth=True)


def boulder(axis, frame):
    from mathutils import Matrix
    if 'd_boulder' not in C._MATS:
        L.tile_mat('d_boulder', boulder_tex(), 'uv', rough=0.8, spec=0.25, bump=1.0)
    r = 0.45
    ob = uv_sphere('ball', 'd_boulder', (0.0, 0.0, 0.0), r, 'Y' if axis == 'x' else 'X')
    ang = math.radians(90.0 / 8 * frame)
    R = Matrix.Rotation(ang, 4, 'Y') if axis == 'x' else Matrix.Rotation(-ang, 4, 'X')
    L.transform(ob, Matrix.Translation((0.5, 0.5, r)) @ R)
    return [ob]


def dart(direction):
    """A small dart at 0.5 m flying towards `direction` ('+x' or '-y'), a
    streak behind it."""
    from mathutils import Matrix
    objs = [L.tube('shaft', 'd_dart', [(0.22, 0.5, 0.5), (0.64, 0.5, 0.5)], [0.018, 0.018], n=8),
            L.lathe('tip', 'd_gold', [(0.0, 0.0), (0.032, 0.0), (0.0, 0.1)], n=10, center=(0.0, 0.0))]
    L.transform(objs[1], Matrix.Translation((0.64, 0.5, 0.5)) @ Matrix.Rotation(-math.pi / 2, 4, 'Y') @
                Matrix.Rotation(math.pi, 4, 'X'))
    for k in range(3):
        t = 2 * math.pi * k / 3
        fv = [(0.22, 0.5, 0.5), (0.33, 0.5, 0.5), (0.24, 0.5 + 0.07 * math.cos(t), 0.5 + 0.07 * math.sin(t))]
        objs.append(L.mesh('fl%d' % k, fv, [(0, 1, 2)], 'd_feather'))
    n = 10
    vs, fs, cols = [], [], []
    for i in range(n + 1):
        x = 0.22 - 0.5 * i / n
        w = 0.04 * (1 - i / n)
        a_ = (1 - i / n) ** 1.5
        vs += [(x, 0.5, 0.5 - w), (x, 0.5, 0.5 + w)]
        cols += [(a_, 0, 0), (a_, 0, 0)]
    for i in range(n):
        fs.append((2 * i, 2 * i + 2, 2 * i + 3, 2 * i + 1))
    objs.append(L.mesh('streak', vs, fs, 'd_streak', cols=cols, recalc=False))
    if direction == '-y':
        for ob in objs:
            L.rotate_z(ob, -90)
    return objs


GATE_LIFT = [0.0, 0.3, 0.65, 1.0, 1.35, 1.7, 2.0]


def gate(frame):
    """The exit: a stone door in a temple frame with a winged sun disk,
    sliding up into the lintel; 06 open with a golden glow."""
    import zones_df_tex_desert as D
    rng = np.random.default_rng(27)
    if 'd_gate_door' not in C._MATS:
        L.tile_mat('d_gate_door', D.crate_face(False), 'side', rough=0.8, spec=0.25)
        L.tile_mat('d_gate_pillar', D.sandstone_side(), 'side', rough=0.8, spec=0.25)
    objs = [_box('pl_l', 'd_gate_pillar', 0.03, 0.34, 0.0, 0.2, 0.66, 2.05),
            _box('pl_r', 'd_gate_pillar', 0.8, 0.34, 0.0, 0.97, 0.66, 2.05),
            _box('cap_l', 'd_gold', 0.02, 0.33, 1.95, 0.21, 0.67, 2.05),
            _box('cap_r', 'd_gold', 0.79, 0.33, 1.95, 0.98, 0.67, 2.05),
            _box('lintel', 'd_statue', 0.02, 0.3, 2.05, 0.98, 0.7, 2.42),
            _box('cornice', 'd_gold', 0.02, 0.28, 2.42, 0.98, 0.72, 2.48)]
    from mathutils import Matrix
    disk = L.lathe('disk', 'd_gold', [(0.0, -0.015), (0.12, -0.015), (0.12, 0.015), (0.0, 0.015)], n=28, center=(0, 0))
    L.transform(disk, Matrix.Translation((0.5, 0.29, 2.235)) @ Matrix.Rotation(math.pi / 2, 4, 'X'))
    objs.append(disk)
    objs.append(L.lathe('disk_in', 'd_rock', [(0.0, -0.02), (0.07, -0.02), (0.07, 0.02), (0.0, 0.02)], n=24, center=(0, 0)))
    L.transform(objs[-1], Matrix.Translation((0.5, 0.275, 2.235)) @ Matrix.Rotation(math.pi / 2, 4, 'X'))
    # the wings of the sun disk: three feathers a side, shorter downwards
    for sx in (-1, 1):
        for k in range(3):
            z1 = 2.29 - k * 0.05
            xa, xb = (0.62, 0.93 - k * 0.06) if sx > 0 else (0.07 + k * 0.06, 0.38)
            objs.append(_box('wing', 'd_turq' if k % 2 else 'd_gold', xa, 0.285, z1 - 0.038, xb, 0.3, z1))
    lift = GATE_LIFT[frame]
    lamps = []
    if lift < 2.0:
        door = _box('door', 'd_gate_door', 0.2, 0.44, 0.0, 0.8, 0.56, 2.05 - lift)
        L.transform(door, Matrix.Translation((0, 0, lift)))
        objs.append(door)
    # the darkness behind the door, and the golden light once it is open
    objs.append(_box('back', 'd_ink', 0.2, 0.62, 0.0, 0.8, 0.64, 2.05))
    if frame == 6:
        vs = [(0.2, 0.5, 0.0), (0.8, 0.5, 0.0), (0.8, 0.5, 2.0), (0.2, 0.5, 2.0)]
        objs.append(L.mesh('veil', vs, [(0, 1, 2, 3)], 'd_gateveil', cols=[(1, 0, 0), (1, 0, 0), (0.2, 0, 0), (0.2, 0, 0)],
                           recalc=False))
        lamps.append(C.add_point_light((0.5, 0.45, 0.9), color=(1.0, 0.78, 0.35), power=30.0, radius=0.3, name='gate'))
    elif frame > 0:
        vs = [(0.2, 0.5, 0.0), (0.8, 0.5, 0.0), (0.8, 0.5, lift), (0.2, 0.5, lift)]
        objs.append(L.mesh('veil', vs, [(0, 1, 2, 3)], 'd_gateveil', cols=[(0.7, 0, 0), (0.7, 0, 0), (0.1, 0, 0), (0.1, 0, 0)],
                           recalc=False))
    return objs, lamps


def reed_bridge(direction):
    """A reed-mat deck at z = 0 spanning the cell, bundles along the
    direction you walk (x or y), lashed three times."""
    if 'd_reed' not in C._MATS:
        import zones_df_tex_desert as D
        L.tile_mat('d_reed', D.reed_tex(), 'uv', rough=0.8, spec=0.2, bump=1.0)
    objs = []
    nb = 6
    r = 0.072
    for k in range(nb):
        y = 0.1 + r + k * (0.8 - 2 * r) / (nb - 1)
        verts, faces, uvs = [], [], []
        nth = 14
        xs = np.linspace(0.0, 1.0, 9)
        for x in xs:
            for j in range(nth + 1):
                t = 2 * math.pi * j / nth
                verts.append((x, y + r * math.cos(t), -r + r * 0.85 * math.sin(t)))
        m = nth + 1
        for i in range(len(xs) - 1):
            for j in range(nth):
                a0 = i * m + j
                faces.append((a0, a0 + m, a0 + m + 1, a0 + 1))
                uvs += [(xs[i], j / nth), (xs[i + 1], j / nth), (xs[i + 1], (j + 1) / nth), (xs[i], (j + 1) / nth)]
        objs.append(L.mesh('bundle%d' % k, verts, faces, 'd_reed', uvs=uvs, smooth=True))
        for xl in (0.16, 0.5, 0.84):
            th = np.linspace(0, 2 * math.pi, 13)
            ring = [(xl, y + (r + 0.006) * math.cos(t), -r + (r * 0.85 + 0.006) * math.sin(t)) for t in th]
            objs.append(L.tube('lash', 'd_rope', ring, [0.009] * len(ring), n=6, cap=False))
    if direction == 'y':
        for ob in objs:
            L.rotate_z(ob, 90)
    return objs


# ---------------------------------------------------------------------------

PROPS = [
    ('palm', palm, {}),
    ('cactus', cactus, {}),
    ('cactus_small', cactus_small, {}),
    ('obelisk', obelisk, {}),
    ('statue', statue, {}),
    ('urn', urn, {}),
    ('sphinx', sphinx, {'footprint': [2, 2]}),
    ('column', column, {}),
    ('scarab', scarab, {}),
    ('rock', rock, {}),
    ('tent', tent, {'footprint': [2, 2]}),
    ('camel', camel, {'footprint': [2, 1], 'faces': '+x'}),
]
LIT = [
    ('brazier', brazier, ((0.5, 0.5), 1.7)),
]


def run(a, sample):
    _mats()
    _mats2()
    for nm, fn, ex in PROPS:
        name = '%s_%s' % (Z, nm)
        if not L.wanted(a, name, sample):
            continue
        objs, hm = fn()
        L.render_prop(a, name, objs, hm, extra=ex)
    for nm, fn, glow in LIT:
        name = '%s_%s' % (Z, nm)
        if not L.wanted(a, name, sample):
            continue
        objs, hm, lights = fn()
        L.render_prop(a, name, objs, hm, glow=glow, lights=lights)
    for axis in ('x', 'y'):
        for f in range(8):
            name = '%s_boulder_%s_%02d' % (Z, axis, f)
            if L.wanted(a, name, sample):
                L.sprite(a, name, boulder(axis, f), extra=dict(
                    anim='roll', dir=axis, frame=f, frames=8, ms=50, height_m=0.9,
                    rolls_towards='+' + axis, note='play the frames backwards to roll towards -' + axis,
                    deg_per_frame=11.25, loop_m=round(2 * math.pi * 0.45 / 4, 4)))
    for d, nm in (('+x', 'x'), ('-y', 'y')):
        name = '%s_dart_%s' % (Z, nm)
        if L.wanted(a, name, sample):
            L.sprite(a, name, dart(d), extra=dict(flies_towards=d, height_m=0.5))
    for f in range(7):
        name = '%s_gate_%02d' % (Z, f)
        if L.wanted(a, name, sample):
            objs, lamps = gate(f)
            L.sprite(a, name, objs, glow=((0.5, 0.4), 1.6) if f == 6 else None, lights=lamps,
                     extra=dict(anim='open', frame=f, frames=7, height_m=2.48, exit=True,
                                states={'0': 'closed', '6': 'open'}))
    for d in ('x', 'y'):
        name = '%s_bridge_%s' % (Z, d)
        if L.wanted(a, name, sample):
            L.sprite(a, name, reed_bridge(d), kind='bridge', shadow_z=-0.18,
                     extra=dict(walk=d, deck_z=0.0, over='desert_surf_water'))
