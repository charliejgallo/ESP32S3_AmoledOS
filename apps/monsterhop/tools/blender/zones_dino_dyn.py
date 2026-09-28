"""Monster Hop - Lost Valley: the zone's moving and timed things (SPEC 13.1),
used by zones_dino_tiles.py. Every one is built on cell (0, 0), floor 0.

  lava_x_<nn>, lava_y_<nn>   the timed lava crack (a basalt block, like the
                             castle's spikes): 00 cold, 01 waking, 02-04
                             erupting, 05 cooling
  vent_<nn>                  the geyser: 00 idle, 01-05 the jet rising to
                             ~1.2 m, 06-07 falling
  fallrock, rockbits_<nn>    a falling volcanic rock and it shattering
  logfloat_w / _m / _e       a floating fern-trunk log (top at z = -0.02)
  gate_<nn>                  the exit: tusks, a sinking stone slab, amber
  crate                      the pushable mossy stone cube
  bridge_x, bridge_y         a fallen fern trunk laid across a pool
"""
import math

import numpy as np
from mathutils import Matrix

import mh_common as C
import zones_df_lib as L
import zones_df_tex as T
import zones_dino_tex as D
import zones_dino_props as P
from zones_df_tex import rgb

Z = 'dino'
F = C.FLOOR_M
_tm = {}


def tmat(key, fn, mapping='top', right=None, **kw):
    if key not in _tm:
        _tm[key] = L.tile_mat(key, fn(), mapping, tex_right=right() if right else None, **kw)
    return _tm[key]


def _mats():
    if getattr(_mats, 'done', False):
        return
    _mats.done = True
    P._mats()
    P._mats_full()
    L.emit_attr_mat('k_flame', strength=2.6)
    L.emit_attr_mat('k_flame_core', strength=3.2)
    L.emit_attr_mat('k_spark', strength=4.0)
    L.veil_mat('k_jet', (214, 240, 250), strength=1.1, alpha=0.75)
    L.veil_mat('k_dust', (170, 140, 120), strength=0.6, alpha=0.6)
    L.veil_mat('k_warm', (255, 190, 110), strength=1.2, alpha=0.5)
    L.flat('k_foam', (170, 226, 210), rough=0.7, spec=0.2)
    L.noise_mat('k_ivory', (250, 240, 214), (220, 200, 160), scale=9, rough=0.35, spec=0.5, bump=0.15)
    L.noise_mat('k_slab', (170, 150, 128), (134, 118, 100), scale=7, rough=0.85, spec=0.2, bump=0.4)
    L.noise_mat('k_plinth', (120, 104, 100), (90, 78, 80), scale=8, rough=0.85, spec=0.2, bump=0.4)
    L.noise_mat('k_deck', (196, 150, 100), (160, 118, 78), scale=18, rough=0.8, spec=0.2, bump=0.3,
                aniso=(1.0, 6.0, 1.0))
    L.noise_mat('k_deck_y', (196, 150, 100), (160, 118, 78), scale=18, rough=0.8, spec=0.2, bump=0.3,
                aniso=(6.0, 1.0, 1.0))
    L.tile_mat('k_fernbark', D.fernbark_tex(), 'uv', rough=0.85, spec=0.2, bump=1.0)
    L.tile_mat('k_fernend', D.fernend_tex(), 'uv', rough=0.8, spec=0.2, bump=0.6)


def flame(name, base, h, r, bend, key='k_flame', core=False, n=14):
    k = 12
    prof, cols = [], []
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
    for v in ob.data.vertices:
        t = (v.co.z - base[2]) / h
        v.co.x += bend[0] * t * t
        v.co.y += bend[1] * t * t
    ob.data.update()
    return ob


def _alpha_cols(ob, a):
    ca = ob.data.color_attributes.new('col', 'FLOAT_COLOR', 'POINT')
    ca.data.foreach_set('color', np.tile([a, a, a, 1.0], len(ob.data.vertices)).astype(np.float32))
    return ob


# ---------------------------------------------------------------------------
# the timed lava crack
# ---------------------------------------------------------------------------

LAVA_FRAMES = ['cold', 'wake', 'hot', 'hot', 'hot', 'cool']


def _crack_line(axis, var=0, n=9):
    """Points along the fissure's middle at the block's top (the texture's
    centre line, sampled)."""
    wv = T.noise1(T.TEX, 0.06, 2600 + var)
    pts = []
    for i in range(n):
        u = (i + 0.5) / n
        env = math.sin(math.pi * u) ** 1.5
        k = min(int(u * T.TEX), T.TEX - 1)
        c = 0.5 + 0.07 * wv[k] * env + 0.03 * math.sin(2 * math.pi * u * 2 + var) * env
        pts.append((u, c) if axis == 'x' else (c, u))
    return pts


def lava(axis, f):
    _mats()
    state = LAVA_FRAMES[f]
    var = 0
    tk = tmat('lava_%s_%s' % (axis, state), lambda: D.lava_top(var, state, axis), 'top', rough=0.7, spec=0.3,
              emit_strength=1.6)
    # the fissure breaks out on the right face (along X) or the front face (along Y)
    if axis == 'x':
        sk = tmat('lavaside_x_%s' % state, lambda: D.lava_side(state, False), 'side',
                  right=lambda: D.lava_side(state, True), rough=0.8, spec=0.3, emit_strength=1.6)
    else:
        sk = tmat('lavaside_y_%s' % state, lambda: D.lava_side(state, True), 'side',
                  right=lambda: D.lava_side(state, False), rough=0.8, spec=0.3, emit_strength=1.6)
    ob = L.block('lava', tk, sk)
    ob.visible_shadow = False
    objs = [ob]
    rng = np.random.default_rng(4000 + f * 7 + (axis == 'y'))
    pts = _crack_line(axis)
    lamps = []
    if state == 'wake':
        # sparks popping out of the crack
        for k in range(7):
            x, y = pts[rng.integers(len(pts))]
            z = rng.uniform(0.04, 0.22)
            sp = L.blob('spark%d' % k, 'k_spark', (x + rng.uniform(-0.03, 0.03), y + rng.uniform(-0.03, 0.03), z),
                        rng.uniform(0.012, 0.02), rng, amp=0, subdiv=1,
                        colfun=lambda P_: np.tile([1.0, 0.55, 0.12], (len(P_), 1)))
            sp.visible_shadow = False
            objs.append(sp)
        power = 1.4
    elif state == 'hot':
        # flames licking up along the crack, a different lick each frame
        for k, (x, y) in enumerate(pts):
            ph = f * 1.9 + k * 1.3
            h = 0.22 + 0.26 * (0.5 + 0.5 * math.sin(ph)) * math.sin(math.pi * (k + 0.5) / len(pts)) ** 0.3
            bend = (0.04 * math.sin(ph * 1.7), 0.04 * math.cos(ph * 1.3))
            fl = flame('fl%d' % k, (x, y, 0.0), h, 0.06, bend)
            fl.visible_shadow = False
            objs.append(fl)
            if k % 2 == 0:
                co = flame('co%d' % k, (x, y, 0.0), h * 0.55, 0.035, bend, key='k_flame_core', core=True)
                co.visible_shadow = False
                objs.append(co)
        for k in range(4):
            x, y = pts[rng.integers(len(pts))]
            sp = L.blob('spark%d' % k, 'k_spark', (x, y, rng.uniform(0.45, 0.6)), 0.014, rng, amp=0, subdiv=1,
                        colfun=lambda P_: np.tile([1.0, 0.7, 0.2], (len(P_), 1)))
            sp.visible_shadow = False
            objs.append(sp)
        power = 5.0
    elif state == 'cool':
        # a thin wisp of smoke
        for k in range(3):
            x, y = pts[2 + 2 * k]
            pb = _alpha_cols(L.blob('smoke%d' % k, 'k_dust', (x, y, 0.12 + 0.12 * k), 0.06 + 0.02 * k, rng, amp=0.2,
                                    subdiv=2), 0.5 - 0.12 * k)
            pb.visible_shadow = False
            objs.append(pb)
        power = 0.8
    else:
        power = 0.0
    if power > 0:
        for k, (x, y) in enumerate(pts[1::3]):
            lamps.append(C.add_point_light((x, y, 0.12), color=(1.0, 0.42, 0.12), power=power, radius=0.05,
                                           name='lv%d' % k))
    h = {'cold': 0.0, 'wake': 0.22, 'hot': 0.5, 'cool': 0.3}[state]
    extra = {'anim': 'trap', 'frame': f, 'frames': 6, 'ms': [600, 150, 90, 90, 90, 200][f], 'along': axis,
             'state': state, 'hazard': state == 'hot', 'warning': state == 'wake', 'height_m': h,
             'play': '00 hold, 01 warning, 02-04 loop (deadly), 05, back to 00'}
    return objs, lamps, extra


# ---------------------------------------------------------------------------
# the geyser vent
# ---------------------------------------------------------------------------

#          jet height, width, spray, steam
VENT_SEQ = [(0.0, 0.0, 0.0, 0.2), (0.35, 0.07, 0.0, 0.3), (0.70, 0.09, 0.1, 0.4), (1.0, 0.10, 0.3, 0.5),
            (1.2, 0.11, 0.6, 0.6), (1.2, 0.10, 0.9, 0.7), (0.7, 0.06, 1.0, 0.6), (0.25, 0.04, 0.8, 0.4)]


def vent(f):
    _mats()
    objs, _, lamps = P.geyser()
    # drop the sample's idle wisp: the frames draw their own
    keep = []
    for ob in objs:
        if ob.name.startswith('steam'):
            C.remove([ob])
        else:
            keep.append(ob)
    objs = keep
    jh, jw, spray, steam = VENT_SEQ[f]
    rng = np.random.default_rng(4100 + f)
    z0 = 0.34
    if jh > 0:
        # the jet: a translucent column, thicker at the base, a frothy top
        top = z0 + jh
        base = z0 if f < 6 else z0 + jh * 0.2 * (f - 5)
        prof = [(jw * 0.9, base), (jw, base + 0.1), (jw * 0.8, top * 0.7 + base * 0.3), (jw * 1.3, top - 0.05),
                (0.0, top + 0.06)]
        col = L.lathe('jet', 'k_jet', prof, n=16, cols=[(0.9, 0, 0)] * len(prof))
        col.visible_shadow = False             # translucent: no hard shadow (and a much smaller _sh)
        objs.append(col)
        for k in range(int(4 + 8 * spray)):
            t = rng.uniform(0, 2 * math.pi)
            rr = rng.uniform(0.05, 0.25) * (0.4 + spray)
            zz = top - rng.uniform(0.0, 0.5) * spray
            dr = _alpha_cols(L.blob('drop%d' % k, 'k_jet', (0.5 + rr * math.cos(t), 0.5 + rr * math.sin(t), zz),
                                    rng.uniform(0.03, 0.06), rng, amp=0.2, subdiv=1), 0.8)
            dr.visible_shadow = False
            objs.append(dr)
    # steam puffs round the mouth and up the jet
    for k in range(int(2 + 5 * steam)):
        t = rng.uniform(0, 2 * math.pi)
        zz = z0 + 0.08 + rng.uniform(0, 0.25 + jh * 0.9)
        rr = rng.uniform(0.05, 0.18)
        pb = _alpha_cols(L.blob('steam%d' % k, 'k_steam', (0.5 + rr * math.cos(t), 0.5 + rr * math.sin(t), zz),
                                rng.uniform(0.07, 0.13), rng, amp=0.2, subdiv=2), 0.35 + 0.3 * steam)
        pb.visible_shadow = False
        objs.append(pb)
    extra = {'anim': 'burst', 'frame': f, 'frames': 8, 'ms': 80, 'height_m': round(0.37 + jh, 2),
             'hazard': f in (3, 4, 5), 'play': 'as the city vent: 00 idle, 01-05 rising, 06-07 falling'}
    return objs, lamps, extra


# ---------------------------------------------------------------------------
# the falling rock and its pieces
# ---------------------------------------------------------------------------

def _vrock(name, c, r, rng, seed, scale=(1, 1, 1), flat=None):
    dark, dark2 = rgb(78, 64, 66), rgb(48, 40, 44)
    return L.blob(name, 'k_veined', c, r, rng, amp=0.2, freq=2.6, subdiv=3, scale=scale, flat_below=flat,
                  colfun=lambda P_: P._veins(P_, dark, dark2, seed, 0.12), smooth=False)


def fallrock():
    _mats()
    rng = np.random.default_rng(4200)
    ob = _vrock('rock', (0.5, 0.5, 0.3), 0.3, rng, 2.0, scale=(1.0, 0.95, 1.0))
    # its own bottom centre at the anchor
    zmin = min(v.co.z for v in ob.data.vertices)
    L.transform(ob, Matrix.Translation((0, 0, -zmin)))
    lamps = [C.add_point_light((0.5, 0.2, 0.3), color=(1.0, 0.42, 0.12), power=5.0, radius=0.2, name='fr')]
    return [ob], lamps, {'height_m': 0.6, 'note': 'anchor at its own bottom centre; the watch drops it and '
                                                  'draws the growing shadow'}


def rockbits(f):
    _mats()
    rng = np.random.default_rng(4300)
    objs = []
    n = 8
    for k in range(n):
        t = 2 * math.pi * k / n + rng.uniform(-0.3, 0.3)
        d0 = rng.uniform(0.05, 0.12)
        spread = [0.0, 0.18, 0.32, 0.38][f] * rng.uniform(0.8, 1.2)
        up = [0.12, 0.22, 0.06, 0.0][f] * rng.uniform(0.6, 1.3)
        r = rng.uniform(0.06, 0.11) * [1.0, 1.0, 0.95, 0.8][f]
        c = (0.5 + (d0 + spread) * math.cos(t), 0.5 + (d0 + spread) * math.sin(t), r * 0.7 + up)
        ob = _vrock('bit%d' % k, c, r, rng, 3.0 + k, flat=0.0 if f >= 2 else None)
        objs.append(ob)
    # a dust ring
    if f < 3:
        rr = [0.25, 0.38, 0.46][f]
        a = [0.7, 0.5, 0.25][f]
        th = np.linspace(0, 2 * math.pi, 33)
        vs, fs = [], []
        for t in th:
            for q in (rr * 0.6, rr):
                vs.append((0.5 + q * math.cos(t), 0.5 + q * math.sin(t), 0.02 + (q - rr * 0.6) * 0.3))
        for i in range(len(th) - 1):
            fs.append((2 * i, 2 * i + 2, 2 * i + 3, 2 * i + 1))
        dust = L.mesh('dust', vs, fs, 'k_dust', cols=[(a, 0, 0), (a * 0.2, 0, 0)] * len(th), recalc=False)
        dust.visible_shadow = False
        objs.append(dust)
    power = [5.0, 3.0, 1.5, 0.5][f]
    lamps = [C.add_point_light((0.5, 0.5, 0.3), color=(1.0, 0.42, 0.12), power=power, radius=0.3, name='rb')]
    return objs, lamps, {'anim': 'shatter', 'frame': f, 'frames': 4, 'ms': 70, 'height_m': 0.4}


# ---------------------------------------------------------------------------
# floating fern-trunk logs (as the forest's: top at z = -0.02, water 0.16 below)
# ---------------------------------------------------------------------------

LOG_A, LOG_B, LOG_ZC = 0.25, 0.22, -0.22      # half-width, half-height, centre (top at z = 0)
FLOAT_Z = -0.02
WATER = -0.18 - FLOAT_Z
CUT = WATER - 0.10


def _log_mesh(name, x0, x1, taper_w=False, nth=36):
    xs = list(np.linspace(x0, x1, 9))
    if taper_w:
        xs = sorted(set(round(x, 4) for x in list(x0 + 0.14 * (1 - np.cos(np.linspace(0, math.pi / 2, 7))))
                        + list(np.linspace(x0 + 0.16, x1, 7))))
    verts, faces, uvs = [], [], []
    th = np.linspace(0, 2 * math.pi, nth + 1)
    for x in xs:
        f = 1.0
        if taper_w:
            d = (x0 + 0.14 - x) / 0.14
            f = max(math.sqrt(max(0.0, 1 - max(0.0, d) ** 2)) if d > 0 else 1.0, 0.06)
        for t in th:
            rib = 1 + 0.03 * math.cos(11 * t)
            verts.append((x, 0.5 + LOG_A * f * rib * math.cos(t), max(LOG_ZC + LOG_B * f * rib * math.sin(t), CUT)))
    m = nth + 1
    for i in range(len(xs) - 1):
        for j in range(nth):
            a0 = i * m + j
            faces.append((a0, a0 + m, a0 + m + 1, a0 + 1))
            uvs += [(xs[i], th[j] / (2 * math.pi)), (xs[i + 1], th[j] / (2 * math.pi)),
                    (xs[i + 1], th[j + 1] / (2 * math.pi)), (xs[i], th[j + 1] / (2 * math.pi))]
    return L.mesh(name, verts, faces, 'k_fernbark', uvs=uvs, smooth=True)


def _foam(name, x0, x1, side):
    wl = LOG_A * math.sqrt(1 - ((WATER - LOG_ZC) / LOG_B) ** 2)
    y_in, y_out = 0.5 + side * (wl - 0.01), 0.5 + side * (wl + 0.022)
    z = WATER + 0.006
    return L.mesh(name, [(x0, y_in, z), (x1, y_in, z), (x1, y_out, z), (x0, y_out, z)], [(0, 1, 2, 3)], 'k_foam')


def _end_disc(name, x1):
    nth = 36
    th = np.linspace(0, 2 * math.pi, nth, endpoint=False)
    vs = [(x1, 0.5, LOG_ZC)]
    for t in th:
        vs.append((x1 + 0.001, 0.5 + LOG_A * math.cos(t) * 0.99, max(LOG_ZC + LOG_B * math.sin(t) * 0.99, CUT)))
    fs, uvs = [], []
    for i in range(nth):
        a0, b0 = 1 + i, 1 + (i + 1) % nth
        fs.append((0, a0, b0))
        for k in (0, a0, b0):
            uvs.append((0.5 + (vs[k][1] - 0.5) / LOG_A * 0.5, 0.5 + (vs[k][2] - LOG_ZC) / LOG_B * 0.5))
    return L.mesh(name, vs, fs, 'k_fernend', uvs=uvs, recalc=False)


def logfloat(part):
    _mats()
    rng = np.random.default_rng(4400 + ord(part))
    objs = []
    if part == 'm':
        objs += [_log_mesh('log', 0.0, 1.0), _foam('fa', 0, 1, -1), _foam('fb', 0, 1, 1)]
    elif part == 'w':
        objs += [_log_mesh('log', 0.08, 1.0, taper_w=True), _foam('fa', 0.12, 1, -1), _foam('fb', 0.12, 1, 1)]
        wl = LOG_A * math.sqrt(1 - ((WATER - LOG_ZC) / LOG_B) ** 2)
        vs, fs = [], []
        th = np.linspace(math.pi / 2, 3 * math.pi / 2, 13)
        for t in th:
            for rr in (wl - 0.01, wl + 0.022):
                vs.append((0.22 + 0.13 / wl * rr * math.cos(t), 0.5 + rr * math.sin(t), WATER + 0.006))
        for i in range(len(th) - 1):
            fs.append((2 * i, 2 * i + 2, 2 * i + 3, 2 * i + 1))
        objs.append(L.mesh('fc', vs, fs, 'k_foam'))
        # a frond stub sprouting from the tapered end
        objs.append(P.frond('stub', (0.3, 0.5, -0.02), 2.6, 0.26, 0.9, 1.3, 0.06, P.TEAL_D, P.TEAL_L, P.TEAL_M,
                            teeth=6))
    else:
        x1 = 0.9
        objs += [_log_mesh('log', 0.0, x1), _foam('fa', 0, x1 + 0.03, -1), _foam('fb', 0, x1 + 0.03, 1),
                 _end_disc('end', x1)]
        vs = [(x1 + 0.005, 0.3, WATER + 0.006), (x1 + 0.04, 0.3, WATER + 0.006), (x1 + 0.04, 0.7, WATER + 0.006),
              (x1 + 0.005, 0.7, WATER + 0.006)]
        objs.append(L.mesh('fe', vs, [(0, 1, 2, 3)], 'k_foam'))
    if part == 'm':
        objs.append(L.blob('moss', 'k_vine', (0.45, 0.5, 0.0), 0.06, rng, amp=0.2, subdiv=2, scale=(1.4, 0.8, 0.3)))
    return objs


# ---------------------------------------------------------------------------
# the exit gate: two tusks arching over a stone slab with an amber emblem;
# the slab sinks into the ground
# ---------------------------------------------------------------------------

SLAB_TOP = [1.62, 1.34, 1.04, 0.74, 0.44, 0.16, 0.0]


def _clip_above(ob, z=0.0):
    for v in ob.data.vertices:
        if v.co.z < z:
            v.co.z = z
    ob.data.update()
    return ob


def gate(f):
    _mats()
    rng = np.random.default_rng(4500)
    objs = []
    for s in (-1, 1):
        x = 0.5 + s * 0.40
        objs.append(C.box('plinth', x - 0.11, 0.39, 0.0, x + 0.11, 0.61, 0.26, 'k_plinth', bevel=0.02))
        tusk = L.catmull([(x, 0.5, 0.24), (x + s * 0.06, 0.48, 0.9), (x - s * 0.02, 0.46, 1.6),
                          (x - s * 0.22, 0.46, 2.0), (0.5 + s * 0.06, 0.48, 2.12)], 24)
        objs.append(L.tube('tusk', 'k_ivory', tusk, np.linspace(0.085, 0.02, 24), n=14))
        for k in range(2):
            z = 0.55 + 0.45 * k
            q = tusk[4 + 5 * k]
            ring = [(q[0] + 0.095 * math.cos(t), q[1] + 0.095 * math.sin(t), q[2] + 0.02 * math.sin(2 * t))
                    for t in np.linspace(0, 2 * math.pi, 16)]
            objs.append(L.tube('band', 'k_vine', ring, [0.022] * 16, n=6, cap=False))
    # the slab (sinking: clipped at the ground)
    top = SLAB_TOP[f]
    if top > 0.01:
        sl = C.box('slab', 0.22, 0.44, top - 1.62, 0.78, 0.56, top, 'k_slab', bevel=0.02)
        bpy_apply(sl)
        objs.append(_clip_above(sl))
        cz = top - 0.72
        if cz > 0.12:
            em = L.lathe('emb', 'k_amber', [(0.0, 0.0), (0.10, 0.0), (0.10, 0.03), (0.0, 0.05)], n=6, smooth=False,
                         center=(0, 0))
            L.transform(em, Matrix.Translation((0.5, 0.44, cz)) @ Matrix.Rotation(math.pi / 2, 4, 'X'))
            objs.append(em)
            ring = [(0.5 + 0.13 * math.cos(t), 0.438, cz + 0.13 * math.sin(t)) for t in np.linspace(0, 2 * math.pi, 20)]
            objs.append(L.tube('embring', 'k_ivory', ring, [0.018] * 20, n=6, cap=False))
    lamps = []
    if f == 6:
        vs = [(0.20, 0.52, 0.0), (0.80, 0.52, 0.0), (0.80, 0.52, 1.9), (0.20, 0.52, 1.9)]
        objs.append(L.mesh('veil', vs, [(0, 1, 2, 3)], 'k_warm', cols=[(1, 0, 0), (1, 0, 0), (0.1, 0, 0), (0.1, 0, 0)],
                           recalc=False))
        lamps.append(C.add_point_light((0.5, 0.55, 0.9), color=(1.0, 0.72, 0.4), power=26.0, radius=0.3, name='gate'))
    return objs, lamps


def bpy_apply(ob):
    """Apply the object's modifiers into its mesh (the slab is clipped after
    its bevel)."""
    import bpy
    dg = bpy.context.evaluated_depsgraph_get()
    ev = ob.evaluated_get(dg)
    me = bpy.data.meshes.new_from_object(ev)
    ob.modifiers.clear()
    old = ob.data
    ob.data = me
    bpy.data.meshes.remove(old)
    return ob


# ---------------------------------------------------------------------------
# the pushable crate and the bridges
# ---------------------------------------------------------------------------

def crate():
    _mats()
    tk = tmat('crate_top', D.crate_top, 'top', rough=0.85, spec=0.2)
    sk = tmat('crate_side', lambda: D.crate_face(False), 'side', right=lambda: D.crate_face(True), rough=0.85,
              spec=0.2)
    ob = L.block('crate', tk, sk, top_z=F, inset=0.02, chamfer=0.02)
    return [ob]


def bridge(direction):
    """A fallen fern trunk across the pool, planed flat on top (the deck at
    z = 0), a vine lashing at each end; built along X, turned for _y. x/y =
    the direction you walk across it."""
    _mats()
    r, zc = 0.34, -0.24
    nth = 40
    xs = np.linspace(0.0, 1.0, 9)
    th = np.linspace(0, 2 * math.pi, nth + 1)
    verts, faces, uvs = [], [], []
    for x in xs:
        for t in th:
            rib = 1 + 0.025 * math.cos(11 * t)
            z = min(zc + r * rib * math.sin(t), 0.0)
            verts.append((x, 0.5 + r * rib * math.cos(t), max(z, -0.40)))
    m = nth + 1
    for i in range(len(xs) - 1):
        for j in range(nth):
            a0 = i * m + j
            faces.append((a0, a0 + m, a0 + m + 1, a0 + 1))
            uvs += [(xs[i], th[j] / (2 * math.pi)), (xs[i + 1], th[j] / (2 * math.pi)),
                    (xs[i + 1], th[j + 1] / (2 * math.pi)), (xs[i], th[j + 1] / (2 * math.pi))]
    objs = [L.mesh('trunk', verts, faces, 'k_fernbark', uvs=uvs, smooth=True)]
    half = math.sqrt(r * r - zc * zc) - 0.01
    objs.append(C.box('deck', 0.0, 0.5 - half, -0.01, 1.0, 0.5 + half, 0.001,
                      'k_deck' if direction == 'x' else 'k_deck_y'))
    rng = np.random.default_rng(4600)
    for x in (0.18, 0.82):
        ring = [(x + 0.02 * math.sin(t * 2), 0.5 + (r + 0.008) * math.cos(t), zc + (r + 0.008) * math.sin(t))
                for t in np.linspace(-0.1, math.pi + 0.1, 16) if zc + (r + 0.008) * math.sin(t) <= 0.004]
        if len(ring) > 2:
            objs.append(L.tube('lash', 'k_vine', ring, [0.02] * len(ring), n=6, cap=False))
    objs.append(L.blob('leaf', 'k_vine', (0.7, 0.5 - half - 0.02, -0.04), 0.05, rng, amp=0.1, subdiv=2,
                       scale=(1.3, 0.6, 0.5)))
    if direction == 'y':
        for ob in objs:
            L.rotate_z(ob, 90)
    return objs
