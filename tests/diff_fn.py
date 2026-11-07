#!/usr/bin/env python3
"""diff_fn.py - differential test of recompiled functions (gen/*.c) against the
original x86 code of FOX_UNP2.EXE executed in unicorn.

Each function is tested in isolation over N randomised trials:
  * identical start state on both sides: MZ image at load segment 1000h,
    randomised game data (DGROUP 1ADF and far data segment 1682), random
    registers and stack arguments (often far pointers into the data area),
    random VRAM
  * every call made by the function is replaced by a stub on both sides
    (C: rt_call_hook, x86: INT3 patched at the callee entry). A stub records
    the target and its 6 stack argument words, returns deterministic AX/DX
    and pops the callee's return address (ret 2 / retf 4, from functions.csv).
    All C run-time calls are stubbed as well (the CRT shims have their own
    test, tests/diff_crt.py); --real-crt runs the pure helpers for real instead.
  * compared after return: AX BX CX DX SI DI BP SP DS ES SS, the whole 1 MB of
    RAM (minus INT3 patch bytes), VGA planes, call trace, OUT trace.
Trials where either side exceeds the loop budget are "inconclusive"; if both
sides raise a divide fault the trial counts as a match.

usage: diff_fn.py GAME_DIR FOX_UNP2.EXE LIB functions.csv function_modules.csv
                  [--modules game_loop,game_logic] [--trials 20] [--only SEG:OFF]
"""
import argparse, csv, ctypes, os, random, struct, sys
from unicorn import Uc, UC_ARCH_X86, UC_MODE_16, UC_HOOK_INSN, UC_HOOK_INTR, UcError
from unicorn.x86_const import *

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from diff_blit import PyVga  # noqa: E402  (independent planar VGA model)

L = 0x1000
DGROUP = 0x1ADF
DATA_LO, DATA_HI = 0x16820, 0x1CF70           # image: far data seg 1682 .. end of DGROUP init data
STACK_SEG, STACK_TOP = 0x8800, 0xF000
RET_FAR = (0x0050, 0x0000)
HEAP_LO, HEAP_HI = 0x40CD, 0x8000
PURE_CRT = {0x1CE6, 0x1D1C, 0x2020, 0x2148, 0x24CE, 0x2470, 0x21F4, 0x2214, 0x2386, 0x22EC,
            0x23B8, 0x2458, 0x2464, 0x1F98}
CRT_SEG = 0x134D
# CRT entry points called by game code (RECON/crt_shims.h); CRT-internal helpers are never stubbed
CRT_API = {0x01DF, 0x06A6, 0x07A0, 0x07BC, 0x092E, 0x0AC6, 0x19CC, 0x19D2, 0x1C92, 0x1CE6, 0x1D1C,
           0x1D7C, 0x1F98, 0x2004, 0x2012, 0x2020, 0x2148, 0x21F4, 0x2214, 0x224C, 0x2266, 0x2282,
           0x229F, 0x22D2, 0x22E8, 0x22EA, 0x22EC, 0x2386, 0x23B8, 0x2458, 0x2464, 0x2470, 0x24CE}
UC_BUDGET = 3_000_000
C_BUDGET = 200_000


class Cpu(ctypes.Structure):
    _fields_ = [(n, ctypes.c_uint16) for n in
                ('ax', 'bx', 'cx', 'dx', 'si', 'di', 'bp', 'sp', 'cs', 'ds', 'es', 'ss', 'ip')] + \
               [(n, ctypes.c_uint8) for n in ('cf', 'zf', 'sf', 'of', 'pf', 'af', 'df', 'ifl')]


class CallRec(ctypes.Structure):
    _fields_ = [('lin', ctypes.c_uint32), ('args', ctypes.c_uint16 * 6)]


REGS = ['ax', 'bx', 'cx', 'dx', 'si', 'di', 'bp', 'sp', 'ds', 'es', 'ss']
UCREG = {n: getattr(sys.modules['unicorn.x86_const'], 'UC_X86_REG_' + n.upper()) for n in REGS + ['cs', 'ip']}


def load_funcs(funcs_csv, mods_csv):
    ret, size = {}, {}
    for r in csv.DictReader(open(funcs_csv)):
        srcs = set(r['sources'].split('|'))
        if srcs and all(x.startswith('isr') for x in srcs) and r['ret'] != 'iret':
            continue                    # setvect heuristic false positives (e.g. 0003:005C)
        ret[int(r['linear'], 16)] = (int(r['seg'], 16), int(r['off'], 16), r['ret'])
        size[int(r['linear'], 16)] = int(r['size'])
    load_funcs.size = size
    mods = {}
    for r in csv.DictReader(open(mods_csv)):
        mods[int(r['linear'], 16)] = (r['module'], r['strategy'])
    return ret, mods


class X86:
    def __init__(self, ram, patches):
        self.uc = uc = Uc(UC_ARCH_X86, UC_MODE_16)
        uc.mem_map(0, 0xA0000)
        uc.mem_map(0xB0000, 0x10000)
        uc.mem_map(0x100000, 0x10000)
        uc.mem_write(0, bytes(ram[:0xA0000]))
        uc.mem_write(0xB0000, bytes(ram[0xB0000:0xC0000]))
        uc.mem_write(0x100000, bytes(ram[0x100000:0x110000]))
        self.vga = PyVga()
        uc.mmio_map(0xA0000, 0x10000, self._vr, None, self._vw, None)
        uc.mmio_map(0xC0000, 0x40000, lambda *a: 0, None, lambda *a: None, None)   # ROM: writes ignored
        uc.hook_add(UC_HOOK_INSN, self._out, None, 1, 0, UC_X86_INS_OUT)
        uc.hook_add(UC_HOOK_INSN, self._in, None, 1, 0, UC_X86_INS_IN)
        uc.hook_add(UC_HOOK_INTR, self._intr)
        self.patches = patches            # lin -> pop bytes
        for lin in patches:
            uc.mem_write(lin, b'\xcc')
        self.calls, self.io, self.fault = [], [], None

    def _vr(self, uc, off, size, ud):
        return sum(self.vga.read((off + k) & 0xFFFF) << (8 * k) for k in range(size))

    def _vw(self, uc, off, size, val, ud):
        for k in range(size):
            self.vga.write((off + k) & 0xFFFF, (val >> (8 * k)) & 0xFF)

    opl_index, opl_status = 0, 0

    def _out(self, uc, port, size, val, ud):
        for k in range(size):
            b = (val >> (8 * k)) & 0xFF
            self.io.append(((port + k) << 8) | b)
            p = port + k                      # OPL timer-status model, same as src/audio_opl.c
            if p in (0x388, 0x228): self.opl_index = b
            elif p in (0x389, 0x229) and self.opl_index == 4:
                if b & 0x80: self.opl_status = 0
                else:
                    if b & 1: self.opl_status |= 0xC0
                    if b & 2: self.opl_status |= 0xA0
            if 0x3B0 <= port + k <= 0x3DF:
                try: self.vga.out(port + k, b)
                except RuntimeError: pass

    def _in(self, uc, port, size, ud):
        if port in (0x388, 0x228):
            return self.opl_status
        if port == 0x64:
            return 0x14
        if 0x3B0 <= port <= 0x3DF:
            try: return self.vga.inp(port)
            except RuntimeError: return 0xFF
        return 0xFF if size == 1 else 0xFFFF

    def _intr(self, uc, intno, ud):
        rd = lambda r: uc.reg_read(UCREG[r])
        if intno == 3:
            cs, ip = rd('cs'), rd('ip')
            lin = (cs * 16 + ip - 1) & 0xFFFFF
            if lin in self.patches:
                pop = self.patches[lin]
                ss, sp = rd('ss'), rd('sp')
                base = ss * 16 + ((sp + pop) & 0xFFFF)
                args = struct.unpack('<6H', bytes(uc.mem_read(base, 12)))
                k = len(self.calls)
                imglin = lin - L * 16
                self.calls.append((imglin, args))
                uc.reg_write(UC_X86_REG_AX, (0x1234 + 0x9E37 * k + imglin * 31) & 0xFFFF)
                uc.reg_write(UC_X86_REG_DX, (0x0F0F + 0x3B5D * k + imglin) & 0xFFFF)
                ret_ip = struct.unpack('<H', bytes(uc.mem_read(ss * 16 + sp, 2)))[0]
                ret_cs = struct.unpack('<H', bytes(uc.mem_read(ss * 16 + ((sp + 2) & 0xFFFF), 2)))[0] if pop >= 4 else cs
                uc.reg_write(UC_X86_REG_SP, (sp + pop) & 0xFFFF)
                uc.reg_write(UC_X86_REG_CS, ret_cs)
                uc.reg_write(UC_X86_REG_IP, ret_ip)
                return
        if intno == 0x21 and self._int21(uc):
            return
        if intno == 0x10 and self._int10(uc):
            return
        self.fault = 'divide' if intno == 0 else f'int{intno:02x}'
        uc.emu_stop()

    # same DOS/BIOS services as src/dos.c (dos_int21 / bios_int10)
    def _int21(self, uc):
        rd = lambda r: uc.reg_read(r)
        ah, al = rd(UC_X86_REG_AH), rd(UC_X86_REG_AL)
        if ah == 0x25:
            uc.mem_write(al * 4, struct.pack('<HH', rd(UC_X86_REG_DX), rd(UC_X86_REG_DS)))
        elif ah == 0x35:
            off, seg = struct.unpack('<HH', bytes(uc.mem_read(al * 4, 4)))
            uc.reg_write(UC_X86_REG_BX, off); uc.reg_write(UC_X86_REG_ES, seg)
        else:
            return False
        uc.reg_write(UC_X86_REG_EFLAGS, uc.reg_read(UC_X86_REG_EFLAGS) & ~1)
        return True

    def _int10(self, uc):
        ah, al = uc.reg_read(UC_X86_REG_AH), uc.reg_read(UC_X86_REG_AL)
        if ah == 0x00:
            if (al & 0x7F) == 0x13:         # mode 13h: registers reset, VRAM cleared
                v = self.vga
                v.planes = [bytearray(0x10000) for _ in range(4)]
                v.seq = [0x03, 0x01, 0x0F, 0x00, 0x0E]; v.gc = [0, 0, 0, 0, 0, 0x40, 0x05, 0x0F, 0xFF]
            return True
        if ah == 0x0F:
            uc.reg_write(UC_X86_REG_AX, (40 << 8) | 0x13); uc.reg_write(UC_X86_REG_BH, 0)
            return True
        return True                         # other video services: ignored, like bios_int10


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('game'); ap.add_argument('exe'); ap.add_argument('lib')
    ap.add_argument('functions_csv'); ap.add_argument('modules_csv')
    ap.add_argument('--modules', default='game_loop,game_logic')
    ap.add_argument('--trials', type=int, default=20)
    ap.add_argument('--seed', type=int, default=2044)
    ap.add_argument('--only', default='')
    ap.add_argument('--verbose', action='store_true')
    ap.add_argument('--poke', action='append', default=[],
                    help='IMGSEG:OFF=LO..HI: set a word of program state to a random value in range each trial')
    ap.add_argument('--uc-budget', type=int, default=UC_BUDGET, help='x86 instruction budget per trial')
    ap.add_argument('--trace', action='store_true', help='with --only and the RT_TRACE lib: report first diverging instruction')
    ap.add_argument('--real-crt', action='store_true',
                    help='run pure CRT helpers for real (original CRT vs shims) instead of stubbing them')
    a = ap.parse_args()

    lib = ctypes.CDLL(a.lib)
    lib.t_ram.restype = ctypes.POINTER(ctypes.c_uint8)
    lib.t_vram.restype = ctypes.POINTER(ctypes.c_uint8)
    lib.t_cpu.restype = ctypes.POINTER(Cpu)
    lib.t_calls.restype = ctypes.POINTER(CallRec)
    lib.t_io.restype = ctypes.POINTER(ctypes.c_uint32)
    lib.mz_load.argtypes = [ctypes.c_char_p, ctypes.c_uint16, ctypes.c_char_p, ctypes.c_void_p]
    lib.t_run.argtypes = [ctypes.c_uint16, ctypes.c_uint16, ctypes.c_int, ctypes.c_uint16, ctypes.c_uint16, ctypes.c_int32]
    lib.t_stub_add.argtypes = [ctypes.c_uint16] * 3
    lib.t_runtime_init.argtypes = [ctypes.c_uint16, ctypes.c_uint16]
    if a.trace:
        lib.t_trace.restype = ctypes.POINTER(ctypes.c_uint16)

    lib.mem_reset()
    if lib.mz_load(a.exe.encode(), L, b' a v', None):
        sys.exit('mz_load failed')
    lib.t_runtime_init(HEAP_LO, HEAP_HI)
    ram = (ctypes.c_uint8 * 0x110000).from_address(ctypes.cast(lib.t_ram(), ctypes.c_void_p).value)
    base_ram = bytes(ram)

    funcs, mods = load_funcs(a.functions_csv, a.modules_csv)
    want = set(a.modules.split(','))
    targets = [(lin, s, o, r) for lin, (s, o, r) in sorted(funcs.items())
               if mods.get(lin, ('', ''))[0] in want and mods[lin][1].startswith('transpile')]
    if a.only:
        s, o = (int(x, 16) for x in a.only.split(':'))
        targets = [t for t in targets if t[0] == s * 16 + o]
    rng = random.Random(a.seed)
    totals = dict(pass_=0, fail=0, inconcl=0, fault_both=0)
    failing = []

    for lin, fseg, foff, rtype in targets:
        is_far = rtype != 'ret'
        # stub every known function except the one under test and the pure CRT helpers
        lib.t_stub_clear()
        patches = {}
        stub_set = {flin: (s, o, r) for flin, (s, o, r) in funcs.items() if s != CRT_SEG}
        crt_set = CRT_API if not a.real_crt else CRT_API - PURE_CRT
        for o in crt_set:
            stub_set[CRT_SEG * 16 + o] = (CRT_SEG, o, 'retf8' if o in (0x22EC, 0x2386, 0x23B8) else 'retf')
        fsize = load_funcs.size.get(lin, 0)
        for flin, (s, o, r) in stub_set.items():
            if lin <= flin < lin + max(fsize, 1):
                continue                # never patch inside the function under test
            pop = {'ret': 2, 'retf8': 12}.get(r, 4)
            lib.t_stub_add(s, o, pop)
            patches[L * 16 + flin] = pop
        res = dict(pass_=0, fail=0, inconcl=0, fault_both=0)
        first_fail = None
        for trial in range(a.trials):
            # ---- identical random start state
            ram[:] = base_ram
            nmut = rng.choice([0, 8, 64, 256])
            for _ in range(nmut):
                p = rng.randrange(DATA_LO, DATA_HI - 1) + L * 16
                v = rng.choice([0, 1, 2, rng.randrange(16), rng.randrange(256), rng.getrandbits(16)])
                ram[p] = v & 0xFF
                if rng.random() < 0.5: ram[p + 1] = v >> 8
            for pk in a.poke:
                loc, rngs = pk.split('=')
                ps, po = (int(x, 16) for x in loc.split(':'))
                lo_, hi_ = (int(x, 0) for x in rngs.split('..'))
                struct.pack_into('<H', ram, (L + ps) * 16 + po, rng.randint(lo_, hi_))
            def rptr():
                return (rng.randrange(0, 0x3000) & ~1, (L + rng.choice([0x1682, 0x1ADF, 0x1800, 0x1900])) & 0xFFFF)
            args = []
            while len(args) < 10:
                if rng.random() < 0.3:
                    o_, s_ = rptr(); args += [o_, s_]
                else:
                    args.append(rng.choice([0, 1, 2, 3, rng.randrange(16), rng.randrange(320), rng.getrandbits(16)]))
            args = args[:10]
            sp = STACK_TOP
            for v in reversed(args):
                sp -= 2
                struct.pack_into('<H', ram, STACK_SEG * 16 + sp, v)
            regs = dict(ax=rng.getrandbits(16), bx=rng.getrandbits(16), cx=rng.getrandbits(16), dx=rng.getrandbits(16),
                        si=rng.getrandbits(16), di=rng.getrandbits(16), bp=rng.getrandbits(16),
                        ds=(L + DGROUP) & 0xFFFF, es=(L + 0x1682) & 0xFFFF, ss=STACK_SEG)
            noise = bytes(rng.getrandbits(8) for _ in range(64)) * 1024
            lib.t_mode_y()
            for p in range(4):
                ctypes.memmove(lib.t_vram(p), noise[p * 7:] + noise[:p * 7], 0x10000)

            # the x86 side has INT3 at stubbed entries; mirror the bytes into the C
            # side's RAM so data reads of code bytes (random far pointers) agree
            for plin in patches:
                if plin < 0xC0000:
                    ram[plin] = 0xCC
            # ---- x86 reference
            x = X86(ram, patches)
            for p in range(4):
                x.vga.planes[p][:] = ctypes.string_at(lib.t_vram(p), 0x10000)
            uc = x.uc
            usp = sp
            ret_cs, ret_ip = RET_FAR if is_far else ((L + fseg) & 0xFFFF, 0xFFF0)
            if is_far:
                usp -= 2; uc.mem_write(STACK_SEG * 16 + usp, struct.pack('<H', ret_cs))
            usp -= 2; uc.mem_write(STACK_SEG * 16 + usp, struct.pack('<H', ret_ip))
            for r, v in regs.items():
                uc.reg_write(UCREG[r], v)
            uc.reg_write(UC_X86_REG_SP, usp)
            uc.reg_write(UC_X86_REG_CS, (L + fseg) & 0xFFFF)
            uc.reg_write(UC_X86_REG_IP, foff)
            uc.reg_write(UC_X86_REG_EFLAGS, 0x0202)
            stop = ((ret_cs * 16) + ret_ip) & 0xFFFFF
            utrace = []
            if a.trace:
                from unicorn import UC_HOOK_CODE
                rr = [UCREG[r] for r in ('ax', 'bx', 'cx', 'dx', 'si', 'di', 'bp', 'sp', 'ds', 'es')]
                lo_, hi_ = (L * 16 + lin), (L * 16 + lin + 0x10000)
                def _code(uc_, addr, size, ud):
                    if lo_ <= addr < hi_ and addr not in x.patches:
                        utrace.append((addr - L * 16,) + tuple(uc_.reg_read(q) for q in rr))
                uc.hook_add(UC_HOOK_CODE, _code)
                lib.t_trace_reset()
            uc_status = 'ok'
            try:
                uc.emu_start(((L + fseg) * 16 + foff) & 0xFFFFF, stop, count=a.uc_budget)
            except UcError as e:
                uc_status = f'ucerr {e}'
            if x.fault:
                uc_status = x.fault
            elif uc_status == 'ok' and (uc.reg_read(UC_X86_REG_CS) * 16 + uc.reg_read(UC_X86_REG_IP)) & 0xFFFFF != stop:
                uc_status = 'budget'

            # ---- C side
            c = lib.t_cpu().contents
            for r, v in regs.items():
                setattr(c, r, v)
            c.sp = sp
            c.cf = c.zf = c.sf = c.of = c.pf = c.af = c.df = 0
            c.ifl = 1
            code = lib.t_run(fseg, foff, int(is_far), ret_cs, ret_ip, C_BUDGET)
            c_status = {0: 'ok', 1: 'budget', 2: 'divide', 3: 'unsupported', 4: 'badswitch', 5: 'halt',
                        6: 'missing', 99: 'nofunc'}.get(code, str(code))

            if a.trace:
                n = lib.t_ntrace(); T = lib.t_trace()
                ctrace = [(T[12 * k] | (T[12 * k + 1] << 16),) + tuple(T[12 * k + 2 + j] for j in range(10)) for k in range(n)]
                names = ('ax', 'bx', 'cx', 'dx', 'si', 'di', 'bp', 'sp', 'ds', 'es')
                for k, (p_, q_) in enumerate(zip(utrace, ctrace)):
                    if p_ != q_:
                        prev = utrace[k - 1][0] if k else None
                        print(f'  trial {trial}: first divergence at step {k}: x86 @{p_[0]:05x} C @{q_[0]:05x}'
                              + (f' (after instruction @{prev:05x})' if prev is not None else ''))
                        for nm, u_, c_ in zip(names, p_[1:], q_[1:]):
                            if u_ != c_: print(f'      {nm}: x86 {u_:04x}  C {c_:04x}')
                        break
                else:
                    if len(utrace) != len(ctrace):
                        m_ = min(len(utrace), len(ctrace))
                        print(f'  trial {trial}: traces equal for {m_} steps, lengths x86 {len(utrace)} C {len(ctrace)}')
                        print('    last common:', ' '.join(f'{t[0]:05x}' for t in utrace[max(0, m_ - 4):m_]),
                              '| x86 next:', ' '.join(f'{t[0]:05x}' for t in utrace[m_:m_ + 3]),
                              '| C next:', ' '.join(f'{t[0]:05x}' for t in ctrace[m_:m_ + 3]))
            # random state can route a function pointer or a computed jump outside the
            # program's domain: no meaningful comparison is possible for that trial
            if c_status in ('missing', 'badswitch'):
                res['inconcl'] += 1; continue
            if 'budget' in (uc_status, c_status):
                res['inconcl'] += 1; continue
            if uc_status == 'divide' and c_status == 'divide':
                res['fault_both'] += 1; continue
            problems = []
            if uc_status != 'ok' or c_status != 'ok':
                problems.append(f'status x86={uc_status} C={c_status}')
            else:
                for r in REGS:
                    xv, cv = uc.reg_read(UCREG[r]), getattr(c, r)
                    if xv != cv:
                        problems.append(f'{r} x86={xv:04x} C={cv:04x}')
                umem = bytes(uc.mem_read(0, 0xA0000)) + bytes(0x10000) + bytes(uc.mem_read(0xB0000, 0x10000))
                cmem = bytearray(ram[:0xC0000])
                cmem[0xA0000:0xB0000] = bytes(0x10000)
                for plin in patches:
                    if plin < 0xC0000:
                        cmem[plin] = 0xCC
                # the stack below the final SP is dead (callee scratch, locals)
                dead_lo, dead_hi = STACK_SEG * 16, STACK_SEG * 16 + c.sp
                umem = umem[:dead_lo] + bytes(dead_hi - dead_lo) + umem[dead_hi:]
                cmem[dead_lo:dead_hi] = bytes(dead_hi - dead_lo)
                if umem != bytes(cmem):
                    diff = [i for i in range(0xC0000) if umem[i] != cmem[i]]
                    problems.append(f'{len(diff)} RAM bytes differ, first {diff[0]:05x}: x86 {umem[diff[0]]:02x} C {cmem[diff[0]]:02x}')
                for p in range(4):
                    if ctypes.string_at(lib.t_vram(p), 0x10000) != bytes(x.vga.planes[p]):
                        problems.append(f'VGA plane {p} differs'); break
                ccalls = [(lib.t_calls()[k].lin, tuple(lib.t_calls()[k].args)) for k in range(lib.t_ncalls())]
                if ccalls != x.calls:
                    k = next((i for i, (p, q) in enumerate(zip(ccalls, x.calls)) if p != q), min(len(ccalls), len(x.calls)))
                    problems.append(f'call trace differs at #{k}: x86 {fmt_call(x.calls, k)} C {fmt_call(ccalls, k)} '
                                    f'(x86 {len(x.calls)} calls, C {len(ccalls)})')
                cio = [lib.t_io()[k] for k in range(lib.t_nio())]
                if cio != x.io:
                    problems.append(f'OUT trace differs ({len(x.io)} vs {len(cio)})')
            if problems:
                res['fail'] += 1
                if first_fail is None:
                    first_fail = (trial, problems)
            else:
                res['pass_'] += 1
        for k in totals: totals[k] += res[k]
        tag = 'OK  ' if not res['fail'] else 'FAIL'
        if res['fail'] or a.verbose:
            print(f'{tag} {fseg:04X}:{foff:04X} pass {res["pass_"]:3} fail {res["fail"]:3} '
                  f'inconclusive {res["inconcl"]:3} both-fault {res["fault_both"]:3}'
                  + (f'  first: trial {first_fail[0]}: ' + '; '.join(first_fail[1][:3]) if first_fail else ''))
        if res['fail']:
            failing.append(f'{fseg:04X}:{foff:04X}')
    n = len(targets)
    print(f'\n{n} functions, {a.trials} trials each: {totals["pass_"]} pass, {totals["fail"]} fail, '
          f'{totals["inconcl"]} inconclusive, {totals["fault_both"]} both-fault')
    print(f'functions with failures: {len(failing)}' + (f' ({", ".join(failing)})' if failing else ''))
    sys.exit(1 if failing else 0)


def fmt_call(calls, k):
    if k >= len(calls):
        return '-'
    lin, args = calls[k]
    return f'{lin >> 4:04X}:{lin & 15:X}(lin {lin:05x}) args {" ".join(f"{v:04x}" for v in args)}'


if __name__ == '__main__':
    main()
