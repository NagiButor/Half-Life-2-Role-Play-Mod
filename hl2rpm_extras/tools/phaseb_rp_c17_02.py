#!/usr/bin/env python3
"""Phase B of rp_c17_02_plaza (called by prep_rp_c17_02_plaza.py): the people of the station plaza.

  SQ-01      npc_cp_union12 by the column after the turnstiles: the dialog drops the can
             (union12_drop_can -> the can spawns from a template at his hand and falls) and bins it
             (union12_can_binned); he calls the registered player once (trigger_dialog_start)
  MQ-00 s6   npc_victor6 at the arch into Kvartal 7: stops the registered newcomer once
  MQ-01 s3   the Labour Bureau in the Administration annex: npc_bureau_clerk at the counter,
             a guard, a short queue; Ruzhena at the reception, Lorch behind his locked door
  ambience   posts at the Prospekt gate (Line-6) and the Opora gate, the kiosk trader, the
             newspaper seller; the world clock; baked-light fakes removed
"""
from slice_kit import (npc_citizen, npc_cp, sign, prop, logic_rpg, trigger_box, strip_baked_fakes,
                       timecycle, lower)


def can_scene(m, log):
    npc_cp(m, "npc_cp_union12", (-3739, -1045, 64), 120)
    can = prop(m, "prop_physics", "models/props_junk/PopCan01a.mdl", (-3722, -1024, 116), "0 30 0",
               targetname="can_union12", spawnflags="256")
    m.add_entity("point_template", (-3722, -1024, 140), targetname="can_union12_tpl", Template01="can_union12",
                 spawnflags="2")
    drop = m.add_entity("logic_relay", (-3740, -1060, 140), targetname="union12_drop_can", spawnflags="1")
    m.add_output(drop, "OnTrigger", "can_union12_tpl", "ForceSpawn", "", 0.4, 1)
    m.add_entity("ambient_generic", (-3728, -696, 90), targetname="union12_bin_snd",
                 message="physics/metal/soda_can_impact_hard1.wav", spawnflags="49", health="7",
                 radius="900", pitch="100", pitchstart="100")
    binned = m.add_entity("logic_relay", (-3740, -1080, 140), targetname="union12_can_binned", spawnflags="1")
    m.add_output(binned, "OnTrigger", "can_union12", "Kill", "", 0.6, 1)
    m.add_output(binned, "OnTrigger", "union12_bin_snd", "PlaySound", "", 0.5, 1)
    # he calls the player walking past the column (only once a pass is in the pocket)
    tr = trigger_box(m, "trigger_rpg", [((-3900, -1240, 64), (-3560, -860, 220))], targetname="rpg_union12_call",
                     condition="mq00_registered, !sq01_result", once="1", spawnflags="1")
    logic_rpg(m, (-3760, -1100, 150), "rpg_union12")
    m.add_output(tr, "OnPass", "rpg_union12", "StartDialog", "npc_cp_union12", 0.2, 1)
    log.append("SQ-01: Union-12, the can template, binning relay, call trigger")


def victor6(m, log):
    npc_cp(m, "npc_victor6", (-5236, -4104, 0), 90)
    tr = trigger_box(m, "trigger_rpg", [((-5420, -4040, 0), (-5060, -3860, 160))], targetname="rpg_victor6_call",
                     condition="mq00_registered, !mq00_victor6_met", once="1", spawnflags="1")
    logic_rpg(m, (-5236, -4000, 120), "rpg_victor6")
    m.add_output(tr, "OnPass", "rpg_victor6", "StartDialog", "npc_victor6", 0.2, 1)
    log.append("MQ-00 s6: Victor-6 at the arch + call trigger")


def annex(m, log):
    npc_citizen(m, "npc_bureau_clerk", (-2024, -3184, 0), 90, "models/humans/group01/female_07.mdl")
    npc_cp(m, "npc_cp_bureau_guard", (-2296, -2990, 0), 300)
    npc_citizen(m, "cit_bureau_queue_1", (-2160, -3040, 0), 0, "models/humans/group01/male_05.mdl")
    npc_citizen(m, "cit_bureau_queue_2", (-2210, -3040, 0), 0, "models/humans/group01/female_01.mdl")
    npc_citizen(m, "npc_ruzhena", (-2240, -3478, 0), 90, "models/humans/group01/female_06.mdl")
    npc_citizen(m, "npc_lorch", (-1980, -3420, 0), 180, "models/humans/group02/male_08.mdl")
    sign(m, (-2216, -2944, 112), 90, "#HL2RPM_Sign_LaborBureau", size=36, color="235 225 200")
    log.append("annex: bureau clerk, guard, queue, Ruzhena, Lorch, sign")


def posts(m, log):
    # Valve's metropolice_plaza stands at z 1.00001 (decompile rounding): "stuck in wall" on every load
    for e in m.entities("npc_metropolice"):
        if lower(e.get("targetname")) == "metropolice_plaza":
            x, y, z = e.get("origin").split()
            e.set("origin", "%s %s %g" % (x, y, float(z) + 8))
    npc_cp(m, "npc_cp_line6", (-1072, -2008, 0), 180)
    npc_cp(m, "npc_cp_gate_opora", (-2216, -4936, 0), 135)
    # Ilona Sanders of the "Gratitude" kiosk (Part V/04 AD-03): dialog npc_ilona -> barter screen
    npc_citizen(m, "npc_ilona", (-2592, -2790, 2), 90, "models/humans/group01/female_02.mdl",
                rpg_trader="kiosk_blagodarnost")
    npc_citizen(m, "cit_newspaper", (-2760, -2280, 0), 30, "models/humans/group01/male_04.mdl")
    log.append("posts: Line-6, Opora gate, kiosk trader Ilona, newspaper seller")


def yard(m, log):
    """MQ-01 s2: the first ration issue in the yard of house 7/3 (the alley in front of it)."""
    prop(m, "prop_dynamic", "models/props_combine/Combine_Dispenser.mdl", (-5546, -3900, 62), "0 0 0",
         targetname="yard_dispenser")
    sign(m, (-5540, -3900, 132), 0, "#HL2RPM_Sign_Ration", size=18, color="140 200 255")
    npc_cp(m, "npc_cp_stik4", (-5490, -3970, 0), 90)
    for k, (x, y, mdl) in enumerate([(-5450, -3890, "models/humans/group01/female_01.mdl"),
                                     (-5400, -3884, "models/humans/group01/male_08.mdl"),
                                     (-5352, -3880, "models/humans/group01/male_09.mdl")]):
        npc_citizen(m, "cit_yard_queue_%d" % (k + 1), (x, y, 0), 180, mdl)
    sign(m, (-5330, -4150, 150), 90, "#HL2RPM_Sign_House73", size=26, color="230 210 150")
    log.append("yard of 7/3: dispenser, Stik-4, queue, house sign")


def build_phase_b(m, log):
    can_scene(m, log)
    victor6(m, log)
    yard(m, log)
    annex(m, log)
    posts(m, log)
    timecycle(m, (-2304, -2048, 200), 9.0)
    strip_baked_fakes(m, log)
