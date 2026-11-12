"""The look of the main menu (game/client/hl2rpm_mainmenu.cpp), after the user's concept
("HALF-LIFE 2 / ROLE PLAY MOD", Portal 2 style): a dark worn wall, a big painted lambda in a ring,
the title. Everything is generated here; the result goes into the mod:

    materials/vgui/hl2rpm/menu/bg.vtf          the wall (4096x2048, drawn over 16:9 - "cover")
    materials/vgui/hl2rpm/menu/emblem.vtf      the sprayed lambda in a ring - the user's graffiti
                                               (art/lambda_graffiti_src.jpg, cut out of its drawn
                                               checkerboard: graffiti_cutout)
    materials/vgui/hl2rpm/menu/title.vtf       HALF-LIFE 2 / ROLE PLAY MOD (alpha)
    materials/vgui/hl2rpm/menu/grain.vtf       film grain, tiled with a random offset every frame
    materials/vgui/hl2rpm/menu/vignette.vtf    the dark corners
    materials/vgui/hl2rpm/menu/mote.vtf        a speck of dust
    materials/vgui/hl2rpm/menu/icon.vtf        a small lambda in a ring (the footer)

    python make_menu_assets.py [--preview out.png]

The emblem and the title are also used by make_steam_assets.py (icon, library art).
The title letters are the glyphs of halflife2.ttf (A = lambda, ' = the superscript 2), as the
HL2 menu draws its title; the subtitle is Bahnschrift (Windows 10+).
"""
import math
import os
import struct
import sys

import numpy as np
from PIL import Image, ImageDraw, ImageFilter, ImageFont

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import hd_textures  # noqa: E402  (DXT VTF writer)

MOD = r"E:\Steam\steamapps\sourcemods\hl2rpm"
OUT = os.path.join(MOD, "materials", "vgui", "hl2rpm", "menu")
ART = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "art")   # the user's source pictures
HL2_FONT = r"E:\Steam\steamapps\common\Source SDK Base 2013 Singleplayer\hl2\resource\halflife2.ttf"
BAHN = r"C:\Windows\Fonts\bahnschrift.ttf"

RNG = np.random.default_rng(1998)


# ------------------------------------------------------------------ noise
def smooth_noise(h, w, cell, seed, resample=Image.BICUBIC):
    """value noise: a random grid of h/cell x w/cell, smoothly enlarged (-1..1)"""
    r = np.random.default_rng(seed)
    gh, gw = max(2, int(math.ceil(h / cell)) + 2), max(2, int(math.ceil(w / cell)) + 2)
    g = r.random((gh, gw), np.float32)
    im = Image.fromarray(g, "F").resize((gw * cell, gh * cell), resample)
    a = np.asarray(im, np.float32)[cell:cell + h, cell:cell + w]
    return a * 2 - 1


def fbm(h, w, cell, octaves, seed, gain=0.5):
    out = np.zeros((h, w), np.float32)
    amp, tot = 1.0, 0.0
    for o in range(octaves):
        c = max(1, cell >> o)
        out += amp * smooth_noise(h, w, c, seed + o * 101)
        tot += amp
        amp *= gain
    return out / tot


def _box(a, r, axis):
    """box filter of radius r along an axis (edge padded), via a cumulative sum"""
    if r < 1:
        return a
    pad = [(0, 0)] * a.ndim
    pad[axis] = (r + 1, r)
    c = np.cumsum(np.pad(a, pad, mode="edge"), axis=axis, dtype=np.float64)
    n = a.shape[axis]
    hi = np.take(c, np.arange(2 * r + 1, 2 * r + 1 + n), axis=axis)
    lo = np.take(c, np.arange(0, n), axis=axis)
    return ((hi - lo) / (2 * r + 1)).astype(np.float32)


def blur(a, sigma):
    """gaussian-like blur (three box passes) of a float array"""
    if sigma <= 0:
        return a.astype(np.float32)
    a = a.astype(np.float32)
    if sigma < 1.0:
        # tiny: a 3-tap kernel
        w = math.exp(-1 / (2 * sigma * sigma))
        k = np.array([w, 1, w], np.float32) / (1 + 2 * w)
        for ax in (0, 1):
            p = np.pad(a, [(1, 1) if i == ax else (0, 0) for i in range(a.ndim)], mode="edge")
            a = k[0] * np.take(p, range(0, a.shape[ax]), axis=ax) + k[1] * np.take(p, range(1, a.shape[ax] + 1), axis=ax) + k[2] * np.take(p, range(2, a.shape[ax] + 2), axis=ax)
        return a
    r = max(1, int(round(math.sqrt(12 * sigma * sigma / 3 + 1) - 1) / 2))
    for _ in range(3):
        a = _box(_box(a, r, 0), r, 1)
    return a


def smoothstep(e0, e1, x):
    t = np.clip((x - e0) / (e1 - e0), 0, 1)
    return t * t * (3 - 2 * t)


# ------------------------------------------------------------------ VTF
def write_bgra(path, rgba, flags=0x4 | 0x8 | 0x200 | 0x2000, mips=True):
    """uncompressed BGRA8888 (small textures: grain, motes - DXT blocks would show)"""
    h, w = rgba.shape[:2]
    levels = [rgba.astype(np.float32)]
    while mips and (levels[-1].shape[0] > 1 or levels[-1].shape[1] > 1):
        p = levels[-1]
        if p.shape[0] > 1:
            p = 0.5 * (p[0::2] + p[1::2])
        if p.shape[1] > 1:
            p = 0.5 * (p[:, 0::2] + p[:, 1::2])
        levels.append(p)
    if not mips:
        flags |= 0x100
    header = struct.pack("<4sIIIHHIHH4x3f4xfIBiBBH",
                         b"VTF\0", 7, 2, 80, w, h, flags, 1, 0,
                         0.1, 0.1, 0.1, 1.0,
                         12, len(levels), -1, 0, 0, 1).ljust(80, b"\0")
    data = bytearray()
    for lv in reversed(levels):
        b = np.clip(lv + 0.5, 0, 255).astype(np.uint8)
        data += b[..., [2, 1, 0, 3]].tobytes()
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "wb") as f:
        f.write(header + data)


def write_dxt(path, rgba, alpha):
    # clamp s/t, no LOD: the menu stays sharp with mat_picmip 1
    hd_textures.write_vtf(path, rgba.astype(np.uint8), 0x4 | 0x8 | 0x200, alpha)


def write_vmt(name, additive=False, wrap=False):
    lines = ['"UnlitGeneric"', "{", '\t"$basetexture" "vgui/hl2rpm/menu/%s"' % name,
             '\t"$translucent" "1"', '\t"$vertexcolor" "1"', '\t"$vertexalpha" "1"',
             '\t"$ignorez" "1"', '\t"$nolod" "1"']
    if additive:
        lines.append('\t"$additive" "1"')
    lines.append("}")
    with open(os.path.join(OUT, name + ".vmt"), "w", newline="\n") as f:
        f.write("\n".join(lines) + "\n")


def rgba_u8(gray, alpha):
    g = np.clip(gray, 0, 255)
    return np.dstack([g, g, g, np.clip(alpha * 255, 0, 255)]).astype(np.uint8)


# ------------------------------------------------------------------ the lambda
def lambda_mask(size, height_frac, center=(0.5, 0.5)):
    """the lambda of halflife2.ttf ('A'), anti-aliased, its ink height = height_frac of size"""
    S = 4 * size
    probe = ImageFont.truetype(HL2_FONT, 1000)
    img = Image.new("L", (1600, 1600), 0)
    ImageDraw.Draw(img).text((100, 0), "A", font=probe, fill=255)
    bb = img.getbbox()
    glyph = img.crop(bb)
    gh = int(S * height_frac)
    gw = int(glyph.width * gh / glyph.height)
    glyph = glyph.resize((gw, gh), Image.LANCZOS)
    canvas = Image.new("L", (S, S), 0)
    canvas.paste(glyph, (int(S * center[0] - gw / 2), int(S * center[1] - gh / 2)))
    return np.asarray(canvas.resize((size, size), Image.LANCZOS), np.float32) / 255


def ring_mask(size, r0, r1, center=(0.5, 0.5)):
    S = 4 * size
    img = Image.new("L", (S, S), 0)
    d = ImageDraw.Draw(img)
    cx, cy = S * center[0], S * center[1]
    d.ellipse((cx - r0 * S, cy - r0 * S, cx + r0 * S, cy + r0 * S), fill=255)
    d.ellipse((cx - r1 * S, cy - r1 * S, cx + r1 * S, cy + r1 * S), fill=0)
    return np.asarray(img.resize((size, size), Image.LANCZOS), np.float32) / 255


def emblem_shape(size):
    """ring + lambda (0..1), the ring centre a little above the middle: the drips need room"""
    c = (0.5, 0.47)
    ring = ring_mask(size, 0.43, 0.352, c)
    lam = lambda_mask(size, 0.47, (0.515, 0.49))
    return np.maximum(ring, lam)


GRAFFITI_SRC = os.path.join(ART, "lambda_graffiti_src.jpg")
_GRAFFITI = None


def graffiti_cutout():
    """the user's sprayed lambda (art/lambda_graffiti_src.jpg, 2000x2000) out of its fake
    transparency: the picture came with a checkerboard drawn into it (40 px squares, ~20 and ~63
    grey; the generator drew some squares of the wrong parity or of another flat shade). Each
    square's own background is known, the paint is near white: alpha from the brightness over the
    background, the colour of the paint from the un-blending. Returns (rgb float HxWx3, alpha)."""
    global _GRAFFITI
    if _GRAFFITI is not None:
        return _GRAFFITI
    src = np.asarray(Image.open(GRAFFITI_SRC).convert("RGB"), np.float32)
    H, W = src.shape[:2]
    dark = np.array([19.9, 18.9, 21.4], np.float32)
    light = np.array([62.9, 62.9, 62.9], np.float32)
    B = np.zeros((H, W, 3), np.float32)
    nj, ni = H // 40, W // 40
    for j in range(nj):
        for i in range(ni):
            blk = src[j * 40 + 3:j * 40 + 37, i * 40 + 3:i * 40 + 37].reshape(-1, 3)
            lum = blk.mean(1)
            exp = dark if np.percentile(lum, 5) < 41 else light   # by its darkest pixels
            ok = np.abs(lum - exp.mean()) < 6
            if lum.std() < 2.5 and lum.mean() < 100:
                col = blk.mean(0)                                  # a flat square of another shade
            else:
                col = blk[ok].mean(0) if ok.mean() > 0.5 else exp
            B[j * 40:(j + 1) * 40, i * 40:(i + 1) * 40] = col
    # (JPEG softened the square edges: so is the background)
    B = np.dstack([blur(B[..., c], 0.9) for c in range(3)])
    L, Lb = src.mean(2), B.mean(2)
    Lf = 246.0
    alpha = np.clip((L - Lb - 4.0) / (Lf - Lb - 4.0), 0, 1)
    # squares a little off the checker's shade leave a flat floor of alpha with square edges:
    # a faint square's floor above its neighbours' goes (real mist runs across the squares)
    floor = np.zeros((nj, ni), np.float32)
    for j in range(nj):
        for i in range(ni):
            floor[j, i] = np.percentile(alpha[j * 40 + 4:j * 40 + 36, i * 40 + 4:i * 40 + 36], 15)
    pad = np.pad(floor, 1, mode="edge")
    nb = np.stack([pad[1 + dj:1 + dj + nj, 1 + di:1 + di + ni] for dj in (-1, 0, 1) for di in (-1, 0, 1) if dj or di])
    excess = np.clip(floor - np.median(nb, 0), 0, None)
    excess[(excess < 0.004) | (floor > 0.08)] = 0
    alpha = np.clip(alpha - blur(np.kron(excess, np.ones((40, 40), np.float32)), 0.9), 0, 1)
    F = B + (src - B) / np.maximum(alpha, 1e-3)[..., None]
    paint = np.array([238, 235, 228], np.float32)
    wf = smoothstep(0.08, 0.35, alpha)[..., None]
    F = np.clip(paint * (1 - wf) + F * wf, 0, 255)
    _GRAFFITI = (F, alpha)
    return F, alpha


def graffiti_emblem(size=2048):
    """the graffiti placed as the old emblem was: the ring's centre at (0.5, 0.47) of the square,
    its outer radius 0.43 (the drips run down below it). Returns (rgb HxWx3, alpha)."""
    F, A = graffiti_cutout()
    H, W = A.shape
    # the ring: the outer edge along rays from a first guess of its centre (the drips are thin -
    # the blurred paint is not over 0.5 there)
    soft = blur(A, 6)
    cx, cy = 1055.0, 950.0
    for _ in range(2):
        pts = []
        for ang in np.radians(np.arange(0, 360, 3)):
            dx, dy = math.cos(ang), math.sin(ang)
            last = None
            for r in range(500, 1000):
                x, y = int(cx + dx * r), int(cy + dy * r)
                if not (0 <= x < W and 0 <= y < H):
                    break
                if soft[y, x] > 0.5:
                    last = (x, y)
            if last:
                pts.append(last)
        P = np.array(pts, np.float64)
        # least squares circle: x^2 + y^2 + D x + E y + F = 0
        M = np.column_stack([P[:, 0], P[:, 1], np.ones(len(P))])
        D, E, Fc = np.linalg.lstsq(M, -(P[:, 0] ** 2 + P[:, 1] ** 2), rcond=None)[0]
        cx, cy = -D / 2, -E / 2
        R = math.sqrt(cx * cx + cy * cy - Fc)
    k = 0.43 * size / R
    out_rgb = np.zeros((size, size, 3), np.float32)
    out_a = np.zeros((size, size), np.float32)
    sw, sh = int(round(W * k)), int(round(H * k))
    rgbk = np.asarray(Image.fromarray(np.clip(F * A[..., None], 0, 255).astype(np.uint8)).resize((sw, sh), Image.LANCZOS), np.float32)
    ak = np.asarray(Image.fromarray((A * 255).astype(np.uint8)).resize((sw, sh), Image.LANCZOS), np.float32) / 255
    ox, oy = int(round(0.5 * size - cx * k)), int(round(0.47 * size - cy * k))
    sx0, sy0, dx0, dy0 = max(0, -ox), max(0, -oy), max(0, ox), max(0, oy)
    w_, h_ = min(sw - sx0, size - dx0), min(sh - sy0, size - dy0)
    out_rgb[dy0:dy0 + h_, dx0:dx0 + w_] = rgbk[sy0:sy0 + h_, sx0:sx0 + w_]
    out_a[dy0:dy0 + h_, dx0:dx0 + w_] = ak[sy0:sy0 + h_, sx0:sx0 + w_]
    # back from premultiplied (the resize must not darken the edges)
    out_rgb = np.clip(out_rgb / np.maximum(out_a, 1 / 255)[..., None], 0, 255)
    print("   graffiti ring: centre %.0f %.0f, radius %.0f px of the source; scale %.3f" % (cx, cy, R, k))
    return out_rgb, out_a


def drips(shape, n, seed, max_len):
    """paint running down from the lower edges of the shape"""
    h, w = shape.shape
    r = np.random.default_rng(seed)
    out = np.zeros_like(shape)
    solid = shape > 0.5
    below = np.zeros_like(solid)
    below[:-1] = solid[:-1] & ~solid[1:]           # a lower edge
    ys, xs = np.nonzero(below)
    if len(ys) == 0:
        return out
    pick = r.choice(len(ys), size=min(n, len(ys)), replace=False)
    for i in pick:
        x0, y0 = xs[i], ys[i]
        length = int(r.uniform(0.15, 1.0) ** 2 * max_len) + 6
        width = r.uniform(1.2, 4.5) * w / 2048
        strength = r.uniform(0.35, 0.85)
        wob = r.uniform(-0.6, 0.6)
        for k in range(length):
            y = y0 + k
            if y >= h:
                break
            t = k / length
            x = x0 + wob * k * 0.02 * math.sin(k * 0.05)
            wd = width * (1.0 - 0.55 * t) + (1.6 * width if k > length - 6 else 0) * (1 - (length - k) / 6)
            a = strength * (1 - t) ** 0.6
            xl, xr = int(x - wd), int(x + wd) + 1
            for xx in range(max(0, xl), min(w, xr + 1)):
                cov = np.clip(wd + 0.5 - abs(xx - x), 0, 1)
                out[y, xx] = max(out[y, xx], a * cov)
    return out


def worn_paint(shape, seed, base=136.0, drip_count=45, drip_len=None):
    """gray paint on a wall: blotches, chipped edges, holes, drips (gray, alpha)"""
    h, w = shape.shape
    k = w / 2048.0
    drip_len = drip_len or int(300 * k)
    tone = base + 20 * fbm(h, w, int(256 * k) or 1, 4, seed) + 8 * fbm(h, w, int(24 * k) or 1, 2, seed + 7)
    tone += 7 * fbm(h, w, max(1, int(3 * k)), 2, seed + 8)              # the wall under the paint
    dark = smoothstep(0.15, 0.6, fbm(h, w, int(150 * k) or 1, 4, seed + 3))
    tone -= 42 * dark
    tone += 16 * smoothstep(0.3, 0.7, fbm(h, w, int(300 * k) or 1, 3, seed + 4))
    # chipped edges: noise eats into the shape near its border
    edge = blur(shape, 6 * k)
    edge_zone = 1 - np.abs(edge * 2 - 1)               # 1 at the border, 0 deep inside / outside
    chip = fbm(h, w, int(14 * k) or 1, 3, seed + 11)
    holes_fine = fbm(h, w, int(5 * k) or 1, 2, seed + 13)
    patches = smoothstep(-0.1, 0.5, fbm(h, w, int(90 * k) or 1, 3, seed + 14))   # where the paint flakes
    alpha = shape.copy()
    alpha *= 1 - smoothstep(0.05, 0.35, chip + edge_zone * 0.55 - 0.25) * edge_zone
    alpha *= 1 - 0.9 * smoothstep(0.62 - 0.22 * patches, 0.75 - 0.2 * patches, holes_fine + 0.1 * chip)
    big_wear = smoothstep(0.3, 0.75, fbm(h, w, int(120 * k) or 1, 4, seed + 17))
    alpha *= 1 - 0.6 * big_wear
    # overspray around the edges
    spray = blur(shape, 10 * k) * (1 - shape) * 0.18 * (0.5 + 0.5 * fbm(h, w, int(3 * k) or 1, 1, seed + 19))
    d = drips(shape, drip_count, seed + 23, drip_len)
    d *= 0.6 + 0.4 * smoothstep(-0.2, 0.4, fbm(h, w, int(30 * k) or 1, 2, seed + 29))
    a = np.clip(np.maximum(alpha * 0.93, d * 0.75) + spray, 0, 1)
    tone = np.where(d > alpha, tone - 18, tone)
    return tone, a


# ------------------------------------------------------------------ the wall
def make_background_image():
    """the wall as a 3840x2160 image"""
    W, H = 3840, 2160
    print("background...")
    y, x = np.mgrid[0:H, 0:W].astype(np.float32)
    u, v = x / W, y / H
    # light behind the emblem, dark corners
    glow = np.exp(-(((u - 0.72) / 0.42) ** 2 + ((v - 0.47) / 0.55) ** 2))
    base = 16.0 + 14.0 * glow
    base += 5.0 * fbm(H, W, 512, 5, 3)
    base += 2.5 * fbm(H, W, 40, 3, 5)
    pores = fbm(H, W, 3, 2, 6)
    base += 2.2 * pores - 4.0 * smoothstep(0.55, 0.8, fbm(H, W, 6, 2, 7))       # concrete
    stains = smoothstep(0.3, 0.8, fbm(H, W, 700, 3, 9))
    base -= 4.0 * stains
    base += 3.0 * smoothstep(0.45, 0.9, fbm(H, W, 300, 4, 31))
    # streaks running down the wall
    streak = smooth_noise(H, W // 3, 1, 41)
    streak = np.asarray(Image.fromarray(((streak + 1) / 2).astype(np.float32), "F").resize((W, H // 60), Image.BILINEAR).resize((W, H), Image.BICUBIC), np.float32)
    base += 2.5 * (streak - 0.5) * (0.4 + 0.6 * v)
    # scratches and specks
    img = Image.fromarray(np.zeros((H, W), np.float32), "F")
    d = ImageDraw.Draw(img)
    r = np.random.default_rng(77)
    for _ in range(220):
        x0, y0 = r.uniform(0, W), r.uniform(0, H)
        ang = r.normal(0.3, 0.9)
        ln = r.uniform(20, 160)
        d.line((x0, y0, x0 + ln * math.cos(ang), y0 + ln * math.sin(ang)), fill=float(r.uniform(3, 8)), width=1)
    for _ in range(2600):
        x0, y0 = r.uniform(0, W), r.uniform(0, H)
        s = r.uniform(0.6, 2.2)
        d.ellipse((x0 - s, y0 - s, x0 + s, y0 + s), fill=float(r.uniform(4, 16)))
    base += blur(np.asarray(img, np.float32), 0.7)
    base += 1.6 * r.standard_normal((H, W)).astype(np.float32)
    rgb = np.dstack([base * 0.99, base, base * 1.02])
    return Image.fromarray(np.clip(rgb, 0, 255).astype(np.uint8))


def make_background():
    img = make_background_image()
    a = np.asarray(img.resize((4096, 2048), Image.LANCZOS))
    write_dxt(os.path.join(OUT, "bg.vtf"), np.dstack([a, np.full(a.shape[:2], 255, np.uint8)]), False)
    write_vmt("bg")
    return img


def make_emblem(size=2048, write=True):
    """the sprayed lambda in a ring (the user's graffiti, 2026-11-02; no glow around it any more:
    the soft additive halo read as a stray ring on the wall)"""
    print("emblem...")
    rgb, alpha = graffiti_emblem(size)
    if write:
        write_dxt(os.path.join(OUT, "emblem.vtf"), np.dstack([rgb, np.clip(alpha * 255, 0, 255)]).astype(np.uint8), True)
        write_vmt("emblem")
        for ext in (".vtf", ".vmt"):
            old = os.path.join(OUT, "emblem_glow" + ext)
            if os.path.exists(old):
                os.remove(old)
    return rgb, alpha


# ------------------------------------------------------------------ the title
def glyph(ch, px_cap, font=HL2_FONT):
    """one glyph of a font as an L image cropped to its ink, its cap height ~ px_cap"""
    f = ImageFont.truetype(font, 1000)
    img = Image.new("L", (2200, 1600), 0)
    ImageDraw.Draw(img).text((200, 100), ch, font=f, fill=255)
    return img.crop(img.getbbox())


def title_image(W=2048, H=512, color=196, sub_color=138, worn=True, sub="ROLE PLAY MOD"):
    """HλLF-LIFE² over ---- ROLE PLAY MOD ----, in a W x H RGBA image (concept layout)"""
    S = 2
    cap = int(0.30 * H) * S
    canvas = Image.new("L", (W * S, H * S), 0)
    href = glyph("H", cap)
    scale = cap / href.height
    x = int(0.06 * W * S)
    top = int(0.14 * H * S)
    gap = int(cap * 0.60)
    for ch in "HALF-LIFE":
        g = glyph(ch, cap)
        if ch == "E":
            # the E of this font carries a (R) mark: keep the letter only
            a = np.asarray(g)
            cols = np.nonzero(a.max(0) > 0)[0]
            gaps = np.nonzero(np.diff(cols) > 1)[0]
            if len(gaps):
                g = g.crop((0, 0, cols[gaps[0]] + 1, g.height))
                g = g.crop(g.getbbox())
        g = g.resize((max(1, int(g.width * scale)), max(1, int(g.height * scale))), Image.LANCZOS)
        if ch == "-":
            y = top + int(cap * 0.5 - g.height / 2)
        elif ch == "A":
            y = top + cap - g.height
        else:
            y = top + cap - g.height
        canvas.paste(255, (x, y), g)
        x += g.width + gap
    # the superscript 2 (the ' of the font)
    two = glyph("'", cap)
    two = two.resize((int(two.width * cap * 0.42 / two.height), int(cap * 0.42)), Image.LANCZOS)
    canvas.paste(255, (x - gap + int(cap * 0.38), top - int(cap * 0.10)), two)
    title_right = x - gap + int(cap * 0.38) + two.width
    # subtitle with lines either side
    sf = ImageFont.truetype(BAHN, int(cap * 0.52))
    try:
        sf.set_variation_by_name("SemiLight")
    except Exception:
        pass
    track = int(cap * 0.40)
    letters = []
    for ch in sub:
        bb = sf.getbbox(ch)
        letters.append((ch, bb))
    sub_w = sum((bb[2] - bb[0]) if ch != " " else int(cap * 0.25) for ch, bb in letters) + track * (len(letters) - 1)
    sub_layer = Image.new("L", canvas.size, 0)
    sd = ImageDraw.Draw(sub_layer)
    left = int(0.035 * W * S)
    right = max(title_right + int(cap * 0.25), left + sub_w + int(cap * 3.2))
    cx = (left + right) // 2 + int(cap * 0.15)
    sy = top + cap + int(cap * 0.42)
    sx = cx - sub_w // 2
    for ch, bb in letters:
        if ch == " ":
            sx += int(cap * 0.25) + track
            continue
        sd.text((sx - bb[0], sy - bb[1] - int(cap * 0.0)), ch, font=sf, fill=255)
        sx += (bb[2] - bb[0]) + track
    sub_cap = sf.getbbox("R")[3] - sf.getbbox("R")[1]
    ly = sy + sub_cap // 2
    lw = max(2, int(cap * 0.035))
    sd.rectangle((left, ly - lw // 2, cx - sub_w // 2 - int(cap * 0.45), ly + lw // 2), fill=200)
    sd.rectangle((cx + sub_w // 2 + int(cap * 0.45), ly - lw // 2, right, ly + lw // 2), fill=200)
    t = np.asarray(canvas.resize((W, H), Image.LANCZOS), np.float32) / 255
    s = np.asarray(sub_layer.resize((W, H), Image.LANCZOS), np.float32) / 255
    gray = np.where(s > t, float(sub_color), float(color)) + 10 * fbm(H, W, 64, 3, 51)
    alpha = np.maximum(t, s)
    if worn:
        alpha *= 1 - 0.55 * smoothstep(0.42, 0.62, fbm(H, W, 6, 2, 53))
        alpha *= 1 - 0.25 * smoothstep(0.2, 0.7, fbm(H, W, 90, 3, 57))
    return gray, alpha


def make_title():
    print("title...")
    gray, alpha = title_image()
    write_dxt(os.path.join(OUT, "title.vtf"), rgba_u8(gray, alpha), True)
    write_vmt("title")
    return gray, alpha


# ------------------------------------------------------------------ small things
def make_small():
    print("grain, vignette, mote, icon...")
    r = np.random.default_rng(91)
    n = r.standard_normal((256, 256)).astype(np.float32)
    n = 0.75 * n + 0.25 * blur(n, 0.8) * 3
    g = np.clip(128 + 60 * n, 0, 255)
    a = np.clip(np.abs(n) * 0.55, 0, 1)
    write_bgra(os.path.join(OUT, "grain.vtf"), np.dstack([g, g, g, a * 255]), flags=0x200 | 0x2000)
    write_vmt("grain")

    y, x = np.mgrid[0:512, 0:512].astype(np.float32)
    rr = np.sqrt(((x - 255.5) / 256) ** 2 + ((y - 255.5) / 256) ** 2)
    a = smoothstep(0.45, 1.42, rr) ** 1.2 * 0.92
    write_bgra(os.path.join(OUT, "vignette.vtf"), np.dstack([np.zeros_like(a)] * 3 + [a * 255]))
    write_vmt("vignette")

    y, x = np.mgrid[0:64, 0:64].astype(np.float32)
    rr = np.sqrt((x - 31.5) ** 2 + (y - 31.5) ** 2) / 32
    a = np.exp(-rr * rr * 9) * 0.9 + np.exp(-rr * rr * 60) * 0.1
    write_bgra(os.path.join(OUT, "mote.vtf"), np.dstack([np.full_like(a, 255)] * 3 + [np.clip(a, 0, 1) * 255]))
    write_vmt("mote", additive=True)

    # the footer's small emblem: the graffiti too, its paint as a white mask
    _rgb, ga = graffiti_emblem(1024)
    sh = np.asarray(Image.fromarray((ga * 255).astype(np.uint8)).resize((128, 128), Image.LANCZOS), np.float32) / 255
    sh = np.clip(sh * 1.25, 0, 1)
    write_bgra(os.path.join(OUT, "icon.vtf"), np.dstack([np.full_like(sh, 255)] * 3 + [sh * 255]))
    write_vmt("icon")


def preview(path, bg, etone, ealpha, ttone, talpha):
    """the menu as the game will draw it (1600x900), to look at the art without the game"""
    W, H = 1600, 900
    img = np.asarray(bg.resize((W, H), Image.LANCZOS), np.float32)
    es = int(H * 0.86)
    e_rgb = np.asarray(Image.fromarray(np.clip(etone, 0, 255).astype(np.uint8)).resize((es, es), Image.LANCZOS), np.float32)
    if e_rgb.ndim == 2:
        e_rgb = e_rgb[..., None]
    e_a = np.asarray(Image.fromarray((ealpha * 255).astype(np.uint8)).resize((es, es), Image.LANCZOS), np.float32)[..., None] / 255 * 0.92
    ex, ey = int(W * 0.725 - es / 2), int(H * 0.47 - es * 0.47)
    sub = img[ey:ey + es, ex:ex + es]
    sub[:] = sub * (1 - e_a) + e_rgb * e_a
    tw = int(W * 0.50)
    th = tw // 4
    t_rgb = np.asarray(Image.fromarray(np.clip(ttone, 0, 255).astype(np.uint8)).resize((tw, th), Image.LANCZOS), np.float32)
    t_a = np.asarray(Image.fromarray((talpha * 255).astype(np.uint8)).resize((tw, th), Image.LANCZOS), np.float32)[..., None] / 255
    tx, ty = int(W * 0.045), int(H * 0.13)
    sub = img[ty:ty + th, tx:tx + tw]
    sub[:] = sub * (1 - t_a) + t_rgb[..., None] * t_a
    Image.fromarray(np.clip(img, 0, 255).astype(np.uint8)).save(path)


def main(argv):
    os.makedirs(OUT, exist_ok=True)
    bg = make_background()
    et, ea = make_emblem()
    tt, ta = make_title()
    make_small()
    if "--preview" in argv:
        preview(argv[argv.index("--preview") + 1], bg, et, ea, tt, ta)
    print("done:", OUT)


if __name__ == "__main__":
    main(sys.argv[1:])
