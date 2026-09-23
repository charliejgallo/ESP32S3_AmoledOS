#!/usr/bin/env python3
"""Mila - previews of the attic and roofs kits (plain python3, no Blender).

    python3 wb_preview.py sheet attic      -> assets/attic/_sheet.png (every sprite, 3x)
    python3 wb_preview.py sample attic     -> assets/attic/_sample.png + _sample_2x.png

The sample is a small level piece put together with ../compose.py, the way
the watch will draw it. compose.py cannot load a shadow-only sprite (it wants
an image), so the objects' separate `_sh` sprites and the `_gl` glow pools
are laid on here, on top of compose's own drawing, with the same rules
(shadows darken what lies on the ground; glow is added to the ground).
"""
import json
import os
import sys

import numpy as np
from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..'))
import compose as CP          # noqa: E402

ASSETS = os.path.normpath(os.path.join(HERE, '..', '..', 'assets'))

LEVELS = {
    # rows from the back (top of the screen) to the front. Letters:
    #  # wall  . target  * object on target  $ object  v/h cat flap
    #  o hole  O hole with a crate in it  b ball  ~ puddle
    #  _ plate  P object on a (pressed) plate  G/g gate in a left-right /
    #  up-down wall (drawn open)  A-F one-cell props, KK the 2-cell prop
    'attic': [
        '######',
        '#A. P#',
        '# $ D#',
        '##v#G#',
        '#  h #',
        '#C*# #',
        '#KK E#',
        '######',
    ],
    'roofs': [
        '######',
        '#A . #',
        '# o b#',
        '##v#E#',
        '#DO~ #',
        '#B $ #',
        '#KK*C#',
        '######',
    ],
}
PROPS = {'attic': {'A': 'attic_prop_horse', 'B': 'attic_prop_armchair', 'C': 'attic_prop_books',
                   'D': 'attic_prop_dressform', 'E': 'attic_prop_lamp', 'K': 'attic_prop2_trunk'},
         'roofs': {'A': 'roofs_prop_dovecote', 'B': 'roofs_prop_skylight', 'C': 'roofs_prop_antenna',
                   'D': 'roofs_prop_dish', 'E': 'roofs_prop_tank', 'K': 'roofs_prop2_chimney'}}
BG = {'attic': [22, 16, 14], 'roofs': [10, 12, 24]}


def _h(x, y, n):
    return ((x * 7349 + y * 1931 + x * y * 131) >> 2) % n


class Scene:
    def __init__(self, world):
        self.world = world
        self.dir = os.path.join(ASSETS, world)
        with open(os.path.join(self.dir, 'meta.json')) as f:
            self.meta = {k: v for k, v in json.load(f).items() if not k.startswith('_')}
        self.lib = CP.Library([self.dir])

    def has(self, name):
        return name in self.meta

    def variants(self, base):
        return sorted(k for k in self.meta if k.startswith(base + '_v') and not k.endswith('_sh')
                      and k[len(base) + 2:].isdigit())


def build_items(sc, rows):
    w = sc.world
    H = len(rows)
    items, shadows, glows = [], [], []
    floors = sc.variants(w + '_floor')
    walls = sc.variants(w + '_wall')
    objs = sc.variants(w + '_obj')
    oi = 0
    for r, row in enumerate(rows):
        y = H - 1 - r
        for x, c in enumerate(row):
            if c == '-':
                continue
            if c == '#':
                items.append([walls[_h(x, y, len(walls))], x, y, 0])
                continue
            if c == '.' or c == '*':
                items.append([w + '_target', x, y, 0])
            elif c == 'o':
                items.append([w + '_hole', x, y, 0])
            elif c == 'O':
                items.append([w + '_hole_crate_v0', x, y, 0])
            elif c == '~':
                items.append([w + '_wet_v0', x, y, 0])
            elif c == '_':
                items.append([w + '_plate_up', x, y, 0])
            elif c == 'P':
                items.append([w + '_plate_down', x, y, 0])
            else:
                items.append([floors[_h(x, y, len(floors))], x, y, 0])
            if c in 'vh':
                items.append(['%s_flap_%s_00' % (w, c), x, y, 0])
            if c in 'Gg':
                items.append(['%s_gate_%s_03' % (w, 'h' if c == 'G' else 'v'), x, y, 0])
            if c in PROPS.get(w, {}) and not (c == 'K' and x > 0 and row[x - 1] == 'K'):
                items.append([PROPS[w][c], x, y, 0])
            if c in '$*P':
                name = objs[oi % len(objs)]
                oi += 1
                items.append([name, x, y, 0])
                shadows.append((name + '_sh', x, y))
            if c == 'b':
                items.append([w + '_ball_v0', x, y, 0])
                shadows.append((w + '_ball_v0_sh', x, y))
    for it in items:
        if sc.has(it[0]) and 'gl' in sc.meta[it[0]]['files']:
            glows.append((it[0], it[1], it[2]))
    return items, shadows, glows


def compose_level(world, rows, look_at, size=(368, 448)):
    sc = Scene(world)
    items, shadows, glows = build_items(sc, rows)
    W, Hh = size
    lx, ly, lz = look_at
    cx, cy = CP.screen(lx, ly, lz)
    CP.ZOOM[0] = 1.0
    cv = CP.Canvas(W, Hh, (cx - W // 2, cy - Hh // 2), BG[world])
    # static first (floors, walls, props), then the glow on the ground, the
    # things' shadows, and the things (as the watch: background cache, then
    # moving sprites)
    moving = [it for it in items if '_obj_' in it[0] or '_ball_' in it[0]]
    static = [it for it in items if it not in moving]

    def draw(lst):
        lst = sorted(lst, key=lambda t: -CP.depth(t[1] + 0.5, t[2] + 0.5, 0))
        for name, x, y, fl in lst:
            spr = sc.lib.get(name)
            if spr is None:
                print('missing', name)
                continue
            cv.draw(spr, x + 0.5, y + 0.5, fl * CP.FLOOR_M)
    draw(static)
    for name, x, y in glows:
        info = sc.meta[name]
        gl = np.asarray(Image.open(os.path.join(sc.dir, info['files']['gl'])).convert('RGB')).astype(np.float32)
        _blit_ground(cv, gl, info, x, y, mode='add')
    for name, x, y in shadows:
        if not sc.has(name):
            continue
        info = sc.meta[name]
        sh = np.asarray(Image.open(os.path.join(sc.dir, info['files']['sh'])).convert('L')).astype(np.float32)
        _blit_ground(cv, sh, info, x, y, mode='shadow')
    draw(moving)
    return cv.image()


def _blit_ground(cv, img, info, x, y, mode):
    wx, wy, wz = x + 0.5, y + 0.5, 0.0
    sx, sy = CP.screen(wx, wy, wz)
    x0 = int(round(sx - cv.ox)) - info['ax']
    y0 = int(round(sy - cv.oy)) - info['ay']
    h, w = img.shape[:2]
    cx0, cy0 = max(0, x0), max(0, y0)
    cx1, cy1 = min(cv.w, x0 + w), min(cv.h, y0 + h)
    if cx0 >= cx1 or cy0 >= cy1:
        return
    sl = (slice(cy0 - y0, cy1 - y0), slice(cx0 - x0, cx1 - x0))
    dst = (slice(cy0, cy1), slice(cx0, cx1))
    dg = CP.depth(wx, wy, wz)
    near = np.abs(cv.z[dst] - dg) < 6           # the ground (within ~0.2 m), not wall tops
    if mode == 'shadow':
        a = img[sl] / 255.0 * 0.55 * near
        cv.rgb[dst] *= (1 - a[..., None])
    else:
        # light on a white ground: the ground's colour times the light
        g = img[sl] / 255.0 * near[..., None]
        cv.rgb[dst] = np.clip(cv.rgb[dst] * (1 + g * 1.2) + g * 18, 0, 255)


def sheet(world, scale=3):
    sc = Scene(world)
    names = sorted(sc.meta)
    tiles = []
    for n in names:
        info = sc.meta[n]
        f = info['files']
        if 'img' in f:
            im = Image.open(os.path.join(sc.dir, f['img'])).convert('RGBA')
        elif 'sh' in f:
            a = Image.open(os.path.join(sc.dir, f['sh'])).convert('L')
            im = Image.new('RGBA', a.size, (0, 0, 0, 0))
            im.putalpha(a)
        else:
            continue
        tiles.append((n, im))
    pad = 8
    cols = 6
    cw = max(t[1].width for t in tiles) * scale + pad
    ch = max(t[1].height for t in tiles) * scale + pad + 14
    rows = (len(tiles) + cols - 1) // cols
    out = Image.new('RGB', (cols * cw, rows * ch), (40, 40, 48))
    d = ImageDraw.Draw(out)
    for i, (n, im) in enumerate(tiles):
        cx, cy = (i % cols) * cw, (i // cols) * ch
        big = im.resize((im.width * scale, im.height * scale), Image.NEAREST)
        bgc = Image.new('RGBA', big.size, (92, 92, 100, 255))
        bgc.alpha_composite(big)
        out.paste(bgc.convert('RGB'), (cx + pad // 2, cy + 14))
        d.text((cx + 4, cy + 1), '%s %dx%d' % (n, im.width, im.height), fill=(230, 230, 230))
    path = os.path.join(sc.dir, '_sheet.png')
    out.save(path)
    print('wrote', path)


def sample(world):
    rows = LEVELS[world]
    H = len(rows)
    W = max(len(r) for r in rows)
    img = compose_level(world, rows, (W / 2.0, H / 2.0 - 0.35, 0.0))
    d = os.path.join(ASSETS, world)
    img.save(os.path.join(d, '_sample.png'))
    img.resize((img.width * 2, img.height * 2), Image.NEAREST).save(os.path.join(d, '_sample_2x.png'))
    print('wrote', os.path.join(d, '_sample.png'))


if __name__ == '__main__':
    what, world = sys.argv[1], sys.argv[2]
    {'sheet': sheet, 'sample': sample}[what](world)
