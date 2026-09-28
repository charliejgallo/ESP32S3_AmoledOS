"""Monster Hop - Abyss Bay monsters and boss (SPEC.md sections 5 and 13.2).

    fishman   the lagoon fish-man, 1.10 m: lurk 2 @ 300 (s, eyes and fins over
              the water), emerge 4 @ 100 (s), idle 2 @ 400 and walk 6 @ 110
              (n e s w), dive 4 @ 100 (s, back into the water). lurk, emerge
              and dive are anchored at the water surface: a hold-out plane
              hides what is under it.
    crab      the giant crab, 0.55 m: idle 2 @ 400, walk 6 @ 70 (sideways:
              along X facing n/s, along Y facing e/w; meta moves_along), snap
              3 @ 80 (both claws out to the cells beside it), n e s w
    jelly     the jellyfish: float 4 @ 150 (s only), glow pass; anchored at
              the water surface below it
    kraken    the boss -> assets/bosses: idle 4 @ 150, slam 6 @ 80 (s; the
              raised tentacle whips down to the screen right, +X), 2 x 2,
              anchor at the block's centre on the water surface
    cards     card_fishman, card_crab, card_jelly, card_piranha (a single
              leaping piranha, for the album) -> assets/monsters, card_kraken
              -> assets/bosses (zoom 2, bosses 1.2; light + id)

    cd apps/monsterhop/tools/blender
    Blender -b -P monsters_bay.py -- --out ../../assets/monsters --bosses ../../assets/bosses \
        [--only fishman,crab_walk] [--dirs s,e] [--samples 48]

meta.json and palettes.json in both folders are MERGED (C.save_meta re-reads
the file right before writing; merge_palettes adds only our entries), so
other scripts' entries survive. The piranha cell and the Kraken's tentacle
as moving things are rendered by zones_bay_tiles.py (zones_bay_dyn.py), which
imports this module's builders (main() only runs as a script).

Built like monsters.py (monsters_geo primitives, rigid parts on a rig of
empties; the Kraken is rebuilt per frame, its tentacles are curves),
rendered light + id + z + shadow under the neutral light and recoloured by
the watch with palettes.json. monsters.py cannot be imported (it runs its
main() at import), so its small eye() / path_on() helpers are copied here.

Shared monster ids (SPEC 5): 1 skin/scales A, 2 skin B (belly, webbing,
suckers), 3 dark, 4 white, 5 eye glow (emissive), 6 fins (the 'hair' slot),
7-9 clothes, 11 accessory, 13 (here) water foam / splash.
"""
import json
import math
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import mh_common as C  # noqa: E402
import monsters_geo as G  # noqa: E402
from monsters_geo import V, on_ell, track, rotm, TAU  # noqa: E402
import bpy  # noqa: E402
from mathutils import Matrix  # noqa: E402
import zones_bay_props as ZP  # noqa: E402

PALETTES = {
    'fishman': {1: (64, 150, 128), 2: (196, 226, 170), 3: (28, 30, 46), 4: (244, 244, 232), 5: (226, 255, 80),
                6: (240, 110, 96), 7: (70, 104, 206), 8: (244, 244, 240), 11: (72, 168, 84), 13: (170, 232, 240)},
    'crab': {1: (226, 84, 62), 2: (250, 200, 150), 3: (40, 24, 30), 4: (252, 248, 236), 5: (120, 255, 236)},
    'jelly': {1: (250, 120, 222), 2: (255, 200, 245), 3: (60, 24, 70), 5: (110, 255, 240)},
    'piranha': {1: (150, 170, 190), 2: (236, 84, 60), 3: (30, 24, 34), 4: (252, 250, 240), 5: (255, 70, 60),
                13: (180, 236, 244)},
    'kraken': {1: (196, 58, 92), 2: (250, 176, 186), 3: (44, 18, 34), 4: (252, 246, 236), 5: (255, 226, 70),
               13: (180, 236, 244)},
}
IDS = {
    'fishman': {1: 'scales', 2: 'belly, lips, webbing', 3: 'dark (mouth, pupils, nostrils)', 4: 'fangs',
                5: 'eye glow', 6: 'fins (crest, cheeks, back, tail, elbows)', 7: 'swim trunks',
                8: 'trunks stripe + waistband', 11: 'seaweed on the head', 13: 'water foam (emerge)'},
    'crab': {1: 'shell, claws, legs', 2: 'underside, claw tips, joints', 3: 'dark (mouth, pupils)',
             4: 'claw teeth', 5: 'eye glow'},
    'jelly': {1: 'bell', 2: 'frills, tentacles, oral arms', 3: 'dark (face)', 5: 'glow (rim lights, tips)'},
    'piranha': {1: 'body (back)', 2: 'belly (red)', 3: 'dark (mouth)', 4: 'teeth', 5: 'eye glow',
                13: 'boiling foam'},
    'kraken': {1: 'skin', 2: 'spots, suckers, lips', 3: 'dark (mouth, pupils)', 4: 'white (eye shine)',
               5: 'eye glow', 13: 'foam around it'},
}
HEIGHT = {'fishman': 1.10, 'crab': 0.55, 'jelly': 0.95, 'piranha': 0.5, 'kraken': 2.4}
YAW = {'s': 0.0, 'e': 90.0, 'n': 180.0, 'w': -90.0}

# key: (id, roughness, metallic, specular)
MATS = {
    'skin': (1, 0.45, 0.0, 0.5), 'skin2': (2, 0.5, 0.0, 0.45), 'dark': (3, 0.4, 0.0, 0.5),
    'white': (4, 0.25, 0.0, 0.6), 'fin': (6, 0.45, 0.0, 0.5), 'c7': (7, 0.8, 0.0, 0.3),
    'c8': (8, 0.8, 0.0, 0.3), 'a11': (11, 0.5, 0.0, 0.45), 'foam': (13, 0.6, 0.0, 0.3),
}


def srgb(c):
    return G.srgb_to_lin(c)


def scaled_skin(id_, pal, rough=0.42, scale=46.0, strength=0.35):
    """Skin with a scale pattern in the bump (Voronoi cells): shading only,
    the colour stays one clean region."""
    def build(nt, neutral):
        b = nt.nodes.new('ShaderNodeBsdfPrincipled')
        col = (0.8, 0.8, 0.8) if neutral else srgb(pal.get(id_, (200, 200, 200)))
        b.inputs['Base Color'].default_value = tuple(col) + (1,)
        b.inputs['Roughness'].default_value = rough
        b.inputs['Specular'].default_value = 0.55
        tc = nt.nodes.new('ShaderNodeTexCoord')
        vo = nt.nodes.new('ShaderNodeTexVoronoi')
        vo.feature = 'F1'
        vo.inputs['Scale'].default_value = scale
        nt.links.new(tc.outputs['Object'], vo.inputs['Vector'])
        bm = nt.nodes.new('ShaderNodeBump')
        bm.inputs['Strength'].default_value = strength
        bm.inputs['Distance'].default_value = 0.004
        nt.links.new(vo.outputs['Distance'], bm.inputs['Height'])
        nt.links.new(bm.outputs['Normal'], b.inputs['Normal'])
        return b.outputs['BSDF']
    return build


def soft_glow(id_, pal, strength=0.35, rough=0.4):
    """A lit surface with a little emission of its own (the jellyfish bell):
    the light pass reads brighter than a sunlit surface, so the watch's
    palette colour comes out luminous."""
    def build(nt, neutral):
        b = nt.nodes.new('ShaderNodeBsdfPrincipled')
        col = (0.8, 0.8, 0.8) if neutral else srgb(pal.get(id_, (200, 200, 200)))
        b.inputs['Base Color'].default_value = tuple(col) + (1,)
        b.inputs['Roughness'].default_value = rough
        b.inputs['Emission'].default_value = tuple(col) + (1,)
        b.inputs['Emission Strength'].default_value = strength
        return b.outputs['BSDF']
    return build


def register_mats(who):
    pal = PALETTES[who]
    for key, (id_, rough, metal, spec) in MATS.items():
        C.mat(key, base=srgb(pal.get(id_, (200, 200, 200))), id=id_, rough=rough, metal=metal, spec=spec)
    C.mat('glow', id=5, build=G.glow_build(pal.get(5, (255, 255, 255))))
    C.mat('scales', id=1, build=scaled_skin(1, pal))
    C.mat('bell', id=1, build=soft_glow(1, pal))
    C.mat('frill', id=2, build=soft_glow(2, pal, 0.25))
    C.mat('holdout', base=(0, 0, 0))


# ---------------------------------------------------------------------------
# helpers copied from monsters.py (importing it would run its main())
# ---------------------------------------------------------------------------

def eye(rig, joint, name, c, r, az, el, rad, key='glow', flat=0.55, sink=0.35, socket=None, pupil=None,
        rot=0.0, squash=1.0, lid=None, out=0.0):
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
    return [on_ell(c, r, az, el, out)[0] for az, el in pts]


def tri(x):
    x = x - math.floor(x)
    return 1.0 - abs(2.0 * x - 1.0)


def fan(name, base, d_out, d_up, length, spread, key, n_rays=5, scallop=0.25, thick=0.012, rays_key=None,
        ray_r=0.010):
    """A webbed fin: a fan of `n_rays` spines from `base`, opening between the
    directions d_out +- spread (radians, rotating towards d_up), membrane
    between them with a scalloped edge. Returns [membrane, rays...]."""
    base, d_out, d_up = V(base), V(d_out).normalized(), V(d_up).normalized()
    side = d_up - d_out * d_up.dot(d_out)
    side.normalize()

    def f(u, v):
        a = (u * 2 - 1) * spread
        d = d_out * math.cos(a) + side * math.sin(a)
        k = 1.0 - scallop * (1 - math.cos(u * (n_rays - 1) * TAU)) * 0.5
        return base + d * (length * v * k)
    mem = G.surface(name, f, 4 * (n_rays - 1) + 1, 5, key, thick=thick)
    out = [mem]
    for i in range(n_rays):
        u = i / (n_rays - 1)
        a = (u * 2 - 1) * spread
        d = d_out * math.cos(a) + side * math.sin(a)
        out.append(G.tube(name + '_r%d' % i, [base, base + d * length * 1.04], [ray_r, ray_r * 0.35],
                          rays_key or key, n=6, sub=1))
    return out


# ---------------------------------------------------------------------------
# Fish-man
# ---------------------------------------------------------------------------

FM_TORSO = [(0.0, 0.20), (0.130, 0.21), (0.178, 0.27), (0.190, 0.36), (0.178, 0.45), (0.152, 0.52),
            (0.105, 0.575), (0.055, 0.60), (0.0, 0.605)]
FM_SY = 0.86
FM_HC, FM_HR = V((0.0, -0.02, 0.835)), (0.272, 0.238, 0.232)


def build_fishman():
    r = G.Rig('fishman')
    r.joint('hips', 'root', (0, 0, 0.26))
    r.joint('spine', 'hips', (0, 0, 0.30))
    r.joint('head', 'spine', (0, 0.0, 0.58))
    r.joint('crest', 'head', (0, 0.0, 1.02))
    r.joint('tailfin', 'hips', (0, 0.15, 0.22))
    for s, side in ((-1, 'R'), (1, 'L')):
        r.joint('arm_' + side, 'spine', (s * 0.17, 0, 0.52))
        r.joint('hand_' + side, 'arm_' + side, (s * 0.235, -0.01, 0.285))
        r.joint('leg_' + side, 'hips', (s * 0.09, 0, 0.24))
        r.joint('foot_' + side, 'leg_' + side, (s * 0.095, 0, 0.08))
        r.joint('gill_' + side, 'head', on_ell(FM_HC, FM_HR, s * 84, -8, -0.02)[0])

    # torso: scaly, a pale belly plate with scute grooves
    r.add(G.lathe('f_torso', FM_TORSO, 'scales', sy=FM_SY, seg=36), 'spine')

    def belly_front(z):
        rr = G.profile_r(FM_TORSO, z)
        return -rr * FM_SY
    r.add(G.ellipsoid('f_belly', (0, belly_front(0.39) + 0.055, 0.39), (0.13, 0.07, 0.17), 'skin2'), 'spine')
    for k, z in enumerate((0.33, 0.39, 0.45)):
        w = 0.105 - abs(z - 0.39) * 0.5
        pts = [(x, belly_front(z) - 0.0 + 0.055 - 0.07 * math.sqrt(max(0.0, 1 - (x / 0.13) ** 2)) *
                math.sqrt(max(0.0, 1 - ((z - 0.39) / 0.17) ** 2)) - 0.002, z) for x in (-w, -w / 2, 0, w / 2, w)]
        r.add(G.tube('f_scute%d' % k, pts, [0.007] * 5, 'skin', n=6, sub=3), 'spine')
    # dorsal fin down the back
    for ob in fan('f_dorsal', (0, 0.15, 0.40), (0, 1, 0.35), (0, 0, 1), 0.13, 0.75, 'fin', n_rays=4, scallop=0.35,
                  rays_key='skin'):
        r.add(ob, 'spine')

    # swim trunks with a stripe and a waistband
    trunks = [(0.0, 0.17), (0.140, 0.175), (0.182, 0.22), (0.192, 0.27), (0.186, 0.31), (0.0, 0.315)]
    r.add(G.lathe('f_trunks', trunks, 'c7', sy=FM_SY * 1.02, seg=36), 'hips')
    r.add(G.wrap('f_band', G.lathe_body_r(trunks, 1.0, FM_SY * 1.02), lambda th: 0.295, 0.028, 'c8', thick=0.010),
          'hips')
    r.add(G.wrap('f_stripe', G.lathe_body_r(trunks, 1.0, FM_SY * 1.02), lambda th: 0.24, 0.024, 'c8', thick=0.008),
          'hips')
    # a little tail fin at the back
    for ob in fan('f_tail', (0, 0.14, 0.2), (0, 1, -0.4), (0, 0, 1), 0.12, 0.6, 'fin', n_rays=4, scallop=0.4,
                  rays_key='skin'):
        r.add(ob, 'tailfin')

    for s, side in ((-1, 'R'), (1, 'L')):
        arm, hand, leg, foot = 'arm_' + side, 'hand_' + side, 'leg_' + side, 'foot_' + side
        # legs: short and thick, big webbed flipper feet
        r.add(G.tube('f_leg' + side, [(s * 0.09, 0, 0.24), (s * 0.094, 0, 0.16), (s * 0.095, 0, 0.09)],
                     [0.066, 0.064, 0.056], 'scales', n=14), leg)

        def flat_bottom(u):
            if u.z < -0.3:
                u.z = -0.3 + (u.z + 0.3) * 0.25
            return u
        r.add(G.ellipsoid('f_foot' + side, (s * 0.10, -0.06, 0.035), (0.085, 0.135, 0.036), 'scales',
                          deform=flat_bottom), foot)
        for k, dx in enumerate((-0.045, 0.0, 0.045)):
            b0 = V((s * 0.10 + dx, -0.05, 0.05))
            r.add(G.tube('f_toe%s%d' % (side, k), [b0, b0 + V((dx * 0.35, -0.13, -0.02))], [0.017, 0.012],
                         'skin2', n=8, sub=2), foot)
        # arms: a fin at the elbow, webbed three-finger hands
        r.add(G.tube('f_arm' + side, [(s * 0.165, 0, 0.52), (s * 0.21, -0.005, 0.40), (s * 0.235, -0.01, 0.30)],
                     [0.052, 0.046, 0.044], 'scales', n=14), arm)
        for ob in fan('f_elb' + side, (s * 0.215, 0.03, 0.43), (s * 0.3, 1, 0.2), (0, 0, 1), 0.075, 0.5, 'fin',
                      n_rays=3, scallop=0.4, rays_key='skin', ray_r=0.008):
            r.add(ob, arm)
        hc = V((s * 0.238, -0.018, 0.25))
        r.add(G.ellipsoid('f_hand' + side, hc, (0.054, 0.048, 0.062), 'skin2'), hand)
        tips = []
        for k, a in enumerate((-35, 0, 35)):
            d = V((s * math.sin(math.radians(a)) * 0.6, -0.55, -math.cos(math.radians(a)) * 0.8)).normalized()
            tip = hc + d * 0.115
            tips.append(tip)
            r.add(G.tube('f_fing%s%d' % (side, k), [hc + d * 0.03, tip], [0.021, 0.015], 'skin2', n=8, sub=1),
                  hand)
        # webbing between the fingers (skin B)
        web = G.mesh('f_web' + side, [hc + (tips[0] - hc) * 0.3, tips[0] * 0.8 + hc * 0.2, tips[1] * 0.8 + hc * 0.2,
                                      tips[2] * 0.8 + hc * 0.2, hc + (tips[2] - hc) * 0.3],
                     [(0, 1, 2), (0, 2, 3), (0, 3, 4)], 'skin2', smooth=True, recalc=False)
        G.solidify(web, 0.012)
        r.add(web, hand)

    # the head: wide, froggy, big bulging glowing eyes
    hc, hr = FM_HC, FM_HR
    r.add(G.ellipsoid('f_head', hc, hr, 'scales', seg=36, rings=18), 'head')
    # a jowly lower face (skin B lips/chin)
    r.add(G.ellipsoid('f_chin', hc + V((0, -0.07, -0.085)), (0.215, 0.17, 0.12), 'skin2', seg=30, rings=14), 'head')
    for s in (-1, 1):
        eye(r, 'head', 'f_eye%d' % s, hc, hr, s * 32, 16, 0.092, squash=1.0, rot=0, out=0.035,
            pupil=(0.05 * s, -0.05, 0.030), lid=('scales', 0.18))
    # a wide grin and two little fangs
    grin = path_on(hc + V((0, -0.07, -0.085)), (0.215, 0.17, 0.12),
                   [(-50, 8), (-30, -2), (-12, -8), (0, -9), (12, -8), (30, -2), (50, 8)], 0.004)
    r.add(G.tube('f_grin', grin, [0.012, 0.018, 0.022, 0.023, 0.022, 0.018, 0.012], 'dark', n=8, sub=3), 'head')
    for s in (-1, 1):
        p, n = on_ell(hc + V((0, -0.07, -0.085)), (0.215, 0.17, 0.12), s * 16, -5, 0.004)
        r.add(G.tube('f_fang%d' % s, [p + V((0, -0.004, 0.006)), p + V((0, -0.012, -0.04))], [0.016, 0.0], 'white',
                     n=8, sub=1), 'head')
    for s in (-1, 1):
        p, n = on_ell(hc, hr, s * 9, 2, 0.0)
        r.add(G.ellipsoid('f_nostril%d' % s, p, (0.012, 0.008, 0.008), 'dark', seg=10, rings=6), 'head')
    # the crest: a scalloped fin along the top of the head, front to back
    top = [on_ell(hc, hr, 0 if el <= 90 else 180, el if el <= 90 else 180 - el, -0.01) for el in
           (48, 64, 80, 96, 112, 128)]

    def crest(u, v):
        k = u * (len(top) - 1)
        i = min(int(k), len(top) - 2)
        f = k - i
        p = top[i][0].lerp(top[i + 1][0], f)
        n = top[i][1].lerp(top[i + 1][1], f).normalized()
        h = 0.19 * (0.55 + 0.45 * math.sin(math.pi * u)) * (1 - 0.35 * (1 - math.cos(u * 5 * TAU)) * 0.5)
        return p + n * (h * v)
    r.add(G.surface('f_crest', crest, 26, 5, 'fin', thick=0.016), 'crest')
    for k in range(len(top)):
        p, n = top[k]
        h = 0.20 * (0.55 + 0.45 * math.sin(math.pi * k / (len(top) - 1)))
        r.add(G.tube('f_spine%d' % k, [p, p + n * h], [0.011, 0.0], 'skin', n=6, sub=1), 'crest')
    # cheek fins (gills) that flare
    for s, side in ((-1, 'R'), (1, 'L')):
        base = on_ell(hc, hr, s * 84, -8, -0.02)[0]
        for ob in fan('f_gill' + side, base, (s * 1.0, 0.2, 0.3), (0, 0, 1), 0.21, 0.85, 'fin', n_rays=5,
                      scallop=0.3, rays_key='skin', ray_r=0.012, thick=0.016):
            r.add(ob, 'gill_' + side)
    # a strand of seaweed draped over the head (accessory A)
    sw = path_on(hc, (hr[0] * 1.03, hr[1] * 1.03, hr[2] * 1.03), [(-60, 60), (-80, 40), (-96, 12)], 0.012)
    sw.append(sw[-1] + V((-0.02, 0.0, -0.14)))
    r.add(G.ribbon('f_weed', sw, [0.05, 0.06, 0.05, 0.03], 'a11', hint=lambda s: (1, 0, 0), thick=0.01), 'head')
    return r


def pose_fishman(anim, f, n, d='s'):
    ph = TAU * f / n
    if anim == 'idle':
        b = math.sin(ph)
        # hunched, arms up in front with the webbed hands spread ("rawr"),
        # gills flare on the second frame
        k = f % 2
        return {'spine': (10 + 2 * b, 0, 0), 'spine@': (0, 0, -0.004 * k), 'head': (-6 - 3 * k, 0, 0),
                'arm_R': (-84 - 5 * k, 0, -18), 'arm_L': (-84 - 5 * k, 0, 18), 'hand_R': (-10, 0, 0),
                'hand_L': (-10, 0, 0), 'gill_R': (0, 0, -14 * k), 'gill_L': (0, 0, 14 * k),
                'crest': (-4 * k, 0, 0), 'tailfin': (0, 0, 10 * (1 - 2 * k)),
                'leg_R': (0, 0, -4), 'leg_L': (0, 0, 4)}
    if anim == 'walk':
        s = math.sin(ph)
        c = math.cos(ph)
        # a flat-footed waddle: the body rolls onto each flipper, the lifted
        # flipper hangs toes-down, arms swing up in front
        return {'spine': (9, 0, 7 * s), 'spine@': (0, 0, -0.014 * abs(c)), 'hips': (0, 8 * s, 0),
                'hips@': (0, 0, 0.012 * abs(s)),
                'head': (-4 + 3 * abs(s), -6 * s, 0),
                'leg_R': (28 * s, 0, -3), 'leg_L': (-28 * s, 0, 3),
                'foot_R': (-18 * max(0.0, s) + 8 * max(0.0, -s), 0, 0),
                'foot_L': (-18 * max(0.0, -s) + 8 * max(0.0, s), 0, 0),
                'arm_R': (-80 - 16 * s, 0, -18), 'arm_L': (-80 + 16 * s, 0, 18),
                'hand_R': (-10, 0, 0), 'hand_L': (-10, 0, 0),
                'gill_R': (0, 0, -6 * c), 'gill_L': (0, 0, 6 * c), 'crest': (4 * c, 0, 0),
                'tailfin': (0, 0, 18 * s)}
    if anim == 'emerge':
        # 00 only the crest and the glowing eyes above the water (also the
        # 'lurking' state), 01 the head up, hands on the surface, 02 heaving
        # out, arms up and wide, 03 out, standing in the shallows
        z = [-0.80, -0.50, -0.26, 0.0][f]
        return {'hips@': (0, 0, z), 'spine': ([14, 6, -6, 6][f], 0, 0),
                'head': ([-24, -12, 4, -4][f], 0, 0),
                'arm_R': ([-10, -40, -95, -70][f], 0, [-40, -60, -75, -28][f]),
                'arm_L': ([-10, -40, -95, -70][f], 0, [40, 60, 75, 28][f]),
                'hand_R': ([0, 30, -10, -20][f], 0, 0), 'hand_L': ([0, 30, -10, -20][f], 0, 0),
                'gill_R': (0, 0, [0, -10, -18, -6][f]), 'gill_L': (0, 0, [0, 10, 18, 6][f]),
                'leg_R': ([30, 20, 10, 0][f], 0, -4), 'leg_L': ([-20, -10, 0, 0][f], 0, 4)}
    if anim == 'lurk':
        # hidden in the water: the crest, the frills and the glowing eyes
        # above the surface, bobbing; the frills flare on 01
        return {'hips@': (0, 0, [-0.80, -0.83][f]), 'spine': (14, 0, 0), 'head': ([-24, -20][f], 0, [0, 4][f]),
                'arm_R': (-10, 0, -40), 'arm_L': (-10, 0, 40),
                'gill_R': (0, 0, [0, -16][f]), 'gill_L': (0, 0, [0, 16][f]), 'crest': ([0, -6][f], 0, 0),
                'leg_R': (30, 0, -4), 'leg_L': (-20, 0, 4)}
    if anim == 'dive':
        # 00 crouch, arms over the head; 01 leaping forward; 02 head first
        # into the water, flippers up; 03 only the flippers and the splash
        return {'hips': ([6, 50, 120, 175][f], 0, 0), 'hips@': ([(0, 0, -0.06), (0, -0.10, -0.12),
                                                               (0, -0.20, -0.22), (0, -0.24, -0.40)][f]),
                'spine': ([28, 6, 0, 0][f], 0, 0), 'head': ([-10, 10, 10, 10][f], 0, 0),
                'arm_R': ([-150, -170, -175, -175][f], 0, [-14, -8, -6, -6][f]),
                'arm_L': ([-150, -170, -175, -175][f], 0, [14, 8, 6, 6][f]),
                'hand_R': (0, 0, 0), 'hand_L': (0, 0, 0),
                'leg_R': ([-40, 10, 20, 26][f], 0, -4), 'leg_L': ([-40, 16, 26, 30][f], 0, 4),
                'foot_R': ([20, 30, 40, 40][f], 0, 0), 'foot_L': ([20, 34, 44, 44][f], 0, 0),
                'gill_R': (0, 0, -10), 'gill_L': (0, 0, 10), 'tailfin': (0, 0, [0, 12, -12, 12][f])}
    return None


def splash_extras(anim, f):
    """Per frame, in model space around the root: the foam ring where he
    breaks the surface and splash drops (id 13)."""
    tab = {'emerge': ([0.30, 0.38, 0.44, 0.36], [0, 3, 6, 0], [0, 0.8, 1.2, 0]),
           'lurk': ([0.30, 0.33], [0, 0], [0, 0]),
           'dive': ([0.0, 0.28, 0.36, 0.38], [0, 3, 6, 4], [0, 0.8, 1.2, 0.6])}[anim]
    rr, drops_n, hz = tab[0][f], tab[1][f], tab[2][f]
    objs = []
    if rr > 0:
        ring = [(rr * math.cos(t), rr * 0.9 * math.sin(t) - (0.2 if anim == 'dive' else 0.0),
                 0.012 + 0.012 * math.sin(5 * t)) for t in [TAU * k / 40 for k in range(41)]]
        objs.append(G.tube('fx_ring', ring, [0.035] * 41, 'foam', n=8, sub=1, cap0=0, cap1=0))
    drops = [(0.30, -0.25, 0.25), (-0.32, -0.2, 0.32), (0.4, 0.05, 0.4), (-0.42, 0.1, 0.2), (0.1, -0.4, 0.15),
             (-0.12, -0.38, 0.42)]
    for k, (x, y, z) in enumerate(drops[:drops_n]):
        sz = 0.03 + 0.012 * (k % 3)
        yy = y - (0.2 if anim == 'dive' else 0.0)
        objs.append(G.ellipsoid('fx_drop%d' % k, (x, yy, z * hz), (sz, sz, sz * 1.4), 'foam', seg=10, rings=6))
    return objs


# ---------------------------------------------------------------------------
# Giant crab
# ---------------------------------------------------------------------------

def build_crab():
    r = G.Rig('crab')
    r.joint('body', 'root', (0, 0, 0.30))
    for s, side in ((-1, 'R'), (1, 'L')):
        r.joint('claw_' + side, 'body', (s * 0.30, -0.10, 0.34))
        r.joint('pinc_' + side, 'claw_' + side, (s * 0.40, -0.32, 0.52))
        r.joint('jaw_' + side, 'pinc_' + side, (s * 0.40, -0.40, 0.52))
        r.joint('stalk_' + side, 'body', (s * 0.08, -0.12, 0.40))
    # the shell: wide, flat, a rim of bumps and a lighter underside
    shell_c, shell_r = V((0, 0.02, 0.32)), (0.36, 0.27, 0.15)

    def lumpy(u):
        u.z *= 1.0 if u.z > 0 else 0.7
        return u
    r.add(G.ellipsoid('c_shell', shell_c, shell_r, 'skin', seg=40, rings=18, deform=lumpy), 'body')
    r.add(G.ellipsoid('c_belly', shell_c + V((0, -0.01, -0.05)), (0.33, 0.24, 0.10), 'skin2', seg=32, rings=12),
          'body')
    for k in range(9):
        az = -80 + 20 * k
        p, n = on_ell(shell_c, shell_r, az, 8, -0.02)
        r.add(G.ellipsoid('c_bump%d' % k, p, (0.035, 0.035, 0.03), 'skin', rot=track(n), seg=12, rings=6), 'body')
    for k, (az, el) in enumerate(((-30, 55), (25, 60), (0, 75), (-55, 40), (55, 42))):
        p, n = on_ell(shell_c, shell_r, az, el, -0.01)
        r.add(G.ellipsoid('c_spot%d' % k, p, (0.03, 0.03, 0.012), 'skin2', rot=track(n), seg=12, rings=6), 'body')
    # face: a little frowny mouth under the rim
    mouth = path_on(shell_c, shell_r, [(-18, -18), (-6, -24), (6, -24), (18, -18)], 0.004)
    r.add(G.tube('c_mouth', mouth, [0.012, 0.016, 0.016, 0.012], 'dark', n=8, sub=3), 'body')
    # eye stalks with glowing eyes
    for s, side in ((-1, 'R'), (1, 'L')):
        b = V((s * 0.08, -0.12, 0.40))
        t = b + V((s * 0.03, -0.02, 0.18))
        r.add(G.tube('c_stalk' + side, [b, t], [0.028, 0.022], 'skin2', n=10), 'stalk_' + side)
        ec, er = t + V((0, 0, 0.04)), (0.058, 0.058, 0.062)
        r.add(G.ellipsoid('c_eye' + side, ec, er, 'glow', seg=20, rings=10), 'stalk_' + side)
        r.add(G.ellipsoid('c_pup' + side, ec + V((-s * 0.005, -0.05, 0.004)), (0.022, 0.012, 0.026), 'dark', seg=12,
                          rings=6), 'stalk_' + side)
    # legs: three a side, jointed, standing
    for s, side in ((-1, 'R'), (1, 'L')):
        for k, ang in enumerate((-10, 20, 50)):
            a = math.radians(ang)
            b = V((s * 0.26 * math.cos(a * 0.5), 0.02 + 0.2 * math.sin(a), 0.28))
            knee = b + V((s * 0.18, 0.08 * math.sin(a), 0.10))
            foot = knee + V((s * 0.10, 0.04 * math.sin(a), -0.38))
            j = 'leg_%s%d' % (side, k)
            r.joint(j, 'root', b)
            r.add(G.tube('c_leg%s%d' % (side, k), [b, knee, foot], [0.034, 0.03, 0.0], 'skin', n=10, sub=4), j)
            r.add(G.ellipsoid('c_knee%s%d' % (side, k), knee, (0.034, 0.034, 0.034), 'skin2', seg=12, rings=6), j)
    # claws: an arm and a big pincer (the upper fixed finger + a moving jaw)
    for s, side in ((-1, 'R'), (1, 'L')):
        big = 1.18 if s < 0 else 1.0
        sh = V((s * 0.30, -0.10, 0.34))
        el = V((s * 0.40, -0.26, 0.44))
        r.add(G.tube('c_arm' + side, [sh, el, V((s * 0.40, -0.32, 0.52))], [0.045, 0.042, 0.045], 'skin', n=12),
              'claw_' + side)
        pc = V((s * 0.40, -0.40, 0.56))
        r.add(G.ellipsoid('c_palm' + side, pc, (0.10 * big, 0.12 * big, 0.085 * big), 'skin', seg=24, rings=12),
              'pinc_' + side)
        f0 = pc + V((0, -0.08 * big, 0.03))
        r.add(G.tube('c_fix' + side, [f0, f0 + V((0, -0.10, 0.02)) * big, f0 + V((s * -0.02, -0.17, -0.03)) * big],
                     [0.05 * big, 0.04 * big, 0.0], 'skin', n=12), 'pinc_' + side)
        r.add(G.ellipsoid('c_tip' + side, f0 + V((s * -0.02, -0.15, -0.02)) * big, (0.025, 0.03, 0.025), 'skin2',
                          seg=10, rings=6), 'pinc_' + side)
        j0 = pc + V((0, -0.06 * big, -0.04))
        r.add(G.tube('c_jaw' + side, [j0, j0 + V((0, -0.09, -0.04)) * big, j0 + V((s * -0.02, -0.15, 0.0)) * big],
                     [0.04 * big, 0.032 * big, 0.0], 'skin', n=12), 'jaw_' + side)
        for k in range(2):
            t0 = j0 + V((0, -0.05 - 0.04 * k, -0.01)) * big
            r.add(G.tube('c_tooth%s%d' % (side, k), [t0, t0 + V((0, 0, 0.03))], [0.012, 0.0], 'white', n=6, sub=1),
                  'jaw_' + side)
    return r


CRAB_A = ('leg_R0', 'leg_R2', 'leg_L1')      # the two leg groups that step in turn
CRAB_B = ('leg_R1', 'leg_L0', 'leg_L2')


def pose_crab(anim, f, n, d='s'):
    ph = TAU * f / n
    if anim == 'idle':
        # claws up and ready, the big one open; 01 the claws bob and the
        # eye stalks look the other way
        k = f % 2
        return {'claw_R': (-20 - 6 * k, 0, -8), 'claw_L': (-12 - 6 * k, 0, 6), 'jaw_R': (32 - 14 * k, 0, 0),
                'jaw_L': (8 + 14 * k, 0, 0), 'pinc_R': (-10, 0, 0), 'pinc_L': (-6, 0, 0),
                'stalk_R': (0, [-8, 8][k], 0), 'stalk_L': (0, [10, -6][k], 0), 'body@': (0, 0, -0.008 * k)}
    if anim == 'walk':
        # scuttling sideways (along the model's X): the two leg groups swing
        # in turn, the body rocks, the claws bob
        sn, c = math.sin(ph), math.cos(ph)
        P = {'body': (0, 4 * sn, 0), 'body@': (0.012 * sn, 0, 0.02 * abs(c)),
             'claw_R': (-18 + 8 * c, 0, -8), 'claw_L': (-12 - 8 * c, 0, 6), 'jaw_R': (24 + 10 * sn, 0, 0),
             'jaw_L': (10 - 10 * sn, 0, 0), 'pinc_R': (-10, 0, 0), 'pinc_L': (-6, 0, 0),
             'stalk_R': (0, -6 * sn, 0), 'stalk_L': (0, -6 * sn, 0)}
        for j in CRAB_A:
            P[j] = (0, 20 * sn, 0)
            P[j + '@'] = (0, 0, 0.03 * max(0.0, c))
        for j in CRAB_B:
            P[j] = (0, -20 * sn, 0)
            P[j + '@'] = (0, 0, 0.03 * max(0.0, -c))
        return P
    if anim == 'snap':
        # 00 claws raised wide open, 01 thrust out to both sides (the cells
        # beside it), 02 snapped shut, fully out
        return {'claw_R': ([-40, -10, -8][f], 0, [-20, -72, -78][f]), 'claw_L': ([-40, -10, -8][f], 0, [20, 72, 78][f]),
                'claw_R@': [(0, 0, 0.02), (-0.06, 0, 0.0), (-0.10, 0, 0.0)][f],
                'claw_L@': [(0, 0, 0.02), (0.06, 0, 0.0), (0.10, 0, 0.0)][f],
                'jaw_R': ([40, 40, -4][f], 0, 0), 'jaw_L': ([40, 40, -4][f], 0, 0),
                'pinc_R': ([-20, 0, 0][f], 0, 0), 'pinc_L': ([-20, 0, 0][f], 0, 0),
                'body@': (0, 0, [0.01, -0.01, -0.015][f]), 'stalk_R': (0, -12, 0), 'stalk_L': (0, 12, 0)}
    return None


# ---------------------------------------------------------------------------
# Jellyfish
# ---------------------------------------------------------------------------

JZ = 0.52        # the bell's rim height over the water


def build_jelly():
    r = G.Rig('jelly')
    r.joint('bell', 'root', (0, 0, JZ))
    # the bell: a soft dome, a scalloped frill rim, rim lights
    prof = [(0.0, JZ + 0.34), (0.12, JZ + 0.325), (0.22, JZ + 0.27), (0.29, JZ + 0.17), (0.32, JZ + 0.07),
            (0.315, JZ + 0.02), (0.26, JZ - 0.005), (0.0, JZ + 0.02)]
    r.add(G.lathe('j_bell', prof, 'bell', seg=40), 'bell')

    def scallop(th, z, i):
        return 1.0 + 0.06 * math.cos(8 * th)
    r.add(G.lathe('j_frill', [(0.30, JZ - 0.01), (0.34, JZ - 0.035), (0.36, JZ - 0.05)], 'frill', seg=48,
                  rfn=scallop, cap_bot=False, cap_top=False, thick=0.012), 'bell')
    for k in range(8):
        t = TAU * k / 8 + TAU / 16
        p = V((0.335 * math.cos(t), 0.335 * math.sin(t), JZ - 0.03))
        r.add(G.ellipsoid('j_light%d' % k, p, (0.024, 0.024, 0.024), 'glow', seg=10, rings=6), 'bell')
    # glowing inner pattern on the dome: four crescents
    hc, hr = V((0, 0, JZ + 0.02)), (0.315, 0.315, 0.32)
    for k in range(4):
        az = 90 * k + 45
        p, n = on_ell(hc, hr, az, 52, 0.002)
        r.add(G.ellipsoid('j_spot%d' % k, p, (0.05, 0.05, 0.012), 'glow', rot=track(n), seg=14, rings=6), 'bell')
    # a cute face
    for s in (-1, 1):
        p, n = on_ell(hc, hr, s * 22, 26, 0.002)
        r.add(G.ellipsoid('j_eye%d' % s, p, (0.032, 0.04, 0.012), 'dark', rot=track(n), seg=14, rings=6), 'bell')
        r.add(G.ellipsoid('j_shine%d' % s, p + n * 0.006 + V((s * -0.008, -0.004, 0.012)), (0.009, 0.009, 0.004),
                          'glow', rot=track(n), seg=8, rings=4), 'bell')
    smile = path_on(hc, hr, [(-12, 12), (-5, 8), (5, 8), (12, 12)], 0.003)
    r.add(G.tube('j_smile', smile, [0.007, 0.009, 0.009, 0.007], 'dark', n=6, sub=3), 'bell')
    # tentacles and oral arms
    for k in range(10):
        t = TAU * k / 10
        rr = 0.27
        b = V((rr * math.cos(t), rr * math.sin(t), JZ - 0.02))
        pts = [b]
        for j in range(1, 6):
            z = JZ - 0.02 - 0.088 * j
            w = 0.04 * math.sin(j * 1.3 + k)
            pts.append(V(((rr + w) * math.cos(t + 0.1 * j), (rr + w) * math.sin(t + 0.1 * j), z)))
        r.add(G.tube('j_tent%d' % k, pts, [0.024, 0.021, 0.018, 0.015, 0.012, 0.0], 'frill', n=8, sub=3), 'bell')
        r.add(G.ellipsoid('j_tip%d' % k, pts[-2], (0.022, 0.022, 0.022), 'glow', seg=10, rings=6), 'bell')
    for k in range(4):
        t = TAU * k / 4 + 0.4
        pts = [V((0.06 * math.cos(t), 0.06 * math.sin(t), JZ))]
        for j in range(1, 5):
            pts.append(V((0.08 * math.cos(t + 0.35 * j), 0.08 * math.sin(t + 0.35 * j), JZ - 0.09 * j)))
        r.add(G.ribbon('j_arm%d' % k, pts, [0.06, 0.07, 0.06, 0.04, 0.0], 'frill',
                       hint=lambda s, t=t: (math.cos(t), math.sin(t), 0), thick=0.01), 'bell')
    return r


def pose_jelly(anim, f, n, d='s'):
    if anim in ('drift', 'float'):
        # a slow pulse: the bell squeezes and bobs up, tilting a little
        ph = TAU * f / n
        sq = math.sin(ph)
        return {'bell': (6 * math.cos(ph), -4, 0), 'bell@': (0, 0, 0.035 * (1 + math.sin(ph - 0.8))),
                'bell*': (1 + 0.05 * sq, 1 + 0.05 * sq, 1 - 0.06 * sq)}
    return None


# ---------------------------------------------------------------------------
# Piranha school (a boiling surface overlay, one cell)
# ---------------------------------------------------------------------------

def piranha_fish(tag, c, heading, pitch, size, jaw_open=True):
    """A cartoon piranha: a deep round body, a red belly, an underbite of
    teeth, a glowing eye, a forked tail. heading in degrees (0 = towards -Y)."""
    objs = []
    R = rotm((pitch, 0, heading))
    c = V(c)
    k = size
    objs.append(G.ellipsoid(tag + 'body', c, (0.05 * k, 0.11 * k, 0.085 * k), 'skin', rot=R, seg=22, rings=12))
    objs.append(G.ellipsoid(tag + 'belly', c + R @ V((0, -0.01 * k, -0.035 * k)), (0.046 * k, 0.09 * k, 0.05 * k),
                            'skin2', rot=R, seg=18, rings=10))
    if jaw_open:
        objs.append(G.ellipsoid(tag + 'mouth', c + R @ V((0, -0.10 * k, -0.01 * k)), (0.035 * k, 0.03 * k, 0.04 * k),
                                'dark', rot=R, seg=14, rings=8))
    for s in (-1, 1):
        for j in range(2):
            b = c + R @ V((s * 0.014 * k * (j + 0.5), -0.118 * k, (-0.034 if j else 0.024) * k))
            tip = b + R @ V((0, -0.012 * k, (0.034 if j else -0.034) * k))
            objs.append(G.tube(tag + 'tooth%d%d' % (s, j), [b, tip], [0.014 * k, 0.0], 'white', n=6, sub=1))
        objs.append(G.ellipsoid(tag + 'eye%d' % s, c + R @ V((s * 0.042 * k, -0.06 * k, 0.025 * k)),
                                (0.014 * k, 0.018 * k, 0.018 * k), 'glow', rot=R, seg=10, rings=6))
    tail = c + R @ V((0, 0.11 * k, 0))
    for s in (-1, 1):
        objs.append(G.tube(tag + 'tail%d' % s, [tail, tail + R @ V((0, 0.07 * k, s * 0.06 * k))],
                           [(0.01 * k, 0.035 * k), 0.0], 'skin', n=8, sub=1, nrm=tuple(R @ V((1, 0, 0)))))
    objs.append(G.tube(tag + 'dorsal', [c + R @ V((0, 0.0, 0.08 * k)), c + R @ V((0, 0.05 * k, 0.12 * k))],
                       [(0.008 * k, 0.03 * k), 0.0], 'skin', n=8, sub=1, nrm=tuple(R @ V((1, 0, 0)))))
    return objs


def build_piranha():
    """Not a rig: a still of one cell of boiling water."""
    import random
    rnd = random.Random(5)
    r = G.Rig('piranha')
    objs = []
    # the churning foam: lumpy blobs over most of the cell, flat on the water
    for k in range(26):
        x, y = rnd.uniform(-0.4, 0.4), rnd.uniform(-0.4, 0.4)
        s = rnd.uniform(0.07, 0.13)
        objs.append(G.ellipsoid('p_foam%d' % k, (x, y, 0.0), (s, s * 0.9, s * 0.45), 'foam', seg=12, rings=6))
    for k in range(14):
        x, y = rnd.uniform(-0.42, 0.42), rnd.uniform(-0.42, 0.42)
        s = rnd.uniform(0.02, 0.035)
        objs.append(G.ellipsoid('p_bub%d' % k, (x, y, rnd.uniform(0.04, 0.2)), (s, s, s), 'foam', seg=10, rings=6))
    # piranhas: two leaping clear, two half out with jaws up, one fin
    objs += piranha_fish('p1', (-0.16, 0.08, 0.40), 25, 30, 1.7)
    objs += piranha_fish('p2', (0.22, -0.14, 0.24), -30, 22, 1.6)
    objs += piranha_fish('p3', (-0.14, -0.28, 0.06), 10, -65, 1.5)
    for ob in objs:
        r.add(ob, 'root')
    return r


def pose_piranha(anim, f, n, d='s'):
    return {}


# ---------------------------------------------------------------------------
# Kraken (boss concept still)
# ---------------------------------------------------------------------------

def tentacle(name, pts, r0, r1, key='skin', suck=True, sucker_side=None, nsuck=8):
    """A tapering tentacle through pts with a row of suckers (skin B) on the
    side facing `sucker_side` (a vector) or the underside."""
    objs = [G.tube(name, pts, [r0 + (r1 - r0) * k / (len(pts) - 1) for k in range(len(pts))], key, n=16, sub=5)]
    if suck:
        P = [V(p) for p in pts]
        samp = G._catmull(P, 6)
        cs = [s_[0] for s_ in samp]
        m = len(cs)
        for j in range(nsuck):
            t = 0.12 + 0.8 * j / max(1, nsuck - 1)
            i = int(t * (m - 1))
            a, b = cs[max(i - 1, 0)], cs[min(i + 1, m - 1)]
            T_ = (b - a).normalized()
            sd = V(sucker_side) if sucker_side is not None else V((0, 0, -1))
            nrm = (sd - T_ * sd.dot(T_)).normalized()
            rr = r0 + (r1 - r0) * t
            p = cs[i] + nrm * rr * 0.92
            s = rr * 0.42
            objs.append(G.ellipsoid(name + '_s%d' % j, p, (s, s, s * 0.35), 'skin2', rot=track(nrm), seg=12,
                                    rings=6))
    return objs


RAISED = {
    'hero': [(0.5, -0.35, 0.2), (0.95, -0.55, 0.65), (1.1, -0.6, 1.3), (1.0, -0.45, 1.9), (0.75, -0.25, 2.25),
             (0.55, -0.2, 2.2), (0.5, -0.3, 2.0)],
    # slam: 00 rising, 01 high, 02 poised curled back, 03 whipping down to
    # the right (+X, towards the front), 04 slammed flat, 05 lifting off
    'slam': [
        [(0.5, -0.35, 0.2), (0.9, -0.5, 0.5), (1.1, -0.55, 0.95), (1.15, -0.5, 1.4), (1.05, -0.4, 1.7),
         (0.95, -0.35, 1.75)],
        [(0.5, -0.35, 0.2), (0.9, -0.45, 0.7), (1.0, -0.4, 1.4), (0.9, -0.2, 2.0), (0.7, 0.0, 2.3),
         (0.5, 0.05, 2.25), (0.45, -0.05, 2.1)],
        [(0.5, -0.35, 0.2), (0.85, -0.35, 0.75), (0.9, -0.2, 1.5), (0.7, 0.1, 2.05), (0.42, 0.32, 2.3),
         (0.2, 0.4, 2.2)],
        [(0.5, -0.35, 0.2), (0.95, -0.5, 0.6), (1.25, -0.6, 0.95), (1.5, -0.65, 1.0), (1.65, -0.6, 0.8)],
        [(0.5, -0.4, 0.15), (0.9, -0.55, 0.14), (1.25, -0.65, 0.12), (1.55, -0.7, 0.12), (1.75, -0.7, 0.16)],
        [(0.5, -0.35, 0.2), (0.9, -0.5, 0.35), (1.2, -0.6, 0.5), (1.4, -0.6, 0.55), (1.5, -0.55, 0.45)],
    ],
}


def build_kraken(anim='hero', f=0, n=1):
    """The Kraken, built for one frame (the tentacles are curves, not a rig):
    anim 'hero' (the approved still), 'idle' (4: breathing, the loops and
    the raised tentacle sway, a blink on 02), 'slam' (6)."""
    r = G.Rig('kraken', anchor=V((1.0, 1.0, 0.0)))
    objs = []
    ph = TAU * f / max(n, 1)
    breathe = 1.0 + (0.03 * math.sin(ph) if anim == 'idle' else 0.0)
    mc, mr = V((0, 0.12, 0.72 * breathe)), (0.62 * breathe, 0.56 * breathe, 0.78 * breathe)

    def bulb(u):
        if u.z > 0:
            u.x *= 1.0 - 0.12 * u.z
            u.y *= 1.0 - 0.12 * u.z
        return u
    objs.append(G.ellipsoid('k_mantle', mc, mr, 'skin', rot=(-12, 0, 0), seg=44, rings=22, deform=bulb))
    import random
    rnd = random.Random(3)
    for k in range(12):
        az, el = rnd.uniform(-100, 100), rnd.uniform(20, 75)
        p, nn = on_ell(mc, mr, az, el, -0.004, rot=(-12, 0, 0))
        sz = rnd.uniform(0.04, 0.08)
        objs.append(G.ellipsoid('k_spot%d' % k, p, (sz, sz, 0.02), 'skin2', rot=track(nn), seg=14, rings=6))
    fc, fr = V((0, -0.14, 0.50)), (0.56, 0.42, 0.42)
    objs.append(G.ellipsoid('k_face', fc, fr, 'skin', seg=40, rings=18))
    blink = anim == 'idle' and f == 2
    angry = anim == 'slam' and f in (2, 3, 4)
    for s in (-1, 1):
        p, nn = on_ell(fc, fr, s * 30, 18, 0.03)
        R = track(nn)
        ctr = p - nn * 0.04
        objs.append(G.ellipsoid('k_eye%d' % s, ctr, (0.18, 0.18, 0.09), 'glow', rot=R, seg=24, rings=12))
        if not blink:
            objs.append(G.ellipsoid('k_pup%d' % s, ctr + R @ V((-s * 0.01, 0.0, 0.082)), (0.034, 0.095, 0.02), 'dark',
                                    rot=R, seg=14, rings=8))
            objs.append(G.ellipsoid('k_shine%d' % s, ctr + R @ V((s * 0.05, 0.06, 0.07)), (0.022, 0.022, 0.01),
                                    'white', rot=R, seg=10, rings=6))
        drop = 0.08 if blink else (0.025 if angry else 0.0)
        tilt = -s * (28 if angry else 18)
        objs.append(G.ellipsoid('k_brow%d' % s, ctr + R @ V((0, 0.12 - drop, 0.035 + drop * 0.4)),
                                (0.22, 0.09 + drop * 1.2, 0.10), 'skin', rot=R @ rotm((0, 0, tilt)), seg=20, rings=10))
    mouth = path_on(fc, fr, [(-22, -14), (-10, -20), (0, -21), (10, -20), (22, -14)], 0.004)
    objs.append(G.tube('k_mouth', mouth, [0.02, 0.028, 0.03, 0.028, 0.02], 'dark', n=8, sub=3))
    loops = [
        [(-0.55, -0.30, -0.1), (-0.85, -0.55, 0.35), (-1.05, -0.8, 0.30), (-1.15, -0.95, -0.1)],
        [(0.1, -0.55, -0.1), (0.05, -0.95, 0.3), (-0.1, -1.2, 0.25), (-0.2, -1.35, -0.1)],
        [(0.6, 0.35, -0.1), (0.95, 0.55, 0.4), (1.15, 0.8, 0.35), (1.25, 0.95, -0.1)],
        [(-0.55, 0.40, -0.1), (-0.9, 0.7, 0.35), (-1.0, 1.0, 0.2), (-1.05, 1.15, -0.1)],
    ]
    for k, pts in enumerate(loops):
        if anim in ('idle', 'slam'):
            w = math.sin(ph + k * 1.7) * (0.06 if anim == 'idle' else 0.03)
            pts = [pts[0], (pts[1][0], pts[1][1], pts[1][2] + w), (pts[2][0] + w, pts[2][1], pts[2][2] + w),
                   pts[3]]
        objs += tentacle('k_loop%d' % k, pts, 0.13, 0.05, sucker_side=(0, 0, -1), nsuck=6)
    if anim == 'slam':
        raised = RAISED['slam'][f]
        side = (0, -0.3, -1) if f in (3, 4, 5) else (-1, -0.4, 0)
    else:
        raised = RAISED['hero']
        side = (-1, -0.4, 0)
        if anim == 'idle':
            sw = math.sin(ph)
            raised = [(x + 0.07 * sw * (z / 2.2), y, z + 0.04 * math.cos(ph) * (z / 2.2)) for x, y, z in raised]
    objs += tentacle('k_raised', raised, 0.15, 0.03, sucker_side=side, nsuck=9)
    curl = [(-0.55, -0.05, 0.2), (-0.95, -0.2, 0.55), (-1.05, -0.3, 1.0), (-0.9, -0.35, 1.25), (-0.72, -0.3, 1.18),
            (-0.72, -0.25, 1.0)]
    if anim in ('idle', 'slam'):
        sw = math.sin(ph + 1.0) * 0.06
        curl = [(x - sw * (z / 1.2), y, z) for x, y, z in curl]
    objs += tentacle('k_curl', curl, 0.13, 0.03, sucker_side=(1, 0, 0), nsuck=7)
    for k in range(18):
        t = TAU * k / 18
        rr = 0.66 + 0.06 * math.sin(3 * t + ph)
        objs.append(G.ellipsoid('k_foam%d' % k, (rr * math.cos(t), 0.08 + rr * 0.9 * math.sin(t), 0.0),
                                (0.12, 0.1, 0.04), 'foam', seg=12, rings=6))
    if anim == 'slam' and f == 4:
        # the splash where it hits
        for k in range(9):
            x = 0.8 + 0.12 * k
            for sgn in (-1, 1):
                objs.append(G.ellipsoid('k_spl%d_%d' % (k, sgn), (x, -0.6 + sgn * (0.2 + 0.03 * (k % 3)), 0.05),
                                        (0.09, 0.07, 0.06 + 0.03 * (k % 2)), 'foam', seg=10, rings=6))
    for ob in objs:
        r.add(ob, 'root')
    return r


# ---------------------------------------------------------------------------
# jobs and output
# ---------------------------------------------------------------------------

BUILD = {'fishman': build_fishman, 'crab': build_crab, 'jelly': build_jelly}
POSE = {'fishman': pose_fishman, 'crab': pose_crab, 'jelly': pose_jelly}
# who, anim, dirs, frames, ms, water (anchored at the water surface: a
# hold-out plane at the anchor hides what is under it)
JOBS = [
    ('fishman', 'lurk', 's', 2, 300, True),
    ('fishman', 'emerge', 's', 4, 100, True),
    ('fishman', 'idle', 'nesw', 2, 400, False),
    ('fishman', 'walk', 'nesw', 6, 110, False),
    ('fishman', 'dive', 's', 4, 100, True),
    ('fishman', 'card', 's', 1, 0, False),
    ('crab', 'idle', 'nesw', 2, 400, False),
    ('crab', 'walk', 'nesw', 6, 70, False),
    ('crab', 'snap', 'nesw', 3, 80, False),
    ('crab', 'card', 's', 1, 0, False),
    ('jelly', 'float', 's', 4, 150, True),
    ('jelly', 'card', 's', 1, 0, False),
    ('piranha', 'card', 's', 1, 0, False),
    ('kraken', 'idle', 's', 4, 150, True),
    ('kraken', 'slam', 's', 6, 80, True),
    ('kraken', 'card', 's', 1, 0, False),
]
CARD_POSE = {'fishman': ('idle', 0, 2), 'crab': ('idle', 0, 2), 'jelly': ('float', 0, 4), 'kraken': ('idle', 0, 4)}
CRAB_CRAB = 0.85          # the crab is built at 0.85 of the sample's size (memory budget)
BOSSES = ('kraken',)


def merge_palettes(out, whos):
    """Add (or refresh) only our entries in <out>/palettes.json: re-read it
    right before writing, keep everything else (other agents write it too)."""
    path = os.path.join(os.path.abspath(out), 'palettes.json')
    pal = {}
    if os.path.exists(path):
        with open(path) as fh:
            pal = json.load(fh)
    has_ids = '_ids' in pal
    for who in whos:
        pal[who] = {str(k): list(v) for k, v in sorted(PALETTES[who].items())}
        if has_ids:
            pal['_ids'][who] = {str(k): v for k, v in IDS[who].items()}
    with open(path, 'w') as fh:
        json.dump(pal, fh, indent=1)


def holdout_plane(anchor, z=-0.002, s=4.0):
    me = bpy.data.meshes.new('water_cut')
    ax, ay, az = anchor
    me.from_pydata([(ax - s, ay - s, az + z), (ax + s, ay - s, az + z), (ax + s, ay + s, az + z),
                    (ax - s, ay + s, az + z)], [], [(0, 1, 2, 3)])
    ob = bpy.data.objects.new('water_cut', me)
    C.link(ob)
    C.assign(ob, 'holdout')
    return ob


def piranha_card_rig():
    r = G.Rig('piranha')
    for ob in piranha_fish('pc', (0, 0, 0.32), 72, -30, 1.9):
        r.add(ob, 'root')
    return r


def drop_rig(rig):
    C.remove(list(rig.parts))
    for e in list(rig.J.values()):
        bpy.data.objects.remove(e, do_unlink=True)


def my_args():
    import argparse
    i = sys.argv.index('--') + 1 if '--' in sys.argv else len(sys.argv)
    argv = sys.argv[i:]
    ap = argparse.ArgumentParser(allow_abbrev=False)
    ap.add_argument('--dirs', default='')
    ap.add_argument('--bosses', default='')
    b, rest = ap.parse_known_args(argv)
    sys.argv[i:] = rest
    return b


def main():
    b = my_args()
    a = C.args()
    mon_out = os.path.abspath(a.out)
    boss_out = os.path.abspath(b.bosses or os.path.join(mon_out, '..', 'bosses'))
    t_all = time.time()
    count = 0
    cur = None
    rig = None
    for who, anim, dirs, n, ms, water in JOBS:
        if a.only and who not in a.only and '%s_%s' % (who, anim) not in a.only:
            continue
        out = boss_out if who in BOSSES else mon_out
        if who != cur:
            if cur is not None:
                C.save_meta(boss_out if cur in BOSSES else mon_out)
            C.reset('neutral', cpu=a.cpu)
            register_mats(who)
            rig = BUILD[who]() if who in BUILD else None
            if who == 'crab':
                rig.root.scale = (CRAB_CRAB,) * 3
            cur = who
        for d in dirs:
            if b.dirs and d not in b.dirs.split(','):
                continue
            for f in range(n):
                card = anim == 'card'
                extras = []
                if who == 'kraken':
                    pa, pf, pn = CARD_POSE['kraken'] if card else (anim, f, n)
                    if rig is not None:
                        drop_rig(rig)
                    rig = build_kraken(pa, pf, pn)
                elif who == 'piranha':
                    if rig is None:
                        rig = piranha_card_rig()
                else:
                    if card:
                        pa, pf, pn = CARD_POSE[who]
                        rig.pose(POSE[who](pa, pf, pn, 's'), 0.0)
                    else:
                        rig.pose(POSE[who](anim, f, n, d), YAW[d])
                    if who == 'fishman' and anim in ('lurk', 'emerge', 'dive'):
                        extras = splash_extras(anim, f)
                        for ob in extras:
                            ob.data.transform(Matrix.Translation(C.cell(0, 0, 0)))
                objs = rig.visible()
                anchor = rig.root.location.copy()
                hold = [holdout_plane(anchor)] if (water and not card) else []
                ids = sorted(IDS[who].keys())
                boss = who in BOSSES
                key = {'boss': who} if boss else {'monster': who}
                t0 = time.time()
                if card:
                    name = 'card_' + who
                    extra = dict(key, pose=('%s_s_%02d' % CARD_POSE[who][:2]) if who in CARD_POSE else 'leap',
                                 ids=ids, height_m=HEIGHT[who])
                    if boss:
                        extra.update(footprint=[2, 2], anim='idle', dir='s', frame=0)
                    info = C.render_sprite(out, name, objs, anchor, passes=('light', 'id'), bounce_ground=anchor.z,
                                           kind='card', samples=a.samples, zoom=1.2 if boss else 2.0, margin=4,
                                           extra=extra)
                else:
                    name = '%s_%s_%s_%02d' % (who, anim, d, f)
                    extra = dict(key, anim=anim, dir=d, frame=f, frames=n, ms=ms, height_m=HEIGHT[who], ids=ids)
                    if water:
                        extra['float_z'] = -0.18
                        extra['note'] = 'anchor = the water surface (the watch puts it at z = -0.18)'
                    if boss:
                        extra['footprint'] = [2, 2]
                        if anim == 'slam':
                            extra['slam_dir'] = '+x'
                    if who == 'crab' and anim == 'walk':
                        extra['moves_along'] = 'x' if d in 'ns' else 'y'
                    if who == 'crab' and anim == 'snap':
                        extra['hits'] = 'the cells beside it: along x facing n/s, along y facing e/w'
                    passes = ('light', 'id', 'z', 'shadow')
                    size = None
                    if who == 'jelly':
                        passes = passes + ('glow',)
                        import zones_df_lib as ZL
                        size = ZL.fit_pool(objs, anchor, (anchor.x, anchor.y), 1.1, 0.0)
                    info = C.render_sprite(out, name, objs + extras, anchor, passes=passes, holdout=hold,
                                           shadow_z=anchor.z, bounce_ground=anchor.z, kind='char',
                                           samples=a.samples, extra=extra, margin=2, size=size)
                    if who == 'jelly':
                        ZL.taper_glow(out, name, info, (anchor.x, anchor.y), 1.1)
                    ZP.clean_shadow(out, info)
                count += 1
                print('rendered %s %dx%d in %.1f s' % (name, info['w'], info['h'], time.time() - t0), flush=True)
                C.remove(extras + hold)
    if cur is not None:
        C.save_meta(boss_out if cur in BOSSES else mon_out)
    whos = sorted(set(j[0] for j in JOBS if not a.only or j[0] in a.only or '%s_%s' % (j[0], j[1]) in a.only))
    mons = [w for w in whos if w not in BOSSES]
    if mons:
        merge_palettes(mon_out, mons)
    if any(w in BOSSES for w in whos):
        merge_palettes(boss_out, [w for w in whos if w in BOSSES])
    print('done: %d frames in %.1f s' % (count, time.time() - t_all))


if __name__ == '__main__':
    main()
