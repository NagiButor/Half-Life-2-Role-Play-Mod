#!/usr/bin/env python3
"""Source 2013 BSP inspection and entity-lump patching for HL2RPM.

    python bsp.py info    <map.bsp>                      version, lumps, counts, skyname
    python bsp.py ents    <map.bsp> [--class C] [--name N] [--keys]
    python bsp.py lights  <map.bsp> [--hdr]              compiled world lights (LUMP_WORLDLIGHTS)
    python bsp.py export  <map.bsp> <out.txt>            entity lump as text
    python bsp.py import  <map.bsp> <in.txt>             replace the entity lump (backup first)
    python bsp.py setkv   <map.bsp> --class C [--name N] key=value ...   edit keys in place (backup first)

Entity edits without a recompile are fine for keyvalues/outputs of point
entities. Never add/remove brush entities or lights this way (brush models
and lightmaps come from the compile): recompile with compile_map.ps1.
The modified lump is appended at the end of the file and the header is
repointed, so every other lump (game lump offsets included) stays valid.
"""
import datetime
import os
import re
import shutil
import struct
import sys

HEADER_LUMPS = 64
LUMP_ENTITIES = 0
LUMP_WORLDLIGHTS = 15
LUMP_WORLDLIGHTS_HDR = 54
LUMP_GAME_LUMP = 35
LUMP_PAKFILE = 40

EMIT_TYPES = {0: "surface", 1: "point", 2: "spotlight", 3: "skylight", 4: "quakelight", 5: "skyambient"}


class BSP:
    def __init__(self, path):
        self.path = path
        with open(path, "rb") as f:
            self.data = bytearray(f.read())
        ident, self.version = struct.unpack_from("<4si", self.data, 0)
        if ident != b"VBSP":
            raise ValueError("not a VBSP file")
        self.lumps = []
        for i in range(HEADER_LUMPS):
            ofs, ln, ver, cc = struct.unpack_from("<iii4s", self.data, 8 + i * 16)
            self.lumps.append([ofs, ln, ver, cc])
        self.revision = struct.unpack_from("<i", self.data, 8 + HEADER_LUMPS * 16)[0]

    def lump(self, i):
        ofs, ln = self.lumps[i][0], self.lumps[i][1]
        return bytes(self.data[ofs:ofs + ln])

    # entities -------------------------------------------------------------
    def entity_text(self):
        return self.lump(LUMP_ENTITIES).rstrip(b"\0").decode("utf-8", errors="surrogateescape")

    def entities(self):
        return parse_entities(self.entity_text())

    def set_entity_text(self, text):
        raw = text.encode("utf-8", errors="surrogateescape")
        if not raw.endswith(b"\0"):
            raw += b"\0"
        # append at the end (4-byte aligned) and repoint lump 0
        pad = (-len(self.data)) % 4
        self.data += b"\0" * pad
        ofs = len(self.data)
        self.data += raw
        self.lumps[LUMP_ENTITIES][0] = ofs
        self.lumps[LUMP_ENTITIES][1] = len(raw)
        struct.pack_into("<iii4s", self.data, 8 + LUMP_ENTITIES * 16, *self.lumps[LUMP_ENTITIES])

    def save(self, backup=True):
        if backup:
            stamp = datetime.datetime.now().strftime("%Y%m%d_%H%M%S")
            shutil.copy2(self.path, "%s.bak_%s" % (self.path, stamp))
        tmp = self.path + ".tmp_write"
        with open(tmp, "wb") as f:
            f.write(self.data)
        os.replace(tmp, self.path)

    # world lights -----------------------------------------------------------
    def worldlights(self, hdr=False):
        raw = self.lump(LUMP_WORLDLIGHTS_HDR if hdr else LUMP_WORLDLIGHTS)
        size = 88
        res = []
        for i in range(len(raw) // size):
            v = struct.unpack_from("<3f3f3fiiifffffffii", raw, i * size)
            res.append({
                "origin": v[0:3], "intensity": v[3:6], "normal": v[6:9], "cluster": v[9],
                "type": EMIT_TYPES.get(v[10], v[10]), "style": v[11], "stopdot": v[12], "stopdot2": v[13],
                "exponent": v[14], "radius": v[15], "constant": v[16], "linear": v[17], "quadratic": v[18],
                "flags": v[19], "texinfo": v[20],
            })
        return res


_ENT_TOKEN = re.compile(r'"((?:[^"\\]|\\.)*)"|(\{)|(\})')


def parse_entities(text):
    ents = []
    cur = None
    key = None
    for m in _ENT_TOKEN.finditer(text):
        s, lb, rb = m.groups()
        if lb:
            cur = []
        elif rb:
            if cur is not None:
                ents.append(cur)
            cur = None
            key = None
        elif cur is not None:
            if key is None:
                key = s
            else:
                cur.append([key, s])
                key = None
    return ents


def dump_entities(ents):
    out = []
    for e in ents:
        out.append("{\n")
        for k, v in e:
            out.append('"%s" "%s"\n' % (k, v))
        out.append("}\n")
    return "".join(out)


def ent_get(e, key, default=""):
    for k, v in e:
        if k.lower() == key.lower():
            return v
    return default


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

    b = BSP(path)

    if cmd == "info":
        ents = b.entities()
        classes = {}
        for e in ents:
            c = ent_get(e, "classname")
            classes[c] = classes.get(c, 0) + 1
        world = next((e for e in ents if ent_get(e, "classname") == "worldspawn"), [])
        print("file        :", path)
        print("version     :", b.version, " revision", b.revision)
        print("skyname     :", ent_get(world, "skyname"))
        print("entities    :", len(ents))
        print("worldlights : LDR %d, HDR %d" % (len(b.worldlights(False)), len(b.worldlights(True))))
        print("pakfile     : %d bytes" % b.lumps[LUMP_PAKFILE][1])
        for c in sorted(classes, key=lambda c: (-classes[c], c))[:40]:
            print("  %5d  %s" % (classes[c], c))
        return 0

    if cmd == "ents":
        cls = opt("--class")
        name = opt("--name")
        keys = flag("--keys")
        for e in b.entities():
            c = ent_get(e, "classname")
            if cls and c.lower() != cls.lower():
                continue
            if name and ent_get(e, "targetname").lower() != name.lower():
                continue
            print("%-28s %-24s %s" % (c, ent_get(e, "targetname"), ent_get(e, "origin")))
            if keys:
                for k, v in e:
                    if k not in ("classname", "targetname", "origin"):
                        print("        %s = %s" % (k, v.replace("\x1b", ",")))
        return 0

    if cmd == "lights":
        hdr = flag("--hdr")
        for i, l in enumerate(b.worldlights(hdr)):
            print("%4d %-10s style %3d  origin (%7.0f %7.0f %7.0f)  I (%.2f %.2f %.2f)  c/l/q %.2f/%.4f/%.6f  r %.0f" % (
                i, l["type"], l["style"], *l["origin"], *l["intensity"], l["constant"], l["linear"], l["quadratic"], l["radius"]))
        return 0

    if cmd == "export":
        with open(rest[0], "w", encoding="utf-8", errors="surrogateescape", newline="\n") as f:
            f.write(b.entity_text())
        print("wrote", rest[0])
        return 0

    if cmd == "import":
        with open(rest[0], "r", encoding="utf-8", errors="surrogateescape") as f:
            text = f.read()
        n = len(parse_entities(text))
        if n == 0:
            raise SystemExit("no entities parsed from %s" % rest[0])
        b.set_entity_text(text)
        b.save()
        print("imported %d entities into %s (backup kept)" % (n, path))
        return 0

    if cmd == "setkv":
        cls = opt("--class")
        name = opt("--name")
        if not cls and not name:
            raise SystemExit("pass --class and/or --name")
        ents = b.entities()
        hits = 0
        for e in ents:
            if cls and ent_get(e, "classname").lower() != cls.lower():
                continue
            if name and ent_get(e, "targetname").lower() != name.lower():
                continue
            hits += 1
            for kv in rest:
                k, v = kv.split("=", 1)
                for p in e:
                    if p[0].lower() == k.lower():
                        p[1] = v
                        break
                else:
                    e.append([k, v])
        if not hits:
            raise SystemExit("no matching entity")
        b.set_entity_text(dump_entities(ents))
        b.save()
        print("changed %d entities (backup kept)" % hits)
        return 0

    print(__doc__)
    return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
