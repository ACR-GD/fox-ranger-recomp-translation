#!/usr/bin/env python3
"""module_map.py functions.csv OUT.csv - assign every recovered function to a
recompilation module and strategy (see RECOMP_PLAN.md for the rationale).
Segment-level assignment, with per-function overrides for known routines."""
import csv, sys, collections

# segment -> (module, strategy)
SEGMENTS = {
    0x0000: ('game_loop', 'transpile'),     # main loop, menus, options, hi-scores, credits driver
    0x0003: ('game_loop', 'transpile'),
    0x05FC: ('game_logic', 'transpile'),    # objects / weapons / collisions (weapon tables)
    0x078E: ('game_logic', 'transpile'),
    0x0A90: ('game_logic', 'transpile'),
    0x0D24: ('game_logic', 'transpile'),
    0x0D81: ('game_logic', 'transpile'),
    0x0E39: ('game_logic', 'transpile'),    # stage set-up (stage.dat), backgrounds
    0x0EFF: ('story_ui', 'transpile'),      # intro / ending / credits sequencing
    0x0FD8: ('story_ui', 'transpile'),      # typewriter FD8:0960, text boxes
    0x04A4: ('system_video', 'transpile+native-hw'),   # mode set, palette, fades, page clear, protection
    0x10A7: ('vga_render', 'native-done'),  # blitters: src/blit_10a7.c, diff-tested
    0x116B: ('file_io', 'transpile'),       # fox.dat / vsprite.pat / score.dat loaders, glyph composer
    0x113E: ('audio_sfx', 'transpile'),     # voice.dat sample loader -> CT-VOICE calls
    0x159E: ('audio_sfx', 'native'),        # SB glue (DSP, DMA, IRQ)
    0x15E5: ('audio_sfx', 'native'),        # embedded CT-VOICE.DRV
    0x030C: ('audio_music', 'transpile'),   # music sequencer (adlib/mt32/sc55 data)
    0x1129: ('audio_music', 'transpile'),   # MIDI/OPL byte output
    0x1258: ('audio_music', 'transpile'),
    0x1265: ('audio_music', 'transpile'),   # OPL driver
    0x1255: ('audio_music', 'transpile'),   # OPL register write + delay (ports [DS:218C] -> io.c -> audio_opl.c)
    0x1337: ('platform_timer', 'transpile'),  # music INT 08h multiplexer (state in CS:000E-0020); delivered by platform.c
    0x0E0C: ('platform_timer', 'transpile'),  # INT 08h ISR 0E0C:016A, PIT 72.8 Hz, delay(); delivered by platform.c
    0x0C6A: ('platform_input', 'transpile'),  # INT 09h ISR 0C6A:0139, key state tables; delivered by platform.c
    0x134D: ('crt', 'native-shim'),         # MSC 6 runtime
}
OVERRIDES = {  # (seg, off) -> (module, strategy, name)
    (0x04A4, 0x00A2): ('vga_render', 'native-done', 'vga_set_mode_y'),
    (0x04A4, 0x14B0): ('system_video', 'stub', 'check_keyfiles (always 0)'),
    (0x0FD8, 0x0960): ('story_ui', 'transpile', 'typewriter'),
    (0x0FD8, 0x0C75): ('story_ui', 'transpile', 'picture_caption'),
    (0x116B, 0x091E): ('file_io', 'transpile', 'draw_text_johab'),
    (0x134D, 0x07A0): ('crt', 'native-shim', 'fopen'),
    (0x134D, 0x07BC): ('crt', 'native-shim', 'fread'),
    (0x134D, 0x06A6): ('crt', 'native-shim', 'fclose'),
    (0x134D, 0x1D7C): ('crt', 'native-shim', 'fseek'),
    (0x134D, 0x1D1C): ('crt', 'native-shim', 'strlen'),
    (0x134D, 0x19CC): ('crt', 'native-shim', 'farmalloc'),
    (0x134D, 0x01DF): ('crt', 'native-shim', 'exit'),
    (0x134D, 0x0AC6): ('crt', 'native-shim', 'puts'),
    (0x134D, 0x229F): ('crt', 'native-shim', '_dos_findfirst'),
    (0x134D, 0x22D2): ('crt', 'native-shim', '_dos_setvect'),
    (0x10A7, 0x002D): ('vga_render', 'native-done', 'blit_sprite'),
    (0x10A7, 0x01A7): ('vga_render', 'native-done', 'blit_sprite_mono'),
    (0x10A7, 0x030D): ('vga_render', 'native-done', 'blit_sprite_opaque'),
    (0x10A7, 0x0390): ('vga_render', 'native-done', 'blit_glyph16'),
    (0x10A7, 0x0466): ('vga_render', 'native-done', 'blit_glyph8x16'),
    (0x10A7, 0x056C): ('vga_render', 'native-done', 'blit_glyph8x8'),
    (0x10A7, 0x061C): ('vga_render', 'native-done', 'blit_sprite_shrunk'),
    (0x10A7, 0x070C): ('vga_render', 'native-done', 'blit_show_page'),
    (0x10A7, 0x0721): ('vga_render', 'native-done', 'blit_scroll_strip'),
    (0x10A7, 0x07AE): ('vga_render', 'native-done', 'blit_scale_sprite'),
    (0x0C6A, 0x0139): ('platform_input', 'transpile', 'int09_isr'),
    (0x0E0C, 0x016A): ('platform_timer', 'transpile', 'int08_isr'),
}

rows = list(csv.DictReader(open(sys.argv[1])))
out = []
for r in rows:
    srcs = set(r['sources'].split('|'))
    if srcs and all(x.startswith('isr') for x in srcs) and r['ret'] != 'iret':
        continue   # setvect heuristic false positive (0003:005C lies inside 0000:0000)
    seg, off = int(r['seg'], 16), int(r['off'], 16)
    mod, strat = SEGMENTS.get(seg, ('unknown', 'investigate'))
    name = ''
    if (seg, off) in OVERRIDES:
        mod, strat, name = OVERRIDES[(seg, off)]
    out.append(dict(seg=r['seg'], off=r['off'], linear=r['linear'], size=r['size'],
                    module=mod, strategy=strat, name=name, sources=r['sources']))
with open(sys.argv[2], 'w', newline='') as f:
    w = csv.DictWriter(f, fieldnames=list(out[0]))
    w.writeheader(); w.writerows(out)
tot = collections.defaultdict(lambda: [0, 0])
for o in out:
    t = tot[(o['module'], o['strategy'] if o['strategy'] != 'native-done' else 'native-done')]
    t[0] += 1; t[1] += int(o['size'])
print(f'{"module":16} {"strategy":20} {"funcs":>5} {"bytes":>7}')
for (m, s), (n, b) in sorted(tot.items()):
    print(f'{m:16} {s:20} {n:5} {b:7}')
print(f'total {len(out)} functions')
