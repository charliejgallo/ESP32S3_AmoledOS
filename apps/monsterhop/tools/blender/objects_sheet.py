#!/usr/bin/env python3
"""Monster Hop - contact sheet of a sprite dir (plain python3).

    python3 objects_sheet.py ../../assets/objects [scale] [cols]

Every sprite of <dir>/meta.json, cropped to its ink, on a dark background with
its name and size -> <dir>/_sheet.png.
"""
import json
import os
import sys

from PIL import Image, ImageDraw

d = sys.argv[1]
scale = int(sys.argv[2]) if len(sys.argv) > 2 else 2
cols = int(sys.argv[3]) if len(sys.argv) > 3 else 8
meta = json.load(open(os.path.join(d, 'meta.json')))
names = sorted(k for k in meta if not k.startswith('_'))
cells = []
for n in names:
    im = Image.open(os.path.join(d, meta[n]['files']['img'])).convert('RGBA')
    bb = im.getbbox() or (0, 0, 1, 1)
    im = im.crop(bb)
    if im.width * scale > 400:
        s = max(1, 400 // im.width)
    else:
        s = scale
    cells.append((n, im.resize((im.width * s, im.height * s), Image.NEAREST), meta[n]))
cw = max(c[1].width for c in cells if c[1].width <= 400) + 12
ch = max(c[1].height for c in cells if c[1].height <= 400) + 26
cw, ch = min(cw, 260), min(ch, 260)
rows = []
row, x = [], 0
for c in cells:
    w = max(cw, c[1].width + 12)
    if row and (len(row) >= cols or x + w > cols * cw):
        rows.append(row)
        row, x = [], 0
    row.append((c, w))
    x += w
rows.append(row)
W = max(sum(w for _, w in r) for r in rows)
H = sum(max(max(ch, c[1].height + 26) for c, _ in r) for r in rows)
sheet = Image.new('RGB', (W, H), (18, 20, 28))
dr = ImageDraw.Draw(sheet)
y = 0
for r in rows:
    rh = max(max(ch, c[1].height + 26) for c, _ in r)
    x = 0
    for (n, im, info), w in r:
        dr.rectangle((x + 2, y + 2, x + w - 3, y + rh - 3), fill=(28, 32, 44))
        sheet.paste(im, (x + (w - im.width) // 2, y + 4 + (rh - 26 - im.height) // 2), im)
        dr.text((x + 5, y + rh - 20), n, fill=(230, 230, 240))
        dr.text((x + 5, y + rh - 11), '%dx%d' % (info['w'], info['h']), fill=(140, 150, 170))
        x += w
    y += rh
out = os.path.join(d, '_sheet.png')
sheet.save(out)
print('wrote', out, sheet.size, len(names), 'sprites')
