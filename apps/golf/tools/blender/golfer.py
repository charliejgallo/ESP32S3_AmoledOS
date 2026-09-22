"""
golfer.py - the golfer of the watch's golf game, built, posed and rendered
from scratch in Blender (no .blend inputs). See SPEC.md.

    /Applications/Blender.app/Contents/MacOS/Blender -b -P golfer.py -- \
        --out ../../assets/render [--seq swing,idle,cheer,sad,turn] \
        [--frames 0,10,14] [--samples 64] [--hats 0,1,2,3,4,5 | --nohats] \
        [--debug DIR]

How it works
  * The body is a handful of lofted surfaces (rings of superellipses swept
    along the rest-pose skeleton), merged into one mesh that is skinned in
    numpy (dual quaternions) every frame, then smoothed by a subdivision
    modifier. Rigid things (head, hair, shoes, hands, club, hats) are separate
    objects whose matrix is set per frame.
  * The skeleton is a small hand-written rig: world-space posture angles for
    pelvis, chest and head, two-bone IK for arms and legs, feet planted by
    their ball-of-foot, hands attached to the club grip. Poses are key frames
    interpolated with Catmull-Rom.
  * Every frame is rendered three times (shade, region id, shadow) plus the
    shade and id of each hat with the body as a hold-out. Renders go to EXR
    and are post-processed here in numpy (sRGB encode, id quantisation and
    fill) and written as 8-bit PNGs with a tiny PNG writer.
"""
import bpy
import bmesh
import sys
import os
import math
import json
import time
import zlib
import struct
import argparse
import numpy as np
from mathutils import Vector, Matrix, Quaternion
from bpy_extras.object_utils import world_to_camera_view

HERE = os.path.dirname(os.path.abspath(__file__))
IMG_W, IMG_H = 368, 448
TURN_W, TURN_H = 184, 280

# ---------------------------------------------------------------------------
# Small math helpers
# ---------------------------------------------------------------------------


def V(*a):
    return Vector(a)


def clamp(x, a, b):
    return a if x < a else b if x > b else x


def smooth(e0, e1, x):
    t = clamp((x - e0) / (e1 - e0), 0.0, 1.0)
    return t * t * (3 - 2 * t)


def R3(axis, deg):
    return Matrix.Rotation(math.radians(deg), 3, axis)


def mat4(rot3, origin):
    m = rot3.to_4x4()
    m.translation = origin
    return m


def posture(tilt, side, turn):
    """World-level posture: turn about the body's own axis, then side bend
    (top towards -Y for side > 0), then forward tilt (top towards +X)."""
    return R3('Y', tilt) @ R3('X', side) @ R3('Z', turn)


def head_rot(pitch, roll, yaw):
    """Head: yaw about world Z, pitch down (face towards -Z) and roll."""
    return R3('Z', yaw) @ R3('Y', pitch) @ R3('X', roll)


def frame_zx(origin, z, xhint):
    """4x4 with Z along z and X as close as possible to xhint."""
    z = z.normalized()
    x = xhint - z * xhint.dot(z)
    if x.length < 1e-6:
        x = V(1, 0, 0) - z * z.x
        if x.length < 1e-6:
            x = V(0, 1, 0) - z * z.y
    x.normalize()
    y = z.cross(x)
    m = Matrix((x, y, z)).transposed().to_4x4()
    m.translation = origin
    return m


def frame_xy(origin, x, yhint):
    """4x4 with X along x and Y as close as possible to yhint."""
    x = x.normalized()
    y = yhint - x * yhint.dot(x)
    if y.length < 1e-6:
        y = V(0, 0, 1) - x * x.z
    y.normalize()
    z = x.cross(y)
    m = Matrix((x, y, z)).transposed().to_4x4()
    m.translation = origin
    return m


def ik2(a, target, l1, l2, pole):
    """Two-bone IK. Returns (mid joint, end, unit pole direction used)."""
    d = target - a
    dist = d.length
    dn = d.normalized() if dist > 1e-9 else V(0, 0, -1)
    dist_c = clamp(dist, abs(l1 - l2) + 1e-4, (l1 + l2) * 0.99995)
    cos_a = (l1 * l1 + dist_c * dist_c - l2 * l2) / (2 * l1 * dist_c)
    sin_a = math.sqrt(max(0.0, 1 - cos_a * cos_a))
    pp = pole - dn * pole.dot(dn)
    if pp.length < 1e-6:
        pp = V(0, 0, -1) - dn * (-dn.z)
    pp.normalize()
    mid = a + dn * (cos_a * l1) + pp * (sin_a * l1)
    end = a + dn * dist_c
    return mid, end, pp, dist / (l1 + l2)


# ---------------------------------------------------------------------------
# Skeleton constants (rest pose: standing, facing +X, left = +Y, metres)
# ---------------------------------------------------------------------------

OFF_SPINE1 = V(0, 0, 0.11)
OFF_SPINE2 = V(0, 0, 0.19)
OFF_NECK = V(-0.015, 0, 0.22)
OFF_HEAD = V(0.015, 0, 0.115)


def OFF_HIP(s):
    return V(0, s * 0.092, -0.04)


def OFF_CLAV(s):
    return V(-0.005, s * 0.03, 0.19)


def CLAV_TO_SHOULDER(s):
    return V(-0.01, s * 0.155, -0.005)


L_UP, L_FORE = 0.29, 0.255
L_THIGH, L_SHIN = 0.415, 0.41
ANKLE_LOCAL = V(0, 0, 0.085)
BALL_LOCAL = V(0.175, 0, 0)   # pivot of a lifted heel (toe joint)
WRIST_OFF = 0.058          # wrist to grip axis
GRIP_H = 0.11              # club "hands point" measured from the butt
GRIP_L, GRIP_R = 0.062, 0.150   # hand centres along the shaft from the butt
L_SHAFT = 1.08             # butt to hosel
LIE = 56.0                 # club lie angle, degrees
FEET_X = -0.85             # feet centre x in the swing set-up

BONES = ['pelvis', 'spine1', 'spine2', 'neck', 'head',
         'clav_L', 'clav_R', 'up_L', 'fore_L', 'up_R', 'fore_R',
         'thigh_L', 'shin_L', 'thigh_R', 'shin_R']
BI = {b: i for i, b in enumerate(BONES)}


def arm_rest_dir(s):
    a = math.radians(40)
    return V(0.05, s * math.sin(a), -math.cos(a)).normalized()


# ---------------------------------------------------------------------------
# The rig: pose parameters -> bone matrices, rigid matrices, joints
# ---------------------------------------------------------------------------

def club_matrix(butt, S, toe):
    """Club local frame: origin at the butt, local -Z along the shaft towards
    the head, local +X the toe hint (perpendicular to the shaft)."""
    z = -S.normalized()
    return frame_zx(butt, z, toe)


def club_head_frame():
    """Head frame (x' toe, y' face normal, z' up when soled) in club local."""
    l = math.radians(LIE)
    xp = V(math.sin(l), 0, -math.cos(l))
    yp = V(0, 1, 0)
    zp = V(math.cos(l), 0, math.sin(l))
    m = Matrix((xp, yp, zp)).transposed().to_4x4()
    m.translation = V(0, 0, -L_SHAFT - 0.035)
    return m


CLUB_HEAD_CENTRE = V(0.058, -0.045, 0.026)   # in head frame


def evaluate(p, rest=False):
    """Evaluates a pose dict. Returns dict with 'M' (bone 4x4 world),
    'rigid' (object 4x4), 'J' (joint positions), 'info'."""
    G = p.get('G', Matrix.Identity(4))
    M, J, rigid, info = {}, {}, {}, {}
    Rp = posture(*p['pelvis_rot'])
    Rc = posture(*p['chest_rot'])
    qs = Rp.to_quaternion().slerp(Rc.to_quaternion(), 0.42)
    Rs1 = qs.to_matrix()
    P0 = V(*p['pelvis_pos'])
    M['pelvis'] = mat4(Rp, P0)
    o1 = P0 + Rp @ OFF_SPINE1
    M['spine1'] = mat4(Rs1, o1)
    o2 = o1 + Rs1 @ OFF_SPINE2
    M['spine2'] = mat4(Rc, o2)
    Rh = head_rot(*p['head_rot'])
    Rn = Rc.to_quaternion().slerp(Rh.to_quaternion(), 0.5).to_matrix()
    on = o2 + Rc @ OFF_NECK
    M['neck'] = mat4(Rn, on)
    oh = on + Rn @ OFF_HEAD
    M['head'] = mat4(Rh, oh)
    rigid['head'] = M['head']

    # clavicles and shoulders
    sh_d = {}
    for s, sd in ((1, 'L'), (-1, 'R')):
        elev, prot = p['clav_' + sd]
        Rcl = Rc @ R3('X', s * elev) @ R3('Z', -s * prot)
        oc = o2 + Rc @ OFF_CLAV(s)
        M['clav_' + sd] = mat4(Rcl, oc)
        sh_d[sd] = oc + Rcl @ CLAV_TO_SHOULDER(s)
        J['shoulder_' + sd] = sh_d[sd]

    # club; with reach_w > 0 the club slides along the lead arm so that the
    # left arm is extended to 'reach' of its length (keeps the lead arm long)
    S = V(*p['club_S']).normalized()
    Hc = V(*p['club_H'])
    rw = p.get('reach_w', 0.0)
    if rw > 1e-3 and p['grip_L'] >= 0 and not rest:
        for it in range(4):
            butt = Hc - S * GRIP_H
            gp = butt + S * p['grip_L']
            off = sh_d['L'] - gp
            off = (off - S * off.dot(S)).normalized() * WRIST_OFF
            d = gp + off - sh_d['L']
            want = p.get('reach', 0.975) * (L_UP + L_FORE)
            Hc = Hc + d.normalized() * (want - d.length) * rw
    butt = Hc - S * GRIP_H
    C = club_matrix(butt, S, V(*p['club_toe']))
    rigid['club'] = C

    for s, sd in ((1, 'L'), (-1, 'R')):
        sh = sh_d[sd]
        pole = V(*p['elbow_' + sd])
        g = p['grip_' + sd]
        if rest:
            wrist = sh + arm_rest_dir(s) * (L_UP + L_FORE)
            yhint = None
        elif g >= 0:
            gp = butt + S * g
            off = sh - gp
            off = off - S * off.dot(S)
            off = off.normalized() * WRIST_OFF
            wrist = gp + off
            yhint = -off
        else:
            wrist = V(*p['wrist_' + sd])
            yhint = None
        el, w2, pp, ext = ik2(sh, wrist, L_UP, L_FORE, pole)
        info['ext_' + sd] = ext
        info['miss_' + sd] = (w2 - wrist).length
        M['up_' + sd] = frame_zx(sh, el - sh, -pp)
        M['fore_' + sd] = frame_zx(el, w2 - el, -pp)
        J['elbow_' + sd] = el
        J['wrist_' + sd] = w2
        # hand frame: origin at the wrist, Y towards the knuckles / grip,
        # X along the grip axis towards the club head
        if yhint is not None:
            rigid['hand_' + sd] = frame_xy(w2, S, yhint)
            rigid['hand_' + sd].translation = w2
        else:
            fd = (w2 - el).normalized()
            hy = V(*p['handY_' + sd]) if p.get('handY_' + sd) else fd
            hx = V(*p['handX_' + sd]) if p.get('handX_' + sd) else -pp
            m = frame_xy(w2, hx - hy * hx.dot(hy) / max(hy.length_squared, 1e-9), hy)
            rigid['hand_' + sd] = m

        # legs
        hip = P0 + Rp @ OFF_HIP(s)
        bx, by, yaw, lift = p['foot_' + sd]
        F = mat4(R3('Z', yaw) @ R3('Y', lift), V(bx, by, 0)) @ Matrix.Translation(-BALL_LOCAL)
        ankle = F @ ANKLE_LOCAL
        kn, a2, kp, lext = ik2(hip, ankle, L_THIGH, L_SHIN, V(*p['knee_' + sd]))
        info['legext_' + sd] = lext
        info['legmiss_' + sd] = (a2 - ankle).length
        M['thigh_' + sd] = frame_zx(hip, kn - hip, -kp)
        M['shin_' + sd] = frame_zx(kn, a2 - kn, -kp)
        rigid['foot_' + sd] = Matrix.Translation(a2 - ankle) @ F
        J['hip_' + sd], J['knee_' + sd], J['ankle_' + sd] = hip, kn, a2

    J['pelvis'], J['spine1'], J['spine2'], J['neck'], J['head'] = P0, o1, o2, on, oh
    # apply the global transform
    for d in (M, rigid):
        for k in d:
            d[k] = G @ d[k]
    for k in J:
        J[k] = G @ J[k]
    hc = rigid['club'] @ club_head_frame() @ CLUB_HEAD_CENTRE
    info['club_head'] = hc
    return {'M': M, 'rigid': rigid, 'J': J, 'info': info}


# ---------------------------------------------------------------------------
# Mesh building: superellipse rings swept along paths
# ---------------------------------------------------------------------------

class MB:
    """Accumulates vertices, faces, per-face material keys, per-corner UVs
    ('pat' in metres, 'rxy'/'rz' rest coordinates) and per-vertex weights."""

    def __init__(self):
        self.v, self.f, self.fm, self.fuv, self.w, self.aux = [], [], [], [], [], []

    def vert(self, p, w=None):
        self.v.append(Vector(p))
        self.w.append(w or {})
        return len(self.v) - 1

    def face(self, idx, mat, uvs):
        self.f.append(tuple(idx))
        self.fm.append(mat)
        self.fuv.append(uvs)


def ring_pts(c, xa, ya, xp, xn, yp, yn, n, e=2.0, th0=0.0):
    pts = []
    for k in range(n):
        th = th0 + 2 * math.pi * k / n
        cs, sn = math.cos(th), math.sin(th)
        ex = 2.0 / e
        x = math.copysign(abs(cs) ** ex, cs) * (xp if cs > 0 else xn)
        y = math.copysign(abs(sn) ** ex, sn) * (yp if sn > 0 else yn)
        pts.append(c + xa * x + ya * y)
    return pts


def loft(mb, rings, vs, mat_fn, weight_fn, perim_period=None, cap0=None, cap1=None,
         closed=True, aux=0.0):
    """rings: list of point lists (same n). vs: v coordinate per ring (m).
    mat_fn(ring_i, k, centre) -> material key. weight_fn(point, ring_i) -> dict.
    cap0/cap1: optional cap centre points. The 'pat' u coordinate runs round
    the ring in metres, snapped to a whole number of perim_period."""
    n = len(rings[0])
    per = sum((rings[len(rings) // 2][k] - rings[len(rings) // 2][(k + 1) % n]).length for k in range(n))
    if perim_period:
        per = max(1, round(per / perim_period)) * perim_period
    base = len(mb.v)
    for i, r in enumerate(rings):
        for p in r:
            mb.vert(p, weight_fn(p, i))
    kk = range(n) if closed else range(n - 1)
    for i in range(len(rings) - 1):
        for k in kk:
            k1 = (k + 1) % n
            a, b = base + i * n + k, base + i * n + k1
            c, d = base + (i + 1) * n + k1, base + (i + 1) * n + k
            u0, u1 = per * k / n, per * (k + 1) / n
            cen = (mb.v[a] + mb.v[b] + mb.v[c] + mb.v[d]) / 4
            mb.face((a, b, c, d), mat_fn(i, k, cen),
                    [(u0, vs[i]), (u1, vs[i]), (u1, vs[i + 1]), (u0, vs[i + 1])])
    for cap, i, flip in ((cap0, 0, True), (cap1, len(rings) - 1, False)):
        if cap is None:
            continue
        ci = mb.vert(cap, weight_fn(cap, i))
        dv = 0.02 if not flip else -0.02
        for k in range(n):
            k1 = (k + 1) % n
            a, b = base + i * n + k, base + i * n + k1
            cen = (mb.v[a] + mb.v[b] + mb.v[ci]) / 3
            u0, u1 = per * k / n, per * (k + 1) / n
            mb.face((a, b, ci), mat_fn(i, k, cen),
                    [(u0, vs[i]), (u1, vs[i]), ((u0 + u1) / 2, vs[i] + dv)])
    return base


def path_frames(pts, xhint):
    """Parallel-ish frames along a polyline: returns list of (pos, tangent,
    xa, ya, s) sampled at the given points."""
    out = []
    s = 0.0
    for i, p in enumerate(pts):
        if i == 0:
            t = (pts[1] - pts[0]).normalized()
        elif i == len(pts) - 1:
            t = (pts[-1] - pts[-2]).normalized()
        else:
            t = (pts[i + 1] - pts[i - 1]).normalized()
        if i > 0:
            s += (pts[i] - pts[i - 1]).length
        xa = (xhint - t * xhint.dot(t)).normalized()
        ya = t.cross(xa)
        out.append((p, t, xa, ya, s))
    return out


def polyline_point(poly, t):
    """Point at arc length t along a polyline (extrapolates at the ends)."""
    acc = 0.0
    for i in range(len(poly) - 1):
        seg = (poly[i + 1] - poly[i])
        L = seg.length
        if t <= acc + L or i == len(poly) - 2:
            return poly[i] + seg.normalized() * (t - acc)
        acc += L
    return poly[-1]


def polyline_param(poly, p):
    """Arc-length parameter of the closest point to p on the polyline
    (extended beyond both ends)."""
    best, bt, acc = 1e9, 0.0, 0.0
    for i in range(len(poly) - 1):
        a, b = poly[i], poly[i + 1]
        ab = b - a
        L = ab.length
        u = (p - a).dot(ab) / (L * L)
        if i > 0:
            u = max(u, 0.0)
        if i < len(poly) - 2:
            u = min(u, 1.0)
        q = a + ab * u
        d = (p - q).length
        if d < best:
            best, bt = d, acc + u * L
        acc += L
    return bt


def interp_table(table, x, cols):
    """Linear interpolation of rows (x, c1, c2, ...) at x."""
    if x <= table[0][0]:
        return table[0][1:1 + cols]
    for i in range(len(table) - 1):
        a, b = table[i], table[i + 1]
        if x <= b[0]:
            t = (x - a[0]) / (b[0] - a[0])
            return tuple(a[j] + (b[j] - a[j]) * t for j in range(1, 1 + cols))
    return table[-1][1:1 + cols]


def ellipsoid(mb, c, r, mat, rot=None, n=12, rings=8, weight=None):
    rot = rot or Matrix.Identity(3)
    rs = []
    vs = []
    for i in range(1, rings):
        ph = -math.pi / 2 + math.pi * i / rings
        z = math.sin(ph)
        rr = math.cos(ph)
        pts = []
        for k in range(n):
            th = 2 * math.pi * k / n
            pts.append(c + rot @ V(rr * math.cos(th) * r[0], rr * math.sin(th) * r[1], z * r[2]))
        rs.append(pts)
        vs.append(z * r[2])
    loft(mb, rs, vs, lambda i, k, cen: mat, lambda p, i: dict(weight or {}),
         cap0=c + rot @ V(0, 0, -r[2]), cap1=c + rot @ V(0, 0, r[2]))


# ---------------------------------------------------------------------------
# Character geometry (rest pose)
# ---------------------------------------------------------------------------

# torso: z, centre x, front, back, half width, superellipse exponent
TORSO = [
    (0.815, 0.00, 0.045, 0.060, 0.080, 2.0),
    (0.845, 0.00, 0.080, 0.095, 0.140, 2.2),
    (0.890, 0.00, 0.098, 0.116, 0.170, 2.3),
    (0.950, 0.00, 0.101, 0.114, 0.176, 2.3),
    (1.000, 0.00, 0.098, 0.104, 0.168, 2.3),
    (1.035, 0.00, 0.097, 0.100, 0.162, 2.3),
    (1.080, 0.005, 0.101, 0.099, 0.161, 2.4),
    (1.150, 0.010, 0.105, 0.099, 0.164, 2.4),
    (1.230, 0.010, 0.112, 0.104, 0.173, 2.4),
    (1.310, 0.000, 0.118, 0.110, 0.184, 2.5),
    (1.375, -0.005, 0.112, 0.114, 0.190, 2.6),
    (1.420, -0.010, 0.094, 0.104, 0.186, 2.6),
    (1.450, -0.014, 0.074, 0.084, 0.160, 2.4),
    (1.475, -0.016, 0.060, 0.066, 0.110, 2.2),
    (1.492, -0.016, 0.050, 0.054, 0.072, 2.0),
]
BELT_Z0, BELT_Z1 = 1.012, 1.056
TROUSER_TOP = 1.034


def torso_radius(z, th):
    cx, f, b, ry, e = interp_table(TORSO, z, 5)
    cs, sn = math.cos(th), math.sin(th)
    ex = 2.0 / e
    x = math.copysign(abs(cs) ** ex, cs) * (f if cs > 0 else b)
    y = math.copysign(abs(sn) ** ex, sn) * ry
    return cx, x, y


# head: z, centre x, front, back, half width
HEAD = [
    (1.568, 0.050, 0.028, 0.028, 0.026),
    (1.580, 0.036, 0.058, 0.058, 0.048),
    (1.600, 0.020, 0.078, 0.074, 0.060),
    (1.625, 0.012, 0.087, 0.085, 0.069),
    (1.650, 0.006, 0.091, 0.094, 0.075),
    (1.680, 0.003, 0.093, 0.099, 0.078),
    (1.710, 0.000, 0.092, 0.100, 0.078),
    (1.740, -0.003, 0.086, 0.096, 0.075),
    (1.765, -0.005, 0.072, 0.083, 0.065),
    (1.785, -0.006, 0.050, 0.059, 0.045),
    (1.797, -0.006, 0.022, 0.026, 0.020),
]
HEAD_TOP = 1.801
HEAD_SCALE = 1.07   # a slightly big head reads better at 160 px


def weights_torso(p):
    z, ay = p.z, abs(p.y)
    w = {}
    a = smooth(0.965, 1.10, z)       # pelvis -> spine1
    b = smooth(1.12, 1.29, z)        # spine1 -> spine2
    w['pelvis'] = 1 - a
    w['spine1'] = a * (1 - b)
    w['spine2'] = a * b
    c = smooth(0.075, 0.165, ay) * smooth(1.29, 1.41, z)
    n = smooth(1.46, 1.50, z) * (1 - smooth(0.05, 0.09, ay)) * 0.35
    side = 'clav_L' if p.y > 0 else 'clav_R'
    tot = 1 - c - n
    w = {k: v * tot for k, v in w.items()}
    w[side] = c
    w['neck'] = n
    return w


def make_arm_weights(rest, sd):
    sh, el, wr = rest['J']['shoulder_' + sd], rest['J']['elbow_' + sd], rest['J']['wrist_' + sd]
    poly = [sh, el, wr]

    def wf(p, i=0):
        t = polyline_param(poly, p)
        c = 1 - smooth(-0.03, 0.05, t)
        e = smooth(L_UP - 0.045, L_UP + 0.045, t)
        return {'clav_' + sd: c, 'up_' + sd: (1 - c) * (1 - e), 'fore_' + sd: (1 - c) * e}
    return wf, poly


def make_leg_weights(rest, sd):
    hip, kn, an = rest['J']['hip_' + sd], rest['J']['knee_' + sd], rest['J']['ankle_' + sd]
    poly = [hip, kn, an]

    def wf(p, i=0):
        t = polyline_param(poly, p)
        a = smooth(-0.04, 0.10, t)
        k = smooth(L_THIGH - 0.05, L_THIGH + 0.05, t)
        return {'pelvis': 1 - a, 'thigh_' + sd: a * (1 - k), 'shin_' + sd: a * k}
    return wf, poly


def weights_neck(p):
    a = smooth(1.445, 1.51, p.z)
    b = smooth(1.575, 1.625, p.z)
    return {'spine2': 1 - a, 'neck': a * (1 - b), 'head': a * b}


def build_body(rest):
    """Deformable body: torso, legs, arms, sleeves, neck, collar, belt."""
    mb = MB()
    N = 28
    # --- torso (trousers below the belt, shirt above) ---
    zs = [0.815 + i * 0.0185 for i in range(37)]
    zs = [z for z in zs if z < 1.492] + [1.492]
    rings = []
    for z in zs:
        cx, f, b, ry, e = interp_table(TORSO, z, 5)
        rings.append(ring_pts(V(cx, 0, z), V(1, 0, 0), V(0, 1, 0), f, b, ry, ry, N, e, th0=math.pi))

    def torso_mat(i, k, cen):
        return 'trousers' if cen.z < TROUSER_TOP else 'shirt'
    loft(mb, rings, zs, torso_mat, lambda p, i: weights_torso(p), perim_period=0.08,
         cap0=V(0, 0, 0.80), cap1=V(-0.016, 0, 1.50))

    # --- belt: a band just outside the torso, with a buckle ---
    bz = [BELT_Z0 - 0.002, BELT_Z0, BELT_Z1, BELT_Z1 + 0.002]
    offs = [0.001, 0.0065, 0.0065, 0.001]
    rings = []
    for z, o in zip(bz, offs):
        pts = []
        for k in range(N * 2):
            th = math.pi + 2 * math.pi * k / (N * 2)
            cx, x, y = torso_radius(z, th)
            d = V(x, y, 0)
            nrm = d.normalized()
            pts.append(V(cx, 0, z) + d + nrm * o)
        rings.append(pts)
    loft(mb, rings, bz, lambda i, k, c: 'belt', lambda p, i: weights_torso(p))
    cx, x, y = torso_radius(1.034, 0.0)
    ellipsoid(mb, V(cx + x + 0.008, 0, 1.034), (0.006, 0.022, 0.019), 'buckle',
              weight=weights_torso(V(0, 0, 1.034)), n=10, rings=6)

    # --- legs (trousers) ---
    for s, sd in ((1, 'L'), (-1, 'R')):
        wf, poly = make_leg_weights(rest, sd)
        # stations: t along hip->knee->ankle, radius
        st = [(-0.075, 0.070), (-0.04, 0.088), (0.0, 0.095), (0.10, 0.090), (0.22, 0.082),
              (0.33, 0.073), (0.415, 0.067), (0.50, 0.065), (0.60, 0.062), (0.70, 0.059),
              (0.78, 0.058), (0.835, 0.059), (0.855, 0.058), (0.862, 0.045)]
        rings, vs = [], []
        for t, r in st:
            c = polyline_point(poly, t)
            tan = (polyline_point(poly, t + 0.01) - polyline_point(poly, t - 0.01)).normalized()
            xa = (V(1, 0, 0) - tan * tan.x).normalized()
            ya = tan.cross(xa)
            # slightly flattened side to side (trouser legs hang flat-ish)
            rings.append(ring_pts(c, xa, -ya if s > 0 else ya, r * 1.04, r * 1.0, r * 0.96, r * 0.96,
                                  N, 2.0, th0=math.pi))
            vs.append(t)
        end = polyline_point(poly, 0.866)
        top = polyline_point(poly, -0.09)
        loft(mb, rings, vs, lambda i, k, c: 'trousers', lambda p, i, wf=wf: wf(p),
             perim_period=0.08, cap0=top, cap1=end)

    # --- arms (skin) and sleeves (shirt) ---
    for s, sd in ((1, 'L'), (-1, 'R')):
        wf, poly = make_arm_weights(rest, sd)
        st = [(0.0, 0.022), (0.012, 0.034), (0.035, 0.041), (0.09, 0.043), (0.16, 0.042),
              (0.23, 0.039), (0.28, 0.036), (0.30, 0.036), (0.335, 0.039), (0.40, 0.037),
              (0.47, 0.031), (0.52, 0.027), (0.54, 0.025)]
        rings, vs = [], []
        for t, r in st:
            c = polyline_point(poly, t)
            tan = (poly[1] - poly[0]).normalized() if t < L_UP else (poly[2] - poly[1]).normalized()
            xa = (V(1, 0, 0) - tan * tan.x).normalized()
            ya = tan.cross(xa)
            # forearm slightly flattened (wider across the palm)
            rings.append(ring_pts(c, xa, ya, r, r, r * 1.08, r * 1.08, 16, 2.0))
            vs.append(t)
        loft(mb, rings, vs, lambda i, k, c: 'skin', lambda p, i, wf=wf: wf(p),
             cap0=polyline_point(poly, -0.006), cap1=polyline_point(poly, 0.552))
        # sleeve: outer surface, folded lip at the opening, inner surface
        tan = (poly[1] - poly[0]).normalized()
        xa = (V(1, 0, 0) - tan * tan.x).normalized()
        ya = tan.cross(xa)
        st = [(-0.020, 0.020), (-0.008, 0.038), (0.008, 0.049), (0.045, 0.054), (0.09, 0.055),
              (0.125, 0.055), (0.143, 0.054), (0.147, 0.050), (0.143, 0.047), (0.12, 0.045)]
        rings, vs = [], []
        for t, r in st:
            c = poly[0] + tan * t
            # the underside of the sleeve (towards the torso) hangs a bit lower
            rings.append(ring_pts(c, xa, ya, r, r, r * 1.02, r * 1.02, 20, 2.0))
            vs.append(t if len(vs) < 7 else 0.29 - t)

        def sleeve_mat(i, k, c):
            return 'cuff' if i >= 5 else 'shirt'
        loft(mb, rings, vs, sleeve_mat, lambda p, i, wf=wf: wf(p), cap0=poly[0] - tan * 0.036)

    # --- neck ---
    pts = [V(-0.032, 0, 1.40), V(-0.022, 0, 1.46), V(-0.008, 0, 1.53), V(0.004, 0, 1.60), V(0.01, 0, 1.645)]
    rads = [0.056, 0.057, 0.055, 0.052, 0.048]
    rings, vs = [], []
    for (pp, tt, xa, ya, sacc), r in zip(path_frames(pts, V(1, 0, 0)), rads):
        rings.append(ring_pts(pp, xa, ya, r * 1.02, r, r * 0.97, r * 0.97, 20, 2.0))
        vs.append(sacc)
    loft(mb, rings, vs, lambda i, k, c: 'skin', lambda p, i: weights_neck(p),
         cap0=V(-0.035, 0, 1.39), cap1=V(0.012, 0, 1.652))

    # --- polo collar: stand + turned-down flap, open at the front ---
    prof = [(0.059, 1.468), (0.060, 1.497), (0.062, 1.516), (0.068, 1.523),
            (0.078, 1.512), (0.088, 1.492), (0.097, 1.470)]
    na = 30
    rings = []
    th_open = math.radians(26)
    axis_c = V(-0.018, 0, 0)
    for r, z in prof:
        pts = []
        for k in range(na + 1):
            th = th_open + (2 * math.pi - 2 * th_open) * k / na
            d = V(math.cos(th), math.sin(th), 0)
            q = axis_c + d * r + V(0, 0, z)
            # keep the flap on the shirt: at least 4 mm outside the torso
            cx, x, y = torso_radius(min(z, 1.49), th)
            surf = V(cx + x, y, 0)
            if z < 1.50 and V(q.x, q.y, 0).length < surf.length + 0.004:
                q.xy = (surf.normalized() * (surf.length + 0.004)).xy
            pts.append(q)
        rings.append(pts)
    vs = [z for r, z in prof]
    # open ring: build as a loft over "rings" with closed=False
    loft(mb, rings, vs, lambda i, k, c: 'collar',
         lambda p, i: {'spine2': 0.7, 'neck': 0.3}, closed=False)
    return mb


def build_head():
    """Head (skin) with ears, nose, eyes, brows; returns MB in rest world
    coordinates, plus the hair MB."""
    mb = MB()
    N = 32
    zs = []
    z = HEAD[0][0]
    while z < HEAD[-1][0]:
        zs.append(z)
        z += 0.012
    zs.append(HEAD[-1][0])
    rings = []
    for z in zs:
        cx, f, b, ry = interp_table(HEAD, z, 4)
        rings.append(ring_pts(V(cx, 0, z), V(1, 0, 0), V(0, 1, 0), f, b, ry, ry, N, 2.25, th0=math.pi))
    loft(mb, rings, zs, lambda i, k, c: 'skin', lambda p, i: {},
         cap0=V(0.05, 0, 1.563), cap1=V(-0.006, 0, HEAD_TOP))
    # nose, ears, eyes, brows
    ellipsoid(mb, V(0.093, 0, 1.652), (0.020, 0.0135, 0.027), 'skin', rot=R3('Y', -18))
    for s in (1, -1):
        ellipsoid(mb, V(-0.006, s * 0.077, 1.664), (0.021, 0.013, 0.031), 'skin', rot=R3('Z', s * 12))
        ellipsoid(mb, V(0.083, s * 0.033, 1.682), (0.009, 0.0125, 0.0075), 'eye')
        ellipsoid(mb, V(0.087, s * 0.035, 1.702), (0.008, 0.019, 0.0055), 'brow', rot=R3('X', s * -8))
    # a hint of a mouth
    ellipsoid(mb, V(0.089, 0, 1.612), (0.005, 0.017, 0.003), 'eye')
    return mb


def hairline(phi):
    """Height of the hairline at angle phi around the head (0 = front)."""
    a = abs(phi)
    tab = [(0.0, 1.738), (0.55, 1.728), (0.95, 1.712), (1.25, 1.700), (1.42, 1.655),
           (1.62, 1.66), (1.80, 1.70), (2.2, 1.66), (2.7, 1.625), (math.pi, 1.618)]
    return interp_table(tab, a, 1)[0]


def build_hair(head_obj):
    """Hair: a shell offset from the (subdivided) head above the hairline."""
    dg = bpy.context.evaluated_depsgraph_get()
    ev = head_obj.evaluated_get(dg)
    me = ev.to_mesh()
    bm = bmesh.new()
    bm.from_mesh(me)
    ev.to_mesh_clear()
    bm.verts.ensure_lookup_table()
    # the head is all loft 0; only keep faces of the skull loft (skip features)
    keep = []
    for f in bm.faces:
        c = f.calc_center_median()
        phi = math.atan2(c.y, c.x)
        if c.z > hairline(phi) and abs(c.y) < 0.083 and c.x < 0.098:
            keep.append(f)
    vmap = {}
    mb = MB()
    for f in keep:
        idx = []
        for v in f.verts:
            if v.index not in vmap:
                p = v.co
                phi = math.atan2(p.y, p.x)
                h = hairline(phi)
                # thicker on top, thin at the hairline, a small quiff in front
                th = 0.004 + 0.012 * smooth(h, h + 0.05, p.z) + 0.006 * smooth(1.70, 1.79, p.z) * smooth(-0.02, 0.07, p.x)
                vmap[v.index] = mb.vert(p + v.normal * th)
            idx.append(vmap[v.index])
        mb.face(idx, 'hair', [(0, 0)] * len(idx))
    bm.free()
    return mb


def build_shoe(s):
    """Shoe in foot-local coordinates (origin on the ground under the ankle,
    +X along the foot)."""
    mb = MB()
    # x, top height, half width, y shift
    st = [(-0.082, 0.050, 0.030, 0.0), (-0.072, 0.072, 0.038, 0.0), (-0.04, 0.086, 0.043, 0.0),
          (0.0, 0.092, 0.046, 0.0), (0.05, 0.078, 0.049, 0.002), (0.10, 0.058, 0.051, 0.004),
          (0.15, 0.044, 0.048, 0.006), (0.185, 0.034, 0.040, 0.007), (0.205, 0.024, 0.026, 0.007)]
    rings, vs = [], []
    for x, h, hw, ys in st:
        c = V(x, s * ys, h * 0.45)
        rings.append(ring_pts(c, V(0, 0, 1), V(0, 1, 0), h * 0.55, h * 0.45, hw, hw, 20, 2.6))
        vs.append(x)

    def mat(i, k, c):
        if c.z < 0.011:
            return 'shoeB'
        if -0.005 < c.x < 0.105 and c.z > 0.03:
            return 'shoeB'
        return 'shoeA'
    loft(mb, rings, vs, mat, lambda p, i: {}, cap0=V(-0.088, 0, 0.022), cap1=V(0.212, s * 0.007, 0.012))
    return mb


def build_hand(s):
    """Fist in hand-local coordinates: origin at the wrist, +Y towards the
    knuckles, +X along the grip axis (towards the club head)."""
    mb = MB()
    mat = 'glove' if s > 0 else 'skin'
    # the palm/back block from the wrist
    st = [(0.0, 0.019, 0.016), (0.018, 0.028, 0.019), (0.038, 0.035, 0.021)]
    rings, vs = [], []
    for y, rx, rz in st:
        rings.append(ring_pts(V(0.004, y, 0), V(1, 0, 0), V(0, 0, 1), rx, rx, rz, rz, 16, 2.4))
        vs.append(y)
    loft(mb, rings, vs, lambda i, k, c: mat, lambda p, i: {}, cap0=V(0.004, -0.008, 0))
    # the curled fingers round the grip: a loft along X
    st = [(-0.036, 0.024, 0.019), (-0.028, 0.030, 0.024), (0.0, 0.034, 0.027), (0.030, 0.034, 0.027),
          (0.040, 0.028, 0.022)]
    rings, vs = [], []
    for x, ry, rz in st:
        rings.append(ring_pts(V(x, 0.056, -0.004), V(0, 1, 0), V(0, 0, 1), ry, ry * 0.9, rz, rz, 16, 2.3))
        vs.append(x)
    loft(mb, rings, vs, lambda i, k, c: mat, lambda p, i: {}, cap0=V(-0.043, 0.056, -0.004),
         cap1=V(0.046, 0.056, -0.004))
    # thumb along the grip towards the head
    ellipsoid(mb, V(0.036, 0.046, s * 0.022), (0.026, 0.012, 0.011), mat, rot=R3('Z', 30), n=10, rings=6)
    if s > 0:
        # glove cuff
        st = [(-0.012, 0.024, 0.020), (0.004, 0.025, 0.021)]
        rings = [ring_pts(V(0.004, y, 0), V(1, 0, 0), V(0, 0, 1), rx, rx, rz, rz, 16, 2.2) for y, rx, rz in st]
        loft(mb, rings, [0, 0.016], lambda i, k, c: mat, lambda p, i: {})
    return mb


def build_club():
    """Club in club-local coordinates (butt at origin, shaft along -Z)."""
    mb = MB()
    st = [(0.0, 0.0105, 'grip'), (0.006, 0.0125, 'grip'), (0.14, 0.0118, 'grip'), (0.27, 0.0102, 'grip'),
          (0.275, 0.0078, 'shaft'), (0.6, 0.0070, 'shaft'), (1.0, 0.0058, 'shaft'),
          (L_SHAFT, 0.0055, 'shaft'), (L_SHAFT + 0.004, 0.0085, 'clubhead'),
          (L_SHAFT + 0.04, 0.0085, 'clubhead')]
    rings, vs, mats = [], [], []
    for z, r, m in st:
        rings.append(ring_pts(V(0, 0, -z), V(1, 0, 0), V(0, 1, 0), r, r, r, r, 10, 2.0))
        vs.append(z)
        mats.append(m)
    loft(mb, rings, vs, lambda i, k, c: mats[i + 1] if i + 1 < len(mats) else mats[-1], lambda p, i: {},
         cap0=V(0, 0, 0.002))
    # driver head, built in the head frame then moved into club local
    Hf = club_head_frame()
    st = [(0.004, 0.010, 0.010, 0.016, 0.020), (0.02, 0.020, 0.021, 0.036, 0.046),
          (0.045, 0.027, 0.027, 0.044, 0.060), (0.075, 0.027, 0.027, 0.045, 0.061),
          (0.100, 0.023, 0.024, 0.039, 0.048), (0.115, 0.014, 0.015, 0.025, 0.027)]
    rings, vs = [], []
    for x, up, dn, fc, bk in st:
        c = V(x, -0.045, 0.027)
        pts = ring_pts(c, V(0, 0, 1), V(0, 1, 0), up, dn, fc, bk, 20, 2.3)
        rings.append([Hf @ p for p in pts])
        vs.append(x)
    loft(mb, rings, vs, lambda i, k, c: 'clubhead', lambda p, i: {},
         cap0=Hf @ V(-0.002, -0.045, 0.027), cap1=Hf @ V(0.121, -0.045, 0.027))
    return mb


# --- hats (rest world coordinates round the rest head) ---

def hat_crown(mb, table, mat_fn, n=32, th0=math.pi, cap=None, e=2.2, inner_lip=True):
    """table rows: z, cx, front, back, half width."""
    rings, vs = [], []
    if inner_lip:
        z, cx, f, b, ry = table[0]
        rings.append(ring_pts(V(cx, 0, z + 0.012), V(1, 0, 0), V(0, 1, 0), f - 0.004, b - 0.004,
                              ry - 0.004, ry - 0.004, n, e, th0))
        vs.append(-0.012)
    for z, cx, f, b, ry in table:
        rings.append(ring_pts(V(cx, 0, z), V(1, 0, 0), V(0, 1, 0), f, b, ry, ry, n, e, th0))
        vs.append(z)
    loft(mb, rings, vs, mat_fn, lambda p, i: {}, cap1=cap)


def bill(mb, x0, x1, widths, z0, pitch, camber, thick, mat, xc=0.0):
    """A cap bill: lens-shaped cross sections swept forwards along X."""
    rings, vs = [], []
    for i, w in enumerate(widths):
        x = x0 + (x1 - x0) * i / (len(widths) - 1)
        z = z0 - (x - x0) * math.tan(math.radians(pitch))
        pts = ring_pts(V(x + xc, 0, z), V(0, 0, 1), V(0, 1, 0), thick, thick, w, w, 24, 2.0)
        for p in pts:
            p.z -= camber * (p.y / 0.09) ** 2
        rings.append(pts)
        vs.append(x)
    loft(mb, rings, vs, lambda i, k, c: mat, lambda p, i: {},
         cap1=V(x1 + xc + 0.004, 0, z0 - (x1 - x0) * math.tan(math.radians(pitch)) - camber * 0.0))


def brim(mb, zc, cx, base, width, drop, thick, mat, curl=0.0, n=40):
    """A round brim from the crown base outwards: top surface out, bottom back."""
    f0, b0, r0 = base
    rings, vs = [], []
    steps = 5
    prof = []
    for i in range(steps + 1):
        t = i / steps
        prof.append((t, thick * 0.5))
    for i in range(steps, -1, -1):
        t = i / steps
        prof.append((t, -thick * 0.5))
    for t, dz in prof:
        pts = ring_pts(V(cx, 0, 0), V(1, 0, 0), V(0, 1, 0), f0 + width * t, b0 + width * t,
                       r0 + width * t, r0 + width * t, n, 2.1, math.pi)
        out = []
        for p in pts:
            side = (p.y / (r0 + width)) ** 2
            z = zc - drop * t + dz + curl * side * t * t - curl * 0.25 * (1 - side) * t * t
            out.append(V(p.x, p.y, z))
        rings.append(out)
        vs.append(t)
    loft(mb, rings, vs, lambda i, k, c: mat, lambda p, i: {})


def build_hats():
    hats = []
    # 0 baseball cap
    mb = MB()
    hat_crown(mb, [(1.708, -0.004, 0.108, 0.116, 0.095), (1.745, -0.004, 0.109, 0.117, 0.096),
                   (1.785, -0.004, 0.097, 0.106, 0.086), (1.812, -0.004, 0.074, 0.082, 0.066),
                   (1.832, -0.004, 0.044, 0.050, 0.040), (1.842, -0.004, 0.014, 0.016, 0.013)],
              lambda i, k, c: 'hatA', cap=V(-0.004, 0, 1.844))
    ellipsoid(mb, V(-0.004, 0, 1.845), (0.008, 0.008, 0.004), 'hatA', n=8, rings=5)
    bill(mb, 0.07, 0.186, [0.080, 0.088, 0.086, 0.078, 0.062, 0.035], 1.713, 9, 0.016, 0.0035, 'hatB')
    hats.append(mb)
    # 1 visor: a band and a bill
    mb = MB()
    rings, vs = [], []
    for z, o in ((1.697, 0.006), (1.700, 0.011), (1.738, 0.011), (1.741, 0.006), (1.738, 0.003), (1.700, 0.003)):
        cx, f, b, ry = interp_table(HEAD, z, 4)
        rings.append(ring_pts(V(cx, 0, z), V(1, 0, 0), V(0, 1, 0), f + 0.012 + o, b + 0.012 + o,
                              ry + 0.012 + o, ry + 0.012 + o, 32, 2.25, math.pi))
        vs.append(z)
    loft(mb, rings + [rings[0]], vs + [vs[0]], lambda i, k, c: 'hatA', lambda p, i: {})
    bill(mb, 0.075, 0.19, [0.078, 0.088, 0.088, 0.080, 0.064, 0.036], 1.708, 11, 0.018, 0.0035, 'hatB')
    hats.append(mb)
    # 2 bucket hat
    mb = MB()
    hat_crown(mb, [(1.702, -0.004, 0.107, 0.115, 0.094), (1.73, -0.004, 0.106, 0.114, 0.093),
                   (1.80, -0.004, 0.099, 0.106, 0.088), (1.835, -0.004, 0.088, 0.094, 0.078),
                   (1.848, -0.004, 0.060, 0.064, 0.053), (1.851, -0.004, 0.02, 0.02, 0.02)],
              lambda i, k, c: 'hatB' if c.z < 1.728 else 'hatA', cap=V(-0.004, 0, 1.852))
    brim(mb, 1.704, -0.004, (0.107, 0.115, 0.094), 0.062, 0.030, 0.006, 'hatA')
    hats.append(mb)
    # 3 beanie with a pompom
    mb = MB()
    hat_crown(mb, [(1.683, -0.004, 0.104, 0.112, 0.092), (1.74, -0.004, 0.107, 0.115, 0.094),
                   (1.79, -0.004, 0.096, 0.103, 0.084), (1.828, -0.004, 0.070, 0.075, 0.062),
                   (1.851, -0.004, 0.036, 0.038, 0.032), (1.858, -0.004, 0.01, 0.01, 0.01)],
              lambda i, k, c: 'hatA', cap=V(-0.004, 0, 1.859))
    rings, vs = [], []
    for z, o in ((1.680, 0.002), (1.683, 0.009), (1.724, 0.010), (1.728, 0.004)):
        rings.append(ring_pts(V(-0.004, 0, z), V(1, 0, 0), V(0, 1, 0), 0.104 + o, 0.112 + o, 0.092 + o,
                              0.092 + o, 36, 2.2, math.pi))
        vs.append(z)
    loft(mb, rings, vs, lambda i, k, c: 'hatB', lambda p, i: {})
    ellipsoid(mb, V(-0.006, 0, 1.884), (0.034, 0.034, 0.031), 'pompom', n=16, rings=10)
    hats.append(mb)
    # 4 flat cap (newsboy)
    mb = MB()
    hat_crown(mb, [(1.706, -0.004, 0.107, 0.115, 0.094), (1.742, 0.008, 0.120, 0.116, 0.098),
                   (1.770, 0.016, 0.127, 0.108, 0.097), (1.790, 0.018, 0.119, 0.094, 0.088),
                   (1.801, 0.018, 0.094, 0.074, 0.068), (1.806, 0.018, 0.04, 0.03, 0.03)],
              lambda i, k, c: 'hatA', cap=V(0.02, 0, 1.807))
    bill(mb, 0.085, 0.152, [0.078, 0.080, 0.074, 0.058, 0.030], 1.716, 22, 0.010, 0.003, 'hatB')
    hats.append(mb)
    # 5 cowboy hat: tall creased crown, band, wide brim curled up at the sides
    mb = MB()
    hat_crown(mb, [(1.703, -0.004, 0.106, 0.114, 0.093), (1.80, -0.004, 0.101, 0.108, 0.088),
                   (1.87, -0.004, 0.092, 0.098, 0.079), (1.898, -0.004, 0.080, 0.086, 0.066),
                   (1.892, -0.004, 0.046, 0.050, 0.030), (1.878, -0.004, 0.012, 0.012, 0.006)],
              lambda i, k, c: 'hatA', cap=V(-0.004, 0, 1.876))
    rings, vs = [], []
    for z, o in ((1.703, 0.001), (1.706, 0.004), (1.738, 0.0035), (1.741, 0.0)):
        cx, f, b, ry = -0.004, 0.106 - (z - 1.703) * 0.05, 0.114 - (z - 1.703) * 0.06, 0.093 - (z - 1.703) * 0.05
        rings.append(ring_pts(V(cx, 0, z), V(1, 0, 0), V(0, 1, 0), f + o, b + o, ry + o, ry + o, 36, 2.2, math.pi))
        vs.append(z)
    loft(mb, rings, vs, lambda i, k, c: 'hatB', lambda p, i: {})
    brim(mb, 1.706, -0.004, (0.106, 0.114, 0.093), 0.105, 0.0, 0.006, 'hatA', curl=0.07)
    hats.append(mb)
    return hats


HAT_NAMES = ['baseball cap', 'visor', 'bucket hat', 'beanie with pompom', 'flat cap', 'cowboy hat']

# ---------------------------------------------------------------------------
# Materials
# ---------------------------------------------------------------------------

REGION = {'skin': 1, 'eye': 1, 'hair': 2, 'brow': 2, 'shirt': 3, 'collar': 4, 'cuff': 4,
          'trousers': 5, 'shoeA': 7, 'shoeB': 8, 'belt': 9, 'buckle': 11, 'glove': 10,
          'clubhead': 11, 'shaft': 12, 'grip': 13, 'hatA': 14, 'hatB': 15, 'pompom': 15}

SHADE = {  # base grey, roughness, specular, metallic, bump kind
    'skin': (0.8, 0.50, 0.30, 0.0, None),
    'eye': (0.07, 0.30, 0.50, 0.0, None),
    'hair': (0.8, 0.50, 0.30, 0.0, 'hair'),
    'brow': (0.8, 0.60, 0.20, 0.0, None),
    'shirt': (0.8, 0.85, 0.15, 0.0, 'shirt'),
    'collar': (0.8, 0.85, 0.15, 0.0, None),
    'cuff': (0.8, 0.85, 0.15, 0.0, None),
    'trousers': (0.8, 0.75, 0.20, 0.0, 'trousers'),
    'shoeA': (0.8, 0.30, 0.50, 0.0, None),
    'shoeB': (0.8, 0.40, 0.40, 0.0, None),
    'belt': (0.8, 0.35, 0.45, 0.0, None),
    'buckle': (0.8, 0.25, 0.50, 0.8, None),
    'glove': (0.8, 0.65, 0.25, 0.0, None),
    'clubhead': (0.8, 0.22, 0.50, 0.6, None),
    'shaft': (0.8, 0.20, 0.50, 0.8, None),
    'grip': (0.8, 0.85, 0.15, 0.0, None),
    'hatA': (0.8, 0.80, 0.18, 0.0, None),
    'hatB': (0.8, 0.70, 0.22, 0.0, None),
    'pompom': (0.8, 0.95, 0.10, 0.0, 'fluff'),
}

MATS = {'shade': {}, 'id': {}}


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


def _uv(nt, name):
    n = nt.nodes.new('ShaderNodeUVMap')
    n.uv_map = name
    s = nt.nodes.new('ShaderNodeSeparateXYZ')
    nt.links.new(n.outputs['UV'], s.inputs[0])
    return s.outputs[0], s.outputs[1]


def _clear(nt):
    for n in list(nt.nodes):
        nt.nodes.remove(n)
    return nt.nodes.new('ShaderNodeOutputMaterial')


def make_materials():
    for key, (base, rough, spec, metal, bump) in SHADE.items():
        m = bpy.data.materials.new('S_' + key)
        m.use_nodes = True
        nt = m.node_tree
        out = _clear(nt)
        b = nt.nodes.new('ShaderNodeBsdfPrincipled')
        b.inputs['Base Color'].default_value = (base, base, base, 1)
        b.inputs['Roughness'].default_value = rough
        b.inputs['Specular'].default_value = spec
        b.inputs['Metallic'].default_value = metal
        nt.links.new(b.outputs[0], out.inputs['Surface'])
        if bump:
            u, v = _uv(nt, 'pat')
            comb = nt.nodes.new('ShaderNodeCombineXYZ')
            height = None
            if bump == 'shirt':
                # soft horizontal folds, stronger near the waist
                nt.links.new(_math(nt, 'MULTIPLY', u, 0.6), comb.inputs[0])
                nt.links.new(_math(nt, 'MULTIPLY', v, 2.2), comb.inputs[1])
                nz = nt.nodes.new('ShaderNodeTexNoise')
                nz.inputs['Scale'].default_value = 6.0
                nz.inputs['Detail'].default_value = 2.0
                nt.links.new(comb.outputs[0], nz.inputs['Vector'])
                height = nz.outputs['Fac']
                strength = 0.35
            elif bump == 'trousers':
                # the crease: a ridge down the front of each leg (u = half
                # perimeter), plus loose folds
                nt.links.new(_math(nt, 'MULTIPLY', u, 1.0), comb.inputs[0])
                nt.links.new(_math(nt, 'MULTIPLY', v, 0.5), comb.inputs[1])
                nz = nt.nodes.new('ShaderNodeTexNoise')
                nz.inputs['Scale'].default_value = 5.0
                nz.inputs['Detail'].default_value = 1.0
                nt.links.new(comb.outputs[0], nz.inputs['Vector'])
                height = nz.outputs['Fac']
                strength = 0.25
            elif bump == 'hair':
                nt.links.new(_math(nt, 'MULTIPLY', u, 40.0), comb.inputs[0])
                nt.links.new(_math(nt, 'MULTIPLY', v, 8.0), comb.inputs[1])
                nz = nt.nodes.new('ShaderNodeTexNoise')
                nz.inputs['Scale'].default_value = 1.0
                nt.links.new(comb.outputs[0], nz.inputs['Vector'])
                height = nz.outputs['Fac']
                strength = 0.3
            else:  # fluff
                nz = nt.nodes.new('ShaderNodeTexNoise')
                nz.inputs['Scale'].default_value = 90.0
                nz.inputs['Detail'].default_value = 3.0
                height = nz.outputs['Fac']
                strength = 0.6
            bn = nt.nodes.new('ShaderNodeBump')
            bn.inputs['Strength'].default_value = strength
            bn.inputs['Distance'].default_value = 0.01
            nt.links.new(height, bn.inputs['Height'])
            nt.links.new(bn.outputs['Normal'], b.inputs['Normal'])
        MATS['shade'][key] = m

    for key, rid in REGION.items():
        m = bpy.data.materials.new('I_' + key)
        m.use_nodes = True
        nt = m.node_tree
        out = _clear(nt)
        em = nt.nodes.new('ShaderNodeEmission')
        em.inputs['Strength'].default_value = 1.0
        va, vb = rid / 16.0, (rid + 1) / 16.0
        if key in ('shirt', 'trousers'):
            u, v = _uv(nt, 'pat')
            if key == 'shirt':
                # horizontal stripes: 7.5 cm period, 3.2 cm band of shirt B
                f = _math(nt, 'LESS_THAN', _math(nt, 'FRACT', _math(nt, 'DIVIDE', _math(nt, 'ADD', v, 0.012), 0.075)), 0.43)
                # the placket at the front of the neck opening
                rx, ry = _uv(nt, 'rxy')
                rz, _ = _uv(nt, 'rz')
                pl = _math(nt, 'MULTIPLY', _math(nt, 'LESS_THAN', _math(nt, 'ABSOLUTE', ry), 0.017),
                           _math(nt, 'MULTIPLY', _math(nt, 'GREATER_THAN', rx, 0.0),
                                 _math(nt, 'GREATER_THAN', rz, 1.375)))
                f = _math(nt, 'MAXIMUM', f, pl)
            else:
                # check: 8 cm squares, 2.6 cm bands of trousers B both ways
                a = _math(nt, 'LESS_THAN', _math(nt, 'FRACT', _math(nt, 'DIVIDE', u, 0.075)), 0.27)
                b2 = _math(nt, 'LESS_THAN', _math(nt, 'FRACT', _math(nt, 'DIVIDE', v, 0.075)), 0.27)
                f = _math(nt, 'MAXIMUM', a, b2)
            val = _math(nt, 'ADD', va, _math(nt, 'MULTIPLY', f, vb - va))
            comb = nt.nodes.new('ShaderNodeCombineXYZ')
            for i in range(3):
                nt.links.new(val, comb.inputs[i])
            nt.links.new(comb.outputs[0], em.inputs['Color'])
        else:
            em.inputs['Color'].default_value = (va, va, va, 1)
        nt.links.new(em.outputs[0], out.inputs['Surface'])
        MATS['id'][key] = m
    # plain materials for the scene
    g = bpy.data.materials.new('ground')
    g.use_nodes = True
    g.node_tree.nodes['Principled BSDF'].inputs['Base Color'].default_value = (0.22, 0.22, 0.22, 1)
    g.node_tree.nodes['Principled BSDF'].inputs['Roughness'].default_value = 0.9
    MATS['ground'] = g
    mk = bpy.data.materials.new('marker')
    mk.use_nodes = True
    nt = mk.node_tree
    out = _clear(nt)
    em = nt.nodes.new('ShaderNodeEmission')
    em.inputs['Color'].default_value = (1, 0, 1, 1)
    nt.links.new(em.outputs[0], out.inputs['Surface'])
    MATS['marker'] = mk


# ---------------------------------------------------------------------------
# Blender objects
# ---------------------------------------------------------------------------

def mb_to_object(name, mb, subsurf=2, coll=None):
    me = bpy.data.meshes.new(name)
    me.from_pydata([tuple(v) for v in mb.v], [], mb.f)
    me.update()
    keys = sorted(set(mb.fm))
    for k in keys:
        me.materials.append(MATS['shade'][k])
    me.polygons.foreach_set('material_index', [keys.index(k) for k in mb.fm])
    me.polygons.foreach_set('use_smooth', [True] * len(mb.f))
    pat = me.uv_layers.new(name='pat')
    rxy = me.uv_layers.new(name='rxy')
    rz = me.uv_layers.new(name='rz')
    flat_uv, flat_xy, flat_z = [], [], []
    for fi, f in enumerate(mb.f):
        for ci, vi in enumerate(f):
            flat_uv += mb.fuv[fi][ci]
            p = mb.v[vi]
            flat_xy += (p.x, p.y)
            flat_z += (p.z, 0.0)
    pat.data.foreach_set('uv', flat_uv)
    rxy.data.foreach_set('uv', flat_xy)
    rz.data.foreach_set('uv', flat_z)
    bm = bmesh.new()
    bm.from_mesh(me)
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    bm.to_mesh(me)
    bm.free()
    ob = bpy.data.objects.new(name, me)
    (coll or bpy.context.scene.collection).objects.link(ob)
    ob['matkeys'] = keys
    if subsurf:
        m = ob.modifiers.new('sub', 'SUBSURF')
        m.levels = subsurf
        m.render_levels = subsurf
    return ob


def set_pass_materials(objs, kind):
    for ob in objs:
        keys = list(ob.get('matkeys', []))
        for i, k in enumerate(keys):
            ob.data.materials[i] = MATS[kind][k]


# ---------------------------------------------------------------------------
# Skinning (dual quaternions, numpy)
# ---------------------------------------------------------------------------

class Skin:
    def __init__(self, mb, rest):
        self.rest = np.array([tuple(v) for v in mb.v], np.float64)
        n = len(mb.v)
        W = np.zeros((n, len(BONES)))
        for i, w in enumerate(mb.w):
            for k, x in w.items():
                if x > 1e-6:
                    W[i, BI[k]] += x
        s = W.sum(1, keepdims=True)
        assert (s > 0).all(), 'unweighted vertices'
        self.W = W / s
        self.ref = self.W.argmax(1)
        self.rest_inv = [rest['M'][b].inverted() for b in BONES]

    def deform(self, M):
        Q, D = [], []
        for bi, b in enumerate(BONES):
            K = M[b] @ self.rest_inv[bi]
            q = K.to_quaternion().normalized()
            t = K.translation
            qt = Quaternion((0.0, t.x, t.y, t.z))
            d = qt @ q
            Q.append((q.w, q.x, q.y, q.z))
            D.append((0.5 * d.w, 0.5 * d.x, 0.5 * d.y, 0.5 * d.z))
        Q, D = np.array(Q), np.array(D)
        sign = np.sign(Q[self.ref] @ Q.T)          # (n, bones): align hemispheres
        sign[sign == 0] = 1
        WS = self.W * sign
        br = WS @ Q
        bd = WS @ D
        ln = np.linalg.norm(br, axis=1, keepdims=True)
        br /= ln
        bd /= ln
        w, u = br[:, :1], br[:, 1:]
        dw, du = bd[:, :1], bd[:, 1:]
        v = self.rest
        uv = np.cross(u, v)
        out = v + 2 * w * uv + 2 * np.cross(u, uv)
        t = 2 * (w * du - dw * u + np.cross(u, du))
        return out + t


# ---------------------------------------------------------------------------
# Poses
# ---------------------------------------------------------------------------

def deep_merge(base, over):
    out = dict(base)
    out.update(over)
    return out


def hermite(ts, vs, f):
    n = len(ts)
    if f <= ts[0]:
        return vs[0]
    if f >= ts[-1]:
        return vs[-1]
    i = max(j for j in range(n - 1) if ts[j] <= f)
    t0, t1 = ts[i], ts[i + 1]

    def tan(j):
        if j == 0 or j == n - 1:
            return 0.0
        return (vs[j + 1] - vs[j - 1]) / (ts[j + 1] - ts[j - 1])
    m0, m1 = tan(i) * (t1 - t0), tan(i + 1) * (t1 - t0)
    s = (f - t0) / (t1 - t0)
    return ((2 * s ** 3 - 3 * s ** 2 + 1) * vs[i] + (s ** 3 - 2 * s ** 2 + s) * m0 +
            (-2 * s ** 3 + 3 * s ** 2) * vs[i + 1] + (s ** 3 - s ** 2) * m1)


def interp(keys, f):
    ts = [k[0] for k in keys]
    out = {}
    for name in keys[0][1]:
        vals = [k[1][name] for k in keys]
        v0 = vals[0]
        if isinstance(v0, (int, float)):
            out[name] = hermite(ts, vals, f)
        elif isinstance(v0, tuple) and all(isinstance(x, (int, float)) for x in v0):
            out[name] = tuple(hermite(ts, [v[i] for v in vals], f) for i in range(len(v0)))
        else:
            j = max(i for i in range(len(ts)) if ts[i] <= f) if f >= ts[0] else 0
            out[name] = vals[j]
    return out


REST_POSE = {
    'pelvis_pos': (0.0, 0.0, 0.95), 'pelvis_rot': (0.0, 0.0, 0.0), 'chest_rot': (0.0, 0.0, 0.0),
    'head_rot': (0.0, 0.0, 0.0), 'clav_L': (0.0, 0.0), 'clav_R': (0.0, 0.0),
    'foot_L': (0.175, 0.10, 0.0, 0.0), 'foot_R': (0.175, -0.10, 0.0, 0.0),
    'knee_L': (1.0, 0.0, 0.0), 'knee_R': (1.0, 0.0, 0.0),
    'elbow_L': (-1.0, 0.0, 0.0), 'elbow_R': (-1.0, 0.0, 0.0),
    'club_H': (0.3, 0.0, 0.5), 'club_S': (0.0, 0.0, -1.0), 'club_toe': (1.0, 0.0, 0.0),
    'grip_L': -1.0, 'grip_R': -1.0, 'wrist_L': (0, 0, 0), 'wrist_R': (0, 0, 0),
    'handY_L': None, 'handY_R': None, 'handX_L': None, 'handX_R': None,
    'reach_w': 0.0, 'reach': 0.975,
}


def address_club(lean=0.0, shaft=52.0, hosel=(-0.045, -0.035, 0.028)):
    """Club soled behind the ball: returns (H, S). lean > 0 leans the shaft
    towards the target (hands ahead)."""
    a = math.radians(shaft)
    S = V(math.cos(a), -math.sin(math.radians(lean)), -math.sin(a)).normalized()
    Hp = V(*hosel) - S * (L_SHAFT - GRIP_H)
    return tuple(Hp), tuple(S)


FX = FEET_X


def club_soled(H, sole):
    """Club resting with its sole at 'sole', the hands near H: returns (H, S)."""
    H, sole = V(*H), V(*sole)
    S = (sole - H).normalized()
    d = L_SHAFT + 0.035 - GRIP_H
    return tuple(sole - S * d), tuple(S)
ADDRESS = deep_merge(REST_POSE, {
    'pelvis_pos': (FX - 0.135, 0.0, 0.915), 'pelvis_rot': (22.0, 0.0, 0.0),
    'chest_rot': (44.0, 9.0, 0.0), 'head_rot': (50.0, 4.0, 0.0),
    'clav_L': (-2.0, 10.0), 'clav_R': (-4.0, 12.0),
    'foot_L': (FX + 0.115, 0.235, 22.0, 0.0), 'foot_R': (FX + 0.115, -0.215, -6.0, 0.0),
    'knee_L': (1.0, 0.18, 0.0), 'knee_R': (1.0, -0.05, 0.0),
    'elbow_L': (-0.5, 0.5, -1.0), 'elbow_R': (-0.6, -0.4, -1.0),
    'club_toe': (1.0, 0.0, 0.4), 'grip_L': GRIP_L, 'grip_R': GRIP_R,
})
_H0, _S0 = address_club()
ADDRESS['club_H'], ADDRESS['club_S'] = _H0, _S0


def K(f, **over):
    return (f, deep_merge(ADDRESS, over))


def swing_keys():
    Hi, Si = address_club(lean=8.0, shaft=57.0, hosel=(-0.045, -0.005, 0.028))
    F = FX + 0.115
    ks = [
        K(0),
        K(2, pelvis_rot=(22, 0, -5), chest_rot=(44, 8, -16), club_H=(-0.66, -0.08, 0.81),
          club_S=(0.52, -0.42, -0.74), club_toe=(0.8, 0.0, 0.6), clav_L=(-1, 13), clav_R=(-3, 15),
          reach_w=0.5, reach=0.955),
        K(4, pelvis_rot=(22, 1, -14), chest_rot=(43, 6, -40), club_H=(-0.70, -0.30, 0.90),
          club_S=(0.34, -0.94, -0.03), club_toe=(0.0, 0.0, 1.0), clav_L=(0, 16), clav_R=(-1, 10),
          elbow_R=(-0.6, -0.2, -1.0), reach_w=1.0, reach=0.96),
        K(6, pelvis_pos=(FX - 0.137, -0.012, 0.915), pelvis_rot=(22, 2, -27), chest_rot=(42, 5, -66),
          club_H=(-0.80, -0.43, 1.20), club_S=(-0.40, -0.25, 0.88), club_toe=(-0.3, -1.0, 0.3),
          clav_L=(2, 19), clav_R=(1, -2), elbow_R=(-0.3, -0.3, -1.0), elbow_L=(-0.3, 0.3, -1.0),
          head_rot=(50, 4, -4), knee_L=(1.0, -0.15, 0.0), reach_w=1.0),
        K(8, pelvis_pos=(FX - 0.138, -0.018, 0.915), pelvis_rot=(22, 3, -38), chest_rot=(41, 5, -84),
          club_H=(-0.87, -0.36, 1.50), club_S=(-0.30, 0.45, 0.84), club_toe=(-0.5, 0.2, -0.6),
          clav_L=(4, 21), clav_R=(4, -6), elbow_R=(-0.2, -0.2, -1.0), elbow_L=(-0.3, 0.2, -1.0),
          head_rot=(50, 4, -7), knee_L=(1.0, -0.25, 0.0), reach_w=1.0),
        K(10, pelvis_pos=(FX - 0.138, -0.022, 0.915), pelvis_rot=(22, 3, -45), chest_rot=(40, 4, -100),
          club_H=(-0.96, -0.16, 1.86), club_S=(0.30, 0.94, -0.12), club_toe=(0.3, 0.0, -1.0),
          clav_L=(9, 22), clav_R=(9, -8), elbow_R=(-0.1, -0.2, -1.0), elbow_L=(-0.3, 0.1, -1.0),
          head_rot=(50, 4, -9), knee_L=(1.0, -0.3, 0.0), reach_w=1.0),
        K(11, pelvis_pos=(FX - 0.136, 0.0, 0.912), pelvis_rot=(22, 2, -33), chest_rot=(40, 8, -86),
          club_H=(-0.90, -0.28, 1.60), club_S=(-0.38, 0.90, 0.18), club_toe=(0.0, 0.3, -1.0),
          clav_L=(3, 20), clav_R=(3, -5), elbow_R=(-0.2, -0.2, -1.0), elbow_L=(-0.3, 0.1, -1.0),
          head_rot=(50, 5, -8), knee_L=(1.0, -0.05, 0.0), knee_R=(1.0, 0.2, 0.0), reach_w=1.0),
        K(12, pelvis_pos=(FX - 0.133, 0.03, 0.910), pelvis_rot=(21, 1, -12), chest_rot=(40, 12, -62),
          club_H=(-0.77, -0.41, 1.18), club_S=(-0.48, -0.10, 0.87), club_toe=(-0.3, -1.0, 0.2),
          clav_L=(1, 16), clav_R=(-1, 4), elbow_R=(-0.5, -0.1, -1.0), elbow_L=(-0.3, 0.2, -1.0),
          head_rot=(50, 6, -6), knee_L=(1.0, 0.25, 0.0), knee_R=(1.0, 0.45, 0.0), reach_w=1.0),
        K(13, pelvis_pos=(FX - 0.130, 0.055, 0.908), pelvis_rot=(20, 0, 12), chest_rot=(40, 16, -32),
          club_H=(-0.67, -0.24, 0.93), club_S=(0.22, -0.95, 0.20), club_toe=(0.0, 0.0, 1.0),
          clav_L=(0, 10), clav_R=(-2, 10), elbow_R=(-0.7, -0.1, -1.0), elbow_L=(-0.4, 0.4, -1.0),
          head_rot=(50, 7, -3), knee_L=(1.0, 0.35, 0.0), knee_R=(1.0, 0.6, 0.0),
          foot_R=(F, -0.215, -4.0, 6.0), reach_w=0.8),
        K(14, pelvis_pos=(FX - 0.127, 0.075, 0.912), pelvis_rot=(18, 0, 38), chest_rot=(39, 19, 10),
          club_H=Hi, club_S=Si, club_toe=(1.0, 0.0, 0.3),
          clav_L=(1, 4), clav_R=(-3, 16), elbow_R=(-0.8, -0.1, -1.0), elbow_L=(-0.5, 0.6, -1.0),
          head_rot=(50, 8, 0), knee_L=(1.0, 0.45, 0.0), knee_R=(1.0, 0.8, 0.0),
          foot_R=(F, -0.215, -2.0, 14.0)),
        K(15, pelvis_pos=(FX - 0.125, 0.09, 0.915), pelvis_rot=(16, -1, 52), chest_rot=(35, 19, 36),
          club_H=(-0.61, 0.30, 0.86), club_S=(0.42, 0.62, -0.66), club_toe=(0.5, 0.0, 0.8),
          clav_L=(2, 0), clav_R=(-3, 20), elbow_R=(-0.6, 0.0, -1.0), elbow_L=(-0.3, 0.6, -1.0),
          head_rot=(46, 7, 16), knee_L=(1.0, 0.6, 0.0), knee_R=(0.8, 1.0, 0.0),
          foot_R=(F, -0.215, 2.0, 24.0), reach_w=1.0, reach=0.99),
        K(16, pelvis_pos=(FX - 0.123, 0.11, 0.915), pelvis_rot=(13, -2, 64), chest_rot=(31, 18, 58),
          club_H=(-0.64, 0.46, 1.03), club_S=(0.18, 0.96, 0.20), club_toe=(0.0, 0.0, 1.0),
          clav_L=(4, -4), clav_R=(-1, 22), elbow_R=(-0.3, 0.2, -1.0), elbow_L=(0.0, 0.4, -1.0),
          head_rot=(36, 5, 38), knee_L=(1.0, 0.8, 0.0), knee_R=(0.6, 1.0, 0.0),
          foot_R=(F, -0.215, 8.0, 36.0), reach_w=1.0, reach=0.98),
        K(17, pelvis_pos=(FX - 0.122, 0.125, 0.915), pelvis_rot=(10, -3, 75), chest_rot=(25, 16, 78),
          club_H=(-0.71, 0.45, 1.27), club_S=(-0.38, 0.28, 0.88), club_toe=(-0.3, 0.8, 0.0),
          clav_L=(8, -6), clav_R=(2, 22), elbow_R=(-0.1, 0.3, -1.0), elbow_L=(0.2, 0.3, -1.0),
          head_rot=(26, 4, 55), foot_R=(F, -0.215, 14.0, 46.0),
          knee_L=(0.8, 1.0, 0.0), knee_R=(0.5, 1.0, 0.0), reach_w=0.4, reach=0.95),
        K(18, pelvis_pos=(FX - 0.122, 0.135, 0.915), pelvis_rot=(8, -3, 83), chest_rot=(20, 13, 92),
          club_H=(-0.84, 0.33, 1.52), club_S=(-0.62, -0.18, 0.76), club_toe=(-0.5, 0.5, 0.0),
          clav_L=(12, -6), clav_R=(5, 22), elbow_R=(0.0, 0.3, -1.0), elbow_L=(0.3, 0.1, -1.0),
          head_rot=(17, 3, 66), foot_R=(F, -0.215, 20.0, 46.0),
          knee_L=(0.6, 1.0, 0.0), knee_R=(0.4, 1.0, 0.0)),
        K(20, pelvis_pos=(FX - 0.122, 0.145, 0.915), pelvis_rot=(5, -4, 91), chest_rot=(11, 9, 108),
          club_H=(-1.03, 0.06, 1.70), club_S=(0.80, -0.30, -0.52), club_toe=(0.0, 0.0, 1.0),
          clav_L=(14, -4), clav_R=(8, 20), elbow_R=(0.2, 0.3, -1.0), elbow_L=(0.3, -0.2, -1.0),
          head_rot=(9, 1, 78), foot_R=(F, -0.215, 30.0, 50.0),
          knee_L=(0.5, 1.0, 0.0), knee_R=(0.3, 1.0, 0.0)),
        K(23, pelvis_pos=(FX - 0.122, 0.148, 0.915), pelvis_rot=(4, -4, 93), chest_rot=(9, 8, 110),
          club_H=(-1.04, 0.06, 1.69), club_S=(0.80, -0.33, -0.50), club_toe=(0.0, 0.0, 1.0),
          clav_L=(14, -4), clav_R=(8, 20), elbow_R=(0.2, 0.3, -1.0), elbow_L=(0.3, -0.2, -1.0),
          head_rot=(8, 1, 80), foot_R=(F, -0.215, 32.0, 52.0),
          knee_L=(0.5, 1.0, 0.0), knee_R=(0.3, 1.0, 0.0)),
    ]
    return ks


def idle_keys():
    # address with a small waggle: club head goes back and up a little,
    # weight rocks towards the trail foot
    return [
        K(0),
        K(1, club_H=(_H0[0] - 0.005, _H0[1] - 0.012, _H0[2] + 0.004), club_S=(0.60, -0.10, -0.79),
          pelvis_pos=(FX - 0.135, -0.004, 0.915), chest_rot=(44, 9, -2)),
        K(2, club_H=(_H0[0] - 0.01, _H0[1] - 0.025, _H0[2] + 0.01), club_S=(0.58, -0.20, -0.79),
          pelvis_pos=(FX - 0.135, -0.009, 0.915), chest_rot=(44, 9, -4), pelvis_rot=(22, 0, -1.5)),
        K(3, club_H=(_H0[0] - 0.014, _H0[1] - 0.035, _H0[2] + 0.016), club_S=(0.55, -0.29, -0.78),
          pelvis_pos=(FX - 0.135, -0.013, 0.915), chest_rot=(44, 9, -6), pelvis_rot=(22, 0, -2.5),
          club_toe=(1.0, 0.0, 0.5)),
    ]


STAND = deep_merge(REST_POSE, {
    'pelvis_pos': (FX - 0.01, 0.0, 0.935), 'pelvis_rot': (3.0, 0.0, 0.0), 'chest_rot': (1.0, 0.0, 0.0),
    'head_rot': (4.0, 0.0, 0.0), 'foot_L': (FX + 0.155, 0.15, 12.0, 0.0), 'foot_R': (FX + 0.155, -0.15, -12.0, 0.0),
    'knee_L': (1.0, 0.1, 0.0), 'knee_R': (1.0, -0.1, 0.0),
    'elbow_L': (-1.0, 0.3, -0.3), 'elbow_R': (-1.0, -0.3, -0.3), 'clav_L': (0.0, 0.0), 'clav_R': (0.0, 0.0),
})


def S_(f, **over):
    return (f, deep_merge(STAND, over))


def cheer_keys():
    # club in the left hand, its head resting on the grass; right fist pumps.
    cH, cS = club_soled((FX + 0.04, 0.27, 0.80), (FX + 0.42, 0.50, 0.0))
    base = dict(grip_L=GRIP_L, grip_R=-1.0, club_H=cH, club_S=cS,
                club_toe=(0.0, 1.0, 0.3), elbow_L=(-1.0, 0.4, -0.3),
                pelvis_rot=(2, 0, -6), chest_rot=(0, -2, -14), head_rot=(-6, 0, -22))
    fist_low = (FX + 0.20, -0.22, 1.18)
    fist_up = (FX + 0.09, -0.34, 1.90)
    fist_pull = (FX + 0.16, -0.20, 1.26)
    up = dict(base, chest_rot=(-4, -4, -18), head_rot=(-14, -3, -24))
    pull = dict(base, chest_rot=(8, 2, -16), head_rot=(4, 2, -22), pelvis_pos=(FX - 0.01, 0.0, 0.915))
    return [
        S_(0, **base, wrist_R=(FX + 0.06, -0.24, 0.90), elbow_R=(-1.0, -0.4, -0.3)),
        S_(2, **base, wrist_R=fist_low, elbow_R=(-0.4, -1.0, -0.6), handY_R=(0.3, 0.0, 1.0)),
        S_(3, **up, wrist_R=fist_up, elbow_R=(0.0, -1.0, -0.3), handY_R=(0.0, 0.0, 1.0)),
        S_(4, **up, wrist_R=(FX + 0.10, -0.35, 1.86), elbow_R=(0.0, -1.0, -0.3), handY_R=(0.0, 0.0, 1.0)),
        S_(5, **pull, wrist_R=fist_pull, elbow_R=(-0.5, -0.6, -1.0), handY_R=(0.2, 0.0, 1.0)),
        S_(6, **up, wrist_R=fist_up, elbow_R=(0.0, -1.0, -0.3), handY_R=(0.0, 0.0, 1.0)),
        S_(7, **pull, wrist_R=fist_pull, elbow_R=(-0.5, -0.6, -1.0), handY_R=(0.2, 0.0, 1.0)),
    ]


def sad_keys():
    # leaning on the club (left hand, head on the grass), right hand on the
    # hip (the side the camera sees); the head drops.
    cH, cS = club_soled((FX + 0.20, 0.17, 0.86), (FX + 0.42, 0.24, 0.0))
    base = dict(grip_R=-1.0, grip_L=GRIP_L, club_H=cH, club_S=cS, club_toe=(1.0, 0.0, 0.0),
                elbow_L=(-1.0, 0.5, -0.4), wrist_R=(FX - 0.05, -0.245, 0.99), elbow_R=(-0.6, -1.0, 0.0),
                handY_R=(0.2, 0.6, -0.6), pelvis_rot=(2, 0, -6), chest_rot=(4, 0, -10))
    return [
        S_(0, **base, head_rot=(8, 0, -10)),
        S_(1, **dict(base, chest_rot=(9, 0, -10)), head_rot=(22, 0, -10), clav_L=(-3, 3), clav_R=(-3, 3)),
        S_(2, **dict(base, chest_rot=(14, 0, -10)), head_rot=(38, 2, -10), clav_L=(-5, 5), clav_R=(-5, 5)),
        S_(3, **dict(base, chest_rot=(17, 0, -10)), head_rot=(48, 3, -12), clav_L=(-6, 6), clav_R=(-6, 6)),
        S_(4, **dict(base, chest_rot=(17, 1, -12)), head_rot=(50, 6, -20), clav_L=(-6, 6), clav_R=(-6, 6)),
        S_(5, **dict(base, chest_rot=(17, -1, -8)), head_rot=(50, -2, -2), clav_L=(-6, 6), clav_R=(-6, 6)),
    ]


def turn_pose():
    # relaxed, the club head resting on the grass by the right foot, the
    # right hand on the grip, the left arm hanging.
    cH, cS = club_soled((FX + 0.08, -0.29, 0.86), (FX + 0.36, -0.44, 0.0))
    return deep_merge(STAND, dict(
        grip_L=-1.0, grip_R=GRIP_L, club_H=cH, club_S=cS,
        club_toe=(1.0, 0.0, 0.0), elbow_R=(-1.0, -0.6, -0.2),
        wrist_L=(FX - 0.02, 0.24, 0.90), elbow_L=(-1.0, 0.3, -0.2), handY_L=(0.2, 0.1, -1.0),
        pelvis_rot=(2, 1, 0), chest_rot=(1, -1, 0), head_rot=(3, 0, 0)))


def pivot_G(yaw_deg, centre=V(FX, 0, 0), dest=None):
    dest = centre if dest is None else dest
    return Matrix.Translation(dest) @ R3('Z', yaw_deg).to_4x4() @ Matrix.Translation(-centre)


def sequences():
    seq = {}
    seq['swing'] = [interp(swing_keys(), f) for f in range(24)]
    ik = idle_keys()
    seq['idle'] = [interp(ik, f) for f in range(4)]
    ck = cheer_keys()
    Gc = pivot_G(-22)
    seq['cheer'] = [dict(interp(ck, f), G=Gc) for f in range(8)]
    sk = sad_keys()
    Gs = pivot_G(-30)
    seq['sad'] = [dict(interp(sk, f), G=Gs) for f in range(6)]
    tp = turn_pose()
    seq['turn'] = [dict(tp, G=pivot_G(-90 + 45 * k, dest=V(0, 0, 0))) for k in range(8)]
    return seq


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


def setup_scene():
    sc = bpy.context.scene
    r = sc.render
    r.resolution_x, r.resolution_y, r.resolution_percentage = IMG_W, IMG_H, 100
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
    # world: soft sky
    w = bpy.data.worlds.new('sky')
    sc.world = w
    w.use_nodes = True
    bg = w.node_tree.nodes['Background']
    bg.inputs['Color'].default_value = (0.80, 0.87, 1.0, 1)
    bg.inputs['Strength'].default_value = 0.32
    # sun from behind-left of the camera, 50 degrees up
    sun = bpy.data.objects.new('sun', bpy.data.lights.new('sun', 'SUN'))
    sc.collection.objects.link(sun)
    el, az = math.radians(50), math.radians(225)   # from (-X, -Y)
    frm = V(math.cos(el) * math.cos(az), math.cos(el) * math.sin(az), math.sin(el))
    sun.rotation_euler = (-frm).to_track_quat('-Z', 'Y').to_euler()
    sun.data.energy = 2.0
    sun.data.angle = math.radians(4.0)
    sun.data.color = (1.0, 0.96, 0.90)
    # swing camera (shared with the watch's renderer: do not change)
    cam = bpy.data.objects.new('cam_swing', bpy.data.cameras.new('cam_swing'))
    sc.collection.objects.link(cam)
    cam.location = (-0.16, -4.80, 2.82)
    cam.rotation_euler = (math.radians(79), 0, 0)
    cam.data.sensor_fit = 'VERTICAL'
    cam.data.sensor_height = 24.0
    cam.data.lens = 12.0 / math.tan(math.radians(25))
    cam.data.clip_start = 0.1
    # showcase camera for the shop turntable
    cam2 = bpy.data.objects.new('cam_turn', bpy.data.cameras.new('cam_turn'))
    sc.collection.objects.link(cam2)
    tgt = V(0, 0, 0.99)
    pos = V(0, -6.0, 1.42)
    cam2.location = pos
    cam2.rotation_euler = (tgt - pos).to_track_quat('-Z', 'Y').to_euler()
    cam2.data.sensor_fit = 'VERTICAL'
    cam2.data.sensor_height = 24.0
    cam2.data.lens = 12.0 / math.tan(math.radians(10.4))
    # ground: shadow catcher / bounce
    me = bpy.data.meshes.new('ground')
    s = 12
    me.from_pydata([(-s, -s, 0), (s, -s, 0), (s, s, 0), (-s, s, 0)], [], [(0, 1, 2, 3)])
    g = bpy.data.objects.new('ground', me)
    sc.collection.objects.link(g)
    me.materials.append(MATS['ground'])
    # debug marker on the ball
    mb = MB()
    ellipsoid(mb, V(0, 0, 0.021), (0.021, 0.021, 0.021), 'skin')
    mk = mb_to_object('marker', mb, subsurf=1)
    mk.data.materials[0] = MATS['marker']
    mk['matkeys'] = []
    mk.hide_render = True
    return cam, cam2, g, mk


# ---------------------------------------------------------------------------
# Output helpers
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
    # fill: every pixel the shade covers gets the nearest region id
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
    return ids


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main():
    argv = sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else []
    ap = argparse.ArgumentParser()
    ap.add_argument('--out', default=os.path.join(HERE, '..', '..', 'assets', 'render'))
    ap.add_argument('--seq', default='swing,idle,cheer,sad,turn')
    ap.add_argument('--frames', default='')
    ap.add_argument('--samples', type=int, default=64)
    ap.add_argument('--hats', default='0,1,2,3,4,5')
    ap.add_argument('--nohats', action='store_true')
    ap.add_argument('--noshadow', action='store_true')
    ap.add_argument('--debug', default='')
    ap.add_argument('--cpu', action='store_true')
    ap.add_argument('--info', action='store_true', help='print rig diagnostics only')
    args = ap.parse_args(argv)
    out = os.path.abspath(args.out)
    os.makedirs(out, exist_ok=True)
    tmp = os.path.join(out, '_tmp')
    os.makedirs(tmp, exist_ok=True)
    hats_on = [] if args.nohats or not args.hats else [int(x) for x in args.hats.split(',')]
    frames_sel = [int(x) for x in args.frames.split(',')] if args.frames else None

    bpy.ops.wm.read_factory_settings(use_empty=True)
    setup_prefs(args.cpu)
    make_materials()
    cam, cam2, ground, marker = setup_scene()
    sc = bpy.context.scene

    # --- build the character ---
    rest = evaluate(REST_POSE, rest=True)
    body_mb = build_body(rest)
    skin = Skin(body_mb, rest)
    body = mb_to_object('body', body_mb, subsurf=2)
    head = mb_to_object('head', build_head(), subsurf=2)
    bpy.context.view_layer.update()
    hair = mb_to_object('hair', build_hair(head), subsurf=1)
    shoes = {sd: mb_to_object('shoe_' + sd, build_shoe(s), subsurf=2) for s, sd in ((1, 'L'), (-1, 'R'))}
    hands = {sd: mb_to_object('hand_' + sd, build_hand(s), subsurf=2) for s, sd in ((1, 'L'), (-1, 'R'))}
    club = mb_to_object('club', build_club(), subsurf=1)
    hats = [mb_to_object('hat%d' % k, mb, subsurf=2) for k, mb in enumerate(build_hats())]
    head_rest_inv = rest['M']['head'].inverted()
    golfer = [body, head, hair, club] + list(shoes.values()) + list(hands.values())
    for h in hats:
        h.hide_render = True
    print('body verts', len(body_mb.v))

    seqs = sequences()
    want = args.seq.split(',')
    meta_path = os.path.join(out, 'meta.json')
    meta = json.load(open(meta_path)) if os.path.exists(meta_path) else {}
    meta.update({
        'note': 'Rendered by tools/blender/golfer.py. colour = palette[id] * shade; '
                'shade RGB is lit neutral grey (0.8 albedo), sRGB; shade_ref is the value '
                'of a sunlit surface facing the light at ~45 degrees.',
        'shade_ref': 200,
        'ids': {str(v): k for k, v in [('empty', 0), ('skin', 1), ('hair', 2), ('shirt A', 3), ('shirt B', 4),
                                         ('trousers A', 5), ('trousers B', 6), ('shoes A', 7), ('shoes B', 8),
                                         ('belt', 9), ('glove', 10), ('club head / buckle', 11), ('shaft', 12),
                                         ('grip', 13), ('hat A', 14), ('hat B', 15)]},
        'hats': {str(k): n for k, n in enumerate(HAT_NAMES)},
        'camera_swing': {'position': [-0.16, -4.80, 2.82], 'rotation_deg': [79, 0, 0], 'fov_v_deg': 50,
                         'size': [IMG_W, IMG_H]},
    })
    meta.setdefault('sequences', {})

    def project(camobj, p, w, h):
        co = world_to_camera_view(sc, camobj, p)
        return [round(co.x * w, 1), round((1 - co.y) * h, 1)]

    for name in want:
        frames = seqs[name]
        is_turn = name == 'turn'
        camobj = cam2 if is_turn else cam
        sc.camera = camobj
        w, h = (TURN_W, TURN_H) if is_turn else (IMG_W, IMG_H)
        sc.render.resolution_x, sc.render.resolution_y = w, h
        m = meta['sequences'].get(name, {})
        m.update({'frames': len(frames), 'size': [w, h]})
        if name == 'idle':
            m['loop'] = [0, 1, 2, 3, 2, 1]
        if name == 'swing':
            m['impact_frame'] = 14
            m['top_frame'] = 10
            m['ball_px'] = project(cam, V(0, 0, 0.021), w, h)
        if is_turn:
            m['feet_baseline_y'] = project(cam2, V(0, 0, 0), w, h)[1]
            m['feet_centre_px'] = project(cam2, V(0, 0, 0), w, h)
            m['yaw_deg'] = [-90 + 45 * k for k in range(8)]
        heads = m.get('club_head_px', [None] * len(frames))
        if len(heads) != len(frames):
            heads = [None] * len(frames)
        for fi, pose in enumerate(frames):
            if frames_sel is not None and fi not in frames_sel:
                continue
            t0 = time.time()
            E = evaluate(pose)
            inf = E['info']
            print('%s %02d  extL %.3f extR %.3f  missL %.3f missR %.3f  legs %.3f %.3f (miss %.3f %.3f)  head %s' % (
                name, fi, inf['ext_L'], inf['ext_R'], inf['miss_L'], inf['miss_R'], inf['legext_L'],
                inf['legext_R'], inf['legmiss_L'], inf['legmiss_R'],
                tuple(round(x, 3) for x in inf['club_head'])))
            heads[fi] = project(camobj, inf['club_head'], w, h)
            if args.info:
                continue
            # pose the objects
            co = skin.deform(E['M'])
            body.data.vertices.foreach_set('co', co.astype(np.float32).ravel())
            body.data.update()
            Mh = E['rigid']['head'] @ Matrix.Scale(HEAD_SCALE, 4) @ head_rest_inv
            head.matrix_world = Mh
            hair.matrix_world = Mh
            for hh in hats:
                hh.matrix_world = Mh
            for sd in ('L', 'R'):
                shoes[sd].matrix_world = E['rigid']['foot_' + sd]
                hands[sd].matrix_world = E['rigid']['hand_' + sd]
            club.matrix_world = E['rigid']['club']
            bpy.context.view_layer.update()
            base = os.path.join(out, '%s_%02d' % (name, fi))
            shade_a = render_passes(sc, golfer, ground, base, tmp, args, hats, hats_on, name, fi, out)
            if args.debug:
                debug_marker(sc, golfer, marker, ground, args.debug, name, fi, tmp)
            print('   rendered in %.1fs' % (time.time() - t0))
        if name == 'swing':
            m['club_head_px'] = heads
        meta['sequences'][name] = m
    with open(meta_path, 'w') as f:
        json.dump(meta, f, indent=1)
    import shutil
    shutil.rmtree(tmp, ignore_errors=True)
    print('done')


def set_vis(objs, camera=True, holdout=False, hide=False):
    for o in objs:
        o.hide_render = hide
        o.visible_camera = camera
        o.is_holdout = holdout


def render_exr(sc, path, samples, filt, denoise, bounces):
    sc.cycles.samples = samples
    sc.cycles.use_denoising = denoise
    if denoise:
        sc.cycles.denoiser = 'OPENIMAGEDENOISE'
    # id passes use ONE sample per pixel: Cycles does not honour a tiny box
    # filter, so a single camera ray is the only way to get exact regions
    sc.cycles.pixel_filter_type = 'BLACKMAN_HARRIS' if filt > 0.5 else 'GAUSSIAN'
    sc.cycles.filter_width = filt
    sc.cycles.max_bounces = bounces
    sc.cycles.diffuse_bounces = min(bounces, 3)
    sc.cycles.glossy_bounces = min(bounces, 2)
    sc.cycles.transmission_bounces = 0
    sc.cycles.transparent_max_bounces = 2
    sc.render.filepath = path
    bpy.ops.render.render(write_still=True)
    return read_exr(path)


def render_passes(sc, golfer, ground, base, tmp, args, hats, hats_on, name, fi, out):
    # shade: golfer visible, ground only bounces light
    set_vis(golfer)
    ground.hide_render = False
    ground.visible_camera = False
    ground.is_shadow_catcher = False
    set_pass_materials(golfer, 'shade')
    a = render_exr(sc, os.path.join(tmp, 'shade.exr'), args.samples, 1.5, True, 4)
    shade = to_shade(a)
    write_png(base + '_shade.png', shade)
    # id: emission only, no filtering
    ground.hide_render = True
    set_pass_materials(golfer, 'id')
    a = render_exr(sc, os.path.join(tmp, 'id.exr'), 1, 0.01, False, 0)
    ids = to_id(a, shade[..., 3])
    write_png(base + '_id.png', (ids * 16).astype(np.uint8))
    # shadow: golfer invisible to the camera but casting, ground catches
    if not args.noshadow:
        set_pass_materials(golfer, 'shade')
        set_vis(golfer, camera=False)
        ground.hide_render = False
        ground.visible_camera = True
        ground.is_shadow_catcher = True
        a = render_exr(sc, os.path.join(tmp, 'shadow.exr'), max(64, args.samples), 1.5, True, 3)
        sh = np.round(np.clip(a[..., 3], 0, 1) * 255).astype(np.uint8)
        write_png(base + '_shadow.png', sh)
        ground.is_shadow_catcher = False
    # hats: the golfer as hold-out
    for k in hats_on:
        hat = hats[k]
        set_vis(golfer, holdout=True)
        set_vis([hat])
        ground.hide_render = False
        ground.visible_camera = False
        set_pass_materials([hat], 'shade')
        set_pass_materials(golfer, 'shade')
        a = render_exr(sc, os.path.join(tmp, 'hat.exr'), max(32, args.samples // 2), 1.5, True, 4)
        hs = to_shade(a)
        write_png(os.path.join(out, 'hat%d_%s_%02d_shade.png' % (k, name, fi)), hs)
        ground.hide_render = True
        set_pass_materials([hat], 'id')
        a = render_exr(sc, os.path.join(tmp, 'hatid.exr'), 1, 0.01, False, 0)
        hid = to_id(a, hs[..., 3])
        write_png(os.path.join(out, 'hat%d_%s_%02d_id.png' % (k, name, fi)), (hid * 16).astype(np.uint8))
        hat.hide_render = True
        set_vis(golfer)
    return shade


def debug_marker(sc, golfer, marker, ground, ddir, name, fi, tmp):
    os.makedirs(ddir, exist_ok=True)
    set_vis(golfer)
    ground.hide_render = True
    marker.hide_render = False
    set_pass_materials(golfer, 'id')
    a = render_exr(sc, os.path.join(tmp, 'dbg.exr'), 1, 0.01, False, 0)
    marker.hide_render = True
    al = a[..., 3]
    rgb = a[..., :3] / np.maximum(al[..., None], 1e-6)
    img = np.zeros(a.shape[:2] + (3,), np.uint8)
    img[..., 1] = np.where(al > 0.5, 60 + rgb[..., 0] * 180, 0)
    mk = (al > 0.5) & (rgb[..., 1] < 0.1) & (rgb[..., 0] > 0.9)
    img[mk] = (255, 0, 255)
    ys, xs = np.nonzero(mk)
    if len(xs):
        print('   ball marker at x %.1f y %.1f' % (xs.mean(), ys.mean()))
    gy, gx = np.nonzero(al > 0.5)
    if len(gy):
        print('   golfer bbox x %d..%d y %d..%d' % (gx.min(), gx.max(), gy.min(), gy.max()))
    write_png(os.path.join(ddir, 'debug_%s_%02d.png' % (name, fi)), img)


if __name__ == '__main__':
    main()
