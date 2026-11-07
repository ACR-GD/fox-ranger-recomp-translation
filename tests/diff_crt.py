#!/usr/bin/env python3
"""diff_crt.py - differential test of the native CRT shims (src/crt_shims.c)
against the original Microsoft C 6 run-time code in FOX_UNP2.EXE (unicorn).

Covers the pure entry points: _fstrlen _fstrcpy _fmemcpy _fmemmove _fmemset (x2)
abs rand _aFlmul _aFldiv _aFlrem _aFlshl _aFlshr sprintf (incl. the game's own
format strings). Compared: AX, DX where defined, SP (incl. `retf 8`), the
preserved registers, and RAM (minus the dead stack and sprintf's internal
string-FILE at DS:2174-217F).

usage: diff_crt.py GAME_DIR FOX_UNP2.EXE LIB functions.csv function_modules.csv [--cases N]
"""
import argparse, ctypes, os, random, struct, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from diff_fn import Cpu, X86, UCREG, L, DGROUP, STACK_SEG, STACK_TOP, RET_FAR, HEAP_LO, HEAP_HI  # noqa: E402
from unicorn.x86_const import UC_X86_REG_SP, UC_X86_REG_CS, UC_X86_REG_IP, UC_X86_REG_EFLAGS  # noqa: E402

CRT = 0x134D
BUF = 0x7000            # scratch buffers (inside the emulated heap range)
RET_DXAX = {'strcpy', 'memcpy', 'memmove', 'memset', 'memset2', 'rand', 'lmul', 'ldiv', 'lrem', 'lshl', 'lshr'}
FUNCS = {'strlen': 0x1D1C, 'strcpy': 0x1CE6, 'memcpy': 0x2470, 'memmove': 0x2020, 'memset': 0x2148,
         'memset2': 0x24CE, 'abs': 0x21F4, 'rand': 0x2214, 'lmul': 0x2386, 'ldiv': 0x22EC, 'lrem': 0x23B8,
         'lshl': 0x2458, 'lshr': 0x2464, 'sprintf': 0x1F98}
GAME_FMTS = [b'%d. %-10lu %-11s    %-6s', b'%d. %-10lu %s', b'STAGE %d']
SYN_FMTS = [b'%5d|%-4x|%04X|%c|%ld|%s%%', b'[%u] %lu %lx', b'%-8s|%8s|%.3s', b'%+d % d %#x %o', b'%*d|%-*d']


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('game'); ap.add_argument('exe'); ap.add_argument('lib')
    ap.add_argument('functions_csv'); ap.add_argument('modules_csv')
    ap.add_argument('--cases', type=int, default=600); ap.add_argument('--seed', type=int, default=6)
    a = ap.parse_args()
    lib = ctypes.CDLL(a.lib)
    lib.t_ram.restype = ctypes.POINTER(ctypes.c_uint8)
    lib.t_cpu.restype = ctypes.POINTER(Cpu)
    lib.mz_load.argtypes = [ctypes.c_char_p, ctypes.c_uint16, ctypes.c_char_p, ctypes.c_void_p]
    lib.t_run.argtypes = [ctypes.c_uint16, ctypes.c_uint16, ctypes.c_int, ctypes.c_uint16, ctypes.c_uint16, ctypes.c_int32]
    lib.t_runtime_init.argtypes = [ctypes.c_uint16, ctypes.c_uint16]
    lib.mem_reset()
    lib.mz_load(a.exe.encode(), L, b' a v', None)
    lib.t_runtime_init(HEAP_LO, HEAP_HI)
    lib.t_stub_clear()
    ram = (ctypes.c_uint8 * 0x110000).from_address(ctypes.cast(lib.t_ram(), ctypes.c_void_p).value)
    base = bytes(ram)
    rng = random.Random(a.seed)
    ds = (L + DGROUP) & 0xFFFF
    stats = {k: [0, 0] for k in FUNCS}

    for case in range(a.cases):
        name = rng.choice(list(FUNCS))
        off = FUNCS[name]
        ram[:] = base
        b0 = BUF * 16
        for k in range(0x2000):
            ram[b0 + k] = rng.choice([0, rng.randrange(1, 256), rng.randrange(0x20, 0x7F)])
        regs = dict(ax=rng.getrandbits(16), bx=rng.getrandbits(16), cx=rng.getrandbits(16), dx=rng.getrandbits(16),
                    si=rng.getrandbits(16), di=rng.getrandbits(16), bp=rng.getrandbits(16), ds=ds, es=rng.getrandbits(16), ss=STACK_SEG)
        args = []
        P = lambda o: [o, BUF]                                  # far pointer into the buffer
        if name == 'strlen':
            o = rng.randrange(0, 0x800); ram[b0 + o + rng.randrange(0, 300)] = 0; args = P(o)
        elif name == 'strcpy':
            s = rng.randrange(0x1000, 0x1800); ram[b0 + s + rng.randrange(0, 200)] = 0
            args = P(rng.randrange(0, 0x800)) + P(s)
        elif name in ('memcpy', 'memmove'):
            d, s = rng.randrange(0, 0x1000), rng.randrange(0, 0x1000)
            if name == 'memmove' and rng.random() < 0.6:
                s = max(0, d + rng.randrange(-40, 40))         # overlapping
            args = P(d) + P(s) + [rng.choice([0, 1, 2, 3, rng.randrange(0, 600)])]
        elif name in ('memset', 'memset2'):
            args = P(rng.randrange(0, 0x1000)) + [rng.getrandbits(16), rng.choice([0, 1, 7, rng.randrange(0, 900)])]
        elif name == 'abs':
            args = [rng.choice([0, 1, 0x7FFF, 0x8000, 0xFFFF, rng.getrandbits(16)])]
        elif name == 'rand':
            v = rng.getrandbits(32)
            struct.pack_into('<I', ram, (L + DGROUP) * 16 + 0x1518, v)
        elif name in ('lmul', 'ldiv', 'lrem'):
            def rl():
                return rng.choice([0, 1, -1, 0x7FFFFFFF, -0x80000000, rng.randrange(-70000, 70000),
                                   rng.randrange(-2**31, 2**31)]) & 0xFFFFFFFF
            x, y = rl(), rl()
            if name != 'lmul':
                while y == 0 or (x == 0x80000000 and y == 0xFFFFFFFF):
                    y = rl()
            args = [x & 0xFFFF, x >> 16, y & 0xFFFF, y >> 16]
        elif name in ('lshl', 'lshr'):
            regs['cx'] = (rng.getrandbits(8) << 8) | rng.choice([0, 1, 5, 15, 16, 17, 31, 32, 33, rng.randrange(256)])
        elif name == 'sprintf':
            fmt = rng.choice(GAME_FMTS + SYN_FMTS)
            fo = 0x1C00
            ram[b0 + fo:b0 + fo + len(fmt) + 1] = fmt + b'\0'
            for so in (0x1D00, 0x1D40, 0x1D80):
                n = rng.randrange(0, 20)
                ram[b0 + so:b0 + so + n + 1] = bytes(rng.randrange(0x20, 0x7F) for _ in range(n)) + b'\0'
            va = []
            spec = fmt.decode()
            i = 0
            while i < len(spec):
                if spec[i] != '%': i += 1; continue
                i += 1
                while spec[i] in '-+ #0': i += 1
                if spec[i] == '*': va.append(rng.randrange(0, 12)); i += 1
                while spec[i].isdigit(): i += 1
                if spec[i] == '.':
                    i += 1
                    while spec[i].isdigit(): i += 1
                lng = False
                while spec[i] in 'hlFN':
                    lng |= spec[i] == 'l'; i += 1
                cv = spec[i]; i += 1
                if cv == '%': continue
                if cv == 's': va += P(rng.choice([0x1D00, 0x1D40, 0x1D80]))
                elif lng: v = rng.getrandbits(32); va += [v & 0xFFFF, v >> 16]
                elif cv == 'c': va.append(rng.randrange(0x20, 0x7F))
                else: va.append(rng.choice([0, 1, 9, 0xFFFF, 0x8000, rng.getrandbits(16)]))
            args = P(0x1E00) + P(fo) + va
        sp = STACK_TOP
        for v in reversed(args):
            sp -= 2; struct.pack_into('<H', ram, STACK_SEG * 16 + sp, v)

        # x86
        x = X86(ram, {})
        uc = x.uc
        usp = sp - 4
        uc.mem_write(STACK_SEG * 16 + usp, struct.pack('<HH', RET_FAR[1], RET_FAR[0]))
        for r, v in regs.items():
            uc.reg_write(UCREG[r], v)
        uc.reg_write(UC_X86_REG_SP, usp)
        uc.reg_write(UC_X86_REG_CS, (L + CRT) & 0xFFFF)
        uc.reg_write(UC_X86_REG_IP, off)
        uc.reg_write(UC_X86_REG_EFLAGS, 0x0202)
        uc.emu_start(((L + CRT) * 16 + off) & 0xFFFFF, RET_FAR[0] * 16 + RET_FAR[1], count=2_000_000)
        # C
        c = lib.t_cpu().contents
        for r, v in regs.items():
            setattr(c, r, v)
        c.sp = sp; c.df = 0
        code = lib.t_run(CRT, off, 1, RET_FAR[0], RET_FAR[1], 1 << 30)

        probs = []
        if code:
            probs.append(f'C trap {code}')
        check = ['ax', 'sp', 'si', 'di', 'bp', 'ds', 'ss']
        if name in RET_DXAX: check.append('dx')
        if name in ('lshl', 'lshr'): check.append('cx')
        for r in check:
            xv, cv = uc.reg_read(UCREG[r]), getattr(c, r)
            if xv != cv: probs.append(f'{r} x86={xv:04x} C={cv:04x}')
        umem = bytearray(uc.mem_read(0, 0xA0000))
        cmem = bytearray(ram[:0xA0000])
        dead = (STACK_SEG * 16, STACK_SEG * 16 + c.sp)
        sf = ((L + DGROUP) * 16 + 0x2174, (L + DGROUP) * 16 + 0x2180)
        for lo, hi in (dead, sf):
            umem[lo:hi] = bytes(hi - lo); cmem[lo:hi] = bytes(hi - lo)
        if umem != cmem:
            d = [i for i in range(0xA0000) if umem[i] != cmem[i]]
            probs.append(f'{len(d)} RAM bytes differ, first {d[0]:05x}: x86 {umem[d[0]]:02x} C {cmem[d[0]]:02x}')
        stats[name][0] += 1
        if probs:
            stats[name][1] += 1
            if stats[name][1] <= 2:
                print(f'FAIL {name} args={[hex(v) for v in args[:8]]} cx={regs["cx"]:04x}: ' + '; '.join(probs[:4]))
    bad = 0
    for k, (n, f) in sorted(stats.items()):
        print(f'  {k:8} {n:4} cases  {f} failures'); bad += f
    print('PASS' if not bad else f'FAILED {bad}')
    sys.exit(1 if bad else 0)


if __name__ == '__main__':
    main()
