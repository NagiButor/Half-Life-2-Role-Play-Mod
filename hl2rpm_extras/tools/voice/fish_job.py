"""Voice a job with the Fish Audio API (zero-shot cloning: every request carries the voice's reference clip and
its transcript, MessagePack body). Key: FISH_AUDIO_API_KEY (environment / HKCU\\Environment), sent only in the
Authorization header, never printed.
usage: fish_venv python fish_job.py <job dir> <out dir> [--model s2.1-pro-free] [--only id1,id2]"""
import argparse
import json
import os
import sys
import time
import urllib.error
import urllib.request
import winreg

import msgpack

sys.stdout.reconfigure(encoding="utf-8", errors="replace")
SPEAK = {  # how a written line is said (same table as the Qwen jobs)
    "Russian": [("ГО", "гэ-о"), ("10-04", "десять-четыре"), ("10-08", "десять-восемь"), ("22:00", "двадцати двух ноль-ноль")],
    "English": [("22:00", "twenty-two hundred")],
}


def key():
    k = os.environ.get("FISH_AUDIO_API_KEY", "")
    if not k:
        with winreg.OpenKey(winreg.HKEY_CURRENT_USER, "Environment") as h:
            k = winreg.QueryValueEx(h, "FISH_AUDIO_API_KEY")[0]
    return k.strip()


def tts(k, model, body, tries=5):
    data = msgpack.packb(body, use_bin_type=True)
    for i in range(tries):
        req = urllib.request.Request("https://api.fish.audio/v1/tts", data=data, headers={
            "Authorization": "Bearer " + k, "Content-Type": "application/msgpack", "model": model})
        try:
            with urllib.request.urlopen(req, timeout=300) as r:
                return 200, r.read()
        except urllib.error.HTTPError as e:
            msg = e.read()[:300].decode("utf-8", "replace").replace(k, "<key>")
            if e.code in (429, 503) and i < tries - 1:
                time.sleep(5 * (i + 1))
                continue
            return e.code, msg
        except Exception as e:
            if i < tries - 1:
                time.sleep(5)
                continue
            return None, repr(e).replace(k, "<key>")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("job")
    ap.add_argument("out")
    ap.add_argument("--model", default="s2.1-pro-free")
    ap.add_argument("--only", default="")
    a = ap.parse_args()
    k = key()
    job = json.load(open(os.path.join(a.job, "job.json"), encoding="utf-8"))
    os.makedirs(a.out, exist_ok=True)
    only = set(x for x in a.only.split(",") if x)
    refs = {}
    report = {}
    t0 = time.time()
    for it in job["items"]:
        if only and it["id"] not in only:
            continue
        dst = os.path.join(a.out, it["id"] + ".wav")
        if os.path.exists(dst):
            continue
        v = job["voices"][it["voice"]]
        if it["voice"] not in refs:
            refs[it["voice"]] = open(os.path.join(a.job, v["ref"]), "rb").read()
        text = it.get("speak") or it["text"]
        for x, y in SPEAK.get(it["lang"], []):
            text = text.replace(x, y)
        body = {"text": text, "references": [{"audio": refs[it["voice"]], "text": v["ref_text"]}],
                "format": "wav", "sample_rate": 44100, "latency": "normal", "normalize": True,
                "temperature": 0.7, "top_p": 0.7}
        t = time.time()
        st, res = tts(k, a.model, body)
        if st == 200 and res[:4] == b"RIFF":
            open(dst, "wb").write(res)
            import soundfile as sf
            info = sf.info(dst)
            report[it["id"]] = {"text_used": text, "sec": round(info.duration, 2), "gen_s": round(time.time() - t, 1)}
            print("%-26s %5.2fs audio in %4.1fs | %s" % (it["id"], info.duration, time.time() - t, text[:60]), flush=True)
        else:
            report[it["id"]] = {"text_used": text, "error": "%s %s" % (st, res if isinstance(res, str) else res[:200])}
            print("%-26s FAILED %s %s" % (it["id"], st, report[it["id"]]["error"][:200]), flush=True)
    json.dump(report, open(os.path.join(a.out, "fish_report.json"), "w", encoding="utf-8"), ensure_ascii=False, indent=1)
    print("done in %.0f s" % (time.time() - t0))


if __name__ == "__main__":
    main()
