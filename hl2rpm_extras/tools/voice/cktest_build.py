"""Build the voice job for every speaking character of ck_test from the CK project:
dialog lines, returning lines, after-lines, shared topics and the topic greeting (as the export puts them into
each NPC's dialog), the NPC's barks and the bark sets. Same (voice, language, text) is generated once.
Writes voice_gen/job_cktest/job.json + refs/ and voice_gen/job_cktest/targets.json (where every result goes).
usage: <CK venv python> cktest_build.py"""
import hashlib
import io
import json
import os
import shutil
import subprocess
import sys

import numpy as np
import soundfile as sf

sys.stdout.reconfigure(encoding="utf-8", errors="replace")
HERE = os.path.dirname(os.path.abspath(__file__))
CK = r"E:\SourceModding\HL2RPMSOURCE\source-sdk-2013-mapbase-v8.0\sp\src\hl2rpm_extras\tools\dialog_editor"
sys.path.insert(0, CK)
sys.path.insert(0, os.path.join(HERE, "colab"))
from dlgedit import model, exporter, sounddata  # noqa: E402
import refs_survey as R  # noqa: E402

PROJECT = r"E:\SourceModding\HL2RPMSOURCE\dialogsrc\test_dialogs.hl2dlg"
OUT = os.path.join(HERE, "job_cktest")
OLD_REFS = os.path.join(HERE, "job", "refs")
SR = 24000

# who speaks with which voice (profile -> base voice + post-processing)
PROFILES = {
    "cp": ("metrocop", "radio_cp"), "combine": ("combine", "radio_combine"), "male01": ("male01", ""),
    "female01": ("female01", ""), "trader": ("trader_new", ""), "worker": ("worker_new", ""),
    "male01_mask": ("male01", "mask"), "worker_mask": ("worker_new", "mask"),
}
NPC_PROFILE = {
    "ck_guard": "cp", "ck_talker": "male01", "ck_wanderer": "female01", "ck_worker": "worker", "ck_trader": "trader",
    "ck_cmb_worker1": "male01", "ck_cmb_worker2": "worker", "ck_hazmat1": "male01_mask", "ck_hazmat2": "worker_mask",
}
COMBINE = ("ck_synth_elite", "ck_synth_elite2", "ck_super_soldier", "ck_super_soldier2", "ck_cremator",
           "ck_concept_elite", "ck_hazmat_cmb")
SET_PROFILE = {"ck_citizens": "male01", "ck_citizens_f": "female01", "ck_citizens_t": "trader",
               "ck_citizens_w": "worker", "ck_citizens_m": "male01_mask", "ck_citizens_wm": "worker_mask"}
DELTA = os.path.join(HERE, "job_cktest2")   # only what is not generated yet (raw results in cktest/raw)
RAW = os.path.join(HERE, "cktest", "raw")
REDO = {"female01_en_en_3cb8719b", "female01_ru_ru_4329e445"}  # misread takes: generated again (another seed)

# how a written line is said (abbreviations, codes, times); the text itself stays as written
SPEAK = {
    "russian": [("ГО", "гэ-о"), ("10-04", "десять-четыре"), ("10-08", "десять-восемь"), ("22:00", "двадцати двух ноль-ноль")],
    "english": [("22:00", "twenty-two hundred"), ("CPs", "C.P.s")],
}
# typos in the project's own lines (fixed in the project too)
TYPOS = [("антиобщественно деятельности", "антиобщественной деятельности"), ("апмутации", "ампутации")]

NEW_REFS = {  # stock voices not in the first test
    "female01": ("female01", ["question23.wav", "question31.wav", "question04.wav"]),
    "combine": ("combine", ["prison_soldier_negativecontainment.wav", "prison_soldier_tohighpoints.wav"]),
}
R.SOURCES["female01"] = {"english": ("vpk", "hl2_sound_vo_english_dir.vpk", "sound/vo/npc/female01/"),
                         "russian": ("vpk", "hl2_sound_vo_russian_dir.vpk", "sound/vo/npc/female01/")}
R.SOURCES["combine"] = {"english": ("dir", os.path.join(R.UNPACKED, "sound", "npc", "combine_soldier", "vo")),
                        "russian": ("vpk", "hl2_sound_vo_russian_dir.vpk", "sound/npc/combine_soldier/vo/")}
DESIGNS = {
    "worker_new": {"instruct": "A man in his forties, a manual laborer working for the occupiers in a grim city. Low, rough, "
                               "tired voice, slightly hoarse from dust, plain unhurried speech. Natural, not theatrical.",
                   "text": "Смена опять затянулась, норму подняли, а пайки урезали. Ну, чего тебе надо? Говори быстрее.",
                   "lang": "Russian"},
}


def speak(text, lang):
    for a, b in SPEAK.get(lang, []):
        text = text.replace(a, b)
    return text


def decode(data):
    p = subprocess.run(["ffmpeg", "-v", "error", "-i", "pipe:0", "-ac", "1", "-ar", str(SR), "-f", "f32le", "pipe:1"],
                       input=data, capture_output=True, check=True)
    return np.frombuffer(p.stdout, dtype=np.float32)


def build_ref(voice, lang, names, path):
    got = {}
    for n, d in R.clips(R.SOURCES[voice][lang]):
        if n in names:
            got[n] = decode(d)
    parts = []
    for n in names:
        a = got[n]
        parts += [a / max(1e-6, float(np.abs(a).max())) * 0.89, np.zeros(int(0.35 * SR), np.float32)]
    sf.write(path, np.concatenate(parts[:-1]), SR, subtype="PCM_16")


def main():
    p = model.Project.load(PROJECT)
    for npc in p.npcs:  # typo fixes in the lines themselves
        for n in npc.nodes:
            for lang, t in list(n.line.items()):
                for a, b in TYPOS:
                    t = t.replace(a, b)
                n.line[lang] = t
        for lang, t in list(npc.returning.line.items()):
            for a, b in TYPOS:
                t = t.replace(a, b)
            npc.returning.line[lang] = t
    # the woman gets her own copy of the citizens' barks (a set is voiced once - a male voice for everyone)
    cit = p.bark_set("ck_citizens")
    fem = p.bark_set("ck_citizens_f")
    if cit and not fem:
        fem = sounddata.BarkSet.from_dict(json.loads(json.dumps(cit.to_dict())))
        fem.id = "ck_citizens_f"
        fem.classes = []
        fem.note = "Демо ck_test: общие фразы горожан, женский голос (ck_wanderer)"
        for b in fem.barks:
            b.voices = {}
        p.bark_sets.append(fem)
    wand = p.npc("ck_wanderer")
    if wand is not None:
        wand.actor.bark_sets = ["ck_citizens_f"]
    p.save(PROJECT)

    ex = exporter.Exporter(p)
    targets = []  # owner, lang, key, text, profile

    def add(owner, lang, key, text, prof):
        if text:
            targets.append({"owner": owner, "lang": lang, "key": key, "text": text, "profile": prof})

    names = list(NPC_PROFILE) + list(COMBINE)
    for name in names:
        npc = p.npc(name)
        if npc is None:
            print("no NPC", name)
            continue
        prof = NPC_PROFILE.get(name, "combine")
        for key, texts, voices, where in model.iter_voiced_texts(npc):
            for lang in p.languages:
                add(name, lang, key, (texts or {}).get(lang, ""), prof)
        n2 = ex.with_topics(npc)
        for nd in n2.nodes:
            if getattr(nd, "topic", "") and nd.id >= ex.TOPIC_NODE or getattr(nd, "topic", "") == "greeting":
                for lang in p.languages:
                    add(name, lang, model.media_key_node(nd.id), nd.line.get(lang, ""), prof)
        for b in npc.actor.barks or []:
            for lang in p.languages:
                add(world_owner(p, npc), lang, sounddata.bark_media_key(b.id), b.text.get(lang, ""), prof)
    for sid, prof in SET_PROFILE.items():
        s = p.bark_set(sid)
        for b in s.barks if s else []:
            for lang in p.languages:
                add("barks_" + sid, lang, sounddata.bark_media_key(b.id), b.text.get(lang, ""), prof)

    # voices of the job
    os.makedirs(os.path.join(OUT, "refs"), exist_ok=True)
    old = json.load(open(os.path.join(HERE, "job", "job.json"), encoding="utf-8"))["voices"]
    voices = {}
    for vid, v in old.items():
        if vid in ("metrocop_en", "metrocop_ru", "male01_en", "male01_ru", "trader_new"):
            shutil.copy(os.path.join(OLD_REFS, vid + ".wav"), os.path.join(OUT, "refs", vid + ".wav"))
            voices[vid] = {"ref": "refs/%s.wav" % vid, "lang": v["lang"], "ref_text": v["ref_text"]}
    for base, (src, clips) in NEW_REFS.items():
        for lang in ("english", "russian"):
            vid = "%s_%s" % (base, lang[:2])
            path = os.path.join(OUT, "refs", vid + ".wav")
            if not os.path.exists(path):
                build_ref(src, lang, clips, path)
            voices[vid] = {"ref": "refs/%s.wav" % vid, "lang": lang.capitalize(), "ref_text": None}

    def voice_id(prof, lang):
        base = PROFILES[prof][0]
        if base in ("trader_new", "worker_new"):
            return base
        return "%s_%s" % (base, lang[:2])

    items, seen = [], {}
    for t in targets:
        vid = voice_id(t["profile"], t["lang"])
        sp = speak(t["text"], t["lang"])
        k = (vid, t["lang"], sp)
        if k not in seen:
            iid = "%s_%s_%s" % (vid, t["lang"][:2], hashlib.md5(sp.encode("utf-8")).hexdigest()[:8])
            seen[k] = iid
            items.append({"id": iid, "voice": vid, "text": t["text"], "speak": sp, "lang": t["lang"].capitalize()})
        t["item"] = seen[k]
        t["post"] = PROFILES[t["profile"]][1]
    items.sort(key=lambda it: (it["lang"] != "English", it["voice"]))
    # voices made / transcribed by an earlier run of this job are kept
    prev = os.path.join(OUT, "job.json")
    if os.path.exists(prev):
        for vid, v in json.load(open(prev, encoding="utf-8"))["voices"].items():
            if vid not in voices or (v.get("ref_text") and not voices[vid].get("ref_text")):
                voices[vid] = v
    job = {"voices": voices, "designs": DESIGNS, "items": items, "seed": 4242}
    json.dump(job, open(os.path.join(OUT, "job.json"), "w", encoding="utf-8"), ensure_ascii=False, indent=1)
    # the delta job: lines without a result yet (+ the takes to redo), seed shifted so a redo is a new take
    for iid in REDO:
        if os.path.exists(os.path.join(RAW, iid + ".wav")):
            os.replace(os.path.join(RAW, iid + ".wav"), os.path.join(RAW, iid + ".old.wav"))
    todo = [it for it in items if not os.path.exists(os.path.join(RAW, it["id"] + ".wav"))]
    if os.path.isdir(DELTA):
        shutil.rmtree(DELTA)
    shutil.copytree(os.path.join(OUT, "refs"), os.path.join(DELTA, "refs"))
    json.dump({"voices": voices, "designs": DESIGNS, "items": todo, "seed": 777}, open(os.path.join(DELTA, "job.json"), "w",
              encoding="utf-8"), ensure_ascii=False, indent=1)
    print("still to generate:", len(todo), [it["id"] for it in todo][:40])
    json.dump(targets, open(os.path.join(OUT, "targets.json"), "w", encoding="utf-8"), ensure_ascii=False, indent=1)
    print("targets:", len(targets), "| unique lines to generate:", len(items),
          "| chars:", sum(len(i["speak"]) for i in items))
    by = {}
    for t in targets:
        by[t["owner"]] = by.get(t["owner"], 0) + 1
    print(by)


def world_owner(p, npc):
    from dlgedit import world_export
    return world_export.bark_owner(p, npc)


if __name__ == "__main__":
    main()
