#!/usr/bin/env python3
"""Monster Hop - Lost Valley: the style-sample compose scene and contact sheet
(plain python3: numpy + PIL).

    python3 zones_dino_scene.py scene     # assets/tiles_dino/_scene.{json,png}, _scene_2x.png (+ _glow)
    python3 zones_dino_scene.py sheet     # assets/tiles_dino/_sheet.png

The scene is built with zones_df_scene.build/render (shared, unchanged): an
ASCII map of block types and heights, props and Tommy on it. If the raptor
of the monster sample (monsters_dino.py) is rendered, it is put in the scene
too, recoloured with its default palette.
"""
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import zones_df_scene as S  # noqa: E402
from PIL import Image, ImageDraw  # noqa: E402

Z = 'dino'
ROOT = S.ROOT
D = os.path.join(ROOT, 'assets', 'tiles_' + Z)
MON = os.path.join(ROOT, 'assets', 'monsters')

LEGEND = {
    'd': dict(top='dirt', fill='dino_fill_dirt'),
    'g': dict(top='grass', fill='dino_fill_dirt', salt=3),
    'r': dict(top='basalt', fill='dino_fill_basalt', salt=5),
    'p': dict(top='path', fill='dino_fill_dirt', salt=2),
    'b': dict(top='bone', fill='dino_fill_dirt'),
    'l': dict(top='dino_lava_x_03', fill='dino_fill_basalt'),
    'L': dict(top='dino_lava_x_01', fill='dino_fill_basalt'),
    't': dict(surf='dino_surf_tar'),
    'w': dict(surf='dino_surf_water'),
}


def scene():
    # back of the level first (y = 9) ... front (y = 0); x = 0..8
    types = [
        "rrrrggggg",
        "rrrrgggdd",
        "lllLLLLdd",
        "rrrrrgpdd",
        "ggggggpgg",
        "ddttgpggg",
        "ddttgpggg",
        "dddgpgggg",
        "ddddpgggd",
        "dbdpggwww",
    ]
    heights = [
        "222211111",
        "222211100",
        "111111100",
        "111110000",
        "000000000",
        "00..00000",
        "00..00000",
        "000000000",
        "000000000",
        "000000...",
    ]
    items = [
        ['dino_tree', 0, 8],
        ['dino_tree', 7, 7],
        ['dino_vent_04', 3, 9],
        ['dino_cycad', 5, 8],
        ['dino_boulder', 1, 6],
        ['dino_ribcage_x', 6, 1],
        ['dino_fern', 8, 3],
        ['dino_fern', 0, 1],
        ['dino_fiddlehead', 4, 5],
        ['dino_amber', 8, 8],
        ['dino_volcanorock', 2, 8],
        ['dino_palm', 8, 6],
        ['dino_skull', 1, 0],
        ['dino_nest', 7, 5],
        ['dino_bonefence_x', 7, 4],
        ['dino_bonefence_x', 8, 4],
        ['tommy_test', 5, 3, {"palette": "tommy"}],
    ]
    sc = S.build(Z, LEGEND, types, heights, items, look_at=[4.2, 4.9, 0.4], bg=[24, 18, 16])
    # the raptor of the monster sample, stalking along the path
    mj = os.path.join(MON, 'meta.json')
    if os.path.exists(mj):
        with open(mj) as fh:
            meta = json.load(fh)
        with open(os.path.join(MON, 'palettes.json')) as fh:
            pals = json.load(fh)
        rel = os.path.relpath(MON, D)
        sc['assets'].append(rel)
        for who in ('raptor', 'compy', 'trike'):
            if who in pals:
                sc['palettes'][who] = pals[who]
        sc['items'].append(['dino_logfloat_w', 6, 0, 0, {'at': [6.5, 0.5, -0.02]}])
        sc['items'].append(['dino_logfloat_e', 7, 0, 0, {'at': [7.5, 0.5, -0.02]}])
        for name, x, y, fl, pal in (('raptor_walk_w_02', 3, 1, 0, 'raptor'), ('compy_run_e_00', 1, 3, 0, 'compy'),
                                     ('compy_run_e_02', 2, 2, 0, 'compy'), ('trike_howl_s_01', 5, 6, 1, 'trike')):
            if name in meta:
                sc['items'].append([name, x, y, fl, {"palette": pal}])
    return sc


def sheet(scale=2, cols=8):
    with open(os.path.join(D, 'meta.json')) as fh:
        meta = json.load(fh)
    names = sorted(k for k in meta if not k.startswith('_'))
    ims = [Image.open(os.path.join(D, meta[n]['files']['img'])).convert('RGBA') for n in names]
    ims = [im.crop(im.getbbox()) if im.getbbox() else im for im in ims]
    W = max(i.width for i in ims) * scale
    Hh = max(i.height for i in ims) * scale + 14
    rows = (len(ims) + cols - 1) // cols
    sh = Image.new('RGBA', (cols * (W + 8), rows * (Hh + 8)), (24, 22, 26, 255))
    dr = ImageDraw.Draw(sh)
    for k, (n, im) in enumerate(zip(names, ims)):
        x = (k % cols) * (W + 8)
        y = (k // cols) * (Hh + 8)
        big = im.resize((im.width * scale, im.height * scale), Image.NEAREST)
        top = y + 14 + (Hh - 14 - big.height)
        sh.alpha_composite(big, (x + (W - big.width) // 2, top))
        dr.text((x + 2, top - 13), '%s %dx%d' % (n.replace(Z + '_', ''), meta[n]['w'], meta[n]['h']),
                fill=(230, 230, 230, 255))
    out = os.path.join(D, '_sheet.png')
    sh.convert('RGB').save(out)
    print('wrote', out)


if __name__ == '__main__':
    what = sys.argv[1] if len(sys.argv) > 1 else 'scene'
    if what == 'sheet':
        sheet()
    else:
        S.render(Z, 'scene', scene())
