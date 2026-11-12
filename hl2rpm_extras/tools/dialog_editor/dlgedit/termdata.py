"""Terminals of the project (M6.5): computers and Combine consoles the player uses like in
Fallout 4 - records to read, menus, commands (doors, force fields, turrets, any action),
a lock hacked with the word game. Game file: scripts/rpg/terminals.txt
(game/server/hl2rpm_terminal.cpp); on a map - a layer object of type "terminal" with
"terminal" = id."""
from . import kv
from . import logic

STYLES = [("pc", "Компьютер города (зелёный ЭЛТ)"), ("combine", "Консоль Альянса (бирюзовая)")]
STYLE_NAMES = dict(STYLES)
# default models and the stock models whose screen the game knows (others: give the screen rect)
MODELS = [
    ("models/props_lab/monitor01a.mdl", "pc", "монитор ЭЛТ"),
    ("models/props_combine/combine_intmonitor001.mdl", "combine", "экран Альянса (большой)"),
    ("models/props_combine/combine_smallmonitor001.mdl", "combine", "экран Альянса (малый)"),
    ("models/props_combine/combine_interface001.mdl", "combine", "пульт Альянса"),
]
DEFAULT_MODEL = {"pc": MODELS[0][0], "combine": MODELS[1][0]}


def _texts(d):
    return {k: v for k, v in (d or {}).items() if v}


class TermEntry:
    """A line of the terminal menu: a record (text), a command (actions without a text) or a submenu (entries)."""

    def __init__(self, key="1"):
        self.key = key
        self.title = {}
        self.text = {}
        self.result = {}
        self.conditions = []
        self.actions = []
        self.once = False
        self.entries = []

    def kind(self):
        if self.entries:
            return "menu"
        if any(self.text.values()):
            return "text"
        return "action"

    def to_dict(self):
        d = {"key": self.key}
        for n in ("title", "text", "result"):
            t = _texts(getattr(self, n))
            if t:
                d[n] = t
        if self.conditions:
            d["conditions"] = [dict(c) for c in self.conditions]
        if self.actions:
            d["actions"] = [dict(a) for a in self.actions]
        if self.once:
            d["once"] = True
        if self.entries:
            d["entries"] = [e.to_dict() for e in self.entries]
        return d

    @classmethod
    def from_dict(cls, d):
        e = cls(str(d.get("key", "1")))
        for n in ("title", "text", "result"):
            setattr(e, n, dict(d.get(n) or {}))
        e.conditions = [dict(c) for c in d.get("conditions", [])]
        e.actions = [dict(a) for a in d.get("actions", [])]
        e.once = bool(d.get("once"))
        e.entries = [cls.from_dict(x) for x in d.get("entries", [])]
        return e

    def label(self, lang, base):
        return self.title.get(lang) or self.title.get(base) or "(без названия)"


class TerminalDef:
    def __init__(self, id="terminal"):
        self.id = id
        self.note = ""
        self.style = "pc"
        self.title = {}
        self.header = {}
        self.lock_level = 0
        self.lock_skill = "science"
        self.password_item = ""
        self.entries = []

    def to_dict(self):
        d = {"id": self.id, "style": self.style}
        if self.note:
            d["note"] = self.note
        for n in ("title", "header"):
            t = _texts(getattr(self, n))
            if t:
                d[n] = t
        if self.lock_level:
            d["lock_level"] = self.lock_level
        if self.lock_skill != "science":
            d["lock_skill"] = self.lock_skill
        if self.password_item:
            d["password_item"] = self.password_item
        d["entries"] = [e.to_dict() for e in self.entries]
        return d

    @classmethod
    def from_dict(cls, d):
        t = cls(str(d.get("id", "terminal")))
        t.note = d.get("note", "")
        t.style = d.get("style", "pc")
        t.title = dict(d.get("title") or {})
        t.header = dict(d.get("header") or {})
        t.lock_level = int(d.get("lock_level", 0))
        t.lock_skill = d.get("lock_skill", "science")
        t.password_item = d.get("password_item", "")
        t.entries = [TermEntry.from_dict(x) for x in d.get("entries", [])]
        return t

    def label(self, lang, base):
        return self.title.get(lang) or self.title.get(base) or self.id

    def all_entries(self):
        out = []

        def walk(lst):
            for e in lst:
                out.append(e)
                walk(e.entries)
        walk(self.entries)
        return out


def new_key(entries):
    used = {e.key for e in entries}
    i = 1
    while str(i) in used:
        i += 1
    return str(i)


# --- game files ---------------------------------------------------------------------------
def _write_entries(parent, entries, project):
    lst = parent.add("entries")
    for e in entries:
        b = lst.add(e.key)
        logic._add_texts(b, "title", e.title, project) if any(e.title.values()) else None
        if e.entries:
            _write_entries(b, e.entries, project)
        elif any(e.text.values()):
            logic._add_texts(b, "text", e.text, project)
        if any(e.result.values()):
            logic._add_texts(b, "result", e.result, project)
        if e.once:
            b.add("once", "1")
        logic.write_conds(b, "conditions", e.conditions, project)
        logic.write_actions(b, "actions", e.actions, project)


def terminals_kv(project):
    root = kv.KV("Terminals")
    for t in getattr(project, "terminals", []):
        if not t.id:
            continue
        b = root.add(t.id, None, t.note)
        b.add("style", t.style)
        if any(t.title.values()):
            logic._add_texts(b, "title", t.title, project)
        if any(t.header.values()):
            logic._add_texts(b, "header", t.header, project)
        if t.lock_level:
            b.add("lock_level", str(int(t.lock_level)))
            b.add("lock_skill", t.lock_skill or "science")
        if t.password_item:
            b.add("password_item", t.password_item)
        _write_entries(b, t.entries, project)
    return root


def _texts_of(blk, key, base):
    out = {}
    for c in blk.values():
        n = c.name.lower()
        if n == key:
            out.setdefault(base, c.value)
        elif n.startswith(key + "_") and n[len(key) + 1:].isalpha():
            out[n[len(key) + 1:]] = c.value
    return out


def _read_entries(blk, base, langs):
    out = []
    lst = blk.block("entries") if blk else None
    for b in (lst.blocks() if lst else []):
        e = TermEntry(b.name)
        e.title = _texts_of(b, "title", base)
        e.text = _texts_of(b, "text", base)
        e.result = _texts_of(b, "result", base)
        e.once = (b.get("once") or "0") not in ("0", "")
        e.conditions = logic.read_conds(b.block("conditions"), langs) if b.block("conditions") else []
        e.actions = logic.read_actions(b.block("actions"), langs) if b.block("actions") else []
        e.entries = _read_entries(b, base, langs)
        out.append(e)
    return out


def import_terminals(project, path, log):
    root = kv.parse_file(path)
    top = root.block("Terminals") or (root.blocks()[0] if root.blocks() else None)
    if not top:
        return
    base = project.base_language
    langs = tuple(project.languages) if getattr(project, "languages", None) else (base,)
    out = []
    for b in top.blocks():
        t = TerminalDef(b.name)
        t.style = b.get("style") or "pc"
        t.title = _texts_of(b, "title", base)
        t.header = _texts_of(b, "header", base)
        try:
            t.lock_level = int(float(b.get("lock_level") or 0))
        except ValueError:
            t.lock_level = 0
        t.lock_skill = b.get("lock_skill") or "science"
        t.password_item = b.get("password_item") or ""
        t.entries = _read_entries(b, base, langs)
        out.append(t)
    project.terminals = out
    log.append("терминалов: %d" % len(out))


def check(project, out):
    """Problems for F7: (text, target) - unknown ids, empty entries."""
    ids = {t.id for t in getattr(project, "terminals", [])}
    items = {i.id for i in project.items}
    for t in getattr(project, "terminals", []):
        if t.password_item and items and t.password_item not in items:
            out.append(("Терминал %s: пароль — неизвестный предмет %s" % (t.id, t.password_item), ("terminal", t.id)))
        if not t.entries:
            out.append(("Терминал %s: нет ни одной записи" % t.id, ("terminal", t.id)))
        for e in t.all_entries():
            if not any(e.title.values()):
                out.append(("Терминал %s: запись %s без названия" % (t.id, e.key), ("terminal", t.id)))
    for mname, layer in getattr(project, "maps", {}).items():
        for r in getattr(layer, "refs", []):
            if getattr(r, "type", "") == "terminal":
                tid = getattr(r, "terminal", "")
                if tid and tid not in ids:
                    out.append(("Карта %s: терминал ссылается на неизвестный «%s»" % (mname, tid), None))
    return out
