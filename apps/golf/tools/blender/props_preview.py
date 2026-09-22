#!/usr/bin/python3
"""Contact sheets for the course props rendered by props.py.

    python3 props_preview.py [--dir ../../assets/props] [--out <dir>] [--zoom 3]

Writes into --out (default: tools/blender/preview, kept out of the assets):
  preview_side.png  side views in a row over sky + fairway, at full size, then
                    the same row scaled to 50 % and 25 % (how they look far away),
                    then the flag frames; everything magnified by --zoom
                    (nearest neighbour, so the real pixels stay visible)
  preview_top.png   top views with their shadows over mown grass with stripes,
                    at full size and at 50 %, magnified by --zoom
  preview_edges.png every side view over black, white and magenta, to spot
                    dark or light fringes in the alpha edges
"""

import argparse
import json
import os

import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
ap = argparse.ArgumentParser()
ap.add_argument("--dir", default=os.path.join(HERE, "..", "..", "assets", "props"))
ap.add_argument("--out", default=None)
ap.add_argument("--zoom", type=int, default=3)
args = ap.parse_args()
D = os.path.abspath(args.dir)
OUT = os.path.abspath(args.out or os.path.join(HERE, "preview"))
os.makedirs(OUT, exist_ok=True)
meta = json.load(open(os.path.join(D, "meta.json")))

TREES = ["tree_pine", "tree_oak", "tree_poplar", "tree_palm", "bush"]
FAIRWAY = np.array([0x4F, 0xA2, 0x3A], float)
ROUGH = np.array([0x3C, 0x7F, 0x2C], float)


def load(name, mode="RGBA"):
    p = os.path.join(D, name)
    return Image.open(p).convert(mode) if os.path.exists(p) else None


def over(bg, im, x, y):
    """Straight-alpha 'over' in 8-bit sRGB (what a simple sprite blitter does)."""
    a = np.asarray(im, float)
    h, w = a.shape[:2]
    reg = bg[y:y + h, x:x + w]
    al = a[..., 3:4] / 255.0
    reg[:] = reg * (1 - al) + a[..., :3] * al


def darken(bg, sh, x, y, strength=0.45):
    s = np.asarray(sh, float)[..., None] / 255.0
    h, w = s.shape[:2]
    reg = bg[y:y + h, x:x + w]
    reg[:] = reg * (1 - strength * s)


def sky_grass(w, h, horizon):
    bg = np.zeros((h, w, 3), float)
    t = np.linspace(0, 1, horizon)[:, None]
    bg[:horizon] = (np.array([0x6C, 0xA8, 0xE8]) * (1 - t) + np.array([0xC8, 0xE0, 0xF4]) * t)[:, None, :]
    rows = np.arange(h - horizon)[:, None]
    stripe = ((rows // 10) % 2)[..., None]
    g = FAIRWAY * (1 - 0.06 * stripe) * (0.9 + 0.1 * rows / max(1, h - horizon))[..., None]
    bg[horizon:] = g
    return bg


def mown(w, h, band=12):
    x = np.arange(w)[None, :]
    y = np.arange(h)[:, None]
    stripe = (((x + y) // band) % 2)[..., None]
    bg = FAIRWAY * (1.0 - 0.08 * stripe)
    bg = np.broadcast_to(bg, (h, w, 3)).copy()
    return bg


def scaled(im, f):
    w, h = im.size
    return im.resize((max(1, round(w * f)), max(1, round(h * f))), Image.LANCZOS)


def save(bg, name):
    im = Image.fromarray(np.clip(bg, 0, 255).astype(np.uint8))
    im = im.resize((im.width * args.zoom, im.height * args.zoom), Image.NEAREST)
    im.save(os.path.join(OUT, name))
    print("wrote", os.path.join(OUT, name), im.size)


def side_sheet():
    sides = [(n, load(n + "_side.png")) for n in TREES]
    sides = [(n, s) for n, s in sides if s is not None]
    flags = [load("flag_side_%d.png" % i) for i in range(4)]
    flags = [f for f in flags if f is not None]
    gap = 8
    rows = []
    for f in (1.0, 0.5, 0.25):
        rows.append([(n, scaled(s, f) if f != 1 else s) for n, s in sides])
    W = max(sum(s.width for _, s in r) + gap * (len(r) + 1) for r in rows)
    W = max(W, sum(f.width for f in flags) + gap * 6 + 60)
    H = sum(max(s.height for _, s in r) + gap * 2 for r in rows) + 80 + gap
    horizon = 40
    bg = sky_grass(W, H, horizon)
    y = horizon - 30
    for r in rows:
        x = gap
        rh = max(s.height for _, s in r)
        base_y = y + rh
        for n, s in r:
            m = meta.get(n, {})
            f = s.height / m["side_size"][1] if m else 1
            bx, by = m.get("side_base_px", [s.width / 2, s.height])
            # align trunk base on a common baseline
            over(bg, s, x, int(round(base_y - by * f)))
            x += s.width + gap
        y += rh + gap * 2
    x = gap
    for f in flags:
        over(bg, f, x, y + 8)
        x += f.width + gap
    # the flag at 50 %
    for f in flags:
        s = scaled(f, 0.5)
        over(bg, s, x + 10, y + 8 + f.height - s.height)
        x += s.width + gap
    save(bg, "preview_side.png")


def top_sheet():
    items = []
    for n in TREES:
        t = load(n + "_top.png")
        s = load(n + "_topshadow.png", "L")
        if t is not None and s is not None:
            items.append((n, t, s))
    gap = 16
    rows = [1.0, 0.5]
    cellw = [max(96, s.width) for _, _, s in items]
    W = sum(cellw) + gap * (len(items) + 1)
    H = sum(max(s.height for _, _, s in items) * f for f in rows) + gap * 3
    H = int(H)
    bg = mown(W, H)
    y = gap
    for f in rows:
        x = gap
        rh = 0
        for (n, t, s), cw in zip(items, cellw):
            ts = scaled(t, f) if f != 1 else t
            ss = scaled(s, f) if f != 1 else s
            darken(bg, ss, x, y)
            over(bg, ts, x, y)
            x += int(cw * f) + gap
            rh = max(rh, ss.height)
        y += rh + gap
    save(bg, "preview_top.png")


def edge_sheet():
    sides = [load(n + "_side.png") for n in TREES] + [load("flag_side_0.png")]
    sides = [s for s in sides if s is not None]
    gap = 4
    W = sum(s.width for s in sides) + gap * (len(sides) + 1)
    Hr = max(s.height for s in sides) + gap
    bg = np.zeros((Hr * 3 + gap, W, 3), float)
    cols = [(0, 0, 0), (255, 255, 255), (255, 0, 255)]
    for i, c in enumerate(cols):
        bg[gap + i * Hr: gap + i * Hr + Hr - gap] = c
        x = gap
        for s in sides:
            over(bg, s, x, gap + i * Hr)
            x += s.width + gap
    save(bg, "preview_edges.png")


side_sheet()
top_sheet()
edge_sheet()
