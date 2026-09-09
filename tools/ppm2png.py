#!/usr/bin/env python3
"""PPM (P6) to PNG, with no dependencies.

It exists because screencapture -R stopped working on this Mac (the screen
recording permission is missing) and the captures come out black. Rather than
fight that, the apps dump their own buffer to PPM and this turns it into PNG.

    python3 tools/ppm2png.py /tmp/jefe00.ppm
"""

import sys, zlib, struct
def conv(src, dst):
    d = open(src,'rb').read()
    # P6 <w> <h> 255\n header
    parts = d.split(b'\n', 3)
    w, h = map(int, parts[1].split())
    px = parts[3]
    raw = b''.join(b'\x00' + px[y*w*3:(y+1)*w*3] for y in range(h))
    def chunk(t, data):
        c = t + data
        return struct.pack('>I', len(data)) + c + struct.pack('>I', zlib.crc32(c))
    png = (b'\x89PNG\r\n\x1a\n'
           + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0))
           + chunk(b'IDAT', zlib.compress(raw, 9))
           + chunk(b'IEND', b''))
    open(dst,'wb').write(png)
for a in sys.argv[1:]:
    conv(a, a.replace('.ppm', '.png'))
