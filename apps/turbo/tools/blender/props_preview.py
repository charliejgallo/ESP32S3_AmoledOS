#!/usr/bin/python3
"""Previews for the Turbo scenery rendered by props.py.

    python3 props_preview.py [--dir ../../assets/props] [--out ../../assets/props_preview]
                             [--stage city,coast] [--zoom 2]

Per stage it writes into --out:
  sheet_<stage>.png  every prop of the stage (and the common ones) at full size
                     over the stage's sky and ground, then the same at 1/4 size,
                     then the backdrop; magnified by --zoom (nearest neighbour)
  road_<stage>.png   a rough mock of the game view, 368 x 448: sky gradient from
                     sky.json, the backdrop on the horizon (bottom row at y = 150),
                     a 3-lane road in perspective with the game camera (focal 300 px,
                     principal point (184, 150), height 2 m) and props placed beside
                     it, scaled by (300 / Z) / ppm. Also road_<stage>_x2.png.
                     A tunnel_portal gets the tunnel the watch draws behind its
                     opening (walls at +-8 m, a flat ceiling at 7 m, lamps every
                     12 m, as main/tb_render.c); tunnels also writes
                     road_tunnels_near.png with the portal close, to check that
                     the arch lines up with those walls.
"""

import argparse
import json
import os

import numpy as np
from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
ap = argparse.ArgumentParser()
ap.add_argument("--dir", default=os.path.join(HERE, "..", "..", "assets", "props"))
ap.add_argument("--out", default=os.path.join(HERE, "..", "..", "assets", "props_preview"))
ap.add_argument("--stage", default="")
ap.add_argument("--zoom", type=int, default=2)
ap.add_argument("--cars", default=os.path.join(HERE, "..", "..", "assets", "cars"))
args = ap.parse_args()
D = os.path.abspath(args.dir)
OUT = os.path.abspath(args.out)
os.makedirs(OUT, exist_ok=True)
META = json.load(open(os.path.join(D, "meta.json")))
PROPS = META.get("props", {})
SKY = json.load(open(os.path.join(D, "sky.json"))) if os.path.exists(os.path.join(D, "sky.json")) else {}

SW, SH, F, CX, HOR, CAMH = 368, 448, 300.0, 184.0, 150.0, 2.0
STAGES = ["city", "coast", "desert", "mountain", "space", "halloween", "tunnels"]
NIGHT = ("space", "mountain", "halloween")
# props a stage borrows from another one (shown on its sheet)
EXTRA = {"tunnels": ["guardrail"]}
# spanning props: never mirrored
SPAN = ("overpass", "checkpoint", "finish", "ring_gate", "sign_gantry", "tunnel_portal")


def hx(h):
    h = h.lstrip("#")
    return np.array([int(h[i:i + 2], 16) for i in (0, 2, 4)], float)


def sky_of(stage):
    s = SKY.get(stage, {})
    d = dict(sky_top="#4a82d0", sky_horizon="#cfe0f0", fog="#c0d0e0", ground_a="#5a9a3a",
             ground_b="#528f35", road_a="#6a6a6e", road_b="#646468", rumble_a="#e02020",
             rumble_b="#f0f0f0", line="#f0f0f0", shoulder="#8a8a80")
    d.update(s)
    return d


def load(name, mode="RGBA"):
    p = os.path.join(D, name)
    return Image.open(p).convert(mode) if os.path.exists(p) else None


def over(bg, im, x, y, alpha_mul=1.0):
    """Straight-alpha 'over' in 8-bit sRGB (what a sprite blitter does), clipped."""
    a = np.asarray(im, float)
    h, w = a.shape[:2]
    x0, y0 = int(round(x)), int(round(y))
    X0, Y0 = max(0, x0), max(0, y0)
    X1, Y1 = min(bg.shape[1], x0 + w), min(bg.shape[0], y0 + h)
    if X1 <= X0 or Y1 <= Y0:
        return
    sub = a[Y0 - y0:Y1 - y0, X0 - x0:X1 - x0]
    reg = bg[Y0:Y1, X0:X1]
    al = sub[..., 3:4] / 255.0 * alpha_mul
    reg[:] = reg * (1 - al) + sub[..., :3] * al


def darken(bg, sh, x, y, strength=0.5):
    s = np.asarray(sh, float)
    h, w = s.shape[:2]
    x0, y0 = int(round(x)), int(round(y))
    X0, Y0 = max(0, x0), max(0, y0)
    X1, Y1 = min(bg.shape[1], x0 + w), min(bg.shape[0], y0 + h)
    if X1 <= X0 or Y1 <= Y0:
        return
    sub = s[Y0 - y0:Y1 - y0, X0 - x0:X1 - x0, None] / 255.0
    bg[Y0:Y1, X0:X1] *= (1 - strength * sub)


def sky_bg(w, h, st, horizon):
    s = sky_of(st)
    bg = np.zeros((h, w, 3), float)
    t = np.linspace(0, 1, max(1, horizon))[:, None] ** 1.3
    bg[:horizon] = (hx(s["sky_top"]) * (1 - t) + hx(s["sky_horizon"]) * t)[:, None, :]
    if st in NIGHT:
        rng = np.random.RandomState(4)
        n = int(w * horizon / 90)
        xs, ys = rng.randint(0, w, n), rng.randint(0, max(1, horizon), n)
        b = rng.uniform(0.3, 1.0, n) ** 2 * (255 if st == "space" else 170)
        for x, y, v in zip(xs, ys, b):
            bg[y, x] = np.maximum(bg[y, x], v)
    rows = np.arange(h - horizon)[:, None]
    stripe = ((rows // 12) % 2)[..., None]
    g = hx(s["ground_a"]) * (1 - stripe) + hx(s["ground_b"]) * stripe
    bg[horizon:] = np.broadcast_to(g[:, None, :] if g.ndim == 2 else g, (h - horizon, w, 3))
    return bg


def scaled(im, f):
    w, h = im.size
    return im.resize((max(1, round(w * f)), max(1, round(h * f))), Image.LANCZOS)


def save(bg, name, zoom=1, labels=()):
    im = Image.fromarray(np.clip(bg, 0, 255).astype(np.uint8))
    if zoom != 1:
        im = im.resize((im.width * zoom, im.height * zoom), Image.NEAREST)
    if labels:
        dr = ImageDraw.Draw(im)
        for x, y, t in labels:
            dr.text((x * zoom + 1, y * zoom + 1), t, fill=(0, 0, 0))
            dr.text((x * zoom, y * zoom), t, fill=(255, 255, 255))
    im.save(os.path.join(OUT, name))
    print("wrote", os.path.join(OUT, name), im.size)


def stage_props(st):
    return [n for n, m in PROPS.items() if m.get("stage") == st and not n.endswith("_off")]


# ---------------------------------------------------------------------------
# Contact sheet


def sheet(st):
    names = stage_props(st) + [n for n in EXTRA.get(st, []) if n in PROPS] \
        + (stage_props("common") if st != "common" else [])
    items = [(n, load(PROPS[n]["file"])) for n in names]
    items = [(n, im) for n, im in items if im is not None]
    if not items:
        return
    gap = 10
    maxw = 1100
    # rows of full-size sprites, baseline-aligned on their anchors
    rows, cur, cw = [], [], gap
    for n, im in items:
        if cur and cw + im.width + gap > maxw:
            rows.append(cur)
            cur, cw = [], gap
        cur.append((n, im))
        cw += im.width + gap
    if cur:
        rows.append(cur)
    small = [(n, scaled(im, 0.25)) for n, im in items]
    bgim = load("bg_%s.png" % st)
    W = maxw
    H = gap
    for r in rows:
        H += max(PROPS[n]["anchor"][1] for n, _ in r) + max(im.height - PROPS[n]["anchor"][1] for n, im in r) + gap + 14
    sh_small = max(im.height for _, im in small)
    H += sh_small + gap * 2 + 14
    if bgim is not None:
        H += bgim.height + gap * 2
    H = int(H + 1)
    bg = sky_bg(W, H, st if st != "common" else "city", H)
    s = sky_of(st if st != "common" else "city")
    labels = []
    y = gap
    for r in rows:
        top = max(PROPS[n]["anchor"][1] for n, _ in r)
        bot = max(im.height - PROPS[n]["anchor"][1] for n, im in r)
        base = y + 14 + top
        bg[int(base):int(base + bot + gap)] = hx(s["ground_a"])
        x = gap
        for n, im in r:
            ax, ay = PROPS[n]["anchor"]
            shp = PROPS[n].get("shadow_file")
            if shp:
                shim = load(shp, "L")
                sax, say = PROPS[n]["shadow_anchor"]
                darken(bg, shim, x + ax - sax, base - say)
            over(bg, im, x, base - ay)
            labels.append((x, y, n))
            x += im.width + gap
        y = base + bot + gap
    # quarter size row
    base = y + 14 + sh_small
    bg[int(base):int(base + gap)] = hx(s["ground_a"])
    x = gap
    labels.append((x, y, "1/4 size"))
    for (n, im), (_, full) in zip(small, items):
        ay = PROPS[n]["anchor"][1] * im.height / full.height
        over(bg, im, x, base - ay)
        x += im.width + gap
    y = base + gap * 2
    if bgim is not None:
        over(bg, bgim, gap, y)
        labels.append((gap, y, "bg_%s" % st))
    save(bg, "sheet_%s.png" % st, zoom=args.zoom if W * args.zoom <= 2400 else 1, labels=labels)


# ---------------------------------------------------------------------------
# Road scene

ROAD_HALF = 6.0      # 3 lanes of 3.7 m + a little
RUMBLE = 0.6
SEG = 6.0            # metres per ground / rumble band


def road_scene(st, placements, car=True, outname=None):
    s = sky_of(st)
    bg = np.zeros((SH, SW, 3), float)
    hor = int(HOR)
    t = np.linspace(0, 1, hor)[:, None] ** 1.2
    bg[:hor] = (hx(s["sky_top"]) * (1 - t) + hx(s["sky_horizon"]) * t)[:, None, :]
    if st in NIGHT:
        rng = np.random.RandomState(7)
        n = 160 if st == "space" else 70
        for x, y, v in zip(rng.randint(0, SW, n), rng.randint(0, hor, n), rng.uniform(0.3, 1, n) ** 2):
            bg[y, x] = np.maximum(bg[y, x], v * (255 if st == "space" else 190))
    bgim = load("bg_%s.png" % st)
    if bgim is not None:
        # scroll so the most interesting bit is centred: offset from sky.json or 0
        off = int(s.get("preview_scroll", 0))
        arr = np.asarray(bgim)
        arr = np.roll(arr, -off, axis=1)
        tile = np.concatenate([arr, arr], axis=1)[:, :SW]
        over(bg, Image.fromarray(tile), 0, HOR - bgim.height)
    fog = hx(s["fog"])
    for y in range(hor, SH):
        dy = y + 0.5 - HOR
        Z = F * CAMH / dy
        seg = int(Z / SEG) % 2
        g = hx(s["ground_a"] if seg else s["ground_b"])
        rd = hx(s["road_a"] if seg else s["road_b"])
        fk = min(1.0, max(0.0, (Z - 30) / 400.0)) ** 0.8 * 0.6
        row = np.tile(g, (SW, 1))
        xs = np.arange(SW) + 0.5
        X = (xs - CX) * Z / F
        ax = np.abs(X)
        row[ax < ROAD_HALF + RUMBLE] = hx(s["rumble_a"] if seg else s["rumble_b"])
        row[ax < ROAD_HALF] = rd
        if st == "space":
            # the road floats: void outside it
            row[ax >= ROAD_HALF + RUMBLE] = np.nan
        # lane lines (dashed)
        if int(Z / 3.0) % 3 == 0:
            for lx in (-ROAD_HALF / 3, ROAD_HALF / 3):
                row[np.abs(X - lx) < 0.12] = hx(s["line"])
        row = row * (1 - fk) + fog * fk
        keep = ~np.isnan(row[:, 0])
        bg[y, keep] = row[keep]
        if st == "space":
            bg[y, ~keep] = hx(s["sky_top"]) * 0.6
    if st == "space":
        rng = np.random.RandomState(9)
        for x, y, v in zip(rng.randint(0, SW, 120), rng.randint(hor, SH, 120), rng.uniform(0.3, 1, 120) ** 2):
            X = (x - CX) * (F * CAMH / (y - HOR)) / F
            if abs(X) > ROAD_HALF + RUMBLE:
                bg[y, x] = np.maximum(bg[y, x], v * 255)
    # sprites, far to near
    for name, X, Z in sorted(placements, key=lambda p: -p[2]):
        m = PROPS.get(name)
        if m is None or Z < 1:
            continue
        im = load(m["file"])
        if im is None:
            continue
        k = (F / Z) / m["ppm"]
        mirror = X < 0 and name not in SPAN
        if name == "tunnel_portal":
            draw_tunnel(bg, s, Z)
        px, py = CX + F * X / Z, HOR + F * CAMH / Z
        if m.get("shadow_file"):
            sh = load(m["shadow_file"], "L")
            sax, say = m["shadow_anchor"]
            if mirror:
                sh = sh.transpose(Image.FLIP_LEFT_RIGHT)
                sax = sh.width - sax
            shs = sh.resize((max(1, round(sh.width * k)), max(1, round(sh.height * k))), Image.BILINEAR)
            darken(bg, shs, px - sax * k, py - say * k, 0.45)
        ax, ay = m["anchor"]
        if mirror:
            im = im.transpose(Image.FLIP_LEFT_RIGHT)
            ax = im.width - ax
        w, h = max(1, round(im.width * k)), max(1, round(im.height * k))
        if w > 2000 or h > 2000:
            continue
        ims = im.resize((w, h), Image.LANCZOS if k < 1 else Image.BILINEAR)
        fk = min(1.0, max(0.0, (Z - 40) / 500.0)) * 0.5
        over(bg, ims, px - ax * k, py - ay * k)
        if fk > 0:
            # a touch of fog on far sprites
            a = np.asarray(ims, float)[..., 3:4] / 255 * fk
            x0, y0 = int(round(px - ax * k)), int(round(py - ay * k))
            X0, Y0 = max(0, x0), max(0, y0)
            X1, Y1 = min(SW, x0 + w), min(SH, y0 + h)
            if X1 > X0 and Y1 > Y0:
                sub = a[Y0 - y0:Y1 - y0, X0 - x0:X1 - x0]
                bg[Y0:Y1, X0:X1] = bg[Y0:Y1, X0:X1] * (1 - sub) + fog * sub
    if car:
        draw_car(bg, st)
    outname = outname or st
    save(bg, "road_%s.png" % outname)
    save(bg, "road_%s_x2.png" % outname, zoom=2)


TUN_HW, TUN_H, TUN_LEN, LAMP_EVERY = 8.0, 7.0, 320.0, 12.0


def mix(a, b, t):
    return a * (1 - t) + b * t


def draw_tunnel(bg, s, Zp):
    """The inside of a tunnel whose mouth is at depth Zp, as the watch draws
    it: a box (walls at +-8 m, a flat ceiling 7 m over the road), lamp strips
    down the middle third of the ceiling every 12 m, darker with depth, the
    exit a bright rectangle. Every pixel whose ray leaves the box beyond Zp."""
    wall = hx(s.get("tunnel_wall", "#b8b2a4"))
    ceil = hx(s.get("tunnel_ceiling", "#6e6a62"))
    lamp = hx(s.get("tunnel_lamp", "#ffb050"))
    yy, xx = np.mgrid[0:SH, 0:SW] + 0.5
    dx = (xx - CX) / F
    dz = -(yy - HOR) / F
    with np.errstate(divide="ignore", invalid="ignore"):
        t_w = np.where(np.abs(dx) > 1e-9, TUN_HW / np.abs(dx), np.inf)
        t_c = np.where(dz > 1e-9, (TUN_H - CAMH) / dz, np.inf)
        t_f = np.where(dz < -1e-9, CAMH / -dz, np.inf)
    t = np.minimum(np.minimum(t_w, t_c), t_f)
    seen = t >= Zp
    if not seen.any():
        return
    dk = np.clip(1.0 - (t - Zp) / 260.0, 0.25, 1.0)[..., None]
    out = bg.copy()
    is_w = (t == t_w) & seen
    is_c = (t == t_c) & seen & ~is_w
    is_f = (t == t_f) & seen & ~is_w & ~is_c
    out[is_w] = (wall * dk)[is_w]
    zc = (Zp + t) % LAMP_EVERY
    lampm = is_c & (np.abs(dx * t) < TUN_HW * 0.3) & (zc < 1.4)
    out[is_c] = (ceil * dk)[is_c]
    out[lampm] = np.clip(lamp * (dk[..., 0][lampm][:, None] + 0.25), 0, 255)
    # the floor: the road (already painted) dimmed and warmed, the walkway beyond the rumble
    X = dx * t
    road = is_f & (np.abs(X) < 6.0)
    rumble = is_f & ~road & (np.abs(X) < 6.0 + 0.6)
    walk = is_f & ~road & ~rumble
    out[road] = mix(bg[road], hx("#201408"), 0.3) * dk[road]
    out[rumble] = (wall * dk)[rumble]
    out[walk] = (mix(wall, np.zeros(3), 0.35) * dk)[walk]
    # the exit, far away: the outside seen through the far end
    ex = seen & (t > Zp + TUN_LEN)
    out[ex] = hx(s["fog"])
    bg[seen] = out[seen]


def draw_car(bg, st):
    """The player's car from cars.py, if it has been rendered (red paint)."""
    d = os.path.abspath(args.cars)
    sh_p = os.path.join(d, "near_wedge_y3_shade.png")
    id_p = os.path.join(d, "near_wedge_y3_id.png")
    if not (os.path.exists(sh_p) and os.path.exists(id_p)):
        return
    try:
        shade = np.asarray(Image.open(sh_p).convert("RGBA"), float)
        ids = np.asarray(Image.open(id_p).convert("L"), int) // 16
        pal = {1: (210, 20, 24), 2: (240, 240, 240), 3: (40, 60, 90), 4: (220, 220, 225), 5: (25, 25, 28),
               6: (30, 30, 30), 7: (180, 180, 190), 8: (230, 20, 20), 9: (240, 240, 220), 10: (230, 230, 220),
               11: (40, 30, 30), 12: (20, 20, 22), 13: (255, 150, 20), 14: (60, 60, 60), 15: (128, 128, 128)}
        P = np.zeros(ids.shape + (3,))
        for k, c in pal.items():
            P[ids == k] = c
        light = shade[..., :1] / 196.0
        rgb = np.clip(P * light, 0, 255)
        sp = os.path.join(d, "near_wedge_y3_shadow.png")
        if os.path.exists(sp):
            s = np.asarray(Image.open(sp).convert("L"), float)[..., None] / 255
            bg[:] = bg * (1 - 0.55 * s)
        a = shade[..., 3:4] / 255
        bg[:] = bg * (1 - a) + rgb * a
    except Exception as e:  # pragma: no cover
        print("car preview failed:", e)


# Where the props stand in the mock scenes: (name, X metres, Z metres); X < 0 = left (mirrored)
def lampline(name, x, z0, z1, step):
    out = []
    z = z0
    while z < z1:
        out.append((name, x, z))
        out.append((name, -x, z + step / 2))
        z += step
    return out


SCENES = {
    "city": [("overpass", 0, 52), ("tower_glass", 42, 150), ("tower_brick", -34, 120),
             ("tower_glass", -60, 210), ("tower_brick", 55, 240), ("tree_round", 12, 18),
             ("tree_round", -13, 30), ("billboard", 16, 70), ("sign_gantry", 0, 100),
             ("barrier", 7.2, 11), ("barrier", -7.2, 13), ("barrier", 7.2, 15)]
    + lampline("lamp", 7.5, 12, 110, 24),
    "coast": [("lighthouse", 38, 160), ("rock_cliff", -22, 70), ("rock_cliff", 26, 110),
              ("palm", 9.5, 14), ("palm_tall", -10, 22), ("palm", -9, 38), ("palm_tall", 10, 45),
              ("palm", 10, 80), ("beach_hut", -16, 55), ("guardrail", 7.2, 12), ("guardrail", 7.2, 16),
              ("guardrail", 7.2, 20), ("sign_curve_l", 8, 30)],
    "desert": [("butte", -120, 700), ("butte", 160, 900), ("saguaro", 10, 16), ("saguaro_small", -9, 24),
               ("saguaro", -11, 40), ("rock_red", 12, 30), ("dead_tree", 13, 55), ("diner_sign", 14, 75),
               ("saguaro_small", 9, 90), ("rock_red", -14, 100), ("checkpoint", 0, 60), ("cone", 5, 12)],
    "mountain": [("pine_snow", 11, 16), ("pine", -10, 20), ("pine_snow", -12, 34), ("pine", 12, 42),
                 ("rock_snow", 14, 28), ("snowbank", 7.5, 11), ("snowbank", -7.5, 14), ("cabin", -20, 60),
                 ("pine_snow", 13, 80), ("pine_snow", -12, 95)] + lampline("lamp_night", 7.5, 13, 120, 30),
    "space": [("ring_gate", 0, 45), ("ring_gate", 0, 110), ("crystal", 14, 20), ("crystal", -16, 55),
              ("asteroid_a", -26, 70), ("asteroid_b", 20, 36), ("satellite", 30, 90), ("beacon", 8, 14),
              ("beacon", -8, 26), ("beacon", 8, 38), ("asteroid_b", -40, 140), ("finish", 0, 160)],
    "halloween": [("haunted_house", -34, 150), ("dead_tree_twisted", 11, 19), ("dead_tree_twisted", -12, 36),
                  ("pumpkins", 7.6, 17), ("pumpkins", -7.8, 24), ("pumpkins", 7.8, 52), ("tombstones", 11.5, 30),
                  ("tombstones", -12.5, 58), ("cemetery_fence", 8.6, 36), ("cemetery_fence", 8.6, 41),
                  ("cemetery_fence", 8.6, 46), ("scarecrow", -15, 44), ("dead_tree_twisted", 17, 75),
                  ("dead_tree_twisted", -20, 95)] + lampline("gas_lamp", 7.6, 15, 120, 28),
    "tunnels": [("tunnel_portal", 0, 62), ("waterfall_cliff", -26, 48), ("rock_granite", 17, 30), ("pylon", 26, 44),
                ("pine_day", 11, 15), ("pine_day", -11, 22), ("pine_day", 12, 38), ("pine_day", -14, 32),
                ("guardrail", 7.2, 9), ("guardrail", 7.2, 13), ("guardrail", 7.2, 17), ("guardrail", 7.2, 21),
                ("guardrail", -7.2, 11), ("guardrail", -7.2, 15), ("guardrail", -7.2, 19)],
    "tunnels_near": [("tunnel_portal", 0, 16), ("guardrail", 7.2, 7), ("guardrail", 7.2, 11),
                     ("guardrail", -7.2, 8), ("guardrail", -7.2, 12)],
    "common": [("checkpoint", 0, 30), ("finish", 0, 80), ("cone", 4, 10), ("cone", 5, 13), ("cone", 6, 16),
               ("sign_curve_l", 8, 18), ("barrier", 7.2, 12), ("barrier", -7.2, 12), ("barrier", 7.2, 14.2)],
}

stages = [s for s in args.stage.split(",") if s] or STAGES + ["common"]
for st in stages:
    sheet(st)
    road_scene(st if st != "common" else "city", SCENES.get(st, []), outname=st)
    if st == "tunnels":
        road_scene("tunnels", SCENES["tunnels_near"], car=True, outname="tunnels_near")
