#!/usr/bin/env python3
"""Takes a screenshot of the board's display and leaves it as a PNG.

    python3 tools/captura.py 192.168.1.116
    python3 tools/captura.py 192.168.1.116 /tmp/settings.png
    python3 tools/captura.py 192.168.1.116 --sin-despertar

It exists because the log does not show the one thing that matters about an
interface: whether a glyph is missing, whether some text runs off, whether two
things are on top of each other. The simulator's audit catches a lot of that,
but the simulator is not the board.

By default it WAKES the screen. Without that you nearly always photograph the
always-on face -the time alone on black-, because the watch dims after a minute
and by the time you ask for the capture that has already happened.
--sin-despertar takes it exactly as it is, which is what you want when that
face IS the thing being looked at.

The board sends BMP because it has nothing to encode a PNG with: lodepng is
compiled as a decoder, for the photo viewer. The conversion happens here, with
no dependencies, by the same trick as tools/ppm2png.py.
"""

import struct
import sys
import urllib.request
import zlib


def bmp_a_png(bmp, destino):
    """Only the BMP the board sends: 24-bit, uncompressed, bottom-up."""
    if bmp[:2] != b"BM":
        raise SystemExit(f"eso no es un BMP: {bmp[:60]!r}")
    off, = struct.unpack_from("<I", bmp, 10)
    w, = struct.unpack_from("<i", bmp, 18)
    h, = struct.unpack_from("<i", bmp, 22)
    bpp, = struct.unpack_from("<H", bmp, 28)
    if bpp != 24:
        raise SystemExit(f"esperaba 24 bits por pixel, vinieron {bpp}")

    fila_bytes = ((w * 3 + 3) // 4) * 4
    filas = []
    for y in range(h - 1, -1, -1):          # a BMP is stored bottom-up
        i = off + y * fila_bytes
        fila = bmp[i:i + w * 3]
        # BGR -> RGB
        filas.append(b"\x00" + bytes(
            fila[x + 2 - c] for x in range(0, w * 3, 3) for c in range(3)))
    raw = b"".join(filas)

    def chunk(t, data):
        c = t + data
        return struct.pack(">I", len(data)) + c + struct.pack(">I", zlib.crc32(c))

    png = (b"\x89PNG\r\n\x1a\n"
           + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
           + chunk(b"IDAT", zlib.compress(raw, 9))
           + chunk(b"IEND", b""))
    open(destino, "wb").write(png)
    return w, h


def main():
    args = [a for a in sys.argv[1:] if a != "--sin-despertar"]
    despertar = "--sin-despertar" not in sys.argv[1:]
    if not args:
        raise SystemExit(__doc__)

    host = args[0]
    destino = args[1] if len(args) > 1 else "captura.png"
    url = f"http://{host}/api/captura" + ("" if despertar else "?sin_despertar=1")

    try:
        # Deliberately generous: waking the screen takes ~1 s on the board's
        # side, and it is half a megabyte over wifi.
        with urllib.request.urlopen(url, timeout=45) as r:
            bmp = r.read()
    except urllib.error.HTTPError as e:
        raise SystemExit(f"la placa dijo que no: {e.code} {e.read().decode('utf-8', 'replace')}")

    w, h = bmp_a_png(bmp, destino)
    print(f"{destino}  {w}x{h}  ({len(bmp) // 1024} KB de BMP)")


if __name__ == "__main__":
    main()
