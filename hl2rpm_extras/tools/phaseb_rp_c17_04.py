#!/usr/bin/env python3
"""RPG layer of rp_c17_04_oldtown (sector C17-04, the Old Town). Called by make_oldtown.py before it saves.

  LM-01 "Sign of the Lambda" (Part V/05): Lukich behind the counter of the watch workshop (npc_lukich),
        Leila in the cellar safehouse (npc_leila), the raid post on the bridge (npc_cp_victor5 + two
        units; walking onto the bridge with the package starts the raid dialog), Ernesto in the lock
        keeper's booth "Shlyuz-3" on the far quay (npc_ernesto)
  SM-01 "The Bathhouse" (Part V/06), scenes 1-2: Mateo on the bathhouse steps (npc_mateo), Kadar in the
        steam room (npc_kadar, trader "kadar"), Irma "Silence" at the steam room door (npc_tishina)
  street life: citizens marked rpg_stream 1 (streamed by distance, RPG_SPEC 11.2)
  the world clock (10:00), locations C17-04 (the arch) and C17-14 (the booth)
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from slice_kit import npc_citizen, npc_cp, logic_rpg, trigger_box, timecycle, tram_stop  # noqa: E402

FL0 = 8
CELLAR = -136


def sit(m, name, pos, yaw, seq):
    m.add_entity("scripted_sequence", pos, targetname="ss_" + name, angles="0 %g 0" % yaw, m_iszEntity=name,
                 m_iszIdle=seq, m_iszPlay=seq, m_iszPostIdle=seq, m_fMoveTo="4", m_flRadius="0", m_flRepeat="0",
                 spawnflags="144", onplayerdeath="0")


def lambda_line(m, log):
    npc_citizen(m, "npc_lukich", (-768, 664, FL0), 270, "models/humans/group01/male_04.mdl", rpg_trader="lukich")
    npc_citizen(m, "npc_leila", (-770, 580, CELLAR), 90, "models/humans/group02/female_03.mdl", rpg_faction="lambda")
    # the raid on the bridge: Victor-5 at the north end, two units on the deck
    npc_cp(m, "npc_cp_victor5", (608, -30, 0), 90)
    npc_cp(m, "npc_cp_raid1", (560, -260, -8), 100)
    npc_cp(m, "npc_cp_raid2", (656, -420, -8), 80)
    tr = trigger_box(m, "trigger_rpg", [((512, -150, -8), (704, -40, 128))], targetname="rpg_lm01_raid",
                     condition="quest_stage lm01_lambda_sign:1, !lm01_route", once="0", spawnflags="1")
    logic_rpg(m, (608, 40, 60), "rpg_lm01_raid_talk")
    m.add_output(tr, "OnPass", "rpg_lm01_raid_talk", "StartDialog", "npc_cp_victor5", 0, -1)
    npc_citizen(m, "npc_ernesto", (1392, -1196, FL0), 90, "models/humans/group02/male_06.mdl", rpg_faction="lambda")
    log.append("LM-01: Lukich, Leila (cellar), the raid post Victor-5 + 2 on the bridge, Ernesto in Shlyuz-3")


def shareholders(m, log):
    npc_citizen(m, "npc_mateo", (516, 484, 0), 270, "models/humans/group02/male_08.mdl", rpg_faction="shareholders")
    npc_citizen(m, "npc_kadar", (560, 772, FL0), 270, "models/humans/group02/male_09.mdl", rpg_faction="shareholders",
                rpg_trader="kadar")
    sit(m, "npc_kadar", (560, 772, FL0), 270, "d1_t02_Plaza_Sit01_Idle")
    npc_citizen(m, "npc_tishina", (466, 716, FL0), 300, "models/humans/group03/female_06.mdl", rpg_faction="shareholders")
    # the bathhouse door is locked until Mateo lets the hero in (sm01_in; after a reload too)
    for e in m.entities("prop_door_rotating"):
        if e.get("targetname") == "bath_door":
            e.set("spawnflags", str(int(e.get("spawnflags") or 0) | 2048))
    la = logic_rpg(m, (516, 440, 60), "rpg_bath_open", condition="sm01_in", test_on_spawn="1")
    m.add_output(la, "OnTrue", "bath_door", "Unlock", "", 0, -1)
    log.append("SM-01: Mateo at the door, Kadar in the steam room, Irma Silence")


def street(m, log):
    models = ["models/humans/group01/male_0%d.mdl" % i for i in (1, 2, 3, 6, 7)] + \
             ["models/humans/group01/female_0%d.mdl" % i for i in (1, 2, 4, 6)]
    spots = [(-1640, 300, 20), (-1210, 450, 250), (-430, 120, 160), (90, 450, 300), (940, 210, 200),
             (1320, 430, 270), (1560, 430, 250), (1760, 300, 190), (1950, 60, 170)]
    for i, (x, y, yaw) in enumerate(spots):
        npc_citizen(m, "cit_oldtown_%d" % i, (x, y, 0), yaw, models[i % len(models)], rpg_stream="1")
    log.append("street: %d citizens streamed by distance (rpg_stream 1)" % len(spots))


def world(m, log):
    timecycle(m, (-1900, 224, 40), 10.0)
    tram_stop(m, "rp_c17_04_oldtown", (-1500, 300, 0), 0, (-1500, 470, 0), 270)
    # an arrested player comes to at the tram stop (rpg_law.cpp)
    m.add_entity("info_target", (-1460, 300, 0), targetname="rpg_arrest_release", angles="0 0 0")
    trigger_box(m, "trigger_rpg", [((-2048, 64, -8), (-1900, 512, 160))], targetname="rpg_oldtown_enter",
                discover="C17-04", once="1", spawnflags="1")
    trigger_box(m, "trigger_rpg", [((1240, -1280, -8), (1560, -1024, 160))], targetname="rpg_lock3_enter",
                discover="C17-14", once="1", spawnflags="1")
    log.append("world clock 10:00, locations C17-04 (arch) and C17-14 (Shlyuz-3)")


def build_phase_b(m, log):
    lambda_line(m, log)
    shareholders(m, log)
    street(m, log)
    world(m, log)
