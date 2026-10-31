"""Game files of the world content (see worlddata.py); the server side is
game/server/hl2rpm_world.cpp and hl2/ai_behavior_rpmroutine.cpp."""
from . import kv

LOC_GAME = {"self": "self", "ref": "ref", "point": "point", "player": "player", "current": "current"}


def _f(x):
    s = ("%.2f" % float(x)).rstrip("0").rstrip(".")
    return s if s not in ("-0", "") else "0"


def _vec(v):
    return " ".join(_f(c) for c in v)


def _loot(items):
    return ";".join("%s:%d" % (c, int(n)) if int(n) != 1 else c for c, n in items if c)


def _texts(blk, key, texts, base):
    if texts.get(base):
        blk.add(key, texts[base])
    for lang, t in sorted(texts.items()):
        if lang != base and t:
            blk.add("%s_%s" % (key, lang), t)


def package_kv(parent, idx, p):
    b = parent.add(str(idx))
    b.add("name", p.name or p.type)
    b.add("type", p.type)
    b.add("start", _f(p.start))
    b.add("duration", _f(24 if p.always() else p.duration))
    b.add("location", LOC_GAME.get(p.location, "self"))
    if p.ref:
        b.add("ref", p.ref)
    if p.location == "point":
        b.add("point", _vec(p.point))
    if p.yaw is not None:
        b.add("yaw", _f(p.yaw))
    if p.radius:
        b.add("radius", str(int(p.radius)))
    if p.run:
        b.add("run", "1")
    if p.anim:
        b.add("anim", p.anim)
    if p.markers:
        b.add("markers", ";".join(p.markers))
    if p.type == "patrol" and p.path:
        b.add("path", p.path)
        if p.path_mode != "loop":
            b.add("path_mode", p.path_mode)
    if p.type == "follow" and p.target:
        b.add("target", p.target)
    if p.type == "talk":
        b.add("once", "1" if p.once else "0")
    if p.type == "sandbox" and (p.min_use, p.max_use) != (30, 90):
        b.add("min_use", str(int(p.min_use)))
        b.add("max_use", str(int(p.max_use)))
    if p.req_state and p.req_state[0]:
        b.add("require_quest_state", "%s:%s" % (p.req_state[0], p.req_state[1]))
    if p.req_stage and p.req_stage[0]:
        b.add("require_quest_stage", "%s:%d" % (p.req_stage[0], int(p.req_stage[1])))
    b.inline = True
    return b


def npc_kv(project, npc):
    a = npc.actor
    base = project.base_language
    r = kv.KV("NPC")
    r.add("targetname", npc.targetname)
    r.add("classname", a.classname)
    if a.model:
        r.add("model", a.model)
    if a.skin:
        r.add("skin", str(a.skin))
    if a.weapon:
        r.add("weapon", a.weapon)
    if a.squad:
        r.add("squad", a.squad)
    if a.faction:
        r.add("faction", a.faction)
    if a.relation:
        r.add("player_relation", a.relation)
    if a.health:
        r.add("health", str(a.health))
    if a.essential:
        r.add("essential", "1")
    if a.commandable:
        r.add("commandable", "1")
    _texts(r, "display_name", npc.display_name, base)
    keys = r.add("keyvalues")
    if a.classname == "npc_citizen" and a.citizen_type:
        keys.add("citizentype", a.citizen_type)
    keys.add("hate_on_dmg", "1" if a.hate_on_dmg else "0")
    if a.spoil_on_dmg:
        keys.add("spoil_on_dmg", "1")
    if a.loot_items:
        keys.add("loot_items", _loot(a.loot_items))
    if npc.display_name.get(base):
        keys.add("loot_displayname", npc.display_name[base])
    for k, v in sorted(a.keyvalues.items()):
        if k:
            keys.add(k, str(v))
    if a.packages:
        pk = r.add("packages")
        for i, p in enumerate(a.packages):
            package_kv(pk, i, p)
    return r


def layer_kv(project, layer):
    base = project.base_language
    w = kv.KV("World")
    w.add("map", layer.name)
    refs = w.add("refs")
    for ref in layer.refs:
        b = refs.add(str(ref.id))
        b.add("type", ref.type)
        if ref.type != "path":
            b.add("origin", _vec(ref.pos))
            b.add("angles", _vec(ref.angles))
        if ref.type == "npc":
            b.add("npc", ref.npc)
        elif ref.type == "marker":
            b.add("name", ref.name or ("rpm_marker_%d" % ref.id))
            b.add("kind", ref.kind)
            if ref.anim:
                b.add("anim", ref.anim)
            if ref.owner:
                b.add("owner", ref.owner)
        elif ref.type == "item":
            b.add("classname", ref.classname)
        elif ref.type == "container":
            b.add("model", ref.model)
            if ref.loot_items:
                b.add("loot_items", _loot(ref.loot_items))
            _texts(b, "loot_displayname", ref.display_name, base)
        elif ref.type == "prop":
            b.add("model", ref.model)
            if ref.physics:
                b.add("physics", "1")
        elif ref.type == "path":
            b.add("name", ref.name or ("rpm_path_%d" % ref.id))
            pts = b.add("points")
            for i, pt in enumerate(ref.points):
                x = pts.add(str(i))
                x.add("origin", _vec(pt.pos))
                if pt.wait:
                    x.add("wait", _f(pt.wait))
                if pt.yaw is not None:
                    x.add("yaw", _f(pt.yaw))
                if pt.anim:
                    x.add("anim", pt.anim)
                x.inline = True
        elif ref.type == "entity":
            b.add("classname", ref.classname)
        if ref.name and ref.type not in ("marker", "path"):
            b.add("name", ref.name)
        if ref.skin:
            b.add("skin", str(ref.skin))
        if ref.keyvalues:
            k = b.add("keyvalues")
            for kk, vv in sorted(ref.keyvalues.items()):
                k.add(kk, str(vv))
            k.inline = True
        if ref.type in ("npc", "marker", "item", "prop"):
            b.inline = True
    return w


def factions_kv(project):
    base = project.base_language
    f = kv.KV("Factions")
    for fac in project.factions:
        b = f.add(fac.id)
        _texts(b, "name", fac.name, base)
        if fac.player:
            b.add("player", fac.player)
        if fac.relations:
            rel = b.add("relations")
            for other, disp in sorted(fac.relations.items()):
                if disp:
                    rel.add(other, disp)
            rel.inline = True
    return f


def nav_text(layer, header):
    lines = ["// " + h for h in header.splitlines()]
    lines.append("// %d AI nodes of %s, spacing %d" % (len(layer.nav), layer.name, layer.nav_spacing))
    for p in layer.nav:
        lines.append("%s %s %s" % (_f(p[0]), _f(p[1]), _f(p[2])))
    return "\n".join(lines) + "\n"


def needs_npc_file(npc):
    return not npc.actor.is_default()
