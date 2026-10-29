#!/usr/bin/env python3
"""Shared helpers for the vertical slice maps (phase B: people, signs, quest hooks).

Used by prep_rp_c17_0*.py. The maps come from decompiled retail HL2 maps that were lit for baked
lighting; the deferred renderer lights them itself (cdeferred_manager_server.cpp converts the
classic lamps, gates the ones that see the sky to the night), so the baked-light fakes go.
"""
import sys

sys.path.insert(0, r"E:\SourceModding\HL2RPMSOURCE\.claude\skills\hl2rpm-hammer\scripts")
from vmf import VMF, Node  # noqa: E402,F401

# NPC spawnflags
SF_FALL = 4
SF_DONT_DROP = 8192
SF_IGNORE_PUSH = 16384
SF_CIT_NOT_COMMANDABLE = 1048576
CITIZEN_FLAGS = str(SF_FALL | SF_IGNORE_PUSH | SF_CIT_NOT_COMMANDABLE)
CP_FLAGS = str(SF_FALL | SF_IGNORE_PUSH | SF_DONT_DROP)


def lower(s):
    return (s or "").lower()


def npc_citizen(m, name, pos, yaw, model, **kw):
    """A slice citizen: stands where placed. Its dialog is scripts/dialogs/<targetname>; without one the key
    rpg_dialog names a shared one (_bark_*), and without that the faction's default bark answers."""
    keys = dict(targetname=name, angles="0 %g 0" % yaw, model=model, citizentype="4",
                expressiontype="Random", spawnflags=CITIZEN_FLAGS, rpg_faction="citizen")
    keys.update(kw)
    return m.add_entity("npc_citizen", pos, **keys)


def npc_cp(m, name, pos, yaw, **kw):
    keys = dict(targetname=name, angles="0 %g 0" % yaw, spawnflags=CP_FLAGS, manhacks="0",
                weapondrawn="0", additionalequipment="weapon_stunstick", rpg_faction="cp")
    keys.update(kw)
    return m.add_entity("npc_metropolice", pos, **keys)


def idle_seq(m, npc_name, pos, yaw, seq, **kw):
    """Loop one animation forever on an NPC that stands at pos (sit on a bench, queue, lean, sweep):
    starts on spawn, no script movement, the post idle loops. The NPC stays talkable."""
    keys = dict(targetname="ss_" + npc_name, angles="0 %g 0" % yaw, m_iszEntity=npc_name, m_iszIdle=seq,
                m_iszPlay=seq, m_iszPostIdle=seq, m_fMoveTo="0", m_flRadius="0", m_flRepeat="0",
                spawnflags="400", onplayerdeath="0")
    keys.update(kw)
    return m.add_entity("scripted_sequence", pos, **keys)


def life(m, name, pos, yaw, model, seq=None, cls="citizen", **kw):
    """An ambient person of the slice (ours, talkable): a citizen with a looped idle, or a CP unit standing
    at a post (CPs stay unscripted: the law system sends them to check the player and back)."""
    if cls == "cp":
        e = npc_cp(m, name, pos, yaw, **kw)
    else:
        e = npc_citizen(m, name, pos, yaw, model, **kw)
    if seq:
        idle_seq(m, name, pos, yaw, seq)
    return e


# ---------------------------------------------------------------------------------------
# The decompiled retail maps come with Valve's people: Gordon's queue, the cops who herd and push
# him (ai_goal_police), the choreographed scenes of every citizen, act-busy goals, NPC makers. None
# of them can be talked to and the police goals block the way. The slice keeps the set (geometry,
# props, lights, sounds, doors) and the environment that is not a person (cameras, scanners, Breen
# on the screens, the strider behind the gates, birds) and puts its own people on it.
# ---------------------------------------------------------------------------------------
KEEP_NPC_CLASSES = {"npc_combine_camera", "npc_cscanner", "npc_clawscanner", "npc_breen", "npc_strider",
                    "npc_pigeon", "npc_crow", "npc_seagull", "npc_turret_ceiling", "npc_combinedropship",
                    "npc_combinegunship", "npc_helicopter"}
AI_SCRIPT_CLASSES = {"scripted_sequence", "aiscripted_schedule", "ai_goal_actbusy", "ai_goal_actbusy_queue",
                     "ai_goal_police", "ai_goal_follow", "ai_goal_lead", "ai_goal_assault", "ai_goal_standoff",
                     "ai_goal_fightfromcover", "ai_script_conditions", "ai_speechfilter", "logic_choreographed_scene",
                     "scripted_target", "ai_changetarget", "ai_changehintgroup", "assault_rallypoint",
                     "assault_assaultpoint", "info_npc_spawn_destination", "ai_relationship", "scripted_sentence"}
_ACTOR_KEYS = ("m_iszentity", "actor", "target1", "target2", "target3", "target4", "target5", "target6", "target7",
               "target8", "subject", "target", "entity", "goal", "searchname", "policetarget")


def strip_original_life(m, log, keep=()):
    """Remove every original person and everything that drives one (see above); keep = targetnames of people
    that stay (renamed into slice characters). Returns the set of removed targetnames (outputs to them go too)."""
    dead = set()
    dead_classes = set()
    keep = set(lower(k) for k in keep)
    n_npc = 0
    for e in list(m.entities()):
        cls = lower(e.classname)
        if cls.startswith("npc_") and cls not in KEEP_NPC_CLASSES and cls != "npc_template_maker" and                 lower(e.get("targetname")) not in keep:
            dead_classes.add(cls)
            if e.get("targetname"):
                dead.add(lower(e.get("targetname")))
            m.delete(e)
            n_npc += 1

    def is_dead(v):
        v = lower(v)
        if not v or v.startswith("!") or v in keep:
            return False
        if v in dead or v in dead_classes:
            return True
        if v.endswith("*"):
            p = v[:-1]
            return any(d.startswith(p) for d in dead)
        return False

    # templates: drop the dead members; a template with nothing left goes
    n_tpl = 0
    for e in list(m.entities("point_template")):
        keys = [k for k, _ in e.props if lower(k).startswith("template") and lower(k) != "templatename"]
        for k in keys:
            if is_dead(e.get(k)):
                e.delete(k)
        if not [k for k, _ in e.props if lower(k).startswith("template")]:
            dead.add(lower(e.get("targetname")))
            m.delete(e)
            n_tpl += 1
    for e in list(m.entities("npc_template_maker")):
        if is_dead(e.get("TemplateName")) or lower(e.get("TemplateName")) in dead:
            dead.add(lower(e.get("targetname")))
            m.delete(e)
            n_tpl += 1
    # what drives people: a script entity that names a removed person (or a class of them) goes
    n_ai = 0
    for _ in range(2):   # a scene can name a script entity removed in the first pass
        for e in list(m.entities()):
            if lower(e.classname) not in AI_SCRIPT_CLASSES:
                continue
            actors = [v for k, v in e.props if lower(k) in _ACTOR_KEYS and v]
            kill = any(is_dead(v) for v in actors)
            if lower(e.classname) in ("ai_goal_police", "ai_goal_actbusy", "ai_goal_actbusy_queue", "aiscripted_schedule",
                                      "ai_script_conditions", "ai_speechfilter", "info_npc_spawn_destination") and \
                    not any(lower(v) in ("strider_1", "breen") or "scanner" in lower(v) for v in actors):
                kill = True      # herding / act-busy / makers' destinations of the old crowd: all of them
            if kill:
                if e.get("targetname"):
                    dead.add(lower(e.get("targetname")))
                m.delete(e)
                n_ai += 1
    # templates whose last members were scripts removed above
    for e in list(m.entities("point_template")):
        keys = [k for k, _ in e.props if lower(k).startswith("template") and lower(k) != "templatename"]
        for k in keys:
            if lower(e.get(k)) in dead:
                e.delete(k)
        if not [k for k, _ in e.props if lower(k).startswith("template")]:
            dead.add(lower(e.get("targetname")))
            m.delete(e)
            n_tpl += 1
    alive = set(lower(e.get("targetname")) for e in m.entities() if e.get("targetname"))
    dead -= alive
    n_out = 0
    for e in m.entities():
        con = e.child("connections")
        if not con:
            continue
        keep = []
        for it in con.items:
            if isinstance(it, Node):
                keep.append(it)
                continue
            sep = "\x1b" if "\x1b" in it[1] else ","
            tgt = lower(it[1].split(sep)[0])
            if tgt in dead or (tgt.endswith("*") and is_dead(tgt)):
                n_out += 1
                continue
            keep.append(it)
        con.items = keep
    log.append("original life removed: %d NPCs (%s), %d templates/makers, %d scripts/scenes/goals, %d outputs"
               % (n_npc, " ".join(sorted(dead_classes)), n_tpl, n_ai, n_out))
    return dead


def sign(m, pos, yaw, token, size=24, color="235 225 200", resolution="96", name=None):
    """In-world text (Mapbase vgui_text_display); #tokens are localized (hl2rpm_dialog_<lang>)."""
    keys = dict(angles="0 %g 0" % yaw, message=token, alignment="4", resolution=resolution,
                textsize=str(size), rendercolor=color, spawnflags="0")
    if name:
        keys["targetname"] = name
    return m.add_entity("vgui_text_display", pos, **keys)


def prop(m, cls, model, pos, ang="0 0 0", **kw):
    keys = dict(model=model, angles=ang)
    if cls == "prop_static":
        keys.update(solid="6", skin="0")
    keys.update(kw)
    return m.add_entity(cls, pos, **keys)


def logic_rpg(m, pos, name, **kw):
    keys = dict(targetname=name)
    keys.update(kw)
    return m.add_entity("logic_rpg", pos, **keys)


def trigger_box(m, classname, boxes, **keys):
    sol = [m.make_box(lo, hi, "TOOLS/TOOLSTRIGGER") for lo, hi in boxes]
    return m.add_brush_entity(classname, sol, **keys)


def clip_brush(m, name, lo, hi, start_disabled=False):
    """Invisible player-blocking func_brush (Disable to let the player through)."""
    sol = [m.make_box(lo, hi, "TOOLS/TOOLSPLAYERCLIP")]
    # Solidity 0 "toggle": solid while enabled. ("2" - always solid - stayed solid after Disable: the station exit
    # was closed even with a checked pass)
    return m.add_brush_entity("func_brush", sol, targetname=name, spawnflags="2", Solidity="0",
                              StartDisabled="1" if start_disabled else "0", rendermode="10",
                              disableshadows="1")


def rename(m, old, new):
    """Rename a targetname and every reference to it (keys and output targets)."""
    n = 0
    lo = lower(old)
    for e in m.entities():
        # props are the node's own [key, value] lists: edit them in place
        for it in e.props:
            if lower(it[0]) not in ("classname", "id") and lower(it[1]) == lo:
                it[1] = new
                n += 1
        con = e.child("connections")
        if con is None:
            continue
        for it in con.props:
            sep = "\x1b" if "\x1b" in it[1] else ","
            parts = it[1].split(sep)
            if parts and lower(parts[0]) == lo:
                parts[0] = new
                it[1] = sep.join(parts)
                n += 1
    return n


def strip_baked_fakes(m, log, keep_box=None):
    """Remove what only made sense with baked lighting: fake light shafts (vol_light models),
    fake lamp cones (unnamed point_spotlight beams), the env_sun sprite. keep_box=(lo, hi) keeps
    the ones inside (the dream set)."""
    def inside(e):
        if not keep_box:
            return False
        o = e.get("origin")
        if not o:
            return False
        x, y, z = map(float, o.split())
        (x0, y0, z0), (x1, y1, z1) = keep_box
        return x0 <= x <= x1 and y0 <= y <= y1 and z0 <= z <= z1

    counts = {}
    for e in list(m.entities()):
        cls = lower(e.get("classname"))
        mdl = lower(e.get("model"))
        kill = False
        if cls in ("prop_dynamic", "prop_static", "prop_dynamic_override") and "effects/vol_light" in mdl:
            kill = True
        elif cls == "point_spotlight" and not e.get("targetname"):
            kill = True
        elif cls == "env_sun":
            kill = True
        elif cls == "beam_spotlight" and not e.get("targetname"):
            kill = True
        if kill and not inside(e):
            m.delete(e)
            counts[cls] = counts.get(cls, 0) + 1
    log.append("baked-light fakes removed: %s" % ", ".join("%s %d" % kv for kv in sorted(counts.items())))
    return counts


def timecycle(m, pos, start_time, **kw):
    """env_timecycle: the world clock (sun, curfew, night lamps, sleep). On a changelevel the map
    takes the world's clock, start_time only counts on a new game / direct map load."""
    keys = dict(targetname="world_clock", start_time=str(start_time), time_scale="10",
                day_length_seconds="8640", sunrise_hour="6", sunset_hour="18.5", max_altitude="58",
                twilight_hours="1.25")
    keys.update(kw)
    for e in list(m.entities("env_timecycle")):
        m.delete(e)
    return m.add_entity("env_timecycle", pos, **keys)


def spot(m, name, pos, yaw=0, **kw):
    keys = dict(targetname=name, angles="0 %g 0" % yaw)
    keys.update(kw)
    return m.add_entity("info_target", pos, **keys)


TRAM_MAPS = ("rp_c17_01_trainstation", "rp_c17_02_plaza", "rp_c17_04_oldtown", "rp_c17_08_opora")


def tram_stop(m, this_map, arrive, yaw, prop_pos, prop_yaw):
    """A tram stop (fast travel, RPG_SPEC 11.2): the sign pole opens the route screen (prop_rpg_usable
    use_action travel); info_landmark lm_travel lets changelevel2 in and out, the player is put at
    info_target travel_arrive; the tiny trigger_transition keeps NPCs near the stop from riding along.
    changelevel2 needs a "connection" back: the new map must have a trigger_changelevel to the old one,
    so every stop gets untouchable ones (spawnflags 2) to the other tram maps."""
    x, y, z = arrive
    for i, dest in enumerate(d for d in TRAM_MAPS if d != this_map):
        m.add_brush_entity("trigger_changelevel", [m.make_box((x - 8 + 20 * i, y - 8, z + 440), (x + 8 + 20 * i, y + 8, z + 456),
                                                              "TOOLS/TOOLSTRIGGER")],
                           map=dest, landmark="lm_travel", spawnflags="2", targetname="tram_link_%d" % i)
    m.add_entity("info_landmark", (x, y, z + 24), targetname="lm_travel")
    m.add_entity("info_target", (x, y, z), targetname="travel_arrive", angles="0 %g 0" % yaw)
    m.add_brush_entity("trigger_transition", [m.make_box((x - 8, y - 8, z + 400), (x + 8, y + 8, z + 416),
                                                         "TOOLS/TOOLSTRIGGER")], targetname="lm_travel")
    # the plate on the pole: "TRAM" in the game's language, both faces
    px, py, pz = prop_pos
    for k, dyaw in enumerate((0, 180)):
        sign(m, (px, py, pz + 104), prop_yaw + dyaw, "#HL2RPM_Sign_Tram", size=20, color="250 215 120",
             name="tram_sign_%d" % k)
    return m.add_entity("prop_rpg_usable", prop_pos, targetname="tram_stop", model="models/props_c17/signpole001.mdl",
                        angles="0 %g 0" % prop_yaw, solid="6", use_action="travel", name="#HL2RPM_Hint_TramStop",
                        DefaultAnim="", disableshadows="0")


_SPECIAL_TARGETS = ("!self", "!player", "!activator", "!caller", "!pvsplayer", "!speechtarget", "!picker",
                    "!target1", "!target2", "!target3", "!target4", "player", "worldspawn")


def prune_missing_outputs(m, log=None):
    """Drop outputs aimed at entities that do not exist (left behind by the strips); a target is a
    targetname, a classname, a wildcard of either, or a !special name."""
    names = set(lower(e.get("targetname")) for e in m.entities() if e.get("targetname"))
    classes = set(lower(e.classname) for e in m.entities())

    def exists(t):
        t = lower(t)
        if not t or t.startswith("!") or t in _SPECIAL_TARGETS:
            return True
        if t in names or t in classes:
            return True
        if t.endswith("*"):
            p = t[:-1]
            return any(n.startswith(p) for n in names) or any(c.startswith(p) for c in classes)
        return False
    n = 0
    for e in m.entities():
        con = e.child("connections")
        if not con:
            continue
        keep = []
        for it in con.items:
            if isinstance(it, Node):
                keep.append(it)
                continue
            sep = "\x1b" if "\x1b" in it[1] else ","
            if exists(it[1].split(sep)[0]):
                keep.append(it)
            else:
                n += 1
        con.items = keep
    if log is not None:
        log.append("pruned %d outputs to missing entities" % n)
    return n
