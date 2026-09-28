"""Monster Hop - Lost Valley (dinosaurs, zone prefix `dino`, SPEC 13.1): blocks,
fills, surfaces, bridges, props and the zone's moving things, rendered under
C.reset('dino') into assets/tiles_dino.

    cd apps/monsterhop/tools/blender
    Blender -b -P zones_dino_tiles.py -- --out ../../assets/tiles_dino                 # everything
    Blender -b -P zones_dino_tiles.py -- --out ../../assets/tiles_dino --sample        # a small subset
    Blender -b -P zones_dino_tiles.py -- --out ../../assets/tiles_dino --only dino_vent_03,dino_gate_06

Reuses zones_df_lib (materials, block geometry, render wrappers) and
zones_df_tex (noise) as they are; textures are zones_dino_tex, props
zones_dino_props, moving things zones_dino_dyn. The light is
mh_common.LIGHTS['dino'].
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import mh_common as C  # noqa: E402
import zones_df_lib as L  # noqa: E402
import zones_dino_tex as D  # noqa: E402
import zones_dino_props as P  # noqa: E402
import zones_dino_dyn as Y  # noqa: E402

Z = 'dino'
SAMPLE = {
    'dino_blk_dirt_v0', 'dino_blk_grass_v0', 'dino_blk_basalt_v0', 'dino_fill_dirt', 'dino_fill_basalt',
    'dino_surf_tar_v0', 'dino_lava_x_02', 'dino_vent_04', 'dino_gate_00',
}

a = L.args()
C.reset(Z, cpu=a.cpu)
_mats = {}


def tmat(key, fn, mapping='top', right=None, **kw):
    if key not in _mats:
        _mats[key] = L.tile_mat(key, fn(), mapping, tex_right=right() if right else None, **kw)
    return _mats[key]


BLOCKS = [
    ('dirt', 3, D.dirt_top, D.dirt_side, dict(rough=0.95, spec=0.2)),
    ('grass', 3, D.grass_top, D.grass_side, dict(rough=0.9, spec=0.25)),
    ('basalt', 2, D.basalt_top, D.basalt_side, dict(rough=0.8, spec=0.3)),
    ('path', 2, D.path_top, D.path_side, dict(rough=0.85, spec=0.25)),
    ('bone', 1, D.bone_top, D.dirt_side, dict(rough=0.85, spec=0.25)),
]
FILLS = [
    ('dirt', D.earth_fill, lambda: D.dirt_top(0)),
    ('basalt', D.basalt_side, lambda: D.basalt_top(0)),
]
SURFS = [
    ('tar', 2, D.tar_top, D.tar_height, dict(rough=0.12, spec=0.6),
     {'sinking': True, 'like': 'desert_surf_quicksand'}),
    ('water', 2, D.water_top, lambda X, Y_: 0 * X, dict(rough=0.3, spec=0.4), {'water': True}),
]


def want(name):
    return L.wanted(a, name, SAMPLE)


def do_blocks():
    for typ, nv, top, side, kw in BLOCKS:
        for v in range(nv):
            name = '%s_blk_%s_v%d' % (Z, typ, v)
            if not want(name):
                continue
            tk = tmat('%s_top_v%d' % (typ, v), lambda: top(v), 'top', **kw)
            sk = tmat('%s_side' % typ, side, 'side', **kw)
            L.render_tile(a, name, [L.block(name, tk, sk)], extra={'decorative': True} if typ == 'bone' else None)
    for typ, side, top in FILLS:
        name = '%s_fill_%s' % (Z, typ)
        if not want(name):
            continue
        tk = tmat('%s_filltop' % typ, top, 'top')
        sk = tmat('%s_fill' % typ, side, 'side', rough=0.9, spec=0.2)
        L.render_tile(a, name, [L.block(name, tk, sk)])
    for typ, nv, top, hfun, kw, ex in SURFS:
        for v in range(nv):
            name = '%s_surf_%s_v%d' % (Z, typ, v)
            if not want(name):
                continue
            tk = tmat('%s_surf_v%d' % (typ, v), lambda: top(v), 'top', **kw)
            L.render_tile(a, name, [L.grid_top(name, tk, hfun, n=48, thick=0.12)], kind='surf', extra=ex)


def do_dyn():
    for axis in ('x', 'y'):
        for f in range(6):
            name = '%s_lava_%s_%02d' % (Z, axis, f)
            if not want(name):
                continue
            objs, lamps, ex = Y.lava(axis, f)
            glow = ((0.5, 0.5), 0.75) if lamps else None
            L.sprite(a, name, objs, kind='dyn', passes=('color', 'z'), glow=glow, lights=lamps, extra=ex)
    for f in range(8):
        name = '%s_vent_%02d' % (Z, f)
        if want(name):
            objs, lamps, ex = Y.vent(f)
            L.sprite(a, name, objs, kind='dyn', glow=((0.5, 0.5), 0.8), lights=lamps, extra=ex)
    name = '%s_fallrock' % Z
    if want(name):
        objs, lamps, ex = Y.fallrock()
        L.sprite(a, name, objs, kind='dyn', passes=('color', 'z'), glow=((0.5, 0.5), 0.75), lights=lamps, extra=ex)
    for f in range(4):
        name = '%s_rockbits_%02d' % (Z, f)
        if want(name):
            objs, lamps, ex = Y.rockbits(f)
            L.sprite(a, name, objs, kind='dyn', glow=((0.5, 0.5), 0.75), lights=lamps, extra=ex)
    for part in ('w', 'm', 'e'):
        name = '%s_logfloat_%s' % (Z, part)
        if want(name):
            L.sprite(a, name, Y.logfloat(part), shadow_z=Y.WATER,
                     extra=dict(part=part, float_z=Y.FLOAT_Z, height_m=0.2, footprint=[1, 1]))
    for f in range(7):
        name = '%s_gate_%02d' % (Z, f)
        if want(name):
            objs, lamps = Y.gate(f)
            L.sprite(a, name, objs, glow=((0.5, 0.6), 1.1) if f == 6 else None, lights=lamps,
                     extra=dict(anim='open', frame=f, frames=7, height_m=2.2, exit=True,
                                states={'0': 'closed', '6': 'open'}))
    name = '%s_crate' % Z
    if want(name):
        L.sprite(a, name, Y.crate(), extra=dict(footprint=[1, 1], height_m=round(C.FLOOR_M, 4), pushable=True))
    for d in ('x', 'y'):
        name = '%s_bridge_%s' % (Z, d)
        if want(name):
            L.sprite(a, name, Y.bridge(d), kind='bridge', shadow_z=-0.18,
                     extra=dict(walk=d, deck_z=0.0, over='dino_surf_water'))


def _read_png(path, ch):
    """A PNG as uint8 (h, w, ch), row 0 at the top (Blender's loader; no PIL here)."""
    import bpy
    import numpy as np
    im = bpy.data.images.load(path, check_existing=False)
    im.colorspace_settings.name = 'Non-Color'
    w, h = im.size
    px = np.empty(w * h * 4, np.float32)
    im.pixels.foreach_get(px)
    bpy.data.images.remove(im)
    return np.round(px.reshape(h, w, 4)[::-1, :, :ch] * 255).astype(np.uint8)


def trim_faint(out, gl_min=8, sh_min=10):
    """Memory: zero the faint tail of glow and shadow planes (under ~3 % of
    light or darkness, invisible on the watch) so the packer's spans stay
    short. Only this run's sprites."""
    for name, info in C._STATE['meta'].items():
        f = info['files']
        if 'gl' in f:
            p = os.path.join(os.path.abspath(out), f['gl'])
            g = _read_png(p, 3).copy()
            g[g.max(axis=2) < gl_min] = 0
            C.write_png(p, g)
        if 'sh' in f:
            p = os.path.join(os.path.abspath(out), f['sh'])
            h = _read_png(p, 1)[..., 0].copy()
            h[h < sh_min] = 0
            C.write_png(p, h)


do_blocks()
P.run(a, SAMPLE)
do_dyn()
trim_faint(a.out)
C.save_meta(a.out)
print('TIMES', ' '.join('%s=%.1f' % t for t in L.TIMES))
print('TOTAL %.1fs for %d sprites' % (sum(t for _, t in L.TIMES), len(L.TIMES)))
