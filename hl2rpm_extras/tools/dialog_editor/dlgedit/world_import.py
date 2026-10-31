"""Read the world files of a game folder into a project (scripts/npcs, scripts/world)."""
import glob
import os

from . import kv
from .model import NPC, Node
from .worlddata import Faction, MapLayer, Package, PathPoint, Ref


def _vec(s, n=3):
    try:
        v = [float(x) for x in (s or "").split()[:n]]
    except ValueError:
        v = []
    return (v + [0.0] * n)[:n]


def _loc_texts(blk, key, base):
    out = {}
    for c in blk.values():
        n = c.name.lower()
        if n == key:
            out[base] = c.value
        elif n.startswith(key + "_") and n[len(key) + 1:].isalpha():
            out[n[len(key) + 1:]] = c.value
    return {k: v for k, v in out.items() if v}


def _loot(s):
    out = []
    for part in (s or "").split(";"):
        part = part.strip()
        if not part:
            continue
        if ":" in part:
            a, b = part.split(":", 1)
            try:
                out.append([a.strip(), int(b)])
            except ValueError:
                out.append([a.strip(), 1])
        else:
            out.append([part, 1])
    return out


def import_package(b):
    p = Package(b.get("type", "wander"))
    p.name = b.get("name", p.name)
    p.start = float(b.get("start", "8") or 8)
    p.duration = float(b.get("duration", "24") or 24)
    p.location = b.get("location", "self")
    p.ref = b.get("ref", "")
    p.point = _vec(b.get("point"))
    y = b.get("yaw")
    p.yaw = float(y) if y not in (None, "") else None
    p.radius = int(float(b.get("radius", "0") or 0))
    p.run = b.get("run", "0") not in ("0", "")
    p.anim = b.get("anim", "")
    p.markers = [m for m in (b.get("markers", "") or "").split(";") if m]
    p.path = b.get("path", "")
    p.path_mode = b.get("path_mode", "loop") or "loop"
    p.target = b.get("target", "")
    p.once = b.get("once", "1" if p.type == "talk" else "0") not in ("0", "")
    p.min_use = int(float(b.get("min_use", "30")))
    p.max_use = int(float(b.get("max_use", "90")))
    rs = b.get("require_quest_state", "")
    if ":" in rs:
        p.req_state = rs.split(":", 1)
    rg = b.get("require_quest_stage", "")
    if ":" in rg:
        q, n = rg.split(":", 1)
        try:
            p.req_stage = [q, int(n)]
        except ValueError:
            pass
    return p


def import_world(project, game_dir, log):
    base = project.base_language
    n_npc = n_maps = 0
    for path in sorted(glob.glob(os.path.join(game_dir, "scripts", "npcs", "*.txt"))):
        try:
            root = kv.parse_file(path)
        except Exception as e:
            log.append("%s: %s" % (path, e))
            continue
        blk = root.block("NPC")
        if blk is None:
            continue
        tn = blk.get("targetname") or os.path.splitext(os.path.basename(path))[0]
        npc = project.npc(tn)
        if npc is None:
            npc = NPC(tn)
            n0 = Node(0)
            n0.pos = [260.0, 0.0]
            n0.end = True
            npc.nodes.append(n0)
            npc.start_pos = [30.0, 6.0]
            npc.returning.pos = [-70.0, 160.0]
            project.npcs.append(npc)
            log.append("%s: новый NPC (без диалога)" % tn)
        a = npc.actor
        a.classname = blk.get("classname", a.classname)
        a.model = blk.get("model", "")
        a.skin = int(blk.get("skin", "0") or 0)
        a.weapon = blk.get("weapon", "")
        a.squad = blk.get("squad", "")
        a.faction = blk.get("faction", "")
        a.relation = blk.get("player_relation", "")
        a.health = int(blk.get("health", "0") or 0)
        a.essential = blk.get("essential", "0") not in ("0", "")
        a.commandable = blk.get("commandable", "0") not in ("0", "")
        names = _loc_texts(blk, "display_name", base)
        for k, v in names.items():
            npc.display_name.setdefault(k, v)
        keys = blk.block("keyvalues")
        if keys is not None:
            for c in keys.values():
                k = c.name.lower()
                if k == "citizentype":
                    a.citizen_type = c.value
                elif k == "hate_on_dmg":
                    a.hate_on_dmg = c.value not in ("0", "")
                elif k == "spoil_on_dmg":
                    a.spoil_on_dmg = c.value not in ("0", "")
                elif k == "loot_items":
                    a.loot_items = _loot(c.value)
                elif k == "loot_displayname":
                    npc.display_name.setdefault(base, c.value)
                else:
                    a.keyvalues[c.name] = c.value
        pk = blk.block("packages")
        if pk is not None:
            a.packages = [import_package(b) for b in pk.blocks()]
        n_npc += 1
    fpath = os.path.join(game_dir, "scripts", "world", "factions.txt")
    if os.path.isfile(fpath):
        root = kv.parse_file(fpath)
        fb = root.block("Factions")
        for b in (fb.blocks() if fb else []):
            f = project.faction(b.name) or Faction(b.name)
            if f not in project.factions:
                project.factions.append(f)
            f.name.update(_loc_texts(b, "name", base))
            f.player = b.get("player", f.player)
            rel = b.block("relations")
            if rel is not None:
                for c in rel.values():
                    f.relations[c.name] = c.value
    for path in sorted(glob.glob(os.path.join(game_dir, "scripts", "world", "*.txt"))):
        name = os.path.splitext(os.path.basename(path))[0]
        if name == "factions" or name.endswith("_nav"):
            continue
        root = kv.parse_file(path)
        w = root.block("World")
        refs = w.block("refs") if w else None
        if refs is None:
            continue
        layer = project.layer(name, create=True)
        layer.refs = []
        for b in refs.blocks():
            try:
                rid = int(b.name)
            except ValueError:
                rid = layer.new_id()
            r = Ref(rid, b.get("type", "marker"), _vec(b.get("origin")))
            r.angles = _vec(b.get("angles"))
            r.name = b.get("name", "")
            r.npc = b.get("npc", "")
            r.kind = b.get("kind", "idle")
            r.anim = b.get("anim", "")
            r.owner = b.get("owner", "")
            r.classname = b.get("classname", "")
            r.model = b.get("model", "")
            r.skin = int(b.get("skin", "0") or 0)
            r.physics = b.get("physics", "0") not in ("0", "")
            r.loot_items = _loot(b.get("loot_items", ""))
            r.display_name = _loc_texts(b, "loot_displayname", base)
            pts = b.block("points")
            if pts is not None:
                for pb in pts.blocks():
                    pt = PathPoint(_vec(pb.get("origin")))
                    pt.wait = float(pb.get("wait", "0") or 0)
                    y = pb.get("yaw")
                    pt.yaw = float(y) if y not in (None, "") else None
                    pt.anim = pb.get("anim", "")
                    r.points.append(pt)
            kvb = b.block("keyvalues")
            if kvb is not None:
                r.keyvalues = {c.name: c.value for c in kvb.values()}
            layer.refs.append(r)
        nav = os.path.join(game_dir, "scripts", "world", name + "_nav.txt")
        if os.path.isfile(nav):
            layer.nav = read_nav(nav)
        n_maps += 1
    log.append("мир: NPC %d, карт %d, фракций %d" % (n_npc, n_maps, len(project.factions)))
    return n_npc, n_maps


def read_nav(path):
    out = []
    with open(path, encoding="utf-8", errors="replace") as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("//") or line.startswith("#"):
                continue
            v = line.split()
            if len(v) >= 3:
                try:
                    out.append([float(v[0]), float(v[1]), float(v[2])])
                except ValueError:
                    pass
    return out
