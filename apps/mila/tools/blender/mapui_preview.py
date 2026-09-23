#!/usr/bin/env python3
"""Mila - previews of the map and the UI art (system python, PIL).

    python3 mapui_preview.py

Reads assets/map and assets/ui (their meta.json) and writes
  assets/map/_sample.png (+ _sample_2x.png): the map screen, scrolled to the
      top of map_home joined to map_living, the stones at their nodes (done,
      current with Mila on it, locked), a HUD with the UI icons;
  assets/map/_strip.png: the whole strip (home + living), unscrolled;
  assets/ui/_sample.png (+ _sample_2x.png): the logo, the emblem and the icons.
The panels chain by their exit/entry points (the watch does the same).
"""
import json
import os

from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
ASSETS = os.path.normpath(os.path.join(HERE, '..', '..', 'assets'))
MAP = os.path.join(ASSETS, 'map')
UI = os.path.join(ASSETS, 'ui')
SW, SH = 368, 448


def font(size):
    for p in ('/System/Library/Fonts/SFNSRounded.ttf', '/System/Library/Fonts/Supplemental/Arial Rounded Bold.ttf',
              '/System/Library/Fonts/Helvetica.ttc'):
        if os.path.exists(p):
            try:
                f = ImageFont.truetype(p, size)
                try:
                    f.set_variation_by_name('Bold')
                except Exception:
                    pass
                return f
            except Exception:
                continue
    return ImageFont.load_default()


def load(d):
    with open(os.path.join(d, 'meta.json')) as fh:
        meta = json.load(fh)
    return meta


def img(d, meta, name):
    return Image.open(os.path.join(d, meta[name]['files']['img'])).convert('RGBA')


def strip(meta, panels):
    """Stack the panels bottom-up, joined at exit/entry. Returns the picture
    and each panel's top y in it."""
    h = sum(meta[p]['h'] for p in panels)
    out = Image.new('RGBA', (SW, h), (0, 0, 0, 255))
    tops = {}
    y = h
    for p in panels:
        y -= meta[p]['h']
        out.alpha_composite(img(MAP, meta, p), (0, y))
        tops[p] = y
    return out, tops


def put(canvas, meta, name, x, y, d=MAP):
    """Draw a sprite with its anchor pixel (ax, ay) at (x, y)."""
    m = meta[name]
    canvas.alpha_composite(img(d, meta, name), (int(x - m['ax']), int(y - m['ay'])))


def pill(d, box, fill=(0, 0, 0, 150)):
    """A translucent rounded pill (blended, not replaced)."""
    im = d._image
    ov = Image.new('RGBA', im.size, (0, 0, 0, 0))
    x0, y0, x1, y1 = box
    ImageDraw.Draw(ov).rounded_rectangle(box, radius=(y1 - y0) // 2, fill=fill)
    im.alpha_composite(ov)


def disc(d, box, fill=(0, 0, 0, 150)):
    im = d._image
    ov = Image.new('RGBA', im.size, (0, 0, 0, 0))
    ImageDraw.Draw(ov).ellipse(box, fill=fill)
    im.alpha_composite(ov)


PANELS = ['map_home', 'map_living', 'map_kitchen', 'map_garden', 'map_attic', 'map_roofs', 'map_soon']


def map_strip():
    """The whole strip with the stones: living half done (Mila on stone 3),
    the other worlds locked (the watch also dims a locked world's panel)."""
    meta = load(MAP)
    ui = load(UI) if os.path.exists(os.path.join(UI, 'meta.json')) else {}
    panels = [p for p in PANELS if p in meta]
    full, tops = strip(meta, panels)
    stars = {'map_living': [3, 2]}
    for p in panels:
        for i, (x, y) in enumerate(meta[p].get('nodes') or []):
            y += tops[p]
            if p == 'map_living':
                kind = 'map_stone_done' if i < 2 else ('map_stone' if i == 2 else 'map_stone_locked')
            else:
                kind = 'map_stone_locked'
            put(full, meta, kind, x, y)
            if p == 'map_living' and i == 2:
                put(full, meta, 'map_stone_ring', x, y)
                put(full, meta, 'map_mila', x, y)
    if 'icon_star' in ui:
        st = img(UI, ui, 'icon_star').resize((14, 14), Image.LANCZOS)
        se = img(UI, ui, 'icon_star_empty').resize((14, 14), Image.LANCZOS)
        for p, ss in stars.items():
            for i, n in enumerate(ss):
                x, y = meta[p]['nodes'][i]
                y += tops[p] + 17
                for k in range(3):
                    full.alpha_composite(st if k < n else se, (x - 22 + k * 15, y))
    full.convert('RGB').save(os.path.join(MAP, '_strip.png'))
    return full, tops, meta, ui


def map_screen(full, tops, meta, ui, y0, title, emblem, fn):
    scr = full.crop((0, y0, SW, y0 + SH))
    d = ImageDraw.Draw(scr)
    f = font(15)
    pill(d, (10, 10, 104, 38))
    if 'icon_coin' in ui:
        scr.alpha_composite(img(UI, ui, 'icon_coin').resize((24, 24), Image.LANCZOS), (14, 12))
    d.text((44, 24), '245', font=f, fill=(255, 220, 120), anchor='lm')
    pill(d, (SW - 104, 10, SW - 10, 38))
    if 'icon_star' in ui:
        scr.alpha_composite(img(UI, ui, 'icon_star').resize((24, 24), Image.LANCZOS), (SW - 100, 12))
    d.text((SW - 72, 24), '5/120', font=font(14), fill=(255, 255, 255), anchor='lm')
    if emblem in ui:
        em = img(UI, ui, emblem).resize((44, 44), Image.LANCZOS)
        pill(d, (SW // 2 - 62, 6, SW // 2 + 62, 42))
        scr.alpha_composite(em, (SW // 2 - 66, 2))
        d.text((SW // 2 + 10, 24), title, font=font(16), fill=(255, 255, 255), anchor='mm')
    if 'icon_shop' in ui:
        disc(d, (SW - 58, SH - 58, SW - 10, SH - 10))
        scr.alpha_composite(img(UI, ui, 'icon_shop').resize((30, 30), Image.LANCZOS), (SW - 49, SH - 49))
    if 'icon_home' in ui:
        disc(d, (10, SH - 58, 58, SH - 10))
        scr.alpha_composite(img(UI, ui, 'icon_home').resize((30, 30), Image.LANCZOS), (19, SH - 49))
    scr = scr.convert('RGB')
    scr.save(os.path.join(MAP, fn + '.png'))
    scr.resize((SW * 2, SH * 2), Image.NEAREST).save(os.path.join(MAP, fn + '_2x.png'))


def locked_look(full, tops, meta, p):
    """What the watch does to a locked world's panel: dimmed, a lock and the
    stars it needs (a guess of the engine's look, for the preview only)."""
    y0, h = tops[p], meta[p]['h']
    reg = full.crop((0, y0, SW, y0 + h))
    ov = Image.new('RGBA', reg.size, (0, 0, 0, 120))
    reg.alpha_composite(ov)
    full.paste(reg, (0, y0))


def ui_sheet():
    ui = load(UI)
    scr = Image.new('RGBA', (SW, SH), (0, 0, 0, 255))
    d = ImageDraw.Draw(scr)
    lg = img(UI, ui, 'ui_logo')
    scr.alpha_composite(lg, ((SW - lg.width) // 2, 34))
    em = img(UI, ui, 'ui_emblem_living')
    scr.alpha_composite(em, (SW // 2 - 36, 176))
    d.text((SW // 2, 262), 'Living', font=font(16), fill=(255, 255, 255), anchor='mm')
    icons = ['icon_coin', 'icon_star', 'icon_star_empty', 'icon_undo', 'icon_shop']
    icons = [n for n in icons if n in ui]
    x = (SW - len(icons) * 56) // 2 + 8
    for n in icons:
        scr.alpha_composite(img(UI, ui, n), (x, 300))
        d.text((x + 20, 350), n.replace('icon_', ''), font=font(10), fill=(170, 165, 180), anchor='mm')
        x += 56
    # the icons as they sit in the HUD: on dark pills and round buttons
    pill(d, (24, 380, 132, 412))
    scr.alpha_composite(img(UI, ui, 'icon_coin').resize((26, 26), Image.LANCZOS), (29, 383))
    d.text((62, 396), '245', font=font(16), fill=(255, 220, 120), anchor='lm')
    disc(d, (150, 372, 198, 420), (255, 255, 255, 40))
    scr.alpha_composite(img(UI, ui, 'icon_undo').resize((32, 32), Image.LANCZOS), (158, 380))
    disc(d, (214, 372, 262, 420), (255, 255, 255, 40))
    scr.alpha_composite(img(UI, ui, 'icon_shop').resize((32, 32), Image.LANCZOS), (222, 380))
    for k in range(3):
        scr.alpha_composite(img(UI, ui, 'icon_star' if k < 2 else 'icon_star_empty').resize((24, 24), Image.LANCZOS),
                            (278 + k * 24, 384))
    scr = scr.convert('RGB')
    scr.save(os.path.join(UI, '_sample.png'))
    scr.resize((SW * 2, SH * 2), Image.NEAREST).save(os.path.join(UI, '_sample_2x.png'))


def ui_full_sheet():
    """Every UI picture at 1x on black with its name (and the logo/emblems
    at 2x, nearest, for a close look)."""
    ui = load(UI)
    names = sorted(k for k in ui if not k.startswith('_'))
    logo = [n for n in names if n == 'ui_logo']
    ems = [n for n in names if n.startswith('ui_emblem_')]
    icons = [n for n in names if n.startswith('icon_')]
    W_ = 640
    rows = 1 + (len(ems) + 5) // 6 + (len(icons) + 8) // 9
    H_ = 150 + ((len(ems) + 5) // 6) * 110 + ((len(icons) + 8) // 9) * 78 + 30
    sh = Image.new('RGBA', (W_, H_), (0, 0, 0, 255))
    d = ImageDraw.Draw(sh)
    lg = img(UI, ui, 'ui_logo')
    sh.alpha_composite(lg, (20, 12))
    sh.alpha_composite(lg.resize((lg.width, lg.height), Image.NEAREST), (20, 12))
    d.text((330, 60), 'ui_logo  %dx%d' % (lg.width, lg.height), font=font(13), fill=(170, 165, 180), anchor='lm')
    y = 150
    for k, n in enumerate(ems):
        x = 20 + (k % 6) * 102
        if k and k % 6 == 0:
            y += 110
        sh.alpha_composite(img(UI, ui, n), (x, y))
        d.text((x + 36, y + 84), n.replace('ui_emblem_', ''), font=font(11), fill=(170, 165, 180), anchor='mm')
    y += 110
    for k, n in enumerate(icons):
        x = 20 + (k % 9) * 68
        if k and k % 9 == 0:
            y += 78
        sh.alpha_composite(img(UI, ui, n), (x + 8, y))
        d.text((x + 28, y + 52), n.replace('icon_', ''), font=font(10), fill=(170, 165, 180), anchor='mm')
    sh = sh.convert('RGB')
    sh.save(os.path.join(UI, '_sheet.png'))
    sh.resize((W_ * 2, H_ * 2), Image.NEAREST).save(os.path.join(UI, '_sheet_2x.png'))


if __name__ == '__main__':
    full, tops, meta, ui = map_strip()
    # 1: home joined to the living room (Mila on stone 3, the ring under her)
    map_screen(full, tops, meta, ui, tops['map_home'] - 262, 'Living', 'ui_emblem_living', '_sample')
    # 2: the attic joined to the roofs under the moon
    if 'map_roofs' in tops:
        map_screen(full, tops, meta, ui, tops['map_attic'] - 250, 'Roofs', 'ui_emblem_roofs', '_sample_roofs')
    if os.path.exists(os.path.join(UI, 'meta.json')):
        ui_sheet()
        ui_full_sheet()
    print('ok')
