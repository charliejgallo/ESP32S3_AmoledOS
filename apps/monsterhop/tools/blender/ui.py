"""Monster Hop - UI art (SPEC section 9) -> assets/ui/.

    Blender -b -P ui.py -- --out ../../assets/ui [--sample] [--only logo,map,emblem_city] [--samples 64]

Not composited with the game, so each picture has its own camera (set here,
not by mh_common.place_camera) under the `map` light; everything else (the
material registry, the EXR -> PNG path, meta.json) is mh_common's.

    logo           340 x 130, transparent: MONSTER HOP in chunky extruded letters
    map            368 x 900, opaque: the world map, zone 1 at the bottom; meta
                   carries the pixel centres of the 16 level pads + 2 locked spots
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


EMBLEMS = {'city': emblem_city, 'castle': emblem_castle, 'desert': emblem_desert, 'forest': emblem_forest,
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
# the world map (draft: composition first)
# ---------------------------------------------------------------------------

MAP_W, MAP_H = 368, 900
MX = 12.0                        # metres across the picture
MAP_EL = math.radians(58)        # the camera looks north (+Y) and down
PX_M = MAP_W / MX
MAP_DEPTH = (MAP_H / PX_M) / math.sin(MAP_EL)
# zone bands along Y (south -> north); zone 1 at the bottom of the picture
BANDS = [('home', 0.6, 2.9), ('city', 2.9, 8.7), ('castle', 8.7, 14.9), ('desert', 14.9, 21.4),
         ('forest', 21.4, 28.3), ('fog', 28.4, 60.0)]
ZONES = ['city', 'castle', 'desert', 'forest']
HOME_Y = 2.15                    # the house pad, the first spot of the path
FOG_Y = 28.4
FORK_Y = 27.9


def _n(x, y, s=1.0, seed=0.0):
    return noise.noise(V((x * s + seed, y * s - seed, seed * 0.37)))


def border(k, x):
    """The wavy northern border of band k."""
    y = BANDS[k][2]
    return y + 0.75 * math.sin(x * 0.55 + 1.7 * k) + 0.35 * _n(x, 0.5, 0.6, 20.0 + k)


def band_of(x, y):
    for k, (z, y0, y1) in enumerate(BANDS):
        if k == len(BANDS) - 1 or y < border(k, x):
            return z
    return 'fog'


def path_x(y):
    return 6.0 + 2.9 * math.sin(y * 0.52 + 0.9) + 0.5 * math.sin(y * 1.3)


def coast(y):
    """x of the west and east shores at y: bays and capes, never closer
    than 1.8 m to the path."""
    xl = 1.25 + 0.95 * math.sin(y * 0.33 + 2.0) + 0.45 * _n(0.3, y, 0.45, 1.0)
    xr = MX - 1.25 - 0.95 * math.sin(y * 0.29 + 0.4) - 0.45 * _n(0.7, y, 0.45, 2.0)
    px_ = path_x(y)
    return max(0.35, min(xl, px_ - 1.8)), min(MX - 0.35, max(xr, px_ + 1.8))


def south_shore(x):
    return 0.9 + 0.35 * _n(x, 0.2, 0.4, 3.0)


def land_mask(x, y):
    """1 on land, 0 at sea, soft over ~0.3 m."""
    xl, xr = coast(y)
    d = min(x - xl, xr - x, y - south_shore(x))
    return max(0.0, min(1.0, d / 0.3 + 0.5))


def hill(x, y, cx, cy, h, sx, sy):
    return h * math.exp(-(((x - cx) / sx) ** 2 + ((y - cy) / sy) ** 2))


CASTLE_C = (3.5, 11.9)
PYRAMID_C = (8.7, 18.4)
OASIS_C = (9.1, 16.1)
GRAVE_C = (8.6, 31.6)
HUT_C = (3.2, 31.2)
SWAMP_PAD = (2.9, 29.5)
GRAVE_PAD = (8.4, 29.7)


def terrain_h(x, y):
    m = land_mask(x, y)
    if m <= 0:
        return -0.35
    h = 0.24 + 0.05 * _n(x, y, 1.3, 5.0)
    # the castle's hill: a flat-topped crag, so it reads as a hill from above
    dc = math.hypot((x - CASTLE_C[0]) / 2.0, (y - CASTLE_C[1]) / 1.6) + 0.12 * _n(x, y, 1.2, 40.0)
    h += 1.9 * max(0.0, min(1.0, (1.25 - dc) / 0.55)) ** 1.5
    z = band_of(x, y)
    if z == 'desert':
        h += 0.07 * math.sin(x * 2.2 + y * 0.9 + 1.5 * _n(x, y, 0.5, 7.0)) + 0.05
    elif z == 'forest':
        h += 0.10 * (0.5 + 0.5 * _n(x, y, 0.6, 9.0))
    elif z == 'city':
        h += 0.0
    h += hill(x, y, GRAVE_C[0], GRAVE_C[1], 1.35, 1.9, 1.5)
    if y > FOG_Y and x < 6.5:
        h -= 0.06
    # a steeper, clearer coast: the land ends in a little cliff
    k = m ** 0.5
    return h * k + (-0.35) * (1 - k)


ZONE_COL = {
    'home': ['#7cbc52', '#62a444'],
    'city': ['#6f757c', '#5c6268'],
    'castle': ['#7a6aa0', '#5d4d86'],
    'desert': ['#dcae68', '#c8924a'],
    'forest': ['#4f9a44', '#2f7236'],
    'fog_swamp': ['#4e6a54', '#3a5444'],
    'fog_grave': ['#6c7a70', '#56645a'],
}


def ground_color(x, y, h):
    z = band_of(x, y)
    if z == 'fog':
        z = 'fog_swamp' if x < 6.0 + 0.6 * _n(x, y, 0.5, 12.0) else 'fog_grave'
    c0, c1 = ZONE_COL[z]
    t = 0.5 + 0.5 * _n(x, y, 1.6, 13.0)
    a_ = L.hexlin(c0)
    b_ = L.hexlin(c1)
    col = [a_[i] * (1 - t) + b_[i] * t for i in range(3)]
    # steep ground is rock (the castle crag, the graveyard hill)
    e = 0.06
    gx = (terrain_h(x + e, y) - terrain_h(x - e, y)) / (2 * e)
    gy = (terrain_h(x, y + e) - terrain_h(x, y - e)) / (2 * e)
    sl = math.hypot(gx, gy)
    if sl > 0.9 and h > 0.3:
        rk = L.hexlin('#4e4262') if z == 'castle' else L.hexlin('#5a5e5a')
        k = max(0.0, min(1.0, (sl - 0.9) / 0.8))
        col = [col[i] * (1 - k) + rk[i] * k for i in range(3)]
    if z == 'city':
        # a street grid
        gx, gy = (x - 0.3) % 1.6, (y - 0.2) % 1.5
        if gx < 0.28 or gy < 0.26:
            col = list(L.hexlin('#3e4248'))
    # beach where land meets the sea, darker earth on the cliff
    if h < 0.19:
        s = L.hexlin('#e6d09a') if h > -0.05 else L.hexlin('#8a6a48')
        k = max(0.0, min(1.0, (0.19 - h) / 0.12))
        col = [col[i] * (1 - k) + s[i] * k for i in range(3)]
    return col


def build_terrain():
    nx, ny = 150, 420
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
        Lk.new(rp.outputs['Color'], p.inputs['Base Color'])
        return p.outputs['BSDF']
    C.mat('map_sea', build=sb)
    return C.box('sea', -2, -3, -0.5, MX + 2, MAP_DEPTH + 4, 0.0, 'map_sea')


def ribbon(name, pts, width, key, lift=0.035):
    verts, faces = [], []
    for i, (x, y) in enumerate(pts):
        if i < len(pts) - 1:
            dx, dy = pts[i + 1][0] - x, pts[i + 1][1] - y
        else:
            dx, dy = x - pts[i - 1][0], y - pts[i - 1][1]
        ln = math.hypot(dx, dy) or 1.0
        nx_, ny_ = -dy / ln * width / 2, dx / ln * width / 2
        for sgn in (-1, 1):
            px_, py_ = x + sgn * nx_, y + sgn * ny_
            verts.append((px_, py_, terrain_h(px_, py_) + lift))
    for i in range(len(pts) - 1):
        k = 2 * i
        faces.append((k, k + 1, k + 3, k + 2))
    return L.mesh_from(name, verts, faces, key)


def g_map():
    if not want('map'):
        return
    samples = MAP_DRAFT_SAMPLES if SAMPLE else a.samples
    rnd = random.Random(21)
    objs = [build_terrain(), build_sea()]
    C.mat('m_path', base=L.hexlin('#ecdcae'), rough=0.8)
    C.mat('m_path_edge', base=L.hexlin('#6a5236'), rough=0.9)
    C.mat('m_pad', build=L.gloss_build('#d8d2c4', rough=0.6, coat=0.2))
    C.mat('m_pad_side', base=L.hexlin('#7a7266'), rough=0.8)
    C.mat('m_pad_ring', build=L.gold_build(emit=0.5, rim=0.5))
    C.mat('m_pad_lk', build=L.gloss_build('#5a5470', rough=0.6, coat=0.2))
    C.mat('m_pad_home', build=L.gloss_build('#f0b440', emit_col='#ffc040', emit=0.35, rough=0.4, coat=0.4))
    C.mat('m_pad_ring_lk', build=L.gloss_build('#9a70ff', emit_col='#9a70ff', emit=0.8, rough=0.3))
    # --- the path: from the city's shore up to the fog, then forking
    ys = [HOME_Y + 0.1 * i for i in range(int((FORK_Y - HOME_Y) / 0.1) + 1)]
    main = [(path_x(y), y) for y in ys]
    fx = path_x(FORK_Y)
    left = [(fx + (SWAMP_PAD[0] - fx) * t + 0.35 * math.sin(t * math.pi), FORK_Y + (SWAMP_PAD[1] - FORK_Y) * t)
            for t in [i / 20 for i in range(21)]]
    right = [(fx + (GRAVE_PAD[0] - fx) * t - 0.35 * math.sin(t * math.pi), FORK_Y + (GRAVE_PAD[1] - FORK_Y) * t)
             for t in [i / 20 for i in range(21)]]
    for nm, pts in (('main', main), ('left', left), ('right', right)):
        objs.append(ribbon('path_e_' + nm, pts, 0.62, 'm_path_edge', 0.03))
        objs.append(ribbon('path_' + nm, pts, 0.44, 'm_path', 0.045))
    # --- level pads: 4 per zone, evenly along the path inside the zone
    pads = {}
    for zone in ZONES:
        on = [y for y in ys if band_of(path_x(y), y) == zone]
        y0, y1 = min(on), max(on)
        pads[zone] = []
        for k in range(4):
            y = y0 + (y1 - y0) * (0.12 + 0.253 * k)
            pads[zone].append(V((path_x(y), y, 0)))
    pads['house'] = [V((path_x(HOME_Y), HOME_Y, 0))]
    pads['swamp'] = [V(SWAMP_PAD + (0,))]
    pads['graveyard'] = [V(GRAVE_PAD + (0,))]
    for zone, lst in pads.items():
        for p in lst:
            p.z = terrain_h(p.x, p.y) + 0.06
            lk = zone in ('swamp', 'graveyard')
            if zone == 'house':
                continue
            objs.append(L.cylinder('pad', 0.42, 0.14, 'm_pad_side', (p.x, p.y, p.z - 0.02), (0, 0, 0), None, 32))
            objs.append(L.cylinder('pad_top', 0.36, 0.05, 'm_pad_lk' if lk else 'm_pad', (p.x, p.y, p.z + 0.06), (0, 0, 0), None, 32))
            objs.append(L.torus('pad_ring', 0.37, 0.025, 'm_pad_ring_lk' if lk else 'm_pad_ring', (p.x, p.y, p.z + 0.075), (0, 0, 0), None, (32, 8)))
    # --- zone props
    hp = pads['house'][0]
    hp.z = terrain_h(hp.x, hp.y) + 0.06
    objs.append(L.cylinder('pad_home', 0.42, 0.14, 'm_pad_side', (hp.x, hp.y, hp.z - 0.02), (0, 0, 0), None, 32))
    objs.append(L.cylinder('pad_home_t', 0.36, 0.05, 'm_pad_home', (hp.x, hp.y, hp.z + 0.06), (0, 0, 0), None, 32))
    objs.append(L.torus('pad_home_r', 0.37, 0.025, 'm_pad_ring', (hp.x, hp.y, hp.z + 0.075), (0, 0, 0), None, (32, 8)))
    objs += map_home(rnd, hp)
    objs += map_city(rnd) + map_castle(rnd) + map_desert(rnd) + map_forest(rnd) + map_future(rnd)
    fog = map_fog(rnd)
    L.update()
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
    info_levels = {z: [px_of(p, MAP_W, MAP_H) for p in lst] for z, lst in pads.items() if z in ('city', 'castle', 'desert', 'forest')}
    locked = {z: px_of(pads[z][0], MAP_W, MAP_H) for z in ('swamp', 'graveyard')}
    info_levels['house'] = px_of(pads['house'][0], MAP_W, MAP_H)
    ui_render('map', MAP_W, MAP_H, objs + fog, samples, fit='VERTICAL',
              extra={'levels': info_levels, 'locked': locked, 'house': info_levels['house'], 'draft': bool(SAMPLE),
                     'pad_radius_px': round(0.42 * PX_M, 1), 'scroll': 'vertical, zone 1 at the bottom'})
    remove(objs + fog + [moon])


def _win_mat():
    if 'm_win' not in C._MATS:
        C.mat('m_win', build=L.emit_build('#ffb040', 2.2))
        C.mat('m_win_v', build=L.emit_build('#ff9a30', 2.0))


def on_path(x, y, r=0.7):
    return abs(x - path_x(y)) < r


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
    """Tommy's house at the start of the path, on a patch of lawn."""
    hx, hy = pad.x - 2.2, pad.y - 0.45
    return build_house(hx, hy, terrain_h(hx, hy), 1.0)


def map_city(rnd):
    _win_mat()
    C.mat('m_bld_a', base=L.hexlin('#8a8f96'), rough=0.8)
    C.mat('m_bld_b', base=L.hexlin('#9a6a5a'), rough=0.8)
    C.mat('m_bld_c', base=L.hexlin('#6a7a80'), rough=0.8)
    C.mat('m_roof', base=L.hexlin('#3c4046'), rough=0.9)
    C.mat('m_car', base=L.hexlin('#b0503a'), rough=0.5)
    C.mat('m_teal', base=L.hexlin('#3aa0a0'), rough=0.5)
    o = []
    for gx in range(8):
        for gy in range(6):
            x = 0.3 + 1.6 * gx + 0.8
            y = 0.2 + 1.5 * gy + 0.75 + 1.0
            if band_of(x, y) != 'city' or land_mask(x, y) < 1 or on_path(x, y, 1.0):
                continue
            if rnd.random() < 0.18:
                continue
            w, d = rnd.uniform(0.7, 1.1), rnd.uniform(0.6, 1.0)
            h = rnd.uniform(0.5, 1.4)
            z0 = terrain_h(x, y)
            key = rnd.choice(['m_bld_a', 'm_bld_b', 'm_bld_c'])
            fate = rnd.random()
            if fate < 0.14:
                # a collapsed lot: only rubble left
                for k in range(7):
                    rx, ry = x + rnd.uniform(-w / 2, w / 2), y + rnd.uniform(-d / 2, d / 2)
                    rr = rnd.uniform(0.07, 0.16)
                    ch = L.centered(C.box('rubble', rx - rr, ry - rr, z0, rx + rr, ry + rr, z0 + rr * 1.2, key))
                    ch.rotation_euler = (rnd.uniform(-0.5, 0.5), rnd.uniform(-0.5, 0.5), rnd.uniform(0, 1.5))
                    o.append(ch)
                continue
            b = L.centered(C.box('bld', x - w / 2, y - d / 2, z0 - 0.1, x + w / 2, y + d / 2, z0 + h, key))
            if rnd.random() < 0.3:
                b.rotation_euler = (rnd.uniform(-0.08, 0.08), rnd.uniform(-0.1, 0.1), 0)
            o.append(b)
            if fate < 0.45:
                # broken top: a slab of the upper floor fallen askew, rubble at the foot
                tp = L.centered(C.box('broken', x - w * 0.4, y - d * 0.35, z0 + h, x + w * 0.25, y + d * 0.35, z0 + h + 0.25, key))
                tp.rotation_euler = (rnd.uniform(-0.35, 0.35), rnd.uniform(-0.45, 0.45), rnd.uniform(-0.3, 0.3))
                o.append(tp)
                for k in range(3):
                    rx = x + rnd.uniform(-w / 2, w / 2)
                    rr = rnd.uniform(0.05, 0.1)
                    ch = L.centered(C.box('rubble', rx - rr, y - d / 2 - 0.2 - rr, z0, rx + rr, y - d / 2 - 0.2 + rr, z0 + rr, key))
                    ch.rotation_euler = (rnd.uniform(-0.5, 0.5), rnd.uniform(-0.5, 0.5), rnd.uniform(0, 1.5))
                    o.append(ch)
            else:
                o.append(C.box('roof', x - w / 2 - 0.03, y - d / 2 - 0.03, z0 + h, x + w / 2 + 0.03, y + d / 2 + 0.03, z0 + h + 0.06, 'm_roof'))
            for k in range(int(h / 0.3)):
                if rnd.random() < 0.45:
                    wx = x + rnd.uniform(-w / 2 + 0.12, w / 2 - 0.12)
                    wz = z0 + 0.2 + k * 0.3
                    o.append(C.box('win', wx - 0.06, y - d / 2 - 0.02, wz, wx + 0.06, y - d / 2 + 0.01, wz + 0.12, 'm_win'))
    # dead trees and a junkyard of crushed cars by the west shore
    C.mat('m_rust', base=L.hexlin('#8a4a2a'), rough=0.8)
    C.mat('m_deadtree', base=L.hexlin('#4a3a30'), rough=0.9)
    for i in range(5):
        y = rnd.uniform(3.2, 8.4)
        x = path_x(y) + rnd.choice([-0.7, 0.7])
        zz = terrain_h(x, y)
        o.append(L.cylinder('dtree', 0.04, 0.6, 'm_deadtree', (x, y, zz + 0.3), (rnd.uniform(-0.2, 0.2), rnd.uniform(-0.2, 0.2), 0), None, 8))
        o.append(L.cylinder('dbranch', 0.02, 0.3, 'm_deadtree', (x + 0.08, y, zz + 0.5), (0, 0.8, 0), None, 6))
    jx, jy = coast(5.2)[0] + 0.9, 5.2
    for k in range(9):
        rx, ry = jx + rnd.uniform(-0.45, 0.45), jy + rnd.uniform(-0.5, 0.5)
        zz = terrain_h(rx, ry) + 0.1 * (k % 3)
        c = L.centered(L.rbox('junk', rx - 0.22, ry - 0.11, zz, rx + 0.22, ry + 0.11, zz + 0.14, rnd.choice(['m_rust', 'm_car', 'm_teal']), 0.03))
        c.rotation_euler = (rnd.uniform(-0.4, 0.4), rnd.uniform(-0.4, 0.4), rnd.uniform(0, 3))
        o.append(c)
    # street lamps and a few wrecked cars
    for i in range(7):
        y = rnd.uniform(3.2, 8.4)
        x = path_x(y) + rnd.choice([-0.55, 0.55])
        z0 = terrain_h(x, y)
        o.append(C.box('lamp_post', x - 0.025, y - 0.025, z0, x + 0.025, y + 0.025, z0 + 0.55, 'm_roof'))
        o.append(L.sphere('lamp', 0.06, 'm_win', (x, y, z0 + 0.58), seg=12, rings=6))
    for i in range(4):
        y = rnd.uniform(3.2, 8.2)
        x = path_x(y) + rnd.choice([-1.2, 1.2])
        z0 = terrain_h(x, y)
        c = L.centered(L.rbox('car', x - 0.3, y - 0.15, z0, x + 0.3, y + 0.15, z0 + 0.2, rnd.choice(['m_car', 'm_teal']), 0.04))
        c.rotation_euler = (0, 0, rnd.uniform(-0.6, 0.6))
        o.append(c)
    return o


def map_castle(rnd):
    _win_mat()
    C.mat('m_stone', base=L.hexlin('#9a8ab8'), rough=0.8)
    C.mat('m_stone_dk', base=L.hexlin('#6e5e92'), rough=0.8)
    C.mat('m_roof_v', build=L.gloss_build('#5a2a7a', rough=0.5, coat=0.3))
    C.mat('m_roof_r', build=L.gloss_build('#a02a4a', rough=0.5, coat=0.3))
    o = []
    cx, cy = CASTLE_C
    z0 = terrain_h(cx, cy)
    o.append(C.box('keep', cx - 0.7, cy - 0.5, z0 - 0.2, cx + 0.7, cy + 0.5, z0 + 1.5, 'm_stone'))
    o.append(L.cone('keep_roof', 0.95, 0.0, 1.0, 'm_roof_v', (cx, cy, z0 + 2.0), (0, 0, math.pi / 4), None, 4))
    for (dx, dy, hh) in ((-1.1, -0.8, 1.7), (1.1, -0.8, 1.7), (-1.1, 0.8, 2.0), (1.1, 0.8, 2.0), (0.0, 1.0, 2.6)):
        x, y = cx + dx, cy + dy
        zz = terrain_h(x, y)
        o.append(L.cylinder('tower', 0.28, hh + 0.3, 'm_stone', (x, y, zz + hh / 2 - 0.15), (0, 0, 0), None, 20))
        o.append(L.cone('tower_roof', 0.36, 0.0, 0.75, 'm_roof_r' if dy < 0 else 'm_roof_v', (x, y, zz + hh + 0.37), (0, 0, 0), None, 20))
        o.append(C.box('twin', x - 0.06, y - 0.29, zz + hh - 0.5, x + 0.06, y - 0.25, zz + hh - 0.3, 'm_win'))
    # walls between the front towers
    for (x0, x1, y) in ((cx - 1.1, cx + 1.1, cy - 0.8),):
        zz = terrain_h((x0 + x1) / 2, y)
        o.append(C.box('wall', x0, y - 0.12, zz - 0.2, x1, y + 0.12, zz + 0.75, 'm_stone_dk'))
        for k in range(8):
            mx = x0 + 0.15 + k * (x1 - x0 - 0.3) / 7
            o.append(C.box('merlon', mx - 0.07, y - 0.12, zz + 0.75, mx + 0.07, y + 0.12, zz + 0.9, 'm_stone_dk'))
    # gate
    o.append(C.box('gate', cx - 0.18, cy - 0.93, z0 - 0.1, cx + 0.18, cy - 0.9, z0 + 0.45, 'm_win_v'))
    # spooky trees on the hill
    C.mat('m_deadwood', base=L.hexlin('#4a3a5a'), rough=0.9)
    for i in range(6):
        x, y = rnd.uniform(0.8, 11.0), rnd.uniform(8.8, 14.4)
        if on_path(x, y, 0.8) or abs(x - cx) < 1.6 and abs(y - cy) < 1.3 or band_of(x, y) != 'castle':
            continue
        zz = terrain_h(x, y)
        o.append(L.cone('vtree', 0.22, 0.0, 0.7, 'm_roof_v', (x, y, zz + 0.4), (0, 0, 0), None, 12))
        o.append(C.box('vtrunk', x - 0.03, y - 0.03, zz, x + 0.03, y + 0.03, zz + 0.2, 'm_deadwood'))
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
    o.append(L.cone('pyramid', 2.0, 0.0, 2.3, 'm_sandstone', (px_, py_, z0 + 1.0), (0, 0, math.pi / 4), None, 4))
    o.append(L.cone('pyr_cap', 0.26, 0.0, 0.3, 'm_gold', (px_, py_, z0 + 2.0), (0, 0, math.pi / 4), None, 4))
    o.append(C.box('pyr_door', px_ - 0.18, py_ - 1.43, z0 - 0.1, px_ + 0.18, py_ - 1.38, z0 + 0.4, 'm_sandstone_dk'))
    for (x, y, s) in ((10.5, 20.4, 0.5), (6.9, 20.7, 0.42)):
        o.append(L.cone('pyr_s', 0.9 * s * 2, 0.0, 1.1 * s * 2, 'm_sandstone_dk', (x, y, terrain_h(x, y) + 0.5 * s * 2), (0, 0, math.pi / 4), None, 4))
    # the oasis
    ox, oy = OASIS_C
    o.append(L.cylinder('oasis', 0.72, 0.06, 'm_water', (ox, oy, max(terrain_h(ox + dx_, oy + dy_) for dx_ in (-0.7, 0, 0.7) for dy_ in (-0.7, 0, 0.7)) + 0.02), (0, 0, 0), None, 32))
    for i in range(3):
        t = i * 2.1 + 0.4
        x, y = ox + 0.95 * math.cos(t), oy + 0.7 * math.sin(t)
        zz = terrain_h(x, y)
        o.append(L.cylinder('palm_t', 0.035, 0.7, 'm_sandstone_dk', (x, y, zz + 0.35), (0.15, 0.1, 0), None, 8))
        for k in range(5):
            u = k * 2 * math.pi / 5
            lf = L.sphere('palm_l', 0.2, 'm_palm', (x + 0.15 * math.cos(u), y + 0.15 * math.sin(u), zz + 0.72), (1.0, 0.35, 0.12), None, 12, 6)
            lf.rotation_euler = (0, 0.3, u)
            o.append(lf)
    for i in range(16):
        x, y = rnd.uniform(0.8, 11.2), rnd.uniform(14.2, 22.4)
        if on_path(x, y, 0.8) or (abs(x - px_) < 2.4 and abs(y - py_) < 2.4) or math.hypot(x - ox, y - oy) < 1.3:
            continue
        if land_mask(x, y) < 1 or band_of(x, y) != 'desert':
            continue
        zz = terrain_h(x, y)
        o.append(L.cylinder('cactus', 0.07, 0.4, 'm_cactus', (x, y, zz + 0.2), (0, 0, 0), None, 10))
        o.append(L.cylinder('cactus_arm', 0.05, 0.18, 'm_cactus', (x + 0.09, y, zz + 0.25), (0, 0.0, 0), None, 8))
    return o


def map_forest(rnd):
    _win_mat()
    C.mat('m_pine', base=L.hexlin('#1f6a3a'), rough=0.8)
    C.mat('m_pine2', base=L.hexlin('#2e8a44'), rough=0.8)
    C.mat('m_oak', base=L.hexlin('#4aa03a'), rough=0.8)
    C.mat('m_trunk', base=L.hexlin('#5a3a22'), rough=0.9)
    C.mat('m_plank', base=L.hexlin('#a87a4a'), rough=0.8)
    C.mat('m_river', build=L.gloss_build('#1e7a8a', emit_col='#1e7a8a', emit=0.15, rough=0.1, coat=1.0))
    o = []
    # a river across the woods
    ry = 25.0
    pts = [(x * 0.2, ry + 0.5 * math.sin(x * 0.2 * 0.8)) for x in range(-5, int(MX / 0.2) + 6)]
    o.append(ribbon('river', pts, 0.7, 'm_river', 0.01))
    # a bridge where the path crosses it
    bx = path_x(ry)
    o.append(C.box('bridge', bx - 0.26, ry - 0.5, terrain_h(bx, ry) + 0.03, bx + 0.26, ry + 0.5, terrain_h(bx, ry) + 0.07, 'm_plank'))
    for i in range(110):
        x, y = rnd.uniform(0.6, 11.4), rnd.uniform(20.8, 29.2)
        if on_path(x, y, 0.75) or abs(y - (ry + 0.5 * math.sin(x * 0.8))) < 0.55 or land_mask(x, y) < 1 \
                or band_of(x, y) != 'forest':
            continue
        zz = terrain_h(x, y)
        if rnd.random() < 0.7:
            s = rnd.uniform(0.8, 1.25)
            for k in range(3):
                o.append(L.cone('pine', (0.34 - 0.08 * k) * s, 0.0, 0.42 * s, rnd.choice(['m_pine', 'm_pine2']),
                                (x, y, zz + (0.3 + 0.22 * k) * s), (0, 0, 0), None, 12))
            o.append(C.box('pt', x - 0.03, y - 0.03, zz, x + 0.03, y + 0.03, zz + 0.2, 'm_trunk'))
        else:
            o.append(L.sphere('oak', 0.34, 'm_oak', (x, y, zz + 0.55), (1, 1, 0.85), None, 16, 8))
            o.append(C.box('ot', x - 0.04, y - 0.04, zz, x + 0.04, y + 0.04, zz + 0.3, 'm_trunk'))
    # lanterns along the path
    for i in range(5):
        y = 22.2 + i * 1.3
        x = path_x(y) + (0.45 if i % 2 else -0.45)
        o.append(L.sphere('lantern', 0.05, 'm_win', (x, y, terrain_h(x, y) + 0.3), seg=10, rings=5))
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
    for i in range(5):
        x, y = rnd.uniform(0.8, 5.5), rnd.uniform(29.0, 33.5)
        o.append(L.cylinder('pool', rnd.uniform(0.35, 0.7), 0.04, 'm_swampw', (x, y, terrain_h(x, y) + 0.01), (0, 0, 0), None, 20))
    hx, hy = HUT_C
    hz = terrain_h(hx, hy)
    hut = L.centered(C.box('hut', hx - 0.45, hy - 0.4, hz - 0.1, hx + 0.45, hy + 0.4, hz + 1.25, 'm_hut'))
    hut.rotation_euler = (0, 0.08, 0.1)
    o.append(hut)
    # a witch-hat roof: a wide brim and a tall cone bent over at the tip
    o.append(L.cylinder('hut_brim', 0.78, 0.06, 'm_hutroof', (hx, hy, hz + 1.28), (0.08, -0.1, 0), None, 24))
    o.append(L.cone('hut_roof', 0.55, 0.18, 0.9, 'm_hutroof', (hx, hy, hz + 1.72), (0.10, -0.14, 0), None, 16))
    o.append(L.cone('hut_tip', 0.18, 0.0, 0.5, 'm_hutroof', (hx + 0.2, hy, hz + 2.25), (0.0, 0.9, 0), None, 12))
    o.append(C.box('hut_win', hx - 0.14, hy - 0.43, hz + 0.75, hx + 0.14, hy - 0.40, hz + 1.05, 'm_witchwin'))
    o.append(L.sphere('hut_glow', 0.09, 'm_witchwin', (hx + 0.55, hy - 0.2, hz + 1.25), seg=10, rings=5))
    for i in range(4):
        x, y = rnd.uniform(0.8, 5.5), rnd.uniform(28.6, 33.5)
        zz = terrain_h(x, y)
        t = L.cylinder('dtree', 0.05, 0.9, 'm_dead', (x, y, zz + 0.45), (rnd.uniform(-0.2, 0.2), rnd.uniform(-0.2, 0.2), 0), None, 8)
        o.append(t)
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
    haze along its southern edge and a few glowing motes above."""
    C.mat('m_cloud', build=L.toon_build('#efe6ff', '#9a82e0', emit=0.45, rough=0.9))
    C.mat('m_cloud2', build=L.toon_build('#e2ecff', '#7e9ae6', emit=0.45, rough=0.9))
    C.mat('m_mote', build=L.emit_build('#f0d8ff', 3.0))
    o = []
    y = FOG_Y - 0.6
    row = 0
    while y < MAP_DEPTH + 1.5:
        x = -0.6 + (0.3 if row % 2 else 0.0)
        while x < MX + 0.6:
            yy = y + rnd.uniform(-0.2, 0.2)
            xx = x + rnd.uniform(-0.2, 0.2)
            edge = max(0.0, min(1.0, (yy - border(4, xx) + 0.3) / 1.6))
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
    for i in range(14):
        x, yy = rnd.uniform(0.5, MX - 0.5), rnd.uniform(FOG_Y + 0.5, MAP_DEPTH)
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
        edge.inputs['From Min'].default_value = FOG_Y - 2.2
        edge.inputs['From Max'].default_value = FOG_Y + 0.8
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
    o.append(C.box('haze', -1.0, FOG_Y - 2.4, -0.4, MX + 1.0, MAP_DEPTH + 3.0, 1.0, 'm_haze'))
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
