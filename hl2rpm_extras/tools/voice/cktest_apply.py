"""Put the generated ck_test voices into the game and the CK project:
 1. every generated line -> 44.1 kHz PCM16 mono, common loudness;
 2. per target: CP / Combine - spectrum matched to the original HL2 clips of that voice + the stock radio on/off
    clicks of that unit; hazmat workers - through a respirator; others as is;
 3. written to the canonical place <game>/sound/dialogs/<owner>/<lang>/<key>.wav;
 4. attached to the project (dlgedit.voiceattach), saved, exported (lip sync scenes are made by the export).
usage: python cktest_apply.py [--no-export]"""
import json
import os
import random
import sys

import numpy as np
import soundfile as sf

sys.stdout.reconfigure(encoding="utf-8", errors="replace")
HERE = os.path.dirname(os.path.abspath(__file__))
CK = r"E:\SourceModding\HL2RPMSOURCE\source-sdk-2013-mapbase-v8.0\sp\src\hl2rpm_extras\tools\dialog_editor"
sys.path.insert(0, CK)
sys.path.insert(0, os.path.join(HERE, "colab"))
sys.path.insert(0, HERE)
import post as P  # noqa: E402
import refs_survey as R  # noqa: E402
import cktest_build as B  # noqa: E402  (voice sources, project path)
from dlgedit import model, exporter, voiceattach  # noqa: E402

def _arg(name, default):
    return sys.argv[sys.argv.index(name) + 1] if name in sys.argv else default


JOB = _arg("--job", os.path.join(HERE, "job_cktest"))        # job.json + targets.json
RAW = _arg("--raw", os.path.join(HERE, "cktest", "raw"))     # <item id>.wav
SR = 44100
TARGET_RMS_DB = -19.0
SKIP = ("on1", "on2", "on3", "off1", "off2", "off3", "off4", "pain", "die", "fire", "lostradio", "radio")


def loud(a):
    """speech RMS (frames within 30 dB of the loudest) to TARGET_RMS_DB, peak under -0.5 dBFS"""
    f = 2048
    if len(a) < f:
        return a
    fr = np.lib.stride_tricks.sliding_window_view(a, f)[::f // 2]
    e = (fr ** 2).mean(1)
    rms = np.sqrt(e[e > e.max() * 1e-3].mean())
    g = 10 ** (TARGET_RMS_DB / 20) / max(rms, 1e-6)
    g = min(g, 0.944 / max(float(np.abs(a).max()), 1e-6))
    return (a * g).astype(np.float32)


def respirator(a):
    """speech through a gas mask: band 300..3000 Hz, a short resonance of the mask, a little muffled"""
    n = 1 << (len(a) + 4096).bit_length()
    f = np.fft.rfftfreq(n, 1 / SR)
    h = 1 / np.sqrt(1 + (300 / np.maximum(f, 1)) ** 4) / np.sqrt(1 + (f / 3000) ** 6)
    h *= 1 + 0.6 * np.exp(-((f - 1100) / 350) ** 2)
    y = np.fft.irfft(np.fft.rfft(a, n) * h, n)[:len(a)]
    d = int(0.0011 * SR)
    out = y.copy()
    out[d:] += 0.35 * y[:-d]
    return out.astype(np.float32)


def originals(voice, lang):
    return [P.decode(d) for n, d in R.clips(B.R.SOURCES[voice][lang]) if not n.lower().startswith(SKIP)]


def clicks(voice):
    src = B.R.SOURCES[voice]["english"]
    on, off = [], []
    for n, d in R.clips(src):
        b = n.lower()[:-4]
        if b in ("on1", "on2"):
            on.append(P.decode(d))
        elif b in ("off1", "off2", "off3", "off4"):
            off.append(P.decode(d))
    return on, off


def main():
    job = json.load(open(os.path.join(JOB, "job.json"), encoding="utf-8"))
    targets = json.load(open(os.path.join(JOB, "targets.json"), encoding="utf-8"))
    p = model.Project.load(B.PROJECT)
    gd = p.settings.game_dir

    # generated lines at 44.1 kHz
    lines, missing = {}, []
    for it in job["items"]:
        path = os.path.join(RAW, it["id"] + ".wav")
        if not os.path.exists(path):
            missing.append(it["id"])
            continue
        a, sr = sf.read(path, dtype="float32")
        lines[it["id"]] = P.resample(a, sr)
    print("generated:", len(lines), "missing:", missing)

    # spectrum match filters per (unit voice, language)
    firs = {}
    unit = {"radio_cp": "metrocop", "radio_combine": "combine"}
    for post, voice in unit.items():
        for lang in ("english", "russian"):
            ids = sorted({t["item"] for t in targets if t["post"] == post and t["lang"] == lang and t["item"] in lines})
            if not ids:
                continue
            ref_p = P.ltas(originals(voice, lang))
            src_p = P.ltas([lines[i] for i in ids])
            firs[(post, lang)] = P.match_fir(ref_p, src_p)[0]
            print("matched", post, lang, len(ids), "lines")
    click = {post: clicks(voice) for post, voice in unit.items()}

    written = 0
    for t in targets:
        a = lines.get(t["item"])
        if a is None:
            continue
        post = t["post"]
        if post in unit:
            y = loud(P.fir(a, firs[(post, t["lang"])]))
            on, off = click[post]
            rnd = random.Random("%s/%s/%s" % (t["owner"], t["lang"], t["key"]))
            gap = np.zeros(int(0.04 * SR), np.float32)
            parts = [loud(rnd.choice(on)) * 0.7, gap, y, gap, loud(rnd.choice(off)) * 0.7] if on and off else [y]
            y = np.concatenate(parts)
        elif post == "mask":
            y = loud(respirator(a))
        else:
            y = loud(a)
        dst = os.path.join(gd, model.canonical_wav(t["owner"], t["lang"], t["key"]))
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        sf.write(dst, np.clip(y, -1, 1), SR, subtype="PCM_16")
        written += 1
    print("written to the game:", written, "files")

    names = sorted({t["owner"] for t in targets if not t["owner"].startswith("barks_")})
    sets = sorted({t["owner"][len("barks_"):] for t in targets if t["owner"].startswith("barks_")})
    att = voiceattach.attach_canonical_voices(p, names, sets)
    print("attached to the project:", len(att))
    p.save(B.PROJECT)
    if "--no-export" not in sys.argv:
        r = exporter.Exporter(p).run()
        print(r.summary())
        p.save(B.PROJECT)


if __name__ == "__main__":
    main()
