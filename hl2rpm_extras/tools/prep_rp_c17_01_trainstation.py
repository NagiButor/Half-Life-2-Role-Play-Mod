#!/usr/bin/env python3
"""Build rp_c17_01_trainstation.vmf from the BSPSource decompile of retail d1_trainstation_01.

    python prep_rp_c17_01_trainstation.py [--src <decompiled.vmf>] [--dst <out.vmf>]

Re-runnable: always starts from the untouched decompile (E:\\SourceModding\\tools\\decomp\\
d1_trainstation_01_d.vmf) and writes the map source into mapsrc. Do not hand-edit the output
VMF - change this script and run it again (Hammer edits of the output are lost on the next run).

What it does (phase A of the RPG vertical slice, see hl2rpm_extras/docs/RPG_SPEC.md §11 and
hl2rpm_extras/content/integration/map_rp_c17_01_trainstation.md):
  * strips the Gordon/Barney/Kleiner story scripting, Valve titles, credits, the G-Man intro
    and the HL2 tutorial hints, keeps the ambience (Breencast screens, queue, citizens,
    patrols, scanners, sounds, lights, props);
  * SP-00 dream: black void with stars + a flash of the Black Mesa test chamber, then the
    player wakes in the train (info_player_start is inside the train car); hooks
    sp00_relay_* for the dream text lines (phase B);
  * the train ride into the station stays (func_tracktrain intro_train_*);
  * transitions: info_landmark / trigger_transition / trigger_changelevel lm_01_02 to
    rp_c17_02_plaza, no other changelevels;
  * walkable exit: a doorway from the storage room (city-exit checkpoint) into the courtyard,
    so the plaza is reachable without stacking crates;
  * ambient NPC templates spawn on map spawn (also when the player arrives from the plaza);
  * named spots (info_target spot_*) for the slice NPCs, locker 88 / 113, the
    registration hall windows 1-6 (window 4 = Tamara), toilets with a lambda graffiti,
    janitors' closet, trigger_rpg location discovery volumes, logic_rpg for flags.
"""
import argparse
import math
import os
import sys

sys.path.insert(0, r"E:\SourceModding\HL2RPMSOURCE\.claude\skills\hl2rpm-hammer\scripts")
from vmf import VMF, Node  # noqa: E402

SRC = r"E:\SourceModding\tools\decomp\d1_trainstation_01_d.vmf"
DST = r"E:\XBLAH's modding tool\content\Mapbase\hl2rpm\mapsrc\rp_c17_01_trainstation.vmf"
MAPNAME = "rp_c17_01_trainstation"
NEXT_MAP = "rp_c17_02_plaza"
LANDMARK = "lm_01_02"

# ---------------------------------------------------------------------------------------
# 1. story entities to delete (targetnames, case-insensitive)
# ---------------------------------------------------------------------------------------
DELETE_NAMES = """
barney helmetFaceplate_hand helmetFaceplate_head helmetBack logic_barney_init gordon_cop_template
relation_barney_metropolice relation_barney_cameras barney_ss_2 barney_ss_3 ss_Helmet_Reveal
security_01 security_02 security_03 security_04 security_intro_01 security_intro_01b security_intro_02
security_pen_01 security_freeze weapon_point_1 weapon_point_2
barney_door_close_trigger barney_lead_trigger_1 trigger_barney_bang_door trigger_cop_close_door_1
trigger_storage_close_door_1 trigger_barney_close_exitdoor logic_BarneyExitNag speaker_BarneyExitNag
logic_kill_citizens barney_room_blocker barney_room_blocker_2 cage_playerclip barney_hallway_clip
cage_door_counter logic_disable_cameras
kleiner kleiner_template kleiner_security_camera_1 barney_security_monitor_1 overlay_kleinertv
teleport_spotlight_2 window_shutters_1
logic_sound_door_pound_1 sound_door_pound_1 sound_door_cop_1 sound_door_cop_2 sound_door_cop_3 sound_door_cop_4
logic_sound_door_cop_1 logic_sound_door_cop_2 logic_sound_door_cop_3 logic_sound_door_cop_4 logic_sound_door_timer
hudhint_usepickup hudhint_useladder hudhint_jump hudhint_movement
timer_hudhintmovement timer_hudhintjump timer_hudhintpickup timer_hudhintladder trigger_hudhintmovement
crate_counter_trigger crate_counter respawning_crate_trigger crate_template storage_room_crate_filter
crate_filter respawning_crate_filter
credits_go text_chapter turret_hurt_1 turret_hurt_1_text cop_spawner_1
scene1_start scene2_start scene2_flash_mode_2 scene2_return_to_mode_0 scene3_start scene3_blend_to_3
scene3_move_to_mouth scene4_start scene4_blend_to_6 scene4_end scene2_lcs_intro intro_version3
zoom_to_fov_1_snap zoom_to_fov_40_12 zoom_to_fov_45_20 zoom_to_fov_10_snap zoom_to_fov_30_snap
zoom_to_fov_30_10 zoom_final zoom_finalg
viewcontrol_left_eye viewcontrol_mouth viewcontrol_left_cheek viewcontrol_final tracktrain_finalview_move
track_viewcontrol_final_1 track_viewcontrol_final_2 logic_killme_1 logic_killme_2 logic_killme_3
point_teleport_destination citizen_train_1_intro citizen_train_2_intro ss_citizens_intro
gman gman_head light_start light_end light_tracktrain light_tracktrain2
dynamic_light_tracktrain dynamic_light_tracktrain2
dynamic_light_path dynamic_light_path0 dynamic_light_path1 dynamic_light_path2 dynamic_light_path3
dynamic_light_path4 dynamic_light_path5 dynamic_light_path6
intro_static intro_zoomout starfield_ramp_timer starfield_ramp_counter starfield_ramp_remap
camera_black teleport
""".split()

# Barney/Kleiner marks (info_target) - prefixes
DELETE_PREFIXES = ("mark_barney", "mark_secmonitor", "mark_cop_security", "marks_kleiner", "mark_barneyroom")
DELETE_NAMES += ["mark_BarneyDoorSlot_Look", "target_chair"]

# unnamed story entities, by decompile id
DELETE_IDS = {
    322235,   # trigger_once at the customs gate: spawns Barney, locks the cage, camera angry
    408378,   # trigger_once next to turret_hurt_1 (out-of-bounds trap)
    1039857, 1039890, 1039898,  # tutorial hint triggers (ladder / crate / jump)
    999881,   # logic_auto OnNewGame of the Valve intro (G-Man speech, credits logo)
    382095,   # logic_auto: barney relationships
    382715,   # logic_auto: razor gate cop likes barney
    888921,   # env_global gordon_invulnerable
    616630,   # trigger_once (citizen filter) closes barney_door_1 / cage clip
    604795,   # trigger_once: sets up the customs queue (now on map spawn)
}
DELETE_CLASSES = {"env_credits", "env_message", "game_text", "point_teleport", "trigger_hurt",
                  "info_player_start", "npc_barney", "npc_kleiner", "script_intro", "env_zoom"}

# ---------------------------------------------------------------------------------------
# helpers
# ---------------------------------------------------------------------------------------


def lower(s):
    return (s or "").lower()


def out_parts(v):
    sep = "\x1b" if "\x1b" in v else ","
    return v.split(sep), sep


def remove_outputs(e, pred):
    """pred(output_name, parts) -> True removes the output"""
    c = e.child("connections")
    if not c:
        return 0
    n0 = len(c.items)
    keep = []
    for it in c.items:
        if isinstance(it, Node):
            keep.append(it)
            continue
        parts, _ = out_parts(it[1])
        if not pred(it[0], parts):
            keep.append(it)
    c.items = keep
    if not [it for it in c.items if not isinstance(it, Node)]:
        e.remove_child(c)
    return n0 - len(keep)


def set_outputs(m, e, outs):
    """replace all outputs: outs = [(output, target, input, param, delay, times)]"""
    c = e.child("connections")
    if c:
        e.remove_child(c)
    for o in outs:
        m.add_output(e, *o)


def by_name(m, name):
    return [e for e in m.entities() if lower(e.get("targetname")) == lower(name)]


def one(m, name):
    r = by_name(m, name)
    assert r, "entity %s not found" % name
    return r[0]


def by_id(m, i):
    for e in m.entities():
        if e.id == i:
            return e
    return None


def box_solid(m, mins, maxs, material, **kw):
    return m.make_box(mins, maxs, material, **kw)


def tex_box(m, mins, maxs, material, nodraw_faces=(), lightmapscale=16):
    fm = {f: "TOOLS/TOOLSNODRAW" for f in nodraw_faces}
    return m.make_box(mins, maxs, material, face_materials=fm, lightmapscale=lightmapscale)


# ---------------------------------------------------------------------------------------
# 2. passes
# ---------------------------------------------------------------------------------------


def strip_story(m, log):
    dn = set(lower(n) for n in DELETE_NAMES)
    removed_names = set()
    for e in list(m.entities()):
        nm = lower(e.get("targetname"))
        kill = (nm and (nm in dn or nm.startswith(DELETE_PREFIXES))) or e.id in DELETE_IDS \
            or e.classname in DELETE_CLASSES
        if e.classname == "env_global" and lower(e.get("globalstate")) == "gordon_invulnerable":
            kill = True
        if kill:
            if nm:
                removed_names.add(nm)
            m.delete(e)
            log.append("deleted %s %s #%s" % (e.classname, e.get("targetname") or "", e.id))
    # outputs aimed at deleted entities
    n = 0
    for e in m.entities():
        n += remove_outputs(e, lambda o, p: lower(p[0]) in removed_names)
    log.append("removed %d outputs to deleted entities" % n)
    return removed_names


def fix_kept_logic(m, log):
    # train arrival: the Breencast loop now starts on map spawn (see ambient_setup)
    rel = one(m, "relay_train_reached_station")
    remove_outputs(rel, lambda o, p: lower(p[0]) in ("logic_breen_startbreencast",) or
                   (lower(p[0]) == "camera_tv_breen"))
    m.add_output(rel, "OnTrigger", "sp00_relay_arrived", "Trigger", "", 0.5, 1)
    # the cage-door counter killed Breen after Barney's scene - deleted; the Barney door must
    # not count anything any more
    for e in by_name(m, "barney_door_1"):
        remove_outputs(e, lambda o, p: True)
    # interrogation-room door used to close Barney's door
    for e in by_name(m, "introom_door_1"):
        remove_outputs(e, lambda o, p: lower(p[0]) in ("barney_door_2", "barney_room_blocker"))
    # triggers on the way from the platform spawned the ambient templates and started their
    # scripts: that now happens on map spawn (ambient_setup), also for a player who comes
    # from the plaza
    amb = set(lower(t) for t in AMBIENT_TEMPLATES)
    moved = {"luggage_script_condition": "enable", "case_sweepspots": "pickrandom",
             "actbusy_chainlink_girl": "*", "actbusy_platform_citizens": "*"}
    for e in m.entities():
        if not e.classname.startswith("trigger_"):
            continue
        remove_outputs(e, lambda o, p: (lower(p[1]) == "forcespawn" and lower(p[0]) in amb) or
                       (lower(p[0]) in moved and moved[lower(p[0])] in ("*", lower(p[1]))))
    log.append("kept logic patched")


def setup_doors(m, log):
    """Doors for free roaming. Names for quests are documented in the map guide."""
    # customs gate -> CP corridor (was barney_door_1: open, locked, ignore +use)
    d = one(m, "barney_door_1")
    d.set("targetname", "cp_post_door")
    d.set("spawnflags", "0")          # closed, unlocked, usable
    # CP office (Barney's security room): locked (safe, quest)
    d = one(m, "barney_door_2")
    d.set("targetname", "cp_office_door")
    d.set("spawnflags", "2048")       # locked, usable (rattles)
    remove_outputs(d, lambda o, p: True)
    slot = one(m, "barney_door_2_slot")
    slot.set("targetname", "cp_office_door_slot")
    slot.set("parentname", "cp_office_door")
    # interrogation room: locked
    d = one(m, "introom_door_1")
    d.set("targetname", "cp_interrogation_door")
    d.set("spawnflags", "2048")
    remove_outputs(d, lambda o, p: True)
    # storage room (exit checkpoint) <-> CP office: closed, unlocked
    d = one(m, "storage_room_door")
    d.set("targetname", "cp_checkpoint_office_door")
    d.set("spawnflags", "0")
    remove_outputs(d, lambda o, p: True)
    # storage room west doors to the razor-train platform (were locked): stay locked
    for i, nm in ((752345, "cp_checkpoint_west_door_1"), (752510, "cp_checkpoint_west_door_2")):
        e = by_id(m, i)
        if e:
            e.set("targetname", nm)
            e.set("spawnflags", "2048")
    # courtyard door into the corridor that leads to the plaza: closed, usable
    e = by_id(m, 860741)
    if e:
        e.set("targetname", "exit_plaza_door")
        e.set("spawnflags", "0")
    # door at the top of the 02 stairwell (east corridor), keep locked
    e = by_id(m, 595051)
    if e:
        e.set("targetname", "exit_corridor_door_upper")
        e.set("spawnflags", "2048")
    # razor-train gate: Barney's scene closed it, it stays closed (citizens' train)
    log.append("doors set up")


def setup_transitions(m, log):
    lm = one(m, "trainstation_01_to_02")
    if lm.classname == "info_landmark":
        pass
    for e in by_name(m, "trainstation_01_to_02"):
        e.set("targetname", LANDMARK)
    n = 0
    for e in list(m.entities("trigger_changelevel")):
        if lower(e.get("landmark")) == "trainstation_01_to_02":
            e.set("map", NEXT_MAP)
            e.set("landmark", LANDMARK)
            e.set("spawnflags", "0")
            n += 1
        else:
            m.delete(e)
    assert n == 1, n
    log.append("transition %s -> %s" % (LANDMARK, NEXT_MAP))


# player start / train ------------------------------------------------------------------
# point_teleport_destination of the Valve intro: inside intro_train_2 at the start of its path
PLAYER_START = (-9419, -2483, 22)
PLAYER_START_ANGLES = "0 15 0"


def setup_start_and_dream(m, log):
    m.add_entity("info_player_start", PLAYER_START, angles=PLAYER_START_ANGLES, spawnflags="1",
                 targetname="spawn_train")
    # passengers: male_07 is the player's model - give the second passenger another face
    e = one(m, "citizen_train_2")
    e.set("model", "models/humans/group01/male_04.mdl")
    # wake-up relay (was logic_start_train after the G-Man intro)
    st = one(m, "logic_start_train")
    keep = []
    for k, v in st.child("connections").props:
        p, _ = out_parts(v)
        t = lower(p[0])
        if t.startswith("intro_train_") or (t.startswith("citizen_train_") and lower(p[1]) == "giveweapon"):
            keep.append((k, p[0], p[1], p[2], float(p[3]), int(p[4])))
    set_outputs(m, st, keep)
    st.set("targetname", "sp00_relay_wake")
    # dream entities ---------------------------------------------------------------------
    base = (-14100, -13700, -1100)

    def at(dx, dy=0, dz=0):
        return (base[0] + dx, base[1] + dy, base[2] + dz)

    fade_in = m.add_entity("env_fade", at(0, 0, 32), targetname="sp00_fade_wake", duration="4",
                           holdtime="0", renderamt="255", rendercolor="0 0 0", spawnflags="1")
    flash = m.add_entity("env_fade", at(16, 0, 32), targetname="sp00_fade_flash", duration="1.2",
                         holdtime="0", renderamt="255", rendercolor="190 215 255", spawnflags="1")
    black = m.add_entity("env_fade", at(32, 0, 32), targetname="sp00_fade_out", duration="1.5",
                         holdtime="2", renderamt="255", rendercolor="0 0 0", spawnflags="0")
    snd_tele = m.add_entity("ambient_generic", at(48, 0, 32), targetname="sp00_snd_flash",
                            message="ambient/levels/citadel/portal_beam_shoot5.wav", spawnflags="49",
                            health="8", radius="1250", pitch="100", pitchstart="100")
    snd_clank = m.add_entity("ambient_generic", at(64, 0, 32), targetname="sp00_snd_coupling",
                             message="Trainyard.train_brake", spawnflags="49", health="10",
                             radius="1250", pitch="100", pitchstart="100")
    rpg = m.add_entity("logic_rpg", at(80, 0, 32), targetname="rpg_sp00")
    rel = m.add_entity("logic_relay", at(0, 32, 0), targetname="sp00_relay_start", spawnflags="1")
    # 0 s: black void, stars, music      14 s: flash of the test chamber      20 s: void
    # 30 s: coupling clank, fade to black  33 s: wake up in the train
    O = [
        ("OnTrigger", "viewcontrol_black", "Enable", "", 0, 1),
        ("OnTrigger", "starfield_logic_start_sparse", "Trigger", "", 1.5, 1),
        ("OnTrigger", "intro_music", "PlaySound", "", 1, 1),
        ("OnTrigger", "rpg_sp00", "SetFlag", "mq00_started", 0, 1),
        ("OnTrigger", "sp00_relay_lines", "Trigger", "", 3, 1),
        ("OnTrigger", "sp00_relay_flash", "Trigger", "", 14, 1),
        ("OnTrigger", "sp00_relay_voice", "Trigger", "", 22, 1),
        ("OnTrigger", "sp00_relay_coupling", "Trigger", "", 30, 1),
    ]
    for o in O:
        m.add_output(rel, *o)
    lines = m.add_entity("logic_relay", at(0, 64, 0), targetname="sp00_relay_lines", spawnflags="1")
    m.add_output(lines, "OnUser1", "!self", "Trigger", "", 0, 0)  # placeholder (phase B: text lines)
    voice = m.add_entity("logic_relay", at(0, 96, 0), targetname="sp00_relay_voice", spawnflags="1")
    m.add_output(voice, "OnUser1", "!self", "Trigger", "", 0, 0)  # placeholder (phase B: G-Man line)
    fl = m.add_entity("logic_relay", at(0, 128, 0), targetname="sp00_relay_flash", spawnflags="1")
    for o in [("OnTrigger", "sp00_fade_flash", "Fade", "", 0, 1),
              ("OnTrigger", "sp00_snd_flash", "PlaySound", "", 0, 1),
              ("OnTrigger", "camera_hl1_scene", "Enable", "", 0, 1),
              ("OnTrigger", "sp00_fade_flash", "Fade", "", 5.5, 1),
              ("OnTrigger", "viewcontrol_black", "Enable", "", 5.6, 1)]:
        m.add_output(fl, *o)
    cp = m.add_entity("logic_relay", at(0, 160, 0), targetname="sp00_relay_coupling", spawnflags="1")
    # waking up cancels what the dream still has queued (the flash re-enables the black camera at 19.6 s)
    for o in [("OnTrigger", "sp00_relay_start", "CancelPending", "", 0, 1),
              ("OnTrigger", "sp00_relay_flash", "CancelPending", "", 0, 1),
              ("OnTrigger", "sp00_snd_coupling", "PlaySound", "", 0, 1),
              ("OnTrigger", "sp00_fade_out", "Fade", "", 0.2, 1),
              ("OnTrigger", "intro_music", "FadeOut", "3", 0, 1),
              ("OnTrigger", "starfield", "TurnOff", "", 2, 1),
              ("OnTrigger", "viewcontrol_black", "Disable", "", 2.2, 1),
              ("OnTrigger", "sp00_fade_wake", "Fade", "", 2.25, 1),
              ("OnTrigger", "sp00_relay_wake", "Trigger", "", 2.4, 1),
              ("OnTrigger", "intro_music", "StopSound", "", 4, 1)]:
        m.add_output(cp, *o)
    arr = m.add_entity("logic_relay", at(0, 192, 0), targetname="sp00_relay_arrived", spawnflags="1")
    m.add_output(arr, "OnTrigger", "rpg_sp00", "SetFlag", "mq00_train_arrived", 0, 1)
    la = m.add_entity("logic_auto", at(0, 224, 0), spawnflags="1")
    m.add_output(la, "OnNewGame", "sp00_relay_start", "Trigger", "", 0.2, 1)
    m.add_entity("info_target", (-14576, -13860, -1212), targetname="spot_gman_dream", angles="0 270 0")
    log.append("dream + start set up")


# ambient -------------------------------------------------------------------------------
AMBIENT_TEMPLATES = ["terminal_side_props_template", "luggage_script_template", "vort_template",
                     "prop_template", "citizen_template_1", "platform_citizen_template",
                     "customs_template", "razortrain_cop_template", "interro_room_template",
                     "chainlink_girl_template"]


def ambient_setup(m, log):
    la = m.add_entity("logic_auto", (-4825, -2085, 40), targetname="rp_ambient_auto", spawnflags="1")
    O = [("OnMapSpawn", "logic_breen_startbreencast", "Trigger", "", 1, 1),
         ("OnMapSpawn", "camera_tv_breen", "SetOn", "", 0.5, 1),
         ("OnMapSpawn", "monitor_breencast_jumbotron3", "Enable", "", 0.5, 1)]
    for i, t in enumerate(AMBIENT_TEMPLATES):
        O.append(("OnMapSpawn", t, "ForceSpawn", "", 0.3 + 0.05 * i, 1))
    O += [("OnMapSpawn", "relay_setup_customs_queue", "Trigger", "", 1.5, 1),
          ("OnMapSpawn", "luggage_script_condition", "Enable", "", 2, 1),
          ("OnMapSpawn", "case_sweepSpots", "PickRandom", "", 2.5, 1),
          ("OnMapSpawn", "actbusy_chainlink_girl", "Activate", "", 1.2, 1),
          ("OnMapSpawn", "actbusy_chainlink_girl", "ForceNPCToActBusy", "chainlink_girl chainlink_girl_clutch tel", 1.3, 1),
          ("OnMapSpawn", "actbusy_platform_citizens", "Activate", "", 1.2, 1),
          ("OnMapSpawn", "actbusy_platform_citizens", "ForceNPCToActBusy", "citizen_platform_1 pre_customs_actbusy_bench_1", 1.3, 1),
          ("OnMapSpawn", "ss_citizen_introom_chair", "BeginSequence", "", 1.5, 1),
          ("OnMapSpawn", "locker_area_occluder", "Deactivate", "", 0, 1)]
    for o in O:
        m.add_output(la, *o)
    # first entry from the plaza (no dream): drive the trains into the station once
    # (sp00_relay_wake is a fire-once relay, so a return trip does nothing)
    m.add_output(la, "OnMapTransition", "sp00_relay_wake", "Trigger", "", 0.5, 1)
    log.append("ambient auto-spawn")


# ---------------------------------------------------------------------------------------
# geometry helpers
# ---------------------------------------------------------------------------------------
import re  # noqa: E402

_PT = re.compile(r"\(\s*([-\d.eE+]+)\s+([-\d.eE+]+)\s+([-\d.eE+]+)\s*\)")
_DIRS = {"top": (2, 1), "bottom": (2, -1), "left": (0, -1), "right": (0, 1), "back": (1, 1), "front": (1, -1)}


def _plane(side):
    pts = [tuple(map(float, g.groups())) for g in _PT.finditer(side.get("plane", ""))]
    a, b, c = pts[:3]
    u = (a[0] - b[0], a[1] - b[1], a[2] - b[2])
    v = (c[0] - b[0], c[1] - b[1], c[2] - b[2])
    n = (u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0])
    L = math.sqrt(sum(x * x for x in n))
    n = tuple(x / L for x in n)
    return n, sum(n[i] * b[i] for i in range(3))


def axis_box(solid):
    """(mins, maxs, {dir_name: side}) if the solid is an axis-aligned box, else None"""
    lo = [None] * 3
    hi = [None] * 3
    sides = {}
    for sd in solid.children_named("side"):
        n, d = _plane(sd)
        ax = max(range(3), key=lambda i: abs(n[i]))
        if abs(abs(n[ax]) - 1) > 1e-4:
            return None
        sgn = 1 if n[ax] > 0 else -1
        if sgn > 0:
            hi[ax] = d
        else:
            lo[ax] = -d
        for k, (a, s) in _DIRS.items():
            if a == ax and s == sgn:
                sides[k] = sd
    if None in lo or None in hi or len(sides) != 6:
        return None
    return tuple(lo), tuple(hi), sides


def box_from(m, mins, maxs, ref_sides, inner_mat):
    """new box; faces on the reference box boundary copy its material/uv, cut faces get inner_mat"""
    rlo = [None] * 3
    rhi = [None] * 3
    for k, sd in ref_sides.items():
        n, d = _plane(sd)
        a, s = _DIRS[k]
        if s > 0:
            rhi[a] = d
        else:
            rlo[a] = -d
    fm = {}
    for k, (a, s) in _DIRS.items():
        on = (maxs[a] == rhi[a]) if s > 0 else (mins[a] == rlo[a])
        fm[k] = ref_sides[k].get("material") if on else inner_mat
    b = m.make_box(mins, maxs, inner_mat, face_materials=fm)
    names = ["top", "bottom", "left", "right", "back", "front"]
    for name, sd in zip(names, b.children_named("side")):
        a, s = _DIRS[name]
        on = (maxs[a] == rhi[a]) if s > 0 else (mins[a] == rlo[a])
        if on:
            ref = ref_sides[name]
            sd.set("uaxis", ref.get("uaxis"))
            sd.set("vaxis", ref.get("vaxis"))
            sd.set("lightmapscale", ref.get("lightmapscale", "16"))
    return b


def carve(m, hole_lo, hole_hi, inner_mat, owner=None, ids=None):
    """cut an axis-aligned hole out of the axis-aligned box solids of the world that touch it"""
    owner = owner or m.world
    n = 0
    for s in list(owner.children_named("solid")):
        if ids is not None and s.id not in ids:
            continue
        ab = axis_box(s)
        if not ab:
            continue
        lo, hi, sides = ab
        if any(hi[i] <= hole_lo[i] or lo[i] >= hole_hi[i] for i in range(3)):
            continue
        if any(sd.get("material", "").upper().startswith("TOOLS/TOOLSSKYBOX") for sd in sides.values()):
            continue
        pieces = []
        cur_lo, cur_hi = list(lo), list(hi)
        for a in range(3):
            if cur_lo[a] < hole_lo[a]:
                p_lo, p_hi = list(cur_lo), list(cur_hi)
                p_hi[a] = hole_lo[a]
                pieces.append((tuple(p_lo), tuple(p_hi)))
                cur_lo[a] = hole_lo[a]
            if cur_hi[a] > hole_hi[a]:
                p_lo, p_hi = list(cur_lo), list(cur_hi)
                p_lo[a] = hole_hi[a]
                pieces.append((tuple(p_lo), tuple(p_hi)))
                cur_hi[a] = hole_hi[a]
        idx = owner.items.index(s)
        owner.items.remove(s)
        for k, (plo, phi) in enumerate(pieces):
            b = box_from(m, plo, phi, sides, inner_mat)
            b.parent = owner
            owner.items.insert(idx + k, b)
        n += 1
    return n


def add_world_box(m, lo, hi, mat, face_materials=None):
    return m.add_box(lo, hi, mat, face_materials=face_materials)


def delete_props_in(m, lo, hi, classes=("prop_physics", "prop_static", "prop_dynamic")):
    n = 0
    for e in list(m.entities()):
        if e.classname not in classes:
            continue
        o = e.origin()
        if o and all(lo[i] <= o[i] <= hi[i] for i in range(3)):
            m.delete(e)
            n += 1
    return n


def trigger_box(m, classname, boxes, **keys):
    sol = [m.make_box(lo, hi, "TOOLS/TOOLSTRIGGER") for lo, hi in boxes]
    return m.add_brush_entity(classname, sol, **keys)


# ---------------------------------------------------------------------------------------
# exit: storage room (city-exit checkpoint) -> courtyard doorway
# ---------------------------------------------------------------------------------------
EXIT_DOOR_Y = (-112, -56)      # opening in the storage room's east wall (x -3264..-3256)
EXIT_DOOR_Z = (-32, 72)


def build_exit_doorway(m, log):
    y1, y2 = EXIT_DOOR_Y
    z1, z2 = EXIT_DOOR_Z
    n = carve(m, (-3264, y1, z1), (-3256, y2, z2), "CONCRETE/CONCRETEWALL004B")
    assert n >= 1, "exit doorway: no wall brush cut"
    # step down into the courtyard (-32 -> -48 -> -64)
    add_world_box(m, (-3256, y1 - 8, -64), (-3224, y2 + 8, -48), "CONCRETE/CONCRETEWALL004B",
                  face_materials={"top": "CONCRETE/CONCRETEFLOOR039A"})
    nprops = delete_props_in(m, (-3262, -128, -80), (-3205, -40, 0), classes=("prop_physics",))
    # the crate-stacking route is not needed any more; the window stays (breakable)
    log.append("exit doorway: %d brushes cut, %d props cleared" % (n, nprops))


# ---------------------------------------------------------------------------------------
# props for the slice
# ---------------------------------------------------------------------------------------


def prop(m, cls, model, pos, ang="0 0 0", **kw):
    keys = dict(model=model, angles=ang)
    if cls == "prop_static":
        keys.update(solid="6", skin="0")
    keys.update(kw)
    return m.add_entity(cls, pos, **keys)


def build_checkpoint(m, log):
    """storage room -> city-exit CP checkpoint (sergeant Gannon's desk)"""
    # clear the junk next to the new doorway and where the desk goes
    nprops = delete_props_in(m, (-3700, -128, -40), (-3560, 0, 120), classes=("prop_physics",))
    prop(m, "prop_static", "models/props_wasteland/controlroom_desk001b.mdl", (-3610, -60, -32), "0 0 0")
    prop(m, "prop_physics", "models/props_c17/chair_office01a.mdl", (-3650, -62, -31), "0 0 0")
    prop(m, "prop_physics", "models/props_lab/desklamp01.mdl", (-3606, -86, 4), "0 30 0")
    prop(m, "prop_physics", "models/props_lab/binderredlabel.mdl", (-3602, -42, 5), "0 180 16")
    prop(m, "prop_static", "models/props_combine/combine_interface001.mdl", (-3700, -100, -32), "0 0 0")
    prop(m, "prop_static", "models/props_combine/combine_barricade_short01a.mdl", (-3330, -28, -32), "0 0 0")
    m.add_entity("info_target", (-3650, -62, -32), targetname="spot_gannon", angles="0 0 0")
    m.add_entity("info_target", (-3296, -84, -32), targetname="spot_cp_checkpoint", angles="0 180 0")
    m.add_entity("info_target", (-3300, -84, -32), targetname="spot_exit_doorway", angles="0 0 0")
    log.append("checkpoint props (%d junk props removed)" % nprops)


TRAIN = "intro_train_2"


def build_train_spots(m, log):
    m.add_entity("info_target", (-5360, -1822, 17), targetname="spot_oldman_train", angles="0 270 0",
                 parentname=TRAIN)
    m.add_entity("info_target", (-5231, -1890, 17), targetname="spot_yun_sohi", angles="0 90 0",
                 parentname=TRAIN)
    # no pram in the stock content: a shopping cart with a bundle stands in (non-solid, so the
    # passengers can still walk down the aisle)
    prop(m, "prop_dynamic", "models/props_junk/PushCart01a.mdl", (-5196, -1872, 34), "0 180 0",
         targetname="prop_sohi_pram", parentname=TRAIN, solid="0", DisableShadows="0")
    prop(m, "prop_dynamic", "models/props_junk/garbage_bag001a.mdl", (-5196, -1872, 40), "0 30 0",
         targetname="prop_sohi_bundle", parentname=TRAIN, solid="0")
    m.add_entity("info_target", (-5526, -1853, 17), targetname="spot_player_wake", angles="0 0 0",
                 parentname=TRAIN)
    log.append("train spots")


def build_lockers(m, log):
    for old, new in (("lockers_2_door_right", "locker_88"), ("lockers_3_door_left", "locker_113")):
        door = one(m, old)
        door.set("targetname", new)
        door.set("spawnflags", str(int(door.get("spawnflags", "0")) | 2048))   # locked
        for e in m.entities():
            if lower(e.get("parentname")) == lower(old):
                e.set("parentname", new)
                e.set("targetname", new + "_model")
        o = door.origin()
        m.add_entity("info_target", (o[0] + 14, o[1], -16), targetname="spot_" + new, angles="0 180 0")
    log.append("lockers 88 / 113")


def build_spots(m, log):
    S = [
        ("spot_cp_xray7", (-4380, -2040, -32), "0 180 0"),     # arrival platform, first check
        ("spot_savva", (-4140, -2796, -32), "0 90 0"),        # vort sweeping (ss_VortSweep2)
        ("spot_cp_post", (-3900, -520, -32), "0 180 0"),       # CP corridor (post)
        ("spot_cp_office", (-3420, -360, -32), "0 180 0"),     # CP office desk
        ("spot_interrogation", (-3714, -340, -32), "0 270 0"),  # interrogation room chair
    ]
    for nm, pos, ang in S:
        m.add_entity("info_target", pos, targetname=nm, angles=ang)
    log.append("spots")


def build_rpg_triggers(m, log):
    # C17-00: the train car (moves with the train)
    trigger_box(m, "trigger_rpg", [((-5660, -1905, 18), (-5060, -1810, 110))], targetname="rpg_discover_c17_00",
                discover="C17-00", once="1", spawnflags="1", parentname=TRAIN)
    # C17-01: stepping onto the arrival platform / coming in from the plaza side
    trigger_box(m, "trigger_rpg", [((-5640, -2140, -32), (-4980, -1935, 96)),
                                   ((-3250, -120, -64), (-2950, 310, 64))],
                targetname="rpg_discover_c17_01", discover="C17-01", once="1", spawnflags="1")
    # Alliance control zones: 1 = public station, 2 = restricted (razor-train platform P4,
    # interrogation room)
    trigger_box(m, "trigger_rpg_zone", [((-6120, -3200, -80), (-3700, -1030, 640)),
                                        ((-6120, -1030, -80), (-3770, -620, 400)),
                                        ((-4400, -620, -80), (-4060, -440, 300)),
                                        ((-4080, -576, -40), (-3360, -464, 140)),
                                        ((-3616, -464, -40), (-3264, -136, 110)),
                                        ((-3712, -128, -40), (-3264, 192, 250))],
                targetname="rpg_zone_station", zone="1")
    trigger_box(m, "trigger_rpg_zone", [((-6400, -440, -80), (-3720, 380, 500)),
                                        ((-3808, -456, -40), (-3620, -290, 110))],
                targetname="rpg_zone_restricted", zone="2")
    m.add_entity("logic_rpg", (-4825, -2085, 60), targetname="rpg_station")
    log.append("rpg triggers")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--src", default=SRC)
    ap.add_argument("--dst", default=DST)
    a = ap.parse_args()
    m = VMF.load(a.src)
    log = []
    strip_story(m, log)
    fix_kept_logic(m, log)
    setup_doors(m, log)
    setup_transitions(m, log)
    setup_start_and_dream(m, log)
    ambient_setup(m, log)
    build_exit_doorway(m, log)
    build_checkpoint(m, log)
    build_train_spots(m, log)
    build_lockers(m, log)
    build_spots(m, log)
    build_rpg_triggers(m, log)
    # phase B: the people of the prologue and their map hooks (phaseb_rp_c17_01.py)
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    from phaseb_rp_c17_01 import build_phase_b
    build_phase_b(m, log)
    m.world.set("comment", "HL2RPM %s, generated by hl2rpm_extras/tools/prep_%s.py from the "
                "BSPSource decompile of d1_trainstation_01 - do not edit by hand" % (MAPNAME, MAPNAME))
    os.makedirs(os.path.dirname(a.dst), exist_ok=True)
    m.save(a.dst, backup=False)
    with open(os.path.splitext(a.dst)[0] + "_prep.log", "w", encoding="utf-8") as f:
        f.write("\n".join(log) + "\n")
    print("wrote", a.dst, "(%d log lines)" % len(log))


if __name__ == "__main__":
    main()
