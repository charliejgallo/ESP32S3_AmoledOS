"""Monster Hop - helpers shared by city.py and castle.py (zones_cc_*).

Not for the other zone scripts: they keep their own helpers.

Three things live here:

  * textures painted with numpy (periodic FFT noise, anti-aliased shapes from
    signed distances), loaded into Blender as float images. The block
    textures are painted per metre (TEX px / m) so that the cell's edges, the
    joints and the courses land exactly where the grid needs them, and so
    that every variant has the same edges;
  * a small node-graph builder for material build() callbacks (mh_common's
    registry swaps them between the passes);
  * geometry helpers (bmesh boxes, lathes, tubes, extruded outlines).

Everything is in metres, Z up, built at cell (0, 0) floor 0 like SPEC.md says.
"""
import math
import os
import random

import bmesh
import bpy
import numpy as np
from mathutils import Matrix, Vector as V

import mh_common as C

FM = C.FLOOR_M
TEX = 256                    # texture pixels per metre (a cell is ~60 px wide on screen)


# ---------------------------------------------------------------------------
# Colour
# ---------------------------------------------------------------------------

def lin(c):
    """sRGB colour ('#rrggbb' or (r, g, b) 0-255) to a linear (r, g, b) tuple."""
    if isinstance(c, str):
        h = c.lstrip('#')
        c = tuple(int(h[i:i + 2], 16) for i in (0, 2, 4))
    out = []
    for v in c:
        v = v / 255.0
        out.append(v / 12.92 if v <= 0.04045 else ((v + 0.055) / 1.055) ** 2.4)
    return tuple(out)


def lina(c):
    return np.array(lin(c), np.float32)


# ---------------------------------------------------------------------------
# numpy painting
# ---------------------------------------------------------------------------

class Paint:
    """A texture over the rectangle [u0, u1] x [v0, v1] metres (TEX px/m).
    Row 0 is v0 (Blender images start at the bottom). X, Y: pixel centres."""

    def __init__(self, u0, u1, v0, v1, px_per_m=TEX):
        self.u0, self.u1, self.v0, self.v1 = u0, u1, v0, v1
        self.w = int(round((u1 - u0) * px_per_m))
        self.h = int(round((v1 - v0) * px_per_m))
        self.px = (u1 - u0) / self.w                 # metres per pixel
        us = u0 + (np.arange(self.w) + 0.5) * (u1 - u0) / self.w
        vs = v0 + (np.arange(self.h) + 0.5) * (v1 - v0) / self.h
        self.X, self.Y = np.meshgrid(us, vs)

    def full(self, col):
        return np.broadcast_to(np.asarray(col, np.float32), (self.h, self.w, 3)).copy()

    def zeros(self):
        return np.zeros((self.h, self.w), np.float32)

    def noise(self, size_m, seed, aniso=(1.0, 1.0)):
        """Periodic (over the whole texture) smooth noise, std 1, blobs ~size_m."""
        rng = np.random.default_rng(seed)
        white = rng.standard_normal((self.h, self.w))
        fy = np.fft.fftfreq(self.h, d=self.px)[:, None] * aniso[1]
        fx = np.fft.fftfreq(self.w, d=self.px)[None, :] * aniso[0]
        f2 = fx * fx + fy * fy
        filt = np.exp(-f2 * (size_m * math.pi) ** 2 / 2.0)
        n = np.real(np.fft.ifft2(np.fft.fft2(white) * filt))
        n -= n.mean()
        s = n.std()
        return (n / s if s > 0 else n).astype(np.float32)

    def cover(self, sd, soft=None):
        """Coverage 0..1 of a signed distance (negative inside), anti-aliased."""
        s = self.px if soft is None else max(soft, self.px)
        return np.clip(0.5 - sd / s, 0.0, 1.0).astype(np.float32)

    # signed distances ----------------------------------------------------
    def sd_box(self, cx, cy, hw, hh, r=0.0, rot=0.0):
        X, Y = self.X - cx, self.Y - cy
        if rot:
            c, s = math.cos(rot), math.sin(rot)
            X, Y = c * X + s * Y, -s * X + c * Y
        qx, qy = np.abs(X) - hw + r, np.abs(Y) - hh + r
        return (np.hypot(np.maximum(qx, 0), np.maximum(qy, 0)) + np.minimum(np.maximum(qx, qy), 0) - r)

    def sd_circle(self, cx, cy, r):
        return np.hypot(self.X - cx, self.Y - cy) - r

    def sd_ellipse(self, cx, cy, rx, ry):
        # approximate (good enough for stains and puddles)
        k = np.hypot((self.X - cx) / rx, (self.Y - cy) / ry)
        return (k - 1.0) * min(rx, ry)

    def sd_seg(self, a, b):
        ax, ay = a
        bx, by = b
        px, py = self.X - ax, self.Y - ay
        dx, dy = bx - ax, by - ay
        L2 = dx * dx + dy * dy
        t = np.clip((px * dx + py * dy) / max(L2, 1e-12), 0, 1)
        return np.hypot(px - t * dx, py - t * dy)

    def sd_polyline(self, pts, width):
        d = np.full(self.X.shape, 1e9, np.float32)
        for a, b in zip(pts[:-1], pts[1:]):
            d = np.minimum(d, self.sd_seg(a, b))
        return d - width / 2.0

    def sd_poly(self, pts):
        """Signed distance to a closed polygon (negative inside)."""
        d = np.full(self.X.shape, 1e9, np.float32)
        inside = np.zeros(self.X.shape, bool)
        n = len(pts)
        for i in range(n):
            a, b = pts[i], pts[(i + 1) % n]
            d = np.minimum(d, self.sd_seg(a, b))
            (x0, y0), (x1, y1) = a, b
            cond = ((y0 > self.Y) != (y1 > self.Y))
            xint = (x1 - x0) * (self.Y - y0) / ((y1 - y0) if y1 != y0 else 1e-12) + x0
            inside ^= cond & (self.X < xint)
        return np.where(inside, -d, d)

    def edge_dist(self):
        """Distance to the texture's u edges and v edges (min)."""
        return np.minimum(np.minimum(self.X - self.u0, self.u1 - self.X),
                          np.minimum(self.Y - self.v0, self.v1 - self.Y))


def blend(img, m, col):
    """img*(1-m) + col*m (col a colour or an image)."""
    m = m[..., None]
    return img * (1 - m) + np.asarray(col, np.float32) * m


def crack_path(rng, a, b, n=6, jitter=0.03):
    """A wobbly polyline from a to b."""
    pts = []
    for i in range(n + 1):
        t = i / n
        x = a[0] + (b[0] - a[0]) * t
        y = a[1] + (b[1] - a[1]) * t
        if 0 < i < n:
            x += rng.uniform(-jitter, jitter)
            y += rng.uniform(-jitter, jitter)
        pts.append((x, y))
    return pts


_IMG_CACHE = {}


def image(name, arr, data=False):
    """A Blender image from a numpy array (h, w, 3|4 or h, w) in LINEAR values
    (colour) or plain data (data=True). Row 0 = bottom (v0). Images are kept
    by name: asking again for a name returns the first one (materials that
    use it stay valid)."""
    if name in _IMG_CACHE:
        return _IMG_CACHE[name]
    a = np.asarray(arr, np.float32)
    if a.ndim == 2:
        a = np.stack([a, a, a], -1)
    h, w = a.shape[:2]
    if a.shape[2] == 3:
        a = np.concatenate([a, np.ones((h, w, 1), np.float32)], -1)
    img = bpy.data.images.new(name, w, h, alpha=True, float_buffer=True)
    img.pixels.foreach_set(np.ascontiguousarray(a).ravel())
    dump = os.environ.get('MH_TEXDUMP')
    if dump:                       # debugging: every texture as an sRGB PNG
        os.makedirs(dump, exist_ok=True)
        s = a[::-1, :, :3] if data else C._srgb(a[::-1, :, :3])
        C.write_png(os.path.join(dump, name + '.png'), np.round(np.clip(s, 0, 1) * 255))
    # pack first: changing the colour space of a generated image regenerates
    # it (black); a packed one is reloaded from its packed pixels
    img.pack()
    img.colorspace_settings.name = 'Non-Color' if data else 'Linear'
    _IMG_CACHE[name] = img
    return img


# ---------------------------------------------------------------------------
# node-graph builder
# ---------------------------------------------------------------------------

class G:
    """Tiny helper around a material node tree (for C.mat(build=...))."""

    def __init__(self, nt, neutral=False):
        self.nt, self.neutral = nt, neutral

    def n(self, kind, **props):
        nd = self.nt.nodes.new(kind)
        for k, v in props.items():
            setattr(nd, k, v)
        return nd

    def set(self, inp, v):
        if isinstance(v, bpy.types.NodeSocket):
            self.nt.links.new(v, inp)
        elif v is not None:
            if inp.type == 'RGBA' and len(v) == 3:
                v = tuple(v) + (1.0,)
            inp.default_value = v

    # geometry
    def pos(self):
        return self.n('ShaderNodeNewGeometry').outputs['Position']

    def normal(self):
        return self.n('ShaderNodeNewGeometry').outputs['Normal']

    def sep(self, v):
        s = self.n('ShaderNodeSeparateXYZ')
        self.set(s.inputs[0], v)
        return s.outputs[0], s.outputs[1], s.outputs[2]

    def comb(self, x, y, z=0.0):
        c = self.n('ShaderNodeCombineXYZ')
        self.set(c.inputs[0], x)
        self.set(c.inputs[1], y)
        self.set(c.inputs[2], z)
        return c.outputs[0]

    def m(self, op, a, b=0.0, c=None, clamp=False):
        nd = self.n('ShaderNodeMath', operation=op, use_clamp=clamp)
        self.set(nd.inputs[0], a)
        self.set(nd.inputs[1], b)
        if c is not None:
            self.set(nd.inputs[2], c)
        return nd.outputs[0]

    def mapr(self, v, a, b, c=0.0, d=1.0, smooth=True, clamp=True):
        nd = self.n('ShaderNodeMapRange', interpolation_type='SMOOTHSTEP' if smooth else 'LINEAR',
                    clamp=clamp)
        self.set(nd.inputs['Value'], v)
        nd.inputs['From Min'].default_value = a
        nd.inputs['From Max'].default_value = b
        nd.inputs['To Min'].default_value = c
        nd.inputs['To Max'].default_value = d
        return nd.outputs['Result']

    def mix(self, fac, a, b, blend='MIX'):
        nd = self.n('ShaderNodeMixRGB', blend_type=blend)
        self.set(nd.inputs['Fac'], fac)
        self.set(nd.inputs['Color1'], a)
        self.set(nd.inputs['Color2'], b)
        return nd.outputs['Color']

    def tex(self, img, vec, interp='Linear', ext='REPEAT'):
        nd = self.n('ShaderNodeTexImage', interpolation=interp, extension=ext)
        nd.image = img
        self.set(nd.inputs['Vector'], vec)
        return nd.outputs['Color']

    def noise(self, vec, scale, detail=2.0, rough=0.5):
        nd = self.n('ShaderNodeTexNoise')
        self.set(nd.inputs['Vector'], vec)
        nd.inputs['Scale'].default_value = scale
        nd.inputs['Detail'].default_value = detail
        nd.inputs['Roughness'].default_value = rough
        return nd.outputs['Fac']

    def bump(self, height, strength=0.3, dist=0.01, normal=None):
        nd = self.n('ShaderNodeBump')
        self.set(nd.inputs['Height'], height)
        nd.inputs['Strength'].default_value = strength
        nd.inputs['Distance'].default_value = dist
        if normal is not None:
            self.set(nd.inputs['Normal'], normal)
        return nd.outputs['Normal']

    def bw(self, col):
        nd = self.n('ShaderNodeRGBToBW')
        self.set(nd.inputs[0], col)
        return nd.outputs[0]

    def opos(self):
        """Object-space position (follows an object rotated after it was built)."""
        return self.n('ShaderNodeTexCoord').outputs['Object']

    def bsdf(self, base, rough=0.7, spec=0.35, metal=0.0, normal=None, emit=None, emit_strength=1.0,
             clearcoat=0.0, cc_rough=0.1, alpha=None):
        b = self.n('ShaderNodeBsdfPrincipled')
        if alpha is not None:
            self.set(b.inputs['Alpha'], alpha)
        self.set(b.inputs['Base Color'], (0.8, 0.8, 0.8) if self.neutral else base)
        self.set(b.inputs['Roughness'], rough)
        self.set(b.inputs['Specular'], spec)
        self.set(b.inputs['Metallic'], metal)
        if clearcoat:
            b.inputs['Clearcoat'].default_value = clearcoat
            b.inputs['Clearcoat Roughness'].default_value = cc_rough
        if normal is not None:
            self.set(b.inputs['Normal'], normal)
        if emit is not None:
            self.set(b.inputs['Emission'], emit)
            self.set(b.inputs['Emission Strength'], emit_strength)
        return b.outputs['BSDF']


# ---------------------------------------------------------------------------
# block materials: a top texture, a side texture, picked by the normal
# ---------------------------------------------------------------------------

def block_mat(key, top=None, side=None, z0=-FM, z1=0.0, rough=0.8, spec=0.3, bump=0.25,
              bump_dist=0.02, top_rough=None, side_rough=None, emit_strength=1.0, metal=0.0,
              clearcoat=0.0, tint=None, sheen=0.0):
    """Register material `key`: `top` / `side` are dicts of images
    {'col': img, 'h': img (height, optional), 'e': img (emission, optional)}.
    The top texture spans x, y in [0, 1]; the side texture spans u in [0, 1]
    (x on the front/back faces, y on the right/left faces) and z in [z0, z1]."""

    def build(nt, neutral):
        g = G(nt, neutral)
        p = g.pos()
        x, y, z = g.sep(p)
        nx, ny, nz = g.sep(g.normal())
        is_top = g.mapr(nz, 0.45, 0.7)
        side_x = g.m('GREATER_THAN', g.m('ABSOLUTE', nx), g.m('ABSOLUTE', ny))
        u = g.mix(side_x, g.comb(x, 0, 0), g.comb(y, 0, 0))
        u, _, _ = g.sep(u)
        v = g.m('MULTIPLY', g.m('SUBTRACT', z, z0), 1.0 / (z1 - z0))
        uv_side = g.comb(u, v, 0)
        uv_top = g.comb(x, y, 0)
        parts = {}
        for k in ('col', 'h', 'e'):
            ts = g.tex(top[k], uv_top) if (top and k in top) else None
            ss = g.tex(side[k], uv_side, ext='EXTEND' if False else 'REPEAT') if (side and k in side) else None
            if ts is not None and ss is not None:
                parts[k] = g.mix(is_top, ss, ts)
            else:
                parts[k] = ts if ts is not None else ss
        col = parts['col']
        if tint is not None:
            col = g.mix(1.0, col, tuple(tint), 'MULTIPLY')
        nrm = None
        if parts.get('h') is not None and bump > 0:
            nrm = g.bump(g.bw(parts['h']), bump, bump_dist)
        r = rough
        if top_rough is not None or side_rough is not None:
            r = g.m('ADD', g.m('MULTIPLY', is_top, (top_rough if top_rough is not None else rough)),
                    g.m('MULTIPLY', g.m('SUBTRACT', 1.0, is_top),
                        (side_rough if side_rough is not None else rough)))
        out = g.bsdf(col, r, spec, metal, nrm, parts.get('e'), emit_strength, clearcoat)
        if sheen:
            out.node.inputs['Sheen'].default_value = sheen
            out.node.inputs['Sheen Tint'].default_value = 0.8
        return out

    return C.mat(key, build=build)


def simple_mat(key, col, rough=0.6, spec=0.4, metal=0.0, emit=None, emit_strength=0.0, clearcoat=0.0,
               noise=0.0, noise_scale=8.0, col2=None, bump=0.0, bump_scale=30.0, id=0, alpha=None,
               obj=False):
    """A flat or lightly mottled material (props). col: sRGB (0-255) or hex.
    id: region id (recoloured sprites); alpha: constant opacity (steam, glass);
    obj: mottle in object space (the pattern turns with a rotated object)."""
    c1 = lin(col)
    c2 = lin(col2) if col2 is not None else None

    def build(nt, neutral):
        g = G(nt, neutral)
        base = c1
        p = g.opos() if obj else g.pos()
        if c2 is not None or noise > 0:
            f = g.noise(p, noise_scale, 3.0, 0.55)
            if c2 is not None:
                base = g.mix(g.mapr(f, 0.35, 0.65), c1, c2)
        nrm = None
        if bump > 0:
            nrm = g.bump(g.noise(p, bump_scale, 2.0, 0.5), bump, 0.01)
        e = lin(emit) if emit is not None else None
        return g.bsdf(base, rough, spec, metal, nrm, e, emit_strength, clearcoat, alpha=alpha)

    return C.mat(key, build=build, id=id)


def emit_mat(key, col, strength=3.0):
    """Pure emission (flames, bulbs, glass lit from inside)."""
    c = lin(col)

    def build(nt, neutral):
        g = G(nt, neutral)
        e = g.n('ShaderNodeEmission')
        g.set(e.inputs['Color'], (1, 1, 1) if neutral else c)
        e.inputs['Strength'].default_value = strength
        return e.outputs['Emission']

    return C.mat(key, build=build)


def tex_mat(key, img, uv='xz', rough=0.6, spec=0.4, himg=None, bump=0.2, eimg=None, emit_strength=1.0,
            scale=(1.0, 1.0), offset=(0.0, 0.0), metal=0.0, clearcoat=0.0, aimg=None, obj=False, id=0,
            ext='REPEAT'):
    """A material with an image mapped by position on two axes (uv='xz': u = x,
    v = z; 'yz', 'xy'), world space or object space (obj=True). scale: metres
    per image. aimg: an opacity image (its red channel), for see-through
    meshes (chain-link, grates)."""
    ax = {'x': 0, 'y': 1, 'z': 2}

    def build(nt, neutral):
        g = G(nt, neutral)
        s = g.sep(g.opos() if obj else g.pos())
        u = g.m('MULTIPLY', g.m('SUBTRACT', s[ax[uv[0]]], offset[0]), 1.0 / scale[0])
        v = g.m('MULTIPLY', g.m('SUBTRACT', s[ax[uv[1]]], offset[1]), 1.0 / scale[1])
        vec = g.comb(u, v, 0)
        col = g.tex(img, vec, ext=ext)
        nrm = g.bump(g.bw(g.tex(himg, vec, ext=ext)), bump, 0.02) if himg is not None else None
        e = g.tex(eimg, vec, ext=ext) if eimg is not None else None
        a = g.sep(g.tex(aimg, vec, ext=ext))[0] if aimg is not None else None
        return g.bsdf(col, rough, spec, metal, nrm, e, emit_strength, clearcoat, alpha=a)

    return C.mat(key, build=build, id=id)


# ---------------------------------------------------------------------------
# geometry
# ---------------------------------------------------------------------------

def obj_from_bm(name, bm, key=None, smooth=False, auto=40.0):
    me = bpy.data.meshes.new(name)
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces[:])
    bm.to_mesh(me)
    bm.free()
    ob = bpy.data.objects.new(name, me)
    C.link(ob)
    if smooth:
        for p in me.polygons:
            p.use_smooth = True
        if auto:
            me.use_auto_smooth = True
            me.auto_smooth_angle = math.radians(auto)
    if key:
        C.assign(ob, key)
    return ob


def bevel(ob, w, seg=2, angle=35.0):
    m = ob.modifiers.new('bevel', 'BEVEL')
    m.width = w
    m.segments = seg
    m.limit_method = 'ANGLE'
    m.angle_limit = math.radians(angle)
    m.harden_normals = False
    return ob


def subsurf(ob, levels=2):
    m = ob.modifiers.new('subd', 'SUBSURF')
    m.levels = levels
    m.render_levels = levels
    return ob


def box(name, x0, y0, z0, x1, y1, z1, key=None, bev=0.0, seg=2):
    bm = bmesh.new()
    bmesh.ops.create_cube(bm, size=1.0)
    for v in bm.verts:
        v.co = V((x0 + (v.co.x + 0.5) * (x1 - x0), y0 + (v.co.y + 0.5) * (y1 - y0),
                  z0 + (v.co.z + 0.5) * (z1 - z0)))
    ob = obj_from_bm(name, bm, key)
    if bev > 0:
        bevel(ob, bev, seg)
    return ob


def cbox(name, c, s, key=None, bev=0.0, seg=2, rz=0.0, rx=0.0, ry=0.0):
    """Box centred at c with size s, rotated (degrees, X then Y then Z about c)."""
    bm = bmesh.new()
    bmesh.ops.create_cube(bm, size=1.0)
    M = (Matrix.Translation(V(c)) @ Matrix.Rotation(math.radians(rz), 4, 'Z') @
         Matrix.Rotation(math.radians(ry), 4, 'Y') @ Matrix.Rotation(math.radians(rx), 4, 'X') @
         Matrix.Diagonal(tuple(s) + (1.0,)))
    bmesh.ops.transform(bm, matrix=M, verts=bm.verts[:])
    ob = obj_from_bm(name, bm, key)
    if bev > 0:
        bevel(ob, bev, seg)
    return ob


def lathe(name, prof, key=None, c=(0.5, 0.5, 0.0), segs=24, smooth=True, auto=40.0, sx=1.0, sy=1.0,
          phase=0.0, caps=True):
    """Revolve a profile [(r, z), ...] (bottom to top) around a vertical axis at c.
    r == 0 makes a pole. sx, sy squash the section (ovals)."""
    bm = bmesh.new()
    rings = []
    for r, z in prof:
        if r <= 1e-6:
            rings.append([bm.verts.new((c[0], c[1], c[2] + z))])
        else:
            ring = []
            for k in range(segs):
                a = 2 * math.pi * k / segs + phase
                ring.append(bm.verts.new((c[0] + r * math.cos(a) * sx, c[1] + r * math.sin(a) * sy, c[2] + z)))
            rings.append(ring)
    for r0, r1 in zip(rings[:-1], rings[1:]):
        if len(r0) == 1 and len(r1) == 1:
            continue
        if len(r0) == 1:
            for k in range(segs):
                bm.faces.new((r0[0], r1[k], r1[(k + 1) % segs]))
        elif len(r1) == 1:
            for k in range(segs):
                bm.faces.new((r0[k], r0[(k + 1) % segs], r1[0]))
        else:
            for k in range(segs):
                bm.faces.new((r0[k], r0[(k + 1) % segs], r1[(k + 1) % segs], r1[k]))
    if caps and len(rings[0]) > 1:
        bm.faces.new(list(reversed(rings[0])))
    if caps and len(rings[-1]) > 1:
        bm.faces.new(rings[-1])
    return obj_from_bm(name, bm, key, smooth, auto)


def cyl(name, c, r, z0, z1, key=None, segs=20, smooth=True, r1=None):
    r1 = r if r1 is None else r1
    return lathe(name, [(0, z0), (r, z0), (r1, z1), (0, z1)], key, c=(c[0], c[1], 0.0), segs=segs,
                 smooth=smooth)


def sphere(name, c, r, key=None, scale=(1, 1, 1), segs=20, rings=12):
    prof = []
    for i in range(rings + 1):
        t = math.pi * i / rings
        prof.append((r * math.sin(t), -r * math.cos(t)))
    ob = lathe(name, prof, key, c=(0, 0, 0), segs=segs, smooth=True, auto=0)
    ob.scale = scale
    ob.location = c
    return ob


def tube(name, pts, r, key=None, segs=12, caps=True, smooth=True):
    """A tube along a polyline; r is a radius or a list of radii."""
    pts = [V(p) for p in pts]
    rs = list(r) if isinstance(r, (list, tuple)) else [r] * len(pts)
    bm = bmesh.new()
    t0 = (pts[1] - pts[0]).normalized()
    ref = V((0, 0, 1)) if abs(t0.z) < 0.9 else V((1, 0, 0))
    nrm = t0.cross(ref).normalized()
    rings = []
    for i, p in enumerate(pts):
        if i == 0:
            t = pts[1] - pts[0]
        elif i == len(pts) - 1:
            t = pts[-1] - pts[-2]
        else:
            t = (pts[i + 1] - pts[i]).normalized() + (pts[i] - pts[i - 1]).normalized()
        t.normalize()
        nrm = (nrm - t * nrm.dot(t)).normalized()
        b = t.cross(nrm)
        ring = [bm.verts.new(p + (nrm * math.cos(2 * math.pi * k / segs) + b * math.sin(2 * math.pi * k / segs)) * rs[i])
                for k in range(segs)]
        rings.append(ring)
    for r0, r1 in zip(rings[:-1], rings[1:]):
        for k in range(segs):
            bm.faces.new((r0[k], r0[(k + 1) % segs], r1[(k + 1) % segs], r1[k]))
    if caps:
        bm.faces.new(list(reversed(rings[0])))
        bm.faces.new(rings[-1])
    return obj_from_bm(name, bm, key, smooth, auto=0)


def prism(name, pts2, z0, z1, key=None, axis='z', bev=0.0, seg=2, offset=0.0):
    """Extrude a 2D outline. axis 'z': pts are (x, y), extruded z0..z1;
    'y': pts are (x, z), extruded along y from z0 to z1; 'x': pts are (y, z)."""
    bm = bmesh.new()

    def P(a, b, h):
        if axis == 'z':
            return (a, b, h)
        if axis == 'y':
            return (a, h, b)
        return (h, a, b)
    lo = [bm.verts.new(P(a, b, z0)) for a, b in pts2]
    hi = [bm.verts.new(P(a, b, z1)) for a, b in pts2]
    n = len(pts2)
    bm.faces.new(lo[::-1])
    bm.faces.new(hi)
    for i in range(n):
        j = (i + 1) % n
        bm.faces.new((lo[i], lo[j], hi[j], hi[i]))
    ob = obj_from_bm(name, bm, key)
    if bev > 0:
        bevel(ob, bev, seg)
    return ob


def arch_outline(cx, w, z0, zs, zt, n=10):
    """Outline (x, z) of a pointed (gothic) arch window: width w, sides from z0
    up to the springing zs, apex at zt."""
    hw = w / 2.0
    # two arcs centred on the opposite springing points (equilateral-ish)
    R = ((zt - zs) ** 2 + hw * hw) / (2 * hw) if hw > 0 else 0
    pts = [(cx - hw, z0), (cx + hw, z0), (cx + hw, zs)]
    # right arc: centre at (cx + hw - R, zs)
    ccx = cx + hw - R
    a0 = 0.0
    a1 = math.atan2(zt - zs, cx - ccx)
    for i in range(1, n):
        a = a0 + (a1 - a0) * i / n
        pts.append((ccx + R * math.cos(a), zs + R * math.sin(a)))
    pts.append((cx, zt))
    ccx2 = cx - hw + R
    for i in range(n - 1, 0, -1):
        a = a0 + (a1 - a0) * i / n
        pts.append((ccx2 - R * math.cos(a), zs + R * math.sin(a)))
    pts.append((cx - hw, zs))
    return pts


def join(objs, name):
    """Join meshes into one object (keeps every material slot and mh_keys)."""
    keys = []
    for o in objs:
        keys.extend(list(o.get('mh_keys', [])))
    ctx = bpy.context
    for o in ctx.scene.objects:
        o.select_set(False)
    for o in objs:
        o.select_set(True)
    ctx.view_layer.objects.active = objs[0]
    bpy.ops.object.join()
    ob = ctx.view_layer.objects.active
    ob.name = name
    ob['mh_keys'] = keys
    return ob


def apply_mods(ob):
    ctx = bpy.context
    for o in ctx.scene.objects:
        o.select_set(False)
    ob.select_set(True)
    ctx.view_layer.objects.active = ob
    for m in list(ob.modifiers):
        bpy.ops.object.modifier_apply(modifier=m.name)
    return ob


def cut(ob, cutters, slot=0):
    """Boolean-subtract the cutter objects from ob (applied), delete the
    cutters. The new faces take material slot `slot` of ob (the exact solver
    would give them an empty slot of their own)."""
    nslots = len(ob.data.materials)
    for c in cutters:
        m = ob.modifiers.new('cut', 'BOOLEAN')
        m.operation = 'DIFFERENCE'
        m.solver = 'EXACT'
        m.object = c
        apply_mods(ob)
    for p in ob.data.polygons:
        if p.material_index >= nslots:
            p.material_index = slot
    while len(ob.data.materials) > nslots:
        ob.data.materials.pop(index=len(ob.data.materials) - 1)
    for c in cutters:
        bpy.data.objects.remove(c, do_unlink=True)
    return ob


def add_spot(pos, color=(1.0, 0.6, 0.25), power=100.0, cone_deg=80.0, blend=0.8, radius=0.05,
             aim=(0.0, 0.0, -1.0), name='spot'):
    """A soft downward spot light: a lamp's pool of light with an edge (a
    point light 2 m up lights the whole screen)."""
    lt = bpy.data.lights.new(name, 'SPOT')
    lt.color = color
    lt.energy = power
    lt.shadow_soft_size = radius
    lt.spot_size = math.radians(cone_deg)
    lt.spot_blend = blend
    ob = bpy.data.objects.new(name, lt)
    ob.location = pos
    ob.rotation_euler = (-V(aim)).to_track_quat('Z', 'Y').to_euler()
    C.link(ob)
    return ob


def remove_lights(lights):
    for ob in lights:
        lt = ob.data
        bpy.data.objects.remove(ob, do_unlink=True)
        bpy.data.lights.remove(lt)


# ---------------------------------------------------------------------------
# asset registry and runner
# ---------------------------------------------------------------------------

class Registry:
    """name -> builder. A builder returns (objs, render kwargs) or does its own
    rendering and returns None."""

    def __init__(self):
        self.items = []

    def add(self, name, sample=False):
        def deco(fn):
            self.items.append((name, fn, sample))
            return fn
        return deco

    def select(self, only, sample):
        import fnmatch
        out = []
        for name, fn, smp in self.items:
            if only:
                if not any(fnmatch.fnmatch(name, pat) or fnmatch.fnmatch(name, '*_' + pat)
                           for pat in only):
                    continue
            elif sample and not smp:
                continue
            out.append((name, fn))
        return out


def cli_flags():
    """Our own flags (--sample), removed from sys.argv so that C.args()
    does not read --sample as an abbreviation of --samples."""
    import sys
    if '--' not in sys.argv:
        return dict(sample=False)
    i = sys.argv.index('--')
    sample = '--sample' in sys.argv[i + 1:]
    sys.argv[i + 1:] = [a for a in sys.argv[i + 1:] if a != '--sample']
    return dict(sample=sample)


def rng_for(name, k=0):
    return random.Random(hash_str(name) * 7919 + k)


def hash_str(s):
    h = 2166136261
    for ch in s.encode():
        h = ((h ^ ch) * 16777619) & 0xffffffff
    return h
