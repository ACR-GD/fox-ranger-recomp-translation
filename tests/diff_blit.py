#!/usr/bin/env python3
"""diff_blit.py - differential test: original x86 blitters (segment 10A7 of
FOX_UNP2.EXE, executed in unicorn) vs the native C replacement (src/blit_10a7.c,
called through build/libfoxrt_test.dylib).

Both sides start from identical RAM (same MZ load, same decoded sprites and
fonts) and identical *random* VRAM, run the same call, and must end with
identical VGA planes, map mask, GC mode, CRTC start address and RAM.

usage: diff_blit.py GAME_DIR FOX_UNP2.EXE LIB [--cases N] [--seed S]
needs: unicorn >= 2 (mmio_map), capstone not required
"""
import argparse, ctypes, os, random, struct, sys
from unicorn import Uc, UC_ARCH_X86, UC_MODE_16, UC_HOOK_INSN, UC_PROT_ALL
from unicorn.x86_const import *

L = 0x1000                      # load segment (both sides)
BLIT = 0x10A7 + L
DESC_SEG, PIX_SEG, FONT_SEG, DST_SEG = 0x5000, 0x6000, 0x5800, 0x9000
STACK_SEG, RET_SEG, RET_OFF = 0x8800, 0x0050, 0x0000   # stack top 0x97FF0 < A0000


class FarPtr(ctypes.Structure):
    _fields_ = [('off', ctypes.c_uint16), ('seg', ctypes.c_uint16)]


# ------------------------------------------------------------ python VGA
class PyVga:
    """Independent planar VGA model for the unicorn side (unchained mode)."""
    def __init__(self):
        self.planes = [bytearray(0x10000) for _ in range(4)]
        self.latch = [0, 0, 0, 0]
        self.seq = [0x03, 0x01, 0x0F, 0x00, 0x06]; self.seq_idx = 0
        self.gc = [0, 0, 0, 0, 0, 0x40, 0x05, 0x0F, 0xFF]; self.gc_idx = 0
        self.crtc = {}; self.crtc_idx = 0

    def write(self, a, val):
        mode, bm = self.gc[5] & 3, self.gc[8]
        for p in range(4):
            if not self.seq[2] & (1 << p):
                continue
            if mode == 1:
                d = self.latch[p]
            elif mode == 0:
                d = ((0xFF if (self.gc[0] >> p) & 1 else 0) if self.gc[1] & (1 << p) else val)
                d = (d & bm) | (self.latch[p] & ~bm & 0xFF)
            else:
                raise RuntimeError('write mode %d not modelled' % mode)
            self.planes[p][a] = d

    def read(self, a):
        for p in range(4):
            self.latch[p] = self.planes[p][a]
        return self.planes[self.gc[4] & 3][a]

    def out(self, port, val):
        if port == 0x3C4: self.seq_idx = val & 7
        elif port == 0x3C5: self.seq[self.seq_idx] = val
        elif port == 0x3CE: self.gc_idx = val & 15
        elif port == 0x3CF: self.gc[self.gc_idx] = val
        elif port == 0x3D4: self.crtc_idx = val
        elif port == 0x3D5: self.crtc[self.crtc_idx] = val
        else: raise RuntimeError('unexpected OUT %x' % port)

    def inp(self, port):
        if port == 0x3CF: return self.gc[self.gc_idx]
        if port == 0x3C5: return self.seq[self.seq_idx]
        raise RuntimeError('unexpected IN %x' % port)


# ---------------------------------------------------------------- set-up
def decode_sprites(game):
    d = open(os.path.join(game, 'VSPRITE.PAT'), 'rb').read()
    t = d[-208 * 6:]
    out, o = [], 0
    for k in range(208):
        w, h, sz = struct.unpack_from('<HHH', t, 6 * k)
        raw = d[o:o + (sz or w * h)]; o += sz or w * h
        if sz:
            px, i = bytearray(), 0
            while i < len(raw):
                c = raw[i]; i += 1
                if c & 0x80: px += bytes([raw[i]]) * (c & 0x7F); i += 1
                else: px += raw[i:i + c]; i += c
            raw = bytes(px[:w * h])
        out.append((w, h, raw))
    return out


def build_ram(lib, exe, game):
    lib.mem_reset()
    info = ctypes.create_string_buffer(32)
    if lib.mz_load(exe.encode(), L, b' a v', info) != 0:
        sys.exit('mz_load failed')
    ram = (ctypes.c_uint8 * 0x110000).from_address(ctypes.cast(lib.t_ram(), ctypes.c_void_p).value)
    sprites = decode_sprites(game)
    # small/medium sprites only (all must fit one 64K pixel segment chain)
    descs, pix_lin = [], PIX_SEG * 16
    for idx, (w, h, px) in enumerate(sprites):
        if not w or not h or w * h > 0x3000:
            continue
        seg = pix_lin >> 4
        ram[pix_lin:pix_lin + len(px)] = px
        d = DESC_SEG * 16 + 11 * len(descs)
        struct.pack_into('<HHHBHH', ram, d, w, h, 0, 1, 0, seg)
        descs.append((DESC_SEG, 11 * (len(descs)), w, h))
        pix_lin += (len(px) + 15) & ~15
        if pix_lin > 0x8F000:
            break
    fox = open(os.path.join(game, 'FOX.DAT'), 'rb').read()
    font = fox[0x834:0x834 + 0x2F8 + 0x5F0] + fox[0x111C:0x111C + 0x1300]
    ram[FONT_SEG * 16:FONT_SEG * 16 + len(font)] = font
    glyph8 = [(FONT_SEG, 8 * i) for i in range(95)]
    glyph816 = [(FONT_SEG, 0x2F8 + 16 * i) for i in range(95)]
    glyph16 = [(FONT_SEG, 0x2F8 + 0x5F0 + 32 * i) for i in range(152)]
    return ram, descs, glyph8, glyph816, glyph16


class X86:
    """Runs one original 10A7 routine in unicorn on a copy of the C-side RAM."""
    def __init__(self, ram_bytes):
        self.uc = uc = Uc(UC_ARCH_X86, UC_MODE_16)
        uc.mem_map(0, 0xA0000)
        uc.mem_map(0xB0000, 0x60000)
        uc.mem_write(0, bytes(ram_bytes[:0xA0000]))
        uc.mem_write(RET_SEG * 16 + RET_OFF, b'\xf4')
        self.vga = PyVga()
        uc.mmio_map(0xA0000, 0x10000, self._mr, None, self._mw, None)
        uc.hook_add(UC_HOOK_INSN, self._out, None, 1, 0, UC_X86_INS_OUT)
        uc.hook_add(UC_HOOK_INSN, self._in, None, 1, 0, UC_X86_INS_IN)

    def _mr(self, uc, off, size, ud):
        v = 0
        for k in range(size):
            v |= self.vga.read((off + k) & 0xFFFF) << (8 * k)
        return v

    def _mw(self, uc, off, size, value, ud):
        for k in range(size):
            self.vga.write((off + k) & 0xFFFF, (value >> (8 * k)) & 0xFF)

    def _out(self, uc, port, size, value, ud):
        self.vga.out(port, value & 0xFF)
        if size == 2:
            self.vga.out(port + 1, value >> 8)

    def _in(self, uc, port, size, ud):
        return self.vga.inp(port)

    def call(self, off, args):
        uc = self.uc
        sp = 0xFFF0
        for a in reversed(args):
            sp -= 2; uc.mem_write(STACK_SEG * 16 + sp, struct.pack('<H', a & 0xFFFF))
        sp -= 4; uc.mem_write(STACK_SEG * 16 + sp, struct.pack('<HH', RET_OFF, RET_SEG))
        for r, v in ((UC_X86_REG_SS, STACK_SEG), (UC_X86_REG_SP, sp), (UC_X86_REG_CS, BLIT),
                     (UC_X86_REG_DS, 0x1ADF + L), (UC_X86_REG_ES, 0), (UC_X86_REG_BP, 0)):
            uc.reg_write(r, v)
        uc.emu_start(BLIT * 16 + off, RET_SEG * 16 + RET_OFF, count=20_000_000)
        if uc.reg_read(UC_X86_REG_CS) != RET_SEG:
            raise RuntimeError('did not return')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('game'); ap.add_argument('exe'); ap.add_argument('lib')
    ap.add_argument('--cases', type=int, default=400); ap.add_argument('--seed', type=int, default=1992)
    a = ap.parse_args()
    lib = ctypes.CDLL(a.lib)
    lib.t_ram.restype = ctypes.POINTER(ctypes.c_uint8)
    lib.t_vram.restype = ctypes.POINTER(ctypes.c_uint8)
    lib.mz_load.argtypes = [ctypes.c_char_p, ctypes.c_uint16, ctypes.c_char_p, ctypes.c_void_p]
    for fn in ('blit_sprite', 'blit_sprite_mono', 'blit_sprite_opaque'):
        getattr(lib, fn).argtypes = [ctypes.c_uint16, ctypes.c_uint16, FarPtr]
    for fn in ('blit_glyph16', 'blit_glyph8x16', 'blit_glyph8x8', 'blit_sprite_shrunk'):
        getattr(lib, fn).argtypes = [ctypes.c_uint16, ctypes.c_uint16, ctypes.c_uint16, FarPtr]
    lib.blit_scroll_strip.argtypes = [ctypes.c_uint16] * 4
    lib.blit_scale_sprite.argtypes = [ctypes.c_uint16, ctypes.c_uint16, FarPtr, FarPtr]

    ram, descs, g8, g816, g16 = build_ram(lib, a.exe, a.game)
    base_ram = bytes(ram)
    rng = random.Random(a.seed)
    stat = {}
    fails = 0

    def coord():
        return rng.choice([rng.randint(-60, 380), rng.randint(0, 319), rng.randint(-20, 20), rng.randint(300, 340)])

    for case in range(a.cases):
        kind = rng.choice(['sprite', 'mono', 'opaque', 'glyph16', 'glyph8x16', 'glyph8x8',
                           'shrunk', 'showpage', 'strip', 'scale'])
        page = rng.choice([0xA000, 0xA400, 0xA800])
        cy0, cy1 = rng.choice([(0, 200), (0x28, 0xA0), (0, 0xC8), (rng.randint(0, 90), rng.randint(110, 200))])
        effect = rng.choice([0, 0, 0x64, rng.randint(1, 255)])
        # identical starting state on both sides
        ram[:] = base_ram
        state = struct.pack('<HhhhhB', page, 0, 320, cy0, cy1, effect)
        ram[(BLIT) * 16 + 0x0A:(BLIT) * 16 + 0x0A + len(state)] = state
        lib.t_mode_y()
        noise = bytes(rng.getrandbits(8) for _ in range(256)) * 256
        for p in range(4):
            ctypes.memmove(lib.t_vram(p), noise[p * 61 % 256:] + noise[:p * 61 % 256], 0x10000)
        x86 = X86(ram)
        for p in range(4):
            x86.vga.planes[p][:] = ctypes.string_at(lib.t_vram(p), 0x10000)

        if kind in ('sprite', 'mono', 'opaque', 'shrunk', 'scale'):
            dseg, doff, w, h = rng.choice(descs)
        if kind == 'sprite':
            x, y = coord() & 0xFFFF, coord() & 0xFFFF
            off, args = 0x002D, [x, y, doff, dseg]; lib.blit_sprite(x, y, FarPtr(doff, dseg))
        elif kind == 'mono':
            x, y = coord() & 0xFFFF, coord() & 0xFFFF
            off, args = 0x01A7, [x, y, doff, dseg]; lib.blit_sprite_mono(x, y, FarPtr(doff, dseg))
        elif kind == 'opaque':
            x, y = rng.randint(0, 320 - min(w, 320)), rng.randint(0, 200 - min(h, 200))
            off, args = 0x030D, [x, y, doff, dseg]; lib.blit_sprite_opaque(x, y, FarPtr(doff, dseg))
        elif kind in ('glyph16', 'glyph8x16', 'glyph8x8'):
            gs, go = rng.choice({'glyph16': g16, 'glyph8x16': g816, 'glyph8x8': g8}[kind])
            x = rng.randint(0, 312)
            y = rng.randint(0, 192) if kind == 'glyph8x8' else (coord() & 0xFFFF)
            col = rng.randint(1, 255) | (rng.choice([0, 0, 1]) << 8)
            off = {'glyph16': 0x0390, 'glyph8x16': 0x0466, 'glyph8x8': 0x056C}[kind]
            args = [x, y, col, go, gs]
            getattr(lib, {'glyph16': 'blit_glyph16', 'glyph8x16': 'blit_glyph8x16',
                          'glyph8x8': 'blit_glyph8x8'}[kind])(x, y, col, FarPtr(go, gs))
        elif kind == 'shrunk':
            div = rng.randint(1, 4)
            x, y = rng.randint(40, 280), rng.randint(40, 160)
            off, args = 0x061C, [x, y, div, doff, dseg]; lib.blit_sprite_shrunk(x, y, div, FarPtr(doff, dseg))
        elif kind == 'showpage':
            off, args = 0x070C, []; lib.blit_show_page()
        elif kind == 'strip':
            row, rows, width = rng.randint(0, 150), rng.randint(1, 40), rng.randint(1, 40)
            buf = rng.choice([s for s in (0xA000, 0xA400, 0xA800) if s != page])
            off, args = 0x0721, [row, rows, width, buf]; lib.blit_scroll_strip(row, rows, width, buf)
        else:  # scale
            dw, dh = rng.randint(1, 80), rng.randint(1, 60)
            off, args = 0x07AE, [dw, dh, doff, dseg, 0, DST_SEG]
            lib.blit_scale_sprite(dw, dh, FarPtr(doff, dseg), FarPtr(0, DST_SEG))

        x86.call(off, args)

        problems = []
        for p in range(4):
            c = ctypes.string_at(lib.t_vram(p), 0x10000)
            if c != bytes(x86.vga.planes[p]):
                i = next(k for k in range(0x10000) if c[k] != x86.vga.planes[p][k])
                problems.append(f'plane {p} @ {i:04x}: C {c[i]:02x} x86 {x86.vga.planes[p][i]:02x}')
        if lib.t_seq(2) != x86.vga.seq[2]: problems.append('map mask')
        if lib.t_gc(5) != x86.vga.gc[5]: problems.append('GC mode')
        if 0x0C in x86.vga.crtc and lib.t_crtc(0x0C) != x86.vga.crtc[0x0C]: problems.append('CRTC 0C')
        if kind == 'scale':
            n = args[0] * args[1]
            if bytes(ram[DST_SEG * 16:DST_SEG * 16 + n]) != bytes(x86.uc.mem_read(DST_SEG * 16, n)):
                problems.append('scaled buffer')
        s = stat.setdefault(kind, [0, 0]); s[0] += 1
        if problems:
            s[1] += 1; fails += 1
            if fails <= 10:
                print(f'FAIL case {case} {kind} args={[hex(v) for v in args]} page={page:04x} '
                      f'clip_y={cy0}-{cy1} effect={effect:#x}: ' + '; '.join(problems))

    for k, (n, f) in sorted(stat.items()):
        print(f'  {k:10} {n:4} cases  {f} failures')
    print('PASS' if not fails else f'FAILED: {fails}/{a.cases}')
    sys.exit(1 if fails else 0)


if __name__ == '__main__':
    main()
