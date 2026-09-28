"""Monster Hop - Abyss Bay (zone prefix `bay`): block, fill and surface
textures in plain numpy (style sample).

Same conventions as zones_df_tex (it is imported for the grids, the periodic
noise and the signed distances): TEX px per metre, row 0 at the bottom, tops
(x, y) and sides (u, z) exactly periodic so blocks join and fills stack.

The bay light (zones_bay_tiles.LIGHT) is a cold blue moonlight: a block top
gets roughly (0.56, 0.74, 0.92) x albedo, the front (0.33, 0.45, 0.58), the
right face (0.19, 0.27, 0.36). Albedos are kept light-ish and a little warm
so the night reads blue without turning to mud; the accents are emissive
(bioluminescent algae and plankton: cyan and magenta).

    python3 zones_bay_tex.py OUTDIR      # previews of every texture, lit
"""
import math
import sys

import numpy as np

import zones_df_tex as T
from zones_df_tex import rgb, lerp, sstep, Tex

LIGHT_TOP = np.array([0.56, 0.74, 0.92])
LIGHT_FRONT = np.array([0.33, 0.45, 0.58])

# bioluminescence (linear, used as emission colours)
BIO_CYAN = rgb(90, 255, 230)
BIO_MAGENTA = rgb(255, 90, 220)


def _glow_dots(x, y, rng, n, rmin, rmax, cols, region=0.12, avoid=None):
    """Tiny emissive specks (algae, plankton): an emission map and the albedo
    tint under it."""
    em = np.zeros(x.shape + (3,))
    for _ in range(n):
        r = rng.uniform(rmin, rmax)
        cx, cy = rng.uniform(region, 1 - region, 2)
        if avoid is not None and avoid(cx, cy):
            continue
        d = T.sd_circle(x, y, cx, cy, r)
        core = T.cover(d)
        halo = np.clip(1 - np.maximum(d, 0) / (r * 2.2), 0, 1) ** 2 * 0.35
        c = cols[rng.integers(len(cols))]
        em = em + (core + halo)[..., None] * c[None, None, :]
    return em


# --- wet sand --------------------------------------------------------------------

SAND = [rgb(182, 164, 130), rgb(162, 144, 114), rgb(200, 184, 150)]
WET = rgb(128, 116, 98)


def _sand(x, y, var, m, seed=100):
    sh = x.shape
    n = T.lerp(T.fbm(sh, 0.10, seed + 1), T.fbm(sh, 0.10, seed + 10 + var), m)
    col = lerp(SAND[1], SAND[0], sstep(-1.2, 1.2, n))
    # low ripple marks left by the tide, along X, a whole number of waves
    wob = 0.03 * T.lerp(T.fbm(sh, 0.12, seed + 20), T.fbm(sh, 0.12, seed + 25 + var), m)
    rip = np.sin(2 * math.pi * (7 * y + wob * 7 + 0.4 * np.sin(2 * math.pi * x)))
    col = col * (1 + 0.07 * rip)[..., None]
    # wet darker patches, glossy (low roughness)
    wn = T.lerp(T.fbm(sh, 0.09, seed + 30), T.fbm(sh, 0.09, seed + 35 + var), m)
    wet = sstep(0.3, 1.1, wn)
    col = lerp(col, WET, wet * 0.32)
    grit = T.fnoise(sh, 0.004, seed + 40 + var)
    col = col * (1 + 0.06 * grit)[..., None]
    hgt = 0.0025 * rip + 0.002 * n
    rough = 0.9 - 0.55 * wet
    return col, hgt, rough, wet


def _shell(x, y, col, hgt, cx, cy, r, ang, c):
    d = T.sd_ellipse(x, y, cx, cy, r, r * 0.8, ang)
    m = T.cover(d)
    # ribs
    th = np.arctan2(y - cy, x - cx)
    ribs = 0.5 + 0.5 * np.cos(9 * th)
    sh = np.clip(1 - np.maximum(T.sd_ellipse(x, y, cx + r * 0.3, cy + r * 0.3, r, r * 0.8, ang), 0) / (r * 0.6), 0, 1)
    col *= (1 - 0.35 * sh * (1 - m))[..., None]
    col[:] = lerp(col, c[None, None, :] * (0.85 + 0.15 * ribs)[..., None], m)
    hgt += 0.006 * m


def _starfish(x, y, col, hgt, cx, cy, r, rot, c):
    th = np.arctan2(y - cy, x - cx) - rot
    rr = np.hypot(x - cx, y - cy)
    arm = r * (0.42 + 0.58 * np.abs(np.cos(2.5 * th)) ** 2.2)
    d = rr - arm
    sh = rr - arm - 0.012
    col *= (1 - 0.30 * T.cover(sh - 0.01, 0.02) * (d > 0))[..., None]
    m = T.cover(d)
    dots = T.cover(np.abs(np.sin(rr * 160)) * 0.01 - 0.006) * 0.0
    col[:] = lerp(col, c[None, None, :] * (1 - 0.25 * sstep(0, r, rr))[..., None], m)
    del dots
    hgt += 0.008 * m * (1 - rr / (r + 1e-6)).clip(0, 1)


def sand_top(var):
    x, y = T.top_grid()
    d = T.edge_dist(x, y)
    m = sstep(0.07, 0.3, d)
    col, hgt, rough, _ = _sand(x, y, var, m)
    rng = np.random.default_rng(160 + var)
    emit = None
    if var == 0:
        _shell(x, y, col, hgt, 0.36, 0.62, 0.035, 0.4, rgb(236, 214, 200))
        T.pebbles(x, y, col, hgt, rng, 3, 0.012, 0.02, [rgb(120, 124, 130), rgb(96, 100, 110)], region=0.15)
    if var == 1:
        _starfish(x, y, col, hgt, 0.58, 0.44, 0.07, 0.3, rgb(206, 112, 80))
        _shell(x, y, col, hgt, 0.28, 0.72, 0.028, 1.4, rgb(214, 190, 230))
    if var == 2:
        # a strand of washed-up kelp and a few glowing plankton specks
        pts = [(0.22, 0.30), (0.34, 0.40), (0.46, 0.37), (0.58, 0.50), (0.72, 0.52)]
        dk = T.sd_polyline(x, y, pts, 0.013)
        sh_ = T.sd_polyline(x, y, [(px + 0.012, py + 0.012) for px, py in pts], 0.016)
        col *= (1 - 0.3 * T.cover(sh_) * (dk > 0))[..., None]
        col = lerp(col, rgb(52, 96, 70), T.cover(dk))
        for k, (px, py) in enumerate(pts[1:-1]):
            dl = T.sd_ellipse(x, y, px + 0.02, py + 0.035 * (-1) ** k, 0.035, 0.014, 0.8 * (-1) ** k)
            col = lerp(col, rgb(70, 120, 76), T.cover(dl))
        hgt = hgt + 0.004 * T.cover(dk)
        emit = _glow_dots(x, y, rng, 6, 0.004, 0.007, [BIO_CYAN], region=0.14)
        col = col + emit * 0.25
    col = T.darken_top_edges(col, x, y)
    return Tex(col, hgt, rough=rough, emit=emit)


def _packed_sand(U, Z, seed=190):
    sh = U.shape
    v = (Z + T.FLOOR_M) / T.FLOOR_M
    n = T.fbm(sh, 0.05, seed)
    col = lerp(SAND[1] * 0.92, SAND[0] * 0.92, sstep(-1.2, 1.2, n))
    lay = np.sin(2 * math.pi * (3 * v + 0.05 * T.fbm(sh, 0.08, seed + 1)))
    col = col * (1 + 0.07 * lay)[..., None]
    grit = T.fnoise(sh, 0.005, seed + 2)
    col = col * (1 + 0.06 * grit)[..., None]
    hgt = 0.003 * n + 0.002 * lay
    # a few embedded shells, periodic
    rng = np.random.default_rng(seed + 3)
    Hm = T.FLOOR_M
    for k in range(6):
        cx, cz = rng.uniform(0, 1), rng.uniform(0, Hm)
        r = rng.uniform(0.010, 0.018)
        for ox in (-1, 0, 1):
            for oz in (-Hm, 0, Hm):
                d = T.sd_ellipse(U, Z + Hm, cx + ox, cz + oz, r * 1.4, r, 0.2)
                col = lerp(col, rgb(222, 206, 196), T.cover(d))
    return col, hgt


def sand_fill():
    U, Z = T.side_grid()
    col, hgt = _packed_sand(U, Z)
    return Tex(col, hgt)


def sand_side():
    U, Z = T.side_grid()
    col, hgt = _packed_sand(U, Z)
    # a darker wet band just under the lip, the lip itself lighter
    edge = 0.04 + 0.015 * T.noise1(U.shape[1], 0.04, 7)[None, :]
    m = sstep(edge + 0.006, edge - 0.006, -Z)
    col = lerp(col, SAND[0], m)
    below = np.clip(1 - (-Z - edge) / 0.06, 0, 1) * (-Z > edge)
    col = col * (1 - 0.28 * below)[..., None]
    return Tex(col, hgt)


# --- rock with barnacles ------------------------------------------------------------

ROCK = [rgb(112, 118, 128), rgb(86, 92, 104), rgb(132, 138, 146)]
BARN = rgb(214, 208, 190)
BARN_D = rgb(70, 66, 64)
ALGA = rgb(44, 110, 92)


def _barnacles(x, y, col, hgt, rng, n, rmin, rmax, region=0.1, cx0=None, spread=0.2):
    """Little volcano cones: pale ring, dark hole, a contact shadow back-right."""
    placed = []
    for _ in range(n * 30):
        if len(placed) >= n:
            break
        r = rng.uniform(rmin, rmax)
        if cx0 is not None:
            cx, cy = cx0[0] + rng.normal(0, spread), cx0[1] + rng.normal(0, spread)
        else:
            cx, cy = rng.uniform(region + r, 1 - region - r, 2)
        if not (region + r < cx < 1 - region - r and region + r < cy < 1 - region - r):
            continue
        if any(math.hypot(cx - px, cy - py) < r + pr + 0.004 for px, py, pr in placed):
            continue
        placed.append((cx, cy, r))
    for cx, cy, r in placed:
        dsh = T.sd_circle(x, y, cx + r * 0.4, cy + r * 0.4, r * 1.05)
        col *= (1 - 0.4 * np.clip(1 - np.maximum(dsh, 0) / (r * 0.6), 0, 1))[..., None]
        d = T.sd_circle(x, y, cx, cy, r)
        m = T.cover(d)
        rr = np.hypot(x - cx, y - cy) / r
        lx = (x - cx) / r
        ly = (y - cy) / r
        lit = 1.0 + 0.25 * np.clip(-(lx + ly) * 0.7, -1, 1)
        c = BARN * rng.uniform(0.85, 1.05) * lit[..., None]
        col[:] = lerp(col, c, m)
        hole = T.cover(T.sd_circle(x, y, cx, cy, r * 0.38))
        col[:] = lerp(col, BARN_D, hole)
        hgt += m * (1 - rr).clip(0, 1) * r * 0.9 - hole * r * 0.3
    return placed


def rock_top(var):
    x, y = T.top_grid()
    sh = x.shape
    d = T.edge_dist(x, y)
    m = sstep(0.06, 0.28, d)
    n = T.lerp(T.fbm(sh, 0.10, 301), T.fbm(sh, 0.10, 310 + var), m)
    col = lerp(ROCK[1], ROCK[0], sstep(-1.3, 1.3, n))
    f = T.lerp(T.fbm(sh, 0.03, 320), T.fbm(sh, 0.03, 325 + var), m)
    col = col * (1 + 0.08 * f)[..., None]
    # dark green algae slick in the hollows
    al = T.lerp(T.fbm(sh, 0.06, 330), T.fbm(sh, 0.06, 335 + var), m)
    ag = sstep(0.6, 1.4, al)
    col = lerp(col, ALGA, ag * 0.75)
    hgt = 0.005 * n + 0.002 * f
    rng = np.random.default_rng(340 + var)
    centres = [(0.34, 0.36), (0.64, 0.62), (0.58, 0.32)]
    _barnacles(x, y, col, hgt, rng, 14 + 4 * var, 0.013, 0.026, region=0.08, cx0=centres[var], spread=0.11)
    emit = None
    if var == 1:
        # glowing algae in the green slick
        emit = _glow_dots(x, y, rng, 12, 0.004, 0.008, [BIO_CYAN, BIO_CYAN, BIO_MAGENTA], region=0.12)
        emit = emit * sstep(0.2, 0.8, al)[..., None]
    col = T.darken_top_edges(col, x, y, 0.09)
    rough = 0.8 - 0.4 * ag
    return Tex(col, hgt, rough=rough, emit=emit)


def rock_side():
    """A wet rock face: rough layers, a band of barnacles near the top and a
    green-black algae fringe at the bottom (the high-tide mark)."""
    U, Z = T.side_grid()
    sh = U.shape
    v = (Z + T.FLOOR_M) / T.FLOOR_M
    n = T.fbm(sh, 0.06, 351)
    col = lerp(ROCK[1], ROCK[0], sstep(-1.2, 1.2, n))
    t = 2 * (v + 0.05 * T.fbm(sh, 0.1, 352))
    fr = np.mod(t, 1.0)
    ledge = np.exp(-((fr - 0.04) / 0.05) ** 2) + np.exp(-((fr - 1.04) / 0.05) ** 2)
    col = col * (1 - 0.28 * ledge)[..., None]
    al = T.fbm(sh, 0.05, 353)
    col = lerp(col, ALGA * 0.8, sstep(0.9, 1.5, al) * 0.7)
    hgt = 0.004 * n - 0.008 * ledge
    # barnacles in the upper band (periodic in u)
    rng = np.random.default_rng(354)
    Hm = T.FLOOR_M
    for k in range(26):
        cu, cz = rng.uniform(0, 1), rng.uniform(Hm * 0.55, Hm * 0.92)
        r = rng.uniform(0.010, 0.02)
        for ox in (-1, 0, 1):
            d = T.sd_circle(U, Z + Hm, cu + ox, cz, r)
            mm = T.cover(d)
            col = lerp(col, BARN * (0.8 + 0.3 * np.clip((Z + Hm - cz) / r, -1, 1))[..., None], mm)
            col = lerp(col, BARN_D, T.cover(T.sd_circle(U, Z + Hm, cu + ox, cz + r * 0.2, r * 0.4)))
            hgt = hgt + 0.006 * mm
    return Tex(col, hgt)


# --- pier planks ------------------------------------------------------------------

WOOD = [rgb(168, 140, 110), rgb(140, 114, 90), rgb(188, 162, 128)]


def pier_top(var, along='x'):
    """A boardwalk you walk along `along`: five boards across the way,
    weathered silver-brown, dark gaps with the water glinting below, iron
    nails over the two stringers."""
    x, y = T.top_grid()
    sh = x.shape
    if along == 'y':
        x, y = y, x               # boards run along X, the way runs along Y
    # u runs with the way, w across it; boards are narrow in u
    u, w = x, y
    nb = 5
    board = np.floor(u * nb).astype(int)
    fu = np.mod(u * nb, 1.0)
    rng = np.random.default_rng(400 + var)
    grain = T.fnoise(sh, 0.01, 401 + var, aniso=(0.35, 8.0) if along == 'x' else (8.0, 0.35))
    col = lerp(WOOD[1], WOOD[0], sstep(-1.2, 1.2, grain))
    tones = rng.uniform(0.86, 1.08, nb)
    col = col * tones[board][..., None]
    # silvery weathering towards the board ends
    wn = T.fbm(sh, 0.05, 405 + var)
    col = lerp(col, rgb(170, 170, 164), sstep(0.8, 1.8, wn) * 0.2)
    gap = 1 - sstep(0.0, 0.07, np.minimum(fu, 1 - fu))
    col = lerp(col, rgb(18, 30, 40), gap * 0.9)
    nails = np.zeros(sh)
    for b in range(nb):
        cu = (b + 0.5) / nb
        for cw in (0.16, 0.84):
            nails = np.maximum(nails, T.cover(np.hypot(u - cu, w - cw) - 0.011))
    col = lerp(col, rgb(62, 58, 58), nails)
    rust = np.zeros(sh)
    for b in range(nb):
        cu = (b + 0.5) / nb
        for cw in (0.16, 0.84):
            rust = np.maximum(rust, np.clip(1 - np.hypot(u - cu, (w - cw) * 0.6) / 0.03, 0, 1) * (w > cw))
    col = lerp(col, rgb(120, 70, 50), rust * 0.35 * (1 - nails))
    if var == 1:
        # a coil of rope lying on the boards
        d0 = np.hypot(u - 0.56, w - 0.46)
        ring = np.abs(np.mod(d0, 0.022) - 0.011) < 0.007
        mm = T.cover(d0 - 0.075) * ring
        col = lerp(col, rgb(206, 180, 126), mm)
    if var == 2:
        # a crack and a patch of green slime
        dd = T.sd_ellipse(u, w, 0.3, 0.62, 0.10, 0.06)
        col = lerp(col, lerp(col, rgb(60, 118, 90), 0.6), sstep(0.0, -0.04, dd) * 0.8)
    hgt = 0.002 * grain - 0.01 * gap + 0.002 * nails
    col = T.darken_top_edges(col, x, y, 0.05)
    return Tex(col, hgt)


def pier_side():
    """A fascia board under the deck, dark posts and cross braces below with
    black gaps (an opaque block, but it reads as a pier on stilts)."""
    U, Z = T.side_grid()
    sh = U.shape
    col = T.fill(sh, rgb(20, 30, 40))
    grain = T.fnoise(sh, 0.01, 410, aniso=(8.0, 0.35))
    wood = lerp(WOOD[1], WOOD[0], sstep(-1.2, 1.2, grain)) * 0.9
    fascia = sstep(0.115, 0.11, -Z)
    col = lerp(col, wood, fascia)
    line = T.cover(np.abs(-Z - 0.112) - 0.006)
    col = col * (1 - 0.5 * line)[..., None]
    # posts at u = 0.12 and 0.88 (every cell), a diagonal brace between
    uu = np.mod(U, 1.0)
    post = (np.abs(uu - 0.12) < 0.05) | (np.abs(uu - 0.88) < 0.05)
    pw = lerp(WOOD[1], WOOD[2], sstep(-1, 1, T.fnoise(sh, 0.02, 411))) * 0.75
    below = -Z > 0.11
    col = np.where((post & below)[..., None], pw, col)
    zz = (-Z - 0.11) / (T.FLOOR_M - 0.11)
    dbr = np.abs((uu - 0.17) / 0.66 - zz) * 0.66
    brace = (dbr < 0.028) & below & (uu > 0.17) & (uu < 0.83)
    col = np.where(brace[..., None], pw * 0.85, col)
    shadow = np.clip(1 - (-Z - 0.11) / 0.08, 0, 1) * below
    col = col * (1 - 0.4 * shadow)[..., None]
    # barnacles low on the posts
    bn = T.fnoise(sh, 0.006, 412)
    col = lerp(col, BARN * 0.8, sstep(1.2, 1.8, bn) * post * (zz > 0.6))
    return Tex(col, np.zeros(sh))


# --- mossy stone quay -------------------------------------------------------------

STONE = [rgb(150, 156, 160), rgb(124, 130, 138)]
MOSS = [rgb(60, 124, 96), rgb(86, 150, 112), rgb(40, 96, 78)]


def quay_top(var):
    x, y = T.top_grid()
    sh = x.shape
    d = T.edge_dist(x, y)
    m = sstep(0.05, 0.25, d)
    layouts = [
        [(0, 0, 1, 0.5), (0, 0.5, 1, 1)],
        [(0, 0, 0.5, 1), (0.5, 0, 1, 0.5), (0.5, 0.5, 1, 1)],
        [(0, 0, 1, 0.34), (0, 0.34, 0.6, 1), (0.6, 0.34, 1, 1)],
    ]
    rng = np.random.default_rng(500 + var)
    col = np.zeros(sh + (3,))
    dj = np.full(sh, 1e9)
    n = T.lerp(T.fbm(sh, 0.08, 501), T.fbm(sh, 0.08, 505 + var), m)
    for (x0, y0, x1, y1) in layouts[var % 3]:
        inside = (x >= x0) & (x < x1) & (y >= y0) & (y < y1)
        tone = rng.uniform(0.92, 1.06)
        c = lerp(STONE[1], STONE[0], sstep(-1.2, 1.2, n)) * tone
        col = np.where(inside[..., None], c, col)
        dd = np.minimum(np.minimum(x - x0, x1 - x), np.minimum(y - y0, y1 - y))
        dj = np.where(inside, dd, dj)
    gap = 1 - sstep(0.008, 0.03, dj)
    mn = T.lerp(T.fbm(sh, 0.05, 510), T.fbm(sh, 0.05, 515 + var), m)
    moss = np.clip(gap * 1.1 + sstep(0.6, 1.4, mn - dj * 14), 0, 1)
    mcol = lerp(MOSS[2], MOSS[1], sstep(-1, 1.5, T.fnoise(sh, 0.012, 520 + var)))
    col = col * (1 - 0.35 * gap)[..., None]
    col = lerp(col, mcol, moss * 0.9)
    hgt = 0.002 * n - 0.006 * gap + 0.005 * moss
    emit = None
    if var == 2:
        # an iron mooring ring set in the stone
        dr = np.abs(T.sd_circle(x, y, 0.70, 0.68, 0.06)) - 0.012
        col = lerp(col, rgb(70, 64, 62), T.cover(dr))
        col = lerp(col, rgb(40, 38, 40), T.cover(T.sd_circle(x, y, 0.70, 0.60, 0.02)))
        hgt = hgt + 0.006 * T.cover(dr)
    col = T.darken_top_edges(col, x, y, 0.07)
    return Tex(col, hgt, emit=emit)


def quay_side():
    """Ashlar courses with a dark wet algae band at the bottom and glowing
    specks in it (the high-tide line)."""
    U, Z = T.side_grid()
    sh = U.shape
    v = (Z + T.FLOOR_M) / T.FLOOR_M
    n = T.fbm(sh, 0.06, 541)
    col = lerp(STONE[1], STONE[0], sstep(-1.2, 1.2, n)) * 0.95
    row = (v >= 0.5).astype(int)
    uu = np.mod(U + 0.25 * row, 0.5)
    du = np.minimum(uu, 0.5 - uu)
    dv = np.minimum(np.mod(v, 0.5), 0.5 - np.mod(v, 0.5)) * T.FLOOR_M
    g = 1 - sstep(0.004, 0.018, np.minimum(du, dv))
    col = col * (1 - 0.4 * g)[..., None]
    band = sstep(0.42, 0.30, v + 0.04 * T.noise1(U.shape[1], 0.03, 542)[None, :])
    col = lerp(col, lerp(MOSS[2], rgb(26, 44, 44), 0.5), band * 0.85)
    edge = 0.035 + 0.03 * np.clip(T.noise1(U.shape[1], 0.03, 543)[None, :], -1, 2)
    mm = sstep(edge + 0.008, edge - 0.008, -Z)
    col = lerp(col, MOSS[0], mm * 0.9)
    hgt = 0.003 * n - 0.006 * g + 0.004 * mm
    rng = np.random.default_rng(544)
    emit = np.zeros(sh + (3,))
    Hm = T.FLOOR_M
    for k in range(10):
        cu, cz = rng.uniform(0, 1), rng.uniform(0.03, Hm * 0.35)
        r = rng.uniform(0.004, 0.007)
        c = BIO_CYAN if k % 3 else BIO_MAGENTA
        for ox in (-1, 0, 1):
            dd = T.sd_circle(U, Z + Hm, cu + ox, cz, r)
            emit = emit + (T.cover(dd) + np.clip(1 - np.maximum(dd, 0) / (r * 2), 0, 1) ** 2 * 0.3)[..., None] * c
    return Tex(col, hgt, emit=emit)


# --- the sea -------------------------------------------------------------------

SEA_D = rgb(14, 52, 84)
SEA_M = rgb(26, 84, 120)
SEA_L = rgb(110, 190, 220)


def sea_top(var):
    """Petrol-blue night sea: slow crossing swells (not a river streak),
    moonlit glints and a few plankton sparks (emissive)."""
    x, y = T.top_grid()
    sh = x.shape
    d = T.edge_dist(x, y)
    m = sstep(0.05, 0.3, d)
    n = T.lerp(T.fbm(sh, 0.14, 601), T.fbm(sh, 0.14, 610 + var), m)
    col = lerp(SEA_D, SEA_M, sstep(-1.4, 1.4, n))
    # swells: two families of soft crests, diagonal, whole waves per cell
    s1 = np.sin(2 * math.pi * (2 * x + 3 * y) + 1.4 * T.lerp(T.fbm(sh, 0.15, 620), T.fbm(sh, 0.15, 625 + var), m))
    s2 = np.sin(2 * math.pi * (-3 * x + 2 * y) + 1.4 * T.lerp(T.fbm(sh, 0.15, 630), T.fbm(sh, 0.15, 635 + var), m))
    crest = sstep(0.75, 1.0, s1) * 0.6 + sstep(0.8, 1.0, s2) * 0.4
    col = lerp(col, SEA_M * 1.3, crest * 0.6)
    gl = T.lerp(T.fnoise(sh, 0.008, 640), T.fnoise(sh, 0.008, 645 + var), m)
    glint = sstep(1.9, 2.6, gl) * sstep(0.3, 1.0, s1)
    col = lerp(col, SEA_L, glint * 0.8)
    rng = np.random.default_rng(650 + var)
    emit = _glow_dots(x, y, rng, 3 + var, 0.004, 0.006, [BIO_CYAN], region=0.1)
    hgt = 0.003 * n + 0.002 * crest
    return Tex(col, hgt, emit=emit, rough=np.full(sh, 0.25))


def whirl_top(var=0):
    """The deep water: an almost black-navy hole with a spiral of lighter
    water and cyan foam arms turning into it. Reads as 'do not go here'."""
    x, y = T.top_grid()
    sh = x.shape
    dx, dy = x - 0.5, y - 0.5
    r = np.hypot(dx, dy)
    th = np.arctan2(dy, dx)
    d = T.edge_dist(x, y)
    m = sstep(0.05, 0.25, d)
    n = T.lerp(T.fbm(sh, 0.14, 701), T.fbm(sh, 0.14, 710 + var), m)
    base = lerp(SEA_D, SEA_M, sstep(-1.4, 1.4, n))
    # at the edges it is the sea; towards the centre it darkens to navy black
    deep = sstep(0.48, 0.05, r)
    col = lerp(base, rgb(4, 14, 30), deep * 0.95)
    # 3 spiral arms: phase = 3 th + k log r
    ph = 3 * th + 9.0 * np.log(r + 0.03)
    arm = 0.5 + 0.5 * np.cos(ph)
    arms = sstep(0.55, 0.95, arm) * sstep(0.47, 0.30, r) * sstep(0.03, 0.10, r)
    col = lerp(col, SEA_M * 1.5, arms * 0.7)
    foam = sstep(0.86, 0.98, arm) * sstep(0.44, 0.26, r) * sstep(0.05, 0.14, r)
    col = lerp(col, rgb(170, 235, 240), foam * 0.85)
    emit = foam[..., None] * BIO_CYAN[None, None, :] * 0.35
    hgt = -0.02 * deep + 0.003 * arms
    return Tex(col, hgt, emit=emit, rough=np.full(sh, 0.25))


def sea_side():
    U, Z = T.side_grid()
    sh = U.shape
    col = T.fill(sh, SEA_D * 0.7)
    return Tex(col, np.zeros(sh))


# --- the tide cell -------------------------------------------------------------------

def tide_top(state):
    """The same cell of sand in three states: 0 dry (low tide: ripples, a
    shell, a tide pool edge), 1 the water creeping in (a wet sheen and a foam
    fringe crossing it: the warning), 2 flooded (shallow sea over the sand,
    unmistakably water: you sink)."""
    x, y = T.top_grid()
    sh = x.shape
    d = T.edge_dist(x, y)
    m = sstep(0.07, 0.3, d)
    col, hgt, rough, wet = _sand(x, y, 0, m, seed=800)
    rng = np.random.default_rng(830)
    _shell(x, y, col, hgt, 0.66, 0.36, 0.03, 0.9, rgb(236, 214, 200))
    # marker: pale lines of dried foam (the tide line) on the dry state
    wob = 0.03 * T.fbm(sh, 0.12, 840)
    emit = None
    if state == 0:
        tl = np.exp(-((y - 0.72 - wob) / 0.012) ** 2)
        col = lerp(col, rgb(214, 214, 204), tl * 0.55)
        # dark wet sand edge (it floods: the sand never dries quite out)
        col = col * (1 - 0.08 * sstep(0.2, 0.9, y))[..., None]
    if state == 1:
        # shallow water sheet over the back half, a lace of foam at its edge
        front = 0.46 + wob + 0.04 * np.sin(2 * math.pi * 2 * x)
        water = sstep(front - 0.01, front + 0.05, y)
        sea = lerp(SEA_M, SEA_L, 0.12)
        col = lerp(col * 0.7, sea, water * 0.75)
        lace = np.exp(-((y - front) / 0.018) ** 2)
        lace2 = np.exp(-((y - front - 0.09) / 0.01) ** 2) * 0.6
        foam = np.clip(lace + lace2, 0, 1) * (0.6 + 0.4 * sstep(-0.5, 1.0, T.fnoise(sh, 0.01, 850)))
        col = lerp(col, rgb(215, 240, 240), foam * 0.9)
        rough = rough * (1 - water) + 0.15 * water
        emit = (foam * 0.25)[..., None] * BIO_CYAN[None, None, :]
        col = col * (1 - 0.18 * (1 - water))[..., None]      # the dry part already dark and wet
    if state == 2:
        n = T.fbm(sh, 0.12, 860)
        sea = lerp(SEA_D * 1.1, SEA_M, sstep(-1.2, 1.2, n))
        # the sand shows faintly through near the rim: shallow
        col = lerp(col * 0.45, sea, 0.82)
        s1 = np.sin(2 * math.pi * (2 * x + 3 * y) + 1.2 * T.fbm(sh, 0.15, 861))
        col = lerp(col, SEA_M * 1.35, sstep(0.75, 1.0, s1) * 0.5)
        gl = sstep(1.9, 2.6, T.fnoise(sh, 0.008, 862)) * sstep(0.3, 1.0, s1)
        col = lerp(col, SEA_L, gl * 0.8)
        # foam rim: the water laps at the four edges
        # a soft irregular foam lace a little inside the rim (the edge itself
        # stays the sea colour: no picture frame)
        wob2 = 0.02 * T.fbm(sh, 0.06, 864)
        lace = np.exp(-((d - 0.06 - wob2) / 0.012) ** 2) * sstep(0.2, 1.2, T.fnoise(sh, 0.015, 863))
        col = lerp(col, rgb(190, 226, 232), lace * 0.55)
        rough = np.full(sh, 0.2)
        emit = _glow_dots(x, y, rng, 3, 0.004, 0.006, [BIO_CYAN], region=0.15)
        hgt = 0.002 * s1
    col = T.darken_top_edges(col, x, y)
    return Tex(col, hgt, rough=rough, emit=emit)


def tide_side(state):
    U, Z = T.side_grid()
    col, hgt = _packed_sand(U, Z)
    col = col * 0.82                   # always damp
    if state == 2:
        edge = 0.03 + 0.01 * T.noise1(U.shape[1], 0.04, 870)[None, :]
        mm = sstep(edge + 0.005, edge - 0.005, -Z)
        col = lerp(col, lerp(SEA_M, SEA_L, 0.4), mm)
    return Tex(col, hgt)


def _preview(out):
    import os
    from PIL import Image
    os.makedirs(out, exist_ok=True)

    def save(name, tex, light):
        lin = tex.col * light
        if tex.emit is not None:
            lin = lin + tex.emit * 2.0
        im = Image.fromarray(T.to_srgb8(lin[::-1]))
        im.resize((im.width * 2, im.height * 2), Image.NEAREST).save(os.path.join(out, name + '.png'))
    for v in range(3):
        save('sand_top_v%d' % v, sand_top(v), LIGHT_TOP)
        save('rock_top_v%d' % v, rock_top(v), LIGHT_TOP)
        save('pier_x_v%d' % v, pier_top(v, 'x'), LIGHT_TOP)
        save('quay_top_v%d' % v, quay_top(v), LIGHT_TOP)
        save('sea_top_v%d' % v, sea_top(v), LIGHT_TOP)
        save('tide_%d' % v, tide_top(v), LIGHT_TOP)
    save('pier_y_v0', pier_top(0, 'y'), LIGHT_TOP)
    save('whirl', whirl_top(0), LIGHT_TOP)
    save('sand_side', sand_side(), LIGHT_FRONT)
    save('rock_side', rock_side(), LIGHT_FRONT)
    save('pier_side', pier_side(), LIGHT_FRONT)
    save('quay_side', quay_side(), LIGHT_FRONT)


if __name__ == '__main__':
    _preview(sys.argv[1] if len(sys.argv) > 1 else '/tmp/bay_tex')
