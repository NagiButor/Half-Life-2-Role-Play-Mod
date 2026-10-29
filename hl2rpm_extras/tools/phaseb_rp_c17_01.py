#!/usr/bin/env python3
"""Phase B of rp_c17_01_trainstation (called by prep_rp_c17_01_trainstation.py): the people of the station
and their map hooks (Part V/01 MQ-00 scenes 1-5, MQ-01 scene 3, MQ-02). Valve's people are gone
(slice_kit.strip_original_life): everyone here is ours and answers E - with an own dialog, a shared one
(rpg_dialog _bark_*) or the faction's bark; CP units stand unscripted at their posts, so the law system
(rpg_law.cpp) can send them to check the player and back.

  scene 1  dream: the G-Man voice is a dialog (spot_gman_dream) started at 21 s; its last line
           fires sp00_relay_coupling (clank, fade, wake up); a fallback wakes at 120 s
  scene 2  train car: npc_oldman_train, npc_yun_sohi (frozen on the car floor)
  scene 3  arrival platform: npc_cp_xray7 + an approach trigger across the platform; passengers with
           their suitcases, a unit at the concourse
  scene 4  the yard by platform 3: npc_savva (Valve's sweeping vortigaunt, kept with his broom scripts)
           + savva_collar_shock; brigadier npc_lotta with the crates (MQ-01 scene 3, the blue crate)
  scene 5  the break room is the arrivals registration hall: windows 1-6, Tamara at 4, two clerks, the
           queue, medic Mitra; people waiting on the benches
  customs  the old customs gate: npc_cp_customs sends arrivals to registration and the registered to the
           exit (the corridor -> the CP duty room -> the exit checkpoint); the restricted razor-train gate
           has its own unit
  exit     the exit checkpoint (storage room): npc_cp_checkpoint and a gate until
           checkpoint_trainstation_passed, sergeant Gannon's recruitment desk (GO-01); locker 113;
           an arrested player comes to in the duty room (rpg_arrest_release)
"""
from slice_kit import (tram_stop, npc_citizen, npc_cp, sign, prop, logic_rpg, trigger_box, clip_brush, rename,
                       strip_baked_fakes, timecycle, spot, lower, life, prune_missing_outputs)

TRAIN = "intro_train_2"
FLOOR = -32

# registration hall: desks along the west wall of the break room, windows 1 (south) .. 6 (north)
REG_X_DESK = -5548
REG_X_CLERK = -5594
REG_YS = [-1500, -1380, -1260, -1140, -1020, -900]

M = "models/humans/group01/male_%02d.mdl"
F = "models/humans/group01/female_%02d.mdl"
M2 = "models/humans/group02/male_%02d.mdl"
F2 = "models/humans/group02/female_%02d.mdl"

SPK_CIT, SPK_CIT_F = "#HL2RPM_Speaker_Citizen", "#HL2RPM_Speaker_CitizenF"
SPK_PASS, SPK_PASS_F = "#HL2RPM_Speaker_Passenger", "#HL2RPM_Speaker_PassengerF"
SPK_CP = "#HL2RPM_Speaker_CP"


def _outputs(m, ent, outs):
    for o in outs:
        m.add_output(ent, *o)


def dream(m, log):
    rel = None
    for e in m.entities("logic_relay"):
        if lower(e.get("targetname")) == "sp00_relay_start":
            rel = e
    assert rel is not None
    con = rel.child("connections")
    keep = []
    for it in con.props:
        parts = it[1].split("\x1b" if "\x1b" in it[1] else ",")
        tgt, inp = lower(parts[0]), lower(parts[1])
        if tgt == "rpg_sp00" and inp == "setflag":
            continue                                   # the dialog sets mq00_started at its end
        if tgt == "sp00_relay_coupling":
            sep = "\x1b" if "\x1b" in it[1] else ","
            parts[3] = "120"                           # fallback only: the dialog wakes the player
            it[1] = sep.join(parts)
        keep.append(it)
    con.items = keep
    for e in m.entities("logic_relay"):
        if lower(e.get("targetname")) == "sp00_relay_voice":
            e.child("connections").items = []
            m.add_output(e, "OnTrigger", "rpg_sp00", "StartDialog", "spot_gman_dream", 0, 1)
    log.append("dream: G-Man dialog at 21 s, wake on its last line (fallback 120 s)")


def train(m, log):
    # A child's traces ignore its root move parent (util_shared.cpp), so a parented NPC never stands on
    # the car: MOVETYPE_STEP gravity takes him through the floor onto the track bed (measured z -64
    # with the floor at 16). The passengers are frozen in MOVETYPE_NONE right after spawn and put
    # back on the floor in the car's frame; the car carries them. Valve's TrainRide poses exist only
    # in male_ss.mdl - the woman sits with a plaza bench pose.
    car = next(e for e in m.entities("func_tracktrain") if e.get("targetname") == TRAIN)
    cx, cy, cz = (float(v) for v in car.get("origin").split())
    common = dict(parentname=TRAIN, lightingoriginhack="lighting_traincar", spawnflags="17825792",
                  physdamagescale="0.0001", ResponseContext="train_passenger:1")
    la = m.add_entity("logic_auto", (cx, cy, cz + 200), spawnflags="1")
    for name, pos, yaw, model, seq in (
            ("npc_oldman_train", (-5360, -1822, 16), 270, M % 9, "d1_t01_TrainRide_Sit_Idle"),
            ("npc_yun_sohi", (-5231, -1890, 16), 90, F % 3, "d1_t02_Plaza_Sit01_Idle")):
        npc_citizen(m, name, pos, yaw, model, **common)
        m.add_entity("scripted_sequence", pos, targetname="ss_" + name, angles="0 %g 0" % yaw, parentname=TRAIN,
                     m_iszEntity=name, m_iszIdle=seq, m_iszPlay=seq, m_iszPostIdle=seq, m_fMoveTo="0",
                     m_flRadius="0", m_flRepeat="0", spawnflags="144", onplayerdeath="0")
        local = "%g %g %g" % (pos[0] - cx, pos[1] - cy, pos[2] - cz + 0.05)
        m.add_output(la, "OnMapSpawn", name, "SetMoveType", "0", 0, 1)
        m.add_output(la, "OnMapSpawn", name, "SetLocalOrigin", local, 0.02, 1)
        m.add_output(la, "OnMapSpawn", name, "SetLocalAngles", "0 %g 0" % yaw, 0.02, 1)
    log.append("train passengers (frozen on the car floor, seated poses)")


def platform(m, log):
    npc_cp(m, "npc_cp_xray7", (-4380, -2040, FLOOR), 180)
    trigger_box(m, "trigger_dialog_start", [((-4570, -2190, -40), (-4530, -1920, 100))],
                npc_name="npc_cp_xray7", trigger_once="1", npc_approach="1", approach_distance="80",
                approach_timeout="6", spawnflags="1")
    # people of the same train with their suitcases (Valve's luggage marks), a unit at the concourse
    life(m, "cit_arrival_1", (-3993, -1920, FLOOR), 0, M % 2, "d1_t01_Luggage_Idle", rpg_dialog="_bark_citizen",
         loot_displayname=SPK_PASS, additionalequipment="weapon_citizensuitcase")
    life(m, "cit_arrival_2", (-3984, -1828, FLOOR), 350, F % 1, "LineIdle02", rpg_dialog="_bark_citizen_f",
         loot_displayname=SPK_PASS_F)      # (the luggage idles exist only for men)
    life(m, "cp_concourse", (-4023, -1673, FLOOR), 330, None, cls="cp", rpg_dialog="_bark_cp", loot_displayname=SPK_CP)
    log.append("platform: X-ray-7 + approach trigger, two passengers, a concourse unit")


def yard(m, log):
    # the sweeping vortigaunt is Savva (Valve's vort with his broom scripts, kept by the strip)
    n = rename(m, "vort_sweepy", "npc_savva")
    for e in m.entities("npc_vortigaunt"):
        if lower(e.get("targetname")) == "npc_savva":
            e.set("rpg_faction", "vortigaunts")
    shock = m.add_entity("logic_relay", (-4140, -2760, 40), targetname="savva_collar_shock", spawnflags="0")
    m.add_entity("env_spark", (-4140, -2796, 36), targetname="savva_collar_spark", parentname="npc_savva",
                 MaxDelay="0", Magnitude="2", TrailLength="1", spawnflags="0")
    m.add_entity("ambient_generic", (-4140, -2796, 40), targetname="savva_collar_snd",
                 message="ambient/energy/zap1.wav", SourceEntityName="npc_savva", spawnflags="48",
                 health="8", radius="900", pitch="100", pitchstart="100")
    _outputs(m, shock, [("OnTrigger", "savva_collar_spark", "SparkOnce", "", 0, -1),
                        ("OnTrigger", "savva_collar_snd", "PlaySound", "", 0, -1),
                        ("OnTrigger", "savva_collar_spark", "SparkOnce", "", 0.25, -1)])
    # brigadier Lotta and the crates by platform 3
    npc_citizen(m, "npc_lotta", (-4700, -2650, FLOOR), 90, F2 % 2)
    for i, (x, y, z) in enumerate([(-4640, -2700, FLOOR), (-4640, -2700, FLOOR + 40), (-4596, -2702, FLOOR),
                                   (-4760, -2716, FLOOR)]):
        prop(m, "prop_physics", "models/props_junk/wood_crate001a.mdl", (x, y, z), "0 %d 0" % (i * 7))
    prop(m, "prop_physics", "models/props_junk/wood_crate002a.mdl", (-4560, -2660, FLOOR), "0 12 0",
         targetname="crate_blue", rendercolor="90 130 210")
    prop(m, "prop_physics", "models/props_junk/PushCart01a.mdl", (-4780, -2620, FLOOR + 30), "0 90 0")
    # the yard door has its unit (Valve's door cop mark)
    life(m, "cp_yard_door", (-3868, -2912, FLOOR), 180, None, cls="cp", rpg_dialog="_bark_cp", loot_displayname=SPK_CP)
    log.append("yard: Savva (%d refs renamed), Lotta and the crates, the yard door unit" % n)


def registration_hall(m, log):
    staffed = {2: ("npc_cit_clerk2", M % 6), 4: ("npc_tamara", F % 4), 5: ("npc_cit_clerk5", F % 2)}
    for i, y in enumerate(REG_YS):
        win = i + 1
        prop(m, "prop_static", "models/props_wasteland/controlroom_desk001b.mdl", (REG_X_DESK, y, FLOOR), "0 0 0")
        prop(m, "prop_physics", "models/props_lab/monitor01a.mdl", (REG_X_DESK - 6, y + 18, FLOOR + 30), "0 0 0")
        prop(m, "prop_physics", "models/props_c17/chair_office01a.mdl", (REG_X_CLERK - 4, y - 10, FLOOR + 1), "0 0 0")
        if win < 6:
            # partitions between the windows
            prop(m, "prop_static", "models/props_combine/combine_barricade_short01a.mdl",
                 (REG_X_DESK + 6, y + 60, FLOOR), "0 90 0")
        sign(m, (REG_X_DESK + 24, y, FLOOR + 92), 0, "#HL2RPM_Sign_Window%d" % win, size=34,
             color="140 200 255")
        if win in staffed:
            nm, mdl = staffed[win]
            if nm == "npc_tamara":
                npc_citizen(m, nm, (REG_X_CLERK, y, FLOOR), 0, mdl)
            else:
                npc_citizen(m, nm, (REG_X_CLERK, y, FLOOR), 0, mdl, rpg_dialog="_bark_clerk",
                            loot_displayname="#HL2RPM_Speaker_Clerk")
        else:
            sign(m, (REG_X_DESK + 24, y, FLOOR + 72), 0, "#HL2RPM_Sign_Closed", size=24, color="220 120 90")
    sign(m, (REG_X_DESK + 30, -1200, FLOOR + 150), 0, "#HL2RPM_Sign_Registration", size=70, color="235 225 200")
    # people in line at the open windows 2 and 5 (not at Tamara's: the player goes there)
    for k, (x, y, mdl, seq, dlg, spk) in enumerate([
            (REG_X_DESK + 70, -1380, M % 4, "LineIdle01", "_bark_queue", SPK_CIT),
            (REG_X_DESK + 114, -1380, F % 1, "LineIdle03", "_bark_queue", SPK_CIT_F),
            (REG_X_DESK + 70, -1020, M % 5, "LineIdle02", "_bark_queue", SPK_CIT)]):
        life(m, "cit_queue_%d" % (k + 1), (x, y, FLOOR), 180, mdl, seq, rpg_dialog=dlg, loot_displayname=spk)
    # medic Mitra's corner (north-west): cot, table, kits, sign
    prop(m, "prop_static", "models/props_c17/furniturebed001a.mdl", (-5520, -700, FLOOR), "0 0 0")
    prop(m, "prop_static", "models/props_c17/furnituretable002a.mdl", (-5410, -712, FLOOR), "0 90 0")
    prop(m, "prop_physics", "models/items/healthkit.mdl", (-5410, -712, FLOOR + 22), "0 30 0")
    prop(m, "prop_physics", "models/healthvial.mdl", (-5398, -700, FLOOR + 22), "0 0 0")
    prop(m, "prop_physics", "models/props_lab/clipboard.mdl", (-5420, -724, FLOOR + 22), "0 80 0")
    npc_citizen(m, "npc_mitra", (-5450, -770, FLOOR), 270, M % 3)
    sign(m, (-5470, -672, FLOOR + 96), 270, "#HL2RPM_Sign_Medpoint", size=40, color="120 230 140")
    # waiting room: Valve's bench / clock / lean marks (their poses fit the furniture), ours on them
    for name, pos, yaw, mdl, seq, dlg, spk in [
            ("cit_bench_1", (-5184, -1390, -28), 90, M2 % 5, "sitcouchknees1", "_bark_citizen", SPK_CIT),
            ("cit_clock", (-5251, -1296, -32), 270, M % 8, "d1_t01_BreakRoom_WatchClock_Sit", "_bark_citizen", SPK_CIT),
            ("cit_bench_2", (-5018, -656, -26.4), 270, M2 % 2, "d1_t01_BreakRoom_Sit01_Idle", "_bark_citizen", SPK_CIT),
            ("cit_lean", (-5297, -598, -28), 200, M2 % 6, "d1_t01_BreakRoom_Sit02", "_bark_citizen", SPK_CIT),
            ("cit_waiting_1", (-4891, -895, -28), 15, M % 1, "LineIdle01", "_bark_citizen", SPK_CIT),
            ("cit_waiting_2", (-4880, -841, -28), 350, M2 % 4, "d1_t01_BreakRoom_WatchBreen", "_bark_citizen", SPK_CIT)]:
        life(m, name, pos, yaw, mdl, seq, rpg_dialog=dlg, loot_displayname=spk)
    # by the fence of the razor-train platform: a woman looking for somebody (Valve's chain-link mark)
    life(m, "cit_fence", (-3960, -1438, -32), 270, F % 6, "d1_t01_Clutch_Chainlink_Idle", rpg_dialog="_bark_citizen_f",
         loot_displayname=SPK_CIT_F)
    log.append("registration hall: 6 windows, Tamara at 4, 2 clerks, queue, Mitra; waiting room people")


def customs(m, log):
    """The old customs gate: no more herding - a unit who tells where to go, the restricted gate guarded."""
    npc_cp(m, "npc_cp_customs", (-4216, -852, FLOOR), 180)
    sign(m, (-4180, -852, FLOOR + 110), 180, "#HL2RPM_Sign_ToExit", size=28, color="140 200 255")
    life(m, "cp_razor_gate", (-4280, -636, FLOOR), 270, None, cls="cp", rpg_dialog="_bark_cp", loot_displayname=SPK_CP)
    # the duty room: a unit at the desk tells the way to the checkpoint; the arrested come to here
    npc_cp(m, "npc_cp_dutyroom", (-3420, -360, FLOOR), 180)
    spot(m, "rpg_arrest_release", (-3440, -250, FLOOR), 270)
    log.append("customs: Customs unit, razor-gate unit, duty-room unit, arrest release point")


def checkpoint(m, log):
    # beside the doorway into the courtyard, not in it (standing at -3296 -84 he blocked the 56-unit opening
    # with his body even after the pass was checked)
    npc_cp(m, "npc_cp_checkpoint", (-3300, -8, FLOOR), 225)
    npc_cp(m, "npc_gannon", (-3650, -62, FLOOR), 0)   # GO-01 recruitment (dialog npc_gannon)
    # the doorway into the courtyard stays shut until the pass is checked
    clip_brush(m, "cp_exit_block", (-3290, -116, -32), (-3264, -52, 80))
    rpg = logic_rpg(m, (-3330, -84, 40), "rpg_cp_exit", condition="checkpoint_trainstation_passed",
                    test_on_spawn="1")
    m.add_output(rpg, "OnTrue", "cp_exit_block", "Disable", "", 0, -1)
    m.add_output(rpg, "OnTrue", "rpg_cp_exit_ask", "Disable", "", 0, -1)
    # walking up to the doorway without the check: the unit stops the player
    tr = trigger_box(m, "trigger_rpg", [((-3420, -150, -32), (-3300, -20, 80))], targetname="rpg_cp_exit_ask",
                     condition="!checkpoint_trainstation_passed", once="0", spawnflags="1")
    logic_rpg(m, (-3350, -84, 60), "rpg_cp_exit_talk")
    m.add_output(tr, "OnPass", "rpg_cp_exit_talk", "StartDialog", "npc_cp_checkpoint", 0, -1)
    sign(m, (-3300, -20, FLOOR + 100), 180, "#HL2RPM_Sign_ExitCheckpoint", size=30, color="140 200 255")
    log.append("checkpoint: CP, gate (cp_exit_block), trigger; Gannon at his desk")


def locker_113(m, log):
    # the old man's letter: the locker opens once he told about it (flag mq00_oldman_letter)
    for e in m.entities("func_door_rotating"):
        if lower(e.get("targetname")) == "locker_113":
            o = e.get("origin").split()
            x, y = float(o[0]), float(o[1])
            m.add_entity("item_letter_113", (x + 10, y + 2, 2), angles="0 90 0")
            chk = logic_rpg(m, (x + 30, y, 40), "rpg_locker_113", condition="mq00_oldman_letter")
            m.add_output(e, "OnLockedUse", "rpg_locker_113", "Test", "", 0, -1)
            m.add_output(chk, "OnTrue", "locker_113", "Unlock", "", 0, -1)
            m.add_output(chk, "OnTrue", "locker_113", "Open", "", 0.1, -1)
            log.append("locker 113: letter inside, opens with mq00_oldman_letter")
            return
    raise SystemExit("locker_113 not found")


def clock_and_fakes(m, log):
    timecycle(m, (-4825, -2085, 120), 8.0)
    tram_stop(m, "rp_c17_01_trainstation", (-3480, 60, FLOOR), 270, (-3480, 176, FLOOR), 270)
    # the dream set (far west, the Black Mesa flash) keeps its beams
    strip_baked_fakes(m, log, keep_box=((-16384, -16384, -16384), (-8000, -8000, 16384)))


def build_phase_b(m, log):
    dream(m, log)
    train(m, log)
    platform(m, log)
    yard(m, log)
    registration_hall(m, log)
    customs(m, log)
    checkpoint(m, log)
    locker_113(m, log)
    clock_and_fakes(m, log)
    prune_missing_outputs(m, log)
