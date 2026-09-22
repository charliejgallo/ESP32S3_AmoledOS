"""chars.py - Tommy, his layers (caps, back items, hand items) and his pets,
modelled, posed and rendered procedurally for Monster Hop. See SPEC.md s.4.

    Blender -b -P chars.py -- --out ../../assets/chars [--style-sample] [--only a,b]
                              [--samples N] [--list] [--cpu]

  --style-sample  only the phase-1 style sample (idle 00 x 4 facings, hop s/e,
             the default cap on all of them, the dog's idle s 00)
  --only     comma-separated name patterns (fnmatch, or a plain prefix):
             `--only tommy_hop_e,cap_cap_hop_e_03,pet_dog*`
  --list     print the names the run would render and quit

How it works
  * Tommy is built from scratch for every frame from a pose (a dict of a few
    angles and offsets): rigid parts (head with its face, hair, shirt, jeans
    pelvis, hands, shoes) are meshes in their bone's frame; limbs are smooth
    tubes swept through shoulder-elbow-wrist and hip-knee-ankle (a rubber-hose
    look that has no joints to show at 50 px). Legs are two-bone IK to foot
    targets, feet can be glued to the ground.
  * The face is decals: thin patches laid 2-6 mm above the analytic head
    surface (eye whites, pupils, catchlights, brows, mouth, blush), so every
    region is a clean id with the shading coming only from the light pass.
  * Layers (caps, ...) are built in the head's (or hand's, or chest's) frame
    of the same pose, so they follow it exactly, and are rendered with every
    body mesh as hold-out.
  * Every character material darkens a little at grazing angles (a soft
    contour inside the silhouette, it separates arm from torso and Tommy
    from the ground at this size); the watch's palette multiplies it.
"""
import fnmatch
import json
import math
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import mh_common as C  # noqa: E402
import bpy  # noqa: E402
import bmesh  # noqa: E402
from mathutils import Matrix, Vector  # noqa: E402
from mathutils.bvhtree import BVHTree  # noqa: E402


def V(*a):
    return Vector(a if len(a) != 1 else a[0])


def clamp(x, a, b):
    return a if x < a else b if x > b else x


def smoothstep(e0, e1, x):
    t = clamp((x - e0) / (e1 - e0), 0.0, 1.0)
    return t * t * (3 - 2 * t)


def lerp(a, b, t):
    return a + (b - a) * t


def interp(table, x):
    """Piecewise-linear lookup in [(x, y), ...] (sorted by x)."""
    if x <= table[0][0]:
        return table[0][1]
    for (x0, y0), (x1, y1) in zip(table, table[1:]):
        if x <= x1:
            return y0 + (y1 - y0) * (x - x0) / (x1 - x0)
    return table[-1][1]


def interp_rows(table, x):
    """Rows (x, a, b, c...) -> interpolated (a, b, c...)."""
    if x <= table[0][0]:
        return table[0][1:]
    for r0, r1 in zip(table, table[1:]):
        if x <= r1[0]:
            t = (x - r0[0]) / (r1[0] - r0[0])
            return tuple(a + (b - a) * t for a, b in zip(r0[1:], r1[1:]))
    return table[-1][1:]


def RX(d):
    return Matrix.Rotation(math.radians(d), 4, 'X')


def RY(d):
    return Matrix.Rotation(math.radians(d), 4, 'Y')


def RZ(d):
    return Matrix.Rotation(math.radians(d), 4, 'Z')


def T(x, y=None, z=None):
    if y is None:
        return Matrix.Translation(Vector(x))
    return Matrix.Translation((x, y, z))


def frame_z(z, xhint):
    """3x3 with Z along z and X as close as possible to xhint."""
    z = z.normalized()
    x = xhint - z * xhint.dot(z)
    if x.length < 1e-6:
        x = V(1, 0, 0) - z * z.x
        if x.length < 1e-6:
            x = V(0, 1, 0) - z * z.y
    x.normalize()
    y = z.cross(x)
    return Matrix((x, y, z)).transposed()


def ik2(a, target, l1, l2, pole):
    """Two-bone IK: returns (mid joint, end)."""
    d = target - a
    dist = d.length
    dn = d.normalized() if dist > 1e-9 else V(0, 0, -1)
    dist_c = clamp(dist, abs(l1 - l2) + 1e-4, (l1 + l2) * 0.9995)
    cos_a = (l1 * l1 + dist_c * dist_c - l2 * l2) / (2 * l1 * dist_c)
    sin_a = math.sqrt(max(0.0, 1 - cos_a * cos_a))
    pp = pole - dn * pole.dot(dn)
    if pp.length < 1e-6:
        pp = V(0, -1, 0) - dn * (-dn.y)
    pp.normalize()
    return a + dn * (cos_a * l1) + pp * (sin_a * l1), a + dn * dist_c


# ---------------------------------------------------------------------------
# Mesh building
# ---------------------------------------------------------------------------

class MB:
    """Vertices, faces and a material key per face."""

    def __init__(self):
        self.v, self.f, self.fk = [], [], []

    def vert(self, p):
        self.v.append(Vector(p))
        return len(self.v) - 1

    def face(self, idx, key):
        self.f.append(tuple(idx))
        self.fk.append(key)


_OBJS = []   # every object made since the last clear()


def to_object(name, mb, M=None, smooth=True, recalc=True):
    me = bpy.data.meshes.new(name)
    me.from_pydata([tuple(v) for v in mb.v], [], mb.f)
    me.update()
    ob = bpy.data.objects.new(name, me)
    C.link(ob)
    keys = []
    for k in mb.fk:
        if k not in keys:
            keys.append(k)
    for k in keys:
        C.assign(ob, k)
    me.polygons.foreach_set('material_index', [keys.index(k) for k in mb.fk])
    me.polygons.foreach_set('use_smooth', [smooth] * len(mb.f))
    if recalc:
        bm = bmesh.new()
        bm.from_mesh(me)
        bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
        bm.to_mesh(me)
        bm.free()
    me.update()
    if M is not None:
        ob.matrix_world = M
    _OBJS.append(ob)
    return ob


def clear():
    for ob in _OBJS:
        me = ob.data
        bpy.data.objects.remove(ob, do_unlink=True)
        if me is not None and me.users == 0:
            bpy.data.meshes.remove(me)
    _OBJS.clear()


def grid_faces(mb, rows, key_fn, closed=True):
    """Quads between consecutive rows of vertex indices (same length)."""
    n = len(rows[0])
    kk = range(n) if closed else range(n - 1)
    for i in range(len(rows) - 1):
        for k in kk:
            k1 = (k + 1) % n
            a, b, c, d = rows[i][k], rows[i][k1], rows[i + 1][k1], rows[i + 1][k]
            cen = (mb.v[a] + mb.v[b] + mb.v[c] + mb.v[d]) / 4
            mb.face((a, b, c, d), key_fn(i, k, cen))


def fan(mb, centre, row, key, flip=False):
    n = len(row)
    for k in range(n):
        k1 = (k + 1) % n
        mb.face((centre, row[k1], row[k]) if not flip else (centre, row[k], row[k1]), key)


def ring_pts(c, xa, ya, xp, xn, yp, yn, n, e=2.0, th0=0.0):
    """A superellipse ring round c in the plane (xa, ya), different extents
    on each side."""
    pts = []
    for k in range(n):
        th = th0 + 2 * math.pi * k / n
        cs, sn = math.cos(th), math.sin(th)
        ex = 2.0 / e
        x = math.copysign(abs(cs) ** ex, cs) * (xp if cs > 0 else xn)
        y = math.copysign(abs(sn) ** ex, sn) * (yp if sn > 0 else yn)
        pts.append(c + xa * x + ya * y)
    return pts


def loft(mb, rings, key_fn, cap0=None, cap1=None, cap_keys=(None, None)):
    """rings: lists of points (same n). key_fn(i, k, centre)."""
    rows = [[mb.vert(p) for p in r] for r in rings]
    grid_faces(mb, rows, key_fn)
    for cap, row, ck in ((cap0, rows[0], cap_keys[0]), (cap1, rows[-1], cap_keys[1])):
        if cap is None:
            continue
        c = mb.vert(cap)
        fan(mb, c, row, ck or key_fn(0, 0, Vector(cap)))
    return rows


def ellipsoid(mb, c, r, key, rot=None, n=20, rings=12):
    rot = (rot or Matrix.Identity(3)).to_3x3()
    c = Vector(c)
    key_fn = key if callable(key) else (lambda p, _k=key: _k)
    top = mb.vert(c + rot @ V(0, 0, r[2]))
    bot = mb.vert(c + rot @ V(0, 0, -r[2]))
    rows = []
    for i in range(1, rings):
        ph = math.pi * i / rings
        z, s = math.cos(ph), math.sin(ph)
        rows.append([mb.vert(c + rot @ V(s * math.cos(2 * math.pi * k / n) * r[0],
                                            s * math.sin(2 * math.pi * k / n) * r[1], z * r[2]))
                     for k in range(n)])
    grid_faces(mb, rows, lambda i, k, cen: key_fn(cen))
    fan(mb, top, rows[0], key_fn(c + rot @ V(0, 0, r[2])))
    fan(mb, bot, rows[-1], key_fn(c + rot @ V(0, 0, -r[2])), flip=True)


def catmull(ctrl, per=6):
    """Catmull-Rom through ctrl (end tangents mirrored); returns (points, t)
    with t = control index as a float, for interpolating radii."""
    ctrl = [Vector(p) for p in ctrl]
    P = [ctrl[0] * 2 - ctrl[1]] + ctrl + [ctrl[-1] * 2 - ctrl[-2]]
    out, ts = [], []
    for i in range(1, len(P) - 2):
        p0, p1, p2, p3 = P[i - 1], P[i], P[i + 1], P[i + 2]
        for k in range(per):
            t = k / per
            out.append(0.5 * ((2 * p1) + (-p0 + p2) * t + (2 * p0 - 5 * p1 + 4 * p2 - p3) * t * t +
                              (-p0 + 3 * p1 - 3 * p2 + p3) * t * t * t))
            ts.append(i - 1 + t)
    out.append(ctrl[-1])
    ts.append(len(ctrl) - 1.0)
    return out, ts


def tube(mb, pts, radii, keys, n=16, cap0=True, cap1=True, up=None):
    """A tube through pts (parallel-transported frames). radii: one per point
    (float or (rx, ry)); keys: one key or one per segment."""
    m = len(pts)
    Ts = []
    for i in range(m):
        d = pts[min(i + 1, m - 1)] - pts[max(i - 1, 0)]
        Ts.append(d.normalized() if d.length > 1e-9 else V(0, 0, 1))
    ref = up if up is not None else (V(0, 0, 1) if abs(Ts[0].z) < 0.9 else V(0, -1, 0))
    N = (ref - Ts[0] * ref.dot(Ts[0])).normalized()
    rows = []
    for i, p in enumerate(pts):
        N = N - Ts[i] * N.dot(Ts[i])
        N.normalize()
        B = Ts[i].cross(N)
        r = radii[i]
        rx, ry = (r, r) if not isinstance(r, (tuple, list)) else r
        rows.append([mb.vert(p + N * (rx * math.cos(2 * math.pi * k / n)) + B * (ry * math.sin(2 * math.pi * k / n)))
                     for k in range(n)])
    kf = (lambda i, k, c: keys[i]) if isinstance(keys, (list, tuple)) else (lambda i, k, c: keys)
    grid_faces(mb, rows, kf)
    k0 = keys[0] if isinstance(keys, (list, tuple)) else keys
    k1 = keys[-1] if isinstance(keys, (list, tuple)) else keys
    if cap0:
        fan(mb, mb.vert(pts[0] - Ts[0] * (radii[0] if not isinstance(radii[0], (tuple, list)) else radii[0][0]) * 0.5),
            rows[0], k0, flip=True)
    if cap1:
        fan(mb, mb.vert(pts[-1] + Ts[-1] * (radii[-1] if not isinstance(radii[-1], (tuple, list)) else radii[-1][0]) * 0.5),
            rows[-1], k1)


def limb(mb, ctrl, rad_ctrl, key, per=6, n=16, cap0=True, cap1=True):
    pts, ts = catmull(ctrl, per)
    radii = []
    for t in ts:
        i = min(int(t), len(rad_ctrl) - 2)
        radii.append(lerp(rad_ctrl[i], rad_ctrl[i + 1], t - i))
    keys = key if not callable(key) else [key(ts[i], pts[i]) for i in range(len(pts) - 1)]
    tube(mb, pts, radii, keys, n=n, cap0=cap0, cap1=cap1)
    return pts


# ---------------------------------------------------------------------------
# Materials (characters: light + id + z; the colours are only for debugging)
# ---------------------------------------------------------------------------

RIM = 0.50        # contour darkening at grazing angles (0 = off)
RIM_POW = 2.2


def shade(base, rough=0.55, spec=0.3, rim=None, emit=0.0):
    rim = RIM if rim is None else rim

    def build(nt, neutral):
        b = nt.nodes.new('ShaderNodeBsdfPrincipled')
        col = (0.8, 0.8, 0.8) if neutral else tuple(base)
        b.inputs['Roughness'].default_value = rough
        b.inputs['Specular'].default_value = spec
        if rim > 0:
            lw = nt.nodes.new('ShaderNodeLayerWeight')
            lw.inputs['Blend'].default_value = 0.5
            pw = nt.nodes.new('ShaderNodeMath')
            pw.operation = 'POWER'
            pw.inputs[1].default_value = RIM_POW
            nt.links.new(lw.outputs['Facing'], pw.inputs[0])
            mx = nt.nodes.new('ShaderNodeMixRGB')
            nt.links.new(pw.outputs[0], mx.inputs['Fac'])
            mx.inputs['Color1'].default_value = col + (1,)
            mx.inputs['Color2'].default_value = tuple(c * (1 - rim) for c in col) + (1,)
            nt.links.new(mx.outputs['Color'], b.inputs['Base Color'])
        else:
            b.inputs['Base Color'].default_value = col + (1,)
        if emit > 0:
            b.inputs['Emission'].default_value = (1, 1, 1, 1) if neutral else tuple(base) + (1,)
            b.inputs['Emission Strength'].default_value = emit
        return b.outputs['BSDF']
    return build


def register_materials():
    M = C.mat
    # Tommy's body (SPEC s.4 ids)
    M('skin', id=1, build=shade((0.90, 0.62, 0.45), rough=0.55, spec=0.30))
    M('hair', id=2, build=shade((0.30, 0.18, 0.10), rough=0.45, spec=0.40))
    M('dark', id=3, build=shade((0.03, 0.03, 0.03), rough=0.35, spec=0.50, rim=0.0))
    M('white', id=4, build=shade((0.95, 0.95, 0.95), rough=0.30, spec=0.50, rim=0.0, emit=0.18))
    M('shirtA', id=5, build=shade((0.10, 0.35, 0.90), rough=0.75, spec=0.25))
    M('shirtB', id=6, build=shade((0.95, 0.95, 0.95), rough=0.75, spec=0.25))
    M('shirtC', id=7, build=shade((0.98, 0.75, 0.10), rough=0.55, spec=0.30))
    M('pants', id=8, build=shade((0.12, 0.15, 0.35), rough=0.85, spec=0.20))
    M('shoeA', id=9, build=shade((0.85, 0.10, 0.10), rough=0.50, spec=0.35))
    M('shoeB', id=10, build=shade((0.95, 0.95, 0.95), rough=0.55, spec=0.30))
    M('socks', id=11, build=shade((0.95, 0.95, 0.95), rough=0.85, spec=0.20))
    M('blush', id=12, build=shade((0.98, 0.55, 0.55), rough=0.55, spec=0.30, rim=0.0))
    # layers (caps, back, hand): 1 main, 2 secondary, 3 detail, 4 dark / glow
    M('capA', id=1, build=shade((0.85, 0.12, 0.12), rough=0.70, spec=0.25))
    M('capB', id=2, build=shade((0.85, 0.12, 0.12), rough=0.70, spec=0.25))
    M('capC', id=3, build=shade((0.95, 0.95, 0.95), rough=0.55, spec=0.30))
    M('capD', id=4, build=shade((0.30, 0.05, 0.05), rough=0.80, spec=0.20))
    M('L1', id=1, build=shade((0.80, 0.50, 0.20), rough=0.60, spec=0.30))
    M('L2', id=2, build=shade((0.30, 0.30, 0.35), rough=0.65, spec=0.25))
    M('L3', id=3, build=shade((0.85, 0.85, 0.90), rough=0.45, spec=0.45))
    M('L3m', id=3, build=shade((0.85, 0.85, 0.90), rough=0.22, spec=0.80))      # metal: glossy
    M('L1gold', id=1, build=shade((0.95, 0.75, 0.20), rough=0.25, spec=0.80))   # the crown
    M('L4g', id=4, build=shade((1.0, 0.9, 0.5), rough=0.5, spec=0.2, rim=0.0, emit=1.3))  # glow
    M('L2gold', id=2, build=shade((0.90, 0.65, 0.15), rough=0.30, spec=0.70))
    M('L3gem', id=3, build=shade((0.85, 0.10, 0.25), rough=0.10, spec=0.90))
    M('knit', id=1, build=shade((0.20, 0.60, 0.30), rough=0.95, spec=0.10))
    M('knitB', id=2, build=shade((0.95, 0.95, 0.90), rough=0.95, spec=0.10))
    # pets
    M('petA', id=1, build=shade((0.80, 0.55, 0.30), rough=0.65, spec=0.25))
    M('petB', id=2, build=shade((0.97, 0.90, 0.78), rough=0.65, spec=0.25))
    M('petDark', id=3, build=shade((0.03, 0.03, 0.03), rough=0.30, spec=0.55, rim=0.0))
    M('petWhite', id=4, build=shade((0.95, 0.95, 0.95), rough=0.30, spec=0.50, rim=0.0, emit=0.18))
    M('petCollar', id=5, build=shade((0.85, 0.15, 0.20), rough=0.50, spec=0.35))
    M('petTag', id=6, build=shade((0.95, 0.75, 0.20), rough=0.30, spec=0.60))


# ---------------------------------------------------------------------------
# Tommy: dimensions. Character space: root on the ground between the feet,
# facing -Y, +X = his LEFT (screen right when he faces us), +Z up. Metres.
# ---------------------------------------------------------------------------

PELVIS_Z = 0.335
CHEST_OFF = 0.060          # pelvis pivot -> chest pivot
NECK_OFF = 0.190           # chest pivot -> neck pivot
HEAD_OFF = 0.195           # neck pivot -> head centre
HIP_X, HIP_Z = 0.066, -0.012
SHOULDER = (0.126, 0.010, 0.132)   # from the chest pivot (x per side)
L_THIGH, L_SHIN = 0.128, 0.128
L_UP, L_FORE = 0.108, 0.098
ANKLE_Z = 0.085
FOOT_X = 0.072
SOLE_Z = -ANKLE_Z          # the ground in foot space

CHEST_REST = V(0, 0, PELVIS_Z + CHEST_OFF)
PELVIS_REST = V(0, 0, PELVIS_Z)

# the head: an ellipsoid, a little taller above the equator
HX, HY, HZU, HZD = 0.196, 0.182, 0.186, 0.168


def hdir(u, v):
    """Direction from the head centre: u round the head (0 = the face, + =
    towards his left, +X), v up from the equator."""
    return V(math.sin(u) * math.cos(v), -math.cos(u) * math.cos(v), math.sin(v))


def ell_pt(d, rx, ry, rzu, rzd):
    rz = rzu if d.z >= 0 else rzd
    k = 1.0 / math.sqrt((d.x / rx) ** 2 + (d.y / ry) ** 2 + (d.z / rz) ** 2)
    return d * k


def ell_nrm(p, rx, ry, rzu, rzd):
    rz = rzu if p.z >= 0 else rzd
    return V(p.x / rx ** 2, p.y / ry ** 2, p.z / rz ** 2).normalized()


def head_pt(u, v, off=0.0):
    p = ell_pt(hdir(u, v), HX, HY, HZU, HZD)
    if off:
        p = p + ell_nrm(p, HX, HY, HZU, HZD) * off
    return p


def head_nrm(u, v):
    return ell_nrm(ell_pt(hdir(u, v), HX, HY, HZU, HZD), HX, HY, HZU, HZD)


def head_skin(mb):
    NU, NV = 72, 36
    top = mb.vert(head_pt(0, math.pi / 2))
    bot = mb.vert(head_pt(0, -math.pi / 2))
    rows = []
    for j in range(1, NV):
        v = math.pi / 2 - math.pi * j / NV
        rows.append([mb.vert(head_pt(-math.pi + 2 * math.pi * i / NU, v)) for i in range(NU)])
    grid_faces(mb, rows, lambda i, k, c: 'skin')
    fan(mb, top, rows[0], 'skin')
    fan(mb, bot, rows[-1], 'skin', flip=True)


def blob(mb, cu, cv, au, av, key, off, rot=0.0, nr=4, na=24, shape=None):
    """An elliptic decal on the head at (cu, cv), half-sizes (au, av) rad."""
    c = mb.vert(head_pt(cu, cv, off))
    rows = []
    cr, sr = math.cos(rot), math.sin(rot)
    for i in range(1, nr + 1):
        rho = i / nr
        row = []
        for k in range(na):
            th = 2 * math.pi * k / na
            x, y = au * rho * math.cos(th), av * rho * math.sin(th)
            if shape:
                x, y = shape(x, y, th, rho)
            x, y = x * cr - y * sr, x * sr + y * cr
            row.append(mb.vert(head_pt(cu + x / math.cos(cv), cv + y, off)))
        rows.append(row)
    fan(mb, c, rows[0], key)
    grid_faces(mb, rows, lambda i, k, cc: key)


def strip(mb, curve, half_w, key, off, n=14):
    """A decal band along curve(s) -> (u, v), s in [0, 1], half width
    half_w(s) (rad)."""
    rows_a, rows_b = [], []
    for i in range(n + 1):
        s = i / n
        u, v = curve(s)
        u1, v1 = curve(max(0.0, s - 1e-3))
        u2, v2 = curve(min(1.0, s + 1e-3))
        tu, tv = (u2 - u1) * math.cos(v), v2 - v1
        L = math.hypot(tu, tv) or 1.0
        nu, nv = -tv / L, tu / L
        w = half_w(s)
        rows_a.append(mb.vert(head_pt(u + nu * w / math.cos(v), v + nv * w, off)))
        rows_b.append(mb.vert(head_pt(u - nu * w / math.cos(v), v - nv * w, off)))
    for i in range(n):
        mb.face((rows_a[i], rows_a[i + 1], rows_b[i + 1], rows_b[i]), key)


# --- the face ---------------------------------------------------------------

EYE_U, EYE_V = 0.335, 0.20          # eye centres (u = +-EYE_U); high on the head: the
                                    # camera looks down 44 deg and squeezes the lower face
EYE_AU, EYE_AV = 0.170, 0.235       # eye white half sizes (rad)


def build_eyes(mb, mode):
    for s in (-1, 1):
        cu = s * EYE_U
        if mode in ('open', 'wide'):
            big = 1.18 if mode == 'wide' else 1.0
            # the white: an egg, flatter at the bottom
            blob(mb, cu, EYE_V, EYE_AU * big, EYE_AV * big, 'white', 0.002)
            if mode == 'open':
                pu, pv, pa, pb = cu - s * 0.030, EYE_V - 0.035, 0.132, 0.205
            else:
                pu, pv, pa, pb = cu - s * 0.01, EYE_V - 0.01, 0.075, 0.110
            blob(mb, pu, pv, pa, pb, 'dark', 0.004)
            # catchlights: both upper-left on the screen (the sun's side)
            blob(mb, pu - 0.050, pv + 0.085, 0.048, 0.058, 'white', 0.006, nr=2, na=12)
        elif mode == 'closed':
            strip(mb, lambda t, cu=cu: (cu + (t - 0.5) * 2 * EYE_AU * 1.05,
                                         EYE_V - 0.06 - 0.10 * (1 - ((t - 0.5) * 2) ** 2)),
                  lambda t: 0.042 * (0.5 + 0.5 * math.sin(math.pi * t)), 'dark', 0.004)
        elif mode == 'dizzy':      # swirls: a dark ring and a dot on the white
            blob(mb, cu, EYE_V, EYE_AU, EYE_AV, 'white', 0.002)
            strip(mb, lambda t, cu=cu: (cu + 0.62 * EYE_AU * math.cos(2 * math.pi * t + s),
                                         EYE_V + 0.62 * EYE_AV * math.sin(2 * math.pi * t + s)),
                  lambda t: 0.030, 'dark', 0.004, n=24)
            blob(mb, cu, EYE_V, 0.045, 0.06, 'dark', 0.005, nr=2, na=12)
        elif mode == 'happy':      # ^ ^
            strip(mb, lambda t, cu=cu: (cu + (t - 0.5) * 2 * EYE_AU * 1.05,
                                         EYE_V - 0.12 + 0.26 * (1 - abs((t - 0.5) * 2) ** 1.3)),
                  lambda t: 0.055 * (0.55 + 0.45 * math.sin(math.pi * t)), 'dark', 0.004)


def build_brows(mb, mode='brave'):
    for s in (-1, 1):
        cu = s * EYE_U
        if mode == 'brave':
            # a short thick dash, the inner end a touch lower: determined, not cross
            curve = (lambda t, cu=cu, s=s: (cu + s * (t - 0.5) * 0.36,
                                             0.53 + 0.030 * (t - 0.5) * 2 * 1.0 + 0.03 * (1 - ((t - 0.5) * 2) ** 2)))
        else:   # raised (surprise)
            curve = (lambda t, cu=cu, s=s: (cu + s * (t - 0.5) * 0.34,
                                             0.60 + 0.05 * (1 - ((t - 0.5) * 2) ** 2)))
        # brows are hair colour (id 2, SPEC s.12)
        strip(mb, curve, lambda t: 0.040 * (0.65 + 0.35 * math.sin(math.pi * t)), 'hair', 0.003)


def build_mouth(mb, mode='smile'):
    if mode == 'smile':
        # a small lopsided smile: his left corner a little higher (a confident grin)
        strip(mb, lambda t: ((t - 0.5) * 0.36 + 0.02,
                             -0.125 - 0.070 * (1 - ((t - 0.5) * 2) ** 2) + 0.040 * (t - 0.5)),
              lambda t: 0.042 * (0.45 + 0.55 * math.sin(math.pi * t)), 'dark', 0.003)
    elif mode == 'grin':       # open smile with teeth
        blob(mb, 0.01, -0.15, 0.15, 0.085, 'dark', 0.003,
             shape=lambda x, y, th, rho: (x, y if y < 0 else y * 0.35))
        blob(mb, 0.01, -0.115, 0.12, 0.026, 'white', 0.005, nr=2)
    elif mode == 'o':          # surprised
        blob(mb, 0.0, -0.15, 0.065, 0.08, 'dark', 0.003)


def build_face_extras(mb, blush=True):
    # nose: a tiny button
    ellipsoid(mb, head_pt(0.0, 0.02, -0.004), (0.022, 0.018, 0.017), 'skin',
              rot=frame_z(head_nrm(0.0, 0.02), V(1, 0, 0)).to_4x4(), n=12, rings=8)
    # ears
    for s in (-1, 1):
        u, v = s * 1.50, -0.05
        p = head_pt(u, v, -0.006)
        n = head_nrm(u, v)
        rot = frame_z(n, V(0, 0, 1)).to_4x4()
        # ellipsoid axes: x (up) 0.056, y (fore-aft) 0.040, z (out) 0.024
        ellipsoid(mb, p, (0.052, 0.036, 0.030), 'skin', rot=rot, n=16, rings=10)
    if blush:
        for s in (-1, 1):
            blob(mb, s * 0.60, -0.02, 0.11, 0.060, 'blush', 0.002, nr=2, na=16)


# --- hair -------------------------------------------------------------------

HAIR_BASE = [(0.0, 0.64), (0.55, 0.62), (0.85, 0.52), (1.02, 0.34), (1.12, 0.12), (1.22, -0.02),
             (1.30, 0.08), (1.38, 0.30), (1.58, 0.36), (1.78, 0.20), (2.10, -0.05), (2.50, -0.30),
             (math.pi, -0.40)]


LOCKS = 16            # lock ridges round the head (the fringe points sit on them)
LOCK_PHASE = 0.13     # swept a little towards his right


def lock_ridge(u):
    return 0.5 + 0.5 * math.cos(LOCKS * (u + LOCK_PHASE))


def hairline(u):
    a = abs(u)
    v = interp(HAIR_BASE, a)
    if a < 1.0:
        # the fringe: pointed locks
        amp = 0.20 * smoothstep(1.0, 0.62, a)
        v -= amp * lock_ridge(u) ** 2.2
    if a > 2.05:
        # the nape: small points
        v -= 0.11 * smoothstep(2.05, 2.4, a) * lock_ridge(u + 0.2) ** 2.2
    # sideburn points
    v -= 0.10 * max(0.0, 1 - abs(a - 1.20) / 0.08)
    return v


HAIR_TOP = 0.020


def hair_thick(u, t):
    base = 0.008 + (HAIR_TOP - 0.008) * (1 - t) ** 0.9
    # lock ridges, fading out at the crown where they would crowd
    rid = lock_ridge(u + 0.05 * math.sin(3 * t))
    k = smoothstep(0.05, 0.35, t)
    lumps = 1 + 0.30 * (rid - 0.5) * k
    flick = 0.007 * smoothstep(0.75, 1.0, t) * (0.4 + 0.6 * rid)
    return base * lumps + flick


def build_hair(mb):
    NU, NT = 176, 22
    vt = math.pi / 2
    rows = []
    for j in range(1, NT + 1):
        t = j / NT
        row = []
        for i in range(NU):
            u = -math.pi + 2 * math.pi * i / NU
            v = vt - t * (vt - hairline(u))
            row.append(mb.vert(head_pt(u, v, hair_thick(u, t))))
        rows.append(row)
    inner = []
    for i in range(NU):
        u = -math.pi + 2 * math.pi * i / NU
        inner.append(mb.vert(head_pt(u, hairline(u) + 0.02, -0.004)))
    rows.append(inner)
    grid_faces(mb, rows, lambda i, k, c: 'hair')
    top = mb.vert(head_pt(0, vt, hair_thick(0, 0)))
    fan(mb, top, rows[0], 'hair')


# --- torso, pelvis ----------------------------------------------------------

# absolute z (rest), half width, front, back
SHIRT = [(0.350, 0.132, 0.104, 0.094), (0.362, 0.139, 0.109, 0.098), (0.390, 0.141, 0.111, 0.099),
         (0.430, 0.140, 0.110, 0.097), (0.470, 0.136, 0.105, 0.093), (0.510, 0.129, 0.097, 0.087),
         (0.540, 0.119, 0.087, 0.079), (0.565, 0.099, 0.071, 0.065), (0.585, 0.066, 0.053, 0.049)]
SHIRT_Z = [0.350, 0.356, 0.362, 0.375, 0.390, 0.410, 0.430, 0.450, 0.470, 0.490, 0.510, 0.525,
           0.540, 0.555, 0.565, 0.575, 0.585]
BANDS = [(0.390, 0.430), (0.470, 0.510)]
PELVIS = [(0.268, 0.050, 0.040, 0.042), (0.282, 0.098, 0.074, 0.080), (0.298, 0.118, 0.089, 0.095),
          (0.325, 0.127, 0.096, 0.100), (0.355, 0.128, 0.099, 0.100), (0.378, 0.124, 0.097, 0.098)]


def shirt_key(z):
    for z0, z1 in BANDS:
        if z0 <= z <= z1:
            return 'shirtB'
    return 'shirtA'


def build_shirt(mb):
    rings = []
    for z in SHIRT_Z:
        hw, fr, bk = interp_rows(SHIRT, z)
        rings.append(ring_pts(V(0, 0, z), V(1, 0, 0), V(0, -1, 0), hw, hw, fr, bk, 40, 2.35))
    loft(mb, rings, lambda i, k, c: shirt_key(c.z), cap0=V(0, 0, 0.352), cap1=V(0, 0, 0.594),
         cap_keys=('shirtA', 'shirtA'))
    return rings


def build_emblem(mb, shirt_mb):
    """A round badge on the chest, projected onto the shirt."""
    bvh = BVHTree.FromPolygons([tuple(v) for v in shirt_mb.v], shirt_mb.f)
    cz, r = 0.450, 0.061
    nr, na = 4, 28

    def proj(x, z):
        hit = bvh.ray_cast(V(x, -0.4, z), V(0, 1, 0))
        if hit[0] is None:
            return V(x, -0.11, z)
        n = hit[1] if hit[1].y < 0 else -hit[1]
        return hit[0] + n * 0.0035
    c = mb.vert(proj(0, cz))
    rows = []
    for i in range(1, nr + 1):
        rho = i / nr
        rows.append([mb.vert(proj(r * rho * math.cos(2 * math.pi * k / na),
                                  cz + r * rho * math.sin(2 * math.pi * k / na))) for k in range(na)])
    fan(mb, c, rows[0], 'shirtC')
    grid_faces(mb, rows, lambda i, k, cc: 'shirtC')


def build_pelvis(mb):
    rings = []
    zs = [0.268, 0.275, 0.282, 0.290, 0.298, 0.31, 0.325, 0.34, 0.355, 0.368, 0.378]
    for z in zs:
        hw, fr, bk = interp_rows(PELVIS, z)
        rings.append(ring_pts(V(0, 0, z), V(1, 0, 0), V(0, -1, 0), hw, hw, fr, bk, 36, 2.3))
    loft(mb, rings, lambda i, k, c: 'pants', cap0=V(0, 0, 0.262), cap1=V(0, 0, 0.382))


# --- hands, shoes -----------------------------------------------------------

def build_hand(mb, s, mode='relaxed'):
    """Hand in hand space: origin at the wrist, +Z along the forearm (out of
    the wrist), +Y the palm's front (thumb side), +X outwards for side s."""
    ellipsoid(mb, V(0, 0, 0.036), (0.040, 0.035, 0.044), 'skin', n=18, rings=10)
    ellipsoid(mb, V(0.0, -0.031, 0.027), (0.014, 0.014, 0.023), 'skin',
              rot=RX(28), n=10, rings=7)


SHOE = [  # y (forward is -), half width, top z, bottom z   (foot space, ankle at 0)
    (0.070, 0.034, -0.030, -0.070), (0.064, 0.046, 0.000, -0.072), (0.045, 0.052, 0.014, -0.072),
    (0.020, 0.055, 0.016, -0.072), (-0.010, 0.057, 0.004, -0.072), (-0.040, 0.058, -0.014, -0.072),
    (-0.070, 0.058, -0.026, -0.072), (-0.098, 0.056, -0.034, -0.072), (-0.120, 0.050, -0.040, -0.070),
    (-0.138, 0.038, -0.046, -0.066), (-0.150, 0.020, -0.052, -0.062)]
SOLE = [  # y, half width
    (0.078, 0.030), (0.072, 0.050), (0.050, 0.058), (0.0, 0.061), (-0.060, 0.063), (-0.110, 0.058),
    (-0.140, 0.046), (-0.156, 0.026)]


def build_shoe(mb, s):
    """Sneaker in foot space (origin at the ankle, forward -Y, sole at z=-0.085)."""
    rings = []
    ys = []
    for y, hw, top, bot in SHOE:
        zc = (top + bot) / 2
        rings.append(ring_pts(V(0, y, zc), V(1, 0, 0), V(0, 0, 1), hw, hw, top - zc, zc - bot, 24, 2.5))
        ys.append(y)

    def key(i, k, c):
        if c.y < -0.128 and c.z < -0.035:
            return 'shoeB'                     # toe cap
        if abs(c.x) < 0.022 and -0.070 < c.y < -0.010 and c.z > -0.030:
            return 'shoeB'                     # laces
        return 'shoeA'
    loft(mb, rings, key, cap0=V(0, 0.074, -0.05), cap1=V(0, -0.154, -0.058),
         cap_keys=('shoeA', 'shoeB'))
    # the chunky sole: a rounded slab
    zs = [(-0.085, 0.94), (-0.083, 1.0), (-0.066, 1.0), (-0.062, 0.95)]
    rings = []
    for z, sc in zs:
        pts = []
        n = 28
        for k in range(n):
            th = 2 * math.pi * k / n
            # the outline: an egg from the SOLE table
            cy = (SOLE[0][0] + SOLE[-1][0]) / 2
            ry = (SOLE[0][0] - SOLE[-1][0]) / 2
            y = cy + ry * math.sin(th)
            hw = interp(sorted(SOLE), y)
            x = hw * math.cos(th) / max(1e-3, abs(math.cos(th)) ** 0.35) * 0.98
            x = clamp(x, -hw, hw)
            pts.append(V(x * sc, cy + (y - cy) * sc, z))
        rings.append(pts)
    loft(mb, rings, lambda i, k, c: 'shoeB', cap0=V(0, -0.04, -0.085), cap1=V(0, -0.04, -0.062))


# ---------------------------------------------------------------------------
# The pose
# ---------------------------------------------------------------------------

BASE_POSE = dict(
    pz=0.0, py=0.0,             # pelvis offset (m): up, forward(-)/back(+)
    pp=0.0,                     # pelvis pitch (deg, + = top forward)
    sp=0.0, st=0.0, sr=0.0,     # spine pitch, twist (+ = towards his left), roll
    hp=-12.0, hy=0.0, hr=0.0,   # head pitch (+ = chin down), yaw, roll: a little chin-up
    armR=(6.0, 11.0, 18.0), armL=(6.0, 11.0, 18.0),   # flex fwd, abduct, elbow bend (deg)
    legR=(0.0, 0.0, 0.0, 0.0, True), legL=(0.0, 0.0, 0.0, 0.0, True),  # dx, dy, dz, toe-down deg, glued
    sq=(1.0, 1.0),              # squash about the root: (xy, z)
    flip=0.0,                   # airborne only: the whole body pitched forwards about his middle (deg)
    hand=None,                  # None or 'tuck': where the hand item goes
    eyes='open', brows='brave', mouth='smile',
)


def P(**kw):
    p = dict(BASE_POSE)
    p.update(kw)
    return p


def arm_dirs(chest_rot, s, f, a, b, roll=0.0):
    """Upper arm and forearm directions: f flexes forwards, a abducts
    outwards, b bends the elbow forwards, roll swings the forearm in the
    frontal plane (+ = outwards; a wave)."""
    up = (RX(-f) @ RY(-s * a)).to_3x3() @ V(0, 0, -1)
    fo = (RX(-(f + b)) @ RY(-s * a)).to_3x3() @ V(0, 0, -1)
    if roll:
        fo = RY(-s * roll).to_3x3() @ fo
    return (chest_rot @ up).normalized(), (chest_rot @ fo).normalized()


def evaluate(p):
    """Pose -> bone matrices (character space) and joint positions."""
    pel = T(0, p['py'], PELVIS_Z + p['pz']) @ RX(p['pp'])
    chest = pel @ T(0, 0, CHEST_OFF) @ RX(p['sp']) @ RZ(p['st']) @ RY(p['sr'])
    neck = chest @ T(0, 0, NECK_OFF) @ RX(p['hp']) @ RZ(p['hy']) @ RY(p['hr'])
    head = neck @ T(0, 0, HEAD_OFF)
    J = dict(pelvis=pel, chest=chest, neck=neck, head=head)
    cr = chest.to_3x3()
    for s, side in ((-1, 'R'), (1, 'L')):
        arm = p['arm' + side]
        f, a, b = arm[:3]
        roll = arm[3] if len(arm) > 3 else 0.0
        sh = chest @ V(s * SHOULDER[0], SHOULDER[1], SHOULDER[2])
        du, df = arm_dirs(cr, s, f, a, b, roll)
        el = sh + du * L_UP
        wr = el + df * L_FORE
        J['sh' + side], J['el' + side], J['wr' + side] = sh, el, wr
        J['hand' + side] = (T(wr) @ frame_z(df, V(s, 0, 0)).to_4x4())
        dx, dy, dz, toe, glued = p['leg' + side]
        hip = pel @ V(s * HIP_X, 0, HIP_Z)
        yaw = s * 7.0
        foot_rot = RZ(yaw) @ RX(toe)
        ank = V(s * FOOT_X + dx, dy, ANKLE_Z + dz)
        if glued:
            # keep the lowest point of the sole on the ground (z = dz)
            lo = min((foot_rot @ V(x, y, SOLE_Z)).z for x in (-0.05, 0.05) for y in (0.075, -0.155))
            ank.z = dz - lo
        pole = pel.to_3x3() @ V(s * 0.25, -1, 0)
        kn, an = ik2(hip, ank, L_THIGH, L_SHIN, pole)
        J['hip' + side], J['kn' + side], J['an' + side] = hip, kn, an
        J['foot' + side] = T(an) @ foot_rot
    # grip frames for hand items: origin in the fist, -Y = where the item
    # points (forwards), +Z = up. The position follows the hand exactly; the
    # item stays mostly upright (a kid keeps a torch up whatever his arm does)
    # and leans a quarter of the way along the forearm.
    rot_c = chest.to_quaternion()
    for side, s in (('R', -1), ('L', 1)):
        H = J['hand' + side]
        c = H @ V(0, 0, 0.036)
        df = (J['wr' + side] - J['el' + side]).normalized()
        up = (V(0, 0, 1) * 1.0 - df * 0.35)
        up = up.normalized() if up.length > 1e-6 else V(0, 0, 1)
        fwd = rot_c.to_matrix() @ V(0, -1, 0)
        fwd = (fwd - up * fwd.dot(up)).normalized()
        x = up.cross(fwd)      # right-handed: X = Y x Z with Y = -fwd
        M = Matrix((x, -fwd, up)).transposed().to_4x4()
        M.translation = c
        J['grip' + side] = M
    # where the hand item goes when both hands are busy (push, use): tucked
    # under his right arm, upright
    M = rot_c.to_matrix().to_4x4()
    M.translation = chest @ V(-0.165, -0.020, -0.005)
    J['tuckR'] = M
    return J


# ---------------------------------------------------------------------------
# Tommy built for one pose
# ---------------------------------------------------------------------------

def bone_delta(M, rest_origin):
    return M @ T(-rest_origin)


def build_tommy(p, root):
    """All body objects for pose p; root = world matrix of character space.
    Returns (objects, joints)."""
    J = evaluate(p)
    objs = []
    # head group (head space)
    mb = MB()
    head_skin(mb)
    build_face_extras(mb)
    objs.append(to_object('t_head', mb, root @ J['head']))
    mb = MB()
    build_eyes(mb, p['eyes'])
    build_brows(mb, p['brows'])
    build_mouth(mb, p['mouth'])
    objs.append(to_object('t_face', mb, root @ J['head'], recalc=False))
    mb = MB()
    build_hair(mb)
    objs.append(to_object('t_hair', mb, root @ J['head']))
    # neck (chest space)
    mb = MB()
    tube(mb, [V(0, 0.005, 0.145), V(0, 0.005, 0.26)], [0.050, 0.050], 'skin', n=20)
    objs.append(to_object('t_neck', mb, root @ J['chest']))
    # shirt + emblem (rest coords moved by the chest)
    mb = MB()
    build_shirt(mb)
    em = MB()
    build_emblem(em, mb)
    Mc = root @ bone_delta(J['chest'], CHEST_REST)
    objs.append(to_object('t_shirt', mb, Mc))
    objs.append(to_object('t_emblem', em, Mc, recalc=False))
    # jeans pelvis
    mb = MB()
    build_pelvis(mb)
    objs.append(to_object('t_pelvis', mb, root @ bone_delta(J['pelvis'], PELVIS_REST)))
    # limbs (character space)
    mb = MB()
    for side, s in (('R', -1), ('L', 1)):
        sh, el, wr = J['sh' + side], J['el' + side], J['wr' + side]
        du = (el - sh).normalized()
        limb(mb, [sh - du * 0.01, el, wr + (wr - el).normalized() * 0.012], [0.031, 0.028, 0.026], 'skin',
             per=7, n=14)
        # sleeve: shirt with a cuff band at its end
        pts = [sh - du * 0.030, sh, sh + du * 0.030, sh + du * 0.052, sh + du * 0.056, sh + du * 0.078]
        tube(mb, pts, [0.050, 0.053, 0.052, 0.050, 0.051, 0.050],
             ['shirtA', 'shirtA', 'shirtA', 'shirtB', 'shirtB'], n=18)
        ellipsoid(mb, sh, (0.052, 0.052, 0.052), 'shirtA', n=18, rings=10)
        # legs: jeans with a rolled cuff, the sock, the shoe
        hip, kn, an = J['hip' + side], J['kn' + side], J['an' + side]
        dshin = (an - kn).normalized()
        cuff = an - dshin * 0.042
        limb(mb, [hip + V(0, 0, 0.035), hip, kn, cuff], [0.056, 0.059, 0.054, 0.058], 'pants', per=7, n=18)
        tube(mb, [cuff - dshin * 0.026, cuff - dshin * 0.021, cuff - dshin * 0.004, cuff + dshin * 0.001],
             [0.058, 0.066, 0.066, 0.060], 'pants', n=18)
        tube(mb, [cuff - dshin * 0.01, an + dshin * 0.02], [0.035, 0.035], 'socks', n=14)
    objs.append(to_object('t_limbs', mb, root))
    for side, s in (('R', -1), ('L', 1)):
        mb = MB()
        build_hand(mb, s)
        objs.append(to_object('t_hand' + side, mb, root @ J['hand' + side]))
        mb = MB()
        build_shoe(mb, s)
        objs.append(to_object('t_shoe' + side, mb, root @ J['foot' + side]))
    return objs, J


# ---------------------------------------------------------------------------
# Layers. Ids: 1 main, 2 secondary, 3 detail / metal, 4 dark (caps) or glow
# (back and hand items). Caps are built in head space, back items in chest
# space (chest pivot at the origin), hand items in the grip frame; the cape,
# the tank's hose and the balloon string are swept in character space.
# ---------------------------------------------------------------------------

CAP_INFL = 0.033


def cap_pt(d, infl=CAP_INFL):
    return ell_pt(d, HX + infl, HY + infl, HZU + infl, HZD + infl)


def cap_dir(Rc, a, b):
    return (Rc @ V(math.sin(b) * math.cos(a), math.sin(b) * math.sin(a), math.cos(b))).normalized()


def crown(mb, Rc, edge_deg, key, n=48, rings=14, infl=CAP_INFL, key_fn=None, bumps=None, fold=0.014):
    """A dome over the (inflated) head round the cap axis (Rc: 3x3, its Z the
    axis), from the apex to edge_deg, the rim folding in under the edge."""
    rows = []
    for i in range(1, rings + 1):
        b = math.radians(edge_deg) * i / rings
        row = []
        for k in range(n):
            a = 2 * math.pi * k / n
            d = cap_dir(Rc, a, b)
            e = infl + (bumps(a, b) if bumps else 0.0)
            row.append(mb.vert(cap_pt(d, e)))
        rows.append(row)
    inner = []
    b = math.radians(edge_deg - 1.0)
    for k in range(n):
        d = cap_dir(Rc, 2 * math.pi * k / n, b)
        inner.append(mb.vert(cap_pt(d, infl) - d * fold))
    rows.append(inner)
    kf = key_fn or (lambda i, k, c: key)
    grid_faces(mb, rows, lambda i, k, c: kf(i, k, c) if i < len(rows) - 2 else 'capD')
    top = mb.vert(cap_pt((Rc @ V(0, 0, 1)).normalized(), infl + (bumps(0, 0) if bumps else 0.0)))
    fan(mb, top, rows[0], kf(0, 0, None) if key_fn else key)
    return rows


def bill(mb, Rc, edge_deg, span_deg, length, droop_deg, camber, thick, key, under='capD', fwd=-90.0,
         infl=CAP_INFL):
    """A cap bill from the crown's rim at azimuth fwd (deg round the cap axis,
    -90 = the front), droop_deg down from the cap's base plane."""
    ncol, nrow = 25, 8
    b = math.radians(edge_deg - 0.5)
    inner, outer = [], []
    fw = Rc @ V(math.cos(math.radians(fwd)), math.sin(math.radians(fwd)), 0)
    ax = Rc @ V(0, 0, 1)
    for c in range(ncol):
        t = (c / (ncol - 1)) * 2 - 1
        a = math.radians(fwd + span_deg * t)
        d = cap_dir(Rc, a, b)
        inner.append(cap_pt(d, infl) - ax * 0.004 - d * 0.010)    # rooted inside the crown: no seam
        outer.append(length * (1 - abs(t) ** 2.2) ** 0.55 + 0.004)
    top_rows, bot_rows = [], []
    tdn = math.tan(math.radians(droop_deg))
    for r in range(nrow + 1):
        s = r / nrow
        trow, brow = [], []
        for c in range(ncol):
            t = (c / (ncol - 1)) * 2 - 1
            pin, L = inner[c], outer[c]
            p = pin + fw * (L * s) - ax * (L * s * tdn) - ax * (camber * t * t * s)
            trow.append(mb.vert(p))
            brow.append(mb.vert(p - ax * thick))
        top_rows.append(trow)
        bot_rows.append(brow)
    for r in range(nrow):
        for c in range(ncol - 1):
            mb.face((top_rows[r][c], top_rows[r][c + 1], top_rows[r + 1][c + 1], top_rows[r + 1][c]), key)
            mb.face((bot_rows[r][c], bot_rows[r + 1][c], bot_rows[r + 1][c + 1], bot_rows[r][c + 1]), under)
    for c in range(ncol - 1):
        mb.face((top_rows[nrow][c], bot_rows[nrow][c], bot_rows[nrow][c + 1], top_rows[nrow][c + 1]), key)
    for c in (0, ncol - 1):
        for r in range(nrow):
            mb.face((top_rows[r][c], bot_rows[r][c], bot_rows[r + 1][c], top_rows[r + 1][c]), key)
    for c in range(ncol - 1):
        mb.face((top_rows[0][c], top_rows[0][c + 1], bot_rows[0][c + 1], bot_rows[0][c]), under)


def brim(mb, Rc, edge_deg, width, droop_deg, thick, key, under, infl):
    """A round brim all the way round the rim (the bucket hat)."""
    n = 64
    b = math.radians(edge_deg - 0.5)
    ax = Rc @ V(0, 0, 1)
    tdn = math.tan(math.radians(droop_deg))
    rows_t, rows_b = [], []
    for r in range(6):
        s = r / 5
        rt, rb = [], []
        for k in range(n):
            a = 2 * math.pi * k / n
            d = cap_dir(Rc, a, b)
            pin = cap_pt(d, infl) - d * 0.008
            out = (d - ax * d.dot(ax)).normalized()
            w = width * (1.0 - 0.18 * max(0.0, -math.sin(a)))     # a little shorter in front
            p = pin + out * (w * s) - ax * (w * s * tdn) - ax * (0.010 * s * s)
            rt.append(mb.vert(p))
            rb.append(mb.vert(p - ax * thick))
        rows_t.append(rt)
        rows_b.append(rb)
    grid_faces(mb, rows_t, lambda i, k, c: key)
    grid_faces(mb, rows_b[::-1], lambda i, k, c: under)
    grid_faces(mb, [rows_t[-1], rows_b[-1]], lambda i, k, c: key)


def button(mb, Rc, infl, key, r=0.020, h=0.010, lift=0.0):
    ax = (Rc @ V(0, 0, 1)).normalized()
    top = cap_pt(ax, infl) + ax * lift
    ellipsoid(mb, top, (r, r, h), key, rot=frame_z(ax, V(1, 0, 0)).to_4x4(), n=12, rings=6)
    return top, ax


def cap_cap(mb, ctx):
    # pushed back so the bill rides above his eyes, but low enough and long
    # enough that the bill reads from every side (SPEC s.12)
    Rc = RX(-14.0).to_3x3()
    edge = 64.0
    crown(mb, Rc, edge, 'capA')
    button(mb, Rc, CAP_INFL, 'capC')
    bill(mb, Rc, edge, 60.0, 0.140, 5.0, 0.024, 0.014, 'capB')


def cap_back(mb, ctx):
    # worn backwards: the band across his forehead, the bill up behind
    Rc = RX(-3.0).to_3x3()
    edge = 62.0
    crown(mb, Rc, edge, 'capA')
    button(mb, Rc, CAP_INFL, 'capC')
    bill(mb, Rc, edge, 60.0, 0.140, -12.0, 0.024, 0.014, 'capB', fwd=90.0)
    # the snapback opening above the forehead: a dark arch, a strap across
    for k in range(2):
        pass
    b = math.radians(edge - 7)
    pts = [cap_pt(cap_dir(Rc, math.radians(-90 + 26 * t), b), CAP_INFL + 0.002) for t in (-1, -0.5, 0, 0.5, 1)]
    ctr = cap_pt(cap_dir(Rc, math.radians(-90), math.radians(edge - 13)), CAP_INFL + 0.002)
    c = mb.vert(ctr)
    ring = [mb.vert(p) for p in pts]
    for i in range(len(ring) - 1):
        mb.face((c, ring[i], ring[i + 1]), 'capD')
    tube(mb, [cap_pt(cap_dir(Rc, math.radians(-90 + 30 * t), math.radians(edge - 3)), CAP_INFL + 0.003)
              for t in (-1, -0.5, 0, 0.5, 1)], [0.007] * 5, 'capB', n=8)


def cap_beanie(mb, ctx):
    Rc = RX(-18.0).to_3x3()
    edge = 80.0
    infl = 0.030
    # knit ribs: fine ridges down the dome
    crown(mb, Rc, edge - 12, 'knit', infl=infl, n=72,
          bumps=lambda a, b: 0.0025 * math.cos(24 * a) * min(1.0, b / 0.4))
    # the folded cuff
    rows = []
    for i, (bb, e) in enumerate(((edge - 14, infl - 0.002), (edge - 13, infl + 0.010), (edge - 2, infl + 0.012),
                                  (edge, infl + 0.004), (edge, infl - 0.010))):
        rows.append([mb.vert(cap_pt(cap_dir(Rc, 2 * math.pi * k / 64, math.radians(bb)), e)) for k in range(64)])
    grid_faces(mb, rows, lambda i, k, c: 'knitB' if i < 3 else 'capD')
    # the pompom
    top, ax = button(mb, Rc, infl, 'knitB', r=0.001, h=0.001)
    ellipsoid(mb, top + ax * 0.040, (0.050, 0.050, 0.046), 'knitB', n=18, rings=12)


def cap_bucket(mb, ctx):
    Rc = RX(-22.0).to_3x3()
    edge = 70.0
    infl = 0.040
    crown(mb, Rc, edge, 'capA', infl=infl)
    # the band just above the brim
    rows = []
    for bb, e in ((edge - 12, infl), (edge - 12, infl + 0.004), (edge - 1, infl + 0.004), (edge - 1, infl)):
        rows.append([mb.vert(cap_pt(cap_dir(Rc, 2 * math.pi * k / 64, math.radians(bb)), e)) for k in range(64)])
    grid_faces(mb, rows, lambda i, k, c: 'capB')
    brim(mb, Rc, edge, 0.070, 22.0, 0.012, 'capB', 'capD', infl)
    button(mb, Rc, infl, 'capA', r=0.012, h=0.004)


def cap_propeller(mb, ctx):
    Rc = RX(-14.0).to_3x3()
    edge = 66.0
    infl = 0.028
    # six panels, alternating colours
    crown(mb, Rc, edge, 'capA', infl=infl, n=48,
          key_fn=lambda i, k, c: 'capA' if (k * 6 // 48) % 2 == 0 else 'capB')
    top, ax = button(mb, Rc, infl, 'capC', r=0.018, h=0.012)
    stem_top = top + ax * 0.050
    tube(mb, [top, stem_top], [0.008, 0.008], 'capC', n=10)
    ellipsoid(mb, stem_top, (0.014, 0.014, 0.010), 'capC', rot=frame_z(ax, V(1, 0, 0)).to_4x4(), n=10, rings=6)
    # two blades, spinning with the frame
    spin = ctx.get('spin', 0.0)
    F = frame_z(ax, V(1, 0, 0))
    for k in (0, 1):
        ang = math.radians(spin + 180 * k)
        dirv = F @ V(math.cos(ang), math.sin(ang), 0)
        c = stem_top + dirv * 0.060
        rot = frame_z(ax, dirv).to_4x4() @ RX(18)
        ellipsoid(mb, c, (0.058, 0.020, 0.005), 'capC', rot=rot, n=14, rings=6)


def cap_crown(mb, ctx):
    """A cartoon golden crown sitting on his hair: a band, five points with
    balls on top, gems round the band."""
    Rc = RX(-8.0).to_3x3()
    base = V(0, 0.008, 0.132)
    rx, ry = 0.158, 0.148
    n = 80
    th = 0.011
    ax = Rc @ V(0, 0, 1)
    ex, ey = Rc @ V(1, 0, 0), Rc @ V(0, 1, 0)

    def top_h(a):
        pk = (0.5 + 0.5 * math.cos(5 * (a + math.pi / 2))) ** 2.5
        return 0.070 + 0.070 * pk

    def pt(a, h, r_off=0.0, flare=0.12):
        k = 1 + flare * h / 0.14
        return base + ex * ((rx + r_off) * k * math.cos(a)) + ey * ((ry + r_off) * k * math.sin(a)) + ax * h
    hs = [0.0, 0.012, 0.030, 0.034]
    outer = [[mb.vert(pt(2 * math.pi * k / n, h)) for k in range(n)] for h in hs]
    outer.append([mb.vert(pt(2 * math.pi * k / n, top_h(2 * math.pi * k / n))) for k in range(n)])
    inner_top = [mb.vert(pt(2 * math.pi * k / n, top_h(2 * math.pi * k / n) - 0.004, -th)) for k in range(n)]
    inner_bot = [mb.vert(pt(2 * math.pi * k / n, 0.0, -th)) for k in range(n)]
    rows = outer + [inner_top, inner_bot]
    keys = ['L2gold', 'L2gold', 'L2gold', 'L1gold', 'L1gold', 'capD']
    grid_faces(mb, rows, lambda i, k, c: keys[i] if i < len(keys) else 'capD')
    grid_faces(mb, [inner_bot, outer[0]], lambda i, k, c: 'L2gold')
    for j in range(5):
        a = -math.pi / 2 + 2 * math.pi * j / 5
        ellipsoid(mb, pt(a, top_h(a) + 0.012), (0.017, 0.017, 0.017), 'L1gold', n=12, rings=8)
        g = pt(a, 0.017, 0.004)
        nrm = (ex * math.cos(a) / rx + ey * math.sin(a) / ry).normalized()
        ellipsoid(mb, g, (0.016, 0.016, 0.007), 'L3gem', rot=frame_z(nrm, ax).to_4x4(), n=12, rings=6)


CAPS = {'cap': cap_cap, 'back': cap_back, 'beanie': cap_beanie, 'bucket': cap_bucket,
        'propeller': cap_propeller, 'crown': cap_crown}


# --- back items (chest space) ----------------------------------------------

def rbox(mb, c, hx, hy, hz, key, e=4.0, n=28, rings=10, key_fn=None):
    """A rounded box: superellipse sections stacked along Z, rounded ends."""
    rows = []
    for i in range(rings + 1):
        t = -1 + 2 * i / rings
        k = (1 - abs(t) ** e) ** (1 / e)
        z = c.z + hz * t
        rows.append(ring_pts(V(c.x, c.y, z), V(1, 0, 0), V(0, 1, 0), hx * k + 1e-4, hx * k + 1e-4,
                             hy * k + 1e-4, hy * k + 1e-4, n, e))
    return loft(mb, rows, key_fn or (lambda i, kk, cc: key),
                cap0=V(c.x, c.y, c.z - hz), cap1=V(c.x, c.y, c.z + hz))


def straps(mb, key, s_list=(-1, 1)):
    for s in s_list:
        path = [V(s * 0.072, 0.118, 0.170), V(s * 0.084, 0.035, 0.200), V(s * 0.088, -0.068, 0.168),
                V(s * 0.096, -0.112, 0.070), V(s * 0.112, -0.104, -0.020), V(s * 0.126, -0.020, -0.062),
                V(s * 0.118, 0.100, -0.050)]
        pts, _ = catmull(path, 5)
        tube(mb, pts, [0.012] * len(pts), key, n=10)


def back_backpack(mb, ctx):
    c = V(0, 0.168, 0.080)
    rbox(mb, c, 0.118, 0.058, 0.128, 'L1')
    # the flap over the top
    rbox(mb, c + V(0, 0.010, 0.085), 0.122, 0.064, 0.050, 'L1')
    # front pocket and its zip
    rbox(mb, c + V(0, 0.056, -0.040), 0.085, 0.024, 0.062, 'L2')
    tube(mb, [c + V(-0.07, 0.084, 0.012), c + V(0.07, 0.084, 0.012)], [0.006, 0.006], 'L3', n=8)
    ellipsoid(mb, c + V(0.07, 0.088, 0.004), (0.010, 0.006, 0.016), 'L3', n=10, rings=6)
    # a grab loop on top
    pts, _ = catmull([c + V(-0.035, 0.0, 0.130), c + V(0, 0.0, 0.160), c + V(0.035, 0.0, 0.130)], 5)
    tube(mb, pts, [0.009] * len(pts), 'L2', n=8)
    straps(mb, 'L2')


def back_tank(mb, ctx):
    c = V(0, 0.150, 0.070)
    rbox(mb, c, 0.105, 0.045, 0.135, 'L1')
    # the tank on the frame
    tc = c + V(0, 0.070, 0.0)
    pts = [tc + V(0, 0, z) for z in (-0.125, -0.12, -0.10, 0.10, 0.12, 0.125)]
    tube(mb, pts, [0.030, 0.050, 0.056, 0.056, 0.050, 0.030], 'L3m', n=24)
    # glowing bands and lights (emissive)
    tube(mb, [tc + V(0, 0, 0.030), tc + V(0, 0, 0.052)], [0.0585, 0.0585], 'L4g', n=24, cap0=False, cap1=False)
    for x, z in ((-0.085, 0.10), (-0.085, 0.06), (0.085, 0.10)):
        ellipsoid(mb, c + V(x, 0.040, z), (0.012, 0.010, 0.012), 'L4g', n=10, rings=6)
    # dials and a vent (metal)
    ellipsoid(mb, c + V(0.06, 0.05, -0.08), (0.022, 0.012, 0.022), 'L3m', n=12, rings=6)
    straps(mb, 'L2')


def tank_hose(J, root):
    """The hose from the pack to a nozzle holstered at his right hip (in
    character space, so it follows chest and pelvis)."""
    mb = MB()
    ch, pel = J['chest'], J['pelvis']
    start = ch @ V(-0.070, 0.150, -0.060)
    noz = pel @ V(-0.165, -0.010, -0.030)
    mid1 = ch @ V(-0.120, 0.200, -0.140)
    mid2 = pel @ V(-0.190, 0.080, -0.060)
    pts, _ = catmull([start, mid1, mid2, noz + (pel.to_3x3() @ V(0, 0, 0.05))], 5)
    tube(mb, pts, [0.016] * len(pts), 'L2', n=10)
    R = pel.to_3x3()
    d = R @ V(0, -0.35, -1).normalized()
    top = noz + R @ V(0, 0, 0.05)
    tube(mb, [top, noz, noz + d * 0.07], [0.022, 0.022, 0.018], 'L3m', n=14)
    ellipsoid(mb, noz + d * 0.075, (0.016, 0.016, 0.008), 'L4g', rot=frame_z(d, V(1, 0, 0)).to_4x4(), n=10, rings=5)
    return to_object('l_back_tank_hose', mb, root)


def wing(mb, hinge, R, span, spread, key, bone, flip_x=1):
    """A little bat wing in a local frame (x out, z up), then R (3x3) and
    hinge. spread: 1 = open."""
    tips = [(0.235, 0.020), (0.175, -0.090), (0.085, -0.120)]
    wr = (0.125, 0.075)
    top = (0.215, 0.105)
    sc = span / 0.24

    def P(x, z):
        return hinge + R @ V(flip_x * x * sc, 0, z * sc)
    outline = [(0.0, 0.03), wr, top]
    prev = top
    for tx, tz in tips:
        # a scallop between the previous point and this tip
        for k in (1, 2):
            t = k / 3
            mx, mz = lerp(prev[0], tx, t), lerp(prev[1], tz, t)
            cx, cz = (wr[0] * 0.6 + mx * 0.4), (wr[1] * 0.6 + mz * 0.4)
            outline.append((lerp(mx, cx, 0.35 * math.sin(math.pi * t)), lerp(mz, cz, 0.35 * math.sin(math.pi * t))))
        outline.append((tx, tz))
        prev = (tx, tz)
    for k in (1, 2):
        t = k / 3
        mx, mz = lerp(prev[0], 0.0, t), lerp(prev[1], -0.04, t)
        outline.append((mx + 0.02 * math.sin(math.pi * t), mz + 0.03 * math.sin(math.pi * t)))
    ctr = mb.vert(P(0.10, 0.0))
    ring = [mb.vert(P(x, z)) for x, z in outline]
    th = R @ V(0, 0.006, 0)
    ctr2 = mb.vert(P(0.10, 0.0) + th)
    ring2 = [mb.vert(mb.v[i] + th) for i in ring]
    fan(mb, ctr, ring, key)
    fan(mb, ctr2, ring2, key, flip=True)
    grid_faces(mb, [ring, ring2], lambda i, k, c: key)
    # the arm along the leading edge and the fingers
    tube(mb, [P(0.0, 0.03), P(*wr), P(*top)], [0.012, 0.010, 0.006], bone, n=8)
    for tx, tz in tips:
        tube(mb, [P(*wr), P(tx, tz)], [0.007, 0.004], bone, n=6)
    ellipsoid(mb, P(*top), (0.010, 0.010, 0.012), 'L3', n=8, rings=5)


def back_wings(mb, ctx):
    flap = ctx.get('flap', 0.0)
    for s in (-1, 1):
        hinge = V(s * 0.045, 0.098, 0.120)
        # swept back, tilted up a little, flapping about the hinge
        R = (RZ(s * (32 - 18 * flap)) @ RY(-s * (8 + 22 * flap))).to_3x3()
        wing(mb, hinge, R, 0.25, 1.0, 'L1', 'L2', flip_x=s)


def cape(J, root, ctx):
    """A short hero cape to the knees, swept in chest space; it lifts and
    ripples with the pose (ctx['cape'] degrees of lift)."""
    mb = MB()
    ch = J['chest']
    lift = 6.0 + ctx.get('cape', 0.0)
    ph = ctx.get('phase', 0.0)
    NC, NR = 21, 12
    L = 0.36
    pivot = V(0, 0.08, 0.17)
    outer, inner = [], []
    for r in range(NR + 1):
        t = r / NR
        g = smoothstep(0.0, 0.28, t)
        hx = lerp(0.092, 0.180, g) + 0.045 * t
        hy = lerp(0.080, 0.125, g)
        z = 0.175 - t * L
        orow, irow = [], []
        for c in range(NC):
            u = (c / (NC - 1)) * 2 - 1
            phi = math.radians(78) * u
            x = hx * math.sin(phi)
            y = hy * math.cos(phi) + 0.022 + 0.010 * g
            # folds and a ripple at the hem
            y += 0.012 * math.sin(4.0 * phi + ph) * t
            zz = z + 0.010 * math.sin(5.0 * phi + ph * 1.3) * t * t
            p = V(x, y, zz)
            ang = math.radians(lift * (0.35 + 0.65 * t))
            p = pivot + (RX(ang).to_3x3() @ (p - pivot))
            n = V(x, y - 0.02, 0).normalized() if (x or y) else V(0, 1, 0)
            orow.append(mb.vert(ch @ p))
            irow.append(mb.vert(ch @ (p - n * 0.007)))
        outer.append(orow)
        inner.append(irow)
    for r in range(NR):
        for c in range(NC - 1):
            mb.face((outer[r][c], outer[r][c + 1], outer[r + 1][c + 1], outer[r + 1][c]), 'L1')
            mb.face((inner[r][c], inner[r + 1][c], inner[r + 1][c + 1], inner[r][c + 1]), 'L2')
    for c in range(NC - 1):    # hem and collar edges
        mb.face((outer[NR][c], inner[NR][c], inner[NR][c + 1], outer[NR][c + 1]), 'L1')
        mb.face((outer[0][c], outer[0][c + 1], inner[0][c + 1], inner[0][c]), 'L2')
    for c in (0, NC - 1):
        for r in range(NR):
            mb.face((outer[r][c], inner[r][c], inner[r + 1][c], outer[r + 1][c]), 'L1')
    # gold clasps at the collar's front corners
    for s in (-1, 1):
        ellipsoid(mb, ch @ V(s * 0.088, -0.018, 0.190), (0.018, 0.012, 0.018), 'L3m', n=10, rings=6)
    return to_object('l_back_cape', mb, root, recalc=False)


BACKS = {'backpack': back_backpack, 'cape': None, 'tank': back_tank, 'wings': back_wings}


# --- hand items (grip frame: -Y where it points, +Z up) ---------------------

def hand_flashlight(mb, ctx):
    # a chunky kid's flashlight: yellow body, black grip rings, a big head
    pts = [V(0, y, 0) for y in (0.090, 0.085, 0.040, -0.080, -0.100, -0.160, -0.172)]
    tube(mb, pts, [0.020, 0.029, 0.029, 0.029, 0.031, 0.048, 0.048],
         ['L1', 'L1', 'L1', 'L3m', 'L3m', 'L3m'], n=18)
    for y0 in (0.070, 0.045):
        tube(mb, [V(0, y0, 0), V(0, y0 - 0.012, 0)], [0.031, 0.031], 'L2', n=18)
    ellipsoid(mb, V(0, -0.173, 0), (0.041, 0.007, 0.041), 'L4g', n=16, rings=6)
    ellipsoid(mb, V(0, -0.02, 0.029), (0.010, 0.016, 0.006), 'L2', n=8, rings=5)    # the switch


def hand_torch(mb, ctx):
    fl = ctx.get('flicker', 0.0)
    pts = [V(0, 0, z) for z in (-0.12, 0.15, 0.17, 0.185, 0.195, 0.26)]
    tube(mb, pts, [0.020, 0.026, 0.030, 0.030, 0.038, 0.040], ['L1', 'L1', 'L3m', 'L2', 'L2'], n=14)
    # the flame: a big cartoon teardrop, flickering
    base = V(0, 0, 0.250)
    rings = []
    h = 0.20 * (1 + 0.10 * fl)
    for i in range(10):
        t = i / 9
        r = 0.060 * math.sin(math.pi * min(1.0, t * 1.4) ** 0.8) * (1 - t) ** 0.5 + 0.002
        c = base + V(0.018 * fl * t * t, -0.006 * t, h * t)
        rings.append(ring_pts(c, V(1, 0, 0), V(0, 1, 0), r, r, r, r, 16))
    loft(mb, rings, lambda i, k, c: 'L4g', cap0=base, cap1=base + V(0.018 * fl, -0.006, h * 1.03))


def hand_bucket(mb, ctx, M):
    """The pumpkin bucket hangs from his fist (gravity, character space)."""
    g = M.translation
    c = g + V(0, 0, -0.165)
    R = V(0.100, 0.100, 0.080)
    rows = []
    n = 36
    for i in range(1, 12):
        ph = math.pi * i / 12
        if ph < 0.45:
            continue
        z, sn = math.cos(ph), math.sin(ph)
        row = []
        for k in range(n):
            a = 2 * math.pi * k / n
            rib = 1 - 0.07 * (0.5 - 0.5 * math.cos(8 * a)) ** 2
            row.append(mb.vert(c + V(R.x * sn * math.cos(a) * rib, R.y * sn * math.sin(a) * rib, R.z * z)))
        rows.append(row)
    grid_faces(mb, rows, lambda i, k, cc: 'L1')
    fan(mb, mb.vert(c + V(0, 0, -R.z)), rows[-1], 'L1', flip=True)
    # the rim, dark inside
    top = rows[0]
    inner = [mb.vert(c + (mb.v[i] - c) * 0.86) for i in top]
    grid_faces(mb, [top, inner], lambda i, k, cc: 'L2')
    fan(mb, mb.vert(c + V(0, 0, R.z * 0.2)), inner, 'L2')
    # candy peeking out
    for x, y in ((0.02, 0.01), (-0.025, -0.015), (0.0, 0.03)):
        ellipsoid(mb, c + V(x, y, R.z * 0.85), (0.022, 0.022, 0.018), 'L3', n=10, rings=6)
    # the face, facing forwards: triangle eyes, a toothy grin (glow)
    fwd = M.to_3x3() @ V(0, -1, 0)
    fwd.z = 0
    fwd.normalize()
    side = fwd.cross(V(0, 0, 1))

    def on(u, v, off=0.004):
        d = (fwd * math.cos(u) * math.cos(v) + side * math.sin(u) * math.cos(v) + V(0, 0, math.sin(v)))
        return c + V(d.x * R.x, d.y * R.y, d.z * R.z) * (1 + off / 0.08)
    for s in (-1, 1):
        tri = [on(s * 0.30, 0.16), on(s * 0.62, 0.02), on(s * 0.10, 0.00)]
        ids = [mb.vert(p) for p in tri]
        mb.face(ids, 'L4g')
    mouth = [(-0.55, -0.20), (-0.3, -0.12), (0, -0.16), (0.3, -0.12), (0.55, -0.20),
             (0.3, -0.38), (0, -0.42), (-0.3, -0.38)]
    mc = mb.vert(on(0, -0.26))
    ids = [mb.vert(on(u, v)) for u, v in mouth]
    fan(mb, mc, ids, 'L4g')
    # the handle: an arc from the fist to the rim
    for s in (-1, 1):
        pass
    arc = [c + side * 0.080 + V(0, 0, 0.050), g + side * 0.030, g - side * 0.030, c - side * 0.080 + V(0, 0, 0.050)]
    pts, _ = catmull(arc, 5)
    tube(mb, pts, [0.007] * len(pts), 'L2', n=8)


def hand_balloon(ctx, M, root):
    """The balloon floats ~0.5 m above his fist on a string (character space,
    always upright)."""
    mb = MB()
    g = M.translation
    sway = ctx.get('sway', 0.0)
    bc = g + V(-0.09 - 0.03 * sway, 0.16, 0.76)     # above his right shoulder, a little behind
    rows = []
    n = 24
    for i in range(1, 16):
        ph = math.pi * i / 16
        z, sn = math.cos(ph), math.sin(ph)
        k = 1 - 0.18 * max(0.0, -z) ** 2          # a little pointy at the bottom
        rows.append([mb.vert(bc + V(0.120 * sn * k * math.cos(2 * math.pi * j / n),
                                    0.120 * sn * k * math.sin(2 * math.pi * j / n), 0.138 * z)) for j in range(n)])
    grid_faces(mb, rows, lambda i, k, c: 'L1')
    fan(mb, mb.vert(bc + V(0, 0, 0.138)), rows[0], 'L1')
    knot = bc + V(0, 0, -0.140)
    fan(mb, mb.vert(knot), rows[-1], 'L1', flip=True)
    ellipsoid(mb, knot + V(0, 0, -0.008), (0.014, 0.014, 0.012), 'L1', n=10, rings=6)
    mid = lerp(g, knot, 0.5) + V(-0.03 * (1 - sway), 0.02, 0)
    pts, _ = catmull([g, mid, knot], 8)
    tube(mb, pts, [0.006] * len(pts), 'L2', n=6)
    return to_object('l_hand_balloon', mb, root)


HANDS = {'flashlight': hand_flashlight, 'torch': hand_torch, 'balloon': None, 'bucket': None}

LAYERS = ([('cap', k) for k in CAPS] + [('back', k) for k in BACKS] + [('hand', k) for k in HANDS])


def build_layer(kind, style, J, root, ctx):
    mb = MB()
    if kind == 'cap':
        CAPS[style](mb, ctx)
        return [to_object('l_cap_%s' % style, mb, root @ J['head'])]
    if kind == 'back':
        if style == 'cape':
            return [cape(J, root, ctx)]
        BACKS[style](mb, ctx)
        out = [to_object('l_back_%s' % style, mb, root @ bone_delta(J['chest'], V(0, 0, 0)))]
        if style == 'tank':
            out.append(tank_hose(J, root))
        return out
    if kind == 'hand':
        M = J['tuckR'] if ctx.get('hand') == 'tuck' else J['gripR']
        if style == 'balloon':
            return [hand_balloon(ctx, M, root)]
        if style == 'bucket':
            hand_bucket(mb, ctx, M)
            return [to_object('l_hand_bucket', mb, root)]
        HANDS[style](mb, ctx)
        return [to_object('l_hand_%s' % style, mb, root @ M)]
    raise KeyError(kind)


# ---------------------------------------------------------------------------
# Tommy's animations (SPEC s.4). Each returns the pose of frame i; ctx()
# gives what the layers need (cape lift, wing flap, spinning propeller...).
# Arms: (flex forwards, abduct outwards, elbow bend, forearm roll) in degrees,
# relative to the chest. Legs: (dx, dy, dz, toe-down, glued to the ground).
# ---------------------------------------------------------------------------

def anim_idle(i):
    bob = [0.0, -0.005, -0.009, -0.004][i]
    arm = [(6, 11, 18), (5, 12, 20), (4, 13, 22), (5, 12, 20)][i]
    return P(pz=bob, sp=[0, 0.6, 1.0, 0.5][i], hp=-12 + [0, 0.5, 1.2, 0.5][i],
             armR=arm, armL=arm, eyes='closed' if i == 2 else 'open')


def anim_hop(i):
    return [
        # 00 crouch: knees bent, leaning in, arms swung back, eyes on the target
        P(pz=-0.085, pp=8, sp=2, hp=-22, armR=(-55, 28, 28), armL=(-55, 28, 28), sq=(1.06, 0.93)),
        # 01 push-off: stretched tall on his toes, arms swinging up
        P(pz=0.030, pp=3, sp=-4, hp=-14, armR=(95, 14, 25), armL=(95, 14, 25),
          legR=(0, 0.01, 0.0, 40, True), legL=(0, 0.01, 0.0, 40, True), sq=(0.96, 1.05)),
        # 02 tuck: knees up, arms up
        P(pz=0.030, pp=-8, sp=-2, hp=-8, armR=(150, 24, 25), armL=(150, 24, 25),
          legR=(0.005, -0.06, 0.19, 25, False), legL=(-0.005, -0.06, 0.19, 25, False), mouth='grin'),
        # 03 tuck, the top: tighter
        P(pz=0.030, pp=-10, sp=-2, hp=-6, armR=(160, 30, 35), armL=(160, 30, 35),
          legR=(0.005, -0.075, 0.23, 20, False), legL=(-0.005, -0.075, 0.23, 20, False), mouth='grin'),
        # 04 legs reaching down, arms out for balance
        P(pz=0.012, pp=2, sp=0, hp=-10, armR=(40, 62, 20), armL=(40, 62, 20),
          legR=(0, -0.01, 0.035, 22, False), legL=(0, -0.01, 0.035, 22, False)),
        # 05 landing squash
        P(pz=-0.066, pp=6, sp=4, hp=-20, armR=(22, 40, 32), armL=(22, 40, 32), sq=(1.07, 0.91)),
    ][i]


def anim_super(i):
    tuck = lambda dy, dz, toe: (0.0, dy, dz, toe, False)   # noqa: E731
    return [
        # 00 deep crouch, arms far back
        P(pz=-0.120, pp=14, sp=6, hp=-26, armR=(-62, 30, 30), armL=(-62, 30, 30), sq=(1.08, 0.90)),
        # 01 launch: stretched, arms high over his head
        P(pz=0.045, pp=-2, sp=-6, hp=-18, armR=(172, 16, 8), armL=(172, 16, 8),
          legR=(0, 0.015, 0.0, 48, True), legL=(0, 0.015, 0.0, 48, True), sq=(0.93, 1.08), mouth='grin'),
        # 02 tight tuck, starting to roll forwards
        P(pz=0.050, pp=-6, sp=4, hp=-6, flip=18, armR=(75, 28, 70), armL=(75, 28, 70),
          legR=tuck(-0.10, 0.25, 30), legL=tuck(-0.10, 0.25, 30), eyes='happy', mouth='grin'),
        # 03 the tuck pitched forwards ~40 deg: a front-flip feel
        P(pz=0.050, pp=-6, sp=6, hp=0, flip=40, armR=(70, 26, 80), armL=(70, 26, 80),
          legR=tuck(-0.11, 0.28, 34), legL=tuck(-0.11, 0.28, 34), eyes='happy', mouth='grin'),
        # 04 opening up, arms out, legs reaching down
        P(pz=0.020, pp=0, sp=-2, hp=-12, flip=10, armR=(100, 70, 15), armL=(100, 70, 15),
          legR=(0, -0.02, 0.06, 26, False), legL=(0, -0.02, 0.06, 26, False), mouth='grin'),
        # 05 landing crouch
        P(pz=-0.095, pp=10, sp=5, hp=-22, armR=(25, 45, 35), armL=(25, 45, 35), sq=(1.08, 0.90)),
    ][i]


def anim_push(i):
    # leaning into a crate in front (the next cell), hands at chest height,
    # legs driving: a loop of two steps
    back, fwd = (0.0, 0.17, 0.0, 34, True), (0.0, -0.05, 0.0, 0, True)
    lift_b = (0.0, 0.07, 0.045, 18, False)
    legs = [(back, fwd), (lift_b, fwd), (fwd, back), (fwd, lift_b)][i]
    bob = [-0.030, -0.018, -0.030, -0.018][i]
    tw = [2.0, 0.0, -2.0, 0.0][i]
    return P(pz=bob, py=-0.05, pp=26, sp=6, st=tw, hp=-34, armR=(56, 12, 16), armL=(56, 12, 16),
             legR=legs[0], legL=legs[1], mouth='grin' if i % 2 else 'smile', hand='tuck')


def anim_use(i):
    # the lever stands in the cell in front of him: reach forwards and down,
    # grab it, pull it towards himself, straighten up
    return [
        P(pz=-0.030, pp=22, sp=6, hp=-28, armR=(32, 12, 6), armL=(32, 12, 6),
          legL=(0, -0.04, 0, 0, True), legR=(0, 0.06, 0, 10, True), hand='tuck'),
        P(pz=-0.045, pp=26, sp=8, hp=-30, armR=(28, 9, 4), armL=(28, 9, 4),
          legL=(0, -0.04, 0, 0, True), legR=(0, 0.06, 0, 10, True), hand='tuck', mouth='o'),
        P(pz=-0.040, pp=-8, sp=-4, hp=-18, armR=(40, 12, 70), armL=(40, 12, 70),
          legL=(0, -0.05, 0, 0, True), legR=(0, 0.10, 0, 22, True), hand='tuck', mouth='grin'),
        P(pz=-0.010, pp=2, sp=0, hp=-14, armR=(14, 14, 36), armL=(14, 14, 36), hand='tuck'),
    ][i]


def anim_win(i):
    return [
        # 00 crouch, ready to jump for joy
        P(pz=-0.075, pp=8, hp=-22, armR=(-30, 22, 40), armL=(-30, 22, 40), eyes='happy', mouth='grin',
          sq=(1.05, 0.94)),
        # 01 jump with a fist pump
        P(pz=0.040, pp=-3, sp=-5, hp=-20, armR=(175, 12, 12), armL=(20, 40, 70),
          legR=(0, 0.01, 0, 42, True), legL=(0, 0.01, 0, 42, True), eyes='happy', mouth='grin', sq=(0.95, 1.06)),
        # 02 in the air, knees up, fist high
        P(pz=0.040, pp=-6, sp=-4, hp=-18, armR=(172, 18, 20), armL=(-15, 48, 70),
          legR=(0.005, -0.05, 0.17, 22, False), legL=(-0.005, -0.04, 0.12, 22, False), eyes='happy', mouth='grin'),
        # 03 landing
        P(pz=-0.060, pp=6, hp=-20, armR=(140, 18, 40), armL=(20, 40, 40), eyes='happy', mouth='grin',
          sq=(1.06, 0.92)),
        # 04-07 waving at the camera with his left hand (the side nearer the camera)
        # (chibi arms: the upper arm goes out sideways, the forearm up, swaying)
        P(pz=0.0, hp=-16, hr=-4, armR=(8, 12, 30), armL=(12, 112, 0, 60), eyes='open', mouth='grin'),
        P(pz=-0.004, hp=-16, hr=-6, armR=(8, 12, 30), armL=(12, 114, 0, 72), eyes='happy', mouth='grin'),
        P(pz=0.0, hp=-16, hr=-4, armR=(8, 12, 30), armL=(12, 112, 0, 44), eyes='open', mouth='grin'),
        P(pz=-0.004, hp=-16, hr=-6, armR=(8, 12, 30), armL=(12, 114, 0, 72), eyes='happy', mouth='grin'),
    ][i]


def anim_hurt(i):
    return [
        # 00 startled: jumping back, arms up, eyes wide
        P(pz=0.030, pp=-8, sp=-8, hp=-20, flip=-12, armR=(120, 58, 30), armL=(120, 58, 30),
          legR=(0, 0.05, 0.05, 20, False), legL=(0, 0.02, 0.07, 20, False),
          eyes='wide', brows='raised', mouth='o'),
        # 01 in the air, backwards
        P(pz=0.040, pp=-10, sp=-8, hp=-18, flip=-20, armR=(140, 72, 40), armL=(140, 72, 40),
          legR=(0, 0.02, 0.11, 25, False), legL=(0, -0.02, 0.09, 25, False),
          eyes='wide', brows='raised', mouth='o'),
        # 02 lands, arms out
        P(pz=-0.050, pp=4, hp=-16, flip=-4, armR=(60, 62, 30), armL=(60, 62, 30),
          eyes='wide', brows='raised', mouth='o', sq=(1.05, 0.93)),
        # 03-05 dizzy wobble (the watch adds the stars)
        P(pz=-0.010, sr=-6, hp=-10, hr=-14, hy=8, armR=(10, 26, 20), armL=(10, 30, 26),
          legL=(0.01, 0, 0, 0, True), eyes='dizzy', brows='raised', mouth='o'),
        P(pz=-0.015, sr=0, hp=-8, hr=2, hy=0, armR=(12, 30, 26), armL=(12, 30, 26),
          eyes='dizzy', brows='raised', mouth='o'),
        P(pz=-0.010, sr=6, hp=-10, hr=14, hy=-8, armR=(10, 30, 26), armL=(10, 26, 20),
          legR=(-0.01, 0, 0, 0, True), eyes='dizzy', brows='raised', mouth='o'),
    ][i]


def anim_sink(i):
    # sinking: arms up flailing, head tilted up (a loop; the watch lowers him)
    a = [((165, 40, 30), (130, 55, 60)), ((150, 50, 45), (160, 38, 25)),
         ((130, 55, 60), (165, 40, 30)), ((160, 38, 25), (150, 50, 45))][i]
    kick = [(0.0, -0.05, 0.05), (0.0, 0.03, 0.02), (0.0, 0.05, 0.03), (0.0, -0.03, 0.05)][i]
    return P(pz=0.0, sp=-6, hp=-34, hr=[-6, 0, 6, 0][i], armR=a[0], armL=a[1],
             legR=(kick[0], kick[1], kick[2], 30, False), legL=(kick[0], -kick[1], 0.07 - kick[2], 30, False),
             eyes='wide', brows='raised', mouth='o')


def anim_fall(i):
    # falling down a pit: spread like a star, surprised
    w = [0, 6, -4][i]
    return P(pz=0.03, sp=-4, hp=-26, armR=(18, 112 + w, 12), armL=(18, 112 - w, 12),
             legR=(-0.09, 0.02, 0.08 + 0.01 * i, 20, False), legL=(0.09, 0.02, 0.09 - 0.01 * i, 20, False),
             eyes='wide', brows='raised', mouth='o', flip=-6)


def anim_turn(i):
    return anim_idle(0)


ANIMS = {   # name: (dirs, frames, ms, pose fn)
    'idle': ('nesw', 4, 250, anim_idle),
    'hop': ('nesw', 6, 30, anim_hop),
    'super': ('nesw', 6, 60, anim_super),
    'push': ('nesw', 4, 120, anim_push),
    'use': ('nesw', 4, 100, anim_use),
    'win': ('s', 8, 100, anim_win),
    'hurt': ('s', 6, 100, anim_hurt),
    'sink': ('s', 4, 150, anim_sink),
    'fall': ('s', 3, 120, anim_fall),
}
YAW = {'s': 0.0, 'e': 90.0, 'n': 180.0, 'w': -90.0}

# what the cape does per animation frame (degrees of lift behind him)
CAPE_LIFT = {
    'idle': [0, 1, 2, 1], 'hop': [8, 25, 45, 50, 30, 10], 'super': [10, 35, 60, 70, 45, 12],
    'push': [18, 16, 18, 16], 'use': [8, 10, 4, 4], 'win': [6, 30, 50, 12, 2, 4, 2, 4],
    'hurt': [5, 0, 10, 4, 6, 4], 'sink': [20, 26, 20, 26], 'fall': [70, 74, 72], 'turn': [0] * 12}


def layer_ctx(anim, i, n, p):
    ph = 2 * math.pi * i / max(1, n)
    flap_amp = {'idle': 0.25, 'turn': 0.0, 'push': 0.2, 'use': 0.2}.get(anim, 0.8)
    return dict(
        cape=CAPE_LIFT.get(anim, [0] * 12)[i % len(CAPE_LIFT.get(anim, [0]))],
        phase=ph,
        flap=flap_amp * math.sin(ph),
        spin=(i * 57.0) % 360 if anim != 'turn' else 20.0,
        flicker=math.sin(ph * 2 + 0.7) if anim != 'turn' else 0.0,
        sway=0.5 + 0.5 * math.sin(ph) if anim != 'turn' else 0.5,
        hand=p.get('hand'))


# ---------------------------------------------------------------------------
# Pets. Built facing -Y, root on the ground, ~0.40 m, chibi. Ids: 1 fur A,
# 2 fur B (belly, ears inside, wing membrane), 3 dark, 4 white, 5 collar,
# 6 collar tag.
# ---------------------------------------------------------------------------

def pet_pose(pet, anim, i):
    q = dict(bob=0.0, lift=0.0, pitch=0.0, fl=0.0, bl=0.0, tail=0.0, ear=0.0, flap=0.0, sq=(1.0, 1.0))
    if anim in ('idle', 'turn'):
        i = 0 if anim == 'turn' else i
        q.update(bob=[0.0, -0.006][i], tail=[-12, 16][i], ear=[0, 4][i], flap=[0.9, -0.6][i])
        if pet == 'bat':
            q['bob'] = [0.0, -0.025][i]
    elif anim == 'hop':
        if pet == 'bat':
            q.update(flap=[1.0, 0.0, -0.9, 0.2][i], bob=[-0.02, 0.02, 0.05, 0.01][i], pitch=[0, -6, -4, 2][i])
        else:
            q.update([
                dict(lift=-0.025, pitch=-6, fl=-8, bl=18, tail=10, ear=-6, sq=(1.05, 0.93)),
                dict(lift=0.020, pitch=16, fl=42, bl=-40, tail=-25, ear=14, sq=(0.96, 1.05)),
                dict(lift=0.030, pitch=4, fl=28, bl=22, tail=-10, ear=20),
                dict(lift=-0.015, pitch=-10, fl=-12, bl=28, tail=15, ear=-4, sq=(1.05, 0.94)),
            ][i])
    return q


def sph_decal(mb, c, R, u, v, au, av, key, off, nr=3, na=18):
    """A decal on a sphere (centre c, radius R) or an ellipsoid (R = (rx, ry,
    rz)) at (u, v) (u from -Y towards +X), off above the surface."""
    def pt(uu, vv):
        if isinstance(R, tuple):
            q = ell_pt(hdir(uu, vv), R[0], R[1], R[2], R[2])
            return c + q + ell_nrm(q, R[0], R[1], R[2], R[2]) * off
        return c + hdir(uu, vv) * (R + off)
    cc = mb.vert(pt(u, v))
    rows = []
    for i in range(1, nr + 1):
        rho = i / nr
        rows.append([mb.vert(pt(u + au * rho * math.cos(2 * math.pi * k / na) / math.cos(v),
                                v + av * rho * math.sin(2 * math.pi * k / na))) for k in range(na)])
    fan(mb, cc, rows[0], key)
    grid_faces(mb, rows, lambda i, k, x: key)


def pet_matrix(root, q, pivot):
    """Lift and pitch (nose up = +) about the body's centre."""
    return root @ T(0, 0, q['lift']) @ T(pivot) @ RX(-q['pitch']) @ T(-pivot)


def legs4(mb, q, bob, att_z, xs, ys, length, rad, paw_key, paw=(0.036, 0.044, 0.026)):
    for sx in (-1, 1):
        for y, ang in ((ys[0], q['fl']), (ys[1], q['bl'])):
            top = V(sx * xs, y, att_z + bob)
            d = RX(-ang).to_3x3() @ V(0, 0, -1)
            L = length + (0.0 if ang == 0 else -0.01)
            bot = top + d * L
            tube(mb, [top, bot], [rad, rad * 0.95], 'petA', n=14)
            ellipsoid(mb, bot + V(0, -0.010, -0.006), paw, paw_key, rot=RX(-ang * 0.6), n=14, rings=8)


def collar(mb, cc, rx, ry, tag_off, bell=False):
    ring = []
    for k in range(29):
        a = 2 * math.pi * k / 28
        ring.append(cc + V(rx * math.cos(a), ry * math.sin(a), -0.014 * math.sin(a)))
    tube(mb, ring, [0.016] * len(ring), 'petCollar', n=10, cap0=False, cap1=False)
    if bell:
        ellipsoid(mb, cc + tag_off, (0.020, 0.020, 0.020), 'petTag', n=12, rings=8)
    else:
        ellipsoid(mb, cc + tag_off, (0.022, 0.008, 0.024), 'petTag', n=12, rings=8)


def build_dog(q, root):
    """A round chibi puppy, 0.40 m: a big head on a small bean body."""
    objs = []
    bob = q['bob']
    Mq = pet_matrix(root, q, V(0, 0.03, 0.17))
    mb = MB()
    bc = V(0, 0.050, 0.140 + bob)

    def body_key(p):
        d = p - bc
        return 'petB' if (d.z < -0.030 and abs(d.x) < 0.06) or (d.y < -0.06 and abs(d.x) < 0.06) else 'petA'
    ellipsoid(mb, bc, (0.092, 0.118, 0.088), body_key, n=28, rings=16)
    legs4(mb, q, bob, 0.12, 0.052, (-0.020, 0.105), 0.085, 0.033, 'petB')
    tb = V(0, 0.160, 0.175 + bob)
    ang = math.radians(q['tail'])
    tip = tb + V(math.sin(ang) * 0.05, 0.040, 0.080)
    limb(mb, [tb, tb + V(math.sin(ang) * 0.02, 0.030, 0.045), tip], [0.026, 0.022, 0.015],
         lambda t, p: 'petB' if t > 1.6 else 'petA', per=5, n=12)
    objs.append(to_object('d_body', mb, Mq))
    HR = 0.132
    hc = V(0, -0.050, 0.268 + bob * 1.4)
    mb = MB()

    def head_key(p):
        d = (p - hc).normalized()
        return 'petB' if (d.y < -0.25 and d.z < 0.05 - 0.25 * abs(d.x)) or d.z < -0.55 else 'petA'
    ellipsoid(mb, hc, (HR * 1.06, HR * 0.97, HR * 0.97), head_key, n=40, rings=22)
    mc = hc + V(0, -0.110, -0.034)
    ellipsoid(mb, mc, (0.060, 0.050, 0.042), 'petB', n=22, rings=12)
    ellipsoid(mb, mc + V(0, -0.040, 0.026), (0.023, 0.016, 0.017), 'petDark', n=14, rings=8)
    for sd in (-1, 1):
        base = hc + V(sd * 0.120, -0.010, 0.072)
        rot = (RZ(sd * 12) @ RY(-sd * (16 + q['ear'])) @ RX(-12)).to_3x3()
        c = base + rot @ V(0, 0, -0.056)
        ellipsoid(mb, c, (0.027, 0.056, 0.078), 'petA', rot=rot.to_4x4(), n=16, rings=10)
    objs.append(to_object('d_head', mb, Mq))
    mb = MB()
    Rf = (HR * 1.06, HR * 0.97, HR * 0.97)
    for sd in (-1, 1):
        sph_decal(mb, hc, Rf, sd * 0.40, 0.33, 0.13, 0.19, 'petDark', 0.004)
        sph_decal(mb, hc, Rf, sd * 0.40 - 0.050, 0.42, 0.050, 0.060, 'petWhite', 0.007, nr=2, na=12)
    objs.append(to_object('d_face', mb, Mq, recalc=False))
    mb = MB()
    collar(mb, V(0, -0.035, 0.178 + bob * 1.2), 0.080, 0.074, V(0, -0.090, -0.032))
    objs.append(to_object('d_collar', mb, Mq))
    return objs


def ear_cone(mb, base, axis, w, d, h, key, inner_key=None, fwd=V(0, -1, 0)):
    """A pointed ear: flattened rings shrinking to a tip along axis."""
    axis = axis.normalized()
    xa = axis.cross(fwd).normalized()
    ya = xa.cross(axis).normalized()
    rings = []
    for i in range(7):
        t = i / 6
        k = (1 - t) ** 0.9
        rings.append(ring_pts(base + axis * (h * t), xa, ya, w * k + 1e-4, w * k + 1e-4, d * k + 1e-4,
                              d * k + 1e-4, 14))
    loft(mb, rings, lambda i, kk, c: key, cap0=base - axis * 0.01, cap1=base + axis * (h * 1.02))
    if inner_key:
        inner = []
        b2 = base + axis * (h * 0.12) - ya * (d * 0.75)
        for i in range(6):
            t = i / 5
            k = (1 - t) ** 0.9
            inner.append(ring_pts(b2 + axis * (h * 0.72 * t), xa, ya, w * 0.55 * k + 1e-4, w * 0.55 * k + 1e-4,
                                  0.006 * k + 1e-4, 0.006 * k + 1e-4, 12))
        loft(mb, inner, lambda i, kk, c: inner_key, cap0=b2, cap1=b2 + axis * (h * 0.74))


def build_cat(q, root):
    """A black kitten with a big head, pointed ears, big eyes and a long tail."""
    objs = []
    bob = q['bob']
    Mq = pet_matrix(root, q, V(0, 0.03, 0.16))
    mb = MB()
    bc = V(0, 0.055, 0.128 + bob)

    def body_key(p):
        d = p - bc
        return 'petB' if (d.z < -0.035 and abs(d.x) < 0.05) or (d.y < -0.07 and abs(d.x) < 0.045) else 'petA'
    ellipsoid(mb, bc, (0.074, 0.108, 0.078), body_key, n=26, rings=14)
    legs4(mb, q, bob, 0.11, 0.042, (-0.010, 0.115), 0.080, 0.025, 'petA', paw=(0.028, 0.034, 0.020))
    sw = math.radians(q['tail'])
    tail = [V(0, 0.150, 0.150 + bob), V(0.02 * math.sin(sw), 0.215, 0.215), V(0.05 * math.sin(sw), 0.235, 0.320),
            V(0.03 + 0.07 * math.sin(sw), 0.200, 0.395)]
    limb(mb, tail, [0.022, 0.020, 0.018, 0.016], 'petA', per=6, n=12)
    objs.append(to_object('c_body', mb, Mq))
    HR = 0.132
    hc = V(0, -0.040, 0.262 + bob * 1.4)
    mb = MB()
    ellipsoid(mb, hc, (HR * 1.10, HR * 0.96, HR * 0.92), 'petA', n=40, rings=22)
    # a lighter muzzle so the face reads on black fur
    mc = hc + V(0, -0.112, -0.040)
    for s in (-1, 1):
        ellipsoid(mb, mc + V(s * 0.024, 0, 0), (0.032, 0.026, 0.026), 'petB', n=14, rings=8)
    ellipsoid(mb, mc + V(0, -0.024, 0.020), (0.014, 0.010, 0.010), 'petDark', n=10, rings=6)
    for s in (-1, 1):
        base = hc + V(s * 0.082, 0.004, 0.088)
        axis = V(s * 0.42, -0.05, 1.0)
        ear_cone(mb, base, axis, 0.048, 0.022, 0.095, 'petA', 'petB')
    objs.append(to_object('c_head', mb, Mq))
    mb = MB()
    Rf = (HR * 1.10, HR * 0.96, HR * 0.92)
    for sd in (-1, 1):
        # big eyes: coloured (id 4 in the palette) with a pupil and a glint
        sph_decal(mb, hc, Rf, sd * 0.42, 0.24, 0.20, 0.27, 'petWhite', 0.003)
        sph_decal(mb, hc, Rf, sd * 0.40, 0.22, 0.085, 0.19, 'petDark', 0.005)
        sph_decal(mb, hc, Rf, sd * 0.40 - 0.035, 0.31, 0.040, 0.050, 'petWhite', 0.007, nr=2, na=12)
    objs.append(to_object('c_face', mb, Mq, recalc=False))
    mb = MB()
    collar(mb, V(0, -0.028, 0.170 + bob * 1.2), 0.068, 0.064, V(0, -0.078, -0.032), bell=True)
    objs.append(to_object('c_collar', mb, Mq))
    return objs


def build_bat(q, root):
    """A baby bat flying ~0.6 m up (the anchor is the ground below it)."""
    objs = []
    z0 = 0.60 + q['bob']
    Mq = pet_matrix(root, q, V(0, 0, z0))
    bc = V(0, 0, z0)
    mb = MB()

    def body_key(p):
        d = p - bc
        return 'petB' if d.y < -0.05 and d.z < -0.02 and abs(d.x) < 0.05 else 'petA'
    ellipsoid(mb, bc, (0.100, 0.090, 0.095), body_key, n=32, rings=18)
    for s in (-1, 1):
        ear_cone(mb, bc + V(s * 0.052, 0.005, 0.070), V(s * 0.35, 0.02, 1.0), 0.036, 0.016, 0.085, 'petA', 'petB')
        # tiny feet
        ellipsoid(mb, bc + V(s * 0.030, 0.02, -0.092), (0.014, 0.018, 0.012), 'petDark', n=10, rings=6)
        # fangs
        ellipsoid(mb, bc + V(s * 0.016, -0.086, -0.034), (0.007, 0.006, 0.013), 'petWhite', n=8, rings=5)
    objs.append(to_object('b_body', mb, Mq))
    mb = MB()
    flap = q['flap']
    for s in (-1, 1):
        hinge = bc + V(s * 0.080, 0.010, 0.010)
        R = (RZ(s * 34) @ RY(-s * (-10 + 38 * flap))).to_3x3()      # swept back: reads from the side too
        wing(mb, hinge, R, 0.24, 1.0, 'petB', 'petA', flip_x=s)
    objs.append(to_object('b_wings', mb, Mq, recalc=False))
    mb = MB()
    Rf = (0.100, 0.090, 0.095)
    for sd in (-1, 1):
        sph_decal(mb, bc, Rf, sd * 0.42, 0.26, 0.23, 0.30, 'petDark', 0.004)
        sph_decal(mb, bc, Rf, sd * 0.42 - 0.08, 0.38, 0.085, 0.095, 'petWhite', 0.007, nr=2, na=12)
    objs.append(to_object('b_face', mb, Mq, recalc=False))
    mb = MB()
    collar(mb, bc + V(0, -0.012, -0.050), 0.080, 0.074, V(0, -0.082, -0.022))
    objs.append(to_object('b_collar', mb, Mq))
    return objs


PETS = {'dog': build_dog, 'cat': build_cat, 'bat': build_bat}
PET_ANIMS = {'idle': ('nesw', 2, 300), 'hop': ('nesw', 4, 45)}
PET_HEIGHT = {'dog': 0.40, 'cat': 0.40, 'bat': 0.40}


# ---------------------------------------------------------------------------
# Jobs and the render loop
# ---------------------------------------------------------------------------

FLIP_PIVOT = 0.45


def root_matrix(yaw, sq=(1.0, 1.0), flip=0.0):
    """Character space -> world: at the anchor, turned by yaw, squashed
    about the ground, and (airborne frames) pitched about his middle."""
    A = C.cell(0, 0, 0)
    S = Matrix.Diagonal((sq[0], sq[0], sq[1], 1.0))
    F = T(0, 0, FLIP_PIVOT) @ RX(flip) @ T(0, 0, -FLIP_PIVOT) if flip else Matrix.Identity(4)
    return T(A) @ RZ(yaw) @ F @ S


def jobs(sample):
    """Every frame to build: (who, anim, dir, frame, frames, ms)."""
    out = []
    if sample:
        tom = [('idle', d, 0) for d in 'nesw'] + [('hop', d, i) for d in 'se' for i in range(6)]
        for anim, d, i in tom:
            dirs, n, ms, _ = ANIMS[anim]
            out.append(('tommy', anim, d, i, n, ms))
        out.append(('pet_dog', 'idle', 's', 0, 2, 300))
        return out
    for anim, (dirs, n, ms, _) in ANIMS.items():
        for d in dirs:
            for i in range(n):
                out.append(('tommy', anim, d, i, n, ms))
    for i in range(12):
        out.append(('tommy', 'turn', '', i, 12, 0))
    for pet in PETS:
        for anim, (dirs, n, ms) in PET_ANIMS.items():
            for d in dirs:
                for i in range(n):
                    out.append(('pet_' + pet, anim, d, i, n, ms))
        for i in range(12):
            out.append(('pet_' + pet, 'turn', '', i, 12, 0))
    return out


def frame_name(who, anim, d, i):
    return '%s_%s_%s_%02d' % (who, anim, d, i) if d else '%s_%s_%02d' % (who, anim, i)


def layer_name(kind, style, anim, d, i):
    return '%s_%s_%s' % (kind, style, frame_name('x', anim, d, i)[2:])


def selected(name, pats):
    if not pats:
        return True
    return any(fnmatch.fnmatch(name, p) or name.startswith(p) for p in pats)


def main():
    argv = sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else []
    sample = '--style-sample' in argv
    listing = '--list' in argv
    for f in ('--style-sample', '--list'):
        while f in sys.argv:
            sys.argv.remove(f)
    a = C.args()
    C.reset('neutral', cpu=a.cpu)
    register_materials()
    todo = jobs(sample)
    layers = LAYERS if not sample else [('cap', 'cap')]
    t_all = time.time()
    count, times = 0, {}
    for who, anim, d, i, n, ms in todo:
        name = frame_name(who, anim, d, i)
        want_layers = []
        if who == 'tommy':
            for kind, style in layers:
                ln = layer_name(kind, style, anim, d, i)
                if selected(ln, a.only):
                    want_layers.append((kind, style, ln))
        want_body = selected(name, a.only)
        if not want_body and not want_layers:
            continue
        if listing:
            print(name if want_body else '(' + name + ')', len(want_layers), 'layers')
            count += int(want_body) + len(want_layers)
            continue
        zoom = 2.0 if anim == 'turn' else 1.0
        yaw = 30.0 * i if anim == 'turn' else YAW[d]
        extra = dict(anim=anim, frame=i, frames=n)
        if d:
            extra['dir'] = d
        if ms:
            extra['ms'] = ms
        if anim == 'turn':
            extra['yaw'] = yaw
        t0 = time.time()
        if who == 'tommy':
            p = ANIMS[anim][3](i) if anim != 'turn' else anim_turn(i)
            root = root_matrix(yaw, p['sq'], p['flip'])
            body, J = build_tommy(p, root)
            extra['height_m'] = 1.0
            ctx = layer_ctx(anim, i, n, p)
        else:
            pet = who[4:]
            q = pet_pose(pet, anim, i)
            root = root_matrix(yaw, q['sq'])
            body = PETS[pet](q, root)
            J = None
            extra['height_m'] = PET_HEIGHT[pet]
            if pet == 'bat':
                extra['fly_m'] = 0.60
        if want_body:
            C.render_sprite(a.out, name, body, C.cell(0, 0, 0), passes=('light', 'id', 'z', 'shadow'),
                            shadow_z=0.0, bounce_ground=0.0, kind='char', samples=a.samples,
                            extra=dict(extra), zoom=zoom)
            count += 1
        for kind, style, ln in want_layers:
            lobjs = build_layer(kind, style, J, root, ctx)
            ex = dict(extra, layer=kind, style=style, body=name)
            ex.pop('height_m', None)
            C.render_sprite(a.out, ln, lobjs, C.cell(0, 0, 0), passes=('light', 'id', 'z'), holdout=body,
                            bounce_ground=0.0, kind='layer', samples=a.samples, extra=ex, zoom=zoom)
            count += 1
            for ob in lobjs:
                _OBJS.remove(ob)
                me = ob.data
                bpy.data.objects.remove(ob, do_unlink=True)
                bpy.data.meshes.remove(me)
        clear()
        dt = time.time() - t0
        times[name] = round(dt, 2)
        print('[chars] %-26s %5.1fs  (+%d layers)' % (name, dt, len(want_layers)), flush=True)
        if count and count % 60 < 16:
            C.save_meta(a.out)
    if listing:
        print('[chars] %d sprites would be rendered' % count)
        return
    C.save_meta(a.out)
    print('[chars] %d sprites in %.1fs' % (count, time.time() - t_all), flush=True)
    tp = os.path.join(os.path.abspath(a.out), '_times.json')
    old = {}
    if os.path.exists(tp):
        with open(tp) as fh:
            old = json.load(fh)
    old.update(times)
    with open(tp, 'w') as fh:
        json.dump(old, fh, indent=1, sort_keys=True)


main()
