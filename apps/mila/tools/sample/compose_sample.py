#!/usr/bin/env python3
"""compose_sample.py - watch screens (368 x 448) for Mila's style sample.

    python3 compose_sample.py <renders dir> <out dir>

Takes the Blender renders of ../blender/sample.py and draws what the watch
would show: the level overview, the view that follows Mila, the zoom between
them (GIF), the shop and Mila's home, with a HUD mock-up on top.
"""
import os
import sys

from PIL import Image, ImageDraw, ImageFont

W, H = 368, 448
HERE = os.path.dirname(os.path.abspath(__file__))
FONTS = os.path.normpath(os.path.join(HERE, '..', '..', '..', '..', 'sim', 'lvgl'))
BOLD = os.path.join(FONTS, 'tests', 'src', 'test_files', 'fonts', 'Montserrat-Bold.ttf')
MED = os.path.join(FONTS, 'scripts', 'built_in_font', 'Montserrat-Medium.ttf')
SYM = os.path.join(FONTS, 'scripts', 'built_in_font', 'DejaVuSans.ttf')


def font(size, bold=True):
    return ImageFont.truetype(BOLD if bold else MED, size)


def sym(size):
    return ImageFont.truetype(SYM, size)


def pill(d, xy, text, f, fill=(0, 0, 0, 150), fg=(255, 255, 255, 255), pad=(10, 5), r=None):
    x, y = xy
    bb = d.textbbox((0, 0), text, font=f)
    w, h = bb[2] - bb[0] + 2 * pad[0], bb[3] - bb[1] + 2 * pad[1]
    d.rounded_rectangle((x, y, x + w, y + h), r if r is not None else h // 2, fill=fill)
    d.text((x + pad[0] - bb[0], y + pad[1] - bb[1]), text, font=f, fill=fg)
    return w, h


def coin(d, x, y, r=8):
    d.ellipse((x - r, y - r, x + r, y + r), fill=(255, 196, 40, 255), outline=(200, 130, 10, 255), width=2)
    d.ellipse((x - r * 0.45, y - r * 0.45, x + r * 0.45, y + r * 0.45), outline=(255, 235, 150, 255), width=1)


def star(d, cx, cy, r, fill):
    import math
    pts = []
    for k in range(10):
        a = -math.pi / 2 + k * math.pi / 5
        rr = r if k % 2 == 0 else r * 0.45
        pts.append((cx + rr * math.cos(a), cy + rr * math.sin(a)))
    d.polygon(pts, fill=fill)


def round_button(d, cx, cy, r, glyph, f, fill=(255, 255, 255, 40), fg=(255, 255, 255, 255)):
    d.ellipse((cx - r, cy - r, cx + r, cy + r), fill=fill)
    bb = d.textbbox((0, 0), glyph, font=f)
    d.text((cx - (bb[0] + bb[2]) / 2, cy - (bb[1] + bb[3]) / 2), glyph, font=f, fill=fg)


def load_level(rdir, cam):
    img = Image.open(os.path.join(rdir, 'level_%s.png' % cam)).convert('RGBA')
    with open(os.path.join(rdir, 'level_%s.txt' % cam)) as fh:
        fw, fh_, mx, my = [float(v) for v in fh.read().split()]
    return img, (mx, my)


def view(img, centre, scale):
    """The 368 x 448 screen showing `img` scaled by `scale` around `centre`."""
    out = Image.new('RGBA', (W, H), (0, 0, 0, 255))
    sw, sh = max(1, round(img.width * scale)), max(1, round(img.height * scale))
    im = img if scale == 1.0 else img.resize((sw, sh), Image.LANCZOS)
    ox = round(W / 2 - centre[0] * scale)
    oy = round(H / 2 - centre[1] * scale)
    out.paste(im, (ox, oy), im)
    return out


def fit_scale(img, top=64, bottom=40):
    return min((W - 16) / img.width, (H - top - bottom) / img.height)


def hud_overview(scr):
    ov = Image.new('RGBA', scr.size, (0, 0, 0, 0))
    d = ImageDraw.Draw(ov)
    f = font(19)
    d.text((W / 2, 22), 'Living  1-3', font=f, fill=(255, 255, 255), anchor='mm')
    d.text((W / 2, 45), 'Llevá los 4 ovillos a sus cestas', font=font(13, False), fill=(235, 225, 210), anchor='mm')
    f2 = font(13)
    txt = 'tocá para empezar'
    bb = d.textbbox((0, 0), txt, font=f2)
    tw = bb[2] - bb[0] + 24
    pill(d, ((W - tw) // 2, H - 34), txt, f2, fill=(255, 255, 255, 38), fg=(255, 255, 255, 230), pad=(12, 6))
    scr.alpha_composite(ov)
    return scr


def hud_play(scr):
    ov = Image.new('RGBA', scr.size, (0, 0, 0, 0))
    d = ImageDraw.Draw(ov)
    f = font(15)
    pill(d, (12, 10), '1-3', f, fill=(0, 0, 0, 140))
    pw, _ = pill(d, (0, 0), 'Movs 14 / 26', f, fill=(0, 0, 0, 0), fg=(0, 0, 0, 0))
    pill(d, (W - pw - 12, 10), 'Movs 14 / 26', f, fill=(0, 0, 0, 140))
    # objects on target
    x0 = W / 2 - 34
    for k in range(4):
        c = (255, 205, 60, 255) if k < 1 else (255, 255, 255, 70)
        d.ellipse((x0 + k * 18, 16, x0 + k * 18 + 12, 28), fill=c)
    # undo / restart
    round_button(d, 34, H - 34, 22, '↶', sym(24), fill=(0, 0, 0, 140))
    round_button(d, W - 34, H - 34, 22, '↻', sym(22), fill=(0, 0, 0, 140))
    scr.alpha_composite(ov)
    return scr


def screens_level(rdir, out, cam):
    img, mila = load_level(rdir, cam)
    s0 = fit_scale(img)
    c0 = (img.width / 2, img.height / 2 - 12 / s0)
    ov = view(img, c0, s0)
    hud_overview(ov)
    ov.save(os.path.join(out, 'screen_%s_overview.png' % cam))
    fl = view(img, mila, 1.0)
    hud_play(fl)
    fl.save(os.path.join(out, 'screen_%s_follow.png' % cam))
    # the zoom: 0.9 s on the whole level, 1 s of zoom, 1 s on Mila
    frames, durs = [], []
    raw0 = view(img, c0, s0)
    frames.append(hud_overview(raw0.copy()))
    durs.append(1400)
    n = 16
    for k in range(1, n + 1):
        t = k / n
        e = t * t * (3 - 2 * t)
        s = s0 + (1.0 - s0) * e
        c = (c0[0] + (mila[0] - c0[0]) * e, c0[1] + (mila[1] - c0[1]) * e)
        frames.append(view(img, c, s))
        durs.append(55)
    frames.append(hud_play(view(img, mila, 1.0)))
    durs.append(1600)
    pal = [f.convert('RGB').quantize(colors=255, method=Image.MEDIANCUT, dither=Image.FLOYDSTEINBERG) for f in frames]
    pal[0].save(os.path.join(out, 'zoom_%s.gif' % cam), save_all=True, append_images=pal[1:], duration=durs, loop=0)


def screen_shop(rdir, out):
    scr = Image.new('RGBA', (W, H), (0, 0, 0, 255))
    ovl = Image.new('RGBA', (W, H), (0, 0, 0, 0))
    d = ImageDraw.Draw(ovl)
    # soft spotlight
    for r in range(150, 0, -6):
        e = Image.new('RGBA', (W, H), (0, 0, 0, 0))
        ImageDraw.Draw(e).ellipse((W / 2 - r * 1.25, 250 - r * 0.55, W / 2 + r * 1.25, 250 + r * 0.55), fill=(120, 80, 170, 9))
        ovl.alpha_composite(e)
    d.text((W / 2, 24), 'Tienda', font=font(20), fill=(255, 255, 255), anchor='mm')
    coin(d, W - 70, 24, 8)
    d.text((W - 58, 24), '245', font=font(15), fill=(255, 220, 120), anchor='lm')
    tabs = ['Gorros', 'Collares', 'Juguetes']
    x = 26
    for k, t in enumerate(tabs):
        f = font(14)
        w, h = pill(d, (x, 48), t, f, fill=(255, 255, 255, 230) if k == 0 else (255, 255, 255, 30),
                    fg=(40, 20, 60, 255) if k == 0 else (255, 255, 255, 200), pad=(12, 6))
        x += w + 8
    scr.alpha_composite(ovl)
    m = Image.open(os.path.join(rdir, 'mila_acc_party.png')).convert('RGBA')
    sc = 250 / m.height
    m = m.resize((round(m.width * sc), round(m.height * sc)), Image.LANCZOS)
    scr.alpha_composite(m, (round(W / 2 - m.width / 2), 88))
    d = ImageDraw.Draw(scr)
    d.text((22, 225), '‹', font=font(40), fill=(200, 200, 200), anchor='mm')
    d.text((W - 22, 225), '›', font=font(40), fill=(200, 200, 200), anchor='mm')
    d.text((W / 2, 356), 'Gorrito de fiesta', font=font(17), fill=(255, 255, 255), anchor='mm')
    # colour dots
    cols = [(90, 215, 165), (240, 90, 140), (60, 110, 240), (255, 190, 50), (150, 80, 230)]
    for k, c in enumerate(cols):
        cx = W / 2 + (k - 2) * 26
        d.ellipse((cx - 8, 372, cx + 8, 388), fill=c, outline=(255, 255, 255) if k == 0 else None, width=2)
    bw = 150
    d.rounded_rectangle((W / 2 - bw / 2, 400, W / 2 + bw / 2, 434), 17, fill=(255, 196, 40))
    coin(d, W / 2 - 30, 417, 8)
    d.text((W / 2 - 18, 417), '60', font=font(17), fill=(60, 30, 0), anchor='lm')
    d.text((W / 2 + 12, 417), 'Comprar', font=font(13), fill=(60, 30, 0), anchor='lm')
    scr.save(os.path.join(out, 'screen_shop.png'))


def screen_casita(rdir, out):
    scr = Image.new('RGBA', (W, H), (0, 0, 0, 255))
    c = Image.open(os.path.join(rdir, 'casita.png')).convert('RGBA')
    scr.alpha_composite(c)
    ov = Image.new('RGBA', scr.size, (0, 0, 0, 0))
    d = ImageDraw.Draw(ov)
    w, h = pill(d, (12, 10), '      245', font(15), fill=(0, 0, 0, 150))
    coin(d, 30, 10 + h / 2, 8)
    pill(d, (W - 104, 10), 'Mila  feliz', font(13), fill=(0, 0, 0, 150))
    # speech bubble
    bx, by = 222, 228
    d.rounded_rectangle((bx, by, bx + 78, by + 32), 14, fill=(255, 255, 255, 235))
    d.polygon([(bx + 10, by + 30), (bx + 2, by + 44), (bx + 24, by + 31)], fill=(255, 255, 255, 235))
    d.text((bx + 39, by + 16), '¡Miau!', font=font(14), fill=(60, 30, 50), anchor='mm')
    # bottom bar
    d.rounded_rectangle((10, H - 64, W - 10, H - 8), 26, fill=(0, 0, 0, 170))
    labels = [('▶', 'Jugar'), ('★', 'Tienda'), ('⚙', 'Ajustes')]
    for k, (g, t) in enumerate(labels):
        cx = W / 2 + (k - 1) * 112
        d.text((cx, H - 44), g, font=sym(20), fill=(255, 210, 90) if k == 0 else (255, 255, 255), anchor='mm')
        d.text((cx, H - 22), t, font=font(12), fill=(255, 255, 255), anchor='mm')
    scr.alpha_composite(ov)
    scr.save(os.path.join(out, 'screen_casita.png'))


def sheet_mila(rdir, out):
    names = ['idle_s', 'walk_e', 'push_e', 'idle_n', 'walk_w']
    ims = [Image.open(os.path.join(rdir, 'mila_%s.png' % n)).convert('RGBA') for n in names]
    acc = ['acc_bow', 'acc_party', 'acc_crown', 'acc_game']
    ims2 = [Image.open(os.path.join(rdir, 'mila_%s.png' % n)).convert('RGBA') for n in acc]
    pad = 16
    Wd = max(sum(i.width for i in ims), sum(i.width for i in ims2)) + pad * 6
    Hd = max(i.height for i in ims) + max(i.height for i in ims2) + pad * 3
    sh = Image.new('RGBA', (Wd, Hd), (218, 170, 118, 255))
    x = pad
    for i in ims:
        sh.alpha_composite(i, (x, pad))
        x += i.width + pad
    y = pad * 2 + max(i.height for i in ims)
    x = pad
    for i in ims2:
        sh.alpha_composite(i, (x, y))
        x += i.width + pad
    sh.save(os.path.join(out, 'mila_sheet.png'))
    # the game-scale Mila (what the watch really draws, x1) next to x3
    g = Image.open(os.path.join(rdir, 'mila_idle_s.png')).convert('RGBA')
    small = g.resize((round(g.width / 3), round(g.height / 3)), Image.LANCZOS)
    small.save(os.path.join(out, 'mila_game_scale.png'))


def main():
    rdir = os.path.abspath(sys.argv[1])
    out = os.path.abspath(sys.argv[2])
    os.makedirs(out, exist_ok=True)
    for cam in ('A', 'B'):
        screens_level(rdir, out, cam)
    screen_shop(rdir, out)
    screen_casita(rdir, out)
    sheet_mila(rdir, out)
    print('ok', out)


if __name__ == '__main__':
    main()
