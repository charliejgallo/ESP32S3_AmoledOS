#!/usr/bin/env python3
"""chars_sheet.py - look at Tommy's sprites the way the watch will colour them.

    python3 chars_sheet.py DIR out.png [--look default] [--scale 3] [--cols 8]
            [--names spec,spec,...] [--with cap_cap,back_cape] [--label]

A spec is a body frame with optional layers: `tommy_hop_e_03+cap_beanie+hand_torch`
(the layer's sprite is <layer>_<anim>_<dir>_<nn>, the body's name without
`tommy_`). --with adds layers to every Tommy. Pets take their own palette.
Without --names: every body frame in meta.json. Each cell is composited like
the watch does (depth-tested, body first, then its layers) on a flat
background, recoloured with a look from DIR/palettes.json.
"""
import argparse
import json
import os
import sys

from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..'))
import compose  # noqa: E402


def world_at(sx, sy):
    """World point on z = 0 that lands on screen offset (sx, sy)."""
    return ((42 * sx + 20 * sy) / 2800.0, (14 * sx - 60 * sy) / 2800.0)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('dir')
    ap.add_argument('out')
    ap.add_argument('--look', default='default')
    ap.add_argument('--scale', type=int, default=3)
    ap.add_argument('--names', default='')
    ap.add_argument('--with', dest='withl', default='')
    ap.add_argument('--cols', type=int, default=8)
    ap.add_argument('--cell', default='92,100')
    ap.add_argument('--bg', default='70,86,70')
    ap.add_argument('--label', action='store_true')
    a = ap.parse_args()
    d = os.path.abspath(a.dir)
    with open(os.path.join(d, 'meta.json')) as fh:
        meta = json.load(fh)
    with open(os.path.join(d, 'palettes.json')) as fh:
        looks = json.load(fh)['looks']
    look = dict(looks['default'])
    look.update(looks.get(a.look, {}))
    specs = [n for n in a.names.split(',') if n] or sorted(
        k for k, v in meta.items() if not k.startswith('_') and v.get('kind') == 'char')
    extra = [x for x in a.withl.split(',') if x]
    lib = compose.Library([d])
    cw, ch = [int(x) for x in a.cell.split(',')]
    cols = min(a.cols, len(specs))
    rows = (len(specs) + cols - 1) // cols
    bg = [int(x) for x in a.bg.split(',')]
    big = [x for x in specs if '_turn_' in x]
    if big:
        cw, ch = cw * 2, ch * 2
    cv = compose.Canvas(cw * cols, ch * rows, (0, 0), bg)
    for k, spec in enumerate(specs):
        parts = spec.split('+')
        body = parts[0]
        cx, cy = (k % cols) * cw + cw // 2 - cw // 8, (k // cols) * ch + ch - ch // 4
        wx, wy = world_at(cx, cy)
        if body.startswith('pet_'):
            pal = look[body.split('_')[0] + '_' + body.split('_')[1]]
        else:
            pal = look['tommy']
        if body in lib.s:
            cv.draw(lib.get(body), wx, wy, 0.0, pal)
        if body.startswith('tommy_'):
            rest = body[len('tommy_'):]
            for lp in parts[1:] + extra:
                ln = '%s_%s' % (lp, rest)
                if ln in lib.s:
                    cv.draw(lib.get(ln), wx, wy, 0.0, look.get(lp))
    img = cv.image()
    if a.scale > 1:
        img = img.resize((img.width * a.scale, img.height * a.scale), Image.NEAREST)
    if a.label:
        dr = ImageDraw.Draw(img)
        for k, spec in enumerate(specs):
            dr.text(((k % cols) * cw * a.scale + 3, (k // cols) * ch * a.scale + 2),
                    spec.replace('tommy_', ''), fill=(255, 255, 255))
    img.save(a.out)
    print('wrote', a.out, img.size)


if __name__ == '__main__':
    main()
