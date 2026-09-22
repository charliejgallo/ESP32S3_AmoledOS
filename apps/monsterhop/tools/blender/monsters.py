"""Monster Hop - the zone monsters and their minions (SPEC.md section 5).

    cd apps/monsterhop/tools/blender
    /Applications/Blender.app/Contents/MacOS/Blender -b -P monsters.py -- --out ../../assets/monsters \
        [--sample] [--only zombie,bat,zombie_walk] [--dirs s,e] [--frames 0,3] [--samples 48]

--sample renders the style sample only (see SAMPLE below); without it, the
whole set. --only takes monster names or monster_anim pairs.

Every monster is built from primitives (monsters_geo.py) as rigid parts on a
small rig of empties, posed per frame, turned by the facing's yaw and
rendered under the neutral light with the light, id, z and shadow passes.
The watch recolours them per region id with the palettes in palettes.json.

Shared monster ids (SPEC 5): 1 skin/fur/wrap A, 2 skin/fur/wrap B, 3 dark,
4 white, 5 eye glow (emissive), 6 hair, 7-9 clothes A-C, 10 shoes,
11-12 accessories, 13 (here) tongue.
"""
import json
import math
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import mh_common as C  # noqa: E402
import monsters_geo as G  # noqa: E402
import monsters_armor as A  # noqa: E402
from monsters_geo import V, sph_dir, on_ell, track, rotm, TAU  # noqa: E402,F401
import bpy  # noqa: E402

# ---------------------------------------------------------------------------
# Palettes (sRGB 0-255). The default per monster, plus the zombie's clothes.
# ---------------------------------------------------------------------------

PALETTES = {
    'zombie': {1: (150, 190, 135), 2: (118, 150, 110), 3: (38, 30, 42), 4: (238, 232, 205),
               5: (205, 255, 80), 6: (92, 64, 46), 7: (120, 160, 210), 8: (200, 55, 60),
               9: (92, 84, 120), 10: (96, 66, 44), 12: (215, 180, 90)},
    'vampire': {1: (212, 196, 232), 2: (178, 150, 205), 3: (40, 22, 44), 4: (252, 250, 250), 5: (255, 48, 62),
                6: (44, 38, 70), 7: (52, 44, 82), 8: (205, 32, 56), 9: (128, 48, 96), 10: (40, 34, 48),
                11: (215, 36, 60), 12: (245, 205, 80), 13: (212, 196, 240)},
    'bat': {1: (132, 104, 168), 2: (72, 52, 96), 3: (34, 24, 40), 4: (252, 250, 250), 5: (255, 56, 60)},
    'mummy': {1: (232, 218, 180), 2: (188, 164, 124), 3: (52, 40, 34), 5: (70, 255, 225)},
    'werewolf': {1: (132, 112, 96), 2: (212, 192, 162), 3: (36, 28, 30), 4: (250, 246, 232), 5: (255, 222, 60),
                 6: (92, 76, 66), 7: (78, 108, 168)},
    'zombiedog': {1: (156, 182, 132), 2: (116, 138, 102), 3: (38, 30, 40), 4: (240, 236, 214), 5: (205, 255, 80),
                  11: (210, 48, 56), 12: (245, 205, 70), 13: (240, 124, 150)},
    'armor': dict(A.PALETTE),
    'scarab': {1: (36, 168, 150), 2: (236, 192, 62), 3: (30, 26, 38), 5: (255, 96, 40)},
    'crow': {1: (84, 88, 128), 2: (168, 174, 212), 3: (50, 44, 46), 5: (255, 72, 50)},
}
# the watch picks one per individual: it replaces these ids of the default
ZOMBIE_VARIANTS = [
    {'name': 'office', 6: (60, 44, 40), 7: (225, 228, 222), 8: (200, 45, 55), 9: (98, 104, 118),
     10: (60, 44, 38), 12: (120, 160, 210)},
    {'name': 'lumberjack', 6: (170, 90, 45), 7: (200, 60, 50), 8: (40, 40, 48), 9: (70, 95, 150),
     10: (120, 78, 45), 12: (230, 200, 120)},
    {'name': 'tourist', 6: (225, 200, 120), 7: (40, 175, 165), 8: (250, 205, 60), 9: (200, 170, 120),
     10: (225, 90, 70), 12: (240, 120, 150)},
]

IDS = {
    'zombie': {1: 'skin', 2: 'skin B (ears inside, lips)', 3: 'dark (sockets, mouth, stitches, pupils)',
               4: 'white (teeth)', 5: 'eye glow', 6: 'hair', 7: 'shirt', 8: 'tie', 9: 'trousers',
               10: 'the one shoe', 12: 'knee patch'},
    'vampire': {1: 'skin', 2: 'inner ears', 3: 'dark (mouth, pupils)', 4: 'white (fangs, shirt front)',
                5: 'eye glow', 6: 'hair and brows (glossy)', 7: 'cape outside, collar outside, trousers',
                8: 'cape lining, collar inside (red)', 9: 'waistcoat', 10: 'shoes', 11: 'bow tie',
                12: 'buttons', 13: 'smoke puff (transform)'},
    'bat': {1: 'fur, belly, snout, wing bones', 2: 'wing membrane, inner ears', 3: 'dark (mouth, feet)',
            4: 'fangs', 5: 'eye glow'},
    'mummy': {1: 'wraps', 2: 'darker wraps', 3: 'dark (eye socket, slit, mouth)', 5: 'eye glow'},
    'werewolf': {1: 'fur', 2: 'muzzle, belly, ruff, inner ears, tail tip', 3: 'dark (nose, mouth, pupils)',
                 4: 'white (fangs, claws)', 5: 'eye glow', 6: 'brows and head tufts', 7: 'torn shorts'},
    'zombiedog': {1: 'fur', 2: 'fur patches, muzzle, floppy ear', 3: 'dark (nose, mouth, stitches, pupils)',
                  4: 'tooth', 5: 'eye glow', 11: 'collar', 12: 'tag', 13: 'tongue'},
    'armor': dict(A.IDS),
    'scarab': {1: 'shell', 2: 'shell stripes, face rake', 3: 'dark (legs, seam, antennae)', 5: 'eye glow'},
    'crow': {1: 'feathers', 2: 'lighter feather tips, chest scruff', 3: 'dark (beak, legs)', 5: 'eye glow'},
}

YAW = {'s': 0.0, 'e': 90.0, 'n': 180.0, 'w': -90.0}

# anim, dirs, frames, ms per frame
ANIMS = {
    'zombie': [('walk', 'nesw', 6, 100), ('idle', 'nesw', 2, 400), ('notice', 'nesw', 2, 150),
               ('lunge', 'nesw', 3, 80)],
    'vampire': [('glide', 'nesw', 6, 90), ('idle', 'nesw', 2, 400), ('transform', 's', 5, 70)],
    'bat': [('fly', 'nesw', 4, 60)],
    'mummy': [('walk', 'nesw', 6, 110), ('idle', 'nesw', 2, 400), ('push', 'nesw', 4, 120)],
    'werewolf': [('idle', 'nesw', 2, 400), ('howl', 'nesw', 5, 120), ('run', 'nesw', 6, 50),
                 ('stun', 'nesw', 4, 150)],
    'zombiedog': [('idle', 'nesw', 2, 400), ('run', 'nesw', 4, 50)],
    'armor': [('idle', 'nesw', 2, 400), ('walk', 'nesw', 6, 110)],
    'scarab': [('crawl', 'nesw', 4, 40)],
    'crow': [('perch', 'nesw', 2, 300), ('fly', 'nesw', 4, 60), ('dive', 'nesw', 3, 60)],
}
HEIGHT = {'zombie': 1.10, 'vampire': 1.15, 'bat': 0.35, 'mummy': 1.10, 'werewolf': 1.20,
          'zombiedog': 0.50, 'armor': 1.20, 'scarab': 0.40, 'crow': 0.40}

# the album cards (SPEC 12): the idle, or first, pose facing s at zoom 2
CARD = {'zombie': ('idle', 0), 'zombiedog': ('idle', 0), 'vampire': ('idle', 0), 'bat': ('fly', 0),
        'mummy': ('idle', 0), 'scarab': ('crawl', 0), 'werewolf': ('idle', 0), 'crow': ('perch', 0),
        'armor': ('idle', 0)}

# the style sample: (monster, anim, dirs, frames)
SAMPLE = [('zombie', 'idle', 's', [0]), ('zombie', 'walk', 'e', [0, 1, 2, 3, 4, 5]),
          ('vampire', 'idle', 's', [0]), ('mummy', 'idle', 's', [0]), ('werewolf', 'idle', 's', [0]),
          ('zombiedog', 'idle', 's', [0]), ('armor', 'idle', 's', [0]), ('scarab', 'crawl', 's', [0]),
          ('crow', 'perch', 's', [0]), ('bat', 'fly', 's', [0])]


# ---------------------------------------------------------------------------
# Materials
# ---------------------------------------------------------------------------

# key: (id, roughness, metallic, specular)
MATS = {
    'skin': (1, 0.55, 0.0, 0.40), 'skin2': (2, 0.60, 0.0, 0.40), 'dark': (3, 0.45, 0.0, 0.50),
    'white': (4, 0.25, 0.0, 0.60), 'hair': (6, 0.60, 0.0, 0.40), 'c7': (7, 0.85, 0.0, 0.30),
    'c8': (8, 0.80, 0.0, 0.30), 'c9': (9, 0.85, 0.0, 0.30), 'shoe': (10, 0.40, 0.0, 0.50),
    'a11': (11, 0.50, 0.0, 0.50), 'a12': (12, 0.60, 0.0, 0.40), 'x13': (13, 0.35, 0.0, 0.50),
    'hairgloss': (6, 0.22, 0.0, 0.75),
}


# per monster tweaks of the shared keys (e.g. a glossy beetle shell)
EXTRA_MATS = {
    'scarab': {'skin': dict(id=1, rough=0.18, spec=0.9), 'skin2': dict(id=2, rough=0.22, spec=0.9, metal=0.3)},
}


def register_mats(pal, extra=None):
    for key, (id_, rough, metal, spec) in MATS.items():
        C.mat(key, base=G.srgb_to_lin(pal.get(id_, (200, 200, 200))), id=id_, rough=rough, metal=metal,
              spec=spec)
    C.mat('glow', id=5, build=G.glow_build(pal.get(5, (255, 255, 255))))
    for key, kw in (extra or {}).items():
        C.mat(key, **kw)


# ---------------------------------------------------------------------------
# Small shared bits
# ---------------------------------------------------------------------------

def eye(rig, joint, name, c, r, az, el, rad, key='glow', flat=0.55, sink=0.35, socket=None, pupil=None,
        rot=0.0, squash=1.0, lid=None, out=0.0):
    """A glowing eye on the head ellipsoid (c, r): a flattened ball pushed into
    the surface (squash < 1 makes it an oval, rot tilts it about the normal).
    socket: a dark ring (its scale); pupil: (dx, dy, size[, stretch]) a dark
    dot; lid: (key, cover) an eyelid of `key` over the top `cover` fraction.
    Returns the eye's centre and normal."""
    p, n = on_ell(c, r, az, el, out)
    R = track(n) @ rotm((0, 0, rot))
    ctr = p - n * (rad * flat * sink)
    rig.add(G.ellipsoid(name, ctr, (rad, rad * squash, rad * flat), key, rot=R, seg=20, rings=10), joint)
    if socket:
        s = socket
        rig.add(G.ellipsoid(name + '_sock', p - n * (rad * 0.25), (rad * s, rad * squash * s * 0.95, rad * 0.35),
                            'dark', rot=R, seg=20, rings=8), joint)
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


def path_on(c, r, pts, out=0.004):
    """Points on the ellipsoid (c, r) at the given (az, el) pairs."""
    return [on_ell(c, r, az, el, out)[0] for az, el in pts]


def stitches(rig, joint, name, c, r, pts, ticks=3, rad=0.011, tick_len=0.028, key='dark'):
    line = path_on(c, r, pts, 0.002)
    rig.add(G.tube(name, line, [rad] * len(line), key, n=6, sub=3), joint)
    for k in range(ticks):
        t = (k + 0.5) / ticks
        f = t * (len(line) - 1)
        i = min(int(f), len(line) - 2)
        a, b = line[i], line[i + 1]
        p = a.lerp(b, f - i)
        n = (p - V(c)).normalized()
        tan = (b - a).normalized()
        perp = n.cross(tan).normalized()
        rig.add(G.tube('%s_t%d' % (name, k), [p - perp * tick_len, p + perp * tick_len], [rad * 0.85] * 2, key,
                       n=6, sub=1), joint)


def lerp(a, b, t):
    return a + (b - a) * t


def interp_profile(prof, z):
    """Radius of a (r, z) profile at height z (linear)."""
    for (r0, z0), (r1, z1) in zip(prof[:-1], prof[1:]):
        if z0 <= z <= z1:
            return lerp(r0, r1, (z - z0) / max(z1 - z0, 1e-9))
    return prof[0][0] if z < prof[0][1] else prof[-1][0]


def tri(x):
    """Triangle wave 0..1..0 with period 1."""
    x = x - math.floor(x)
    return 1.0 - abs(2.0 * x - 1.0)


# ---------------------------------------------------------------------------
# Zombie
# ---------------------------------------------------------------------------

Z_TORSO = [(0.0, 0.23), (0.12, 0.24), (0.160, 0.29), (0.172, 0.37), (0.166, 0.45), (0.152, 0.52),
           (0.105, 0.585), (0.055, 0.615), (0.0, 0.625)]
Z_SY = 0.78


def build_zombie():
    r = G.Rig('zombie')
    r.joint('hips', 'root', (0, 0, 0.28))
    r.joint('spine', 'hips', (0, 0, 0.31))
    r.joint('head', 'spine', (0, 0.0, 0.59))
    r.joint('arm_R', 'spine', (-0.165, 0, 0.53))
    r.joint('arm_L', 'spine', (0.165, 0, 0.53))
    r.joint('hand_R', 'arm_R', (-0.232, 0, 0.31))
    r.joint('hand_L', 'arm_L', (0.232, 0, 0.31))
    r.joint('leg_R', 'hips', (-0.085, 0, 0.27))
    r.joint('leg_L', 'hips', (0.085, 0, 0.27))
    r.joint('foot_R', 'leg_R', (-0.088, 0, 0.085))
    r.joint('foot_L', 'leg_L', (0.088, 0, 0.085))

    # torso (skin shows through the torn hem) and the shirt over it
    r.add(G.lathe('z_torso', Z_TORSO, 'skin', sy=Z_SY, seg=32), 'spine')

    def hem(th, i):
        if i != 0:
            return 0.0
        # torn hem: irregular teeth, a deeper rip at the front-left
        k = tri(th * 7 / TAU + 0.13) * 0.046 + tri(th * 11 / TAU) * 0.012
        front = math.exp(-((math.atan2(math.sin(th + 1.25), math.cos(th + 1.25))) ** 2) / 0.05)
        return k + 0.045 * front
    shirt = [(0.176, 0.325), (0.183, 0.37), (0.178, 0.45), (0.163, 0.52), (0.118, 0.583), (0.080, 0.603)]
    r.add(G.lathe('z_shirt', shirt, 'c7', sy=Z_SY, seg=56, zfn=hem, cap_bot=False, cap_top=False,
                  thick=0.016), 'spine')
    # collar points
    for s in (-1, 1):
        r.add(G.ellipsoid('z_collar%d' % s, (s * 0.045, -0.105, 0.593), (0.042, 0.018, 0.028), 'c7',
                          rot=(-50, s * 25, s * -30), seg=12, rings=6), 'spine')

    # the tie, lying on the shirt, a bit askew
    def shirt_front(z, x=0.0):
        rr = interp_profile(shirt, z) + 0.016
        a = rr
        b = rr * Z_SY
        # y on the ellipse at this x
        yy = b * math.sqrt(max(0.0, 1 - (x / a) ** 2))
        return -yy - 0.006
    r.add(G.ellipsoid('z_knot', (0.004, shirt_front(0.568) - 0.008, 0.568), (0.026, 0.02, 0.026), 'c8',
                      seg=12, rings=8), 'spine')

    def tie(u, v):
        z = 0.555 - 0.20 * v
        x = 0.004 + 0.045 * v * v
        w = (0.020 + 0.030 * v) * (1.0 if v < 0.80 else max(0.0, (1.0 - v) / 0.20))
        xx = x + (u * 2 - 1) * w
        return (xx, shirt_front(z, xx) - 0.004, z - (0.02 * max(0.0, v - 0.8) / 0.2) * (1 - abs(u * 2 - 1)))
    r.add(G.surface('z_tie', tie, 5, 12, 'c8', thick=0.012, offset=1.0), 'spine')

    # trousers
    pants = [(0.0, 0.19), (0.13, 0.195), (0.168, 0.24), (0.176, 0.29), (0.172, 0.335), (0.0, 0.34)]
    r.add(G.lathe('z_pants', pants, 'c9', sy=Z_SY * 1.02, seg=32), 'hips')
    for s, side in ((-1, 'R'), (1, 'L')):
        leg = 'leg_' + side
        r.add(G.tube('z_leg' + side, [(s * 0.085, 0, 0.27), (s * 0.088, 0, 0.17), (s * 0.090, 0, 0.105)],
                     [0.066, 0.068, 0.074], 'c9', n=14, cap0=1.0, cap1=0.0), leg)
        r.add(G.tube('z_ankle' + side, [(s * 0.088, 0, 0.13), (s * 0.088, 0, 0.06)], [0.036, 0.034], 'skin',
                     n=10), leg)
    # knee patch on the left leg
    r.add(G.box('z_patch', (0.092, -0.068, 0.175), (0.07, 0.012, 0.062), 'a12', rot=(0, 0, 10), bevel=0.004),
          'leg_L')

    # feet: the left one in a shoe, the right one bare
    def flat_bottom(u):
        if u.z < -0.35:
            u.z = -0.35 + (u.z + 0.35) * 0.25
        return u
    r.add(G.ellipsoid('z_shoe', (0.090, -0.035, 0.047), (0.066, 0.108, 0.052), 'shoe', deform=flat_bottom),
          'foot_L')
    r.add(G.ellipsoid('z_sole', (0.090, -0.035, 0.018), (0.070, 0.112, 0.018), 'dark', seg=20, rings=8),
          'foot_L')
    r.add(G.ellipsoid('z_foot', (-0.088, -0.028, 0.040), (0.056, 0.090, 0.042), 'skin', deform=flat_bottom),
          'foot_R')
    for k, dx in enumerate((-0.030, -0.004, 0.022)):
        r.add(G.ellipsoid('z_toe%d' % k, (-0.088 + dx, -0.108 + abs(dx) * 0.3, 0.028), (0.019, 0.022, 0.020),
                          'skin', seg=12, rings=6), 'foot_R')

    # arms: torn short sleeve, skinny grey arm, mitten hand
    for s, side in ((-1, 'R'), (1, 'L')):
        arm = 'arm_' + side

        def slv_hem(th, i, s=s):
            return 0.0
        r.add(G.tube('z_sleeve' + side, [(s * 0.155, 0, 0.545), (s * 0.200, 0, 0.500), (s * 0.218, 0, 0.455)],
                     [0.066, 0.066, 0.062], 'c7', n=16, cap0=1.0, cap1=0.0), arm)
        r.add(G.tube('z_arm' + side, [(s * 0.190, 0, 0.50), (s * 0.222, 0, 0.40), (s * 0.232, 0, 0.325)],
                     [0.043, 0.041, 0.038], 'skin', n=12), arm)
        hand = 'hand_' + side
        r.add(G.ellipsoid('z_hand' + side, (s * 0.234, -0.004, 0.268), (0.040, 0.050, 0.062), 'skin'), hand)
        r.add(G.ellipsoid('z_thumb' + side, (s * 0.214, -0.040, 0.285), (0.018, 0.022, 0.030), 'skin',
                          rot=(20, 0, 0), seg=12, rings=6), hand)

    # head
    hc, hr = V((0.0, -0.01, 0.845)), (0.250, 0.228, 0.242)
    r.add(G.ellipsoid('z_head', hc, hr, 'skin', seg=36, rings=18), 'head')
    for s in (-1, 1):
        p, n = on_ell(hc, hr, s * 90, 2, -0.01)
        r.add(G.ellipsoid('z_ear%d' % s, p, (0.028, 0.046, 0.060), 'skin', rot=(0, 0, s * 10)), 'head')
        r.add(G.ellipsoid('z_earin%d' % s, p + V((s * 0.012, -0.004, 0)), (0.012, 0.028, 0.036), 'skin2',
                          rot=(0, 0, s * 10), seg=12, rings=6), 'head')
    # eyes: a big one and a small one, sockets, pupils looking two ways
    eye(r, 'head', 'z_eyeL', hc, hr, 27, 24, 0.084, socket=1.25, pupil=(0.16, -0.10, 0.024))
    eye(r, 'head', 'z_eyeR', hc, hr, -27, 20, 0.064, socket=1.32, pupil=(-0.22, 0.14, 0.020))
    # mouth: a crooked open groan with one tooth
    p, n = on_ell(hc, hr, 5, -6)
    Rm = track(n) @ rotm((0, 0, -9))
    r.add(G.ellipsoid('z_mouth', p - n * 0.012, (0.078, 0.032, 0.03), 'dark', rot=Rm, seg=20, rings=8), 'head')
    r.add(G.box('z_tooth', p + Rm @ V((0.022, 0.020, 0.004)), (0.026, 0.028, 0.02), 'white', rot=Rm,
                bevel=0.004), 'head')
    r.add(G.ellipsoid('z_lip', p + Rm @ V((0.0, -0.030, 0.004)), (0.060, 0.016, 0.018), 'skin2', rot=Rm,
                      seg=16, rings=6), 'head')
    # stitches across the forehead
    stitches(r, 'head', 'z_stitch', hc, hr, [(-58, 44), (-66, 30), (-70, 14)], ticks=3, rad=0.012, tick_len=0.03)

    # messy hair: a cap with a ragged hairline and tufts
    hr2 = (hr[0] * 1.035, hr[1] * 1.035, hr[2] * 1.035)

    def hairline(az):
        a = math.radians(az)
        base = 30 + 30 * math.cos(a)          # high at the front: the face shows
        return base - 8 * tri(az / 30.0 + 0.2)

    def cap(u, v):
        az = u * 360.0
        el0 = hairline(az)
        el = el0 + (90.0 - el0) * (v ** 0.9)
        return on_ell(hc, hr2, az, el)[0]
    r.add(G.surface('z_hair', cap, 48, 10, 'hair', cyc_u=True, thick=0.022, offset=-1.0), 'head')
    # (az, el, bend direction, radius, length): a few spikes up, clumps flopping
    tufts = [(10, 70, (0.05, -0.09, 0.02), 0.052, 1.0), (50, 64, (0.10, -0.02, 0.04), 0.046, 0.9),
             (-45, 66, (-0.09, -0.05, 0.05), 0.048, 1.0), (-8, 86, (0.02, 0.02, 0.12), 0.050, 1.1),
             (130, 48, (0.08, 0.07, -0.02), 0.046, 0.8), (-125, 50, (-0.08, 0.06, -0.01), 0.044, 0.8),
             (180, 64, (0.0, 0.08, 0.05), 0.046, 0.9), (85, 42, (0.09, 0.0, -0.03), 0.040, 0.7)]
    for k, (az, el, d, rad, ln) in enumerate(tufts):
        p, n = on_ell(hc, hr2, az, el, -0.012)
        d = V(d) * ln
        q1 = p + n * 0.030 * ln + d * 0.45
        q2 = p + n * 0.036 * ln + d
        r.add(G.tube('z_tuft%d' % k, [p, q1, q2], [rad, rad * 0.72, 0.0], 'hair', n=10, sub=4), 'head')
    return r


def pose_zombie(anim, f, n, d='s'):
    ph = TAU * f / n
    if anim == 'idle':
        b = math.sin(ph)
        return {'spine': (7 + 2 * b, 0, 0), 'spine@': (0, 0, -0.006 * (1 - b)),
                'head': (-9 - 3 * b, 11, 4), 'arm_R': (-88 + 4 * b, 0, -22), 'arm_L': (-96 - 3 * b, 0, 20),
                'hand_R': (34, 0, 0), 'hand_L': (28, 0, 0)}
    if anim == 'walk':
        s = math.sin(ph)
        c = math.cos(ph)
        lift = max(0.0, -s)
        # a lurching shuffle: the body rolls onto each step, the bare right
        # foot drags toes-down behind, the shod one plants flat
        return {'spine': (11, 8 * s, -5 * s), 'spine@': (0, 0, -0.016 * abs(c)),
                'hips': (0, 5 * s, 6 * s),
                'head': (-8 + 4 * abs(s), 12 - 7 * s, 4 + 3 * c),
                'leg_R': (26 * s, 0, 0), 'leg_L': (-24 * s, 0, 0),
                'foot_R': (-16 * max(0, s) + 6 * max(0, -s), 0, 0), 'foot_L': (-8 * lift, 0, 0),
                'arm_R': (-88 + 5 * c, 0, -18), 'arm_L': (-96 - 5 * c, 0, 16),
                'hand_R': (34, 0, 0), 'hand_L': (28, 0, 0)}
    if anim == 'notice':
        # startled: arms fly up, the lolling head snaps straight to where he faces
        k = f % 2
        return {'spine': (-6 + 4 * k, 0, 0), 'spine@': (0, 0, 0.012 - 0.008 * k),
                'head': (-16 + 5 * k, [-4, 2][k], 0),
                # thrown up in a V, out to the sides (straight up they vanish into the big head)
                'arm_R': (-40 + 6 * k, 128 - 12 * k, 0), 'arm_L': (-44 + 6 * k, -132 + 12 * k, 0),
                'hand_R': (-20, 0, 0), 'hand_L': (-24, 0, 0),
                'leg_R': (-4, 0, -3), 'leg_L': (4, 0, 3)}
    if anim == 'lunge':
        # 00 wind-up (rears back, arms high), 01 lurch, 02 full reach; the feet stay put
        sp = [-8, 26, 36][f]
        return {'spine': (sp, 0, [0, -4, -6][f]), 'spine@': (0, [0.01, -0.03, -0.06][f], [0.01, -0.02, -0.035][f]),
                'head': ([-4, -20, -28][f], [8, 3, 0][f], 0),
                'arm_R': ([-132, -92, -84][f] - sp * 0.0, 0, [-16, -10, -8][f]),
                'arm_L': ([-138, -96, -88][f], 0, [16, 10, 8][f]),
                'hand_R': ([-10, 20, 8][f], 0, 0), 'hand_L': ([-14, 16, 4][f], 0, 0),
                'leg_R': ([-6, -26, -30][f], 0, 0), 'leg_L': ([8, 20, 26][f], 0, 0),
                'foot_R': (0, 0, 0), 'foot_L': ([0, -12, -18][f], 0, 0)}
    return None


# ---------------------------------------------------------------------------
# Vampire
# ---------------------------------------------------------------------------

VA_TORSO = [(0.0, 0.22), (0.110, 0.23), (0.140, 0.28), (0.148, 0.36), (0.145, 0.44), (0.135, 0.51),
            (0.095, 0.57), (0.050, 0.598), (0.0, 0.605)]
VA_SY = 0.80
VA_HC, VA_HR = V((0.0, -0.005, 0.875)), (0.245, 0.225, 0.245)


def front_y(prof, sy, z, x=0.0, out=0.0):
    rr = G.profile_r(prof, z) + out
    return -rr * sy * math.sqrt(max(0.0, 1 - (x / max(rr, 1e-6)) ** 2))


def pw(table, x):
    """Piecewise linear table [(x, y)]."""
    if x <= table[0][0]:
        return table[0][1]
    for (x0, y0), (x1, y1) in zip(table[:-1], table[1:]):
        if x <= x1:
            return y0 + (y1 - y0) * (x - x0) / (x1 - x0)
    return table[-1][1]


def vamp_cape(name, key, inner, trail=0.0, sway=0.0, flare=0.0, open_=40.0, wrap=0.0, hem=0.13):
    """The cape: an open cone from the shoulders to the hem, the front open by
    2 * open_ degrees; `inner` is the red lining (a hair smaller). trail pushes
    the lower part back (+Y), sway sideways; wrap (0..1) closes it round the
    body (transform)."""
    op = open_ * (1.0 - wrap) + (2.5 if inner else 0.0)
    th0 = math.radians(-90 + op)
    th1 = math.radians(270 - op)

    def fn(u, v):
        th = th0 + (th1 - th0) * u
        z = 0.600 - v * (0.600 - hem)
        z += 0.034 * tri(u * 7.0 + 0.5) * v ** 6
        rr = 0.100 + (0.235 + flare) * v ** 0.70
        rr *= 1.0 + 0.055 * v * math.sin(u * TAU * 4.5)
        rr *= 1.0 - 0.25 * wrap * v
        rr += -0.010 if inner else 0.006
        if inner:
            z += 0.012
        x = rr * math.cos(th) + sway * v * v
        y = rr * 0.86 * math.sin(th) + trail * v * v
        return (x, y, z)
    return G.surface(name, fn, 44, 12, key, thick=0.006, offset=0.0)


def vamp_collar(name, key, inner):
    th0 = math.radians(-90 + 52)
    th1 = math.radians(270 - 52)

    def fn(u, v):
        th = th0 + (th1 - th0) * u
        pk = math.exp(-((u - 0.20) / 0.075) ** 2) + math.exp(-((u - 0.80) / 0.075) ** 2)
        H = 0.10 + 0.17 * math.sin(math.pi * u) ** 0.5 + 0.13 * pk
        z = 0.575 + H * v
        rr = 0.100 + 0.25 * v ** 1.25 - (0.008 if inner else 0.0)
        if inner:
            z -= 0.006 * v
        return (rr * math.cos(th), rr * 0.88 * math.sin(th) + 0.035 * v, z)
    return G.surface(name, fn, 48, 7, key, thick=0.006, offset=0.0)


def build_vampire():
    r = G.Rig('vampire')
    r.joint('float', 'root', (0, 0, 0.0))
    r.joint('hips', 'float', (0, 0, 0.28))
    r.joint('spine', 'hips', (0, 0, 0.31))
    r.joint('head', 'spine', (0, 0, 0.60))
    r.joint('cape', 'spine', (0, 0, 0.60))
    r.joint('arm_R', 'spine', (-0.14, 0, 0.53))
    r.joint('arm_L', 'spine', (0.14, 0, 0.53))
    r.joint('leg_R', 'hips', (-0.07, 0, 0.25))
    r.joint('leg_L', 'hips', (0.07, 0, 0.25))

    # waistcoat body, white shirt V, bow tie, gold buttons
    r.add(G.lathe('v_torso', VA_TORSO, 'c9', sy=VA_SY, seg=32), 'spine')

    def shirt(u, v):
        z = 0.598 - 0.13 * v
        w = 0.080 * (1.0 - v) + 0.004
        x = (u * 2 - 1) * w
        return (x, front_y(VA_TORSO, VA_SY, z, x, 0.004), z)
    r.add(G.surface('v_shirt', shirt, 5, 8, 'white', thick=0.006), 'spine')
    fy = front_y(VA_TORSO, VA_SY, 0.575, 0.0, 0.012)
    for s in (-1, 1):
        r.add(G.ellipsoid('v_bow%d' % s, (s * 0.034, fy - 0.004, 0.573), (0.036, 0.016, 0.024), 'a11',
                          rot=(0, s * 12, 0), seg=14, rings=8), 'spine')
    r.add(G.ellipsoid('v_knot', (0, fy - 0.012, 0.573), (0.015, 0.012, 0.016), 'a11', seg=10, rings=6), 'spine')
    for k, z in enumerate((0.44, 0.385, 0.33)):
        r.add(G.ellipsoid('v_btn%d' % k, (0, front_y(VA_TORSO, VA_SY, z, 0, 0.004), z), (0.013, 0.008, 0.013),
                          'a12', seg=10, rings=6), 'spine')

    # trousers, legs, pointy shoes
    pants = [(0.0, 0.18), (0.110, 0.185), (0.138, 0.23), (0.146, 0.28), (0.0, 0.29)]
    r.add(G.lathe('v_pants', pants, 'c7', sy=VA_SY, seg=28), 'hips')
    for s, side in ((-1, 'R'), (1, 'L')):
        leg = 'leg_' + side
        r.add(G.tube('v_leg' + side, [(s * 0.07, 0, 0.25), (s * 0.072, 0, 0.14), (s * 0.074, 0.0, 0.07)],
                     [0.048, 0.044, 0.040], 'c7', n=12), leg)
        r.add(G.tube('v_shoe' + side, [(s * 0.074, 0.03, 0.045), (s * 0.075, -0.03, 0.035),
                                       (s * 0.076, -0.10, 0.040), (s * 0.077, -0.14, 0.065)],
                     [(0.046, 0.040), (0.044, 0.036), (0.026, 0.020), 0.0], 'shoe', n=12, nrm=(1, 0, 0)), leg)

    # the cape (outer and red lining) and the high collar
    r.add(vamp_cape('v_cape', 'c7', False), 'cape', tag='cape')
    r.add(vamp_cape('v_lining', 'c8', True), 'cape', tag='cape')
    r.add(vamp_collar('v_collar', 'c7', False), 'spine', tag='collar')
    r.add(vamp_collar('v_collarin', 'c8', True), 'spine', tag='collar')

    # hands holding the cape's front edges
    for s, side in ((-1, 'R'), (1, 'L')):
        r.add(G.ellipsoid('v_hand' + side, (s * 0.146, -0.166, 0.392), (0.036, 0.034, 0.044), 'skin', seg=14,
                          rings=8), 'arm_' + side, tag='hands')
    # the cartoon smoke puff he vanishes into (transform 03-04)
    r.joint('puff', 'root', (0, 0, 0.50))
    for k, (dx, dy, dz, rad) in enumerate(((0, 0, 0, 0.16), (-0.14, 0.02, -0.06, 0.12), (0.15, -0.01, -0.05, 0.12),
                                          (-0.07, -0.08, 0.10, 0.11), (0.08, -0.06, 0.11, 0.11),
                                          (0.0, 0.06, 0.16, 0.10), (-0.16, 0.06, 0.08, 0.08),
                                          (0.17, 0.05, 0.07, 0.08), (0.0, -0.12, -0.08, 0.10))):
        r.add(G.ellipsoid('v_puff%d' % k, (dx, dy, 0.50 + dz), (rad, rad, rad * 0.9), 'x13', seg=18, rings=10),
              'puff', tag='puff')

    # head: slicked hair with a widow's peak, pointy ears, sly red eyes, fangs
    hc, hr = VA_HC, VA_HR
    r.add(G.ellipsoid('v_head', hc, hr, 'skin', seg=36, rings=18), 'head')
    hr2 = (hr[0] * 1.04, hr[1] * 1.04, hr[2] * 1.04)
    HL = [(0, 36), (20, 58), (50, 50), (92, 18), (135, 0), (180, -10)]

    def cap(u, v):
        az = u * 360.0
        a = abs(((az + 180.0) % 360.0) - 180.0)
        el0 = pw(HL, a)
        el = el0 + (90.0 - el0) * v
        p = on_ell(hc, hr2, az, el)[0]
        # slicked back: a little extra volume at the back of the crown
        back = max(0.0, math.cos(math.radians(az - 180))) * math.sin(math.radians(el)) * 0.02
        return p + V((0, back, back * 0.5))
    r.add(G.surface('v_hair', cap, 60, 12, 'hairgloss', cyc_u=True, thick=0.016, offset=-1.0), 'head')
    for s in (-1, 1):
        p, n = on_ell(hc, hr, s * 86, 10, -0.03)
        r.add(G.tube('v_ear%d' % s, [p, p + V((s * 0.07, 0.025, 0.035)), p + V((s * 0.15, 0.06, 0.11))],
                     [(0.058, 0.020), (0.040, 0.016), 0.0], 'skin', n=12, nrm=(0, 0, 1)), 'head')
        r.add(G.tube('v_earin%d' % s, [p + V((s * 0.03, -0.012, 0.0)), p + V((s * 0.075, 0.012, 0.04)),
                                        p + V((s * 0.12, 0.04, 0.085))],
                     [(0.032, 0.008), (0.022, 0.007), 0.0], 'skin2', n=10, nrm=(0, 0, 1)), 'head')
    for s in (-1, 1):
        eye(r, 'head', 'v_eye%d' % s, hc, hr, s * 25, 21, 0.066, squash=0.80, rot=s * 14,
            pupil=(0.0, -0.05, 0.016, 2.2), lid=('skin', 0.30))
        brow = path_on(hc, hr, [(s * 9, 32), (s * 22, 38), (s * 37, 37)], 0.006)
        r.add(G.tube('v_brow%d' % s, brow, [0.012, 0.015, 0.008], 'hairgloss', n=8, sub=3), 'head')
    mouth = path_on(hc, hr, [(-15, -7), (-4, -10), (8, -10), (19, -5)], 0.002)
    r.add(G.tube('v_mouth', mouth, [0.010, 0.012, 0.012, 0.009], 'dark', n=8, sub=3), 'head')
    for s in (-1, 1):
        p, n = on_ell(hc, hr, s * 7 + 1, -10, 0.003)
        r.add(G.tube('v_fang%d' % s, [p, p + V((0, 0, -0.030)) + n * 0.006, p + V((0, 0, -0.052)) + n * 0.004],
                     [0.017, 0.010, 0.0], 'white', n=8, sub=2), 'head')
    return r


def vamp_reshape(rig, cape):
    """Rebuild the cape (outside and lining) with this frame's parameters."""
    outer, lining = rig.tags['cape']
    rig.reshape(outer, vamp_cape('v_tmp', 'c7', False, **cape), 'cape')
    rig.reshape(lining, vamp_cape('v_tmp2', 'c8', True, **cape), 'cape')


def pose_vampire(anim, f, n, d='s'):
    ph = TAU * f / n
    if anim == 'idle':
        b = math.sin(ph)
        return {'float@': (0, 0, 0.085 + 0.018 * b), 'spine': (0, 0, 0), 'head': (-6 - 2 * b, 0, 0),
                'leg_R': (8, 0, 0), 'leg_L': (-4, 0, 0), '_cape': dict(trail=0.01 * b), '_hide': ('puff',)}
    if anim == 'glide':
        # hovering forward, the cape streaming back and swaying, toes trailing
        b = math.sin(ph)
        c = math.cos(ph)
        return {'float@': (0, 0, 0.10 + 0.022 * b), 'spine': (9, 0, 3 * c), 'head': (-12 - 2 * b, 0, -3 * c),
                'leg_R': (26 + 6 * c, 0, 0), 'leg_L': (18 - 6 * c, 0, 0),
                '_cape': dict(trail=0.085 + 0.030 * c, sway=0.030 * b, flare=0.015 + 0.012 * c),
                '_hide': ('puff',)}
    if anim == 'transform':
        # s only: sweeps the cape round himself, shrinks, and pops into a puff
        wrap = [0.35, 0.75, 1.0, 1.0, 1.0][f]
        sc = [1.0, 0.92, 0.74, 0.46, 0.18][f]
        puff = [0.0, 0.0, 0.0, 0.62, 1.0][f]
        rise = [0, 0.01, 0.03, 0.04, 0.05][f]
        # shrinking about his middle (0.5 m up), the puff centred on him
        P = {'float*': sc, 'float@': (0, 0, 0.085 + 0.50 * (1 - sc) + rise), 'puff@': (0, 0, 0.09 + rise),
             'root': (0, 0, 0), 'head': (-8, 0, [0, 12, -10, 0, 0][f]),
             'arm_R': (-40, 0, -10), 'leg_R': (10, 0, 0), 'leg_L': (4, 0, 0),
             '_cape': dict(wrap=wrap, flare=0.03 * (1 - wrap), trail=0.02 * f)}
        hide = ['hands'] if f > 0 else []
        if puff > 0:
            P['puff*'] = puff
        else:
            hide.append('puff')
        P['_hide'] = tuple(hide)
        return P
    return None


# ---------------------------------------------------------------------------
# Bat
# ---------------------------------------------------------------------------

BAT_Z = 0.80


def bat_wing(s, drop):
    """The +X (s = 1) or -X (s = -1) wing, spread. `drop` is the angle (degrees)
    of the membrane below the backward horizontal: -26 trails back and up
    (the e/w frames), 100 hangs down with the scallops underneath, a hair
    forward (the s/n frames: face-on to this camera, the classic silhouette)."""
    span = 0.235
    segs = (0.0, 0.40, 0.70, 1.0)

    def lead(u):
        return V((s * (0.065 + span * u), 0.005 - 0.035 * math.sin(math.pi * u),
                  BAT_Z - 0.015 + 0.060 * math.sin(math.pi * u) ** 0.8 - 0.02 * u))

    def depth(u):
        for a, b in zip(segs[:-1], segs[1:]):
            if u <= b + 1e-9:
                t = (u - a) / (b - a)
                break
        base = 0.205 * (1.0 - u) ** 0.55 + 0.026
        return base * (1.0 - 0.50 * math.sin(math.pi * t) ** 0.8)

    a = math.radians(drop)
    down = V((0, math.cos(a), -math.sin(a)))

    def fn(u, v):
        p = lead(u)
        d = depth(u)
        return p + down * (d * v) + V((0, 0, -0.020 * math.sin(math.pi * v)))
    tag = 'v' if drop > 45 else 'h'
    parts = [G.surface('b_wing%s%d' % (tag, s), fn, 30, 6, 'skin2', thick=0.007)]
    arm = [lead(u) + V((0, 0.004, 0.004)) for u in (0.0, 0.2, 0.45, 0.7, 0.9, 1.0)]
    parts.append(G.tube('b_arm%s%d' % (tag, s), arm, [0.016, 0.015, 0.013, 0.010, 0.008, 0.004], 'skin', n=8, sub=3))
    wrist = lead(0.45)
    for k, u in enumerate((0.40, 0.70)):
        tip = fn(u, 1.0)
        parts.append(G.tube('b_fing%s%d_%d' % (tag, s, k), [wrist, wrist.lerp(tip, 0.5) + V((0, 0, 0.006)), tip],
                            [0.010, 0.008, 0.004], 'skin', n=6, sub=3))
    return parts


def build_bat():
    r = G.Rig('bat')
    r.joint('body', 'root', (0, 0, BAT_Z))
    r.joint('wing_R', 'body', (-0.065, 0.0, BAT_Z - 0.015))
    r.joint('wing_L', 'body', (0.065, 0.0, BAT_Z - 0.015))
    bc, br = V((0, 0, BAT_Z)), (0.105, 0.095, 0.100)

    def pear(u):
        if u.z < 0:
            u.x *= 1.0 + 0.10 * u.z
            u.y *= 1.0 + 0.10 * u.z
        return u
    r.add(G.ellipsoid('b_body', bc, br, 'skin', seg=28, rings=14, deform=pear), 'body')
    r.add(G.ellipsoid('b_belly', bc + V((0, -0.052, -0.030)), (0.066, 0.050, 0.058), 'skin', seg=18, rings=10),
          'body')
    for s in (-1, 1):
        p, n = on_ell(bc, br, s * 32, 46, -0.02)
        tip = p + V((s * 0.085, 0.020, 0.170))
        r.add(G.tube('b_ear%d' % s, [p, p + V((s * 0.036, 0.008, 0.080)), tip], [(0.080, 0.022), (0.058, 0.018), 0.0],
                     'skin', n=12, nrm=(1, 0, 0)), 'body')
        r.add(G.tube('b_earin%d' % s, [p + V((s * 0.006, -0.020, 0.018)), p + V((s * 0.032, -0.012, 0.078)),
                                        tip + V((-s * 0.012, -0.010, -0.034))],
                     [(0.050, 0.008), (0.036, 0.007), 0.0], 'skin2', n=10, nrm=(1, 0, 0)), 'body')
        eye(r, 'body', 'b_eye%d' % s, bc, br, s * 25, 16, 0.034, squash=1.0)
    mouth = path_on(bc, br, [(-12, -8), (0, -12), (12, -8)], 0.002)
    r.add(G.tube('b_mouth', mouth, [0.007, 0.008, 0.007], 'dark', n=6, sub=3), 'body')
    for s in (-1, 1):
        p, n = on_ell(bc, br, s * 6, -11, 0.002)
        r.add(G.tube('b_fang%d' % s, [p, p + V((0, 0, -0.024)) + n * 0.004], [0.010, 0.0], 'white', n=6, sub=1),
              'body')
    # a tiny snout
    p, n = on_ell(bc, br, 0, 3, -0.004)
    r.add(G.ellipsoid('b_snout', p, (0.026, 0.018, 0.018), 'skin', seg=12, rings=6), 'body')
    for s in (-1, 1):
        r.add(G.tube('b_foot%d' % s, [(s * 0.030, 0.02, BAT_Z - 0.085), (s * 0.034, 0.025, BAT_Z - 0.125)],
                     [0.011, 0.007], 'dark', n=6, sub=1), 'body')
    for s, side in ((-1, 'R'), (1, 'L')):
        for drop, tag in ((-26, 'wing_h'), (100, 'wing_v')):
            for ob in bat_wing(s, drop):
                r.add(ob, 'wing_' + side, tag=tag)
    return r


def pose_bat(anim, f, n, d='s'):
    if anim == 'fly':
        flap = [20, 50, 0, -32][f % 4]
        bob = [0.0, -0.012, 0.0, 0.016][f % 4]
        return {'body@': (0, 0, bob), 'body': (-10, 0, 0), 'wing_L': (0, -flap, 0), 'wing_R': (0, flap, 0),
                '_hide': ('wing_h',) if d in 'sn' else ('wing_v',)}
    return None


# ---------------------------------------------------------------------------
# Mummy
# ---------------------------------------------------------------------------

MU_TORSO = [(0.0, 0.20), (0.125, 0.21), (0.160, 0.26), (0.170, 0.34), (0.166, 0.43), (0.152, 0.51),
            (0.110, 0.575), (0.060, 0.605), (0.0, 0.615)]
MU_SY = 0.82
MU_HC, MU_HR = V((0.0, -0.005, 0.845)), (0.240, 0.222, 0.235)


def build_mummy():
    r = G.Rig('mummy')
    r.joint('hips', 'root', (0, 0, 0.27))
    r.joint('spine', 'hips', (0, 0, 0.30))
    r.joint('head', 'spine', (0, 0.0, 0.59))
    r.joint('arm_R', 'spine', (-0.155, 0, 0.52))
    r.joint('arm_L', 'spine', (0.155, 0, 0.52))
    r.joint('hand_R', 'arm_R', (-0.222, 0, 0.31))
    r.joint('hand_L', 'arm_L', (0.222, 0, 0.31))
    r.joint('leg_R', 'hips', (-0.080, 0, 0.25))
    r.joint('leg_L', 'hips', (0.080, 0, 0.25))
    r.joint('foot_R', 'leg_R', (-0.082, 0, 0.08))
    r.joint('foot_L', 'leg_L', (0.082, 0, 0.08))

    r.add(G.lathe('m_torso', MU_TORSO, 'skin', sy=MU_SY, seg=36), 'spine')
    rf = G.lathe_body_r(MU_TORSO, 1.0, MU_SY)
    bands = [(0.235, 0.022, 0.3, 'skin'), (0.285, -0.030, 1.2, 'skin2'), (0.335, 0.026, 2.0, 'skin'),
             (0.385, -0.024, 0.5, 'skin'), (0.435, 0.032, 2.6, 'skin2'), (0.485, -0.026, 1.7, 'skin'),
             (0.535, 0.024, 0.9, 'skin')]
    for k, (z0, tilt, ph, key) in enumerate(bands):
        r.add(G.wrap('m_tw%d' % k, rf, lambda th, z0=z0, tilt=tilt, ph=ph: z0 + tilt * math.sin(th + ph), 0.050,
                     key, n=48, thick=0.014), 'spine')
    # a sash crossing the chest diagonally
    r.add(G.wrap('m_sash', lambda th, z: rf(th, z) + 0.012, lambda th: 0.43 + 0.10 * math.sin(th + 0.4), 0.048,
                 'skin2', n=56, thick=0.013), 'spine')

    hc, hr = MU_HC, MU_HR
    r.add(G.ellipsoid('m_head', hc, hr, 'skin', seg=36, rings=18), 'head')
    rh = G.ell_body_r(hc, hr)
    hb = [(0.655, 0.020, 0.2, 'skin'), (0.705, -0.028, 1.1, 'skin'), (0.760, 0.030, 2.4, 'skin2'),
          (0.815, -0.026, 0.4, 'skin'), (0.870, 0.030, 1.7, 'skin'), (0.925, -0.034, 2.9, 'skin'),
          (0.980, 0.030, 0.8, 'skin2'), (1.030, -0.020, 2.0, 'skin')]
    for k, (z0, tilt, ph, key) in enumerate(hb):
        r.add(G.wrap('m_hw%d' % k, lambda th, z: rh(th, z) - 0.001, lambda th, z0=z0, tilt=tilt, ph=ph:
                     z0 + tilt * math.sin(th + ph), 0.050, key, c=(hc.x, hc.y, 0), n=56, thick=0.013), 'head')
    # one eye glowing through a gap, the other a squinting slit under a wrap
    eye(r, 'head', 'm_eye', hc, hr, 25, 20, 0.070, socket=1.42, out=0.016, pupil=None)
    p, n = on_ell(hc, hr, -27, 17, 0.018)
    Rs = track(n) @ rotm((0, 0, -12))
    r.add(G.ellipsoid('m_slit', p, (0.058, 0.017, 0.012), 'dark', rot=Rs, seg=16, rings=6), 'head')
    r.add(G.ellipsoid('m_slitglow', p + n * 0.004, (0.036, 0.008, 0.010), 'glow', rot=Rs, seg=14, rings=6), 'head')
    r.add(G.wrap('m_cover', lambda th, z: rh(th, z) + 0.011, lambda th: 0.905 + 0.05 * math.sin(th + 2.0), 0.050,
                 'skin2', c=(hc.x, hc.y, 0), th0=math.radians(-160), th1=math.radians(-40), n=24, thick=0.012,
                 taper=0.15), 'head')
    p, n = on_ell(hc, hr, 3, -12, 0.016)
    r.add(G.ellipsoid('m_mouth', p, (0.048, 0.020, 0.012), 'dark', rot=track(n) @ rotm((0, 0, 6)), seg=16,
                      rings=6), 'head')
    # a loose end fluttering from the back of the head
    p0 = on_ell(hc, hr, 128, -8, 0.010)[0]
    r.add(G.ribbon('m_end', [p0, p0 + V((0.06, 0.03, -0.06)), p0 + V((0.12, 0.05, -0.04)),
                             p0 + V((0.17, 0.05, -0.08)), p0 + V((0.22, 0.07, -0.05))],
                   [0.056, 0.056, 0.054, 0.052, 0.046], 'skin', hint=(0, 1, 0.3), thick=0.012), 'head')

    for s, side in ((-1, 'R'), (1, 'L')):
        arm = 'arm_' + side
        a0, a1 = V((s * 0.160, 0, 0.52)), V((s * 0.222, 0, 0.32))
        r.add(G.tube('m_arm' + side, [a0, a0.lerp(a1, 0.5) + V((s * 0.01, 0, 0)), a1], [0.050, 0.047, 0.044],
                     'skin', n=14), arm)
        for k, t in enumerate((0.25, 0.55, 0.85)):
            r.add(G.ringband('m_ab%s%d' % (side, k), a0.lerp(a1, t), a1 - a0, 0.047 - 0.004 * t, 0.042,
                             'skin2' if k == 1 else 'skin', tilt=0.018, phase=k * 1.7, thick=0.011), arm)
        hand = 'hand_' + side
        r.add(G.ellipsoid('m_hand' + side, (s * 0.224, -0.006, 0.262), (0.040, 0.048, 0.060), 'skin'), hand)
        r.add(G.ringband('m_hb' + side, (s * 0.224, -0.006, 0.270), (0, 0, 1), 0.043, 0.030, 'skin2', tilt=0.012,
                         thick=0.010), hand)
        leg = 'leg_' + side
        l0, l1 = V((s * 0.080, 0, 0.26)), V((s * 0.083, 0, 0.08))
        r.add(G.tube('m_leg' + side, [l0, l1], [0.070, 0.064], 'skin', n=14), leg)
        for k, t in enumerate((0.3, 0.7)):
            r.add(G.ringband('m_lb%s%d' % (side, k), l0.lerp(l1, t), l1 - l0, 0.068, 0.046,
                             'skin2' if (k + (s > 0)) % 2 else 'skin', tilt=0.022, phase=k * 2.1 + s, thick=0.012),
                  leg)
        foot = 'foot_' + side

        def flat_bottom(u):
            if u.z < -0.3:
                u.z = -0.3 + (u.z + 0.3) * 0.25
            return u
        r.add(G.ellipsoid('m_foot' + side, (s * 0.083, -0.030, 0.045), (0.062, 0.095, 0.050), 'skin',
                          deform=flat_bottom), foot)
        r.add(G.ringband('m_fb' + side, (s * 0.083, -0.045, 0.045), (0, -1, 0.3), 0.050, 0.035, 'skin2',
                         tilt=0.012, thick=0.010, ref=(1, 0, 0)), foot)
    # the long end dragging on the ground behind him
    r.add(G.ribbon('m_drag', [(0.08, 0.13, 0.30), (0.12, 0.18, 0.15), (0.16, 0.23, 0.03), (0.23, 0.29, 0.008),
                              (0.31, 0.27, 0.006), (0.38, 0.32, 0.006)],
                   [0.056, 0.058, 0.060, 0.062, 0.060, 0.052], 'skin',
                   hint=lambda s: (0, 1, 0) if s < 1.6 else (0, 0, 1), thick=0.011), 'hips')
    return r


def pose_mummy(anim, f, n, d='s'):
    ph = TAU * f / n
    if anim == 'idle':
        b = math.sin(ph)
        return {'spine': (2, 0, 0), 'spine@': (0, 0, -0.004 * (1 - b)), 'head': (-8, -6 + 2 * b, -3),
                'arm_R': (-90 + 2 * b, 0, -6), 'arm_L': (-88 - 2 * b, 0, 6), 'hand_R': (10, 0, 0),
                'hand_L': (8, 0, 0)}
    if anim == 'walk':
        # stiff: straight legs swing from the hip, arms locked forward, a wobble
        s_ = math.sin(ph)
        c = math.cos(ph)
        return {'spine': (4, 4 * s_, -3 * s_), 'spine@': (0, 0, -0.012 * abs(c)), 'hips': (0, 3 * s_, 4 * s_),
                'head': (-8 + 2 * abs(s_), -5 + 5 * s_, -3),
                'arm_R': (-90 + 3 * c, 0, -6), 'arm_L': (-88 - 3 * c, 0, 6), 'hand_R': (10, 0, 0),
                'hand_L': (8, 0, 0), 'leg_R': (20 * s_, 0, 0), 'leg_L': (-20 * s_, 0, 0),
                'foot_R': (-8 * max(0, s_), 0, 0), 'foot_L': (-8 * max(0, -s_), 0, 0)}
    if anim == 'push':
        # leaning into a boulder in front of him: arms level at chest height,
        # palms flat, legs driving behind in turn (a loop)
        s_ = math.sin(ph)
        sp = 26 + 2 * s_
        return {'spine': (sp, 0, 2 * s_), 'spine@': (0, 0, -0.02), 'head': (-24, 0, 3 * s_),
                'arm_R': (-100 - sp + 3 * s_, 0, -8), 'arm_L': (-100 - sp - 3 * s_, 0, 8),
                'hand_R': (-55, 0, 0), 'hand_L': (-55, 0, 0),
                'leg_R': (24 + 12 * s_, 0, 0), 'leg_L': (24 - 12 * s_, 0, 0),
                'foot_R': (-22 - 10 * s_, 0, 0), 'foot_L': (-22 + 10 * s_, 0, 0)}
    return None


# ---------------------------------------------------------------------------
# Werewolf
# ---------------------------------------------------------------------------

WW_TORSO = [(0.0, 0.25), (0.130, 0.26), (0.175, 0.31), (0.195, 0.40), (0.205, 0.50), (0.195, 0.58),
            (0.150, 0.645), (0.080, 0.675), (0.0, 0.68)]
WW_SY = 0.84
WW_HC, WW_HR = V((0.0, -0.03, 0.885)), (0.245, 0.225, 0.232)


def build_werewolf():
    r = G.Rig('werewolf')
    r.joint('hips', 'root', (0, 0.02, 0.30))
    r.joint('spine', 'hips', (0, 0.02, 0.32))
    r.joint('head', 'spine', (0, -0.02, 0.66))
    r.joint('jaw', 'head', (0, -0.12, 0.79))
    r.joint('tail', 'hips', (0, 0.15, 0.32))
    for s, side in ((-1, 'R'), (1, 'L')):
        r.joint('arm_' + side, 'spine', (s * 0.19, 0.0, 0.59))
        r.joint('fore_' + side, 'arm_' + side, (s * 0.25, 0.0, 0.43))
        r.joint('hand_' + side, 'fore_' + side, (s * 0.27, 0.0, 0.285))
        r.joint('leg_' + side, 'hips', (s * 0.10, 0.02, 0.27))
        r.joint('shin_' + side, 'leg_' + side, (s * 0.105, 0.0, 0.155))
        r.joint('foot_' + side, 'shin_' + side, (s * 0.105, 0.02, 0.065))

    r.add(G.lathe('w_torso', WW_TORSO, 'skin', sy=WW_SY, seg=36), 'spine')
    r.add(G.ellipsoid('w_chest', (0, front_y(WW_TORSO, WW_SY, 0.43, 0, -0.045), 0.43), (0.105, 0.06, 0.115), 'skin2'),
          'spine')
    # a ruff of lighter fur at the neck, spiky
    for k in range(7):
        az = -60 + 20 * k
        th = math.radians(-90 + az)
        rr = G.ell_r(0.15, 0.15 * WW_SY, th)
        p = V((rr * math.cos(th), rr * math.sin(th), 0.625))
        d = V((math.cos(th), math.sin(th) * 0.8, 0)).normalized()
        r.add(G.tube('w_ruff%d' % k, [p, p + d * 0.04 + V((0, 0, -0.06))], [0.040, 0.0], 'skin2', n=8, sub=2),
              'spine')
    # torn shorts
    shorts = [(0.0, 0.20), (0.160, 0.205), (0.198, 0.26), (0.205, 0.33), (0.200, 0.36), (0.0, 0.37)]
    r.add(G.lathe('w_shorts', shorts, 'c7', sy=WW_SY, seg=32), 'hips')
    for s, side in ((-1, 'R'), (1, 'L')):
        def jag(th, i, s=s):
            if i != 0:
                return 0.0
            return 0.030 * tri(th * 5 / TAU + 0.3 * s) + 0.012 * tri(th * 9 / TAU)
        r.add(G.lathe('w_shortleg' + side, [(0.090, 0.165), (0.094, 0.23), (0.092, 0.29)], 'c7',
                      c=(s * 0.10, 0.02, 0), sy=0.95, seg=30, zfn=jag, cap_bot=False, cap_top=False, thick=0.012),
              'leg_' + side)
    # tail
    r.add(G.tube('w_tail', [(0, 0.14, 0.32), (0, 0.26, 0.30), (0.01, 0.36, 0.37), (0.02, 0.41, 0.49)],
                 [0.045, 0.080, 0.078, 0.0], 'skin', n=14), 'tail')
    r.add(G.ellipsoid('w_tailtip', (0.02, 0.395, 0.455), (0.048, 0.048, 0.055), 'skin2', seg=14, rings=8), 'tail')

    # limbs
    for s, side in ((-1, 'R'), (1, 'L')):
        r.add(G.tube('w_up' + side, [(s * 0.19, 0, 0.59), (s * 0.25, 0, 0.43)], [0.066, 0.058], 'skin', n=14),
              'arm_' + side)
        r.add(G.tube('w_elb' + side, [(s * 0.255, 0.02, 0.44), (s * 0.28, 0.07, 0.43)], [0.035, 0.0], 'skin',
                     n=8, sub=2), 'arm_' + side)
        r.add(G.tube('w_fore' + side, [(s * 0.25, 0, 0.43), (s * 0.268, 0, 0.30)], [0.064, 0.072], 'skin', n=14),
              'fore_' + side)
        r.add(G.ellipsoid('w_hand' + side, (s * 0.272, -0.010, 0.245), (0.062, 0.064, 0.064), 'skin'),
              'hand_' + side)
        for k, dx in enumerate((-0.03, 0.0, 0.03)):
            b0 = V((s * 0.272 + dx, -0.045, 0.205))
            r.add(G.tube('w_claw%s%d' % (side, k), [b0, b0 + V((0, -0.030, -0.035))], [0.014, 0.0], 'white', n=8,
                         sub=1), 'hand_' + side)
        r.add(G.tube('w_thigh' + side, [(s * 0.10, 0.02, 0.27), (s * 0.105, 0.0, 0.155)], [0.076, 0.062], 'skin',
                     n=14), 'leg_' + side)
        r.add(G.tube('w_shin' + side, [(s * 0.105, 0.0, 0.155), (s * 0.105, 0.02, 0.065)], [0.058, 0.050], 'skin',
                     n=12), 'shin_' + side)

        def flat_bottom(u):
            if u.z < -0.3:
                u.z = -0.3 + (u.z + 0.3) * 0.25
            return u
        r.add(G.ellipsoid('w_foot' + side, (s * 0.105, -0.040, 0.040), (0.070, 0.110, 0.045), 'skin',
                          deform=flat_bottom), 'foot_' + side)
        for k, dx in enumerate((-0.035, 0.0, 0.035)):
            b0 = V((s * 0.105 + dx, -0.135, 0.035))
            r.add(G.tube('w_toe%s%d' % (side, k), [b0, b0 + V((0, -0.030, -0.020))], [0.013, 0.0], 'white', n=8,
                         sub=1), 'foot_' + side)

    # head: a long snout with a toothy grin, tall pointed ears, cheek tufts
    hc, hr = WW_HC, WW_HR
    r.add(G.ellipsoid('w_head', hc, hr, 'skin', seg=36, rings=18), 'head')

    def snout(u):
        if u.y < 0:                       # narrower towards the nose
            u.x *= 1.0 + 0.22 * u.y
        return u
    r.add(G.ellipsoid('w_muzzle', (0, -0.235, 0.815), (0.112, 0.165, 0.082), 'skin2', seg=28, rings=14,
                      deform=snout), 'head')
    r.add(G.ellipsoid('w_nose', (0, -0.392, 0.852), (0.052, 0.036, 0.036), 'dark', seg=18, rings=10), 'head')
    # the grin: a dark mouth across the front of the snout, fangs hanging over it
    r.add(G.ellipsoid('w_mouth', (0, -0.322, 0.772), (0.080, 0.064, 0.030), 'dark', rot=(-18, 0, 0), seg=22,
                      rings=10), 'jaw')
    r.add(G.ellipsoid('w_jaw', (0, -0.262, 0.752), (0.084, 0.100, 0.036), 'skin2', rot=(-10, 0, 0), seg=22,
                      rings=10), 'jaw')
    for s_ in (-1, 1):
        b0 = V((s_ * 0.046, -0.352, 0.800))
        r.add(G.tube('w_fang%d' % s_, [b0, b0 + V((0, -0.006, -0.046))], [0.019, 0.0], 'white', n=8, sub=1),
              'head')
        b1 = V((s_ * 0.062, -0.318, 0.748))
        r.add(G.tube('w_lfang%d' % s_, [b1, b1 + V((0, -0.008, 0.032))], [0.013, 0.0], 'white', n=8, sub=1), 'jaw')
    for s_ in (-1, 1):
        eye(r, 'head', 'w_eye%d' % s_, hc, hr, s_ * 31, 26, 0.060, squash=0.9, rot=-s_ * 10,
            pupil=(0.0, 0.0, 0.019), lid=('skin', 0.22))
        brow = path_on(hc, hr, [(s_ * 12, 36), (s_ * 28, 43), (s_ * 44, 44)], 0.010)
        r.add(G.tube('w_brow%d' % s_, brow, [0.022, 0.020, 0.0], 'hair', n=8, sub=3), 'head')
        # tall pointed ears, the inside facing the front
        p, n = on_ell(hc, hr, s_ * 40, 58, -0.03)
        tip = p + V((s_ * 0.080, 0.020, 0.265))
        r.add(G.tube('w_ear%d' % s_, [p, p + V((s_ * 0.030, 0.010, 0.13)), tip],
                     [(0.090, 0.034), (0.062, 0.026), 0.0], 'skin', n=12, nrm=(1, 0, 0)), 'head')
        r.add(G.tube('w_earin%d' % s_, [p + V((0, -0.030, 0.03)), p + V((s_ * 0.028, -0.022, 0.13)),
                                         tip + V((-s_ * 0.014, -0.018, -0.06))],
                     [(0.056, 0.012), (0.040, 0.010), 0.0], 'skin2', n=10, nrm=(1, 0, 0)), 'head')
        for k, (az, el, d) in enumerate(((76, -12, (0.13, -0.02, -0.07)), (92, 6, (0.14, 0.03, -0.02)))):
            q, m = on_ell(hc, hr, s_ * az, el, -0.02)
            r.add(G.tube('w_cheek%d_%d' % (s_, k), [q, q + V((s_ * d[0], d[1], d[2]))], [0.052, 0.0], 'skin', n=10,
                         sub=2), 'head')
    q, m = on_ell(hc, hr, 0, 70, -0.02)
    r.add(G.tube('w_tuft', [q, q + V((0.0, -0.02, 0.07)), q + V((0.0, -0.07, 0.10))], [0.05, 0.034, 0.0], 'hair',
                 n=10, sub=3), 'head')
    return r


def pose_werewolf(anim, f, n, d='s'):
    ph = TAU * f / n
    if anim == 'idle':
        b = math.sin(ph)
        return {'spine': (16, 0, 0), 'head': (-20 - 6 * b, 0, 3 * b), 'jaw': (4 + 2 * b, 0, 0),
                'arm_R': (-14, 0, -6), 'arm_L': (-18, 0, 6), 'fore_R': (-22, 0, 0), 'fore_L': (-18, 0, 0),
                'hand_R': (-10, 0, 0), 'hand_L': (-10, 0, 0),
                'leg_R': (-22, 0, -4), 'leg_L': (-18, 0, 4), 'shin_R': (34, 0, 0), 'shin_L': (30, 0, 0),
                'foot_R': (-12, 0, 0), 'foot_L': (-12, 0, 0), 'hips@': (0, 0, -0.022),
                'tail': (-10 + 6 * b, 0, 10 * b)}
    if anim == 'howl':
        # 00 crouch and breathe in, 01 rise, 02-03 head up to the moon, jaw wide, 04 winding down
        sp = [24, 6, -8, -10, 4][f]
        hd = [-8, -36, -62, -66, -40][f]
        jw = [0, 10, 28, 30, 12][f]
        ar = [(-24, 10), (-10, 22), (18, 36), (20, 38), (0, 22)][f]
        return {'spine': (sp, 0, [0, 0, 0, 2, 0][f]), 'hips@': (0, 0, [-0.045, -0.02, 0, 0.004, -0.015][f]),
                'head': (hd, 0, [0, 0, -3, 3, 0][f]), 'jaw': (jw, 0, 0),
                'arm_R': (ar[0], 0, -ar[1]), 'arm_L': (ar[0], 0, ar[1]), 'fore_R': (-20, 0, 0), 'fore_L': (-20, 0, 0),
                'hand_R': (-10, 0, 0), 'hand_L': (-10, 0, 0),
                'leg_R': ([-30, -18, -10, -10, -16][f], 0, -4), 'leg_L': ([-26, -16, -8, -8, -14][f], 0, 4),
                'shin_R': ([46, 28, 16, 16, 26][f], 0, 0), 'shin_L': ([42, 26, 14, 14, 24][f], 0, 0),
                'foot_R': ([-16, -10, -6, -6, -10][f], 0, 0), 'foot_L': ([-16, -10, -6, -6, -10][f], 0, 0),
                'tail': ([-10, 10, 30, 32, 14][f], 0, 0)}
    if anim == 'run':
        # on all fours, a bounding gallop: forelegs and hind legs in pairs
        s_ = math.sin(ph)
        c = math.cos(ph)
        return {'spine': (68 + 6 * c, 0, 0), 'hips@': (0, 0.02, 0.035 * max(0.0, s_) - 0.01),
                'hips': (-6 * c, 0, 0),
                'head': (-58 - 6 * c, 0, 0), 'jaw': (10 + 6 * s_, 0, 0),
                'arm_R': (-68 - 36 * s_, 0, -6), 'arm_L': (-68 - 30 * math.sin(ph + 0.5), 0, 6),
                'fore_R': (-8 + 18 * max(0, s_), 0, 0), 'fore_L': (-8 + 18 * max(0, math.sin(ph + 0.5)), 0, 0),
                'hand_R': (-20 + 10 * s_, 0, 0), 'hand_L': (-20 + 10 * s_, 0, 0),
                'leg_R': (36 * s_, 0, -3), 'leg_L': (32 * math.sin(ph + 0.5), 0, 3),
                'shin_R': (30 * max(0, -s_) + 6, 0, 0), 'shin_L': (30 * max(0, -math.sin(ph + 0.5)) + 6, 0, 0),
                'foot_R': (-14 * max(0, s_), 0, 0), 'foot_L': (-14 * max(0, s_), 0, 0),
                'tail': (-52 + 8 * c, 0, 10 * s_)}
    if anim == 'stun':
        # sat on the ground after hitting a wall: legs out, head rolling round, jaw slack
        c = math.cos(ph)
        s_ = math.sin(ph)
        return {'hips@': (0, 0.02, -0.165), 'spine': (-10 + 4 * c, 5 * s_, 0),
                'head': (-4 + 10 * c, 14 * s_, 6 * c), 'jaw': (16, 0, 0),
                'arm_R': (-6, 0, -30 - 4 * s_), 'arm_L': (-6, 0, 30 - 4 * s_), 'fore_R': (-14, 0, 0),
                'fore_L': (-14, 0, 0), 'hand_R': (0, 0, 0), 'hand_L': (0, 0, 0),
                'leg_R': (-72, 0, -14), 'leg_L': (-72, 0, 14), 'shin_R': (8, 0, 0), 'shin_L': (8, 0, 0),
                'foot_R': (52, 0, 0), 'foot_L': (52, 0, 0), 'tail': (40, 0, 20 * s_)}
    return None


# ---------------------------------------------------------------------------
# Zombie dog
# ---------------------------------------------------------------------------

def build_zombiedog():
    r = G.Rig('zombiedog')
    r.joint('body', 'root', (0, 0.02, 0.22))
    r.joint('head', 'body', (0, -0.12, 0.30))
    r.joint('tail', 'body', (0, 0.17, 0.27))
    legs = {'FR': (-0.060, -0.085), 'FL': (0.060, -0.085), 'BR': (-0.062, 0.125), 'BL': (0.062, 0.125)}
    for k, (x, y) in legs.items():
        r.joint('leg_' + k, 'body', (x, y, 0.17))
    bc, br = V((0, 0.02, 0.215)), (0.100, 0.165, 0.098)
    r.add(G.ellipsoid('d_body', bc, br, 'skin', seg=28, rings=14), 'body')
    for k, (az, el, sz) in enumerate(((95, 30, (0.050, 0.065, 0.055)), (-80, 50, (0.045, 0.050, 0.040)),
                                      (150, 55, (0.05, 0.05, 0.04)))):
        p, n = on_ell(bc, br, az, el, -0.018)
        r.add(G.ellipsoid('d_patch%d' % k, p, sz, 'skin2', rot=track(n), seg=16, rings=8), 'body')
    stitches(r, 'body', 'd_stitch', bc, br, [(62, 8), (80, 20), (98, 24)], ticks=3, rad=0.009, tick_len=0.02)
    for k, (x, y) in legs.items():
        r.add(G.tube('d_leg' + k, [(x, y, 0.18), (x * 1.05, y, 0.04)], [0.034, 0.030], 'skin', n=10),
              'leg_' + k)
        r.add(G.ellipsoid('d_paw' + k, (x * 1.05, y - 0.018, 0.022), (0.036, 0.046, 0.024), 'skin', seg=14, rings=8),
              'leg_' + k)
    r.add(G.tube('d_tail', [(0, 0.165, 0.27), (0, 0.215, 0.33), (0.02, 0.235, 0.40), (0.045, 0.215, 0.445)],
                 [0.026, 0.021, 0.015, 0.0], 'skin', n=10), 'tail')
    # head
    hc, hr = V((0, -0.175, 0.345)), (0.118, 0.106, 0.104)
    r.add(G.ellipsoid('d_head', hc, hr, 'skin', seg=28, rings=14), 'head')
    r.add(G.ellipsoid('d_muzzle', (0, -0.268, 0.318), (0.066, 0.066, 0.052), 'skin2', seg=20, rings=10), 'head')
    r.add(G.ellipsoid('d_nose', (0, -0.332, 0.342), (0.030, 0.021, 0.022), 'dark', seg=12, rings=8), 'head')
    p, n = on_ell(hc, hr, 36, 22, -0.015)
    r.add(G.ellipsoid('d_eyepatch', p, (0.056, 0.052, 0.03), 'skin2', rot=track(n), seg=16, rings=8), 'head')
    eye(r, 'head', 'd_eyeL', hc, hr, 36, 22, 0.042, pupil=(0.12, -0.15, 0.014))
    eye(r, 'head', 'd_eyeR', hc, hr, -34, 18, 0.033, pupil=(-0.18, 0.10, 0.012))
    # one ear up, one flopped down
    p, n = on_ell(hc, hr, 50, 52, -0.012)
    r.add(G.tube('d_earup', [p, p + V((0.02, 0.004, 0.06)), p + V((0.035, 0.012, 0.125))],
                 [(0.046, 0.016), (0.034, 0.013), 0.0], 'skin', n=12, nrm=(1, 0, 0)), 'head')
    r.add(G.tube('d_earupin', [p + V((0.0, -0.012, 0.015)), p + V((0.02, -0.010, 0.06)),
                               p + V((0.03, -0.004, 0.10))], [(0.028, 0.006), (0.020, 0.005), 0.0], 'skin2', n=10,
                 nrm=(1, 0, 0)), 'head')
    p, n = on_ell(hc, hr, -68, 40, -0.01)
    r.add(G.tube('d_eardown', [p, p + V((-0.045, 0.0, 0.0)), p + V((-0.06, -0.01, -0.07)),
                               p + V((-0.05, -0.02, -0.12))], [(0.018, 0.040), (0.018, 0.046), (0.016, 0.042), 0.0],
                 'skin2', n=12, nrm=(1, 0, 0)), 'head')
    # mouth, one tooth, tongue out
    mouth = [V((-0.045, -0.300, 0.296)), V((-0.015, -0.322, 0.288)), V((0.02, -0.322, 0.288)),
             V((0.05, -0.298, 0.298))]
    r.add(G.tube('d_mouth', mouth, [0.008, 0.009, 0.009, 0.007], 'dark', n=6, sub=3), 'head')
    r.add(G.box('d_tooth', (-0.018, -0.326, 0.281), (0.016, 0.010, 0.016), 'white', bevel=0.003), 'head')
    r.add(G.tube('d_tongue', [(0.020, -0.318, 0.288), (0.028, -0.336, 0.262), (0.034, -0.340, 0.232)],
                 [(0.024, 0.009), (0.024, 0.010), (0.018, 0.009)], 'x13', n=12, nrm=(1, 0, 0)), 'head')
    # collar with a tag
    nc = V((0, -0.100, 0.292))
    ax = V((0, -0.55, 0.8)).normalized()
    r.add(G.ringband('d_collar', nc, ax, 0.094, 0.042, 'a11', thick=0.018, ref=(1, 0, 0)), 'body')
    r.add(G.ellipsoid('d_tag', (0.072, -0.160, 0.226), (0.028, 0.010, 0.030), 'a12', rot=(-20, 0, 38), seg=14,
                      rings=6), 'body')
    return r


def pose_zombiedog(anim, f, n, d='s'):
    ph = TAU * f / n
    if anim == 'idle':
        b = math.sin(ph)
        return {'head': (-14 - 3 * b, 8, 14), 'tail': (0, 20 * b, -10), 'body@': (0, 0, -0.004 * (1 - b)),
                'leg_FR': (0, 0, 0), 'leg_FL': (0, 0, 0)}
    if anim == 'run':
        # bounding gallop: 00 gather, 01 push off, 02 stretched in the air, 03 land on the forelegs
        F = [24, -12, -46, -18][f]
        B = [-36, 34, 42, 4][f]
        return {'body@': (0, 0, [0.0, 0.02, 0.05, 0.012][f]), 'body': ([2, -9, 0, 9][f], 0, 0),
                'head': ([-6, 4, -8, -14][f], 0, [4, -3, 5, -2][f]),
                'leg_FR': (F, 0, 0), 'leg_FL': (F + 8, 0, 0), 'leg_BR': (B, 0, 0), 'leg_BL': (B - 8, 0, 0),
                'tail': ([-30, -44, -52, -38][f], [15, -10, 12, -12][f], 0)}
    return None


# ---------------------------------------------------------------------------
# Scarab
# ---------------------------------------------------------------------------

def build_scarab():
    r = G.Rig('scarab')
    r.joint('body', 'root', (0, 0, 0.07))
    legdef = [(-0.105, -40), (-0.025, 5), (0.055, 45)]
    for s, side in ((-1, 'R'), (1, 'L')):
        for k, (y, ang) in enumerate(legdef):
            r.joint('leg_%s%d' % (side, k), 'body', (s * 0.075, y, 0.065))
    sc, sr = V((0, 0.035, 0.085)), (0.135, 0.165, 0.100)

    def flat(u):
        if u.z < 0:
            u.z *= 0.35
        return u
    r.add(G.ellipsoid('s_shell', sc, sr, 'skin', seg=32, rings=16, deform=flat), 'body')

    def top_z(x, y, out=0.004):
        k = 1 - (x / sr[0]) ** 2 - ((y - sc.y) / sr[1]) ** 2
        return sc.z + sr[2] * math.sqrt(max(k, 0.0)) + out
    # the jewel pattern: a gold seam between the wing cases, a gold rim round
    # them and a gold spot on each
    ys = [-0.098 + 0.03 * i for i in range(10)]
    seam = [V((0, y, top_z(0, y, 0.003))) for y in ys]
    r.add(G.tube('s_seam', seam, [(0.017, 0.007)] * len(seam), 'skin2', n=8, sub=2, nrm=(1, 0, 0)), 'body')
    rim = []
    for k in range(25):
        th = math.radians(-58 + (360 - 64) * k / 24.0)
        rim.append(V((sc.x + sr[0] * 1.005 * math.cos(th), sc.y + sr[1] * 1.005 * math.sin(th), sc.z + 0.012)))
    r.add(G.tube('s_rim', rim, [(0.012, 0.017)] * len(rim), 'skin2', n=8, sub=2, nrm=(0, 0, 1)), 'body')
    for s_ in (-1, 1):
        x, y = s_ * 0.070, 0.06
        r.add(G.ellipsoid('s_spot%d' % s_, (x, y, top_z(x, y, -0.004)), (0.030, 0.042, 0.012), 'skin2', seg=14,
                          rings=6), 'body')
    r.add(G.ellipsoid('s_thorax', (0, -0.125, 0.092), (0.108, 0.068, 0.070), 'skin', seg=24, rings=12), 'body')
    r.add(G.ringband('s_thrim', (0, -0.125, 0.082), (0, 0, 1), 0.094, 0.018, 'skin2', thick=0.012, ref=(1, 0, 0)),
          'body')
    r.add(G.ellipsoid('s_head', (0, -0.192, 0.070), (0.072, 0.052, 0.046), 'skin', seg=20, rings=10), 'body')
    # the scarab's toothed shovel of a face
    for k in range(5):
        x = -0.05 + 0.025 * k
        r.add(G.tube('s_rake%d' % k, [(x, -0.215, 0.062), (x * 1.2, -0.252, 0.052)], [0.012, 0.0], 'skin2', n=6,
                     sub=1), 'body')
    for s in (-1, 1):
        r.add(G.ellipsoid('s_eye%d' % s, (s * 0.052, -0.210, 0.086), (0.026, 0.026, 0.024), 'glow', seg=14,
                          rings=8), 'body')
        b0 = V((s * 0.030, -0.226, 0.090))
        r.add(G.tube('s_ant%d' % s, [b0, b0 + V((s * 0.03, -0.04, 0.04))], [0.008, 0.007], 'dark', n=6, sub=1),
              'body')
        r.add(G.ellipsoid('s_club%d' % s, b0 + V((s * 0.036, -0.048, 0.050)), (0.018, 0.014, 0.020), 'dark',
                          seg=10, rings=6), 'body')
    for s, side in ((-1, 'R'), (1, 'L')):
        for k, (y, ang) in enumerate(legdef):
            a = math.radians(ang)
            hip = V((s * 0.075, y, 0.065))
            knee = hip + V((s * 0.085 * math.cos(a), 0.085 * math.sin(a), 0.035))
            foot = knee + V((s * 0.060 * math.cos(a), 0.060 * math.sin(a), -0.10))
            r.add(G.tube('s_leg%s%d' % (side, k), [hip, knee, foot], [0.020, 0.016, 0.009], 'dark', n=8, sub=3),
                  'leg_%s%d' % (side, k))
    return r


def pose_scarab(anim, f, n, d='s'):
    if anim == 'crawl':
        ph = TAU * f / n
        P = {'body@': (0, 0, 0.004 * math.cos(2 * ph))}
        for s, side in ((-1, 'R'), (1, 'L')):
            for k in range(3):
                # tripod gait: L0 R1 L2 together, R0 L1 R2 the other half
                g = ph + (math.pi if ((k + (s > 0)) % 2) else 0.0)
                P['leg_%s%d' % (side, k)] = (0, -s * 6 * max(0, math.sin(g)), 16 * math.cos(g))
        return P
    return None


# ---------------------------------------------------------------------------
# Crow
# ---------------------------------------------------------------------------

def feather(rig, joint, name, p0, p1, rad, tip=0.35, bend=(0, 0, 0), flat=0.45, nrm=(0, 0, 1), tag=None):
    """A feather: base colour (1) with a lighter tip (2)."""
    p0, p1 = V(p0), V(p1)
    pm = p0.lerp(p1, 1.0 - tip) + V(bend) * 0.5
    rig.add(G.tube(name, [p0, pm], [(rad, rad * flat), (rad * 0.85, rad * 0.85 * flat)], 'skin', n=8, sub=2,
                   nrm=nrm, cap1=0.6), joint, tag=tag)
    rig.add(G.tube(name + 't', [pm - (pm - p0).normalized() * 0.004, p1 + V(bend)],
                   [(rad * 0.85, rad * 0.85 * flat), 0.0], 'skin2', n=8, sub=2, nrm=nrm), joint, tag=tag)


def crow_wing(rig, s, side):
    """The spread wing (fly, dive): a broad inner wing trailing back and a
    little up (it faces this high camera), ragged secondaries along the back
    edge and five fingered primaries fanning at the tip."""
    joint = 'wing_' + side
    a = math.radians(-22)
    back = V((0, math.cos(a), -math.sin(a)))

    def lead(u):
        return V((s * (0.072 + 0.215 * u), -0.012 - 0.022 * math.sin(math.pi * u),
                  0.245 + 0.030 * math.sin(math.pi * u) ** 0.8 - 0.012 * u))

    def depth(u):
        return 0.115 * (1.0 - 0.45 * u)

    def fn(u, v):
        return lead(u) + back * (depth(u) * v) + V((0, 0, -0.012 * math.sin(math.pi * v)))
    rig.add(G.surface('c_swing' + side, fn, 20, 6, 'skin', thick=0.012), joint, tag='spread')
    rig.add(G.tube('c_sarm' + side, [lead(u) + V((0, 0.004, 0.004)) for u in (0.0, 0.3, 0.6, 0.85)],
                   [0.022, 0.020, 0.016, 0.012], 'skin', n=8, sub=3), joint, tag='spread')
    for k in range(4):
        u = 0.10 + 0.17 * k
        b0 = fn(u, 0.7)
        feather(rig, joint, 'c_sec%s%d' % (side, k), b0, b0 + back * 0.07 + V((s * 0.01, 0, 0)), 0.024, tip=0.5,
                nrm=(0, 0, 1), flat=0.4, tag='spread')
    for k in range(5):
        u = 0.70 + 0.075 * k
        ang = math.radians(62 - 13 * k)
        b0 = fn(u, 0.35)
        dirv = V((s * math.cos(ang), 0, 0)) + back * math.sin(ang)
        feather(rig, joint, 'c_pri%s%d' % (side, k), b0, b0 + dirv * (0.10 + 0.008 * k), 0.022, tip=0.45,
                nrm=(0, 0, 1), flat=0.35, tag='spread')


def build_crow():
    r = G.Rig('crow')
    r.joint('body', 'root', (0, 0.02, 0.19))
    r.joint('head', 'body', (0, -0.05, 0.27))
    r.joint('tail', 'body', (0, 0.12, 0.15))
    for s, side in ((-1, 'R'), (1, 'L')):
        r.joint('wing_' + side, 'body', (s * 0.075, 0.0, 0.24))
        r.joint('leg_' + side, 'body', (s * 0.036, 0.03, 0.11))
    bc, br, brot = V((0, 0.035, 0.185)), (0.100, 0.125, 0.105), (-26, 0, 0)
    r.add(G.ellipsoid('c_body', bc, br, 'skin', rot=brot, seg=28, rings=14), 'body')
    # scruffy chest: a ruff of feathers with light tips
    for k, (x, z, ln) in enumerate(((-0.045, 0.215, 1.0), (-0.015, 0.225, 1.1), (0.018, 0.222, 1.1),
                                   (0.048, 0.210, 1.0), (-0.030, 0.170, 0.9), (0.005, 0.165, 1.0),
                                   (0.038, 0.168, 0.9))):
        p = V((x, -0.078 + abs(x) * 0.5, z))
        feather(r, 'body', 'c_chest%d' % k, p, p + V((x * 0.5, -0.030, -0.046)) * ln, 0.020, tip=0.5, nrm=(1, 0, 0),
                flat=0.6)
    # folded wings with ragged tips
    for s, side in ((-1, 'R'), (1, 'L')):
        r.add(G.ellipsoid('c_wing' + side, (s * 0.088, 0.048, 0.195), (0.036, 0.120, 0.080), 'skin',
                          rot=(-24, 0, s * 6), seg=20, rings=10), 'wing_' + side, tag='folded')
        for k in range(3):
            p0 = V((s * (0.092 - 0.012 * k), 0.10 + 0.012 * k, 0.175 - 0.022 * k))
            feather(r, 'wing_' + side, 'c_wf%s%d' % (side, k), p0,
                    p0 + V((s * (0.016 - 0.010 * k), 0.11 - 0.014 * k, -0.060 + 0.012 * k)), 0.024, tip=0.5,
                    nrm=(1, 0, 0), flat=0.5, tag='folded')
        crow_wing(r, s, side)
    # tail fan
    for k, dx in enumerate((-0.05, -0.017, 0.017, 0.05)):
        p0 = V((dx * 0.4, 0.125, 0.15))
        feather(r, 'tail', 'c_tail%d' % k, p0, p0 + V((dx, 0.14, -0.08 + abs(dx) * 0.4)), 0.027, tip=0.45,
                nrm=(0, 0, 1), flat=0.35)
    # legs and toes
    for s, side in ((-1, 'R'), (1, 'L')):
        x = s * 0.036
        r.add(G.tube('c_leg' + side, [(x, 0.03, 0.12), (x, 0.015, 0.012)], [0.015, 0.012], 'dark', n=8), 'leg_' + side)
        for k, d in enumerate(((-0.024, -0.048), (0.0, -0.055), (0.024, -0.048), (0.0, 0.032))):
            r.add(G.tube('c_toe%s%d' % (side, k), [(x, 0.015, 0.009), (x + d[0], 0.015 + d[1], 0.005)],
                         [0.010, 0.005], 'dark', n=6, sub=1), 'leg_' + side)
    # head: big, a scruffy crest, a heavy hooked beak, grumpy glowing eyes
    hc, hr = V((0, -0.062, 0.318)), (0.100, 0.093, 0.092)
    r.add(G.ellipsoid('c_head', hc, hr, 'skin', seg=28, rings=14), 'head')
    r.add(G.tube('c_beak', [(0, -0.140, 0.318), (0, -0.195, 0.300), (0, -0.236, 0.272), (0, -0.252, 0.236)],
                 [(0.046, 0.040), (0.032, 0.027), (0.018, 0.014), 0.0], 'dark', n=12, nrm=(1, 0, 0)), 'head')
    for k, (az, el, d) in enumerate(((0, 66, (0.0, -0.035, 0.085)), (-32, 70, (-0.04, 0.0, 0.075)),
                                     (32, 72, (0.04, 0.01, 0.08)), (0, 84, (0.0, 0.03, 0.09)),
                                     (150, 58, (0.02, 0.06, 0.05)))):
        p, n = on_ell(hc, hr, az, el, -0.012)
        feather(r, 'head', 'c_tuft%d' % k, p, p + V(d), 0.024, tip=0.45, nrm=(1, 0, 0), flat=0.6)
    for s in (-1, 1):
        eye(r, 'head', 'c_eye%d' % s, hc, hr, s * 36, 20, 0.037, rot=-s * 18, squash=0.95, lid=('skin', 0.32))
    return r


def pose_crow(anim, f, n, d='s'):
    ph = TAU * f / n
    if anim == 'perch':
        # a crow cocks its head: frame 0 shows the beak in profile (turned away
        # from this camera where the facing would point it at us), frame 1 a
        # jerk round to glare at the player
        turn = {'s': (-40, 16), 'n': (-40, 24), 'e': (6, -18), 'w': (-6, 18)}[d][f % 2]
        b = 1.0 if f % 2 else 0.0
        return {'head': (-6 + 4 * b, 0, turn), 'body@': (0, 0, -0.004 * b), 'tail': (-5 * b, 0, 0),
                '_hide': ('spread',)}
    if anim == 'fly':
        # at 1.0 m (the anchor stays on the ground below), body level, legs tucked
        flap = [22, 48, 0, -34][f % 4]
        bob = [0.0, -0.014, 0.0, 0.018][f % 4]
        return {'body@': (0, 0, 0.815 + bob), 'body': (24, 0, 0), 'head': (-24, 0, 0), 'tail': (-8, 0, 0),
                'wing_L': (0, -flap, 0), 'wing_R': (0, flap, 0), 'leg_R': (60, 0, 0), 'leg_L': (60, 0, 0),
                '_hide': ('folded',)}
    if anim == 'dive':
        # swooping from 1.0 m to 0.3 m: nose down with the wings swept back,
        # then pulling up with the wings thrown open and the talons out
        h = [1.0, 0.65, 0.30][f]
        return {'body@': (0, 0, h - 0.185), 'body': ([52, 66, 8][f], 0, 0), 'head': ([-36, -46, -12][f], 0, 0),
                'wing_L': (0, [-18, -8, -52][f], [34, 58, -12][f]), 'wing_R': (0, [18, 8, 52][f], [-34, -58, 12][f]),
                'tail': ([-4, -10, 22][f], 0, 0), 'leg_R': ([60, 70, -30][f], 0, 0), 'leg_L': ([60, 70, -36][f], 0, 0),
                '_hide': ('folded',)}
    return None


# ---------------------------------------------------------------------------
# The table of monsters
# ---------------------------------------------------------------------------

def build_armor():
    return A.build('armor')


BUILD = {'zombie': build_zombie, 'vampire': build_vampire, 'bat': build_bat, 'mummy': build_mummy,
         'werewolf': build_werewolf, 'zombiedog': build_zombiedog, 'armor': build_armor, 'scarab': build_scarab,
         'crow': build_crow}
POSE = {'zombie': pose_zombie, 'vampire': pose_vampire, 'bat': pose_bat, 'mummy': pose_mummy,
        'werewolf': pose_werewolf, 'zombiedog': pose_zombiedog, 'armor': A.pose, 'scarab': pose_scarab,
        'crow': pose_crow}


def write_palettes(out):
    pal = {}
    for who, p in PALETTES.items():
        pal[who] = {str(k): list(v) for k, v in sorted(p.items())}
    # the watch picks one per individual; each replaces these ids of the default
    pal['zombie_variants'] = {var['name']: {str(k): list(v) for k, v in var.items() if k != 'name'}
                              for var in ZOMBIE_VARIANTS}
    pal['_ids'] = {who: {str(k): v for k, v in d.items()} for who, d in IDS.items()}
    with open(os.path.join(os.path.abspath(out), 'palettes.json'), 'w') as fh:
        json.dump(pal, fh, indent=1)


def my_args():
    """Our own flags. They are taken out of sys.argv before mh_common.args()
    sees it: its argparse would read --sample as an abbreviation of --samples."""
    import argparse
    i = sys.argv.index('--') + 1 if '--' in sys.argv else len(sys.argv)
    argv = sys.argv[i:]
    ap = argparse.ArgumentParser(allow_abbrev=False)
    ap.add_argument('--sample', action='store_true')
    ap.add_argument('--dirs', default='')
    ap.add_argument('--frames', default='')
    b, rest = ap.parse_known_args(argv)
    sys.argv[i:] = rest
    return b


def jobs(a, b):
    todo = []
    if b.sample:
        for who, anim, dirs, frames in SAMPLE:
            n = [x for x in ANIMS[who] if x[0] == anim][0]
            todo.append((who, anim, dirs, frames, n[2], n[3]))
    else:
        for who, lst in ANIMS.items():
            for anim, dirs, n, ms in lst:
                todo.append((who, anim, dirs, list(range(n)), n, ms))
        for who in CARD:
            todo.append((who, 'card', 's', [0], 1, 0))
    out = []
    for who, anim, dirs, frames, n, ms in todo:
        if anim == 'card':
            if not a.only or who in a.only or 'card' in a.only or 'card_' + who in a.only:
                out.append((who, anim, 's', 0, 1, 0))
            continue
        if a.only and who not in a.only and '%s_%s' % (who, anim) not in a.only:
            continue
        if who not in BUILD:
            print('skip (not built yet):', who)
            continue
        for d in dirs:
            if b.dirs and d not in b.dirs.split(','):
                continue
            for f in frames:
                if b.frames and str(f) not in b.frames.split(','):
                    continue
                out.append((who, anim, d, f, n, ms))
    return out


def main():
    b = my_args()
    a = C.args()
    todo = jobs(a, b)
    os.makedirs(os.path.abspath(a.out), exist_ok=True)
    write_palettes(a.out)
    t_all = time.time()
    count = 0
    cur = None
    rig = None
    for who, anim, d, f, n, ms in todo:
        if who != cur:
            if cur is not None:
                C.save_meta(a.out)        # reset() forgets the meta collected so far
            C.reset('neutral', cpu=a.cpu)
            register_mats(PALETTES[who], extra=EXTRA_MATS.get(who))
            if who == 'armor':
                A.register_mats()
            rig = BUILD[who]()
            cur = who
        card = anim == 'card'
        if card:
            anim_p, f_p = CARD[who]
            n_p = [x for x in ANIMS[who] if x[0] == anim_p][0][2]
            P = POSE[who](anim_p, f_p, n_p, 's')
        else:
            P = POSE[who](anim, f, n, d)
        if P is None:
            print('skip (no pose yet):', who, anim)
            continue
        P = dict(P)
        hide = P.pop('_hide', ())
        cape = P.pop('_cape', None)
        if cape is not None:
            vamp_reshape(rig, cape)
        rig.pose(P, YAW[d])
        t0 = time.time()
        ids = sorted(IDS.get(who, {}).keys())
        if card:
            name = 'card_' + who
            C.render_sprite(a.out, name, rig.visible(hide), C.cell(0, 0, 0), passes=('light', 'id'),
                            bounce_ground=0.0, kind='card', samples=a.samples, zoom=2.0, margin=4,
                            extra=dict(monster=who, pose='%s_s_%02d' % (anim_p, f_p), ids=ids,
                                       height_m=HEIGHT[who]))
        else:
            name = '%s_%s_%s_%02d' % (who, anim, d, f)
            extra = dict(monster=who, anim=anim, dir=d, frame=f, frames=n, ms=ms, height_m=HEIGHT[who], ids=ids)
            C.render_sprite(a.out, name, rig.visible(hide), C.cell(0, 0, 0),
                            passes=('light', 'id', 'z', 'shadow'), shadow_z=0.0, bounce_ground=0.0, kind='char',
                            samples=a.samples, extra=extra, margin=2)
        count += 1
        print('rendered %s in %.1f s' % (name, time.time() - t0), flush=True)
    if cur is not None:
        C.save_meta(a.out)
    print('done: %d frames in %.1f s' % (count, time.time() - t_all))


main()
