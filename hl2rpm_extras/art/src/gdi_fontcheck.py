# -*- coding: utf-8 -*-
"""Checks the mod fonts through Windows GDI the way Source's vgui creates Win32 fonts:
AddFontResourceEx(FR_PRIVATE) on the file, CreateFontA(face name, ANSI_CHARSET, ANTIALIASED_QUALITY),
then: does GDI resolve the face to that name (GetTextFace), are the Cyrillic/Greek glyphs present
(GetGlyphIndicesW with GGI_MARK_NONEXISTING_GLYPHS), and a rendered sample sheet (ExtTextOutW into a DIB).

usage: python -I gdi_fontcheck.py <fonts_dir> <out.png>
"""
import ctypes
import os
import sys
from ctypes import wintypes

import numpy as np
from PIL import Image, ImageDraw, ImageFont

gdi = ctypes.WinDLL('gdi32')
user = ctypes.WinDLL('user32')
gdi.AddFontResourceExW.argtypes = [wintypes.LPCWSTR, wintypes.DWORD, ctypes.c_void_p]
gdi.RemoveFontResourceExW.argtypes = [wintypes.LPCWSTR, wintypes.DWORD, ctypes.c_void_p]
gdi.CreateFontA.restype = wintypes.HFONT
gdi.CreateFontA.argtypes = [ctypes.c_int] * 5 + [wintypes.DWORD] * 8 + [ctypes.c_char_p]
gdi.CreateCompatibleDC.restype = wintypes.HDC
gdi.CreateCompatibleDC.argtypes = [wintypes.HDC]
gdi.SelectObject.restype = wintypes.HGDIOBJ
gdi.SelectObject.argtypes = [wintypes.HDC, wintypes.HGDIOBJ]
gdi.GetTextFaceW.argtypes = [wintypes.HDC, ctypes.c_int, wintypes.LPWSTR]
gdi.GetGlyphIndicesW.argtypes = [wintypes.HDC, wintypes.LPCWSTR, ctypes.c_int, ctypes.POINTER(wintypes.WORD), wintypes.DWORD]
gdi.ExtTextOutW.argtypes = [wintypes.HDC, ctypes.c_int, ctypes.c_int, wintypes.UINT, ctypes.c_void_p, wintypes.LPCWSTR, wintypes.UINT, ctypes.c_void_p]
gdi.SetTextColor.argtypes = [wintypes.HDC, wintypes.COLORREF]
gdi.SetBkColor.argtypes = [wintypes.HDC, wintypes.COLORREF]
gdi.CreateDIBSection.restype = wintypes.HBITMAP
gdi.CreateDIBSection.argtypes = [wintypes.HDC, ctypes.c_void_p, wintypes.UINT, ctypes.POINTER(ctypes.c_void_p), wintypes.HANDLE, wintypes.DWORD]
gdi.DeleteObject.argtypes = [wintypes.HGDIOBJ]
gdi.DeleteDC.argtypes = [wintypes.HDC]

FR_PRIVATE = 0x10
ANSI_CHARSET = 0
ANTIALIASED_QUALITY = 4
GGI_MARK_NONEXISTING_GLYPHS = 1


class BITMAPINFOHEADER(ctypes.Structure):
    _fields_ = [('biSize', wintypes.DWORD), ('biWidth', wintypes.LONG), ('biHeight', wintypes.LONG),
                ('biPlanes', wintypes.WORD), ('biBitCount', wintypes.WORD), ('biCompression', wintypes.DWORD),
                ('biSizeImage', wintypes.DWORD), ('biXPelsPerMeter', wintypes.LONG), ('biYPelsPerMeter', wintypes.LONG),
                ('biClrUsed', wintypes.DWORD), ('biClrImportant', wintypes.DWORD)]


# (file, GDI face name to request, weight, italic, role)
FACES = [
    ('FiraSansCondensed-Regular.ttf', 'Fira Sans Condensed', 400, 0, 'headers / labels'),
    ('FiraSansCondensed-Italic.ttf', 'Fira Sans Condensed', 400, 1, 'headers / labels'),
    ('FiraSansCondensed-SemiBold.ttf', 'Fira Sans Condensed SemiBold', 600, 0, 'titles / menu'),
    ('FiraSansCondensed-Bold.ttf', 'Fira Sans Condensed', 700, 0, 'headers / labels'),
    ('FiraSans-Regular.ttf', 'Fira Sans', 400, 0, 'body text'),
    ('FiraSans-Italic.ttf', 'Fira Sans', 400, 1, 'body text'),
    ('FiraSans-Bold.ttf', 'Fira Sans', 700, 0, 'body text'),
    ('FiraMono-Regular.ttf', 'Fira Mono', 400, 0, 'data readouts'),
    ('FiraMono-Medium.ttf', 'Fira Mono Medium', 500, 0, 'data readouts'),
    ('FiraMono-Bold.ttf', 'Fira Mono', 700, 0, 'data readouts'),
]
TEST = ('АБВГДЕЁЖЗИЙКЛМНОПРСТУФХЦЧШЩЪЫЬЭЮЯабвгдеёжзийклмнопрстуфхцчшщъыьэюя'
        'ІЇЄЎҐіїєўґ№«»—–„“…·Λλ0123456789%')
SAMPLE = 'Λ.А.М.Б.Д.А. Гражданин, предъявите удостоверение № 47-212. Ёлка, щи, съезд. Citizen ID OK'


def render(hdc, text, w, h):
    bmi = BITMAPINFOHEADER(ctypes.sizeof(BITMAPINFOHEADER), w, -h, 1, 32, 0, 0, 0, 0, 0, 0)
    bits = ctypes.c_void_p()
    hbm = gdi.CreateDIBSection(hdc, ctypes.byref(bmi), 0, ctypes.byref(bits), None, 0)
    old = gdi.SelectObject(hdc, hbm)
    gdi.SetBkColor(hdc, 0)
    gdi.SetTextColor(hdc, 0xFFFFFF)
    rect = (wintypes.LONG * 4)(0, 0, w, h)
    gdi.ExtTextOutW(hdc, 0, 0, 2, ctypes.byref(rect), None, 0, None)  # ETO_OPAQUE clear
    gdi.ExtTextOutW(hdc, 2, 0, 0, None, text, len(text), None)
    buf = (ctypes.c_uint8 * (w * h * 4)).from_address(bits.value)
    a = np.frombuffer(buf, dtype=np.uint8).reshape(h, w, 4)[:, :, 1].copy()
    gdi.SelectObject(hdc, old)
    gdi.DeleteObject(hbm)
    return a


def main(fdir, out):
    rows = []
    ok = True
    hdc = gdi.CreateCompatibleDC(None)
    for fn, face, wt, it, role in FACES:
        path = os.path.abspath(os.path.join(fdir, fn))
        n = gdi.AddFontResourceExW(path, FR_PRIVATE, None)
        for tall in (14, 18, 24):
            hf = gdi.CreateFontA(tall, 0, 0, 0, wt, it, 0, 0, ANSI_CHARSET, 0, 0, ANTIALIASED_QUALITY, 0,
                                 face.encode('ascii'))
            old = gdi.SelectObject(hdc, hf)
            nm = ctypes.create_unicode_buffer(64)
            gdi.GetTextFaceW(hdc, 64, nm)
            idx = (wintypes.WORD * len(TEST))()
            gdi.GetGlyphIndicesW(hdc, TEST, len(TEST), idx, GGI_MARK_NONEXISTING_GLYPHS)
            missing = [TEST[i] for i in range(len(TEST)) if idx[i] == 0xFFFF]
            img = render(hdc, '%s %d: %s' % (face, tall, SAMPLE), 1300, int(tall * 1.4))
            gdi.SelectObject(hdc, old)
            gdi.DeleteObject(hf)
            good = n > 0 and nm.value == face and not missing
            ok &= good
            if tall == 14:
                print('%-32s %-30s w%d i%d  added=%d  GDI face=%-30r missing=%s  %s' % (
                    fn, face, wt, it, n, nm.value, ''.join(missing) or '-', 'OK' if good else 'FAIL'))
            rows.append((img, good))
        gdi.RemoveFontResourceExW(path, FR_PRIVATE, None)
    gdi.DeleteDC(hdc)

    pad = 4
    H = sum(r.shape[0] + pad for r, _ in rows) + 40
    sheet = Image.new('RGB', (1320, H), (12, 16, 20))
    d = ImageDraw.Draw(sheet)
    lab = ImageFont.truetype(os.path.join(os.environ.get('WINDIR', 'C:/Windows'), 'Fonts', 'consola.ttf'), 14)
    d.text((10, 8), 'GDI render (AddFontResourceEx FR_PRIVATE + CreateFontA ANSI_CHARSET, antialiased) - as Source vgui creates fonts',
           font=lab, fill=(120, 140, 160))
    y = 32
    blue = np.array([110, 190, 255], np.float32)
    bg = np.array([12, 16, 20], np.float32)
    for a, good in rows:
        t = (a.astype(np.float32) / 255.0)[:, :, None]
        rgb = (bg * (1 - t) + (blue if good else np.array([230, 70, 60], np.float32)) * t).astype(np.uint8)
        sheet.paste(Image.fromarray(rgb), (10, y))
        y += a.shape[0] + pad
    sheet.save(out)
    print('saved', out, 'ALL OK' if ok else 'SOME FAILED')
    return ok


if __name__ == '__main__':
    sys.exit(0 if main(sys.argv[1], sys.argv[2]) else 1)
