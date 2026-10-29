#!/usr/bin/env python3
"""RPG layer of rp_c17_08_opora (sector C17-08, the "Opora" Civil Protection precinct).

Called by make_opora.py right before it saves the VMF: build_phase_b(vmf, log). Content (Part V/04):
  GO-01 "Volunteers", scene 2 "Training and the test":
    gym          Sergeant Gannon at the shooting range (npc_gannon_opora, dialog npc_gannon_opora), the
                 "violator" plant with his hands tied at the east wall (npc_violator), a stunstick on a table
    barracks     Barney on a bunk (npc_barney); the hero's bunk becomes prop_rpg_usable "go01_bunk" - sleeping
                 there closes GO-01, afterwards it is the recruit's own bed (home)
  duty desk      the precinct terminal (prop_rpg_usable, use_action terminal, note cp_terminal)
  the world clock (8:00 - "tomorrow by eight") and the location C17-08 at the compound gate.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from slice_kit import npc_citizen, npc_cp, prop, trigger_box, timecycle, lower, tram_stop  # noqa: E402

FL0, FL2 = 8, 264           # floors of storey 0 and 2 (make_opora.FL)


def _find(m, classname, model=None, near=None, targetname=None, tol=24):
    for e in m.entities(classname):
        if targetname is not None and lower(e.get("targetname")) != targetname:
            continue
        if model is not None and lower(e.get("model")) != model:
            continue
        if near is not None:
            x, y, z = (float(v) for v in e.get("origin").split())
            if abs(x - near[0]) > tol or abs(y - near[1]) > tol or abs(z - near[2]) > tol:
                continue
        return e
    return None


def gym(m, log):
    npc_cp(m, "npc_gannon_opora", (430, 900, FL0), 270, rpg_faction="cp", weapondrawn="0")
    # the plant: a "violator" at the wall of the baton corner, hands behind his back (line-up idle)
    npc_citizen(m, "npc_violator", (686, 1250, FL0), 180, "models/humans/group01/male_06.mdl",
                rpg_faction="citizen")
    prop(m, "prop_dynamic", "models/props_c17/FurnitureTable001a.mdl", (600, 1180, FL0 + 18), "0 90 0")
    prop(m, "prop_dynamic", "models/weapons/w_stunbaton.mdl", (598, 1180, FL0 + 37), "0 20 90")
    log.append("gym: Gannon at the range, the violator at the east wall, a stunstick on the table")


def barracks(m, log):
    npc_cp(m, "npc_barney", (-300, 430, FL2), 60, model="models/barney.mdl", rpg_faction="cp")
    # the hero's bunk (spot_hero_bunk): the first bunk bed of the north row becomes usable
    bed = _find(m, "prop_static", model="models/props_forest/bunkbed.mdl", near=(-434, 586, FL2))
    if bed is None:
        log.append("WARNING: hero bunk (bunkbed at -434 586) not found")
        return
    bed.set("classname", "prop_rpg_usable")
    for k in ("disablevertexlighting", "disableselfshadowing", "ignorenormals", "screenspacefade",
              "lightmapresolutionx", "lightmapresolutiony", "uniformscale"):
        bed.delete(k)
    bed.set("targetname", "go01_bunk")
    bed.set("solid", "6")
    bed.set("use_action", "sleep")
    bed.set("name", "#HL2RPM_Hint_Bunk")
    bed.set("condition", "quest_stage go01_volunteers:3 | quest_state go01_volunteers:completed")
    bed.set("effects", "set_flag=go01_bunk")
    bed.set("sleep_hours", "8")
    bed.set("home", "0")
    log.append("barracks: Barney, the hero's bunk go01_bunk (sleep closes GO-01)")


def duty_desk(m, log):
    term = _find(m, "prop_dynamic", targetname="opora_terminal")
    if term is None:
        log.append("WARNING: opora_terminal not found")
        return
    term.set("classname", "prop_rpg_usable")
    term.set("use_action", "terminal")
    term.set("note", "cp_terminal")
    term.set("name", "#HL2RPM_Hint_Terminal")
    log.append("duty desk: the precinct terminal (cp_terminal)")


def world(m, log):
    timecycle(m, (-640, -600, 40), 8.0)
    tram_stop(m, "rp_c17_08_opora", (-2000, -600, -8), 0, (-2000, -450, 0), 270)
    # entering the compound through the pedestrian door: the location, and GO-01 knows the hero came
    trigger_box(m, "trigger_rpg", [((150, -360, -8), (480, -200, 120))], targetname="rpg_opora_enter",
                discover="C17-08", once="1", spawnflags="1")
    log.append("world clock 8:00, location C17-08 at the gate")


def build_phase_b(m, log):
    gym(m, log)
    barracks(m, log)
    duty_desk(m, log)
    world(m, log)
