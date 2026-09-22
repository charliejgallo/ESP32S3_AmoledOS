#!/usr/bin/env python3
"""
GOLF - packs the Blender renders into golf.pak, the one file the watch reads.

    python3 tools/pack_assets.py            # -> assets/golf.pak

Layout (little endian):

    "GFPK" u16 version u16 count
    count x entry (50 bytes):
        char name[24]  u8 type  u8 flags  u16 w  u16 h  i16 ax  i16 ay
        u32 off  u32 clen  u32 rawlen  u16 ppm8  (px per metre x 8)  u16 extra
    data: one LZ4 block per entry

Types:
    1  sprite:  raw = w*h RGB565 (LE) then w*h alpha bytes. Alpha is straight.
    2  plane:   raw = w*h bytes (a shadow's strength); flags 1 = stored at
               half resolution, ax/ay in half pixels too
    3  golfer:  raw = w*h * 2 bytes: (id << 4 | alpha >> 4), shade.
               ax, ay = where the crop's top-left sits in the 368x448 frame.

The LZ4 block format is the standard one (token, literals, offset, match);
gf_art.c decodes it in thirty lines.
"""
import json, os, struct, sys
from PIL import Image
import numpy as np

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PROPS = os.path.join(ROOT, 'assets', 'props')
RENDER = os.path.join(ROOT, 'assets', 'render')
OUT = os.path.join(ROOT, 'assets', 'golf.pak')


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


def rgb565(a):
    r = a[..., 0].astype(np.uint16)
    g = a[..., 1].astype(np.uint16)
    b = a[..., 2].astype(np.uint16)
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)


def sprite_raw(img):
    a = np.asarray(img.convert('RGBA'))
    px = rgb565(a).astype('<u2').tobytes()
    al = a[..., 3].astype(np.uint8).tobytes()
    return px + al


def pack_props():
    meta = json.load(open(os.path.join(PROPS, 'meta.json')))
    kinds = ['tree_pine', 'tree_oak', 'tree_poplar', 'tree_palm', 'bush']
    for i, k in enumerate(kinds):
        m = meta[k]
        side = Image.open(os.path.join(PROPS, m['side_files'][0]))
        add(k[:12] + '_s', 1, side.width, side.height, m['side_base_px'][0], m['side_base_px'][1],
            sprite_raw(side), m['side_px_per_m'], int(m['height_m'] * 100))
        top = Image.open(os.path.join(PROPS, k + '_top.png'))
        add(k[:12] + '_t', 1, top.width, top.height, m['top_trunk_px'][0], m['top_trunk_px'][1],
            sprite_raw(top), m['top_px_per_m'], int(m['canopy_diameter_m'] * 100))
        sh = Image.open(os.path.join(PROPS, k + '_topshadow.png')).convert('L')
        add(k[:12] + '_h', 2, sh.width, sh.height, m['top_trunk_px'][0], m['top_trunk_px'][1],
            np.asarray(sh).astype(np.uint8).tobytes(), m['top_px_per_m'])
    fm = meta.get('flag', {})
    for f in range(4):
        p = os.path.join(PROPS, 'flag_side_%d.png' % f)
        if os.path.exists(p):
            im = Image.open(p)
            base = fm.get('side_base_px', [3.0, 62.85])
            add('flag_%d' % f, 1, im.width, im.height, base[0], base[1], sprite_raw(im),
                fm.get('side_px_per_m', 26.6))


def golfer_frame(prefix, name, hat=False):
    shp = os.path.join(RENDER, prefix + '_shade.png')
    idp = os.path.join(RENDER, prefix + '_id.png')
    if not os.path.exists(shp):
        return False
    sh = np.asarray(Image.open(shp).convert('RGBA'))
    ids = np.asarray(Image.open(idp).convert('L')).astype(np.int32)
    alpha = sh[..., 3].astype(np.int32)
    shade = sh[..., :3].astype(np.int32).mean(axis=2)
    idv = (ids + 8) // 16
    cover = (alpha > 0) | (idv > 0)
    # pixels with coverage but no id borrow a neighbour's id
    if (cover & (idv == 0)).any():
        from itertools import product
        for _ in range(3):
            hole = cover & (idv == 0)
            if not hole.any():
                break
            for dx, dy in product((-1, 0, 1), repeat=2):
                sft = np.roll(np.roll(idv, dy, 0), dx, 1)
                fill = hole & (sft > 0)
                idv[fill] = sft[fill]
                hole = cover & (idv == 0)
    idv[alpha == 0] = 0
    ys, xs = np.nonzero(alpha > 0)
    if len(xs) == 0:
        add(name, 3, 1, 1, 0, 0, b'\0\0')
        return True
    x0, x1, y0, y1 = xs.min(), xs.max() + 1, ys.min(), ys.max() + 1
    a4 = np.clip((alpha[y0:y1, x0:x1] + 8) // 17, 0, 15)
    b0 = ((idv[y0:y1, x0:x1] & 15) << 4) | a4
    b1 = np.clip(np.round(shade[y0:y1, x0:x1]), 0, 255)
    b1[a4 == 0] = 0
    raw = np.stack([b0, b1], axis=-1).astype(np.uint8).tobytes()
    add(name, 3, int(x1 - x0), int(y1 - y0), x0, y0, raw)
    if not hat:
        sp = os.path.join(RENDER, prefix + '_shadow.png')
        if os.path.exists(sp):
            # Half resolution, smoothed and quantised: a soft shadow loses
            # nothing and the render's sampling noise stops eating the LZ4.
            # flags = 1 tells the watch to scale it back up.
            im = Image.open(sp).convert('L')
            im = im.resize((im.width // 2, im.height // 2), Image.BOX)
            s = np.asarray(im).astype(np.float32)
            k = np.array([1, 2, 1], np.float32) / 4
            s = np.apply_along_axis(lambda r: np.convolve(r, k, 'same'), 1, s)
            s = np.apply_along_axis(lambda c: np.convolve(c, k, 'same'), 0, s)
            s = (np.round(s / 12) * 12).clip(0, 255).astype(np.uint8)
            s[s < 12] = 0
            ys, xs = np.nonzero(s > 0)
            if len(xs):
                sx0, sx1, sy0, sy1 = xs.min(), xs.max() + 1, ys.min(), ys.max() + 1
                add(name + 's', 2, int(sx1 - sx0), int(sy1 - sy0), sx0, sy0, s[sy0:sy1, sx0:sx1].tobytes(), flags=1)
    return True


def pack_golfer():
    mp = os.path.join(RENDER, 'meta.json')
    if not os.path.exists(mp):
        print('no golfer renders yet')
        return
    meta = json.load(open(mp))
    seqs = meta.get('sequences', meta)
    names = [s for s in ('swing', 'idle', 'cheer', 'sad', 'turn') if s in seqs]
    for s in names:
        n = seqs[s].get('frames', seqs[s].get('frame_count', 0)) if isinstance(seqs[s], dict) else 0
        if isinstance(n, list):
            n = len(n)
        for f in range(n):
            base = '%s_%02d' % (s, f)
            golfer_frame(base, base)
            for k in range(6):
                golfer_frame('hat%d_%s' % (k, base), 'h%d%s%02d' % (k, s[:2], f), hat=True)


def main():
    pack_props()
    pack_golfer()
    hdr = b'GFPK' + struct.pack('<HH', 1, len(entries))
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
    print('%d entries, %d KB raw -> %d KB in %s' % (len(entries), raw // 1024,
                                                   (len(hdr) + len(table) + len(blob)) // 1024, OUT))


if __name__ == '__main__':
    main()
