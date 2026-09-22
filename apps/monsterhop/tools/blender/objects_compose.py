#!/usr/bin/env python3
"""Monster Hop - tools/compose.py plus the `glow` pass (plain python3).

compose.py does not read the `_gl.png` files yet. This does what SPEC section 1
says the watch does with them: after the scene is drawn, the glow of every
sprite that has one is ADDED to the ground pixels around it (pixels last
written by a tile, lying on the sprite's ground plane), so lanterns and keys
light the floor, and nothing standing on it.

    python3 objects_compose.py scene.json out.png [out_2x.png]
"""
import json
import os
import sys

import numpy as np
from PIL import Image

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
import compose as K  # noqa: E402

GROUND_TOL = 7.0     # depth units (1/32 m) a ground pixel may be off the glow's plane


class GlowCanvas(K.Canvas):
    def __init__(self, *a, **kw):
        super().__init__(*a, **kw)
        self.ground = np.zeros((self.h, self.w), bool)

    def draw(self, spr, wx, wy, wz, palette=None, alpha=1.0, shadow=True):
        before = self.z.copy()
        super().draw(spr, wx, wy, wz, palette, alpha, shadow)
        changed = self.z != before
        self.ground = np.where(changed, spr.info.get('kind') in ('tile', 'surf'), self.ground)

    def plane_depth(self, gz):
        """Depth (units) of the plane z = gz under every pixel."""
        yy, xx = np.mgrid[0:self.h, 0:self.w].astype(np.float64)
        sx = xx + self.ox
        sy = yy + self.oy + K.FLOOR_PX * gz / K.FLOOR_M
        # 60x + 20y = sx ; 14x - 42y = sy
        det = 60 * -42 - 20 * 14
        x = (sx * -42 - 20 * sy) / det
        y = (60 * sy - 14 * sx) / det
        return K.depth(x, y, gz)

    def add_glow(self, d, spr, wx, wy, wz):
        f = spr.info['files']
        gl = np.asarray(Image.open(os.path.join(d, f['gl'])).convert('RGB')).astype(np.float32)
        sx, sy = K.screen(wx, wy, wz)
        x0 = int(round(sx - self.ox)) - spr.ax
        y0 = int(round(sy - self.oy)) - spr.ay
        h, w = gl.shape[:2]
        cx0, cy0 = max(0, x0), max(0, y0)
        cx1, cy1 = min(self.w, x0 + w), min(self.h, y0 + h)
        if cx0 >= cx1 or cy0 >= cy1:
            return
        sl = (slice(cy0 - y0, cy1 - y0), slice(cx0 - x0, cx1 - x0))
        dst = (slice(cy0, cy1), slice(cx0, cx1))
        pd = self.plane_depth(wz + spr.info.get('shadow_z', 0.0))[dst]
        m = self.ground[dst] & (np.abs(self.z[dst] - pd) < GROUND_TOL)
        self.rgb[dst] += gl[sl] * m[..., None]


def render_scene(sc, base_dir='.'):
    dirs = [os.path.join(base_dir, d) for d in sc['assets']]
    lib = K.Library(dirs)
    w, h = sc.get('size', [368, 448])
    lx, ly, lz = sc.get('look_at', [0, 0, 0])
    cx, cy = K.screen(lx, ly, lz)
    cv = GlowCanvas(w, h, (cx - w // 2, cy - h // 2), sc.get('bg', [16, 16, 24]))
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
                    items.append((g['fill'] if k < hgt else g['floor'], x + 0.5, y + 0.5, k * K.FLOOR_M, {}))
    for it in sc.get('items', []):
        name, x, y, fl = it[:4]
        opt = it[4] if len(it) > 4 else {}
        if 'at' in opt:
            wx, wy, wz = opt['at']
        else:
            wx, wy, wz = x + 0.5, y + 0.5, fl * K.FLOOR_M
        items.append((name, wx, wy, wz, opt))
    items.sort(key=lambda t: -K.depth(t[1], t[2], t[3]))
    pals = sc.get('palettes', {})
    for name, wx, wy, wz, opt in items:
        pal = pals.get(opt.get('palette')) if opt.get('palette') else None
        cv.draw(lib.get(name), wx, wy, wz, pal, opt.get('alpha', 1.0))
    for name, wx, wy, wz, opt in items:
        spr = lib.get(name)
        if 'gl' in spr.info['files']:
            cv.add_glow(lib.s[name][0], spr, wx, wy, wz)
    return cv.image()


if __name__ == '__main__':
    if len(sys.argv) < 3:
        print(__doc__)
        sys.exit(1)
    with open(sys.argv[1]) as fh:
        sc = json.load(fh)
    img = render_scene(sc, os.path.dirname(os.path.abspath(sys.argv[1])))
    img.save(sys.argv[2])
    print('wrote', sys.argv[2])
    if len(sys.argv) > 3:
        img.resize((img.width * 2, img.height * 2), Image.NEAREST).save(sys.argv[3])
        print('wrote', sys.argv[3])
