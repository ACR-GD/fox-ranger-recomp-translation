#!/usr/bin/env python3
"""make_trailer.py - Generates an authentic 1992-style release trailer for
Fox Ranger Native Recompilation & English Translation.

Captures real in-game footage and audio directly from the recompiled engine
(headless virtual clock), renders retro 1992 arcade title cards, and
assembles a high-definition 1080p 60fps MP4 release trailer with ffmpeg.
"""

import sys
sys.path = [p for p in sys.path if not p.endswith('/tools') and not p.endswith('\\tools')]

import os
import shutil
import struct
import subprocess
import wave
from PIL import Image, ImageDraw, ImageFont

ROOT_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FOXRT = os.path.join(ROOT_DIR, "build", "FoxRanger.app", "Contents", "MacOS", "FoxRanger")
GAME_DIR = os.path.join(ROOT_DIR, "fox-ranger")
EXE_PATH = os.path.join(ROOT_DIR, "work", "FOX_EN_FINAL.EXE")
OUT_DIR = os.path.join(ROOT_DIR, "scratch", "trailer_build")
FINAL_MP4 = os.path.join(ROOT_DIR, "fox_ranger_release_trailer.mp4")

# Fallback fonts
FONT_TITLE_PATH = "/System/Library/Fonts/Helvetica.ttc"
FONT_MONO_PATH = "/System/Library/Fonts/Monaco.ttf"


def draw_retro_card(title, subtitle, bullets, footer, icon_path, out_png):
    """Render a 1920x1080 retro arcade title card with 1992 Fox Ranger aesthetics."""
    w, h = 1920, 1080
    im = Image.new("RGB", (w, h), (4, 8, 20))
    d = ImageDraw.Draw(im)

    # 1. Subtle scanline pattern
    for y in range(0, h, 6):
        d.line([(0, y), (w, y)], fill=(6, 13, 30), width=1)

    # 2. Cyber HUD borders & neon frame
    d.rectangle([40, 40, w - 40, h - 40], outline=(0, 180, 255), width=3)
    d.rectangle([48, 48, w - 48, h - 48], outline=(0, 60, 130), width=2)

    # Corner brackets
    for cx, cy in [(40, 40), (w - 40, 40), (40, h - 40), (w - 40, h - 40)]:
        dx = 50 if cx == 40 else -50
        dy = 50 if cy == 40 else -50
        d.line([(cx, cy), (cx + dx, cy)], fill=(0, 255, 255), width=6)
        d.line([(cx, cy), (cx, cy + dy)], fill=(0, 255, 255), width=6)

    # Top/Bottom status bars
    d.text((60, 56), "MISSION: FOX RANGER RESTORATION", fill=(0, 200, 255), font=ImageFont.truetype(FONT_MONO_PATH, 16))
    d.text((w - 280, 56), "SYS VER 1.0.0 [OK]", fill=(0, 255, 180), font=ImageFont.truetype(FONT_MONO_PATH, 16))

    # Optional icon
    top_offset = 120
    if icon_path and os.path.exists(icon_path):
        icon = Image.open(icon_path).convert("RGBA").resize((160, 160), Image.Resampling.LANCZOS)
        im.paste(icon, ((w - 160) // 2, 90), icon)
        top_offset = 270

    font_title = ImageFont.truetype(FONT_TITLE_PATH, 68)
    font_sub = ImageFont.truetype(FONT_TITLE_PATH, 34)
    font_body = ImageFont.truetype(FONT_MONO_PATH, 32)
    font_foot = ImageFont.truetype(FONT_MONO_PATH, 24)

    # Subtitle / Category
    if subtitle:
        bb = d.textbbox((0, 0), subtitle, font=font_sub)
        d.text(((w - (bb[2] - bb[0])) // 2, top_offset), subtitle, fill=(0, 220, 255), font=font_sub)
        top_offset += 60

    # Main Title with drop shadow and gold metallic glow
    bb = d.textbbox((0, 0), title, font=font_title)
    tx = (w - (bb[2] - bb[0])) // 2
    ty = top_offset
    d.text((tx + 4, ty + 4), title, fill=(90, 45, 0), font=font_title)
    d.text((tx, ty), title, fill=(255, 215, 30), font=font_title)
    top_offset += 100

    # Cyan divider line
    d.line([(w // 4, top_offset), (3 * w // 4, top_offset)], fill=(0, 180, 255), width=3)
    top_offset += 50

    # Bullets
    for b in bullets:
        bb = d.textbbox((0, 0), b, font=font_body)
        bx = (w - (bb[2] - bb[0])) // 2
        d.text((bx, top_offset), b, fill=(230, 245, 255), font=font_body)
        top_offset += 65

    # Footer
    if footer:
        bb = d.textbbox((0, 0), footer, font=font_foot)
        d.text(((w - (bb[2] - bb[0])) // 2, h - 90), footer, fill=(110, 170, 230), font=font_foot)

    os.makedirs(os.path.dirname(out_png), exist_ok=True)
    im.save(out_png)


def record_game_raw(total_seconds, keys_list, work_dir, fps=30):
    """Run foxrt headless and record continuous frames and synchronized audio."""
    os.makedirs(work_dir, exist_ok=True)
    frames_dir = os.path.join(work_dir, "frames")
    os.makedirs(frames_dir, exist_ok=True)
    wav_path = os.path.join(work_dir, "audio.wav")

    cmd = [
        FOXRT,
        "--headless",
        "--game", GAME_DIR,
        "--exe", EXE_PATH,
        "--seconds", str(total_seconds),
        "--record-frames", frames_dir,
        "--record-fps", str(fps),
        "--wav", wav_path
    ]
    if keys_list:
        cmd.extend(["--keys", ",".join(keys_list)])

    print(f"Recording {total_seconds}s from foxrt at {fps} FPS...")
    subprocess.run(cmd, check=True)
    return frames_dir, wav_path


def slice_game_clip(frames_dir, wav_path, start_s, dur_s, fps, out_mp4):
    """Cut a segment from recorded frames and audio and encode to 1080p MP4."""
    start_frame = int(start_s * fps)
    num_frames = int(dur_s * fps)

    ff = [
        "ffmpeg", "-y",
        "-framerate", str(fps),
        "-start_number", str(start_frame),
        "-i", os.path.join(frames_dir, "frame_%06d.ppm"),
        "-ss", str(start_s),
        "-t", str(dur_s),
        "-i", wav_path,
        "-vframes", str(num_frames),
        "-vf", "scale=1440:1080:flags=neighbor,pad=1920:1080:(ow-iw)/2:(oh-ih)/2:color=black",
        "-c:v", "libx264", "-pix_fmt", "yuv420p", "-preset", "fast", "-crf", "18",
        "-c:a", "aac", "-b:a", "192k",
        "-r", str(fps),
        "-shortest",
        out_mp4
    ]
    subprocess.run(ff, check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    print(f"Created game clip ({dur_s}s): {out_mp4}")


def make_card_clip(card_png, audio_wav, audio_start_s, dur_s, fps, out_mp4):
    """Encode an image card with background audio into an MP4 clip with fade in/out."""
    ff = [
        "ffmpeg", "-y",
        "-loop", "1",
        "-t", str(dur_s),
        "-i", card_png,
        "-ss", str(audio_start_s),
        "-t", str(dur_s),
        "-i", audio_wav,
        "-vf", f"fade=t=in:st=0:d=0.3,fade=t=out:st={dur_s - 0.3}:d=0.3",
        "-af", f"afade=t=in:st=0:d=0.3,afade=t=out:st={dur_s - 0.3}:d=0.3",
        "-c:v", "libx264", "-pix_fmt", "yuv420p", "-preset", "fast", "-crf", "18",
        "-c:a", "aac", "-b:a", "192k",
        "-r", str(fps),
        "-shortest",
        out_mp4
    ]
    subprocess.run(ff, check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    print(f"Created card clip ({dur_s}s): {out_mp4}")


def make_launcher_clip(launcher_png, audio_wav, audio_start_s, dur_s, fps, out_mp4):
    """Encode launcher preview into 1080p MP4."""
    ff = [
        "ffmpeg", "-y",
        "-loop", "1",
        "-t", str(dur_s),
        "-i", launcher_png,
        "-ss", str(audio_start_s),
        "-t", str(dur_s),
        "-i", audio_wav,
        "-vf", f"scale=1440:1080:flags=neighbor,pad=1920:1080:(ow-iw)/2:(oh-ih)/2:color=black,fade=t=in:st=0:d=0.3,fade=t=out:st={dur_s - 0.3}:d=0.3",
        "-af", f"afade=t=in:st=0:d=0.3,afade=t=out:st={dur_s - 0.3}:d=0.3",
        "-c:v", "libx264", "-pix_fmt", "yuv420p", "-preset", "fast", "-crf", "18",
        "-c:a", "aac", "-b:a", "192k",
        "-r", str(fps),
        "-shortest",
        out_mp4
    ]
    subprocess.run(ff, check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    print(f"Created launcher clip ({dur_s}s): {out_mp4}")


def main():
    print("=== Building Fox Ranger (1992) Official Release Trailer ===")
    os.makedirs(OUT_DIR, exist_ok=True)
    fps = 30
    icon_path = os.path.join(ROOT_DIR, "packaging", "foxranger.png")
    launcher_img = os.path.join(ROOT_DIR, "packaging", "launcher_preview.png")
    if not os.path.exists(launcher_img):
        # Fallback to brain artifact if needed
        art_img = "/Users/acr/.gemini/antigravity/brain/1bafc1c2-59f1-41d2-bb5a-13e4326f1014/launcher_preview.png"
        if os.path.exists(art_img):
            launcher_img = art_img

    # -------------------------------------------------------------------------
    # 1. Generate Title Cards
    # -------------------------------------------------------------------------
    print("Generating retro title cards...")
    card1 = os.path.join(OUT_DIR, "card1_intro.png")
    draw_retro_card(
        "FOX RANGER (폭스레인저)",
        "SOFT ACTION • 1992",
        [
            "THE LEGENDARY MS-DOS VERTICAL ARCADE SHOOT'EM UP",
            "THE PIONEER OF KOREAN PC GAMING"
        ],
        "NOW CELEBRATING 34 YEARS OF LEGACY",
        icon_path, card1
    )

    card2 = os.path.join(OUT_DIR, "card2_recomp.png")
    draw_retro_card(
        "REBORN NATIVELY IN C",
        "34 YEARS LATER",
        [
            "100% STATIC RECOMPILATION — ZERO EMULATION OVERHEAD",
            "NATIVE 60 FPS • APPLE SILICON • WINDOWS • LINUX",
            "FULL ENGLISH TRANSLATION BUILT-IN"
        ],
        "PIXEL-PERFECT MODE-X VGA RESTORATION",
        icon_path, card2
    )

    card3 = os.path.join(OUT_DIR, "card3_launcher.png")
    draw_retro_card(
        "MISSION CONTROL LAUNCHER",
        "NEW ENHANCED INTERFACE",
        [
            "4-CHANNEL AUDIO MIXER: MASTER • BGM • SFX • VOICE",
            "NUKED-OPL3 FM + CT-VOICE + ARRANGE CD SOUNDTRACK",
            "ZERO COPYRIGHT ASSETS SHIPPED — RUNTIME DISK INGESTION"
        ],
        "SELECT YOUR 1992 DOS GAME FOLDER TO PLAY",
        None, card3
    )

    card4 = os.path.join(OUT_DIR, "card4_combat.png")
    draw_retro_card(
        "INTENSE ARCADIA COMBAT",
        "PILOT YOUR FOX RANGER",
        [
            "RAPID AUTOFIRE (18 SHOTS / SECOND)",
            "SECONDARY MISSILE PODS & WEAPON MODULES",
            "SIX EPIC WORLDS & DEADLY BOSS ENCOUNTERS"
        ],
        "PREPARE FOR BATTLE",
        icon_path, card4
    )

    card5 = os.path.join(OUT_DIR, "card5_outnow.png")
    draw_retro_card(
        "AVAILABLE NOW ON GITHUB",
        "MISSION ACCOMPLISHED",
        [
            "MACOS (APPLE SILICON & INTEL) • WINDOWS • LINUX",
            "100% FREE & OPEN SOURCE",
            "BRING YOUR OWN 1992 DOS DISKS"
        ],
        "GITHUB.COM • FOX RANGER RECOMP PROJECT",
        icon_path, card5
    )

    # -------------------------------------------------------------------------
    # 2. Record Gameplay Footage from Fox Ranger Engine
    # -------------------------------------------------------------------------
    # Script gameplay keys:
    # 20.0s: SPACE (skip intro)
    # 25.0s: ENTER (open menu)
    # 28.0s: ENTER (start Stage 1)
    # 30s-55s: Active combat gameplay
    keys = ["20.0:SPACE", "25.0:ENTER", "28.0:ENTER"]

    # Generate intense flight & combat inputs for Stage 1
    for t_step in range(300, 560, 2):
        sec = t_step / 10.0
        keys.append(f"{sec:.1f}:D")          # Rapid fire primary weapon
        # Steering manoeuvres
        phase = (t_step - 300) // 20
        if phase % 4 == 0:
            keys.append(f"{sec:.1f}:RIGHT")
        elif phase % 4 == 1:
            keys.append(f"{sec:.1f}:UP")
        elif phase % 4 == 2:
            keys.append(f"{sec:.1f}:LEFT")
        elif phase % 4 == 3:
            keys.append(f"{sec:.1f}:DOWN")
        # Trigger secondary weapons
        if t_step in [380, 420, 460, 500, 540]:
            keys.append(f"{sec:.1f}:C")      # Bomb / secondary weapon

    work_rec = os.path.join(OUT_DIR, "game_rec")
    frames_dir, wav_path = record_game_raw(56, keys, work_rec, fps=fps)

    # -------------------------------------------------------------------------
    # 3. Create Video Segments
    # -------------------------------------------------------------------------
    segments = []

    # Segment 1: Intro Card (3.5s)
    clip_c1 = os.path.join(OUT_DIR, "seg01_card1.mp4")
    make_card_clip(card1, wav_path, audio_start_s=0.5, dur_s=3.5, fps=fps, out_mp4=clip_c1)
    segments.append(clip_c1)

    # Segment 2: Intro Story Cutscene & Fighter Launch (6.5s: 3.5s - 10.0s)
    clip_g1 = os.path.join(OUT_DIR, "seg02_intro.mp4")
    slice_game_clip(frames_dir, wav_path, start_s=3.5, dur_s=6.5, fps=fps, out_mp4=clip_g1)
    segments.append(clip_g1)

    # Segment 3: Recomp Announcement Card (3.5s)
    clip_c2 = os.path.join(OUT_DIR, "seg03_card2.mp4")
    make_card_clip(card2, wav_path, audio_start_s=21.0, dur_s=3.5, fps=fps, out_mp4=clip_c2)
    segments.append(clip_c2)

    # Segment 4: Title Screen (4.5s: 21.5s - 26.0s)
    clip_g2 = os.path.join(OUT_DIR, "seg04_title.mp4")
    slice_game_clip(frames_dir, wav_path, start_s=21.5, dur_s=4.5, fps=fps, out_mp4=clip_g2)
    segments.append(clip_g2)

    # Segment 5: Launcher Feature Card (3.0s)
    clip_c3 = os.path.join(OUT_DIR, "seg05_card3.mp4")
    make_card_clip(card3, wav_path, audio_start_s=23.0, dur_s=3.0, fps=fps, out_mp4=clip_c3)
    segments.append(clip_c3)

    # Segment 6: Mission Control Launcher UI (3.5s)
    clip_l = os.path.join(OUT_DIR, "seg06_launcher.mp4")
    make_launcher_clip(launcher_img, wav_path, audio_start_s=23.5, dur_s=3.5, fps=fps, out_mp4=clip_l)
    segments.append(clip_l)

    # Segment 7: Combat Callout Card (3.0s)
    clip_c4 = os.path.join(OUT_DIR, "seg07_card4.mp4")
    make_card_clip(card4, wav_path, audio_start_s=30.0, dur_s=3.0, fps=fps, out_mp4=clip_c4)
    segments.append(clip_c4)

    # Segment 8: Stage 1 Gameplay - Waves & Explosions (8.0s: 31.5s - 39.5s)
    clip_g3 = os.path.join(OUT_DIR, "seg08_combat1.mp4")
    slice_game_clip(frames_dir, wav_path, start_s=31.5, dur_s=8.0, fps=fps, out_mp4=clip_g3)
    segments.append(clip_g3)

    # Segment 9: Stage 1 Gameplay - High Intensity & Missiles (6.5s: 42.0s - 48.5s)
    clip_g4 = os.path.join(OUT_DIR, "seg09_combat2.mp4")
    slice_game_clip(frames_dir, wav_path, start_s=42.0, dur_s=6.5, fps=fps, out_mp4=clip_g4)
    segments.append(clip_g4)

    # Segment 10: Out Now / Final Card (5.0s)
    clip_c5 = os.path.join(OUT_DIR, "seg10_outnow.mp4")
    make_card_clip(card5, wav_path, audio_start_s=22.0, dur_s=5.0, fps=fps, out_mp4=clip_c5)
    segments.append(clip_c5)

    # -------------------------------------------------------------------------
    # 4. Concatenate Final Master Trailer
    # -------------------------------------------------------------------------
    print("Concatenating master trailer...")
    concat_list = os.path.join(OUT_DIR, "concat.txt")
    with open(concat_list, "w") as f:
        for seg in segments:
            f.write(f"file '{os.path.abspath(seg)}'\n")

    ff_concat = [
        "ffmpeg", "-y",
        "-f", "concat",
        "-safe", "0",
        "-i", concat_list,
        "-c", "copy",
        FINAL_MP4
    ]
    subprocess.run(ff_concat, check=True)

    # Copy to brain artifact directory for UI display
    art_dir = "/Users/acr/.gemini/antigravity/brain/1bafc1c2-59f1-41d2-bb5a-13e4326f1014"
    if os.path.isdir(art_dir):
        shutil.copy(FINAL_MP4, os.path.join(art_dir, "fox_ranger_release_trailer.mp4"))

    trailer_size = os.path.getsize(FINAL_MP4)
    print(f"\n=======================================================")
    print(f"Trailer generated successfully: {FINAL_MP4}")
    print(f"File size: {trailer_size / (1024 * 1024):.2f} MB")
    print(f"Total duration: ~47 seconds (1080p 30fps H.264 / AAC)")
    print(f"=======================================================")


if __name__ == "__main__":
    main()
