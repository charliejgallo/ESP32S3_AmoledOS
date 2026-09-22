"""Monster Hop - common objects and effects (SPEC section 7) -> assets/objects/.

    Blender -b -P objects.py -- --out ../../assets/objects [--sample] [--only key,chest_03] [--samples 64]

--sample   only the style sample (the frames listed in SAMPLE below)
--only     names or name prefixes: `key` = every key frame, `key_03` = one

Everything is final colour under the neutral light (the same in every zone),
kind 'dyn', anchored on the ground of cell (0, 0) (floating things too: the
anchor is the ground point below them). Passes: colour + z + shadow; lit
things add a glow on the ground (the lantern, the key, the open chest).

After Blender renders a pickup, two 2D touches go on it (objects_lib.Sprite2D):
a soft halo behind it and four-ray glints on its highlights, so keys and coins
sparkle at 20 px the way a pickup must.
"""
import math
import os
import random
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
_argv = sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else []
SAMPLE = '--sample' in _argv
sys.argv = [x for x in sys.argv if x != '--sample']     # argparse would take it for --samples

import mh_common as C          # noqa: E402
import objects_lib as L        # noqa: E402
import bpy                     # noqa: E402
from mathutils import Vector as V  # noqa: E402

a = C.args(defaults={'samples': 64})
OUT = os.path.abspath(a.out)
C.reset('neutral', cpu=a.cpu)
A0 = C.cell(0, 0, 0)
META = C._STATE['meta']
T0 = time.time()

SAMPLE_SET = set(['key_%02d' % i for i in range(8)] +
                 ['coin_00', 'heart_00', 'lantern_off', 'lantern_on_00', 'chest_00', 'chest_03'] +
                 ['fx_poof_%02d' % i for i in range(6)] + ['fx_sparkle_%02d' % i for i in range(6)])


def want(name):
    if SAMPLE and name not in SAMPLE_SET:
        return False
    if a.only:
        return any(name == o or name.startswith(o + '_') for o in a.only)
    return True


def render(name, objs, size, passes=('color', 'z', 'shadow'), extra=None, **kw):
    t = time.time()
    info = C.render_sprite(OUT, name, objs, A0, passes=passes, shadow_z=0.0, size=size,
                           samples=a.samples, kind='dyn', extra=extra, **kw)
    print('[objects] %-16s %3dx%-3d %.1fs' % (name, info['w'], info['h'], time.time() - t))
    return info


def glow_pass(stem, objs, size, frames, rmax, gain=1.0):
    """Render the glow of the current scene lights as <stem>_gl.png, fade it
    out at rmax metres, and give it to every frame in `frames` (they share
    size and anchor)."""
    keep = META.get(stem)
    info = C.render_sprite(OUT, stem, objs, A0, passes=('glow',), size=size, samples=a.samples)
    L.shape_glow(os.path.join(OUT, stem + '_gl.png'), info, rmax, gain)
    if keep is None:
        del META[stem]
    else:
        META[stem] = keep
    for n in frames:
        if n in META:
            META[n]['files']['gl'] = stem + '_gl.png'


def lamp(pos, color, power, radius=0.05, shadow=True, name='lamp'):
    ob = C.add_point_light(pos, color, power, radius, name)
    ob.data.cycles.cast_shadow = shadow
    return ob


def remove(objs):
    for ob in objs:
        if ob.name in bpy.data.objects:
            bpy.data.objects.remove(ob, do_unlink=True)


# ---------------------------------------------------------------------------
# materials (final colour)
# ---------------------------------------------------------------------------

C.mat('gold', build=L.gold_build(emit=0.55, base='#ffb81c', rough=0.20, metal=0.7, rim=0.7))
C.mat('gold_coin', build=L.gold_build(emit=0.50, base='#ffc21f', rough=0.24, metal=0.7, rim=0.65, env=L.COIN_ENV))
C.mat('gold_star', build=L.gold_build(emit=0.90, base='#ffe870', rough=0.16, metal=0.5, rim=0.3, env=L.COIN_ENV))
C.mat('gold_trim', build=L.gold_build(emit=0.30, base='#f0a818', rough=0.28, metal=0.8, rim=0.6))
C.mat('gold_pile', build=L.gold_build(emit=1.10, base='#ffc83a', rough=0.25, metal=0.5, rim=0.3))
C.mat('heart', build=L.gloss_build('#f0142e', emit_col='#ff1a3c', emit=0.30, rough=0.16, coat=1.0))
C.mat('heart_hi', build=L.emit_build('#ffe8ee', 1.0))


def _wood(base, dark, groove, period, axis='Z'):
    bl, dl, gl = L.hexlin(base), L.hexlin(dark), L.hexlin(groove)

    def b(nt, neutral):
        N, Lk = nt.nodes, nt.links
        p = N.new('ShaderNodeBsdfPrincipled')
        p.inputs['Roughness'].default_value = 0.75
        p.inputs['Specular'].default_value = 0.3
        tc = N.new('ShaderNodeTexCoord')
        sep = N.new('ShaderNodeSeparateXYZ')
        Lk.new(tc.outputs['Object'], sep.inputs[0])
        m = N.new('ShaderNodeMath')
        m.operation = 'MULTIPLY'
        m.inputs[1].default_value = 1.0 / period
        Lk.new(sep.outputs[axis], m.inputs[0])
        fr = N.new('ShaderNodeMath')
        fr.operation = 'FRACT'
        Lk.new(m.outputs[0], fr.inputs[0])
        fl = N.new('ShaderNodeMath')
        fl.operation = 'FLOOR'
        Lk.new(m.outputs[0], fl.inputs[0])
        # a random tone per plank
        wn = N.new('ShaderNodeTexWhiteNoise')
        wn.noise_dimensions = '1D'
        Lk.new(fl.outputs[0], wn.inputs['W'])
        mix1 = N.new('ShaderNodeMixRGB')
        mix1.inputs['Color1'].default_value = bl + (1,)
        mix1.inputs['Color2'].default_value = dl + (1,)
        Lk.new(wn.outputs['Value'], mix1.inputs['Fac'])
        # grain
        nz = N.new('ShaderNodeTexNoise')
        nz.inputs['Scale'].default_value = 18.0
        nz.inputs['Detail'].default_value = 3.0
        Lk.new(tc.outputs['Object'], nz.inputs['Vector'])
        mix2 = N.new('ShaderNodeMixRGB')
        mix2.blend_type = 'MULTIPLY'
        mix2.inputs['Fac'].default_value = 0.35
        Lk.new(mix1.outputs[0], mix2.inputs['Color1'])
        Lk.new(nz.outputs['Fac'], mix2.inputs['Color2'])
        # dark grooves between planks
        lt = N.new('ShaderNodeMath')
        lt.operation = 'LESS_THAN'
        lt.inputs[1].default_value = 0.12
        Lk.new(fr.outputs[0], lt.inputs[0])
        mix3 = N.new('ShaderNodeMixRGB')
        mix3.inputs['Color2'].default_value = gl + (1,)
        Lk.new(lt.outputs[0], mix3.inputs['Fac'])
        Lk.new(mix2.outputs[0], mix3.inputs['Color1'])
        if neutral:
            p.inputs['Base Color'].default_value = (0.8, 0.8, 0.8, 1)
        else:
            Lk.new(mix3.outputs[0], p.inputs['Base Color'])
        return p.outputs['BSDF']
    return b


C.mat('chest_wood', build=_wood('#b0602a', '#8a4520', '#3a1808', 0.105, 'Z'))
C.mat('chest_wood_lid', build=_wood('#b86630', '#8e4822', '#3a1808', 0.085, 'Y'))
C.mat('chest_inside', base=L.hexlin('#4a2210'), rough=0.9)
C.mat('iron', base=L.hexlin('#1c1414'), rough=0.5, metal=0.3)
C.mat('post_wood', build=_wood('#a0703e', '#84562e', '#4a2c14', 0.09, 'X'))
C.mat('pumpkin', build=L.gloss_build('#ff7a10', emit_col='#ff6a00', emit=0.10, rough=0.45, coat=0.3, spec=0.4))
C.mat('pk_in_off', base=L.hexlin('#2a1206'), rough=0.9)
C.mat('pk_in_on', build=L.gloss_build('#ffd060', emit_col='#ffb030', emit=1.6, rough=0.6, coat=0.0, spec=0.2))
C.mat('stem', base=L.hexlin('#5c6b1e'), rough=0.7)
C.mat('leaf', base=L.hexlin('#4fa02a'), rough=0.6)
C.mat('flame_core', build=L.emit_build('#fff6c0', 6.0))
C.mat('flame_out', build=L.emit_build('#ffa21e', 4.0))
C.mat('smoke', build=L.toon_build('#f4f0ff', '#c4b2f0', emit=0.38, rough=0.85))
C.mat('smoke_flash', build=L.emit_build('#ffffff', 1.0))
C.mat('spark_gold', build=L.emit_build('#ffc830', 1.0))
C.mat('spark_pale', build=L.emit_build('#fff3a8', 1.0))
C.mat('spark_white', build=L.emit_build('#ffffff', 1.0))

GOLD_HALO = (1.0, 0.86, 0.35)


# ---------------------------------------------------------------------------
# spinning pickups: key, coin, heart
# ---------------------------------------------------------------------------

KEY_Z = 0.57        # centre of the key above the ground
KEY_S = 1.12
KEY_ZS = 1.25       # stretched in Z so the bow reads round under the 44 deg camera
COIN_Z = 0.36
COIN_ZS = 1.30
HEART_Z = 0.42
HEART_ZS = 1.12

# glints per spin frame: (point, ray length px, strength); points are local
# to the spinning root (objects_lib.KEY_GLINT_PTS for the key)
KEY_GLINTS = {0: [('bow_f', 9, 1.0)], 1: [('bow_f', 5, 0.85)], 2: [('ball', 3, 0.7)], 3: [('bit', 3, 0.6)],
              4: [('bow_b', 9, 1.0)], 5: [('bow_b', 5, 0.85)], 6: [('ball', 3, 0.7)], 7: []}
# and a twinkle that circles the key (screen-plane offsets in metres from its centre)
for _f in range(8):
    _t = math.radians(200 + 135 * _f)
    KEY_GLINTS[_f].append(((0.25 * math.cos(_t), 0.31 * math.sin(_t)), 4.5 if _f % 2 else 3.5, 1.0))
COIN_PTS = {'rim_f': (-0.095, -0.03, 0.105), 'rim_b': (0.095, 0.03, 0.105), 'top': (0.0, 0.0, 0.165)}
COIN_GLINTS = {0: [('rim_f', 6, 1.0)], 1: [('rim_f', 3, 0.7)], 2: [('top', 3, 0.6)], 3: [],
               4: [('rim_b', 6, 1.0)], 5: [('rim_b', 3, 0.7)], 6: [('top', 3, 0.6)], 7: []}
HEART_PTS = {'lobe_f': (-0.09, -0.06, 0.10), 'lobe_b': (0.09, 0.06, 0.10)}
HEART_GLINTS = {0: [('lobe_f', 4, 0.75)], 4: [('lobe_b', 4, 0.75)]}


def spin_group(stem, build, float_z, n=8, ms=80, glints=None, pts=None, zs=1.0, halo=True, glow=None,
               ang0=0.0, halo_kw=None, extra=None):
    names = ['%s_%02d' % (stem, f) for f in range(n)]
    todo = [nm for nm in names if want(nm)]
    if not todo:
        return
    root = L.empty(stem + '_root', A0 + V((0, 0, float_z)))
    root.scale = (1, 1, zs)
    objs = build(root)

    def pose(f):
        root.rotation_euler = (0, 0, C.PSI + math.radians(ang0) + f * 2 * math.pi / n)
        L.update()

    sizes = []
    for f in range(n):
        pose(f)
        sizes.append(C.fit(objs, A0, 3, 0.0))
    size = L.size_union(*sizes)
    if glow:
        size = L.size_union(size, L.size_ground_disc(A0, glow['r']))
    size = L.grow(size, 8 if halo else 2)
    zc = int(round(128 + float_z * C.F.z / C.DEPTH_UNIT))
    hk = dict(halo=GOLD_HALO, sigma=3.2, gain=2.0, amax=0.62)
    hk.update(halo_kw or {})
    for f, nm in enumerate(names):
        if nm not in todo:
            continue
        pose(f)
        ex = {'anim': 'spin', 'frame': f, 'frames': n, 'ms': ms, 'float_m': float_z}
        ex.update(extra or {})
        render(nm, objs, size, extra=ex)
        if halo:
            s = L.Sprite2D(OUT, nm)
            s.halo(hk['halo'], sigma=hk['sigma'], gain=hk['gain'], amax=hk['amax'], z=zc + 3)
            for src, ln, st in (glints or {}).get(f, []):
                if isinstance(src, tuple):          # a point around the root, not on the model
                    w = root.matrix_world.translation + C.R * src[0] + C.UP * src[1]
                else:
                    w = root.matrix_world @ V(pts[src])
                x, y = s.px(w)
                s.glint(x, y, ln, st, z=zc - 4)
            s.save()
    if glow:
        pose(0)
        lt = lamp(A0 + V((0, 0, glow['h'])), glow['color'], glow['power'], 0.2, shadow=False)
        glow_pass(stem, objs, size, todo, glow['r'], glow.get('gain', 1.0))
        remove([lt])
    remove(objs + [root])


def build_key(root):
    return L.build_key(root, 'gold', s=KEY_S)


def build_coin(root):
    o = []
    R, T = 0.17, 0.055
    d = L.cylinder('coin_disc', R, T, 'gold_coin', (0, 0, 0), (math.pi / 2, 0, 0), root, 48, sm=True)
    bv = d.modifiers.new('bevel', 'BEVEL')
    bv.width = 0.016
    bv.segments = 3
    bv.limit_method = 'ANGLE'
    o.append(d)
    for sgn in (-1, 1):
        o.append(L.torus('coin_rim', R - 0.024, 0.014, 'gold_coin', (0, sgn * T / 2, 0), (math.pi / 2, 0, 0), root, (48, 10)))
        st = L.pillow('coin_star', L.star_outline(0.105, 0.048, 5, 4), 0.024, 'gold_star', rings=6, q=0.7,
                      back=False, parent=root, y0=0.0)
        if sgn > 0:
            st.rotation_euler = (0, 0, math.pi)       # face +Y
        st.location = (0, sgn * (T / 2 - 0.002), 0)
        o.append(st)
    return o


def build_heart(root):
    ol = L.heart_outline(0.38, 96)
    h = L.pillow('heart', ol, 0.075, 'heart', rings=12, q=0.75, parent=root)
    h.location = (0, 0, -0.02)
    return [h]


def g_pickups():
    spin_group('key', build_key, KEY_Z, 8, 80, KEY_GLINTS, L.KEY_GLINT_PTS, KEY_ZS,
               glow=dict(r=0.72, h=0.55, color=(1.0, 0.80, 0.35), power=3.0),
               extra={'height_m': 0.95})
    spin_group('coin', build_coin, COIN_Z, 8, 70, COIN_GLINTS, COIN_PTS, COIN_ZS,
               halo_kw=dict(sigma=2.2, gain=1.3, amax=0.40), extra={'height_m': 0.60})
    spin_group('heart', build_heart, HEART_Z, 8, 80, HEART_GLINTS, HEART_PTS, HEART_ZS,
               halo_kw=dict(halo=(1.0, 0.45, 0.55), sigma=2.2, gain=1.2, amax=0.35), extra={'height_m': 0.64})


# ---------------------------------------------------------------------------
# the checkpoint: a jack-o'-lantern on a short post
# ---------------------------------------------------------------------------

PK_R = 0.26
PK_SQ = 0.80
PK_PITCH = -14.0    # the face tilts up towards the camera
POST_H = 0.28


def _prism(name, poly, y0, y1):
    n = len(poly)
    verts = [(x, y0, z) for x, z in poly] + [(x, y1, z) for x, z in poly]
    faces = [list(range(n)), list(range(2 * n - 1, n - 1, -1))]
    for i in range(n):
        j = (i + 1) % n
        faces.append((i, j, n + j, n + i))
    me = bpy.data.meshes.new(name)
    me.from_pydata(verts, [], faces)
    me.validate()
    ob = bpy.data.objects.new(name, me)
    C.link(ob)
    return ob


def _face_polys(R):
    s = R / 0.2
    eyeL = [(-0.115, 0.035), (-0.030, 0.030), (-0.078, 0.105)]
    eyeR = [(0.030, 0.030), (0.115, 0.035), (0.078, 0.105)]
    nose = [(-0.022, -0.005), (0.022, -0.005), (0.0, 0.028)]
    top, bot = [], []
    xs = [x / 100.0 for x in range(-13, 14)]
    for x in xs:
        zt = -0.028 - 0.030 * (1 - (x / 0.13) ** 2)
        if -0.058 <= x <= -0.022:
            zt -= 0.032
        top.append((x, zt))
    for x in reversed(xs[1:-1]):
        zb = -0.030 - 0.085 * (1 - (x / 0.13) ** 2)
        if 0.022 <= x <= 0.058:
            zb += 0.034
        bot.append((x, zb))
    mouth = top + bot
    return [[(x * s * 1.08, z * s * 1.15) for x, z in p] for p in (eyeL, eyeR, nose, mouth)]


def build_lantern(lit):
    root = L.empty('lantern_root', A0)
    root.rotation_euler = (0, 0, C.PSI - math.radians(8))
    objs = []
    objs.append(L.rbox('post', -0.06, -0.06, 0.0, 0.06, 0.06, POST_H, 'post_wood', 0.014, parent=root))
    objs.append(L.rbox('post_cap', -0.13, -0.13, POST_H, 0.13, 0.13, POST_H + 0.04, 'post_wood', 0.014, parent=root))
    cz = POST_H + 0.04 + PK_R * PK_SQ - 0.025
    bpy.ops.mesh.primitive_uv_sphere_add(radius=1.0, segments=64, ring_count=32, location=(0, 0, 0))
    pk = bpy.context.object
    pk.name = 'pumpkin'
    for v in pk.data.vertices:
        x, y, z = v.co
        phi = math.atan2(y, x)
        rr = math.sqrt(x * x + y * y)
        rib = 1.0 - 0.075 * (0.5 - 0.5 * math.cos(8 * (phi + math.pi / 2)))
        zz = z * PK_SQ * (1 - 0.22 * (1 - rr) ** 2)
        v.co = (x * PK_R * rib, y * PK_R * rib, zz * PK_R)
    L.smooth(pk)
    C.assign(pk, 'pumpkin')
    C.assign(pk, 'pk_in_on' if lit else 'pk_in_off')
    so = pk.modifiers.new('solid', 'SOLIDIFY')
    so.thickness = 0.024
    so.offset = -1.0
    so.use_even_offset = True
    so.use_rim = True
    so.material_offset = 1
    so.material_offset_rim = 1
    cutters = []
    for i, poly in enumerate(_face_polys(PK_R)):
        cu = _prism('pk_cut%d' % i, poly, -PK_R - 0.1, -PK_R * 0.35)
        for p in cu.data.polygons:
            p.material_index = 1
        cu.data.materials.append(bpy.data.materials.new('cut_a'))
        cu.data.materials.append(bpy.data.materials.new('cut_b'))
        cu.parent = pk
        cu.hide_render = True
        cutters.append(cu)
        bo = pk.modifiers.new('cut%d' % i, 'BOOLEAN')
        bo.operation = 'DIFFERENCE'
        bo.solver = 'EXACT'
        bo.object = cu
    # bake the carving into the mesh now (and check it: an exact boolean that
    # fails leaves an empty mesh, and the pumpkin would silently vanish)
    L.update()
    dg = bpy.context.evaluated_depsgraph_get()
    me = bpy.data.meshes.new_from_object(pk.evaluated_get(dg))
    if len(me.polygons) < 500:
        for m in list(pk.modifiers):
            if m.type == 'BOOLEAN':
                m.solver = 'FAST'
        L.update()
        dg = bpy.context.evaluated_depsgraph_get()
        me = bpy.data.meshes.new_from_object(pk.evaluated_get(dg))
    assert len(me.polygons) >= 500, 'pumpkin carving failed'
    for m in list(pk.modifiers):
        pk.modifiers.remove(m)
    pk.data = me
    pk.parent = root
    pk.location = (0, 0, cz)
    pk.rotation_euler = (math.radians(PK_PITCH), 0, 0)
    objs.append(pk)
    objs.append(L.cylinder('pk_stem', 0.026, 0.09, 'stem', (0.01, 0.01, cz + PK_R * PK_SQ * 0.78 + 0.03),
                           (0.25, 0.18, 0), root, 12))
    lf = L.sphere('pk_leaf', 0.05, 'leaf', (0.07, 0.03, cz + PK_R * PK_SQ * 0.80), (1.0, 0.55, 0.18), root, 16, 8)
    lf.rotation_euler = (0.2, -0.35, 0.5)
    objs.append(lf)
    flames = []
    if lit:
        flames.append(L.sphere('flame_core', 0.028, 'flame_core', (0, 0.02, cz - 0.03), (1, 1, 1.9), root, 16, 8))
        flames.append(L.sphere('flame_out', 0.045, 'flame_out', (0, 0.03, cz - 0.02), (1, 1, 1.8), root, 16, 8))
    return root, objs, flames, cutters, V((0, 0, cz))


def g_lantern():
    names = ['lantern_off'] + ['lantern_on_%02d' % i for i in range(3)]
    todo = [n for n in names if want(n)]
    if not todo:
        return
    size = None
    for lit in (False, True):
        root, objs, flames, cutters, pc = build_lantern(lit)
        L.update()
        if size is None:
            size = L.grow(L.size_union(C.fit(objs, A0, 3, 0.0), L.size_ground_disc(A0, 1.25)), 2)
        pcw = root.matrix_world @ pc
        ex = {'height_m': round(pcw.z + PK_R * PK_SQ + 0.08, 3), 'checkpoint': True}
        if not lit:
            if 'lantern_off' in todo:
                render('lantern_off', objs, size, extra=dict(ex, lit=False))
        else:
            flick = [(1.00, 1.00, 0.00), (0.80, 0.86, 0.35), (1.12, 1.10, -0.30)]
            base = {fl.name: tuple(fl.scale) for fl in flames}
            for i, (pw, fs, sway) in enumerate(flick):
                nm = 'lantern_on_%02d' % i
                if nm not in todo:
                    continue
                for fl in flames:
                    bx, by, bz = base[fl.name]
                    fl.scale = (bx * fs, by * fs, bz * fs * (1.1 if fs < 1 else 1.0))
                    fl.rotation_euler = (0, sway * 0.4, 0)
                L.update()
                inner = lamp(pcw + V((0, 0, -0.02)), (1.0, 0.62, 0.22), 7.0 * pw, 0.03, True, 'pk_in')
                render(nm, objs + flames, size,
                       extra=dict(ex, lit=True, anim='flicker', frame=i, frames=3, ms=120))
                # the pool on the ground: a shadowless lamp at the pumpkin
                C.remove([inner])
                pool = lamp(pcw + V((0, 0, 0.05)), (1.0, 0.50, 0.14), 9.0 * pw, 0.1, False, 'pk_pool')
                glow_pass(nm, objs + flames, size, [nm], 1.2)
                META[nm]['files']['gl'] = nm + '_gl.png'
                C.remove([pool])
        remove(objs + flames + cutters + [root])


# ---------------------------------------------------------------------------
# the treasure chest
# ---------------------------------------------------------------------------

CH_W, CH_D, CH_H = 0.70, 0.48, 0.30
LID_R = CH_D / 2
LID_SQ = 0.72


def _half_cyl(name, w, r, sq, key, parent, x0=None, ring=24):
    """A half cylinder along X (the lid), its flat bottom at z = 0, spanning
    y in [-2r, 0] (hinge line at y = 0)."""
    x0 = -w / 2 if x0 is None else x0
    x1 = x0 + w
    verts = []
    for x in (x0, x1):
        for i in range(ring + 1):
            t = math.pi * i / ring
            verts.append((x, -r + r * math.cos(t), r * sq * math.sin(t)))
    n = ring + 1
    faces = []
    for i in range(ring):
        faces.append((i, i + 1, n + i + 1, n + i))
    faces.append(list(range(n)))
    faces.append(list(range(2 * n - 1, n - 1, -1)))
    faces.append((0, n, 2 * n - 1, n - 1))
    o = L.mesh_from(name, verts, faces, key, parent, sm=True)
    o.data.use_auto_smooth = True
    o.data.auto_smooth_angle = math.radians(35)
    bv = o.modifiers.new('bevel', 'BEVEL')
    bv.width = 0.012
    bv.segments = 2
    bv.limit_method = 'ANGLE'
    return o


def build_chest():
    root = L.empty('chest_root', A0)
    objs = []
    w, d, h = CH_W, CH_D, CH_H
    objs.append(L.rbox('ch_body', -w / 2, -d / 2, 0.0, w / 2, d / 2, h, 'chest_wood', 0.02, parent=root))
    # inside (visible when open): a dark rim frame and the treasure
    t = 0.035
    objs.append(L.rbox('ch_in', -w / 2 + t, -d / 2 + t, h - 0.01, w / 2 - t, d / 2 - t, h + 0.004, 'chest_inside', 0.0, parent=root))
    # gold bands and corner guards
    for sx in (-1, 1):
        x = sx * (w / 2 - 0.14)
        objs.append(L.rbox('ch_band', x - 0.035, -d / 2 - 0.012, -0.005, x + 0.035, d / 2 + 0.012, h + 0.005, 'gold_trim', 0.008, parent=root))
        for sy in (-1, 1):
            cx, cy = sx * w / 2, sy * d / 2
            objs.append(L.rbox('ch_corner', cx - 0.05, cy - 0.05, -0.008, cx + 0.05, cy + 0.05, 0.09, 'gold_trim', 0.012, parent=root))
    objs.append(L.rbox('ch_rimgold', -w / 2 - 0.012, -d / 2 - 0.012, h - 0.035, w / 2 + 0.012, d / 2 + 0.012, h, 'gold_trim', 0.008, parent=root))
    # the lock plate on the body front
    objs.append(L.rbox('ch_lock', -0.065, -d / 2 - 0.03, h - 0.15, 0.065, -d / 2 + 0.01, h - 0.01, 'gold_trim', 0.012, parent=root))
    objs.append(L.cylinder('ch_hole', 0.018, 0.03, 'iron', (0, -d / 2 - 0.028, h - 0.07), (math.pi / 2, 0, 0), root, 16))
    objs.append(L.rbox('ch_hole2', -0.009, -d / 2 - 0.043, h - 0.125, 0.009, -d / 2 - 0.02, h - 0.07, 'iron', 0.0, parent=root))
    # the lid on its hinge (back top edge)
    hinge = L.empty('ch_hinge', (0, d / 2, h), root)
    lid = _half_cyl('ch_lid', w, LID_R, LID_SQ, 'chest_wood_lid', hinge)
    objs.append(lid)
    lin_ = _half_cyl('ch_lid_in', w - 0.07, LID_R - 0.03, LID_SQ * 0.92, 'chest_inside', hinge, None, 16)
    lin_.location = (0, -0.03, -0.002)
    lin_.scale = (1, 1, -0.05)
    objs.append(lin_)
    for sx in (-1, 1):
        x = sx * (w / 2 - 0.14)
        b = _half_cyl('ch_lidband', 0.07, LID_R + 0.012, LID_SQ * (LID_R + 0.016) / (LID_R + 0.012), 'gold_trim', hinge, x - 0.035, 24)
        b.location = (0, 0.012, 0)
        objs.append(b)
    objs.append(L.rbox('ch_hasp', -0.045, -2 * LID_R - 0.025, -0.07, 0.045, -2 * LID_R + 0.01, 0.07, 'gold_trim', 0.01, parent=hinge))
    # the treasure (hidden under the closed lid)
    rnd = random.Random(3)
    tre = []
    tre.append(L.sphere('ch_pile', 1.0, 'gold_pile', (0, 0, h - 0.02), (w / 2 - 0.05, d / 2 - 0.05, 0.10), root, 32, 12))
    for i in range(9):
        x = rnd.uniform(-w / 2 + 0.1, w / 2 - 0.1)
        y = rnd.uniform(-d / 2 + 0.09, d / 2 - 0.09)
        zz = h - 0.02 + 0.10 * math.sqrt(max(0.0, 1 - (x / (w / 2 - 0.05)) ** 2 - (y / (d / 2 - 0.05)) ** 2)) + 0.004
        c = L.cylinder('ch_coin%d' % i, 0.048, 0.014, 'gold_coin', (x, y, zz), (rnd.uniform(-0.5, 0.5), rnd.uniform(-0.5, 0.5), 0), root, 20)
        tre.append(c)
    return root, hinge, objs, tre


CHEST_OPEN = [0.0, -38.0, -78.0, -108.0]


def g_chest():
    names = ['chest_%02d' % i for i in range(4)]
    todo = [n for n in names if want(n)]
    if not todo:
        return
    root, hinge, objs, tre = build_chest()
    sizes = []
    for ang in CHEST_OPEN:
        hinge.rotation_euler = (math.radians(ang), 0, 0)
        L.update()
        sizes.append(C.fit(objs + tre, A0, 3, 0.0))
    size = L.grow(L.size_union(*sizes, L.size_ground_disc(A0, 0.95)), 6)
    for i, nm in enumerate(names):
        if nm not in todo:
            continue
        hinge.rotation_euler = (math.radians(CHEST_OPEN[i]), 0, 0)
        L.update()
        lights = []
        k = [0.0, 0.25, 0.7, 1.0][i]
        if k > 0:
            lights.append(lamp(A0 + V((0, -0.02, CH_H + 0.16)), (1.0, 0.78, 0.30), 10.0 * k, 0.12, True, 'ch_glow'))
        render(nm, objs + tre, size, extra={'anim': 'open', 'frame': i, 'frames': 4, 'ms': 80,
                                            'height_m': 0.66, 'footprint': [1, 1]})
        if k > 0:
            s = L.Sprite2D(OUT, nm)
            # the gold light spilling out of the opening
            top = s.px(A0 + V((0, 0, CH_H + 0.06)))
            h_, w_ = s.a.shape
            yy, xx = [q.astype(np.float32) for q in np.mgrid[0:h_, 0:w_]]
            src = np.exp(-(((xx - top[0]) / 16.0) ** 2 + ((yy - top[1] + 4) / 9.0) ** 2)) * k
            s.halo(GOLD_HALO, sigma=3.5, gain=1.2, amax=0.5 * k, z=s.zcode(A0 + V((0, 0, CH_H + 0.1))), src=np.maximum(src, 0))
            for (x, y), ln in zip(s.hot(2, min_sep=8), (6, 4)):
                s.glint(x, y, ln * k, 0.9 * k, z=s.zcode(A0 + V((0, -0.2, CH_H + 0.1))))
            s.save()
            remove(lights)
            lights = [lamp(A0 + V((0, -0.05, CH_H + 0.35)), (1.0, 0.76, 0.28), 5.0 * k, 0.2, False, 'ch_pool')]
            glow_pass(nm, objs + tre, size, [nm], 0.9)
            META[nm]['files']['gl'] = nm + '_gl.png'
        remove(lights)
    remove(objs + tre + [hinge, root])


# ---------------------------------------------------------------------------
# effects
# ---------------------------------------------------------------------------

import numpy as np   # noqa: E402


def astroid(R, p=2.6, n=48, squash=1.0):
    pts = []
    for i in range(n):
        t = 2 * math.pi * i / n
        c, s = math.cos(t), math.sin(t)
        pts.append((R * math.copysign(abs(c) ** p, c), R * squash * math.copysign(abs(s) ** p, s)))
    return pts


def fx_group(stem, n, ms, frame_fn, extra=None, post=None):
    names = ['%s_%02d' % (stem, f) for f in range(n)]
    todo = [nm for nm in names if want(nm)]
    if not todo:
        return
    frames = [frame_fn(f) for f in range(n)]
    L.update()
    size = L.grow(L.size_union(*[C.fit(fr, A0, 3) for fr in frames]), 6)
    for f, nm in enumerate(names):
        if nm not in todo:
            continue
        ex = {'anim': stem[3:], 'frame': f, 'frames': n, 'ms': ms, 'fx': True}
        ex.update(extra or {})
        render(nm, frames[f], size, passes=('color', 'z'), extra=ex)
        if post:
            post(nm, f)
    for fr in frames:
        remove(fr)


POOF_Z = 0.45


def burst(R, r, n=9, rot=0.0):
    """A cartoon 'pow' starburst outline (n spikes)."""
    pts = []
    for i in range(2 * n):
        t = math.pi / 2 + rot + i * math.pi / n
        rr = R if i % 2 == 0 else r
        pts.append((rr * math.cos(t), rr * math.sin(t) * 0.9))
    return pts


def poof_frame(f):
    rnd = random.Random(11)
    objs = []
    # (cluster radius, puff radius, rise)
    spec = [(0.06, 0.10, 0.00), (0.20, 0.16, 0.02), (0.30, 0.20, 0.05), (0.40, 0.19, 0.09),
            (0.46, 0.15, 0.14), (0.52, 0.10, 0.19)][f]
    cr, pr, rise = spec
    c = A0 + V((0, 0, POOF_Z + rise))
    npuff = 10
    for i in range(npuff):
        # directions spread on a sphere, flattened a little
        t = 2 * math.pi * (i + rnd.uniform(-0.2, 0.2)) / npuff
        el = rnd.uniform(-0.55, 0.75)
        d = V((math.cos(t) * math.cos(el), math.sin(t) * math.cos(el) * 0.8, math.sin(el) * 0.85))
        r = pr * rnd.uniform(0.8, 1.2)
        objs.append(L.sphere('poof%d' % i, r, 'smoke', c + d * cr, seg=24, rings=12))
    if f <= 3:
        objs.append(L.sphere('poof_c', pr * 1.25 * (1.0 if f < 3 else 0.7), 'smoke', c, seg=24, rings=12))
    if f == 0:
        objs.append(L.billboard('poof_flash', burst(0.34, 0.17, 9), c, 'smoke_flash', -0.3))
    if f == 1:
        objs.append(L.billboard('poof_flash', burst(0.40, 0.26, 9, 0.3), c, 'smoke_flash', -0.4))
    return objs


def poof_post(nm, f):
    k = [1.0, 1.0, 1.0, 0.95, 0.72, 0.42][f]
    s = L.Sprite2D(OUT, nm)
    if k < 1:
        s.fade(k)
    s.save()


SPARK_Z = KEY_Z


def sparkle_frame(f):
    rnd = random.Random(5)
    objs = []
    c = A0 + V((0, 0, SPARK_Z))
    # 9 stars flying out and up
    rad = [0.06, 0.20, 0.34, 0.46, 0.56, 0.64][f]
    up = [0.0, 0.04, 0.10, 0.17, 0.25, 0.33][f]
    size = [0.05, 0.12, 0.115, 0.10, 0.075, 0.045][f]
    for i in range(9):
        ang = math.radians(90 + (i - 4) * 36 + rnd.uniform(-10, 10))
        sp = rnd.uniform(0.75, 1.15)
        x = math.cos(ang) * rad * sp * 1.25
        y = math.sin(ang) * rad * sp * 0.9 + up
        s = size * rnd.uniform(0.75, 1.25)
        tw = rnd.random()
        key = 'spark_gold' if i % 3 else 'spark_pale'
        p = c + C.R * x + C.UP * y
        objs.append(L.billboard('spk%d' % i, astroid(s, 2.7, 40), p, key, 0.02 * i))
        if s > 0.06:
            objs.append(L.billboard('spkw%d' % i, astroid(s * 0.45, 2.3, 32), p, 'spark_white', 0.02 * i + 0.01))
    # little round glitter
    if 1 <= f <= 4:
        for i in range(7):
            ang = rnd.uniform(0, 2 * math.pi)
            rr = rad * rnd.uniform(0.35, 0.8)
            p = c + C.R * (math.cos(ang) * rr) + C.UP * (math.sin(ang) * rr * 0.8 + up * 0.6)
            objs.append(L.billboard('dot%d' % i, L.circle_outline(0.018, 12), p, 'spark_pale', 0.3))
    # the burst in the middle
    if f <= 1:
        R0 = [0.26, 0.18][f]
        objs.append(L.billboard('spk_c', astroid(R0, 3.0, 64), c, 'spark_gold', 0.4))
        objs.append(L.billboard('spk_cw', astroid(R0 * 0.55, 2.6, 48), c, 'spark_white', 0.42))
    return objs


def sparkle_post(nm, f):
    s = L.Sprite2D(OUT, nm)
    k = [1.0, 1.0, 1.0, 0.95, 0.8, 0.55][f]
    s.halo(GOLD_HALO, sigma=2.2, gain=1.1, amax=0.42 * k)
    if k < 1:
        s.fade(k)
    s.save()


def g_fx():
    fx_group('fx_poof', 6, 60, poof_frame, post=poof_post, extra={'height_m': 1.0})
    fx_group('fx_sparkle', 6, 50, sparkle_frame, post=sparkle_post, extra={'height_m': 1.0})


# ---------------------------------------------------------------------------
# more pickups: hourglass, sticker
# ---------------------------------------------------------------------------

def _glass(nt, neutral):
    N, Lk = nt.nodes, nt.links
    p = N.new('ShaderNodeBsdfPrincipled')
    p.inputs['Base Color'].default_value = L.hexlin('#dff6ff') + (1,)
    p.inputs['Roughness'].default_value = 0.05
    p.inputs['Specular'].default_value = 1.0
    p.inputs['Emission'].default_value = L.hexlin('#bfe8ff') + (1,)
    p.inputs['Emission Strength'].default_value = 0.15
    lw = N.new('ShaderNodeLayerWeight')
    lw.inputs['Blend'].default_value = 0.35
    mr = N.new('ShaderNodeMapRange')
    mr.inputs['To Min'].default_value = 0.30
    mr.inputs['To Max'].default_value = 0.95
    Lk.new(lw.outputs['Facing'], mr.inputs['Value'])
    tr = N.new('ShaderNodeBsdfTransparent')
    mx = N.new('ShaderNodeMixShader')
    Lk.new(mr.outputs['Result'], mx.inputs['Fac'])
    Lk.new(tr.outputs['BSDF'], mx.inputs[1])
    Lk.new(p.outputs['BSDF'], mx.inputs[2])
    return mx.outputs['Shader']


C.mat('glass', build=_glass)
C.mat('sand_blue', build=L.gloss_build('#3a9cff', emit_col='#2a8cff', emit=0.45, rough=0.5, coat=0.2))
C.mat('holo', build=L.holo_build(0.75, 0.5))
C.mat('card_white', build=L.gloss_build('#f6f4ff', emit_col='#ffffff', emit=0.2, rough=0.3, coat=0.5))
C.mat('steel', build=L.gold_build(emit=0.35, base='#c8ccd4', rough=0.25, metal=0.8, rim=0.6, env=L.SILVER_ENV))
C.mat('lever_stone', base=L.hexlin('#8a8790'), rough=0.85)
C.mat('lever_stone_dk', base=L.hexlin('#5c5a64'), rough=0.85)
C.mat('knob_red', build=L.gloss_build('#e8222e', emit_col='#ff2030', emit=0.25, rough=0.15, coat=1.0))

HOUR_Z = 0.40
HOUR_ZS = 1.10
HOUR_PTS = {'top_f': (-0.06, -0.08, 0.16), 'top_b': (0.06, 0.08, 0.16), 'glass': (-0.05, -0.06, 0.08)}
HOUR_GLINTS = {0: [('glass', 5, 0.9)], 2: [('top_f', 3, 0.7)], 4: [('glass', 5, 0.9)], 6: [('top_b', 3, 0.7)]}


def build_hourglass(root):
    o = []
    o.append(L.lathe('hg_glass', [(0, -0.14), (0.055, -0.14), (0.092, -0.11), (0.095, -0.07), (0.07, -0.03),
                                   (0.02, -0.004), (0.02, 0.004), (0.07, 0.03), (0.095, 0.07), (0.092, 0.11),
                                   (0.055, 0.14), (0, 0.14)], 'glass', 32, root))
    o.append(L.lathe('hg_sand_b', [(0, -0.135), (0.082, -0.135), (0.084, -0.10), (0.05, -0.07), (0.012, -0.055),
                                    (0, -0.054)], 'sand_blue', 24, root))
    o.append(L.lathe('hg_sand_t', [(0, 0.03), (0.04, 0.045), (0.075, 0.075), (0.085, 0.095), (0, 0.095)],
                     'sand_blue', 24, root))
    o.append(L.cylinder('hg_stream', 0.008, 0.09, 'sand_blue', (0, 0, -0.02), parent=root, verts=8))
    for z in (-0.155, 0.155):
        o.append(L.cylinder('hg_cap', 0.125, 0.032, 'gold', (0, 0, z), parent=root, verts=40))
        o.append(L.torus('hg_caprim', 0.122, 0.014, 'gold', (0, 0, z), parent=root, seg=(40, 8)))
    for i in range(3):
        t = math.radians(90 + 120 * i)
        o.append(L.cylinder('hg_post', 0.013, 0.30, 'gold', (0.108 * math.cos(t), 0.108 * math.sin(t), 0),
                            parent=root, verts=10))
    return o


STICK_Z = 0.42
STICK_ZS = 1.15
STICK_PTS = {'corner_f': (-0.09, -0.012, 0.14), 'corner_b': (0.09, 0.012, 0.14), 'star': (0.0, -0.03, 0.02)}
STICK_GLINTS = {0: [('corner_f', 7, 1.0)], 1: [('star', 3, 0.7)], 3: [('corner_b', 4, 0.8)],
                4: [('corner_b', 7, 1.0)], 5: [('star', 3, 0.7)], 7: [('corner_f', 4, 0.8)]}


def build_sticker(root):
    o = []
    o.append(L.pillow('stk_card', L.rrect_outline(0.27, 0.36, 0.045), 0.007, 'holo', rings=3, q=1.0, parent=root))
    o.append(L.pillow('stk_rim', L.rrect_outline(0.305, 0.395, 0.055), 0.0045, 'card_white', rings=2, q=1.0,
                      parent=root))
    for sgn in (-1, 1):
        st = L.pillow('stk_star', L.star_outline(0.088, 0.04, 5, 3), 0.012, 'gold_star', rings=5, q=0.7,
                      back=False, parent=root)
        if sgn > 0:
            st.rotation_euler = (0, 0, math.pi)
        st.location = (0, sgn * 0.006, 0.0)
        o.append(st)
    return o


def g_pickups2():
    spin_group('hourglass', build_hourglass, HOUR_Z, 8, 80, HOUR_GLINTS, HOUR_PTS, HOUR_ZS,
               halo_kw=dict(halo=(0.62, 0.86, 1.0), sigma=2.4, gain=1.3, amax=0.42), extra={'height_m': 0.62})
    spin_group('sticker', build_sticker, STICK_Z, 8, 80, STICK_GLINTS, STICK_PTS, STICK_ZS,
               halo_kw=dict(halo=(1.0, 0.92, 1.0), sigma=2.2, gain=1.1, amax=0.35),
               extra={'height_m': 0.64, 'collectible': True})


# ---------------------------------------------------------------------------
# the lever
# ---------------------------------------------------------------------------

LEVER_ANG = [-38.0, 0.0, 38.0]      # 00 left, 01 middle, 02 right


def g_lever():
    names = ['lever_%02d' % i for i in range(3)]
    todo = [n for n in names if want(n)]
    if not todo:
        return
    root = L.empty('lever_root', A0)
    objs = []
    objs.append(L.rbox('lv_base', -0.21, -0.15, 0.0, 0.21, 0.15, 0.17, 'lever_stone', 0.03, parent=root))
    objs.append(L.rbox('lv_foot', -0.25, -0.19, 0.0, 0.25, 0.19, 0.05, 'lever_stone_dk', 0.02, parent=root))
    objs.append(L.rbox('lv_slot', -0.15, -0.035, 0.16, 0.15, 0.035, 0.178, 'iron', 0.008, parent=root))
    objs.append(L.rbox('lv_plate', -0.10, -0.158, 0.05, 0.10, -0.14, 0.13, 'gold_trim', 0.008, parent=root))
    for x in (-0.07, 0.07):
        objs.append(L.sphere('lv_rivet', 0.012, 'gold_trim', (x, -0.162, 0.09), parent=root, seg=10, rings=5))
    objs.append(L.cylinder('lv_axle', 0.03, 0.10, 'steel', (0, 0, 0.18), (math.pi / 2, 0, 0), root, 16))
    piv = L.empty('lv_pivot', (0, 0, 0.18), root)
    objs.append(L.cylinder('lv_rod', 0.022, 0.36, 'steel', (0, 0, 0.18), parent=piv, verts=14))
    objs.append(L.sphere('lv_knob', 0.06, 'knob_red', (0, 0, 0.38), parent=piv, seg=24, rings=12))
    sizes = []
    for ang in LEVER_ANG:
        piv.rotation_euler = (0, math.radians(ang), 0)
        L.update()
        sizes.append(C.fit(objs, A0, 3, 0.0))
    size = L.size_union(*sizes)
    for i, nm in enumerate(names):
        if nm not in todo:
            continue
        piv.rotation_euler = (0, math.radians(LEVER_ANG[i]), 0)
        L.update()
        render(nm, objs, size, extra={'state': ['left', 'middle', 'right'][i], 'frame': i, 'frames': 3,
                                      'height_m': 0.62})
    remove(objs + [piv, root])


# ---------------------------------------------------------------------------
# more effects: dust, splash, bubbles
# ---------------------------------------------------------------------------

C.mat('dust', build=L.toon_build('#e6dac2', '#a8987a', emit=0.32, rough=0.9))
C.mat('splash', build=L.toon_build('#f0fdff', '#5ec6d6', emit=0.42, rough=0.6))
C.mat('bubble', build=L.gloss_build('#c8f2ff', emit_col='#bff0ff', emit=0.35, rough=0.08, coat=1.0, spec=0.9))
C.mat('bubble_hi', build=L.emit_build('#ffffff', 1.0))


def dust_frame(f):
    rnd = random.Random(17)
    rr = [0.12, 0.22, 0.32, 0.40, 0.46][f]
    pr = [0.085, 0.10, 0.095, 0.075, 0.055][f]
    o = []
    n = 11
    for i in range(n):
        t = 2 * math.pi * (i + rnd.uniform(-0.25, 0.25)) / n
        r = pr * rnd.uniform(0.75, 1.25)
        d = rr * rnd.uniform(0.85, 1.1)
        o.append(L.sphere('dust%d' % i, r, 'dust', A0 + V((math.cos(t) * d, math.sin(t) * d * 0.9, r * 0.55)),
                          (1.0, 1.0, 0.65), None, 16, 8))
    if f < 2:
        for i in range(3):
            t = i * 2.1
            o.append(L.sphere('dustc', pr * 0.9, 'dust', A0 + V((0.05 * math.cos(t), 0.05 * math.sin(t), pr * 0.5)),
                              (1.0, 1.0, 0.6), None, 16, 8))
    return o


def dust_post(nm, f):
    s = L.Sprite2D(OUT, nm)
    s.fade([0.85, 0.8, 0.66, 0.46, 0.26][f])
    s.save()


def splash_frame(f):
    rnd = random.Random(23)
    o = []
    rc = [0.10, 0.16, 0.22, 0.26, 0.29, 0.31][f]
    hc = [0.20, 0.34, 0.30, 0.13, 0.0, 0.0][f]
    if hc > 0:
        for i in range(10):
            t = 2 * math.pi * (i + 0.5 * (i % 2)) / 10
            h = hc * rnd.uniform(0.75, 1.2)
            c = L.cone('spl_crown', 0.055, 0.0, h, 'splash', A0 + V((math.cos(t) * rc, math.sin(t) * rc, h / 2)),
                       (0, 0, 0), None, 10)
            c.rotation_euler = (-math.sin(t) * 0.45, math.cos(t) * 0.45, 0)
            o.append(c)
            if h > 0.12:
                o.append(L.sphere('spl_tip', 0.03, 'splash', A0 + V((math.cos(t) * (rc + h * 0.43), math.sin(t) * (rc + h * 0.43), h * 0.93)),
                                  seg=10, rings=5))
    col = [0.30, 0.46, 0.34, 0.12, 0.0, 0.0][f]
    if col > 0:
        o.append(L.sphere('spl_col', 0.07, 'splash', A0 + V((0, 0, col / 2)), (1, 1, col / 0.14), None, 16, 8))
        o.append(L.sphere('spl_colt', 0.05, 'splash', A0 + V((0, 0, col + 0.03)), seg=12, rings=6))
    if f >= 1:
        dd = [0, 0.15, 0.28, 0.40, 0.50, 0.58][f]
        zz = [0, 0.42, 0.55, 0.50, 0.34, 0.12][f]
        for i in range(9):
            t = 2 * math.pi * i / 9 + 0.3
            k = rnd.uniform(0.8, 1.2)
            o.append(L.sphere('spl_drop', 0.034 * (1.0 if f < 5 else 0.7), 'splash',
                              A0 + V((math.cos(t) * dd * k, math.sin(t) * dd * k, zz * rnd.uniform(0.75, 1.15))),
                              seg=10, rings=5))
    rp = [0.12, 0.20, 0.30, 0.40, 0.48, 0.55][f]
    ri = L.torus('spl_ring', rp, 0.028, 'splash', A0 + V((0, 0, 0.0)), parent=None, seg=(40, 8))
    ri.scale = (1, 1, 0.5)
    o.append(ri)
    return o


def splash_post(nm, f):
    s = L.Sprite2D(OUT, nm)
    s.fade([1.0, 1.0, 0.95, 0.85, 0.66, 0.42][f])
    s.save()


BUBBLES = [(-0.18, -0.10, 0.075, 0.00), (0.14, -0.14, 0.060, 0.30), (0.02, 0.12, 0.090, 0.55),
           (0.24, 0.10, 0.050, 0.80), (-0.22, 0.16, 0.055, 0.40)]


def bubbles_frame(f):
    o = []
    for i, (x, y, rmax, ph0) in enumerate(BUBBLES):
        ph = (ph0 + f / 4.0) % 1.0
        c = A0 + V((x, y, 0))
        if ph < 0.75:
            r = rmax * (0.35 + 0.65 * ph / 0.75)
            o.append(L.sphere('bub%d' % i, r, 'bubble', c + V((0, 0, -r * 0.25)), seg=20, rings=10))
            o.append(L.sphere('bubh%d' % i, r * 0.22, 'bubble_hi', c + V((-r * 0.35, -r * 0.45, r * 0.45)),
                              seg=8, rings=4))
        else:
            ri = L.torus('bubr%d' % i, rmax * 1.15, 0.012, 'bubble', c + V((0, 0, 0.005)), seg=(24, 6))
            ri.scale = (1, 1, 0.5)
            o.append(ri)
            for k in range(4):
                t = k * math.pi / 2 + 0.4
                o.append(L.sphere('bubd%d' % i, 0.014, 'bubble', c + V((math.cos(t) * rmax, math.sin(t) * rmax, rmax * 0.8)),
                                  seg=8, rings=4))
    return o


def bubbles_post(nm, f):
    s = L.Sprite2D(OUT, nm)
    s.fade(0.85)
    s.save()


def g_fx2():
    fx_group('fx_dust', 5, 40, dust_frame, post=dust_post, extra={'height_m': 0.2})
    fx_group('fx_splash', 6, 50, splash_frame, post=splash_post, extra={'height_m': 0.6, 'on': 'surface'})
    fx_group('fx_bubbles', 4, 120, bubbles_frame, post=bubbles_post, extra={'height_m': 0.2, 'on': 'surface'})


# ---------------------------------------------------------------------------
# UI icons (96 x 96, their own camera): trophies and the medal
# ---------------------------------------------------------------------------

ICON = 96
C.mat('silver', build=L.gold_build(emit=0.45, base='#d0d6e0', rough=0.18, metal=0.8, rim=0.6, env=L.SILVER_ENV))
C.mat('silver_star', build=L.gold_build(emit=0.8, base='#eef2f8', rough=0.15, metal=0.6, rim=0.3, env=L.SILVER_ENV))
C.mat('bronze', build=L.gold_build(emit=0.45, base='#c47a3c', rough=0.22, metal=0.8, rim=0.6, env=L.BRONZE_ENV))
C.mat('bronze_star', build=L.gold_build(emit=0.8, base='#f0a868', rough=0.18, metal=0.6, rim=0.3, env=L.BRONZE_ENV))
C.mat('plinth', build=L.gloss_build('#3a2458', emit_col='#3a2458', emit=0.12, rough=0.3, coat=0.8))
C.mat('plinth_dk', build=L.gloss_build('#24163a', emit_col='#24163a', emit=0.1, rough=0.3, coat=0.8))
C.mat('ribbon_r', build=L.gloss_build('#e8283a', emit_col='#e8283a', emit=0.2, rough=0.5, coat=0.2))
C.mat('ribbon_b', build=L.gloss_build('#2e62e8', emit_col='#2e62e8', emit=0.2, rough=0.5, coat=0.2))
C.mat('ribbon_w', build=L.gloss_build('#f4f4ff', emit_col='#ffffff', emit=0.2, rough=0.5, coat=0.2))

METALS = {'gold': ('gold', 'gold_star'), 'silver': ('silver', 'silver_star'), 'bronze': ('bronze', 'bronze_star')}


def build_trophy(metal):
    mk, sk = METALS[metal]
    o = []
    o.append(L.rbox('tr_base', -0.27, -0.19, -0.50, 0.27, 0.19, -0.33, 'plinth', 0.025))
    o.append(L.rbox('tr_base2', -0.20, -0.14, -0.33, 0.20, 0.14, -0.23, 'plinth_dk', 0.02))
    o.append(L.rbox('tr_plate', -0.13, -0.20, -0.465, 0.13, -0.185, -0.365, mk, 0.008))
    o.append(L.lathe('tr_cup', [(0, -0.23), (0.15, -0.23), (0.15, -0.20), (0.07, -0.17), (0.045, -0.10),
                                (0.075, -0.065), (0.045, -0.03), (0.06, 0.02), (0.19, 0.09), (0.255, 0.20),
                                (0.275, 0.33), (0.285, 0.39), (0.26, 0.40), (0.235, 0.27), (0.12, 0.21),
                                (0, 0.20)], mk, 48))
    for sx in (-1, 1):
        o.append(L.torus('tr_handle', 0.10, 0.028, mk, (sx * 0.27, 0, 0.23), (math.pi / 2, 0, 0), seg=(32, 10)))
    st = L.pillow('tr_star', L.star_outline(0.09, 0.04, 5, 3), 0.018, sk, rings=5, q=0.7, back=False)
    st.location = (0, -0.262, 0.22)
    o.append(st)
    ps = L.pillow('tr_pstar', L.star_outline(0.035, 0.016, 5, 2), 0.006, sk, rings=3, q=0.7, back=False)
    ps.location = (0, -0.20, -0.415)
    o.append(ps)
    return o


def build_medal():
    o = []
    d = L.cylinder('md_disc', 0.25, 0.05, 'gold', (0, 0, -0.18), (math.pi / 2, 0, 0), None, 48)
    bv = d.modifiers.new('bevel', 'BEVEL')
    bv.width = 0.015
    bv.segments = 3
    bv.limit_method = 'ANGLE'
    o.append(d)
    o.append(L.torus('md_rim', 0.215, 0.018, 'gold', (0, -0.025, -0.18), (math.pi / 2, 0, 0), seg=(48, 10)))
    st = L.pillow('md_star', L.star_outline(0.14, 0.062, 5, 4), 0.025, 'gold_star', rings=6, q=0.7, back=False)
    st.location = (0, -0.024, -0.18)
    o.append(st)
    o.append(L.torus('md_loop', 0.05, 0.016, 'gold', (0, 0, 0.09), (0, math.pi / 2, 0), seg=(24, 8)))
    for sx, key in ((-1, 'ribbon_r'), (1, 'ribbon_b')):
        rb = L.centered(L.rbox('md_rib', -0.08, 0.01, 0.10, 0.08, 0.03, 0.62, key, 0.01))
        rb.rotation_euler = (0, sx * math.radians(24), 0)
        rb.location = (sx * 0.12, rb.location.y + (0.0 if sx < 0 else 0.012), rb.location.z)
        o.append(rb)
        wb = L.centered(L.rbox('md_ribw', -0.022, 0.0, 0.10, 0.022, 0.012, 0.62, 'ribbon_w', 0.004))
        wb.rotation_euler = rb.rotation_euler
        wb.location = rb.location + V((0, -0.002, 0))
        o.append(wb)
    return o


def icon_post(nm):
    s = L.Sprite2D(OUT, nm)
    for (x, y), ln in zip(s.hot(2, min_sep=14), (7, 4)):
        s.glint(x, y, ln, 0.95)
    s.save()


def g_icons():
    todo = [n for n in ('trophy_gold', 'trophy_silver', 'trophy_bronze', 'medal') if want(n)]
    if not todo:
        return
    key = C.add_point_light((-1.4, -2.4, 1.8), (1.0, 0.96, 0.9), 160.0, 0.6, 'icon_key')
    for nm in todo:
        objs = build_medal() if nm == 'medal' else build_trophy(nm.split('_')[1])
        L.update()
        L.camera((0, 0, -0.03), (0.0, 1.0, -0.16), ortho=1.08)
        L.picture(OUT, nm, ICON, ICON, objs, a.samples, extra={'what': nm}, kind='icon')
        icon_post(nm)
        remove(objs)
    remove([key])


# ---------------------------------------------------------------------------

g_pickups()
g_pickups2()
g_lever()
g_lantern()
g_chest()
g_fx()
g_fx2()
g_icons()
C.save_meta(OUT)
print('[objects] done: %d sprites in %.1fs' % (len(META), time.time() - T0))
