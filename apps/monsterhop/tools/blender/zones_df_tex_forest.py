"""Monster Hop - Werewolf Woods: the block, fill and surface textures (numpy).

Albedos in sRGB 0-255. The forest light is a cool teal-green moonlight: the
top of a block gets about (0.69, 0.89, 0.77) x albedo, the front face
(0.39, 0.53, 0.45), the right face (0.22, 0.34, 0.28). So albedos are kept
fairly light and warm, or the woods turn murky.

    python3 zones_df_tex_forest.py OUTDIR      # previews of every texture, lit
"""
import math
import sys

import numpy as np

import zones_df_tex as T
from zones_df_tex import rgb, lerp, sstep, Tex

LIGHT_TOP = np.array([0.69, 0.89, 0.77])
LIGHT_FRONT = np.array([0.39, 0.53, 0.45])
LIGHT_RIGHT = np.array([0.24, 0.36, 0.30])

GRASS = [rgb(40, 106, 72), rgb(58, 132, 88), rgb(28, 84, 60), rgb(96, 172, 140)]
EARTH = [rgb(132, 94, 68), rgb(108, 76, 56), rgb(150, 108, 78)]


# --- grass ---------------------------------------------------------------------

def _grass_colour(x, y, var, m):
    sh = x.shape
    n1 = T.lerp(T.fbm(sh, 0.11, 601), T.fbm(sh, 0.11, 610 + var), m)
    n2 = T.lerp(T.fnoise(sh, 0.018, 620), T.fnoise(sh, 0.018, 625 + var), m)
    blades = T.fnoise(sh, 0.0045, 630 + var, aniso=(0.55, 1.0))
    col = lerp(GRASS[0], GRASS[1], sstep(-1.1, 1.3, n1))
    col = lerp(col, GRASS[2], sstep(0.5, 1.8, -n2) * 0.55)          # darker clumps
    col = lerp(col, GRASS[3], sstep(1.1, 2.2, n2) * 0.45)            # moonlit tips
    col = col * (1 + 0.10 * blades)[..., None]
    hgt = 0.005 * n2 + 0.0025 * blades
    return col, hgt


def _flower(x, y, col, hgt, cx, cy, c, r=0.012, petals=5, rot=0.0, mid=None):
    for pk in range(petals):
        t = 2 * math.pi * pk / petals + rot
        dp = T.sd_circle(x, y, cx + r * math.cos(t), cy + r * math.sin(t), r * 0.8)
        col[:] = lerp(col, c, T.cover(dp))
    col[:] = lerp(col, mid if mid is not None else rgb(250, 210, 80), T.cover(T.sd_circle(x, y, cx, cy, r * 0.55)))
    hgt += 0.004 * T.cover(T.sd_circle(x, y, cx, cy, r * 1.8))
    # a soft dark contact shadow to the back-right
    return col


def grass_top(var):
    x, y = T.top_grid()
    d = T.edge_dist(x, y)
    m = sstep(0.07, 0.3, d)
    col, hgt = _grass_colour(x, y, var, m)
    rng = np.random.default_rng(640 + var)
    if var == 1:
        # a few moonlit flowers: white daisies and a pale-blue one
        spots = [(0.30, 0.66), (0.62, 0.30), (0.70, 0.72), (0.40, 0.36)]
        cols = [rgb(250, 250, 240), rgb(170, 200, 255), rgb(250, 250, 240), rgb(255, 214, 90)]
        for (cx, cy), c in zip(spots, cols):
            sh_ = T.sd_circle(x, y, cx + 0.012, cy + 0.012, 0.022)
            col *= (1 - 0.3 * np.clip(1 - np.maximum(sh_, 0) / 0.015, 0, 1))[..., None]
            _flower(x, y, col, hgt, cx, cy, c, rot=rng.uniform(0, 1))
    if var == 2:
        # a clover patch and a pale mushroom cap
        for k in range(7):
            cx, cy = 0.55 + rng.uniform(-0.12, 0.12), 0.45 + rng.uniform(-0.1, 0.1)
            _flower(x, y, col, hgt, cx, cy, rgb(80, 170, 70), r=0.011, petals=3, rot=rng.uniform(0, 3),
                    mid=rgb(70, 150, 64))
        T.pebbles(x, y, col, hgt, rng, 1, 0.028, 0.03, [rgb(236, 226, 206)], region=0.25, shade=0.6)
    col = T.darken_top_edges(col, x, y)
    return Tex(col, hgt)


def _earth(U, Z, seed=651):
    sh = U.shape
    v = (Z + T.FLOOR_M) / T.FLOOR_M
    n = T.fbm(sh, 0.05, seed)
    col = lerp(EARTH[1], EARTH[0], sstep(-1.2, 1.2, n))
    lay = np.sin(2 * math.pi * (2 * v + 0.05 * T.fbm(sh, 0.08, seed + 1)))
    col = col * (1 + 0.06 * lay)[..., None]
    grit = T.fnoise(sh, 0.005, seed + 2)
    col = col * (1 + 0.06 * grit)[..., None]
    hgt = 0.003 * n
    # embedded stones: periodic placement on a coarse lattice (wraps in u and z)
    rng = np.random.default_rng(seed + 3)
    Hm = T.FLOOR_M
    for k in range(9):
        cx, cz = rng.uniform(0, 1), rng.uniform(0, Hm)
        r = rng.uniform(0.012, 0.024)
        tone = rng.uniform(0.8, 1.05)
        for ox in (-1, 0, 1):
            for oz in (-Hm, 0, Hm):
                d = T.sd_ellipse(U, Z + Hm, cx + ox, cz + oz, r * 1.4, r, rng.uniform(-0.3, 0.3))
                m = T.cover(d)
                shade = 1 + 0.25 * np.clip((Z + Hm - cz - oz) / r, -1, 1)      # lit from above
                col = lerp(col, rgb(150, 140, 128) * tone * shade[..., None], m)
                hgt = hgt + 0.008 * m
    # a few pale root threads
    for k in range(3):
        cx = rng.uniform(0, 1)
        pts = [(cx + 0.03 * math.sin(i * 1.3 + k), Hm - i * 0.06) for i in range(6)]
        for ox in (-1, 0, 1):
            dd = T.sd_polyline(U, Z + Hm, [(px + ox, pz) for px, pz in pts], 0.004)
            col = lerp(col, rgb(196, 160, 120), T.cover(dd) * 0.8)
    return col, hgt


def earth_fill():
    U, Z = T.side_grid()
    col, hgt = _earth(U, Z)
    return Tex(col, hgt)


def lip(U, Z, depth=0.08, amp=0.025, seed=5):
    n = T.noise1(U.shape[1], 0.05, seed)[None, :]
    edge = depth + amp * n
    return edge


def grass_side():
    U, Z = T.side_grid()
    col, hgt = _earth(U, Z)
    edge = lip(U, Z)
    below = np.clip(1 - (-Z - edge) / 0.05, 0, 1) * (-Z > edge)
    col = col * (1 - 0.35 * below)[..., None]
    gcol, _ = _grass_colour(U, np.full_like(U, 0.03), 0, np.zeros_like(U))
    jag = 0.016 * np.abs(np.sin(2 * math.pi * 19 * U + 3 * T.noise1(U.shape[1], 0.02, 9)[None, :]))
    m2 = sstep(edge + jag + 0.006, edge + jag - 0.006, -Z)
    col = lerp(col, gcol * lerp(0.75, 1.0, sstep(edge + jag, 0.0, -Z))[..., None], m2)
    hgt = hgt * (1 - m2) + 0.004 * m2
    return Tex(col, hgt)


# --- path ------------------------------------------------------------------------

PATH_A = rgb(160, 126, 94)
PATH_B = rgb(136, 104, 78)


def path_top(var):
    x, y = T.top_grid()
    sh = x.shape
    d = T.edge_dist(x, y)
    m = sstep(0.06, 0.3, d)
    n = T.lerp(T.fbm(sh, 0.09, 701), T.fbm(sh, 0.09, 710 + var), m)
    col = lerp(PATH_B, PATH_A, sstep(-1.2, 1.2, n))
    # packed ruts along Y (the direction you walk), faint
    rut = np.exp(-((x - 0.33) / 0.06) ** 2) + np.exp(-((x - 0.67) / 0.06) ** 2)
    col = col * (1 - 0.06 * rut * m)[..., None]
    grit = T.fnoise(sh, 0.004, 720 + var)
    col = col * (1 + 0.07 * grit)[..., None]
    hgt = 0.002 * n - 0.002 * rut * m
    rng = np.random.default_rng(730 + var)
    T.pebbles(x, y, col, hgt, rng, 5 if var != 1 else 3, 0.012, 0.026,
              [rgb(190, 184, 174), rgb(160, 150, 140), rgb(210, 200, 186)], region=0.1, shade=0.6)
    if var == 1:
        # a paw print pressed in the mud: the werewolf went this way
        cx, cy = 0.52, 0.5
        pad = T.sd_ellipse(x, y, cx, cy - 0.02, 0.05, 0.04)
        m_ = T.cover(pad)
        for k, (ox, oy) in enumerate(((-0.055, 0.045), (-0.02, 0.075), (0.02, 0.075), (0.055, 0.045))):
            m_ = np.maximum(m_, T.cover(T.sd_ellipse(x, y, cx + ox, cy + oy, 0.018, 0.022)))
        col = col * (1 - 0.28 * m_)[..., None]
        hgt = hgt - 0.006 * m_
    col = T.darken_top_edges(col, x, y)
    return Tex(col, hgt)


def path_side():
    U, Z = T.side_grid()
    col, hgt = _earth(U, Z)
    edge = lip(U, Z, 0.05, 0.015, 11)
    m = sstep(edge + 0.006, edge - 0.006, -Z)
    col = lerp(col, lerp(PATH_B, PATH_A, 0.3) * 0.9, m)
    below = np.clip(1 - (-Z - edge) / 0.04, 0, 1) * (-Z > edge)
    col = col * (1 - 0.25 * below)[..., None]
    return Tex(col, hgt)


# --- rock ----------------------------------------------------------------------

ROCK_A = rgb(156, 160, 162)
ROCK_B = rgb(124, 128, 134)
LICHEN = rgb(140, 170, 104)


def _cracks(x, y, rng, n, lo, hi, width=0.004):
    d = np.full(x.shape, 1e9)
    for k in range(n):
        px, py = rng.uniform(lo, hi, 2)
        ang = rng.uniform(0, math.pi)
        pts = [(px, py)]
        for i in range(4):
            ang += rng.uniform(-0.7, 0.7)
            px += 0.07 * math.cos(ang)
            py += 0.07 * math.sin(ang)
            pts.append((min(max(px, lo), hi), min(max(py, lo), hi)))
        d = np.minimum(d, T.sd_polyline(x, y, pts, width))
    return d


def rock_top(var):
    x, y = T.top_grid()
    sh = x.shape
    d = T.edge_dist(x, y)
    m = sstep(0.06, 0.28, d)
    n = T.lerp(T.fbm(sh, 0.10, 801), T.fbm(sh, 0.10, 810 + var), m)
    col = lerp(ROCK_B, ROCK_A, sstep(-1.3, 1.3, n))
    f = T.lerp(T.fbm(sh, 0.035, 820), T.fbm(sh, 0.035, 825 + var), m)
    col = col * (1 + 0.07 * f)[..., None]
    lich = T.lerp(T.fbm(sh, 0.05, 830), T.fbm(sh, 0.05, 835 + var), m)
    col = lerp(col, LICHEN, sstep(1.0, 1.6, lich) * 0.7)
    rng = np.random.default_rng(840 + var)
    dc = _cracks(x, y, rng, 2 + var % 2, 0.12, 0.88)
    c = T.cover(dc)
    col = col * (1 - 0.45 * c)[..., None]
    hgt = 0.004 * n + 0.002 * f - 0.006 * c
    col = T.darken_top_edges(col, x, y, 0.09)
    return Tex(col, hgt)


def rock_side():
    """A cliff face: layered rock with vertical joints, periodic."""
    U, Z = T.side_grid()
    sh = U.shape
    v = (Z + T.FLOOR_M) / T.FLOOR_M
    n = T.fbm(sh, 0.06, 851)
    col = lerp(rgb(124, 128, 134), rgb(148, 152, 154), sstep(-1.2, 1.2, n))
    t = 2 * (v + 0.04 * T.fbm(sh, 0.1, 852))
    f = np.mod(t, 1.0)
    ledge = np.exp(-((f - 0.04) / 0.05) ** 2) + np.exp(-((f - 1.04) / 0.05) ** 2)
    col = col * (1 - 0.30 * ledge)[..., None]
    col = col * (1 + 0.10 * sstep(0.1, 0.35, f) * (1 - sstep(0.35, 0.9, f)))[..., None]   # lit upper lip of each layer
    joints = np.full(sh, 1e9)
    rng = np.random.default_rng(853)
    for k in range(4):
        u0 = rng.uniform(0, 1)
        for ox in (-1, 0, 1):
            pts = [(u0 + ox + 0.02 * math.sin(i * 2.1 + k), -T.FLOOR_M + i * T.FLOOR_M / 5) for i in range(6)]
            joints = np.minimum(joints, T.sd_polyline(U, Z, pts, 0.004))
    col = col * (1 - 0.35 * T.cover(joints))[..., None]
    lich = T.fbm(sh, 0.05, 854)
    col = lerp(col, LICHEN * 0.9, sstep(1.2, 1.8, lich) * 0.6)
    hgt = 0.004 * n - 0.008 * ledge - 0.004 * T.cover(joints)
    return Tex(col, hgt)


# --- moss slabs ----------------------------------------------------------------------

MOSS = [rgb(70, 146, 62), rgb(100, 176, 76), rgb(50, 116, 54)]
SLAB = [rgb(142, 148, 146), rgb(120, 126, 126)]


def moss_top(var):
    """Old stone slabs, moss growing in the joints and over the corners."""
    x, y = T.top_grid()
    sh = x.shape
    d = T.edge_dist(x, y)
    m = sstep(0.05, 0.25, d)
    # slab layout (joints inside the cell), the cell edge is always a joint
    layouts = [
        [(0, 0, 0.55, 0.5), (0.55, 0, 1, 0.5), (0, 0.5, 0.4, 1), (0.4, 0.5, 1, 1)],
        [(0, 0, 1, 0.45), (0, 0.45, 0.5, 1), (0.5, 0.45, 1, 1)],
        [(0, 0, 0.45, 0.6), (0.45, 0, 1, 0.35), (0.45, 0.35, 1, 1), (0, 0.6, 0.45, 1)],
    ]
    rng = np.random.default_rng(900 + var)
    col = np.zeros(sh + (3,))
    dj = np.full(sh, 1e9)
    n = T.lerp(T.fbm(sh, 0.08, 901), T.fbm(sh, 0.08, 905 + var), m)
    for (x0, y0, x1, y1) in layouts[var % 3]:
        inside = (x >= x0) & (x < x1) & (y >= y0) & (y < y1)
        tone = rng.uniform(0.92, 1.06)
        c = lerp(SLAB[1], SLAB[0], sstep(-1.2, 1.2, n)) * tone
        col = np.where(inside[..., None], c, col)
        dd = np.minimum(np.minimum(x - x0, x1 - x), np.minimum(y - y0, y1 - y))
        dj = np.where(inside, dd, dj)
    gap = 1 - sstep(0.008, 0.03, dj)
    # moss: in the joints, spreading on the slabs by noise (never on the rim
    # pattern differently: the rim joint is the same moss band in all variants)
    mn = T.lerp(T.fbm(sh, 0.05, 910), T.fbm(sh, 0.05, 915 + var), m)
    moss = np.clip(gap * 1.2 + sstep(0.4, 1.2, mn - dj * 12), 0, 1)
    mcol = lerp(MOSS[2], MOSS[1], sstep(-1, 1.5, T.fnoise(sh, 0.012, 920 + var)))
    col = col * (1 - 0.35 * gap)[..., None]
    col = lerp(col, mcol, moss * 0.95)
    tuft = T.fnoise(sh, 0.004, 930 + var)
    col = col * (1 + 0.08 * tuft * moss)[..., None]
    hgt = 0.002 * n - 0.006 * gap + 0.006 * moss
    col = T.darken_top_edges(col, x, y, 0.06)
    return Tex(col, hgt)


def moss_side():
    U, Z = T.side_grid()
    sh = U.shape
    v = (Z + T.FLOOR_M) / T.FLOOR_M
    n = T.fbm(sh, 0.06, 941)
    col = lerp(SLAB[1], SLAB[0], sstep(-1.2, 1.2, n)) * 0.95
    du = np.minimum(np.mod(U, 0.5), 0.5 - np.mod(U, 0.5))
    dv = np.minimum(v, 1 - v) * T.FLOOR_M
    g = 1 - sstep(0.004, 0.02, np.minimum(du, dv))
    col = col * (1 - 0.35 * g)[..., None]
    # moss drips over the top edge
    edge = 0.06 + 0.05 * np.clip(T.noise1(U.shape[1], 0.03, 942)[None, :], -1, 2)
    mm = sstep(edge + 0.008, edge - 0.008, -Z)
    col = lerp(col, lerp(MOSS[2], MOSS[0], 0.5) * 0.95, mm)
    hgt = 0.003 * n - 0.006 * g + 0.004 * mm
    return Tex(col, hgt)


# --- river -------------------------------------------------------------------------

RIVER_D = rgb(26, 92, 110)
RIVER_M = rgb(40, 124, 138)
RIVER_L = rgb(150, 220, 214)


def river_top(var):
    """Dark teal water; a lighter flow streak along X through the cell's
    middle, periodic in x so a river of any length flows on."""
    x, y = T.top_grid()
    sh = x.shape
    dx = np.minimum(x, 1 - x)
    m = sstep(0.05, 0.3, dx)
    n = T.lerp(T.fbm(sh, 0.12, 1001, aniso=(2.5, 0.8)), T.fbm(sh, 0.12, 1010 + var, aniso=(2.5, 0.8)), m)
    col = lerp(RIVER_D, RIVER_M, sstep(-1.3, 1.3, n))
    # flow lines: stretched along X
    fl = T.lerp(T.fnoise(sh, 0.02, 1020, aniso=(4.0, 0.6)), T.fnoise(sh, 0.02, 1025 + var, aniso=(4.0, 0.6)), m)
    col = lerp(col, RIVER_M * 1.15, sstep(0.8, 1.8, fl) * 0.5)
    # the streak: a wobbly lighter band near the middle, fading in and out
    # along X (a whole number of waves per cell), plus thin long flow lines
    wob = 0.035 * np.sin(2 * math.pi * x + 0.7) + 0.02 * T.lerp(T.fbm(sh, 0.12, 1030), T.fbm(sh, 0.12, 1035 + var), m)
    band = np.exp(-((y - 0.5 - wob) / 0.045) ** 2)
    ph = 2 * math.pi * 2 * x + 1.2 * T.lerp(T.fbm(sh, 0.15, 1040), T.fbm(sh, 0.15, 1045 + var), m)
    dash = 0.35 + 0.65 * sstep(-0.3, 0.8, np.sin(ph))
    col = lerp(col, RIVER_M * 1.35, band * dash * 0.8)
    core = np.exp(-((y - 0.5 - wob) / 0.014) ** 2) * sstep(0.2, 0.9, np.sin(ph))
    col = lerp(col, RIVER_L, core * 0.8)
    lines = T.lerp(T.fnoise(sh, 0.012, 1050, aniso=(7.0, 0.35)), T.fnoise(sh, 0.012, 1055 + var, aniso=(7.0, 0.35)), m)
    edge_fade = sstep(0.04, 0.15, np.minimum(y, 1 - y))
    col = lerp(col, RIVER_L * 0.8, sstep(1.5, 2.3, lines) * 0.45 * edge_fade)
    hgt = 0.002 * n + 0.002 * band
    return Tex(col, hgt)


# --- wooden deck --------------------------------------------------------------------

WOOD = [rgb(176, 124, 80), rgb(150, 102, 64), rgb(196, 146, 98)]


def plank_top(var):
    """Four boards along X per cell; board ends at the cell edge and, for
    every other board, at x = 0.5 (staggered). Nails at the ends."""
    x, y = T.top_grid()
    sh = x.shape
    board = np.floor(y * 4).astype(int)
    fy = np.mod(y * 4, 1.0)
    rng = np.random.default_rng(1500 + var)
    grain = T.fnoise(sh, 0.01, 1501 + var, aniso=(8.0, 0.35))
    col = lerp(WOOD[1], WOOD[0], sstep(-1.2, 1.2, grain))
    tones = rng.uniform(0.88, 1.08, 8)
    half = (x > 0.5).astype(int)
    stag = (board % 2 == 1)
    key = np.where(stag, board * 2 + half, board * 2)
    col = col * tones[key][..., None]
    gap = 1 - sstep(0.0, 0.05, np.minimum(fy, 1 - fy))
    endx = np.where(stag, np.minimum(np.minimum(x, 1 - x), np.abs(x - 0.5)), np.minimum(x, 1 - x))
    gapx = 1 - sstep(0.0, 0.012, endx)
    g = np.maximum(gap, gapx)
    col = lerp(col, rgb(40, 28, 20), g * 0.85)
    nails = np.zeros(sh)
    for b in range(4):
        cy = (b + 0.5) / 4
        xs = [0.035, 0.965] + ([0.465, 0.535] if b % 2 == 1 else [])
        for cx in xs:
            nails = np.maximum(nails, T.cover(T.sd_circle(x, y, cx, cy, 0.009)))
    col = lerp(col, rgb(60, 62, 66), nails)
    if var == 1:
        k = T.cover(T.sd_ellipse(x, y, 0.3, 0.62, 0.035, 0.02))
        col = lerp(col, rgb(90, 60, 40), k)
    if var == 2:
        dd = T.sd_ellipse(x, y, 0.6, 0.35, 0.14, 0.08)
        col = lerp(col, lerp(col, rgb(70, 140, 60), 0.6), sstep(0.0, -0.05, dd) * 0.8)
    hgt = 0.002 * grain - 0.008 * g + 0.002 * nails
    col = T.darken_top_edges(col, x, y, 0.05)
    return Tex(col, hgt)


def plank_side():
    """A fascia board under the deck, earth below (it sits on the earth fill)."""
    U, Z = T.side_grid()
    col, hgt = _earth(U, Z)
    sh = U.shape
    grain = T.fnoise(sh, 0.01, 1510, aniso=(8.0, 0.35))
    wood = lerp(WOOD[1], WOOD[0], sstep(-1.2, 1.2, grain)) * 0.95
    fascia = sstep(0.125, 0.12, -Z)
    col = lerp(col, wood, fascia)
    line = T.cover(np.abs(-Z - 0.125) - 0.006)
    col = col * (1 - 0.5 * line)[..., None]
    shadow = np.clip(1 - (-Z - 0.125) / 0.05, 0, 1) * (-Z > 0.125)
    col = col * (1 - 0.35 * shadow)[..., None]
    for cx in (0.1, 0.9):
        col = lerp(col, rgb(60, 62, 66), T.cover(T.sd_circle(U, Z, cx, -0.06, 0.009)))
    hgt = hgt * (1 - fascia) + 0.006 * fascia
    return Tex(col, hgt)


# --- mud -------------------------------------------------------------------------------

MUD = [rgb(96, 70, 52), rgb(76, 54, 40), rgb(120, 92, 68)]


def mud_top(var):
    """Wet dark mud: glossy puddles (low roughness), a few bubbles, tracks."""
    x, y = T.top_grid()
    sh = x.shape
    d = T.edge_dist(x, y)
    m = sstep(0.07, 0.3, d)
    n = T.lerp(T.fbm(sh, 0.08, 1601), T.fbm(sh, 0.08, 1605 + var), m)
    col = lerp(MUD[1], MUD[0], sstep(-1.2, 1.2, n))
    lumps = T.fnoise(sh, 0.015, 1610 + var)
    col = col * (1 + 0.10 * lumps)[..., None]
    pud = T.lerp(T.fbm(sh, 0.09, 1620), T.fbm(sh, 0.09, 1625 + var), m)
    wet = sstep(0.55, 0.9, pud) * m
    col = lerp(col, rgb(56, 52, 50), wet * 0.7)
    rng = np.random.default_rng(1630 + var)
    for k in range(3):
        cx, cy = rng.uniform(0.2, 0.8, 2)
        db = T.sd_circle(x, y, cx, cy, rng.uniform(0.012, 0.02))
        col = lerp(col, rgb(150, 130, 110), T.cover(np.abs(db) - 0.003) * 0.8)
    rough = 0.85 - 0.7 * wet
    hgt = 0.004 * lumps * (1 - wet) - 0.004 * wet
    col = T.darken_top_edges(col, x, y, 0.08)
    return Tex(col, hgt, rough=rough)


def mud_side():
    U, Z = T.side_grid()
    col, hgt = _earth(U, Z)
    edge = lip(U, Z, 0.07, 0.03, 13)
    m = sstep(edge + 0.008, edge - 0.008, -Z)
    col = lerp(col, MUD[1] * 0.95, m)
    drip = sstep(0.8, 1.6, T.noise1(U.shape[1], 0.012, 14)[None, :]) * sstep(edge + 0.07, edge, -Z)
    col = lerp(col, MUD[1], drip * (1 - m))
    return Tex(col, hgt)


def _preview(out):
    import os
    from PIL import Image
    os.makedirs(out, exist_ok=True)

    def save(name, tex, light):
        lin = tex.col * light
        im = Image.fromarray(T.to_srgb8(lin[::-1]))
        im.resize((im.width * 2, im.height * 2), Image.NEAREST).save(os.path.join(out, name + '.png'))
    for v in range(3):
        save('grass_top_v%d' % v, grass_top(v), LIGHT_TOP)
        save('path_top_v%d' % v, path_top(v), LIGHT_TOP)
        save('rock_top_v%d' % v, rock_top(v), LIGHT_TOP)
        save('moss_top_v%d' % v, moss_top(v), LIGHT_TOP)
        save('river_top_v%d' % v, river_top(v), LIGHT_TOP)
    save('grass_side', grass_side(), LIGHT_FRONT)
    save('earth_fill', earth_fill(), LIGHT_FRONT)
    save('path_side', path_side(), LIGHT_FRONT)
    save('rock_side', rock_side(), LIGHT_FRONT)
    save('moss_side', moss_side(), LIGHT_FRONT)
    for v in range(3):
        save('plank_top_v%d' % v, plank_top(v), LIGHT_TOP)
        save('mud_top_v%d' % v, mud_top(v), LIGHT_TOP)
    save('plank_side', plank_side(), LIGHT_FRONT)
    save('mud_side', mud_side(), LIGHT_FRONT)


if __name__ == '__main__':
    _preview(sys.argv[1] if len(sys.argv) > 1 else '/tmp/df_tex')
