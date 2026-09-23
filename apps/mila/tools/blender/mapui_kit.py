"""Mila - materials and shapes shared by map.py and ui.py.

sample.py's helpers, ported to ml_common's material registry (C.mat with a
build= function, C.assign), plus Mila's model (sample.py's mila(), no
accessories) for the map marker. Import after ml_common:

    import ml_common as C
    import mapui_kit as K
"""
import math
import random

import bpy
import bmesh
from mathutils import Euler, Matrix, Vector as V

import ml_common as C


# ---------------------------------------------------------------------------
# materials
# ---------------------------------------------------------------------------

def _bsdf(nt, col, rough, spec, metal, sheen, cc, neutral, emit=None, es=0.0):
    b = nt.nodes.new('ShaderNodeBsdfPrincipled')
    b.inputs['Base Color'].default_value = tuple((0.8, 0.8, 0.8) if neutral else col) + (1,)
    b.inputs['Roughness'].default_value = rough
    b.inputs['Specular'].default_value = spec
    b.inputs['Metallic'].default_value = metal
    b.inputs['Sheen'].default_value = sheen
    b.inputs['Clearcoat'].default_value = cc
    if emit is not None and es > 0 and not neutral:
        b.inputs['Emission'].default_value = tuple(emit) + (1,)
        b.inputs['Emission Strength'].default_value = es
    return b


def pm(key, col, rough=0.6, spec=0.3, metal=0.0, emit=None, es=0.0, sheen=0.0, cc=0.0, id=0):
    """A plain principled material (sample.py's principled())."""
    if key in C._MATS:
        return key

    def build(nt, neutral):
        return _bsdf(nt, col, rough, spec, metal, sheen, cc, neutral, emit, es).outputs['BSDF']
    return C.mat(key, base=col, rough=rough, metal=metal, id=id, build=build)


def _ramp(nt, fac, c0, c1, p0=0.35, p1=0.65):
    r = nt.nodes.new('ShaderNodeValToRGB')
    r.color_ramp.elements[0].color = tuple(c0) + (1,)
    r.color_ramp.elements[1].color = tuple(c1) + (1,)
    r.color_ramp.elements[0].position = p0
    r.color_ramp.elements[1].position = p1
    nt.links.new(fac, r.inputs['Fac'])
    return r.outputs['Color']


def _coords(nt, scale=(1, 1, 1), kind='Object', rot=(0, 0, 0), loc=(0, 0, 0)):
    tc = nt.nodes.new('ShaderNodeTexCoord')
    mp = nt.nodes.new('ShaderNodeMapping')
    mp.inputs['Scale'].default_value = scale
    mp.inputs['Rotation'].default_value = tuple(math.radians(v) for v in rot)
    mp.inputs['Location'].default_value = loc
    nt.links.new(tc.outputs[kind], mp.inputs['Vector'])
    return mp.outputs['Vector']


def checker(key, c0, c1, size=0.5, rough=0.45, cc=0.25):
    """Square tiles of two colours, `size` metres, aligned on the grid."""
    if key in C._MATS:
        return key

    def build(nt, neutral):
        b = _bsdf(nt, c0, rough, 0.4, 0.0, 0.0, cc, neutral)
        if neutral:
            return b.outputs['BSDF']
        ch = nt.nodes.new('ShaderNodeTexChecker')
        ch.inputs['Scale'].default_value = 1.0 / size
        ch.inputs['Color1'].default_value = tuple(c0) + (1,)
        ch.inputs['Color2'].default_value = tuple(c1) + (1,)
        nt.links.new(_coords(nt, loc=(0.0005, 0.0005, size / 2)), ch.inputs['Vector'])
        br = nt.nodes.new('ShaderNodeTexBrick')          # thin grout lines
        br.offset = 0.0
        br.inputs['Scale'].default_value = 1.0
        br.inputs['Brick Width'].default_value = size
        br.inputs['Row Height'].default_value = size
        br.inputs['Mortar Size'].default_value = 0.012
        br.inputs['Color1'].default_value = (1, 1, 1, 1)
        br.inputs['Color2'].default_value = (1, 1, 1, 1)
        br.inputs['Mortar'].default_value = (0.8, 0.8, 0.8, 1)
        nt.links.new(_coords(nt), br.inputs['Vector'])
        mix = nt.nodes.new('ShaderNodeMixRGB')
        mix.blend_type = 'MULTIPLY'
        mix.inputs['Fac'].default_value = 1.0
        nt.links.new(ch.outputs['Color'], mix.inputs['Color1'])
        nt.links.new(br.outputs['Color'], mix.inputs['Color2'])
        nt.links.new(mix.outputs['Color'], b.inputs['Base Color'])
        return b.outputs['BSDF']
    return C.mat(key, base=c0, build=build)


def tiles(key, col, grout, w=0.2, h=0.2, offset=0.0, plane='XY', col2=None, rough=0.45, cc=0.25):
    """Tiles, bricks or boards: a brick texture on the XY plane (floors) or
    the XZ plane (walls facing -Y)."""
    if key in C._MATS:
        return key

    def build(nt, neutral):
        b = _bsdf(nt, col, rough, 0.35, 0.0, 0.0, cc, neutral)
        if neutral:
            return b.outputs['BSDF']
        br = nt.nodes.new('ShaderNodeTexBrick')
        br.offset = offset
        br.offset_frequency = 2
        br.inputs['Scale'].default_value = 1.0
        br.inputs['Brick Width'].default_value = w
        br.inputs['Row Height'].default_value = h
        br.inputs['Mortar Size'].default_value = 0.014
        br.inputs['Mortar Smooth'].default_value = 0.2
        br.inputs['Color1'].default_value = tuple(col) + (1,)
        br.inputs['Color2'].default_value = tuple(col2 or [c * 0.93 for c in col]) + (1,)
        br.inputs['Mortar'].default_value = tuple(grout) + (1,)
        nt.links.new(_coords(nt, rot=(90, 0, 0) if plane == 'XZ' else (0, 0, 0)), br.inputs['Vector'])
        nt.links.new(br.outputs['Color'], b.inputs['Base Color'])
        return b.outputs['BSDF']
    return C.mat(key, base=col, build=build)


def wood(key, col, scale=1.0, grain=(1.0, 10.0, 1.0), rough=0.55, cc=0.15):
    """sample.py's wood_mat: noise stretched into a grain along X."""
    if key in C._MATS:
        return key

    def build(nt, neutral):
        b = _bsdf(nt, col, rough, 0.35, 0.0, 0.0, cc, neutral)
        if not neutral:
            nz = nt.nodes.new('ShaderNodeTexNoise')
            nz.inputs['Scale'].default_value = 2.0 * scale
            nz.inputs['Detail'].default_value = 3.0
            nt.links.new(_coords(nt, grain), nz.inputs['Vector'])
            nt.links.new(_ramp(nt, nz.outputs['Fac'], [c * 0.78 for c in col], col), b.inputs['Base Color'])
        return b.outputs['BSDF']
    return C.mat(key, base=col, build=build)


def planks(key, col, row=0.34, length=1.7, gap=0.018, rough=0.55):
    """Floor boards along X (a brick texture: staggered ends, dark seams)."""
    if key in C._MATS:
        return key

    def build(nt, neutral):
        b = _bsdf(nt, col, rough, 0.35, 0.0, 0.0, 0.12, neutral)
        if neutral:
            return b.outputs['BSDF']
        br = nt.nodes.new('ShaderNodeTexBrick')
        br.offset = 0.5
        br.offset_frequency = 2
        br.inputs['Scale'].default_value = 1.0
        br.inputs['Brick Width'].default_value = length
        br.inputs['Row Height'].default_value = row
        br.inputs['Mortar Size'].default_value = gap
        br.inputs['Mortar Smooth'].default_value = 0.2
        br.inputs['Bias'].default_value = 0.0
        br.inputs['Color1'].default_value = tuple(c * 1.06 for c in col) + (1,)
        br.inputs['Color2'].default_value = tuple(c * 0.86 for c in col) + (1,)
        br.inputs['Mortar'].default_value = tuple(c * 0.45 for c in col) + (1,)
        v = _coords(nt, (1, 1, 1))
        nt.links.new(v, br.inputs['Vector'])
        # grain on top
        nz = nt.nodes.new('ShaderNodeTexNoise')
        nz.inputs['Scale'].default_value = 3.0
        nz.inputs['Detail'].default_value = 3.0
        nt.links.new(_coords(nt, (1.0, 12.0, 1.0)), nz.inputs['Vector'])
        mix = nt.nodes.new('ShaderNodeMixRGB')
        mix.blend_type = 'MULTIPLY'
        mix.inputs['Fac'].default_value = 1.0
        nt.links.new(br.outputs['Color'], mix.inputs['Color1'])
        nt.links.new(_ramp(nt, nz.outputs['Fac'], (0.86, 0.86, 0.86), (1, 1, 1)), mix.inputs['Color2'])
        nt.links.new(mix.outputs['Color'], b.inputs['Base Color'])
        return b.outputs['BSDF']
    return C.mat(key, base=col, build=build)


def stripes(key, c0, c1, width=0.18, axis='X', rough=0.8):
    """Wallpaper stripes (bands across `axis`)."""
    if key in C._MATS:
        return key

    def build(nt, neutral):
        b = _bsdf(nt, c0, rough, 0.3, 0.0, 0.0, 0.0, neutral)
        if neutral:
            return b.outputs['BSDF']
        wv = nt.nodes.new('ShaderNodeTexWave')
        wv.wave_type = 'BANDS'
        wv.bands_direction = axis
        wv.inputs['Scale'].default_value = 1.0 / (width * 2)
        wv.inputs['Distortion'].default_value = 0.0
        nt.links.new(_coords(nt, (1, 1, 1)), wv.inputs['Vector'])
        nt.links.new(_ramp(nt, wv.outputs['Fac'], c0, c1, 0.48, 0.52), b.inputs['Base Color'])
        return b.outputs['BSDF']
    return C.mat(key, base=c0, build=build)


def mottled(key, c0, c1, scale=3.0, rough=0.8, sheen=0.0, p=(0.35, 0.65), detail=3.0):
    """Two colours through a noise (grass, stone, soil)."""
    if key in C._MATS:
        return key

    def build(nt, neutral):
        b = _bsdf(nt, c0, rough, 0.3, 0.0, sheen, 0.0, neutral)
        if neutral:
            return b.outputs['BSDF']
        nz = nt.nodes.new('ShaderNodeTexNoise')
        nz.inputs['Scale'].default_value = scale
        nz.inputs['Detail'].default_value = detail
        nt.links.new(_coords(nt, (1, 1, 1)), nz.inputs['Vector'])
        nt.links.new(_ramp(nt, nz.outputs['Fac'], c0, c1, p[0], p[1]), b.inputs['Base Color'])
        return b.outputs['BSDF']
    return C.mat(key, base=c0, build=build)


def yarn(key, col):
    """sample.py's yarn_mat: wave bands (the wound thread) on a woolly colour."""
    if key in C._MATS:
        return key

    def build(nt, neutral):
        b = _bsdf(nt, col, 0.9, 0.2, 0.0, 0.8, 0.0, neutral)
        wv = nt.nodes.new('ShaderNodeTexWave')
        wv.inputs['Scale'].default_value = 7.0
        wv.inputs['Distortion'].default_value = 3.0
        wv.inputs['Detail'].default_value = 1.0
        nt.links.new(_coords(nt, (1, 1, 1)), wv.inputs['Vector'])
        if not neutral:
            nt.links.new(_ramp(nt, wv.outputs['Fac'], [c * 0.55 for c in col], [min(1, c * 1.1) for c in col],
                               0.25, 0.75), b.inputs['Base Color'])
        bump = nt.nodes.new('ShaderNodeBump')
        bump.inputs['Strength'].default_value = 0.6
        bump.inputs['Distance'].default_value = 0.02
        nt.links.new(wv.outputs['Fac'], bump.inputs['Height'])
        nt.links.new(bump.outputs['Normal'], b.inputs['Normal'])
        return b.outputs['BSDF']
    return C.mat(key, base=col, build=build)


def fur(key='fur'):
    """sample.py's fur_black: nearly black with a cool rim sheen (facing
    ratio emission) so Mila reads on the black screen."""
    if key in C._MATS:
        return key

    def build(nt, neutral):
        b = _bsdf(nt, (0.020, 0.019, 0.025), 0.58, 0.30, 0.0, 1.0, 0.0, neutral)
        b.inputs['Sheen Tint'].default_value = 0.0
        lw = nt.nodes.new('ShaderNodeLayerWeight')
        lw.inputs['Blend'].default_value = 0.35
        col = _ramp(nt, lw.outputs['Facing'], (0, 0, 0), (0.16, 0.18, 0.30), 0.45, 1.0)
        em = nt.nodes.new('ShaderNodeEmission')
        em.inputs['Strength'].default_value = 0.9
        nt.links.new(col, em.inputs['Color'])
        add = nt.nodes.new('ShaderNodeAddShader')
        nt.links.new(b.outputs['BSDF'], add.inputs[0])
        nt.links.new(em.outputs['Emission'], add.inputs[1])
        return add.outputs['Shader']
    return C.mat(key, base=(0.02, 0.02, 0.025), build=build)


def catcher(name, c, r, z=0.0):
    """An invisible shadow-catching disc (a soft contact shadow inside a
    colour-only sprite)."""
    me = bpy.data.meshes.new(name)
    bm = bmesh.new()
    bmesh.ops.create_circle(bm, cap_ends=True, segments=40, radius=r)
    bm.to_mesh(me)
    bm.free()
    ob = bpy.data.objects.new(name, me)
    C.link(ob)
    ob.location = (c[0], c[1], z)
    ob.is_shadow_catcher = True
    pm('catch', (0.8, 0.8, 0.8))
    C.assign(ob, 'catch')
    return ob


# ---------------------------------------------------------------------------
# shapes (sample.py's, with material keys)
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


def cone(name, c, r1, r2, h, key, rot=(0, 0, 0), verts=24, scale=(1, 1, 1), smooth=True, bevel=0.0):
    me = bpy.data.meshes.new(name)
    bm = bmesh.new()
    bmesh.ops.create_cone(bm, cap_ends=True, segments=verts, radius1=r1, radius2=r2, depth=h)
    bmesh.ops.translate(bm, verts=bm.verts, vec=(0, 0, h / 2))
    bm.to_mesh(me)
    bm.free()
    ob = _obj(name, me, key, smooth)
    ob.scale = scale
    ob.rotation_euler = Euler([math.radians(a) for a in rot], 'XYZ')
    ob.location = c
    if smooth:              # flat caps: without it the caps shade dark
        me.use_auto_smooth = True
        me.auto_smooth_angle = math.radians(40)
    if bevel > 0:
        md = ob.modifiers.new('bv', 'BEVEL')
        md.width = bevel
        md.segments = 4
        md.limit_method = 'ANGLE'
        me.use_auto_smooth = True
        me.auto_smooth_angle = math.radians(40)
    return ob


def torus(name, c, R, r, key, rot=(0, 0, 0), scale=(1, 1, 1), n=40, m=14, arc=1.0):
    me = bpy.data.meshes.new(name)
    bm = bmesh.new()
    vs = []
    closed = arc >= 1.0
    nn = n if closed else n + 1
    for i in range(nn):
        a = 2 * math.pi * arc * i / n
        for j in range(m):
            b = 2 * math.pi * j / m
            vs.append(bm.verts.new(((R + r * math.cos(b)) * math.cos(a),
                                    (R + r * math.cos(b)) * math.sin(a), r * math.sin(b))))
    for i in range(n if closed else n):
        i2 = (i + 1) % nn
        for j in range(m):
            bm.faces.new((vs[i * m + j], vs[i2 * m + j], vs[i2 * m + (j + 1) % m], vs[i * m + (j + 1) % m]))
    bm.to_mesh(me)
    bm.free()
    ob = _obj(name, me, key)
    ob.scale = scale
    ob.rotation_euler = Euler([math.radians(a) for a in rot], 'XYZ')
    ob.location = c
    return ob


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
    return ob


def tube(name, pts, radii, key, res=10):
    """A tube through pts with per-point radius (tails, threads)."""
    cu = bpy.data.curves.new(name, 'CURVE')
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
    ob = bpy.data.objects.new(name, cu)
    C.link(ob)
    if key:
        C.assign(ob, key)
    return ob


def merge_skin(objs, name, key, voxel=0.0065, smooth=6, factor=0.6):
    """Fuse overlapping parts into one smooth skin (voxel remesh + smooth)."""
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
    ob = _obj(name, me, None)
    rm = ob.modifiers.new('remesh', 'REMESH')
    rm.mode = 'VOXEL'
    rm.voxel_size = voxel
    rm.use_smooth_shade = True
    sm = ob.modifiers.new('smooth', 'SMOOTH')
    sm.factor = factor
    sm.iterations = smooth
    C.assign(ob, key)
    return ob


def capsule(name, p0, p1, r, key, seg=20):
    """A cylinder with round ends between p0 and p1 (letters' strokes)."""
    p0, p1 = V(p0), V(p1)
    d = p1 - p0
    L = d.length
    o = [ellipsoid(name + 'a', p0, (r, r, r), key, seg=seg, rings=12),
         ellipsoid(name + 'b', p1, (r, r, r), key, seg=seg, rings=12)]
    if L > 1e-6:
        cy = cone(name + 'c', (0, 0, 0), r, r, L, key, verts=seg)
        q = d.normalized().to_track_quat('Z', 'Y')
        cy.matrix_world = Matrix.Translation(p0) @ q.to_matrix().to_4x4()
        o.append(cy)
    return o


def parent_all(objs, name, loc=(0, 0, 0), rotz=0.0, scale=1.0):
    root = bpy.data.objects.new(name, None)
    C.link(root)
    for o in objs:
        o.parent = root
    root.location = loc
    root.rotation_euler = (0, 0, math.radians(rotz))
    root.scale = (scale, scale, scale)
    return root


def paw(name, c, ang, s, key, z=0.0, t=0.006, seg=12):
    """A flat paw print lying on the plane z (toes towards angle `ang`, rad)."""
    ca, sa = math.cos(ang - math.pi / 2), math.sin(ang - math.pi / 2)

    def at(dx, dy):
        return (c[0] + (dx * ca - dy * sa) * s, c[1] + (dx * sa + dy * ca) * s, z)
    o = [ellipsoid(name + 'p', at(0, -0.03), (0.085 * s, 0.07 * s, t), key, rot=(0, 0, math.degrees(ang - math.pi / 2)),
                   seg=seg, rings=6)]
    for k, (dx, dy) in enumerate(((-0.1, 0.055), (-0.037, 0.11), (0.037, 0.11), (0.1, 0.055))):
        o.append(ellipsoid(name + 't%d' % k, at(dx, dy), (0.034 * s, 0.038 * s, t), key,
                           rot=(0, 0, math.degrees(ang - math.pi / 2)), seg=seg, rings=6))
    return o


def paw_upright(name, c, s, key, depth=0.05, tilt=0.0, seg=20):
    """A puffy paw print standing in the XZ plane, facing -Y (UI art)."""
    x, y, z = c
    o = [ellipsoid(name + 'p', (x, y, z - 0.03 * s), (0.09 * s, depth, 0.075 * s), key, seg=seg, rings=12)]
    for k, (dx, dz) in enumerate(((-0.105, 0.06), (-0.038, 0.118), (0.038, 0.118), (0.105, 0.06))):
        o.append(ellipsoid(name + 't%d' % k, (x + dx * s, y, z + dz * s), (0.036 * s, depth * 0.8, 0.041 * s), key,
                           seg=seg, rings=12))
    return o


# ---------------------------------------------------------------------------
# Mila (sample.py's model, no accessories). Cat space: origin on the ground in
# the middle, facing -Y, metres.
# ---------------------------------------------------------------------------

def mila(tag, pose='sit'):
    fk = fur()
    pink = pm('m_pink', (0.95, 0.45, 0.55), rough=0.5)
    blush = pm('m_blush', (0.85, 0.30, 0.42), rough=0.7)
    eye = pm('m_eye', (1.0, 0.72, 0.02), rough=0.15, spec=0.8, emit=(1.0, 0.62, 0.0), es=0.6, cc=1.0)
    pupil = pm('m_pupil', (0.0, 0.0, 0.0), rough=0.1, spec=0.9)
    glint = pm('m_glint', (1, 1, 1), emit=(1, 1, 1), es=4.0)
    whisk = pm('m_whisker', (0.85, 0.85, 0.9), rough=0.4, emit=(0.6, 0.6, 0.7), es=0.4)
    o = []
    lean = 0.0
    head_dz = 0.0
    body_c, body_s, body_r = (0, 0.07, 0.22), (0.13, 0.21, 0.12), (0, 0, 0)
    legs = {
        'fl': ((0.07, -0.08, 0.10), (0.042, 0.042, 0.10), (0, 0, 0)),
        'fr': ((-0.07, -0.08, 0.10), (0.042, 0.042, 0.10), (0, 0, 0)),
        'bl': ((0.08, 0.19, 0.10), (0.046, 0.046, 0.10), (0, 0, 0)),
        'br': ((-0.08, 0.19, 0.10), (0.046, 0.046, 0.10), (0, 0, 0)),
    }
    paws = {'fl': (0.07, -0.10, 0.03), 'fr': (-0.07, -0.10, 0.03),
            'bl': (0.08, 0.18, 0.03), 'br': (-0.08, 0.18, 0.03)}
    tail = [(0, 0.26, 0.22), (0.0, 0.34, 0.30), (0.05, 0.40, 0.45), (0.06, 0.37, 0.58), (0.0, 0.31, 0.62)]
    haunch = False
    if pose == 'sit':
        body_c, body_s, body_r = (0, 0.08, 0.20), (0.13, 0.15, 0.16), (25, 0, 0)
        legs['fl'] = ((0.06, -0.07, 0.11), (0.040, 0.040, 0.11), (0, 0, 0))
        legs['fr'] = ((-0.06, -0.07, 0.11), (0.040, 0.040, 0.11), (0, 0, 0))
        legs['bl'] = ((0.12, 0.08, 0.10), (0.07, 0.11, 0.08), (0, 0, 0))
        legs['br'] = ((-0.12, 0.08, 0.10), (0.07, 0.11, 0.08), (0, 0, 0))
        paws['bl'] = (0.12, -0.03, 0.03)
        paws['br'] = (-0.12, -0.03, 0.03)
        paws['fl'] = (0.06, -0.10, 0.03)
        paws['fr'] = (-0.06, -0.10, 0.03)
        # tail curled round to the front, the tip raised a little so it shows
        tail = [(-0.02, 0.22, 0.05), (-0.14, 0.18, 0.04), (-0.20, 0.05, 0.04), (-0.17, -0.08, 0.05),
                (-0.08, -0.15, 0.07)]
        haunch = True
    head_y = -0.17 + lean
    head_z = 0.45 + head_dz + (0.04 if pose == 'sit' else 0.0)
    skin = []
    skin.append(ellipsoid(tag + 'body', body_c, body_s, fk, body_r))
    skin.append(ellipsoid(tag + 'chest', (0, -0.05 + lean * 0.5, 0.25 + head_dz * 0.5 + (0.04 if haunch else 0)),
                          (0.11, 0.11, 0.12), fk))
    for k, (c, s, r) in legs.items():
        skin.append(ellipsoid(tag + 'leg' + k, c, s, fk, r))
        skin.append(ellipsoid(tag + 'paw' + k, paws[k], (0.048, 0.056, 0.036), fk))
    skin.append(tube(tag + 'tail', tail, [0.040, 0.038, 0.034, 0.030, 0.024], fk))
    hc = V((0, head_y, head_z))
    skin.append(ellipsoid(tag + 'head', hc, (0.215, 0.19, 0.175), fk))
    for sx in (1, -1):
        skin.append(ellipsoid(tag + 'cheek%d' % sx, hc + V((sx * 0.10, -0.07, -0.07)), (0.10, 0.09, 0.075), fk))
        ec = hc + V((sx * 0.105, 0.03, 0.125))
        skin.append(cone(tag + 'ear%d' % sx, ec, 0.085, 0.004, 0.15, fk, rot=(-8, sx * 14, 0), scale=(1, 0.55, 1)))
        o.append(cone(tag + 'earin%d' % sx, ec + V((0, -0.022, 0.012)), 0.056, 0.003, 0.11, pink,
                      rot=(-8, sx * 14, 0), scale=(1, 0.25, 1)))
        epos = hc + V((sx * 0.083, -0.165, 0.015))
        o.append(ellipsoid(tag + 'eye%d' % sx, epos, (0.056, 0.030, 0.066), eye, rot=(0, sx * 8, sx * -18)))
        o.append(ellipsoid(tag + 'pupil%d' % sx, epos + V((sx * -0.004, -0.018, 0.0)), (0.015, 0.016, 0.048), pupil,
                           rot=(0, 0, sx * -18)))
        o.append(ellipsoid(tag + 'glint%d' % sx, epos + V((sx * -0.018, -0.03, 0.027)), (0.013, 0.01, 0.013), glint))
        o.append(ellipsoid(tag + 'blush%d' % sx, hc + V((sx * 0.135, -0.135, -0.055)), (0.035, 0.012, 0.022), blush,
                           rot=(0, 0, sx * -35)))
        for k, a in enumerate((12, 0, -12)):
            base = hc + V((sx * 0.14, -0.14, -0.06))
            dx = math.cos(math.radians(a))
            tip = base + V((sx * 0.16 * dx, 0.02, 0.16 * math.sin(math.radians(a)) * 0.6))
            o.append(tube(tag + 'wh%d%d' % (sx, k), [tuple(base), tuple(base.lerp(tip, 0.5)), tuple(tip)],
                          [0.0035, 0.003, 0.0015], whisk))
    o.append(ellipsoid(tag + 'nose', hc + V((0, -0.19, -0.045)), (0.024, 0.014, 0.016), pink))
    skin.append(ellipsoid(tag + 'muzzle', hc + V((0, -0.155, -0.075)), (0.07, 0.05, 0.045), fk))
    o.append(merge_skin(skin, tag + 'skin', fk))
    return o


# ---------------------------------------------------------------------------
# paths
# ---------------------------------------------------------------------------

def catmull(pts, step=0.05):
    """Points every ~step metres along a Catmull-Rom curve through pts (2D)."""
    P = [V((p[0], p[1])) for p in pts]
    P = [P[0] + (P[0] - P[1])] + P + [P[-1] + (P[-1] - P[-2])]
    out = []
    for i in range(1, len(P) - 2):
        p0, p1, p2, p3 = P[i - 1], P[i], P[i + 1], P[i + 2]
        n = max(2, int((p2 - p1).length / step))
        for k in range(n):
            t = k / n
            t2, t3 = t * t, t * t * t
            q = 0.5 * ((2 * p1) + (-p0 + p2) * t + (2 * p0 - 5 * p1 + 4 * p2 - p3) * t2 + (-p0 + 3 * p1 - 3 * p2 + p3) * t3)
            out.append(q)
    out.append(P[-2])
    return out


def paw_trail(tag, pts, key, z_at, spacing=0.5, side=0.1, s=1.0, avoid=(), avoid_r=0.8, seg=12, start=0.2):
    """Paw prints along a path (alternating left/right), skipping the circles
    in `avoid` (where the stones go). z_at(x, y) gives the ground height."""
    curve = catmull(pts)
    o = []
    acc = spacing - start
    k = 0
    for i in range(1, len(curve)):
        a, b = curve[i - 1], curve[i]
        acc += (b - a).length
        if acc < spacing:
            continue
        acc = 0.0
        d = (b - a).normalized()
        n = V((-d.y, d.x))
        lr = 1 if k % 2 else -1
        p = b + n * (side * lr)
        k += 1
        if any((p - V(c)).length < avoid_r for c in avoid):
            continue
        z = z_at(p.x, p.y)
        if z is None:
            continue
        o += paw('%s%d' % (tag, k), (p.x, p.y), math.atan2(d.y, d.x), s, key, z=z + 0.004, seg=seg)
    return o


def puffy(name, outline, key, t=0.12, dome=0.3, bevel=0.06, levels=2):
    """A puffy flat shape standing in the XZ plane facing -Y: `outline` is a
    list of (x, z) (convex round the centre), domed front and back."""
    me = bpy.data.meshes.new(name)
    bm = bmesh.new()
    cx = sum(p[0] for p in outline) / len(outline)
    cz = sum(p[1] for p in outline) / len(outline)
    rf = [bm.verts.new((x, -t, z)) for x, z in outline]
    rb = [bm.verts.new((x, t, z)) for x, z in outline]
    cf = bm.verts.new((cx, -t - dome, cz))
    cb = bm.verts.new((cx, t + dome, cz))
    n = len(outline)
    for k in range(n):
        k2 = (k + 1) % n
        bm.faces.new((cf, rf[k2], rf[k]))
        bm.faces.new((cb, rb[k], rb[k2]))
        bm.faces.new((rf[k], rf[k2], rb[k2], rb[k]))
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    bm.to_mesh(me)
    bm.free()
    ob = _obj(name, me, key)
    if bevel > 0:
        bv = ob.modifiers.new('bv', 'BEVEL')
        bv.width = bevel
        bv.segments = 3
    if levels:
        sd = ob.modifiers.new('sd', 'SUBSURF')
        sd.levels = sd.render_levels = levels
    return ob


def flat_poly(name, pts, key, z=0.0):
    """A flat polygon (may be concave) lying on the plane z."""
    me = bpy.data.meshes.new(name)
    bm = bmesh.new()
    vs = [bm.verts.new((x, y, z)) for x, y in pts]
    bm.faces.new(vs)
    bm.to_mesh(me)
    bm.free()
    return _obj(name, me, key, smooth=False)


def crescent_pts(c, R, d, n=24, rot=0.0):
    """Outline of a crescent moon: the circle (c, R) minus the same circle
    moved by d along the direction `rot` (radians)."""
    th1 = math.acos(d / (2 * R))
    pts = []
    for k in range(n + 1):
        a = th1 + (2 * math.pi - 2 * th1) * k / n
        pts.append((R * math.cos(a), R * math.sin(a)))
    for k in range(1, n):
        a = (th1 - math.pi) - 2 * th1 * k / n     # the inner circle's near side, bottom to top
        pts.append((d + R * math.cos(a), R * math.sin(a)))
    ca, sa = math.cos(rot), math.sin(rot)
    return [(c[0] + x * ca - y * sa, c[1] + x * sa + y * ca) for x, y in pts]


def facing_disc(name, c, r, key, verts=48):
    """A disc that faces the game camera (the moon on the black sky)."""
    me = bpy.data.meshes.new(name)
    bm = bmesh.new()
    bmesh.ops.create_circle(bm, cap_ends=True, segments=verts, radius=r)
    bm.to_mesh(me)
    bm.free()
    ob = _obj(name, me, key, smooth=False)
    ob.matrix_world = Matrix.Translation(V(c)) @ Matrix((C.R, C.UP, -C.F)).transposed().to_4x4()
    return ob


def read_png(path):
    """Read a PNG written by C.write_png (8 bit, filter 0) into a numpy array."""
    import struct
    import zlib
    import numpy as np
    with open(path, 'rb') as f:
        data = f.read()
    pos = 8
    idat = b''
    while pos < len(data):
        n = struct.unpack('>I', data[pos:pos + 4])[0]
        t = data[pos + 4:pos + 8]
        d = data[pos + 8:pos + 8 + n]
        if t == b'IHDR':
            w, h, _, ctype = struct.unpack('>IIBB', d[:10])
        elif t == b'IDAT':
            idat += d
        pos += 12 + n
    ch = {0: 1, 4: 2, 2: 3, 6: 4}[ctype]
    raw = np.frombuffer(zlib.decompress(idat), np.uint8).reshape(h, 1 + w * ch)
    assert not raw[:, 0].any(), 'only filter 0 is supported'
    return raw[:, 1:].reshape(h, w, ch).copy()
