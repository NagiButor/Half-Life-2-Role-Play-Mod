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
    """A slice citizen: stands where placed, the dialog key is its targetname."""
    keys = dict(targetname=name, angles="0 %g 0" % yaw, model=model, citizentype="4",
                expressiontype="Random", spawnflags=CITIZEN_FLAGS, rpg_faction="citizen")
    keys.update(kw)
    return m.add_entity("npc_citizen", pos, **keys)


def npc_cp(m, name, pos, yaw, **kw):
    keys = dict(targetname=name, angles="0 %g 0" % yaw, spawnflags=CP_FLAGS, manhacks="0",
                weapondrawn="0", additionalequipment="weapon_stunstick", rpg_faction="cp")
    keys.update(kw)
    return m.add_entity("npc_metropolice", pos, **keys)


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
    return m.add_brush_entity("func_brush", sol, targetname=name, spawnflags="2", Solidity="2",
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
    return m.add_entity("prop_rpg_usable", prop_pos, targetname="tram_stop", model="models/props_c17/signpole001.mdl",
                        angles="0 %g 0" % prop_yaw, solid="6", use_action="travel", name="#HL2RPM_Hint_TramStop",
                        DefaultAnim="", disableshadows="0")
