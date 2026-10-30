"""Lip sync: runs the mod's VCD generator (lipsync_gen/vcd_<language>.py) for one
voiced line. The generator writes the .vcd (speech, look/face at the player,
facial animation, gesture) and embeds phonemes into the WAV (VDAT chunk)."""
import contextlib
import importlib
import io
import os
import re
import zlib
from pathlib import Path

_GENERATORS = {"english": "vcd_english", "russian": "vcd_russian", "ukrainian": "vcd_russian"}
_loaded = {}


def generator_for(lang):
    return _GENERATORS.get(lang, "vcd_english")


def _module(lang):
    name = generator_for(lang)
    if name not in _loaded:
        mod = importlib.import_module("dlgedit.lipsync_gen." + name)
        # The generator seeds its random curves with hash(node id); str hash is
        # randomized per process -> make the result repeatable.
        mod.hash = lambda s: zlib.crc32(str(s).encode("utf-8"))
        _loaded[name] = mod
    return _loaded[name]


def generate(lang, text, wav_abs, vcd_abs, actor, sound_rel, key="0"):
    """Returns (ok, log). sound_rel: path below sound/, e.g. dialogs/npc/english/0.wav"""
    mod = _module(lang)
    out = io.StringIO()
    try:
        with contextlib.redirect_stdout(out):
            wav = Path(wav_abs)
            dur = mod.get_wav_duration(wav)
            node = mod.DialogNode(str(key), text, "", wav, dur, sound_rel.replace("\\", "/"))
            cfg = mod.Config(sound_dir=wav.parent, scene_dir=Path(vcd_abs).parent,
                             dialog_path=Path(""), actor=actor)
            mod.write_vcd(cfg, node, Path(vcd_abs))
    except Exception as e:  # the generator is a script, keep the editor alive
        return False, out.getvalue() + "\nОшибка генератора: %s" % e
    ok = os.path.exists(vcd_abs)
    return ok, out.getvalue()


_SPEAK_RE = re.compile(r'event speak "([^"]*)"')
_ACTOR_RE = re.compile(r'^actor "([^"]*)"', re.M)


def vcd_sound(vcd_text):
    m = _SPEAK_RE.search(vcd_text)
    return m.group(1) if m else ""


def rewrite_vcd(vcd_text, new_sound_rel, actor):
    """Point a scene at a moved WAV (speak event name/param) and rename its actor."""
    old = vcd_sound(vcd_text)
    new = new_sound_rel.replace("/", "\\")
    if old and old != new:
        vcd_text = vcd_text.replace('"%s"' % old, '"%s"' % new)
    vcd_text = _ACTOR_RE.sub('actor "%s"' % actor, vcd_text, count=1)
    return vcd_text


def read_text(path):
    raw = open(path, "rb").read()
    try:
        return raw.decode("utf-8")
    except UnicodeDecodeError:
        return raw.decode("cp1251")
