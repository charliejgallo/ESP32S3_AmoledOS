"""Monster Hop - Mummy Desert (brown): blocks, fills, surfaces, props and the
zone's moving things, rendered under C.reset('desert') into assets/tiles_desert.

    cd apps/monsterhop/tools/blender
    Blender -b -P desert.py -- --out ../../assets/tiles_desert            # everything
    Blender -b -P desert.py -- --out ../../assets/tiles_desert --sample   # the style sample
    Blender -b -P desert.py -- --out ../../assets/tiles_desert --only desert_palm,desert_cactus

Textures are numpy (zones_df_tex_desert), exactly periodic so blocks join;
props are procedural meshes. Lit props add their own point lights and remove
them after their render.
"""
import math
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import mh_common as C  # noqa: E402
import zones_df_lib as L  # noqa: E402
import zones_df_tex as T  # noqa: E402
import zones_df_tex_desert as D  # noqa: E402
import zones_df_props_desert as P  # noqa: E402

Z = 'desert'
SAMPLE = {
    'desert_blk_sand_v0', 'desert_blk_sand_v1', 'desert_blk_sandstone_v0', 'desert_blk_brick_v0',
    'desert_blk_brick_win', 'desert_blk_oasis_v0', 'desert_fill_sand', 'desert_fill_brick', 'desert_fill_sandstone',
    'desert_surf_water_v0', 'desert_surf_quicksand_v0',
    'desert_palm', 'desert_cactus', 'desert_obelisk', 'desert_brazier',
}

a = L.args()
C.reset(Z, cpu=a.cpu)
_mats = {}


def tmat(key, fn, mapping='top', right=None, **kw):
    """Register (once) a tile material from a texture recipe."""
    if key not in _mats:
        _mats[key] = L.tile_mat(key, fn(), mapping, tex_right=right() if right else None, **kw)
    return _mats[key]


# ---------------------------------------------------------------------------
# blocks: (type, variants, top recipe(v), side recipe, material options)
# ---------------------------------------------------------------------------

BLOCKS = [
    ('sand', 3, D.sand_top, D.sand_side, dict(rough=0.95, spec=0.2)),
    ('sandstone', 3, D.sandstone_top, D.sandstone_side, dict(rough=0.8, spec=0.25)),
    ('brick', 3, D.brick_top, D.brick_side, dict(rough=0.85, spec=0.2)),
    ('oasis', 3, D.oasis_top, D.oasis_side, dict(rough=0.9, spec=0.2)),
    ('cracked', 3, D.cracked_top, D.cracked_side, dict(rough=0.8, spec=0.25)),
    ('gold', 3, D.gold_top, D.gold_side, dict(rough=0.35, spec=0.5)),
]
FILLS = [
    ('sand', D.sand_fill, lambda: D.sand_top(0)),
    ('brick', D.brick_fill, lambda: D.brick_top(0)),
    ('sandstone', D.sandstone_side, lambda: D.sandstone_top(0)),
]
SURFS = [
    ('water', D.water_top, lambda X, Y: 0 * X, dict(rough=0.35, spec=0.35)),
    ('quicksand', D.quicksand_top, D.quicksand_height, dict(rough=0.75, spec=0.3)),
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
    # the hieroglyph wall: a brick block with the carved panel on both faces
    name = '%s_blk_brick_win' % Z
    if L.wanted(a, name, SAMPLE):
        tk = tmat('brick_top_v0', lambda: D.brick_top(0), 'top', rough=0.85, spec=0.2)
        sk = tmat('brick_win_side', lambda: D.brick_win_face(False), 'side',
                  right=lambda: D.brick_win_face(True), rough=0.75, spec=0.3)
        L.render_tile(a, name, [L.block(name, tk, sk)])
    # the dart wall: a brick block with three dart holes on each visible face
    name = '%s_dartwall' % Z
    if L.wanted(a, name, SAMPLE):
        tk = tmat('brick_top_v0', lambda: D.brick_top(0), 'top', rough=0.85, spec=0.2)
        sk = tmat('dartwall_side', lambda: D.dartwall_face(False), 'side',
                  right=lambda: D.dartwall_face(True), rough=0.8, spec=0.25)
        L.render_tile(a, name, [L.block(name, tk, sk)], extra={'holes_z_m': 0.28, 'dart_dirs': ['-y', '+x']})
    # the pushable sandstone block: exactly one floor tall, on top of the ground
    name = '%s_crate' % Z
    if L.wanted(a, name, SAMPLE):
        tk = tmat('sandstone_top_v2', lambda: D.sandstone_top(2), 'top', rough=0.8, spec=0.25)
        sk = tmat('crate_side', lambda: D.crate_face(False), 'side', right=lambda: D.crate_face(True),
                  rough=0.8, spec=0.25)
        L.render_tile(a, name, [L.block(name, tk, sk, top_z=C.FLOOR_M, inset=0.02)], kind='dyn',
                      extra={'footprint': [1, 1], 'height_m': round(C.FLOOR_M, 4), 'pushable': True})
    for typ, side, top in FILLS:
        name = '%s_fill_%s' % (Z, typ)
        if not L.wanted(a, name, SAMPLE):
            continue
        tk = tmat('%s_filltop' % typ, top, 'top')
        sk = tmat('%s_fill' % typ, side, 'side', rough=0.9, spec=0.2)
        L.render_tile(a, name, [L.block(name, tk, sk, chamfer=0.0)])
    for typ, top, hfun, kw in SURFS:
        for v in range(3):
            name = '%s_surf_%s_v%d' % (Z, typ, v)
            if not L.wanted(a, name, SAMPLE):
                continue
            tk = tmat('%s_surf_v%d' % (typ, v), lambda: top(v), 'top', **kw)
            L.render_tile(a, name, [L.grid_top(name, tk, hfun, n=48, thick=0.12)], kind='surf')


do_blocks()
P.run(a, SAMPLE)
C.save_meta(a.out)
print('TIMES', ' '.join('%s=%.1f' % t for t in L.TIMES))
print('TOTAL %.1fs for %d sprites' % (sum(t for _, t in L.TIMES), len(L.TIMES)))
