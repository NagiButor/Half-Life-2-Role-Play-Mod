# @title HL2RPM voice: one step on the Colab VM (env FIRST, COUNT); state stays in the kernel between steps.
# Qwen3-TTS-12Hz-1.7B-Base: English with the original weights; Russian with the stress LoRA of
# sknyazev/qwen3-tts-12hz-1.7b-ru-stress-gguf merged into the talker and U+0301 stress marks from RUAccent.
# Every take is read back with Whisper large-v3-turbo; up to TRIES takes (+ a timbre-only fallback), length capped.
import gc, json, os, re, time, zipfile
from math import gcd
import numpy as np
import soundfile as sf
import torch
from scipy.signal import resample_poly

JOB = "/content/job"
OUT = "/content/out"
TRIES = 3
os.makedirs(OUT, exist_ok=True)
job = json.load(open(JOB + "/job.json", encoding="utf-8"))
G = globals()
G.setdefault("_st", {"tts": None, "asr": None, "acc": None, "lora": False, "prompts": {}, "xprompts": {}})
S = _st
T0 = time.time()
VOWELS = "аеёиоуыэюяАЕЁИОУЫЭЮЯ"


def log(*a):
    print("[%6.1fs]" % (time.time() - T0), *a, flush=True)


def mem(tag):
    import psutil
    vm = psutil.virtual_memory()
    log("[mem] %s: RAM %.1f / %.1f GB, GPU %.1f GB" % (tag, (vm.total - vm.available) / 2**30, vm.total / 2**30,
                                                       torch.cuda.memory_allocated() / 2**30))


def tts():
    if S["tts"] is None:
        from qwen_tts import Qwen3TTSModel
        S["tts"] = Qwen3TTSModel.from_pretrained("Qwen/Qwen3-TTS-12Hz-1.7B-Base", device_map="cuda:0",
                                                 dtype=torch.float16, attn_implementation="sdpa")
        S["lora"] = False
        mem("after Qwen")
    return S["tts"]


def asr():
    if S["asr"] is None:
        from transformers import pipeline
        S["asr"] = pipeline("automatic-speech-recognition", model="openai/whisper-large-v3-turbo",
                            dtype=torch.float16, device=0, model_kwargs={"low_cpu_mem_usage": True})
        mem("after Whisper")
    return S["asr"]


def numbers(s, lang):
    from num2words import num2words
    return re.sub(r"\d+", lambda m: num2words(int(m.group()), lang="ru" if lang == "Russian" else "en"), s)


def stress(s):
    if S["acc"] is None:
        from ruaccent import RUAccent
        S["acc"] = RUAccent()
        S["acc"].load(omograph_model_size="turbo3.1", use_dictionary=True, tiny_mode=False)
        mem("after RUAccent")
    plus = S["acc"].process_all(s)
    out = re.sub(r"\+([" + VOWELS + "])", lambda m: m.group(1) + "́", plus)

    def fix(w):  # no marks on one-vowel words and on words with ё (the stress model's convention)
        bare = w.group(0).replace("́", "")
        return bare if sum(ch in VOWELS for ch in bare) <= 1 or "ё" in bare.lower() else w.group(0)
    return re.sub(r"[А-Яа-яЁё́-]+", fix, out)


def hear(a, sr, lang):
    g = gcd(16000, sr)
    a16 = resample_poly(a, 16000 // g, sr // g).astype(np.float32)
    return asr()({"raw": a16, "sampling_rate": 16000},
                 generate_kwargs={"language": lang.lower(), "task": "transcribe", "num_beams": 3})["text"].strip()


def words(s, lang):
    s = numbers(s, lang).lower().replace("ё", "е").replace("́", "")
    return " ".join(re.sub(r"[^\w\s]|_", " ", s).split())


def cer(a, b):
    prev = list(range(len(b) + 1))
    for i, ca in enumerate(a, 1):
        cur = [i] + [0] * len(b)
        for j, cb in enumerate(b, 1):
            cur[j] = min(prev[j] + 1, cur[j - 1] + 1, prev[j - 1] + (ca != cb))
        prev = cur
    return prev[-1] / max(1, len(a))


def torch_module(obj):
    for attr in ("model", "_model", "tts_model"):
        m = getattr(obj, attr, None)
        if isinstance(m, torch.nn.Module):
            return m
    for v in vars(obj).values():
        if isinstance(v, torch.nn.Module):
            return v
    raise RuntimeError("no torch module in the TTS wrapper")


def set_lora(on):
    """merge (on) / unmerge (off) the stress LoRA in the talker; prompts are rebuilt after a change"""
    if S["lora"] == on:
        return
    from huggingface_hub import hf_hub_download
    from safetensors.torch import load_file
    repo = "sknyazev/qwen3-tts-12hz-1.7b-ru-stress-gguf"
    cfg = json.load(open(hf_hub_download(repo, "lora/adapter.json")))
    lora = load_file(hf_hub_download(repo, "lora/adapter.safetensors"))
    scale = (1 if on else -1) * cfg["alpha"] / cfg["r"]
    params = dict(torch_module(tts()).named_parameters())
    rx = re.compile(r"(?:^|\.)talker\.model\.(layers\.\d+\.(?:self_attn|mlp)\.\w+_proj)\.weight$")
    target = {m.group(1): n for n in params for m in [rx.search(n)] if m}
    k = 0
    with torch.no_grad():
        for key in lora:
            if key.endswith(".lora_A.weight"):
                mod = key[:-len(".lora_A.weight")]
                p = params[target[mod]]
                p += (scale * (lora[mod + ".lora_B.weight"].float().cuda() @ lora[key].float().cuda())).to(p.dtype)
                k += 1
    S["lora"] = on
    S["prompts"].clear(); S["xprompts"].clear()
    log("stress LoRA %s: %d talker matrices (of %d found)" % ("merged" if on else "removed", k, len(target)))


def prompt(vid, xvec=False):
    d = S["xprompts" if xvec else "prompts"]
    if vid not in d:
        v = job["voices"][vid]
        d[vid] = tts().create_voice_clone_prompt(ref_audio=JOB + "/" + v["ref"], ref_text=None if xvec else v["ref_text"],
                                                 x_vector_only_mode=xvec)
    return d[vid]


if os.environ.get("STAGE") == "prep":
    prep = {"refs": {}, "designs": []}
    todo = {d: x for d, x in job.get("designs", {}).items() if d not in job["voices"]}
    if todo:
        if S["tts"] is not None:
            S["tts"] = None
            S["prompts"].clear(); S["xprompts"].clear()
            gc.collect(); torch.cuda.empty_cache()
        from qwen_tts import Qwen3TTSModel
        vd = Qwen3TTSModel.from_pretrained("Qwen/Qwen3-TTS-12Hz-1.7B-VoiceDesign", device_map="cuda:0",
                                           dtype=torch.float16, attn_implementation="sdpa")
        for did, d in todo.items():
            torch.manual_seed(job.get("seed", 0))
            wavs, sr = vd.generate_voice_design(text=d["text"], language=d["lang"], instruct=d["instruct"])
            sf.write(OUT + "/design_%s.wav" % did, np.asarray(wavs[0], dtype=np.float32), sr, subtype="PCM_16")
            prep["designs"].append(did)
            log("designed", did, "%.1f s" % (len(wavs[0]) / sr))
        del vd
        gc.collect(); torch.cuda.empty_cache()
    for vid, v in job["voices"].items():
        if not v.get("ref_text"):
            a, sr = sf.read(JOB + "/" + v["ref"], dtype="float32")
            prep["refs"][vid] = hear(a, sr, v["lang"])
            log("ref", vid, "->", prep["refs"][vid])
    json.dump(prep, open(OUT + "/prep.json", "w", encoding="utf-8"), ensure_ascii=False, indent=1)
    with zipfile.ZipFile("/content/step_prep_0.zip", "w") as z:
        z.write(OUT + "/prep.json", "prep.json")
        for did in prep["designs"]:
            z.write(OUT + "/design_%s.wav" % did, "design_%s.wav" % did)
    print("packed /content/step_prep_0.zip")
    raise SystemExit
first, count = int(os.environ.get("FIRST", 0)), int(os.environ.get("COUNT", 7))
items = job["items"][first:first + count]
done, report = [], {}
for n, it in enumerate(items, first):
    set_lora(it["lang"] == "Russian")
    spoken = it.get("speak") or it["text"]
    text = numbers(spoken, it["lang"])
    if it["lang"] == "Russian":
        text = stress(text)
    target = words(spoken, it["lang"])
    cap = 2.0 + 2.5 * len(spoken) / 12.0
    best, takes = None, []
    for k in range(TRIES + 1):
        xv = k == TRIES
        if xv and best is not None and best[0] < 0.3:
            break
        torch.manual_seed(job.get("seed", 0) + n + 1000 * k)
        t = time.time()
        try:
            wavs, sr = tts().generate_voice_clone(text=text, language=it["lang"], voice_clone_prompt=prompt(it["voice"], xv),
                                                  max_new_tokens=int(12 * cap) + 8)
        except Exception as e:
            takes.append({"error": repr(e)[:300]})
            continue
        a = np.asarray(wavs[0], dtype=np.float32)
        gen = time.time() - t
        hyp = hear(a, sr, it["lang"])
        c = cer(target, words(hyp, it["lang"]))
        dur = len(a) / sr
        cps = len(spoken) / max(dur, 0.1)
        runaway = dur >= cap - 0.3
        pace = (0 if 6 <= cps <= 22 else min(1.0, abs(np.log(cps / 12)) / 2)) + (1.0 if runaway else 0)
        takes.append({"cer": round(c, 3), "sec": round(dur, 2), "cps": round(cps, 1), "hyp": hyp,
                      "mode": "xvec" if xv else "icl", "runaway": runaway, "gen_s": round(gen, 1)})
        if best is None or c + pace < best[0]:
            best = (c + pace, a, sr, len(takes) - 1)
        if c <= 0.08 and pace == 0:
            break
    if best is None:
        log(it["id"], "FAILED", takes)
        report[it["id"]] = {"text_used": text, "takes": takes}
        continue
    _, a, sr, bi = best
    sf.write(OUT + "/%s.wav" % it["id"], a, sr, subtype="PCM_16")
    done.append(it["id"] + ".wav")
    report[it["id"]] = {"text_used": text, "best": bi, "takes": takes}
    b = takes[bi]
    log("%2d %-24s take %d/%d cer %.2f %.2fs gen %.1fs | %s | heard: %s" % (
        n, it["id"], bi + 1, len(takes), b["cer"], b["sec"], b["gen_s"], text[:45], b["hyp"][:45]))
qn = "qc_clone_%d.json" % first
json.dump(report, open(OUT + "/" + qn, "w", encoding="utf-8"), ensure_ascii=False, indent=1)
done.append(qn)
name = "/content/step_clone_%d.zip" % first
with zipfile.ZipFile(name, "w") as z:
    for fn in done:
        z.write(OUT + "/" + fn, fn)
print("packed", name, len(done), "files, %.1f s" % (time.time() - T0))
