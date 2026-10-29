#!/usr/bin/env python3
"""Build rp_c17_03_block7.vmf (house 7/3 of Kvartal 7) from the BSPSource decompile of retail
d1_trainstation_03.

    python prep_rp_c17_03_block7.py [--src <decompiled.vmf>] [--dst <out.vmf>]

Re-runnable: always starts from the untouched decompile (E:\\SourceModding\\tools\\decomp\\
d1_trainstation_03_d.vmf) and writes the map source into mapsrc. Do not hand-edit the output
VMF - change this script and run it again. The leaked Valve VMFs are only a reference.
(prep_rp_c17_03_block7_shell_wip.py holds the unfinished geometry stage - the house's outside,
the ground floor and the Old Town street - for a later pass.)

What it does (RPG vertical slice, hl2rpm_extras/docs/RPG_SPEC.md §11; Part V/01 MQ-00 s.7, MQ-01):
  * strips Gordon's story: the Civil Protection raid (break-in, rush and window cops, their goals,
    schedules, scenes, sounds and timers), the door bracer, the fleeing citizens and the peeker,
    the knock-out teleports, the APC, autosave, gordon_* globals, the changelevels to Valve's maps;
    keeps the life of the house - the TVs with the Breencast, the tenants on act-busy, their small
    scenes (TV comments, the consoling couple, window watchers), sounds, props;
  * transition lm_02_03 back to rp_c17_02_plaza (Valve's 02->03 place, the entrance);
  * the tenants of the slice: kv. 9 Amani and Ostap, kv. 4 Oswaldo (floor 1), kv. 14 Gary,
    kv. 17 Rosario and kv. 20 Yun Sohi (floor 2); kv. 11 on floor 1 is the hero's room: the bed
    (prop_rpg_usable bed_room11, sleep, home), the citizen's memo and the first-issue coupon on
    the nightstand; door number plates in the corridors;
  * MQ-00 stage 4: stepping into room 11 sets mq00_room11; sleeping there sets mq01_slept;
  * the world clock, the location trigger C17-03, baked-light fakes removed.
"""
import argparse
import os
import re
import sys

sys.path.insert(0, r"E:\SourceModding\HL2RPMSOURCE\.claude\skills\hl2rpm-hammer\scripts")
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from vmf import VMF, Node  # noqa: E402
from slice_kit import (npc_citizen, sign, prop, trigger_box, rename, strip_baked_fakes, timecycle,  # noqa: E402
                       lower, life, strip_original_life, prune_missing_outputs)

SRC = r"E:\SourceModding\tools\decomp\d1_trainstation_03_d.vmf"
DST = r"E:\XBLAH's modding tool\content\Mapbase\hl2rpm\mapsrc\rp_c17_03_block7.vmf"
MAPNAME = "rp_c17_03_block7"
PREV_MAP = "rp_c17_02_plaza"
LANDMARK = "lm_02_03"

F1 = 257        # floor 1 (z of the floor)
F2 = 385        # floor 2

# the raid and Gordon's route: names (regex, case-insensitive) and classes to delete
RAID_NAME = re.compile(
    r"(cop|police|raid|(?:^|_)rush|breakin|kickin|bracer|knockout|beatdown|beat_|beating|escape|flee|peeker|"
    r"window_cop|apc|enemyfinder|attic|cull_|blocker|anticipation|bootstomp|door_cops|doorholdnag|"
    r"autosave|miscount|crying|hallway_knockout|physicsexp_raid|template_raid|template_rush|"
    r"lcs_raidrunner|lounger_cower|couchguy_fear|staircops|2nd_rushcops|mark_raid)", re.I)
KEEP_CLASSES_BY_NAME = ("prop_door_rotating", "func_door", "func_door_rotating", "ai_relationship")
DELETE_CLASSES = ("npc_metropolice", "ai_goal_police", "aiscripted_schedule", "ai_script_conditions",
                  "npc_enemyfinder", "npc_furniture", "npc_vehicledriver", "prop_vehicle_jeep",
                  "trigger_teleport", "info_teleport_destination", "env_physexplosion", "logic_autosave",
                  "npc_template_maker", "env_fade")


def out_parts(v):
    sep = "\x1b" if "\x1b" in v else ","
    return v.split(sep), sep


def prune_outputs(m, removed):
    n = 0
    for e in m.entities():
        c = e.child("connections")
        if not c:
            continue
        keep = []
        for it in c.items:
            if isinstance(it, Node):
                keep.append(it)
                continue
            parts, _ = out_parts(it[1])
            if lower(parts[0]) in removed:
                n += 1
                continue
            keep.append(it)
        c.items = keep
        if not [it for it in c.items if not isinstance(it, Node)]:
            e.remove_child(c)
    return n


def strip_story(m, log):
    removed = set()
    for e in list(m.entities()):
        cls = lower(e.classname)
        nm = lower(e.get("targetname"))
        kill = cls in DELETE_CLASSES
        if nm and RAID_NAME.search(nm) and cls not in KEEP_CLASSES_BY_NAME:
            kill = True
        if cls == "env_global" and lower(e.get("globalstate")).startswith("gordon_"):
            kill = True
        if cls == "npc_citizen" and nm in ("citizen_doorbracer", "citizen_preraid_peeker", "fleeing_citizen_1",
                                            "fleeing_citizen_2", "npc_flr1_citizen4"):
            kill = True
        if cls == "ai_goal_actbusy" and nm == "actbusy_flr1_citizen4":
            kill = True
        if cls == "path_corner" and nm.startswith("apc_"):
            kill = True
        if kill:
            if nm:
                removed.add(nm)
            m.delete(e)
    n = prune_outputs(m, removed)
    # triggers and relays left without any output did only the raid's work
    dead = 0
    for e in list(m.entities()):
        cls = lower(e.classname)
        if cls in ("trigger_once", "trigger_multiple", "logic_relay", "logic_timer") and not e.child("connections"):
            nm = lower(e.get("targetname"))
            referenced = nm and any(nm == lower(out_parts(it[1])[0][0]) for x in m.entities()
                                    for c in [x.child("connections")] if c for it in c.props)
            if not referenced:
                m.delete(e)
                dead += 1
    log.append("story stripped: %d entities, %d outputs to them, %d empty triggers/relays" % (len(removed), n, dead))


def transitions(m, log):
    for e in list(m.entities()):
        nm = lower(e.get("targetname"))
        if nm == "landmark_trainstation_03-04":
            m.delete(e)
        elif nm == "landmark_trainstation_02-03":
            e.set("targetname", LANDMARK)
    kept = 0
    for e in list(m.entities("trigger_changelevel")):
        if lower(e.get("landmark")) == "landmark_trainstation_02-03" and lower(e.get("map")) == "d1_trainstation_02":
            e.set("map", PREV_MAP)
            e.set("landmark", LANDMARK)
            e.set("spawnflags", "0")
            kept += 1
        else:
            m.delete(e)
    assert kept == 1, kept
    starts = list(m.entities("info_player_start"))
    for e in starts:
        o = e.get("origin").split()
        if float(o[2]) > 200:
            m.delete(e)
    log.append("transition %s -> %s; the roof route to Valve's 04 removed" % (LANDMARK, PREV_MAP))


def tenants(m, log):
    # the tenants of the slice on Valve's marks (their poses fit the couches, the bed, the door), ours only:
    # Valve's tenants with their HL2 scenes went with slice_kit.strip_original_life
    G1 = "models/humans/group01/%s.mdl"
    for name, pos, yaw, mdl, seq, kw in (
            ("npc_oswaldo", (-4088, -4837, F1 - 1), 150, G1 % "male_09", "d1_t03_sit_couch_consoling", {}),
            ("cit_tenant_4b", (-4083, -4824, F1 - 1), 180, G1 % "female_01", "d1_t03_Sit_couch",
             dict(rpg_dialog="_bark_citizen_f", loot_displayname="#HL2RPM_Speaker_CitizenF")),
            ("npc_amani", (-4583, -4661, F1 - 1), 90, G1 % "female_02", "d1_t03_LookOutDoor", {}),
            ("npc_ostap", (-4672, -4897, F1 - 1), 180, G1 % "male_04", "drinker_sit_idle", {}),
            ("npc_rosario", (-4061, -4288, F2), 284, G1 % "female_06", "d1_t03_Sit_Bed", {}),
            ("cit_tenant_18", (-4184, -4193, F2), 105, G1 % "male_01", "laycouch1",
             dict(rpg_dialog="_bark_citizen", loot_displayname="#HL2RPM_Speaker_Citizen")),
            ("cit_tenant_19", (-4378, -4034, F2 - 1), 270, "models/humans/group02/male_03.mdl", "Sit_Ground",
             dict(rpg_dialog="_bark_citizen", loot_displayname="#HL2RPM_Speaker_Citizen"))):
        life(m, name, pos, yaw, mdl, seq, **kw)
    npc_citizen(m, "npc_gary", (-3900, -4880, F1), 90, "models/humans/group01/male_02.mdl")
    npc_citizen(m, "npc_yun_sohi", (-4100, -4800, F2), 90, "models/humans/group01/female_03.mdl")
    prop(m, "prop_physics", "models/props_junk/PushCart01a.mdl", (-4060, -4830, F2 + 30), "0 160 0")
    # door number plates on the corridor walls (floor 1: kv. 9, 11, 4, 14; floor 2: kv. 20, 17)
    for x, num, z in ((-4630, 9, F1), (-4400, 11, F1), (-4150, 4, F1), (-3900, 14, F1),
                      (-4030, 20, F2), (-4200, 17, F2)):
        y = -4612 if num != 17 else -4528
        yaw = 90 if num != 17 else 270
        sign(m, (x, y, z + 72), yaw, "#HL2RPM_Sign_Apt%d" % num, size=10, color="230 210 150")
    log.append("tenants: Oswaldo, Amani, Ostap, Rosario, Gary, Yun Sohi and three neighbours on Valve's marks; door plates")


def room11(m, log):
    # the hero's room: floor 1, the second flat from the west stairs
    bed = m.add_entity("prop_rpg_usable", (-4330, -4930, F1), targetname="bed_room11",
                       model="models/props_c17/furniturebed001a.mdl", angles="0 90 0", use_action="sleep",
                       home="1", sleep_hours="8", name="#HL2RPM_Hint_Bed", effects="set_flag=mq01_slept",
                       solid="6", spawnflags="0")
    prop(m, "prop_static", "models/props_c17/furnituredrawer001a.mdl", (-4290, -4968, F1), "0 90 0")
    m.add_entity("item_citizen_memo", (-4290, -4968, F1 + 36), angles="0 30 0")
    m.add_entity("item_ration_coupon", (-4282, -4956, F1 + 36), angles="0 80 0")
    trigger_box(m, "trigger_rpg", [((-4510, -4980, F1), (-4280, -4625, F1 + 110))], targetname="rpg_room11",
                effects="set_flag=mq00_room11", once="1", spawnflags="1")
    log.append("room 11: bed_room11, memo, coupon, mq00_room11 trigger")


def discovery(m, log):
    trigger_box(m, "trigger_rpg", [((-5240, -4960, 0), (-4900, -4100, 140))], targetname="rpg_discover_c17_03",
                discover="C17-03", once="1", spawnflags="1")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--src", default=SRC)
    ap.add_argument("--dst", default=DST)
    a = ap.parse_args()
    m = VMF.load(a.src)
    log = []
    strip_story(m, log)
    strip_original_life(m, log)
    transitions(m, log)
    tenants(m, log)
    room11(m, log)
    discovery(m, log)
    timecycle(m, (-5176, -4440, 40), 10.0)
    strip_baked_fakes(m, log)
    prune_missing_outputs(m, log)
    m.world.set("comment", "HL2RPM %s, generated by hl2rpm_extras/tools/prep_%s.py from the "
                "BSPSource decompile of d1_trainstation_03 - do not edit by hand" % (MAPNAME, MAPNAME))
    os.makedirs(os.path.dirname(a.dst), exist_ok=True)
    m.save(a.dst, backup=False)
    with open(os.path.splitext(a.dst)[0] + "_prep.log", "w", encoding="utf-8") as f:
        f.write("\n".join(log) + "\n")
    print("wrote", a.dst)
    print("\n".join(log))


if __name__ == "__main__":
    main()
