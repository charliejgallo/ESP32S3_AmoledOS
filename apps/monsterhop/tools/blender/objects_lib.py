"""Monster Hop - helpers shared by objects.py and ui.py (not by the other scripts).

Geometry (pillows, rings, keys, bats), the cartoon gold and a few other shaders,
and the 2D touches applied to a sprite after Blender renders it: a soft halo
behind it and four-ray glints on top (the "sparkle" of keys and coins). The
2D work reads back the PNGs mh_common writes (8-bit, filter 0) with a tiny
reader, because Blender's Python has no PIL.
"""
import math
import os
import random
import struct
import zlib

import bpy
import bmesh
import numpy as np
from mathutils import Vector as V

import mh_common as C


# ---------------------------------------------------------------------------
# colour helpers
# ---------------------------------------------------------------------------

def lin(c):
    """sRGB 0..1 (or 0..255 ints) -> linear, for shader inputs."""
    c = [x / 255.0 if isinstance(x, int) and x > 1 else x for x in c]
    return tuple(x / 12.92 if x <= 0.04045 else ((x + 0.055) / 1.055) ** 2.4 for x in c)


def hexlin(h):
    h = h.lstrip('#')
    return lin(tuple(int(h[i:i + 2], 16) for i in (0, 2, 4)))


# ---------------------------------------------------------------------------
# PNG in / out (only the files mh_common.write_png makes: 8 bit, filter 0)
# ---------------------------------------------------------------------------

def read_png(path):
    with open(path, 'rb') as f:
        data = f.read()
    assert data[:8] == b'\x89PNG\r\n\x1a\n', path
    pos, idat, w = 8, b'', None
    while pos < len(data):
        ln = struct.unpack('>I', data[pos:pos + 4])[0]
        t = data[pos + 4:pos + 8]
        d = data[pos + 8:pos + 8 + ln]
        pos += 12 + ln
        if t == b'IHDR':
            w, h, bd, ct = struct.unpack('>IIBB', d[:10])
            assert bd == 8
        elif t == b'IDAT':
            idat += d
    ch = {0: 1, 2: 3, 4: 2, 6: 4}[ct]
    raw = np.frombuffer(zlib.decompress(idat), np.uint8).reshape(h, 1 + w * ch)
    assert (raw[:, 0] == 0).all(), 'unsupported PNG filter in ' + path
    px = raw[:, 1:]
    return (px.reshape(h, w, ch) if ch > 1 else px.reshape(h, w)).copy()


def write_png(path, arr):
    C.write_png(path, arr)


# ---------------------------------------------------------------------------
# 2D effects on a rendered sprite
# ---------------------------------------------------------------------------

def blur(a, sigma):
    r = max(1, int(math.ceil(sigma * 3)))
    k = np.exp(-0.5 * (np.arange(-r, r + 1) / sigma) ** 2)
    k /= k.sum()
    h, w = a.shape
    p = np.pad(a, ((0, 0), (r, r)))
    o = np.zeros_like(a)
    for i, kv in enumerate(k):
        o += kv * p[:, i:i + w]
    p = np.pad(o, ((r, r), (0, 0)))
    o2 = np.zeros_like(a)
    for i, kv in enumerate(k):
        o2 += kv * p[i:i + h, :]
    return o2


def _over(trgb, ta, brgb, ba):
    oa = ta + ba * (1 - ta)
    orgb = (trgb * ta[..., None] + brgb * (ba * (1 - ta))[..., None]) / np.maximum(oa, 1e-6)[..., None]
    return orgb, oa


class Sprite2D:
    """The colour and depth PNGs of one rendered sprite, as floats."""

    def __init__(self, out, name):
        self.out, self.name = out, name
        im = read_png(os.path.join(out, name + '.png')).astype(np.float32) / 255.0
        self.rgb, self.a = im[..., :3], im[..., 3]
        zp = os.path.join(out, name + '_z.png')
        self.z = read_png(zp).astype(np.int32) if os.path.exists(zp) else None
        self.info = C._STATE['meta'][name]

    def px(self, p):
        """Pixel position (float, pixel-centre coordinates) of world point p."""
        A = V(self.info['anchor'])
        sx, sy = C.to_screen(V(p), A)
        return self.info['ax'] + sx, self.info['ay'] + sy

    def zcode(self, p):
        A = V(self.info['anchor'])
        return int(round(128 + (C.depth(V(p)) - C.depth(A)) / C.DEPTH_UNIT))

    def halo(self, color, sigma=3.0, gain=1.6, amax=0.55, z=None, src=None):
        """A soft glow behind the sprite (straight-alpha 'over', so it also
        reads on dark ground). z: the depth code of the new halo pixels."""
        base = self.a if src is None else src
        g = np.clip(blur(base, sigma) * gain, 0, amax)
        g = g * (1 - self.a)          # only around, the sprite stays itself
        rgb, a = _over(self.rgb, self.a, np.broadcast_to(np.array(color, np.float32), self.rgb.shape), g)
        self._newz(a, z)
        self.rgb, self.a = rgb, a

    def glint(self, x, y, length, strength=1.0, color=(1.0, 0.98, 0.85), z=None, diag=0.35, thick=0.6):
        """A four-ray star glint centred on pixel (x, y) (float)."""
        h, w = self.a.shape
        yy, xx = np.mgrid[0:h, 0:w].astype(np.float32)
        acc = np.zeros((h, w), np.float32)
        n = 4
        for sy in range(n):
            for sx in range(n):
                dx = xx + (sx + 0.5) / n - x - 0.5
                dy = yy + (sy + 0.5) / n - y - 0.5
                rh = np.clip(1 - np.abs(dx) / length, 0, 1) ** 1.6 * np.exp(-0.5 * (dy / thick) ** 2)
                rv = np.clip(1 - np.abs(dy) / length, 0, 1) ** 1.6 * np.exp(-0.5 * (dx / thick) ** 2)
                u, v = (dx + dy) * 0.7071, (dx - dy) * 0.7071
                L2 = length * 0.45
                d1 = np.clip(1 - np.abs(u) / L2, 0, 1) ** 2 * np.exp(-0.5 * (v / (thick * 0.9)) ** 2)
                d2 = np.clip(1 - np.abs(v) / L2, 0, 1) ** 2 * np.exp(-0.5 * (u / (thick * 0.9)) ** 2)
                core = np.exp(-0.5 * (dx * dx + dy * dy) / (0.35 * length * 0.5 + 0.6) ** 2)
                acc += np.maximum.reduce([rh, rv, d1 * diag, d2 * diag]) + core * 0.8
        g = np.clip(acc / (n * n) * strength, 0, 1)
        rgb, a = _over(np.broadcast_to(np.array(color, np.float32), self.rgb.shape), g, self.rgb, self.a)
        self._newz(a, z)
        self.rgb, self.a = rgb, a

    def fade(self, k):
        self.a = self.a * k

    def _newz(self, a_new, z):
        if self.z is None:
            return
        new = (a_new > 0.5 / 255) & (self.z >= 255)
        if z is None:
            have = self.z[self.z < 255]
            z = int(np.median(have)) if have.size else 128
        self.z = np.where(new, z, self.z)

    def hot(self, k=1, min_sep=6, min_alpha=0.9):
        """The k brightest pixels of the sprite (highlights), well apart."""
        lum = (self.rgb * np.array([0.3, 0.55, 0.15])).sum(-1) * (self.a >= min_alpha)
        pts = []
        lum = lum.copy()
        for _ in range(k):
            i = int(np.argmax(lum))
            y, x = divmod(i, lum.shape[1])
            if lum[y, x] <= 0:
                break
            pts.append((float(x), float(y)))
            yy, xx = np.mgrid[0:lum.shape[0], 0:lum.shape[1]]
            lum[(yy - y) ** 2 + (xx - x) ** 2 < min_sep ** 2] = 0
        return pts

    def save(self):
        a8 = np.round(np.clip(self.a, 0, 1) * 255).astype(np.uint8)
        rgb8 = np.round(np.clip(self.rgb, 0, 1) * 255).astype(np.uint8)
        rgb8[a8 == 0] = 0
        out = np.dstack([rgb8, a8])
        write_png(os.path.join(self.out, self.name + '.png'), out)
        if self.z is not None:
            z = np.where(a8 > 0, self.z, 255).astype(np.uint8)
            write_png(os.path.join(self.out, self.name + '_z.png'), z)


def shape_glow(path, info, rmax, gain=1.0, power=2.0):
    """Window a rendered _gl.png so the pool fades to black at rmax metres
    from the anchor (a point light never quite ends, the sprite does), and
    scale it (in linear light). The watch adds the file as it is."""
    gl = read_png(path).astype(np.float32) / 255.0
    h, w = gl.shape[:2]
    yy, xx = np.mgrid[0:h, 0:w].astype(np.float32)
    dx = xx + 0.5 - info['ax']
    dy = yy + 0.5 - info['ay']
    det = 60.0 * -42.0 - 20.0 * 14.0
    X = (dx * -42.0 - 20.0 * dy) / det
    Y = (60.0 * dy - 14.0 * dx) / det
    r = np.sqrt(X * X + Y * Y)
    win = np.clip(1 - (r / rmax) ** 2, 0, 1) ** power
    lin_ = np.where(gl <= 0.04045, gl / 12.92, ((gl + 0.055) / 1.055) ** 2.4)
    lin_ = lin_ * win[..., None] * gain
    out = C._srgb(lin_)
    write_png(path, np.round(np.clip(out, 0, 1) * 255).astype(np.uint8))


# ---------------------------------------------------------------------------
# sizes
# ---------------------------------------------------------------------------

def size_union(*sizes):
    x0 = min(-s[2] for s in sizes)
    y0 = min(-s[3] for s in sizes)
    x1 = max(s[0] - s[2] for s in sizes)
    y1 = max(s[1] - s[3] for s in sizes)
    return (x1 - x0, y1 - y0, -x0, -y0)


def size_ground_disc(anchor, r, z=0.0, margin=2):
    """The screen box of a disc of radius r on the plane z around anchor."""
    A = V(anchor)
    xs, ys = [], []
    for i in range(32):
        t = 2 * math.pi * i / 32
        sx, sy = C.to_screen(V((A.x + r * math.cos(t), A.y + r * math.sin(t), z)), A)
        xs.append(sx)
        ys.append(sy)
    x0, x1 = math.floor(min(xs)) - margin, math.ceil(max(xs)) + margin
    y0, y1 = math.floor(min(ys)) - margin, math.ceil(max(ys)) + margin
    return (x1 - x0, y1 - y0, -x0, -y0)


def grow(size, m):
    w, h, ax, ay = size
    return (w + 2 * m, h + 2 * m, ax + m, ay + m)


def update():
    bpy.context.view_layer.update()


# ---------------------------------------------------------------------------
# geometry
# ---------------------------------------------------------------------------

def smooth(o, on=True):
    for p in o.data.polygons:
        p.use_smooth = on
    return o


def _last(name, key, parent=None, sm=True):
    o = bpy.context.object
    o.name = name
    if sm:
        smooth(o)
    if key:
        C.assign(o, key)
    if parent is not None:
        o.parent = parent
    return o


def sphere(name, r, key, loc=(0, 0, 0), scale=(1, 1, 1), parent=None, seg=32, rings=16):
    bpy.ops.mesh.primitive_uv_sphere_add(radius=r, segments=seg, ring_count=rings, location=loc)
    o = _last(name, key, parent)
    o.scale = scale
    return o


def cylinder(name, r, depth, key, loc=(0, 0, 0), rot=(0, 0, 0), parent=None, verts=32, sm=True):
    bpy.ops.mesh.primitive_cylinder_add(radius=r, depth=depth, vertices=verts, location=loc, rotation=rot)
    o = _last(name, key, parent, sm)
    if sm:
        # flat caps, round sides
        o.data.use_auto_smooth = True
        o.data.auto_smooth_angle = math.radians(50)
    return o


def cone(name, r1, r2, depth, key, loc=(0, 0, 0), rot=(0, 0, 0), parent=None, verts=32):
    bpy.ops.mesh.primitive_cone_add(radius1=r1, radius2=r2, depth=depth, vertices=verts, location=loc, rotation=rot)
    o = _last(name, key, parent)
    o.data.use_auto_smooth = True
    o.data.auto_smooth_angle = math.radians(50)
    return o


def torus(name, R, r, key, loc=(0, 0, 0), rot=(0, 0, 0), parent=None, seg=(48, 16)):
    bpy.ops.mesh.primitive_torus_add(major_radius=R, minor_radius=r, major_segments=seg[0],
                                     minor_segments=seg[1], location=loc, rotation=rot)
    return _last(name, key, parent)


def rbox(name, x0, y0, z0, x1, y1, z1, key, bevel=0.01, seg=3, parent=None):
    o = C.box(name, x0, y0, z0, x1, y1, z1, key, bevel=bevel, segments=seg)
    if bevel > 0:
        o.modifiers['bevel'].limit_method = 'NONE'
        smooth(o)
        o.data.use_auto_smooth = True
        o.data.auto_smooth_angle = math.radians(40)
    if parent is not None:
        o.parent = parent
    return o


def centered(o):
    """Move the object's origin to the centre of its vertices (C.box builds
    boxes in world coordinates around the world origin), so rotating it
    turns it in place."""
    vs = o.data.vertices
    c = sum((v.co for v in vs), V((0, 0, 0))) / max(1, len(vs))
    for v in vs:
        v.co -= c
    o.location = o.location + c
    return o


def empty(name, loc=(0, 0, 0), parent=None):
    e = bpy.data.objects.new(name, None)
    C.link(e)
    e.location = loc
    if parent is not None:
        e.parent = parent
    return e


def mesh_from(name, verts, faces, key=None, parent=None, sm=True):
    me = bpy.data.meshes.new(name)
    me.from_pydata([tuple(v) for v in verts], [], [tuple(f) for f in faces])
    bm = bmesh.new()
    bm.from_mesh(me)
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    bm.to_mesh(me)
    bm.free()
    me.validate()
    o = bpy.data.objects.new(name, me)
    C.link(o)
    if sm:
        smooth(o)
    if key:
        C.assign(o, key)
    if parent is not None:
        o.parent = parent
    return o


def pillow(name, outline, H, key, rings=10, q=0.8, back=True, parent=None, y0=0.0, Hb=None):
    """A puffy slab from a closed 2D outline [(x, z), ...] (star-shaped about its
    centroid): flat against y = y0, bulging H towards -y (front) and Hb towards +y
    (back; 0 or None-with-back=False gives a flat back)."""
    n = len(outline)
    cx = sum(p[0] for p in outline) / n
    cz = sum(p[1] for p in outline) / n
    verts, faces = [], []
    Hb = H if Hb is None else Hb

    def side(sign, hh):
        idx = []
        for k in range(1, rings + 1):
            th = (math.pi / 2) * k / rings
            s = math.cos(th)
            hgt = hh * (math.sin(th) ** q) if hh > 0 else 0.0
            if k == rings:
                idx.append([len(verts)])
                verts.append((cx, y0 + sign * hgt, cz))
                break
            row = []
            for (x, z) in outline:
                row.append(len(verts))
                verts.append((cx + (x - cx) * s, y0 + sign * hgt, cz + (z - cz) * s))
            idx.append(row)
        return idx

    rim = list(range(n))
    for (x, z) in outline:
        verts.append((x, y0, z))
    for sign, hh in ((-1, H), (1, Hb if back else 0.0)):
        if hh <= 0:
            faces.append(list(reversed(rim)) if sign > 0 else rim)
            continue
        rows = [rim] + side(sign, hh)
        for a_, b_ in zip(rows[:-1], rows[1:]):
            if len(b_) == 1:
                c = b_[0]
                for i in range(n):
                    faces.append((a_[i], a_[(i + 1) % n], c))
            else:
                for i in range(n):
                    faces.append((a_[i], a_[(i + 1) % n], b_[(i + 1) % n], b_[i]))
    return mesh_from(name, verts, faces, key, parent)


def star_outline(R, r, n=5, pts_per_edge=3, rot=90.0, round_tip=0.0):
    out = []
    corners = []
    for i in range(2 * n):
        a = math.radians(rot) + i * math.pi / n
        rr = R if i % 2 == 0 else r
        corners.append((rr * math.cos(a), rr * math.sin(a)))
    for i in range(2 * n):
        p0, p1 = corners[i], corners[(i + 1) % (2 * n)]
        for k in range(pts_per_edge):
            t = k / pts_per_edge
            out.append((p0[0] + (p1[0] - p0[0]) * t, p0[1] + (p1[1] - p0[1]) * t))
    return out


def heart_outline(width, npts=96):
    s = width / 32.0
    pts = []
    for i in range(npts):
        t = 2 * math.pi * i / npts
        x = 16 * math.sin(t) ** 3
        y = 13 * math.cos(t) - 5 * math.cos(2 * t) - 2 * math.cos(3 * t) - math.cos(4 * t)
        pts.append((x * s, (y + 2.5) * s))
    return pts


def circle_outline(r, n=48):
    return [(r * math.cos(2 * math.pi * i / n), r * math.sin(2 * math.pi * i / n)) for i in range(n)]


def curve_to_mesh(ob, name, key=None):
    dg = bpy.context.evaluated_depsgraph_get()
    me = bpy.data.meshes.new_from_object(ob.evaluated_get(dg))
    o = bpy.data.objects.new(name, me)
    C.link(o)
    o.matrix_world = ob.matrix_world.copy()
    bpy.data.objects.remove(ob, do_unlink=True)
    if key:
        C.assign(o, key)
    return o


def billboard(name, shape2d, center, key, depth_off=0.0, parent=None):
    """A flat polygon in the screen plane (x right, y up, metres), at world
    `center`, pushed depth_off metres towards the camera."""
    c = V(center) - C.F * depth_off
    verts = [tuple(c + C.R * x + C.UP * y) for (x, y) in shape2d]
    verts.append(tuple(c))
    n = len(shape2d)
    faces = [(i, (i + 1) % n, n) for i in range(n)]
    me = bpy.data.meshes.new(name)
    me.from_pydata(verts, [], faces)
    me.validate()
    o = bpy.data.objects.new(name, me)
    C.link(o)
    if key:
        C.assign(o, key)
    if parent is not None:
        o.parent = parent
    return o


# ---------------------------------------------------------------------------
# shaders (mh_common `build` functions)
# ---------------------------------------------------------------------------

def _ramp(nt, stops):
    r = nt.nodes.new('ShaderNodeValToRGB')
    els = r.color_ramp.elements
    els[0].position, els[0].color = stops[0][0], tuple(stops[0][1]) + (1,)
    els[1].position, els[1].color = stops[-1][0], tuple(stops[-1][1]) + (1,)
    for pos, col in stops[1:-1]:
        e = els.new(pos)
        e.color = tuple(col) + (1,)
    return r


GOLD_ENV = [(0.00, hexlin('#5a2604')), (0.16, hexlin('#d07a10')), (0.36, hexlin('#ffbe30')),
            (0.47, hexlin('#7a3a04')), (0.55, hexlin('#ffd24a')), (0.78, hexlin('#fff0a0')),
            (1.00, hexlin('#ffffff'))]


COIN_ENV = [(0.00, hexlin('#6a4204')), (0.16, hexlin('#e0a014')), (0.36, hexlin('#ffd040')),
            (0.47, hexlin('#8a5a06')), (0.55, hexlin('#ffe060')), (0.78, hexlin('#fff4b0')),
            (1.00, hexlin('#ffffff'))]


def gold_build(emit=0.45, base='#ffb81c', rough=0.20, metal=0.7, rim=0.7, env=None):
    """Cartoon gold: a fake environment reflection (a sky/horizon/ground ramp on
    the reflection vector, as emission) over a lit metallic base, so it gleams
    without an HDRI and turns its gleams as it spins; the edges that turn away
    from the camera darken (`rim`), which keeps the shapes readable at 20 px."""
    env = env or GOLD_ENV
    basel = hexlin(base)

    def b(nt, neutral):
        N, Lk = nt.nodes, nt.links
        p = N.new('ShaderNodeBsdfPrincipled')
        p.inputs['Base Color'].default_value = ((0.8, 0.8, 0.8) if neutral else basel) + (1,)
        p.inputs['Metallic'].default_value = metal
        p.inputs['Roughness'].default_value = rough
        p.inputs['Specular'].default_value = 0.7
        tc = N.new('ShaderNodeTexCoord')
        sep = N.new('ShaderNodeSeparateXYZ')
        Lk.new(tc.outputs['Reflection'], sep.inputs[0])
        mr = N.new('ShaderNodeMapRange')
        mr.inputs['From Min'].default_value = -1.0
        mr.inputs['From Max'].default_value = 1.0
        Lk.new(sep.outputs['Z'], mr.inputs['Value'])
        rp = _ramp(nt, env if not neutral else [(0, (0.3, 0.3, 0.3)), (1, (0.8, 0.8, 0.8))])
        Lk.new(mr.outputs['Result'], rp.inputs['Fac'])
        lw = N.new('ShaderNodeLayerWeight')
        lw.inputs['Blend'].default_value = 0.5
        fr = _ramp(nt, [(0.0, (1, 1, 1)), (0.55, (1, 1, 1)), (0.9, (0.45, 0.3, 0.2)), (1.0, (0.2, 0.1, 0.05))])
        Lk.new(lw.outputs['Facing'], fr.inputs['Fac'])
        mix = N.new('ShaderNodeMixRGB')
        mix.blend_type = 'MULTIPLY'
        mix.inputs['Fac'].default_value = rim
        Lk.new(rp.outputs['Color'], mix.inputs['Color1'])
        Lk.new(fr.outputs['Color'], mix.inputs['Color2'])
        em = N.new('ShaderNodeEmission')
        Lk.new(mix.outputs[0], em.inputs['Color'])
        em.inputs['Strength'].default_value = emit
        add = N.new('ShaderNodeAddShader')
        Lk.new(p.outputs['BSDF'], add.inputs[0])
        Lk.new(em.outputs['Emission'], add.inputs[1])
        return add.outputs['Shader']
    return b


def gloss_build(base, emit_col=None, emit=0.0, rough=0.18, coat=1.0, spec=0.6, sss=0.0):
    basel = hexlin(base)
    emc = hexlin(emit_col) if emit_col else basel

    def b(nt, neutral):
        N = nt.nodes
        p = N.new('ShaderNodeBsdfPrincipled')
        p.inputs['Base Color'].default_value = ((0.8, 0.8, 0.8) if neutral else basel) + (1,)
        p.inputs['Roughness'].default_value = rough
        p.inputs['Specular'].default_value = spec
        p.inputs['Clearcoat'].default_value = coat
        p.inputs['Clearcoat Roughness'].default_value = 0.04
        if emit > 0 and not neutral:
            p.inputs['Emission'].default_value = emc + (1,)
            p.inputs['Emission Strength'].default_value = emit
        return p.outputs['BSDF']
    return b


def emit_build(col, strength=1.0):
    c = hexlin(col) if isinstance(col, str) else tuple(col)

    def b(nt, neutral):
        e = nt.nodes.new('ShaderNodeEmission')
        e.inputs['Color'].default_value = c + (1,)
        e.inputs['Strength'].default_value = strength
        return e.outputs['Emission']
    return b


def toon_build(base, shade, emit=0.35, rough=0.8):
    """Soft cartoon matter (smoke): lit diffuse + a flat emission floor so the
    shadow side stays a tinted colour and never goes grey."""
    bl, sl = hexlin(base), hexlin(shade)

    def b(nt, neutral):
        N, Lk = nt.nodes, nt.links
        p = N.new('ShaderNodeBsdfPrincipled')
        p.inputs['Base Color'].default_value = bl + (1,)
        p.inputs['Roughness'].default_value = rough
        p.inputs['Specular'].default_value = 0.2
        em = N.new('ShaderNodeEmission')
        em.inputs['Color'].default_value = sl + (1,)
        em.inputs['Strength'].default_value = emit
        add = N.new('ShaderNodeAddShader')
        Lk.new(p.outputs['BSDF'], add.inputs[0])
        Lk.new(em.outputs['Emission'], add.inputs[1])
        return add.outputs['Shader']
    return b


# ---------------------------------------------------------------------------
# the key (objects.py renders it; ui.py puts it in the logo)
# ---------------------------------------------------------------------------

KEY_GLINT_PTS = {
    # key-local points where the light catches (before build_key's scale)
    'bow_f': (-0.085, -0.05, 0.285),     # upper-left of the bow, front side
    'bow_b': (0.085, 0.05, 0.285),       # the same spot on the back side
    'ball': (0.0, 0.0, 0.40),            # the knob on top
    'bit': (0.15, 0.0, -0.17),           # the corner of the bit
}


def build_key(root, key='gold', s=1.0):
    """A chunky skeleton key, vertical, bow on top, its flat side facing -Y,
    centred on root's origin. Returns its mesh objects. (The pickup stretches
    the root 1.25x in Z so the ring reads round under the 44 deg camera.)"""
    P = root
    o = []
    o.append(torus('key_bow', 0.118 * s, 0.043 * s, key, (0, 0, 0.20 * s), (math.pi / 2, 0, 0), P, (48, 18)))
    o.append(sphere('key_knob', 0.040 * s, key, (0, 0, (0.20 + 0.118 + 0.05) * s), parent=P))
    o.append(cylinder('key_collar', 0.056 * s, 0.05 * s, key, (0, 0, 0.055 * s), parent=P))
    o.append(torus('key_collar2', 0.050 * s, 0.018 * s, key, (0, 0, 0.012 * s), parent=P, seg=(32, 12)))
    o.append(cylinder('key_shaft', 0.034 * s, 0.27 * s, key, (0, 0, -0.105 * s), parent=P))
    t = 0.030 * s
    o.append(rbox('key_bit_a', 0.0, -t, -0.245 * s, 0.155 * s, t, -0.190 * s, key, 0.012 * s, parent=P))
    o.append(rbox('key_bit_b', 0.0, -t, -0.150 * s, 0.155 * s, t, -0.095 * s, key, 0.012 * s, parent=P))
    o.append(rbox('key_bit_c', 0.0, -t, -0.245 * s, 0.085 * s, t, -0.095 * s, key, 0.012 * s, parent=P))
    o.append(sphere('key_tip', 0.040 * s, key, (0, 0, -0.25 * s), parent=P))
    return o


# ---------------------------------------------------------------------------
# a cute bat (the logo, the castle emblem)
# ---------------------------------------------------------------------------

def bat_wing_outline(span=0.32, n_scallop=3):
    """Right wing in (x, z), from the shoulder out: an arched top edge, a
    scalloped trailing edge back to the body."""
    top = []
    for i in range(12):
        t = i / 11.0
        x = span * t
        z = 0.03 + 0.11 * math.sin(math.pi * 0.62 * t) + 0.02 * t
        top.append((x, z))
    tip = (span * 1.04, 0.07)
    bot = []
    # finger ends along the trailing edge
    ends = [(span * 0.98, 0.02)]
    for k in range(1, n_scallop + 1):
        u = 1 - k / n_scallop
        ends.append((span * (0.08 + 0.82 * u), -0.07 + 0.03 * u - 0.02 * (k == n_scallop)))
    ends[-1] = (0.0, -0.05)
    for a_, b_ in zip(ends[:-1], ends[1:]):
        for j in range(6):
            t = j / 6.0
            x = a_[0] + (b_[0] - a_[0]) * t
            z = a_[1] + (b_[1] - a_[1]) * t + 0.035 * math.sin(math.pi * t)
            bot.append((x, z))
    return top + [tip] + bot


def build_bat(root, body='bat_body', wing='bat_wing', eye_w='bat_eye', eye_d='bat_pupil', fang='bat_fang',
              s=1.0, flap=0.0):
    """A chibi bat facing -Y, centred on root: round body, big head with ears,
    wings spread (flap: radians, + = wings up)."""
    o = []
    P = root
    o.append(sphere('bat_body', 0.11 * s, body, (0, 0, -0.06 * s), (0.9, 0.85, 1.0), P, 24, 12))
    o.append(sphere('bat_head', 0.13 * s, body, (0, -0.01 * s, 0.10 * s), (1.0, 0.92, 0.9), P, 32, 16))
    for sx in (-1, 1):
        e = cone('bat_ear', 0.06 * s, 0.0, 0.14 * s, body, (sx * 0.075 * s, 0.0, 0.25 * s), (0, sx * 0.35, 0), P, 16)
        o.append(e)
        o.append(sphere('bat_eyew', 0.042 * s, eye_w, (sx * 0.050 * s, -0.105 * s, 0.12 * s), (1, 0.6, 1.15), P, 16, 8))
        o.append(sphere('bat_pupil', 0.022 * s, eye_d, (sx * 0.046 * s, -0.128 * s, 0.115 * s), (1, 0.6, 1.2), P, 12, 6))
        o.append(cone('bat_fang', 0.012 * s, 0.0, 0.03 * s, fang, (sx * 0.025 * s, -0.115 * s, 0.035 * s), (math.pi, 0, 0), P, 8))
        piv = empty('bat_shoulder', (sx * 0.07 * s, 0.01 * s, 0.0), P)
        piv.rotation_euler = (0, -sx * flap, 0)
        ol = [(x * s * sx, z * s) for x, z in bat_wing_outline(0.34)]
        if sx < 0:
            ol = list(reversed(ol))
        w = pillow('bat_wing', ol, 0.012 * s, wing, rings=3, q=1.0, parent=piv)
        o.append(w)
        o.append(piv)
    return [ob for ob in o if ob.type == 'MESH'], [ob for ob in o if ob.type != 'MESH']


# ---------------------------------------------------------------------------
# more metals, the sticker foil, lathe and rounded rectangles
# ---------------------------------------------------------------------------

SILVER_ENV = [(0.00, hexlin('#3a3e4a')), (0.16, hexlin('#8c95a6')), (0.36, hexlin('#d4dae6')),
              (0.47, hexlin('#565c6a')), (0.55, hexlin('#e6ecf6')), (0.78, hexlin('#f6f9ff')),
              (1.00, hexlin('#ffffff'))]
BRONZE_ENV = [(0.00, hexlin('#3e1a06')), (0.16, hexlin('#a4521e')), (0.36, hexlin('#dc8e4e')),
              (0.47, hexlin('#5e2c0e')), (0.55, hexlin('#eba46a')), (0.78, hexlin('#fcd2a8')),
              (1.00, hexlin('#fff0e0'))]


def holo_build(emit=0.7, sat=0.55):
    """Holographic foil: a rainbow whose hue follows the reflection vector (it
    sweeps across the card as it turns) over a bright silver base."""
    def b(nt, neutral):
        N, Lk = nt.nodes, nt.links
        p = N.new('ShaderNodeBsdfPrincipled')
        p.inputs['Base Color'].default_value = (0.8, 0.82, 0.86, 1)
        p.inputs['Metallic'].default_value = 0.8
        p.inputs['Roughness'].default_value = 0.2
        tc = N.new('ShaderNodeTexCoord')
        sep = N.new('ShaderNodeSeparateXYZ')
        Lk.new(tc.outputs['Reflection'], sep.inputs[0])
        so = N.new('ShaderNodeSeparateXYZ')
        Lk.new(tc.outputs['Object'], so.inputs[0])
        m1 = N.new('ShaderNodeMath')
        m1.operation = 'MULTIPLY_ADD'
        m1.inputs[1].default_value = 1.4
        Lk.new(sep.outputs['Z'], m1.inputs[0])
        Lk.new(sep.outputs['X'], m1.inputs[2])
        m2 = N.new('ShaderNodeMath')
        m2.operation = 'MULTIPLY_ADD'
        m2.inputs[1].default_value = 2.5
        Lk.new(so.outputs['Z'], m2.inputs[0])
        Lk.new(m1.outputs[0], m2.inputs[2])
        fr = N.new('ShaderNodeMath')
        fr.operation = 'FRACT'
        Lk.new(m2.outputs[0], fr.inputs[0])
        hsv = N.new('ShaderNodeCombineHSV')
        Lk.new(fr.outputs[0], hsv.inputs['H'])
        hsv.inputs['S'].default_value = sat
        hsv.inputs['V'].default_value = 1.0
        em = N.new('ShaderNodeEmission')
        Lk.new(hsv.outputs[0], em.inputs['Color'])
        em.inputs['Strength'].default_value = emit
        add = N.new('ShaderNodeAddShader')
        Lk.new(p.outputs['BSDF'], add.inputs[0])
        Lk.new(em.outputs['Emission'], add.inputs[1])
        return add.outputs['Shader']
    return b


def lathe(name, profile, key, seg=40, parent=None, loc=(0, 0, 0)):
    """Revolve a profile [(r, z), ...] (bottom to top) about Z. r = 0 ends
    close the shape."""
    verts, faces = [], []
    rows = []
    for (r, z) in profile:
        if r <= 1e-6:
            rows.append([len(verts)])
            verts.append((0, 0, z))
        else:
            row = []
            for i in range(seg):
                t = 2 * math.pi * i / seg
                row.append(len(verts))
                verts.append((r * math.cos(t), r * math.sin(t), z))
            rows.append(row)
    for a_, b_ in zip(rows[:-1], rows[1:]):
        if len(a_) == 1 and len(b_) == 1:
            continue
        if len(a_) == 1:
            for i in range(seg):
                faces.append((a_[0], b_[i], b_[(i + 1) % seg]))
        elif len(b_) == 1:
            for i in range(seg):
                faces.append((a_[i], a_[(i + 1) % seg], b_[0]))
        else:
            for i in range(seg):
                faces.append((a_[i], a_[(i + 1) % seg], b_[(i + 1) % seg], b_[i]))
    me = bpy.data.meshes.new(name)
    me.from_pydata(verts, [], faces)
    me.validate()
    o = bpy.data.objects.new(name, me)
    C.link(o)
    smooth(o)
    if key:
        C.assign(o, key)
    if parent is not None:
        o.parent = parent
    o.location = loc
    return o


def rrect_outline(w, h, r, n=6):
    pts = []
    for cx, cy, a0 in ((w / 2 - r, h / 2 - r, 0), (-w / 2 + r, h / 2 - r, 90),
                       (-w / 2 + r, -h / 2 + r, 180), (w / 2 - r, -h / 2 + r, 270)):
        for i in range(n + 1):
            t = math.radians(a0 + 90 * i / n)
            pts.append((cx + r * math.cos(t), cy + r * math.sin(t)))
    return pts


# ---------------------------------------------------------------------------
# pictures with their own camera (UI art, icons): not the game projection
# ---------------------------------------------------------------------------

def camera(target, direction, ortho=None, lens=None, up_hint=V((0, 0, 1)), dist=40.0):
    from mathutils import Matrix
    cam = C._STATE['cam']
    F = V(direction).normalized()
    R = F.cross(up_hint).normalized()
    U = R.cross(F)
    M = Matrix((R, U, -F)).transposed().to_4x4()
    cam.matrix_world = Matrix.Translation(V(target) - F * dist) @ M
    cam.data.clip_start = 0.1
    cam.data.clip_end = dist * 3
    if ortho is not None:
        cam.data.type = 'ORTHO'
        cam.data.ortho_scale = ortho
    else:
        cam.data.type = 'PERSP'
        cam.data.lens = lens or 50.0
    return cam


def frame(w, h, fit='HORIZONTAL'):
    sc = C._STATE['scene']
    sc.render.resolution_x, sc.render.resolution_y = int(w), int(h)
    C._STATE['cam'].data.sensor_fit = fit


def picture(out, name, w, h, objs, samples, extra=None, filt=1.2, bounces=6, fit='HORIZONTAL', kind='ui'):
    """Render objs with the current camera into <out>/<name>.png (RGBA)."""
    import time
    t = time.time()
    frame(w, h, fit)
    os.makedirs(os.path.join(out, '_tmp'), exist_ok=True)
    C._set_vis(objs, [])
    C._use_pass(objs, 'color')
    arr = C._render_exr(os.path.join(out, '_tmp', 'pic.exr'), samples, filt, True, bounces)
    rgba = C._to_rgba(arr)
    C.write_png(os.path.join(out, name + '.png'), rgba)
    info = dict(kind=kind, w=int(w), h=int(h), files={'img': name + '.png'})
    info.update(extra or {})
    C._STATE['meta'][name] = info
    print('[pic] %-14s %dx%d  %d samples  %.1fs' % (name, w, h, samples, time.time() - t))
    return info
