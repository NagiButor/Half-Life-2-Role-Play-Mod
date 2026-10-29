#!/usr/bin/env python3
"""Steam store / library art from in-game screenshots + the logo lockups (art/steam/*).

    python -I make_steam_assets.py <out_dir> <shot_main.jpg> <shot_alt.jpg> [more shots for screenshots/]

Every capsule is a crop of a real in-game frame (no external images), graded a little darker toward
the logo side, with the language lockup on top. Sizes are Steam's (2024): header 460x215,
small 231x87, main 616x353, vertical 374x448, library capsule 600x900, library hero 3840x1240
(no text), page background 1438x810 (no text). Screenshots are copied at their size (>= 1280x720).
"""
import os
import sys

import numpy as np
from PIL import Image, ImageEnhance, ImageFilter

HERE = os.path.dirname(os.path.abspath(__file__))
ART = os.path.dirname(HERE)
LOCKUP = {lang: Image.open(os.path.join(ART, "steam", "logo_lockup_%s_full.png" % lang)).convert("RGBA")
          for lang in ("en", "ru")}


def crop_to(img, w, h, focus=(0.5, 0.5)):
    """Scale to cover w x h, crop around focus (fractions of the source)."""
    sw, sh = img.size
    s = max(w / sw, h / sh)
    nw, nh = int(round(sw * s)), int(round(sh * s))
    im = img.resize((nw, nh), Image.LANCZOS)
    cx, cy = focus[0] * nw, focus[1] * nh
    x0 = int(min(max(cx - w / 2, 0), nw - w))
    y0 = int(min(max(cy - h / 2, 0), nh - h))
    return im.crop((x0, y0, x0 + w, y0 + h))


def grade(img, side="left", strength=0.55):
    """Darken toward one side (where the logo goes) and lift contrast a touch."""
    a = np.asarray(img.convert("RGB")).astype(np.float32) / 255.0
    h, w = a.shape[:2]
    if side == "left":
        t = np.linspace(1.0, 0.0, w)[None, :, None]
    elif side == "bottom":
        t = np.linspace(0.0, 1.0, h)[:, None, None]
    elif side == "top":
        t = np.linspace(1.0, 0.0, h)[:, None, None]
    else:
        t = np.zeros((1, 1, 1))
    t = np.clip(t * 1.6 - 0.25, 0.0, 1.0) ** 1.5
    a = a * (1.0 - strength * t)
    # slight cool tint in the shadows (Combine blue), keep highlights
    a = a + (np.array([0.0, 0.01, 0.03]) * (1.0 - a))
    im = Image.fromarray(np.clip(a * 255.0 + 0.5, 0, 255).astype(np.uint8))
    return ImageEnhance.Contrast(im).enhance(1.06)


def put_logo(img, lang, width_frac, pos=("left", "center"), margin=0.06):
    w, h = img.size
    lw = int(w * width_frac)
    lock = LOCKUP[lang]
    lh = int(lock.size[1] * lw / lock.size[0])
    logo = lock.resize((lw, lh), Image.LANCZOS)
    # soft shadow for legibility over bright skies
    sh = Image.new("RGBA", logo.size, (0, 0, 0, 0))
    sh.putalpha(logo.split()[3].point(lambda v: int(v * 0.65)))
    sh = sh.filter(ImageFilter.GaussianBlur(max(2, lw // 120)))
    mx, my = int(w * margin), int(h * margin)
    x = mx if pos[0] == "left" else (w - lw) // 2 if pos[0] == "center" else w - lw - mx
    y = (h - lh) // 2 if pos[1] == "center" else my if pos[1] == "top" else h - lh - my
    out = img.convert("RGBA")
    out.alpha_composite(sh, (x + max(1, lw // 300), y + max(1, lw // 250)))
    out.alpha_composite(logo, (x, y))
    return out.convert("RGB")


def main():
    out, main_shot, alt_shot = sys.argv[1], sys.argv[2], sys.argv[3]
    extra = sys.argv[4:]
    os.makedirs(out, exist_ok=True)
    A = Image.open(main_shot).convert("RGB")
    B = Image.open(alt_shot).convert("RGB")
    jobs = [
        ("header_capsule_460x215", A, 460, 215, (0.55, 0.42), "left", 0.62, ("left", "center")),
        ("small_capsule_231x87", A, 231, 87, (0.55, 0.40), "left", 0.86, ("center", "center")),
        ("main_capsule_616x353", A, 616, 353, (0.55, 0.45), "left", 0.60, ("left", "center")),
        ("vertical_capsule_374x448", B, 374, 448, (0.62, 0.45), "bottom", 0.86, ("center", "bottom")),
        ("library_capsule_600x900", B, 600, 900, (0.62, 0.45), "bottom", 0.86, ("center", "bottom")),
    ]
    for name, src, w, h, focus, side, lw, pos in jobs:
        base = grade(crop_to(src, w, h, focus), side)
        for lang in ("en", "ru"):
            put_logo(base, lang, lw, pos).save(os.path.join(out, "%s_%s.png" % (name, lang)))
    # text-free art
    grade(crop_to(A, 3840, 1240, (0.5, 0.40)), "none").save(os.path.join(out, "library_hero_3840x1240.png"))
    page = grade(crop_to(B, 1438, 810, (0.5, 0.5)), "none", 0.0).filter(ImageFilter.GaussianBlur(2))
    ImageEnhance.Brightness(page).enhance(0.55).save(os.path.join(out, "page_background_1438x810.png"))
    shots = os.path.join(out, "screenshots")
    os.makedirs(shots, exist_ok=True)
    for i, p in enumerate([main_shot, alt_shot] + extra):
        Image.open(p).convert("RGB").save(os.path.join(shots, "screenshot_%02d.jpg" % (i + 1)), quality=94)
    print("wrote", out)


if __name__ == "__main__":
    main()
