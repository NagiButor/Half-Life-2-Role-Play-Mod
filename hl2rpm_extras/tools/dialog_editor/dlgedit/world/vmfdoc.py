"""The map's Hammer source (.vmf) as far as the Creation Kit edits it: point entities
(props, lights, NPCs, info_*). Brushes and brush entities are never touched.

A VMF of an open-world map is ~10 MB: instead of building the whole tree, the file is
split into its top-level blocks (Hammer writes them at column 0), only point entities are
parsed, and saving replaces just the entity blocks that changed - the rest of the file
stays byte for byte what Hammer wrote."""
import datetime
import os
import re
import shutil

from .bsp import angle_matrix  # noqa: F401  (re-exported for callers)

_KV = re.compile(r'^\t"([^"]*)"\s+"([^"]*)"\s*$')
_ID = re.compile(rb'"id"\s+"(\d+)"')

# classes Hammer keeps as point entities but the kit should not offer to move
_FIXED = ("worldspawn",)


class MapEnt:
    """One point entity of the VMF."""

    def __init__(self, kv, children, text=None, span=None):
        self.kv = kv                  # [[key, value], ...] in file order
        self.children = children      # raw text of child blocks (editor, connections, ...)
        self.text = text              # original block text (None: new entity)
        self.span = span              # (start, end) in the file the block came from
        self.dirty = text is None
        self.deleted = False

    # --- keys
    def get(self, key, default=""):
        k = key.lower()
        for a, b in self.kv:
            if a.lower() == k:
                return b
        return default

    def set(self, key, value):
        value = str(value)
        k = key.lower()
        for it in self.kv:
            if it[0].lower() == k:
                if it[1] != value:
                    it[1] = value
                    self.dirty = True
                return
        self.kv.append([key, value])
        self.dirty = True

    def delete_key(self, key):
        k = key.lower()
        n = len(self.kv)
        self.kv = [it for it in self.kv if it[0].lower() != k]
        if len(self.kv) != n:
            self.dirty = True

    @property
    def id(self):
        try:
            return int(self.get("id", "0"))
        except ValueError:
            return 0

    @property
    def classname(self):
        return self.get("classname")

    @property
    def origin(self):
        return _vec(self.get("origin", "0 0 0"))

    @origin.setter
    def origin(self, v):
        self.set("origin", "%s %s %s" % tuple(_num(x) for x in v[:3]))

    @property
    def angles(self):
        return _vec(self.get("angles", "0 0 0"))

    @angles.setter
    def angles(self, v):
        self.set("angles", "%s %s %s" % tuple(_num(x) for x in v[:3]))

    @property
    def model(self):
        return self.get("model").replace("\\", "/").lower()

    @property
    def outputs(self):
        """[(output, "target,input,param,delay,times")] from the connections block."""
        out = []
        for c in self.children:
            if c.lstrip("\t").startswith("connections"):
                for line in c.splitlines():
                    m = _KV.match(line[1:] if line.startswith("\t\t") else line)
                    if m:
                        out.append((m.group(1), m.group(2)))
        return out

    def is_prop(self):
        return self.classname.startswith("prop_") and self.model.endswith(".mdl")

    def block(self, newline):
        if not self.dirty and self.text is not None:
            return self.text
        out = ["entity" + newline, "{" + newline]
        for k, v in self.kv:
            out.append('\t"%s" "%s"%s' % (k, v.replace('"', "'"), newline))
        for c in self.children:
            out.append(c if c.endswith(newline) else c + newline)
        out.append("}" + newline)
        return "".join(out)

    def clone(self, new_id):
        kv = [[k, v] for k, v in self.kv]
        e = MapEnt(kv, [c for c in self.children if not c.lstrip("\t").startswith("connections")], None, None)
        e.set("id", str(new_id))
        return e


def _num(x):
    x = float(x)
    if abs(x - round(x)) < 1e-4:
        return "%d" % round(x)
    return ("%.3f" % x).rstrip("0").rstrip(".")


def _vec(s):
    try:
        v = [float(x) for x in s.split()[:3]]
    except ValueError:
        v = []
    return (v + [0.0, 0.0, 0.0])[:3]


def _split_top(text):
    """[(name, start, end)] of the top-level blocks (Hammer writes them at column 0)."""
    out = []
    n = len(text)
    pos = 0
    while pos < n:
        # block name: a line at column 0 that is not a brace
        e = text.find("\n", pos)
        if e < 0:
            e = n
        line = text[pos:e].strip()
        if not line or line in ("{", "}"):
            pos = e + 1
            continue
        name = line
        start = pos
        # the block ends at the next "}" at column 0
        close = text.find("\n}", e)
        if close < 0:
            break
        end = text.find("\n", close + 2)
        end = n if end < 0 else end + 1
        out.append((name, start, end))
        pos = end
    return out


def _parse_entity(block):
    """Point entity -> (kv, children) or None for brush entities (they have solids)."""
    lines = block.splitlines(keepends=True)
    kv = []
    children = []
    i = 2                          # skip "entity" and "{"
    while i < len(lines) - 1:
        ln = lines[i]
        if ln.startswith('\t"'):
            m = _KV.match(ln.rstrip("\r\n"))
            if m:
                kv.append([m.group(1), m.group(2)])
            i += 1
            continue
        if ln.startswith("\t") and not ln.startswith("\t\t") and ln.strip() and ln.strip() not in ("{", "}"):
            name = ln.strip()
            if name == "solid":
                return None
            # child block: up to the "\t}" line
            j = i + 1
            while j < len(lines) and lines[j].rstrip("\r\n") != "\t}":
                j += 1
            children.append("".join(lines[i:j + 1]))
            i = j + 1
            continue
        i += 1
    return kv, children


class VMFDoc:
    def __init__(self, path):
        self.path = path
        self.ents = []              # MapEnt (point entities only)
        self.brush_classes = {}     # brush entities: classname -> count (shown, not edited)
        self.newline = "\r\n"
        self._text = ""
        self._stamp = None
        self.max_id = 0
        self.load()

    # --- file
    def _file_stamp(self):
        try:
            st = os.stat(self.path)
            return (st.st_mtime_ns, st.st_size)
        except OSError:
            return None

    def load(self):
        with open(self.path, "rb") as f:
            data = f.read()
        self._stamp = self._file_stamp()
        ids = [int(m.group(1)) for m in _ID.finditer(data)]
        self.max_id = max(ids) if ids else 0
        text = data.decode("utf-8", errors="surrogateescape")
        self.newline = "\r\n" if "\r\n" in text[:4096] else "\n"
        self._text = text
        self.ents = []
        self.brush_classes = {}
        for name, a, b in _split_top(text):
            if name != "entity":
                continue
            blk = text[a:b]
            p = _parse_entity(blk)
            if p is None:
                m = re.search(r'"classname"\s+"([^"]*)"', blk)
                cls = m.group(1) if m else "?"
                self.brush_classes[cls] = self.brush_classes.get(cls, 0) + 1
                continue
            kv, children = p
            e = MapEnt(kv, children, blk, (a, b))
            if e.classname in _FIXED:
                continue
            self.ents.append(e)

    def changed_on_disk(self):
        """True if Hammer (or anyone) saved the file after we read it."""
        return self._file_stamp() != self._stamp

    @property
    def dirty(self):
        return any(e.dirty or e.deleted for e in self.ents)

    def kinds_changed(self):
        """What a rebuild needs: 'full' if static props changed (they live in the BSP
        geometry lumps), 'ents' if only entities (vbsp -onlyents), '' if nothing."""
        full = False
        any_change = False
        for e in self.ents:
            if not (e.dirty or e.deleted):
                continue
            any_change = True
            if e.classname in ("prop_static", "prop_detail", "light", "light_spot", "light_environment", "info_overlay"):
                full = True
        return "full" if full else ("ents" if any_change else "")

    def save(self, backup=True):
        """Write the changed entity blocks into the file. Returns the backup path or ''."""
        text = self._text
        pieces = []
        last = 0
        new_ents = []
        for e in sorted((x for x in self.ents if x.span), key=lambda x: x.span[0]):
            a, b = e.span
            pieces.append(text[last:a])
            if not e.deleted:
                pieces.append(e.block(self.newline))
            last = b
        tail = text[last:]
        new_ents = [e for e in self.ents if e.span is None and not e.deleted]
        if new_ents:
            # after the last entity block (before cameras / cordon), as Hammer does
            ins = "".join(e.block(self.newline) for e in new_ents)
            pieces.append(ins)
        pieces.append(tail)
        out = "".join(pieces)
        bak = ""
        if backup and os.path.exists(self.path):
            stamp = datetime.datetime.now().strftime("%Y%m%d_%H%M%S")
            bak = "%s.bak_%s" % (self.path, stamp)
            shutil.copy2(self.path, bak)
        tmp = self.path + ".tmp_ck"
        with open(tmp, "wb") as f:
            f.write(out.encode("utf-8", errors="surrogateescape"))
        os.replace(tmp, self.path)
        # keep the same MapEnt objects (the editor holds them): new spans, clean state
        self._text = out
        self._stamp = self._file_stamp()
        self.ents = [e for e in self.ents if not e.deleted]
        by_id = {e.id: e for e in self.ents}
        for name, a, b in _split_top(out):
            if name != "entity":
                continue
            blk = out[a:b]
            m = re.search(r'^\t"id"\s+"(\d+)"', blk, re.M)
            e = by_id.get(int(m.group(1))) if m else None
            if e is not None:
                e.span = (a, b)
                e.text = blk
                e.dirty = False
        return bak

    # --- undo: only what differs from the file is kept
    def state(self):
        mod = {}
        new = []
        for e in self.ents:
            if e.span is None:
                if not e.deleted:
                    new.append([[list(x) for x in e.kv], list(e.children)])
            elif e.dirty or e.deleted:
                mod[str(e.id)] = [[list(x) for x in e.kv], e.deleted]
        return {"mod": mod, "new": new, "max_id": self.max_id}

    def restore(self, st):
        mod = st.get("mod", {})
        kept = []
        for e in self.ents:
            if e.span is None:
                continue
            m = mod.get(str(e.id))
            if m is not None:
                e.kv = [list(x) for x in m[0]]
                e.deleted = bool(m[1])
                e.dirty = True
            elif e.dirty or e.deleted:
                kv, children = _parse_entity(e.text)
                e.kv, e.children = kv, children
                e.dirty = False
                e.deleted = False
            kept.append(e)
        for kv, children in st.get("new", []):
            kept.append(MapEnt([list(x) for x in kv], list(children), None, None))
        self.ents = kept
        self.max_id = max(self.max_id, int(st.get("max_id", 0)))

    # --- edits
    def new_id(self):
        self.max_id += 1
        return self.max_id

    def add(self, classname, origin, angles=(0, 0, 0), **keys):
        e = MapEnt([["id", str(self.new_id())], ["classname", classname]], [], None, None)
        e.angles = angles
        for k, v in keys.items():
            e.set(k, v)
        e.origin = origin
        e.children.append(_editor_block(self.newline))
        self.ents.append(e)
        return e

    def duplicate(self, ent):
        e = ent.clone(self.new_id())
        self.ents.append(e)
        return e

    def remove(self, ent):
        if ent.span is None:
            self.ents.remove(ent)
        else:
            ent.deleted = True

    def find(self, eid):
        for e in self.ents:
            if e.id == eid and not e.deleted:
                return e
        return None

    def live(self):
        return [e for e in self.ents if not e.deleted]


def _editor_block(nl):
    return ("\teditor" + nl + "\t{" + nl + '\t\t"color" "220 30 220"' + nl + '\t\t"visgroupshown" "1"' + nl +
            '\t\t"visgroupautoshown" "1"' + nl + '\t\t"logicalpos" "[0 0]"' + nl + "\t}" + nl)


def find_vmf(name, dirs):
    """The VMF a BSP was compiled from: <dir>/<name>.vmf in the given folders."""
    for d in dirs:
        if not d:
            continue
        p = os.path.join(d, name + ".vmf")
        if os.path.isfile(p):
            return p
    return ""
