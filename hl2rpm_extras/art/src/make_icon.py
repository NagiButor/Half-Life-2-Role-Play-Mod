# -*- coding: utf-8 -*-
"""HL2RPM mod icon generator (original design, no external artwork).

All geometry is vector (PIL polygons) on a 32-unit grid, rendered supersampled and box-filtered
(exact area coverage, no ringing). Wear (mottled plate, scratches, chipped paint) only from 64 px up.
16 px uses its own pixel-snapped geometry (see icon_small16).

  python -I make_icon.py concepts <out_dir>   three concepts + comparison sheet (16/32/64 on dark & light)
  python -I make_icon.py final <art_dir> <mod_resource_dir>
        -> <art_dir>/icon_256.png, icon_184.png, icon.ico (16/32/48/256), icon/ (all sizes),
           <mod_resource_dir>/icon1.tga (16), icon1_big.tga (32), icon1_thumb.tga (32)
"""
import os
import struct
import sys

import numpy as np
from PIL import Image, ImageDraw, ImageFont

BLUE = (110, 190, 255)
BLUE_HI = (160, 215, 255)
BLUE_LO = (78, 150, 225)
AMBER = (255, 170, 60)
AMBER_LO = (215, 120, 30)
SLATE_TOP = (36, 46, 56)
SLATE_BOT = (16, 21, 27)
EDGE = (6, 8, 10)


def c01(c):
    return np.array(c, np.float32) / 255.0


class Canvas:
    """Premultiplied RGBA float canvas at N*ss pixels; drawing coordinates in grid units."""

    def __init__(self, N, ss=None, grid=32.0):
        self.N = N
        self.ss = ss or max(4, int(np.ceil(1024 / N)))
        self.S = N * self.ss
        self.k = self.S / grid
        self.rgba = np.zeros((self.S, self.S, 4), np.float32)

    def mask(self, fn):
        im = Image.new('L', (self.S, self.S), 0)
        fn(ImageDraw.Draw(im), self.k)
        return np.asarray(im, np.float32) / 255.0

    def vgrad(self, top, bot, y0=0.0, y1=32.0):
        t = (np.arange(self.S, dtype=np.float32) / self.k - y0) / max(1e-3, y1 - y0)
        t = np.clip(t, 0, 1)[:, None, None]
        return c01(top)[None, None, :] * (1 - t) + c01(bot)[None, None, :] * t

    def fill(self, m, color, alpha=1.0):
        col = c01(color)[None, None, :] if isinstance(color, tuple) else color
        a = (m * alpha)[:, :, None]
        self.rgba[:, :, :3] = col * a + self.rgba[:, :, :3] * (1 - a)
        self.rgba[:, :, 3:] = a + self.rgba[:, :, 3:] * (1 - a)

    def image(self):
        N, ss = self.N, self.ss
        p = self.rgba.reshape(N, ss, N, ss, 4).mean(axis=(1, 3))
        a = p[:, :, 3:]
        rgb = np.where(a > 1e-6, p[:, :, :3] / np.maximum(a, 1e-6), 0)
        out = np.concatenate([rgb, a], axis=2)
        return Image.fromarray((np.clip(out, 0, 1) * 255 + 0.5).astype(np.uint8), 'RGBA')


def P(k, pts):
    return [(x * k, y * k) for x, y in pts]


# --------------------------------------------------------------------------------------------
# Lambda: own geometry - long diagonal with horizontal cuts, a chamfered flag at the top,
# short leg down-left. (cx, yt, yb) place it, w = stroke width (horizontal), all in grid units.
# --------------------------------------------------------------------------------------------
def lambda_polys(cx=16.0, yt=7.0, yb=25.0, w=4.4, slope=0.47, flag=3.4, flag_h=2.8, leg_w=None, leg_slope=0.56):
    leg_w = leg_w or w * 0.96
    h = yb - yt
    x0 = cx - 0.255 * h  # long stroke left edge at the top
    xl = lambda y: x0 + slope * (y - yt)
    long_ = [(xl(yt), yt), (xl(yt) + w, yt), (xl(yb) + w, yb), (xl(yb), yb)]
    fh = flag_h
    flag_ = [(xl(yt) - flag, yt), (xl(yt) + 0.2, yt), (xl(yt + fh) + 0.2, yt + fh), (xl(yt) - flag + fh * 0.55, yt + fh)]
    xb = cx - 0.445 * h  # leg bottom-left
    # leg left edge meets long stroke left edge at yj
    yj = (xb + leg_slope * yb - x0 + slope * yt) / (slope + leg_slope)
    leg = [(xb, yb), (xb + leg_w, yb), (xb + leg_w + leg_slope * (yb - yj), yj), (xb + leg_slope * (yb - yj), yj)]
    return [long_, flag_, leg]


def draw_lambda(d, k, **kw):
    for p in lambda_polys(**kw):
        d.polygon(P(k, p), fill=255)


def outline_lambda(d, k, r, **kw):
    """Lambda grown by r grid units (filled polygons + round-jointed outline of width 2r)."""
    for p in lambda_polys(**kw):
        pts = P(k, p)
        d.polygon(pts, fill=255)
        d.line(pts + [pts[0]], fill=255, width=max(1, int(2 * r * k)), joint='curve')
        for x, y in pts:
            d.ellipse([x - r * k, y - r * k, x + r * k, y + r * k], fill=255)


# --------------------------------------------------------------------------------------------
# Wear
# --------------------------------------------------------------------------------------------
def value_noise(S, cells, seed):
    rng = np.random.default_rng(seed)
    g = rng.random((cells + 1, cells + 1)).astype(np.float32)
    im = Image.fromarray((g * 255).astype(np.uint8), 'L').resize((S, S), Image.BICUBIC)
    return np.asarray(im, np.float32) / 255.0


def fractal(S, seed, octaves=(4, 9, 20, 44)):
    n = np.zeros((S, S), np.float32)
    amp, tot = 1.0, 0.0
    for i, c in enumerate(octaves):
        n += value_noise(S, c, seed + i) * amp
        tot += amp
        amp *= 0.55
    return n / tot


def scratches(cv, region, seed, count=26, alpha=0.10, color=(200, 220, 235)):
    rng = np.random.default_rng(seed)

    def fn(d, k):
        for _ in range(count):
            x, y = rng.uniform(3, 29), rng.uniform(3, 29)
            ang = rng.uniform(-0.6, 0.6) + (np.pi / 2 if rng.random() < 0.3 else 0)
            ln = rng.uniform(1.0, 5.5)
            x2, y2 = x + np.cos(ang) * ln, y + np.sin(ang) * ln
            d.line([(x * k, y * k), (x2 * k, y2 * k)], fill=int(rng.uniform(120, 255)),
                   width=max(1, int(k * rng.uniform(0.05, 0.12))))

    m = cv.mask(fn) * region
    cv.fill(m, color, alpha)


def chips(cv, region, seed, count=14, color=SLATE_BOT, alpha=0.85):
    rng = np.random.default_rng(seed)

    def fn(d, k):
        for _ in range(count):
            x, y = rng.uniform(1, 31), rng.uniform(1, 31)
            r = rng.uniform(0.12, 0.45)
            pts = [(x + r * np.cos(a) * rng.uniform(0.5, 1.3), y + r * np.sin(a) * rng.uniform(0.5, 1.3))
                   for a in np.linspace(0, 2 * np.pi, 7)[:-1]]
            d.polygon(P(k, pts), fill=255)

    m = cv.mask(fn) * region
    cv.fill(m, color, alpha)


def plate_fill(cv, m, worn, seed, top=SLATE_TOP, bot=SLATE_BOT):
    col = cv.vgrad(top, bot)
    if worn:
        n = fractal(cv.S, seed)
        col = col * (0.88 + 0.24 * n)[:, :, None]
    cv.fill(m, col)


def lambda_fill(cv, m, worn, seed):
    col = cv.vgrad(BLUE_HI, BLUE_LO, 5, 27)
    if worn:
        n = fractal(cv.S, seed + 50, (6, 14, 30))
        col = col * (0.9 + 0.14 * n)[:, :, None]
    cv.fill(m, col)
    if worn:
        chips(cv, m, seed + 7, count=10)


# --------------------------------------------------------------------------------------------
# Concepts
# --------------------------------------------------------------------------------------------
def concept_bracelet(N, worn=None, seed=1):
    """A: ID bracelet - strap across, display module with blue bezel, lambda, amber status LED."""
    worn = N >= 64 if worn is None else worn
    cv = Canvas(N)
    strap = cv.mask(lambda d, k: d.rounded_rectangle([0.4 * k, 10 * k, 31.6 * k, 22 * k], radius=1.6 * k, fill=255))
    plate_fill(cv, strap, worn, seed, (44, 52, 60), (22, 27, 33))
    if N >= 32:  # strap stitching / ribs
        rib = cv.mask(lambda d, k: [d.rectangle([x * k, 11.2 * k, (x + 0.5) * k, 20.8 * k], fill=255)
                                    for x in (1.6, 2.9, 28.6, 29.9)])
        cv.fill(rib, EDGE, 0.55)
    body = cv.mask(lambda d, k: d.rounded_rectangle([4.6 * k, 1.4 * k, 27.4 * k, 30.6 * k], radius=4.2 * k, fill=255))
    cv.fill(body, EDGE)
    bezel = cv.mask(lambda d, k: d.rounded_rectangle([5.6 * k, 2.4 * k, 26.4 * k, 29.6 * k], radius=3.4 * k, fill=255))
    cv.fill(bezel, cv.vgrad(BLUE_HI, BLUE_LO, 2, 30))
    disp = cv.mask(lambda d, k: d.rounded_rectangle([7.4 * k, 4.2 * k, 24.6 * k, 27.8 * k], radius=2.0 * k, fill=255))
    plate_fill(cv, disp, worn, seed + 1, (24, 32, 40), (10, 14, 18))
    lam = cv.mask(lambda d, k: draw_lambda(d, k, cx=16.0, yt=7.6, yb=24.6, w=4.2, flag=3.0))
    lambda_fill(cv, lam, worn, seed)
    led = cv.mask(lambda d, k: d.rectangle([21.0 * k, 6.0 * k, 23.0 * k, 8.0 * k], fill=255))
    cv.fill(led, AMBER)
    if worn:
        scratches(cv, bezel * (1 - disp), seed + 3, count=30, alpha=0.18, color=(20, 30, 40))
        scratches(cv, disp, seed + 4, count=16, alpha=0.06)
    return cv.image()


def octagon(x0, y0, x1, y1, c):
    return [(x0 + c, y0), (x1 - c, y0), (x1, y0 + c), (x1, y1 - c), (x1 - c, y1), (x0 + c, y1), (x0, y1 - c), (x0, y0 + c)]


def concept_plate(N, worn=None, seed=2):
    """B: Combine registry plate - chamfered octagon, blue inset rim, lambda, amber registry bar."""
    worn = N >= 64 if worn is None else worn
    cv = Canvas(N)
    outer = cv.mask(lambda d, k: d.polygon(P(k, octagon(0.8, 0.8, 31.2, 31.2, 7.0)), fill=255))
    cv.fill(outer, EDGE)
    plate = cv.mask(lambda d, k: d.polygon(P(k, octagon(1.6, 1.6, 30.4, 30.4, 6.6)), fill=255))
    plate_fill(cv, plate, worn, seed)
    rim_o = cv.mask(lambda d, k: d.polygon(P(k, octagon(3.0, 3.0, 29.0, 29.0, 6.0)), fill=255))
    rim_i = cv.mask(lambda d, k: d.polygon(P(k, octagon(5.0, 5.0, 27.0, 27.0, 5.2)), fill=255))
    rim = np.clip(rim_o - rim_i, 0, 1)
    cv.fill(rim, cv.vgrad(BLUE_HI, BLUE_LO, 3, 29))
    lam = cv.mask(lambda d, k: draw_lambda(d, k, cx=16.0, yt=7.4, yb=22.4, w=4.0, flag=2.8, flag_h=2.5))
    lambda_fill(cv, lam, worn, seed)
    bar = cv.mask(lambda d, k: d.rectangle([10.0 * k, 23.8 * k, 22.0 * k, 25.6 * k], fill=255))
    cv.fill(bar, cv.vgrad(AMBER, AMBER_LO, 23.8, 25.6))
    if N >= 48:
        ticks = cv.mask(lambda d, k: [d.rectangle([x * k, 23.8 * k, (x + w) * k, 25.6 * k], fill=255)
                                      for x, w in ((12.2, 0.35), (13.4, 0.7), (15.6, 0.35), (17.0, 0.35), (18.4, 0.8), (20.4, 0.35))])
        cv.fill(ticks, EDGE, 0.9)
    if worn:
        chips(cv, rim, seed + 11, count=16)
        scratches(cv, plate * (1 - rim), seed + 3, count=24, alpha=0.07)
    return cv.image()


def concept_brackets(N, worn=None, seed=3):
    """C: scanner brackets - dark tile, four blue corner brackets, amber scan line behind the lambda."""
    worn = N >= 64 if worn is None else worn
    cv = Canvas(N)
    outer = cv.mask(lambda d, k: d.rounded_rectangle([0.8 * k, 0.8 * k, 31.2 * k, 31.2 * k], radius=5.5 * k, fill=255))
    cv.fill(outer, EDGE)
    plate = cv.mask(lambda d, k: d.rounded_rectangle([1.6 * k, 1.6 * k, 30.4 * k, 30.4 * k], radius=4.8 * k, fill=255))
    plate_fill(cv, plate, worn, seed)

    def br(d, k):
        a, t, i = 7.0, 2.0, 3.6
        for (x, y, sx, sy) in ((i, i, 1, 1), (32 - i, i, -1, 1), (i, 32 - i, 1, -1), (32 - i, 32 - i, -1, -1)):
            d.rectangle(sorted_rect(x, y, x + sx * a, y + sy * t, k), fill=255)
            d.rectangle(sorted_rect(x, y, x + sx * t, y + sy * a, k), fill=255)

    brm = cv.mask(br)
    cv.fill(brm, cv.vgrad(BLUE_HI, BLUE_LO, 3, 29))
    scan = cv.mask(lambda d, k: d.rectangle([6.2 * k, 15.0 * k, 25.8 * k, 16.8 * k], fill=255))
    cv.fill(scan, AMBER, 0.95)
    lkw = dict(cx=16.0, yt=7.0, yb=25.0, w=4.4, flag=3.2)
    lam = cv.mask(lambda d, k: draw_lambda(d, k, **lkw))
    knock = np.clip(cv.mask(lambda d, k: outline_lambda(d, k, 0.9, **lkw)) - lam, 0, 1)
    cv.fill(knock, (12, 16, 20))
    lambda_fill(cv, lam, worn, seed)
    if worn:
        chips(cv, brm, seed + 11, count=12)
        scratches(cv, plate * (1 - brm), seed + 3, count=24, alpha=0.07)
    return cv.image()


def sorted_rect(x0, y0, x1, y1, k):
    return [min(x0, x1) * k, min(y0, y1) * k, max(x0, x1) * k, max(y0, y1) * k]


CONCEPTS = [('A  ID bracelet', concept_bracelet), ('B  Registry plate', concept_plate), ('C  Scanner brackets', concept_brackets)]


# --------------------------------------------------------------------------------------------
# Sheets
# --------------------------------------------------------------------------------------------
def label_font(sz):
    for p in ('consola.ttf', 'arial.ttf'):
        fp = os.path.join(os.environ.get('WINDIR', 'C:/Windows'), 'Fonts', p)
        if os.path.exists(fp):
            return ImageFont.truetype(fp, sz)
    return ImageFont.load_default()


def compare_sheet(rows, out, title):
    """rows: [(label, {size: RGBA image}, big RGBA image)]. Each size at 1x and x4 nearest, on dark and light."""
    bgs = [((27, 40, 56), 'Steam dark'), ((14, 20, 27), 'near-black'), ((236, 238, 240), 'light')]
    cell = 64 * 4 + 16
    W = 40 + 300 + len(bgs) * (3 * 1 + 1) * 0 + 1600
    row_h = 330
    H = 70 + row_h * len(rows)
    sheet = Image.new('RGB', (W, H), (40, 44, 48))
    d = ImageDraw.Draw(sheet)
    f, fs = label_font(20), label_font(14)
    d.text((20, 18), title, font=f, fill=(230, 230, 230))
    for r, (lab, imgs, big) in enumerate(rows):
        y0 = 60 + r * row_h
        d.text((20, y0 + 4), lab, font=f, fill=(255, 200, 120))
        bg = Image.new('RGB', (276, 276), (27, 40, 56))
        bg.paste(big, (10, 10), big)
        sheet.paste(bg, (20, y0 + 36))
        x = 320
        for col, name in bgs:
            panel = Image.new('RGB', (420, 300), col)
            pd = ImageDraw.Draw(panel)
            pd.text((8, 4), name, font=fs, fill=(128, 128, 128))
            # 1x row
            px = 10
            for s in (16, 32, 64):
                im = imgs[s]
                panel.paste(im, (px, 28 + (64 - s) // 2), im)
                px += s + 14
            # magnified 16 (x4) and 32 (x4)
            m16 = imgs[16].resize((64, 64), Image.NEAREST)
            m32 = imgs[32].resize((128, 128), Image.NEAREST)
            panel.paste(m16, (10, 110), m16)
            panel.paste(m32, (90, 110), m32)
            pd.text((10, 245), '16 x4', font=fs, fill=(128, 128, 128))
            pd.text((90, 245), '32 x4', font=fs, fill=(128, 128, 128))
            m64 = imgs[64].resize((128, 128), Image.NEAREST)
            panel.paste(m64, (232, 110), m64)
            pd.text((232, 245), '64 x2', font=fs, fill=(128, 128, 128))
            sheet.paste(panel, (x, y0 + 12))
            x += 432
    sheet.save(out)
    print('saved', out, sheet.size)


def concepts(out_dir):
    os.makedirs(out_dir, exist_ok=True)
    rows = []
    for lab, fn in CONCEPTS:
        imgs = {s: fn(s) for s in (16, 32, 64)}
        big = fn(256)
        tag = lab.split()[0]
        big.save(os.path.join(out_dir, 'icon_concept_%s_256.png' % tag))
        rows.append((lab, imgs, big))
    compare_sheet(rows, os.path.join(out_dir, 'icon_concepts_compare.png'),
                  'HL2RPM icon concepts - 16 / 32 / 64 px at 1x, then x4 / x2 nearest; on Steam dark, near-black, light')


# --------------------------------------------------------------------------------------------
# Final design: concept A refined. 32-grid geometry on whole units (crisp at 32/64/128/256),
# steel-blue bezel darker than the lambda (the lambda is the brightest thing), screen glow and
# scanlines on the display, wear from 64 px. 16 px: own geometry on the 16-pixel grid.
# --------------------------------------------------------------------------------------------
STEEL_HI = (96, 164, 226)
STEEL_LO = (44, 92, 146)
GUN_HI = (82, 90, 98)
GUN_LO = (34, 39, 45)
LAMBDA_HI = (178, 226, 255)
LAMBDA_LO = (96, 178, 248)


def rr(x0, y0, x1, y1, r):
    """rounded rect in grid units -> drawing fn (exact pixel coverage at supersampled res)."""
    return lambda d, k: d.rounded_rectangle([x0 * k, y0 * k, x1 * k - 1, y1 * k - 1], radius=r * k, fill=255)


def rect(x0, y0, x1, y1):
    return lambda d, k: d.rectangle([x0 * k, y0 * k, x1 * k - 1, y1 * k - 1], fill=255)


def blur_mask(cv, m, radius_units):
    from PIL import ImageFilter
    im = Image.fromarray((np.clip(m, 0, 1) * 255).astype(np.uint8), 'L')
    im = im.filter(ImageFilter.GaussianBlur(radius_units * cv.k))
    return np.asarray(im, np.float32) / 255.0


def shift(m, dy_px):
    out = np.zeros_like(m)
    if dy_px > 0:
        out[dy_px:] = m[:-dy_px]
    else:
        out[:dy_px] = m[-dy_px:]
    return out


LAMBDA32 = dict(cx=16.0, yt=8.0, yb=24.0, w=4.0, flag=3.0, flag_h=3.0)


def icon_final(N, worn=None, seed=11):
    worn = N >= 64 if worn is None else worn
    rich = N >= 64
    cv = Canvas(N)
    # strap (gunmetal), ribs
    strap = cv.mask(rr(0, 10, 32, 22, 2))
    plate_fill(cv, strap, worn, seed, GUN_HI, GUN_LO)
    if rich:
        cv.fill(strap * (1 - shift(strap, max(1, int(0.5 * cv.k)))), (150, 160, 170), 0.35)  # top edge light
        ribs = cv.mask(lambda d, k: [rect(x, 11, x + 0.5, 21)(d, k) for x in (1.0, 2.5, 29.0, 30.5)])
        cv.fill(ribs, EDGE, 0.55)
    else:
        ribs = cv.mask(lambda d, k: [rect(x, 11, x + 1, 21)(d, k) for x in (1, 30)])
        cv.fill(ribs, EDGE, 0.35)
    # module: dark rim, steel bezel with bevel, display
    body = cv.mask(rr(4, 1, 28, 31, 4.5))
    cv.fill(body, EDGE)
    bezel = cv.mask(rr(5, 2, 27, 30, 3.5))
    cv.fill(bezel, cv.vgrad(STEEL_HI, STEEL_LO, 2, 30))
    disp = cv.mask(rr(7, 4, 25, 28, 2))
    if rich:
        sp = max(1, int(0.5 * cv.k))
        ring = bezel * (1 - disp)
        cv.fill(np.clip(ring - shift(ring, sp), 0, 1) * ring, (190, 228, 255), 0.55)  # top bevel
        cv.fill(np.clip(ring - shift(ring, -sp), 0, 1) * ring, (10, 20, 34), 0.45)    # bottom bevel
    plate_fill(cv, disp, worn, seed + 1, (24, 33, 42), (8, 11, 15))
    lam = cv.mask(lambda d, k: draw_lambda(d, k, **LAMBDA32))
    led = cv.mask(rect(21, 6, 23, 8))
    if rich:
        lines = cv.mask(lambda d, k: [rect(7, y, 25, y + 0.35)(d, k) for y in np.arange(4.5, 28, 1.0)])
        cv.fill(lines * disp, (0, 0, 0), 0.22)
        cv.fill(blur_mask(cv, lam, 1.1) * disp, BLUE, 0.38)       # screen glow of the lambda
        cv.fill(blur_mask(cv, led, 0.9) * disp, AMBER, 0.45)
        inner = blur_mask(cv, 1 - disp, 1.2) * disp                # display vignette
        cv.fill(inner, (0, 0, 0), 0.35)
    lambda_fill(cv, lam, worn, seed)
    cv.fill(lam, cv.vgrad(LAMBDA_HI, LAMBDA_LO, 8, 24), 0.55 if worn else 1.0)
    cv.fill(led, cv.vgrad((255, 205, 120), AMBER_LO, 6, 8))
    if worn:
        ring = bezel * (1 - disp)
        scratches(cv, ring, seed + 3, count=34, alpha=0.22, color=(16, 26, 38))
        scratches(cv, ring, seed + 5, count=12, alpha=0.20, color=(200, 232, 255))
        chips(cv, ring, seed + 9, count=18, color=(30, 36, 42), alpha=0.9)
        scratches(cv, disp * (1 - lam), seed + 4, count=14, alpha=0.05)
        scratches(cv, strap * (1 - body), seed + 6, count=18, alpha=0.12, color=(170, 180, 190))
    return cv.image()


def icon_final16():
    cv = Canvas(16, ss=64, grid=16.0)
    strap = cv.mask(rr(0, 5, 16, 11, 1))
    cv.fill(strap, cv.vgrad(GUN_HI, GUN_LO, 5, 11))
    bezel = cv.mask(rr(2, 0, 14, 16, 2.5))
    cv.fill(bezel, cv.vgrad(STEEL_HI, STEEL_LO, 0, 16))
    disp = cv.mask(rr(3, 1, 13, 15, 1.5))
    cv.fill(disp, cv.vgrad((22, 30, 38), (8, 11, 15), 1, 15))
    led = cv.mask(rect(11, 2, 12, 3))
    cv.fill(led, AMBER)
    img = np.asarray(cv.image(), np.float32) / 255.0
    # lambda as hand-placed pixels ('#' full, '+' half coverage)
    for y, row in enumerate(LAMBDA16):
        t = (y - 3) / 9.0
        col = c01(LAMBDA_HI) * (1 - t) + c01(LAMBDA_LO) * t
        for x, ch in enumerate(row):
            a = {'#': 1.0, '+': 0.5}.get(ch, 0.0)
            if a:
                img[y, x, :3] = col * a + img[y, x, :3] * (1 - a)
                img[y, x, 3] = max(img[y, x, 3], a)
    return Image.fromarray((np.clip(img, 0, 1) * 255 + 0.5).astype(np.uint8), 'RGBA')


LAMBDA16 = [
    '................',
    '................',
    '................',
    '....####........',
    '......##........',
    '......##+.......',
    '.......##.......',
    '.......##+......',
    '......####......',
    '.....+##.##.....',
    '.....##..##+....',
    '....+##...##....',
    '....##....##+...',
    '................',
    '................',
    '................',
]


def render_final(N):
    return icon_final16() if N == 16 else icon_final(N)


def refine_sheet(out_dir):
    rows = [('A  concept', {s: concept_bracelet(s) for s in (16, 32, 64)}, concept_bracelet(256)),
            ('A  refined', {s: render_final(s) for s in (16, 32, 64)}, render_final(256))]
    compare_sheet(rows, os.path.join(out_dir, 'icon_refined_compare.png'),
                  'HL2RPM icon - concept A vs refined (pixel-aligned 32-grid, own 16 px, steel bezel, screen glow, wear)')


# --------------------------------------------------------------------------------------------
# Writers
# --------------------------------------------------------------------------------------------
def write_tga(img, path):
    """32-bit uncompressed true-colour TGA (type 2), bottom-left origin, 8 alpha bits, no footer -
    byte-for-byte the same layout as sourcemods/mapbase_episodic/resource/icon1*.tga."""
    img = img.convert('RGBA')
    w, h = img.size
    hdr = struct.pack('<BBBHHBHHHHBB', 0, 0, 2, 0, 0, 0, 0, 0, w, h, 32, 0x08)
    a = np.asarray(img, np.uint8)[::-1, :, [2, 1, 0, 3]]  # bottom-up BGRA
    with open(path, 'wb') as f:
        f.write(hdr)
        f.write(a.tobytes())


def write_ico(images, path):
    """ICO with BMP (DIB 32bpp + AND mask) entries for <256 and a PNG entry for 256."""
    import io
    blobs = []
    for img in images:
        w, h = img.size
        if w >= 256:
            b = io.BytesIO()
            img.save(b, 'PNG')
            blobs.append((w, h, b.getvalue()))
            continue
        a = np.asarray(img.convert('RGBA'), np.uint8)
        bgra = a[::-1, :, [2, 1, 0, 3]].tobytes()
        row = ((w + 31) // 32) * 4
        mask = bytearray(row * h)
        for y in range(h):
            for x in range(w):
                if a[h - 1 - y, x, 3] == 0:
                    mask[y * row + x // 8] |= 0x80 >> (x % 8)
        bih = struct.pack('<IiiHHIIiiII', 40, w, h * 2, 1, 32, 0, len(bgra) + len(mask), 0, 0, 0, 0)
        blobs.append((w, h, bih + bgra + bytes(mask)))
    out = struct.pack('<HHH', 0, 1, len(blobs))
    off = 6 + 16 * len(blobs)
    for w, h, data in blobs:
        out += struct.pack('<BBBBHHII', w % 256, h % 256, 0, 0, 1, 32, len(data), off)
        off += len(data)
    with open(path, 'wb') as f:
        f.write(out)
        for _, _, data in blobs:
            f.write(data)


def final(art_dir, res_dir):
    idir = os.path.join(art_dir, 'icon')
    os.makedirs(idir, exist_ok=True)
    imgs = {}
    for s in (16, 24, 32, 48, 64, 128, 184, 256, 512, 1024):
        imgs[s] = render_final(s)
        imgs[s].save(os.path.join(idir, 'icon_%d.png' % s))
    imgs[256].save(os.path.join(art_dir, 'icon_256.png'))
    imgs[184].save(os.path.join(art_dir, 'icon_184.png'))
    write_ico([imgs[16], imgs[32], imgs[48], imgs[256]], os.path.join(art_dir, 'icon.ico'))
    write_tga(imgs[16], os.path.join(res_dir, 'icon1.tga'))
    write_tga(imgs[32], os.path.join(res_dir, 'icon1_big.tga'))
    write_tga(imgs[32], os.path.join(res_dir, 'icon1_thumb.tga'))
    print('written', art_dir, res_dir)


if __name__ == '__main__':
    if sys.argv[1] == 'concepts':
        concepts(sys.argv[2])
    elif sys.argv[1] == 'refine':
        refine_sheet(sys.argv[2])
    elif sys.argv[1] == 'final':
        final(sys.argv[2], sys.argv[3])
