"""Post-process the Colab output for listening and for Source:
 - every line -> PCM16 mono 44100 (what CK accepts), peak-normalized;
 - CP / Overwatch clones get a second variant whose spectrum is matched to the original HL2 clips
   (long-term average spectrum EQ, linear-phase FIR), CP lines also a radio preview:
   NPC_MetroPolice.Radio.On click + voice + Radio.Off click (stock on1/on2, off1..off4);
 - index.html with players, grouped.
usage: post.py <out.zip> <dest dir>"""
import html
import json
import os
import random
import subprocess
import sys
import zipfile

import numpy as np
import soundfile as sf

from refs_survey import SOURCES, clips

SR = 44100
NFFT = 4096


def resample(a, sr_in, sr_out=SR):
    if sr_in == sr_out:
        return a.astype(np.float32)
    p = subprocess.run(["ffmpeg", "-v", "error", "-f", "f32le", "-ar", str(sr_in), "-ac", "1", "-i", "pipe:0",
                        "-af", "aresample=resampler=soxr", "-ar", str(sr_out), "-f", "f32le", "pipe:1"],
                       input=a.astype(np.float32).tobytes(), capture_output=True, check=True)
    return np.frombuffer(p.stdout, dtype=np.float32)


def decode(data):
    p = subprocess.run(["ffmpeg", "-v", "error", "-i", "pipe:0", "-ac", "1", "-ar", str(SR), "-f", "f32le", "pipe:1"],
                       input=data, capture_output=True, check=True)
    return np.frombuffer(p.stdout, dtype=np.float32)


def norm(a, peak=0.89):
    m = float(np.abs(a).max())
    return a * (peak / m) if m > 1e-6 else a


def ltas(signals):
    """average power spectrum of the speech frames (frames within 35 dB of the loudest)"""
    win = np.hanning(NFFT).astype(np.float32)
    acc, n = np.zeros(NFFT // 2 + 1), 0
    for a in signals:
        if len(a) < NFFT:
            continue
        fr = np.lib.stride_tricks.sliding_window_view(a, NFFT)[::NFFT // 2]
        e = (fr ** 2).mean(1)
        keep = e > e.max() * 10 ** (-35 / 10)
        acc += (np.abs(np.fft.rfft(fr[keep] * win, axis=1)) ** 2).sum(0)
        n += int(keep.sum())
    return acc / max(n, 1)


def third_octave_smooth(p):
    f = np.fft.rfftfreq(NFFT, 1 / SR)
    out = np.empty_like(p)
    for i, fc in enumerate(f):
        lo, hi = fc / 2 ** (1 / 6), fc * 2 ** (1 / 6)
        m = (f >= lo) & (f <= hi)
        out[i] = p[m].mean() if m.any() else p[i]
    return out


def match_fir(ref_p, src_p, taps=2047):
    f = np.fft.rfftfreq(NFFT, 1 / SR)
    g = np.sqrt(third_octave_smooth(ref_p) / np.maximum(third_octave_smooth(src_p), 1e-20))
    g /= g[np.argmin(np.abs(f - 1000))]
    gdb = np.clip(20 * np.log10(np.maximum(g, 1e-9)), -40, 12)
    h = np.fft.irfft(10 ** (gdb / 20), NFFT)
    h = np.roll(h, taps // 2)[:taps] * np.hanning(taps)
    return h.astype(np.float32), f, gdb


def fir(a, h):
    n = len(a) + len(h) - 1
    m = 1 << (n - 1).bit_length()
    y = np.fft.irfft(np.fft.rfft(a, m) * np.fft.rfft(h, m), m)[:n]
    d = len(h) // 2
    return y[d:d + len(a)].astype(np.float32)


def write(path, a):
    sf.write(path, np.clip(a, -1, 1), SR, subtype="PCM_16")


def main(dest, job_path=os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "job", "job.json")):
    raw_dir = os.path.join(dest, "raw")
    job = json.load(open(job_path, encoding="utf-8"))
    log = {"refs": {k: v["ref_text"] for k, v in job["voices"].items() if k not in job.get("designs", {})}}
    qc = {}
    for fn in os.listdir(raw_dir):
        if fn.startswith("qc_clone_"):
            qc.update(json.load(open(os.path.join(raw_dir, fn), encoding="utf-8")))
    for sub in ("source", "matched", "radio"):
        os.makedirs(os.path.join(dest, sub), exist_ok=True)

    # every line -> 44.1k PCM16
    lines = {}
    for it in job["items"]:
        p = os.path.join(raw_dir, it["id"] + ".wav")
        if not os.path.exists(p):
            continue
        a, sr = sf.read(p, dtype="float32")
        a = norm(resample(a, sr))
        write(os.path.join(dest, "source", it["id"] + ".wav"), a)
        lines[it["id"]] = a
    for fn in os.listdir(raw_dir):
        if fn.startswith("design_"):
            a, sr = sf.read(os.path.join(raw_dir, fn), dtype="float32")
            write(os.path.join(dest, "source", fn), norm(resample(a, sr)))

    # spectrum match for the processed stock voices
    report = {}
    clicks_on, clicks_off = [], []
    for name, data in clips(SOURCES["metrocop"]["english"]):
        if name in ("on1.wav", "on2.wav"):
            clicks_on.append(decode(data))
        elif name in ("off1.wav", "off2.wav", "off3.wav", "off4.wav"):
            clicks_off.append(decode(data))
    rnd = random.Random(7)
    for voice, lang, prefix in (("metrocop", "russian", "cp_ru_"), ("metrocop", "english", "cp_en_"),
                                ("overwatch", "russian", "ow_ru_"), ("overwatch", "english", "ow_en_")):
        ids = [i for i in lines if i.startswith(prefix)]
        if not ids:
            continue
        orig = [decode(d) for n, d in clips(SOURCES[voice][lang])
                if not n.startswith(("on1", "on2", "off", "pain", "die", "fire", "lostradio", "radio"))]
        ref_p = ltas(orig)
        src_p = ltas([lines[i] for i in ids])
        h, f, gdb = match_fir(ref_p, src_p)
        bands = [125, 250, 500, 1000, 2000, 3000, 4000, 6000, 8000, 12000]
        report[prefix] = {"orig_clips": len(orig), "eq_db": {b: round(float(gdb[np.argmin(np.abs(f - b))]), 1) for b in bands}}
        for i in ids:
            y = norm(fir(lines[i], h))
            write(os.path.join(dest, "matched", i + ".wav"), y)
            if voice == "metrocop":
                on, off = rnd.choice(clicks_on), rnd.choice(clicks_off)
                gap = np.zeros(int(0.04 * SR), np.float32)
                write(os.path.join(dest, "radio", i + ".wav"),
                      np.concatenate([norm(on, 0.6), gap, y, gap, norm(off, 0.6)]))
        # how close is the clone to the original before / after (LTAS distance 200 Hz - 8 kHz, dB)
        band = (f > 200) & (f < 8000)
        def dist(p):
            d = 10 * np.log10(third_octave_smooth(p)[band] / third_octave_smooth(ref_p)[band])
            return round(float(np.sqrt(((d - d.mean()) ** 2).mean())), 2)
        report[prefix]["ltas_rms_db_before"] = dist(src_p)
        report[prefix]["ltas_rms_db_after"] = dist(ltas([fir(lines[i], h) for i in ids]))

    # listening page
    groups = [("ГО по-русски (клон русской озвучки HL2)", "cp_ru_"), ("ГО по-английски (клон оригинала)", "cp_en_"),
              ("Надзор по-русски", "ow_ru_"), ("Надзор по-английски", "ow_en_"),
              ("Торговец стоковым голосом горожанина", "trader_male01_"),
              ("Новый голос: торговец (по описанию)", "trader_new_"), ("Новый голос: женщина (по описанию)", "woman_new_")]
    text = {it["id"]: it["text"] for it in job["items"]}
    rows = ["<!doctype html><meta charset='utf-8'><title>Проба озвучки</title>",
            "<style>body{font:15px Segoe UI,sans-serif;background:#1b1b1b;color:#ddd;margin:24px}"
            "h2{color:#f0b040;margin-top:28px}td{padding:4px 10px;vertical-align:middle}"
            "audio{height:30px;width:260px}.t{max-width:520px}.s{color:#888;font-size:12px}</style>",
            "<h1>Проба озвучки Qwen3-TTS 1.7B</h1>",
            "<p class=s>«сырой» — как дала модель; «по спектру» — подогнан к оригинальным записям HL2; "
            "«рация» — щелчок включения + голос по спектру + щелчок выключения (стоковые on/off ГО).</p>"]
    for title, prefix in groups:
        ids = [i for i in lines if i.startswith(prefix)]
        if not ids:
            continue
        rows.append("<h2>%s</h2><table>" % html.escape(title))
        for i in ids:
            extra = ""
            q = qc.get(i)
            if q and "best" in q:
                b = q["takes"][q["best"]]
                extra = "<br>Whisper услышал: «%s» · ошибок %.0f%% · дубль %d из %d" % (
                    html.escape(b["hyp"]), 100 * b["cer"], q["best"] + 1, len(q["takes"]))
                if q.get("text_used") and q["text_used"] != text[i]:
                    extra += "<br>в модель подано: «%s»" % html.escape(q["text_used"]).replace(
                        "́", "́")
            cells = ["<td class=t>%s<div class=s>%s · %.1f с%s</div></td>" % (html.escape(text[i]), i, len(lines[i]) / SR, extra)]
            for sub, lab in (("source", "сырой"), ("matched", "по спектру"), ("radio", "рация")):
                if os.path.exists(os.path.join(dest, sub, i + ".wav")):
                    cells.append("<td>%s<br><audio controls preload=none src='%s/%s.wav'></audio></td>" % (lab, sub, i))
            rows.append("<tr>%s</tr>" % "".join(cells))
        rows.append("</table>")
    rows.append("<h2>Образцы новых голосов (сделаны по описанию, с них клонируется)</h2><table>")
    for did, d in job["designs"].items():
        rows.append("<tr><td class=t>%s<div class=s>%s</div></td><td><audio controls preload=none src='source/design_%s.wav'>"
                    "</audio></td></tr>" % (html.escape(d["text"]), html.escape(d["instruct"]), did))
    rows.append("</table>")
    rows.append("<h2>Расшифровки образцов (Whisper)</h2><ul>%s</ul>" %
                "".join("<li><b>%s</b>: %s</li>" % (k, html.escape(v)) for k, v in log.get("refs", {}).items()))
    with open(os.path.join(dest, "index.html"), "w", encoding="utf-8") as fo:
        fo.write("\n".join(rows))
    json.dump(report, open(os.path.join(dest, "match_report.json"), "w", encoding="utf-8"), ensure_ascii=False, indent=1)
    print(json.dumps(report, ensure_ascii=False, indent=1))
    print("lines:", len(lines))


if __name__ == "__main__":
    main(sys.argv[1], *(sys.argv[2:3]))
