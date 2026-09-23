"""Mila - the kitten's sprites (SPEC.md section 4) -> assets/mila/.

    /Applications/Blender.app/Contents/MacOS/Blender -b -P mila.py -- --out ../../assets/mila [--set all|sample]
        [--anims idle,c_sit] [--only mila_idle_s_00,...] [--items hat_bow,...] [--part 0/2] [--floor]

--set all      every frame of SPEC section 4 with the 14 items on each (default)
--set sample   phase 1: the frames and layers listed in SAMPLE
--anims a,b    only these animations; --only a,b only these body frames
--items a,b    only these layers (default: all 14)
--part i/N     render every N-th job into <out>/_part<i> (run N at once, then
               `python3 mila_merge.py <out>` merges them)
--floor        also the helper floor tile / ball for the previews (into <out>/_floor)

Body frames: color + z + shadow under the neutral light (she is always
black). Layers (hats, neck items): light + id + z, with her body as hold-out.
The model and the poses are in mila_rig.py and mila_poses.py.
"""
import json
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ml_common as C          # noqa: E402
import mila_rig as R           # noqa: E402
import mila_poses as MP        # noqa: E402

SAMPLES = 64

SAMPLE = [('idle', d, 0) for d in 'nesw'] + [('walk', 'e', k) for k in range(6)] + \
    [('push', 'n', 0), ('push', 'e', 2), ('win', 's', 4),
     ('c_sit', 's', 0), ('c_sleep', 's', 0), ('c_belly', 's', 0), ('c_meow', 's', 1)]
SAMPLE_ITEMS = ['hat_beret', 'hat_bunny', 'neck_bandana', 'neck_bell']
SAMPLE_LAYERED = {('idle', 's', 0), ('walk', 'e', 3), ('c_sit', 's', 0)}

PALETTES = {
    'hat_bow': {'1': [245, 120, 170], '2': [220, 80, 140]},
    'hat_party': {'1': [90, 215, 165], '2': [250, 250, 250], '3': [255, 120, 160]},
    'hat_crown': {'1': [255, 196, 60], '3': [230, 40, 95]},
    'hat_beret': {'1': [205, 45, 65], '2': [150, 28, 45]},
    'hat_beanie': {'1': [95, 150, 235], '2': [245, 245, 245]},
    'hat_flower': {'1': [255, 150, 195], '2': [255, 210, 70], '3': [95, 190, 95]},
    'hat_bunny': {'1': [245, 245, 250], '2': [255, 165, 195]},
    'hat_witch': {'1': [120, 72, 195], '2': [255, 150, 60], '3': [255, 215, 80]},
    'neck_bell': {'1': [225, 45, 55], '3': [255, 200, 70]},
    'neck_fish': {'1': [60, 125, 235], '3': [215, 225, 235]},
    'neck_pearls': {'1': [248, 242, 232]},
    'neck_bandana': {'1': [230, 60, 75]},
    'neck_dots': {'1': [70, 130, 230], '2': [255, 255, 255]},
    'neck_bow': {'1': [235, 55, 85], '2': [190, 35, 65]},
}
DARK = [48, 36, 46]


def fade_border(path, edge=3):
    """ml_common removes the shadow haze; the shadow still must reach 0 before
    the sprite's border (no hard cut on the floor): fade the last pixels."""
    import numpy as np
    import bpy
    img = bpy.data.images.load(path, check_existing=False)
    w, h = img.size
    a = np.empty(w * h * 4, np.float32)
    img.pixels.foreach_get(a)
    bpy.data.images.remove(img)
    sh = a.reshape(h, w, 4)[::-1, :, 0] * 255.0
    yy, xx = np.mgrid[0:h, 0:w]
    d = np.minimum(np.minimum(xx, w - 1 - xx), np.minimum(yy, h - 1 - yy)).astype(np.float32)
    sh *= np.clip(d / edge, 0, 1)
    C.write_png(path, np.round(sh).astype(np.uint8))


def frame_name(anim, d, f):
    if anim == 'turn':
        return 'mila_turn_%02d' % f
    return 'mila_%s_%s_%02d' % (anim, d, f)


ALL_ITEMS = ['hat_bow', 'hat_party', 'hat_crown', 'hat_beret', 'hat_beanie', 'hat_flower', 'hat_bunny', 'hat_witch',
             'neck_bell', 'neck_fish', 'neck_pearls', 'neck_bandana', 'neck_dots', 'neck_bow']


def all_jobs():
    jobs = []
    for anim, (dirs, n, ms, zoom, fn) in MP.ANIMS.items():
        for d in dirs:
            for f in range(n):
                jobs.append((anim, d, f))
    return jobs


def render_frame(out, anim, d, f, items=(), log=None):
    dirs, n, ms, zoom, fn = MP.ANIMS[anim]
    zoom = float(os.environ.get('MILA_ZOOM', zoom))     # debugging close-ups only
    t0 = time.time()
    C.reset('neutral')
    R.register_materials()
    P = MP.pose(anim, f, d)
    A = C.cell(0, 0, 0)
    yaw = 30.0 * f if anim == 'turn' else R.FACING[d]
    voxel = 0.0045 if zoom > 2 else R.VOXEL
    rig = R.build(P, items=items, anchor=A, yaw=yaw, voxel=voxel)
    t1 = time.time()
    name = frame_name(anim, d, f)
    extra = dict(anim=anim, dir=d, frame=f, frames=n, ms=ms)
    if anim == 'turn':
        extra = dict(anim='turn', frame=f, frames=n, yaw=yaw)
    C.render_sprite(out, name, rig['body'], A, passes=('color', 'z', 'shadow'), shadow_z=0.0,
                    bounce_ground=0.0, kind='char', samples=SAMPLES, zoom=zoom, extra=extra, margin=6)
    fade_border(os.path.join(out, name + '_sh.png'))
    t2 = time.time()
    tl = []
    for it in items:
        lname = '%s_%s' % (it, name[len('mila_'):])
        C.render_sprite(out, lname, rig['layers'][it], A, passes=('light', 'id', 'z'), holdout=rig['body'],
                        kind='layer', samples=SAMPLES, zoom=zoom, extra=dict(extra, item=it, of=name))
        tl.append(time.time())
    C.save_meta(out)
    msg = '%s: build %.1fs, body %.1fs, %d layers %.1fs' % (name, t1 - t0, t2 - t1, len(items),
                                                           (tl[-1] - t2) if tl else 0.0)
    print('[mila]', msg)
    if log is not None:
        log.append(dict(name=name, build=round(t1 - t0, 2), body=round(t2 - t1, 2),
                        layers=round((tl[-1] - t2) if tl else 0.0, 2), n_layers=len(items)))


def floor(out):
    """A neutral wooden floor tile (game and casita scale) and a ball the
    size of a pushable thing, for the previews only."""
    d = os.path.join(out, '_floor')
    for zoom, nm in ((1.0, 'mila_floor'), (MP.CASITA, 'mila_floor_c')):
        C.reset('living')

        def wood(col):
            def b(nt, neutral):
                p = nt.nodes.new('ShaderNodeBsdfPrincipled')
                p.inputs['Roughness'].default_value = 0.55
                p.inputs['Specular'].default_value = 0.35
                nz = nt.nodes.new('ShaderNodeTexNoise')
                nz.inputs['Scale'].default_value = 2.0
                nz.inputs['Detail'].default_value = 3.0
                tc = nt.nodes.new('ShaderNodeTexCoord')
                mp = nt.nodes.new('ShaderNodeMapping')
                mp.inputs['Scale'].default_value = (1.0, 10.0, 1.0)
                nt.links.new(tc.outputs['Object'], mp.inputs['Vector'])
                nt.links.new(mp.outputs['Vector'], nz.inputs['Vector'])
                ramp = nt.nodes.new('ShaderNodeValToRGB')
                ramp.color_ramp.elements[0].color = (col[0] * 0.8, col[1] * 0.8, col[2] * 0.8, 1)
                ramp.color_ramp.elements[1].color = tuple(col) + (1,)
                ramp.color_ramp.elements[0].position = 0.35
                ramp.color_ramp.elements[1].position = 0.65
                nt.links.new(nz.outputs['Fac'], ramp.inputs['Fac'])
                nt.links.new(ramp.outputs['Color'], p.inputs['Base Color'])
                return p.outputs['BSDF']
            return b
        C.mat('under', base=(0.30, 0.19, 0.11), rough=0.9)
        obs = [C.box('u', 0, 0, -0.06, 1, 1, -0.012, 'under')]
        for k, sh in enumerate((1.0, 0.94, 1.05, 0.97)):
            key = C.mat('plank%d' % k, build=wood((0.80 * sh, 0.56 * sh, 0.34 * sh)))
            obs.append(C.box('p%d' % k, 0.003, k * 0.25 + 0.006, -0.03, 0.997, (k + 1) * 0.25 - 0.006, 0.0, key,
                             bevel=0.004))
        C.render_sprite(d, nm, obs, C.cell(0, 0, 0), passes=('color', 'z'), kind='tile', zoom=zoom, samples=32)
        C.save_meta(d)
    C.reset('living')
    import bpy
    import bmesh
    bm = bmesh.new()
    bmesh.ops.create_uvsphere(bm, u_segments=32, v_segments=20, radius=0.30)
    me = bpy.data.meshes.new('ball')
    bm.to_mesh(me)
    bm.free()
    ob = bpy.data.objects.new('ball', me)
    C.link(ob)
    for p in me.polygons:
        p.use_smooth = True
    ob.location = (0.5, 0.5, 0.30)
    C.mat('ball', base=(0.95, 0.40, 0.60), rough=0.8)
    C.assign(ob, 'ball')
    C.render_sprite(d, 'mila_ball', [ob], C.cell(0, 0, 0), passes=('color', 'z', 'shadow'), shadow_z=0.0,
                    kind='prop', samples=32)
    C.save_meta(d)


def main():
    a = C.args({'samples': SAMPLES})
    argv = sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else []

    def opt(k, default=''):
        return argv[argv.index(k) + 1] if k in argv else default
    out = os.path.abspath(a.out)
    os.makedirs(out, exist_ok=True)
    if '--floor' in argv:
        floor(out)
    with open(os.path.join(out, 'palettes.json'), 'w') as fh:
        pal = {k: dict(v, **{'4': DARK}) for k, v in PALETTES.items()}
        json.dump(pal, fh, indent=1, sort_keys=True)
    which = opt('--set', 'all')
    anims = [x for x in opt('--anims').split(',') if x]
    items_sel = [] if opt('--items') == '-' else ([x for x in opt('--items').split(',') if x] or ALL_ITEMS)
    part = opt('--part')
    if which == 'sample':
        jobs = [(j, SAMPLE_ITEMS if j in SAMPLE_LAYERED else ()) for j in SAMPLE]
    else:
        jobs = [(j, items_sel) for j in all_jobs()]
    jobs = [(j, it) for (j, it) in jobs if (not anims or j[0] in anims) and
            (not a.only or frame_name(*j) in a.only)]
    dst = out
    if part:
        pi, pn = (int(x) for x in part.split('/'))
        jobs = jobs[pi::pn]
        dst = os.path.join(out, '_part%d' % pi)
    log = []
    t0 = time.time()
    for k, ((anim, d, f), items) in enumerate(jobs):
        render_frame(dst, anim, d, f, items, log)
        print('[mila] %d/%d  %.0fs' % (k + 1, len(jobs), time.time() - t0))
    print('[mila] total %.1fs' % (time.time() - t0))
    with open(os.path.join(dst, '_timing%s.json' % (part.replace('/', '_') if part else '')), 'w') as fh:
        json.dump(log, fh, indent=1)


main()
