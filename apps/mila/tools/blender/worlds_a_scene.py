#!/usr/bin/env python3
"""Mila - sample screens of the living room, kitchen and garden kits.

    python3 worlds_a_scene.py [living,kitchen,garden]

Builds a small level piece per world from a text grid and composites it with
../compose.py (the watch's depth-tested placement) into
assets/<world>/_sample.png (368 x 448) and _sample_2x.png.

compose.py cannot load a shadow-only sprite (<name>_sh has no colour image)
nor a glow, so this script draws those two itself with compose's Canvas and
placement. Shadows also differ from compose.py on purpose: compose multiplies
each sprite's shadow into the picture as it goes, so where the shadows of two
wall cells overlap the ground gets darkened twice (a saw-tooth along every
wall). Here every shadow goes into one shadow buffer by MAX, and the buffer
darkens once, at the end, only the pixels a floor-kind sprite (kind 'tile':
floors, targets, plates, puddles) wrote last: props are never darkened by
their own footprint's shadow. That is what the engine should do.
A lamp's glow brightens the ground around it, added last.
"""
import os
import sys
import zlib

import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..'))
import compose as K  # noqa: E402

ASSETS = os.path.normpath(os.path.join(HERE, '..', '..', 'assets'))

# the level pieces: row 0 is the back. '#' wall, ' ' floor, '.' target,
# '$' object, '*' object on a target, '~' wet floor, '_' plate up,
# '=' plate down, 'G' gate (h) closed, 'g' gate (h) open, capitals: props
LEVELS = {
    'living': dict(rows=[
        '######',
        '#AR .#',
        '#  $ #',
        '#P  B#',
        '#SS# #',
        '# *$ #',
        '#   K#',
        '######',
    ], props={'A': 'living_prop_armchair', 'B': 'living_prop_table', 'R': 'living_prop_records',
              'P': 'living_prop_plant', 'K': 'living_prop_bookcase'},
        props2={'S': 'living_prop2_sofa'}, look=(3.0, 4.1)),
    'kitchen': dict(rows=[
        '######',
        '#A B #',
        '#  . #',
        '#~~$C#',
        '#~~# #',
        '#* ~T#',
        '#SS  #',
        '#N$ .#',
        '######',
    ], props={'A': 'kitchen_prop_fridge', 'B': 'kitchen_prop_stove', 'C': 'kitchen_prop_cabinet',
              'T': 'kitchen_prop_stool', 'N': 'kitchen_prop_bin'},
        props2={'S': 'kitchen_prop2_counter'}, look=(3.0, 4.6)),
    'garden': dict(rows=[
        '#######',
        '#A .#B#',
        '#  $V #',
        '#U  #W#',
        '##G##~#',
        '#_ = D#',
        '#* $  #',
        '#SS#g##',
        '#######',
    ], props={'A': 'garden_prop_gnome', 'B': 'garden_prop_bush', 'U': 'garden_prop_stump',
              'W': 'garden_prop_watering', 'D': 'garden_prop_birdbath'},
        props2={'S': 'garden_prop2_bench'}, look=(3.3, 4.6)),
}

GATES = {'G': ('h', '00'), 'g': ('h', '03'), 'V': ('v', '00'), 'v': ('v', '03')}


def h(x, y, k=0):
    return zlib.crc32(b'%d,%d,%d' % (x, y, k))


class Extra:
    """Shadow-only and glow sprites (compose.Sprite needs a colour image)."""

    def __init__(self, d, info):
        f = info['files']
        self.info = info
        self.ax, self.ay = info['ax'], info['ay']
        self.sh = np.asarray(Image.open(os.path.join(d, f['sh'])).convert('L')).astype(np.float32) if 'sh' in f else None
        self.gl = np.asarray(Image.open(os.path.join(d, f['gl'])).convert('RGB')).astype(np.float32) if 'gl' in f else None


def region(cv, ax, ay, w, hh, wx, wy, wz):
    sx, sy = K.screen(wx, wy, wz)
    x0 = int(round(sx - cv.ox)) - ax
    y0 = int(round(sy - cv.oy)) - ay
    cx0, cy0 = max(0, x0), max(0, y0)
    cx1, cy1 = min(cv.w, x0 + w), min(cv.h, y0 + hh)
    if cx0 >= cx1 or cy0 >= cy1:
        return None
    return (slice(cy0 - y0, cy1 - y0), slice(cx0 - x0, cx1 - x0)), (slice(cy0, cy1), slice(cx0, cx1))


def add_shadow(buf, cv, sh, ax, ay, wx, wy, wz):
    r = region(cv, ax, ay, sh.shape[1], sh.shape[0], wx, wy, wz)
    if r is None:
        return
    sl, dst = r
    buf[dst] = np.maximum(buf[dst], sh[sl] / 255.0)


def draw_glow(cv, ex, wx, wy, wz, gm, gain=1.3):
    r = region(cv, ex.ax, ex.ay, ex.gl.shape[1], ex.gl.shape[0], wx, wy, wz)
    if r is None:
        return
    sl, dst = r
    add = cv.rgb[dst] * (ex.gl[sl] / 255.0) * gain
    cv.rgb[dst] = np.clip(cv.rgb[dst] + add * gm[dst][..., None], 0, 255)


def build(world):
    L = LEVELS[world]
    rows = L['rows']
    H = len(rows)
    d = os.path.join(ASSETS, world)
    lib = K.Library([d])
    extras = {}
    for k, (dd, info) in lib.s.items():
        if 'sh' in info['files'] or 'gl' in info['files']:
            extras[k] = Extra(dd, info)
    names = set(lib.s)

    def variants(base):
        return sorted(n for n in names if n.startswith(base + '_v') and not n.endswith('_sh'))

    floors = variants('%s_floor' % world)
    walls = variants('%s_wall' % world)
    wets = variants('%s_wet' % world)
    objs = variants('%s_obj' % world)
    items_floor, items = [], []
    shadows, glows = [], []
    oi = 0
    for r, row in enumerate(rows):
        y = H - 1 - r
        for x, c in enumerate(row):
            if c == '#':
                items.append((walls[h(x, y) % len(walls)], x, y))
                continue
            if c == '-':
                continue
            if c == '~' and wets:
                items_floor.append((wets[h(x, y) % len(wets)], x, y))
            else:
                items_floor.append((floors[h(x, y) % len(floors)], x, y))
            if c in '.*':
                items_floor.append(('%s_target' % world, x, y))
            if c == '_':
                items_floor.append(('garden_plate_up', x, y))
            if c == '=':
                items_floor.append(('garden_plate_down', x, y))
            if c in GATES:
                ax, f = GATES[c]
                items.append(('%s_gate_%s_%s' % (world, ax, f), x, y))
                shadows.append(('%s_gate_%s_sh_%s' % (world, ax, f), x, y, True))
            if c in L.get('props2', {}) and (x == 0 or row[x - 1] != c or (x - row.index(c)) % 2 == 0):
                items.append((L['props2'][c], x, y))
            if c in L['props']:
                n = L['props'][c]
                items.append((n, x, y))
                if n in extras and extras[n].gl is not None:
                    glows.append((n, x, y))
            if c in '$*':
                v = objs[oi % len(objs)]
                oi += 1
                on = v.replace('_obj_', '_obj_on_')
                if c == '*' and on in names:
                    items.append((on, x, y))
                else:
                    items.append((v, x, y))
                    shadows.append((v + '_sh', x, y, True))
    return lib, extras, items_floor, items, shadows, glows


def render(world):
    L = LEVELS[world]
    lib, extras, items_floor, items, shadows, glows = build(world)
    W, Hh = 368, 448
    lx, ly = L['look']
    cx, cy = K.screen(lx, ly, 0.0)
    cv = K.Canvas(W, Hh, (int(round(cx)) - W // 2, int(round(cy)) - Hh // 2), L.get('bg', (0, 0, 0)))
    K.ZOOM[0] = 1.0
    seq = []
    for n, x, y in items_floor:
        seq.append((0, n, x, y))
    for n, x, y, _ in shadows:
        seq.append((1, n, x, y))
    for n, x, y in items:
        seq.append((2, n, x, y))
    # far to near; at equal depth: floors, then shadows, then things
    seq.sort(key=lambda t: (-K.depth(t[2] + 0.5, t[3] + 0.5, 0.0), t[0]))
    missing = set()
    shbuf = np.zeros((Hh, W), np.float32)
    ground = np.zeros((Hh, W), bool)     # pixels last written by a floor-kind sprite
    for kind, n, x, y in seq:
        wx, wy = x + 0.5, y + 0.5
        if kind == 1:
            ex = extras.get(n)
            if ex is None or ex.sh is None:
                missing.add(n)
                continue
            add_shadow(shbuf, cv, ex.sh, ex.ax, ex.ay, wx, wy, 0.0)
            continue
        spr = lib.get(n)
        if spr is None:
            missing.add(n)
            continue
        if spr.sh is not None:
            add_shadow(shbuf, cv, spr.sh, spr.ax, spr.ay, wx, wy, 0.0)
        z0 = cv.z.copy()
        cv.draw(spr, wx, wy, 0.0, shadow=False)
        ch = cv.z != z0
        ground[ch] = spr.info.get('kind') == 'tile'
    gm = ground
    cv.rgb *= (1 - 0.55 * shbuf * gm)[..., None]
    for n, x, y in glows:
        draw_glow(cv, extras[n], x + 0.5, y + 0.5, 0.0, gm)
    if missing:
        print('missing:', sorted(missing))
    img = cv.image()
    out = os.path.join(ASSETS, world)
    img.save(os.path.join(out, '_sample.png'))
    img.resize((W * 2, Hh * 2), Image.NEAREST).save(os.path.join(out, '_sample_2x.png'))
    print('wrote', os.path.join(out, '_sample.png'))


def sheet(world, scale=2, cols_px=1100):
    """assets/<world>/_sheet.png: every sprite of the kit on a neutral
    ground, 2x, with its name and size; shadow-only sprites as a grey map."""
    from PIL import ImageDraw
    import json
    d = os.path.join(ASSETS, world)
    meta = json.load(open(os.path.join(d, 'meta.json')))
    cells = []
    for n in sorted(k for k in meta if not k.startswith('_')):
        info = meta[n]
        f = info['files']
        if 'img' in f:
            im = Image.open(os.path.join(d, f['img'])).convert('RGBA')
            bg = Image.new('RGBA', im.size, (92, 92, 104, 255))
            bg.alpha_composite(im)
        else:
            sh = np.asarray(Image.open(os.path.join(d, f['sh'])).convert('L')).astype(np.float32)
            v = (200 - sh * 0.55 * 200 / 255).astype(np.uint8)
            bg = Image.fromarray(np.dstack([v, v, v, np.full_like(v, 255)]))
        im = bg.resize((bg.width * scale, bg.height * scale), Image.NEAREST)
        cells.append((n, '%dx%d' % (info['w'], info['h']), im))
    pad, lab = 8, 26
    x = y = pad
    rowh = 0
    pos = []
    for n, sz, im in cells:
        w = max(im.width, 150)
        if x + w > cols_px:
            x = pad
            y += rowh + lab + pad
            rowh = 0
        pos.append((x, y, n, sz, im))
        x += w + pad
        rowh = max(rowh, im.height)
    out = Image.new('RGB', (cols_px, y + rowh + lab + pad), (28, 28, 34))
    dr = ImageDraw.Draw(out)
    for x, y, n, sz, im in pos:
        out.paste(im.convert('RGB'), (x, y))
        dr.text((x, y + im.height + 2), n, fill=(230, 230, 230))
        dr.text((x, y + im.height + 13), sz, fill=(150, 150, 160))
    out.save(os.path.join(d, '_sheet.png'))
    print('wrote', os.path.join(d, '_sheet.png'), len(cells), 'sprites')


if __name__ == '__main__':
    ws = sys.argv[1].split(',') if len(sys.argv) > 1 else ['living', 'kitchen', 'garden']
    for w in ws:
        render(w)
        sheet(w)
