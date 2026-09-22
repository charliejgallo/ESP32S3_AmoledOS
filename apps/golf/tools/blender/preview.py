#!/usr/bin/env python3
"""
preview.py - colour the golfer passes rendered by golfer.py the way the watch
will (colour = palette[id] * shade / shade_ref), over mown grass with the cast
shadow, optionally with a hat layer on top, and lay them out as contact
sheets so the result can be looked at.

    python3 preview.py [--dir ../../assets/render] [--out <dir>]
                       [--seq swing,idle,cheer,sad,turn] [--palette navy,striped]
                       [--hat -1|0..5] [--zoom 2] [--crop] [--qa]

Writes (default out: tools/blender/preview) <out>/<seq>_<palette>_hat<k>.png contact sheets, plus with --qa an
id false-colour sheet and a raw shade sheet per sequence.
"""
import argparse
import json
import os
import numpy as np
from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))


def hexc(s):
    s = s.lstrip('#')
    return tuple(int(s[i:i + 2], 16) for i in (0, 2, 4))


# region ids: 1 skin, 2 hair, 3/4 shirt A/B, 5/6 trousers A/B, 7/8 shoes A/B,
# 9 belt, 10 glove, 11 club head, 12 shaft, 13 grip, 14/15 hat A/B
PALETTES = {
    'navy': {1: '#E0A882', 2: '#4A3222', 3: '#1F2E5C', 4: '#1F2E5C', 5: '#C9B48A', 6: '#C9B48A',
             7: '#F2F2F2', 8: '#5A3A24', 9: '#3B2A1E', 10: '#F4F4F4', 11: '#B8BEC6', 12: '#9AA0A8',
             13: '#2A2A2A', 14: '#F4F4F4', 15: '#1F2E5C'},
    'striped': {1: '#F0C3A0', 2: '#D9B060', 3: '#C8202A', 4: '#F5F5F5', 5: '#1E5A36', 6: '#E8C33A',
                7: '#FFFFFF', 8: '#1A1A1A', 9: '#FFFFFF', 10: '#FFFFFF', 11: '#30343A', 12: '#C0C4CA',
                13: '#D02020', 14: '#C8202A', 15: '#F5F5F5'},
    'mono': {i: '#CCCCCC' for i in range(1, 16)},
}

QA_COLOURS = ['#000000', '#FFC89A', '#6B3F1F', '#2050FF', '#90C0FF', '#E0B000', '#FF6000',
              '#FFFFFF', '#FF00FF', '#8B4513', '#00FFFF', '#FF0000', '#00FF00', '#303030',
              '#A000FF', '#FFFF00']


def grass(h, w):
    """Mown fairway: two tones in diagonal-free stripes, like the watch."""
    y = np.arange(h)[:, None]
    x = np.arange(w)[None, :]
    base = np.array(hexc('#4FA23A'), np.float32)
    alt = np.array(hexc('#469734'), np.float32)
    stripe = (((x - y * 0.35) // 28) % 2).astype(bool)
    img = np.where(stripe[..., None], alt, base) * np.ones((h, w, 1), np.float32)
    rng = np.random.default_rng(1)
    img += rng.normal(0, 2.0, img.shape)
    return img


def load(path, mode):
    if not os.path.exists(path):
        return None
    im = Image.open(path)
    return np.asarray(im.convert(mode)).astype(np.float32)


def colourise(shade, ids, pal, ref):
    lut = np.zeros((16, 3), np.float32)
    for k, v in pal.items():
        lut[k] = hexc(v)
    col = lut[(ids / 16).round().astype(int).clip(0, 15)]
    lit = shade[..., :3] / ref
    return np.clip(col * lit, 0, 255), shade[..., 3:4] / 255.0


def compose(d, seq, fi, pal, hat, ref, bg=None):
    base = os.path.join(d, '%s_%02d' % (seq, fi))
    shade = load(base + '_shade.png', 'RGBA')
    ids = load(base + '_id.png', 'L')
    if shade is None or ids is None:
        return None
    h, w = ids.shape
    img = grass(h, w) if bg is None else bg.copy()
    sh = load(base + '_shadow.png', 'L')
    if sh is not None:
        img *= (1 - 0.55 * sh[..., None] / 255.0)
    c, a = colourise(shade, ids, pal, ref)
    img = img * (1 - a) + c * a
    if hat >= 0:
        hb = os.path.join(d, 'hat%d_%s_%02d' % (hat, seq, fi))
        hs = load(hb + '_shade.png', 'RGBA')
        hi = load(hb + '_id.png', 'L')
        if hs is not None and hi is not None:
            c, a = colourise(hs, hi, pal, ref)
            img = img * (1 - a) + c * a
    return np.clip(img, 0, 255).astype(np.uint8)


def qa_ids(d, seq, fi):
    ids = load(os.path.join(d, '%s_%02d_id.png' % (seq, fi)), 'L')
    shade = load(os.path.join(d, '%s_%02d_shade.png' % (seq, fi)), 'RGBA')
    if ids is None:
        return None
    lut = np.array([hexc(c) for c in QA_COLOURS], np.uint8)
    img = lut[(ids / 16).round().astype(int).clip(0, 15)].copy()
    # red flag: shade covers but id is empty
    bad = (shade[..., 3] > 0) & (ids == 0)
    img[bad] = (255, 0, 0)
    img[(ids == 0) & ~bad] = (40, 40, 40)
    return img, int(bad.sum())


def qa_shade(d, seq, fi):
    shade = load(os.path.join(d, '%s_%02d_shade.png' % (seq, fi)), 'RGBA')
    if shade is None:
        return None
    bg = np.full(shade.shape[:2] + (3,), 90.0)
    a = shade[..., 3:4] / 255
    return (bg * (1 - a) + shade[..., :3] * a).astype(np.uint8)


def crop_box(d, seq, n):
    """Union bounding box of the golfer (and hats) over the sequence."""
    x0 = y0 = 10 ** 9
    x1 = y1 = -1
    for fi in range(n):
        for pat in ('%s_%02d_shade.png' % (seq, fi),) + tuple('hat%d_%s_%02d_shade.png' % (k, seq, fi) for k in range(6)):
            p = os.path.join(d, pat)
            if not os.path.exists(p):
                continue
            a = np.asarray(Image.open(p).convert('RGBA'))[..., 3]
            ys, xs = np.nonzero(a)
            if len(xs):
                x0, x1 = min(x0, xs.min()), max(x1, xs.max())
                y0, y1 = min(y0, ys.min()), max(y1, ys.max())
    if x1 < 0:
        return None
    return max(0, x0 - 8), max(0, y0 - 8), x1 + 9, y1 + 9


def sheet(tiles, labels, cols, zoom, title):
    th, tw = tiles[0].shape[:2]
    tw, th = tw * zoom, th * zoom
    rows = (len(tiles) + cols - 1) // cols
    pad = 4
    W = cols * (tw + pad) + pad
    H = rows * (th + pad + 14) + pad + 18
    out = Image.new('RGB', (W, H), (24, 24, 28))
    dr = ImageDraw.Draw(out)
    dr.text((pad, 3), title, fill=(230, 230, 230))
    for i, (t, l) in enumerate(zip(tiles, labels)):
        r, c = divmod(i, cols)
        x, y = pad + c * (tw + pad), 18 + pad + r * (th + pad + 14)
        im = Image.fromarray(t).resize((tw, th), Image.NEAREST)
        out.paste(im, (x, y))
        dr.text((x + 2, y + th + 1), l, fill=(200, 200, 200))
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--dir', default=os.path.join(HERE, '..', '..', 'assets', 'render'))
    ap.add_argument('--out', default='')
    ap.add_argument('--seq', default='swing,idle,cheer,sad,turn')
    ap.add_argument('--palette', default='navy,striped')
    ap.add_argument('--hat', default='')
    ap.add_argument('--zoom', type=int, default=1)
    ap.add_argument('--crop', action='store_true', help='crop to the golfer')
    ap.add_argument('--cols', type=int, default=8)
    ap.add_argument('--qa', action='store_true')
    ap.add_argument('--frames', default='')
    args = ap.parse_args()
    d = os.path.abspath(args.dir)
    out = os.path.abspath(args.out or os.path.join(HERE, 'preview'))
    os.makedirs(out, exist_ok=True)
    meta = {}
    if os.path.exists(os.path.join(d, 'meta.json')):
        meta = json.load(open(os.path.join(d, 'meta.json')))
    ref = meta.get('shade_ref', 200)
    for seq in args.seq.split(','):
        n = meta.get('sequences', {}).get(seq, {}).get('frames', 24)
        frames = [int(x) for x in args.frames.split(',')] if args.frames else list(range(n))
        box = crop_box(d, seq, n) if args.crop else None
        pals = args.palette.split(',')
        hats = [int(x) for x in args.hat.split(',')] if args.hat else [-1]
        for pi, pal in enumerate(pals):
            for hat in hats:
                tiles, labels = [], []
                for fi in frames:
                    im = compose(d, seq, fi, PALETTES[pal], hat, ref)
                    if im is None:
                        continue
                    if box:
                        im = im[box[1]:box[3], box[0]:box[2]]
                    tiles.append(im)
                    labels.append('%s %d' % (seq, fi))
                    Image.fromarray(im).save(os.path.join(out, '%s_%02d_%s_hat%d.png' % (seq, fi, pal, hat)))
                if tiles:
                    s = sheet(tiles, labels, min(args.cols, len(tiles)), args.zoom,
                              '%s  palette %s  hat %s' % (seq, pal, hat if hat >= 0 else 'none'))
                    p = os.path.join(out, 'sheet_%s_%s_hat%d.png' % (seq, pal, hat))
                    s.save(p)
                    print(p)
        if args.qa:
            tiles, labels, tiles2 = [], [], []
            for fi in frames:
                r = qa_ids(d, seq, fi)
                if r is None:
                    continue
                im, bad = r
                sh = qa_shade(d, seq, fi)
                if box:
                    im = im[box[1]:box[3], box[0]:box[2]]
                    sh = sh[box[1]:box[3], box[0]:box[2]]
                tiles.append(im)
                tiles2.append(sh)
                labels.append('%d: %d uncovered' % (fi, bad))
                if bad:
                    print('WARNING %s %d: %d shade pixels without id' % (seq, fi, bad))
            if tiles:
                p = os.path.join(out, 'qa_ids_%s.png' % seq)
                sheet(tiles, labels, min(args.cols, len(tiles)), args.zoom, seq + ' ids (red = uncovered)').save(p)
                p2 = os.path.join(out, 'qa_shade_%s.png' % seq)
                sheet(tiles2, labels, min(args.cols, len(tiles2)), args.zoom, seq + ' shade').save(p2)
                print(p, p2)


if __name__ == '__main__':
    main()
