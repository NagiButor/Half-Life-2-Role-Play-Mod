"""Game files of the world content (see worlddata.py); the server side is
game/server/hl2rpm_world.cpp and hl2/ai_behavior_rpmroutine.cpp."""
from . import kv
from . import logic

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
    else:  # the plain key is the fallback of every language (see Exporter._texts)
        fb = next((t for _, t in sorted(texts.items()) if t), None)
        if fb:
            blk.add(key, fb)
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
    logic.write_conds(b, "conditions", p.conditions)
    b.inline = True
    return b


def bark_owner(project, npc, depth=0):
    """Whose barks the NPC says (its own, else the first template that has some): their voices lie
    under sound/dialogs/<owner>/."""
    if npc.actor.barks or not npc.actor.template or depth > 6:
        return npc.targetname
    t = project.npc(npc.actor.template)
    return bark_owner(project, t, depth + 1) if t is not None and t is not npc else npc.targetname


def barks_kv(parent, barks, project, media=None):
    """Bark lines into parent: "<id>" { event value text(_lang) choreo(_lang) sound(_lang) weight ... }."""
    base = project.base_language
    media = media or {}
    for b in barks:
        if not b.id:
            continue
        x = parent.add(b.id, None, b.note)
        x.add("event", b.event or "idle")
        if b.value:
            x.add("value", b.value)
        _texts(x, "text", b.text, base)
        m = media.get(b.id) or {}
        if base in m:
            c, s = m[base]
            if c:
                x.add("choreo", c)
            if s:
                x.add("sound", s)
        elif b.sound:
            x.add("sound", b.sound)
        for lang in project.languages:
            if lang == base:
                continue
            if lang in m:
                c, s = m[lang]
                x.add("choreo_" + lang, c)
                x.add("sound_" + lang, s)
            elif base in m and project.settings.missing_voice == "silent":
                x.add("choreo_" + lang, "")
                x.add("sound_" + lang, "")
        if b.weight != 1.0:
            x.add("weight", _f(b.weight))
        x.add("cooldown", _f(b.cooldown))
        if b.once:
            x.add("once", "1")
        if b.chance != 100:
            x.add("chance", str(int(b.chance)))
        logic.write_conds(x, "conditions", b.conditions, project)


def npc_kv(project, npc, bark_media=None):
    a = npc.actor.resolved(project)
    base = project.base_language
    names = npc.display_name
    if not any(names.values()) and npc.actor.template and project.npc(npc.actor.template):
        names = project.npc(npc.actor.template).display_name
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
    _texts(r, "display_name", names, base)
    if a.models:
        r.add("models", ";".join(a.models))
    if a.weapons:
        r.add("weapons", ";".join(a.weapons))
    if a.skin_max > a.skin:
        r.add("skin_max", str(a.skin_max))
    if a.proficiency >= 0:
        r.add("proficiency", str(a.proficiency))
    if a.aggression >= 0:
        r.add("aggression", str(a.aggression))
    if a.confidence >= 0:
        r.add("confidence", str(a.confidence))
    if a.yield_player >= 0:
        r.add("yield", str(a.yield_player))
    if a.pickup_weapons >= 0:
        r.add("pickup_weapons", str(a.pickup_weapons))
    if a.weapon_law >= 0:
        r.add("weapon_law", str(a.weapon_law))
    if a.xp:
        r.add("xp", str(a.xp))
    if a.names or a.names_female:
        nb = r.add("names")
        for i, (n, female) in enumerate([(n, False) for n in a.names] + [(n, True) for n in a.names_female]):
            x = nb.add(str(i))
            for lang, t in sorted(n.items()):
                if t:
                    x.add(lang, t)
            if female:
                x.add("female", "1")
            x.inline = True
    if a.stock_speech:
        r.add("stock_speech", a.stock_speech)
    if a.bark_sets:
        r.add("bark_sets", ";".join(a.bark_sets))
    if a.barks:
        barks_kv(r.add("barks"), a.barks, project, (bark_media or {}).get(bark_owner(project, npc)))
    if a.vendor:
        vb = r.add("vendor")
        for k in ("list", "money", "markup", "pays", "restock_hours", "currency"):
            if a.vendor.get(k) not in (None, ""):
                vb.add(k, str(a.vendor[k]))
        vb.inline = True
    keys = r.add("keyvalues")
    if a.classname == "npc_citizen" and a.citizen_type:
        keys.add("citizentype", a.citizen_type)
    keys.add("hate_on_dmg", "1" if a.hate_on_dmg else "0")
    if a.spoil_on_dmg:
        keys.add("spoil_on_dmg", "1")
    if a.loot_items:
        keys.add("loot_items", _loot(a.loot_items))
    if names.get(base):
        keys.add("loot_displayname", names[base])
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
            if ref.seat_h >= 0:
                b.add("seat_h", "%.1f" % ref.seat_h)
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
        elif ref.type == "terminal":
            from .termdata import DEFAULT_MODEL
            t = project.terminal(ref.terminal) if hasattr(project, "terminal") else None
            b.add("model", ref.model or DEFAULT_MODEL.get(t.style if t else "pc", DEFAULT_MODEL["pc"]))
            b.add("terminal", ref.terminal)
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
        elif ref.type == "transition":
            arrive, yaw = ref.arrive()
            b.add("target_map", ref.target_map)
            b.add("target", ref.target)
            b.add("size", _vec(ref.size))
            b.add("arrive", _vec(arrive))
            b.add("arrive_yaw", _f(yaw))
        elif ref.type == "zone":
            b.add("size", _vec(ref.size))
            _texts(b, "display_name", ref.display_name, base)
            if ref.discover_xp:
                b.add("discover_xp", str(int(ref.discover_xp)))
            if ref.music:
                b.add("music", ";".join(ref.music))
            if ref.soundscape:
                b.add("soundscape", ref.soundscape)
                if ref.soundscape_night:
                    b.add("soundscape_night", ref.soundscape_night)
                if ref.soundscape_rain:
                    b.add("soundscape_rain", ref.soundscape_rain)
        elif ref.type == "sound":
            b.add("sound", ref.sound)
            b.add("volume", _f(ref.volume))
            b.add("radius", str(int(ref.radius)))
            if ref.everywhere:
                b.add("everywhere", "1")
        if ref.name and ref.type not in ("marker", "path"):
            b.add("name", ref.name)
        if ref.skin:
            b.add("skin", str(ref.skin))
        if ref.keyvalues:
            k = b.add("keyvalues")
            for kk, vv in sorted(ref.keyvalues.items()):
                k.add(kk, str(vv))
            k.inline = True
        if ref.type not in ("path", "transition", "zone"):
            logic.write_conds(b, "conditions", ref.conditions, project)
        if ref.type in ("npc", "marker", "item", "prop", "zone"):
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
