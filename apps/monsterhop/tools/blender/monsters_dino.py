"""Monster Hop - Lost Valley (dinosaurs): the style sample of the zone's
monsters (SPEC.md section 5 conventions).

    cd apps/monsterhop/tools/blender
    /Applications/Blender.app/Contents/MacOS/Blender -b -P monsters_dino.py -- \\
        --out ../../assets/monsters/_sample_dino [--only raptor,trex] [--samples 48]

  raptor   the zone monster, fully: idle 2 @ 400 and walk 6 @ 90, facings n e s w
  trike    triceratops (charges in a straight line): one still, facing s
  ptero    pterodactyl (a flier, anchor on the ground below it): one still, facing s
  compy    tiny dino that runs in packs: one still, facing s (the raptor's
           body at 0.45 scale with a bigger head, no crest)
  trex     the boss (the lair's chase level): a hero still, 2 x 2, roaring,
           facings s and e

Built like monsters.py (monsters_geo primitives on a rig of empties, posed,
turned by the facing's yaw, rendered under the neutral light with the light,
id, z and shadow passes; the watch recolours them per id). monsters.py runs
its main() on import, so the few helpers it has (eye, path_on, the material
table) are copied here, unchanged in spirit.

Shared ids (SPEC 5) as used by the dinosaurs: 1 hide, 2 belly / throat /
wing membrane, 3 dark (mouth, nostrils, pupils, beak), 4 white (teeth,
claws, horns), 5 eye glow, 6 crest and feathers (raptor), crest (ptero),
back ridge (trex), 7 stripes and spots, 11 frill (trike) / beak (ptero),
13 tongue.

The sprites go to a folder of their own (not assets/monsters/meta.json) so
the sample never reaches the packer or the existing sheets.
"""
import json
import math
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import mh_common as C  # noqa: E402
import monsters_geo as G  # noqa: E402
from monsters_geo import V, on_ell, track, rotm, TAU  # noqa: E402,F401
import bpy  # noqa: E402

PALETTES = {
    'raptor': {1: (84, 128, 196), 2: (232, 220, 186), 3: (32, 26, 42), 4: (250, 248, 236), 5: (255, 214, 40),
               6: (255, 122, 40), 7: (38, 56, 112)},
    'trike': {1: (150, 132, 176), 2: (206, 194, 214), 3: (44, 36, 52), 4: (250, 242, 214), 5: (80, 240, 255),
              7: (60, 176, 160), 11: (246, 144, 56)},
    'ptero': {1: (214, 92, 70), 2: (246, 170, 118), 3: (44, 30, 34), 4: (250, 246, 236), 5: (255, 240, 90),
              6: (255, 214, 64), 11: (246, 208, 110)},
    'compy': {1: (176, 206, 64), 2: (250, 234, 150), 3: (34, 40, 30), 4: (250, 248, 236), 5: (255, 70, 60),
              7: (44, 104, 56)},
    'trex': {1: (88, 134, 92), 2: (226, 208, 154), 3: (40, 28, 32), 4: (252, 248, 232), 5: (255, 140, 30),
             6: (64, 92, 70), 7: (48, 84, 60), 13: (236, 110, 130)},
}
IDS = {
    'raptor': {1: 'hide', 2: 'belly, throat, jaw', 3: 'dark (mouth, nostrils, slit pupils, brows)',
               4: 'white (teeth, claws, sickle claw)', 5: 'eye glow', 6: 'crest and arm feathers, tail tuft',
               7: 'back and tail stripes'},
    'trike': {1: 'hide', 2: 'belly, toes', 3: 'dark (beak, nostrils, pupils)', 4: 'white (horns, toenails, frill spikes)',
              5: 'eye glow', 7: 'frill spots, back spots', 11: 'frill'},
    'ptero': {1: 'body, wing bones', 2: 'wing membrane, belly', 3: 'dark (pupils, nostrils, feet)',
              4: 'white (claws)', 5: 'eye glow', 6: 'head crest', 11: 'beak'},
    'compy': {1: 'hide', 2: 'belly, throat, jaw', 3: 'dark (mouth, nostrils, pupils, brows)',
              4: 'white (teeth, claws)', 5: 'eye glow', 7: 'stripes'},
    'trex': {1: 'hide', 2: 'belly, jaw, throat', 3: 'dark (mouth inside, nostrils, pupils, brows)',
             4: 'white (teeth, claws)', 5: 'eye glow', 6: 'back ridge', 7: 'stripes', 13: 'tongue'},
}
HEIGHT = {'raptor': 0.9, 'trike': 0.72, 'ptero': 1.25, 'compy': 0.45, 'trex': 2.0}
YAW = {'s': 0.0, 'e': 90.0, 'n': 180.0, 'w': -90.0}

# who: [(anim, dirs, frames, ms)] -- SPEC 13.1
ANIMS = {
    'raptor': [('idle', 'nesw', 2, 400), ('walk', 'nesw', 6, 90), ('notice', 'nesw', 2, 150), ('run', 'nesw', 6, 50)],
    'trike': [('idle', 'nesw', 2, 400), ('walk', 'nesw', 6, 120), ('howl', 'nesw', 4, 120), ('run', 'nesw', 6, 60),
              ('stun', 'nesw', 4, 150)],
    'ptero': [('perch', 'nesw', 2, 300), ('fly', 'nesw', 4, 60), ('dive', 'nesw', 3, 60)],
    'compy': [('run', 'nesw', 4, 50)],
    'trex': [('run', 'new', 6, 70), ('roar', 'ns', 4, 120), ('stomp', 'n', 4, 90)],
}
# the album card (SPEC 12): the idle, or first, pose facing s
CARD = {'raptor': ('idle', 0, 2), 'trike': ('idle', 0, 2), 'ptero': ('perch', 0, 2), 'compy': ('run', 0, 4),
        'trex': ('roar', 0, 4)}
BOSSES = ('trex',)
# overall scale of each rig (the memory budget; SPEC 13.1 T-Rex ~2.0 m)
SIZE = {'raptor': 0.9, 'trike': 0.75, 'compy': 0.46, 'trex': 0.9}


MATS = {
    'skin': (1, 0.55, 0.0, 0.40), 'skin2': (2, 0.60, 0.0, 0.40), 'dark': (3, 0.45, 0.0, 0.50),
    'white': (4, 0.25, 0.0, 0.60), 'hair': (6, 0.60, 0.0, 0.40), 'c7': (7, 0.60, 0.0, 0.40),
    'a11': (11, 0.50, 0.0, 0.50), 'a12': (12, 0.60, 0.0, 0.40), 'x13': (13, 0.35, 0.0, 0.50),
}


def register_mats(pal):
    for key, (id_, rough, metal, spec) in MATS.items():
        C.mat(key, base=G.srgb_to_lin(pal.get(id_, (200, 200, 200))), id=id_, rough=rough, metal=metal, spec=spec)
    C.mat('glow', id=5, build=G.glow_build(pal.get(5, (255, 255, 255))))


def eye(rig, joint, name, c, r, az, el, rad, key='glow', flat=0.55, sink=0.35, pupil=None, rot=0.0, squash=1.0,
        lid=None):
    """(copy of monsters.eye) a glowing eye pushed into the head ellipsoid."""
    p, n = on_ell(c, r, az, el, 0.0)
    R = track(n) @ rotm((0, 0, rot))
    ctr = p - n * (rad * flat * sink)
    rig.add(G.ellipsoid(name, ctr, (rad, rad * squash, rad * flat), key, rot=R, seg=20, rings=10), joint)
    if pupil:
        dx, dy, ps = pupil[:3]
        st = pupil[3] if len(pupil) > 3 else 1.0
        tip = ctr + R @ V((dx * rad, dy * rad * squash, rad * flat * 0.90))
        rig.add(G.ellipsoid(name + '_pup', tip, (ps, ps * st, ps * 0.5), 'dark', rot=R, seg=12, rings=6), joint)
    if lid:
        lkey, cover = lid
        ry = rad * squash * 0.95
        yc = rad * squash * (1.0 - 2.0 * cover) + ry
        rig.add(G.ellipsoid(name + '_lid', ctr + R @ V((0, yc, rad * flat * 0.10)),
                            (rad * 1.22, ry, rad * flat * 1.15), lkey, rot=R, seg=20, rings=10), joint)
    return ctr, n


def path_on(c, r, pts, out=0.004, rot=None):
    return [on_ell(c, r, az, el, out, rot)[0] for az, el in pts]


def patch(rig, joint, name, c, r, az, el, size, key, rot=None, spin=0.0, out=-0.004):
    """A flat marking (stripe, spot) lying on an ellipsoid's surface."""
    p, n = on_ell(c, r, az, el, out, rot)
    R = track(n) @ rotm((0, 0, spin))
    rig.add(G.ellipsoid(name, p, size, key, rot=R, seg=16, rings=8), joint)


# ---------------------------------------------------------------------------
# Raptor (and the compy, the same body smaller with a bigger head)
# ---------------------------------------------------------------------------

R_TC, R_TR, R_TROT = V((0.0, 0.04, 0.50)), (0.150, 0.235, 0.160), (-14, 0, 0)
R_HC, R_HR = V((0.0, -0.245, 0.815)), (0.150, 0.165, 0.140)


def build_raptor(name='raptor', crest=True, sickle=True, stripes=3):
    r = G.Rig(name)
    r.joint('hips', 'root', (0, 0.06, 0.47))
    r.joint('neck', 'hips', (0, -0.14, 0.58))
    r.joint('head', 'neck', (0, -0.22, 0.73))
    r.joint('jaw', 'head', (0, -0.30, 0.73))
    r.joint('tail', 'hips', (0, 0.22, 0.52))
    r.joint('tail2', 'tail', (0, 0.40, 0.55))
    for s, side in ((-1, 'R'), (1, 'L')):
        r.joint('arm_' + side, 'hips', (s * 0.105, -0.15, 0.53))
        r.joint('leg_' + side, 'hips', (s * 0.105, 0.08, 0.44))
        r.joint('shin_' + side, 'leg_' + side, (s * 0.118, -0.03, 0.25))
        r.joint('foot_' + side, 'shin_' + side, (s * 0.118, 0.07, 0.085))

    # body
    r.add(G.ellipsoid(name + '_torso', R_TC, R_TR, 'skin', rot=R_TROT, seg=32, rings=16), 'hips')
    r.add(G.ellipsoid(name + '_belly', (0, -0.07, 0.455), (0.112, 0.160, 0.105), 'skin2', rot=(-20, 0, 0)), 'hips')
    # stripes across the back
    for k in range(stripes):
        patch(r, 'hips', '%s_str%d' % (name, k), R_TC, R_TR, 180, 100 - 26 * k, (0.11, 0.021, 0.011),
              'c7', rot=R_TROT, out=0.001)
    # neck and throat
    r.add(G.tube(name + '_neck', [(0, -0.10, 0.57), (0, -0.17, 0.66), (0, -0.22, 0.75)], [0.095, 0.085, 0.08],
                 'skin', n=16), 'neck')
    r.add(G.tube(name + '_throat', [(0, -0.155, 0.55), (0, -0.215, 0.64), (0, -0.265, 0.72)], [0.06, 0.058, 0.05],
                 'skin2', n=12), 'neck')

    # head: a big round cranium, a snout, a jaw with a toothy grin
    hc, hr = R_HC, R_HR
    r.add(G.ellipsoid(name + '_head', hc, hr, 'skin', seg=32, rings=16), 'head')

    def snout(u):
        if u.y < 0:
            u.x *= 1.0 + 0.25 * u.y
            u.z *= 1.0 + 0.10 * u.y
        return u
    r.add(G.ellipsoid(name + '_snout', (0, -0.395, 0.775), (0.108, 0.160, 0.082), 'skin', seg=28, rings=14,
                      deform=snout), 'head')
    r.add(G.ellipsoid(name + '_jaw', (0, -0.37, 0.708), (0.090, 0.150, 0.042), 'skin2', rot=(-4, 0, 0), seg=24,
                      rings=10), 'jaw')
    # the grin: a dark line round the snout, teeth hanging over it
    grin = [(-0.090, -0.30, 0.735), (-0.075, -0.42, 0.728), (-0.035, -0.515, 0.735), (0.0, -0.53, 0.738),
            (0.035, -0.515, 0.735), (0.075, -0.42, 0.728), (0.090, -0.30, 0.735)]
    r.add(G.tube(name + '_grin', grin, [0.014] * len(grin), 'dark', n=8, sub=3), 'head')
    for s_ in (-1, 1):
        for k, (x, y) in enumerate(((0.083, -0.345), (0.078, -0.395), (0.066, -0.445), (0.048, -0.49))):
            b0 = V((s_ * x, y, 0.738))
            r.add(G.tube('%s_tooth%d_%d' % (name, s_, k), [b0, b0 + V((0, -0.004, -0.030))], [0.0125, 0.0], 'white',
                         n=8, sub=1), 'head')
        r.add(G.ellipsoid('%s_nos%d' % (name, s_), (s_ * 0.032, -0.535, 0.800), (0.014, 0.010, 0.010), 'dark',
                          rot=(0, 0, s_ * 20), seg=10, rings=6), 'head')
    for s_ in (-1, 1):
        eye(r, 'head', '%s_eye%d' % (name, s_), hc, hr, s_ * 36, 20, 0.060, squash=1.0, rot=-s_ * 8,
            pupil=(0.05 * s_, 0.0, 0.016, 2.2), lid=('skin', 0.18))
        brow = path_on(hc, hr, [(s_ * 14, 40), (s_ * 34, 44), (s_ * 52, 38)], 0.010)
        r.add(G.tube('%s_brow%d' % (name, s_), brow, [0.020, 0.018, 0.0], 'dark', n=8, sub=3), 'head')
    if crest:
        r.joint('crest', 'head', (0, -0.22, 0.92))
        for k, (dx, h, bk) in enumerate(((0.0, 0.16, 0.20), (-0.05, 0.11, 0.16), (0.05, 0.11, 0.16))):
            p0 = on_ell(hc, hr, dx * 400, 70, -0.02)[0]
            r.add(G.tube('%s_crest%d' % (name, k), [p0, p0 + V((dx * 0.4, bk * 0.4, h * 0.8)),
                                                    p0 + V((dx * 0.8, bk, h))],
                         [(0.045, 0.02), (0.032, 0.014), 0.0], 'hair', n=10, nrm=(1, 0, 0)), 'crest')
    # arms: short, three claws, a feather flap
    for s, side in ((-1, 'R'), (1, 'L')):
        a0 = V((s * 0.105, -0.15, 0.53))
        el = V((s * 0.14, -0.20, 0.45))
        hd = V((s * 0.13, -0.28, 0.43))
        r.add(G.tube('%s_arm%s' % (name, side), [a0, el, hd], [0.040, 0.034, 0.030], 'skin', n=12), 'arm_' + side)
        for k, dx in enumerate((-0.018, 0.0, 0.018)):
            b0 = hd + V((dx, -0.02, 0.0))
            r.add(G.tube('%s_claw%s%d' % (name, side, k), [b0, b0 + V((0, -0.030, -0.028))], [0.011, 0.0], 'white',
                         n=6, sub=1), 'arm_' + side)
        if crest:
            r.add(G.tube('%s_feath%s' % (name, side), [el + V((s * 0.02, 0.03, 0.0)), el + V((s * 0.04, 0.07, -0.05)),
                                                        el + V((s * 0.04, 0.10, -0.10))],
                         [(0.012, 0.035), (0.010, 0.028), 0.0], 'hair', n=8, nrm=(0, 0, 1)), 'arm_' + side)
    # legs: a big thigh, a thin shin back to the ankle, a foot with a raised
    # sickle claw
    for s, side in ((-1, 'R'), (1, 'L')):
        x = s * 0.118
        r.add(G.ellipsoid('%s_thigh%s' % (name, side), (s * 0.112, 0.03, 0.37), (0.078, 0.115, 0.118), 'skin',
                          rot=(20, 0, 0), seg=20, rings=10), 'leg_' + side)
        r.add(G.tube('%s_shin%s' % (name, side), [(x, -0.03, 0.26), (x, 0.03, 0.16), (x, 0.07, 0.085)],
                     [0.050, 0.040, 0.034], 'skin', n=12), 'shin_' + side)

        def flat_bottom(u):
            if u.z < -0.3:
                u.z = -0.3 + (u.z + 0.3) * 0.25
            return u
        r.add(G.ellipsoid('%s_foot%s' % (name, side), (x, -0.015, 0.035), (0.052, 0.095, 0.036), 'skin',
                          deform=flat_bottom, seg=16, rings=8), 'foot_' + side)
        for k, dx in enumerate((-0.022, 0.022)):
            b0 = V((x + dx, -0.10, 0.03))
            r.add(G.tube('%s_toe%s%d' % (name, side, k), [b0, b0 + V((0, -0.028, -0.018))], [0.012, 0.0], 'white',
                         n=6, sub=1), 'foot_' + side)
        if sickle:
            b0 = V((x + s * 0.012, -0.03, 0.06))
            r.add(G.tube('%s_sickle%s' % (name, side), [b0, b0 + V((0, -0.02, 0.05)), b0 + V((0, -0.065, 0.06))],
                         [0.018, 0.012, 0.0], 'white', n=8, sub=3), 'foot_' + side)
    # tail: two segments, banded, a feather tuft at the tip
    r.add(G.tube(name + '_tail1', [(0, 0.16, 0.51), (0, 0.28, 0.53), (0, 0.42, 0.55)], [0.105, 0.085, 0.064],
                 'skin', n=16), 'tail')
    r.add(G.tube(name + '_tail2', [(0, 0.40, 0.55), (0, 0.50, 0.565), (0, 0.62, 0.585)], [0.066, 0.045, 0.0],
                 'skin', n=14), 'tail2')
    for k, (y, rad, jt) in enumerate(((0.30, 0.082, 'tail'), (0.45, 0.058, 'tail2'), (0.53, 0.040, 'tail2'))):
        z = 0.53 + (y - 0.28) * 0.15
        r.add(G.ringband('%s_band%d' % (name, k), (0, y, z), (0, 1, 0.15), rad, 0.035, 'c7', thick=0.006,
                         ref=(1, 0, 0)), jt)
    if crest:
        for k, dx in enumerate((-0.03, 0.0, 0.03)):
            b0 = V((dx * 0.5, 0.58, 0.583))
            r.add(G.tube('%s_tuft%d' % (name, k), [b0, b0 + V((dx, 0.06, 0.03)), b0 + V((dx * 2, 0.12, 0.04))],
                         [(0.03, 0.012), (0.022, 0.009), 0.0], 'hair', n=8, nrm=(0, 0, 1)), 'tail2')
    return r


def pose_raptor(anim, f, n, d='s'):
    ph = TAU * f / n
    base = {'hips': (0, 0, 0), 'neck': (-6, 0, 0), 'head': (8, 0, 0), 'jaw': (0, 0, 0),
            'arm_R': (-10, 0, -6), 'arm_L': (-10, 0, 6), 'leg_R': (-10, 0, 0), 'leg_L': (-10, 0, 0),
            'shin_R': (14, 0, 0), 'shin_L': (14, 0, 0), 'foot_R': (-4, 0, 0), 'foot_L': (-4, 0, 0),
            'tail': (4, 0, 0), 'tail2': (4, 0, 0), 'hips@': (0, 0, -0.012)}
    if anim == 'idle':
        # breathing, the head cocked one way then the other, the tail swaying
        b = [0.0, 1.0][f]
        base.update({'hips@': (0, 0, -0.012 - 0.008 * b), 'neck': (-6 + 4 * b, 0, 0),
                     'head': (8 - 4 * b, 0, [-8, 10][f]), 'jaw': (3 * b, 0, 0),
                     'tail': (4 - 3 * b, 0, [6, -6][f]), 'tail2': (4, 0, [8, -10][f]),
                     'arm_R': (-10 - 8 * b, 0, -6), 'arm_L': (-10 - 8 * b, 0, 6)})
        return base
    if anim == 'walk':
        # a stalking walk, one step per three frames; a bob twice a cycle
        out = dict(base)
        for side, off in (('R', 0.0), ('L', math.pi)):
            q = ph + off
            swing = 26 * math.sin(q)               # + back, - forward
            lift = max(0.0, -math.cos(q))           # swinging forward: the foot lifts
            out['leg_' + side] = (-10 + swing, 0, 0)
            out['shin_' + side] = (14 + 34 * lift, 0, 0)
            out['foot_' + side] = (-4 - 30 * lift + 0.4 * swing, 0, 0)
        out['hips@'] = (0, 0, -0.012 - 0.018 * abs(math.cos(ph)))
        out['hips'] = (-2 * math.cos(2 * ph), 0, 3 * math.sin(ph))
        out['neck'] = (-8 + 3 * math.cos(2 * ph), 0, -4 * math.sin(ph))
        out['head'] = (10, 0, -3 * math.sin(ph))
        out['tail'] = (4 + 2 * math.cos(2 * ph), 0, -8 * math.sin(ph))
        out['tail2'] = (4, 0, -12 * math.sin(ph - 0.7))
        out['arm_R'] = (-14 - 10 * math.sin(ph), 0, -6)
        out['arm_L'] = (-14 + 10 * math.sin(ph), 0, 6)
        return out
    if anim == 'notice':
        # head snaps up, crest flares, jaw drops, arms up: it has seen Tommy
        k = [0.0, 1.0][f]
        base.update({'hips@': (0, 0, 0.01), 'hips': (-6 - 2 * k, 0, 0), 'neck': (-22 - 6 * k, 0, 0),
                     'head': (-8 - 6 * k, 0, 4 * k), 'jaw': (10 + 10 * k, 0, 0),
                     'crest': (-18 - 8 * k, 0, 0), 'crest*': 1.25 + 0.2 * k,
                     'arm_R': (-34 - 14 * k, 0, -22 - 8 * k), 'arm_L': (-34 - 14 * k, 0, 22 + 8 * k),
                     'leg_R': (-4, 0, 0), 'leg_L': (-4, 0, 0), 'shin_R': (6, 0, 0), 'shin_L': (6, 0, 0),
                     'tail': (-6 - 4 * k, 0, 0), 'tail2': (-6, 0, 0)})
        return base
    if anim == 'run':
        # a sprint: body pitched forward, big strides, the tail straight out
        out = dict(base)
        lean = 14
        for side, off in (('R', 0.0), ('L', math.pi)):
            q = ph + off
            swing = 46 * math.sin(q)
            lift = max(0.0, -math.cos(q))
            out['leg_' + side] = (-10 - lean + swing, 0, 0)
            out['shin_' + side] = (18 + 52 * lift, 0, 0)
            out['foot_' + side] = (-6 - 40 * lift + 0.5 * swing, 0, 0)
        out['hips'] = (lean + 3 * math.cos(2 * ph), 0, 2 * math.sin(ph))
        out['hips@'] = (0, 0, -0.035 + 0.03 * (1 - abs(math.cos(ph))))
        out['neck'] = (-26, 0, 0)
        out['head'] = (18 - 3 * math.cos(2 * ph), 0, 0)
        out['jaw'] = (10, 0, 0)
        out['tail'] = (-12 + 3 * math.cos(2 * ph), 0, -4 * math.sin(ph))
        out['tail2'] = (-4, 0, -6 * math.sin(ph - 0.7))
        out['arm_R'] = (-50, 0, -10)
        out['arm_L'] = (-50, 0, 10)
        return out
    return None


def build_compy():
    r = build_raptor('compy', crest=False, sickle=False, stripes=3)
    return r


def pose_compy(anim, f, n, d='s'):
    P = pose_raptor('run', f, n, d)
    P.update({'head*': 1.28})
    return P


# ---------------------------------------------------------------------------
# Triceratops
# ---------------------------------------------------------------------------

T_BC, T_BR = V((0, 0.12, 0.43)), (0.27, 0.36, 0.25)
T_HC, T_HR = V((0, -0.37, 0.52)), (0.225, 0.22, 0.21)


def build_trike():
    r = G.Rig('trike')
    r.joint('body', 'root', (0, 0.1, 0.4))
    r.joint('head', 'body', (0, -0.24, 0.50))
    r.joint('tail', 'body', (0, 0.45, 0.42))
    legs = {'FR': (-0.17, -0.10), 'FL': (0.17, -0.10), 'BR': (-0.18, 0.30), 'BL': (0.18, 0.30)}
    for k, (x, y) in legs.items():
        r.joint('leg_' + k, 'body', (x, y, 0.34))
    r.add(G.ellipsoid('t_body', T_BC, T_BR, 'skin', seg=32, rings=16), 'body')
    r.add(G.ellipsoid('t_belly', (0, 0.06, 0.30), (0.21, 0.30, 0.15), 'skin2', seg=28, rings=12), 'body')
    for k, (az, el, sz) in enumerate(((150, 60, 0.05), (175, 70, 0.06), (205, 58, 0.045), (130, 40, 0.04),
                                      (230, 40, 0.04), (180, 48, 0.05))):
        patch(r, 'body', 't_spot%d' % k, T_BC, T_BR, az, el, (sz, sz * 0.9, 0.014), 'c7', out=0.0)
    for k, (x, y) in legs.items():
        r.add(G.tube('t_leg' + k, [(x, y, 0.36), (x * 1.05, y, 0.18), (x * 1.05, y - 0.01, 0.05)], [0.10, 0.085, 0.085],
                     'skin', n=14), 'leg_' + k)
        r.add(G.ellipsoid('t_foot' + k, (x * 1.05, y - 0.02, 0.035), (0.092, 0.098, 0.04), 'skin2', seg=16, rings=8),
              'leg_' + k)
        for j, dx in enumerate((-0.045, 0.0, 0.045)):
            r.add(G.ellipsoid('t_nail%s%d' % (k, j), (x * 1.05 + dx, y - 0.105, 0.03), (0.022, 0.016, 0.022), 'white',
                              seg=10, rings=6), 'leg_' + k)
    r.add(G.tube('t_tail', [(0, 0.44, 0.42), (0, 0.60, 0.34), (0.02, 0.74, 0.22)], [0.13, 0.08, 0.0], 'skin', n=14),
          'tail')
    # head: round, a beak, three horns, a big frill behind with spikes and spots
    hc, hr = T_HC, T_HR
    r.add(G.ellipsoid('t_head', hc, hr, 'skin', seg=32, rings=16), 'head')
    r.add(G.ellipsoid('t_snout', (0, -0.55, 0.45), (0.13, 0.15, 0.12), 'skin', seg=24, rings=12), 'head')
    r.add(G.tube('t_beak', [(0, -0.64, 0.45), (0, -0.71, 0.41), (0, -0.75, 0.35)], [(0.08, 0.065), (0.055, 0.045), 0.0],
                 'dark', n=12, nrm=(1, 0, 0)), 'head')
    r.add(G.ellipsoid('t_cheeks', (0, -0.47, 0.42), (0.19, 0.13, 0.10), 'skin2', seg=24, rings=10), 'head')
    for s in (-1, 1):
        r.add(G.ellipsoid('t_nos%d' % s, (s * 0.045, -0.68, 0.51), (0.018, 0.013, 0.013), 'dark', seg=10, rings=6),
              'head')
        eye(r, 'head', 't_eye%d' % s, hc, hr, s * 38, 14, 0.062, pupil=(0.0, 0.05, 0.02), lid=('skin', 0.15))
        b0 = V((s * 0.095, -0.50, 0.66))
        r.add(G.tube('t_horn%d' % s, [b0, b0 + V((s * 0.02, -0.08, 0.10)), b0 + V((s * 0.03, -0.20, 0.15))],
                     [0.050, 0.034, 0.0], 'white', n=12, sub=4), 'head')
    b0 = V((0, -0.66, 0.54))
    r.add(G.tube('t_nhorn', [b0, b0 + V((0, -0.03, 0.06)), b0 + V((0, -0.035, 0.10))], [0.04, 0.025, 0.0], 'white',
                 n=10), 'head')
    fc, fr = V((0, -0.14, 0.66)), 0.31
    tilt = math.radians(50)

    def frill(u, v):
        a = math.radians(-105 + 210 * u)
        rr = fr * v * (1.0 + 0.06 * math.cos(9 * a) * v)
        x = rr * math.sin(a)
        zz = rr * math.cos(a)
        return (fc.x + x, fc.y + zz * math.sin(tilt) + 0.02 * v, fc.z + zz * math.cos(tilt) - 0.06)
    r.add(G.surface('t_frill', frill, 30, 8, 'a11', thick=0.035, offset=0.0), 'head')
    for k in range(9):
        a = math.radians(-100 + 200 * k / 8)
        p = V(frill((a + math.radians(105)) / math.radians(210), 1.0))
        d = (p - V((fc.x, fc.y + 0.0, fc.z - 0.06))).normalized()
        r.add(G.tube('t_spike%d' % k, [p - d * 0.01, p + d * 0.07], [0.028, 0.0], 'white', n=8, sub=1), 'head')
    for k, (u, v, sz) in enumerate(((0.3, 0.6, 0.045), (0.7, 0.6, 0.045), (0.5, 0.75, 0.04), (0.15, 0.8, 0.03),
                                     (0.85, 0.8, 0.03))):
        p = V(frill(u, v))
        nrm = V((0, -math.cos(tilt), math.sin(tilt)))
        r.add(G.ellipsoid('t_fspot%d' % k, p + nrm * 0.018, (sz, sz, 0.01), 'c7', rot=track(nrm), seg=14, rings=6),
              'head')
    return r


def pose_trike(anim, f, n, d='s'):
    ph = TAU * f / n
    P = {'head': (4, 0, 0), 'body': (0, 0, 0), 'tail': (0, 0, 0)}
    if anim == 'idle':
        b = [0.0, 1.0][f]
        P.update({'body@': (0, 0, -0.008 * b), 'head': (4 + 3 * b, 0, [-6, 6][f]), 'tail': (0, 0, [8, -8][f])})
        return P
    if anim == 'walk':
        # a trotting walk: FR with BL, FL with BR
        for k, off in (('FR', 0.0), ('BL', 0.0), ('FL', math.pi), ('BR', math.pi)):
            P['leg_' + k] = (22 * math.sin(ph + off), 0, 0)
        P.update({'body@': (0, 0, -0.012 * abs(math.cos(ph))), 'body': (0, 0, 2 * math.sin(ph)),
                  'head': (4 + 3 * math.sin(2 * ph), 0, -3 * math.sin(ph)), 'tail': (0, 0, 8 * math.sin(ph))})
        return P
    if anim == 'howl':
        # the warning: head down, horns forward, pawing the ground with FR
        paw = [-28, 12, -28, 12][f]
        P.update({'body': (5, 0, 0), 'body@': (0, 0, -0.02), 'head': (18 + 3 * (f % 2), 0, [-4, 2, 4, -2][f]),
                  'leg_FR': (paw, 0, 0), 'leg_FL': (-4, 0, 0), 'leg_BR': (6, 0, 0), 'leg_BL': (6, 0, 0),
                  'tail': (-20, 0, [10, -10, 10, -10][f])})
        return P
    if anim == 'run':
        # the charge: a gallop, front pair then back pair, head down
        for k, off in (('FR', 0.0), ('FL', 0.5), ('BR', math.pi), ('BL', math.pi + 0.5)):
            P['leg_' + k] = (38 * math.sin(ph + off), 0, 0)
        P.update({'body': (6 * math.cos(ph), 0, 0), 'body@': (0, 0, 0.03 * abs(math.sin(ph)) - 0.01),
                  'head': (18 + 4 * math.cos(ph), 0, 0), 'tail': (-24, 0, 6 * math.sin(ph))})
        return P
    if anim == 'stun':
        # sat on its haunches after hitting something, head wobbling round
        c, s_ = math.cos(ph), math.sin(ph)
        P.update({'body@': (0, 0.04, -0.10), 'body': (-12, 0, 0), 'head': (-2 + 8 * c, 12 * s_, 8 * c),
                  'leg_FR': (-8, 0, -18), 'leg_FL': (-8, 0, 18), 'leg_BR': (-56, 0, -14), 'leg_BL': (-56, 0, 14),
                  'tail': (18, 0, 10 * s_)})
        return P
    return None


# ---------------------------------------------------------------------------
# Pterodactyl (flies at ~1.0 m; the anchor is the ground below it)
# ---------------------------------------------------------------------------

P_Z = 1.0
P_HC, P_HR = V((0, -0.19, P_Z + 0.08)), (0.13, 0.14, 0.12)


def ptero_wing(s, lift=0.0):
    """Membrane from the body side to the wing tip: leading edge an arm bone,
    trailing edge scalloped back to the leg."""
    def lead(u):
        return V((s * (0.06 + 0.60 * u), -0.04 - 0.05 * math.sin(math.pi * u), P_Z + 0.05 + (0.10 + lift) * math.sin(math.pi * u * 0.85) - 0.06 * u))

    def trail(u):
        return V((s * (0.06 + 0.56 * u ** 0.9), 0.15 - 0.16 * u + 0.025 * math.sin(3 * math.pi * u),
                  P_Z + 0.0 + (0.05 + lift * 0.8) * math.sin(math.pi * u * 0.85) - 0.05 * u))

    def fn(u, v):
        return lead(u).lerp(trail(u), v)
    return fn, lead


def build_ptero():
    r = G.Rig('ptero')
    r.joint('body', 'root', (0, 0, P_Z))
    r.joint('head', 'body', (0, -0.10, P_Z + 0.04))
    r.joint('wing_R', 'body', (-0.06, -0.02, P_Z + 0.04))
    r.joint('wing_L', 'body', (0.06, -0.02, P_Z + 0.04))
    r.add(G.ellipsoid('p_body', (0, 0.03, P_Z), (0.11, 0.19, 0.105), 'skin', rot=(-8, 0, 0), seg=24, rings=12),
          'body')
    r.add(G.ellipsoid('p_belly', (0, -0.01, P_Z - 0.035), (0.085, 0.14, 0.075), 'skin2', seg=20, rings=10), 'body')
    for s in (-1, 1):
        r.add(G.tube('p_leg%d' % s, [(s * 0.04, 0.12, P_Z - 0.03), (s * 0.05, 0.20, P_Z - 0.09), (s * 0.05, 0.24, P_Z - 0.12)],
                     [0.02, 0.016, 0.012], 'skin', n=8), 'body')
        r.add(G.ellipsoid('p_ft%d' % s, (s * 0.05, 0.25, P_Z - 0.13), (0.022, 0.03, 0.012), 'dark', seg=10, rings=6),
              'body')
    r.add(G.tube('p_tail', [(0, 0.16, P_Z), (0, 0.26, P_Z - 0.01)], [0.03, 0.0], 'skin', n=8), 'body')
    hc, hr = P_HC, P_HR
    r.add(G.ellipsoid('p_head', hc, hr, 'skin', seg=28, rings=14), 'head')
    r.add(G.tube('p_beak', [(0, -0.28, P_Z + 0.06), (0, -0.40, P_Z + 0.035), (0, -0.52, P_Z + 0.01)],
                 [(0.05, 0.06), (0.034, 0.04), 0.0], 'a11', n=12, nrm=(1, 0, 0)), 'head')
    r.add(G.tube('p_mouth', [(-0.05, -0.28, P_Z + 0.04), (0, -0.33, P_Z + 0.03), (0.05, -0.28, P_Z + 0.04)],
                 [0.009] * 3, 'dark', n=6, sub=3), 'head')
    r.add(G.tube('p_crest', [(0, -0.14, P_Z + 0.18), (0, -0.03, P_Z + 0.25), (0, 0.10, P_Z + 0.29)],
                 [(0.018, 0.06), (0.012, 0.045), 0.0], 'hair', n=10, nrm=(1, 0, 0)), 'head')
    for s in (-1, 1):
        eye(r, 'head', 'p_eye%d' % s, hc, hr, s * 40, 18, 0.05, pupil=(0.0, 0.0, 0.014), lid=('skin', 0.12))
        fn, lead = ptero_wing(s, 0.06)
        r.add(G.surface('p_wing%d' % s, fn, 22, 6, 'skin2', thick=0.014), 'wing_' + ('L' if s > 0 else 'R'))
        pts = [lead(u) for u in (0.0, 0.25, 0.5, 0.75, 1.0)]
        r.add(G.tube('p_arm%d' % s, pts, [0.03, 0.026, 0.02, 0.014, 0.0], 'skin', n=10),
              'wing_' + ('L' if s > 0 else 'R'))
        c0 = lead(0.52)
        r.add(G.tube('p_claw%d' % s, [c0, c0 + V((0, -0.05, 0.02))], [0.012, 0.0], 'white', n=6, sub=1),
              'wing_' + ('L' if s > 0 else 'R'))
    return r


def pose_ptero(anim, f, n, d='s'):
    if anim == 'perch':
        # on the ground, wings folded back along the body
        b = [0.0, 1.0][f]
        return {'body@': (0, 0, -(P_Z - 0.14) - 0.006 * b), 'body': (-12, 0, 0), 'head': (-6 - 4 * b, 0, [-10, 12][f]),
                'wing_R': (0, -12, -78), 'wing_L': (0, 12, 78), 'wing_R*': (0.55, 1, 1), 'wing_L*': (0.55, 1, 1)}
    if anim == 'fly':
        w = [28, 6, -26, -4][f]
        return {'wing_R': (0, w, 0), 'wing_L': (0, -w, 0), 'body@': (0, 0, [0.03, 0.0, -0.03, 0.0][f]),
                'head': (-6, 0, 0), 'body': (-6, 0, 0)}
    if anim == 'dive':
        # swooping from 1.0 m down to 0.3 m, wings swept back, pulling up at the end
        return {'body@': (0, 0, [-0.15, -0.45, -0.70][f]), 'body': ([25, 38, 12][f], 0, 0),
                'head': ([-10, -14, -4][f], 0, 0), 'wing_R': (0, [22, 18, 30][f], [-40, -48, -20][f]),
                'wing_L': (0, [-22, -18, -30][f], [40, 48, 20][f])}
    return None


# ---------------------------------------------------------------------------
# T-Rex (the boss): 2 x 2, anchor at the centre of the block
# ---------------------------------------------------------------------------

X_BC, X_BR, X_BROT = V((0, 0.12, 1.02)), (0.40, 0.56, 0.44), (-18, 0, 0)
X_HC, X_HR = V((0, -0.54, 1.66)), (0.34, 0.38, 0.30)


def build_trex():
    r = G.Rig('trex', anchor=V((1.0, 1.0, 0.0)))
    r.joint('hips', 'root', (0, 0.10, 0.95))
    r.joint('neck', 'hips', (0, -0.30, 1.28))
    r.joint('head', 'neck', (0, -0.44, 1.52))
    r.joint('jaw', 'head', (0, -0.40, 1.46))
    r.joint('tail', 'hips', (0, 0.55, 1.05))
    for s, side in ((-1, 'R'), (1, 'L')):
        r.joint('arm_' + side, 'hips', (s * 0.30, -0.36, 1.10))
        r.joint('leg_' + side, 'hips', (s * 0.28, 0.12, 0.92))
    r.add(G.ellipsoid('x_body', X_BC, X_BR, 'skin', rot=X_BROT, seg=36, rings=18), 'hips')
    r.add(G.ellipsoid('x_belly', (0, -0.16, 0.92), (0.30, 0.34, 0.30), 'skin2', rot=(-25, 0, 0), seg=28, rings=14),
          'hips')
    for k in range(5):
        patch(r, 'hips', 'x_str%d' % k, X_BC, X_BR, 180, 88 - 14 * (k - 1.5), (0.28, 0.05, 0.02), 'c7', rot=X_BROT,
              out=0.001)
    r.add(G.tube('x_neck', [(0, -0.22, 1.20), (0, -0.36, 1.38), (0, -0.46, 1.56)], [0.30, 0.27, 0.25], 'skin', n=20),
          'neck')
    r.add(G.tube('x_throat', [(0, -0.36, 1.12), (0, -0.48, 1.28), (0, -0.56, 1.42)], [0.18, 0.17, 0.15], 'skin2', n=14),
          'neck')
    # back ridge: a row of rounded scutes
    for k in range(8):
        t = k / 7
        p = V((0, -0.40 + 1.25 * t, 1.62 - 0.35 * t + 0.1 * math.sin(math.pi * t)))
        if k < 2:
            continue
        r.add(G.tube('x_scute%d' % k, [p + V((0, 0, -0.06)), p + V((0, 0.03, 0.07))], [0.07, 0.0], 'hair', n=10,
                     sub=2), 'hips')
    hc, hr = X_HC, X_HR
    r.add(G.ellipsoid('x_head', hc, hr, 'skin', seg=36, rings=18), 'head')

    def snout(u):
        if u.y < 0:
            u.x *= 1.0 + 0.15 * u.y
        return u
    r.add(G.ellipsoid('x_snout', (0, -0.86, 1.60), (0.27, 0.34, 0.19), 'skin', seg=32, rings=16, deform=snout), 'head')
    # the open mouth: a dark inside, a pink tongue, teeth top and bottom
    r.add(G.ellipsoid('x_mouth', (0, -0.80, 1.44), (0.22, 0.32, 0.11), 'dark', seg=28, rings=12), 'head')
    r.add(G.ellipsoid('x_tongue', (0, -0.74, 1.40), (0.14, 0.22, 0.06), 'x13', seg=24, rings=10), 'jaw')
    r.add(G.ellipsoid('x_jaw', (0, -0.74, 1.33), (0.23, 0.30, 0.115), 'skin2', seg=28, rings=12), 'jaw')
    for s in (-1, 1):
        for k, (x, y) in enumerate(((0.24, -0.66), (0.23, -0.80), (0.20, -0.93), (0.14, -1.04))):
            b0 = V((s * x, y, 1.50))
            r.add(G.tube('x_tt%d_%d' % (s, k), [b0, b0 + V((0, -0.01, -0.09))], [0.035, 0.0], 'white', n=8, sub=1),
                  'head')
            b1 = V((s * (x - 0.02), y + 0.04, 1.39))
            if k < 3:
                r.add(G.tube('x_bt%d_%d' % (s, k), [b1, b1 + V((0, -0.01, 0.07))], [0.03, 0.0], 'white', n=8, sub=1),
                      'jaw')
        r.add(G.ellipsoid('x_nos%d' % s, (s * 0.08, -1.13, 1.70), (0.032, 0.022, 0.022), 'dark', seg=10, rings=6),
              'head')
        eye(r, 'head', 'x_eye%d' % s, hc, hr, s * 40, 24, 0.10, pupil=(0.0, 0.0, 0.03, 1.8), lid=('skin', 0.2))
        brow = path_on(hc, hr, [(s * 16, 44), (s * 36, 50), (s * 56, 42)], 0.02)
        r.add(G.tube('x_brow%d' % s, brow, [0.042, 0.036, 0.0], 'dark', n=8, sub=3), 'head')
    # tiny arms
    for s, side in ((-1, 'R'), (1, 'L')):
        a0, el, hd = V((s * 0.30, -0.36, 1.10)), V((s * 0.36, -0.46, 1.00)), V((s * 0.34, -0.58, 1.02))
        r.add(G.tube('x_arm' + side, [a0, el, hd], [0.075, 0.06, 0.05], 'skin', n=12), 'arm_' + side)
        for k, dx in enumerate((-0.02, 0.02)):
            b0 = hd + V((dx, -0.03, 0.0))
            r.add(G.tube('x_claw%s%d' % (side, k), [b0, b0 + V((0, -0.04, -0.03))], [0.016, 0.0], 'white', n=6, sub=1),
                  'arm_' + side)
    # legs: huge thighs, shins, big feet with three claws
    for s, side in ((-1, 'R'), (1, 'L')):
        x = s * 0.30
        r.add(G.ellipsoid('x_thigh' + side, (s * 0.30, 0.08, 0.72), (0.19, 0.30, 0.30), 'skin', rot=(15, 0, 0)),
              'leg_' + side)
        r.add(G.tube('x_shin' + side, [(x, -0.02, 0.50), (x, 0.06, 0.30), (x, 0.12, 0.14)], [0.14, 0.11, 0.09], 'skin',
                     n=14), 'leg_' + side)

        def flat_bottom(u):
            if u.z < -0.3:
                u.z = -0.3 + (u.z + 0.3) * 0.25
            return u
        r.add(G.ellipsoid('x_foot' + side, (x, -0.06, 0.07), (0.15, 0.24, 0.075), 'skin', deform=flat_bottom),
              'leg_' + side)
        for k, dx in enumerate((-0.08, 0.0, 0.08)):
            b0 = V((x + dx, -0.27, 0.06))
            r.add(G.tube('x_toe%s%d' % (side, k), [b0, b0 + V((0, -0.06, -0.035))], [0.028, 0.0], 'white', n=8, sub=1),
                  'leg_' + side)
    # the tail, sweeping round to the side so it shows from the front
    r.add(G.tube('x_tail', [(0, 0.45, 1.05), (0.08, 0.80, 0.92), (0.32, 1.04, 0.74), (0.62, 1.10, 0.58)],
                 [0.30, 0.21, 0.12, 0.0], 'skin', n=20), 'tail')
    for k, (p, rad) in enumerate((((0.07, 0.76, 0.94), 0.22), ((0.25, 1.0, 0.78), 0.14))):
        r.add(G.ringband('x_tband%d' % k, p, (0.1 + 0.25 * k, 1, -0.25), rad, 0.07, 'c7', thick=0.01, ref=(1, 0, 0)),
              'tail')
    return r


def pose_trex(anim, f, n, d='s'):
    ph = TAU * f / n
    P = {'neck': (0, 0, 0), 'head': (0, 0, 0), 'jaw': (6, 0, 0), 'arm_R': (-20, 0, -8), 'arm_L': (-20, 0, 8),
         'leg_R': (0, 0, 0), 'leg_L': (0, 0, 0), 'tail': (0, 0, 0), 'hips': (0, 0, 0), 'hips@': (0, 0, 0)}
    if anim == 'run':
        # a heavy chasing run: long strides, the head low and forward, jaw open
        for side, off in (('R', 0.0), ('L', math.pi)):
            P['leg_' + side] = (30 * math.sin(ph + off), 0, 0)
        P.update({'hips': (6 + 3 * math.cos(2 * ph), 0, 3 * math.sin(ph)),
                  'hips@': (0, 0, -0.02 - 0.05 * abs(math.cos(ph))), 'neck': (-4, 0, 0),
                  'head': (4 + 4 * math.sin(2 * ph), 0, -3 * math.sin(ph)), 'jaw': (14 + 6 * math.sin(2 * ph), 0, 0),
                  'tail': (-6, 0, -9 * math.sin(ph)), 'arm_R': (-34 - 14 * math.sin(ph), 0, -8),
                  'arm_L': (-34 + 14 * math.sin(ph), 0, 8)})
        return P
    if anim == 'roar':
        # 00 breathes in, 01-02 the roar, jaw wide, 03 back
        P.update({'neck': ([-16, 2, 4, -6][f], 0, 0), 'head': ([-14, 6, 8, -4][f], 0, [0, -3, 3, 0][f]),
                  'jaw': ([3, 30, 34, 12][f], 0, 0), 'arm_R': ([-20, -44, -48, -30][f], 0, -12),
                  'arm_L': ([-20, -44, -48, -30][f], 0, 12), 'hips': ([-6, 2, 3, 0][f], 0, 0),
                  'tail': ([6, -4, -6, 0][f], 0, [0, 6, -6, 0][f]), 'leg_R': (-6, 0, 0), 'leg_L': (6, 0, 0)})
        return P
    if anim == 'stomp':
        # a foot slam: lift the right foot, raise it high, slam, recover
        P.update({'leg_R': ([-32, -48, 4, 0][f], 0, 0), 'leg_L': ([6, 8, -2, 0][f], 0, 0),
                  'hips': ([-6, -8, 8, 2][f], 0, [-4, -5, 2, 0][f]),
                  'hips@': (0, 0, [0.02, 0.04, -0.07, -0.02][f]), 'jaw': ([6, 10, 22, 10][f], 0, 0),
                  'arm_R': ([-30, -50, -10, -20][f], 0, -12), 'arm_L': ([-30, -50, -10, -20][f], 0, 12),
                  'neck': ([-6, -10, 8, 0][f], 0, 0), 'tail': ([4, 8, -10, 0][f], 0, 0)})
        return P
    return None


BUILD = {'raptor': build_raptor, 'trike': build_trike, 'ptero': build_ptero, 'compy': build_compy,
         'trex': build_trex}
POSE = {'raptor': pose_raptor, 'trike': pose_trike, 'ptero': pose_ptero, 'compy': pose_compy, 'trex': pose_trex}
EXTRA = {'ptero': {'fly_z_m': P_Z, 'note': 'fly and dive: the anchor is the ground below it'},
         'trex': {'footprint': [2, 2]}}


def palettes():
    return {who: {str(k): list(v) for k, v in sorted(p.items())} for who, p in PALETTES.items()}


def my_args():
    """--dirs / --frames / --cards-only, taken out before mh_common.args()."""
    import argparse
    i = sys.argv.index('--') + 1 if '--' in sys.argv else len(sys.argv)
    ap = argparse.ArgumentParser(allow_abbrev=False)
    ap.add_argument('--dirs', default='')
    ap.add_argument('--frames', default='')
    ap.add_argument('--anims', default='')
    ap.add_argument('--no-cards', action='store_true')
    b, rest = ap.parse_known_args(sys.argv[i:])
    sys.argv[i:] = rest
    return b


def main():
    """--out is a staging root: monsters go to <out>/monsters, the boss to
    <out>/bosses (monsters_dino_merge.py then merges them into assets/)."""
    b = my_args()
    a = C.args()
    root = os.path.abspath(a.out)
    for sub in ('monsters', 'bosses'):
        os.makedirs(os.path.join(root, sub), exist_ok=True)
    with open(os.path.join(root, 'palettes.json'), 'w') as fh:
        json.dump({'palettes': palettes(), 'ids': {w: {str(k): v for k, v in d.items()} for w, d in IDS.items()}},
                  fh, indent=1)
    t_all = time.time()
    count = 0
    for who, lst in ANIMS.items():
        if a.only and who not in a.only:
            continue
        boss = who in BOSSES
        out = os.path.join(root, 'bosses' if boss else 'monsters')
        C.reset('neutral', cpu=a.cpu)
        register_mats(PALETTES[who])
        rig = BUILD[who]()
        size = SIZE.get(who, 1.0)
        anchor = V((1.0, 1.0, 0.0)) if boss else C.cell(0, 0, 0)
        ids = {str(k): v for k, v in IDS[who].items()} if boss else sorted(IDS[who].keys())
        base = dict(height_m=HEIGHT[who], ids=ids)
        base.update({'boss': who} if boss else {'monster': who})
        base.update(EXTRA.get(who, {}))
        jobs = [(anim, d, f, n, ms) for anim, dirs, n, ms in lst for d in dirs for f in range(n)]
        if not b.no_cards:
            jobs.append(('card', 's', 0, 1, 0))
        for anim, d, f, n, ms in jobs:
            if b.anims and anim not in b.anims.split(',') and not (anim == 'card' and 'card' in b.anims):
                continue
            if b.dirs and d not in b.dirs.split(','):
                continue
            if b.frames and str(f) not in b.frames.split(','):
                continue
            card = anim == 'card'
            if card:
                ca, cf, cn = CARD[who]
                P = POSE[who](ca, cf, cn, 's')
            else:
                P = POSE[who](anim, f, n, d)
            rig.pose(P, YAW[d])
            rig.root.scale = (size, size, size)
            bpy.context.view_layer.update()
            t0 = time.time()
            if card:
                name = 'card_' + who
                extra = dict(base, pose='%s_s_%02d' % (ca, cf))
                if boss:
                    extra.update(anim=ca, dir='s', frame=cf)
                C.render_sprite(out, name, rig.visible(), anchor, passes=('light', 'id'), bounce_ground=0.0,
                                kind='card', samples=a.samples, zoom=1.2 if boss else 2.0, margin=4, extra=extra)
            else:
                name = '%s_%s_%s_%02d' % (who, anim, d, f)
                extra = dict(base, anim=anim, dir=d, frame=f, frames=n, ms=ms)
                C.render_sprite(out, name, rig.visible(), anchor, passes=('light', 'id', 'z', 'shadow'),
                                shadow_z=0.0, bounce_ground=0.0, kind='char', samples=a.samples, extra=extra,
                                margin=2)
            count += 1
            print('rendered %s in %.1f s' % (name, time.time() - t0), flush=True)
        C.save_meta(out)
    print('done: %d frames in %.1f s' % (count, time.time() - t_all))


main()
