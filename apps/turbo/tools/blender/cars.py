"""
cars.py - the vehicles of Turbo, built procedurally and rendered as lighting +
region-id + shadow passes (see SPEC.md in this folder).

    /Applications/Blender.app/Contents/MacOS/Blender -b -P cars.py -- --out ../../assets/cars
        [--cars wedge,muscle,...] [--near 0,1,2,3,4,5,6 | --nonear] [--far l,c,r | --nofar]
        [--samples 64] [--noshadow] [--studio <dir>] [--cpu]

Everything is built from code: bodies are lofts of cross-sections along the
car (Y), cabins are a second loft on top, wheel arches are boolean cuts, the
details (lamps, louvres, bumpers, strakes...) are small bevelled parts.
Region ids that do not follow the mesh (stripes, bands, windows on the cabin
loft) come from masks evaluated in the material on object coordinates, so the
id pass has crisp edges whatever the mesh density.

Car-local coordinates: X right, Y forward, Z up; the rear bumper's rearmost
point at Y = 0, wheels on Z = 0, centred on X = 0.
"""
import argparse
import json
import math
import os
import struct
import sys
import time
import zlib

import bpy
import bmesh
import numpy as np
from mathutils import Matrix, Vector
from bpy_extras.object_utils import world_to_camera_view

HERE = os.path.dirname(os.path.abspath(__file__))

NEAR_W, NEAR_H = 368, 448
NEAR_F = 300.0
NEAR_PP = (184.0, 150.0)
NEAR_REAR_Y = 3.2
YAWS = [-24, -16, -8, 0, 8, 16, 24]
FAR_F = 1200.0
FAR_Y = 12.0
FAR_VIEWS = {'l': 3.5, 'c': 0.0, 'r': -3.5}
CAM_Z = 2.0
SUN_EL, SUN_AZ = 55.0, 225.0      # from (-X, -Y), 55 degrees up
SUN_ENERGY = 1.30
SKY = (0.78, 0.86, 1.0)
SKY_STRENGTH = 0.24

REGION = {'paintA': 1, 'paintB': 2, 'glass': 3, 'chrome': 4, 'black': 5, 'tyre': 6, 'rim': 7,
          'tail': 8, 'head': 9, 'plate': 10, 'interior': 11, 'under': 12, 'amber': 13, 'extra': 14}

# neutral grey materials: (base, roughness, specular, metallic, clearcoat)
SHADE = {
    'paintA': (0.80, 0.30, 0.50, 0.0, 1.0),
    'paintB': (0.80, 0.30, 0.50, 0.0, 1.0),
    'glass': (0.30, 0.04, 1.00, 0.0, 0.0),
    'chrome': (0.85, 0.14, 0.50, 1.0, 0.0),
    'black': (0.80, 0.55, 0.30, 0.0, 0.0),
    'tyre': (0.80, 0.80, 0.15, 0.0, 0.0),
    'rim': (0.85, 0.25, 0.50, 0.7, 0.0),
    'tail': (0.80, 0.12, 0.70, 0.0, 0.0),
    'head': (0.85, 0.08, 0.90, 0.0, 0.0),
    'plate': (0.80, 0.50, 0.30, 0.0, 0.0),
    'interior': (0.80, 0.80, 0.20, 0.0, 0.0),
    'under': (0.80, 0.85, 0.10, 0.0, 0.0),
    'amber': (0.80, 0.12, 0.70, 0.0, 0.0),
    'extra': (0.80, 0.35, 0.50, 0.0, 0.3),
}

MATS = {'shade': {}, 'id': {}}


def V(*a):
    return Vector(a[0] if len(a) == 1 else a)


def clamp(x, a, b):
    return a if x < a else b if x > b else x


# ---------------------------------------------------------------------------
# Masks: region = AND of half-spaces on (|x|, y, z) and (|nx|, ny, nz), object
# space; mask = OR of regions.  cond = (cp, cn, c): P.cp + N.cn > c
# ---------------------------------------------------------------------------

def C(ax=0.0, y=0.0, z=0.0, anx=0.0, ny=0.0, nz=0.0, gt=0.0):
    return ((ax, y, z), (anx, ny, nz), gt)


def gt(var, v):
    return C(**{var: 1.0, 'gt': v})


def lt(var, v):
    return C(**{var: -1.0, 'gt': -v})


def poly(axes, pts):
    """convex polygon in a plane of two of (ax, y, z): list of conds"""
    a0, a1 = axes
    area = 0.0
    for i in range(len(pts)):
        p, q = pts[i], pts[(i + 1) % len(pts)]
        area += p[0] * q[1] - q[0] * p[1]
    if area < 0:
        pts = pts[::-1]
    out = []
    for i in range(len(pts)):
        p, q = pts[i], pts[(i + 1) % len(pts)]
        du, dv = q[0] - p[0], q[1] - p[1]
        # left of p->q: du*(v - pv) - dv*(u - pu) > 0
        k = {a0: -dv, a1: du}
        out.append(C(**k, gt=-dv * p[0] + du * p[1]))
    return out


# ---------------------------------------------------------------------------
# Materials
# ---------------------------------------------------------------------------

def _clear(nt):
    for n in list(nt.nodes):
        nt.nodes.remove(n)
    return nt.nodes.new('ShaderNodeOutputMaterial')


def _math(nt, op, a, b=None):
    n = nt.nodes.new('ShaderNodeMath')
    n.operation = op
    for i, x in enumerate((a, b)):
        if x is None:
            continue
        if isinstance(x, (int, float)):
            n.inputs[i].default_value = x
        else:
            nt.links.new(x, n.inputs[i])
    return n.outputs[0]


def _bsdf(nt, key):
    base, rough, spec, metal, coat = SHADE[key]
    b = nt.nodes.new('ShaderNodeBsdfPrincipled')
    b.inputs['Base Color'].default_value = (base, base, base, 1)
    b.inputs['Roughness'].default_value = rough
    b.inputs['Specular'].default_value = spec
    b.inputs['Metallic'].default_value = metal
    b.inputs['Clearcoat'].default_value = coat
    b.inputs['Clearcoat Roughness'].default_value = 0.03
    return b.outputs[0]


def _coords(nt):
    tc = nt.nodes.new('ShaderNodeTexCoord')
    sp = nt.nodes.new('ShaderNodeSeparateXYZ')
    nt.links.new(tc.outputs['Object'], sp.inputs[0])
    P = nt.nodes.new('ShaderNodeCombineXYZ')
    nt.links.new(_math(nt, 'ABSOLUTE', sp.outputs[0]), P.inputs[0])
    nt.links.new(sp.outputs[1], P.inputs[1])
    nt.links.new(sp.outputs[2], P.inputs[2])
    sn = nt.nodes.new('ShaderNodeSeparateXYZ')
    nt.links.new(tc.outputs['Normal'], sn.inputs[0])
    N = nt.nodes.new('ShaderNodeCombineXYZ')
    nt.links.new(_math(nt, 'ABSOLUTE', sn.outputs[0]), N.inputs[0])
    nt.links.new(sn.outputs[1], N.inputs[1])
    nt.links.new(sn.outputs[2], N.inputs[2])
    return P.outputs[0], N.outputs[0]


def _dot(nt, vec, coef):
    n = nt.nodes.new('ShaderNodeVectorMath')
    n.operation = 'DOT_PRODUCT'
    nt.links.new(vec, n.inputs[0])
    n.inputs[1].default_value = coef
    return n.outputs['Value']


def _mask(nt, regions, P, N):
    out = None
    for reg in regions:
        rv = None
        for cp, cn, c in reg:
            s = None
            if any(cp):
                s = _dot(nt, P, cp)
            if any(cn):
                d = _dot(nt, N, cn)
                s = d if s is None else _math(nt, 'ADD', s, d)
            v = _math(nt, 'GREATER_THAN', s, c)
            rv = v if rv is None else _math(nt, 'MULTIPLY', rv, v)
        out = rv if out is None else _math(nt, 'MAXIMUM', out, rv)
    return out


def make_material(name, entries, default):
    """entries: [(key, regions)] highest priority first; default: key."""
    for kind in ('shade', 'id'):
        m = bpy.data.materials.new(('S_' if kind == 'shade' else 'I_') + name)
        m.use_nodes = True
        nt = m.node_tree
        out = _clear(nt)
        P = N = None
        if entries:
            P, N = _coords(nt)
        if kind == 'shade':
            cur = _bsdf(nt, default)
            for key, regs in reversed(entries):
                mx = nt.nodes.new('ShaderNodeMixShader')
                nt.links.new(_mask(nt, regs, P, N), mx.inputs[0])
                nt.links.new(cur, mx.inputs[1])
                nt.links.new(_bsdf(nt, key), mx.inputs[2])
                cur = mx.outputs[0]
            nt.links.new(cur, out.inputs['Surface'])
        else:
            em = nt.nodes.new('ShaderNodeEmission')
            em.inputs['Strength'].default_value = 1.0
            val = REGION[default] / 16.0
            if not entries:
                em.inputs['Color'].default_value = (val, val, val, 1)
            else:
                cur = None
                for key, regs in reversed(entries):
                    v2 = REGION[key] / 16.0
                    mk = _mask(nt, regs, P, N)
                    # cur + mask * (v2 - cur)
                    base = val if cur is None else cur
                    diff = _math(nt, 'SUBTRACT', v2, base)
                    cur = _math(nt, 'ADD', base, _math(nt, 'MULTIPLY', mk, diff))
                comb = nt.nodes.new('ShaderNodeCombineXYZ')
                for i in range(3):
                    nt.links.new(cur, comb.inputs[i])
                nt.links.new(comb.outputs[0], em.inputs['Color'])
            nt.links.new(em.outputs[0], out.inputs['Surface'])
        MATS[kind][name] = m


def make_base_materials():
    for k in REGION:
        make_material(k, [], k)
    g = bpy.data.materials.new('ground')
    g.use_nodes = True
    bs = g.node_tree.nodes['Principled BSDF']
    bs.inputs['Base Color'].default_value = (0.20, 0.20, 0.20, 1)
    bs.inputs['Roughness'].default_value = 0.9
    MATS['ground'] = g
    ca = bpy.data.materials.new('calib')
    ca.use_nodes = True
    bs = ca.node_tree.nodes['Principled BSDF']
    bs.inputs['Base Color'].default_value = (0.8, 0.8, 0.8, 1)
    bs.inputs['Roughness'].default_value = 1.0
    bs.inputs['Specular'].default_value = 0.0
    MATS['calib'] = ca


# ---------------------------------------------------------------------------
# Mesh building
# ---------------------------------------------------------------------------

class MB:
    def __init__(self):
        self.v, self.f, self.fm = [], [], []

    def vert(self, p):
        self.v.append(Vector(p))
        return len(self.v) - 1

    def face(self, idx, mat):
        self.f.append(tuple(idx))
        self.fm.append(mat)

    def box(self, c, s, mat, rot=None):
        """box centred at c with full sizes s; rot: 3x3 Matrix about c"""
        c = Vector(c)
        hx, hy, hz = s[0] / 2, s[1] / 2, s[2] / 2
        base = len(self.v)
        for z in (-hz, hz):
            for y in (-hy, hy):
                for x in (-hx, hx):
                    p = Vector((x, y, z))
                    if rot is not None:
                        p = rot @ p
                    self.v.append(c + p)
        for q in ((0, 2, 3, 1), (4, 5, 7, 6), (0, 1, 5, 4), (2, 6, 7, 3), (0, 4, 6, 2), (1, 3, 7, 5)):
            self.face([base + i for i in q], mat)

    def boxr(self, lo, hi, mat):
        lo, hi = Vector(lo), Vector(hi)
        self.box((lo + hi) / 2, hi - lo, mat)

    def cyl(self, c, axis, r, length, mat, n=24, cap=True, capmat=None, r2=None):
        c, axis = Vector(c), Vector(axis).normalized()
        u = axis.orthogonal().normalized()
        w = axis.cross(u)
        r2 = r if r2 is None else r2
        a, b = [], []
        for i in range(n):
            t = 2 * math.pi * i / n
            d = u * math.cos(t) + w * math.sin(t)
            a.append(self.vert(c - axis * length / 2 + d * r))
            b.append(self.vert(c + axis * length / 2 + d * r2))
        for i in range(n):
            j = (i + 1) % n
            self.face((a[i], a[j], b[j], b[i]), mat)
        if cap:
            self.face(a[::-1], capmat or mat)
            self.face(b, capmat or mat)

    def lathe(self, prof, c, side, mat, n=48, closed=False, rfun=None):
        """prof: [(r, x)] around the X axis through c; side = +1/-1 mirrors x.
        mat: key or function(i_profile_segment) -> key"""
        c = Vector(c)
        rings = []
        for i in range(n):
            t = 2 * math.pi * i / n
            ring = []
            for k, (r, x) in enumerate(prof):
                rr = rfun(k, t, r) if rfun else r
                ring.append(self.vert(c + Vector((side * x, rr * math.cos(t), rr * math.sin(t)))))
            rings.append(ring)
        m = len(prof)
        segs = m if closed else m - 1
        for i in range(n):
            j = (i + 1) % n
            for k in range(segs):
                k2 = (k + 1) % m
                key = mat(k) if callable(mat) else mat
                self.face((rings[i][k], rings[j][k], rings[j][k2], rings[i][k2]), key)


def mb_object(name, mb, coll, bevel=0.0, segs=2, smooth_angle=35.0):
    me = bpy.data.meshes.new(name)
    me.from_pydata([tuple(v) for v in mb.v], [], mb.f)
    me.update()
    keys = sorted(set(mb.fm))
    for k in keys:
        me.materials.append(MATS['shade'][k])
    me.polygons.foreach_set('material_index', [keys.index(k) for k in mb.fm])
    me.polygons.foreach_set('use_smooth', [True] * len(mb.f))
    me.use_auto_smooth = True
    me.auto_smooth_angle = math.radians(smooth_angle)
    bm = bmesh.new()
    bm.from_mesh(me)
    bmesh.ops.remove_doubles(bm, verts=bm.verts, dist=1e-6)
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    bm.to_mesh(me)
    bm.free()
    ob = bpy.data.objects.new(name, me)
    coll.objects.link(ob)
    ob['matkeys'] = keys
    if bevel > 0:
        m = ob.modifiers.new('bevel', 'BEVEL')
        m.width = bevel
        m.segments = segs
        m.limit_method = 'ANGLE'
        m.angle_limit = math.radians(40)
    return ob


def refresh_matkeys(ob):
    ob['matkeys'] = [m.name[2:] for m in ob.data.materials]


def set_pass_materials(objs, kind):
    for ob in objs:
        for i, k in enumerate(list(ob.get('matkeys', []))):
            ob.data.materials[i] = MATS[kind][k]


def apply_modifiers(ob):
    bpy.context.view_layer.update()
    dg = bpy.context.evaluated_depsgraph_get()
    me = bpy.data.meshes.new_from_object(ob.evaluated_get(dg))
    old = ob.data
    ob.modifiers.clear()
    ob.data = me
    me.use_auto_smooth = True
    me.auto_smooth_angle = old.auto_smooth_angle
    bpy.data.meshes.remove(old)
    refresh_matkeys(ob)


# ---------------------------------------------------------------------------
# Lofted bodies
# ---------------------------------------------------------------------------

def smooth_table(keys, names, y0, y1, sigma, step=0.01):
    """keys: [(y, {param: value})]; params missing in a key are carried over.
    Returns (ys, {name: array}) linearly interpolated then gaussian-smoothed."""
    ys = np.arange(y0, y1 + step * 0.5, step)
    tab = {}
    for nm in names:
        ky, kv = [], []
        last = None
        for y, d in keys:
            if nm in d:
                last = d[nm]
            if last is not None:
                ky.append(y)
                kv.append(last)
        arr = np.interp(ys, ky, kv)
        s = sigma.get(nm, sigma.get('*', 0.0)) if isinstance(sigma, dict) else sigma
        if s > 0:
            k = int(3 * s / step) + 1
            g = np.exp(-0.5 * (np.arange(-k, k + 1) * step / s) ** 2)
            g /= g.sum()
            pad = np.concatenate([np.full(k, arr[0]), arr, np.full(k, arr[-1])])
            arr = np.convolve(pad, g, 'valid')
        tab[nm] = arr
    return ys, tab


def sample(tab, ys, y):
    return {k: float(np.interp(y, ys, v)) for k, v in tab.items()}


def qbez(p0, p1, p2, t):
    return ((1 - t) ** 2 * p0[0] + 2 * (1 - t) * t * p1[0] + t * t * p2[0],
            (1 - t) ** 2 * p0[1] + 2 * (1 - t) * t * p1[1] + t * t * p2[1])


NB, NF1, NS, NF2, NT = 4, 4, 10, 7, 10


def half_profile(p):
    """cross-section from bottom centre to top centre as [(x, z)]:
    flat bottom at zb up to wb, fillet rb, side = quadratic bezier from (wb, zb)
    through control (wc, zc) to the shoulder (ws, zs), fillet rt, then a crowned
    top z = zt - (zt - zs) (x / ws)^2 to the centre."""
    zb, zs, zt = p['zb'], p['zs'], p['zt']
    wb, wc, ws = p['wb'], p['wc'], p['ws']
    zc = p.get('zc', None)
    if zc is None or zc <= zb or zc >= zs:
        zc = zb + (zs - zb) * p.get('zcf', 0.45)
    h = max(zs - zb, 1e-3)
    rb = min(p.get('rb', 0.05), 0.4 * h, 0.4 * wb)
    rt = min(p.get('rt', 0.08), 0.45 * h, 0.45 * ws)
    P0, P1, P2 = (wb, zb), (wc, zc), (ws, zs)
    L = math.hypot(wc - wb, zc - zb) + math.hypot(ws - wc, zs - zc)
    tb, tt = clamp(rb / L, 0.0, 0.3), clamp(rt / L, 0.0, 0.4)

    def top(x):
        return zt - (zt - zs) * (x / ws) ** 2 if ws > 1e-4 else zt

    pts = []
    for i in range(NB):                       # bottom
        pts.append(((wb - rb) * i / NB, zb))
    a, b = (wb - rb, zb), qbez(P0, P1, P2, tb)
    for i in range(NF1):                      # bottom fillet
        pts.append(qbez(a, P0, b, i / NF1))
    for i in range(NS):                       # side
        t = tb + (1 - tt - tb) * i / NS
        pts.append(qbez(P0, P1, P2, t))
    a = qbez(P0, P1, P2, 1 - tt)
    xb = max(ws - rt, 0.0)
    b = (xb, top(xb))
    for i in range(NF2):                      # shoulder fillet
        pts.append(qbez(a, P2, b, i / NF2))
    for i in range(NT + 1):                   # crowned top
        x = xb * (1 - i / NT)
        pts.append((x, top(x)))
    return pts


def ring_from_half(half):
    full = half + [(-x, z) for x, z in reversed(half[1:-1])]
    return full


def offset_ring(ring, d):
    """move points of a closed 2D ring inward by d (ring is CCW or CW)"""
    n = len(ring)
    area = sum(ring[i][0] * ring[(i + 1) % n][1] - ring[(i + 1) % n][0] * ring[i][1] for i in range(n))
    sgn = 1.0 if area > 0 else -1.0
    out = []
    for i in range(n):
        a, b = ring[i - 1], ring[(i + 1) % n]
        tx, tz = b[0] - a[0], b[1] - a[1]
        l = math.hypot(tx, tz) or 1.0
        nx, nz = -tz / l * sgn, tx / l * sgn     # inward for CCW
        out.append((ring[i][0] + nx * d, ring[i][1] + nz * d))
    return out


def loft(mb, prof_at, y0, y1, step, r0, r1, mat, cap_mat=None):
    """prof_at(y) -> closed ring [(x, z)]; rounded ends of radius r0 (at y0) and
    r1 (at y1); mat(face_centre, normal_hint) -> key"""
    ys = []
    for i in range(7):                                    # rounded start
        a = math.pi / 2 * i / 6
        ys.append((y0 + r0 - r0 * math.cos(a), r0 - r0 * math.sin(a)))
    n = max(2, int(math.ceil((y1 - y0 - r0 - r1) / step)))
    for i in range(1, n):
        ys.append((y0 + r0 + (y1 - y0 - r0 - r1) * i / n, 0.0))
    for i in range(6, -1, -1):                            # rounded end
        a = math.pi / 2 * i / 6
        ys.append((y1 - r1 + r1 * math.cos(a), r1 - r1 * math.sin(a)))
    rings = []
    for y, d in ys:
        r = prof_at(y)
        if d > 1e-5:
            r = offset_ring(r, d)
        rings.append([mb.vert((x, y, z)) for x, z in r])
    m = len(rings[0])
    for j in range(len(rings) - 1):
        for i in range(m):
            i2 = (i + 1) % m
            q = (rings[j][i], rings[j][i2], rings[j + 1][i2], rings[j + 1][i])
            cen = sum((mb.v[k] for k in q), Vector()) / 4
            nrm = (mb.v[q[1]] - mb.v[q[0]]).cross(mb.v[q[3]] - mb.v[q[0]])
            rc = sum((mb.v[k] for k in rings[j]), Vector()) / m
            out = cen - rc
            out.y = 0.0
            if nrm.dot(out) < 0:            # make the hint point outwards
                nrm = -nrm
            mb.face(q, mat(cen, nrm))
    for ring, rev in ((rings[0], True), (rings[-1], False)):
        cz = sum(mb.v[k].z for k in ring) / m
        cy = mb.v[ring[0]].y
        c = mb.vert((0.0, cy, cz))
        for i in range(m):
            i2 = (i + 1) % m
            tri = (ring[i], ring[i2], c) if not rev else (ring[i2], ring[i], c)
            mb.face(tri, cap_mat or mat(mb.v[c], Vector((0, 1 if not rev else -1, 0))))
    return rings


def rrect_ring(x0, x1, z0, z1, rb, rt, n=5):
    """closed ring of a rounded rectangle in (x, z), bottom corners rb, top rt"""
    pts = []
    rb = min(rb, 0.45 * (x1 - x0), 0.45 * (z1 - z0))
    rt = min(rt, 0.45 * (x1 - x0), 0.45 * (z1 - z0))
    for cx, cz, r, a0 in ((x1 - rb, z0 + rb, rb, -90), (x1 - rt, z1 - rt, rt, 0),
                          (x0 + rt, z1 - rt, rt, 90), (x0 + rb, z0 + rb, rb, 180)):
        for i in range(n + 1):
            a = math.radians(a0 + 90 * i / n)
            pts.append((cx + r * math.cos(a), cz + r * math.sin(a)))
    return pts


class Body:
    """lower body + cabin tables of one car"""

    def __init__(self, body_keys, cabin_keys, length, sig=0.08, csig=0.04):
        self.L = length
        self.bys, self.bt = smooth_table(body_keys, ['zb', 'zs', 'zt', 'wb', 'wc', 'ws', 'rt', 'rb', 'zcf'],
                                         0.0, length, sig)
        self.cabin = None
        if cabin_keys:
            c0, c1 = cabin_keys[0][0], cabin_keys[-1][0]
            self.cys, self.ct = smooth_table(cabin_keys, ['zr', 'wbase', 'wroof', 'rr', 'dz'], c0, c1, csig)
            self.cabin = (c0, c1)

    def prof(self, y):
        return sample(self.bt, self.bys, y)

    def ring(self, y):
        return ring_from_half(half_profile(self.prof(y)))

    def side_x(self, y, z):
        """outer x of the lower body at (y, z)"""
        h = half_profile(self.prof(y))
        best = None
        for (x0, z0), (x1, z1) in zip(h[:-1], h[1:]):
            if (z0 - z) * (z1 - z) <= 0 and abs(z1 - z0) > 1e-6:
                x = x0 + (x1 - x0) * (z - z0) / (z1 - z0)
                best = x if best is None else max(best, x)
        return best if best is not None else max(x for x, _ in h)

    def top_z(self, y, x=0.0):
        p = self.prof(y)
        ws = p['ws']
        return p['zt'] - (p['zt'] - p['zs']) * (min(abs(x), ws) / ws) ** 2

    def cabin_prof(self, y):
        c = sample(self.ct, self.cys, y)
        zbase = self.top_z(y) - c.get('dz', 0.04)
        zr = max(c['zr'], zbase + 0.02)
        h = zr - zbase
        p = {'zb': zbase - 0.06, 'zs': zr - min(0.04, 0.3 * h), 'zt': zr, 'wb': c['wbase'],
             'wc': (c['wbase'] * 0.55 + c['wroof'] * 0.45) + 0.01, 'ws': c['wroof'],
             'zcf': 0.5, 'rb': 0.01, 'rt': min(c.get('rr', 0.07), 0.45 * (h + 0.06))}
        return ring_from_half(half_profile(p))


# ---------------------------------------------------------------------------
# Wheels
# ---------------------------------------------------------------------------

def wheel(mb, c, side, R, W, style, rim_frac=0.64, tread=None):
    """tyre + rim at centre c (the wheel's centre), side = +1 right, -1 left;
    x axis of the profile points outwards"""
    Rr = R * rim_frac
    hw = W / 2
    cr = min(0.05, 0.25 * W)
    # tyre cross-section, closed, (r, x): from inner bead round to the outer bead
    prof = []
    prof.append((Rr + 0.005, -hw + 0.02))
    prof.append((Rr + (R - Rr) * 0.5, -hw - 0.004))              # inner sidewall bulge
    for i in range(5):                                             # inner shoulder
        a = math.pi / 2 * i / 4
        prof.append((R - cr + cr * math.sin(a), -hw + cr - cr * math.cos(a)))
    for i in range(1, 6):
        prof.append((R, -hw + cr + (W - 2 * cr) * i / 6))
    for i in range(5):                                             # outer shoulder
        a = math.pi / 2 * i / 4
        prof.append((R - cr + cr * math.cos(a), hw - cr + cr * math.sin(a)))
    prof.append((Rr + (R - Rr) * 0.5, hw + 0.004))
    prof.append((Rr + 0.005, hw - 0.02))
    tread_idx = set(range(4, 4 + 5 + 5 + 3))

    def rfun(k, t, r):
        if tread and k in tread_idx:
            n, depth = tread
            s = math.sin(n * t + (0.9 if k % 2 else 0.0))
            return r - (depth if s > 0.3 else 0.0)
        return r
    mb.lathe(prof, c, side, 'tyre', n=64 if tread else 48, rfun=rfun if tread else None)
    # rim: face slightly recessed from the tyre's outer sidewall
    xo = hw - 0.025
    lip = [(Rr + 0.012, -hw + 0.03), (Rr + 0.012, xo), (Rr - 0.004, xo + 0.006), (Rr - 0.02, xo)]
    mb.lathe(lip, c, side, 'rim', n=48)
    dish_key = 'black' if style in ('star', 'mesh') else 'rim'
    depth = {'star': 0.05, 'slot': 0.03, 'steel': 0.02, 'mesh': 0.04, 'turbine': 0.04}[style]
    dish = [(Rr - 0.02, xo), (Rr - 0.03, xo - depth), (0.07, xo - depth), (0.0, xo - depth)]
    mb.lathe(dish, c, side, dish_key, n=48)
    c = Vector(c)
    face_x = xo - depth * 0.5
    if style == 'star':          # five-spoke star, pointed to the lip
        for k in range(5):
            t = 2 * math.pi * k / 5 + 0.3
            d = Vector((0, math.cos(t), math.sin(t)))
            rot = Matrix.Rotation(t, 3, 'X')
            mid = (0.06 + Rr - 0.02) / 2
            mb.box(c + Vector((side * face_x, 0, 0)) + d * mid, (depth * 0.9, Rr - 0.09, 0.055), 'rim',
                   rot=rot @ Matrix.Rotation(math.pi / 2, 3, 'X'))
    elif style == 'slot':        # slot mag: dark slots in a bright face
        for k in range(5):
            t = 2 * math.pi * k / 5
            rot = Matrix.Rotation(t + math.pi / 5, 3, 'X')
            d = Vector((0, math.cos(t + math.pi / 5), math.sin(t + math.pi / 5)))
            mb.box(c + Vector((side * face_x, 0, 0)) + d * (Rr * 0.55), (depth * 0.9, 0.05, Rr * 0.9), 'rim',
                   rot=rot)
    elif style == 'mesh':        # cross-spoke mesh (BBS-ish): many thin spokes
        for k in range(10):
            t = 2 * math.pi * k / 10
            rot = Matrix.Rotation(t, 3, 'X') @ Matrix.Rotation(math.pi / 2, 3, 'X')
            d = Vector((0, math.cos(t), math.sin(t)))
            mid = (0.06 + Rr - 0.02) / 2
            mb.box(c + Vector((side * face_x, 0, 0)) + d * mid, (depth * 0.9, Rr - 0.09, 0.022), 'rim', rot=rot)
    elif style == 'steel':       # white/steel spoke wheel with round holes
        for k in range(6):
            t = 2 * math.pi * k / 6
            d = Vector((0, math.cos(t), math.sin(t)))
            mb.cyl(c + Vector((side * (xo - depth + 0.004), 0, 0)) + d * Rr * 0.62, (1, 0, 0), Rr * 0.16,
                   0.004, 'black', n=12)
    # hub cap
    mb.cyl(c + Vector((side * (xo - depth + 0.012), 0, 0)), (1, 0, 0), 0.065, 0.028, 'rim', n=16)
    mb.cyl(c + Vector((side * (xo - depth + 0.028), 0, 0)), (1, 0, 0), 0.03, 0.01, 'chrome', n=12)


# ---------------------------------------------------------------------------
# Car definitions
# ---------------------------------------------------------------------------

class Car:
    def __init__(self, key):
        self.key = key
        self.objs = []
        self.coll = bpy.data.collections.new(key)
        bpy.context.scene.collection.children.link(self.coll)
        self.root = bpy.data.objects.new(key + '_root', None)
        self.coll.objects.link(self.root)

    def add(self, name, mb, **kw):
        ob = mb_object(self.key + '_' + name, mb, self.coll, **kw)
        ob.parent = self.root
        self.objs.append(ob)
        return ob

    def finish(self):
        for ob in self.objs:
            if ob.modifiers:
                apply_modifiers(ob)
        bpy.context.view_layer.update()
        pts = []
        for ob in self.objs:
            n = len(ob.data.vertices)
            a = np.empty(n * 3, np.float32)
            ob.data.vertices.foreach_get('co', a)
            pts.append(a.reshape(-1, 3))
        pts = np.concatenate(pts)
        self.lo, self.hi = pts.min(0), pts.max(0)
        # width of the body and wheels (mirrors left out)
        wx = 0.0
        for ob in self.objs:
            if ob.name.split('_')[-1] in ('body', 'wheels', 'box', 'cabin'):
                a = np.empty(len(ob.data.vertices) * 3, np.float32)
                ob.data.vertices.foreach_get('co', a)
                wx = max(wx, float(np.abs(a.reshape(-1, 3)[:, 0]).max()))
        self.dims = (float(self.hi[1] - self.lo[1]), 2 * wx, float(self.hi[2]))
        print('%s: length %.3f width %.3f height %.3f  (y %.3f..%.3f, x %.3f..%.3f, z %.3f..%.3f)' % (
            self.key, self.dims[0], self.dims[1], self.dims[2], self.lo[1], self.hi[1], self.lo[0], self.hi[0],
            self.lo[2], self.hi[2]))

    def place(self, rear_y, yaw_deg=0.0, x=0.0):
        """rear bumper's ground point at (x, rear_y, 0) before yaw; yaw about the car's centre"""
        cy = (self.lo[1] + self.hi[1]) / 2
        M = (Matrix.Translation((x, rear_y - self.lo[1] + cy, 0)) @
             Matrix.Rotation(math.radians(-yaw_deg), 4, 'Z') @ Matrix.Translation((0, -cy, 0)))
        self.root.matrix_world = M
        bpy.context.view_layer.update()

    def hide(self, h):
        for o in self.objs:
            o.hide_render = h


def arch_cutter(mb, axles, R, xin, xout=1.5, n=40):
    """cylinders along X around each wheel (both sides) to cut the arches"""
    for (y, rr) in axles:
        for s in (1, -1):
            xm = s * (xin + xout) / 2
            mb.cyl((xm, y, rr[1]), (1, 0, 0), rr[0], xout - xin, 'under', n=n)


def cut_arches(car, body_ob, axles, xin):
    cmb = MB()
    arch_cutter(cmb, axles, None, xin)
    cut = mb_object(car.key + '_cutter', cmb, car.coll, smooth_angle=30)
    cut.hide_render = True
    m = body_ob.modifiers.new('arches', 'BOOLEAN')
    m.operation = 'DIFFERENCE'
    m.solver = 'EXACT'
    m.object = cut
    apply_modifiers(body_ob)
    bpy.data.objects.remove(cut)


# --- the wedge ------------------------------------------------------------

def build_wedge():
    car = Car('wedge')
    L = 4.50
    bk = [
        (0.00, dict(zb=0.30, zs=0.855, zt=0.885, wb=0.68, wc=0.89, ws=0.83, rt=0.09, rb=0.04, zcf=0.6)),
        (0.22, dict(zb=0.17)),
        (0.35, dict(zb=0.15, zs=0.86, zt=0.89, wb=0.72, wc=0.97, ws=0.88)),
        (0.75, dict(wb=0.78, wc=1.06, ws=0.93)),
        (1.05, dict(wb=0.82, wc=1.10, ws=0.95, zcf=0.62)),
        (1.30, dict(zs=0.85, zt=0.88, wb=0.82, wc=1.09, ws=0.94, zcf=0.62)),
        (1.95, dict(zs=0.79, zt=0.84, wb=0.86, wc=1.00, ws=0.90, zcf=0.5)),
        (2.50, dict(zs=0.72, zt=0.765, wc=0.975, ws=0.875)),
        (3.10, dict(zs=0.64, zt=0.69, wb=0.86, wc=0.95, ws=0.85, rt=0.07)),
        (3.80, dict(zs=0.56, zt=0.60, wb=0.84, wc=0.93, ws=0.83, zb=0.15)),
        (4.30, dict(zs=0.48, zt=0.52, wb=0.79, wc=0.88, ws=0.77, zb=0.19, rt=0.06)),
        (4.50, dict(zs=0.42, zt=0.46, wb=0.70, wc=0.80, ws=0.68, zb=0.24)),
    ]
    ck = [
        (1.34, dict(zr=0.84, wbase=0.76, wroof=0.70, rr=0.06, dz=0.03)),
        (1.50, dict(zr=1.03, wroof=0.62)),
        (1.80, dict(zr=1.115, wroof=0.575)),
        (2.30, dict(zr=1.13, wbase=0.78, wroof=0.57)),
        (2.55, dict(zr=1.10)),
        (3.14, dict(zr=0.68, wbase=0.80, wroof=0.62)),
    ]
    b = Body(bk, ck, L, sig={'*': 0.07, 'zb': 0.03}, csig=0.05)
    car.body = b
    RY, FY = 0.98, 3.62                 # axles
    RR, FR = 0.335, 0.315               # tyre radii
    RW, FW = 0.34, 0.26                 # tyre widths
    RX, FX = 0.895, 0.80                # wheel centres x

    # --- paint masks (object coords, x as |x|)
    strake_zone = poly(('y', 'z'), [(1.48, 0.33), (2.95, 0.33), (2.95, 0.67), (1.48, 0.74)])
    body_entries = [
        ('paintB', [strake_zone + [gt('anx', 0.5)],
                    # lower body / rear valance under the tail panel (two-tone)
                    [lt('z', 0.40), C(nz=-1.0, gt=-0.6)]]),
    ]
    make_material('wedge_body', body_entries, 'paintA')
    # cabin: windscreen, side windows, rear window; roof in paint A
    ws = poly(('ax', 'z'), [(0.0, 0.66), (0.74, 0.66), (0.52, 1.09), (0.0, 1.09)])
    side = poly(('y', 'z'), [(1.62, 0.84), (2.88, 0.80), (2.52, 1.075), (1.86, 1.085)])
    rear = poly(('ax', 'z'), [(0.0, 0.86), (0.66, 0.86), (0.56, 1.06), (0.0, 1.06)])
    cab_entries = [
        ('glass', [ws + [gt('ny', 0.30)],
                   side + [gt('anx', 0.35)],
                   rear + [lt('ny', -0.30)]]),
    ]
    make_material('wedge_cabin', cab_entries, 'paintA')

    mb = MB()

    def body_mat(cen, nrm):
        n = nrm.normalized() if nrm.length > 0 else nrm
        if n.z < -0.6 or cen.z < b.prof(cen.y)['zb'] + 0.01:
            return 'under'
        return 'wedge_body'
    loft(mb, b.ring, 0.0, L, 0.03, 0.07, 0.16, body_mat)
    body = car.add('body', mb)
    cut_arches(car, body, [(RY, (RR + 0.045, RR)), (FY, (FR + 0.04, FR))], 0.52)

    mb = MB()
    loft(mb, b.cabin_prof, ck[0][0], ck[-1][0], 0.02, 0.02, 0.02,
         lambda c, n: 'wedge_cabin')
    car.add('cabin', mb)

    # --- wheels
    mb = MB()
    for s in (1, -1):
        wheel(mb, (s * RX, RY, RR), s, RR, RW, 'star', rim_frac=0.62)
        wheel(mb, (s * FX, FY, FR), s, FR, FW, 'star', rim_frac=0.62)
    car.add('wheels', mb)

    # --- arch liners: dark wheel wells so the tyre sits in shadow, not in a hole
    mb = MB()
    for y, r in ((RY, RR + 0.04), (FY, FR + 0.035)):
        for s in (1, -1):
            mb.cyl((s * 0.62, y, r + 0.005), (1, 0, 0), r, 0.18, 'under', n=32, cap=True)
    car.add('wells', mb)

    # --- tail: full-width louvred panel over the lights
    mb = MB()
    zt0, zt1 = 0.555, 0.795
    xw = 0.76
    ry = 0.0
    mb.boxr((-xw, ry - 0.012, zt0), (xw, ry + 0.05, zt1), 'tail')
    # centre section behind the louvres is black (reversing/grille area)
    mb.boxr((-0.30, ry - 0.016, zt0 + 0.01), (0.30, ry + 0.05, zt1 - 0.01), 'black')
    # frame
    mb.boxr((-xw - 0.02, ry - 0.030, zt1), (xw + 0.02, ry + 0.05, zt1 + 0.022), 'black')
    mb.boxr((-xw - 0.02, ry - 0.030, zt0 - 0.022), (xw + 0.02, ry + 0.05, zt0), 'black')
    for s in (1, -1):
        mb.boxr((s * xw - 0.02 if s > 0 else -xw - 0.02, ry - 0.030, zt0 - 0.02),
                (s * xw + 0.02 if s > 0 else -xw + 0.02, ry + 0.05, zt1 + 0.02), 'black')
    nl = 6
    for i in range(1, nl):
        z = zt0 + (zt1 - zt0) * i / nl
        mb.boxr((-xw, ry - 0.034, z - 0.009), (xw, ry + 0.01, z + 0.009), 'black')
    car.add('tail', mb, bevel=0.004)

    # plate, rear bumper / diffuser, exhausts
    mb = MB()
    mb.boxr((-0.27, -0.012, 0.395), (0.27, 0.03, 0.515), 'plate')
    mb.boxr((-0.29, -0.006, 0.38), (0.29, 0.03, 0.53), 'black')
    mb.boxr((-0.64, -0.02, 0.15), (0.64, 0.35, 0.30), 'black')     # lower valance / diffuser
    for x in (-0.5, -0.25, 0.0, 0.25, 0.5):
        mb.boxr((x - 0.012, -0.03, 0.15), (x + 0.012, 0.25, 0.28), 'black')
    for s in (1, -1):
        for dx in (0.0, 0.10):
            x = s * (0.30 + dx)
            mb.cyl((x, 0.05, 0.235), (0, 1, 0), 0.045, 0.2, 'chrome', n=20, cap=False)
            mb.cyl((x, 0.06, 0.235), (0, 1, 0), 0.036, 0.2, 'under', n=20)
        # small amber/reflector at the corners of the bumper
        mb.boxr((s * 0.58 - 0.05, -0.03, 0.245), (s * 0.58 + 0.05, 0.0, 0.28), 'tail')
    car.add('rear', mb, bevel=0.006)

    # flying buttresses: the roof's sail panels run down to the tail
    mb = MB()

    def butt_ring(y):
        t = clamp((y - 0.10) / (1.62 - 0.10), 0.0, 1.0)
        xo = min(b.prof(y)['ws'] - 0.05, 0.79)
        xi = xo - 0.15 - 0.02 * t
        zd = b.top_z(y, xo - 0.08) - 0.06
        zt_ = b.top_z(y, xo - 0.08) + 0.035 + 0.20 * t ** 1.4
        out = rrect_ring(xi, xo, zd, zt_, 0.03, 0.05)
        return out
    for sgn in (1, -1):
        mbs = MB()
        loft(mbs, butt_ring, 0.06, 1.66, 0.04, 0.04, 0.02, lambda c, n: 'paintA')
        if sgn < 0:
            mbs.v = [Vector((-v.x, v.y, v.z)) for v in mbs.v]
        mb.f += [tuple(i + len(mb.v) for i in f) for f in mbs.f]
        mb.fm += mbs.fm
        mb.v += mbs.v
    car.add('buttress', mb)

    # engine cover louvres on the rear deck, between the buttresses
    mb = MB()
    y0, y1, xl = 0.26, 1.30, 0.52
    ztop = b.top_z(0.8)
    mb.boxr((-xl, y0, ztop - 0.03), (xl, y1, ztop + 0.004), 'black')
    for i in range(9):
        y = y0 + 0.05 + (y1 - y0 - 0.1) * i / 8
        mb.boxr((-xl + 0.02, y - 0.022, ztop - 0.01), (xl - 0.02, y + 0.022, ztop + 0.014), 'paintA')
    car.add('deck', mb, bevel=0.006)

    # side strakes: five fins across the door and the intake behind it
    mb = MB()
    for s in (1, -1):
        for i in range(5):
            z = 0.39 + i * 0.068
            ya, yb = 1.52, 2.90 - 0.05 * i
            xs = min(b.side_x(ya + 0.3, z), b.side_x(yb - 0.2, z))
            n = 8
            for k in range(n):
                y = ya + (yb - ya) * (k + 0.5) / n
                x = b.side_x(y, z)
                mb.box((s * (x + 0.012), y, z), (0.05, (yb - ya) / n + 0.01, 0.028), 'paintB')
        # the intake behind the strakes
        for k in range(10):
            y = 1.52 + (2.5 - 1.52) * (k + 0.5) / 10
            for zz in (0.42, 0.49, 0.56, 0.63):
                pass
    car.add('strakes', mb, bevel=0.008)

    # mirrors, front lamps, indicators
    mb = MB()
    for s in (1, -1):
        x = b.side_x(2.93, 0.72)
        mb.box((s * (x + 0.04), 2.95, 0.78), (0.10, 0.04, 0.03), 'black')
        mb.box((s * (x + 0.12), 2.93, 0.82), (0.16, 0.07, 0.09), 'paintA')
        # front: turn lamps and driving lamps in the nose
        mb.box((s * 0.58, L - 0.02, 0.36), (0.30, 0.04, 0.07), 'head')
        mb.box((s * 0.80, L - 0.08, 0.36), (0.07, 0.07, 0.06), 'amber')
    mb.box((0, L - 0.03, 0.26), (1.1, 0.04, 0.07), 'black')
    car.add('trim', mb, bevel=0.01)
    car.axles = (RY, FY)
    return car


# ---------------------------------------------------------------------------
# Shared helpers for the other vehicles
# ---------------------------------------------------------------------------

def cut_with(car, ob, cmb, use_self=False):
    """boolean-difference the (hidden) MB cmb out of ob; cut faces take the
    cutter's material keys (use_self: for a loft that the solver otherwise
    reads inside-out and returns empty, e.g. the hearse's greenhouse)"""
    cut = mb_object(car.key + '_cutter', cmb, car.coll, smooth_angle=30)
    cut.hide_render = True
    m = ob.modifiers.new('cut', 'BOOLEAN')
    m.operation = 'DIFFERENCE'
    m.solver = 'EXACT'
    m.use_self = use_self
    m.object = cut
    apply_modifiers(ob)
    bpy.data.objects.remove(cut)


def body_part(car, b, L, mat, r0, r1, arches, xin, step=0.03, extra_cut=None, name='body', y0=0.0,
              shift=0.0):
    mb = MB()

    def bm(cen, nrm):
        n = nrm.normalized() if nrm.length > 0 else nrm
        if n.z < -0.6 or cen.z < b.prof(cen.y)['zb'] + 0.01:
            return 'under'
        return mat
    loft(mb, b.ring, y0, L, step, r0, r1, bm)
    if shift:
        mb.v = [v + Vector((0, shift, 0)) for v in mb.v]
    ob = car.add(name, mb)
    cmb = MB()
    if arches:
        arch_cutter(cmb, arches, None, xin)
    if extra_cut:
        extra_cut(cmb)
    if cmb.f:
        cut_with(car, ob, cmb)
    return ob


def cabin_part(car, b, mat, r0=0.02, r1=0.02, step=0.02, shift=0.0):
    mb = MB()
    c0, c1 = b.cabin
    loft(mb, b.cabin_prof, c0, c1, step, r0, r1, lambda c, n: mat)
    if shift:
        mb.v = [v + Vector((0, shift, 0)) for v in mb.v]
    return car.add('cabin', mb)


def add_wheels(car, specs, name='wheels'):
    """specs: [(x, y, R, W, style, rim_frac, tread)] mirrored to both sides"""
    mb = MB()
    for x, y, R, W, style, frac, tread in specs:
        for s in (1, -1):
            wheel(mb, (s * x, y, R), s, R, W, style, rim_frac=frac, tread=tread)
    car.add(name, mb)


def add_wells(car, specs):
    """dark wheel wells: [(y, r, x_centre, width, zc)]"""
    mb = MB()
    for y, r, xc, w, zc in specs:
        zc = max(zc, r + 0.005)          # never below the road
        for s in (1, -1):
            mb.cyl((s * xc, y, zc), (1, 0, 0), r, w, 'under', n=32)
    car.add('wells', mb)


def lamp_round(mb, x, z, r, key, y=0.0, bezel='chrome', depth=0.05, n=24):
    mb.cyl((x, y + 0.012, z), (0, 1, 0), r, depth, key, n=n)
    if bezel:
        mb.cyl((x, y + 0.03, z), (0, 1, 0), r + 0.018, depth, bezel, n=n)


def mirrors(mb, b, y, z, key, dz=0.05, reach=0.08, size=(0.13, 0.06, 0.08), shift=0.0):
    for s in (1, -1):
        x = b.side_x(y, z - dz)
        mb.box((s * (x + reach * 0.4), y + shift, z - dz * 0.5), (reach, 0.03, 0.03), 'black')
        mb.box((s * (x + reach + size[0] * 0.3), y + shift, z), size, key)


def sym_boxr(mb, lo, hi, key):
    """box given for the +x side, mirrored to -x"""
    mb.boxr(lo, hi, key)
    mb.boxr((-hi[0], lo[1], lo[2]), (-lo[0], hi[1], hi[2]), key)


def stripes(a, b, nz=0.3):
    return [gt('ax', a), lt('ax', b), gt('nz', nz)]


# --- the muscle car -------------------------------------------------------

def build_muscle():
    car = Car('muscle')
    L = 4.70
    bk = [
        (0.00, dict(zb=0.36, zs=0.93, zt=0.975, wb=0.68, wc=0.84, ws=0.80, rt=0.06, rb=0.04, zcf=0.55)),
        (0.12, dict(zb=0.24)),
        (0.30, dict(zs=0.90, zt=0.945, wb=0.72, wc=0.92, ws=0.845)),
        (0.70, dict(wb=0.80, wc=1.05, ws=0.89, zs=0.88, zt=0.935)),
        (1.35, dict(wc=1.03)),
        (1.95, dict(wc=0.955, ws=0.88, zs=0.87, zt=0.925)),
        (3.00, dict(zs=0.86, zt=0.92, wc=0.95, ws=0.87)),
        (3.90, dict(zs=0.84, zt=0.905, wc=0.97)),
        (4.45, dict(zs=0.79, zt=0.85, wb=0.78, wc=0.91, ws=0.85, zb=0.26)),
        (4.70, dict(zs=0.68, zt=0.73, wb=0.72, wc=0.83, ws=0.79, zb=0.32)),
    ]
    ck = [
        (0.52, dict(zr=0.90, wbase=0.80, wroof=0.74, rr=0.05, dz=0.04)),
        (1.00, dict(zr=1.01, wroof=0.68)),
        (1.60, dict(zr=1.16)),
        (2.10, dict(zr=1.285, wroof=0.62, rr=0.08)),
        (2.65, dict(zr=1.30, wbase=0.82)),
        (2.95, dict(zr=1.19)),
        (3.24, dict(zr=0.90, wroof=0.72)),
    ]
    b = Body(bk, ck, L, sig={'*': 0.08, 'zb': 0.03, 'zt': 0.05, 'zs': 0.05}, csig=0.07)
    RY, FY = 0.90, 3.92
    RR, FR = 0.345, 0.33
    make_material('muscle_body', [('paintB', [stripes(0.085, 0.245)])], 'paintA')
    ws = poly(('ax', 'z'), [(0.0, 0.93), (0.76, 0.93), (0.58, 1.27), (0.0, 1.27)])
    side = poly(('y', 'z'), [(1.95, 0.975), (3.10, 0.955), (2.72, 1.255), (2.10, 1.26)])
    rear = poly(('ax', 'y'), [(0.0, 1.18), (0.50, 1.18), (0.55, 2.02), (0.0, 2.02)])
    make_material('muscle_cabin', [('glass', [ws + [gt('ny', 0.30)], side + [gt('anx', 0.35)],
                                              rear + [gt('nz', 0.45), lt('ny', 0.1)]]),
                                   ('paintB', [stripes(0.085, 0.245)])], 'paintA')
    body_part(car, b, L, 'muscle_body', 0.05, 0.12,
              [(RY, (RR + 0.05, RR)), (FY, (FR + 0.045, FR))], 0.56)
    cabin_part(car, b, 'muscle_cabin')
    add_wheels(car, [(0.84, RY, RR, 0.34, 'slot', 0.60, None), (0.785, FY, FR, 0.26, 'slot', 0.60, None)])
    add_wells(car, [(RY, RR + 0.045, 0.64, 0.20, RR), (FY, FR + 0.04, 0.64, 0.20, FR)])

    # ducktail lip, hood scoop (share the body paint so the stripes run over them)
    mb = MB()
    zt = b.top_z(0.05)
    mb.box((0, 0.13, zt + 0.012), (1.56, 0.24, 0.05), 'muscle_body', rot=Matrix.Rotation(math.radians(9), 3, 'X'))
    zh = b.top_z(3.6)
    mb.boxr((-0.24, 3.25, zh - 0.02), (0.24, 3.95, zh + 0.07), 'muscle_body')
    car.add('lip', mb, bevel=0.02, segs=3)
    mb = MB()
    mb.boxr((-0.20, 3.955, zh + 0.0), (0.20, 3.975, zh + 0.055), 'black')   # scoop mouth
    car.add('scoop', mb)

    # tail: black panel, twin round lamps per side, chrome bumper
    mb = MB()
    mb.boxr((-0.72, -0.010, 0.60), (0.72, 0.05, 0.86), 'black')
    for s in (1, -1):
        for x in (0.28, 0.545):
            lamp_round(mb, s * x, 0.73, 0.088, 'tail', y=-0.03)
    mb.cyl((0, -0.02, 0.73), (0, 1, 0), 0.045, 0.04, 'chrome', n=20)      # generic round badge
    car.add('tail', mb, bevel=0.004)
    mb = MB()
    mb.boxr((-0.80, -0.055, 0.42), (0.80, 0.16, 0.53), 'chrome')
    for s in (1, -1):
        mb.box((s * 0.80, 0.10, 0.475), (0.07, 0.26, 0.11), 'chrome', rot=Matrix.Rotation(math.radians(-s * 25), 3, 'Z'))
    mb.boxr((-0.21, -0.02, 0.27), (0.21, 0.04, 0.39), 'plate')
    mb.boxr((-0.23, -0.005, 0.255), (0.23, 0.05, 0.405), 'black')
    for s in (1, -1):
        mb.cyl((s * 0.56, 0.08, 0.27), (0, 1, 0), 0.042, 0.26, 'chrome', n=20, cap=False)
        mb.cyl((s * 0.56, 0.09, 0.27), (0, 1, 0), 0.033, 0.26, 'under', n=20)
        mb.box((s * 0.86, 0.30, 0.66), (0.04, 0.10, 0.05), 'tail')         # side marker
        mb.box((s * 0.905, 4.25, 0.64), (0.04, 0.10, 0.05), 'amber')
    # front: chrome bumper, black grille, round lamps
    mb.boxr((-0.82, L - 0.10, 0.34), (0.82, L + 0.03, 0.45), 'chrome')
    mb.boxr((-0.72, L - 0.06, 0.48), (0.72, L - 0.0, 0.66), 'black')
    for s in (1, -1):
        mb.cyl((s * 0.58, L - 0.02, 0.57), (0, 1, 0), 0.08, 0.05, 'head', n=20)
    mirrors(mb, b, 3.05, 0.99, 'chrome')
    car.add('trim', mb, bevel=0.012)
    return car


# --- the rally hatch ------------------------------------------------------

def build_rally():
    car = Car('rally')
    L = 3.90
    bk = [
        (0.00, dict(zb=0.30, zs=0.86, zt=0.90, wb=0.60, wc=0.77, ws=0.73, rt=0.07, rb=0.04, zcf=0.5)),
        (0.14, dict(zb=0.22)),
        (0.30, dict(wb=0.66, wc=0.88, ws=0.78)),
        (0.70, dict(wb=0.74, wc=1.06, ws=0.84, zcf=0.58)),
        (1.12, dict(wb=0.72, wc=0.90, ws=0.82, zcf=0.5)),
        (1.50, dict(wc=0.87, ws=0.81)),
        (2.60, dict(wc=0.88, ws=0.81, zs=0.84, zt=0.88)),
        (3.14, dict(wb=0.74, wc=1.00, ws=0.84, zs=0.80, zt=0.845, zcf=0.58)),
        (3.62, dict(wc=0.93, zs=0.76, zt=0.80, zb=0.24)),
        (3.90, dict(zs=0.66, zt=0.70, wb=0.68, wc=0.84, ws=0.77, zb=0.30)),
    ]
    ck = [
        (0.10, dict(zr=0.88, wbase=0.78, wroof=0.74, rr=0.05, dz=0.03)),
        (0.18, dict(zr=1.24)),
        (0.40, dict(zr=1.38, wroof=0.67, rr=0.08)),
        (1.90, dict(zr=1.40, wbase=0.80)),
        (2.15, dict(zr=1.36)),
        (2.78, dict(zr=0.85, wroof=0.72)),
    ]
    b = Body(bk, ck, L, sig={'*': 0.07, 'zb': 0.03}, csig=0.035)
    RY, FY = 0.72, 3.16
    R = 0.32
    band = [gt('z', 0.50), lt('z', 0.64), C(nz=-1.0, gt=-0.6)]
    make_material('rally_body', [('paintB', [band, stripes(0.0, 0.16, 0.5) + [gt('y', 2.3)]])], 'paintA')
    ws = poly(('ax', 'z'), [(0.0, 0.87), (0.76, 0.87), (0.62, 1.33), (0.0, 1.33)])
    side = poly(('y', 'z'), [(0.34, 0.95), (2.58, 0.93), (2.08, 1.33), (0.42, 1.34)])
    rear = poly(('ax', 'z'), [(0.0, 0.98), (0.66, 0.98), (0.60, 1.31), (0.0, 1.31)])
    make_material('rally_cabin', [('glass', [ws + [gt('ny', 0.30)], side + [gt('anx', 0.35)],
                                             rear + [lt('ny', -0.30)]]),
                                  ('paintB', [stripes(0.0, 0.16, 0.5)])], 'paintA')
    body_part(car, b, L, 'rally_body', 0.06, 0.10, [(RY, (R + 0.05, R)), (FY, (R + 0.05, R))], 0.50)
    cabin_part(car, b, 'rally_cabin')
    add_wheels(car, [(0.82, RY, R, 0.28, 'mesh', 0.66, None), (0.78, FY, R, 0.26, 'mesh', 0.66, None)])
    add_wells(car, [(RY, R + 0.045, 0.58, 0.18, R), (FY, R + 0.045, 0.58, 0.18, R)])

    mb = MB()
    # tail lamps high on the corners, amber inboard, plate, black bumper
    for s in (1, -1):
        sym_boxr(mb, (0.47, -0.012, 0.66), (0.70, 0.04, 0.83), 'tail') if s > 0 else None
        sym_boxr(mb, (0.39, -0.010, 0.66), (0.47, 0.04, 0.83), 'amber') if s > 0 else None
        sym_boxr(mb, (0.37, -0.004, 0.645), (0.72, 0.045, 0.845), 'black') if s > 0 else None
    mb.boxr((-0.22, -0.012, 0.66), (0.22, 0.04, 0.80), 'plate')
    mb.boxr((-0.24, -0.004, 0.645), (0.24, 0.045, 0.815), 'black')
    mb.boxr((-0.72, -0.03, 0.20), (0.72, 0.22, 0.40), 'black')
    mb.cyl((-0.50, 0.06, 0.24), (0, 1, 0), 0.05, 0.22, 'chrome', n=20, cap=False)
    mb.cyl((-0.50, 0.07, 0.24), (0, 1, 0), 0.04, 0.22, 'under', n=20)
    # mud flaps behind the rear wheels
    for s in (1, -1):
        mb.boxr((s * 0.68 if s > 0 else -0.96, 0.30, 0.05), (0.96 if s > 0 else -0.68, 0.33, 0.34), 'black')
    # front: black bumper, four spot lamps, headlamps
    mb.boxr((-0.80, L - 0.20, 0.24), (0.80, L + 0.0, 0.42), 'black')
    for x in (-0.54, -0.19, 0.19, 0.54):
        mb.cyl((x, L + 0.02, 0.52), (0, 1, 0), 0.085, 0.07, 'black', n=20)
        mb.cyl((x, L + 0.05, 0.52), (0, 1, 0), 0.07, 0.02, 'head', n=20)
    for s in (1, -1):
        mb.box((s * 0.52, L - 0.05, 0.62), (0.30, 0.06, 0.12), 'head')
    mb.boxr((-0.30, L - 0.04, 0.58), (0.30, L - 0.01, 0.66), 'black')
    mirrors(mb, b, 2.55, 0.98, 'black')
    car.add('trim', mb, bevel=0.01)

    # rear wing on the roof's trailing edge, roof vent
    mb = MB()
    zr = 1.38
    mb.box((0, 0.20, zr + 0.13), (1.58, 0.34, 0.035), 'paintA', rot=Matrix.Rotation(math.radians(-8), 3, 'X'))
    for s in (1, -1):
        mb.box((s * 0.80, 0.22, zr + 0.10), (0.025, 0.40, 0.16), 'black')
        mb.box((s * 0.40, 0.30, zr + 0.05), (0.04, 0.14, 0.14), 'black')
    mb.boxr((-0.20, 1.40, zr + 0.01), (0.20, 1.75, zr + 0.06), 'paintA')
    car.add('wing', mb, bevel=0.008)
    mb = MB()
    mb.boxr((-0.17, 1.755, zr + 0.015), (0.17, 1.765, zr + 0.055), 'black')
    for i in range(4):
        y = 1.46 + i * 0.07
        mb.boxr((-0.16, y, zr + 0.058), (0.16, y + 0.03, zr + 0.064), 'black')
    car.add('vent', mb)
    return car


# --- the pickup -----------------------------------------------------------

def build_pickup():
    car = Car('pickup')
    L = 4.70
    bk = [
        (0.00, dict(zb=0.68, zs=1.10, zt=1.13, wb=0.80, wc=0.85, ws=0.835, rt=0.05, rb=0.04, zcf=0.5)),
        (0.10, dict(zb=0.66)),
        (0.60, dict(zb=0.66)),
        (0.75, dict(zb=0.58)),
        (0.35, dict(wb=0.84, wc=0.895, ws=0.87)),
        (3.25, dict(zs=1.10, zt=1.13)),
        (3.45, dict(zs=1.08, zt=1.115)),
        (4.35, dict(zs=1.04, zt=1.09, wb=0.82, wc=0.88, ws=0.845)),
        (4.70, dict(zs=0.97, zt=1.02, wb=0.78, wc=0.84, ws=0.80, zb=0.64)),
    ]
    ck = [
        (1.90, dict(zr=1.78, wbase=0.84, wroof=0.79, rr=0.06, dz=0.03)),
        (2.95, dict(zr=1.80)),
        (3.12, dict(zr=1.74, wroof=0.78)),
        (3.62, dict(zr=1.10, wroof=0.81, wbase=0.845)),
    ]
    b = Body(bk, ck, L, sig={'*': 0.04}, csig=0.03)
    RY, FY = 1.02, 3.95
    R = 0.40
    make_material('pickup_body', [('paintB', [[lt('z', 0.80), C(nz=-1.0, gt=-0.6)]])], 'paintA')
    ws = poly(('ax', 'z'), [(0.0, 1.16), (0.76, 1.16), (0.70, 1.72), (0.0, 1.72)])
    side = poly(('y', 'z'), [(2.02, 1.20), (3.30, 1.20), (3.02, 1.70), (2.02, 1.71)])
    rear = poly(('ax', 'z'), [(0.0, 1.26), (0.60, 1.26), (0.60, 1.68), (0.0, 1.68)])
    make_material('pickup_cabin', [('glass', [ws + [gt('ny', 0.30)], side + [gt('anx', 0.35)],
                                              rear + [lt('ny', -0.50)]])], 'paintA')

    def bed(cmb):
        for i, f in enumerate(MB().f):
            pass
        cmb.boxr((-0.755, 0.075, 0.84), (0.755, 1.86, 2.0), 'interior')
    body_part(car, b, L, 'pickup_body', 0.04, 0.10, [(RY, (R + 0.05, R)), (FY, (R + 0.05, R))], 0.52,
              extra_cut=bed)
    cabin_part(car, b, 'pickup_cabin')
    add_wheels(car, [(0.86, RY, R, 0.34, 'steel', 0.50, (22, 0.016)),
                     (0.80, FY, R, 0.31, 'steel', 0.50, (22, 0.016))])
    add_wells(car, [(RY, R + 0.045, 0.60, 0.16, R + 0.05), (FY, R + 0.045, 0.60, 0.16, R + 0.05)])

    mb = MB()
    # tail lamps on the bed corners, tailgate handle, chrome step bumper, plate
    for s in (1, -1):
        sym_boxr(mb, (0.70, -0.014, 0.78), (0.83, 0.03, 1.07), 'tail') if s > 0 else None
        sym_boxr(mb, (0.70, -0.016, 0.93), (0.83, 0.03, 0.97), 'amber') if s > 0 else None
    mb.boxr((-0.14, -0.012, 1.00), (0.14, 0.03, 1.05), 'black')
    mb.boxr((-0.60, -0.006, 1.075), (0.60, 0.03, 1.09), 'black')              # tailgate top seam
    mb.boxr((-0.64, -0.06, 0.53), (0.64, 0.20, 0.68), 'chrome')
    mb.boxr((-0.20, -0.075, 0.55), (0.20, -0.055, 0.66), 'plate')
    mb.boxr((-0.07, -0.02, 0.44), (0.07, 0.25, 0.53), 'black')                  # hitch receiver
    # chassis: frame rails, rear axle with the diff, exhaust
    for s in (1, -1):
        mb.boxr((s * 0.40 if s > 0 else -0.50, 0.10, 0.45), (0.50 if s > 0 else -0.40, 4.50, 0.62), 'under')
    mb.cyl((0, RY, R), (1, 0, 0), 0.055, 1.40, 'under', n=16)
    mb.cyl((0.08, RY + 0.02, R - 0.02), (0, 1, 0), 0.15, 0.30, 'under', n=20)
    mb.cyl((0.55, 0.30, 0.42), (0, 1, 0), 0.04, 0.5, 'under', n=16)
    # front: chrome bumper, black grille, lamps
    mb.boxr((-0.86, L - 0.12, 0.50), (0.86, L + 0.02, 0.66), 'chrome')
    mb.boxr((-0.62, L - 0.03, 0.72), (0.62, L + 0.005, 0.94), 'black')
    for s in (1, -1):
        mb.boxr((s * 0.62 if s > 0 else -0.82, L - 0.04, 0.76), (0.82 if s > 0 else -0.62, L + 0.005, 0.90), 'head')
    mirrors(mb, b, 3.20, 1.30, 'black', size=(0.12, 0.05, 0.16))
    car.add('trim', mb, bevel=0.01)

    # roll bar with two lamps
    mb = MB()
    yb, zf, zt_ = 1.68, 0.84, 1.62
    for s in (1, -1):
        mb.cyl((s * 0.66, yb, (zf + zt_) / 2), (0, 0, 1), 0.04, zt_ - zf, 'extra', n=16)
        mb.cyl((s * 0.66, yb - 0.35, (zf + zt_) / 2 - 0.05), (0, 0.9, 1.0), 0.03, 0.95, 'extra', n=12)
    mb.cyl((0, yb, zt_), (1, 0, 0), 0.042, 1.40, 'extra', n=16)
    for s in (1, -1):
        mb.cyl((s * 0.35, yb + 0.02, zt_ + 0.11), (0, 1, 0), 0.095, 0.13, 'extra', n=20)
        mb.cyl((s * 0.35, yb + 0.09, zt_ + 0.11), (0, 1, 0), 0.08, 0.02, 'head', n=20)
        mb.box((s * 0.35, yb, zt_ + 0.04), (0.04, 0.04, 0.08), 'extra')
    car.add('rollbar', mb)
    return car


# --- traffic ----------------------------------------------------------------

def build_sedan():
    car = Car('sedan')
    L = 4.70
    bk = [
        (0.00, dict(zb=0.30, zs=0.78, zt=0.82, wb=0.74, wc=0.87, ws=0.83, rt=0.08, rb=0.04, zcf=0.5)),
        (0.15, dict(zb=0.22)),
        (0.40, dict(wb=0.78, wc=0.91, ws=0.855)),
        (1.00, dict(zs=0.80, zt=0.84)),
        (3.30, dict(zs=0.78, zt=0.82)),
        (4.40, dict(zs=0.70, zt=0.745, zb=0.24)),
        (4.70, dict(zs=0.60, zt=0.645, wb=0.72, wc=0.84, ws=0.78, zb=0.30)),
    ]
    ck = [
        (0.95, dict(zr=0.80, wbase=0.82, wroof=0.74, rr=0.06, dz=0.04)),
        (1.55, dict(zr=1.38, wroof=0.66, rr=0.10)),
        (2.60, dict(zr=1.42, wbase=0.84)),
        (3.30, dict(zr=0.78, wroof=0.74)),
    ]
    b = Body(bk, ck, L, sig={'*': 0.09, 'zb': 0.03}, csig=0.10)
    RY, FY, R = 1.10, 3.82, 0.31
    make_material('sedan_body', [('paintB', [[lt('z', 0.40), C(nz=-1.0, gt=-0.6)]])], 'paintA')
    ws = poly(('ax', 'z'), [(0.0, 0.85), (0.78, 0.85), (0.60, 1.36), (0.0, 1.36)])
    side = poly(('y', 'z'), [(1.18, 0.88), (3.12, 0.86), (2.62, 1.35), (1.52, 1.37)])
    rear = poly(('ax', 'z'), [(0.0, 0.88), (0.74, 0.88), (0.58, 1.34), (0.0, 1.34)])
    make_material('sedan_cabin', [('glass', [ws + [gt('ny', 0.25)], side + [gt('anx', 0.35)],
                                             rear + [lt('ny', -0.25)]])], 'paintA')
    body_part(car, b, L, 'sedan_body', 0.08, 0.14, [(RY, (R + 0.04, R)), (FY, (R + 0.04, R))], 0.52)
    cabin_part(car, b, 'sedan_cabin')
    add_wheels(car, [(0.76, RY, R, 0.21, 'turbine', 0.64, None), (0.76, FY, R, 0.21, 'turbine', 0.64, None)])
    add_wells(car, [(RY, R + 0.035, 0.60, 0.16, R), (FY, R + 0.035, 0.60, 0.16, R)])
    mb = MB()
    for s in (1, -1):
        mb.boxr((s * 0.42 if s > 0 else -0.80, -0.012, 0.60), (0.80 if s > 0 else -0.42, 0.05, 0.75), 'tail')
        mb.boxr((s * 0.32 if s > 0 else -0.42, -0.010, 0.60), (0.42 if s > 0 else -0.32, 0.05, 0.75), 'amber')
    mb.boxr((-0.32, -0.008, 0.62), (0.32, 0.05, 0.73), 'black')
    mb.boxr((-0.21, -0.012, 0.44), (0.21, 0.04, 0.56), 'plate')
    mb.boxr((-0.85, -0.02, 0.30), (0.85, 0.08, 0.34), 'black')                  # rubbing strip
    mb.cyl((-0.50, 0.10, 0.22), (0, 1, 0), 0.035, 0.24, 'under', n=16)
    mb.boxr((-0.70, L - 0.03, 0.52), (0.70, L + 0.005, 0.64), 'black')
    for s in (1, -1):
        mb.boxr((s * 0.46 if s > 0 else -0.76, L - 0.06, 0.53), (0.76 if s > 0 else -0.46, L + 0.01, 0.63), 'head')
    mirrors(mb, b, 3.10, 0.92, 'paintA')
    car.add('trim', mb, bevel=0.008)
    return car


def build_compact():
    car = Car('compact')
    L = 3.80
    bk = [
        (0.00, dict(zb=0.30, zs=0.80, zt=0.84, wb=0.66, wc=0.79, ws=0.75, rt=0.08, rb=0.04, zcf=0.5)),
        (0.15, dict(zb=0.22)),
        (0.40, dict(wb=0.70, wc=0.835, ws=0.785)),
        (3.20, dict(zs=0.76, zt=0.80)),
        (3.58, dict(zs=0.70, zt=0.74, zb=0.24)),
        (3.80, dict(zs=0.60, zt=0.645, wb=0.64, wc=0.76, ws=0.70, zb=0.30)),
    ]
    ck = [
        (0.10, dict(zr=0.86, wbase=0.76, wroof=0.72, rr=0.05, dz=0.03)),
        (0.26, dict(zr=1.30, wroof=0.64, rr=0.09)),
        (1.90, dict(zr=1.40, wbase=0.78)),
        (2.12, dict(zr=1.35)),
        (2.72, dict(zr=0.80, wroof=0.70)),
    ]
    b = Body(bk, ck, L, sig={'*': 0.08, 'zb': 0.03}, csig=0.06)
    RY, FY, R = 0.68, 3.05, 0.29
    make_material('compact_body', [('paintB', [[gt('z', 0.53), lt('z', 0.58), gt('anx', 0.3)]])], 'paintA')
    ws = poly(('ax', 'z'), [(0.0, 0.85), (0.72, 0.85), (0.60, 1.33), (0.0, 1.33)])
    side = poly(('y', 'z'), [(0.36, 0.93), (2.52, 0.90), (2.06, 1.32), (0.44, 1.33)])
    rear = poly(('ax', 'z'), [(0.0, 0.96), (0.62, 0.96), (0.56, 1.28), (0.0, 1.28)])
    make_material('compact_cabin', [('glass', [ws + [gt('ny', 0.30)], side + [gt('anx', 0.35)],
                                               rear + [lt('ny', -0.30)]]),
                                    ('paintB', [[gt('nz', 0.55), gt('z', 1.30)]])], 'paintA')
    body_part(car, b, L, 'compact_body', 0.08, 0.12, [(RY, (R + 0.04, R)), (FY, (R + 0.04, R))], 0.48)
    cabin_part(car, b, 'compact_cabin')
    add_wheels(car, [(0.69, RY, R, 0.19, 'steel', 0.62, None), (0.69, FY, R, 0.19, 'steel', 0.62, None)])
    add_wells(car, [(RY, R + 0.035, 0.55, 0.14, R), (FY, R + 0.035, 0.55, 0.14, R)])
    mb = MB()
    for s in (1, -1):
        mb.boxr((s * 0.56 if s > 0 else -0.74, -0.012, 0.58), (0.74 if s > 0 else -0.56, 0.05, 0.82), 'tail')
        mb.boxr((s * 0.56 if s > 0 else -0.74, -0.014, 0.66), (0.74 if s > 0 else -0.56, 0.05, 0.70), 'amber')
    mb.boxr((-0.21, -0.012, 0.44), (0.21, 0.04, 0.56), 'plate')
    mb.boxr((-0.78, -0.03, 0.22), (0.78, 0.12, 0.38), 'black')
    mb.cyl((-0.45, 0.10, 0.22), (0, 1, 0), 0.03, 0.24, 'under', n=16)
    mb.boxr((-0.66, L - 0.03, 0.50), (0.66, L + 0.005, 0.62), 'black')
    for s in (1, -1):
        mb.boxr((s * 0.44 if s > 0 else -0.68, L - 0.06, 0.51), (0.68 if s > 0 else -0.44, L + 0.01, 0.61), 'head')
    mirrors(mb, b, 2.52, 0.94, 'black')
    car.add('trim', mb, bevel=0.008)
    return car


def build_van():
    car = Car('van')
    L = 4.80
    bk = [
        (0.00, dict(zb=0.30, zs=0.92, zt=0.95, wb=0.80, wc=0.90, ws=0.88, rt=0.06, rb=0.04, zcf=0.5)),
        (0.15, dict(zb=0.24)),
        (4.15, dict(zs=0.92, zt=0.95)),
        (4.55, dict(zs=0.80, zt=0.845, zb=0.26)),
        (4.80, dict(zs=0.66, zt=0.70, wb=0.76, wc=0.86, ws=0.80, zb=0.32)),
    ]
    ck = [
        (0.03, dict(zr=0.94, wbase=0.88, wroof=0.85, rr=0.06, dz=0.02)),
        (0.09, dict(zr=1.88, rr=0.12, wroof=0.81)),
        (3.00, dict(zr=1.90, wbase=0.88)),
        (3.40, dict(zr=1.80)),
        (4.35, dict(zr=0.93, wroof=0.86)),
    ]
    b = Body(bk, ck, L, sig={'*': 0.07, 'zb': 0.03}, csig=0.05)
    RY, FY, R = 1.00, 3.92, 0.32
    make_material('van_body', [('paintB', [[lt('z', 0.52), C(nz=-1.0, gt=-0.6)]])], 'paintA')
    ws = poly(('ax', 'z'), [(0.0, 1.00), (0.80, 1.00), (0.74, 1.74), (0.0, 1.74)])
    side = poly(('y', 'z'), [(0.30, 1.20), (3.62, 1.20), (3.22, 1.74), (0.30, 1.75)])
    rear = poly(('ax', 'z'), [(0.0, 1.20), (0.74, 1.20), (0.72, 1.76), (0.0, 1.76)])
    make_material('van_cabin', [('glass', [ws + [gt('ny', 0.30)], side + [gt('anx', 0.35)],
                                           rear + [lt('ny', -0.50)]]),
                                ('paintB', [[lt('z', 0.52)]])], 'paintA')
    body_part(car, b, L, 'van_body', 0.05, 0.14, [(RY, (R + 0.04, R)), (FY, (R + 0.04, R))], 0.56)
    cabin_part(car, b, 'van_cabin')
    add_wheels(car, [(0.78, RY, R, 0.22, 'turbine', 0.62, None), (0.78, FY, R, 0.22, 'turbine', 0.62, None)])
    add_wells(car, [(RY, R + 0.035, 0.62, 0.16, R), (FY, R + 0.035, 0.62, 0.16, R)])
    mb = MB()
    for s in (1, -1):
        mb.boxr((s * 0.74 if s > 0 else -0.87, 0.0, 0.96), (0.87 if s > 0 else -0.74, 0.07, 1.46), 'tail')
        mb.boxr((s * 0.74 if s > 0 else -0.87, -0.002, 1.12), (0.87 if s > 0 else -0.74, 0.07, 1.18), 'amber')
    mb.boxr((-0.22, -0.012, 0.66), (0.22, 0.04, 0.78), 'plate')
    mb.boxr((-0.12, -0.012, 0.86), (0.12, 0.04, 0.90), 'black')
    mb.boxr((-0.88, -0.03, 0.26), (0.88, 0.14, 0.44), 'black')
    mb.cyl((-0.50, 0.10, 0.22), (0, 1, 0), 0.035, 0.24, 'under', n=16)
    mb.boxr((-0.30, 1.0, 1.905), (0.30, 1.02, 1.935), 'black')                     # roof rails
    for s in (1, -1):
        mb.boxr((s * 0.62 if s > 0 else -0.66, 0.4, 1.90), (0.66 if s > 0 else -0.62, 2.9, 1.95), 'black')
        mb.boxr((s * 0.48 if s > 0 else -0.76, L - 0.06, 0.60), (0.76 if s > 0 else -0.48, L + 0.01, 0.72), 'head')
    mb.boxr((-0.48, L - 0.03, 0.58), (0.48, L + 0.005, 0.72), 'black')
    mirrors(mb, b, 3.50, 1.18, 'black', size=(0.12, 0.05, 0.16))
    car.add('trim', mb, bevel=0.008)
    return car


def build_truck():
    car = Car('truck')
    # cargo box
    mb = MB()
    BX, BZ0, BZ1, BY0, BY1 = 1.20, 1.08, 3.30, 0.10, 5.30
    loft(mb, lambda y: rrect_ring(-BX, BX, BZ0, BZ1, 0.03, 0.06), BY0, BY1, 0.5, 0.03, 0.03,
         lambda c, n: 'truck_box')
    make_material('truck_box', [('black', [[lt('ny', -0.7), lt('ax', 0.012)]]),
                                ('paintB', [[lt('ny', -0.7)]])], 'paintA')
    car.add('box', mb)
    mb = MB()
    for x in (-0.95, -0.35, 0.35, 0.95):                                  # lock rods
        mb.boxr((x - 0.018, BY0 - 0.035, BZ0 + 0.10), (x + 0.018, BY0, BZ1 - 0.10), 'black')
        mb.boxr((x - 0.05, BY0 - 0.05, 1.75), (x + 0.05, BY0, 1.83), 'chrome')
    for s in (1, -1):
        for z in (1.35, 2.2, 3.0):                                          # hinges
            mb.boxr((s * 1.10 if s > 0 else -1.21, BY0 - 0.03, z), (1.21 if s > 0 else -1.10, BY0, z + 0.12), 'black')
        mb.boxr((s * 1.02 if s > 0 else -1.18, BY0 - 0.02, 3.18), (1.18 if s > 0 else -1.02, BY0 + 0.02, 3.25), 'tail')
        mb.boxr((s * 1.205 if s > 0 else -1.225, 2.6, 1.30), (1.225 if s > 0 else -1.205, 2.75, 1.36), 'amber')
    mb.boxr((-BX, BY0 - 0.01, BZ0 - 0.06), (BX, BY1, BZ0), 'black')         # box floor rail
    # rear crossmember with lamps and plate, underride bar
    mb.boxr((-1.22, 0.06, 0.72), (1.22, 0.30, 1.02), 'black')
    for s in (1, -1):
        mb.boxr((s * 0.82 if s > 0 else -1.12, 0.03, 0.78), (1.12 if s > 0 else -0.82, 0.07, 0.96), 'tail')
        mb.boxr((s * 0.66 if s > 0 else -0.82, 0.035, 0.78), (0.82 if s > 0 else -0.66, 0.07, 0.96), 'amber')
        mb.boxr((s * 0.82 if s > 0 else -0.86, 0.05, 0.40), (0.86 if s > 0 else -0.82, 0.10, 0.72), 'black')
    mb.boxr((-0.22, 0.03, 0.80), (0.22, 0.07, 0.94), 'plate')
    mb.boxr((-1.05, 0.00, 0.36), (1.05, 0.10, 0.46), 'black')
    # chassis rails, fuel tank
    for s in (1, -1):
        mb.boxr((s * 0.42 if s > 0 else -0.56, 0.10, 0.60), (0.56 if s > 0 else -0.42, 7.20, 0.92), 'under')
    mb.cyl((1.0, 4.4, 0.72), (0, 1, 0), 0.24, 1.0, 'chrome', n=20)
    mb.boxr((-1.2, 3.0, 0.62), (-0.7, 3.8, 1.05), 'under')
    mb.cyl((0, 1.6, 0.50), (1, 0, 0), 0.08, 1.8, 'under', n=16)
    car.add('boxtrim', mb, bevel=0.008)
    # cab: a short body + cabin, built at the origin and shifted forwards
    CY = 5.50
    bk = [
        (0.00, dict(zb=0.60, zs=1.62, zt=1.66, wb=1.08, wc=1.14, ws=1.12, rt=0.06, rb=0.04, zcf=0.5)),
        (1.60, dict(zs=1.60, zt=1.64)),
        (2.00, dict(zs=1.42, zt=1.48, wb=1.04, wc=1.10, ws=1.06, zb=0.62)),
    ]
    ck = [
        (0.04, dict(zr=2.95, wbase=1.10, wroof=1.02, rr=0.10, dz=0.03)),
        (1.10, dict(zr=2.95)),
        (1.30, dict(zr=2.85)),
        (1.80, dict(zr=1.66, wroof=1.06)),
    ]
    b = Body(bk, ck, 2.0, sig={'*': 0.04}, csig=0.05)
    FY, R = 6.45, 0.50
    make_material('truck_cab', [('paintB', [[gt('z', 1.20), lt('z', 1.32), gt('anx', 0.3)]])], 'paintA')
    ws = poly(('ax', 'z'), [(0.0, 1.80), (1.00, 1.80), (0.95, 2.78), (0.0, 2.78)])
    side = poly(('y', 'z'), [(CY + 0.70, 1.78), (CY + 1.62, 1.78), (CY + 1.25, 2.72), (CY + 0.70, 2.72)])
    make_material('truck_cabin', [('glass', [ws + [gt('ny', 0.30)], side + [gt('anx', 0.35)]])], 'paintA')
    body_part(car, b, 2.0, 'truck_cab', 0.05, 0.10, [(FY - CY, (R + 0.05, R))], 0.70, shift=CY)
    cabin_part(car, b, 'truck_cabin', shift=CY)
    add_wheels(car, [(0.80, 1.60, R, 0.23, 'steel', 0.56, (18, 0.012)),
                     (1.04, 1.60, R, 0.23, 'steel', 0.56, (18, 0.012)),
                     (1.00, FY, R, 0.26, 'steel', 0.56, (18, 0.012))])
    mb = MB()
    mb.boxr((-1.12, CY + 1.96, 0.45), (1.12, CY + 2.06, 0.72), 'chrome')
    mb.boxr((-0.80, CY + 1.99, 0.80), (0.80, CY + 2.01, 1.35), 'black')
    for s in (1, -1):
        mb.boxr((s * 0.82 if s > 0 else -1.04, CY + 1.97, 0.95), (1.04 if s > 0 else -0.82, CY + 2.02, 1.15), 'head')
        mb.boxr((s * 1.14 if s > 0 else -1.30, CY + 1.2, 1.9), (1.30 if s > 0 else -1.14, CY + 1.24, 2.4), 'black')
    car.add('cabtrim', mb, bevel=0.01)
    return car


# --- the hearse (added v0.4.12, traffic, far views only) --------------------

def cabin_side_x(b, y, z):
    """outer x of the cabin (greenhouse) at (y, z)"""
    ring = b.cabin_prof(y)
    half = ring[:len(ring) // 2 + 1]
    best = None
    for (x0, z0), (x1, z1) in zip(half[:-1], half[1:]):
        if x0 < 0 or x1 < 0:
            continue
        if (z0 - z) * (z1 - z) <= 0 and abs(z1 - z0) > 1e-6:
            x = x0 + (x1 - x0) * (z - z0) / (z1 - z0)
            best = x if best is None else max(best, x)
    return best if best is not None else max(x for x, _ in half)


def hexa(mb, pts, key):
    """hexahedron from 8 corners ordered like MB.box: for z (lo, hi), for y (lo, hi), for x (lo, hi)"""
    base = len(mb.v)
    for p in pts:
        mb.v.append(Vector(p))
    for q in ((0, 2, 3, 1), (4, 5, 7, 6), (0, 1, 5, 4), (2, 6, 7, 3), (0, 4, 6, 2), (1, 3, 7, 5)):
        mb.face([base + i for i in q], key)


def sweep(mb, path, a, b, key, outward, n=10, cap=True):
    """tube along path (list of Vector) with an elliptic section: half-width a
    across the path in the surface, half-depth b along `outward`"""
    outward = Vector(outward)
    rings = []
    m = len(path)
    for i, p in enumerate(path):
        t = (path[min(i + 1, m - 1)] - path[max(i - 1, 0)]).normalized()
        nrm = (outward - t * outward.dot(t)).normalized()
        bi = t.cross(nrm)
        rings.append([mb.vert(p + bi * a * math.cos(2 * math.pi * k / n) + nrm * b * math.sin(2 * math.pi * k / n))
                      for k in range(n)])
    for j in range(m - 1):
        for k in range(n):
            k2 = (k + 1) % n
            mb.face((rings[j][k], rings[j][k2], rings[j + 1][k2], rings[j + 1][k]), key)
    if cap:
        mb.face(rings[0][::-1], key)
        mb.face(rings[-1], key)


def prism_y(mb, ring, y0, y1, key):
    """a closed prism along Y with the (x, z) ring as its section"""
    a = [mb.vert((x, y0, z)) for x, z in ring]
    b_ = [mb.vert((x, y1, z)) for x, z in ring]
    n = len(ring)
    for i in range(n):
        j = (i + 1) % n
        mb.face((a[i], a[j], b_[j], b_[i]), key)
    mb.face(a[::-1], key)
    mb.face(b_, key)


def drape(mb, o, u, v, nrm, W, H, period, amp, key, seg=12):
    """a hanging curtain: a sheet from o, W along u and H along v, pleated
    (a cosine ripple of the given period and amplitude) along nrm"""
    nu = max(2, int(round(W / period * seg)))
    rows = []
    for j in range(3):
        row = []
        for i in range(nu + 1):
            a = W * i / nu
            d = amp * math.cos(2 * math.pi * a / period) * (1.0 + 0.25 * math.sin(2 * math.pi * a / (period * 3.7)))
            row.append(mb.vert(o + u * a + v * (H * j / 2) + nrm * d))
        rows.append(row)
    for j in range(2):
        for i in range(nu):
            mb.face((rows[j][i], rows[j][i + 1], rows[j + 1][i + 1], rows[j + 1][i]), key)


def bez3(p0, p1, p2, p3, t):
    u = 1 - t
    return tuple(u ** 3 * a + 3 * u * u * t * b_ + 3 * u * t * t * c + t ** 3 * d
                 for a, b_, c, d in zip(p0, p1, p2, p3))


def build_hearse():
    """long 1980s American hearse on a station-wagon body: formal flat roof
    running to the rear door, curtained rear windows, chrome landau bars on
    the blind rear quarters, fender skirts, a thin paint-B pinstripe"""
    car = Car('hearse')
    L = 5.76
    bk = [
        (0.00, dict(zb=0.36, zs=0.92, zt=0.95, wb=0.88, wc=0.995, ws=0.965, rt=0.05, rb=0.04, zcf=0.5)),
        (0.16, dict(zb=0.27)),
        (0.45, dict(wb=0.92, wc=1.025, ws=0.99)),
        (3.95, dict(zs=0.93, zt=0.96)),
        (4.40, dict(zs=0.91, zt=0.945)),
        (5.47, dict(zs=0.86, zt=0.895, zb=0.29)),
        (5.76, dict(zs=0.78, zt=0.82, wb=0.84, wc=0.94, ws=0.90, zb=0.36)),
    ]
    C0 = 0.015                       # the cabin's rear face (the rear door's upper half)
    ck = [
        (C0, dict(zr=1.44, wbase=0.945, wroof=0.885, rr=0.05, dz=0.03)),
        (0.12, dict(zr=1.50)),
        (3.30, dict(zr=1.50)),
        (3.45, dict(zr=1.475)),
        (4.02, dict(zr=0.97, wroof=0.84)),
    ]
    b = Body(bk, ck, L, sig={'*': 0.08, 'zb': 0.03}, csig=0.04)
    RY, FY, R = 1.32, 4.94, 0.36

    # paint B: a thin pinstripe along the flanks, wrapping across the tail
    pin = [gt('z', 0.842), lt('z', 0.874)]
    make_material('hearse_body', [('paintB', [pin + [gt('anx', 0.5)], pin + [lt('ny', -0.5)]])], 'paintA')
    ws = poly(('ax', 'z'), [(0.0, 1.00), (0.78, 1.00), (0.70, 1.45), (0.0, 1.45)])
    front = poly(('y', 'z'), [(2.58, 1.03), (3.86, 1.03), (3.40, 1.43), (2.58, 1.43)])
    make_material('hearse_cabin', [('glass', [ws + [gt('ny', 0.25)], front + [gt('anx', 0.35)]])], 'paintA')

    # rear wheels behind fender skirts: only the front arches are cut
    body_part(car, b, L, 'hearse_body', 0.04, 0.12, [(FY, (R + 0.045, R))], 0.64)
    cab = cabin_part(car, b, 'hearse_cabin')

    # curtained windows: shallow pockets in the greenhouse (the reveal is black
    # trim) with a draped, pleated curtain and a valance inside (interior)
    SW = (1.50, 2.40, 1.05, 1.41)            # rear side window: y0, y1, z0, z1
    RW = (0.52, 1.08, 1.39, 0.03, 0.09)      # rear window: half width, z0, z1, corner radii bottom/top
    D = 0.035
    cmb = MB()
    y0, y1, z0, z1 = SW
    for s in (1, -1):
        pts = []
        for z in (z0, z1):
            for y in (y0, y1):
                xs = cabin_side_x(b, y, z)
                xa, xb = xs - D, 1.4
                pts += [(s * xa, y, z), (s * xb, y, z)] if s > 0 else [(s * xb, y, z), (s * xa, y, z)]
        hexa(cmb, pts, 'black')
    hw, rz0, rz1, rcb, rct = RW
    prism_y(cmb, rrect_ring(-hw, hw, rz0, rz1, rcb, rct, n=6), C0 - 0.4, C0 + D, 'black')
    cut_with(car, cab, cmb, use_self=True)
    if not len(cab.data.vertices):
        raise SystemExit('hearse: the window pockets emptied the greenhouse')

    mb = MB()
    xb0, xb1 = cabin_side_x(b, 2.0, z0), cabin_side_x(b, 2.0, z1)
    lean = Vector((xb1 - xb0, 0.0, z1 - z0))
    ang = math.atan2(xb1 - xb0, z1 - z0)
    for s in (1, -1):
        up = Vector((s * lean.x, 0, lean.z)).normalized()
        drape(mb, Vector((s * (xb0 - D + 0.014), y0 - 0.02, z0 - 0.02)), Vector((0, 1, 0)), up,
              Vector((s, 0, 0)), y1 - y0 + 0.04, lean.length + 0.04, 0.075, 0.009, 'interior')
        zv = z1 - 0.03
        xv = xb0 + (xb1 - xb0) * (zv - z0) / (z1 - z0) - D + 0.014
        mb.box((s * xv, (y0 + y1) / 2, zv), (0.03, y1 - y0, 0.07), 'interior',
               rot=Matrix.Rotation(s * ang, 3, 'Y'))
    drape(mb, Vector((-hw - 0.02, C0 + D - 0.014, rz0 - 0.02)), Vector((1, 0, 0)), Vector((0, 0, 1)),
          Vector((0, -1, 0)), 2 * hw + 0.04, rz1 - rz0 + 0.04, 0.075, 0.009, 'interior')
    mb.boxr((-hw, C0 + D - 0.03, rz1 - 0.07), (hw, C0 + D, rz1), 'interior')
    car.add('curtains', mb)

    # chrome: window surrounds, landau bars, drip rails, belt mouldings
    mb = MB()
    t = 0.024
    ring = rrect_ring(-hw - t / 2, hw + t / 2, rz0 - t / 2, rz1 + t / 2, rcb + t / 2, rct + t / 2, n=6)
    ring = [Vector((x, C0 - 0.002, z)) for x, z in ring]
    sweep(mb, ring + [ring[0], ring[1]], t / 2, 0.009, 'chrome', (0, -1, 0), n=6)
    for s in (1, -1):
        # side window surround: a flat ring on the greenhouse's surface
        def on_side(y, z, dx=0.004):
            return Vector((s * (cabin_side_x(b, y, z) + dx), y, z))
        frame = [on_side(y0 - t / 2, z0 - t / 2), on_side(y1 + t / 2, z0 - t / 2), on_side(y1 + t / 2, z1 + t / 2),
                 on_side(y0 - t / 2, z1 + t / 2), on_side(y0 - t / 2, z0 - t / 2)]
        for p, q in zip(frame[:-1], frame[1:]):
            k = 6
            sweep(mb, [p.lerp(q, i / k) for i in range(k + 1)], t / 2, 0.009, 'chrome', (s, 0, 0), n=6)
        # the landau bar: a fat chrome S across the blind rear quarter, scroll
        # bosses at both ends (arcade-sized: it is what makes the flank read)
        P = [(0.27, 1.08), (0.98, 0.98), (0.68, 1.45), (1.33, 1.37)]
        path = []
        for i in range(33):
            y, z = bez3(*P, i / 32)
            path.append(Vector((s * (cabin_side_x(b, y, z) + 0.010), y, z)))
        sweep(mb, path, 0.056, 0.036, 'chrome', (s, 0, 0), n=12)
        for (y, z) in (P[0], P[3]):
            xs = cabin_side_x(b, y, z)
            mb.cyl((s * (xs + 0.016), y, z), (1, 0, 0), 0.078, 0.04, 'chrome', n=20)
        # drip rail along the whole roof edge (makes the long roofline read)
        rail = []
        k = NB + NF1 + NS + NF2 // 2
        for i in range(41):
            y = 0.08 + (3.36 - 0.08) * i / 40
            x, z = b.cabin_prof(y)[k]
            rail.append(Vector((s * (x + 0.010), y, z + 0.004)))
        sweep(mb, rail, 0.022, 0.022, 'chrome', (s, 0, 0), n=8)
        # belt moulding on the body's shoulder, rear to cowl
        belt = []
        for i in range(41):
            y = 0.06 + (3.98 - 0.06) * i / 40
            x, z = half_profile(b.prof(y))[k]
            belt.append(Vector((s * (x + 0.008), y, z + 0.004)))
        sweep(mb, belt, 0.017, 0.017, 'chrome', (s, 0, 0), n=8)
    car.add('chrome', mb)

    add_wheels(car, [(0.785, RY, R, 0.23, 'turbine', 0.60, None), (0.82, FY, R, 0.23, 'turbine', 0.60, None)])
    add_wells(car, [(FY, R + 0.04, 0.68, 0.18, R)])

    mb = MB()
    # tall tail lamps on the rear corners in chrome bezels, plate, handle, bumper
    sym_boxr(mb, (0.80, -0.016, 0.47), (0.978, 0.07, 0.90), 'tail')
    sym_boxr(mb, (0.78, -0.006, 0.45), (0.988, 0.07, 0.92), 'chrome')
    mb.boxr((-0.22, -0.014, 0.52), (0.22, 0.04, 0.64), 'plate')
    mb.boxr((-0.245, -0.006, 0.50), (0.245, 0.04, 0.66), 'chrome')
    mb.boxr((-0.12, -0.016, 0.80), (0.12, 0.03, 0.83), 'chrome')                    # door handle
    mb.boxr((-0.70, -0.004, 0.93), (0.70, 0.03, 0.945), 'black')                    # door seam
    mb.boxr((-1.01, -0.085, 0.24), (1.01, 0.16, 0.45), 'chrome')
    mb.boxr((-0.97, -0.098, 0.325), (0.97, -0.07, 0.365), 'black')                 # rub strip
    for s in (1, -1):
        mb.box((s * 0.985, 0.13, 0.345), (0.07, 0.30, 0.21), 'chrome', rot=Matrix.Rotation(math.radians(-s * 22), 3, 'Z'))
        mb.box((s * 1.005, 0.36, 0.66), (0.03, 0.12, 0.05), 'tail')                 # side marker
        mb.box((s * 1.005, L - 0.40, 0.64), (0.03, 0.12, 0.05), 'amber')
    mb.cyl((-0.55, 0.15, 0.22), (0, 1, 0), 0.04, 0.36, 'under', n=16)
    # front: chrome bumper, chrome grille on black, quad headlamps
    mb.boxr((-0.97, L - 0.12, 0.26), (0.97, L + 0.07, 0.47), 'chrome')
    mb.boxr((-0.40, L - 0.05, 0.53), (0.40, L + 0.005, 0.82), 'black')
    for i in range(7):
        x = -0.36 + 0.12 * i
        mb.boxr((x - 0.012, L - 0.04, 0.54), (x + 0.012, L + 0.02, 0.81), 'chrome')
    mb.boxr((-0.42, L - 0.03, 0.80), (0.42, L + 0.025, 0.84), 'chrome')
    for s in (1, -1):
        mb.boxr((s * 0.46 if s > 0 else -0.86, L - 0.06, 0.62), (0.86 if s > 0 else -0.46, L + 0.01, 0.76), 'head')
        mb.boxr((s * 0.46 if s > 0 else -0.86, L - 0.06, 0.53), (0.86 if s > 0 else -0.46, L + 0.005, 0.59), 'amber')
    mirrors(mb, b, 3.66, 1.06, 'chrome')
    car.add('trim', mb, bevel=0.008)
    return car


BUILDERS = {'wedge': build_wedge, 'muscle': build_muscle, 'rally': build_rally, 'pickup': build_pickup,
            'sedan': build_sedan, 'compact': build_compact, 'van': build_van, 'truck': build_truck,
            'hearse': build_hearse}
PLAYER = ['wedge', 'muscle', 'rally', 'pickup']
TRAFFIC = ['sedan', 'compact', 'van', 'truck', 'hearse']


# ---------------------------------------------------------------------------
# Scene
# ---------------------------------------------------------------------------

def setup_prefs(cpu=False):
    sc = bpy.context.scene
    sc.render.engine = 'CYCLES'
    ok = False
    if not cpu:
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
    print('Cycles device:', sc.cycles.device)


def sun_dir():
    el, az = math.radians(SUN_EL), math.radians(SUN_AZ)
    return V(math.cos(el) * math.cos(az), math.cos(el) * math.sin(az), math.sin(el))


def setup_scene():
    sc = bpy.context.scene
    r = sc.render
    r.film_transparent = True
    r.dither_intensity = 0.0
    r.image_settings.file_format = 'OPEN_EXR'
    r.image_settings.color_mode = 'RGBA'
    r.image_settings.color_depth = '32'
    r.image_settings.exr_codec = 'ZIP'
    sc.view_settings.view_transform = 'Standard'
    sc.view_settings.look = 'None'
    sc.view_settings.exposure = 0.0
    sc.view_settings.gamma = 1.0
    sc.cycles.use_adaptive_sampling = False
    sc.cycles.seed = 7
    w = bpy.data.worlds.new('sky')
    sc.world = w
    w.use_nodes = True
    nt = w.node_tree
    bg = nt.nodes['Background']
    bg.inputs['Strength'].default_value = SKY_STRENGTH
    # soft gradient: bright hazy horizon, deeper blue zenith (gives the gloss
    # something to reflect); same average as a flat sky of colour SKY
    tc = nt.nodes.new('ShaderNodeTexCoord')
    sp = nt.nodes.new('ShaderNodeSeparateXYZ')
    nt.links.new(tc.outputs['Generated'], sp.inputs[0])
    ramp = nt.nodes.new('ShaderNodeValToRGB')
    nt.links.new(_math(nt, 'POWER', _math(nt, 'MAXIMUM', sp.outputs[2], 0.0), 0.5), ramp.inputs[0])
    ramp.color_ramp.elements[0].color = (1.70, 1.70, 1.70, 1)
    ramp.color_ramp.elements[1].position = 0.55
    ramp.color_ramp.elements[1].color = (0.50, 0.64, 1.00, 1)
    nt.links.new(ramp.outputs[0], bg.inputs['Color'])
    sun = bpy.data.objects.new('sun', bpy.data.lights.new('sun', 'SUN'))
    sc.collection.objects.link(sun)
    sun.rotation_euler = (-sun_dir()).to_track_quat('-Z', 'Y').to_euler()
    sun.data.energy = SUN_ENERGY
    sun.data.angle = math.radians(4.0)
    sun.data.color = (1.0, 0.96, 0.90)
    cam = bpy.data.objects.new('cam', bpy.data.cameras.new('cam'))
    sc.collection.objects.link(cam)
    cam.rotation_euler = (math.radians(90), 0, 0)
    cam.data.sensor_fit = 'HORIZONTAL'
    cam.data.sensor_width = 36.0
    cam.data.clip_start = 0.05
    cam.data.clip_end = 500
    sc.camera = cam
    me = bpy.data.meshes.new('ground')
    s = 60
    me.from_pydata([(-s, -s, 0), (s, -s, 0), (s, s, 0), (-s, s, 0)], [], [(0, 1, 2, 3)])
    g = bpy.data.objects.new('ground', me)
    sc.collection.objects.link(g)
    me.materials.append(MATS['ground'])
    return cam, g


def set_camera(sc, cam, pos, W, H, f, pp):
    """pinhole, no pitch; f in px; pp = principal point in px from the top-left"""
    sc.render.resolution_x, sc.render.resolution_y, sc.render.resolution_percentage = W, H, 100
    cam.location = pos
    cam.data.lens = 36.0 * f / W
    # Blender's shift is in units of the sensor-fit side (the WIDTH here, fit
    # HORIZONTAL) and a positive shift_y moves the image content DOWN
    cam.data.shift_x = (W / 2 - pp[0]) / W
    cam.data.shift_y = -(H / 2 - pp[1]) / W
    bpy.context.view_layer.update()


def project(sc, cam, p):
    co = world_to_camera_view(sc, cam, Vector(p))
    W, H = sc.render.resolution_x, sc.render.resolution_y
    return [round(co.x * W, 2), round((1 - co.y) * H, 2)]


# ---------------------------------------------------------------------------
# Output helpers (as golfer.py)
# ---------------------------------------------------------------------------

def write_png(path, arr):
    arr = np.ascontiguousarray(arr.astype(np.uint8))
    h, w = arr.shape[:2]
    ch = 1 if arr.ndim == 2 else arr.shape[2]
    ctype = {1: 0, 2: 4, 3: 2, 4: 6}[ch]
    raw = b''.join(b'\x00' + arr[y].tobytes() for y in range(h))

    def chunk(t, d):
        return struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t + d) & 0xffffffff)
    with open(path, 'wb') as f:
        f.write(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, ctype, 0, 0, 0)) +
                chunk(b'IDAT', zlib.compress(raw, 9)) + chunk(b'IEND', b''))


def read_exr(path):
    img = bpy.data.images.load(path, check_existing=False)
    w, h = img.size
    a = np.empty(w * h * 4, np.float32)
    img.pixels.foreach_get(a)
    bpy.data.images.remove(img)
    return a.reshape(h, w, 4)[::-1].copy()


def srgb(x):
    x = np.clip(x, 0, 1)
    return np.where(x <= 0.0031308, x * 12.92, 1.055 * np.power(x, 1 / 2.4) - 0.055)


def to_shade(a):
    al = a[..., 3]
    rgb = a[..., :3] / np.maximum(al[..., None], 1e-6)
    out = np.zeros(a.shape, np.uint8)
    out[..., :3] = np.round(srgb(rgb) * 255)
    out[..., 3] = np.round(np.clip(al, 0, 1) * 255)
    out[out[..., 3] == 0] = 0
    return out


def to_id(a, cover_alpha):
    al = a[..., 3]
    v = a[..., 0] / np.maximum(al, 1e-6)
    ids = np.where(al > 0.5, np.round(v * 16), 0).astype(np.int32)
    ids = np.clip(ids, 0, 15)
    need = (cover_alpha > 0) & (ids == 0)
    it = 0
    while need.any() and it < 12:
        best = np.zeros_like(ids)
        for dy, dx in ((0, 1), (0, -1), (1, 0), (-1, 0), (1, 1), (1, -1), (-1, 1), (-1, -1)):
            sh = np.roll(np.roll(ids, dy, 0), dx, 1)
            best = np.where((best == 0) & (sh > 0), sh, best)
        ids = np.where(need & (best > 0), best, ids)
        need = (cover_alpha > 0) & (ids == 0)
        it += 1
    ids[cover_alpha == 0] = 0
    return ids


def set_vis(objs, camera=True):
    for o in objs:
        o.hide_render = False
        o.visible_camera = camera


def render_exr(sc, path, samples, filt, denoise, bounces):
    sc.cycles.samples = samples
    sc.cycles.use_denoising = denoise
    if denoise:
        sc.cycles.denoiser = 'OPENIMAGEDENOISE'
    sc.cycles.pixel_filter_type = 'BLACKMAN_HARRIS' if filt > 0.5 else 'GAUSSIAN'
    sc.cycles.filter_width = filt
    sc.cycles.max_bounces = bounces
    sc.cycles.diffuse_bounces = min(bounces, 3)
    sc.cycles.glossy_bounces = min(bounces, 3)
    sc.cycles.transmission_bounces = 0
    sc.cycles.transparent_max_bounces = 2
    sc.render.filepath = path
    bpy.ops.render.render(write_still=True)
    return read_exr(path)


def render_passes(sc, car, ground, base, tmp, args):
    objs = car.objs
    set_vis(objs)
    ground.hide_render = False
    ground.visible_camera = False
    ground.is_shadow_catcher = False
    set_pass_materials(objs, 'shade')
    a = render_exr(sc, os.path.join(tmp, 'shade.exr'), args.samples, 1.5, True, 6)
    shade = to_shade(a)
    write_png(base + '_shade.png', shade)
    ground.hide_render = True
    set_pass_materials(objs, 'id')
    a = render_exr(sc, os.path.join(tmp, 'id.exr'), 1, 0.01, False, 0)
    ids = to_id(a, shade[..., 3])
    write_png(base + '_id.png', (ids * 16).astype(np.uint8))
    bad = int(((shade[..., 3] > 0) & (ids == 0)).sum())
    car.last_counts = {int(i): int((ids == i).sum()) for i in np.unique(ids) if i}
    if not args.noshadow:
        set_pass_materials(objs, 'shade')
        set_vis(objs, camera=False)
        ground.hide_render = False
        ground.visible_camera = True
        ground.is_shadow_catcher = True
        a = render_exr(sc, os.path.join(tmp, 'shadow.exr'), max(64, args.samples), 1.5, True, 3)
        sh = np.round(np.clip(a[..., 3], 0, 1) * 255).astype(np.uint8)
        write_png(base + '_shadow.png', sh)
        ground.is_shadow_catcher = False
        set_vis(objs)
    ys, xs = np.nonzero(shade[..., 3] > 0)
    bbox = [int(xs.min()), int(ys.min()), int(xs.max()), int(ys.max())] if len(xs) else None
    return bbox, bad


# ---------------------------------------------------------------------------
# Far framing
# ---------------------------------------------------------------------------

def far_frame(car, margin=6):
    """image size and principal points for the three far views of a car"""
    L, Wd, Hh = car.dims
    x0, x1 = float(car.lo[0]), float(car.hi[0])
    pts = []
    for x in (x0, x1):
        for y in (0.0, L):
            for z in (0.0, Hh):
                pts.append((x, FAR_Y + y, z))
    # shadow reach: sun from behind-left, shadow falls forward-right
    d = sun_dir()
    k = Hh / d.z
    for x in (x0, x1):
        for y in (0.0, L):
            pts.append((x - d.x * k, FAR_Y + y - d.y * k, 0.0))
    ext = {}
    for v, cx in FAR_VIEWS.items():
        us = [FAR_F * (p[0] - cx) / p[1] for p in pts]
        vs = [FAR_F * (CAM_Z - p[2]) / p[1] for p in pts]
        ext[v] = (min(us), max(us), min(vs), max(vs))
    W = max(e[1] - e[0] for e in ext.values()) + 2 * margin
    H = max(e[3] - e[2] for e in ext.values()) + 2 * margin
    W, H = int(math.ceil(W / 8) * 8), int(math.ceil(H / 8) * 8)
    pps = {}
    for v, (u0, u1, v0, v1) in ext.items():
        ppx = round(W / 2 - (u0 + u1) / 2)
        ppy = round(H / 2 - (v0 + v1) / 2)
        pps[v] = (float(ppx), float(ppy))
    return W, H, pps


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def studio(sc, cam, ground, cars, d, tmp, args):
    """debug views: side, rear 3/4 high, front 3/4, top; shade + id false colour"""
    os.makedirs(d, exist_ok=True)
    qa = np.array([[0, 0, 0], [255, 64, 64], [64, 160, 255], [128, 224, 255], [255, 255, 255], [48, 48, 48],
                   [110, 110, 110], [192, 192, 64], [255, 0, 255], [255, 255, 128], [240, 240, 240],
                   [128, 64, 0], [20, 20, 20], [255, 160, 0], [0, 192, 96], [255, 0, 255]], np.uint8)
    cam.data.shift_x = cam.data.shift_y = 0
    for k, car in cars.items():
        car.hide(False)
        car.place(0.0, 0.0)
        L, Wd, Hh = car.dims
        c = Vector((0, L / 2, Hh * 0.45))
        views = {'side': Vector((9, L / 2, 0.9)), 'rear34': Vector((-4.5, -5.5, 2.6)),
                 'front34': Vector((4.8, L + 5.0, 1.8)), 'top': Vector((0.01, L / 2, 11))}
        tiles = []
        for vn, pos in views.items():
            sc.render.resolution_x, sc.render.resolution_y = 480, 300
            cam.location = pos
            cam.rotation_euler = (c - pos).to_track_quat('-Z', 'Y').to_euler()
            cam.data.lens = 36.0 * 480 / (L * 1.25) / (c - pos).length * 1.0
            cam.data.lens = 36.0 / (L * 1.25) * (c - pos).length
            bpy.context.view_layer.update()
            set_pass_materials(car.objs, 'shade')
            ground.hide_render = False
            ground.visible_camera = False
            a = render_exr(sc, os.path.join(tmp, 's.exr'), 24, 1.5, True, 6)
            sh = to_shade(a)
            al = sh[..., 3:4] / 255.0
            img = (np.full(sh.shape[:2] + (3,), 100.0) * (1 - al) + sh[..., :3] * al).astype(np.uint8)
            ground.hide_render = True
            set_pass_materials(car.objs, 'id')
            a = render_exr(sc, os.path.join(tmp, 'i.exr'), 1, 0.01, False, 0)
            ids = to_id(a, sh[..., 3])
            tiles.append(np.concatenate([img, qa[ids]], 1))
        write_png(os.path.join(d, 'studio_%s.png' % k), np.concatenate(tiles, 0))
        print('studio', k)
        car.hide(True)
    cam.rotation_euler = (math.radians(90), 0, 0)


def check_near_camera(sc, cam):
    set_camera(sc, cam, (0, 0, CAM_Z), NEAR_W, NEAR_H, NEAR_F, NEAR_PP)
    checks = [((0, 50, 2.0), (184, 150)), ((0.95, 3.2, 0), (273, 337.5)), ((0, 3.2, 0), (184, 337.5))]
    ok = True
    for p, want in checks:
        got = project(sc, cam, p)
        good = abs(got[0] - want[0]) < 0.1 and abs(got[1] - want[1]) < 0.1
        ok = ok and good
        print('camera check %s -> (%.2f, %.2f) want %s %s' % (p, got[0], got[1], want, 'OK' if good else 'FAIL'))
    if not ok:
        raise SystemExit('near camera check failed')


def main():
    argv = sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else []
    ap = argparse.ArgumentParser()
    ap.add_argument('--out', default=os.path.join(HERE, '..', '..', 'assets', 'cars'))
    ap.add_argument('--cars', default='')
    ap.add_argument('--near', default='0,1,2,3,4,5,6')
    ap.add_argument('--nonear', action='store_true')
    ap.add_argument('--far', default='l,c,r')
    ap.add_argument('--nofar', action='store_true')
    ap.add_argument('--samples', type=int, default=64)
    ap.add_argument('--noshadow', action='store_true')
    ap.add_argument('--cpu', action='store_true')
    ap.add_argument('--calib', action='store_true')
    ap.add_argument('--studio', default='', help='debug: side / 3-4 views of each car into this dir')
    args = ap.parse_args(argv)
    out = os.path.abspath(args.out)
    os.makedirs(out, exist_ok=True)
    tmp = os.path.join(out, '_tmp')
    os.makedirs(tmp, exist_ok=True)
    t_all = time.time()

    bpy.ops.wm.read_factory_settings(use_empty=True)
    setup_prefs(args.cpu)
    make_base_materials()
    cam, ground = setup_scene()
    sc = bpy.context.scene
    check_near_camera(sc, cam)

    if args.calib:
        # a matte grey sphere: its brightest point faces the sun
        mb = MB()
        me = bpy.data.meshes.new('calib')
        bm = bmesh.new()
        bmesh.ops.create_uvsphere(bm, u_segments=64, v_segments=32, radius=0.5)
        bm.to_mesh(me)
        bm.free()
        for p in me.polygons:
            p.use_smooth = True
        ob = bpy.data.objects.new('calib', me)
        sc.collection.objects.link(ob)
        me.materials.append(MATS['calib'])
        ob.location = (0, 5, 0.5)
        ground.hide_render = False
        ground.visible_camera = False
        a = render_exr(sc, os.path.join(tmp, 'calib.exr'), 64, 1.5, True, 4)
        s = to_shade(a)
        m = s[..., 3] == 255
        print('CALIB sphere max %d  p99 %d' % (s[..., 0][m].max(), np.percentile(s[..., 0][m], 99)))
        return

    keys = args.cars.split(',') if args.cars else [k for k in PLAYER + TRAFFIC if k in BUILDERS]
    meta_path = os.path.join(out, 'meta.json')
    meta = json.load(open(meta_path)) if os.path.exists(meta_path) else {}
    meta.update({
        'note': 'Rendered by tools/blender/cars.py. colour = palette[id] * shade / shade_ref; shade RGB is '
                'lit neutral grey (0.8 albedo), sRGB, Standard view transform; id png = id * 16; shadow = '
                'how much the road is darkened (0..255).',
        'shade_ref': 196,
        'ids': {str(v): k for k, v in REGION.items()},
        'near_camera': {'position': [0, 0, CAM_Z], 'f_px': NEAR_F, 'pp': list(NEAR_PP),
                        'size': [NEAR_W, NEAR_H], 'rear_y': NEAR_REAR_Y, 'yaw_deg': YAWS},
        'far_camera': {'z': CAM_Z, 'f_px': FAR_F, 'rear_y': FAR_Y, 'x': FAR_VIEWS, 'ppm': FAR_F / FAR_Y},
        'sun': {'elevation_deg': SUN_EL, 'azimuth_deg': SUN_AZ, 'energy': SUN_ENERGY},
    })
    meta.setdefault('vehicles', {})
    meta.setdefault('renders', {})
    near_frames = [] if args.nonear else [int(x) for x in args.near.split(',') if x != '']
    far_views = [] if args.nofar else [v for v in args.far.split(',') if v]

    cars = {}
    done = set()
    for k in keys:
        t0 = time.time()
        car = BUILDERS[k]()
        car.finish()
        car.hide(True)
        cars[k] = car
        meta['vehicles'][k] = {'length': round(car.dims[0], 3), 'width': round(car.dims[1], 3),
                               'height': round(car.dims[2], 3), 'player': k in PLAYER}
        print('built %s in %.1fs' % (k, time.time() - t0))

    if args.studio:
        studio(sc, cam, ground, cars, os.path.abspath(args.studio), tmp, args)
        return

    for k, car in cars.items():
        car.hide(False)
        if k in PLAYER:
            for fi in near_frames:
                t0 = time.time()
                set_camera(sc, cam, (0, 0, CAM_Z), NEAR_W, NEAR_H, NEAR_F, NEAR_PP)
                car.place(NEAR_REAR_Y, YAWS[fi])
                name = 'near_%s_y%d' % (k, fi)
                bbox, bad = render_passes(sc, car, ground, os.path.join(out, name), tmp, args)
                anchor = project(sc, cam, (0, NEAR_REAR_Y, 0))
                meta['renders'][name] = {'size': [NEAR_W, NEAR_H], 'anchor': anchor, 'yaw_deg': YAWS[fi],
                                         'bbox': bbox}
                done.add(name)
                print('%s: anchor %s bbox %s (w %d px) uncovered %d  id2 %d px  %.1fs' % (
                    name, anchor, bbox, bbox[2] - bbox[0] + 1 if bbox else 0, bad, car.last_counts.get(2, 0),
                    time.time() - t0))
        if far_views:
            W, H, pps = far_frame(car)
            for v in far_views:
                t0 = time.time()
                cx = FAR_VIEWS[v]
                set_camera(sc, cam, (cx, 0, CAM_Z), W, H, FAR_F, pps[v])
                car.place(FAR_Y, 0.0)
                name = 'far_%s_%s' % (k, v)
                bbox, bad = render_passes(sc, car, ground, os.path.join(out, name), tmp, args)
                anchor = project(sc, cam, (0, FAR_Y, 0))
                want = (pps[v][0] + FAR_F * (0 - cx) / FAR_Y, pps[v][1] + FAR_F * CAM_Z / FAR_Y)
                meta['renders'][name] = {'size': [W, H], 'anchor': anchor, 'ppm': FAR_F / FAR_Y, 'bbox': bbox,
                                         'camera_x': cx}
                done.add(name)
                ok = abs(anchor[0] - want[0]) < 0.1 and abs(anchor[1] - want[1]) < 0.1
                print('%s: %dx%d anchor %s (expected %s: %s) bbox %s uncovered %d  id2 %d px  %.1fs' % (
                    name, W, H, anchor, want, 'OK' if ok else 'FAIL', bbox, bad, car.last_counts.get(2, 0),
                    time.time() - t0))
        car.hide(True)
    # merge into the file as it is on disk NOW: only the vehicles built and the
    # renders made by this run are added/replaced, every other entry stays as is
    cur = json.load(open(meta_path)) if os.path.exists(meta_path) else {}
    for key, val in meta.items():
        if key == 'vehicles':
            cur.setdefault(key, {}).update({k: val[k] for k in cars})
        elif key == 'renders':
            cur.setdefault(key, {}).update({n: val[n] for n in sorted(done)})
        else:
            cur[key] = val              # the header: camera, sun, ids (constants)
    with open(meta_path, 'w') as f:
        json.dump(cur, f, indent=1)
    import shutil
    shutil.rmtree(tmp, ignore_errors=True)
    print('done in %.1fs' % (time.time() - t_all))


if __name__ == '__main__':
    main()
