#!/usr/bin/env python3
"""Scan files for runs of Korean text in Johab (KSSM) and Wansung (EUC-KR)."""
import sys, codecs

def runs(data, codec, ok):
    out, i, start, buf = [], 0, None, []
    while i < len(data) - 1:
        b1, b2 = data[i], data[i+1]
        ch = None
        if b1 >= 0x80 and ok(b1, b2):
            try:
                ch = bytes([b1, b2]).decode(codec)
            except UnicodeDecodeError:
                ch = None
        if ch and '가' <= ch <= '힣':
            if start is None: start = i
            buf.append(ch); i += 2; continue
        if ch is None and start is not None and 0x20 <= b1 < 0x7f:
            buf.append(chr(b1)); i += 1; continue
        if start is not None:
            s = ''.join(buf).rstrip()
            if sum('가' <= c <= '힣' for c in s) >= 3:
                out.append((start, s))
            start, buf = None, []
        i += 1
    return out

for fn in sys.argv[1:]:
    d = open(fn, 'rb').read()
    for name, codec, ok in [
        ('johab', 'johab', lambda a, b: 0x84 <= a <= 0xD3 and (0x41 <= b <= 0x7E or 0x81 <= b <= 0xFE)),
        ('wansung', 'euc_kr', lambda a, b: 0xB0 <= a <= 0xC8 and 0xA1 <= b <= 0xFE)]:
        r = runs(d, codec, ok)
        print(f'== {fn} [{name}] {len(r)} runs')
        for off, s in r[:15]:
            print(f'  {off:#08x}: {s[:70]}')
