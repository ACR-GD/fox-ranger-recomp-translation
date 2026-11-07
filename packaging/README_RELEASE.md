# Fox Ranger (1992) Enhanced Native Recompilation & Translation

Welcome to the native C static recompilation and English translation of **Fox Ranger (폭스레인저)**, the legendary 1992 Korean MS-DOS vertical shoot'em up by Soft Action!

---

## 1. Quick Start

1. **Launch the game**:
   - **macOS**: Open `FoxRanger.app`.
   - **Windows**: Run `foxrt.exe`.
   - **Linux**: Run `./foxrt` or launch from your desktop application menu.

2. **Select your 1992 DOS Game Files**:
   On first boot, the **Mission Control Launcher** will appear. Click **`BROWSE FOLDER...`** and select the folder containing your original 1992 Fox Ranger DOS disk files.
   
   The required files are:
   - `FOX.DAT` (Font, HUD, story artwork)
   - `ADLIB.DAT` (Soundtrack & FM instrument definitions)
   - `VOICE.DAT` (Digitized CT-VOICE speech clips)
   - `STAGE.DAT` (Tilemaps and stage progression)
   - `VSPRITE.PAT` (Sprites and enemy graphics)
   - `FOX.EXE` / `FOX_EN.EXE` (Original DOS executable)

3. Once all asset badges light up **`[OK]`**, click **`>>> START MISSION (ENTER) <<<`**!

---

## 2. Controls

| Action | Primary Key | Alternate Key | Gamepad (SDL2) |
| :--- | :--- | :--- | :--- |
| **Move Ship** | `Arrow Keys` | `W` / `A` / `S` / `D` | D-Pad / Left Stick |
| **Primary Weapon** | `D` | `Ctrl` | Button A (South) |
| **Secondary Weapon** | `C` | `Alt` | Button B (East) |
| **Menu Confirm** | `Enter` / `Space` | | Button Start / A |
| **Pause / Exit** | `Esc` | | Button Back / Guide |

*Note: The secondary weapon (Bomb/Special) only activates once you pick up secondary weapon power-up modules during a stage!*

---

## 3. Enhancements & Features

### Audio Engine & Mixer
- **Nuked-OPL3 FM Synthesis**: Cycle-accurate emulation of Yamaha YMF262/OPL3.
- **Modern Arranged CD Soundtrack**: Place custom `.ogg` or `.wav` files into the `enhanced_music/` folder. The engine will stream high-fidelity modern arrangements with automatic fallback to OPL3.
- **Roland MT-32 & SC-55 Support**: Selectable in the launcher if `MT32.DAT` or `SC55.DAT` are provided.
- **Independent 4-Channel Mixer**: Real-time independent volume sliders for Master, Music, Sound Effects, and Voice/Speech.

### Gameplay & Visuals
- **Rapid Autofire**: Hold down fire (`D` or `Ctrl`) for high-frequency 18 shots/second rapid fire.
- **CRT Aspect Ratio Correction**: Toggle between authentic 4:3 CRT proportion with subtle pillarboxing or full 16:10 square pixels.
- **Full Playthrough Verified**: All 6 stages, bosses, cutscenes, and the translated ending credits are fully playable natively at 60 FPS.
- **Shield Cheat**: Select "Infinite Shields" in the launcher, or enter the authentic Konami Code on the main menu (`Up, Up, Down, Down, Left, Right, Left, Right, B, A`).

---

## 4. Configuration (`foxranger.ini`)

All settings are automatically saved to `foxranger.ini`:
- **macOS**: `~/Library/Application Support/FoxRanger/foxranger.ini`
- **Windows**: `%APPDATA%\FoxRanger\foxranger.ini`
- **Linux**: `~/.config/foxranger/foxranger.ini` (or local directory)
