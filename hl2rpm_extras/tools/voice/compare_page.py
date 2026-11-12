"""Side by side: the Qwen3-TTS test (test3) and the Fish Audio test (fish_test) of the same 28 lines.
usage: python compare_page.py  -> voice_gen/compare_qwen_fish.html"""
import html
import json
import os

HERE = os.path.dirname(os.path.abspath(__file__))
job = json.load(open(os.path.join(HERE, "job", "job.json"), encoding="utf-8"))
fish = {}
p = os.path.join(HERE, "fish_test", "raw", "fish_report.json")
if os.path.exists(p):
    fish = json.load(open(p, encoding="utf-8"))
GROUPS = [("ГО по-русски", "cp_ru_"), ("ГО по-английски", "cp_en_"), ("Надзор по-русски", "ow_ru_"), ("Надзор по-английски", "ow_en_"),
          ("Торговец стоковым голосом горожанина", "trader_male01_"), ("Торговец, голос по описанию", "trader_new_"),
          ("Женщина, голос по описанию", "woman_new_")]


def cell(base, sub, iid, label):
    path = os.path.join(HERE, base, sub, iid + ".wav")
    if not os.path.exists(path):
        return "<td class=s>—</td>"
    return "<td>%s<br><audio controls preload=none src='%s/%s/%s.wav'></audio></td>" % (label, base, sub, iid)


rows = ["<!doctype html><meta charset='utf-8'><title>Qwen против Fish</title>",
        "<style>body{font:15px Segoe UI,sans-serif;background:#1b1b1b;color:#ddd;margin:24px}h2{color:#f0b040;margin-top:28px}"
        "td{padding:4px 10px;vertical-align:middle}audio{height:30px;width:230px}.t{max-width:420px}.s{color:#888;font-size:12px}"
        "th{color:#9cf;text-align:left;padding:4px 10px}</style>",
        "<h1>Одни и те же фразы: Qwen3-TTS 1.7B (ударения LoRA + RUAccent) и Fish Audio s2.1-pro-free</h1>",
        "<p class=s>Голоса - клоны одних и тех же образцов. «рация» - щелчок включения, голос, подогнанный по спектру к "
        "оригиналу HL2, щелчок выключения.</p>"]
for title, prefix in GROUPS:
    ids = [it["id"] for it in job["items"] if it["id"].startswith(prefix)]
    if not ids:
        continue
    radio = prefix.startswith("cp_")
    rows.append("<h2>%s</h2><table><tr><th>фраза</th><th>Qwen</th>%s<th>Fish</th>%s</tr>" % (
        html.escape(title), "<th>Qwen, рация</th>" if radio else "", "<th>Fish, рация</th>" if radio else ""))
    for iid in ids:
        it = next(x for x in job["items"] if x["id"] == iid)
        note = ""
        if iid in fish and "error" in fish[iid]:
            note = "<div class=s>Fish: %s</div>" % html.escape(fish[iid]["error"][:120])
        r = ["<td class=t>%s<div class=s>%s</div>%s</td>" % (html.escape(it["text"]), iid, note),
             cell("test3", "source", iid, "Qwen")]
        if radio:
            r.append(cell("test3", "radio", iid, "Qwen, рация"))
        r.append(cell("fish_test", "source", iid, "Fish"))
        if radio:
            r.append(cell("fish_test", "radio", iid, "Fish, рация"))
        rows.append("<tr>%s</tr>" % "".join(r))
    rows.append("</table>")
open(os.path.join(HERE, "compare_qwen_fish.html"), "w", encoding="utf-8").write("\n".join(rows))
print("written", os.path.join(HERE, "compare_qwen_fish.html"))
