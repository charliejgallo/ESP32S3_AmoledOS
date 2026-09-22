#!/usr/bin/env python3
"""Monster Hop - desert & forest zones: compose scenes and contact sheets.

Plain python3 (numpy + PIL). Builds the scene JSON for tools/compose.py from
a small ASCII map (what block type is on each cell and how high), renders it
at 368 x 448 and a 2x nearest-neighbour enlargement.

    python3 zones_df_scene.py desert sample     # assets/tiles_desert/_sample.{json,png}, _sample_2x.png
    python3 zones_df_scene.py forest sample
    python3 zones_df_scene.py desert sheet      # assets/tiles_desert/zone.png contact sheet

Map rows are written from the BACK of the level (far, top of the screen) to
the FRONT (y = 0). A cell is a legend letter plus a height character
(0-3, '.' for a pit: the surface of the legend entry goes in it).
"""
import json
import math
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, '..', '..'))
sys.path.insert(0, os.path.join(ROOT, 'tools'))
import compose  # noqa: E402
from PIL import Image, ImageDraw  # noqa: E402

FLOOR_M = compose.FLOOR_M
SURF_DZ = -0.18          # a surface sits this far below the ground around it
LOG_DZ = -0.02           # floating logs: their top here (SPEC 12)

TOMMY = {"1": [245, 190, 150], "2": [90, 55, 30], "3": [25, 20, 20], "4": [255, 255, 255],
         "5": [40, 120, 245], "6": [250, 250, 250], "7": [250, 200, 40], "8": [50, 60, 110],
         "9": [230, 40, 40], "10": [245, 245, 245], "11": [230, 40, 40], "12": [250, 150, 150]}


def _variants(meta, zone, typ):
    vs = sorted(k for k in meta if k.startswith('%s_blk_%s_v' % (zone, typ)))
    return vs


def _pick(vs, x, y, salt=0):
    if not vs:
        return None
    h = (x * 73856093) ^ (y * 19349663) ^ (salt * 83492791)
    return vs[(h >> 3) % len(vs)]


def build(zone, legend, types, heights, items, look_at, bg, extra_items=()):
    """legend: letter -> dict(top='sand' | block name, fill='desert_fill_sand',
    surf='desert_surf_water' (for pits), under=[names below the top, from the
    top down] (optional, overrides fill))."""
    d = os.path.join(ROOT, 'assets', 'tiles_' + zone)
    with open(os.path.join(d, 'meta.json')) as fh:
        meta = json.load(fh)
    types = list(reversed(types))
    heights = list(reversed(heights))
    H = len(types)
    W = len(types[0])
    out = []
    floor_of = {}
    for y in range(H):
        for x in range(W):
            c = types[y][x]
            hc = heights[y][x]
            if c == ' ':
                continue
            L = legend[c]
            if hc == '.':
                sv = sorted(k for k in meta if k.startswith(L['surf'] + '_v'))
                name = _pick(sv, x, y, 7)
                if name:
                    out.append([name, x, y, 0, {"at": [x + 0.5, y + 0.5, SURF_DZ]}])
                floor_of[(x, y)] = SURF_DZ / FLOOR_M
                continue
            h = int(hc)
            top = L['top']
            if top in meta:
                name = top
            else:
                name = _pick(_variants(meta, zone, top), x, y, L.get('salt', 0))
            if name is None:
                print('missing block', top)
                continue
            out.append([name, x, y, h])
            under = L.get('under')
            for k in range(h - 1, -2, -1):
                i = h - 1 - k
                if under and i < len(under):
                    nm = under[i]
                else:
                    nm = L.get('fill')
                if nm in meta:
                    out.append([nm, x, y, k])
            floor_of[(x, y)] = h
    for it in items:
        name, x, y = it[:3]
        opt = dict(it[3]) if len(it) > 3 else {}
        fl = opt.pop('floor', floor_of.get((x, y), 0))
        lib_ok = name in meta or name.startswith('tommy')
        if not lib_ok:
            print('missing sprite', name)
            continue
        out.append([name, x, y, fl, opt] if opt else [name, x, y, fl])
    out.extend(extra_items)
    return {"assets": [".", "../_test"], "size": [368, 448], "look_at": look_at, "bg": bg,
            "palettes": {"tommy": TOMMY}, "items": out}


def render_glow(sc, base_dir, gain=2.2):
    """compose.render_scene plus what the watch does with the glow passes: the
    light of lamps, fires and mushrooms added to the ground around them (the
    reference compositor ignores _gl.png)."""
    import numpy as np
    lib = compose.Library([os.path.join(base_dir, d) for d in sc['assets']])
    w, h = sc.get('size', [368, 448])
    lx, ly, lz = sc.get('look_at', [0, 0, 0])
    cx, cy = compose.screen(lx, ly, lz)
    cv = compose.Canvas(w, h, (cx - w // 2, cy - h // 2), sc.get('bg', [16, 16, 24]))
    items = []
    for it in sc.get('items', []):
        name, x, y, fl = it[:4]
        opt = it[4] if len(it) > 4 else {}
        wx, wy, wz = opt['at'] if 'at' in opt else (x + 0.5, y + 0.5, fl * FLOOR_M)
        items.append((name, wx, wy, wz, opt))
    items.sort(key=lambda t: -compose.depth(t[1], t[2], t[3]))
    pals = sc.get('palettes', {})
    for name, wx, wy, wz, opt in items:
        pal = pals.get(opt.get('palette')) if opt.get('palette') else None
        cv.draw(lib.get(name), wx, wy, wz, pal, opt.get('alpha', 1.0))
    lin = np.where(cv.rgb / 255 <= 0.04045, cv.rgb / 255 / 12.92, ((cv.rgb / 255 + 0.055) / 1.055) ** 2.4)
    add = np.zeros_like(lin)
    for name, wx, wy, wz, opt in items:
        d, info = lib.s[name]
        if 'gl' not in info['files']:
            continue
        g = np.asarray(Image.open(os.path.join(d, info['files']['gl'])).convert('RGB')).astype(np.float32) / 255
        g = np.where(g <= 0.04045, g / 12.92, ((g + 0.055) / 1.055) ** 2.4)
        sx, sy = compose.screen(wx, wy, wz)
        x0 = int(round(sx - cv.ox)) - info['ax']
        y0 = int(round(sy - cv.oy)) - info['ay']
        gh, gw = g.shape[:2]
        cx0, cy0, cx1, cy1 = max(0, x0), max(0, y0), min(w, x0 + gw), min(h, y0 + gh)
        if cx0 >= cx1 or cy0 >= cy1:
            continue
        sl = (slice(cy0 - y0, cy1 - y0), slice(cx0 - x0, cx1 - x0))
        dst = (slice(cy0, cy1), slice(cx0, cx1))
        near = np.abs(cv.z[dst] - compose.depth(wx, wy, wz)) < 40
        add[dst] += g[sl] * near[..., None]
    lin = lin * (1 + gain * add)
    out = np.where(lin <= 0.0031308, lin * 12.92, 1.055 * np.power(np.clip(lin, 0, 1), 1 / 2.4) - 0.055)
    return Image.fromarray(np.clip(np.round(out * 255), 0, 255).astype(np.uint8))


def render(zone, tag, scene):
    d = os.path.join(ROOT, 'assets', 'tiles_' + zone)
    js = os.path.join(d, '_%s.json' % tag)
    with open(js, 'w') as fh:
        json.dump(scene, fh, indent=0)
    img = compose.render_scene(scene, d)
    img.save(os.path.join(d, '_%s.png' % tag))
    img.resize((img.width * 2, img.height * 2), Image.NEAREST).save(os.path.join(d, '_%s_2x.png' % tag))
    print('wrote', os.path.join(d, '_%s.png' % tag))
    img = render_glow(scene, d)
    img.save(os.path.join(d, '_%s_glow.png' % tag))
    img.resize((img.width * 2, img.height * 2), Image.NEAREST).save(os.path.join(d, '_%s_glow_2x.png' % tag))


def sheet(zone, names=None, scale=2, cols=6):
    d = os.path.join(ROOT, 'assets', 'tiles_' + zone)
    with open(os.path.join(d, 'meta.json')) as fh:
        meta = json.load(fh)
    names = names or sorted(k for k in meta if not k.startswith('_'))
    ims = [Image.open(os.path.join(d, meta[n]['files']['img'])).convert('RGBA') for n in names]
    W = max(i.width for i in ims) * scale
    Hh = max(i.height for i in ims) * scale + 14
    rows = (len(ims) + cols - 1) // cols
    sh = Image.new('RGBA', (cols * (W + 8), rows * (Hh + 8)), (24, 24, 30, 255))
    dr = ImageDraw.Draw(sh)
    for k, (n, im) in enumerate(zip(names, ims)):
        x = (k % cols) * (W + 8)
        y = (k // cols) * (Hh + 8)
        sh.alpha_composite(im.resize((im.width * scale, im.height * scale), Image.NEAREST), (x, y + 14))
        dr.text((x + 2, y + 1), n.replace(zone + '_', ''), fill=(230, 230, 230, 255))
    sh.convert('RGB').save(os.path.join(d, 'zone.png'))
    print('wrote', os.path.join(d, 'zone.png'))


if __name__ == '__main__':
    zone, what = sys.argv[1], sys.argv[2]
    mod = __import__('zones_df_scene_' + zone)
    if what == 'sheet':
        sheet(zone)
    else:
        render(zone, what, getattr(mod, what)())
