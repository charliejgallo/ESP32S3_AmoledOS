"""Monster Hop - desert & forest zones: Blender helpers shared by desert.py and
forest.py (private to those two scripts; the shared module is mh_common).

  * image textures made from numpy arrays (zones_df_tex) and the two tile
    materials that read them: tops mapped by (x, y), sides by (x + y, z / FLOOR)
    so the front and right faces of neighbouring blocks continue each other
  * block / surface geometry built at cell (0, 0), floor 0
  * mesh helpers for props (lathe, tube, blob, ribbon) with smooth normals
  * render wrappers: tiles, props (+ shadow, + glow with a light pool big
    enough for the glow), a contact sheet
"""
import math
import os
import sys
import time

import bmesh
import bpy
import numpy as np
from mathutils import Vector as V

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import mh_common as C  # noqa: E402
import zones_df_tex as T  # noqa: E402

F = C.FLOOR_M


# ---------------------------------------------------------------------------
# command line: mh_common's flags plus --sample
# ---------------------------------------------------------------------------

def args():
    """mh_common's flags plus --sample. argparse would read `--sample` as an
    abbreviation of `--samples`, so it is taken out of argv first."""
    sample = '--sample' in sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else False
    if sample:
        i = sys.argv.index('--')
        sys.argv[i + 1:] = [s for s in sys.argv[i + 1:] if s != '--sample']
    a = C.args({'samples': 64})
    a.sample = sample
    return a


def wanted(a, name, sample_set):
    if a.only:
        return name in a.only
    if a.sample:
        return name in sample_set
    return True


# ---------------------------------------------------------------------------
# images from numpy
# ---------------------------------------------------------------------------

def image(name, arr, noncolor=False):
    """A float image from a (h, w, 3) linear colour or (h, w) scalar array,
    row 0 at the bottom."""
    arr = np.asarray(arr, np.float32)
    h, w = arr.shape[:2]
    if arr.ndim == 2:
        arr = np.repeat(arr[..., None], 3, axis=2)
    rgba = np.ones((h, w, 4), np.float32)
    rgba[..., :3] = arr
    old = bpy.data.images.get(name)
    if old is not None:
        bpy.data.images.remove(old)
    im = bpy.data.images.new(name, w, h, alpha=False, float_buffer=True)
    im.colorspace_settings.name = 'Non-Color' if noncolor else 'Linear'
    im.pixels.foreach_set(rgba.ravel())
    im.pack()
    return im


# ---------------------------------------------------------------------------
# node helpers
# ---------------------------------------------------------------------------

class NB:
    """Tiny node-tree builder: sockets or numbers everywhere."""

    def __init__(self, nt):
        self.nt = nt

    def _in(self, sock, val):
        if hasattr(val, 'is_linked') or hasattr(val, 'links'):
            self.nt.links.new(val, sock)
        elif isinstance(val, (tuple, list)):
            sock.default_value = tuple(val) if len(sock.default_value) == len(val) else tuple(val) + (1.0,)
        else:
            sock.default_value = val

    def node(self, kind, **kw):
        n = self.nt.nodes.new(kind)
        for k, v in kw.items():
            setattr(n, k, v)
        return n

    def math(self, op, a, b=0.0, clamp=False):
        n = self.node('ShaderNodeMath', operation=op, use_clamp=clamp)
        self._in(n.inputs[0], a)
        self._in(n.inputs[1], b)
        return n.outputs[0]

    def objco(self):
        tc = self.node('ShaderNodeTexCoord')
        sep = self.node('ShaderNodeSeparateXYZ')
        self.nt.links.new(tc.outputs['Object'], sep.inputs[0])
        return sep.outputs['X'], sep.outputs['Y'], sep.outputs['Z']

    def combine(self, x, y, z=0.0):
        n = self.node('ShaderNodeCombineXYZ')
        self._in(n.inputs[0], x)
        self._in(n.inputs[1], y)
        self._in(n.inputs[2], z)
        return n.outputs[0]

    def img(self, im, vec, interp='Linear'):
        n = self.node('ShaderNodeTexImage')
        n.image = im
        n.interpolation = interp
        n.extension = 'REPEAT'
        self.nt.links.new(vec, n.inputs['Vector'])
        return n.outputs['Color']

    def mix(self, fac, a, b):
        n = self.node('ShaderNodeMixRGB', blend_type='MIX')
        self._in(n.inputs[0], fac)
        self._in(n.inputs[1], a)
        self._in(n.inputs[2], b)
        return n.outputs[0]

    def bw(self, col):
        n = self.node('ShaderNodeRGBToBW')
        self.nt.links.new(col, n.inputs[0])
        return n.outputs[0]

    def attr(self, name):
        n = self.node('ShaderNodeAttribute')
        n.attribute_name = name
        return n

    def bump(self, h, dist=1.0, strength=1.0):
        n = self.node('ShaderNodeBump')
        n.inputs['Strength'].default_value = strength
        n.inputs['Distance'].default_value = dist
        self.nt.links.new(h, n.inputs['Height'])
        return n.outputs['Normal']

    def principled(self, col, rough=0.8, spec=0.3, normal=None, emit=None, emit_strength=0.0,
                   metal=0.0, rough_sock=None):
        b = self.node('ShaderNodeBsdfPrincipled')
        self._in(b.inputs['Base Color'], col)
        b.inputs['Roughness'].default_value = rough
        if rough_sock is not None:
            self.nt.links.new(rough_sock, b.inputs['Roughness'])
        b.inputs['Specular'].default_value = spec
        b.inputs['Metallic'].default_value = metal
        if normal is not None:
            self.nt.links.new(normal, b.inputs['Normal'])
        if emit is not None:
            self._in(b.inputs['Emission'], emit)
            b.inputs['Emission Strength'].default_value = emit_strength
        return b.outputs['BSDF']


# ---------------------------------------------------------------------------
# tile materials
# ---------------------------------------------------------------------------

def _imgs(name, tex):
    col = image(name + '.col', tex.col)
    hgt = image(name + '.hgt', tex.hgt, noncolor=True)
    emi = image(name + '.emi', tex.emit) if tex.emit is not None else None
    rou = image(name + '.rou', tex.rough, noncolor=True) if tex.rough is not None else None
    return col, hgt, emi, rou


def tile_mat(key, tex, mapping='top', tex_right=None, rough=0.85, spec=0.25, bump=1.0,
             emit_strength=0.0, metal=0.0):
    """Register a material reading numpy textures.
    mapping 'top': (x, y); 'side': (x + y, z / FLOOR_M + 1) with tex_right (if
    given) on faces whose normal points +X; 'uv': the mesh's UV map."""
    imgs = _imgs(key, tex)
    imgs_r = _imgs(key + '_r', tex_right) if tex_right is not None else None

    def build(nt, neutral):
        nb = NB(nt)
        if mapping == 'uv':
            uvn = nb.node('ShaderNodeUVMap')
            vec = uvn.outputs['UV']
        else:
            x, y, z = nb.objco()
            if mapping == 'top':
                vec = nb.combine(x, y)
            else:
                vec = nb.combine(nb.math('ADD', x, y), nb.math('ADD', nb.math('MULTIPLY', z, 1.0 / F), 1.0))
        col = nb.img(imgs[0], vec)
        hgt = nb.bw(nb.img(imgs[1], vec))
        emi = nb.img(imgs[2], vec) if imgs[2] is not None else None
        rou = nb.bw(nb.img(imgs[3], vec)) if imgs[3] is not None else None
        if imgs_r is not None:
            geo = nb.node('ShaderNodeNewGeometry')
            sep = nb.node('ShaderNodeSeparateXYZ')
            nt.links.new(geo.outputs['Normal'], sep.inputs[0])
            right = nb.math('GREATER_THAN', sep.outputs['X'], 0.5)
            col = nb.mix(right, col, nb.img(imgs_r[0], vec))
            hgt = nb.math('ADD', nb.math('MULTIPLY', hgt, nb.math('SUBTRACT', 1.0, right)),
                          nb.math('MULTIPLY', nb.bw(nb.img(imgs_r[1], vec)), right))
            if emi is not None and imgs_r[2] is not None:
                emi = nb.mix(right, emi, nb.img(imgs_r[2], vec))
        normal = nb.bump(hgt, 1.0, bump) if bump > 0 else None
        base = (0.8, 0.8, 0.8, 1.0) if neutral else col
        met = None
        if tex.metal is not None:
            met = nb.bw(nb.img(image(key + '.met', tex.metal, noncolor=True), vec))
        b = nb.principled(base, rough, spec, normal, emi, emit_strength if emi is not None else 0.0,
                          metal=metal, rough_sock=rou)
        if met is not None:
            nt.links.new(met, b.node.inputs['Metallic'])
        return b
    return C.mat(key, build=build)


def attr_mat(key, rough=0.7, spec=0.3, emit_attr=None, emit_strength=0.0, metal=0.0, bump_noise=0.0,
             bump_scale=40.0):
    """A material whose base colour is the mesh's 'col' colour attribute."""
    def build(nt, neutral):
        nb = NB(nt)
        a = nb.attr('col')
        normal = None
        if bump_noise > 0:
            nz = nb.node('ShaderNodeTexNoise')
            nz.inputs['Scale'].default_value = bump_scale
            nz.inputs['Detail'].default_value = 3.0
            tc = nb.node('ShaderNodeTexCoord')
            nt.links.new(tc.outputs['Object'], nz.inputs['Vector'])
            normal = nb.bump(nz.outputs['Fac'], 0.02, bump_noise)
        emit = None
        if emit_attr:
            emit = nb.attr(emit_attr).outputs['Color']
        base = (0.8, 0.8, 0.8, 1.0) if neutral else a.outputs['Color']
        return nb.principled(base, rough, spec, normal, emit, emit_strength, metal)
    return C.mat(key, build=build)


def noise_mat(key, c1, c2, scale=8.0, rough=0.7, spec=0.3, bump=0.0, metal=0.0, detail=4.0,
              emit=None, emit_strength=0.0, aniso=None):
    """Two colours (sRGB 0-255) mixed by object-space noise, optional bump."""
    l1 = tuple(T.rgb(*c1)) + (1.0,)
    l2 = tuple(T.rgb(*c2)) + (1.0,)

    def build(nt, neutral):
        nb = NB(nt)
        tc = nb.node('ShaderNodeTexCoord')
        nz = nb.node('ShaderNodeTexNoise')
        nz.inputs['Scale'].default_value = scale
        nz.inputs['Detail'].default_value = detail
        if aniso is not None:
            mp = nb.node('ShaderNodeMapping')
            mp.inputs['Scale'].default_value = aniso
            nt.links.new(tc.outputs['Object'], mp.inputs['Vector'])
            nt.links.new(mp.outputs['Vector'], nz.inputs['Vector'])
        else:
            nt.links.new(tc.outputs['Object'], nz.inputs['Vector'])
        ramp = nb.node('ShaderNodeValToRGB')
        ramp.color_ramp.elements[0].position = 0.3
        ramp.color_ramp.elements[1].position = 0.7
        ramp.color_ramp.elements[0].color = l1
        ramp.color_ramp.elements[1].color = l2
        nt.links.new(nz.outputs['Fac'], ramp.inputs[0])
        normal = nb.bump(nz.outputs['Fac'], 0.02, bump) if bump > 0 else None
        base = (0.8, 0.8, 0.8, 1.0) if neutral else ramp.outputs[0]
        em = tuple(T.rgb(*emit)) + (1.0,) if emit is not None else None
        return nb.principled(base, rough, spec, normal, em, emit_strength, metal)
    return C.mat(key, build=build)


def flat(key, c, rough=0.7, spec=0.3, metal=0.0, emit=None, emit_strength=0.0):
    """A plain material from an sRGB 0-255 colour."""
    return C.mat(key, base=tuple(T.rgb(*c)), rough=rough, metal=metal, spec=spec,
                 emit=tuple(T.rgb(*emit)) if emit is not None else None, emit_strength=emit_strength)


# ---------------------------------------------------------------------------
# geometry
# ---------------------------------------------------------------------------

def _finish(ob, smooth=False, recalc=True):
    me = ob.data
    if recalc:
        bm = bmesh.new()
        bm.from_mesh(me)
        bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
        bm.to_mesh(me)
        bm.free()
    for p in me.polygons:
        p.use_smooth = smooth
    me.update()
    return ob


def mesh(name, verts, faces, keys, mat_idx=None, smooth=False, recalc=True, cols=None, uvs=None):
    """Mesh object with one or more registered material keys (face indices in
    mat_idx), optional per-vertex 'col' colour attribute (linear RGB) and
    per-loop UVs."""
    me = bpy.data.meshes.new(name)
    me.from_pydata([tuple(map(float, v)) for v in verts], [], [tuple(int(i) for i in f) for f in faces])
    me.validate()
    ob = bpy.data.objects.new(name, me)
    C.link(ob)
    if isinstance(keys, str):
        keys = [keys]
    for k in keys:
        C.assign(ob, k)
    if mat_idx is not None:
        for p, i in zip(me.polygons, mat_idx):
            p.material_index = int(i)
    if cols is not None:
        ca = me.color_attributes.new('col', 'FLOAT_COLOR', 'POINT')
        c4 = np.ones((len(cols), 4), np.float32)
        c4[:, :3] = np.asarray(cols, np.float32)
        ca.data.foreach_set('color', c4.ravel())
    if uvs is not None:
        uvl = me.uv_layers.new(name='UVMap')
        uvl.data.foreach_set('uv', np.asarray(uvs, np.float32).ravel())
    return _finish(ob, smooth, recalc)


def block(name, top_key, side_key, chamfer=0.0, z0=None, top_z=0.0, inset=0.0):
    """A block filling cell (0, 0) (less `inset` on every side), top at top_z,
    bottom at z0 (default one floor down), optionally chamfered top edges."""
    c = chamfer
    zb = top_z - F if z0 is None else z0
    zt = top_z
    zc = zt - c
    a, b = inset, 1 - inset
    vs = [(a + c, a + c, zt), (b - c, a + c, zt), (b - c, b - c, zt), (a + c, b - c, zt),
          (a, a, zc), (b, a, zc), (b, b, zc), (a, b, zc),
          (a, a, zb), (b, a, zb), (b, b, zb), (a, b, zb)]
    fs = [(0, 1, 2, 3),
          (4, 5, 1, 0), (5, 6, 2, 1), (6, 7, 3, 2), (7, 4, 0, 3),
          (8, 9, 5, 4), (9, 10, 6, 5), (10, 11, 7, 6), (11, 8, 4, 7),
          (8, 11, 10, 9)]
    mi = [0, 0, 0, 0, 0, 1, 1, 1, 1, 1]
    if c <= 0:
        vs = vs[4:]
        fs = [(0, 1, 2, 3), (4, 5, 1, 0), (5, 6, 2, 1), (6, 7, 3, 2), (7, 4, 0, 3), (4, 7, 6, 5)]
        mi = [0, 1, 1, 1, 1, 1]
    return mesh(name, vs, fs, [top_key, side_key], mi)


def grid_top(name, key, hfun, n=40, thick=0.12, side_key=None, x0=0.0, x1=1.0, y0=0.0, y1=1.0):
    """A 1 x 1 slab whose top is the height field hfun(x, y) (arrays, metres,
    0 at the rim), with vertical skirts down to -thick."""
    xs = np.linspace(x0, x1, n + 1)
    ys = np.linspace(y0, y1, n + 1)
    X, Y = np.meshgrid(xs, ys)
    Z = hfun(X, Y)
    verts = [(X[j, i], Y[j, i], Z[j, i]) for j in range(n + 1) for i in range(n + 1)]
    idx = lambda i, j: j * (n + 1) + i  # noqa: E731
    faces = [(idx(i, j), idx(i + 1, j), idx(i + 1, j + 1), idx(i, j + 1)) for j in range(n) for i in range(n)]
    mi = [0] * len(faces)
    # skirt: the rim loop down to -thick
    rim = [idx(i, 0) for i in range(n + 1)] + [idx(n, j) for j in range(1, n + 1)] + \
          [idx(i, n) for i in range(n - 1, -1, -1)] + [idx(0, j) for j in range(n - 1, 0, -1)]
    b0 = len(verts)
    for k in rim:
        vx, vy, _ = verts[k]
        verts.append((vx, vy, -thick))
    m = len(rim)
    for t in range(m):
        a, b = rim[t], rim[(t + 1) % m]
        faces.append((a, b, b0 + (t + 1) % m, b0 + t))
        mi.append(1 if side_key else 0)
    faces.append(tuple(b0 + t for t in range(m - 1, -1, -1)))
    mi.append(1 if side_key else 0)
    keys = [key, side_key] if side_key else [key]
    return mesh(name, verts, faces, keys, mi, smooth=True)


def lathe(name, key, prof, n=24, smooth=True, cols=None, center=(0.5, 0.5), rfun=None, cap_top=True,
          cap_bot=True):
    """Surface of revolution about the vertical line through `center`.
    prof: [(r, z), ...] bottom to top. rfun(theta, z) -> radius factor (ribs).
    cols: per profile point linear RGB."""
    cx, cy = center
    verts, faces, vc = [], [], []
    th = np.linspace(0, 2 * math.pi, n, endpoint=False)
    for k, (r, z) in enumerate(prof):
        for t in th:
            f = rfun(t, z) if rfun else 1.0
            verts.append((cx + r * f * math.cos(t), cy + r * f * math.sin(t), z))
            if cols is not None:
                vc.append(cols[k])
    L = len(prof)
    for k in range(L - 1):
        for i in range(n):
            a = k * n + i
            b = k * n + (i + 1) % n
            faces.append((a, b, b + n, a + n))
    if cap_bot and prof[0][0] > 1e-6:
        faces.append(tuple(range(n - 1, -1, -1)))
    if cap_top and prof[-1][0] > 1e-6:
        faces.append(tuple((L - 1) * n + i for i in range(n)))
    return mesh(name, verts, faces, key, smooth=smooth, cols=vc if cols is not None else None)


def tube(name, key, pts, radii, n=12, smooth=True, cols=None, rfun=None, cap=True):
    """A tube along a polyline pts (N x 3) with radii (N), parallel-transport
    frames. rfun(theta, s) -> radius factor, s in 0..1 along the tube."""
    P = np.asarray(pts, float)
    N = len(P)
    tan = np.gradient(P, axis=0)
    tan /= np.linalg.norm(tan, axis=1, keepdims=True)
    ref = np.array([1.0, 0.0, 0.0]) if abs(tan[0][2]) > 0.9 else np.array([0.0, 0.0, 1.0])
    nrm = np.cross(tan[0], ref)
    nrm /= np.linalg.norm(nrm)
    verts, faces, vc = [], [], []
    th = np.linspace(0, 2 * math.pi, n, endpoint=False)
    for k in range(N):
        if k > 0:
            nrm = nrm - tan[k] * nrm.dot(tan[k])
            nrm /= np.linalg.norm(nrm)
        bnm = np.cross(tan[k], nrm)
        s = k / (N - 1)
        for t in th:
            f = rfun(t, s) if rfun else 1.0
            p = P[k] + radii[k] * f * (math.cos(t) * nrm + math.sin(t) * bnm)
            verts.append(tuple(p))
            if cols is not None:
                vc.append(cols[k])
    for k in range(N - 1):
        for i in range(n):
            a = k * n + i
            b = k * n + (i + 1) % n
            faces.append((a, b, b + n, a + n))
    if cap:
        faces.append(tuple(range(n - 1, -1, -1)))
        faces.append(tuple((N - 1) * n + i for i in range(n)))
    return mesh(name, verts, faces, key, smooth=smooth, cols=vc if cols is not None else None)


def blob(name, key, c, r, rng, amp=0.15, freq=2.0, subdiv=3, scale=(1, 1, 1), flat_below=None,
         colfun=None, smooth=True):
    """A noise-displaced icosphere (canopy lumps, rocks, stones)."""
    bm = bmesh.new()
    bmesh.ops.create_icosphere(bm, subdivisions=subdiv, radius=1.0)
    P = np.array([v.co[:] for v in bm.verts])
    faces = [[v.index for v in f.verts] for f in bm.faces]
    bm.free()
    w = rng.normal(size=(6, 3)) * freq
    ph = rng.uniform(0, 2 * math.pi, 6)
    disp = 1 + amp * (np.sin(P @ w.T + ph).sum(axis=1) / math.sqrt(3))
    P = P * disp[:, None] * np.asarray(scale) * r + np.asarray(c)
    if flat_below is not None:
        P[:, 2] = np.maximum(P[:, 2], flat_below)
    cols = colfun(P) if colfun else None
    return mesh(name, P, faces, key, smooth=smooth, cols=cols)


def ribbon(name, key, spine, widths, normals, cols=None, smooth=True, thick=0.0):
    """A strip along `spine` (N x 3), half-width widths (N) along the side
    vectors `normals` (N x 3). With thick > 0 it gets a back face offset."""
    P = np.asarray(spine, float)
    S = np.asarray(normals, float)
    N = len(P)
    verts, faces, vc = [], [], []
    for k in range(N):
        verts.append(tuple(P[k] - S[k] * widths[k]))
        verts.append(tuple(P[k] + S[k] * widths[k]))
        if cols is not None:
            vc += [cols[k], cols[k]]
    for k in range(N - 1):
        a = 2 * k
        faces.append((a, a + 1, a + 3, a + 2))
    return mesh(name, verts, faces, key, smooth=smooth, cols=vc if cols is not None else None, recalc=False)


def solidify(ob, t=0.01):
    m = ob.modifiers.new('solid', 'SOLIDIFY')
    m.thickness = t
    m.offset = 0.0
    return ob


def subsurf(ob, levels=1):
    m = ob.modifiers.new('sub', 'SUBSURF')
    m.levels = levels
    m.render_levels = levels
    return ob


# ---------------------------------------------------------------------------
# rendering
# ---------------------------------------------------------------------------

TIMES = []


def render_tile(a, name, objs, kind='tile', extra=None):
    t0 = time.time()
    info = C.render_sprite(a.out, name, objs, C.cell(0, 0, 0), passes=('color', 'z'),
                           samples=a.samples, kind=kind, extra=extra)
    TIMES.append((name, time.time() - t0))
    print('  %-32s %3dx%-3d %.1fs' % (name, info['w'], info['h'], time.time() - t0))
    C.remove(objs)
    return info


def fit_pool(objs, anchor, centre_xy, radius, shadow_z=0.0, margin=3):
    """fit() of the objects and their shadow, grown to hold a ground disc of
    `radius` around centre_xy (a light pool for the glow pass)."""
    w, h, ax, ay = C.fit(objs, anchor, margin, shadow_z)
    x0, y0, x1, y1 = -ax, -ay, w - ax, h - ay
    A = V(anchor)
    for k in range(48):
        t = 2 * math.pi * k / 48
        p = V((centre_xy[0] + radius * math.cos(t), centre_xy[1] + radius * math.sin(t), shadow_z))
        sx, sy = C.to_screen(p, A)
        x0, x1 = min(x0, math.floor(sx)), max(x1, math.ceil(sx))
        y0, y1 = min(y0, math.floor(sy)), max(y1, math.ceil(sy))
    return (x1 - x0, y1 - y0, -x0, -y0)


def render_prop(a, name, objs, height_m, extra=None, glow=None, kind='prop', lights=()):
    """glow: (centre_xy, radius) of the light pool for a 'glow' pass."""
    t0 = time.time()
    ex = dict(height_m=round(height_m, 2))
    if extra:
        ex.update(extra)
    passes = ('color', 'z', 'shadow')
    size = None
    if glow is not None:
        passes = passes + ('glow',)
        size = fit_pool(objs, C.cell(0, 0, 0), glow[0], glow[1])
    info = C.render_sprite(a.out, name, objs, C.cell(0, 0, 0), passes=passes, samples=a.samples,
                           shadow_z=0.0, kind=kind, extra=ex, size=size)
    if glow is not None:
        taper_glow(a.out, name, info, glow[0], glow[1])
    TIMES.append((name, time.time() - t0))
    print('  %-32s %3dx%-3d %.1fs' % (name, info['w'], info['h'], time.time() - t0))
    C.remove(list(objs) + list(lights))
    return info


def emit_attr_mat(key, strength=5.0, attr='col'):
    """Pure emission from the mesh's colour attribute (flames, glowing glass)."""
    def build(nt, neutral):
        nb = NB(nt)
        e = nb.node('ShaderNodeEmission')
        nt.links.new(nb.attr(attr).outputs['Color'], e.inputs['Color'])
        e.inputs['Strength'].default_value = strength
        return e.outputs['Emission']
    return C.mat(key, build=build)


def taper_glow(out, name, info, centre_xy, radius, inner=0.55):
    """Fade the glow pass to black towards `radius` metres from centre_xy on
    the ground plane, so the light pool never ends at the image's edge."""
    path = os.path.join(os.path.abspath(out), info['files']['gl'])
    im = bpy.data.images.load(path, check_existing=False)
    im.colorspace_settings.name = 'Non-Color'
    w, h = im.size
    px = np.empty(w * h * 4, np.float32)
    im.pixels.foreach_get(px)
    bpy.data.images.remove(im)
    rgb = px.reshape(h, w, 4)[::-1, :, :3]
    # pixel centre -> ground offset from the anchor (z = 0): solve the projection
    xs = np.arange(w) + 0.5 - info['ax']
    ys = np.arange(h) + 0.5 - info['ay']
    SX, SY = np.meshgrid(xs, ys)
    det = C.PX_X[0] * C.PX_Y[1] - C.PX_Y[0] * C.PX_X[1]
    dx = (C.PX_Y[1] * SX - C.PX_Y[0] * SY) / det
    dy = (-C.PX_X[1] * SX + C.PX_X[0] * SY) / det
    ax, ay = info['anchor'][0], info['anchor'][1]
    r = np.hypot(ax + dx - centre_xy[0], ay + dy - centre_xy[1])
    t = np.clip((radius - r) / (radius * (1 - inner)), 0, 1)
    win = t * t * (3 - 2 * t)
    out8 = np.round(np.clip(rgb * win[..., None], 0, 1) * 255).astype(np.uint8)
    C.write_png(path, out8)


def catmull(ctrl, n):
    """Catmull-Rom through control points, n samples."""
    c = np.asarray(ctrl, float)
    P = np.vstack([2 * c[0] - c[1], c, 2 * c[-1] - c[-2]])
    out = []
    segs = len(c) - 1
    for i in range(n):
        t = i / (n - 1) * segs
        s = min(int(t), segs - 1)
        u = t - s
        p0, p1, p2, p3 = P[s], P[s + 1], P[s + 2], P[s + 3]
        out.append(0.5 * ((2 * p1) + (-p0 + p2) * u + (2 * p0 - 5 * p1 + 4 * p2 - p3) * u * u
                          + (-p0 + 3 * p1 - 3 * p2 + p3) * u ** 3))
    return np.array(out)


def stripe_mat(key, c1, c2, axis='Z', freq=10.0, rough=0.5, spec=0.35, metal1=0.0, offset=0.0):
    """Two sRGB colours in bands along an object axis (freq bands per metre)."""
    l1 = tuple(T.rgb(*c1)) + (1.0,)
    l2 = tuple(T.rgb(*c2)) + (1.0,)

    def build(nt, neutral):
        nb = NB(nt)
        x, y, z = nb.objco()
        comp = {'X': x, 'Y': y, 'Z': z}[axis]
        s = nb.math('SINE', nb.math('MULTIPLY', nb.math('ADD', comp, offset), 2 * math.pi * freq / 2))
        f = nb.math('GREATER_THAN', s, 0.0)
        col = nb.mix(f, l2, l1)
        base = (0.8, 0.8, 0.8, 1.0) if neutral else col
        return nb.principled(base, rough, spec, None, None, 0.0, metal1)
    return C.mat(key, build=build)


def veil_mat(key, color, strength=1.5, alpha=0.5):
    """Semi-transparent emission (a glow in a doorway, a motion streak); the
    'col' attribute's red channel scales the opacity."""
    def build(nt, neutral):
        nb = NB(nt)
        e = nb.node('ShaderNodeEmission')
        e.inputs['Color'].default_value = tuple(T.rgb(*color)) + (1.0,)
        e.inputs['Strength'].default_value = strength
        tr = nb.node('ShaderNodeBsdfTransparent')
        mx = nb.node('ShaderNodeMixShader')
        a = nb.attr('col')
        sep = nb.node('ShaderNodeSeparateRGB')
        nt.links.new(a.outputs['Color'], sep.inputs[0])
        fac = nb.math('MULTIPLY', sep.outputs['R'], alpha)
        nt.links.new(fac, mx.inputs[0])
        nt.links.new(tr.outputs[0], mx.inputs[1])
        nt.links.new(e.outputs[0], mx.inputs[2])
        return mx.outputs[0]
    return C.mat(key, build=build)


def rotate_z(ob, deg, center=(0.5, 0.5)):
    """Rotate a mesh's vertices about the vertical through `center`."""
    t = math.radians(deg)
    c, s = math.cos(t), math.sin(t)
    for v in ob.data.vertices:
        x, y = v.co.x - center[0], v.co.y - center[1]
        v.co.x, v.co.y = center[0] + c * x - s * y, center[1] + s * x + c * y
    ob.data.update()
    return ob


def transform(ob, M):
    """Apply a 4x4 mathutils Matrix to the mesh's vertices (keeps object
    coordinates = world coordinates, which the tile materials rely on)."""
    ob.data.transform(M)
    ob.data.update()
    return ob


def sprite(a, name, objs, kind='dyn', shadow_z=0.0, glow=None, extra=None, lights=(), passes=None, remove=True):
    """Render a moving thing / multi-pass sprite with timing."""
    t0 = time.time()
    ps = passes or ('color', 'z', 'shadow')
    size = None
    if glow is not None:
        ps = tuple(ps) + ('glow',)
        size = fit_pool(objs, C.cell(0, 0, 0), glow[0], glow[1], shadow_z)
    info = C.render_sprite(a.out, name, objs, C.cell(0, 0, 0), passes=ps, samples=a.samples,
                           shadow_z=shadow_z if 'shadow' in ps or glow else None, kind=kind, extra=extra,
                           size=size)
    if glow is not None:
        taper_glow(a.out, name, info, glow[0], glow[1])
    TIMES.append((name, time.time() - t0))
    print('  %-32s %3dx%-3d %.1fs' % (name, info['w'], info['h'], time.time() - t0))
    if remove:
        C.remove(list(objs) + list(lights))
    return info
