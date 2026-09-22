#!/usr/bin/env python3
"""chars_scene.py - the preview scenes and contact sheets of assets/chars.

    python3 chars_scene.py [../../assets/chars]

Writes, in the chars dir:
  _scene.json/.png/_2x.png  a game situation on the test floor: Tommy
                            mid-hop with his dog behind, kids in other gear
                            pushing, pulling, waving, sinking, dizzy...
  _skins.json/.png/_2x.png  the shop's odd skins and hair on the same Tommy
                            (skin must stay a clean region)
  _sample.*                 the phase-1 sample, re-rendered from the new sprites
  _sheet_body.png, _sheet_layers.png, _sheet_pets.png, _sheet_turn.png
Layers are always listed after their body (the watch must draw them after).
"""
import json
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..'))
import compose  # noqa: E402

TEST = os.path.normpath(os.path.join(HERE, '..', '..', 'assets', '_test'))


def load(d):
    with open(os.path.join(d, 'palettes.json')) as fh:
        P = json.load(fh)
    looks = P['looks']
    pals = {}
    for ln, L in looks.items():
        for part, p in L.items():
            pals['%s.%s' % (ln, part)] = p
    return P, looks, pals


def kid(items, frame, at, gear=(), look='default', looks=None, pals=None, tint=None):
    """Tommy's body frame plus layers (e.g. 'cap_cap', 'hand_torch')."""
    key = look + '.tommy'
    if tint:
        key = tint
    items.append(['tommy_' + frame, 0, 0, 0, {'palette': key, 'at': at}])
    for g in gear:
        lk = look if (look + '.' + g) in pals else 'default'
        items.append(['%s_%s' % (g, frame), 0, 0, 0, {'palette': '%s.%s' % (lk, g), 'at': at}])


def save(d, name, sc):
    with open(os.path.join(d, name + '.json'), 'w') as fh:
        json.dump(sc, fh, indent=1)
    img = compose.render_scene(sc, d)
    img.save(os.path.join(d, name + '.png'))
    img.resize((img.width * 2, img.height * 2), compose.Image.NEAREST).save(os.path.join(d, name + '_2x.png'))
    print('wrote', name)


def scene(d):
    P, looks, pals = load(d)
    it = []
    # a little level: two raised terraces at the back, a pit on the left
    F = compose.FLOOR_M
    LX, LY = 4.0, 5.2

    def x(y, dx):         # rows centred on screen
        return LX - (y - LY) / 3.0 + dx
    heights = ["000000000", "000000000", "000000000", "000000000", "0.0000000",
               "011111000", "011111000", "022222000", "022222000", "000000000"]
    # the hero, mid-hop, his dog one cell behind; a friend with a backwards cap and a flashlight
    kid(it, 'hop_e_03', [x(1.5, -0.9), 1.5, 0.35], ('cap_cap',), pals=pals)
    it.append(['pet_dog_hop_e_02', 0, 0, 0, {'palette': 'default.pet_dog', 'at': [x(1.5, -1.9), 1.5, 0.12]}])
    kid(it, 'idle_s_00', [x(1.5, 1.2), 1.5, 0.0], ('cap_back', 'hand_flashlight'), look='dark_green', pals=pals)
    # pushing with a torch tucked; a blond kid in a crown and cape waving, with his cat
    kid(it, 'push_e_01', [x(3.0, -1.2), 3.0, 0.0], ('cap_beanie', 'back_backpack', 'hand_torch'), pals=pals)
    kid(it, 'win_s_05', [x(3.0, 0.9), 3.0, 0.0], ('cap_crown', 'back_cape'), look='blond_purple', pals=pals)
    it.append(['pet_cat_idle_w_00', 0, 0, 0, {'palette': 'default.pet_cat', 'at': [x(3.0, 1.7), 2.8, 0.0]}])
    # pulling a lever, then the terraces: a super jump, a balloon kid with his bat
    kid(it, 'use_n_02', [x(4.2, 0.2), 4.2, 0.0], ('cap_bucket', 'back_tank', 'hand_bucket'), pals=pals)
    kid(it, 'super_n_03', [x(5.6, -0.8), 5.6, F + 0.40], ('cap_propeller', 'back_wings'), pals=pals)
    kid(it, 'idle_w_00', [x(5.6, 0.7), 5.6, F], ('cap_cap', 'hand_balloon'), pals=pals)
    it.append(['pet_bat_idle_w_00', 0, 0, 0, {'palette': 'default.pet_bat', 'at': [x(5.6, 1.4), 5.6, F]}])
    kid(it, 'hurt_s_04', [x(7.4, -0.6), 7.4, 2 * F], ('cap_cap',), pals=pals)
    kid(it, 'idle_n_00', [x(7.4, 0.8), 7.4, 2 * F], ('cap_beanie', 'back_cape'), pals=pals)
    sc = {"assets": [os.path.relpath(TEST, d), "."], "size": [368, 448], "look_at": [LX, LY, 0.35],
          "bg": [16, 20, 28], "palettes": pals,
          "grid": {"w": 9, "h": 10, "floor": "blk_grass", "fill": "blk_dirt", "heights": heights},
          "items": it}
    save(d, '_scene', sc)


def skins(d):
    P, looks, pals = load(d)
    it = []
    base = dict(looks['default']['tommy'])
    names = [k for k in P['skins'] if not k.startswith('_')]
    hair = P['hair']
    hairs = ['brown', 'black', 'blond', 'red', 'blue', 'white', 'black', 'brown', 'blond', 'blue']
    for k, sk in enumerate(names):
        p = dict(base)
        p['1'] = P['skins'][sk]
        p['12'] = P['skins'][sk] if sk not in ('light', 'medium') else base['12']
        p['2'] = hair[hairs[k % len(hairs)]]
        pals['skin.' + sk] = p
        x = 1.0 + (k % 5) * 1.0 + (0.35 if k >= 5 else 0.0)
        y = 1.3 + (k // 5) * 2.2
        kid(it, 'idle_s_00', [x, y, 0.0], ('cap_cap',) if k % 2 else (), pals=pals, tint='skin.' + sk)
    sc = {"assets": [os.path.relpath(TEST, d), "."], "size": [368, 260], "look_at": [3.3, 2.4, 0.4],
          "bg": [16, 20, 28], "palettes": pals,
          "grid": {"w": 8, "h": 6, "floor": "blk_grass", "fill": "blk_dirt", "heights": ["00000000"] * 6},
          "items": it}
    save(d, '_skins', sc)


def sheets(d):
    meta = json.load(open(os.path.join(d, 'meta.json')))
    sh = os.path.join(HERE, 'chars_sheet.py')

    def run(out, names, cols, *extra):
        subprocess.run([sys.executable, sh, d, os.path.join(d, out), '--cols', str(cols), '--scale', '2',
                        '--label', '--names', ','.join(names)] + list(extra), check=True)
    body = sorted((k for k, v in meta.items() if k.startswith('tommy_') and '_turn_' not in k),
                  key=lambda k: (list(('idle', 'hop', 'super', 'push', 'use', 'win', 'hurt', 'sink', 'fall')).index(
                      meta[k]['anim']), meta[k].get('dir', ''), meta[k]['frame']))
    run('_sheet_body.png', body, 12)
    frames = ['idle_s_00', 'idle_e_00', 'idle_n_00', 'idle_w_00', 'hop_e_03', 'super_s_03', 'push_e_00',
              'use_s_02', 'win_s_05', 'hurt_s_04', 'sink_s_00', 'fall_s_00']
    layers = sorted({v['layer'] + '_' + v['style'] for v in meta.values() if isinstance(v, dict) and v.get('layer')},
                    key=lambda s: (['cap', 'back', 'hand'].index(s.split('_')[0]), s))
    run('_sheet_layers.png', ['tommy_%s+%s' % (f, L) for L in layers for f in frames], 12)
    pets = sorted(k for k, v in meta.items() if k.startswith('pet_') and '_turn_' not in k)
    run('_sheet_pets.png', pets, 16, '--cell', '70,80')
    turn = ['tommy_turn_%02d+cap_cap' % i for i in range(12)] + \
           ['tommy_turn_%02d+cap_crown+back_cape+hand_torch' % i for i in range(12)] + \
           ['pet_%s_turn_%02d' % (p, i) for p in ('dog', 'cat', 'bat') for i in range(0, 12, 3)]
    run('_sheet_turn.png', turn, 12, '--cell', '80,80')


def main():
    d = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, '..', '..', 'assets', 'chars'))
    scene(d)
    skins(d)
    subprocess.run([sys.executable, os.path.join(HERE, 'chars_sample.py'), d], check=True)
    sheets(d)


if __name__ == '__main__':
    main()
