"""Monster Hop - Mummy Desert: the block, fill and surface textures (numpy).

Colours are albedos written in sRGB 0-255. The desert light is warm (the top
of a block gets about (1.26, 1.03, 0.75) x albedo in linear light, the front
face (0.72, 0.57, 0.42), the right face (0.43, 0.32, 0.23)), so the albedos
here are paler and yellower than the sand they end up as.

    python3 zones_df_tex_desert.py OUTDIR      # previews of every texture, lit
"""
import math
import sys

import numpy as np

import zones_df_tex as T
from zones_df_tex import rgb, lerp, sstep, Tex

LIGHT_TOP = np.array([1.26, 1.03, 0.75])
LIGHT_FRONT = np.array([0.72, 0.57, 0.42])
LIGHT_RIGHT = np.array([0.43, 0.32, 0.23])

SAND_A = rgb(232, 204, 140)     # stoss slope, lit
SAND_B = rgb(212, 174, 106)     # lee of a ripple
SAND_C = rgb(242, 218, 160)     # crest highlight
STRATA = [rgb(214, 150, 92), rgb(196, 128, 76), rgb(224, 168, 104), rgb(184, 116, 70)]


def ripple_profile(ph):
    """Wind ripples: a thin bright crest, a short dark lee, a long stoss slope."""
    t = np.mod(ph / (2 * math.pi), 1.0)
    lee = sstep(0.0, 0.06, t) * (1 - sstep(0.06, 0.34, t))         # dark band right behind the crest
    stoss = sstep(0.3, 1.0, t)                                     # brightening towards the next crest
    crest = np.exp(-((t - 1.0) / 0.035) ** 2) + np.exp(-(t / 0.02) ** 2)
    shade = -1.0 * lee + 0.55 * stoss + 0.9 * crest
    hgt = np.where(t < 0.08, 1 - t / 0.08, (t - 0.08) / 0.92) * 0.004
    return shade, hgt


def _sand_colour(x, y, var, m):
    sh = x.shape
    ws = T.fbm(sh, 0.16, 11, octaves=2) * 0.9
    wv = T.fbm(sh, 0.14, 20 + var, octaves=2) * 1.5
    ph = 2 * math.pi * (1 * x + 6 * y) + ws + m * wv
    shade, hgt = ripple_profile(ph)
    col = lerp(SAND_A, SAND_B, np.clip(-shade, 0, 1))
    col = lerp(col, SAND_C, np.clip(shade - 0.4, 0, 1) * 0.8)
    mot = T.lerp(T.fbm(sh, 0.2, 31), T.fbm(sh, 0.2, 40 + var), m)
    col = col * (1 + 0.035 * mot)[..., None]
    grain = T.fnoise(sh, 0.004, 50 + var)
    col = col * (1 + 0.03 * grain)[..., None]
    return col, hgt


def sand_top(var):
    x, y = T.top_grid()
    d = T.edge_dist(x, y)
    m = sstep(0.07, 0.30, d)
    col, hgt = _sand_colour(x, y, var, m)
    rng = np.random.default_rng(700 + var)
    stones = [rgb(206, 184, 150), rgb(190, 164, 128), rgb(220, 200, 170)]
    if var == 1:
        T.pebbles(x, y, col, hgt, rng, 1, 0.05, 0.06, stones, region=0.2, shade=0.6)
        T.pebbles(x, y, col, hgt, rng, 1, 0.022, 0.028, stones, region=0.2, shade=0.6)
    if var == 2:
        # a sun-bleached flat stone half buried, a wind streak behind it
        dd = T.sd_ellipse(x, y, 0.56, 0.47, 0.16, 0.04, 0.35)
        col = col * (1 - 0.08 * np.clip(1 - np.maximum(dd, 0) / 0.05, 0, 1))[..., None]
        T.pebbles(x, y, col, hgt, np.random.default_rng(5), 1, 0.065, 0.065, [rgb(214, 190, 156)],
                  region=0.3, shade=0.5)
    col = T.darken_top_edges(col, x, y)
    return Tex(col, hgt)


def _strata(U, Z, seed=3):
    sh = U.shape
    v = (Z + T.FLOOR_M) / T.FLOOR_M
    warp = T.fbm(sh, 0.05, seed) * 0.03 + 0.02 * np.sin(2 * math.pi * U)
    t = 2 * (v + warp)                                       # two bands per floor
    col = T.bands(t, [(0.0, STRATA[0], 0.05), (0.62, STRATA[2], 0.12),
                      (1.0, STRATA[1], 0.05), (1.55, STRATA[3], 0.12)], 2.0)
    f = np.mod(t, 1.0)
    # a thin dark seam at the top of each band, fine lamination inside
    seam = np.exp(-((f - 0.03) / 0.035) ** 2) + np.exp(-((f - 1.03) / 0.035) ** 2)
    col = col * (1 - 0.12 * seam)[..., None]
    lam = np.sin(2 * math.pi * (8 * (v + warp * 1.4)))
    col = col * (1 + 0.03 * lam)[..., None]
    grit = T.fnoise(sh, 0.006, seed + 9)
    col = col * (1 + 0.05 * grit)[..., None]
    hgt = -0.006 * seam + 0.002 * lam
    return col, hgt


def sand_fill():
    U, Z = T.side_grid()
    col, hgt = _strata(U, Z)
    return Tex(col, hgt)


def lip(U, Z, depth=0.075, amp=0.02, seed=5):
    """1 where the top material wraps over the side: a wavy lower edge."""
    n = T.noise1(U.shape[1], 0.05, seed)[None, :]
    edge = depth + amp * n
    return sstep(edge + 0.006, edge - 0.006, -Z), edge


def sand_side():
    U, Z = T.side_grid()
    col, hgt = _strata(U, Z)
    m, edge = lip(U, Z)
    x = U
    ycol, _ = _sand_colour(x, np.full_like(x, 0.02), 0, np.zeros_like(x))
    # the sand crust darkens a touch at its lower lip, casts a soft line below
    below = np.clip(1 - (-Z - edge) / 0.03, 0, 1) * (-Z > edge)
    col = col * (1 - 0.22 * below)[..., None]
    col = lerp(col, ycol * 0.97, m)
    hgt = hgt * (1 - m) + 0.004 * m
    return Tex(col, hgt)


# --- sandstone: one big slab per cell ------------------------------------

STONE_A = rgb(228, 170, 126)
STONE_B = rgb(210, 148, 108)


def sandstone_top(var):
    """A big square temple floor tile per cell: a soft pillow bevel at the
    joint, an incised border line inset 0.11 m, sedimentary striation."""
    x, y = T.top_grid()
    sh = x.shape
    d = T.edge_dist(x, y)
    m = sstep(0.05, 0.2, d)
    n = T.lerp(T.fbm(sh, 0.10, 61), T.fbm(sh, 0.10, 70 + var), m)
    col = lerp(STONE_B, STONE_A, 0.5 + 0.35 * n)
    stri = np.sin(2 * math.pi * (3 * x + 2 * y) + 1.5 * T.fbm(sh, 0.15, 65 + var) * m)
    col = col * (1 + 0.035 * stri)[..., None]
    tone = [1.0, 0.975, 1.02][var % 3]
    col = col * tone
    pits = T.fnoise(sh, 0.005, 80 + var)
    col = col * (1 - 0.12 * sstep(1.8, 2.6, pits))[..., None]
    grain = T.fnoise(sh, 0.003, 90 + var)
    col = col * (1 + 0.03 * grain)[..., None]
    hgt = 0.0015 * n
    # pillow bevel: brighter on the front-left rim (towards the sun), darker back-right
    rim = 1 - sstep(0.0, 0.045, d)
    facing = np.where(np.minimum(x, y) <= np.minimum(1 - x, 1 - y), 1.0, -1.0)
    col = col * (1 + 0.07 * rim * facing)[..., None]
    col = col * (1 - 0.10 * (1 - sstep(0.0, 0.012, d)))[..., None]
    hgt = hgt - 0.006 * rim
    # the incised border
    dl = np.abs(T.sd_box(x, y, 0.5, 0.5, 0.39, 0.39, 0.03)) - 0.007
    line = T.cover(dl)
    col = col * (1 - 0.30 * line)[..., None]
    lit = T.cover(np.abs(T.sd_box(x, y, 0.5, 0.5, 0.39 - 0.012, 0.39 - 0.012, 0.03)) - 0.004)
    col = col * (1 + 0.10 * lit * (1 - line))[..., None]
    hgt = hgt - 0.004 * line
    if var == 1:
        pts = [(0.66, 0.72), (0.58, 0.63), (0.60, 0.56), (0.52, 0.48), (0.53, 0.40)]
        dc = T.sd_polyline(x, y, pts, 0.004)
        col = col * (1 - 0.45 * T.cover(dc))[..., None]
        hgt = hgt - 0.004 * T.cover(dc)
    if var == 2:
        dd = T.sd_ellipse(x, y, 0.5, 0.52, 0.2, 0.15, 0.3)
        col = col * (1 - 0.06 * np.clip(-dd / 0.1, 0, 1))[..., None]
    return Tex(col, hgt)


def sandstone_side():
    """A sandstone block face: one ashlar per cell and floor."""
    U, Z = T.side_grid()
    sh = U.shape
    v = (Z + T.FLOOR_M) / T.FLOOR_M
    n = T.fbm(sh, 0.08, 63)
    col = lerp(rgb(196, 134, 96), rgb(214, 154, 110), 0.5 + 0.35 * n)
    lam = np.sin(2 * math.pi * (6 * v + 0.3 * T.fbm(sh, 0.1, 64)))
    col = col * (1 + 0.03 * lam)[..., None]
    du = np.minimum(U, 1 - U)
    dv = np.minimum(v, 1 - v) * T.FLOOR_M
    dj = np.minimum(du, dv)
    g = 1 - sstep(0.0, 0.025, dj)
    col = col * (1 - 0.28 * g)[..., None]
    hgt = 0.0015 * n - 0.008 * g
    return Tex(col, hgt)


# --- limestone brick (pyramid) ---------------------------------------------

LIME_A = rgb(222, 206, 166)
LIME_B = rgb(204, 186, 146)


def _course_tone(i, j, seed):
    r = np.random.default_rng(seed + 17 * i + 131 * j)
    return r.uniform(0.93, 1.05)


def brick_side_base(seed=21):
    """Limestone courses: two per floor, blocks 0.5 m long, staggered."""
    U, Z = T.side_grid()
    sh = U.shape
    v = (Z + T.FLOOR_M) / T.FLOOR_M
    course = np.floor(v * 2).astype(int) % 2           # 0 lower, 1 upper
    off = np.where(course == 0, 0.0, 0.25)
    uu = (U + off) % 1.0
    blk = np.floor(uu * 2).astype(int) % 2
    fu = (uu * 2) % 1.0
    fv = (v * 2) % 1.0
    tone = np.ones(sh)
    for c in (0, 1):
        for b in (0, 1):
            tone[(course == c) & (blk == b)] = _course_tone(c, b, seed)
    n = T.fbm(sh, 0.06, seed + 1)
    col = lerp(LIME_B, LIME_A, 0.55 + 0.3 * n) * tone[..., None]
    # joints (mortar lines), rounded worn arrises
    du = np.minimum(fu, 1 - fu) * 0.5
    dv = np.minimum(fv, 1 - fv) * T.FLOOR_M * 0.5
    dj = np.minimum(du, dv)
    g = 1 - sstep(0.004, 0.022, dj)
    col = lerp(col, rgb(150, 128, 98), g * 0.75)
    # weathering: a darker, grittier lower band on each block
    wv = sstep(0.4, 0.0, fv) * 0.08
    col = col * (1 - wv)[..., None]
    pits = T.fnoise(sh, 0.005, seed + 2)
    col = col * (1 - 0.12 * sstep(1.7, 2.5, pits))[..., None]
    hgt = 0.002 * n - 0.012 * g
    return U, Z, v, col, hgt


def brick_side():
    U, Z, v, col, hgt = brick_side_base()
    return Tex(col, hgt)


def brick_fill():
    return brick_side()


def brick_top(var):
    """Two long limestone slabs per cell (joint along X at y = 0.5)."""
    x, y = T.top_grid()
    sh = x.shape
    d = T.edge_dist(x, y)
    m = sstep(0.05, 0.2, d)
    n = T.lerp(T.fbm(sh, 0.08, 121), T.fbm(sh, 0.08, 130 + var), m)
    col = lerp(LIME_B, LIME_A, 0.55 + 0.3 * n)
    half = (y > 0.5)
    col = col * np.where(half, 1.0, 0.965)[..., None]
    dj = np.minimum(d, np.abs(y - 0.5))
    g = 1 - sstep(0.004, 0.022, dj)
    col = lerp(col, rgb(150, 128, 98), g * 0.7)
    pits = T.fnoise(sh, 0.005, 140 + var)
    col = col * (1 - 0.10 * sstep(1.7, 2.5, pits))[..., None]
    hgt = 0.002 * n - 0.012 * g
    col = T.darken_top_edges(col, x, y, 0.10)
    return Tex(col, hgt)


# --- the hieroglyph panel (brick_win) --------------------------------------

TURQ = rgb(40, 178, 170)
TURQ_D = rgb(20, 110, 120)
TERRA = rgb(200, 78, 44)
GOLD = rgb(242, 178, 40)
INK = rgb(58, 38, 30)
PLASTER = rgb(238, 222, 184)
WHITE = rgb(246, 240, 226)


def _glyph_bird(x, y, cx, cy, s):
    """An ibis-like bird standing, facing left (x, y in metres on the face)."""
    body = T.sd_ellipse(x, y, cx + 0.01 * s, cy + 0.02 * s, 0.055 * s, 0.035 * s, -0.35)
    neck = T.sd_seg(x, y, cx - 0.035 * s, cy + 0.035 * s, cx - 0.045 * s, cy + 0.085 * s, 0.012 * s)
    head = T.sd_circle(x, y, cx - 0.045 * s, cy + 0.09 * s, 0.018 * s)
    beak = T.sd_polyline(x, y, [(cx - 0.055 * s, cy + 0.093 * s), (cx - 0.085 * s, cy + 0.075 * s),
                                (cx - 0.095 * s, cy + 0.055 * s)], 0.006 * s)
    tail = T.sd_poly(x, y, [(cx + 0.04 * s, cy + 0.03 * s), (cx + 0.085 * s, cy - 0.005 * s),
                            (cx + 0.05 * s, cy + 0.0 * s)])
    legs = np.minimum(T.sd_seg(x, y, cx, cy - 0.005 * s, cx - 0.01 * s, cy - 0.075 * s, 0.006 * s),
                      T.sd_seg(x, y, cx + 0.015 * s, cy - 0.005 * s, cx + 0.02 * s, cy - 0.075 * s, 0.006 * s))
    wing = T.sd_ellipse(x, y, cx + 0.02 * s, cy + 0.025 * s, 0.035 * s, 0.02 * s, -0.4)
    shape = np.minimum.reduce([body, neck, head, beak, tail, legs])
    return shape, wing, head


def _glyph_eye(x, y, cx, cy, s):
    """A stylised eye with a curl below (abstract, not a real sign)."""
    up = T.sd_ellipse(x, y, cx, cy + 0.01 * s, 0.07 * s, 0.035 * s)
    outline = np.abs(up) - 0.007 * s
    iris = T.sd_circle(x, y, cx + 0.005 * s, cy + 0.01 * s, 0.022 * s)
    brow = T.sd_polyline(x, y, [(cx - 0.07 * s, cy + 0.06 * s), (cx, cy + 0.07 * s), (cx + 0.08 * s, cy + 0.055 * s)],
                         0.008 * s)
    curl = T.sd_polyline(x, y, [(cx - 0.005 * s, cy - 0.025 * s), (cx - 0.01 * s, cy - 0.06 * s),
                                (cx + 0.02 * s, cy - 0.075 * s), (cx + 0.035 * s, cy - 0.06 * s)], 0.007 * s)
    tail = T.sd_seg(x, y, cx + 0.065 * s, cy + 0.0, cx + 0.095 * s, cy - 0.005 * s, 0.007 * s)
    return up, outline, iris, np.minimum.reduce([brow, curl, tail])


def _glyph_sun(x, y, cx, cy, s, sx=1.0):
    disk = T.sd_ellipse(x, y, cx, cy, 0.05 * s * sx, 0.05 * s)
    ring = np.abs(T.sd_ellipse(x, y, cx, cy, 0.065 * s * sx, 0.065 * s)) - 0.008 * s
    rays = np.full(x.shape, 1e9)
    for k in range(8):
        t = 2 * math.pi * k / 8 + math.pi / 8
        rays = np.minimum(rays, T.sd_seg(x, y, cx + 0.08 * s * sx * math.cos(t), cy + 0.08 * s * math.sin(t),
                                         cx + 0.1 * s * sx * math.cos(t), cy + 0.1 * s * math.sin(t), 0.007 * s))
    return disk, ring, rays


def _glyph_waves(x, y, cx, cy, s, sx=1.0):
    d = np.full(x.shape, 1e9)
    for k in range(3):
        yy = cy + (k - 1) * 0.04 * s
        pts = [(cx + (i / 12 - 0.5) * 0.15 * s * sx, yy + 0.012 * s * math.sin(i / 12 * 4 * math.pi))
               for i in range(13)]
        d = np.minimum(d, T.sd_polyline(x, y, pts, 0.007 * s))
    return d


def brick_win_face(right=False):
    """The carved and painted panel on a limestone block face. Front: a row of
    four signs. Right face (seen 3x narrower): one sun and waves, drawn 3x
    wider so they look round on the screen."""
    U, Z, v, col, hgt = brick_side_base(seed=41)
    x, y = U, Z + T.FLOOR_M                        # metres on the face, y up from the bottom
    Hh = T.FLOOR_M
    # the recessed panel with a raised frame
    pb = T.sd_box(x, y, 0.5, Hh / 2, 0.43, Hh / 2 - 0.055, 0.01)
    frame = np.abs(pb + 0.012) - 0.012
    inside = T.cover(pb + 0.024)
    col = lerp(col, PLASTER, inside)
    hgt = hgt * (1 - T.cover(pb)) - 0.010 * T.cover(pb)
    fr = T.cover(frame)
    col = lerp(col, lerp(GOLD, rgb(210, 150, 50), 0.4), fr)
    hgt = hgt + 0.006 * fr
    # inner shadow line on the top and left inside the recess (sun from the front-left, above)

    def put(d, c, raise_=0.004):
        nonlocal col, hgt
        m = T.cover(d) * inside
        col = lerp(col, c, m)
        hgt = hgt + raise_ * m
    cy = Hh / 2 - 0.005
    if not right:
        # dividers between the four slots
        for k in (1, 2, 3):
            put(T.sd_box(x, y, 0.07 + k * 0.215, cy, 0.006, Hh / 2 - 0.1), INK, 0.002)
        s = 1.6
        b, wing, head = _glyph_bird(x, y, 0.178, cy - 0.005, s)
        put(b, TURQ_D)
        put(wing, TURQ, 0.005)
        put(T.sd_circle(x, y, 0.178 - 0.045 * s, cy - 0.005 + 0.09 * s, 0.006), INK)
        up, outline, iris, lines = _glyph_eye(x, y, 0.392, cy + 0.005, 1.35)
        put(up, WHITE)
        put(iris, INK)
        put(outline, INK)
        put(lines, INK)
        disk, ring, rays = _glyph_sun(x, y, 0.607, cy, 1.25)
        put(rays, GOLD)
        put(ring, GOLD)
        put(disk, TERRA)
        put(_glyph_waves(x, y, 0.822, cy, 1.25), TURQ)
    else:
        sx = 2.4
        disk, ring, rays = _glyph_sun(x, y, 0.32, cy, 1.3, sx)
        put(rays, GOLD)
        put(ring, GOLD)
        put(disk, TERRA)
        put(T.sd_box(x, y, 0.56, cy, 0.008 * sx, Hh / 2 - 0.1), INK, 0.002)
        put(_glyph_waves(x, y, 0.76, cy, 1.25, sx * 0.9), TURQ)
    # a soft darkening inside the top-left of the recess (depth)
    ao = sstep(0.05, 0.0, -(pb + 0.024)) * inside
    col = col * (1 - 0.12 * ao)[..., None]
    return Tex(col, hgt)


# --- oasis grass ------------------------------------------------------------

OASIS = [rgb(96, 170, 58), rgb(128, 196, 72), rgb(70, 142, 50), rgb(160, 210, 90)]


def _oasis_colour(x, y, var, m):
    sh = x.shape
    n1 = T.lerp(T.fbm(sh, 0.09, 201), T.fbm(sh, 0.09, 210 + var), m)
    n2 = T.fnoise(sh, 0.012, 220 + var)
    blades = T.fnoise(sh, 0.004, 230 + var, aniso=(0.6, 1.0))
    col = lerp(OASIS[0], OASIS[1], sstep(-1.0, 1.2, n1))
    col = lerp(col, OASIS[2], sstep(0.6, 1.8, -n2) * 0.6)
    col = lerp(col, OASIS[3], sstep(1.0, 2.2, n2) * 0.5)
    col = col * (1 + 0.08 * blades)[..., None]
    hgt = 0.004 * n2 + 0.002 * blades
    return col, hgt


def oasis_top(var):
    x, y = T.top_grid()
    d = T.edge_dist(x, y)
    m = sstep(0.07, 0.3, d)
    col, hgt = _oasis_colour(x, y, var, m)
    rng = np.random.default_rng(300 + var)
    if var in (1, 2):
        # small flowers: white and yellow dots with a centre
        k = 5 if var == 1 else 3
        cols = [rgb(250, 246, 230), rgb(255, 214, 70)] if var == 1 else [rgb(250, 130, 160), rgb(250, 246, 230)]
        for i in range(k):
            cx, cy = rng.uniform(0.15, 0.85, 2)
            c = cols[i % 2]
            for pk in range(5):
                t = 2 * math.pi * pk / 5 + rng.uniform(0, 1)
                dp = T.sd_circle(x, y, cx + 0.011 * math.cos(t), cy + 0.011 * math.sin(t), 0.009)
                col = lerp(col, c, T.cover(dp))
            col = lerp(col, rgb(240, 170, 40), T.cover(T.sd_circle(x, y, cx, cy, 0.006)))
            hgt = hgt + 0.004 * T.cover(T.sd_circle(x, y, cx, cy, 0.02))
    col = T.darken_top_edges(col, x, y, 0.14)
    return Tex(col, hgt)


def oasis_side():
    """Green grass wrapping over the edge, sand strata below (it sits on the
    sand fill)."""
    U, Z = T.side_grid()
    col, hgt = _strata(U, Z)
    # moist, darker sand just under the turf
    m, edge = lip(U, Z, depth=0.085, amp=0.028, seed=7)
    below = np.clip(1 - (-Z - edge) / 0.06, 0, 1) * (-Z > edge)
    col = col * (1 - 0.35 * below)[..., None]
    gcol, _ = _oasis_colour(U, np.full_like(U, 0.03), 0, np.zeros_like(U))
    # hanging blades: a jagged lower edge
    jag = 0.012 * np.abs(np.sin(2 * math.pi * 23 * U + 3 * T.noise1(U.shape[1], 0.02, 9)[None, :]))
    m2 = sstep(edge + jag + 0.006, edge + jag - 0.006, -Z)
    col = lerp(col, gcol * 0.92, m2)
    hgt = hgt * (1 - m2) + 0.004 * m2
    return Tex(col, hgt)


# --- surfaces -----------------------------------------------------------------

WATER_D = rgb(22, 128, 140)
WATER_M = rgb(38, 170, 172)
WATER_L = rgb(130, 226, 214)


def water_top(var):
    x, y = T.top_grid()
    sh = x.shape
    d = T.edge_dist(x, y)
    m = sstep(0.05, 0.3, d)
    n = T.lerp(T.fbm(sh, 0.16, 401), T.fbm(sh, 0.16, 410 + var), m)
    col = lerp(WATER_D, WATER_M, sstep(-1.4, 1.4, n))
    # long soft swells, lighter on their crests, and a few sparkles
    ws = T.fbm(sh, 0.14, 421) * 1.2
    wv = T.fbm(sh, 0.12, 430 + var) * 1.8
    ph = 2 * math.pi * (1 * x + 3 * y) + ws + m * wv
    sw = np.sin(ph)
    col = lerp(col, WATER_M * 1.08, sstep(0.3, 1.0, sw) * 0.5)
    col = lerp(col, WATER_L, sstep(0.9, 0.995, sw) * 0.45)
    rng = np.random.default_rng(460 + var)
    for k in range(3):
        cx, cy = rng.uniform(0.15, 0.85, 2)
        g = np.exp(-(((x - cx) / 0.035) ** 2 + ((y - cy) / 0.012) ** 2))
        col = lerp(col, rgb(236, 255, 246), g * 0.7)
    hgt = 0.002 * sw
    return Tex(col, hgt)


QS_EDGE = rgb(190, 140, 88)
QS_LIGHT = rgb(228, 188, 128)
QS_DARK = rgb(128, 76, 40)
QS_CORE = rgb(60, 30, 18)


def quicksand_top(var):
    """A sinking vortex: a three-armed spiral of light and dark wet sand that
    tightens into a dark core; the rim (the cell edge) is one flat tone so
    patches of quicksand join."""
    x, y = T.top_grid()
    sh = x.shape
    d = T.edge_dist(x, y)
    rng = np.random.default_rng(500 + var)
    cx, cy = 0.5 + rng.uniform(-0.03, 0.03), 0.5 + rng.uniform(-0.03, 0.03)
    dx, dy = x - cx, y - cy
    r = np.hypot(dx, dy)
    th = np.arctan2(dy, dx)
    arms = 3
    tw = 7.0 + var                                   # twist
    ph = arms * th + tw * np.log(r + 0.02) * 1.0 + T.fbm(sh, 0.05, 510 + var) * 0.5
    band = np.sin(ph)
    swirl = sstep(0.46, 0.20, r)                     # the vortex fades out before the rim
    col = lerp(QS_DARK, QS_LIGHT, 0.5 + 0.5 * band)
    col = lerp(QS_EDGE, col, swirl)
    # darker towards the centre and a dark core
    col = col * (1 - 0.5 * sstep(0.32, 0.0, r))[..., None]
    col = lerp(col, QS_CORE, sstep(0.13, 0.04, r))
    # a warning halo: a slightly redder ring just outside the vortex
    halo = np.exp(-((r - 0.40) / 0.05) ** 2) * (1 - sstep(0.45, 0.5, r))
    col = lerp(col, rgb(200, 104, 58), halo * 0.45)
    # wet sheen noise on the flat rim, a couple of bubbles near the core
    wet = T.fbm(sh, 0.06, 520)
    col = col * (1 + 0.04 * wet * (1 - swirl))[..., None]
    for k in range(2 + var % 2):
        t = rng.uniform(0, 2 * math.pi)
        rr = rng.uniform(0.12, 0.2)
        bx, by = cx + rr * math.cos(t), cy + rr * math.sin(t)
        br = rng.uniform(0.012, 0.018)
        db = T.sd_circle(x, y, bx, by, br)
        ringm = T.cover(np.abs(db) - 0.003)
        col = lerp(col, rgb(236, 206, 150), ringm * 0.9)
        col = lerp(col, rgb(120, 76, 44), T.cover(db + 0.004) * 0.5)
    hgt = 0.004 * band * swirl
    return Tex(col, hgt)


def quicksand_height(X, Y, var=0):
    """Geometry of the quicksand slab: a funnel, 0 at the rim."""
    r = np.hypot(X - 0.5, Y - 0.5)
    return -0.07 * (1 - sstep(0.0, 0.46, r)) ** 1.6


# --- cracked sandstone --------------------------------------------------------

def voronoi(x, y, pts):
    """Distances to the nearest and second nearest of pts (N x 2) and the
    index of the nearest."""
    d = np.stack([np.hypot(x - px, y - py) for px, py in pts], 0)
    o = np.argsort(d, axis=0)
    f1 = np.take_along_axis(d, o[:1], 0)[0]
    f2 = np.take_along_axis(d, o[1:2], 0)[0]
    return f1, f2, o[0]


def cracked_top(var):
    """Sandstone slab broken into plates: sand-filled cracks, plates tilted a
    little (their tone), the cell's joint as on the plain sandstone."""
    base = sandstone_top(0)
    x, y = T.top_grid()
    d = T.edge_dist(x, y)
    rng = np.random.default_rng(1200 + var)
    pts = [(rng.uniform(0.12, 0.88), rng.uniform(0.12, 0.88)) for _ in range(7 + var)]
    f1, f2, idx = voronoi(x, y, pts)
    edge = f2 - f1
    inner = sstep(0.07, 0.13, d)
    crack = T.cover(edge - 0.012) * inner
    tones = rng.uniform(0.9, 1.07, len(pts))
    col = base.col * np.where(inner[..., None] > 0.5, tones[idx][..., None], 1.0)
    col = col * (1 - 0.06 * sstep(0.03, 0.0, edge)[..., None] * inner[..., None])
    col = lerp(col, lerp(rgb(150, 96, 62), SAND_B, 0.35), crack * 0.95)
    hgt = base.hgt - 0.01 * crack
    return Tex(col, hgt)


def cracked_side():
    t = sandstone_side()
    U, Z = T.side_grid()
    pts = [(0.30, -0.02), (0.36, -0.12), (0.33, -0.2), (0.41, -0.3), (0.72, -0.05), (0.66, -0.16), (0.7, -0.24)]
    dc = np.minimum(T.sd_polyline(U, Z, pts[:4], 0.005), T.sd_polyline(U, Z, pts[4:], 0.005))
    c = T.cover(dc)
    t.col = t.col * (1 - 0.5 * c)[..., None]
    t.hgt = t.hgt - 0.006 * c
    return t


# --- gilded temple floor --------------------------------------------------------

GOLD_A = rgb(255, 222, 110)
GOLD_B = rgb(236, 176, 56)


def gold_top(var):
    """Four gilded tiles per cell, each with an engraved motif (v0 rosette,
    v1 sun disk with a turquoise inlay, v2 lotus diamond)."""
    x, y = T.top_grid()
    sh = x.shape
    d = T.edge_dist(x, y)
    n = T.fbm(sh, 0.1, 1300 + var)
    col = lerp(GOLD_B, GOLD_A, 0.55 + 0.25 * n)
    fx, fy = np.mod(x * 2, 1.0), np.mod(y * 2, 1.0)
    dt = np.minimum(np.minimum(fx, 1 - fx), np.minimum(fy, 1 - fy)) * 0.5
    joint = 1 - sstep(0.003, 0.014, dt)
    col = lerp(col, rgb(120, 70, 20), joint * 0.8)
    lx, ly = (fx - 0.5) * 0.5, (fy - 0.5) * 0.5            # metres from the tile centre
    r = np.hypot(lx, ly)
    eng = np.zeros(sh)
    inlay = np.zeros(sh)
    if var == 0:
        eng = T.cover(np.abs(r - 0.15) - 0.008) + T.cover(np.abs(r - 0.07) - 0.007)
        for k in range(8):
            t = k * math.pi / 4
            eng = np.maximum(eng, T.cover(T.sd_seg(lx, ly, 0.075 * math.cos(t), 0.075 * math.sin(t),
                                                   0.14 * math.cos(t), 0.14 * math.sin(t), 0.006)))
    elif var == 1:
        eng = T.cover(np.abs(r - 0.16) - 0.008)
        inlay = T.cover(r - 0.09)
        eng = np.maximum(eng, T.cover(np.abs(r - 0.09) - 0.006))
    else:
        dd = np.abs(lx) + np.abs(ly)
        eng = T.cover(np.abs(dd - 0.17) - 0.008) + T.cover(np.abs(dd - 0.09) - 0.007)
        inlay = T.cover(dd - 0.05)
    eng = np.clip(eng, 0, 1)
    col = lerp(col, rgb(150, 92, 26), eng * 0.85)
    col = lerp(col, TURQ, inlay * 0.9 if var == 1 else inlay * 0.0)
    col = lerp(col, TERRA, inlay * 0.9 if var == 2 else inlay * 0.0)
    col = T.darken_top_edges(col, x, y, 0.12)
    metal = np.clip(1 - joint - eng * 0.6 - inlay, 0, 1) * 0.4
    rough = 0.3 + 0.3 * eng + 0.4 * inlay + 0.1 * np.abs(n)
    hgt = -0.006 * joint - 0.004 * eng + 0.001 * n
    return Tex(col, hgt, rough=rough, metal=metal)


def gold_side():
    """The gilded floor's edge: a gold trim band over sandstone."""
    t = sandstone_side()
    U, Z = T.side_grid()
    band = sstep(0.075, 0.068, -Z)
    t.col = lerp(t.col, lerp(GOLD_B, GOLD_A, 0.5), band)
    line = T.cover(np.abs(-Z - 0.035) - 0.004)
    t.col = t.col * (1 - 0.35 * line * band)[..., None]
    t.metal = band * 0.4
    t.hgt = t.hgt + 0.004 * band
    return t


# --- dart wall (a brick block with three dart holes) ----------------------------

def dartwall_face(right=False):
    U, Z, v, col, hgt = brick_side_base(seed=51)
    x, y = U, Z + T.FLOOR_M
    cy = T.FLOOR_M * 0.55
    sx = 2.4 if right else 1.0
    for cx in (0.25, 0.5, 0.75):
        rim = T.sd_ellipse(x, y, cx, cy, 0.075 * sx, 0.075)
        hole = T.sd_ellipse(x, y, cx, cy, 0.05 * sx, 0.05)
        col = lerp(col, rgb(170, 150, 118), T.cover(rim) * 0.8)
        col = col * (1 - 0.25 * T.cover(rim) * np.clip((y - cy) / 0.05, 0, 1))[..., None]
        col = lerp(col, rgb(24, 14, 10), T.cover(hole))
        hgt = hgt + 0.004 * T.cover(rim) - 0.02 * T.cover(hole)
    # a warning: a red painted band of small triangles above the holes
    for k in range(5):
        cx = 0.1 + k * 0.2
        tri = T.sd_poly(x, y, [(cx - 0.05 * sx / (sx if not right else 1.2), cy + 0.12), (cx + 0.05 * sx / (sx if not right else 1.2), cy + 0.12),
                               (cx, cy + 0.075)])
        col = lerp(col, TERRA, T.cover(tri) * 0.9)
    return Tex(col, hgt)


# --- the pushable sandstone block -------------------------------------------------

def crate_face(right=False):
    """A sandstone block face with a carved eye in a frame (u across the face,
    z over one floor)."""
    t = sandstone_side()
    U, Z = T.side_grid()
    x, y = U, Z + T.FLOOR_M
    Hh = T.FLOOR_M
    sx = 2.4 if right else 1.0
    fr = np.abs(T.sd_box(x, y, 0.5, Hh / 2, 0.40, Hh / 2 - 0.07, 0.02)) - 0.012
    t.col = t.col * (1 - 0.28 * T.cover(fr))[..., None]
    t.hgt = t.hgt - 0.006 * T.cover(fr)
    if not right:
        up, outline, iris, lines = _glyph_eye(x, y, 0.5, Hh / 2 + 0.01, 1.6)
        for dd, k in ((outline, 0.45), (lines, 0.45), (iris, 0.5)):
            t.col = t.col * (1 - k * T.cover(dd))[..., None]
            t.hgt = t.hgt - 0.006 * T.cover(dd)
    else:
        disk, ring, rays = _glyph_sun(x, y, 0.5, Hh / 2, 1.2, sx)
        for dd in (ring, rays):
            t.col = t.col * (1 - 0.45 * T.cover(dd))[..., None]
            t.hgt = t.hgt - 0.006 * T.cover(dd)
        t.col = lerp(t.col, TERRA, T.cover(disk) * 0.6)
    # chipped, worn arrises
    du = np.minimum(U, 1 - U)
    dv = np.minimum(y, Hh - y)
    t.col = t.col * (1 - 0.18 * (1 - sstep(0.0, 0.03, np.minimum(du, dv))))[..., None]
    return t


# --- reed bundles (bridge) ----------------------------------------------------------

def reed_tex():
    """Around a reed bundle: u along it (1 m, periodic), v around it."""
    w, h = T.TEX, 96
    u = (np.arange(w) + 0.5) / w
    v = (np.arange(h) + 0.5) / h
    U, Vv = np.meshgrid(u, v)
    sh = U.shape
    stripes = np.sin(2 * math.pi * 16 * Vv + 0.8 * T.fnoise(sh, 0.05, 1401, aniso=(6, 0.3)))
    col = lerp(rgb(170, 136, 66), rgb(222, 190, 110), sstep(-0.8, 0.9, stripes))
    n = T.fnoise(sh, 0.04, 1402, aniso=(5, 0.5))
    col = col * (1 + 0.08 * n)[..., None]
    return Tex(col, 0.004 * stripes)


# ---------------------------------------------------------------------------

def _preview(out):
    import os
    from PIL import Image
    os.makedirs(out, exist_ok=True)

    def save(name, tex, light):
        lin = tex.col * light
        im = Image.fromarray(T.to_srgb8(lin[::-1]), 'RGB')
        im.resize((im.width * 2, im.height * 2), Image.NEAREST).save(os.path.join(out, name + '.png'))
    for v in range(3):
        save('sand_top_v%d' % v, sand_top(v), LIGHT_TOP)
        save('sandstone_top_v%d' % v, sandstone_top(v), LIGHT_TOP)
        save('brick_top_v%d' % v, brick_top(v), LIGHT_TOP)
        save('oasis_top_v%d' % v, oasis_top(v), LIGHT_TOP)
        save('water_top_v%d' % v, water_top(v), LIGHT_TOP)
        save('quicksand_top_v%d' % v, quicksand_top(v), LIGHT_TOP)
    save('sand_side', sand_side(), LIGHT_FRONT)
    save('sand_fill', sand_fill(), LIGHT_FRONT)
    save('sandstone_side', sandstone_side(), LIGHT_FRONT)
    save('brick_side', brick_side(), LIGHT_FRONT)
    save('brick_win_front', brick_win_face(False), LIGHT_FRONT)
    save('brick_win_right', brick_win_face(True), LIGHT_RIGHT)
    save('oasis_side', oasis_side(), LIGHT_FRONT)
    for v in range(3):
        save('cracked_top_v%d' % v, cracked_top(v), LIGHT_TOP)
        save('gold_top_v%d' % v, gold_top(v), LIGHT_TOP)
    save('cracked_side', cracked_side(), LIGHT_FRONT)
    save('gold_side', gold_side(), LIGHT_FRONT)
    save('dartwall_front', dartwall_face(False), LIGHT_FRONT)
    save('dartwall_right', dartwall_face(True), LIGHT_RIGHT)
    save('crate_front', crate_face(False), LIGHT_FRONT)
    save('crate_right', crate_face(True), LIGHT_RIGHT)
    save('reed', reed_tex(), LIGHT_TOP)


if __name__ == '__main__':
    _preview(sys.argv[1] if len(sys.argv) > 1 else '/tmp/df_tex')
