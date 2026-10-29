#!/usr/bin/env python3
"""Build rp_c17_02_plaza.vmf from the BSPSource decompile of retail d1_trainstation_02.

    python prep_rp_c17_02_plaza.py [--src <decompiled.vmf>] [--dst <out.vmf>]

Re-runnable: always starts from the untouched decompile (E:\\SourceModding\\tools\\decomp\\
d1_trainstation_02_d.vmf) and writes the map source into mapsrc. Do not hand-edit the output
VMF - change this script and run it again (Hammer edits of the output are lost on the next run).
The leaked Valve VMFs are only looked at for reference; nothing is copied from them.

What it does (phase A of the RPG vertical slice, hl2rpm_extras/docs/RPG_SPEC.md §11; map guide:
hl2rpm_extras/content/integration/map_rp_c17_02_plaza.md):
  * strips Gordon's story scripting: the "pick up the can" cop, the shop arrest and the beating
    sounds, the knock-out police goals and their teleports, the APC/strider/alley triggers that
    were fired by the player's route, Valve's playground commentary scenes, tutorial hints,
    achievements, gordon_invulnerable; keeps the ambience (Breencast screens, food queue, random
    citizens and passers-by, plaza cops on act-busy, scanners, pigeons, cameras, force fields,
    sounds, lights, props) - former templates now spawn with the map;
  * the strider walks behind the Combine gates every few minutes (logic_timer);
  * transitions: lm_01_02 (back to rp_c17_01_trainstation, Valve's 01->02 place),
    lm_02_03 (Valve's 02->03 place, apartment entrance -> rp_c17_03_block7),
    lm_02_08 (new: through the south Combine gate by the APC -> rp_c17_08_opora);
  * Administration annex (C17-07): Labour Bureau hall with the clerk counter, Ruzhena's
    reception and Lorch's office (locked) behind the south facade of the plaza, entered from the
    plaza through a new door (annex_door_main, areaportal);
  * plaza dressing: dry fountain + benches, the "Obespechenie" kiosk (water, leaflets);
  * named spots (info_target spot_*) for the slice NPCs, trigger_rpg discovery volumes,
    info_node additions for the new rooms.
"""
import argparse
import math
import os
import sys

sys.path.insert(0, r"E:\SourceModding\HL2RPMSOURCE\.claude\skills\hl2rpm-hammer\scripts")
from vmf import VMF, Node  # noqa: E402

SRC = r"E:\SourceModding\tools\decomp\d1_trainstation_02_d.vmf"
DST = r"E:\XBLAH's modding tool\content\Mapbase\hl2rpm\mapsrc\rp_c17_02_plaza.vmf"
MAPNAME = "rp_c17_02_plaza"
MAP_01 = "rp_c17_01_trainstation"
MAP_03 = "rp_c17_03_block7"
MAP_08 = "rp_c17_08_opora"

# ---------------------------------------------------------------------------------------
# 1. what goes
# ---------------------------------------------------------------------------------------

DELETE_NAMES = [
    # "pick up the can" (SQ-01 is re-made by the quest code; the spots stay as info_target)
    "cupcop", "cupcop_can", "cupcop_policegoal", "cupcop_actbusy", "cupcop_physimpact",
    "ss_cop_ItemKnockOff", "trashcan_trigger", "throwcan_trigger", "player_escape_trigger",
    "cupcop_nag_timer", "cupcop_failsafe_timer", "cupcop_nag_timer_putin", "cupcop_fail_relay",
    "cupcop_can_pickup", "filter_can", "cup_hint", "lcs_CupCop_01", "lcs_CupCop_Fail",
    "lcs_CupCop_Pass", "lcs_CupCop_nag", "lcs_CupCop_intro", "lcs_CupCop_Fail_Done",
    "lcs_CupCop_Pass_Leave", "lcs_CupCop_Fail_Leave", "lcs_CupCop_nag_putin",
    "achievement_can_hitcop", "achievement_can_intrash", "knockout_fade",
    # shop arrest + beating (the shop stays as an empty interior)
    "shop_cop", "shop_police_goal", "shop_knockout_trigger", "shop_knockout_target",
    "shop_cop_dismount", "shop_template", "spreadwall2", "spreadwall1", "spreadcopscript",
    "spreadcopscript1", "spreadcopscript2", "arrestedcit2", "arrestedcit1", "spreadcop1",
    "covercop", "covercop1", "citizen_beating_timer", "citizen_beating_sound",
    # knock-out police goals / teleports (alley end, courtyard door, APC)
    "monkey", "monkey2", "knockout_trigger", "knockout", "knockout_trigger_2", "knockout2",
    "door_police_goal", "door_cop", "door_cop_dismount", "door_cop_block_door",
    "apc_police_goal", "apc_police_goal2", "apc_knockout_trigger", "apc_knockout",
    # Valve's playground commentary (they comment on Gordon's arrest route)
    "lcs_playground_cit_lookatcops", "lcs_playground_cit_2_look", "lcs_playground_cit_1_look",
    "lcs_playground_cit_1_lookatcops", "lcs_Courtyard_commentators", "mark_playground_lookat",
    "trigger_playground_cits_lookatplayer", "trigger_playground_cits_lookatcops_cancel",
    "trigger_lcs_courtyardComments",
    # tutorial hint
    "hudhint_useladder",
]

# unnamed story entities, by decompile id
DELETE_IDS = {
    948913, 1020513,          # trigger_once: cupcop police goal / scene start
    810731,                   # scripted_sequence: shop cop leaning in the shop door
    810234, 809708,           # trigger_once: shop cop dismount / shop + APC templates
    809514,                   # trigger_multiple: beating sound on/off
    699810, 855680, 931226,   # alley end: police goal triggers
    710336, 855626,           # courtyard door cop: police goal triggers
    1157503, 1157517,         # APC: knock-out on/off
    702848,                   # trigger_once: APC police goals + strider (strider is on a timer now)
    720204,                   # trigger_once: alley/playground spawns, kills the Breencast scene
    918926,                   # trigger_once: restarts the Breencast (logic_auto already runs it)
    1181047,                  # trigger_multiple: ladder hint
    1020760,                  # env_global gordon_invulnerable (the RPG player can be hurt)
    863453,                   # trigger_changelevel to d1_trainstation_02 itself (notouch artifact)
    1023664,                  # func_door (combine lock) where the annex door is now
}
DELETE_CLASSES = {"logic_achievement", "env_hudhint", "trigger_teleport", "info_teleport_destination",
                  "game_text", "env_message", "point_teleport", "trigger_hurt", "env_credits"}

# point_templates whose members should simply exist with the map (ambient life)
UNTEMPLATE = ["alley_cop_template", "playground_cop_template", "playground_cit_template",
              "apc_cop_template", "alley_scanner_template"]

# world solids replaced by the annex / the opened gate
DELETE_SOLIDS = {
    1467,   # facade: storefront door texture where the real annex door is cut
    782,    # poster wall inside the building block (inside the annex now)
    338,    # nodraw floor inside the building block (the annex has its own floor)
    3048,   # player clip in the south Combine gate -> func_brush cp_gate_opora_clip
}

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


FACES = ("top", "bottom", "left", "right", "back", "front")   # make_box order: +z -z -x +x +y -y


def box(m, mins, maxs, mat, faces=None, vscale=None, lightmapscale=16):
    """Axis aligned solid. mat: material for all faces; faces: {face: material};
    vscale: vertical texture scale for the side faces (e.g. 144/512 so a two-tone wall
    texture spans exactly floor..ceiling, bottom of the texture at mins z)."""
    s = m.make_box(mins, maxs, mat, face_materials=faces or {}, lightmapscale=lightmapscale)
    if vscale:
        for i, sd in enumerate(s.children_named("side")):
            if FACES[i] in ("left", "right", "back", "front"):
                ax = sd.get("vaxis").split("]")[0] + "]"
                # offset so the texture bottom sits at mins z: v = -z/vs + off == 0 at z = z0
                off = (mins[2] / vscale) % 512
                ax = ax.replace("[0 0 -1 0", "[0 0 -1 %g" % off)
                sd.set("vaxis", "%s %g" % (ax, vscale))
    return s


def world_box(m, mins, maxs, mat, **kw):
    s = box(m, mins, maxs, mat, **kw)
    owner = m.world
    idx = len(owner.children)
    for i, c in enumerate(owner.children):
        if c.name.lower() in ("editor", "hidden", "group"):
            idx = i
            break
    owner.add_child(s, idx)
    return s


def detail(m, solids, **keys):
    return m.add_brush_entity("func_detail", solids, **keys)


def trigger(m, cls, mins, maxs, **keys):
    return m.add_brush_entity(cls, [m.make_box(mins, maxs, "TOOLS/TOOLSTRIGGER")], **keys)


def prop(m, model, origin, yaw=0, cls="prop_static", pitch=0, roll=0, **keys):
    if cls == "prop_static":
        keys.setdefault("solid", "6")
        keys.setdefault("skin", "0")
        keys.setdefault("fademindist", "-1")
        keys.setdefault("fadescale", "1")
    else:
        keys.setdefault("spawnflags", "1")      # physics clutter: start asleep
    keys = {k: v for k, v in keys.items() if v is not None}
    return m.add_entity(cls, origin, model=model, angles="%g %g %g" % (pitch, yaw, roll), **keys)


def target(m, name, origin, yaw=0):
    return m.add_entity("info_target", origin, targetname=name, angles="0 %g 0" % yaw)


_node_id = [700]


def node(m, origin, cls="info_node"):
    _node_id[0] += 1
    return m.add_entity(cls, origin, nodeid=str(_node_id[0]), angles="0 0 0")


_DOOR_KEYS = {}


def door(m, name, hinge, yaw, locked=False, skin=2):
    """door01_left: 47 wide, 108 tall, width along local +y (yaw 90 -> world -x, 0 -> +y);
    the keys (sounds, speed) are copied from Valve's station entrance door."""
    x, y, z = hinge
    keys = dict(_DOOR_KEYS)
    keys.update(targetname=name, angles="0 %g 0" % yaw, spawnflags=str(2048 if locked else 8192),
                skin=str(skin), returndelay="-1", opendir="0",
                axis="%g %g %g, %g %g %g" % (x, y, z + 1, x, y, z + 107))
    if locked:
        keys.update(soundlocked="DoorHandles.Locked1", soundunlocked="DoorHandles.Unlocked1")
    return m.add_entity("prop_door_rotating", (x, y, z + 54), **keys)


def grab_door_keys(m):
    src = by_id(m, 694326)        # station_entrance (door01_left)
    for k, v in src.props:
        if k in ("id", "classname", "origin", "angles", "targetname", "axis", "spawnflags", "skin",
                 "returndelay"):
            continue
        _DOOR_KEYS[k] = v


# ---------------------------------------------------------------------------------------
# 2. passes
# ---------------------------------------------------------------------------------------


def strip_story(m, log):
    dn = set(lower(n) for n in DELETE_NAMES)
    removed = set()
    for e in list(m.entities()):
        nm = lower(e.get("targetname"))
        kill = (nm and nm in dn) or e.id in DELETE_IDS or e.classname in DELETE_CLASSES
        if e.classname == "func_clip_vphysics" and lower(e.get("filtername")) == "filter_can":
            kill = True
        if e.classname == "logic_navigation" and lower(e.get("target")) == "cupcop_can":
            kill = True
        if e.classname == "info_player_start" and e.id != 919822:
            kill = True      # one start (the 01->02 arrival); a new game starts in rp_c17_01
        if kill:
            if nm:
                removed.add(nm)
            m.delete(e)
            log.append("deleted %s %s #%s" % (e.classname, e.get("targetname") or "", e.id))
    # unknown targets left behind (names that only existed in the deleted set)
    alive = set(lower(e.get("targetname")) for e in m.entities() if e.get("targetname"))
    dead = removed - alive
    n = 0
    for e in m.entities():
        n += remove_outputs(e, lambda o, p: lower(p[0]) in dead)
    log.append("removed %d outputs to deleted entities" % n)
    # templates -> members spawn with the map
    for t in UNTEMPLATE:
        e = one(m, t)
        m.delete(e)
        log.append("untemplated %s" % t)
    for e in m.entities():
        n = remove_outputs(e, lambda o, p: lower(p[0]) in set(lower(x) for x in UNTEMPLATE))
        if n:
            log.append("removed %d template outputs from #%s" % (n, e.id))
    # world solids
    ws = [s for s in m.world.children_named("solid") if s.id in DELETE_SOLIDS]
    for s in ws:
        if s.id != 3048:
            m.world.remove_child(s)
    log.append("deleted %d world solids" % len(ws))
    return ws


def fix_kept_logic(m, log, gate_clip_solid):
    # exiting the station: pigeons yes, Valve's dramatic music cue no
    e = by_id(m, 581960)
    remove_outputs(e, lambda o, p: lower(p[0]) == "plaza_music")
    # playground ambience trigger: keep the children's-memory sounds, drop the story scanner call
    e = by_id(m, 843227)
    remove_outputs(e, lambda o, p: lower(p[0]) == "zippy")
    # strider path end: kill every strider (times -1) and its escort scanner
    e = one(m, "strider_1_path_6")
    remove_outputs(e, lambda o, p: True)
    m.add_output(e, "OnPass", "strider_1", "Kill", "", 0, -1)
    m.add_output(e, "OnPass", "strider_escort_scanner", "Kill", "", 0.5, -1)
    # playground citizen 1 had the player's model (male_07)
    one(m, "playground_cit_1").set("model", "models/humans/group01/male_03.mdl")
    # the south Combine gate clip becomes a toggle (open in the slice: the way to "Opora")
    m.world.remove_child(gate_clip_solid)
    m.add_brush_entity("func_brush", [gate_clip_solid], targetname="cp_gate_opora_clip",
                       StartDisabled="1", Solidity="2", spawnflags="2", rendermode="10",
                       renderamt="0", disableshadows="1", disablereceiveshadows="1", InputFilter="0")
    log.append("kept logic fixed; south gate clip -> cp_gate_opora_clip (disabled = open)")


def ambient_setup(m, log):
    la = m.add_entity("logic_auto", (-3635, -2060, 193), targetname="rp_ambient_auto", spawnflags="1")
    O = [
        ("OnMapSpawn", "alley_citizen_groundscript", "BeginSequence", "", 0.5, 1),
        ("OnMapSpawn", "alley_citizen_wallscript", "BeginSequence", "", 0.5, 1),
        ("OnMapSpawn", "playground_wallcop_script", "BeginSequence", "", 0.6, 1),
        ("OnMapSpawn", "apcidlescript2", "BeginSequence", "", 0.6, 1),
        ("OnMapSpawn", "alley_scanner", "InspectTargetPhoto", "floor_cit", 2, 1),
        ("OnMapSpawn", "act_busy_playground_cit_1", "Activate", "", 0.4, 1),
        ("OnMapSpawn", "act_busy_playground_cit_1", "ForceNPCToActBusy",
         "playground_cit_1 playground_cit_1_armscrossed 0", 0.5, 1),
        ("OnMapSpawn", "act_busy_playground_cit_2", "Activate", "", 0.4, 1),
        ("OnMapSpawn", "act_busy_playground_cit_2", "ForceNPCToActBusy",
         "playground_cit_2 playground_cit_2_pockets 0", 0.5, 1),
        ("OnMapSpawn", "rp_strider_timer", "FireTimer", "", 25, 1),
    ]
    for o in O:
        m.add_output(la, *o)
    # strider: walks along Valve's path behind the south gate, removed at the end, every 4 min
    t = m.add_entity("logic_timer", (-1543, -4980, 489), targetname="rp_strider_timer",
                     RefireTime="240", StartDisabled="0", spawnflags="0", UseRandomTime="0")
    for o in [("OnTimer", "strider_template", "ForceSpawn", "", 0, -1),
              ("OnTimer", "strider_relationship", "ApplyRelationship", "", 0.1, -1),
              ("OnTimer", "strider_escort_scanner", "SetFollowTarget", "strider_1", 0.2, -1),
              ("OnTimer", "strider_walk", "StartSchedule", "", 0.3, -1)]:
        m.add_output(t, *o)
    log.append("ambient auto-spawn + strider timer")


# transitions -----------------------------------------------------------------------------
# 01 <-> 02: Valve's landmark (-4256 0 -8) between the 01 north room and the start room.
#   rp_c17_01 touches its trigger at rel y -160..-144 (the doorway, walking -y); the player
#   arrives here at y ~ -128 (bbox -144..-112). The way back is a trigger further north in the
#   duplicated north room (rel y -64..-48): never touched on arrival, and arriving in 01 from it
#   (y ~ -80) is clear of 01's trigger.
# 02 <-> 03: Valve's place: 02 trigger at rel y -76..-64 (walking south in the apartment
#   entrance), 03's trigger back at rel y +52..+64.
# 02 <-> 08: new, behind the south Combine gate (see LM_02_08 below).
LM_02_08 = (-2112, -5232, 0)


def setup_transitions(m, log):
    one(m, "trainstation_01_to_02").set("targetname", "lm_01_02")
    e = by_id(m, 918550)
    e.set("map", MAP_01)
    e.set("landmark", "lm_01_02")
    e.set("spawnflags", "0")
    for s in e.children_named("solid"):
        e.remove_child(s)
    e.add_child(m.make_box((-4400, -64, -72), (-4168, -48, 112), "TOOLS/TOOLSTRIGGER"), 0)
    m.add_brush_entity("trigger_transition",
                       [m.make_box((-4408, -128, -72), (-4160, 312, 432), "TOOLS/TOOLSTRIGGER")],
                       targetname="lm_01_02")
    # 02 -> 03
    for nm in ("landmark_trainstation_02-03",):
        for e in by_name(m, nm):
            e.set("targetname", "lm_02_03")
    e = by_id(m, 863459)
    e.set("map", MAP_03)
    e.set("landmark", "lm_02_03")
    e.set("spawnflags", "0")
    # 02 -> 08: landmark on the floor of the passage behind the south gate (walking SE, yaw -45)
    m.add_entity("info_landmark", LM_02_08, targetname="lm_02_08")
    x, y, _ = LM_02_08
    # the trigger fills the passage behind the gate line (gate line runs NE-SW through
    # (-2130 -5017)); axis-aligned box well behind it
    m.add_brush_entity("trigger_changelevel",
                       [m.make_box((x - 72, y - 72, 0), (x + 72, y + 40, 128), "TOOLS/TOOLSTRIGGER")],
                       map=MAP_08, landmark="lm_02_08", spawnflags="0")
    m.add_brush_entity("trigger_transition",
                       [m.make_box((x - 128, y - 160, 0), (x + 128, y + 96, 192), "TOOLS/TOOLSTRIGGER")],
                       targetname="lm_02_08")
    n = 0
    for e in list(m.entities("trigger_changelevel")):
        if e.get("map") not in (MAP_01, MAP_03, MAP_08):
            m.delete(e)
        else:
            n += 1
    assert n == 3, n
    log.append("transitions lm_01_02 -> %s, lm_02_03 -> %s, lm_02_08 -> %s" % (MAP_01, MAP_03, MAP_08))


# Administration annex (C17-07) ----------------------------------------------------------
# inside the building block south of the plaza (x -2432..-1920, y -3968..-2944 was void
# behind the storefront facade). Floor z 0, ceiling 144.
AX0, AX1 = -2384, -1936      # outer walls
AY0, AY1 = -3536, -2952
CEIL = 144
WALL = "PLASTER/PLASTERWALL024B"
WALL_VS = CEIL / 512.0
WALL_C = "PLASTER/PLASTERWALL030A"
FLOOR_A = "TILE/TILEFLOOR010A"
FLOOR_B = "WOOD/WOODFLOOR007A"
CEILING = "PLASTER/PLASTERCEILING008A"
TRIM = "WOOD/WOODWALL009A"
DOOR_X = (-2264, -2216)      # entrance doorway in the plaza facade
DOOR_AB = (-2232, -2184)     # lobby -> reception
DOOR_BC = (-3424, -3376)     # reception -> Lorch's office (y range in the x -2112 wall)


def wall(m, mins, maxs, mat=WALL):
    return world_box(m, mins, maxs, mat, vscale=CEIL / 512.0 if mat == WALL else None)


def build_annex(m, log):
    # facade frame around the new entrance (the storefront door texture brush is gone)
    F = "STOREFRONT_TEMPLATE/STOREFRONT_TEMPLATE001G"
    world_box(m, (-2272, -2952, 0), (DOOR_X[0], -2944, 128), F)
    world_box(m, (DOOR_X[1], -2952, 0), (-2208, -2944, 128), F)
    world_box(m, (DOOR_X[0], -2952, 108), (DOOR_X[1], -2944, 128), F)
    # floors / ceiling
    world_box(m, (AX0, -3264, -16), (AX1, AY1, 0), FLOOR_A, faces={"bottom": "TOOLS/TOOLSNODRAW"})
    world_box(m, (AX0, AY0, -16), (AX1, -3264, 0), FLOOR_B, faces={"bottom": "TOOLS/TOOLSNODRAW"})
    world_box(m, (AX0, AY0, CEIL), (AX1, AY1, CEIL + 16), "TOOLS/TOOLSNODRAW",
              faces={"bottom": CEILING})
    # outer walls (outside faces nodraw: they face the void of the building block)
    def outer(mins, maxs, inner_face):
        fm = {f: "TOOLS/TOOLSNODRAW" for f in FACES if f != inner_face}
        return world_box(m, mins, maxs, WALL, faces=fm, vscale=WALL_VS)
    outer((AX0, AY0, 0), (AX0 + 16, AY1, CEIL), "right")                 # west
    outer((AX1 - 16, AY0, 0), (AX1, AY1, CEIL), "left")                  # east
    outer((AX0, AY0, 0), (AX1, AY0 + 16, CEIL), "back")                  # south
    # north wall with the doorway
    s = outer((AX0, AY1 - 16, 0), (DOOR_X[0], AY1, CEIL), "front")
    s.children_named("side")[3].set("material", TRIM)            # +x jamb
    s = outer((DOOR_X[1], AY1 - 16, 0), (AX1, AY1, CEIL), "front")
    s.children_named("side")[2].set("material", TRIM)            # -x jamb
    s = outer((DOOR_X[0], AY1 - 16, 108), (DOOR_X[1], AY1, CEIL), "front")
    s.children_named("side")[1].set("material", TRIM)            # lintel underside
    # lobby | back rooms (y -3280..-3264), doorway DOOR_AB
    wall(m, (AX0 + 16, -3280, 0), (DOOR_AB[0], -3264, CEIL))
    wall(m, (DOOR_AB[1], -3280, 0), (AX1 - 16, -3264, CEIL))
    wall(m, (DOOR_AB[0], -3280, 108), (DOOR_AB[1], -3264, CEIL))
    # reception | office (x -2112..-2096), doorway DOOR_BC
    wall(m, (-2112, AY0 + 16, 0), (-2096, DOOR_BC[0], CEIL))
    wall(m, (-2112, DOOR_BC[1], 0), (-2096, -3280, CEIL))
    wall(m, (-2112, DOOR_BC[0], 108), (-2096, DOOR_BC[1], CEIL))
    # baseboards (func_detail, 6 tall) along the lobby walls
    bb = []
    for (a, b) in [((AX0 + 16, -2968 - 2, 0), (DOOR_X[0], -2968, 6)),
                   ((DOOR_X[1], -2968 - 2, 0), (AX1 - 16, -2968, 6)),
                   ((AX0 + 16, -3264, 0), (DOOR_AB[0], -3262, 6)),
                   ((DOOR_AB[1], -3264, 0), (AX1 - 16, -3262, 6)),
                   ((AX0 + 16, -3262, 0), (AX0 + 18, -2970, 6)),
                   ((AX1 - 18, -3262, 0), (AX1 - 16, -2970, 6))]:
        bb.append(box(m, a, b, TRIM))
    detail(m, bb)
    # clerk counter (func_detail): front x -2120..-1952 at y -3128..-3112, return x -2120..-2104
    ctr = [box(m, (-2120, -3128, 0), (-1952, -3112, 40), TRIM, faces={"top": "WOOD/WOODFLOOR005A"}),
           box(m, (-2120, -3264, 0), (-2104, -3128, 40), TRIM, faces={"top": "WOOD/WOODFLOOR005A"}),
           box(m, (-2124, -3132, 40), (-1952, -3108, 44), "WOOD/WOODFLOOR005A"),
           # glass-less partition posts on the counter
           box(m, (-2120, -3124, 44), (-2112, -3116, 100), "METAL/METALWALL061F"),
           box(m, (-2036, -3124, 44), (-2028, -3116, 100), "METAL/METALWALL061F"),
           box(m, (-2120, -3124, 100), (-1952, -3116, 108), "METAL/METALWALL061F")]
    detail(m, ctr)
    # doors
    door(m, "annex_door_main", (DOOR_X[1], -2956, 0), 90)
    door(m, "annex_door_reception", (DOOR_AB[1], -3272, 0), 90)
    door(m, "door_lorch_office", (-2104, DOOR_BC[0], 0), 0, locked=True)
    m.add_brush_entity("func_areaportal",
                       [m.make_box((DOOR_X[0], -2966, 0), (DOOR_X[1], -2962, 108), "TOOLS/TOOLSAREAPORTAL")],
                       target="annex_door_main", StartOpen="0", PortalVersion="1")
    # furniture ------------------------------------------------------------------------
    P = prop
    # lobby: benches on the west wall, a Breen poster, combine monitor behind the counter
    for y in (-3016, -3112, -3208):
        P(m, "models/props_trainstation/bench_indoor001a.mdl", (-2348, y, 18), yaw=0)
    P(m, "models/props_trainstation/trashcan_indoor001a.mdl", (-2346, -3248, 20), yaw=0)
    P(m, "models/props_combine/combine_intmonitor001.mdl", (-1960, -3200, 44), yaw=180)
    P(m, "models/props_wasteland/controlroom_filecabinet002a.mdl", (-2088, -3250, 35), yaw=90)
    P(m, "models/props_wasteland/controlroom_filecabinet002a.mdl", (-2056, -3250, 35), yaw=90)
    P(m, "models/props_wasteland/controlroom_filecabinet001a.mdl", (-2000, -3254, 14), yaw=90)
    P(m, "models/props_lab/monitor01a.mdl", (-2060, -3120, 57), yaw=90)
    P(m, "models/props_lab/clipboard.mdl", (-2000, -3118, 44), yaw=80)
    P(m, "models/props_c17/paper01.mdl", (-2088, -3121, 45), yaw=100)
    P(m, "models/props_c17/chair_office01a.mdl", (-2024, -3192, 0), yaw=90, cls="prop_physics")
    P(m, "models/props_interiors/radiator01a.mdl", (-2200, -2978, 20), yaw=270)
    P(m, "models/props_combine/breenclock.mdl", (-2232, -3262, 112), yaw=90)
    m.add_entity("infodecal", (-2140, -2969, 72), texture="decals/decal_posterbreen")
    m.add_entity("infodecal", (-2366, -3150, 80), texture="decals/decal_posters005a")
    m.add_entity("infodecal", (-1954, -3040, 76), texture="decals/decalposter004a")
    # reception (Ruzhena): desk, chair, cabinets, plant
    P(m, "models/props_interiors/furniture_desk01a.mdl", (-2240, -3440, 20), yaw=90)
    P(m, "models/props_c17/chair_office01a.mdl", (-2240, -3474, 0), yaw=90, cls="prop_physics")
    P(m, "models/props_lab/desklamp01.mdl", (-2268, -3436, 40), yaw=70)
    P(m, "models/props_c17/paper01.mdl", (-2232, -3438, 41), yaw=10)
    P(m, "models/props_wasteland/controlroom_filecabinet002a.mdl", (-2354, -3330, 35), yaw=0)
    P(m, "models/props_wasteland/controlroom_filecabinet002a.mdl", (-2354, -3362, 35), yaw=0)
    P(m, "models/props_interiors/furniture_shelf01a.mdl", (-2356, -3460, 44), yaw=0)
    P(m, "models/props_interiors/pot01a.mdl", (-2128, -3300, 8), yaw=0, cls="prop_physics")
    P(m, "models/props_c17/furniturechair001a.mdl", (-2160, -3320, 20), yaw=200, cls="prop_physics")
    m.add_entity("infodecal", (-2240, -3519, 80), texture="decals/decal_posterbreen")
    # Lorch's office (locked): desk, chair, cabinet
    P(m, "models/props_wasteland/controlroom_desk001a.mdl", (-2010, -3420, 17), yaw=0)
    P(m, "models/props_wasteland/controlroom_chair001a.mdl", (-1980, -3420, 20), yaw=180,
      cls="prop_physics")
    P(m, "models/props_wasteland/controlroom_filecabinet002a.mdl", (-1970, -3300, 35), yaw=180)
    P(m, "models/props_combine/breenclock.mdl", (-1954, -3420, 100), yaw=180)
    # lights: fixtures on the ceiling + spots below them
    for (x, y) in [(-2240, -3040), (-2240, -3190), (-2030, -3040), (-2030, -3190), (-2240, -3400),
                   (-2024, -3400)]:
        P(m, "models/props_interiors/lights_florescent01a.mdl", (x, y, CEIL - 3), yaw=0)
        m.add_entity("light_spot", (x, y, CEIL - 8), angles="-90 0 0", pitch="-90",
                     _light="223 233 216 220", _lightHDR="-1 -1 -1 1", _lightscaleHDR="1",
                     _inner_cone="50", _cone="80", _exponent="1", _quadratic_attn="1",
                     _linear_attn="0", _constant_attn="0", _distance="0", style="0")
    m.add_entity("light", (-2160, -3120, 100), _light="230 220 190 12", _lightHDR="-1 -1 -1 1",
                 _lightscaleHDR="1", _quadratic_attn="1", style="0")
    m.add_entity("env_cubemap", (-2160, -3110, 64), cubemapsize="0")
    m.add_entity("env_cubemap", (-2240, -3400, 64), cubemapsize="0")
    m.add_entity("env_soundscape", (-2160, -3100, 64), soundscape="d1_trainstation.Interrogation",
                 radius="200")
    # AI nodes: inside the annex + the plaza in front of the door
    for p in [(-2240, -2910, 4), (-2240, -3000, 4), (-2300, -3100, 4), (-2160, -3060, 4),
              (-2040, -3060, 4), (-2208, -3230, 4), (-2208, -3320, 4), (-2280, -3400, 4),
              (-2180, -3470, 4), (-2030, -3460, 4), (-2024, -3200, 4)]:
        node(m, p)
    # spots
    target(m, "spot_bureau_clerk", (-2024, -3184, 0), 90)
    target(m, "spot_bureau_window", (-2024, -3080, 0), 270)
    target(m, "spot_bureau_queue", (-2160, -3040, 0), 0)
    target(m, "spot_bureau_guard", (-2296, -2990, 0), 300)
    target(m, "spot_ruzhena", (-2240, -3478, 0), 90)
    target(m, "spot_ruzhena_visitor", (-2240, -3380, 0), 270)
    target(m, "spot_lorch", (-1980, -3420, 0), 180)
    trigger(m, "trigger_rpg", (AX0 + 16, -3264, 0), (AX1 - 16, AY1 - 16, 112),
            targetname="rpg_discover_c17_07", discover="C17-07", once="1")
    log.append("annex built (lobby/bureau, Ruzhena's reception, Lorch's office)")


# plaza dressing ---------------------------------------------------------------------------
KIOSK = (-2592, -2800)       # centre of the Obespechenie kiosk (SW corner of the plaza)


def build_kiosk(m, log):
    cx, cy = KIOSK
    w, d, h = 128, 96, 104       # x, y, height; the sales window faces north (+y, the plaza)
    x0, x1, y0, y1 = cx - w / 2, cx + w / 2, cy - d / 2, cy + d / 2
    MW = "METAL/METALWALL021A"
    MT = "METAL/METALROOF005A"
    s = []
    s.append(box(m, (x0, y0, 0), (x0 + 8, y1, h), MW))                    # west
    s.append(box(m, (x1 - 8, y0, 0), (x1, y1, h), MW))                    # east
    s.append(box(m, (x0 + 8, y0, 0), (x1 - 8, y0 + 8, h), MW))            # south (back)
    # north wall with the window x -2620..-2564, z 40..92
    s.append(box(m, (x0 + 8, y1 - 8, 0), (x1 - 8, y1, 40), MW))
    s.append(box(m, (x0 + 8, y1 - 8, 92), (x1 - 8, y1, h), MW))
    s.append(box(m, (x0 + 8, y1 - 8, 40), (cx - 28, y1, 92), MW))
    s.append(box(m, (cx + 28, y1 - 8, 40), (x1 - 8, y1, 92), MW))
    s.append(box(m, (cx - 32, y1 - 8, 38), (cx + 32, y1 + 12, 42), "WOOD/WOODFLOOR005A"))  # counter
    s.append(box(m, (x0 - 8, y0 - 8, h), (x1 + 8, y1 + 24, h + 8), MT))   # roof with overhang
    s.append(box(m, (x0 + 8, y0 + 8, 0), (x1 - 8, y1 - 8, 2), "WOOD/WOODFLOOR007A"))  # floor
    detail(m, s)
    prop(m, "models/props_c17/Awning002a.mdl", (cx, y1 + 14, 100), yaw=90)
    prop(m, "models/props_junk/cardboard_box001a.mdl", (cx + 36, y0 + 26, 12), yaw=10)
    prop(m, "models/props_junk/cardboard_box004a.mdl", (cx + 36, y0 + 26, 30), yaw=40,
         cls="prop_physics")
    prop(m, "models/props_junk/plasticcrate01a.mdl", (cx - 40, y0 + 24, 10), yaw=0,
         cls="prop_physics")
    m.add_entity("infodecal", (x0 - 1, cy, 60), texture="decals/decal_posterbreen")
    m.add_entity("light", (cx, cy, 90), _light="254 216 146 10", _lightHDR="-1 -1 -1 1",
                 _lightscaleHDR="1", _quadratic_attn="1", style="0")
    target(m, "spot_kiosk_trader", (cx, cy + 10, 2), 90)
    target(m, "spot_kiosk_window", (cx, y1 + 40, 0), 270)
    target(m, "spot_water_seller", (cx, cy + 10, 2), 90)
    log.append("kiosk at %s" % (KIOSK,))


FOUNTAIN = (-2448, -1216)


def dress_plaza(m, log):
    fx, fy = FOUNTAIN
    prop(m, "models/props_c17/fountain_01.mdl", (fx, fy, 0), yaw=15)
    for ang in (0, 120, 240):
        a = math.radians(ang + 45)
        bx, by = fx + 168 * math.cos(a), fy + 168 * math.sin(a)
        prop(m, "models/props_trainstation/BenchOutdoor01a.mdl", (bx, by, 12), yaw=ang + 45)
    target(m, "spot_fountain", (fx + 120, fy - 100, 0), 135)
    log.append("fountain at %s" % (FOUNTAIN,))


def add_spots(m, log):
    # SQ-01: the cop by the column after the turnstiles, the bin he points at, where the can falls
    target(m, "spot_cp_union12", (-3739, -1045, 64), 120)
    target(m, "spot_can_urn", (-3728, -696, 64), 195)
    target(m, "spot_can_drop", (-3705, -915, 64), 195)
    # MQ-00 s6: Victor-6 at the arch into Kvartal 7 (courtyard entrance of house 7/3)
    target(m, "spot_victor6", (-5236, -4104, 0), 90)
    # Prospekt checkpoint (east Combine gate, closed in the slice)
    target(m, "spot_cp_line6", (-1072, -2008, 0), 180)
    # Opora checkpoint (south Combine gate by the APC)
    target(m, "spot_cp_gate_opora", (-2216, -4936, 0), 135)
    # square life
    target(m, "spot_breen_screen", (-1960, -1820, 720), 210)
    target(m, "spot_monument", (-1990, -1792, 64), 180)
    target(m, "spot_ration_point", (-4060, -2256, 64), 180)
    target(m, "spot_minfo_speaker", (-2304, -2048, 0), 0)
    target(m, "spot_newspaper_seller", (-2760, -2280, 0), 30)
    target(m, "spot_cp_plaza_1", (-2491, -1202, 0), 0)
    target(m, "spot_cp_plaza_2", (-2076, -2272, 0), 90)
    target(m, "spot_station_exit", (-3100, -2048, 64), 0)
    # location discovery: station portico + plaza
    trigger(m, "trigger_rpg", (-3184, -2304, 40), (-2944, -1792, 224), targetname="rpg_discover_c17_02",
            discover="C17-02", once="1")
    trigger(m, "trigger_rpg", (-2800, -2880, 0), (-1152, -768, 256), targetname="rpg_discover_c17_02_square",
            discover="C17-02", once="1")
    log.append("spots + discovery triggers")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--src", default=SRC)
    ap.add_argument("--dst", default=DST)
    a = ap.parse_args()
    m = VMF.load(a.src)
    log = []
    grab_door_keys(m)
    ws = strip_story(m, log)
    gate = [s for s in ws if s.id == 3048][0]
    fix_kept_logic(m, log, gate)
    ambient_setup(m, log)
    setup_transitions(m, log)
    build_annex(m, log)
    build_kiosk(m, log)
    dress_plaza(m, log)
    add_spots(m, log)
    # phase B: the people of the plaza and their map hooks (phaseb_rp_c17_02.py)
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    from phaseb_rp_c17_02 import build_phase_b
    build_phase_b(m, log)
    m.world.set("comment", "HL2RPM %s, generated by hl2rpm_extras/tools/prep_%s.py from the "
                "BSPSource decompile of d1_trainstation_02 - do not edit by hand" % (MAPNAME, MAPNAME))
    os.makedirs(os.path.dirname(a.dst), exist_ok=True)
    m.save(a.dst, backup=False)
    with open(os.path.splitext(a.dst)[0] + "_prep.log", "w", encoding="utf-8") as f:
        f.write("\n".join(log) + "\n")
    print("wrote", a.dst, "(%d log lines)" % len(log))


if __name__ == "__main__":
    main()
