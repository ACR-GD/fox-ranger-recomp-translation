#!/usr/bin/env python3
"""classify_segments.py IMG.EXE functions.csv - per-segment profile to help
assign recompilation modules: size, function count, CRT calls, port/INT use,
far-call fan-in/out and the DGROUP strings each segment references."""
import capstone, collections, csv, re, struct, sys
from capstone import x86

HDR, DGROUP = 0x25E0, 0x1ADF
CRT = {0x7A0: 'fopen', 0x7BC: 'fread', 0x6A6: 'fclose', 0x19CC: 'farmalloc', 0x1DF: 'exit',
       0xAC6: 'puts', 0x1D7C: 'fseek', 0x1D1C: 'strlen', 0x229F: 'findfirst', 0x22D2: 'setvect',
       0x2214: 'CRT?2214', 0x2148: 'memcpy?', 0x2470: 'strcpy?', 0x24CE: 'strcat?'}

f = open(sys.argv[1], 'rb').read(); img = f[HDR:]
ds = img[DGROUP * 16:DGROUP * 16 + 0x2180]
funcs = list(csv.DictReader(open(sys.argv[2])))
md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_16); md.detail = True


def ds_string(off):
    if off >= len(ds) - 4:
        return None
    m = re.match(rb'[\x20-\x7e]{4,40}\x00', ds[off:off + 41])
    return m.group()[:-1].decode() if m else None


prof = collections.defaultdict(lambda: dict(n=0, size=0, crt=collections.Counter(), io=collections.Counter(),
                                            out=collections.Counter(), inn=collections.Counter(), strs=set()))
for r in funcs:
    seg = int(r['seg'], 16); lin = int(r['linear'], 16); size = int(r['size'])
    p = prof[seg]; p['n'] += 1; p['size'] += size
    for i in md.disasm(img[lin:lin + max(size, 1)], lin - seg * 16):
        m = i.mnemonic
        if m == 'lcall' and i.operands[0].type == x86.X86_OP_IMM:
            ts, to = i.operands[0].imm, i.operands[1].imm
            if ts == 0x134D:
                p['crt'][CRT.get(to, f'crt{to:04X}')] += 1
            elif ts != seg:
                p['out'][ts] += 1; prof[ts]['inn'][seg] += 1
        elif m in ('in', 'out'):
            p['io'][m] += 1
        elif m == 'int':
            p['io'][f'int{i.operands[0].imm:02X}'] += 1
        elif m == 'mov' and len(i.operands) == 2 and i.operands[1].type == x86.X86_OP_IMM:
            s = ds_string(i.operands[1].imm & 0xFFFF)
            if s and sum(c.isalpha() for c in s) >= 3:
                p['strs'].add(s)

for seg in sorted(prof):
    p = prof[seg]
    if not p['n']:
        continue
    print(f'== seg {seg:04X}  funcs {p["n"]:3}  bytes {p["size"]:6}')
    if p['crt']: print('   crt :', dict(p['crt'].most_common(8)))
    if p['io']: print('   io  :', dict(p['io']))
    print('   calls->', ' '.join(f'{s:04X}x{n}' for s, n in p['out'].most_common(8)))
    print('   <-from', ' '.join(f'{s:04X}x{n}' for s, n in p['inn'].most_common(8)))
    if p['strs']: print('   strs:', sorted(p['strs'])[:10])
