"""Import game dialog files (scripts/dialogs/*.txt) into a project.

Both formats are read: the old one (one language per file, a translated copy as a
separate NPC such as demo_npc_1_rus with quests demo_quest_rus) and the editor's
own (one file, translations in "<key>_<language>" keys). Old language copies
with the same structure as their base file are merged into one NPC.
"""
import os
import re

from . import kv
from . import audio
from . import lipsync
from .model import NPC, Node, Option, Quest, Stage, Voice, Returning, clean_text, LANGUAGE_NAMES

LANG_SUFFIXES = [("_russian", "russian"), ("_rus", "russian"), ("_ru", "russian"),
                 ("_english", "english"), ("_eng", "english"), ("_en", "english"),
                 ("_ukrainian", "ukrainian"), ("_ua", "ukrainian"), ("_german", "german")]

_CYR = re.compile("[А-Яа-яЁё]")


def _int(s, default=-1):
    try:
        return int(str(s).strip())
    except (TypeError, ValueError):
        return default


def _true(s):
    if s is None:
        return False
    s = str(s).strip().lower()
    if s in ("1", "true", "yes", "on"):
        return True
    try:
        return int(s) != 0
    except ValueError:
        return False


class RawDialog:
    """One file as read: texts are dicts lang -> text, media dicts lang -> (choreo, sound)."""

    def __init__(self):
        self.path = ""
        self.entity = ""
        self.lang = ""            # language of the plain keys
        self.npc = None
        self.quests = []
        self.media = {}           # key -> {lang: (choreo, sound)}
        self.problems = []


def _loc(block, key, langs, base_lang):
    """Read key and key_<lang> -> {lang: value} (empty values dropped)."""
    out = {}
    v = block.get(key)
    if v:
        out[base_lang] = clean_text(v)
    for lg in langs:
        if lg == base_lang:
            continue
        lv = block.get(key + "_" + lg)
        if lv:
            out[lg] = clean_text(lv)
    return out


def _loc_media(block, key_choreo, key_sound, langs, base_lang):
    out = {}
    for lg in [base_lang] + [x for x in langs if x != base_lang]:
        suf = "" if lg == base_lang else "_" + lg
        if block.has(key_choreo + suf) or block.has(key_sound + suf):
            c = block.get(key_choreo + suf, "") or ""
            s = block.get(key_sound + suf, "") or ""
            if c or s:
                out[lg] = (c, s)
    return out


def detect_lang(dlg, entity, default="english"):
    for suf, lg in LANG_SUFFIXES:
        if entity.lower().endswith(suf):
            return lg
    nodes = dlg.block("nodes")
    sample = []
    if nodes:
        for n in nodes.blocks()[:6]:
            sample.append(n.get("line", ""))
    if _CYR.search(" ".join(sample)):
        return "russian"
    return default


def read_dialog(path, langs, default_lang="english"):
    raw = RawDialog()
    raw.path = path
    root = kv.parse_file(path)
    dlg = next((c for c in root.children if c.value is None), None)
    if dlg is None or dlg.name.lower() != "dialog":
        raw.problems.append('нет корневого блока "Dialog"')
        return None
    entity = dlg.get("entity") or os.path.splitext(os.path.basename(path))[0]
    raw.entity = entity
    base = detect_lang(dlg, entity, default_lang)
    raw.lang = base
    all_langs = list(dict.fromkeys(list(langs) + [lg for lg in LANGUAGE_NAMES]))

    npc = NPC(entity)
    npc.display_name = _loc(dlg, "display_name", all_langs, base)
    npc.start_node = _int(dlg.get("start_node", "0"), 0)
    npc.auto_face_npc = _true(dlg.get("auto_face_npc", "0"))
    r = Returning()
    r.line = _loc(dlg, "returning_line", all_langs, base)
    raw.media["returning"] = _loc_media(dlg, "returning_choreo", "returning_sound", all_langs, base)
    r.next = _int(dlg.get("returning_auto_next"), -1)
    r.no_options = _true(dlg.get("returning_no_options", "0"))
    r.auto_close = _true(dlg.get("returning_auto_close", "0"))
    npc.returning = r

    qb = dlg.block("quests")
    if qb:
        for qkv in qb.blocks():
            raw.quests.append(_read_quest(qkv, all_langs, base, entity))

    nb = dlg.block("nodes")
    if nb is None:
        raw.problems.append('нет блока "nodes"')
        return None
    for nkv in nb.blocks():
        if not re.fullmatch(r"-?\d+", nkv.name.strip()):
            raw.problems.append("узел %r: имя не число, игра его не видит — пропущен" % nkv.name)
            continue
        n = Node(int(nkv.name))
        n.note = nkv.comment
        n.line = _loc(nkv, "line", all_langs, base)
        raw.media[str(n.id)] = _loc_media(nkv, "choreo", "sound", all_langs, base)
        n.sequence = nkv.get("sequence", "") or ""
        n.server_sequence = nkv.get("server_sequence", "") or ""
        od = (nkv.get("option_delay", "") or "").strip()
        n.option_delay = od if od and od.lower() != "auto" else "auto"
        n.auto_next = _int(nkv.get("auto_next"), -1)
        n.end = _true(nkv.get("end")) or _true(nkv.get("close")) or _true(nkv.get("auto_close"))
        n.spoils = _true(nkv.get("spoils"))
        ob = nkv.block("options")
        if ob:
            slots = {}
            extra = []
            for okv in ob.blocks():
                o = _read_option(okv, all_langs, base)
                idx = _int(okv.name, -1)
                if idx >= 0 and idx not in slots:
                    slots[idx] = (o, okv)
                else:
                    extra.append((o, okv))
            ordered = [slots[k] for k in sorted(slots)] + extra
            for oi, (o, okv) in enumerate(ordered):
                n.options.append(o)
                raw.media["%d_%d_after" % (n.id, oi)] = _loc_media(okv, "after_choreo", "after_sound", all_langs, base)
        npc.nodes.append(n)
    raw.npc = npc
    return raw


def _split_targets(s):
    return [t.strip() for t in (s or "").split(";") if t.strip()]


def _read_quest(qkv, langs, base, owner):
    q = Quest(qkv.name)
    q.owner = owner
    q.note = qkv.comment
    q.title = _loc(qkv, "title", langs, base)
    q.desc = _loc(qkv, "desc", langs, base)
    q.min_rep = _int(qkv.get("min_rep", "0"), 0)
    q.fail_on_kill = _split_targets(qkv.get("fail_on_kill", "")) + _split_targets(qkv.get("fail_on_kill_targets", ""))
    q.fail_rep_penalty = _int(qkv.get("fail_rep_penalty", "0"), 0)
    sb = qkv.block("stages")
    stages = {}
    if sb:
        for skv in sb.blocks():
            i = _int(skv.name, -1)
            if i < 0:
                continue
            s = Stage()
            s.title = _loc(skv, "title", langs, base)
            s.desc = _loc(skv, "desc", langs, base)
            s.kill_targets = _split_targets(skv.get("kill_targets", "")) + _split_targets(skv.get("kill_target", ""))
            s.kill_count = _int(skv.get("kill_count", "0"), 0)
            stages[i] = s
    if stages:
        top = max(stages)
        q.stages = [stages.get(i, Stage()) for i in range(top + 1)]
    # quest-level kill = stage 0 kill in the game when the stage has none
    qt = _split_targets(qkv.get("kill_targets", "")) + _split_targets(qkv.get("kill_target", ""))
    qc = _int(qkv.get("kill_count", "0"), 0)
    if qc > 0:
        if not q.stages:
            q.stages.append(Stage())
        if q.stages[0].kill_count <= 0:
            q.stages[0].kill_targets = qt
            q.stages[0].kill_count = qc
    rb = qkv.block("rewards")
    if rb:
        seen = set()
        for c in rb.children:
            amt = _int(c.value, 0) if c.value is not None else 0
            if c.name and amt > 0 and c.name not in seen:
                seen.add(c.name)
                q.rewards.append([c.name, amt])
    return q


def _read_option(okv, langs, base):
    o = Option()
    o.note = okv.comment
    o.text = _loc(okv, "text", langs, base)
    o.next = _int(okv.get("next"), -1)
    o.close = _true(okv.get("close")) or (_int(okv.get("flags"), 0) & 1) == 1
    o.spoils = _true(okv.get("spoils"))
    o.server_sequence = okv.get("server_sequence", "") or ""
    o.grant = okv.get("grant_quest", "") or ""
    o.complete = okv.get("complete_quest", "") or ""
    o.fail = okv.get("fail_quest", "") or ""
    ss = okv.get("set_stage", "")
    if ss and ":" in ss:
        q, n = ss.split(":", 1)
        o.set_stage = [q.strip(), _int(n, 0)]
    rr = okv.get("require_rep")
    if rr not in (None, ""):
        o.require_rep = _int(rr, 0)
    rs = okv.get("require_quest_state", "")
    if rs and ":" in rs:
        q, st = rs.split(":", 1)
        st = st.strip().lower()
        st = "completed" if st == "done" else st
        o.require_state = [q.strip(), st]
    rg = okv.get("require_quest_stage", "")
    if rg and ":" in rg:
        q, n = rg.split(":", 1)
        o.require_stage = [q.strip(), _int(n, 0)]
    elif okv.get("require_quest"):
        o.require_stage = [okv.get("require_quest"), _int(okv.get("require_stage", "0"), 0)]
    o.after_next = _int(okv.get("after_next"), -1)
    o.after_line = _loc(okv, "after_line", langs, base)
    o.after_sequence = okv.get("after_sequence", "") or ""
    return o


# --- merging language copies ---------------------------------------------------

def _strip_suffix(name, suffix):
    return name[: -len(suffix)] if suffix and name.lower().endswith(suffix) else name


def _shape(npc, qmap):
    def m(q):
        return qmap.get(q, q)
    out = []
    for n in sorted(npc.nodes, key=lambda x: x.id):
        opts = []
        for o in n.options:
            opts.append((o.next, o.close, o.spoils, m(o.grant), m(o.complete), m(o.fail),
                         tuple(o.set_stage and [m(o.set_stage[0]), o.set_stage[1]] or []),
                         o.require_rep, tuple(o.require_state and [m(o.require_state[0]), o.require_state[1]] or []),
                         tuple(o.require_stage and [m(o.require_stage[0]), o.require_stage[1]] or []),
                         o.after_next, o.server_sequence))
        out.append((n.id, n.auto_next, n.end, n.spoils, n.server_sequence, tuple(opts)))
    return (npc.start_node, npc.returning.next, npc.returning.no_options, tuple(out))


def merge_into(base, other, lang, suffix, log):
    """Merge `other` (a translated copy) into `base` as language `lang`.
    Returns True when merged."""
    qmap = {}
    base_q = {q.id for q in base.quests}
    for q in other.quests:
        cand = _strip_suffix(q.id, suffix)
        if cand in base_q:
            qmap[q.id] = cand
        elif q.id in base_q:
            qmap[q.id] = q.id
    if _shape(base.npc, {}) != _shape(other.npc, qmap):
        log.append("%s: структура отличается от %s — импортирован отдельным NPC" % (other.entity, base.entity))
        return False
    bn = base.npc
    on = other.npc
    for k, v in on.display_name.items():
        bn.display_name.setdefault(lang, v)

    def take(dst, src):
        t = src.get(other.lang) or next(iter(src.values()), "")
        if t:
            dst[lang] = t
    take(bn.returning.line, on.returning.line)
    for n in on.nodes:
        b = bn.node(n.id)
        take(b.line, n.line)
        if not b.note and n.note:
            b.note = n.note
        for oi, o in enumerate(n.options):
            bo = b.options[oi]
            take(bo.text, o.text)
            take(bo.after_line, o.after_line)
            if not bo.note and o.note:
                bo.note = o.note
    for key, media in other.media.items():
        m = media.get(other.lang) or next(iter(media.values()), None)
        if m:
            base.media.setdefault(key, {})[lang] = m
    for q in other.quests:
        bq = next((x for x in base.quests if x.id == qmap.get(q.id)), None)
        if not bq:
            continue
        take(bq.title, q.title)
        take(bq.desc, q.desc)
        for si, s in enumerate(q.stages):
            if si < len(bq.stages):
                take(bq.stages[si].title, s.title)
                take(bq.stages[si].desc, s.desc)
    log.append("%s → %s: объединён как язык «%s»%s" % (other.entity, base.entity, LANGUAGE_NAMES.get(lang, lang),
               (", квесты " + ", ".join("%s→%s" % kvp for kvp in qmap.items() if kvp[0] != kvp[1])) if any(a != b for a, b in qmap.items()) else ""))
    return True


# --- media -----------------------------------------------------------------------

def _resolve_voice(project, choreo, sound, text):
    v = Voice()
    if choreo:
        rel = choreo.replace("\\", "/")
        if not rel.lower().endswith(".vcd"):
            rel += ".vcd"
        vcd_abs = project.find_content(rel)
        if vcd_abs:
            v.vcd = project.rel_to_game(vcd_abs)
            snd = lipsync.vcd_sound(lipsync.read_text(vcd_abs))
            if snd.lower().endswith(".wav"):
                wav_abs = project.find_content("sound/" + snd.replace("\\", "/"))
                if wav_abs:
                    v.wav = project.rel_to_game(wav_abs)
                    info = audio.wav_info(wav_abs)
                    v.duration = info.duration if info.ok else 0.0
                    # the scene was made by the mod's generator from this text and this audio
                    v.lips_text = text
                    v.lips_audio = audio.audio_hash(wav_abs)
            if not v.wav:
                v.vcd_kind = "custom"
    if not v.wav and sound:
        wav_abs = project.find_content("sound/" + sound.replace("\\", "/"))
        if wav_abs:
            v.wav = project.rel_to_game(wav_abs)
            info = audio.wav_info(wav_abs)
            v.duration = info.duration if info.ok else 0.0
    return v


def _attach_media(project, raw):
    npc = raw.npc
    targets = {"returning": (npc.returning.line, npc.returning.voice)}
    for n in npc.nodes:
        targets[str(n.id)] = (n.line, n.voice)
        for oi, o in enumerate(n.options):
            targets["%d_%d_after" % (n.id, oi)] = (o.after_line, o.after_voice)
    missing = []
    for key, media in raw.media.items():
        if key not in targets:
            continue
        texts, voices = targets[key]
        for lg, (choreo, sound) in media.items():
            v = _resolve_voice(project, choreo, sound, texts.get(lg, ""))
            if not v.is_empty():
                voices[lg] = v
            else:
                missing.append("%s/%s %s" % (key, lg, choreo or sound))
    return missing


def import_files(project, paths, log=None, merge=True):
    """Read dialog files into the project. Returns list of NPCs added/replaced."""
    log = log if log is not None else []
    langs = project.languages
    raws = []
    for p in paths:
        try:
            r = read_dialog(p, langs, project.base_language)
        except Exception as e:
            log.append("%s: ошибка чтения: %s" % (os.path.basename(p), e))
            continue
        if r is None:
            log.append("%s: не диалог, пропущен" % os.path.basename(p))
            continue
        for pr in r.problems:
            log.append("%s: %s" % (os.path.basename(p), pr))
        raws.append(r)

    by_entity = {r.entity.lower(): r for r in raws}
    merged_away = set()
    if merge:
        for r in raws:
            for suf, lg in LANG_SUFFIXES:
                if r.entity.lower().endswith(suf):
                    base = by_entity.get(r.entity.lower()[: -len(suf)])
                    if base is not None and base is not r and base.lang != r.lang:
                        if merge_into(base, r, r.lang, suf, log):
                            merged_away.add(r.entity.lower())
                    break

    added = []
    for r in raws:
        if r.entity.lower() in merged_away:
            continue
        missing = _attach_media(project, r)
        for m in missing:
            log.append("%s: не найдена озвучка/сцена %s" % (r.entity, m))
        # the project keeps texts in its languages; a file in a language that is not
        # the project's base keeps its texts under that language
        if r.lang not in project.languages:
            project.settings.languages.append(r.lang)
        old = project.npc(r.entity)
        if old:
            project.npcs[project.npcs.index(old)] = r.npc
        else:
            project.npcs.append(r.npc)
        for q in r.quests:
            oq = project.quest(q.id)
            if oq:
                project.quests[project.quests.index(oq)] = q
            else:
                project.quests.append(q)
        added.append(r.npc)
        nv = sum(len(n.voice) for n in r.npc.nodes) + len(r.npc.returning.voice)
        log.append("%s: %d реплик, %d квестов, озвучек %d, языки: %s" % (
            r.entity, len(r.npc.nodes), len(r.quests), nv,
            ", ".join(sorted({lg for n in r.npc.nodes for lg in n.line}))))
    return added
