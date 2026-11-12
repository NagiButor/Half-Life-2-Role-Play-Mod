"""What the NPCs of the CK project say (dialog lines, returning lines, after-lines, shared topics as exported,
barks of the NPC and of the bark sets it uses) and what is voiced already. Writes inventory.json.
usage: <CK venv python> inventory.py [project.hl2dlg]"""
import json
import os
import sys

sys.stdout.reconfigure(encoding="utf-8", errors="replace")
CK = r"E:\SourceModding\HL2RPMSOURCE\source-sdk-2013-mapbase-v8.0\sp\src\hl2rpm_extras\tools\dialog_editor"
sys.path.insert(0, CK)
from dlgedit import model, exporter  # noqa: E402

PATH = sys.argv[1] if len(sys.argv) > 1 else r"E:\SourceModding\HL2RPMSOURCE\dialogsrc\test_dialogs.hl2dlg"
p = model.Project.load(PATH)
ex = exporter.Exporter(p)
out = {"npcs": {}, "bark_sets": {}}
for npc in p.npcs:
    a = npc.actor.resolved(p) if hasattr(npc.actor, "resolved") else npc.actor
    if getattr(npc.actor, "is_template", False):
        continue
    n2 = ex.with_topics(npc)
    rows = []
    for key, texts, voices, where in model.iter_voiced_texts(n2):
        for lang in p.languages:
            t = (texts or {}).get(lang, "")
            if not t:
                continue
            v = (voices or {}).get(lang)
            rows.append({"key": key, "lang": lang, "text": t, "voiced": bool(v and not v.is_empty()),
                         "topic": key.split("_")[0].isdigit() and int(key.split("_")[0]) >= ex.TOPIC_NODE})
    barks = []
    for b in getattr(npc.actor, "barks", []) or []:
        for lang in p.languages:
            t = b.text.get(lang, "")
            if t:
                v = b.voices.get(lang)
                barks.append({"id": b.id, "event": b.event, "lang": lang, "text": t,
                              "voiced": bool(v and not v.is_empty())})
    out["npcs"][npc.targetname] = {
        "classname": getattr(a, "classname", ""), "model": getattr(a, "model", "") or "",
        "models": list(getattr(a, "models", []) or []), "faction": getattr(a, "faction", ""),
        "bark_sets": list(getattr(npc.actor, "bark_sets", []) or []), "maps": sorted({r.get("map", "") for r in []}),
        "lines": rows, "barks": barks}
for s in p.bark_sets:
    rows = []
    for b in s.barks:
        for lang in p.languages:
            t = b.text.get(lang, "")
            if t:
                v = b.voices.get(lang)
                rows.append({"id": b.id, "event": b.event, "lang": lang, "text": t, "voiced": bool(v and not v.is_empty())})
    out["bark_sets"][s.id] = {"classes": list(getattr(s, "classes", []) or []), "barks": rows}
json.dump(out, open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "inventory.json"), "w", encoding="utf-8"),
          ensure_ascii=False, indent=1)
for name, d in out["npcs"].items():
    lines = [r for r in d["lines"] if not r["voiced"]]
    barks = [r for r in d["barks"] if not r["voiced"]]
    if lines or barks or d["lines"] or d["barks"]:
        print("%-22s %-18s %-40s unvoiced lines %2d (topics %d), barks %2d | voiced %d | sets %s" % (
            name, d["classname"], (d["model"] or ";".join(d["models"]))[:40], len(lines),
            sum(r["topic"] for r in lines), len(barks), sum(r["voiced"] for r in d["lines"] + d["barks"]), d["bark_sets"]))
for sid, d in out["bark_sets"].items():
    print("set %-18s classes %s: unvoiced %d" % (sid, d["classes"], sum(not r["voiced"] for r in d["barks"])))
