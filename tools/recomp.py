#!/usr/bin/env python3
"""recomp.py - static recompiler: 16-bit x86 (FOX_UNP2.EXE) -> C99 on the
src/ runtime (Cpu struct, emulated memory, far-call registry).

usage: recomp.py EXE function_modules.csv OUTDIR --modules game_loop,game_logic [...]
       [--extra SEG:OFF,...]

For every selected function:
  * recursive-descent control-flow discovery from the entry point (calls are
    not followed; data such as inline switch tables is never decoded),
  * one C function  void f_SSSS_OOOO(Cpu *c)  with a label per jump target,
  * inline `jmp cs:[bx+T]` switch tables -> C `switch` (bounds from the
    preceding `cmp ax,N / jbe` guard, entries read from the image),
  * relocated immediates (segment constants) -> SEG(x) so they follow the
    load address; call targets are image-relative seg:off,
  * flags in a local struct (synchronised with Cpu at calls/returns),
  * calls through rt_call_near/rt_call_far (test hooks, natives, registry).
Writes OUTDIR/recomp_<module>.c, recomp_gen.h, recomp_register.c and
recomp_report.txt (per-function statistics and warnings).
"""
import argparse, collections, csv, os, struct, sys
import capstone
from capstone import x86

HDR = 0x25E0
R = x86

JCC = {
    'jo': 'F.of', 'jno': '!F.of', 'jb': 'F.cf', 'jae': '!F.cf', 'je': 'F.zf', 'jne': '!F.zf',
    'jbe': 'F.cf || F.zf', 'ja': '!F.cf && !F.zf', 'js': 'F.sf', 'jns': '!F.sf',
    'jp': 'F.pf', 'jnp': '!F.pf', 'jl': 'F.sf != F.of', 'jge': 'F.sf == F.of',
    'jle': 'F.zf || F.sf != F.of', 'jg': '!F.zf && F.sf == F.of',
}
GPR16 = {'ax', 'bx', 'cx', 'dx', 'si', 'di', 'bp', 'sp'}
GPR8 = {'al', 'ah', 'bl', 'bh', 'cl', 'ch', 'dl', 'dh'}
SREG = {'es', 'ds', 'ss', 'cs'}
SEG_PREFIX = {0x26: 'es', 0x2E: 'cs', 0x36: 'ss', 0x3E: 'ds'}

# computed jumps (`jmp reg`) with known target sets, verified by hand:
#   1258:0031  jmp bx, bx = 4Ch - 2*[DS:2496]: Duff's-device multiply into the
#              run of eleven 2-byte `add ax, cx` at 0034..0048 (+ fall-out 004A)
COMPUTED_JMP = {
    (0x1258, 0x0031): ('bx', list(range(0x0034, 0x004C, 2))),
}


class Unsupported(Exception):
    pass


class Image:
    def __init__(self, path):
        f = open(path, 'rb').read()
        hdr = struct.unpack_from('<H', f, 8)[0] * 16
        nrel, relofs = struct.unpack_from('<HH', f, 6)[0], struct.unpack_from('<H', f, 0x18)[0]
        self.img = f[hdr:]
        self.relocs = set()
        for i in range(nrel):
            o, s = struct.unpack_from('<HH', f, relofs + 4 * i)
            self.relocs.add(s * 16 + o)
        self.md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_16)
        self.md.detail = True

    def decode(self, seg, off):
        lin = (seg * 16 + off) & 0xFFFFF
        try:
            return next(self.md.disasm(self.img[lin:lin + 16], off, 1))
        except StopIteration:
            return None

    def has_reloc(self, seg, ins):
        lin = seg * 16 + ins.address
        return any(lin + k in self.relocs for k in range(ins.size))

    def word(self, lin):
        return struct.unpack_from('<H', self.img, lin)[0]


class Func:
    def __init__(self, seg, off, module, name=''):
        self.seg, self.off, self.module, self.name = seg, off, module, name
        self.sym = f'f_{seg:04X}_{off:04X}'
        self.lin = seg * 16 + off


class Gen:
    def __init__(self, image, funcs):
        self.I = image
        self.funcs = funcs
        self.by_lin = {f.lin: f for f in funcs}
        self.warn = collections.defaultdict(list)
        self.stats = {}

    # ------------------------------------------------------------ CFG
    def switch_table(self, f, ins, insns):
        op = ins.operands[0]
        table = op.mem.disp & 0xFFFF
        # bound: nearest preceding `cmp <reg>, imm` within 16 bytes
        n = None
        for a in sorted((a for a in insns if ins.address - 16 <= a < ins.address), reverse=True):
            p = insns[a]
            if p.mnemonic == 'cmp' and len(p.operands) == 2 and p.operands[1].type == R.X86_OP_IMM:
                n = (p.operands[1].imm & 0xFFFF) + 1
                break
        if n is None:
            # fall back to probing preceding bytes (guard may precede in address order only)
            for back in range(3, 20):
                p = self.I.decode(f.seg, ins.address - back)
                if p and p.mnemonic == 'cmp' and p.address + p.size <= ins.address and \
                        len(p.operands) == 2 and p.operands[1].type == R.X86_OP_IMM:
                    n = (p.operands[1].imm & 0xFFFF) + 1
                    break
        if n is None or n > 256:
            raise Unsupported(f'switch at {f.seg:04X}:{ins.address:04X}: no bound')
        base = f.seg * 16 + table
        return [self.I.word(base + 2 * k) for k in range(n)]

    def discover(self, f):
        insns, work, switches = {}, [f.off], {}
        while work:
            a = work.pop()
            while a not in insns:
                ins = self.I.decode(f.seg, a)
                if ins is None:
                    raise Unsupported(f'undecodable byte at {f.seg:04X}:{a:04X}')
                insns[a] = ins
                m = mnem(ins)
                nxt = (a + ins.size) & 0xFFFF
                if m in ('ret', 'retf', 'iret', 'hlt'):
                    break
                if m == 'ljmp':
                    raise Unsupported(f'far jmp at {f.seg:04X}:{a:04X}')
                if m == 'jmp':
                    op = ins.operands[0]
                    if op.type == R.X86_OP_IMM:
                        a = op.imm & 0xFFFF
                        continue
                    if op.type == R.X86_OP_MEM and op.mem.segment == R.X86_REG_CS and \
                            ins.reg_name(op.mem.base) == 'bx' and op.mem.index == 0:
                        tgts = self.switch_table(f, ins, insns)
                        switches[a] = tgts
                        work.extend(tgts)
                        break
                    if (f.seg, a) in COMPUTED_JMP:
                        tgts = COMPUTED_JMP[(f.seg, a)][1]
                        switches[a] = tgts
                        work.extend(tgts)
                        break
                    raise Unsupported(f'indirect jmp at {f.seg:04X}:{a:04X}: {ins.op_str}')
                if m in JCC or m in ('jcxz', 'loop', 'loope', 'loopne'):
                    work.append(ins.operands[0].imm & 0xFFFF)
                a = nxt
        # overlap check
        addrs = sorted(insns)
        for x, y in zip(addrs, addrs[1:]):
            if x + insns[x].size > y:
                raise Unsupported(f'overlapping instructions at {f.seg:04X}:{x:04X}/{y:04X}')
        return insns, switches

    # ------------------------------------------------------------ operands
    def reg(self, f, name, write=False):
        if name in GPR16 or name in GPR8 or name in ('es', 'ds', 'ss'):
            return f'c->{name}'
        if name == 'cs':
            if write:
                raise Unsupported('write to CS')
            return f'SEG(0x{f.seg:04X})'
        raise Unsupported(f'register {name}')

    def seg_of(self, f, ins, op):
        if op.mem.segment != 0:
            return self.reg(f, ins.reg_name(op.mem.segment))
        base = ins.reg_name(op.mem.base) if op.mem.base else None
        return 'c->ss' if base == 'bp' else 'c->ds'

    def ea(self, ins, op):
        parts = []
        if op.mem.base:
            parts.append(f'c->{ins.reg_name(op.mem.base)}')
        if op.mem.index:
            parts.append(f'c->{ins.reg_name(op.mem.index)}')
        disp = op.mem.disp & 0xFFFF
        if disp or not parts:
            parts.append(f'0x{disp:X}')
        return f'(uint16_t)({" + ".join(parts)})'

    def imm(self, f, ins, op, size):
        v = op.imm & (0xFF if size == 1 else 0xFFFF)
        if size == 2 and self.I.has_reloc(f.seg, ins):
            return f'SEG(0x{v:04X})'
        return f'0x{v:X}'

    def rd(self, f, ins, op, pre, size=None):
        """C expression reading op; may append temp declarations to pre."""
        size = size or op.size
        if op.type == R.X86_OP_REG:
            return self.reg(f, ins.reg_name(op.reg))
        if op.type == R.X86_OP_IMM:
            return self.imm(f, ins, op, size)
        s, o = self.memref(f, ins, op, pre)
        return f'rd{8 * size}({s}, {o})'

    def memref(self, f, ins, op, pre):
        if not any(p.startswith('const uint16_t s_') for p in pre):
            pre.append(f'const uint16_t s_ = {self.seg_of(f, ins, op)}, o_ = {self.ea(ins, op)};')
        return 's_', 'o_'

    def wr(self, f, ins, op, val, pre, size=None):
        size = size or op.size
        if op.type == R.X86_OP_REG:
            name = ins.reg_name(op.reg)
            cast = 'uint8_t' if name in GPR8 else 'uint16_t'
            return f'{self.reg(f, name, write=True)} = ({cast})({val});'
        s, o = self.memref(f, ins, op, pre)
        return f'wr{8 * size}({s}, {o}, (uint{8 * size}_t)({val}));'

    # ------------------------------------------------------------ emit
    def emit_insn(self, f, ins, switches, labels):
        m = mnem(ins)
        a = ins.address
        lin = f.seg * 16 + a
        ops = ins.operands
        nxt = (a + ins.size) & 0xFFFF
        pre, body = [], []
        B = body.append

        def goto(t):
            t &= 0xFFFF
            back = f'RT_BACKEDGE(c, 0x{f.seg * 16 + a:05X}); ' if t <= a else ''
            return f'{{ {back}goto L_{t:04X}; }}'

        sz = ops[0].size if ops else 2
        bits = 8 * sz

        if m == 'mov':
            B(self.wr(f, ins, ops[0], self.rd(f, ins, ops[1], pre, ops[0].size), pre))
        elif m in ('add', 'adc', 'sub', 'sbb', 'cmp', 'and', 'or', 'xor', 'test'):
            x = self.rd(f, ins, ops[0], pre)
            y = self.rd(f, ins, ops[1], pre, sz)
            fn = {'add': f'add{bits}(&F, {x}, {y}, 0)', 'adc': f'add{bits}(&F, {x}, {y}, F.cf)',
                  'sub': f'sub{bits}(&F, {x}, {y}, 0)', 'sbb': f'sub{bits}(&F, {x}, {y}, F.cf)',
                  'cmp': f'sub{bits}(&F, {x}, {y}, 0)', 'and': f'logic{bits}(&F, (uint{bits}_t)({x} & {y}))',
                  'or': f'logic{bits}(&F, (uint{bits}_t)({x} | {y}))', 'xor': f'logic{bits}(&F, (uint{bits}_t)({x} ^ {y}))',
                  'test': f'logic{bits}(&F, (uint{bits}_t)({x} & {y}))'}[m]
            if m in ('cmp', 'test'):
                B(f'(void){fn};')
            else:
                B(self.wr(f, ins, ops[0], fn, pre))
        elif m in ('inc', 'dec', 'neg'):
            x = self.rd(f, ins, ops[0], pre)
            B(self.wr(f, ins, ops[0], f'{m}{bits}(&F, {x})', pre))
        elif m == 'not':
            x = self.rd(f, ins, ops[0], pre)
            B(self.wr(f, ins, ops[0], f'~{x}', pre))
        elif m == 'lea':
            B(self.wr(f, ins, ops[0], self.ea(ins, ops[1]), pre))
        elif m in ('les', 'lds'):
            s, o = self.memref(f, ins, ops[1], pre)
            sreg = 'c->es' if m == 'les' else 'c->ds'
            B(f'{{ uint16_t v_ = rd16({s}, {o}), g_ = rd16({s}, (uint16_t)({o} + 2));')
            B(f'  {self.reg(f, ins.reg_name(ops[0].reg), True)} = v_; {sreg} = g_; }}')
        elif m == 'xchg':
            x = self.rd(f, ins, ops[0], pre); y = self.rd(f, ins, ops[1], pre)
            B(f'{{ uint{bits}_t t_ = {x};')
            B('  ' + self.wr(f, ins, ops[0], y, pre))
            B('  ' + self.wr(f, ins, ops[1], 't_', pre) + ' }')
        elif m == 'push':
            v = self.rd(f, ins, ops[0], pre, 2)
            if ops[0].type == R.X86_OP_REG and ins.reg_name(ops[0].reg) == 'sp':
                v = 'c->sp'   # 286+: pushes the value before decrement
            B(f'push16(c, (uint16_t)({v}));')
        elif m == 'pop':
            B(f'{{ uint16_t v_ = pop16(c); ' + self.wr(f, ins, ops[0], 'v_', pre) + ' }')
        elif m == 'pushf':
            B('push16(c, rt_flags_word(c, &F));')
        elif m == 'popf':
            B('rt_flags_set(c, &F, pop16(c));')
        elif m == 'sahf':
            B('rt_flags_set(c, &F, (uint16_t)((rt_flags_word(c, &F) & 0xFF00) | c->ah));')
        elif m == 'lahf':
            B('c->ah = (uint8_t)rt_flags_word(c, &F);')
        elif m in ('cbw', 'cwd', 'cwde', 'cdq'):
            if ins.bytes[0] == 0x98:
                B('c->ax = (uint16_t)(int16_t)(int8_t)c->al;')
            else:
                B('c->dx = (c->ax & 0x8000) ? 0xFFFF : 0;')
        elif m in ('mul', 'imul', 'div', 'idiv'):
            if m == 'imul' and len(ops) == 3:
                B(self.wr(f, ins, ops[0], f'imul16_3(&F, {self.rd(f, ins, ops[1], pre)}, {self.rd(f, ins, ops[2], pre, 2)})', pre))
            elif m == 'imul' and len(ops) == 2:
                B(self.wr(f, ins, ops[0], f'imul16_3(&F, {self.rd(f, ins, ops[0], pre)}, {self.rd(f, ins, ops[1], pre)})', pre))
            else:
                x = self.rd(f, ins, ops[0], pre)
                if m in ('mul', 'imul'):
                    B(f'{m}{bits}(c, &F, {x});')
                else:
                    B(f'{m}{bits}(c, {x}, 0x{lin:05X});')
        elif m in ('shl', 'sal', 'shr', 'sar', 'rol', 'ror', 'rcl', 'rcr'):
            x = self.rd(f, ins, ops[0], pre)
            cnt = '1' if len(ops) == 1 else (self.rd(f, ins, ops[1], pre, 1) if ops[1].type != R.X86_OP_IMM else str(ops[1].imm & 0xFF))
            op_ = 'shl' if m == 'sal' else m
            B(self.wr(f, ins, ops[0], f'{op_}{bits}(&F, {x}, {cnt})', pre))
        elif m in JCC:
            B(f'if ({JCC[m]}) {goto(ops[0].imm)}')
        elif m == 'jcxz':
            B(f'if (c->cx == 0) {goto(ops[0].imm)}')
        elif m in ('loop', 'loope', 'loopne'):
            cond = {'loop': '', 'loope': ' && F.zf', 'loopne': ' && !F.zf'}[m]
            B(f'if (--c->cx != 0{cond}) {goto(ops[0].imm)}')
        elif m == 'jmp':
            if a in switches and (f.seg, a) in COMPUTED_JMP:
                reg, tg = COMPUTED_JMP[(f.seg, a)]
                B(f'switch (c->{reg}) {{')
                for t in tg:
                    B(f'  case 0x{t:X}: {goto(t)}')
                B(f'  default: rt_fault(c, RT_TRAP_BADSWITCH, 0x{lin:05X}, "computed jmp");')
                B('}')
            elif a in switches:
                B('switch (c->bx) {')
                for k, t in enumerate(switches[a]):
                    B(f'  case 0x{2 * k:X}: {goto(t)}')
                B(f'  default: rt_fault(c, RT_TRAP_BADSWITCH, 0x{lin:05X}, "switch");')
                B('}')
            else:
                B(goto(ops[0].imm))
        elif m == 'call':
            op = ops[0]
            if op.type == R.X86_OP_IMM:
                t = op.imm & 0xFFFF
                tf = self.by_lin.get(f.seg * 16 + t)
                B(f'rt_flags_out(c, &F); rt_call_near(c, {tf.sym if tf else "NULL"}, 0x{f.seg:04X}, 0x{t:04X}, 0x{nxt:04X}); rt_flags_in(c, &F);')
            else:
                tgt = self.rd(f, ins, op, pre, 2)
                B(f'rt_flags_out(c, &F); rt_call_near(c, NULL, 0x{f.seg:04X}, {tgt}, 0x{nxt:04X}); rt_flags_in(c, &F);')
        elif m == 'lcall':
            if len(ops) == 2 and ops[0].type == R.X86_OP_IMM:
                ts, to = ops[0].imm & 0xFFFF, ops[1].imm & 0xFFFF
                tf = self.by_lin.get((ts * 16 + to) & 0xFFFFF)
                B(f'rt_flags_out(c, &F); rt_call_far(c, {tf.sym if tf else "NULL"}, 0x{ts:04X}, 0x{to:04X}, SEG(0x{f.seg:04X}), 0x{nxt:04X}); rt_flags_in(c, &F);')
            else:
                s, o = self.memref(f, ins, ops[0], pre)
                B(f'rt_flags_out(c, &F); rt_call_far_ptr(c, rd16({s}, (uint16_t)({o} + 2)), rd16({s}, {o}), SEG(0x{f.seg:04X}), 0x{nxt:04X}); rt_flags_in(c, &F);')
        elif m in ('ret', 'retf'):
            k = (2 if m == 'ret' else 4) + (ops[0].imm & 0xFFFF if ops else 0)
            B(f'rt_flags_out(c, &F); c->sp = (uint16_t)(c->sp + {k}); return;')
        elif m == 'iret':
            B('c->sp = (uint16_t)(c->sp + 4); rt_flags_set(c, &F, pop16(c)); rt_flags_out(c, &F); return;')
        elif m == 'enter':
            B(f'push16(c, c->bp); c->bp = c->sp; c->sp = (uint16_t)(c->sp - 0x{ops[0].imm & 0xFFFF:X});')
            if ops[1].imm:
                raise Unsupported('enter with nesting level')
        elif m == 'leave':
            B('c->sp = c->bp; c->bp = pop16(c);')
        elif m in ('clc', 'stc', 'cmc', 'cld', 'std', 'cli', 'sti'):
            B({'clc': 'F.cf = 0;', 'stc': 'F.cf = 1;', 'cmc': 'F.cf ^= 1;', 'cld': 'c->df = 0;',
               'std': 'c->df = 1;', 'cli': 'c->ifl = 0;', 'sti': 'c->ifl = 1;'}[m])
        elif m in ('movsb', 'movsw', 'stosb', 'stosw', 'lodsb', 'lodsw', 'scasb', 'scasw', 'cmpsb', 'cmpsw'):
            self.emit_string(f, ins, m, B)
        elif m in ('in', 'out'):
            port = None
            for op in ops:
                if op.type == R.X86_OP_IMM:
                    port = f'0x{op.imm & 0xFF:X}'
                elif op.type == R.X86_OP_REG and ins.reg_name(op.reg) == 'dx':
                    port = 'c->dx'
            acc = [ins.reg_name(op.reg) for op in ops if op.type == R.X86_OP_REG and ins.reg_name(op.reg) in ('al', 'ax')][0]
            if m == 'in':
                B(f'c->{acc} = port_in{"8" if acc == "al" else "16"}({port});')
            else:
                B(f'port_out{"8" if acc == "al" else "16"}({port}, c->{acc});')
        elif m == 'int':
            B(f'rt_flags_out(c, &F); rt_interrupt(c, 0x{ops[0].imm & 0xFF:X}); rt_flags_in(c, &F);')
        elif m == 'xlatb':
            seg = 'c->ds'
            B(f'c->al = rd8({seg}, (uint16_t)(c->bx + c->al));')
        elif m == 'nop':
            pass
        elif m == 'hlt':
            B(f'rt_fault(c, RT_TRAP_HALT, 0x{lin:05X}, "hlt");')
        else:
            raise Unsupported(f'{m} {ins.op_str} at {f.seg:04X}:{a:04X}')
        return pre, body

    def emit_string(self, f, ins, m, B):
        pfx = ins.prefix[0]
        rep = {0xF3: 'rep', 0xF2: 'repne'}.get(pfx)
        if rep == 'rep' and m.startswith(('scas', 'cmps')):
            rep = 'repe'
        sp = SEG_PREFIX.get(ins.prefix[1])
        src = self.reg(f, sp) if sp else 'c->ds'
        w = m.endswith('w')
        n = 2 if w else 1
        bits = 16 if w else 8
        step = f'(c->df ? -{n} : {n})'
        acc = 'c->ax' if w else 'c->al'
        core = {
            'movs': f'wr{bits}(c->es, c->di, rd{bits}({src}, c->si)); c->si = (uint16_t)(c->si + {step}); c->di = (uint16_t)(c->di + {step});',
            'stos': f'wr{bits}(c->es, c->di, {acc}); c->di = (uint16_t)(c->di + {step});',
            'lods': f'{acc} = rd{bits}({src}, c->si); c->si = (uint16_t)(c->si + {step});',
            'scas': f'(void)sub{bits}(&F, {acc}, rd{bits}(c->es, c->di), 0); c->di = (uint16_t)(c->di + {step});',
            'cmps': f'(void)sub{bits}(&F, rd{bits}({src}, c->si), rd{bits}(c->es, c->di), 0); c->si = (uint16_t)(c->si + {step}); c->di = (uint16_t)(c->di + {step});',
        }[m[:4]]
        if not rep:
            B(core)
        else:
            stop = {'rep': '', 'repe': ' if (!F.zf) break;', 'repne': ' if (F.zf) break;'}[rep]
            B(f'while (c->cx) {{ {core} c->cx--;{stop} }}')

    def gen_func(self, f):
        insns, switches = self.discover(f)
        labels = set()
        for a, ins in insns.items():
            m = mnem(ins)
            if m in JCC or m in ('jmp', 'jcxz', 'loop', 'loope', 'loopne'):
                if a in switches:
                    labels.update(switches[a])
                elif ins.operands and ins.operands[0].type == R.X86_OP_IMM:
                    labels.add(ins.operands[0].imm & 0xFFFF)
        out = [f'/* {f.seg:04X}:{f.off:04X} (image {f.lin:05X}) module {f.module}{" - " + f.name if f.name else ""}, '
               f'{len(insns)} instructions{", " + str(len(switches)) + " switch table(s)" if switches else ""} */',
               f'void {f.sym}(Cpu *c)', '{', '    Flags F;', '    rt_flags_in(c, &F);']
        prev_end = None
        for a in sorted(insns):
            ins = insns[a]
            if a in labels or (prev_end is not None and prev_end != a):
                out.append(f'L_{a:04X}:;')
            pre, body = self.emit_insn(f, ins, switches, labels)
            body.insert(0, f'RT_STEP(c, 0x{f.seg * 16 + a:05X});')
            text = f'{ins.mnemonic} {ins.op_str}'.strip().replace('*/', '* /')
            if pre:
                out.append(f'    {{ {" ".join(pre)}  /* {a:04X}: {text} */')
                out.extend(f'      {b}' for b in body)
                out.append('    }')
            else:
                out.append(f'    /* {a:04X}: {text} */')
                out.extend(f'    {b}' for b in body)
            m = mnem(ins)
            prev_end = None if m in ('ret', 'retf', 'iret', 'hlt') or (m == 'jmp') else a + ins.size
        if prev_end is not None:
            out.append(f'    rt_fault(c, RT_TRAP_UNSUPPORTED, 0x{f.seg * 16 + prev_end:05X}, "fell off function end");')
        out.append('}')
        self.stats[f.sym] = (len(insns), len(switches))
        f.is_isr = any(mnem(i) == 'iret' for i in insns.values())
        return '\n'.join(out)


def mnem(ins):
    m = ins.mnemonic
    for p in ('rep ', 'repe ', 'repne ', 'repz ', 'repnz '):
        if m.startswith(p):
            m = m[len(p):]
    m = m.replace('rep ', '')
    alias = {'jc': 'jb', 'jnae': 'jb', 'jnb': 'jae', 'jnc': 'jae', 'jz': 'je', 'jnz': 'jne', 'jna': 'jbe',
             'jnbe': 'ja', 'jpe': 'jp', 'jpo': 'jnp', 'jnge': 'jl', 'jnl': 'jge', 'jng': 'jle', 'jnle': 'jg',
             'loopz': 'loope', 'loopnz': 'loopne', 'xlat': 'xlatb', 'sal': 'shl'}
    return alias.get(m, m)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('exe'); ap.add_argument('modules_csv'); ap.add_argument('outdir')
    ap.add_argument('--modules', required=True, help='comma-separated module names from the CSV')
    ap.add_argument('--extra', default='', help='extra SEG:OFF entry points')
    ap.add_argument('--extra-module', default='extra', help='module (output file) name for --extra functions')
    a = ap.parse_args()
    I = Image(a.exe)
    want = set(a.modules.split(','))
    funcs = []
    for r in csv.DictReader(open(a.modules_csv)):
        if r['module'] in want and r['strategy'].startswith('transpile'):
            funcs.append(Func(int(r['seg'], 16), int(r['off'], 16), r['module'], r['name']))
    for e in filter(None, a.extra.split(',')):
        s, o = (int(x, 16) for x in e.split(':'))
        funcs.append(Func(s, o, a.extra_module))
    # functions sharing a linear address (e.g. 0003:005C == 0000:008C): keep first
    seen, uniq = set(), []
    for f in funcs:
        if f.lin not in seen:
            seen.add(f.lin); uniq.append(f)
    funcs = uniq
    G = Gen(I, funcs)
    os.makedirs(a.outdir, exist_ok=True)
    bymod = collections.defaultdict(list)
    failed = []
    for f in funcs:
        try:
            bymod[f.module].append(G.gen_func(f))
        except Unsupported as e:
            failed.append((f, str(e)))
            bymod[f.module].append(f'/* {f.seg:04X}:{f.off:04X} NOT TRANSLATED: {e} */\n'
                                   f'void {f.sym}(Cpu *c) {{ rt_fault(c, RT_TRAP_UNSUPPORTED, 0x{f.lin:05X}, "untranslated"); }}')
    hdr = ['/* generated by tools/recomp.py - do not edit */', '#ifndef RECOMP_GEN_H', '#define RECOMP_GEN_H',
           '#include "recomp_rt.h"']
    hdr += [f'void {f.sym}(Cpu *c);' for f in funcs]
    hdr += ['void recomp_register_all(void);', '#endif', '']
    open(os.path.join(a.outdir, 'recomp_gen.h'), 'w').write('\n'.join(hdr))
    for mod, bodies in bymod.items():
        open(os.path.join(a.outdir, f'recomp_{mod}.c'), 'w').write(
            '/* generated by tools/recomp.py from FOX_UNP2.EXE - do not edit */\n'
            '#include "recomp_gen.h"\n\n' + '\n\n'.join(bodies) + '\n')
    reg = ['/* generated by tools/recomp.py - do not edit */', '#include "recomp_gen.h"', '',
           'void recomp_register_all(void)', '{']
    reg += [f'    far_register_flags(0x{f.seg:04X}, 0x{f.off:04X}, {f.sym}, "{f.sym}", '
            f'{"FAR_FN_ISR" if getattr(f, "is_isr", False) else "0"});' for f in funcs]
    reg += ['}', '']
    open(os.path.join(a.outdir, 'recomp_register.c'), 'w').write('\n'.join(reg))
    with open(os.path.join(a.outdir, 'recomp_report.txt'), 'w') as rep:
        tot = sum(v[0] for v in G.stats.values())
        rep.write(f'{len(funcs)} functions, {len(funcs) - len(failed)} translated, {tot} instructions, '
                  f'{sum(v[1] for v in G.stats.values())} switch tables\n')
        for f, e in failed:
            rep.write(f'FAILED {f.sym}: {e}\n')
        for s, (n, sw) in sorted(G.stats.items()):
            rep.write(f'{s} {n} insns {sw} switches\n')
    print(f'{len(funcs)} functions -> {len(funcs) - len(failed)} translated, '
          f'{sum(v[0] for v in G.stats.values())} instructions, {sum(v[1] for v in G.stats.values())} switch tables')
    for f, e in failed:
        print('  FAILED', f.sym, e)


if __name__ == '__main__':
    main()
