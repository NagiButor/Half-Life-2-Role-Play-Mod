"""Checks of a project: everything the game would choke on or a player would
notice (broken links, missing translations/voice, unreachable lines...).

Each problem: (severity, npc targetname or "", where, message) where
`where` is ("npc",) / ("node", id) / ("option", id, index) / ("returning",) /
("quest", id) / ("package", index) / ("ref", map, id) and severity is
"error" | "warning" | "info".
"""
import glob
import os

from . import audio
from .model import BAD_CHARS, LANGUAGE_NAMES, QUEST_STATES, TARGETNAME_RE, iter_voiced_texts
from .layout import successors


class Problem:
    __slots__ = ("severity", "npc", "where", "message")

    def __init__(self, severity, npc, where, message):
        self.severity = severity
        self.npc = npc
        self.where = where
        self.message = message


def _lname(lang):
    return LANGUAGE_NAMES.get(lang, lang)


def validate(project, mapindex=None, check_files=True):
    out = []
    p = project
    langs = p.languages
    base = p.base_language
    quest_ids = set(p.quest_ids())

    def add(sev, npc, where, msg):
        out.append(Problem(sev, npc.targetname if npc else "", where, msg))

    seen = {}
    for npc in p.npcs:
        t = npc.targetname
        if not t:
            add("error", npc, ("npc",), "пустой targetname — диалог невозможен")
        elif not TARGETNAME_RE.match(t):
            add("error", npc, ("npc",), "targetname «%s»: только латиница, цифры, _ - ." % t)
        if t.lower() in seen:
            add("error", npc, ("npc",), "targetname «%s» повторяется — в игре останется один из диалогов" % t)
        seen[t.lower()] = npc
        for lg in langs:
            if not npc.display_name.get(lg):
                add("warning", npc, ("npc",), "нет имени NPC (%s) — в окне диалога и лута будет имя из Hammer" % _lname(lg))
        for lg, s in npc.display_name.items():
            if any(c in s for c in BAD_CHARS):
                add("error", npc, ("npc",), "имя (%s) содержит | ; ~ — это разделители передачи диалога" % _lname(lg))
        validate_actor(p, npc, add)
        if not npc.has_dialog():
            # an NPC of the world without lines: no dialog file, nothing to check
            continue
        if not npc.nodes:
            add("error", npc, ("npc",), "нет ни одной реплики")
            continue
        ids = set(npc.node_ids())
        if npc.start_node not in ids:
            add("error", npc, ("npc",), "стартовая реплика #%d не существует" % npc.start_node)

        # map presence
        if mapindex is not None and mapindex.maps:
            if not mapindex.find(t) and not p.placements(t):
                add("info", npc, ("npc",), "NPC «%s» не найден ни на одной карте из папок карт" % t)

        # reachability
        reach = set()
        stack = [npc.start_node]
        r = npc.returning
        if r.enabled():
            stack.append(npc.returning_target())
            if r.next != -1:
                stack.append(r.next)
        while stack:
            u = stack.pop()
            if u in reach or u not in ids:
                continue
            reach.add(u)
            stack.extend(successors(npc.node(u)))

        if r.enabled():
            if r.next != -1 and r.next not in ids:
                add("error", npc, ("returning",), "повторный визит ведёт на несуществующую реплику #%d" % r.next)
            for lg in langs:
                if not r.line.get(lg):
                    add("warning", npc, ("returning",), "повторная реплика без перевода (%s)" % _lname(lg))

        for n in npc.nodes:
            w = ("node", n.id)
            if n.id not in reach:
                add("warning", npc, w, "реплика #%d недостижима (на неё ничего не ведёт)" % n.id)
            for lg in langs:
                if not n.line.get(lg):
                    add("warning" if n.line else "error", npc, w, "#%d: нет текста (%s)" % (n.id, _lname(lg)))
            if n.auto_next != -1 and n.auto_next not in ids:
                add("error", npc, w, "#%d: «далее» ведёт на несуществующую #%d" % (n.id, n.auto_next))
            if n.options and n.auto_next != -1:
                add("info", npc, w, "#%d: есть варианты ответа — «далее» (#%d) игра не использует" % (n.id, n.auto_next))
            if not n.options and n.auto_next == -1 and not n.end and not n.spoils:
                add("warning", npc, w, "#%d: нет ни вариантов, ни «далее», ни «конец» — игроку останется только выйти" % n.id)
            if n.option_delay != "auto":
                try:
                    float(n.option_delay)
                except ValueError:
                    add("error", npc, w, "#%d: задержка вариантов «%s» — не число" % (n.id, n.option_delay))
            for oi, o in enumerate(n.options):
                ow = ("option", n.id, oi)
                label = "#%d вариант %d" % (n.id, oi + 1)
                for lg in langs:
                    tx = o.text.get(lg, "")
                    if not tx:
                        add("warning" if o.text else "error", npc, ow, "%s: нет текста (%s)" % (label, _lname(lg)))
                    elif len(tx.encode("utf-8")) > 255:
                        add("error", npc, ow, "%s (%s): %d байт, игра обрежет до 255" % (label, _lname(lg), len(tx.encode("utf-8"))))
                if o.next != -1 and o.next not in ids:
                    add("error", npc, ow, "%s ведёт на несуществующую #%d" % (label, o.next))
                if not o.leads_somewhere():
                    add("warning", npc, ow, "%s: никуда не ведёт и не закрывает диалог" % label)
                if o.next != -1 and o.close:
                    add("info", npc, ow, "%s: «закрыть» не действует, пока задана следующая реплика" % label)
                if o.complete and o.next != -1 and (o.after_next != -1 or any(o.after_line.values())):
                    add("warning", npc, ow, "%s: «после завершения» игра использует только без следующей реплики" % label)
                if o.has_after() and o.after_next != -1 and o.after_next not in ids:
                    add("error", npc, ow, "%s: после завершения квеста — несуществующая #%d" % (label, o.after_next))
                for q in o.quest_refs():
                    if q not in quest_ids:
                        add("error", npc, ow, "%s: квест «%s» не описан" % (label, q))
                for ref in (o.set_stage, o.require_stage):
                    if ref and ref[0] in quest_ids:
                        qd = p.quest(ref[0])
                        if ref[1] >= max(1, len(qd.stages)):
                            add("warning", npc, ow, "%s: у квеста «%s» нет этапа %d" % (label, ref[0], ref[1]))
                if o.require_state and o.require_state[1] not in QUEST_STATES:
                    add("error", npc, ow, "%s: состояние «%s»" % (label, o.require_state[1]))
                if o.grant and o.require_state and o.require_state[0] == o.grant and o.require_state[1] != "inactive":
                    add("warning", npc, ow, "%s: выдаёт квест, который уже должен быть «%s»" % (label, o.require_state[1]))

        # characters / voice / lip sync
        for key, texts, voices, where in iter_voiced_texts(npc):
            for lg, tx in texts.items():
                if any(c in tx for c in BAD_CHARS):
                    add("error", npc, where, "%s (%s): символы | ; ~ ломают передачу диалога" % (key, _lname(lg)))
            if check_files:
                any_voice = any(not v.is_empty() for v in voices.values())
                for lg in langs:
                    v = voices.get(lg)
                    if not v or v.is_empty():
                        if any_voice and texts.get(lg):
                            add("warning", npc, where, "%s: нет озвучки (%s), есть на другом языке" % (key, _lname(lg)))
                        continue
                    if v.wav:
                        path = p.game_path(v.wav)
                        if not os.path.exists(path):
                            add("error", npc, where, "%s (%s): нет файла озвучки %s" % (key, _lname(lg), v.wav))
                            continue
                        if path.lower().endswith(".wav"):
                            info = audio.wav_info(path)
                            if not info.ok:
                                add("error", npc, where, "%s (%s): файл не читается: %s" % (key, _lname(lg), info.error))
                            elif not info.source_ready():
                                add("info", npc, where, "%s (%s): %s — при сохранении будет сконвертирован" % (key, _lname(lg), info.describe()))
                        if v.vcd_kind == "auto" and texts.get(lg) and (v.lips_text != texts.get(lg) or not v.vcd):
                            add("info", npc, where, "%s (%s): липсинк будет пересоздан при сохранении" % (key, _lname(lg)))
                    if v.vcd_kind == "custom" and v.vcd and not os.path.exists(p.game_path(v.vcd)):
                        add("error", npc, where, "%s (%s): нет файла сцены %s" % (key, _lname(lg), v.vcd))

    # quests
    used = set()
    for npc in p.npcs:
        for n in npc.nodes:
            for o in n.options:
                used.update(o.quest_refs())
    from . import logic
    for _w, kind, lst in logic.iter_logic(p):
        for x in lst or []:
            if x.get("type") == "quest_start" and x.get("quest"):
                used.add(x["quest"])
    qseen = set()
    kill_names = set(mapindex.targetnames()) if (mapindex is not None and mapindex.maps) else None
    for q in p.quests:
        w = ("quest", q.id)
        if q.id in qseen:
            out.append(Problem("error", q.owner, w, "квест «%s» описан дважды" % q.id))
        qseen.add(q.id)
        if any(c in q.id for c in ' "|;~:'):
            out.append(Problem("error", q.owner, w, "id квеста «%s»: без пробелов и : ; | ~" % q.id))
        if q.id not in used:
            out.append(Problem("info", q.owner, w, "задание «%s» никто не выдаёт (ответ в диалоге, действие «Задание: выдать»)" % q.id))
        oids = set()
        for si, s in enumerate(q.stages):
            for o in s.objectives:
                if o.id in oids:
                    out.append(Problem("error", "", w, "задание «%s»: id цели «%s» повторяется" % (q.id, o.id)))
                oids.add(o.id)
                if o.type in ("kill", "talk", "go", "use") and not o.target:
                    out.append(Problem("error", "", w, "задание «%s» этап %d: цель %s — не указано, кого / куда" % (q.id, si, o.id)))
                if o.type == "collect" and not o.item:
                    out.append(Problem("error", "", w, "задание «%s» этап %d: цель %s — не указан предмет" % (q.id, si, o.id)))
                if o.type == "cond" and not o.conditions:
                    out.append(Problem("warning", "", w, "задание «%s» этап %d: цель %s — нет условий (выполнится сразу)" % (q.id, si, o.id)))
                for lg in langs:
                    if not o.text.get(lg):
                        out.append(Problem("info", "", w, "задание «%s» цель %s: нет текста (%s)" % (q.id, o.id, _lname(lg))))
                        break
            if s.next >= len(q.stages):
                out.append(Problem("error", "", w, "задание «%s» этап %d: «дальше» — этап %d, а его нет" % (q.id, si, s.next)))
        if not q.stages:
            out.append(Problem("warning", q.owner, w, "квест «%s» без этапов" % q.id))
        for lg in langs:
            if not q.title.get(lg):
                out.append(Problem("warning", q.owner, w, "квест «%s»: нет названия (%s)" % (q.id, _lname(lg))))
        for si, s in enumerate(q.stages):
            if s.kill_targets and s.kill_count <= 0:
                out.append(Problem("warning", q.owner, w, "квест «%s» этап %d: цели есть, а сколько убить — 0" % (q.id, si)))
            if s.kill_count > len(s.kill_targets) and s.kill_targets:
                out.append(Problem("warning", q.owner, w, "квест «%s» этап %d: убить %d, а целей %d" % (q.id, si, s.kill_count, len(s.kill_targets))))
            if kill_names is not None:
                for kt in s.kill_targets:
                    if kt not in kill_names and not kt.endswith("*"):
                        out.append(Problem("info", q.owner, w, "квест «%s» этап %d: цель «%s» не найдена на картах" % (q.id, si, kt)))
        for item, amount in q.rewards:
            if not item or int(amount) <= 0:
                out.append(Problem("warning", q.owner, w, "квест «%s»: пустая награда" % q.id))

    out += validate_world(p)
    out += validate_logic(p, mapindex)
    out += validate_sound(p)
    out += validate_rpg(p)
    out += validate_terminals(p)

    # other dialog files of the game that define the same NPCs
    if check_files:
        mine = {n.targetname.lower() for n in p.npcs}
        roots = {os.path.normcase(os.path.abspath(r)) for r in (p.settings.game_dir, p.settings.mod_dir,
                 os.path.join(p.settings.mod_dir, "testmaps")) if r and os.path.isdir(r)}
        own_dir = os.path.normcase(os.path.abspath(os.path.join(p.settings.game_dir, "scripts", "dialogs")))
        for r in roots:
            ddir = os.path.join(r, "scripts", "dialogs")
            if os.path.normcase(os.path.abspath(ddir)) == own_dir:
                continue
            for f in glob.glob(os.path.join(ddir, "*.txt")):
                name = os.path.splitext(os.path.basename(f))[0].lower()
                if name in mine:
                    out.append(Problem("warning", name, ("npc",), "в игре есть ещё файл %s с этим NPC — загрузится какой-то один" % f))
    order = {"error": 0, "warning": 1, "info": 2}
    out.sort(key=lambda x: (order[x.severity], x.npc))
    return out


def validate_logic(p, mapindex=None):
    """References of conditions / actions / events (Creation Kit logic)."""
    from . import logic
    out = []
    quests = {q.id: q for q in p.quests}
    zones = {r.name for layer in p.maps.values() for r in layer.refs if r.type == "zone" and r.name}
    npcs = {n.targetname.lower() for n in p.npcs}
    names = {r.name.lower() for layer in p.maps.values() for r in layer.refs if r.name} | npcs
    if mapindex is not None and getattr(mapindex, "maps", None):
        names |= {t.lower() for t in mapindex.targetnames()}
    items = {it.id.lower() for it in p.items}
    events = {e.id for e in p.events}
    for where, kind, lst in logic.iter_logic(p):
        for x in lst or []:
            t = x.get("type", "")
            known = (logic.ACTION_BY_ID if kind == "action" else logic.COND_BY_ID)
            if t not in known:
                out.append(Problem("error", "", ("logic",), "%s: неизвестное %s «%s»" % (where, "действие" if kind == "action" else "условие", t)))
                continue
            qid = x.get("quest")
            if qid and qid not in quests:
                out.append(Problem("error", "", ("logic",), "%s: задания «%s» нет в проекте" % (where, qid)))
            elif qid and x.get("obj") and not any(o.id == x["obj"] for o in quests[qid].all_objectives()):
                out.append(Problem("error", "", ("logic",), "%s: в задании «%s» нет цели «%s»" % (where, qid, x["obj"])))
            if t in ("quest_state", "quest_stage", "objective", "quest_start", "quest_complete", "quest_fail") and not qid:
                out.append(Problem("error", "", ("logic",), "%s: не выбрано задание" % where))
            if x.get("item") and items and x["item"].lower() not in items and not x["item"].startswith(("weapon_", "item_", "rpm_")):
                out.append(Problem("warning", "", ("logic",), "%s: предмета «%s» нет в базе предметов" % (where, x["item"])))
            if t == "zone" and x.get("name") and x["name"] not in zones:
                out.append(Problem("warning", "", ("logic",), "%s: зоны «%s» нет ни на одной карте" % (where, x["name"])))
            if t in ("flag", "var") and not x.get("name"):
                out.append(Problem("error", "", ("logic",), "%s: не указано имя %s" % (where, "флага" if t == "flag" else "переменной")))
            if t == "event" and x.get("name") and x["name"] not in events:
                out.append(Problem("warning", "", ("logic",), "%s: события «%s» нет во вкладке «Логика»" % (where, x["name"])))
            tgt = x.get("target")
            if tgt and t not in ("fire",) and "*" not in tgt and not tgt.startswith("!") and tgt.lower() not in names and tgt not in zones:
                out.append(Problem("info", "", ("logic",), "%s: «%s» не найден среди NPC, объектов и зон" % (where, tgt)))
    for e in p.events:
        w = ("event", e.id)
        if not e.actions:
            out.append(Problem("warning", "", w, "событие «%s»: нет действий" % e.id))
        if e.on in ("zone_enter", "zone_leave") and e.target and e.target not in zones:
            out.append(Problem("warning", "", w, "событие «%s»: зоны «%s» нет ни на одной карте" % (e.id, e.target)))
        if e.on.startswith("quest_") and e.target and e.target not in quests:
            out.append(Problem("error", "", w, "событие «%s»: задания «%s» нет в проекте" % (e.id, e.target)))
        if e.on == "when" and not e.conditions:
            out.append(Problem("warning", "", w, "событие «%s» «когда условия верны» без условий — сработает сразу" % e.id))
    seen = set()
    for g in p.globals:
        if g.name in seen:
            out.append(Problem("error", "", ("globals",), "флаг/переменная «%s» описан дважды" % g.name))
        seen.add(g.name)
    return out


def validate_rpg(p):
    """Perks, traits, the new game ("RPG" tab)."""
    from . import logic, rpgchar
    out = []
    known = set(rpgchar.STAT_IDS) | set(rpgchar.SKILL_IDS)
    for kind, lst in (("perk", p.perks), ("trait", p.traits)):
        seen = set()
        for d in lst:
            w = (kind, d.id)
            if d.id in seen:
                out.append(Problem("error", "", w, "%s «%s» описан дважды" % ("перк" if kind == "perk" else "черта", d.id)))
            seen.add(d.id)
            if not any(d.name.values()):
                out.append(Problem("warning", "", w, "«%s»: нет названия" % d.id))
            for cid in getattr(d, "req", {}) or {}:
                if cid not in known:
                    out.append(Problem("error", "", w, "«%s»: требование «%s» — нет такой характеристики или навыка" % (d.id, cid)))
            for e in d.effects:
                if e.get("type") not in rpgchar.EFFECT_NAMES:
                    out.append(Problem("error", "", w, "«%s»: неизвестный эффект «%s»" % (d.id, e.get("type"))))
                elif e.get("type") in ("stat", "skill") and e.get("id") not in known:
                    out.append(Problem("error", "", w, "«%s»: эффект без характеристики / навыка" % d.id))
            if not d.effects:
                out.append(Problem("info", "", w, "«%s»: без эффектов (только для условий «У игрока перк»)" % d.id))
    perks = {x.id for x in p.perks}
    for where, kind, items in logic.iter_logic(p):
        for x in items or []:
            if x.get("type") == "perk" and x.get("name") and x["name"] not in perks:
                out.append(Problem("error", "", ("logic",), "%s: перка «%s» нет (вкладка «РПГ»)" % (where, x["name"])))
    ng = p.newgame
    if ng:
        if ng.get("map") and p.maps and ng["map"] not in p.maps:
            out.append(Problem("info", "", ("newgame",), "новая игра: карта «%s» — на ней ничего не поставлено из программы" % ng["map"]))
        if ng.get("quest") and not p.quest(ng["quest"]):
            out.append(Problem("error", "", ("newgame",), "новая игра: задания «%s» нет" % ng["quest"]))
    if (p.perks or p.traits) and not ng:
        out.append(Problem("info", "", ("newgame",), "новая игра не настроена: окно создания персонажа само не откроется "
                                                    "(вкладка «РПГ» → «Новая игра» или действие «Окно создания персонажа»)"))
    return out


def validate_terminals(p):
    """Terminals (M6.5): unknown password items, empty menus, map objects without a terminal."""
    from . import termdata
    out = []
    found = []
    termdata.check(p, found)
    for text, target in found:
        out.append(Problem("warning", "", target or ("terminal",), text))
    seen = set()
    for t in getattr(p, "terminals", []):
        if t.id in seen:
            out.append(Problem("error", "", ("terminal", t.id), "терминал «%s» описан дважды" % t.id))
        seen.add(t.id)
    for name, layer in p.maps.items():
        for r in layer.refs:
            if r.type == "terminal" and not r.terminal:
                out.append(Problem("warning", "", ("ref", name, r.id), "%s: терминал без содержимого (поле «Терминал»)" % name))
    return out


def validate_sound(p):
    """Music tracks, soundscapes, barks ("Sound" tab, NPC "Barks" tab, locations)."""
    from . import logic
    out = []
    tracks = {t.id: t for t in p.music}
    scapes = {s.name.lower() for s in p.soundscapes}
    sets = {s.id for s in p.bark_sets}
    seen = set()
    for t in p.music:
        w = ("track", t.id)
        if t.id in seen:
            out.append(Problem("error", "", w, "трек «%s» описан дважды" % t.id))
        seen.add(t.id)
        if not t.file:
            out.append(Problem("error", "", w, "трек «%s»: не выбран файл" % t.id))
        elif not t.file.lower().endswith((".mp3", ".wav")):
            out.append(Problem("error", "", w, "трек «%s»: игра играет только MP3 и WAV (%s)" % (t.id, t.file)))
    for name, layer in sorted(p.maps.items()):
        for r in layer.refs:
            w = ("ref", name, r.id)
            if r.type == "zone":
                for tid in r.music:
                    if tid not in tracks:
                        out.append(Problem("error", "", w, "%s: локация «%s» — трека «%s» нет" % (name, r.name or r.id, tid)))
                for sc_ in (r.soundscape, r.soundscape_night, r.soundscape_rain):
                    if sc_ and sc_.lower() not in scapes and not sc_.lower().startswith(("streets.", "d1_", "d2_", "d3_", "ep1_", "ep2_", "e3_", "coast.", "prison.", "canals.", "trainstation.")):
                        out.append(Problem("warning", "", w, "%s: локация «%s» — звуковой сцены «%s» нет в проекте" % (name, r.name or r.id, sc_)))
                if (r.soundscape_night or r.soundscape_rain) and not r.soundscape:
                    out.append(Problem("warning", "", w, "%s: локация «%s» — сцены «ночью / в дождь» работают только вместе с основной" % (name, r.name or r.id)))
                if (r.music or r.soundscape or r.discover_xp) and not r.name:
                    out.append(Problem("warning", "", w, "%s: у зоны с музыкой / сценой / опытом нет имени" % name))
                if r.discover_xp and not any(r.display_name.values()):
                    out.append(Problem("warning", "", w, "%s: опыт за открытие без названия локации — не начислится" % name))
            if r.type == "sound" and not r.sound:
                out.append(Problem("error", "", w, "%s: звук на карте без файла" % name))
    for where, kind, lst in logic.iter_logic(p):
        if kind != "action":
            continue
        for a in lst or []:
            if a.get("type") in ("music", "stinger"):
                for tid in [x for x in str(a.get("value", "")).split(";") if x]:
                    if tid not in tracks:
                        out.append(Problem("error", "", ("logic",), "%s: трека «%s» нет (вкладка «Звук»)" % (where, tid)))
                if a.get("type") == "stinger" and not a.get("value"):
                    out.append(Problem("error", "", ("logic",), "%s: стингер без трека" % where))

    def barks(owner, lst, npc=None):
        npc = npc.targetname if npc is not None else ""
        ids = set()
        for b in lst:
            w = ("barks",) if npc else ("barkset", owner)
            if b.id in ids:
                out.append(Problem("error", npc, w, "%s: фраза «%s» дважды" % (owner, b.id)))
            ids.add(b.id)
            has_voice = any(v and not v.is_empty() for v in b.voices.values()) or b.sound
            if not any(b.text.values()) and not has_voice:
                out.append(Problem("warning", npc, w, "%s: фраза «%s» без текста и звука" % (owner, b.id)))
    for n in p.npcs:
        barks(n.targetname, n.actor.barks, n)
        for sid in n.actor.bark_sets:
            if sid != "-" and sid not in sets:
                out.append(Problem("error", n.targetname, ("barks",), "набора фраз «%s» нет (вкладка «Звук»)" % sid))
    for s in p.bark_sets:
        barks(s.id, s.barks)
        if not s.barks:
            out.append(Problem("info", "", ("barkset", s.id), "набор фраз «%s» пуст" % s.id))
    return out


def validate_actor(p, npc, add):
    """AI packages and placements of an NPC (world part of the Creation Kit)."""
    from .worlddata import NPC_HAS_ROUTINE, PACKAGE_NAMES
    a = npc.actor
    places = p.placements(npc.targetname)
    if a.packages and not NPC_HAS_ROUTINE.get(a.classname, True):
        add("warning", npc, ("npc",), "у класса %s нет распорядка в игре — пакеты не будут работать" % a.classname)
    if a.faction and not p.faction(a.faction):
        add("info", npc, ("npc",), "фракция «%s» не описана (отношения по классу NPC)" % a.faction)
    maps = [m for m, _ in places]
    names = set()
    paths = set()
    for m in maps or list(p.maps):
        layer = p.layer(m)
        if layer:
            names.update(r.name for r in layer.refs if r.name)
            paths.update(layer.path_names())
    for i, pk in enumerate(a.packages):
        w = ("package", i)
        label = "пакет %d «%s»" % (i + 1, pk.name)
        if pk.type == "patrol":
            if not pk.path:
                add("error", npc, w, "%s: не выбран маршрут" % label)
            elif pk.path not in paths:
                add("warning", npc, w, "%s: маршрута «%s» нет на картах NPC" % (label, pk.path))
        if (pk.location == "ref" or pk.type == "use_marker") and pk.ref and pk.ref not in names:
            add("warning", npc, w, "%s: «%s» нет на картах NPC (в игре — только если стоит в Hammer)" % (label, pk.ref))
        if pk.location == "ref" and not pk.ref and pk.type != "use_marker":
            add("error", npc, w, "%s: место «маркер или объект», но имя не выбрано" % label)
        if pk.location == "point" and not any(pk.point):
            add("warning", npc, w, "%s: точка 0 0 0 — укажите её на карте" % label)
        if pk.type in ("sleep", "eat") and maps:
            kind = "sleep" if pk.type == "sleep" else "eat"
            have = any(r.type == "marker" and r.kind in (kind, "sit") and (not r.owner or r.owner.lower() == npc.targetname.lower())
                       for m in maps for r in p.layer(m).refs)
            if not have:
                add("info", npc, w, "%s: на картах NPC нет маркера «%s» — %s" % (
                    label, "кровать" if kind == "sleep" else "стол", "уснёт на земле" if kind == "sleep" else "будет стоять"))
        if not pk.always() and pk.duration <= 0:
            add("error", npc, w, "%s: пустое время" % label)
    if a.packages:
        covered = sum(1 for k in range(96) if a.package_at((k + 0.5) / 4.0))
        if covered < 96:
            add("info", npc, ("npc",), "распорядок покрывает %d из 24 ч — в остальное время NPC стоит как обычно" % (covered // 4))
        if not places and not npc.nodes:
            add("info", npc, ("npc",), "есть распорядок, но NPC не поставлен на карту из программы")


def validate_world(p):
    """Placed objects: markers, owners, names, NPC references."""
    out = []
    from .worlddata import MARKER_NAMES
    seen = {}
    for m, layer in sorted(p.maps.items()):
        for r in layer.refs:
            w = ("ref", m, r.id)
            if r.type == "npc" and not p.npc(r.npc):
                out.append(Problem("error", "", w, "%s: объект NPC «%s» — такого NPC нет в проекте" % (m, r.npc)))
            if r.type == "marker":
                if r.owner and not p.npc(r.owner):
                    out.append(Problem("warning", "", w, "%s: маркер «%s» — владелец «%s» не найден" % (m, r.name or r.id, r.owner)))
            if r.type == "path" and len(r.points) < 2:
                out.append(Problem("warning", "", w, "%s: маршрут «%s» — меньше двух точек" % (m, r.name)))
            if r.type == "transition":
                pair = p.layer(r.target_map).ref_by_name(r.target) if r.target_map and p.layer(r.target_map) else None
                if not r.target_map or not r.target:
                    out.append(Problem("error", "", w, "%s: переход «%s» никуда не ведёт (карта и парный переход)" % (m, r.name)))
                elif pair is None:
                    out.append(Problem("error", "", w, "%s: переход «%s» — на карте %s нет перехода «%s»" % (m, r.name, r.target_map, r.target)))
                elif pair.target != r.name or pair.target_map != m:
                    out.append(Problem("warning", "", w, "%s: переход «%s» → %s/%s, а тот ведёт на %s/%s" %
                                       (m, r.name, r.target_map, r.target, pair.target_map, pair.target)))
                if len(m) > 31 or len(r.target_map) > 31:
                    out.append(Problem("error", "", w, "%s: имя карты длиннее 31 символа — сохранение при переходе не загрузится" % m))
            if r.type == "container" and not r.model:
                out.append(Problem("error", "", w, "%s: контейнер #%d без модели" % (m, r.id)))
            if r.name:
                key = (m, r.name.lower())
                if key in seen:
                    out.append(Problem("error", "", w, "%s: имя «%s» повторяется (маркеры/маршруты ищутся по имени)" % (m, r.name)))
                seen[key] = r
    return out
