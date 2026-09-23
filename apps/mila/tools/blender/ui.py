"""Mila - UI art (SPEC.md section 8): logo, world emblems, icons.

    /Applications/Blender.app/Contents/MacOS/Blender -b -P ui.py -- --out ../../assets/ui [--only icon_coin,...]

Colour only, transparent, a free camera (orthographic, from the front and a
little above) and a soft light from the front-left. Every picture is rendered
at 4x and averaged down in linear light (premultiplied), so the small icons
keep clean edges. meta.json gets w, h, ax = ay = 0, kind 'ui'.

Phase 1: ui_logo, ui_emblem_living, icon_coin, icon_star, icon_star_empty,
icon_undo, icon_shop.
"""
import math
import os
import sys
import time

import bmesh
import bpy
import numpy as np
from mathutils import Matrix, Vector as V

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ml_common as C  # noqa: E402
import mapui_kit as K  # noqa: E402

SS = 4
META = {}
TIMES = {}


# ---------------------------------------------------------------------------
# camera, light, render
# ---------------------------------------------------------------------------

def _points(objs):
    bpy.context.view_layer.update()
    dg = bpy.context.evaluated_depsgraph_get()
    pts = []
    for ob in objs:
        if ob.type not in ('MESH', 'CURVE') or ob.is_shadow_catcher:
            continue
        ev = ob.evaluated_get(dg)
        me = ev.to_mesh()
        pts.extend(ev.matrix_world @ v.co for v in me.vertices)
        ev.to_mesh_clear()
    return pts


def frame(objs, w, h, elev=10.0, yaw=0.0, margin=2):
    """Point the camera from the front (looking +Y), `elev` degrees from
    above, and fit objs into w x h with `margin` px free all round."""
    e, y = math.radians(elev), math.radians(yaw)
    F = V((math.sin(y) * math.cos(e), math.cos(y) * math.cos(e), -math.sin(e)))
    R = V((math.cos(y), -math.sin(y), 0.0))
    UP = R.cross(F)
    if UP.z < 0:
        UP = -UP
    pts = _points(objs)
    us = [p.dot(R) for p in pts]
    vs = [p.dot(UP) for p in pts]
    u0, u1, v0, v1 = min(us), max(us), min(vs), max(vs)
    s = min((w - 2 * margin) / (u1 - u0), (h - 2 * margin) / (v1 - v0))   # px per unit
    sc = C._STATE['scene']
    cam = C._STATE['cam']
    sc.render.resolution_x, sc.render.resolution_y = w * SS, h * SS
    cam.data.sensor_fit = 'HORIZONTAL' if w >= h else 'VERTICAL'
    cam.data.ortho_scale = max(w, h) / s
    c = R * ((u0 + u1) / 2) + UP * ((v0 + v1) / 2)
    cam.matrix_world = Matrix.Translation(c - F * 50.0) @ Matrix((R, UP, -F)).transposed().to_4x4()


def light(strength=3.0, towards_sun=(-0.45, -0.8, 0.95), sky=0.55):
    sun = C._STATE['sun']
    L = V(towards_sun).normalized()
    sun.rotation_euler = (-L).to_track_quat('-Z', 'Y').to_euler()
    sun.data.energy = strength
    sun.data.angle = math.radians(12)
    C._STATE['world'].node_tree.nodes['Background'].inputs['Strength'].default_value = sky


def render(out, name, w, h, samples=96):
    t = time.time()
    out = os.path.abspath(out)
    tmp = os.path.join(out, '_tmp')
    os.makedirs(tmp, exist_ok=True)
    a = C._render_exr(os.path.join(tmp, 'u.exr'), samples, 1.5, True, 6)      # premultiplied, linear
    a = np.clip(a, 0, None).reshape(h, SS, w, SS, 4).mean(axis=(1, 3))
    C.write_png(os.path.join(out, name + '.png'), C._to_rgba(a))
    META[name] = dict(kind='ui', w=w, h=h, ax=0, ay=0, files={'img': name + '.png'})
    TIMES[name] = time.time() - t
    print('rendered %s %dx%d in %.1fs' % (name, w, h, TIMES[name]))


def start():
    C.reset('map')
    light()


# ---------------------------------------------------------------------------
# the logo: "Mila" in puffy round letters, cat ears on the M, a paw for the
# i's dot, a tail curling off the a; on a cream sticker with a plum edge
# ---------------------------------------------------------------------------

IX = 1.27       # the i's x (its dot, the paw, sits above it)


def letters(tag, r, key, grow=0.0, y=0.0, dx=0.0, dz=0.0, inner=None, cat=None):
    """The strokes of "Mila" (capsules, to be fused). cat: the key of the
    ears and the tail, which are then returned apart: (letters, cat parts)."""
    def P(x, z):
        return (x + dx, y, z + dz)
    parts = []

    def stroke(a, b):
        parts.extend(K.capsule(tag + 's%d' % len(parts), P(*a), P(*b), r, key))
    # M
    stroke((0, 0), (0, 1))
    stroke((0, 1), (0.38, 0.42))
    stroke((0.38, 0.42), (0.76, 1))
    stroke((0.76, 1), (0.76, 0))
    ears = []
    for sx, ex in ((-1, -0.02), (1, 0.78)):
        c = P(ex + sx * 0.02, 1.08)
        ears.append(K.cone(tag + 'ear%d' % sx, c, 0.21 + grow, 0.02 + grow * 0.5, 0.36 + grow * 1.2, cat or key,
                           rot=(0, sx * 22, 0), scale=(1, 0.55, 1), verts=32))
        if inner:
            K.cone(tag + 'eari%d' % sx, (c[0] + sx * 0.02, c[1] - 0.09, c[2] + 0.05), 0.13, 0.01, 0.24, inner,
                   rot=(0, sx * 22, 0), scale=(1, 0.3, 1), verts=32)
    catp = ears
    # i (the dot is a paw), l
    stroke((IX, 0), (IX, 0.6))
    stroke((1.62, 0), (1.62, 1.05))
    # a: a round bowl and its stem, the tail curling off the foot of the stem
    n = 20
    for k in range(n):
        a0, a1 = 2 * math.pi * k / n, 2 * math.pi * (k + 1) / n
        stroke((2.2 + 0.31 * math.cos(a0), 0.31 + 0.31 * math.sin(a0)),
               (2.2 + 0.31 * math.cos(a1), 0.31 + 0.31 * math.sin(a1)))
    stroke((2.51, 0.62), (2.51, 0.02))
    # Mila's tail: out of the foot of the stem, rising to the right, the tip curled
    # short, hanging down off the foot of the a, the tip curled (not a letter)
    tail = [(2.6, 0.22), (2.7, 0.06), (2.72, -0.14), (2.77, -0.3), (2.9, -0.36), (2.99, -0.28)]
    t = r * 0.55 + grow
    rr = [t, t * 0.95, t * 0.88, t * 0.8, t * 0.7, t * 0.6]
    catp.append(K.tube(tag + 'tail', [P(*p) for p in tail], rr, cat or key, res=16))
    if cat:
        return parts, catp
    return parts + catp


def logo(out, samples):
    start()
    pink = K.pm('lg_pink', (0.92, 0.20, 0.40), rough=0.3, cc=0.8, sheen=0.3)
    cream = K.pm('lg_cream', (1.0, 0.94, 0.86), rough=0.5, cc=0.3)
    plum = K.pm('lg_plum', (0.42, 0.16, 0.34), rough=0.5)
    inner = K.pm('lg_inner', (1.0, 0.62, 0.72), rough=0.5)
    pawk = K.pm('lg_paw', (1.0, 0.94, 0.86), rough=0.4, cc=0.5)
    r = 0.15
    furk = K.fur('lg_fur')
    lt, catp = letters('f', r, pink, inner=inner, cat=furk)
    o = [K.merge_skin(lt, 'front', pink, voxel=0.012, smooth=4),
         K.merge_skin(catp, 'cat', furk, voxel=0.01, smooth=4)]
    # the dot of the i: a paw print, and the inner ears
    o += K.paw_upright('dot', (IX, -0.02, 0.9), 1.45, pawk, depth=0.1)
    o += [ob for ob in bpy.context.scene.objects if ob.name.startswith('feari')]
    back = letters('b', r + 0.075, cream, grow=0.07, y=0.12)
    back += K.paw_upright('bdot', (IX, 0.1, 0.9), 1.9, cream, depth=0.1)
    o.append(K.merge_skin(back, 'back', cream, voxel=0.014, smooth=3))
    edge = letters('e', r + 0.075, plum, grow=0.07, y=0.24, dx=0.035, dz=-0.07)
    edge += K.paw_upright('edot', (IX + 0.035, 0.22, 0.9 - 0.07), 1.9, plum, depth=0.1)
    o.append(K.merge_skin(edge, 'edge', plum, voxel=0.014, smooth=3))
    frame(o, 300, 120, elev=8, margin=3)
    render(out, 'ui_logo', 300, 120, samples)


# ---------------------------------------------------------------------------
# emblems: a round badge, the world's colours on its face, its object on top
# ---------------------------------------------------------------------------

def badge(face_top, face_bottom, rim, split=-0.28):
    """A round badge standing in the XZ plane facing -Y: the face in two
    colours (wall above, floor below), a puffy rim."""
    o = [K.cone('bdisc', (0, 0.1, 0), 1.0, 1.0, 0.2, face_top, rot=(90, 0, 0), verts=64)]
    # the floor: the circle's segment below z = split, a hair in front
    me = bpy.data.meshes.new('bfloor')
    bm = bmesh.new()
    pts = []
    zc = split
    a0 = math.asin(zc / 1.0)
    n = 40
    for k in range(n + 1):
        a = (math.pi - a0) + (2 * math.pi - (math.pi - 2 * a0)) * 0 + ((2 * math.pi + a0) - (math.pi - a0)) * k / n
        pts.append(bm.verts.new((math.cos(a) * 0.99, -0.105, math.sin(a) * 0.99)))
    bm.faces.new(pts)
    bm.to_mesh(me)
    bm.free()
    fl = bpy.data.objects.new('bfloor', me)
    C.link(fl)
    C.assign(fl, face_bottom)
    o.append(fl)
    o.append(K.box('bskirt', -0.99, -0.107, split - 0.035, 0.99, -0.102, split + 0.005, K.pm('bskirt', (0.97, 0.94, 0.88))))
    o.append(K.torus('brim', (0, -0.1, 0), 1.0, 0.1, rim, rot=(90, 0, 0), n=64, m=16))
    return o


def emblem_living(out, samples):
    start()
    paper = K.stripes('em_paper', (0.30, 0.56, 0.60), (0.38, 0.64, 0.67), width=0.08, axis='X')
    floor = K.stripes('em_floor', (0.80, 0.53, 0.30), (0.88, 0.62, 0.37), width=0.1, axis='Z', rough=0.6)
    rim = K.pm('em_rim', (1.0, 0.86, 0.52), rough=0.35, cc=0.6)
    o = badge(paper, floor, rim)
    # a pink yarn ball with a loose thread, standing on the floor
    col = (0.92, 0.26, 0.48)
    m = K.yarn('em_yarn', col)
    c = V((0.0, -0.45, -0.12))
    o.append(K.ellipsoid('ball', c, (0.47, 0.47, 0.46), m, rot=(20, 35, 10), seg=48, rings=32))
    for k, rot in enumerate(((70, 10, 20), (15, 80, 60), (120, 40, 150))):
        o.append(K.torus('bw%d' % k, c, 0.46, 0.028, m, rot=rot, n=48, m=8))
    o.append(K.tube('thr', [(0.3, -0.62, -0.42), (0.45, -0.66, -0.62), (0.62, -0.6, -0.66), (0.72, -0.55, -0.55),
                            (0.66, -0.52, -0.45)], [0.035] * 5, m))
    frame(o, 72, 72, elev=6, margin=1)
    render(out, 'ui_emblem_living', 72, 72, samples)


# ---------------------------------------------------------------------------
# icons (40 x 40)
# ---------------------------------------------------------------------------

def icon_coin(out, samples):
    start()
    gold = K.pm('coin', (1.0, 0.70, 0.14), rough=0.28, metal=0.35, cc=0.7)
    gold2 = K.pm('coin2', (1.0, 0.84, 0.36), rough=0.25, metal=0.2, cc=0.7)
    o = [K.cone('c', (0, 0.1, 0), 1.0, 1.0, 0.22, gold, rot=(90, 0, 0), verts=64, bevel=0.06),
         K.torus('crim', (0, -0.13, 0), 0.84, 0.07, gold2, rot=(90, 0, 0), n=64, m=12)]
    o += K.paw_upright('cp', (0, -0.14, 0.02), 3.6, gold2, depth=0.05)
    K.parent_all(o, 'coin', rotz=-22)
    frame(o, 40, 40, elev=8, margin=2)
    render(out, 'icon_coin', 40, 40, samples)


def star_mesh(name, key, r0=1.0, r1=0.5, t=0.16, dome=0.34):
    """A puffy five-pointed star facing -Y (domed front and back)."""
    me = bpy.data.meshes.new(name)
    bm = bmesh.new()
    ring_f, ring_b = [], []
    for k in range(10):
        a = math.pi / 2 + k * math.pi / 5
        r = r0 if k % 2 == 0 else r1
        ring_f.append(bm.verts.new((r * math.cos(a), -t, r * math.sin(a))))
        ring_b.append(bm.verts.new((r * math.cos(a), t, r * math.sin(a))))
    cf = bm.verts.new((0, -t - dome, 0))
    cb = bm.verts.new((0, t + dome, 0))
    for k in range(10):
        k2 = (k + 1) % 10
        bm.faces.new((cf, ring_f[k2], ring_f[k]))
        bm.faces.new((cb, ring_b[k], ring_b[k2]))
        bm.faces.new((ring_f[k], ring_f[k2], ring_b[k2], ring_b[k]))
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    bm.to_mesh(me)
    bm.free()
    ob = K._obj(name, me, key)
    bv = ob.modifiers.new('bv', 'BEVEL')
    bv.width = 0.07
    bv.segments = 3
    sd = ob.modifiers.new('sd', 'SUBSURF')
    sd.levels = sd.render_levels = 2
    return ob


def icon_star(out, samples, empty=False):
    start()
    if empty:
        key = K.pm('star_e', (0.17, 0.16, 0.23), rough=0.5, cc=0.4)
    else:
        key = K.pm('star', (1.0, 0.76, 0.12), rough=0.25, cc=0.8, emit=(1.0, 0.62, 0.05), es=0.25)
    o = [star_mesh('star', key)]
    frame(o, 40, 40, elev=6, margin=2)
    render(out, 'icon_star_empty' if empty else 'icon_star', 40, 40, samples)


def arc_arrow(th0, th1, key, R=0.58, r=0.14, n=16):
    """A round arrow along a circle from angle th0 to th1 (degrees, either
    way), the head at th1 pointing the way it goes."""
    a0, a1 = math.radians(th0), math.radians(th1)
    pts = [(R * math.cos(a0 + (a1 - a0) * k / n), 0, R * math.sin(a0 + (a1 - a0) * k / n)) for k in range(n + 1)]
    o = [K.tube('arc', pts, [r] * len(pts), key, res=6),
         K.ellipsoid('cap', pts[0], (r, r, r), key)]
    tip = V(pts[-1])
    s = 1 if a1 > a0 else -1
    tan = V((-math.sin(a1) * s, 0, math.cos(a1) * s))
    head = K.cone('head', (0, 0, 0), 0.36, 0.0, 0.44, key, verts=4, smooth=False, bevel=0.03)
    yl = V((0, 1, 0))
    rot = Matrix((yl.cross(tan), yl, tan)).transposed().to_4x4()
    head.matrix_world = Matrix.Translation(tip - tan * 0.05) @ rot @ Matrix.Diagonal((1, 0.45, 1, 1))
    o.append(head)
    return o


def icon_undo(out, samples):
    start()
    o = arc_arrow(-45, 172, K.pm('undo', (0.99, 0.96, 0.91), rough=0.35, cc=0.5))
    frame(o, 40, 40, elev=4, margin=3)
    render(out, 'icon_undo', 40, 40, samples)


def icon_restart(out, samples):
    start()
    o = arc_arrow(105, -192, K.pm('undo', (0.99, 0.96, 0.91), rough=0.35, cc=0.5), n=24)
    frame(o, 40, 40, elev=4, margin=3)
    render(out, 'icon_restart', 40, 40, samples)


def icon_paw(out, samples):
    start()
    o = K.paw_upright('paw', (0, 0, 0), 5.0, K.pm('paw', (0.96, 0.42, 0.60), rough=0.3, cc=0.8), depth=0.16)
    frame(o, 40, 40, elev=8, margin=2)
    render(out, 'icon_paw', 40, 40, samples)


def heart_outline(n=40, s=0.06):
    pts = []
    for k in range(n):
        t = 2 * math.pi * k / n
        x = 16 * math.sin(t) ** 3
        z = 13 * math.cos(t) - 5 * math.cos(2 * t) - 2 * math.cos(3 * t) - math.cos(4 * t)
        pts.append((x * s, z * s))
    return pts


def icon_heart(out, samples):
    start()
    o = [K.puffy('heart', heart_outline(), K.pm('heart', (0.95, 0.24, 0.42), rough=0.25, cc=0.9), t=0.1, dome=0.32,
                 bevel=0.05)]
    frame(o, 40, 40, elev=6, margin=2)
    render(out, 'icon_heart', 40, 40, samples)


def icon_play(out, samples):
    start()
    tri = [(-0.45, 0.62), (-0.45, -0.62), (0.62, 0.0)]
    pts = []
    for (ax, az), (bx, bz) in zip(tri, tri[1:] + tri[:1]):       # subdivide the edges
        for k in range(6):
            pts.append((ax + (bx - ax) * k / 6, az + (bz - az) * k / 6))
    o = [K.puffy('play', pts, K.pm('play', (0.30, 0.82, 0.60), rough=0.3, cc=0.8), t=0.1, dome=0.26, bevel=0.12)]
    frame(o, 40, 40, elev=6, margin=3)
    render(out, 'icon_play', 40, 40, samples)


def icon_home(out, samples):
    start()
    walls = K.pm('hwall', (0.99, 0.90, 0.80), rough=0.6)
    roof = K.pm('hroof', (0.92, 0.40, 0.46), rough=0.4, cc=0.4)
    inner = K.pm('hin', (1.0, 0.72, 0.78), rough=0.5)
    door = K.pm('hdoor', (0.25, 0.62, 0.66), rough=0.4, cc=0.3)
    win = K.pm('hwin', (1.0, 0.80, 0.4), emit=(1.0, 0.7, 0.3), es=1.0)
    o = [K.box('body', -0.55, -0.4, 0, 0.55, 0.4, 0.75, walls, bevel=0.05)]
    v = [(-0.72, -0.5, 0.68), (0.72, -0.5, 0.68), (0.72, 0.0, 1.25), (-0.72, 0.0, 1.25), (-0.72, 0.5, 0.68),
         (0.72, 0.5, 0.68)]
    rf = C.mesh_object('roof', v + [(a, b, c + 0.1) for a, b, c in v],
                       [(6, 7, 8, 9), (9, 8, 11, 10), (0, 3, 2, 1), (3, 4, 5, 2), (0, 1, 7, 6), (4, 10, 11, 5),
                        (0, 6, 9, 3), (3, 9, 10, 4), (1, 2, 8, 7), (2, 5, 11, 8)], roof)
    bm = bmesh.new()
    bm.from_mesh(rf.data)
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    bm.to_mesh(rf.data)
    bm.free()
    rf.modifiers.new('bv', 'BEVEL').width = 0.04
    o.append(rf)
    for sx in (-1, 1):
        o.append(K.cone('ear%d' % sx, (sx * 0.5, 0.0, 1.25), 0.2, 0.01, 0.34, roof, rot=(0, sx * 18, 0),
                        scale=(1, 0.5, 1)))
        o.append(K.cone('eari%d' % sx, (sx * 0.5, -0.06, 1.28), 0.12, 0.01, 0.24, inner, rot=(0, sx * 18, 0),
                        scale=(1, 0.3, 1)))
    o.append(K.box('door', -0.17, -0.44, 0, 0.17, -0.4, 0.46, door, bevel=0.02))
    o.append(K.cone('doort', (0, -0.4, 0.46), 0.17, 0.17, 0.04, door, rot=(90, 0, 0), verts=24))
    o.append(K.cone('win', (0.36, -0.4, 0.48), 0.1, 0.1, 0.03, win, rot=(90, 0, 0), verts=24))
    K.parent_all(o, 'home', rotz=-18)
    frame(o, 40, 40, elev=12, margin=2)
    render(out, 'icon_home', 40, 40, samples)


def icon_gear(out, samples):
    start()
    grey = K.pm('gear', (0.74, 0.74, 0.84), rough=0.3, cc=0.6, metal=0.2)
    pink = K.pm('gearp', (0.96, 0.46, 0.62), rough=0.3, cc=0.6)
    dark = K.pm('gearh', (0.16, 0.14, 0.2), rough=0.5)
    parts = [K.cone('disc', (0, 0.12, 0), 0.72, 0.72, 0.24, grey, rot=(90, 0, 0), verts=48)]
    for k in range(8):
        a = 2 * math.pi * k / 8
        parts += K.capsule('t%d' % k, (0.72 * math.cos(a), 0, 0.72 * math.sin(a)),
                           (0.9 * math.cos(a), 0, 0.9 * math.sin(a)), 0.15, grey)
    o = [K.merge_skin(parts, 'gear', grey, voxel=0.025, smooth=3)]
    o.append(K.torus('hub', (0, -0.13, 0), 0.3, 0.08, pink, rot=(90, 0, 0), n=40, m=12))
    o.append(K.cone('hole', (0, -0.1, 0), 0.23, 0.23, 0.04, dark, rot=(90, 0, 0), verts=32))
    frame(o, 40, 40, elev=6, margin=2)
    render(out, 'icon_gear', 40, 40, samples)


def icon_gift(out, samples):
    start()
    box_ = K.pm('gift', (0.96, 0.46, 0.62), rough=0.4, cc=0.4)
    lid = K.pm('giftlid', (0.99, 0.60, 0.72), rough=0.4, cc=0.4)
    rib = K.pm('ribbon', (0.25, 0.74, 0.76), rough=0.3, cc=0.6)
    o = [K.box('b', -0.5, -0.5, 0, 0.5, 0.5, 0.7, box_, bevel=0.04),
         K.box('l', -0.56, -0.56, 0.68, 0.56, 0.56, 0.88, lid, bevel=0.04),
         K.box('r1', -0.1, -0.575, 0, 0.1, 0.575, 0.895, rib),
         K.box('r2', -0.575, -0.1, 0, 0.575, 0.1, 0.895, rib)]
    for sx in (-1, 1):
        o.append(K.torus('bow%d' % sx, (sx * 0.2, 0, 1.02), 0.16, 0.05, rib, rot=(90, sx * -25, 0), scale=(1.3, 1, 0.9)))
    o.append(K.ellipsoid('knot', (0, 0, 0.94), (0.09, 0.09, 0.08), rib))
    K.parent_all(o, 'gift', rotz=-22)
    frame(o, 40, 40, elev=18, margin=2)
    render(out, 'icon_gift', 40, 40, samples)


def icon_lock(out, samples):
    start()
    gold = K.pm('lock', (1.0, 0.72, 0.18), rough=0.28, metal=0.35, cc=0.7)
    steel = K.pm('shackle', (0.80, 0.82, 0.90), rough=0.25, metal=0.8)
    dark = K.pm('keyhole', (0.36, 0.20, 0.08), rough=0.5)
    o = [K.box('body', -0.5, -0.22, -0.55, 0.5, 0.22, 0.2, gold, bevel=0.12, segments=4),
         K.torus('sh', (0, 0, 0.2), 0.33, 0.085, steel, rot=(90, 0, 0), arc=0.5, n=24, m=12)]
    for sx in (-1, 1):
        o.append(K.cone('leg%d' % sx, (sx * 0.33, 0, 0.1), 0.085, 0.085, 0.12, steel))
    o += K.paw_upright('kh', (0, -0.23, -0.2), 2.4, dark, depth=0.02)
    frame(o, 40, 40, elev=6, margin=2)
    render(out, 'icon_lock', 40, 40, samples)


def icon_tab_hat(out, samples):
    start()
    cone_ = K.pm('phat', (0.30, 0.82, 0.62), rough=0.35, cc=0.5)
    white = K.pm('pom', (0.99, 0.98, 0.96), rough=0.9, sheen=1.0)
    dot = K.pm('pdot', (0.98, 0.45, 0.65), rough=0.4)
    o = [K.cone('hat', (0, 0, 0), 0.5, 0.02, 1.25, cone_, rot=(0, 10, 0), verts=40),
         K.torus('rim', (0, 0, 0.03), 0.5, 0.08, white, n=40, m=12),
         K.ellipsoid('pom', (0.22, 0, 1.28), (0.17, 0.17, 0.17), white)]
    for k, (a, h) in enumerate(((-60, 0.3), (-110, 0.55), (-80, 0.8), (-135, 0.25), (-35, 0.62))):
        r = 0.5 * (1 - h / 1.25) + 0.01
        aa = math.radians(a)
        o.append(K.ellipsoid('d%d' % k, (r * math.cos(aa) + h * 0.17, r * math.sin(aa), h), (0.065, 0.03, 0.065), dot,
                             rot=(0, 0, a + 90)))
    frame(o, 40, 40, elev=12, margin=2)
    render(out, 'icon_tab_hat', 40, 40, samples)


def icon_tab_neck(out, samples):
    start()
    red = K.pm('collar', (0.88, 0.14, 0.22), rough=0.35, cc=0.5)
    gold = K.pm('bell', (1.0, 0.76, 0.22), rough=0.2, metal=0.8, cc=0.5)
    dark = K.pm('bellslot', (0.3, 0.18, 0.05))
    o = [K.torus('collar', (0, 0, 0.3), 0.62, 0.12, red, rot=(62, 0, 0), n=48, m=14),
         K.ellipsoid('bell', (0, -0.62, -0.1), (0.26, 0.26, 0.26), gold),
         K.torus('loop', (0, -0.6, 0.15), 0.08, 0.03, gold, rot=(90, 0, 0)),
         K.box('slot', -0.16, -0.86, -0.2, 0.16, -0.82, -0.16, dark),
         K.ellipsoid('hole', (0, -0.86, -0.22), (0.05, 0.02, 0.05), dark)]
    frame(o, 40, 40, elev=10, margin=2)
    render(out, 'icon_tab_neck', 40, 40, samples)


def icon_tab_toy(out, samples):
    start()
    grey = K.pm('mouse', (0.66, 0.66, 0.74), rough=0.8, sheen=1.0)
    pink = K.pm('mpink', (0.98, 0.55, 0.65), rough=0.5)
    eye = K.pm('meye', (0.08, 0.06, 0.1), rough=0.2, spec=0.8)
    o = [K.ellipsoid('body', (0, 0, 0.3), (0.55, 0.34, 0.3), grey, rot=(0, -8, 0)),
         K.ellipsoid('snout', (-0.52, 0, 0.26), (0.18, 0.14, 0.14), grey),
         K.ellipsoid('nose', (-0.69, -0.02, 0.27), (0.05, 0.05, 0.05), pink),
         K.ellipsoid('eye', (-0.46, -0.16, 0.38), (0.04, 0.02, 0.05), eye)]
    for k, dy in enumerate((-0.12, 0.14)):
        o.append(K.ellipsoid('ear%d' % k, (-0.3, dy, 0.6), (0.16, 0.05, 0.16), grey, rot=(0, 0, -15)))
        o.append(K.ellipsoid('eari%d' % k, (-0.31, dy - 0.03, 0.6), (0.11, 0.03, 0.11), pink, rot=(0, 0, -15)))
    o.append(K.tube('tail', [(0.52, 0, 0.2), (0.75, -0.05, 0.12), (0.9, -0.05, 0.3), (0.8, -0.05, 0.48),
                             (0.68, -0.05, 0.42)], [0.04, 0.035, 0.03, 0.025, 0.02], pink))
    frame(o, 40, 40, elev=10, margin=2)
    render(out, 'icon_tab_toy', 40, 40, samples)


def icon_link(out, samples):
    start()
    teal = K.pm('linka', (0.25, 0.74, 0.76), rough=0.3, cc=0.7)
    pink = K.pm('linkb', (0.96, 0.44, 0.62), rough=0.3, cc=0.7)
    o = [K.torus('a', (-0.32, 0, 0), 0.4, 0.12, teal, rot=(90, 0, -20), scale=(1.35, 1, 1), n=48, m=14),
         K.torus('b', (0.32, 0, 0), 0.4, 0.12, pink, rot=(25, 0, -20), scale=(1.35, 1, 1), n=48, m=14)]
    frame(o, 40, 40, elev=8, margin=2)
    render(out, 'icon_link', 40, 40, samples)


# --- the other emblems ---

def em_frame(name, o, samples, out):
    frame(o, 72, 72, elev=6, margin=1)
    render(out, name, 72, 72, samples)


def emblem_kitchen(out, samples):
    start()
    wall = K.tiles('em_ktile', (0.99, 0.88, 0.52), (1.0, 0.98, 0.92), w=0.2, h=0.2, plane='XZ', col2=(0.97, 0.84, 0.46))
    floor = K.checker('em_kfloor', (0.40, 0.78, 0.64), (0.95, 0.93, 0.88), size=0.25)
    o = badge(wall, floor, K.pm('em_rim', (1.0, 0.86, 0.52), rough=0.35, cc=0.6))
    body = K.pm('em_tin', (0.88, 0.24, 0.30), rough=0.25, metal=0.3, cc=0.8)
    lid = K.pm('em_tinlid', (0.98, 0.46, 0.52), rough=0.25, metal=0.3, cc=0.8)
    band = K.pm('em_band', (0.99, 0.95, 0.86), rough=0.5)
    parts = [K.cone('tin', (0, 0, 0), 0.42, 0.42, 0.52, body, verts=40, bevel=0.03),
             K.cone('band', (0, 0, 0.16), 0.432, 0.432, 0.17, band, verts=40),
             K.cone('lid', (0, 0, 0.49), 0.46, 0.46, 0.12, lid, verts=40, bevel=0.045)]
    parts += K.paw_upright('bp', (0, -0.44, 0.245), 1.25, K.pm('em_bpaw', (0.88, 0.24, 0.30)), depth=0.012)
    root = K.parent_all(parts, 'tinr', loc=(0, -0.5, -0.62))
    root.rotation_euler = (math.radians(14), 0, 0)
    o += parts
    em_frame('ui_emblem_kitchen', o, samples, out)


def emblem_garden(out, samples):
    start()
    sky = K.pm('em_sky', (0.55, 0.80, 0.98), rough=0.7)
    grass = K.mottled('em_grass', (0.34, 0.66, 0.28), (0.46, 0.76, 0.34), scale=4.0, rough=0.9)
    o = badge(sky, grass, K.pm('em_rim', (1.0, 0.86, 0.52), rough=0.35, cc=0.6), split=-0.2)
    cl = K.pm('em_cloud', (1.0, 1.0, 1.0), rough=0.9)
    for k, (x, z, r) in enumerate(((-0.45, 0.5, 0.16), (-0.28, 0.56, 0.2), (-0.1, 0.5, 0.15), (0.42, 0.34, 0.12),
                                   (0.56, 0.37, 0.14))):
        o.append(K.ellipsoid('cl%d' % k, (x, -0.11, z), (r, 0.02, r * 0.8), cl))
    pot = K.pm('em_pot', (0.88, 0.46, 0.30), rough=0.6)
    leaf = K.pm('em_leaf', (0.26, 0.64, 0.30), rough=0.5)
    petal = K.pm('em_petal', (0.96, 0.38, 0.62), rough=0.5, sheen=0.5)
    mid = K.pm('em_mid', (1.0, 0.84, 0.28), rough=0.5)
    y = -0.5
    o += [K.cone('pot', (0, y, -0.78), 0.26, 0.34, 0.42, pot, verts=32),
          K.torus('lip', (0, y, -0.36), 0.35, 0.06, pot),
          K.cone('stem', (0, y, -0.36), 0.03, 0.025, 0.4, leaf)]
    for sx in (-1, 1):
        o.append(K.ellipsoid('lf%d' % sx, (sx * 0.16, y - 0.05, -0.22), (0.17, 0.06, 0.06), leaf, rot=(0, sx * -30, 0)))
    for k in range(7):
        a = 2 * math.pi * k / 7
        o.append(K.ellipsoid('pt%d' % k, (0.19 * math.cos(a), y - 0.1, 0.1 + 0.19 * math.sin(a)), (0.13, 0.04, 0.09),
                             petal, rot=(0, -math.degrees(a), 0)))
    o.append(K.ellipsoid('mid', (0, y - 0.14, 0.1), (0.1, 0.05, 0.1), mid))
    em_frame('ui_emblem_garden', o, samples, out)


def emblem_attic(out, samples):
    start()
    boards = K.tiles('em_awall', (0.56, 0.40, 0.27), (0.30, 0.19, 0.12), w=0.22, h=4.0, plane='XZ',
                     col2=(0.50, 0.35, 0.23), rough=0.7, cc=0.0)
    floor = K.stripes('em_afloor', (0.78, 0.57, 0.38), (0.68, 0.49, 0.32), width=0.09, axis='Z', rough=0.6)
    o = badge(boards, floor, K.pm('em_rim', (1.0, 0.86, 0.52), rough=0.35, cc=0.6))
    glass = K.pm('em_rwin', (0.62, 0.85, 0.98), emit=(0.62, 0.85, 0.98), es=0.9)
    frm = K.pm('em_rwf', (0.97, 0.93, 0.84), rough=0.5)
    o += [K.cone('rw', (-0.45, -0.1, 0.42), 0.2, 0.2, 0.02, glass, rot=(90, 0, 0), verts=32),
          K.torus('rwf', (-0.45, -0.13, 0.42), 0.21, 0.035, frm, rot=(90, 0, 0)),
          K.box('rwx', -0.65, -0.14, 0.405, -0.25, -0.12, 0.435, frm),
          K.box('rwy', -0.465, -0.14, 0.22, -0.435, -0.12, 0.62, frm)]
    card = K.pm('em_card', (0.82, 0.62, 0.38), rough=0.85)
    tape = K.pm('em_tape', (0.92, 0.25, 0.25), rough=0.3, cc=0.4)
    lab = K.pm('em_label', (0.99, 0.97, 0.92), rough=0.6)
    parts = [K.box('box', -0.36, -0.3, 0, 0.36, 0.3, 0.58, card, bevel=0.02),
             K.box('t1', -0.08, -0.31, 0.577, 0.08, 0.31, 0.59, tape),
             K.box('t2', -0.08, -0.31, 0.36, 0.08, -0.295, 0.59, tape),
             K.box('lb', 0.12, -0.31, 0.08, 0.32, -0.295, 0.24, lab)]
    root = K.parent_all(parts, 'boxr', loc=(0.05, -0.55, -0.66), rotz=-18)
    root.rotation_euler = (math.radians(12), 0, math.radians(-18))
    o += parts
    em_frame('ui_emblem_attic', o, samples, out)


def emblem_roofs(out, samples):
    start()
    sky = K.pm('em_night', (0.06, 0.08, 0.22), emit=(0.08, 0.10, 0.28), es=0.6, rough=0.8)
    tiles = K.tiles('em_rtile', (0.90, 0.46, 0.30), (0.40, 0.18, 0.12), w=0.26, h=0.12, offset=0.5, plane='XZ',
                    col2=(0.80, 0.40, 0.26), rough=0.5)
    o = badge(sky, tiles, K.pm('em_rim', (1.0, 0.86, 0.52), rough=0.35, cc=0.6))
    moon = K.pm('em_moon', (1.0, 0.96, 0.78), emit=(1.0, 0.94, 0.72), es=2.2)
    star = K.pm('em_star', (1, 1, 0.9), emit=(1, 1, 0.9), es=4)
    o.append(K.cone('moon', (0.36, -0.1, 0.36), 0.3, 0.3, 0.02, moon, rot=(90, 0, 0), verts=40))
    for k, (x, z) in enumerate(((-0.55, 0.45), (-0.25, 0.7), (-0.1, 0.35), (-0.6, 0.1), (0.7, 0.05))):
        o.append(K.ellipsoid('st%d' % k, (x, -0.11, z), (0.035, 0.01, 0.035), star))
    wood = K.stripes('em_crate', (0.80, 0.58, 0.36), (0.70, 0.50, 0.30), width=0.07, axis='Z', rough=0.7)
    frm = K.pm('em_cframe', (0.52, 0.34, 0.20), rough=0.6)
    paint = K.pm('em_stencil', (0.99, 0.95, 0.82), rough=0.6)
    s = 0.62
    parts = [K.box('cr', -s / 2, -s / 2, 0, s / 2, s / 2, s, wood, bevel=0.01)]
    for sx in (-1, 1):
        parts.append(K.box('cv%d' % sx, sx * s / 2 - 0.035 - 0.036 * sx, -s / 2 - 0.02, 0, sx * s / 2 + 0.035 - 0.036 * sx,
                           -s / 2 + 0.05, s, frm))
    for z in (0.0, s - 0.07):
        parts.append(K.box('ch%.1f' % z, -s / 2, -s / 2 - 0.02, z, s / 2, -s / 2 + 0.05, z + 0.07, frm))
    parts += K.paw_upright('cp', (0, -s / 2 - 0.025, s / 2 - 0.02), 1.5, paint, depth=0.008)
    root = K.parent_all(parts, 'crr', loc=(-0.08, -0.5, -0.66))
    root.rotation_euler = (math.radians(10), 0, math.radians(-14))
    o += parts
    em_frame('ui_emblem_roofs', o, samples, out)


def emblem_soon(out, samples):
    start()
    sky = K.pm('em_lilac', (0.60, 0.54, 0.86), rough=0.8)
    cloudk = K.pm('em_cloudf', (0.97, 0.96, 1.0), rough=0.9, sheen=0.5)
    o = badge(sky, cloudk, K.pm('em_rim', (1.0, 0.86, 0.52), rough=0.35, cc=0.6), split=-0.62)
    for k, (x, z, r) in enumerate(((-0.62, -0.5, 0.3), (-0.3, -0.42, 0.34), (0.05, -0.48, 0.32), (0.4, -0.42, 0.34),
                                   (0.68, -0.52, 0.26))):
        o.append(K.ellipsoid('cl%d' % k, (x, -0.2, z), (r, 0.12, r * 0.8), cloudk))
    q = K.pm('em_q', (0.96, 0.38, 0.62), rough=0.3, cc=0.7)
    pts = [(-0.2, -0.35, 0.34), (-0.12, -0.35, 0.5), (0.1, -0.35, 0.52), (0.22, -0.35, 0.36), (0.1, -0.35, 0.2),
           (0.0, -0.35, 0.08), (0.0, -0.35, -0.02)]
    o.append(K.tube('q', pts, [0.1] * len(pts), q))
    o.append(K.ellipsoid('qd', (0.0, -0.35, -0.26), (0.1, 0.1, 0.1), q))
    spark = K.pm('em_spark', (1.0, 0.9, 0.5), emit=(1.0, 0.85, 0.4), es=3.0)
    for k, (x, z, r) in enumerate(((-0.5, 0.35, 0.05), (0.52, 0.5, 0.06), (0.62, 0.12, 0.04))):
        o.append(K.ellipsoid('sp%d' % k, (x, -0.12, z), (r, 0.01, r), spark))
    em_frame('ui_emblem_soon', o, samples, out)


def icon_shop(out, samples):
    start()
    bag = K.pm('bag', (0.96, 0.46, 0.62), rough=0.45, cc=0.4)
    fold = K.pm('bagfold', (0.99, 0.66, 0.76), rough=0.45, cc=0.4)
    handle = K.pm('baghandle', (0.25, 0.70, 0.72), rough=0.35, cc=0.5)
    pawk = K.pm('bagpaw', (1.0, 0.95, 0.88), rough=0.5)
    o = [K.box('bag', -0.5, -0.28, -0.55, 0.5, 0.28, 0.42, bag, bevel=0.1, segments=4),
         K.box('fold', -0.51, -0.29, 0.28, 0.51, 0.29, 0.46, fold, bevel=0.06, segments=3)]
    for sx in (-1, 1):
        o.append(K.torus('h%d' % sx, (0, sx * 0.12, 0.44), 0.26, 0.055, handle, rot=(90, 0, 0), arc=0.5, n=24, m=10))
    o += K.paw_upright('bp', (0, -0.3, -0.1), 3.0, pawk, depth=0.035)
    K.parent_all(o, 'shop', rotz=-20)
    frame(o, 40, 40, elev=12, margin=2)
    render(out, 'icon_shop', 40, 40, samples)


def main():
    a = C.args({'samples': 96})
    out = os.path.abspath(a.out)
    jobs = [
        ('ui_logo', lambda: logo(out, a.samples)),
        ('ui_emblem_living', lambda: emblem_living(out, a.samples)),
        ('icon_coin', lambda: icon_coin(out, a.samples)),
        ('icon_star', lambda: icon_star(out, a.samples)),
        ('icon_star_empty', lambda: icon_star(out, a.samples, empty=True)),
        ('icon_undo', lambda: icon_undo(out, a.samples)),
        ('icon_shop', lambda: icon_shop(out, a.samples)),
        ('ui_emblem_kitchen', lambda: emblem_kitchen(out, a.samples)),
        ('ui_emblem_garden', lambda: emblem_garden(out, a.samples)),
        ('ui_emblem_attic', lambda: emblem_attic(out, a.samples)),
        ('ui_emblem_roofs', lambda: emblem_roofs(out, a.samples)),
        ('ui_emblem_soon', lambda: emblem_soon(out, a.samples)),
        ('icon_paw', lambda: icon_paw(out, a.samples)),
        ('icon_restart', lambda: icon_restart(out, a.samples)),
        ('icon_home', lambda: icon_home(out, a.samples)),
        ('icon_gear', lambda: icon_gear(out, a.samples)),
        ('icon_gift', lambda: icon_gift(out, a.samples)),
        ('icon_lock', lambda: icon_lock(out, a.samples)),
        ('icon_heart', lambda: icon_heart(out, a.samples)),
        ('icon_play', lambda: icon_play(out, a.samples)),
        ('icon_tab_hat', lambda: icon_tab_hat(out, a.samples)),
        ('icon_tab_neck', lambda: icon_tab_neck(out, a.samples)),
        ('icon_tab_toy', lambda: icon_tab_toy(out, a.samples)),
        ('icon_link', lambda: icon_link(out, a.samples)),
    ]
    t0 = time.time()
    for name, fn in jobs:
        if a.only and name not in a.only:
            continue
        fn()
    C._STATE['meta'].clear()
    C._STATE['meta'].update(META)
    C.save_meta(out)
    print('ui: %d pictures in %.1fs' % (len(META), time.time() - t0))


main()
