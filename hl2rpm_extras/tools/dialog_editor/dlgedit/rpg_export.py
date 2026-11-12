"""RPG data -> game files (scripts/rpg/*.txt) and back."""
import os

from . import kv
from .rpgdata import ITEM_FIELDS, ItemDef, LootList
from .world_export import _texts


def _num(v):
    if isinstance(v, float):
        return ("%.3f" % v).rstrip("0").rstrip(".") or "0"
    return str(v)


def items_kv(project):
    base = project.base_language
    root = kv.KV("Items")
    for it in project.items:
        if not it.id:
            continue
        b = root.add(it.id.lower(), None, it.note)
        for name, kind, default in ITEM_FIELDS:
            if name in ("id", "note"):
                continue
            v = getattr(it, name)
            if kind == "text":
                _texts(b, name, v, base)
            elif kind == "bool":
                if v:
                    b.add(name, "1")
            elif v != default:
                b.add(name, _num(v))
    return root


def lootlists_kv(project):
    root = kv.KV("LootLists")
    for ll in project.lootlists:
        if not ll.id:
            continue
        b = root.add(ll.id, None, ll.note)
        if not ll.all:
            b.add("pick", "one")         # one random entry instead of each by its chance
        for i, e in enumerate(ll.entries):
            item = e.get("item", "")
            if not item:
                continue
            x = b.add(str(i))
            x.add("item", item)
            for k, d in (("min", 1), ("max", 1), ("chance", 100), ("level", 0)):
                v = int(e.get(k, d))
                if v != d:
                    x.add(k, str(v))
            x.inline = True
    return root


def _loc(blk, key):
    out = {}
    for c in blk.values():
        n = c.name.lower()
        if n == key:
            out["_base"] = c.value
        elif n.startswith(key + "_") and n[len(key) + 1:].isalpha():
            out[n[len(key) + 1:]] = c.value
    return out


def import_rpg(project, game_dir, log):
    """scripts/rpg/items.txt and lootlists.txt of the game folder into the project (replaces)."""
    base = project.base_language
    p = os.path.join(game_dir, "scripts", "rpg", "items.txt")
    if os.path.isfile(p):
        root = kv.parse_file(p)
        top = root.block("Items") or (root.blocks()[0] if root.blocks() else kv.KV("Items"))
        items = []
        for b in top.blocks():
            it = ItemDef(id=b.name)
            for name, kind, default in ITEM_FIELDS:
                if name == "id":
                    continue
                if kind == "text":
                    t = _loc(b, name)
                    if "_base" in t:
                        t.setdefault(base, t.pop("_base"))
                    t.pop("_base", None)
                    setattr(it, name, t)
                    continue
                v = b.get(name)
                if v is None:
                    continue
                try:
                    setattr(it, name, {"int": int, "float": float, "bool": lambda s: s not in ("0", ""), "str": str}[kind](
                        int(float(v)) if kind == "int" else v))
                except ValueError:
                    pass
            items.append(it)
        project.items = items
        log.append("предметов: %d" % len(items))
    p = os.path.join(game_dir, "scripts", "rpg", "lootlists.txt")
    if os.path.isfile(p):
        root = kv.parse_file(p)
        top = root.block("LootLists") or (root.blocks()[0] if root.blocks() else kv.KV("LootLists"))
        lists = []
        for b in top.blocks():
            ll = LootList(id=b.name)
            ll.all = (b.get("pick") or "") != "one"
            for e in b.blocks():
                ent = {"item": e.get("item") or ""}
                for k in ("min", "max", "chance", "level"):
                    if e.get(k):
                        ent[k] = int(float(e.get(k)))
                ll.entries.append(ent)
            lists.append(ll)
        project.lootlists = lists
        log.append("списков лута: %d" % len(lists))
    p = os.path.join(game_dir, "scripts", "rpg", "terminals.txt")
    if os.path.isfile(p):
        from .termdata import import_terminals
        import_terminals(project, p, log)
