#!/usr/bin/env python3
"""Contact sheets of the boss sprites, coloured with palettes.json the way
the watch does (palette[id] * light / 196), one row per anim and facing.

    python3 bosses_sheet.py ../../assets/bosses [scale]

Writes <dir>/_sheet_<boss>.png and <dir>/_sheet_cards.png.
"""
import json
import os
import sys

import numpy as np
from PIL import Image, ImageDraw

D = sys.argv[1]
SC = int(sys.argv[2]) if len(sys.argv) > 2 else 1
meta = json.load(open(os.path.join(D, 'meta.json')))
pals = json.load(open(os.path.join(D, 'palettes.json')))
BG = np.array((58, 74, 60), np.float32)


def colour(name):
    m = meta[name]
    f = m['files']
    img = np.asarray(Image.open(os.path.join(D, f['img'])).convert('RGBA')).astype(np.float32)
    ids = np.asarray(Image.open(os.path.join(D, f['id'])).convert('L')) // 16
    lut = np.zeros((16, 3), np.float32)
    for k, c in pals[m['boss']].items():
        lut[int(k)] = c
    col = np.clip(lut[ids] * img[..., :3] / 196.0, 0, 255)
    a = img[..., 3:] / 255.0
    bg = np.empty(col.shape, np.float32)
    bg[:] = BG
    if 'sh' in f:
        sh = np.asarray(Image.open(os.path.join(D, f['sh'])).convert('L')).astype(np.float32) / 255
        bg *= (1 - 0.55 * sh[..., None])
    return Image.fromarray((bg * (1 - a) + col * a).astype(np.uint8)), m


def sheet(names, rows_of, out):
    rows = {}
    for n in names:
        rows.setdefault(rows_of(n), []).append(n)
    ims = {n: colour(n) for n in names}
    # every frame's anchor lands on the same point of its cell
    L = max(ims[n][1]['ax'] for n in names)
    Rr = max(ims[n][1]['w'] - ims[n][1]['ax'] for n in names)
    T = max(ims[n][1]['ay'] for n in names)
    B = max(ims[n][1]['h'] - ims[n][1]['ay'] for n in names)
    cw, ch = L + Rr, T + B
    ncol = max(len(v) for v in rows.values())
    W, H = ncol * cw + 70, len(rows) * (ch + 12)
    s = Image.new('RGB', (W, H), (18, 18, 22))
    dr = ImageDraw.Draw(s)
    for r, (k, ns) in enumerate(sorted(rows.items())):
        y = r * (ch + 12)
        dr.text((2, y + 2), k, fill=(230, 230, 230))
        for c, n in enumerate(sorted(ns)):
            im, m = ims[n]
            s.paste(im, (70 + c * cw + L - m['ax'], y + 12 + T - m['ay']))
    if SC > 1:
        s = s.resize((s.width * SC, s.height * SC), Image.NEAREST)
    s.save(out)
    print('wrote', out, s.size)


chars = [k for k, v in meta.items() if not k.startswith('_') and v.get('kind') == 'char']
for b in sorted(set(meta[k]['boss'] for k in chars)):
    ns = [k for k in chars if meta[k]['boss'] == b]
    sheet(ns, lambda n: '%s %s' % (meta[n]['anim'], meta[n]['dir']), os.path.join(D, '_sheet_%s.png' % b))
cards = [k for k, v in meta.items() if not k.startswith('_') and v.get('kind') == 'card']
if cards:
    sheet(cards, lambda n: 'cards', os.path.join(D, '_sheet_cards.png'))
