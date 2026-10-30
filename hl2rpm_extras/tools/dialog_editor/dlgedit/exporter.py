"""Write a project into the game folders.

    scripts/dialogs/<npc>.txt                    dialog + its quests, all languages
    sound/dialogs/<npc>/<lang>/<key>.wav         voice (converted for Source if needed)
    scenes/dialogs/<npc>/<lang>/<key>.vcd        scene with lip sync

Plain keys carry the base language, "<key>_<lang>" the others; the game picks by
its language (dialog_language / cl_language). Only files the editor wrote
itself (project.managed_files) are ever deleted.
"""
import datetime
import os
import shutil

from . import audio
from . import kv
from . import lipsync
from .model import canonical_vcd, canonical_wav, media_key_after, media_key_node

HEADER = ("HL2RPM Dialog Editor: generated from %s\n"
          "Edit this NPC in the editor - manual changes are overwritten on save.")
HEADER_MARK = "HL2RPM Dialog Editor: generated"


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

    def summary(self):
        parts = ["диалогов записано: %d (без изменений: %d)" % (len([w for w in self.written if w.endswith(".txt")]), self.unchanged)]
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
        if HEADER_MARK.encode() not in old[:400]:
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

    # --- dialog file ---------------------------------------------------------------
    def _texts(self, blk, key, texts):
        base = self.p.base_language
        if texts.get(base):
            blk.add(key, texts[base])
        elif key in ("line", "text") and texts:
            # the game needs a plain value; show any translation instead of nothing
            blk.add(key, next(iter(texts.values())))
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

        quests = [q for q in p.quests if self._quest_owner(q) == npc.targetname]
        if quests:
            qb = root.add("quests")
            for q in quests:
                self._quest_kv(qb, q)

        nb = root.add("nodes")
        for n in sorted(npc.nodes, key=lambda x: x.id):
            b = nb.add(str(n.id), None, n.note)
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
            if n.options:
                ob = b.add("options")
                for oi, o in enumerate(n.options):
                    x = ob.add(str(oi), None, o.note)
                    x.inline = True
                    self._texts(x, "text", o.text)
                    x.add("next", str(o.next))
                    if o.close:
                        x.add("close", "1")
                    if o.spoils:
                        x.add("spoils", "1")
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
        for i, npc in enumerate(npcs):
            self.progress(i / total, "NPC %s" % npc.targetname)
            root = self.build_npc_kv(npc, with_media=True)
            text = kv.dumps(root, header=HEADER % os.path.basename(p.path or "project"))
            rel = "scripts/dialogs/%s.txt" % npc.targetname
            _write_if_changed(os.path.normpath(os.path.join(gd, rel)), text, self.report, p.path)
            self.managed.add(rel)
        if self.only_npcs is None:
            self._cleanup()
            p.managed_files = sorted(self.managed)
        else:
            p.managed_files = sorted(set(p.managed_files) | self.managed)
        self.progress(1.0, "Готово")
        return self.report

    def _cleanup(self):
        gd = self.p.settings.game_dir
        for rel in set(self.p.managed_files) - self.managed:
            path = os.path.normpath(os.path.join(gd, rel))
            if not os.path.exists(path):
                continue
            low = rel.replace("\\", "/").lower()
            if low.startswith("scripts/dialogs/"):
                with open(path, "rb") as f:
                    if HEADER_MARK.encode() not in f.read(400):
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
