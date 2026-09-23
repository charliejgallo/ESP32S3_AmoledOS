#!/usr/bin/env python3
"""Mila - reference compositor (from Monster Hop's).

Puts rendered sprites together the way the watch does (a colour buffer and a
depth buffer; every sprite placed by its anchor and depth-tested per pixel),
so an asset can be seen in context without the watch or the simulator.

    python3 compose.py scene.json out.png

scene.json:
    {"assets": ["../assets/tiles_forest", "../assets/chars"],   # dirs with meta.json
     "size": [368, 448], "look_at": [3.5, 4.5, 0],               # world point at the centre
     "bg": [20, 30, 25],
     "zoom": 1.0,                                  # 1.5 for the casita (sprites rendered at that zoom)
     "palettes": {"tommy": {"1": [240,190,150], "5": [30,120,240], ...}},
     "grid": {"w": 8, "h": 12, "floor": "blk_grass", "fill": "blk_dirt",
              "heights": ["00000000", ...]},     # optional: rows from y=0 (front) up
     "items": [["tommy_test", 3, 1, 0, {"palette": "tommy"}], ...]}  # name, x, y, floor
The x, y of an item is the cell (its anchor at the cell centre) unless the
option "at": [wx, wy, wz] gives a world point.

Also usable as a module (the Blender-side scripts' previews import it).
"""
import json
import math
import os
import sys

import numpy as np
from PIL import Image

PX_X = (72, 0)
PX_Y = (0, -54)
FLOOR_PX = 24
_S = 72.0
_SIN_E = 0.75
_COS_E = math.sqrt(1 - _SIN_E ** 2)
FLOOR_M = FLOOR_PX / (_S * _COS_E)
_PSI = 0.0
VIEW = (-math.sin(_PSI) * _COS_E, math.cos(_PSI) * _COS_E, -_SIN_E)
DEPTH_UNIT = 1.0 / 32.0
DPLANE = -(_COS_E / 54.0) / DEPTH_UNIT      # depth per screen px down, on a horizontal plane
BIAS = 2.0          # depth units a sprite may sit "inside" the ground and still show


ZOOM = [1.0]


def screen(x, y, z):
    """Pixel offset (x right, y down) of a world point from the world origin."""
    k = ZOOM[0]
    return (k * (PX_X[0] * x + PX_Y[0] * y), k * (PX_X[1] * x + PX_Y[1] * y - FLOOR_PX * z / FLOOR_M))


def depth(x, y, z):
    return (x * VIEW[0] + y * VIEW[1] + z * VIEW[2]) / DEPTH_UNIT


class Sprite:
    def __init__(self, d, name, info):
        f = info['files']
        self.name, self.info = name, info
        self.ax, self.ay = info['ax'], info['ay']
        if 'img' in f:
            self.img = np.asarray(Image.open(os.path.join(d, f['img'])).convert('RGBA')).astype(np.float32)
        else:
            # a shadow on its own (a pushable thing's): nothing but the shadow
            sh = np.asarray(Image.open(os.path.join(d, f['sh'])).convert('L'))
            self.img = np.zeros(sh.shape + (4,), np.float32)
        self.z = np.asarray(Image.open(os.path.join(d, f['z'])).convert('L')).astype(np.float32) if 'z' in f else None
        self.id = (np.asarray(Image.open(os.path.join(d, f['id'])).convert('L')) // 16) if 'id' in f else None
        self.sh = np.asarray(Image.open(os.path.join(d, f['sh'])).convert('L')).astype(np.float32) if 'sh' in f else None
        self.anchor = info.get('anchor', [0, 0, 0])


class Library:
    def __init__(self, dirs):
        self.s = {}
        for d in dirs:
            with open(os.path.join(d, 'meta.json')) as fh:
                meta = json.load(fh)
            for k, v in meta.items():
                if k.startswith('_'):
                    continue
                self.s[k] = (d, v)
        self.cache = {}

    def get(self, name, variant=0):
        """a sprite by name; a sheet's base name finds its variant/frame
        (_v<k>, _<nn>); None when the art does not exist (yet)"""
        key = name
        if key not in self.s:
            for cand in ('%s_v%d' % (name, variant), '%s_v0' % name, '%s_%02d' % (name, variant), '%s_00' % name):
                if cand in self.s:
                    key = cand
                    break
            else:
                return None
        if key not in self.cache:
            d, v = self.s[key]
            self.cache[key] = Sprite(d, key, v)
        return self.cache[key]


class Canvas:
    def __init__(self, w, h, origin, bg):
        self.w, self.h = w, h
        self.ox, self.oy = origin      # screen() coordinates of the canvas' top-left
        self.rgb = np.zeros((h, w, 3), np.float32)
        self.rgb[:] = bg
        self.z = np.full((h, w), 1e9, np.float32)

    def draw(self, spr, wx, wy, wz, palette=None, alpha=1.0, shadow=True):
        sx, sy = screen(wx, wy, wz)
        x0 = int(round(sx - self.ox)) - spr.ax
        y0 = int(round(sy - self.oy)) - spr.ay
        dz = depth(wx, wy, wz)
        h, w = spr.img.shape[:2]
        cx0, cy0 = max(0, x0), max(0, y0)
        cx1, cy1 = min(self.w, x0 + w), min(self.h, y0 + h)
        if cx0 >= cx1 or cy0 >= cy1:
            return
        sl = (slice(cy0 - y0, cy1 - y0), slice(cx0 - x0, cx1 - x0))
        dst = (slice(cy0, cy1), slice(cx0, cx1))
        if shadow and spr.sh is not None:
            # the shadow darkens what lies on the ground plane under the sprite
            # and nothing above it (a wall top in front): as the watch does,
            # the plane's depth on each row against the depth buffer
            dg = depth(wx, wy, wz + 0.0)
            a = spr.sh[sl] / 255.0 * 0.55
            rows = np.arange(cy0, cy1, dtype=np.float32)[:, None] + self.oy - sy
            dp = dg + rows * (DPLANE / ZOOM[0])
            near = np.abs(self.z[dst] - dp) < 5
            m = a * near
            self.rgb[dst] *= (1 - m[..., None])
        a = spr.img[sl][..., 3] / 255.0 * alpha
        if palette is not None and spr.id is not None:
            ids = spr.id[sl]
            lut = np.zeros((16, 3), np.float32)
            for k, c in palette.items():
                lut[int(k)] = c
            base = lut[ids]
            col = np.clip(base * spr.img[sl][..., :3] / 196.0, 0, 255)
        else:
            col = spr.img[sl][..., :3]
        if spr.z is not None:
            zz = spr.z[sl]
            sz = np.where(zz >= 255, 1e9, zz - 128 + dz)
        else:
            sz = np.full(a.shape, dz)
        vis = (a > 0) & (sz < self.z[dst] + BIAS)
        a = np.where(vis, a, 0)
        self.rgb[dst] = self.rgb[dst] * (1 - a[..., None]) + col * a[..., None]
        wr = vis & (spr.img[sl][..., 3] >= 128)
        self.z[dst] = np.where(wr, sz, self.z[dst])

    def image(self):
        return Image.fromarray(np.clip(self.rgb, 0, 255).astype(np.uint8), 'RGB')


def render_scene(sc, base_dir='.'):
    lib = Library([os.path.join(base_dir, d) for d in sc['assets']])
    ZOOM[0] = float(sc.get('zoom', 1.0))
    w, h = sc.get('size', [368, 448])
    lx, ly, lz = sc.get('look_at', [0, 0, 0])
    cx, cy = screen(lx, ly, lz)
    cv = Canvas(w, h, (cx - w // 2, cy - h // 2), sc.get('bg', [16, 16, 24]))
    items = []
    g = sc.get('grid')
    if g:
        rows = g['heights']
        for y in range(g['h']):
            row = rows[y] if y < len(rows) else '0' * g['w']
            for x in range(g['w']):
                c = row[x] if x < len(row) else '0'
                if c == '.':
                    continue
                hgt = int(c)
                for k in range(-1, hgt + 1):
                    items.append((g['fill'] if k < hgt else g['floor'], x + 0.5, y + 0.5, k * FLOOR_M, {}))
    for it in sc.get('items', []):
        name, x, y, fl = it[:4]
        opt = it[4] if len(it) > 4 else {}
        if 'at' in opt:
            wx, wy, wz = opt['at']
        else:
            wx, wy, wz = x + 0.5, y + 0.5, fl * FLOOR_M
        items.append((name, wx, wy, wz, opt))
    # far to near, so antialiased edges blend over what is behind them
    items.sort(key=lambda t: -depth(t[1], t[2], t[3]))
    pals = sc.get('palettes', {})
    for name, wx, wy, wz, opt in items:
        pal = pals.get(opt.get('palette')) if opt.get('palette') else None
        spr = lib.get(name, opt.get('variant', 0))
        if spr is not None:
            cv.draw(spr, wx, wy, wz, pal, opt.get('alpha', 1.0))
    return cv.image()


if __name__ == '__main__':
    if len(sys.argv) < 3:
        print(__doc__)
        sys.exit(1)
    with open(sys.argv[1]) as fh:
        sc = json.load(fh)
    img = render_scene(sc, os.path.dirname(os.path.abspath(sys.argv[1])))
    scale = int(sys.argv[3]) if len(sys.argv) > 3 else 1
    if scale > 1:
        img = img.resize((img.width * scale, img.height * scale), Image.NEAREST)
    img.save(sys.argv[2])
    print('wrote', sys.argv[2])
