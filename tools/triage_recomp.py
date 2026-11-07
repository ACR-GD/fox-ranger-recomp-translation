#!/usr/bin/env python3
"""triage_recomp.py - static-recompilation triage for 16-bit DOS MZ programs.

  * per-file size / Shannon entropy / magic / packer detection
  * recursive-descent disassembly (capstone) seeded from the entry point,
    relocated far-call/far-jump targets and `push bp; mov bp,sp` prologues
  * INT usage with AH sub-function tracking (INT 21h/10h/33h/16h/...)
  * IN/OUT port usage (immediate or tracked `mov dx, imm`) grouped by device
  * ISR installation (INT 21h AH=25h / direct IVT writes)
  * candidate function entry table (CSV)

usage: triage_recomp.py [--csv funcs.csv] FILE [FILE ...]
Packed EXEs are reported but not disassembled; unpack them first with
unpack_exe.py (emulation-based, handles PKLITE, EXEPACK, LZEXE, ...).
"""
import argparse, collections, math, os, struct, sys

try:
    import capstone
except ImportError:
    sys.exit('needs capstone: pip install capstone')

# ---------------------------------------------------------------- helpers
def entropy(d):
    if not d:
        return 0.0
    c = collections.Counter(d)
    return -sum(v / len(d) * math.log2(v / len(d)) for v in c.values())


def parse_mz(d):
    if len(d) < 0x1C or d[:2] not in (b'MZ', b'ZM'):
        return None
    f = struct.unpack_from('<13H', d, 2)
    last, pages, nrel, hdrpar, minal, maxal, ss, sp, csum, ip, cs, relofs, ovl = f
    size = pages * 512 - ((512 - last) if last else 0)
    relocs = [struct.unpack_from('<HH', d, relofs + 4 * i) for i in range(nrel)
              if relofs + 4 * i + 4 <= len(d)]
    return dict(hdr=hdrpar * 16, size=size, nrel=nrel, relofs=relofs, minalloc=minal,
                maxalloc=maxal, ss=ss, sp=sp, ip=ip, cs=cs, ovl=ovl,
                relocs=[s * 16 + o for o, s in relocs])


def detect_packer(d, mz):
    hits = []
    head = d[:0x200]
    if b'PKLITE' in head or b'PKlite' in head:
        hits.append('PKLITE v%d.%02d%s%s' % (d[0x1D] & 0x0F, d[0x1C], ' +extra' if d[0x1D] & 0x10 else '',
                                            ' +large' if d[0x1D] & 0x20 else ''))
    if d[0x1C:0x20] in (b'LZ09', b'LZ91'):
        hits.append('LZEXE ' + d[0x1C:0x20].decode())
    if b'diet' in head.lower() or b'dlz' in head:
        hits.append('DIET')
    if mz:
        entry = mz['hdr'] + ((mz['cs'] * 16 + mz['ip']) & 0xFFFFF)
        stub = d[entry:entry + 16]
        # Microsoft EXEPACK: header at CS:0 ends with 'RB', stub: mov bp,ax / mov ax,es / add ax,10h
        if stub.startswith(b'\x8b\xe8\x8c\xc0\x05\x10\x00'):
            hits.append('Microsoft EXEPACK')
        cs0 = mz['hdr'] + mz['cs'] * 16
        if d[cs0 + 0x0E:cs0 + 0x10] == b'RB' or d[cs0 + 0x10:cs0 + 0x12] == b'RB':
            if 'Microsoft EXEPACK' not in hits:
                hits.append('Microsoft EXEPACK (RB sig)')
        if b'W. Collis' in d[:0x400]:
            hits.append('unknown packer (W. Collis 1990 signature)')
        if not hits and entropy(d[mz['hdr']:mz['size']]) > 7.5:
            hits.append('unknown (high entropy image)')
    return hits


DEVICES = [
    (0x20, 0x21, 'PIC master'), (0xA0, 0xA1, 'PIC slave'), (0x40, 0x43, 'PIT timer'),
    (0x60, 0x64, 'keyboard ctrl'), (0x61, 0x61, 'PC speaker/port B'),
    (0x200, 0x207, 'gameport'), (0x220, 0x22F, 'Sound Blaster DSP/mixer (base 220)'),
    (0x240, 0x24F, 'Sound Blaster (base 240)'), (0x388, 0x38B, 'AdLib OPL2/OPL3'),
    (0x3C0, 0x3DA, 'VGA'), (0x00, 0x0F, 'DMA ch0-3'), (0x80, 0x8F, 'DMA page regs'),
    (0x330, 0x331, 'MPU-401 MIDI'),
]
VGA_REGS = {0x3C0: 'ATTR', 0x3C2: 'MISC_W', 0x3C4: 'SEQ_IDX', 0x3C5: 'SEQ_DATA', 0x3C6: 'DAC_MASK',
            0x3C7: 'DAC_RD_IDX', 0x3C8: 'DAC_WR_IDX', 0x3C9: 'DAC_DATA', 0x3CE: 'GC_IDX',
            0x3CF: 'GC_DATA', 0x3D4: 'CRTC_IDX', 0x3D5: 'CRTC_DATA', 0x3DA: 'INPUT_STATUS'}


def device(port):
    if port is None:
        return 'dynamic (DX unknown)'
    if isinstance(port, tuple):
        return 'base var [DS:%04X]%+d' % (port[1], port[2])
    names = [n for lo, hi, n in DEVICES if lo <= port <= hi]
    s = names[-1] if names else 'other'
    if port in VGA_REGS:
        s += ' ' + VGA_REGS[port]
    return s


IRQ_VECTORS = {0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x1C, 0x70, 0x71, 0x72, 0x73, 0x74, 0x75, 0x76, 0x77}
INT_NAMES = {0x10: 'BIOS video', 0x16: 'BIOS keyboard', 0x1A: 'BIOS time', 0x21: 'DOS',
             0x33: 'mouse', 0x2F: 'multiplex/XMS', 0x67: 'EMS', 0x15: 'BIOS sysconf',
             0x08: 'timer (direct)', 0x09: 'keyboard (direct)', 0x1C: 'user tick (direct)'}
DOS_FN = {0x09: 'print $str', 0x25: 'set int vector', 0x30: 'get DOS version', 0x35: 'get int vector',
          0x3C: 'create', 0x3D: 'open', 0x3E: 'close', 0x3F: 'read', 0x40: 'write', 0x41: 'delete',
          0x42: 'lseek', 0x43: 'attrib', 0x44: 'IOCTL', 0x48: 'alloc', 0x49: 'free', 0x4A: 'resize',
          0x4B: 'exec', 0x4C: 'exit', 0x2A: 'get date', 0x2C: 'get time', 0x0B: 'kbd status',
          0x06: 'direct console', 0x07: 'raw input', 0x08: 'input', 0x01: 'getche', 0x02: 'putch',
          0x19: 'get drive', 0x0E: 'set drive', 0x47: 'getcwd', 0x3B: 'chdir', 0x4E: 'findfirst',
          0x4F: 'findnext', 0x1A: 'set DTA', 0x2F: 'get DTA', 0x36: 'disk free', 0x56: 'rename', 0x57: 'file date',
          0x33: 'ctrl-break', 0x45: 'dup', 0x46: 'dup2', 0x62: 'get PSP', 0x29: 'parse fname'}
VIDEO_FN = {0x00: 'set mode', 0x01: 'cursor shape', 0x02: 'set cursor', 0x03: 'get cursor',
            0x06: 'scroll up', 0x09: 'write char', 0x0E: 'teletype', 0x0F: 'get mode',
            0x10: 'palette/DAC', 0x11: 'char gen', 0x12: 'alt select', 0x1A: 'display combo'}


# ------------------------------------------------------------ disassembly
class Analyzer:
    def __init__(self, img, relocs, entry):
        self.img = img
        self.relocset = set(relocs)
        self.md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_16)
        self.md.detail = True
        self.funcs = {}          # linear -> dict(seg, off, sources)
        self.visited = set()     # linear addresses of decoded instructions
        self.ints = collections.defaultdict(list)    # (int, ah) -> [linear]
        self.ports = collections.defaultdict(list)   # (dir, port, width) -> [linear]
        self.isr = []
        self.vga_modes = set()
        self.indirect = []       # (linear, text) indirect call/jmp sites
        self.cs_writes = set()   # writes to CS: memory (data in code segment)
        self.add_func(entry[0], entry[1], 'entry')

    def add_func(self, seg, off, src):
        lin = (seg * 16 + off) & 0xFFFFF
        if lin >= len(self.img):
            return
        f = self.funcs.setdefault(lin, dict(seg=seg, off=off, src=set(), size=0, ret=None))
        f['src'].add(src)

    def seed_far_calls(self):
        """Relocated far CALL/JMP immediates: opcode at reloc-3, offset at reloc-2."""
        for r in self.relocset:
            if r >= 3 and self.img[r - 3] in (0x9A, 0xEA):
                off = struct.unpack_from('<H', self.img, r - 2)[0]
                seg = struct.unpack_from('<H', self.img, r)[0]
                self.add_func(seg, off, 'farcall' if self.img[r - 3] == 0x9A else 'farjmp')

    def seed_prologues(self, code_end):
        i = 0
        while True:
            i = self.img.find(b'\x55\x8b\xec', i, code_end)
            if i < 0:
                break
            # attribute to the enclosing known code segment (from far-call targets)
            bases = [f['seg'] for f in self.funcs.values() if f['seg'] * 16 <= i < f['seg'] * 16 + 0x10000]
            seg = max(bases) if bases else i >> 4
            self.add_func(seg, i - seg * 16, 'prologue')
            i += 1

    def run(self):
        pending = list(self.funcs)
        done = set()
        while pending:
            lin = pending.pop()
            if lin in done:
                continue
            done.add(lin)
            f = self.funcs[lin]
            before = set(self.funcs)
            self.walk(lin, f)
            pending.extend(set(self.funcs) - before)

    def walk(self, start, f):
        seg_base = f['seg'] * 16
        work, seen = [start], set()
        lo, hi = start, start
        while work:
            a = work.pop()
            regs = {}  # crude constant tracking within a straight-line run
            pushes = []  # known values pushed since the last call (for setvect-style args)
            while 0 <= a < len(self.img) and a not in seen:
                seen.add(a)
                try:
                    ins = next(self.md.disasm(self.img[a:a + 16], a - seg_base, 1))
                except StopIteration:
                    break
                self.visited.add(a)
                hi = max(hi, a + ins.size)
                m, ops = ins.mnemonic, ins.operands
                nxt = a + ins.size
                self.track(ins, regs, a)
                if m == 'push':
                    op = ops[0]
                    if op.type == capstone.x86.X86_OP_IMM:
                        pushes.append(op.imm & 0xFFFF)
                    elif op.type == capstone.x86.X86_OP_REG:
                        v = regs.get(ins.reg_name(op.reg))
                        pushes.append(v if isinstance(v, int) else None)
                    else:
                        pushes.append(None)
                if m in ('mov', 'add', 'sub', 'inc', 'dec', 'and', 'or', 'xor') and ops \
                        and ops[0].type == capstone.x86.X86_OP_MEM and ops[0].mem.segment == capstone.x86.X86_REG_CS:
                    self.cs_writes.add(a)
                if m in ('call', 'lcall', 'jmp', 'ljmp') and ops and ops[0].type != capstone.x86.X86_OP_IMM:
                    if a not in {x[0] for x in self.indirect}: self.indirect.append((a, f"{m} {ins.op_str}"))
                if m in ('call', 'lcall'):
                    # MSC _dos_setvect(vec, handler far*) => push seg, push off, push vec
                    if len(pushes) >= 3 and None not in pushes[-3:] and pushes[-1] in IRQ_VECTORS:
                        vec, off, seg = pushes[-1], pushes[-2], pushes[-3]
                        if seg * 16 + off < len(self.img):
                            self.isr.append((a, 'setvect-style call: vector %02Xh -> handler %04X:%04X' % (vec, seg, off), None))
                            self.add_func(seg, off, 'isr%02X' % vec)
                    pushes = []
                if m in ('ret', 'retf', 'iret', 'hlt'):
                    f['ret'] = f['ret'] or m
                    break
                if m == 'lcall' or m == 'ljmp':
                    if len(ops) == 2 and ops[0].type == capstone.x86.X86_OP_IMM:
                        self.add_func(ops[0].imm, ops[1].imm, 'farcall')
                    if m == 'ljmp':
                        break
                    a = nxt; continue
                if m == 'call':
                    if ops and ops[0].type == capstone.x86.X86_OP_IMM:
                        self.add_func(f['seg'], ops[0].imm & 0xFFFF, 'call')
                    regs.clear()
                    a = nxt; continue
                if m == 'jmp':
                    if ops and ops[0].type == capstone.x86.X86_OP_IMM:
                        a = seg_base + (ops[0].imm & 0xFFFF); regs = {}
                        continue
                    break  # indirect jump (switch table) - stop
                if m.startswith('j') or m.startswith('loop') or m == 'jcxz':
                    if ops and ops[0].type == capstone.x86.X86_OP_IMM:
                        work.append(seg_base + (ops[0].imm & 0xFFFF))
                a = nxt
        f['size'] = max(f['size'], hi - lo)

    def track(self, ins, regs, a):
        m, ops = ins.mnemonic, ins.operands
        R = capstone.x86
        if m == 'mov' and len(ops) == 2 and ops[0].type == R.X86_OP_REG:
            name = ins.reg_name(ops[0].reg)
            if ops[1].type == R.X86_OP_IMM:
                v = ops[1].imm & 0xFFFF
                regs[name] = v
                if name == 'ax':
                    regs['ah'], regs['al'] = v >> 8, v & 0xFF
                elif name == 'ah':
                    regs['ax'] = None
                elif name == 'al':
                    regs['ax'] = None
            elif ops[1].type == R.X86_OP_MEM and ops[1].mem.base == 0 and ops[1].mem.index == 0 \
                    and name == 'dx':
                regs['dx'] = ('var', ops[1].mem.disp & 0xFFFF, 0)   # port base held in a variable
            else:
                regs.pop(name, None)
                if name == 'ax':
                    regs.pop('ah', None); regs.pop('al', None)
        elif m == 'xor' and len(ops) == 2 and ops[0].type == R.X86_OP_REG and ops[1].type == R.X86_OP_REG \
                and ops[0].reg == ops[1].reg:
            name = ins.reg_name(ops[0].reg)
            regs[name] = 0
            if name == 'ax':
                regs['ah'] = regs['al'] = 0
        elif m == 'int':
            n = ops[0].imm & 0xFF
            ah = regs.get('ah')
            self.ints[(n, ah)].append(a)
            if n == 0x21 and ah == 0x25:
                self.isr.append((a, 'INT21/25 set vector', regs.get('al')))
            if n == 0x10 and ah == 0x00:
                self.vga_modes.add(regs.get('al'))
        elif m in ('in', 'out', 'insb', 'insw', 'outsb', 'outsw'):
            port = None
            for op in ops:
                if op.type == R.X86_OP_IMM:
                    port = op.imm & 0xFFFF
                elif op.type == R.X86_OP_REG and ins.reg_name(op.reg) == 'dx':
                    port = regs.get('dx')
            width = 16 if any(op.type == R.X86_OP_REG and ins.reg_name(op.reg) == 'ax' for op in ops) else 8
            self.ports[('IN' if m.startswith('in') else 'OUT', port, width)].append(a)
        elif m in ('add', 'sub', 'inc', 'dec') and ops and ops[0].type == R.X86_OP_REG:
            name = ins.reg_name(ops[0].reg)
            if name == 'dl' and isinstance(regs.get('dx'), tuple):
                name = 'dx'
            if isinstance(regs.get(name), tuple):
                k, var, adj = regs[name]
                if m in ('inc', 'dec'):
                    regs[name] = (k, var, adj + (1 if m == 'inc' else -1))
                elif len(ops) == 2 and ops[1].type == R.X86_OP_IMM:
                    regs[name] = (k, var, adj + (ops[1].imm if m == 'add' else -ops[1].imm))
                else:
                    regs.pop(name, None)
            elif name in regs and regs[name] is not None and m in ('inc', 'dec'):
                regs[name] = (regs[name] + (1 if m == 'inc' else -1)) & 0xFFFF
            elif name in regs and regs[name] is not None and len(ops) == 2 and ops[1].type == R.X86_OP_IMM:
                regs[name] = (regs[name] + (ops[1].imm if m == 'add' else -ops[1].imm)) & 0xFFFF
            else:
                regs.pop(name, None)
        # direct IVT writes: mov es:[0x20..0x27] / [0x24..0x27] with es=0 (timer/keyboard ISR)
        if m == 'mov' and len(ops) == 2 and ops[0].type == R.X86_OP_MEM and ops[0].mem.base == 0 \
                and ops[0].mem.segment not in (R.X86_REG_CS, R.X86_REG_SS) \
                and ops[0].mem.index == 0 and ops[0].mem.disp in (0x20, 0x22, 0x24, 0x26, 0x70, 0x72):
            self.isr.append((a, 'possible IVT write [%#x] (vector %#x)' % (ops[0].mem.disp, ops[0].mem.disp // 4),
                             None))

    def linear_sweep(self, code_end):
        """Linear-disassemble the gaps left by recursive descent and report
        INT/IN/OUT found there (unreached code: indirect calls, ISRs, tables)."""
        out, a = [], 0
        while a < code_end:
            if a in self.visited:
                a += 1; continue
            gap_end = a
            while gap_end < code_end and gap_end not in self.visited:
                gap_end += 1
            if gap_end - a >= 8:      # ignore tiny alignment gaps
                for ins in self.md.disasm(self.img[a:gap_end], a):
                    if ins.mnemonic == 'int' or ins.mnemonic in ('in', 'out'):
                        out.append((ins.address, ins.mnemonic, ins.op_str))
            a = gap_end
        return out


# ------------------------------------------------------------------ report
def analyse_exe(path, d, mz, args):
    print(f'  MZ: hdr={mz["hdr"]:#x} image={mz["size"] - mz["hdr"]:#x} ({mz["size"] - mz["hdr"]} B)'
          f'  CS:IP={mz["cs"]:04X}:{mz["ip"]:04X}  SS:SP={mz["ss"]:04X}:{mz["sp"]:04X}'
          f'  relocs={mz["nrel"]} @ {mz["relofs"]:#x}  minalloc={mz["minalloc"]:#x}')
    if mz['size'] < len(d):
        print(f'  overlay / appended data: {len(d) - mz["size"]} bytes after image')
    packers = detect_packer(d, mz)
    if packers:
        print('  PACKED:', ', '.join(packers), '-> unpack with tools/unpack_exe.py before analysis')
        return
    img = d[mz['hdr']:mz['size']]
    A = Analyzer(img, mz['relocs'], (mz['cs'], mz['ip']))
    A.seed_far_calls()
    for sd in args.seed or []:
        sseg, soff = (int(x, 16) for x in sd.split(':'))
        A.add_func(sseg, soff, 'seed')
    # code ends where DGROUP starts: estimate as lowest SS / data seg used by `mov ax, seg` near entry
    code_end = args.code_end if args.code_end else min(mz['ss'] * 16, len(img))
    A.seed_prologues(code_end)
    A.run()
    segs = collections.Counter(f['seg'] for f in A.funcs.values() if 'prologue' not in f['src'] or len(f['src']) > 1)
    print(f'  code segments (from far-call targets): ' + ', '.join(f'{s:04X}' for s in sorted(segs)))
    print(f'  functions: {len(A.funcs)}  decoded instructions: {len(A.visited)}')

    print('\n  -- software interrupts --')
    for (n, ah), sites in sorted(A.ints.items(), key=lambda x: (x[0][0], -1 if x[0][1] is None else x[0][1])):
        desc = INT_NAMES.get(n, '')
        if n == 0x21 and ah is not None:
            desc += ' / ' + DOS_FN.get(ah, '?')
        if n == 0x10 and ah is not None:
            desc += ' / ' + VIDEO_FN.get(ah, '?')
        ahs = 'AH=??' if ah is None else f'AH={ah:02X}'
        print(f'  INT {n:02X}h {ahs:6} x{len(sites):<3} {desc:34} @ ' + ' '.join(f'{s:05x}' for s in sites[:6]))
    if A.vga_modes:
        print('  video modes set via INT10/00:', ', '.join('??' if m is None else f'{m:02X}h' for m in A.vga_modes))

    print('\n  -- port I/O --')
    by_dev = collections.defaultdict(list)
    for (dr, port, w), sites in A.ports.items():
        by_dev[device(port).split(' ')[0] if port is not None else 'dynamic'].append((dr, port, w, sites))
    for (dr, port, w), sites in sorted(A.ports.items(), key=lambda x: (str(x[0][1]), x[0][0])):
        ps = '  DX?' if port is None else ('  var' if isinstance(port, tuple) else f'{port:04X}h')
        print(f'  {dr:3} {ps} {w:2}-bit x{len(sites):<3} {device(port):38} @ ' + ' '.join(f'{s:05x}' for s in sites[:6]))

    if A.isr:
        print('\n  -- interrupt handler installation --')
        for a, what, al in A.isr:
            print(f'  {a:05x}: {what}' + (f' AL={al:02X}h' if al is not None else ''))

    if A.indirect:
        print(f'\n  -- {len(A.indirect)} indirect call/jmp sites (need jump-table / function-pointer resolution) --')
        for a, t in A.indirect[:args.max_unreached]:
            print(f'  {a:05x}: {t}')
    if A.cs_writes:
        print(f'\n  -- {len(A.cs_writes)} writes to CS: memory (variables or self-modifying code in code segments) --')
        print('  ' + ' '.join(f'{a:05x}' for a in sorted(A.cs_writes)[:40]) + (' ...' if len(A.cs_writes) > 40 else ''))

    un = A.linear_sweep(code_end)
    if un:
        print(f'\n  -- {len(un)} INT/IN/OUT sites not reached by recursive descent (indirect calls?) --')
        for a, m, o in un[:args.max_unreached]:
            print(f'  {a:05x}: {m} {o}')

    if args.csv:
        out = args.csv if len(args.files) == 1 else f'{os.path.splitext(args.csv)[0]}_{os.path.basename(path)}.csv'
        with open(out, 'w') as fh:
            fh.write('linear,seg,off,size,ret,sources\n')
            for lin in sorted(A.funcs):
                f = A.funcs[lin]
                fh.write(f'{lin:05x},{f["seg"]:04X},{f["off"]:04X},{f["size"]},{f["ret"] or ""},'
                         f'{"|".join(sorted(f["src"]))}\n')
        print(f'\n  function table -> {out}')


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('files', nargs='+')
    ap.add_argument('--csv', help='write candidate function entry table')
    ap.add_argument('--seed', action='append', help='extra entry point SEG:OFF (hex), e.g. ISR found manually')
    ap.add_argument("--max-unreached", type=int, default=200)
    ap.add_argument('--code-end', type=lambda x: int(x, 0), help='image offset where code ends (DGROUP start)')
    args = ap.parse_args()
    for path in args.files:
        d = open(path, 'rb').read()
        print(f'== {path}: {len(d)} bytes, entropy {entropy(d):.2f} bits/byte, magic {d[:4].hex()}')
        mz = parse_mz(d)
        if mz:
            analyse_exe(path, d, mz, args)
        else:
            if d.startswith(b'Creative Voice File'):
                print('  Creative Voice File (VOC)')
            blocks = [entropy(d[i:i + 4096]) for i in range(0, len(d), 4096)]
            if blocks:
                print(f'  4K-block entropy min/avg/max: {min(blocks):.2f}/{sum(blocks) / len(blocks):.2f}/{max(blocks):.2f}'
                      + ('  (likely compressed/encrypted)' if min(blocks) > 7.5 else ''))
        print()


if __name__ == '__main__':
    main()
