#!/usr/bin/env python3
"""VMF (Hammer map source) reader/writer and small editing CLI for HL2RPM.

Library use (from another script):
    import sys; sys.path.insert(0, r"<this folder>")
    from vmf import VMF
    m = VMF.load(r"...\\mapsrc\\test_deferred.vmf")
    for e in m.entities(classname="light"):
        print(e.id, e.get("targetname"), e.get("origin"))
    lamp = m.add_entity("light", (0, 0, 128), _light="255 230 200 300", targetname="lamp1")
    m.add_box((-64, -64, 0), (64, 64, 16), "DEV/DEV_MEASUREGENERIC01B")          # world brush
    m.save()          # writes <map>.vmf.bak_<time> first

CLI (python vmf.py <command> <map.vmf> ...):
    info   <vmf>                          counts, bounds, skyname, light entities
    ents   <vmf> [--class C] [--name N] [--keys]
    get    <vmf> --id N                   print one entity/solid block
    set    <vmf> --id N key=value ...     change keyvalues ("key=" deletes the key)
    add    <vmf> <classname> x y z [key=value ...]
    output <vmf> --id N "OnTrigger" "target,Input,param,delay,times"
    delete <vmf> --id N
    box    <vmf> x1 y1 z1 x2 y2 z2 <material> [--entity-id N]
    check  <vmf>                          parse + write round trip must be byte identical

The file is kept byte-exact for everything that is not edited (utf-8 with
surrogateescape, original line endings). Hammer must not have the map open
while it is edited here: its next save would overwrite these changes.
"""
import datetime
import os
import re
import shutil
import sys

# --------------------------------------------------------------------------
# tree
# --------------------------------------------------------------------------


class Node:
    """A VMF block: name { "key" "value" ... child blocks }.

    items keeps keyvalues ([key, value] lists) and child Nodes in file order:
    Hammer++ interleaves them (side: plane, vertices_plus {..}, material ...).
    Keys may repeat (connections)."""

    __slots__ = ("name", "items", "parent")

    def __init__(self, name, props=None, children=None):
        self.name = name
        self.items = []
        self.parent = None
        for p in props or []:
            self.items.append([p[0], p[1]])
        for c in children or []:
            self.add_child(c)

    @property
    def props(self):
        return [it for it in self.items if not isinstance(it, Node)]

    @property
    def children(self):
        return [it for it in self.items if isinstance(it, Node)]

    # keyvalues ---------------------------------------------------------
    def get(self, key, default=None):
        kl = key.lower()
        for it in self.items:
            if not isinstance(it, Node) and it[0].lower() == kl:
                return it[1]
        return default

    def all(self, key):
        kl = key.lower()
        return [it[1] for it in self.items if not isinstance(it, Node) and it[0].lower() == kl]

    def set(self, key, value):
        """Replace the first occurrence (keeps position) or add after the last keyvalue."""
        kl = key.lower()
        for it in self.items:
            if not isinstance(it, Node) and it[0].lower() == kl:
                it[1] = str(value)
                return
        self.add(key, value)

    def add(self, key, value):
        last = -1
        for i, it in enumerate(self.items):
            if not isinstance(it, Node):
                last = i
        self.items.insert(last + 1, [key, str(value)])

    def delete(self, key):
        kl = key.lower()
        self.items = [it for it in self.items if isinstance(it, Node) or it[0].lower() != kl]

    # children ----------------------------------------------------------
    def child(self, name):
        nl = name.lower()
        for c in self.children:
            if c.name.lower() == nl:
                return c
        return None

    def children_named(self, name):
        nl = name.lower()
        return [c for c in self.children if c.name.lower() == nl]

    def add_child(self, node, index=None):
        """index counts child blocks only (None = append at the end)."""
        node.parent = self
        if index is None:
            self.items.append(node)
            return node
        n = 0
        for i, it in enumerate(self.items):
            if isinstance(it, Node):
                if n == index:
                    self.items.insert(i, node)
                    return node
                n += 1
        self.items.append(node)
        return node

    def remove_child(self, node):
        self.items = [it for it in self.items if it is not node]
        node.parent = None

    def walk(self):
        yield self
        for c in self.children:
            yield from c.walk()

    # convenience -------------------------------------------------------
    @property
    def id(self):
        v = self.get("id")
        return int(v) if v is not None and v.lstrip("-").isdigit() else None

    @property
    def classname(self):
        return self.get("classname", "")

    def origin(self):
        v = self.get("origin")
        if not v:
            return None
        try:
            return tuple(float(x) for x in v.split()[:3])
        except ValueError:
            return None

    def __repr__(self):
        return "<Node %s id=%s class=%s>" % (self.name, self.get("id"), self.get("classname"))


# --------------------------------------------------------------------------
# parse / dump
# --------------------------------------------------------------------------

_TOKEN = re.compile(r'"([^"]*)"|(\{)|(\})|([^\s{}"]+)')


def parse(text):
    root = Node("<root>")
    stack = [root]
    pending_key = None
    pending_name = None
    for m in _TOKEN.finditer(text):
        quoted, lbrace, rbrace, bare = m.groups()
        if lbrace:
            name = pending_name if pending_name is not None else (pending_key or "")
            if pending_key is not None and pending_name is None:
                # "name" { ... } with a quoted block name
                pass
            node = Node(name)
            stack[-1].add_child(node)
            stack.append(node)
            pending_key = None
            pending_name = None
        elif rbrace:
            if pending_key is not None:
                raise ValueError("dangling key %r before '}'" % pending_key)
            if pending_name is not None:
                raise ValueError("dangling word %r before '}'" % pending_name)
            if len(stack) == 1:
                raise ValueError("unbalanced '}'")
            stack.pop()
        else:
            tok = quoted if quoted is not None else bare
            if bare is not None and pending_key is None and pending_name is None:
                # bare word: a block name (world, entity, solid, side, ...)
                pending_name = bare
            elif pending_key is None and pending_name is None:
                pending_key = tok
            elif pending_key is not None:
                stack[-1].items.append([pending_key, tok])
                pending_key = None
            else:
                raise ValueError("unexpected token %r after block name %r" % (tok, pending_name))
    if len(stack) != 1:
        raise ValueError("unexpected end of file inside block %r" % stack[-1].name)
    if pending_key is not None or pending_name is not None:
        raise ValueError("unexpected end of file")
    return root


def dump(root, newline="\r\n"):
    out = []

    def emit(node, depth):
        ind = "\t" * depth
        out.append(ind + node.name + newline)
        out.append(ind + "{" + newline)
        for it in node.items:
            if isinstance(it, Node):
                emit(it, depth + 1)
            else:
                out.append('%s\t"%s" "%s"%s' % (ind, it[0], it[1], newline))
        out.append(ind + "}" + newline)

    for c in root.children:
        emit(c, 0)
    return "".join(out)


# --------------------------------------------------------------------------
# map
# --------------------------------------------------------------------------


class VMF:
    def __init__(self, root, path=None, newline="\r\n"):
        self.root = root
        self.path = path
        self.newline = newline
        self._next_id = None
        self._next_side_id = None

    @classmethod
    def load(cls, path):
        with open(path, "rb") as f:
            data = f.read()
        text = data.decode("utf-8", errors="surrogateescape")
        newline = "\r\n" if "\r\n" in text else "\n"
        return cls(parse(text), path, newline)

    def text(self):
        return dump(self.root, self.newline)

    def save(self, path=None, backup=True):
        path = path or self.path
        if backup and os.path.exists(path):
            stamp = datetime.datetime.now().strftime("%Y%m%d_%H%M%S")
            shutil.copy2(path, "%s.bak_%s" % (path, stamp))
        data = self.text().encode("utf-8", errors="surrogateescape")
        tmp = path + ".tmp_write"
        with open(tmp, "wb") as f:
            f.write(data)
        os.replace(tmp, path)
        return path

    # structure --------------------------------------------------------
    @property
    def world(self):
        return self.root.child("world")

    def entities(self, classname=None, targetname=None):
        res = []
        for e in self.root.children_named("entity"):
            if classname and e.get("classname", "").lower() != classname.lower():
                continue
            if targetname and (e.get("targetname") or "").lower() != targetname.lower():
                continue
            res.append(e)
        return res

    def find_id(self, id_):
        """Entity, world solid or entity solid with this id (sides excluded)."""
        for n in self.root.walk():
            if n.name.lower() in ("entity", "solid", "world") and n.get("id") == str(id_):
                return n
        return None

    def solids(self):
        """(owner, solid): owner is the world or a brush entity."""
        res = []
        if self.world:
            res += [(self.world, s) for s in self.world.children_named("solid")]
        for e in self.entities():
            res += [(e, s) for s in e.children_named("solid")]
        return res

    # ids ---------------------------------------------------------------
    def _scan_ids(self):
        mx, mside = 1, 1
        for n in self.root.walk():
            v = n.get("id")
            if v is None or not v.lstrip("-").isdigit():
                continue
            if n.name.lower() == "side":
                mside = max(mside, int(v))
            else:
                mx = max(mx, int(v))
        self._next_id = mx + 1
        self._next_side_id = mside + 1

    def new_id(self):
        if self._next_id is None:
            self._scan_ids()
        self._next_id += 1
        return self._next_id - 1

    def new_side_id(self):
        if self._next_side_id is None:
            self._scan_ids()
        self._next_side_id += 1
        return self._next_side_id - 1

    # editing -----------------------------------------------------------
    def add_entity(self, classname, origin=None, **keys):
        """Point entity. Keys with a leading underscore work (_light, _cone)."""
        e = Node("entity")
        e.add("id", self.new_id())
        e.add("classname", classname)
        for k, v in keys.items():
            e.add(k, v)
        if origin is not None:
            e.set("origin", "%g %g %g" % tuple(origin))
        ed = Node("editor", [["color", "220 30 220"], ["visgroupshown", "1"],
                             ["visgroupautoshown", "1"], ["logicalpos", "[0 0]"]])
        e.add_child(ed)
        self._insert_entity(e)
        return e

    def add_brush_entity(self, classname, solids, **keys):
        e = Node("entity")
        e.add("id", self.new_id())
        e.add("classname", classname)
        for k, v in keys.items():
            e.add(k, v)
        for s in solids:
            e.add_child(s)
        e.add_child(Node("editor", [["color", "220 30 220"], ["visgroupshown", "1"],
                                    ["visgroupautoshown", "1"], ["logicalpos", "[0 0]"]]))
        self._insert_entity(e)
        return e

    def _insert_entity(self, e):
        # entities go after the last existing entity (before cameras/cordons)
        idx = 0
        for i, c in enumerate(self.root.children):
            if c.name.lower() in ("world", "entity"):
                idx = i + 1
        self.root.add_child(e, idx)

    def delete(self, node):
        node.parent.remove_child(node)

    def add_output(self, entity, output, target, input_, param="", delay=0, times=-1):
        conn = entity.child("connections")
        if conn is None:
            conn = Node("connections")
            # connections come before solids/editor, after keyvalues
            entity.add_child(conn, 0)
        sep = self._output_separator()
        conn.add(output, sep.join([target, input_, param, "%g" % delay, str(times)]))
        return conn

    def _output_separator(self):
        for n in self.root.walk():
            if n.name.lower() == "connections":
                for _, v in n.props:
                    return "\x1b" if "\x1b" in v else ","
        return ","

    def make_box(self, mins, maxs, material, lightmapscale=16, face_materials=None):
        """Axis aligned box solid. face_materials: dict top/bottom/left/right/front/back -> material."""
        x1, y1, z1 = mins
        x2, y2, z2 = maxs
        # plane points are clockwise when seen from outside (Hammer convention)
        faces = {
            "top":    ("(%g %g %g) (%g %g %g) (%g %g %g)" % (x1, y2, z2, x2, y2, z2, x2, y1, z2), "[1 0 0 0] 0.25", "[0 -1 0 0] 0.25"),
            "bottom": ("(%g %g %g) (%g %g %g) (%g %g %g)" % (x1, y1, z1, x2, y1, z1, x2, y2, z1), "[1 0 0 0] 0.25", "[0 -1 0 0] 0.25"),
            "left":   ("(%g %g %g) (%g %g %g) (%g %g %g)" % (x1, y2, z2, x1, y1, z2, x1, y1, z1), "[0 1 0 0] 0.25", "[0 0 -1 0] 0.25"),
            "right":  ("(%g %g %g) (%g %g %g) (%g %g %g)" % (x2, y2, z1, x2, y1, z1, x2, y1, z2), "[0 1 0 0] 0.25", "[0 0 -1 0] 0.25"),
            "back":   ("(%g %g %g) (%g %g %g) (%g %g %g)" % (x2, y2, z2, x1, y2, z2, x1, y2, z1), "[1 0 0 0] 0.25", "[0 0 -1 0] 0.25"),
            "front":  ("(%g %g %g) (%g %g %g) (%g %g %g)" % (x2, y1, z1, x1, y1, z1, x1, y1, z2), "[1 0 0 0] 0.25", "[0 0 -1 0] 0.25"),
        }
        s = Node("solid")
        s.add("id", self.new_id())
        for name in ("top", "bottom", "left", "right", "back", "front"):
            plane, u, v = faces[name]
            side = Node("side")
            side.add("id", self.new_side_id())
            side.add("plane", plane)
            side.add("material", (face_materials or {}).get(name, material))
            side.add("uaxis", u)
            side.add("vaxis", v)
            side.add("rotation", "0")
            side.add("lightmapscale", str(lightmapscale))
            side.add("smoothing_groups", "0")
            s.add_child(side)
        s.add_child(Node("editor", [["color", "0 180 220"], ["visgroupshown", "1"], ["visgroupautoshown", "1"]]))
        return s

    def add_box(self, mins, maxs, material, entity=None, **kw):
        s = self.make_box(mins, maxs, material, **kw)
        owner = entity if entity is not None else self.world
        # solids go before the owner's editor block
        idx = len(owner.children)
        for i, c in enumerate(owner.children):
            if c.name.lower() in ("editor", "hidden", "group"):
                idx = i
                break
        owner.add_child(s, idx)
        return s

    # info --------------------------------------------------------------
    def bounds(self):
        lo = [1e30] * 3
        hi = [-1e30] * 3
        pt = re.compile(r"\(([-\d.eE+]+) ([-\d.eE+]+) ([-\d.eE+]+)\)")
        for _, s in self.solids():
            for side in s.children_named("side"):
                for m in pt.finditer(side.get("plane", "")):
                    for i in range(3):
                        v = float(m.group(i + 1))
                        lo[i] = min(lo[i], v)
                        hi[i] = max(hi[i], v)
        return lo, hi


# --------------------------------------------------------------------------
# CLI
# --------------------------------------------------------------------------

def _kv_args(items):
    res = []
    for it in items:
        if "=" not in it:
            raise SystemExit("expected key=value, got %r" % it)
        k, v = it.split("=", 1)
        res.append((k, v))
    return res


def _print_block(node, depth=0):
    print(dump(Node("<root>", children=[node]), "\n"), end="")


def main(argv):
    if len(argv) < 3:
        print(__doc__)
        return 1
    cmd, path = argv[1], argv[2]
    rest = argv[3:]

    def opt(name, default=None):
        if name in rest:
            i = rest.index(name)
            v = rest[i + 1]
            del rest[i:i + 2]
            return v
        return default

    def flag(name):
        if name in rest:
            rest.remove(name)
            return True
        return False

    m = VMF.load(path)

    if cmd == "check":
        with open(path, "rb") as f:
            orig = f.read()
        new = m.text().encode("utf-8", errors="surrogateescape")
        if orig == new:
            print("round trip OK (%d bytes)" % len(orig))
            return 0
        # whitespace-only differences are harmless for Hammer; show the first difference
        for i, (a, b) in enumerate(zip(orig, new)):
            if a != b:
                print("differs at byte %d: %r vs %r" % (i, orig[max(0, i - 40):i + 40], new[max(0, i - 40):i + 40]))
                break
        else:
            print("length differs: %d vs %d" % (len(orig), len(new)))
        return 2

    if cmd == "info":
        ents = m.entities()
        classes = {}
        for e in ents:
            classes[e.classname] = classes.get(e.classname, 0) + 1
        lo, hi = m.bounds()
        w = m.world
        print("file      :", path)
        print("skyname   :", w.get("skyname") if w else None)
        print("solids    : %d (world %d)" % (len(m.solids()), len(w.children_named("solid")) if w else 0))
        print("entities  :", len(ents))
        print("bounds    : (%.0f %.0f %.0f) .. (%.0f %.0f %.0f)" % tuple(lo + hi))
        for c in sorted(classes, key=lambda c: (-classes[c], c)):
            print("  %5d  %s" % (classes[c], c))
        return 0

    if cmd == "ents":
        cls = opt("--class")
        name = opt("--name")
        keys = flag("--keys")
        for e in m.entities(cls, name):
            print("%6s  %-28s %-24s %s" % (e.get("id"), e.classname, e.get("targetname", ""), e.get("origin", "")))
            if keys:
                for k, v in e.props:
                    if k not in ("id", "classname", "targetname", "origin"):
                        print("          %s = %s" % (k, v))
                conn = e.child("connections")
                if conn:
                    for k, v in conn.props:
                        print("          %s -> %s" % (k, v.replace("\x1b", ",")))
        return 0

    if cmd == "get":
        n = m.find_id(opt("--id"))
        if not n:
            raise SystemExit("id not found")
        _print_block(n)
        return 0

    if cmd == "set":
        n = m.find_id(opt("--id"))
        if not n:
            raise SystemExit("id not found")
        for k, v in _kv_args(rest):
            if v == "":
                n.delete(k)
            else:
                n.set(k, v)
        print("saved", m.save())
        return 0

    if cmd == "add":
        classname = rest[0]
        origin = tuple(float(x) for x in rest[1:4])
        e = m.add_entity(classname, origin, **dict(_kv_args(rest[4:])))
        print("added entity id", e.id, "saved", m.save())
        return 0

    if cmd == "output":
        n = m.find_id(opt("--id"))
        if not n:
            raise SystemExit("id not found")
        output, value = rest[0], rest[1]
        parts = value.split(",")
        while len(parts) < 5:
            parts.append(["", "", "", "0", "-1"][len(parts)])
        m.add_output(n, output, parts[0], parts[1], parts[2], float(parts[3]), int(parts[4]))
        print("saved", m.save())
        return 0

    if cmd == "delete":
        n = m.find_id(opt("--id"))
        if not n:
            raise SystemExit("id not found")
        m.delete(n)
        print("saved", m.save())
        return 0

    if cmd == "box":
        ent_id = opt("--entity-id")
        v = [float(x) for x in rest[0:6]]
        material = rest[6]
        owner = m.find_id(ent_id) if ent_id else None
        s = m.add_box(v[0:3], v[3:6], material, entity=owner)
        print("added solid id", s.id, "saved", m.save())
        return 0

    print(__doc__)
    return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
