"""Monster Hop - geometry and rig helpers of monsters.py (and monsters_armor.py).

Everything here builds plain Blender meshes from numbers: no .blend, no
operators, so it runs the same headless every time.

  ellipsoid()   a UV ellipsoid, optional deform, any rotation
  tube()        a smooth tube along a Catmull-Rom curve, radius per point,
                rounded or flat ends (limbs, tails, tufts, horns)
  lathe()       a surface of revolution with elliptic sections and per-angle
                radius / height functions (torsos, shirts with torn hems, helmets)
  surface()     a parametric grid (capes, wings, ears, tabards), solidified
  wrap()        a band laid around a lathe-like body (a mummy's wraps)
  ringband()    a band around a tube (wraps on limbs, cuffs, belts)
  Rig           rigid parts parented to joints (empties) posed by a dict

Model every character facing -Y with its root on the ground at the origin;
the Rig puts the root on C.cell(0, 0, 0) and turns it by the facing's yaw.
"""
import math

import bmesh
import bpy
from mathutils import Euler, Matrix, Vector as V

import mh_common as C

TAU = 2.0 * math.pi


def srgb_to_lin(c):
    """(r, g, b) 0-255 sRGB -> linear 0-1 (material base colours)."""
    out = []
    for x in c[:3]:
        x = x / 255.0
        out.append(x / 12.92 if x <= 0.04045 else ((x + 0.055) / 1.055) ** 2.4)
    return tuple(out)


def rotm(r):
    """Euler degrees (XYZ) or a 3x3 Matrix or None -> 3x3 Matrix."""
    if r is None:
        return Matrix.Identity(3)
    if isinstance(r, Matrix):
        return r.to_3x3()
    return Euler([math.radians(a) for a in r], 'XYZ').to_matrix()


def track(n, up=(0, 0, 1)):
    """3x3 rotation taking local +Z onto direction n."""
    n = V(n).normalized()
    u = V(up)
    if abs(n.dot(u)) > 0.98:
        u = V((0, 1, 0)) if abs(n.y) < 0.9 else V((1, 0, 0))
    x = u.cross(n).normalized()
    y = n.cross(x)
    return Matrix((x, y, n)).transposed()


def sph_dir(az, el):
    """Unit direction: az degrees from the front (-Y) towards +X, el up."""
    a, e = math.radians(az), math.radians(el)
    return V((math.sin(a) * math.cos(e), -math.cos(a) * math.cos(e), math.sin(e)))


def on_ell(c, r, az, el, out=0.0, rot=None):
    """Point on an ellipsoid (centre c, radii r, rotation rot) in direction
    (az, el), pushed `out` metres along the surface normal; and that normal."""
    d = sph_dir(az, el)
    t = 1.0 / math.sqrt((d.x / r[0]) ** 2 + (d.y / r[1]) ** 2 + (d.z / r[2]) ** 2)
    p = d * t
    n = V((p.x / r[0] ** 2, p.y / r[1] ** 2, p.z / r[2] ** 2)).normalized()
    R = rotm(rot)
    return V(c) + R @ (p + n * out), (R @ n).normalized()


def ell_r(a, b, th):
    """Radius of an ellipse with semi-axes a (x), b (y) at angle th."""
    return a * b / math.sqrt((b * math.cos(th)) ** 2 + (a * math.sin(th)) ** 2)


# ---------------------------------------------------------------------------
# Mesh plumbing
# ---------------------------------------------------------------------------

def finish(ob, smooth=True, sub=0, recalc=True, auto=None):
    me = ob.data
    if recalc:
        bm = bmesh.new()
        bm.from_mesh(me)
        bmesh.ops.recalc_face_normals(bm, faces=bm.faces[:])
        bm.to_mesh(me)
        bm.free()
    for p in me.polygons:
        p.use_smooth = smooth
    if auto is not None:
        me.use_auto_smooth = True
        me.auto_smooth_angle = math.radians(auto)
    if sub:
        m = ob.modifiers.new('sub', 'SUBSURF')
        m.levels = sub
        m.render_levels = sub
    return ob


def mesh(name, verts, faces, key, smooth=True, sub=0, recalc=True, auto=None):
    ob = C.mesh_object(name, [tuple(v) for v in verts], faces, key)
    return finish(ob, smooth, sub, recalc, auto)


def solidify(ob, thick, offset=0.0, even=True):
    m = ob.modifiers.new('solid', 'SOLIDIFY')
    m.thickness = thick
    m.offset = offset
    m.use_even_offset = even
    m.use_quality_normals = True
    return ob


def _rings_to_faces(rings, cyclic=True):
    """Quads between consecutive rings (lists of vertex indices)."""
    faces = []
    for a, b in zip(rings[:-1], rings[1:]):
        n = len(a)
        for j in range(n if cyclic else n - 1):
            k = (j + 1) % n
            faces.append((a[j], a[k], b[k], b[j]))
    return faces


# ---------------------------------------------------------------------------
# Primitives
# ---------------------------------------------------------------------------

def ellipsoid(name, c, r, key, rot=None, seg=24, rings=14, deform=None, sub=0):
    """deform(u) gets the unit-sphere point u (a Vector) and returns the point
    to use instead (still in unit space, before scaling by r)."""
    R = rotm(rot)
    c = V(c)
    unit = [V((0, 0, 1))]
    for i in range(1, rings):
        th = math.pi * i / rings
        for j in range(seg):
            ph = TAU * j / seg
            unit.append(V((math.sin(th) * math.cos(ph), math.sin(th) * math.sin(ph), math.cos(th))))
    unit.append(V((0, 0, -1)))
    faces = [(0, 1 + j, 1 + (j + 1) % seg) for j in range(seg)]
    for i in range(rings - 2):
        for j in range(seg):
            a = 1 + i * seg + j
            b = 1 + i * seg + (j + 1) % seg
            faces.append((a, a + seg, b + seg, b))
    last = len(unit) - 1
    base = 1 + (rings - 2) * seg
    faces += [(base + j, last, base + (j + 1) % seg) for j in range(seg)]
    verts = []
    for u in unit:
        if deform:
            u = V(deform(u.copy()))
        verts.append(c + R @ V((u.x * r[0], u.y * r[1], u.z * r[2])))
    return mesh(name, verts, faces, key, sub=sub)


def _catmull(P, sub):
    n = len(P)
    out = []
    for i in range(n - 1):
        p1, p2 = P[i], P[i + 1]
        p0 = P[i - 1] if i > 0 else p1 + (p1 - p2)
        p3 = P[i + 2] if i + 2 < n else p2 + (p2 - p1)
        for k in range(sub):
            t = k / sub
            t2, t3 = t * t, t * t * t
            pt = 0.5 * ((2 * p1) + (-p0 + p2) * t + (2 * p0 - 5 * p1 + 4 * p2 - p3) * t2 +
                        (-p0 + 3 * p1 - 3 * p2 + p3) * t3)
            out.append((pt, i + t))
    out.append((P[-1], float(n - 1)))
    return out


def tube(name, pts, radii, key, n=12, sub=4, cap0=1.0, cap1=1.0, nrm=None, capn=4, smooth=True,
         subsurf=0, twist=0.0):
    """A tube through pts (Catmull-Rom), radius per point: a float or (rx, ry)
    (rx along the `nrm` hint direction). cap = 0 flat, 1 hemisphere, >1 pointier.
    A radius of 0 at an end makes a point (tufts, claws, horns)."""
    P = [V(p) for p in pts]
    rr = [(float(r), float(r)) if isinstance(r, (int, float)) else (float(r[0]), float(r[1])) for r in radii]
    samp = _catmull(P, sub)
    cs = [s[0] for s in samp]
    m = len(cs)
    T = []
    for k in range(m):
        a = cs[max(k - 1, 0)]
        b = cs[min(k + 1, m - 1)]
        T.append((b - a).normalized())
    hint = V(nrm) if nrm is not None else (V((1, 0, 0)) if abs(T[0].x) < 0.9 else V((0, 0, 1)))
    N = hint - T[0] * hint.dot(T[0])
    if N.length < 1e-6:
        N = V((0, 1, 0)) - T[0] * T[0].y
    N.normalize()
    frames = []
    for k in range(m):
        if k > 0:
            N = N - T[k] * N.dot(T[k])
            if N.length < 1e-8:
                N = frames[-1][0]
            N.normalize()
        B = T[k].cross(N)
        frames.append((N.copy(), B))

    def rad(s):
        i = min(int(s), len(rr) - 2) if len(rr) > 1 else 0
        f = s - i
        if len(rr) == 1:
            return rr[0]
        return (rr[i][0] + (rr[i + 1][0] - rr[i][0]) * f, rr[i][1] + (rr[i + 1][1] - rr[i][1]) * f)

    verts, rings = [], []

    def ring(c, Nk, Bk, rx, ry, tw=0.0):
        idx = []
        for j in range(n):
            a = TAU * j / n + tw
            verts.append(c + Nk * (rx * math.cos(a)) + Bk * (ry * math.sin(a)))
            idx.append(len(verts) - 1)
        return idx

    faces = []
    r0 = rad(0.0)
    # start cap
    pole0 = None
    if r0[0] < 1e-5:
        verts.append(cs[0])
        pole0 = len(verts) - 1
    elif cap0 > 0:
        ra = 0.5 * (r0[0] + r0[1])
        verts.append(cs[0] - T[0] * ra * cap0)
        pole0 = len(verts) - 1
        for q in range(capn - 1, 0, -1):
            ang = (math.pi / 2) * q / capn
            rings.append(ring(cs[0] - T[0] * (ra * cap0 * math.sin(ang)), frames[0][0], frames[0][1],
                              r0[0] * math.cos(ang), r0[1] * math.cos(ang)))
    body_start = len(rings)
    for k in range(m):
        s = samp[k][1]
        rx, ry = rad(s)
        if (k == 0 and r0[0] < 1e-5) or (k == m - 1 and rad(s)[0] < 1e-5):
            continue
        rings.append(ring(cs[k], frames[k][0], frames[k][1], max(rx, 1e-4), max(ry, 1e-4),
                          twist * k / max(1, m - 1)))
    r1 = rad(samp[-1][1])
    pole1 = None
    if r1[0] < 1e-5:
        verts.append(cs[-1])
        pole1 = len(verts) - 1
    elif cap1 > 0:
        ra = 0.5 * (r1[0] + r1[1])
        for q in range(1, capn):
            ang = (math.pi / 2) * q / capn
            rings.append(ring(cs[-1] + T[-1] * (ra * cap1 * math.sin(ang)), frames[-1][0], frames[-1][1],
                              r1[0] * math.cos(ang), r1[1] * math.cos(ang), twist))
        verts.append(cs[-1] + T[-1] * ra * cap1)
        pole1 = len(verts) - 1
    faces += _rings_to_faces(rings)
    if pole0 is not None:
        f = rings[0]
        faces += [(pole0, f[(j + 1) % n], f[j]) for j in range(n)]
    else:
        faces.append(tuple(reversed(rings[0])))
    if pole1 is not None:
        f = rings[-1]
        faces += [(pole1, f[j], f[(j + 1) % n]) for j in range(n)]
    else:
        faces.append(tuple(rings[-1]))
    return mesh(name, verts, faces, key, smooth=smooth, sub=subsurf)


def lathe(name, prof, key, c=(0, 0, 0), sx=1.0, sy=1.0, seg=32, rfn=None, zfn=None, rot=None,
          cap_bot=True, cap_top=True, sub=0, smooth=True, thick=0.0, shift=None):
    """Surface of revolution about the local Z axis. prof: [(r, z)] from the
    bottom up. rfn(th, z, i) scales the radius, zfn(th, i) shifts the height
    (th = 0 at +X, pi/2 at +Y, the front -Y is th = -pi/2). shift(z) -> (dx, dy)
    offsets a ring sideways (a bent body). thick > 0 solidifies an open shell."""
    R = rotm(rot)
    c = V(c)
    verts, rings = [], []
    pole_b = pole_t = None
    last = len(prof) - 1
    for i, (r, z) in enumerate(prof):
        dx, dy = shift(z) if shift else (0.0, 0.0)
        if r <= 1e-6 and i in (0, last):
            verts.append(c + R @ V((dx, dy, z)))
            if i == 0:
                pole_b = len(verts) - 1
            else:
                pole_t = len(verts) - 1
            continue
        idx = []
        for j in range(seg):
            th = TAU * j / seg
            rr = r * (rfn(th, z, i) if rfn else 1.0)
            zz = z + (zfn(th, i) if zfn else 0.0)
            verts.append(c + R @ V((dx + rr * sx * math.cos(th), dy + rr * sy * math.sin(th), zz)))
            idx.append(len(verts) - 1)
        rings.append(idx)
    faces = _rings_to_faces(rings)
    n = seg
    if pole_b is not None:
        f = rings[0]
        faces += [(pole_b, f[(j + 1) % n], f[j]) for j in range(n)]
    elif cap_bot:
        faces.append(tuple(reversed(rings[0])))
    if pole_t is not None:
        f = rings[-1]
        faces += [(pole_t, f[j], f[(j + 1) % n]) for j in range(n)]
    elif cap_top:
        faces.append(tuple(rings[-1]))
    closed = (pole_b is not None or cap_bot) and (pole_t is not None or cap_top)
    ob = C.mesh_object(name, [tuple(v) for v in verts], faces, key)
    if thick > 0:
        solidify(ob, thick, 1.0, even=False)     # even offset blows up on jagged hems
    return finish(ob, smooth, sub, recalc=closed)


def surface(name, fn, nu, nv, key, cyc_u=False, thick=0.0, offset=0.0, sub=0, smooth=True):
    """Grid surface: fn(u, v) -> point, u, v in [0, 1] (u in [0, 1) if cyclic)."""
    verts = []
    for j in range(nv):
        v = j / (nv - 1)
        for i in range(nu):
            u = i / nu if cyc_u else i / (nu - 1)
            verts.append(V(fn(u, v)))
    faces = []
    for j in range(nv - 1):
        for i in range(nu if cyc_u else nu - 1):
            a = j * nu + i
            b = j * nu + (i + 1) % nu
            faces.append((a, b, b + nu, a + nu))
    ob = C.mesh_object(name, [tuple(v) for v in verts], faces, key)
    if thick > 0:
        solidify(ob, thick, offset)
    return finish(ob, smooth, sub, recalc=False)


def wrap(name, rfun, zc, width, key, c=(0, 0, 0), th0=0.0, th1=TAU, n=48, thick=0.016, sink=0.006,
         taper=0.0, rot=None):
    """A band around a body whose surface radius is rfun(th, z) about the
    vertical axis through c. zc(th) is the band's centre height at angle th;
    width may be a number or a function of th. Partial bands (th0..th1 not a
    full turn) get flat ends, `taper` narrows them."""
    R = rotm(rot)
    c = V(c)
    full = abs((th1 - th0) - TAU) < 1e-6
    cnt = n if full else n + 1
    secs = []
    verts = []
    for k in range(cnt):
        t = k / n
        th = th0 + (th1 - th0) * t
        w = width(th) if callable(width) else width
        if taper > 0 and not full:
            w *= min(1.0, (min(t, 1 - t) / taper)) if taper > 0 else 1.0
            w = max(w, 0.004)
        z = zc(th)
        z0, z1 = z - w / 2, z + w / 2
        ct, st = math.cos(th), math.sin(th)
        sec = []
        for (zz, dr) in ((z0, -sink), (z0, thick), (z1, thick), (z1, -sink)):
            r = rfun(th, zz) + dr
            verts.append(c + R @ V((r * ct, r * st, zz)))
            sec.append(len(verts) - 1)
        secs.append(sec)
    faces = []
    for k in range(cnt if full else cnt - 1):
        a = secs[k]
        b = secs[(k + 1) % cnt]
        for q in range(4):
            qq = (q + 1) % 4
            faces.append((a[q], a[qq], b[qq], b[q]))
    if not full:
        faces.append(tuple(secs[0]))
        faces.append(tuple(reversed(secs[-1])))
    return mesh(name, verts, faces, key, smooth=True)


def ringband(name, center, axis, radius, width, key, tilt=0.0, phase=0.0, thick=0.014, sink=0.004, n=24,
             ref=None):
    """A band around a tube: centre point, axis direction, tube radius.
    tilt (metres) tips the band so it runs diagonally round the limb."""
    ax = V(axis).normalized()
    e1 = V(ref) if ref is not None else (V((1, 0, 0)) if abs(ax.x) < 0.9 else V((0, 1, 0)))
    e1 = (e1 - ax * e1.dot(ax)).normalized()
    e2 = ax.cross(e1)
    c = V(center)
    verts, secs = [], []
    for k in range(n):
        ph = TAU * k / n
        d = e1 * math.cos(ph) + e2 * math.sin(ph)
        off = tilt * math.sin(ph + phase)
        sec = []
        for (h, dr) in ((-width / 2, -sink), (-width / 2, thick), (width / 2, thick), (width / 2, -sink)):
            verts.append(c + ax * (off + h) + d * (radius + dr))
            sec.append(len(verts) - 1)
        secs.append(sec)
    faces = []
    for k in range(n):
        a, b = secs[k], secs[(k + 1) % n]
        for q in range(4):
            qq = (q + 1) % 4
            faces.append((a[q], a[qq], b[qq], b[q]))
    return mesh(name, verts, faces, key, smooth=True)


def box(name, c, size, key, rot=None, bevel=0.0, seg=2):
    """A box centred on c with full sizes `size`, rotated."""
    R = rotm(rot)
    c = V(c)
    hx, hy, hz = size[0] / 2, size[1] / 2, size[2] / 2
    corners = [(-hx, -hy, -hz), (hx, -hy, -hz), (hx, hy, -hz), (-hx, hy, -hz),
               (-hx, -hy, hz), (hx, -hy, hz), (hx, hy, hz), (-hx, hy, hz)]
    verts = [c + R @ V(p) for p in corners]
    faces = [(0, 3, 2, 1), (4, 5, 6, 7), (0, 1, 5, 4), (1, 2, 6, 5), (2, 3, 7, 6), (3, 0, 4, 7)]
    ob = mesh(name, verts, faces, key, smooth=False)
    if bevel > 0:
        m = ob.modifiers.new('bevel', 'BEVEL')
        m.width = bevel
        m.segments = seg
        m.limit_method = 'NONE'
        for p in ob.data.polygons:
            p.use_smooth = True
        ob.data.use_auto_smooth = True
        ob.data.auto_smooth_angle = math.radians(40)
    return ob


# ---------------------------------------------------------------------------
# The rig
# ---------------------------------------------------------------------------

class Rig:
    """Rigid parts on a tree of empties. Joints are placed at their rest pivot
    (model coordinates, root at the origin); parts are built in the same model
    coordinates and handed to add(part, joint). pose({...}) sets, per joint,
    a rotation (Euler XYZ degrees, in the parent's frame) and, with a key
    'joint@', a location offset in metres."""

    def __init__(self, name, anchor=None):
        self.name = name
        self.root = bpy.data.objects.new(name + '_root', None)
        C.link(self.root)
        self.root.location = anchor if anchor is not None else C.cell(0, 0, 0)
        self.root.rotation_mode = 'XYZ'
        self.J = {'root': self.root}
        self.rest = {'root': V((0, 0, 0))}
        self.par = {'root': None}
        self.parts = []
        self.tags = {}

    def joint(self, name, parent, pivot):
        e = bpy.data.objects.new('%s_%s' % (self.name, name), None)
        C.link(e)
        e.parent = self.J[parent]
        e.location = V(pivot) - self.rest[parent]
        e.rotation_mode = 'XYZ'
        self.J[name] = e
        self.rest[name] = V(pivot)
        self.par[name] = parent
        return e

    def add(self, ob, joint='root', tag=None):
        ob.data.transform(Matrix.Translation(-self.rest[joint]))
        ob.parent = self.J[joint]
        ob.location = (0, 0, 0)
        self.parts.append(ob)
        if tag:
            self.tags.setdefault(tag, []).append(ob)
        return ob

    def pose(self, P, yaw=0.0):
        for k, e in self.J.items():
            if k == 'root':
                continue
            e.rotation_euler = (0, 0, 0)
            e.location = self.rest[k] - self.rest[self.par[k]]
            e.scale = (1, 1, 1)
        for k, v in (P or {}).items():
            if k.endswith('*'):
                self.J[k[:-1]].scale = (v, v, v) if isinstance(v, (int, float)) else tuple(v)
            elif k.endswith('@'):
                j = k[:-1]
                self.J[j].location = self.rest[j] - self.rest[self.par[j]] + V(v)
            elif k in self.J and k != 'root':
                self.J[k].rotation_euler = [math.radians(a) for a in v]
        self.root.rotation_euler = (0, 0, math.radians(yaw))
        bpy.context.view_layer.update()

    def reshape(self, ob, new, joint):
        """Give part `ob` the vertex positions of `new` (same topology, built in
        model coordinates, e.g. a cape rebuilt with other parameters) and drop
        `new`. The modifiers of `ob` (solidify) re-evaluate by themselves."""
        off = self.rest[joint]
        co = []
        for v in new.data.vertices:
            c = v.co - off
            co += [c.x, c.y, c.z]
        assert len(co) == 3 * len(ob.data.vertices), (ob.name, len(co), len(ob.data.vertices))
        ob.data.vertices.foreach_set('co', co)
        ob.data.update()
        bpy.data.objects.remove(new, do_unlink=True)

    def visible(self, hide_tags=()):
        """The parts to render, without those under the given tags."""
        hid = set()
        for t in hide_tags:
            hid |= set(id(o) for o in self.tags.get(t, []))
        return [o for o in self.parts if id(o) not in hid]


def ribbon(name, pts, widths, key, hint=(0, 0, 1), sub=4, thick=0.010, smooth=True):
    """A flat strip along a Catmull-Rom curve (a loose bandage end, a tie):
    width per point, the width direction perpendicular to the curve and to
    `hint` (a vector or a function of the curve parameter)."""
    P = [V(p) for p in pts]
    samp = _catmull(P, sub)
    cs = [s[0] for s in samp]
    m = len(cs)
    verts = []
    for k in range(m):
        T = (cs[min(k + 1, m - 1)] - cs[max(k - 1, 0)]).normalized()
        s = samp[k][1]
        i = min(int(s), len(widths) - 2)
        w = widths[i] + (widths[i + 1] - widths[i]) * (s - i)
        h = V(hint(s) if callable(hint) else hint)
        side = T.cross(h)
        if side.length < 1e-6:
            side = T.cross(V((1, 0, 0)))
        side.normalize()
        verts += [cs[k] - side * (w / 2), cs[k] + side * (w / 2)]
    faces = [(2 * k, 2 * k + 1, 2 * k + 3, 2 * k + 2) for k in range(m - 1)]
    ob = C.mesh_object(name, [tuple(v) for v in verts], faces, key)
    if thick > 0:
        solidify(ob, thick, 0.0)
    return finish(ob, smooth, 0, recalc=False)


def glow_build(col, lo=0.70, hi=1.25):
    """Eye glow: pure emission, brightest where it faces the camera (the light
    pass reads ~255 in the middle, a little less at the rim). col is the sRGB
    colour for a final-colour ('color') pass; the light pass uses white."""
    def b(nt, neutral):
        em = nt.nodes.new('ShaderNodeEmission')
        c = (1.0, 1.0, 1.0) if neutral else srgb_to_lin(col)
        em.inputs['Color'].default_value = tuple(c) + (1.0,)
        lw = nt.nodes.new('ShaderNodeLayerWeight')
        lw.inputs['Blend'].default_value = 0.45
        m = nt.nodes.new('ShaderNodeMath')
        m.operation = 'MULTIPLY_ADD'
        nt.links.new(lw.outputs['Facing'], m.inputs[0])
        m.inputs[1].default_value = lo - hi
        m.inputs[2].default_value = hi
        nt.links.new(m.outputs[0], em.inputs['Strength'])
        return em.outputs['Emission']
    return b


def profile_r(prof, z):
    """Radius of a lathe profile [(r, z)] at height z (linear)."""
    for (r0, z0), (r1, z1) in zip(prof[:-1], prof[1:]):
        if z0 <= z <= z1:
            return r0 + (r1 - r0) * (z - z0) / max(z1 - z0, 1e-9)
    return prof[0][0] if z < prof[0][1] else prof[-1][0]


def ell_body_r(c, r):
    """rfun(th, z) for wrap(): the radius of an axis-aligned ellipsoid."""
    def f(th, z):
        k = 1.0 - ((z - c[2]) / r[2]) ** 2
        k = math.sqrt(max(k, 0.0025))
        return ell_r(r[0] * k, r[1] * k, th)
    return f


def lathe_body_r(prof, sx=1.0, sy=1.0):
    """rfun(th, z) for wrap(): the radius of a lathe body."""
    def f(th, z):
        rr = max(profile_r(prof, z), 0.01)
        return ell_r(rr * sx, rr * sy, th)
    return f
