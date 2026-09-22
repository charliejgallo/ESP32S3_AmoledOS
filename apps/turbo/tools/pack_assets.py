#!/usr/bin/env python3
"""
TURBO - packs the Blender renders into turbo.pak, the one file the watch reads.

    python3 tools/pack_assets.py            # -> assets/turbo.pak

Layout (little endian), the same as Golf's golf.pak:

    "TBPK" u16 version u16 count
    count x entry (50 bytes):
        char name[24]  u8 type  u8 flags  u16 w  u16 h  i16 ax  i16 ay
        u32 off  u32 clen  u32 rawlen  u16 ppm8  (px per metre x 8)  u16 extra
    data: one LZ4 block per entry

Types:
    1  sprite:  raw = w*h RGB565 (LE, ordered dither) then w*h alpha bytes
    2  plane:   raw = w*h bytes, how much a shadow darkens
    4  vehicle: raw = w*h * 2 bytes: (id << 4 | alpha >> 4), light
               (the watch colours it with the paint: tb_art.h)

Names: near_<car>_y<k> / nsh_ (its shadow), far_<vehicle>_<l|c|r> / fsh_,
p_<prop> / psh_, bg_<stage>. ax, ay is the anchor inside the crop: the
ground point under the rear bumper for vehicles, the base for props.
"""
import json, os, struct, sys
from PIL import Image
import numpy as np

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CARS = os.path.join(ROOT, 'assets', 'cars')
PROPS = os.path.join(ROOT, 'assets', 'props')
OUT = os.path.join(ROOT, 'assets', 'turbo.pak')
FAR_SCALE = 0.7
# the arches span the road and are drawn big only for an instant
PROP_SCALE = {'checkpoint': 0.67, 'finish': 0.67}

def lz4_compress(src: bytes) -> bytes:
    """Greedy LZ4 block compressor (hash of 4 bytes, one candidate)."""
    n = len(src)
    out = bytearray()
    table = {}
    anchor = 0
    i = 0
    mv = memoryview(src)

    def emit(lit_start, lit_end, off, mlen):
        lit = lit_end - lit_start
        tok_l = min(lit, 15)
        tok_m = 0 if mlen is None else min(mlen - 4, 15)
        out.append((tok_l << 4) | tok_m)
        if lit >= 15:
            r = lit - 15
            while r >= 255:
                out.append(255)
                r -= 255
            out.append(r)
        out.extend(mv[lit_start:lit_end])
        if mlen is None:
            return
        out.extend(struct.pack('<H', off))
        if mlen - 4 >= 15:
            r = mlen - 4 - 15
            while r >= 255:
                out.append(255)
                r -= 255
            out.append(r)

    while i + 4 <= n:
        key = bytes(mv[i:i + 4])
        cand = table.get(key)
        table[key] = i
        if cand is not None and i - cand <= 65535:
            m = 4
            while i + m < n and src[cand + m] == src[i + m] and m < 65535:
                m += 1
            emit(anchor, i, i - cand, m)
            # index a few positions inside the match
            j = i + 1
            end = i + m
            while j < end - 3 and j < i + 16:
                table[bytes(mv[j:j + 4])] = j
                j += 1
            i += m
            anchor = i
        else:
            i += 1
    emit(anchor, n, 0, None)
    return bytes(out)


def lz4_decompress(src: bytes, rawlen: int) -> bytes:
    out = bytearray()
    i = 0
    while i < len(src):
        tok = src[i]; i += 1
        lit = tok >> 4
        if lit == 15:
            while True:
                b = src[i]; i += 1
                lit += b
                if b != 255:
                    break
        out.extend(src[i:i + lit]); i += lit
        if i >= len(src):
            break
        off = src[i] | (src[i + 1] << 8); i += 2
        m = (tok & 15) + 4
        if (tok & 15) == 15:
            while True:
                b = src[i]; i += 1
                m += b
                if b != 255:
                    break
        for _ in range(m):
            out.append(out[-off])
    assert len(out) == rawlen, (len(out), rawlen)
    return bytes(out)


entries = []


def add(name, typ, w, h, ax, ay, raw, ppm=0.0, extra=0, flags=0):
    assert len(name) < 24, name
    c = lz4_compress(raw)
    assert lz4_decompress(c, len(raw)) == raw
    entries.append(dict(name=name, type=typ, w=w, h=h, ax=int(round(ax)), ay=int(round(ay)),
                        raw=len(raw), data=c, ppm8=int(round(ppm * 8)), extra=extra, flags=flags))


BAYER = np.array([[0, 8, 2, 10], [12, 4, 14, 6], [3, 11, 1, 9], [15, 7, 13, 5]], np.int32)


def rgb565_dither(a):
    h, w = a.shape[:2]
    d = np.tile(BAYER, (h // 4 + 1, w // 4 + 1))[:h, :w]
    r = np.clip(a[..., 0].astype(np.int32) + (d >> 1) - 4, 0, 255)
    g = np.clip(a[..., 1].astype(np.int32) + (d >> 2) - 2, 0, 255)
    b = np.clip(a[..., 2].astype(np.int32) + (d >> 1) - 4, 0, 255)
    return (((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)).astype('<u2')


def crop_box(mask, pad=0):
    ys, xs = np.nonzero(mask)
    if len(xs) == 0:
        return None
    return xs.min(), xs.max() + 1, ys.min(), ys.max() + 1


def add_sprite(name, path, anchor, ppm, extra=0, scale=1.0):
    im = Image.open(path).convert('RGBA')
    if scale != 1.0:
        # premultiplied for the resize, so transparent edges do not bleed dark
        a = np.asarray(im).astype(np.float32) / 255.0
        a[..., :3] *= a[..., 3:4]
        im = Image.fromarray((a * 255).astype(np.uint8), 'RGBA')
        im = im.resize((max(1, int(im.width * scale)), max(1, int(im.height * scale))), Image.LANCZOS)
        a = np.asarray(im).astype(np.float32) / 255.0
        al = np.maximum(a[..., 3:4], 1e-4)
        a[..., :3] = np.clip(a[..., :3] / al, 0, 1)
        im = Image.fromarray((a * 255).astype(np.uint8), 'RGBA')
        anchor = (anchor[0] * scale, anchor[1] * scale)
        ppm *= scale
    a = np.asarray(im)
    box = crop_box(a[..., 3] > 0)
    if box is None:
        return
    x0, x1, y0, y1 = box
    c = a[y0:y1, x0:x1]
    raw = rgb565_dither(c).tobytes() + c[..., 3].astype(np.uint8).tobytes()
    add(name, 1, int(x1 - x0), int(y1 - y0), anchor[0] - x0, anchor[1] - y0, raw, ppm, extra)


def add_plane(name, path, anchor, ppm=0.0, cut=10, scale=1.0):
    im = Image.open(path).convert('L')
    if scale != 1.0:
        im = im.resize((max(1, int(im.width * scale)), max(1, int(im.height * scale))), Image.BOX)
        anchor = (anchor[0] * scale, anchor[1] * scale)
        ppm *= scale
    s = np.asarray(im).astype(np.int32)
    # quantised: a soft shadow loses nothing and the render's noise stops
    # eating the LZ4
    s = (np.round(s / 8) * 8).clip(0, 255)
    s[s < cut] = 0
    box = crop_box(s > 0)
    if box is None:
        return
    x0, x1, y0, y1 = box
    add(name, 2, int(x1 - x0), int(y1 - y0), anchor[0] - x0, anchor[1] - y0,
        s[y0:y1, x0:x1].astype(np.uint8).tobytes(), ppm)


def add_vehicle(name, base, anchor, ppm, scale=1.0):
    shi = Image.open(base + '_shade.png').convert('RGBA')
    idi = Image.open(base + '_id.png').convert('L')
    if scale != 1.0:
        size = (max(1, int(shi.width * scale)), max(1, int(shi.height * scale)))
        shi = shi.resize(size, Image.BOX)
        idi = idi.resize(size, Image.NEAREST)
        anchor = (anchor[0] * scale, anchor[1] * scale)
        ppm *= scale
    sh = np.asarray(shi)
    ids = np.asarray(idi).astype(np.int32)
    alpha = sh[..., 3].astype(np.int32)
    light = sh[..., :3].astype(np.int32).mean(axis=2)
    idv = (ids + 8) // 16
    idv[alpha == 0] = 0
    box = crop_box(alpha > 0)
    x0, x1, y0, y1 = box
    a4 = np.clip((alpha[y0:y1, x0:x1] + 8) // 17, 0, 15)
    b0 = ((idv[y0:y1, x0:x1] & 15) << 4) | a4
    b1 = np.clip(np.round(light[y0:y1, x0:x1]), 0, 255)
    b1[a4 == 0] = 0
    raw = np.stack([b0, b1], axis=-1).astype(np.uint8).tobytes()
    add(name, 4, int(x1 - x0), int(y1 - y0), anchor[0] - x0, anchor[1] - y0, raw, ppm)


def pack_cars():
    mp = os.path.join(CARS, 'meta.json')
    if not os.path.exists(mp):
        print('no car renders yet')
        return
    meta = json.load(open(mp))
    for key, r in meta['renders'].items():
        base = os.path.join(CARS, key)
        if not os.path.exists(base + '_shade.png'):
            continue
        anchor = r['anchor']
        ppm = r.get('ppm', 0.0)
        near = key.startswith('near_')
        # far views at 70 %: a car beside ours (4 m away) still needs no
        # magnifying, and the eight vehicles' 24 views fit in ~1.3 MB
        add_vehicle(key, base, anchor, ppm, 1.0 if near else FAR_SCALE)
        sp = base + '_shadow.png'
        if not os.path.exists(sp):
            continue
        if near:
            # one shadow per car, the straight frame's: the yawed ones differ
            # by a few pixels under a soft blur
            if key.endswith('_y3'):
                add_plane('nsh_' + key[5:-3], sp, anchor, 0.0, cut=24)
        else:
            add_plane('fsh_' + key[4:], sp, anchor, ppm, cut=16, scale=0.5)


def pack_props():
    mp = os.path.join(PROPS, 'meta.json')
    if not os.path.exists(mp):
        print('no prop renders yet')
        return
    meta = json.load(open(mp))
    for name, m in meta.get('props', {}).items():
        path = os.path.join(PROPS, m.get('file', name + '.png'))
        if not os.path.exists(path):
            continue
        add_sprite('p_' + name, path, m['anchor'], m['ppm'], int(m.get('height_m', 0) * 10),
                   PROP_SCALE.get(name, 1.0))
        sf = m.get('shadow_file')
        if sf and os.path.exists(os.path.join(PROPS, sf)):
            add_plane('psh_' + name, os.path.join(PROPS, sf), m.get('shadow_anchor', m['anchor']), m['ppm'])
    for stage, m in meta.get('backdrops', {}).items():
        path = os.path.join(PROPS, m.get('file', 'bg_%s.png' % stage) if isinstance(m, dict) else 'bg_%s.png' % stage)
        if not os.path.exists(path):
            continue
        a = np.asarray(Image.open(path).convert('RGBA'))
        h, w = a.shape[:2]
        raw = rgb565_dither(a).tobytes() + a[..., 3].astype(np.uint8).tobytes()
        add('bg_' + stage, 1, w, h, 0, h - 1, raw)


def main():
    pack_cars()
    pack_props()
    hdr = b'TBPK' + struct.pack('<HH', 1, len(entries))
    off = len(hdr) + 50 * len(entries)
    table = bytearray()
    blob = bytearray()
    for e in entries:
        table += struct.pack('<24sBBHHhhIIIHH', e['name'].encode(), e['type'], e['flags'], e['w'], e['h'],
                             e['ax'], e['ay'], off + len(blob), len(e['data']), e['raw'],
                             e['ppm8'], e['extra'] & 0xFFFF)
        blob += e['data']
    with open(OUT, 'wb') as f:
        f.write(hdr + table + blob)
    raw = sum(e['raw'] for e in entries)
    kinds = {}
    for e in entries:
        kinds[e['type']] = kinds.get(e['type'], 0) + 1
    print('%d entries (%s), %d KB raw -> %d KB in %s' % (len(entries), kinds, raw // 1024,
                                                         (len(hdr) + len(table) + len(blob)) // 1024, OUT))


if __name__ == '__main__':
    main()
