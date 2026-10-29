# -*- coding: utf-8 -*-
"""Contact sheet of the finished art: icon at every exported size on dark/light, logos, font mock.
usage: python -I overview.py <art_dir> <out.png>"""
import os
import sys
from PIL import Image, ImageDraw, ImageFont

art, out = sys.argv[1], sys.argv[2]
lab = ImageFont.truetype(os.path.join(os.environ.get('WINDIR', 'C:/Windows'), 'Fonts', 'consola.ttf'), 16)
W = 1700
sheet = Image.new('RGB', (W, 1640), (40, 44, 48))
d = ImageDraw.Draw(sheet)
d.text((20, 14), 'HL2RPM art overview - icon (all exported sizes), logos, UI fonts', font=lab, fill=(230, 230, 230))
sizes = [16, 24, 32, 48, 64, 128, 184, 256]
y = 50
for bg, name in (((27, 40, 56), 'Steam dark'), ((236, 238, 240), 'light')):
    band = Image.new('RGB', (W - 40, 300), bg)
    bd = ImageDraw.Draw(band)
    x = 16
    for s in sizes:
        im = Image.open(os.path.join(art, 'icon', 'icon_%d.png' % s))
        band.paste(im, (x, 280 - s), im)
        bd.text((x, 4), str(s), font=lab, fill=(128, 128, 128))
        x += s + 24
    bd.text((W - 160, 4), name, font=lab, fill=(128, 128, 128))
    sheet.paste(band, (20, y))
    y += 310
for p in ('menu_logo_1024x256_en.png', 'menu_logo_1024x256_ru.png'):
    im = Image.open(os.path.join(art, p))
    band = Image.new('RGB', (1024, 256), (20, 26, 32))
    band.paste(im, (0, 0), im)
    sheet.paste(band, (20, y))
    d.text((1060, y + 10), p, font=lab, fill=(200, 200, 200))
    y += 266
mock = Image.open(os.path.join(art, 'previews', 'fonts_ui_mock.png'))
mock = mock.resize((mock.width * 2 // 3, mock.height * 2 // 3), Image.LANCZOS)
sheet.paste(mock, (20, y))
lib = Image.open(os.path.join(art, 'previews', 'logo_library_en_over_2.png')).resize((640, 360), Image.LANCZOS)
sheet.paste(lib, (W - 680, y))
sheet.save(out)
print('saved', out, sheet.size)
