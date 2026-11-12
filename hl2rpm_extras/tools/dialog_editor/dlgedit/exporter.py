"""Write a project into the game folders.

    scripts/dialogs/<npc>.txt                    dialog, all languages
    scripts/rpg/quests.txt, events.txt, globals.txt   quests, events of the "Logic" tab, world flags/variables
    scripts/rpg/music.txt, barks.txt             music tracks, shared bark sets ("Sound" tab)
    scripts/soundscapes_hl2rpm.txt               soundscapes of locations
    scripts/rpg/rpg.txt, newgame.txt             character rules, perks, traits ("RPG" tab), the new game
    sound/dialogs/<npc or barks_<set>>/<lang>/bark_<id>.wav   voices of barks
    sound/dialogs/<npc>/<lang>/<key>.wav         voice (converted for Source if needed)
    scenes/dialogs/<npc>/<lang>/<key>.vcd        scene with lip sync
    scripts/npcs/<npc>.txt                       actor and AI packages (world_export.py)
    scripts/world/<map>.txt, <map>_nav.txt       what is placed on a map, AI nodes
    scripts/world/factions.txt                   faction relations

Plain keys carry the base language, "<key>_<lang>" the others; the game picks by
its language (dialog_language / cl_language). Only files the editor wrote
itself (project.managed_files) are ever deleted.
"""
import copy
import datetime
import os
import shutil

from . import audio
from . import rpg_export
from . import rpgchar
from . import kv
from . import lipsync
from . import logic
from . import world_export
from .model import canonical_vcd, canonical_wav, media_key_after, media_key_node
from .sounddata import bark_media_key


class _Owner:
    """Who voice files belong to (an NPC or a shared bark set: barks_<set>)."""

    def __init__(self, targetname):
        self.targetname = targetname

HEADER = ("HL2RPM Creation Kit: generated from %s\n"
          "Edit this in the Creation Kit - manual changes are overwritten on save.")
HEADER_MARK = "HL2RPM Creation Kit: generated"
# files of the earlier name of the program are ours too
HEADER_MARKS = (HEADER_MARK, "HL2RPM Dialog Editor: generated")


def is_ours(data):
    head = data[:400]
    return any(m.encode() in head for m in HEADER_MARKS)


class ExportReport:
    def __init__(self):
        self.written = []
        self.unchanged = 0
        self.converted = []
        self.lipsync = []
        self.copied = []
        self.deleted = []
        self.backups = []
        self.errors = []
        self.warnings = []
        self.world = []           # NPC / map / faction files written

    def summary(self):
        parts = ["диалогов записано: %d (без изменений: %d)" % (len([w for w in self.written if w.endswith(".txt")]), self.unchanged)]
        if self.world:
            parts.append("мир (NPC, карты, фракции): %d" % len(self.world))
        if self.copied:
            parts.append("озвучка/сцены разложены: %d" % len(self.copied))
        if self.converted:
            parts.append("сконвертировано WAV: %d" % len(self.converted))
        if self.lipsync:
            parts.append("липсинк сгенерирован: %d" % len(self.lipsync))
        if self.deleted:
            parts.append("удалено устаревших: %d" % len(self.deleted))
        if self.errors:
            parts.append("ОШИБОК: %d" % len(self.errors))
        return "; ".join(parts)


def _b(v):
    return "1" if v else "0"


def _write_if_changed(path, text, report, project_name, count=True):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    data = text.encode("utf-8")
    if os.path.exists(path):
        with open(path, "rb") as f:
            old = f.read()
        if old == data:
            if count:
                report.unchanged += 1
            return False
        if not is_ours(old):
            stamp = datetime.datetime.now().strftime("%Y%m%d_%H%M%S")
            bak = path + ".bak_" + stamp
            shutil.copyfile(path, bak)
            report.backups.append(bak)
    tmp = path + ".tmp"
    with open(tmp, "wb") as f:
        f.write(data)
    os.replace(tmp, path)
    if count:
        report.written.append(path)
    return True


class Exporter:
    def __init__(self, project, progress=None, regenerate_all=False, only_npcs=None):
        self.p = project
        self.progress = progress or (lambda frac, msg: None)
        self.regenerate_all = regenerate_all
        self.only_npcs = only_npcs
        self.report = ExportReport()
        self.managed = set()
        self.bark_media = {}      # owner -> {bark id: {lang: (choreo, sound)}}

    # --- media ---------------------------------------------------------------
    def _place_voice(self, npc, lang, key, text, v):
        """Bring one voice into its canonical place; returns (choreo, sound) for the dialog file."""
        p = self.p
        rep = self.report
        gd = p.settings.game_dir
        wav_rel = canonical_wav(npc.targetname, lang, key)
        vcd_rel = canonical_vcd(npc.targetname, lang, key)
        wav_dst = os.path.normpath(os.path.join(gd, wav_rel))
        vcd_dst = os.path.normpath(os.path.join(gd, vcd_rel))
        sound_rel = wav_rel[len("sound/"):]
        choreo_out = ""
        sound_out = ""

        have_wav = False
        if v.wav:
            src = p.game_path(v.wav)
            if not os.path.exists(src):
                rep.errors.append("%s %s/%s: нет файла озвучки %s" % (npc.targetname, key, lang, src))
            else:
                info = audio.wav_info(src) if src.lower().endswith(".wav") else None
                same = os.path.normcase(os.path.abspath(src)) == os.path.normcase(os.path.abspath(wav_dst))
                if same and info and info.source_ready():
                    have_wav = True
                else:
                    ok, msg = audio.convert_for_source(src, wav_dst, p.settings.ffmpeg)
                    if ok:
                        have_wav = True
                        if "сконверт" in msg:
                            rep.converted.append("%s → %s (%s)" % (os.path.basename(src), wav_rel, msg))
                        elif not same:
                            rep.copied.append(wav_rel)
                    else:
                        rep.errors.append("%s %s/%s: %s" % (npc.targetname, key, lang, msg))
        if have_wav:
            self.managed.add(wav_rel)
            v.wav = wav_rel
            info = audio.wav_info(wav_dst)
            v.duration = info.duration if info.ok else v.duration
            sound_out = sound_rel

        # scene
        if v.vcd_kind == "custom" and v.vcd:
            src = p.game_path(v.vcd)
            if os.path.exists(src):
                txt = lipsync.read_text(src)
                if have_wav and lipsync.vcd_sound(txt).lower().endswith(".wav"):
                    txt = lipsync.rewrite_vcd(txt, sound_rel, npc.targetname)
                if os.path.normcase(os.path.abspath(src)) != os.path.normcase(os.path.abspath(vcd_dst)):
                    rep.copied.append(vcd_rel)
                _write_if_changed(vcd_dst, txt, self.report, "", count=False)
                self.managed.add(vcd_rel)
                v.vcd = vcd_rel
                choreo_out = vcd_rel[:-4]
            else:
                rep.errors.append("%s %s/%s: нет файла сцены %s" % (npc.targetname, key, lang, src))
        elif have_wav and text.strip():
            ahash = audio.audio_hash(wav_dst)
            cur = p.game_path(v.vcd) if v.vcd else ""
            fresh = (cur and os.path.exists(cur) and v.lips_text == text and v.lips_audio == ahash
                     and not self.regenerate_all)
            if fresh:
                if os.path.normcase(os.path.abspath(cur)) != os.path.normcase(os.path.abspath(vcd_dst)):
                    txt = lipsync.rewrite_vcd(lipsync.read_text(cur), sound_rel, npc.targetname)
                    os.makedirs(os.path.dirname(vcd_dst), exist_ok=True)
                    with open(vcd_dst, "w", encoding="utf-8", newline="\n") as f:
                        f.write(txt)
                    rep.copied.append(vcd_rel)
            else:
                ok, log = lipsync.generate(lang, text, wav_dst, vcd_dst, npc.targetname, sound_rel, key)
                if ok:
                    rep.lipsync.append("%s %s/%s" % (npc.targetname, key, lang))
                    v.lips_text = text
                    v.lips_audio = audio.audio_hash(wav_dst)  # samples are unchanged by VDAT
                else:
                    rep.errors.append("%s %s/%s: липсинк не создан: %s" % (npc.targetname, key, lang, log.strip()[-300:]))
            if os.path.exists(vcd_dst):
                self.managed.add(vcd_rel)
                v.vcd = vcd_rel
                choreo_out = vcd_rel[:-4]
        if choreo_out:
            sound_out = ""
        return choreo_out, sound_out

    def _media_keys(self, blk, npc, key, texts, voices, choreo_key, sound_key):
        p = self.p
        base = p.base_language
        out = {}
        for lang in p.languages:
            v = voices.get(lang)
            if v and not v.is_empty():
                text = texts.get(lang) or texts.get(base, "")
                out[lang] = self._place_voice(npc, lang, key, text, v)
        if base in out:
            c, s = out[base]
            if c:
                blk.add(choreo_key, c)
            if s:
                blk.add(sound_key, s)
        for lang in p.languages:
            if lang == base:
                continue
            if lang in out:
                c, s = out[lang]
                blk.add(choreo_key + "_" + lang, c)
                blk.add(sound_key + "_" + lang, s)
            elif base in out and p.settings.missing_voice == "silent":
                # no voice in this language: text only instead of the base language voice
                blk.add(choreo_key + "_" + lang, "")
                blk.add(sound_key + "_" + lang, "")

    def _bark_media(self, owner, barks):
        """Voices of barks into place (lip sync like dialog lines): {bark id: {lang: (choreo, sound)}}."""
        p = self.p
        o = _Owner(owner)
        out = {}
        for b in barks:
            m = {}
            for lang in p.languages:
                v = b.voices.get(lang)
                if v and not v.is_empty():
                    text = b.text.get(lang) or b.text.get(p.base_language, "")
                    c, s = self._place_voice(o, lang, bark_media_key(b.id), text, v)
                    if v.wav.replace("\\", "/").startswith("sound/"):
                        s = v.wav.replace("\\", "/")[len("sound/"):]
                    m[lang] = (c, s)
            out[b.id] = m
        return out

    # --- dialog file ---------------------------------------------------------------
    def _texts(self, blk, key, texts):
        base = self.p.base_language
        if texts.get(base):
            blk.add(key, texts[base])
        else:
            # no text in the base language: the plain key is what every other language falls back
            # to - without it a Russian-only returning line made the English game skip the returning
            # visit altogether (the game checks returning_line for being empty)
            fb = next((texts[l] for l in self.p.languages if texts.get(l)), None) or next((t for t in texts.values() if t), None)
            if fb:
                blk.add(key, fb)
        for lang in self.p.languages:
            if lang != base and texts.get(lang):
                blk.add(key + "_" + lang, texts[lang])

    def _quest_kv(self, parent, q):
        qb = parent.add(q.id, None, q.note)
        self._texts(qb, "title", q.title)
        self._texts(qb, "desc", q.desc)
        qb.add("min_rep", str(q.min_rep))
        if q.fail_on_kill:
            qb.add("fail_on_kill_targets", ";".join(q.fail_on_kill))
            qb.add("fail_rep_penalty", str(q.fail_rep_penalty))
        sb = qb.add("stages")
        for i, s in enumerate(q.stages):
            st = sb.add(str(i))
            self._texts(st, "title", s.title)
            self._texts(st, "desc", s.desc)
            if s.kill_targets and s.kill_count > 0:
                st.add("kill_targets", ";".join(s.kill_targets))
                st.add("kill_count", str(s.kill_count))
        for i, s in enumerate(q.stages):
            st = sb.children[i]
            if s.objectives:
                ob = st.add("objectives")
                for o in s.objectives:
                    x = ob.add(o.id)
                    x.add("type", o.type)
                    self._texts(x, "text", o.text)
                    if o.target and o.type in ("kill", "talk", "go", "use", "collect"):
                        x.add("target", o.target)
                    if o.item and o.type == "collect":
                        x.add("item", o.item)
                    if o.count != 1 and o.type in ("kill", "collect"):
                        x.add("count", str(int(o.count)))
                    if o.type == "go" and o.radius != 128:
                        x.add("radius", str(int(o.radius)))
                    if o.optional:
                        x.add("optional", "1")
                    if not o.marker:
                        x.add("marker", "0")
                    if o.type == "cond":
                        logic.write_conds(x, "conditions", o.conditions, self.p)
                    x.inline = True
            logic.write_actions(st, "actions", s.actions, self.p)
            if s.next != -1:
                st.add("next", str(s.next))
            if s.final:
                st.add("final", "1")
        if q.xp:
            qb.add("xp", str(int(q.xp)))
        logic.write_actions(qb, "on_complete", q.on_complete, self.p)
        logic.write_actions(qb, "on_fail", q.on_fail, self.p)
        logic.write_conds(qb, "fail_conditions", q.fail_conditions, self.p)
        rb = qb.add("rewards")
        for item, amount in q.rewards:
            if item and int(amount) > 0:
                rb.add(item, str(int(amount)))

    def build_npc_kv(self, npc, with_media=True):
        p = self.p
        root = kv.KV("Dialog", None, npc.note)
        root.add("entity", npc.targetname)
        self._texts(root, "display_name", npc.display_name)
        root.add("start_node", str(npc.start_node))
        root.add("auto_face_npc", _b(npc.auto_face_npc))
        r = npc.returning
        if r.enabled():
            self._texts(root, "returning_line", r.line)
            if with_media:
                self._media_keys(root, npc, "returning", r.line, r.voice, "returning_choreo", "returning_sound")
            if r.next != -1:
                root.add("returning_auto_next", str(r.next))
            root.add("returning_no_options", _b(r.no_options))
            root.add("returning_auto_close", _b(r.auto_close))

        # (the quests go to scripts/rpg/quests.txt - every NPC may give any of them)

        nb = root.add("nodes")
        for n in sorted(npc.nodes, key=lambda x: x.id):
            b = nb.add(str(n.id), None, n.note)
            if getattr(n, "topic", ""):
                b.add("topic", n.topic)
            self._texts(b, "line", n.line)
            if with_media:
                self._media_keys(b, npc, media_key_node(n.id), n.line, n.voice, "choreo", "sound")
            if n.sequence:
                b.add("sequence", n.sequence)
            if n.server_sequence:
                b.add("server_sequence", n.server_sequence)
            b.add("option_delay", n.option_delay or "auto")
            if n.auto_next != -1 and not n.options:
                b.add("auto_next", str(n.auto_next))
            if n.end and not n.options:
                b.add("end", "1")
            if n.spoils:
                b.add("spoils", "1")
            logic.write_actions(b, "actions", n.actions, p)
            if n.options:
                ob = b.add("options")
                for oi, o in enumerate(n.options):
                    x = ob.add(str(oi), None, o.note)
                    x.inline = True
                    if getattr(o, "topic", ""):
                        x.add("topic", o.topic)
                    self._texts(x, "text", o.text)
                    x.add("next", str(o.next))
                    if o.close:
                        x.add("close", "1")
                    if o.spoils:
                        x.add("spoils", "1")
                    if o.trade:
                        x.add("trade", "1")
                    if o.server_sequence:
                        x.add("server_sequence", o.server_sequence)
                    if o.grant:
                        x.add("grant_quest", o.grant)
                    if o.complete:
                        x.add("complete_quest", o.complete)
                    if o.fail:
                        x.add("fail_quest", o.fail)
                    if o.set_stage and o.set_stage[0]:
                        x.add("set_stage", "%s:%d" % (o.set_stage[0], o.set_stage[1]))
                    if o.complete:
                        if o.after_next != -1:
                            x.add("after_next", str(o.after_next))
                        elif any(o.after_line.values()):
                            self._texts(x, "after_line", o.after_line)
                            if with_media:
                                self._media_keys(x, npc, media_key_after(n.id, oi), o.after_line, o.after_voice,
                                                 "after_choreo", "after_sound")
                            if o.after_sequence:
                                x.add("after_sequence", o.after_sequence)
                    if o.require_rep is not None:
                        x.add("require_rep", str(o.require_rep))
                    if o.require_state and o.require_state[0]:
                        x.add("require_quest_state", "%s:%s" % tuple(o.require_state))
                    if o.require_stage and o.require_stage[0]:
                        x.add("require_quest_stage", "%s:%d" % (o.require_stage[0], o.require_stage[1]))
                    if o.conditions:
                        logic.write_conds(x, "conditions", o.conditions, p)
                        if o.show_unmet:
                            x.add("show_unmet", "1")
                    if o.check and (o.check.get("skill") or o.check.get("stat")):
                        key = "stat" if o.check.get("stat") else "skill"
                        cb = x.add("check")
                        cb.add(key, o.check[key])
                        cb.add("value", str(int(o.check.get("value", 0) or 0)))
                        cb.inline = True
                        if o.fail_next != -1:
                            x.add("fail_next", str(o.fail_next))
                        if not o.check_once:
                            x.add("check_once", "0")
                        logic.write_actions(x, "fail_actions", o.fail_actions, p)
                    logic.write_actions(x, "actions", o.actions, p)
        return root

    def quests_kv(self):
        root = kv.KV("Quests")
        for q in self.p.quests:
            self._quest_kv(root, q)
        return root

    def events_kv(self):
        root = kv.KV("Events")
        for e in self.p.events:
            b = root.add(e.id, None, e.note)
            b.add("on", e.on)
            if e.target and logic.EVENT_BY_ID.get(e.on, ("", "", "", ""))[2]:
                b.add("target", e.target)
            if str(e.value) != "" and logic.EVENT_BY_ID.get(e.on, ("", "", "", ""))[3]:
                b.add("value", str(e.value))
            if e.map:
                b.add("map", e.map)
            b.add("once", _b(e.once))
            if e.delay:
                b.add("delay", "%g" % e.delay)
            logic.write_conds(b, "conditions", e.conditions, self.p)
            logic.write_actions(b, "actions", e.actions, self.p)
        return root

    def music_kv(self):
        root = kv.KV("Music")
        tb = root.add("tracks")
        for t in self.p.music:
            if not t.id or not t.file:
                continue
            b = tb.add(t.id, None, t.note)
            b.add("file", t.file.replace("\\", "/"))
            if t.volume != 1.0:
                b.add("volume", "%g" % round(t.volume, 2))
            b.add("kind", t.kind)
            if t.time != "any" and t.kind in ("explore", "combat", "location"):
                b.add("time", t.time)
            if t.maps:
                b.add("maps", ";".join(t.maps))
            if t.event and t.kind == "stinger":
                b.add("event", t.event)
            b.inline = True
        return root

    def barks_kv(self):
        root = kv.KV("Barks")
        for s in self.p.bark_sets:
            if not s.id:
                continue
            b = root.add(s.id, None, s.note)
            if s.classes:
                b.add("classes", ";".join(s.classes))
            world_export.barks_kv(b, s.barks, self.p, self.bark_media.get("barks_" + s.id))
        return root

    def soundscapes_text(self, header):
        """Several top-level blocks, like the game's own soundscapes_*.txt."""
        lines = ["// " + h for h in header.splitlines()]
        for s in self.p.soundscapes:
            if not s.name:
                continue
            b = kv.KV(s.name)
            if s.note:
                b.comment = s.note
            b.add("dsp", str(int(s.dsp)))
            for lp in s.loops:
                if not lp.get("wave"):
                    continue
                x = b.add("playlooping")
                x.add("volume", "%g" % round(float(lp.get("volume", 0.6)), 2))
                x.add("wave", lp["wave"].replace("\\", "/"))
                x.add("pitch", str(int(lp.get("pitch", 100))))
                if int(lp.get("soundlevel", 0) or 0):
                    x.add("soundlevel", "SNDLVL_%ddB" % int(lp["soundlevel"]))
                x.inline = True
            for r in s.randoms:
                waves = [w for w in r.get("waves", []) if w]
                if not waves:
                    continue
                x = b.add("playrandom")
                x.add("time", "%g,%g" % tuple(r.get("time", (8, 20))))
                x.add("volume", "%g,%g" % tuple(round(v, 2) for v in r.get("volume", (0.4, 0.7))))
                x.add("pitch", "%g,%g" % tuple(r.get("pitch", (95, 105))))
                if int(r.get("soundlevel", 0) or 0):
                    x.add("soundlevel", "SNDLVL_%ddB" % int(r["soundlevel"]))
                rw = x.add("rndwave")
                for wv in waves:
                    rw.add("wave", wv.replace("\\", "/"))
            lines.extend(kv.write(b, 0))
            lines.append("")
        return "\n".join(lines) + "\n"

    def globals_kv(self):
        root = kv.KV("Globals")
        fb = root.add("flags")
        vb = root.add("vars")
        for g in self.p.globals:
            (fb if g.kind == "flag" else vb).add(g.name, str(int(g.default)), g.note)
        return root

    def _quest_owner(self, q):
        names = [n.targetname for n in self.p.npcs]
        if q.owner in names:
            return q.owner
        for n in self.p.npcs:
            for node in n.nodes:
                for o in node.options:
                    if q.id in o.quest_refs():
                        return n.targetname
        return names[0] if names else ""

    TOPIC_NODE = 9000

    def with_topics(self, npc):
        """The NPC as exported: the shared topics of its class / faction added to its dialog (a copy;
        the NPC itself if none matches). The hub - the start node with options (else the node the
        returning line leads to); an NPC without lines gets a greeting node 0."""
        p = self.p
        if npc.actor.is_template or not getattr(p, "topics", None):
            return npc
        a = npc.actor.resolved(p)
        tops = [t for t in p.topics if t.matches(a.classname, a.faction) and any(v for v in t.text.values())]
        if not tops:
            return npc
        from .model import NPC, Node, Option
        n2 = NPC.from_dict(copy.deepcopy(npc.to_dict()))
        hub = n2.node(n2.start_node)
        if hub is None or not hub.options:
            alt = n2.node(n2.returning.next) if n2.returning.next != -1 else None
            if alt is not None and alt.options:
                hub = alt
            elif not npc.has_dialog():
                n2.nodes = []
                hub = Node(0)
                hub.line = dict(p.topic_greeting) or {"english": "Yes?", "russian": "Чего тебе?"}
                hub.topic = "greeting"
                # the project's own Voice objects (not copies): the export writes their new paths and lip sync back
                hub.voice = p.topic_greeting_voices.setdefault(npc.targetname, {})
                bye = Option()
                bye.text = {"english": "Bye.", "russian": "Пока."}
                bye.close = True
                bye.topic = "greeting"
                hub.options = [bye]
                n2.nodes.append(hub)
                n2.start_node = 0
                n2.returning.line = {}
            else:
                return npc
        used = set(n2.node_ids())
        nid = self.TOPIC_NODE
        # before the closing options at the end ("Bye")
        at = len(hub.options)
        while at > 0 and hub.options[at - 1].close and not hub.options[at - 1].next >= 0:
            at -= 1
        added = []
        for t in tops:
            while nid in used:
                nid += 1
            used.add(nid)
            o = Option()
            o.text = dict(t.text)
            o.next = nid
            o.conditions = copy.deepcopy(t.conditions)
            o.actions = copy.deepcopy(t.actions)
            if t.once:
                flag = "topic_%s_%s" % (t.id, npc.targetname)
                o.conditions.append({"type": "flag", "name": flag, "not": 1})
                o.actions.insert(0, {"type": "flag", "name": flag, "value": 1})
            o.topic = t.id
            hub.options.insert(at + len(added), o)
            nd = Node(nid)
            nd.line = dict(t.line)
            nd.topic = t.id
            nd.voice = t.voices.setdefault(npc.targetname, {})
            added.append(nd)
        # after the answer - the same list again (as in New Vegas)
        for nd in added:
            nd.options = copy.deepcopy(hub.options)
            n2.nodes.append(nd)
        return n2

    def topics_kv(self):
        root = kv.KV("Topics")
        g = root.add("greeting")
        self._texts(g, "line", self.p.topic_greeting)
        for t in self.p.topics:
            b = root.add(t.id, None, t.note)
            if t.classes:
                b.add("classes", ";".join(t.classes))
            if t.factions:
                b.add("factions", ";".join(t.factions))
            self._texts(b, "text", t.text)
            self._texts(b, "line", t.line)
            if t.once:
                b.add("once", "1")
            logic.write_conds(b, "conditions", t.conditions, self.p)
            logic.write_actions(b, "actions", t.actions, self.p)
        return root

    def dialog_text(self, npc, with_media=False):
        root = self.build_npc_kv(npc, with_media=with_media)
        return kv.dumps(root, header=HEADER % os.path.basename(self.p.path or "project"))

    # --- run -------------------------------------------------------------------------
    def run(self):
        p = self.p
        gd = p.settings.game_dir
        if not gd or not os.path.isdir(gd):
            self.report.errors.append("Папка игры не найдена: %s (Проект → Настройки)" % gd)
            return self.report
        npcs = [n for n in p.npcs if self.only_npcs is None or n.targetname in self.only_npcs]
        total = max(1, len(npcs))
        header = HEADER % os.path.basename(p.path or "project")
        # voices of barks first: NPCs made from a template say the template's barks
        for n in p.npcs:
            if n.actor.barks:
                self.bark_media[n.targetname] = self._bark_media(n.targetname, n.actor.barks)
        for s in p.bark_sets:
            if s.barks:
                self.bark_media["barks_" + s.id] = self._bark_media("barks_" + s.id, s.barks)
        for i, npc0 in enumerate(npcs):
            self.progress(i / total * 0.9, "NPC %s" % npc0.targetname)
            npc = self.with_topics(npc0)
            if npc.has_dialog():
                root = self.build_npc_kv(npc, with_media=True)
                text = kv.dumps(root, header=header)
                rel = "scripts/dialogs/%s.txt" % npc.targetname
                _write_if_changed(os.path.normpath(os.path.join(gd, rel)), text, self.report, p.path)
                self.managed.add(rel)
            npc = npc0
            if world_export.needs_npc_file(npc) or p.placements(npc.targetname):
                rel = "scripts/npcs/%s.txt" % npc.targetname
                text = kv.dumps(world_export.npc_kv(p, npc, self.bark_media), header=header)
                if _write_if_changed(os.path.normpath(os.path.join(gd, rel)), text, self.report, p.path, count=False):
                    self.report.world.append(rel)
                self.managed.add(rel)
        if self.only_npcs is None:
            self.progress(0.92, "Карты и фракции")
            self._world(header)
            self._cleanup()
            p.managed_files = sorted(self.managed)
        else:
            p.managed_files = sorted(set(p.managed_files) | self.managed)
        self.progress(1.0, "Готово")
        return self.report

    def _world(self, header):
        p = self.p
        gd = p.settings.game_dir

        def put(rel, text):
            if _write_if_changed(os.path.normpath(os.path.join(gd, rel)), text, self.report, p.path, count=False):
                self.report.world.append(rel)
            self.managed.add(rel)
        for name, layer in sorted(p.maps.items()):
            if layer.refs:
                put("scripts/world/%s.txt" % name, kv.dumps(world_export.layer_kv(p, layer), header=header))
            if layer.nav:
                put("scripts/world/%s_nav.txt" % name, world_export.nav_text(layer, header))
        if p.factions:
            put("scripts/world/factions.txt", kv.dumps(world_export.factions_kv(p), header=header))
        if p.items:
            put("scripts/rpg/items.txt", kv.dumps(rpg_export.items_kv(p), header=header))
        if p.lootlists:
            put("scripts/rpg/lootlists.txt", kv.dumps(rpg_export.lootlists_kv(p), header=header))
        if getattr(p, "terminals", None):
            from . import termdata
            put("scripts/rpg/terminals.txt", kv.dumps(termdata.terminals_kv(p), header=header))
        if p.quests:
            put("scripts/rpg/quests.txt", kv.dumps(self.quests_kv(), header=header))
        if p.events:
            put("scripts/rpg/events.txt", kv.dumps(self.events_kv(), header=header))
        if getattr(p, "topics", None):
            put("scripts/rpg/topics.txt", kv.dumps(self.topics_kv(), header=header))
        if p.globals:
            put("scripts/rpg/globals.txt", kv.dumps(self.globals_kv(), header=header))
        if p.music:
            put("scripts/rpg/music.txt", kv.dumps(self.music_kv(), header=header))
        if p.bark_sets:
            put("scripts/rpg/barks.txt", kv.dumps(self.barks_kv(), header=header))
        if p.soundscapes:
            put("scripts/soundscapes_hl2rpm.txt", self.soundscapes_text(header))
        if rpgchar.uses_rpg(p):
            put("scripts/rpg/rpg.txt", kv.dumps(rpgchar.rpg_kv(p), header=header))
        if p.newgame:
            put("scripts/rpg/newgame.txt", kv.dumps(rpgchar.newgame_kv(p), header=header))

    def _cleanup(self):
        gd = self.p.settings.game_dir
        for rel in set(self.p.managed_files) - self.managed:
            path = os.path.normpath(os.path.join(gd, rel))
            if not os.path.exists(path):
                continue
            low = rel.replace("\\", "/").lower()
            if (low.startswith("scripts/dialogs/") or low.startswith("scripts/npcs/") or low.startswith("scripts/world/")
                    or low.startswith("scripts/rpg/") or low == "scripts/soundscapes_hl2rpm.txt"):
                with open(path, "rb") as f:
                    if not is_ours(f.read(400)):
                        continue
            elif not (low.startswith("sound/dialogs/") or low.startswith("scenes/dialogs/")):
                continue
            try:
                os.remove(path)
                self.report.deleted.append(rel)
            except OSError as e:
                self.report.warnings.append("не удалось удалить %s: %s" % (rel, e))
        # empty folders left by renamed NPCs
        for top in ("sound/dialogs", "scenes/dialogs"):
            base = os.path.join(gd, top)
            if not os.path.isdir(base):
                continue
            for dirpath, dirnames, filenames in os.walk(base, topdown=False):
                if not dirnames and not filenames and os.path.abspath(dirpath) != os.path.abspath(base):
                    try:
                        os.rmdir(dirpath)
                    except OSError:
                        pass
