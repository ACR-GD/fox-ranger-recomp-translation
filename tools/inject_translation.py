#!/usr/bin/env python3
"""inject_translation.py - put English text into the unpacked FOX.EXE.

usage: inject_translation.py IN.EXE OUT.EXE [--intro intro_en.json]
                             [--ending ending_en.json] [--credits credits_en.json]
                             [--fox-dat FOX.DAT --preview DIR] [--allow-more-lines]

IN.EXE must still contain the original Korean strings of every section being
injected (use FOX_UNP2.EXE or FOX_NOPROT.EXE, not an already translated file).

How the game draws text (load-image offsets, see RECON.md):
  * story strings are reached only through far-pointer tables in DGROUP 1ADF:
        intro  DS:0B0A x13 (player img FF00-106DA)
        ending DS:029C x4  (img F098-F307)
    so the strings can be repacked anywhere in the freed blocks and repointed.
  * all of them go through the typewriter FD8:0960(x0, y, color, char far*):
        byte >= 0x80: Johab pair, wrap if x >= 0x138-x0, draw 16x16, x += 16
        byte <  0x80: ASCII,      wrap if x >= 0x140-x0, FOX.DAT 8x16 glyph, x += 8
        wrap = (y += 16, x = x0): per character, no word wrap, no '\\n'.
    English is word-wrapped here by padding each line with spaces to the full
    column count, so the engine's character wrap falls between words.
    x0 = 16 -> 36 columns, x0 = 0 -> 40 columns, 'center' -> x0=(40-strlen)*4.
  * credits: 51 slots of {char text[41]; u8 color} at DS:02AC, each drawn as
    one centred line (no wrap) by the scroller at img FB67 -> max 40 chars.
"""
import argparse, json, os, struct, sys, textwrap, zlib

HDR = 0x25E0          # header size of FOX_UNP2.EXE
DGROUP = 0x1ADF
DS_FILE = HDR + DGROUP * 16   # file offset of DS:0000
SCREEN_W, SCREEN_H, LINE_H = 320, 200, 16

# section -> pointer table, entry count, per-entry (mode, y, call site)
#   mode: 'center' (x0 = (40-len)*4), or an int x0
SECTIONS = {
    'intro': dict(table=0x0B0A, count=13, layout={
        0: ('center', 0x5C, 'img FF77'), 1: (16, 0x5C, 'img FFBE'), 2: ('center', 0x5C, 'img 1000F'),
        3: (16, 0x6F, 'img 10078'), 4: (16, 0x64, 'img 100E4'), 5: (16, 0x6F, 'img 10150'),
        6: (16, 0x6F, 'img 101C0'), 7: (16, 0x6F, 'img 10230'), 8: (16, 0x6F, 'img 1027C'),
        9: (16, 0x5C, 'img 102B7'), 10: (16, 0x6F, 'FD8:0C75 pic A3'),
        11: (16, 0x6F, 'FD8:0C75 pic A4'), 12: (16, 0x6F, 'FD8:0C75 pic A5')}),
    'ending': dict(table=0x029C, count=4, layout={
        0: ('center', 0x6F, 'img F0C2'), 1: (0, 0x60, 'img F270'),
        2: (0, 0x60, 'img F2A8'), 3: ('center', 0x5C, 'img F307')}),
}
CREDITS_TABLE, CREDITS_SLOTS, CREDITS_SLOT, CREDITS_TEXT = 0x02AC, 51, 42, 41


def x0_for(mode, s):
    return (40 - len(s)) * 4 if mode == 'center' else mode


def cols_for(mode):
    return 40 if mode == 'center' else (0x140 - 2 * mode) // 8


def typeset(s, x0):
    """Exact re-implementation of FD8:0960's cursor logic.
    Returns glyph placements (x, line, bytes) and the line count."""
    out, x, line, i = [], x0, 0, 0
    while i < len(s):
        if s[i] > 0x7F:
            if x >= 0x138 - x0:
                line += 1; x = x0
            out.append((x, line, s[i:i + 2])); x += 16; i += 2
        else:
            if x >= 0x140 - x0:
                line += 1; x = x0
            out.append((x, line, s[i:i + 1])); x += 8; i += 1
    return out, (out[-1][1] + 1 if out else 0)


def wrap_english(text, cols):
    lines = textwrap.wrap(text, cols, break_long_words=True, break_on_hyphens=False)
    return ''.join(l.ljust(cols) for l in lines[:-1]) + (lines[-1] if lines else ''), lines


def cstr(data, ds_off, limit=None):
    a = DS_FILE + ds_off
    e = data.index(b'\0', a)
    if limit is not None:
        e = min(e, a + limit)
    return bytes(data[a:e])


def check_ascii(text, what, errors):
    bad = sorted({c for c in text if not ' ' <= c <= '~'})
    if bad:
        errors.append(f'{what}: characters outside the 8x16 font (0x20-0x7E): {bad!r}')
        return False
    return True


# ------------------------------------------------------------------ PNG
def write_png(path, w, h, px, pal):
    def chunk(t, d):
        return struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t + d) & 0xFFFFFFFF)
    rows = b''.join(b'\0' + bytes(px[r * w:(r + 1) * w]) for r in range(h))
    open(path, 'wb').write(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 3, 0, 0, 0))
                           + chunk(b'PLTE', bytes(pal)) + chunk(b'IDAT', zlib.compress(rows)) + chunk(b'IEND', b''))


def blit_text(px, w, h, placements, y, font16):
    for x, ln, ch in placements:
        g = font16[(ch[0] - 0x20) * 16:(ch[0] - 0x20) * 16 + 16]
        for r in range(16):
            yy = y + ln * LINE_H + r
            for b in range(8):
                if 0 <= yy < h and 0 <= x + b < w and g[r] & (0x80 >> b):
                    px[yy * w + x + b] = 1


PAL = [0, 0, 0, 255, 255, 255, 40, 40, 90, 90, 30, 30]


def preview_screen(path, b, mode, y, font16):
    px = bytearray(SCREEN_W * SCREEN_H)
    x0 = x0_for(mode, b)
    if mode != 'center':                     # mark the text box edges
        for yy in range(SCREEN_H):
            for xx in (x0 - 1, SCREEN_W - x0):
                if 0 <= xx < SCREEN_W:
                    px[yy * SCREEN_W + xx] = 2
    blit_text(px, SCREEN_W, SCREEN_H, typeset(b, x0)[0], y, font16)
    write_png(path, SCREEN_W, SCREEN_H, px, PAL)


def preview_credits(path, lines, font16):
    """Credits rows: one centred line each, 30 px apart (scroller at img FB67)."""
    h = 30 * len(lines) + 10
    px = bytearray(SCREEN_W * h)
    for i, (b, _) in enumerate(lines):
        x0 = (40 - len(b)) * 4
        blit_text(px, SCREEN_W, h, [(x0 + 8 * k, 0, b[k:k + 1]) for k in range(len(b))], 5 + 30 * i, font16)
    write_png(path, SCREEN_W, h, px, PAL)


# ------------------------------------------------------------ sections
def load_section(data, name, path, args, errors):
    sec = SECTIONS[name]
    ptrs = []
    for k in range(sec['count']):
        off, seg = struct.unpack_from('<HH', data, DS_FILE + sec['table'] + 4 * k)
        if seg != DGROUP:
            raise SystemExit(f'{name} table entry {k}: segment {seg:04X} != {DGROUP:04X} - wrong binary?')
        ptrs.append(off)
    tr = {e['index']: e for e in json.load(open(path, encoding='utf-8'))}
    if sorted(tr) != list(range(sec['count'])):
        raise SystemExit(f'{path}: needs indices 0..{sec["count"] - 1}')

    spans = sorted((p, p + len(cstr(data, p)) + 1) for p in ptrs)
    for (s0, e0), (s1, e1) in zip(spans, spans[1:]):
        if s1 != e0:
            raise SystemExit(f'{name}: strings not contiguous ({e0:#x}..{s1:#x}) - already injected? '
                             'start from FOX_UNP2.EXE / FOX_NOPROT.EXE')
    region = (spans[0][0], spans[-1][1])

    print(f'\n== {name}: table DS:{sec["table"]:04X}, original block DS:{region[0]:04X}-{region[1]:04X} '
          f'({region[1] - region[0]} B)')
    print(f'{"#":>2} {"x0":>6} {"y":>3} {"KO lines":>8} {"EN lines":>8} {"bytes":>5}  first line')
    entries = []
    for k in range(sec['count']):
        mode, y, _ = sec['layout'][k]
        ko = cstr(data, ptrs[k])
        if 'ds_offset' in tr[k] and int(tr[k]['ds_offset'], 16) != ptrs[k]:
            raise SystemExit(f'{name} {k}: table points to {ptrs[k]:#x}, translation says {tr[k]["ds_offset"]}')
        text = tr[k]['english']
        if not check_ascii(text, f'{name} {k}', errors):
            continue
        cols = cols_for(mode)
        if mode == 'center':
            packed, lines = text, [text]
            if len(text) > cols:
                errors.append(f'{name} {k}: centred line longer than {cols} columns')
        else:
            packed, lines = wrap_english(text, cols)
        b = packed.encode('ascii')
        ko_lines = typeset(ko, x0_for(mode, ko))[1]
        en_lines = typeset(b, x0_for(mode, b))[1]
        assert en_lines == len(lines), (name, k, en_lines, lines)
        bottom, flag = y + en_lines * LINE_H, ''
        if bottom > SCREEN_H:
            errors.append(f'{name} {k}: {en_lines} lines run off screen (bottom y={bottom})')
            flag = '  <-- OFF SCREEN'
        elif en_lines > ko_lines and not args.allow_more_lines:
            errors.append(f'{name} {k}: {en_lines} lines > Korean {ko_lines} (--allow-more-lines to accept)')
            flag = '  <-- more lines than original'
        print(f'{k:2} {str(mode):>6} {y:3} {ko_lines:8} {en_lines:8} {len(b):5}  {lines[0]!r}{flag}')
        entries.append(dict(section=name, index=k, bytes=b, lines=lines, cols=cols, mode=mode, y=y))
    return entries, region


def pack(entries, regions):
    """First-fit-decreasing of NUL-terminated strings into the freed blocks."""
    free = [list(r) for r in sorted(regions)]
    for e in sorted(entries, key=lambda e: -len(e['bytes'])):
        need = len(e['bytes']) + 1
        for r in free:
            if r[1] - r[0] >= need:
                e['ds'] = r[0]; r[0] += need
                break
        else:
            return False, free
    return True, free


def inject_credits(data, path, errors, previews):
    rows = json.load(open(path, encoding='utf-8'))
    print(f'\n== credits: {CREDITS_SLOTS} slots x {CREDITS_SLOT} B at DS:{CREDITS_TABLE:04X} '
          f'(text[{CREDITS_TEXT}] + color byte)')
    for r in rows:
        slot = r['slot']
        if not 0 <= slot < CREDITS_SLOTS:
            errors.append(f'credits: slot {slot} out of range'); continue
        base = DS_FILE + CREDITS_TABLE + slot * CREDITS_SLOT
        if 'ds_offset' in r and int(r['ds_offset'], 16) != CREDITS_TABLE + slot * CREDITS_SLOT:
            raise SystemExit(f'credits slot {slot}: ds_offset mismatch')
        text = r['english']
        if not check_ascii(text, f'credits slot {slot}', errors):
            continue
        if len(text) > CREDITS_TEXT - 1:
            errors.append(f'credits slot {slot}: {len(text)} chars > {CREDITS_TEXT - 1}'); continue
        old = cstr(data, CREDITS_TABLE + slot * CREDITS_SLOT, CREDITS_TEXT)
        color = data[base + CREDITS_TEXT]
        b = text.encode('ascii')
        data[base:base + CREDITS_TEXT] = b + b'\0' * (CREDITS_TEXT - len(b))   # colour byte untouched
        print(f'  slot {slot:2} color {color:02X}  {len(b):2}/40  {text!r}  (was {old.decode("johab", "replace")})')
        previews.append((b, color))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('exe'); ap.add_argument('out')
    ap.add_argument('--intro'); ap.add_argument('--ending'); ap.add_argument('--credits')
    ap.add_argument('--fox-dat', help='FOX.DAT (for --preview)')
    ap.add_argument('--preview', help='directory for rendered PNG previews')
    ap.add_argument('--allow-more-lines', action='store_true',
                    help='permit more lines than the Korean original (still bounded by the screen)')
    a = ap.parse_args()
    if not (a.intro or a.ending or a.credits):
        ap.error('nothing to inject: give --intro, --ending and/or --credits')

    data = bytearray(open(a.exe, 'rb').read())
    if data[:2] != b'MZ' or int.from_bytes(data[8:10], 'little') * 16 != HDR:
        raise SystemExit('expected the unpacked FOX_UNP2.EXE (or a patched copy of it)')

    errors, entries, regions = [], [], []
    for name in ('intro', 'ending'):
        if getattr(a, name):
            e, r = load_section(data, name, getattr(a, name), a, errors)
            entries += e; regions.append(r)

    if entries:
        need = sum(len(e['bytes']) + 1 for e in entries)
        have = sum(r[1] - r[0] for r in regions)
        ok, free = pack(entries, regions)
        print(f'\nstring pool: {need}/{have} bytes in {len(regions)} block(s)')
        if not ok:
            errors.append(f'strings do not fit the freed blocks ({need}/{have} B, fragmented) - shorten text')

    credit_rows = []
    if a.credits:
        inject_credits(data, a.credits, errors, credit_rows)

    if errors:
        print('\nERRORS:\n  ' + '\n  '.join(errors))
        raise SystemExit(1)

    if entries:
        for lo, hi in regions:                       # clear freed blocks
            data[DS_FILE + lo:DS_FILE + hi] = b'\0' * (hi - lo)
        for e in entries:
            data[DS_FILE + e['ds']:DS_FILE + e['ds'] + len(e['bytes'])] = e['bytes']
            tbl = SECTIONS[e['section']]['table']
            struct.pack_into('<HH', data, DS_FILE + tbl + 4 * e['index'], e['ds'], DGROUP)
        for e in sorted(entries, key=lambda e: (e['section'], e['index'])):
            print(f'\n[{e["section"]} {e["index"]}] -> DS:{e["ds"]:04X}\n    '
                  + '\n    '.join('|' + l.ljust(e['cols']) + '|' for l in e['lines']))

    open(a.out, 'wb').write(data)
    print(f'\nwrote {a.out}')

    if a.preview:
        if not a.fox_dat:
            raise SystemExit('--preview needs --fox-dat')
        os.makedirs(a.preview, exist_ok=True)
        font16 = open(a.fox_dat, 'rb').read()[0xB2C:0xB2C + 0x5F0]
        for e in entries:
            preview_screen(os.path.join(a.preview, f'{e["section"]}_{e["index"]:02d}.png'),
                           e['bytes'], e['mode'], e['y'], font16)
        if credit_rows:
            preview_credits(os.path.join(a.preview, 'credits_dedication.png'), credit_rows, font16)
        print(f'previews -> {a.preview}')


if __name__ == '__main__':
    main()
