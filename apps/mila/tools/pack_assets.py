#!/usr/bin/env python3
"""
MILA - packs the Blender renders into mila.pak, the one file the watch
reads (main/ml_art.c has the layout). From Monster Hop's packer.

    python3 tools/pack_assets.py [--only dir,dir] [-v]   # -> assets/mila.pak
                                                         #    and assets/card/ (in parts)

Every assets/<dir>/meta.json is read (tools/blender/SPEC.md). Sprites become
SHEETS: the frames of an animation (names ending _NN) or the variants of a
tile (_vK) go together under the base name; a sprite's shadow and glow
become <base>_sh and <base>_gl (a shadow-only sprite named <x>_sh too).
Palettes (palettes.json in a dir) become pal_<name> blobs, levels
(assets/levels/*.bin) lvl_<name> blobs, assets/levels/worlds.txt the
"worlds" blob, and each kit's furniture names a props_<kit> blob.
"""
import json
import os
import re
import struct
import subprocess
import sys

import numpy as np
from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ASSETS = os.path.join(ROOT, 'assets')
OUT = os.path.join(ASSETS, 'mila.pak')
CARD = os.path.join(ASSETS, 'card')          # the same, in parts under 8 MB
PART = 7 * 1024 * 1024
LZ4 = '/tmp/ml_lz4blk'
NAME_LEN = 32

COL, LID, PLANE, GLOW, IMG, RGB, BLOB = 1, 2, 3, 4, 5, 6, 8
BPP = {COL: 4, LID: 3, PLANE: 1, GLOW: 2, IMG: 3, RGB: 2}


def kits():
    """the kits the worlds table names (tools/levels.py wrote it)"""
    out = []
    with open(os.path.join(ASSETS, 'levels', 'worlds.txt')) as fh:
        for line in fh:
            if line.startswith('kit '):
                k = line.split()[1]
                if k not in out:
                    out.append(k)
    return tuple(out)

BAYER = np.array([[0, 8, 2, 10], [12, 4, 14, 6], [3, 11, 1, 9], [15, 7, 13, 5]], np.int32)


def lz4(data):
    if not os.path.exists(LZ4) or os.path.getmtime(LZ4) < os.path.getmtime(os.path.join(ROOT, 'tools', 'lz4blk.c')):
        subprocess.check_call(['cc', '-O2', '-o', LZ4, os.path.join(ROOT, 'tools', 'lz4blk.c')])
    return subprocess.run([LZ4], input=data, stdout=subprocess.PIPE, check=True).stdout


def rgb565(rgb, dither=True):
    """rgb: (h, w, 3) uint8 -> (h, w) uint16, ordered dither"""
    h, w = rgb.shape[:2]
    r = rgb[..., 0].astype(np.int32)
    g = rgb[..., 1].astype(np.int32)
    b = rgb[..., 2].astype(np.int32)
    if dither:
        d = BAYER[np.arange(h)[:, None] & 3, np.arange(w)[None, :] & 3]
        r = np.clip(r + (d >> 1) - 4, 0, 255)
        g = np.clip(g + (d >> 2) - 2, 0, 255)
        b = np.clip(b + (d >> 1) - 4, 0, 255)
    return (((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)).astype(np.uint16)


def load(d, fn, mode):
    return np.asarray(Image.open(os.path.join(d, fn)).convert(mode))


def encode_frame(fmt, cover, planes, ax, ay):
    """cover: (h, w) bool of pixels that exist; planes: per-pixel byte arrays
    (h, w, bpp) uint8. Crops to cover, returns the frame's bytes."""
    ys, xs = np.nonzero(cover)
    if len(ys) == 0:
        # an empty frame: 1x1 with nothing in it
        return struct.pack('<hhhhI', 1, 1, 0, 0, 0) + struct.pack('<HH', 0, 0) + struct.pack('<I', 0)
    y0, y1, x0, x1 = ys.min(), ys.max() + 1, xs.min(), xs.max() + 1
    cov = cover[y0:y1, x0:x1]
    pl = planes[y0:y1, x0:x1]
    h, w = cov.shape
    spans, offs, data = [], [], bytearray()
    for y in range(h):
        row = np.nonzero(cov[y])[0]
        offs.append(len(data))
        if len(row) == 0:
            spans.append((0, 0))
            continue
        a, b = int(row.min()), int(row.max()) + 1
        spans.append((a, b))
        data.extend(pl[y, a:b].tobytes())
    while len(data) & 3:
        data.append(0)
    out = struct.pack('<hhhhI', w, h, int(ax - x0), int(ay - y0), len(data))
    out += b''.join(struct.pack('<HH', a, b) for a, b in spans)
    out += b''.join(struct.pack('<I', o) for o in offs)
    return out + bytes(data)


def frame_main(d, info):
    f = info['files']
    img = load(d, f['img'], 'RGBA')
    al = img[..., 3]
    z = load(d, f['z'], 'L') if 'z' in f else np.full(al.shape, 128, np.uint8)
    cover = al > 0
    if 'id' in f:
        ids = load(d, f['id'], 'L') // 16
        light = np.round(img[..., :3].astype(np.float32).mean(axis=2)).astype(np.uint8)
        a4 = np.clip((al.astype(np.int32) + 8) >> 4, 0, 15)
        cover &= a4 > 0
        ida = ((ids.astype(np.int32) << 4) | a4).astype(np.uint8)
        pl = np.stack([ida, light, z], axis=2)
        return LID, encode_frame(LID, cover, pl, info.get('ax', 0), info.get('ay', 0))
    c = rgb565(img[..., :3])
    if info.get('kind') == 'map':
        # a map panel: opaque on black, every pixel, 2 bytes
        pl = np.stack([(c & 255).astype(np.uint8), (c >> 8).astype(np.uint8)], axis=2)
        return RGB, encode_frame(RGB, np.ones(al.shape, bool), pl, info.get('ax', 0), info.get('ay', 0))
    if info.get('kind') in ('ui', 'map_stone', 'map_marker'):
        # interface art: colour and alpha for LVGL, no depth; opaque images
        # keep every pixel so a row is one copy
        pl = np.stack([(c & 255).astype(np.uint8), (c >> 8).astype(np.uint8), al], axis=2)
        return IMG, encode_frame(IMG, al > 0 if (al < 255).any() else np.ones(al.shape, bool), pl, 0, 0)
    pl = np.stack([(c & 255).astype(np.uint8), (c >> 8).astype(np.uint8), al, z], axis=2)
    return COL, encode_frame(COL, cover, pl, info.get('ax', 0), info.get('ay', 0))


def frame_plane(d, fn, info):
    a = load(d, fn, 'L')
    return encode_frame(PLANE, a > 3, a[..., None], info.get('ax', 0), info.get('ay', 0))


def frame_glow(d, fn, info):
    g = load(d, fn, 'RGB')
    c = rgb565(g, dither=False)
    cover = c > 0
    pl = np.stack([(c & 255).astype(np.uint8), (c >> 8).astype(np.uint8)], axis=2)
    return encode_frame(GLOW, cover, pl, info.get('ax', 0), info.get('ay', 0))


def split_name(name):
    """(base, index) for sheet grouping; the shop's turntable frames stay one
    entry each (the shop loads only the one it shows)"""
    if '_turn_' in name:
        return name, 0
    m = re.match(r'^(.*)_(\d\d)$', name)
    if m:
        return m.group(1), int(m.group(2))
    m = re.match(r'^(.*)_v(\d)$', name)
    if m:
        return m.group(1), int(m.group(2))
    return name, 0


def main():
    only = []
    verbose = '-v' in sys.argv
    # the levels first: assets/levels/ is generated (and not in git), and a
    # level with problems stops the pack
    r = subprocess.run([sys.executable, os.path.join(ROOT, 'tools', 'levels.py')],
                       capture_output=True, text=True)
    if r.returncode != 0:
        sys.exit('levels.py found problems:\n' + r.stdout)
    if '--only' in sys.argv:
        only = sys.argv[sys.argv.index('--only') + 1].split(',')
    dirs = sorted(d for d in os.listdir(ASSETS) if os.path.isfile(os.path.join(ASSETS, d, 'meta.json'))
                  and not d.startswith('_'))
    if only:
        dirs = [d for d in dirs if d in only]
    sheets = {}        # name -> [fmt, ms, {index: bytes}]
    props = {}         # kit -> furniture names
    KITS = kits()
    blobs = {}
    total_raw = 0

    def add(name, fmt, idx, data, ms=0):
        if len(name) >= NAME_LEN:
            sys.exit('name too long (%d): %s' % (len(name), name))
        s = sheets.setdefault(name, [fmt, ms, {}])
        if s[0] != fmt:
            sys.exit('%s: frames in two formats' % name)
        if idx in s[2]:
            sys.exit('%s: frame %d twice' % (name, idx))
        s[2][idx] = data
        if ms and not s[1]:
            s[1] = ms

    for dn in dirs:
        d = os.path.join(ASSETS, dn)
        with open(os.path.join(d, 'meta.json')) as fh:
            meta = json.load(fh)
        n = 0
        for name, info in sorted(meta.items()):
            if name.startswith('_') or not isinstance(info, dict) or 'files' not in info:
                continue
            f = info['files']
            if 'img' not in f:
                if 'sh' in f and (name.endswith('_sh') or '_sh_' in name):
                    # a shadow rendered on its own (a pushable thing's: x_v0_sh;
                    # a gate frame's: x_sh_02)
                    base, idx = split_name(name[:-3] if name.endswith('_sh') else name.replace('_sh_', '_', 1))
                    add(base + '_sh', PLANE, idx, frame_plane(d, f['sh'], info), int(info.get('ms', 0) or 0))
                continue
            base, idx = split_name(name)
            ms = int(info.get('ms', 0) or 0)
            fmt, data = frame_main(d, info)
            if info.get('kind') == 'ui' and fmt == COL:
                pass
            add(base, fmt, idx, data, ms)
            if 'nodes' in info or 'exit' in info:
                # a map panel's places: "nodes x,y x,y\nentry x,y\nexit x,y\ndoor x,y\nhouse x0,y0,x1,y1"
                lines = ['nodes ' + ' '.join('%d,%d' % (round(x), round(y)) for x, y in info.get('nodes', []))]
                for k in ('entry', 'exit', 'door', 'house'):
                    if info.get(k):
                        lines.append(k + ' ' + ','.join(str(int(round(v))) for v in info[k]))
                blobs[base + '_nodes'] = ('\n'.join(lines)).encode() + b'\0'
            if base.startswith(tuple(k + '_prop' for k in KITS)):
                kit = base.split('_prop')[0]
                props.setdefault(kit, set()).add(base[len(kit) + 1:])
            if 'sh' in f:
                add(base + '_sh', PLANE, idx, frame_plane(d, f['sh'], info), ms)
            if 'gl' in f:
                add(base + '_gl', GLOW, idx, frame_glow(d, f['gl'], info), ms)
            n += 1
        pal = os.path.join(d, 'palettes.json')
        if os.path.exists(pal):
            with open(pal) as fh:
                pals = json.load(fh)

            def put_pal(nm, p):
                b = bytearray(48)
                for k, v in p.items():
                    if str(k).isdigit() and 0 <= int(k) < 16 and isinstance(v, (list, tuple)) and len(v) >= 3:
                        b[int(k) * 3:int(k) * 3 + 3] = bytes(int(c) & 255 for c in v[:3])
                blobs['pal_' + nm] = bytes(b)

            def walk(prefix, obj):
                if isinstance(obj, dict) and any(str(k).isdigit() for k in obj):
                    put_pal(prefix, obj)
                elif isinstance(obj, dict):
                    for k, v in obj.items():
                        if k == '_variants' and isinstance(v, dict):
                            # {"zombie": {"office": {...}, ...}} -> pal_zombie_0, _1...
                            for mon, var in v.items():
                                items = list(var.values()) if isinstance(var, dict) else list(var)
                                for i, pv in enumerate(items):
                                    walk('%s_%d' % (mon, i), pv)
                            continue
                        if k.startswith('_'):
                            continue
                        walk(prefix + '_' + k if prefix else k, v)
                elif isinstance(obj, list):
                    for i, v in enumerate(obj):
                        walk('%s_%d' % (prefix, i), v)
            walk('', pals)
        print('%-14s %4d sprites' % (dn, n))

    lv = os.path.join(ASSETS, 'levels')
    if os.path.isdir(lv):
        for fn in sorted(os.listdir(lv)):
            if fn.endswith('.bin'):
                with open(os.path.join(lv, fn), 'rb') as fh:
                    blobs['lvl_' + fn[:-4]] = fh.read()
        with open(os.path.join(lv, 'worlds.txt'), 'rb') as fh:
            blobs['worlds'] = fh.read() + b'\0'
    for kit, names in props.items():
        one = sorted(n for n in names if n.startswith('prop_') and not n.endswith('_sh'))
        two = sorted(n for n in names if n.startswith('prop2_') and not n.endswith('_sh'))
        blobs['props_' + kit] = (' '.join(one) + '\n' + ' '.join(two)).encode() + b'\0'

    entries = []
    gaps = []
    for name, (fmt, ms, frames) in sheets.items():
        idx = sorted(frames)
        if idx != list(range(len(idx))):
            # a partial render (a style sample): the frames there are, in order
            gaps.append(name)
        raw = b''.join(frames[i] for i in idx)
        entries.append((name, fmt, len(idx), ms, raw))
    for name, raw in list(blobs.items()):
        if len(name) >= NAME_LEN:
            if name.startswith('pal_'):
                continue            # a sample look nobody asks for by name
            sys.exit('name too long: ' + name)
        entries.append((name, BLOB, 0, 0, raw))
    entries.sort(key=lambda e: e[0].encode())
    if gaps:
        print('%d sheets with missing frames (packed as they are): %s%s' %
              (len(gaps), ', '.join(sorted(gaps)[:8]), ' ...' if len(gaps) > 8 else ''))

    hdr_len = 8 + 48 * len(entries)
    table, data = bytearray(), bytearray()
    for name, fmt, nf, ms, raw in entries:
        c = lz4(raw)
        total_raw += len(raw)
        table += name.encode().ljust(NAME_LEN, b'\0')
        table += struct.pack('<BBHIII', fmt, nf, ms, hdr_len + len(data), len(c), len(raw))
        data += c
        if verbose:
            print('  %-31s fmt %d x%-3d raw %7d lz4 %7d' % (name, fmt, nf, len(raw), len(c)))
    with open(OUT, 'wb') as fh:
        fh.write(b'MHPK' + struct.pack('<HH', 1, len(entries)))
        fh.write(table)
        fh.write(data)
    print('%s: %d entries, %.2f MB raw, %.2f MB on the card' %
          (os.path.relpath(OUT, ROOT), len(entries), total_raw / 1e6, (hdr_len + len(data)) / 1e6))
    # the card's copy in parts, because the portal takes 8 MB per upload:
    # mila.pak, mila.pak.1, ... read back as one file (ml_art.c)
    blob = open(OUT, 'rb').read()
    os.makedirs(CARD, exist_ok=True)
    for f in os.listdir(CARD):
        if f.startswith('mila.pak'):
            os.remove(os.path.join(CARD, f))
    parts = [blob[i:i + PART] for i in range(0, len(blob), PART)]
    for i, part in enumerate(parts):
        with open(os.path.join(CARD, 'mila.pak' + ('.%d' % i if i else '')), 'wb') as fh:
            fh.write(part)
    print('%s: %d part(s) for the card' % (os.path.relpath(CARD, ROOT), len(parts)))


if __name__ == '__main__':
    main()
