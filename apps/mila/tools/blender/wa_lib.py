"""Mila - helpers for worlds_a.py (living, kitchen, garden).

Geometry built straight into the scene (every piece gets its material through
ml_common's registry, so the colour / z / shadow passes swap it) and the node
materials of sample.py, ported to C.mat(..., build=...).

Materials: pm(key, colour, ..., tex=...) registers a Principled material; tex
is one of the small recipes below (wood grain, yarn bands, periodic noise,
leafy voronoi, square tiles). Every texture that must tile from cell to cell
uses WORLD coordinates wrapped on a torus (period 1 m in X and Y), so two
cells side by side never show a seam.
"""
import math
import random

import bpy
import bmesh
from mathutils import Euler, Vector as V

import ml_common as C

TAU = 2.0 * math.pi


# ---------------------------------------------------------------------------
# node helpers
# ---------------------------------------------------------------------------

def _n(nt, kind, **inputs):
    n = nt.nodes.new(kind)
    for k, v in inputs.items():
        n.inputs[k].default_value = v
    return n


def _math(nt, op, a, b=None, c=None):
    m = nt.nodes.new('ShaderNodeMath')
    m.operation = op
    for i, v in enumerate((a, b, c)):
        if v is None:
            continue
        if isinstance(v, (int, float)):
            m.inputs[i].default_value = v
        else:
            nt.links.new(v, m.inputs[i])
    return m.outputs[0]


def _world_xyz(nt):
    geo = nt.nodes.new('ShaderNodeNewGeometry')
    sep = nt.nodes.new('ShaderNodeSeparateXYZ')
    nt.links.new(geo.outputs['Position'], sep.inputs[0])
    return sep.outputs['X'], sep.outputs['Y'], sep.outputs['Z']


def _obj_xyz(nt):
    tc = nt.nodes.new('ShaderNodeTexCoord')
    sep = nt.nodes.new('ShaderNodeSeparateXYZ')
    nt.links.new(tc.outputs['Object'], sep.inputs[0])
    return sep.outputs['X'], sep.outputs['Y'], sep.outputs['Z']


def torus_coords(nt, fx, fy, kz=0.0, seed=0.0):
    """4D coordinates periodic with 1 m in world X and Y: (vector, w).
    fx, fy ~ how many noise features per metre along X and Y. kz adds world
    Z into W (fronts of walls get a pattern that varies with height)."""
    x, y, z = _world_xyz(nt)
    rx, ry = fx / TAU, fy / TAU
    ax = _math(nt, 'MULTIPLY', x, TAU)
    ay = _math(nt, 'MULTIPLY', y, TAU)
    cx = _math(nt, 'MULTIPLY', _math(nt, 'COSINE', ax), rx)
    sx = _math(nt, 'MULTIPLY', _math(nt, 'SINE', ax), rx)
    cy = _math(nt, 'ADD', _math(nt, 'MULTIPLY', _math(nt, 'COSINE', ay), ry), seed)
    sy = _math(nt, 'MULTIPLY', _math(nt, 'SINE', ay), ry)
    if kz:
        sy = _math(nt, 'ADD', sy, _math(nt, 'MULTIPLY', z, kz))
    comb = nt.nodes.new('ShaderNodeCombineXYZ')
    nt.links.new(cx, comb.inputs[0])
    nt.links.new(sx, comb.inputs[1])
    nt.links.new(cy, comb.inputs[2])
    return comb.outputs[0], sy


def _ramp(nt, fac, stops):
    """stops: [(pos, (r, g, b)), ...]"""
    r = nt.nodes.new('ShaderNodeValToRGB')
    els = r.color_ramp.elements
    while len(els) < len(stops):
        els.new(0.5)
    for e, (p, c) in zip(els, stops):
        e.position = p
        e.color = tuple(c) + (1,)
    nt.links.new(fac, r.inputs['Fac'])
    return r.outputs['Color']


def _mul(c, k):
    return tuple(min(1.0, v * k) for v in c)


# ---------------------------------------------------------------------------
# texture recipes: f(nt, col) -> (colour socket or None, height socket or None,
# bump strength, bump distance)
# ---------------------------------------------------------------------------

def tex_wood(scale=1.0, stretch=10.0, dark=0.78, offset=(0, 0, 0)):
    """Sample.py's wood: noise stretched along X (the grain), object space."""
    def f(nt, col):
        tc = nt.nodes.new('ShaderNodeTexCoord')
        mp = nt.nodes.new('ShaderNodeMapping')
        mp.inputs['Scale'].default_value = (1.0, stretch, 1.0)
        mp.inputs['Location'].default_value = offset
        nt.links.new(tc.outputs['Object'], mp.inputs['Vector'])
        nz = _n(nt, 'ShaderNodeTexNoise', Scale=2.0 * scale, Detail=3.0)
        nt.links.new(mp.outputs['Vector'], nz.inputs['Vector'])
        c = _ramp(nt, nz.outputs['Fac'], [(0.35, _mul(col, dark)), (0.65, col)])
        return c, None, 0, 0
    return f


def tex_twood(fx=1.0, fy=14.0, dark=0.75, kz=0.0, seed=0.0):
    """Wood grain along X that tiles with period 1 m (wall tops, worktops)."""
    def f(nt, col):
        v, w = torus_coords(nt, fx, fy, kz, seed)
        nz = _n(nt, 'ShaderNodeTexNoise', Scale=1.0, Detail=4.0, Roughness=0.55)
        nz.noise_dimensions = '4D'
        nt.links.new(v, nz.inputs['Vector'])
        nt.links.new(w, nz.inputs['W'])
        c = _ramp(nt, nz.outputs['Fac'], [(0.3, _mul(col, dark)), (0.7, col)])
        return c, None, 0, 0
    return f


def tex_tnoise(col2, f=8.0, detail=4.0, lo=0.3, hi=0.7, bump=0.0, dist=0.01, kz=0.0, seed=0.0):
    """Periodic noise between col (low) and col2 (high), with optional bump."""
    def fn(nt, col):
        v, w = torus_coords(nt, f, f, kz, seed)
        nz = _n(nt, 'ShaderNodeTexNoise', Scale=1.0, Detail=detail, Roughness=0.6)
        nz.noise_dimensions = '4D'
        nt.links.new(v, nz.inputs['Vector'])
        nt.links.new(w, nz.inputs['W'])
        c = _ramp(nt, nz.outputs['Fac'], [(lo, col), (hi, col2)])
        return c, (nz.outputs['Fac'] if bump else None), bump, dist
    return fn


def tex_leaves(cols, f=16.0, bump=0.8, dist=0.02, kz=0.0, seed=0.0, fine=40.0, zshade=None):
    """Leafy: periodic voronoi cells, each a leaf of one of `cols` (dark to
    light), bumped by the distance to the cell centre, plus a fine noise."""
    def fn(nt, col):
        v, w = torus_coords(nt, f, f, kz, seed)
        vo = _n(nt, 'ShaderNodeTexVoronoi', Scale=1.0, Randomness=1.0)
        vo.voronoi_dimensions = '4D'
        vo.feature = 'F1'
        nt.links.new(v, vo.inputs['Vector'])
        nt.links.new(w, vo.inputs['W'])
        sep = nt.nodes.new('ShaderNodeSeparateRGB')
        nt.links.new(vo.outputs['Color'], sep.inputs[0])
        v2, w2 = torus_coords(nt, fine, fine, kz * fine / f, seed + 3.0)
        nz = _n(nt, 'ShaderNodeTexNoise', Scale=1.0, Detail=2.0)
        nz.noise_dimensions = '4D'
        nt.links.new(v2, nz.inputs['Vector'])
        nt.links.new(w2, nz.inputs['W'])
        # leaf tone: cell random + a little distance shading (edges darker)
        d = _math(nt, 'MULTIPLY', vo.outputs['Distance'], 0.8)
        t = _math(nt, 'SUBTRACT', sep.outputs['R'], d)
        t = _math(nt, 'ADD', t, _math(nt, 'MULTIPLY', nz.outputs['Fac'], 0.35))
        n = len(cols)
        c = _ramp(nt, t, [(-0.25 + 1.1 * k / (n - 1), cols[k]) for k in range(n)])
        if zshade is not None:
            # darker towards the ground: (z0, z1, factor at z0)
            z0, z1, k0 = zshade
            x_, y_, z_ = _world_xyz(nt)
            mr = nt.nodes.new('ShaderNodeMapRange')
            mr.inputs['From Min'].default_value = z0
            mr.inputs['From Max'].default_value = z1
            mr.inputs['To Min'].default_value = k0
            mr.inputs['To Max'].default_value = 1.0
            nt.links.new(z_, mr.inputs['Value'])
            mx = nt.nodes.new('ShaderNodeMixRGB')
            mx.blend_type = 'MULTIPLY'
            mx.inputs['Fac'].default_value = 1.0
            nt.links.new(c, mx.inputs['Color1'])
            cmb = nt.nodes.new('ShaderNodeCombineRGB')
            for i in range(3):
                nt.links.new(mr.outputs['Result'], cmb.inputs[i])
            nt.links.new(cmb.outputs[0], mx.inputs['Color2'])
            c = mx.outputs[0]
        h = _math(nt, 'SUBTRACT', 1.0, vo.outputs['Distance'])
        h = _math(nt, 'ADD', h, _math(nt, 'MULTIPLY', nz.outputs['Fac'], 0.3))
        return c, h, bump, dist
    return fn


def tex_yarn(scale=7.0):
    """Sample.py's yarn: wave bands of wound thread, object space."""
    def f(nt, col):
        wv = _n(nt, 'ShaderNodeTexWave', Scale=scale, Distortion=3.0, Detail=1.0)
        tc = nt.nodes.new('ShaderNodeTexCoord')
        nt.links.new(tc.outputs['Object'], wv.inputs['Vector'])
        c = _ramp(nt, wv.outputs['Fac'], [(0.25, _mul(col, 0.55)), (0.75, _mul(col, 1.1))])
        return c, wv.outputs['Fac'], 0.6, 0.02
    return f


def tex_tiles(mortar_col, bw=0.25, rh=0.125, mortar=0.012, var=0.06, plane='xz', bump=0.4):
    """Square wall tiles (brick texture, no offset) in world X and Z (a wall
    front) or X and Y (a floor): the tile width must divide 1 m."""
    def f(nt, col):
        x, y, z = _world_xyz(nt)
        comb = nt.nodes.new('ShaderNodeCombineXYZ')
        nt.links.new(x, comb.inputs[0])
        nt.links.new(z if plane == 'xz' else y, comb.inputs[1])
        br = nt.nodes.new('ShaderNodeTexBrick')
        br.offset = 0.0
        br.squash = 1.0
        br.inputs['Scale'].default_value = 1.0
        br.inputs['Brick Width'].default_value = bw
        br.inputs['Row Height'].default_value = rh
        br.inputs['Mortar Size'].default_value = mortar
        br.inputs['Mortar Smooth'].default_value = 0.3
        br.inputs['Bias'].default_value = 0.0
        br.inputs['Color1'].default_value = tuple(_mul(col, 1.0 - var)) + (1,)
        br.inputs['Color2'].default_value = tuple(_mul(col, 1.0 + var * 0.5)) + (1,)
        br.inputs['Mortar'].default_value = tuple(mortar_col) + (1,)
        nt.links.new(comb.outputs[0], br.inputs['Vector'])
        h = _math(nt, 'SUBTRACT', 1.0, br.outputs['Fac'])
        return br.outputs['Color'], h, bump, 0.004
    return f


def tex_zgrad(col_top, z0, z1):
    """Colour from col (at z0) to col_top (at z1) along world Z."""
    def f(nt, col):
        x, y, z = _world_xyz(nt)
        mr = nt.nodes.new('ShaderNodeMapRange')
        mr.inputs['From Min'].default_value = z0
        mr.inputs['From Max'].default_value = z1
        nt.links.new(z, mr.inputs['Value'])
        c = _ramp(nt, mr.outputs['Result'], [(0.0, col), (1.0, col_top)])
        return c, None, 0, 0
    return f


def tex_mix(tex_a, tex_b_col_fn):
    """tex_a gives a colour; tex_b_col_fn(nt, colour_socket) post-processes it."""
    def f(nt, col):
        c, h, s, d = tex_a(nt, col)
        return tex_b_col_fn(nt, c), h, s, d
    return f


# ---------------------------------------------------------------------------
# materials
# ---------------------------------------------------------------------------

def pm(key, col, rough=0.6, spec=0.3, metal=0.0, emit=None, es=0.0, sheen=0.0, cc=0.0,
       tex=None, id=0, ccr=0.1):
    """A Principled material registered in ml_common (colour and light passes
    both built from the same recipe; light = grey 0.8, same bump/gloss)."""
    if key in C._MATS:
        return key

    def build(nt, neutral):
        b = nt.nodes.new('ShaderNodeBsdfPrincipled')
        b.inputs['Base Color'].default_value = ((0.8, 0.8, 0.8) if neutral else tuple(col)) + (1,)
        b.inputs['Roughness'].default_value = rough
        b.inputs['Specular'].default_value = spec
        b.inputs['Metallic'].default_value = metal
        b.inputs['Sheen'].default_value = sheen
        b.inputs['Sheen Tint'].default_value = 0.5
        b.inputs['Clearcoat'].default_value = cc
        b.inputs['Clearcoat Roughness'].default_value = ccr
        if emit is not None and es > 0:
            b.inputs['Emission'].default_value = tuple(emit) + (1,)
            b.inputs['Emission Strength'].default_value = es
        if tex is not None:
            c, h, s, d = tex(nt, col)
            if c is not None and not neutral:
                nt.links.new(c, b.inputs['Base Color'])
            if h is not None and s > 0:
                bu = _n(nt, 'ShaderNodeBump', Strength=s, Distance=d)
                nt.links.new(h, bu.inputs['Height'])
                nt.links.new(bu.outputs['Normal'], b.inputs['Normal'])
        return b.outputs['BSDF']
    C.mat(key, base=col, rough=rough, id=id, build=build)
    return key


def wood(key, col, scale=1.0, offset=(0, 0, 0), rough=0.55, cc=0.15):
    return pm(key, col, rough=rough, spec=0.35, cc=cc, tex=tex_wood(scale, offset=offset))


# ---------------------------------------------------------------------------
# geometry (every function returns the object(s); `key` is a registered
# material key)
# ---------------------------------------------------------------------------

def _obj(name, me, key=None, smooth=True):
    ob = bpy.data.objects.new(name, me)
    C.link(ob)
    if key is not None:
        C.assign(ob, key)
    if smooth:
        for p in me.polygons:
            p.use_smooth = True
    return ob


def _xf(ob, c, scale=(1, 1, 1), rot=(0, 0, 0)):
    ob.scale = scale
    ob.rotation_euler = Euler([math.radians(a) for a in rot], 'XYZ')
    ob.location = c
    return ob


def ellipsoid(name, c, size, key, rot=(0, 0, 0), seg=28, rings=18):
    me = bpy.data.meshes.new(name)
    bm = bmesh.new()
    bmesh.ops.create_uvsphere(bm, u_segments=seg, v_segments=rings, radius=1.0)
    bm.to_mesh(me)
    bm.free()
    return _xf(_obj(name, me, key), c, size, rot)


def cone(name, c, r1, r2, h, key, rot=(0, 0, 0), verts=32, scale=(1, 1, 1), smooth=True, bevel=0.0):
    """Cylinder / cone standing on c (its base), height h."""
    me = bpy.data.meshes.new(name)
    bm = bmesh.new()
    bmesh.ops.create_cone(bm, cap_ends=True, segments=verts, radius1=r1, radius2=r2, depth=h)
    bmesh.ops.translate(bm, verts=bm.verts, vec=(0, 0, h / 2))
    bm.to_mesh(me)
    bm.free()
    ob = _obj(name, me, key, smooth)
    if bevel > 0:
        md = ob.modifiers.new('bv', 'BEVEL')
        md.width = bevel
        md.segments = 3
        md.limit_method = 'ANGLE'
        md.angle_limit = math.radians(50)
        me.use_auto_smooth = True
        me.auto_smooth_angle = math.radians(40)
        md.harden_normals = False
    return _xf(ob, c, scale, rot)


def torus(name, c, R, r, key, rot=(0, 0, 0), scale=(1, 1, 1), n=48, m=14):
    me = bpy.data.meshes.new(name)
    bm = bmesh.new()
    vs = []
    for i in range(n):
        a = TAU * i / n
        for j in range(m):
            b = TAU * j / m
            vs.append(bm.verts.new(((R + r * math.cos(b)) * math.cos(a),
                                    (R + r * math.cos(b)) * math.sin(a), r * math.sin(b))))
    for i in range(n):
        for j in range(m):
            bm.faces.new((vs[i * m + j], vs[((i + 1) % n) * m + j],
                          vs[((i + 1) % n) * m + (j + 1) % m], vs[i * m + (j + 1) % m]))
    bm.to_mesh(me)
    bm.free()
    return _xf(_obj(name, me, key), c, scale, rot)


def box(name, x0, y0, z0, x1, y1, z1, key, bevel=0.0, segments=3):
    me = bpy.data.meshes.new(name)
    v = [(x0, y0, z0), (x1, y0, z0), (x1, y1, z0), (x0, y1, z0),
         (x0, y0, z1), (x1, y0, z1), (x1, y1, z1), (x0, y1, z1)]
    f = [(0, 3, 2, 1), (4, 5, 6, 7), (0, 1, 5, 4), (1, 2, 6, 5), (2, 3, 7, 6), (3, 0, 4, 7)]
    me.from_pydata(v, [], f)
    ob = _obj(name, me, key, smooth=False)
    if bevel > 0:
        md = ob.modifiers.new('bv', 'BEVEL')
        md.width = bevel
        md.segments = segments
        for p in me.polygons:
            p.use_smooth = True
        me.use_auto_smooth = True
        me.auto_smooth_angle = math.radians(35)
    return ob


def rbox(name, x0, y0, z0, x1, y1, z1, key, r):
    """A box with round edges of radius r (a soft toy-like block)."""
    return box(name, x0, y0, z0, x1, y1, z1, key, bevel=r, segments=5)


def prism(name, pts, z0, z1, key, bevel=0.0, smooth=False):
    """Extrude a closed polygon (x, y points, counter-clockwise) from z0 to z1."""
    n = len(pts)
    v = [(x, y, z0) for x, y in pts] + [(x, y, z1) for x, y in pts]
    f = [tuple(range(n - 1, -1, -1)), tuple(range(n, 2 * n))]
    for i in range(n):
        j = (i + 1) % n
        f.append((i, j, n + j, n + i))
    me = bpy.data.meshes.new(name)
    me.from_pydata(v, [], f)
    ob = _obj(name, me, key, smooth=smooth)
    if bevel > 0:
        md = ob.modifiers.new('bv', 'BEVEL')
        md.width = bevel
        md.segments = 3
        md.limit_method = 'ANGLE'
        md.angle_limit = math.radians(40)
        me.use_auto_smooth = True
        me.auto_smooth_angle = math.radians(35)
    return ob


def disc(name, c, r, h, key, seg=48, bevel=0.0, jitter=0.0, seed=0, scale=(1, 1, 1)):
    """A flat round slab (optionally with a wobbly outline) standing on c."""
    rnd = random.Random(seed)
    ph = [rnd.uniform(0, TAU) for _ in range(3)]
    pts = []
    for i in range(seg):
        a = TAU * i / seg
        rr = r * (1 + jitter * (0.5 * math.sin(3 * a + ph[0]) + 0.3 * math.sin(5 * a + ph[1])
                                + 0.2 * math.sin(7 * a + ph[2])))
        pts.append((c[0] + rr * math.cos(a) * scale[0], c[1] + rr * math.sin(a) * scale[1]))
    return prism(name, pts, c[2], c[2] + h, key, bevel=bevel, smooth=True)


def tube(name, pts, radii, key, res=10, bev=4):
    """A round tube through pts (NURBS), converted to a mesh at once so the
    sprite fitting sees it."""
    cu = bpy.data.curves.new(name, 'CURVE')
    cu.dimensions = '3D'
    cu.bevel_depth = 1.0
    cu.bevel_resolution = bev
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
    bpy.context.view_layer.update()
    dg = bpy.context.evaluated_depsgraph_get()
    me = bpy.data.meshes.new_from_object(tmp.evaluated_get(dg))
    bpy.data.objects.remove(tmp, do_unlink=True)
    bpy.data.curves.remove(cu)
    me.materials.clear()
    return _obj(name, me, key)


def rotate_group(objs, pivot, rotz_deg):
    """Rotate a set of objects about a vertical axis through pivot."""
    root = bpy.data.objects.new('pivot', None)
    C.link(root)
    root.location = pivot
    bpy.context.view_layer.update()
    for o in objs:
        mw = o.matrix_world.copy()
        o.parent = root
        o.matrix_parent_inverse = root.matrix_world.inverted()
        o.matrix_world = mw
    root.rotation_euler = (0, 0, math.radians(rotz_deg))
    bpy.context.view_layer.update()
    return root


def clear_scene_objects(keep=('ml_cam', 'sun', 'ml_ground')):
    for ob in list(bpy.context.scene.objects):
        if ob.name in keep:
            continue
        bpy.data.objects.remove(ob, do_unlink=True)
    for me in list(bpy.data.meshes):
        if me.users == 0:
            bpy.data.meshes.remove(me)
    for lt in list(bpy.data.lights):
        if lt.users == 0:
            bpy.data.lights.remove(lt)
