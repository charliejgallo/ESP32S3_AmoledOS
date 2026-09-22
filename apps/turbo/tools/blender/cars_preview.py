#!/usr/bin/env python3
"""
cars_preview.py - colour the vehicle passes rendered by cars.py the way the
watch will (colour = palette[id] * shade / 196) over grey asphalt with the
cast shadow, and lay them out as contact sheets (full size and 1/4 size).

    python3 cars_preview.py [--dir ../../assets/cars] [--out ../../assets/cars_preview] [--qa]

Writes <out>/near_sheet.png, near_sheet_q.png (1/4), far_sheet.png,
far_sheet_q.png, one near_<car>_y3.png per car, and with --qa an id
false-colour sheet (red = shade pixel with no id).  A vehicle with extra
colourings (VARIANTS, e.g. the hearse in black and in white) also gets its own
small far_<car>.png: every colouring at full size, 1/4, and scaled so the
vehicle is 60 and 40 px wide (the sizes traffic is seen at).
"""
import argparse
import json
import os
import numpy as np
from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
PLAYER = ['wedge', 'muscle', 'rally', 'pickup']
TRAFFIC = ['sedan', 'compact', 'van', 'truck', 'hearse']


def hexc(s):
    s = s.lstrip('#')
    return tuple(int(s[i:i + 2], 16) for i in (0, 2, 4))


# ids: 1 paint A, 2 paint B, 3 glass, 4 chrome, 5 black trim, 6 tyre, 7 rim,
# 8 tail, 9 head, 10 plate, 11 interior, 12 underbody, 13 amber, 14 extra
COMMON = {3: '#6E8AA8', 4: '#E4E8EE', 5: '#262628', 6: '#2A2A2C', 7: '#C8CCD2', 8: '#D01818',
          9: '#FFF6D8', 10: '#E8E8E0', 11: '#3A3634', 12: '#1C1C1E', 13: '#FF9A10', 14: '#303034',
          15: '#FF00FF'}


def pal(a, b, **over):
    p = dict(COMMON)
    p[1], p[2] = a, b
    for k, v in over.items():
        p[int(k[1:])] = v
    return p


PALETTES = {
    'wedge': pal('#D8141C', '#1A1A1C'),                    # red, black strakes
    'muscle': pal('#1C3FB0', '#F2F2F2'),                   # blue, white stripes
    'rally': pal('#F2F2F0', '#D8141C'),                    # white, red band
    'pickup': pal('#F0C418', '#2A2A2C', i14='#1E1E20'),     # yellow, dark lower body
    'sedan': pal('#2E6B3A', '#2E6B3A'),
    'compact': pal('#E0E0DA', '#B01818'),
    'van': pal('#8C1F2A', '#D8D8D2'),
    'truck': pal('#F2F2EE', '#3060C0'),
    'hearse': pal('#202224', '#7A1E1E'),                   # black, dark red pinstripe
}
# extra colourings shown next to the default one: [(tag, palette)]
VARIANTS = {
    'hearse': [('white', pal('#E8E8E4', '#5A5E64'))],     # white, grey pinstripe
}
BRAKE = '#FF5A48'

QA = ['#000000', '#FF4040', '#40A0FF', '#80E0FF', '#FFFFFF', '#303030', '#606060', '#C0C040',
      '#FF00FF', '#FFFF80', '#F0F0F0', '#804000', '#101010', '#FFA000', '#00C060', '#FF00FF']


def asphalt(h, w, lines=None):
    rng = np.random.default_rng(3)
    img = np.full((h, w, 3), hexc('#6A6A6E'), np.float32)
    img += rng.normal(0, 3.0, (h, w, 1))
    if lines:
        for x0, x1 in lines:
            img[:, x0:x1] = hexc('#D8D8D0')
    return img


def load(path, mode):
    if not os.path.exists(path):
        return None
    return np.asarray(Image.open(path).convert(mode)).astype(np.float32)


def compose(d, name, palette, ref=196.0, brake=False, bg=None):
    shade = load(os.path.join(d, name + '_shade.png'), 'RGBA')
    ids = load(os.path.join(d, name + '_id.png'), 'L')
    if shade is None or ids is None:
        return None
    h, w = ids.shape
    img = asphalt(h, w) if bg is None else bg.copy()
    sh = load(os.path.join(d, name + '_shadow.png'), 'L')
    if sh is not None:
        img *= (1 - 0.62 * sh[..., None] / 255.0)
    lut = np.zeros((16, 3), np.float32)
    for k, v in palette.items():
        lut[k] = hexc(v)
    if brake:
        lut[8] = hexc(BRAKE)
    col = lut[(ids / 16).round().astype(int).clip(0, 15)]
    c = np.clip(col * shade[..., :3] / ref, 0, 255)
    if brake:
        m = (ids / 16).round() == 8
        c[m] = np.clip(c[m] * 1.6 + 40, 0, 255)
    a = shade[..., 3:4] / 255.0
    img = img * (1 - a) + c * a
    return np.clip(img, 0, 255).astype(np.uint8)


def qa_ids(d, name):
    ids = load(os.path.join(d, name + '_id.png'), 'L')
    shade = load(os.path.join(d, name + '_shade.png'), 'RGBA')
    if ids is None:
        return None, 0
    lut = np.array([hexc(c) for c in QA], np.uint8)
    img = lut[(ids / 16).round().astype(int).clip(0, 15)].copy()
    bad = (shade[..., 3] > 0) & (ids == 0)
    img[bad] = (255, 0, 0)
    img[(ids == 0) & ~bad] = (60, 60, 64)
    return img, int(bad.sum())


def sheet(tiles, labels, cols, title, scale=1.0):
    tiles = [t if scale == 1.0 else np.asarray(Image.fromarray(t).resize(
        (max(1, int(t.shape[1] * scale)), max(1, int(t.shape[0] * scale))), Image.LANCZOS)) for t in tiles]
    th = max(t.shape[0] for t in tiles)
    tw = max(t.shape[1] for t in tiles)
    rows = (len(tiles) + cols - 1) // cols
    pad = 4
    lab = 12 if scale >= 0.5 else 0
    W = cols * (tw + pad) + pad
    H = rows * (th + pad + lab) + pad + 16
    out = Image.new('RGB', (W, H), (24, 24, 28))
    dr = ImageDraw.Draw(out)
    dr.text((pad, 2), title, fill=(230, 230, 230))
    for i, (t, l) in enumerate(zip(tiles, labels)):
        r, c = divmod(i, cols)
        x, y = pad + c * (tw + pad), 16 + pad + r * (th + pad + lab)
        out.paste(Image.fromarray(t), (x, y))
        if lab:
            dr.text((x + 2, y + th), l, fill=(200, 200, 200))
    return out


def crop(img, box, m=6):
    x0, y0, x1, y1 = box
    return img[max(0, y0 - m):y1 + m + 1, max(0, x0 - m):x1 + m + 1]


def vehicle_sheet(d, out, car, meta):
    """far_<car>.png: each colouring's l/c/r cropped to the vehicle + shadow,
    at full size, then 1/4 and at 60 / 40 px wide (same scale for l/c/r)"""
    rows = [('', PALETTES[car])] + VARIANTS.get(car, [])
    ims = {}
    for tag, p in rows:
        for v in 'lcr':
            im = compose(d, 'far_%s_%s' % (car, v), p)
            if im is not None:
                ims[tag, v] = im
    if not ims:
        return
    # crop every view to its non-empty area (car or shadow) plus a margin
    boxes = {}
    for v in 'lcr':
        sh = load(os.path.join(d, 'far_%s_%s_shade.png' % (car, v)), 'RGBA')
        sd = load(os.path.join(d, 'far_%s_%s_shadow.png' % (car, v)), 'L')
        if sh is None:
            continue
        m = (sh[..., 3] > 0) | ((sd > 8) if sd is not None else False)
        ys, xs = np.nonzero(m)
        boxes[v] = (int(xs.min()), int(ys.min()), int(xs.max()), int(ys.max()))
    rc = meta.get('renders', {}).get('far_%s_c' % car, {}).get('bbox')
    car_w = (rc[2] - rc[0] + 1) if rc else 200
    blocks = []
    for label, sc in (('full (100 px/m)', 1.0), ('1/4', 0.25), ('60 px wide', 60.0 / car_w),
                      ('40 px wide', 40.0 / car_w)):
        tiles, labs = [], []
        for tag, _ in rows:
            for v in 'lcr':
                if (tag, v) not in ims:
                    continue
                t = crop(ims[tag, v], boxes[v])
                if sc != 1.0:
                    t = np.asarray(Image.fromarray(t).resize((max(1, round(t.shape[1] * sc)),
                                                              max(1, round(t.shape[0] * sc))), Image.LANCZOS))
                tiles.append(t)
                labs.append('%s %s' % (v, tag or 'default'))
        blocks.append(sheet(tiles, labs if sc == 1.0 else [''] * len(labs), 3 if sc == 1.0 else 6,
                            '%s far, %s' % (car, label)))
    W = max(b.size[0] for b in blocks)
    H = sum(b.size[1] for b in blocks)
    img = Image.new('RGB', (W, H), (24, 24, 28))
    y = 0
    for b in blocks:
        img.paste(b, (0, y))
        y += b.size[1]
    img.save(os.path.join(out, 'far_%s.png' % car))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--dir', default=os.path.join(HERE, '..', '..', 'assets', 'cars'))
    ap.add_argument('--out', default=os.path.join(HERE, '..', '..', 'assets', 'cars_preview'))
    ap.add_argument('--qa', action='store_true')
    ap.add_argument('--cars', default='')
    args = ap.parse_args()
    d, out = os.path.abspath(args.dir), os.path.abspath(args.out)
    os.makedirs(out, exist_ok=True)
    only = args.cars.split(',') if args.cars else None

    # near: the full frame over a road with lane lines, like the game
    tiles, labels, qtiles, qlabels = [], [], [], []
    for car in PLAYER:
        if only and car not in only:
            continue
        for k in range(7):
            name = 'near_%s_y%d' % (car, k)
            bg = asphalt(448, 368, lines=[(40, 46), (322, 328)])
            im = compose(d, name, PALETTES[car], bg=bg, brake=(k == 6))
            if im is None:
                continue
            tiles.append(im)
            labels.append(name + (' (brake)' if k == 6 else ''))
            if k == 3:
                Image.fromarray(im).save(os.path.join(out, name + '.png'))
            if args.qa:
                q, bad = qa_ids(d, name)
                qtiles.append(q)
                qlabels.append('%s %d uncovered' % (name, bad))
    if tiles:
        sheet(tiles, labels, 7, 'near renders (k=0..6, yaw -24..+24; last = brake lights on)').save(
            os.path.join(out, 'near_sheet.png'))
        sheet(tiles, labels, 7, 'near 1/4', 0.25).save(os.path.join(out, 'near_sheet_q.png'))
        # the straight frames only, bigger, for a close look
        st = [t for t, l in zip(tiles, labels) if '_y3' in l]
        sheet([t[150:400, 40:328] for t in st], [l for l in labels if '_y3' in l], 4,
              'near straight, cropped').save(os.path.join(out, 'near_straight.png'))
    if qtiles:
        sheet(qtiles, qlabels, 7, 'near ids (red = uncovered)').save(os.path.join(out, 'qa_near_ids.png'))

    # far: cropped to the vehicle, all at the same scale
    tiles, labels, qtiles = [], [], []
    meta = {}
    mp = os.path.join(d, 'meta.json')
    if os.path.exists(mp):
        meta = json.load(open(mp))
    for car in PLAYER + TRAFFIC:
        if only and car not in only:
            continue
        for tag, p in [('', PALETTES[car])] + VARIANTS.get(car, []):
            for v in 'lcr':
                name = 'far_%s_%s' % (car, v)
                im = compose(d, name, p)
                if im is None:
                    continue
                tiles.append(im)
                labels.append(name + (' ' + tag if tag else ''))
                if args.qa and not tag:
                    qtiles.append(qa_ids(d, name)[0])
        if car in VARIANTS:
            vehicle_sheet(d, out, car, meta)
    if tiles:
        sheet(tiles, labels, 6, 'far renders (100 px/m at the rear)').save(os.path.join(out, 'far_sheet.png'))
        sheet(tiles, labels, 6, 'far 1/4 (25 px/m: traffic at ~48 m)', 0.25).save(
            os.path.join(out, 'far_sheet_q.png'))
        sheet(tiles, labels, 6, 'far 1/2', 0.5).save(os.path.join(out, 'far_sheet_h.png'))
    if qtiles:
        sheet(qtiles, [l for l in labels if ' ' not in l], 6, 'far ids').save(os.path.join(out, 'qa_far_ids.png'))
    print('previews in', out)


if __name__ == '__main__':
    main()
