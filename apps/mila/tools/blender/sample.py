"""Mila - style sample (phase 1). Everything is built here by code.

    /Applications/Blender.app/Contents/MacOS/Blender -b -P sample.py -- --out DIR --what level_A,level_B,mila,casita

Renders, with a transparent background:
  level_<cam>.png   the whole sample level at game scale (the watch crops/zooms it)
  mila_<n>.png      Mila close-ups (poses and shop accessories)
  casita.png        Mila's home
The composer (../sample/compose_sample.py) turns them into watch screens.
"""
import bpy
import math
import os
import random
import sys

import numpy as np
from mathutils import Matrix, Vector as V, Euler

# ---------------------------------------------------------------------------
# cameras: yaw (deg), elevation (deg), px per metre
# ---------------------------------------------------------------------------
CAMS = {
    # A: straight 3/4, one cell = 56 x 42 px (the grid is square on screen: up = up)
    'A': dict(yaw=0.0, elev=math.degrees(math.asin(42 / 56)), s=56.0),
    # B: the Monster Hop family, turned a little (~16 deg), same scale
    'B': dict(yaw=16.0, elev=46.0, s=56.0),
    # A2: camera A, bigger: one cell = 72 x 54 px (chosen 2026-09-23)
    'A2': dict(yaw=0.0, elev=math.degrees(math.asin(54 / 72)), s=72.0),
}


def cam_axes(yaw, elev):
    psi, e = math.radians(yaw), math.radians(elev)
    F = V((-math.sin(psi) * math.cos(e), math.cos(psi) * math.cos(e), -math.sin(e)))
    R = V((math.cos(psi), math.sin(psi), 0.0))
    UP = R.cross(F)
    if UP.z < 0:
        UP = -UP
    return F, R, UP


class Cam:
    def __init__(self, yaw, elev, s):
        self.F, self.R, self.UP = cam_axes(yaw, elev)
        self.s = s

    def screen(self, p):
        return (self.s * V(p).dot(self.R), -self.s * V(p).dot(self.UP))

    def frame(self, pts, margin=8):
        xs, ys = zip(*[self.screen(p) for p in pts])
        x0, x1 = math.floor(min(xs)) - margin, math.ceil(max(xs)) + margin
        y0, y1 = math.floor(min(ys)) - margin, math.ceil(max(ys)) + margin
        return x1 - x0, y1 - y0, x0, y0

    def place(self, w, h, x0, y0):
        """Ortho camera whose image covers screen rect [x0, x0+w) x [y0, y0+h)."""
        sc = bpy.context.scene
        cam = sc.camera
        sc.render.resolution_x, sc.render.resolution_y = int(w), int(h)
        if w > h:
            cam.data.sensor_fit = 'HORIZONTAL'
            cam.data.ortho_scale = w / self.s
        else:
            cam.data.sensor_fit = 'VERTICAL'
            cam.data.ortho_scale = h / self.s
        cx, cy = x0 + w / 2.0, y0 + h / 2.0
        c = self.R * (cx / self.s) + self.UP * (-cy / self.s)
        M = Matrix((self.R, self.UP, -self.F)).transposed().to_4x4()
        cam.matrix_world = Matrix.Translation(c - self.F * 60.0) @ M


# ---------------------------------------------------------------------------
# scene
# ---------------------------------------------------------------------------

def reset(warm=True):
    bpy.ops.wm.read_factory_settings(use_empty=True)
    _M.clear()
    sc = bpy.context.scene
    sc.render.engine = 'CYCLES'
    ok = False
    try:
        pr = bpy.context.preferences.addons['cycles'].preferences
        pr.compute_device_type = 'METAL'
        pr.get_devices()
        for d in pr.devices:
            d.use = (d.type == 'METAL')
            ok = ok or d.use
    except Exception as e:  # noqa
        print('GPU setup failed:', e)
    sc.cycles.device = 'GPU' if ok else 'CPU'
    r = sc.render
    r.film_transparent = True
    r.dither_intensity = 0.0
    r.resolution_percentage = 100
    r.image_settings.file_format = 'PNG'
    r.image_settings.color_mode = 'RGBA'
    sc.view_settings.view_transform = 'Standard'
    sc.view_settings.look = 'None'
    sc.cycles.samples = 96
    sc.cycles.use_denoising = True
    sc.cycles.max_bounces = 6
    sc.cycles.seed = 3
    w = bpy.data.worlds.new('sky')
    sc.world = w
    w.use_nodes = True
    bg = w.node_tree.nodes['Background']
    bg.inputs['Color'].default_value = (0.85, 0.82, 0.78, 1) if warm else (0.6, 0.65, 0.8, 1)
    bg.inputs['Strength'].default_value = 0.45
    sun = bpy.data.objects.new('sun', bpy.data.lights.new('sun', 'SUN'))
    sc.collection.objects.link(sun)
    el, az = math.radians(55), math.radians(215)
    d = V((math.cos(el) * math.cos(az), math.cos(el) * math.sin(az), math.sin(el)))
    sun.rotation_euler = (-d).to_track_quat('-Z', 'Y').to_euler()
    sun.data.angle = math.radians(6)
    sun.data.energy = 2.6
    sun.data.color = (1.0, 0.93, 0.82)
    cam = bpy.data.objects.new('cam', bpy.data.cameras.new('cam'))
    sc.collection.objects.link(cam)
    sc.camera = cam
    cam.data.type = 'ORTHO'
    cam.data.clip_start = 0.1
    cam.data.clip_end = 400.0
    return sc


def link(ob):
    bpy.context.scene.collection.objects.link(ob)
    return ob


# ---------------------------------------------------------------------------
# materials
# ---------------------------------------------------------------------------
_M = {}


def principled(name, col, rough=0.6, spec=0.3, metal=0.0, emit=None, es=0.0, sheen=0.0, cc=0.0):
    if name in _M:
        return _M[name]
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    b = m.node_tree.nodes['Principled BSDF']
    b.inputs['Base Color'].default_value = tuple(col) + (1,)
    b.inputs['Roughness'].default_value = rough
    b.inputs['Specular'].default_value = spec
    b.inputs['Metallic'].default_value = metal
    b.inputs['Sheen'].default_value = sheen
    b.inputs['Clearcoat'].default_value = cc
    if emit is not None:
        b.inputs['Emission'].default_value = tuple(emit) + (1,)
        b.inputs['Emission Strength'].default_value = es
    _M[name] = m
    return m


def fur_black():
    """Black fur that still reads on a black screen: a soft blue-grey sheen
    on the rim (facing ratio), the rest nearly black."""
    if 'fur' in _M:
        return _M['fur']
    m = bpy.data.materials.new('fur')
    m.use_nodes = True
    nt = m.node_tree
    b = nt.nodes['Principled BSDF']
    b.inputs['Base Color'].default_value = (0.020, 0.019, 0.025, 1)
    b.inputs['Roughness'].default_value = 0.58
    b.inputs['Specular'].default_value = 0.30
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
    em.inputs['Strength'].default_value = 0.9
    nt.links.new(ramp.outputs['Color'], em.inputs['Color'])
    add = nt.nodes.new('ShaderNodeAddShader')
    nt.links.new(b.outputs['BSDF'], add.inputs[0])
    nt.links.new(em.outputs['Emission'], add.inputs[1])
    nt.links.new(add.outputs['Shader'], nt.nodes['Material Output'].inputs['Surface'])
    _M['fur'] = m
    return m


def yarn_mat(name, col):
    """Wave bands (the wound thread) on a woolly colour."""
    if name in _M:
        return _M[name]
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    nt = m.node_tree
    b = nt.nodes['Principled BSDF']
    b.inputs['Roughness'].default_value = 0.9
    b.inputs['Sheen'].default_value = 0.8
    b.inputs['Specular'].default_value = 0.2
    wv = nt.nodes.new('ShaderNodeTexWave')
    wv.inputs['Scale'].default_value = 7.0
    wv.inputs['Distortion'].default_value = 3.0
    wv.inputs['Detail'].default_value = 1.0
    tc = nt.nodes.new('ShaderNodeTexCoord')
    nt.links.new(tc.outputs['Object'], wv.inputs['Vector'])
    ramp = nt.nodes.new('ShaderNodeValToRGB')
    c = col
    ramp.color_ramp.elements[0].color = (c[0] * 0.55, c[1] * 0.55, c[2] * 0.55, 1)
    ramp.color_ramp.elements[1].color = (min(1, c[0] * 1.1), min(1, c[1] * 1.1), min(1, c[2] * 1.1), 1)
    ramp.color_ramp.elements[0].position = 0.25
    ramp.color_ramp.elements[1].position = 0.75
    nt.links.new(wv.outputs['Fac'], ramp.inputs['Fac'])
    nt.links.new(ramp.outputs['Color'], b.inputs['Base Color'])
    bump = nt.nodes.new('ShaderNodeBump')
    bump.inputs['Strength'].default_value = 0.6
    bump.inputs['Distance'].default_value = 0.02
    nt.links.new(wv.outputs['Fac'], bump.inputs['Height'])
    nt.links.new(bump.outputs['Normal'], b.inputs['Normal'])
    _M[name] = m
    return m


def wood_mat(name, col, scale=1.0):
    if name in _M:
        return _M[name]
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    nt = m.node_tree
    b = nt.nodes['Principled BSDF']
    b.inputs['Roughness'].default_value = 0.55
    b.inputs['Specular'].default_value = 0.35
    b.inputs['Clearcoat'].default_value = 0.15
    nz = nt.nodes.new('ShaderNodeTexNoise')
    nz.inputs['Scale'].default_value = 2.0 * scale
    nz.inputs['Detail'].default_value = 3.0
    tc = nt.nodes.new('ShaderNodeTexCoord')
    mp = nt.nodes.new('ShaderNodeMapping')
    mp.inputs['Scale'].default_value = (1.0, 10.0, 1.0)   # grain along X
    nt.links.new(tc.outputs['Object'], mp.inputs['Vector'])
    nt.links.new(mp.outputs['Vector'], nz.inputs['Vector'])
    ramp = nt.nodes.new('ShaderNodeValToRGB')
    ramp.color_ramp.elements[0].color = (col[0] * 0.78, col[1] * 0.78, col[2] * 0.78, 1)
    ramp.color_ramp.elements[1].color = tuple(col) + (1,)
    ramp.color_ramp.elements[0].position = 0.35
    ramp.color_ramp.elements[1].position = 0.65
    nt.links.new(nz.outputs['Fac'], ramp.inputs['Fac'])
    nt.links.new(ramp.outputs['Color'], b.inputs['Base Color'])
    _M[name] = m
    return m


# ---------------------------------------------------------------------------
# geometry helpers
# ---------------------------------------------------------------------------

def _obj(name, me, mat=None, smooth=True):
    ob = bpy.data.objects.new(name, me)
    link(ob)
    if mat is not None:
        me.materials.append(mat)
    if smooth:
        for p in me.polygons:
            p.use_smooth = True
    return ob


def ellipsoid(name, c, size, mat, rot=(0, 0, 0), seg=28, rings=18):
    import bmesh
    me = bpy.data.meshes.new(name)
    bm = bmesh.new()
    bmesh.ops.create_uvsphere(bm, u_segments=seg, v_segments=rings, radius=1.0)
    bm.to_mesh(me)
    bm.free()
    ob = _obj(name, me, mat)
    ob.scale = size
    ob.rotation_euler = Euler([math.radians(a) for a in rot], 'XYZ')
    ob.location = c
    return ob


def cone(name, c, r1, r2, h, mat, rot=(0, 0, 0), verts=24, scale=(1, 1, 1), smooth=True):
    import bmesh
    me = bpy.data.meshes.new(name)
    bm = bmesh.new()
    bmesh.ops.create_cone(bm, cap_ends=True, segments=verts, radius1=r1, radius2=r2, depth=h)
    bmesh.ops.translate(bm, verts=bm.verts, vec=(0, 0, h / 2))
    bm.to_mesh(me)
    bm.free()
    ob = _obj(name, me, mat, smooth)
    ob.scale = scale
    ob.rotation_euler = Euler([math.radians(a) for a in rot], 'XYZ')
    ob.location = c
    return ob


def torus(name, c, R, r, mat, rot=(0, 0, 0), scale=(1, 1, 1)):
    import bmesh
    me = bpy.data.meshes.new(name)
    bm = bmesh.new()
    n, m = 40, 14
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
    ob = _obj(name, me, mat)
    ob.scale = scale
    ob.rotation_euler = Euler([math.radians(a) for a in rot], 'XYZ')
    ob.location = c
    return ob


def box(name, x0, y0, z0, x1, y1, z1, mat, bevel=0.0):
    me = bpy.data.meshes.new(name)
    v = [(x0, y0, z0), (x1, y0, z0), (x1, y1, z0), (x0, y1, z0),
         (x0, y0, z1), (x1, y0, z1), (x1, y1, z1), (x0, y1, z1)]
    f = [(0, 3, 2, 1), (4, 5, 6, 7), (0, 1, 5, 4), (1, 2, 6, 5), (2, 3, 7, 6), (3, 0, 4, 7)]
    me.from_pydata(v, [], f)
    ob = _obj(name, me, mat, smooth=False)
    if bevel > 0:
        md = ob.modifiers.new('bv', 'BEVEL')
        md.width = bevel
        md.segments = 3
        for p in me.polygons:
            p.use_smooth = True
        me.use_auto_smooth = True
    return ob


def tube(name, pts, radii, mat, n=14):
    """A tube through pts with per-point radius (tail, strands)."""
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
    sp.order_u = 4
    sp.use_endpoint_u = True
    sp.resolution_u = 10
    ob = bpy.data.objects.new(name, cu)
    link(ob)
    cu.materials.append(mat)
    return ob


def merge_skin(objs, name, mat, voxel=0.0065, smooth=6):
    """Fuse overlapping parts into one smooth skin (voxel remesh + smooth), so
    the rim light only draws the outer silhouette, never the seams."""
    import bmesh
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
    ob = _obj(name, me, mat)
    rm = ob.modifiers.new('remesh', 'REMESH')
    rm.mode = 'VOXEL'
    rm.voxel_size = voxel
    rm.use_smooth_shade = True
    sm = ob.modifiers.new('smooth', 'SMOOTH')
    sm.factor = 0.6
    sm.iterations = smooth
    return ob


def parent_all(objs, name, loc=(0, 0, 0), rotz=0.0, scale=1.0):
    root = bpy.data.objects.new(name, None)
    link(root)
    for o in objs:
        o.parent = root
    root.location = loc
    root.rotation_euler = (0, 0, math.radians(rotz))
    root.scale = (scale, scale, scale)
    return root


# ---------------------------------------------------------------------------
# Mila. Cat space: origin on the ground in the middle of her cell, facing -Y
# (towards the camera = "south"), +Z up, metres (a cell is 1 m).
# ---------------------------------------------------------------------------
FACING = {'s': 0.0, 'e': 62.0, 'n': 180.0, 'w': -62.0}   # sides turned a little to the camera

ACCENT = {
    'pink': (0.95, 0.35, 0.55), 'red': (0.85, 0.08, 0.10), 'blue': (0.15, 0.40, 0.95),
    'gold': (1.0, 0.70, 0.18), 'mint': (0.35, 0.85, 0.65), 'purple': (0.55, 0.30, 0.90),
    'white': (0.95, 0.95, 0.95),
}


def mila(tag, pose='idle', acc=()):
    fur = fur_black()
    pink = principled('pink', (0.95, 0.45, 0.55), rough=0.5)
    blush = principled('blush', (0.85, 0.30, 0.42), rough=0.7)
    eye = principled('eye', (1.0, 0.72, 0.02), rough=0.15, spec=0.8, emit=(1.0, 0.62, 0.0), es=0.6, cc=1.0)
    pupil = principled('pupil', (0.0, 0.0, 0.0), rough=0.1, spec=0.9)
    glint = principled('glint', (1, 1, 1), emit=(1, 1, 1), es=4.0)
    whisk = principled('whisker', (0.85, 0.85, 0.9), rough=0.4, emit=(0.6, 0.6, 0.7), es=0.4)
    o = []

    # posture numbers
    lean = 0.0        # head forward (pushing)
    head_dz = 0.0
    body_c, body_s, body_r = (0, 0.07, 0.22), (0.13, 0.21, 0.12), (0, 0, 0)
    legs = {  # name: (base xyz, size, rot)
        'fl': ((0.07, -0.08, 0.10), (0.042, 0.042, 0.10), (0, 0, 0)),
        'fr': ((-0.07, -0.08, 0.10), (0.042, 0.042, 0.10), (0, 0, 0)),
        'bl': ((0.08, 0.19, 0.10), (0.046, 0.046, 0.10), (0, 0, 0)),
        'br': ((-0.08, 0.19, 0.10), (0.046, 0.046, 0.10), (0, 0, 0)),
    }
    paws = {'fl': (0.07, -0.10, 0.03), 'fr': (-0.07, -0.10, 0.03),
            'bl': (0.08, 0.18, 0.03), 'br': (-0.08, 0.18, 0.03)}
    tail = [(0, 0.26, 0.22), (0.0, 0.34, 0.30), (0.05, 0.40, 0.45), (0.06, 0.37, 0.58), (0.0, 0.31, 0.62)]
    haunch = False
    if pose == 'walk':
        legs['fl'] = ((0.07, -0.13, 0.11), (0.042, 0.042, 0.10), (-25, 0, 0))
        legs['br'] = ((-0.08, 0.24, 0.11), (0.046, 0.046, 0.10), (25, 0, 0))
        legs['fr'] = ((-0.07, -0.05, 0.10), (0.042, 0.042, 0.10), (15, 0, 0))
        legs['bl'] = ((0.08, 0.15, 0.10), (0.046, 0.046, 0.10), (-15, 0, 0))
        paws['fl'] = (0.07, -0.18, 0.04)
        paws['br'] = (-0.08, 0.28, 0.04)
        paws['fr'] = (-0.07, -0.02, 0.03)
        paws['bl'] = (0.08, 0.12, 0.03)
        tail = [(0, 0.26, 0.22), (0.0, 0.36, 0.28), (-0.04, 0.46, 0.36), (-0.02, 0.52, 0.48), (0.03, 0.50, 0.55)]
    elif pose == 'push':
        # head-butting the object, hips up, back legs driving
        lean = -0.10
        head_dz = -0.10
        body_c, body_s, body_r = (0, 0.11, 0.19), (0.13, 0.24, 0.11), (7, 0, 0)
        legs['fl'] = ((0.08, -0.19, 0.10), (0.042, 0.042, 0.10), (-30, 0, 0))
        legs['fr'] = ((-0.08, -0.17, 0.10), (0.042, 0.042, 0.10), (-22, 0, 0))
        legs['bl'] = ((0.08, 0.30, 0.11), (0.046, 0.046, 0.12), (42, 0, 0))
        legs['br'] = ((-0.08, 0.27, 0.11), (0.046, 0.046, 0.12), (34, 0, 0))
        paws['fl'] = (0.08, -0.25, 0.03)
        paws['fr'] = (-0.08, -0.22, 0.03)
        paws['bl'] = (0.08, 0.39, 0.03)
        paws['br'] = (-0.08, 0.35, 0.03)
        tail = [(0, 0.30, 0.24), (0.0, 0.38, 0.34), (0.0, 0.42, 0.48), (0.02, 0.42, 0.60), (0.06, 0.38, 0.66)]
    elif pose == 'sit':
        body_c, body_s, body_r = (0, 0.08, 0.20), (0.13, 0.15, 0.16), (25, 0, 0)
        legs['fl'] = ((0.06, -0.07, 0.11), (0.040, 0.040, 0.11), (0, 0, 0))
        legs['fr'] = ((-0.06, -0.07, 0.11), (0.040, 0.040, 0.11), (0, 0, 0))
        legs['bl'] = ((0.12, 0.08, 0.10), (0.07, 0.11, 0.08), (0, 0, 0))
        legs['br'] = ((-0.12, 0.08, 0.10), (0.07, 0.11, 0.08), (0, 0, 0))
        paws['bl'] = (0.12, -0.03, 0.03)
        paws['br'] = (-0.12, -0.03, 0.03)
        paws['fl'] = (0.06, -0.10, 0.03)
        paws['fr'] = (-0.06, -0.10, 0.03)
        tail = [(-0.02, 0.22, 0.05), (-0.14, 0.18, 0.04), (-0.20, 0.05, 0.04), (-0.17, -0.08, 0.04), (-0.08, -0.14, 0.04)]
        haunch = True
    head_y = -0.17 + lean
    head_z = 0.45 + head_dz + (0.04 if pose == 'sit' else 0.0)

    skin = []
    skin.append(ellipsoid(tag + 'body', body_c, body_s, fur, body_r))
    skin.append(ellipsoid(tag + 'chest', (0, -0.05 + lean * 0.5, 0.25 + head_dz * 0.5 + (0.04 if haunch else 0)), (0.11, 0.11, 0.12), fur))
    for k, (c, s, r) in legs.items():
        skin.append(ellipsoid(tag + 'leg' + k, c, s, fur, r))
        skin.append(ellipsoid(tag + 'paw' + k, paws[k], (0.048, 0.056, 0.036), fur))
    skin.append(tube(tag + 'tail', tail, [0.040, 0.038, 0.034, 0.030, 0.024], fur))

    # head
    hc = V((0, head_y, head_z))
    skin.append(ellipsoid(tag + 'head', hc, (0.215, 0.19, 0.175), fur))
    for sx in (1, -1):
        skin.append(ellipsoid(tag + 'cheek%d' % sx, hc + V((sx * 0.10, -0.07, -0.07)), (0.10, 0.09, 0.075), fur))
        # ears: flattened cones tilted outwards, pink inside
        ec = hc + V((sx * 0.105, 0.03, 0.125))
        skin.append(cone(tag + 'ear%d' % sx, ec, 0.085, 0.004, 0.15, fur, rot=(-8, sx * 14, 0), scale=(1, 0.55, 1)))
        o.append(cone(tag + 'earin%d' % sx, ec + V((0, -0.022, 0.012)), 0.056, 0.003, 0.11, pink,
                      rot=(-8, sx * 14, 0), scale=(1, 0.25, 1)))
        # eyes
        epos = hc + V((sx * 0.083, -0.165, 0.015))
        o.append(ellipsoid(tag + 'eye%d' % sx, epos, (0.056, 0.030, 0.066), eye, rot=(0, sx * 8, sx * -18)))
        o.append(ellipsoid(tag + 'pupil%d' % sx, epos + V((sx * -0.004, -0.018, 0.0)), (0.015, 0.016, 0.048), pupil,
                           rot=(0, 0, sx * -18)))
        o.append(ellipsoid(tag + 'glint%d' % sx, epos + V((sx * -0.018, -0.03, 0.027)), (0.013, 0.01, 0.013), glint))
        # blush
        o.append(ellipsoid(tag + 'blush%d' % sx, hc + V((sx * 0.135, -0.135, -0.055)), (0.035, 0.012, 0.022), blush,
                           rot=(0, 0, sx * -35)))
        # whiskers
        for k, a in enumerate((12, 0, -12)):
            base = hc + V((sx * 0.14, -0.14, -0.06 + k * 0.0))
            dx = math.cos(math.radians(a))
            tip = base + V((sx * 0.16 * dx, 0.02, 0.16 * math.sin(math.radians(a)) * 0.6))
            o.append(tube(tag + 'wh%d%d' % (sx, k), [tuple(base), tuple(base.lerp(tip, 0.5)), tuple(tip)],
                          [0.0035, 0.003, 0.0015], whisk))
    o.append(ellipsoid(tag + 'nose', hc + V((0, -0.19, -0.045)), (0.024, 0.014, 0.016), pink))
    skin.append(ellipsoid(tag + 'muzzle', hc + V((0, -0.155, -0.075)), (0.07, 0.05, 0.045), fur))
    o.append(merge_skin(skin, tag + 'skin', fur))

    # accessories
    for a in acc:
        kind, colname = a.split(':') if ':' in a else (a, 'red')
        col = ACCENT[colname]
        if kind == 'collar':
            cm = principled('collar_' + colname, col, rough=0.4, spec=0.4)
            nc = V((0, -0.08 + lean * 0.7, 0.27 + head_dz * 0.8))
            o.append(torus(tag + 'collar', nc, 0.12, 0.028, cm, rot=(-42, 0, 0), scale=(1.0, 0.95, 1)))
            gold = principled('bell', (1.0, 0.75, 0.25), rough=0.2, metal=1.0)
            o.append(ellipsoid(tag + 'bell', nc + V((0, -0.115, -0.13)), (0.042, 0.042, 0.042), gold))
        elif kind == 'tag':
            cm = principled('collar_' + colname, col, rough=0.4, spec=0.4)
            nc = V((0, -0.08 + lean * 0.7, 0.27 + head_dz * 0.8))
            o.append(torus(tag + 'collar', nc, 0.12, 0.026, cm, rot=(-42, 0, 0), scale=(1.0, 0.95, 1)))
            sil = principled('tagfish', (0.8, 0.85, 0.9), rough=0.2, metal=1.0)
            o.append(ellipsoid(tag + 'fish', nc + V((0, -0.12, -0.13)), (0.05, 0.012, 0.028), sil))
            o.append(cone(tag + 'fisht', nc + V((0.05, -0.12, -0.13)), 0.02, 0.0, 0.03, sil, rot=(0, 90, 0),
                          scale=(1, 0.4, 1)))
        elif kind == 'bow':
            bm_ = principled('bow_' + colname, col, rough=0.35, spec=0.4, cc=0.3)
            bc = hc + V((0.1, -0.02, 0.17))
            for sx in (1, -1):
                o.append(ellipsoid(tag + 'bow%d' % sx, bc + V((sx * 0.045, 0, 0.0)), (0.05, 0.022, 0.035), bm_,
                                   rot=(0, sx * 25, 20)))
            o.append(ellipsoid(tag + 'bowk', bc, (0.022, 0.024, 0.022), bm_))
        elif kind == 'party':
            pm = principled('party_' + colname, col, rough=0.4, spec=0.3)
            wm = principled('pompom', (0.98, 0.98, 0.98), rough=0.9, sheen=1.0)
            hc2 = hc + V((0.0, 0.0, 0.16))
            o.append(cone(tag + 'party', hc2, 0.075, 0.004, 0.2, pm, rot=(-10, 12, 0)))
            o.append(ellipsoid(tag + 'pom', hc2 + V((0.04, 0.035, 0.195)), (0.03, 0.03, 0.03), wm))
            o.append(torus(tag + 'partyrim', hc2 + V((0, 0, 0.012)), 0.07, 0.012, wm, rot=(-10, 12, 0)))
        elif kind == 'crown':
            gold = principled('crown', (1.0, 0.76, 0.22), rough=0.25, metal=1.0)
            gem = principled('gem', (0.9, 0.1, 0.3), rough=0.05, spec=1.0, emit=(0.9, 0.1, 0.3), es=0.6)
            cc = hc + V((0.0, 0.01, 0.165))
            o.append(cone(tag + 'crownb', cc, 0.085, 0.09, 0.05, gold, verts=24, rot=(-8, 0, 0)))
            for k in range(5):
                a = 2 * math.pi * k / 5 + math.pi / 2
                p = cc + V((0.085 * math.cos(a), 0.085 * math.sin(a), 0.045))
                o.append(cone(tag + 'crs%d' % k, p, 0.022, 0.0, 0.05, gold, verts=8, rot=(-8, 0, 0)))
            o.append(ellipsoid(tag + 'gem', cc + V((0, -0.088, 0.025)), (0.018, 0.01, 0.018), gem))
    return o


# ---------------------------------------------------------------------------
# the living room kit
# ---------------------------------------------------------------------------
YARN = [(0.95, 0.40, 0.60), (0.25, 0.75, 0.78), (0.98, 0.78, 0.25), (0.62, 0.48, 0.95)]


def yarn(name, c, col, in_basket=False):
    m = yarn_mat('yarn_%s' % name, col)
    z = 0.30 + (0.07 if in_basket else 0.0)
    o = [ellipsoid(name, (c[0], c[1], z), (0.30, 0.30, 0.29), m, rot=(20, 35, 10))]
    rnd = random.Random(hash(name) & 0xffff)
    for k in range(3):
        o.append(torus(name + 'w%d' % k, (c[0], c[1], z), 0.293, 0.012, m,
                       rot=(rnd.uniform(0, 180), rnd.uniform(0, 180), rnd.uniform(0, 180))))
    if not in_basket:
        # a loose thread on the floor
        a = rnd.uniform(0, 6.28)
        p0 = V((c[0] + 0.2 * math.cos(a), c[1] + 0.2 * math.sin(a), 0.1))
        pts = [tuple(p0), (p0.x + 0.12 * math.cos(a), p0.y + 0.12 * math.sin(a), 0.02),
               (p0.x + 0.22 * math.cos(a + 0.6), p0.y + 0.22 * math.sin(a + 0.6), 0.012),
               (p0.x + 0.32 * math.cos(a + 0.2), p0.y + 0.32 * math.sin(a + 0.2), 0.012)]
        o.append(tube(name + 'str', pts, [0.012, 0.012, 0.011, 0.01], m))
    return o


def basket(name, c, glow=False):
    wick = wood_mat('wicker', (0.62, 0.38, 0.16), 6)
    wick2 = principled('wicker2', (0.80, 0.56, 0.28), rough=0.7)
    cush = principled('cushion', (0.92, 0.36, 0.42), rough=0.9, sheen=1.0)
    paw = principled('paw', (1.0, 0.85, 0.88), rough=0.9)
    o = []
    o.append(cone(name + 'b', (c[0], c[1], 0.0), 0.35, 0.43, 0.16, wick))
    o.append(torus(name + 'rim', (c[0], c[1], 0.16), 0.42, 0.04, wick2))
    o.append(ellipsoid(name + 'cu', (c[0], c[1], 0.14), (0.36, 0.36, 0.045), cush))
    # a paw print on the cushion: the "put it here" mark
    o.append(ellipsoid(name + 'pp', (c[0], c[1] - 0.04, 0.183), (0.09, 0.075, 0.01), paw))
    for k, (dx, dy) in enumerate(((-0.1, 0.07), (-0.035, 0.12), (0.035, 0.12), (0.1, 0.07))):
        o.append(ellipsoid(name + 'pt%d' % k, (c[0] + dx, c[1] + dy, 0.18), (0.033, 0.033, 0.01), paw))
    if glow:
        g = principled('glowring', (1.0, 0.85, 0.4), emit=(1.0, 0.8, 0.3), es=3.0)
        o.append(torus(name + 'glow', (c[0], c[1], 0.012), 0.47, 0.018, g))
    return o


def floor_cell(name, x, y):
    """Four planks along X with a hair of gap (the dark underlay shows)."""
    under = principled('underlay', (0.18, 0.10, 0.06), rough=0.9)
    o = [box(name + 'u', x, y, -0.06, x + 1, y + 1, -0.012, under)]
    rnd = random.Random(x * 131 + y * 7)
    for k in range(4):
        shade = rnd.uniform(0.9, 1.08)
        col = (0.78 * shade, 0.52 * shade, 0.30 * shade)
        m = wood_mat('plank%d' % int(shade * 20), col)
        y0 = y + k * 0.25 + 0.006
        y1 = y + (k + 1) * 0.25 - 0.006
        # boards end at a random x in the cell on odd rows
        o.append(box(name + 'p%d' % k, x + 0.003, y0, -0.03, x + 1 - 0.003, y1, 0.0, m, bevel=0.004))
    return o


WALL_H = 0.46


def wall_cell(name, x, y):
    paper = principled('paper', (0.30, 0.55, 0.58), rough=0.8)
    wain = principled('wain', (0.93, 0.90, 0.84), rough=0.6)
    top = wood_mat('walltop', (0.16, 0.09, 0.06), 3)
    return [box(name + 'w', x, y, 0.16, x + 1, y + 1, WALL_H - 0.05, paper),
            box(name + 'b', x - 0.006, y - 0.006, 0, x + 1.006, y + 1.006, 0.17, wain),
            box(name + 't', x - 0.012, y - 0.012, WALL_H - 0.05, x + 1.012, y + 1.012, WALL_H, top, bevel=0.012)]


def plant(name, c):
    pot = principled('pot', (0.80, 0.42, 0.28), rough=0.6)
    soil = principled('soil', (0.2, 0.12, 0.07), rough=1)
    leaf = principled('leaf', (0.22, 0.62, 0.30), rough=0.5, spec=0.4)
    leaf2 = principled('leaf2', (0.32, 0.72, 0.36), rough=0.5, spec=0.4)
    o = [cone(name + 'pot', (c[0], c[1], 0), 0.32, 0.42, 0.46, pot),
         torus(name + 'lip', (c[0], c[1], 0.46), 0.41, 0.05, pot),
         ellipsoid(name + 'soil', (c[0], c[1], 0.44), (0.38, 0.38, 0.03), soil)]
    rnd = random.Random(5)
    for k in range(9):
        a = 2 * math.pi * k / 9 + rnd.uniform(-0.2, 0.2)
        r = rnd.uniform(0.2, 0.34)
        o.append(ellipsoid(name + 'lf%d' % k, (c[0] + r * math.cos(a), c[1] + r * math.sin(a), 0.66 + rnd.uniform(0, 0.25)),
                           (0.28, 0.1, 0.04), leaf if k % 2 else leaf2,
                           rot=(0, rnd.uniform(-40, -10), math.degrees(a))))
    return o


def sofa(name, x0, y0, x1, y1):
    fab = principled('sofa', (0.85, 0.36, 0.30), rough=0.85, sheen=0.8)
    fab2 = principled('sofa2', (0.93, 0.48, 0.40), rough=0.85, sheen=0.8)
    leg = wood_mat('sofaleg', (0.35, 0.22, 0.12))
    o = []
    for lx in (x0 + 0.1, x1 - 0.15):
        for ly in (y0 + 0.12, y1 - 0.17):
            o.append(box(name + 'lg%.1f%.1f' % (lx, ly), lx, ly, 0, lx + 0.05, ly + 0.05, 0.1, leg))
    o.append(box(name + 'base', x0 + 0.03, y0 + 0.03, 0.1, x1 - 0.03, y1 - 0.03, 0.32, fab, bevel=0.06))
    o.append(box(name + 'back', x0 + 0.05, y1 - 0.30, 0.3, x1 - 0.05, y1 - 0.06, 0.72, fab, bevel=0.07))
    for sx in (x0 + 0.05, x1 - 0.2):
        o.append(box(name + 'arm%.1f' % sx, sx, y0 + 0.08, 0.3, sx + 0.15, y1 - 0.06, 0.5, fab, bevel=0.06))
    n = int(round(x1 - x0))
    for k in range(n):
        cx = x0 + 0.25 + k * ((x1 - x0 - 0.5) / max(1, n)) + 0.04
        o.append(box(name + 'cu%d' % k, cx, y0 + 0.1, 0.3, cx + (x1 - x0 - 0.5) / n - 0.08, y1 - 0.3, 0.42, fab2, bevel=0.06))
    pil = principled('pillow', (0.98, 0.84, 0.45), rough=0.9, sheen=1)
    o.append(ellipsoid(name + 'pil', (x0 + 0.42, y1 - 0.36, 0.55), (0.15, 0.06, 0.13), pil, rot=(-15, 0, 10)))
    return o


def side_table(name, c):
    wood = wood_mat('table', (0.62, 0.40, 0.22))
    shade = principled('lampshade', (1.0, 0.92, 0.7), rough=0.9, emit=(1.0, 0.8, 0.45), es=1.2)
    brass = principled('brass', (0.9, 0.7, 0.35), rough=0.3, metal=1)
    book = principled('book', (0.25, 0.4, 0.8), rough=0.7)
    o = [cone(name + 'top', (c[0], c[1], 0.34), 0.45, 0.45, 0.06, wood),
         cone(name + 'leg', (c[0], c[1], 0.0), 0.05, 0.05, 0.34, wood),
         cone(name + 'foot', (c[0], c[1], 0.0), 0.2, 0.18, 0.03, wood),
         cone(name + 'lampb', (c[0] + 0.08, c[1] + 0.08, 0.39), 0.07, 0.03, 0.2, brass),
         cone(name + 'shade', (c[0] + 0.08, c[1] + 0.08, 0.57), 0.14, 0.08, 0.16, shade),
         box(name + 'book', c[0] - 0.22, c[1] - 0.18, 0.39, c[0] - 0.02, c[1] + 0.02, 0.43, book, bevel=0.005)]
    lt = bpy.data.lights.new(name + 'l', 'POINT')
    lt.color = (1.0, 0.75, 0.45)
    lt.energy = 25
    lt.shadow_soft_size = 0.08
    lo = bpy.data.objects.new(name + 'l', lt)
    lo.location = (c[0] + 0.08, c[1] + 0.08, 0.62)
    link(lo)
    return o


def bookshelf(name, x, y):
    wood = wood_mat('shelf', (0.55, 0.34, 0.20))
    o = [box(name + 'c', x + 0.03, y + 0.2, 0, x + 0.97, y + 0.97, 0.9, wood, bevel=0.01)]
    cols = [(0.85, 0.2, 0.2), (0.2, 0.5, 0.9), (0.95, 0.75, 0.2), (0.3, 0.7, 0.4), (0.7, 0.4, 0.8), (0.95, 0.5, 0.3)]
    rnd = random.Random(9)
    for sh, z in enumerate((0.08, 0.48)):
        xx = x + 0.12
        while xx < x + 0.84:
            wdt = rnd.uniform(0.05, 0.09)
            hgt = rnd.uniform(0.26, 0.34)
            m = principled('bk%d' % rnd.randrange(6), cols[rnd.randrange(6)], rough=0.6)
            o.append(box(name + 'b%.2f' % xx, xx, y + 0.17, z, xx + wdt, y + 0.5, z + hgt, m, bevel=0.004))
            xx += wdt + 0.008
    return o


FURN = {(4, 2): 'plant', (2, 4): 'shelf', (3, 6): 'table', (6, 7): 'plant'}


def build_level(text, state=None):
    rows = [r for r in text.strip('\n').split('\n')]
    w = max(len(r) for r in rows)
    rows = [r.ljust(w, '-') for r in rows]
    h = len(rows)
    # world: row 0 is the BACK of the room (far, +Y). cell (x, r) -> world (x, h-1-r)
    wy = lambda r: h - 1 - r  # noqa: E731
    grid = {(x, r): rows[r][x] for r in range(h) for x in range(w)}
    # floor region = flood fill from Mila
    man = next(p for p, c in grid.items() if c in '@+')
    fl, todo = {man}, [man]
    while todo:
        x, r = todo.pop()
        for dx, dr in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            q = (x + dx, r + dr)
            if q in grid and grid[q] != '#' and q not in fl:
                fl.add(q)
                todo.append(q)
    objs = []
    for (x, r) in fl:
        objs += floor_cell('f%d_%d' % (x, r), x, wy(r))
    furn = {}
    for (x, r), c in grid.items():
        if c != '#':
            continue
        nfl = sum((x + dx, r + dr) in fl for dx, dr in ((1, 0), (-1, 0), (0, 1), (0, -1)))
        if nfl >= 3:
            furn[(x, r)] = 1
    # keep only walls that touch the floor (8-neighbourhood)
    for (x, r), c in grid.items():
        if c != '#' or (x, r) in furn:
            continue
        if any((x + dx, r + dr) in fl for dx in (-1, 0, 1) for dr in (-1, 0, 1)):
            objs += wall_cell('w%d_%d' % (x, r), x, wy(r))
    # furniture: pairs along a row become the sofa
    used = set()
    kinds = ['plant', 'shelf', 'table']
    k = 0
    for (x, r) in sorted(furn, key=lambda p: (p[1], p[0])):
        if (x, r) in used:
            continue
        if (x + 1, r) in furn:
            used |= {(x, r), (x + 1, r)}
            objs += floor_cell('ff%d_%d' % (x, r), x, wy(r)) + floor_cell('ff%d_%d' % (x + 1, r), x + 1, wy(r))
            objs += sofa('sofa%d' % x, x, wy(r), x + 2, wy(r) + 1)
            continue
        used.add((x, r))
        objs += floor_cell('ff%d_%d' % (x, r), x, wy(r))
        kind = FURN.get((x, r), kinds[k % len(kinds)])
        k += 1
        c = (x + 0.5, wy(r) + 0.5)
        if kind == 'plant':
            objs += plant('pl%d_%d' % (x, r), c)
        elif kind == 'shelf':
            objs += bookshelf('bs%d_%d' % (x, r), x, wy(r))
        else:
            objs += side_table('tb%d_%d' % (x, r), c)
    # targets, objects, Mila
    yi = 0
    for (x, r), c in sorted(grid.items(), key=lambda t: (t[0][1], t[0][0])):
        cc = (x + 0.5, wy(r) + 0.5)
        if c in '.*+':
            objs += basket('bk%d_%d' % (x, r), cc, glow=(c == '*'))
        if c in '$*':
            objs += yarn('y%d_%d' % (x, r), cc, YARN[yi % len(YARN)], in_basket=(c == '*'))
            yi += 1
    return objs, (w, h), man, wy


def rug(name, c, rx, ry, col, col2):
    m = principled(name, col, rough=1, sheen=1)
    m2 = principled(name + 'b', col2, rough=1, sheen=1)
    return [ellipsoid(name, (c[0], c[1], 0.0), (rx, ry, 0.012), m, seg=48),
            ellipsoid(name + 'i', (c[0], c[1], 0.004), (rx * 0.75, ry * 0.75, 0.012), m2, seg=48)]


# ---------------------------------------------------------------------------
# renders
# ---------------------------------------------------------------------------
SAMPLE_LEVEL = """
-#######-
##  .  ##
#  $#   #
# .  $  #
###@##  #
#   $ .##
#  #    #
# *   # #
#########
"""
# the state shown in the sample: one yarn already in its basket, Mila about
# to push the second one towards the right
SAMPLE_STATE = """
-#######-
##  .  ##
#  $#   #
# .  $  #
### ##  #
#  @$ .##
#  #    #
# *   # #
#########
"""


def render_to(path, samples=96):
    sc = bpy.context.scene
    sc.cycles.samples = samples
    sc.render.filepath = path
    bpy.ops.render.render(write_still=True)


def do_level(out, camkey):
    reset()
    cam = Cam(**CAMS[camkey])
    objs, (w, h), _, wy = build_level(SAMPLE_STATE)
    # Mila at column 3, row 5, facing east, pushing
    mx, mr = 3, 5
    m = mila('mila_', 'push', acc=('collar:red',))
    parent_all(m, 'mila', loc=(mx + 0.5 + 0.06, wy(mr) + 0.5, 0), rotz=FACING['e'])
    pts = [V((x, y, z)) for x in (0, w) for y in (0, h) for z in (0, 1.0)]
    fw, fh, x0, y0 = cam.frame(pts, margin=10)
    cam.place(fw, fh, x0, y0)
    # where Mila's cell centre lands in the picture (the composer centres on it)
    mx_px, my_px = cam.screen(V((mx + 0.5, wy(mr) + 0.5, 0.25)))
    with open(os.path.join(out, 'level_%s.txt' % camkey), 'w') as fh_:
        fh_.write('%d %d %.1f %.1f\n' % (fw, fh, mx_px - x0, my_px - y0))
    render_to(os.path.join(out, 'level_%s.png' % camkey))


def do_mila(out):
    shots = [
        ('idle_s', 'idle', 's', ()),
        ('walk_e', 'walk', 'e', ()),
        ('push_e', 'push', 'e', ()),
        ('idle_n', 'idle', 'n', ()),
        ('sit_s', 'sit', 's', ()),
        ('walk_w', 'walk', 'w', ()),
        ('acc_bow', 'sit', 's', ('bow:pink', 'collar:red')),
        ('acc_party', 'sit', 's', ('party:mint', 'tag:blue')),
        ('acc_crown', 'sit', 's', ('crown', 'collar:purple')),
        ('acc_game', 'idle', 's', ('collar:red',)),
    ]
    only = [a for a in ARGS.get('only', '').split(',') if a]
    for name, pose, face, acc in shots:
        if only and name not in only:
            continue
        reset()
        cam = Cam(**CAMS['A'])
        cam.s = 56.0 * (4.0 if name.startswith(('acc', 'sit')) else 3.0)
        m = mila('m_', pose, acc)
        parent_all(m, 'mila', rotz=FACING[face])
        pts = [V((x, y, z)) for x in (-0.45, 0.45) for y in (-0.45, 0.45) for z in (0, 0.85)]
        fw, fh, x0, y0 = cam.frame(pts, margin=4)
        cam.place(fw, fh, x0, y0)
        render_to(os.path.join(out, 'mila_%s.png' % name), 128)


def do_casita(out):
    reset()
    cam = Cam(**CAMS['A'])
    cam.s = 56.0 * 1.9
    W, D = 3.6, 3.2       # a cosy room: x 0..W, y 0..D, back wall at y = D
    o = []
    for x in range(4):
        for y in range(4):
            o += floor_cell('cf%d_%d' % (x, y), x, y)
    for ob in o:
        ob.scale = (0.9, 0.8, 1.0)
    paper = principled('paper2', (0.96, 0.78, 0.70), rough=0.85)
    stripe = principled('stripe', (0.99, 0.88, 0.80), rough=0.85)
    n = int(W / 0.2)
    for k in range(n):
        o.append(box('bw%d' % k, k * 0.2, D, 0, (k + 1) * 0.2, D + 0.15, 2.0, paper if k % 2 else stripe))
    o.append(box('bwb', 0, D - 0.03, 0, W, D, 0.12, wood_mat('base', (0.55, 0.36, 0.22))))
    o.append(box('bwt', -0.02, D - 0.02, 2.0, W + 0.02, D + 0.17, 2.06, wood_mat('walltop', (0.16, 0.09, 0.06), 3)))
    frame = wood_mat('wframe', (0.96, 0.94, 0.9), 3)
    sky = principled('night', (0.05, 0.08, 0.25), emit=(0.10, 0.16, 0.45), es=1.4)
    moon = principled('moon', (1, 1, 0.9), emit=(1.0, 0.95, 0.75), es=6)
    star = principled('star', (1, 1, 1), emit=(1, 1, 0.9), es=8)
    wx0, wx1, wz0, wz1 = 1.9, 3.2, 0.75, 1.75
    o.append(box('win', wx0, D - 0.03, wz0, wx1, D - 0.01, wz1, sky))
    o.append(ellipsoid('moon', (wx1 - 0.3, D - 0.04, wz1 - 0.28), (0.12, 0.01, 0.12), moon))
    rnd = random.Random(2)
    for k in range(6):
        o.append(ellipsoid('st%d' % k, (rnd.uniform(wx0 + 0.1, wx1 - 0.5), D - 0.04, rnd.uniform(wz0 + 0.15, wz1 - 0.1)),
                           (0.016, 0.005, 0.016), star))
    for (a0, a1) in ((wx0 - 0.06, wx0), (wx1, wx1 + 0.06), ((wx0 + wx1) / 2 - 0.025, (wx0 + wx1) / 2 + 0.025)):
        o.append(box('wf%.2f' % a0, a0, D - 0.08, wz0, a1, D, wz1, frame))
    for (b0, b1) in ((wz0 - 0.06, wz0), (wz1, wz1 + 0.06), ((wz0 + wz1) / 2 - 0.025, (wz0 + wz1) / 2 + 0.025)):
        o.append(box('wh%.2f' % b0, wx0 - 0.06, D - 0.08, b0, wx1 + 0.06, D, b1, frame))
    o.append(box('sill', wx0 - 0.15, D - 0.22, wz0 - 0.1, wx1 + 0.15, D, wz0 - 0.03, frame))
    # curtains
    cur = principled('curtain', (0.95, 0.55, 0.65), rough=0.9, sheen=1)
    for cx in (wx0 - 0.2, wx1 + 0.08):
        for k in range(3):
            o.append(ellipsoid('cur%.1f%d' % (cx, k), (cx + 0.02 + k * 0.045, D - 0.12, 1.2), (0.035, 0.05, 0.62), cur))
    # a framed fish picture
    o.append(box('pic', 0.35, D - 0.04, 1.0, 1.25, D - 0.01, 1.62, frame))
    o.append(box('picin', 0.42, D - 0.05, 1.07, 1.18, D - 0.02, 1.55, principled('picbg', (0.55, 0.8, 0.95))))
    fishm = principled('fishp', (1.0, 0.55, 0.2), rough=0.5)
    o.append(ellipsoid('fishp', (0.75, D - 0.06, 1.31), (0.18, 0.01, 0.09), fishm))
    o.append(cone('fishpt', (0.93, D - 0.06, 1.31), 0.09, 0.0, 0.11, fishm, rot=(0, 90, 0), scale=(1, 0.1, 1)))
    # a shelf with a plant
    o.append(box('shelf', 0.3, D - 0.25, 1.85, 1.3, D, 1.9, frame))
    o += [cone('shpot', (1.1, D - 0.12, 1.9), 0.07, 0.09, 0.12, principled('pot', (0.80, 0.42, 0.28))),
          ellipsoid('shpl', (1.1, D - 0.12, 2.06), (0.12, 0.08, 0.08), principled('leaf', (0.22, 0.62, 0.30)))]
    # rug
    o += rug('rug', (1.8, 1.35), 1.35, 0.95, (0.55, 0.75, 0.88), (0.93, 0.96, 0.99))
    # cat bed (donut)
    bedm = principled('bed', (0.95, 0.55, 0.70), rough=0.9, sheen=1)
    bedi = principled('bedin', (1.0, 0.92, 0.85), rough=0.95, sheen=1)
    o.append(torus('bed', (0.65, 2.45, 0.13), 0.42, 0.14, bedm, scale=(1, 0.9, 1)))
    o.append(ellipsoid('bedin', (0.65, 2.45, 0.07), (0.42, 0.38, 0.06), bedi))
    # scratching post
    rope = principled('rope', (0.85, 0.72, 0.5), rough=1)
    carpet = principled('carpet', (0.55, 0.45, 0.85), rough=1, sheen=1)
    px, py = 3.05, 2.5
    o.append(box('spb', px - 0.35, py - 0.35, 0, px + 0.35, py + 0.35, 0.08, carpet, bevel=0.03))
    o.append(cone('spp', (px, py, 0.08), 0.09, 0.09, 1.0, rope))
    o.append(box('spt', px - 0.3, py - 0.3, 1.08, px + 0.3, py + 0.3, 1.16, carpet, bevel=0.03))
    o.append(ellipsoid('spball', (px + 0.22, py - 0.25, 0.92), (0.06, 0.06, 0.06), principled('ball2', (1, 0.3, 0.3))))
    o.append(tube('spstr', [(px + 0.22, py - 0.25, 1.1), (px + 0.225, py - 0.25, 1.02), (px + 0.22, py - 0.25, 0.97)],
                  [0.006, 0.006, 0.006], rope))
    # food and water
    bowl = principled('bowl', (0.3, 0.55, 0.95), rough=0.3, cc=0.5)
    kib = principled('kibble', (0.55, 0.32, 0.15), rough=0.8)
    o.append(cone('bowl', (0.45, 0.45, 0), 0.16, 0.21, 0.1, bowl))
    o.append(ellipsoid('kib', (0.45, 0.45, 0.09), (0.17, 0.17, 0.035), kib))
    o.append(cone('bowl2', (0.9, 0.38, 0), 0.15, 0.19, 0.1, principled('bowlw', (0.95, 0.95, 0.95), rough=0.2, cc=0.5)))
    o.append(ellipsoid('water', (0.9, 0.38, 0.09), (0.15, 0.15, 0.01),
                       principled('water', (0.5, 0.8, 1.0), rough=0.05, spec=1)))
    # cardboard box (cats!)
    card = principled('card', (0.78, 0.60, 0.38), rough=0.85)
    card2 = principled('card2', (0.62, 0.46, 0.28), rough=0.9)
    bx0, by0, bw, bd = 2.55, 0.35, 0.75, 0.6
    o.append(box('cbin', bx0, by0, 0, bx0 + bw, by0 + bd, 0.02, card2))
    for (a_, b_, c_, d_) in ((bx0, by0, bx0 + bw, by0 + 0.03), (bx0, by0 + bd - 0.03, bx0 + bw, by0 + bd),
                             (bx0, by0, bx0 + 0.03, by0 + bd), (bx0 + bw - 0.03, by0, bx0 + bw, by0 + bd)):
        o.append(box('cbw%.2f%.2f' % (a_, b_), a_, b_, 0, c_, d_, 0.42, card))
    flap = box('cbf', 0, -0.28, -0.01, bw, 0.0, 0.0, card)
    flap.location = (bx0, by0, 0.42)
    flap.rotation_euler = (math.radians(-35), 0, 0)
    o.append(flap)
    # toys: mouse, yarn, feather wand
    grey = principled('mouse', (0.62, 0.62, 0.68), rough=0.8, sheen=1)
    mx, my = 2.55, 1.75
    o.append(ellipsoid('mouse', (mx, my, 0.06), (0.1, 0.06, 0.06), grey, rot=(0, 0, 30)))
    for sx in (1, -1):
        o.append(ellipsoid('mear%d' % sx, (mx - 0.07 - 0.02 * sx, my - 0.03 + 0.04 * sx, 0.11), (0.028, 0.01, 0.028),
                           principled('pink', (0.95, 0.45, 0.55))))
    o.append(tube('mtail', [(mx + 0.08, my + 0.05, 0.04), (mx + 0.2, my + 0.08, 0.02), (mx + 0.26, my + 0.2, 0.01)],
                  [0.008, 0.007, 0.005], principled('pink', (0.95, 0.45, 0.55))))
    for ob in yarn('cy', (1.05, 1.05), YARN[1]):
        ob.scale = tuple(v * 0.7 for v in ob.scale)
        ob.location = (ob.location.x, ob.location.y, ob.location.z * 0.7)
        o.append(ob)
    feather = principled('feather', (0.3, 0.9, 0.8), rough=0.6, sheen=1)
    stick = wood_mat('stick', (0.6, 0.4, 0.2))
    o.append(tube('wand', [(0.25, 1.75, 0.02), (0.55, 1.68, 0.03), (0.85, 1.62, 0.03)], [0.011, 0.011, 0.011], stick))
    o.append(ellipsoid('fth', (0.95, 1.6, 0.05), (0.12, 0.035, 0.02), feather, rot=(0, 10, -15)))
    # Mila sitting on the rug with her bow and collar
    m = mila('mila_', 'sit', ('bow:pink', 'collar:red'))
    parent_all(m, 'mila', loc=(1.85, 1.45, 0.012), rotz=-10)
    lt = bpy.data.lights.new('room', 'POINT')
    lt.color = (1.0, 0.8, 0.6)
    lt.energy = 120
    lt.shadow_soft_size = 0.5
    lo = bpy.data.objects.new('room', lt)
    lo.location = (1.4, 1.6, 2.4)
    link(lo)
    # framing: 368 x 448, the room a little below the middle (room for the HUD)
    top = cam.screen(V((W / 2, D + 0.15, 2.1)))
    bot = cam.screen(V((W / 2, 0, 0)))
    cy = (top[1] + bot[1]) / 2.0 - 6
    cx = cam.screen(V((W / 2, 0, 0)))[0]
    cam.place(368, 448, cx - 184, cy - 224)
    render_to(os.path.join(out, 'casita.png'), 160)


ARGS = {}


def main():
    argv = sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else []
    it = iter(argv)
    for a in it:
        if a.startswith('--'):
            ARGS[a[2:]] = next(it)
    out = os.path.abspath(ARGS['out'])
    os.makedirs(out, exist_ok=True)
    for what in ARGS.get('what', 'level_A,level_B,mila,casita').split(','):
        if what.startswith('level_'):
            do_level(out, what[6:])
        elif what == 'mila':
            do_mila(out)
        elif what == 'casita':
            do_casita(out)


main()
