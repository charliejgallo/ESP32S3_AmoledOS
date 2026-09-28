"""Monster Hop - Abyss Bay (zone prefix `bay`, water monsters): blocks, fills,
surfaces, the tide cell, props and bridges, rendered under the zone's light
into assets/tiles_bay: the full set of SPEC 13.2 (the sample was approved
on 2026-09-27).

    cd apps/monsterhop/tools/blender
    Blender -b -P zones_bay_tiles.py -- --out ../../assets/tiles_bay            # everything
    Blender -b -P zones_bay_tiles.py -- --out ../../assets/tiles_bay --only bay_buoy,bay_anchor

The zone light is mh_common.LIGHTS['bay'] (a cold blue moonlight: sun
(0.64, 0.80, 1.00) x 2.15, sky (0.22, 0.40, 0.62) x 0.62).

Textures are numpy (zones_bay_tex), props procedural (zones_bay_props),
moving things in zones_bay_dyn; the
desert/forest helpers (zones_df_lib) are imported, not copied or changed.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import mh_common as C  # noqa: E402
import zones_df_lib as L  # noqa: E402
import zones_bay_tex as BT  # noqa: E402
import zones_bay_props as P  # noqa: E402
import zones_bay_dyn as D  # noqa: E402

Z = 'bay'

a = L.args()

C.reset(Z, cpu=a.cpu)
_mats = {}


def tmat(key, fn, mapping='top', **kw):
    if key not in _mats:
        _mats[key] = L.tile_mat(key, fn(), mapping, **kw)
    return _mats[key]


# (type, variants, top(v), side, kwargs)
BLOCKS = [
    ('sand', 3, BT.sand_top, BT.sand_side, dict(rough=0.8, spec=0.3, emit_strength=2.5)),
    ('rock', 2, BT.rock_top, BT.rock_side, dict(rough=0.7, spec=0.35, emit_strength=3.0)),
    ('plank_x', 2, lambda v: BT.pier_top(v, 'x'), BT.pier_side, dict(rough=0.8, spec=0.25)),
    ('plank_y', 2, lambda v: BT.pier_top(v, 'y'), BT.pier_side, dict(rough=0.8, spec=0.25)),
    ('quay', 3, BT.quay_top, BT.quay_side, dict(rough=0.8, spec=0.3, emit_strength=3.0)),
]
FILLS = [
    ('sand', BT.sand_fill, lambda: BT.sand_top(0)),
    ('rock', BT.rock_side, lambda: BT.rock_top(0)),
    ('quay', BT.quay_side, lambda: BT.quay_top(0)),
]
SURFS = [
    ('sea', 2, BT.sea_top, dict(rough=0.25, spec=0.5, emit_strength=2.5)),
    ('deep', 1, BT.whirl_top, dict(rough=0.25, spec=0.5, emit_strength=2.0)),
]


def do_blocks():
    for typ, nv, top, side, kw in BLOCKS:
        for v in range(nv):
            name = '%s_blk_%s_v%d' % (Z, typ, v)
            if a.only and name not in a.only:
                continue
            tk = tmat('%s_top_v%d' % (typ, v), lambda: top(v), 'top', **kw)
            skw = dict(kw)
            sk = tmat('%s_side' % typ, side, 'side', **skw)
            ex = {'walk': typ[-1]} if typ.startswith('plank') else None
            L.render_tile(a, name, [L.block(name, tk, sk)], extra=ex)
    for typ, side, top in FILLS:
        name = '%s_fill_%s' % (Z, typ)
        if a.only and name not in a.only:
            continue
        tk = tmat('%s_filltop' % typ, top, 'top')
        sk = tmat('%s_fill' % typ, side, 'side', rough=0.85, spec=0.25, emit_strength=3.0)
        L.render_tile(a, name, [L.block(name, tk, sk)])
    sea_side = tmat('sea_side', BT.sea_side, 'side', rough=0.3, spec=0.4)
    for typ, nv, top, kw in SURFS:
        for v in range(nv):
            name = '%s_surf_%s_v%d' % (Z, typ, v)
            if a.only and name not in a.only:
                continue
            tk = tmat('%s_surf_v%d' % (typ, v), lambda: top(v), 'top', **kw)
            L.render_tile(a, name, [L.grid_top(name, tk, lambda X, Y: 0 * X, n=8, thick=0.12, side_key=sea_side)],
                          kind='surf', extra={'depth_m': 0.18, 'deep': typ == 'deep'})
    # the tide cell: one block in three states the watch swaps on a clock
    states = {'0': 'dry (walkable)', '1': 'water coming in (warning, still walkable)', '2': 'flooded (sinks you)'}
    for s in range(3):
        name = '%s_tide_%02d' % (Z, s)
        if a.only and name not in a.only:
            continue
        tk = tmat('tide_top%d' % s, lambda: BT.tide_top(s), 'top', rough=0.8, spec=0.35, emit_strength=2.5)
        sk = tmat('tide_side%d' % s, lambda: BT.tide_side(s), 'side', rough=0.8, spec=0.3)
        L.render_tile(a, name, [L.block(name, tk, sk)],
                      extra=dict(anim='tide', frame=s, frames=3, states=states, block=True))


do_blocks()
P.run(a, set())
D.run(a)
C.save_meta(a.out)
print('TIMES', ' '.join('%s=%.1f' % t for t in L.TIMES))
print('TOTAL %.1fs for %d sprites' % (sum(t for _, t in L.TIMES), len(L.TIMES)))
