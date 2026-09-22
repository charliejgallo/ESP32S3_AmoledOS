"""Monster Hop - the zone-agnostic half of city.py / castle.py: texture
utilities (edge lines, pebbles, caching), material shortcuts, and the render
wrappers for tiles and props (with the glow split). See zones_cc_lib.py for
the painting, node and geometry primitives.

    import zones_cc_kit as K
    A, REG = K.setup('city')         # parses --sample / --only / --samples, C.reset('city')
    ... @REG.add('city_blk_x_v0', sample=True) def _(): K.render_tile(...)
    K.main('city')
"""
import math
import os
import time

import bmesh
import numpy as np

import mh_common as C
import zones_cc_lib as L
from zones_cc_lib import FM, blend, lina

A = None
REG = None
_MATS = {}
_TEXC = {}

EDGE_W = 0.022       # the faint grid line on the tops (SPEC 2)
EDGE_K = 0.20
SURF_T = 0.33        # surface slabs: thickness below their top


def setup(zone):
    global A, REG
    flags = L.cli_flags()            # before C.args(): argparse would take --sample for --samples
    A = C.args(defaults={'samples': 64})
    A.sample = flags['sample']
    C.reset(zone, cpu=A.cpu)
    REG = L.Registry()
    _MATS.clear()
    _TEXC.clear()
    return A, REG


# ---------------------------------------------------------------------------
# texture utilities
# ---------------------------------------------------------------------------

def cached(fn):
    def w(*a):
        k = (fn.__module__, fn.__name__) + a
        if k not in _TEXC:
            _TEXC[k] = fn(*a)
        return _TEXC[k]
    w.__name__ = fn.__name__
    return w


def smooth01(t):
    t = np.clip(t, 0, 1)
    return t * t * (3 - 2 * t)


def edge_shade(P, w=EDGE_W, k=EDGE_K):
    """Darken the top texture towards the cell's edges: the faint grid."""
    return (1.0 - k * (1.0 - smooth01(P.edge_dist() / w)))[..., None]


def side_shade(P, top_line=True, k=0.14, w=0.016):
    """Faint lines at a side texture's vertical edges and at its top and
    bottom (the floors read on walls)."""
    du = np.minimum(P.X - P.u0, P.u1 - P.X)
    dv = np.minimum(P.Y - P.v0, P.v1 - P.Y) if top_line else (P.Y - P.v0)
    return (1.0 - k * (1.0 - smooth01(np.minimum(du, dv) / w)))[..., None]


def interior(P, margin=0.07, soft=0.08):
    """0 at the cell's edges, 1 inside (variant features fade out at the edges)."""
    return np.clip((P.edge_dist() - margin) / soft, 0, 1)


def wrap_dots(P, rng, n, rmin, rmax, wrap_u=True, wrap_v=False, zmin=None, zmax=None):
    """Random dots as (cx, cy, r), duplicated across the periodic borders."""
    dots = []
    for _ in range(n):
        cx = rng.uniform(P.u0, P.u1)
        cy = rng.uniform(zmin if zmin is not None else P.v0, zmax if zmax is not None else P.v1)
        r = rng.uniform(rmin, rmax)
        for du in ((-1, 0, 1) if wrap_u else (0,)):
            for dv in ((-1, 0, 1) if wrap_v else (0,)):
                dots.append((cx + du * (P.u1 - P.u0), cy + dv * (P.v1 - P.v0), r))
    return dots


def draw_pebbles(P, col, h, dots, c_light, c_dark, hscale=1.0, squash=0.75):
    """Little stones with a lit top and a darker bottom (they read in 3 px)."""
    for cx, cy, r in dots:
        if cx + r < P.u0 or cx - r > P.u1 or cy + r < P.v0 or cy - r > P.v1:
            continue
        sd = P.sd_ellipse(cx, cy, r, r * squash)
        m = P.cover(sd)
        if not m.any():
            continue
        shade = np.clip((P.Y - cy) / (r * squash), -1, 1)
        c = np.where(shade[..., None] > -0.1, lina(c_light), lina(c_dark))
        col[:] = blend(col, m * 0.9, c)
        h[:] = np.maximum(h, m * 0.6 * hscale)


def water_glints(P, rng, n, col, c_glint, alpha=0.75, length=(0.14, 0.24), thick=(0.045, 0.06),
                  margin=0.12):
    """Cartoon highlights on water: a few rounded dashes along X (the current),
    one or two with a short companion. They are thick enough in Y to read
    (0.05 m is 2 px on screen) and stay inside the cell."""
    marks = []
    tries = 0
    while len(marks) < n and tries < 200:
        tries += 1
        Lx = rng.uniform(*length)
        cx = rng.uniform(margin + Lx / 2, 1 - margin - Lx / 2)
        cy = rng.uniform(margin + 0.05, 1 - margin - 0.05)
        if any(abs(cy - m[1]) < 0.16 and abs(cx - m[0]) < (Lx + m[2]) / 2 + 0.08 for m in marks):
            continue
        marks.append((cx, cy, Lx))
    for cx, cy, Lx in marks:
        t = rng.uniform(*thick)
        sd = P.sd_box(cx, cy, Lx / 2, t / 2, t / 2)
        col = blend(col, P.cover(sd, 0.006) * alpha, lina(c_glint))
        if rng.random() < 0.6:
            L2 = Lx * rng.uniform(0.35, 0.5)
            sd = P.sd_box(cx + rng.choice((-1, 1)) * (Lx / 2 - L2 / 2) * 0.6, cy - 0.075, L2 / 2, t * 0.4, t * 0.4)
            col = blend(col, P.cover(sd, 0.006) * alpha * 0.7, lina(c_glint))
    return col


# ---------------------------------------------------------------------------
# materials
# ---------------------------------------------------------------------------

def M(key, fn):
    """Register a material once (fn() -> key)."""
    if key not in _MATS:
        _MATS[key] = fn()
    return _MATS[key]


def pm(key, col, **kw):
    """A plain prop material (see L.simple_mat)."""
    return M(key, lambda: L.simple_mat(key, col, **kw))


def tex_pair(name, pair, emit=None):
    col, h = pair[0], pair[1]
    d = {'col': L.image(name + '_c', col), 'h': L.image(name + '_h', h, data=True)}
    if emit is not None:
        d['e'] = L.image(name + '_e', emit)
    return d


def flat_tex(name, col, h=0.5):
    """A 4x4 one-colour texture pair (joints, bases)."""
    return {'col': L.image(name + '_c', np.full((4, 4, 3), lina(col))),
            'h': L.image(name + '_h', np.full((4, 4), h, np.float32), data=True)}


def mat_block(key, top, side, **kw):
    return M(key, lambda: L.block_mat(key, top=top, side=side, **kw))


# ---------------------------------------------------------------------------
# rendering
# ---------------------------------------------------------------------------

def block_box(key, bev=0.010, z0=-FM, z1=0.0, name='blk'):
    return L.box(name, 0, 0, z0, 1, 1, z1, key, bev=bev, seg=2)


def render_tile(name, objs, kind='tile', extra=None, hidden=()):
    """A block / fill / surface: color + z, anchor cell(0, 0, 0)."""
    C.render_sprite(A.out, name, objs, C.cell(0, 0, 0), passes=('color', 'z'), samples=A.samples,
                    kind=kind, extra=extra)
    C.remove(list(objs) + list(hidden))


def pool_size(objs, center, R, anchor):
    """Image size holding objs and a light pool of radius R on the ground."""
    bm = bmesh.new()
    cx, cy = center
    for i in range(40):
        a = 2 * math.pi * i / 40
        bm.verts.new((cx + R * math.cos(a), cy + R * math.sin(a), 0.0))
    disc = L.obj_from_bm('pool', bm)
    size = C.fit(list(objs) + [disc], anchor, 3, 0.0)
    C.remove([disc])
    return size


def render_prop(name, objs, height, lights=(), glow_R=None, glow_c=None, footprint=None, extra=None,
                kind='prop', pool_lights=None):
    """color + z + shadow (+ glow when it has lights). `lights` light the prop
    in its colour pass; `pool_lights` (default: all of them) are the ones
    whose light on the ground is the glow pass. The shadow pass is rendered
    with every light removed: their light is the glow, not a shadow."""
    ex = {'height_m': round(height, 3)}
    if footprint:
        ex['footprint'] = list(footprint)
    if extra:
        ex.update(extra)
    anchor = C.cell(0, 0, 0)
    if glow_R:
        pool = list(lights) if pool_lights is None else list(pool_lights)
        rest = [lt for lt in lights if lt not in pool]
        size = pool_size(objs, glow_c, glow_R, anchor)
        files = {}
        info = dict(C.render_sprite(A.out, name, objs, anchor, passes=('color', 'z'), size=size,
                                    samples=A.samples, shadow_z=0.0, kind=kind, extra=ex))
        files.update(info['files'])
        L.remove_lights(rest)
        files.update(C.render_sprite(A.out, name, objs, anchor, passes=('glow',), size=size,
                                     samples=A.samples, shadow_z=0.0, kind=kind, extra=ex)['files'])
        L.remove_lights(pool)
        info2 = C.render_sprite(A.out, name, objs, anchor, passes=('shadow',), size=size,
                                samples=A.samples, shadow_z=0.0, kind=kind, extra=ex)
        files.update(info2['files'])
        info.update(info2)
        info['files'] = files
        C._STATE['meta'][name] = info
    else:
        C.render_sprite(A.out, name, objs, anchor, passes=('color', 'z', 'shadow'), samples=A.samples,
                        shadow_z=0.0, kind=kind, extra=ex)
        L.remove_lights(lights)
    C.remove(objs)


def transform(objs, M):
    """Apply a world matrix to objects (after they were built)."""
    import bpy
    bpy.context.view_layer.update()
    for o in objs:
        o.matrix_world = M @ o.matrix_world
    bpy.context.view_layer.update()
    return objs


def rot_z(objs, deg, pivot=(0.5, 0.5, 0.0)):
    from mathutils import Matrix, Vector
    p = Vector(pivot)
    return transform(objs, Matrix.Translation(p) @ Matrix.Rotation(math.radians(deg), 4, 'Z') @
                     Matrix.Translation(-p))


def move(objs, d):
    from mathutils import Matrix, Vector
    return transform(objs, Matrix.Translation(Vector(d)))


def render_char_like(name, objs, anchor=None, extra=None, kind='dyn', samples=None, size=None):
    """A recoloured sprite (light + id + z + shadow) under the neutral light,
    like the characters (SPEC 3), then back to the zone light."""
    zone = C._STATE['light']
    C.set_light('neutral')
    C.render_sprite(A.out, name, objs, anchor or C.cell(0, 0, 0), passes=('light', 'id', 'z', 'shadow'),
                    samples=samples or A.samples, shadow_z=0.0, bounce_ground=0.0, kind=kind, extra=extra,
                    size=size)
    C.set_light(zone)


def fit_many(frames, anchor=None, shadow=True, margin=3):
    """One image size that holds every frame (a list of object lists), so an
    animation's frames share their size and anchor pixel."""
    anchor = anchor or C.cell(0, 0, 0)
    xs0, ys0, xs1, ys1 = [], [], [], []
    for objs in frames:
        w, h, ax, ay = C.fit(objs, anchor, margin, 0.0 if shadow else None)
        xs0.append(-ax)
        ys0.append(-ay)
        xs1.append(w - ax)
        ys1.append(h - ay)
    x0, y0, x1, y1 = min(xs0), min(ys0), max(xs1), max(ys1)
    return (x1 - x0, y1 - y0, -x0, -y0)


def main(zone):
    todo = REG.select(A.only, A.sample)
    t0 = time.time()
    times = {}
    for name, fn in todo:
        t = time.time()
        fn()
        times[name] = round(time.time() - t, 2)
        print('[%s] %-30s %.1fs' % (zone, name, times[name]), flush=True)
    C.save_meta(A.out)
    print('[%s] %d sprites in %.1fs (samples %d)' % (zone, len(todo), time.time() - t0, A.samples))
    return times
