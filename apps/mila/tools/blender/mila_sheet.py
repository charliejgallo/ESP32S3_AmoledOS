#!/usr/bin/env python3
"""Mila's contact sheet: python3 mila_sheet.py ../../assets/mila

Writes <dir>/_sheet.png: one row per body animation and direction (frames
aligned on their anchor, shadow under them, native pixels), the shop
turntable, and the 14 items worn on c_sit_s_00 and walk_e_03 (palettes from
palettes.json). Plain Python + PIL, no Blender.
"""
import json
import os
import re
import sys

import numpy as np
from PIL import Image, ImageDraw

D = os.path.abspath(sys.argv[1])
META = json.load(open(os.path.join(D, 'meta.json')))
PAL = json.load(open(os.path.join(D, 'palettes.json')))
BG = (196, 150, 104)
ITEMS = ['hat_bow', 'hat_party', 'hat_crown', 'hat_beret', 'hat_beanie', 'hat_flower', 'hat_bunny', 'hat_witch',
         'neck_bell', 'neck_fish', 'neck_pearls', 'neck_bandana', 'neck_dots', 'neck_bow']
_cache = {}


def load(n):
    if n in _cache:
        return _cache[n]
    m = META[n]
    img = np.asarray(Image.open(os.path.join(D, m['files']['img'])).convert('RGBA')).astype(np.float32)
    col, a = img[..., :3], img[..., 3:] / 255.0
    if 'id' in m['files']:
        ids = np.asarray(Image.open(os.path.join(D, m['files']['id']))) // 16
        lut = np.zeros((16, 3), np.float32)
        for k, c in PAL[m['item']].items():
            lut[int(k)] = c
        col = np.clip(lut[ids] * col / 196.0, 0, 255)
    sh = None
    if 'sh' in m['files']:
        sh = np.asarray(Image.open(os.path.join(D, m['files']['sh']))).astype(np.float32)[..., None] / 255 * 0.55
    _cache[n] = (m, col, a, sh)
    return _cache[n]


def cell(names):
    """Composite a body frame and its layers: (rgb, alpha-ish mask, ax, ay)."""
    ms = [META[n] for n in names]
    L = max(m['ax'] for m in ms)
    T = max(m['ay'] for m in ms)
    R = max(m['w'] - m['ax'] for m in ms)
    B = max(m['h'] - m['ay'] for m in ms)
    rgb = np.zeros((T + B, L + R, 3), np.float32)
    rgb[:] = BG
    for n in names:
        m, col, a, sh = load(n)
        y0, x0 = T - m['ay'], L - m['ax']
        reg = rgb[y0:y0 + m['h'], x0:x0 + m['w']]
        if sh is not None:
            reg[:] = reg * (1 - sh)
        reg[:] = reg * (1 - a) + col * a
    return rgb, L, T


def row(cells, gap=6):
    L = max(c[1] for c in cells)
    T = max(c[2] for c in cells)
    R = max(c[0].shape[1] - c[1] for c in cells)
    B = max(c[0].shape[0] - c[2] for c in cells)
    out = np.zeros((T + B, (L + R + gap) * len(cells), 3), np.float32)
    out[:] = BG
    for i, (rgb, l, t) in enumerate(cells):
        x0 = i * (L + R + gap) + L - l
        out[T - t:T - t + rgb.shape[0], x0:x0 + rgb.shape[1]] = rgb
        out[:, (i + 1) * (L + R + gap) - gap:(i + 1) * (L + R + gap)] = (60, 56, 64)
    return out


def main():
    rows = []
    heads = []
    groups = {}
    for k, v in META.items():
        if k.startswith('_') or v.get('kind') != 'char':
            continue
        mm = re.fullmatch(r'(mila_.+)_(\d\d)', k)
        groups.setdefault(mm.group(1), []).append(k)
    order = ['idle', 'walk', 'push', 'win', 'yawn', 'c_walk', 'c_run', 'c_sit', 'c_sleep', 'c_belly', 'c_pounce',
             'c_bat', 'c_jump', 'c_scratch', 'c_eat', 'c_groom', 'c_meow', 'c_purr', 'c_peek', 'c_lie', 'turn']

    def key(g):
        a = META[groups[g][0]].get('anim')
        return (order.index(a) if a in order else 99, g)
    for g in sorted(groups, key=key):
        fr = sorted(groups[g])
        rows.append(row([cell([n]) for n in fr]))
        heads.append('%s  (%d x %d ms)' % (g, len(fr), META[fr[0]].get('ms', 0)))
    for base in ('c_sit_s_00', 'walk_e_03', 'idle_s_00'):
        cells = [cell(['mila_' + base])] + [cell(['mila_' + base, it + '_' + base]) for it in ITEMS]
        rows.append(row(cells))
        heads.append('items on %s: none, %s' % (base, ', '.join(ITEMS)))
    W = max(r.shape[1] for r in rows) + 8
    H = sum(r.shape[0] + 18 for r in rows) + 8
    sheet = np.zeros((H, W, 3), np.float32)
    sheet[:] = (32, 30, 36)
    y = 4
    ys = []
    for r in rows:
        ys.append(y)
        sheet[y + 14:y + 14 + r.shape[0], 4:4 + r.shape[1]] = r
        y += r.shape[0] + 18
    im = Image.fromarray(np.clip(sheet, 0, 255).astype(np.uint8))
    dr = ImageDraw.Draw(im)
    for yy, h in zip(ys, heads):
        dr.text((6, yy + 1), h, fill=(235, 230, 220))
    im.save(os.path.join(D, '_sheet.png'))
    print('wrote _sheet.png', im.size)


main()
