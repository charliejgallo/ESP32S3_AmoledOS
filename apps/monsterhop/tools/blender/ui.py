"""Monster Hop - UI art (SPEC section 9) -> assets/ui/.

    Blender -b -P ui.py -- --out ../../assets/ui [--sample] [--only logo,map,emblem_city] [--samples 64]

Not composited with the game, so each picture has its own camera (set here,
not by mh_common.place_camera) under the `map` light; everything else (the
material registry, the EXR -> PNG path, meta.json) is mh_common's.

    logo           340 x 130, transparent: MONSTER HOP in chunky extruded letters
    map            368 x 1400, opaque: the world map as a hub (house at the bottom
                   centre, two zones per tier); meta carries the pixel centres of the
                   24 level pads, the house and the 2 locked spots
    emblem_<zone>  96 x 96 medallions for the zone picker
"""
import math
import os
import random
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
_argv = sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else []
SAMPLE = '--sample' in _argv
sys.argv = [x for x in sys.argv if x != '--sample']

import mh_common as C          # noqa: E402
import objects_lib as L        # noqa: E402
import bpy                     # noqa: E402
from mathutils import Matrix, Vector as V, noise  # noqa: E402
from bpy_extras.object_utils import world_to_camera_view  # noqa: E402

a = C.args(defaults={'samples': 64})
OUT = os.path.abspath(a.out)
C.reset('map', cpu=a.cpu)
META = C._STATE['meta']
T0 = time.time()

# the sample: final logo + city emblem, a low-sample first draft of the map
SAMPLE_SET = {'logo', 'map', 'emblem_city'}
MAP_DRAFT_SAMPLES = 16


def want(name):
    if SAMPLE and name not in SAMPLE_SET:
        return False
    if a.only:
        return any(name == o or name.startswith(o + '_') for o in a.only)
    return True


# ---------------------------------------------------------------------------
# camera and rendering (UI only: our own camera)
# ---------------------------------------------------------------------------

camera = L.camera


def ui_render(name, w, h, objs, samples, extra=None, filt=1.2, bounces=6, fit='HORIZONTAL'):
    return L.picture(OUT, name, w, h, objs, samples, extra, filt, bounces, fit)


def px_of(p, w, h):
    sc = C._STATE['scene']
    u, v, _ = world_to_camera_view(sc, C._STATE['cam'], V(p))
    return [round(u * w, 1), round((1 - v) * h, 1)]


def remove(objs):
    for ob in objs:
        if ob.name in bpy.data.objects:
            bpy.data.objects.remove(ob, do_unlink=True)


def ramp_z_build(stops, z0, z1, gloss=0.35, emit=0.12, side=1.0, shine=None, rough=0.3, coat=1.0):
    """A colour ramp over world Z (z0 -> z1), glossy; `side` darkens it (the
    extruded sides of letters); `shine` = (local_z0, local_z1) puts a lighter
    band on the upper part of each letter (object coordinates)."""
    st = [(p, L.hexlin(c)) for p, c in stops]

    def b(nt, neutral):
        N, Lk = nt.nodes, nt.links
        p = N.new('ShaderNodeBsdfPrincipled')
        p.inputs['Roughness'].default_value = rough
        p.inputs['Clearcoat'].default_value = coat
        p.inputs['Clearcoat Roughness'].default_value = 0.08
        p.inputs['Specular'].default_value = 0.6
        sep = N.new('ShaderNodeSeparateXYZ')
        mr = N.new('ShaderNodeMapRange')
        if z0 is not None:
            mr.inputs['From Min'].default_value = z0
            mr.inputs['From Max'].default_value = z1
        if z0 is None:        # per letter: its own height, 0 at the bottom, 1 at the top
            tc0 = N.new('ShaderNodeTexCoord')
            Lk.new(tc0.outputs['Generated'], sep.inputs[0])
            mr.inputs['From Min'].default_value = 0.0
            mr.inputs['From Max'].default_value = 1.0
            Lk.new(sep.outputs['Y'], mr.inputs['Value'])
        else:
            geo = N.new('ShaderNodeNewGeometry')
            Lk.new(geo.outputs['Position'], sep.inputs[0])
            Lk.new(sep.outputs['Z'], mr.inputs['Value'])
        rp = L._ramp(nt, st)
        Lk.new(mr.outputs['Result'], rp.inputs['Fac'])
        col = rp.outputs['Color']
        if shine:
            tc = N.new('ShaderNodeTexCoord')
            s2 = N.new('ShaderNodeSeparateXYZ')
            Lk.new(tc.outputs['Generated'], s2.inputs[0])
            m2 = N.new('ShaderNodeMapRange')
            m2.inputs['From Min'].default_value = shine[0]
            m2.inputs['From Max'].default_value = shine[1]
            Lk.new(s2.outputs['Y'], m2.inputs['Value'])
            mx = N.new('ShaderNodeMixRGB')
            mx.blend_type = 'SCREEN'
            Lk.new(m2.outputs['Result'], mx.inputs['Fac'])
            Lk.new(col, mx.inputs['Color1'])
            mx.inputs['Color2'].default_value = (0.30, 0.30, 0.30, 1)
            col = mx.outputs[0]
        if side != 1.0:
            mul = N.new('ShaderNodeMixRGB')
            mul.blend_type = 'MULTIPLY'
            mul.inputs['Fac'].default_value = 1.0
            Lk.new(col, mul.inputs['Color1'])
            mul.inputs['Color2'].default_value = (side, side, side, 1)
            col = mul.outputs[0]
        Lk.new(col, p.inputs['Base Color'])
        em = N.new('ShaderNodeEmission')
        Lk.new(col, em.inputs['Color'])
        em.inputs['Strength'].default_value = emit
        add = N.new('ShaderNodeAddShader')
        Lk.new(p.outputs['BSDF'], add.inputs[0])
        Lk.new(em.outputs['Emission'], add.inputs[1])
        return add.outputs['Shader']
    return b


# ---------------------------------------------------------------------------
# the logo
# ---------------------------------------------------------------------------

def text_mesh(ch, size, extrude, bevel, offset, name, keys=None, res=3):
    cu = bpy.data.curves.new(name, 'FONT')
    cu.body = ch
    cu.size = size
    cu.extrude = extrude
    cu.bevel_depth = bevel
    cu.bevel_resolution = res
    cu.offset = offset
    cu.align_x = 'CENTER'
    cu.resolution_u = 6
    ob = bpy.data.objects.new(name, cu)
    C.link(ob)
    me_ob = L.curve_to_mesh(ob, name)
    me = me_ob.data
    # centre the glyph on x (align_x centres the advance box, not the ink)
    xs = [v.co.x for v in me.vertices]
    cx = (min(xs) + max(xs)) / 2
    for v in me.vertices:
        v.co.x -= cx
    if keys:
        for k in keys:
            C.assign(me_ob, k)
        # 0: faces towards the camera (+Z in text space), 1: sides and back
        for p in me.polygons:
            p.material_index = 0 if p.normal.z > 0.55 else 1
    L.smooth(me_ob)
    for p in me.polygons:          # flat fronts: smooth n-gons shade in blotches
        if abs(p.normal.z) > 0.985:
            p.use_smooth = False
    me.use_auto_smooth = True
    me.auto_smooth_angle = math.radians(40)
    w = max(xs) - min(xs)
    return me_ob, w


LOGO_W, LOGO_H = 340, 130


def g_logo():
    if not want('logo'):
        return
    Z0, Z1 = -0.95, 1.12
    green = [(0.00, '#138a2e'), (0.40, '#3cc43a'), (0.75, '#8ee83e'), (1.00, '#e4ff62')]
    purple = [(0.00, '#3a0c92'), (0.40, '#7a2ee8'), (0.75, '#aa66ff'), (1.00, '#e0b4ff')]
    C.mat('logo_face_g', build=ramp_z_build(green, None, None, emit=0.18))
    C.mat('logo_side_g', build=ramp_z_build(green, None, None, emit=0.05, side=0.40, rough=0.4))
    C.mat('logo_face_p', build=ramp_z_build(purple, None, None, emit=0.18))
    C.mat('logo_side_p', build=ramp_z_build(purple, None, None, emit=0.05, side=0.40, rough=0.4))
    C.mat('logo_line', base=L.hexlin('#12061f'), rough=0.5, spec=0.3)
    C.mat('logo_line_hi', build=L.gloss_build('#24103e', rough=0.45, coat=0.25, spec=0.35))
    C.mat('gold', build=L.gold_build(emit=0.55, base='#ffb81c', rough=0.20, metal=0.7, rim=0.7))
    C.mat('bat_body', build=L.gloss_build('#3b2a63', emit_col='#3b2a63', emit=0.12, rough=0.5, coat=0.4))
    C.mat('bat_wing', build=L.gloss_build('#6a44a8', emit_col='#6a44a8', emit=0.12, rough=0.6, coat=0.2))
    C.mat('bat_eye', build=L.emit_build('#ffffff', 1.0))
    C.mat('bat_pupil', base=L.hexlin('#120818'), rough=0.3)
    C.mat('bat_fang', build=L.emit_build('#ffffff', 0.9))
    objs, extra = [], []

    def word(text, size, base_z, bounce, tilt, track, face_keys):
        glyphs = []
        for i, ch in enumerate(text):
            face, w = text_mesh(ch, size, 0.16 * size, 0.024 * size, 0.022 * size, 'lg_%s%d' % (ch, i),
                                face_keys)
            line, _ = text_mesh(ch, size, 0.20 * size, 0.02 * size, 0.085 * size, 'ln_%s%d' % (ch, i),
                                ['logo_line', 'logo_line_hi'], res=2)
            glyphs.append((face, line, w))
        total = sum(g[2] for g in glyphs) + track * size * (len(glyphs) - 1)
        x = -total / 2
        for i, (face, line, w) in enumerate(glyphs):
            cx = x + w / 2
            for ob, dy in ((face, 0.0), (line, 0.09 * size)):
                ob.rotation_euler = (math.pi / 2, math.radians(tilt[i]), 0)
                ob.location = (cx, dy, base_z + bounce[i] * size)
            objs.extend([face, line])
            x += w + track * size

    # MONSTER on top, HOP below and bigger, each letter hopping a little
    word('MONSTER', 1.0, 0.32, [0.00, 0.06, -0.01, 0.05, 0.00, 0.07, 0.01],
         [-5, 3, -2, 4, -3, 3, 6], 0.02, ['logo_face_g', 'logo_side_g'])
    word('HOP', 1.34, -0.82, [0.00, 0.10, 0.02], [-7, 3, 8], 0.03, ['logo_face_p', 'logo_side_p'])
    # the key, leaning on the left of HOP
    kr = L.empty('logo_key', (-1.98, -0.30, -0.42))
    kr.rotation_euler = (math.radians(8), math.radians(-34), math.radians(18))
    kr.scale = (1.85, 1.85, 1.85)
    objs += L.build_key(kr, 'gold', 1.0)
    extra.append(kr)
    # the bat, flying on the right of HOP
    br = L.empty('logo_bat', (2.10, -0.35, -0.30))
    br.rotation_euler = (math.radians(-6), math.radians(12), math.radians(-14))
    br.scale = (2.1, 2.1, 2.1)
    bo, be = L.build_bat(br, s=1.0, flap=math.radians(14))
    objs += bo
    extra += [br] + be
    L.update()
    camera((0.12, 0.0, 0.02), (-0.12, 1.0, 0.13), ortho=6.55)
    rim = C.add_point_light((3.0, 2.5, 3.0), (0.75, 0.85, 1.0), 900.0, 0.8, 'logo_rim')
    ui_render('logo', LOGO_W, LOGO_H, objs, a.samples, extra={'what': 'MONSTER HOP logo'})
    remove(objs + extra + [rim])


# ---------------------------------------------------------------------------
# emblems (96 x 96 medallions)
# ---------------------------------------------------------------------------

EMB = 96


def medallion(zone_stops, name):
    """The common frame: a dark stone ring with a gold rim, and an inner disc
    with the zone's colours (radial gradient). Faces -Y, centred at origin,
    radius 0.5."""
    st = [(p, L.hexlin(c)) for p, c in zone_stops]

    def disc_build(nt, neutral):
        N, Lk = nt.nodes, nt.links
        p = N.new('ShaderNodeBsdfPrincipled')
        p.inputs['Roughness'].default_value = 0.6
        tc = N.new('ShaderNodeTexCoord')
        sep = N.new('ShaderNodeSeparateXYZ')
        Lk.new(tc.outputs['Object'], sep.inputs[0])
        # radial gradient, a little lower than the centre (a glow from below)
        comb = N.new('ShaderNodeCombineXYZ')
        Lk.new(sep.outputs['X'], comb.inputs['X'])
        off = N.new('ShaderNodeMath')
        off.operation = 'ADD'
        off.inputs[1].default_value = 0.22
        Lk.new(sep.outputs['Z'], off.inputs[0])
        Lk.new(off.outputs[0], comb.inputs['Y'])
        ln = N.new('ShaderNodeVectorMath')
        ln.operation = 'LENGTH'
        Lk.new(comb.outputs[0], ln.inputs[0])
        mr = N.new('ShaderNodeMapRange')
        mr.inputs['From Min'].default_value = 0.0
        mr.inputs['From Max'].default_value = 0.72
        Lk.new(ln.outputs['Value'], mr.inputs['Value'])
        rp = L._ramp(nt, st)
        Lk.new(mr.outputs['Result'], rp.inputs['Fac'])
        Lk.new(rp.outputs['Color'], p.inputs['Base Color'])
        em = N.new('ShaderNodeEmission')
        Lk.new(rp.outputs['Color'], em.inputs['Color'])
        em.inputs['Strength'].default_value = 0.35
        add = N.new('ShaderNodeAddShader')
        Lk.new(p.outputs['BSDF'], add.inputs[0])
        Lk.new(em.outputs['Emission'], add.inputs[1])
        return add.outputs['Shader']

    C.mat('md_disc_' + name, build=disc_build)
    if 'md_ring' not in C._MATS:
        C.mat('md_ring', build=L.gloss_build('#3a3448', emit_col='#3a3448', emit=0.08, rough=0.45, coat=0.5))
        C.mat('md_gold', build=L.gold_build(emit=0.45, base='#ffb81c', rough=0.22, metal=0.75, rim=0.6))
    o = []
    d = L.cylinder('md_disc', 0.41, 0.04, 'md_disc_' + name, (0, 0.03, 0), (math.pi / 2, 0, 0), None, 64)
    o.append(d)
    o.append(L.torus('md_ring', 0.445, 0.05, 'md_ring', (0, 0, 0), (math.pi / 2, 0, 0), None, (64, 16)))
    o.append(L.torus('md_rim', 0.492, 0.016, 'md_gold', (0, -0.005, 0), (math.pi / 2, 0, 0), None, (64, 10)))
    o.append(L.torus('md_rim2', 0.398, 0.012, 'md_gold', (0, -0.012, 0), (math.pi / 2, 0, 0), None, (64, 10)))
    for i in range(8):
        t = math.pi / 8 + i * math.pi / 4
        o.append(L.sphere('md_rivet', 0.018, 'md_gold', (0.445 * math.cos(t), -0.045, 0.445 * math.sin(t)), seg=12, rings=6))
    return o


def capsule(name, p0, p1, r, key):
    p0, p1 = V(p0), V(p1)
    d = p1 - p0
    c = L.cylinder(name, r, d.length, key, (p0 + p1) / 2, (0, 0, 0), None, 16)
    c.rotation_mode = 'QUATERNION'
    c.rotation_quaternion = d.to_track_quat('Z', 'Y')
    s0 = L.sphere(name + 'a', r, key, p0, seg=16, rings=8)
    s1 = L.sphere(name + 'b', r, key, p1, seg=16, rings=8)
    return [c, s0, s1]


def emblem_city():
    C.mat('zb_skin', build=L.gloss_build('#8fbf78', emit_col='#8fbf78', emit=0.12, rough=0.55, coat=0.2))
    C.mat('zb_skin_dk', base=L.hexlin('#5f8a58'), rough=0.6)
    C.mat('zb_nail', base=L.hexlin('#3e5a3c'), rough=0.4)
    C.mat('zb_sleeve', build=L.gloss_build('#5a6fa0', rough=0.8, coat=0.0, spec=0.2))
    C.mat('zb_stitch', base=L.hexlin('#2a2030'), rough=0.6)
    C.mat('zb_rubble', base=L.hexlin('#4c525c'), rough=0.9)
    C.mat('zb_crack', build=L.emit_build('#ffa030', 1.4))
    o = medallion([(0.0, '#ff9a3a'), (0.14, '#a8705a'), (0.32, '#4a5462'), (0.65, '#2c3442'), (1.0, '#1c222c')], 'city')
    # the town's skyline behind the hand, a few windows lit sodium orange
    C.mat('zb_sky', base=L.hexlin('#1e2430'), rough=0.9)
    C.mat('zb_win', build=L.emit_build('#ffb040', 1.6))
    rs = random.Random(9)
    x = -0.40
    while x < 0.40:
        w = rs.uniform(0.08, 0.14)
        top = rs.uniform(-0.12, 0.10) + (0.06 if abs(x) > 0.2 else 0.0)
        o.append(C.box('zb_bld', x, -0.005, -0.42, x + w, 0.012, top, 'zb_sky'))
        for k in range(3):
            if rs.random() < 0.5:
                wx = x + rs.uniform(0.02, w - 0.04)
                wz = top - 0.05 - 0.06 * k
                o.append(C.box('zb_w', wx, -0.012, wz, wx + 0.022, -0.004, wz + 0.028, 'zb_win'))
        x += w + rs.uniform(0.0, 0.02)
    # a mound of broken asphalt the hand bursts out of
    mound = L.sphere('zb_mound', 0.30, 'zb_rubble', (0, -0.05, -0.43), (1.25, 0.5, 0.55), None, 32, 12)
    o.append(mound)
    rnd = random.Random(4)
    for i in range(6):
        x = rnd.uniform(-0.30, 0.30)
        r = rnd.uniform(0.045, 0.07)
        ch = L.centered(L.rbox('zb_chunk', x - r, -0.18 - r * 0.6, -0.36 - r * 0.7, x + r, -0.18 + r * 0.6,
                               -0.36 + r * 0.7, 'zb_rubble', 0.012))
        ch.rotation_euler = (rnd.uniform(-0.4, 0.4), rnd.uniform(-0.6, 0.6), rnd.uniform(-0.4, 0.4))
        o.append(ch)
    # the glowing crack at the base
    o.append(L.billboard('zb_glow', [(-0.20, -0.02), (-0.05, 0.015), (0.06, -0.01), (0.22, 0.02), (0.06, 0.03),
                                    (-0.05, 0.035)], (0, -0.2, -0.33), 'zb_crack', 0.0))
    # forearm and torn sleeve
    arm = []
    arm.append(L.cylinder('zb_arm', 0.085, 0.36, 'zb_skin', (0.0, -0.08, -0.18), (0.0, math.radians(-8), 0), None, 24))
    sl = L.cylinder('zb_sleeve', 0.112, 0.20, 'zb_sleeve', (0.02, -0.08, -0.33), (0.0, math.radians(-8), 0), None, 24)
    # tear the top edge of the sleeve into zigzags
    me = sl.data
    for v in me.vertices:
        if v.co.z > 0:
            ang = math.atan2(v.co.y, v.co.x)
            v.co.z += 0.05 * (0.5 + 0.5 * math.sin(ang * 7)) - 0.02
    arm.append(sl)
    # the palm
    palm = L.sphere('zb_palm', 0.16, 'zb_skin', (0.0, -0.10, 0.05), (1.0, 0.55, 0.88), None, 32, 16)
    arm.append(palm)
    # fingers: base -> knuckle -> tip, fanned and a little curled
    fing = [(-0.105, 0.14, -24, 0.20), (-0.035, 0.17, -8, 0.24), (0.040, 0.17, 7, 0.23), (0.110, 0.14, 21, 0.19)]
    for i, (x, z, ang, ln) in enumerate(fing):
        t = math.radians(ang)
        p0 = V((x, -0.11, z))
        p1 = p0 + V((math.sin(t), -0.03, math.cos(t))) * ln * 0.55
        t2 = t * 1.25
        p2 = p1 + V((math.sin(t2), -0.10, math.cos(t2))) * ln * 0.45
        arm += capsule('zb_f%d' % i, p0, p1, 0.043, 'zb_skin')
        arm += capsule('zb_g%d' % i, p1, p2, 0.040, 'zb_skin')
        n = L.sphere('zb_nail%d' % i, 0.026, 'zb_nail', p2 + V((0, -0.022, 0.0)), (1.0, 0.5, 1.0), None, 12, 6)
        arm.append(n)
    # thumb out to the left
    p0 = V((-0.14, -0.13, -0.02))
    p1 = p0 + V((-0.11, -0.05, 0.07))
    p2 = p1 + V((-0.05, -0.07, 0.09))
    arm += capsule('zb_t0', p0, p1, 0.046, 'zb_skin')
    arm += capsule('zb_t1', p1, p2, 0.042, 'zb_skin')
    # a stitched patch on the palm
    arm.append(L.rbox('zb_patch', 0.02, -0.205, -0.02, 0.12, -0.17, 0.06, 'zb_skin_dk', 0.01))
    for k in range(3):
        z = -0.005 + 0.03 * k
        arm.append(L.rbox('zb_st', 0.005, -0.215, z - 0.006, 0.135, -0.2, z + 0.006, 'zb_stitch', 0.003))
    hand = L.empty('zb_hand', (0, 0, 0))
    for ob in arm:
        ob.parent = hand
    hand.location = (0.0, -0.03, -0.03)
    hand.rotation_euler = (0, math.radians(8), 0)
    hand.scale = (1.0, 1.0, 1.0)
    return o + arm, [hand]


def _bat_mats(eye='#ffffff'):
    C.mat('bat_body', build=L.gloss_build('#3b2a63', emit_col='#3b2a63', emit=0.12, rough=0.5, coat=0.4))
    C.mat('bat_wing', build=L.gloss_build('#6a44a8', emit_col='#6a44a8', emit=0.12, rough=0.6, coat=0.2))
    C.mat('bat_eye', build=L.emit_build(eye, 1.0))
    C.mat('bat_pupil', base=L.hexlin('#120818'), rough=0.3)
    C.mat('bat_fang', build=L.emit_build('#ffffff', 0.9))


def emblem_castle():
    _bat_mats()
    C.mat('cs_moon', build=L.emit_build('#fff2c0', 1.3))
    C.mat('cs_tower', base=L.hexlin('#2a1a44'), rough=0.9)
    C.mat('cs_win', build=L.emit_build('#ffb040', 1.8))
    o = medallion([(0.0, '#d68aff'), (0.25, '#8a4ad0'), (0.55, '#4a2482'), (1.0, '#24103e')], 'castle')
    o.append(L.cylinder('cs_moon', 0.13, 0.01, 'cs_moon', (0.19, 0.0, 0.19), (math.pi / 2, 0, 0), None, 40))
    # the castle's silhouette along the bottom
    for (x, w, h) in ((-0.30, 0.10, -0.08), (-0.18, 0.16, -0.16), (0.0, 0.14, -0.02), (0.17, 0.16, -0.14), (0.30, 0.10, -0.10)):
        o.append(C.box('cs_t', x - w / 2, -0.004, -0.45, x + w / 2, 0.012, h, 'cs_tower'))
        o.append(L.cone('cs_roof', w * 0.65, 0.0, 0.12, 'cs_tower', (x, 0.004, h + 0.06), (0, 0, 0), None, 12))
        o.append(C.box('cs_w', x - 0.012, -0.012, h - 0.09, x + 0.012, -0.005, h - 0.05, 'cs_win'))
    br = L.empty('cs_bat', (-0.04, -0.12, -0.02))
    br.scale = (1.22, 1.22, 1.22)
    br.rotation_euler = (math.radians(-6), 0, 0)
    bo, be = L.build_bat(br, s=1.0, flap=math.radians(18))
    return o + bo, [br] + be


def emblem_desert():  # noqa: C901
    C.mat('ds_stone', build=L.gloss_build('#e8b868', rough=0.8, coat=0.0, spec=0.3))
    C.mat('ds_stone_dk', base=L.hexlin('#b07a38'), rough=0.85)
    C.mat('ds_dune', base=L.hexlin('#d49a50'), rough=0.9)
    C.mat('ds_eyew', build=L.emit_build('#fffaf0', 1.0))
    C.mat('ds_iris', build=L.emit_build('#20d0c8', 1.6))
    C.mat('ds_pupil', base=L.hexlin('#101010'), rough=0.3)
    C.mat('ds_gold', build=L.gold_build(emit=0.6, rim=0.5))
    C.mat('ds_sun', build=L.emit_build('#fff0b0', 1.4))
    o = medallion([(0.0, '#ffd070'), (0.3, '#f09040'), (0.6, '#a84a3a'), (1.0, '#4a2040')], 'desert')
    o.append(L.cylinder('ds_sun', 0.12, 0.01, 'ds_sun', (-0.18, 0.0, 0.16), (math.pi / 2, 0, 0), None, 32))
    py = L.cone('ds_pyr', 0.50, 0.0, 0.56, 'ds_stone', (0.0, -0.05, -0.08), (0, 0, math.pi / 4), None, 4)
    py.scale = (1.0, 0.45, 1.0)
    o.append(py)
    o.append(L.cone('ds_cap', 0.09, 0.0, 0.10, 'ds_gold', (0.0, -0.05, 0.155), (0, 0, math.pi / 4), None, 4))
    o[-1].scale = (1.0, 0.45, 1.0)
    ez = -0.05
    ew = L.sphere('ds_eye', 0.11, 'ds_eyew', (0, -0.21, ez), (1.0, 0.25, 0.55), None, 24, 12)
    o.append(ew)
    o.append(L.sphere('ds_iris', 0.05, 'ds_iris', (0, -0.235, ez), (1.0, 0.3, 1.0), None, 16, 8))
    o.append(L.sphere('ds_pup', 0.024, 'ds_pupil', (0, -0.25, ez), (1.0, 0.3, 1.0), None, 12, 6))
    for sgn in (-1, 1):
        o.append(L.cylinder('ds_lid', 0.012, 0.25, 'ds_pupil', (0, -0.235, ez + sgn * 0.055), (0, math.pi / 2, 0), None, 8))
    # dunes across the bottom, cut to the disc
    clip = L.cylinder('ds_clip', 0.43, 1.0, None, (0, 0, 0), (math.pi / 2, 0, 0), None, 48)
    clip.hide_render = True
    for (x, z, sx, key) in ((-0.22, -0.52, 0.42, 'ds_dune'), (0.26, -0.56, 0.40, 'ds_stone_dk')):
        dn = L.sphere('ds_dune', 1.0, key, (x, -0.24, z), (sx, 0.12, 0.26), None, 32, 12)
        bo = dn.modifiers.new('clip', 'BOOLEAN')
        bo.operation = 'INTERSECT'
        bo.solver = 'EXACT'
        bo.object = clip
        o.append(dn)
    return o, []


def emblem_forest():
    C.mat('fo_moon', build=L.emit_build('#f4f8e0', 1.4))
    C.mat('fo_fur', build=L.gloss_build('#8a7a6a', emit_col='#8a7a6a', emit=0.08, rough=0.7, coat=0.0, spec=0.3))
    C.mat('fo_fur_lt', build=L.gloss_build('#d8ccb8', emit_col='#d8ccb8', emit=0.08, rough=0.7, coat=0.0, spec=0.3))
    C.mat('fo_ear_in', base=L.hexlin('#c87a7a'), rough=0.7)
    C.mat('fo_eye', build=L.emit_build('#ffe23a', 2.4))
    C.mat('fo_dark', base=L.hexlin('#141014'), rough=0.3)
    C.mat('moon_tooth', build=L.emit_build('#ffffff', 0.9))
    C.mat('fo_pine', base=L.hexlin('#10382a'), rough=0.9)
    o = medallion([(0.0, '#5ad0b0'), (0.3, '#2a8a6a'), (0.6, '#14503e'), (1.0, '#0a261e')], 'forest')
    o.append(L.cylinder('fo_moon', 0.2, 0.01, 'fo_moon', (0.0, 0.0, 0.12), (math.pi / 2, 0, 0), None, 48))
    for (x, h) in ((-0.33, 0.30), (-0.22, 0.42), (0.23, 0.40), (0.34, 0.28)):
        o.append(L.cone('fo_pine', 0.09, 0.0, h, 'fo_pine', (x, 0.0, -0.40 + h / 2), (0, 0, 0), None, 6))
    # the wolf's head, howling a little up
    hd = L.empty('fo_head', (0.0, -0.12, -0.10))
    parts = []
    parts.append(L.sphere('fo_skull', 0.17, 'fo_fur', (0, 0, 0), (1.0, 0.9, 0.95), hd, 32, 16))
    parts.append(L.sphere('fo_cheekL', 0.09, 'fo_fur', (-0.13, -0.03, -0.07), (1, 0.8, 0.9), hd, 16, 8))
    parts.append(L.sphere('fo_cheekR', 0.09, 'fo_fur', (0.13, -0.03, -0.07), (1, 0.8, 0.9), hd, 16, 8))
    parts.append(L.sphere('fo_snout', 0.085, 'fo_fur_lt', (0, -0.15, -0.05), (0.9, 1.2, 0.75), hd, 20, 10))
    parts.append(L.sphere('fo_nose', 0.034, 'fo_dark', (0, -0.25, -0.02), (1.2, 0.8, 0.9), hd, 12, 6))
    parts.append(L.sphere('fo_mouth', 0.05, 'fo_dark', (0, -0.18, -0.12), (1.0, 0.6, 0.5), hd, 12, 6))
    for sx in (-1, 1):
        e = L.cone('fo_ear', 0.075, 0.0, 0.17, 'fo_fur', (sx * 0.1, 0.02, 0.19), (0, sx * 0.35, 0), hd, 12)
        parts.append(e)
        parts.append(L.cone('fo_earin', 0.04, 0.0, 0.11, 'fo_ear_in', (sx * 0.098, -0.02, 0.175), (0, sx * 0.35, 0), hd, 10))
        parts.append(L.sphere('fo_eye', 0.03, 'fo_eye', (sx * 0.07, -0.135, 0.04), (1.1, 0.5, 0.8), hd, 12, 6))
        parts.append(L.cone('fo_brow', 0.02, 0.0, 0.09, 'fo_dark', (sx * 0.07, -0.145, 0.085), (0, sx * 1.2, 0), hd, 6))
        parts.append(L.cone('fo_tooth', 0.012, 0.0, 0.03, 'moon_tooth', (sx * 0.03, -0.215, -0.1), (math.pi, 0, 0), hd, 6))
        parts.append(L.sphere('fo_tuft', 0.06, 'fo_fur_lt', (sx * 0.14, -0.06, -0.14), (0.8, 0.6, 1.1), hd, 12, 6))
    hd.rotation_euler = (math.radians(12), 0, 0)
    hd.scale = (1.05, 1.05, 1.05)
    return o + parts, [hd]


def emblem_swamp():
    C.mat('sw_hat', build=L.gloss_build('#5a2a8a', emit_col='#5a2a8a', emit=0.1, rough=0.6, coat=0.1))
    C.mat('sw_band', build=L.gloss_build('#3ad04a', emit_col='#3ad04a', emit=0.25, rough=0.5, coat=0.2))
    C.mat('sw_buckle', build=L.gold_build(emit=0.6, rim=0.5))
    C.mat('sw_bubble', build=L.gloss_build('#9aff6a', emit_col='#9aff6a', emit=0.6, rough=0.2, coat=0.5))
    o = medallion([(0.0, '#b8ff7a'), (0.3, '#4aa05a'), (0.6, '#1e5a48'), (1.0, '#0e2a26')], 'swamp')
    hat = L.empty('sw_hat', (0.0, -0.12, -0.08))
    parts = []
    br = L.cylinder('sw_brim', 0.34, 0.035, 'sw_hat', (0, 0, -0.14), (0, 0, 0), hat, 48)
    parts.append(br)
    parts.append(L.lathe('sw_crown', [(0.19, -0.13), (0.17, 0.0), (0.12, 0.14), (0.07, 0.24), (0.0, 0.28)], 'sw_hat', 32, hat))
    tip = L.cone('sw_tip', 0.07, 0.0, 0.22, 'sw_hat', (0.08, 0, 0.33), (0, math.radians(55), 0), hat, 16)
    parts.append(tip)
    parts.append(L.lathe('sw_bandm', [(0.183, -0.115), (0.18, -0.06), (0.175, -0.06), (0.178, -0.115)], 'sw_band', 32, hat))
    parts.append(L.rbox('sw_buck', -0.05, -0.20, -0.115, 0.05, -0.17, -0.055, 'sw_buckle', 0.01, parent=hat))
    hat.rotation_euler = (math.radians(-18), math.radians(-8), 0)
    hat.scale = (1.1, 1.1, 1.1)
    for (x, z, r) in ((-0.25, -0.30, 0.04), (0.22, -0.26, 0.05), (0.3, -0.12, 0.03), (-0.3, -0.12, 0.025)):
        parts.append(L.sphere('sw_bub', r, 'sw_bubble', (x, -0.1, z), seg=12, rings=6))
    return o + parts, [hat]


def emblem_graveyard():
    C.mat('gy_bone', build=L.gloss_build('#f2eee0', emit_col='#f2eee0', emit=0.12, rough=0.5, coat=0.2))
    C.mat('gy_dark', build=L.emit_build('#1a1428', 1.0))
    C.mat('gy_glow', build=L.emit_build('#80ffd0', 2.0))
    o = medallion([(0.0, '#b0c4d4'), (0.3, '#6a7e94'), (0.6, '#3a4658'), (1.0, '#1c2230')], 'graveyard')
    sk = L.empty('gy_skull', (0, -0.12, 0.0))
    parts = []
    parts.append(L.sphere('gy_cran', 0.23, 'gy_bone', (0, 0, 0.05), (1.0, 0.9, 0.92), sk, 32, 16))
    parts.append(L.rbox('gy_jaw', -0.13, -0.10, -0.24, 0.13, 0.10, -0.08, 'gy_bone', 0.05, parent=sk))
    for sx in (-1, 1):
        parts.append(L.sphere('gy_sock', 0.075, 'gy_dark', (sx * 0.085, -0.17, 0.02), (1.0, 0.5, 1.1), sk, 16, 8))
        parts.append(L.sphere('gy_spark', 0.022, 'gy_glow', (sx * 0.085, -0.215, 0.03), (1, 0.5, 1), sk, 8, 4))
    parts.append(L.cone('gy_nose', 0.035, 0.0, 0.06, 'gy_dark', (0, -0.2, -0.07), (math.pi, 0, 0), sk, 3))
    for i in range(4):
        x = -0.075 + 0.05 * i
        parts.append(C.box('gy_tooth', x - 0.004, -0.105, -0.22, x + 0.004, -0.095, -0.12, 'gy_dark'))
        parts[-1].parent = sk
    sk.rotation_euler = (0, 0, math.radians(-6))
    return o + parts, [sk]


def emblem_lock():
    C.mat('lk_body', build=L.gold_build(emit=0.5, rim=0.6))
    C.mat('lk_steel', build=L.gold_build(emit=0.35, base='#c8ccd4', rough=0.25, metal=0.8, rim=0.6, env=L.SILVER_ENV))
    C.mat('lk_hole', base=L.hexlin('#1a1020'), rough=0.4)
    o = medallion([(0.0, '#8a82a0'), (0.35, '#4c4660'), (0.7, '#2a2638'), (1.0, '#16141e')], 'lock')
    o.append(L.rbox('lk_body', -0.2, -0.2, -0.28, 0.2, -0.04, 0.02, 'lk_body', 0.05))
    sh = L.torus('lk_shackle', 0.12, 0.035, 'lk_steel', (0, -0.12, 0.04), (math.pi / 2, 0, 0), None, (40, 12))
    # keep the upper half of the ring: push the lower half into the body
    for v in sh.data.vertices:
        if v.co.z < 0:
            v.co.z = v.co.z * 0.1
    o.append(sh)
    for sx in (-1, 1):
        o.append(L.cylinder('lk_leg', 0.035, 0.08, 'lk_steel', (sx * 0.12, -0.12, 0.03), (0, 0, 0), None, 16))
    o.append(L.cylinder('lk_hole', 0.035, 0.03, 'lk_hole', (0, -0.205, -0.10), (math.pi / 2, 0, 0), None, 16))
    o.append(L.rbox('lk_slot', -0.016, -0.215, -0.20, 0.016, -0.195, -0.10, 'lk_hole', 0.006))
    return o, []


def _disc_clip(name, r=0.43):
    """A hidden cylinder the size of the medallion's disc, for INTERSECT
    booleans (things that rise from the bottom edge stay inside the ring)."""
    clip = L.cylinder(name, r, 1.0, None, (0, 0, 0), (math.pi / 2, 0, 0), None, 48)
    clip.hide_render = True
    return clip


def _clip_to(ob, clip):
    bo = ob.modifiers.new('clip', 'BOOLEAN')
    bo.operation = 'INTERSECT'
    bo.solver = 'EXACT'
    bo.object = clip
    return ob


def flat_xz(name, pts, y, key, off=(0.0, 0.0)):
    """A flat polygon facing the medallion camera (the XZ plane at depth y)."""
    vs = [(x + off[0], y, z + off[1]) for (x, z) in pts]
    vs.append((sum(v[0] for v in vs) / len(vs), y, sum(v[2] for v in vs) / len(vs)))
    n = len(pts)
    return L.mesh_from(name, vs, [(i, (i + 1) % n, n) for i in range(n)], key, sm=False)


def _frond(name, key, base, ang, ln, wd, parent=None, droop=0.0):
    """A fern frond seen from the front: a long leaf (flattened sphere) with
    little leaflets along it, leaning `ang` degrees from vertical."""
    t = math.radians(ang)
    d = V((math.sin(t), 0.0, math.cos(t)))
    o = []
    mid = V(base) + d * ln * 0.5
    lf = L.sphere(name, ln * 0.5, key, tuple(mid), (wd / ln, 0.25, 1.0), parent, 16, 8)
    lf.rotation_euler = (0, t, 0)
    o.append(lf)
    for k in range(4):
        s = 0.25 + 0.17 * k
        p = V(base) + d * ln * s
        for sg in (-1, 1):
            q = p + V((math.cos(t) * sg, -0.004, -math.sin(t) * sg)) * wd * 1.1 + V((0, 0, -droop * s))
            lt = L.sphere(name + 'l', wd * 0.9 * (1.1 - 0.2 * k), key, tuple(q), (1.4, 0.3, 0.55), parent, 10, 5)
            lt.rotation_euler = (0, t + sg * 0.5, 0)
            o.append(lt)
    return o


def emblem_dino():
    """Lost Valley: a friendly T-Rex head in profile, a smoking volcano and
    fern fronds behind it, on an orange-to-jungle-green disc."""
    C.mat('dn_skin', build=L.gloss_build('#6cc04a', emit_col='#6cc04a', emit=0.12, rough=0.5, coat=0.25))
    C.mat('dn_skin_dk', build=L.gloss_build('#3f8a34', emit_col='#3f8a34', emit=0.08, rough=0.6, coat=0.1))
    C.mat('dn_belly', build=L.gloss_build('#f6b448', emit_col='#f6b448', emit=0.14, rough=0.5, coat=0.2))
    C.mat('dn_mouth', base=L.hexlin('#6a1a24'), rough=0.5)
    C.mat('dn_tongue', base=L.hexlin('#e0607a'), rough=0.5)
    C.mat('dn_eye', build=L.emit_build('#ffffff', 1.0))
    C.mat('dn_pupil', base=L.hexlin('#141014'), rough=0.3)
    C.mat('dn_tooth', build=L.emit_build('#fffaf0', 0.9))
    C.mat('dn_volc', build=L.gloss_build('#5a3a40', emit_col='#5a3a40', emit=0.1, rough=0.8, coat=0.0, spec=0.3))
    C.mat('dn_lava', build=L.emit_build('#ff8a2a', 2.4))
    C.mat('dn_smoke', build=L.toon_build('#e8dcd6', '#9a7a86', emit=0.45))
    C.mat('dn_fern', build=L.gloss_build('#2ab08e', emit_col='#2ab08e', emit=0.15, rough=0.6, coat=0.0, spec=0.3))
    C.mat('dn_fern2', build=L.gloss_build('#3a9a40', emit_col='#3a9a40', emit=0.12, rough=0.6, coat=0.0, spec=0.3))
    o = medallion([(0.0, '#ffd070'), (0.26, '#f08a3a'), (0.55, '#6a8a2a'), (1.0, '#16341a')], 'dino')
    clip = _disc_clip('dn_clip')
    # the volcano on the horizon, bottom right, smoking, a lava tongue down its side
    v = L.cone('dn_volc', 0.36, 0.08, 0.26, 'dn_volc', (0.25, -0.02, -0.33), (0, 0, 0), None, 32)
    v.scale = (1.0, 0.12, 1.0)
    o.append(_clip_to(v, clip))
    o.append(L.sphere('dn_crater', 0.07, 'dn_lava', (0.25, -0.04, -0.20), (1.15, 0.3, 0.3), None, 12, 6))
    for (dx, dz, r) in ((0.0, 0.05, 0.04), (0.035, 0.105, 0.05), (0.08, 0.16, 0.055)):
        o.append(L.sphere('dn_smoke', r, 'dn_smoke', (0.25 + dx, -0.03, -0.20 + dz), (1.0, 0.4, 0.8), None, 16, 8))
    # fern fronds from the bottom, in front of the neck
    for (x, ang, ln, key) in ((-0.34, -40, 0.30, 'dn_fern'), (-0.08, -8, 0.26, 'dn_fern2'), (0.04, 12, 0.18, 'dn_fern'),
                              (-0.20, -24, 0.22, 'dn_fern2')):
        for ob in _frond('dn_fr', key, (x, -0.34, -0.46), ang, ln, 0.034, droop=0.03):
            o.append(_clip_to(ob, clip))
    # the head, facing right, mouth open in a grin: a tall boxy skull, a short
    # deep snout (a T-Rex, not a crocodile)
    hd = L.empty('dn_head', (-0.06, -0.14, 0.04))
    p = []
    p.append(L.sphere('dn_cran', 0.175, 'dn_skin', (-0.07, 0, 0.05), (1.0, 0.8, 1.05), hd, 32, 16))
    p.append(L.sphere('dn_snout', 0.14, 'dn_skin', (0.10, 0, 0.04), (1.12, 0.74, 0.78), hd, 32, 16))
    p.append(L.sphere('dn_brow', 0.06, 'dn_skin', (0.02, -0.05, 0.16), (1.4, 0.9, 0.6), hd, 16, 8))
    p.append(L.sphere('dn_mouth', 0.12, 'dn_mouth', (0.10, 0.005, -0.06), (1.12, 0.66, 0.36), hd, 24, 12))
    p.append(L.sphere('dn_tongue', 0.06, 'dn_tongue', (0.08, -0.02, -0.085), (1.4, 0.8, 0.35), hd, 16, 8))
    jaw = L.sphere('dn_jaw', 0.125, 'dn_belly', (0.06, 0, -0.12), (1.15, 0.68, 0.46), hd, 32, 16)
    jaw.rotation_euler = (0, math.radians(6), 0)
    p.append(jaw)
    p.append(L.sphere('dn_throat', 0.12, 'dn_belly', (-0.09, 0.01, -0.13), (1.0, 0.7, 0.85), hd, 24, 12))
    # the neck, down to the bottom-left edge
    nk = L.sphere('dn_neck', 0.15, 'dn_skin', (-0.18, 0.03, -0.30), (1.0, 0.75, 1.5), hd, 32, 16)
    p.append(nk)
    for (x, z, r) in ((-0.18, 0.14, 0.03), (-0.11, 0.19, 0.024), (-0.22, 0.04, 0.026), (-0.26, -0.15, 0.03)):
        p.append(L.sphere('dn_spot', r, 'dn_skin_dk', (x, -0.105, z), (1.0, 0.4, 0.8), hd, 12, 6))
    # four big friendly teeth down, two up
    for i in range(4):
        x = 0.04 + 0.055 * i
        p.append(L.cone('dn_tooth', 0.02, 0.0, 0.045, 'dn_tooth', (x, -0.085, -0.035), (math.pi, 0, 0), hd, 8))
    for i in range(2):
        x = 0.07 + 0.07 * i
        p.append(L.cone('dn_tooth', 0.017, 0.0, 0.036, 'dn_tooth', (x, -0.08, -0.09), (0, 0, 0), hd, 8))
    # a big friendly eye with a highlight, the nostril
    p.append(L.sphere('dn_eye', 0.062, 'dn_eye', (0.0, -0.11, 0.10), (1.0, 0.5, 1.05), hd, 20, 10))
    p.append(L.sphere('dn_pup', 0.034, 'dn_pupil', (0.02, -0.135, 0.095), (1.0, 0.45, 1.1), hd, 16, 8))
    p.append(L.sphere('dn_hl', 0.012, 'dn_eye', (0.03, -0.15, 0.113), (1, 0.5, 1), hd, 8, 4))
    p.append(L.sphere('dn_nos', 0.017, 'dn_pupil', (0.235, -0.085, 0.08), (1.4, 0.5, 0.8), hd, 10, 5))
    for ob in p:
        if ob.name.startswith('dn_neck') or ob.name.startswith('dn_throat'):
            _clip_to(ob, clip)
    hd.rotation_euler = (0, math.radians(-4), 0)
    return o + p, [hd, clip]


def emblem_bay():
    """Abyss Bay: the red-and-white lighthouse throwing its beams over a night
    sea, a magenta tentacle curling out of the waves, cyan sparks in the water."""
    C.mat('by_red', build=L.gloss_build('#e0383a', emit_col='#e0383a', emit=0.12, rough=0.45, coat=0.3))
    C.mat('by_white', build=L.gloss_build('#f4f0e6', emit_col='#f4f0e6', emit=0.12, rough=0.5, coat=0.2))
    C.mat('by_dark', base=L.hexlin('#1e2430'), rough=0.5)
    C.mat('by_lamp', build=L.emit_build('#fff2b0', 3.2))
    C.mat('by_rock', build=L.gloss_build('#3e4a62', rough=0.7, coat=0.1, spec=0.3))
    C.mat('by_wave', build=L.gloss_build('#1ea8b8', emit_col='#1ea8b8', emit=0.3, rough=0.2, coat=0.8))
    C.mat('by_wave2', build=L.gloss_build('#146a8a', emit_col='#146a8a', emit=0.2, rough=0.2, coat=0.8))
    C.mat('by_foam', build=L.emit_build('#c8fff4', 1.0))
    C.mat('by_tent', build=L.gloss_build('#d23ea6', emit_col='#d23ea6', emit=0.3, rough=0.35, coat=0.5))
    C.mat('by_suck', build=L.gloss_build('#ffb0e0', emit_col='#ffb0e0', emit=0.3, rough=0.4, coat=0.3))
    C.mat('by_cyan', build=L.emit_build('#5afff0', 2.5))
    C.mat('by_mag', build=L.emit_build('#ff6ae0', 2.5))
    C.mat('by_star', build=L.emit_build('#ffffff', 1.4))

    def beam_build(nt, neutral):
        N, Lk = nt.nodes, nt.links
        e = N.new('ShaderNodeEmission')
        e.inputs['Color'].default_value = L.hexlin('#fff4c0') + (1,)
        e.inputs['Strength'].default_value = 1.2
        tr = N.new('ShaderNodeBsdfTransparent')
        mx = N.new('ShaderNodeMixShader')
        mx.inputs['Fac'].default_value = 0.45
        Lk.new(tr.outputs['BSDF'], mx.inputs[1])
        Lk.new(e.outputs['Emission'], mx.inputs[2])
        return mx.outputs['Shader']
    C.mat('by_beam', build=beam_build)
    o = medallion([(0.0, '#7affe6'), (0.25, '#22a6b4'), (0.55, '#1a4e86'), (1.0, '#141a44')], 'bay')
    clip = _disc_clip('by_clip')
    rs = random.Random(5)
    for i in range(9):
        a_ = rs.uniform(0.3, 2.8)
        r = rs.uniform(0.2, 0.36)
        o.append(L.sphere('by_star', rs.uniform(0.006, 0.011), 'by_star', (r * math.cos(a_), 0.0, 0.05 + r * math.sin(a_) * 0.9),
                          seg=6, rings=3))
    X = -0.07
    # the rock and the tower (four bands, red / white), the gallery, the lamp
    o.append(L.sphere('by_rock', 0.16, 'by_rock', (X, -0.04, -0.30), (1.5, 0.5, 0.6), None, 24, 12))
    zs = [-0.29, -0.19, -0.09, 0.01, 0.11]
    for i in range(4):
        r1 = 0.118 - 0.008 * i
        r2 = r1 - 0.008
        o.append(L.cone('by_band', r1, r2, zs[i + 1] - zs[i], 'by_red' if i % 2 == 0 else 'by_white',
                        (X, -0.06, (zs[i] + zs[i + 1]) / 2), (0, 0, 0), None, 32))
    o.append(L.rbox('by_door', X - 0.025, -0.18, -0.29, X + 0.025, -0.14, -0.22, 'by_dark', 0.008))
    o.append(L.rbox('by_win', X - 0.016, -0.17, -0.06, X + 0.016, -0.13, -0.02, 'by_lamp', 0.006))
    o.append(L.cylinder('by_gal', 0.12, 0.022, 'by_dark', (X, -0.06, 0.12), (0, 0, 0), None, 32))
    o.append(L.torus('by_rail', 0.112, 0.006, 'by_dark', (X, -0.06, 0.16), (0, 0, 0), None, (32, 6)))
    o.append(L.cylinder('by_lamp', 0.066, 0.085, 'by_lamp', (X, -0.06, 0.17), (0, 0, 0), None, 24))
    for k in range(4):
        t = k * math.pi / 2 + math.pi / 4
        o.append(C.box('by_bar', X + 0.066 * math.cos(t) - 0.005, -0.06 + 0.066 * math.sin(t) - 0.005, 0.13,
                       X + 0.066 * math.cos(t) + 0.005, -0.06 + 0.066 * math.sin(t) + 0.005, 0.215, 'by_dark'))
    o.append(L.cone('by_dome', 0.085, 0.0, 0.08, 'by_red', (X, -0.06, 0.253), (0, 0, 0), None, 24))
    o.append(L.sphere('by_knob', 0.014, 'by_dark', (X, -0.06, 0.298), seg=10, rings=5))
    # the beams, left and right
    for sg in (-1, 1):
        ln = (0.43 + X) if sg < 0 else (0.43 - X)
        o.append(flat_xz('by_beam', [(0.0, 0.012), (sg * ln, 0.08), (sg * ln, -0.025), (0.0, -0.012)], -0.02, 'by_beam',
                         (X, 0.17)))
    # the sea along the bottom: rolling waves with foam crests
    for (x, z, r, key) in ((-0.30, -0.40, 0.16, 'by_wave2'), (0.05, -0.43, 0.2, 'by_wave2'), (0.34, -0.40, 0.16, 'by_wave2'),
                           (-0.16, -0.46, 0.18, 'by_wave'), (0.20, -0.47, 0.19, 'by_wave')):
        wv = L.sphere('by_wave', r, key, (x, -0.12, z), (1.3, 0.5, 0.55), None, 24, 12)
        o.append(_clip_to(wv, clip))
    for (x, z) in ((-0.16, -0.365), (0.20, -0.37), (-0.30, -0.32), (0.05, -0.345)):
        fm = L.sphere('by_foam', 0.05, 'by_foam', (x, -0.19, z), (1.6, 0.4, 0.35), None, 12, 6)
        o.append(_clip_to(fm, clip))
    for (x, z, key) in ((-0.34, -0.25, 'by_cyan'), (0.12, -0.27, 'by_mag'), (-0.02, -0.33, 'by_cyan'), (0.30, -0.24, 'by_cyan'),
                        (-0.24, -0.36, 'by_mag')):
        o.append(L.sphere('by_spark', 0.012, key, (x, -0.2, z), seg=8, rings=4))
    # the tentacle: out of the waves on the right, curling in at the top
    pts = []
    n = 26
    for i in range(n):
        t = i / (n - 1)
        if t < 0.55:
            u = t / 0.55
            x, z = 0.27 + 0.03 * math.sin(u * math.pi), -0.38 + 0.42 * u
        else:
            u = (t - 0.55) / 0.45
            ang = u * 1.45 * math.pi
            rr = 0.075 * (1 - 0.45 * u)
            x, z = 0.27 - rr + rr * math.cos(ang), 0.04 + rr * math.sin(ang)
        pts.append((x, z, 0.052 * (1 - 0.72 * t)))
    for i, (x, z, r) in enumerate(pts):
        sp = L.sphere('by_tent', r, 'by_tent', (x, -0.16, z), seg=16, rings=8)
        if i < 4:
            _clip_to(sp, clip)
        o.append(sp)
        if 3 <= i < n - 3 and i % 3 == 0:
            # suckers on the inner (left) side
            if i + 1 < n:
                dx, dz = pts[i + 1][0] - x, pts[i + 1][1] - z
                ln = math.hypot(dx, dz) or 1
                nx_, nz_ = -dz / ln, dx / ln
                o.append(L.sphere('by_suck', r * 0.42, 'by_suck', (x + nx_ * r * 0.75, -0.19, z + nz_ * r * 0.75),
                                  (1, 0.5, 1), None, 10, 5))
    return o, [clip]


EMBLEMS = {'city': emblem_city, 'castle': emblem_castle, 'desert': emblem_desert, 'forest': emblem_forest,
           'dino': emblem_dino, 'bay': emblem_bay,
           'swamp': emblem_swamp, 'graveyard': emblem_graveyard, 'lock': emblem_lock}


def g_emblems():
    for zone, fn in EMBLEMS.items():
        nm = 'emblem_' + zone
        if not want(nm):
            continue
        objs, extra = fn()
        L.update()
        camera((0, 0, 0.0), (0.0, 1.0, 0.0), ortho=1.06)
        lt = C.add_point_light((-1.2, -2.0, 1.6), (1.0, 0.95, 0.9), 120.0, 0.6, 'emb_key')
        ui_render(nm, EMB, EMB, objs, a.samples, extra={'zone': zone})
        remove(objs + extra + [lt])


# ---------------------------------------------------------------------------
# the world map: a hub (2026-09-27)
#
# Tommy's house at the bottom centre; a main path runs up the middle and a
# branch leaves it into each zone. Two zones per tier, left and right:
#   tier 1  city   | dino        (open)
#   tier 2  forest | bay         (open; the bay's cove opens on the east sea)
#   tier 3  castle | desert      (opened by stars: the watch draws the locks)
#   tier 4  swamp  | graveyard   (future zones, under the magic fog)
# Each built zone has 4 pads on a short winding branch; the 4th (the lair)
# sits by the zone's landmark.
# ---------------------------------------------------------------------------

MAP_W, MAP_H = 368, 1400
MX = 12.0                        # metres across the picture
MAP_EL = math.radians(58)        # the camera looks north (+Y) and down
PX_M = MAP_W / MX
MAP_DEPTH = (MAP_H / PX_M) / math.sin(MAP_EL)
TIER_Y = [5.2, 18.6, 32.0, 45.4]  # northern edge of the home lawn and of tiers 1-3
TIER_GRID = [5.2, 17.7, 30.2, 42.7]  # the design grid the positions below are written in


def ty(y):
    """Design-grid y (12.5 m tiers) -> map y (the real, taller tiers)."""
    if y <= TIER_GRID[0]:
        return y
    for k in range(3):
        if y <= TIER_GRID[k + 1]:
            return TIER_Y[k] + (y - TIER_GRID[k]) * (TIER_Y[k + 1] - TIER_Y[k]) / (TIER_GRID[k + 1] - TIER_GRID[k])
    return y + TIER_Y[3] - TIER_GRID[3]


def _p(x, y):
    return (x, ty(y))

TIERS = [('city', 'dino'), ('forest', 'bay'), ('castle', 'desert'), ('swamp', 'graveyard')]
ZONES = ['city', 'castle', 'desert', 'forest', 'dino', 'bay']
HOME_Y = 1.95                    # the house pad, where the main path starts
FOG_Y = TIER_Y[3]
FORK_Y = FOG_Y + 0.35
COVE = (ty(19.6), ty(26.0))       # Abyss Bay's cove in the east shore
CAPE = (ty(26.9), ty(29.5))       # and the lighthouse's cape
SWAMP_PAD = _p(2.9, 45.0)
GRAVE_PAD = _p(9.1, 45.2)
HUT_C = _p(3.1, 47.6)
GRAVE_C = _p(8.9, 48.0)
CASTLE_C = _p(2.9, 41.0)
PYRAMID_C = _p(9.45, 41.55)
OASIS_C = _p(10.75, 37.3)
VOLCANO_C = _p(10.25, 16.35)
LIGHT_C = _p(10.85, 28.15)
CLOCK_C = _p(3.75, 15.55)
MILL_C = _p(4.35, 26.3)
RIVER_Y = ty(25.75)                 # the forest's stream, between pads 3 and 4

# the pads of each zone, level 1 -> 4, and where each branch leaves the main path
PADS = {
    'city':   [_p(4.3, 7.8), _p(2.0, 9.4), _p(3.8, 11.8), _p(2.2, 14.2)],
    'dino':   [_p(7.7, 8.2), _p(10.0, 9.8), _p(8.2, 12.2), _p(9.3, 14.2)],
    'forest': [_p(4.3, 20.3), _p(2.0, 21.9), _p(3.8, 24.3), _p(2.3, 27.0)],
    'bay':    [_p(7.6, 20.4), _p(9.1, 22.5), _p(7.7, 24.7), _p(9.5, 26.9)],
    'castle': [_p(4.3, 32.8), _p(2.0, 34.4), _p(3.8, 36.8), _p(2.9, 39.3)],
    'desert': [_p(7.7, 33.0), _p(10.0, 34.6), _p(8.2, 37.0), _p(9.45, 39.45)],
}
JUNCTION = {'city': ty(6.6), 'dino': ty(7.1), 'forest': ty(19.1), 'bay': ty(19.5), 'castle': ty(31.6), 'desert': ty(32.0)}


def _n(x, y, s=1.0, seed=0.0):
    return noise.noise(V((x * s + seed, y * s - seed, seed * 0.37)))


def _bump(y, a, b):
    if y <= a or y >= b:
        return 0.0
    return 0.5 - 0.5 * math.cos(2 * math.pi * (y - a) / (b - a))


def path_x(y):
    """The main path, up the middle with a gentle sway."""
    return 6.0 + 0.28 * math.sin(y * 0.45 + 0.5) + 0.12 * math.sin(y * 1.2)


def border(k, x):
    """The wavy northern border of the home lawn (k = 0) and tiers 1-3."""
    return TIER_Y[k] + 0.5 * math.sin(x * 0.55 + 1.7 * k) + 0.28 * _n(x, 0.5, 0.6, 20.0 + k)


def tier_of(x, y):
    if y < border(0, x):
        return -1
    for k in (1, 2, 3):
        if y < border(k, x):
            return k - 1
    return 3


def zone_of(x, y):
    t = tier_of(x, y)
    if t < 0:
        return 'home'
    if t == 3:
        left = x < 6.0 + 0.6 * _n(x, y, 0.5, 12.0)
    else:
        left = x < path_x(y)
    return TIERS[t][0 if left else 1]


band_of = zone_of


def coast(y):
    """x of the west and east shores at y. The east shore opens a cove for
    Abyss Bay's harbour and pushes out a cape for its lighthouse."""
    xl = 0.62 + 0.3 * math.sin(y * 0.33 + 2.0) + 0.25 * _n(0.3, y, 0.45, 1.0)
    xr = MX - 0.62 - 0.3 * math.sin(y * 0.29 + 0.4) - 0.25 * _n(0.7, y, 0.45, 2.0)
    xr -= 1.6 * _bump(y, COVE[0], COVE[1])
    xr += 0.7 * _bump(y, CAPE[0], CAPE[1])
    return max(0.3, xl), min(MX - 0.1, xr)


def south_shore(x):
    return 0.9 + 0.35 * _n(x, 0.2, 0.4, 3.0)


def land_mask(x, y):
    """1 on land, 0 at sea, soft over ~0.3 m."""
    xl, xr = coast(y)
    d = min(x - xl, xr - x, y - south_shore(x))
    return max(0.0, min(1.0, d / 0.3 + 0.5))


def hill(x, y, cx, cy, h, sx, sy):
    return h * math.exp(-(((x - cx) / sx) ** 2 + ((y - cy) / sy) ** 2))


def terrain_h(x, y):
    m = land_mask(x, y)
    if m <= 0:
        return -0.35
    h = 0.24 + 0.05 * _n(x, y, 1.3, 5.0)
    # the castle's crag: flat-topped, so it reads as a hill from above
    dc = math.hypot((x - CASTLE_C[0]) / 1.75, (y - CASTLE_C[1]) / 1.3) + 0.1 * _n(x, y, 1.2, 40.0)
    h += 1.2 * max(0.0, min(1.0, (1.25 - dc) / 0.5)) ** 1.5
    z = zone_of(x, y)
    if z == 'desert':
        h += 0.07 * math.sin(x * 2.2 + y * 0.9 + 1.5 * _n(x, y, 0.5, 7.0)) + 0.05
    elif z == 'forest':
        h += 0.10 * (0.5 + 0.5 * _n(x, y, 0.6, 9.0))
        dr = abs(y - river_y(x))
        if x < 5.3 and dr < 0.6:
            h -= 0.09 * (1 - dr / 0.6) ** 0.5
    elif z == 'dino':
        h += 0.08 * (0.5 + 0.5 * _n(x, y, 0.8, 17.0))
    h += hill(x, y, GRAVE_C[0], GRAVE_C[1], 1.35, 1.9, 1.5)
    if tier_of(x, y) == 3 and x < 6.5:
        h -= 0.06
    k = m ** 0.5
    return h * k + (-0.35) * (1 - k)


def river_y(x):
    """The forest stream's centre line (it runs west from the mill pond)."""
    return RIVER_Y + 0.22 * math.sin(x * 1.3)


def volcano_r(z):
    """The volcano's radius at height z above its foot (a lathe profile)."""
    prof = VOLC_PROFILE
    for (r0, z0), (r1, z1) in zip(prof[:-1], prof[1:]):
        if z0 <= z <= z1:
            return r0 + (r1 - r0) * (z - z0) / (z1 - z0)
    return 0.0


VOLC_PROFILE = [(1.62, 0.0), (1.35, 0.35), (0.95, 0.95), (0.62, 1.55), (0.46, 1.9)]


def volcano_z(x, y):
    """Height of the volcano's flank at (x, y) above the terrain, 0 off it."""
    d = math.hypot(x - VOLCANO_C[0], y - VOLCANO_C[1])
    prof = VOLC_PROFILE
    for (r0, z0), (r1, z1) in zip(prof[:-1], prof[1:]):
        if r1 <= d <= r0:
            return z0 + (z1 - z0) * (r0 - d) / (r0 - r1)
    return 0.0


ZONE_COL = {
    'home': ['#7cbc52', '#62a444'],
    'city': ['#6f757c', '#5c6268'],
    'castle': ['#7a6aa0', '#5d4d86'],
    'desert': ['#dcae68', '#c8924a'],
    'forest': ['#4f9a44', '#2f7236'],
    'dino': ['#b0603c', '#94492e'],
    'dino_grass': ['#62ac3a', '#3f9034'],
    'bay': ['#7e9e98', '#61827e'],
    'fog_swamp': ['#4e6a54', '#3a5444'],
    'fog_grave': ['#6c7a70', '#56645a'],
}


def _mixc(a_, b_, t):
    return [a_[i] * (1 - t) + b_[i] * t for i in range(3)]


def ground_color(x, y, h):
    z = zone_of(x, y)
    if z in ('swamp', 'graveyard'):
        z = 'fog_swamp' if z == 'swamp' else 'fog_grave'
    c0, c1 = ZONE_COL[z]
    t = 0.5 + 0.5 * _n(x, y, 1.6, 13.0)
    col = _mixc(L.hexlin(c0), L.hexlin(c1), t)
    if z == 'dino':
        # red-brown soil with patches of jungle grass, dark basalt round the volcano
        g = _n(x, y, 0.75, 44.0) + 0.25 * _n(x, y, 2.2, 45.0)
        if g > 0.02:
            gc = _mixc(L.hexlin(ZONE_COL['dino_grass'][0]), L.hexlin(ZONE_COL['dino_grass'][1]), t)
            col = _mixc(col, gc, min(1.0, (g - 0.02) / 0.12))
        dv = math.hypot(x - VOLCANO_C[0], y - VOLCANO_C[1])
        if dv < 2.6:
            k = max(0.0, min(1.0, (2.6 - dv) / 0.6))
            col = _mixc(col, L.hexlin('#4a3c3c'), k)
    elif z == 'bay':
        # quay flagstones: a faint grid
        gx, gy = x % 0.7, y % 0.55
        if gx < 0.05 or gy < 0.05:
            col = _mixc(col, L.hexlin('#40585a'), 0.6)
    # steep ground is rock (the castle crag, the graveyard hill)
    e = 0.06
    gx = (terrain_h(x + e, y) - terrain_h(x - e, y)) / (2 * e)
    gy = (terrain_h(x, y + e) - terrain_h(x, y - e)) / (2 * e)
    sl = math.hypot(gx, gy)
    if sl > 0.9 and h > 0.3:
        rk = L.hexlin('#4e4262') if z == 'castle' else L.hexlin('#5a5e5a')
        k = max(0.0, min(1.0, (sl - 0.9) / 0.8))
        col = _mixc(col, rk, k)
    if z == 'city':
        # a street grid
        gx, gy = (x - 0.3) % 1.6, (y - TIER_Y[0] - 0.2) % 1.5
        if gx < 0.28 or gy < 0.26:
            col = list(L.hexlin('#3e4248'))
    # beach where land meets the sea, darker earth on the cliff
    if h < 0.19:
        s = L.hexlin('#e6d09a') if h > -0.05 else L.hexlin('#8a6a48')
        if z == 'dino' and h > -0.05:
            s = L.hexlin('#5a4a44')          # black volcanic sand
        k = max(0.0, min(1.0, (0.19 - h) / 0.12))
        col = _mixc(col, s, k)
    return col


def build_terrain():
    nx, ny = 150, int(420 * (MAP_DEPTH + 1.7) / 36.3)
    y0, y1 = -0.5, MAP_DEPTH + 1.2
    xs = [MX * i / (nx - 1) for i in range(nx)]
    ys = [y0 + (y1 - y0) * j / (ny - 1) for j in range(ny)]
    verts, faces, cols = [], [], []
    for j, y in enumerate(ys):
        for i, x in enumerate(xs):
            h = terrain_h(x, y)
            verts.append((x, y, h))
            cols.append(ground_color(x, y, h))
    for j in range(ny - 1):
        for i in range(nx - 1):
            k = j * nx + i
            faces.append((k, k + 1, k + nx + 1, k + nx))
    me = bpy.data.meshes.new('terrain')
    me.from_pydata(verts, [], faces)
    me.validate()
    ca = me.color_attributes.new('col', 'FLOAT_COLOR', 'POINT')
    for i, c in enumerate(cols):
        ca.data[i].color = (c[0], c[1], c[2], 1.0)
    ob = bpy.data.objects.new('terrain', me)
    C.link(ob)
    L.smooth(ob)

    def tb(nt, neutral):
        N, Lk = nt.nodes, nt.links
        p = N.new('ShaderNodeBsdfPrincipled')
        p.inputs['Roughness'].default_value = 0.85
        p.inputs['Specular'].default_value = 0.2
        at = N.new('ShaderNodeAttribute')
        at.attribute_name = 'col'
        Lk.new(at.outputs['Color'], p.inputs['Base Color'])
        return p.outputs['BSDF']
    C.mat('map_terrain', build=tb)
    C.assign(ob, 'map_terrain')
    return ob


def build_sea():
    def sb(nt, neutral):
        N, Lk = nt.nodes, nt.links
        p = N.new('ShaderNodeBsdfPrincipled')
        p.inputs['Roughness'].default_value = 0.25
        p.inputs['Specular'].default_value = 0.6
        wv = N.new('ShaderNodeTexWave')
        wv.inputs['Scale'].default_value = 1.2
        wv.inputs['Distortion'].default_value = 6.0
        rp = L._ramp(nt, [(0.0, L.hexlin('#16244e')), (0.8, L.hexlin('#1d3264')), (1.0, L.hexlin('#3a5a94'))])
        Lk.new(wv.outputs['Fac'], rp.inputs['Fac'])
        # Abyss Bay's water: petrol blue round the cove
        rp2 = L._ramp(nt, [(0.0, L.hexlin('#0c4a5c')), (0.8, L.hexlin('#10606e')), (1.0, L.hexlin('#2a9aa4'))])
        Lk.new(wv.outputs['Fac'], rp2.inputs['Fac'])
        geo = N.new('ShaderNodeNewGeometry')
        sub = N.new('ShaderNodeVectorMath')
        sub.operation = 'SUBTRACT'
        Lk.new(geo.outputs['Position'], sub.inputs[0])
        sub.inputs[1].default_value = (11.6, ty(23.4), 0.0)
        flat = N.new('ShaderNodeVectorMath')
        flat.operation = 'MULTIPLY'
        Lk.new(sub.outputs['Vector'], flat.inputs[0])
        flat.inputs[1].default_value = (1.0, 0.75, 0.0)
        ln = N.new('ShaderNodeVectorMath')
        ln.operation = 'LENGTH'
        Lk.new(flat.outputs['Vector'], ln.inputs[0])
        mr = N.new('ShaderNodeMapRange')
        mr.inputs['From Min'].default_value = 1.8
        mr.inputs['From Max'].default_value = 4.5
        mr.inputs['To Min'].default_value = 1.0
        mr.inputs['To Max'].default_value = 0.0
        Lk.new(ln.outputs['Value'], mr.inputs['Value'])
        mx = N.new('ShaderNodeMixRGB')
        Lk.new(mr.outputs['Result'], mx.inputs['Fac'])
        Lk.new(rp.outputs['Color'], mx.inputs['Color1'])
        Lk.new(rp2.outputs['Color'], mx.inputs['Color2'])
        Lk.new(mx.outputs['Color'], p.inputs['Base Color'])
        return p.outputs['BSDF']
    C.mat('map_sea', build=sb)
    return C.box('sea', -2, -3, -0.5, MX + 2, MAP_DEPTH + 4, 0.0, 'map_sea')


def ground_z(x, y):
    return terrain_h(x, y) + volcano_z(x, y)


def ribbon(name, pts, width, key, lift=0.035, hf=None):
    hf = hf or terrain_h
    verts, faces = [], []
    for i, (x, y) in enumerate(pts):
        if i < len(pts) - 1:
            dx, dy = pts[i + 1][0] - x, pts[i + 1][1] - y
        else:
            dx, dy = x - pts[i - 1][0], y - pts[i - 1][1]
        ln = math.hypot(dx, dy) or 1.0
        w = width(i / max(1, len(pts) - 1)) if callable(width) else width
        nx_, ny_ = -dy / ln * w / 2, dx / ln * w / 2
        for sgn in (-1, 1):
            px_, py_ = x + sgn * nx_, y + sgn * ny_
            verts.append((px_, py_, hf(px_, py_) + lift))
    for i in range(len(pts) - 1):
        k = 2 * i
        faces.append((k, k + 1, k + 3, k + 2))
    return L.mesh_from(name, verts, faces, key)


def catmull(pts, step=0.08):
    """A smooth curve through pts (Catmull-Rom), sampled about every `step` m."""
    P = [V((p[0], p[1], 0)) for p in pts]
    P = [P[0] * 2 - P[1]] + P + [P[-1] * 2 - P[-2]]
    out = []
    for i in range(1, len(P) - 2):
        p0, p1, p2, p3 = P[i - 1], P[i], P[i + 1], P[i + 2]
        n = max(2, int((p2 - p1).length / step))
        for k in range(n):
            t = k / n
            t2, t3 = t * t, t * t * t
            q = 0.5 * ((2 * p1) + (-p0 + p2) * t + (2 * p0 - 5 * p1 + 4 * p2 - p3) * t2 + (-p0 + 3 * p1 - 3 * p2 + p3) * t3)
            out.append((q.x, q.y))
    out.append((pts[-1][0], pts[-1][1]))
    return out


# what the props must leave free: path samples (x, y, half width) and circles
_PATHS = []
_KEEP = []
_PADS_ALL = []


def clear(x, y, r, h=0.0):
    for (px_, py_, w) in _PATHS:
        if (x - px_) ** 2 + (y - py_) ** 2 < (r + w) ** 2:
            return False
    for (kx, ky, kr) in _KEEP:
        if (x - kx) ** 2 + (y - ky) ** 2 < (r + kr) ** 2:
            return False
    if h > 0:
        # nothing tall right in front of (south of) a pad: it would hide it
        for (px_, py_) in _PADS_ALL:
            if abs(x - px_) < r + 0.45 and 0 < py_ - y < h * 0.63 + r + 0.45:
                return False
    return True


def zone_box(zone):
    for k, pair in enumerate(TIERS):
        if zone in pair:
            y0 = TIER_Y[k] - 0.6
            y1 = TIER_Y[k + 1] + 0.6 if k < 3 else MAP_DEPTH
            return (0.3, 6.3, y0, y1) if pair[0] == zone else (5.7, MX - 0.2, y0, y1)
    return (0.3, MX - 0.3, 0.6, TIER_Y[0] + 0.6)


def scatter(rnd, zone, n, r, h, make, keep=0.8, inland=1.0, limit=None):
    """Try n random spots in the zone; make(x, y, z) -> objects for each one
    that is on land, in the zone and clear of paths, pads and other props."""
    x0, x1, y0, y1 = zone_box(zone)
    o = []
    placed = 0
    for i in range(n):
        if limit is not None and placed >= limit:
            break
        x, y = rnd.uniform(x0, x1), rnd.uniform(y0, y1)
        if zone_of(x, y) != zone or land_mask(x, y) < inland or not clear(x, y, r, h):
            continue
        o += make(x, y, terrain_h(x, y))
        _KEEP.append((x, y, r * keep))
        placed += 1
    return o


def along(pts, n, off, t0=0.05, t1=0.95):
    """n points beside a polyline, alternately left and right of it."""
    out = []
    for k in range(n):
        t = t0 + (t1 - t0) * (k / max(1, n - 1))
        i = min(len(pts) - 2, int(t * (len(pts) - 1)))
        (x, y), (x2, y2) = pts[i], pts[i + 1]
        dx, dy = x2 - x, y2 - y
        ln = math.hypot(dx, dy) or 1.0
        s = off if k % 2 else -off
        q = (x - dy / ln * s, y + dx / ln * s)
        if any(math.hypot(q[0] - px_, q[1] - py_) < 0.65 for (px_, py_) in _PADS_ALL):
            continue
        out.append(q)
    return out


def g_map():
    if not want('map'):
        return
    samples = MAP_DRAFT_SAMPLES if SAMPLE else a.samples
    rnd = random.Random(21)
    _PATHS.clear()
    _KEEP.clear()
    _PADS_ALL.clear()
    t0 = time.time()
    objs = [build_terrain(), build_sea()]
    print('[map] terrain %.1fs' % (time.time() - t0))
    C.mat('m_path', base=L.hexlin('#ecdcae'), rough=0.8)
    C.mat('m_path_edge', base=L.hexlin('#6a5236'), rough=0.9)
    C.mat('m_pad', build=L.gloss_build('#d8d2c4', rough=0.6, coat=0.2))
    C.mat('m_pad_side', base=L.hexlin('#7a7266'), rough=0.8)
    C.mat('m_pad_ring', build=L.gold_build(emit=0.5, rim=0.5))
    C.mat('m_pad_lk', build=L.gloss_build('#5a5470', rough=0.6, coat=0.2))
    C.mat('m_pad_home', build=L.gloss_build('#f0b440', emit_col='#ffc040', emit=0.35, rough=0.4, coat=0.4))
    C.mat('m_pad_ring_lk', build=L.gloss_build('#9a70ff', emit_col='#9a70ff', emit=0.8, rough=0.3))
    # --- the paths: the main one up the middle, a branch into each zone, the fork into the fog
    ys = [HOME_Y + 0.1 * i for i in range(int((FORK_Y - HOME_Y) / 0.1) + 1)]
    main = [(path_x(y), y) for y in ys]
    fx = path_x(FORK_Y)
    left = [(fx + (SWAMP_PAD[0] - fx) * t + 0.35 * math.sin(t * math.pi), FORK_Y + (SWAMP_PAD[1] - FORK_Y) * t)
            for t in [i / 24 for i in range(25)]]
    right = [(fx + (GRAVE_PAD[0] - fx) * t - 0.35 * math.sin(t * math.pi), FORK_Y + (GRAVE_PAD[1] - FORK_Y) * t)
             for t in [i / 24 for i in range(25)]]
    branches = {}
    for zone in ZONES:
        yj = JUNCTION[zone]
        branches[zone] = catmull([(path_x(yj), yj)] + PADS[zone])
    for nm, pts, w in [('main', main, 0.44), ('left', left, 0.40), ('right', right, 0.40)] + \
            [(z, branches[z], 0.38) for z in ZONES]:
        objs.append(ribbon('path_e_' + nm, pts, w + 0.18, 'm_path_edge', 0.03))
        objs.append(ribbon('path_' + nm, pts, w, 'm_path', 0.065 if nm == 'main' else 0.05))
        for (x, y) in pts[::2]:
            _PATHS.append((x, y, (w + 0.18) / 2))
    # --- the pads
    pads = {z: [V((x, y, 0)) for (x, y) in PADS[z]] for z in ZONES}
    pads['house'] = [V((path_x(HOME_Y), HOME_Y, 0))]
    pads['swamp'] = [V(SWAMP_PAD + (0,))]
    pads['graveyard'] = [V(GRAVE_PAD + (0,))]
    for zone, lst in pads.items():
        for p in lst:
            p.z = terrain_h(p.x, p.y) + 0.06
            _PADS_ALL.append((p.x, p.y))
            _KEEP.append((p.x, p.y, 0.5))
            lk = zone in ('swamp', 'graveyard')
            top = 'm_pad_home' if zone == 'house' else 'm_pad_lk' if lk else 'm_pad'
            ring = 'm_pad_ring_lk' if lk else 'm_pad_ring'
            objs.append(L.cylinder('pad', 0.42, 0.14, 'm_pad_side', (p.x, p.y, p.z - 0.02), (0, 0, 0), None, 32))
            objs.append(L.cylinder('pad_top', 0.36, 0.05, top, (p.x, p.y, p.z + 0.06), (0, 0, 0), None, 32))
            objs.append(L.torus('pad_ring', 0.37, 0.025, ring, (p.x, p.y, p.z + 0.075), (0, 0, 0), None, (32, 8)))
    # the landmarks' ground stays free of scattered props
    for (c, r) in ((CLOCK_C, 0.75), (VOLCANO_C, 1.75), (MILL_C, 0.8), (LIGHT_C, 0.8), (CASTLE_C, 1.9), (PYRAMID_C, 1.6),
                   (OASIS_C, 1.15), (HUT_C, 1.0)):
        _KEEP.append((c[0], c[1], r))
    # --- the zones
    hp = pads['house'][0]
    lights = []
    objs += map_home(rnd, hp)
    objs += map_city(rnd, branches['city'])
    d_objs, d_lights = map_dino(rnd, branches['dino'])
    objs += d_objs
    lights += d_lights
    objs += map_forest(rnd, branches['forest'])
    b_objs, b_lights = map_bay(rnd, branches['bay'])
    objs += b_objs
    lights += b_lights
    objs += map_castle(rnd) + map_desert(rnd) + map_future(rnd)
    fog = map_fog(rnd)
    L.update()
    print('[map] scene %.1fs, %d objects' % (time.time() - t0, len(objs) + len(fog)))
    # --- camera: ortho, looking north and down
    Fd = V((0, math.cos(MAP_EL), -math.sin(MAP_EL)))
    Hm = MAP_H / PX_M
    # the picture's vertical centre is where the ground plane z = 0.25 meets the view axis
    cy = (MAP_DEPTH / 2) - 0.25 / math.tan(MAP_EL) + 0.1
    camera((MX / 2, cy, 0.25), Fd, ortho=Hm, dist=60.0)
    sc = C._STATE['scene']                  # the frame, before projecting the pads
    sc.render.resolution_x, sc.render.resolution_y = MAP_W, MAP_H
    C._STATE['cam'].data.sensor_fit = 'VERTICAL'
    moon = C.add_point_light((MX * 0.5, MAP_DEPTH * 0.55, 12), (0.8, 0.85, 1.0), 0.0, 1.0, 'map_fill')
    info_levels = {z: [px_of(p, MAP_W, MAP_H) for p in pads[z]] for z in ZONES}
    locked = {z: px_of(pads[z][0], MAP_W, MAP_H) for z in ('swamp', 'graveyard')}
    info_levels['house'] = px_of(pads['house'][0], MAP_W, MAP_H)
    ui_render('map', MAP_W, MAP_H, objs + fog, samples, fit='VERTICAL',
              extra={'levels': info_levels, 'locked': locked, 'house': info_levels['house'], 'draft': bool(SAMPLE),
                     'pad_radius_px': round(0.42 * PX_M, 1), 'scroll': 'vertical, zone 1 at the bottom',
                     'layout': 'hub: house at the bottom centre; tiers city|dino, forest|bay, castle|desert, '
                               'swamp|graveyard (fogged)'})
    remove(objs + fog + [moon] + lights)


def _win_mat():
    if 'm_win' not in C._MATS:
        C.mat('m_win', build=L.emit_build('#ffb040', 2.2))
        C.mat('m_win_v', build=L.emit_build('#ff9a30', 2.0))


def on_path(x, y, r=0.7):
    return not clear(x, y, r)


def house_mats():
    if 'h_wall' in C._MATS:
        return
    C.mat('h_wall', build=L.gloss_build('#f3dfa8', rough=0.8, coat=0.0, spec=0.3))
    C.mat('h_trim', build=L.gloss_build('#ffffff', rough=0.6, coat=0.0, spec=0.3))
    C.mat('h_roof', build=L.gloss_build('#c63a2c', emit_col='#c63a2c', emit=0.06, rough=0.6, coat=0.2))
    C.mat('h_brick', base=L.hexlin('#9a4a36'), rough=0.9)
    C.mat('h_door', build=L.gloss_build('#6a3a8a', rough=0.5, coat=0.3))
    C.mat('h_found', base=L.hexlin('#7a7470'), rough=0.9)
    C.mat('h_win', build=L.emit_build('#ffc864', 2.0))
    C.mat('h_pk', build=L.gloss_build('#ff7a10', emit_col='#ff6a00', emit=0.15, rough=0.45, coat=0.3))
    C.mat('h_pkface', build=L.emit_build('#ffd040', 3.0))
    C.mat('h_leaf', base=L.hexlin('#e2862a'), rough=0.8)
    C.mat('h_leaf2', base=L.hexlin('#c85a22'), rough=0.8)
    C.mat('h_trunk', base=L.hexlin('#5a3a22'), rough=0.9)
    C.mat('h_mail', build=L.gloss_build('#3a6ae0', rough=0.4, coat=0.5))


def build_house(cx, cy, z0, s=1.0, detail=False):
    """Tommy's house: a two-storey cottage, lit windows, a red gable roof, a
    purple door and jack-o'-lanterns on the step. Faces -Y."""
    house_mats()
    o = []
    W, D, H = 1.2 * s, 0.9 * s, 0.78 * s
    x0, x1, y0, y1 = cx - W / 2, cx + W / 2, cy - D / 2, cy + D / 2
    o.append(C.box('h_found', x0 - 0.02 * s, y0 - 0.02 * s, z0 - 0.1, x1 + 0.02 * s, y1 + 0.02 * s, z0 + 0.08 * s, 'h_found'))
    o.append(C.box('h_body', x0, y0, z0 + 0.08 * s, x1, y1, z0 + H, 'h_wall'))
    # gable roof along X
    ov = 0.09 * s
    rz, rt = z0 + H, z0 + H + 0.55 * s
    v = [(x0 - ov, y0 - ov, rz - 0.04 * s), (x1 + ov, y0 - ov, rz - 0.04 * s), (x1 + ov, cy, rt), (x0 - ov, cy, rt),
         (x0 - ov, y1 + ov, rz - 0.04 * s), (x1 + ov, y1 + ov, rz - 0.04 * s)]
    f = [(0, 1, 2, 3), (3, 2, 5, 4), (0, 3, 4), (1, 5, 2), (0, 4, 5, 1)]
    rf = L.mesh_from('h_roof', v, f, 'h_roof', sm=False)
    sol = rf.modifiers.new('solid', 'SOLIDIFY')
    sol.thickness = 0.05 * s
    o.append(rf)
    gv = [(x0, y0 + 0.001, rz), (x1, y0 + 0.001, rz), (cx, y0 + 0.001, rz + 0.5 * s - 0.04 * s)]
    o.append(L.mesh_from('h_gable', gv + [(x0, y1, rz), (x1, y1, rz), (cx, y1, rz + 0.46 * s)],
                         [(0, 1, 2), (3, 5, 4), (0, 3, 4, 1), (0, 2, 5, 3), (1, 4, 5, 2)], 'h_wall', sm=False))
    o.append(C.box('h_chim', x1 - 0.3 * s, cy + 0.05 * s, rz + 0.2 * s, x1 - 0.16 * s, cy + 0.2 * s, rt + 0.12 * s, 'h_brick'))
    # door, step, windows
    o.append(C.box('h_door', cx - 0.12 * s, y0 - 0.02 * s, z0 + 0.08 * s, cx + 0.12 * s, y0 + 0.01, z0 + 0.46 * s, 'h_door'))
    o.append(C.box('h_step', cx - 0.2 * s, y0 - 0.14 * s, z0 - 0.02, cx + 0.2 * s, y0, z0 + 0.08 * s, 'h_found'))
    wins = [(-0.34, 0.2, 0.42), (0.34, 0.2, 0.42), (-0.34, 0.5, 0.7), (0.34, 0.5, 0.7), (0.0, 0.5, 0.7)]
    for (dx, zb, zt) in wins:
        if dx == 0.0 and not detail:
            continue
        wx = cx + dx * s
        o.append(C.box('h_winf', wx - 0.11 * s, y0 - 0.015 * s, z0 + zb * s - 0.02 * s, wx + 0.11 * s, y0 + 0.005, z0 + zt * s + 0.02 * s, 'h_trim'))
        o.append(C.box('h_win', wx - 0.08 * s, y0 - 0.025 * s, z0 + zb * s, wx + 0.08 * s, y0 - 0.01 * s, z0 + zt * s, 'h_win'))
    # the round attic window
    o.append(L.cylinder('h_attic', 0.075 * s, 0.02 * s, 'h_win', (cx, y0 - 0.01 * s, rz + 0.2 * s), (math.pi / 2, 0, 0), None, 20))
    # a window on the right side too
    o.append(C.box('h_winr', x1 - 0.01, cy - 0.12 * s, z0 + 0.22 * s, x1 + 0.02 * s, cy + 0.12 * s, z0 + 0.44 * s, 'h_win'))
    # jack-o'-lanterns on the step
    for dx in (-0.3, 0.3):
        px_, py_ = cx + dx * s, y0 - 0.1 * s
        o.append(L.sphere('h_pk', 0.075 * s, 'h_pk', (px_, py_, z0 + 0.06 * s), (1, 1, 0.8), None, 16, 8))
        if detail:
            for ex in (-0.025, 0.025):
                o.append(C.box('h_pkeye', px_ + ex * s - 0.012 * s, py_ - 0.08 * s, z0 + 0.07 * s, px_ + ex * s + 0.012 * s,
                               py_ - 0.06 * s, z0 + 0.095 * s, 'h_pkface'))
            o.append(C.box('h_pkm', px_ - 0.03 * s, py_ - 0.08 * s, z0 + 0.035 * s, px_ + 0.03 * s, py_ - 0.06 * s, z0 + 0.05 * s, 'h_pkface'))
    # an autumn tree and the mailbox
    tx, ty = x0 - 0.35 * s, cy + 0.15 * s
    o.append(L.cylinder('h_trunk', 0.05 * s, 0.5 * s, 'h_trunk', (tx, ty, z0 + 0.25 * s), (0, 0, 0), None, 10))
    for (dx, dy, dz, r, k) in ((0, 0, 0.62, 0.3, 'h_leaf'), (0.14, -0.08, 0.5, 0.2, 'h_leaf2'), (-0.14, 0.05, 0.52, 0.22, 'h_leaf2')):
        o.append(L.sphere('h_crown', r * s, k, (tx + dx * s, ty + dy * s, z0 + dz * s), (1, 1, 0.9), None, 20, 10))
    mx_, my_ = cx + 0.45 * s, y0 - 0.45 * s
    o.append(C.box('h_mpost', mx_ - 0.015 * s, my_ - 0.015 * s, z0, mx_ + 0.015 * s, my_ + 0.015 * s, z0 + 0.22 * s, 'h_trunk'))
    o.append(L.rbox('h_mbox', mx_ - 0.05 * s, my_ - 0.08 * s, z0 + 0.22 * s, mx_ + 0.05 * s, my_ + 0.08 * s, z0 + 0.3 * s, 'h_mail', 0.02 * s))
    if detail:
        # a white picket fence along the front, with a gap at the path
        for i in range(-9, 10):
            fx_ = cx + i * 0.12 * s
            if abs(fx_ - cx) < 0.22 * s:
                continue
            o.append(C.box('h_picket', fx_ - 0.02 * s, y0 - 0.62 * s, z0, fx_ + 0.02 * s, y0 - 0.6 * s, z0 + 0.2 * s, 'h_trim'))
        for zz in (0.07, 0.15):
            for sgn in (-1, 1):
                a_, b_ = (cx + 0.22 * s, cx + 1.1 * s) if sgn > 0 else (cx - 1.1 * s, cx - 0.22 * s)
                o.append(C.box('h_rail', a_, y0 - 0.625 * s, z0 + zz * s, b_, y0 - 0.605 * s, z0 + zz * s + 0.025 * s, 'h_trim'))
    return o


def map_home(rnd, pad):
    """Tommy's house at the foot of the main path, on its lawn, with a couple
    of autumn trees and pumpkins."""
    hx, hy = pad.x - 1.55, pad.y + 0.3
    o = build_house(hx, hy, terrain_h(hx, hy), 1.15)
    _KEEP.append((hx, hy, 1.1))
    for (x, y, s) in ((pad.x + 1.6, pad.y + 0.9, 1.0), (pad.x + 2.7, pad.y + 0.2, 0.8), (hx - 1.7, hy + 0.6, 0.9),
                      (pad.x + 3.8, pad.y + 1.3, 0.85), (hx - 3.0, hy + 0.1, 0.75)):
        if land_mask(x, y) < 1 or not clear(x, y, 0.35):
            continue
        z0 = terrain_h(x, y)
        o.append(L.cylinder('h_trunk', 0.05 * s, 0.5 * s, 'h_trunk', (x, y, z0 + 0.25 * s), (0, 0, 0), None, 10))
        for (dx, dy, dz, r, k) in ((0, 0, 0.62, 0.3, 'h_leaf'), (0.14, -0.08, 0.5, 0.2, 'h_leaf2'), (-0.14, 0.05, 0.52, 0.22, 'h_leaf2')):
            o.append(L.sphere('h_crown', r * s, k, (x + dx * s, y + dy * s, z0 + dz * s), (1, 1, 0.9), None, 20, 10))
        _KEEP.append((x, y, 0.4))
    for (x, y) in ((pad.x + 0.75, pad.y - 0.35), (pad.x - 0.6, pad.y - 0.55), (pad.x + 1.2, pad.y + 0.1)):
        z0 = terrain_h(x, y)
        o.append(L.sphere('h_pk', 0.1, 'h_pk', (x, y, z0 + 0.08), (1, 1, 0.8), None, 16, 8))
    return o


def map_city(rnd, br):
    _win_mat()
    C.mat('m_bld_a', base=L.hexlin('#8a8f96'), rough=0.8)
    C.mat('m_bld_b', base=L.hexlin('#9a6a5a'), rough=0.8)
    C.mat('m_bld_c', base=L.hexlin('#6a7a80'), rough=0.8)
    C.mat('m_roof', base=L.hexlin('#3c4046'), rough=0.9)
    C.mat('m_car', base=L.hexlin('#b0503a'), rough=0.5)
    C.mat('m_teal', base=L.hexlin('#3aa0a0'), rough=0.5)
    C.mat('m_rust', base=L.hexlin('#8a4a2a'), rough=0.8)
    C.mat('m_deadtree', base=L.hexlin('#4a3a30'), rough=0.9)
    C.mat('m_clock', build=L.emit_build('#f4f0c8', 1.6))
    C.mat('m_hand', base=L.hexlin('#20242a'), rough=0.6)
    C.mat('m_zglow', build=L.emit_build('#8aff6a', 2.2))
    o = []
    # the landmark by the lair: the town hall's clock tower, stopped at midnight
    cx, cy = CLOCK_C
    z0 = terrain_h(cx, cy)
    o.append(C.box('hall', cx - 0.75, cy - 0.35, z0 - 0.1, cx + 0.75, cy + 0.45, z0 + 0.75, 'm_bld_b'))
    o.append(C.box('hall_roof', cx - 0.8, cy - 0.4, z0 + 0.75, cx + 0.8, cy + 0.5, z0 + 0.83, 'm_roof'))
    o.append(C.box('tower', cx - 0.3, cy - 0.28, z0 + 0.7, cx + 0.3, cy + 0.32, z0 + 1.9, 'm_bld_a'))
    o.append(L.cone('tower_roof', 0.5, 0.0, 0.7, 'm_roof', (cx, cy + 0.02, z0 + 2.25), (0, 0, math.pi / 4), None, 4))
    o.append(L.cylinder('clock', 0.2, 0.03, 'm_clock', (cx, cy - 0.29, z0 + 1.55), (math.pi / 2, 0, 0), None, 24))
    o.append(C.box('hand_h', cx - 0.012, cy - 0.32, z0 + 1.55, cx + 0.012, cy - 0.30, z0 + 1.7, 'm_hand'))
    o.append(C.box('hand_m', cx - 0.01, cy - 0.33, z0 + 1.55, cx + 0.01, cy - 0.31, z0 + 1.73, 'm_hand'))
    for dx in (-0.5, 0.5):
        o.append(C.box('hall_win', cx + dx - 0.1, cy - 0.37, z0 + 0.3, cx + dx + 0.1, cy - 0.34, z0 + 0.55, 'm_zglow'))
    o.append(C.box('hall_door', cx - 0.14, cy - 0.37, z0, cx + 0.14, cy - 0.34, z0 + 0.4, 'm_win_v'))
    # the blocks of the ruined town
    y00 = TIER_Y[0] + 0.2
    for gx in range(4):
        for gy in range(9):
            x = 0.3 + 1.6 * gx + 0.8
            y = y00 + 1.5 * gy + 0.75
            if zone_of(x, y) != 'city' or land_mask(x, y) < 1:
                continue
            w, d = rnd.uniform(0.65, 1.0), rnd.uniform(0.55, 0.9)
            h = rnd.uniform(0.5, 1.3)
            if not clear(x, y, max(w, d) / 2 + 0.05, h):
                h = 0.0
                if not clear(x, y, 0.3):
                    continue
            if rnd.random() < 0.12:
                continue
            z0 = terrain_h(x, y)
            key = rnd.choice(['m_bld_a', 'm_bld_b', 'm_bld_c'])
            fate = rnd.random()
            if h == 0.0 or fate < 0.14:
                # a collapsed lot: only rubble left
                for k in range(6):
                    rx, ry = x + rnd.uniform(-0.3, 0.3), y + rnd.uniform(-0.3, 0.3)
                    if not clear(rx, ry, 0.1):
                        continue
                    rr = rnd.uniform(0.06, 0.13)
                    ch = L.centered(C.box('rubble', rx - rr, ry - rr, z0, rx + rr, ry + rr, z0 + rr * 1.2, key))
                    ch.rotation_euler = (rnd.uniform(-0.5, 0.5), rnd.uniform(-0.5, 0.5), rnd.uniform(0, 1.5))
                    o.append(ch)
                continue
            _KEEP.append((x, y, max(w, d) / 2))
            b = L.centered(C.box('bld', x - w / 2, y - d / 2, z0 - 0.1, x + w / 2, y + d / 2, z0 + h, key))
            if rnd.random() < 0.3:
                b.rotation_euler = (rnd.uniform(-0.08, 0.08), rnd.uniform(-0.1, 0.1), 0)
            o.append(b)
            if fate < 0.45:
                tp = L.centered(C.box('broken', x - w * 0.4, y - d * 0.35, z0 + h, x + w * 0.25, y + d * 0.35, z0 + h + 0.22, key))
                tp.rotation_euler = (rnd.uniform(-0.35, 0.35), rnd.uniform(-0.45, 0.45), rnd.uniform(-0.3, 0.3))
                o.append(tp)
            else:
                o.append(C.box('roof', x - w / 2 - 0.03, y - d / 2 - 0.03, z0 + h, x + w / 2 + 0.03, y + d / 2 + 0.03, z0 + h + 0.06, 'm_roof'))
            for k in range(int(h / 0.3)):
                if rnd.random() < 0.45:
                    wx = x + rnd.uniform(-w / 2 + 0.12, w / 2 - 0.12)
                    wz = z0 + 0.2 + k * 0.3
                    o.append(C.box('win', wx - 0.06, y - d / 2 - 0.02, wz, wx + 0.06, y - d / 2 + 0.01, wz + 0.12, 'm_win'))
    # a junkyard of crushed cars by the west shore
    jy = ty(11.0)
    jx = coast(jy)[0] + 0.75
    for k in range(8):
        rx, ry = jx + rnd.uniform(-0.4, 0.4), jy + rnd.uniform(-0.5, 0.5)
        if not clear(rx, ry, 0.15):
            continue
        zz = terrain_h(rx, ry) + 0.1 * (k % 3)
        c = L.centered(L.rbox('junk', rx - 0.2, ry - 0.1, zz, rx + 0.2, ry + 0.1, zz + 0.13, rnd.choice(['m_rust', 'm_car', 'm_teal']), 0.03))
        c.rotation_euler = (rnd.uniform(-0.4, 0.4), rnd.uniform(-0.4, 0.4), rnd.uniform(0, 3))
        o.append(c)
    # street lamps along the branch, dead trees and wrecked cars
    for (x, y) in along(br, 6, 0.42):
        z0 = terrain_h(x, y)
        o.append(C.box('lamp_post', x - 0.025, y - 0.025, z0, x + 0.025, y + 0.025, z0 + 0.5, 'm_roof'))
        o.append(L.sphere('lamp', 0.06, 'm_win', (x, y, z0 + 0.53), seg=12, rings=6))

    def dtree(x, y, z):
        return [L.cylinder('dtree', 0.04, 0.6, 'm_deadtree', (x, y, z + 0.3), (rnd.uniform(-0.2, 0.2), rnd.uniform(-0.2, 0.2), 0), None, 8),
                L.cylinder('dbranch', 0.02, 0.3, 'm_deadtree', (x + 0.08, y, z + 0.5), (0, 0.8, 0), None, 6)]

    def car(x, y, z):
        c = L.centered(L.rbox('car', x - 0.28, y - 0.14, z, x + 0.28, y + 0.14, z + 0.19, rnd.choice(['m_car', 'm_teal']), 0.04))
        c.rotation_euler = (0, 0, rnd.uniform(-0.6, 0.6))
        return [c]
    o += scatter(rnd, 'city', 40, 0.15, 0.6, dtree)
    o += scatter(rnd, 'city', 40, 0.3, 0.2, car)
    return o


def map_dino(rnd, br):
    """Lost Valley: red-brown soil and jungle, a smoking volcano with a lava
    river running to the sea, ferns, cycads, a fossil ribcage and a tar pit."""
    C.mat('m_basalt', build=L.gloss_build('#4e3e40', rough=0.8, coat=0.0, spec=0.3))
    C.mat('m_basalt_dk', base=L.hexlin('#3a2c2e'), rough=0.9)
    C.mat('m_lava', build=L.emit_build('#ff7a1a', 3.2))
    C.mat('m_lava_hot', build=L.emit_build('#ffd060', 4.0))
    C.mat('m_vsmoke', build=L.toon_build('#e4dad6', '#8e7a86', emit=0.45))
    C.mat('m_fern', base=L.hexlin('#2aa88a'), rough=0.7)
    C.mat('m_fern2', base=L.hexlin('#3e9a3a'), rough=0.7)
    C.mat('m_jungle', base=L.hexlin('#2f8a3a'), rough=0.8)
    C.mat('m_jungle2', base=L.hexlin('#4aa83a'), rough=0.8)
    C.mat('m_dtrunk', base=L.hexlin('#6a4a2e'), rough=0.9)
    C.mat('m_bone', build=L.gloss_build('#f0e8d2', rough=0.6, coat=0.1, spec=0.3))
    C.mat('m_tar', build=L.gloss_build('#18121a', rough=0.08, coat=1.0, spec=0.8))
    C.mat('m_egg', build=L.gloss_build('#f4eede', rough=0.5, coat=0.2))
    C.mat('m_nest', base=L.hexlin('#8a6a3a'), rough=0.9)
    o, lights = [], []
    vx, vy = VOLCANO_C
    vz = terrain_h(vx, vy) - 0.05
    o.append(L.lathe('volcano', VOLC_PROFILE + [(0.3, 1.72), (0.0, 1.66)], 'm_basalt', 40, None, (vx, vy, vz)))
    # the crater's lava and a glow
    o.append(L.cylinder('crater', 0.36, 0.04, 'm_lava_hot', (vx, vy, vz + 1.75), (0, 0, 0), None, 24))
    lights.append(C.add_point_light((vx, vy - 0.2, vz + 2.3), (1.0, 0.5, 0.15), 60.0, 0.3, 'volc_glow'))
    # lava down the south-east flank, then a river to the east sea
    rim = (vx + 0.33, vy - 0.28)
    flank = [(rim[0] + (0.95 - 0.33) * t * 1.0 + 0.1 * math.sin(t * 5), rim[1] - 1.1 * t) for t in [i / 16 for i in range(17)]]
    river = catmull([flank[-1], (vx + 1.05, vy - 2.2), (vx + 1.3, vy - 3.1), (MX + 0.3, vy - 3.9)], 0.08)
    o.append(ribbon('lava_flank', flank, lambda t: 0.16 + 0.14 * t, 'm_lava', 0.02, hf=ground_z))
    o.append(ribbon('lava_river_e', river, 0.5, 'm_basalt_dk', 0.02, hf=ground_z))
    o.append(ribbon('lava_river', river, 0.34, 'm_lava', 0.035, hf=ground_z))
    for (x, y) in river[::2]:
        _PATHS.append((x, y, 0.3))
    lights.append(C.add_point_light((vx + 1.1, vy - 2.6, 0.9), (1.0, 0.45, 0.12), 25.0, 0.3, 'lava_glow'))
    # a steam puff where it meets the sea, smoke over the crater
    sx_, sy_ = river[-1][0] - 0.6, river[-1][1] + 0.1
    o.append(L.sphere('steam', 0.22, 'm_vsmoke', (sx_, sy_, 0.35), (1, 1, 0.8), None, 16, 8))
    for k, (dx, dy, dz, r) in enumerate(((0.0, 0.0, 2.05, 0.26), (0.18, 0.12, 2.45, 0.32), (0.42, 0.28, 2.85, 0.36),
                                         (0.78, 0.4, 3.15, 0.3))):
        o.append(L.sphere('vsmoke', r, 'm_vsmoke', (vx + dx, vy + dy, vz + dz), (1, 1, 0.85), None, 20, 10))
    # the tar pit, glossy black, a couple of bubbles
    def tar(x, y, z):
        t = L.cylinder('tar', 0.42, 0.04, 'm_tar', (x, y, z + 0.01), (0, 0, 0), None, 28)
        t.scale = (1.25, 0.85, 1)
        out = [t]
        for k in range(3):
            out.append(L.sphere('tarb', rnd.uniform(0.04, 0.07), 'm_tar', (x + rnd.uniform(-0.3, 0.3), y + rnd.uniform(-0.2, 0.2), z + 0.03),
                                (1, 1, 0.6), None, 12, 6))
        return out
    o += scatter(rnd, 'dino', 30, 0.55, 0.0, tar, keep=1.0, limit=2)
    # the fossil ribcage and skull
    def ribs(x, y, z):
        out = [L.cylinder('spine', 0.045, 1.25, 'm_bone', (x, y, z + 0.08), (0, math.pi / 2, 0), None, 10)]
        for i in range(5):
            xx = x - 0.46 + 0.23 * i
            rr = 0.36 - 0.04 * abs(i - 1.5)
            t = L.torus('rib', rr, 0.035, 'm_bone', (xx, y, z + 0.05), (0, math.pi / 2, 0), None, (24, 6))
            for v in t.data.vertices:
                if v.co.y < -0.02:
                    v.co.y = -0.02
            out.append(t)
        out.append(L.sphere('fskull', 0.2, 'm_bone', (x + 0.8, y - 0.05, z + 0.12), (1.4, 0.9, 0.7), None, 16, 8))
        return out
    o += scatter(rnd, 'dino', 40, 0.75, 0.4, ribs, keep=1.0, limit=2)
    # the nest with eggs near the lair
    def nest(x, y, z):
        out = [L.torus('nest', 0.2, 0.07, 'm_nest', (x, y, z + 0.05), (0, 0, 0), None, (20, 8))]
        for k in range(3):
            aa = k * 2.1
            out.append(L.sphere('egg', 0.08, 'm_egg', (x + 0.07 * math.cos(aa), y + 0.07 * math.sin(aa), z + 0.12), (1, 1, 1.3), None, 12, 6))
        return out
    o += scatter(rnd, 'dino', 30, 0.3, 0.2, nest, limit=1)
    # cycads and jungle trees, ferns everywhere
    def cycad(x, y, z):
        s = rnd.uniform(0.85, 1.2)
        out = [L.cylinder('cy_t', 0.06 * s, 0.6 * s, 'm_dtrunk', (x, y, z + 0.3 * s), (0, 0, 0), None, 10)]
        for k in range(7):
            u = k * 2 * math.pi / 7 + rnd.uniform(-0.2, 0.2)
            lf = L.sphere('cy_l', 0.3 * s, rnd.choice(['m_jungle', 'm_jungle2']), (x + 0.2 * s * math.cos(u), y + 0.2 * s * math.sin(u), z + 0.62 * s),
                          (1.0, 0.3, 0.12), None, 12, 6)
            lf.rotation_euler = (0, -0.35, u)
            out.append(lf)
        return out

    def jtree(x, y, z):
        s = rnd.uniform(0.9, 1.2)
        return [L.cylinder('jt_t', 0.06 * s, 0.5 * s, 'm_dtrunk', (x, y, z + 0.25 * s), (0, 0, 0), None, 10),
                L.sphere('jt_c', 0.36 * s, rnd.choice(['m_jungle', 'm_jungle2']), (x, y, z + 0.6 * s), (1, 1, 0.75), None, 16, 8),
                L.sphere('jt_c2', 0.24 * s, 'm_jungle2', (x + 0.16 * s, y - 0.1 * s, z + 0.75 * s), (1, 1, 0.8), None, 12, 6)]

    def fern(x, y, z):
        s = rnd.uniform(0.7, 1.1)
        key = rnd.choice(['m_fern', 'm_fern2'])
        out = []
        for k in range(6):
            u = k * math.pi / 3 + rnd.uniform(-0.2, 0.2)
            lf = L.sphere('fern', 0.2 * s, key, (x + 0.12 * s * math.cos(u), y + 0.12 * s * math.sin(u), z + 0.1 * s), (1.0, 0.28, 0.1), None, 10, 5)
            lf.rotation_euler = (0, -0.6, u)
            out.append(lf)
        return out
    o += scatter(rnd, 'dino', 26, 0.36, 0.85, cycad)
    o += scatter(rnd, 'dino', 40, 0.38, 0.8, jtree)
    o += scatter(rnd, 'dino', 90, 0.2, 0.2, fern, keep=0.9)
    return o, lights


def map_forest(rnd, br):
    _win_mat()
    C.mat('m_pine', base=L.hexlin('#1f6a3a'), rough=0.8)
    C.mat('m_pine2', base=L.hexlin('#2e8a44'), rough=0.8)
    C.mat('m_oak', base=L.hexlin('#4aa03a'), rough=0.8)
    C.mat('m_trunk', base=L.hexlin('#5a3a22'), rough=0.9)
    C.mat('m_plank', base=L.hexlin('#a87a4a'), rough=0.8)
    C.mat('m_river', build=L.gloss_build('#1e7a8a', emit_col='#1e7a8a', emit=0.15, rough=0.1, coat=1.0))
    C.mat('m_millstone', base=L.hexlin('#8a8478'), rough=0.9)
    C.mat('m_millwood', base=L.hexlin('#7a5234'), rough=0.9)
    C.mat('m_millroof', base=L.hexlin('#4a3a5a'), rough=0.8)
    o = []
    # the stream: from the mill pond west to the sea, under a plank bridge
    ry = river_y
    x_end = coast(RIVER_Y)[0] - 0.8
    pts = [(4.85 - 0.1 * i, ry(4.85 - 0.1 * i)) for i in range(int((4.85 - x_end) / 0.1) + 1)]
    o.append(ribbon('river', pts, lambda t: 0.36 + 0.3 * min(1.0, t * 4), 'm_river', 0.05))
    for (x, y) in pts[::2]:
        _PATHS.append((x, y, 0.4))
    px_, py_ = 4.98, ry(4.98)
    o.append(L.cylinder('pond', 0.34, 0.05, 'm_river', (px_, py_, max(terrain_h(px_ + dx_, py_ + dy_) for dx_ in (-0.3, 0, 0.3) for dy_ in (-0.3, 0, 0.3)) + 0.01), (0, 0, 0), None, 28))
    # the bridge where the branch crosses the stream
    best = min(br, key=lambda p: abs(p[1] - ry(p[0])))
    i = br.index(best)
    a_, b_ = br[max(0, i - 3)], br[min(len(br) - 1, i + 3)]
    ang = math.atan2(b_[1] - a_[1], b_[0] - a_[0])
    bz = terrain_h(*best) + 0.06
    bd = C.box('bridge', -0.52, -0.25, -0.02, 0.52, 0.25, 0.03, 'm_plank')
    bd.location = (best[0], best[1], bz)
    bd.rotation_euler = (0, 0, ang)
    o.append(bd)
    for sg in (-1, 1):
        rl = C.box('rail', -0.5, sg * 0.25 - 0.025, 0.03, 0.5, sg * 0.25 + 0.025, 0.14, 'm_trunk')
        rl.location = (best[0], best[1], bz)
        rl.rotation_euler = (0, 0, ang)
        o.append(rl)
    # the old mill by the lair, its wheel in the stream
    mx_, my_ = MILL_C
    mz = terrain_h(mx_, my_)
    o.append(C.box('mill_base', mx_ - 0.42, my_ - 0.35, mz - 0.1, mx_ + 0.42, my_ + 0.4, mz + 0.45, 'm_millstone'))
    o.append(C.box('mill_top', mx_ - 0.4, my_ - 0.33, mz + 0.45, mx_ + 0.4, my_ + 0.38, mz + 0.85, 'm_millwood'))
    rf = L.cone('mill_roof', 0.62, 0.0, 0.5, 'm_millroof', (mx_, my_ + 0.02, mz + 1.1), (0, 0, math.pi / 4), None, 4)
    o.append(rf)
    o.append(C.box('mill_win', mx_ - 0.1, my_ - 0.36, mz + 0.55, mx_ + 0.1, my_ - 0.33, mz + 0.75, 'm_win'))
    wx, wy = mx_ + 0.12, ry(mx_ + 0.12) + 0.08
    o.append(L.cylinder('wheel', 0.4, 0.1, 'm_millwood', (wx, wy, mz + 0.18), (math.pi / 2, 0, 0), None, 20))
    o.append(L.cylinder('wheel_hub', 0.08, 0.14, 'm_trunk', (wx, wy - 0.02, mz + 0.18), (math.pi / 2, 0, 0), None, 12))
    for k in range(8):
        t = k * math.pi / 4
        pd = C.box('paddle', -0.05, -0.07, -0.04, 0.05, 0.07, 0.04, 'm_trunk')
        pd.location = (wx + 0.42 * math.cos(t), wy - 0.02, mz + 0.18 + 0.42 * math.sin(t))
        pd.rotation_euler = (0, -t, 0)
        o.append(pd)
    # the woods
    def tree(x, y, zz):
        out = []
        if rnd.random() < 0.7:
            s = rnd.uniform(0.8, 1.2)
            for k in range(3):
                out.append(L.cone('pine', (0.34 - 0.08 * k) * s, 0.0, 0.42 * s, rnd.choice(['m_pine', 'm_pine2']),
                                  (x, y, zz + (0.3 + 0.22 * k) * s), (0, 0, 0), None, 12))
            out.append(C.box('pt', x - 0.03, y - 0.03, zz, x + 0.03, y + 0.03, zz + 0.2, 'm_trunk'))
        else:
            out.append(L.sphere('oak', 0.34, 'm_oak', (x, y, zz + 0.55), (1, 1, 0.85), None, 16, 8))
            out.append(C.box('ot', x - 0.04, y - 0.04, zz, x + 0.04, y + 0.04, zz + 0.3, 'm_trunk'))
        return out
    o += scatter(rnd, 'forest', 260, 0.3, 1.0, tree, keep=0.75)
    # lanterns along the branch
    for (x, y) in along(br, 6, 0.4):
        o.append(L.sphere('lantern', 0.05, 'm_win', (x, y, terrain_h(x, y) + 0.3), seg=10, rings=5))
    return o


def map_bay(rnd, br):
    """Abyss Bay: mossy quays round a cove of the east sea, a pier, a wreck
    half sunk in the cove, the red-and-white lighthouse on its cape, cyan and
    magenta glows in the water and a tentacle curling out of it."""
    C.mat('m_lh_red', build=L.gloss_build('#e0383a', rough=0.45, coat=0.3))
    C.mat('m_lh_white', build=L.gloss_build('#f2eee4', rough=0.5, coat=0.2))
    C.mat('m_lh_dark', base=L.hexlin('#232a36'), rough=0.5)
    C.mat('m_lh_lamp', build=L.emit_build('#fff0b0', 5.0))
    C.mat('m_brock', build=L.gloss_build('#7a869c', rough=0.7, coat=0.1, spec=0.3))
    C.mat('m_pier', base=L.hexlin('#8a6a4a'), rough=0.85)
    C.mat('m_pier_dk', base=L.hexlin('#4a3626'), rough=0.9)
    C.mat('m_hull', base=L.hexlin('#4a3428'), rough=0.8)
    C.mat('m_hull_lt', base=L.hexlin('#6a4c38'), rough=0.8)
    C.mat('m_sail', base=L.hexlin('#cfcab8'), rough=0.9)
    C.mat('m_cyan', build=L.emit_build('#4afff0', 3.0))
    C.mat('m_mag', build=L.emit_build('#ff5ae0', 3.0))
    C.mat('m_tent', build=L.gloss_build('#c83a9e', emit_col='#c83a9e', emit=0.35, rough=0.35, coat=0.5))
    C.mat('m_suck', build=L.gloss_build('#ffb0e0', emit_col='#ffb0e0', emit=0.3, rough=0.4, coat=0.3))
    C.mat('m_crate', base=L.hexlin('#a07a4a'), rough=0.85)
    C.mat('m_barrel', base=L.hexlin('#7a4a2e'), rough=0.8)
    C.mat('m_buoy', build=L.gloss_build('#d8363a', rough=0.4, coat=0.4))
    C.mat('m_cove', build=L.gloss_build('#12606e', emit_col='#0e5a6a', emit=0.25, rough=0.15, coat=1.0))
    _win_mat()
    o, lights = [], []
    # the lighthouse on its cape
    lx, ly = LIGHT_C
    lz = terrain_h(lx, ly)
    for k in range(6):
        aa = k * math.pi / 3 + 0.3
        o.append(L.sphere('lh_rock', rnd.uniform(0.22, 0.32), 'm_brock', (lx + 0.5 * math.cos(aa), ly + 0.45 * math.sin(aa), lz),
                          (1.2, 1.0, 0.6), None, 16, 8))
    o.append(L.cylinder('lh_base', 0.5, 0.2, 'm_brock', (lx, ly, lz + 0.05), (0, 0, 0), None, 24))
    zs = [0.1, 0.55, 1.0, 1.45, 1.9]
    for i in range(4):
        r1 = 0.36 - 0.035 * i
        r2 = r1 - 0.035
        o.append(L.cone('lh_band', r1, r2, zs[i + 1] - zs[i], 'm_lh_red' if i % 2 == 0 else 'm_lh_white',
                        (lx, ly, lz + (zs[i] + zs[i + 1]) / 2), (0, 0, 0), None, 28))
    o.append(L.cylinder('lh_gal', 0.3, 0.06, 'm_lh_dark', (lx, ly, lz + 1.93), (0, 0, 0), None, 28))
    o.append(L.torus('lh_rail', 0.29, 0.015, 'm_lh_dark', (lx, ly, lz + 2.06), (0, 0, 0), None, (28, 6)))
    o.append(L.cylinder('lh_lamp', 0.18, 0.26, 'm_lh_lamp', (lx, ly, lz + 2.09), (0, 0, 0), None, 24))
    o.append(L.cone('lh_dome', 0.23, 0.0, 0.24, 'm_lh_red', (lx, ly, lz + 2.34), (0, 0, 0), None, 24))
    o.append(C.box('lh_door', lx - 0.08, ly - 0.37, lz + 0.1, lx + 0.08, ly - 0.33, lz + 0.38, 'm_lh_dark'))
    lights.append(C.add_point_light((lx, ly - 0.1, lz + 2.1), (1.0, 0.9, 0.6), 40.0, 0.15, 'lh_glow'))
    # the pier into the cove, on stilts
    py0 = ty(24.35)
    x0 = coast(py0)[1] - 0.35
    x1 = min(MX + 0.1, x0 + 2.2)
    n = int((x1 - x0) / 0.16)
    for k in range(n):
        xx = x0 + k * 0.16
        o.append(C.box('plank', xx, py0 - 0.26, 0.2, xx + 0.13, py0 + 0.26, 0.25, 'm_pier'))
    for k in range(int((x1 - x0) / 0.55) + 1):
        xx = x0 + 0.1 + k * 0.55
        for sg in (-1, 1):
            o.append(L.cylinder('stilt', 0.04, 0.6, 'm_pier_dk', (xx, py0 + sg * 0.24, -0.05), (0, 0, 0), None, 8))
            o.append(L.cylinder('post', 0.035, 0.18, 'm_pier_dk', (xx, py0 + sg * 0.27, 0.32), (0, 0, 0), None, 8))
    _KEEP.append(((x0 + x1) / 2, py0, 0.5))
    # the wreck half sunk in the cove
    wx, wy = coast(ty(21.4))[1] + 0.8, ty(21.4)
    hull = L.sphere('hull', 0.5, 'm_hull', (wx, wy, -0.08), (1.9, 0.62, 0.6), None, 24, 12)
    hull.rotation_euler = (math.radians(12), math.radians(-8), math.radians(20))
    o.append(hull)
    o.append(L.sphere('deck', 0.46, 'm_hull_lt', (wx, wy, 0.05), (1.8, 0.5, 0.2), None, 20, 8))
    o[-1].rotation_euler = (math.radians(12), math.radians(-8), math.radians(20))
    o.append(L.cylinder('mast', 0.035, 1.2, 'm_pier_dk', (wx + 0.1, wy + 0.05, 0.55), (math.radians(18), math.radians(-14), 0), None, 8))
    sail = C.box('sail', -0.02, -0.28, -0.2, 0.02, 0.1, 0.25, 'm_sail')
    sail.location = (wx + 0.15, wy + 0.03, 0.62)
    sail.rotation_euler = (math.radians(18), math.radians(-14), 0.3)
    o.append(sail)
    for k in range(3):
        o.append(L.sphere('porthole', 0.045, 'm_cyan', (wx - 0.4 + 0.3 * k, wy - 0.3 + 0.1 * k, 0.03), (1, 0.4, 1), None, 10, 5))
    # a tentacle curling out of the water beside the wreck
    tnx, tny = wx - 0.25, wy + 1.1
    for i in range(18):
        t = i / 17
        if t < 0.6:
            u = t / 0.6
            x, z = tnx + 0.1 * math.sin(u * math.pi), -0.05 + 1.0 * u
        else:
            u = (t - 0.6) / 0.4
            aa = u * 1.4 * math.pi
            rr = 0.19 * (1 - 0.4 * u)
            x, z = tnx - rr + rr * math.cos(aa), 0.95 + rr * math.sin(aa)
        r = 0.13 * (1 - 0.7 * t)
        o.append(L.sphere('tent', r, 'm_tent', (x, tny, z), seg=14, rings=7))
        if 3 <= i <= 12 and i % 3 == 0:
            o.append(L.sphere('suck', r * 0.4, 'm_suck', (x - r * 0.8, tny - 0.03, z), (0.5, 1, 1), None, 8, 4))
    o.append(L.torus('ripple', 0.26, 0.025, 'm_cyan', (tnx, tny, 0.01), (0, 0, 0), None, (24, 6)))
    # glowing plankton in the cove and the open sea
    for k in range(40):
        y = rnd.uniform(ty(18.0), ty(29.5))
        x = rnd.uniform(coast(y)[1] + 0.25, MX + 0.2)
        o.append(L.sphere('glow', rnd.uniform(0.03, 0.06), rnd.choice(['m_cyan', 'm_cyan', 'm_mag']), (x, y, 0.01), (1, 1, 0.4), None, 8, 4))
    # a buoy in the cove
    bx_, by_ = coast(ty(23.2))[1] + 0.8, ty(23.2)
    o.append(L.cylinder('buoy', 0.16, 0.2, 'm_buoy', (bx_, by_, 0.05), (0, 0, 0), None, 16))
    o.append(L.cone('buoy_t', 0.1, 0.04, 0.25, 'm_lh_white', (bx_, by_, 0.27), (0, 0, 0), None, 12))
    o.append(L.sphere('buoy_l', 0.05, 'm_cyan', (bx_, by_, 0.43), seg=10, rings=5))
    # harbour lamps along the branch, crates and barrels on the quay, shore rocks with glow
    for (x, y) in along(br, 5, 0.42):
        z0 = terrain_h(x, y)
        o.append(C.box('hl_post', x - 0.025, y - 0.025, z0, x + 0.025, y + 0.025, z0 + 0.5, 'm_lh_dark'))
        o.append(L.sphere('hl_lamp', 0.06, 'm_win', (x, y, z0 + 0.53), seg=12, rings=6))

    def crates(x, y, z):
        out = []
        for k in range(rnd.choice([2, 3])):
            dx, dy = (0, 0) if k == 0 else (rnd.uniform(-0.2, 0.2), rnd.uniform(-0.2, 0.2))
            zz = z + (0.22 if k == 2 else 0.0)
            out.append(L.rbox('crate', x + dx - 0.12, y + dy - 0.12, zz, x + dx + 0.12, y + dy + 0.12, zz + 0.22, 'm_crate', 0.015))
        return out

    def barrels(x, y, z):
        return [L.cylinder('barrel', 0.1, 0.24, 'm_barrel', (x + 0.13 * k, y + 0.05 * (k % 2), z + 0.12), (0, 0, 0), None, 14)
                for k in range(rnd.choice([1, 2, 3]))]

    def rock(x, y, z):
        out = [L.sphere('srock', rnd.uniform(0.18, 0.28), 'm_brock', (x, y, z + 0.05), (1.2, 1.0, 0.7), None, 14, 7)]
        out.append(L.sphere('sglow', 0.035, rnd.choice(['m_cyan', 'm_mag']), (x + 0.08, y - 0.12, z + 0.2), seg=8, rings=4))
        return out
    def shed(x, y, z):
        out = [C.box('shed', x - 0.7, y - 0.38, z - 0.1, x + 0.7, y + 0.38, z + 0.55, 'm_hull_lt')]
        v = [(x - 0.78, y - 0.46, z + 0.55), (x + 0.78, y - 0.46, z + 0.55), (x + 0.78, y, z + 0.85), (x - 0.78, y, z + 0.85),
             (x - 0.78, y + 0.46, z + 0.55), (x + 0.78, y + 0.46, z + 0.55)]
        out.append(L.mesh_from('shed_roof', v, [(0, 1, 2, 3), (3, 2, 5, 4)], 'm_lh_red', sm=False))
        out.append(C.box('shed_door', x - 0.2, y - 0.41, z, x + 0.2, y - 0.37, z + 0.4, 'm_pier_dk'))
        out.append(C.box('shed_win', x + 0.35, y - 0.41, z + 0.25, x + 0.55, y - 0.37, z + 0.42, 'm_win'))
        return out
    o += scatter(rnd, 'bay', 60, 0.8, 0.9, shed, keep=1.0, limit=1)
    o += scatter(rnd, 'bay', 50, 0.3, 0.45, crates, limit=14)
    o += scatter(rnd, 'bay', 40, 0.25, 0.3, barrels, limit=8)
    o += scatter(rnd, 'bay', 60, 0.28, 0.3, rock, inland=0.6, limit=7)
    return o, lights


def map_castle(rnd):
    _win_mat()
    C.mat('m_stone', base=L.hexlin('#9a8ab8'), rough=0.8)
    C.mat('m_stone_dk', base=L.hexlin('#6e5e92'), rough=0.8)
    C.mat('m_roof_v', build=L.gloss_build('#5a2a7a', rough=0.5, coat=0.3))
    C.mat('m_roof_r', build=L.gloss_build('#a02a4a', rough=0.5, coat=0.3))
    C.mat('m_deadwood', base=L.hexlin('#4a3a5a'), rough=0.9)
    o = []
    cx, cy = CASTLE_C
    z0 = terrain_h(cx, cy)
    s = 0.9
    o.append(C.box('keep', cx - 0.7 * s, cy - 0.5 * s, z0 - 0.2, cx + 0.7 * s, cy + 0.5 * s, z0 + 1.4 * s, 'm_stone'))
    o.append(L.cone('keep_roof', 0.95 * s, 0.0, 0.95 * s, 'm_roof_v', (cx, cy, z0 + 1.4 * s + 0.47 * s), (0, 0, math.pi / 4), None, 4))
    for (dx, dy, hh) in ((-1.1, -0.8, 1.5), (1.1, -0.8, 1.5), (-1.1, 0.8, 1.8), (1.1, 0.8, 1.8), (0.0, 1.0, 2.3)):
        x, y = cx + dx * s, cy + dy * s
        zz = terrain_h(x, y)
        hh *= s
        o.append(L.cylinder('tower', 0.27, hh + 0.3, 'm_stone', (x, y, zz + hh / 2 - 0.15), (0, 0, 0), None, 20))
        o.append(L.cone('tower_roof', 0.35, 0.0, 0.7, 'm_roof_r' if dy < 0 else 'm_roof_v', (x, y, zz + hh + 0.35), (0, 0, 0), None, 20))
        o.append(C.box('twin', x - 0.06, y - 0.28, zz + hh - 0.5, x + 0.06, y - 0.24, zz + hh - 0.3, 'm_win'))
    x0, x1, y = cx - 1.1 * s, cx + 1.1 * s, cy - 0.8 * s
    zz = terrain_h(cx, y)
    o.append(C.box('wall', x0, y - 0.12, zz - 0.3, x1, y + 0.12, zz + 0.7, 'm_stone_dk'))
    for k in range(7):
        mx = x0 + 0.15 + k * (x1 - x0 - 0.3) / 6
        o.append(C.box('merlon', mx - 0.07, y - 0.12, zz + 0.7, mx + 0.07, y + 0.12, zz + 0.84, 'm_stone_dk'))
    o.append(C.box('gate', cx - 0.17, y - 0.14, zz - 0.1, cx + 0.17, y - 0.11, zz + 0.42, 'm_win_v'))

    def vtree(x, y, zz):
        return [L.cone('vtree', 0.22, 0.0, 0.7, 'm_roof_v', (x, y, zz + 0.4), (0, 0, 0), None, 12),
                C.box('vtrunk', x - 0.03, y - 0.03, zz, x + 0.03, y + 0.03, zz + 0.2, 'm_deadwood')]
    o += scatter(rnd, 'castle', 60, 0.25, 0.75, vtree, limit=18)
    return o


def map_desert(rnd):
    C.mat('m_sandstone', base=L.hexlin('#e0b070'), rough=0.8)
    C.mat('m_sandstone_dk', base=L.hexlin('#b88448'), rough=0.8)
    C.mat('m_cactus', base=L.hexlin('#4a9a4a'), rough=0.6)
    C.mat('m_water', build=L.gloss_build('#2ac0c0', emit_col='#2ac0c0', emit=0.25, rough=0.1, coat=1.0))
    C.mat('m_palm', base=L.hexlin('#3a8a3a'), rough=0.6)
    C.mat('m_gold', build=L.gold_build(emit=0.6, rim=0.5))
    o = []
    px_, py_ = PYRAMID_C
    z0 = terrain_h(px_, py_)
    o.append(L.cone('pyramid', 1.75, 0.0, 2.0, 'm_sandstone', (px_, py_, z0 + 0.9), (0, 0, math.pi / 4), None, 4))
    o.append(L.cone('pyr_cap', 0.23, 0.0, 0.26, 'm_gold', (px_, py_, z0 + 1.77), (0, 0, math.pi / 4), None, 4))
    o.append(C.box('pyr_door', px_ - 0.16, py_ - 1.26, z0 - 0.1, px_ + 0.16, py_ - 1.2, z0 + 0.36, 'm_sandstone_dk'))
    for (x, y, s) in ((7.35, ty(42.2), 0.3),):
        if land_mask(x, y) < 1:
            continue
        o.append(L.cone('pyr_s', 0.9 * s * 2, 0.0, 1.1 * s * 2, 'm_sandstone_dk', (x, y, terrain_h(x, y) + 0.5 * s * 2), (0, 0, math.pi / 4), None, 4))
        _KEEP.append((x, y, 0.8 * s * 2))
    ox, oy = OASIS_C
    o.append(L.cylinder('oasis', 0.6, 0.06, 'm_water', (ox, oy, max(terrain_h(ox + dx_, oy + dy_) for dx_ in (-0.6, 0, 0.6) for dy_ in (-0.6, 0, 0.6)) + 0.02),
                        (0, 0, 0), None, 32))
    for i in range(3):
        t = i * 2.1 + 0.4
        x, y = ox + 0.8 * math.cos(t), oy + 0.62 * math.sin(t)
        zz = terrain_h(x, y)
        o.append(L.cylinder('palm_t', 0.035, 0.7, 'm_sandstone_dk', (x, y, zz + 0.35), (0.15, 0.1, 0), None, 8))
        for k in range(5):
            u = k * 2 * math.pi / 5
            lf = L.sphere('palm_l', 0.2, 'm_palm', (x + 0.15 * math.cos(u), y + 0.15 * math.sin(u), zz + 0.72), (1.0, 0.35, 0.12), None, 12, 6)
            lf.rotation_euler = (0, 0.3, u)
            o.append(lf)

    def cactus(x, y, zz):
        return [L.cylinder('cactus', 0.07, 0.4, 'm_cactus', (x, y, zz + 0.2), (0, 0, 0), None, 10),
                L.cylinder('cactus_arm', 0.05, 0.18, 'm_cactus', (x + 0.09, y, zz + 0.25), (0, 0.0, 0), None, 8)]
    o += scatter(rnd, 'desert', 60, 0.2, 0.4, cactus, limit=18)
    return o


def map_future(rnd):
    C.mat('m_swampw', build=L.gloss_build('#2a4a3a', rough=0.15, coat=1.0))
    C.mat('m_hut', base=L.hexlin('#5a4430'), rough=0.9)
    C.mat('m_hutroof', base=L.hexlin('#3a2a4a'), rough=0.8)
    C.mat('m_witchwin', build=L.emit_build('#9aff5a', 2.5))
    C.mat('m_tomb', base=L.hexlin('#c4cad2'), rough=0.8)
    C.mat('m_dead', base=L.hexlin('#3a3030'), rough=0.9)
    o = []
    # the swamp: pools, dead trees and the witch's hut
    for i in range(6):
        x, y = rnd.uniform(0.8, 5.5), rnd.uniform(ty(43.8), ty(49.5))
        if math.hypot(x - SWAMP_PAD[0], y - SWAMP_PAD[1]) < 1.0:
            continue
        o.append(L.cylinder('pool', rnd.uniform(0.35, 0.7), 0.04, 'm_swampw', (x, y, terrain_h(x, y) + 0.01), (0, 0, 0), None, 20))
    hx, hy = HUT_C
    hz = terrain_h(hx, hy)
    hut = L.centered(C.box('hut', hx - 0.45, hy - 0.4, hz - 0.1, hx + 0.45, hy + 0.4, hz + 1.25, 'm_hut'))
    hut.rotation_euler = (0, 0.08, 0.1)
    o.append(hut)
    o.append(L.cylinder('hut_brim', 0.78, 0.06, 'm_hutroof', (hx, hy, hz + 1.28), (0.08, -0.1, 0), None, 24))
    o.append(L.cone('hut_roof', 0.55, 0.18, 0.9, 'm_hutroof', (hx, hy, hz + 1.72), (0.10, -0.14, 0), None, 16))
    o.append(L.cone('hut_tip', 0.18, 0.0, 0.5, 'm_hutroof', (hx + 0.2, hy, hz + 2.25), (0.0, 0.9, 0), None, 12))
    o.append(C.box('hut_win', hx - 0.14, hy - 0.43, hz + 0.75, hx + 0.14, hy - 0.40, hz + 1.05, 'm_witchwin'))
    o.append(L.sphere('hut_glow', 0.09, 'm_witchwin', (hx + 0.55, hy - 0.2, hz + 1.25), seg=10, rings=5))
    for i in range(4):
        x, y = rnd.uniform(0.8, 5.5), rnd.uniform(ty(43.5), ty(49.5))
        zz = terrain_h(x, y)
        o.append(L.cylinder('dtree', 0.05, 0.9, 'm_dead', (x, y, zz + 0.45), (rnd.uniform(-0.2, 0.2), rnd.uniform(-0.2, 0.2), 0), None, 8))
    # the graveyard hill
    gx, gy = GRAVE_C
    for i in range(9):
        x, y = gx + rnd.uniform(-0.9, 0.9), gy + rnd.uniform(-0.5, 0.8)
        zz = terrain_h(x, y)
        tb = L.centered(L.rbox('tomb', x - 0.16, y - 0.06, zz - 0.05, x + 0.16, y + 0.06, zz + 0.42, 'm_tomb', 0.06))
        tb.rotation_euler = (rnd.uniform(-0.15, 0.15), rnd.uniform(-0.15, 0.15), 0)
        o.append(tb)
    o.append(L.cylinder('gtree', 0.06, 1.2, 'm_dead', (gx + 0.3, gy + 0.6, terrain_h(gx + 0.3, gy + 0.6) + 0.6), (0, 0.2, 0), None, 8))
    return o


def map_fog(rnd):
    """The two future regions under a thick magic fog: a blanket of cartoon
    cloud puffs (the game's smoke) whose top sits at ~1.2 m, so the hut's
    crooked roof and the top of the graveyard hill poke out of it, a violet
    haze along its southern edge and a few glowing motes above. It starts at
    the tier-3 border: the castle and the desert stay clear."""
    C.mat('m_cloud', build=L.toon_build('#efe6ff', '#9a82e0', emit=0.45, rough=0.9))
    C.mat('m_cloud2', build=L.toon_build('#e2ecff', '#7e9ae6', emit=0.45, rough=0.9))
    C.mat('m_mote', build=L.emit_build('#f0d8ff', 3.0))
    o = []
    y = FOG_Y - 0.4
    row = 0
    while y < MAP_DEPTH + 1.5:
        x = -0.6 + (0.3 if row % 2 else 0.0)
        while x < MX + 0.6:
            yy = y + rnd.uniform(-0.2, 0.2)
            xx = x + rnd.uniform(-0.2, 0.2)
            edge = max(0.0, min(1.0, (yy - border(3, xx) - 0.1) / 1.6))
            if edge <= 0.02 or rnd.random() > 0.35 + 0.65 * edge:
                x += 0.62
                continue
            r = (0.30 + 0.45 * edge) * rnd.uniform(0.8, 1.2)
            top = 1.15 + 0.25 * _n(xx, yy, 0.7, 31.0)
            th = terrain_h(xx, yy)
            near = min(math.hypot(xx - SWAMP_PAD[0], yy - SWAMP_PAD[1]), math.hypot(xx - GRAVE_PAD[0], yy - GRAVE_PAD[1]))
            if th > top - 0.35 or near < 0.55 + r:      # hill tops and the locked spots stay out
                x += 0.62
                continue
            zc = max(th + 0.2, top - r)
            key = 'm_cloud' if _n(xx, yy, 0.4, 33.0) > -0.1 else 'm_cloud2'
            o.append(L.sphere('cloud', r, key, (xx, yy, zc), (1.0, 0.85, 0.75), None, 20, 10))
            x += 0.62
        y += 0.52
        row += 1
    for i in range(18):
        x, yy = rnd.uniform(0.5, MX - 0.5), rnd.uniform(FOG_Y + 1.0, MAP_DEPTH)
        o.append(L.sphere('mote', rnd.uniform(0.03, 0.06), 'm_mote', (x, yy, 1.5 + rnd.uniform(0, 0.6)), seg=8, rings=4))

    def hb(nt, neutral):
        N, Lk = nt.nodes, nt.links
        out = nt.nodes['out']
        vol = N.new('ShaderNodeVolumePrincipled')
        vol.inputs['Color'].default_value = L.hexlin('#e0d0ff') + (1,)
        vol.inputs['Emission Color'].default_value = L.hexlin('#8a60ff') + (1,)
        geo = N.new('ShaderNodeNewGeometry')
        sep = N.new('ShaderNodeSeparateXYZ')
        Lk.new(geo.outputs['Position'], sep.inputs[0])
        edge = N.new('ShaderNodeMapRange')
        edge.inputs['From Min'].default_value = FOG_Y - 0.6
        edge.inputs['From Max'].default_value = FOG_Y + 1.6
        Lk.new(sep.outputs['Y'], edge.inputs['Value'])
        den = N.new('ShaderNodeMath')
        den.operation = 'MULTIPLY'
        den.inputs[1].default_value = 0.35
        Lk.new(edge.outputs['Result'], den.inputs[0])
        Lk.new(den.outputs[0], vol.inputs['Density'])
        em = N.new('ShaderNodeMath')
        em.operation = 'MULTIPLY'
        em.inputs[1].default_value = 0.25
        Lk.new(edge.outputs['Result'], em.inputs[0])
        Lk.new(em.outputs[0], vol.inputs['Emission Strength'])
        Lk.new(vol.outputs['Volume'], out.inputs['Volume'])
        return N.new('ShaderNodeBsdfTransparent').outputs['BSDF']
    C.mat('m_haze', build=hb)
    o.append(C.box('haze', -1.0, FOG_Y - 0.8, -0.4, MX + 1.0, MAP_DEPTH + 3.0, 1.0, 'm_haze'))
    return o


# ---------------------------------------------------------------------------
# the house for the hub screen (368 x 200)
# ---------------------------------------------------------------------------

HOUSE_W, HOUSE_H = 368, 200


def sky_build(stops, z0, z1):
    st = [(p, L.hexlin(c)) for p, c in stops]

    def b(nt, neutral):
        N, Lk = nt.nodes, nt.links
        geo = N.new('ShaderNodeNewGeometry')
        sep = N.new('ShaderNodeSeparateXYZ')
        Lk.new(geo.outputs['Position'], sep.inputs[0])
        mr = N.new('ShaderNodeMapRange')
        mr.inputs['From Min'].default_value = z0
        mr.inputs['From Max'].default_value = z1
        Lk.new(sep.outputs['Z'], mr.inputs['Value'])
        rp = L._ramp(nt, st)
        Lk.new(mr.outputs['Result'], rp.inputs['Fac'])
        e = N.new('ShaderNodeEmission')
        Lk.new(rp.outputs['Color'], e.inputs['Color'])
        return e.outputs['Emission']
    return b


def g_house():
    if not want('house'):
        return
    rnd = random.Random(31)
    house_mats()
    _bat_mats()
    C.mat('hb_sky', build=sky_build([(0.0, '#ffb070'), (0.18, '#f07a6a'), (0.45, '#8a4ab0'), (1.0, '#1c1448')], 0.0, 6.0))
    C.mat('hb_moon', build=L.emit_build('#fff4c8', 1.6))
    C.mat('hb_star', build=L.emit_build('#ffffff', 1.5))
    C.mat('hb_hill', base=L.hexlin('#3a2a5a'), rough=0.9)
    C.mat('hb_hill2', base=L.hexlin('#2a2046'), rough=0.9)
    C.mat('hb_lawn', build=L.gloss_build('#5aa844', rough=0.9, coat=0.0, spec=0.2))
    C.mat('hb_stone', base=L.hexlin('#c8bca8'), rough=0.8)
    C.mat('hb_bush', base=L.hexlin('#3a8a3a'), rough=0.8)
    C.mat('hb_lamp', build=L.emit_build('#ffd070', 3.0))
    o = build_house(0.0, 0.0, 0.0, 1.0, detail=True)
    o.append(L.cylinder('hb_lawn', 6.0, 0.1, 'hb_lawn', (0, 0, -0.05), (0, 0, 0), None, 64))
    for i in range(5):
        y = -0.62 - 0.22 * i
        st = L.cylinder('hb_step', 0.1, 0.02, 'hb_stone', (0.02 * (i % 2), y, 0.005), (0, 0, 0), None, 16)
        st.scale = (1.3, 0.8, 1)
        o.append(st)
    for (x, y, r) in ((-1.05, -0.35, 0.16), (0.8, -0.35, 0.18), (1.0, -0.1, 0.14), (-0.75, -0.42, 0.12)):
        o.append(L.sphere('hb_bush', r, 'hb_bush', (x, y, r * 0.7), (1, 1, 0.8), None, 16, 8))
    # a garden lamp by the path
    o.append(L.cylinder('hb_lpost', 0.015, 0.4, 'h_trunk', (0.34, -1.1, 0.2), (0, 0, 0), None, 8))
    o.append(L.sphere('hb_lbulb', 0.045, 'hb_lamp', (0.34, -1.1, 0.42), seg=12, rings=6))
    # backdrop: the evening sky, hills, the moon, a few stars
    o.append(C.box('hb_sky', -40, 7.0, -3.0, 40, 7.2, 24.0, 'hb_sky'))
    o.append(L.cylinder('hb_moon', 0.45, 0.05, 'hb_moon', (0.9, 6.8, 2.25), (math.pi / 2, 0, 0), None, 48))
    for i in range(24):
        o.append(L.sphere('hb_star', rnd.uniform(0.02, 0.04), 'hb_star', (rnd.uniform(-7, 7), 6.9, rnd.uniform(2.2, 5.5)), seg=6, rings=3))
    for (x, r, key) in ((-4.0, 2.4, 'hb_hill2'), (3.5, 2.8, 'hb_hill2'), (-1.0, 2.0, 'hb_hill'), (5.5, 1.8, 'hb_hill')):
        o.append(L.sphere('hb_hill', r, key, (x, 5.5, -r * 0.55), (1.4, 0.3, 0.6), None, 32, 12))
    extra = []
    for (x, z, sc) in ((-1.5, 1.35, 0.75), (-2.1, 1.6, 0.55)):
        br = L.empty('hb_bat', (x, 1.5, z))
        br.scale = (sc, sc, sc)
        bo, be = L.build_bat(br, s=1.0, flap=math.radians(25))
        o += bo
        extra += [br] + be
    L.update()
    camera((0.1, 0.0, 0.62), (-0.30, 1.0, -0.22), lens=30.0, dist=4.2)
    C._STATE['cam'].data.clip_end = 200.0
    # an evening light: the map's sun, lower and warmer, for this picture only
    sun = C._STATE['sun']
    keep = (tuple(sun.data.color), sun.data.energy)
    sun.data.color = (1.0, 0.78, 0.62)
    sun.data.energy = 1.6
    porch = C.add_point_light((0.0, -0.75, 0.62), (1.0, 0.72, 0.4), 18.0, 0.1, 'hb_porch')
    garden = C.add_point_light((0.34, -1.12, 0.45), (1.0, 0.8, 0.45), 6.0, 0.05, 'hb_garden')
    L.frame(HOUSE_W, HOUSE_H)
    door = px_of((0, -0.46, 0.25), HOUSE_W, HOUSE_H)
    ui_render('house', HOUSE_W, HOUSE_H, o, a.samples,
              extra={'what': "Tommy's house, the hub screen backdrop", 'door_px': door})
    sun.data.color, sun.data.energy = keep
    remove(o + extra + [porch, garden])


# ---------------------------------------------------------------------------

g_logo()
g_emblems()
g_house()
g_map()
C.save_meta(OUT)
print('[ui] done: %d pictures in %.1fs' % (len(META), time.time() - T0))
