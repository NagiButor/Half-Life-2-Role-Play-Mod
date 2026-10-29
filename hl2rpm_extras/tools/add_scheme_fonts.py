"""HL2RPM: регистрирует шрифты интерфейса RPG (Fira, resource/fonts) в resource/clientscheme.res мода.

python add_scheme_fonts.py [path to clientscheme.res]

Повторный запуск заменяет блок между маркерами //HL2RPM_FONTS_BEGIN / //HL2RPM_FONTS_END.
Размеры - под высоту 900; на других разрешениях - пропорционально (диапазоны yres).
"""
import sys, re

SCHEME = sys.argv[1] if len(sys.argv) > 1 else r"E:\Steam\steamapps\sourcemods\hl2rpm\resource\clientscheme.res"

# (файл, имя семейства, как его видит GDI). Блок CustomFontFiles с "name" и диапазоном по языкам -
# единственный способ, чтобы vgui рисовал этим шрифтом и кириллицу: иначе он берёт из файла только
# 0x00-0xFF, а остальное - из Tahoma (vgui FontManager::SetFontGlyphSet, IsFontForeignLanguageCapable).
FILES = [
    ("resource/fonts/FiraSans-Regular.ttf", "Fira Sans"),
    ("resource/fonts/FiraSans-Bold.ttf", "Fira Sans"),
    ("resource/fonts/FiraSans-Italic.ttf", "Fira Sans"),
    ("resource/fonts/FiraSansCondensed-Regular.ttf", "Fira Sans Condensed"),
    ("resource/fonts/FiraSansCondensed-SemiBold.ttf", "Fira Sans Condensed SemiBold"),
    ("resource/fonts/FiraSansCondensed-Bold.ttf", "Fira Sans Condensed"),
    ("resource/fonts/FiraSansCondensed-Italic.ttf", "Fira Sans Condensed"),
    ("resource/fonts/FiraMono-Regular.ttf", "Fira Mono"),
    ("resource/fonts/FiraMono-Medium.ttf", "Fira Mono Medium"),
    ("resource/fonts/FiraMono-Bold.ttf", "Fira Mono"),
]
LANGUAGES = ["english", "russian", "ukrainian", "german", "french", "spanish", "polish", "czech", "italian",
             "brazilian", "portuguese", "hungarian", "bulgarian", "turkish", "latam"]
RANGE = "0x0000 0x2122"

# name, face, weight, base tall at 900 lines, extra
FONTS = [
    ("HL2RPM.Title",      "Fira Sans Condensed SemiBold", 600, 30, ""),
    ("HL2RPM.Header",     "Fira Sans Condensed SemiBold", 600, 24, ""),
    ("HL2RPM.Body",       "Fira Sans",                    400, 20, ""),
    ("HL2RPM.BodySmall",  "Fira Sans",                    400, 16, ""),
    ("HL2RPM.Mono",       "Fira Mono Medium",             500, 18, ""),
    ("HL2RPM.MonoSmall",  "Fira Mono",                    400, 14, ""),
    ("HL2RPM.HudSmall",   "Fira Sans Condensed",          400, 16, ""),
    ("HL2RPM.HudMedium",  "Fira Sans Condensed SemiBold", 600, 20, ""),
    ("HL2RPM.HudNumbers", "Fira Sans Condensed SemiBold", 600, 38, ""),
    ("HL2RPM.HudGlyph",   "Fira Mono Medium",             500, 24, ""),
    ("HL2RPM.Huge",       "Fira Sans Condensed SemiBold", 600, 54, ""),
]

RANGES = [(1, 599, 540), (600, 767, 700), (768, 1023, 900), (1024, 1199, 1080),
          (1200, 1439, 1300), (1440, 2159, 1600), (2160, 10000, 2160)]


def font_block(name, face, weight, base):
    out = ['\t\t"%s"' % name, "\t\t{"]
    for i, (lo, hi, mid) in enumerate(RANGES, 1):
        tall = max(8, int(round(base * mid / 900.0)))
        out += ['\t\t\t"%d"' % i, "\t\t\t{",
                '\t\t\t\t"name"\t\t"%s"' % face,
                '\t\t\t\t"tall"\t\t"%d"' % tall,
                '\t\t\t\t"weight"\t"%d"' % weight,
                '\t\t\t\t"antialias"\t"1"',
                '\t\t\t\t"yres"\t\t"%d %d"' % (lo, hi),
                "\t\t\t}"]
    out.append("\t\t}")
    return "\n".join(out)


def main():
    raw = open(SCHEME, "rb").read()
    text = raw.decode("utf-8", errors="surrogateescape")
    crlf = "\r\n" in text
    t = text.replace("\r\n", "\n")

    # шрифты: блок в начале секции Fonts
    t = re.sub(r"\t*//HL2RPM_FONTS_BEGIN.*?//HL2RPM_FONTS_END\n", "", t, flags=re.S)
    m = re.search(r"\n\tFonts\s*\n\t\{\n", t)
    if not m:
        raise SystemExit("Fonts section not found")
    block = "\t\t//HL2RPM_FONTS_BEGIN (hl2rpm_extras/tools/add_scheme_fonts.py)\n" + \
            "\n".join(font_block(*f[:4]) for f in FONTS) + "\n\t\t//HL2RPM_FONTS_END\n"
    t = t[:m.end()] + block + t[m.end():]

    # файлы шрифтов
    t = re.sub(r"\t*//HL2RPM_FONTFILES_BEGIN.*?//HL2RPM_FONTFILES_END\n", "", t, flags=re.S)
    m = re.search(r"\n\tCustomFontFiles\s*\n\t\{\n", t)
    if not m:
        raise SystemExit("CustomFontFiles section not found")
    start = m.end()
    end = t.find("\n\t}", start)
    existing = t[start:end]
    nums = [int(x) for x in re.findall(r'"(\d+)"', existing)] or [0]
    n = max(nums)
    lines = ["\t\t//HL2RPM_FONTFILES_BEGIN"]
    for path, family in FILES:
        n += 1
        lines += ['\t\t"%d"' % n, "\t\t{", '\t\t\t"font"\t"%s"' % path, '\t\t\t"name"\t"%s"' % family]
        for lang in LANGUAGES:
            lines.append('\t\t\t"%s"\t{ "range" "%s" }' % (lang, RANGE))
        lines.append("\t\t}")
    lines.append("\t\t//HL2RPM_FONTFILES_END")
    t = t[:end] + "\n" + "\n".join(lines) + t[end:]

    if crlf:
        t = t.replace("\n", "\r\n")
    open(SCHEME, "wb").write(t.encode("utf-8", errors="surrogateescape"))
    print("fonts registered in", SCHEME)


main()
