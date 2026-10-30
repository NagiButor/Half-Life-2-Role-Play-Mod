"""Read entities from Hammer maps (.vmf): which NPCs (targetname, class, Hammer
display name, model) stand on which map. Used for autocompletion, checks
("NPC is not on any map") and the "Add NPC from map" picker."""
import os
import re

_KV_RE = re.compile(r'^\s*"([^"]*)"\s+"([^"]*)"\s*$')
KEEP = ("classname", "targetname", "loot_displayname", "model", "loot_items", "ftag")


class MapEntity:
    __slots__ = ("map", "classname", "targetname", "display", "model", "loot_items", "ftag")

    def __init__(self, mapname, keys):
        self.map = mapname
        self.classname = keys.get("classname", "")
        self.targetname = keys.get("targetname", "")
        self.display = keys.get("loot_displayname", "")
        self.model = keys.get("model", "")
        self.loot_items = keys.get("loot_items", "")
        self.ftag = keys.get("ftag", "")

    def is_npc(self):
        return self.classname.startswith(("npc_", "monster_", "generic_actor", "cycler_actor"))


def scan_vmf(path):
    mapname = os.path.splitext(os.path.basename(path))[0]
    out = []
    depth = 0
    in_entity = False
    keys = {}
    pending_block = ""
    try:
        f = open(path, "r", encoding="utf-8", errors="replace")
    except OSError:
        return out
    with f:
        for line in f:
            s = line.strip()
            if s == "{":
                depth += 1
                if depth == 1 and pending_block == "entity":
                    in_entity = True
                    keys = {}
                continue
            if s == "}":
                if depth == 1 and in_entity:
                    if keys.get("targetname"):
                        out.append(MapEntity(mapname, keys))
                    in_entity = False
                depth -= 1
                continue
            if depth == 0:
                pending_block = s
                continue
            if in_entity and depth == 1 and s.startswith('"'):
                m = _KV_RE.match(s)
                if m and m.group(1) in KEEP:
                    keys[m.group(1)] = m.group(2)
    return out


class MapIndex:
    def __init__(self):
        self.entities = []
        self.maps = []
        self.by_name = {}
        self._cache = {}   # path -> (mtime, entities)

    def scan(self, dirs, progress=None):
        files = []
        for d in dirs:
            if not d or not os.path.isdir(d):
                continue
            for root, _dirs, names in os.walk(d):
                if "autosave" in root.lower():
                    continue
                for n in names:
                    if n.lower().endswith(".vmf") and ".bak" not in n.lower():
                        files.append(os.path.join(root, n))
        ents = []
        maps = []
        for i, fpath in enumerate(sorted(files)):
            try:
                mt = os.path.getmtime(fpath)
            except OSError:
                continue
            c = self._cache.get(fpath)
            if c and c[0] == mt:
                e = c[1]
            else:
                e = scan_vmf(fpath)
                self._cache[fpath] = (mt, e)
            ents.extend(e)
            maps.append(os.path.splitext(os.path.basename(fpath))[0])
            if progress:
                progress(i + 1, len(files))
        self.entities = ents
        self.maps = maps
        self.by_name = {}
        for e in ents:
            self.by_name.setdefault(e.targetname.lower(), []).append(e)

    def find(self, targetname):
        return self.by_name.get((targetname or "").lower(), [])

    def npcs(self):
        return [e for e in self.entities if e.is_npc()]

    def targetnames(self, npc_only=False, classes=None):
        names = set()
        for e in self.entities:
            if npc_only and not e.is_npc():
                continue
            if classes and not e.classname.startswith(classes):
                continue
            names.add(e.targetname)
        return sorted(names, key=str.lower)
