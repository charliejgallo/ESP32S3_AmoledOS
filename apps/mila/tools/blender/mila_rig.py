"""Mila's rig: builds the kitten from a pose, re-fusing her skin every frame.

The model is sample.py's approved mila() (same primitives, same numbers),
turned into a rig: a pose (see mila_poses.py) says where the chest, rump,
head, paws and tail are, and build() puts the primitives there and fuses the
skin with a voxel remesh, so every pose is one clean surface and the rim
sheen only draws the outer silhouette.

Cat space: metres of the SAMPLE model (scaled by MS at the root), origin on
the ground under her middle, facing -Y (south, towards the camera), +Z up.
Her left is +X. The head is an empty (H) with the face parts and the hats
parented to it; neck items are computed from the fused skin (ray casts from
inside the neck), so they follow any pose.
"""
import math

import bmesh
import bpy
from mathutils import Euler, Matrix, Quaternion, Vector as V
from mathutils.bvhtree import BVHTree

import ml_common as C

MS = 0.855          # sample units -> metres: 0.62 m to the ear tips standing
VOXEL = 0.0065      # skin remesh (sample units)
FACING = {'s': 0.0, 'e': 62.0, 'n': 180.0, 'w': -62.0}


def E(r):
    return Euler([math.radians(a) for a in r], 'XYZ')


# ---------------------------------------------------------------------------
# materials (ml_common registry: every pass gets its variant)
# ---------------------------------------------------------------------------

def _principled(nt, col, rough, spec=0.5):
    b = nt.nodes.new('ShaderNodeBsdfPrincipled')
    b.inputs['Base Color'].default_value = tuple(col) + (1,)
    b.inputs['Roughness'].default_value = rough
    b.inputs['Specular'].default_value = spec
    return b


def fur_build(nt, neutral):
    """sample.py's black fur: nearly black, sheen, and a blue-grey rim
    (facing-ratio emission) so she reads on the black AMOLED."""
    b = _principled(nt, (0.8, 0.8, 0.8) if neutral else (0.020, 0.019, 0.025), 0.58, 0.30)
    b.inputs['Sheen'].default_value = 1.0
    b.inputs['Sheen Tint'].default_value = 0.0
    lw = nt.nodes.new('ShaderNodeLayerWeight')
    lw.inputs['Blend'].default_value = 0.35
    ramp = nt.nodes.new('ShaderNodeValToRGB')
    ramp.color_ramp.elements[0].position = 0.45
    ramp.color_ramp.elements[0].color = (0, 0, 0, 1)
    ramp.color_ramp.elements[1].position = 1.0
    ramp.color_ramp.elements[1].color = (0.16, 0.18, 0.30, 1)
    nt.links.new(lw.outputs['Facing'], ramp.inputs['Fac'])
    em = nt.nodes.new('ShaderNodeEmission')
    em.inputs['Strength'].default_value = RIM[0]
    nt.links.new(ramp.outputs['Color'], em.inputs['Color'])
    add = nt.nodes.new('ShaderNodeAddShader')
    nt.links.new(b.outputs['BSDF'], add.inputs[0])
    nt.links.new(em.outputs['Emission'], add.inputs[1])
    return add.outputs['Shader']


RIM = [0.9]


def eye_build(nt, neutral):
    """Amber-yellow iris, a warmer amber ring at the edge, softly glowing."""
    b = _principled(nt, (0.8, 0.8, 0.8), 0.15, 0.8)
    b.inputs['Clearcoat'].default_value = 1.0
    lw = nt.nodes.new('ShaderNodeLayerWeight')
    lw.inputs['Blend'].default_value = 0.5
    ramp = nt.nodes.new('ShaderNodeValToRGB')
    el = ramp.color_ramp.elements
    el[0].position, el[0].color = 0.0, (1.0, 0.80, 0.10, 1)
    el[1].position, el[1].color = 1.0, (0.85, 0.38, 0.02, 1)
    mid = el.new(0.55)
    mid.color = (1.0, 0.68, 0.02, 1)
    nt.links.new(lw.outputs['Facing'], ramp.inputs['Fac'])
    if not neutral:
        nt.links.new(ramp.outputs['Color'], b.inputs['Base Color'])
        nt.links.new(ramp.outputs['Color'], b.inputs['Emission'])
        b.inputs['Emission Strength'].default_value = 0.6
    return b.outputs['BSDF']


def register_materials():
    C.mat('fur', build=fur_build)
    C.mat('eye', build=eye_build)
    C.mat('pupil', base=(0.0, 0.0, 0.0), rough=0.1, spec=0.9)
    C.mat('glint', base=(1, 1, 1), emit=(1, 1, 1), emit_strength=4.0)
    C.mat('pink', base=(0.95, 0.45, 0.55), rough=0.5)
    C.mat('blush', base=(0.85, 0.30, 0.42), rough=0.7)
    C.mat('whisker', base=(0.85, 0.85, 0.9), rough=0.4, emit=(0.6, 0.6, 0.7), emit_strength=0.4)
    C.mat('lash', base=(0.50, 0.54, 0.70), rough=0.5, emit=(0.40, 0.45, 0.66), emit_strength=0.9)
    C.mat('mouthline', base=(0.55, 0.40, 0.46), rough=0.5, emit=(0.42, 0.28, 0.34), emit_strength=0.6)
    C.mat('mouth', base=(0.32, 0.03, 0.07), rough=0.5)
    C.mat('tongue', base=(1.0, 0.50, 0.60), rough=0.45)
    # layers (the watch recolours them by id: light pass on grey 0.8)
    C.mat('L_dark', rough=0.6, id=4)
    C.mat('beret_felt', rough=0.9, spec=0.3, id=1)
    C.mat('beret_stalk', rough=0.8, spec=0.3, id=2)
    C.mat('bunny_out', rough=0.75, spec=0.3, id=1)
    C.mat('bunny_in', rough=0.7, spec=0.3, id=2)
    C.mat('bandana', rough=0.85, spec=0.3, id=1)
    C.mat('collar', rough=0.45, spec=0.5, id=1)
    C.mat('bell', rough=0.22, metal=0.55, spec=0.8, id=3)
    C.mat('dots', rough=0.85, spec=0.3, id=2)
    C.mat('fishtag', rough=0.25, metal=0.5, spec=0.8, id=3)
    C.mat('pearl', rough=0.18, spec=0.9, id=1)
    C.mat('bowtie', rough=0.45, spec=0.5, id=1)
    C.mat('bowknot', rough=0.45, spec=0.5, id=2)
    C.mat('hbow', rough=0.35, spec=0.5, id=1)
    C.mat('hbowknot', rough=0.35, spec=0.5, id=2)
    C.mat('party', rough=0.45, spec=0.4, id=1)
    C.mat('pompom', rough=0.95, spec=0.2, id=2)
    C.mat('partydot', rough=0.45, spec=0.4, id=3)
    C.mat('gold', rough=0.25, metal=0.6, spec=0.8, id=1)
    C.mat('gem', rough=0.05, spec=1.0, id=3)
    C.mat('knit', build=knit_build(35.0), id=1)
    C.mat('knitcuff', build=knit_build(55.0), id=2)
    C.mat('petal', rough=0.6, spec=0.3, id=1)
    C.mat('fcentre', rough=0.8, spec=0.3, id=2)
    C.mat('leaf', rough=0.5, spec=0.4, id=3)
    C.mat('witch', rough=0.8, spec=0.3, id=1)
    C.mat('wband', rough=0.6, spec=0.4, id=2)
    C.mat('buckle', rough=0.25, metal=0.6, spec=0.8, id=3)


def knit_build(scale):
    """Knitted ribs (bump only, on grey: the watch colours it)."""
    def b(nt, neutral):
        p = _principled(nt, (0.8, 0.8, 0.8), 0.9, 0.3)
        tc = nt.nodes.new('ShaderNodeTexCoord')
        wv = nt.nodes.new('ShaderNodeTexWave')
        wv.wave_type = 'BANDS'
        wv.bands_direction = 'X'
        wv.inputs['Scale'].default_value = scale
        wv.inputs['Distortion'].default_value = 0.0
        nt.links.new(tc.outputs['Object'], wv.inputs['Vector'])
        bu = nt.nodes.new('ShaderNodeBump')
        bu.inputs['Strength'].default_value = 0.5
        bu.inputs['Distance'].default_value = 0.004
        nt.links.new(wv.outputs['Fac'], bu.inputs['Height'])
        nt.links.new(bu.outputs['Normal'], p.inputs['Normal'])
        return p.outputs['BSDF']
    return b


# ---------------------------------------------------------------------------
# geometry
# ---------------------------------------------------------------------------

def _obj(name, me, key=None, parent=None, smooth=True):
    ob = bpy.data.objects.new(name, me)
    C.link(ob)
    if smooth:
        for p in me.polygons:
            p.use_smooth = True
    if key:
        C.assign(ob, key)
    if parent is not None:
        ob.parent = parent
    return ob


def empty(name):
    ob = bpy.data.objects.new(name, None)
    C.link(ob)
    return ob


def _set_rot(ob, rot):
    if rot is None:
        return
    if isinstance(rot, Quaternion):
        ob.rotation_mode = 'QUATERNION'
        ob.rotation_quaternion = rot
    elif isinstance(rot, Matrix):
        ob.rotation_mode = 'QUATERNION'
        ob.rotation_quaternion = rot.to_quaternion()
    else:
        ob.rotation_euler = E(rot)


def ellipsoid(name, c, size, key=None, rot=None, parent=None, seg=32, rings=20):
    bm = bmesh.new()
    bmesh.ops.create_uvsphere(bm, u_segments=seg, v_segments=rings, radius=1.0)
    me = bpy.data.meshes.new(name)
    bm.to_mesh(me)
    bm.free()
    ob = _obj(name, me, key, parent)
    ob.scale = size
    _set_rot(ob, rot)
    ob.location = c
    return ob


def cone(name, c, r1, r2, h, key=None, rot=None, scale=(1, 1, 1), parent=None, verts=24):
    bm = bmesh.new()
    bmesh.ops.create_cone(bm, cap_ends=True, segments=verts, radius1=r1, radius2=r2, depth=h)
    bmesh.ops.translate(bm, verts=bm.verts, vec=(0, 0, h / 2))
    me = bpy.data.meshes.new(name)
    bm.to_mesh(me)
    bm.free()
    ob = _obj(name, me, key, parent)
    ob.scale = scale
    _set_rot(ob, rot)
    ob.location = c
    return ob


def torus(name, c, R, r, key=None, rot=None, scale=(1, 1, 1), parent=None, n=40, m=14):
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
    me = bpy.data.meshes.new(name)
    bm.to_mesh(me)
    bm.free()
    ob = _obj(name, me, key, parent)
    ob.scale = scale
    _set_rot(ob, rot)
    ob.location = c
    return ob


def capsule(name, a, b, ra, rb, key=None, parent=None):
    """A tapered capsule from a to b (legs)."""
    a, b = V(a), V(b)
    d = b - a
    L = max(d.length, 1e-4)
    bm = bmesh.new()
    bmesh.ops.create_cone(bm, cap_ends=True, segments=20, radius1=ra, radius2=rb, depth=L)
    bmesh.ops.translate(bm, verts=bm.verts, vec=(0, 0, L / 2))
    for r, z in ((ra, 0.0), (rb, L)):
        tmp = bmesh.new()
        bmesh.ops.create_uvsphere(tmp, u_segments=20, v_segments=12, radius=r)
        bmesh.ops.translate(tmp, verts=tmp.verts, vec=(0, 0, z))
        me = bpy.data.meshes.new('tmp')
        tmp.to_mesh(me)
        tmp.free()
        bm.from_mesh(me)
        bpy.data.meshes.remove(me)
    me = bpy.data.meshes.new(name)
    bm.to_mesh(me)
    bm.free()
    ob = _obj(name, me, key, parent)
    _set_rot(ob, d.normalized().to_track_quat('Z', 'Y'))
    ob.location = a
    return ob


def tube(name, pts, radii, key=None, parent=None, cyclic=False, res=10, bres=4):
    """A tube through pts with per-point radius, as a MESH (ml_common's fit
    only sees meshes)."""
    cu = bpy.data.curves.new(name, 'CURVE')
    cu.dimensions = '3D'
    cu.bevel_depth = 1.0
    cu.bevel_resolution = bres
    cu.use_fill_caps = not cyclic
    sp = cu.splines.new('NURBS')
    sp.points.add(len(pts) - 1)
    for p, q, r in zip(sp.points, pts, radii):
        p.co = (q[0], q[1], q[2], 1)
        p.radius = r
    sp.order_u = min(4, len(pts))
    sp.use_cyclic_u = cyclic
    sp.use_endpoint_u = not cyclic
    sp.resolution_u = res
    co = bpy.data.objects.new(name + '_cu', cu)
    C.link(co)
    bpy.context.view_layer.update()
    dg = bpy.context.evaluated_depsgraph_get()
    me = bpy.data.meshes.new_from_object(co.evaluated_get(dg))
    bpy.data.objects.remove(co, do_unlink=True)
    bpy.data.curves.remove(cu)
    me.materials.clear()
    return _obj(name, me, key, parent)


def mesh_from(name, verts, faces, key=None, parent=None, smooth=True):
    me = bpy.data.meshes.new(name)
    me.from_pydata([tuple(v) for v in verts], [], [tuple(f) for f in faces])
    me.validate()
    return _obj(name, me, key, parent, smooth)


def apply_modifiers(ob):
    bpy.context.view_layer.update()
    dg = bpy.context.evaluated_depsgraph_get()
    me = bpy.data.meshes.new_from_object(ob.evaluated_get(dg))
    old = ob.data
    ob.modifiers.clear()
    ob.data = me
    bpy.data.meshes.remove(old)
    return ob


def merge_skin(objs, name, voxel=VOXEL, smooth=6):
    """Fuse overlapping parts into one smooth skin (sample.py's recipe:
    voxel remesh + smooth), applied, in cat space."""
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
        m = ob.data
        bpy.data.objects.remove(ob, do_unlink=True)
        if m is not None and m.users == 0:
            bpy.data.meshes.remove(m)
    ob = _obj(name, me)
    rm = ob.modifiers.new('remesh', 'REMESH')
    rm.mode = 'VOXEL'
    rm.voxel_size = voxel
    rm.use_smooth_shade = True
    sm = ob.modifiers.new('smooth', 'SMOOTH')
    sm.factor = 0.6
    sm.iterations = smooth
    apply_modifiers(ob)
    for p in ob.data.polygons:
        p.use_smooth = True
    C.assign(ob, 'fur')
    return ob


# ---------------------------------------------------------------------------
# the head (head-local coordinates = sample.py's offsets from hc)
# ---------------------------------------------------------------------------

EYE_SIZE = (0.056, 0.030, 0.066)


def _eye_xf(sx):
    return V((sx * 0.083, -0.165, 0.015)), E((0, sx * 8, sx * -18))


def _lid_line(sx, shape, lift=0.0):
    """Points of a closed eye's line on the lid: 'closed' dips (a relaxed
    U), 'happy' arches (^)."""
    c, rot = _eye_xf(sx)
    R = rot.to_matrix()
    a, b, h = EYE_SIZE[0] * 1.06, EYE_SIZE[1] * 1.06, EYE_SIZE[2] * 1.06
    pts = []
    for k in range(7):
        u = -0.92 + 1.84 * k / 6
        w = (-0.30 * (1 - u * u) + 0.05) if shape == 'closed' else (0.34 * (1 - u * u) - 0.14)
        w += lift
        yy = -b * math.sqrt(max(0.0, 1 - u * u * 0.85 - w * w)) - 0.004
        pts.append(c + R @ V((u * a, yy - 0.003, w * h)))
    return pts


def _muzzle_y(x, z):
    q = 1 - (x / 0.07) ** 2 - ((z + 0.075) / 0.045) ** 2
    return -0.155 - 0.05 * math.sqrt(max(q, 0.0))


def head_skin(tag, H, P):
    """Fur parts of the head (fused into the skin)."""
    o = [ellipsoid(tag + 'head', (0, 0, 0), (0.215, 0.19, 0.175), parent=H)]
    erx, ery = P.get('ears', (0, 0))
    for sx in (1, -1):
        o.append(ellipsoid(tag + 'cheek%d' % sx, (sx * 0.10, -0.07, -0.07), (0.10, 0.09, 0.075), parent=H))
        o.append(cone(tag + 'ear%d' % sx, (sx * 0.105, 0.03, 0.125), 0.085, 0.004, 0.15,
                      rot=(-8 + erx, sx * (14 + ery), 0), scale=(1, 0.55, 1), parent=H))
        if P.get('eyes', 'open') != 'open':
            c, rot = _eye_xf(sx)
            o.append(ellipsoid(tag + 'lid%d' % sx, c + V((0, 0.002, 0)), tuple(s * 1.04 for s in EYE_SIZE),
                               rot=rot.to_quaternion(), parent=H))
    o.append(ellipsoid(tag + 'muzzle', (0, -0.155, -0.075), (0.07, 0.05, 0.045), parent=H))
    mo = P.get('mouth_open', 0.0) if P.get('mouth') == 'open' else 0.0
    if mo > 0:
        o.append(ellipsoid(tag + 'chin', (0, -0.135, -0.105 - 0.035 * mo), (0.052, 0.05, 0.035), parent=H))
    return o


def head_face(tag, H, P):
    """The coloured parts of the face (not fused)."""
    o = []
    erx, ery = P.get('ears', (0, 0))
    eyes = P.get('eyes', 'open')
    for sx in (1, -1):
        ec = V((sx * 0.105, 0.03, 0.125))
        o.append(cone(tag + 'earin%d' % sx, ec + V((0, -0.022, 0.012)), 0.056, 0.003, 0.11, 'pink',
                      rot=(-8 + erx, sx * (14 + ery), 0), scale=(1, 0.25, 1), parent=H))
        c, rot = _eye_xf(sx)
        if eyes == 'open':
            o.append(ellipsoid(tag + 'eye%d' % sx, c, EYE_SIZE, 'eye', rot=rot.to_quaternion(), parent=H))
            pw = P.get('pupil', 1.0)
            o.append(ellipsoid(tag + 'pupil%d' % sx, c + V((sx * -0.004, -0.018, 0.0)),
                               (0.015 * pw, 0.016, 0.048), 'pupil', rot=(0, 0, sx * -18), parent=H))
            o.append(ellipsoid(tag + 'glint%d' % sx, c + V((sx * -0.018, -0.03, 0.027)), (0.013, 0.01, 0.013),
                               'glint', parent=H))
            o.append(ellipsoid(tag + 'glintb%d' % sx, c + V((sx * 0.016, -0.027, -0.030)), (0.006, 0.006, 0.006),
                               'glint', parent=H))
        else:
            pts = _lid_line(sx, eyes)
            o.append(tube(tag + 'lash%d' % sx, pts, [0.0045, 0.0065, 0.0075, 0.0075, 0.0075, 0.0065, 0.0045],
                          'lash', parent=H))
        o.append(ellipsoid(tag + 'blush%d' % sx, (sx * 0.135, -0.135, -0.055), (0.035, 0.012, 0.022), 'blush',
                           rot=(0, 0, sx * -35), parent=H))
        for k, a in enumerate((12, 0, -12)):
            base = V((sx * 0.14, -0.14, -0.06))
            dx = math.cos(math.radians(a))
            tip = base + V((sx * 0.16 * dx, 0.02, 0.16 * math.sin(math.radians(a)) * 0.6))
            o.append(tube(tag + 'wh%d%d' % (sx, k), [base, base.lerp(tip, 0.5), tip], [0.0045, 0.0036, 0.0018],
                          'whisker', parent=H, bres=2))
    o.append(ellipsoid(tag + 'nose', (0, -0.19, -0.045), (0.024, 0.014, 0.016), 'pink', parent=H))
    m = P.get('mouth')
    if m == 'w':
        for sx in (1, -1):
            pts = []
            for (x, z) in ((0.0, -0.066), (0.0, -0.080), (0.016, -0.092), (0.032, -0.082)):
                pts.append(V((sx * x, _muzzle_y(x, z) - 0.0035, z)))
            o.append(tube(tag + 'mw%d' % sx, pts, [0.0045, 0.0045, 0.0048, 0.004], 'mouthline', parent=H, bres=2))
    elif m == 'open':
        mo = P.get('mouth_open', 1.0)
        o.append(ellipsoid(tag + 'mouth', (0, -0.182, -0.102 - 0.014 * mo), (0.042, 0.024, 0.018 + 0.027 * mo),
                           'mouth', parent=H))
        o.append(ellipsoid(tag + 'tongue', (0, -0.192, -0.112 - 0.028 * mo), (0.026, 0.014, 0.011), 'tongue',
                           parent=H))
    return o


# ---------------------------------------------------------------------------
# the rig
# ---------------------------------------------------------------------------

def head_matrix(P):
    if 'head_axes' in P:
        fwd, up = (V(v).normalized() for v in P['head_axes'])
        up = (up - fwd * up.dot(fwd)).normalized()
        Y = -fwd
        X = Y.cross(up)
        return Matrix((X, Y, up)).transposed()
    return E(P.get('head_rot', (0, 0, 0))).to_matrix()


def build(P, tag='m', items=(), voxel=VOXEL, anchor=None, yaw=0.0):
    """Build Mila in pose P. Returns dict(root, body, layers{item: objs})."""
    bw = P.get('breath', 0.0)
    H = empty(tag + 'H')
    H.rotation_mode = 'QUATERNION'
    H.rotation_quaternion = head_matrix(P).to_quaternion()
    H.location = P['head']
    bpy.context.view_layer.update()

    skin = []
    ch, ru = V(P['chest']), V(P['rump'])
    if P.get('spine'):
        sp = P['spine']
        for i in range(len(sp) - 1):
            skin.append(capsule(tag + 'sp%d' % i, sp[i][0], sp[i + 1][0], sp[i][1] * (1 + bw), sp[i + 1][1] * (1 + bw)))
    else:
        ax = ru - ch
        mid = (ch + ru) / 2 + V((0, 0, P.get('arch', 0.0)))
        up = V(P.get('body_up', (0, 0, 1)))
        q = ax.normalized().to_track_quat('Y', 'Z')
        if 'body_up' in P:
            Yb = ax.normalized()
            Zb = (up - Yb * up.dot(Yb)).normalized()
            q = Matrix((Yb.cross(Zb), Yb, Zb)).transposed().to_quaternion()
        skin.append(ellipsoid(tag + 'body', mid, (0.13 * (1 + bw), ax.length / 2 + 0.10, 0.12 * (1 + bw)), rot=q))
    cs = P.get('chest_s', (0.11, 0.11, 0.12))
    skin.append(ellipsoid(tag + 'chest', ch, tuple(s * (1 + bw) for s in cs)))
    skin.append(ellipsoid(tag + 'rump', ru, P.get('rump_s', (0.12, 0.11, 0.115))))
    hn = H.matrix_world @ V((0, 0.04, -0.10))
    skin.append(ellipsoid(tag + 'neck', ch.lerp(hn, 0.55), (0.085, 0.085, 0.09)))
    sep = []           # legs in front of her face/chest get their own skin (and rim)
    for k, (root, paw) in P['legs'].items():
        r = P['leg_r'][k[0]]
        root, paw = V(root), V(paw)
        dst = sep if k in P.get('sep_legs', ()) else skin
        dst.append(capsule(tag + 'leg' + k, root, paw + V((0, 0, 0.012)), r, r * 0.9))
        dst.append(ellipsoid(tag + 'paw' + k, paw, (0.048, 0.056, 0.036), rot=P.get('paw_rot', {}).get(k)))
    for i, (c, s, r) in enumerate(P.get('thighs', [])):
        skin.append(ellipsoid(tag + 'thigh%d' % i, c, s, rot=r))
    skin.append(tube(tag + 'tail', P['tail'], P.get('tail_r', [0.040, 0.038, 0.034, 0.030, 0.024])))
    skin += head_skin(tag, H, P)
    sk = merge_skin(skin, tag + 'skin', voxel)
    extra = [merge_skin(sep, tag + 'skinlegs', voxel)] if sep else []
    face = head_face(tag, H, P)

    ctx = dict(tag=tag, H=H, skin=sk, P=P)
    layers = {}
    for it in items:
        layers[it] = ITEMS[it](ctx)

    root = empty(tag + 'root')
    for ob in list(bpy.context.scene.objects):
        if ob.parent is None and ob is not root and ob.type in ('MESH', 'EMPTY') and not ob.name.startswith('ml_'):
            ob.parent = root
    root.location = anchor if anchor is not None else V((0, 0, 0))
    root.rotation_euler = (0, 0, math.radians(yaw))
    root.scale = (MS, MS, MS)
    bpy.context.view_layer.update()
    return dict(root=root, body=[sk] + extra + face, layers=layers, H=H)


# ---------------------------------------------------------------------------
# layers: hats (parented to the head) and neck items (built on the skin)
# ---------------------------------------------------------------------------

def _bvh(ctx):
    if 'bvh' not in ctx:
        dg = bpy.context.evaluated_depsgraph_get()
        ctx['bvh'] = BVHTree.FromObject(ctx['skin'], dg)
    return ctx['bvh']


def neck_frame(ctx):
    """Origin inside the neck and axes: Z up the neck (chest -> head),
    -Y towards the chin, X = Y x Z."""
    if 'neck' in ctx:
        return ctx['neck']
    P, H = ctx['P'], ctx['H']
    ch = V(P['chest'])
    hn = H.matrix_world @ V((0, 0.03, -0.09))
    Z = (hn - ch).normalized()
    fwd = H.matrix_world.to_3x3().normalized() @ V((0, -1, 0))
    Yn = -(fwd - Z * fwd.dot(Z)).normalized()
    X = Yn.cross(Z)
    N = ch.lerp(hn, P.get('neck_t', 0.5))
    ctx['neck'] = (N, X, Yn, Z)
    return ctx['neck']


def cast_out(ctx, O, d):
    """First skin surface from O (inside the body) along d: (point, dist)."""
    d = V(d).normalized()
    loc, nor, idx, dist = _bvh(ctx).ray_cast(O, d)
    if loc is None:
        return O + d * 0.12, 0.12
    return loc, dist


def ring_dir(fr, a, tilt=0.0):
    N, X, Y, Z = fr
    return (X * math.cos(a) + Y * math.sin(a) + Z * tilt).normalized()


def neck_ring(ctx, off, n=40, tilt=0.0):
    fr = neck_frame(ctx)
    pts = []
    for i in range(n):
        d = ring_dir(fr, 2 * math.pi * i / n, tilt)
        loc, dist = cast_out(ctx, fr[0], d)
        pts.append(loc + d * off)
    return pts


def push_out(ctx, p, r):
    """Move a ball of radius r at p out of the skin, along the ray from the
    neck origin."""
    N = neck_frame(ctx)[0]
    d = (p - N)
    loc, dist = cast_out(ctx, N, d)
    if d.length < dist + r:
        p = N + d.normalized() * (dist + r)
    return p


def hat_beret(ctx):
    t, H = ctx['tag'] + 'beret', ctx['H']
    rot = (-12, -17, 6)
    c = V((-0.022, -0.035, 0.175))
    R = E(rot).to_matrix()
    o = [ellipsoid(t + 'top', c + R @ V((0, 0, 0.022)), (0.125, 0.118, 0.045), 'beret_felt', rot=rot, parent=H),
         torus(t + 'band', c + R @ V((0, 0, 0.004)), 0.092, 0.014, 'beret_felt', rot=rot, parent=H, m=10),
         cone(t + 'stalk', c + R @ V((0.0, 0.0, 0.058)), 0.014, 0.008, 0.040, 'beret_stalk',
              rot=(rot[0] - 10, rot[1] + 25, rot[2]), parent=H, verts=12)]
    return o


def hat_bunny(ctx):
    t, H = ctx['tag'] + 'bunny', ctx['H']
    y0 = -0.035
    k = math.sqrt(1 - (y0 / 0.19) ** 2)
    a, cz = 0.215 * k + 0.012, 0.175 * k + 0.012
    pts = [V((a * math.cos(math.radians(th)), y0, cz * math.sin(math.radians(th))))
           for th in range(12, 170, 12)]
    o = [tube(t + 'band', pts, [0.013] * len(pts), 'bunny_out', parent=H, res=6)]
    for sx, tilt, back in ((1, 14, -10), (-1, -20, -6)):
        th = math.radians(90 - sx * 25)
        base = V((a * math.cos(th), y0, cz * math.sin(th)))
        r = E((back, tilt, 0)).to_matrix()
        o.append(ellipsoid(t + 'knob%d' % sx, base, (0.022, 0.018, 0.018), 'bunny_out', parent=H))
        o.append(ellipsoid(t + 'ear%d' % sx, base + r @ V((0, 0, 0.12)), (0.042, 0.020, 0.125), 'bunny_out',
                           rot=(back, tilt, 0), parent=H))
        o.append(ellipsoid(t + 'in%d' % sx, base + r @ V((0, -0.011, 0.125)), (0.026, 0.012, 0.095), 'bunny_in',
                           rot=(back, tilt, 0), parent=H))
    return o


def drape(ctx, p, off):
    """Keep p at least `off` outside the skin (nearest surface point)."""
    loc, nor, idx, dist = _bvh(ctx).find_nearest(p)
    if loc is None:
        return p
    s = (p - loc).dot(nor)
    if s < off:
        p = loc + nor * off
    return p


def hanging_triangle(ctx, off, L, spread=80, nu=14, nv=10):
    """A cloth triangle hanging from the front half of the neck ring: top
    edge on the ring, the point L below it (along P['hang'], default world
    down) and a little forward; kept outside the skin."""
    fr = neck_frame(ctx)
    N, X, Y, Z = fr
    P = ctx['P']
    down = V(P.get('hang', (0, 0, -1))).normalized()
    fwd = -Y
    top = []
    for i in range(nu + 1):
        u = -1 + 2 * i / nu
        d = ring_dir(fr, -math.pi / 2 + u * math.radians(spread))
        loc, dist = cast_out(ctx, N, d)
        top.append(loc + d * off)
    front = top[nu // 2]
    apex = front + down * L + fwd * 0.03
    if P.get('hang') is None:
        apex.z = max(apex.z, 0.05)
    grid = []
    for j in range(nv + 1):
        v = j / nv
        row = []
        for i in range(nu + 1):
            u = -1 + 2 * i / nu
            # rows shrink towards the point; a little belly of cloth in the middle
            p = top[i].lerp(apex, v) + fwd * (0.012 * math.sin(math.pi * v) * (1 - abs(u)))
            row.append(p)
        grid.append(row)
    for it in range(4):
        for j in range(1, nv + 1):
            for i in range(nu + 1):
                grid[j][i] = drape(ctx, grid[j][i], off + 0.002 * (j / nv))
        if it < 3:        # relax the interior a little
            g2 = [r[:] for r in grid]
            for j in range(1, nv):
                for i in range(1, nu):
                    g2[j][i] = (grid[j][i] * 2 + grid[j - 1][i] + grid[j + 1][i] + grid[j][i - 1] + grid[j][i + 1]) / 6
            grid = g2
    verts = [p for r in grid for p in r]
    faces = []
    for j in range(nv):
        for i in range(nu):
            a = j * (nu + 1) + i
            faces.append((a, a + 1, a + nu + 2, a + nu + 1))
    return verts, faces, grid


SCARF_LEN = 0.34      # the scarf's point hangs this far below the ring (sample units)


def drape_n(ctx, p, off):
    """drape() that also returns the skin normal there."""
    loc, nor, idx, dist = _bvh(ctx).find_nearest(p)
    if loc is None:
        return p, V((0, -1, 0))
    if (p - loc).dot(nor) < off:
        p = loc + nor * off
    return p, nor


def _front_frame(ctx, c):
    """A frame at c facing outwards (skin normal blended with the chin side):
    columns X (sideways), Y (into the body), Z (up the neck)."""
    N, X, Y, Z = neck_frame(ctx)
    loc, nor, idx, dist = _bvh(ctx).find_nearest(c)
    F = ((nor if nor is not None else -Y) + (-Y)).normalized()
    Xl = (X - F * X.dot(F)).normalized()
    Yl = -F
    return Matrix((Xl, Yl, Xl.cross(Yl))).transposed()


def _hang(ctx, ring, drop, fwd, r):
    """A thing hanging from the front of the collar ring, kept out of the skin."""
    N, X, Y, Z = neck_frame(ctx)
    front = ring[3 * len(ring) // 4]          # a = -90 deg: the chin side
    down = V(ctx['P'].get('hang', (0, 0, -1))).normalized()
    c = drape(ctx, front + down * drop + (-Y) * fwd, r)
    return front, c


def _collar(ctx, t, key='collar'):
    ring = neck_ring(ctx, 0.010)
    return ring, tube(t + 'collar', ring, [0.017] * len(ring), key, cyclic=True, res=4)


def neck_bandana(ctx, dots=False):
    t = ctx['tag'] + ('dots' if dots else 'bandana')
    fr = neck_frame(ctx)
    N, X, Y, Z = fr
    o = []
    # the rolled band around the neck
    ring = neck_ring(ctx, 0.012)
    o.append(tube(t + 'roll', ring, [0.017] * len(ring), 'bandana', cyclic=True, res=4))
    # the triangle: its top edge on the front half of the ring, its point
    # hanging down (gravity) in front of the chest; draped over the skin
    verts, faces, grid = hanging_triangle(ctx, 0.012, ctx['P'].get('scarf_len', SCARF_LEN), spread=88)
    fl = mesh_from(t + 'flap', verts, faces, 'bandana')
    so = fl.modifiers.new('solid', 'SOLIDIFY')
    so.thickness = 0.008
    so.offset = 0.0
    apply_modifiers(fl)
    o.append(fl)
    if dots:
        for (j, i) in ((2, 1), (2, 5), (2, 9), (2, 13), (4, 3), (4, 7), (4, 11), (6, 5), (6, 9), (8, 7)):
            p, n = drape_n(ctx, grid[j][i], 0.012 + 0.0035)
            o.append(ellipsoid(t + 'dot%d_%d' % (j, i), p, (0.017, 0.017, 0.004), 'dots',
                               rot=n.to_track_quat('Z', 'Y'), seg=16, rings=8))
    # the knot at the back of the neck, two little tails
    kd = ring_dir(fr, math.pi / 2)
    loc, dist = cast_out(ctx, N, kd)
    kc = loc + kd * 0.022
    o.append(ellipsoid(t + 'knot', kc, (0.026, 0.020, 0.022), 'bandana'))
    for sx in (1, -1):
        tc = kc + X * (sx * 0.028) - Z * 0.03 + kd * 0.006
        q = Matrix((X, kd, Z)).transposed() @ E((0, sx * 35, 0)).to_matrix()
        o.append(ellipsoid(t + 'tail%d' % sx, tc, (0.018, 0.010, 0.036), 'bandana', rot=q))
    return o


def neck_dots(ctx):
    return neck_bandana(ctx, dots=True)


def neck_bell(ctx):
    t = ctx['tag'] + 'bell'
    ring, col = _collar(ctx, t)
    o = [col]
    br = 0.036
    front, c = _hang(ctx, ring, ctx['P'].get('bell_drop', 0.10), 0.03, br + 0.004)
    o.append(ellipsoid(t + 'bell', c, (br, br, br), 'bell'))
    M = _front_frame(ctx, c)
    o.append(torus(t + 'loop', front.lerp(c, 0.55), 0.013, 0.005, 'bell', rot=M @ E((0, 90, 0)).to_matrix(), n=16, m=6))
    # the slit (dark) on the front-bottom of the bell
    dn = (M @ V((0, -0.6, -1))).normalized()
    o.append(ellipsoid(t + 'slit', c + dn * (br * 0.93), (0.024, 0.007, 0.006), 'L_dark', rot=M))
    o.append(ellipsoid(t + 'hole', c + dn * (br * 0.98), (0.008, 0.008, 0.008), 'L_dark'))
    return o


def neck_fish(ctx):
    t = ctx['tag'] + 'fish'
    ring, col = _collar(ctx, t)
    o = [col]
    front, c = _hang(ctx, ring, 0.10, 0.03, 0.022)
    M = _front_frame(ctx, c)

    def W(p):
        return c + M @ V(p)
    o.append(ellipsoid(t + 'body', W((-0.008, 0, 0)), (0.046, 0.012, 0.029), 'fishtag', rot=M))
    o.append(cone(t + 'tailf', W((0.072, 0, 0)), 0.028, 0.003, 0.034, 'fishtag',
                  rot=M @ E((0, -90, 0)).to_matrix(), scale=(1, 0.35, 1)))
    o.append(ellipsoid(t + 'eye', W((-0.034, -0.011, 0.007)), (0.0065, 0.004, 0.0065), 'L_dark', rot=M))
    o.append(torus(t + 'loop', front.lerp(W((-0.03, 0, 0.02)), 0.5), 0.012, 0.005, 'fishtag',
                   rot=M @ E((0, 90, 0)).to_matrix(), n=16, m=6))
    return o


def neck_pearls(ctx):
    t = ctx['tag'] + 'pearls'
    N, X, Y, Z = neck_frame(ctx)
    down = V(ctx['P'].get('hang', (0, 0, -1))).normalized()
    n = 90
    loop = []
    for i in range(n):
        a = 2 * math.pi * i / n
        d = ring_dir((N, X, Y, Z), a)
        loc, dist = cast_out(ctx, N, d)
        w = max(0.0, -math.sin(a)) ** 2          # the string sags on the chest in front
        loop.append(drape(ctx, loc + d * 0.018 + down * 0.095 * w + (-Y) * 0.03 * w, 0.019))
    L = [0.0]
    for i in range(1, n + 1):
        L.append(L[-1] + (loop[i % n] - loop[i - 1]).length)
    k = max(12, int(L[-1] / 0.036))
    step = L[-1] / k
    o = []
    j = 0
    for m in range(k):
        s = m * step
        while L[j + 1] < s:
            j += 1
        f = (s - L[j]) / max(L[j + 1] - L[j], 1e-6)
        p = loop[j].lerp(loop[(j + 1) % n], f)
        o.append(ellipsoid(t + '%02d' % m, p, (0.018, 0.018, 0.018), 'pearl', seg=16, rings=10))
    return o


def neck_bow(ctx):
    t = ctx['tag'] + 'bowtie'
    ring = neck_ring(ctx, 0.010)
    front, c = _hang(ctx, ring, 0.06, 0.04, 0.034)
    M = _front_frame(ctx, c)
    o = []
    for sx in (1, -1):
        R = M @ E((0, sx * 12, 0)).to_matrix()
        o.append(ellipsoid(t + 'lobe%d' % sx, c + M @ V((sx * 0.052, 0.004, 0)), (0.056, 0.022, 0.042), 'bowtie', rot=R))
    o.append(ellipsoid(t + 'knot', c + M @ V((0, -0.008, 0)), (0.024, 0.022, 0.028), 'bowknot', rot=M))
    return o


# --- hats (head-local, parented to H) --------------------------------------

def hat_bow(ctx):
    """sample.py's bow."""
    t, H = ctx['tag'] + 'hbow', ctx['H']
    bc = V((0.1, -0.02, 0.17))
    o = []
    for sx in (1, -1):
        o.append(ellipsoid(t + 'l%d' % sx, bc + V((sx * 0.045, 0, 0.0)), (0.05, 0.022, 0.035), 'hbow',
                           rot=(0, sx * 25, 20), parent=H))
        o.append(ellipsoid(t + 't%d' % sx, bc + V((sx * 0.022, 0.012, -0.038)), (0.013, 0.008, 0.032), 'hbow',
                           rot=(0, sx * 25, 20), parent=H))
    o.append(ellipsoid(t + 'k', bc, (0.022, 0.024, 0.022), 'hbowknot', parent=H))
    return o


def hat_party(ctx):
    """sample.py's party cone, dotted."""
    t, H = ctx['tag'] + 'party', ctx['H']
    c2 = V((0.0, 0.0, 0.16))
    rot = (-10, 12, 0)
    R = E(rot).to_matrix()
    o = [cone(t + 'cone', c2, 0.075, 0.004, 0.2, 'party', rot=rot, parent=H),
         ellipsoid(t + 'pom', c2 + R @ V((0, 0, 0.205)), (0.03, 0.03, 0.03), 'pompom', parent=H),
         torus(t + 'rim', c2 + R @ V((0, 0, 0.012)), 0.07, 0.012, 'pompom', rot=rot, parent=H)]
    for k, (hz, a) in enumerate(((0.05, -100), (0.05, -30), (0.05, 40), (0.05, 180), (0.10, -65),
                                 (0.10, 5), (0.10, 120), (0.145, -90), (0.145, 30))):
        r = 0.075 * (1 - hz / 0.2) + 0.001
        rad = V((math.cos(math.radians(a)), math.sin(math.radians(a)), 0.36)).normalized()
        p = V((r * math.cos(math.radians(a)), r * math.sin(math.radians(a)), hz))
        q = R @ rad.to_track_quat('Z', 'Y').to_matrix()
        o.append(ellipsoid(t + 'd%d' % k, c2 + R @ p, (0.012, 0.012, 0.004), 'partydot', rot=q, parent=H,
                           seg=12, rings=6))
    return o


def hat_crown(ctx):
    """sample.py's crown, a spike to the front over the gem."""
    t, H = ctx['tag'] + 'crown', ctx['H']
    cc = V((0.0, 0.01, 0.165))
    rot = (-8, 0, 0)
    R = E(rot).to_matrix()
    o = [cone(t + 'b', cc, 0.085, 0.09, 0.05, 'gold', verts=24, rot=rot, parent=H)]
    for k in range(5):
        a = -math.pi / 2 + 2 * math.pi * k / 5
        p = cc + R @ V((0.086 * math.cos(a), 0.086 * math.sin(a), 0.045))
        o.append(cone(t + 's%d' % k, p, 0.022, 0.0, 0.05, 'gold', verts=8, rot=rot, parent=H))
        o.append(ellipsoid(t + 'bt%d' % k, p + R @ V((0, 0, 0.052)), (0.011, 0.011, 0.011), 'gold', parent=H,
                           seg=12, rings=8))
    o.append(ellipsoid(t + 'gem', cc + R @ V((0, -0.091, 0.025)), (0.019, 0.01, 0.019), 'gem', rot=rot, parent=H))
    for sx in (1, -1):
        a = math.radians(-90 + sx * 60)
        o.append(ellipsoid(t + 'gs%d' % sx, cc + R @ V((0.091 * math.cos(a), 0.091 * math.sin(a), 0.025)),
                           (0.011, 0.011, 0.011), 'gem', parent=H, seg=12, rings=8))
    return o


def hat_beanie(ctx):
    """A knit beanie: a dome over the head (her ears come out through it),
    a rolled cuff and a pompom."""
    t, H = ctx['tag'] + 'beanie', ctx['H']
    A, B, Cc = 0.222, 0.200, 0.222
    c0 = V((0.0, 0.012, -0.004))

    def P(th, ph):
        return c0 + V((A * math.sin(th) * math.cos(ph), B * math.sin(th) * math.sin(ph), Cc * math.cos(th)))

    def zcut(p):
        return 0.090 - 0.028 * (p.y / 0.2)       # the rim: on the brow in front, a little lower at the back
    nph, nth = 48, 12
    rim, verts = [], []
    for i in range(nph):
        ph = 2 * math.pi * i / nph
        lo, hi = 0.0, math.pi
        for _ in range(30):
            mid = (lo + hi) / 2
            p = P(mid, ph)
            if p.z > zcut(p):
                lo = mid
            else:
                hi = mid
        rim.append(P(lo, ph))
        for j in range(nth + 1):
            th = 0.03 + (lo - 0.03) * j / nth
            verts.append(P(th, ph))
    faces = []
    for i in range(nph):
        for j in range(nth):
            a = i * (nth + 1) + j
            b = ((i + 1) % nph) * (nth + 1) + j
            faces.append((a, b, b + 1, a + 1))
    dome = mesh_from(t + 'dome', verts, faces, 'knit', parent=H)
    so = dome.modifiers.new('solid', 'SOLIDIFY')
    so.thickness = 0.010
    so.offset = 1.0
    apply_modifiers(dome)
    rp = [p + (p - c0).normalized() * 0.004 for p in rim]
    o = [dome, tube(t + 'cuff', rp, [0.017] * len(rp), 'knitcuff', parent=H, cyclic=True, res=4)]
    o.append(ellipsoid(t + 'pom', c0 + V((0.0, 0.0, Cc + 0.03)), (0.046, 0.044, 0.042), 'knitcuff', parent=H))
    return o


def hat_flower(ctx):
    """A big flower beside her left ear (+X), facing out and up."""
    t, H = ctx['tag'] + 'flower', ctx['H']
    fc = V((0.158, -0.012, 0.135))
    n = V((0.45, -0.75, 0.50)).normalized()
    Xl = V((0, 0, 1)).cross(n).normalized()
    Yl = n.cross(Xl)
    o = []
    for k in range(6):
        a = 2 * math.pi * k / 6 + 0.3
        rd = Xl * math.cos(a) + Yl * math.sin(a)
        M = Matrix((rd, n.cross(rd), n)).transposed()
        o.append(ellipsoid(t + 'p%d' % k, fc + rd * 0.043 - n * 0.004, (0.044, 0.028, 0.011), 'petal', rot=M,
                           parent=H, seg=20, rings=10))
    o.append(ellipsoid(t + 'c', fc + n * 0.006, (0.026, 0.026, 0.016), 'fcentre',
                       rot=Matrix((Xl, Yl, n)).transposed(), parent=H))
    for k, a in enumerate((200, 285)):
        rd = Xl * math.cos(math.radians(a)) + Yl * math.sin(math.radians(a))
        M = Matrix((rd, n.cross(rd), n)).transposed()
        o.append(ellipsoid(t + 'lf%d' % k, fc + rd * 0.078 - n * 0.012, (0.046, 0.019, 0.008), 'leaf', rot=M,
                           parent=H, seg=16, rings=8))
    return o


def hat_witch(ctx):
    """A tiny witch hat: brim (dark underside), a crooked cone, band, buckle."""
    t, H = ctx['tag'] + 'witch', ctx['H']
    c = V((0.0, 0.005, 0.168))
    R = E((-10, -10, 0)).to_matrix()
    q = R.to_quaternion()
    o = [cone(t + 'brim', c, 0.115, 0.112, 0.008, 'witch', rot=q, parent=H, verts=32),
         cone(t + 'under', c + R @ V((0, 0, -0.004)), 0.112, 0.112, 0.004, 'L_dark', rot=q, parent=H, verts=32)]
    pts = [V((0, 0, 0.004)), V((0, 0, 0.08)), V((0, 0.012, 0.15)), V((0, 0.05, 0.205)), V((0, 0.095, 0.205))]
    o.append(tube(t + 'cone', [c + R @ p for p in pts], [0.072, 0.056, 0.034, 0.016, 0.005], 'witch', parent=H))
    o.append(torus(t + 'band', c + R @ V((0, 0, 0.022)), 0.069, 0.013, 'wband', rot=q, parent=H, m=10))
    o.append(torus(t + 'buckle', c + R @ V((0, -0.081, 0.022)), 0.019, 0.0055, 'buckle',
                   rot=R @ E((90, 45, 0)).to_matrix(), parent=H, n=4, m=6))
    return o


ITEMS = {'hat_bow': hat_bow, 'hat_party': hat_party, 'hat_crown': hat_crown, 'hat_beret': hat_beret,
         'hat_beanie': hat_beanie, 'hat_flower': hat_flower, 'hat_bunny': hat_bunny, 'hat_witch': hat_witch,
         'neck_bell': neck_bell, 'neck_fish': neck_fish, 'neck_pearls': neck_pearls,
         'neck_bandana': neck_bandana, 'neck_dots': neck_dots, 'neck_bow': neck_bow}
