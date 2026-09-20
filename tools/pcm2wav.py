#!/usr/bin/env python3
"""Pone una cabecera WAV a un volcado crudo de 16 bits mono.

El simulador se traga las muestras del parlante en streaming, asi que Chatarra
con CH_WAV=<archivo> escribe lo que genera su sintetizador. Esto lo vuelve
escuchable:

    python3 tools/pcm2wav.py /tmp/x.pcm /tmp/x.wav [frecuencia]
"""
import struct, sys

def main():
    if len(sys.argv) < 3:
        print(__doc__); return 1
    crudo = open(sys.argv[1], 'rb').read()
    hz = int(sys.argv[3]) if len(sys.argv) > 3 else 16000
    with open(sys.argv[2], 'wb') as f:
        f.write(b'RIFF' + struct.pack('<I', 36 + len(crudo)) + b'WAVEfmt ')
        f.write(struct.pack('<IHHIIHH', 16, 1, 1, hz, hz * 2, 2, 16))
        f.write(b'data' + struct.pack('<I', len(crudo)) + crudo)
    print('%s  %.1f s a %d Hz' % (sys.argv[2], len(crudo) / 2.0 / hz, hz))
    return 0

sys.exit(main())
