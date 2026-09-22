"""Monster Hop - city (Zombie Town, grey): blocks, fills, surfaces, bridges,
props and the zone's moving things, rendered to sprites. See SPEC.md 6.

    Blender -b -P city.py -- --out ../../assets/tiles_city [--sample] [--only a,b] [--samples N]

--sample renders the style sample only (phase 1); --only takes sprite names
(or fnmatch patterns, or the part after the zone prefix: --only lamp,blk_*).
Everything is built at cell (0, 0) floor 0 under C.reset('city').
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
from zones_cc_kit import (M, block_box, cached, draw_pebbles, edge_shade, interior, mat_block,  # noqa: E402
                          pm, render_prop, render_tile, side_shade, tex_pair, wrap_dots, SURF_T)

A, REG = K.setup('city')

# ---------------------------------------------------------------------------
# palette (sRGB): cool greys, sodium orange, a little teal
# ---------------------------------------------------------------------------
P_ASPH = '#454b57'
P_ASPH_SIDE = '#4c515b'
P_GRAVEL = '#86827b'
P_EARTH = '#655b54'
P_EARTH_DK = '#4c443f'
P_PEBBLE = '#8b847b'
P_LINE = '#ece6cf'
P_WALK = '#8f95a0'
P_WALK_DK = '#7f8590'
P_JOINT = '#5d6168'
P_CONC = '#979ba1'
P_BRICK = '#955b50'
P_BRICK2 = '#7f6660'
P_BRICK3 = '#a4675a'
P_MORTAR = '#a29d97'
P_COPING = '#b8babd'
P_GRAVEL_ROOF = '#767c86'
P_SEWER = '#3b5953'
P_SODIUM = '#ff9a3c'

# ---------------------------------------------------------------------------
# textures
# ---------------------------------------------------------------------------

@cached
def tex_earth_side(seed=11):
    """Earth with pebbles: tiles in u and in v (fills stack)."""
    P = Paint(0, 1, -FM, 0)
    rng = L.rng_for('earth', seed)
    col = P.full(lina(P_EARTH))
    n = P.noise(0.05, seed)
    n2 = P.noise(0.012, seed + 1)
    col *= (1 + 0.10 * n + 0.05 * n2)[..., None]
    # darker soil layers (periodic wavy bands)
    band = np.sin((P.Y / FM) * 2 * math.pi * 2 + 0.6 * P.noise(0.2, seed + 2))
    col = blend(col, np.clip(band - 0.55, 0, 1) * 0.5, lina(P_EARTH_DK))
    h = P.zeros()
    draw_pebbles(P, col, h, wrap_dots(P, rng, 26, 0.014, 0.03, True, True), P_PEBBLE, '#5a534c')
    return col, h


@cached
def tex_asphalt_side():
    P = Paint(0, 1, -FM, 0)
    ecol, eh = tex_earth_side()
    col = ecol.copy()
    h = eh.copy()
    wav = 0.008 * P.noise(0.08, 5)
    z_asph = -0.10 + wav
    z_grav = -0.165 + wav * 1.5
    rng = L.rng_for('gravel')
    g = P.full(lina(P_GRAVEL)) * (1 + 0.08 * P.noise(0.01, 6))[..., None]
    gh = P.zeros()
    draw_pebbles(P, g, gh, wrap_dots(P, rng, 40, 0.008, 0.016, True, False, -0.17, -0.10), '#a09a91',
                 '#6a655f')
    mg = P.cover(z_grav - P.Y, 0.004)
    col = blend(col, mg, g)
    h = np.where(mg > 0.5, gh, h)
    ma = P.cover(z_asph - P.Y, 0.004)
    a = P.full(lina(P_ASPH_SIDE)) * (1 + 0.06 * P.noise(0.012, 7))[..., None]
    col = blend(col, ma, a)
    h = np.where(ma > 0.5, 0.3 + 0.1 * P.noise(0.01, 8), h)
    col *= side_shade(P, top_line=True, k=0.10)
    return col, h


@cached
def tex_asphalt_top(variant, line=False, cross=False):
    P = Paint(0, 1, 0, 1)
    rng = L.rng_for('asphalt', variant)
    col = P.full(lina(P_ASPH))
    grain = P.noise(0.010, 100)            # shared by every variant: edges match
    low = P.noise(0.22, 200 + variant)
    inner = interior(P, 0.06, 0.15)
    col *= (1 + 0.055 * grain + 0.045 * low * inner)[..., None]
    h = 0.5 + 0.12 * grain
    # a few pale aggregate specks (shared)
    srng = L.rng_for('asph_specks')
    for _ in range(70):
        cx, cy = srng.uniform(0, 1), srng.uniform(0, 1)
        m = P.cover(P.sd_circle(cx, cy, srng.uniform(0.005, 0.009)))
        col = blend(col, m * 0.55, lina('#7d828b'))
    if variant == 0 and not line and not cross:
        # the common one: only a short hairline crack
        ck = L.crack_path(rng, (0.58, 0.62), (0.76, 0.40), 4, 0.025)
        m = P.cover(P.sd_polyline(ck, 0.014), 0.004)
        col = blend(col, m * 0.7, lina('#353940'))
        h -= 0.2 * m
    if variant == 1:
        # branching crack
        ck = L.crack_path(rng, (0.18, 0.72), (0.62, 0.40), 6, 0.04)
        m = P.cover(P.sd_polyline(ck, 0.017), 0.004)
        br = L.crack_path(rng, ck[3], (0.72, 0.78), 4, 0.03)
        m = np.maximum(m, P.cover(P.sd_polyline(br, 0.013), 0.004))
        col = blend(col, m * 0.85, lina('#30343b'))
        h -= 0.25 * m
    if variant == 2:
        # a repaired rectangular patch, a touch darker, with a tar seam
        sd = P.sd_box(0.40, 0.60, 0.20, 0.14, 0.015, rot=0.06)
        m = P.cover(sd, 0.004)
        col = blend(col, m, col * 0.86)
        rim = P.cover(np.abs(sd) - 0.006, 0.004)
        col = blend(col, rim * 0.6, lina('#3f434b'))
        h -= 0.08 * m
    if line:
        # dashed lane line along X through the middle (y = 0.5), worn paint
        wear = P.noise(0.02, 400 + variant)
        sd = P.sd_box(0.5, 0.5, 0.29, 0.042, 0.01)
        m = P.cover(sd, 0.004) * np.clip(1.25 - 0.35 * np.maximum(wear, 0), 0, 1)
        m *= np.clip(0.75 + 0.5 * P.noise(0.006, 401), 0, 1) * 0.35 + 0.65
        col = blend(col, m * 0.92, lina(P_LINE) * (1 + 0.04 * grain)[..., None])
        h += 0.06 * m
    if cross:
        # zebra bars running along Y (they join the next crosswalk cell in Y),
        # two per cell, kept 7 cm off the cell's X edges
        wear = P.noise(0.03, 410 + variant)
        m = np.zeros_like(P.X)
        for cx in (0.27, 0.73):
            m = np.maximum(m, P.cover(np.abs(P.X - cx) - 0.105, 0.004))
        m *= np.clip(1.3 - 0.3 * np.maximum(wear, 0), 0, 1)
        m *= np.clip(0.8 + 0.4 * P.noise(0.006, 411), 0, 1) * 0.3 + 0.7
        col = blend(col, np.clip(m, 0, 1) * 0.9, lina(P_LINE) * (1 + 0.04 * grain)[..., None])
        h += 0.06 * m
    col *= edge_shade(P)
    return col, h


@cached
def tex_walk_top(variant):
    """One big concrete paver per cell (the slab is its own object)."""
    P = Paint(0, 1, 0, 1)
    rng = L.rng_for('walk', variant)
    col = P.full(lina(P_WALK))
    grain = P.noise(0.008, 500)
    low = P.noise(0.18, 510 + variant)
    inner = interior(P, 0.05, 0.12)
    col *= (1 + 0.035 * grain + 0.05 * low * inner)[..., None]
    h = 0.5 + 0.1 * grain
    # a faint trowel border (scored line) near the slab's edge: every variant
    sd = np.abs(P.sd_box(0.5, 0.5, 0.405, 0.405, 0.02))
    m = P.cover(sd - 0.004, 0.004)
    col = blend(col, m * 0.35, lina('#8b8f96'))
    h -= 0.12 * m
    # speckles (shared)
    srng = L.rng_for('walk_specks')
    for _ in range(40):
        cx, cy = srng.uniform(0.08, 0.92), srng.uniform(0.08, 0.92)
        m = P.cover(P.sd_circle(cx, cy, srng.uniform(0.006, 0.011)))
        col = blend(col, m * 0.5, lina('#8e929a'))
    if variant == 0:
        # two old gum spots and a soft stain
        for cx, cy in ((0.30, 0.34), (0.66, 0.70)):
            m = P.cover(P.sd_circle(cx, cy, 0.022))
            col = blend(col, m * 0.7, lina('#80848c'))
        sd = P.sd_ellipse(0.62, 0.36, 0.14, 0.09) + 0.015 * P.noise(0.04, 520)
        col = blend(col, P.cover(sd, 0.04) * 0.35, lina('#8d9198'))
    if variant == 1:
        # a crack across a corner, a chipped corner
        ck = L.crack_path(rng, (0.20, 0.78), (0.72, 0.24), 6, 0.045)
        m = P.cover(P.sd_polyline(ck, 0.018), 0.004)
        col = blend(col, m * 0.85, lina('#53575e'))
        h -= 0.3 * m
    if variant == 2:
        # a little drain grate in the middle-back
        sd = P.sd_box(0.5, 0.62, 0.12, 0.08, 0.01)
        m = P.cover(sd)
        slots = (np.abs(((P.X - 0.5) / 0.04) % 1.0 - 0.5) < 0.22) & (np.abs(P.Y - 0.62) < 0.06)
        col = blend(col, m, lina('#5f646b'))
        col = blend(col, m * slots * 0.9, lina('#262a2f'))
        h -= 0.3 * m * slots
    col *= edge_shade(P, 0.03, 0.10)
    return col, h


@cached
def tex_walk_side():
    """Under the pavers: a concrete bed, then earth."""
    P = Paint(0, 1, -FM, 0)
    ecol, eh = tex_earth_side(13)
    col = ecol.copy()
    h = eh.copy()
    wav = 0.006 * P.noise(0.08, 21)
    mc = P.cover((-0.19 + wav) - P.Y, 0.004)
    cc = P.full(lina(P_CONC)) * (1 + 0.05 * P.noise(0.01, 22) + 0.04 * P.noise(0.1, 23))[..., None]
    col = blend(col, mc, cc)
    h = np.where(mc > 0.5, 0.45 + 0.08 * P.noise(0.01, 24), h)
    ms = P.cover(-0.078 - P.Y, 0.003)
    col = blend(col, ms, P.full(lina(P_WALK_DK)))
    # the slab's shadowed underside line
    ml = P.cover(np.abs(P.Y + 0.080) - 0.005, 0.003)
    col = blend(col, ml * 0.5, lina('#6a6e75'))
    col *= side_shade(P, True, 0.08)
    return col, h


@cached
def tex_walk_slab_side():
    P = Paint(0, 1, -0.08, 0)
    col = P.full(lina(P_WALK_DK)) * (1 + 0.04 * P.noise(0.01, 30))[..., None]
    return col, P.zeros() + 0.5


def brick_layout(P, seed, courses=4, blen=0.25, mortar=0.020, fixed_edge=True):
    """Running-bond bricks over the side texture (u in [0, 1], v = z over one
    floor). Returns (colour, height, brick_mask). Bricks that cross the cell's
    edge (u = 0 / 1) take a colour that does not depend on the variant."""
    rng = L.rng_for('brick', seed)
    ch = (P.v1 - P.v0) / courses
    row = np.floor((P.Y - P.v0) / ch).astype(int)
    off = np.where(row % 2 == 1, blen / 2.0, 0.0)
    uu = P.X - P.u0 + off
    col_i = np.floor(uu / blen).astype(int)
    nper = int(round(1.0 / blen))
    fu = uu / blen - np.floor(uu / blen)
    fv = (P.Y - P.v0) / ch - row
    du = np.minimum(fu, 1 - fu) * blen
    dv = np.minimum(fv, 1 - fv) * ch
    d = np.minimum(du, dv)
    mort = P.cover(d - mortar / 2.0, 0.004)          # 1 in the mortar
    # colours per brick
    pal = [lina(P_BRICK), lina(P_BRICK2), lina(P_BRICK3), lina(P_BRICK) * 0.88, lina('#8a5e57')]
    table = {}
    for r in range(courses):
        for c in range(nper):
            if (r % 2 == 1) and c == 0 and fixed_edge:
                # this brick straddles u = 0 / 1: same colour in every variant
                k = L.rng_for('brick_edge', r).randrange(len(pal))
                f = 1.0
            else:
                k = rng.randrange(len(pal))
                f = rng.uniform(0.94, 1.06)
            table[(r, c)] = pal[k] * f
    col = np.zeros((P.h, P.w, 3), np.float32)
    rr = row % courses
    cc = col_i % nper
    for (r, c), v in table.items():
        sel = (rr == r) & (cc == c)
        col[sel] = v
    # a little texture inside each brick, lighter top edge, darker lower edge
    col *= (1 + 0.07 * P.noise(0.012, seed + 40))[..., None]
    col *= (1 + 0.10 * np.clip(fv - 0.75, 0, 1) * 4 - 0.10 * np.clip(0.25 - fv, 0, 1) * 4)[..., None]
    mcol = P.full(lina(P_MORTAR)) * (1 + 0.05 * P.noise(0.01, seed + 41))[..., None]
    col = blend(col, mort, mcol)
    h = 0.6 - 0.45 * mort + 0.04 * P.noise(0.01, seed + 42)
    return col, h, 1 - mort


@cached
def tex_brick_side(variant, coping=True, win=False):
    P = Paint(0, 1, -FM, 0)
    col, h, _ = brick_layout(P, 60 + variant)
    if coping:
        # a concrete coping band at the top of the wall (the wall's lip)
        mc = P.cover(-0.058 - P.Y, 0.003)
        cc = P.full(lina(P_COPING)) * (1 + 0.04 * P.noise(0.01, 70))[..., None]
        col = blend(col, mc, cc)
        h = np.where(mc > 0.5, 0.7, h)
        ml = P.cover(np.abs(P.Y + 0.061) - 0.004, 0.003)
        col = blend(col, ml * 0.6, lina('#5a5552'))
    if win:
        # lintel (a soldier course) above the window and a stone sill below
        for cx in (0.5,):
            m = P.cover(P.sd_box(cx, -0.105, 0.235, 0.03, 0.004))
            col = blend(col, m, lina('#8b7a73') * (1 + 0.05 * P.noise(0.01, 71))[..., None])
            sl = P.cover(P.sd_box(cx, -0.425, 0.24, 0.022, 0.004))
            col = blend(col, sl, lina('#c3c0b8'))
    col *= side_shade(P, True, 0.10)
    return col, h


@cached
def tex_coping_top():
    P = Paint(0, 1, 0, 1)
    col = P.full(lina(P_COPING)) * (1 + 0.04 * P.noise(0.01, 80) + 0.03 * P.noise(0.15, 81))[..., None]
    col *= edge_shade(P, 0.03, 0.18)
    return col, 0.5 + 0.05 * P.noise(0.01, 82)


@cached
def tex_brick_top():
    """Bricks seen from above (a fill's top, hardly ever seen)."""
    P = Paint(0, 1, 0, 1)
    col, h, _ = brick_layout(P, 90, courses=8, blen=0.25)
    return col, h


@cached
def tex_roof_top(variant):
    P = Paint(0, 1, 0, 1)
    rng = L.rng_for('roof', variant)
    col = P.full(lina(P_GRAVEL_ROOF))
    col *= (1 + 0.05 * P.noise(0.2, 600 + variant) * interior(P, 0.05, 0.15))[..., None]
    h = P.zeros() + 0.3
    srng = L.rng_for('roof_gravel')
    tones = ['#9a9ea5', '#6a6e75', '#8d9097', '#a4a29c', '#72767e']
    for _ in range(360):
        cx, cy = srng.uniform(0, 1), srng.uniform(0, 1)
        r = srng.uniform(0.010, 0.018)
        sd = P.sd_ellipse(cx, cy, r, r * 0.8)
        m = P.cover(sd)
        t = tones[srng.randrange(len(tones))]
        col = blend(col, m * 0.85, lina(t))
        h = np.maximum(h, m * 0.7)
    if variant == 0:
        # a darker tar patch and a puddle with a sky tint
        sd = P.sd_ellipse(0.62, 0.40, 0.11, 0.07) + 0.015 * P.noise(0.05, 610)
        m = P.cover(sd, 0.01)
        col = blend(col, m * 0.6, lina('#66707b'))
        h = np.where(m > 0.5, 0.25, h)
    if variant == 1:
        for cx, cy in ((0.3, 0.3), (0.7, 0.65)):
            m = P.cover(P.sd_circle(cx, cy, 0.03))
            col = blend(col, m * 0.6, lina('#6e5f52'))
    col *= edge_shade(P, 0.025, 0.16)
    return col, h


@cached
def tex_roof_side():
    P = Paint(0, 1, -FM, 0)
    col, h, _ = brick_layout(P, 95)
    # parapet coping lip: a taller concrete band with a drip line
    mc = P.cover(-0.075 - P.Y, 0.003)
    cc = P.full(lina(P_COPING)) * (1 + 0.04 * P.noise(0.01, 96))[..., None]
    col = blend(col, mc, cc)
    h = np.where(mc > 0.5, 0.7, h)
    ml = P.cover(np.abs(P.Y + 0.079) - 0.005, 0.003)
    col = blend(col, ml * 0.7, lina('#56524f'))
    col *= side_shade(P, True, 0.10)
    return col, h


@cached
def tex_sewer_top(variant):
    """Murky grey-green water: slow swirls, pale ripple streaks along X (the
    current), a few scum flecks. The swirls and ripples are shared by the
    variants (periodic: a channel joins seamlessly); the flecks are not."""
    P = Paint(0, 1, 0, 1)
    col = P.full(lina(P_SEWER))
    sw = P.noise(0.16, 700)
    sw2 = P.noise(0.05, 701)
    col *= (1 + 0.06 * sw)[..., None]
    rip = P.zeros()
    rng = L.rng_for('sewer', variant)
    col = K.water_glints(P, rng, 3, col, '#6f9486', 0.8)
    inner = interior(P, 0.08, 0.1)
    for _ in range(3):
        cx, cy = rng.uniform(0.2, 0.8), rng.uniform(0.2, 0.8)
        sd = P.sd_ellipse(cx, cy, rng.uniform(0.03, 0.05), rng.uniform(0.018, 0.026))
        col = blend(col, P.cover(sd, 0.006) * 0.5 * inner, lina('#6c7f62'))
    h = 0.5 + 0.25 * rip + 0.08 * sw2
    return col, h


@cached
def tex_sewer_side():
    P = Paint(0, 1, -0.33, 0)
    col = P.full(lina('#3b574d')) * (1 + 0.05 * P.noise(0.05, 710))[..., None]
    return col, P.zeros() + 0.5


@cached
def tex_window_glass(variant=0):
    """Lit window: a warm sodium glow, a cross frame, a half-drawn blind."""
    P = Paint(0, 1, 0, 1, px_per_m=128)
    y = P.Y
    col = P.full(lina('#ffb14d'))
    col = blend(col, np.clip(1 - y, 0, 1) * 0.35, lina('#ff8a2a'))
    # the blind (upper part), with its pull
    mb = P.cover((0.62 - 0.04 * variant) - P.Y, 0.01)
    col = blend(col, mb * 0.85, lina('#e0843a'))
    stripes = (np.abs(((P.Y - 0.6) / 0.07) % 1.0 - 0.5) < 0.12) * mb
    col = blend(col, stripes * 0.4, lina('#b8612a'))
    # frame: a cross of mullions and the border
    fr = np.minimum(np.minimum(P.X, 1 - P.X), np.minimum(P.Y, 1 - P.Y))
    mf = P.cover(fr - 0.07, 0.01)
    mf = np.maximum(mf, P.cover(np.abs(P.X - 0.5) - 0.04, 0.01))
    mf = np.maximum(mf, P.cover(np.abs(P.Y - 0.45) - 0.035, 0.01))
    col = blend(col, mf, lina('#2e2622'))
    emit = col * (1 - mf[..., None]) * 1.0
    return col, emit


# ---------------------------------------------------------------------------
# blocks
# ---------------------------------------------------------------------------

for v in range(3):
    @REG.add('city_blk_asphalt_v%d' % v, sample=(v == 0))
    def _(v=v):
        k = mat_block('asph%d' % v, tex_pair('asph_top%d' % v, tex_asphalt_top(v)),
                      tex_pair('asph_side', tex_asphalt_side()), rough=0.85, spec=0.25, bump=0.15)
        render_tile('city_blk_asphalt_v%d' % v, [block_box(k)])

    @REG.add('city_blk_asphalt_line_v%d' % v, sample=(v == 0))
    def _(v=v):
        k = mat_block('asphl%d' % v, tex_pair('asphl_top%d' % v, tex_asphalt_top(v, True)),
                      tex_pair('asph_side', tex_asphalt_side()), rough=0.85, spec=0.25, bump=0.15)
        render_tile('city_blk_asphalt_line_v%d' % v, [block_box(k)])


def sidewalk(v):
    kb = mat_block('walkbase', K.flat_tex('joint', P_JOINT, 0.0),
                   tex_pair('walk_side', tex_walk_side()), rough=0.9, spec=0.2, bump=0.2)
    ks = mat_block('walk%d' % v, tex_pair('walk_top%d' % v, tex_walk_top(v)),
                   tex_pair('walk_slab_side', tex_walk_slab_side()), z0=-0.08, z1=0.0,
                   rough=0.8, spec=0.3, bump=0.2)
    base = L.box('base', 0, 0, -FM, 1, 1, -0.022, kb)
    g = 0.011
    slab = L.box('slab', g, g, -0.08, 1 - g, 1 - g, 0.0, ks, bev=0.011, seg=2)
    objs = [base, slab]
    if v == 1:
        # a weed tuft in the crack
        kw = M('weed', lambda: L.simple_mat('weed', '#7f8f6a', rough=0.8, col2='#5f7050', noise=1))
        rng = L.rng_for('weed')
        for i in range(7):
            a = rng.uniform(0, 2 * math.pi)
            r = rng.uniform(0.0, 0.03)
            cx, cy = 0.47 + r * math.cos(a), 0.50 + r * math.sin(a)
            tip = (cx + rng.uniform(-0.05, 0.05), cy + rng.uniform(-0.05, 0.05), rng.uniform(0.06, 0.11))
            objs.append(L.tube('blade', [(cx, cy, -0.005), ((cx + tip[0]) / 2, (cy + tip[1]) / 2, tip[2] * 0.6),
                                         tip], [0.011, 0.008, 0.002], kw, segs=5))
    return objs


for v in range(3):
    @REG.add('city_blk_sidewalk_v%d' % v, sample=(v < 2))
    def _(v=v):
        render_tile('city_blk_sidewalk_v%d' % v, sidewalk(v))


def brick_block(v, win=False, coping=True, top=None):
    side = tex_pair('brick_side%d%s%s' % (v, 'w' if win else '', 'c' if coping else ''),
                    tex_brick_side(v, coping, win))
    top = top or tex_pair('coping_top', tex_coping_top())
    k = mat_block('brick%d%s%s' % (v, 'w' if win else '', 'c' if coping else ''), top, side,
                  rough=0.85, spec=0.25, bump=0.35, bump_dist=0.015)
    wall = block_box(k, bev=0.0 if win else 0.008)
    objs = [wall]
    if win:
        # recessed windows on the front (-Y) and right (+X) faces, lit
        # (the right face is seen at a grazing angle, 20 px per metre: a deep
        # recess would hide its glass, so it is shallower there)
        ww, z0, z1, dep, dep_r = 0.44, -0.405, -0.13, 0.05, 0.015
        cut_f = L.box('cut_f', 0.5 - ww / 2, -0.1, z0, 0.5 + ww / 2, dep, z1)
        cut_r = L.box('cut_r', 1 - dep_r, 0.5 - ww / 2, z0, 1.1, 0.5 + ww / 2, z1)
        L.cut(wall, [cut_f, cut_r])
        L.bevel(wall, 0.008)       # after the cut
        gl_c, gl_e = tex_window_glass(0)
        img_c = L.image('winglass_c', gl_c)
        img_e = L.image('winglass_e', gl_e)
        kg = M('winglass_f', lambda: L.tex_mat('winglass_f', img_c, 'xz', rough=0.25, spec=0.6, eimg=img_e,
                                                emit_strength=2.2, scale=(ww, z1 - z0),
                                                offset=(0.5 - ww / 2, z0)))
        kg2 = M('winglass_r', lambda: L.tex_mat('winglass_r', img_c, 'yz', rough=0.25, spec=0.6, eimg=img_e,
                                                 emit_strength=2.2, scale=(ww, z1 - z0),
                                                 offset=(0.5 - ww / 2, z0)))
        objs.append(L.box('glass_f', 0.5 - ww / 2, dep - 0.004, z0, 0.5 + ww / 2, dep, z1, kg))
        objs.append(L.box('glass_r', 1 - dep_r, 0.5 - ww / 2, z0, 1 - dep_r + 0.004, 0.5 + ww / 2, z1, kg2))
    return objs


for v in range(3):
    @REG.add('city_blk_brick_v%d' % v, sample=(v == 0))
    def _(v=v):
        render_tile('city_blk_brick_v%d' % v, brick_block(v))


@REG.add('city_blk_brick_win', sample=True)
def _():
    render_tile('city_blk_brick_win', brick_block(0, win=True, coping=False), extra={'lit': True})


for v in range(3):
    @REG.add('city_blk_roof_v%d' % v, sample=(v == 0))
    def _(v=v):
        k = mat_block('roof%d' % v, tex_pair('roof_top%d' % v, tex_roof_top(v)),
                      tex_pair('roof_side', tex_roof_side()), rough=0.9, spec=0.2, bump=0.4,
                      bump_dist=0.012)
        render_tile('city_blk_roof_v%d' % v, [block_box(k, bev=0.008)])


# fills ----------------------------------------------------------------------

@REG.add('city_fill_earth', sample=True)
def _():
    col, h = tex_earth_side()
    k = mat_block('earth', tex_pair('earth_top', (Paint(0, 1, 0, 1).full(lina(P_EARTH)),
                                                   np.zeros((256, 256)) + 0.5)),
                  tex_pair('earth_side', (col * side_shade(Paint(0, 1, -FM, 0), True, 0.08), h)),
                  rough=0.95, spec=0.15, bump=0.4, bump_dist=0.015)
    render_tile('city_fill_earth', [block_box(k, bev=0.006)])


@REG.add('city_fill_brick', sample=True)
def _():
    render_tile('city_fill_brick', brick_block(2, coping=False,
                                                     top=tex_pair('brick_top', tex_brick_top())))


# surfaces -------------------------------------------------------------------



for v in range(3):
    @REG.add('city_surf_sewer_v%d' % v, sample=(v == 0))
    def _(v=v):
        k = mat_block('sewer%d' % v, tex_pair('sewer_top%d' % v, tex_sewer_top(v)),
                      tex_pair('sewer_side', tex_sewer_side()), z0=-SURF_T, z1=0.0,
                      rough=0.10, spec=0.5, bump=0.05, bump_dist=0.02, clearcoat=0.6)
        ob = L.box('surf', 0, 0, -SURF_T, 1, 1, 0, k)
        C.render_sprite(A.out, 'city_surf_sewer_v%d' % v, [ob], C.cell(0, 0, 0), passes=('color', 'z'),
                        samples=A.samples, kind='surf', extra={'depth_m': 0.18})
        C.remove([ob])


# ---------------------------------------------------------------------------
# props
# ---------------------------------------------------------------------------

@REG.add('city_lamp', sample=True)
def _():
    k_pole = pm('lamp_pole', '#4d5f5c', rough=0.45, spec=0.5, col2='#3f4c4a', noise=1, noise_scale=6)
    k_rust = pm('lamp_rust', '#7d5a45', rough=0.8, spec=0.2)
    k_hood = pm('lamp_hood', '#5a6b67', rough=0.4, spec=0.5)
    k_bulb = M('lamp_bulb', lambda: L.emit_mat('lamp_bulb', '#ffb35c', 9.0))
    k_glass = M('lamp_glass', lambda: L.simple_mat('lamp_glass', '#ffc27a', rough=0.2, spec=0.6,
                                                     emit='#ff9a3c', emit_strength=3.5))
    objs = []
    bx, by = 0.36, 0.56
    # a heavy fluted base, the pole kinked by a bump from a car
    objs.append(L.lathe('base', [(0, 0), (0.12, 0), (0.12, 0.05), (0.095, 0.08), (0.085, 0.25),
                                 (0.06, 0.30), (0.05, 0.34), (0, 0.34)], k_pole, c=(bx, by, 0), segs=16))
    objs.append(L.lathe('band', [(0, 0.12), (0.092, 0.12), (0.092, 0.16), (0, 0.16)], k_rust,
                        c=(bx, by, 0), segs=16))
    kink = (bx + 0.02, by + 0.01, 1.25)
    top = (bx + 0.14, by - 0.02, 2.02)
    objs.append(L.tube('pole', [(bx, by, 0.3), (bx + 0.005, by, 0.8), kink,
                                (bx + 0.08, by - 0.01, 1.65), top], [0.042, 0.038, 0.034, 0.032, 0.03],
                       k_pole, segs=12))
    # the arm curls over and droops
    arm = [top, (bx + 0.20, by - 0.03, 2.13), (bx + 0.30, by - 0.04, 2.15), (bx + 0.40, by - 0.05, 2.10),
           (bx + 0.46, by - 0.055, 2.03)]
    objs.append(L.tube('arm', arm, [0.03, 0.028, 0.026, 0.025, 0.024], k_pole, segs=10))
    hx, hy, hz = bx + 0.47, by - 0.055, 1.98
    # the hood (a squat cone) and the glass bowl under it
    objs.append(L.lathe('hood', [(0, 0.10), (0.03, 0.10), (0.15, 0.02), (0.165, 0.0), (0.16, -0.012),
                                 (0, -0.012)], k_hood, c=(hx, hy, hz), segs=20))
    objs.append(L.lathe('bowl', [(0, -0.13), (0.06, -0.125), (0.11, -0.09), (0.135, -0.04),
                                 (0.14, -0.012), (0, -0.012)], k_glass, c=(hx, hy, hz), segs=20))
    objs.append(L.sphere('bulb', (hx, hy, hz - 0.05), 0.045, k_bulb))
    for o in objs[-2:]:
        o.visible_shadow = False          # the lamp's light shines through its glass
        o.visible_diffuse = False         # and the pool is the spot's, not the glass's
    spot = L.add_spot((hx, hy, hz - 0.07), color=lin('#ffb45a'), power=170.0, cone_deg=72, blend=0.9)
    lights = [spot, C.add_point_light((hx, hy, hz - 0.07), color=lin('#ffb45a'), power=6.0, radius=0.05)]
    render_prop('city_lamp', objs, 2.12, lights, glow_R=1.45, glow_c=(hx, hy), pool_lights=[spot],
                extra={'light_at': [round(hx, 3), round(hy, 3), round(hz - 0.07, 3)]})


def car_body(k_paint, k_rust, k_glass, k_chrome, k_tyre, k_rim, k_dark, k_light, flat=True, k_cabin=None,
             k_rocker=None, k_tail=None, cab_shift=0.0):
    """A wrecked 70s sedan along X over cells (0, 0) and (1, 0)."""
    objs = []
    x0, x1 = 0.08, 1.92
    y0, y1 = 0.12, 0.88
    zb, zt = 0.17, 0.47
    # lower body with a dent: hood a bit lower than the belt line
    objs.append(L.cbox('body', ((x0 + x1) / 2, 0.5, (zb + zt) / 2), (x1 - x0, y1 - y0, zt - zb), k_paint,
                       bev=0.05, seg=3))
    # rocker panel rust strip
    objs.append(L.cbox('rocker', ((x0 + x1) / 2, 0.5, zb + 0.035), (x1 - x0 - 0.12, y1 - y0 + 0.012, 0.06),
                       k_rocker or k_rust, bev=0.02))
    # cabin (a trapezoid greenhouse)
    cx0, cx1 = 0.62 + cab_shift, 1.40 + cab_shift
    prof = [(cx0 - 0.08, zt - 0.01), (cx1 + 0.12, zt - 0.01), (cx1 - 0.05, zt + 0.27), (cx0 + 0.12, zt + 0.27)]
    cab = L.prism('cabin', prof, y0 + 0.07, y1 - 0.07, k_cabin or k_paint, axis='y', bev=0.035, seg=3)
    objs.append(cab)
    # windows: dark glass inset on the sides, front and back
    wz0, wz1 = zt + 0.03, zt + 0.235
    for yy in (y0 + 0.064, y1 - 0.074):
        wp = [(cx0 + 0.02, wz0), (cx1 + 0.02, wz0), (cx1 - 0.07, wz1), (cx0 + 0.15, wz1)]
        objs.append(L.prism('win_s', wp, yy, yy + 0.01, k_glass, axis='y'))
    # windshield (front, +X) cracked: glass plane slanted
    objs.append(L.prism('win_f', [(y0 + 0.11, 0), (y1 - 0.11, 0), (y1 - 0.14, 1), (y0 + 0.14, 1)], 0, 0.01,
                        k_glass, axis='x'))
    ws = objs[-1]
    for vtx in ws.data.vertices:
        # map (y, t) -> along the slanted windshield from (cx1+0.10, zt) to (cx1-0.04, zt+0.26)
        t = vtx.co.z
        yy = vtx.co.y
        off = vtx.co.x
        px = cx1 + 0.105 - 0.14 * t + off
        pz = zt + 0.012 + 0.235 * t
        vtx.co = (px + 0.003, yy, pz)
    # rear window
    objs.append(L.prism('win_b', [(y0 + 0.12, 0), (y1 - 0.12, 0), (y1 - 0.15, 1), (y0 + 0.15, 1)], 0, 0.01,
                        k_glass, axis='x'))
    wb = objs[-1]
    for vtx in wb.data.vertices:
        t = vtx.co.z
        yy = vtx.co.y
        off = vtx.co.x
        px = cx0 - 0.065 + 0.17 * t - off
        pz = zt + 0.012 + 0.235 * t
        vtx.co = (px - 0.003, yy, pz)
    # bumpers
    for bx in (x0 - 0.005, x1 + 0.005):
        objs.append(L.cbox('bumper', (bx, 0.5, zb + 0.06), (0.06, y1 - y0 + 0.03, 0.07), k_chrome, bev=0.02))
    # headlights (+X): one dead, one hanging
    for yy, lit in ((0.28, False), (0.72, False)):
        h = L.lathe('hl', [(0, 0), (0.05, 0), (0.05, 0.02), (0, 0.025)], k_light if lit else k_dark,
                    c=(0, 0, 0), segs=14)
        h.rotation_euler = (0, math.radians(90), 0)
        h.location = (x1 - 0.005, yy, zt - 0.10)
        objs.append(h)
    # tail lights (-X)
    for yy in (0.24, 0.76):
        objs.append(L.cbox('tl', (x0 - 0.002, yy, zt - 0.08), (0.02, 0.12, 0.06), k_tail or k_rust, bev=0.01))
    # wheels (along Y), the front right one flat
    for wx, wy, fl in ((0.42, y0, False), (1.55, y0, flat), (0.42, y1, False), (1.55, y1, False)):
        r = 0.155
        sq = 0.62 if fl else 1.0
        t = L.lathe('tyre', [(0.0, -0.07), (0.10, -0.075), (r * 0.97, -0.065), (r, -0.03), (r, 0.03),
                             (r * 0.97, 0.065), (0.10, 0.075), (0.0, 0.07)], k_tyre, c=(0, 0, 0), segs=20)
        near = wy < 0.5
        t.rotation_euler = (math.radians(-90 if near else 90), 0, 0)
        t.scale = (1.0 + (0.12 if fl else 0), sq, 1.0)
        zc = r * sq
        t.location = (wx, wy + (0.02 if wy < 0.5 else -0.02), zc)
        objs.append(t)
        rim = L.lathe('rim', [(0, -0.078), (0.07, -0.078), (0.07, -0.07), (0, -0.07)], k_rim, c=(0, 0, 0),
                      segs=16)
        rim.rotation_euler = (math.radians(-90 if near else 90), 0, 0)
        rim.scale = (1, sq, 1)
        rim.location = t.location
        objs.append(rim)
    return objs


def wreck_car():
    k_paint = M('car_paint', lambda: car_paint_mat('car_paint'))
    k_rust = pm('car_rust', '#8a4f33', rough=0.85, spec=0.2, col2='#6d3d29', noise=1, noise_scale=20)
    k_glass = pm('car_glass', '#2e4a52', rough=0.15, spec=0.7)
    k_chrome = pm('car_chrome', '#a9aeb3', rough=0.35, spec=0.7, metal=0.6)
    k_tyre = pm('car_tyre', '#2b2c30', rough=0.8, spec=0.2)
    k_rim = pm('car_rim', '#8b9096', rough=0.4, spec=0.6, metal=0.4)
    k_dark = pm('car_hl_dark', '#5d6166', rough=0.3, spec=0.6)
    objs = car_body(k_paint, k_rust, k_glass, k_chrome, k_tyre, k_rim, k_dark, k_dark)
    # tilt the car onto its flat tyre (front right = +X, -Y)
    import mathutils
    piv = mathutils.Vector((1.55, 0.12, 0.0))
    R = (mathutils.Matrix.Translation(piv) @ mathutils.Matrix.Rotation(math.radians(-2.2), 4, 'X') @
         mathutils.Matrix.Rotation(math.radians(-1.6), 4, 'Y') @ mathutils.Matrix.Translation(-piv))
    return K.transform(objs, R)


@REG.add('city_car_x', sample=True)
def _():
    render_prop('city_car_x', wreck_car(), 0.78, footprint=(2, 1))


@REG.add('city_car_y')
def _():
    # along Y over cells (0, 0) and (0, 1), the bonnet away from the camera
    render_prop('city_car_y', K.rot_z(wreck_car(), 90), 0.78, footprint=(1, 2))


def car_paint_mat(key):
    """Faded teal paint with rust blooms along the lower edges."""
    c_paint, c_paint2 = lin('#5f9c97'), lin('#4f857f')
    c_rust, c_rust2 = lin('#94532f'), lin('#6e3b24')

    def build(nt, neutral):
        g = L.G(nt, neutral)
        p = g.pos()
        x, y, z = g.sep(p)
        n1 = g.noise(p, 5.0, 3.0, 0.6)
        n2 = g.noise(p, 18.0, 2.0, 0.5)
        paint = g.mix(g.mapr(n1, 0.4, 0.62), c_paint, c_paint2)
        # rust: more near the bottom and at the wheel arches
        low = g.mapr(z, 0.42, 0.20, 0.0, 1.0)
        rmask = g.m('ADD', g.m('MULTIPLY', low, 0.55), g.m('MULTIPLY', n2, 0.75))
        rmask = g.mapr(rmask, 0.78, 0.86)
        rust = g.mix(n1, c_rust, c_rust2)
        col = g.mix(rmask, paint, rust)
        rough = g.m('ADD', 0.45, g.m('MULTIPLY', rmask, 0.4))
        nrm = g.bump(n2, 0.15, 0.01)
        return g.bsdf(col, rough, 0.45, 0.0, nrm)

    return C.mat(key, build=build)


@REG.add('city_hydrant', sample=True)
def _():
    k_red = pm('hyd_red', '#c24a3c', rough=0.45, spec=0.5, col2='#a63c31', noise=1, noise_scale=10)
    k_cap = pm('hyd_cap', '#d9d2c2', rough=0.4, spec=0.5)
    k_dark = pm('hyd_dark', '#6e3029', rough=0.6)
    c = (0.5, 0.5, 0)
    objs = [L.lathe('hyd', [(0, 0), (0.15, 0), (0.15, 0.04), (0.12, 0.055), (0.11, 0.07), (0.105, 0.36),
                            (0.125, 0.37), (0.125, 0.40), (0.11, 0.41), (0.10, 0.46), (0.07, 0.51),
                            (0.03, 0.53), (0, 0.53)], k_red, c=c, segs=20)]
    objs.append(L.lathe('nut', [(0, 0.52), (0.035, 0.52), (0.035, 0.575), (0, 0.575)], k_cap, c=c, segs=6))
    objs.append(L.lathe('ring', [(0, 0.365), (0.128, 0.365), (0.128, 0.39), (0, 0.39)], k_cap, c=c, segs=20))
    # nozzles: two side (along X), one big front (-Y)
    for ang, r, ln in ((0, 0.045, 0.16), (180, 0.045, 0.16), (-90, 0.065, 0.17)):
        a = math.radians(ang)
        d = (math.cos(a), math.sin(a))
        p0 = (c[0] + d[0] * 0.08, c[1] + d[1] * 0.08, 0.27)
        p1 = (c[0] + d[0] * ln, c[1] + d[1] * ln, 0.27)
        objs.append(L.tube('noz', [p0, p1], r, k_red, segs=14))
        p2 = (c[0] + d[0] * (ln + 0.03), c[1] + d[1] * (ln + 0.03), 0.27)
        objs.append(L.tube('capn', [p1, p2], r * 0.92, k_cap, segs=6))
    render_prop('city_hydrant', objs, 0.575)


@REG.add('city_dumpster', sample=True)
def _():
    k_body = M('dump_body', lambda: dumpster_mat('dump_body'))
    k_lid = pm('dump_lid', '#37403f', rough=0.7, spec=0.3)
    k_dark = pm('dump_dark', '#2a302f', rough=0.7)
    k_bag = pm('dump_bag', '#34363d', rough=0.25, spec=0.6)
    k_wheel = pm('dump_wheel', '#26272b', rough=0.7)
    objs = []
    x0, x1, y0, y1 = 0.08, 0.92, 0.16, 0.86
    zb, zt = 0.10, 0.78
    # trapezoid body: wider at the top at the front (sloped front face)
    prof = [(y0 + 0.07, zb), (y1, zb), (y1, zt), (y0, zt)]
    body = L.prism('body', prof, x0, x1, k_body, axis='x', bev=0.02)
    objs.append(body)
    # ribs on the front face
    for xx in (0.25, 0.5, 0.75):
        objs.append(L.prism('rib', [(y0 + 0.07 - 0.02, zb + 0.05), (y0 + 0.07, zb + 0.05), (y0, zt - 0.08),
                                    (y0 - 0.02, zt - 0.08)], xx - 0.025, xx + 0.025, k_body, axis='x',
                             bev=0.008))
    # a rim at the top
    objs.append(L.cbox('rim', ((x0 + x1) / 2, (y0 + y1) / 2, zt + 0.015), (x1 - x0 + 0.03, y1 - y0 + 0.03, 0.035),
                       k_body, bev=0.012))
    # side pockets (for the truck forks)
    for xx in (x0 - 0.015, x1 + 0.015):
        objs.append(L.cbox('pocket', (xx, 0.5, 0.52), (0.04, 0.34, 0.09), k_dark, bev=0.01))
    # the lids: back one closed and flat, front one propped open (tilted back)
    objs.append(L.cbox('lid_b', ((x0 + x1) / 2, 0.5 * (0.5 + y1) + 0.005, zt + 0.045),
                       (x1 - x0 + 0.02, (y1 - 0.5) + 0.01, 0.03), k_lid, bev=0.01))
    lid = L.cbox('lid_f', ((x0 + x1) / 2, 0.30, zt + 0.045), (x1 - x0 + 0.02, 0.34, 0.03), k_lid, bev=0.01,
                 rx=0)
    import mathutils
    piv = mathutils.Vector((0.5, 0.49, zt + 0.06))
    lid.matrix_world = (mathutils.Matrix.Translation(piv) @ mathutils.Matrix.Rotation(math.radians(-38), 4, 'X') @
                        mathutils.Matrix.Translation(-piv)) @ lid.matrix_world
    objs.append(lid)
    # a trash bag peeking out under the open lid
    bag = L.sphere('bag', (0.40, 0.33, zt + 0.02), 0.13, k_bag, scale=(1.3, 1.0, 0.75))
    L.subsurf(bag, 1)
    objs.append(bag)
    objs.append(L.sphere('bag2', (0.66, 0.36, zt + 0.0), 0.10, k_bag, scale=(1.1, 1.0, 0.8)))
    # little wheels
    for wx, wy in ((0.16, 0.26), (0.84, 0.26), (0.16, 0.80), (0.84, 0.80)):
        wh = L.lathe('wheel', [(0, -0.02), (0.05, -0.02), (0.05, 0.02), (0, 0.02)], k_wheel, c=(0, 0, 0), segs=12)
        wh.rotation_euler = (math.radians(90), 0, 0)
        wh.location = (wx, wy, 0.05)
        objs.append(wh)
        objs.append(L.cbox('caster', (wx, wy, 0.085), (0.05, 0.05, 0.03), k_dark))
    render_prop('city_dumpster', objs, 0.84)


def dumpster_mat(key):
    c1, c2 = lin('#4f7a5e'), lin('#44684f')
    cr = lin('#7c4a31')

    def build(nt, neutral):
        g = L.G(nt, neutral)
        p = g.pos()
        x, y, z = g.sep(p)
        n1 = g.noise(p, 6.0, 3.0, 0.6)
        n2 = g.noise(p, 22.0, 2.0, 0.5)
        col = g.mix(g.mapr(n1, 0.4, 0.62), c1, c2)
        low = g.mapr(z, 0.35, 0.10, 0.0, 1.0)
        rm = g.mapr(g.m('ADD', g.m('MULTIPLY', low, 0.5), g.m('MULTIPLY', n2, 0.8)), 0.80, 0.88)
        col = g.mix(rm, col, cr)
        return g.bsdf(col, g.m('ADD', 0.55, g.m('MULTIPLY', rm, 0.3)), 0.4, 0.0, g.bump(n2, 0.12, 0.01))

    return C.mat(key, build=build)


# ---------------------------------------------------------------------------

# ===========================================================================
# phase 2: the rest of the city blocks and fills
# ===========================================================================
P_CONCRETE = '#9b9891'
P_GRASS = '#72806a'
P_GRASS_DK = '#5d6a58'
P_GRASS_LT = '#8b9679'
P_DIRT = '#6f645a'

for v in range(3):
    @REG.add('city_blk_crosswalk_v%d' % v)
    def _(v=v):
        k = mat_block('cross%d' % v, tex_pair('cross_top%d' % v, tex_asphalt_top(v, False, True)),
                      tex_pair('asph_side', tex_asphalt_side()), rough=0.85, spec=0.25, bump=0.15)
        render_tile('city_blk_crosswalk_v%d' % v, [block_box(k)])


@cached
def tex_concrete_side(fill=False):
    """Poured concrete: board-formed lines every 0.127 m (4 per floor, so it
    stacks), a few tie holes, rain streaks."""
    P = Paint(0, 1, -FM, 0)
    col = P.full(lina(P_CONCRETE))
    col *= (1 + 0.05 * P.noise(0.012, 900) + 0.05 * P.noise(0.12, 901))[..., None]
    h = 0.5 + 0.06 * P.noise(0.012, 902)
    ch = FM / 4
    fv = ((P.Y - P.v0) / ch) % 1.0
    d = np.minimum(fv, 1 - fv) * ch
    m = P.cover(d - 0.004, 0.003)
    col = blend(col, m * 0.35, lina('#7c7a75'))
    h -= 0.15 * m
    rng = L.rng_for('ties')
    for r in range(4):
        for cx in (0.25, 0.75):
            cy = P.v0 + (r + 0.5) * ch
            mm = P.cover(P.sd_circle(cx + (0.125 if r % 2 else 0), cy, 0.011))
            col = blend(col, mm * 0.7, lina('#6c6a66'))
    # rain streaks (periodic in u)
    st = P.noise(0.03, 903, aniso=(1.0, 0.12))
    col *= (1 - 0.06 * np.clip(st, 0, 2))[..., None]
    del rng
    col *= side_shade(P, True, 0.10)
    return col, h


@cached
def tex_concrete_top(variant, fill=False):
    P = Paint(0, 1, 0, 1)
    rng = L.rng_for('concrete', variant)
    col = P.full(lina(P_CONCRETE) * 1.06)
    broom = P.noise(0.006, 910, aniso=(0.2, 1.0))
    col *= (1 + 0.035 * broom + 0.05 * P.noise(0.2, 911 + variant) * interior(P, 0.05, 0.15))[..., None]
    h = 0.5 + 0.08 * broom
    if variant == 0:
        sd = P.sd_ellipse(0.35, 0.62, 0.12, 0.08) + 0.02 * P.noise(0.04, 912)
        col = blend(col, P.cover(sd, 0.03) * 0.3, lina('#85827c'))
    if variant == 1:
        ck = L.crack_path(rng, (0.15, 0.30), (0.82, 0.58), 7, 0.04)
        m = P.cover(P.sd_polyline(ck, 0.016), 0.004)
        col = blend(col, m * 0.85, lina('#5f5d59'))
        h -= 0.3 * m
    if variant == 2:
        # a rusty bolt plate and an oil drip
        m = P.cover(P.sd_box(0.62, 0.40, 0.08, 0.06, 0.01))
        col = blend(col, m, lina('#7e6453'))
        for cx, cy in ((0.57, 0.36), (0.67, 0.36), (0.57, 0.44), (0.67, 0.44)):
            col = blend(col, P.cover(P.sd_circle(cx, cy, 0.01)), lina('#4f463f'))
        sd = P.sd_ellipse(0.32, 0.66, 0.10, 0.06)
        col = blend(col, P.cover(sd, 0.02) * 0.45, lina('#57585c'))
    col *= edge_shade(P, 0.025, 0.24)
    return col, h


for v in range(3):
    @REG.add('city_blk_concrete_v%d' % v)
    def _(v=v):
        k = mat_block('conc%d' % v, tex_pair('conc_top%d' % v, tex_concrete_top(v)),
                      tex_pair('conc_side', tex_concrete_side()), rough=0.85, spec=0.25, bump=0.25)
        render_tile('city_blk_concrete_v%d' % v, [block_box(k, bev=0.008)])


@REG.add('city_fill_concrete')
def _():
    k = mat_block('concfill', tex_pair('conc_top0', tex_concrete_top(0)),
                  tex_pair('conc_side', tex_concrete_side()), rough=0.85, spec=0.25, bump=0.25)
    render_tile('city_fill_concrete', [block_box(k, bev=0.006)])


@cached
def tex_grass_top(variant):
    """Dead, patchy grey-green lawn: little blade strokes over a mottled
    ground, bare patches inside (per variant)."""
    P = Paint(0, 1, 0, 1)
    rng = L.rng_for('grass', variant)
    col = P.full(lina(P_GRASS))
    col *= (1 + 0.08 * P.noise(0.05, 920) + 0.06 * P.noise(0.2, 921 + variant) * interior(P, 0.05, 0.15))[..., None]
    h = P.zeros() + 0.4
    srng = L.rng_for('blades')
    for _ in range(900):
        x, y = srng.uniform(-0.02, 1.02), srng.uniform(-0.02, 1.02)
        ln = srng.uniform(0.018, 0.035)
        a = srng.uniform(-0.6, 0.6) + math.pi / 2
        pts = [(x, y), (x + ln * math.cos(a) * 0.4, y + ln * math.sin(a))]
        # strokes are cheap only in a small window
        x0, x1 = int(max(0, (x - 0.05) * P.w)), int(min(P.w, (x + 0.05) * P.w))
        y0, y1 = int(max(0, (y - 0.05) * P.h)), int(min(P.h, (y + 0.06) * P.h))
        if x1 <= x0 or y1 <= y0:
            continue
        sub = Paint.__new__(Paint)
        sub.X, sub.Y, sub.px = P.X[y0:y1, x0:x1], P.Y[y0:y1, x0:x1], P.px
        m = sub.cover(Paint.sd_polyline(sub, pts, 0.006), 0.003)
        t = srng.random()
        c = lina(P_GRASS_LT) if t < 0.45 else (lina(P_GRASS_DK) if t < 0.85 else lina('#8d8a70'))
        col[y0:y1, x0:x1] = blend(col[y0:y1, x0:x1], m * 0.8, c)
        h[y0:y1, x0:x1] = np.maximum(h[y0:y1, x0:x1], m * 0.8)
    # bare patches of earth (inside the cell)
    patches = {0: [(0.62, 0.36, 0.09, 0.06)], 1: [(0.35, 0.60, 0.16, 0.10), (0.70, 0.28, 0.07, 0.05)],
               2: [(0.50, 0.50, 0.20, 0.13)]}[variant]
    for cx, cy, rx, ry in patches:
        sd = P.sd_ellipse(cx, cy, rx, ry) + 0.02 * P.noise(0.03, 930 + variant)
        m = P.cover(sd, 0.01)
        col = blend(col, m, lina(P_DIRT) * (1 + 0.08 * P.noise(0.015, 931))[..., None])
        h = np.where(m > 0.5, 0.25, h)
    del rng
    col *= edge_shade(P, 0.02, 0.14)
    return col, h


@cached
def tex_grass_side():
    P = Paint(0, 1, -FM, 0)
    ecol, eh = tex_earth_side(15)
    col, h = ecol.copy(), eh.copy()
    lip = -0.045 + 0.012 * P.noise(0.03, 940) + 0.01 * np.abs(np.sin(P.X * 2 * math.pi * 9))
    m = P.cover(lip - P.Y, 0.003)
    gc = lina(P_GRASS) * (1 + 0.1 * P.noise(0.01, 941))[..., None]
    col = blend(col, m, gc)
    h = np.where(m > 0.5, 0.6, h)
    col *= side_shade(P, True, 0.08)
    return col, h


def grass_tufts(objs, rng, n, key, area=(0.12, 0.88)):
    for _ in range(n):
        cx, cy = rng.uniform(*area), rng.uniform(*area)
        for i in range(5):
            a = rng.uniform(0, 2 * math.pi)
            r = rng.uniform(0, 0.02)
            bx, by = cx + r * math.cos(a), cy + r * math.sin(a)
            tip = (bx + rng.uniform(-0.035, 0.035), by + rng.uniform(-0.035, 0.035), rng.uniform(0.05, 0.09))
            objs.append(L.tube('blade', [(bx, by, -0.004), ((bx + tip[0]) / 2, (by + tip[1]) / 2, tip[2] * 0.6), tip],
                               [0.010, 0.007, 0.002], key, segs=5))


for v in range(3):
    @REG.add('city_blk_grass_v%d' % v)
    def _(v=v):
        k = mat_block('grass%d' % v, tex_pair('grass_top%d' % v, tex_grass_top(v)),
                      tex_pair('grass_side', tex_grass_side()), rough=0.95, spec=0.15, bump=0.3, bump_dist=0.01)
        objs = [block_box(k, bev=0.006)]
        kt = pm('tuft', P_GRASS_LT, rough=0.8, col2=P_GRASS_DK, noise=1, noise_scale=30)
        grass_tufts(objs, L.rng_for('tufts', v), [1, 2, 1][v], kt)
        if v == 2:
            # a dead dandelion (a grey puff on a stalk)
            objs.append(L.tube('stalk', [(0.30, 0.70, 0), (0.31, 0.70, 0.12)], 0.005, kt, segs=5))
            objs.append(L.sphere('puff', (0.31, 0.70, 0.135), 0.022, pm('puff', '#c9cbc6', rough=0.9)))
        render_tile('city_blk_grass_v%d' % v, objs)


# scrap: a junkyard block of crushed cars ------------------------------------
SCRAP_COLS = ['#5f9c97', '#b0513f', '#cdb986', '#7d8b9a', '#8a4f33', '#6b7a55', '#c9c4b6', '#4f6b8f']


@cached
def tex_scrap_side(variant):
    """Crushed car layers: jagged bands of faded paints and rust with dark
    gaps, periodic in u; the bands of one floor end at its top and bottom,
    so scrap stacks on scrap."""
    P = Paint(0, 1, -FM, 0)
    rng = L.rng_for('scrap', variant)
    nb = 5
    edges = [P.v0] + sorted(P.v0 + (P.v1 - P.v0) * (i + rng.uniform(-0.25, 0.25)) / nb for i in range(1, nb)) + [P.v1]
    col = np.zeros((P.h, P.w, 3), np.float32)
    h = P.zeros()
    band = np.zeros(P.X.shape, int)
    for i in range(1, nb):
        wob = 0.018 * P.noise(0.04, 950 + i + 10 * variant) + 0.012 * np.sin(P.X * 2 * math.pi * rng.randint(2, 5))
        band += (P.Y > edges[i] + wob)
    for i in range(nb):
        c = lina(SCRAP_COLS[rng.randrange(len(SCRAP_COLS))])
        col[band == i] = c
    dent = P.noise(0.035, 960 + variant)
    col *= (1 + 0.14 * dent)[..., None]
    # rust blooms and the dark crushed gaps between the layers
    rust = np.clip((P.noise(0.05, 961 + variant) - 0.6) / 0.5, 0, 1)
    col = blend(col, rust * 0.8, lina('#8a4f33'))
    gap = np.zeros(P.X.shape, np.float32)
    for i in range(1, nb):
        wob = 0.018 * P.noise(0.04, 950 + i + 10 * variant)
        gap = np.maximum(gap, P.cover(np.abs(P.Y - edges[i] - wob) - 0.008, 0.004))
    col = blend(col, gap * 0.9, lina('#2b2a2c'))
    h = 0.5 + 0.2 * dent - 0.4 * gap
    col *= side_shade(P, True, 0.14)
    return col, h


@cached
def tex_scrap_top(variant):
    P = Paint(0, 1, 0, 1)
    rng = L.rng_for('scraptop', variant)
    col = P.full(lina('#7d7f82'))
    # flattened panels: a few rotated rectangles of paint
    for i in range(5):
        cx, cy = rng.uniform(0.15, 0.85), rng.uniform(0.15, 0.85)
        sd = P.sd_box(cx, cy, rng.uniform(0.15, 0.3), rng.uniform(0.1, 0.22), 0.02, rot=rng.uniform(-0.6, 0.6))
        c = lina(SCRAP_COLS[rng.randrange(len(SCRAP_COLS))])
        m = P.cover(sd, 0.004)
        col = blend(col, m, c)
        col = blend(col, P.cover(np.abs(sd) - 0.005, 0.003) * 0.7, lina('#2f2e30'))
    dent = P.noise(0.04, 970 + variant)
    col *= (1 + 0.14 * dent)[..., None]
    rust = np.clip((P.noise(0.05, 971 + variant) - 0.5) / 0.5, 0, 1)
    col = blend(col, rust * 0.7, lina('#8a4f33'))
    col *= edge_shade(P, 0.025, 0.25)
    return col, 0.5 + 0.25 * dent


for v in range(3):
    @REG.add('city_blk_scrap_v%d' % v)
    def _(v=v):
        k = mat_block('scrap%d' % v, tex_pair('scrap_top%d' % v, tex_scrap_top(v)),
                      tex_pair('scrap_side%d' % v, tex_scrap_side(v)), rough=0.55, spec=0.45, bump=0.5,
                      bump_dist=0.02)
        render_tile('city_blk_scrap_v%d' % v, [block_box(k, bev=0.012)])


# boarded windows ---------------------------------------------------------------

@REG.add('city_blk_brick_boarded')
def _():
    side = tex_pair('brick_side1w', tex_brick_side(1, False, True))
    top = tex_pair('coping_top', tex_coping_top())
    k = mat_block('brick_boarded', top, side, rough=0.85, spec=0.25, bump=0.35, bump_dist=0.015)
    wall = block_box(k, bev=0.0)
    ww, z0, z1, dep, dep_r = 0.44, -0.405, -0.13, 0.05, 0.02
    L.cut(wall, [L.box('cut_f', 0.5 - ww / 2, -0.1, z0, 0.5 + ww / 2, dep, z1),
                 L.box('cut_r', 1 - dep_r, 0.5 - ww / 2, z0, 1.1, 0.5 + ww / 2, z1)])
    L.bevel(wall, 0.008)
    objs = [wall]
    kd = pm('win_dark', '#1e1c1f', rough=0.9)
    objs.append(L.box('back_f', 0.5 - ww / 2, dep - 0.004, z0, 0.5 + ww / 2, dep, z1, kd))
    objs.append(L.box('back_r', 1 - dep_r, 0.5 - ww / 2, z0, 1 - dep_r + 0.004, 0.5 + ww / 2, z1, kd))
    kw = pm('board', '#8d7a63', rough=0.8, spec=0.2, col2='#6f5f4d', noise=1, noise_scale=18, obj=True)
    kw2 = pm('board2', '#9c8a6e', rough=0.8, spec=0.2, col2='#7b6a54', noise=1, noise_scale=18, obj=True)
    kn = pm('nail', '#4a4a4c', rough=0.5)
    rng = L.rng_for('boards')
    for face in ('f', 'r'):
        for i, zc in enumerate((-0.36, -0.27, -0.18)):
            tilt = rng.uniform(-6, 6)
            kk = kw if i % 2 == 0 else kw2
            if face == 'f':
                b = L.cbox('board', (0.5, 0.012, zc), (ww + 0.08, 0.02, 0.07), kk, bev=0.006, ry=tilt)
            else:
                b = L.cbox('board', (0.988, 0.5, zc), (0.02, ww + 0.08, 0.07), kk, bev=0.006, rx=tilt)
            objs.append(b)
        # one diagonal
        if face == 'f':
            objs.append(L.cbox('diag', (0.5, 0.004, -0.27), (ww + 0.02, 0.012, 0.06), kw2, bev=0.005, ry=-28))
        else:
            objs.append(L.cbox('diag', (0.996, 0.5, -0.27), (0.012, ww + 0.02, 0.06), kw2, bev=0.005, rx=28))
    del kn
    render_tile('city_blk_brick_boarded', objs)


# bridges: a steel grate deck -------------------------------------------------

def grate_bridge(axis):
    """Walk along `axis`: two side beams and a rail on each side along it, a
    grate of bars between them (you see the water through it). Built along X,
    turned for Y."""
    kst = pm('steel', '#6f8385', rough=0.45, spec=0.5, col2='#5c6e70', noise=1, noise_scale=6, obj=True)
    kdk = pm('steel_dk', '#3f4b4d', rough=0.6, spec=0.4)
    krust = pm('steel_rust', '#8a5634', rough=0.8)
    objs = []
    y0, y1 = 0.10, 0.90
    for yy in (y0, y1):
        # I-beam: web + flanges, the full cell long so the next deck joins
        objs.append(L.box('web', 0.0, yy - 0.012, -0.16, 1.0, yy + 0.012, -0.01, kst))
        objs.append(L.box('fl_t', 0.0, yy - 0.045, -0.035, 1.0, yy + 0.045, 0.0, kst, bev=0.004))
        objs.append(L.box('fl_b', 0.0, yy - 0.04, -0.17, 1.0, yy + 0.04, -0.15, kst))
    # the grate: bars along the walking direction, cross bars every 0.25
    n = 9
    for i in range(n):
        yy = y0 + 0.05 + (y1 - y0 - 0.10) * i / (n - 1)
        objs.append(L.box('bar', 0.0, yy - 0.011, -0.03, 1.0, yy + 0.011, -0.004, kdk))
    for xx in (0.125, 0.375, 0.625, 0.875):
        objs.append(L.box('xbar', xx - 0.012, y0, -0.035, xx + 0.012, y1, -0.012, kst))
    # rails: a post in the middle of each side, a top rail and a mid rail the cell long
    for yy in (y0 - 0.02, y1 + 0.02):
        objs.append(L.box('post', 0.47, yy - 0.02, 0.0, 0.53, yy + 0.02, 0.42, kst, bev=0.006))
        objs.append(L.tube('rail', [(0.0, yy, 0.40), (1.0, yy, 0.40)], 0.022, kst, segs=10, caps=False))
        objs.append(L.tube('rail2', [(0.0, yy, 0.21), (1.0, yy, 0.21)], 0.014, kst, segs=8, caps=False))
    objs.append(L.box('rust', 0.62, y0 - 0.05, -0.03, 0.78, y0 - 0.044, -0.005, krust))
    if axis == 'y':
        K.rot_z(objs, 90)
    return objs


for ax in ('x', 'y'):
    @REG.add('city_bridge_%s' % ax)
    def _(ax=ax):
        objs = grate_bridge(ax)
        C.render_sprite(A.out, 'city_bridge_%s' % ax, objs, C.cell(0, 0, 0), passes=('color', 'z', 'shadow'),
                        samples=A.samples, shadow_z=-0.18, kind='bridge',
                        extra={'walk': ax, 'deck_z': 0.0, 'height_m': 0.42})
        C.remove(objs)


# ===========================================================================
# phase 2: city props
# ===========================================================================

def quad(name, pts, key):
    return C.mesh_object(name, pts, [(0, 1, 2, 3)], key)


@cached
def tex_chainlink():
    """Galvanised chain-link: a diamond mesh (period 7 cm), its wires as the
    opacity (a mesh at this size reads as a see-through haze with a pattern)."""
    p = 0.07
    P = Paint(0, 7 * p, 0, 7 * p, px_per_m=512)
    a = (P.X + P.Y) / p
    b = (P.X - P.Y) / p
    d = np.minimum(np.abs(a - np.round(a)), np.abs(b - np.round(b))) * p / 1.4142
    wire = np.clip(P.cover(d - 0.0065, 0.003) * 0.95, 0, 1)
    col = P.full(lina('#aab1b6')) * (1 + 0.1 * P.noise(0.03, 990))[..., None]
    return col, wire


def chainlink_mat(key='chainlink', uv='xz'):
    col, a = tex_chainlink()
    ic, ia = L.image('chain_c', col), L.image('chain_a', a, data=True)
    return M(key, lambda: L.tex_mat(key, ic, uv, rough=0.45, spec=0.6, metal=0.3, aimg=ia,
                                    scale=(0.49, 0.49), obj=True))


@REG.add('city_trashcan')
def _():
    kg = pm('can', '#8f979d', rough=0.45, spec=0.55, metal=0.3, col2='#7b8389', noise=1, noise_scale=9)
    kd = pm('can_dk', '#626a70', rough=0.5, spec=0.5, metal=0.3)
    c = (0.5, 0.52, 0)
    objs = [L.lathe('can', [(0, 0), (0.20, 0), (0.205, 0.02), (0.23, 0.60), (0.225, 0.62), (0, 0.62)], kg, c=c,
                    segs=24)]
    for z in (0.12, 0.33, 0.52):
        objs.append(L.lathe('rib', [(0, z), (0.215 + z * 0.045, z), (0.222 + z * 0.045, z + 0.03),
                                    (0, z + 0.03)], kd, c=c, segs=24))
    for sx in (-1, 1):
        objs.append(L.tube('handle', [(c[0] + sx * 0.22, c[1], 0.50), (c[0] + sx * 0.27, c[1], 0.52),
                                      (c[0] + sx * 0.27, c[1], 0.44), (c[0] + sx * 0.225, c[1], 0.44)], 0.012, kd,
                           segs=6))
    # the lid, knocked askew, and a paper ball on the ground
    lid = L.lathe('lid', [(0, 0.0), (0.245, 0.0), (0.245, 0.025), (0.12, 0.07), (0.03, 0.08), (0.03, 0.11),
                          (0, 0.11)], kg, c=(0, 0, 0), segs=24)
    lid.rotation_euler = (math.radians(-14), math.radians(9), 0)
    lid.location = (c[0] + 0.02, c[1] + 0.03, 0.63)
    objs.append(lid)
    objs.append(L.sphere('paper', (0.27, 0.27, 0.045), 0.045, pm('paper', '#e3e0d6', rough=0.9)))
    objs.append(L.sphere('bag', (c[0] - 0.05, c[1] + 0.02, 0.62), 0.12, pm('bag', '#34363d', rough=0.25, spec=0.6),
                         scale=(1, 1, 0.55)))
    render_prop('city_trashcan', objs, 0.74)


@REG.add('city_cone')
def _():
    ko = pm('cone', '#ff7b2e', rough=0.45, spec=0.5)
    kw = pm('cone_w', '#f1efe6', rough=0.4, spec=0.5)
    kb = pm('cone_b', '#2c2c30', rough=0.7)
    objs = [L.box('base', 0.31, 0.31, 0.0, 0.69, 0.69, 0.035, kb, bev=0.01)]
    objs.append(L.lathe('cone', [(0, 0.03), (0.14, 0.03), (0.035, 0.52), (0.02, 0.535), (0, 0.535)], ko, segs=24))
    for z0, z1 in ((0.20, 0.27), (0.36, 0.41)):
        r0 = 0.14 - (0.14 - 0.035) * (z0 - 0.03) / 0.49 + 0.003
        r1 = 0.14 - (0.14 - 0.035) * (z1 - 0.03) / 0.49 + 0.003
        objs.append(L.lathe('band', [(0, z0), (r0, z0), (r1, z1), (0, z1)], kw, segs=24))
    render_prop('city_cone', objs, 0.535)


@cached
def tex_stripes():
    P = Paint(0, 0.8, 0, 0.8, px_per_m=256)
    t = ((P.X + P.Y) / 0.2) % 1.0
    m = P.cover(np.abs(t - 0.5) * 0.2 - 0.05, 0.004)
    col = blend(P.full(lina('#f3f0e6')), m, lina('#ff6f2a'))
    col *= (1 + 0.05 * P.noise(0.02, 995))[..., None]
    return col


def barricade(axis):
    ks = M('stripes', lambda: L.tex_mat('stripes', L.image('stripes_c', tex_stripes()), 'xz', rough=0.5, spec=0.4,
                                        scale=(0.8, 0.8), obj=True))
    kl = pm('bar_leg', '#8a9095', rough=0.5, spec=0.5, metal=0.3)
    ka = M('amber', lambda: L.simple_mat('amber', '#ffb13b', rough=0.3, spec=0.5, emit='#ffa326', emit_strength=4))
    objs = []
    for x in (0.14, 0.86):
        for sy in (-1, 1):
            objs.append(L.tube('leg', [(x, 0.5 + sy * 0.20, 0.0), (x, 0.5 + sy * 0.03, 0.80)], 0.02, kl, segs=6))
    objs.append(L.box('board', 0.06, 0.475, 0.60, 0.94, 0.51, 0.76, ks, bev=0.008))
    objs.append(L.box('board2', 0.08, 0.47, 0.34, 0.92, 0.505, 0.46, ks, bev=0.008))
    objs.append(L.box('lamp_b', 0.80, 0.47, 0.76, 0.88, 0.51, 0.79, kl))
    objs.append(L.sphere('lamp', (0.84, 0.49, 0.83), 0.04, ka, scale=(1, 0.8, 1)))
    if axis == 'y':
        K.rot_z(objs, 90)
    return objs


for ax in ('x', 'y'):
    @REG.add('city_barricade_%s' % ax)
    def _(ax=ax):
        render_prop('city_barricade_%s' % ax, barricade(ax), 0.87)


@cached
def tex_bus_sign():
    W, H = 0.34, 0.30
    P = Paint(0, W, 0, H, px_per_m=400)
    col = P.full(lina('#2f7f7f'))
    col = blend(col, P.cover(np.abs(P.sd_box(W / 2, H / 2, W / 2 - 0.012, H / 2 - 0.012, 0.02)) - 0.006, 0.003),
                lina('#eef2ee'))
    bus = P.sd_box(W / 2, H / 2 + 0.01, 0.10, 0.065, 0.02)
    col = blend(col, P.cover(bus, 0.003), lina('#eef2ee'))
    for cx in (W / 2 - 0.045, W / 2 + 0.045):
        col = blend(col, P.cover(P.sd_box(cx, H / 2 + 0.03, 0.03, 0.022, 0.006), 0.003), lina('#2f7f7f'))
        col = blend(col, P.cover(P.sd_circle(cx, H / 2 - 0.06, 0.018), 0.003), lina('#eef2ee'))
    return col


@REG.add('city_busstop')
def _():
    kp = pm('pole', '#7c8589', rough=0.45, spec=0.5, metal=0.3)
    ksign = M('bussign', lambda: L.tex_mat('bussign', L.image('bussign_c', tex_bus_sign()), 'xz', rough=0.4,
                                          spec=0.4, scale=(0.34, 0.30), offset=(0.63, 1.62), obj=True,
                                          ext='EXTEND'))
    kw = pm('slat', '#8b6a4c', rough=0.7, spec=0.3, col2='#76583e', noise=1, noise_scale=20, obj=True)
    ki = pm('iron', '#3b4644', rough=0.5, spec=0.4)
    objs = [L.cyl('pole', (0.80, 0.70), 0.03, 0.0, 2.02, kp, segs=12)]
    objs.append(L.box('sign', 0.63, 0.66, 1.62, 0.97, 0.675, 1.92, ksign, bev=0.006))
    # the bench, along X, facing the camera
    for x in (0.12, 0.62):
        objs.append(L.box('leg', x, 0.30, 0.0, x + 0.04, 0.34, 0.26, ki))
        objs.append(L.box('leg2', x, 0.52, 0.0, x + 0.04, 0.56, 0.52, ki))
    for i in range(3):
        y = 0.28 + i * 0.09
        objs.append(L.box('seat', 0.08, y, 0.26, 0.70, y + 0.075, 0.29, kw, bev=0.008))
    for z in (0.35, 0.45):
        objs.append(L.box('back', 0.08, 0.545, z, 0.70, 0.575, z + 0.07, kw, bev=0.008))
    render_prop('city_busstop', objs, 2.02)


@REG.add('city_mailbox')
def _():
    kb = pm('mail', '#3b62a3', rough=0.4, spec=0.5, col2='#32558f', noise=1, noise_scale=8)
    kd = pm('mail_dk', '#27406b', rough=0.5)
    kw = pm('mail_w', '#e7e6df', rough=0.5)
    objs = []
    for x in (0.31, 0.65):
        for y in (0.28, 0.70):
            objs.append(L.box('leg', x, y, 0.0, x + 0.05, y + 0.05, 0.30, kd))
    r = 0.21
    prof = [(0.29, 0.30), (0.71, 0.30), (0.71, 0.62)]
    for i in range(1, 12):
        a = math.pi * i / 12
        prof.append((0.5 + r * math.cos(a), 0.62 + r * math.sin(a)))
    prof.append((0.29, 0.62))
    objs.append(L.prism('box', prof, 0.25, 0.78, kb, axis='y', bev=0.012))
    objs.append(L.box('slot', 0.40, 0.245, 0.66, 0.60, 0.26, 0.70, kd, bev=0.004))
    objs.append(L.box('handle', 0.44, 0.235, 0.71, 0.56, 0.25, 0.74, kd, bev=0.004))
    objs.append(L.box('decal', 0.705, 0.40, 0.40, 0.715, 0.62, 0.56, kw))
    render_prop('city_mailbox', objs, 0.83)


def branch(objs, p, d, length, r, depth, rng, key, bounds=(0.08, 0.92)):
    from mathutils import Vector
    p = Vector(p)
    d = Vector(d).normalized()
    pts, rs = [p.copy()], [r]
    n = 4
    q = p.copy()
    for i in range(1, n + 1):
        d = (d + Vector((rng.uniform(-0.3, 0.3), rng.uniform(-0.3, 0.3), rng.uniform(-0.1, 0.25)))).normalized()
        q = q + d * (length / n)
        q.x = min(max(q.x, bounds[0]), bounds[1])
        q.y = min(max(q.y, bounds[0]), bounds[1])
        pts.append(q.copy())
        rs.append(r * (1 - 0.7 * i / n))
    objs.append(L.tube('br', pts, rs, key, segs=7))
    if depth > 0:
        for k in range(2 if depth > 1 else rng.randint(1, 2)):
            t = rng.uniform(0.45, 0.9)
            idx = int(t * n)
            nd = (d + Vector((rng.uniform(-1, 1), rng.uniform(-1, 1), rng.uniform(0.0, 0.6)))).normalized()
            branch(objs, pts[idx], nd, length * 0.6, rs[idx] * 0.8, depth - 1, rng, key, bounds)


@REG.add('city_deadtree')
def _():
    kb = pm('bark', '#5a514b', rough=0.85, spec=0.2, col2='#453e3a', noise=1, noise_scale=25, bump=0.4,
           bump_scale=35)
    rng = L.rng_for('deadtree')
    objs = [L.lathe('flare', [(0, 0), (0.17, 0), (0.12, 0.05), (0.075, 0.18), (0, 0.18)], kb, c=(0.5, 0.5, 0),
                    segs=10)]
    trunk = [(0.5, 0.5, 0.0), (0.49, 0.5, 0.5), (0.52, 0.52, 1.0), (0.55, 0.50, 1.35)]
    objs.append(L.tube('trunk', trunk, [0.085, 0.07, 0.055, 0.045], kb, segs=9))
    branch(objs, (0.52, 0.52, 1.0), (-0.8, -0.2, 0.9), 0.75, 0.04, 2, rng, kb)
    branch(objs, (0.55, 0.5, 1.33), (0.6, 0.3, 1.0), 0.8, 0.04, 2, rng, kb)
    branch(objs, (0.50, 0.5, 0.75), (0.3, -0.9, 0.6), 0.5, 0.03, 1, rng, kb)
    branch(objs, (0.55, 0.5, 1.33), (-0.1, 0.5, 1.0), 0.7, 0.035, 1, rng, kb)
    h = max(v.co.z for o in objs for v in o.data.vertices)
    render_prop('city_deadtree', objs, round(h, 2))


def bench(axis):
    kw = pm('bslat', '#8f6d4e', rough=0.7, spec=0.3, col2='#785a3f', noise=1, noise_scale=20, obj=True)
    ki = pm('biron', '#394240', rough=0.5, spec=0.45)
    objs = []
    for x in (0.10, 0.86):
        end = [(0.30, 0.0), (0.36, 0.0), (0.40, 0.25), (0.62, 0.25), (0.64, 0.0), (0.70, 0.0), (0.67, 0.30),
               (0.72, 0.62), (0.66, 0.62), (0.61, 0.31), (0.36, 0.31)]
        objs.append(L.prism('end', end, x, x + 0.04, ki, axis='x', bev=0.006))
    for i in range(4):
        y = 0.33 + i * 0.075
        objs.append(L.box('seat', 0.07, y, 0.30, 0.93, y + 0.065, 0.335, kw, bev=0.008))
    for z in (0.40, 0.51):
        objs.append(L.cbox('back', (0.5, 0.655 + (z - 0.4) * 0.12, z + 0.03), (0.86, 0.025, 0.07), kw, bev=0.008,
                           rx=-12))
    if axis == 'y':
        K.rot_z(objs, 90)
    return objs


for ax in ('x', 'y'):
    @REG.add('city_bench_%s' % ax)
    def _(ax=ax):
        render_prop('city_bench_%s' % ax, bench(ax), 0.64)


@REG.add('city_phonebooth')
def _():
    kf = pm('booth', '#6f8e8c', rough=0.4, spec=0.5, col2='#5f7c7a', noise=1, noise_scale=8)
    kg = pm('booth_glass', '#a9c6cf', rough=0.1, spec=0.6, alpha=0.32)
    ks = M('booth_sign', lambda: L.simple_mat('booth_sign', '#d6ecff', rough=0.3, emit='#bfe3ff', emit_strength=2.0))
    kph = pm('phone', '#2b2e33', rough=0.4, spec=0.5)
    kc = pm('phone_chrome', '#b0b5b9', rough=0.3, spec=0.6, metal=0.6)
    x0, x1, y0, y1 = 0.14, 0.86, 0.14, 0.86
    objs = [L.box('base', x0 - 0.02, y0 - 0.02, 0.0, x1 + 0.02, y1 + 0.02, 0.06, kf, bev=0.01)]
    for x in (x0, x1 - 0.05):
        for y in (y0, y1 - 0.05):
            objs.append(L.box('post', x, y, 0.06, x + 0.05, y + 0.05, 1.92, kf, bev=0.008))
    objs.append(L.box('roof', x0 - 0.02, y0 - 0.02, 1.92, x1 + 0.02, y1 + 0.02, 2.10, kf, bev=0.015))
    objs.append(L.box('roofcap', x0 + 0.04, y0 + 0.04, 2.10, x1 - 0.04, y1 - 0.04, 2.14, kf, bev=0.01))
    # the lit band under the roof on the front and right faces (no text)
    objs.append(L.box('sign_f', x0 + 0.06, y0 - 0.025, 1.95, x1 - 0.06, y0 - 0.015, 2.06, ks))
    objs.append(L.box('sign_r', x1 + 0.015, y0 + 0.06, 1.95, x1 + 0.025, y1 - 0.06, 2.06, ks))
    # glass: front (a folding door), right and left sides; the back is solid with the phone
    objs.append(L.box('gl_f', x0 + 0.05, y0 + 0.02, 0.10, x1 - 0.05, y0 + 0.03, 1.90, kg))
    objs.append(L.box('gl_r', x1 - 0.03, y0 + 0.05, 0.10, x1 - 0.02, y1 - 0.05, 1.90, kg))
    objs.append(L.box('gl_l', x0 + 0.02, y0 + 0.05, 0.10, x0 + 0.03, y1 - 0.05, 1.90, kg))
    objs.append(L.box('back', x0 + 0.05, y1 - 0.05, 0.06, x1 - 0.05, y1 - 0.02, 1.92, kf))
    for z in (0.95, 1.40):
        objs.append(L.box('bar_f', x0 + 0.05, y0 + 0.015, z, x1 - 0.05, y0 + 0.035, z + 0.03, kf))
        objs.append(L.box('bar_r', x1 - 0.035, y0 + 0.05, z, x1 - 0.015, y1 - 0.05, z + 0.03, kf))
    objs.append(L.box('phone', 0.40, y1 - 0.12, 1.05, 0.60, y1 - 0.05, 1.40, kph, bev=0.01))
    objs.append(L.cbox('handset', (0.44, y1 - 0.13, 1.28), (0.05, 0.04, 0.18), kph, bev=0.012))
    objs.append(L.box('coin', 0.52, y1 - 0.125, 1.30, 0.57, y1 - 0.12, 1.36, kc))
    objs.append(L.tube('cord', [(0.44, y1 - 0.13, 1.19), (0.46, y1 - 0.16, 1.05), (0.50, y1 - 0.13, 1.08),
                                (0.52, y1 - 0.12, 1.12)], 0.008, kph, segs=5))
    # the interior light (warm white) and the pool it throws out of the booth
    spot = L.add_spot((0.5, 0.5, 1.86), color=lin('#fff1d0'), power=60.0, cone_deg=64, blend=1.0)
    pt = C.add_point_light((0.5, 0.45, 1.80), color=lin('#fff1d0'), power=18.0, radius=0.05)
    objs.append(L.box('lamp', 0.38, 0.38, 1.90, 0.62, 0.62, 1.915,
                      M('booth_lamp', lambda: L.emit_mat('booth_lamp', '#fff4dc', 3.0))))
    objs[-1].visible_diffuse = False
    render_prop('city_phonebooth', objs, 2.14, [spot, pt], glow_R=1.1, glow_c=(0.5, 0.5), pool_lights=[spot],
                extra={'light_at': [0.5, 0.5, 1.86]})


def tyre(name, c, key, rout=0.30, rin=0.14, w=0.20, rot=None):
    prof = [(rin, -w * 0.35), (rin + 0.02, -w / 2), (rout - 0.03, -w / 2), (rout, -w * 0.3), (rout, w * 0.3),
            (rout - 0.03, w / 2), (rin + 0.02, w / 2), (rin, w * 0.35)]
    bm_obj = L.lathe(name, prof + [prof[0]], key, c=(0, 0, 0), segs=24, smooth=True, caps=False)
    if rot:
        bm_obj.rotation_euler = rot
    bm_obj.location = c
    return bm_obj


@REG.add('city_tires')
def _():
    kt = pm('tyre_r', '#2b2c30', rough=0.8, spec=0.25, bump=0.4, bump_scale=60)
    objs = []
    for i, (dx, dy) in enumerate(((0.0, 0.0), (0.03, -0.02), (-0.02, 0.02), (0.02, 0.01))):
        objs.append(tyre('t', (0.46 + dx, 0.56 + dy, 0.10 + i * 0.20), kt, rout=0.29))
    # one leaning against the stack at the front
    objs.append(tyre('lean', (0.60, 0.20, 0.26), kt, rout=0.26, rin=0.13, w=0.17,
                     rot=(math.radians(72), 0, math.radians(8))))
    render_prop('city_tires', objs, 0.90)


@REG.add('city_newsbox')
def _():
    kr = pm('news', '#c8563f', rough=0.4, spec=0.5, col2='#b24a37', noise=1, noise_scale=9)
    kd = pm('news_dk', '#3c3f45', rough=0.5, spec=0.4)
    kg = pm('news_glass', '#3a4c55', rough=0.1, spec=0.7)
    kp = pm('newspaper', '#dcd9cf', rough=0.8)
    objs = [L.box('foot', 0.36, 0.36, 0.0, 0.64, 0.64, 0.03, kd, bev=0.006),
            L.box('ped', 0.45, 0.45, 0.03, 0.55, 0.55, 0.34, kd)]
    prof = [(0.30, 0.34), (0.72, 0.34), (0.72, 0.80), (0.30, 0.86)]
    objs.append(L.prism('box', prof, 0.28, 0.72, kr, axis='x', bev=0.012))
    objs.append(L.box('win', 0.34, 0.275, 0.52, 0.66, 0.29, 0.76, kg))
    objs.append(L.box('paper', 0.38, 0.29, 0.54, 0.62, 0.295, 0.70, kp))
    objs.append(L.box('coin', 0.52, 0.27, 0.80, 0.66, 0.34, 0.90, kd, bev=0.008))
    objs.append(L.box('handle', 0.44, 0.265, 0.46, 0.56, 0.28, 0.49, kd))
    render_prop('city_newsbox', objs, 0.91)


@cached
def tex_street_sign():
    P = Paint(0, 0.5, 0, 0.12, px_per_m=400)
    col = P.full(lina('#2f6f52'))
    m = P.cover(np.abs(P.sd_box(0.25, 0.06, 0.235, 0.045, 0.012)) - 0.005, 0.002)
    col = blend(col, m, lina('#e9eee8'))
    return col


@REG.add('city_sign_x')
def _():
    kp = pm('spole', '#7c8589', rough=0.45, spec=0.5, metal=0.3)
    img = L.image('ssign_c', tex_street_sign())
    ka = M('ssign_x', lambda: L.tex_mat('ssign_x', img, 'xz', rough=0.4, scale=(0.5, 0.12), obj=True, ext='EXTEND',
                                         offset=(0.30, 1.98)))
    kb = M('ssign_y', lambda: L.tex_mat('ssign_y', img, 'yz', rough=0.4, scale=(0.5, 0.12), obj=True, ext='EXTEND',
                                         offset=(0.30, 2.12)))
    # a pole bent by a car: straight, a kink, then leaning
    pts = [(0.45, 0.5, 0.0), (0.45, 0.5, 0.8), (0.47, 0.49, 1.05), (0.56, 0.47, 1.60), (0.62, 0.46, 2.05)]
    objs = [L.tube('pole', pts, 0.028, kp, segs=10)]
    objs.append(L.cyl('foot', (0.45, 0.5), 0.05, 0.0, 0.05, kp, segs=12))
    blade_x = L.box('blade_x', 0.30, 0.435, 1.98, 0.80, 0.45, 2.10, ka, bev=0.004)
    blade_y = L.box('blade_y', 0.61, 0.30, 2.12, 0.625, 0.80, 2.24, kb, bev=0.004)
    objs += [blade_x, blade_y]
    # the blades hang crooked
    from mathutils import Matrix, Vector
    piv = Vector((0.62, 0.46, 2.10))
    K.transform([blade_x], Matrix.Translation(piv) @ Matrix.Rotation(math.radians(-9), 4, 'Y') @
                Matrix.Translation(-piv))
    K.transform([blade_y], Matrix.Translation(piv) @ Matrix.Rotation(math.radians(6), 4, 'X') @
                Matrix.Translation(-piv))
    objs.append(L.cyl('cap', (0.62, 0.46), 0.03, 2.24, 2.28, kp, segs=10))
    render_prop('city_sign_x', objs, 2.28)


def fence(axis):
    kp = pm('fpost', '#8c9397', rough=0.4, spec=0.55, metal=0.4)
    km = chainlink_mat()
    objs = []
    H = 1.15
    for x in (0.04, 0.96):
        objs.append(L.cyl('post', (x, 0.5), 0.028, 0.0, H + 0.04, kp, segs=10))
        objs.append(L.sphere('cap', (x, 0.5, H + 0.05), 0.033, kp, scale=(1, 1, 0.7), segs=10, rings=6))
    objs.append(L.tube('rail', [(0.04, 0.5, H), (0.96, 0.5, H)], 0.018, kp, segs=8, caps=False))
    # the mesh, torn open at the bottom right and a corner peeled out towards the camera
    outline = [(0.06, 0.04), (0.60, 0.04), (0.66, 0.16), (0.74, 0.12), (0.80, 0.30), (0.94, 0.36),
               (0.94, H - 0.02), (0.06, H - 0.02)]
    objs.append(L.prism('mesh', outline, 0.498, 0.502, km, axis='y'))
    flap = C.mesh_object('flap', [(0.66, 0.5, 0.16), (0.94, 0.5, 0.36), (0.93, 0.36, 0.18), (0.70, 0.40, 0.06)],
                         [(0, 1, 2, 3)], km)
    objs.append(flap)
    objs.append(L.tube('wire', [(0.06, 0.5, 0.05), (0.40, 0.5, 0.035), (0.60, 0.5, 0.04)], 0.006, kp, segs=5))
    if axis == 'y':
        K.rot_z(objs, 90)
    return objs


for ax in ('x', 'y'):
    @REG.add('city_fence_%s' % ax)
    def _(ax=ax):
        render_prop('city_fence_%s' % ax, fence(ax), 1.20, extra={'joins': ax})


# ===========================================================================
# phase 2: the city's moving things
# ===========================================================================

# runaway car: recoloured (light + id + z + shadow, neutral light). Region ids
# (SPEC 6.5): 1 paint A, 2 paint B, 3 glass, 4 chrome, 5 black trim, 6 tyres,
# 7 lights, 8 rust.
RC_IDS = {'paint_a': 1, 'paint_b': 2, 'glass': 3, 'chrome': 4, 'trim': 5, 'tyre': 6, 'lights': 7, 'rust': 8}


def runcar(frame):
    k = {n: pm('rc_' + n, '#cccccc', rough=r, spec=sp, id=i, metal=mt)
         for n, i, r, sp, mt in (('paint_a', 1, 0.4, 0.5, 0.0), ('paint_b', 2, 0.4, 0.5, 0.0),
                                 ('glass', 3, 0.12, 0.8, 0.0), ('chrome', 4, 0.25, 0.8, 0.5),
                                 ('trim', 5, 0.6, 0.3, 0.0), ('tyre', 6, 0.8, 0.2, 0.0),
                                 ('lights', 7, 0.2, 0.6, 0.0), ('rust', 8, 0.85, 0.2, 0.0))}
    objs = car_body(k['paint_a'], k['rust'], k['glass'], k['chrome'], k['tyre'], k['chrome'], k['lights'],
                    k['lights'], flat=False, k_cabin=k['paint_b'], k_rocker=k['trim'], k_tail=k['lights'],
                    cab_shift=-0.14)          # a long bonnet: which way it rolls reads at a glance
    # a paint-B stripe along both flanks, a black grille, rust at the wheel arches
    for y in (0.117, 0.883):
        objs.append(L.box('stripe', 0.14, y - 0.004, 0.36, 1.86, y + 0.004, 0.395, k['paint_b']))
    objs.append(L.box('grille', 1.92, 0.36, 0.23, 1.935, 0.64, 0.33, k['trim']))
    for x in (0.42, 1.55):
        objs.append(L.box('arch_rust', x - 0.2, 0.113, 0.17, x - 0.08, 0.118, 0.22, k['rust']))
    # hubcaps: three slots that turn with the wheel (the car rolls to +X:
    # +30 degrees about +Y per frame; the slots repeat every 120 degrees)
    for wx in (0.42, 1.55):
        for wy, yface in ((0.14, 0.058), (0.86, 0.942)):
            objs.append(L.box('hub', wx - 0.03, yface - 0.003, 0.125, wx + 0.03, yface + 0.003, 0.185,
                              k['chrome']))
            for j in range(3):
                phi = math.radians(20 + 30 * frame + 120 * j)
                cx, cz = wx + 0.052 * math.sin(phi), 0.155 + 0.052 * math.cos(phi)
                objs.append(L.cbox('slot', (cx, yface + (0.004 if yface < 0.5 else -0.004), cz),
                                   (0.034, 0.004, 0.018), k['trim'], ry=math.degrees(phi) - 90))
    return objs


for d in ('e', 'w'):
    for f in range(4):
        @REG.add('city_runcar_%s_%02d' % (d, f))
        def _(d=d, f=f):
            objs = runcar(f)
            if d == 'w':
                K.rot_z(objs, 180, pivot=(1.0, 0.5, 0.0))       # same footprint, facing -X
            K.render_char_like('city_runcar_%s_%02d' % (d, f), objs,
                               extra={'anim': 'roll', 'dir': d, 'frame': f, 'frames': 4, 'ms': 60,
                                      'footprint': [2, 1], 'height_m': 0.78, 'ids': RC_IDS})
            C.remove(objs)


# vent: a manhole cover and a burst of steam ------------------------------------

@cached
def tex_manhole():
    P = Paint(-0.32, 0.32, -0.32, 0.32, px_per_m=400)
    r = np.hypot(P.X, P.Y)
    col = P.full(lina('#4a4d52')) * (1 + 0.06 * P.noise(0.02, 1000))[..., None]
    h = P.zeros() + 0.4
    # raised grid of little squares inside, two rings
    g = (np.abs(((P.X / 0.05) % 1.0) - 0.5) < 0.3) & (np.abs(((P.Y / 0.05) % 1.0) - 0.5) < 0.3) & (r < 0.22)
    col = np.where(g[..., None], col * 1.35, col)
    h = np.where(g, 0.8, h)
    for rr in (0.235, 0.28):
        m = P.cover(np.abs(r - rr) - 0.008, 0.003)
        col = blend(col, m, lina('#63676d'))
        h = np.maximum(h, m * 0.9)
    rust = np.clip((P.noise(0.04, 1001) - 0.7) / 0.4, 0, 1)
    col = blend(col, rust * 0.6, lina('#7a4a30'))
    return col, h


def vent_frame(f):
    kc = M('manhole', lambda: L.tex_mat('manhole', L.image('manhole_c', tex_manhole()[0]), 'xy', rough=0.55,
                                        spec=0.4, himg=L.image('manhole_h', tex_manhole()[1], data=True),
                                        bump=0.4, scale=(0.64, 0.64), offset=(-0.32, -0.32), obj=True,
                                        ext='EXTEND'))
    kr = pm('mh_ring', '#3a3c40', rough=0.6, spec=0.4)
    kh = pm('mh_hole', '#121214', rough=1.0, spec=0.0)
    objs = [L.lathe('ring', [(0.30, 0.0), (0.345, 0.0), (0.345, 0.008), (0.30, 0.008)], kr, segs=32)]
    objs.append(L.cyl('hole', (0.5, 0.5), 0.301, 0.0, 0.004, kh, segs=32))
    #        lift  tilt  steam: (height, spread, alpha)
    seq = [(0.0, 0, None), (0.07, 9, (0.35, 0.16, 0.75)), (0.16, -14, (0.75, 0.22, 0.75)),
           (0.24, 18, (1.15, 0.28, 0.72)), (0.12, -8, (1.45, 0.33, 0.62)), (0.03, 4, (1.65, 0.36, 0.50)),
           (0.0, 0, (1.80, 0.40, 0.32)), (0.0, 0, (1.95, 0.42, 0.16))]
    lift, tilt, steam = seq[f]
    cov = L.cyl('cover', (0.0, 0.0), 0.295, -0.012, 0.012, kc, segs=32)
    cov.rotation_euler = (math.radians(tilt), math.radians(tilt * 0.6), 0)
    cov.location = (0.5, 0.5, 0.012 + lift)
    objs.append(cov)
    if steam:
        top, spread, a = steam
        ks = pm('steam_%02d' % f, '#e9ecf3', rough=0.9, spec=0.2, alpha=a)
        rng = L.rng_for('steam', f)
        n = 9 + f
        base = 0.08 if f < 6 else top * 0.45          # it lifts off the ground as it fades
        for i in range(n):
            t = (i + 0.5) / n
            z = base + (top - base) * t
            r = 0.10 + spread * (0.35 + 0.65 * t) * rng.uniform(0.6, 1.0)
            ox, oy = rng.uniform(-0.09, 0.09) * (1 + t), rng.uniform(-0.09, 0.09) * (1 + t)
            ob = L.sphere('puff', (0.5 + ox, 0.5 + oy, z), r * 0.6, ks, segs=14, rings=8)
            ob.visible_shadow = True
            objs.append(ob)
    return objs


for f in range(8):
    @REG.add('city_vent_%02d' % f)
    def _(f=f):
        render_prop('city_vent_%02d' % f, vent_frame(f), 0.02 if f == 0 else 2.2, kind='dyn',
                    extra={'anim': 'burst', 'frame': f, 'frames': 8, 'ms': 80})


# gate: the exit -----------------------------------------------------------------

def city_gate(f):
    kp = pm('gpost', '#8c9397', rough=0.4, spec=0.55, metal=0.4)
    km = chainlink_mat()
    kl = pm('padlock', '#e6b84a', rough=0.3, spec=0.7, metal=0.6)
    kch = pm('chain', '#a4aaae', rough=0.35, spec=0.6, metal=0.6)
    red = M('lamp_red', lambda: L.simple_mat('lamp_red', '#ff4a3d', rough=0.3, emit='#ff3a2a', emit_strength=4))
    grn = M('lamp_grn', lambda: L.simple_mat('lamp_grn', '#6dff8a', rough=0.3, emit='#40ff70', emit_strength=4))
    objs = []
    H = 1.30
    for x in (0.07, 0.93):
        objs.append(L.cyl('post', (x, 0.5), 0.045, 0.0, H, kp, segs=12))
        objs.append(L.cyl('cap', (x, 0.5), 0.055, H, H + 0.03, kp, segs=12))
        lamp = L.sphere('lamp', (x, 0.5, H + 0.07), 0.045, grn if f == 6 else red)
        lamp.visible_diffuse = False
        objs.append(lamp)
    angles = [0, 4, 25, 50, 75, 95, 105]
    leaf = []
    hx, x1 = 0.125, 0.885
    z0, z1 = 0.07, 1.18
    frame_pts = [(hx, 0.5, z0), (x1, 0.5, z0), (x1, 0.5, z1), (hx, 0.5, z1), (hx, 0.5, z0)]
    leaf.append(L.tube('frame', frame_pts, 0.022, kp, segs=8, caps=False))
    leaf.append(L.tube('brace', [(hx, 0.5, z0), (x1, 0.5, z1)], 0.016, kp, segs=6))
    leaf.append(L.box('mesh', hx, 0.498, z0, x1, 0.502, z1, km))
    for zz in (0.25, 1.0):
        leaf.append(L.cyl('hinge', (hx - 0.035, 0.5), 0.03, zz, zz + 0.07, kp, segs=8))
    if f == 0:
        # a chain round the leaf and the post, and the padlock hanging on it
        for i in range(7):
            a = 2 * math.pi * i / 7
            leaf.append(L.sphere('link', (0.91 + 0.05 * math.cos(a), 0.5 + 0.05 * math.sin(a), 0.64 + 0.01 * i),
                                 0.018, kch, scale=(1.3, 1.3, 0.7), segs=8, rings=5))
        leaf.append(L.box('lock', 0.87, 0.435, 0.48, 0.95, 0.465, 0.57, kl, bev=0.012))
        leaf.append(L.tube('shackle', [(0.885, 0.45, 0.57), (0.89, 0.45, 0.62), (0.935, 0.45, 0.62),
                                       (0.935, 0.45, 0.57)], 0.009, kch, segs=6))
    K.rot_z(leaf, angles[f], pivot=(hx - 0.035, 0.5, 0.0))
    objs += leaf
    if f >= 1:
        # the padlock falls (01) and lies open on the ground with its chain
        lz = 0.34 if f == 1 else 0.035
        lock = L.cbox('lock', (0.84, 0.36, lz), (0.08, 0.03, 0.09), kl, bev=0.012, ry=25 if f == 1 else 80)
        objs.append(lock)
        for i in range(6):
            objs.append(L.sphere('link', (0.72 + 0.03 * i, 0.30 + 0.015 * math.sin(i), 0.015 if f > 1 else lz + 0.1 + 0.03 * i),
                                 0.017, kch, scale=(1.3, 1.3, 0.7), segs=8, rings=5))
    lights, pool = [], None
    if f == 6:
        # open: the lamps turn green and warm light spills over the threshold
        kgl = M('exit_glow', lambda: L.emit_mat('exit_glow', '#ffc860', 1.5))
        strip = L.box('glow', 0.13, 0.44, 0.0, 0.87, 0.56, 0.012, kgl)
        strip.visible_diffuse = False
        strip.visible_shadow = False
        objs.append(strip)
        pool = L.add_spot((0.5, 0.5, 1.2), color=lin('#ffe6a0'), power=45.0, cone_deg=120, blend=1.0)
        lights = [pool]
    return objs, lights


for f in range(7):
    @REG.add('city_gate_%02d' % f)
    def _(f=f):
        objs, lights = city_gate(f)
        ex = {'anim': 'open', 'frame': f, 'frames': 7, 'ms': 100, 'exit': True}
        if lights:
            render_prop('city_gate_%02d' % f, objs, 1.42, lights, glow_R=1.2, glow_c=(0.5, 0.5), kind='dyn', extra=ex)
        else:
            render_prop('city_gate_%02d' % f, objs, 1.42, kind='dyn', extra=ex)


# crate: wooden, pushable, exactly one floor ---------------------------------------

@cached
def tex_crate_planks(top=False):
    """Planks: 4 per floor on the sides (u across), along X on the top."""
    if top:
        P = Paint(0, 1, 0, 1)
        fv = (P.Y / 0.25) % 1.0
        dv = np.minimum(fv, 1 - fv) * 0.25
    else:
        P = Paint(0, 1, 0, FM)
        fv = (P.Y / (FM / 4)) % 1.0
        dv = np.minimum(fv, 1 - fv) * FM / 4
    rng = L.rng_for('planks', 1 if top else 0)
    idx = np.floor(P.Y / (0.25 if top else FM / 4)).astype(int)
    col = np.zeros((P.h, P.w, 3), np.float32)
    for i in range(int(idx.max()) + 1):
        col[idx == i] = lina('#b08352') * rng.uniform(0.88, 1.08)
    grain = P.noise(0.01, 1010 + top, aniso=(0.15, 1.0))
    col *= (1 + 0.10 * grain)[..., None]
    gap = P.cover(dv - 0.005, 0.003)
    col = blend(col, gap, lina('#4a3524'))
    return col, 0.5 + 0.1 * grain - 0.4 * gap


@REG.add('city_crate')
def _():
    k = mat_block('crate', tex_pair('crate_top', tex_crate_planks(True)), tex_pair('crate_side', tex_crate_planks()),
                  z0=0.0, z1=FM, rough=0.75, spec=0.3, bump=0.3, bump_dist=0.01)
    kf = pm('crate_frame', '#8c6238', rough=0.75, spec=0.3, col2='#7a5430', noise=1, noise_scale=16, obj=True)
    kn = pm('crate_nail', '#5a5a5e', rough=0.4, spec=0.6)
    e, t = 0.02, 0.022
    objs = [L.box('body', e + t * 0.6, e + t * 0.6, 0.0, 1 - e - t * 0.6, 1 - e - t * 0.6, FM, k, bev=0.006)]
    # the frame: vertical corner posts and a rim at the top and the bottom
    for x in (e, 1 - e - 0.07):
        for y in (e, 1 - e - 0.07):
            objs.append(L.box('corner', x, y, 0.0, x + 0.07, y + 0.07, FM, kf, bev=0.008))
    for z in (0.0, FM - 0.065):
        objs.append(L.box('rim_f', e, e, z, 1 - e, e + 0.03, z + 0.065, kf, bev=0.006))
        objs.append(L.box('rim_r', 1 - e - 0.03, e, z, 1 - e, 1 - e, z + 0.065, kf, bev=0.006))
        objs.append(L.box('rim_b', e, 1 - e - 0.03, z, 1 - e, 1 - e, z + 0.065, kf, bev=0.006))
        objs.append(L.box('rim_l', e, e, z, e + 0.03, 1 - e, z + 0.065, kf, bev=0.006))
    # diagonal braces on the front and the right
    ang = math.degrees(math.atan2(FM - 0.13, 1 - 2 * e - 0.14))
    objs.append(L.cbox('brace_f', (0.5, e + 0.012, FM / 2), (0.98, 0.022, 0.06), kf, bev=0.005, ry=-ang))
    objs.append(L.cbox('brace_r', (1 - e - 0.012, 0.5, FM / 2), (0.022, 0.98, 0.06), kf, bev=0.005, rx=ang))
    for (x, y) in ((0.06, e - 0.002), (0.94, e - 0.002)):
        for z in (0.03, FM - 0.03):
            objs.append(L.box('nail', x - 0.008, y - 0.004, z - 0.008, x + 0.008, y, z + 0.008, kn))
    render_prop('city_crate', objs, FM, kind='dyn', extra={'pushable': True, 'top_floor': 1})


K.main('city')
