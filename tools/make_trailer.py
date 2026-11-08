#!/usr/bin/env python3
"""make_trailer.py - Official Release Trailer Generator for
Fox Ranger (1992) — Native C Recompilation & English Translation by Second Save.

Captures:
  1. Live animated interactive footage from the 1992 Mission Control Launcher
     (scrolling starfield, folder ingestion, [OK] badges, 4-channel mixer bars,
     scaling/aspect/shield toggles, and launching the mission).
  2. Side-by-side comparison of the 1992 Original Korean MS-DOS release vs.
     the new English Translation by Second Save.
  3. Full-screen showcase of the translated English story cutscenes (Planet
     Malan, VEXER, Earth city destruction, Fox Ranger hangar, cockpit launch,
     English menu, and Stage 1 banner).
  4. High-intensity 60 FPS Stage 1 gameplay with Nuked-OPL3 FM music,
     CT-VOICE speech, and hybrid sound effects.
"""

import sys
sys.path = [p for p in sys.path if not p.endswith('/tools') and not p.endswith('\\tools')]

import os
import shutil
import subprocess
from PIL import Image, ImageDraw, ImageFont

ROOT_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FOXRT = os.path.join(ROOT_DIR, "build", "FoxRanger.app", "Contents", "MacOS", "FoxRanger")
GAME_DIR = os.path.join(ROOT_DIR, "fox-ranger")
EXE_EN = os.path.join(ROOT_DIR, "work", "FOX_EN_FINAL.EXE")
EXE_KR = os.path.join(ROOT_DIR, "work", "FOX_UNP2.EXE")
OUT_DIR = os.path.join(ROOT_DIR, "scratch", "trailer_build")
FINAL_MP4 = os.path.join(ROOT_DIR, "fox_ranger_release_trailer.mp4")

FONT_TITLE_PATH = "/System/Library/Fonts/Helvetica.ttc"
FONT_MONO_PATH = "/System/Library/Fonts/Monaco.ttf"


def draw_retro_card(title, subtitle, bullets, footer, icon_path, out_png):
    """Render a 1920x1080 retro arcade title card with Second Save branding."""
    w, h = 1920, 1080
    im = Image.new("RGB", (w, h), (4, 8, 20))
    d = ImageDraw.Draw(im)

    # Scanline pattern
    for y in range(0, h, 6):
        d.line([(0, y), (w, y)], fill=(7, 14, 32), width=1)

    # Outer & inner neon frames
    d.rectangle([40, 40, w - 40, h - 40], outline=(0, 180, 255), width=3)
    d.rectangle([48, 48, w - 48, h - 48], outline=(0, 60, 130), width=2)

    # Corner brackets
    for cx, cy in [(40, 40), (w - 40, 40), (40, h - 40), (w - 40, h - 40)]:
        dx = 50 if cx == 40 else -50
        dy = 50 if cy == 40 else -50
        d.line([(cx, cy), (cx + dx, cy)], fill=(0, 255, 255), width=6)
        d.line([(cx, cy), (cx, cy + dy)], fill=(0, 255, 255), width=6)

    # Top status bar
    f_small = ImageFont.truetype(FONT_MONO_PATH, 18)
    d.text((64, 58), "SECOND SAVE  *  FOX RANGER (1992) RECOMP & TRANSLATION", fill=(0, 210, 255), font=f_small)
    d.text((w - 310, 58), "RELEASE BUILD v1.0.0 [OK]", fill=(0, 255, 160), font=f_small)

    top_offset = 120
    if icon_path and os.path.exists(icon_path):
        icon = Image.open(icon_path).convert("RGBA").resize((160, 160), Image.Resampling.LANCZOS)
        im.paste(icon, ((w - 160) // 2, 95), icon)
        top_offset = 275

    font_title = ImageFont.truetype(FONT_TITLE_PATH, 68)
    font_sub = ImageFont.truetype(FONT_TITLE_PATH, 34)
    font_body = ImageFont.truetype(FONT_MONO_PATH, 30)
    font_foot = ImageFont.truetype(FONT_MONO_PATH, 24)

    if subtitle:
        bb = d.textbbox((0, 0), subtitle, font=font_sub)
        d.text(((w - (bb[2] - bb[0])) // 2, top_offset), subtitle, fill=(0, 230, 255), font=font_sub)
        top_offset += 60

    bb = d.textbbox((0, 0), title, font=font_title)
    tx = (w - (bb[2] - bb[0])) // 2
    ty = top_offset
    d.text((tx + 4, ty + 4), title, fill=(90, 45, 0), font=font_title)
    d.text((tx, ty), title, fill=(255, 215, 30), font=font_title)
    top_offset += 100

    d.line([(w // 5, top_offset), (4 * w // 5, top_offset)], fill=(0, 180, 255), width=3)
    top_offset += 50

    for b in bullets:
        bb = d.textbbox((0, 0), b, font=font_body)
        bx = (w - (bb[2] - bb[0])) // 2
        d.text((bx, top_offset), b, fill=(230, 245, 255), font=font_body)
        top_offset += 64

    if footer:
        bb = d.textbbox((0, 0), footer, font=font_foot)
        d.text(((w - (bb[2] - bb[0])) // 2, h - 95), footer, fill=(255, 205, 60), font=font_foot)

    os.makedirs(os.path.dirname(out_png), exist_ok=True)
    im.save(out_png)


def make_bezel_overlay(top_label, bottom_label, out_png):
    """Create a 1920x1080 RGB background frame with top/bottom HUD banners
    into which a 1280x960 (4:3) video clip will be overlaid at (320, 60)."""
    w, h = 1920, 1080
    im = Image.new("RGB", (w, h), (5, 10, 24))
    d = ImageDraw.Draw(im)

    for y in range(0, h, 6):
        d.line([(0, y), (w, y)], fill=(8, 16, 36), width=1)

    # Outer frame around the 1280x960 4:3 viewport (x=320..1600, y=60..1020)
    d.rectangle([316, 56, 1604, 1024], outline=(0, 190, 255), width=3)
    d.rectangle([310, 50, 1610, 1030], outline=(0, 70, 140), width=2)

    f_top = ImageFont.truetype(FONT_MONO_PATH, 22)
    f_bot = ImageFont.truetype(FONT_MONO_PATH, 22)

    bb_t = d.textbbox((0, 0), top_label, font=f_top)
    d.text(((w - (bb_t[2] - bb_t[0])) // 2, 16), top_label, fill=(255, 215, 40), font=f_top)

    bb_b = d.textbbox((0, 0), bottom_label, font=f_bot)
    d.text(((w - (bb_b[2] - bb_b[0])) // 2, 1040), bottom_label, fill=(0, 230, 255), font=f_bot)

    os.makedirs(os.path.dirname(out_png), exist_ok=True)
    im.save(out_png)


def make_split_overlay(out_png):
    """Create a 1920x1080 background for side-by-side Korean vs English comparison.
    Left viewport: 880x660 at (40, 220). Right viewport: 880x660 at (1000, 220)."""
    w, h = 1920, 1080
    im = Image.new("RGB", (w, h), (5, 10, 24))
    d = ImageDraw.Draw(im)

    for y in range(0, h, 6):
        d.line([(0, y), (w, y)], fill=(8, 16, 36), width=1)

    f_title = ImageFont.truetype(FONT_TITLE_PATH, 46)
    f_sub = ImageFont.truetype(FONT_MONO_PATH, 26)
    f_bot = ImageFont.truetype(FONT_MONO_PATH, 24)

    header = "COMPLETE ENGLISH TRANSLATION BY SECOND SAVE"
    bb = d.textbbox((0, 0), header, font=f_title)
    tx = (w - (bb[2] - bb[0])) // 2
    d.text((tx + 3, 53), header, fill=(80, 40, 0), font=f_title)
    d.text((tx, 50), header, fill=(255, 215, 30), font=f_title)

    # Left box (Korean)
    d.rectangle([36, 216, 924, 884], outline=(180, 140, 60), width=3)
    lbl_l = "[ 1992 ORIGINAL KOREAN (MS-DOS) ]"
    bb_l = d.textbbox((0, 0), lbl_l, font=f_sub)
    d.text((40 + (880 - (bb_l[2] - bb_l[0])) // 2, 170), lbl_l, fill=(255, 190, 80), font=f_sub)

    # Right box (English)
    d.rectangle([996, 216, 1884, 884], outline=(0, 220, 255), width=3)
    lbl_r = "[ ENGLISH TRANSLATION BY SECOND SAVE ]"
    bb_r = d.textbbox((0, 0), lbl_r, font=f_sub)
    d.text((1000 + (880 - (bb_r[2] - bb_r[0])) // 2, 170), lbl_r, fill=(0, 240, 255), font=f_sub)

    footer = "EVERY STORY SCENE, BOSS DIALOGUE, MENU & CREDIT SCREEN TRANSLATED INTO ENGLISH"
    bb_f = d.textbbox((0, 0), footer, font=f_bot)
    d.text(((w - (bb_f[2] - bb_f[0])) // 2, 950), footer, fill=(220, 240, 255), font=f_bot)

    os.makedirs(os.path.dirname(out_png), exist_ok=True)
    im.save(out_png)


def record_game_raw(exe_path, total_seconds, keys_list, work_dir, fps=30):
    """Run foxrt headless and record continuous PPM frames and WAV audio."""
    os.makedirs(work_dir, exist_ok=True)
    frames_dir = os.path.join(work_dir, "frames")
    os.makedirs(frames_dir, exist_ok=True)
    wav_path = os.path.join(work_dir, "audio.wav")

    cmd = [
        FOXRT,
        "--headless",
        "--game", GAME_DIR,
        "--exe", exe_path,
        "--seconds", str(total_seconds),
        "--record-frames", frames_dir,
        "--record-fps", str(fps),
        "--wav", wav_path
    ]
    if keys_list:
        cmd.extend(["--keys", ",".join(keys_list)])

    print(f"Recording {total_seconds}s from {os.path.basename(exe_path)} at {fps} FPS...")
    subprocess.run(cmd, check=True)
    return frames_dir, wav_path


def make_card_clip(card_png, audio_wav, audio_start_s, dur_s, out_mp4, fps=30):
    """Encode a 1920x1080 title card with background audio."""
    ff = [
        "ffmpeg", "-y",
        "-loop", "1", "-t", str(dur_s), "-i", card_png,
        "-ss", str(audio_start_s), "-t", str(dur_s), "-i", audio_wav,
        "-vf", f"fade=t=in:st=0:d=0.25,fade=t=out:st={dur_s - 0.25}:d=0.25",
        "-af", f"afade=t=in:st=0:d=0.15,afade=t=out:st={dur_s - 0.15}:d=0.15",
        "-c:v", "libx264", "-pix_fmt", "yuv420p", "-preset", "fast", "-crf", "18",
        "-c:a", "aac", "-b:a", "192k", "-r", str(fps), "-shortest",
        out_mp4
    ]
    subprocess.run(ff, check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    print(f"Created card clip ({dur_s}s): {os.path.basename(out_mp4)}")


def slice_framed_clip(frames_dir, src_fps, wav_path, start_s, dur_s, bezel_png, out_mp4, out_fps=30, audio_start_override=None):
    """Slice recorded frames, scale crisp 4:3 to 1280x960, overlay onto bezel_png (1920x1080)."""
    start_frame = int(start_s * src_fps)
    num_frames = int(dur_s * src_fps)
    a_start = audio_start_override if audio_start_override is not None else start_s

    ff = [
        "ffmpeg", "-y",
        "-loop", "1", "-t", str(dur_s), "-i", bezel_png,
        "-framerate", str(src_fps),
        "-start_number", str(start_frame),
        "-i", os.path.join(frames_dir, "frame_%06d.ppm"),
        "-ss", str(a_start), "-t", str(dur_s), "-i", wav_path,
        "-filter_complex",
        f"[1:v]trim=end_frame={num_frames},setpts=PTS-STARTPTS,scale=1280:960:flags=neighbor,fps={out_fps}[fg];"
        f"[0:v][fg]overlay=320:60:shortest=1",
        "-map", "2:a",
        "-c:v", "libx264", "-pix_fmt", "yuv420p", "-preset", "fast", "-crf", "18",
        "-c:a", "aac", "-b:a", "192k", "-r", str(out_fps), "-shortest",
        out_mp4
    ]
    subprocess.run(ff, check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    print(f"Created framed clip ({dur_s}s): {os.path.basename(out_mp4)}")


def make_split_comparison_clip(kr_frames, kr_start_s, en_frames, en_start_s, src_fps, wav_path, audio_start_s, dur_s, bg_png, out_mp4, out_fps=30):
    """Render side-by-side Korean vs English video clip onto bg_png."""
    kr_sf = int(kr_start_s * src_fps)
    en_sf = int(en_start_s * src_fps)
    n_frames = int(dur_s * src_fps)

    ff = [
        "ffmpeg", "-y",
        "-loop", "1", "-t", str(dur_s), "-i", bg_png,
        "-framerate", str(src_fps), "-start_number", str(kr_sf), "-i", os.path.join(kr_frames, "frame_%06d.ppm"),
        "-framerate", str(src_fps), "-start_number", str(en_sf), "-i", os.path.join(en_frames, "frame_%06d.ppm"),
        "-ss", str(audio_start_s), "-t", str(dur_s), "-i", wav_path,
        "-filter_complex",
        f"[1:v]trim=end_frame={n_frames},setpts=PTS-STARTPTS,scale=880:660:flags=neighbor,fps={out_fps}[kr];"
        f"[2:v]trim=end_frame={n_frames},setpts=PTS-STARTPTS,scale=880:660:flags=neighbor,fps={out_fps}[en];"
        f"[0:v][kr]overlay=40:220:shortest=1[tmp];"
        f"[tmp][en]overlay=1000:220:shortest=1,fade=t=in:st=0:d=0.25,fade=t=out:st={dur_s - 0.25}:d=0.25",
        "-map", "3:a",
        "-c:v", "libx264", "-pix_fmt", "yuv420p", "-preset", "fast", "-crf", "18",
        "-c:a", "aac", "-b:a", "192k", "-r", str(out_fps), "-shortest",
        out_mp4
    ]
    subprocess.run(ff, check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    print(f"Created split comparison clip ({dur_s}s): {os.path.basename(out_mp4)}")


def main():
    print("=== Building Fox Ranger (1992) Trailer — Recomp & Translation by Second Save ===")
    if os.path.exists(OUT_DIR):
        shutil.rmtree(OUT_DIR)
    os.makedirs(OUT_DIR, exist_ok=True)

    icon_path = os.path.join(ROOT_DIR, "packaging", "foxranger.png")

    # -------------------------------------------------------------------------
    # 1. Generate Title Cards & Bezel Overlays
    # -------------------------------------------------------------------------
    card1 = os.path.join(OUT_DIR, "card1_opening.png")
    draw_retro_card(
        "FOX RANGER (1992)",
        "A NATIVE C RECOMP & TRANSLATION BY SECOND SAVE",
        [
            "SOFT ACTION'S 1992 MS-DOS KOREAN SHMUP LANDMARK",
            "100% STATIC C RECOMPILATION — ZERO EMULATION",
            "COMPLETE ENGLISH TRANSLATION + MISSION CONTROL LAUNCHER"
        ],
        "MACOS (APPLE SILICON & INTEL)  *  WINDOWS  *  LINUX",
        icon_path, card1
    )

    card2 = os.path.join(OUT_DIR, "card2_translation.png")
    draw_retro_card(
        "ENGLISH TRANSLATION BY SECOND SAVE",
        "PLAYABLE IN ENGLISH FOR THE FIRST TIME",
        [
            "EVERY STORY CUTSCENE, DIALOGUE & MENU TRANSLATED",
            "FAITHFUL RESTORATION OF THE 1992 KOREAN SCRIPT",
            "CUSTOM BITMAP FONT RENDERING IN NATIVE VGA"
        ],
        "34 YEARS AFTER ITS 1992 SEOUL RELEASE",
        icon_path, card2
    )

    card3 = os.path.join(OUT_DIR, "card3_gameplay.png")
    draw_retro_card(
        "60 FPS NATIVE ARCADE COMBAT",
        "ENGINEERED BY SECOND SAVE",
        [
            "NUKED-OPL3 FM SYNTH + CT-VOICE SPEECH + HYBRID SFX",
            "ARRANGED CD SOUNDTRACK STREAMING (.OGG / .WAV)",
            "RAPID AUTOFIRE (18 SHOTS/SEC) & SECONDARY MISSILES"
        ],
        "BIT-EXACT MODE-X 4-PLANE VGA GRAPHICS",
        icon_path, card3
    )

    card4 = os.path.join(OUT_DIR, "card4_finale.png")
    draw_retro_card(
        "FOX RANGER (1992) — OUT NOW",
        "RECOMP & TRANSLATION BY SECOND SAVE",
        [
            "DOWNLOAD FREE FOR MACOS, WINDOWS & LINUX",
            "ZERO COPYRIGHTED ASSETS SHIPPED — BRING YOUR DOS FILES",
            "100% OPEN SOURCE ON GITHUB"
        ],
        "GITHUB.COM/ACR-GD/FOX-RANGER-RECOMP-TRANSLATION",
        icon_path, card4
    )

    bezel_launcher = os.path.join(OUT_DIR, "bezel_launcher.png")
    make_bezel_overlay(
        "FOX RANGER (1992)  *  RECOMP & TRANSLATION BY SECOND SAVE",
        "1992 MISSION CONTROL LAUNCHER: ASSET INGESTION, 4-CHANNEL MIXER & CRT SCALING",
        bezel_launcher
    )

    bezel_story = os.path.join(OUT_DIR, "bezel_story.png")
    make_bezel_overlay(
        "FOX RANGER (1992)  *  ENGLISH TRANSLATION BY SECOND SAVE",
        "TRANSLATED IN-GAME CINEMATIC STORY: THE INVASION OF VEXER & LAUNCH OF FOX RANGER",
        bezel_story
    )

    bezel_combat = os.path.join(OUT_DIR, "bezel_combat.png")
    make_bezel_overlay(
        "FOX RANGER (1992)  *  NATIVE C RECOMPILATION BY SECOND SAVE",
        "STAGE 1: 60 FPS GAMEPLAY, RAPID AUTOFIRE, OPL3 FM MUSIC & HYBRID SOUND EFFECTS",
        bezel_combat
    )

    split_bg = os.path.join(OUT_DIR, "split_bg.png")
    make_split_overlay(split_bg)

    # -------------------------------------------------------------------------
    # 2. Record Animated Launcher Demo (360 frames = 12.0s at 30 FPS)
    # -------------------------------------------------------------------------
    launcher_frames = os.path.join(OUT_DIR, "launcher_frames")
    os.makedirs(launcher_frames, exist_ok=True)
    print("Recording animated Mission Control Launcher walkthrough...")
    subprocess.run([FOXRT, "--record-launcher", launcher_frames, "--game", "fox-ranger"], cwd=ROOT_DIR, check=True)

    # -------------------------------------------------------------------------
    # 3. Record Game Footage:
    #    (a) Full English Intro (0..268s at 15 FPS)
    #    (b) Original Korean Intro (0..165s at 15 FPS)
    #    (c) English Title, Menu & Stage 1 Combat (0..55s at 30 FPS)
    # -------------------------------------------------------------------------
    en_intro_dir, en_intro_wav = record_game_raw(EXE_EN, 268, [], os.path.join(OUT_DIR, "rec_en_intro"), fps=15)
    kr_intro_dir, _ = record_game_raw(EXE_KR, 165, [], os.path.join(OUT_DIR, "rec_kr_intro"), fps=15)

    combat_keys = ["20.0:SPACE", "26.0:ENTER", "29.0:ENTER"]
    for t_step in range(310, 550, 2):
        sec = t_step / 10.0
        combat_keys.append(f"{sec:.1f}:D")
        phase = (t_step - 310) // 20
        if phase % 4 == 0:
            combat_keys.append(f"{sec:.1f}:RIGHT")
        elif phase % 4 == 1:
            combat_keys.append(f"{sec:.1f}:UP")
        elif phase % 4 == 2:
            combat_keys.append(f"{sec:.1f}:LEFT")
        else:
            combat_keys.append(f"{sec:.1f}:DOWN")
        if t_step in [380, 420, 460, 500, 530]:
            combat_keys.append(f"{sec:.1f}:C")

    combat_dir, combat_wav = record_game_raw(EXE_EN, 55, combat_keys, os.path.join(OUT_DIR, "rec_combat"), fps=30)

    # -------------------------------------------------------------------------
    # 4. Assemble Video Segments
    # -------------------------------------------------------------------------
    segments = []

    # Seg 1 (4.0s): Opening Card — Recomp & Translation by Second Save
    s01 = os.path.join(OUT_DIR, "seg01_opening.mp4")
    make_card_clip(card1, combat_wav, audio_start_s=21.0, dur_s=4.0, out_mp4=s01)
    segments.append(s01)

    # Seg 2 (11.5s): Animated Mission Control Launcher Footage
    s02 = os.path.join(OUT_DIR, "seg02_launcher.mp4")
    slice_framed_clip(
        launcher_frames, src_fps=30, wav_path=en_intro_wav,
        start_s=0.0, dur_s=11.5, bezel_png=bezel_launcher, out_mp4=s02,
        out_fps=30, audio_start_override=12.0
    )
    segments.append(s02)

    # Seg 3 (3.5s): Translation Card — Localized by Second Save
    s03 = os.path.join(OUT_DIR, "seg03_trans_card.mp4")
    make_card_clip(card2, en_intro_wav, audio_start_s=60.0, dur_s=3.5, out_mp4=s03)
    segments.append(s03)

    # Seg 4 (6.0s): Side-by-Side Split-Screen — 1992 Korean vs Second Save English
    #   Part A (3.0s): VEXER dialogue (KR 86s vs EN 108s)
    #   Part B (3.0s): Fox Ranger hangar (KR 152s vs EN 211s)
    s04a = os.path.join(OUT_DIR, "seg04a_split1.mp4")
    make_split_comparison_clip(
        kr_frames=kr_intro_dir, kr_start_s=86.0,
        en_frames=en_intro_dir, en_start_s=108.0,
        src_fps=15, wav_path=en_intro_wav, audio_start_s=108.0, dur_s=3.5,
        bg_png=split_bg, out_mp4=s04a
    )
    segments.append(s04a)

    s04b = os.path.join(OUT_DIR, "seg04b_split2.mp4")
    make_split_comparison_clip(
        kr_frames=kr_intro_dir, kr_start_s=152.0,
        en_frames=en_intro_dir, en_start_s=211.0,
        src_fps=15, wav_path=en_intro_wav, audio_start_s=211.0, dur_s=3.5,
        bg_png=split_bg, out_mp4=s04b
    )
    segments.append(s04b)

    # Seg 5 (4.0s): Full-Screen English Story 1 — Planet Malan & Ruler (88s..98s)
    s05 = os.path.join(OUT_DIR, "seg05_story_malan.mp4")
    slice_framed_clip(
        en_intro_dir, src_fps=15, wav_path=en_intro_wav,
        start_s=90.0, dur_s=4.0, bezel_png=bezel_story, out_mp4=s05
    )
    segments.append(s05)

    # Seg 6 (4.0s): Full-Screen English Story 2 — VEXER targets Earth (154s..164s)
    s06 = os.path.join(OUT_DIR, "seg06_story_vexer.mp4")
    slice_framed_clip(
        en_intro_dir, src_fps=15, wav_path=en_intro_wav,
        start_s=156.0, dur_s=4.0, bezel_png=bezel_story, out_mp4=s06
    )
    segments.append(s06)

    # Seg 7 (5.0s): Full-Screen English Story 3 — VEXER Warship & City Destruction (170s..174s + 194s..200s)
    s07a = os.path.join(OUT_DIR, "seg07a_story_ship.mp4")
    slice_framed_clip(
        en_intro_dir, src_fps=15, wav_path=en_intro_wav,
        start_s=170.0, dur_s=3.0, bezel_png=bezel_story, out_mp4=s07a
    )
    segments.append(s07a)

    s07b = os.path.join(OUT_DIR, "seg07b_story_city.mp4")
    slice_framed_clip(
        en_intro_dir, src_fps=15, wav_path=en_intro_wav,
        start_s=195.0, dur_s=3.5, bezel_png=bezel_story, out_mp4=s07b
    )
    segments.append(s07b)

    # Seg 8 (6.5s): Full-Screen English Story 4 — Pilot Saluting & Hangar Launch Tunnel (239s..265s)
    s08 = os.path.join(OUT_DIR, "seg08_story_launch.mp4")
    slice_framed_clip(
        en_intro_dir, src_fps=15, wav_path=en_intro_wav,
        start_s=240.0, dur_s=6.5, bezel_png=bezel_story, out_mp4=s08
    )
    segments.append(s08)

    # Seg 9 (5.0s): Title Logo, English Menu (> START / OPTION) & STAGE 1 OUTER SPACE (24.5s..31.5s of combat run)
    s09 = os.path.join(OUT_DIR, "seg09_menu_stage1.mp4")
    slice_framed_clip(
        combat_dir, src_fps=30, wav_path=combat_wav,
        start_s=24.5, dur_s=6.5, bezel_png=bezel_story, out_mp4=s09
    )
    segments.append(s09)

    # Seg 10 (3.0s): Combat Feature Card
    s10 = os.path.join(OUT_DIR, "seg10_combat_card.mp4")
    make_card_clip(card3, combat_wav, audio_start_s=31.0, dur_s=3.0, out_mp4=s10)
    segments.append(s10)

    # Seg 11 (10.0s): Stage 1 High-Intensity Gameplay (32.0s..42.0s & 44.0s..49.0s)
    s11a = os.path.join(OUT_DIR, "seg11a_combat1.mp4")
    slice_framed_clip(
        combat_dir, src_fps=30, wav_path=combat_wav,
        start_s=32.0, dur_s=6.0, bezel_png=bezel_combat, out_mp4=s11a
    )
    segments.append(s11a)

    s11b = os.path.join(OUT_DIR, "seg11b_combat2.mp4")
    slice_framed_clip(
        combat_dir, src_fps=30, wav_path=combat_wav,
        start_s=43.0, dur_s=5.5, bezel_png=bezel_combat, out_mp4=s11b
    )
    segments.append(s11b)

    # Seg 12 (5.0s): Finale Card — Recomp & Translation by Second Save
    s12 = os.path.join(OUT_DIR, "seg12_finale.mp4")
    make_card_clip(card4, combat_wav, audio_start_s=21.5, dur_s=5.0, out_mp4=s12)
    segments.append(s12)

    # -------------------------------------------------------------------------
    # 5. Concatenate Final Master Trailer (Re-encode audio cleanly for seamless PTS)
    # -------------------------------------------------------------------------
    print("Concatenating master trailer...")
    concat_list = os.path.join(OUT_DIR, "concat.txt")
    with open(concat_list, "w") as f:
        for seg in segments:
            f.write(f"file '{os.path.abspath(seg)}'\n")

    ff_concat = [
        "ffmpeg", "-y",
        "-f", "concat", "-safe", "0",
        "-i", concat_list,
        "-c:v", "copy",
        "-c:a", "aac", "-b:a", "192k",
        FINAL_MP4
    ]
    subprocess.run(ff_concat, check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

    # Clean up raw frame directories to save disk space
    for sub in ["rec_en_intro", "rec_kr_intro", "rec_combat", "launcher_frames"]:
        p = os.path.join(OUT_DIR, sub)
        if os.path.exists(p):
            shutil.rmtree(p)

    art_dir = "/Users/acr/.gemini/antigravity/brain/1bafc1c2-59f1-41d2-bb5a-13e4326f1014"
    if os.path.isdir(art_dir):
        shutil.copy(FINAL_MP4, os.path.join(art_dir, "fox_ranger_release_trailer.mp4"))

    sz = os.path.getsize(FINAL_MP4) / (1024 * 1024)
    print(f"\n=======================================================")
    print(f"Trailer generated successfully: {FINAL_MP4}")
    print(f"File size: {sz:.2f} MB")
    print(f"=======================================================")


if __name__ == "__main__":
    main()
