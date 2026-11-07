#!/usr/bin/env python3
"""Generate retro Fox Ranger application icons for macOS (.icns) and Linux/Windows (.png)."""

import os
import random
import subprocess
from PIL import Image, ImageDraw, ImageFont

def draw_fox_ranger_icon(size=1024):
    img = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    draw = ImageDraw.Draw(img)

    # 1. Rounded container / shield badge
    pad = int(size * 0.05)
    r = int(size * 0.18)
    draw.rounded_rectangle(
        [pad, pad, size - pad, size - pad],
        radius=r,
        fill=(6, 11, 24, 255),
        outline=(0, 200, 255, 255),
        width=int(size * 0.02)
    )

    # Inner beveled border
    inner_pad = pad + int(size * 0.025)
    draw.rounded_rectangle(
        [inner_pad, inner_pad, size - inner_pad, size - inner_pad],
        radius=int(r * 0.85),
        fill=(10, 22, 45, 255),
        outline=(0, 100, 180, 255),
        width=int(size * 0.01)
    )

    # 2. Starfield
    rng = random.Random(1992)
    min_coord = inner_pad + 20
    max_coord = size - inner_pad - 20
    for _ in range(45):
        sx = rng.randint(min_coord, max_coord)
        sy = rng.randint(min_coord, max_coord)
        scol = rng.choice([
            (255, 255, 255, 230),
            (100, 210, 255, 220),
            (255, 225, 110, 200)
        ])
        srad = rng.choice([2, 3, 5])
        draw.ellipse([sx - srad, sy - srad, sx + srad, sy + srad], fill=scol)

    # 3. Thruster Flame Exhaust (Twin plumes)
    cx = size // 2
    cy = int(size * 0.52)
    flame_w = int(size * 0.06)
    flame_h = int(size * 0.22)
    
    # Left Flame
    draw.polygon([
        (cx - int(size * 0.08), cy + int(size * 0.14)),
        (cx - int(size * 0.08) - flame_w // 2, cy + int(size * 0.14) + flame_h),
        (cx - int(size * 0.08) + flame_w // 2, cy + int(size * 0.14) + flame_h * 0.8)
    ], fill=(255, 100, 0, 230))
    draw.polygon([
        (cx - int(size * 0.08), cy + int(size * 0.14)),
        (cx - int(size * 0.08) - flame_w // 4, cy + int(size * 0.14) + int(flame_h * 0.6)),
        (cx - int(size * 0.08) + flame_w // 4, cy + int(size * 0.14) + int(flame_h * 0.5))
    ], fill=(255, 240, 50, 255))

    # Right Flame
    draw.polygon([
        (cx + int(size * 0.08), cy + int(size * 0.14)),
        (cx + int(size * 0.08) + flame_w // 2, cy + int(size * 0.14) + flame_h),
        (cx + int(size * 0.08) - flame_w // 2, cy + int(size * 0.14) + flame_h * 0.8)
    ], fill=(255, 100, 0, 230))
    draw.polygon([
        (cx + int(size * 0.08), cy + int(size * 0.14)),
        (cx + int(size * 0.08) + flame_w // 4, cy + int(size * 0.14) + int(flame_h * 0.6)),
        (cx + int(size * 0.08) - flame_w // 4, cy + int(size * 0.14) + int(flame_h * 0.5))
    ], fill=(255, 240, 50, 255))

    # 4. Supersonic Fighter Jet (Fox Ranger Interceptor)
    nose = (cx, int(size * 0.18))
    wing_l = (int(size * 0.14), int(size * 0.62))
    wing_r = (int(size * 0.86), int(size * 0.62))
    inner_l = (int(size * 0.38), int(size * 0.68))
    inner_r = (int(size * 0.62), int(size * 0.68))
    tail_l = (cx - int(size * 0.04), int(size * 0.64))
    tail_r = (cx + int(size * 0.04), int(size * 0.64))

    # Main Wings (Delta shape)
    draw.polygon([nose, wing_l, inner_l, tail_l, tail_r, inner_r, wing_r],
                 fill=(20, 60, 130, 255), outline=(0, 220, 255, 255), width=max(2, size // 250))

    # Left Wing Highlight
    draw.polygon([nose, (cx - int(size * 0.06), cy), inner_l, wing_l],
                 fill=(25, 90, 190, 255))
    # Right Wing Shading
    draw.polygon([nose, (cx + int(size * 0.06), cy), inner_r, wing_r],
                 fill=(18, 40, 95, 255))

    # Fuselage Spine
    fuse_nose = (cx, int(size * 0.18))
    fuse_l = (cx - int(size * 0.07), int(size * 0.58))
    fuse_r = (cx + int(size * 0.07), int(size * 0.58))
    draw.polygon([fuse_nose, fuse_l, fuse_r], fill=(30, 120, 240, 255), outline=(0, 255, 255, 255), width=max(2, size // 300))

    # Gold Cockpit Canopy
    canopy_top = (cx, int(size * 0.28))
    canopy_l = (cx - int(size * 0.04), int(size * 0.44))
    canopy_r = (cx + int(size * 0.04), int(size * 0.44))
    draw.polygon([canopy_top, canopy_l, canopy_r], fill=(255, 204, 0, 255), outline=(255, 255, 200, 255), width=max(1, size // 400))
    # Canopy glass highlight
    draw.line([(cx - int(size * 0.015), int(size * 0.32)), (cx - int(size * 0.015), int(size * 0.40))],
              fill=(255, 255, 255, 230), width=max(2, size // 150))

    # Wingtip Plasma Cannons (Cyan glow)
    laser_w = max(3, size // 75)
    draw.line([(wing_l[0], wing_l[1]), (wing_l[0], wing_l[1] - int(size * 0.10))], fill=(0, 255, 255, 255), width=laser_w)
    draw.line([(wing_r[0], wing_r[1]), (wing_r[0], wing_r[1] - int(size * 0.10))], fill=(0, 255, 255, 255), width=laser_w)

    # 5. Badge Emblem: "FOX RANGER"
    try:
        font_size = int(size * 0.085)
        font = ImageFont.truetype("/System/Library/Fonts/Supplemental/Arial Bold.ttf", font_size)
    except Exception:
        font = ImageFont.load_default()

    text = "FOX RANGER"
    bbox = draw.textbbox((0, 0), text, font=font)
    tw = bbox[2] - bbox[0]
    th = bbox[3] - bbox[1]
    tx = (size - tw) // 2
    ty = int(size * 0.78)

    # Banner backing
    bw = tw + int(size * 0.08)
    bh = th + int(size * 0.04)
    bx = (size - bw) // 2
    by = ty - int(size * 0.02)
    draw.rounded_rectangle([bx, by, bx + bw, by + bh], radius=int(size * 0.025),
                           fill=(0, 16, 40, 240), outline=(255, 204, 0, 255), width=max(2, size // 150))

    # Text shadow + gold text
    draw.text((tx + 3, ty + 3), text, font=font, fill=(0, 0, 0, 255))
    draw.text((tx, ty), text, font=font, fill=(255, 204, 0, 255))

    return img

def main():
    os.makedirs("packaging", exist_ok=True)
    master = draw_fox_ranger_icon(1024)
    
    # Save standard PNGs
    master.resize((512, 512), Image.Resampling.LANCZOS).save("packaging/foxranger.png", "PNG")
    master.resize((256, 256), Image.Resampling.LANCZOS).save("packaging/foxranger_256.png", "PNG")
    print("Saved packaging/foxranger.png and foxranger_256.png")

    # Generate macOS .iconset
    iconset_dir = "packaging/FoxRanger.iconset"
    os.makedirs(iconset_dir, exist_ok=True)
    
    sizes = [
        ("icon_16x16.png", 16),
        ("icon_16x16@2x.png", 32),
        ("icon_32x32.png", 32),
        ("icon_32x32@2x.png", 64),
        ("icon_128x128.png", 128),
        ("icon_128x128@2x.png", 256),
        ("icon_256x256.png", 256),
        ("icon_256x256@2x.png", 512),
        ("icon_512x512.png", 512),
        ("icon_512x512@2x.png", 1024),
    ]

    for fname, sz in sizes:
        resized = master.resize((sz, sz), Image.Resampling.LANCZOS)
        out_f = os.path.join(iconset_dir, fname)
        resized.save(out_f, "PNG")

    print(f"Saved iconset to {iconset_dir}")

    # Run iconutil if on macOS
    if os.path.exists("/usr/bin/iconutil"):
        icns_path = "packaging/FoxRanger.icns"
        subprocess.run(["iconutil", "-c", "icns", iconset_dir, "-o", icns_path], check=True)
        print(f"Generated macOS icon: {icns_path}")

if __name__ == "__main__":
    main()
