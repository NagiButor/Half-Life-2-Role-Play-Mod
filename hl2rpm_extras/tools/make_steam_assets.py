"""Steam art of HL2RPM in the style of the main menu (make_menu_assets.py): the icon - the user's
badge (art/icon_src.jpg, cut out of its backdrop) - the library art (hero, logo, capsules) with the
sprayed lambda of the menu and the store / Steamworks sizes for the release.

    python make_steam_assets.py [--out <dir>] [--grid]

<out> (default E:/SourceModding/HL2RPMSOURCE/steam_assets):
    icon/hl2rpm.ico                     16..256, the desktop shortcut and Steamworks "client icon"
    icon/hl2rpm_256.png, icon_184.jpg   community icon (184x184 jpg), client tga (32x32)
    library/hero.jpg 3840x1240, logo.png 1280x720, capsule_600x900.png, header_920x430.png
    store/ main capsule 1232x706, small 462x174, vertical 748x896, header 920x430,
           page background 1438x810, library header 920x430, library capsule 600x900

The mod itself: resource/icon1.tga (16x16) and resource/icon1_big.tga (32x32) - gameinfo "icon"
(the icon in Steam's library list) - and resource/game.ico (the game window's icon).

--grid also copies the library art into Steam's custom artwork folder
(userdata/<user>/config/grid) under the ids Steam may give the mod (it does not store the id
anywhere we can read; crc32 of the mod folder with the high bit is the first guess).
Steam reads the folder at its start.
"""
import math
import os
import shutil
import struct
import sys
import zlib

import numpy as np
from PIL import Image, ImageDraw, ImageFilter

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import make_menu_assets as mm  # noqa: E402

MOD = mm.MOD
STEAM = r"E:\Steam"


# ------------------------------------------------------------------ the icon
ICON_SRC = os.path.join(mm.ART, "icon_src.jpg")
_ICON = None


def icon_master():
    """the user's icon (art/icon_src.jpg, 2026-11-02: the lambda badge with "RPM") cut out of its
    dark backdrop: its rim is as dark as the backdrop, only a thin light edge and the bevel tell
    them apart - so the backdrop is what is reached from the border without crossing anything
    brighter or darker than it. Square RGBA (float), the badge centred."""
    global _ICON
    if _ICON is not None:
        return _ICON
    a = np.asarray(Image.open(ICON_SRC).convert("RGB"), np.float32)
    L = a.mean(2)
    bgL = float(np.median(np.concatenate([L[:20].ravel(), L[-20:].ravel(), L[:, :20].ravel(), L[:, -20:].ravel()])))
    barrier = Image.fromarray(((np.abs(L - bgL) > 14) * 255).astype(np.uint8)).filter(ImageFilter.MaxFilter(7))
    fill = Image.fromarray(np.where(np.asarray(barrier) > 0, 0, 255).astype(np.uint8)).copy()
    ImageDraw.floodfill(fill, (0, 0), 128)
    back = Image.fromarray(((np.asarray(fill) == 128) * 255).astype(np.uint8)).filter(ImageFilter.MaxFilter(7))
    badge = 255 - np.asarray(back, np.float32)
    alpha = np.asarray(Image.fromarray(badge.astype(np.uint8)).filter(ImageFilter.GaussianBlur(1.2)), np.float32) / 255
    ys, xs = np.nonzero(alpha > 0.5)
    x0, x1, y0, y1 = xs.min(), xs.max() + 1, ys.min(), ys.max() + 1
    side = int(max(x1 - x0, y1 - y0) * 1.02)
    out = np.zeros((side, side, 4), np.float32)
    ox, oy = (side - (x1 - x0)) // 2, (side - (y1 - y0)) // 2
    out[oy:oy + y1 - y0, ox:ox + x1 - x0, :3] = a[y0:y1, x0:x1]
    out[oy:oy + y1 - y0, ox:ox + x1 - x0, 3] = alpha[y0:y1, x0:x1] * 255
    _ICON = out
    return out


def icon_image(size, worn=True):
    """the icon at a size (RGBA uint8): resized premultiplied, so the edge does not go dark"""
    m = icon_master()
    a = m[..., 3:4] / 255
    pre = np.dstack([m[..., :3] * a, m[..., 3:4]])
    r = np.dstack([np.asarray(Image.fromarray(np.clip(pre[..., c], 0, 255).astype(np.uint8)).resize((size, size), Image.LANCZOS), np.float32) for c in range(4)])
    al = r[..., 3:4] / 255
    rgb = r[..., :3] / np.maximum(al, 1 / 255)
    if size <= 48:
        # small sizes: a touch of contrast, the downscale washes the lambda out
        rgb = np.clip((rgb - 110) * 1.15 + 110, 0, 255)
    return np.dstack([np.clip(rgb, 0, 255), r[..., 3:4]]).astype(np.uint8)


def write_ico(path, sizes):
    """.ico: 32-bit DIB entries (what LoadImage of the window icon reads everywhere), PNG for 256"""
    import io
    entries, blobs = [], []
    for s in sizes:
        rgba = icon_image(s, worn=s >= 48)
        if s >= 256:
            b = io.BytesIO()
            Image.fromarray(rgba).save(b, "PNG")
            blob = b.getvalue()
        else:
            bmp = struct.pack("<IiiHHIIiiII", 40, s, s * 2, 1, 32, 0, s * s * 4, 0, 0, 0, 0)
            xor = rgba[::-1][..., [2, 1, 0, 3]].tobytes()
            row = ((s + 31) // 32) * 4
            andmask = bytearray(row * s)
            for y in range(s):
                for x in range(s):
                    if rgba[s - 1 - y, x, 3] < 128:
                        andmask[y * row + x // 8] |= 0x80 >> (x % 8)
            blob = bmp + xor + bytes(andmask)
        blobs.append(blob)
        entries.append(s)
    header = struct.pack("<HHH", 0, 1, len(sizes))
    off = 6 + 16 * len(sizes)
    dirs = b""
    for s, blob in zip(entries, blobs):
        dirs += struct.pack("<BBBBHHII", s if s < 256 else 0, s if s < 256 else 0, 0, 0, 1, 32, len(blob), off)
        off += len(blob)
    with open(path, "wb") as f:
        f.write(header + dirs + b"".join(blobs))


def write_tga(path, rgba):
    h, w = rgba.shape[:2]
    head = struct.pack("<BBBHHBHHHHBB", 0, 0, 2, 0, 0, 0, 0, 0, w, h, 32, 8 | 0x20)
    with open(path, "wb") as f:
        f.write(head + rgba[..., [2, 1, 0, 3]].tobytes())


# ------------------------------------------------------------------ library / store art
WALL = None


def wall(w, h, emblem_cx, emblem_cy, emblem_size, lines=True, seed=0):
    """the menu's wall with the emblem, at any size"""
    global WALL
    if WALL is None:
        print("wall...")
        WALL = mm.make_background_image()
    bg = WALL
    # cover
    aspect = bg.width / bg.height
    if w / h > aspect:
        sw, sh = w, int(w / aspect)
    else:
        sw, sh = int(h * aspect), h
    img = bg.resize((sw, sh), Image.LANCZOS).crop(((sw - w) // 2, (sh - h) // 2, (sw - w) // 2 + w, (sh - h) // 2 + h))
    out = np.asarray(img, np.float32).copy()
    if emblem_size > 0:
        es = int(emblem_size)
        tone, alpha = EMBLEM
        e_rgb = np.asarray(Image.fromarray(np.clip(tone, 0, 255).astype(np.uint8)).resize((es, es), Image.LANCZOS), np.float32)
        if e_rgb.ndim == 2:
            e_rgb = e_rgb[..., None]
        e_a = np.asarray(Image.fromarray((alpha * 255).astype(np.uint8)).resize((es, es), Image.LANCZOS), np.float32) / 255 * 0.92
        x0, y0 = int(emblem_cx - es / 2), int(emblem_cy - es * 0.47)
        paste_alpha(out, e_rgb, e_a, x0, y0)
    if lines:
        d = Image.fromarray(np.clip(out, 0, 255).astype(np.uint8))
        dr = ImageDraw.Draw(d, "RGBA")
        lw = max(1, h // 900)
        for gl in [(0.057, 0.035, 0.057, 0.965, 20), (0.56, 0.083, 0.985, 0.083, 17), (0.03, 0.792, 0.985, 0.792, 19)]:
            dr.line((gl[0] * w, gl[1] * h, gl[2] * w, gl[3] * h), fill=(255, 255, 255, gl[4]), width=lw)
        cs = h * 0.011
        for cx, cy in [(0.933, 0.083), (0.446, 0.792), (0.933, 0.792)]:
            dr.line((cx * w - cs, cy * h, cx * w + cs, cy * h), fill=(210, 210, 210, 70), width=lw)
            dr.line((cx * w, cy * h - cs, cx * w, cy * h + cs), fill=(210, 210, 210, 70), width=lw)
        out = np.asarray(d, np.float32).copy()
    # vignette
    y, x = np.mgrid[0:h, 0:w].astype(np.float32)
    rr = np.sqrt(((x - w / 2) / (w / 2)) ** 2 + ((y - h / 2) / (h / 2)) ** 2)
    out *= (1 - 0.55 * mm.smoothstep(0.5, 1.45, rr))[..., None]
    return out


def paste_alpha(out, rgb, a, x0, y0):
    h, w = out.shape[:2]
    eh, ew = a.shape[:2]
    sx0, sy0 = max(0, -x0), max(0, -y0)
    dx0, dy0 = max(0, x0), max(0, y0)
    dx1, dy1 = min(w, x0 + ew), min(h, y0 + eh)
    if dx1 <= dx0 or dy1 <= dy0:
        return
    sub = out[dy0:dy1, dx0:dx1]
    aa = a[sy0:sy0 + dy1 - dy0, sx0:sx0 + dx1 - dx0]
    cc = rgb[sy0:sy0 + dy1 - dy0, sx0:sx0 + dx1 - dx0]
    if aa.ndim == 2:
        aa = aa[..., None]
    if cc.ndim == 2:
        cc = cc[..., None]
    sub[:] = sub * (1 - aa) + cc * aa


def put_title(out, x, y, width, shadow=True):
    gray, alpha = TITLE
    th = int(width / 4)
    t_rgb = np.asarray(Image.fromarray(np.clip(gray, 0, 255).astype(np.uint8)).resize((width, th), Image.LANCZOS), np.float32)
    t_a = np.asarray(Image.fromarray((alpha * 255).astype(np.uint8)).resize((width, th), Image.LANCZOS), np.float32) / 255
    if shadow:
        sh = mm.blur(t_a, max(1.0, width / 300)) * 0.7
        paste_alpha(out, np.zeros_like(t_rgb), sh, x + max(1, width // 400), y + max(1, width // 300))
    paste_alpha(out, t_rgb, t_a, x, y)


def save(img, path, quality=92):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    im = Image.fromarray(np.clip(img, 0, 255).astype(np.uint8))
    if path.endswith(".jpg"):
        im.convert("RGB").save(path, quality=quality)
    else:
        im.save(path)
    print("  ", os.path.basename(path), im.size)


def banner(w, h, title=True, emblem_right=True):
    """a wide picture: title on the left, emblem on the right"""
    es = h * 0.95
    out = wall(w, h, w - es * 0.56 if emblem_right else w / 2, h * 0.50, es)
    if title:
        tw = int(min(w * 0.47, h * 1.9, w - es * 1.1 - w * 0.06))
        put_title(out, int(w * 0.045), int(h * 0.5 - tw / 8 - h * 0.04), tw)
    return out


def logo_png(w=1280, h=720):
    gray, alpha = TITLE
    # crop the title texture to its ink, fit it into the logo box
    ys, xs = np.nonzero(alpha > 0.02)
    g = gray[ys.min():ys.max() + 1, xs.min():xs.max() + 1]
    a = alpha[ys.min():ys.max() + 1, xs.min():xs.max() + 1]
    scale = min(w * 0.96 / a.shape[1], h * 0.9 / a.shape[0])
    nw, nh = int(a.shape[1] * scale), int(a.shape[0] * scale)
    g = np.asarray(Image.fromarray(np.clip(g, 0, 255).astype(np.uint8)).resize((nw, nh), Image.LANCZOS), np.float32)
    a = np.asarray(Image.fromarray((a * 255).astype(np.uint8)).resize((nw, nh), Image.LANCZOS), np.float32) / 255
    out = np.zeros((h, w, 4), np.float32)
    x0, y0 = (w - nw) // 2, (h - nh) // 2
    # a soft dark halo under the letters: readable over any hero
    halo = np.zeros((h, w), np.float32)
    halo[y0:y0 + nh, x0:x0 + nw] = a
    halo = np.clip(mm.blur(halo, 10) * 1.6, 0, 0.75)
    out[..., 3] = halo * 255
    rgb = np.zeros((h, w), np.float32)
    rgb[y0:y0 + nh, x0:x0 + nw] = g * 1.08
    aa = np.zeros((h, w), np.float32)
    aa[y0:y0 + nh, x0:x0 + nw] = a
    out[..., 3] = np.maximum(out[..., 3], aa * 255)
    for c in range(3):
        out[..., c] = rgb * aa / np.maximum(out[..., 3] / 255, 1e-3)
    return out


def portrait(w, h):
    es = w * 0.92
    out = wall(w, h, w * 0.5, h * 0.38, es, lines=False)
    tw = int(w * 0.88)
    put_title(out, int(w * 0.06), int(h * 0.74), tw)
    return out


EMBLEM = None
TITLE = None


def main(argv):
    global EMBLEM, TITLE
    out = argv[argv.index("--out") + 1] if "--out" in argv else r"E:\SourceModding\HL2RPMSOURCE\steam_assets"
    os.makedirs(out, exist_ok=True)
    print("emblem, title...")
    EMBLEM = mm.make_emblem(2048, write=False)
    TITLE = mm.title_image(4096, 1024)

    print("icon:")
    os.makedirs(os.path.join(out, "icon"), exist_ok=True)
    write_ico(os.path.join(out, "icon", "hl2rpm.ico"), [16, 24, 32, 48, 64, 128, 256])
    # the game window's icon: the engine loads resource/game.ico from the search paths (without
    # one in the mod the title bar showed Half-Life 2's)
    write_ico(os.path.join(MOD, "resource", "game.ico"), [16, 24, 32, 48, 64, 128, 256])
    Image.fromarray(icon_image(512)).save(os.path.join(out, "icon", "hl2rpm_512.png"))
    Image.fromarray(icon_image(256)).save(os.path.join(out, "icon", "hl2rpm_256.png"))
    com = Image.fromarray(icon_image(184))
    bgc = Image.new("RGB", com.size, (22, 22, 22))
    bgc.paste(com, (0, 0), com)
    bgc.save(os.path.join(out, "icon", "community_icon_184.jpg"), quality=95)
    write_tga(os.path.join(out, "icon", "client_32.tga"), icon_image(32, worn=False))
    write_tga(os.path.join(MOD, "resource", "icon1.tga"), icon_image(16, worn=False))
    write_tga(os.path.join(MOD, "resource", "icon1_big.tga"), icon_image(32, worn=False))
    print("   resource/game.ico, icon1.tga, icon1_big.tga -> the mod")

    print("library:")
    lib = os.path.join(out, "library")
    hero = banner(3840, 1240, title=False)
    save(hero, os.path.join(lib, "hero.jpg"))
    save(logo_png(), os.path.join(lib, "logo.png"))
    save(portrait(600, 900), os.path.join(lib, "capsule_600x900.png"))
    save(banner(920, 430), os.path.join(lib, "header_920x430.png"))

    print("store:")
    st = os.path.join(out, "store")
    save(banner(1232, 706), os.path.join(st, "main_capsule_1232x706.png"))
    save(banner(462, 174), os.path.join(st, "small_capsule_462x174.png"))
    save(portrait(748, 896), os.path.join(st, "vertical_capsule_748x896.png"))
    save(banner(920, 430), os.path.join(st, "header_capsule_920x430.png"))
    save(wall(1438, 810, 1438 * 0.72, 810 * 0.47, 810 * 0.86) * 0.7, os.path.join(st, "page_background_1438x810.jpg"))
    save(banner(920, 430), os.path.join(st, "library_header_920x430.png"))
    save(portrait(600, 900), os.path.join(st, "library_capsule_600x900.png"))
    save(hero, os.path.join(st, "library_hero_3840x1240.jpg"))
    save(logo_png(), os.path.join(st, "library_logo_1280x720.png"))

    if "--grid" in argv:
        users = os.path.join(STEAM, "userdata")
        ids = []
        for s in ("hl2rpm", os.path.join(STEAM, "steamapps", "sourcemods", "hl2rpm")):
            mod = zlib.crc32(s.encode()) | 0x80000000
            ids.append(str(mod))
        ids.append(str(((zlib.crc32(b"hl2rpm") | 0x80000000) << 32) | (2 << 24) | 243730))
        for u in os.listdir(users):
            grid = os.path.join(users, u, "config", "grid")
            os.makedirs(grid, exist_ok=True)
            for gid in ids:
                shutil.copy(os.path.join(lib, "hero.jpg"), os.path.join(grid, gid + "_hero.jpg"))
                shutil.copy(os.path.join(lib, "logo.png"), os.path.join(grid, gid + "_logo.png"))
                shutil.copy(os.path.join(lib, "capsule_600x900.png"), os.path.join(grid, gid + "p.png"))
                shutil.copy(os.path.join(lib, "header_920x430.png"), os.path.join(grid, gid + ".png"))
                shutil.copy(os.path.join(out, "icon", "hl2rpm_256.png"), os.path.join(grid, gid + "_icon.png"))
            print("grid:", grid, ids)
    print("done:", out)


if __name__ == "__main__":
    main(sys.argv[1:])
