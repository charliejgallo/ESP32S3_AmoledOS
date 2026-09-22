#!/usr/bin/env python3
"""Monster Hop - previews of the monster sprites (plain python3: numpy + PIL).

    python3 monsters_scene.py sheet DIR [out.png] [scale]   recoloured contact sheet of DIR
    python3 monsters_scene.py sample [DIR]                  the style-sample compose scene
    python3 monsters_scene.py scene [DIR]                   a game situation (_scene.json/png)
    python3 monsters_scene.py sheets [DIR]                  _sheet_<monster>.png per monster + _cards.png

The sheet recolours every monster frame with its default palette from
DIR/palettes.json exactly like tools/compose.py (palette[id] * light / 196)
and shows it at 1x and enlarged, on a dark ground.
"""
import json
import os
import subprocess
import sys

import numpy as np
from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
ASSETS = os.path.normpath(os.path.join(HERE, '..', '..', 'assets'))
COMPOSE = os.path.normpath(os.path.join(HERE, '..', 'compose.py'))


def load_palettes(d):
    with open(os.path.join(d, 'palettes.json')) as fh:
        return json.load(fh)


def full_palette(pals, who, variant=None):
    p = {k: v for k, v in pals[who].items()}
    if variant is not None:
        var = pals[who + '_variants']
        key = variant if isinstance(variant, str) else sorted(var)[variant] if isinstance(var, dict) else variant
        for k, v in (var[key] if isinstance(var, dict) else var[variant]).items():
            if k != 'name':
                p[k] = v
    return p


def recolour(d, info, pal):
    f = info['files']
    img = np.asarray(Image.open(os.path.join(d, f['img'])).convert('RGBA')).astype(np.float32)
    ids = np.asarray(Image.open(os.path.join(d, f['id'])).convert('L')) // 16
    lut = np.zeros((16, 3), np.float32)
    lut[:] = (255, 0, 255)
    for k, c in pal.items():
        lut[int(k)] = c
    col = np.clip(lut[ids] * img[..., :3] / 196.0, 0, 255)
    return col, img[..., 3] / 255.0


def sheet(d, out=None, scale=3, bg=(34, 44, 40), only=None, cards=True, cards_only=False):
    with open(os.path.join(d, 'meta.json')) as fh:
        meta = json.load(fh)
    pals = load_palettes(d)
    names = sorted(k for k, v in meta.items() if not k.startswith('_') and 'monster' in v
                   and (only is None or v['monster'] == only)
                   and (cards or v.get('kind') != 'card') and (not cards_only or v.get('kind') == 'card'))
    tiles = []
    for nm in names:
        info = meta[nm]
        col, al = recolour(d, info, full_palette(pals, info['monster']))
        h, w = al.shape
        canvas = np.zeros((h, w, 3), np.float32)
        canvas[:] = bg
        if 'sh' in info['files']:
            sh = np.asarray(Image.open(os.path.join(d, info['files']['sh'])).convert('L')) / 255.0 * 0.55
            canvas *= (1 - sh[..., None])
        canvas = canvas * (1 - al[..., None]) + col * al[..., None]
        im = Image.fromarray(np.clip(canvas, 0, 255).astype(np.uint8))
        tiles.append((nm, im, info))
    if not tiles:
        print('nothing to show')
        return
    cellw = max(t[1].width for t in tiles) * (scale + 1) + 24
    cellh = max(t[1].height for t in tiles) * scale + 30
    cols = max(1, min(len(tiles), 1600 // cellw))
    rows = (len(tiles) + cols - 1) // cols
    sheet_im = Image.new('RGB', (cols * cellw, rows * cellh), (16, 18, 22))
    dr = ImageDraw.Draw(sheet_im)
    for i, (nm, im, info) in enumerate(tiles):
        x0, y0 = (i % cols) * cellw + 8, (i // cols) * cellh + 4
        sheet_im.paste(im, (x0, y0))
        big = im.resize((im.width * scale, im.height * scale), Image.NEAREST)
        sheet_im.paste(big, (x0 + im.width + 8, y0))
        dr.text((x0, y0 + big.height + 4), '%s %dx%d' % (nm, info['w'], info['h']), fill=(200, 200, 200))
    out = out or os.path.join(d, '_sheet.png')
    sheet_im.save(out)
    print('wrote', out)


TOMMY = {"1": [245, 190, 150], "3": [25, 20, 20], "5": [40, 120, 245], "8": [50, 60, 110], "11": [230, 40, 40]}
WHO = ('zombie', 'vampire', 'bat', 'mummy', 'werewolf', 'zombiedog', 'armor', 'scarab', 'crow')


def _palettes(pals):
    out = {'tommy': TOMMY}
    for who in WHO:
        if who in pals:
            out[who] = full_palette(pals, who)
    for name in pals.get('zombie_variants', {}):
        out['zombie_' + name] = full_palette(pals, 'zombie', name)
    return out


def _write_and_compose(d, stem, sc):
    path = os.path.join(d, stem + '.json')
    with open(path, 'w') as fh:
        json.dump(sc, fh, indent=1)
    for outname, scale in ((stem + '.png', 1), (stem + '_2x.png', 2)):
        subprocess.check_call([sys.executable, COMPOSE, path, os.path.join(d, outname), str(scale)])


def sample(d=None):
    """The style sample: the walk cycle on a dirt path in front, Tommy and the
    four zone monsters in the middle row, the five small ones at the back."""
    d = os.path.abspath(d or os.path.join(ASSETS, 'monsters'))
    test = os.path.relpath(os.path.join(ASSETS, '_test'), d)
    pals = load_palettes(d)
    with open(os.path.join(d, 'meta.json')) as fh:
        have = set(json.load(fh).keys())
    have.add('tommy_test')
    items = []
    ox = 2.0                           # the floor starts two cells left of the lineup

    def put(name, pal, wx, wy):
        if name in have:
            items.append([name, 0, 0, 0, {'palette': pal, 'at': [ox + wx, wy, 0.0]}])
    for f in range(6):
        put('zombie_walk_e_%02d' % f, 'zombie', 2.3 + 0.9 * f, 1.0)
    mid = [('tommy_test', 'tommy'), ('zombie_idle_s_00', 'zombie'), ('vampire_idle_s_00', 'vampire'),
           ('mummy_idle_s_00', 'mummy'), ('werewolf_idle_s_00', 'werewolf')]
    for i, (nm, pal) in enumerate(mid):
        put(nm, pal, 1.5 + i, 3.5)
    back = [('zombiedog_idle_s_00', 'zombiedog'), ('scarab_crawl_s_00', 'scarab'), ('crow_perch_s_00', 'crow'),
            ('bat_fly_s_00', 'bat'), ('armor_idle_s_00', 'armor')]
    for i, (nm, pal) in enumerate(back):
        put(nm, pal, 0.5 + i, 5.5)
    w, h = 12, 9
    rows = ['0' * w] * h
    rows[1] = '.' * w                  # the dirt path of the walk cycle (placed as items below)
    for x in range(w):
        items.append(['blk_dirt', x, 1, 0])
        items.append(['blk_dirt', x, 1, -1])
    sc = {'assets': ['.', test], 'size': [368, 448], 'look_at': [ox + 3.69, 3.60, 0.0], 'bg': [14, 16, 26],
          'palettes': _palettes(pals),
          'grid': {'w': w, 'h': h, 'floor': 'blk_grass', 'fill': 'blk_dirt', 'heights': rows},
          'items': items}
    _write_and_compose(d, '_sample', sc)
    variants(d)


def variants(d):
    """The zombie in its default clothes and the three variations."""
    test = os.path.relpath(os.path.join(ASSETS, '_test'), d)
    pals = load_palettes(d)
    names = ['zombie'] + ['zombie_' + k for k in pals['zombie_variants']]
    items = [['zombie_idle_s_00', 0, 0, 0, {'palette': nm, 'at': [0.5 + i, 0.5, 0.0]}] for i, nm in enumerate(names)]
    items += [['zombie_walk_e_02', 0, 0, 0, {'palette': nm, 'at': [0.2 + i, 2.0, 0.0]}] for i, nm in enumerate(names)]
    sc = {'assets': ['.', test], 'size': [300, 210], 'look_at': [2.25, 1.75, 0.45], 'bg': [14, 16, 26],
          'palettes': _palettes(pals),
          'grid': {'w': 5, 'h': 3, 'floor': 'blk_grass', 'fill': 'blk_dirt', 'heights': ['00000'] * 3},
          'items': items}
    _write_and_compose(d, '_variants', sc)


def scene(d=None):
    """A game situation: the whole cast moving about a small raised level,
    in the frames the watch will show (walks, runs, flights, a lunge)."""
    d = os.path.abspath(d or os.path.join(ASSETS, 'monsters'))
    test = os.path.relpath(os.path.join(ASSETS, '_test'), d)
    pals = load_palettes(d)
    it = []

    def put(name, pal, x, y, fl=0, at=None):
        o = {'palette': pal}
        if at:
            o['at'] = at
        it.append([name, x, y, fl, o])
    put('tommy_test', 'tommy', 3, 2)
    put('zombie_lunge_w_01', 'zombie', 4, 2)
    put('zombie_walk_s_02', 'zombie_office', 1, 3)
    put('zombie_walk_n_04', 'zombie_tourist', 4, 4)
    put('zombiedog_run_e_02', 'zombiedog', 1, 1)
    put('mummy_push_e_01', 'mummy', 1, 4)
    put('werewolf_run_w_03', 'werewolf', 5, 3)
    put('werewolf_howl_s_03', 'werewolf', 4, 6, 2)
    put('vampire_glide_e_02', 'vampire', 2, 5, 1)
    put('armor_walk_s_01', 'armor', 3, 6, 1)
    put('armor_idle_s_00', 'armor', 1, 6, 1)
    put('bat_fly_w_01', 'bat', 4, 4)
    put('bat_fly_s_03', 'bat', 3, 4)
    put('crow_fly_e_00', 'crow', 0, 3)
    put('crow_dive_w_01', 'crow', 5, 1)
    put('crow_perch_s_01', 'crow', 5, 5, 2)
    for k in range(3):
        put('scarab_crawl_e_%02d' % k, 'scarab', 0, 0, 0, at=[2.2 + 0.7 * k, 0.5, 0.0])
    rows = ['0000000'] * 5 + ['1111222'] * 3
    sc = {'assets': ['.', test], 'size': [368, 448], 'look_at': [2.7, 4.3, 0.0], 'bg': [14, 16, 26],
          'palettes': _palettes(pals),
          'grid': {'w': 7, 'h': 8, 'floor': 'blk_grass', 'fill': 'blk_dirt', 'heights': rows},
          'items': it}
    _write_and_compose(d, '_scene', sc)


def sheets(d=None):
    d = os.path.abspath(d or os.path.join(ASSETS, 'monsters'))
    for who in WHO:
        sheet(d, os.path.join(d, '_sheet_%s.png' % who), 2, only=who, cards=False)
    sheet(d, os.path.join(d, '_cards.png'), 1, cards_only=True, bg=(235, 225, 200))


if __name__ == '__main__':
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(1)
    if sys.argv[1] == 'sheet':
        sheet(sys.argv[2], sys.argv[3] if len(sys.argv) > 3 else None,
              int(sys.argv[4]) if len(sys.argv) > 4 else 3)
    elif sys.argv[1] == 'sample':
        sample(sys.argv[2] if len(sys.argv) > 2 else None)
    elif sys.argv[1] == 'scene':
        scene(sys.argv[2] if len(sys.argv) > 2 else None)
    elif sys.argv[1] == 'sheets':
        sheets(sys.argv[2] if len(sys.argv) > 2 else None)
