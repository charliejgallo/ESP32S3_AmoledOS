#!/usr/bin/env python3
"""chars_sample.py - the phase-1 style sample scene for Tommy.

    python3 chars_sample.py [../../assets/chars]

Writes <dir>/_sample.json and renders it with tools/compose.py to
<dir>/_sample.png (368 x 448) and <dir>/_sample_2x.png (nearest-neighbour).
On the neutral test floor (assets/_test): the three looks from palettes.json
in front (the default with the dog), the four idle facings, and the hop
frames 00-05 facing s and facing e. Every Tommy carries the default cap
layer, listed after his body (a layer must be drawn after its body).
"""
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..'))
import compose  # noqa: E402

TEST = os.path.normpath(os.path.join(HERE, '..', '..', 'assets', '_test'))


def main():
    d = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, '..', '..', 'assets', 'chars'))
    with open(os.path.join(d, 'palettes.json')) as fh:
        looks = json.load(fh)['looks']
    pals = {}
    for ln, L in looks.items():
        for part, p in L.items():
            pals['%s.%s' % (ln, part)] = p
    items = []

    def tommy(frame, x, y, look='default', at=None):
        opt = {'palette': look + '.tommy'}
        copt = {'palette': look + '.cap_cap'}
        if at:
            opt['at'] = copt['at'] = at
        items.append(['tommy_' + frame, x, y, 0, opt])
        items.append(['cap_cap_' + frame, x, y, 0, copt])

    LX, LY = 4.5, 3.9          # look_at: every row is centred on it on screen

    def row(y, n, step):
        """World x of n actors at spacing step on row y, centred on screen."""
        xc = LX - (y - LY) * 20.0 / 60.0
        return [xc + step * (k - (n - 1) / 2.0) for k in range(n)]

    # front row: the dog next to the default Tommy, then the other two looks
    xs = row(1.2, 4, 0.95)
    items.append(['pet_dog_idle_s_00', 0, 0, 0, {'palette': 'default.pet_dog', 'at': [xs[0] + 0.2, 1.45, 0.0]}])
    for x, look in zip(xs[1:], ('default', 'dark_green', 'blond_purple')):
        tommy('idle_s_00', 0, 0, look, at=[x, 1.2, 0.0])
    # the four facings
    for x, dd in zip(row(2.9, 4, 0.95), 'nesw'):
        tommy('idle_%s_00' % dd, 0, 0, at=[x, 2.9, 0.0])
    # hop frames, facing s then facing e
    for k, x in enumerate(row(4.6, 6, 0.78)):
        tommy('hop_s_%02d' % k, 0, 0, at=[x, 4.6, 0.0])
    for k, x in enumerate(row(6.3, 6, 0.78)):
        tommy('hop_e_%02d' % k, 0, 0, at=[x, 6.3, 0.0])
    sc = {"assets": [os.path.relpath(TEST, d), "."],
          "size": [368, 448], "look_at": [LX, LY, 0], "bg": [16, 20, 28],
          "palettes": pals,
          "grid": {"w": 9, "h": 9, "floor": "blk_grass", "fill": "blk_dirt",
                   "heights": ["000000000"] * 9},
          "items": items}
    path = os.path.join(d, '_sample.json')
    with open(path, 'w') as fh:
        json.dump(sc, fh, indent=1)
    img = compose.render_scene(sc, d)
    img.save(os.path.join(d, '_sample.png'))
    img.resize((img.width * 2, img.height * 2), compose.Image.NEAREST).save(os.path.join(d, '_sample_2x.png'))
    print('wrote', path, '_sample.png', '_sample_2x.png')


if __name__ == '__main__':
    main()
