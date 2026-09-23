"""Mila's poses: one function per animation, pose(frame, frames) -> dict.

Coordinates in cat space (sample units, see mila_rig.py): facing -Y, her
left is +X, ground at z = 0. The standing numbers are sample.py's mila().
"""
import copy
import math

from mathutils import Matrix, Vector as V

from mila_rig import E, MS

TAU = 2 * math.pi

TAIL_IDLE = [(0, 0.26, 0.22), (0.0, 0.34, 0.30), (0.05, 0.40, 0.45), (0.06, 0.37, 0.58), (0.0, 0.31, 0.62)]
TAIL_WALK = [(0, 0.26, 0.22), (0.0, 0.36, 0.28), (-0.04, 0.46, 0.36), (-0.02, 0.52, 0.48), (0.03, 0.50, 0.55)]
TAIL_PUSH = [(0, 0.30, 0.24), (0.0, 0.38, 0.34), (0.0, 0.42, 0.48), (0.02, 0.42, 0.60), (0.06, 0.38, 0.66)]
TAIL_UP = [(0, 0.26, 0.23), (0.0, 0.31, 0.36), (0.0, 0.32, 0.50), (0.02, 0.29, 0.63), (0.07, 0.25, 0.69)]
TAIL_SIT_UP = [(0, 0.20, 0.10), (0, 0.28, 0.22), (0, 0.30, 0.38), (0.02, 0.28, 0.52), (0.07, 0.24, 0.58)]
TAIL_SIT = [(-0.02, 0.22, 0.05), (-0.14, 0.18, 0.04), (-0.20, 0.05, 0.04), (-0.17, -0.08, 0.04), (-0.08, -0.14, 0.04)]


def vv(pts):
    return [V(p) for p in pts]


def stand():
    return dict(
        chest=V((0, -0.05, 0.25)), rump=V((0, 0.18, 0.205)), arch=0.0, breath=0.0,
        head=V((0, -0.17, 0.45)), head_rot=(0, 0, 0),
        legs={'fl': [V((0.07, -0.07, 0.20)), V((0.07, -0.10, 0.03))],
              'fr': [V((-0.07, -0.07, 0.20)), V((-0.07, -0.10, 0.03))],
              'bl': [V((0.08, 0.18, 0.20)), V((0.08, 0.18, 0.03))],
              'br': [V((-0.08, 0.18, 0.20)), V((-0.08, 0.18, 0.03))]},
        leg_r={'f': 0.042, 'b': 0.046},
        thighs=[(V((0.075, 0.16, 0.17)), (0.06, 0.085, 0.09), None),
                (V((-0.075, 0.16, 0.17)), (0.06, 0.085, 0.09), None)],
        tail=vv(TAIL_IDLE), eyes='open', mouth=None)


def sit():
    """sample.py's sitting pose."""
    return dict(
        chest=V((0, -0.045, 0.29)), rump=V((0, 0.10, 0.15)), arch=0.0, breath=0.0,
        rump_s=(0.135, 0.12, 0.12),
        head=V((0, -0.16, 0.49)), head_rot=(0, 0, 0),
        legs={'fl': [V((0.06, -0.06, 0.23)), V((0.06, -0.10, 0.03))],
              'fr': [V((-0.06, -0.06, 0.23)), V((-0.06, -0.10, 0.03))],
              'bl': [V((0.12, 0.05, 0.06)), V((0.12, -0.03, 0.03))],
              'br': [V((-0.12, 0.05, 0.06)), V((-0.12, -0.03, 0.03))]},
        leg_r={'f': 0.040, 'b': 0.040},
        thighs=[(V((0.115, 0.08, 0.10)), (0.07, 0.11, 0.085), None),
                (V((-0.115, 0.08, 0.10)), (0.07, 0.11, 0.085), None)],
        tail=vv(TAIL_SIT), eyes='open', mouth=None)


def shift(P, d):
    d = V(d)
    for k in ('chest', 'rump', 'head'):
        P[k] = P[k] + d
    for k in P['legs']:
        P['legs'][k] = [p + d for p in P['legs'][k]]
    P['thighs'] = [(c + d, s, r) for (c, s, r) in P.get('thighs', [])]
    P['tail'] = [p + d for p in P['tail']]
    if P.get('spine'):
        P['spine'] = [(p + d, r) for (p, r) in P['spine']]
    return P


def lift_body(P, dz, legs=True):
    """Raise the body (not the paws) by dz; leg roots follow."""
    d = V((0, 0, dz))
    for k in ('chest', 'rump', 'head'):
        P[k] = P[k] + d
    for k in P['legs']:
        P['legs'][k][0] = P['legs'][k][0] + d
    P['thighs'] = [(c + d, s, r) for (c, s, r) in P.get('thighs', [])]
    P['tail'] = [p + d for p in P['tail']]
    return P


def sway_tail(pts, deg, lift=0.0):
    """Bend the tail sideways (about the vertical through its base) by an
    angle growing along it, and lift it (about X) the same way."""
    b = pts[0]
    n = len(pts) - 1
    out = [b]
    for i, p in enumerate(pts[1:], 1):
        k = i / n
        R = Matrix.Rotation(math.radians(deg) * k, 3, 'Z') @ Matrix.Rotation(math.radians(-lift) * k, 3, 'X')
        out.append(b + R @ (p - b))
    return out


def nose_of(P):
    return V(P['head']) + E(P['head_rot']).to_matrix() @ V((0, -0.205, -0.045))


# ---------------------------------------------------------------------------
# game scale
# ---------------------------------------------------------------------------

def idle(f, n=4, d='s'):
    t = f / n
    P = stand()
    s = math.sin(TAU * t)
    P['breath'] = 0.018 * s
    P['chest'] = P['chest'] + V((0, 0, 0.005 * s))
    P['head'] = P['head'] + V((0, 0, 0.006 * math.sin(TAU * t - 0.5)))
    P['head_rot'] = (0, 3 * math.sin(TAU * t - 0.8), 0)
    P['tail'] = sway_tail(P['tail'], 16 * math.sin(TAU * t))
    if f == 2:
        P['eyes'] = 'closed'
    return P


def _trot_paw(y0, p, A, Hh, x0, z0=0.03):
    """A paw's place at phase p: stance (on the ground, moving back) in the
    first half, swing (lifted, moving forward) in the second."""
    if p < 0.5:
        s = p / 0.5
        return V((x0, y0 - A + 2 * A * s, z0))
    s = (p - 0.5) / 0.5
    return V((x0, y0 + A * math.cos(math.pi * s), z0 + Hh * math.sin(math.pi * s)))


def walk(f, n=6, d='s'):
    """A trot: diagonal pairs (fl+br, fr+bl), one cycle per cell."""
    t = f / n
    P = stand()
    off = {'fl': 0.0, 'br': 0.0, 'fr': 0.5, 'bl': 0.5}
    for k, (root, paw) in P['legs'].items():
        p = (t + off[k]) % 1.0
        front = k[0] == 'f'
        P['legs'][k][1] = _trot_paw(paw.y, p, 0.10 if front else 0.09, 0.09 if front else 0.075, paw.x)
    bob = 0.012 * math.cos(2 * TAU * t)
    lift_body(P, bob)
    P['head'] = P['head'] + V((0, -0.01, 0.006 * math.cos(2 * TAU * t + 0.6)))
    P['head_rot'] = (0, 0, 4 * math.sin(TAU * t))
    P['arch'] = 0.01
    P['tail'] = sway_tail(vv(TAIL_WALK), 12 * math.sin(TAU * t + 1.0), 4 * math.cos(2 * TAU * t))
    return P


PUSH_NOSE = -0.50 / MS    # the pushed thing's near face is 0.15 m in front of her nose (0.65 m)


def push_base():
    P = stand()
    P['chest'] = V((0, -0.10, 0.20))
    P['rump'] = V((0, 0.19, 0.25))
    P['head'] = V((0, -0.285, 0.325))
    P['head_rot'] = (16, 0, 0)
    P['legs'] = {'fl': [V((0.075, -0.12, 0.18)), V((0.08, -0.25, 0.03))],
                 'fr': [V((-0.075, -0.12, 0.18)), V((-0.08, -0.22, 0.03))],
                 'bl': [V((0.08, 0.20, 0.24)), V((0.08, 0.35, 0.03))],
                 'br': [V((-0.08, 0.20, 0.24)), V((-0.08, 0.32, 0.03))]}
    P['thighs'] = [(V((0.078, 0.20, 0.22)), (0.06, 0.09, 0.09), (35, 0, 0)),
                   (V((-0.078, 0.20, 0.22)), (0.06, 0.09, 0.09), (35, 0, 0))]
    P['tail'] = vv(TAIL_PUSH)
    P['ears'] = (-6, 6)
    return P


def push(f, n=6, d='s'):
    """Head-butting the thing in front, back legs driving (a loop)."""
    t = f / n
    P = push_base()
    dy = PUSH_NOSE - nose_of(P).y
    shift(P, (0, dy, 0))
    s = math.sin(TAU * t)
    P['head'] = P['head'] + V((0, -0.012 * s, 0.004 * s))
    P['chest'] = P['chest'] + V((0, -0.008 * s, 0))
    for k, ph in (('bl', 0.0), ('br', 0.5)):
        root, paw = P['legs'][k]
        p = (t + ph) % 1.0
        if p < 0.6:      # driving back on the ground
            P['legs'][k][1] = paw + V((0, -0.03 + 0.08 * (p / 0.6), 0))
        else:            # a quick step forward
            q = (p - 0.6) / 0.4
            P['legs'][k][1] = paw + V((0, 0.05 - 0.08 * q, 0.05 * math.sin(math.pi * q)))
    P['tail'] = sway_tail(P['tail'], 10 * math.sin(TAU * t))
    return P


def win(f, n=8, d='s'):
    """A happy hop, lands, sits, tail up, eyes closed smiling."""
    P = stand()
    P['tail'] = vv(TAIL_UP)
    P['eyes'] = 'happy'
    P['mouth'] = 'w'
    if f == 0:          # crouch
        lift_body(P, -0.04)
        P['head_rot'] = (8, 0, 0)
    elif f in (1, 2, 3):
        h = {1: 0.10, 2: 0.17, 3: 0.09}[f]
        lift_body(P, 0.02)
        for k in P['legs']:
            root, paw = P['legs'][k]
            tuck = 0.05 if f == 2 else 0.02
            P['legs'][k][1] = paw + V((0, 0.02 if k[0] == 'f' else -0.02, tuck + (0.02 if k[0] == 'f' else 0)))
        shift(P, (0, 0, h))
        P['head_rot'] = (-10, 0, 0)
    elif f == 4:        # landing squash
        lift_body(P, -0.045)
        P['legs']['fl'][1] = V((0.10, -0.12, 0.03))
        P['legs']['fr'][1] = V((-0.10, -0.12, 0.03))
        P['legs']['bl'][1] = V((0.11, 0.19, 0.03))
        P['legs']['br'][1] = V((-0.11, 0.19, 0.03))
        P['breath'] = 0.03
        P['head_rot'] = (4, 0, 0)
    else:
        P = sit()
        P['tail'] = sway_tail(vv(TAIL_SIT_UP), {5: 0, 6: 8, 7: -8}[f])
        P['eyes'] = 'happy'
        P['mouth'] = 'w'
        P['head_rot'] = (-6, {5: 0, 6: 6, 7: -6}[f], 0)
        if f == 5:
            lift_body(P, 0.02)
    return P


# ---------------------------------------------------------------------------
# casita scale
# ---------------------------------------------------------------------------

def c_sit(f, n=4, d='s'):
    t = f / n
    P = sit()
    s = math.sin(TAU * t)
    P['breath'] = 0.015 * s
    tail = vv(TAIL_SIT)
    # the tip swishes: lifts off the floor and sweeps
    k = 0.5 + 0.5 * s
    tail[-1] = tail[-1] + V((0.03 * s, -0.02 * k, 0.06 * k))
    tail[-2] = tail[-2] + V((0.01 * s, 0, 0.02 * k))
    P['tail'] = tail
    P['head_rot'] = (0, 4 * math.sin(TAU * t - 1.0), 0)
    if f == 2:
        P['eyes'] = 'closed'
    return P


def c_meow(f, n=4, d='s'):
    P = sit()
    o = [0.35, 1.0, 0.65, 0.15][f]
    P['mouth'] = 'open'
    P['mouth_open'] = o
    P['head_rot'] = (-14 * o, 0, 3 * o)
    P['head'] = P['head'] + V((0, -0.01 * o, 0.012 * o))
    P['ears'] = (-4 * o, 6 * o)
    P['chest'] = P['chest'] + V((0, -0.01 * o, 0.01 * o))
    return P


def c_sleep(f, n=4, d='s'):
    """Curled up in a ring (fits a circle of radius 0.34 m), head on her
    paws, tail wrapped around the front, breathing."""
    t = f / n
    b = 0.02 * math.sin(TAU * t)
    cx, cy, R = 0.02, 0.05, 0.13

    def arc(a, r, z):
        a = math.radians(a)
        return V((cx + r * math.cos(a), cy + r * math.sin(a), z))
    sp = [(arc(215, R, 0.12), 0.115), (arc(150, R, 0.12), 0.125), (arc(90, R, 0.125), 0.13),
          (arc(30, R, 0.12), 0.125), (arc(-20, R, 0.115), 0.12)]
    P = dict(
        spine=sp, chest=sp[0][0], rump=sp[-1][0], rump_s=(0.12, 0.12, 0.11), chest_s=(0.11, 0.11, 0.11),
        breath=b,
        head=V((-0.05, -0.175, 0.195 + 0.004 * math.sin(TAU * t))), head_rot=(-12, -8, 12),
        legs={'fl': [V((-0.04, -0.08, 0.10)), V((0.03, -0.24, 0.035))],
              'fr': [V((-0.13, -0.05, 0.10)), V((-0.13, -0.23, 0.035))],
              'bl': [V((0.16, 0.02, 0.10)), V((0.20, -0.12, 0.035))],
              'br': [V((0.10, 0.02, 0.07)), V((0.12, -0.14, 0.03))]},
        leg_r={'f': 0.040, 'b': 0.044},
        thighs=[(V((0.15, 0.03, 0.12)), (0.075, 0.10, 0.08), (0, 0, 30))],
        tail=[arc(-25, 0.19, 0.09), arc(-55, 0.27, 0.05), arc(-85, 0.30, 0.045), arc(-120, 0.29, 0.045),
              arc(-150, 0.26, 0.045)],
        tail_r=[0.040, 0.038, 0.034, 0.030, 0.026],
        eyes='closed', mouth=None, ears=(6, 4), neck_t=0.4)
    return P


def c_belly(f, n=6, d='s'):
    """Rolled on her back (head to the left, looking at you), paws in the
    air wiggling."""
    t = f / n
    s = math.sin(TAU * t)
    c = math.cos(TAU * t)
    P = dict(
        chest=V((-0.10, 0.0, 0.135)), rump=V((0.17, 0.02, 0.13)), body_up=(0, 0.2, -1), arch=-0.01,
        breath=0.01 * s, rump_s=(0.12, 0.115, 0.11),
        head=V((-0.32, -0.03, 0.175)),
        head_axes=((0.05 * s, -0.62, 0.78), (-0.95, 0.0, 0.25 + 0.08 * c)),
        legs={'fl': [V((-0.10, -0.07, 0.19)), V((-0.16, -0.10, 0.37)) + V((0.02 * s, 0, 0.02 * c))],
              'fr': [V((-0.10, 0.07, 0.19)), V((-0.04, 0.05, 0.39)) + V((-0.02 * c, 0, 0.02 * s))],
              'bl': [V((0.17, -0.08, 0.17)), V((0.23, -0.13, 0.33)) + V((0.02 * c, 0, -0.02 * s))],
              'br': [V((0.17, 0.08, 0.17)), V((0.28, 0.05, 0.31)) + V((0.02 * s, 0, 0.02 * c))]},
        paw_rot={'fl': (90, 0, 0), 'fr': (90, 0, 0), 'bl': (90, 0, 0), 'br': (90, 0, 0)},
        leg_r={'f': 0.040, 'b': 0.044},
        thighs=[(V((0.15, -0.08, 0.15)), (0.07, 0.07, 0.09), None), (V((0.15, 0.08, 0.15)), (0.07, 0.07, 0.09), None)],
        tail=[V((0.26, 0.02, 0.10)), V((0.36, 0.0, 0.05)), V((0.46, -0.05 + 0.03 * s, 0.04)),
              V((0.52, -0.14 + 0.03 * s, 0.04)), V((0.48, -0.22 + 0.02 * s, 0.05))],
        eyes='open', mouth=None, neck_t=0.45, hang=(1, 0, -0.15))
    return P


# ---------------------------------------------------------------------------
# blending and the rest of the poses
# ---------------------------------------------------------------------------

def _lerp3(a, b, t):
    a = (0, 0, 0) if a is None else a
    b = (0, 0, 0) if b is None else b
    return tuple(x + (y - x) * t for x, y in zip(a, b))


def blend(A, B, t):
    """A pose between A and B (same leg keys, thighs and tail length)."""
    P = copy.deepcopy(A if t < 0.5 else B)
    for k in ('chest', 'rump', 'head'):
        P[k] = V(A[k]).lerp(V(B[k]), t)
    P['head_rot'] = _lerp3(A.get('head_rot'), B.get('head_rot'), t)
    for k in P['legs']:
        P['legs'][k] = [V(A['legs'][k][i]).lerp(V(B['legs'][k][i]), t) for i in (0, 1)]
    P['leg_r'] = {k: A['leg_r'][k] * (1 - t) + B['leg_r'][k] * t for k in 'fb'}
    P['thighs'] = [(V(a[0]).lerp(V(b[0]), t), _lerp3(a[1], b[1], t), _lerp3(a[2], b[2], t))
                   for a, b in zip(A['thighs'], B['thighs'])]
    P['tail'] = [V(a).lerp(V(b), t) for a, b in zip(A['tail'], B['tail'])]
    for k, dv in (('rump_s', (0.12, 0.11, 0.115)), ('chest_s', (0.11, 0.11, 0.12))):
        P[k] = _lerp3(A.get(k, dv), B.get(k, dv), t)
    for k in ('breath', 'arch'):
        P[k] = A.get(k, 0.0) * (1 - t) + B.get(k, 0.0) * t
    return P


def near_paw(d):
    """The front paw on the camera's side: her right (-X) facing east, her
    left (+X) facing west (the yaw turns her 62 deg towards the camera)."""
    return ('fr', -1) if d in ('e', 's') else ('fl', 1)


def stretch():
    """The play-bow stretch: front paws forward, chest down, rump up."""
    P = stand()
    P['chest'] = V((0, -0.14, 0.14))
    P['rump'] = V((0, 0.17, 0.26))
    P['head'] = V((0, -0.30, 0.27))
    P['head_rot'] = (-12, 0, 0)
    P['legs'] = {'fl': [V((0.07, -0.15, 0.12)), V((0.07, -0.36, 0.03))],
                 'fr': [V((-0.07, -0.15, 0.12)), V((-0.07, -0.36, 0.03))],
                 'bl': [V((0.08, 0.17, 0.25)), V((0.08, 0.21, 0.03))],
                 'br': [V((-0.08, 0.17, 0.25)), V((-0.08, 0.21, 0.03))]}
    P['thighs'] = [(V((0.075, 0.17, 0.22)), (0.06, 0.085, 0.09), None),
                   (V((-0.075, 0.17, 0.22)), (0.06, 0.085, 0.09), None)]
    P['tail'] = vv(TAIL_UP)
    P['tail'] = [p + V((0, 0, 0.03)) for p in P['tail']]
    return P


def yawn(f, n=8, d='s'):
    """Idle for a while: stretches, a big yawn, sits back."""
    st, si = stretch(), sit()
    if f == 0:
        P = blend(stand(), st, 0.35)
    elif f == 1:
        P = blend(stand(), st, 0.8)
        P['eyes'] = 'closed'
    elif f == 2:
        P = st
        P['eyes'] = 'closed'
        P['mouth'], P['mouth_open'] = 'open', 0.45
    elif f == 3:
        P = blend(st, si, 0.5)
    else:
        P = si
        o = {4: 0.55, 5: 1.0, 6: 0.4, 7: 0.0}[f]
        if o > 0:
            P['mouth'], P['mouth_open'] = 'open', o
        P['eyes'] = 'closed' if f in (5, 6) else 'open'
        P['head_rot'] = (-18 * o, 0, 0)
        P['head'] = P['head'] + V((0, 0.01 * o, 0.012 * o))
        P['ears'] = (-6 * o, 10 * o)
    return P


def _walk_paw(y0, p, A, Hh, x0, duty=0.6, z0=0.03):
    if p < duty:
        return V((x0, y0 - A + 2 * A * (p / duty), z0))
    s = (p - duty) / (1 - duty)
    return V((x0, y0 + A * math.cos(math.pi * s), z0 + Hh * math.sin(math.pi * s)))


def c_walk(f, n=6, d='s'):
    """A calm walk (lateral sequence: left hind, left fore, right hind, right fore)."""
    t = f / n
    P = stand()
    off = {'bl': 0.0, 'fl': 0.25, 'br': 0.5, 'fr': 0.75}
    for k, (root, paw) in P['legs'].items():
        p = (t + off[k]) % 1.0
        front = k[0] == 'f'
        P['legs'][k][1] = _walk_paw(paw.y, p, 0.075 if front else 0.07, 0.06 if front else 0.05, paw.x)
    lift_body(P, 0.007 * math.cos(2 * TAU * t))
    P['head'] = P['head'] + V((0, -0.005, 0.005 * math.cos(2 * TAU * t + 0.8)))
    P['head_rot'] = (2, 0, 3 * math.sin(TAU * t))
    P['tail'] = sway_tail(vv(TAIL_WALK), 10 * math.sin(TAU * t), -6)
    return P


def c_run(f, n=6, d='e'):
    """A gallop: the body stretches (paws far apart, a moment in the air) and
    gathers (paws under her)."""
    t = f / n
    e = math.cos(TAU * t)          # 1 stretched, -1 gathered
    w = math.sin(TAU * t)
    P = stand()
    P['chest'] = V((0, -0.06 - 0.06 * e, 0.24))
    P['rump'] = V((0, 0.17 + 0.06 * e, 0.22 + 0.025 * max(0.0, -e)))
    P['arch'] = 0.035 * max(0.0, -e)
    P['head'] = V((0, -0.19 - 0.05 * e, 0.42))
    P['head_rot'] = (-4 + 6 * e, 0, 0)
    for k, lag in (('fl', 0.0), ('fr', 0.07), ('bl', 0.0), ('br', 0.07)):
        tt = TAU * (t - lag)
        ee, ww = math.cos(tt), math.sin(tt)
        x = P['legs'][k][1].x
        if k[0] == 'f':
            y = -0.13 - 0.17 * ee
            z = 0.03 + 0.10 * max(0.0, ww)
            root = V((x, P['chest'].y - 0.02, 0.19))
        else:
            y = 0.17 + 0.18 * ee
            z = 0.03 + 0.09 * max(0.0, -ww)
            root = V((x, P['rump'].y, 0.19))
        P['legs'][k] = [root, V((x, y, z))]
    P['thighs'] = [(V((sx * 0.075, P['rump'].y - 0.01, 0.18)), (0.06, 0.085, 0.09), (-20 * e, 0, 0)) for sx in (1, -1)]
    lift_body(P, 0.075 * max(0.0, e))
    for kk in P['legs']:
        if e > 0.3:
            P['legs'][kk][1] = P['legs'][kk][1] + V((0, 0, 0.05 * e))
    b = P['rump'] + V((0, 0.08, 0.02))
    P['tail'] = [b, b + V((0, 0.10, 0.04)), b + V((0.01, 0.20, 0.09 + 0.03 * w)), b + V((0.0, 0.29, 0.15 + 0.05 * w)),
                 b + V((-0.02, 0.35, 0.22 + 0.06 * w))]
    P['ears'] = (-14, 16)
    P['pupil'] = 1.4
    return P


def crouch():
    P = stand()
    P['chest'] = V((0, -0.08, 0.15))
    P['rump'] = V((0, 0.17, 0.17))
    P['head'] = V((0, -0.23, 0.31))
    P['head_rot'] = (-8, 0, 0)
    P['legs'] = {'fl': [V((0.07, -0.10, 0.13)), V((0.07, -0.18, 0.03))],
                 'fr': [V((-0.07, -0.10, 0.13)), V((-0.07, -0.18, 0.03))],
                 'bl': [V((0.08, 0.15, 0.15)), V((0.095, 0.11, 0.03))],
                 'br': [V((-0.08, 0.15, 0.15)), V((-0.095, 0.11, 0.03))]}
    P['thighs'] = [(V((0.085, 0.16, 0.14)), (0.065, 0.10, 0.08), None),
                   (V((-0.085, 0.16, 0.14)), (0.065, 0.10, 0.08), None)]
    P['tail'] = vv([(0, 0.26, 0.16), (0, 0.36, 0.12), (0, 0.46, 0.08), (0.02, 0.55, 0.06), (0.05, 0.62, 0.07)])
    P['pupil'] = 1.9
    P['ears'] = (-4, -2)
    return P


def c_pounce(f, n=8, d='e'):
    """Crouch, butt wiggle, leap (~0.6 m: the watch moves her), land."""
    P = crouch()
    if f in (1, 2, 3):
        sx = (1, -1, 1)[f - 1]
        P['rump'] = P['rump'] + V((0.025 * sx, 0, 0.035))
        P['thighs'] = [(c + V((0.025 * sx, 0, 0.03)), s, r) for (c, s, r) in P['thighs']]
        for k in ('bl', 'br'):
            P['legs'][k][0] = P['legs'][k][0] + V((0.02 * sx, 0, 0.03))
        P['tail'] = sway_tail(P['tail'], 14 * sx, 6)
    elif f == 4:        # take-off
        P['chest'] = V((0, -0.14, 0.28))
        P['rump'] = V((0, 0.16, 0.22))
        P['head'] = V((0, -0.31, 0.40))
        P['head_rot'] = (-10, 0, 0)
        P['legs'] = {'fl': [V((0.07, -0.16, 0.26)), V((0.07, -0.36, 0.30))],
                     'fr': [V((-0.07, -0.16, 0.26)), V((-0.07, -0.34, 0.26))],
                     'bl': [V((0.08, 0.17, 0.20)), V((0.08, 0.36, 0.05))],
                     'br': [V((-0.08, 0.17, 0.20)), V((-0.08, 0.34, 0.04))]}
        P['thighs'] = [(V((sx * 0.078, 0.18, 0.19)), (0.06, 0.09, 0.085), (40, 0, 0)) for sx in (1, -1)]
        P['tail'] = vv([(0, 0.26, 0.22), (0, 0.36, 0.22), (0, 0.46, 0.21), (0.0, 0.56, 0.21), (0.02, 0.64, 0.23)])
    elif f == 5:        # in the air
        P['chest'] = V((0, -0.10, 0.40))
        P['rump'] = V((0, 0.18, 0.36))
        P['head'] = V((0, -0.27, 0.51))
        P['head_rot'] = (12, 0, 0)
        P['legs'] = {'fl': [V((0.07, -0.12, 0.37)), V((0.09, -0.31, 0.25))],
                     'fr': [V((-0.07, -0.12, 0.37)), V((-0.09, -0.30, 0.24))],
                     'bl': [V((0.08, 0.19, 0.33)), V((0.08, 0.35, 0.24))],
                     'br': [V((-0.08, 0.19, 0.33)), V((-0.08, 0.34, 0.22))]}
        P['thighs'] = [(V((sx * 0.078, 0.19, 0.33)), (0.06, 0.09, 0.085), (30, 0, 0)) for sx in (1, -1)]
        P['tail'] = vv([(0, 0.27, 0.38), (0, 0.37, 0.40), (0, 0.47, 0.43), (0.0, 0.56, 0.47), (0.03, 0.62, 0.52)])
    elif f == 6:        # front paws land on it
        P['chest'] = V((0, -0.13, 0.18))
        P['rump'] = V((0, 0.15, 0.30))
        P['head'] = V((0, -0.29, 0.28))
        P['head_rot'] = (22, 0, 0)
        P['legs'] = {'fl': [V((0.07, -0.16, 0.15)), V((0.095, -0.30, 0.03))],
                     'fr': [V((-0.07, -0.16, 0.15)), V((-0.095, -0.29, 0.03))],
                     'bl': [V((0.08, 0.17, 0.28)), V((0.08, 0.30, 0.13))],
                     'br': [V((-0.08, 0.17, 0.28)), V((-0.08, 0.28, 0.10))]}
        P['thighs'] = [(V((sx * 0.078, 0.17, 0.27)), (0.06, 0.09, 0.085), (30, 0, 0)) for sx in (1, -1)]
        P['tail'] = vv([(0, 0.25, 0.32), (0, 0.33, 0.42), (0, 0.38, 0.54), (0.02, 0.38, 0.64), (0.06, 0.34, 0.69)])
    elif f == 7:        # got it
        P['legs']['fl'][1] = V((0.09, -0.26, 0.03))
        P['legs']['fr'][1] = V((-0.09, -0.25, 0.03))
        P['head'] = P['head'] + V((0, -0.03, -0.03))
        P['head_rot'] = (24, 0, 0)
        P['tail'] = sway_tail(vv(TAIL_WALK), 10)
        P['pupil'] = 1.6
    return P


def c_bat(f, n=6, d='e'):
    """Sitting, swats with the front paw on the camera's side."""
    P = sit()
    k, sx = near_paw(d)
    P['head_rot'] = (10, 0, 0)
    P['pupil'] = 1.5
    paws = {1: (0.09, -0.18, 0.30), 2: (0.07, -0.34, 0.22), 3: (0.02, -0.33, 0.06), 4: (0.07, -0.21, 0.10),
            5: (0.06, -0.13, 0.05)}
    if f in paws:
        x, y, z = paws[f]
        P['legs'][k] = [V((sx * 0.06, -0.06, 0.25)), V((sx * x, y, z))]
        P['sep_legs'] = [k]
        P['chest'] = P['chest'] + V((sx * 0.01, -0.015, -0.005))
        P['head_rot'] = (14 if f in (2, 3) else 10, 0, sx * 6)
        P['ears'] = (-4, 4) if f in (2, 3) else (0, 0)
    P['tail'] = sway_tail(vv(TAIL_SIT), 0)
    if f in (2, 3):
        P['tail'][-1] = P['tail'][-1] + V((0.02, -0.02, 0.05))
    return P


def c_jump(f, n=6, d='e'):
    """A hop straight up, swiping at the feather above."""
    k, sx = near_paw(d)
    if f in (0, 5):
        P = crouch()
        P['head_rot'] = (-25, 0, 0)
        P['head'] = P['head'] + V((0, 0.03, 0.03))
        P['pupil'] = 1.8
        return P
    h = {1: 0.05, 2: 0.22, 3: 0.20, 4: 0.08}[f]
    P = stand()
    P['body_up'] = (0, 1, 0.25)
    P['chest'] = V((0, -0.03, 0.43))
    P['rump'] = V((0, 0.09, 0.21))
    P['head'] = V((0, -0.10, 0.63))
    P['head_rot'] = (-32, 0, 0)
    P['legs'] = {'fl': [V((0.07, -0.08, 0.44)), V((0.08, -0.12, 0.64))],
                 'fr': [V((-0.07, -0.08, 0.44)), V((-0.08, -0.12, 0.64))],
                 'bl': [V((0.08, 0.10, 0.18)), V((0.08, 0.12, 0.03))],
                 'br': [V((-0.08, 0.10, 0.18)), V((-0.08, 0.14, 0.03))]}
    P['thighs'] = [(V((sx_ * 0.08, 0.10, 0.19)), (0.065, 0.085, 0.10), None) for sx_ in (1, -1)]
    P['tail'] = vv([(0, 0.18, 0.14), (0, 0.28, 0.08), (0, 0.38, 0.06), (0.03, 0.47, 0.08), (0.07, 0.52, 0.14)])
    P['pupil'] = 1.8
    P['sep_legs'] = ['fl', 'fr']
    P['hang'], P['scarf_len'] = (0, 0.45, -1), 0.24     # upright: cloth lies along her chest
    if f == 2:
        P['legs'][k][1] = V((sx * 0.03, -0.17, 0.80))
        P['legs'][('fl' if k == 'fr' else 'fr')][1] = V((-sx * 0.10, -0.08, 0.62))
    elif f == 3:
        P['legs'][k][1] = V((-sx * 0.05, -0.21, 0.68))
        P['head_rot'] = (-24, 0, -sx * 8)
    elif f == 4:
        for kk in ('fl', 'fr'):
            x = P['legs'][kk][1].x
            P['legs'][kk][1] = V((x, -0.17, 0.40))
        P['head_rot'] = (-12, 0, 0)
        P['chest'] = V((0, -0.05, 0.38))
        P['head'] = V((0, -0.13, 0.57))
    if f in (2, 3):     # hind legs dangle in the air
        for kk in ('bl', 'br'):
            P['legs'][kk][1] = P['legs'][kk][1] + V((0, -0.03, 0.04))
    shift(P, (0, 0, h))
    return P


POST_Y = -0.25 / MS     # the scratching post's face, 0.25 m in front of her
BOWL_Y = -0.20 / MS     # the food bowl, 0.2 m in front


def c_scratch(f, n=6, d='n'):
    """Up on her hind legs, front paws on the post (z 0.35-0.6 m), scratching."""
    t = f / n
    P = stand()
    P['body_up'] = (0, 1, 0.4)
    P['hang'], P['scarf_len'] = (0, 0.45, -1), 0.24     # upright: cloth lies along her chest
    P['chest'] = V((0, -0.09, 0.42))
    P['rump'] = V((0, 0.10, 0.19))
    P['head'] = V((0, -0.14, 0.62))
    sw = math.sin(TAU * t)
    P['chest'] = P['chest'] + V((0.025 * sw, 0, 0.01 * math.cos(2 * TAU * t)))
    P['head'] = P['head'] + V((0.02 * sw, 0, 0.012 * math.cos(2 * TAU * t)))
    P['head_rot'] = (-6, 7 * sw, 4 * math.sin(TAU * t))
    y = POST_Y + 0.035
    legs = {}
    for k, ph, sx in (('fl', 0.0, 1), ('fr', 0.5, -1)):
        s = (t + ph) % 1.0
        z = (0.66 - 0.24 * (s / 0.7)) if s < 0.7 else (0.42 + 0.24 * (s - 0.7) / 0.3)
        yy = y if s < 0.7 else y + 0.03
        legs[k] = [V((sx * 0.065, -0.12, 0.45)), V((sx * 0.07, yy, z))]
    legs['bl'] = [V((0.08, 0.10, 0.16)), V((0.08, 0.06, 0.03))]
    legs['br'] = [V((-0.08, 0.10, 0.16)), V((-0.08, 0.06, 0.03))]
    P['legs'] = legs
    P['thighs'] = [(V((sx * 0.08, 0.10, 0.17)), (0.065, 0.09, 0.10), None) for sx in (1, -1)]
    P['tail'] = sway_tail(vv([(0, 0.20, 0.12), (0, 0.30, 0.05), (0.03, 0.40, 0.035), (0.08, 0.49, 0.035),
                              (0.14, 0.53, 0.05)]), 12 * math.sin(TAU * t))
    P['ears'] = (-6, 6)
    return P


def c_eat(f, n=4, d='n'):
    """Head down in the bowl 0.2 m in front, chewing."""
    t = f / n
    P = stand()
    P['chest'] = V((0, -0.04, 0.20))
    P['rump'] = V((0, 0.19, 0.17))
    P['rump_s'] = (0.13, 0.12, 0.115)
    c = math.sin(TAU * t)
    P['head'] = V((0, BOWL_Y + 0.08, 0.235 + 0.012 * c))
    P['head_rot'] = (44 + 5 * c, 0, 0)
    P['legs'] = {'fl': [V((0.07, -0.06, 0.17)), V((0.08, -0.12, 0.03))],
                 'fr': [V((-0.07, -0.06, 0.17)), V((-0.08, -0.12, 0.03))],
                 'bl': [V((0.10, 0.14, 0.07)), V((0.11, 0.08, 0.03))],
                 'br': [V((-0.10, 0.14, 0.07)), V((-0.11, 0.08, 0.03))]}
    P['thighs'] = [(V((sx * 0.105, 0.16, 0.11)), (0.07, 0.11, 0.08), None) for sx in (1, -1)]
    P['tail'] = sway_tail(vv([(0.0, 0.28, 0.10), (0.10, 0.34, 0.05), (0.20, 0.28, 0.04), (0.24, 0.16, 0.04),
                              (0.21, 0.05, 0.04)]), 4 * c)
    P['ears'] = (4, 6)
    return P


def c_groom(f, n=8, d='s'):
    """Sitting: licks her paw, wipes her face with it."""
    P = sit()
    k = 'fr'
    root = V((-0.06, -0.07, 0.26))
    steps = {0: ((-0.05, -0.20, 0.25), (0, 0, 0), 'open', 0.0),
             1: ((-0.03, -0.40, 0.36), (10, -8, 0), 'closed', 0.3),
             2: ((-0.04, -0.41, 0.38), (14, -12, 0), 'closed', 0.45),
             3: ((-0.03, -0.40, 0.36), (8, -6, 0), 'closed', 0.3),
             4: ((-0.12, -0.38, 0.49), (6, -18, -10), 'closed', 0.0),
             5: ((-0.14, -0.31, 0.60), (12, -22, -12), 'closed', 0.0),
             6: ((-0.06, -0.16, 0.20), (0, -5, 0), 'open', 0.0),
             7: (None, (0, 0, 0), 'open', 0.0)}
    paw, hr, eyes, mo = steps[f]
    if paw is not None:
        P['legs'][k] = [root, V(paw)]
        P['sep_legs'] = [k]
    P['head_rot'] = hr
    P['eyes'] = eyes
    if mo > 0:
        P['mouth'], P['mouth_open'] = 'open', mo
    return P


def c_purr(f, n=6, d='s'):
    """Eyes closed, blissful, leaning into a pet."""
    t = f / n
    s, c = math.sin(TAU * t), math.cos(TAU * t)
    P = sit()
    P['eyes'] = 'happy'
    P['mouth'] = 'w'
    P['head'] = P['head'] + V((0.012 * s, 0.01, 0.018 + 0.006 * c))
    P['head_rot'] = (-14 - 3 * c, 12 * s, 4 * s)
    P['chest'] = P['chest'] + V((0.01 * s, 0, 0.005))
    P['breath'] = 0.02 * c
    P['ears'] = (4, 10)
    P['tail'] = sway_tail(vv(TAIL_SIT_UP), 10 * s)
    return P


def c_peek(f, n=4, d='s'):
    """Sitting low in a box (hidden below z 0.3 m): head and front paws up
    on the rim, looking around."""
    P = sit()
    P['chest'] = V((0, -0.10, 0.29))
    P['head'] = V((0, -0.19, 0.53))
    rim = 0.30 / MS + 0.02
    P['legs']['fl'] = [V((0.07, -0.12, 0.30)), V((0.085, -0.28, rim))]
    P['legs']['fr'] = [V((-0.07, -0.12, 0.30)), V((-0.085, -0.28, rim))]
    P['head_rot'] = (0, 0, (0, 28, 0, -28)[f])
    P['ears'] = (-4, -4)
    P['pupil'] = 1.4
    if f == 2:
        P['eyes'] = 'closed'
    return P


def c_lie(f, n=4, d='s'):
    """A loaf on the hammock (belly at z 0), tail hanging over the edge."""
    t = f / n
    s = math.sin(TAU * t)
    P = stand()
    P['chest'] = V((0, -0.07, 0.12))
    P['rump'] = V((0, 0.16, 0.115))
    P['chest_s'] = (0.11, 0.11, 0.11)
    P['rump_s'] = (0.125, 0.12, 0.11)
    P['breath'] = 0.02 * s
    P['head'] = V((0, -0.19, 0.30 + 0.004 * s))
    P['head_rot'] = (6, 3 * s, 0)
    P['legs'] = {'fl': [V((0.06, -0.10, 0.08)), V((0.06, -0.17, 0.035))],
                 'fr': [V((-0.06, -0.10, 0.08)), V((-0.06, -0.17, 0.035))],
                 'bl': [V((0.10, 0.14, 0.08)), V((0.12, 0.04, 0.035))],
                 'br': [V((-0.10, 0.14, 0.08)), V((-0.12, 0.04, 0.035))]}
    P['thighs'] = [(V((sx * 0.10, 0.14, 0.09)), (0.07, 0.10, 0.07), None) for sx in (1, -1)]
    sw = 0.03 * s
    P['tail'] = vv([(0.05, 0.25, 0.10), (0.16, 0.29, 0.05), (0.25 + sw * 0.3, 0.27, -0.05),
                    (0.28 + sw, 0.24, -0.18), (0.27 + sw * 1.5, 0.21, -0.30)])
    if f == 2:
        P['eyes'] = 'closed'
    return P


def turn(f, n=12, d='s'):
    return c_sit(0)


# ---------------------------------------------------------------------------
# the table: anim -> (dirs, frames, ms, zoom, pose function)
# ---------------------------------------------------------------------------
GAME, CASITA, SHOP = 1.0, 1.5, 3.0
ANIMS = {
    'idle': ('nesw', 4, 250, GAME, idle),
    'walk': ('nesw', 6, 40, GAME, walk),
    'push': ('nesw', 6, 50, GAME, push),
    'win': ('s', 8, 90, GAME, win),
    'yawn': ('s', 8, 120, GAME, yawn),
    'c_walk': ('nesw', 6, 70, CASITA, c_walk),
    'c_run': ('ew', 6, 45, CASITA, c_run),
    'c_sit': ('nesw', 4, 300, CASITA, c_sit),
    'c_sleep': ('s', 4, 450, CASITA, c_sleep),
    'c_belly': ('s', 6, 140, CASITA, c_belly),
    'c_pounce': ('ew', 8, 70, CASITA, c_pounce),
    'c_bat': ('ew', 6, 70, CASITA, c_bat),
    'c_jump': ('ew', 6, 60, CASITA, c_jump),
    'c_scratch': ('n', 6, 90, CASITA, c_scratch),
    'c_eat': ('n', 4, 200, CASITA, c_eat),
    'c_groom': ('s', 8, 120, CASITA, c_groom),
    'c_meow': ('s', 4, 110, CASITA, c_meow),
    'c_purr': ('s', 6, 150, CASITA, c_purr),
    'c_peek': ('s', 4, 250, CASITA, c_peek),
    'c_lie': ('s', 4, 300, CASITA, c_lie),
    'turn': ('-', 12, 0, SHOP, turn),
}


def pose(anim, f, d='s'):
    dirs, n, ms, zoom, fn = ANIMS[anim]
    return copy.deepcopy(fn(f, n, d))
