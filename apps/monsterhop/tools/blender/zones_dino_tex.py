"""Monster Hop - Lost Valley (dinosaurs): the block, fill and surface textures
(numpy only; the helpers come from zones_df_tex, which is shared read-only).

Albedos in sRGB 0-255. The valley light (zones_dino_lib.LIGHT) is a warm
late-afternoon sun with a teal-green sky fill from the jungle canopy: the top
of a block gets about (1.02, 0.98, 0.79) x albedo in linear light, the front
face (0.55, 0.57, 0.48), the right face (0.29, 0.35, 0.31). Warm tops, cool
teal shadows: that and the red-brown soil keep it apart from the desert
(pale sand, warm shadows) and the forest (deep green night).

    python3 zones_dino_tex.py OUTDIR      # previews of every texture, lit
"""
import math
import sys

import numpy as np

import zones_df_tex as T
from zones_df_tex import rgb, lerp, sstep, Tex

LIGHT_TOP = np.array([1.02, 0.98, 0.79])
LIGHT_FRONT = np.array([0.55, 0.57, 0.48])
LIGHT_RIGHT = np.array([0.29, 0.35, 0.31])

DIRT = [rgb(176, 94, 58), rgb(146, 72, 46), rgb(112, 54, 36), rgb(200, 124, 78)]
GRASS = [rgb(56, 130, 50), rgb(86, 156, 56), rgb(34, 94, 46), rgb(162, 200, 80)]
BASALT = [rgb(116, 98, 96), rgb(90, 76, 78), rgb(62, 52, 56), rgb(150, 132, 124)]
STONE = [rgb(216, 150, 96), rgb(186, 124, 80), rgb(236, 180, 126)]
BONE = rgb(240, 228, 200)


# ---------------------------------------------------------------------------
# red-brown soil
# ---------------------------------------------------------------------------

def _dirt_colour(x, y, var, m):
    sh = x.shape
    n1 = T.lerp(T.fbm(sh, 0.10, 2101), T.fbm(sh, 0.10, 2110 + var), m)
    n2 = T.lerp(T.fnoise(sh, 0.016, 2120), T.fnoise(sh, 0.016, 2125 + var), m)
    grit = T.fnoise(sh, 0.004, 2130 + var)
    col = lerp(DIRT[1], DIRT[0], sstep(-1.2, 1.2, n1))
    col = lerp(col, DIRT[2], sstep(0.8, 2.0, -n2) * 0.5)          # darker clods
    col = lerp(col, DIRT[3], sstep(1.2, 2.2, n2) * 0.45)           # dry light crust
    col = col * (1 + 0.08 * grit)[..., None]
    hgt = 0.004 * n2 + 0.002 * grit
    return col, hgt


def _track(x, y, col, hgt, cx, cy, s=1.0, ang=0.0):
    """A three-toed dinosaur footprint pressed into the soil (pointing +y)."""
    c, sn = math.cos(ang), math.sin(ang)
    m = T.cover(T.sd_ellipse(x, y, cx, cy, 0.055 * s, 0.05 * s, ang))
    for k, a in enumerate((-0.55, 0.0, 0.55)):
        dx, dy = math.sin(a) * 0.12 * s, math.cos(a) * 0.12 * s
        tx, ty = cx + c * dx - sn * dy, cy + sn * dx + c * dy
        m = np.maximum(m, T.cover(T.sd_seg(x, y, cx, cy, tx, ty, 0.024 * s)))
    col *= (1 - 0.45 * m)[..., None]
    # the lit back wall of the print (the sun is front-left: the far edge catches it)
    rim = T.cover(T.sd_ellipse(x, y, cx + 0.008, cy + 0.012, 0.07 * s, 0.07 * s, ang)) * (1 - m)
    col *= (1 + 0.10 * rim * sstep(cy - 0.02, cy + 0.08, y))[..., None]
    hgt -= 0.008 * m
    return m


def dirt_top(var):
    x, y = T.top_grid()
    d = T.edge_dist(x, y)
    m = sstep(0.07, 0.3, d)
    col, hgt = _dirt_colour(x, y, var, m)
    rng = np.random.default_rng(2140 + var)
    if var == 0:
        T.pebbles(x, y, col, hgt, rng, 3, 0.016, 0.03, [rgb(196, 150, 110), rgb(160, 120, 96)], region=0.14)
    if var == 1:
        _track(x, y, col, hgt, 0.47, 0.38, 1.35, 0.35)
    if var == 2:
        # a bleached bone fragment half in the soil and a pebble
        dd = T.sd_seg(x, y, 0.34, 0.58, 0.60, 0.66, 0.022)
        knob = np.minimum(T.sd_circle(x, y, 0.33, 0.57, 0.032), T.sd_circle(x, y, 0.61, 0.67, 0.032))
        dd = np.minimum(dd, knob)
        sh_ = np.clip(1 - np.maximum(dd - 0.01, 0) / 0.02, 0, 1)
        col *= (1 - 0.35 * sh_ * (1 - T.cover(dd)))[..., None]
        col[:] = lerp(col, BONE * (1 + 0.1 * T.fnoise(x.shape, 0.01, 2150))[..., None], T.cover(dd))
        hgt += 0.01 * T.cover(dd)
        T.pebbles(x, y, col, hgt, rng, 2, 0.015, 0.025, [rgb(190, 146, 108)], region=0.2,
                  avoid=lambda cx, cy: abs(cy - 0.62) < 0.1)
    col = T.darken_top_edges(col, x, y, 0.09)
    return Tex(col, hgt)


def _earth(U, Z, seed=2201):
    """The block's sides: red-brown soil in strata, a dark ash band and an
    ochre band (volcanic valley), embedded stones."""
    sh = U.shape
    Hm = T.FLOOR_M
    v = (Z + Hm) / Hm
    n = T.fbm(sh, 0.05, seed)
    wob = 0.05 * T.fbm(sh, 0.10, seed + 1)
    col = lerp(DIRT[1], DIRT[0], sstep(-1.2, 1.2, n))
    # strata: whole numbers of bands per floor so fills stack
    ash = np.exp(-((np.mod(v + wob, 1.0) - 0.30) / 0.045) ** 2)
    ochre = np.exp(-((np.mod(v + wob, 1.0) - 0.72) / 0.07) ** 2)
    col = lerp(col, rgb(84, 58, 54), ash * 0.75)
    col = lerp(col, rgb(206, 140, 78), ochre * 0.55)
    grit = T.fnoise(sh, 0.005, seed + 2)
    col = col * (1 + 0.07 * grit)[..., None]
    hgt = 0.003 * n - 0.004 * ash
    rng = np.random.default_rng(seed + 3)
    for k in range(8):
        cx, cz = rng.uniform(0, 1), rng.uniform(0, Hm)
        r = rng.uniform(0.014, 0.026)
        tone = rng.uniform(0.85, 1.05)
        for ox in (-1, 0, 1):
            for oz in (-Hm, 0, Hm):
                d = T.sd_ellipse(U, Z + Hm, cx + ox, cz + oz, r * 1.4, r, rng.uniform(-0.3, 0.3))
                mm = T.cover(d)
                shade = 1 + 0.25 * np.clip((Z + Hm - cz - oz) / r, -1, 1)
                col = lerp(col, rgb(170, 132, 110) * tone * shade[..., None], mm)
                hgt = hgt + 0.008 * mm
    return col, hgt


def earth_fill():
    U, Z = T.side_grid()
    col, hgt = _earth(U, Z)
    return Tex(col, hgt)


def lip(U, Z, depth=0.08, amp=0.025, seed=5):
    n = T.noise1(U.shape[1], 0.05, seed)[None, :]
    return depth + amp * n


def dirt_side():
    U, Z = T.side_grid()
    col, hgt = _earth(U, Z)
    edge = lip(U, Z, 0.045, 0.012, 21)
    m = sstep(edge + 0.006, edge - 0.006, -Z)
    col = lerp(col, DIRT[0] * 0.95, m)
    below = np.clip(1 - (-Z - edge) / 0.04, 0, 1) * (-Z > edge)
    col = col * (1 - 0.22 * below)[..., None]
    return Tex(col, hgt)


# ---------------------------------------------------------------------------
# jungle grass
# ---------------------------------------------------------------------------

def _grass_colour(x, y, var, m):
    sh = x.shape
    n1 = T.lerp(T.fbm(sh, 0.11, 2301), T.fbm(sh, 0.11, 2310 + var), m)
    n2 = T.lerp(T.fnoise(sh, 0.017, 2320), T.fnoise(sh, 0.017, 2325 + var), m)
    blades = T.fnoise(sh, 0.0045, 2330 + var, aniso=(0.55, 1.0))
    col = lerp(GRASS[0], GRASS[1], sstep(-1.1, 1.3, n1))
    col = lerp(col, GRASS[2], sstep(0.5, 1.8, -n2) * 0.55)
    col = lerp(col, GRASS[3], sstep(1.1, 2.2, n2) * 0.40)          # sunlit tips
    col = col * (1 + 0.12 * blades)[..., None]
    hgt = 0.005 * n2 + 0.0025 * blades
    return col, hgt


def _leaf(x, y, col, hgt, cx, cy, ang, ln, wd, c):
    """A small fallen or sprouting leaf seen from above: a pointed ellipse
    with a lighter midrib."""
    ca, sa = math.cos(ang), math.sin(ang)
    dx, dy = x - cx, y - cy
    u = dx * ca + dy * sa
    v = -dx * sa + dy * ca
    w = wd * np.sqrt(np.clip(1 - (u / ln) ** 2, 0, 1)) * (1 - 0.35 * np.clip(u / ln, 0, 1))
    d = np.abs(v) - w
    d = np.where(np.abs(u) > ln, 1.0, d)
    mm = T.cover(d)
    sh_ = T.cover(np.where(np.abs(u - 0.01) > ln, 1.0, np.abs(v - 0.012) - w)) * (1 - mm)
    col *= (1 - 0.3 * sh_)[..., None]
    col[:] = lerp(col, c * (1 + 0.15 * np.clip(u / ln, -1, 1))[..., None], mm)
    col[:] = lerp(col, c * 1.35, mm * T.cover(np.abs(v) - 0.0035) * (np.abs(u) < ln * 0.85))
    hgt += 0.003 * mm


def grass_top(var):
    x, y = T.top_grid()
    d = T.edge_dist(x, y)
    m = sstep(0.07, 0.3, d)
    col, hgt = _grass_colour(x, y, var, m)
    rng = np.random.default_rng(2340 + var)
    if var == 1:
        # a few bright jungle flowers (red-orange and yellow)
        for (cx, cy), c in zip([(0.30, 0.64), (0.64, 0.34), (0.70, 0.70)],
                               [rgb(240, 70, 50), rgb(255, 200, 60), rgb(240, 110, 40)]):
            for pk in range(5):
                t = 2 * math.pi * pk / 5 + rng.uniform(0, 1)
                dp = T.sd_circle(x, y, cx + 0.015 * math.cos(t), cy + 0.015 * math.sin(t), 0.013)
                col[:] = lerp(col, c, T.cover(dp))
            col[:] = lerp(col, rgb(255, 236, 140), T.cover(T.sd_circle(x, y, cx, cy, 0.008)))
            hgt += 0.004 * T.cover(T.sd_circle(x, y, cx, cy, 0.03))
    if var == 2:
        # broad sprouting leaves and a stone
        for k in range(4):
            a = rng.uniform(0, 2 * math.pi)
            _leaf(x, y, col, hgt, 0.52 + 0.06 * math.cos(a), 0.48 + 0.06 * math.sin(a), a, 0.075, 0.03,
                  rgb(48, 140, 110))
        T.pebbles(x, y, col, hgt, rng, 1, 0.026, 0.03, [rgb(206, 150, 104)], region=0.24, shade=0.6,
                  avoid=lambda cx, cy: math.hypot(cx - 0.52, cy - 0.48) < 0.16)
    col = T.darken_top_edges(col, x, y)
    return Tex(col, hgt)


def grass_side():
    U, Z = T.side_grid()
    col, hgt = _earth(U, Z)
    edge = lip(U, Z)
    below = np.clip(1 - (-Z - edge) / 0.05, 0, 1) * (-Z > edge)
    col = col * (1 - 0.35 * below)[..., None]
    gcol, _ = _grass_colour(U, np.full_like(U, 0.03), 0, np.zeros_like(U))
    jag = 0.018 * np.abs(np.sin(2 * math.pi * 17 * U + 3 * T.noise1(U.shape[1], 0.02, 9)[None, :]))
    m2 = sstep(edge + jag + 0.006, edge + jag - 0.006, -Z)
    col = lerp(col, gcol * lerp(0.72, 1.0, sstep(edge + jag, 0.0, -Z))[..., None], m2)
    hgt = hgt * (1 - m2) + 0.004 * m2
    return Tex(col, hgt)


# ---------------------------------------------------------------------------
# volcanic rock (basalt): plates on top, columns on the sides
# ---------------------------------------------------------------------------

def voronoi(x, y, pts):
    d = np.stack([np.hypot(x - px, y - py) for px, py in pts], 0)
    o = np.argsort(d, axis=0)
    f1 = np.take_along_axis(d, o[:1], 0)[0]
    f2 = np.take_along_axis(d, o[1:2], 0)[0]
    return f1, f2, o[0]


def _periodic_pts(rng, n, lo=0.0, hi=1.0, dmin=0.26):
    """n sites on the unit torus at least dmin apart (no sliver plates),
    and their 3 x 3 periodic copies."""
    pts = []
    for _ in range(n * 200):
        if len(pts) >= n:
            break
        px, py = rng.uniform(lo, hi), rng.uniform(lo, hi)
        ok = True
        for qx, qy in pts:
            ddx = abs(px - qx)
            ddy = abs(py - qy)
            if math.hypot(min(ddx, 1 - ddx), min(ddy, 1 - ddy)) < dmin:
                ok = False
                break
        if ok:
            pts.append((px, py))
    out = []
    for px, py in pts:
        for ox in (-1, 0, 1):
            for oy in (-1, 0, 1):
                out.append((px + ox, py + oy))
    return pts, out


def basalt_top(var, crack_w=0.010):
    """Cracked volcanic rock: plates of dark basalt with pale edges, darker
    cracks, a little ash in the hollows. The plates are the same near the
    cell's edge in every variant (edges match)."""
    x, y = T.top_grid()
    sh = x.shape
    d = T.edge_dist(x, y)
    m = sstep(0.07, 0.3, d)
    n = T.lerp(T.fbm(sh, 0.08, 2401), T.fbm(sh, 0.08, 2410 + var), m)
    col = lerp(BASALT[1], BASALT[0], sstep(-1.2, 1.2, n))
    grit = T.fnoise(sh, 0.004, 2420 + var)
    col = col * (1 + 0.10 * grit)[..., None]
    # plates from a periodic voronoi (shared layout: the edges agree)
    rng = np.random.default_rng(2430)
    _, pts = _periodic_pts(rng, 7, dmin=0.28)
    f1, f2, idx = voronoi(x, y, pts)
    edge = f2 - f1
    crack = T.cover(edge - crack_w)
    tones = np.random.default_rng(2431 + var).uniform(0.88, 1.10, len(pts))
    tones_e = np.random.default_rng(2431).uniform(0.88, 1.10, len(pts))
    tone = lerp(tones_e[idx], tones[idx], m)
    col = col * tone[..., None]
    # plate rims catch light, the cracks are dark with a little ash
    rim = sstep(0.05, 0.012, edge) * (1 - crack)
    col = col * (1 + 0.18 * rim)[..., None]
    col = lerp(col, BASALT[2] * 0.7, crack)
    # pale vesicles (the bubbles of cooled lava)
    rng2 = np.random.default_rng(2440 + var)
    for k in range(10):
        cx, cy = rng2.uniform(0.14, 0.86, 2)
        dv = T.sd_circle(x, y, cx, cy, rng2.uniform(0.006, 0.012))
        col = lerp(col, BASALT[2] * 0.8, T.cover(dv))
    hgt = 0.004 * n - 0.012 * crack + 0.003 * rim
    col = T.darken_top_edges(col, x, y, 0.08)
    return Tex(col, hgt)


def basalt_side(glow_notch=False, emit_zero=False):
    """Columnar basalt: vertical columns (four per metre, whole numbers so
    the faces continue), each a little different in tone, with horizontal
    joints. glow_notch: the lava crack of the lava block reaching this face
    (the right face) at u = 0.5, glowing."""
    U, Z = T.side_grid()
    sh = U.shape
    Hm = T.FLOOR_M
    cols_n = 5
    uu = U * cols_n
    k = np.floor(uu)
    f = uu - k
    tone = 0.86 + 0.24 * np.mod(np.sin(k * 12.9898) * 43758.5453, 1.0)
    n = T.fbm(sh, 0.05, 2501)
    col = lerp(BASALT[1], BASALT[0], sstep(-1.2, 1.2, n)) * tone[..., None]
    # rounded columns: lit left, dark seams
    prof = np.sin(math.pi * f)
    col = col * (0.78 + 0.30 * prof + 0.08 * (0.5 - f))[..., None]
    seam = T.cover(np.minimum(f, 1 - f) / cols_n - 0.004)
    col = lerp(col, BASALT[2] * 0.55, seam)
    # horizontal joints, staggered per column (one per floor, periodic)
    jz = np.mod((Z + Hm) / Hm + 0.37 * np.mod(k * 0.618, 1.0), 1.0)
    joint = T.cover(np.abs(jz - 0.5) * Hm - 0.004) * (1 - seam)
    col = lerp(col, BASALT[2] * 0.6, joint)
    hgt = 0.01 * prof - 0.01 * seam - 0.006 * joint
    # the top lip: a band of the plate colour
    edge = 0.02
    m = sstep(edge + 0.006, edge - 0.004, -Z)
    col = lerp(col, BASALT[0] * 1.1, m)
    emit = np.zeros(sh + (3,)) if emit_zero else None
    if glow_notch:
        emit = np.zeros(sh + (3,))
        w = 0.05 * sstep(-0.20, 0.0, Z) + 0.008
        dd = np.abs(np.mod(U, 1.0) - 0.5) - w * (1 + 0.3 * np.sin(Z * 60))
        notch = T.cover(dd) * sstep(-0.24, -0.10, Z)
        core = T.cover(dd + 0.012) * sstep(-0.18, -0.06, Z)
        col = lerp(col, rgb(255, 120, 30), notch)
        col = lerp(col, rgb(255, 220, 110), core)
        emit = lerp(emit, rgb(255, 90, 20), notch)
        emit = lerp(emit, rgb(255, 200, 90), core)
        # the rock around the notch reddened by the heat
        warm = np.exp(-(np.maximum(dd, 0) / 0.05) ** 2) * sstep(-0.3, -0.05, Z) * (1 - notch)
        col = lerp(col, col * np.array([1.6, 0.9, 0.7]), warm * 0.6)
        emit = emit + rgb(255, 70, 10)[None, None, :] * (warm * 0.12)[..., None]
        hgt = hgt - 0.02 * notch
    return Tex(col, hgt, emit=emit)


# ---------------------------------------------------------------------------
# lava crack: basalt with a glowing fissure running along X through the cell
# ---------------------------------------------------------------------------

# how hot the fissure is in each state of the timed trap: (lava colour mix,
# emission gain, heat halo, crust cover)
LAVA_STATE = {
    'cold': (0.0, 0.10, 0.0, 0.85),
    'wake': (0.75, 0.70, 0.6, 0.35),
    'hot': (1.0, 1.0, 1.0, 0.10),
    'cool': (0.45, 0.30, 0.35, 0.60),
}


def lava_top(var, state='hot', axis='x'):
    """Basalt with a fissure through the middle of the cell, along X (y =
    0.5 at both edges, so a row joins) or along Y (the same, transposed).
    state: 'cold' a dark crack with a faint ember line, 'wake' glowing,
    'hot' molten and bright, 'cool' crusting over, dull red."""
    mix, gain, halo, crust_cover = LAVA_STATE[state]
    base = basalt_top(var, crack_w=0.008)
    x, y = T.top_grid()
    sh = x.shape
    wv = T.noise1(sh[1], 0.06, 2600 + var)[None, :]
    env = np.sin(math.pi * x) ** 1.5
    yc = 0.5 + 0.07 * wv * env + 0.03 * np.sin(2 * math.pi * x * 2 + var) * env
    wid = 0.058 + 0.018 * T.noise1(sh[1], 0.03, 2610 + var)[None, :] * env
    dd = np.abs(y - yc) - wid
    fis = T.cover(dd)
    core = T.cover(dd + wid * 0.55)
    ember = T.cover(dd + wid * 0.80)
    heat = np.exp(-(np.maximum(dd, 0) / 0.07) ** 2) * (1 - fis)
    col = base.col * (1 - 0.35 * heat)[..., None]
    col = lerp(col, col * np.array([1.7, 0.85, 0.6]), heat * 0.55 * halo)
    crust = T.fnoise(sh, 0.012, 2620 + var)
    lav = lerp(rgb(230, 70, 14), rgb(255, 150, 40), sstep(-1.0, 1.0, crust))
    # the empty crack: dark, deep
    dark = rgb(34, 22, 22)
    lav = lerp(rgb(120, 28, 12), lav, sstep(0.3, 1.0, mix))
    cw = float(np.clip((mix - 0.7) / 0.3, 0, 1)) ** 1.5
    fill = lerp(dark, lav, min(1.0, mix * 1.6))
    col = lerp(col, fill, fis)
    col = lerp(col, rgb(255, 232, 140), core * cw)
    emit = np.zeros(sh + (3,))
    emit = lerp(emit, lav * 0.85 * gain, fis * min(1.0, mix * 1.6))
    emit = lerp(emit, rgb(255, 214, 110) * gain, core * cw)
    emit = emit + rgb(255, 70, 10)[None, None, :] * (heat * 0.10 * halo)[..., None]
    if state == 'cold':
        # a faint ember line deep in the crack
        col = lerp(col, rgb(120, 36, 18), ember * 0.7)
        emit = emit + rgb(255, 60, 10)[None, None, :] * (ember * 0.15)[..., None]
    # cooled crusts on the melt: more of them when colder
    rng = np.random.default_rng(2630 + var)
    ncr = int(3 + 10 * crust_cover)
    for k in range(ncr):
        cx = rng.uniform(0.08, 0.92)
        cy = 0.5 + 0.07 * float(np.interp(cx, (np.arange(sh[1]) + 0.5) / sh[1], wv[0])) * math.sin(math.pi * cx) ** 1.5
        dc = T.sd_ellipse(x, y, cx, cy, rng.uniform(0.02, 0.04), rng.uniform(0.014, 0.026), rng.uniform(-0.4, 0.4))
        cm = T.cover(dc) * fis * (1.0 if state != 'cold' else 0.0)
        col = lerp(col, rgb(70, 36, 30), cm)
        emit = lerp(emit, np.zeros(3), cm)
    hgt = base.hgt * (1 - fis) - 0.03 * fis + 0.01 * core * mix
    if axis == 'y':
        col, emit, hgt = col.transpose(1, 0, 2), emit.transpose(1, 0, 2), hgt.T
    return Tex(np.ascontiguousarray(col), np.ascontiguousarray(hgt), emit=np.ascontiguousarray(emit))


def lava_side(state='hot', notch=True):
    """The basalt side; with notch, the fissure breaking out at u = 0.5
    (right face for a crack along X, front face for one along Y)."""
    if not notch:
        return basalt_side(False, emit_zero=True)
    t = basalt_side(True)
    mix, gain, halo, _ = LAVA_STATE[state]
    plain = basalt_side(False, emit_zero=True)
    k = max(mix, 0.25 if state == 'cold' else 0.0)
    if state == 'cold':
        # dark notch, a faint ember
        U, Z = T.side_grid()
        w = 0.05 * sstep(-0.20, 0.0, Z) + 0.008
        dd = np.abs(np.mod(U, 1.0) - 0.5) - w
        notch_m = T.cover(dd) * sstep(-0.24, -0.10, Z)
        plain.col = lerp(plain.col, rgb(34, 22, 22), notch_m)
        return plain
    t.col = lerp(plain.col, t.col, k)
    t.emit = t.emit * gain * mix
    return t


# ---------------------------------------------------------------------------
# stone path: flat orange stepping stones set in the red soil
# ---------------------------------------------------------------------------

def path_top(var):
    x, y = T.top_grid()
    sh = x.shape
    d = T.edge_dist(x, y)
    m = sstep(0.07, 0.3, d)
    col, hgt = _dirt_colour(x, y, 7, m)
    col = col * 0.92
    rng = np.random.default_rng(2700 + var)
    # four big flat stones in a loose 2 x 2, jittered per variant, all inside
    # the 0.07 m margin
    cells = [(0.28, 0.28), (0.72, 0.30), (0.30, 0.72), (0.70, 0.71)]
    for k, (cx, cy) in enumerate(cells):
        cx += rng.uniform(-0.03, 0.03)
        cy += rng.uniform(-0.03, 0.03)
        rx, ry = rng.uniform(0.15, 0.18), rng.uniform(0.14, 0.17)
        ang = rng.uniform(-0.5, 0.5)
        ds = T.sd_ellipse(x, y, cx, cy, rx, ry, ang)
        # a soft rounded-square: blend with a box
        db = T.sd_box(x, y, cx, cy, rx * 0.86, ry * 0.86, 0.06)
        ds = 0.5 * (ds + db)
        sh_ = np.clip(1 - np.maximum(T.sd_box(x, y, cx + 0.012, cy + 0.016, rx * 0.86, ry * 0.86, 0.06), 0) / 0.02,
                      0, 1)
        col *= (1 - 0.35 * sh_ * (1 - T.cover(ds)))[..., None]
        n = T.fbm(sh, 0.05, 2710 + k + 10 * var)
        c = lerp(STONE[1], STONE[0], sstep(-1.2, 1.2, n)) * rng.uniform(0.92, 1.06)
        # lit front-left rim, a greyer weathered patch
        rim = sstep(-0.03, 0.0, ds) * (1 - sstep(-0.004, 0.004, ds))
        lx = (x - cx) + (y - cy)
        c = c * (1 + 0.14 * rim * np.clip(-lx * 8, -1, 1))[..., None]
        c = lerp(c, rgb(170, 150, 136), sstep(0.6, 1.8, T.fnoise(sh, 0.03, 2720 + k)) * 0.5)
        mm = T.cover(ds)
        col = lerp(col, c, mm)
        hgt = hgt * (1 - mm) + mm * (0.012 + 0.002 * n)
        # a hairline crack on one stone
        if k == (var + 1) % 4:
            dc = T.sd_polyline(x, y, [(cx - 0.08, cy + 0.02), (cx - 0.01, cy - 0.02), (cx + 0.04, cy + 0.03),
                                      (cx + 0.09, cy + 0.0)], 0.0025)
            col = lerp(col, c * 0.55, T.cover(dc) * mm)
    col = T.darken_top_edges(col, x, y, 0.08)
    return Tex(col, hgt)


def path_side():
    return dirt_side()


# ---------------------------------------------------------------------------
# tar pit (a surface, like quicksand): glossy black, an oily sheen, bubbles
# ---------------------------------------------------------------------------

TAR_RIM = rgb(46, 34, 34)
TAR_A = rgb(26, 20, 24)
TAR_B = rgb(62, 46, 64)


def tar_top(var):
    """Thick glossy tar (the zone's quicksand): a black-violet pool with an
    oily sheen, a thin glossy amber rim along the cell's edges (the pit's
    meniscus; inside a big pool it reads as the faint grid), and two or three
    big slow bubbles, one of them popping."""
    x, y = T.top_grid()
    sh = x.shape
    rng = np.random.default_rng(2800 + var)
    d = T.edge_dist(x, y)
    m = sstep(0.05, 0.25, d)
    n1 = T.lerp(T.fnoise(sh, 0.12, 2801), T.fnoise(sh, 0.12, 2802 + var), m)
    n2 = T.lerp(T.fnoise(sh, 0.07, 2811, aniso=(1.0, 0.5)), T.fnoise(sh, 0.07, 2812 + var, aniso=(1.0, 0.5)), m)
    col = lerp(TAR_A, TAR_B, sstep(-1.2, 1.8, n1) * 0.9)
    band = np.sin(2.2 * n2 + 1.4 * n1)
    col = lerp(col, rgb(160, 110, 64), sstep(0.93, 0.995, band) * 0.35)
    col = lerp(col, rgb(70, 130, 140), sstep(0.93, 0.995, -band) * 0.25)
    hgt = 0.004 * band
    # the rim: a darker gutter, then a thin bright glossy amber lip at the edge
    gutter = sstep(0.07, 0.03, d) * (1 - sstep(0.03, 0.022, d))
    col = col * (1 - 0.45 * gutter)[..., None]
    lip = sstep(0.026, 0.016, d)
    col = lerp(col, rgb(196, 120, 52), lip * 0.85)
    col = lerp(col, rgb(255, 214, 140), sstep(0.012, 0.006, d) * 0.7)
    hgt += 0.006 * lip
    # big slow bubbles: glossy domes with a catchlight, one popping
    placed = []
    want = 2 + var % 2
    for k in range(80):
        if len(placed) >= want:
            break
        br = rng.uniform(0.07, 0.10)
        bx, by = rng.uniform(0.12 + br, 0.88 - br, 2)
        if any(math.hypot(bx - px, by - py) < br + pr + 0.04 for px, py, pr in placed):
            continue
        placed.append((bx, by, br))
    for k, (bx, by, br) in enumerate(placed):
        db = T.sd_circle(x, y, bx, by, br)
        if k == 0:
            ringm = T.cover(np.abs(db) - 0.008)
            col = lerp(col, rgb(120, 96, 118), ringm)
            col = lerp(col, rgb(10, 6, 8), T.cover(db + 0.012))
            # the splash ring around the pop
            col = lerp(col, rgb(90, 72, 90), T.cover(np.abs(db - 0.035) - 0.004) * 0.7)
            hgt += 0.006 * ringm
            continue
        dome = T.cover(db)
        shade = np.clip(1 - ((x - bx) + (y - by)) / (2 * br), 0.4, 1.4)
        col = lerp(col, rgb(48, 34, 50) * shade[..., None], dome)
        hl = T.cover(T.sd_ellipse(x, y, bx - br * 0.38, by - br * 0.30, br * 0.30, br * 0.18, 0.6))
        col = lerp(col, rgb(245, 232, 250), hl * dome)
        col = lerp(col, rgb(170, 110, 60), T.cover(np.abs(db) - 0.004) * 0.6)
        hgt += dome * np.sqrt(np.clip(-db / br, 0, 1)) * br * 0.7
    return Tex(col, hgt)


def tar_height(X, Y, var=0):
    """Geometry of the tar slab: flat (pools of many cells join)."""
    return 0 * X


# ---------------------------------------------------------------------------
# previews (plain python3)
# ---------------------------------------------------------------------------

# ---------------------------------------------------------------------------
# bone floor: bleached fossil bones inlaid flush in the soil (decorative)
# ---------------------------------------------------------------------------

def bone_top(var=0):
    x, y = T.top_grid()
    sh = x.shape
    d = T.edge_dist(x, y)
    m = sstep(0.07, 0.3, d)
    col, hgt = _dirt_colour(x, y, 11 + var, m)
    col = col * 0.9
    bn = T.fnoise(sh, 0.01, 2900 + var)
    bone = lerp(rgb(214, 196, 160), BONE, sstep(-1.0, 1.0, bn))
    # a spine along the diagonal, ribs off it, all 0.07 m inside the edge
    dsp = T.sd_seg(x, y, 0.16, 0.20, 0.84, 0.80, 0.030)
    vert = np.full(sh, 1e9)
    for k in range(7):
        t = (k + 0.5) / 7
        cx, cy = 0.16 + 0.68 * t, 0.20 + 0.60 * t
        vert = np.minimum(vert, T.sd_circle(x, y, cx, cy, 0.045))
    ribs = np.full(sh, 1e9)
    for k in range(4):
        t = 0.22 + 0.18 * k
        cx, cy = 0.16 + 0.68 * t, 0.20 + 0.60 * t
        for s_ in (-1, 1):
            pts = [(cx, cy), (cx - s_ * 0.13, cy + s_ * 0.10), (cx - s_ * 0.18, cy + s_ * 0.22)]
            pts = [(min(max(px, 0.09), 0.91), min(max(py, 0.09), 0.91)) for px, py in pts]
            ribs = np.minimum(ribs, T.sd_polyline(x, y, pts, 0.022))
    dall = np.minimum(np.minimum(dsp, vert), ribs)
    shd = np.clip(1 - np.maximum(T.sd_seg(x, y, 0.17, 0.215, 0.85, 0.815, 0.03), 0) / 0.02, 0, 1)
    col *= (1 - 0.25 * shd * (1 - T.cover(dall)))[..., None]
    mm = T.cover(dall)
    col = lerp(col, bone, mm)
    # the gaps between vertebrae
    gaps = T.cover(np.abs(vert) - 0.003) * T.cover(dsp)
    col = lerp(col, bone * 0.7, gaps)
    hgt = hgt * (1 - mm) + 0.004 * mm
    col = T.darken_top_edges(col, x, y, 0.08)
    return Tex(col, hgt)


# ---------------------------------------------------------------------------
# jungle pool water (teal-green)
# ---------------------------------------------------------------------------

def water_top(var):
    x, y = T.top_grid()
    sh = x.shape
    d = T.edge_dist(x, y)
    m = sstep(0.05, 0.25, d)
    n1 = T.lerp(T.fnoise(sh, 0.10, 3001), T.fnoise(sh, 0.10, 3002 + var), m)
    n2 = T.lerp(T.fnoise(sh, 0.03, 3011), T.fnoise(sh, 0.03, 3012 + var), m)
    col = lerp(rgb(24, 96, 92), rgb(40, 132, 118), sstep(-1.4, 1.4, n1))
    # caustic ripples: thin light lines
    rip = np.abs(np.sin(3.0 * n2 + 1.0 * n1))
    col = lerp(col, rgb(120, 214, 190), sstep(0.07, 0.0, rip) * 0.55)
    # a few floating leaves / specks
    rng = np.random.default_rng(3020 + var)
    for k in range(2):
        cx, cy = rng.uniform(0.2, 0.8, 2)
        _leaf(x, y, col, np.zeros(sh), cx, cy, rng.uniform(0, 6.28), 0.05, 0.02, rgb(110, 170, 60))
    hgt = 0.003 * np.sin(3.0 * n2)
    return Tex(col, hgt)


# ---------------------------------------------------------------------------
# the pushable crate: a mossy stone cube with a fossil spiral
# ---------------------------------------------------------------------------

STONE_G = [rgb(150, 142, 124), rgb(122, 116, 102), rgb(96, 90, 80)]
MOSS = rgb(72, 140, 62)


def _stone(shp, seed):
    n = T.fbm(shp, 0.05, seed)
    return lerp(STONE_G[1], STONE_G[0], sstep(-1.2, 1.2, n)), n


def crate_top():
    x, y = T.top_grid()
    col, n = _stone(x.shape, 3101)
    d = T.edge_dist(x, y)
    moss = sstep(0.4, 1.4, T.fbm(x.shape, 0.06, 3102)) * (0.4 + 0.6 * sstep(0.2, 0.0, d))
    col = lerp(col, MOSS, moss * 0.9)
    col = col * (1 - 0.25 * sstep(0.04, 0.0, d))[..., None]
    return Tex(col, 0.004 * n + 0.004 * moss)


def _ammonite(u, v, cx, cy, R):
    """Signed-ish mask of an ammonite spiral (a log spiral groove + ribs)."""
    dx, dy = u - cx, v - cy
    r = np.hypot(dx, dy) + 1e-6
    th = np.arctan2(dy, dx)
    b = 0.20
    k = (np.log(r / R) / (2 * math.pi * b) - th / (2 * math.pi))
    groove = np.abs(k - np.round(k))
    shell = r < R
    ribs = np.abs(np.sin(th * 14 + np.log(r) * 3))
    return shell, groove, ribs, r


def crate_face(right=False):
    """Side texture of the crate (u along the face, z over one floor):
    stone, moss creeping down from the top, a big fossil spiral in the
    middle of the face."""
    U, Z = T.side_grid()
    Hm = T.FLOOR_M
    col, n = _stone(U.shape, 3111 + right)
    u = np.mod(U, 1.0)
    v = Z + Hm
    shell, groove, ribs, r = _ammonite(u, v, 0.5, Hm * 0.5, 0.19)
    sm = shell.astype(float) * sstep(0.19, 0.17, r)
    col = lerp(col, rgb(206, 180, 136), sm * 0.8)
    col = lerp(col, rgb(120, 96, 70), sm * sstep(0.12, 0.05, groove))
    col = col * (1 - 0.12 * sm * (ribs < 0.2))[..., None]
    rim = T.cover(np.abs(r - 0.19) - 0.006)
    col = lerp(col, rgb(90, 80, 66), rim)
    moss = sstep(0.3, 1.4, T.fbm(U.shape, 0.05, 3120 + right)) * sstep(-0.18, -0.02, Z)
    col = lerp(col, MOSS * 0.9, moss * 0.8)
    edge = np.minimum(u, 1 - u)
    col = col * (1 - 0.3 * sstep(0.03, 0.0, edge))[..., None]
    hgt = 0.004 * n - 0.01 * sm * sstep(0.12, 0.05, groove) + 0.004 * moss
    return Tex(col, hgt)


# ---------------------------------------------------------------------------
# fern-trunk bark (UV: u along the trunk in metres, v around 0..1)
# ---------------------------------------------------------------------------

def fernbark_tex():
    """Diamond leaf-scars of a tree fern / cycad trunk."""
    n = 256
    u = (np.arange(n) + 0.5) / n
    U, Vv = np.meshgrid(u, u)
    a = U * 6 + Vv * 12
    b = U * 6 - Vv * 12
    fa, fb = np.mod(a, 1.0), np.mod(b, 1.0)
    diamond = np.minimum(np.minimum(fa, 1 - fa), np.minimum(fb, 1 - fb))
    nz = T.fnoise((n, n), 0.03, 3201)
    col = lerp(rgb(112, 76, 50), rgb(168, 124, 80), sstep(0.0, 0.25, diamond))
    col = col * (1 + 0.08 * nz)[..., None]
    col = lerp(col, rgb(70, 46, 32), sstep(0.04, 0.0, diamond))
    hgt = 0.02 * sstep(0.0, 0.3, diamond)
    return Tex(col, hgt)


def fernend_tex():
    """The cut end of a fern trunk: a fibrous pale core, dark fibre dots, a
    bark ring."""
    n = 256
    u = (np.arange(n) + 0.5) / n
    X, Y = np.meshgrid(u, u)
    r = np.hypot(X - 0.5, Y - 0.5) * 2
    nz = T.fnoise((n, n), 0.01, 3211)
    col = lerp(rgb(214, 170, 110), rgb(186, 140, 90), sstep(-1, 1, nz))
    rng = np.random.default_rng(3212)
    for k in range(40):
        cx, cy = rng.uniform(0.2, 0.8, 2)
        col = lerp(col, rgb(110, 70, 44), T.cover(T.sd_circle(X, Y, cx, cy, 0.012), 1.0 / n) * (r < 0.8))
    col = lerp(col, rgb(100, 66, 42), sstep(0.82, 0.9, r))
    return Tex(col, 0.01 * nz)


def _preview(out):
    import os
    from PIL import Image
    os.makedirs(out, exist_ok=True)

    def save(name, tex, light):
        c = np.clip(tex.col * light, 0, 1)
        if tex.emit is not None:
            c = np.clip(c + tex.emit * 2.0, 0, 1)
        im = Image.fromarray(T.to_srgb8(c[::-1]), 'RGB')
        im.resize((im.width * 2, im.height * 2), Image.NEAREST).save(os.path.join(out, name + '.png'))
    for v in range(3):
        save('dirt_top_v%d' % v, dirt_top(v), LIGHT_TOP)
        save('grass_top_v%d' % v, grass_top(v), LIGHT_TOP)
        save('basalt_top_v%d' % v, basalt_top(v), LIGHT_TOP)
        save('path_top_v%d' % v, path_top(v), LIGHT_TOP)
        save('lava_top_v%d' % v, lava_top(v), LIGHT_TOP)
        save('water_top_v%d' % v, water_top(v), LIGHT_TOP)
        save('tar_top_v%d' % v, tar_top(v), LIGHT_TOP)
    save('earth_fill', earth_fill(), LIGHT_FRONT)
    save('bone_top', bone_top(0), LIGHT_TOP)
    save('crate_face', crate_face(), LIGHT_FRONT)
    save('crate_top', crate_top(), LIGHT_TOP)
    save('fernbark', fernbark_tex(), LIGHT_FRONT)
    save('fernend', fernend_tex(), LIGHT_FRONT)
    for st in ('cold', 'wake', 'hot', 'cool'):
        save('lava_%s' % st, lava_top(0, st), LIGHT_TOP)
        save('lava_%s_y' % st, lava_top(0, st, 'y'), LIGHT_TOP)
        save('lavaside_%s' % st, lava_side(st), LIGHT_RIGHT)
    save('grass_side', grass_side(), LIGHT_FRONT)
    save('basalt_side', basalt_side(), LIGHT_FRONT)
    save('basalt_side_notch', basalt_side(True), LIGHT_RIGHT)


if __name__ == '__main__':
    _preview(sys.argv[1] if len(sys.argv) > 1 else '/tmp/dino_tex')
