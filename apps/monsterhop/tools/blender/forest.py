"""Monster Hop - Werewolf Woods (green): blocks, fills, surfaces, props and the
zone's moving things, rendered under C.reset('forest') into assets/tiles_forest.

    cd apps/monsterhop/tools/blender
    Blender -b -P forest.py -- --out ../../assets/tiles_forest            # everything
    Blender -b -P forest.py -- --out ../../assets/tiles_forest --sample   # the style sample
    Blender -b -P forest.py -- --out ../../assets/tiles_forest --only forest_pine

Textures are numpy (zones_df_tex_forest), exactly periodic so blocks join;
props are procedural meshes (zones_df_props_forest). Lanterns and mushrooms
add their own point lights and remove them after their render.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import mh_common as C  # noqa: E402
import zones_df_lib as L  # noqa: E402
import zones_df_tex_forest as FT  # noqa: E402
import zones_df_props_forest as P  # noqa: E402

Z = 'forest'
SAMPLE = {
    'forest_blk_grass_v0', 'forest_blk_grass_v1', 'forest_blk_path_v0', 'forest_blk_rock_v0',
    'forest_blk_moss_v0', 'forest_fill_earth', 'forest_fill_rock', 'forest_surf_river_v0',
    'forest_pine', 'forest_oak', 'forest_mushroom', 'forest_lantern',
    'forest_logfloat_w', 'forest_logfloat_m', 'forest_logfloat_e',
}

a = L.args()
C.reset(Z, cpu=a.cpu)
_mats = {}


def tmat(key, fn, mapping='top', **kw):
    if key not in _mats:
        _mats[key] = L.tile_mat(key, fn(), mapping, **kw)
    return _mats[key]


BLOCKS = [
    ('grass', 3, FT.grass_top, FT.grass_side, dict(rough=0.9, spec=0.25)),
    ('path', 3, FT.path_top, FT.path_side, dict(rough=0.95, spec=0.2)),
    ('rock', 3, FT.rock_top, FT.rock_side, dict(rough=0.8, spec=0.3)),
    ('moss', 3, FT.moss_top, FT.moss_side, dict(rough=0.85, spec=0.25)),
    ('plank', 3, FT.plank_top, FT.plank_side, dict(rough=0.75, spec=0.3)),
    ('mud', 3, FT.mud_top, FT.mud_side, dict(rough=0.85, spec=0.45)),
]
FILLS = [
    ('earth', FT.earth_fill, lambda: FT.path_top(0)),
    ('rock', FT.rock_side, lambda: FT.rock_top(0)),
]
SURFS = [
    ('river', FT.river_top, lambda X, Y: 0 * X, dict(rough=0.3, spec=0.4)),
]


def do_blocks():
    for typ, nv, top, side, kw in BLOCKS:
        for v in range(nv):
            name = '%s_blk_%s_v%d' % (Z, typ, v)
            if not L.wanted(a, name, SAMPLE):
                continue
            tk = tmat('%s_top_v%d' % (typ, v), lambda: top(v), 'top', **kw)
            sk = tmat('%s_side' % typ, side, 'side', **kw)
            L.render_tile(a, name, [L.block(name, tk, sk)])
    for typ, side, top in FILLS:
        name = '%s_fill_%s' % (Z, typ)
        if not L.wanted(a, name, SAMPLE):
            continue
        tk = tmat('%s_filltop' % typ, top, 'top')
        sk = tmat('%s_fill' % typ, side, 'side', rough=0.9, spec=0.2)
        L.render_tile(a, name, [L.block(name, tk, sk)])
    for typ, top, hfun, kw in SURFS:
        for v in range(3):
            name = '%s_surf_%s_v%d' % (Z, typ, v)
            if not L.wanted(a, name, SAMPLE):
                continue
            tk = tmat('%s_surf_v%d' % (typ, v), lambda: top(v), 'top', **kw)
            L.render_tile(a, name, [L.grid_top(name, tk, hfun, n=8, thick=0.12)], kind='surf')


do_blocks()
P.run(a, SAMPLE)
C.save_meta(a.out)
print('TIMES', ' '.join('%s=%.1f' % t for t in L.TIMES))
print('TOTAL %.1fs for %d sprites' % (sum(t for _, t in L.TIMES), len(L.TIMES)))
