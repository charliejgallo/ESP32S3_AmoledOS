"""Monster Hop - the far backdrop of each zone (desktop build only).

On the desktop the level floats over a painted far backdrop that scrolls at
0.3 of the level's speed, visible wherever the level is not: above its far
edge, at its sides and below its near edge. One picture per zone, 2600 x 1800
(and a 1300 x 900 downscale), the horizon at 40 % from the top: sky above it,
far terrain or sea below it, darkening to the zone's `void_bot` at the bottom.

Two halves, both in this file:

  * In Blender (bpy): a perspective camera looking at the horizon, low-poly
    silhouettes (terrain bands, pines, towns, the castle on its crag, the
    pyramids, the volcano...), lit by the moon/sun from behind. Cycles writes
    colour + world position + emission + object index into a cache (.npz).
  * In plain python3 (numpy + PIL): the painting. The sky (gradient, stars,
    moon/sun and halo, clouds), atmospheric perspective and valley mist from
    the world positions, the zone's extras (bats, pterosaurs, the smoke
    plume, the lighthouse beam, the moon's glitter path and the plankton,
    the heat haze), the darkening to void_bot, bloom; then the PNGs, the
    1x downscale, the contact sheet and meta.json.

Run (from apps/monsterhop):

    /Applications/Blender.app/Contents/MacOS/Blender -b -P tools/blender/backdrops.py -- \
        --out assets/backdrops [--only city,bay] [--draft] [--samples N]

Blender renders, then calls python3 on this same file for the painting. To
repaint from the cached renders without Blender:

    python3 tools/blender/backdrops.py --post --out assets/backdrops [--only city]

The cache lives in --cache (default: <tmp>/mh_backdrops). --draft renders at
half size with few samples and writes into <cache>_draft/out/ instead.
"""

import json
import math
import os
import shutil
import subprocess
import sys
import tempfile
import time

import numpy as np

try:
    import bpy
    from mathutils import Vector as V
    IN_BLENDER = True
except ImportError:
    IN_BLENDER = False

HERE = os.path.dirname(os.path.abspath(__file__))
W, H = 2600, 1800
HORIZON = 0.40              # horizon row / H
HFOV = 70.0                 # horizontal field of view, degrees
FPX = (W / 2.0) / math.tan(math.radians(HFOV / 2.0))       # focal length, px
PITCH = math.atan((0.5 - HORIZON) * H / FPX)               # camera looks down by this
ORDER = ['city', 'castle', 'desert', 'forest', 'dino', 'bay']
TITLES = {'city': 'city - Zombie Town', 'castle': 'castle - Vampire Castle',
          'desert': 'desert - Mummy Desert', 'forest': 'forest - Werewolf Woods',
          'dino': 'dino - Lost Valley', 'bay': 'bay - Abyss Bay'}

# ---------------------------------------------------------------------------
# The zones. Colours are sRGB hex. top/bot are the watch's s_looks void
# colours (main/mh_world.c): the top row of the sky and the bottom row.
# sky: gradient stops (y fraction, colour); fog: the mist colour by row.
# body: the moon or the sun (screen fraction x, y; radius in degrees).
# ---------------------------------------------------------------------------

ZONES = {
    'city': dict(
        top=0x2A2F3A, bot=0x0C0E14, cam_h=230,
        sky=[(0.00, 0x2A2F3A), (0.16, 0x30343F), (0.29, 0x40424D), (0.36, 0x57555C),
             (0.40, 0x6A6060), (0.43, 0x5A5558)],
        fog=[(0.36, 0x5A5659), (0.41, 0x4E4B52), (0.50, 0x363740), (0.66, 0x22242D),
             (0.85, 0x14161D), (1.00, 0x0C0E14)],
        fog_b=1.25e-4, fog_a=0.0060, fog_hf=45.0,
        body=dict(fx=0.23, fy=0.27, r=3.1, col=0xEDE6D6, halo=0xB8B0A8, halo_k=(0.30, 0.10),
                  kind='moon'),
        stars=0.35, star_col=0xD8DCE6,
        clouds=dict(amt=0.55, y0=0.13, y1=0.38, col=0x383A45, lit=0x8C8894, sx=2.2, sy=9.0,
                    cover=0.52, seed=3),
        moon_light=((0.80, 0.82, 0.90), 1.2), ambient=((0.36, 0.38, 0.46), 0.22),
        dark=(0.55, 1.6),
    ),
    'castle': dict(
        top=0x2A1740, bot=0x0A0612, cam_h=420,
        sky=[(0.00, 0x2A1740), (0.18, 0x2F1B4A), (0.30, 0x3D2862), (0.37, 0x533A7A),
             (0.40, 0x684C8C), (0.44, 0x55407A)],
        fog=[(0.34, 0x5A4480), (0.40, 0x4E3A72), (0.50, 0x33244E), (0.66, 0x1E1430),
             (0.85, 0x120B1E), (1.00, 0x0A0612)],
        fog_b=0.9e-4, fog_a=0.0030, fog_hf=70.0,
        body=dict(fx=0.29, fy=0.265, r=3.3, col=0xEEE6FA, halo=0xB49CE0, halo_k=(0.34, 0.12),
                  kind='moon'),
        stars=1.0, star_col=0xE0D8FF,
        clouds=dict(amt=0.35, y0=0.13, y1=0.37, col=0x2A1C44, lit=0x8C78C0, sx=2.0, sy=10.0,
                    cover=0.60, seed=11),
        moon_light=((0.80, 0.72, 1.00), 1.3), ambient=((0.42, 0.30, 0.62), 0.22),
        dark=(0.55, 1.5),
    ),
    'desert': dict(
        top=0x6A4630, bot=0x24140C, cam_h=120,
        sky=[(0.00, 0x6A4630), (0.14, 0x7A5236), (0.27, 0x96673F), (0.35, 0xB48050),
             (0.40, 0xC8955E), (0.43, 0xB88A5A)],
        fog=[(0.36, 0xBC8C5C), (0.41, 0xA87A50), (0.50, 0x7A5436), (0.66, 0x4E3222),
             (0.85, 0x321E13), (1.00, 0x24140C)],
        fog_b=0.8e-4, fog_a=0.0, fog_hf=40.0,
        body=dict(fx=0.63, fy=0.29, r=2.1, col=0xFFE6BC, halo=0xE8A868, halo_k=(0.55, 0.22),
                  kind='sun'),
        stars=0.0, star_col=0xFFFFFF,
        clouds=dict(amt=0.30, y0=0.13, y1=0.37, col=0x7A5638, lit=0xE0B080, sx=1.6, sy=12.0,
                    cover=0.62, seed=5),
        moon_light=((1.00, 0.80, 0.55), 2.2), ambient=((0.95, 0.70, 0.50), 0.20),
        dark=(0.55, 1.5), haze=True,
    ),
    'forest': dict(
        top=0x10281E, bot=0x040C08, cam_h=380,
        sky=[(0.00, 0x10281E), (0.16, 0x143226), (0.28, 0x1D4234), (0.36, 0x2C5A48),
             (0.40, 0x3A6A58), (0.44, 0x2E5848)],
        fog=[(0.34, 0x3E6E5C), (0.40, 0x35624F), (0.50, 0x23463A), (0.66, 0x132A22),
             (0.85, 0x091610), (1.00, 0x040C08)],
        fog_b=1.0e-4, fog_a=0.0060, fog_hf=55.0,
        body=dict(fx=0.70, fy=0.265, r=3.5, col=0xEEFAF0, halo=0x9CD8BC, halo_k=(0.36, 0.13),
                  kind='moon'),
        stars=0.8, star_col=0xD0FFF0,
        clouds=dict(amt=0.30, y0=0.13, y1=0.37, col=0x163A2C, lit=0x7CB8A0, sx=2.0, sy=11.0,
                    cover=0.62, seed=7),
        moon_light=((0.78, 0.96, 0.88), 1.2), ambient=((0.40, 0.62, 0.50), 0.18),
        dark=(0.55, 1.5),
    ),
    'dino': dict(
        top=0x5A3A26, bot=0x1C0E08, cam_h=300,
        sky=[(0.00, 0x5A3A26), (0.14, 0x6A4630), (0.27, 0x8A5E40), (0.35, 0xA8764E),
             (0.40, 0xB88458), (0.44, 0xA07452)],
        fog=[(0.34, 0xB08058), (0.40, 0x9C704C), (0.50, 0x6E5036), (0.66, 0x44301F),
             (0.85, 0x2A1810), (1.00, 0x1C0E08)],
        fog_b=0.75e-4, fog_a=0.0045, fog_hf=60.0,
        body=dict(fx=0.33, fy=0.28, r=2.3, col=0xFFE2B8, halo=0xE09A60, halo_k=(0.45, 0.20), bright=0.62,
                  kind='sun'),
        stars=0.0, star_col=0xFFFFFF,
        clouds=dict(amt=0.45, y0=0.13, y1=0.37, col=0x6A4A34, lit=0xD0A078, sx=2.0, sy=8.0,
                    cover=0.55, seed=9),
        moon_light=((1.00, 0.84, 0.62), 2.0), ambient=((0.62, 0.66, 0.56), 0.22),
        dark=(0.55, 1.5),
    ),
    'bay': dict(
        top=0x0E2438, bot=0x040A12, cam_h=40,
        sky=[(0.00, 0x0E2438), (0.16, 0x112B44), (0.29, 0x173A58), (0.36, 0x22506E),
             (0.40, 0x2E6080), (0.43, 0x224A66)],
        fog=[(0.34, 0x2C5C7A), (0.40, 0x245068), (0.50, 0x173650), (0.66, 0x0E2236),
             (0.85, 0x07121E), (1.00, 0x040A12)],
        fog_b=0.9e-4, fog_a=0.0, fog_hf=40.0,
        body=dict(fx=0.40, fy=0.27, r=3.0, col=0xEAF2FF, halo=0x8CB8E8, halo_k=(0.34, 0.13),
                  kind='moon'),
        stars=1.0, star_col=0xDCEBFF,
        clouds=dict(amt=0.30, y0=0.13, y1=0.37, col=0x14304A, lit=0x7CA8D0, sx=2.0, sy=11.0,
                    cover=0.62, seed=13),
        moon_light=((0.64, 0.80, 1.00), 1.1), ambient=((0.22, 0.40, 0.62), 0.25),
        dark=(0.52, 1.5),
    ),
}


def srgb_hex(h):
    return np.array([((h >> 16) & 255) / 255.0, ((h >> 8) & 255) / 255.0, (h & 255) / 255.0])


def to_lin(c):
    c = np.asarray(c, np.float64)
    return np.where(c <= 0.04045, c / 12.92, ((c + 0.055) / 1.055) ** 2.4)


def to_srgb(x):
    x = np.clip(x, 0, 1)
    return np.where(x <= 0.0031308, x * 12.92, 1.055 * np.power(x, 1 / 2.4) - 0.055)


def lin_hex(h):
    return to_lin(srgb_hex(h))


# ---------------------------------------------------------------------------
# Noise (numpy only, shared by both halves)
# ---------------------------------------------------------------------------

def _hash(ix, iy, seed):
    h = (ix.astype(np.int64) * 374761393 + iy.astype(np.int64) * 668265263 + int(seed) * 1442695041) & 0xffffffff
    h = ((h ^ (h >> 13)) * 1274126177) & 0xffffffff
    h = h ^ (h >> 16)
    return (h & 0xffffff).astype(np.float64) / float(0xffffff)


def vnoise(x, y, seed=0):
    """Smooth value noise in [0, 1]."""
    x = np.asarray(x, np.float64)
    y = np.asarray(y, np.float64)
    fx0 = np.floor(x)
    fy0 = np.floor(y)
    fx, fy = x - fx0, y - fy0
    ux = fx * fx * fx * (fx * (fx * 6 - 15) + 10)
    uy = fy * fy * fy * (fy * (fy * 6 - 15) + 10)
    ix, iy = fx0.astype(np.int64), fy0.astype(np.int64)
    a = _hash(ix, iy, seed)
    b = _hash(ix + 1, iy, seed)
    c = _hash(ix, iy + 1, seed)
    d = _hash(ix + 1, iy + 1, seed)
    return (a + (b - a) * ux) + ((c + (d - c) * ux) - (a + (b - a) * ux)) * uy


def fbm(x, y, octaves=5, seed=0, lac=2.03, gain=0.5):
    s, amp, tot = 0.0, 1.0, 0.0
    x = np.asarray(x, np.float64)
    y = np.asarray(y, np.float64)
    for o in range(octaves):
        s = s + amp * vnoise(x, y, seed + o * 17)
        tot += amp
        x, y = x * lac + 3.1, y * lac + 1.7
        amp *= gain
    return s / tot


def ridged(x, y, octaves=5, seed=0, lac=2.1, gain=0.5):
    s, amp, tot = 0.0, 1.0, 0.0
    x = np.asarray(x, np.float64)
    y = np.asarray(y, np.float64)
    for o in range(octaves):
        n = 1.0 - np.abs(2.0 * vnoise(x, y, seed + o * 31) - 1.0)
        s = s + amp * n * n
        tot += amp
        x, y = x * lac + 5.3, y * lac + 2.9
        amp *= gain
    return s / tot


def smoothstep(e0, e1, x):
    t = np.clip((np.asarray(x, np.float64) - e0) / (e1 - e0), 0, 1)
    return t * t * (3 - 2 * t)


def bump(y, c, w):
    return np.exp(-((np.asarray(y) - c) / w) ** 2)


# ---------------------------------------------------------------------------
# Camera (both halves): at (0, 0, cam_h) looking along +Y, pitched down
# ---------------------------------------------------------------------------

def cam_basis(p=PITCH):
    right = np.array([1.0, 0.0, 0.0])
    up = np.array([0.0, math.sin(p), math.cos(p)])
    fwd = np.array([0.0, math.cos(p), -math.sin(p)])
    return right, up, fwd


def screen_dir(fx, fy):
    """World direction of the screen point (fx, fy) (fractions of the frame)."""
    right, up, fwd = cam_basis()
    a = (fx - 0.5) * W / FPX
    b = (0.5 - fy) * H / FPX
    d = a * right + b * up + fwd
    return d / np.linalg.norm(d)


def project(P, cam_h, w=W, h=H):
    """Pixel (x, y) of world points P (N, 3) for a w x h frame."""
    right, up, fwd = cam_basis()
    rel = np.asarray(P, np.float64) - np.array([0, 0, cam_h])
    f = rel @ fwd
    s = w / float(W)
    px = w / 2.0 + (rel @ right) / f * FPX * s
    py = h / 2.0 - (rel @ up) / f * FPX * s
    return px, py, f


def ux_at(fx):
    """x / y ratio of the screen column fx on the ground (for placing things)."""
    return (fx - 0.5) * W / FPX * math.cos(PITCH)


# ===========================================================================
# Blender half
# ===========================================================================

if IN_BLENDER:
    sys.path.insert(0, HERE)
    import mh_common as C

    def _mat_vcol(name='vcol', rough=0.85, spec=0.25):
        if name in bpy.data.materials:
            return bpy.data.materials[name]
        m = bpy.data.materials.new(name)
        m.use_nodes = True
        nt = m.node_tree
        b = nt.nodes['Principled BSDF']
        at = nt.nodes.new('ShaderNodeAttribute')
        at.attribute_name = 'col'
        nt.links.new(at.outputs['Color'], b.inputs['Base Color'])
        b.inputs['Roughness'].default_value = rough
        b.inputs['Specular'].default_value = spec
        return m

    def _mat_emit(name='vemit', strength=1.0):
        if name in bpy.data.materials:
            return bpy.data.materials[name]
        m = bpy.data.materials.new(name)
        m.use_nodes = True
        nt = m.node_tree
        for n in list(nt.nodes):
            if n.type != 'OUTPUT_MATERIAL':
                nt.nodes.remove(n)
        out = [n for n in nt.nodes if n.type == 'OUTPUT_MATERIAL'][0]
        at = nt.nodes.new('ShaderNodeAttribute')
        at.attribute_name = 'col'
        e = nt.nodes.new('ShaderNodeEmission')
        e.inputs['Strength'].default_value = strength
        nt.links.new(at.outputs['Color'], e.inputs['Color'])
        nt.links.new(e.outputs['Emission'], out.inputs['Surface'])
        return m

    class Mesh:
        """Accumulates vertices, faces (any arity) and per-vertex colours."""

        def __init__(self):
            self.v, self.f, self.c = [], [], []
            self.n = 0

        def add(self, verts, faces, col):
            verts = np.asarray(verts, np.float64).reshape(-1, 3)
            faces = np.asarray(faces, np.int64)
            col = np.asarray(col, np.float64)
            if col.ndim == 1:
                col = np.broadcast_to(col, (len(verts), 3))
            self.v.append(verts)
            self.f.append((faces.reshape(-1, faces.shape[-1]) + self.n))
            self.c.append(np.asarray(col).reshape(-1, 3))
            self.n += len(verts)

        def build(self, name, mat, smooth=False, index=0):
            if not self.v:
                return None
            verts = np.concatenate(self.v)
            cols = np.concatenate(self.c)
            me = bpy.data.meshes.new(name)
            me.vertices.add(len(verts))
            me.vertices.foreach_set('co', verts.astype(np.float32).ravel())
            loops, starts, totals, s = [], [], [], 0
            for f in self.f:
                k = f.shape[1]
                loops.append(f.ravel())
                starts.append(np.arange(len(f)) * k + s)
                totals.append(np.full(len(f), k))
                s += len(f) * k
            loops = np.concatenate(loops).astype(np.int32)
            me.loops.add(len(loops))
            me.loops.foreach_set('vertex_index', loops)
            nf = sum(len(f) for f in self.f)
            me.polygons.add(nf)
            me.polygons.foreach_set('loop_start', np.concatenate(starts).astype(np.int32))
            me.polygons.foreach_set('loop_total', np.concatenate(totals).astype(np.int32))
            me.update(calc_edges=True)
            me.polygons.foreach_set('use_smooth', np.full(nf, bool(smooth)))
            at = me.color_attributes.new('col', 'FLOAT_COLOR', 'POINT')
            rgba = np.ones((len(verts), 4), np.float32)
            rgba[:, :3] = cols
            at.data.foreach_set('color', rgba.ravel())
            me.materials.append(mat)
            ob = bpy.data.objects.new(name, me)
            bpy.context.scene.collection.objects.link(ob)
            ob.pass_index = index
            return ob

    # ---- primitive generators (vectorised) ----

    def boxes(m, cx, cy, z0, sx, sy, sz, col, tilt=None):
        cx, cy, z0, sx, sy, sz = [np.atleast_1d(np.asarray(a, np.float64)) for a in (cx, cy, z0, sx, sy, sz)]
        N = len(cx)
        u = np.array([(-1, -1, 0), (1, -1, 0), (1, 1, 0), (-1, 1, 0),
                      (-1, -1, 1), (1, -1, 1), (1, 1, 1), (-1, 1, 1)], np.float64)
        v = np.empty((N, 8, 3))
        v[..., 0] = cx[:, None] + u[None, :, 0] * sx[:, None] / 2
        v[..., 1] = cy[:, None] + u[None, :, 1] * sy[:, None] / 2
        v[..., 2] = z0[:, None] + u[None, :, 2] * sz[:, None]
        if tilt is not None:
            v[:, 4:, 2] += tilt
        f = np.array([(0, 3, 2, 1), (4, 5, 6, 7), (0, 1, 5, 4), (1, 2, 6, 5), (2, 3, 7, 6), (3, 0, 4, 7)])
        faces = f[None] + (np.arange(N) * 8)[:, None, None]
        col = np.asarray(col, np.float64)
        col = np.repeat(col.reshape(-1, 3), 8, axis=0) if col.ndim == 2 else col
        m.add(v.reshape(-1, 3), faces.reshape(-1, 4) - 0, col)

    def cones(m, cx, cy, z0, r, h, col, n=6, rot=None, rtop=0.0):
        """Cones (rtop = 0) or truncated cones, no caps except a top cap when truncated."""
        cx, cy, z0, r, h = np.broadcast_arrays(*[np.atleast_1d(np.asarray(a, np.float64))
                                                 for a in (cx, cy, z0, r, h)])
        N = len(cx)
        rot = np.zeros(N) if rot is None else np.atleast_1d(rot)
        th = np.linspace(0, 2 * np.pi, n, endpoint=False)[None, :] + rot[:, None]
        ring = np.stack([cx[:, None] + r[:, None] * np.cos(th), cy[:, None] + r[:, None] * np.sin(th),
                         np.repeat(z0[:, None], n, 1)], -1)
        col = np.asarray(col, np.float64)
        if rtop <= 0:
            apex = np.stack([cx, cy, z0 + h], -1)[:, None, :]
            v = np.concatenate([ring, apex], 1)          # N, n+1
            i = np.arange(n)
            f = np.stack([i, (i + 1) % n, np.full(n, n)], -1)
            faces = f[None] + (np.arange(N) * (n + 1))[:, None, None]
            cc = np.repeat(col.reshape(-1, 3), n + 1, axis=0) if col.ndim == 2 else col
            m.add(v.reshape(-1, 3), faces.reshape(-1, 3), cc)
        else:
            rt = r * rtop
            top = np.stack([cx[:, None] + rt[:, None] * np.cos(th), cy[:, None] + rt[:, None] * np.sin(th),
                            (z0 + h)[:, None].repeat(n, 1)], -1)
            v = np.concatenate([ring, top], 1)           # N, 2n
            i = np.arange(n)
            f = np.stack([i, (i + 1) % n, (i + 1) % n + n, i + n], -1)
            faces = f[None] + (np.arange(N) * 2 * n)[:, None, None]
            cc = np.repeat(col.reshape(-1, 3), 2 * n, axis=0) if col.ndim == 2 else col
            m.add(v.reshape(-1, 3), faces.reshape(-1, 4), cc)
            cap = (np.arange(n) + n)[None] + (np.arange(N) * 2 * n)[:, None]
            m.f.append(cap + m.n - N * 2 * n)

    def blobs(m, cx, cy, cz, r, col, squash=0.8, nlon=7, seed=0):
        """Low-poly lumpy spheres (tree canopies)."""
        cx, cy, cz, r = [np.atleast_1d(np.asarray(a, np.float64)) for a in (cx, cy, cz, r)]
        N = len(cx)
        rng = np.random.default_rng(seed)
        lats = np.array([-0.15, 0.35, 0.75]) * np.pi / 2
        pts = [(0.0, -np.pi / 2)]
        for la in lats:
            for k in range(nlon):
                pts.append((2 * np.pi * k / nlon, la))
        pts.append((0.0, np.pi / 2))
        pts = np.array(pts)
        dirs = np.stack([np.cos(pts[:, 1]) * np.cos(pts[:, 0]), np.cos(pts[:, 1]) * np.sin(pts[:, 0]),
                         np.sin(pts[:, 1])], -1)
        nv = len(dirs)
        jit = 1.0 + 0.18 * (rng.random((N, nv)) - 0.5)
        v = np.empty((N, nv, 3))
        v[..., 0] = cx[:, None] + dirs[None, :, 0] * r[:, None] * jit
        v[..., 1] = cy[:, None] + dirs[None, :, 1] * r[:, None] * jit
        v[..., 2] = cz[:, None] + dirs[None, :, 2] * r[:, None] * jit * squash
        tris, quads = [], []
        for k in range(nlon):
            k1 = (k + 1) % nlon
            tris.append((0, 1 + k1, 1 + k))
            tris.append((nv - 1, 1 + 2 * nlon + k, 1 + 2 * nlon + k1))
            for L in range(2):
                a, b = 1 + L * nlon, 1 + (L + 1) * nlon
                quads.append((a + k, a + k1, b + k1, b + k))
        off = (np.arange(N) * nv)[:, None, None]
        col = np.asarray(col, np.float64)
        cc = np.repeat(col.reshape(-1, 3), nv, axis=0) if col.ndim == 2 else col
        m.add(v.reshape(-1, 3), (np.array(tris)[None] + off).reshape(-1, 3), cc)
        m.f.append((np.array(quads)[None] + off).reshape(-1, 4) + m.n - N * nv)

    def radial(m, cx, cy, R, zfun, col_fn, nr=24, nt=48, noise=0.0, seed=0):
        """A mound around (cx, cy): rings from the top outwards; zfun(s) with
        s = r / R in [0, 1]; the outline wobbles by `noise`."""
        s = np.linspace(0, 1, nr)[1:] ** 1.2
        th = np.linspace(0, 2 * np.pi, nt, endpoint=False)
        S, T = np.meshgrid(s, th, indexing='ij')
        wob = 1.0 + noise * (fbm(np.cos(T) * 2.0 + 7, np.sin(T) * 2.0 + S * 3.0, 4, seed) - 0.5) * 2
        X = cx + R * S * wob * np.cos(T)
        Y = cy + R * S * wob * np.sin(T)
        Z = zfun(S, T, X, Y)
        cz = zfun(np.zeros(1), np.zeros(1), np.array([cx]), np.array([cy]))[0]
        verts = np.concatenate([[[cx, cy, cz]], np.stack([X, Y, Z], -1).reshape(-1, 3)])
        idx = 1 + np.arange((nr - 1) * nt).reshape(nr - 1, nt)
        j = np.arange(nt)
        tris = np.stack([np.zeros(nt, np.int64), idx[0, j], idx[0, (j + 1) % nt]], -1)
        a, b = idx[:-1], idx[1:]
        quads = np.stack([a, np.roll(a, -1, 1), np.roll(b, -1, 1), b], -1).reshape(-1, 4)
        col = col_fn(verts[:, 0], verts[:, 1], verts[:, 2])
        base = m.n
        m.add(verts, tris, col)
        m.f.append(quads + base)
        return verts

    def terrain(m, fn, col_fn, y0, y1, nr, nc, umax=0.95):
        t = np.linspace(0, 1, nr)
        ys = y0 * (y1 / y0) ** t
        u = np.linspace(-umax, umax, nc)
        Y = np.repeat(ys[:, None], nc, 1)
        X = u[None, :] * ys[:, None]
        Z = fn(X, Y)
        verts = np.stack([X, Y, Z], -1).reshape(-1, 3)
        idx = np.arange(nr * nc).reshape(nr, nc)
        q = np.stack([idx[:-1, :-1], idx[:-1, 1:], idx[1:, 1:], idx[1:, :-1]], -1).reshape(-1, 4)
        m.add(verts, q, col_fn(verts[:, 0], verts[:, 1], verts[:, 2]))

    def scatter(rng, n, ymin, ymax, umax=0.78):
        """Points spread evenly on the SCREEN over flat ground (dense far away)."""
        y = 1.0 / rng.uniform(1.0 / ymax, 1.0 / ymin, n)
        x = rng.uniform(-umax, umax, n) * y
        return x, y

    def lin(h):
        return lin_hex(h)

    def jitter_cols(rng, base_hex, n, amt=0.25):
        b = lin(base_hex)
        k = 1.0 + amt * (rng.random((n, 1)) * 2 - 1)
        return np.clip(b[None, :] * k, 0, 1)

    def windows_on(m, rng, cx, cy, z0, sx, sy, sz, prob, col_hex, fl=3.6, cw=4.5, size=(1.6, 2.0)):
        """Lit window quads on the faces of boxes that look at the camera."""
        wc = lin(col_hex)
        V_, F_ = [], []
        for i in range(len(cx)):
            faces = [('y', cy[i] - sy[i] / 2 - 0.3, sx[i])]
            if cx[i] > 0:
                faces.append(('x', cx[i] - sx[i] / 2 - 0.3, sy[i]))
            else:
                faces.append(('x', cx[i] + sx[i] / 2 + 0.3, sy[i]))
            nfl = int(sz[i] / fl) - 1
            for kind, pos, span in faces:
                ncol = int(span / cw)
                if nfl < 1 or ncol < 1:
                    continue
                lit = rng.random((nfl, ncol)) < prob
                for a, b in zip(*np.nonzero(lit)):
                    zc = z0[i] + (a + 1) * fl
                    o = (b + 0.5) * span / ncol - span / 2
                    hw, hh = size[0] / 2, size[1] / 2
                    if kind == 'y':
                        xc = cx[i] + o
                        V_.append([(xc - hw, pos, zc - hh), (xc + hw, pos, zc - hh),
                                   (xc + hw, pos, zc + hh), (xc - hw, pos, zc + hh)])
                    else:
                        yc = cy[i] + o
                        V_.append([(pos, yc - hw, zc - hh), (pos, yc + hw, zc - hh),
                                   (pos, yc + hw, zc + hh), (pos, yc - hw, zc + hh)])
        if not V_:
            return 0
        V_ = np.array(V_)
        n = len(V_)
        k = (0.55 + 0.45 * rng.random((n, 1))) * wc[None, :]
        m.add(V_.reshape(-1, 3), (np.arange(n * 4).reshape(n, 4)), np.repeat(k, 4, 0))
        return n

    def ribbon(m, pts, width, col):
        """A flat strip along pts (K, 3), `width` metres wide (per point)."""
        pts = np.asarray(pts, np.float64)
        d = np.gradient(pts[:, :2], axis=0)
        d /= np.linalg.norm(d, axis=1, keepdims=True) + 1e-9
        nrm = np.stack([-d[:, 1], d[:, 0]], -1)
        w = np.broadcast_to(np.asarray(width, np.float64), (len(pts),))[:, None] / 2
        L = pts.copy()
        R_ = pts.copy()
        L[:, :2] -= nrm * w
        R_[:, :2] += nrm * w
        K = len(pts)
        verts = np.concatenate([L, R_])
        i = np.arange(K - 1)
        q = np.stack([i, i + 1, K + i + 1, K + i], -1)
        m.add(verts, q, np.concatenate([col, col]) if np.ndim(col) == 2 else col)

    # ---------------------------------------------------------------------
    # The zones
    # ---------------------------------------------------------------------

    def build_city(Z, rng):
        ch = Z['cam_h']

        def fn(x, y):
            near = 3.0 * fbm(x / 300, y / 300, 3, 1)
            hills = smoothstep(2600, 7500, y) * (40 + 230 * fbm(x / 2600, 0.5, 4, 2))
            far = 820 * (0.35 + 0.65 * fbm(x / 7000, 1.5, 5, 3)) * bump(y, 26000, 7000)
            return np.maximum(near + hills, far)

        def col(x, y, z):
            n = fbm(x / 500, y / 500, 3, 4)[:, None]
            a = lin(0x2E2C30)[None] * (0.8 + 0.4 * n)
            return np.where((y > 12000)[:, None], lin(0x3A3A44)[None] * (0.8 + 0.4 * n), a)
        g = Mesh()
        terrain(g, fn, col, 250, 60000, 300, 420)
        g.build('ground', _mat_vcol())

        b = Mesh()
        win = Mesh()
        # the town in the valley: blocks on a street grid
        x, y = scatter(rng, 5200, 330, 3400)
        gx = 52.0
        x = np.round(x / gx) * gx + rng.uniform(-6, 6, len(x))
        y = np.round(y / gx) * gx + rng.uniform(-6, 6, len(y))
        key = np.round(x / gx) * 100000 + np.round(y / gx)
        _, keep = np.unique(key, return_index=True)
        x, y = x[keep], y[keep]
        keep = rng.random(len(x)) < 0.8          # empty lots
        x, y = x[keep], y[keep]
        n = len(x)
        sx = rng.uniform(18, 40, n)
        sy = rng.uniform(18, 40, n)
        sz = rng.choice([7, 10, 14, 18, 24, 32], n, p=[0.2, 0.25, 0.2, 0.15, 0.12, 0.08]) * rng.uniform(0.9, 1.15, n)
        z0 = fn(x, y) - 2
        tilt = np.zeros((n, 4))
        broken = rng.random(n) < 0.35
        tilt[broken] = -rng.random((broken.sum(), 4)) * sz[broken, None] * 0.35
        cols = jitter_cols(rng, 0x3E3A3C, n, 0.35)
        brick = rng.random(n) < 0.35
        cols[brick] = jitter_cols(rng, 0x4A3632, int(brick.sum()), 0.3)
        boxes(b, x, y, z0, sx, sy, sz, cols, tilt)
        windows_on(win, rng, x, y, z0, sx, sy, sz, 0.007, 0xFF9A3C)
        # street lamps: rows of sodium dots along some streets
        for yy in np.arange(400, 3400, gx * 2):
            if rng.random() < 0.6:
                continue
            xs = np.arange(-0.8 * yy, 0.8 * yy, 36.0) + rng.uniform(-3, 3)
            xs = xs[rng.random(len(xs)) < 0.3]
            ys = np.full(len(xs), yy + gx / 2)
            boxes(win, xs, ys, fn(xs, ys) + 6, 2.2, 2.2, 1.6, lin(0xFF8A2A) * 0.9)
        # the skyline: taller ruined towers in a few clusters, spread
        x, y = scatter(rng, 900, 3600, 9500)
        u = x / y
        dens = np.zeros(len(u))
        for c, w_, a in ((-0.56, 0.14, 1.0), (-0.12, 0.10, 0.7), (0.30, 0.15, 1.0), (0.62, 0.10, 0.8)):
            dens += a * np.exp(-((u - c) / w_) ** 2)
        keep = rng.random(len(u)) < 0.18 + 0.8 * np.clip(dens, 0, 1)
        x, y, dens = x[keep], y[keep], dens[keep]
        n = len(x)
        sx = rng.uniform(30, 75, n)
        sy = rng.uniform(30, 75, n)
        sz = (40 + 200 * rng.random(n) ** 1.6) * (0.4 + 0.9 * np.clip(dens, 0, 1))
        z0 = fn(x, y) - 3
        tilt = np.zeros((n, 4))
        broken = rng.random(n) < 0.45
        tilt[broken] = -rng.random((broken.sum(), 4)) * sz[broken, None] * 0.3
        cols = jitter_cols(rng, 0x403E44, n, 0.3)
        boxes(b, x, y, z0, sx, sy, sz, cols, tilt)
        windows_on(win, rng, x, y, z0, sx, sy, sz, 0.022, 0xFFA040, fl=4.0, cw=6.0, size=(2.6, 2.8))
        # antennas and water tanks on a few tall ones
        tall = np.nonzero(sz > 120)[0]
        tt = tall[rng.random(len(tall)) < 0.5]
        boxes(b, x[tt], y[tt], z0[tt] + sz[tt] - 5, 1.5, 1.5, rng.uniform(20, 45, len(tt)), lin(0x302E34))
        b.build('buildings', _mat_vcol())
        win.build('windows', _mat_emit('vemit', 3.0), index=2)

    def build_castle(Z, rng):
        ch = Z['cam_h']
        crag_u, crag_y = ux_at(0.745), 5200.0
        crag_x = crag_u * crag_y
        bands = [(1900, 600, 170, 800, 21), (3400, 1000, 330, 1300, 22), (6500, 1900, 760, 1800, 23),
                 (11000, 3000, 1500, 2600, 24), (18000, 4800, 2300, 3800, 25), (30000, 8000, 3200, 6000, 26)]

        def fn(x, y):
            h = 22 * ridged(x / 500, y / 500, 3, 20)
            for D, wd, A, sc, sd in bands:
                prof = ridged(x / sc, y / (sc * 1.5), 5, sd) ** 1.8
                dip = 1.0 - 0.7 * np.exp(-((x - crag_x) / 2200.0) ** 2) * (1 if 3000 < D < 13000 else 0)
                h = np.maximum(h, A * (0.25 + 0.95 * prof) * bump(y, D, wd) * dip)
            return h

        def col(x, y, z):
            n = fbm(x / 400, y / 400, 3, 26)[:, None]
            a = lin(0x3A3048)[None] * (0.75 + 0.5 * n)
            snow = smoothstep(1500, 2300, z)[:, None]
            return a * (1 - snow) + lin(0x6A6284)[None] * snow
        g = Mesh()
        terrain(g, fn, col, 700, 60000, 340, 460)
        g.build('ground', _mat_vcol())

        # the crag and the castle on top
        top = 800.0
        rt = 0.24
        K = 2.1                  # the castle is bigger than life: it must read from 5 km

        def zc(S, T, X, Y):
            s = np.clip((S - rt) / (1 - rt), 0, 1)
            z = top * (1 - s ** 1.4) ** 1.2 * (1 - 0.35 * s)
            z = z * (1 + 0.16 * (ridged(np.cos(T) * 4 + 2, S * 5, 3, 27) - 0.5))
            return np.where(S < rt, top + 4 * np.cos(T * 3) * 0, z)
        r = Mesh()
        radial(r, crag_x, crag_y, 560, zc, lambda x, y, z: np.repeat(lin(0x2A2234)[None], len(x), 0),
               nr=30, nt=64, noise=0.30, seed=28)
        # lesser rock spires at its feet
        for i, (dx_, dy_, hh, rr_) in enumerate(((-430, 150, 430, 260), (380, 260, 330, 220), (-250, -350, 260, 200),
                                                 (560, -120, 220, 190))):
            radial(r, crag_x + dx_, crag_y + dy_, rr_,
                   lambda S, T, X, Y, hh=hh, i=i: hh * (1 - S ** 1.3) ** 1.3 * (1 + 0.2 * (ridged(np.cos(T) * 3 + i, S * 4, 3, 40 + i) - 0.5)),
                   lambda x, y, z: np.repeat(lin(0x2A2234)[None], len(x), 0), nr=12, nt=24, noise=0.35, seed=30 + i)
        r.build('crag', _mat_vcol())

        c = Mesh()
        w = Mesh()
        wall = lin(0x352C44)
        roof = lin(0x22182E)
        cx, cy, z0 = crag_x, crag_y, top - 2
        # keep and walls
        boxes(c, [cx, cx - 55 * K, cx + 55 * K, cx], [cy + 10 * K, cy, cy, cy - 50 * K], z0,
              np.array([70, 10, 10, 110]) * K, np.array([50, 100, 100, 10]) * K, np.array([70, 32, 32, 30]) * K, wall)
        towers = [(-60, -55, 10, 90), (60, -55, 10, 80), (-60, 55, 9, 70), (60, 55, 9, 75),
                  (-10, 20, 12, 130), (25, 25, 7, 105), (-40, -5, 6, 60)]
        tx = np.array([cx + t[0] * K for t in towers])
        ty = np.array([cy + t[1] * K for t in towers])
        tr = np.array([t[2] for t in towers], float) * K
        th = np.array([t[3] for t in towers], float) * K
        cones(c, tx, ty, z0, tr, th, wall, n=8, rtop=1.0)
        cones(c, tx, ty, z0 + th, tr * 1.35, tr * 3.6, roof, n=8)
        cones(c, tx[:2], ty[:2], z0 + th[:2] - 25 * K, tr[:2] * 1.25, 8 * K, wall, n=8, rtop=1.0)   # a ledge
        # lit windows on the towers, facing the camera
        for i in range(len(towers)):
            for zz in np.arange(z0 + 20 * K, z0 + th[i] - 8 * K, 16 * K):
                if rng.random() < 0.45:
                    hw = 1.8 * K
                    x0 = tx[i] + rng.uniform(-tr[i] * 0.4, tr[i] * 0.4)
                    yy = ty[i] - tr[i] - 0.5
                    wc = lin(0xFFA050) if rng.random() < 0.7 else lin(0xFF4A5A)
                    w.add([(x0 - hw, yy, zz), (x0 + hw, yy, zz), (x0 + hw, yy, zz + 5 * K), (x0 - hw, yy, zz + 5 * K)],
                          [[0, 1, 2, 3]], wc)
        c.build('castle', _mat_vcol())
        w.build('windows', _mat_emit('vemit', 4.0), index=2)

    def build_desert(Z, rng):
        pyr = [(ux_at(0.15), 4600, 620, 400), (ux_at(0.255), 6200, 450, 290)]
        sph_u, sph_y = ux_at(0.81), 2300.0
        k = 2.0                  # the sphinx's scale
        TERR = 75.0              # the height of its terrace
        sph_x = sph_u * sph_y

        def fn(x, y):
            a = math.radians(24)
            s = (x * math.cos(a) + y * math.sin(a))
            lam = 300 + 500 * smoothstep(0, 8000, y)
            p = s / lam + 1.6 * fbm(x / 1600, y / 1600, 3, 40)
            f = p - np.floor(p)
            prof = np.where(f < 0.72, (f / 0.72) ** 1.5, (1 - f) / 0.28)   # long slope, steep lee
            amp = 10 + 55 * smoothstep(300, 9000, y)
            h = amp * prof + 90 * fbm(x / 4000, y / 4000, 4, 41) * smoothstep(1000, 6000, y)
            for px, py, base, _ in pyr:
                h = h * (1 - 0.8 * np.exp(-(((x - px * py) / (base * 1.4)) ** 2 + ((y - py) / (base * 1.4)) ** 2)))
            h = h * (1 - 0.97 * np.exp(-(((x - sph_x) / 700) ** 2 + ((y - sph_y) / 450) ** 2)))
            mesa = 480 * np.clip((fbm(x / 5500, 3.0, 4, 42) - 0.47) * 7, 0, 1) ** 0.7
            mesa = mesa * bump(y, 30000, 5500)
            # the sphinx's rock terrace, so that it clears the dunes in front
            rr = np.sqrt(((x - sph_x) / 1.6) ** 2 + (y - sph_y) ** 2)
            terr = TERR * np.clip((1 - rr / 420.0) * 5, 0, 1) * (1 + 0.08 * (fbm(x / 90, y / 90, 2, 44) - 0.5))
            return np.maximum(np.maximum(h, mesa), terr)

        def col(x, y, z):
            n = fbm(x / 600, y / 600, 3, 43)[:, None]
            return lin(0x8A5E3C)[None] * (0.8 + 0.35 * n)
        g = Mesh()
        terrain(g, fn, col, 160, 60000, 360, 480)
        g.build('ground', _mat_vcol(rough=0.95), smooth=True)

        p = Mesh()
        for u, py, base, hh in pyr:
            px = u * py
            z0 = fn(np.array([px]), np.array([py]))[0] - 8
            hb = base / 2
            v = [(px - hb, py - hb, z0), (px + hb, py - hb, z0), (px + hb, py + hb, z0), (px - hb, py + hb, z0),
                 (px, py, z0 + hh)]
            p.add(v, [[0, 1, 4], [1, 2, 4], [2, 3, 4], [3, 0, 4]], lin(0xA07048))
        # the sphinx, lying facing -X (its profile to the camera)
        sx, sy = sph_x, sph_y
        z0 = TERR - 3
        st = lin(0x9A6A44)
        B_ = lambda cxs, cys, zz, a, b, c: boxes(p, sx + np.array(cxs) * k, sy + np.array(cys) * k, z0 + zz * k,
                                                np.array(a) * k, np.array(b) * k, np.array(c) * k, st)
        B_([10], [0], -3, [150], [70], [4])                                         # a stone plinth
        B_([30, 82], [0, 0], 0, [120, 36], [44, 48], [26, 30])                      # body, haunch
        B_([-62, -62], [-12, 12], 0, [76, 76], [11, 11], [9, 9])                    # the paws
        B_([-24], [0], 0, [26], [32], [40])                                         # chest
        # the head with its nemes: a tapered block, the lappets down to the chest
        hx, hz = sx - 26 * k, z0 + 36 * k
        v = np.array([(-10, -19, -6), (13, -19, -6), (13, 19, -6), (-10, 19, -6),
                      (-7, -10, 20), (8, -10, 20), (8, 10, 20), (-7, 10, 20)], float) * k + (hx, sy, hz)
        p.add(v, [(0, 3, 2, 1), (4, 5, 6, 7), (0, 1, 5, 4), (1, 2, 6, 5), (2, 3, 7, 6), (3, 0, 4, 7)], st)
        B_([-39], [0], 34, [8], [12], [16])                                         # the face
        B_([100], [16], 0, [70], [8], [5])                                          # tail
        p.build('monuments', _mat_vcol(rough=0.9))

    def build_forest(Z, rng):
        ch = Z['cam_h']
        bands = [(1400, 500, 130, 700, 50), (2600, 800, 250, 1200, 51), (4300, 1200, 400, 1800, 52),
                 (7000, 2000, 600, 2600, 53), (11500, 3200, 900, 3600, 54), (19000, 5000, 1350, 5200, 55),
                 (30000, 8000, 1900, 7000, 56)]

        def fn(x, y):
            h = 8 * fbm(x / 200, y / 200, 3, 49)
            for D, wd, A, sc, sd in bands:
                prof = fbm(x / sc, y / (sc * 3), 4, sd)
                prof = np.clip((prof - 0.25) * 1.7, 0, 1) ** 1.2
                h = np.maximum(h, A * (0.2 + 0.9 * prof) * bump(y, D, wd))
            return h

        def col(x, y, z):
            n = fbm(x / 300, y / 300, 3, 57)[:, None]
            return lin(0x1C3A2C)[None] * (0.7 + 0.5 * n)
        g = Mesh()
        terrain(g, fn, col, 500, 60000, 380, 480)
        g.build('ground', _mat_vcol(), smooth=True)

        t = Mesh()
        x, y = scatter(rng, 52000, 520, 17000)
        z = fn(x, y)
        n = len(x)
        hh = rng.uniform(20, 34, n) * (1 + 0.5 * smoothstep(4000, 17000, y))
        r = hh * rng.uniform(0.24, 0.32, n)
        cols = jitter_cols(rng, 0x1E4436, n, 0.35)
        cols = cols * (0.85 + 0.3 * rng.random((n, 1)))
        rot = rng.random(n) * 6
        cones(t, x, y, z - 2, r, hh * 0.62, cols, n=5, rot=rot)
        cones(t, x, y, z - 2 + hh * 0.38, r * 0.72, hh * 0.62, cols * 1.08, n=5, rot=rot + 0.6)
        t.build('pines', _mat_vcol())

    def build_dino(Z, rng):
        vu, vy = ux_at(0.80), 17000.0
        vx = vu * vy
        VR, VH, CR = 7500.0, 2700.0, 520.0
        bands = [(1900, 600, 160, 900, 58), (3600, 1100, 330, 1500, 59), (6500, 1800, 560, 2200, 60),
                 (10500, 2800, 850, 3000, 61), (18000, 4500, 1300, 4500, 62), (30000, 8000, 1800, 7000, 67)]

        def vol_z(r):
            s = np.clip((r - CR) / (VR - CR), 0, 1)
            return np.where(r < CR, VH - 170 * (1 - (r / CR) ** 2), VH * (1 - s) ** 1.9)

        def fn(x, y):
            h = 12 * fbm(x / 250, y / 250, 3, 59)
            for D, wd, A, sc, sd in bands:
                prof = fbm(x / sc, y / (sc * 2), 4, sd)
                prof = np.clip((prof - 0.2) * 1.6, 0, 1) ** 1.5
                dip = 1.0 - 0.8 * np.exp(-((x - vx) / 6000.0) ** 2) * (1 if D > 9000 else 0)
                h = np.maximum(h, A * (0.15 + 0.95 * prof) * bump(y, D, wd) * dip)
            return h

        def col(x, y, z):
            n = fbm(x / 400, y / 400, 3, 63)[:, None]
            green = lin(0x3A4A26)[None] * (0.75 + 0.5 * n)
            return green
        g = Mesh()
        terrain(g, fn, col, 420, 60000, 340, 460)
        g.build('ground', _mat_vcol(), smooth=True)

        # the volcano
        v = Mesh()
        gul = lambda T: 1 - 0.06 * ridged(np.cos(T) * 4 + 9, np.sin(T) * 4, 3, 64)

        def zv(S, T, X, Y):
            r = S * VR
            return vol_z(r) * np.where(r > CR, gul(T), 1.0)

        def vcol(x, y, z):
            f = smoothstep(300, 1300, z)[:, None]
            return lin(0x2E3A20)[None] * (1 - f) + lin(0x241C1A)[None] * f
        radial(v, vx, vy, VR, zv, vcol, nr=40, nt=96, noise=0.08, seed=65)
        v.build('volcano', _mat_vcol(), smooth=True)
        # lava streams down the flank that faces the camera
        lava = Mesh()
        for k, (th0, wob, L) in enumerate(((-1.80, 0.9, 0.36), (-1.28, -0.7, 0.24))):
            rr = np.linspace(CR * 0.95, VR * L, 60)
            th = th0 + wob * np.sin(np.linspace(0, 7, 60) + k * 2) * (rr / VR) + 0.05 * np.sin(np.linspace(0, 23, 60))
            X = vx + rr * np.cos(th)
            Y = vy + rr * np.sin(th)
            Zs = vol_z(rr) * gul(th) + 14
            w_ = np.linspace(95, 35, 60)
            heat = (np.linspace(1.0, 0.3, 60) ** 1.5)[:, None]
            cc = lin(0xFF6A18)[None] * heat
            ribbon(lava, np.stack([X, Y, Zs], -1), w_, cc)
        # the crater's glowing rim (seen only a little from here)
        th = np.linspace(0, 2 * np.pi, 49)
        ring = np.stack([vx + CR * 0.9 * np.cos(th), vy + CR * 0.9 * np.sin(th), np.full(49, VH - 60)], -1)
        ribbon(lava, ring, 120, lin(0xFF8A30)[None].repeat(49, 0))
        lava.build('lava', _mat_emit('lavaemit', 2.6), index=3)

        # a few steep jungle peaks, irregular, spread along the horizon
        k = Mesh()
        peaks = [(0.06, 7800, 520), (0.21, 5200, 380), (0.44, 9800, 640), (0.57, 6400, 330), (0.66, 12000, 700),
                 (0.95, 5600, 450), (0.33, 13500, 560)]
        for i, (fx_, yy, Hh) in enumerate(peaks):
            xx = ux_at(fx_) * yy
            R = Hh * rng.uniform(0.55, 0.8)
            b0 = fn(np.array([xx]), np.array([yy]))[0] - 20

            def zk(S, T, X, Y, Hh=Hh, b0=b0, i=i):
                lean = 0.25 * np.cos(T - rng.uniform(0, 6)) * S
                prof = (1 - S ** 1.6) ** 1.1 * (1 + 0.18 * (ridged(np.cos(T) * 2 + i, S * 4, 3, 90 + i) - 0.5))
                return b0 + Hh * np.clip(prof + lean * 0, 0, None)
            radial(k, xx, yy, R, zk,
                   lambda x, y, z: lin(0x33452A)[None].repeat(len(x), 0),
                   nr=14, nt=28, noise=0.35, seed=100 + i)
        k.build('peaks', _mat_vcol())
        # the jungle canopy
        c = Mesh()
        x, y = scatter(rng, 20000, 500, 9000)
        r = rng.uniform(10, 20, len(x)) * (1 + 1.5 * smoothstep(2000, 9000, y))
        cols = jitter_cols(rng, 0x2E4420, len(x), 0.4)
        blobs(c, x, y, fn(x, y) + r * 0.4, r, cols, seed=66)
        palms = rng.random(len(x)) < 0.0
        c.build('canopy', _mat_vcol())
        return dict(crater=(vx, vy, VH))

    def build_bay(Z, rng):
        s = Mesh()
        s.add([(-90000, 20, 0), (90000, 20, 0), (90000, 90000, 0), (-90000, 90000, 0)], [[0, 1, 2, 3]],
              lin(0x0C2236))
        s.build('sea', _mat_vcol(rough=0.35, spec=0.4), index=1)
        # the headland on the right, with the lighthouse at its tip
        hu = ux_at(0.80)
        hx, hy = hu * 2700, 2700.0

        def head(x, y):
            m = np.exp(-(((x - (hx + 1500)) / 1900.0) ** 2) - ((y - (hy + 450)) / 900.0) ** 2)
            m = m + 0.35 * (fbm(x / 600, y / 600, 4, 70) - 0.5)
            t = np.clip((m - 0.32) * 5.0, 0, 1)
            return (t ** 0.3) * (100 + 150 * fbm(x / 800, y / 800, 3, 71) * smoothstep(hx, hx + 2500, x)) - 3

        g = Mesh()
        ys = np.linspace(hy - 1300, hy + 2200, 220)
        xs = np.linspace(hx - 1200, hx + 4200, 280)
        X, Y = np.meshgrid(xs, ys)
        Zh = head(X, Y)
        ys_h = ys
        verts = np.stack([X, Y, Zh], -1).reshape(-1, 3)
        nr, nc = X.shape
        idx = np.arange(nr * nc).reshape(nr, nc)
        q = np.stack([idx[:-1, :-1], idx[:-1, 1:], idx[1:, 1:], idx[1:, :-1]], -1).reshape(-1, 4)
        n = fbm(verts[:, 0] / 200, verts[:, 1] / 200, 3, 72)[:, None]
        g.add(verts, q, lin(0x1E2E3A)[None] * (0.7 + 0.5 * n))
        # the far shore on the left: low hills with the harbour town's lights
        lu = (ux_at(0.0), ux_at(0.30))

        def shore(x, y):
            u = x / y
            m = smoothstep(lu[1], lu[1] - 0.25, u)
            return m * (80 + 420 * fbm(x / 3000, 7.0, 4, 73)) * bump(y, 15000, 3500) - 5
        ys = np.linspace(9000, 24000, 120)
        us = np.linspace(-1.1, lu[1] + 0.05, 200)
        Y, Uu = np.meshgrid(ys, us, indexing='ij')
        X = Uu * Y
        Zs = shore(X, Y)
        verts = np.stack([X, Y, Zs], -1).reshape(-1, 3)
        nr, nc = X.shape
        idx = np.arange(nr * nc).reshape(nr, nc)
        q = np.stack([idx[:-1, :-1], idx[:-1, 1:], idx[1:, 1:], idx[1:, :-1]], -1).reshape(-1, 4)
        g.add(verts, q, lin(0x1A2A38))
        g.build('land', _mat_vcol(), smooth=True)
        # town lights along the far shore
        L = Mesh()
        xs_, ys_ = [], []
        for i in range(260):
            yy = rng.uniform(11000, 13500)
            uu = rng.uniform(-1.05, lu[1] - 0.12)
            xx = uu * yy
            zz = shore(np.array([xx]), np.array([yy]))[0]
            if zz < 2 or zz > 90:
                continue
            xs_.append(xx)
            ys_.append(yy)
        xs_, ys_ = np.array(xs_), np.array(ys_)
        if len(xs_):
            cc = np.where((rng.random(len(xs_)) < 0.8)[:, None], lin(0xFFB060)[None], lin(0xB8E8FF)[None])
            boxes(L, xs_, ys_, shore(xs_, ys_) + 2, 9, 9, 7, cc)
        # the lighthouse on the headland's tip
        XX, YY = np.meshgrid(xs, ys_h)
        land = Zh > 70
        k = np.argmin(np.where(land, XX, 1e9))
        lx, ly = XX.ravel()[k] + 60, YY.ravel()[k]
        lz = head(np.array([lx]), np.array([ly]))[0] - 2
        tw = Mesh()
        k = 1.7
        cones(tw, [lx], [ly], [lz], [9.0 * k], [38.0 * k], lin(0x6E7480), n=10, rtop=0.72)
        cones(tw, [lx], [ly], [lz + 13 * k], [8.3 * k], [8.0 * k], lin(0x7A2A2A), n=10, rtop=0.95)   # the red band
        cones(tw, [lx], [ly], [lz + 30 * k], [7.4 * k], [6.0 * k], lin(0x7A2A2A), n=10, rtop=0.95)
        cones(tw, [lx], [ly], [lz + 45 * k], [7.8 * k], [6.0 * k], lin(0x2A2E36), n=10)
        boxes(tw, [lx + 20 * k], [ly + 6], [lz], [18 * k], [12 * k], [10 * k], lin(0x5A606A))   # the keeper's house
        tw.build('lighthouse', _mat_vcol())
        cones(L, [lx], [ly], [lz + 38 * k], [6.2 * k], [7.0 * k], lin(0xFFE8B0), n=10, rtop=1.0)
        L.build('lights', _mat_emit('vemit', 3.0), index=2)
        return dict(lamp=(lx, ly, lz + 41.5 * k))

    BUILDERS = dict(city=build_city, castle=build_castle, desert=build_desert, forest=build_forest,
                    dino=build_dino, bay=build_bay)

    def render_zone(zone, cache, draft, samples):
        Z = ZONES[zone]
        C.reset(zone)
        sc = bpy.context.scene
        # our own camera: perspective, at the horizon (this is not a game sprite)
        cam = sc.camera
        cam.data.type = 'PERSP'
        cam.data.sensor_fit = 'HORIZONTAL'
        cam.data.sensor_width = 36.0
        cam.data.lens = 18.0 / math.tan(math.radians(HFOV / 2))
        cam.data.clip_start = 5.0
        cam.data.clip_end = 150000.0
        cam.location = (0, 0, Z['cam_h'])
        cam.rotation_euler = (math.pi / 2 - PITCH, 0, 0)
        # the moon / sun behind the scene lights it (rim light), sky fills
        md = screen_dir(Z['body']['fx'], Z['body']['fy'])
        sun = [o for o in sc.objects if o.type == 'LIGHT'][0]
        sun.rotation_euler = (-V(md)).to_track_quat('-Z', 'Y').to_euler()
        sun.data.color = Z['moon_light'][0]
        sun.data.energy = Z['moon_light'][1]
        sun.data.angle = math.radians(4.0)
        sun.data.specular_factor = 0.0      # no lamp highlight on the sea: the painting does the glitter
        sun.visible_glossy = False          # (that is the Cycles switch; the line above is EEVEE's)
        bg = sc.world.node_tree.nodes['Background']
        bg.inputs['Color'].default_value = tuple(Z['ambient'][0]) + (1,)
        bg.inputs['Strength'].default_value = Z['ambient'][1]
        C._STATE['ground'].hide_render = True

        rng = np.random.default_rng(ORDER.index(zone) * 101 + 7)
        t0 = time.time()
        extra = BUILDERS[zone](Z, rng) or {}
        print('[%s] built in %.1f s' % (zone, time.time() - t0))

        s = 0.5 if draft else 1.0
        r = sc.render
        r.resolution_x, r.resolution_y = int(W * s), int(H * s)
        r.resolution_percentage = 100
        r.film_transparent = True
        sc.cycles.samples = samples
        sc.cycles.use_denoising = True
        sc.cycles.denoiser = 'OPENIMAGEDENOISE'
        sc.cycles.max_bounces = 3
        sc.cycles.diffuse_bounces = 2
        sc.cycles.glossy_bounces = 1
        sc.cycles.pixel_filter_type = 'BLACKMAN_HARRIS'
        sc.cycles.filter_width = 1.5
        vl = sc.view_layers[0]
        vl.use_pass_position = True
        vl.use_pass_emit = True
        vl.use_pass_object_index = True
        sc.use_nodes = True
        nt = sc.node_tree
        for n_ in list(nt.nodes):
            nt.nodes.remove(n_)
        rl = nt.nodes.new('CompositorNodeRLayers')
        fo = nt.nodes.new('CompositorNodeOutputFile')
        tmp = os.path.join(cache, '_exr_' + zone)
        shutil.rmtree(tmp, ignore_errors=True)
        os.makedirs(tmp)
        fo.base_path = tmp
        fo.format.file_format = 'OPEN_EXR'
        fo.format.color_depth = '32'
        fo.format.color_mode = 'RGBA'
        fo.file_slots.clear()
        for slot, sock in (('rgba', 'Image'), ('pos', 'Position'), ('emit', 'Emit'), ('idx', 'IndexOB')):
            fo.file_slots.new(slot)
            nt.links.new(rl.outputs[sock], fo.inputs[slot])
        r.filepath = os.path.join(tmp, 'x')
        t0 = time.time()
        bpy.ops.render.render(write_still=False)
        rt = time.time() - t0
        print('[%s] rendered in %.1f s' % (zone, rt))

        def load(slot):
            fn = [f for f in os.listdir(tmp) if f.startswith(slot)][0]
            img = bpy.data.images.load(os.path.join(tmp, fn), check_existing=False)
            w, h = img.size
            a = np.empty(w * h * 4, np.float32)
            img.pixels.foreach_get(a)
            bpy.data.images.remove(img)
            return a.reshape(h, w, 4)[::-1]
        rgba = load('rgba')
        pos = load('pos')[..., :3]
        emit = load('emit')[..., :3]
        idx = load('idx')[..., 0]
        np.savez(os.path.join(cache, zone + '.npz'), rgba=rgba.astype(np.float16), pos=pos.astype(np.float32),
                 emit=emit.astype(np.float16), idx=np.round(idx).astype(np.uint8))
        info = dict(zone=zone, w=r.resolution_x, h=r.resolution_y, cam_h=Z['cam_h'], samples=samples,
                    render_s=round(rt, 1), extra={k: list(map(float, v)) for k, v in extra.items()})
        with open(os.path.join(cache, zone + '.json'), 'w') as f:
            json.dump(info, f)
        shutil.rmtree(tmp, ignore_errors=True)
        return rt

    def blender_main():
        argv = sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else []
        import argparse
        ap = argparse.ArgumentParser()
        ap.add_argument('--out', required=True)
        ap.add_argument('--only', default='')
        ap.add_argument('--draft', action='store_true')
        ap.add_argument('--samples', type=int, default=0)
        ap.add_argument('--cache', default=os.path.join(tempfile.gettempdir(), 'mh_backdrops'))
        ap.add_argument('--no-post', action='store_true')
        a = ap.parse_args(argv)
        zones = [z for z in (a.only.split(',') if a.only else ORDER) if z]
        cache = os.path.abspath(a.cache) + ('_draft' if a.draft else '')
        os.makedirs(cache, exist_ok=True)
        samples = a.samples or (10 if a.draft else 48)
        times = {}
        for z in zones:
            times[z] = render_zone(z, cache, a.draft, samples)
        print('render times:', times)
        if not a.no_post:
            py = shutil.which('python3') or '/usr/bin/python3'
            cmd = [py, os.path.abspath(__file__), '--post', '--out', a.out, '--only', ','.join(zones),
                   '--cache', a.cache] + (['--draft'] if a.draft else [])
            subprocess.check_call(cmd)


# ===========================================================================
# Painting half (plain python3: numpy + PIL)
# ===========================================================================

def save_png(im, path):
    """Write atomically: the packer may be reading this folder."""
    tmp = path[:-4] + '.part.png'
    im.save(tmp, optimize=True)
    os.replace(tmp, path)


def gauss_blur(img, sigma):
    """Gaussian blur of an (h, w[, c]) float array, via FFT (edges wrap a little: pad)."""
    if sigma <= 0.3:
        return img
    pad = int(3 * sigma) + 2
    a = img if img.ndim == 3 else img[..., None]
    a = np.pad(a, ((pad, pad), (pad, pad), (0, 0)), mode='edge')
    h, w = a.shape[:2]
    fy = np.fft.fftfreq(h)[:, None]
    fx = np.fft.rfftfreq(w)[None, :]
    k = np.exp(-2 * (np.pi * sigma) ** 2 * (fx ** 2 + fy ** 2))
    out = np.empty_like(a)
    for c in range(a.shape[2]):
        out[..., c] = np.fft.irfft2(np.fft.rfft2(a[..., c]) * k, s=(h, w))
    out = out[pad:-pad, pad:-pad]
    return out if img.ndim == 3 else out[..., 0]


def stops_eval(stops, yf):
    """Colour gradient (linear light) at row fractions yf, smooth between stops."""
    ys = np.array([s[0] for s in stops])
    cs = np.array([lin_hex(s[1]) for s in stops])
    yf = np.clip(yf, ys[0], ys[-1])
    i = np.clip(np.searchsorted(ys, yf, side='right') - 1, 0, len(ys) - 2)
    t = (yf - ys[i]) / np.maximum(ys[i + 1] - ys[i], 1e-9)
    t = t * t * (3 - 2 * t)
    return cs[i] * (1 - t)[..., None] + cs[i + 1] * t[..., None]


def big_noise(h, w, cell, seed, octaves=5, sx=1.0, sy=1.0):
    ys, xs = np.mgrid[0:h, 0:w].astype(np.float64)
    return fbm(xs / (cell * sx), ys / (cell * sy), octaves, seed)


def silhouettes(h, w, items, ss=3):
    """Antialiased mask of polygons [(points (K, 2) in px), ...]."""
    from PIL import Image, ImageDraw
    im = Image.new('L', (w * ss, h * ss), 0)
    dr = ImageDraw.Draw(im)
    for pts in items:
        dr.polygon([(float(x) * ss, float(y) * ss) for x, y in pts], fill=255)
    im = im.resize((w, h), Image.BOX)
    return np.asarray(im, np.float64) / 255.0


BAT = np.array([(0, -0.05), (0.08, -0.16), (0.10, -0.06), (0.30, -0.28), (0.55, -0.34), (0.95, -0.18),
                (0.80, -0.08), (0.66, -0.12), (0.58, 0.00), (0.44, -0.06), (0.34, 0.06), (0.18, 0.02),
                (0.08, 0.14), (0, 0.10)])
PTERO = np.array([(0, -0.02), (0.05, -0.07), (0.30, -0.12), (0.62, -0.22), (1.0, -0.12), (0.62, -0.10),
                  (0.30, 0.02), (0.08, 0.06), (0, 0.16)])
PTERO_HEAD = np.array([(0.0, -0.06), (0.18, -0.26), (0.23, -0.30), (0.10, -0.14), (0.20, -0.06), (0.45, -0.02),
                       (0.10, 0.02), (0.0, 0.08)])


def mirror_shape(half, flap=0.0):
    """A symmetric flyer from its right half; flap raises the wing tips."""
    R = half.copy()
    R[:, 1] = R[:, 1] - flap * R[:, 0] ** 2
    L = R[::-1].copy()
    L[:, 0] = -L[:, 0]
    return np.concatenate([R, L])


def flyer_polys(shape, cx, cy, size, rot, flap, head=None):
    pts = mirror_shape(shape, flap)
    polys = []
    c, s = math.cos(rot), math.sin(rot)
    M = np.array([[c, -s], [s, c]])
    polys.append((pts @ M.T) * size + (cx, cy))
    if head is not None:
        hp = head.copy()
        hp[:, 1] = hp[:, 1] - 0.02
        polys.append((hp @ M.T) * size + (cx, cy))
    return polys


def post_zone(zone, cache, outdir, draft):
    from PIL import Image
    Z = ZONES[zone]
    d = np.load(os.path.join(cache, zone + '.npz'))
    with open(os.path.join(cache, zone + '.json')) as f:
        info = json.load(f)
    rgba = d['rgba'].astype(np.float64)
    pos = d['pos'].astype(np.float64)
    emit = d['emit'].astype(np.float64)
    idx = d['idx']
    h, w = rgba.shape[:2]
    s = w / float(W)                               # 1.0 final, 0.5 draft
    ch = info['cam_h']
    camp = np.array([0.0, 0.0, ch])
    right, up, fwd = cam_basis()
    ys, xs = np.mgrid[0:h, 0:w].astype(np.float64)
    a_ = (xs + 0.5 - w / 2) / (FPX * s)
    b_ = (h / 2 - ys - 0.5) / (FPX * s)
    ray = a_[..., None] * right + b_[..., None] * up + fwd
    ray /= np.linalg.norm(ray, axis=-1, keepdims=True)
    yf = (ys + 0.5) / h
    xf = (xs + 0.5) / w
    rng = np.random.default_rng(ORDER.index(zone) * 7 + 1)

    # ---- the sky ----
    sky = stops_eval(Z['sky'], yf)
    B = Z['body']
    bdir = screen_dir(B['fx'], B['fy'])
    cosang = np.clip(ray @ bdir, -1, 1)
    ang = np.degrees(np.arccos(cosang))
    halo_c = lin_hex(B['halo'])
    k1, k2 = B['halo_k']
    halo = (k1 * np.exp(-ang / (B['r'] * 1.6)) + k2 * np.exp(-ang / (B['r'] * 7.0)))[..., None] * halo_c
    sky = sky + halo * 0.6
    # stars
    if Z['stars'] > 0:
        n = int(1500 * Z['stars'] * s * s)
        sxs = rng.uniform(0, w, n)
        sys_ = rng.uniform(0, h * HORIZON * 0.98, n) ** 1.0
        mag = rng.random(n) ** 3.5
        star = np.zeros((h, w))
        np.add.at(star, (sys_.astype(int), sxs.astype(int)), 0.10 + 0.9 * mag)
        big = mag > 0.55
        st = gauss_blur(star, 0.55 * s * 2) * 3.0
        fade = smoothstep(HORIZON, HORIZON - 0.12, yf) * smoothstep(B['r'] * 2.2, B['r'] * 6, ang)
        sky = sky + (st * fade)[..., None] * lin_hex(Z['star_col']) * 0.32
    # the moon / sun disc
    rpx = FPX * s * math.tan(math.radians(B['r']))
    bx = w / 2 + (bdir @ right) / (bdir @ fwd) * FPX * s
    by = h / 2 - (bdir @ up) / (bdir @ fwd) * FPX * s
    dx, dy = (xs + 0.5 - bx) / rpx, (ys + 0.5 - by) / rpx
    rr = np.sqrt(dx * dx + dy * dy)
    bc = lin_hex(B['col'])
    mu = np.sqrt(np.clip(1 - rr * rr, 0, 1))
    if B['kind'] == 'moon':
        disc = np.clip((1.0 - rr) * rpx / 1.4 + 0.5, 0, 1)
        maria = fbm(dx * 1.3 + 11, dy * 1.3 + 3, 4, 91)
        tex = 0.94 - 0.16 * smoothstep(0.45, 0.62, maria) - 0.05 * smoothstep(0.5, 0.8, fbm(dx * 6, dy * 6, 3, 92))
        moon = bc * (tex * (0.80 + 0.20 * mu))[..., None]
        # a veil of haze in front: bright, but not a white sticker
        moon = moon * 0.66 + sky * 0.18
        sky = sky * (1 - disc[..., None]) + moon * disc[..., None]
    else:
        disc = np.clip((1.0 - rr) * rpx / 3.0 + 0.5, 0, 1)
        moon = bc * (0.86 + 0.14 * mu)[..., None] * B.get('bright', 0.85)
        sky = sky * (1 - disc[..., None]) + np.maximum(moon, sky) * disc[..., None]
    # clouds
    cl = Z['clouds']
    if cl['amt'] > 0:
        cell = 260 * s
        n1 = big_noise(h, w, cell, cl['seed'], 6, cl['sx'], 1.0 / cl['sy'] * 2.2)
        n2 = big_noise(h, w, cell * 0.35, cl['seed'] + 5, 4, 3.0, 0.5)
        dens = smoothstep(cl['cover'], cl['cover'] + 0.18, n1 * 0.85 + n2 * 0.15)
        band = smoothstep(cl['y0'], cl['y0'] + 0.06, yf) * smoothstep(cl['y1'], cl['y1'] - 0.08, yf)
        alpha = dens * band * cl['amt']
        lit = np.clip(np.exp(-ang / (B['r'] * 3.0)) * 1.2 + 0.25 * np.exp(-ang / (B['r'] * 12)), 0, 1)
        edge = np.clip(1.0 - dens, 0, 1)
        ccol = lin_hex(cl['col'])[None, None] * (1 - lit[..., None]) + lin_hex(cl['lit'])[None, None] * lit[..., None]
        ccol = ccol * (0.85 + 0.35 * edge[..., None] * lit[..., None])
        sky = sky * (1 - alpha[..., None]) + ccol * alpha[..., None]

    # ---- the geometry, with atmospheric perspective and valley mist ----
    al = rgba[..., 3]
    ok = al > 1e-4
    col = np.where(ok[..., None], rgba[..., :3] / np.maximum(al, 1e-4)[..., None], 0)
    em = np.where(ok[..., None], emit / np.maximum(al, 1e-4)[..., None], 0)
    base = np.clip(col - em, 0, None)
    rel = pos - camp
    dist = np.linalg.norm(rel, axis=-1)
    dist = np.where(ok, dist, 1e6)
    kz = np.where(ok, (pos[..., 2] - ch) / Z['fog_hf'], 0)
    kfac = np.where(np.abs(kz) < 1e-4, 1.0, (1 - np.exp(-np.clip(kz, -60, 60))) / np.where(np.abs(kz) < 1e-4, 1, kz))
    tau = Z['fog_b'] * dist + Z['fog_a'] * math.exp(-ch / Z['fog_hf']) * dist * np.clip(kfac, 0, 1e8)
    # mist texture: a few bands of thicker mist
    mt = big_noise(h, w, 180 * s, 77, 4, 4.0, 0.6)
    tau = tau * (0.8 + 0.5 * mt)
    fogf = 1 - np.exp(-tau)
    fogc = stops_eval(Z['fog'], yf) + halo * 0.35 * smoothstep(0.55, 0.38, yf)[..., None]
    geo = base * (1 - fogf[..., None]) + fogc * fogf[..., None] + em * np.exp(-dist / 30000.0)[..., None]
    img = geo * al[..., None] + sky * (1 - al[..., None])

    # ---- zone extras ----
    extra = info.get('extra', {})
    glow_add = np.zeros_like(img)

    def occl_depth(dmin):
        """1 where nothing nearer than dmin covers the pixel."""
        return np.where(ok & (dist < dmin), 1 - al, 1.0)

    if zone == 'castle' or zone == 'dino':
        polys = []
        if zone == 'castle':
            groups = [(0.40, 0.24, 7, 0.07), (0.57, 0.19, 5, 0.06), (0.86, 0.27, 6, 0.05), (0.10, 0.31, 4, 0.05),
                      (0.66, 0.33, 3, 0.04)]
            for gx, gy, n, spread in groups:
                for i in range(n):
                    cx = (gx + rng.normal(0, spread)) * w
                    cy = (gy + rng.normal(0, spread * 0.45)) * h
                    size = rng.uniform(16, 30) * s
                    polys += flyer_polys(BAT, cx, cy, size, rng.normal(0, 0.25), rng.uniform(-0.6, 0.9))
            sil = silhouettes(h, w, polys)
            scol = lin_hex(0x140A20)
        else:
            groups = [(0.53, 0.23, 7, 0.05), (0.13, 0.31, 2, 0.03), (0.68, 0.34, 3, 0.03)]
            for gx, gy, n, spread in groups:
                for i in range(n):
                    cx = (gx + rng.normal(0, spread)) * w
                    cy = (gy + rng.normal(0, spread * 0.5)) * h
                    size = rng.uniform(28, 46) * s
                    polys += flyer_polys(PTERO, cx, cy, size, rng.normal(0, 0.12), rng.uniform(-0.4, 0.4),
                                         head=PTERO_HEAD)
            sil = silhouettes(h, w, polys)
            scol = lin_hex(0x4A2E20)
        # a touch of the sky through them: they are far
        sc_ = scol * 0.7 + stops_eval(Z['sky'], yf) * 0.3
        img = img * (1 - sil[..., None] * 0.92) + sc_ * (sil[..., None] * 0.92)

    if zone == 'dino' and 'crater' in extra:
        vx, vy, vh = extra['crater']
        px, py, _ = project(np.array([[vx, vy, vh]]), ch, w, h)
        px, py = px[0], py[0]
        t = np.clip((py - ys) / (h * 0.40), 0, None)          # 0 at the crater, up
        cxl = px - (t ** 1.4) * w * 0.20 + np.sin(t * 5) * 12 * s
        width = (30 + 330 * t ** 0.85) * s
        prof = np.exp(-((xs - cxl) / width) ** 2)
        tex = big_noise(h, w, 70 * s, 81, 5, 1.0, 1.0)
        tex2 = big_noise(h, w, 25 * s, 82, 3, 1.0, 1.0)
        dens = np.clip(prof * (0.45 + 0.9 * tex + 0.25 * tex2) - 0.3, 0, 1) * smoothstep(0, 0.03, t)
        dens = dens * smoothstep(1.25, 0.6, t) * (ys < py)
        dens = np.clip(dens * 1.8, 0, 0.8)
        occ = occl_depth(np.linalg.norm(np.array([vx, vy, vh]) - camp) * 0.85)
        dens = dens * occ
        lava = lin_hex(0xFF7A30)
        smoke = lin_hex(0x5A4636) * (0.7 + 0.6 * tex[..., None]) + lava * 0.30 * np.exp(-t / 0.08)[..., None]
        img = img * (1 - dens[..., None]) + smoke * dens[..., None]
        # the glow over the crater
        gr = np.sqrt((xs - px) ** 2 + ((ys - py) * 1.6) ** 2)
        glow_add += (np.exp(-gr / (60 * s)) * 0.30 + np.exp(-gr / (180 * s)) * 0.08)[..., None] * lava * \
            (ys < py + 10 * s)[..., None]

    if zone == 'bay':
        sea = (idx == 1) & ok
        # waves and the moon's glitter path, from the world position on the sea
        X, Y = pos[..., 0], pos[..., 1]
        fy_ = (dist ** 2) / (ch * FPX * s)                 # metres of sea per pixel row
        wn = lambda x, y: fbm(x / 22.0, y / 5.0, 4, 95) + 0.5 * fbm(x / 7.0, y / 2.2, 3, 96)
        fade = np.clip(1.0 - fy_ / 8.0, 0, 1)               # the texture washes out far away
        # the moon's path: a wavy sea reflects the moon over a column below it,
        # tall in elevation (the wave slopes) and narrow in azimuth
        el_m = math.degrees(math.asin(bdir[2]))
        az_m = math.degrees(math.atan2(bdir[0], bdir[1]))
        el_r = -np.degrees(np.arcsin(np.clip(ray[..., 2], -1, 1)))      # reflected elevation (flat sea)
        az_r = np.degrees(np.arctan2(ray[..., 0], ray[..., 1]))
        col_ = np.exp(-((az_r - az_m) / (B['r'] * 0.75 + 1.6 * np.clip(el_r / 30.0, 0, 1))) ** 2)
        col_ = col_ * np.exp(-((el_r - el_m) / 16.0) ** 2)
        sparkle = smoothstep(0.50, 0.72, fbm(X / 5.0, Y / 0.9, 4, 98))    # broken into dashes
        sp = fade * sparkle * 1.6 + (1 - fade) * 0.55
        glit = col_ * sp * np.where(sea, 1.0, 0.0)
        wave_tone = (wn(X * 0.5, Y * 0.5) - 0.5) * fade
        img = img * (1 + 0.25 * wave_tone[..., None] * sea[..., None])
        img = img + glit[..., None] * lin_hex(B['col']) * 0.30
        # plankton: glowing dots in drifts on the water
        n = int(22000 * s)
        wy = 1.0 / rng.uniform(1.0 / 6000.0, 1.0 / 60.0, n)          # even on the screen
        wx = rng.uniform(-0.75, 0.75, n) * wy
        dists = wy
        drift = fbm(wx / 70.0, wy / (18.0 + wy * 0.08), 4, 97)
        keep = rng.random(n) < smoothstep(0.55, 0.75, drift) * 0.6
        wx, wy, dists = wx[keep], wy[keep], dists[keep]
        P = np.stack([wx, wy, np.zeros_like(wx)], -1)
        ppx, ppy, _ = project(P, ch, w, h)
        inb = (ppx >= 0) & (ppx < w - 1) & (ppy >= 0) & (ppy < h - 1)
        ppx, ppy, dists = ppx[inb], ppy[inb], dists[inb]
        ix, iy = ppx.astype(int), ppy.astype(int)
        vis = sea[iy, ix]
        ix, iy, dists = ix[vis], iy[vis], dists[vis]
        mag = rng.random(len(ix)) ** 2 * np.clip(400.0 / dists, 0.25, 1.0)
        cyan = rng.random(len(ix)) < 0.85
        buf = np.zeros((h, w, 3))
        np.add.at(buf, (iy[cyan], ix[cyan]), mag[cyan, None] * lin_hex(0xA8F8FF))
        np.add.at(buf, (iy[~cyan], ix[~cyan]), mag[~cyan, None] * lin_hex(0xFF6AD8))
        dots = gauss_blur(buf, 0.8 * s * 2) * 4.0
        img = img + dots * 0.45
        glow_add += gauss_blur(buf, 7 * s) * 2.2
        # the lighthouse beam
        if 'lamp' in extra:
            lx, ly, lz = extra['lamp']
            lp = np.array([lx, ly, lz])
            px, py, _ = project(lp[None], ch, w, h)
            px, py = px[0], py[0]
            ang0 = math.radians(184)                      # pointing left, a little up, under the moon
            vx_, vy_ = xs - px, ys - py
            rlen = np.sqrt(vx_ ** 2 + vy_ ** 2) + 1e-6
            a = np.arctan2(vy_, vx_)
            da = np.angle(np.exp(1j * (a - ang0)))
            spread_ = math.radians(2.2) + 0.00002 * rlen / s
            beam = np.exp(-(da / spread_) ** 2) * (1.0 / (1 + rlen / (380 * s))) * smoothstep(0, 14 * s, rlen)
            beam2 = np.exp(-(np.angle(np.exp(1j * (a - ang0 - math.pi))) / (spread_ * 1.4)) ** 2) * \
                (1.0 / (1 + rlen / (120 * s))) * 0.5
            occ = occl_depth(np.linalg.norm(lp - camp) * 0.97)
            bm = (beam * 0.13 + beam2 * 0.08) * occ
            img = img + bm[..., None] * lin_hex(0xFFE6B0)
            lr = np.sqrt((xs - px) ** 2 + (ys - py) ** 2)
            glow_add += (np.exp(-lr / (5 * s)) * 1.0 + np.exp(-lr / (26 * s)) * 0.18)[..., None] * lin_hex(0xFFE0A0)

    if Z.get('haze'):
        # heat haze: the rows around the horizon wobble and lift
        band = np.exp(-((yf - HORIZON - 0.012) / 0.03) ** 2)
        ph = big_noise(h, 1, 1, 83, 2)[:, 0]
        off = (np.sin(ys * 0.9 / s + ph[:, None] * 20) * 1.0 * s + (big_noise(h, w, 40 * s, 84, 3, 3.0, 0.15) - 0.5) *
               2.5 * s) * band
        xsh = np.clip(xs + off, 0, w - 1.001)
        x0 = np.floor(xsh).astype(int)
        fr = (xsh - x0)[..., None]
        yi = ys.astype(int)
        img = img[yi, x0] * (1 - fr) + img[yi, x0 + 1] * fr
        img = img * (1 - 0.25 * band[..., None]) + stops_eval(Z['fog'], np.full_like(yf, HORIZON - 0.02)) * \
            (0.25 * band[..., None])

    # ---- darkening towards the bottom: the level floats high over this ----
    y0, pw = Z['dark']
    m = smoothstep(y0, 1.0, yf) ** pw
    img = img * (1 - m[..., None]) + lin_hex(Z['bot']) * m[..., None]
    # the very top row is the zone's void_top
    mt_ = smoothstep(0.03, 0.0, yf)
    img = img * (1 - mt_[..., None]) + lin_hex(Z['top']) * mt_[..., None]

    # ---- bloom: glows and the bright moon ----
    lum = img @ np.array([0.2126, 0.7152, 0.0722])
    bright = np.clip(lum - 0.28, 0, None)[..., None] * img / np.maximum(lum, 1e-4)[..., None]
    glowsrc = em * al[..., None] * np.exp(-dist / 30000.0)[..., None]
    bl = gauss_blur(bright + glowsrc * 0.5, 5 * s) * 0.35 + gauss_blur(bright + glowsrc * 0.5, 22 * s) * 0.25
    img = img + bl + glow_add

    # ---- out ----
    out = to_srgb(img)
    out = out * 255.0 + rng.uniform(-0.5, 0.5, out.shape)          # dither against banding
    out8 = np.clip(np.round(out), 0, 255).astype(np.uint8)
    os.makedirs(outdir, exist_ok=True)
    name = 'backdrop_%s' % zone
    save_png(Image.fromarray(out8), os.path.join(outdir, name + '.png'))
    # the 1x: an area average in linear light
    lin_img = np.clip(img, 0, None)
    h2, w2 = h // 2, w // 2
    half = lin_img[:h2 * 2, :w2 * 2].reshape(h2, 2, w2, 2, 3).mean(axis=(1, 3))
    o1 = to_srgb(half) * 255.0 + rng.uniform(-0.5, 0.5, half.shape)
    save_png(Image.fromarray(np.clip(np.round(o1), 0, 255).astype(np.uint8)), os.path.join(outdir, name + '_1x.png'))
    return dict(w=w, h=h, horizon=HORIZON)


def contact_sheet(outdir):
    from PIL import Image, ImageDraw, ImageFont
    tw, th = 640, 443
    pad, lab = 16, 34
    cols = 3
    rows = 2
    sheet = Image.new('RGB', (cols * tw + (cols + 1) * pad, rows * (th + lab) + (rows + 1) * pad), (18, 18, 22))
    dr = ImageDraw.Draw(sheet)
    try:
        font = ImageFont.load_default(size=22)
    except TypeError:
        font = ImageFont.load_default()
    for i, z in enumerate(ORDER):
        p = os.path.join(outdir, 'backdrop_%s_1x.png' % z)
        if not os.path.exists(p):
            continue
        im = Image.open(p).convert('RGB').resize((tw, th), Image.LANCZOS)
        x = pad + (i % cols) * (tw + pad)
        y = pad + (i // cols) * (th + lab + pad)
        dr.text((x + 2, y + 4), TITLES[z], fill=(225, 225, 232), font=font)
        sheet.paste(im, (x, y + lab))
        hy = y + lab + int(th * HORIZON)
        dr.line([(x - 6, hy), (x - 1, hy)], fill=(200, 200, 90), width=2)
    save_png(sheet, os.path.join(outdir, '_sheet.png'))


def post_main():
    import argparse
    ap = argparse.ArgumentParser()
    ap.add_argument('--post', action='store_true')
    ap.add_argument('--out', required=True)
    ap.add_argument('--only', default='')
    ap.add_argument('--draft', action='store_true')
    ap.add_argument('--cache', default=os.path.join(tempfile.gettempdir(), 'mh_backdrops'))
    a = ap.parse_args()
    zones = [z for z in (a.only.split(',') if a.only else ORDER) if z]
    cache = os.path.abspath(a.cache) + ('_draft' if a.draft else '')
    outdir = os.path.abspath(a.out)
    if a.draft:                         # drafts stay out of the project
        outdir = os.path.join(cache, 'out')
    meta_p = os.path.join(outdir, 'meta.json')
    meta = {}
    if os.path.exists(meta_p):
        with open(meta_p) as f:
            meta = json.load(f)
    for z in zones:
        if not os.path.exists(os.path.join(cache, z + '.npz')):
            print('no cached render for', z)
            continue
        t0 = time.time()
        info = post_zone(z, cache, outdir, a.draft)
        meta['backdrop_%s' % z] = dict(w=W, h=H, horizon=HORIZON, w_1x=W // 2, h_1x=H // 2,
                                       files=dict(img='backdrop_%s.png' % z, img_1x='backdrop_%s_1x.png' % z))
        if a.draft:
            meta['backdrop_%s' % z].update(w=info['w'], h=info['h'])
        print('[%s] painted in %.1f s' % (z, time.time() - t0))
    with open(meta_p + '.part', 'w') as f:
        json.dump(meta, f, indent=1, sort_keys=True)
    os.replace(meta_p + '.part', meta_p)
    contact_sheet(outdir)


if __name__ == '__main__':
    if IN_BLENDER:
        blender_main()
    else:
        post_main()
