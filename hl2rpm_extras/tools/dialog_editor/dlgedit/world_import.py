"""Read the world files of a game folder into a project (scripts/npcs, scripts/world)."""
import glob
import math
import os

from . import kv
from . import logic
from .model import NPC, Node, Voice
from .sounddata import Bark, BarkSet, MusicTrack, Soundscape, new_loop, new_random
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
    out = {k: v for k, v in out.items() if v}
    # the exporter copies a translation into the plain key when the base language has no text
    if base in out and any(l != base and t == out[base] for l, t in out.items()):
        del out[base]
    return out


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
    p.conditions = logic.read_conds(b.block("conditions"))
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
        a.models = [x for x in (blk.get("models") or "").split(";") if x]
        a.weapons = (blk.get("weapons") or "").split(";") if blk.get("weapons") else []
        a.skin_max = int(blk.get("skin_max", "0") or 0)
        a.proficiency = int(blk.get("proficiency", "-1") or -1)
        a.aggression = int(blk.get("aggression", "-1") or -1)
        a.confidence = int(blk.get("confidence", "-1") or -1)
        a.yield_player = int(blk.get("yield", "-1") or -1)
        a.pickup_weapons = int(blk.get("pickup_weapons", "-1") or -1)
        a.weapon_law = int(blk.get("weapon_law", "-1") or -1)
        a.xp = int(blk.get("xp", "0") or 0)
        vb = blk.block("vendor")
        if vb is not None:
            a.vendor = {c.name.lower(): c.value for c in vb.values()}
        nb = blk.block("names")
        if nb is not None:
            for x in nb.blocks():
                n = {c.name.lower(): c.value for c in x.values() if c.name.lower() != "female"}
                (a.names_female if x.get("female") == "1" else a.names).append(n)
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
        a.stock_speech = blk.get("stock_speech", "") or ""
        a.bark_sets = [x for x in (blk.get("bark_sets") or "").split(";") if x]
        bb = blk.block("barks")
        if bb is not None:
            a.barks = import_barks(bb, project)
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
            r.seat_h = float(b.get("seat_h", "-1") or -1)
            r.classname = b.get("classname", "")
            r.model = b.get("model", "")
            r.terminal = b.get("terminal", "") or ""
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
            r.conditions = logic.read_conds(b.block("conditions"), project.languages)
            if r.type == "zone":
                r.size = _vec(b.get("size", "256 256 128"))
                r.display_name = _loc_texts(b, "display_name", base)
                r.discover_xp = int(float(b.get("discover_xp", "0") or 0))
                r.music = [x for x in (b.get("music") or "").split(";") if x]
                r.soundscape = b.get("soundscape", "") or ""
                r.soundscape_night = b.get("soundscape_night", "") or ""
                r.soundscape_rain = b.get("soundscape_rain", "") or ""
            if r.type == "sound":
                r.sound = b.get("sound", "") or ""
                r.volume = float(b.get("volume", "1") or 1)
                r.radius = float(b.get("radius", "1250") or 1250)
                r.everywhere = b.get("everywhere", "0") not in ("0", "")
            if r.type == "transition":
                r.target_map = b.get("target_map", "")
                r.target = b.get("target", "")
                r.size = _vec(b.get("size", "64 64 96"))
                arr = _vec(b.get("arrive", b.get("origin")))
                r.arrive_dist = round(math.hypot(arr[0] - r.pos[0], arr[1] - r.pos[1]), 1)
            layer.refs.append(r)
        nav = os.path.join(game_dir, "scripts", "world", name + "_nav.txt")
        if os.path.isfile(nav):
            layer.nav = read_nav(nav)
        n_maps += 1
    import_rpg_logic(project, game_dir, log)
    import_sound(project, game_dir, log)
    import_rpg_char(project, game_dir, log)
    log.append("мир: NPC %d, карт %d, фракций %d" % (n_npc, n_maps, len(project.factions)))
    return n_npc, n_maps


def import_barks(blk, project):
    """Bark lines of an NPC file / a bark set back into sounddata.Bark (voices under sound/ become voices)."""
    base = project.base_language
    out = []
    for x in blk.blocks():
        b = Bark(x.name, x.get("event", "idle") or "idle")
        b.value = x.get("value", "") or ""
        b.text = _loc_texts(x, "text", base)
        b.note = x.comment or ""
        for lang in project.languages:
            snd = x.get("sound") if lang == base else x.get("sound_" + lang)
            ch = x.get("choreo") if lang == base else x.get("choreo_" + lang)
            if snd and snd.replace("\\", "/").lower().startswith("dialogs/"):
                v = Voice()
                v.wav = "sound/" + snd.replace("\\", "/")
                if ch:
                    v.vcd = ch + ".vcd"
                b.voices[lang] = v
            elif snd and lang == base:
                b.sound = snd
        b.weight = float(x.get("weight", "1") or 1)
        b.cooldown = float(x.get("cooldown", "30") or 30)
        b.once = x.get("once", "0") not in ("0", "")
        b.chance = int(float(x.get("chance", "100") or 100))
        b.conditions = logic.read_conds(x.block("conditions"), project.languages)
        out.append(b)
    return out


def _range(s, default):
    try:
        v = [float(x) for x in (s or "").replace(" ", "").split(",") if x]
    except ValueError:
        return list(default)
    return [v[0], v[0]] if len(v) == 1 else (v[:2] if len(v) >= 2 else list(default))


def _sndlvl(s):
    s = (s or "").upper().replace("SNDLVL_", "").replace("DB", "")
    try:
        return int(float(s))
    except ValueError:
        return 0


def import_rpg_char(project, game_dir, log):
    """scripts/rpg/rpg.txt and newgame.txt ("RPG" and "Game" of the Creation Kit)."""
    from . import rpgchar
    path = os.path.join(game_dir, "scripts", "rpg", "rpg.txt")
    if os.path.isfile(path):
        rpgchar.import_rpg_char(project, kv.parse_file(path))
        log.append("РПГ: перков %d, черт %d" % (len(project.perks), len(project.traits)))
    path = os.path.join(game_dir, "scripts", "rpg", "newgame.txt")
    if os.path.isfile(path):
        rpgchar.import_newgame(project, kv.parse_file(path))
        log.append("новая игра: карта %s" % (project.newgame.get("map") or "любая"))


def import_sound(project, game_dir, log):
    """scripts/rpg/music.txt, barks.txt, scripts/soundscapes_hl2rpm.txt ("Sound" tab)."""
    path = os.path.join(game_dir, "scripts", "rpg", "music.txt")
    if os.path.isfile(path):
        root = kv.parse_file(path)
        top = root.block("Music") or (root.blocks()[0] if root.blocks() else None)
        tb = top.block("tracks") if top is not None else None
        tracks = []
        for b in (tb.blocks() if tb is not None else []):
            t = MusicTrack(b.name)
            t.file = b.get("file", "") or ""
            t.volume = float(b.get("volume", "1") or 1)
            t.kind = b.get("kind", "explore") or "explore"
            t.time = b.get("time", "any") or "any"
            t.maps = [x for x in (b.get("maps") or "").split(";") if x]
            t.event = b.get("event", "") or ""
            t.note = b.comment or ""
            tracks.append(t)
        project.music = tracks
        log.append("музыка: треков %d" % len(tracks))
    path = os.path.join(game_dir, "scripts", "rpg", "barks.txt")
    if os.path.isfile(path):
        root = kv.parse_file(path)
        top = root.block("Barks") or (root.blocks()[0] if root.blocks() else None)
        sets = []
        for b in (top.blocks() if top is not None else []):
            s = BarkSet(b.name)
            s.classes = [x for x in (b.get("classes") or "").split(";") if x]
            s.note = b.comment or ""
            s.barks = import_barks(b, project)
            sets.append(s)
        project.bark_sets = sets
        log.append("наборов фраз: %d" % len(sets))
    path = os.path.join(game_dir, "scripts", "soundscapes_hl2rpm.txt")
    if os.path.isfile(path):
        root = kv.parse_file(path)
        scapes = []
        for b in root.blocks():
            s = Soundscape(b.name)
            s.note = b.comment or ""
            try:
                s.dsp = int(float(b.get("dsp", "1") or 1))
            except ValueError:
                s.dsp = 1
            for x in b.blocks():
                n = x.name.lower()
                if n == "playlooping" and x.get("wave"):
                    s.loops.append(new_loop(x.get("wave"), float(x.get("volume", "0.6") or 0.6),
                                            int(float(x.get("pitch", "100") or 100)), _sndlvl(x.get("soundlevel"))))
                elif n == "playrandom":
                    rw = x.block("rndwave")
                    waves = [c.value for c in rw.values()] if rw is not None else []
                    s.randoms.append(new_random(waves, _range(x.get("time"), (8, 20)), _range(x.get("volume"), (0.4, 0.7)),
                                                _range(x.get("pitch"), (95, 105)), _sndlvl(x.get("soundlevel"))))
            scapes.append(s)
        project.soundscapes = scapes
        log.append("звуковых сцен: %d" % len(scapes))


def import_rpg_logic(project, game_dir, log):
    """scripts/rpg/quests.txt, events.txt, globals.txt (Creation Kit logic)."""
    from .importer import _read_quest
    from .model import Event, Global
    base = project.base_language
    langs = [base] + [lg for lg in project.languages if lg != base]
    path = os.path.join(game_dir, "scripts", "rpg", "quests.txt")
    if os.path.isfile(path):
        qb = kv.parse_file(path).block("Quests")
        n = 0
        for qkv in (qb.blocks() if qb else []):
            q = _read_quest(qkv, langs, base, "")
            old = project.quest(q.id)
            if old is not None:
                q.owner = old.owner
                project.quests[project.quests.index(old)] = q
            else:
                project.quests.append(q)
            n += 1
        log.append("задания: %d" % n)
    path = os.path.join(game_dir, "scripts", "rpg", "events.txt")
    if os.path.isfile(path):
        eb = kv.parse_file(path).block("Events")
        for b in (eb.blocks() if eb else []):
            e = Event(b.name)
            e.note = b.comment
            e.on = b.get("on", "when")
            e.target = b.get("target", "") or ""
            e.value = b.get("value", "") or ""
            e.map = b.get("map", "") or ""
            e.once = b.get("once", "1") not in ("0", "")
            e.delay = float(b.get("delay", "0") or 0)
            e.conditions = logic.read_conds(b.block("conditions"), langs)
            e.actions = logic.read_actions(b.block("actions"), langs)
            old = project.event(e.id)
            if old is not None:
                project.events[project.events.index(old)] = e
            else:
                project.events.append(e)
        log.append("события: %d" % len(project.events))
    path = os.path.join(game_dir, "scripts", "rpg", "topics.txt")
    if os.path.isfile(path):
        from .model import Topic
        tb = kv.parse_file(path).block("Topics")
        for b in (tb.blocks() if tb else []):
            if b.name == "greeting":
                project.topic_greeting = _loc_texts(b, "line", project.base_language)
                continue
            t = Topic(b.name)
            t.note = b.comment
            t.classes = [x for x in (b.get("classes") or "").split(";") if x]
            t.factions = [x for x in (b.get("factions") or "").split(";") if x]
            t.text = _loc_texts(b, "text", project.base_language)
            t.line = _loc_texts(b, "line", project.base_language)
            t.once = b.get("once", "0") not in ("0", "")
            t.conditions = logic.read_conds(b.block("conditions"), langs)
            t.actions = logic.read_actions(b.block("actions"), langs)
            old = project.topic(t.id)
            if old is not None:
                project.topics[project.topics.index(old)] = t
            else:
                project.topics.append(t)
        log.append("общие темы: %d" % len(project.topics))
    path = os.path.join(game_dir, "scripts", "rpg", "globals.txt")
    if os.path.isfile(path):
        gb = kv.parse_file(path).block("Globals")
        for kind, key in (("flag", "flags"), ("var", "vars")):
            blk = gb.block(key) if gb else None
            for c in (blk.values() if blk else []):
                g = project.global_(c.name) or Global(c.name, kind)
                g.kind = kind
                try:
                    g.default = int(c.value)
                except ValueError:
                    g.default = 0
                g.note = c.comment or g.note
                if g not in project.globals:
                    project.globals.append(g)


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
