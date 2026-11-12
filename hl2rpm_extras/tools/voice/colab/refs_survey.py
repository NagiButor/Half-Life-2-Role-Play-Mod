"""List candidate HL2 voice clips (longest first) for the clone references, per voice and language.
Writes refs_survey.txt (UTF-8) next to this script."""
import io
import os
import sys

import soundfile as sf

sys.path.insert(0, r"E:\SourceModding\HL2RPMSOURCE\source-sdk-2013-mapbase-v8.0\sp\src\hl2rpm_extras\tools\dialog_editor")
from dlgedit.world.vpk import open_vpk  # noqa: E402

HL2 = r"E:\Steam\steamapps\common\Half-Life 2\hl2"
UNPACKED = r"E:\SourceModding\UsefulStuff\Source sdk 2013 HL2 unpacked vpk\hl2"
SKIP = ("on1", "on2", "off1", "off2", "off3", "off4", "pain", "die", "fire", "lostradio", "radio")

SOURCES = {
    # voice: {lang: [(kind, container, prefix)]}
    "metrocop": {"english": ("dir", os.path.join(UNPACKED, "sound", "npc", "metropolice", "vo")),
                 "russian": ("vpk", "hl2_sound_vo_russian_dir.vpk", "sound/npc/metropolice/vo/")},
    "overwatch": {"english": ("dir", os.path.join(UNPACKED, "sound", "npc", "overwatch", "cityvoice")),
                  "russian": ("vpk", "hl2_sound_vo_russian_dir.vpk", "sound/npc/overwatch/cityvoice/")},
    "male01": {"english": ("vpk", "hl2_sound_vo_english_dir.vpk", "sound/vo/npc/male01/"),
               "russian": ("vpk", "hl2_sound_vo_russian_dir.vpk", "sound/vo/npc/male01/")},
}

_vpks = {}


def vpk(name):
    if name not in _vpks:
        _vpks[name] = open_vpk(os.path.join(HL2, name))
    return _vpks[name]


def clips(src):
    """-> list of (name, bytes)"""
    if src[0] == "dir":
        for fn in sorted(os.listdir(src[1])):
            if fn.lower().endswith(".wav"):
                with open(os.path.join(src[1], fn), "rb") as f:
                    yield fn, f.read()
    else:
        v = vpk(src[1])
        for n in sorted(v.names()):
            if n.startswith(src[2]) and n.endswith(".wav") and "/" not in n[len(src[2]):]:
                yield n[len(src[2]):], v.read(n)


def info(data):
    try:
        with sf.SoundFile(io.BytesIO(data)) as f:
            return f.frames / f.samplerate, f.samplerate, f.channels
    except Exception as e:  # ADPCM etc.
        return None, str(e)[:40], 0


if __name__ == "__main__":
    out = []
    for voice, langs in SOURCES.items():
        for lang, src in langs.items():
            rows = []
            for name, data in clips(src):
                if any(name.lower().startswith(s) for s in SKIP):
                    continue
                d, sr, ch = info(data)
                rows.append((d or 0, name, sr, ch))
            rows.sort(reverse=True)
            out.append("== %s / %s: %d clips" % (voice, lang, len(rows)))
            for d, name, sr, ch in rows[:25]:
                out.append("   %5.2f s  %s  %s Hz  ch%s" % (d, name, sr, ch))
    p = os.path.join(os.path.dirname(os.path.abspath(__file__)), "refs_survey.txt")
    with open(p, "w", encoding="utf-8") as f:
        f.write("\n".join(out))
    print("written", p)
