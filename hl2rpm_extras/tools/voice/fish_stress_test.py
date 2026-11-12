"""Does Fish Audio follow Russian stress written in the text? Minimal pairs (за́мок / замо́к ...) in three spellings -
plain, combining acute (U+0301) after the stressed vowel, capital stressed vowel - and a measure of where the stress
fell: the stressed vowel of a two-syllable word is longer and louder, so the share of the energy in the first vowel
peak against the second tells first-syllable stress (>1) from second-syllable stress (<1).
Writes fish_test/stress/*.wav and stress_report.json; the files are for listening too."""
import json
import os
import sys

import numpy as np
import soundfile as sf

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import fish_job as F  # noqa: E402

sys.stdout.reconfigure(encoding="utf-8", errors="replace")
HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "fish_test", "stress")
os.makedirs(OUT, exist_ok=True)
A = "́"
PAIRS = [  # word, (first-syllable spelling variants), (second-syllable variants)
    ("замок", ("за" + A + "мок", "зАмок"), ("замо" + A + "к", "замОк")),
    ("стоит", ("сто" + A + "ит", "стОит"), ("стои" + A + "т", "стоИт")),
    ("мука", ("му" + A + "ка", "мУка"), ("мука" + A, "мукА")),
]


def syllable_ratio(path):
    a, sr = sf.read(path, dtype="float32")
    hop = sr // 100
    e = np.sqrt(np.convolve(a ** 2, np.ones(hop * 3) / (hop * 3), "same"))[::hop]
    e = np.convolve(e, np.ones(5) / 5, "same")
    on = np.where(e > e.max() * 0.08)[0]
    if len(on) < 4:
        return None
    seg = e[on[0]:on[-1] + 1]
    # the two vowel nuclei: the biggest peak and the biggest one at least 80 ms away from it
    i1 = int(np.argmax(seg))
    mask = np.abs(np.arange(len(seg)) - i1) > 8
    if not mask.any():
        return None
    i2 = int(np.argmax(np.where(mask, seg, 0)))
    first, second = sorted([i1, i2])

    def area(c):
        lo, hi = max(0, c - 6), min(len(seg), c + 7)
        return float((seg[lo:hi] ** 2).sum())
    return round(area(first) / max(area(second), 1e-9), 2)


def main():
    k = F.key()
    job = json.load(open(os.path.join(HERE, "job", "job.json"), encoding="utf-8"))
    v = job["voices"]["male01_ru"]
    ref = open(os.path.join(HERE, "job", v["ref"]), "rb").read()
    report = []
    for word, firsts, seconds in PAIRS:
        for kind, text in [("plain", word)] + [("acute1", firsts[0]), ("caps1", firsts[1]),
                                              ("acute2", seconds[0]), ("caps2", seconds[1])]:
            body = {"text": text.capitalize() if kind == "plain" else text[0].upper() + text[1:] if not text[0].isupper() else text,
                    "references": [{"audio": ref, "text": v["ref_text"]}], "format": "wav", "sample_rate": 44100,
                    "latency": "normal", "normalize": False, "temperature": 0.3, "top_p": 0.7}
            st, res = F.tts(k, "s2.1-pro-free", body)
            path = os.path.join(OUT, "%s_%s.wav" % (word, kind))
            if st == 200:
                open(path, "wb").write(res)
                r = syllable_ratio(path)
            else:
                r = "error %s" % st
            report.append({"word": word, "kind": kind, "text": body["text"], "first_vs_second": r})
            print("%-6s %-7s %-10s first/second energy %s" % (word, kind, body["text"], r), flush=True)
    json.dump(report, open(os.path.join(OUT, "stress_report.json"), "w", encoding="utf-8"), ensure_ascii=False, indent=1)


if __name__ == "__main__":
    main()
