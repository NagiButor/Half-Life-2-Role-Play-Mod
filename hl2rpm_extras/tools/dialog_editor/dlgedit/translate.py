"""Machine translation of the project's texts: Google Translate without a key (the
endpoint of the web widget, translate.googleapis.com/translate_a/single?client=gtx).

Every text of the project is a dict language -> text; a job fills the target languages
from the source language. A text written by the machine is remembered by its hash
(project.mt_marks), so the editor can show "МП" next to it until someone edits it by
hand, and "translate again" never overwrites a manual translation."""
import hashlib
import json
import time
import urllib.error
import urllib.parse
import urllib.request
from concurrent.futures import ThreadPoolExecutor

LANG_CODES = {
    "english": "en", "russian": "ru", "ukrainian": "uk", "german": "de", "french": "fr", "spanish": "es", "latam": "es",
    "italian": "it", "polish": "pl", "brazilian": "pt", "portuguese": "pt", "czech": "cs", "turkish": "tr",
    "japanese": "ja", "koreana": "ko", "schinese": "zh-CN", "tchinese": "zh-TW", "hungarian": "hu", "swedish": "sv",
    "finnish": "fi", "dutch": "nl", "danish": "da", "norwegian": "no", "romanian": "ro", "bulgarian": "bg",
    "greek": "el", "thai": "th", "vietnamese": "vi",
}


def mt_mark(lang, text):
    return hashlib.sha1((lang + "\0" + text).encode("utf-8")).hexdigest()[:12]


def is_mt(project, lang, text):
    return bool(text) and mt_mark(lang, text) in getattr(project, "mt_marks", set())


class TranslateError(Exception):
    pass


def google(text, src, dst, timeout=20):
    """One text; retries with a pause when Google says "too many requests"."""
    url = ("https://translate.googleapis.com/translate_a/single?client=gtx&sl=%s&tl=%s&dt=t&q=%s"
           % (LANG_CODES.get(src, src), LANG_CODES.get(dst, dst), urllib.parse.quote(text)))
    req = urllib.request.Request(url, headers={"User-Agent": "Mozilla/5.0"})
    delay = 2.0
    for attempt in range(6):
        try:
            with urllib.request.urlopen(req, timeout=timeout) as r:
                data = json.loads(r.read().decode("utf-8"))
            return "".join(seg[0] for seg in (data[0] or []) if seg and seg[0])
        except urllib.error.HTTPError as e:
            if e.code in (429, 503) and attempt < 5:
                time.sleep(delay)
                delay *= 2
                continue
            raise TranslateError("HTTP %d" % e.code)
        except (urllib.error.URLError, TimeoutError, OSError) as e:
            if attempt < 2:
                time.sleep(1.0)
                continue
            raise TranslateError(str(e))
    raise TranslateError("нет ответа")


def _logic_texts(lst, label, out):
    for a in lst or []:
        if a.get("type") == "message" and isinstance(a.get("text"), dict):
            out.append((a["text"], label + ": сообщение"))


def collect(project, npcs=None):
    """[(texts dict, where)] - the texts of the project (or of these NPCs only)."""
    out = []
    for npc in project.npcs:
        if npcs is not None and npc.targetname not in npcs:
            continue
        tn = npc.targetname
        out.append((npc.display_name, "%s: имя" % tn))
        out.append((npc.returning.line, "%s: повторный визит" % tn))
        for n in npc.nodes:
            out.append((n.line, "%s #%d" % (tn, n.id)))
            _logic_texts(n.actions, "%s #%d" % (tn, n.id), out)
            for i, o in enumerate(n.options):
                w = "%s #%d вариант %d" % (tn, n.id, i + 1)
                out.append((o.text, w))
                out.append((o.after_line, w + " (после сдачи)"))
                _logic_texts(o.actions, w, out)
                _logic_texts(o.fail_actions, w, out)
        for b in npc.actor.barks:
            out.append((b.text, "%s: фраза %s" % (tn, b.id)))
    if npcs is not None:
        return out
    for s in project.bark_sets:
        for b in s.barks:
            out.append((b.text, "набор фраз %s: %s" % (s.id, b.id)))
    for kind, lst in (("перк", getattr(project, "perks", [])), ("черта", getattr(project, "traits", []))):
        for d in lst:
            out.append((d.name, "%s %s" % (kind, d.id)))
            out.append((d.desc, "%s %s: описание" % (kind, d.id)))
    for q in project.quests:
        out.append((q.title, "задание %s" % q.id))
        out.append((q.desc, "задание %s: описание" % q.id))
        _logic_texts(q.on_complete, "задание %s" % q.id, out)
        _logic_texts(q.on_fail, "задание %s" % q.id, out)
        for si, s in enumerate(q.stages):
            out.append((s.title, "задание %s этап %d" % (q.id, si)))
            out.append((s.desc, "задание %s этап %d: описание" % (q.id, si)))
            _logic_texts(s.actions, "задание %s этап %d" % (q.id, si), out)
            for o in s.objectives:
                out.append((o.text, "задание %s цель %s" % (q.id, o.id)))
    for e in project.events:
        _logic_texts(e.actions, "событие %s" % e.id, out)
    for it in project.items:
        for k in ("name", "desc", "text"):
            v = getattr(it, k, None)
            if isinstance(v, dict):
                out.append((v, "предмет %s: %s" % (it.id, k)))
    for f in project.factions:
        out.append((f.name, "фракция %s" % f.id))
    for t in getattr(project, "terminals", []):
        out.append((t.title, "терминал %s" % t.id))
        out.append((t.header, "терминал %s: приветствие" % t.id))
        for e in t.all_entries():
            w = "терминал %s, запись %s" % (t.id, e.key)
            out.append((e.title, w))
            out.append((e.text, w + ": текст"))
            out.append((e.result, w + ": ответ"))
            _logic_texts(e.actions, w, out)
    for mname, layer in project.maps.items():
        for r in layer.refs:
            if isinstance(getattr(r, "display_name", None), dict) and r.display_name:
                out.append((r.display_name, "%s #%d" % (mname, r.id)))
    return out


def plan(project, src, targets, mode, npcs=None):
    """[(texts, where, lang, source text)] to translate. mode: "empty" - only missing translations;
    "machine" - also earlier machine translations (manual ones are never touched)."""
    jobs = []
    seen = set()
    for texts, where in collect(project, npcs):
        source = (texts.get(src) or "").strip()
        if not source:
            continue
        for lang in targets:
            if lang == src:
                continue
            cur = texts.get(lang, "")
            if cur and not (mode == "machine" and is_mt(project, lang, cur)):
                continue
            key = (id(texts), lang)
            if key in seen:
                continue
            seen.add(key)
            jobs.append((texts, where, lang, source))
    return jobs


def run(jobs, src, progress=None, cancelled=None, workers=4):
    """Translate; returns ([(texts, lang, translation)], [errors]). progress(done, total)."""
    results = []
    errors = []
    cache = {}
    done = [0]

    def one(job):
        texts, where, lang, source = job
        if cancelled and cancelled():
            return None
        key = (lang, source)
        if key in cache:
            return texts, lang, cache[key]
        try:
            t = google(source, src, lang)
        except TranslateError as e:
            errors.append("%s → %s: %s" % (where, lang, e))
            return None
        cache[key] = t
        return texts, lang, t

    with ThreadPoolExecutor(max_workers=workers) as ex:
        for r in ex.map(one, jobs):
            done[0] += 1
            if progress:
                progress(done[0], len(jobs))
            if r is not None and r[2]:
                results.append(r)
    return results, errors


def apply(project, results):
    """Write the translations into the project (main thread) and mark them as machine-made."""
    from .model import clean_text
    marks = getattr(project, "mt_marks", None)
    if marks is None:
        marks = project.mt_marks = set()
    n = 0
    for texts, lang, t in results:
        t = clean_text(t)
        if not t:
            continue
        texts[lang] = t
        marks.add(mt_mark(lang, t))
        n += 1
    return n
