"""Monster Hop - Abyss Bay props and moving things (procedural meshes), used
by zones_bay_tiles.py. Built on cell (0, 0), floor 0, base centred on
(0.5, 0.5); multi-cell props over cells (0..w-1, 0..d-1) with the anchor still
C.cell(0, 0, 0) and `footprint` in extra (SPEC 6.3).

Reuses the desert/forest helpers (zones_df_lib: lathe, tube, blob, mesh,
materials) without changing them.
"""
import math
import time

import bpy
import numpy as np
from mathutils import Matrix, Vector

import mh_common as C
import monsters_geo as G
import zones_df_lib as L
import zones_df_tex as T
from zones_df_tex import rgb

Z = 'bay'


def _mats():
    if getattr(_mats, 'done', False):
        return
    _mats.done = True
    L.attr_mat('b_vcol', rough=0.8, spec=0.25)
    L.attr_mat('b_vcol_wet', rough=0.45, spec=0.45)
    L.noise_mat('b_wood', (160, 134, 104), (124, 102, 80), scale=18, rough=0.85, spec=0.2, bump=0.3,
                aniso=(1.0, 1.0, 0.25))
    L.noise_mat('b_wood_y', (160, 134, 104), (124, 102, 80), scale=18, rough=0.85, spec=0.2, bump=0.3,
                aniso=(1.0, 0.25, 1.0))
    L.noise_mat('b_wood_x', (160, 134, 104), (124, 102, 80), scale=18, rough=0.85, spec=0.2, bump=0.3,
                aniso=(0.25, 1.0, 1.0))
    L.noise_mat('b_darkwood', (98, 78, 62), (70, 56, 46), scale=16, rough=0.85, spec=0.2, bump=0.3,
                aniso=(0.25, 1.0, 1.0))
    L.stripe_mat('b_hull', (104, 82, 64), (80, 62, 50), axis='Z', freq=12.0, rough=0.8, spec=0.2)
    L.flat('b_verdigris', (78, 150, 128), rough=0.6, spec=0.35)
    L.flat('b_cream', (224, 214, 186), rough=0.6, spec=0.3)
    L.flat('b_hollow', (22, 22, 30), rough=0.9, spec=0.1)
    L.noise_mat('b_iron', (74, 72, 76), (52, 50, 56), scale=30, rough=0.5, metal=0.7, spec=0.5, bump=0.2)
    L.noise_mat('b_rust', (150, 88, 60), (92, 60, 48), scale=26, rough=0.8, metal=0.2, spec=0.3, bump=0.3)
    L.noise_mat('b_rope', (214, 186, 132), (176, 146, 100), scale=60, rough=0.9, spec=0.15, bump=0.4)
    L.noise_mat('b_rock', (122, 128, 138), (88, 94, 106), scale=7, rough=0.8, spec=0.3, bump=0.4)
    L.flat('b_barn', (220, 214, 196), rough=0.7, spec=0.3)
    L.flat('b_barn_d', (60, 56, 56), rough=0.9, spec=0.1)
    L.flat('b_alga', (46, 112, 88), rough=0.5, spec=0.4)
    L.flat('b_kelp', (58, 128, 84), rough=0.5, spec=0.4)
    L.flat('b_bio_c', (150, 255, 236), rough=0.4, emit=(90, 255, 230), emit_strength=3.0)
    L.flat('b_bio_m', (255, 170, 240), rough=0.4, emit=(255, 90, 220), emit_strength=3.0)
    L.flat('b_white', (232, 232, 226), rough=0.55, spec=0.35)
    L.flat('b_red', (196, 52, 50), rough=0.5, spec=0.4)
    L.flat('b_black', (44, 48, 58), rough=0.45, spec=0.45, metal=0.3)
    L.stripe_mat('b_tower', (234, 234, 228), (196, 50, 50), axis='Z', freq=1.55, rough=0.55, spec=0.35,
                 offset=-0.02)
    L.flat('b_lamp', (255, 244, 200), rough=0.2, emit=(255, 232, 160), emit_strength=9.0)
    L.flat('b_window', (255, 220, 150), rough=0.3, emit=(255, 200, 120), emit_strength=2.2)
    L.flat('b_lamp_g', (190, 255, 200), rough=0.2, emit=(90, 255, 140), emit_strength=6.0)
    L.flat('b_glass', (140, 190, 210), rough=0.1, spec=0.8)
    L.noise_mat('b_crate', (184, 146, 102), (150, 116, 80), scale=20, rough=0.8, spec=0.2, bump=0.3,
                aniso=(1.0, 1.0, 0.3))
    L.noise_mat('b_net', (58, 92, 96), (40, 66, 72), scale=90, rough=0.9, spec=0.1, bump=0.6)
    L.flat('b_lobster', (226, 84, 52), rough=0.4, spec=0.5)
    L.flat('b_eye', (20, 16, 18), rough=0.2, spec=0.8)
    L.flat('b_star', (236, 112, 70), rough=0.6, spec=0.3)
    L.flat('b_pool', (30, 96, 128), rough=0.1, spec=0.7)


# ---------------------------------------------------------------------------
# small shared bits
# ---------------------------------------------------------------------------

def barnacle(name, p, r, n=(0, 0, 1)):
    """A little cone with a dark hole, standing on the surface normal n."""
    n = np.asarray(n, float)
    n = n / np.linalg.norm(n)
    objs = []
    h = r * 1.1
    base = np.asarray(p, float)
    # build upright, then tilt with a mesh transform
    cone = L.lathe(name, 'b_barn', [(r, 0.0), (r * 0.85, h * 0.5), (r * 0.5, h)], n=8, center=(0, 0),
                   cap_top=False)
    hole = L.lathe(name + 'h', 'b_barn_d', [(r * 0.5, h * 0.98), (0.0, h * 0.7)], n=8, center=(0, 0),
                   cap_bot=False, cap_top=False)
    up = Vector((0, 0, 1))
    q = up.rotation_difference(Vector(tuple(n)))
    M = Matrix.Translation(Vector(tuple(base))) @ q.to_matrix().to_4x4()
    for ob in (cone, hole):
        L.transform(ob, M)
        objs.append(ob)
    return objs


def rock_colfun(base_lo=(84, 90, 102), base_hi=(126, 132, 142), alga=(46, 112, 88), wet_line=0.18):
    lo, hi, ag = rgb(*base_lo), rgb(*base_hi), rgb(*alga)

    def f(P):
        h = P[:, 2]
        nz = np.sin(P @ np.array([13.1, 9.7, 11.3])) * 0.5 + 0.5
        c = T.lerp(lo, hi, np.clip(h / 0.6 + 0.25 * nz, 0, 1))
        # dark wet algae skirt near the ground (the tide line)
        k = np.clip((wet_line - h) / 0.08 + (nz - 0.5) * 0.8, 0, 1)
        return T.lerp(c, ag * 0.8, k)
    return f


def surface_points(ob, rng, n, zmin, zmax, facing_up=0.3):
    """Random points on a mesh's faces (world = object coords here) whose
    normal points up at least `facing_up`, between zmin and zmax."""
    me = ob.data
    me.calc_loop_triangles()
    cand = []
    for tri in me.loop_triangles:
        c = tri.center
        nz = tri.normal
        if zmin <= c.z <= zmax and nz.z >= facing_up:
            cand.append((tuple(c), tuple(nz)))
    if not cand:
        return []
    idx = rng.choice(len(cand), size=min(n, len(cand)), replace=False)
    return [cand[i] for i in idx]


def scatter_barnacles(tag, ob, rng, n, zmin, zmax, rmin=0.018, rmax=0.032, facing=0.2):
    objs = []
    for k, (p, nrm) in enumerate(surface_points(ob, rng, n, zmin, zmax, facing)):
        objs += barnacle('%s_b%d' % (tag, k), p, rng.uniform(rmin, rmax), nrm)
    return objs


def glow_spots(tag, ob, rng, n, zmin, zmax, key='b_bio_c', r=(0.018, 0.03)):
    objs = []
    for k, (p, nrm) in enumerate(surface_points(ob, rng, n, zmin, zmax, -0.2)):
        rr = rng.uniform(*r)
        c = np.asarray(p) + np.asarray(nrm) * rr * 0.2
        objs.append(L.blob('%s_g%d' % (tag, k), key, c, rr, rng, amp=0.1, subdiv=1, scale=(1, 1, 0.6)))
    return objs


def kelp(name, base, h, rng, key='b_kelp', lean=(0.0, 0.0)):
    pts = [(base[0], base[1], base[2])]
    for k in range(1, 5):
        t = k / 4
        pts.append((base[0] + lean[0] * t + 0.03 * math.sin(k * 1.7), base[1] + lean[1] * t,
                    base[2] + h * t))
    return L.tube(name, key, L.catmull(pts, 10), np.linspace(0.028, 0.006, 10), n=6)


# ---------------------------------------------------------------------------
# lighthouse (2 x 2, tall, lit)
# ---------------------------------------------------------------------------

def lighthouse():
    rng = np.random.default_rng(101)
    cx, cy = 1.0, 1.0
    objs = []
    # the rocky base
    base = [((1.0, 1.0, 0.10), (0.78, 0.74, 0.30)), ((0.45, 0.55, 0.05), (0.36, 0.34, 0.22)),
            ((1.55, 0.50, 0.05), (0.36, 0.36, 0.2)), ((1.45, 1.55, 0.06), (0.40, 0.34, 0.24)),
            ((0.50, 1.48, 0.05), (0.34, 0.36, 0.22)), ((1.0, 0.36, 0.03), (0.30, 0.24, 0.16))]
    cf = rock_colfun(wet_line=0.14)
    rocks = []
    for k, (c, s) in enumerate(base):
        rocks.append(L.blob('lh_rock%d' % k, 'b_vcol', c, 1.0, rng, amp=0.18, freq=2.4, subdiv=3, scale=s,
                            flat_below=0.0, colfun=cf))
    objs += rocks
    for k, rk in enumerate(rocks[1:]):
        objs += scatter_barnacles('lh%d' % k, rk, rng, 5, 0.05, 0.3)
    objs += glow_spots('lh', rocks[3], rng, 5, 0.02, 0.25)
    objs += glow_spots('lhm', rocks[1], rng, 3, 0.02, 0.2, key='b_bio_m')
    # the tower: red and white bands, a plinth
    objs.append(L.lathe('lh_plinth', 'b_white', [(0.60, 0.22), (0.62, 0.30), (0.58, 0.40), (0.0, 0.40)], n=32,
                        center=(cx, cy)))
    prof = [(0.52, 0.38), (0.47, 1.0), (0.42, 1.6), (0.38, 2.2), (0.37, 2.36)]
    objs.append(L.lathe('lh_tower', 'b_tower', prof, n=36, center=(cx, cy)))
    # door and a window on the front, facing -Y
    objs.append(C.box('lh_door', cx - 0.13, cy - 0.53, 0.40, cx + 0.13, cy - 0.44, 0.78, 'b_darkwood'))
    top = L.lathe('lh_doortop', 'b_darkwood', [(0.13, -0.045), (0.13, 0.045), (0.0, 0.045)], n=16, center=(0, 0))
    L.transform(top, Matrix.Translation((cx, cy - 0.485, 0.78)) @ Matrix.Rotation(math.pi / 2, 4, 'X'))
    objs.append(top)
    objs.append(C.box('lh_win', cx - 0.07, cy - 0.46, 1.52, cx + 0.07, cy - 0.38, 1.70, 'b_window'))
    objs.append(C.box('lh_win2', cx + 0.35, cy - 0.08, 1.10, cx + 0.44, cy + 0.08, 1.26, 'b_window'))
    # gallery deck, railing
    objs.append(L.lathe('lh_gallery', 'b_black', [(0.30, 2.30), (0.54, 2.36), (0.54, 2.42), (0.0, 2.42)], n=36,
                        center=(cx, cy)))
    rail = [(cx + 0.52 * math.cos(t), cy + 0.52 * math.sin(t), 2.62) for t in np.linspace(0, 2 * math.pi, 37)]
    objs.append(L.tube('lh_rail', 'b_black', rail, [0.016] * len(rail), n=6, cap=False))
    for k in range(12):
        t = 2 * math.pi * k / 12
        x, y = cx + 0.52 * math.cos(t), cy + 0.52 * math.sin(t)
        objs.append(L.tube('lh_post%d' % k, 'b_black', [(x, y, 2.42), (x, y, 2.62)], [0.012, 0.012], n=5))
    # lantern room: glowing glass, bars, a red dome and a finial
    objs.append(L.lathe('lh_glass', 'b_lamp', [(0.27, 2.42), (0.27, 2.80), (0.0, 2.80)], n=24, center=(cx, cy)))
    for k in range(8):
        t = 2 * math.pi * k / 8 + 0.2
        x, y = cx + 0.28 * math.cos(t), cy + 0.28 * math.sin(t)
        objs.append(C.box('lh_bar%d' % k, x - 0.02, y - 0.02, 2.42, x + 0.02, y + 0.02, 2.80, 'b_black'))
    objs.append(L.lathe('lh_dome', 'b_red', [(0.34, 2.80), (0.33, 2.86), (0.24, 2.98), (0.10, 3.04), (0.0, 3.05)],
                        n=28, center=(cx, cy)))
    objs.append(L.lathe('lh_fin', 'b_black', [(0.03, 3.04), (0.03, 3.12), (0.05, 3.14), (0.0, 3.19)], n=10,
                        center=(cx, cy)))
    for ob in objs:
        if ob.name.startswith(('lh_glass', 'lh_win')):
            ob.visible_shadow = False
    lamps = [C.add_point_light((cx, cy, 2.62), color=(1.0, 0.92, 0.70), power=260.0, radius=0.15, name='lh_lamp')]
    return objs, 3.19, lamps


# ---------------------------------------------------------------------------
# the wreck: bow and stern, each 2 x 1 along X (stern on the left)
# ---------------------------------------------------------------------------

HULL_W = 0.42
DECK = 0.78
KEEL = -0.20
ROLL = 11.0


def _hull(tag, x0, x1, width_fn, sheer_fn, jag_end, nlen=26, nsec=14):
    """The hull between x0 and x1: a rounded U section of half-width
    width_fn(x) from the keel to the sheer line sheer_fn(x); `jag_end` is
    'x0' or 'x1', the broken end (planks at uneven lengths)."""
    verts, faces = [], []
    xs = np.linspace(x0, x1, nlen)
    rng = np.random.default_rng(len(tag))
    jag = rng.uniform(0.0, 0.16, nsec)
    for i, x in enumerate(xs):
        w = width_fn(x)
        zt = sheer_fn(x)
        for j in range(nsec):
            a = -math.pi / 2 + math.pi * j / (nsec - 1)
            y = 0.5 + w * math.sin(a)
            z = KEEL + (zt - KEEL) * (1 - math.cos(a))
            xx = x
            if jag_end == 'x0' and i == 0:
                xx = x + jag[j]
            if jag_end == 'x1' and i == nlen - 1:
                xx = x - jag[j]
            verts.append((xx, y, z))
    for i in range(nlen - 1):
        for j in range(nsec - 1):
            a = i * nsec + j
            faces.append((a, a + nsec, a + nsec + 1, a + 1))
    ob = L.mesh(tag + '_hull', verts, faces, 'b_hull', smooth=True)
    L.solidify(ob, 0.045)
    return ob


def _deck(tag, x0, x1, width_fn, sheer_fn, drop=0.08, n=20):
    verts, faces = [], []
    xs = np.linspace(x0, x1, n)
    for x in xs:
        w = width_fn(x) * 0.93
        z = sheer_fn(x) - drop
        verts += [(x, 0.5 - w, z), (x, 0.5 + w, z)]
    for i in range(n - 1):
        a = 2 * i
        faces.append((a, a + 2, a + 3, a + 1))
    ob = L.mesh(tag + '_deck', verts, faces, 'b_darkwood', smooth=False)
    L.solidify(ob, 0.03)
    return ob


def _rail(tag, x0, x1, width_fn, sheer_fn, side, n=16):
    xs = np.linspace(x0, x1, n)
    pts = [(x, 0.5 + side * width_fn(x) * 0.99, sheer_fn(x) + 0.02) for x in xs]
    return L.tube(tag + '_rail%d' % side, 'b_cream', pts, [0.032] * n, n=8)


def _roll(objs, deg=ROLL, pivot=(0.0, 0.5, 0.0)):
    M = Matrix.Translation(pivot) @ Matrix.Rotation(math.radians(deg), 4, 'X') @ Matrix.Translation(
        (-pivot[0], -pivot[1], -pivot[2]))
    for ob in objs:
        L.transform(ob, M)
    return objs


def _hull_dressing(tag, hull, rng, xs=(0.2, 1.8)):
    objs = []
    objs += scatter_barnacles(tag, hull, rng, 16, 0.0, 0.35, 0.02, 0.034, facing=-0.9)
    objs += glow_spots(tag, hull, rng, 7, 0.02, 0.35)
    objs += glow_spots(tag + 'm', hull, rng, 3, 0.02, 0.3, key='b_bio_m')
    return objs


def _front_y(width_fn, sheer_fn, x, z, out=0.03):
    """y of the hull's front (-Y) surface at (x, z), pushed `out` metres."""
    w = width_fn(x)
    zt = sheer_fn(x)
    c = 1 - (z - KEEL) / (zt - KEEL)
    c = min(1.0, max(-1.0, c))
    a = math.acos(c)
    return 0.5 - w * math.sin(a) - out


def _porthole(tag, x, z, width_fn, sheer_fn, r=0.075):
    """A brass-rimmed round porthole on the front side, glowing cyan inside."""
    y = _front_y(width_fn, sheer_fn, x, z, 0.0)
    ring = [(x + r * math.cos(t), y - 0.03, z + r * math.sin(t)) for t in np.linspace(0, 2 * math.pi, 21)]
    rim = L.tube(tag + '_ph', 'b_cream', ring, [0.022] * 21, n=6, cap=False)
    glass = L.lathe(tag + '_phg', 'b_bio_c', [(r * 0.85, 0.0), (0.0, 0.0)], n=16, center=(0, 0))
    L.transform(glass, Matrix.Translation((x, y - 0.025, z)) @ Matrix.Rotation(math.pi / 2, 4, 'X'))
    return [rim, glass]


def _hole(tag, x, z, width_fn, sheer_fn, rx=0.2, rz=0.14, seed=0):
    """A jagged dark hole stove in the front side of the hull, splintered
    plank ends round it."""
    rng = np.random.default_rng(seed)
    n = 14
    pts = []
    for k in range(n):
        t = 2 * math.pi * k / n
        f = 1.0 if k % 2 == 0 else rng.uniform(0.55, 0.75)
        xx = x + rx * f * math.cos(t)
        zz = z + rz * f * math.sin(t)
        pts.append((xx, _front_y(width_fn, sheer_fn, xx, zz, 0.012), zz))
    c = (x, _front_y(width_fn, sheer_fn, x, z, 0.012), z)
    verts = [c] + pts
    faces = [(0, 1 + k, 1 + (k + 1) % n) for k in range(n)]
    objs = [L.mesh(tag + '_hole', verts, faces, 'b_hollow', smooth=False)]
    for k in range(0, n, 2):
        p = np.asarray(pts[k])
        d = p - np.asarray(c)
        d[1] = 0
        d /= (np.linalg.norm(d) + 1e-6)
        objs.append(L.tube(tag + '_spl%d' % k, 'b_wood', [tuple(p - d * 0.02), tuple(p - d * 0.07 + np.array([0, -0.03, 0]))],
                           [0.018, 0.0], n=5))
    return objs


def _gaff_sail(tag, x0, x1, z0, z1, y, tear=0.35, seed=5):
    """A fore-and-aft sail in the XZ plane (it faces the camera), torn: a
    ragged lower edge and a hole."""
    rng = np.random.default_rng(seed)
    nu, nv = 12, 8
    rag = rng.uniform(0, tear, nu)
    verts = []
    for j in range(nv):
        v = j / (nv - 1)
        for i in range(nu):
            u = i / (nu - 1)
            top = z1 - 0.12 * u
            bot = z0 + rag[i] * (0.8 + 0.4 * math.sin(i * 1.7))
            z = top + (bot - top) * v
            x = x0 + (x1 - x0) * u * (1 - 0.25 * v)
            yy = y - 0.08 * math.sin(math.pi * v) * math.sin(math.pi * u)
            verts.append((x, yy, z))
    faces = []
    for j in range(nv - 1):
        for i in range(nu - 1):
            if 4 <= i <= 5 and 3 <= j <= 4:
                continue          # a tear in the cloth
            a = j * nu + i
            faces.append((a, a + 1, a + nu + 1, a + nu))
    s = L.mesh(tag + '_sail', verts, faces, 'b_cream', smooth=True, recalc=False)
    L.solidify(s, 0.012)
    return s


def wreck_bow():
    """The front half: the pointed bow at x = 1.95 with a bowsprit and a
    little figurehead, the broken end at x = 0.05 (ribs sticking up), a
    foremast stump with a torn jib, portholes glowing cyan, a hole stove in."""
    rng = np.random.default_rng(111)
    tag = 'bow'

    def width(x):
        t = np.clip((x - 0.8) / 1.15, 0, 1)
        return HULL_W * math.sqrt(max(1e-4, 1 - t ** 2.0))

    def sheer(x):
        return DECK + 0.30 * np.clip((x - 0.7) / 1.25, 0, 1) ** 2

    x0, x1 = 0.05, 1.96
    hull = _hull(tag, x0, x1, width, sheer, 'x0')
    deck = _deck(tag, x0 + 0.08, 1.8, width, sheer)
    objs = [hull, deck, _rail(tag, x0, x1 - 0.02, width, sheer, -1), _rail(tag, x0, x1 - 0.02, width, sheer, 1)]
    objs.append(L.tube('bow_stem', 'b_darkwood', [(1.93, 0.5, KEEL + 0.1), (1.99, 0.5, 0.7), (1.96, 0.5, 1.16)],
                       [0.05, 0.05, 0.045], n=8))
    objs.append(L.tube('bow_sprit', 'b_wood', [(1.6, 0.5, 1.02), (2.05, 0.5, 1.22), (2.4, 0.5, 1.40)],
                       [0.055, 0.045, 0.032], n=10))
    # figurehead: a round cream-and-teal seashell medallion under the bowsprit
    objs.append(L.blob('bow_fig', 'b_verdigris', (2.0, 0.5, 0.9), 0.09, rng, amp=0.02, subdiv=2,
                       scale=(0.7, 1.0, 1.1)))
    for k, y in enumerate((0.16, 0.30, 0.70, 0.84)):
        h = [0.65, 0.95, 0.85, 0.55][k]
        objs.append(L.tube('bow_rib%d' % k, 'b_darkwood', [(0.06, y, 0.1), (0.04, y + (0.5 - y) * 0.1, h),
                                                          (0.0, y + (0.5 - y) * 0.2, h + 0.12)],
                           [0.035, 0.03, 0.02], n=6))
    objs.append(L.mesh('bow_inside', [(0.07, 0.5 - width(0.07) * 0.9, 0.0), (0.07, 0.5 + width(0.07) * 0.9, 0.0),
                                      (0.07, 0.5 + width(0.07) * 0.9, sheer(0.07) - 0.1),
                                      (0.07, 0.5 - width(0.07) * 0.9, sheer(0.07) - 0.1)],
                       [(0, 1, 2, 3)], 'b_hollow'))
    # the foremast, broken, and a torn jib from it to the bowsprit
    objs.append(L.tube('bow_mast', 'b_wood', [(0.95, 0.5, DECK - 0.1), (0.97, 0.5, 1.9), (0.99, 0.5, 2.05)],
                       [0.07, 0.06, 0.05], n=10))
    objs.append(L.tube('bow_mastbrk', 'b_wood', [(0.99, 0.5, 2.05), (1.03, 0.52, 2.13), (0.98, 0.5, 2.18)],
                       [0.05, 0.03, 0.0], n=8))
    objs.append(_gaff_sail('bow', 1.02, 1.95, 1.05, 1.95, 0.47, tear=0.3, seed=11))
    objs.append(L.tube('bow_stay', 'b_rope', [(0.98, 0.5, 2.0), (2.38, 0.5, 1.39)], [0.012, 0.012], n=5))
    for k, x in enumerate((0.45, 0.8)):
        objs += _porthole('bow%d' % k, x, 0.52, width, sheer)
    objs += _hole('bow', 1.35, 0.36, width, sheer, 0.2, 0.14, seed=3)
    objs += _hull_dressing(tag, hull, rng)
    for k, x in enumerate((0.3, 1.6)):
        objs.append(kelp('bow_kelp%d' % k, (x, _front_y(width, sheer, x, 0.05, 0.0), 0.05), 0.35, rng,
                         lean=(0.05, -0.04)))
    objs = _roll(objs)
    return objs, 2.2


def wreck_stern():
    """The back half: a raised stern castle with a cabin, its windows glowing
    a spooky cyan, a lantern, the broken end at x = 1.95 (ribs), a broken
    mainmast with a torn fore-and-aft sail, portholes, a hole stove in."""
    rng = np.random.default_rng(112)
    tag = 'st'

    def width(x):
        t = np.clip((0.5 - x) / 0.45, 0, 1)
        return HULL_W * (1 - 0.18 * t ** 1.5)

    def sheer(x):
        return DECK + 0.40 * np.clip((0.75 - x) / 0.55, 0, 1) ** 1.2

    x0, x1 = 0.05, 1.95
    hull = _hull(tag, x0, x1, width, sheer, 'x1')
    deck = _deck(tag, 0.1, 1.85, width, sheer)
    objs = [hull, deck, _rail(tag, x0, x1, width, sheer, -1), _rail(tag, x0, x1, width, sheer, 1)]
    w0 = width(0.05)
    zt = sheer(0.05)
    tr = [(0.04, 0.5 - w0, 0.25), (0.04, 0.5 + w0, 0.25), (0.04, 0.5 + w0, zt + 0.02), (0.04, 0.5 - w0, zt + 0.02)]
    objs.append(L.mesh('st_transom', tr, [(0, 1, 2, 3)], 'b_darkwood'))
    L.solidify(objs[-1], 0.04)
    objs.append(L.tube('st_rudder', 'b_darkwood', [(0.02, 0.5, 0.05), (0.0, 0.5, 0.7)], [0.05, 0.04], n=6))
    # the stern cabin on the raised deck: dark planks, a cream roof trim,
    # cyan windows on the front and right faces
    zc = sheer(0.3) - 0.08
    objs.append(C.box('st_cabin', 0.12, 0.26, zc, 0.66, 0.74, zc + 0.42, 'b_darkwood'))
    objs.append(C.box('st_cabroof', 0.09, 0.23, zc + 0.42, 0.69, 0.77, zc + 0.47, 'b_darkwood'))
    objs.append(C.box('st_cabtrim', 0.08, 0.22, zc + 0.40, 0.70, 0.78, zc + 0.43, 'b_cream'))
    for k, x in enumerate((0.24, 0.48)):
        objs.append(C.box('st_cabwin%d' % k, x, 0.245, zc + 0.14, x + 0.12, 0.27, zc + 0.30, 'b_bio_c'))
    objs.append(C.box('st_cabwinr', 0.655, 0.40, zc + 0.14, 0.675, 0.56, zc + 0.30, 'b_bio_c'))
    # a lantern hanging at the cabin corner (glowing cyan: ghost light)
    objs.append(L.tube('st_lanarm', 'b_iron', [(0.66, 0.26, zc + 0.4), (0.78, 0.2, zc + 0.4)], [0.01, 0.01], n=5))
    objs.append(L.lathe('st_lantern', 'b_bio_c', [(0.04, zc + 0.20), (0.05, zc + 0.26), (0.03, zc + 0.33),
                                                  (0.0, zc + 0.34)], n=12, center=(0.78, 0.2)))
    objs.append(L.lathe('st_lantop', 'b_iron', [(0.05, zc + 0.33), (0.0, zc + 0.39)], n=12, center=(0.78, 0.2)))
    # the mainmast, broken, leaning back, with a torn fore-and-aft sail
    mb = (1.25, 0.5, DECK - 0.1)
    mt = (1.12, 0.52, 2.25)
    objs.append(L.tube('st_mast', 'b_wood', [mb, mt], [0.08, 0.06], n=10))
    objs.append(L.tube('st_mastbrk', 'b_wood', [mt, (1.10, 0.55, 2.36), (1.13, 0.52, 2.42)], [0.06, 0.03, 0.0],
                       n=8))
    objs.append(L.tube('st_gaff', 'b_wood', [(1.16, 0.5, 2.0), (1.75, 0.5, 1.82)], [0.03, 0.028], n=8))
    objs.append(L.tube('st_boom', 'b_wood', [(1.22, 0.5, 1.12), (1.9, 0.5, 1.06)], [0.032, 0.03], n=8))
    objs.append(_gaff_sail('st', 1.2, 1.76, 1.14, 1.98, 0.47, tear=0.35, seed=21))
    for y in (0.1, 0.9):
        objs.append(L.tube('st_shroud%d' % int(y * 10), 'b_rope', [(1.15, 0.5 + (y - 0.5) * 0.1, 2.1),
                                                                   (1.4, y, DECK + 0.02)], [0.012, 0.012], n=5))
    for k, y in enumerate((0.18, 0.34, 0.68, 0.82)):
        h = [0.7, 1.0, 0.9, 0.6][k]
        objs.append(L.tube('st_rib%d' % k, 'b_darkwood', [(1.94, y, 0.1), (1.96, y + (0.5 - y) * 0.1, h),
                                                         (2.0, y + (0.5 - y) * 0.2, h + 0.1)],
                           [0.035, 0.03, 0.02], n=6))
    objs.append(L.mesh('st_inside', [(1.93, 0.5 - width(1.93) * 0.9, 0.0), (1.93, 0.5 + width(1.93) * 0.9, 0.0),
                                     (1.93, 0.5 + width(1.93) * 0.9, sheer(1.93) - 0.1),
                                     (1.93, 0.5 - width(1.93) * 0.9, sheer(1.93) - 0.1)],
                       [(0, 1, 2, 3)], 'b_hollow'))
    for k, x in enumerate((0.95, 1.3)):
        objs += _porthole('st%d' % k, x, 0.5, width, sheer)
    objs += _hole('st', 0.45, 0.34, width, sheer, 0.18, 0.13, seed=7)
    objs += _hull_dressing(tag, hull, rng)
    objs.append(kelp('st_kelp', (1.65, _front_y(width, sheer, 1.65, 0.05, 0.0), 0.05), 0.3, rng, lean=(0.04, -0.03)))
    objs = _roll(objs)
    lamps = [C.add_point_light((0.78, 0.2 - 0.02, zc + 0.27), color=(0.35, 1.0, 0.9), power=5.0, radius=0.05,
                               name='st_glow')]
    return objs, 2.42, lamps


# ---------------------------------------------------------------------------
# rocks with barnacles
# ---------------------------------------------------------------------------

def _chunk(name, c, s, rng, cf, amp=0.24, freq=2.6, top=None, subdiv=3):
    ob = L.blob(name, 'b_vcol', c, 1.0, rng, amp=amp, freq=freq, subdiv=subdiv, scale=s, flat_below=0.0, colfun=cf)
    if top is not None:
        # a flattish, slightly tilted top: a rock, not a pebble
        for v in ob.data.vertices:
            lim = top + 0.06 * (v.co.x - c[0]) - 0.04 * (v.co.y - c[1])
            if v.co.z > lim:
                v.co.z = lim + (v.co.z - lim) * 0.25
        ob.data.update()
    return ob


def rock_a():
    rng = np.random.default_rng(121)
    cf = rock_colfun()
    main = _chunk('ra', (0.46, 0.54, 0.2), (0.36, 0.30, 0.46), rng, cf, top=0.56)
    b2 = _chunk('ra1', (0.68, 0.42, 0.12), (0.22, 0.2, 0.3), rng, cf, top=0.34)
    side = _chunk('ra2', (0.78, 0.24, 0.05), (0.13, 0.11, 0.12), rng, cf)
    objs = [main, b2, side]
    objs += scatter_barnacles('ra', main, rng, 18, 0.12, 0.6, facing=-0.3)
    objs += scatter_barnacles('ra1', b2, rng, 6, 0.08, 0.36, facing=-0.3)
    objs += scatter_barnacles('ra2', side, rng, 3, 0.02, 0.2)
    objs.append(kelp('ra_k1', (0.22, 0.42, 0.02), 0.3, rng, lean=(-0.08, -0.05)))
    objs.append(kelp('ra_k2', (0.30, 0.30, 0.02), 0.22, rng, lean=(-0.02, -0.08)))
    return objs, 0.62


def rock_spire():
    """A tall sea stack of stacked, leaning slabs, a skirt of barnacles and
    glowing algae (lit: the algae glow)."""
    rng = np.random.default_rng(122)
    cf = rock_colfun(wet_line=0.3)
    objs = []
    slabs = [((0.5, 0.52, 0.14), (0.42, 0.38, 0.26), 0.3), ((0.47, 0.55, 0.46), (0.33, 0.30, 0.22), 0.62),
             ((0.52, 0.52, 0.80), (0.27, 0.25, 0.22), 0.96), ((0.56, 0.49, 1.12), (0.21, 0.19, 0.2), 1.28),
             ((0.6, 0.47, 1.40), (0.14, 0.13, 0.16), 1.55)]
    parts = []
    for k, (c, s, top) in enumerate(slabs):
        parts.append(_chunk('rs%d' % k, c, s, rng, cf, amp=0.2, freq=2.4, top=top))
    objs += parts
    objs += scatter_barnacles('rs', parts[0], rng, 14, 0.06, 0.34, facing=-0.3)
    objs += scatter_barnacles('rs1', parts[1], rng, 6, 0.3, 0.62, facing=-0.3)
    objs += glow_spots('rs', parts[0], rng, 6, 0.02, 0.3)
    objs += glow_spots('rs1', parts[1], rng, 3, 0.3, 0.6)
    objs += glow_spots('rsm', parts[2], rng, 3, 0.6, 0.95, key='b_bio_m')
    objs.append(kelp('rs_k', (0.18, 0.38, 0.02), 0.34, rng, lean=(-0.08, -0.06)))
    lamps = [C.add_point_light((0.42, 0.22, 0.2), color=(0.35, 1.0, 0.9), power=2.5, radius=0.1, name='rs_glow')]
    return objs, 1.6, lamps


def tidepool():
    """A ring of low rocks around a little pool: a glowing anemone, a
    starfish, barnacles. Lit (the anemone and algae glow)."""
    rng = np.random.default_rng(123)
    cf = rock_colfun(wet_line=0.12)
    objs = []
    for k in range(9):
        t = 2 * math.pi * k / 9 + 0.3
        r = 0.33 + rng.uniform(-0.03, 0.03)
        c = (0.5 + r * math.cos(t), 0.5 + r * math.sin(t), 0.03)
        s = rng.uniform(0.10, 0.15)
        # lower at the front so the pool shows
        hz = s * (0.8 if math.sin(t) < -0.3 else 1.3)
        rk = L.blob('tp%d' % k, 'b_vcol', c, 1.0, rng, amp=0.2, freq=2.5, subdiv=3, scale=(s * 1.2, s, hz),
                    flat_below=0.0, colfun=cf)
        objs.append(rk)
        if k % 2 == 0:
            objs += scatter_barnacles('tp%d' % k, rk, rng, 3, 0.04, 0.2)
    # the water in the pool
    objs.append(L.lathe('tp_water', 'b_pool', [(0.34, 0.04), (0.0, 0.04)], n=28))
    # anemone: a glowing tuft of tentacles (magenta), and a cyan one
    for (ax, ay, key, n_) in ((0.44, 0.52, 'b_bio_m', 9), (0.60, 0.42, 'b_bio_c', 7)):
        objs.append(L.lathe('tp_an%s' % key, 'b_red' if key == 'b_bio_m' else 'b_alga',
                            [(0.05, 0.03), (0.045, 0.08), (0.0, 0.085)], n=12, center=(ax, ay)))
        for k in range(n_):
            t = 2 * math.pi * k / n_
            b = (ax + 0.03 * math.cos(t), ay + 0.03 * math.sin(t), 0.08)
            e = (ax + 0.09 * math.cos(t), ay + 0.09 * math.sin(t), 0.16 + 0.03 * math.sin(3 * t))
            objs.append(L.tube('tp_t%s%d' % (key, k), key, [b, e], [0.014, 0.0], n=5))
    # a starfish on the rim
    sx, sy = 0.62, 0.76
    for k in range(5):
        t = 2 * math.pi * k / 5 + 0.2
        objs.append(L.tube('tp_star%d' % k, 'b_star', [(sx, sy, 0.17), (sx + 0.08 * math.cos(t), sy + 0.08 * math.sin(t),
                                                                         0.15)], [0.025, 0.006], n=6))
    lamps = [C.add_point_light((0.46, 0.5, 0.22), color=(1.0, 0.4, 0.9), power=5.0, radius=0.1, name='tp_m'),
             C.add_point_light((0.60, 0.42, 0.22), color=(0.35, 1.0, 0.9), power=4.0, radius=0.1, name='tp_c')]
    return objs, 0.3, lamps


# ---------------------------------------------------------------------------
# harbour bits
# ---------------------------------------------------------------------------

def bollard():
    """A cast-iron bollard with a rope looped round it, the rope running off
    to the left and a coil on the ground."""
    objs = []
    objs.append(L.lathe('bo_body', 'b_iron', [(0.17, 0.0), (0.17, 0.05), (0.12, 0.08), (0.10, 0.30), (0.12, 0.42),
                                              (0.21, 0.46), (0.21, 0.52), (0.14, 0.56), (0.0, 0.57)], n=24))
    # rope turns round the neck
    for k, z in enumerate((0.20, 0.27)):
        ring = [(0.5 + 0.125 * math.cos(t), 0.5 + 0.125 * math.sin(t), z + 0.015 * math.sin(t))
                for t in np.linspace(0, 2 * math.pi, 25)]
        objs.append(L.tube('bo_turn%d' % k, 'b_rope', ring, [0.03] * 25, n=8, cap=False))
    # the rope off to the left and down to a coil in the front-left
    run = L.catmull([(0.38, 0.46, 0.24), (0.24, 0.40, 0.14), (0.16, 0.30, 0.04), (0.2, 0.2, 0.03)], 12)
    objs.append(L.tube('bo_run', 'b_rope', run, [0.03] * 12, n=8))
    spiral = []
    for k in range(40):
        t = k / 39 * 2.6 * 2 * math.pi
        r = 0.06 + 0.1 * k / 39
        spiral.append((0.26 + r * math.cos(t), 0.22 + r * math.sin(t) * 0.9, 0.03 + 0.012 * (k % 2)))
    objs.append(L.tube('bo_coil', 'b_rope', spiral, [0.028] * 40, n=6))
    return objs, 0.57


def crates():
    """Two stacked fishing crates, a net draped over a corner, a glass float."""
    rng = np.random.default_rng(131)
    objs = []

    def crate(tag, x0, y0, s, h, z0, rot=0.0):
        o = []
        b = 0.035
        o.append(C.box(tag + 'core', x0 + 0.02, y0 + 0.02, z0 + 0.01, x0 + s - 0.02, y0 + s - 0.02, z0 + h - 0.01,
                       'b_crate'))
        for z in (z0, z0 + h * 0.45, z0 + h - b * 1.4):
            o.append(C.box(tag + 'slat', x0, y0, z, x0 + s, y0 + s, z + b * 1.4, 'b_wood'))
        for (cx, cy) in ((x0, y0), (x0 + s - b, y0), (x0 + s - b, y0 + s - b), (x0, y0 + s - b)):
            o.append(C.box(tag + 'post', cx, cy, z0, cx + b, cy + b, z0 + h, 'b_darkwood'))
        if rot:
            for ob in o:
                L.rotate_z(ob, rot, center=(x0 + s / 2, y0 + s / 2))
        return o
    objs += crate('c1', 0.1, 0.12, 0.78, 0.48, 0.0)
    objs += crate('c2', 0.2, 0.24, 0.56, 0.40, 0.48, rot=14)
    # a coil of rope on the top crate and a cork float
    spiral = []
    for k in range(34):
        t = k / 33 * 2.4 * 2 * math.pi
        r = 0.05 + 0.08 * k / 33
        spiral.append((0.5 + r * math.cos(t), 0.52 + r * math.sin(t), 0.9 + 0.012 * (k % 2)))
    objs.append(L.tube('cr_coil', 'b_rope', spiral, [0.024] * 34, n=6))
    # a glass float (teal) in the net, and a cork float
    objs.append(L.blob('cr_cork', 'b_red', (0.82, 0.28, 0.07), 0.07, rng, amp=0.0, subdiv=2, scale=(1.3, 1.0, 0.8)))
    return objs, 0.95


def lobster_trap():
    """A slatted half-barrel trap along X with netted ends, a cartoon lobster
    peeking inside, and a buoy line."""
    rng = np.random.default_rng(141)
    objs = []
    x0, x1 = 0.14, 0.86
    R = 0.28
    cy, cz = 0.5, 0.0
    # base boards
    objs.append(C.box('lt_base', x0 - 0.02, cy - R - 0.02, 0.0, x1 + 0.02, cy + R + 0.02, 0.04, 'b_darkwood'))
    # hoops at the ends and middle
    for k, x in enumerate((x0, 0.5, x1)):
        hoop = [(x, cy + R * math.cos(t), 0.04 + R * math.sin(t)) for t in np.linspace(0, math.pi, 17)]
        objs.append(L.tube('lt_hoop%d' % k, 'b_darkwood', hoop, [0.022] * 17, n=6))
    # slats along X over the hoops
    for k in range(7):
        t = math.pi * (k + 0.5) / 7
        y = cy + (R + 0.01) * math.cos(t)
        z = 0.04 + (R + 0.01) * math.sin(t)
        objs.append(L.tube('lt_slat%d' % k, 'b_wood', [(x0 - 0.02, y, z), (x1 + 0.02, y, z)], [0.022, 0.022], n=6))
    # net ends
    for k, x in enumerate((x0, x1)):
        prof = [(R * 0.98, 0.0), (0.0, 0.0)]
        ob = L.lathe('lt_end%d' % k, 'b_net', [(R * 0.97, 0.0), (0.06, 0.0)], n=16, center=(0, 0), cap_top=False,
                     cap_bot=False)
        L.transform(ob, Matrix.Translation((x, cy, 0.04)) @ Matrix.Rotation(math.pi / 2, 4, 'Y'))
        objs.append(ob)
        del prof
    # the lobster inside (seen between the slats)
    lx, ly = 0.46, 0.5
    objs.append(L.blob('lt_lob', 'b_lobster', (lx, ly, 0.12), 0.1, rng, amp=0.03, subdiv=2, scale=(1.4, 0.8, 0.7)))
    objs.append(L.blob('lt_lobh', 'b_lobster', (lx - 0.14, ly, 0.13), 0.07, rng, amp=0.02, subdiv=2))
    for s in (-1, 1):
        objs.append(L.blob('lt_claw%d' % s, 'b_lobster', (lx - 0.25, ly + s * 0.08, 0.12), 0.05, rng, amp=0.02,
                           subdiv=2, scale=(1.4, 0.8, 0.7)))
        objs.append(L.blob('lt_eye%d' % s, 'b_eye', (lx - 0.2, ly + s * 0.03, 0.19), 0.018, rng, amp=0.0, subdiv=1))
    # a rope to a little float lying beside
    objs.append(L.tube('lt_line', 'b_rope', L.catmull([(x1, cy, 0.3), (0.92, 0.3, 0.1), (0.8, 0.16, 0.04)], 10),
                       [0.014] * 10, n=5))
    objs.append(L.blob('lt_float', 'b_red', (0.76, 0.13, 0.06), 0.06, rng, amp=0.0, subdiv=2, scale=(1.2, 1, 1)))
    return objs, 0.36


def buoy():
    """A channel buoy floating in the sea: the anchor is the waterline (the
    watch puts it on the water surface), a red float with a white band, a
    little cage tower and a green lamp on top."""
    objs = []
    objs.append(L.lathe('bu_float', 'b_red', [(0.20, -0.04), (0.31, 0.02), (0.33, 0.10), (0.30, 0.18),
                                              (0.20, 0.24), (0.0, 0.25)], n=28))
    objs.append(L.lathe('bu_band', 'b_white', [(0.335, 0.07), (0.34, 0.10), (0.335, 0.14)], n=28, cap_top=False,
                        cap_bot=False))
    L.solidify(objs[-1], 0.01)
    for k in range(4):
        t = 2 * math.pi * k / 4 + math.pi / 4
        b = (0.5 + 0.16 * math.cos(t), 0.5 + 0.16 * math.sin(t), 0.22)
        e = (0.5 + 0.06 * math.cos(t), 0.5 + 0.06 * math.sin(t), 0.78)
        objs.append(L.tube('bu_leg%d' % k, 'b_red', [b, e], [0.022, 0.018], n=6))
    for z, r in ((0.45, 0.115), (0.68, 0.075)):
        ring = [(0.5 + r * math.cos(t), 0.5 + r * math.sin(t), z) for t in np.linspace(0, 2 * math.pi, 21)]
        objs.append(L.tube('bu_ring%d' % int(z * 100), 'b_red', ring, [0.014] * 21, n=5, cap=False))
    objs.append(L.lathe('bu_cap', 'b_black', [(0.08, 0.77), (0.08, 0.80), (0.0, 0.80)], n=16))
    objs.append(L.lathe('bu_lamp', 'b_lamp_g', [(0.05, 0.80), (0.05, 0.90), (0.0, 0.93)], n=16))
    objs.append(L.lathe('bu_top', 'b_black', [(0.06, 0.93), (0.0, 0.97)], n=16, cap_bot=True))
    # a foam ring on the water
    ring = [(0.5 + 0.38 * math.cos(t), 0.5 + 0.38 * math.sin(t), 0.0) for t in np.linspace(0, 2 * math.pi, 33)]
    objs.append(L.tube('bu_foam', 'b_white', ring, [0.03] * 33, n=6, cap=False))
    for v in objs[-1].data.vertices:
        v.co.z = max(v.co.z, 0.0) * 0.4
    objs[-1].data.update()
    lamps = [C.add_point_light((0.5, 0.5, 0.86), color=(0.4, 1.0, 0.55), power=6.0, radius=0.05, name='bu_lamp')]
    objs[-3].visible_shadow = False
    return objs, 0.97, lamps


def anchor():
    """An old iron anchor stuck in the sand, leaning, the crown half buried
    (cut by a hold-out ground), a chain lying off to the right."""
    objs = []
    lean = math.radians(-24)
    pivot = Vector((0.46, 0.5, 0.0))

    def P(x, z):
        # model coordinates in the XZ plane of the anchor, leaning about Y
        v = Vector((x, 0, z))
        v = Matrix.Rotation(lean, 3, 'Y') @ v
        return tuple(pivot + v)
    objs.append(L.tube('an_shank', 'b_rust', [P(0, -0.05), P(0, 0.62)], [0.065, 0.055], n=12))
    # the crown: an arc with flukes
    arc = [P(0.34 * math.sin(t), 0.08 - 0.34 * math.cos(t)) for t in np.linspace(-1.15, 1.15, 13)]
    objs.append(L.tube('an_arc', 'b_rust', arc, [0.055] * 13, n=10))
    for s in (-1, 1):
        t = 1.15 * s
        tip = P(0.34 * math.sin(t), 0.08 - 0.34 * math.cos(t))
        tip2 = P(0.34 * math.sin(t) + 0.08 * s, 0.08 - 0.34 * math.cos(t) + 0.15)
        objs.append(G.tube('an_fluke%d' % s, [tip, tip2], [(0.10, 0.035), (0.04, 0.016)], 'b_rust', n=10,
                           nrm=(0, 1, 0)))
    # stock (the crossbar) and the ring
    st = [tuple(pivot + Matrix.Rotation(lean, 3, 'Y') @ Vector((0, y, 0.54))) for y in (-0.3, 0.3)]
    objs.append(L.tube('an_stock', 'b_darkwood', st, [0.05, 0.05], n=8))
    ringc = Vector(P(0, 0.72))
    ring = [tuple(ringc + Matrix.Rotation(lean, 3, 'Y') @ Vector((0.09 * math.cos(t), 0, 0.09 * math.sin(t))))
            for t in np.linspace(0, 2 * math.pi, 17)]
    objs.append(L.tube('an_ring', 'b_rust', ring, [0.026] * 17, n=6, cap=False))
    # the chain: alternating links from the ring down to the sand, off to the right
    path = L.catmull([tuple(ringc), (0.8, 0.44, 0.4), (0.88, 0.34, 0.06), (0.9, 0.16, 0.04)], 12)
    for k in range(len(path) - 1):
        a, b = np.asarray(path[k]), np.asarray(path[k + 1])
        c = (a + b) / 2
        d = b - a
        L_ = np.linalg.norm(d)
        d = d / L_
        ang = 0 if k % 2 == 0 else math.pi / 2
        n0 = np.cross(d, [0, 0, 1.0])
        if np.linalg.norm(n0) < 1e-3:
            n0 = np.array([1.0, 0, 0])
        n0 /= np.linalg.norm(n0)
        n1 = np.cross(d, n0)
        side = n0 * math.cos(ang) + n1 * math.sin(ang)
        pts = [tuple(c + d * (0.05 * math.cos(t)) + side * (0.026 * math.sin(t))) for t in np.linspace(0, 2 * math.pi, 11)]
        objs.append(L.tube('an_link%d' % k, 'b_iron', pts, [0.013] * 11, n=4, cap=False))
    # barnacles and a kelp strand on the arc
    rng = np.random.default_rng(151)
    for k in range(5):
        t = rng.uniform(-0.9, 0.9)
        p = P(0.30 * math.sin(t), 0.06 - 0.30 * math.cos(t) + 0.03)
        objs += barnacle('an_b%d' % k, p, rng.uniform(0.015, 0.022))
    # a hold-out ground just below z = 0 hides the buried half (render_prop_cut)
    return objs, 0.78


# ---------------------------------------------------------------------------
# more harbour bits (full set): net rack, harbour lamp, barrels
# ---------------------------------------------------------------------------

def _net_mat(key='b_netcord'):
    """Diamond-mesh cords; the holes are transparent (real holes in the
    colour, depth and shadow passes)."""
    def build(nt, neutral):
        nb = L.NB(nt)
        x, y, z = nb.objco()
        u = nb.math('MULTIPLY', nb.math('ADD', x, z), 13.0)
        v = nb.math('MULTIPLY', nb.math('SUBTRACT', x, z), 13.0)
        fu = nb.math('ABSOLUTE', nb.math('SUBTRACT', nb.math('FRACT', u), 0.5))
        fv = nb.math('ABSOLUTE', nb.math('SUBTRACT', nb.math('FRACT', v), 0.5))
        cord = nb.math('MAXIMUM', nb.math('GREATER_THAN', fu, 0.36), nb.math('GREATER_THAN', fv, 0.36))
        col = (0.8, 0.8, 0.8, 1.0) if neutral else tuple(T.rgb(196, 176, 128)) + (1.0,)
        b = nb.principled(col, 0.9, 0.2)
        tr = nb.node('ShaderNodeBsdfTransparent')
        mx = nb.node('ShaderNodeMixShader')
        nt.links.new(cord, mx.inputs[0])
        nt.links.new(tr.outputs[0], mx.inputs[1])
        nt.links.new(b, mx.inputs[2])
        return mx.outputs[0]
    return C.mat(key, build=build)


def net_rack():
    """A fishing net hung to dry between two posts, along X, cork floats on
    the top rope and a glass float caught in it."""
    rng = np.random.default_rng(171)
    _net_mat()
    objs = []
    for x in (0.1, 0.9):
        objs.append(L.tube('nr_post', 'b_darkwood', [(x, 0.5, 0.0), (x, 0.5, 1.28)], [0.05, 0.045], n=10))
        objs.append(L.lathe('nr_cap', 'b_darkwood', [(0.05, 1.28), (0.0, 1.32)], n=10, center=(x, 0.5)))
    objs.append(L.tube('nr_bar', 'b_wood', [(0.06, 0.5, 1.2), (0.94, 0.5, 1.2)], [0.035, 0.035], n=8))

    def sheet(u, v):
        x = 0.14 + 0.72 * u
        sag = 0.12 * math.sin(math.pi * u)
        z = 1.16 - (0.86 + 0.1 * math.sin(3 * u)) * v - sag * v
        y = 0.5 - 0.05 * math.sin(math.pi * v) + 0.02 * math.sin(7 * u)
        return (x, y, max(z, 0.02))
    nu, nv = 16, 12
    vs = [sheet(i / (nu - 1), j / (nv - 1)) for j in range(nv) for i in range(nu)]
    fs = [(j * nu + i, j * nu + i + 1, (j + 1) * nu + i + 1, (j + 1) * nu + i) for j in range(nv - 1)
          for i in range(nu - 1)]
    net = L.mesh('nr_net', vs, fs, 'b_netcord', smooth=True, recalc=False)
    objs.append(net)
    # the net's hem pooled on the ground
    objs.append(L.blob('nr_pool', 'b_net', (0.5, 0.46, 0.02), 1.0, rng, amp=0.2, freq=3, subdiv=3,
                       scale=(0.36, 0.14, 0.05), flat_below=0.0))
    for k in range(6):
        x = 0.18 + 0.64 * k / 5
        objs.append(L.blob('nr_cork%d' % k, 'b_red', (x, 0.47, 1.14), 0.035, rng, amp=0.0, subdiv=2,
                           scale=(1.3, 0.8, 0.8)))
    objs.append(L.blob('nr_glass', 'b_glass', (0.62, 0.44, 0.55), 0.08, rng, amp=0.0, subdiv=3))
    return objs, 1.32


def harbour_lamp():
    """A cast-iron harbour lamp post with a warm lantern and a life ring
    hanging on it."""
    objs = []
    objs.append(L.lathe('hl_base', 'b_iron', [(0.14, 0.0), (0.14, 0.06), (0.10, 0.10), (0.08, 0.30), (0.06, 0.34),
                                              (0.05, 1.62), (0.07, 1.66), (0.0, 1.67)], n=20))
    for k in range(4):
        t = math.pi / 2 * k + math.pi / 4
        objs.append(L.tube('hl_fin%d' % k, 'b_iron', [(0.5 + 0.06 * math.cos(t), 0.5 + 0.06 * math.sin(t), 0.1),
                                                      (0.5 + 0.1 * math.cos(t), 0.5 + 0.1 * math.sin(t), 0.02)],
                           [0.02, 0.02], n=5))
    hz = 1.67
    objs.append(L.lathe('hl_glass', 'b_window', [(0.08, hz + 0.02), (0.11, hz + 0.18), (0.10, hz + 0.26),
                                                 (0.0, hz + 0.26)], n=16))
    for k in range(4):
        t = math.pi / 2 * k + math.pi / 4
        objs.append(L.tube('hl_bar%d' % k, 'b_iron', [(0.5 + 0.085 * math.cos(t), 0.5 + 0.085 * math.sin(t), hz),
                                                      (0.5 + 0.115 * math.cos(t), 0.5 + 0.115 * math.sin(t),
                                                       hz + 0.18), (0.5 + 0.105 * math.cos(t),
                                                                    0.5 + 0.105 * math.sin(t), hz + 0.27)],
                           [0.012] * 3, n=5))
    objs.append(L.lathe('hl_roof', 'b_iron', [(0.15, hz + 0.26), (0.12, hz + 0.30), (0.04, hz + 0.38),
                                              (0.02, hz + 0.44), (0.0, hz + 0.46)], n=16))
    # the life ring on a hook, facing the camera
    ring = [(0.5 + 0.13 * math.cos(t), 0.5 - 0.1, 0.95 + 0.13 * math.sin(t)) for t in np.linspace(0, 2 * math.pi, 29)]
    ob = L.tube('hl_ring', 'b_red', ring, [0.045] * 29, n=10, cap=False)
    objs.append(ob)
    for k in range(4):
        t = math.pi / 2 * k + math.pi / 4
        seg = [(0.5 + 0.13 * math.cos(t + d), 0.5 - 0.1, 0.95 + 0.13 * math.sin(t + d)) for d in (-0.22, 0.22)]
        objs.append(L.tube('hl_band%d' % k, 'b_white', seg, [0.05, 0.05], n=10))
    objs.append(L.tube('hl_hook', 'b_iron', [(0.5, 0.45, 1.1), (0.5, 0.4, 1.12)], [0.012, 0.012], n=5))
    for o in objs:
        if o.name.startswith('hl_glass'):
            o.visible_shadow = False
    lamps = [C.add_point_light((0.5, 0.5, hz + 0.14), color=(1.0, 0.8, 0.5), power=40.0, radius=0.06, name='hl')]
    return objs, 2.13, lamps


def barrels():
    """Two fish barrels with iron hoops (one full of fish tails) and a keg."""
    rng = np.random.default_rng(181)
    objs = []

    def barrel(tag, cx, cy, h, r):
        prof = [(r * 0.86, 0.0), (r * 0.97, h * 0.25), (r, h * 0.5), (r * 0.97, h * 0.75), (r * 0.86, h),
                (r * 0.8, h), (0.0, h - 0.01)]
        o = [L.lathe(tag, 'b_wood', prof, n=24, center=(cx, cy))]
        for z in (0.1, 0.5, 0.9):
            rr = r * (0.86 + 0.14 * math.sin(math.pi * z)) + 0.008
            o.append(L.lathe(tag + 'h', 'b_iron', [(rr, h * z - 0.02), (rr, h * z + 0.02)], n=24, center=(cx, cy),
                             cap_top=False, cap_bot=False))
            L.solidify(o[-1], 0.01)
        return o
    objs += barrel('br1', 0.34, 0.6, 0.62, 0.2)
    objs += barrel('br2', 0.66, 0.36, 0.55, 0.18)
    # fish tails poking out of the first barrel
    for k, (dx, dy, a) in enumerate(((-0.06, 0.02, 20), (0.05, -0.04, -25), (0.02, 0.07, 5))):
        b = (0.34 + dx, 0.6 + dy, 0.6)
        objs.append(L.tube('bf%d' % k, 'b_white', [b, (b[0] + 0.02 * math.sin(math.radians(a)), b[1], 0.72)],
                           [0.03, 0.012], n=8))
        for s in (-1, 1):
            objs.append(L.tube('bft%d%d' % (k, s), 'b_white', [(b[0], b[1], 0.71),
                                                              (b[0] + s * 0.05, b[1], 0.78)], [0.012, 0.002], n=5))
    # a lid on the second
    objs.append(L.lathe('br2_lid', 'b_darkwood', [(0.17, 0.55), (0.17, 0.58), (0.0, 0.58)], n=24, center=(0.66, 0.36)))
    # a small keg lying on its side
    keg = barrel('keg', 0.0, 0.0, 0.3, 0.1)
    for o in keg:
        L.transform(o, Matrix.Translation((0.2, 0.22, 0.1)) @ Matrix.Rotation(math.pi / 2, 4, 'Y') @
                    Matrix.Translation((0, 0, -0.15)))
    objs += keg
    del rng
    return objs, 0.78


# ---------------------------------------------------------------------------
# bridges: the pier deck on stilts over the water
# ---------------------------------------------------------------------------

def pier_bridge(direction):
    """A walkable deck at z = 0: five boards across the way, two stringers
    with it, stilts at x = 0.12 / 0.88 on both sides going down into the
    water (the water surface hides them below -0.18), post tops a little
    above the deck, barnacles at the waterline."""
    rng = np.random.default_rng(161)
    objs = []
    for k in range(5):
        x = 0.02 + k * 0.196
        objs.append(C.box('pb_board', x, 0.04, -0.06, x + 0.175, 0.96, 0.0, 'b_wood_' + ('y' if direction == 'x' else 'x')))
    for y in (0.14, 0.86):
        objs.append(C.box('pb_stringer', 0.0, y - 0.045, -0.2, 1.0, y + 0.045, -0.06, 'b_darkwood'))
    for x in (0.12, 0.88):
        for y in (0.06, 0.94):
            objs.append(L.tube('pb_post', 'b_darkwood', [(x, y, -0.32), (x, y, 0.16)], [0.06, 0.055], n=10))
            objs.append(L.lathe('pb_cap', 'b_darkwood', [(0.058, 0.16), (0.04, 0.2), (0.0, 0.21)], n=10,
                                center=(x, y)))
            for k in range(3):
                t = rng.uniform(0, 2 * math.pi)
                p = (x + 0.058 * math.cos(t), y + 0.058 * math.sin(t), -0.16 + rng.uniform(-0.02, 0.03))
                objs += barnacle('pb_b', p, rng.uniform(0.014, 0.02), (math.cos(t), math.sin(t), 0.3))
            objs.append(L.lathe('pb_alga', 'b_alga', [(0.066, -0.25), (0.066, -0.14), (0.0, -0.14)], n=10,
                                center=(x, y)))
    for k in range(5):
        x = 0.02 + k * 0.196 + 0.088
        for y in (0.14, 0.86):
            objs.append(C.box('pb_nail', x - 0.011, y - 0.011, 0.0, x + 0.011, y + 0.011, 0.004, 'b_iron'))
    if direction == 'y':
        for ob in objs:
            L.rotate_z(ob, 90)
    return objs


# ---------------------------------------------------------------------------
# rendering: forest's render_prop plus a hold-out ground for buried things
# ---------------------------------------------------------------------------

def _cut_plane(z=-0.002, s=4.0):
    me = bpy.data.meshes.new('bay_cut')
    me.from_pydata([(-s, -s, z), (s, -s, z), (s, s, z), (-s, s, z)], [], [(0, 1, 2, 3)])
    ob = bpy.data.objects.new('bay_cut', me)
    C.link(ob)
    C.mat('b_cutmat', base=(0, 0, 0))
    C.assign(ob, 'b_cutmat')
    return ob


SHADOW_FLOOR = 12        # _sh values below this (under 5 % darkness) are denoiser haze: cleared


def clean_shadow(out, info, floor=SHADOW_FLOOR):
    """Clear the faint haze the shadow catcher leaves over the whole image
    (invisible on the watch, but it widens every packed row)."""
    import os
    fn = info.get('files', {}).get('sh')
    if not fn:
        return
    path = os.path.join(os.path.abspath(out), fn)
    im = bpy.data.images.load(path, check_existing=False)
    im.colorspace_settings.name = 'Non-Color'
    w, h = im.size
    px = np.empty(w * h * 4, np.float32)
    im.pixels.foreach_get(px)
    bpy.data.images.remove(im)
    a = np.round(px.reshape(h, w, 4)[::-1, :, 0] * 255).astype(np.int32)
    a[a < floor] = 0
    C.write_png(path, a.astype(np.uint8))


def render_prop_cut(a, name, objs, height_m, extra=None, glow=None, lights=(), kind='prop', shadow_z=0.0,
                    cut=True, cut_z=-0.002, passes=None):
    """render_prop with a hold-out plane at the ground so what is below it
    (a buried anchor crown, a keel in the sand) does not show."""
    t0 = time.time()
    ex = dict(height_m=round(height_m, 2))
    if extra:
        ex.update(extra)
    passes = tuple(passes or ('color', 'z', 'shadow'))
    size = None
    if glow is not None:
        passes = passes + ('glow',)
        size = L.fit_pool(objs, C.cell(0, 0, 0), glow[0], glow[1], shadow_z)
    hold = [_cut_plane(cut_z)] if cut else []
    info = C.render_sprite(a.out, name, objs, C.cell(0, 0, 0), passes=passes, samples=a.samples, holdout=hold,
                           shadow_z=shadow_z if ('shadow' in passes or glow) else None, kind=kind, extra=ex,
                           size=size)
    if glow is not None:
        L.taper_glow(a.out, name, info, glow[0], glow[1])
    clean_shadow(a.out, info)
    L.TIMES.append((name, time.time() - t0))
    print('  %-32s %3dx%-3d %.1fs' % (name, info['w'], info['h'], time.time() - t0))
    C.remove(list(objs) + list(lights) + hold)
    return info


PROPS = [('rock_low', rock_a, None, None),
         ('bollard', bollard, None, None),
         ('crates', crates, None, None),
         ('trap', lobster_trap, None, None),
         ('anchor', anchor, None, None),
         ('net', net_rack, None, {'along': 'x'}),
         ('barrels', barrels, None, None),
         ('wreck_bow', wreck_bow, None, {'footprint': [2, 1], 'pair': 'bay_wreck_stern'})]
LIT = [('lighthouse', lighthouse, ((1.0, 1.0), 2.1), {'footprint': [2, 2]}),
       ('rock_spire', rock_spire, ((0.5, 0.5), 1.2), None),
       ('tidepool', tidepool, ((0.5, 0.5), 1.1), None),
       ('lamp', harbour_lamp, ((0.5, 0.5), 1.4), None),
       ('wreck_stern', wreck_stern, ((0.3, 0.5), 1.3), {'footprint': [2, 1], 'pair': 'bay_wreck_bow'})]


def run(a, sample):
    _mats()
    for nm, fn, glow, ex in PROPS:
        name = '%s_%s' % (Z, nm)
        if L.wanted(a, name, sample):
            objs, hm = fn()
            render_prop_cut(a, name, objs, hm, extra=ex)
    for nm, fn, glow, ex in LIT:
        name = '%s_%s' % (Z, nm)
        if L.wanted(a, name, sample):
            objs, hm, lamps = fn()
            render_prop_cut(a, name, objs, hm, glow=glow, lights=lamps, extra=ex)
    name = '%s_buoy' % Z
    if L.wanted(a, name, sample):
        objs, hm, lamps = buoy()
        render_prop_cut(a, name, objs, hm, glow=((0.5, 0.5), 1.0), lights=lamps, kind='dyn',
                        extra=dict(float_z=-0.18, note='anchor = the waterline; the watch puts it on the water '
                                   'surface (z = -0.18) and may bob it'))
    for d in ('x', 'y'):
        name = '%s_bridge_%s' % (Z, d)
        if L.wanted(a, name, sample):
            render_prop_cut(a, name, pier_bridge(d), 0.21, kind='bridge', shadow_z=-0.18, cut_z=-0.185,
                            extra=dict(walk=d, deck_z=0.0, over='bay_surf_sea'))
