# -*- coding: utf-8 -*-
"""HL2RPM wordmark / logo lockups (original artwork; fonts: Fira Sans Condensed, OFL).

Lockup: bracelet emblem (make_icon.icon_final) | "HALF-LIFE 2" + "RP" registry tag
        thin Combine rule with amber start | subtitle justified to the full text width.

  python -I make_logo.py <fonts_dir> <art_dir> [backdrop.jpg ...]
    -> <art_dir>/steam/library_logo_1280x720_{en,ru}.png   (transparent)
       <art_dir>/menu_logo_1024x256_{en,ru}.png             (transparent, left-aligned)
       <art_dir>/steam/logo_lockup_{en,ru}_full.png          (transparent, full-res master)
       <art_dir>/previews/logo_*.png                         (over backdrops / menu mock)
"""
import os
import sys

import numpy as np
from PIL import Image, ImageDraw, ImageFilter, ImageFont

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import make_icon as MI  # noqa: E402

WHITE_HI = (242, 245, 248)
WHITE_LO = (196, 206, 216)
BLUE = (110, 190, 255)
AMBER = (255, 170, 60)
SUB_COL = (214, 224, 234)

# the mod's name: Half-Life 2: Role Play Mod (HL2RPM) - the same in every language
SUBTITLES = {'en': 'ROLE PLAY MOD', 'ru': 'ROLE PLAY MOD'}


def cap_font(path, cap_px):
    probe = ImageFont.truetype(path, 1000)
    cap = -probe.getbbox('H', anchor='ls')[1] / 1000.0
    return ImageFont.truetype(path, max(4, int(round(cap_px / cap))))


def run_width(font, text, track):
    return font.getlength(text) + track * (len(text) - 1)


def draw_run(d, x, y, text, font, track, fill=255):
    for i, ch in enumerate(text):
        d.text((x + font.getlength(text[:i]) + i * track, y), ch, font=font, fill=fill, anchor='ls')


def L(W, H):
    im = Image.new('L', (W, H), 0)
    return im, ImageDraw.Draw(im)


def arr(im):
    return np.asarray(im, np.float32) / 255.0


def vgrad(H, W, top, bot, y0, y1):
    t = np.clip((np.arange(H, dtype=np.float32) - y0) / max(1.0, y1 - y0), 0, 1)[:, None, None]
    return (np.array(top, np.float32)[None, None] * (1 - t) + np.array(bot, np.float32)[None, None] * t) / 255.0 \
        * np.ones((1, W, 1), np.float32)


def specks(W, H, region_scale, seed, count, rmin, rmax):
    rng = np.random.default_rng(seed)
    im, d = L(W, H)
    for _ in range(count):
        x, y = rng.uniform(0, W), rng.uniform(0, H)
        r = rng.uniform(rmin, rmax) * region_scale
        pts = [(x + r * np.cos(a) * rng.uniform(0.5, 1.4), y + r * np.sin(a) * rng.uniform(0.5, 1.4))
               for a in np.linspace(0, 2 * np.pi, 8)[:-1]]
        d.polygon(pts, fill=255)
    for _ in range(count // 3):  # hairline scratches
        x, y = rng.uniform(0, W), rng.uniform(0, H)
        a = rng.uniform(-0.5, 0.5)
        ln = rng.uniform(0.2, 1.0) * region_scale * 8
        d.line([(x, y), (x + np.cos(a) * ln, y + np.sin(a) * ln)], fill=180, width=max(1, int(region_scale * 0.25)))
    return arr(im)


class RGBA:
    def __init__(self, W, H):
        self.W, self.H = W, H
        self.p = np.zeros((H, W, 4), np.float32)  # premultiplied

    def over(self, m, col, alpha=1.0):
        c = np.array(col, np.float32)[None, None] / 255.0 if isinstance(col, tuple) else col
        a = (m * alpha)[:, :, None]
        self.p[:, :, :3] = c * a + self.p[:, :, :3] * (1 - a)
        self.p[:, :, 3:] = a + self.p[:, :, 3:] * (1 - a)

    def under(self, m, col, alpha=1.0):
        c = np.array(col, np.float32)[None, None] / 255.0
        a = (m * alpha)[:, :, None]
        inv = 1 - self.p[:, :, 3:]
        self.p[:, :, :3] += c * a * inv
        self.p[:, :, 3:] += a * inv

    def image(self):
        a = self.p[:, :, 3:]
        rgb = np.where(a > 1e-6, self.p[:, :, :3] / np.maximum(a, 1e-6), 0)
        return Image.fromarray((np.clip(np.concatenate([rgb, a], 2), 0, 1) * 255 + 0.5).astype(np.uint8), 'RGBA')


def lockup(fonts_dir, lang, C=240, emblem=True, worn=True, seed=5):
    fb = cap_font(os.path.join(fonts_dir, 'FiraSansCondensed-Bold.ttf'), C)
    fs = cap_font(os.path.join(fonts_dir, 'FiraSansCondensed-SemiBold.ttf'), 0.27 * C)
    main = 'HALF-LIFE 2'
    track = 0.035 * fb.size
    main_w = run_width(fb, main, track)
    rp_track = 0.03 * fb.size
    rp_w = run_width(fb, 'RP', rp_track)
    pad_x, pad_y = 0.30 * C, 0.24 * C
    tag_w, tag_h = rp_w + 2 * pad_x, C + 2 * pad_y
    gap = 0.34 * C
    text_w = main_w + gap + tag_w

    sub = SUBTITLES[lang]
    sub_nat = run_width(fs, sub, 0)
    if sub_nat < 0.72 * text_w:  # a short subtitle: larger letters instead of huge gaps
        fs = ImageFont.truetype(fs.path, int(fs.size * min(0.72 * text_w / sub_nat, 1.8)))
        sub_nat = run_width(fs, sub, 0)
    sub_track = (text_w - sub_nat) / (len(sub) - 1)
    if sub_track < 0.08 * fs.size:  # too long to justify -> shrink
        fs = ImageFont.truetype(fs.path, int(fs.size * text_w / (sub_nat + 0.08 * fs.size * (len(sub) - 1))))
        sub_nat = run_width(fs, sub, 0)
        sub_track = (text_w - sub_nat) / (len(sub) - 1)
    sub_cap = -fs.getbbox('H', anchor='ls')[1]

    top = 0.0
    base = top + pad_y + C
    rule_y = top + tag_h + 0.26 * C
    rule_t = max(2.0, 0.035 * C)
    sub_base = rule_y + rule_t + 0.20 * C + sub_cap
    bottom = sub_base
    E = bottom - top  # emblem height
    ex = 0.0
    tx = (E + 0.30 * C) if emblem else 0.0
    m = int(0.25 * C)  # margin for shadow
    W = int(tx + text_w + 2 * m)
    H = int(bottom + 2 * m + 0.1 * C)
    ox, oy = m, m

    cv = RGBA(W, H)
    # --- masks
    mi, md = L(W, H)
    draw_run(md, ox + tx, oy + base, main, fb, track)
    m_main = arr(mi)
    ti, td = L(W, H)
    tag_x0, tag_y0 = ox + tx + main_w + gap, oy + top
    td.rounded_rectangle([tag_x0, tag_y0, tag_x0 + tag_w, tag_y0 + tag_h], radius=0.14 * C, fill=255)
    m_tag = arr(ti)
    ti2, td2 = L(W, H)
    bw = 0.06 * C
    td2.rounded_rectangle([tag_x0 + bw, tag_y0 + bw, tag_x0 + tag_w - bw, tag_y0 + tag_h - bw], radius=0.09 * C, fill=255)
    m_tag_in = arr(ti2)
    ri, rd = L(W, H)
    draw_run(rd, tag_x0 + pad_x, oy + base, 'RP', fb, rp_track)
    m_rp = arr(ri)
    li, ld = L(W, H)
    led = 0.13 * C
    lx1, ly0 = tag_x0 + tag_w - bw - 0.12 * C, tag_y0 + bw + 0.12 * C
    ld.rectangle([lx1 - led, ly0, lx1, ly0 + led], fill=255)
    m_led = arr(li)
    rui, rud = L(W, H)
    rx0, rx1 = ox + tx, ox + tx + text_w
    rud.rectangle([rx0, oy + rule_y, rx1, oy + rule_y + rule_t], fill=255)
    for x in (rx1 - 0.0, rx1 - 0.22 * C, rx1 - 0.44 * C):  # registry ticks at the right end
        rud.rectangle([x - rule_t, oy + rule_y - 0.10 * C, x, oy + rule_y + rule_t], fill=255)
    m_rule = arr(rui)
    ami, amd = L(W, H)
    amd.rectangle([rx0, oy + rule_y - 0.05 * C, rx0 + 0.9 * C, oy + rule_y + rule_t + 0.05 * C], fill=255)
    m_amber = arr(ami)
    si, sd = L(W, H)
    draw_run(sd, rx0, oy + sub_base, sub, fs, sub_track)
    m_sub = arr(si)

    if worn:
        sp = specks(W, H, C / 100.0, seed, int(W * H / (C * C) * 22), 0.25, 0.75)
        m_main = m_main * (1 - 0.7 * sp)
        m_rp = m_rp * (1 - 0.5 * sp)
        bez = np.clip(m_tag - m_tag_in, 0, 1)
    # --- shadow (under everything)
    allm = np.clip(m_main + m_tag + m_rule + m_amber + m_sub, 0, 1)
    sh = arr(Image.fromarray((allm * 255).astype(np.uint8)).filter(ImageFilter.GaussianBlur(0.09 * C)))
    sh = np.roll(sh, int(0.04 * C), axis=0)
    # --- paint
    cv.over(m_main, vgrad(H, W, WHITE_HI, WHITE_LO, oy + base - C, oy + base))
    cv.over(m_tag, (8, 11, 14), 0.92)
    cv.over(np.clip(m_tag - m_tag_in, 0, 1), vgrad(H, W, MI.STEEL_HI, MI.STEEL_LO, tag_y0, tag_y0 + tag_h))
    glow = arr(Image.fromarray((m_rp * 255).astype(np.uint8)).filter(ImageFilter.GaussianBlur(0.10 * C)))
    cv.over(glow * m_tag_in, BLUE, 0.35)
    lines = np.zeros((H, W), np.float32)
    step = max(2, int(0.06 * C))
    lines[::step] = 1.0
    cv.over(lines * m_tag_in, (0, 0, 0), 0.25)
    cv.over(m_rp, vgrad(H, W, MI.LAMBDA_HI, MI.LAMBDA_LO, oy + base - C, oy + base))
    lg = arr(Image.fromarray((m_led * 255).astype(np.uint8)).filter(ImageFilter.GaussianBlur(0.08 * C)))
    cv.over(lg * m_tag_in, AMBER, 0.5)
    cv.over(m_led, AMBER)
    if worn:
        sc = specks(W, H, C / 100.0, seed + 1, int(W * H / (C * C) * 30), 0.2, 0.6)
        cv.over(sc * bez, (24, 30, 36), 0.8)
    cv.over(m_rule, BLUE, 0.9)
    cv.over(m_amber, AMBER)
    cv.over(m_sub, SUB_COL)
    if emblem:
        Ei = int(round(E))
        em = MI.icon_final(max(64, Ei))
        if em.size[0] != Ei:
            em = em.resize((Ei, Ei), Image.LANCZOS)
        ea = np.zeros((H, W, 4), np.float32)
        ex0, ey0 = int(ox + ex), int(oy + top)
        ea[ey0:ey0 + Ei, ex0:ex0 + Ei] = np.asarray(em, np.float32) / 255.0
        cv.over(ea[:, :, 3], ea[:, :, :3])
        esh = arr(Image.fromarray((ea[:, :, 3] * 255).astype(np.uint8)).filter(ImageFilter.GaussianBlur(0.09 * C)))
        sh = np.maximum(sh, np.roll(esh, int(0.04 * C), axis=0))
    # tight dark edge (legibility over bright skies) + soft drop shadow
    edge = arr(Image.fromarray((np.clip(m_main + m_sub + m_rule, 0, 1) * 255).astype(np.uint8)).filter(
        ImageFilter.GaussianBlur(0.022 * C)))
    cv.under(np.clip(edge * 1.6, 0, 1), (4, 6, 8), 0.75)
    cv.under(sh, (0, 0, 0), 0.6)
    return cv.image()


def fit(img, W, H, margin, align='center'):
    bb = img.getbbox()
    img = img.crop(bb)
    s = min((W - 2 * margin) / img.width, (H - 2 * margin) / img.height)
    im = img.resize((max(1, int(img.width * s)), max(1, int(img.height * s))), Image.LANCZOS)
    out = Image.new('RGBA', (W, H), (0, 0, 0, 0))
    x = (W - im.width) // 2 if align == 'center' else margin
    out.paste(im, (x, (H - im.height) // 2), im)
    return out


def backdrop(path, W, H):
    im = Image.open(path).convert('RGB')
    s = max(W / im.width, H / im.height)
    im = im.resize((int(im.width * s + 0.5), int(im.height * s + 0.5)), Image.LANCZOS)
    x, y = (im.width - W) // 2, (im.height - H) // 2
    return im.crop((x, y, x + W, y + H))


def menu_mock(fonts_dir, logo, lang):
    W, H = 1600, 900
    bg = Image.new('RGB', (W, H), (20, 24, 28))
    d = ImageDraw.Draw(bg, 'RGBA')
    g = np.linspace(0, 1, H)[:, None]
    base = (np.array([34, 44, 54])[None, None] * (1 - g[..., None]) + np.array([10, 12, 15])[None, None] * g[..., None])
    bg = Image.fromarray(np.repeat(base, W, axis=1).astype(np.uint8))
    d = ImageDraw.Draw(bg, 'RGBA')
    lg = logo.resize((int(logo.width * 0.62), int(logo.height * 0.62)), Image.LANCZOS)
    bg.paste(lg, (60, 170), lg)
    f = ImageFont.truetype(os.path.join(fonts_dir, 'FiraSansCondensed-SemiBold.ttf'), 30)
    items = {'ru': ['ПРОДОЛЖИТЬ', 'НОВАЯ ИГРА', 'ЗАГРУЗИТЬ ИГРУ', 'НАСТРОЙКИ', 'ВЫХОД'],
             'en': ['RESUME GAME', 'NEW GAME', 'LOAD GAME', 'OPTIONS', 'QUIT']}[lang]
    y = 360
    for i, t in enumerate(items):
        d.text((84, y), t, font=f, fill=(220, 230, 240, 255 if i != 1 else 255))
        if i == 1:
            d.rectangle([70, y + 6, 74, y + 30], fill=(110, 190, 255, 255))
        y += 46
    fm = ImageFont.truetype(os.path.join(fonts_dir, 'FiraMono-Regular.ttf'), 16)
    d.text((W - 360, H - 40), 'C17 // ГРАЖДАНСКИЙ РЕЕСТР', font=fm, fill=(120, 140, 160, 255))
    return bg


def main(fonts_dir, art_dir, backdrops):
    steam = os.path.join(art_dir, 'steam')
    prev = os.path.join(art_dir, 'previews')
    os.makedirs(steam, exist_ok=True)
    os.makedirs(prev, exist_ok=True)
    for lang in ('en', 'ru'):
        full = lockup(fonts_dir, lang)
        full.save(os.path.join(steam, 'logo_lockup_%s_full.png' % lang))
        lib = fit(full, 1280, 720, 64)
        lib.save(os.path.join(steam, 'library_logo_1280x720_%s.png' % lang))
        menu = fit(full, 1024, 256, 12, align='left')
        menu.save(os.path.join(art_dir, 'menu_logo_1024x256_%s.png' % lang))
        for i, b in enumerate(backdrops):
            bd = backdrop(b, 1280, 720)
            bd.paste(lib, (0, 0), lib)
            bd.save(os.path.join(prev, 'logo_library_%s_over_%d.png' % (lang, i + 1)))
        menu_mock(fonts_dir, menu, lang).save(os.path.join(prev, 'logo_menu_mock_%s.png' % lang))
        # checker preview (shows transparency) for the 1024x256 menu logo
        ck = Image.new('RGB', (1024, 256), (200, 200, 200))
        cd = ImageDraw.Draw(ck)
        for yy in range(0, 256, 16):
            for xx in range(0, 1024, 16):
                if (xx // 16 + yy // 16) % 2:
                    cd.rectangle([xx, yy, xx + 15, yy + 15], fill=(150, 150, 150))
        ck.paste(menu, (0, 0), menu)
        ck.save(os.path.join(prev, 'logo_menu_%s_transparency.png' % lang))
        print('done', lang, full.size)


if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2], sys.argv[3:])
