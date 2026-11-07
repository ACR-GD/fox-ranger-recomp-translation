#!/usr/bin/env python3
"""dis.py IMAGE.EXE START [LEN] - disassemble load-image offset range (hex) of an MZ."""
import sys, struct, capstone
f = open(sys.argv[1], 'rb').read()
hdr = struct.unpack_from('<H', f, 8)[0] * 16
img = f[hdr:]
s = int(sys.argv[2], 16); n = int(sys.argv[3], 16) if len(sys.argv) > 3 else 0x100
CRT = {0x7a0: 'fopen', 0x7bc: 'fread', 0x6a6: 'fclose', 0x19cc: 'farmalloc', 0x1df: 'exit', 0xac6: 'puts'}
md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_16)
for i in md.disasm(img[s:s + n], s):
    note = ''
    if i.mnemonic == 'lcall' and i.op_str.startswith('0x134d'):
        note = '  ; ' + CRT.get(int(i.op_str.split(',')[1], 16), '?crt')
    print(f'{i.address:05x}: {i.mnemonic:6} {i.op_str}{note}')
