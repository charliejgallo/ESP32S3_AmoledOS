"""Monster Hop - Lost Valley (dinosaurs) props, procedural meshes, used by
zones_dino_tiles.py. Built on cell (0, 0), floor 0, base centred on (0.5, 0.5)
(multi-cell props over cells (0..w-1, 0..d-1), anchor still cell (0, 0)).

Uses the geometry helpers of zones_df_lib (tube, blob, lathe, mesh, catmull)
as they are; nothing there is changed.
"""
import math

import numpy as np

import mh_common as C
import zones_df_lib as L
import zones_df_tex as T
from zones_df_tex import rgb

Z = 'dino'


def _mats():
    if getattr(_mats, 'done', False):
        return
    _mats.done = True
    L.attr_mat('k_vcol', rough=0.8, spec=0.25)
    L.attr_mat('k_leaf', rough=0.55, spec=0.35)
    L.attr_mat('k_bark', rough=0.9, spec=0.15, bump_noise=0.5, bump_scale=50)
    L.attr_mat('k_rock', rough=0.85, spec=0.2, bump_noise=0.6, bump_scale=26)
    L.noise_mat('k_bone', (242, 232, 206), (212, 194, 160), scale=11, rough=0.6, spec=0.3, bump=0.25)
    L.noise_mat('k_bone_dirty', (226, 206, 170), (176, 146, 112), scale=7, rough=0.7, spec=0.25, bump=0.3)
    L.flat('k_socket', (62, 40, 34), rough=0.9, spec=0.1)
    L.noise_mat('k_straw', (206, 164, 96), (150, 106, 60), scale=30, rough=0.85, spec=0.2, bump=0.4,
                aniso=(1.0, 1.0, 4.0))
    L.noise_mat('k_twig', (120, 84, 56), (90, 60, 40), scale=20, rough=0.85, spec=0.2)
    L.attr_mat('k_egg', rough=0.35, spec=0.45)
    L.flat('k_cone', (232, 120, 44), rough=0.5, spec=0.35)
    L.flat('k_cone2', (196, 82, 34), rough=0.55, spec=0.3)
    L.flat('k_vine', (54, 132, 76), rough=0.6, spec=0.3)
    L.flat('k_lava', (255, 150, 50), rough=0.4, emit=(255, 120, 30), emit_strength=3.0)
    L.flat('k_lava_core', (255, 232, 150), rough=0.4, emit=(255, 214, 110), emit_strength=3.5)
    L.flat('k_earth', (150, 78, 50), rough=0.95, spec=0.15)
    L.veil_mat('k_steam', (236, 236, 230), strength=1.0, alpha=0.45)


# ---------------------------------------------------------------------------
# fronds: a comb-edged leaf along a curved spine (ferns, cycads, the tree)
# ---------------------------------------------------------------------------

def frond(name, base, phi, length, up, droop, width, c_base, c_tip, c_edge, n=24, teeth=10, comb=0.40,
          fold=0.40, key='k_leaf', tip_curl=0.0):
    t = np.linspace(0, 1, n)
    h = np.array([math.cos(phi), math.sin(phi), 0.0])
    spine = np.asarray(base, float)[None, :] + np.outer(length * t * (1 - 0.1 * t), h) + \
        np.outer(length * (up * t - droop * t * t), [0, 0, 1])
    if tip_curl:
        k = t ** 3 * tip_curl
        spine = spine - np.outer(k * length * 0.25, h) - np.outer(k * length * 0.1, [0, 0, 1])
    tan = np.gradient(spine, axis=0)
    tan /= np.linalg.norm(tan, axis=1, keepdims=True)
    side = np.cross(tan, [0, 0, 1])
    side /= np.linalg.norm(side, axis=1, keepdims=True) + 1e-9
    w = width * np.sin(math.pi * np.clip(t, 0, 1) ** 0.75) * (1 - 0.3 * t)
    w = w * ((1 - comb) + comb * np.abs(np.cos(math.pi * teeth * t)))
    w[0] = 0.012
    verts, faces, vc = [], [], []
    for k in range(n):
        dz = np.array([0, 0, -fold * w[k]])
        ck = T.lerp(c_base, c_tip, t[k] ** 1.3)
        verts += [tuple(spine[k] - side[k] * w[k] + dz), tuple(spine[k] + np.array([0, 0, 0.012])),
                  tuple(spine[k] + side[k] * w[k] + dz)]
        vc += [T.lerp(ck, c_edge, 0.5), ck * 1.08, T.lerp(ck, c_edge, 0.5)]
    for k in range(n - 1):
        a0 = 3 * k
        faces += [(a0, a0 + 1, a0 + 4, a0 + 3), (a0 + 1, a0 + 2, a0 + 5, a0 + 4)]
    return L.mesh(name, verts, faces, key, smooth=True, cols=vc, recalc=False)


def fiddlehead(name, base, phi, h, r0, col, n=40):
    """A young curled fern frond: a stalk rising and rolling into a spiral."""
    d = np.array([math.cos(phi), math.sin(phi), 0.0])
    pts, rad = [], []
    for i in range(n):
        s = i / (n - 1)
        if s < 0.55:
            u = s / 0.55
            p = np.asarray(base) + d * (0.05 * u) + np.array([0, 0, h * u])
            rr = r0 * (1 - 0.2 * u)
        else:
            u = (s - 0.55) / 0.45
            ang = u * 1.7 * 2 * math.pi
            R = 0.07 * (1 - 0.75 * u)
            c = np.asarray(base) + d * 0.05 + np.array([0, 0, h])
            p = c + d * (R * math.sin(ang)) + np.array([0, 0, R * (1 - math.cos(ang))]) + d * 0.07 * (1 - u) * 0
            rr = r0 * 0.8 * (1 - 0.6 * u)
        pts.append(p)
        rad.append(rr)
    return L.tube(name, 'k_leaf', pts, rad, n=10, cols=[col * (0.85 + 0.3 * i / n) for i in range(n)])


# ---------------------------------------------------------------------------
# 1. giant fern (jungle teal)
# ---------------------------------------------------------------------------

TEAL_D, TEAL_M, TEAL_L = rgb(20, 96, 92), rgb(40, 150, 128), rgb(130, 214, 170)


def fern():
    rng = np.random.default_rng(301)
    objs = []
    nf = 12
    for i in range(nf):
        phi = 2 * math.pi * i / nf + rng.uniform(-0.15, 0.15)
        inner = i % 2 == 0
        objs.append(frond('fr%d' % i, (0.5, 0.5, 0.02), phi, rng.uniform(0.62, 0.74) if inner else rng.uniform(0.5, 0.6),
                          rng.uniform(1.7, 2.1) if inner else rng.uniform(1.1, 1.4), rng.uniform(1.5, 1.8), 0.12,
                          TEAL_D, TEAL_L, TEAL_M, teeth=11, comb=0.5))
    for k in range(3):
        phi = 2 * math.pi * k / 3 + 0.6
        objs.append(fiddlehead('fid%d' % k, (0.5 + 0.04 * math.cos(phi), 0.5 + 0.04 * math.sin(phi), 0.0), phi,
                               0.24 + 0.05 * k, 0.018, rgb(90, 190, 130)))
    return objs, 0.95


# ---------------------------------------------------------------------------
# 2. cycad: a scaly pineapple trunk, stiff glossy fronds, an orange cone
# ---------------------------------------------------------------------------

def cycad():
    rng = np.random.default_rng(302)
    objs = []
    H = 0.55
    prof = [(0.21, 0.0), (0.22, 0.05), (0.20, 0.2), (0.18, 0.38), (0.16, H), (0.10, H + 0.04), (0.0, H + 0.05)]
    # diamond leaf-scars: radius bumps in a lattice (a pineapple)
    rf = lambda th, z: 1.0 + 0.10 * abs(math.sin(7 * th + 22 * z)) * abs(math.sin(7 * th - 22 * z)) - 0.04  # noqa: E731
    k_lo, k_hi = rgb(112, 80, 50), rgb(170, 128, 74)
    cols = [T.lerp(k_lo, k_hi, min(1.0, z / H)) for r, z in prof]
    trunk = L.lathe('trunk', 'k_bark', prof, n=42, rfun=rf, cols=cols)
    objs.append(trunk)
    # the crown: two rings of stiff arching fronds
    for ring, (n, up, length, z) in enumerate(((11, 0.75, 0.66, H - 0.02), (8, 1.35, 0.52, H + 0.02))):
        for i in range(n):
            phi = 2 * math.pi * i / n + ring * 0.3 + rng.uniform(-0.1, 0.1)
            objs.append(frond('cf%d_%d' % (ring, i), (0.5, 0.5, z), phi, length * rng.uniform(0.92, 1.05), up,
                              up * 0.9 + 0.35, 0.10, rgb(24, 92, 52), rgb(110, 176, 70), rgb(40, 120, 60),
                              teeth=14, comb=0.55, fold=0.2))
    # the cone in the middle
    cprof = [(0.0, H), (0.075, H + 0.02), (0.085, H + 0.10), (0.07, H + 0.2), (0.035, H + 0.27), (0.0, H + 0.29)]
    objs.append(L.lathe('cone', 'k_cone', cprof, n=24, rfun=lambda th, z: 1 + 0.12 * abs(math.sin(8 * th + 40 * z))))
    return objs, 1.05


# ---------------------------------------------------------------------------
# 3. fiddlehead clump (small): curled young fronds and broad leaves
# ---------------------------------------------------------------------------

def fiddle():
    rng = np.random.default_rng(303)
    objs = []
    for k in range(5):
        phi = 2 * math.pi * k / 5 + rng.uniform(-0.2, 0.2)
        r = rng.uniform(0.04, 0.1)
        objs.append(fiddlehead('fh%d' % k, (0.5 + r * math.cos(phi), 0.5 + r * math.sin(phi), 0.0), phi,
                               rng.uniform(0.22, 0.36), 0.022, rgb(80, 180, 120)))
    for k in range(6):
        phi = 2 * math.pi * k / 6 + 0.3
        objs.append(frond('lf%d' % k, (0.5, 0.5, 0.01), phi, rng.uniform(0.28, 0.34), 0.9, 1.2, 0.08,
                          TEAL_D, TEAL_L, TEAL_M, teeth=6, comb=0.35))
    return objs, 0.45


# ---------------------------------------------------------------------------
# 4. the big-trunk jungle tree: a fat scaly trunk on buttress roots, a crown
#    of huge fronds, a hanging vine
# ---------------------------------------------------------------------------

def jungle_tree():
    rng = np.random.default_rng(304)
    objs = []
    H = 2.45
    ctrl = [(0.5, 0.5, 0.0), (0.5, 0.5, 0.8), (0.46, 0.53, 1.7), (0.42, 0.55, H)]
    N = 120
    path = L.catmull(ctrl, N)
    s = np.linspace(0, 1, N)
    rings = 11
    f = np.mod(s * rings, 1.0)
    radii = 0.19 + 0.05 * (1 - s) ** 1.2 + 0.10 * (1 - s) ** 10
    radii = radii * (1 + 0.10 * f ** 2.5)
    c_lo, c_hi = rgb(112, 74, 50), rgb(170, 122, 80)
    cols = [T.lerp(c_lo, c_hi, 0.2 + 0.8 * fi ** 3) * (0.85 + 0.2 * si) for fi, si in zip(f, s)]
    objs.append(L.tube('trunk', 'k_bark', path, radii, n=18, cols=cols))
    # buttress roots
    for k in range(5):
        t = 2 * math.pi * k / 5 + 0.2
        d = np.array([math.cos(t), math.sin(t), 0])
        p = [np.array([0.5, 0.5, 0.55]) + d * 0.12, np.array([0.5, 0.5, 0.18]) + d * 0.30,
             np.array([0.5, 0.5, 0.0]) + d * 0.43]
        objs.append(L.tube('root%d' % k, 'k_bark', L.catmull(p, 8), np.linspace(0.10, 0.035, 8), n=10,
                           cols=[c_lo * 0.95] * 8))
    top = path[-1] + np.array([0.0, 0.0, 0.02])
    nf = 13
    for i in range(nf):
        phi = 2 * math.pi * i / nf + rng.uniform(-0.12, 0.12)
        up = rng.uniform(0.25, 0.55) if i % 3 else rng.uniform(0.7, 0.95)
        objs.append(frond('tf%d' % i, top, phi, rng.uniform(0.82, 0.95), up, rng.uniform(0.95, 1.2), 0.22,
                          rgb(22, 96, 78), rgb(120, 206, 150), rgb(36, 130, 100), n=26, teeth=12, comb=0.45,
                          fold=0.35))
    objs.append(L.blob('crown', 'k_vcol', top + np.array([0, 0, -0.03]), 0.16, rng, amp=0.12, subdiv=2,
                       colfun=lambda P: np.tile(rgb(92, 104, 44), (len(P), 1))))
    # two fruit clusters and a hanging vine
    for k in range(2):
        t = 2 * math.pi * k / 2 + 1.1
        c = top + np.array([0.14 * math.cos(t), 0.14 * math.sin(t), -0.14])
        objs.append(L.blob('fruit%d' % k, 'k_cone', c, 0.075, rng, amp=0.1, subdiv=2))
    vine = [top + np.array([0.12, -0.12, -0.05]), top + np.array([0.2, -0.2, -0.5]),
            top + np.array([0.18, -0.24, -0.95]), top + np.array([0.22, -0.2, -1.2])]
    objs.append(L.tube('vine', 'k_vine', L.catmull(vine, 16), np.linspace(0.018, 0.012, 16), n=6))
    for k in range(4):
        q = L.catmull(vine, 16)[3 + 3 * k]
        objs.append(L.blob('vl%d' % k, 'k_vine', q + np.array([0.03, -0.02, 0]), 0.035, rng, amp=0.05, subdiv=1,
                           scale=(1.2, 0.5, 0.9)))
    return objs, 3.05


# ---------------------------------------------------------------------------
# 5. fossil ribcage (2 x 1 along X): a spine arching out of the ground,
#    five pairs of ribs curving down into it
# ---------------------------------------------------------------------------

def ribcage_x():
    rng = np.random.default_rng(305)
    objs = []
    spine = L.catmull([(0.10, 0.5, -0.05), (0.45, 0.52, 0.72), (1.05, 0.5, 1.02), (1.6, 0.48, 0.80),
                       (1.92, 0.5, 0.25)], 40)
    objs.append(L.tube('spine', 'k_bone', spine, np.linspace(0.085, 0.06, 40), n=12))
    for k in range(9):
        p = spine[4 + 4 * k]
        objs.append(L.blob('vert%d' % k, 'k_bone', p + np.array([0, 0, 0.055]), 0.07, rng, amp=0.08, subdiv=2,
                           scale=(0.7, 1.0, 0.9)))
    for k in range(5):
        i = 8 + 5 * k
        p = spine[i]
        span = 0.36 - 0.03 * abs(k - 2)
        for sgn in (-1, 1):
            rib = [p, p + np.array([0.02, sgn * span * 0.75, 0.02]),
                   np.array([p[0] + 0.05, 0.5 + sgn * span * 1.12, p[2] * 0.55]),
                   np.array([p[0] + 0.08, 0.5 + sgn * span * 0.92, -0.06])]
            objs.append(L.tube('rib%d_%d' % (k, sgn), 'k_bone', L.catmull(rib, 18), np.linspace(0.058, 0.03, 18),
                               n=10))
    # earth mounds where the bones go in
    ends = [(0.14, 0.5), (1.9, 0.5)] + [(spine[8 + 5 * k][0] + 0.08, 0.5 + s * (0.36 - 0.03 * abs(k - 2)) * 0.92)
                                         for k in range(5) for s in (-1, 1)]
    for k, (x, y) in enumerate(ends):
        objs.append(L.blob('mound%d' % k, 'k_earth', (x, y, 0.0), 0.10, rng, amp=0.2, subdiv=2, scale=(1.2, 1.0, 0.45),
                           flat_below=0.0))
    return objs, 1.1


# ---------------------------------------------------------------------------
# 6. dinosaur skull rock: a big fossil skull half sunk in a mound, grinning
# ---------------------------------------------------------------------------

def skull():
    """A cartoon T-rex fossil skull resting on a mound, turned to look at the
    camera: big round eye holes, a long snout, a row of teeth."""
    rng = np.random.default_rng(306)
    objs = []
    from mathutils import Matrix
    parts = []
    # built facing -Y, then turned to face the camera (about +18 deg)
    parts.append(L.blob('cran', 'k_bone_dirty', (0.5, 0.64, 0.30), 1.0, rng, amp=0.04, freq=1.5, subdiv=4,
                        scale=(0.27, 0.25, 0.27)))
    parts.append(L.blob('snout', 'k_bone_dirty', (0.5, 0.33, 0.22), 1.0, rng, amp=0.03, freq=1.5, subdiv=4,
                        scale=(0.17, 0.28, 0.14)))
    parts.append(L.blob('jaw', 'k_bone_dirty', (0.5, 0.36, 0.07), 1.0, rng, amp=0.03, subdiv=3,
                        scale=(0.16, 0.27, 0.07)))
    for s in (-1, 1):
        parts.append(L.blob('brow%d' % s, 'k_bone', (0.5 + s * 0.14, 0.46, 0.49), 1.0, rng, amp=0.05, subdiv=3,
                            scale=(0.10, 0.08, 0.06)))
        parts.append(L.blob('sock%d' % s, 'k_socket', (0.5 + s * 0.13, 0.42, 0.37), 1.0, rng, amp=0.03,
                            subdiv=3, scale=(0.085, 0.06, 0.085)))
        parts.append(L.blob('nos%d' % s, 'k_socket', (0.5 + s * 0.055, 0.08, 0.30), 1.0, rng, amp=0.03, subdiv=2,
                            scale=(0.032, 0.03, 0.03)))
        # the dark gap of the mouth along the snout side, teeth hanging over it
        parts.append(L.tube('gap%d' % s, 'k_socket', [(0.5 + s * 0.145, 0.10, 0.13), (0.5 + s * 0.17, 0.30, 0.12),
                                                      (0.5 + s * 0.16, 0.50, 0.13)], [0.026, 0.03, 0.022], n=8))
        for k in range(4):
            y = 0.12 + 0.095 * k
            x = 0.5 + s * (0.135 + 0.012 * k)
            parts.append(L.tube('tooth%d_%d' % (s, k), 'k_bone', [(x, y, 0.19), (x + s * 0.008, y, 0.09)],
                                [0.028, 0.0], n=8))
    for k in range(3):
        x = 0.5 + (k - 1) * 0.06
        parts.append(L.tube('ftooth%d' % k, 'k_bone', [(x, 0.07, 0.17), (x, 0.065, 0.09)], [0.024, 0.0], n=8))
    for ob in parts:
        L.rotate_z(ob, 18, center=(0.5, 0.5))
        L.transform(ob, Matrix.Translation((0, 0, 0.02)))
    objs += parts
    objs.append(L.blob('mound', 'k_earth', (0.5, 0.55, 0.0), 1.0, rng, amp=0.15, subdiv=3,
                       scale=(0.44, 0.40, 0.10), flat_below=0.0))
    objs += [frond('sf%d' % k, (0.80, 0.78, 0.03), 0.6 + 1.2 * k, 0.26, 1.1, 1.3, 0.07, TEAL_D, TEAL_L, TEAL_M,
                   teeth=6) for k in range(4)]
    return objs, 0.62


# ---------------------------------------------------------------------------
# 7. boulder: orange strata rock with a tuft on top
# ---------------------------------------------------------------------------

def boulder():
    rng = np.random.default_rng(307)
    a, b, c = rgb(214, 124, 72), rgb(150, 74, 48), rgb(240, 172, 110)

    def colfun(P):
        z = P[:, 2]
        band = np.sin(z * 30 + np.sin(P[:, 0] * 9) * 0.6) * 0.5 + 0.5
        nz = np.sin(P @ np.array([13.1, 9.7, 11.3])) * 0.5 + 0.5
        col = T.lerp(b, a, np.clip(band * 0.8 + 0.2 * nz, 0, 1))
        return T.lerp(col, c, np.clip((band - 0.8) * 4, 0, 1))
    objs = [L.blob('bd', 'k_rock', (0.5, 0.52, 0.18), 1.0, rng, amp=0.22, freq=2.6, subdiv=3,
                   scale=(0.40, 0.36, 0.42), flat_below=0.0, colfun=colfun, smooth=False),
            L.blob('bd2', 'k_rock', (0.82, 0.24, 0.06), 1.0, rng, amp=0.2, freq=3, subdiv=2,
                   scale=(0.12, 0.11, 0.12), flat_below=0.0, colfun=colfun, smooth=False)]
    objs += [frond('bf%d' % k, (0.42, 0.56, 0.56), 0.9 + 1.6 * k, 0.2, 1.2, 1.4, 0.05, TEAL_D, TEAL_L, TEAL_M,
                   teeth=5) for k in range(4)]
    return objs, 0.72


# ---------------------------------------------------------------------------
# 8. nest with three speckled eggs
# ---------------------------------------------------------------------------

def nest():
    rng = np.random.default_rng(308)
    objs = []
    # the bowl of straw
    prof = [(0.0, 0.03), (0.28, 0.03), (0.38, 0.08), (0.40, 0.16), (0.36, 0.20), (0.30, 0.17), (0.22, 0.10),
            (0.0, 0.08)]
    objs.append(L.lathe('bowl', 'k_straw', prof, n=40, rfun=lambda th, z: 1 + 0.04 * math.sin(11 * th + z * 30)))
    # twigs poking out around the rim
    for k in range(22):
        t = rng.uniform(0, 2 * math.pi)
        r0 = rng.uniform(0.3, 0.38)
        z0 = rng.uniform(0.1, 0.19)
        d = np.array([math.cos(t), math.sin(t), 0])
        tg = np.array([-math.sin(t), math.cos(t), 0]) * rng.choice([-1, 1])
        p0 = np.array([0.5, 0.5, z0]) + d * r0 - tg * 0.12
        p1 = np.array([0.5, 0.5, z0 + rng.uniform(-0.02, 0.04)]) + d * (r0 + rng.uniform(0.02, 0.08)) + tg * 0.12
        objs.append(L.tube('tw%d' % k, 'k_twig', [p0, p1], [0.012, 0.008], n=5))
    spot, cream = rgb(40, 150, 140), rgb(244, 234, 206)
    for k, (x, y, rot) in enumerate(((0.42, 0.56, 0.2), (0.60, 0.52, -0.3), (0.50, 0.40, 0.1))):
        def colfun(P, k=k):
            nz = np.sin(P @ np.array([61.0, 47.0, 53.0]) + k) * np.sin(P @ np.array([-37.0, 71.0, 29.0]))
            return T.lerp(cream, spot, (nz > 0.55).astype(float) * 0.9)
        eg = L.blob('egg%d' % k, 'k_egg', (x, y, 0.19), 1.0, rng, amp=0.0, subdiv=4,
                    scale=(0.095, 0.095, 0.13), colfun=colfun)
        for v in eg.data.vertices:            # egg shape: narrower on top
            dz = v.co.z - 0.19
            f = 1.0 - 0.18 * max(0.0, dz / 0.13)
            v.co.x = x + (v.co.x - x) * f
            v.co.y = y + (v.co.y - y) * f
        eg.data.update()
        L.rotate_z(eg, math.degrees(rot), center=(x, y))
        objs.append(eg)
    return objs, 0.34


# ---------------------------------------------------------------------------
# 9. geyser mouth (lit): a terraced mineral mound, a glowing hot vent
# ---------------------------------------------------------------------------

def geyser():
    rng = np.random.default_rng(309)
    objs = []
    t1, t2, t3 = rgb(236, 206, 150), rgb(214, 150, 84), rgb(170, 96, 60)
    prof = [(0.44, 0.0), (0.43, 0.05), (0.36, 0.08), (0.34, 0.15), (0.27, 0.18), (0.25, 0.26), (0.18, 0.30),
            (0.15, 0.36), (0.10, 0.37), (0.085, 0.33), (0.0, 0.30)]
    cols = [t3, t3, t2, t2, t1, t1, t2, t1, t1, t3, t3]
    objs.append(L.lathe('mound', 'k_rock', prof, n=40,
                        rfun=lambda th, z: 1 + 0.05 * math.sin(5 * th + 3 * z) + 0.03 * math.sin(13 * th),
                        cols=cols))
    # the hot pool in the vent and its bright core
    objs.append(L.lathe('pool', 'k_lava', [(0.0, 0.335), (0.088, 0.335), (0.0, 0.336)], n=24))
    objs.append(L.lathe('core', 'k_lava_core', [(0.0, 0.338), (0.045, 0.338), (0.0, 0.339)], n=20))
    # a few stones round the base
    for k in range(5):
        t = 2 * math.pi * k / 5 + 0.3
        objs.append(L.blob('st%d' % k, 'k_rock', (0.5 + 0.42 * math.cos(t), 0.5 + 0.40 * math.sin(t), 0.02),
                           rng.uniform(0.045, 0.06), rng, amp=0.2, subdiv=2, flat_below=0.0, scale=(1, 1, 0.7),
                           colfun=lambda P: np.tile(t3, (len(P), 1))))
    # a wisp of steam rising
    rng2 = np.random.default_rng(3091)
    for k in range(3):
        c = (0.5 + 0.03 * k, 0.5 - 0.02 * k, 0.48 + 0.16 * k)
        pb = L.blob('steam%d' % k, 'k_steam', c, 0.08 + 0.02 * k, rng2, amp=0.2, subdiv=2)
        ca = pb.data.color_attributes.new('col', 'FLOAT_COLOR', 'POINT')
        a = 0.8 - 0.22 * k
        ca.data.foreach_set('color', np.tile([a, a, a, 1.0], len(pb.data.vertices)).astype(np.float32))
        pb.visible_shadow = False
        objs.append(pb)
    for ob in objs:
        if ob.name.startswith(('pool', 'core')):
            ob.visible_shadow = False
    lamps = [C.add_point_light((0.5, 0.5, 0.42), color=(1.0, 0.5, 0.18), power=9.0, radius=0.08, name='vent')]
    for k in range(3):
        t = 2 * math.pi * k / 3 + 0.4
        lamps.append(C.add_point_light((0.5 + 0.5 * math.cos(t), 0.5 + 0.5 * math.sin(t), 0.3),
                                       color=(1.0, 0.45, 0.15), power=4.0, radius=0.1, name='spill%d' % k))
    return objs, 0.8, lamps


# ---------------------------------------------------------------------------
# 10. bone fence (along X): a thigh-bone post, two rib rails lashed with vines
# ---------------------------------------------------------------------------

def bonefence_x():
    rng = np.random.default_rng(310)
    objs = []
    post = [(0.5, 0.5, 0.0), (0.5, 0.5, 0.9)]
    objs.append(L.tube('post', 'k_bone', post, [0.06, 0.05], n=12))
    for z in (0.02, 0.9):
        for dx in (-0.045, 0.045):
            objs.append(L.blob('knob', 'k_bone', (0.5 + dx, 0.5, z + 0.02), 0.06, rng, amp=0.05, subdiv=2))
    for z, sag in ((0.34, 0.03), (0.66, 0.04)):
        rail = L.catmull([(0.0, 0.5, z), (0.25, 0.5, z + sag), (0.5, 0.5, z), (0.75, 0.5, z + sag), (1.0, 0.5, z)], 16)
        rr = 0.034 + 0.008 * np.abs(np.cos(np.linspace(0, 2 * math.pi, 16)))
        objs.append(L.tube('rail', 'k_bone', rail, rr, n=10))
        # vine lashing at the post
        for k in range(3):
            a = k / 3 * 2 * math.pi
            ring = [(0.5 + 0.075 * math.cos(t + a), 0.5 + 0.075 * math.sin(t + a), z + 0.035 * math.sin(t * 0.5) - 0.02 + 0.02 * k)
                    for t in np.linspace(0, 2 * math.pi, 14)]
            objs.append(L.tube('lash', 'k_vine', ring, [0.014] * 14, n=6, cap=False))
    objs.append(L.blob('leafy', 'k_vine', (0.56, 0.46, 0.72), 0.05, rng, amp=0.1, subdiv=2, scale=(1.2, 0.5, 0.8)))
    return objs, 0.98


# ---------------------------------------------------------------------------
# more props for the full set
# ---------------------------------------------------------------------------

def _mats_full():
    if getattr(_mats_full, 'done', False):
        return
    _mats_full.done = True

    def veined(nt, neutral):
        """Base colour from the 'col' attribute; where it is hot orange (red
        well above blue) it also emits: glowing veins in dark rock."""
        nb = L.NB(nt)
        a = nb.attr('col')
        sep = nb.node('ShaderNodeSeparateRGB')
        nt.links.new(a.outputs['Color'], sep.inputs[0])
        hot = nb.math('GREATER_THAN', nb.math('SUBTRACT', sep.outputs['R'], sep.outputs['B']), 0.35)
        nz = nb.node('ShaderNodeTexNoise')
        nz.inputs['Scale'].default_value = 26
        tc = nb.node('ShaderNodeTexCoord')
        nt.links.new(tc.outputs['Object'], nz.inputs['Vector'])
        normal = nb.bump(nz.outputs['Fac'], 0.02, 0.5)
        base = (0.8, 0.8, 0.8, 1.0) if neutral else a.outputs['Color']
        b = nb.principled(base, 0.8, 0.25, normal, a.outputs['Color'], 0.0)
        str_ = nb.math('MULTIPLY', hot, 3.0)
        nt.links.new(str_, b.node.inputs['Emission Strength'])
        return b
    C.mat('k_veined', build=veined)

    def amber(nt, neutral):
        nb = L.NB(nt)
        b = nb.node('ShaderNodeBsdfPrincipled')
        col = (0.8, 0.8, 0.8, 1.0) if neutral else tuple(T.rgb(255, 150, 30)) + (1.0,)
        b.inputs['Base Color'].default_value = col
        b.inputs['Roughness'].default_value = 0.12
        b.inputs['Specular'].default_value = 0.6
        b.inputs['Transmission'].default_value = 0.75
        b.inputs['IOR'].default_value = 1.2
        b.inputs['Emission'].default_value = tuple(T.rgb(255, 140, 30)) + (1.0,)
        b.inputs['Emission Strength'].default_value = 0.0 if neutral else 0.35
        return b.outputs['BSDF']
    C.mat('k_amber', build=amber)
    L.flat('k_bug', (40, 30, 24), rough=0.5)
    L.flat('k_berry', (240, 90, 40), rough=0.4, spec=0.5)
    L.flat('k_petal_r', (240, 60, 50), rough=0.5)
    L.flat('k_petal_o', (255, 140, 40), rough=0.5)
    L.flat('k_petal_y', (255, 214, 70), rough=0.5)
    L.flat('k_petal_p', (236, 90, 150), rough=0.5)
    L.flat('k_stamen', (255, 240, 150), rough=0.5)


def fern_small():
    rng = np.random.default_rng(311)
    objs = []
    nf = 8
    for i in range(nf):
        phi = 2 * math.pi * i / nf + rng.uniform(-0.2, 0.2)
        objs.append(frond('fs%d' % i, (0.5, 0.5, 0.02), phi, rng.uniform(0.34, 0.42), rng.uniform(1.3, 1.8),
                          rng.uniform(1.5, 1.9), 0.08, TEAL_D, TEAL_L, TEAL_M, teeth=8, comb=0.5))
    objs.append(fiddlehead('fid', (0.52, 0.5, 0.0), 0.8, 0.2, 0.014, rgb(90, 190, 130)))
    return objs, 0.5


def palm():
    """A slender, curved prehistoric palm: a ringed trunk, a crown of teal
    fronds, orange fruit (the big-trunk tree is `tree`)."""
    rng = np.random.default_rng(312)
    H = 2.6
    ctrl = [(0.5, 0.5, 0.0), (0.52, 0.5, 0.8), (0.60, 0.46, 1.7), (0.70, 0.40, H)]
    N = 150
    path = L.catmull(ctrl, N)
    s_ = np.linspace(0, 1, N)
    f = np.mod(s_ * 15, 1.0)
    radii = (0.09 + 0.035 * (1 - s_) ** 1.5 + 0.05 * (1 - s_) ** 14) * (1 + 0.16 * f ** 2.5)
    c_lo, c_hi = rgb(120, 82, 54), rgb(186, 140, 92)
    cols = [T.lerp(c_lo, c_hi, 0.25 + 0.75 * fi ** 3) * (0.85 + 0.15 * si) for fi, si in zip(f, s_)]
    objs = [L.tube('palm_trunk', 'k_bark', path, radii, n=14, cols=cols)]
    top = path[-1] + np.array([0.0, 0.0, 0.03])
    for i in range(10):
        phi = 2 * math.pi * i / 10 + rng.uniform(-0.2, 0.2)
        up = rng.uniform(0.25, 0.5) if i % 3 else rng.uniform(0.6, 0.85)
        objs.append(frond('pf%d' % i, top, phi, rng.uniform(0.72, 0.84), up, rng.uniform(0.85, 1.1), 0.17,
                          rgb(24, 100, 76), rgb(140, 210, 120), rgb(40, 130, 96), n=24, teeth=10, comb=0.45))
    objs.append(L.blob('crown', 'k_vcol', top + np.array([0, 0, -0.02]), 0.09, rng, amp=0.1, subdiv=2,
                       colfun=lambda P: np.tile(rgb(96, 110, 40), (len(P), 1))))
    for k in range(3):
        t = 2 * math.pi * k / 3 + 0.5
        c = top + np.array([0.08 * math.cos(t), 0.08 * math.sin(t), -0.11])
        objs.append(L.blob('fruit%d' % k, 'k_cone', c, 0.06, rng, amp=0.05, subdiv=2))
    return objs, H + 0.3


def _turn_y(objs):
    """Turn an along-X prop over cells (0..1, 0) into its along-Y twin over
    cells (0, 0..1) (the far end at +Y)."""
    for ob in objs:
        L.rotate_z(ob, 90, center=(0.5, 0.5))
    return objs


def ribcage_y():
    objs, h = ribcage_x()
    return _turn_y(objs), h


def bonefence_y():
    objs, h = bonefence_x()
    for ob in objs:
        L.rotate_z(ob, 90, center=(0.5, 0.5))
    return objs, h


def _veins(P, dark, dark2, seed, amount=0.10):
    nz = np.sin(P @ np.array([13.1, 9.7, 11.3]) + seed) * 0.5 + 0.5
    c = T.lerp(dark2, dark, np.clip(nz, 0, 1))
    v1 = np.abs(np.sin(P @ np.array([17.0, -11.0, 23.0]) + seed * 1.7 + 2.0 * np.sin(P @ np.array([5.0, 7.0, 3.0]))))
    v2 = np.abs(np.sin(P @ np.array([-13.0, 19.0, 9.0]) + seed * 0.6))
    vein = (np.minimum(v1, v2) < amount).astype(float)
    return T.lerp(c, T.rgb(255, 110, 20), vein)


def volcanorock():
    rng = np.random.default_rng(313)
    dark, dark2 = rgb(74, 62, 64), rgb(46, 38, 42)
    objs = [L.blob('vr', 'k_veined', (0.5, 0.52, 0.22), 1.0, rng, amp=0.22, freq=2.4, subdiv=4,
                   scale=(0.38, 0.34, 0.40), flat_below=0.0, colfun=lambda P: _veins(P, dark, dark2, 1.0),
                   smooth=False),
            L.blob('vr2', 'k_veined', (0.80, 0.26, 0.07), 1.0, rng, amp=0.2, freq=3, subdiv=3,
                   scale=(0.13, 0.12, 0.13), flat_below=0.0, colfun=lambda P: _veins(P, dark, dark2, 4.0),
                   smooth=False)]
    lamps = [C.add_point_light((0.5, 0.2, 0.35), color=(1.0, 0.42, 0.12), power=6.0, radius=0.2, name='v1'),
             C.add_point_light((0.85, 0.55, 0.3), color=(1.0, 0.42, 0.12), power=4.0, radius=0.2, name='v2')]
    return objs, 0.66, lamps


def amber():
    """A big amber crystal cluster on a rock, a little bug inside the biggest."""
    rng = np.random.default_rng(314)
    objs = [L.blob('base', 'k_rock', (0.5, 0.5, 0.06), 1.0, rng, amp=0.2, freq=2.5, subdiv=3,
                   scale=(0.36, 0.32, 0.14), flat_below=0.0, colfun=lambda P: np.tile(rgb(120, 90, 70), (len(P), 1)))]

    def crystal(name, c, r, h, tilt, yaw):
        from mathutils import Matrix
        prof = [(r * 0.8, 0.0), (r, 0.08), (r, h * 0.75), (0.0, h)]
        ob = L.lathe(name, 'k_amber', prof, n=6, smooth=False, center=(0, 0))
        M = Matrix.Translation(c) @ Matrix.Rotation(math.radians(yaw), 4, 'Z') @ Matrix.Rotation(math.radians(tilt), 4, 'X')
        L.transform(ob, M)
        return ob
    objs.append(crystal('c0', (0.5, 0.52, 0.08), 0.15, 0.78, 8, 20))
    objs.append(crystal('c1', (0.30, 0.46, 0.06), 0.09, 0.45, -30, 70))
    objs.append(crystal('c2', (0.70, 0.40, 0.06), 0.08, 0.40, 28, -30))
    objs.append(crystal('c3', (0.60, 0.70, 0.06), 0.07, 0.34, 25, 200))
    # the bug: a body, a head, wings, legs, inside the big crystal
    bx, by, bz = 0.5, 0.515, 0.38
    objs.append(L.blob('bug_b', 'k_bug', (bx, by, bz), 0.045, rng, amp=0.0, subdiv=2, scale=(0.7, 0.6, 1.4)))
    objs.append(L.blob('bug_h', 'k_bug', (bx, by, bz + 0.07), 0.025, rng, amp=0.0, subdiv=2))
    for s in (-1, 1):
        objs.append(L.blob('bug_w%d' % s, 'k_stamen', (bx + s * 0.04, by, bz + 0.03), 0.04, rng, amp=0.0, subdiv=2,
                           scale=(1.0, 0.25, 0.5)))
        for k in range(3):
            z = bz - 0.02 + 0.025 * k
            objs.append(L.tube('bug_l%d%d' % (s, k), 'k_bug', [(bx, by, z), (bx + s * 0.06, by, z - 0.03)],
                               [0.006, 0.004], n=5))
    lamps = [C.add_point_light((0.5, 0.52, 0.42), color=(1.0, 0.60, 0.20), power=10.0, radius=0.12, name='amb')]
    return objs, 0.86, lamps


def bush():
    rng = np.random.default_rng(315)
    light, dark = rgb(60, 156, 110), rgb(18, 80, 64)

    def colfun(P):
        h = np.clip(P[:, 2] / 0.8, 0, 1)
        return T.lerp(dark, light, np.clip(h * 1.3 - 0.1, 0, 1))
    objs = []
    for c, r in (((0.5, 0.5, 0.34), 0.28), ((0.28, 0.46, 0.24), 0.2), ((0.72, 0.44, 0.24), 0.2),
                 ((0.5, 0.70, 0.28), 0.22), ((0.5, 0.30, 0.22), 0.18)):
        objs.append(L.blob('b', 'k_vcol', c, r, rng, amp=0.15, freq=3.5, subdiv=3, colfun=colfun, flat_below=0.0))
    for i in range(7):
        phi = 2 * math.pi * i / 7 + 0.2
        objs.append(frond('bl%d' % i, (0.5, 0.5, 0.2), phi, 0.34, 1.1, 1.4, 0.10, rgb(30, 110, 80), rgb(110, 200, 130),
                          rgb(40, 130, 96), teeth=3, comb=0.2, fold=0.3))
    for k in range(8):
        t = rng.uniform(0, 2 * math.pi)
        z = rng.uniform(0.25, 0.5)
        objs.append(L.blob('berry', 'k_berry', (0.5 + 0.28 * math.cos(t), 0.5 + 0.26 * math.sin(t), z), 0.032, rng,
                           amp=0, subdiv=1))
    return objs, 0.64


def flowers():
    """Tropical flowers: broad leaves and bright five-petal blooms."""
    rng = np.random.default_rng(316)
    objs = []
    for i in range(6):
        phi = 2 * math.pi * i / 6 + rng.uniform(-0.2, 0.2)
        objs.append(frond('lf%d' % i, (0.5, 0.5, 0.01), phi, 0.26, 0.8, 1.2, 0.07, rgb(30, 110, 70), rgb(100, 180, 90),
                          rgb(40, 130, 80), teeth=2, comb=0.1))
    keys = ['k_petal_r', 'k_petal_o', 'k_petal_y', 'k_petal_p', 'k_petal_r', 'k_petal_o', 'k_petal_y']
    for k, key in enumerate(keys):
        cx, cy = 0.5 + rng.uniform(-0.24, 0.24), 0.5 + rng.uniform(-0.22, 0.22)
        h = rng.uniform(0.2, 0.36)
        objs.append(L.tube('stem', 'k_vine', [(cx, cy, 0.0), (cx + 0.02, cy, h)], [0.009, 0.007], n=5))
        for pk in range(5):
            t = 2 * math.pi * pk / 5 + rng.uniform(0, 1)
            objs.append(L.blob('pet', key, (cx + 0.02 + 0.034 * math.cos(t), cy + 0.034 * math.sin(t), h + 0.01), 0.03,
                               rng, amp=0, subdiv=1, scale=(1, 1, 0.4)))
        objs.append(L.blob('mid', 'k_stamen', (cx + 0.02, cy, h + 0.02), 0.016, rng, amp=0, subdiv=1))
    return objs, 0.4


PROPS = [
    ('fern', fern, {}),
    ('fern_small', fern_small, {}),
    ('cycad', cycad, {}),
    ('fiddlehead', fiddle, {}),
    ('tree', jungle_tree, {'canopy_overhang_m': 0.35}),
    ('palm', palm, {'canopy_overhang_m': 0.4}),
    ('ribcage_x', ribcage_x, {'footprint': [2, 1], 'along': 'x'}),
    ('ribcage_y', ribcage_y, {'footprint': [1, 2], 'along': 'y'}),
    ('skull', skull, {}),
    ('boulder', boulder, {}),
    ('nest', nest, {}),
    ('bonefence_x', bonefence_x, {'along': 'x'}),
    ('bonefence_y', bonefence_y, {'along': 'y'}),
    ('bush', bush, {}),
    ('flowers', flowers, {}),
]
LIT = [
    ('volcanorock', volcanorock, ((0.55, 0.45), 0.8), {}),
    ('amber', amber, ((0.5, 0.5), 0.8), {}),
]


def run(a, sample):
    _mats()
    _mats_full()
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
