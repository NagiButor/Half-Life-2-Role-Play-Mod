HL2RPM UI fonts (all SIL Open Font License 1.1 - see OFL-FiraSans.txt, OFL-FiraMono.txt)

Source: https://github.com/google/fonts  (commit 9862abc066c9b83bf84c3e8ed1fc2d8d48f9bbf0)
        ofl/firasanscondensed, ofl/firasans, ofl/firamono - unmodified static TTF files.
Design: Carrois Apostrophe for Mozilla / Telefonica.
Coverage: Latin-1, full Russian + Ukrainian/Belarusian Cyrillic, Greek (Λ λ), № « » — „ “ …

Windows/GDI face names (the "name" to use in clientscheme.res; checked with
AddFontResourceEx(FR_PRIVATE) + CreateFontA(ANSI_CHARSET) + GetTextFace, the way vgui creates fonts):

  file                              "name"                          weight  italic  role
  FiraSansCondensed-Regular.ttf     Fira Sans Condensed             400     0       labels, HUD text
  FiraSansCondensed-Italic.ttf      Fira Sans Condensed             400     1
  FiraSansCondensed-SemiBold.ttf    Fira Sans Condensed SemiBold    600     0       titles, menu buttons
  FiraSansCondensed-Bold.ttf        Fira Sans Condensed             700     0       headers
  FiraSans-Regular.ttf              Fira Sans                       400     0       body text (dialog, notes)
  FiraSans-Italic.ttf               Fira Sans                       400     1
  FiraSans-Bold.ttf                 Fira Sans                       700     0
  FiraMono-Regular.ttf              Fira Mono                       400     0       data readouts
  FiraMono-Medium.ttf               Fira Mono Medium                500     0
  FiraMono-Bold.ttf                 Fira Mono                       700     0

SemiBold / Medium are separate GDI families: request them by their own name with their own weight
(asking "Fira Sans Condensed" with weight 600 gives Regular or synthetic bold instead).
Do not limit "range" to 0x0000-0x007F/0x017F in scheme fonts that must show Russian (Cyrillic is 0x0400-0x04FF,
Greek Λ is 0x039B).
