#!/usr/bin/env python3
"""Monster Hop - compose scenes and contact sheets for the city and castle
tiles (plain python3: numpy + PIL). Uses tools/compose.py's Library and
Canvas, with two things compose.py does not do yet:

  * glow passes (<name>_gl.png): the light of lamps and candles on the
    ground, applied as   ground_lin *= 1 + GLOW_GAIN * glow_lin
  * shadows and glows land only on ground pixels at the plane they were
    rendered for (each canvas pixel's world point is rebuilt from its screen
    position and depth), and they are applied after every tile is drawn and
    before any prop, so a shadow falling onto a nearer cell is not lost.

The scene files are plain compose.py scenes (compose.py renders them too,
without glow); `layout` below is only how this script writes them.

    python3 zones_cc_scene.py sample city|castle     -> assets/tiles_<zone>/_sample.json/.png/_2x.png
    python3 zones_cc_scene.py sheet  city|castle [pattern]  -> assets/tiles_<zone>/_sheet.png
"""
import fnmatch
import json
import math
import os
import sys

import numpy as np
from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..'))
import compose as CP  # noqa: E402

ASSETS = os.path.normpath(os.path.join(HERE, '..', '..', 'assets'))
GLOW_GAIN = 1.5
SHADOW_K = 0.55

# a sample paint job for the recoloured runaway car (the watch picks its own)
RUNCAR = {"1": [205, 70, 55], "2": [236, 226, 196], "3": [60, 90, 110], "4": [210, 214, 218], "5": [40, 40, 44],
          "6": [38, 38, 42], "7": [255, 225, 150], "8": [140, 80, 50]}

TOMMY = {"1": [245, 190, 150], "3": [25, 20, 20], "5": [40, 120, 245], "8": [50, 60, 110],
         "11": [230, 40, 40]}

# screen/depth of a world point, as a matrix (compose.screen and compose.depth)
_M = np.array([[CP.PX_X[0], CP.PX_Y[0], 0.0],
               [CP.PX_X[1], CP.PX_Y[1], -CP.FLOOR_PX / CP.FLOOR_M],
               [CP.VIEW[0] / CP.DEPTH_UNIT, CP.VIEW[1] / CP.DEPTH_UNIT, CP.VIEW[2] / CP.DEPTH_UNIT]])
_MI = np.linalg.inv(_M)


def world_z(cv):
    """World z of every canvas pixel (nan where nothing was drawn)."""
    h, w = cv.h, cv.w
    xs = np.arange(w) + 0.5 + cv.ox
    ys = np.arange(h) + 0.5 + cv.oy
    X, Y = np.meshgrid(xs, ys)
    D = cv.z
    P = _MI @ np.stack([X.ravel(), Y.ravel(), D.ravel()])
    z = P[2].reshape(h, w)
    return np.where(D < 1e8, z, np.nan)


def srgb2lin(c):
    c = np.clip(c / 255.0, 0, 1)
    return np.where(c <= 0.04045, c / 12.92, ((c + 0.055) / 1.055) ** 2.4)


def lin2srgb(c):
    c = np.clip(c, 0, 1)
    return np.where(c <= 0.0031308, c * 12.92, 1.055 * np.power(c, 1 / 2.4) - 0.055) * 255.0


def apply_ground(cv, lib, name, wx, wy, wz, wzc):
    """Shadow and glow of sprite `name` placed at (wx, wy, wz) onto the
    ground pixels at its shadow plane."""
    spr = lib.get(name)
    d, info = lib.s[name]
    files = info['files']
    gl = None
    if 'gl' in files:
        gl = np.asarray(Image.open(os.path.join(d, files['gl'])).convert('RGB')).astype(np.float32)
    if spr.sh is None and gl is None:
        return
    sx, sy = CP.screen(wx, wy, wz)
    x0 = int(round(sx - cv.ox)) - spr.ax
    y0 = int(round(sy - cv.oy)) - spr.ay
    h, w = spr.img.shape[:2]
    cx0, cy0 = max(0, x0), max(0, y0)
    cx1, cy1 = min(cv.w, x0 + w), min(cv.h, y0 + h)
    if cx0 >= cx1 or cy0 >= cy1:
        return
    sl = (slice(cy0 - y0, cy1 - y0), slice(cx0 - x0, cx1 - x0))
    dst = (slice(cy0, cy1), slice(cx0, cx1))
    plane = wz + info.get('shadow_z', 0.0)
    zz = wzc[dst]
    # a flat tolerance: depths are stored in 1/32 m steps, so a ground pixel's
    # rebuilt z is only good to ~1-2 cm
    dz = np.abs(zz - plane)
    on = np.nan_to_num(np.clip((0.12 - dz) / 0.06, 0, 1), nan=0.0)
    # a glow also reaches a little below its plane (water in a channel)
    below = np.nan_to_num(np.clip(1.0 - (plane - zz - 0.06) / 0.2, 0, 1) * (zz < plane), nan=0.0)
    if spr.sh is not None:
        a = spr.sh[sl] / 255.0 * SHADOW_K * on
        cv.rgb[dst] *= (1 - a[..., None])
    if gl is not None:
        g = srgb2lin(gl[sl]) * np.maximum(on, below)[..., None]
        c = srgb2lin(cv.rgb[dst])
        cv.rgb[dst] = lin2srgb(c * (1.0 + GLOW_GAIN * g))


def render(sc, base_dir, glow=True):
    lib = CP.Library([os.path.join(base_dir, d) for d in sc['assets']])
    w, h = sc.get('size', [368, 448])
    lx, ly, lz = sc.get('look_at', [0, 0, 0])
    cx, cy = CP.screen(lx, ly, lz)
    cv = CP.Canvas(w, h, (cx - w // 2, cy - h // 2), sc.get('bg', [16, 16, 24]))
    items = []
    for it in sc.get('items', []):
        name, x, y, fl = it[:4]
        opt = it[4] if len(it) > 4 else {}
        if 'at' in opt:
            wx, wy, wz = opt['at']
        else:
            wx, wy, wz = x + 0.5, y + 0.5, fl * CP.FLOOR_M
        items.append((name, wx, wy, wz, opt))
    items.sort(key=lambda t: -CP.depth(t[1], t[2], t[3]))
    pals = sc.get('palettes', {})
    kind = lambda n: lib.s[n][1].get('kind', 'sprite')  # noqa: E731
    ground = [t for t in items if kind(t[0]) in ('tile', 'surf')]
    rest = [t for t in items if kind(t[0]) not in ('tile', 'surf')]
    for name, wx, wy, wz, opt in ground:
        cv.draw(lib.get(name), wx, wy, wz, None, opt.get('alpha', 1.0), shadow=False)
    wzc = world_z(cv)
    for name, wx, wy, wz, opt in rest:
        if glow:
            apply_ground(cv, lib, name, wx, wy, wz, wzc)
    for name, wx, wy, wz, opt in rest:
        pal = pals.get(opt.get('palette')) if opt.get('palette') else None
        cv.draw(lib.get(name), wx, wy, wz, pal, opt.get('alpha', 1.0), shadow=False)
    return cv.image()


# ---------------------------------------------------------------------------
# layouts -> compose items
# ---------------------------------------------------------------------------

def build_items(rows, legend, fills_front=2):
    """rows: strings from the BACK row to the FRONT row (as seen on screen);
    legend: char -> dict(h=floor, top=name, walls=[names for floors 1..h-1]
    (bottom up), fill=name, pit=surface name)."""
    H = len(rows)
    items = []
    for j, row in enumerate(rows):
        y = H - 1 - j
        for x, ch in enumerate(row):
            if ch == ' ':
                continue
            L = legend[ch]
            if L.get('pit'):
                pit = L['pit']
                if isinstance(pit, (list, tuple)):
                    pit = pit[(x * 5 + y) % len(pit)]
                items.append([pit, x, y, 0, {"at": [x + 0.5, y + 0.5, -0.18]}])
                if L.get('bridge'):
                    items.append([L['bridge'], x, y, 0])
                continue
            h = L.get('h', 0)
            fill = L['fill']
            under = fills_front if y == 0 else 1
            for k in range(-under, 0):
                items.append([L.get('fill_front', fill) if (y == 0 and k < -1) else fill, x, y, k])
            walls = L.get('walls', [])
            for k in range(0, h):
                nm = walls[k - 1] if (1 <= k <= len(walls)) else L.get('base', fill)
                if isinstance(nm, (list, tuple)):
                    nm = nm[x % len(nm)]
                items.append([nm, x, y, k])
            top = L['top']
            if isinstance(top, (list, tuple)):
                top = top[(x * 7 + y * 3) % len(top)]
            items.append([top, x, y, h])
    return items


def scene_city():
    Z = 'city_'
    walk = [Z + 'blk_sidewalk_v0', Z + 'blk_sidewalk_v0', Z + 'blk_sidewalk_v1', Z + 'blk_sidewalk_v0']
    legend = {
        's': dict(top=walk, fill=Z + 'fill_earth'),
        'a': dict(top=Z + 'blk_asphalt_v0', fill=Z + 'fill_earth'),
        'l': dict(top=Z + 'blk_asphalt_line_v0', fill=Z + 'fill_earth'),
        'w': dict(pit=Z + 'surf_sewer_v0'),
        'B': dict(h=3, top=Z + 'blk_roof_v0', fill=Z + 'fill_brick', base=Z + 'fill_brick',
                  walls=[[Z + 'blk_brick_win', Z + 'fill_brick'], Z + 'blk_brick_win']),
        'b': dict(h=1, top=Z + 'blk_brick_v0', fill=Z + 'fill_brick', base=Z + 'fill_brick'),
    }
    rows = [
        "ssBBBBBssss",
        "ssBBBBBbbss",
        "sssssssssss",
        "aaaaaaaaaaa",
        "lllllllllll",
        "aaaaaaaaaaa",
        "sssssssssss",
        "wwwwwwwwwww",
        "sssssssssss",
    ]
    items = build_items(rows, legend)
    H = len(rows)
    y = lambda r: H - 1 - r  # noqa: E731  (row index from the top -> y)
    items += [
        [Z + 'lamp', 3, y(2), 0],
        [Z + 'lamp', 7, y(6), 0],
        [Z + 'car_x', 4, y(5), 0],
        [Z + 'hydrant', 6, y(2), 0],
        [Z + 'dumpster', 7, y(2), 0],
        ['tommy_test', 5, y(6), 0, {"palette": "tommy"}],
    ]
    return {"assets": [".", "../_test"], "size": [368, 448], "look_at": [5.3, 4.5, 0.4],
            "bg": [14, 15, 20], "palettes": {"tommy": TOMMY}, "items": items}


def scene_castle():
    Z = 'castle_'
    flag = [Z + 'blk_flagstone_v0', Z + 'blk_flagstone_v1']
    legend = {
        'f': dict(top=flag, fill=Z + 'fill_stone'),
        'c': dict(top=Z + 'blk_carpet_v0', fill=Z + 'fill_stone'),
        'm': dict(pit=Z + 'surf_moat_v0'),
        'W': dict(h=3, top=Z + 'blk_battlement_v0', fill=Z + 'fill_stone', base=Z + 'fill_stone',
                  walls=[[Z + 'blk_stone_v0', Z + 'blk_stone_win'], [Z + 'blk_stone_win', Z + 'blk_stone_v0']]),
        'T': dict(h=3, top=Z + 'blk_stone_v0', fill=Z + 'fill_stone', base=Z + 'fill_stone',
                  walls=[Z + 'fill_stone', Z + 'fill_stone']),
        'w': dict(h=1, top=Z + 'blk_stone_v0', fill=Z + 'fill_stone', base=Z + 'fill_stone'),
    }
    rows = [
        "TTTTTTTTTTT",
        "TTTTTTTTTTT",
        "WWWWWWWWWWW",
        "fffffffffff",
        "ccccccccccc",
        "fffffffffff",
        "mmmmmmmmmmm",
        "fffffffffff",
    ]
    items = build_items(rows, legend, fills_front=1)
    H = len(rows)
    y = lambda r: H - 1 - r  # noqa: E731
    items += [
        [Z + 'pillar', 3, y(3), 0],
        [Z + 'pillar', 8, y(3), 0],
        [Z + 'candelabra', 5, y(3), 0],
        [Z + 'candelabra', 7, y(5), 0],
        [Z + 'coffin', 3, y(5), 0],
        [Z + 'roses', 4, y(3), 0],
        [Z + 'roses', 8, y(7), 0],
        ['tommy_test', 5, y(4), 0, {"palette": "tommy"}],
    ]
    return {"assets": [".", "../_test"], "size": [368, 448], "look_at": [5.2, 3.45, 0.45],
            "bg": [16, 12, 24], "palettes": {"tommy": TOMMY}, "items": items}


def write_sample(zone, name='_sample'):
    sc = {'city': scene_city, 'castle': scene_castle}[zone]()
    d = os.path.join(ASSETS, 'tiles_' + zone)
    with open(os.path.join(d, name + '.json'), 'w') as f:
        json.dump(sc, f, separators=(',', ':'))
    img = render(sc, d)
    img.save(os.path.join(d, name + '.png'))
    img.resize((img.width * 2, img.height * 2), Image.NEAREST).save(os.path.join(d, name + '_2x.png'))
    print('wrote', os.path.join(d, name + '.png'))


def sheet(zone, pattern='*', scale=2, bg=(22, 22, 30), out=None, kinds=None, W=1100):
    d = os.path.join(ASSETS, 'tiles_' + zone)
    with open(os.path.join(d, 'meta.json')) as f:
        meta = json.load(f)
    names = sorted(k for k in meta if not k.startswith('_') and fnmatch.fnmatch(k, pattern)
                   and (kinds is None or meta[k].get('kind') in kinds))
    cells = []
    for n in names:
        im = Image.open(os.path.join(d, meta[n]['files']['img'])).convert('RGBA')
        if 'id' in meta[n]['files']:
            a = np.asarray(im).astype(np.float32)
            ids = np.asarray(Image.open(os.path.join(d, meta[n]['files']['id'])).convert('L')) // 16
            lut = np.zeros((16, 3), np.float32)
            for k, c in RUNCAR.items():
                lut[int(k)] = c
            a[..., :3] = np.clip(lut[ids] * a[..., :3] / 196.0, 0, 255)
            im = Image.fromarray(a.astype(np.uint8))
        bb = im.getbbox() or (0, 0, 1, 1)
        im = im.crop(bb)
        cells.append((n, im.resize((im.width * scale, im.height * scale), Image.NEAREST)))
    x = y = 8
    rowh = 0
    pos = []
    for n, im in cells:
        cw = max(im.width, 150 if scale > 1 else 110) + 10
        if x + cw > W:
            x = 8
            y += rowh + 22
            rowh = 0
        pos.append((x, y, n, im))
        x += cw
        rowh = max(rowh, im.height)
    H = y + rowh + 30
    sh = Image.new('RGB', (W, H), bg)
    dr = ImageDraw.Draw(sh)
    for x, y, n, im in pos:
        sh.paste(im, (x, y), im)
        dr.text((x, y + im.height + 4), n.replace(zone + '_', ''), fill=(200, 200, 210))
    out = out or os.path.join(d, '_sheet.png')
    sh.save(out)
    print('wrote', out)


def write_scene(zone):
    """The full-set preview: a bigger game situation with most of the zone."""
    sc = {'city': scene_city_full, 'castle': scene_castle_full}[zone]()
    d = os.path.join(ASSETS, 'tiles_' + zone)
    with open(os.path.join(d, '_scene.json'), 'w') as f:
        json.dump(sc, f, separators=(',', ':'))
    img = render(sc, d)
    img.save(os.path.join(d, '_scene.png'))
    img.resize((img.width * 2, img.height * 2), Image.NEAREST).save(os.path.join(d, '_scene_2x.png'))
    print('wrote', os.path.join(d, '_scene.png'))


def scene_city_full():
    Z = 'city_'
    v = lambda t: [Z + 'blk_%s_v0' % t, Z + 'blk_%s_v0' % t, Z + 'blk_%s_v1' % t, Z + 'blk_%s_v0' % t,  # noqa: E731
                   Z + 'blk_%s_v2' % t]
    legend = {
        's': dict(top=v('sidewalk'), fill=Z + 'fill_earth'),
        'a': dict(top=v('asphalt'), fill=Z + 'fill_earth'),
        'l': dict(top=v('asphalt_line'), fill=Z + 'fill_earth'),
        'x': dict(top=v('crosswalk'), fill=Z + 'fill_earth'),
        'g': dict(top=v('grass'), fill=Z + 'fill_earth'),
        'c': dict(top=v('concrete'), fill=Z + 'fill_concrete'),
        'w': dict(pit=[Z + 'surf_sewer_v0', Z + 'surf_sewer_v1', Z + 'surf_sewer_v2'], fill=Z + 'fill_earth'),
        'W': dict(pit=Z + 'surf_sewer_v1', bridge=Z + 'bridge_y', fill=Z + 'fill_earth'),
        'B': dict(h=3, top=v('roof'), fill=Z + 'fill_brick', base=Z + 'fill_brick',
                  walls=[[Z + 'blk_brick_win', Z + 'blk_brick_boarded', Z + 'fill_brick'],
                         [Z + 'blk_brick_boarded', Z + 'blk_brick_win', Z + 'blk_brick_win']]),
        'J': dict(h=2, top=v('scrap'), fill=Z + 'fill_concrete', base=Z + 'fill_concrete',
                  walls=[[Z + 'blk_scrap_v1', Z + 'blk_scrap_v2', Z + 'blk_scrap_v0']]),
        'j': dict(h=1, top=v('scrap'), fill=Z + 'fill_concrete', base=Z + 'fill_concrete'),
        'k': dict(h=1, top=v('concrete'), fill=Z + 'fill_concrete', base=Z + 'fill_concrete'),
    }
    rows = [
        "BBBBBccgggg",
        "BBBBBccgggg",
        "BBBBBcccggg",
        "BBBBBcccggg",
        "sssssssssss",
        "aaaaaxaaaaa",
        "lllllxlllll",
        "aaaaaxaaaaa",
        "sssssssssss",
        "wwwwwwWwwww",
        "ggJjjsskcgg",
    ]
    items = build_items(rows, legend)
    H = len(rows)
    y = lambda r: H - 1 - r  # noqa: E731
    y = lambda r: H - 1 - (r + 2)  # noqa: E731  (row numbers from the sidewalk behind the street)
    items += [
        [Z + 'deadtree', 8, y(-1), 0], [Z + 'dumpster', 5, y(0), 0],
        [Z + 'fence_x', 6, y(-1), 0], [Z + 'gate_06', 7, y(-1), 0], [Z + 'fence_x', 8, y(-2), 0],
        [Z + 'tires', 9, y(-1), 0], [Z + 'crate', 6, y(0), 0],
        [Z + 'lamp', 1, y(2), 0], [Z + 'newsbox', 3, y(2), 0], [Z + 'mailbox', 4, y(2), 0],
        [Z + 'bench_x', 6, y(2), 0], [Z + 'busstop', 7, y(2), 0], [Z + 'phonebooth', 9, y(2), 0],
        [Z + 'runcar_e_01', 6, y(3), 0, {"palette": "runcar"}], [Z + 'car_x', 2, y(5), 0],
        [Z + 'vent_03', 8, y(4), 0], [Z + 'cone', 4, y(5), 0], [Z + 'cone', 4, y(4), 0],
        [Z + 'barricade_x', 2, y(6), 0], [Z + 'trashcan', 4, y(6), 0], [Z + 'hydrant', 7, y(6), 0],
        [Z + 'lamp', 8, y(6), 0], [Z + 'sign_x', 10, y(6), 0], [Z + 'tires', 1, y(8), 0],
        [Z + 'bench_y', 9, y(8), 0], [Z + 'fence_y', 10, y(8), 0], [Z + 'crate', 7, y(8), 1],
        [Z + 'cone', 8, y(8), 0], [Z + 'deadtree', 0, y(8), 0],
        ['tommy_test', 6, y(6), 0, {"palette": "tommy"}],
    ]
    return {"assets": [".", "../_test"], "size": [368, 448], "look_at": [5.6, 4.5, 0.4],
            "bg": [14, 15, 20], "palettes": {"tommy": TOMMY, "runcar": RUNCAR}, "items": items}


def scene_castle_full():
    Z = 'castle_'
    v = lambda t: [Z + 'blk_%s_v0' % t, Z + 'blk_%s_v1' % t, Z + 'blk_%s_v0' % t, Z + 'blk_%s_v2' % t]  # noqa: E731
    legend = {
        'f': dict(top=v('flagstone'), fill=Z + 'fill_stone', fill_front=Z + 'fill_rock'),
        'o': dict(top=v('woodfloor'), fill=Z + 'fill_stone'),
        'r': dict(top=[Z + 'blk_rug_v0'], fill=Z + 'fill_stone'),
        'q': dict(top=[Z + 'blk_rug_v1'], fill=Z + 'fill_stone'),
        'c': dict(top=v('carpet'), fill=Z + 'fill_stone'),
        'y': dict(top=v('carpet_y'), fill=Z + 'fill_stone'),
        'p': dict(top=[Z + 'spikes_02'], fill=Z + 'fill_stone'),
        'P': dict(top=[Z + 'spikes_00'], fill=Z + 'fill_stone'),
        'm': dict(pit=[Z + 'surf_moat_v0', Z + 'surf_moat_v1', Z + 'surf_moat_v2'], fill=Z + 'fill_stone'),
        'M': dict(pit=Z + 'surf_moat_v1', bridge=Z + 'bridge_y', fill=Z + 'fill_stone'),
        'W': dict(h=3, top=v('battlement'), fill=Z + 'fill_stone', base=Z + 'fill_stone',
                  walls=[[Z + 'blk_stone_v0', Z + 'blk_stone_win', Z + 'blk_stone_v1'],
                         [Z + 'blk_stone_win', Z + 'blk_stone_v2', Z + 'blk_stone_win']]),
        'T': dict(h=3, top=v('stone'), fill=Z + 'fill_stone', base=Z + 'fill_stone',
                  walls=[Z + 'fill_stone', Z + 'fill_stone']),
    }
    rows = [
        "TTTTTTTTTTT",
        "WWWWWfWWWWW",
        "oooooyooooo",
        "ooroqyooooo",
        "ccccccccccc",
        "ffffffpPfff",
        "mmmmmmMmmmm",
        "fffffffffff",
    ]
    items = build_items(rows, legend)
    H = len(rows)
    y = lambda r: H - 1 - r  # noqa: E731
    items += [
        [Z + 'gate_00', 5, y(1), 0],
        [Z + 'bookshelf_x', 1, y(2), 0], [Z + 'clock', 2, y(2), 0], [Z + 'banner', 4, y(2), 0],
        [Z + 'banner', 6, y(2), 0], [Z + 'throne', 7, y(2), 0], [Z + 'bookshelf_y', 9, y(2), 0],
        [Z + 'chandelier_stand', 8, y(3), 0], [Z + 'pillar', 3, y(3), 0], [Z + 'pillar', 10, y(3), 0],
        [Z + 'gargoyle', 2, y(5), 0], [Z + 'candelabra', 4, y(5), 0], [Z + 'coffin_open_03', 9, y(5), 0],
        [Z + 'crate', 3, y(5), 0],
        [Z + 'platform_01', 9, y(6), 0, {"at": [9.5, y(6) + 0.5, 0.0]}],
        [Z + 'roses', 2, y(7), 0], [Z + 'ironfence_x', 3, y(7), 0], [Z + 'ironfence_x', 4, y(7), 0],
        [Z + 'roses', 8, y(7), 0], [Z + 'ironfence_y', 10, y(7), 0],
        ['tommy_test', 5, y(5), 0, {"palette": "tommy"}],
    ]
    return {"assets": [".", "../_test"], "size": [368, 448], "look_at": [6.2, 3.7, 0.5],
            "bg": [16, 12, 24], "palettes": {"tommy": TOMMY}, "items": items}


if __name__ == '__main__':
    if len(sys.argv) < 3:
        print(__doc__)
        sys.exit(1)
    if sys.argv[1] == 'sample':
        write_sample(sys.argv[2])
    elif sys.argv[1] == 'sheet':
        sheet(sys.argv[2], sys.argv[3] if len(sys.argv) > 3 else '*',
              out=sys.argv[4] if len(sys.argv) > 4 else None)
    elif sys.argv[1] == 'sheets':
        # every sprite at 1x, then each group at 2x
        z = sys.argv[2]
        d = os.path.join(ASSETS, 'tiles_' + z)
        sheet(z, scale=1, W=1400)
        sheet(z, kinds=('tile', 'surf', 'bridge'), out=os.path.join(d, '_sheet_tiles.png'), W=1300)
        sheet(z, kinds=('prop',), out=os.path.join(d, '_sheet_props.png'), W=1300)
        sheet(z, kinds=('dyn',), out=os.path.join(d, '_sheet_dyn.png'), W=1300)
    elif sys.argv[1] == 'scene':
        write_scene(sys.argv[2])
