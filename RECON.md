# Fox Ranger (1992, Soft Action) — reconnaissance report

Offsets: **file** = byte offset in the file named; **img** = offset in the MZ load image
(`work/FOX_UNP2.EXE` file offset − 0x25E0); **DS:** = offset in DGROUP (segment 0x1ADF,
file offset of DS:0000 in `FOX_UNP2.EXE` = 0x1D3D0).

## 1. Binary state

| File | What it is |
|---|---|
| `FOX.EXE` (48,193 B) | The game. **Double packed**: PKLITE 1.12 (+extra +large flags, CS:IP FFF0:0100) wrapping **Microsoft EXEPACK**. |
| `CALL-TNW.EXE`, `TNW.1ST`, `DREAM.ON!`, `DISCLAIM.NTO` | BBS release add-ons (Turbo Pascal ad, ANSI art), **not** game files. |
| `FR.BAT` | `fox a v` → AdLib music + Sound Blaster voice. |

Unpacked with `tools/unpack_exe.py`, which emulates each stub under unicorn at two load segments and diffs the dumps to recover relocations:

| Layer | Output | OEP | SS:SP | Image | Relocs |
|---|---|---|---|---|---|
| PKLITE → | `work/FOX_UNP.EXE` | 1802:0010 (EXEPACK stub) | 1E3A:0080 | 121,904 B | 0 |
| EXEPACK → | `work/FOX_UNP2.EXE` | **134D:001A** (MSC 6 startup) | 1D2B:0800 | 118,640 B | **2,414** |

The program was built with Microsoft C 6 ("MS Run-Time Library 1990"), large model, using far calls. The CRT is in segment 134D and DGROUP is 0x1ADF. About 25 code segments and roughly 300 functions are found (`work/functions.csv`).
The rebuilt EXE is structurally sound (correct CRT entry, consistent relocations) but **not yet run-tested in DOSBox**.

## 2. Hardware / OS footprint (the SDL2 shim surface)

Full log: `work/triage_FOX_UNP2.txt`.

* **Video**: INT 10h AH=00 **mode 13h**, then unchained to **Mode Y**: SEQ 04h chain-4 off, CRTC 14h/17h byte mode, map mask through 3C4h/3C5h, read map through 3CEh/3CFh.
  That gives 4 planes, 256 KB VRAM, and page flipping. Vsync is polled on `[0040:0063]+6` (= 3DAh).
  The palette is loaded from FOX.DAT; DAC writes go through a DX variable.
  **Shim**: emulate 4-plane VRAM, the map-mask/read-map registers, CRTC start address and DAC, then present 320x200 as an SDL texture.
* **Timer**: own **INT 08h ISR at 0E0C:016A**; PIT ch0 is reprogrammed with divisor 0x4000 (about **72.8 Hz**) and chains to the BIOS.
  **Shim**: a 72.8 Hz tick calling the recompiled ISR.
* **Keyboard**: own **INT 09h ISR at 0C6A:0139** (installed at img C7A2, restored at C7CB). No INT 16h is used.
  **Shim**: SDL scancodes → XT set-1 scancodes fed to the ISR.
* **Mouse**: none. INT 33h is not used.
* **Sound Blaster**: the base port is held in **DS:10F0** (default 220h).
  It touches DSP reset/read/write/status (+6/+0A/+0C/+0E), FM (+8/+9), 8237 DMA ch1 (ports 02/03/0A/0B/0C/83h) and PIC 21h/20h for the IRQ.
  An embedded Creative **CT-VOICE driver** sits in segment 15E5 and is called through far pointers.
  **Shim**: high-level replacement that plays 8-bit PCM from VOICE.DAT; don't emulate the DSP.
* **AdLib/OPL2**: register writes through the port variable **DS:218C** (388h, seeded from DS:0C5A), with a 35×-read delay loop at img 1255C.
  **Shim**: OPL emulator (Nuked-OPL3/DOSBox OPL) on writes to that variable's port.
* **Music variants** supported by the code: `internal.dat` (PC speaker), `adlib.dat`, `mt32.dat`, `sc55.dat` (MIDI). **Only adlib.dat ships.**
* **DOS (INT 21h)**: 1A/2F (DTA), 25/35 (vectors), 30, 3C/3D/3E/3F/40/41/42/43/44 (file I/O), 48/4A (memory), 4C. Mostly through the MSC CRT (fopen/fread/fseek), with **no EXEC (4Bh) and no overlays**.
* **Recompiler hazards**:
  * 49 indirect call/jmp sites: CRT function pointers, plus `jmp cs:[bx+…]` switch tables at img 034D, 0425F, 06205, 06E15, 0B50C, 0D7A1, 144DF.
  * 86 **writes to CS: memory**: sprite blitters in segments 10A7–1129 keep clip variables in their code segment, so code segments must be writable.
  * Code that is **only reached through ISRs/IRQ vectors** (the timer ISR, the SB IRQ handler around img 15BA4, DMA setup around img 160D0). Pass these to the triage with `--seed`.
  * `lcall 0000:0000` at img 11C9E is a relocated call into segment 0.
* **Copy protection** (disassembled in phase 1; an earlier guess placed it at img 4829, which is actually the music-bank loader):
  `check_keyfiles()` at **img 05EF0 (04A4:14B0)** runs `_dos_findfirst` on `\EV22.SYS`, falling back to `EV22.SYS`, and requires a size of 5100. It then does the same for `EV21.SYS` with size 512. **File contents are never read.** It returns AL=1 on failure.
  The caller at img 04A57 branches at **img 04A62 `jne 4A67`** to `puts("\aUnauthorized user…")` + `exit(1)`.
  `tools/patch_protection.py` changes the branch to `jmp 4A83` and stubs the checker to `xor al,al; retf`.

## 3. Asset layout

Everything is read with stdio and there are no overlays. Two files are read per stage: STAGE.DAT is seeked to the stage's record, and VSPRITE.PAT sprites are decoded into the far heap per stage according to the needed-flags table at DS:03C2 (all 208 decoded would be 953 KB).

| File | Format |
|---|---|
| **FOX.DAT** (13,180 B) | Sequential, no header (loader at img 11700–119D0): `0x000` VGA palette (768 B, 6-bit) · `0x300` 4 records `{u8 n; u8 a[2·(n&7F)]; u8 b[2·(n&7F)]}` (motion/curve tables) · `0x834` **8x8 ASCII font** (95 glyphs, 0x20–0x7E) · `0xB2C` **8x16 ASCII font** (95) · `0x111C` **Hangul initials (cho)** 8 sets × 19 × 32 B · `0x241C` **vowels (jung)** 2 × 21 × 32 B · `0x295C` **finals (jong)** 3 × 27 × 32 B. Totals verified to the byte. |
| **VSPRITE.PAT** (511,034 B) | 208 sprites. **Directory at EOF**: 208 × `{u16 w, u16 h, u16 packed}` (`fseek(-0x4E0, SEEK_END)`). Data is sequential from offset 0. `packed==0` means raw w·h bytes; otherwise **RLE**: `c&0x80` → repeat next byte `c&0x7F` times, else copy `c` literals. Truncate to w·h because the encoder pads by 1–12 bytes. 8-bit pixels use the FOX.DAT palette. |
| **STAGE.DAT** (24,576 B) | 6 × 4096-byte stage records, `fseek(stage<<12)` + `fread(0x1000)` into 1682:1444. Probably enemy-wave scripts of 16-bit cells. |
| **VOICE.DAT** (198,990 B) | **18 concatenated VOC 1.10 files** (8000 Hz, 8-bit unsigned PCM). Lengths come from the `u16 len[18]` table at DS:0B3E. Clips 0–7 are loaded one at a time by `voice_load(n)`. Clips 8–17 form a resident 62,440-byte bank loaded by `voice_load(8)`, with sub-clips played by `voice_play_bank(n)`. `dump_assets.py` exports them as WAV files. |
| **ADLIB.DAT** (193,021 B) | AdLib music data (custom, not yet decoded). |
| **SCORE.DAT** (85 B) | High-score table: 5 × 17 B records (score + initials). |
| **EV21.SYS / EV22.SYS** | Copy-protection key files (entropy 7.5–7.95). |

## 4. Text, fonts, localization scope

* **No Korean text in any data file.** Every Johab/Wansung hit in the .DAT/.PAT/.SYS files is noise from compressed or graphics data.
* **All Korean text is raw, uncompressed Johab (KSSM) in DGROUP of the unpacked EXE**, mixed with ASCII (`VEXER`, `NF43`, `FOX RANGER`). There are 19 strings (`work/assets/script_johab.json` has offsets and byte budgets):
  * **Ending** (3 strings) at file 0x1ED22–0x1EE59, through a far-pointer table at **DS:029C** (file 0x1D66C).
  * **Intro story / cutscene** (13 strings) at file 0x1EE84–0x1F33B, through a far-pointer table at **DS:0B0A** (file 0x1DEDA).
  * **Credits dedication** (3 lines) at file 0x1DDDE / 0x1DE08 / 0x1DE32, inside the credits table: fixed `char[42]` slots from file 0x1D6FA.
* Story strings are only reached through those far-pointer tables (offset + relocated segment 1ADF). The whole ~1.5 KB story block can therefore be **repacked freely** and the table offsets rewritten. No code patches are needed unless English exceeds the block.
* **Already English**: menus, HUD, weapons, staff names and errors (`Shield`, `Music Test`, `Exit OPTION`, `Game Over`, `Quit (Y/N)`, `STAGE CLEAR`, `Continue? (Y/N)`, `Warning !!!`, `SHADOW MISSILE`…), at file 0x1D420–0x1EAB0.
* **Font**: a Johab 8×4×4-style "bul" font in FOX.DAT, with an 8-set cho, 2-set jung and 3-set jong variant. It is composed at runtime by the routine around img 121E0 (5-5-5 field split, set picked by vowel class).
  ASCII bytes inside the same strings render with the FOX.DAT ASCII fonts, so **English needs no new font**. It only needs to fit the line layout, which is not yet checked (line width per render call).

### Text renderer (verified in phase 1)

* `FD8:0960 typewriter(x0, y, color, char far *s)` at img 106E0:
  * **lead byte > 0x7F**: Johab pair. Wrap if `x ≥ 0x138 − x0`, draw 16×16, `x += 16`.
  * **otherwise**: ASCII. Wrap if `x ≥ 0x140 − x0`, draw 8×16, `x += 8`.
  * Wrap is `y += 16; x = x0` for both, so mixing ASCII and Hangul keeps the same line pitch.
  * It wraps **per character** (no word wrap, no `\n`) and waits 200 units per character, returning 1 if a key skips it.
* Glyph routine `116B:091E` at img 11FCE:
  * ASCII glyph = `FOX.DAT@0xB2C + (c − 0x20) × 16`, the italic 8x16 font, blitted by `10A7:0466`. Only 0x20–0x7E are valid (no bounds check).
  * Hangul is composed from the cho/jung/jong sets around img 121E0.
* Intro player (img FF00–106DA): entries 0 and 2 are centred with `x0 = (40 − strlen) × 4`. All others use x0 = 16, giving **36 columns**, at y = 0x5C, 0x64 or 0x6F.
  Entries 10–12 go through `FD8:0C75(pic, s)`, which first draws sprite A3/A4/A5 at y = 0x2F.
  Each table entry is loaded directly (`push [0B0C]; push [0B0A]` …), so rewriting the table repoints the text.

## Tools

* `tools/unpack_exe.py IN OUT`: emulation unpacker (needs `unicorn`). Run it twice on FOX.EXE.
* `tools/triage_recomp.py [--code-end 0x1adf0] [--seed SEG:OFF] [--csv f.csv] FILES…`: entropy and packer detection, recursive-descent INT/port/ISR/indirect-call scan, function table (needs `capstone`).
* `tools/dump_assets.py GAME_DIR OUT [--exe FOX_UNP2.EXE]`: FOX.DAT fonts/palette to PNG, VSPRITE.PAT to 208 PNGs, STAGE split, VOC blocks, Johab script to JSON (stdlib only).
* `tools/kscan.py` (Johab/Wansung run scanner) and `tools/dis.py IMG START LEN` (quick disassembly with CRT names).
* `tools/patch_protection.py IN OUT` disables the key-file check. It verifies the original bytes and is idempotent.
* `tools/inject_translation.py IN OUT [--intro F] [--ending F] [--credits F] [--fox-dat FOX.DAT --preview DIR]`: lays out English exactly as the game does, padding lines to the box width (36 cols at x0=16, 40 at x0=0, centred ≤ 40) to get word wrap.
  It checks charset, line counts and screen bottom, then packs intro and ending strings into one pool made of both original blocks (DS:1952–1A8F and DS:1AB4–1F6C, 1527 B) and rewrites both pointer tables.
  Credits slots `{char text[41]; u8 color}` at DS:02AC are filled up to 40 chars, keeping the color byte.
* `tools/build_en.sh` runs the full build to `work/FOX_EN_FINAL.EXE` (no Korean text left; 1527-byte pool 1472 B used).

### Ending / credits layout (phase 2)
* Ending table DS:029C (4 entries):
  * 0 is centred at y=0x6F.
  * 1 and 2 are at x0=0, y=0x60, giving **40 columns**.
  * 3 is "? ? ?", centred at y=0x5C.
* Credits scroller at img FB67 draws slot *i* centred with `x=(40−strlen)×4` through glyph routine 116B:091E, with no wrap, rows 30 px apart, and color from byte +0x29. The dedication is slots 45–47 (color 1F).
