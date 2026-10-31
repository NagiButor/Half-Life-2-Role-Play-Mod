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
            out.append(Problem("info", q.owner, w, "квест «%s» не выдаётся ни одним вариантом ответа" % q.id))
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
            if r.type == "container" and not r.model:
                out.append(Problem("error", "", w, "%s: контейнер #%d без модели" % (m, r.id)))
            if r.name:
                key = (m, r.name.lower())
                if key in seen:
                    out.append(Problem("error", "", w, "%s: имя «%s» повторяется (маркеры/маршруты ищутся по имени)" % (m, r.name)))
                seen[key] = r
    return out
