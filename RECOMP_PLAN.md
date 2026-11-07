# Fox Ranger — static recompilation plan (C99 + SDL2)

Target: macOS / Linux / Windows from one C99 code base. Input is `work/FOX_UNP2.EXE` (the fully unpacked MZ, see RECON.md). Translated builds come from the same pipeline (`work/FOX_EN_FINAL.EXE` only differs in data).

## 1. Architecture

```
 ┌───────────── recompiled C (generated, tools/recomp.py) ─────────────┐
 │ game_loop · game_logic · story_ui · file_io · audio_music · system  │
 └──────┬──────────────┬──────────────┬───────────────┬────────────────┘
        │ mem_rd/wr    │ port_in/out  │ far_call()    │ crt shims
 ┌──────▼──────┐ ┌─────▼─────┐ ┌──────▼──────┐ ┌──────▼──────┐
 │ ram[1 MB]   │ │ io.c      │ │ cpu.c far   │ │ crt_*.c     │   native runtime (src/)
 │ mem.h       │ │ PIT PIC   │ │ registry    │ │ fopen fread │
 │ A000→VGA    │ │ VGA OPL   │ │ + natives   │ │ farmalloc…  │
 └──────┬──────┘ └─────┬─────┘ └─────────────┘ └─────────────┘
        └──────┬───────┘
     ┌─────────▼─────────┐  ┌───────────────┐  ┌─────────────────┐
     │ vga_modex.c       │  │ blit_10a7.c   │  │ platform (SDL2) │
     │ 4×64K planes,     │◄─┤ native, diff- │  │ video 70 Hz,    │
     │ latches, WM0-3,   │  │ tested vs x86 │  │ INT08 @72.8 Hz, │
     │ DAC, CRTC scanout │  └───────────────┘  │ INT09 scancodes,│
     └───────────────────┘                     │ OPL + PCM audio │
                                               └─────────────────┘
```

**Key decision: keep the DOS memory layout.** The program keeps its data in many places:
- DGROUP, far heaps, the per-stage sprite heap and code-segment variables;
- 64 DGROUP words holding segment values (e.g. 10A7, 1682);
- far-pointer tables such as the story text at DS:0B0A.

Re-laying that out would mean rewriting every pointer. Instead the unpacked EXE is loaded into `ram[]` exactly like DOS does (`mz_load.c`): PSP, image and relocations. Generated code then addresses memory by `seg:off` through `mem.h`.

## 2. Sprite blitters and the "self-modifying code" (segment 10A7)

### 2.1 Findings

**There is no self-modifying code.** The triage flagged 86 code-segment writes, and all of them come from one source.
- In 10A7 they all go to a **data block at the start of that code segment**, CS:000A–002C. No instruction bytes are ever written.
- The other CS-relative accesses in the program are of two kinds:
  - **14 inline switch tables** (13 in game code, 1 in the CRT), `jmp cs:[bx+tbl]`, at 0000:034D, 030C:119F, 04A4:0B51, 05FC:0245, 05FC:0E55, 078E:2570, 078E:2DBA, 0A90:0C0C, 0A90:1B12, 0D24:0561, 0E39:08ED, 116B:0AEE, 1265:07CB and 134D:100F;
  - **ISR state** at 1337:000E–0020, and one access inside the CT-VOICE driver (15E5:006E).

10A7 data block (state kept in `ram`, so recompiled callers' writes keep working):

| CS: | type | meaning | written by |
|---|---|---|---|
| 000A | u16 | draw page segment A000 / A400 / A800 | 32 external sites (`mov es:[0A],…`) |
| 000C / 000E | i16 | clip x0 / x1 = 0 / 320 | never changed |
| 0010 / 0012 | i16 | clip y0 / y1 (0–C8h, letterbox 28h–A0h) | 3 + 5 external sites |
| 0014 | u8 | effect: 0 normal, 64h hit-flash (color 1), other = `pix & 44h`; for 01A7, the silhouette color | 8 external sites |
| 0018–002C | | scratch: row pointers, clip amounts L/R/T/B, plane mask | internal only |

Sprite descriptor: 11 bytes, 208 entries at 1682:2482.

| Offset | Field |
|---|---|
| +0 | `u16 w` |
| +2 | `u16 h` |
| +4 | `u16 packed` |
| +6 | `u8 flag` (decoded/loaded) |
| +7 | far pointer to w×h decoded pixels |

Sprites are decoded **per stage**, driven by the needed-flags table at DS:03C2. All 208 decoded would be 953 KB, which doesn't fit.

### 2.2 The ten routines

All are leaf **far cdecl** procs: arguments at `[bp+6…]`, caller pops, DS/ES/SI/DI preserved, no calls, no interrupts. They draw a pixel at a time into unchained VRAM, writing the Sequencer map mask (3C5h) per pixel as `11h rol (x&3)` (which plane), and advancing `di` when the mask wraps back to 11h. The row pitch is 80 bytes.

| Entry | Native name | Arguments | Behaviour |
|---|---|---|---|
| 002D | `blit_sprite` | x, y, desc far* | Centred at (x−w/2, y−h/2). Clipped to [0C..0E]×[10..12] with signed compares. Colour 0 transparent. Effect byte applies. |
| 01A7 | `blit_sprite_mono` | x, y, desc | As 002D, but every opaque pixel is drawn in colour [14] (silhouette / flash). |
| 030D | `blit_sprite_opaque` | x, y, desc | Top-left, unclipped, every pixel written. Used for backdrops (e.g. the 320×200 sprites). |
| 0390 | `blit_glyph16` | x, y, colour, glyph far* | 16×16 1bpp: Hangul jamo and composed syllables. Vertical clip only, transparent. |
| 0466 | `blit_glyph8x16` | x, y, colour \| opaque<<8, glyph | FOX.DAT 8x16 ASCII font. Vertical clip. A non-zero high byte gives a black background. |
| 056C | `blit_glyph8x8` | x, y, colour \| opaque<<8, glyph | 8x8 font, unclipped. |
| 061C | `blit_sprite_shrunk` | x, y, div, desc | Centred, 1/div size (`div r8`, so w/div ≤ 255), transparent, unclipped. Source stride is `(w & 0xFF) × div`, quirk preserved. |
| 070C | `blit_show_page` | — | CRTC 0Ch ← (page >> 8) << 4: A000/A400/A800 → start address 0000/4000/8000. Port from `[0040:0063]`. |
| 0721 | `blit_scroll_strip` | row, rows, width, buf_seg | VRAM↔VRAM **write mode 1** latch copies. It puts buf's left strip on the page's right edge and rotates buf left. GC mode is left in write mode 1, as the original does. |
| 07AE | `blit_scale_sprite` | dw, dh, desc, dst far* | Nearest-neighbour scale into linear memory (not VRAM). |

### 2.3 Decision: native reimplementation (done)

Transpiling would gain nothing here:
- These routines are hand-written assembly with no calls, a fixed ABI, and state that is just RAM.
- A transpiled version would emit a port write per pixel through the emulated Sequencer.

The native version in `src/blit_10a7.c` keeps the exact semantics: 16-bit wraparound, the original signed/unsigned compares (0390 uses `jl`, 0466 uses `jb`), `loop` with a count of 0 meaning 65536, and the per-pixel mask writes. That lets it be **verified byte-for-byte** against the original:

* `tests/diff_blit.py` runs the original 10A7 code from FOX_UNP2.EXE in unicorn, with an independent Python planar-VGA model (MMIO window plus IN/OUT hooks), alongside the C functions, from identical RAM and random VRAM.
  It uses real VSPRITE sprites and FOX.DAT fonts, random positions across every clip edge, all effect modes and all three pages.
  It compares the 4 planes, map mask, GC mode, CRTC 0Ch, and the destination buffer for 07AE.
* **2,000/2,000 cases pass** (seeds 1992 and 7). A mutant build (effect mask 44h changed to 45h) is caught, so the test is sensitive. `ctest` runs 500 cases.

Next step for speed, once everything is correct: a fast path that writes `vram[plane][off]` directly when GC is in its default state (write mode 0, bitmask FF, no ALU op), keeping the emulated path as the reference.

## 3. Runtime harness (`src/`)

| File | Role |
|---|---|
| `mem.h/.c` | `uint8_t ram[1 MB + 64 KB]`, `LIN()`, `REAL_PTR(seg,off)`, `SEG()` (image → loaded segment), `mem_rd/wr8/16` (A0000–AFFFF goes to VGA, C0000+ is ROM), `rd/wr16(seg,off)` with offset wrap, `FarPtr`. The BDA CRTC port (0040:0063 = 3D4h) is pre-set. |
| `cpu.h/.c` | `Cpu` register file (ax/al/ah unions, segment registers, flags). Emulated stack `push16/pop16`. `farg()` for far-proc arguments. Far-call registry `far_register/far_lookup/far_call` for natives and indirect calls. |
| `vga_modex.h/.c` | 4×64 KB planes, latches, write modes 0–3 (set/reset, rotate, ALU, bitmask), read modes 0/1. SEQ/GC/CRTC/ATTR/DAC. CR11 write protect. A 3DAh poll hook for frame pacing. Geometry comes from the CRTC (200 lines for Mode Y/13h, 240 for Mode X). Indexed and ARGB scan-out from the start address. `vga_fox_set_mode_y` replaces 04A4:00A2. |
| `io.h/.c` | Port dispatch: VGA, PIT (divisor → `pit_ch0_hz()`), PIC, an OPL 388h/389h hook, and an unhandled counter. |
| `mz_load.h/.c` | DOS-faithful MZ load: PSP with command tail (`" a v"` from FR.BAT), image, relocations, entry state, heap start. |
| `assets.h/.c` | Native FOX.DAT / VSPRITE.PAT loaders in the game's in-memory layout (descriptor table, per-stage `wanted` mask), plus a paragraph heap. |
| `blit_10a7.h/.c` | Native blitters and their `Cpu`-ABI adapters (registered at 10A7:xxxx). |
| `main_sdl.c` | SDL2 front-end: 4:3 aspect, 70 Hz pacing, SDL → XT scancode table. Currently runs a harness demo; `--screenshot` renders headless. |

Memory map at load segment 1000h, matching what FOX.EXE sees under DOS:

| Range | Contents |
|---|---|
| 0000:0000–03FF | IVT. Installs of INT 08/09 are routed to platform natives. |
| 0040:0063 | BDA CRTC base port (3D4h), read by 05BE8 and 10A7:070C. |
| 0FF0:0000 | PSP: command tail `a v`. |
| 1000:0000–2CF7:000F | Image (118,640 bytes, 2,414 relocations). Code segments are 1000 + image segment. |
| 2ADF:0000 | DGROUP (image 1ADF). Stack is SS 2D2B:0800. |
| 40CD:0000–9FFF:000F | Far heap (about 390 KB): per-stage decoded sprites, buffers. |
| A000 / A400 / A800 | VRAM pages 0 / 1 / 2 (plane offsets 0000 / 4000 / 8000). A800 is the scroll buffer used by 0721. |

Build:
```
cmake -S . -B build -DFOX_PYTHON=<python with unicorn>
cmake --build build
./build/foxrt
./build/foxrt --screenshot out.ppm
ctest --test-dir build
```

## 4. Module map (298 functions)

Generated by `tools/module_map.py` into `work/function_modules.csv`, with one row per function giving segment, offset, size, module, strategy and name.

| Module | Segments | Funcs | Bytes | Strategy |
|---|---|---|---|---|
| **Game loop / front-end** | 0000, 0003 | 15 | 12.5 K | Transpile. Main loop, title/options/difficulty, hi-score table (`score.dat`), credits driver, continue/quit prompts. |
| **Game logic / engine & math** | 05FC, 078E, 0A90, 0D24, 0D81, 0E39 | 44 | 33.2 K | Transpile. Player, enemies, weapons (SHADOW MISSILE, FIRE COBRA, …), collisions, motion tables (FOX.DAT records), stage set-up (`stage.dat`, img 0294E / E68F). |
| **Story / UI text** | 0EFF, 0FD8 | 9 | 6.8 K | Transpile. Intro/ending/credits sequencing, typewriter FD8:0960, captions FD8:0C75. |
| **System / video helpers** | 04A4 | 17 | 5.2 K | Transpile, with port I/O going to `vga_modex`. Palette fades, page clears, waits. `check_keyfiles` 04A4:14B0 is stubbed and `vga_set_mode_y` 04A4:00A2 is native. |
| **VGA / render** | 10A7 (+04A4:00A2) | 11 | 2.1 K | **Native, done and diff-tested.** |
| **File I/O / assets** | 116B | 5 | 3.7 K | Transpile on top of the CRT shims. FOX.DAT / VSPRITE.PAT / SCORE.DAT loaders and the Johab glyph composer 116B:091E. `assets.c` is the native fallback. |
| **Audio: music** | 030C, 1129, 1258, 1265, 1255 | 62 | 10.5 K | Transpile the sequencer and driver. 1255 (OPL write + delay through `[DS:218C]`) becomes native and feeds an OPL2 emulator. |
| **Audio: SFX** | 113E, 159E, 15E5 | 27 | 3.7 K | 113E (voice.dat offsets) is transpiled. The SB glue and **embedded CT-VOICE.DRV** are replaced natively with an SDL audio PCM mixer. |
| **Platform: timer** | 0E0C, 1337 | 12 | 0.9 K | Native. The INT 08 ISR 0E0C:016A is run by a host timer at the PIT rate (`pit_ch0_hz()`, 72.8 Hz). The music hook in 1337 is chained. `delay()` busy-waits become host sleeps. |
| **Platform: input** | 0C6A | 5 | 3.0 K | Native. The SDL → XT set-1 scancode queue feeds the INT 09 ISR logic (0C6A:0139). Key-state tables stay in RAM. |
| **C runtime** | 134D | 90 | 11.3 K | **Native shims**, not recompiled. Known entries: fopen 07A0, fread 07BC, fclose 06A6, fseek 1D7C, strlen 1D1C, farmalloc 19CC, exit 01DF, puts 0AC6, `_dos_findfirst` 229F, `_dos_setvect` 22D2. To identify: 2214 (46 calls, likely long-math helper), 2470/24CE (strcpy/strcat?), 2148, 2012, 2004, 2386, 2458, 1CE6, 1F98. |

## 5. Recompiler (`tools/recomp.py`, next phase)

Capstone-driven, one C function per recovered function:

1. **Control flow.** Basic blocks become C labels with `goto`. `loop`, `jcxz` and `rep` string ops are expanded. The 14 inline switch tables are read from the image, with bounds taken from the preceding `cmp bx,N / ja` guard, and emitted as `switch`.
2. **Registers and flags.** The `Cpu` struct is used directly. Flags are computed lazily: store the last ALU operands and result, and materialise CF/ZF/SF/OF only when a `jcc`, `adc`/`sbb` or `pushf` consumes them.
3. **Memory.** Every operand goes through `rd/wr8/16(seg, off)` with the active segment register, so the VGA window, ROM and wraparound are handled.
4. **Calls.**
   - Direct `lcall seg:off` becomes `far_call()`, or a direct C call when the target is static.
   - Near calls become direct C calls.
   - The 49 indirect calls/jumps (CRT function pointers, `lcall [mem]`) go through `far_lookup()`.
5. **Interrupts.** `int 21h` maps to CRT/DOS shims by AH (the list is in RECON.md). `int 10h` AX=0013h maps to `vga_set_mode13`.
6. **I/O.** `in`/`out` become `port_in*`/`port_out*`.
7. **Verification.** Each function is diff-tested exactly like `tests/diff_blit.py`: the unicorn reference and the C version start from the same RAM and registers and must produce the same RAM, registers and port trace. Hot loops can be swapped for hand-written C later.

## 6. Roadmap

| Phase | Deliverable | Exit criterion |
|---|---|---|
| 0 ✅ | Unpack, protection patch, full English text | FOX_EN_FINAL.EXE runs in DOSBox |
| 1 ✅ | Runtime harness, VGA Mode Y, native blitters | `ctest` diff test passes; demo renders through the real data paths |
| 2 ✅ | CRT shims: name all 134D entry points, DOS file I/O over host files (case-insensitive), farmalloc on the paragraph heap | Asset loaders from 116B, transpiled, load FOX.DAT / VSPRITE.PAT identically to `assets.c` |
| 3 ✅ (partly) | `recomp.py` v1: leaf functions, flags, memory, switch tables; per-function diff harness | 04A4 and 116B transpiled and passing diff tests |
| 4 ✅ | Platform: INT 08 / INT 09 natives, 72.8 Hz scheduler, 3DAh pacing hook | Title screen and menus run natively with keyboard |
| 5 ✅ | Story/UI and game loop transpiled | Intro (English) plays; menus work |
| 6 (partly) | Game logic transpiled | Stage 1 playable ✅, then all 6 stages |
| 7 (partly) | Audio: OPL2 emulator plus sequencer, CT-VOICE replacement (VOICE.DAT PCM) | Music and voice play ✅; not yet compared with DOSBox recordings |
| 8 | Polish: fast VGA path, scaling filters, config, CI builds for macOS/Linux/Windows | Release builds on all three |

## 7. Open questions and risks

* **Timing.** Gameplay speed is tied to the 72.8 Hz ISR counters and the 3DAh polls. Both have to be driven from one host scheduler so they don't drift.
* **Write-mode leftovers.** 0721 leaves GC in write mode 1. Something must restore write mode 0 before the next blit; the 04A4 helpers do, so call order has to be preserved.
* **Chain-4 addressing** uses the DOSBox-style linear model. That's harmless here because VRAM is cleared after unchaining, but it's documented in `vga_modex.c`.
* **OPL emulator licensing.** Nuked-OPL3 (chosen) is LGPL-2.1-or-later. It is vendored unmodified in `third_party/nuked-opl3` and built as its own library. Use `-DFOX_OPL_SHARED=ON` to ship it as a replaceable shared library if the project is distributed under a non-LGPL licence.

## 8. Phase 2 results

### Sound

**OPL.** Nuked-OPL3 v1.8 sits behind `src/audio.h`.
* Ports 388h/389h and the SB FM mirror 228h/229h go through a lock-free SPSC ring to the mixer (the SDL audio callback, or `audio_render()` when headless).
* OPL timer status is modelled well enough for AdLib detection.

**SFX lookup table.** **DS:0B3E is `u16 len[18]`**, and VOICE.DAT is 18 concatenated VOC files. The 113E API (all transpiled):

| Entry | Function |
|---|---|
| 0002 | `voice_init` |
| 0056 | `voice_stop` |
| 007B | `voice_play_bank(n)`: sub-clip at `sum(len[8..n-1])` |
| 010D | `voice_play_loaded` |
| 0178 | `voice_load(n)`: n < 8 seeks to `sum(len[0..n-1])`; n ≥ 8 loads the bank at 21566h |

**CT-VOICE.** `src/ctvoice.c` replaces the driver natively at 15E5:0000, using the original register ABI (BX = function).
* Function 6 walks the VOC block chain (types 1/2/3/6/7) and queues the PCM to the mixer.
* The status word (DS:10EE, set by function 5) reads FFFFh while a clip plays and 0 once it ends.
* The hardware probes 159E:01F4, 00E8 and 036C are native too, reporting SB 220h IRQ 7 plus AdLib. The 159E wrappers are transpiled (`gen/recomp_sb_glue.c`).

### CRT shims

`src/crt_shims.c` replaces the **33 entry points the game calls**. The other ~57 CRT functions are internals that become unreachable.
* **Strings and memory** map to C semantics over emulated memory, with 16-bit offset wrap. `_fmemmove` uses linear-address overlap handling, as the original does.
* **`sprintf`** reads its varargs from the emulated stack.
* **`rand`** is the exact MSC LCG, with its state at DS:1518.
* **Long-math helpers** pop their own arguments (`retf 8`) or use the register ABI, as in the original.
* **Heap**: `_fmalloc/_ffree/_fmsize/_dos_allocmem` run on `heap.c`.
* **stdio**: `FILE*` values are real far pointers into an emulated MSC `_iobuf` table, so `_flag` is visible to the game.
* **Interrupts**: `_dos_set/getvect`, `_chain_intr` and `_disable/_enable` hook into the platform layer.

`src/dos.c` resolves DOS paths against the game directory case-insensitively. Drive letters and leading `\` are stripped, so `\EV22.SYS` resolves to `fox-ranger/EV22.SYS`. It also handles INT 21h file, vector, memory and exit services and INT 10h mode set.

### Transpiler

`tools/recomp.py` emits C that calls `src/recomp_rt.h`:
* control flow is discovered by recursive descent;
* each function becomes a `goto`-labelled body;
* flags live in a local struct and are synced with `Cpu` only at calls and returns;
* the 9 inline switch tables in these modules become C `switch` statements;
* relocated immediates become `SEG()`;
* every call goes through `rt_call_near/far`.

That gives 73 functions and 15,870 instructions, all translated with no unsupported instructions:
* game loop: 14
* game logic: 44
* SFX: 5
* SB glue: 10

The code is goto-style rather than structured: it is literal and verifiable, and structuring (if/while recovery) is a later readability pass that can be checked against the same tests.

### Verification (`ctest`)

| Test | What it checks | Result |
|---|---|---|
| `diff_fn_game` | 63 transpiled functions vs the x86 originals in unicorn. Random state, all calls stubbed on both sides. Compares registers, 1 MB RAM, VGA, call trace and OUT trace. | 0 failures |
| `diff_fn.py --trace` | 6 non-terminating loops (`main` 0000:0175 and menu/attract loops waiting for input) | identical instruction and register traces for 137k–150k steps |
| `diff_crt_shims` | the 14 pure CRT shims vs the original MSC code, incl. the game's `sprintf` formats | 600/600 |
| `audio_sfx_opl` | transpiled `voice_init → voice_load(8) → voice_play_bank(10)` through to rendered PCM | sample-exact vs VOICE.DAT; status word FFFFh → 0; AdLib detection; OPL tone 310.0 Hz (expected 309.5) |
| `diff_blit_10a7` | native blitters | 500/500 |

## 9. Phases 3–4 results: playable native build

`./build/foxrt` boots `work/FOX_EN_FINAL.EXE` through the English intro to the title screen with AdLib music, opens the START/OPTION menu and plays Stage 1 with the keyboard.
* **Controls:** SPACE skips the intro, ENTER opens the menu and starts the game, the arrow keys move.
* **Headless:** `--headless --seconds S --keys "T:KEY,..." --shots "T:f.ppm,..." --wav f.wav` runs on a deterministic virtual clock.

### Code coverage

**All 184 non-CRT, non-native functions are transpiled:** 26,507 instructions and 14 switch tables.
* The tables are the 13 inline `jmp cs:[bx+T]` tables plus one computed jump. 1258:0031 is a Duff's-device multiply whose targets are listed in `recomp.py:COMPUTED_JMP`.
* That includes the ISRs (0C6A:0139, 0E0C:016A and the music multiplexer 1337:00D3), the music sequencer (030C) and the OPL driver (1129, 1255, 1258, 1265).

**Natives:**
* `04A4:00A2` sets the video mode.
* `04A4:14B0` replaces the copy-protection check (always passes).
* Also native: the 10A7 blitters, CT-VOICE plus the SB probes, the 33 CRT entry points, and the BIOS INT 08h/09h stand-ins at F000.

### Start-up (`src/main_sdl.c`)

The work of the MSC start-up is done natively, then `main(argc, argv, envp)` (0000:0175) is called with `" a v"` as in FR.BAT:
* SS = DS = DGROUP, SP = 2CBEh;
* the BSS at DS:2174–24BF is zeroed;
* argv is built on the heap.

The load segment is 0800h.

### Interrupts (`src/platform.c`)

**Delivery** happens on the game thread at loop back-edges. In production builds `RT_BACKEDGE` calls `rt_poll` every 1024 back-edges.
* Delivery goes through the IVT in `ram[]`, and honours IF, the PIC mask and a nesting limit.
* ISRs run inside `rt_isr_call` unwind contexts, so the CRT's `_chain_intr` (134D:2266) restores the interrupt frame, runs the previous handler and abandons the ISR, as in the original.

**Chain:** IRQ0 → 1337 music multiplexer → (accumulator ≥ 4000h) → 0E0C game ISR → (every 4th tick) BIOS.
* The PIT is a fixed-timestep accumulator at the programmed divisor. The game sets 4000h (72.82 Hz), and the music driver runs it at up to about 1.4 kHz.
* The multiplexer *resets* its accumulator instead of subtracting, so at divisor 6822 the game ISR runs at 58.3 Hz. That is the original behaviour, reproduced faithfully.

**Keyboard:** SDL scancodes become an XT set-1 queue. Each code goes to port 60h, followed by IRQ1.

### Video

* **Retrace** is a pure function of time (70.086 Hz, 1.43 ms blanking). Deriving it from presentation bookkeeping made each vsync wait miss the retrace window.
* **Presentation** never paces the game: frames are capped at the display refresh rate, and after a slow present (Metal `nextDrawable` back-pressure) the next ones are skipped.

### Idle

A poll with no CPU memory writes since the previous one is a busy-wait:
* **Interactive:** the platform sleeps until the next event (timer tick, retrace start or end, scripted event).
* **Headless:** virtual time jumps to that event, so 60 s of game run in about 2 s of wall time.

### Verification (all in `ctest`, 5/5 passing)

| Test | Coverage |
|---|---|
| `diff_fn_game` | 173 transpiled functions vs x86 in unicorn, all modules. The unicorn side emulates INT 21h 25/35 and INT 10h like `dos.c`, plus the OPL status model. Stub patch bytes are mirrored into the C side's RAM. Function-pointer calls and computed jumps on out-of-domain random state count as inconclusive. |
| additional checks | 1258:000E constrained (`--poke 1ADF:2496=1..12`): 40/40. The non-terminating loops match instruction traces for 140–150k steps. |
| `boot_title_stage1` | Deterministic headless run: title (25 s) and Stage 1 gameplay (41 s) bit-exact against `tests/ref/`, music RMS > 200 |
| `audio_sfx_opl`, `diff_crt_shims`, `diff_blit_10a7` | as in phase 2 |

### Known gaps / next

* **CPU use while idle:** idle waits shorter than 1.5 ms spin, because they aren't slept.
* **Window frame rate:** perceived smoothness was not checked by eye, and `--shots` captures the emulated frame, not the window.
* **PC-speaker sound** (port 61h / PIT channel 2), used by the `internal` music option, is not emulated.
* **Not yet exercised:** stages 2–6, high-score entry (`score.dat` writes) and the ending.
* **Code shape:** the generated C is goto-style, and structuring is a later readability pass.
