"""Mila - the casita (Mila's home, the hub). SPEC.md section 6.

    /Applications/Blender.app/Contents/MacOS/Blender -b -P casita.py -- --out ../../assets/casita [--only a,b,toy_x_*] [--mila]

Everything at zoom C.CASITA_ZOOM under C.reset('casita'). The room is one big
sprite (colour + z) anchored at the world origin; every other piece is its
own sprite at its fixed spot (colour + z + shadow), so the watch can show or
hide it. The whole room is built in world coordinates (metres):

    floor x 0 .. 3.4, y 0 .. 3.0 (a diorama: no side walls)
    back wall at y = 3.0, 2.0 m tall
    door x 0.3 .. 1.0 (arched), window x 1.9 .. 3.1, z 0.75 .. 1.6

The room (with the rug baked in) is the engine's background, drawn once; the
doorway is open in it and shows a little hall, and the door panel is always
drawn as casita_door_<nn> (00 = closed). By day, casita_window_day is laid
over the window. The top ~0.35 m of the wall is only wallpaper and trim (it
lies under the top HUD); the front 0.2 m of floor lies under the bottom bar.

--mila also renders a temporary sitting Mila from sample.py's model into
<out>/_tmp (phase 1 preview; the Mila artist's frames replace it).
"""
import math
import os
import random
import sys

import bmesh
import bpy
from mathutils import Euler, Matrix, Vector as V

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ml_common as C  # noqa: E402

ARGS = C.args({'samples': 96})
ZOOM = C.CASITA_ZOOM
ONLY = set(ARGS.only)
X0, X1 = -0.01, 3.41      # the floor and wall run a hair past 0 .. 3.4 so the 368 px screen has no gap
WY = 3.0                  # the back wall's face


def want(name):
    return not ONLY or name in ONLY or any(name.startswith(o.rstrip('*')) for o in ONLY if o.endswith('*'))


# ---------------------------------------------------------------------------
# materials (node trees through C.mat(build=...), so every pass works)
# ---------------------------------------------------------------------------

def _n(nt, kind, **inputs):
    node = nt.nodes.new(kind)
    for k, v in inputs.items():
        node.inputs[k].default_value = v
    return node


def _bsdf(nt, col, neutral, rough=0.6, spec=0.4, sheen=0.0, cc=0.0, metal=0.0, emit=None, es=0.0):
    b = nt.nodes.new('ShaderNodeBsdfPrincipled')
    b.inputs['Base Color'].default_value = ((0.8, 0.8, 0.8) if neutral else tuple(col)) + (1,)
    b.inputs['Roughness'].default_value = rough
    b.inputs['Specular'].default_value = spec
    b.inputs['Sheen'].default_value = sheen
    b.inputs['Sheen Tint'].default_value = 0.5
    b.inputs['Clearcoat'].default_value = cc
    b.inputs['Clearcoat Roughness'].default_value = 0.15
    b.inputs['Metallic'].default_value = metal
    if emit is not None and es > 0:
        b.inputs['Emission'].default_value = tuple(emit) + (1,)
        b.inputs['Emission Strength'].default_value = es
    return b


def _coords(nt, kind='Object'):
    return nt.nodes.new('ShaderNodeTexCoord').outputs[kind]


def _bump(nt, b, height_sock, strength, dist=0.01):
    bp = _n(nt, 'ShaderNodeBump', Strength=strength, Distance=dist)
    nt.links.new(height_sock, bp.inputs['Height'])
    nt.links.new(bp.outputs['Normal'], b.inputs['Normal'])


def _noise(nt, scale, detail=4.0, vec=None):
    tx = _n(nt, 'ShaderNodeTexNoise', Scale=scale, Detail=detail)
    nt.links.new(vec if vec is not None else _coords(nt), tx.inputs['Vector'])
    return tx


def _ramp(nt, stops, interp='LINEAR'):
    r = nt.nodes.new('ShaderNodeValToRGB')
    r.color_ramp.interpolation = interp
    els = r.color_ramp.elements
    els[0].position, els[0].color = stops[0][0], tuple(stops[0][1]) + (1,)
    els[1].position, els[1].color = stops[-1][0], tuple(stops[-1][1]) + (1,)
    for pos, col in stops[1:-1]:
        e = els.new(pos)
        e.color = tuple(col) + (1,)
    return r


def _math(nt, op, a, b=None):
    m = nt.nodes.new('ShaderNodeMath')
    m.operation = op
    for i, v in enumerate((a, b)):
        if v is None:
            continue
        if isinstance(v, (int, float)):
            m.inputs[i].default_value = v
        else:
            nt.links.new(v, m.inputs[i])
    return m.outputs[0]


def _xyz(nt, vec=None):
    s = nt.nodes.new('ShaderNodeSeparateXYZ')
    nt.links.new(vec if vec is not None else _coords(nt), s.inputs[0])
    return s.outputs


def P(key, col, bump=0.0, bscale=60.0, bdist=0.01, **kw):
    """Plain principled, optional noise bump (fabric, plush, felt)."""
    if key in C._MATS:
        return key

    def build(nt, neutral):
        b = _bsdf(nt, col, neutral, **kw)
        if bump > 0:
            _bump(nt, b, _noise(nt, bscale).outputs['Fac'], bump, bdist)
        return b.outputs['BSDF']
    C.mat(key, base=col, build=build)
    return key


def WOOD(key, col, scale=2.0, grain=10.0, var=0.0, rough=0.55, cc=0.12, axis='X'):
    """Wood grain along X (or Z), a darker late wood; `var` shifts each object's shade."""
    if key in C._MATS:
        return key

    def build(nt, neutral):
        b = _bsdf(nt, col, neutral, rough=rough, spec=0.35, cc=cc)
        if neutral:
            return b.outputs['BSDF']
        mp = nt.nodes.new('ShaderNodeMapping')
        mp.inputs['Scale'].default_value = (1.0, grain, grain) if axis == 'X' else (grain, grain, 1.0)
        info = nt.nodes.new('ShaderNodeObjectInfo')
        off = nt.nodes.new('ShaderNodeVectorMath')
        off.operation = 'ADD'
        nt.links.new(_coords(nt), off.inputs[0])
        cmb = nt.nodes.new('ShaderNodeCombineXYZ')
        nt.links.new(_math(nt, 'MULTIPLY', info.outputs['Random'], 17.0), cmb.inputs[0])
        nt.links.new(off.outputs[0], mp.inputs['Vector'])
        nt.links.new(cmb.outputs[0], off.inputs[1])
        tx = _noise(nt, scale, 3.0, mp.outputs['Vector'])
        r = _ramp(nt, [(0.35, [c * 0.80 for c in col]), (0.65, col)])
        nt.links.new(tx.outputs['Fac'], r.inputs['Fac'])
        mix = nt.nodes.new('ShaderNodeMixRGB')
        mix.blend_type = 'MULTIPLY'
        mix.inputs['Fac'].default_value = 1.0
        shade = _math(nt, 'MULTIPLY_ADD', info.outputs['Random'], 2 * var)
        shade.node.inputs[2].default_value = 1.0 - var
        gray = nt.nodes.new('ShaderNodeCombineRGB')
        for i in range(3):
            nt.links.new(shade, gray.inputs[i])
        nt.links.new(r.outputs['Color'], mix.inputs['Color1'])
        nt.links.new(gray.outputs[0], mix.inputs['Color2'])
        nt.links.new(mix.outputs['Color'], b.inputs['Base Color'])
        _bump(nt, b, tx.outputs['Fac'], 0.08, 0.005)
        return b.outputs['BSDF']
    C.mat(key, base=col, build=build)
    return key


def STRIPES(key, axis, period, stops, rough=0.85, spec=0.25, bump=0.0, sheen=0.0, interp='CONSTANT'):
    """Bands along an object-space axis (wallpaper, beadboard)."""
    if key in C._MATS:
        return key

    def build(nt, neutral):
        b = _bsdf(nt, stops[0][1], neutral, rough=rough, spec=spec, sheen=sheen)
        if neutral:
            return b.outputs['BSDF']
        comp = _xyz(nt)['XYZ'.index(axis)]
        fr = _math(nt, 'FRACT', _math(nt, 'DIVIDE', comp, period))
        r = _ramp(nt, stops, interp)
        nt.links.new(fr, r.inputs['Fac'])
        nt.links.new(r.outputs['Color'], b.inputs['Base Color'])
        if bump > 0:
            _bump(nt, b, _noise(nt, 90.0).outputs['Fac'], bump, 0.004)
        return b.outputs['BSDF']
    C.mat(key, base=stops[0][1], build=build)
    return key


def WALLPAPER(key):
    """Pastel stripes: rose and cream, a thin rose pinstripe in the cream, and
    a faint paper texture."""
    if key in C._MATS:
        return key
    rose, cream, pin = (0.93, 0.66, 0.62), (0.99, 0.88, 0.78), (0.90, 0.58, 0.58)

    def build(nt, neutral):
        b = _bsdf(nt, rose, neutral, rough=0.85, spec=0.2)
        if neutral:
            return b.outputs['BSDF']
        fr = _math(nt, 'FRACT', _math(nt, 'DIVIDE', _xyz(nt)[0], 0.22))
        r = _ramp(nt, [(0.0, rose), (0.5, cream), (0.72, pin), (0.78, cream)], 'CONSTANT')
        nt.links.new(fr, r.inputs['Fac'])
        nt.links.new(r.outputs['Color'], b.inputs['Base Color'])
        _bump(nt, b, _noise(nt, 120.0).outputs['Fac'], 0.05, 0.003)
        return b.outputs['BSDF']
    C.mat(key, base=rose, build=build)
    return key


def EMIT(key, col, strength=1.0):
    """Emission only: the world outside the window is not lit by the room."""
    if key in C._MATS:
        return key

    def build(nt, neutral):
        e = _n(nt, 'ShaderNodeEmission', Color=tuple(col) + (1,), Strength=strength)
        return e.outputs['Emission']
    C.mat(key, base=col, build=build)
    return key


def SKY(key, z0, z1, stops, strength=1.0):
    """Emission gradient over world z (the sky in the window)."""
    if key in C._MATS:
        return key

    def build(nt, neutral):
        mr = _n(nt, 'ShaderNodeMapRange')
        mr.inputs['From Min'].default_value = z0
        mr.inputs['From Max'].default_value = z1
        nt.links.new(_xyz(nt)[2], mr.inputs['Value'])
        r = _ramp(nt, stops)
        nt.links.new(mr.outputs['Result'], r.inputs['Fac'])
        e = _n(nt, 'ShaderNodeEmission', Strength=strength)
        nt.links.new(r.outputs['Color'], e.inputs['Color'])
        return e.outputs['Emission']
    C.mat(key, base=stops[0][1], build=build)
    return key


def GLINT(key, amount=0.22):
    """A see-through white streak on the window glass."""
    if key in C._MATS:
        return key

    def build(nt, neutral):
        mix = _n(nt, 'ShaderNodeMixShader', Fac=amount)
        t = nt.nodes.new('ShaderNodeBsdfTransparent')
        e = _n(nt, 'ShaderNodeEmission', Color=(1, 1, 1, 1), Strength=1.0)
        nt.links.new(t.outputs[0], mix.inputs[1])
        nt.links.new(e.outputs[0], mix.inputs[2])
        return mix.outputs[0]
    C.mat(key, base=(1, 1, 1), build=build)
    return key


def RUG(key, rx, ry, rings):
    """Concentric braided rings (colour by the elliptic radius in object space)."""
    if key in C._MATS:
        return key

    def build(nt, neutral):
        b = _bsdf(nt, rings[0][1], neutral, rough=0.95, spec=0.15, sheen=1.0)
        s = _xyz(nt)
        vx = _math(nt, 'DIVIDE', s[0], rx)
        vy = _math(nt, 'DIVIDE', s[1], ry)
        rr = _math(nt, 'SQRT', _math(nt, 'ADD', _math(nt, 'MULTIPLY', vx, vx), _math(nt, 'MULTIPLY', vy, vy)))
        if not neutral:
            r = _ramp(nt, rings, 'CONSTANT')
            nt.links.new(rr, r.inputs['Fac'])
            nt.links.new(r.outputs['Color'], b.inputs['Base Color'])
        # the braid: little diagonal ridges along each ring
        w = nt.nodes.new('ShaderNodeTexWave')
        w.wave_type = 'BANDS'
        w.bands_direction = 'DIAGONAL'
        w.inputs['Scale'].default_value = 40.0
        w.inputs['Distortion'].default_value = 1.0
        nt.links.new(_coords(nt), w.inputs['Vector'])
        _bump(nt, b, w.outputs['Fac'], 0.25, 0.004)
        return b.outputs['BSDF']
    C.mat(key, base=rings[0][1], build=build)
    return key


def ROPE(key, col, pitch=0.035):
    """Sisal rope coiled round a pole along Z (object space, pole on the Z axis)."""
    if key in C._MATS:
        return key

    def build(nt, neutral):
        b = _bsdf(nt, col, neutral, rough=0.95, spec=0.2, sheen=0.4)
        s = _xyz(nt)
        ang = _math(nt, 'DIVIDE', _math(nt, 'ARCTAN2', s[1], s[0]), 2 * math.pi)
        ph = _math(nt, 'FRACT', _math(nt, 'ADD', _math(nt, 'DIVIDE', s[2], pitch), ang))
        tri = _math(nt, 'ABSOLUTE', _math(nt, 'SUBTRACT', ph, 0.5))      # 0 at the groove centre... 0.5 at the ridge
        if not neutral:
            r = _ramp(nt, [(0.0, col), (0.38, col), (0.5, [c * 0.55 for c in col])])
            nt.links.new(tri, r.inputs['Fac'])
            nt.links.new(r.outputs['Color'], b.inputs['Base Color'])
        _bump(nt, b, _math(nt, 'SUBTRACT', 0.5, tri), 0.9, 0.012)
        return b.outputs['BSDF']
    C.mat(key, base=col, build=build)
    return key


def CLEAR(key, tint, body, lo, hi, gloss=0.6, back=None, glow=0.0):
    """Water / glass: see-through in the middle, more body at grazing angles.
    back: the body of the far side seen from inside (the water's blue backdrop
    that the fish swim in front of), or None to treat both sides alike."""
    if key in C._MATS:
        return key

    def build(nt, neutral):
        lw = _n(nt, 'ShaderNodeLayerWeight', Blend=0.45)
        mr = _n(nt, 'ShaderNodeMapRange')
        mr.inputs['To Min'].default_value = lo
        mr.inputs['To Max'].default_value = hi
        nt.links.new(lw.outputs['Facing'], mr.inputs['Value'])
        fac = mr.outputs['Result']
        if back is not None:
            geo = nt.nodes.new('ShaderNodeNewGeometry')
            diff = _math(nt, 'SUBTRACT', back, fac)
            fac = _math(nt, 'ADD', fac, _math(nt, 'MULTIPLY', diff, geo.outputs['Backfacing']))
        mix = nt.nodes.new('ShaderNodeMixShader')
        nt.links.new(fac, mix.inputs['Fac'])
        t = _n(nt, 'ShaderNodeBsdfTransparent', Color=tuple(tint) + (1,))
        b = _bsdf(nt, body, neutral, rough=0.08, spec=gloss, cc=0.8, emit=body, es=glow)
        nt.links.new(t.outputs[0], mix.inputs[1])
        nt.links.new(b.outputs[0], mix.inputs[2])
        return mix.outputs[0]
    C.mat(key, base=body, build=build)
    return key


# ---------------------------------------------------------------------------
# geometry helpers (all meshes: fit() only sees meshes)
# ---------------------------------------------------------------------------

def _keys(ob, key):
    for k in (key if isinstance(key, (list, tuple)) else [key]):
        C.assign(ob, k)
    return ob


def from_bm(name, bm, key, smooth=True, normals=True):
    if normals:
        bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    me = bpy.data.meshes.new(name)
    bm.to_mesh(me)
    bm.free()
    ob = bpy.data.objects.new(name, me)
    C.link(ob)
    if smooth:
        for p in me.polygons:
            p.use_smooth = True
    return _keys(ob, key)


def place(ob, loc=(0, 0, 0), rot=(0, 0, 0), scale=(1, 1, 1)):
    ob.location = loc
    ob.rotation_euler = Euler([math.radians(a) for a in rot], 'XYZ')
    ob.scale = scale
    return ob


def ellipsoid(name, c, size, key, rot=(0, 0, 0), seg=32, rings=16):
    bm = bmesh.new()
    bmesh.ops.create_uvsphere(bm, u_segments=seg, v_segments=rings, radius=1.0)
    return place(from_bm(name, bm, key), c, rot, size)


def lathe(name, prof, key, c=(0, 0, 0), n=48, rot=(0, 0, 0), scale=(1, 1, 1), smooth=True):
    """Revolve a profile [(r, z), ...] about Z; r == 0 ends become poles."""
    bm = bmesh.new()
    rows = []
    for r, z in prof:
        if r <= 1e-6:
            rows.append([bm.verts.new((0, 0, z))])
        else:
            rows.append([bm.verts.new((r * math.cos(2 * math.pi * i / n), r * math.sin(2 * math.pi * i / n), z))
                         for i in range(n)])
    for a, b in zip(rows, rows[1:]):
        if len(a) == 1 and len(b) == 1:
            continue
        for i in range(n):
            j = (i + 1) % n
            if len(a) == 1:
                bm.faces.new((a[0], b[j], b[i]))
            elif len(b) == 1:
                bm.faces.new((a[i], a[j], b[0]))
            else:
                bm.faces.new((a[i], a[j], b[j], b[i]))
    return place(from_bm(name, bm, key, smooth, normals=False), c, rot, scale)


def torus(name, c, R, a, b, key, n=56, m=18, rot=(0, 0, 0), scale=(1, 1, 1)):
    """Torus of ring radius R and an elliptic section (a across, b up)."""
    bm = bmesh.new()
    vs = []
    for i in range(n):
        t = 2 * math.pi * i / n
        for j in range(m):
            s = 2 * math.pi * j / m
            rr = R + a * math.cos(s)
            vs.append(bm.verts.new((rr * math.cos(t), rr * math.sin(t), b * math.sin(s))))
    for i in range(n):
        for j in range(m):
            bm.faces.new((vs[i * m + j], vs[((i + 1) % n) * m + j],
                          vs[((i + 1) % n) * m + (j + 1) % m], vs[i * m + (j + 1) % m]))
    return place(from_bm(name, bm, key), c, rot, scale)


def box(name, x0, y0, z0, x1, y1, z1, key, bevel=0.0, seg=3):
    bm = bmesh.new()
    bmesh.ops.create_cube(bm, size=1.0)
    for v in bm.verts:
        v.co = V((x0 if v.co.x < 0 else x1, y0 if v.co.y < 0 else y1, z0 if v.co.z < 0 else z1))
    ob = from_bm(name, bm, key, smooth=bevel > 0)
    if bevel > 0:
        md = ob.modifiers.new('bv', 'BEVEL')
        md.width = bevel
        md.segments = seg
        md.limit_method = 'ANGLE'
        ob.data.use_auto_smooth = True
        ob.data.auto_smooth_angle = math.radians(35)
    return ob


def rbox(name, c, size, key, bevel, rot=(0, 0, 0), seg=3):
    """A box centred on c (size = full extents), rotated about c."""
    sx, sy, sz = size
    ob = box(name, -sx / 2, -sy / 2, -sz / 2, sx / 2, sy / 2, sz / 2, key, bevel, seg)
    return place(ob, c, rot)


def slab(name, p0, u, v, t, key, bevel=0.0):
    """Parallelepiped p0, p0+u, p0+v, thickness t along u x v."""
    p0, u, v = V(p0), V(u), V(v)
    nrm = u.cross(v).normalized() * t
    bm = bmesh.new()
    q = [bm.verts.new(p) for p in (p0, p0 + u, p0 + u + v, p0 + v)]
    r = [bm.verts.new(p + nrm) for p in (p0, p0 + u, p0 + u + v, p0 + v)]
    bm.faces.new(q[::-1])
    bm.faces.new(r)
    for i in range(4):
        j = (i + 1) % 4
        bm.faces.new((q[i], q[j], r[j], r[i]))
    ob = from_bm(name, bm, key, smooth=bevel > 0)
    if bevel > 0:
        md = ob.modifiers.new('bv', 'BEVEL')
        md.width = bevel
        md.segments = 2
        md.limit_method = 'ANGLE'
        ob.data.use_auto_smooth = True
    return ob


def prism_xz(name, pts, y0, y1, key, bevel=0.0):
    """A flat shape drawn in the wall plane (x, z), from y0 (front) to y1."""
    bm = bmesh.new()
    f = [bm.verts.new((x, y0, z)) for x, z in pts]
    b = [bm.verts.new((x, y1, z)) for x, z in pts]
    bm.faces.new(f)
    bm.faces.new(b[::-1])
    n = len(pts)
    for i in range(n):
        j = (i + 1) % n
        bm.faces.new((f[i], b[i], b[j], f[j]))
    ob = from_bm(name, bm, key, smooth=False)
    if bevel > 0:
        md = ob.modifiers.new('bv', 'BEVEL')
        md.width = bevel
        md.segments = 2
        md.limit_method = 'ANGLE'
        ob.data.use_auto_smooth = True
    return ob


def prism_xy(name, pts, z0, z1, key):
    """A flat shape on the floor (x, y), from z0 up to z1."""
    bm = bmesh.new()
    lo = [bm.verts.new((x, y, z0)) for x, y in pts]
    hi = [bm.verts.new((x, y, z1)) for x, y in pts]
    bm.faces.new(lo[::-1])
    bm.faces.new(hi)
    n = len(pts)
    for i in range(n):
        j = (i + 1) % n
        bm.faces.new((lo[i], lo[j], hi[j], hi[i]))
    return from_bm(name, bm, key, smooth=False)


def decal(name, pts, p0, e1, e2, n, t0, t1, key):
    """A flat shape drawn in the plane p0 + s*e1 + t*e2, from t0 to t1 along n."""
    p0, e1, e2, n = V(p0), V(e1), V(e2), V(n)
    bm = bmesh.new()
    lo = [bm.verts.new(p0 + e1 * a + e2 * b + n * t0) for a, b in pts]
    hi = [bm.verts.new(p0 + e1 * a + e2 * b + n * t1) for a, b in pts]
    bm.faces.new(lo[::-1])
    bm.faces.new(hi)
    k = len(pts)
    for i in range(k):
        j = (i + 1) % k
        bm.faces.new((lo[i], lo[j], hi[j], hi[i]))
    return from_bm(name, bm, key, smooth=False)


def ring_xz(name, inner, outer, y0, y1, key, closed=True, bevel=0.006):
    """A frame between two outlines with the same number of points."""
    bm = bmesh.new()
    fi = [bm.verts.new((x, y0, z)) for x, z in inner]
    fo = [bm.verts.new((x, y0, z)) for x, z in outer]
    bi = [bm.verts.new((x, y1, z)) for x, z in inner]
    bo = [bm.verts.new((x, y1, z)) for x, z in outer]
    n = len(inner)
    for i in range(n if closed else n - 1):
        j = (i + 1) % n
        bm.faces.new((fi[i], fi[j], fo[j], fo[i]))
        bm.faces.new((bo[i], bo[j], bi[j], bi[i]))
        bm.faces.new((fo[i], fo[j], bo[j], bo[i]))
        bm.faces.new((bi[i], bi[j], fi[j], fi[i]))
    if not closed:
        for k in (0, n - 1):
            bm.faces.new((fi[k], fo[k], bo[k], bi[k]))
    ob = from_bm(name, bm, key, smooth=False)
    if bevel > 0:
        md = ob.modifiers.new('bv', 'BEVEL')
        md.width = bevel
        md.segments = 2
        md.limit_method = 'ANGLE'
        ob.data.use_auto_smooth = True
    return ob


def rod(name, p0, p1, r, key, seg=16, r1=None):
    p0, p1 = V(p0), V(p1)
    d = p1 - p0
    bm = bmesh.new()
    bmesh.ops.create_cone(bm, cap_ends=True, segments=seg, radius1=r, radius2=r if r1 is None else r1, depth=d.length)
    ob = from_bm(name, bm, key)
    ob.matrix_world = Matrix.Translation((p0 + p1) / 2) @ d.to_track_quat('Z', 'Y').to_matrix().to_4x4()
    return ob


def tube(name, pts, radii, key, res=10):
    """A NURBS tube turned into a mesh (strings, threads)."""
    cu = bpy.data.curves.new(name, 'CURVE')
    cu.dimensions = '3D'
    cu.bevel_depth = 1.0
    cu.bevel_resolution = 3
    cu.use_fill_caps = True
    sp = cu.splines.new('NURBS')
    sp.points.add(len(pts) - 1)
    for p, (x, y, z), r in zip(sp.points, pts, radii):
        p.co = (x, y, z, 1)
        p.radius = r
    sp.order_u = min(4, len(pts))
    sp.use_endpoint_u = True
    sp.resolution_u = res
    tmp = bpy.data.objects.new(name + '_c', cu)
    C.link(tmp)
    dg = bpy.context.evaluated_depsgraph_get()
    me = bpy.data.meshes.new_from_object(tmp.evaluated_get(dg))
    bpy.data.objects.remove(tmp, do_unlink=True)
    ob = bpy.data.objects.new(name, me)
    C.link(ob)
    for p in me.polygons:
        p.use_smooth = True
    return _keys(ob, key)


def text_xz(name, body, c, size, key, font=None, depth=0.01):
    """Letters standing in the wall plane, facing the camera, centred on c."""
    cu = bpy.data.curves.new(name, 'FONT')
    cu.body = body
    cu.size = size
    cu.extrude = depth
    cu.align_x = 'CENTER'
    cu.align_y = 'CENTER'
    if font and os.path.exists(font):
        cu.font = bpy.data.fonts.load(font)
    tmp = bpy.data.objects.new(name + '_c', cu)
    C.link(tmp)
    tmp.rotation_euler = (math.radians(90), 0, 0)
    tmp.location = c
    bpy.context.view_layer.update()
    dg = bpy.context.evaluated_depsgraph_get()
    ev = tmp.evaluated_get(dg)
    me = bpy.data.meshes.new_from_object(ev)
    me.transform(ev.matrix_world)
    bpy.data.objects.remove(tmp, do_unlink=True)
    ob = bpy.data.objects.new(name, me)
    C.link(ob)
    return _keys(ob, key)


def group(objs, name, loc, rot=(0, 0, 0)):
    """Parent objs (built around the origin) to an empty at loc."""
    root = bpy.data.objects.new(name, None)
    C.link(root)
    for o in objs:
        o.parent = root
    place(root, loc, rot)
    bpy.context.view_layer.update()
    return objs


def circle(cx, cz, r, n=24, a0=0.0):
    return [(cx + r * math.cos(a0 + 2 * math.pi * i / n), cz + r * math.sin(a0 + 2 * math.pi * i / n)) for i in range(n)]


def arch(x0, x1, zs, off=0.0, n=18):
    """Door outline: up the left side, round the arch, down the right side."""
    r, cx = (x1 - x0) / 2 + off, (x0 + x1) / 2
    pts = [(x0 - off, 0.0), (x0 - off, zs)]
    for i in range(1, n):
        a = math.pi - math.pi * i / n
        pts.append((cx + r * math.cos(a), zs + r * math.sin(a)))
    return pts + [(x1 + off, zs), (x1 + off, 0.0)]


def rect(x0, z0, x1, z1):
    return [(x0, z0), (x1, z0), (x1, z1), (x0, z1)]


def sparkle(cx, cz, r, k=0.32):
    """A four-pointed star."""
    pts = []
    for i in range(8):
        a = math.pi / 2 + math.pi * i / 4
        rr = r if i % 2 == 0 else r * k
        pts.append((cx + rr * math.cos(a), cz + rr * math.sin(a)))
    return pts


def crescent(cx, cz, r, dx, dz, r2, n=48):
    """The part of circle (cx, cz, r) outside circle (cx+dx, cz+dz, r2)."""
    step = 2 * math.pi / n
    out = [i for i in range(n)
           if math.hypot(r * math.cos(i * step) - dx, r * math.sin(i * step) - dz) > r2]
    # rotate the kept indices so they run as one contiguous arc
    for s in range(len(out)):
        if (out[s] - out[s - 1]) % n != 1:
            out = out[s:] + out[:s]
            break
    arc = [(cx + r * math.cos(i * step), cz + r * math.sin(i * step)) for i in out]
    ang = lambda p: math.atan2(p[1] - cz - dz, p[0] - cx - dx)  # noqa: E731
    a_end, a_start = ang(arc[-1]), ang(arc[0])
    span = (a_start - a_end) % (2 * math.pi)
    for sp in (span, span - 2 * math.pi):
        m = a_end + sp / 2
        if math.hypot(dx + r2 * math.cos(m), dz + r2 * math.sin(m)) < r:
            break
    inn = [(cx + dx + r2 * math.cos(a_end + sp * i / n), cz + dz + r2 * math.sin(a_end + sp * i / n)) for i in range(1, n)]
    return arc + inn


# ---------------------------------------------------------------------------
# the room
# ---------------------------------------------------------------------------

DOOR = (0.30, 1.00, 0.95)          # x0, x1, height where the arch springs (top at 1.30)
WIN = (1.90, 3.10, 0.75, 1.60)     # x0, x1, z0, z1


def build_floor():
    o = []
    WOOD('plank', (0.78, 0.52, 0.30), var=0.12)
    WOOD('slab', (0.40, 0.25, 0.15), grain=6.0)
    o.append(box('slab', X0, 0.0, -0.14, X1, WY + 0.1, -0.02, 'slab', bevel=0.012))
    rnd = random.Random(11)
    rows = 15
    rw = WY / rows
    for r in range(rows):
        y0 = r * rw
        x = X0 - rnd.uniform(0.15, 1.0)
        while x < X1:
            L = rnd.uniform(0.95, 1.6)
            xa, xb = max(x, X0), min(x + L, X1)
            if xb - xa > 0.08:
                ga = 0.0 if xa <= X0 else 0.004
                gb = 0.0 if xb >= X1 else 0.004
                o.append(box('pl%d_%.2f' % (r, x), xa + ga, y0 + 0.005, -0.04, xb - gb, y0 + rw - 0.005, 0.0,
                             'plank', bevel=0.005, seg=2))
            x += L
    return o


def build_wall():
    """The back wall with a doorway cut through it (the door panel is its own
    sprite), and the night window. The rug is baked into the room too."""
    o = []
    WALLPAPER('paper')
    STRIPES('bead', 'X', 0.075, [(0.0, (0.97, 0.95, 0.90)), (0.9, (0.83, 0.80, 0.76))], rough=0.5, spec=0.35)
    P('trim', (0.98, 0.96, 0.92), rough=0.45, spec=0.4, cc=0.2)
    P('brass', (1.0, 0.74, 0.30), rough=0.25, metal=1.0)
    WOOD('walltop', (0.22, 0.12, 0.07), scale=3.0)
    x0, x1, zs = DOOR
    wl, wr = 0.15, 1.15                                  # the wall piece round the doorway
    o.append(box('wall0', X0, WY, 0.0, wl, WY + 0.15, 2.0, 'paper'))
    o.append(box('wall1', wr, WY, 0.0, X1, WY + 0.15, 2.0, 'paper'))
    inner, outer = door_ring(x0, x1, zs, wl, wr, 2.0)
    o.append(ring_xz('walldoor', inner, outer, WY, WY + 0.15, 'paper', closed=False, bevel=0.0))
    o.append(box('walltop', X0 - 0.005, WY - 0.03, 2.0, X1 + 0.005, WY + 0.17, 2.07, 'walltop', bevel=0.012))
    o.append(box('crown', X0, WY - 0.025, 1.93, X1, WY, 2.0, 'trim', bevel=0.008))
    dl, dr = x0 - 0.075, x1 + 0.075       # the door casing's outer edges
    for i, (a, b) in enumerate(((X0, dl), (dr, X1))):
        o.append(box('wain%d' % i, a, WY - 0.012, 0.0, b, WY, 0.56, 'bead'))
        o.append(box('rail%d' % i, a, WY - 0.035, 0.55, b, WY, 0.61, 'trim', bevel=0.01))
        o.append(box('base%d' % i, a, WY - 0.03, 0.0, b, WY, 0.09, 'trim', bevel=0.008))
    o.append(ring_xz('doorcase', arch(x0, x1, zs, 0.0), arch(x0, x1, zs, 0.075), WY - 0.045, WY, 'trim', closed=False))
    # the name plate above the door
    P('plate', (1.0, 0.97, 0.93), rough=0.4, spec=0.4, cc=0.3)
    P('platetxt', (0.80, 0.22, 0.40), rough=0.4, spec=0.4)
    cx = (x0 + x1) / 2
    o.append(prism_xz('plate', rect(cx - 0.23, 1.42, cx + 0.23, 1.60), WY - 0.03, WY, 'plate', bevel=0.012))
    o.append(text_xz('platetxt', 'Mila', (cx, WY - 0.03, 1.51), 0.16, 'platetxt',
                     '/System/Library/Fonts/Supplemental/Arial Rounded Bold.ttf', 0.008))
    o += build_hall()
    o += build_window('night') + build_window('frame')
    o += build_decor()
    return o


def door_ring(x0, x1, zs, wl, wr, top, n=18):
    """Outlines for the wall round an arched doorway: the doorway (inner) and,
    point for point, the rectangle wl..wr x 0..top (outer), projected from the
    arch's centre so the band has no gaps (the corners are sampled too)."""
    cx, r = (x0 + x1) / 2, (x1 - x0) / 2
    angs = sorted(set([math.pi - math.pi * i / n for i in range(n + 1)] +
                      [math.atan2(top - zs, wl - cx), math.atan2(top - zs, wr - cx)]), reverse=True)
    inner, outer = [(x0, 0.0)], [(wl, 0.0)]
    for a in angs:
        c, s_ = math.cos(a), math.sin(a)
        ts = []
        if c < -1e-9:
            ts.append((wl - cx) / c)
        if c > 1e-9:
            ts.append((wr - cx) / c)
        if s_ > 1e-9:
            ts.append((top - zs) / s_)
        t = min(ts)
        inner.append((cx + r * c, zs + r * s_))
        outer.append((cx + t * c, zs + t * s_))
    inner.append((x1, 0.0))
    outer.append((wr, 0.0))
    return inner, outer


def build_hall():
    """What the open door shows: a little entry hall behind the wall (a
    doormat, a dim back wall, a warm sconce). Works by day and by night."""
    x0, x1, zs = DOOR
    WOOD('hallfloor', (0.55, 0.36, 0.22), var=0.06)
    P('hallwall', (0.62, 0.50, 0.56), rough=0.9, spec=0.2)
    P('hallbase', (0.80, 0.74, 0.70), rough=0.6)
    P('doormat', (0.86, 0.46, 0.40), rough=1.0, sheen=1.0, bump=0.3, bscale=150)
    P('doormat2', (0.98, 0.82, 0.55), rough=1.0, sheen=1.0)
    EMIT('sconce', (1.0, 0.84, 0.55), 1.3)
    hb = WY + 0.6
    o = [box('thresh', x0 - 0.02, WY - 0.01, -0.02, x1 + 0.02, WY + 0.15, 0.012, 'walltop', bevel=0.006)]
    for k in range(4):
        o.append(box('hfl%d' % k, 0.12, WY + 0.15 + k * 0.12 + 0.003, -0.04, 1.18, WY + 0.15 + (k + 1) * 0.12 - 0.003, 0.0,
                     'hallfloor', bevel=0.004, seg=2))
    o.append(box('hallw', 0.12, hb, 0.0, 1.18, hb + 0.05, 1.0, 'hallwall'))
    o.append(box('hallb', 0.12, hb - 0.02, 0.0, 1.18, hb, 0.08, 'hallbase'))
    # a doormat just behind the threshold, with a paw
    mat = [(0.65 + 0.26 * math.cos(2 * math.pi * i / 40), WY + 0.33 + 0.12 * math.sin(2 * math.pi * i / 40)) for i in range(40)]
    o.append(prism_xy('dmat', mat, 0.0, 0.01, 'doormat'))
    pad = [(0.65 + 0.045 * math.cos(2 * math.pi * i / 16), WY + 0.31 + 0.035 * math.sin(2 * math.pi * i / 16)) for i in range(16)]
    o.append(prism_xy('dmatp', pad, 0.01, 0.013, 'doormat2'))
    for k, dx in enumerate((-0.06, -0.02, 0.02, 0.06)):
        dy = 0.045 if abs(dx) < 0.03 else 0.03
        toe = [(0.65 + dx + 0.016 * math.cos(2 * math.pi * i / 10), WY + 0.31 + dy + 0.016 * math.sin(2 * math.pi * i / 10))
               for i in range(10)]
        o.append(prism_xy('dmatt%d' % k, toe, 0.01, 0.013, 'doormat2'))
    o.append(prism_xz('sconce', circle(0.65, 0.55, 0.06, 20), hb - 0.012, hb, 'sconce'))
    return o


def build_door(angle=0.0):
    """The door panel, hinged on its left edge, swinging `angle` degrees into
    the hall. Its round window is frosted glass (a warm glow from the hall)."""
    x0, x1, zs = DOOR
    P('door', (0.40, 0.74, 0.66), rough=0.45, spec=0.35, cc=0.25)
    P('doorin', (0.33, 0.64, 0.57), rough=0.5, spec=0.35, cc=0.25)
    P('doorback', (0.36, 0.66, 0.59), rough=0.5, spec=0.3)
    P('brass', (1.0, 0.74, 0.30), rough=0.25, metal=1.0)
    P('trim', (0.98, 0.96, 0.92), rough=0.45, spec=0.4, cc=0.2)
    EMIT('frost', (1.0, 0.90, 0.72), 1.0)
    P('frostline', (1.0, 0.97, 0.92), rough=0.3, spec=0.5)
    o = [prism_xz('door', arch(x0, x1, zs, -0.015), WY - 0.02, WY + 0.005, ['door'], bevel=0.005)]
    for i, (a, b) in enumerate(((x0 + 0.08, (x0 + x1) / 2 - 0.03), ((x0 + x1) / 2 + 0.03, x1 - 0.08))):
        o.append(prism_xz('dpan%d' % i, rect(a, 0.10, b, 0.62), WY - 0.028, WY - 0.02, 'doorin', bevel=0.008))
    cx, cz = (x0 + x1) / 2, 1.00
    o.append(prism_xz('dwin', circle(cx, cz, 0.13, 32), WY - 0.024, WY - 0.02, 'frost'))
    o.append(prism_xz('dwinx', rect(cx - 0.008, cz - 0.13, cx + 0.008, cz + 0.13), WY - 0.027, WY - 0.024, 'frostline'))
    o.append(prism_xz('dwiny', rect(cx - 0.13, cz - 0.008, cx + 0.13, cz + 0.008), WY - 0.027, WY - 0.024, 'frostline'))
    o.append(ring_xz('dwinr', circle(cx, cz, 0.13, 32), circle(cx, cz, 0.17, 32), WY - 0.04, WY - 0.02, 'trim'))
    for sy in (-1, 1):       # a knob on each side
        yk = WY - 0.05 if sy < 0 else WY + 0.05
        o.append(ellipsoid('knob%d' % sy, (x1 - 0.09, yk, 0.62), (0.034, 0.03, 0.034), 'brass'))
    o.append(prism_xz('knobr', circle(x1 - 0.09, 0.62, 0.045, 20), WY - 0.026, WY - 0.02, 'brass'))
    if angle:
        h = V((x0 + 0.015, WY + 0.005, 0.0))
        M = Matrix.Translation(h) @ Matrix.Rotation(math.radians(angle), 4, 'Z') @ Matrix.Translation(-h)
        bpy.context.view_layer.update()
        for ob in o:
            ob.matrix_world = M @ ob.matrix_world
    return o


def build_window(part):
    """part: 'night' or 'day' (what is seen through the glass, and the glass's
    glints) or 'frame' (casing, mullions, sill, curtains)."""
    x0, x1, z0, z1 = WIN
    o = []
    if part == 'frame':
        return build_window_frame()
    day = part == 'day'
    if day:
        SKY('sky_d', z0, z1, [(0.0, (0.62, 0.84, 1.0)), (1.0, (0.30, 0.60, 0.98))], 1.0)
        sky = 'sky_d'
    else:
        SKY('sky_n', z0, z1, [(0.0, (0.20, 0.20, 0.55)), (0.45, (0.09, 0.11, 0.36)), (1.0, (0.04, 0.05, 0.20))], 1.0)
        sky = 'sky_n'
    GLINT('glint')
    o.append(prism_xz('sky', rect(x0, z0, x1, z1), WY - 0.02, WY - 0.005, sky))
    fy = WY - 0.022      # the front of the sky pane: things outside sit on it
    if day:
        EMIT('sun', (1.0, 0.86, 0.35), 1.4)
        EMIT('cloud', (1.0, 1.0, 1.0), 1.0)
        EMIT('hill', (0.35, 0.72, 0.36), 1.0)
        EMIT('hill2', (0.25, 0.60, 0.30), 1.0)
        EMIT('roof_d', (0.85, 0.38, 0.28), 1.0)
        EMIT('house_d', (0.98, 0.92, 0.80), 1.0)
        o.append(prism_xz('sun', circle(2.83, 1.40, 0.095, 32), fy - 0.002, fy, 'sun'))
        for k, (cx, cz, s) in enumerate(((2.12, 1.43, 1.0), (2.60, 1.25, 0.8))):
            for j, (dx, dz, r) in enumerate(((0, 0, 0.055), (0.06, 0.02, 0.07), (0.13, 0, 0.05), (0.065, -0.02, 0.05))):
                o.append(prism_xz('cl%d%d' % (k, j), circle(cx + dx * s, cz + dz * s, r * s, 20), fy - 0.004, fy - 0.001, 'cloud'))
        hill = [(x0, z0)] + [(x0 + (x1 - x0) * i / 30, z0 + 0.10 + 0.05 * math.sin(i / 30 * 5.0 + 1.0)) for i in range(31)] + [(x1, z0)]
        o.append(prism_xz('hill', hill, fy - 0.003, fy, 'hill2'))
        roofs, walls = 'roof_d', 'house_d'
    else:
        EMIT('moon', (1.0, 0.93, 0.62), 1.25)
        EMIT('halo', (0.22, 0.24, 0.55), 1.0)
        EMIT('star', (1.0, 0.95, 0.70), 1.5)
        EMIT('house_n', (0.05, 0.06, 0.17), 1.0)
        EMIT('lit', (1.0, 0.78, 0.35), 1.3)
        o.append(prism_xz('halo', circle(2.84, 1.38, 0.15, 36), fy - 0.001, fy, 'halo'))
        o.append(prism_xz('moon', crescent(2.84, 1.38, 0.105, 0.05, 0.035, 0.09), fy - 0.004, fy - 0.001, 'moon'))
        rnd = random.Random(4)
        spots = [(2.02, 1.48, 0.034), (2.30, 1.30, 0.026), (2.62, 1.52, 0.03), (2.14, 1.16, 0.022), (3.00, 1.10, 0.024),
                 (2.42, 1.08, 0.02)]
        for k, (sx, sz, r) in enumerate(spots):
            o.append(prism_xz('st%d' % k, sparkle(sx, sz, r), fy - 0.004, fy - 0.001, 'star'))
        for k in range(10):
            sx, sz = rnd.uniform(x0 + 0.05, x1 - 0.05), rnd.uniform(1.0, z1 - 0.04)
            if math.hypot(sx - 2.84, sz - 1.38) < 0.2:
                continue
            o.append(prism_xz('sd%d' % k, circle(sx, sz, 0.009, 8), fy - 0.004, fy - 0.001, 'star'))
        roofs = walls = 'house_n'
    # little houses along the bottom of the window (lit windows at night)
    hx = [(1.93, 0.16, 0.13), (2.13, 0.20, 0.18), (2.36, 0.14, 0.10), (2.55, 0.22, 0.16), (2.80, 0.15, 0.12), (2.96, 0.18, 0.15)]
    for k, (hx0, w, hgt) in enumerate(hx):
        base = z0
        o.append(prism_xz('hs%d' % k, rect(hx0, base, hx0 + w, base + hgt), fy - 0.006, fy - 0.003, walls))
        o.append(prism_xz('hr%d' % k, [(hx0 - 0.015, base + hgt), (hx0 + w + 0.015, base + hgt), (hx0 + w / 2, base + hgt + w * 0.45)],
                          fy - 0.006, fy - 0.003, roofs))
        if not day and k % 2 == 0:
            o.append(prism_xz('hl%d' % k, rect(hx0 + w * 0.35, base + hgt * 0.35, hx0 + w * 0.65, base + hgt * 0.7),
                              fy - 0.008, fy - 0.006, 'lit'))
        if day:
            o.append(prism_xz('hw%d' % k, rect(hx0 + w * 0.35, base + hgt * 0.35, hx0 + w * 0.65, base + hgt * 0.7),
                              fy - 0.008, fy - 0.006, 'sky_d'))
    # glass glints
    for k, gx in enumerate((x0 + 0.08, (x0 + x1) / 2 + 0.08)):
        o.append(prism_xz('gl%d' % k, [(gx, z1 - 0.06), (gx + 0.10, z1 - 0.06), (gx + 0.02, z1 - 0.30), (gx - 0.08, z1 - 0.30)],
                          fy - 0.012, fy - 0.010, 'glint'))
    return o


def build_window_frame():
    x0, x1, z0, z1 = WIN
    o = []
    m = 0.06
    o.append(ring_xz('wincase', rect(x0, z0, x1, z1), rect(x0 - m, z0 - m, x1 + m, z1 + m), WY - 0.05, WY, 'trim'))
    cx, cz = (x0 + x1) / 2, (z0 + z1) / 2
    o.append(box('mulv', cx - 0.018, WY - 0.04, z0, cx + 0.018, WY - 0.01, z1, 'trim', bevel=0.006))
    o.append(box('mulh', x0, WY - 0.04, cz - 0.018, x1, WY - 0.01, cz + 0.018, 'trim', bevel=0.006))
    o.append(box('sill', x0 - 0.12, WY - 0.2, z0 - 0.08, x1 + 0.12, WY, z0 - 0.02, 'trim', bevel=0.015))
    o += build_curtains()
    return o


def curtain(name, xo, s, W, ztop, zbot, key, keyb, n_pleat=5):
    """Pleated curtain from the rod, gathered by a tie-back. xo: outer edge x;
    s: +1 if it spreads to the right."""
    nu, nv = 36, 30
    vt = 0.42

    def width(v):
        if v < vt:
            t = v / vt
            return 1.0 - 0.52 * (t * t * (3 - 2 * t))
        t = (v - vt) / (1 - vt)
        return 0.48 + 0.26 * (t * t * (3 - 2 * t))
    bm = bmesh.new()
    vs = []
    for j in range(nv + 1):
        v = j / nv
        z = ztop - v * (ztop - zbot)
        w = width(v)
        amp = 0.022 / max(0.4, w) * (1.0 if j else 0.6)
        for i in range(nu + 1):
            u = i / nu
            x = xo + s * u * W * w
            y = WY - 0.10 - amp * (0.5 + 0.5 * math.cos(2 * math.pi * u * n_pleat))
            vs.append(bm.verts.new((x, y, z)))
    for j in range(nv):
        for i in range(nu):
            a = j * (nu + 1) + i
            bm.faces.new((vs[a], vs[a + 1], vs[a + nu + 2], vs[a + nu + 1]))
    ob = from_bm(name, bm, key)
    sol = ob.modifiers.new('sol', 'SOLIDIFY')
    sol.thickness = 0.012
    zt = ztop - vt * (ztop - zbot)
    tb = ellipsoid(name + 'tie', (xo + s * W * 0.48 * 0.5, WY - 0.115, zt), (W * 0.48 * 0.58, 0.05, 0.028), keyb)
    return [ob, tb]


def build_curtains():
    x0, x1, z0, z1 = WIN
    P('curtain', (0.96, 0.56, 0.66), rough=0.9, sheen=1.0, spec=0.2, bump=0.1, bscale=150)
    P('tie', (0.85, 0.36, 0.50), rough=0.7, sheen=0.8, spec=0.3)
    zr = z1 + 0.11
    o = []
    o += curtain('curl', x0 - 0.30, 1, 0.36, zr - 0.01, z0 - 0.16, 'curtain', 'tie')
    o += curtain('curr', x1 + 0.30, -1, 0.36, zr - 0.01, z0 - 0.16, 'curtain', 'tie')
    o.append(rod('rodbar', (x0 - 0.34, WY - 0.10, zr), (x1 + 0.27, WY - 0.10, zr), 0.016, 'brass'))
    for xx in (x0 - 0.34, x1 + 0.27):
        o.append(ellipsoid('fin%.2f' % xx, (xx, WY - 0.10, zr), (0.032, 0.032, 0.032), 'brass'))
        o.append(rod('brk%.2f' % xx, (xx + (0.05 if xx < 2 else -0.05), WY, zr), (xx + (0.05 if xx < 2 else -0.05), WY - 0.1, zr),
                     0.01, 'brass'))
    return o


def build_decor():
    """A shelf with a plant and books, and a portrait of Mila, between the door
    and the window (below the HUD band)."""
    o = []
    P('pot', (0.86, 0.45, 0.30), rough=0.6, spec=0.3)
    P('soil', (0.22, 0.13, 0.08), rough=1.0)
    P('leaf', (0.30, 0.68, 0.36), rough=0.5, spec=0.4)
    P('leaf2', (0.42, 0.78, 0.42), rough=0.5, spec=0.4)
    sx0, sx1, sz = 1.13, 1.57, 0.86
    o.append(box('shelf', sx0, WY - 0.19, sz, sx1, WY, sz + 0.04, 'trim', bevel=0.01))
    for bx in (sx0 + 0.07, sx1 - 0.09):
        o.append(slab('brk%.2f' % bx, (bx, WY, sz), (0, 0, -0.10), (0, -0.14, 0), 0.02, 'trim'))
    # a potted plant with trailing leaves
    px = sx0 + 0.11
    o.append(lathe('spot', [(0, sz + 0.04), (0.055, sz + 0.04), (0.07, sz + 0.14), (0.078, sz + 0.145), (0.078, sz + 0.16),
                            (0.066, sz + 0.16), (0, sz + 0.15)], 'pot', c=(px, WY - 0.1, 0)))
    rnd = random.Random(8)
    for k in range(9):
        a = rnd.uniform(0, 2 * math.pi)
        r = rnd.uniform(0.02, 0.06)
        o.append(ellipsoid('sl%d' % k, (px + r * math.cos(a), WY - 0.1 + r * math.sin(a) * 0.7, sz + 0.19 + rnd.uniform(-0.02, 0.05)),
                           (0.05, 0.022, 0.03), 'leaf' if k % 2 else 'leaf2', rot=(0, rnd.uniform(-40, 40), math.degrees(a))))
    # books
    cols = [(0.35, 0.55, 0.92), (0.98, 0.78, 0.30), (0.62, 0.45, 0.88)]
    xx = sx1 - 0.19
    for k, (w, h) in enumerate(((0.04, 0.17), (0.035, 0.15), (0.045, 0.18))):
        P('book%d' % k, cols[k], rough=0.6)
        o.append(box('book%d' % k, xx, WY - 0.15, sz + 0.04, xx + w, WY - 0.03, sz + 0.04 + h, 'book%d' % k, bevel=0.004))
        xx += w + 0.004
    # Mila's portrait
    P('frame', (0.95, 0.58, 0.68), rough=0.45, spec=0.4, cc=0.3)
    P('mat', (1.0, 0.93, 0.86), rough=0.9)
    P('catblk', (0.04, 0.04, 0.05), rough=0.6)
    EMIT('cateye', (1.0, 0.78, 0.10), 1.2)
    fx0, fx1, fz0, fz1 = 1.16, 1.54, 1.06, 1.46
    o.append(ring_xz('pframe', rect(fx0, fz0, fx1, fz1), rect(fx0 - 0.04, fz0 - 0.04, fx1 + 0.04, fz1 + 0.04), WY - 0.04, WY, 'frame'))
    o.append(prism_xz('pmat', rect(fx0, fz0, fx1, fz1), WY - 0.02, WY - 0.005, 'mat'))
    cx, cz = (fx0 + fx1) / 2, 1.23
    head = circle(cx, cz, 0.115, 28)
    o.append(prism_xz('phead', head, WY - 0.025, WY - 0.02, 'catblk'))
    o.append(prism_xz('pbody', [(cx - 0.12, fz0), (cx + 0.12, fz0), (cx + 0.09, cz - 0.06), (cx - 0.09, cz - 0.06)],
                      WY - 0.024, WY - 0.02, 'catblk'))
    for s in (-1, 1):
        o.append(prism_xz('pear%d' % s, [(cx + s * 0.035, cz + 0.09), (cx + s * 0.11, cz + 0.05), (cx + s * 0.10, cz + 0.175)],
                          WY - 0.025, WY - 0.02, 'catblk'))
        o.append(prism_xz('peye%d' % s, circle(cx + s * 0.045, cz + 0.005, 0.028, 16), WY - 0.028, WY - 0.025, 'cateye'))
        o.append(prism_xz('ppup%d' % s, [(cx + s * 0.045 - 0.006, cz + 0.005), (cx + s * 0.045, cz + 0.03),
                                         (cx + s * 0.045 + 0.006, cz + 0.005), (cx + s * 0.045, cz - 0.02)],
                          WY - 0.030, WY - 0.028, 'catblk'))
    P('heart', (0.95, 0.35, 0.50), rough=0.5)
    hx, hz = fx1 - 0.06, fz1 - 0.06
    heart = [(hx + 0.028 * 16 * math.sin(t) ** 3 / 16, hz + 0.028 * (13 * math.cos(t) - 5 * math.cos(2 * t) - 2 * math.cos(3 * t)
                                                               - math.cos(4 * t)) / 16) for t in [2 * math.pi * i / 32 for i in range(32)]]
    o.append(prism_xz('pheart', heart, WY - 0.024, WY - 0.02, 'heart'))
    return o


# ---------------------------------------------------------------------------
# the fixtures: rug, bed, bowls, gift
# ---------------------------------------------------------------------------

RUG_C, RUG_R = (1.7, 1.35), (1.28, 0.90)
BED_C = (0.6, 2.3)
BOWLS_C = (0.45, 0.45)
GIFT_C = (1.3, 2.05)


def build_rug():
    rx, ry = RUG_R
    RUG('rug', rx, ry, [(0.0, (0.84, 0.91, 0.98)), (0.40, (0.97, 0.97, 0.99)), (0.52, (0.62, 0.80, 0.94)),
                        (0.66, (0.97, 0.97, 0.99)), (0.78, (0.52, 0.72, 0.92)), (0.90, (0.40, 0.60, 0.86))])
    nr, na = 40, 128
    bm = bmesh.new()
    centre = bm.verts.new((0, 0, 0.011))
    rows = []
    for i in range(1, nr + 1):
        s = i / nr
        z = 0.009 + 0.003 * abs(math.sin(math.pi * s * 7.5))
        if s > 0.96:
            z *= (1 - s) / 0.04 * 0.7 + 0.3
        rows.append([bm.verts.new((rx * s * math.cos(2 * math.pi * k / na), ry * s * math.sin(2 * math.pi * k / na), z))
                     for k in range(na)])
    for k in range(na):
        bm.faces.new((centre, rows[0][k], rows[0][(k + 1) % na]))
    for a, b in zip(rows, rows[1:]):
        for k in range(na):
            j = (k + 1) % na
            bm.faces.new((a[k], b[k], b[j], a[j]))
    edge = rows[-1]
    lo = [bm.verts.new((v.co.x, v.co.y, 0.0)) for v in edge]
    for k in range(na):
        j = (k + 1) % na
        bm.faces.new((edge[k], lo[k], lo[j], edge[j]))
    bm.faces.new(lo[::-1])
    ob = from_bm('rug', bm, 'rug')
    ob.location = (RUG_C[0], RUG_C[1], 0)
    return [ob]


def build_bed():
    P('bed', (0.96, 0.58, 0.72), rough=0.9, sheen=1.0, spec=0.2, bump=0.25, bscale=40, bdist=0.01)
    P('bedin', (1.0, 0.93, 0.86), rough=0.95, sheen=1.0, spec=0.15, bump=0.2, bscale=50)
    P('bedpip', (0.99, 0.90, 0.93), rough=0.8, sheen=1.0, spec=0.2)
    cx, cy = BED_C
    o = [torus('bedrim', (cx, cy, 0.14), 0.48, 0.12, 0.14, 'bed')]
    # the cushion: its top is the floor of the bed, z 0.08
    o.append(lathe('bedin', [(0, 0.0), (0.42, 0.0), (0.44, 0.03), (0.42, 0.07), (0.36, 0.08), (0, 0.08)], 'bedin',
                   c=(cx, cy, 0), n=64))
    # piping round the top of the rim
    o.append(torus('bedpip', (cx, cy, 0.265), 0.49, 0.018, 0.018, 'bedpip'))
    # quilting tufts on the cushion
    for k in range(6):
        a = 2 * math.pi * k / 6 + 0.3
        o.append(ellipsoid('tuft%d' % k, (cx + 0.2 * math.cos(a), cy + 0.2 * math.sin(a), 0.078), (0.012, 0.012, 0.006), 'bedpip'))
    return o


def build_bowls():
    P('bowl', (0.36, 0.60, 0.95), rough=0.25, spec=0.5, cc=0.6)
    P('bowlw', (0.97, 0.97, 0.98), rough=0.25, spec=0.5, cc=0.6)
    P('bowlrim', (0.36, 0.60, 0.95), rough=0.25, spec=0.5, cc=0.6)
    P('water', (0.45, 0.75, 0.98), rough=0.03, spec=1.0, cc=1.0)
    P('kibble', (0.62, 0.36, 0.16), rough=0.7)
    P('kibble2', (0.78, 0.50, 0.24), rough=0.7)
    P('fishmat', (0.99, 0.86, 0.42), rough=0.85, sheen=0.5)
    P('fishmat2', (0.96, 0.66, 0.30), rough=0.85)
    P('paw', (1.0, 1.0, 1.0), rough=0.4)
    o = []
    # a fish-shaped mat under both bowls
    mx, my = 0.66, 0.43
    body = [(mx + 0.42 * math.cos(2 * math.pi * i / 48), my + 0.25 * math.sin(2 * math.pi * i / 48)) for i in range(48)]
    o.append(prism_xy('mat', body, 0.0, 0.007, 'fishmat'))
    o.append(prism_xy('mattail', [(mx + 0.36, my), (mx + 0.58, my + 0.19), (mx + 0.53, my), (mx + 0.58, my - 0.19)],
                      0.0, 0.006, 'fishmat'))
    o.append(prism_xy('mateye', [(mx - 0.30 + 0.035 * math.cos(2 * math.pi * i / 16), my + 0.08 + 0.035 * math.sin(2 * math.pi * i / 16))
                                 for i in range(16)], 0.006, 0.009, 'fishmat2'))
    for k in range(3):
        xx = mx + 0.2 + k * 0.05
        o.append(prism_xy('matfin%d' % k, [(xx, my - 0.14), (xx + 0.018, my - 0.14), (xx + 0.018, my + 0.14), (xx, my + 0.14)],
                          0.006, 0.0085, 'fishmat2'))

    def bowl(name, c, key, r=0.17):
        k = r / 0.17
        prof = [(0, 0.007), (0.12 * k, 0.007), (0.138 * k, 0.017), (0.162 * k, 0.085), (0.172 * k, 0.1), (0.166 * k, 0.108),
                (0.152 * k, 0.104), (0.14 * k, 0.09), (0.112 * k, 0.03), (0, 0.03)]
        return lathe(name, prof, key, c=(c[0], c[1], 0), n=48)
    fx, fy = BOWLS_C
    o.append(bowl('bowlf', (fx, fy), 'bowl'))
    o.append(ellipsoid('kibmound', (fx, fy, 0.075), (0.125, 0.125, 0.03), 'kibble'))
    rnd = random.Random(3)
    for k in range(26):
        a = rnd.uniform(0, 2 * math.pi)
        r = math.sqrt(rnd.uniform(0, 1)) * 0.11
        z = 0.075 + 0.03 * math.sqrt(max(0, 1 - (r / 0.125) ** 2)) + 0.004
        o.append(ellipsoid('kib%d' % k, (fx + r * math.cos(a), fy + r * math.sin(a), z), (0.022, 0.017, 0.012),
                           'kibble2' if k % 3 else 'kibble', rot=(rnd.uniform(-20, 20), rnd.uniform(-20, 20), rnd.uniform(0, 180))))
    # a white paw print on the front of the food bowl
    py = fy - 0.155
    o.append(ellipsoid('pawpad', (fx, py, 0.048), (0.026, 0.006, 0.019), 'paw', rot=(-16, 0, 0)))
    for k, (dx, dz) in enumerate(((-0.03, 0.074), (-0.011, 0.083), (0.011, 0.083), (0.03, 0.074))):
        o.append(ellipsoid('pawtoe%d' % k, (fx + dx, py + 0.003 + (0.002 if abs(dx) > 0.02 else 0), dz), (0.009, 0.005, 0.009),
                           'paw', rot=(-16, 0, 0)))
    wx, wy = 0.9, 0.4
    o.append(bowl('bowlw', (wx, wy), 'bowlw', 0.16))
    o.append(torus('bowlwrim', (wx, wy, 0.1), 0.158, 0.009, 0.009, 'bowlrim'))
    o.append(lathe('water', [(0, 0.072), (0.14, 0.072), (0.14, 0.074), (0, 0.074)], 'water', c=(wx, wy, 0), n=48))
    return o


GIFT_STAGES = {
    # stage: body squash (xy, z), lid (lift, dx, dy, tilt about y, tilt about x) or 'floor', coins, sparkles
    'closed': ((1.0, 1.0), (0.0, 0, 0, 0, 0), 0, 0),
    0: ((1.05, 0.90), (0.03, 0, 0, 6, 0), 0, 0),
    1: ((0.98, 1.06), (0.15, 0.02, 0.03, 14, -12), 2, 2),
    2: ((1.0, 1.0), (0.27, 0.12, 0.04, 38, -20), 3, 4),
    3: ((1.0, 1.0), 'floor', 3, 3),
}


def build_gift(stage='closed'):
    """The daily parcel on the floor by the bed: a lid with a bow, and when
    it opens, tissue paper, coins and sparkles."""
    P('giftp', (0.96, 0.40, 0.50), rough=0.45, spec=0.45, cc=0.3)
    P('giftr', (1.0, 0.84, 0.32), rough=0.35, spec=0.5, cc=0.4)
    P('tissue', (1.0, 0.90, 0.93), rough=0.9, sheen=0.8, spec=0.2)
    P('tissue2', (0.85, 0.93, 1.0), rough=0.9, sheen=0.8, spec=0.2)
    P('coin', (1.0, 0.76, 0.22), rough=0.25, metal=1.0, emit=(1.0, 0.7, 0.2), es=0.25)
    EMIT('spark', (1.0, 0.93, 0.55), 1.6)
    (sxy, sz), lid, ncoin, nspark = GIFT_STAGES[stage]
    w, d, hb, hl = 0.24, 0.21, 0.13, 0.05
    h = hb * sz
    o = [rbox('gbox', (0, 0, h / 2), (w * sxy, d * sxy, h), 'giftp', 0.01)]
    o.append(rbox('grx', (0, 0, h / 2), (w * sxy + 0.008, 0.045, h - 0.004), 'giftr', 0.003))
    o.append(rbox('gry', (0, 0, h / 2), (0.045, d * sxy + 0.008, h - 0.004), 'giftr', 0.003))
    # the lid, built round its own bottom centre
    lw, ld = w * sxy + 0.02, d * sxy + 0.02
    lo = [rbox('glid', (0, 0, hl / 2), (lw, ld, hl), 'giftp', 0.01)]
    lo.append(rbox('glx', (0, 0, hl / 2), (lw + 0.006, 0.045, hl + 0.006), 'giftr', 0.003))
    lo.append(rbox('gly', (0, 0, hl / 2), (0.045, ld + 0.006, hl + 0.006), 'giftr', 0.003))
    for sgn in (-1, 1):
        lo.append(torus('gbow%d' % sgn, (sgn * 0.045, 0, hl + 0.03), 0.04, 0.012, 0.012, 'giftr', rot=(90, 0, sgn * 20),
                        scale=(1.0, 0.6, 1.0)))
        lo.append(slab('gtail%d' % sgn, (0, 0, hl + 0.004), (sgn * 0.07, -0.05, 0), (0.02 * sgn, 0.02, 0), 0.004, 'giftr'))
    lo.append(ellipsoid('gknot', (0, 0, hl + 0.022), (0.022, 0.02, 0.018), 'giftr'))
    root = bpy.data.objects.new('gliddle', None)
    C.link(root)
    for ob in lo:
        ob.parent = root
    if lid == 'floor':
        # lying on the floor against the box's right side, top towards the camera
        root.location = (w / 2 + 0.07, -0.02, 0.12)
        root.rotation_euler = (math.radians(-10), math.radians(78), math.radians(10))
    else:
        lift, dx, dy, ty, tx = lid
        root.location = (dx, dy, h - 0.035 + lift)
        root.rotation_euler = (math.radians(tx), math.radians(ty), 0)
    o += lo
    if stage != 'closed' and stage >= 1:
        rnd = random.Random(5)
        for k in range(5):
            o.append(ellipsoid('gtis%d' % k, (rnd.uniform(-0.07, 0.07), rnd.uniform(-0.06, 0.06), h + 0.01),
                               (0.05, 0.04, 0.03), 'tissue' if k % 2 else 'tissue2', rot=(0, 0, rnd.uniform(0, 90))))
    coins = {1: [(0.0, 0.0, 0.07, 20), (0.05, -0.02, 0.12, -30)],
             2: [(-0.05, 0.0, 0.16, 25), (0.04, -0.03, 0.24, -20), (0.0, 0.02, 0.32, 60)],
             3: [(-0.04, -0.02, 0.045, 30), (0.03, -0.03, 0.05, -15), (0.0, 0.03, 0.06, 5)]}.get(stage, [])
    for k, (cx, cy, cz, tilt) in enumerate(coins[:ncoin]):
        c = lathe('gcoin%d' % k, [(0, -0.007), (0.042, -0.007), (0.046, 0), (0.042, 0.007), (0, 0.007)], 'coin', n=32)
        c.location = (cx, cy, h + cz)
        c.rotation_euler = (math.radians(70 if stage != 3 else 25), math.radians(tilt), 0)
        o.append(c)
    sp = {1: [(-0.10, 0.20, 0.03), (0.12, 0.16, 0.025)],
          2: [(-0.14, 0.30, 0.04), (0.13, 0.36, 0.035), (-0.05, 0.44, 0.03), (0.16, 0.18, 0.025)],
          3: [(-0.12, 0.22, 0.03), (0.10, 0.26, 0.025), (0.0, 0.34, 0.02)]}.get(stage, [])
    for k, (sx_, sz_, r) in enumerate(sp[:nspark]):
        o.append(prism_xz('gsp%d' % k, sparkle(sx_, h + sz_, r), -0.14, -0.137, 'spark'))
    group([ob for ob in o if ob.parent is None] + [root], 'gift', (GIFT_C[0], GIFT_C[1], 0), (0, 0, -18))
    return o


# ---------------------------------------------------------------------------
# toys
# ---------------------------------------------------------------------------

TOYS = {'post': (3.0, 2.4), 'box': (2.9, 0.6), 'fishbowl': (0.35, 1.5), 'mouse': (2.5, 1.8), 'yarn': (1.1, 0.95),
        'ball': (2.2, 1.0), 'tunnel': (1.6, 2.5), 'hammock': (2.4, 2.6), 'catnip': (3.15, 1.4), 'feather': (1.7, 1.35)}
HAMMOCK_BED_Z = 0.55
DOOR_ANCHOR = (0.65, 3.0, 0.0)                  # the doorway's bottom centre
DOOR_ANGLES = (0, 28, 58, 88)                  # the panel swings into the hall
WIN_ANCHOR = (2.5, 3.0, 0.75)                   # the window's bottom centre, on the wall


def build_post():
    px, py = TOYS['post']
    P('carpet', (0.66, 0.56, 0.92), rough=1.0, sheen=1.0, spec=0.15, bump=0.35, bscale=180, bdist=0.006)
    ROPE('rope', (0.88, 0.74, 0.50))
    P('pompom', (0.98, 0.42, 0.58), rough=1.0, sheen=1.0, bump=0.6, bscale=220)
    P('string', (0.96, 0.90, 0.80), rough=0.8)
    def puck(name, r, z0, z1, e=0.03):
        return lathe(name, [(0, z0), (r - e, z0), (r - e * 0.3, z0 + e * 0.3), (r, z0 + e), (r, z1 - e), (r - e * 0.3, z1 - e * 0.3),
                            (r - e, z1), (0, z1)], 'carpet', c=(px, py, 0), n=64)
    o = [puck('spbase', 0.25, 0.0, 0.075)]
    o.append(lathe('sppole', [(0, 0.06), (0.085, 0.06), (0.085, 1.0), (0, 1.0)], 'rope', c=(px, py, 0), n=40))
    o.append(puck('sptop', 0.28, 1.0, 1.1, 0.035))
    # a pompom dangling from the platform's front-left edge
    sx, sy = px - 0.18, py - 0.18
    o.append(tube('spstr', [(sx, sy, 1.0), (sx + 0.004, sy, 0.84), (sx, sy, 0.72)], [0.006, 0.006, 0.006], 'string'))
    o.append(ellipsoid('sppom', (sx, sy, 0.68), (0.055, 0.055, 0.055), 'pompom'))
    return o


def build_box():
    bx, by = TOYS['box']
    P('card', (0.80, 0.60, 0.38), rough=0.85, spec=0.25, bump=0.05, bscale=80)
    P('cardin', (0.66, 0.48, 0.29), rough=0.9, spec=0.2)
    P('tape', (0.90, 0.78, 0.55), rough=0.3, spec=0.5, cc=0.4)
    P('stencil', (0.40, 0.25, 0.14), rough=0.9)
    w, d, h, t = 0.70, 0.50, 0.40, 0.022
    x0, y0, x1, y1 = bx - w / 2, by - d / 2, bx + w / 2, by + d / 2
    o = [box('cbot', x0, y0, 0, x1, y1, 0.02, 'cardin')]
    o.append(box('cwf', x0, y0, 0, x1, y0 + t, h, ['card'], bevel=0.004, seg=2))
    o.append(box('cwb', x0, y1 - t, 0, x1, y1, h, 'card', bevel=0.004, seg=2))
    o.append(box('cwl', x0, y0, 0, x0 + t, y1, h, 'card', bevel=0.004, seg=2))
    o.append(box('cwr', x1 - t, y0, 0, x1, y1, h, 'card', bevel=0.004, seg=2))
    L = 0.24
    # flaps: front hangs out and down, back stands up and leans back, sides droop out
    a = math.radians(58)
    o.append(slab('cff', (x0, y0, h), (w, 0, 0), (0, -L * math.cos(a), -L * math.sin(a)), 0.016, 'card', bevel=0.003))
    b = math.radians(18)
    o.append(slab('cfb', (x0, y1, h), (0, L * math.sin(b), L * math.cos(b)), (w, 0, 0), 0.016, 'card', bevel=0.003))
    c = math.radians(62)
    o.append(slab('cfl', (x0, y0, h), (-L * math.cos(c), 0, -L * math.sin(c)), (0, d, 0), 0.016, 'card', bevel=0.003))
    o.append(slab('cfr', (x1, y0, h), (0, d, 0), (L * math.cos(c), 0, -L * math.sin(c)), 0.016, 'card', bevel=0.003))
    # packing tape across the back flap, a fish stencil on the front
    tb = 0.06
    o.append(slab('ctape', (bx - tb / 2, y1 + 0.003, h), (0, L * math.sin(b), L * math.cos(b)), (tb, 0, 0), 0.003, 'tape'))
    fp0 = (x0, y0, h)
    e1, e2 = (1, 0, 0), (0, -math.cos(a), -math.sin(a))
    nout = (0, -math.sin(a), math.cos(a))
    fs, ft = w / 2 - 0.03, L / 2 + 0.005
    fish = [(fs + 0.095 * math.cos(2 * math.pi * i / 24), ft + 0.05 * math.sin(2 * math.pi * i / 24)) for i in range(24)]
    o.append(decal('cfish', fish, fp0, e1, e2, nout, -0.001, 0.002, 'stencil'))
    o.append(decal('cfisht', [(fs + 0.075, ft), (fs + 0.165, ft - 0.055), (fs + 0.145, ft), (fs + 0.165, ft + 0.055)],
                   fp0, e1, e2, nout, -0.001, 0.002, 'stencil'))
    o.append(decal('cfishe', circle(fs - 0.045, ft - 0.012, 0.012, 10), fp0, e1, e2, nout, 0.0, 0.003, 'card'))
    o.append(decal('cftape', [(-0.004, -0.004), (w + 0.004, -0.004), (w + 0.004, 0.045), (-0.004, 0.045)],
                   fp0, e1, e2, nout, -0.001, 0.0025, 'tape'))
    return o


def YARN(key, col):
    """Wound thread: wave bands on a woolly colour (object space, so it turns
    with the ball)."""
    if key in C._MATS:
        return key

    def build(nt, neutral):
        b = _bsdf(nt, col, neutral, rough=0.9, spec=0.2, sheen=0.8)
        wv = _n(nt, 'ShaderNodeTexWave', Scale=5.0, Distortion=3.0, Detail=1.0)
        nt.links.new(_coords(nt), wv.inputs['Vector'])
        if not neutral:
            r = _ramp(nt, [(0.25, [c * 0.62 for c in col]), (0.75, [min(1.0, c * 1.1) for c in col])])
            nt.links.new(wv.outputs['Fac'], r.inputs['Fac'])
            nt.links.new(r.outputs['Color'], b.inputs['Base Color'])
        _bump(nt, b, wv.outputs['Fac'], 0.6, 0.02)
        return b.outputs['BSDF']
    C.mat(key, base=col, build=build)
    return key


def build_mouse(frame=0, frames=4):
    """A grey plush mouse, nose to the front left; the frames rock it."""
    P('plush', (0.66, 0.66, 0.72), rough=0.9, sheen=1.0, spec=0.2, bump=0.3, bscale=120)
    P('plushpink', (0.97, 0.56, 0.66), rough=0.7, sheen=0.6)
    P('bead', (0.03, 0.03, 0.04), rough=0.15, spec=0.9, cc=1.0)
    t = 2 * math.pi * frame / frames
    o = [ellipsoid('mbody', (-0.02, 0, 0.058), (0.09, 0.068, 0.06), 'plush'),
         ellipsoid('mhead', (0.06, 0, 0.052), (0.065, 0.05, 0.046), 'plush'),
         ellipsoid('mnose', (0.123, 0, 0.05), (0.014, 0.014, 0.013), 'plushpink')]
    for sgn in (-1, 1):
        o.append(ellipsoid('mear%d' % sgn, (0.045, sgn * 0.042, 0.1), (0.014, 0.036, 0.036), 'plush', rot=(sgn * -20, 0, sgn * 10)))
        o.append(ellipsoid('meari%d' % sgn, (0.052, sgn * 0.042, 0.1), (0.008, 0.026, 0.026), 'plushpink', rot=(sgn * -20, 0, sgn * 10)))
        o.append(ellipsoid('meye%d' % sgn, (0.1, sgn * 0.024, 0.068), (0.009, 0.009, 0.01), 'bead'))
    wig = 0.03 * math.sin(t)
    o.append(tube('mtail', [(-0.1, 0, 0.04), (-0.16, 0.03 + wig, 0.015), (-0.22, 0.0 + wig, 0.01), (-0.25, -0.05 + wig, 0.01),
                            (-0.22, -0.085, 0.01)], [0.008, 0.007, 0.006, 0.005, 0.004], 'plushpink'))
    return group(o, 'mouse', (TOYS['mouse'][0], TOYS['mouse'][1], 0), (7 * math.sin(t), 0, 200 + 6 * math.cos(t)))


def build_feather(frame=0, frames=4, top=1.25):
    """Feathers on a string that rises to the player's finger; the frames sway
    it about the string's top. Built round the top of the string."""
    P('string', (0.96, 0.90, 0.80), rough=0.8)
    P('fbead', (0.95, 0.35, 0.55), rough=0.2, spec=0.7, cc=0.8)
    P('fth1', (0.25, 0.80, 0.78), rough=0.6, sheen=1.0)
    P('fth2', (0.98, 0.50, 0.66), rough=0.6, sheen=1.0)
    P('fth3', (1.0, 0.84, 0.32), rough=0.6, sheen=1.0)
    P('fthrib', (0.97, 0.97, 0.95), rough=0.5)
    t = 2 * math.pi * frame / frames
    zb = 0.42 - top                 # the bead, relative to the top
    o = [tube('fstr', [(0, 0, 0), (0, 0, zb / 2), (0, 0, zb + 0.02)], [0.005, 0.005, 0.005], 'string'),
         ellipsoid('fbead', (0, 0, zb), (0.028, 0.028, 0.028), 'fbead')]
    for k, (key, yaw, tilt, ln) in enumerate((('fth1', -35, 28, 0.27), ('fth2', 20, 18, 0.30), ('fth3', 70, 32, 0.25))):
        flut = 7 * math.sin(t + k * 2.1)
        d = Euler((0, math.radians(tilt + flut), math.radians(yaw)), 'XYZ').to_matrix() @ V((0, 0, -1))
        base = V((0, 0, zb - 0.012))
        f = ellipsoid('fth%d' % k, base + d * (ln / 2), (0.06, 0.01, ln / 2), key)
        f.rotation_mode = 'QUATERNION'
        f.rotation_quaternion = (-d).to_track_quat('Z', 'X')
        o.append(f)
        o.append(rod('frib%d' % k, base, base + d * (ln * 0.95), 0.004, 'fthrib', seg=8))
    group(o, 'feather', (TOYS['feather'][0], TOYS['feather'][1], top), (0, 5 * math.sin(t), 0))
    return o


def build_yarn(frame=None, frames=8):
    """A yarn ball; frame None = at rest with a loose thread on the floor,
    else rolling to the right (+x) by 360/frames degrees a frame."""
    YARN('yarn', (0.98, 0.45, 0.58))
    R = 0.13
    o = [ellipsoid('yball', (0, 0, 0), (R, R, R * 0.97), 'yarn', rot=(20, 35, 10))]
    rnd = random.Random(7)
    for k in range(3):
        o.append(torus('yw%d' % k, (0, 0, 0), R - 0.004, 0.0075, 0.0075, 'yarn',
                       rot=(rnd.uniform(0, 180), rnd.uniform(0, 180), rnd.uniform(0, 180))))
    o.append(tube('yend', [(-0.05, -0.11, 0.05), (-0.09, -0.1, 0.0), (-0.12, -0.07, -0.05)], [0.008, 0.008, 0.007], 'yarn'))
    ang = 0 if frame is None else -360.0 * frame / frames
    group(o, 'yarnball', (TOYS['yarn'][0], TOYS['yarn'][1], R), (0, ang, 0))
    if frame is None:
        yx, yy = TOYS['yarn']
        o.append(tube('ythread', [(yx - 0.12, yy - 0.07, R - 0.05), (yx - 0.16, yy - 0.09, 0.01), (yx - 0.26, yy - 0.05, 0.008),
                                  (yx - 0.34, yy - 0.12, 0.008), (yx - 0.42, yy - 0.08, 0.008)], [0.008] * 5, 'yarn'))
    return o


def build_ball(frame=None, frames=8):
    """A lattice jingle ball with a golden bell inside; rolling to the right."""
    P('lattice', (0.95, 0.24, 0.34), rough=0.25, spec=0.6, cc=0.8)
    P('bell', (1.0, 0.78, 0.30), rough=0.2, metal=1.0)
    P('bellslot', (0.12, 0.08, 0.04), rough=0.6)
    R = 0.095
    bm = bmesh.new()
    bmesh.ops.create_icosphere(bm, subdivisions=1, radius=R)
    lat = from_bm('lattice', bm, 'lattice', smooth=True)
    wf = lat.modifiers.new('wf', 'WIREFRAME')
    wf.thickness = 0.016
    wf.use_even_offset = True
    sub = lat.modifiers.new('sub', 'SUBSURF')
    sub.levels = 1
    sub.render_levels = 1
    ang = 0 if frame is None else -360.0 * frame / frames
    group([lat], 'ballroot', (TOYS['ball'][0], TOYS['ball'][1], R), (0, ang, 0))
    bx, by = TOYS['ball']
    o = [lat, ellipsoid('bell', (bx, by, 0.05), (0.042, 0.042, 0.04), 'bell'),
         ellipsoid('bellsl', (bx, by - 0.02, 0.035), (0.03, 0.028, 0.006), 'bellslot', rot=(-30, 0, 0))]
    return o


def build_tunnel(yaw=0.0):
    """A crinkly fabric tunnel along X, 0.9 m, openings of radius 0.2, turned
    `yaw` degrees so its left mouth faces the camera a little, with a pompom
    hanging in that mouth."""
    P('tunnel', (0.20, 0.64, 0.74), rough=0.45, spec=0.5, sheen=0.3, bump=0.5, bscale=35, bdist=0.012)
    P('tunnelin', (0.10, 0.36, 0.44), rough=0.7, spec=0.3, bump=0.4, bscale=35, bdist=0.012)
    P('pompom', (0.98, 0.42, 0.58), rough=1.0, sheen=1.0, bump=0.6, bscale=220)
    P('string', (0.96, 0.90, 0.80), rough=0.8)
    tx, ty = TOYS['tunnel']
    L, R, sag = 0.9, 0.2, 0.88
    n = 90
    outer = [(R + 0.012 * abs(math.sin(math.pi * (i / n) * L / 0.075)), -L / 2 + L * i / n) for i in range(n + 1)]
    inner = [(r - 0.014, z) for r, z in outer[::-1]]
    o = [lathe('tunnel', outer, 'tunnel', c=(0, 0, R * sag), n=48, rot=(0, 90, 0), scale=(sag, 1, 1)),
         lathe('tunnelin', inner, 'tunnelin', c=(0, 0, R * sag), n=48, rot=(0, 90, 0), scale=(sag, 1, 1))]
    for k, xe in enumerate((-L / 2, L / 2)):      # the rims
        o.append(torus('trim%d' % k, (xe, 0, R * sag), R - 0.001, 0.009, 0.009, 'tunnel', rot=(0, 90, 0), scale=(sag, 1, 1)))
    ex = L / 2
    o += [tube('tstr', [(ex - 0.01, -0.02, 2 * R * sag + 0.005), (ex + 0.05, -0.06, 0.3), (ex + 0.07, -0.08, 0.2)],
               [0.004] * 3, 'string'),
          ellipsoid('tpom', (ex + 0.07, -0.08, 0.165), (0.038, 0.038, 0.038), 'pompom')]
    return group(o, 'tunnelroot', (tx, ty, 0), (0, 0, yaw))


def tunnel_mouth(yaw=0.0):
    """World point at the bottom centre of the tunnel's right opening (the
    left one is against the bed)."""
    a = math.radians(yaw)
    return (round(TOYS['tunnel'][0] + 0.45 * math.cos(a), 3), round(TOYS['tunnel'][1] + 0.45 * math.sin(a), 3), 0.0)


def build_hammock():
    """A fleece hammock on a little wooden frame under the window; the top of
    the fleece at its middle is at z HAMMOCK_BED_Z (Mila's c_lie goes there)."""
    WOOD('hamwood', (0.60, 0.38, 0.22), scale=2.0, grain=6.0, cc=0.2)
    P('fleece', (0.98, 0.58, 0.50), rough=1.0, sheen=1.0, spec=0.15, bump=0.35, bscale=160, bdist=0.006)
    P('fleecepip', (1.0, 0.94, 0.86), rough=0.8, sheen=1.0)
    hx, hy = TOYS['hammock']
    hw, y0, y1, zt = 0.30, hy - 0.2, hy + 0.18, 0.80
    o = []
    for sx in (-1, 1):
        x = hx + sx * (hw + 0.02)
        for yy in (y0, y1):
            o.append(box('hpost%d%.2f' % (sx, yy), x - 0.022, yy - 0.022, 0.0, x + 0.022, yy + 0.022, zt + 0.03, 'hamwood', bevel=0.008))
        o.append(box('hbar%d' % sx, x - 0.024, y0 - 0.03, zt - 0.02, x + 0.024, y1 + 0.03, zt + 0.02, 'hamwood', bevel=0.008))
    for yy in (y0, y1):
        o.append(box('hrail%.2f' % yy, hx - hw - 0.04, yy - 0.02, 0.0, hx + hw + 0.04, yy + 0.02, 0.05, 'hamwood', bevel=0.008))
    # the fleece sling: a sheet sagging between the two top bars
    nu, nv = 32, 14
    sag_x = zt - 0.015 - HAMMOCK_BED_Z - 0.012
    bm = bmesh.new()
    vs = []
    for j in range(nv + 1):
        v = j / nv
        yy = y0 + 0.01 + (y1 - y0 - 0.02) * v
        for i in range(nu + 1):
            u = -1 + 2 * i / nu
            z = zt - 0.015 - sag_x * (1 - u * u) - 0.012 * (1 - (2 * v - 1) ** 2)
            vs.append(bm.verts.new((hx + u * hw, yy, z)))
    for j in range(nv):
        for i in range(nu):
            k = j * (nu + 1) + i
            bm.faces.new((vs[k], vs[k + 1], vs[k + nu + 2], vs[k + nu + 1]))
    sl = from_bm('sling', bm, 'fleece', normals=False)
    so = sl.modifiers.new('sol', 'SOLIDIFY')
    so.thickness = 0.025
    so.offset = -1.0 if sl.data.polygons[0].normal.z > 0 else 1.0
    o.append(sl)
    for yy in (y0 + 0.01, y1 - 0.01):
        pts = [(hx + u * hw, yy, zt - 0.015 - sag_x * (1 - u * u) - 0.005) for u in (-1, -0.5, 0, 0.5, 1)]
        o.append(tube('hpip%.2f' % yy, pts, [0.014] * 5, 'fleecepip'))
    for sx in (-1, 1):      # the fleece wrapped round each top bar
        x = hx + sx * (hw + 0.02)
        o.append(lathe('hwrap%d' % sx, [(0, y0 - 0.005), (0.036, y0 - 0.005), (0.042, y0 + 0.01), (0.042, y1 - 0.01), (0.036, y1 + 0.005),
                                         (0, y1 + 0.005)], 'fleece', c=(0, 0, 0), n=24))
        o[-1].rotation_euler = (math.radians(-90), 0, 0)
        o[-1].location = (x, 0, zt)
    return o


def build_catnip():
    """Catnip in a terracotta pot with a pink band and a paw label."""
    P('pot', (0.86, 0.45, 0.30), rough=0.6, spec=0.3)
    P('soil', (0.22, 0.13, 0.08), rough=1.0)
    P('potband', (0.97, 0.60, 0.70), rough=0.5, spec=0.4)
    P('nip1', (0.40, 0.74, 0.34), rough=0.55, spec=0.35, bump=0.2, bscale=90)
    P('nip2', (0.55, 0.84, 0.40), rough=0.55, spec=0.35, bump=0.2, bscale=90)
    P('stem', (0.35, 0.60, 0.28), rough=0.6)
    P('label', (1.0, 0.98, 0.94), rough=0.5)
    P('labelpaw', (0.93, 0.40, 0.55), rough=0.5)
    cx, cy = TOYS['catnip']
    o = [lathe('npot', [(0, 0.0), (0.09, 0.0), (0.12, 0.15), (0.135, 0.155), (0.135, 0.19), (0.12, 0.19), (0.11, 0.17), (0, 0.17)],
               'pot', c=(cx, cy, 0), n=48),
         torus('nband', (cx, cy, 0.172), 0.132, 0.012, 0.018, 'potband'),
         ellipsoid('nsoil', (cx, cy, 0.17), (0.11, 0.11, 0.012), 'soil')]
    rnd = random.Random(14)
    for k in range(22):
        a = 2 * math.pi * k / 7.3 + rnd.uniform(-0.2, 0.2)
        lvl = k / 22
        r = 0.05 + 0.07 * (1 - lvl) + rnd.uniform(-0.01, 0.01)
        z = 0.2 + 0.2 * lvl + rnd.uniform(0, 0.03)
        p = (cx + r * math.cos(a), cy + r * math.sin(a), z)
        o.append(ellipsoid('nleaf%d' % k, p, (0.05, 0.034, 0.007), 'nip1' if k % 2 else 'nip2',
                           rot=(0, -25 - 20 * (1 - lvl), math.degrees(a))))
        if k % 3 == 0:
            o.append(tube('nstem%d' % k, [(cx, cy, 0.17), (cx + r * 0.5 * math.cos(a), cy + r * 0.5 * math.sin(a), (0.17 + z) / 2),
                                          (p[0], p[1], p[2] - 0.005)], [0.006, 0.005, 0.004], 'stem'))
    o.append(rod('nstick', (cx + 0.07, cy - 0.06, 0.15), (cx + 0.09, cy - 0.08, 0.33), 0.005, 'label', seg=8))
    o.append(prism_xz('nlab', rect(cx + 0.045, 0.30, cx + 0.135, 0.37), cy - 0.085, cy - 0.078, 'label', bevel=0.004))
    o.append(prism_xz('nlabp', circle(cx + 0.09, 0.332, 0.018, 12), cy - 0.088, cy - 0.085, 'labelpaw'))
    for k, dx in enumerate((-0.022, 0.0, 0.022)):
        o.append(prism_xz('nlabt%d' % k, circle(cx + 0.09 + dx, 0.36 - (0.0 if dx == 0 else 0.004), 0.008, 8),
                          cy - 0.088, cy - 0.085, 'labelpaw'))
    return o


def fish(name, col_key, fin_key, heading, wag):
    """A little goldfish around the origin, nose along +X, turned by heading."""
    P('fisheye', (0.02, 0.02, 0.03), rough=0.2, spec=0.8)
    o = [ellipsoid(name + 'b', (0, 0, 0), (0.06, 0.026, 0.042), col_key)]
    tail = prism_xz(name + 't', [(0, 0), (-0.07, 0.05), (-0.052, 0), (-0.07, -0.05)], -0.005, 0.005, fin_key)
    tail.location = (-0.045, 0, 0)
    tail.rotation_euler = (0, 0, math.radians(wag))
    o.append(tail)
    o.append(slab(name + 'd', (-0.025, 0.003, 0.032), (0.045, 0, 0.0), (-0.025, 0, 0.032), 0.006, fin_key))
    for s in (-1, 1):
        o.append(ellipsoid(name + 'e%d' % s, (0.036, s * 0.021, 0.01), (0.011, 0.006, 0.011), 'fisheye'))
    return o


def build_fishbowl(frame=0, frames=8, opaque=False):
    cx, cy = TOYS['fishbowl']
    WOOD('stoolw', (0.98, 0.82, 0.44), scale=2.0, grain=4.0, cc=0.3, rough=0.4)
    P('stoolleg', (0.96, 0.78, 0.40), rough=0.45, spec=0.4)
    if opaque:
        CLEAR('fbwater_o', (0.7, 0.9, 1.0), (0.30, 0.64, 0.96), 0.08, 0.6, back=0.92, glow=0.35)
        CLEAR('fbglass_o', (1, 1, 1), (0.9, 0.97, 1.0), 0.15, 0.8)
        wk, gk = 'fbwater_o', 'fbglass_o'
    else:
        CLEAR('fbwater', (0.75, 0.92, 1.0), (0.30, 0.64, 0.96), 0.04, 0.5, back=0.82, glow=0.2)
        CLEAR('fbglass', (1, 1, 1), (0.9, 0.97, 1.0), 0.03, 0.6)
        wk, gk = 'fbwater', 'fbglass'
    P('gravel1', (0.98, 0.66, 0.74), rough=0.5)
    P('gravel2', (0.95, 0.88, 0.72), rough=0.6)
    P('gravel3', (0.88, 0.80, 0.62), rough=0.6)
    P('weed', (0.30, 0.75, 0.40), rough=0.5, spec=0.4)
    P('fish1', (1.0, 0.42, 0.06), rough=0.35, spec=0.6, cc=0.5, emit=(1.0, 0.40, 0.05), es=0.35)
    P('fin1', (1.0, 0.70, 0.35), rough=0.4, spec=0.5)
    P('fish2', (1.0, 0.25, 0.35), rough=0.35, spec=0.6, cc=0.5, emit=(1.0, 0.25, 0.35), es=0.3)
    P('fin2', (1.0, 0.75, 0.80), rough=0.4, spec=0.5)
    o = [lathe('seat', [(0, 0.255), (0.18, 0.255), (0.195, 0.265), (0.2, 0.285), (0.19, 0.3), (0, 0.3)], 'stoolw',
               c=(cx, cy, 0), n=48)]
    for k in range(3):
        a = 2 * math.pi * k / 3 - math.pi / 2
        o.append(rod('leg%d' % k, (cx + 0.12 * math.cos(a), cy + 0.12 * math.sin(a), 0.26),
                     (cx + 0.165 * math.cos(a), cy + 0.165 * math.sin(a), 0.0), 0.022, 'stoolleg', r1=0.018))
    ring = [(cx + 0.14 * math.cos(2 * math.pi * k / 3 - math.pi / 2), cy + 0.14 * math.sin(2 * math.pi * k / 3 - math.pi / 2))
            for k in range(3)]
    for k in range(3):
        (ax, ay), (bx, by) = ring[k], ring[(k + 1) % 3]
        o.append(rod('rung%d' % k, (ax, ay, 0.11), (bx, by, 0.11), 0.011, 'stoolleg'))
    # the bowl: water up to zw, a glass band above, a lip
    zc, R, Rg, zw = 0.478, 0.195, 0.2, 0.60
    a0 = math.asin(0.08 / R)
    a1 = math.acos((zc - zw) / R)
    prof = [(0, zc - R * math.cos(a0))]
    for i in range(0, 25):
        a = a0 + (a1 - a0) * i / 24
        prof.append((R * math.sin(a), zc - R * math.cos(a)))
    prof.append((0, zw))
    o.append(lathe('water', prof, wk, c=(cx, cy, 0), n=56))
    zl = zc + math.sqrt(Rg * Rg - 0.12 * 0.12)
    ga0, ga1 = math.acos((zc - zw) / Rg), math.acos((zc - zl) / Rg)
    band = [(Rg * math.sin(ga0 + (ga1 - ga0) * i / 8), zc - Rg * math.cos(ga0 + (ga1 - ga0) * i / 8)) for i in range(9)]
    o.append(lathe('glassband', band, gk, c=(cx, cy, 0), n=56))
    o.append(torus('lip', (cx, cy, zl), 0.12, 0.011, 0.011, gk))
    # gravel and a weed at the bottom
    rnd = random.Random(21)
    zb = zc - R * math.cos(a0)
    for k in range(34):
        a = rnd.uniform(0, 2 * math.pi)
        r = math.sqrt(rnd.uniform(0, 1)) * 0.10
        o.append(ellipsoid('grv%d' % k, (cx + r * math.cos(a), cy + r * math.sin(a), zb + 0.012 + (r / 0.1) ** 2 * 0.02),
                           (0.017, 0.015, 0.012), ('gravel1', 'gravel2', 'gravel3', 'gravel2')[k % 4], seg=10, rings=6))
    for k, (dx, dy, tilt, hgt) in enumerate(((0.05, 0.07, 12, 0.07), (0.075, 0.05, -14, 0.055), (0.03, 0.085, -5, 0.05))):
        o.append(ellipsoid('weed%d' % k, (cx + dx, cy + dy, zb + 0.02 + hgt), (0.015, 0.008, hgt), 'weed', rot=(tilt * 0.5, tilt, 0)))
    # two fish going round, opposite ways
    t = 2 * math.pi * frame / frames
    wag = 18 if frame % 2 == 0 else -18
    a = t - math.pi / 2                  # starts in front, swimming right
    f1 = fish('fa', 'fish1', 'fin1', 0, wag)
    group(f1, 'fa', (cx + 0.085 * math.cos(a), cy + 0.085 * math.sin(a), 0.47 + 0.015 * math.sin(2 * t)),
          (0, 0, math.degrees(a) + 90))
    b = math.pi * 0.8 - t                # starts at the back left, the other way
    f2 = fish('fb', 'fish2', 'fin2', 0, -wag)
    group(f2, 'fb', (cx + 0.07 * math.cos(b), cy + 0.07 * math.sin(b), 0.52 + 0.012 * math.cos(2 * t)),
          (0, 0, math.degrees(b) - 90))
    o += f1 + f2
    # a highlight on the glass, towards the light
    GLINT('shine', 0.75)
    d = V((-0.55, -0.62, 0.56)).normalized()
    hl = ellipsoid('shine', (cx + d.x * (Rg + 0.003), cy + d.y * (Rg + 0.003), zc + d.z * (Rg + 0.003)), (0.045, 0.003, 0.016), 'shine',
                   seg=16, rings=8)
    hl.rotation_mode = 'QUATERNION'
    hl.rotation_quaternion = d.to_track_quat('Y', 'Z')
    o.append(hl)
    return o


# ---------------------------------------------------------------------------
# rendering
# ---------------------------------------------------------------------------

def render(name, objs, anchor, passes=('color', 'z', 'shadow'), kind='prop', extra=None, samples=None):
    t0 = __import__('time').time()
    info = C.render_sprite(ARGS.out, name, objs, V(anchor), passes=passes, shadow_z=0.0 if 'shadow' in passes else None,
                           samples=samples or ARGS.samples, kind=kind, extra=extra, zoom=ZOOM, shadow_samples=96)
    if 'shadow' in passes:
        feather(os.path.join(os.path.abspath(ARGS.out), name + '_sh.png'))
    print('RENDERED %-22s %4d x %-4d %.1f s' % (name, info['w'], info['h'], __import__('time').time() - t0))
    return info


def feather(path, px=5):
    """The shadow pass also catches the sky's occlusion, which runs past the
    fitted frame; fade the last few pixels so the watch never shows a cut edge."""
    import numpy as np
    img = bpy.data.images.load(path, check_existing=False)
    w, h = img.size
    a = np.empty(w * h * 4, np.float32)
    img.pixels.foreach_get(a)
    bpy.data.images.remove(img)
    sh = a.reshape(h, w, 4)[::-1, :, 0] * 255.0
    fx = np.clip(np.minimum(np.arange(w), w - 1 - np.arange(w)) / float(px), 0, 1)
    fy = np.clip(np.minimum(np.arange(h), h - 1 - np.arange(h)) / float(px), 0, 1)
    C.write_png(path, np.round(sh * fy[:, None] * fx[None, :]).astype(np.uint8))


def icon(name, objs, anchor, px=80, pad=5):
    """A shop thumbnail: same camera, zoom chosen so the toy fills px x px (at most 3)."""
    w, h, ax, ay = C.fit(objs, V(anchor), margin=0, zoom=1.0)
    z = min((px - 2 * pad) / w, (px - 2 * pad) / h, C.SHOP_ZOOM)
    cx, cy = w / 2.0 - ax, h / 2.0 - ay
    size = (px, px, round(px / 2 - cx * z), round(px / 2 - cy * z))
    t0 = __import__('time').time()
    C.render_sprite(ARGS.out, name, objs, V(anchor), passes=('color',), size=size, samples=max(ARGS.samples, 128),
                    kind='icon', zoom=z)
    print('RENDERED %-22s %4d x %-4d zoom %.2f %.1f s' % (name, px, px, z, __import__('time').time() - t0))


def job(name, build, anchor, **kw):
    if want(name):
        objs = build()
        render(name, objs, anchor, **kw)
        C.remove(objs)


def job_icon(name, build, anchor):
    if want(name):
        objs = build()
        icon(name, objs, anchor)
        C.remove(objs)


def main():
    C.reset('casita', cpu=ARGS.cpu)
    T = {k: (v[0], v[1], 0) for k, v in TOYS.items()}
    job('casita_room', lambda: build_floor() + build_wall() + build_rug(), (0, 0, 0), passes=('color', 'z'), kind='room',
        samples=max(ARGS.samples, 128), extra={'background': True})
    job('casita_window_day', lambda: build_window('day'), WIN_ANCHOR, passes=('color', 'z'), kind='overlay',
        extra={'note': 'drawn over the window by day'})
    for f, ang in enumerate(DOOR_ANGLES):
        job('casita_door_%02d' % f, lambda: build_door(ang), DOOR_ANCHOR, passes=('color', 'z'), kind='prop',
            extra={'anim': 'door', 'frame': f, 'frames': len(DOOR_ANGLES), 'ms': 90, 'angle': ang})
    job('casita_bed', build_bed, (BED_C[0], BED_C[1], 0), extra={'bed_z': 0.08, 'inner_r': 0.36})
    job('casita_bowls', build_bowls, (BOWLS_C[0], BOWLS_C[1], 0), extra={'food': list(BOWLS_C), 'water': [0.9, 0.4]})
    ga = (GIFT_C[0], GIFT_C[1], 0)
    job('casita_gift', build_gift, ga)
    for f in range(4):
        job('casita_gift_open_%02d' % f, lambda: build_gift(f), ga, extra={'anim': 'gift_open', 'frame': f, 'frames': 4, 'ms': 110})
    # toys
    job('toy_post', build_post, T['post'], extra={'spot': list(TOYS['post']), 'top_z': 1.1})
    job('toy_box', build_box, T['box'], extra={'spot': list(TOYS['box']), 'inner': [0.66, 0.46], 'wall_h': 0.4})
    for f in range(8):
        job('toy_fishbowl_%02d' % f, lambda: build_fishbowl(f), T['fishbowl'],
            extra={'spot': list(TOYS['fishbowl']), 'anim': 'swim', 'frame': f, 'frames': 8, 'ms': 160})
    for f in range(4):
        job('toy_mouse_%02d' % f, lambda: build_mouse(f), T['mouse'],
            extra={'start': list(TOYS['mouse']), 'free': True, 'anim': 'wobble', 'frame': f, 'frames': 4, 'ms': 120})
    for f in range(4):
        job('toy_feather_%02d' % f, lambda: build_feather(f), T['feather'],
            extra={'free': True, 'anim': 'sway', 'frame': f, 'frames': 4, 'ms': 140, 'string_top_z': 1.25})
    job('toy_yarn', build_yarn, T['yarn'], extra={'start': list(TOYS['yarn']), 'free': True})
    for f in range(8):
        job('toy_yarn_roll_%02d' % f, lambda: build_yarn(f), T['yarn'],
            extra={'free': True, 'anim': 'roll', 'frame': f, 'frames': 8, 'ms': 60, 'rolls': '+x (play backwards for -x)',
                   'm_per_turn': round(2 * math.pi * 0.13, 3)})
    job('toy_ball', build_ball, T['ball'], extra={'start': list(TOYS['ball']), 'free': True})
    for f in range(8):
        job('toy_ball_roll_%02d' % f, lambda: build_ball(f), T['ball'],
            extra={'free': True, 'anim': 'roll', 'frame': f, 'frames': 8, 'ms': 50, 'rolls': '+x (play backwards for -x)',
                   'm_per_turn': round(2 * math.pi * 0.095, 3)})
    job('toy_tunnel', build_tunnel, T['tunnel'], extra={'spot': list(TOYS['tunnel']), 'length': 0.9, 'opening_r': 0.2, 'yaw': 0,
                                                     'mouth': list(tunnel_mouth())})
    job('toy_hammock', build_hammock, T['hammock'], extra={'spot': list(TOYS['hammock']), 'bed_z': HAMMOCK_BED_Z})
    job('toy_catnip', build_catnip, T['catnip'], extra={'spot': list(TOYS['catnip'])})
    # shop thumbnails
    job_icon('icon_toy_mouse', build_mouse, T['mouse'])
    job_icon('icon_toy_feather', lambda: build_feather(0, top=0.75), T['feather'])
    job_icon('icon_toy_yarn', build_yarn, T['yarn'])
    job_icon('icon_toy_ball', build_ball, T['ball'])
    job_icon('icon_toy_post', build_post, T['post'])
    job_icon('icon_toy_box', build_box, T['box'])
    job_icon('icon_toy_tunnel', build_tunnel, T['tunnel'])
    job_icon('icon_toy_fishbowl', lambda: build_fishbowl(0, opaque=True), T['fishbowl'])
    job_icon('icon_toy_hammock', build_hammock, T['hammock'])
    job_icon('icon_toy_catnip', build_catnip, T['catnip'])
    C.save_meta(ARGS.out)


def mila_preview():
    """A temporary sitting Mila (sample.py's model) at casita zoom, for the
    preview only: <out>/_tmp/mila_tmp_sit."""
    here = os.path.dirname(os.path.abspath(__file__))
    src = open(os.path.join(here, 'sample.py')).read()
    src = src[:src.rstrip().rfind('main()')]
    ns = {'__name__': 'sample_model'}
    exec(compile(src, 'sample.py', 'exec'), ns)
    C.reset('neutral', cpu=ARGS.cpu)
    objs = ns['mila']('m_', 'sit', ())
    root = ns['parent_all'](objs, 'mila', loc=(0, 0, 0), rotz=-10)
    bpy.context.view_layer.update()
    for ob in objs:
        keys = []
        for m in ob.data.materials:
            k = 'raw_' + m.name
            if k not in C._MATS:
                C.mat(k)
                C._MATS[k].variants['color'] = m
            keys.append(k)
        ob['ml_keys'] = keys
    out = os.path.join(ARGS.out, '_tmp')
    C.render_sprite(out, 'mila_tmp_sit', objs, V((0, 0, 0)), passes=('color', 'z', 'shadow'), shadow_z=0.0,
                    bounce_ground=0.0, samples=64, kind='char', zoom=ZOOM, margin=16)
    feather(os.path.join(os.path.abspath(out), 'mila_tmp_sit_sh.png'))
    C.save_meta(out)
    del root


if __name__ == '__main__':
    main()
    if '--mila' in sys.argv:
        mila_preview()
