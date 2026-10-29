# -*- coding: utf-8 -*-
"""Font preview sheets for HL2RPM UI fonts + programmatic Cyrillic glyph check.

usage:
  python -I fonts_preview.py compare <out.png> <font.ttf> [<font.ttf> ...]
      one block per font: alphabet, RU/EN samples, data line, sizes 12..48 (Combine panel colours)
  python -I fonts_preview.py check <font.ttf> [...]
      renders every Russian letter (+ a few signs) and compares it with the font's .notdef glyph;
      prints 'OK' or the list of characters that come out as .notdef / empty
"""
import os
import sys

from PIL import Image, ImageDraw, ImageFont

PANEL = (12, 16, 20)
BLUE = (110, 190, 255)
WHITE = (220, 230, 240)
AMBER = (255, 170, 60)
DIM = (120, 140, 160)

RU_UP = 'АБВГДЕЁЖЗИЙКЛМНОПРСТУФХЦЧШЩЪЫЬЭЮЯ'
RU_LO = 'абвгдеёжзийклмнопрстуфхцчшщъыьэюя'
EXTRA = 'ІЇЄЎҐіїєўґ №«»—–„“…·Λλ'
SAMPLE_RU = 'Гражданин, предъявите удостоверение. Съешь же ещё этих мягких французских булок, да выпей чаю.'
SAMPLE_EN = 'Citizen, present your identification. The quick brown fox jumps over the lazy dog.'
HEAD_RU = 'КАРТОЧКА ГРАЖДАНИНА · Λ.А.М.Б.Д.А. — 34 очка'
HEAD_EN = 'CITIZEN RECORD · CIVIL PROTECTION · SECTOR C17-04'
DATA = 'ID 47-212-0391  ЛОЯЛЬНОСТЬ 68%  КРЕДИТЫ 1 250 кр.  № 0417  0O 1lI'


def font_label(path):
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import fontinfo
    r = fontinfo.inspect(path)
    return '%s  /  %s  (weight %d)  -  %s' % (r['family_id1'], r['subfamily_id2'], r['os2']['weight'],
                                              os.path.basename(path))


def block(path, width):
    lines = []
    lab = ImageFont.truetype(os.path.join(os.environ.get('WINDIR', 'C:/Windows'), 'Fonts', 'consola.ttf'), 14)
    lines.append((lab, font_label(path), DIM))
    for sz, txt, col in ((34, HEAD_RU, BLUE), (34, HEAD_EN, BLUE), (24, RU_UP, WHITE), (24, RU_LO, WHITE),
                         (24, EXTRA, AMBER), (18, SAMPLE_RU, WHITE), (18, SAMPLE_EN, WHITE), (18, DATA, BLUE),
                         (14, SAMPLE_RU, WHITE), (12, SAMPLE_RU, WHITE), (12, DATA, BLUE)):
        lines.append((ImageFont.truetype(path, sz), txt, col))
    h = 16 + sum(int(f.size * 1.45) for f, _, _ in lines) + 12
    im = Image.new('RGB', (width, h), PANEL)
    d = ImageDraw.Draw(im)
    d.rectangle([0, 0, width - 1, h - 1], outline=(40, 60, 80))
    d.line([(0, 0), (18, 0)], fill=BLUE, width=2)
    d.line([(0, 0), (0, 18)], fill=BLUE, width=2)
    y = 10
    for f, t, c in lines:
        d.text((16, y), t, font=f, fill=c)
        y += int(f.size * 1.45)
    return im


def compare(out, fonts):
    width = 1400
    blocks = [block(p, width) for p in fonts]
    H = sum(b.height for b in blocks) + 10 * (len(blocks) + 1)
    sheet = Image.new('RGB', (width + 20, H), (4, 6, 8))
    y = 10
    for b in blocks:
        sheet.paste(b, (10, y))
        y += b.height + 10
    sheet.save(out)
    print('saved', out, sheet.size)


def glyph_bitmap(font, ch):
    m = font.getmask(ch)
    return m.size, bytes(m)


def check(fonts):
    chars = RU_UP + RU_LO + '№«»—Λλ'
    ok_all = True
    for p in fonts:
        f = ImageFont.truetype(p, 48)
        notdef = glyph_bitmap(f, '\ue123')  # private use: surely absent -> .notdef
        bad = []
        for ch in chars:
            g = glyph_bitmap(f, ch)
            if g == notdef or g[0][0] == 0 or not any(g[1]):
                bad.append(ch)
        st = 'OK (%d chars)' % len(chars) if not bad else 'MISSING: ' + ' '.join(bad)
        ok_all &= not bad
        print('%-40s %s' % (os.path.basename(p), st))
    return ok_all


def mock(out, fdir):
    """Bracelet-style panel using the three roles together (header / body / data)."""
    F = lambda n, s: ImageFont.truetype(os.path.join(fdir, n), s)
    W, H = 1000, 560
    im = Image.new('RGB', (W, H), (30, 34, 38))
    d = ImageDraw.Draw(im, 'RGBA')
    for y in range(0, H, 3):  # faint scanlines behind the panel
        d.line([(0, y), (W, y)], fill=(0, 0, 0, 40))
    x0, y0, x1, y1 = 40, 40, W - 40, H - 40
    d.rectangle([x0, y0, x1, y1], fill=(12, 16, 20, 235), outline=(110, 190, 255, 70))
    L = 22
    for (cx, cy, sx, sy) in ((x0, y0, 1, 1), (x1, y0, -1, 1), (x0, y1, 1, -1), (x1, y1, -1, -1)):
        d.line([(cx, cy), (cx + sx * L, cy)], fill=BLUE, width=2)
        d.line([(cx, cy), (cx, cy + sy * L)], fill=BLUE, width=2)
    d.text((70, 62), 'КАРТОЧКА ГРАЖДАНИНА', font=F('FiraSansCondensed-SemiBold.ttf', 40), fill=BLUE)
    d.text((70, 110), 'CITIZEN RECORD  ·  ГОРОД 17  ·  СЕКТОР C17-04', font=F('FiraSansCondensed-Regular.ttf', 20), fill=DIM)
    d.line([(70, 145), (W - 70, 145)], fill=(110, 190, 255, 90), width=1)
    mono = F('FiraMono-Regular.ttf', 20)
    monob = F('FiraMono-Bold.ttf', 20)
    rows = [('ИМЯ', 'Гражданин Ломакин Е. Ю.'), ('ID', '47-212-0391'), ('ЛОЯЛЬНОСТЬ', '68 %'),
            ('КРЕДИТЫ', '1 250 кр.'), ('ПОДОЗРЕНИЕ', '12 / 100')]
    y = 165
    for k, v in rows:
        d.text((70, y), k, font=mono, fill=DIM)
        d.text((260, y), v, font=monob if k == 'ID' else mono, fill=WHITE)
        y += 30
    d.text((70, y + 6), 'Λ.А.М.Б.Д.А.', font=F('FiraSansCondensed-Bold.ttf', 26), fill=AMBER)
    d.text((240, y + 10), 'Л 7  А 5  М 6  Б 4  Д 6  А 6   =  34', font=mono, fill=AMBER)
    body = F('FiraSans-Regular.ttf', 19)
    d.text((70, y + 52), '«Гражданин, ваше поведение отмечено. Явитесь на пункт распределения', font=body, fill=WHITE)
    d.text((70, y + 78), 'для получения пайка.» — Citizen, report to your ration distribution point.', font=body, fill=WHITE)
    d.text((70, y + 112), 'ВНИМАНИЕ: НАРУШЕНИЕ КОМЕНДАНТСКОГО ЧАСА', font=F('FiraSansCondensed-Bold.ttf', 22), fill=(230, 70, 60))
    d.text((W - 300, H - 78), 'Fira Sans Condensed / Fira Sans / Fira Mono', font=F('FiraSansCondensed-Italic.ttf', 15), fill=DIM)
    im.save(out)
    print('saved', out)


if __name__ == '__main__':
    if sys.argv[1] == 'compare':
        compare(sys.argv[2], sys.argv[3:])
    elif sys.argv[1] == 'mock':
        mock(sys.argv[2], sys.argv[3])
    elif sys.argv[1] == 'check':
        sys.exit(0 if check(sys.argv[2:]) else 1)
