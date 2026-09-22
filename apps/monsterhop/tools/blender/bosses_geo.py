"""Monster Hop bosses - geometry builders and a tiny rigid-part rig.

Pure bpy/mathutils, no camera or light (those belong to mh_common). Meshes
are built as lists of points and faces (`Geo`), then turned into objects with
one material key per slot. The rig is forward kinematics computed in Python
(bones have an identity rest orientation, so a rest point p on bone b goes to
M[b] @ (p - head[b])), with a two-bone IK and world-rotation overrides; each
rigid part is an object whose matrix_world is set per frame, deformable parts
(capes, tails, bandages) are rebuilt per frame in character space.
"""
import math

import bpy
from mathutils import Matrix, Vector as V, Euler

import mh_common as C

TAU = 2.0 * math.pi


def rad(d):
    return math.radians(d)


def lerp(a, b, t):
    return a + (b - a) * t


def smooth(e0, e1, x):
    t = min(1.0, max(0.0, (x - e0) / (e1 - e0))) if e1 != e0 else (1.0 if x >= e1 else 0.0)
    return t * t * (3 - 2 * t)


def rot(deg, order='XYZ'):
    """3x3 rotation from Euler degrees (x, y, z), Blender's XYZ order (x first)."""
    return Euler([rad(a) for a in deg], order).to_matrix()


def frame(d, p):
    """Orthonormal 3x3 with columns (d, p', d x p')."""
    x = V(d).normalized()
    y = V(p) - x * V(p).dot(x)
    if y.length < 1e-6:
        y = x.orthogonal()
    y.normalize()
    z = x.cross(y)
    return Matrix((x, y, z)).transposed()


def aim(d_rest, d_new, pole_rest, pole_new):
    """Rotation taking direction d_rest to d_new, twist fixed by the poles."""
    return frame(d_new, pole_new) @ frame(d_rest, pole_rest).transposed()


def ik2(a, t, l1, l2, pole):
    """Middle joint of a two-bone chain from a towards t, bending towards pole."""
    a, t = V(a), V(t)
    d = t - a
    L = d.length
    L = min(max(L, abs(l1 - l2) + 1e-4), l1 + l2 - 1e-4)
    x = d.normalized()
    y = V(pole) - x * V(pole).dot(x)
    if y.length < 1e-6:
        y = x.orthogonal()
    y.normalize()
    ca = (l1 * l1 + L * L - l2 * l2) / (2 * l1 * L)
    ca = min(1.0, max(-1.0, ca))
    return a + x * (l1 * ca) + y * (l1 * math.sqrt(1 - ca * ca))


# ---------------------------------------------------------------------------
# Geometry
# ---------------------------------------------------------------------------

class Geo:
    """Points, faces and a material slot per face."""

    def __init__(self):
        self.v, self.f, self.m = [], [], []

    def vert(self, p):
        self.v.append(V(p))
        return len(self.v) - 1

    def face(self, idx, m=0):
        self.f.append(tuple(idx))
        self.m.append(m)

    def add(self, g, slot=None):
        o = len(self.v)
        self.v.extend(V(p) for p in g.v)
        for f, m in zip(g.f, g.m):
            self.f.append(tuple(i + o for i in f))
            self.m.append(m if slot is None else slot)
        return self

    def xform(self, M):
        M = M if len(M) == 4 else M.to_4x4()
        self.v = [M @ p for p in self.v]
        return self

    def move(self, d):
        d = V(d)
        self.v = [p + d for p in self.v]
        return self

    def map(self, fn):
        self.v = [V(fn(p)) for p in self.v]
        return self


def spe(c, s, e):
    """Superellipse coordinate."""
    if e == 2.0:
        return c, s
    k = 2.0 / e
    return math.copysign(abs(c) ** k, c), math.copysign(abs(s) ** k, s)


def lathe(rings, n=24, e=2.0, arc=None, mat=None, zfun=None, rfun=None):
    """Stack of horizontal (super)ellipses around Z.

    rings: (z, rx, ry[, cx, cy]) or (z, r). rx = ry = 0 makes a pole.
    arc:   None (closed) or (a0, a1) radians: an open sheet from a0 to a1.
    mat:   fn(i_ring, j_seg, theta) -> slot, per face (the face's lower ring).
    zfun:  fn(i_ring, theta) -> dz (jagged hems).
    rfun:  fn(i_ring, theta) -> radius factor (scallops, bulges).
    Angles: 0 = +X, 90 deg = +Y (the back), -90 = -Y (the front).
    """
    g = Geo()
    closed = arc is None
    cols = n if closed else n + 1
    rows = []
    for i, rg in enumerate(rings):
        if len(rg) == 2:
            z, rx, ry, cx, cy = rg[0], rg[1], rg[1], 0.0, 0.0
        elif len(rg) == 3:
            z, rx, ry = rg
            cx = cy = 0.0
        else:
            z, rx, ry, cx, cy = rg[:5]
        if rx == 0 and ry == 0:
            rows.append(('pole', g.vert((cx, cy, z))))
            continue
        idx = []
        for j in range(cols):
            th = (TAU * j / n) if closed else lerp(arc[0], arc[1], j / n)
            c, s = spe(math.cos(th), math.sin(th), e)
            f = rfun(i, th) if rfun else 1.0
            dz = zfun(i, th) if zfun else 0.0
            idx.append(g.vert((cx + rx * c * f, cy + ry * s * f, z + dz)))
        rows.append(('ring', idx))
    segs = n if closed else n
    for i in range(len(rows) - 1):
        ka, a = rows[i]
        kb, b = rows[i + 1]
        for j in range(segs):
            j1 = (j + 1) % cols if closed else j + 1
            th = (TAU * (j + 0.5) / n) if closed else lerp(arc[0], arc[1], (j + 0.5) / n)
            m = mat(i, j, th) if mat else 0
            if ka == 'pole' and kb == 'ring':
                g.face((a, b[j1], b[j]), m)
            elif ka == 'ring' and kb == 'pole':
                g.face((a[j], a[j1], b), m)
            elif ka == 'ring' and kb == 'ring':
                g.face((a[j], a[j1], b[j1], b[j]), m)
    return g


def sphere(c, r, n=24, rings=12, rot_m=None, e=2.0, mat=None):
    """Ellipsoid at c with radii r (number or (rx, ry, rz)), optional rotation."""
    if not isinstance(r, (tuple, list)):
        r = (r, r, r)
    rr = [(-r[2], 0, 0)]
    for k in range(1, rings):
        a = -math.pi / 2 + math.pi * k / rings
        rr.append((r[2] * math.sin(a), r[0] * math.cos(a), r[1] * math.cos(a)))
    rr.append((r[2], 0, 0))
    g = lathe(rr, n, e, mat=mat)
    if rot_m is not None:
        g.xform(rot_m if isinstance(rot_m, Matrix) else rot(rot_m))
    return g.move(c)


def path_frames(pts, xhint):
    """Parallel-transport frames (t, x, y) along a polyline."""
    n = len(pts)
    ts = []
    for i in range(n):
        a = pts[max(0, i - 1)]
        b = pts[min(n - 1, i + 1)]
        t = (V(b) - V(a))
        ts.append(t.normalized() if t.length > 1e-9 else V((0, 0, 1)))
    x = V(xhint) - ts[0] * V(xhint).dot(ts[0])
    if x.length < 1e-6:
        x = ts[0].orthogonal()
    x.normalize()
    out = []
    for i in range(n):
        if i > 0:
            q = ts[i - 1].rotation_difference(ts[i])
            x = q @ x
            x = (x - ts[i] * x.dot(ts[i])).normalized()
        out.append((ts[i], x, ts[i].cross(x)))
    return out


def tube(pts, radii, n=16, xhint=(1, 0, 0), cap0=True, cap1=True, mat=None, e=2.0,
         jag=None):
    """A tube along a polyline. radii: r or (rx, ry) per point (rx along the
    frame's x, which starts along xhint). Round caps. mat(i, j) -> slot.
    jag: fn(i, theta) -> offset along the tangent (torn ends)."""
    pts = [V(p) for p in pts]
    fr = path_frames(pts, xhint)
    rs = [(r, r) if not isinstance(r, (tuple, list)) else r for r in radii]
    g = Geo()
    rows = []

    def ring(c, t, x, y, rx, ry, i):
        idx = []
        for j in range(n):
            th = TAU * j / n
            cc, ss = spe(math.cos(th), math.sin(th), e)
            dz = jag(i, th) if jag else 0.0
            idx.append(g.vert(c + x * (rx * cc) + y * (ry * ss) + t * dz))
        return idx

    if cap0:
        t, x, y = fr[0]
        rx, ry = rs[0]
        rows.append(('pole', g.vert(pts[0] - t * min(rx, ry) * 0.95), -1))
        for a in (60, 35):
            k = math.cos(rad(a))
            rows.append(('ring', ring(pts[0] - t * (min(rx, ry) * math.sin(rad(a)) * 0.95),
                                      t, x, y, rx * k, ry * k, -1), -1))
    for i, (p, (t, x, y), (rx, ry)) in enumerate(zip(pts, fr, rs)):
        rows.append(('ring', ring(p, t, x, y, rx, ry, i), i))
    if cap1:
        t, x, y = fr[-1]
        rx, ry = rs[-1]
        for a in (35, 60):
            k = math.cos(rad(a))
            rows.append(('ring', ring(pts[-1] + t * (min(rx, ry) * math.sin(rad(a)) * 0.95),
                                      t, x, y, rx * k, ry * k, len(pts)), len(pts)))
        rows.append(('pole', g.vert(pts[-1] + t * min(rx, ry) * 0.95), len(pts)))
    for k in range(len(rows) - 1):
        ka, a, ia = rows[k]
        kb, b, ib = rows[k + 1]
        for j in range(n):
            j1 = (j + 1) % n
            m = mat(max(0, min(len(pts) - 2, ia)), j) if mat else 0
            if ka == 'pole':
                g.face((a, b[j1], b[j]), m)
            elif kb == 'pole':
                g.face((a[j], a[j1], b), m)
            else:
                g.face((a[j], a[j1], b[j1], b[j]), m)
    return g


def strip(pts, widths, up, thick=0.02, mat=None):
    """A flat ribbon (with a little thickness) along pts; `up` is the side
    normal hint (the ribbon's face normal), one vector or one per point."""
    pts = [V(p) for p in pts]
    n = len(pts)
    ups = [V(u) for u in up] if isinstance(up, list) else [V(up)] * n
    g = Geo()
    rows = []
    for i in range(n):
        t = (pts[min(n - 1, i + 1)] - pts[max(0, i - 1)]).normalized()
        nn = ups[i] - t * ups[i].dot(t)
        nn.normalize()
        s = t.cross(nn).normalized()
        w = widths[i] if isinstance(widths, (list, tuple)) else widths
        h = thick / 2
        c = pts[i]
        rows.append([g.vert(c + s * w / 2 + nn * h), g.vert(c - s * w / 2 + nn * h),
                     g.vert(c - s * w / 2 - nn * h), g.vert(c + s * w / 2 - nn * h)])
    for i in range(n - 1):
        m = mat(i) if mat else 0
        a, b = rows[i], rows[i + 1]
        for k in range(4):
            k1 = (k + 1) % 4
            g.face((a[k], a[k1], b[k1], b[k]), m)
    g.face(tuple(reversed(rows[0])), mat(0) if mat else 0)
    g.face(tuple(rows[-1]), mat(n - 2) if mat else 0)
    return g


def cone(base, tip, r, n=10, bulge=0.55):
    """A soft spike from base to tip (fur tufts, claws, fangs, ears)."""
    base, tip = V(base), V(tip)
    pts = [base, base.lerp(tip, 0.35), base.lerp(tip, 0.7), tip]
    rs = [r, r * (0.55 + bulge * 0.4), r * 0.45, r * 0.06]
    return tube(pts, rs, n=n, cap0=True, cap1=False)


def box(c, s, rot_m=None, bevel_rings=True):
    """A rounded box: superellipse lathe (reads as a soft block after subsurf)."""
    sx, sy, sz = s
    rr = [(-sz, 0, 0), (-sz, sx * 0.8, sy * 0.8), (-sz * 0.85, sx, sy), (sz * 0.85, sx, sy),
          (sz, sx * 0.8, sy * 0.8), (sz, 0, 0)]
    g = lathe(rr, 16, 5.0)
    if rot_m is not None:
        g.xform(rot_m if isinstance(rot_m, Matrix) else rot(rot_m))
    return g.move(c)


# ---------------------------------------------------------------------------
# Objects
# ---------------------------------------------------------------------------

def make_obj(name, g, keys, sub=1, smooth_shade=True, local_origin=None):
    """Turn a Geo into a linked object with material keys per slot. The mesh
    is written relative to local_origin (the bone head) when given."""
    o = V(local_origin) if local_origin is not None else V((0, 0, 0))
    me = bpy.data.meshes.new(name)
    me.from_pydata([tuple(p - o) for p in g.v], [], g.f)
    for p, m in zip(me.polygons, g.m):
        p.material_index = m
        p.use_smooth = smooth_shade
    me.validate()
    ob = bpy.data.objects.new(name, me)
    C.link(ob)
    for k in (keys if isinstance(keys, (list, tuple)) else [keys]):
        C.assign(ob, k)
    if sub:
        md = ob.modifiers.new('sub', 'SUBSURF')
        md.levels = sub
        md.render_levels = sub
    return ob


def drop(objs):
    for ob in objs:
        me = ob.data
        bpy.data.objects.remove(ob, do_unlink=True)
        if me is not None and me.users == 0:
            bpy.data.meshes.remove(me)


# ---------------------------------------------------------------------------
# Rig
# ---------------------------------------------------------------------------

class Rig:
    def __init__(self):
        self.order, self.parent, self.head = [], {}, {}
        self.M = {}

    def bone(self, name, parent, head):
        self.order.append(name)
        self.parent[name] = parent
        self.head[name] = V(head)
        return name

    def rest_dir(self, a, b):
        return self.head[b] - self.head[a]

    def solve(self, P):
        """P: dict with
            rot   {bone: (x, y, z) deg}  local rotation at the joint
            off   {bone: V}              extra translation (in the parent frame)
            world {bone: Matrix3}        world (character space) rotation override
            ik    [(upper, lower, end, target, pole)]
        """
        rots = P.get('rot', {})
        offs = P.get('off', {})
        world = dict(P.get('world', {}))
        iks = {}
        for k in P.get('ik', []):
            u, lo, en, t, p = k[:5]
            pr = k[5] if len(k) > 5 else (0, -1, 0)
            iks[u] = (lo, en, V(t), V(p), V(pr))
        pend = {}
        M = {}
        for b in self.order:
            pa = self.parent[b]
            if pa is None:
                base = Matrix.Translation(self.head[b] + V(offs.get(b, (0, 0, 0))))
                Rp = Matrix.Identity(3)
            else:
                base = M[pa] @ Matrix.Translation(self.head[b] - self.head[pa] +
                                                  V(offs.get(b, (0, 0, 0))))
                Rp = M[pa].to_3x3()
            if b in iks:
                lo, en, t, pole, pr = iks[b]
                a = base.translation.copy()
                l1 = (self.head[lo] - self.head[b]).length
                l2 = (self.head[en] - self.head[lo]).length
                e = ik2(a, t, l1, l2, pole)
                world[b] = aim(self.head[lo] - self.head[b], e - a, pr, pole)
                pend[lo] = (e, t, self.head[en] - self.head[lo], pole, pr)
            if b in pend:
                e, t, d2r, pole, pr = pend.pop(b)
                world[b] = aim(d2r, t - e, pr, pole)
            if b in world:
                Rl = Rp.transposed() @ world[b]
            else:
                Rl = rot(rots.get(b, (0, 0, 0)))
            M[b] = base @ Rl.to_4x4()
        self.M = M
        return M

    def pt(self, b, p):
        """Posed character-space position of rest point p on bone b."""
        return self.M[b] @ (V(p) - self.head[b])

    def dirv(self, b, d):
        return self.M[b].to_3x3() @ V(d)



# ---------------------------------------------------------------------------
# Profiles and wraps (bandages, belts)
# ---------------------------------------------------------------------------

def _ring5(rg):
    if len(rg) == 2:
        return (rg[0], rg[1], rg[1], 0.0, 0.0)
    if len(rg) == 3:
        return (rg[0], rg[1], rg[2], 0.0, 0.0)
    return tuple(rg[:5])


def prof_at(rings, z):
    """(rx, ry, cx, cy) of a lathe profile at height z (rings sorted by z)."""
    rs = [_ring5(r) for r in rings]
    if z <= rs[0][0]:
        return rs[0][1:]
    for a, b in zip(rs, rs[1:]):
        if a[0] <= z <= b[0]:
            t = (z - a[0]) / max(1e-9, b[0] - a[0])
            return tuple(lerp(a[k], b[k], t) for k in range(1, 5))
    return rs[-1][1:]


def resample(rings, z0, z1, step):
    """Profile rings every `step` between z0 and z1 (for stripes by ring)."""
    n = max(1, int(round((z1 - z0) / step)))
    return [(z0 + (z1 - z0) * k / n,) + prof_at(rings, z0 + (z1 - z0) * k / n) for k in range(n + 1)]


def band_on(rings, z0, width, off=0.012, tilt=0.0, tdir=0.0, n=32, thick=0.02, scale=1.0):
    """A bandage/belt band round a lathe body: its centre line at height z0,
    tilted by `tilt` deg towards direction `tdir` (deg, 0 = +X)."""
    g = Geo()
    rows = []
    ta = math.tan(rad(tilt))
    for k, (dzw, dr) in enumerate(((-width / 2, -thick), (-width / 2, 0), (width / 2, 0), (width / 2, -thick))):
        idx = []
        for j in range(n):
            th = TAU * j / n
            c, s = math.cos(th), math.sin(th)
            rx, ry, cx, cy = prof_at(rings, z0)
            z = z0 + ta * (rx * c * math.cos(rad(tdir)) + ry * s * math.sin(rad(tdir))) + dzw
            rx, ry, cx, cy = prof_at(rings, z)
            rx, ry = rx * scale + off + dr, ry * scale + off + dr
            idx.append(g.vert((cx + rx * c, cy + ry * s, z)))
        rows.append(idx)
    for k in range(4):
        a, b = rows[k], rows[(k + 1) % 4]
        for j in range(n):
            j1 = (j + 1) % n
            g.face((a[j], a[j1], b[j1], b[j]))
    return g


def tube_band(p0, p1, r0, r1, t, width, off=0.012, tilt=0.0, n=20, thick=0.02, xhint=(1, 0, 0), phase=0.0):
    """A band round the straight tube p0-p1 at parameter t, tilted."""
    p0, p1 = V(p0), V(p1)
    ax = (p1 - p0)
    L = ax.length
    ax.normalize()
    x = V(xhint) - ax * V(xhint).dot(ax)
    if x.length < 1e-6:
        x = ax.orthogonal()
    x.normalize()
    y = ax.cross(x)
    g = Geo()
    rows = []
    ta = math.tan(rad(tilt))
    for dzw, dr in ((-width / 2, -thick), (-width / 2, 0), (width / 2, 0), (width / 2, -thick)):
        idx = []
        for j in range(n):
            th = TAU * j / n
            c, s = math.cos(th + phase), math.sin(th + phase)
            r = lerp(r0, r1, t)
            d = ta * r * c + dzw
            tt = min(1.0, max(0.0, t + d / L))
            r = lerp(r0, r1, tt) + off + dr
            idx.append(g.vert(p0 + ax * (tt * L) + x * (r * math.cos(th)) + y * (r * math.sin(th))))
        rows.append(idx)
    for k in range(4):
        a, b = rows[k], rows[(k + 1) % 4]
        for j in range(n):
            j1 = (j + 1) % n
            g.face((a[j], a[j1], b[j1], b[j]))
    return g


def lathe_arc(rings, arcs, n=32, mat=None, zfun=None, rfun=None):
    """An open lathe sheet whose angular span varies per ring: arcs[i] =
    (a0, a1) radians. For garments open at the front (jackets, hoods)."""
    g = Geo()
    rows = []
    for i, rg in enumerate(rings):
        z, rx, ry, cx, cy = _ring5(rg)
        a0, a1 = arcs[i]
        idx = []
        for j in range(n + 1):
            th = lerp(a0, a1, j / n)
            f = rfun(i, th) if rfun else 1.0
            dz = zfun(i, th) if zfun else 0.0
            idx.append(g.vert((cx + rx * math.cos(th) * f, cy + ry * math.sin(th) * f, z + dz)))
        rows.append(idx)
    for i in range(len(rows) - 1):
        for j in range(n):
            m = mat(i, j) if mat else 0
            g.face((rows[i][j], rows[i][j + 1], rows[i + 1][j + 1], rows[i + 1][j]), m)
    return g
