"""Monster Hop - Abyss Bay: the zone's moving and timed things (SPEC 13.2),
used by zones_bay_tiles.py. Final colour under the bay light, `kind='dyn'`,
built on cell (0, 0) with the anchor at C.cell(0, 0, 0).

    logfloat_w/m/e   driftwood raft pieces (top at the anchor; the watch puts
                     the anchor at z = -0.02, the sea is 0.16 below it)
    lily_00..03      the buoy-raft that sinks in turns (anchor = its deck;
                     the watch puts it at z = -0.17, the sea 0.01 below)
    piranha_00..04   the boiling piranha cell over the sea (anchor = the sea
                     surface): 00 calm (fins), 01-04 the boiling loop
    wave_x/y_00..04  a breaking wave crossing one cell towards +X / -Y (towards
                     the camera, so its curl faces the player)
    gate_00..06      the exit: iron bars under a ship's-wheel arch
    crate            the pushable cargo crate, one floor tall
    tentacle_w/m/e_00..03  the Kraken's tentacle laid along a row (w = the
                     base coming out of the water, m = middle, e = the tip)

The piranhas and the tentacle reuse monsters_bay's builders and palettes
(its main() only runs as a script).
"""
import math
import random

import numpy as np
from mathutils import Matrix, Vector as V

import mh_common as C
import monsters_bay as MB
import zones_bay_props as P
import zones_df_lib as L
from zones_df_tex import rgb

Z = 'bay'
F = C.FLOOR_M


def _mats():
    if getattr(_mats, 'done', False):
        return
    _mats.done = True
    L.noise_mat('b_drift', (184, 172, 150), (140, 128, 110), scale=16, rough=0.85, spec=0.2, bump=0.35,
                aniso=(0.2, 1.0, 1.0))
    L.stripe_mat('b_ringstripe', (214, 60, 56), (236, 236, 228), axis='X', freq=5.0, rough=0.45, spec=0.4)
    L.attr_mat('b_wave', rough=0.2, spec=0.6)
    L.flat('b_foam', (214, 240, 244), rough=0.6, spec=0.3, emit=(120, 230, 240), emit_strength=0.15)
    L.flat('b_brass', (214, 172, 84), rough=0.35, metal=0.75, spec=0.5)
    L.noise_mat('b_pillar', (150, 156, 162), (120, 126, 134), scale=9, rough=0.85, spec=0.25, bump=0.3)
    L.veil_mat('b_gateglow', (110, 255, 236), strength=2.2, alpha=0.55)


def _cut_render(a, name, objs, extra, cut_z=-0.002, shadow_z=0.0, shadow=True, glow=None, lights=()):
    passes = ('color', 'z', 'shadow') if shadow else ('color', 'z')
    P.render_prop_cut(a, name, objs, extra.pop('height_m', 0.5), extra=extra, glow=glow, lights=lights, kind='dyn',
                      shadow_z=shadow_z, cut=cut_z is not None, cut_z=cut_z if cut_z is not None else -0.002,
                      passes=passes)


# ---------------------------------------------------------------------------
# driftwood raft
# ---------------------------------------------------------------------------

RAFT_WATER = -0.16            # the sea relative to the raft's top (SPEC 12: top at -0.02)


def logfloat(part):
    rng = np.random.default_rng({'w': 1, 'm': 2, 'e': 3}[part])
    objs = []
    for k, y in enumerate((0.28, 0.5, 0.72)):
        x0 = 0.0 if part != 'w' else 0.05 + rng.uniform(0, 0.06)
        x1 = 1.0 if part != 'e' else 0.95 - rng.uniform(0, 0.06)
        z1 = -0.005 * k if part == 'm' else rng.uniform(-0.012, 0.0)
        objs.append(C.box('raft%d' % k, x0, y - 0.095, -0.2, x1, y + 0.095, z1, 'b_drift', bevel=0.02, segments=2))
    # lashings: rope wrapped round the three planks
    xs = {'w': (0.28,), 'm': (0.5,), 'e': (0.72,)}[part]
    for x in xs:
        loop = [(x, 0.16, -0.1), (x, 0.16, 0.012), (x, 0.84, 0.012), (x, 0.84, -0.1)]
        objs.append(L.tube('lash', 'b_rope', loop, [0.022] * 4, n=6))
        objs.append(L.tube('lash2', 'b_rope', [(p[0] + 0.05, p[1], p[2]) for p in loop], [0.022] * 4, n=6))
    # a couple of barnacles on the plank sides, a strand of weed trailing
    for k in range(3):
        p = (rng.uniform(0.15, 0.85), 0.28 - 0.095, rng.uniform(-0.14, -0.06))
        objs += P.barnacle('rb%d' % k, p, rng.uniform(0.014, 0.02), (0, -1, 0.2))
    if part == 'e':
        objs.append(L.tube('weed', 'b_kelp', L.catmull([(0.9, 0.3, -0.1), (1.05, 0.26, -0.13), (1.15, 0.3, -0.15)], 8),
                           np.linspace(0.02, 0.006, 8), n=6))
    return objs


# ---------------------------------------------------------------------------
# the sinking buoy-raft
# ---------------------------------------------------------------------------

LILY_WATER = -0.01            # the sea relative to the deck (anchor at z = -0.17)


def lily(f):
    """00 floating, 01-03 going under: tilting, dropping, bubbling."""
    drop = [0.0, 0.04, 0.08, 0.13][f]
    tilt = [0.0, 10.0, 18.0, 26.0][f]
    objs = []
    ring = []
    for k in range(49):
        t = 2 * math.pi * k / 48
        ring.append((0.5 + 0.36 * math.cos(t), 0.5 + 0.36 * math.sin(t), -0.05))
    objs.append(L.tube('ly_ring', 'b_ringstripe', ring, [0.1] * 49, n=14, cap=False))
    objs.append(L.lathe('ly_deck', 'b_drift', [(0.30, -0.05), (0.30, 0.0), (0.0, 0.0)], n=28))
    for k in range(4):
        y = 0.5 - 0.2 + 0.133 * k
        objs.append(C.box('ly_gap', 0.24, y - 0.004, -0.001, 0.76, y + 0.004, 0.002, 'b_hollow'))
    objs.append(L.tube('ly_rope', 'b_rope', [(0.5 + 0.47 * math.cos(t), 0.5 + 0.47 * math.sin(t), 0.02)
                                             for t in np.linspace(0, 2 * math.pi, 33)], [0.014] * 33, n=5, cap=False))
    M = Matrix.Translation((0.5, 0.5, -drop)) @ Matrix.Rotation(math.radians(tilt), 4, 'X') @ \
        Matrix.Rotation(math.radians(tilt * 0.6), 4, 'Y') @ Matrix.Translation((-0.5, -0.5, 0))
    for ob in objs:
        L.transform(ob, M)
    rng = random.Random(10 + f)
    if f:
        for k in range(3 + 3 * f):
            x, y = rng.uniform(0.2, 0.8), rng.uniform(0.2, 0.8)
            r = rng.uniform(0.02, 0.045)
            objs.append(L.blob('ly_bub%d' % k, 'b_foam', (x, y, LILY_WATER + rng.uniform(0.0, 0.08)), r,
                               np.random.default_rng(k), amp=0.0, subdiv=2))
        foam = [(0.5 + 0.44 * math.cos(t), 0.5 + 0.44 * math.sin(t), LILY_WATER + 0.004)
                for t in np.linspace(0, 2 * math.pi, 33)]
        objs.append(L.tube('ly_foam', 'b_foam', foam, [0.03] * 33, n=6, cap=False))
    return objs


# ---------------------------------------------------------------------------
# piranhas
# ---------------------------------------------------------------------------

def piranha_cell(f):
    """00 calm: three fins cutting the water; 01-04 the boiling loop: fish
    leaping on arcs (a quarter of a cycle apart), foam and bubbles."""
    objs = []
    c0 = V((0.5, 0.5, 0.0))
    rnd = random.Random(40 + f)
    if f == 0:
        for k, (x, y, h) in enumerate(((-0.22, -0.1, 30), (0.16, 0.18, -40), (0.1, -0.26, 80))):
            R = MB.rotm((0, 0, h))
            b = c0 + V((x, y, 0.0))
            objs.append(MB.G.tube('pf%d' % k, [b, b + R @ V((0, 0.08, 0.2))], [(0.016, 0.1), 0.0], 'skin', n=8,
                                  sub=1, nrm=tuple(R @ V((1, 0, 0)))))
            ring = [(b.x + 0.1 * math.cos(t), b.y + 0.07 * math.sin(t), 0.004) for t in np.linspace(0, 6.3, 21)]
            objs.append(MB.G.tube('pr%d' % k, ring, [0.014] * 21, 'foam', n=5, sub=1, cap0=0, cap1=0))
        return objs
    ph = (f - 1) / 4.0
    arcs = [((-0.34, 0.1), (0.2, -0.05), 0.0, 1.7), ((0.3, 0.28), (-0.12, -0.22), 0.33, 1.6),
            ((0.05, -0.36), (0.1, 0.3), 0.66, 1.5)]
    for k, ((xa, ya), (xb, yb), off, size) in enumerate(arcs):
        t = (ph + off) % 1.0
        if 0.08 < t < 0.92:
            x = xa + (xb - xa) * t
            y = ya + (yb - ya) * t
            z = 0.46 * math.sin(math.pi * t)
            head = math.degrees(math.atan2(xb - xa, -(yb - ya)))
            pitch = -55 * math.cos(math.pi * t)
            objs += MB.piranha_fish('pp%d' % k, c0 + V((x, y, z)), head, pitch, size)
        else:
            # just splashing in: a fin and a burst
            b = c0 + V((xa if t < 0.5 else xb, ya if t < 0.5 else yb, 0.0))
            for j in range(4):
                objs.append(MB.G.ellipsoid('ps%d_%d' % (k, j), b + V((rnd.uniform(-.06, .06), rnd.uniform(-.06, .06),
                                                                         rnd.uniform(0.05, 0.18))),
                                           (0.03, 0.03, 0.04), 'foam', seg=10, rings=6))
    for k in range(24):
        x, y = rnd.uniform(-0.4, 0.4), rnd.uniform(-0.4, 0.4)
        s = rnd.uniform(0.07, 0.13)
        objs.append(MB.G.ellipsoid('pfoam%d' % k, c0 + V((x, y, 0.0)), (s, s * 0.9, s * rnd.uniform(0.35, 0.55)),
                                   'foam', seg=12, rings=6))
    for k in range(10):
        x, y = rnd.uniform(-0.42, 0.42), rnd.uniform(-0.42, 0.42)
        s = rnd.uniform(0.02, 0.035)
        objs.append(MB.G.ellipsoid('pbub%d' % k, c0 + V((x, y, rnd.uniform(0.04, 0.22))), (s, s, s), 'foam', seg=10,
                                   rings=6))
    return objs


# ---------------------------------------------------------------------------
# breaking wave
# ---------------------------------------------------------------------------

WAVE_TOP = [
    # 00 a swell rising
    [(0.04, 0.0), (0.16, 0.08), (0.28, 0.22), (0.40, 0.30), (0.52, 0.26), (0.64, 0.14), (0.76, 0.04), (0.84, 0.0)],
    # 01 the curl: the crest leans over forward
    [(0.10, 0.0), (0.22, 0.12), (0.34, 0.32), (0.44, 0.52), (0.56, 0.62), (0.68, 0.58), (0.76, 0.46), (0.74, 0.36),
     (0.66, 0.34), (0.62, 0.22), (0.66, 0.08), (0.72, 0.0)],
    # 02 the crash: the lip slams down in front
    [(0.24, 0.0), (0.36, 0.14), (0.48, 0.30), (0.60, 0.38), (0.74, 0.34), (0.86, 0.20), (0.92, 0.06), (0.95, 0.0)],
    # 03 foam rolling on
    [(0.40, 0.0), (0.52, 0.10), (0.66, 0.15), (0.80, 0.14), (0.92, 0.08), (0.97, 0.0)],
    # 04 the foam thinning out
    [(0.60, 0.0), (0.72, 0.05), (0.84, 0.06), (0.95, 0.03), (0.98, 0.0)],
]


def wave(f, direction):
    top = [(x, z * 1.4) for x, z in WAVE_TOP[f]]
    zmax = max(z for _, z in top)
    lo, mid, hi, foam = rgb(14, 56, 96), rgb(30, 110, 150), rgb(100, 196, 214), rgb(226, 246, 248)
    ys = np.linspace(0.04, 0.96, 7)
    n = len(top)
    verts, cols = [], []
    for j, y in enumerate(ys):
        ends = 0.55 if j in (0, len(ys) - 1) else 1.0          # the wave dies down at its two ends
        zs = ends * (1.0 + 0.12 * math.sin(2 * math.pi * y * 1.5 + f))
        dx = 0.04 * math.sin(2 * math.pi * y + 0.7 * f)
        for i, (x, z) in enumerate(top):
            verts.append((x + dx, y, z * zs))
            h = z / max(zmax, 1e-3)
            c = lo if h < 0.3 else (mid if h < 0.7 else hi)
            c = c + (hi - c) * max(0.0, h - 0.5) * 0.6
            if f >= 3 or (f in (1, 2) and h > 0.8):
                c = foam
            cols.append(c)
    m = len(ys)
    faces = [tuple(range(n)), tuple(range(m * n - 1, (m - 1) * n - 1, -1))]
    for j in range(m - 1):
        for i in range(n - 1):
            a0 = j * n + i
            faces.append((a0, a0 + 1, a0 + n + 1, a0 + n))
    ob = L.mesh('wave', verts, faces, 'b_wave', smooth=True, cols=cols)
    L.subsurf(ob, 1)
    objs = [ob]
    rnd = random.Random(60 + f)
    # foam on the crest and spray
    crest = max(top, key=lambda p: p[1])
    if f in (1, 2):
        for k in range(10):
            y = 0.08 + 0.84 * k / 9
            objs.append(L.blob('wc%d' % k, 'b_foam', (crest[0] + 0.06, y, crest[1] + 0.02), 0.08,
                               np.random.default_rng(k), amp=0.2, subdiv=2, scale=(1.0, 1.4, 0.7)))
    if f in (2, 3):
        for k in range(12):
            x = rnd.uniform(0.55, 0.98)
            y = rnd.uniform(0.08, 0.92)
            z = rnd.uniform(0.1, 0.5 if f == 2 else 0.25)
            r = rnd.uniform(0.025, 0.05)
            objs.append(L.blob('ws%d' % k, 'b_foam', (x, y, z), r, np.random.default_rng(k + 20), amp=0.0, subdiv=2))
    if f >= 3:
        for k in range(8):
            x = rnd.uniform(0.35 if f == 3 else 0.6, 0.95)
            y = 0.1 + 0.8 * k / 7 + rnd.uniform(-0.04, 0.04)
            objs.append(L.blob('wf%d' % k, 'b_foam', (x, y, 0.02), 0.08, np.random.default_rng(k + 40), amp=0.25,
                               subdiv=2, scale=(1.4, 1.0, 0.4)))
    if direction == 'y':
        # wave_y rolls towards the camera (-Y): its curl faces the player
        for o in objs:
            L.rotate_z(o, -90)
    return objs, zmax


# ---------------------------------------------------------------------------
# the exit gate
# ---------------------------------------------------------------------------

def gate(f):
    """Stone pillars, an iron arch beam with a brass ship's wheel, two leaves
    of iron bars swinging in (towards +Y); open 06 glows cyan."""
    ang = [0, 16, 32, 50, 68, 84, 92][f]
    objs = []
    rng = np.random.default_rng(71)
    for x0 in (0.02, 0.82):
        objs.append(C.box('gp', x0, 0.36, 0.0, x0 + 0.16, 0.64, 1.86, 'b_pillar', bevel=0.02))
        objs.append(C.box('gpc', x0 - 0.02, 0.34, 1.86, x0 + 0.18, 0.66, 1.94, 'b_pillar', bevel=0.02))
        objs += P.scatter_barnacles('gb%d' % int(x0 * 10), objs[-2], rng, 5, 0.02, 0.5, facing=-0.9)
        objs.append(P.kelp('gk%d' % int(x0 * 10), (x0 + 0.08, 0.35, 0.02), 0.32, rng, lean=(0.02, -0.05)))
    # the arch beam and the wheel
    objs.append(C.box('gbeam', 0.1, 0.44, 1.72, 0.9, 0.56, 1.82, 'b_iron'))
    wc = V((0.5, 0.42, 2.12))
    ring = [tuple(wc + V((0.2 * math.cos(t), 0, 0.2 * math.sin(t)))) for t in np.linspace(0, 2 * math.pi, 33)]
    objs.append(L.tube('gwheel', 'b_brass', ring, [0.028] * 33, n=8, cap=False))
    objs.append(L.blob('ghub', 'b_brass', tuple(wc), 0.05, rng, amp=0.0, subdiv=2, scale=(1, 0.7, 1)))
    for k in range(8):
        t = math.pi * k / 4
        d = V((math.cos(t), 0, math.sin(t)))
        objs.append(L.tube('gspoke%d' % k, 'b_brass', [tuple(wc + d * 0.04), tuple(wc + d * 0.29)], [0.016, 0.02],
                           n=6))
        objs.append(L.blob('gknob%d' % k, 'b_brass', tuple(wc + d * 0.3), 0.026, rng, amp=0.0, subdiv=1))
    objs.append(L.tube('gwpost', 'b_iron', [(0.5, 0.46, 1.82), (0.5, 0.46, 1.92)], [0.03, 0.03], n=6))
    # the two leaves of bars, hinged on the pillars
    for side, hx in ((-1, 0.18), (1, 0.82)):
        leaf = []
        w = 0.32
        for k in range(5):
            x = hx - side * (0.04 + k * 0.07)
            leaf.append(L.tube('gbar', 'b_iron', [(x, 0.5, 0.06), (x, 0.5, 1.62)], [0.02, 0.02], n=6))
            leaf.append(L.lathe('gtip', 'b_iron', [(0.03, 1.62), (0.0, 1.72)], n=8, center=(x, 0.5)))
        for z in (0.14, 0.86, 1.52):
            leaf.append(C.box('grail', min(hx, hx - side * w), 0.48, z - 0.03, max(hx, hx - side * w), 0.52, z + 0.03,
                              'b_iron'))
        # half a porthole ring on each leaf (a whole one when closed)
        cx = 0.5
        half = [(cx + 0.14 * math.cos(t), 0.5, 1.12 + 0.14 * math.sin(t)) for t in
                np.linspace(math.pi / 2, 3 * math.pi / 2, 13)] if side < 0 else \
               [(cx + 0.14 * math.cos(t), 0.5, 1.12 + 0.14 * math.sin(t)) for t in
                np.linspace(-math.pi / 2, math.pi / 2, 13)]
        leaf.append(L.tube('gring', 'b_brass', half, [0.022] * 13, n=6))
        M = Matrix.Translation((hx, 0.5, 0)) @ Matrix.Rotation(math.radians(side * ang), 4, 'Z') @ \
            Matrix.Translation((-hx, -0.5, 0))
        for ob in leaf:
            L.transform(ob, M)
        objs += leaf
    lights = []
    if f == 6:
        v = [(0.2, 0.5, 0.0), (0.8, 0.5, 0.0), (0.8, 0.5, 1.7), (0.2, 0.5, 1.7)]
        cols = [(1, 1, 1), (1, 1, 1), (0.2, 0.2, 0.2), (0.2, 0.2, 0.2)]
        objs.append(L.mesh('gveil', v, [(0, 1, 2, 3)], 'b_gateglow', cols=cols, recalc=False))
        objs[-1].visible_shadow = False
        lights = [C.add_point_light((0.5, 0.45, 0.6), color=(0.4, 1.0, 0.92), power=40.0, radius=0.3, name='gglow')]
    return objs, lights


def cargo_crate():
    Hh = F
    objs = [C.box('cc_core', 0.06, 0.06, 0.0, 0.94, 0.94, Hh, 'b_crate')]
    b = 0.05
    for (x0, y0, x1, y1) in ((0.04, 0.04, 0.96, 0.04 + b), (0.04, 0.96 - b, 0.96, 0.96),
                             (0.04, 0.04, 0.04 + b, 0.96), (0.96 - b, 0.04, 0.96, 0.96)):
        for z in (0.0, Hh - b):
            objs.append(C.box('cc_e', x0, y0, z, x1, y1, z + b, 'b_wood'))
    for x in (0.04, 0.96 - b):
        for y in (0.04, 0.96 - b):
            objs.append(C.box('cc_p', x, y, 0.0, x + b, y + b, Hh, 'b_darkwood'))
    # a teal painted band round the middle and rope handles
    objs.append(C.box('cc_band', 0.055, 0.055, Hh * 0.42, 0.945, 0.945, Hh * 0.58, 'b_verdigris'))
    objs.append(L.tube('cc_hf', 'b_rope', L.catmull([(0.36, 0.05, Hh * 0.75), (0.5, 0.0, Hh * 0.66),
                                                     (0.64, 0.05, Hh * 0.75)], 8), [0.018] * 8, n=6))
    objs.append(L.tube('cc_hr', 'b_rope', L.catmull([(0.95, 0.36, Hh * 0.75), (1.0, 0.5, Hh * 0.66),
                                                     (0.95, 0.64, Hh * 0.75)], 8), [0.018] * 8, n=6))
    return objs


# ---------------------------------------------------------------------------
# the Kraken's tentacle along a row
# ---------------------------------------------------------------------------

TENT_Z = [0.02, 1.0, 0.13, -0.07]      # axis height per frame: rising, high, slammed, sinking
TENT_R = 0.16


def tentacle(part, f):
    zc = TENT_Z[f]
    y = 0.52
    if part == 'm':
        pts = [(-0.02, y, zc), (0.33, y - 0.02, zc + 0.02), (0.66, y + 0.02, zc - 0.01), (1.02, y, zc)]
        r0, r1 = TENT_R, TENT_R
    elif part == 'w':
        # the base: coming up out of the water at the west end
        if f == 1:
            pts = [(0.08, y, -0.2), (0.14, y, 0.35), (0.32, y, 0.82), (0.62, y, zc), (1.02, y, zc)]
        else:
            pts = [(0.06, y, zc - 0.25), (0.28, y, zc - 0.05), (0.62, y, zc), (1.02, y, zc)]
        r0, r1 = TENT_R * 1.15, TENT_R
    else:
        # the tip: tapering and curling up at the east end
        pts = [(-0.02, y, zc), (0.35, y, zc + 0.01), (0.66, y, zc + 0.05), (0.84, y, zc + 0.18), (0.8, y, zc + 0.3),
               (0.7, y, zc + 0.28)]
        r0, r1 = TENT_R, 0.035
    objs = MB.tentacle('tent', pts, r0, r1, sucker_side=(0, -1, -0.5), nsuck=4 if part != 'e' else 5)
    if part == 'm':
        pass
    rnd = random.Random(80 + f + {'w': 0, 'm': 3, 'e': 6}[part])
    if f in (0, 2, 3):
        n = {0: 6, 2: 12, 3: 5}[f]
        for k in range(n):
            x = rnd.uniform(0.05, 0.95)
            s = rnd.uniform(-1, 1)
            yy = y + (0.2 + 0.08 * abs(s)) * (1 if s > 0 else -1)
            h = 0.06 if f != 2 else rnd.uniform(0.05, 0.16)
            objs.append(MB.G.ellipsoid('tf%d' % k, (x, yy, h * 0.5), (0.08, 0.07, h), 'foam', seg=10, rings=6))
    return objs


# ---------------------------------------------------------------------------

def run(a):
    _mats()
    P._mats()
    for part in ('w', 'm', 'e'):
        name = '%s_logfloat_%s' % (Z, part)
        if L.wanted(a, name, set()):
            _cut_render(a, name, logfloat(part), dict(part=part, float_z=-0.02, height_m=0.2, footprint=[1, 1],
                                                      note='a driftwood raft piece; anchor = its top'),
                        cut_z=RAFT_WATER, shadow_z=RAFT_WATER, shadow=False)
    for f in range(4):
        name = '%s_lily_%02d' % (Z, f)
        if L.wanted(a, name, set()):
            _cut_render(a, name, lily(f), dict(anim='sink', frame=f, frames=4, float_z=-0.17, standable=(f == 0),
                                               height_m=0.12, note='a buoy-raft; anchor = its deck'),
                        cut_z=LILY_WATER, shadow_z=LILY_WATER, shadow=False)
    for f in range(5):
        name = '%s_piranha_%02d' % (Z, f)
        if L.wanted(a, name, set()):
            MB.register_mats('piranha')
            _cut_render(a, name, piranha_cell(f),
                        dict(anim='boil', frame=f, frames=5, ms=80, float_z=-0.18, over='bay_surf_sea',
                             states={'0': 'calm (safe)', '1-4': 'boiling loop (deadly)'}, height_m=0.5),
                        shadow=False)
    for d in ('x', 'y'):
        for f in range(5):
            name = '%s_wave_%s_%02d' % (Z, d, f)
            if L.wanted(a, name, set()):
                objs, hm = wave(f, d)
                _cut_render(a, name, objs, dict(anim='wave', frame=f, frames=5, ms=70, push={'x': '+x', 'y': '-y'}[d], height_m=hm),
                            shadow=False)
    for f in range(7):
        name = '%s_gate_%02d' % (Z, f)
        if L.wanted(a, name, set()):
            objs, lamps = gate(f)
            _cut_render(a, name, objs, dict(anim='open', frame=f, frames=7, height_m=2.42, exit=True,
                                            states={'0': 'closed', '6': 'open'}),
                        glow=((0.5, 0.6), 1.3) if f == 6 else None, lights=lamps, cut_z=None)
    name = '%s_crate' % Z
    if L.wanted(a, name, set()):
        _cut_render(a, name, cargo_crate(), dict(footprint=[1, 1], height_m=round(F, 4), pushable=True), cut_z=None)
    for part in ('w', 'm', 'e'):
        for f in range(4):
            name = '%s_tentacle_%s_%02d' % (Z, part, f)
            if L.wanted(a, name, set()):
                MB.register_mats('kraken')
                _cut_render(a, name, tentacle(part, f),
                            dict(anim='slam', frame=f, frames=4, part=part, height_m=1.2,
                                 states={'0': 'rising', '1': 'high (warning: its shadow marks the row)',
                                         '2': 'slammed flat (deadly)', '3': 'sinking'},
                                 note='one cell along X; the watch lays w, m..., e across the row it hits'))
