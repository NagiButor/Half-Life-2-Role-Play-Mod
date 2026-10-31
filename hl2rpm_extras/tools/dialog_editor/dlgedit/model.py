"""Project data model of the HL2RPM Creation Kit (formerly the dialog editor).

A project (.hl2dlg, JSON) holds every NPC (dialog in all languages, actor data and
AI packages), quest, faction and what is placed on each map. Saving writes the
game files:

    <game_dir>/scripts/dialogs/<targetname>.txt
    <game_dir>/sound/dialogs/<targetname>/<language>/<key>.wav
    <game_dir>/scenes/dialogs/<targetname>/<language>/<key>.vcd
    <game_dir>/scripts/npcs/<targetname>.txt, scripts/world/<map>.txt (see worlddata.py)

where <key> is the node id, "returning" or "<node>_<option>_after".
Texts are dicts language -> text; the project's base language is written to
the plain keys ("line"), the others with a suffix ("line_russian").
"""
import copy
import json
import os
import re

from .worlddata import Actor, Faction, MapLayer

FORMAT = "hl2rpm-dialog-project"
VERSION = 2

# Source engine language names (cl_language / Steam API) and how to show them
KNOWN_LANGUAGES = [
    ("english", "English"), ("russian", "Русский"), ("ukrainian", "Українська"),
    ("german", "Deutsch"), ("french", "Français"), ("spanish", "Español"),
    ("latam", "Español (LatAm)"), ("italian", "Italiano"), ("polish", "Polski"),
    ("brazilian", "Português (BR)"), ("portuguese", "Português"), ("czech", "Čeština"),
    ("turkish", "Türkçe"), ("japanese", "日本語"), ("koreana", "한국어"),
    ("schinese", "简体中文"), ("tchinese", "繁體中文"),
]
LANGUAGE_NAMES = dict(KNOWN_LANGUAGES)
SHORT_LANG = {"english": "EN", "russian": "RU", "ukrainian": "UA", "german": "DE", "french": "FR",
              "spanish": "ES", "latam": "LA", "italian": "IT", "polish": "PL", "brazilian": "BR",
              "portuguese": "PT", "czech": "CZ", "turkish": "TR", "japanese": "JA", "koreana": "KO",
              "schinese": "ZH", "tchinese": "TW"}

QUEST_STATES = ["inactive", "active", "completed", "failed"]
QUEST_STATE_NAMES = {"inactive": "не взят", "active": "активен", "completed": "выполнен", "failed": "провален"}

# Characters the game's client payload uses as separators
BAD_CHARS = "|;~"


def short_lang(lang):
    return SHORT_LANG.get(lang, lang[:2].upper())


def clean_text(s):
    """Texts are stored without straight double quotes (they would end a KeyValues
    value) and without CR; quotes become typographic ones."""
    if not s:
        return ""
    s = s.replace("\r\n", "\n").replace("\r", "\n")
    if '"' in s:
        out = []
        for i, ch in enumerate(s):
            if ch == '"':
                prev = s[i - 1] if i > 0 else " "
                out.append("“" if prev.isspace() or prev in "([{«—-" else "”")
            else:
                out.append(ch)
        s = "".join(out)
    return s


class Voice:
    """Voice-over of one text in one language."""

    def __init__(self):
        self.wav = ""          # path: game-relative ("sound/...") or absolute
        self.vcd = ""          # current scene: game-relative or absolute, "" = none yet
        self.vcd_kind = "auto"  # auto: editor (re)generates lip sync; custom: hand-made, copied as is
        self.lips_text = ""    # text the current auto scene was generated from
        self.lips_audio = ""   # audio hash the current auto scene was generated from
        self.duration = 0.0

    def is_empty(self):
        return not self.wav and not self.vcd

    def to_dict(self):
        d = {"wav": self.wav}
        if self.vcd:
            d["vcd"] = self.vcd
        if self.vcd_kind != "auto":
            d["vcd_kind"] = self.vcd_kind
        if self.lips_text:
            d["lips_text"] = self.lips_text
        if self.lips_audio:
            d["lips_audio"] = self.lips_audio
        if self.duration:
            d["duration"] = round(self.duration, 3)
        return d

    @staticmethod
    def from_dict(d):
        v = Voice()
        v.wav = d.get("wav", "")
        v.vcd = d.get("vcd", "")
        v.vcd_kind = d.get("vcd_kind", "auto")
        v.lips_text = d.get("lips_text", "")
        v.lips_audio = d.get("lips_audio", "")
        v.duration = float(d.get("duration", 0.0))
        return v


def _texts_from(d):
    return {k: clean_text(v) for k, v in (d or {}).items() if v}


def _voices_from(d):
    out = {}
    for k, v in (d or {}).items():
        vo = Voice.from_dict(v)
        if not vo.is_empty():
            out[k] = vo
    return out


def _voices_to(d):
    return {k: v.to_dict() for k, v in d.items() if not v.is_empty()}


class Option:
    """A player reply."""

    def __init__(self):
        self.text = {}
        self.next = -1
        self.close = False
        self.spoils = False
        self.server_sequence = ""
        # quest actions (the game runs all that are set, in this order)
        self.grant = ""
        self.complete = ""
        self.fail = ""
        self.set_stage = None                # [quest, stage] or None
        # after completing a quest
        self.after_next = -1
        self.after_line = {}
        self.after_voice = {}
        self.after_sequence = ""
        # conditions (option hidden when false)
        self.require_rep = None              # int or None
        self.require_state = None            # [quest, state] or None
        self.require_stage = None            # [quest, stage] or None
        self.note = ""

    def to_dict(self):
        d = {"text": self.text, "next": self.next}
        for k in ("close", "spoils"):
            if getattr(self, k):
                d[k] = True
        if self.server_sequence:
            d["server_sequence"] = self.server_sequence
        for k in ("grant", "complete", "fail"):
            if getattr(self, k):
                d[k] = getattr(self, k)
        if self.set_stage:
            d["set_stage"] = list(self.set_stage)
        if self.after_next != -1:
            d["after_next"] = self.after_next
        if self.after_line:
            d["after_line"] = self.after_line
        if self.after_voice:
            d["after_voice"] = _voices_to(self.after_voice)
        if self.after_sequence:
            d["after_sequence"] = self.after_sequence
        if self.require_rep is not None:
            d["require_rep"] = self.require_rep
        if self.require_state:
            d["require_state"] = list(self.require_state)
        if self.require_stage:
            d["require_stage"] = list(self.require_stage)
        if self.note:
            d["note"] = self.note
        return d

    @staticmethod
    def from_dict(d):
        o = Option()
        o.text = _texts_from(d.get("text"))
        o.next = int(d.get("next", -1))
        o.close = bool(d.get("close", False))
        o.spoils = bool(d.get("spoils", False))
        o.server_sequence = d.get("server_sequence", "")
        o.grant = d.get("grant", "")
        o.complete = d.get("complete", "")
        o.fail = d.get("fail", "")
        ss = d.get("set_stage")
        o.set_stage = [ss[0], int(ss[1])] if ss else None
        o.after_next = int(d.get("after_next", -1))
        o.after_line = _texts_from(d.get("after_line"))
        o.after_voice = _voices_from(d.get("after_voice"))
        o.after_sequence = d.get("after_sequence", "")
        rr = d.get("require_rep")
        o.require_rep = int(rr) if rr is not None else None
        rs = d.get("require_state")
        o.require_state = [rs[0], rs[1]] if rs else None
        rg = d.get("require_stage")
        o.require_stage = [rg[0], int(rg[1])] if rg else None
        o.note = d.get("note", "")
        return o

    def has_after(self):
        """The game uses after_* only for an option that completes a quest and has no next."""
        return bool(self.complete) and self.next == -1 and (self.after_next != -1 or any(self.after_line.values()))

    def leads_somewhere(self):
        return self.next != -1 or self.close or self.spoils or self.has_after()

    def quest_refs(self):
        refs = [q for q in (self.grant, self.complete, self.fail) if q]
        if self.set_stage:
            refs.append(self.set_stage[0])
        if self.require_state:
            refs.append(self.require_state[0])
        if self.require_stage:
            refs.append(self.require_stage[0])
        return refs


class Node:
    """An NPC line."""

    def __init__(self, nid=0):
        self.id = nid
        self.pos = [0.0, 0.0]
        self.line = {}
        self.voice = {}
        self.sequence = ""
        self.server_sequence = ""
        self.option_delay = "auto"   # "auto" or seconds as string
        self.auto_next = -1
        self.end = False
        self.spoils = False
        self.options = []
        self.note = ""

    def to_dict(self):
        d = {"id": self.id, "pos": [round(self.pos[0], 1), round(self.pos[1], 1)], "line": self.line}
        if self.voice:
            d["voice"] = _voices_to(self.voice)
        for k in ("sequence", "server_sequence"):
            if getattr(self, k):
                d[k] = getattr(self, k)
        if self.option_delay != "auto":
            d["option_delay"] = self.option_delay
        if self.auto_next != -1:
            d["auto_next"] = self.auto_next
        if self.end:
            d["end"] = True
        if self.spoils:
            d["spoils"] = True
        if self.options:
            d["options"] = [o.to_dict() for o in self.options]
        if self.note:
            d["note"] = self.note
        return d

    @staticmethod
    def from_dict(d):
        n = Node(int(d.get("id", 0)))
        p = d.get("pos", [0, 0])
        n.pos = [float(p[0]), float(p[1])]
        n.line = _texts_from(d.get("line"))
        n.voice = _voices_from(d.get("voice"))
        n.sequence = d.get("sequence", "")
        n.server_sequence = d.get("server_sequence", "")
        n.option_delay = str(d.get("option_delay", "auto")) or "auto"
        n.auto_next = int(d.get("auto_next", -1))
        n.end = bool(d.get("end", False))
        n.spoils = bool(d.get("spoils", False))
        n.options = [Option.from_dict(o) for o in d.get("options", [])]
        n.note = d.get("note", "")
        return n

    def flow_kind(self):
        if self.options:
            return "options"
        if self.auto_next != -1:
            return "auto"
        if self.end or self.spoils:
            return "end"
        return "none"


class Returning:
    """What the NPC says on the second and later visits."""

    def __init__(self):
        self.line = {}
        self.voice = {}
        self.next = -1          # node whose options follow (-1 = start node); with no_options: node to go to after the line
        self.no_options = False
        self.auto_close = False
        self.pos = [0.0, 0.0]

    def enabled(self):
        return bool(any(self.line.values()))

    def to_dict(self):
        d = {"line": self.line, "next": self.next, "pos": [round(self.pos[0], 1), round(self.pos[1], 1)]}
        if self.voice:
            d["voice"] = _voices_to(self.voice)
        if self.no_options:
            d["no_options"] = True
        if self.auto_close:
            d["auto_close"] = True
        return d

    @staticmethod
    def from_dict(d):
        r = Returning()
        if not d:
            return r
        r.line = _texts_from(d.get("line"))
        r.voice = _voices_from(d.get("voice"))
        r.next = int(d.get("next", -1))
        r.no_options = bool(d.get("no_options", False))
        r.auto_close = bool(d.get("auto_close", False))
        p = d.get("pos", [0, 0])
        r.pos = [float(p[0]), float(p[1])]
        return r


class NPC:
    def __init__(self, targetname="npc"):
        self.targetname = targetname
        self.display_name = {}
        self.start_node = 0
        self.auto_face_npc = True
        self.returning = Returning()
        self.nodes = []
        self.note = ""
        self.start_pos = [0.0, 0.0]
        self.actor = Actor()

    def node(self, nid):
        for n in self.nodes:
            if n.id == nid:
                return n
        return None

    def node_ids(self):
        return [n.id for n in self.nodes]

    def new_node_id(self, near=None):
        used = set(self.node_ids())
        if near is not None:
            # next free id after `near` within the same "hundred" block first
            cand = near + 1
            while cand in used:
                cand += 1
            return cand
        cand = 0
        while cand in used:
            cand += 1
        return cand

    def to_dict(self):
        return {
            "targetname": self.targetname,
            "display_name": self.display_name,
            "start_node": self.start_node,
            "auto_face_npc": self.auto_face_npc,
            "returning": self.returning.to_dict(),
            "start_pos": [round(self.start_pos[0], 1), round(self.start_pos[1], 1)],
            "note": self.note,
            "nodes": [n.to_dict() for n in self.nodes],
            "actor": self.actor.to_dict(),
        }

    @staticmethod
    def from_dict(d):
        n = NPC(d.get("targetname", "npc"))
        n.display_name = _texts_from(d.get("display_name"))
        n.start_node = int(d.get("start_node", 0))
        n.auto_face_npc = bool(d.get("auto_face_npc", True))
        n.returning = Returning.from_dict(d.get("returning"))
        p = d.get("start_pos", [0, 0])
        n.start_pos = [float(p[0]), float(p[1])]
        n.note = d.get("note", "")
        n.nodes = [Node.from_dict(x) for x in d.get("nodes", [])]
        n.actor = Actor.from_dict(d.get("actor"))
        return n

    def has_dialog(self):
        """An NPC made only for the world (no line written) gets no dialog file."""
        return len(self.nodes) > 1 or any(any(n.line.values()) or n.options for n in self.nodes) or self.returning.enabled()

    def returning_target(self):
        """Node whose options the returning visit shows (game logic: from the
        chosen node follow auto_next while the node has no options)."""
        nid = self.returning.next if self.returning.next != -1 else self.start_node
        if self.returning.no_options:
            return nid
        seen = 0
        while seen < 16:
            n = self.node(nid)
            if not n or n.options or n.auto_next == -1:
                break
            nid = n.auto_next
            seen += 1
        return nid

    def renumber_node(self, old, new):
        n = self.node(old)
        if not n or self.node(new):
            return False
        n.id = new
        if self.start_node == old:
            self.start_node = new
        if self.returning.next == old:
            self.returning.next = new
        for x in self.nodes:
            if x.auto_next == old:
                x.auto_next = new
            for o in x.options:
                if o.next == old:
                    o.next = new
                if o.after_next == old:
                    o.after_next = new
        return True

    def remove_node(self, nid):
        self.nodes = [n for n in self.nodes if n.id != nid]
        if self.returning.next == nid:
            self.returning.next = -1
        for x in self.nodes:
            if x.auto_next == nid:
                x.auto_next = -1
            for o in x.options:
                if o.next == nid:
                    o.next = -1
                if o.after_next == nid:
                    o.after_next = -1


class Stage:
    def __init__(self):
        self.title = {}
        self.desc = {}
        self.kill_targets = []
        self.kill_count = 0

    def to_dict(self):
        d = {"title": self.title, "desc": self.desc}
        if self.kill_targets:
            d["kill_targets"] = self.kill_targets
            d["kill_count"] = self.kill_count
        return d

    @staticmethod
    def from_dict(d):
        s = Stage()
        s.title = _texts_from(d.get("title"))
        s.desc = _texts_from(d.get("desc"))
        s.kill_targets = list(d.get("kill_targets", []))
        s.kill_count = int(d.get("kill_count", 0))
        return s


class Quest:
    def __init__(self, qid="quest"):
        self.id = qid
        self.owner = ""          # NPC targetname whose file carries the quest
        self.title = {}
        self.desc = {}
        self.min_rep = 0
        self.fail_on_kill = []   # targetnames: killing one fails the quest
        self.fail_rep_penalty = 0
        self.stages = []
        self.rewards = []        # [[classname, amount], ...]
        self.note = ""

    def to_dict(self):
        d = {"id": self.id, "owner": self.owner, "title": self.title, "desc": self.desc,
             "min_rep": self.min_rep, "stages": [s.to_dict() for s in self.stages],
             "rewards": [[r[0], int(r[1])] for r in self.rewards]}
        if self.fail_on_kill:
            d["fail_on_kill"] = self.fail_on_kill
        if self.fail_rep_penalty:
            d["fail_rep_penalty"] = self.fail_rep_penalty
        if self.note:
            d["note"] = self.note
        return d

    @staticmethod
    def from_dict(d):
        q = Quest(d.get("id", "quest"))
        q.owner = d.get("owner", "")
        q.title = _texts_from(d.get("title"))
        q.desc = _texts_from(d.get("desc"))
        q.min_rep = int(d.get("min_rep", 0))
        q.fail_on_kill = list(d.get("fail_on_kill", []))
        q.fail_rep_penalty = int(d.get("fail_rep_penalty", 0))
        q.stages = [Stage.from_dict(s) for s in d.get("stages", [])]
        q.rewards = [[r[0], int(r[1])] for r in d.get("rewards", [])]
        q.note = d.get("note", "")
        return q


class Settings:
    def __init__(self):
        self.game_dir = r"E:\Steam\steamapps\sourcemods\hl2rpm"
        self.mod_dir = r"E:\Steam\steamapps\sourcemods\hl2rpm"
        self.maps_dirs = []
        self.languages = ["english", "russian"]
        self.base_language = "english"
        self.missing_voice = "silent"   # silent: a language without voice is text only; base: play the base language voice
        self.hl2_exe = r"E:\Steam\steamapps\common\Source SDK Base 2013 Singleplayer\hl2.exe"
        self.ffmpeg = ""
        self.test_map = ""
        self.nav_spacing = 128

    def to_dict(self):
        return dict(self.__dict__)

    @staticmethod
    def from_dict(d):
        s = Settings()
        for k, v in (d or {}).items():
            if hasattr(s, k):
                setattr(s, k, v)
        if s.base_language not in s.languages:
            s.languages.insert(0, s.base_language)
        return s


class Project:
    def __init__(self):
        self.path = ""
        self.settings = Settings()
        self.npcs = []
        self.quests = []
        self.managed_files = []   # game-relative paths written by the editor (cleanup of renamed/deleted)
        self.view = {}            # per NPC: canvas zoom/center (editor state)
        self.factions = []
        self.maps = {}            # map name -> MapLayer

    # --- lookup ----------------------------------------------------------
    def npc(self, targetname):
        t = targetname.lower()
        for n in self.npcs:
            if n.targetname.lower() == t:
                return n
        return None

    def quest(self, qid):
        for q in self.quests:
            if q.id == qid:
                return q
        return None

    def quest_ids(self):
        return [q.id for q in self.quests]

    def faction(self, fid):
        for f in self.factions:
            if f.id == fid:
                return f
        return None

    def layer(self, name, create=False):
        m = self.maps.get(name)
        if m is None and create:
            m = self.maps[name] = MapLayer(name)
        return m

    def placements(self, targetname):
        """[(map, Ref)] of an NPC on all maps."""
        out = []
        for mname, layer in sorted(self.maps.items()):
            for r in layer.refs:
                if r.type == "npc" and r.npc.lower() == targetname.lower():
                    out.append((mname, r))
        return out

    @property
    def languages(self):
        return self.settings.languages

    @property
    def base_language(self):
        return self.settings.base_language

    # --- (de)serialization -------------------------------------------------
    def to_dict(self):
        return {
            "format": FORMAT, "version": VERSION,
            "settings": self.settings.to_dict(),
            "npcs": [n.to_dict() for n in self.npcs],
            "quests": [q.to_dict() for q in self.quests],
            "factions": [f.to_dict() for f in self.factions],
            "maps": {k: v.to_dict() for k, v in sorted(self.maps.items())},
            "managed_files": sorted(set(self.managed_files)),
            "view": self.view,
        }

    @staticmethod
    def from_dict(d):
        if d.get("format") != FORMAT:
            raise ValueError("Это не проект редактора диалогов HL2RPM")
        p = Project()
        p.settings = Settings.from_dict(d.get("settings"))
        p.npcs = [NPC.from_dict(x) for x in d.get("npcs", [])]
        p.quests = [Quest.from_dict(x) for x in d.get("quests", [])]
        p.factions = [Faction.from_dict(x) for x in d.get("factions", [])]
        p.maps = {k: MapLayer.from_dict(v) for k, v in (d.get("maps") or {}).items()}
        p.managed_files = list(d.get("managed_files", []))
        p.view = d.get("view", {})
        return p

    def to_json(self):
        return json.dumps(self.to_dict(), ensure_ascii=False, indent=1)

    @staticmethod
    def load(path):
        with open(path, "r", encoding="utf-8") as f:
            p = Project.from_dict(json.load(f))
        p.path = path
        return p

    def save(self, path=None):
        path = path or self.path
        data = self.to_json()
        tmp = path + ".tmp"
        with open(tmp, "w", encoding="utf-8") as f:
            f.write(data)
        # keep the previous version
        if os.path.exists(path):
            bak = path + ".bak"
            try:
                if os.path.exists(bak):
                    os.remove(bak)
                os.replace(path, bak)
            except OSError:
                pass
        os.replace(tmp, path)
        self.path = path

    def clone(self):
        return copy.deepcopy(self)

    # --- paths -------------------------------------------------------------
    def game_path(self, rel):
        """Absolute path of a game-relative or absolute path."""
        if not rel:
            return ""
        if os.path.isabs(rel):
            return os.path.normpath(rel)
        return os.path.normpath(os.path.join(self.settings.game_dir, rel))

    def find_content(self, rel):
        """A game-relative path looked up in game_dir, then mod_dir and its testmaps."""
        if not rel:
            return ""
        if os.path.isabs(rel):
            return rel if os.path.exists(rel) else ""
        roots = [self.settings.game_dir, self.settings.mod_dir, os.path.join(self.settings.mod_dir, "testmaps")]
        for r in roots:
            if r:
                p = os.path.normpath(os.path.join(r, rel))
                if os.path.exists(p):
                    return p
        return ""

    def rel_to_game(self, path):
        """Game-relative form of an absolute path if it lies inside game_dir."""
        if not path:
            return ""
        try:
            rel = os.path.relpath(os.path.abspath(path), os.path.abspath(self.settings.game_dir))
        except ValueError:
            return os.path.abspath(path)
        if rel.startswith(".."):
            return os.path.abspath(path)
        return rel.replace("\\", "/")


# --- canonical media locations ---------------------------------------------
TARGETNAME_RE = re.compile(r"^[A-Za-z0-9_\-\.]+$")


def media_key_node(nid):
    return str(nid)


def media_key_after(nid, oi):
    return "%d_%d_after" % (nid, oi)


def canonical_wav(npc, lang, key):
    return "sound/dialogs/%s/%s/%s.wav" % (npc, lang, key)


def canonical_vcd(npc, lang, key):
    return "scenes/dialogs/%s/%s/%s.vcd" % (npc, lang, key)


def text_for(texts, lang, base):
    """(text, is_fallback)"""
    t = texts.get(lang, "")
    if t:
        return t, False
    return texts.get(base, ""), True


def iter_voiced_texts(npc):
    """Yield (key, texts, voices, where) for everything of an NPC that can carry a voice."""
    if npc.returning.enabled() or npc.returning.voice:
        yield "returning", npc.returning.line, npc.returning.voice, ("returning",)
    for n in npc.nodes:
        yield media_key_node(n.id), n.line, n.voice, ("node", n.id)
        for oi, o in enumerate(n.options):
            if o.complete and (o.after_line or o.after_voice):
                yield media_key_after(n.id, oi), o.after_line, o.after_voice, ("option", n.id, oi)
