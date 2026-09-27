#!/usr/bin/env python3
"""
Offline maps for the Maps app, from the command line.

The portal's /mapas page does this in the browser; this is the same thing for
zones too big to download through a watch, or to fill a card straight from
the computer. It fetches OpenFreeMap's vector tiles for a box, keeps only the
layers the watch draws, and writes what the app reads from <card>/maps:

    <slug>.amp, <slug>.2.amp ...   the tiles, in packs of at most 7.5 MB
    <slug>.idx                     the names, for the watch's search

    python3 tools/map_pack.py "Palermo" --center=-34.5885,-58.4305 --km 3
    python3 tools/map_pack.py "Buenos Aires" --bbox=-58.53,-34.71,-58.33,-34.53 --out /Volumes/SD/maps
    python3 tools/map_pack.py --reindex caba.idx amba.idx     first version's text index -> AIX2

The formats are documented in apps/mapas/main/mp_store.h (.amp) and
apps/mapas/main/mp_search.h (.idx), and must match what components/aos_web/
mapas.html writes. Data (c) OpenStreetMap contributors, OpenMapTiles,
OpenFreeMap; mind their terms for very large downloads.
"""
import argparse
import json
import math
import os
import re
import struct
import sys
import unicodedata
from concurrent.futures import ThreadPoolExecutor
from urllib.request import Request, urlopen
from urllib.error import HTTPError

TILEJSON = "https://tiles.openfreemap.org/planet"
PART = int(7.5 * 1024 * 1024)
KEEP = {"water", "waterway", "landcover", "landuse", "park", "building", "aeroway",
        "transportation", "transportation_name", "boundary", "place", "water_name"}
NAMED = {"transportation_name": "calle", "place": "lugar", "poi": "poi", "water_name": "agua",
         "park": "parque", "aerodrome_label": "aeropuerto", "mountain_peak": "cerro"}
UA = {"User-Agent": "AmoledOS map_pack/1.0 (+https://github.com/charliejgallo/ESP32S3_AmoledOS)"}


# --------------------------------------------------------------- protobuf --

def varint(b, i):
    r = s = 0
    while True:
        c = b[i]
        i += 1
        r |= (c & 0x7F) << s
        s += 7
        if c < 0x80:
            return r, i


def fields(b, a=0, e=None):
    """(field, wire, value or (start, end), start of the field)"""
    e = len(b) if e is None else e
    i = a
    while i < e:
        st = i
        k, i = varint(b, i)
        f, w = k >> 3, k & 7
        if w == 0:
            v, i = varint(b, i)
            yield f, w, v, st, i
        elif w == 2:
            n, i = varint(b, i)
            yield f, w, (i, i + n), st, i + n
            i += n
        elif w == 1:
            i += 8
            yield f, w, None, st, i
        elif w == 5:
            i += 4
            yield f, w, None, st, i
        else:
            raise ValueError("mvt")


def layer_name(b, a, e):
    for f, w, v, _, _ in fields(b, a, e):
        if f == 1 and w == 2:
            return b[v[0]:v[1]].decode("utf-8", "replace")
    return ""


def strip(b):
    out = bytearray()
    for f, w, v, st, en in fields(b):
        if f == 3 and w == 2 and layer_name(b, *v) in KEEP:
            out += b[st:en]
    return bytes(out)


def zz(v):
    return (v >> 1) ^ -(v & 1)


def names(b, z, x, y, emit):
    for f, w, v, _, _ in fields(b):
        if f != 3 or w != 2:
            continue
        a, e = v
        name, ext, keys, vals, feats = "", 4096, [], [], []
        for f2, w2, v2, _, _ in fields(b, a, e):
            if f2 == 1 and w2 == 2:
                name = b[v2[0]:v2[1]].decode("utf-8", "replace")
            elif f2 == 2 and w2 == 2:
                feats.append(v2)
            elif f2 == 3 and w2 == 2:
                keys.append(b[v2[0]:v2[1]].decode("utf-8", "replace"))
            elif f2 == 4 and w2 == 2:
                vals.append(v2)
            elif f2 == 5 and w2 == 0:
                ext = v2
        kind = NAMED.get(name)
        if not kind:
            continue

        def sval(i):
            if i >= len(vals):
                return ""
            for f3, w3, v3, _, _ in fields(b, *vals[i]):
                if f3 == 1 and w3 == 2:
                    return b[v3[0]:v3[1]].decode("utf-8", "replace")
            return ""
        kn = keys.index("name") if "name" in keys else -1
        kl = keys.index("name:latin") if "name:latin" in keys else -1
        kc = keys.index("class") if "class" in keys else -1
        for fa, fe in feats:
            tags = geom = None
            for f3, w3, v3, _, _ in fields(b, fa, fe):
                if f3 == 2 and w3 == 2:
                    tags = v3
                elif f3 == 4 and w3 == 2:
                    geom = v3
            if not tags or not geom:
                continue
            nm = lat = cl = ""
            i = tags[0]
            while i < tags[1]:
                k, i = varint(b, i)
                vv, i = varint(b, i)
                if k == kn:
                    nm = sval(vv)
                elif k == kl:
                    lat = sval(vv)
                elif k == kc:
                    cl = sval(vv)
            nm = nm or lat
            if not nm:
                continue
            pts, px, py, i = [], 0, 0, geom[0]
            while i < geom[1] and len(pts) < 2000:
                c, i = varint(b, i)
                cid, n = c & 7, c >> 3
                if cid == 7:
                    continue
                for _ in range(n):
                    dx, i = varint(b, i)
                    dy, i = varint(b, i)
                    px += zz(dx)
                    py += zz(dy)
                    pts.append((px, py))
            if not pts:
                continue
            fx, fy = pts[len(pts) // 2][0] / ext, pts[len(pts) // 2][1] / ext
            if kind != "calle" and not (0 <= fx < 1 and 0 <= fy < 1):
                continue
            n2 = 1 << z
            lon = (x + fx) / n2 * 360 - 180
            la = math.degrees(math.atan(math.sinh(math.pi * (1 - 2 * (y + fy) / n2))))
            emit(nm, cl if kind == "poi" and cl else kind, la, lon, z)


def key(s):
    s = unicodedata.normalize("NFD", s)
    s = "".join(c for c in s if unicodedata.category(c) != "Mn").lower()
    return re.sub(r"\s+", " ", s).strip()


# ------------------------------------------------------------ the index --
#
# AIX2, little-endian (the watch's reader is apps/mapas/main/mp_search.c, the
# browser's writer components/aos_web/mapas.html; the three must agree):
#
#   0  "AIX2"   4 names   8 keys   12 kinds   16 kinds_off   20 blocks_off
#   24 keys_off   28 names_off   32 block (256)   36 reserved
#   kinds   NUL-terminated strings ("lugar", "calle", "agua", "school"...)
#   blocks  the first key of every 256, 9 bytes each: the watch loads these
#   keys    16 bytes each, sorted: key[9], class, flags (1: the name's first
#           word), name length (normalised, capped at 255), name offset (u32)
#   names   lat i32, lon i32 (1e6), kind u8, the name in UTF-8, NUL
#
# A key is the normalised name from one of its words on, 9 bytes: "avenida
# rivadavia" gives "avenida r" and "rivadavia". Normalised the way the watch
# does it: Latin-1 letters without accent, lower case, anything else beyond
# ASCII dropped, runs of spaces as one.

FOLD = "AAAAAAACEEEEIIIIDNOOOOOxOUUUUYTsaaaaaaaceeeeiiiidnooooo/ouuuuyty"
STOP = {"de", "del", "la", "las", "el", "los", "y", "e", "a", "al", "en", "of", "the", "da", "do", "dos"}
SKIP_KINDS = {"bus"}            # stops named after their corner: the streets are there already
KEY_LEN, BLOCK, MAX_KEYS = 9, 256, 6


def norm(s):
    out, space = [], True
    for ch in s:
        c = ord(ch)
        if 0xC0 <= c <= 0xFF:
            ch = FOLD[c - 0xC0]
        elif c >= 0x80:
            continue
        if ch.isspace():
            if not space:
                out.append(" ")
            space = True
            continue
        space = False
        out.append(ch.lower() if "A" <= ch <= "Z" else ch)
    return "".join(out).rstrip()


def alnum(c):
    return "a" <= c <= "z" or "0" <= c <= "9"


def word_keys(n):
    """(key, first word?) for every word worth a key"""
    out = []
    for i, c in enumerate(n):
        if not alnum(c) or (i and alnum(n[i - 1])):
            continue
        j = i
        while j < len(n) and alnum(n[j]):
            j += 1
        w = n[i:j]
        if i and (w in STOP or (len(w) == 1 and not w.isdigit())):
            continue
        out.append((n[i:i + KEY_LEN], i == 0))
        if len(out) >= MAX_KEYS:
            break
    return out


def kind_class(k):
    return {"lugar": 0, "calle": 1, "agua": 2}.get(k, 3)


def write_idx(entries):
    """entries: (name, kind, lat, lon) -> the AIX2 file's bytes"""
    # bus stops, and bus lines named "135 - Rivadavia" that come as streets
    entries = [e for e in entries if e[1] not in SKIP_KINDS and not re.match(r"^\d+\s*-\s", e[0])]
    freq = {}
    for e in entries:
        freq[e[1]] = freq.get(e[1], 0) + 1
    kinds = [k for k, _ in sorted(freq.items(), key=lambda kv: -kv[1])][:255]
    if "poi" not in kinds:
        kinds = kinds[:254] + ["poi"]
    kind_ix = {k: i for i, k in enumerate(kinds)}
    names, keys = bytearray(), []
    for name, kind, la, lo in entries:
        n = norm(name)
        if not n:
            continue
        ki = kind_ix.get(kind, kind_ix["poi"])
        off = len(names)
        nb = name.encode("utf-8")[:60].decode("utf-8", "ignore").encode("utf-8")
        names += struct.pack("<iiB", round(la * 1e6), round(lo * 1e6), ki) + nb + b"\0"
        for k, first in word_keys(n):
            keys.append((k.encode("ascii").ljust(KEY_LEN, b"\0"), kind_class(kinds[ki]), 1 if first else 0,
                         min(255, len(n)), off))
    keys.sort()
    kb = b"".join(k.encode("ascii") + b"\0" for k in kinds)
    blocks = b"".join(keys[i][0] for i in range(0, len(keys), BLOCK))
    kr = b"".join(struct.pack("<9sBBBI", *k) for k in keys)
    kinds_off = 40
    blocks_off = kinds_off + len(kb)
    keys_off = blocks_off + len(blocks)
    names_off = keys_off + len(kr)
    head = b"AIX2" + struct.pack("<9I", len(entries), len(keys), len(kinds), kinds_off, blocks_off,
                                 keys_off, names_off, BLOCK, 0)
    return head + kb + blocks + kr + bytes(names)


def read_idx_v1(path):
    """the text index of the first version: key, name, kind, lat, lon"""
    out = []
    with open(path, encoding="utf-8") as f:
        for line in f:
            p = line.rstrip("\n").split("\t")
            if len(p) >= 5:
                out.append((p[1], p[2], int(p[3]) / 1e6, int(p[4]) / 1e6))
    return out


def slug(s):
    s = key(s)
    return (re.sub(r"[^a-z0-9]+", "-", s).strip("-")[:24]) or "zona"


# -------------------------------------------------------------------- tiles --

def lon2x(lo, z):
    return int((lo + 180) / 360 * (1 << z))


def lat2y(la, z):
    r = math.radians(la)
    return int((1 - math.log(math.tan(r) + 1 / math.cos(r)) / math.pi) / 2 * (1 << z))


def tiles_in(w, s, e, n, minz, maxz):
    out = []
    for z in range(minz, maxz + 1):
        m = (1 << z) - 1
        for x in range(max(0, lon2x(w, z)), min(m, lon2x(e, z)) + 1):
            for y in range(max(0, lat2y(n, z)), min(m, lat2y(s, z)) + 1):
                out.append((z, x, y))
    return out


def fetch(url):
    for _ in range(3):
        try:
            with urlopen(Request(url, headers=UA), timeout=30) as r:
                return r.read()
        except HTTPError as e:
            if e.code in (204, 404):
                return b""
        except Exception:
            pass
    raise RuntimeError("could not fetch " + url)


def pack(name, box, tiles):
    tiles = sorted(tiles)
    n = len(tiles)
    head = bytearray(64)
    head[0:4] = b"AMP1"
    struct.pack_into("<I", head, 4, n)
    head[8] = min(t[0] for t in tiles)
    head[9] = max(t[0] for t in tiles)
    struct.pack_into("<4i", head, 12, *(round(v * 1e6) for v in box))
    nb = name.encode("utf-8")[:31].decode("utf-8", "ignore").encode("utf-8")   # no letter cut in half
    head[28:28 + len(nb)] = nb
    struct.pack_into("<I", head, 60, 64)
    idx, data, off = bytearray(), bytearray(), 64 + n * 20
    for z, x, y, d in tiles:
        idx += struct.pack("<B3xIIII", z, x, y, off, len(d))
        data += d
        off += len(d)
    return bytes(head + idx + data)


def main():
    if len(sys.argv) >= 3 and sys.argv[1] == "--reindex":
        # the first version's text index -> AIX2, in place of the same name
        for path in sys.argv[2:]:
            if open(path, "rb").read(4) == b"AIX2":
                print(path, "already AIX2")
                continue
            data = write_idx(read_idx_v1(path))
            with open(path, "wb") as f:
                f.write(data)
            print(path, struct.unpack_from("<I", data, 4)[0], "names,", len(data), "bytes")
        return
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("name")
    g = ap.add_mutually_exclusive_group(required=True)
    g.add_argument("--bbox", help="west,south,east,north in degrees")
    g.add_argument("--center", help="lat,lon")
    ap.add_argument("--km", type=float, default=3.0, help="with --center: the side of the square")
    ap.add_argument("--minz", type=int, default=6)
    ap.add_argument("--maxz", type=int, default=14)
    ap.add_argument("--out", default=".")
    ap.add_argument("--max-tiles", type=int, default=6000)
    a = ap.parse_args()

    if a.bbox:
        w, s, e, n = (float(v) for v in a.bbox.split(","))
    else:
        la, lo = (float(v) for v in a.center.split(","))
        dla = a.km / 2 / 111.32
        dlo = dla / math.cos(math.radians(la))
        w, s, e, n = lo - dlo, la - dla, lo + dlo, la + dla
    lst = tiles_in(w, s, e, n, a.minz, min(14, a.maxz))
    print(f"{len(lst)} tiles, z{a.minz}-{a.maxz}")
    if len(lst) > a.max_tiles:
        sys.exit(f"too many tiles (over {a.max_tiles}); shrink the box or pass --max-tiles")

    tpl = json.loads(fetch(TILEJSON))["tiles"][0]
    idx = {}
    done = []

    def work(t):
        z, x, y = t
        raw = fetch(tpl.replace("{z}", str(z)).replace("{x}", str(x)).replace("{y}", str(y)))
        return t, raw

    with ThreadPoolExecutor(6) as ex:
        for i, ((z, x, y), raw) in enumerate(ex.map(work, lst), 1):
            def emit(nm, kind, la, lo, zz_):
                grp = kind if kind in ("calle", "lugar", "agua") else "poi:" + kind
                k = (key(nm), grp)
                if k not in idx or zz_ > idx[k][4]:
                    idx[k] = (nm, kind, la, lo, zz_)
            if raw:
                names(raw, z, x, y, emit)
            done.append((z, x, y, strip(raw) if raw else b""))
            print(f"\r{i}/{len(lst)}", end="", flush=True)
    print()

    base = slug(a.name)
    os.makedirs(a.out, exist_ok=True)
    parts, cur, size = [], [], 0
    for t in sorted(done):
        if cur and size + len(t[3]) + 20 > PART - 64:
            parts.append(cur)
            cur, size = [], 0
        cur.append(t)
        size += len(t[3]) + 20
    if cur:
        parts.append(cur)
    for i, p in enumerate(parts):
        fn = os.path.join(a.out, base + (f".{i + 1}" if i else "") + ".amp")
        with open(fn, "wb") as f:
            f.write(pack(a.name, (w, s, e, n), p))
        print(fn, os.path.getsize(fn))
    data = write_idx([(v[0], v[1], v[2], v[3]) for v in idx.values()])
    fn = os.path.join(a.out, base + ".idx")
    with open(fn, "wb") as f:
        f.write(data)
    print(fn, struct.unpack_from("<I", data, 4)[0], "names,", len(data), "bytes")
    print(f"zones.txt line:  {a.name}\t{round((s + n) / 2 * 1e6)}\t{round((w + e) / 2 * 1e6)}\t150")


if __name__ == "__main__":
    main()
