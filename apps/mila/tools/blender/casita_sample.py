#!/usr/bin/env python3
"""Mila - the casita sample screens and contact sheet, with ../compose.py.

    python3 casita_sample.py [../../assets/casita]

Writes into <dir>:
  _sample.png / _sample_2x.png   night, every toy in its spot, Mila sitting on the rug
  _sample_day.png (+ _2x)        the day window, the door open, the gift opening
  _sample_hud.png                _sample.png with the HUD bands of the approved mock
  _sheet.png                     every sprite, labelled
The room is drawn first (the engine's background, drawn once), then every
other sprite far to near by its anchor, depth-tested (compose.py's Canvas).
Mila's frames come from ../mila (the Mila artist's casita frames).
"""
import json
import os
import sys

from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..'))
import compose as CP  # noqa: E402

D = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, '../../assets/casita'))
MILA = os.path.join(os.path.dirname(D), 'mila')
W, H = 368, 448
FRONT_Y = 398          # the screen row of the floor's front edge (world y = 0, z = 0)

TOYS = [
    ('toy_post', (3.0, 2.4, 0)),
    ('toy_box', (2.9, 0.6, 0)),
    ('toy_fishbowl_00', (0.35, 1.5, 0)),
    ('toy_tunnel', (1.6, 2.5, 0)),
    ('toy_hammock', (2.4, 2.6, 0)),
    ('toy_catnip', (3.15, 1.4, 0)),
    ('toy_mouse_00', (2.5, 1.8, 0)),
    ('toy_yarn', (1.1, 0.95, 0)),
    ('toy_ball', (2.2, 1.0, 0)),
]
FIXED = [
    ('casita_door_00', (0.65, 3.0, 0)),
    ('casita_bed', (0.6, 2.3, 0)),
    ('casita_bowls', (0.45, 0.45, 0)),
]


def screen(lib, items):
    CP.ZOOM[0] = 1.5
    ox = CP.screen(1.7, 0, 0)[0] - W / 2          # the room centred left-right
    oy = CP.screen(0, 0, 0)[1] - FRONT_Y
    cv = CP.Canvas(W, H, (ox, oy), (0, 0, 0))
    cv.draw(lib.get('casita_room'), 0, 0, 0)
    for name, (x, y, z) in sorted(items, key=lambda t: -CP.depth(*t[1])):
        spr = lib.get(name)
        if spr is None:
            print('missing', name)
            continue
        cv.draw(spr, x, y, z)
    return cv.image()


def save2(img, name):
    img.save(os.path.join(D, name + '.png'))
    img.resize((W * 2, H * 2), Image.NEAREST).save(os.path.join(D, name + '_2x.png'))


def hud(img):
    hud = img.convert('RGBA')
    ov = Image.new('RGBA', hud.size, (0, 0, 0, 0))
    d = ImageDraw.Draw(ov)
    d.rounded_rectangle((10, 8, 88, 34), 13, fill=(20, 20, 28, 190))
    d.rounded_rectangle((262, 8, 356, 34), 13, fill=(20, 20, 28, 190))
    d.rounded_rectangle((8, 386, 360, 442), 20, fill=(20, 20, 28, 200))
    d.text((34, 14), '245', fill=(255, 255, 255, 255))
    d.text((276, 14), 'Mila  happy', fill=(255, 255, 255, 255))
    for x, t in ((55, 'Play'), (170, 'Shop'), (285, 'Settings')):
        d.text((x, 420), t, fill=(255, 255, 255, 255))
    return Image.alpha_composite(hud, ov).convert('RGB')


def sheet():
    with open(os.path.join(D, 'meta.json')) as f:
        meta = {k: v for k, v in json.load(f).items() if not k.startswith('_')}
    names = [n for n in sorted(meta) if meta[n]['kind'] != 'room']
    pad, lab, maxw = 8, 12, 760
    tiles = []
    for n in names:
        im = Image.open(os.path.join(D, meta[n]['files']['img'])).convert('RGBA')
        if 'sh' in meta[n]['files']:     # show the shadow under the sprite, as the watch would
            sh = Image.open(os.path.join(D, meta[n]['files']['sh'])).convert('L')
            base = Image.new('RGBA', im.size, (0, 0, 0, 0))
            base.putalpha(sh.point(lambda v: int(v * 0.55)))
            base.alpha_composite(im)
            im = base
        tiles.append((n, im))
    rows, row, x = [], [], 0
    for n, im in tiles:
        w = max(im.width, 6 * len(n)) + pad
        if x + w > maxw and row:
            rows.append(row)
            row, x = [], 0
        row.append((n, im, w))
        x += w
    rows.append(row)
    hgt = sum(max(im.height for _, im, _ in r) + lab + pad for r in rows) + pad
    room = Image.open(os.path.join(D, 'casita_room.png')).convert('RGBA')
    out = Image.new('RGBA', (maxw + room.width + 3 * pad, max(hgt, room.height + 2 * pad + lab)), (46, 44, 52, 255))
    d = ImageDraw.Draw(out)
    y = pad
    for r in rows:
        x = pad
        rh = max(im.height for _, im, _ in r)
        for n, im, w in r:
            out.alpha_composite(im, (x, y))
            d.text((x, y + rh + 1), n, fill=(230, 230, 235, 255))
            x += w
        y += rh + lab + pad
    out.alpha_composite(room, (maxw + 2 * pad, pad))
    d.text((maxw + 2 * pad, pad + room.height + 1), 'casita_room', fill=(230, 230, 235, 255))
    out.convert('RGB').save(os.path.join(D, '_sheet.png'))


def main():
    dirs = [D] + ([MILA] if os.path.exists(os.path.join(MILA, 'meta.json')) else [])
    lib = CP.Library(dirs)
    mila = [('mila_c_sit_s_00', (1.75, 1.3, 0))] if lib.get('mila_c_sit_s_00') else []
    night = screen(lib, FIXED + TOYS + [('casita_gift', (1.3, 2.05, 0))] + mila)
    save2(night, '_sample')
    hud(night).save(os.path.join(D, '_sample_hud.png'))
    sleep = [('mila_c_sleep_s_00', (0.6, 2.3, 0.08))] if lib.get('mila_c_sleep_s_00') else []
    day_fixed = [('casita_window_day', (2.5, 3.0, 0.75)), ('casita_door_03', (0.65, 3.0, 0)),
                 ('casita_bed', (0.6, 2.3, 0)), ('casita_bowls', (0.45, 0.45, 0)),
                 ('casita_gift_open_02', (1.3, 2.05, 0)), ('toy_feather_00', (1.9, 1.0, 0))]
    day = screen(lib, day_fixed + TOYS + sleep)
    save2(day, '_sample_day')
    sheet()
    print('wrote', os.path.join(D, '_sample.png'), '_sample_day.png _sheet.png')


main()
