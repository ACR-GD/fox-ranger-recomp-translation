#!/usr/bin/env python3
"""Generic emulation-based unpacker for DOS MZ executables (PKLITE, LZEXE, ...).

Runs the decompression stub in a 16-bit real-mode emulator (unicorn) until
control is transferred to the original entry point (OEP), then dumps the
image.  The stub is run twice at two different load segments; differing
words between the two dumps reveal the relocation table, which lets us
rebuild a proper, uncompressed MZ executable.

usage: unpack_exe.py PACKED.EXE OUT.EXE
"""
import struct, sys
from unicorn import Uc, UC_ARCH_X86, UC_MODE_16, UC_HOOK_INTR, UC_HOOK_CODE
from unicorn.x86_const import *

MEM = 0x110000


def parse_mz(data):
    (sig, last, pages, nrel, hdrpar, minalloc, maxalloc, ss, sp, csum, ip, cs,
     relofs, ovl) = struct.unpack_from('<2s13H', data, 0)
    size = pages * 512 - (512 - last if last else 0)
    return dict(hdr=hdrpar * 16, size=size, nrel=nrel, relofs=relofs,
                ss=ss, sp=sp, ip=ip, cs=cs, minalloc=minalloc)


def run(data, load_seg):
    h = parse_mz(data)
    img = data[h['hdr']:h['size']]
    psp = load_seg - 0x10
    uc = Uc(UC_ARCH_X86, UC_MODE_16)
    uc.mem_map(0, MEM)
    # minimal PSP
    psp_bytes = bytearray(256)
    psp_bytes[0:2] = b'\xcd\x20'
    struct.pack_into('<H', psp_bytes, 2, 0x9FFF)  # top of memory
    psp_bytes[0x80] = 0
    psp_bytes[0x81] = 0x0D
    uc.mem_write(psp * 16, bytes(psp_bytes))
    uc.mem_write(load_seg * 16, img)
    # apply MZ relocations of the packed file itself (normally none)
    for i in range(h['nrel']):
        o, s = struct.unpack_from('<HH', data, h['relofs'] + 4 * i)
        a = (load_seg + s) * 16 + o
        v = struct.unpack('<H', uc.mem_read(a, 2))[0]
        uc.mem_write(a, struct.pack('<H', (v + load_seg) & 0xFFFF))
    uc.reg_write(UC_X86_REG_CS, (load_seg + h['cs']) & 0xFFFF)
    uc.reg_write(UC_X86_REG_IP, h['ip'])
    uc.reg_write(UC_X86_REG_SS, load_seg + h['ss'])
    uc.reg_write(UC_X86_REG_SP, h['sp'])
    uc.reg_write(UC_X86_REG_DS, psp)
    uc.reg_write(UC_X86_REG_ES, psp)
    stub_cs = (load_seg + h['cs']) & 0xFFFF
    state = {'oep': None, 'last_cs': stub_cs, 'jumps': []}

    def on_int(uc, intno, _):
        ah = uc.reg_read(UC_X86_REG_AH)
        if intno == 0x21 and ah in (0x4A, 0x30, 0x25, 0x35, 0x09, 0x48):
            if ah == 0x30:
                uc.reg_write(UC_X86_REG_AX, 0x0005)
            if ah == 0x48:
                uc.reg_write(UC_X86_REG_AX, 0x8000)
            # clear CF in the pushed FLAGS
            fl = uc.reg_read(UC_X86_REG_EFLAGS) & ~1
            uc.reg_write(UC_X86_REG_EFLAGS, fl)
            return
        raise RuntimeError('unhandled int %02x ah=%02x at %04x:%04x' % (
            intno, ah, uc.reg_read(UC_X86_REG_CS), uc.reg_read(UC_X86_REG_IP)))

    def on_code(uc, addr, size, _):
        cs = uc.reg_read(UC_X86_REG_CS)
        if cs != state['last_cs']:
            state['jumps'].append((state['last_cs'], cs, uc.reg_read(UC_X86_REG_IP)))
            state['last_cs'] = cs
            # OEP: control lands inside the program image, below the stub
            # OEP heuristic: a far transfer from the (relocated) stub segment
            # back down into the program image, after the stub moved itself up
            moved = [j for j in state['jumps'][:-1] if j[1] > state['jumps'][-1][1]]
            if load_seg <= cs and moved and state['jumps'][-1][0] == moved[-1][1]:
                state['oep'] = (cs, uc.reg_read(UC_X86_REG_IP),
                                uc.reg_read(UC_X86_REG_SS), uc.reg_read(UC_X86_REG_SP))
                uc.emu_stop()

    uc.hook_add(UC_HOOK_INTR, on_int)
    uc.hook_add(UC_HOOK_CODE, on_code)
    uc.emu_start(h['ip'] + stub_cs * 16, 0xFFFFF, count=200_000_000)
    if not state['oep']:
        raise RuntimeError('OEP not reached; jumps=%r' % state['jumps'])
    mem = bytes(uc.mem_read(load_seg * 16, MEM - load_seg * 16))
    return state['oep'], mem, state['jumps']


def main(src, dst):
    data = open(src, 'rb').read()
    A, B = 0x1000, 0x2100  # delta low byte 0 -> clean word diffs
    (csA, ipA, ssA, spA), memA, jumps = run(data, A)
    (csB, ipB, ssB, spB), memB, _ = run(data, B)
    print('CS transitions:', ['%04x->%04x:%04x' % j for j in jumps])
    assert ipA == ipB and csA - A == csB - B
    # image length: up to the stub segment of run A (decompressed area)
    h = parse_mz(data)
    end = (h['cs'] + 0) * 16
    # the decompressor may unpack beyond the stub; find extent where dumps are meaningful
    # use: last non-zero byte before the relocated stub copy, bounded by both runs
    limit = min(len(memA), len(memB))
    # find highest differing-or-nonzero region below the original SS (stack) or 0x9000 paras
    img_end = max((ssA - A) * 16 + spA, end)
    img_end = min(img_end, limit)
    # never include the relocated decompressor copy
    stub_hi = max(j[1] for j in jumps)
    img_end = min(img_end, (stub_hi - A) * 16)
    while img_end > 0 and memA[img_end - 1] == 0 and memB[img_end - 1] == 0:
        img_end -= 1
    img_end = (img_end + 15) & ~15
    relocs = []
    delta = B - A
    i = 0
    while i < img_end - 1:
        wa = struct.unpack_from('<H', memA, i)[0]
        wb = struct.unpack_from('<H', memB, i)[0]
        if wa != wb:
            if (wb - wa) & 0xFFFF != delta:
                raise RuntimeError('unexpected diff at %x: %04x %04x' % (i, wa, wb))
            relocs.append(i)
            i += 2
        else:
            i += 1
    image = bytearray(memA[:img_end])
    for r in relocs:  # un-relocate to load segment 0
        v = struct.unpack_from('<H', image, r)[0]
        struct.pack_into('<H', image, r, (v - A) & 0xFFFF)
    hdr_size = (0x1C + 4 * len(relocs) + 15) // 16 * 16
    total = hdr_size + len(image)
    hdr = bytearray(hdr_size)
    min_extra = max(0, ((ssA - A) * 16 + spA - img_end + 15) // 16) + 0x10
    struct.pack_into('<2s13H', hdr, 0, b'MZ', total % 512, (total + 511) // 512,
                     len(relocs), hdr_size // 16, min(min_extra + h['minalloc'], 0xFFFF), 0xFFFF,
                     ssA - A, spA, 0, ipA, csA - A, 0x1C, 0)
    for n, r in enumerate(relocs):
        struct.pack_into('<HH', hdr, 0x1C + 4 * n, r & 0xF, r >> 4)
    open(dst, 'wb').write(bytes(hdr) + bytes(image))
    print('OEP %04x:%04x  SS:SP %04x:%04x  image %d bytes  relocs %d -> %s' % (
        csA - A, ipA, ssA - A, spA, img_end, len(relocs), dst))


if __name__ == '__main__':
    main(*sys.argv[1:3])
