"""Mila - helpers for the attic and roofs kits (worlds_b.py).

Geometry built in world coordinates at cell (0, 0), materials through
ml_common's registry (C.mat with build=, so every pass gets its variant),
2D shapes for stencils and marks, and a `shoot()` that builds, renders and
cleans up one sprite at a time.
"""
import math
import random
import time

import bpy
import bmesh
from mathutils import Vector as V, Euler, Matrix

import ml_common as C

# ---------------------------------------------------------------------------
# materials
# ---------------------------------------------------------------------------


def _bsdf(nt, col, neutral, rough=0.6, spec=0.4, metal=0.0, cc=0.0, sheen=0.0,
          emit=None, es=0.0, alpha=1.0, color_sock=None):
    b = nt.nodes.new('ShaderNodeBsdfPrincipled')
    b.inputs['Base Color'].default_value = tuple((0.8, 0.8, 0.8) if neutral else col) + (1,)
    if color_sock is not None and not neutral:
        nt.links.new(color_sock, b.inputs['Base Color'])
    b.inputs['Roughness'].default_value = rough
    b.inputs['Specular'].default_value = spec
    b.inputs['Metallic'].default_value = metal
    b.inputs['Clearcoat'].default_value = cc
    b.inputs['Sheen'].default_value = sheen
    if emit is not None and es > 0:
        b.inputs['Emission'].default_value = tuple(emit) + (1,)
        b.inputs['Emission Strength'].default_value = es
    if alpha < 1.0:
        b.inputs['Alpha'].default_value = alpha
    return b


def solid(key, col, **kw):
    """A plain principled material (with sheen / clearcoat / alpha / emission)."""
    if key in C._MATS:
        return key

    def build(nt, neutral):
        return _bsdf(nt, col, neutral, **kw).outputs['BSDF']
    return C.mat(key, base=col, build=build)


def _ramp(nt, fac, c0, c1, p0=0.3, p1=0.7):
    r = nt.nodes.new('ShaderNodeValToRGB')
    r.color_ramp.elements[0].position = p0
    r.color_ramp.elements[0].color = tuple(c0) + (1,)
    r.color_ramp.elements[1].position = p1
    r.color_ramp.elements[1].color = tuple(c1) + (1,)
    nt.links.new(fac, r.inputs['Fac'])
    return r.outputs['Color']


def _coords(nt, scale=(1, 1, 1), offset=(0, 0, 0), space='Object'):
    tc = nt.nodes.new('ShaderNodeTexCoord')
    mp = nt.nodes.new('ShaderNodeMapping')
    mp.inputs['Location'].default_value = offset
    mp.inputs['Scale'].default_value = scale
    nt.links.new(tc.outputs[space], mp.inputs['Vector'])
    return mp.outputs['Vector']


def wood(key, col, scale=1.0, grain=(1.0, 10.0, 1.0), offset=(0, 0, 0), dark=0.78,
         rough=0.55, cc=0.12, spec=0.35, sheen=0.0, lines=0.0):
    """Wood grain along X (noise stretched across the grain); `lines` adds
    fine darker streaks."""
    if key in C._MATS:
        return key

    def build(nt, neutral):
        v = _coords(nt, grain, offset)
        nz = nt.nodes.new('ShaderNodeTexNoise')
        nz.inputs['Scale'].default_value = 2.0 * scale
        nz.inputs['Detail'].default_value = 3.0
        nt.links.new(v, nz.inputs['Vector'])
        c0 = [c * dark for c in col]
        colsock = _ramp(nt, nz.outputs['Fac'], c0, col, 0.35, 0.65)
        if lines > 0:
            wv = nt.nodes.new('ShaderNodeTexWave')
            wv.wave_type = 'BANDS'
            wv.bands_direction = 'Y'
            wv.inputs['Scale'].default_value = 6.0 * scale
            wv.inputs['Distortion'].default_value = 6.0
            wv.inputs['Detail'].default_value = 2.0
            nt.links.new(v, wv.inputs['Vector'])
            mix = nt.nodes.new('ShaderNodeMixRGB')
            mix.blend_type = 'MULTIPLY'
            fac = _ramp(nt, wv.outputs['Fac'], (1, 1, 1), (1 - lines, 1 - lines, 1 - lines), 0.75, 0.95)
            mix.inputs['Fac'].default_value = 1.0
            nt.links.new(colsock, mix.inputs[1])
            nt.links.new(fac, mix.inputs[2])
            colsock = mix.outputs['Color']
        return _bsdf(nt, col, neutral, rough=rough, cc=cc, spec=spec, sheen=sheen,
                     color_sock=colsock).outputs['BSDF']
    return C.mat(key, base=col, build=build)


def mottled(key, col, var=0.12, scale=6.0, rough=0.7, spec=0.35, cc=0.0, sheen=0.0, offset=(0, 0, 0),
            col2=None):
    """A colour with soft noise variation (cardboard, brick, terracotta)."""
    if key in C._MATS:
        return key

    def build(nt, neutral):
        v = _coords(nt, (1, 1, 1), offset)
        nz = nt.nodes.new('ShaderNodeTexNoise')
        nz.inputs['Scale'].default_value = scale
        nz.inputs['Detail'].default_value = 2.0
        nt.links.new(v, nz.inputs['Vector'])
        c0 = [c * (1 - var) for c in col]
        c1 = col2 if col2 is not None else [min(1.0, c * (1 + var)) for c in col]
        colsock = _ramp(nt, nz.outputs['Fac'], c0, c1, 0.3, 0.7)
        return _bsdf(nt, col, neutral, rough=rough, spec=spec, cc=cc, sheen=sheen,
                     color_sock=colsock).outputs['BSDF']
    return C.mat(key, base=col, build=build)


def banded(key, col_a, col_b, axis=2, half=0.28, rough=0.3, spec=0.6, cc=0.4):
    """Two colours by the object's local coordinate: |coord| < half -> col_b
    (a ball's stripe)."""
    if key in C._MATS:
        return key

    def build(nt, neutral):
        tc = nt.nodes.new('ShaderNodeTexCoord')
        sep = nt.nodes.new('ShaderNodeSeparateXYZ')
        nt.links.new(tc.outputs['Object'], sep.inputs[0])
        ab = nt.nodes.new('ShaderNodeMath')
        ab.operation = 'ABSOLUTE'
        nt.links.new(sep.outputs[axis], ab.inputs[0])
        lt = nt.nodes.new('ShaderNodeMath')
        lt.operation = 'LESS_THAN'
        lt.inputs[1].default_value = half
        nt.links.new(ab.outputs[0], lt.inputs[0])
        mix = nt.nodes.new('ShaderNodeMixRGB')
        mix.inputs[1].default_value = tuple(col_a) + (1,)
        mix.inputs[2].default_value = tuple(col_b) + (1,)
        nt.links.new(lt.outputs[0], mix.inputs['Fac'])
        return _bsdf(nt, col_a, neutral, rough=rough, spec=spec, cc=cc,
                     color_sock=mix.outputs['Color']).outputs['BSDF']
    return C.mat(key, base=col_a, build=build)


# ---------------------------------------------------------------------------
# geometry (everything in world coordinates, built at cell (0, 0))
# ---------------------------------------------------------------------------

def _obj(name, me, key=None, smooth=True):
    ob = bpy.data.objects.new(name, me)
    C.link(ob)
    if smooth:
        for p in me.polygons:
            p.use_smooth = True
    if key:
        C.assign(ob, key)
    return ob


def box(name, x0, y0, z0, x1, y1, z1, key, bevel=0.0, seg=2):
    ob = C.box(name, x0, y0, z0, x1, y1, z1, key)
    if bevel > 0:
        md = ob.modifiers.new('bv', 'BEVEL')
        md.width = bevel
        md.segments = seg
        md.limit_method = 'ANGLE'
        for p in ob.data.polygons:
            p.use_smooth = True
        ob.data.use_auto_smooth = True
    return ob


def _bevel_by_weight(ob, bevel, keep, seg=2):
    """Bevel every edge except those `keep(v1, v2)` says to leave sharp (cut
    faces that must join a neighbour cell without a groove)."""
    me = ob.data
    bm = bmesh.new()
    bm.from_mesh(me)
    lay = bm.edges.layers.bevel_weight.verify()
    for e in bm.edges:
        a, b = e.verts[0].co, e.verts[1].co
        # only sharp edges get bevelled (angle between faces)
        e[lay] = 0.0 if keep(a, b) else 1.0
    bm.to_mesh(me)
    bm.free()
    md = ob.modifiers.new('bv', 'BEVEL')
    md.width = bevel
    md.segments = seg
    md.limit_method = 'WEIGHT'
    for p in me.polygons:
        p.use_smooth = True
    me.use_auto_smooth = True
    me.auto_smooth_angle = math.radians(35)
    return ob


def on_planes(planes, eps=1e-5):
    """keep() for _bevel_by_weight: edges lying on any of the given planes,
    ('x', 0.0) / ('y', 1.0)..."""
    idx = {'x': 0, 'y': 1, 'z': 2}

    def keep(a, b):
        for ax, val in planes:
            i = idx[ax]
            if abs(a[i] - val) < eps and abs(b[i] - val) < eps:
                return True
        return False
    return keep


def cbox(name, x0, y0, z0, x1, y1, z1, key, bevel, cut=()):
    """A bevelled box whose faces on the cell border listed in `cut`
    (('x', 0.0), ...) stay sharp, so it continues seamlessly into the next cell."""
    ob = C.box(name, x0, y0, z0, x1, y1, z1, key)
    if bevel > 0:
        _bevel_by_weight(ob, bevel, on_planes(cut))
    return ob


def ellipsoid(name, c, size, key, rot=(0, 0, 0), seg=28, rings=18):
    me = bpy.data.meshes.new(name)
    bm = bmesh.new()
    bmesh.ops.create_uvsphere(bm, u_segments=seg, v_segments=rings, radius=1.0)
    bm.to_mesh(me)
    bm.free()
    ob = _obj(name, me, key)
    ob.scale = size
    ob.rotation_euler = Euler([math.radians(a) for a in rot], 'XYZ')
    ob.location = c
    return ob


def cone(name, c, r1, r2, h, key, rot=(0, 0, 0), verts=32, scale=(1, 1, 1), smooth=True):
    me = bpy.data.meshes.new(name)
    bm = bmesh.new()
    bmesh.ops.create_cone(bm, cap_ends=True, segments=verts, radius1=r1, radius2=r2, depth=h)
    bmesh.ops.translate(bm, verts=bm.verts, vec=(0, 0, h / 2))
    bm.to_mesh(me)
    bm.free()
    ob = _obj(name, me, key, smooth)
    if smooth:
        me.use_auto_smooth = True
        me.auto_smooth_angle = math.radians(40)
    ob.scale = scale
    ob.rotation_euler = Euler([math.radians(a) for a in rot], 'XYZ')
    ob.location = c
    return ob


def torus(name, c, R, r, key, rot=(0, 0, 0), scale=(1, 1, 1), n=40, m=14):
    me = bpy.data.meshes.new(name)
    bm = bmesh.new()
    vs = []
    for i in range(n):
        a = 2 * math.pi * i / n
        for j in range(m):
            b = 2 * math.pi * j / m
            vs.append(bm.verts.new(((R + r * math.cos(b)) * math.cos(a),
                                    (R + r * math.cos(b)) * math.sin(a), r * math.sin(b))))
    for i in range(n):
        for j in range(m):
            bm.faces.new((vs[i * m + j], vs[((i + 1) % n) * m + j],
                          vs[((i + 1) % n) * m + (j + 1) % m], vs[i * m + (j + 1) % m]))
    bm.to_mesh(me)
    bm.free()
    ob = _obj(name, me, key)
    ob.scale = scale
    ob.rotation_euler = Euler([math.radians(a) for a in rot], 'XYZ')
    ob.location = c
    return ob


def tube(name, pts, radii, key, res=10):
    """A tube through pts (a NURBS with per-point radius), turned into a mesh
    (fit() only sees meshes)."""
    cu = bpy.data.curves.new(name + '_c', 'CURVE')
    cu.dimensions = '3D'
    cu.bevel_depth = 1.0
    cu.bevel_resolution = 4
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
    bpy.data.curves.remove(cu)
    me.materials.clear()
    return _obj(name, me, key)


def annulus(name, cx, cy, r0, r1, zb, zt, key, n=48):
    """A flat ring (washer) between radii r0 < r1."""
    verts, faces = [], []
    for k in range(n):
        a = 2 * math.pi * k / n
        c, s = math.cos(a), math.sin(a)
        for r in (r0, r1):
            for z in (zb, zt):
                verts.append((cx + r * c, cy + r * s, z))
    # vertex index: k*4 + (r index)*2 + (z index)
    for k in range(n):
        j = (k + 1) % n
        i0b, i0t, i1b, i1t = k * 4, k * 4 + 1, k * 4 + 2, k * 4 + 3
        j0b, j0t, j1b, j1t = j * 4, j * 4 + 1, j * 4 + 2, j * 4 + 3
        faces.append((i0t, i1t, j1t, j0t))      # top
        faces.append((i0b, j0b, j1b, i1b))      # bottom
        faces.append((i1b, j1b, j1t, i1t))      # outer
        faces.append((i0b, i0t, j0t, j0b))      # inner
    return C.mesh_object(name, verts, faces, key)


def skin(objs, name, key, voxel=0.02, iters=8, factor=0.6):
    """Fuse overlapping primitives into one soft skin (voxel remesh + smooth)."""
    bpy.context.view_layer.update()
    dg = bpy.context.evaluated_depsgraph_get()
    bm = bmesh.new()
    for ob in objs:
        ev = ob.evaluated_get(dg)
        me = ev.to_mesh()
        tmp = bmesh.new()
        tmp.from_mesh(me)
        tmp.transform(ev.matrix_world)
        tme = bpy.data.meshes.new('tmp')
        tmp.to_mesh(tme)
        tmp.free()
        bm.from_mesh(tme)
        bpy.data.meshes.remove(tme)
        ev.to_mesh_clear()
    me = bpy.data.meshes.new(name)
    bm.to_mesh(me)
    bm.free()
    for ob in objs:
        bpy.data.objects.remove(ob, do_unlink=True)
    ob = _obj(name, me, key)
    rm = ob.modifiers.new('remesh', 'REMESH')
    rm.mode = 'VOXEL'
    rm.voxel_size = voxel
    rm.use_smooth_shade = True
    sm = ob.modifiers.new('smooth', 'SMOOTH')
    sm.factor = factor
    sm.iterations = iters
    return ob


def prism(name, polys, zb, zt, key, xform=None, bevel=0.0, smooth=False, cut=()):
    """Extrude 2D polygons (lists of (x, y), counter-clockwise) between zb and
    zt (numbers or functions f(x, y)). xform: a 4x4 Matrix applied after."""
    if polys and isinstance(polys[0][0], (int, float)):
        polys = [polys]
    zbf = zb if callable(zb) else (lambda x, y: zb)
    ztf = zt if callable(zt) else (lambda x, y: zt)
    verts, faces = [], []
    for poly in polys:
        n = len(poly)
        b0 = len(verts)
        for (x, y) in poly:
            verts.append((x, y, zbf(x, y)))
        for (x, y) in poly:
            verts.append((x, y, ztf(x, y)))
        faces.append(tuple(b0 + i for i in reversed(range(n))))
        faces.append(tuple(b0 + n + i for i in range(n)))
        for i in range(n):
            j = (i + 1) % n
            faces.append((b0 + i, b0 + j, b0 + n + j, b0 + n + i))
    if xform is not None:
        verts = [tuple(xform @ V(v)) for v in verts]
    ob = C.mesh_object(name, verts, faces, key)
    if bevel > 0 and cut:
        return _bevel_by_weight(ob, bevel, on_planes(cut))
    if bevel > 0:
        md = ob.modifiers.new('bv', 'BEVEL')
        md.width = bevel
        md.segments = 2
        md.limit_method = 'ANGLE'
    if smooth or bevel > 0:
        for p in ob.data.polygons:
            p.use_smooth = True
        ob.data.use_auto_smooth = True
        ob.data.auto_smooth_angle = math.radians(35)
    return ob


def front_xform(x, y, z):
    """Matrix for a prism drawn in (u, v) on a FRONT face (-Y normal): u -> +X,
    v -> +Z, extrusion w -> -Y (towards the camera), origin at (x, y, z)."""
    return Matrix.Translation((x, y, z)) @ Matrix.Rotation(math.radians(90), 4, 'X')


def top_xform(x, y, z):
    return Matrix.Translation((x, y, z))


def rot_about(objs, cx, cy, deg):
    """Rotate objects about the vertical axis through (cx, cy) (parent to an
    empty)."""
    root = bpy.data.objects.new('rot', None)
    C.link(root)
    root.location = (cx, cy, 0)
    for o in objs:
        o.location = V(o.location) - V((cx, cy, 0))
        o.parent = root
    root.rotation_euler = (0, 0, math.radians(deg))
    bpy.context.view_layer.update()
    return root


# ---------------------------------------------------------------------------
# 2D shapes (counter-clockwise lists of points; centred at 0 unless said)
# ---------------------------------------------------------------------------

def circle(r, n=32, cx=0.0, cy=0.0):
    return [(cx + r * math.cos(2 * math.pi * k / n), cy + r * math.sin(2 * math.pi * k / n)) for k in range(n)]


def ellipse(rx, ry, n=32, cx=0.0, cy=0.0, rot=0.0):
    a = math.radians(rot)
    out = []
    for k in range(n):
        t = 2 * math.pi * k / n
        x, y = rx * math.cos(t), ry * math.sin(t)
        out.append((cx + x * math.cos(a) - y * math.sin(a), cy + x * math.sin(a) + y * math.cos(a)))
    return out


def roundrect(w, h, r, n=6, cx=0.0, cy=0.0):
    r = min(r, w / 2, h / 2)
    pts = []
    for (qx, qy, a0) in ((w / 2 - r, -h / 2 + r, -90), (w / 2 - r, h / 2 - r, 0),
                         (-w / 2 + r, h / 2 - r, 90), (-w / 2 + r, -h / 2 + r, 180)):
        for k in range(n + 1):
            a = math.radians(a0 + 90 * k / n)
            pts.append((cx + qx + r * math.cos(a), cy + qy + r * math.sin(a)))
    return pts


def heart(s, n=48):
    pts = []
    for k in range(n):
        t = 2 * math.pi * k / n
        x = 16 * math.sin(t) ** 3
        y = 13 * math.cos(t) - 5 * math.cos(2 * t) - 2 * math.cos(3 * t) - math.cos(4 * t)
        pts.append((x * s / 17.0, (y + 2.5) * s / 17.0))
    # parametric goes clockwise: reverse for ccw
    return list(reversed(pts))


def star(r1, r2, n=5, rot=90.0, cx=0.0, cy=0.0):
    pts = []
    for k in range(2 * n):
        a = math.radians(rot) + math.pi * k / n
        r = r1 if k % 2 == 0 else r2
        pts.append((cx + r * math.cos(a), cy + r * math.sin(a)))
    return pts


def crescent(R, r, d, n=40, rot=0.0):
    """Disc of radius R minus a disc of radius r centred (d, 0): a moon."""
    xi = (R * R - r * r + d * d) / (2 * d)
    yi = math.sqrt(max(0.0, R * R - xi * xi))
    a = math.atan2(yi, xi)
    b = math.atan2(yi, xi - d)
    pts = []
    for k in range(n + 1):
        t = a + (2 * math.pi - 2 * a) * k / n
        pts.append((R * math.cos(t), R * math.sin(t)))
    for k in range(1, n):
        t = (2 * math.pi - b) - (2 * math.pi - 2 * b) * k / n
        pts.append((d + r * math.cos(t), r * math.sin(t)))
    c, s_ = math.cos(math.radians(rot)), math.sin(math.radians(rot))
    return [(x * c - y * s_, x * s_ + y * c) for x, y in pts]


def paw(s):
    """A paw print: the pad and four toes (list of polygons), about s across."""
    out = [ellipse(0.30 * s, 0.24 * s, 28, 0, -0.12 * s)]
    for (dx, dy, rr) in ((-0.33, 0.12, 0.11), (-0.12, 0.28, 0.115), (0.12, 0.28, 0.115), (0.33, 0.12, 0.11)):
        out.append(ellipse(rr * s, rr * 1.15 * s, 20, dx * s, dy * s))
    return out


def fish(s):
    body = ellipse(0.36 * s, 0.22 * s, 32, -0.08 * s, 0)
    tail = [(0.20 * s, 0.0), (0.46 * s, -0.20 * s), (0.46 * s, 0.20 * s)]
    return [body, tail]


def transform2(pts, sx=1.0, sy=1.0, dx=0.0, dy=0.0, rot=0.0):
    c, s_ = math.cos(math.radians(rot)), math.sin(math.radians(rot))
    out = []
    for x, y in pts:
        x, y = x * sx, y * sy
        out.append((x * c - y * s_ + dx, x * s_ + y * c + dy))
    return out


# ---------------------------------------------------------------------------
# one sprite: build, render, clean
# ---------------------------------------------------------------------------

TIMES = {}


def _cleanup(objs):
    for ob in list(objs):
        try:
            for ch in list(ob.children):
                objs.append(ch)
        except ReferenceError:
            pass
    seen = set()
    for ob in objs:
        try:
            if ob.name in seen:
                continue
            seen.add(ob.name)
            bpy.data.objects.remove(ob, do_unlink=True)
        except ReferenceError:
            pass
    for ob in list(bpy.context.scene.objects):
        if ob.type == 'EMPTY' or (ob.type == 'LIGHT' and ob.name != 'sun'):
            bpy.data.objects.remove(ob, do_unlink=True)


def shoot(out, name, objs, passes=('color', 'z'), kind='prop', shadow=False, glow=0, samples=48,
          extra=None, anchor=None, size=None, margin=3, sh_sprite=False, keep=False):
    """Render `objs` as sprite `name` anchored at cell (0, 0) (or `anchor`).
    shadow: adds the shadow pass on z = 0. glow: pixels of pool around the
    fitted size for a glow pass (0 = none)."""
    t0 = time.time()
    A = anchor if anchor is not None else C.cell(0, 0, 0)
    ps = list(passes)
    if shadow and 'shadow' not in ps:
        ps.append('shadow')
    if glow and 'glow' not in ps:
        ps.append('glow')
    bpy.context.view_layer.update()
    if size is None and glow:
        w, h, ax, ay = C.fit(objs, A, margin, 0.0 if shadow else None)
        size = (w + 2 * glow, h + 2 * glow, ax + glow, ay + glow)
    info = C.render_sprite(out, name, objs, A, passes=tuple(ps), size=size, samples=samples,
                           shadow_z=0.0 if (shadow or glow) else None, kind=kind, extra=extra, margin=margin)
    TIMES[name] = time.time() - t0
    print('SPRITE %-28s %4dx%-4d %.1fs' % (name, info['w'], info['h'], TIMES[name]))
    if sh_sprite:
        t1 = time.time()
        i2 = C.render_sprite(out, name + '_sh', objs, A, passes=('shadow',), shadow_z=0.0, kind='shadow',
                             margin=margin)
        TIMES[name + '_sh'] = time.time() - t1
        print('SPRITE %-28s %4dx%-4d %.1fs' % (name + '_sh', i2['w'], i2['h'], TIMES[name + '_sh']))
    if not keep:
        _cleanup(list(objs))
    return info


def fade_edges(path, frac=0.35):
    """Fade a glow pool to black towards the image's border (the point
    lights' falloff never reaches zero; the sprite must not end in a hard
    rectangle)."""
    import numpy as np
    img = bpy.data.images.load(path, check_existing=False)
    w, h = img.size
    a = np.empty(w * h * img.channels, np.float32)
    img.pixels.foreach_get(a)
    ch = img.channels
    bpy.data.images.remove(img)
    a = a.reshape(h, w, ch)[::-1, :, :3]
    yy, xx = np.mgrid[0:h, 0:w]
    dx = np.minimum(xx + 0.5, w - xx - 0.5) / (w * frac)
    dy = np.minimum(yy + 0.5, h - yy - 0.5) / (h * frac)
    t = np.clip(np.minimum(dx, dy), 0, 1)
    win = t * t * (3 - 2 * t)
    out = np.round(np.clip(a * win[..., None], 0, 1) * 255)
    C.write_png(path, out.astype(np.uint8))
