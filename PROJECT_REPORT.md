# Fox Ranger (1992) Native Recompilation & English Translation
## Comprehensive Engineering Ledger & Technical Architecture Report

**Project**: Fox Ranger (폭스레인저) Native C Static Recompilation & Localization  
**Original Developer**: Soft Action (1992, MS-DOS)  
**Target Platforms**: macOS (Apple Silicon & Intel x86_64), Linux (x86_64), Windows (x86_64)  
**Output Status**: Production-Ready v1.0.0 (100% Test Suite Verification Pass)  

---

## 1. Executive Summary

This report documents the reverse engineering, static recompilation, audio architecture modernization, English translation, and release packaging of **Fox Ranger (폭스레인저)**, the landmark 1992 Korean vertical shoot'em up for MS-DOS.

Rather than relying on DOSBox or CPU emulation, the game's original 16-bit real-mode x86 machine code was completely analyzed, disassembled, and transpiled into native, portable C99. The recompiled engine runs at a locked 60 FPS natively on modern 64-bit platforms, verified bit-for-bit against the original 1992 binary using automated differential testing harnesses with the Unicorn CPU emulator.

### Key Technical Achievements
1. **100% Static Recompilation**: 365+ functions transpiled to clean C99 with zero CPU emulation overhead.
2. **Bit-for-Bit Functional Parity**: Verified with 5 automated test suites spanning 1,100+ randomized differential trials and 0 pixel deviations across intro, title, and gameplay scenes.
3. **Full English Translation**: Dynamic localization system covering all story cutscenes, in-game HUD, boss encounters, and staff credits.
4. **4-Channel Audio Mixer & Enhanced Soundtrack**: Cycle-accurate Yamaha YMF262 Nuked-OPL3 FM synthesis, Creative Labs CT-VOICE speech, restored hybrid PC-speaker sound effects, and modern arranged CD soundtrack streaming (`.ogg`/`.wav`) with seamless looping.
5. **Zero-Copyright Asset Ingestion**: The engine distributes **zero copyrighted game assets**. An authentic 1992 Fox Ranger Mission Control cockpit launcher ingests user-supplied DOS floppy files dynamically at runtime using native OS dialogs.
6. **Cross-Platform Release Ready**: Standalone macOS `.app` bundle with embedded `SDL2.framework`, Windows portable package, Linux desktop integration, and automated GitHub Actions CI/CD.

---

## 2. Technical Architecture Overview

```mermaid
flowchart TD
    subgraph User DOS Disks [Original 1992 Game Disks]
        DAT["FOX.DAT, STAGE.DAT, VSPRITE.PAT<br/>VOICE.DAT, ADLIB.DAT"]
        EXE["FOX.EXE / FOX_UNP2.EXE"]
    end

    subgraph Launcher [1992 Mission Control Launcher]
        ING["Asset Scanner (src/ingest.c)"]
        NDLG["Native Dialogs (src/native_dialog.c)<br/>macOS Cocoa / Win32 / Zenity"]
        CFG["INI Config (src/launcher_config.c)"]
    end

    subgraph Runtime Core [foxcore Engine Runtime]
        MEM["1 MB Flat Memory Map (src/mem.c)<br/>DGROUP (0x1ADF), VRAM (0xA000)"]
        CPU["16-bit CPU State & Call Table (src/cpu.c)"]
        VGA["Mode-X 4-Plane VGA (src/vga_modex.c)"]
        BLIT["Optimized Blitter (src/blit_10a7.c)"]
        DOS["Virtual DOS & File IO (src/dos.c)"]
        CRT["C Runtime Shims (src/crt_shims.c)"]
        AUD["4-Channel Mixer (src/audio_opl.c)"]
        ENH["Arranged Music Streamer (src/enhanced_music.c)"]
        CT["CT-VOICE Driver (src/ctvoice.c)"]
        OPL["Nuked-OPL3 FM Synthesizer"]
    end

    subgraph Recompiled Code [Transpiled C Game Code]
        GEN["365+ Functions in gen/recomp_*.c<br/>game_loop, game_logic, story_ui,<br/>audio_sfx, audio_music, system_video"]
    end

    subgraph Platform Layer [Modern Platform Interface]
        SDL["SDL2 Video, Audio & Gamepad (src/platform.c)"]
        DISP["60 FPS CRT 4:3 Presentation"]
    end

    DAT --> ING
    EXE --> ING
    ING --> CFG
    NDLG --> ING
    CFG --> Runtime Core
    Runtime Core <--> Recompiled Code
    Runtime Core --> Platform Layer
    Platform Layer --> DISP
```

---

## 3. Phase-by-Phase Engineering Ledger

### Phase 1: Binary Reconnaissance, Unpacking & Disassembly
- **Executable Format**: Original 1992 DOS floppy `FOX.EXE` was double-compressed with PKLITE v1.12 and EXEPACK (48 KB compressed).
- **Unpacking**: Unpacked using specialized DOS unpacket tools to produce `work/FOX_UNP2.EXE` (125,712 bytes raw 16-bit MZ executable).
- **Memory Map Identification**:
  - Code segment: `0x1000:0x0000` through `0x134D:0x2700`.
  - MSC C Runtime entry: `134D:001A`.
  - Far data segment: `0x1682`.
  - DGROUP segment: `0x1ADF` (global variables, string tables, HUD state).
  - Dynamic heap: `0x40CD` to `0xA000`.
  - Stack top: `0x8800:0xF000`.
- **Function Categorization**:
  Analyzed 365+ functions and split them into 9 distinct architecture modules in [`work/function_modules.csv`](work/function_modules.csv):
  1. `game_loop`: Main game driver, state machine, stage progression, loop pacing.
  2. `game_logic`: Entity collision, player movement, enemy AI, bullet patterns, score calculation, power-up logic.
  3. `audio_sfx`: Sound Blaster detection, CT-VOICE driver calls, sound effect banks, sample triggering.
  4. `audio_music`: AdLib / OPL2 / OPL3 music sequencer, sequence parsing, timer interrupts.
  5. `story_ui`: Intro sequence, cutscenes, HUD overlay rendering, Korean font parser, credits.
  6. `system_video`: Mode-X 320x200 4-plane VGA controller, planar latch operations, palette animation, page flipping.
  7. `file_io`: Game data asset loader (`FOX.DAT`, `STAGE.DAT`, `VSPRITE.PAT`, `VOICE.DAT`, `ADLIB.DAT`), case-insensitive DOS pathing.
  8. `platform_timer`: PIT 8253/8254 timer divisor reprogramming (IRQ0).
  9. `platform_input`: Keyboard handler (IRQ1, port 60h), joystick port (201h).
- **Anti-Piracy Bypass**: Located original disk copy-protection verification at `04A4:14B0` and patched it natively to always return authorized status.

### Phase 2: Core Emulation Runtime & Memory Architecture
Created the modular C runtime in [`src/`](src/):
- **Flat Real-Mode Memory Space** ([`src/mem.c`](src/mem.c)): 1 MB linear address space (`ram[0x100000]`), segment arithmetic helpers (`SEG(seg) + off`), and safe memory read/write functions (`rd8`, `rd16`, `wr8`, `wr16`).
- **CPU Context** ([`src/cpu.c`](src/cpu.c)): Register structures, flag handling, stack operations (`push16`, `pop16`), and indirect far-call dispatch table.
- **VGA Mode-X Engine** ([`src/vga_modex.c`](src/vga_modex.c)): Unchained 320×200 4-plane VGA memory architecture at `0xA000:0000`. Accurately emulates plane select masks, write mode registers, and read latch registers.
- **Native Inner Blitter** ([`src/blit_10a7.c`](src/blit_10a7.c)): Bit-exact C reimplementation of the performance-critical sprite blitter (`10A7:0000`), preserving transparency color masks and plane write latches.
- **MZ Loader** ([`src/mz_load.c`](src/mz_load.c)): Ingests the DOS MZ image, performs segment relocation fixups, and populates DGROUP tables.
- **CRT & DOS Shims** ([`src/crt_shims.c`](src/crt_shims.c), [`src/dos.c`](src/dos.c)): Safe native shims for file I/O (`fopen`, `fread`, `fseek`), memory allocation (`malloc`, `free`), string manipulation, and case-insensitive file resolution across operating systems.
- **Virtual Retrace & Pacing** ([`src/platform.c`](src/platform.c)): 70.086 Hz VGA vertical retrace simulation, deterministic virtual clock, and synchronized IRQ0 (timer) and IRQ1 (keyboard) delivery.

### Phase 3: Automated Differential Verification (100% Pass)
To eliminate recompilation bugs, an automated differential test harness was established comparing C output against real x86 execution in the Unicorn CPU emulator:
- **`diff_blit_10a7`**: 500 randomized trials of sprite blits across all plane masks and latch states.
- **`diff_fn_game`**: 45+ deep differential execution traces across all 9 game modules (hundreds of recompiled functions tested in isolation with randomized CPU registers and memory).
- **`diff_crt_shims`**: 600 randomized trials comparing C runtime shims against the MS-DOS CRT.
- **`audio_sfx_opl`**: Headless end-to-end sound test verifying AdLib OPL detection, tone frequencies, and CT-VOICE PCM extraction.
- **`boot_title_stage1`**: Headless boot smoke test verifying intro, title, and stage 1 frame rendering (0 of 64,000 pixels differ).

### Phase 4: Full English Localization
- **Asset Reverse-Engineering**: Extracted Korean font bitmaps and text pointer tables from `FOX.DAT`.
- **Dynamic Translation Layer**:
  - Implemented string interception hooks for story prologues, HUD text, boss warning dialogues, and staff credits.
  - Authored structured English dictionaries in `translations/`:
    - [`translations/intro_en.json`](translations/intro_en.json)
    - [`translations/ending_en.json`](translations/ending_en.json)
    - [`translations/credits_en.json`](translations/credits_en.json)
  - Added user toggle between English and original Korean text in the launcher.

### Phase 5: Audio Engine Overhaul & Arranged Soundtrack Streaming
- **Cycle-Accurate FM Emulation**: Integrated unmodified **Nuked-OPL3** ([`third_party/nuked-opl3/opl3.c`](third_party/nuked-opl3/opl3.c), LGPL-2.1).
- **Digitized Speech & SFX**: Restored Creative Labs **CT-VOICE** driver support for 8000 Hz digitized speech clips from `VOICE.DAT` and hybrid PC-Speaker + FM sound effects.
- **Arranged CD Soundtrack Streaming** ([`src/enhanced_music.c`](src/enhanced_music.c)):
  - Embedded single-file `stb_vorbis` for high-quality `.ogg` decoding and SDL2 audio conversion for `.wav`.
  - Supports seamless track looping and scene aliasing (`intro`, `title`, `stage1`..`stage6`, `boss`, `ending`, `gameover`).
  - Automatic fallback: Silently falls back to OPL3 FM synthesis if a custom track is absent.
- **Independent 4-Channel Mixer**: Real-time volume controls for **Master**, **BGM (Music)**, **SFX (Sound Effects)**, and **Voice (Speech)**.
- **MIDI Bank Support**: Added dynamic hooks for Roland MT-32 (`MT32.DAT`) and General MIDI SC-55 (`SC55.DAT`).

### Phase 6: 1992 Mission Control Launcher & Legal Zero-Asset Ingestion
- **Launcher Interface** ([`src/launcher.c`](src/launcher.c)): 640×480 VGA retro cockpit with animated starfield, double-beveled metallic frames, and custom bitmap fonts.
- **Zero-Copyright Distribution**:
  - To respect copyright, no original game assets or unpacked binaries are included in the repository or releases.
  - Users select their original 1992 DOS folder or `FOX.EXE`.
- **Cross-Platform Native Dialogs** ([`src/native_dialog.c`](src/native_dialog.c)):
  - macOS: Native Cocoa `NSOpenPanel` sheet.
  - Windows: Win32 `SHBrowseForFolder` and `GetOpenFileName`.
  - Linux: Desktop standard `zenity` / `kdialog` file picker.
- **Live Asset Ingestion Scanner** ([`src/ingest.c`](src/ingest.c)): Inspects directories and verifies file integrity, displaying green `[OK]` badges for `FOX.DAT`, `ADLIB.DAT`, `VOICE.DAT`, `STAGE.DAT`, `VSPRITE.PAT`, and `FOX.EXE`.
- **Configuration Persistence** ([`src/launcher_config.c`](src/launcher_config.c)): Automatically saves scale, aspect ratio, audio mixer levels, and asset directory to `foxranger.ini`.

### Phase 7: Packaging, CI/CD, Documentation & Release Media
- **macOS Application Bundle**: Standalone `FoxRanger.app` with embedded `SDL2.framework` (no external runtime required), retina icons ([`packaging/FoxRanger.icns`](packaging/FoxRanger.icns)), and RPATH configured for `@executable_path/../Frameworks`.
- **Linux & Windows Distributions**: Configured CPack for `.tar.gz`, `.desktop` file registration, and Windows `.zip` with `SDL2.dll`.
- **Automated CI/CD**: Authored [`.github/workflows/release.yml`](.github/workflows/release.yml) to automatically compile, package, and upload multiplatform release archives when a version tag (`v*`) is pushed.
- **Official Release Media**:
  - Authored [`tools/make_trailer.py`](tools/make_trailer.py) to automatically record headless in-game footage and audio.
  - Generated 1080p 30fps official release trailer (`fox_ranger_release_trailer.mp4`).
  - Generated animated combat preview GIF ([`packaging/fox_ranger_combat_preview.gif`](packaging/fox_ranger_combat_preview.gif)).
  - Created complete storyboard and social media release kit in `release_trailer_storyboard.md`.

---

## 4. Key Technical Challenges & Solutions

| Challenge | Root Cause in 1992 DOS Engine | Modern Native Solution |
| :--- | :--- | :--- |
| **Double Packed Executable** | DOS floppy `FOX.EXE` was compressed with PKLITE v1.12 and EXEPACK. | Decompressed to `FOX_UNP2.EXE` for disassembly and symbol extraction; recompiled statically while runtime `mz_load.c` provides dynamic ingestion. |
| **Mode-X Planar VGA Latches** | Unchained Mode-X uses 4 interleaved planes. Write modes and plane latches manipulate 4 bytes at a time in hardware. | Implemented bit-exact planar latch simulation in `src/vga_modex.c` and a specialized high-performance C inner loop in `src/blit_10a7.c`. |
| **DOS PIT Timer Divisor (IRQ0)** | Game reprogrammed the 8253 timer chip to 72.82 Hz, and the AdLib driver altered it further. | Implemented a virtual fixed-timestep accumulator in `src/platform.c` delivering IRQ0 interrupts synchronously at loop back-edges. |
| **Sound Blaster CT-VOICE Driver** | Original game invoked Creative Labs resident driver at interrupt vector `15E5:0000`. | Implemented native CT-VOICE dispatcher in `src/ctvoice.c` intercepting driver commands and streaming 8000 Hz PCM directly to the audio mixer. |
| **Secondary Weapon Mechanics** | Secondary weapon button (`C` / `Alt`) does nothing at game start. | Confirmed original game design: secondary missiles/bombs require collecting power-up capsules during stage progression before the weapon subsystem activates. |
| **Copyright Compliance** | Distributing the original binary or data files violates Soft Action copyright. | Designed runtime asset ingestion launcher with zero copyrighted assets in the release package, guarded by strict `.gitignore`. |

---

## 5. File Manifest & Repository Structure

```
fox-ranger/
├── .github/workflows/
│   └── release.yml             # Cross-platform CI/CD release pipeline
├── CMakeLists.txt              # Unified multiplatform build configuration
├── LICENSE                     # MIT License
├── README.md                   # Front-facing GitHub repository documentation
├── PROJECT_REPORT.md           # Master technical ledger (this document)
├── RECOMP_PLAN.md              # Initial decompilation roadmap
├── RECON.md                    # Low-level DOS binary reconnaissance notes
├── enhanced_music/             # Drop-in directory for arranged soundtrack
│   └── README.md               # User guide for custom .ogg/.wav music
├── gen/                        # Recompiled C source code (generated)
│   ├── recomp_gen.h            # Function prototypes and call tables
│   └── recomp_*.c              # 365+ transpiled game functions
├── packaging/                  # Multiplatform packaging & assets
│   ├── FoxRanger.icns          # macOS multi-resolution icon
│   ├── Info.plist.in           # macOS bundle property list template
│   ├── README_RELEASE.md       # End-user release instructions
│   ├── fox_ranger_combat_preview.gif # High-res animated gameplay GIF
│   ├── foxranger.desktop       # Linux XDG desktop entry
│   └── foxranger.png           # 512x512 high-res application icon
├── src/                        # Native engine runtime core
│   ├── audio.h / audio_opl.c   # 4-channel audio mixer & Nuked-OPL3 driver
│   ├── blit_10a7.c / .h        # Native Mode-X sprite blitter
│   ├── cheat.c / .h            # Cheat engine & Konami code handler
│   ├── cpu.c / .h              # 16-bit CPU registers & call table
│   ├── crt_shims.c / .h        # Native MS-DOS CRT function shims
│   ├── ctvoice.c / .h          # Creative Labs CT-VOICE speech driver
│   ├── dos.c / .h              # DOS file path resolution & shims
│   ├── enhanced_music.c / .h   # Arranged CD soundtrack streaming (.ogg/.wav)
│   ├── ingest.c / .h           # Runtime DOS floppy asset scanner
│   ├── launcher.c / .h         # 1992 Mission Control retro cockpit launcher
│   ├── launcher_config.c / .h  # INI configuration manager (foxranger.ini)
│   ├── main_sdl.c              # Application entry point & headless hooks
│   ├── mem.c / .h              # 1 MB real-mode memory model
│   ├── native_dialog.c / .h    # OS native file/folder pickers
│   ├── platform.c / .h         # SDL2 video/audio & virtual clock
│   ├── retro_font.c / .h       # Custom bitmap font rendering
│   └── vga_modex.c / .h        # Mode-X 4-plane VGA emulation
├── tests/                      # Automated test suites
│   ├── boot_smoke.py           # Headless boot, title & stage 1 smoke test
│   ├── diff_blit.py            # Planar VGA blit differential test
│   ├── diff_crt.py             # C runtime shim differential test
│   ├── diff_fn.py              # Unicorn CPU differential test for recompiled code
│   └── test_audio.c            # Headless OPL and CT-VOICE audio verification
├── third_party/                # Permissively licensed third-party libraries
│   ├── nuked-opl3/             # Nuked-OPL3 Yamaha YMF262 emulator (LGPL-2.1)
│   └── stb_vorbis/             # Public domain single-file Ogg Vorbis decoder
├── tools/                      # Engineering utilities & scripts
│   ├── generate_icon.py        # Iconset generation tool
│   ├── make_trailer.py         # Automated 1080p release trailer generator
│   ├── package_release.sh      # Local release packager
│   └── recomp.py               # Assembly-to-C transpilation tool
└── translations/               # English localization dictionaries
    ├── credits_en.json         # Ending staff credits
    ├── ending_en.json          # Victory cutscene & epilogue
    └── intro_en.json           # Opening story prologue & briefings
```

---

## 6. How to Build, Test, and Contribute

### Building Locally
```bash
# macOS
brew install cmake sdl2
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release

# Linux (Debian / Ubuntu)
sudo apt-get update && sudo apt-get install -y build-essential cmake libsdl2-dev
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release

# Windows (PowerShell / Visual Studio / MinGW)
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

### Running Automated Verification
```bash
ctest --test-dir build --output-on-failure
```

### Packaging Releases
```bash
bash tools/package_release.sh
```

### Generating the Release Trailer
```bash
python3 tools/make_trailer.py
```
*(Requires `ffmpeg` installed on the system)*
