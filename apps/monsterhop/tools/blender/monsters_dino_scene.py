#!/usr/bin/env python3
"""Monster Hop - Lost Valley monsters: contact sheets, a compose scene and the
memory budget (plain python3: numpy + PIL).

    python3 monsters_dino_scene.py sheets    # monsters/_sheet_<raptor|trike|ptero|compy>.png,
                                             # monsters/_cards_dino.png, bosses/_sheet_trex.png
    python3 monsters_dino_scene.py scene     # monsters/_scene_dino.{json,png}, _scene_dino_2x.png
    python3 monsters_dino_scene.py budget    # KB per monster / boss / zone, the packer's own encoding

Reads the merged assets/monsters and assets/bosses (monsters_dino.py renders
into a staging root, monsters_dino_merge.py merges it).
"""
import json
import os
import subprocess
import sys

import numpy as np
from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import monsters_scene as MS  # noqa: E402

ASSETS = MS.ASSETS
MON = os.path.join(ASSETS, 'monsters')
BOS = os.path.join(ASSETS, 'bosses')
TILES = os.path.join(ASSETS, 'tiles_dino')
MONSTERS = ('raptor', 'trike', 'ptero', 'compy')
BG = (58, 40, 32)


def _boss_view(d):
    """A copy of bosses' meta with 'monster' set, for monsters_scene.sheet."""
    with open(os.path.join(d, 'meta.json')) as fh:
        meta = json.load(fh)
    for v in meta.values():
        if isinstance(v, dict) and 'boss' in v:
            v['monster'] = v['boss']
    return meta


def _sheet(d, meta, pals, who, out, scale, cards=False):
    names = sorted(k for k, v in meta.items() if isinstance(v, dict) and v.get('monster', v.get('boss')) == who
                   and (v.get('kind') == 'card') == cards)
    tiles = []
    for nm in names:
        info = meta[nm]
        col, al = MS.recolour(d, info, pals[who])
        c = np.zeros(col.shape, np.float32)
        c[:] = BG
        if 'sh' in info['files']:
            sh = np.asarray(Image.open(os.path.join(d, info['files']['sh'])).convert('L')) / 255.0 * 0.55
            c *= (1 - sh[..., None])
        c = c * (1 - al[..., None]) + col * al[..., None]
        tiles.append((nm, Image.fromarray(np.clip(c, 0, 255).astype(np.uint8)), info))
    cw = max(t[1].width for t in tiles) * scale + 12
    ch = max(t[1].height for t in tiles) * scale + 18
    cols = max(1, min(len(tiles), 1800 // cw))
    rows = (len(tiles) + cols - 1) // cols
    im = Image.new('RGB', (cols * cw, rows * ch), (16, 14, 14))
    dr = ImageDraw.Draw(im)
    for i, (nm, t, info) in enumerate(tiles):
        x, y = (i % cols) * cw + 6, (i // cols) * ch + 2
        big = t.resize((t.width * scale, t.height * scale), Image.NEAREST)
        im.paste(big, (x, y))
        dr.text((x, y + big.height + 2), '%s %dx%d' % (nm, info['w'], info['h']), fill=(210, 210, 210))
    im.save(out)
    print('wrote', out)


def sheets():
    with open(os.path.join(MON, 'meta.json')) as fh:
        meta = json.load(fh)
    pals = MS.load_palettes(MON)
    for who in MONSTERS:
        _sheet(MON, meta, pals, who, os.path.join(MON, '_sheet_%s.png' % who), 2)
    bmeta = _boss_view(BOS)
    bpals = MS.load_palettes(BOS)
    _sheet(BOS, bmeta, bpals, 'trex', os.path.join(BOS, '_sheet_trex.png'), 1)
    # the cards on one sheet
    tiles = []
    for d, m, p, who in [(MON, meta, pals, w) for w in MONSTERS] + [(BOS, bmeta, bpals, 'trex')]:
        info = m['card_' + who]
        col, al = MS.recolour(d, info, p[who])
        c = np.zeros(col.shape, np.float32)
        c[:] = (235, 225, 200)
        c = c * (1 - al[..., None]) + col * al[..., None]
        tiles.append(Image.fromarray(np.clip(c, 0, 255).astype(np.uint8)))
    W = sum(t.width + 12 for t in tiles) + 12
    H = max(t.height for t in tiles) + 24
    im = Image.new('RGB', (W, H), (235, 225, 200))
    x = 12
    for t in tiles:
        im.paste(t, (x, H - 12 - t.height))
        x += t.width + 12
    out = os.path.join(MON, '_cards_dino.png')
    im.save(out)
    print('wrote', out)


def scene():
    """A game moment in the valley: the T-Rex chasing up the level, a raptor
    sprinting, a trike charging, a compy pack, a pterodactyl diving, Tommy."""
    pals = {'tommy': MS.TOMMY}
    mp = MS.load_palettes(MON)
    for w in MONSTERS:
        pals[w] = mp[w]
    pals['trex'] = MS.load_palettes(BOS)['trex']
    rel = lambda p: os.path.relpath(p, MON)  # noqa: E731
    items = []
    w, h = 8, 9
    for y in range(h):
        for x in range(w):
            if y in (3, 4) and 2 <= x <= 3:
                items.append(['dino_surf_tar_v%d' % ((x + y) % 2), x, y, 0, {'at': [x + 0.5, y + 0.5, -0.18]}])
                continue
            if y == 6:
                top = 'dino_lava_x_03' if 1 <= x <= 5 else 'dino_blk_basalt_v%d' % (x % 2)
            elif x == 5 and y < 6:
                top = 'dino_blk_path_v%d' % (y % 2)
            else:
                top = 'dino_blk_grass_v%d' % ((x * 7 + y * 3) % 3) if (x + y) % 5 else 'dino_blk_dirt_v%d' % (x % 3)
            items.append([top, x, y, 0])
            items.append(['dino_fill_dirt', x, y, -1])

    def put(name, pal, x, y, at=None):
        o = {'palette': pal} if pal else {}
        if at:
            o['at'] = at
        items.append([name, x, y, 0, o])
    put('tommy_test', 'tommy', 5, 3)
    put('trex_run_n_02', 'trex', 0, 0, at=[5.0, 1.0, 0.0])
    put('raptor_run_w_02', 'raptor', 6, 4)
    put('trike_run_e_03', 'trike', 1, 5)
    put('trike_howl_s_01', 'trike', 6, 7)
    for k in range(3):
        put('compy_run_e_%02d' % k, 'compy', 0, 0, at=[0.8 + 0.55 * k, 2.5 - 0.15 * k, 0.0])
    put('ptero_dive_w_01', 'ptero', 3, 5)
    put('raptor_notice_s_01', 'raptor', 1, 7)
    put('dino_fern', None, 7, 8)
    put('dino_cycad', None, 0, 8)
    put('dino_vent_04', None, 7, 2)
    put('dino_boulder', None, 4, 8)
    sc = {'assets': ['.', rel(BOS), rel(TILES), rel(os.path.join(ASSETS, '_test'))], 'size': [368, 448],
          'look_at': [4.0, 4.2, 0.4], 'bg': [24, 18, 16], 'palettes': pals, 'items': items}
    path = os.path.join(MON, '_scene_dino.json')
    with open(path, 'w') as fh:
        json.dump(sc, fh, indent=1)
    for outname, scale in (('_scene_dino.png', 1), ('_scene_dino_2x.png', 2)):
        subprocess.check_call([sys.executable, MS.COMPOSE, path, os.path.join(MON, outname), str(scale)])


def _cost(d, meta, pred):
    sys.path.insert(0, os.path.join(ASSETS, '..', 'tools'))
    import pack_assets as PA
    tot = 0
    n = 0
    for nm, info in sorted(meta.items()):
        if nm.startswith('_') or not isinstance(info, dict) or 'files' not in info or not pred(nm, info):
            continue
        _, data = PA.frame_main(d, info)
        tot += len(data)
        f = info['files']
        if 'sh' in f:
            tot += len(PA.frame_plane(d, f['sh'], info))
        if 'gl' in f:
            tot += len(PA.frame_glow(d, f['gl'], info))
        n += 1
    return n, tot


def budget():
    rows = []
    with open(os.path.join(MON, 'meta.json')) as fh:
        meta = json.load(fh)
    for who in MONSTERS:
        n, t = _cost(MON, meta, lambda k, v: v.get('monster') == who and v.get('kind') == 'char')
        nc, tc = _cost(MON, meta, lambda k, v: k == 'card_' + who)
        rows.append((who, n, t, tc, 380))
    bmeta = _boss_view(BOS)
    n, t = _cost(BOS, bmeta, lambda k, v: v.get('boss') == 'trex' and v.get('kind') == 'char')
    nc, tc = _cost(BOS, bmeta, lambda k, v: k == 'card_trex')
    rows.append(('trex', n, t, tc, 800))
    with open(os.path.join(TILES, 'meta.json')) as fh:
        tmeta = json.load(fh)
    groups = [('blocks + fills', lambda k, v: v['kind'] == 'tile' and '_lava_' not in k),
              ('surfaces', lambda k, v: v['kind'] == 'surf'),
              ('props', lambda k, v: v['kind'] == 'prop'),
              ('bridges', lambda k, v: v['kind'] == 'bridge'),
              ('lava traps', lambda k, v: '_lava_' in k),
              ('moving things', lambda k, v: v['kind'] == 'dyn' and '_lava_' not in k)]
    print('%-16s %6s %10s %10s %8s' % ('what', 'frames', 'KB', 'card KB', 'budget'))
    for who, n, t, tc, b in rows:
        print('%-16s %6d %10.1f %10.1f %8d' % (who, n, t / 1024, tc / 1024, b))
    zt = 0
    for g, pred in groups:
        n, t = _cost(TILES, tmeta, lambda k, v, pred=pred: pred(k, v))
        zt += t
        print('%-16s %6d %10.1f' % ('dino ' + g, n, t / 1024))
    print('%-16s %6s %10.1f %10s %8d' % ('dino zone total', '', zt / 1024, '', 1400))


if __name__ == '__main__':
    what = sys.argv[1] if len(sys.argv) > 1 else 'sheets'
    {'sheets': sheets, 'scene': scene, 'budget': budget}[what]()
