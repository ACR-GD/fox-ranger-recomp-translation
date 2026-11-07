#!/usr/bin/env python3
"""ppm2png.py IN.ppm... - convert P6 PPMs to PNG next to them (stdlib only); optional 2x scale."""
import struct, sys, zlib
for p in sys.argv[1:]:
    d = open(p, 'rb').read()
    parts = d.split(b'\n', 3)
    w, h = map(int, parts[1].split()); px = parts[3]
    rows = []
    for y in range(h):
        r = px[y * w * 3:(y + 1) * w * 3]
        r2 = b''.join(r[i:i + 3] * 2 for i in range(0, len(r), 3))
        rows += [b'\0' + r2] * 2
    c = lambda t, x: struct.pack('>I', len(x)) + t + x + struct.pack('>I', zlib.crc32(t + x))
    open(p[:-4] + '.png', 'wb').write(b'\x89PNG\r\n\x1a\n' + c(b'IHDR', struct.pack('>IIBBBBB', 2 * w, 2 * h, 8, 2, 0, 0, 0))
                                       + c(b'IDAT', zlib.compress(b''.join(rows))) + c(b'IEND', b''))
