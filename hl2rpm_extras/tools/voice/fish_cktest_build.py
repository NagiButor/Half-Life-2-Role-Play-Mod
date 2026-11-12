"""The ck_test voicing for Fish Audio: the same targets as the Qwen job (job_cktest/targets.json, built by
cktest_build.py from the CK project) with Fish's own spoken form - emotion cues in square brackets by the kind of
line (bark event / who speaks / what is said), codes and times as words. Same (voice, language, spoken form) once.
Writes job_cktest_fish/job.json, targets.json, refs/."""
import hashlib
import json
import os
import shutil
import sys

sys.stdout.reconfigure(encoding="utf-8", errors="replace")
HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, "job_cktest")
OUT = os.path.join(HERE, "job_cktest_fish")

SPEAK = {
    "russian": [("ГО", "гэ-о"), ("10-04", "десять-четыре"), ("10-08", "десять-восемь"), ("22:00", "двадцати двух ноль-ноль")],
    "english": [("22:00", "twenty-two hundred")],
}
BARK_CUE = {  # bark id prefix -> cue
    "combat": "[shouting]", "hurt": "[scared][shouting]", "theft": "[angry][shouting]", "weapon": "[angry][shouting]",
}
TEXT_CUE = [  # (substring of the English or Russian text, cue) - checked in order, first match wins
    ("Проваливай", "[angry]"), ("Чего уставился", "[contemptuous]"),
    ("Запрашиваю", "[calm]"), ("Диспетчер", "[calm]"), ("Внимание всем", "[determined]"),
    ("Комендантский час", "[indifferent]"), ("Curfew", "[indifferent]"), ("Сектор семнадцать", "[bored]"),
    ("Sector seventeen", "[bored]"), ("кто-то вскрыл", "[doubtful]"), ("opened the caretaker", "[doubtful]"),
    ("ничего не говорил", "[nervous]"), ("told you nothing", "[nervous]"),
    ("пайки опять урезали", "[resigned]"), ("rations were cut", "[resigned]"),
    ("Пора спать", "[slightly tired]"), ("Time to sleep", "[slightly tired]"), ("Опять льёт", "[sighing]"),
    ("Pouring again", "[sighing]"), ("переселят", "[worried]"), ("relocate", "[worried]"),
    ("новенький", "[curious]"), ("new face", "[curious]"), ("Жетоны есть", "[friendly]"), ("Got tokens", "[friendly]"),
    ("Подходи", "[friendly]"), ("Come over", "[friendly]"), ("Пайки, свежие", "[friendly]"), ("Rations, fresh", "[friendly]"),
    ("Опять ты", "[friendly]"), ("You again", "[friendly]"), ("Хм...", "[hesitant]"), ("Hm...", "[hesitant]"),
    ("Эй! Погоди", "[excited]"), ("Hey! Wait up", "[excited]"),
]


def spoken(t):
    text = t["text"]
    for a, b in SPEAK.get(t["lang"], []):
        text = text.replace(a, b)
    cue = ""
    if t["key"].startswith("bark_"):
        ev = t["key"][len("bark_"):].rstrip("0123456789")
        cue = BARK_CUE.get(ev, "")
    if not cue:
        for sub, c in TEXT_CUE:
            if sub in t["text"]:
                cue = c
                break
    return (cue + " " + text).strip() if cue else text


def main():
    job = json.load(open(os.path.join(SRC, "job.json"), encoding="utf-8"))
    targets = json.load(open(os.path.join(SRC, "targets.json"), encoding="utf-8"))
    vid_of = {it["id"]: it["voice"] for it in job["items"]}
    items, seen = [], {}
    for t in targets:
        vid = vid_of[t["item"]]
        sp = spoken(t)
        k = (vid, t["lang"], sp)
        if k not in seen:
            iid = "%s_%s_%s" % (vid, t["lang"][:2], hashlib.md5(sp.encode("utf-8")).hexdigest()[:8])
            seen[k] = iid
            items.append({"id": iid, "voice": vid, "text": t["text"], "speak": sp, "lang": t["lang"].capitalize()})
        t["item"] = seen[k]
    if os.path.isdir(OUT):
        shutil.rmtree(OUT)
    shutil.copytree(os.path.join(SRC, "refs"), os.path.join(OUT, "refs"))
    json.dump({"voices": job["voices"], "items": items}, open(os.path.join(OUT, "job.json"), "w", encoding="utf-8"),
              ensure_ascii=False, indent=1)
    json.dump(targets, open(os.path.join(OUT, "targets.json"), "w", encoding="utf-8"), ensure_ascii=False, indent=1)
    print("targets:", len(targets), "| lines:", len(items), "| with a cue:", sum(it["speak"].startswith("[") for it in items))
    for it in items[:200]:
        if it["speak"].startswith("["):
            print("  ", it["voice"], "|", it["speak"][:80])


if __name__ == "__main__":
    main()
