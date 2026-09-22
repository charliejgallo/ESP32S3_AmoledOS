"""Monster Hop - the haunted suit of armour, shared by the castle's static
`armor` prop and the walking `armor` minion (SPEC 5: "it must look exactly
like the castle's static armor prop"). One geometry, one idle pose, used by
both scripts, so the player cannot tell which ones walk.

    import monsters_armor as A
    A.register_mats()                 # palette ids 1 2 3 5 11 12 (see PALETTE)
    rig = A.build()                   # a monsters_geo.Rig at C.cell(0, 0, 0)
    rig.pose(A.pose('idle', 0, 2), yaw)   # the prop is idle frame 0
    C.render_sprite(out, name, rig.parts, C.cell(0, 0, 0), ...)

For a final-colour prop (the castle's `color` pass under the castle light) the
materials carry the palette below as base colours and the visor glow emits
its colour; for the minion (light + id under the neutral light) the watch
recolours the same regions with the same palette. Built at cell (0, 0) on
floor 0, facing -Y, inside the cell (x, y within 0.05..0.95).
"""
import math

import mh_common as C
import monsters_geo as G
from monsters_geo import V, rotm, track, TAU

HEIGHT_M = 1.20

# sRGB 0-255, per region id
PALETTE = {1: (178, 188, 206), 2: (112, 118, 142), 3: (24, 20, 34), 5: (110, 235, 255),
           11: (214, 42, 70), 12: (108, 62, 176)}
IDS = {1: 'steel', 2: 'darker plates (pauldrons, cops, gauntlets, sabatons, belt, gorget, visor rims)',
       3: 'dark (visor slit, breaths)', 5: 'eye glow in the visor', 11: 'plume', 12: 'tabard'}

# key: (id, roughness, metallic, specular)
MATS = {'arm_steel': (1, 0.26, 0.45, 0.80), 'arm_plate': (2, 0.32, 0.45, 0.70), 'arm_dark': (3, 0.60, 0.0, 0.30),
        'arm_plume': (11, 0.75, 0.0, 0.30), 'arm_tabard': (12, 0.85, 0.0, 0.30)}


def register_mats(palette=None):
    pal = dict(PALETTE)
    pal.update(palette or {})
    for key, (id_, rough, metal, spec) in MATS.items():
        C.mat(key, base=G.srgb_to_lin(pal[id_]), id=id_, rough=rough, metal=metal, spec=spec)
    C.mat('arm_glow', id=5, build=G.glow_build(pal[5]))


HELM = [(0.0, 0.640), (0.165, 0.645), (0.212, 0.685), (0.236, 0.760), (0.242, 0.860), (0.236, 0.950),
        (0.215, 1.020), (0.170, 1.072), (0.095, 1.102), (0.0, 1.110)]
HELM_SY = 0.94
CHEST = [(0.0, 0.345), (0.150, 0.350), (0.176, 0.400), (0.186, 0.470), (0.181, 0.540), (0.156, 0.600),
         (0.100, 0.632), (0.0, 0.638)]
CHEST_SY = 0.82


def _keel(th, z, i):
    """The breastplate's central ridge."""
    d = math.atan2(math.sin(th + math.pi / 2), math.cos(th + math.pi / 2))
    return 1.0 + 0.07 * math.exp(-(d / 0.30) ** 2)


def chest_front(z, x=0.0):
    """y of the breastplate's front surface (keel included) at height z, x."""
    R = max(G.profile_r(CHEST, z), 0.12)
    k = 1.0 + 0.07 * math.exp(-(x / (R * 0.30)) ** 2)
    return -R * CHEST_SY * k * math.sqrt(max(0.0, 1.0 - (x / R) ** 2))


def build(name='armor'):
    r = G.Rig(name)
    r.joint('hips', 'root', (0, 0, 0.33))
    r.joint('spine', 'hips', (0, 0, 0.36))
    r.joint('head', 'spine', (0, 0, 0.64))
    for s, side in ((-1, 'R'), (1, 'L')):
        r.joint('arm_' + side, 'spine', (s * 0.190, 0, 0.585))
        r.joint('fore_' + side, 'arm_' + side, (s * 0.222, 0, 0.445))
        r.joint('leg_' + side, 'hips', (s * 0.085, 0, 0.30))
        r.joint('shin_' + side, 'leg_' + side, (s * 0.088, 0, 0.175))
        r.joint('foot_' + side, 'shin_' + side, (s * 0.090, 0, 0.065))

    # --- helmet: a chibi great helm with a glowing visor slit and a plume
    r.add(G.lathe('a_helm', HELM, 'arm_steel', sy=HELM_SY, seg=40), 'head')
    rh = G.lathe_body_r(HELM, 1.0, HELM_SY)
    zs = 0.885
    front = (math.radians(-152), math.radians(-28))
    r.add(G.wrap('a_slit', lambda th, z: rh(th, z) - 0.004, lambda th: zs, 0.056, 'arm_dark', th0=front[0],
                 th1=front[1], n=28, thick=0.006, sink=0.004), 'head')
    for dz, w in ((0.040, 0.026), (-0.040, 0.026)):
        r.add(G.wrap('a_rim%d' % (dz > 0), rh, lambda th, dz=dz: zs + dz, w, 'arm_plate', th0=front[0] - 0.08,
                     th1=front[1] + 0.08, n=28, thick=0.016, taper=0.08), 'head')
    for s in (-1, 1):
        th = math.radians(-90 + s * 23)
        rr = rh(th, zs)
        p = V((rr * math.cos(th), rr * math.sin(th), zs))
        nrm = V((math.cos(th), math.sin(th) / HELM_SY, 0)).normalized()
        r.add(G.ellipsoid('a_eye%d' % s, p + nrm * 0.004, (0.046, 0.022, 0.018), 'arm_glow',
                          rot=track(nrm), seg=16, rings=8), 'head')
    # ridge from the slit down to the chin, and over the crown front to back
    pts = []
    for z in (0.84, 0.78, 0.72, 0.67):
        pts.append(V((0, -rh(-math.pi / 2, z) - 0.002, z)))
    r.add(G.tube('a_chinridge', pts, [0.018, 0.018, 0.016, 0.014], 'arm_steel', n=8, sub=2), 'head')
    crest = []
    for z, sgn in ((0.95, -1), (1.02, -1), (1.07, -1), (1.10, -1), (1.112, 0), (1.10, 1), (1.07, 1), (1.02, 1),
                   (0.95, 1)):
        crest.append(V((0, sgn * (G.profile_r(HELM, z) * HELM_SY + 0.002), z + 0.004)))
    r.add(G.tube('a_crest', crest, [0.016] * len(crest), 'arm_steel', n=8, sub=3), 'head')
    # plume: three curling feathers
    for k, (dx, lift, back) in enumerate(((0.0, 0.0, 0.0), (-0.045, -0.03, -0.03), (0.045, -0.03, -0.03))):
        base = V((dx * 0.5, 0.01, 1.10))
        pts = [base, base + V((dx * 0.6, 0.05, 0.10 + lift)), base + V((dx, 0.17 + back, 0.14 + lift)),
               base + V((dx * 1.2, 0.27 + back, 0.06 + lift))]
        r.add(G.tube('a_plume%d' % k, pts, [0.036, 0.058 - 0.01 * (k > 0), 0.048 - 0.01 * (k > 0), 0.0],
                     'arm_plume', n=12), 'head')
    r.add(G.lathe('a_plumecup', [(0.0, 1.090), (0.035, 1.092), (0.040, 1.115), (0.0, 1.118)], 'arm_plate',
                  c=(0, 0.01, 0), seg=16), 'head')

    # --- body
    r.add(G.lathe('a_gorget', [(0.0, 0.595), (0.135, 0.600), (0.158, 0.625), (0.150, 0.655), (0.0, 0.66)],
                  'arm_plate', sy=0.90, seg=32), 'spine')
    r.add(G.lathe('a_chest', CHEST, 'arm_steel', sy=CHEST_SY, seg=40, rfn=_keel), 'spine')
    rc = G.lathe_body_r(CHEST, 1.0, CHEST_SY)
    r.add(G.wrap('a_belt', lambda th, z: rc(th, z) + 0.004, lambda th: 0.368, 0.034, 'arm_plate', n=40,
                 thick=0.014), 'spine')
    # faulds: two overlapping lames flaring over the hips
    for k, (z0, z1, r0, r1) in enumerate(((0.355, 0.300, 0.186, 0.200), (0.305, 0.250, 0.196, 0.212))):
        r.add(G.lathe('a_fauld%d' % k, [(r1, z1), (r0, z0)], 'arm_plate', sy=CHEST_SY * 1.02, seg=40,
                      cap_bot=False, cap_top=False, thick=0.014), 'hips')

    # tabard: a front panel from the chest to above the knees, and one at the back
    def tabard(sgn, z_top, z_bot, w0, w1):
        def fn(u, v):
            z = z_top - (z_top - z_bot) * v
            w = w0 + (w1 - w0) * v
            x = (u * 2 - 1) * w
            y = min(chest_front(z, x) - 0.012, -0.215 * CHEST_SY - 0.020 - 0.02 * v) if z < 0.37 else \
                chest_front(z, x) - 0.012
            if v > 0.97:
                z += 0.035 * (1 - abs(u * 2 - 1))            # a shallow point at the hem
            return (x, sgn * y, z)
        return fn
    r.add(G.surface('a_tabard', tabard(1, 0.555, 0.175, 0.080, 0.100), 7, 12, 'arm_tabard', thick=0.010),
          'spine', tag='tabard')
    r.add(G.surface('a_tabardb', tabard(-1, 0.555, 0.20, 0.085, 0.100), 7, 12, 'arm_tabard', thick=0.010),
          'spine', tag='tabard')
    # a raised diamond on the tabard (relief only, same region)
    fy = chest_front(0.45, 0.0) - 0.012 - 0.006
    r.add(G.box('a_badge', (0, fy, 0.450), (0.060, 0.012, 0.060), 'arm_tabard', rot=(0, 45, 0),
                bevel=0.006), 'spine', tag='tabard')

    # --- arms: big pauldrons, cops, vambraces, gauntlet fists
    def half(u):
        if u.z < 0:
            u.z *= 0.30
        return u
    for s, side in ((-1, 'R'), (1, 'L')):
        arm, fore = 'arm_' + side, 'fore_' + side
        r.add(G.ellipsoid('a_paul' + side, (s * 0.200, 0.0, 0.600), (0.118, 0.122, 0.088), 'arm_plate',
                          rot=(0, s * 14, 0), deform=half, seg=28, rings=14), arm)
        r.add(G.ellipsoid('a_paul2' + side, (s * 0.214, 0.0, 0.548), (0.100, 0.108, 0.060), 'arm_plate',
                          rot=(0, s * 20, 0), deform=half, seg=28, rings=14), arm)
        r.add(G.tube('a_upper' + side, [(s * 0.200, 0, 0.56), (s * 0.222, 0, 0.45)], [0.046, 0.044], 'arm_steel',
                     n=14), arm)
        r.add(G.ellipsoid('a_cop' + side, (s * 0.224, 0.004, 0.445), (0.056, 0.058, 0.050), 'arm_plate', seg=18,
                          rings=10), fore)
        r.add(G.tube('a_vamb' + side, [(s * 0.224, 0, 0.44), (s * 0.234, 0, 0.33)], [0.046, 0.050], 'arm_steel',
                     n=14), fore)
        r.add(G.lathe('a_cuff' + side, [(0.050, 0.345), (0.066, 0.300), (0.0, 0.298)], 'arm_plate',
                      c=(s * 0.234, 0, 0), seg=20, cap_top=False), fore)
        r.add(G.ellipsoid('a_fist' + side, (s * 0.236, -0.006, 0.262), (0.054, 0.062, 0.060), 'arm_plate', seg=20,
                          rings=10), fore)
        # --- legs
        leg, shin, foot = 'leg_' + side, 'shin_' + side, 'foot_' + side
        r.add(G.tube('a_thigh' + side, [(s * 0.085, 0, 0.30), (s * 0.088, 0, 0.18)], [0.066, 0.058], 'arm_steel',
                     n=14), leg)
        r.add(G.ellipsoid('a_knee' + side, (s * 0.088, -0.022, 0.172), (0.060, 0.050, 0.056), 'arm_plate', seg=18,
                          rings=10), shin)
        r.add(G.tube('a_greave' + side, [(s * 0.088, 0, 0.17), (s * 0.090, 0, 0.07)], [0.054, 0.058], 'arm_steel',
                     n=14), shin)

        def sabaton(u):
            if u.z < -0.3:
                u.z = -0.3 + (u.z + 0.3) * 0.25
            if u.y < 0:
                u.x *= 1.0 + 0.35 * u.y
            return u
        r.add(G.ellipsoid('a_sabaton' + side, (s * 0.090, -0.040, 0.042), (0.066, 0.112, 0.048), 'arm_plate',
                          deform=sabaton, seg=24, rings=12), foot)
    return r


def pose(anim, f, n, d='s'):
    """idle frame 0 is THE pose of the static prop. walk: a stiff clank."""
    ph = TAU * f / max(n, 1)
    if anim == 'idle':
        if f % 2 == 0:
            return {'arm_R': (0, 0, -3), 'arm_L': (0, 0, 3)}
        return {'arm_R': (0, 0, -3), 'arm_L': (0, 0, 3), 'head': (0, 0, 7), 'spine@': (0, 0, -0.004)}
    if anim == 'walk':
        s = math.sin(ph)
        c = math.cos(ph)
        return {'leg_R': (20 * s, 0, 0), 'leg_L': (-20 * s, 0, 0), 'shin_R': (8 * max(0, -s), 0, 0),
                'shin_L': (8 * max(0, s), 0, 0), 'arm_R': (-16 * s, 0, -3), 'arm_L': (16 * s, 0, 3),
                'spine': (0, 4 * s, 0), 'head': (0, -3 * s, 3 * s), 'spine@': (0, 0, -0.012 * abs(c))}
    return None
