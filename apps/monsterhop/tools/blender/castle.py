"""Monster Hop - castle (Vampire Castle, violet): blocks, fills, surfaces,
bridges, props and the zone's moving things, rendered to sprites. SPEC.md 6.

    Blender -b -P castle.py -- --out ../../assets/tiles_castle [--sample] [--only a,b] [--samples N]

--sample renders the style sample only (phase 1); --only takes sprite names
(or fnmatch patterns, or the part after the zone prefix).
Everything is built at cell (0, 0) floor 0 under C.reset('castle').
"""
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import numpy as np  # noqa: E402
import mh_common as C  # noqa: E402
import zones_cc_lib as L  # noqa: E402
import zones_cc_kit as K  # noqa: E402
from zones_cc_lib import FM, Paint, blend, lin, lina  # noqa: E402
from zones_cc_kit import (M, block_box, cached, edge_shade, interior, mat_block, pm,  # noqa: E402
                          render_prop, render_tile, side_shade, smooth01, tex_pair, SURF_T)

A, REG = K.setup('castle')

# ---------------------------------------------------------------------------
# palette (sRGB albedo; the castle light takes them down to violet):
# purples, lilac stone, crimson accents, candle orange
# ---------------------------------------------------------------------------
P_FLAG = '#cbbde4'
P_FLAG_TONES = ['#cbbde4', '#c2b2de', '#d3c6ea', '#bfaed8', '#c9b5dc', '#c4b8e4']
P_JOINT = '#4b3b63'
P_STONE = '#bfafdb'
P_STONE_TONES = ['#bfafdb', '#b3a3d3', '#c8b9e2', '#ad9dcb', '#bda6d4', '#c3b4df']
P_MORTAR = '#5a4a74'
P_CAP = '#d2c6ea'
P_SURROUND = '#dcd2ee'
P_CARPET = '#b8265f'
P_CARPET_DK = '#8e1a4b'
P_CARPET_EDGE = '#5c1034'
P_GOLD = '#f5c451'
P_MOAT = '#4a3480'


# ---------------------------------------------------------------------------
# textures
# ---------------------------------------------------------------------------

def ashlar(P, seed, rows, mortar=0.022, tones=P_STONE_TONES, mcol=P_MORTAR, fixed_edge=True):
    """Stone courses over a side texture. rows: for each course (bottom up) the
    joints' u positions in [0, 1) (periodic in u). The stone that straddles
    u = 0 / 1 has a colour that does not depend on `seed`, so any variant
    sits next to any other. Returns colour, height."""
    rng = L.rng_for('ashlar', seed)
    n = len(rows)
    ch = (P.v1 - P.v0) / n
    fv_all = (P.Y - P.v0) / ch
    row = np.clip(np.floor(fv_all).astype(int), 0, n - 1)
    fv = fv_all - row
    col = np.zeros((P.h, P.w, 3), np.float32)
    du = np.full(P.X.shape, 1e9, np.float32)
    u = P.X - P.u0
    for r, J in enumerate(rows):
        sel = row == r
        J = sorted(J)
        # stone index: number of joints <= u (0 .. len(J)); the last one wraps to 0
        idx = np.zeros(P.X.shape, int)
        for j in J:
            idx += (u >= j)
        idx = np.where(idx == len(J), 0, idx)
        wraps = J[0] > 0
        for k in range(len(J)):
            if k == 0 and wraps and fixed_edge:
                t = tones[L.rng_for('ashlar_edge', r).randrange(len(tones))]
                f = 1.0
            else:
                t = tones[rng.randrange(len(tones))]
                f = rng.uniform(0.95, 1.05)
            m = sel & (idx == k)
            col[m] = lina(t) * f
        d = np.full(P.X.shape, 1e9, np.float32)
        for j in J:
            dd = np.abs(u - j)
            dd = np.minimum(dd, 1.0 - dd)
            d = np.minimum(d, dd)
        du = np.where(sel, d, du)
    dv = np.minimum(fv, 1 - fv) * ch
    dist = np.minimum(du, dv)
    mort = P.cover(dist - mortar / 2.0, 0.004)
    # painted bevel: light top edge, dark bottom edge, soft rounding
    top_e = smooth01(1 - (1 - fv) * ch / 0.03)
    bot_e = smooth01(1 - fv * ch / 0.03)
    col *= (1 + 0.14 * top_e - 0.16 * bot_e - 0.08 * smooth01(1 - du / 0.03))[..., None]
    col *= (1 + 0.05 * P.noise(0.02, seed + 3) + 0.04 * P.noise(0.1, seed + 4))[..., None]
    col = blend(col, mort, lina(mcol) * (1 + 0.05 * P.noise(0.01, seed + 5))[..., None])
    h = 0.7 - 0.5 * mort - 0.12 * smooth01(1 - dist / 0.03) + 0.05 * P.noise(0.012, seed + 6)
    return col, h


ROWS_A = [[0.00, 0.47], [0.22, 0.73]]            # two courses per floor
ROWS_B = [[0.00, 0.38, 0.70], [0.18, 0.55, 0.86]]
ROWS_C = [[0.00, 0.55], [0.27, 0.78]]


@cached
def tex_stone_side(variant, win=False):
    P = Paint(0, 1, -FM, 0)
    rows = [ROWS_A, ROWS_B, ROWS_C][variant % 3]
    col, h = ashlar(P, 40 + variant, rows)
    if win:
        # a lighter carved surround around the arch
        pts = L.arch_outline(0.5, WIN_W, WIN_Z0, WIN_ZS, WIN_ZT, 12)
        sd = P.sd_poly(pts)
        m = P.cover(sd - 0.045, 0.004) * (1 - P.cover(sd + 0.002, 0.004))
        sc = lina(P_SURROUND) * (1 + 0.04 * P.noise(0.015, 90))[..., None]
        col = blend(col, m, sc)
        rim = P.cover(np.abs(sd - 0.045) - 0.004, 0.004)
        col = blend(col, rim * 0.6, lina(P_MORTAR))
        # the key stone and the sill
        sill = P.cover(P.sd_box(0.5, WIN_Z0 - 0.015, WIN_W / 2 + 0.05, 0.018, 0.004))
        col = blend(col, sill, lina(P_SURROUND))
        h = np.where(m > 0.5, 0.75, h)
    col *= side_shade(P, True, 0.10)
    return col, h


@cached
def tex_stone_top(variant):
    """A wall's capstone: one big slab per cell."""
    P = Paint(0, 1, 0, 1)
    col = P.full(lina(P_CAP))
    col *= (1 + 0.04 * P.noise(0.012, 300) + 0.05 * P.noise(0.18, 301 + variant) * interior(P, 0.05, 0.15))[..., None]
    rng = L.rng_for('cap', variant)
    for _ in range(18):
        cx, cy = rng.uniform(0.1, 0.9), rng.uniform(0.1, 0.9)
        m = P.cover(P.sd_circle(cx, cy, rng.uniform(0.006, 0.012)))
        col = blend(col, m * 0.5, lina('#a597c4'))
    col *= edge_shade(P, 0.035, 0.26)
    return col, 0.5 + 0.06 * P.noise(0.012, 302)


@cached
def tex_fill_stone_top():
    P = Paint(0, 1, 0, 1)
    col, h = ashlar(P, 70, [[0.0, 0.5], [0.25, 0.75], [0.0, 0.5], [0.25, 0.75]])
    return col, h


@cached
def tex_flag_top(variant):
    """Grain and speckle on the flags (the tint comes per flag)."""
    P = Paint(0, 1, 0, 1)
    col = P.full((1.0, 1.0, 1.0))
    col *= (1 + 0.05 * P.noise(0.012, 400) + 0.04 * P.noise(0.06, 401 + variant))[..., None]
    rng = L.rng_for('flagspeck', variant)
    for _ in range(40):
        cx, cy = rng.uniform(0.03, 0.97), rng.uniform(0.03, 0.97)
        m = P.cover(P.sd_circle(cx, cy, rng.uniform(0.005, 0.010)))
        col = blend(col, m * 0.45, (0.78, 0.74, 0.86))
    h = 0.5 + 0.08 * P.noise(0.012, 402)
    if variant == 1:
        ck = L.crack_path(rng, (0.12, 0.20), (0.34, 0.50), 5, 0.025)
        m = P.cover(P.sd_polyline(ck, 0.012), 0.003)
        col = blend(col, m * 0.8, (0.45, 0.40, 0.55))
        h -= 0.2 * m
    return col, h


@cached
def tex_flag_side():
    P = Paint(0, 1, -0.1, 0.0)
    col = P.full((0.92, 0.92, 0.95)) * (1 + 0.05 * P.noise(0.012, 410))[..., None]
    return col, 0.5 + 0.05 * P.noise(0.012, 411)


@cached
def tex_found_side():
    """The foundation under a floor block (and the stone fill): big ashlar."""
    P = Paint(0, 1, -FM, 0)
    col, h = ashlar(P, 80, [[0.10, 0.62], [0.36, 0.86]])
    col *= side_shade(P, True, 0.08)
    return col, h


@cached
def tex_carpet_top(variant):
    """Runner along X: gold borders along the cell's X edges (y ~ 0 and 1),
    a crimson-purple field with a diamond motif (period 0.5 m along X)."""
    P = Paint(0, 1, 0, 1)
    y = P.Y
    x = P.X
    col = P.full(lina(P_CARPET))
    col *= (1 + 0.04 * P.noise(0.008, 500))[..., None]
    # diamond lattice in the field (period 0.5 along X, centred on y = 0.5)
    fx = ((x / 0.25) % 1.0) - 0.5
    fy = (((y - 0.5) / 0.25) % 1.0) - 0.5
    dia = np.abs(fx) + np.abs(fy)
    field = (y > 0.15) & (y < 0.85)
    ring = P.cover(np.abs(dia - 0.34) * 0.25 - 0.006, 0.003) * field
    col = blend(col, ring * 0.9, lina(P_CARPET_DK))
    dot = P.cover(dia * 0.25 - 0.018, 0.003) * field
    col = blend(col, dot * 0.9, lina('#e8a948'))
    # borders
    dy = np.minimum(y, 1 - y)
    gold = (dy > 0.035) & (dy < 0.13)
    gcol = lina(P_GOLD) * (1 + 0.05 * P.noise(0.01, 501))[..., None]
    mg = P.cover(np.abs(dy - 0.0825) - 0.0475, 0.003)
    col = blend(col, mg, gcol)
    # a crimson line and little diamonds inside the gold band
    ml = P.cover(np.abs(dy - 0.0825) - 0.006, 0.003)
    bx = ((x / 0.125) % 1.0) - 0.5
    bd = np.abs(bx) * 0.125 + np.abs(dy - 0.0825)
    md = P.cover(bd - 0.022, 0.003) * gold
    col = blend(col, np.maximum(ml, md) * 0.85, lina('#9c2350'))
    me = P.cover(0.035 - dy, 0.003)
    col = blend(col, 1 - me, lina(P_CARPET_EDGE))
    # a faint line at the cell's y edges only (the runner is continuous along X)
    col *= (1.0 - 0.25 * (1 - smooth01(dy / 0.012)))[..., None]
    col *= (1.0 - 0.06 * (1 - smooth01(np.minimum(x, 1 - x) / 0.012)))[..., None]
    h = 0.5 + 0.08 * P.noise(0.006, 502) - 0.15 * ring - 0.1 * (1 - me)
    if variant == 1:
        # a worn, paler patch in the field where everyone walks
        sd = P.sd_ellipse(0.46, 0.50, 0.20, 0.13) + 0.02 * P.noise(0.04, 503)
        col = blend(col, P.cover(sd, 0.05) * 0.35, lina('#d0587f'))
    if variant == 2:
        rng = L.rng_for('carpet_petals')
        for _ in range(3):
            cx, cy, a = rng.uniform(0.25, 0.75), rng.uniform(0.3, 0.7), rng.uniform(0, 3)
            col = blend(col, P.cover(P.sd_box(cx, cy, 0.028, 0.017, 0.015, rot=a), 0.003), lina('#e8467e'))
    return col, h


@cached
def tex_carpet_side():
    P = Paint(0, 1, -0.045, 0.0)
    col = P.full(lina(P_CARPET_EDGE))
    m = P.cover(np.abs(P.Y + 0.012) - 0.006, 0.002)
    col = blend(col, m, lina(P_GOLD))
    return col, P.zeros() + 0.5


@cached
def tex_moat_top(variant):
    P = Paint(0, 1, 0, 1)
    col = P.full(lina(P_MOAT))
    sw = P.noise(0.16, 800)
    col *= (1 + 0.07 * sw)[..., None]
    rip = P.zeros()
    rng = L.rng_for('moat', variant)
    col = K.water_glints(P, rng, 3, col, '#9784d8', 0.8)
    inner = interior(P, 0.1, 0.1)
    if variant == 0:
        # two drifting rose petals
        for cx, cy, a in ((0.30, 0.30, 0.5), (0.70, 0.62, -0.4)):
            sd = P.sd_box(cx, cy, 0.03, 0.018, 0.016, rot=a)
            col = blend(col, P.cover(sd, 0.003) * inner, lina('#d23a6e'))
    h = 0.5 + 0.3 * rip + 0.08 * sw
    return col, h


@cached
def tex_moat_side():
    P = Paint(0, 1, -SURF_T, 0)
    col = P.full(lina('#34245e')) * (1 + 0.05 * P.noise(0.05, 810))[..., None]
    return col, P.zeros() + 0.5


WIN_W, WIN_Z0, WIN_ZS, WIN_ZT = 0.36, -0.455, -0.265, -0.045    # a pointed arch: rise > half width


@cached
def tex_stained_glass(variant=0):
    """Stained glass in window coordinates (u in [0, W], v in [0, H], metres).
    Big panes for a ~20 px window: two lancets (violet, blue) split by a bar,
    a transom, and a rose above (crimson ring, gold heart) on deep violet."""
    W, H = WIN_W, WIN_ZT - WIN_Z0
    P = Paint(0, W, 0, H, px_per_m=320)
    pts = [(x - (0.5 - W / 2), z - WIN_Z0) for x, z in L.arch_outline(0.5, W, WIN_Z0, WIN_ZS, WIN_ZT, 14)]
    sd = P.sd_poly(pts)
    X, Y = P.X, P.Y
    tr = 0.215                       # the transom
    left = X < W / 2
    col = np.where(left[..., None], lina('#a066ff'), lina('#5b86ff'))
    col = np.where((Y < 0.105)[..., None], col * 0.78, col)                   # lower panes a bit darker
    col = np.where((Y > tr)[..., None], lina('#7446e8'), col)
    rcx, rcy, rr = W / 2, tr + 0.085, 0.062
    dr = np.hypot(X - rcx, Y - rcy)
    col = np.where((dr < rr)[..., None], lina('#ff4f8f'), col)
    col = np.where((dr < 0.030)[..., None], lina('#ffcf55'), col)
    col *= (0.85 + 0.3 * (Y / H))[..., None]
    lw = 0.0065
    lead = P.cover(np.abs(sd + 0.004) - 0.008, 0.003)
    lead = np.maximum(lead, P.cover(np.abs(X - W / 2) - lw, 0.003) * (Y < tr))
    lead = np.maximum(lead, P.cover(np.abs(Y - tr) - lw, 0.003))
    lead = np.maximum(lead, P.cover(np.abs(Y - 0.105) - lw * 0.8, 0.003) * (Y < tr))
    lead = np.maximum(lead, P.cover(np.abs(dr - rr) - lw, 0.003))
    lead = np.maximum(lead, P.cover(np.abs(dr - 0.030) - lw * 0.8, 0.003))
    col = blend(col, lead, lina('#1c1230'))
    emit = col * (1 - lead)[..., None]
    return col, emit


# ---------------------------------------------------------------------------
# blocks
# ---------------------------------------------------------------------------

FLAG_LAYOUTS = [
    [(0, 0, 0.58, 0.46), (0.58, 0, 1, 0.46), (0, 0.46, 0.36, 1), (0.36, 0.46, 1, 1)],
    [(0, 0, 0.42, 0.62), (0.42, 0, 1, 0.36), (0.42, 0.36, 1, 1), (0, 0.62, 0.42, 1)],
    [(0, 0, 1, 0.34), (0, 0.34, 0.5, 1), (0.5, 0.34, 1, 0.70), (0.5, 0.70, 1, 1)],
]
JOINT_EDGE = 0.034      # at the cell's edges (the grid reads) ...
JOINT_IN = 0.020        # ... and between the flags of one cell


def foundation(key='found', z1=-0.03):
    k = mat_block(key, K.flat_tex('flagjoint', P_JOINT, 0.0), tex_pair('found_side', tex_found_side()),
                  rough=0.9, spec=0.2, bump=0.3, bump_dist=0.015)
    return L.box('found', 0, 0, -FM, 1, 1, z1, k, bev=0.006)


def flag_block(v):
    rng = L.rng_for('flags', v)
    objs = [foundation()]
    top = tex_pair('flag_top%d' % v, tex_flag_top(v))
    side = tex_pair('flag_side', tex_flag_side())
    for i, (x0, y0, x1, y1) in enumerate(FLAG_LAYOUTS[v % 3]):
        jx0 = (JOINT_EDGE if x0 <= 0 else JOINT_IN) / 2
        jx1 = (JOINT_EDGE if x1 >= 1 else JOINT_IN) / 2
        jy0 = (JOINT_EDGE if y0 <= 0 else JOINT_IN) / 2
        jy1 = (JOINT_EDGE if y1 >= 1 else JOINT_IN) / 2
        # irregular: every corner pulled in a little, the top a hair lower
        cs = [(x0 + jx0, y0 + jy0), (x1 - jx1, y0 + jy0), (x1 - jx1, y1 - jy1), (x0 + jx0, y1 - jy1)]
        jit = []
        for (cx, cy) in cs:
            sx = 1 if cx < (x0 + x1) / 2 else -1
            sy = 1 if cy < (y0 + y1) / 2 else -1
            jit.append((cx + sx * rng.uniform(0.0, 0.010), cy + sy * rng.uniform(0.0, 0.010)))
        zt = -rng.uniform(0.0, 0.008)
        tone = P_FLAG_TONES[rng.randrange(len(P_FLAG_TONES))]
        tint = tuple(np.array(lin(tone)) * rng.uniform(0.86, 0.95))
        key = 'flag%d_%d' % (v, i)
        k = M(key, lambda key=key, tint=tint: L.block_mat(key, top=top, side=side, z0=-0.1, z1=0.0,
                                                           rough=0.75, spec=0.3, bump=0.3, bump_dist=0.012,
                                                           tint=tint))
        ob = L.prism('flag', jit, -0.075, zt, k, bev=0.013, seg=2)
        objs.append(ob)
    return objs


for v in range(3):
    @REG.add('castle_blk_flagstone_v%d' % v, sample=(v < 2))
    def _(v=v):
        render_tile('castle_blk_flagstone_v%d' % v, flag_block(v))


def carpet_block(v, axis='x'):
    objs = [foundation(z1=-0.04)]
    col, h = tex_carpet_top(v)
    if axis == 'y':
        # the runner along Y: the same carpet turned a quarter (borders on the X = 0 / 1 edges)
        col, h = np.ascontiguousarray(np.transpose(col, (1, 0, 2))), np.ascontiguousarray(h.T)
    k = mat_block('carpet%s%d' % (axis, v), tex_pair('carpet_top%s%d' % (axis, v), (col, h)),
                  tex_pair('carpet_side', tex_carpet_side()), z0=-0.045, z1=0.0, rough=0.95, spec=0.15,
                  bump=0.25, bump_dist=0.01, sheen=0.6)
    objs.append(L.box('carpet', 0, 0, -0.045, 1, 1, 0.0, k, bev=0.005, seg=2))
    return objs


for v in range(3):
    @REG.add('castle_blk_carpet_v%d' % v, sample=(v == 0))
    def _(v=v):
        render_tile('castle_blk_carpet_v%d' % v, carpet_block(v), extra={'runner': 'x'})

    @REG.add('castle_blk_carpet_y_v%d' % v)
    def _(v=v):
        render_tile('castle_blk_carpet_y_v%d' % v, carpet_block(v, 'y'), extra={'runner': 'y'})


def stone_block(v, win=False, top=None, key=None):
    side = tex_pair('stone_side%d%s' % (v, 'w' if win else ''), tex_stone_side(v, win))
    top = top or tex_pair('stone_top%d' % v, tex_stone_top(v))
    key = key or 'stone%d%s' % (v, 'w' if win else '')
    k = mat_block(key, top, side, rough=0.8, spec=0.3, bump=0.35, bump_dist=0.015)
    wall = block_box(k, bev=0.0 if win else 0.008)
    objs = [wall]
    if win:
        dep, dep_r = 0.04, 0.015          # the right face is seen at a grazing angle
        pts = L.arch_outline(0.5, WIN_W, WIN_Z0, WIN_ZS, WIN_ZT, 12)
        cut_f = L.prism('cut_f', pts, -0.1, dep, None, axis='y')
        cut_r = L.prism('cut_r', pts, 1 - dep_r, 1.1, None, axis='x')
        L.cut(wall, [cut_f, cut_r])
        L.bevel(wall, 0.008)
        gc, ge = tex_stained_glass(0)
        ic, ie = L.image('stglass_c', gc), L.image('stglass_e', ge)
        Hh = WIN_ZT - WIN_Z0
        kf = M('stglass_f', lambda: L.tex_mat('stglass_f', ic, 'xz', rough=0.2, spec=0.5, eimg=ie,
                                               emit_strength=1.5, scale=(WIN_W, Hh),
                                               offset=(0.5 - WIN_W / 2, WIN_Z0)))
        kr = M('stglass_r', lambda: L.tex_mat('stglass_r', ic, 'yz', rough=0.2, spec=0.5, eimg=ie,
                                               emit_strength=1.5, scale=(WIN_W, Hh),
                                               offset=(0.5 - WIN_W / 2, WIN_Z0)))
        objs.append(L.prism('glass_f', pts, dep - 0.005, dep, kf, axis='y'))
        objs.append(L.prism('glass_r', pts, 1 - dep_r, 1 - dep_r + 0.005, kr, axis='x'))
    return objs


for v in range(3):
    @REG.add('castle_blk_stone_v%d' % v, sample=(v == 0))
    def _(v=v):
        render_tile('castle_blk_stone_v%d' % v, stone_block(v))


@REG.add('castle_blk_stone_win', sample=True)
def _():
    render_tile('castle_blk_stone_win', stone_block(0, win=True), extra={'lit': True})


def battlement_block(v):
    objs = stone_block(v)
    km = M('merlon', lambda: L.simple_mat('merlon', P_STONE, rough=0.8, spec=0.3, col2='#b3a3d3', noise=1,
                                           noise_scale=9, bump=0.15, bump_scale=25))
    kc = M('merlon_cap', lambda: L.simple_mat('merlon_cap', P_CAP, rough=0.75, spec=0.3, col2='#c8bbe2',
                                               noise=1, noise_scale=12))
    s, hgt = 0.25, 0.30
    for cx, cy in ((0, 0), (1 - s, 0), (0, 1 - s), (1 - s, 1 - s)):
        objs.append(L.box('merlon', cx + 0.005, cy + 0.005, -0.01, cx + s - 0.005, cy + s - 0.005, hgt - 0.04,
                          km, bev=0.012))
        objs.append(L.box('mcap', cx + 0.002, cy + 0.002, hgt - 0.045, cx + s - 0.002, cy + s - 0.002, hgt,
                          kc, bev=0.012))
    return objs


for v in range(3):
    @REG.add('castle_blk_battlement_v%d' % v, sample=(v == 0))
    def _(v=v):
        render_tile('castle_blk_battlement_v%d' % v, battlement_block(v),
                    extra={'merlon_h_m': 0.30})


# fills ----------------------------------------------------------------------

@REG.add('castle_fill_stone', sample=True)
def _():
    k = mat_block('fillstone', tex_pair('fillstone_top', tex_fill_stone_top()),
                  tex_pair('found_side', tex_found_side()), rough=0.85, spec=0.25, bump=0.35,
                  bump_dist=0.015)
    render_tile('castle_fill_stone', [block_box(k, bev=0.006)])


# surfaces -------------------------------------------------------------------

for v in range(3):
    @REG.add('castle_surf_moat_v%d' % v, sample=(v == 0))
    def _(v=v):
        k = mat_block('moat%d' % v, tex_pair('moat_top%d' % v, tex_moat_top(v)),
                      tex_pair('moat_side', tex_moat_side()), z0=-SURF_T, z1=0.0,
                      rough=0.08, spec=0.5, bump=0.05, bump_dist=0.02, clearcoat=0.6)
        ob = L.box('surf', 0, 0, -SURF_T, 1, 1, 0, k)
        render_tile('castle_surf_moat_v%d' % v, [ob], kind='surf', extra={'depth_m': 0.18})


# ---------------------------------------------------------------------------
# props
# ---------------------------------------------------------------------------

def gold_mat(key='gold'):
    return pm(key, '#e2ab45', rough=0.3, spec=0.8, metal=0.35, col2='#c88f32', noise=1, noise_scale=14)


def candle_lights(flames, power=45.0):
    """Each flame: a soft downward cone for the pool (the glow pass) and a
    small point light for the candelabra itself. Returns (all, pool)."""
    lts, pool = [], []
    for p in flames:
        s = L.add_spot(p, color=lin('#ffa84a'), power=power, cone_deg=100, blend=1.0, radius=0.03)
        lts.append(s)
        pool.append(s)
        lts.append(C.add_point_light(p, color=lin('#ffa84a'), power=1.5, radius=0.02))
    return lts, pool


def candle(objs, x, y, z, h, k_wax, k_flame, k_wick):
    objs.append(L.cyl('candle', (x, y), 0.03, z, z + h, k_wax, segs=14))
    objs.append(L.lathe('drip', [(0, z + h - 0.03), (0.034, z + h - 0.03), (0.034, z + h - 0.012),
                                 (0.028, z + h), (0, z + h)], k_wax, c=(x, y, 0), segs=14))
    objs.append(L.cyl('wick', (x, y), 0.005, z + h, z + h + 0.02, k_wick, segs=6))
    fl = L.lathe('flame', [(0, 0), (0.022, 0.012), (0.026, 0.03), (0.018, 0.06), (0.007, 0.085), (0, 0.095)],
                 k_flame, c=(x, y, z + h + 0.012), segs=12)
    fl.visible_shadow = False
    fl.visible_diffuse = False        # the pool is the spot's: the flame mesh lights nothing
    objs.append(fl)
    return (x, y, z + h + 0.05)


@REG.add('castle_candelabra', sample=True)
def _():
    kg = gold_mat()
    k_wax = pm('wax', '#f2e8d6', rough=0.5, spec=0.3)
    k_flame = M('flame', lambda: L.emit_mat('flame', '#ffb54f', 9.0))
    k_wick = pm('wick', '#2a2024', rough=0.8)
    cx, cy = 0.5, 0.5
    objs = []
    # a round stepped foot with three little claw feet
    objs.append(L.lathe('foot', [(0, 0.03), (0.17, 0.03), (0.17, 0.055), (0.13, 0.075), (0.09, 0.10),
                                 (0.05, 0.13), (0.035, 0.16), (0, 0.16)], kg, c=(cx, cy, 0), segs=24))
    for a in (90, 210, 330):
        r = math.radians(a)
        objs.append(L.sphere('claw', (cx + 0.15 * math.cos(r), cy + 0.15 * math.sin(r), 0.028), 0.03, kg))
    # stem with knops
    objs.append(L.lathe('stem', [(0, 0.15), (0.03, 0.15), (0.022, 0.40), (0.05, 0.44), (0.05, 0.47),
                                 (0.022, 0.51), (0.02, 0.80), (0.045, 0.84), (0.045, 0.87), (0.024, 0.90),
                                 (0.024, 1.02), (0, 1.02)], kg, c=(cx, cy, 0), segs=16))
    # two S arms along X (seen side-on from the camera) and the centre cup
    flames = []
    arm_z = 0.93
    for sgn in (-1, 1):
        pts = []
        for i in range(9):
            t = i / 8.0
            x = cx + sgn * 0.26 * t
            z = arm_z + 0.12 * math.sin(math.pi * t * 0.5) ** 2 - 0.06 * math.sin(math.pi * t)
            pts.append((x, cy, z))
        objs.append(L.tube('arm', pts, 0.016, kg, segs=10))
        ex, ez = pts[-1][0], pts[-1][2]
        objs.append(L.lathe('cup', [(0, ez - 0.02), (0.02, ez - 0.02), (0.045, ez + 0.01), (0.05, ez + 0.025),
                                    (0, ez + 0.025)], kg, c=(ex, cy, 0), segs=16))
        flames.append(candle(objs, ex, cy, ez + 0.02, 0.15, k_wax, k_flame, k_wick))
    objs.append(L.lathe('cupc', [(0, 1.0), (0.025, 1.0), (0.05, 1.035), (0.055, 1.05), (0, 1.05)], kg,
                        c=(cx, cy, 0), segs=16))
    flames.append(candle(objs, cx, cy, 1.045, 0.19, k_wax, k_flame, k_wick))
    lights, pool = candle_lights(flames)
    render_prop('castle_candelabra', objs, 1.33, lights, glow_R=1.45, glow_c=(cx, cy), pool_lights=pool,
                extra={'light_at': [[round(p[0], 3), round(p[1], 3), round(p[2], 3)] for p in flames]})


@REG.add('castle_pillar', sample=True)
def _():
    k = pm('pillar', '#d0c3e8', rough=0.75, spec=0.3, col2='#c1b2dd', noise=1, noise_scale=6, bump=0.1,
           bump_scale=30)
    k2 = pm('pillar_dk', '#b9a9d6', rough=0.75, spec=0.3)
    kg = gold_mat('pillar_gold')
    cx, cy = 0.5, 0.5
    objs = [L.box('plinth', 0.13, 0.13, 0.0, 0.87, 0.87, 0.17, k2, bev=0.02)]
    objs.append(L.lathe('base', [(0, 0.17), (0.31, 0.17), (0.31, 0.21), (0.27, 0.25), (0.285, 0.29),
                                 (0.23, 0.33), (0, 0.33)], k, c=(cx, cy, 0), segs=24))
    # a clustered shaft: a core and four colonnettes
    objs.append(L.cyl('core', (cx, cy), 0.17, 0.32, 2.56, k, segs=20))
    for a in (45, 135, 225, 315):
        r = math.radians(a)
        objs.append(L.cyl('col', (cx + 0.155 * math.cos(r), cy + 0.155 * math.sin(r)), 0.075, 0.30, 2.58,
                          k, segs=14))
    objs.append(L.lathe('ring', [(0, 2.55), (0.27, 2.55), (0.27, 2.59), (0, 2.59)], kg, c=(cx, cy, 0), segs=24))
    objs.append(L.lathe('capital', [(0, 2.58), (0.25, 2.58), (0.27, 2.62), (0.33, 2.76), (0.36, 2.80),
                                    (0, 2.80)], k, c=(cx, cy, 0), segs=24))
    objs.append(L.box('abacus', 0.13, 0.13, 2.79, 0.87, 0.87, 3.0, k2, bev=0.02))
    render_prop('castle_pillar', objs, 3.0)


def coffin_outline(scale=1.0, cx=0.5, cy=0.5):
    # along X, the head end at +X
    pts = [(-0.43, -0.15), (0.16, -0.24), (0.43, -0.17), (0.43, 0.17), (0.16, 0.24), (-0.43, 0.15)]
    return [(cx + x * scale, cy + y * scale) for x, y in pts]


@REG.add('castle_coffin', sample=True)
def _():
    k_wood = M('coffin_wood', lambda: wood_mat('coffin_wood', '#5e2f4c', '#4c243e'))
    k_lid = M('coffin_lid', lambda: wood_mat('coffin_lid', '#6d3659', '#5a2b4a'))
    kg = gold_mat('coffin_gold')
    objs = [L.prism('body', coffin_outline(0.97), 0.0, 0.25, k_wood, bev=0.018, seg=2)]
    objs.append(L.prism('lid', coffin_outline(1.0), 0.245, 0.31, k_lid, bev=0.022, seg=3))
    objs.append(L.prism('panel', coffin_outline(0.78), 0.30, 0.335, k_wood, bev=0.012, seg=2))
    # gold trim along the lid's lower edge and handles on the long sides
    ring = coffin_outline(1.005)
    objs.append(L.tube('trim', [(x, y, 0.248) for x, y in ring + ring[:1]], 0.009, kg, segs=6, caps=False))
    for hx in (-0.22, 0.05):
        for sy in (-1, 1):
            yy = 0.5 + sy * (0.15 + (0.24 - 0.15) * (hx + 0.43) / 0.59 + 0.012)
            objs.append(L.tube('handle', [(0.5 + hx - 0.05, yy, 0.15), (0.5 + hx - 0.04, yy + sy * 0.02, 0.12),
                                          (0.5 + hx + 0.04, yy + sy * 0.02, 0.12), (0.5 + hx + 0.05, yy, 0.15)],
                               0.009, kg, segs=6))
    # a little gold bat on the lid
    # (wings along the lid's width, head towards the head end)
    bat = [(0.0, 0.035), (0.018, 0.05), (0.028, 0.02), (0.06, 0.045), (0.11, 0.05), (0.13, 0.0),
           (0.10, -0.02), (0.07, -0.01), (0.045, -0.04), (0.02, -0.02), (0.0, -0.05),
           (-0.02, -0.02), (-0.045, -0.04), (-0.07, -0.01), (-0.10, -0.02), (-0.13, 0.0), (-0.11, 0.05),
           (-0.06, 0.045), (-0.028, 0.02), (-0.018, 0.05)]
    bx, by = 0.54, 0.5
    objs.append(L.prism('bat', [(bx + y * 1.25, by - x * 1.25) for x, y in bat], 0.33, 0.35, kg, bev=0.004,
                        seg=1))
    render_prop('castle_coffin', objs, 0.35)


def wood_mat(key, c1, c2):
    a, b = lin(c1), lin(c2)

    def build(nt, neutral):
        g = L.G(nt, neutral)
        p = g.pos()
        x, y, z = g.sep(p)
        # grain along X: stretched noise
        v = g.comb(g.m('MULTIPLY', x, 3.0), g.m('MULTIPLY', y, 40.0), g.m('MULTIPLY', z, 40.0))
        n = g.noise(v, 1.0, 3.0, 0.6)
        col = g.mix(g.mapr(n, 0.35, 0.65), a, b)
        return g.bsdf(col, 0.45, 0.45, 0.0, g.bump(n, 0.12, 0.01), clearcoat=0.3, cc_rough=0.25)

    return C.mat(key, build=build)


@REG.add('castle_roses', sample=True)
def _():
    k_pot = pm('planter', '#c9bce4', rough=0.8, spec=0.3, col2='#b5a6d2', noise=1, noise_scale=8)
    k_leaf = M('leaf', lambda: L.simple_mat('leaf', '#46907a', rough=0.6, spec=0.35, col2='#336e5c', noise=1,
                                             noise_scale=14, bump=0.3, bump_scale=40))
    k_rose = pm('rose', '#d66cff', rough=0.45, spec=0.4, col2='#b24ee6', noise=1, noise_scale=40)
    k_rose2 = pm('rose2', '#e0407a', rough=0.45, spec=0.4)
    k_rose_dk = pm('rose_dk', '#6a2a90', rough=0.5)
    cx, cy = 0.5, 0.5
    objs = [L.lathe('planter', [(0, 0), (0.17, 0), (0.17, 0.04), (0.14, 0.06), (0.16, 0.20), (0.24, 0.30),
                                (0.25, 0.33), (0.22, 0.34), (0, 0.34)], k_pot, c=(cx, cy, 0), segs=20)]
    rng = L.rng_for('roses')
    # the bush: overlapping leafy balls
    balls = [(0.0, 0.0, 0.52, 0.22)]
    for i in range(10):
        a = rng.uniform(0, 2 * math.pi)
        r = rng.uniform(0.08, 0.17)
        balls.append((r * math.cos(a), r * math.sin(a), rng.uniform(0.42, 0.66), rng.uniform(0.10, 0.14)))
    for bx, by, bz, br in balls:
        ob = L.sphere('leaf', (cx + bx, cy + by, bz), br, k_leaf, segs=12, rings=8)
        disp = ob.modifiers.new('d', 'DISPLACE')
        tx = __import__('bpy').data.textures.new('leafn', 'CLOUDS')
        tx.noise_scale = 0.06
        disp.texture = tx
        disp.strength = 0.05
        disp.texture_coords = 'GLOBAL'
        L.subsurf(ob, 1)
        objs.append(ob)
    # roses on the outside of the bush, more of them on the camera side
    for i in range(17):
        a = rng.uniform(-math.pi * 0.95, math.pi * 0.35) - math.pi / 2 + 0.3
        el = rng.uniform(0.1, 0.9)
        r = 0.25
        px = cx + r * math.cos(a) * math.cos(el)
        py = cy + r * math.sin(a) * math.cos(el)
        pz = 0.52 + 0.22 * math.sin(el)
        kk = k_rose2 if i % 5 == 2 else k_rose
        objs.append(L.sphere('rose', (px, py, pz), 0.056, kk, scale=(1, 1, 0.8), segs=12, rings=8))
        objs.append(L.sphere('rosec', (px + 0.0, py - 0.012, pz + 0.03), 0.02, k_rose_dk, segs=8, rings=6))
    render_prop('castle_roses', objs, 0.80)


# ===========================================================================
# phase 2: the rest of the castle blocks, fills and bridges
# ===========================================================================
P_WOOD = '#7a5446'
P_WOOD_TONES = ['#7a5446', '#6e4b3f', '#84604f', '#72503f', '#7d5a4c']


@cached
def tex_woodfloor_top(variant):
    """Dark planks along X, 8 to a cell, end joints staggered (periodic 1 m)."""
    P = Paint(0, 1, 0, 1)
    pw = 0.125
    row = np.floor(P.Y / pw).astype(int)
    fv = (P.Y / pw) % 1.0
    ends = {r: ((0.13 + 0.37 * r) % 1.0) for r in range(8)}        # one butt joint per row, shared
    col = np.zeros((P.h, P.w, 3), np.float32)
    du = np.full(P.X.shape, 1e9, np.float32)
    for r in range(8):
        sel = row == r
        e = ends[r]
        side = (P.X >= e)
        # the plank left of the joint and the plank right of it
        for s, t in ((0, P_WOOD_TONES[(r * 3 + 1) % 5]), (1, P_WOOD_TONES[(r * 2 + 3) % 5])):
            col[sel & (side == bool(s))] = lina(t)
        d = np.abs(P.X - e)
        du = np.where(sel, np.minimum(d, 1 - d), du)
    grain = P.noise(0.006, 1100, aniso=(0.08, 1.0))
    col *= (1 + 0.09 * grain + 0.04 * P.noise(0.1, 1101 + variant) * interior(P, 0.05, 0.1))[..., None]
    dv = np.minimum(fv, 1 - fv) * pw
    gap = np.maximum(P.cover(dv - 0.0045, 0.003), P.cover(du - 0.004, 0.003))
    col = blend(col, gap, lina('#2e1f22'))
    h = 0.5 + 0.08 * grain - 0.4 * gap
    # nails at the butt joints
    for r in range(8):
        for dx in (-0.02, 0.02):
            m = P.cover(P.sd_circle(ends[r] + dx, (r + 0.5) * pw, 0.006))
            col = blend(col, m * 0.8, lina('#3a3236'))
    rng = L.rng_for('woodfloor', variant)
    if variant == 1:
        # a dark knot and a scuff
        m = P.cover(P.sd_ellipse(0.62, 0.32, 0.03, 0.02), 0.004)
        col = blend(col, m * 0.8, lina('#4a3230'))
        sd = P.sd_polyline(L.crack_path(rng, (0.2, 0.7), (0.45, 0.66), 3, 0.01), 0.012)
        col = blend(col, P.cover(sd, 0.004) * 0.35, lina('#a07a66'))
    if variant == 2:
        # a spilt wax drop from a candle
        m = P.cover(P.sd_ellipse(0.40, 0.45, 0.035, 0.025), 0.004)
        col = blend(col, m, lina('#efe6d2'))
    col *= edge_shade(P, 0.02, 0.22)
    return col, h


@cached
def tex_woodfloor_side():
    P = Paint(0, 1, -FM, 0)
    col, h = tex_found_side()
    col, h = col.copy(), h.copy()
    m = P.cover(-0.05 - P.Y, 0.003)
    col = blend(col, m, lina('#5e4034') * (1 + 0.08 * P.noise(0.006, 1110, aniso=(0.1, 1.0)))[..., None])
    col = blend(col, P.cover(np.abs(P.Y + 0.05) - 0.004, 0.003) * 0.7, lina('#2e1f22'))
    h = np.where(m > 0.5, 0.55, h)
    return col, h


for v in range(3):
    @REG.add('castle_blk_woodfloor_v%d' % v)
    def _(v=v):
        k = mat_block('wood%d' % v, tex_pair('wood_top%d' % v, tex_woodfloor_top(v)),
                      tex_pair('wood_side', tex_woodfloor_side()), rough=0.55, spec=0.35, bump=0.3,
                      bump_dist=0.01, clearcoat=0.25)
        render_tile('castle_blk_woodfloor_v%d' % v, [block_box(k, bev=0.006)])


RUG_SCHEMES = [('#b8265f', '#f5c451', '#6d1a44'), ('#6c3fb8', '#f5c451', '#b8265f'), ('#3f3a9a', '#e5b54a', '#c93a6e')]


@cached
def tex_rug(variant):
    R = 0.43
    P = Paint(-R, R, -R, R, px_per_m=320)
    field, gold, acc = [lina(c) for c in RUG_SCHEMES[variant]]
    r = np.hypot(P.X, P.Y)
    a = np.arctan2(P.Y, P.X)
    col = P.full(field) * (1 + 0.04 * P.noise(0.008, 1120))[..., None]
    # rings: gold border, an inner scalloped ring, a star medallion
    col = blend(col, P.cover(np.abs(r - 0.37) - 0.028, 0.003), gold)
    col = blend(col, P.cover(np.abs(r - 0.37) - 0.008, 0.003), acc)
    scal = 0.26 + 0.025 * np.cos(8 * a)
    col = blend(col, P.cover(np.abs(r - scal) - 0.012, 0.003), gold)
    star = r - (0.12 + 0.05 * np.cos(5 * a))
    col = blend(col, P.cover(star, 0.003), acc)
    col = blend(col, P.cover(r - 0.04, 0.003), gold)
    # fringe dots all round
    fr = P.cover(np.abs(r - 0.415) - 0.012, 0.003) * (np.cos(a * 60) > 0)
    col = blend(col, fr, gold * 0.9)
    alpha = P.cover(r - 0.428, 0.003)
    return col, alpha


def rug_block(v):
    objs = flag_block(0)
    col, alpha = tex_rug(v)
    k = M('rug%d' % v, lambda: L.tex_mat('rug%d' % v, L.image('rug%d_c' % v, col), 'xy', rough=0.95, spec=0.15,
                                         scale=(0.86, 0.86), offset=(0.07, 0.07), ext='EXTEND',
                                         aimg=L.image('rug%d_a' % v, alpha, data=True)))
    rug = L.cyl('rug', (0.5, 0.5), 0.43, 0.0, 0.012, k, segs=48)
    objs.append(rug)
    return objs


for v in range(3):
    @REG.add('castle_blk_rug_v%d' % v)
    def _(v=v):
        render_tile('castle_blk_rug_v%d' % v, rug_block(v))


@cached
def tex_rock_side():
    """Natural rock in faceted chunks (periodic Voronoi): each chunk a tone,
    lit on its upper left, dark cracks between."""
    P = Paint(0, 1, -FM, 0)
    rng = L.rng_for('rock')
    seeds = [(rng.uniform(0, 1), rng.uniform(-FM, 0)) for _ in range(16)]
    W, Hh = 1.0, FM
    d1 = np.full(P.X.shape, 1e9, np.float32)
    d2 = np.full(P.X.shape, 1e9, np.float32)
    idx = np.zeros(P.X.shape, int)
    sx = np.zeros(P.X.shape, np.float32)
    sy = np.zeros(P.X.shape, np.float32)
    for i, (x, y) in enumerate(seeds):
        for du in (-W, 0, W):
            for dv in (-Hh, 0, Hh):
                d = np.hypot(P.X - x - du, (P.Y - y - dv) * 1.3)
                closer = d < d1
                d2 = np.where(closer, d1, np.minimum(d2, d))
                idx = np.where(closer, i, idx)
                sx = np.where(closer, x + du, sx)
                sy = np.where(closer, y + dv, sy)
                d1 = np.where(closer, d, d1)
    tones = ['#8f84a8', '#83789c', '#9a8fb2', '#7c7294', '#8a7fa2']
    col = np.zeros((P.h, P.w, 3), np.float32)
    for i in range(len(seeds)):
        col[idx == i] = lina(tones[i % len(tones)]) * rng.uniform(0.92, 1.06)
    # facet shading: lighter towards the chunk's upper left
    sh = np.clip(((P.Y - sy) - 0.5 * (P.X - sx)) / 0.12, -1, 1)
    col *= (1 + 0.13 * sh)[..., None]
    col *= (1 + 0.05 * P.noise(0.012, 1130))[..., None]
    crack = P.cover((d2 - d1) - 0.012, 0.004)
    col = blend(col, crack, lina('#3e3550'))
    h = 0.5 + 0.2 * sh - 0.4 * crack
    col *= side_shade(P, True, 0.06)
    return col, h


@REG.add('castle_fill_rock')
def _():
    col, h = tex_rock_side()
    k = mat_block('rock', tex_pair('rock_top', (col[:256 if col.shape[0] > 256 else None], h)),
                  tex_pair('rock_side', (col, h)), rough=0.85, spec=0.25, bump=0.5, bump_dist=0.015)
    render_tile('castle_fill_rock', [block_box(k, bev=0.01)])


# bridges: a drawbridge-style plank deck with chains ------------------------------

def wood_bridge(axis):
    kw = pm('plank', '#7d5a48', rough=0.7, spec=0.3, col2='#6a4a3c', noise=1, noise_scale=14, obj=True)
    kw2 = pm('plank2', '#8a6552', rough=0.7, spec=0.3, col2='#73513f', noise=1, noise_scale=14, obj=True)
    ki = pm('biron', '#34303e', rough=0.45, spec=0.5, metal=0.4)
    kc = pm('bchain', '#8d8a99', rough=0.35, spec=0.6, metal=0.6)
    objs = []
    y0, y1 = 0.07, 0.93
    # stringers along the walking direction, the cell long (decks join)
    for yy in (0.20, 0.80):
        objs.append(L.box('stringer', 0.0, yy - 0.04, -0.15, 1.0, yy + 0.04, -0.05, kw2))
    # planks across it, 8 to a cell, a hair apart
    for i in range(8):
        x = i * 0.125
        kk = kw if i % 2 else kw2
        objs.append(L.box('plank', x + 0.006, y0, -0.055, x + 0.119, y1, 0.0, kk, bev=0.006))
    # iron straps with rivets
    for yy in (0.20, 0.80):
        objs.append(L.box('strap', 0.0, yy - 0.025, 0.0, 1.0, yy + 0.025, 0.006, ki))
    # a post at each end of each side and a chain between them (a rail)
    for yy in (y0 - 0.005, y1 + 0.005):
        for x in (0.06, 0.94):
            objs.append(L.box('post', x - 0.035, yy - 0.035, -0.05, x + 0.035, yy + 0.035, 0.36, ki, bev=0.008))
            objs.append(L.sphere('knob', (x, yy, 0.38), 0.035, ki, segs=10, rings=6))
        n = 14
        for i in range(n + 1):
            t = i / n
            x = 0.06 + 0.88 * t
            z = 0.33 - 0.12 * math.sin(math.pi * t)
            objs.append(L.sphere('link', (x, yy, z), 0.02, kc, scale=(1.5, 0.7, 1.0) if i % 2 else (1.5, 1.0, 0.7),
                                 segs=8, rings=5))
    if axis == 'y':
        K.rot_z(objs, 90)
    return objs


for ax in ('x', 'y'):
    @REG.add('castle_bridge_%s' % ax)
    def _(ax=ax):
        objs = wood_bridge(ax)
        C.render_sprite(A.out, 'castle_bridge_%s' % ax, objs, C.cell(0, 0, 0), passes=('color', 'z', 'shadow'),
                        samples=A.samples, shadow_z=-0.18, kind='bridge',
                        extra={'walk': ax, 'deck_z': 0.0, 'height_m': 0.41})
        C.remove(objs)


# ===========================================================================
# phase 2: castle props
# ===========================================================================

def stone_mat(key='gstone', col='#a99fc0', col2='#968cb0'):
    return pm(key, col, rough=0.8, spec=0.3, col2=col2, noise=1, noise_scale=10, bump=0.2, bump_scale=30)


@REG.add('castle_gargoyle')
def _():
    ks = stone_mat()
    kp = stone_mat('plinth', '#c3b6dc', '#b2a4d0')
    ke = M('garg_eye', lambda: L.simple_mat('garg_eye', '#ff5a5a', rough=0.3, emit='#ff3040', emit_strength=5))
    kd = pm('garg_dk', '#5c5470', rough=0.8)
    objs = [L.box('plinth', 0.20, 0.20, 0.0, 0.80, 0.80, 0.10, kp, bev=0.015),
            L.box('plinth2', 0.25, 0.25, 0.10, 0.75, 0.75, 0.52, kp, bev=0.015),
            L.box('plinth3', 0.21, 0.21, 0.52, 0.79, 0.79, 0.60, kp, bev=0.015)]
    cx, cy, z = 0.5, 0.52, 0.60
    # a squat crouching body, big head, folded wings, clawed feet
    body = L.sphere('body', (cx, cy + 0.02, z + 0.20), 0.2, ks, scale=(1.0, 0.85, 1.05))
    head = L.sphere('head', (cx, cy - 0.07, z + 0.50), 0.16, ks, scale=(1.05, 0.95, 0.92))
    snout = L.sphere('snout', (cx, cy - 0.2, z + 0.45), 0.075, ks, scale=(1.2, 1.0, 0.8))
    objs += [body, head, snout]
    for sx in (-1, 1):
        objs.append(L.tube('horn', [(cx + sx * 0.09, cy - 0.06, z + 0.60), (cx + sx * 0.14, cy - 0.02, z + 0.70),
                                    (cx + sx * 0.13, cy + 0.04, z + 0.76)], [0.035, 0.022, 0.004], ks, segs=8))
        objs.append(L.sphere('ear', (cx + sx * 0.16, cy - 0.04, z + 0.53), 0.05, ks, scale=(0.5, 0.8, 1.3)))
        eye = L.sphere('eye', (cx + sx * 0.06, cy - 0.205, z + 0.52), 0.028, ke, scale=(1.0, 0.6, 0.8))
        eye.visible_diffuse = False
        objs.append(eye)
        objs.append(L.sphere('nostril', (cx + sx * 0.025, cy - 0.27, z + 0.45), 0.012, kd))
        # arms resting on the knees, feet with claws on the plinth's edge
        objs.append(L.tube('arm', [(cx + sx * 0.15, cy - 0.02, z + 0.32), (cx + sx * 0.17, cy - 0.14, z + 0.18),
                                   (cx + sx * 0.10, cy - 0.20, z + 0.12)], [0.05, 0.045, 0.04], ks, segs=10))
        objs.append(L.sphere('foot', (cx + sx * 0.10, cy - 0.17, z + 0.04), 0.07, ks, scale=(1.0, 1.3, 0.6)))
        for k in (-1, 0, 1):
            objs.append(L.sphere('claw', (cx + sx * 0.10 + k * 0.03, cy - 0.255, z + 0.03), 0.018, kd))
        # folded wings: a flat membrane, up and back over the shoulder
        wing = [(0.0, 0.0), (0.22, 0.10), (0.30, 0.34), (0.20, 0.30), (0.16, 0.20), (0.07, 0.16)]
        pts = [(cx + sx * (0.10 + a * 0.6), cy + 0.12 + a * 0.35, z + 0.25 + b) for a, b in wing]
        w = C.mesh_object('wing', pts, [tuple(range(len(pts)))], ks)
        sol = w.modifiers.new('sol', 'SOLIDIFY')
        sol.thickness = 0.03
        objs.append(w)
    objs.append(L.tube('tail', [(cx, cy + 0.18, z + 0.06), (cx + 0.12, cy + 0.2, z + 0.02), (cx + 0.2, cy + 0.08, z + 0.02),
                                (cx + 0.24, cy - 0.02, z + 0.05)], [0.04, 0.03, 0.02, 0.008], ks, segs=8))
    render_prop('castle_gargoyle', objs, 1.36)


@cached
def tex_banner():
    W, H = 0.50, 0.95
    P = Paint(0, W, 0, H, px_per_m=320)
    col = P.full(lina('#a8245a')) * (1 + 0.05 * P.noise(0.01, 1200))[..., None]
    gold = lina('#f2c14e')
    edge = np.minimum(np.minimum(P.X, W - P.X), H - P.Y)
    col = blend(col, P.cover(np.abs(edge - 0.035) - 0.012, 0.003), gold)
    # a gold bat in the middle
    bx, by = W / 2, H * 0.6
    bat = [(0.0, 0.03), (0.018, 0.05), (0.028, 0.02), (0.06, 0.045), (0.11, 0.05), (0.13, 0.0), (0.10, -0.02),
           (0.07, -0.01), (0.045, -0.04), (0.02, -0.02), (0.0, -0.05), (-0.02, -0.02), (-0.045, -0.04),
           (-0.07, -0.01), (-0.10, -0.02), (-0.13, 0.0), (-0.11, 0.05), (-0.06, 0.045), (-0.028, 0.02),
           (-0.018, 0.05)]
    sd = P.sd_poly([(bx + x * 1.4, by + y * 1.4) for x, y in bat])
    col = blend(col, P.cover(sd, 0.003), gold)
    col = blend(col, P.cover(np.abs(P.sd_circle(bx, by, 0.15)) - 0.008, 0.003), gold)
    return col


@REG.add('castle_banner')
def _():
    kp = pm('bpole', '#3a3346', rough=0.4, spec=0.5, metal=0.4)
    kg = pm('bgold', '#e2ab45', rough=0.3, spec=0.8, metal=0.35)
    kb = M('banner', lambda: L.tex_mat('banner', L.image('banner_c', tex_banner()), 'xz', rough=0.8, spec=0.2,
                                       scale=(0.50, 0.95), offset=(0.25, 0.80), obj=True, ext='EXTEND'))
    objs = [L.lathe('foot', [(0, 0), (0.2, 0), (0.2, 0.04), (0.12, 0.07), (0.05, 0.12), (0, 0.12)], kp, segs=16)]
    objs.append(L.cyl('pole', (0.5, 0.52), 0.022, 0.1, 1.95, kp, segs=10))
    objs.append(L.sphere('finial', (0.5, 0.52, 1.99), 0.04, kg))
    objs.append(L.tube('bar', [(0.22, 0.50, 1.80), (0.78, 0.50, 1.80)], 0.016, kp, segs=8))
    for x in (0.22, 0.78):
        objs.append(L.sphere('barknob', (x, 0.50, 1.80), 0.028, kg))
    # the cloth: a gently waving sheet with a swallowtail
    import bmesh
    bm = bmesh.new()
    nx, nz = 10, 18
    W, H = 0.50, 0.95
    grid = []
    for j in range(nz + 1):
        row = []
        for i in range(nx + 1):
            u, v = i / nx, j / nz
            x = 0.25 + W * u
            z = 1.78 - H * (1 - v)
            if v < 0.18:                     # the swallowtail notch at the bottom
                cut = (0.18 - v) / 0.18 * 0.5
                z += 0.0 if abs(u - 0.5) > cut * 0.5 else (0.18 - v) * H * (1 - abs(u - 0.5) / (cut * 0.5 + 1e-6)) * 0.9
            y = 0.47 + 0.018 * math.sin(u * math.pi * 2.2 + v * 1.5) * (1 - v * 0.5)
            row.append(bm.verts.new((x, y, z)))
        grid.append(row)
    for j in range(nz):
        for i in range(nx):
            bm.faces.new((grid[j][i], grid[j][i + 1], grid[j + 1][i + 1], grid[j + 1][i]))
    cloth = L.obj_from_bm('cloth', bm, kb, smooth=True, auto=0)
    sol = cloth.modifiers.new('sol', 'SOLIDIFY')
    sol.thickness = 0.01
    objs.append(cloth)
    objs.append(L.tube('rod', [(0.24, 0.47, 1.78), (0.76, 0.47, 1.78)], 0.012, kg, segs=8))
    render_prop('castle_banner', objs, 2.03)


@REG.add('castle_throne')
def _():
    kw = M('throne_wood', lambda: wood_mat('throne_wood', '#4e2a44', '#3e2036'))
    kc = pm('cushion', '#c32a5e', rough=0.9, spec=0.2, col2='#a8224f', noise=1, noise_scale=20)
    kg = gold_mat('throne_gold')
    objs = []
    x0, x1, y0, y1 = 0.14, 0.86, 0.18, 0.80
    for x in (x0, x1 - 0.07):
        for y in (y0, y1 - 0.07):
            objs.append(L.box('leg', x, y, 0.0, x + 0.07, y + 0.07, 0.42, kw, bev=0.01))
            objs.append(L.sphere('foot', (x + 0.035, y + 0.035, 0.035), 0.045, kg))
    objs.append(L.box('seat', x0, y0, 0.36, x1, y1, 0.46, kw, bev=0.015))
    objs.append(L.box('cush', x0 + 0.06, y0 + 0.02, 0.46, x1 - 0.06, y1 - 0.08, 0.53, kc, bev=0.03, seg=3))
    # arms
    for x in (x0, x1 - 0.08):
        objs.append(L.box('arm', x, y0 + 0.02, 0.46, x + 0.08, y1 - 0.06, 0.70, kw, bev=0.012))
        objs.append(L.box('armcap', x - 0.01, y0 - 0.01, 0.70, x + 0.09, y1 - 0.05, 0.74, kg, bev=0.008))
        objs.append(L.sphere('knob', (x + 0.04, y0 + 0.01, 0.78), 0.04, kg))
    # the tall gothic back with a crimson panel and spikes
    back = L.arch_outline(0.5, x1 - x0, 0.46, 1.25, 1.62, 10)
    objs.append(L.prism('back', back, y1 - 0.08, y1, kw, axis='y', bev=0.01))
    panel = L.arch_outline(0.5, x1 - x0 - 0.16, 0.54, 1.20, 1.46, 10)
    objs.append(L.prism('panel', panel, y1 - 0.095, y1 - 0.08, kc, axis='y'))
    for x, h in ((x0 + 0.02, 1.34), (x1 - 0.02, 1.34), (0.5, 1.72)):
        objs.append(L.lathe('spike', [(0, h - 0.02), (0.035, h - 0.02), (0.0, h + 0.14)], kg, c=(x, y1 - 0.04, 0),
                            segs=10))
    objs.append(L.sphere('gem', (0.5, y1 - 0.1, 1.33), 0.04, M('gem', lambda: L.simple_mat(
        'gem', '#ff4f8f', rough=0.1, spec=0.9, emit='#ff2a70', emit_strength=1.2)), scale=(1, 0.5, 1.2)))
    render_prop('castle_throne', objs, 1.86)


BOOK_COLS = ['#b8265f', '#6c3fb8', '#3f8a8a', '#e2ab45', '#e6dcc4', '#8e1a4b', '#4f64e8', '#5a8a4a']


def bookshelf(axis):
    kw = M('shelf_wood', lambda: wood_mat('shelf_wood', '#5b3444', '#4a2a38'))
    kg = gold_mat('shelf_gold')
    rng = L.rng_for('books')
    objs = []
    x0, x1, y0, y1, H = 0.07, 0.93, 0.50, 0.88, 1.70
    t = 0.04
    objs.append(L.box('back', x0, y1 - t, 0.0, x1, y1, H, kw))
    objs.append(L.box('side_l', x0, y0, 0.0, x0 + t, y1, H, kw, bev=0.006))
    objs.append(L.box('side_r', x1 - t, y0, 0.0, x1, y1, H, kw, bev=0.006))
    objs.append(L.box('top', x0 - 0.02, y0 - 0.02, H, x1 + 0.02, y1 + 0.005, H + 0.06, kw, bev=0.01))
    objs.append(L.box('crest', 0.35, y1 - 0.05, H + 0.06, 0.65, y1 - 0.02, H + 0.16, kw, bev=0.01))
    shelves = [0.0, 0.42, 0.84, 1.26]
    for z in shelves:
        objs.append(L.box('shelf', x0 + t, y0, z, x1 - t, y1 - t, z + 0.04, kw, bev=0.004))
    objs.append(L.box('plinth', x0, y0 - 0.01, 0.0, x1, y0 + 0.03, 0.10, kw))
    for z in shelves:
        x = x0 + t + 0.01
        while x < x1 - t - 0.05:
            bw = rng.uniform(0.035, 0.07)
            bh = rng.uniform(0.25, 0.35)
            col = BOOK_COLS[rng.randrange(len(BOOK_COLS))]
            kb = pm('book_%s' % col.strip('#'), col, rough=0.6, spec=0.3)
            lean = rng.random() < 0.08
            if lean:
                objs.append(L.cbox('book', (x + 0.08, (y0 + y1 - t) / 2, z + 0.04 + bh * 0.35),
                                   (bw, 0.24, bh), kb, bev=0.006, ry=-35))
                x += 0.16
            else:
                objs.append(L.box('book', x, y0 + 0.05, z + 0.04, x + bw, y1 - t - 0.02, z + 0.04 + bh, kb, bev=0.005))
                objs.append(L.box('band', x - 0.001, y0 + 0.049, z + 0.04 + bh * 0.75, x + bw + 0.001, y0 + 0.052,
                                  z + 0.04 + bh * 0.8, kg))
                x += bw + 0.004
            if rng.random() < 0.12:
                x += 0.07
    if axis == 'y':
        K.rot_z(objs, 90)
    return objs


for ax in ('x', 'y'):
    @REG.add('castle_bookshelf_%s' % ax)
    def _(ax=ax):
        render_prop('castle_bookshelf_%s' % ax, bookshelf(ax), 1.86, extra={'faces': '-y' if ax == 'x' else '+x'})


def ironfence(axis):
    ki = pm('wiron', '#2e2a3a', rough=0.35, spec=0.6, metal=0.5)
    kg = gold_mat('fence_gold')
    objs = []
    for z in (0.10, 0.78):
        objs.append(L.box('rail', 0.0, 0.485, z, 1.0, 0.515, z + 0.03, ki))
    for i in range(10):
        x = 0.05 + i * 0.1
        objs.append(L.cyl('bar', (x, 0.5), 0.011, 0.0, 0.92, ki, segs=8))
        objs.append(L.lathe('tip', [(0, 0.90), (0.022, 0.92), (0.0, 1.0)], kg, c=(x, 0.5, 0), segs=8))
        # a curl between the bars at the bottom
        if i < 9:
            objs.append(L.tube('curl', [(x, 0.5, 0.13), (x + 0.03, 0.5, 0.21), (x + 0.07, 0.5, 0.21), (x + 0.1, 0.5, 0.13)],
                               0.007, ki, segs=5))
    objs.append(L.box('post', 0.46, 0.46, 0.0, 0.54, 0.54, 1.02, ki, bev=0.01))
    objs.append(L.sphere('ball', (0.5, 0.5, 1.07), 0.045, kg))
    if axis == 'y':
        K.rot_z(objs, 90)
    return objs


for ax in ('x', 'y'):
    @REG.add('castle_ironfence_%s' % ax)
    def _(ax=ax):
        render_prop('castle_ironfence_%s' % ax, ironfence(ax), 1.11, extra={'joins': ax})


@cached
def tex_clockface():
    R = 0.13
    P = Paint(-R, R, -R, R, px_per_m=600)
    r = np.hypot(P.X, P.Y)
    a = np.arctan2(P.Y, P.X)
    col = P.full(lina('#efe5cc'))
    col = blend(col, P.cover(np.abs(r - 0.115) - 0.01, 0.002), lina('#e2ab45'))
    ticks = (np.abs(((a / (2 * math.pi) * 12) % 1.0) - 0.5) > 0.44) & (r > 0.08) & (r < 0.1)
    col = np.where(ticks[..., None], lina('#2a2230'), col)
    # hands at a quarter to midnight (no numbers, it is a spooky clock)
    for ang, ln, w in ((math.radians(90 + 0), 0.085, 0.008), (math.radians(180), 0.06, 0.011)):
        sd = P.sd_seg((0, 0), (ln * math.cos(ang), ln * math.sin(ang))) - w / 2
        col = blend(col, P.cover(sd, 0.002), lina('#2a2230'))
    col = blend(col, P.cover(r - 0.012, 0.002), lina('#2a2230'))
    return col


@REG.add('castle_clock')
def _():
    kw = M('clock_wood', lambda: wood_mat('clock_wood', '#5e3048', '#4c263b'))
    kg = gold_mat('clock_gold')
    kgl = pm('clock_glass', '#302a44', rough=0.08, spec=0.8)
    # (the dial is built at the origin and turned up: its own x, y are the face)
    kf = M('clockface', lambda: L.tex_mat('clockface', L.image('clockface_c', tex_clockface()), 'xy', rough=0.4,
                                          scale=(0.26, 0.26), offset=(-0.13, -0.13), obj=True, ext='EXTEND'))
    x0, x1, y0, y1 = 0.25, 0.75, 0.35, 0.72
    objs = [L.box('base', x0 - 0.03, y0 - 0.03, 0.0, x1 + 0.03, y1 + 0.03, 0.30, kw, bev=0.012),
            L.box('trunk', x0 + 0.04, y0 + 0.02, 0.30, x1 - 0.04, y1, 1.30, kw, bev=0.01),
            L.box('waist', x0 - 0.01, y0 - 0.01, 1.28, x1 + 0.01, y1 + 0.01, 1.34, kw, bev=0.008),
            L.box('hood', x0 - 0.03, y0 - 0.02, 1.34, x1 + 0.03, y1 + 0.02, 1.78, kw, bev=0.012)]
    # the pendulum window and the brass bob
    objs.append(L.box('window', 0.37, y0 + 0.012, 0.52, 0.63, y0 + 0.02, 1.18, kgl))
    objs.append(L.box('rod', 0.495, y0 + 0.03, 0.72, 0.505, y0 + 0.035, 1.16, kg))
    objs.append(L.cyl('bob', (0.0, 0.0), 0.06, -0.01, 0.01, kg, segs=20))
    objs[-1].rotation_euler = (math.radians(90), 0, 0)
    objs[-1].location = (0.5, y0 + 0.035, 0.70)
    # the dial
    dial = L.cyl('dial', (0.0, 0.0), 0.13, -0.006, 0.006, kf, segs=32)
    dial.rotation_euler = (math.radians(90), 0, 0)
    dial.location = (0.5, y0 - 0.026, 1.56)
    objs.append(dial)
    # a pointed pediment and finials
    ped = [(x0 - 0.04, 1.78), (x1 + 0.04, 1.78), (0.5, 1.98)]
    objs.append(L.prism('ped', ped, y0 - 0.02, y1 + 0.02, kw, axis='y', bev=0.008))
    for x, z in ((x0 - 0.02, 1.80), (x1 + 0.02, 1.80), (0.5, 1.99)):
        objs.append(L.sphere('fin', (x, y0 + 0.1, z + 0.03), 0.035, kg))
    render_prop('castle_clock', objs, 2.03)


@REG.add('castle_chandelier_stand')
def _():
    kg = gold_mat()
    k_wax = pm('wax', '#f2e8d6', rough=0.5, spec=0.3)
    k_flame = M('flame', lambda: L.emit_mat('flame', '#ffb54f', 9.0))
    k_wick = pm('wick', '#2a2024', rough=0.8)
    cx, cy = 0.5, 0.5
    objs = [L.lathe('foot', [(0, 0.03), (0.22, 0.03), (0.22, 0.06), (0.15, 0.09), (0.08, 0.14), (0.04, 0.2),
                             (0, 0.2)], kg, c=(cx, cy, 0), segs=24)]
    for a in (90, 210, 330):
        r = math.radians(a)
        objs.append(L.sphere('claw', (cx + 0.2 * math.cos(r), cy + 0.2 * math.sin(r), 0.03), 0.035, kg))
    objs.append(L.lathe('stem', [(0, 0.19), (0.035, 0.19), (0.026, 0.60), (0.06, 0.64), (0.06, 0.68), (0.026, 0.72),
                                 (0.024, 1.60), (0, 1.60)], kg, c=(cx, cy, 0), segs=16))
    flames = []
    for tier, (zt, rr, n) in enumerate(((1.18, 0.26, 5), (1.45, 0.15, 3))):
        objs.append(L.lathe('ring', [(rr - 0.012, zt - 0.012), (rr + 0.012, zt - 0.012), (rr + 0.012, zt + 0.012),
                                     (rr - 0.012, zt + 0.012), (rr - 0.012, zt - 0.012)], kg, c=(cx, cy, 0), segs=32,
                            caps=False))
        for i in range(n):
            a = 2 * math.pi * i / n + (0.3 if tier else -0.2)
            px, py = cx + rr * math.cos(a), cy + rr * math.sin(a)
            objs.append(L.tube('spoke', [(cx, cy, zt - 0.06), (px, py, zt)], 0.01, kg, segs=6))
            objs.append(L.lathe('cup', [(0, zt - 0.01), (0.02, zt - 0.01), (0.04, zt + 0.02), (0, zt + 0.02)], kg,
                                c=(px, py, 0), segs=12))
            flames.append(candle(objs, px, py, zt + 0.015, 0.12, k_wax, k_flame, k_wick))
    flames.append(candle(objs, cx, cy, 1.60, 0.16, k_wax, k_flame, k_wick))
    # one soft cone for the pool (all the candles together), a few small lights on the tree itself
    pool = L.add_spot((cx, cy, 1.55), color=lin('#ffa84a'), power=170.0, cone_deg=84, blend=1.0, radius=0.2)
    lights = [pool] + [C.add_point_light(p, color=lin('#ffa84a'), power=1.2, radius=0.02) for p in flames[::2]]
    render_prop('castle_chandelier_stand', objs, 1.82, lights, glow_R=1.35, glow_c=(cx, cy), pool_lights=[pool],
                extra={'light_at': [cx, cy, 1.55]})


# ===========================================================================
# phase 2: the castle's moving things
# ===========================================================================

@cached
def tex_rune_side(bright):
    """Ashlar of the floating slab with glowing runes (emission) on each face."""
    P = Paint(0, 1, -FM, 0)
    col, h = ashlar(P, 1300, [[0.0, 0.5], [0.25, 0.75]])
    col *= 1.05
    # runes: small angular glyphs in the middle of each stone (abstract, no letters)
    m = np.zeros(P.X.shape, np.float32)
    rng = L.rng_for('runes')
    ch = FM / 2
    for r, J in enumerate([[0.0, 0.5], [0.25, 0.75]]):
        cy = P.v0 + (r + 0.5) * ch
        for j in J:
            cx = (j + 0.25) % 1.0
            pts = []
            for k in range(4):
                pts.append((cx + rng.uniform(-0.06, 0.06), cy + rng.uniform(-0.07, 0.07)))
            m = np.maximum(m, P.cover(P.sd_polyline(pts, 0.014), 0.003))
            m = np.maximum(m, P.cover(np.abs(P.sd_circle(cx, cy, 0.075)) - 0.006, 0.003) * 0.9)
    glow = lina('#b58cff') * (1.0 if bright else 0.45)
    col = blend(col, m, lina('#6a4aa8'))
    emit = np.clip(m, 0, 1)[..., None] * glow
    col *= side_shade(P, True, 0.10)
    return col, h, emit


@cached
def tex_rune_top(bright):
    P = Paint(0, 1, 0, 1)
    col, h = tex_stone_top(1)
    col = col.copy()
    r = np.hypot(P.X - 0.5, P.Y - 0.5)
    a = np.arctan2(P.Y - 0.5, P.X - 0.5)
    m = P.cover(np.abs(r - 0.30) - 0.008, 0.003)
    m = np.maximum(m, P.cover(np.abs(r - 0.22) - 0.006, 0.003))
    spokes = (np.abs(((a / (2 * math.pi) * 6) % 1.0) - 0.5) < 0.04) & (r > 0.22) & (r < 0.30)
    m = np.maximum(m, spokes.astype(np.float32))
    col = blend(col, m, lina('#7a5ab8'))
    emit = m[..., None] * lina('#b58cff') * (0.9 if bright else 0.35)
    return col, h, emit


for f in range(2):
    @REG.add('castle_platform_%02d' % f)
    def _(f=f):
        sc, sh, se = tex_rune_side(f == 1)
        tc, th, te = tex_rune_top(f == 1)
        k = mat_block('platform%d' % f, tex_pair('plat_top%d' % f, (tc, th), te),
                      tex_pair('plat_side%d' % f, (sc, sh), se), rough=0.8, spec=0.3, bump=0.35,
                      bump_dist=0.015, emit_strength=1.6)
        ob = block_box(k, bev=0.02)
        # a rough, slightly tapered underside hangs below the slab
        kr = M('plat_under', lambda: L.simple_mat('plat_under', '#8d82a8', rough=0.9, col2='#7a7096', noise=1,
                                                   noise_scale=8))
        under = L.lathe('under', [(0, -FM - 0.16), (0.2, -FM - 0.12), (0.42, -FM - 0.03), (0.45, -FM + 0.01),
                                  (0, -FM + 0.01)], kr, segs=8, sx=1.0, sy=1.0, phase=math.pi / 8)
        render_tile('castle_platform_%02d' % f, [ob, under], kind='dyn',
                    extra={'anim': 'pulse', 'frame': f, 'frames': 2, 'ms': 400, 'top_floor': 0,
                           'floating': True})


# spikes: a floor trap flush with the ground ------------------------------------

@cached
def tex_spike_plate():
    P = Paint(0, 1, 0, 1)
    col, h = tex_stone_top(2)
    col, h = col.copy(), h.copy()
    plate = P.sd_box(0.5, 0.5, 0.38, 0.38, 0.03)
    col = blend(col, P.cover(plate, 0.003), lina('#5a5868'))
    col = blend(col, P.cover(np.abs(plate + 0.02) - 0.02, 0.003), lina('#d8322f'))       # warning red rim
    stripes = ((((P.X + P.Y) / 0.06) % 1.0) < 0.5) & (np.abs(plate + 0.02) < 0.02)
    col = np.where(stripes[..., None], lina('#f0c040'), col)
    for i in range(3):
        for j in range(3):
            cx, cy = 0.28 + 0.22 * i, 0.28 + 0.22 * j
            col = blend(col, P.cover(P.sd_circle(cx, cy, 0.05), 0.003), lina('#141218'))
            col = blend(col, P.cover(np.abs(P.sd_circle(cx, cy, 0.055)) - 0.006, 0.003), lina('#8b8898'))
    h = np.where(P.cover(plate) > 0.5, 0.55, h)
    col *= edge_shade(P, 0.035, 0.26)
    return col, h


for f in range(3):
    @REG.add('castle_spikes_%02d' % f)
    def _(f=f):
        k = mat_block('spikeplate', tex_pair('spikeplate', tex_spike_plate()),
                      tex_pair('stone_side1', tex_stone_side(1)), rough=0.6, spec=0.4, bump=0.3, bump_dist=0.01)
        objs = [block_box(k, bev=0.008)]
        ksp = pm('spike', '#d9dce6', rough=0.18, spec=0.9, metal=0.85)
        hgt = [0.0, 0.07, 0.34][f]
        if hgt > 0:
            for i in range(3):
                for j in range(3):
                    cx, cy = 0.28 + 0.22 * i, 0.28 + 0.22 * j
                    objs.append(L.lathe('spike', [(0, -0.05), (0.042, -0.05), (0.036, hgt - 0.10 if hgt > 0.12 else -0.02),
                                                  (0.0, hgt)], ksp, c=(cx, cy, 0), segs=12, auto=30))
        render_tile('castle_spikes_%02d' % f, objs, kind='dyn',
                    extra={'anim': 'trap', 'frame': f, 'frames': 3, 'ms': 60, 'hazard': f == 2, 'height_m': hgt})


# gate: the exit, a portcullis in a stone gatehouse ----------------------------------

def castle_gate(f):
    kst = mat_block('gate_stone', tex_pair('stone_top0', tex_stone_top(0)), tex_pair('gatehouse_side', tex_gatehouse_side()),
                    z0=0.0, z1=2.8, rough=0.8, spec=0.3, bump=0.35, bump_dist=0.015)
    ki = pm('pc_iron', '#3a3446', rough=0.35, spec=0.6, metal=0.6)
    kc = M('merlon_cap', lambda: L.simple_mat('merlon_cap', P_CAP, rough=0.75, spec=0.3, col2='#c8bbe2',
                                               noise=1, noise_scale=12))
    y0, y1 = 0.28, 0.72
    ox0, ox1, zs, za = 0.24, 0.76, 1.08, 1.36          # the opening and its pointed arch
    house = L.box('house', 0.02, y0, 0.0, 0.98, y1, 2.55, kst)
    arch = L.arch_outline(0.5, ox1 - ox0, -0.05, zs, za, 12)
    cut = L.prism('cut', arch, y0 - 0.1, y1 + 0.1, None, axis='y')
    # the slot the portcullis rides up into
    slot = L.box('slot', ox0 - 0.03, 0.47, 0.5, ox1 + 0.03, 0.53, 2.54)
    L.cut(house, [cut, slot])
    L.bevel(house, 0.012)
    objs = [house]
    for x in (0.02, 0.98 - 0.26):
        objs.append(L.box('merlon', x, y0, 2.55, x + 0.26, y1, 2.80, kc, bev=0.015))
    # the portcullis: bars, cross bars, spiked feet; raised by frame
    lift = [0.0, 0.18, 0.42, 0.66, 0.88, 1.06, 1.16][f]
    grid = []
    for i in range(6):
        x = ox0 + 0.04 + (ox1 - ox0 - 0.08) * i / 5
        # (1.38 m long: fully raised, the top stays inside the gatehouse, which ends at 2.55)
        grid.append(L.box('bar', x - 0.016, 0.485, 0.06, x + 0.016, 0.515, 1.38, ki))
        grid.append(L.lathe('pt', [(0, 0.0), (0.018, 0.07), (0, 0.07)], ki, c=(x, 0.5, 0), segs=6))
    for z in (0.30, 0.62, 0.94, 1.26):
        grid.append(L.box('xbar', ox0 + 0.005, 0.48, z, ox1 - 0.005, 0.52, z + 0.035, ki))
    K.move(grid, (0, 0, lift))
    objs += grid
    lights = []
    if f == 6:
        # open: the arch lights up violet (a glowing rim) and the light pools in front
        kgl = M('pc_glow', lambda: L.emit_mat('pc_glow', '#9a5cff', 2.0))
        rim = [(x, 0.275, z) for x, z in L.arch_outline(0.5, ox1 - ox0 - 0.03, 0.0, zs, za - 0.015, 12)[2:]]
        rim = [(ox1 - 0.015, 0.275, 0.02)] + rim + [(ox0 + 0.015, 0.275, 0.02)]
        glow = L.tube('glow', rim, 0.022, kgl, segs=8)
        glow.visible_diffuse = False
        glow.visible_shadow = False
        objs.append(glow)
        # (a small pool on the threshold: the gatehouse's long shadow already fills the image)
        lights = [L.add_spot((0.5, 0.12, 1.15), color=lin('#c89bff'), power=45.0, cone_deg=44, blend=1.0)]
    return objs, lights


@cached
def tex_gatehouse_side():
    """Ashlar for the 2.8 m gatehouse: 11 courses."""
    P = Paint(0, 1, 0, 2.8)
    rows = []
    for r in range(11):
        rows.append([0.0, 0.5] if r % 2 == 0 else [0.25, 0.75])
    col, h = ashlar(P, 1400, rows)
    return col, h


for f in range(7):
    @REG.add('castle_gate_%02d' % f)
    def _(f=f):
        objs, lights = castle_gate(f)
        ex = {'anim': 'open', 'frame': f, 'frames': 7, 'ms': 100, 'exit': True}
        if lights:
            render_prop('castle_gate_%02d' % f, objs, 2.80, lights, glow_R=0.5, glow_c=(0.5, 0.12), kind='dyn', extra=ex)
        else:
            render_prop('castle_gate_%02d' % f, objs, 2.80, kind='dyn', extra=ex)


# crate: a heavy stone block with a carved rune, pushable --------------------------

@REG.add('castle_crate')
def _():
    sc, sh, se = tex_rune_side(False)
    tc, th, te = tex_rune_top(False)
    # sides/top textures span z in [0, FM] here (the crate stands on the floor)
    k = mat_block('crate_stone', tex_pair('crate_top', (tc, th), te), tex_pair('crate_side', (sc, sh), se),
                  z0=0.0, z1=FM, rough=0.8, spec=0.3, bump=0.4, bump_dist=0.015, emit_strength=1.2,
                  tint=lin('#ece4ff'))
    ob = L.box('crate', 0.03, 0.03, 0.0, 0.97, 0.97, FM, k, bev=0.035, seg=3)
    render_prop('castle_crate', [ob], FM, kind='dyn', extra={'pushable': True, 'top_floor': 1})


# the boss's coffin, opening ------------------------------------------------------

for f in range(5):
    @REG.add('castle_coffin_open_%02d' % f)
    def _(f=f):
        k_wood = M('bcoffin_wood', lambda: wood_mat('bcoffin_wood', '#4a2340', '#3a1a32'))
        k_lid = M('bcoffin_lid', lambda: wood_mat('bcoffin_lid', '#5c2b4f', '#4a2240'))
        kg = gold_mat('coffin_gold')
        kdais = stone_mat('dais', '#b9acd6', '#a898c8')
        glow = [0.0, 0.8, 1.4, 2.0, 2.6][f]
        kin = pm('coffin_in_%d' % f, '#b8264a', rough=0.7, spec=0.3, emit='#ff2a3a', emit_strength=glow)
        s = 1.08
        objs = [L.box('dais', 0.03, 0.14, 0.0, 0.97, 0.86, 0.08, kdais, bev=0.015),
                L.box('dais2', 0.08, 0.19, 0.08, 0.92, 0.81, 0.12, kdais, bev=0.01)]
        body = L.prism('body', coffin_outline(0.97 * s * 0.92), 0.12, 0.40, k_wood, bev=0.018, seg=2)
        inner = L.prism('inner', coffin_outline(0.80 * s * 0.92), 0.17, 0.45, None)
        L.cut(body, [inner])
        objs.append(body)
        objs.append(L.prism('lining', coffin_outline(0.80 * s * 0.92), 0.17, 0.175, kin))
        lining_w = L.prism('lining_w', coffin_outline(0.805 * s * 0.92), 0.17, 0.39, kin)
        L.cut(lining_w, [L.prism('lcut', coffin_outline(0.78 * s * 0.92), 0.16, 0.45, None)])
        objs.append(lining_w)
        # the lid slides towards the foot (-X) and twists off
        lid = [L.prism('lid', coffin_outline(s * 0.92), 0.395, 0.46, k_lid, bev=0.022, seg=3),
               L.prism('panel', coffin_outline(0.78 * s * 0.92), 0.455, 0.49, k_wood, bev=0.012, seg=2)]
        ring = coffin_outline(1.005 * s * 0.92)
        lid.append(L.tube('trim', [(x, y, 0.40) for x, y in ring + ring[:1]], 0.009, kg, segs=6, caps=False))
        bat = [(0.0, 0.035), (0.018, 0.05), (0.028, 0.02), (0.06, 0.045), (0.11, 0.05), (0.13, 0.0),
               (0.10, -0.02), (0.07, -0.01), (0.045, -0.04), (0.02, -0.02), (0.0, -0.05),
               (-0.02, -0.02), (-0.045, -0.04), (-0.07, -0.01), (-0.10, -0.02), (-0.13, 0.0), (-0.11, 0.05),
               (-0.06, 0.045), (-0.028, 0.02), (-0.018, 0.05)]
        lid.append(L.prism('bat', [(0.55 + y * 1.4, 0.5 - x * 1.4) for x, y in bat], 0.485, 0.50, kg, bev=0.004, seg=1))
        slide = [0.0, 0.10, 0.22, 0.34, 0.44][f]
        twist = [0, 0, 4, 9, 16][f]
        from mathutils import Matrix, Vector
        piv = Vector((0.5 - slide, 0.5, 0.45))
        K.transform(lid, Matrix.Translation(Vector((-slide, 0, 0))))
        K.transform(lid, Matrix.Translation(piv) @ Matrix.Rotation(math.radians(twist), 4, 'Z') @
                    Matrix.Rotation(math.radians(-twist * 0.5), 4, 'Y') @ Matrix.Translation(-piv))
        objs += lid
        for hx in (-0.22, 0.05):
            for sy in (-1, 1):
                yy = 0.5 + sy * (0.155 + 0.08 * (hx + 0.43) / 0.59 + 0.012)
                objs.append(L.tube('handle', [(0.5 + hx - 0.05, yy, 0.27), (0.5 + hx - 0.04, yy + sy * 0.02, 0.24),
                                              (0.5 + hx + 0.04, yy + sy * 0.02, 0.24), (0.5 + hx + 0.05, yy, 0.27)],
                                   0.009, kg, segs=6))
        ex = {'anim': 'open', 'frame': f, 'frames': 5, 'ms': 120, 'boss': 'count'}
        if f > 0:
            # red light pours out of the open end
            lt = C.add_point_light((0.62, 0.5, 0.36), color=lin('#ff3040'), power=6.0 + 9.0 * f, radius=0.08)
            render_prop('castle_coffin_open_%02d' % f, objs, 0.50, [lt], glow_R=1.1, glow_c=(0.55, 0.5),
                        kind='dyn', extra=ex)
        else:
            render_prop('castle_coffin_open_%02d' % f, objs, 0.50, kind='dyn', extra=ex)


K.main('castle')
