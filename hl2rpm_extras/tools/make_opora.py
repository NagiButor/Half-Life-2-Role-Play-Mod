"""rp_c17_08_opora: sector C17-08, Civil Protection precinct "Opora" (former school No. 3).

    python make_opora.py [out.vmf] [--steps camera_steps.txt]

Layout (x east, y north, z up; 1 storey = 128 units, slabs 8):

    y 1856 -------- hull --------------------------------------------------------
           Nexus wall (Combine, 960 high)
    y 1600 ---- compound north wall ----
           back yard: dumpsters, trees, gym exit, back gate
    y 1360         [cells annex][canteen annex][   gym (2 storeys)   ]
    y  848 [================ school main wing, 3 storeys + roof ===============]  garage
    y  320 [ stairW | med/int1/barracks | lobby/int2,3/... | armoury/int4/archive | stairE ]
           yard (loyalty post, smoking corner west, driveway to the garage east)
    y -352 ---- compound south wall: gate (force field) + combine door, checkpoint ----
           street (sidewalk / road / sidewalk), x -2560 .. 2304
    y-1024 ---- row of apartment blocks (building_template facades) ----
    west end: arch passage -> trigger_changelevel rp_c17_02_plaza (lm_02_08)

The architecture kit (subtractive shell, trims, props on floors) is in the first half of
this file; the map itself follows. Writes the VMF and <out>_steps.txt (camera steps for
run_game.ps1: one or more per room).
"""
import math
import os
import random
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from opora_kit import (Map, MISSING, NODRAW, SKY, PCLIP, NPCCLIP, TRIGGER, EPS, mdl, mat_ok, subtract_rects)  # noqa: E402

ARGS = list(sys.argv[1:])
STEPS_OUT = None
if '--steps' in ARGS:
    _i = ARGS.index('--steps')
    STEPS_OUT = ARGS[_i + 1]
    del ARGS[_i:_i + 2]
OUT = ARGS[0] if ARGS else r"E:\XBLAH's modding tool\content\Mapbase\hl2rpm\mapsrc\rp_c17_08_opora.vmf"
M = Map(OUT)
rnd = random.Random(808)
STEPS = []

ST = 128


def FL(k):
    return 8 + ST * k


def CL(k):
    return ST * (k + 1)


# ---------------------------------------------------------------- materials
FAC_G = "BUILDING_TEMPLATE/BUILDING_TEMPLATE012I"      # yellow stucco, grey plinth band
FAC = "BUILDING_TEMPLATE/BUILDING_TEMPLATE012A"        # yellow stucco
FAC_T = "BUILDING_TEMPLATE/BUILDING_TEMPLATE012K"      # yellow stucco, cornice band at the top
TRIM = "STONE/STONEWALL013A"                            # white stone: sills, string courses, cornice
PLINTH = "STONE/STONEWALL029A"
ROOF = "PROPS/RUBBERROOF002A"
FLASH = "METAL/METALROOF005A"
PARAPET_IN = "CONCRETE/CONCRETEWALL004A"
REVEAL_EXT = "PLASTER/PLASTERWALL034A"
FRAME = "WOOD/WOODBEAM001A"
WINFRAME = "PLASTER/PLASTERWALL034A"
GLASS = "GLASS/GLASSWINDOW007A"
BARS = "METAL/METALBAR001A"
CEIL = "PLASTER/PLASTERCEILING008A"
STEP_TOP = "STONE/STONESTAIR001A"
STEP_SIDE = "CONCRETE/CONCRETEWALL004A"
COMBINE_PANEL = "METAL/CITADEL_METALWALL063A"
NEXUS = "METAL/CITADEL_METALWALL072B"
NEXUS2 = "METAL/CITADEL_METALWALL072A"
COMPOUND = "CONCRETE/CONCRETEWALL064B"
SIDEWALK = "CONCRETE/CONCRETEFLOOR028C"
ROAD = "CONCRETE/CONCRETEFLOOR033A"
ROAD_LINE = "CONCRETE/CONCRETEFLOOR033K"
YARD = "CONCRETE/CONCRETEFLOOR023A"
DRIVE = "CONCRETE/CONCRETEFLOOR036A"
GRASS = "NATURE/RED_GRASS"
DIRT = "NATURE/DIRTFLOOR011A"

ROOM = {
    "corridor": dict(floor="TILE/TILEFLOOR013A", wall="PLASTER/PLASTERWALL034C", ceil=CEIL),
    "lobby": dict(floor="TILE/TILEFLOOR013A", wall="PLASTER/PLASTERWALL034C", ceil=CEIL),
    "stair": dict(floor="TILE/TILEFLOOR013A", wall="PLASTER/PLASTERWALL034C", ceil=CEIL),
    "class": dict(floor="WOOD/WOODFLOOR007A", wall="PLASTER/PLASTERWALL005C", ceil=CEIL),
    "interrogation": dict(floor="WOOD/WOODFLOOR007A", wall="PLASTER/PLASTERWALL042C", ceil=CEIL),
    "med": dict(floor="TILE/TILEFLOOR007B", wall="TILE/TILEWALL009B", ceil="PLASTER/PLASTERWALL003A"),
    "armoury": dict(floor="CONCRETE/CONCRETEFLOOR011A", wall="CONCRETE/CONCRETEWALL041A", ceil=PARAPET_IN),
    "office": dict(floor="WOOD/WOODFLOOR005A", wall="PLASTER/PLASTERWALL030C", ceil=CEIL),
    "archive": dict(floor="WOOD/WOODFLOOR007A", wall="PLASTER/PLASTERWALL032C", ceil=CEIL),
    "cells": dict(floor="CONCRETE/CONCRETEFLOOR011A", wall="CONCRETE/CONCRETEWALL041A", ceil=PARAPET_IN),
    "cell": dict(floor="CONCRETE/CONCRETEFLOOR026A", wall="CONCRETE/CONCRETEWALL041E", ceil=PARAPET_IN),
    "canteen": dict(floor="TILE/TILEFLOOR012A", wall="TILE/TILEWALL009B", ceil=CEIL),
    "gym": dict(floor="WOOD/WOODFLOOR002", wall="PLASTER/PLASTERWALL047B", wall_hi="PLASTER/PLASTERWALL047A",
                ceil=PARAPET_IN, band=128),
    "garage": dict(floor="CONCRETE/CONCRETEFLOOR011A", wall="CONCRETE/CONCRETEWALL032C",
                   wall_hi="CONCRETE/CONCRETEWALL032A", ceil="METAL/METALROOF006A", band=128),
}


def room(name, a, b, preset, **kw):
    d = dict(ROOM[preset])
    d.update(kw)
    return M.space(name, a, b, "in", **d)


# ---------------------------------------------------------------- main wing layout
MX0, MX1, MY0, MY1 = -744, 744, 320, 848
YS0, YS1 = 352, 640          # south rooms
YC0, YC1 = 656, 816          # corridor
BAY = {"stairW": (-712, -520), "R1": (-504, -264), "R2": (-248, -8), "R3": (8, 248), "R4": (264, 504),
       "stairE": (520, 712)}
ROOMS = []   # (space, preset, storey)


def add_room(name, x0, x1, k, preset, y0=YS0, y1=YS1, **kw):
    s = room(name, (x0, y0, FL(k)), (x1, y1, CL(k)), preset, **kw)
    ROOMS.append((s, preset, k))
    return s


# stairwells (open through all storeys they serve)
stairW = room("stairW", (BAY["stairW"][0], YS0, FL(0)), (BAY["stairW"][1], YS1, CL(2)), "stair", soundscape="general.concrete_quiet")
stairE = room("stairE", (BAY["stairE"][0], YS0, FL(0)), (BAY["stairE"][1], YS1, 520), "stair", soundscape="general.concrete_quiet")
# corridors
corr0W = add_room("corridor0W", -712, -248, 0, "corridor", YC0, YC1)
corr0E = add_room("corridor0E", 248, 712, 0, "corridor", YC0, YC1)
corr1 = add_room("corridor1", -712, 712, 1, "corridor", YC0, YC1)
corr2 = add_room("corridor2", -712, 712, 2, "corridor", YC0, YC1)
# ground floor
med = add_room("medblock", *BAY["R1"], 0, "med")
lobby = add_room("lobby", -248, 248, 0, "lobby", YS0, YC1)
armoury = add_room("armoury", *BAY["R4"], 0, "armoury")
# first floor: four interrogation rooms (former classrooms)
ints = [add_room("interrogation%d" % (i + 1), *BAY["R%d" % (i + 1)], 1, "interrogation") for i in range(4)]
# second floor
barracks = add_room("barracks", BAY["R1"][0], BAY["R2"][1], 2, "class")
schulz = add_room("schulz_office", *BAY["R3"], 2, "office")
archive = add_room("archive", *BAY["R4"], 2, "archive")

# annexes behind the main wing
cellsCorr = add_room("cellblock", -712, -248, 0, "cells", 848, 976)
cells = [add_room("cell%d" % (i + 1), -712 + 120 * i, -608 + 120 * i, 0, "cell", 976, 1136) for i in range(4)]
canteen = add_room("canteen", -184, 184, 0, "canteen", 848, 1136)
gym = M.space("gym", (248, 848, FL(0)), (712, 1328, 248), "in", **ROOM["gym"])
ROOMS.append((gym, "gym", 0))
garage = M.space("garage", (1024, 96, 0), (1440, 576, 224), "in", **ROOM["garage"])
ROOMS.append((garage, "garage", 0))


def school_facade(z0, z1, f, lo, hi):
    if z1 <= 128 + EPS:
        return FAC_G, 128
    if z1 <= 256 + EPS:
        return FAC, 256
    if z1 <= 384 + EPS:
        return FAC_T, 384
    return FAC, 512


def annex_facade(z0, z1, f, lo, hi):
    if z1 <= 128 + EPS:
        return FAC_G, 128
    if z1 <= 256 + EPS:
        return FAC_T, 256
    return FAC, 384


M.envelope("main", (MX0, MY0, -16), (MX1, MY1, 392), school_facade, zlines=(0, 128, 256, 384))
M.envelope("headhouse", (504, 320, 392), (744, 672, 528), lambda z0, z1, f, lo, hi: (FAC, 512), zlines=())
M.envelope("cells", (-744, 848, -16), (-216, 1168, 136), annex_facade, zlines=(0, 128))
M.envelope("canteen", (-216, 848, -16), (216, 1168, 136), annex_facade, zlines=(0, 128))
M.envelope("gym", (216, 848, -16), (744, 1360, 264), annex_facade, zlines=(0, 128, 256))
M.envelope("garage", (992, 64, -16), (1472, 608, 240), lambda z0, z1, f, lo, hi: ("BRICK/BRICKWALL031A", None),
           top=ROOF, zlines=(0,))

# ---------------------------------------------------------------- openings
WINDOWS = []     # dicts for the frame/glass pass
DOORS = []       # dicts for the frame/door pass


def window(axis, w0, w1, t0, t1, z0, z1, out_sign, bars=False, name=None, inner_board=True, frame=True):
    a = (w0, t0, z0) if axis == "y" else (t0, w0, z0)
    b = (w1, t1, z1) if axis == "y" else (t1, w1, z1)
    M.opening(a, b, REVEAL_EXT, name or "window")
    WINDOWS.append(dict(axis=axis, w0=w0, w1=w1, t0=t0, t1=t1, z0=z0, z1=z1, out=out_sign, bars=bars,
                        board=inner_board, frame=frame))


def door(axis, w0, t0, t1, z0, width=56, height=112, kind="wood", name=None, locked=False, hinge="lo",
         frame=True, prop=True, reveal=FRAME, open_angle=None):
    w1 = w0 + width
    a = (w0, t0, z0) if axis == "y" else (t0, w0, z0)
    b = (w1, t1, z0 + height) if axis == "y" else (t1, w1, z0 + height)
    M.opening(a, b, reveal, "door")
    d = dict(axis=axis, w0=w0, w1=w1, t0=t0, t1=t1, z0=z0, h=height, kind=kind, name=name, locked=locked,
             hinge=hinge, frame=frame, prop=prop, open_angle=open_angle)
    DOORS.append(d)
    return d


def passage(axis, w0, w1, t0, t1, z0, z1, reveal=REVEAL_EXT):
    a = (w0, t0, z0) if axis == "y" else (t0, w0, z0)
    b = (w1, t1, z1) if axis == "y" else (t1, w1, z1)
    return M.opening(a, b, reveal, "passage")


# stairwells <-> corridors
for k in range(3):
    passage("y", -680, -552, YS1, YC0, FL(k), FL(k) + 112)
    passage("y", 552, 680, YS1, YC0, FL(k), FL(k) + 112)
# rooms -> corridor doors (through the partition y 640..656)
for s, preset, k in ROOMS:
    if preset in ("interrogation", "class", "office", "archive", "med", "armoury") and s.a[1] == YS0:
        x1 = s.b[0]
        if s is barracks:
            for dx in (-312, -88):
                door("y", x1 + dx, YS1, YC0, FL(k), name="door_barracks_%d" % dx)
            continue
        door("y", x1 - 80, YS1, YC0, FL(k), name="door_" + s.name, locked=(s is armoury),
             kind="metal" if s in (armoury,) else "wood")
# lobby: main entrance (double door), windows each side
door("y", -52, MY0, YS0, FL(0), width=104, kind="double", name="door_main")
# lobby north side: canteen (double), corridor to cells, gym (double)
door("y", -52, YC1, MY1, FL(0), width=104, kind="double", name="door_canteen")
door("y", -680, YC1, MY1, FL(0), name="door_cellblock", kind="metal")
door("y", 428, YC1, MY1, FL(0), width=104, kind="double", name="door_gym")
# head-house door to the roof
door("y", 572, 640, 672, 392, name="door_roof", kind="metal")
# gym back door, canteen back door, garage gate, cell block back door? (no: prisoners)
door("y", 600, 1328, 1360, FL(0), name="door_gym_back", kind="metal")
door("y", 100, 1136, 1168, FL(0), name="door_kitchen_back", kind="metal")
garage_gate = passage("y", 1104, 1360, 64, 96, 0, 184, reveal="CONCRETE/CONCRETEWALL032A")
door("y", 1400 - 56, 64, 96, 0, name="door_garage_side", kind="metal", hinge="hi")

# windows: south facade (yard)
for s, preset, k in ROOMS:
    if s.a[1] != YS0 or s is lobby:
        continue
    x0, x1 = s.a[0], s.b[0]
    n = 2 if x1 - x0 < 300 else 4
    step = (x1 - x0) / n
    for i in range(n):
        cx = x0 + step * (i + 0.5)
        window("y", cx - 28, cx + 28, MY0, YS0, FL(k) + 32, FL(k) + 112, -1, bars=(k == 0))
for cx in (-184, 184):
    window("y", cx - 28, cx + 28, MY0, YS0, FL(0) + 32, FL(0) + 112, -1, bars=True)
for cx in (-112, 112):           # narrow windows beside the entrance
    window("y", cx - 12, cx + 12, MY0, YS0, FL(0) + 40, FL(0) + 104, -1, bars=True)
# north facade: corridor windows where no annex is behind
for k in (1, 2):
    for cx in (-616, -384, -128, 128, 384, 616):
        if k == 1 and cx > 216:
            continue
        window("y", cx - 28, cx + 28, YC1, MY1, FL(k) + 32, FL(k) + 112, +1)
# corridor ends and stairwell end walls (west / east)
for k in range(3):
    for (t0, t1, sg) in ((MX0, -712, -1), (712, MX1, +1)):
        window("x", 708, 764, t0, t1, FL(k) + 32, FL(k) + 112, sg, bars=(k == 0))
        window("x", 560, 616, t0, t1, FL(k) + 32, FL(k) + 112, sg, bars=(k == 0))
# head-house window (east)
window("x", 440, 496, 712, 744, 392 + 32, 392 + 104, +1)
# cells: small barred windows high up, canteen windows, gym high windows
for i in range(4):
    cx = -660 + 120 * i
    window("y", cx - 16, cx + 16, 1136, 1168, FL(0) + 72, FL(0) + 104, +1, bars=True, inner_board=False)
for cx in (-120, -20):
    window("y", cx - 28, cx + 28, 1136, 1168, FL(0) + 32, FL(0) + 112, +1, bars=True)
for cy in (960, 1088, 1216):
    window("x", cy - 36, cy + 36, 712, 744, 136, 232, +1, bars=False)
for cx in (344, 472):
    window("y", cx - 36, cx + 36, 1328, 1360, 136, 232, +1, bars=False)
# garage: two high windows on the east wall
for cy in (240, 432):
    window("x", cy - 40, cy + 40, 1440, 1472, 120, 184, +1, bars=True, inner_board=False)

M.build_envelopes()
print("envelopes:", ", ".join("%s %d" % (e.name, e.brushes) for e in M.envelopes))

# ================================================================ stairs
def flight(lane, y_lo, y_hi, z_base, up_dir, k, first):
    """7 steps + the clip ramp. lane (x0, x1); the flight rises from z_base (the step 0 level)
    by 8 per 12-unit tread in direction up_dir (-1 = towards -y)."""
    x0, x1 = lane
    for i in range(1, 8):
        top = z_base + 8 * i
        if up_dir < 0:
            ya, yb = y_hi - 12 * i, y_hi - 12 * (i - 1)
        else:
            ya, yb = y_lo + 12 * (i - 1), y_lo + 12 * i
        bot = FL(k) if first else max(top - 24, FL(k))
        M.detail((x0, ya, bot), (x1, yb, top), {"top": STEP_TOP, "all": STEP_SIDE, "bottom": CEIL}, group="stairs")
    # clip ramp over the nosings (34 degrees)
    if up_dir < 0:
        M.detail_solid(M.ramp_solid("y", x0, x1, y_hi + 12, z_base, y_lo, z_base + 64, 16, PCLIP), "stairclip")
    else:
        M.detail_solid(M.ramp_solid("y", x0, x1, y_lo - 12, z_base, y_hi, z_base + 64, 16, PCLIP), "stairclip")


def stairwell(sp, levels):
    x0, x1 = sp.a[0], sp.b[0]
    xm = (x0 + x1) // 2
    laneA, laneB = (x0, xm - 8), (xm + 8, x1)
    for k in range(levels):
        F = FL(k)
        flight(laneA, 448, 532, F, -1, k, k == 0)
        M.detail((x0, 352, F if k == 0 else F + 56), (x1, 448, F + 64),
                 {"top": ROOM["stair"]["floor"], "all": STEP_SIDE, "bottom": CEIL}, group="stairs")
        flight(laneB, 448, 532, F + 64, +1, k, k == 0)
        M.detail((x0, 532, F + 120), (x1, 640, F + 128),
                 {"top": ROOM["stair"]["floor"], "all": STEP_SIDE, "bottom": CEIL}, group="stairs")
        # the spine between the flights
        M.world((xm - 8, 448, F), (xm + 8, 532, F + 128), ROOM["stair"]["wall"],
                offs={f: (0, ((F + 128) / 0.25) % 512) for f in ("left", "right")})
    top = sp.b[2]
    if top > FL(levels) + 8:
        M.world((xm - 8, 448, FL(levels)), (xm + 8, 532, top), ROOM["stair"]["wall"])


stairwell(stairW, 2)
stairwell(stairE, 3)

# ================================================================ window and door details
def _box(axis, w0, w1, t0, t1, z0, z1):
    if axis == "y":
        return (min(w0, w1), min(t0, t1), z0), (max(w0, w1), max(t0, t1), z1)
    return (min(t0, t1), min(w0, w1), z0), (max(t0, t1), max(w0, w1), z1)


def dbox(axis, w0, w1, t0, t1, z0, z1, mats, group="trim"):
    a, b = _box(axis, w0, w1, t0, t1, z0, z1)
    return M.detail(a, b, mats, group=group)


for wdef in WINDOWS:
    ax, w0, w1, t0, t1, z0, z1, out = (wdef[k] for k in ("axis", "w0", "w1", "t0", "t1", "z0", "z1", "out"))
    tm = (t0 + t1) // 2
    t_out = t0 if out < 0 else t1          # facade plane
    t_in = t1 if out < 0 else t0
    if wdef["frame"]:
        # frame at mid-depth: jambs, head, sill rail, mullion, transom; glass pane behind
        dbox(ax, w0, w0 + 3, tm - 2, tm + 2, z0, z1, WINFRAME)
        dbox(ax, w1 - 3, w1, tm - 2, tm + 2, z0, z1, WINFRAME)
        dbox(ax, w0 + 3, w1 - 3, tm - 2, tm + 2, z1 - 3, z1, WINFRAME)
        dbox(ax, w0 + 3, w1 - 3, tm - 2, tm + 2, z0, z0 + 3, WINFRAME)
        if w1 - w0 >= 40:
            wc = (w0 + w1) // 2
            dbox(ax, wc - 1, wc + 1, tm - 2, tm + 2, z0 + 3, z1 - 3, WINFRAME)
        zt = z0 + int((z1 - z0) * 0.72)
        dbox(ax, w0 + 3, w1 - 3, tm - 2, tm + 2, zt - 1, zt + 1, WINFRAME)
        dbox(ax, w0 + 3, w1 - 3, tm, tm + 1, z0 + 3, z1 - 3, {"all": NODRAW, "back" if ax == "y" else "right": GLASS,
                                                             "front" if ax == "y" else "left": GLASS}, group="glass")
    # exterior: sill and stucco surround
    so = 1 if out > 0 else -1
    dbox(ax, w0 - 6, w1 + 6, t_out, t_out + so * 6, z0 - 6, z0, TRIM)
    dbox(ax, w0 - 6, w0, t_out, t_out + so * 2, z0, z1 + 6, TRIM)
    dbox(ax, w1, w1 + 6, t_out, t_out + so * 2, z0, z1 + 6, TRIM)
    dbox(ax, w0, w1, t_out, t_out + so * 2, z1, z1 + 6, TRIM)
    if wdef["bars"]:
        dbox(ax, w0, w1, t_out - so * 4, t_out - so * 3, z0, z1, {"all": NODRAW, "back" if ax == "y" else "right": BARS,
                                                                  "front" if ax == "y" else "left": BARS}, group="bars")
    if wdef["board"]:
        dbox(ax, w0 - 4, w1 + 4, t_in, t_in - so * 4, z0 - 2, z0, "WOOD/WOODWALL009A")

DOOR_MODEL = {"wood": "models/props_c17/door01_left.mdl", "metal": "models/props_doors/door03_slotted_left.mdl",
              "double": "models/props_c17/door01_left.mdl"}
for d in DOORS:
    ax, w0, w1, t0, t1, z0, h = (d[k] for k in ("axis", "w0", "w1", "t0", "t1", "z0", "h"))
    zt = z0 + h
    if d["frame"]:
        # linings inside the hole and casings on both wall faces
        dbox(ax, w0, w0 + 4, t0, t1, z0, zt, FRAME, group="doorframes")
        dbox(ax, w1 - 4, w1, t0, t1, z0, zt, FRAME, group="doorframes")
        dbox(ax, w0 + 4, w1 - 4, t0, t1, zt - 4, zt, FRAME, group="doorframes")
        for tt, sg in ((t0, -1), (t1, 1)):
            dbox(ax, w0 - 6, w0, tt, tt + sg * 2, z0, zt + 6, FRAME, group="doorframes")
            dbox(ax, w1, w1 + 6, tt, tt + sg * 2, z0, zt + 6, FRAME, group="doorframes")
            dbox(ax, w0, w1, tt, tt + sg * 2, zt, zt + 6, FRAME, group="doorframes")
    if not d["prop"]:
        continue
    tm = (t0 + t1) / 2.0
    model = DOOR_MODEL[d["kind"]]
    sf = 8192 | (2048 if d["locked"] else 0)
    leaves = [("lo", w0 + 4)] if d["kind"] != "double" else [("lo", w0 + 4), ("hi", w1 - 4)]
    if d["kind"] != "double" and d["hinge"] == "hi":
        leaves = [("hi", w1 - 4)]
    for side, hw in leaves:
        # door01_left extends along local +y from the hinge
        if ax == "y":
            yaw = 270 if side == "lo" else 90
            pos = (hw, tm, z0 + 54)
        else:
            yaw = 0 if side == "lo" else 180
            pos = (tm, hw, z0 + 54)
        keys = dict(model=model, angles="0 %d 0" % yaw, skin="0", distance="90", speed="100", spawnpos="0",
                    opendir="0", hardware="1", returndelay="-1", spawnflags=str(sf), forceclosed="0",
                    health="0", ajarangles="0 0 0", fademindist="-1")
        if d["name"]:
            keys["targetname"] = d["name"] + ("" if len(leaves) == 1 else "_" + side)
        M.ent("prop_door_rotating", pos, **keys)

# top landings: a low wall over the open lane
for sp, lv in ((stairW, 2), (stairE, 3)):
    x0, x1 = sp.a[0], sp.b[0]
    xm = (x0 + x1) // 2
    M.detail((x0, 528, FL(lv)), (xm - 8, 532, FL(lv) + 40), ROOM["stair"]["wall"], group="stairs")
    M.detail((x0, 527, FL(lv) + 40), (xm - 8, 533, FL(lv) + 44), FRAME, group="stairs")


# ================================================================ facade trims
def facade_band(z0, z1, depth, mat, gaps=()):
    """a horizontal band around the main wing at z0..z1 protruding 'depth', interrupted at gaps
    ((side, w0, w1) with side S / N / W / E)"""
    def run(axis, fixed, side, w0, w1):
        cuts = sorted([(g0, g1) for (fa, g0, g1) in gaps if fa == side])
        cur = w0
        for g0, g1 in cuts + [(w1, w1)]:
            if g0 > cur:
                if axis == "y":
                    t = (fixed, fixed - depth) if side == "S" else (fixed, fixed + depth)
                    M.detail((cur, min(t), z0), (g0, max(t), z1), mat, group="facade")
                else:
                    t = (fixed, fixed - depth) if side == "W" else (fixed, fixed + depth)
                    M.detail((min(t), cur, z0), (max(t), g0, z1), mat, group="facade")
            cur = max(cur, g1)
    run("y", MY0, "S", MX0 - depth, MX1 + depth)
    run("y", MY1, "N", MX0 - depth, MX1 + depth)
    run("x", MX0, "W", MY0, MY1)
    run("x", MX1, "E", MY0, MY1)


facade_band(0, 16, 6, PLINTH, gaps=[("S", -136, 136), ("N", -760, 760)])
facade_band(124, 132, 4, TRIM, gaps=[("S", -144, 144), ("N", -760, 760)])
facade_band(252, 260, 4, TRIM, gaps=[("N", 216, 760)])
facade_band(372, 388, 8, TRIM)
facade_band(388, 392, 12, TRIM)
# pilasters at the corners and between the window bays
for px in (-744, -512, -256, 256, 512, 744):
    x0 = px - 16 if abs(px) != 744 else (px if px < 0 else px - 32)
    M.detail((x0, MY0 - 6, 16), (x0 + 32, MY0, 372), TRIM, group="facade")
    zb = 136 if px < 216 else 264
    M.detail((x0, MY1, zb), (x0 + 32, MY1 + 6, 372), TRIM, group="facade")

# entrance portico: platform, columns, canopy
M.detail((-136, 224, 0), (136, MY0, 8), {"top": "STONE/STONEFLOOR006A", "all": PLINTH}, group="facade")
for cx in (-120, 104):
    M.detail((cx, 232, 8), (cx + 16, 248, 132), TRIM, group="facade", block=True)
M.detail((-144, 216, 132), (144, MY0, 144), {"all": TRIM, "bottom": "PLASTER/PLASTERWALL034A"}, group="facade")
M.detail((-144, 216, 144), (144, MY0, 148), FLASH, group="facade")


# parapets on the flat roofs
def parapet(x0, y0, x1, y1, z, h=32, t=16, skip=()):
    for (a, b) in (((x0, y0, z), (x1, y0 + t, z + h)), ((x0, y1 - t, z), (x1, y1, z + h)),
                   ((x0, y0 + t, z), (x0 + t, y1 - t, z + h)), ((x1 - t, y0 + t, z), (x1, y1 - t, z + h))):
        if any(s[0] < b[0] and s[2] > a[0] and s[1] < b[1] and s[3] > a[1] for s in skip):
            continue
        M.world(a, b, {"top": FLASH, "all": FAC})


parapet(MX0, MY0, MX1, MY1, 392, skip=[(504, 320, 744, 672)])
M.world((744 - 16, 672, 392), (744, MY1 - 16, 424), {"top": FLASH, "all": FAC})
M.world((488, MY0, 392), (504, 672, 424), {"top": FLASH, "all": FAC})
parapet(-744, 848, -216, 1168, 136, h=24)
parapet(-216, 848, 216, 1168, 136, h=24)
parapet(216, 848, 744, 1360, 264, h=24)
parapet(992, 64, 1472, 608, 240, h=16)

# ================================================================ ground, street, compound
CX0, CY0, CX1, CY1 = -1280, -384, 1536, 1600         # compound outer bounds
W = 32
FOOT = [(MX0, MY0, MX1, MY1), (-744, 848, -216, 1168), (-216, 848, 216, 1168), (216, 848, 744, 1360),
        (992, 64, 1472, 608)]
GATE = (-160, 160)
PEDDOOR = (224, 416)
BACKGATE = (-64, 128)
WALLS = [  # compound walls (x0, y0, x1, y1)
    (CX0, CY0, GATE[0], CY0 + W), (GATE[1], CY0, PEDDOOR[0], CY0 + W), (PEDDOOR[1], CY0, CX1, CY0 + W),
    (CX0, CY1 - W, BACKGATE[0], CY1), (BACKGATE[1], CY1 - W, CX1, CY1),
    (CX0, CY0 + W, CX0 + W, CY1 - W), (CX1 - W, CY0 + W, CX1, CY1 - W)]
for (x0, y0, x1, y1) in WALLS:
    M.world((x0, y0, -16), (x1, y1, 224), {"top": "CONCRETE/CONCRETEFLOOR028A", "all": COMPOUND, "bottom": NODRAW})
    M.detail((x0 - 2, y0 - 2, 216), (x1 + 2, y1 + 2, 224), "CONCRETE/CONCRETEFLOOR028A", group="compound")
M.world((PEDDOOR[0], CY0, 192), (PEDDOOR[1], CY0 + W, 224), {"top": "CONCRETE/CONCRETEFLOOR028A", "all": COMPOUND})

# yard ground: base slab and the top layer around the buildings
M.world((CX0, CY0, -48), (CX1, CY1, -16), {"all": NODRAW, "top": YARD})
DRIVEWAY = (-160, CY0 + W, 1400, 64)        # asphalt from the gate to the garage
for (x0, y0, x1, y1) in subtract_rects((CX0 + W, CY0 + W, CX1 - W, CY1 - W), FOOT):
    for (a0, b0, a1, b1) in subtract_rects((x0, y0, x1, y1), [DRIVEWAY]):
        mat = YARD if b0 < 1170 else "CONCRETE/CONCRETEFLOOR013A"
        M.world((a0, b0, -16), (a1, b1, 0), {"all": NODRAW, "top": mat})
    dx0, dy0 = max(x0, DRIVEWAY[0]), max(y0, DRIVEWAY[1])
    dx1, dy1 = min(x1, DRIVEWAY[2]), min(y1, DRIVEWAY[3])
    if dx0 < dx1 and dy0 < dy1:
        M.world((dx0, dy0, -16), (dx1, dy1, 0), {"all": NODRAW, "top": DRIVE})
M.world((GATE[0], CY0, -16), (GATE[1], CY0 + W, 0), {"all": NODRAW, "top": DRIVE})
M.world((PEDDOOR[0], CY0, -16), (PEDDOOR[1], CY0 + W, 0), {"all": NODRAW, "top": SIDEWALK})
M.world((PEDDOOR[0], CY0, 0), (PEDDOOR[1], CY0 + W, 2), {"all": NODRAW, "top": SIDEWALK})
M.world((BACKGATE[0], CY1 - W, -16), (BACKGATE[1], CY1, 0), {"all": NODRAW, "top": "CONCRETE/CONCRETEFLOOR013A"})
# grass beds along the compound walls (yard)
for (x0, y0, x1, y1) in ((-1240, -340, -1032, 160), (1080, -340, 1496, -200), (-1240, 1180, -800, 1560),
                         (800, 1380, 1496, 1560)):
    M.detail((x0, y0, 0), (x1, y1, 2), {"all": "CONCRETE/CONCRETEFLOOR028A", "top": GRASS}, group="ground")

# street
SX0, SX1 = -2560, 2304
M.world((SX0, -1024, -48), (SX1, -384, -32), {"all": NODRAW})
M.world((SX0, -1024, -32), (SX1, -896, 0), {"all": NODRAW, "top": SIDEWALK, "back": SIDEWALK})       # south sidewalk
M.world((SX0, -512, -32), (SX1, -384, 0), {"all": NODRAW, "top": SIDEWALK, "front": SIDEWALK})       # north sidewalk
M.world((SX0, -896, -32), (SX1, -720, -8), {"all": NODRAW, "top": ROAD})
M.world((SX0, -720, -32), (SX1, -688, -8), {"all": NODRAW, "top": ROAD_LINE})
M.world((SX0, -688, -32), (SX1, -512, -8), {"all": NODRAW, "top": ROAD})


# ================================================================ surrounding city blocks
def block(x0, y0, x1, y1, h, sides, tpl, bay=128):
    """a city block: 'sides' = faces that show the facade (front back left right; the first one
    gets the ground storey in bays, every third an alternate: doors, shops), upper storeys, top band"""
    g, ga, up, tb = TPL[tpl]
    side = sides[0]
    horiz = "x" if side in ("front", "back") else "y"
    lo, hi = (x0, x1) if horiz == "x" else (y0, y1)
    n = max(1, int(round((hi - lo) / float(bay))))
    edges = [int(lo + (hi - lo) * i / n) for i in range(n + 1)]

    def faces(mat, top=NODRAW, alt=None):
        d = {f: NODRAW for f in ("left", "right", "back", "front", "bottom")}
        for s_ in sides:
            d[s_] = mat
        if alt:
            d[side] = alt
        d["top"] = top
        return d
    for i in range(n):
        alt = ga if i % 3 == 1 else None
        if horiz == "x":
            M.world((edges[i], y0, -32), (edges[i + 1], y1, 128), faces(g, alt=alt))
        else:
            M.world((x0, edges[i], -32), (x1, edges[i + 1], 128), faces(g, alt=alt))
    if h - 128 > 128:
        M.world((x0, y0, 128), (x1, y1, h - 128), faces(up))
    M.world((x0, y0, h - 128), (x1, y1, h), faces(tb, ROOF))
    for s_ in sides:
        if s_ == "back":
            M.detail((x0, y1, h - 8), (x1, y1 + 8, h), "BUILDING_TEMPLATE/BUILDING_TEMPLATE002K", group="city")
        elif s_ == "front":
            M.detail((x0, y0 - 8, h - 8), (x1, y0, h), "BUILDING_TEMPLATE/BUILDING_TEMPLATE002K", group="city")
        elif s_ == "right":
            M.detail((x1, y0, h - 8), (x1 + 8, y1, h), "BUILDING_TEMPLATE/BUILDING_TEMPLATE002K", group="city")
        else:
            M.detail((x0 - 8, y0, h - 8), (x0, y1, h), "BUILDING_TEMPLATE/BUILDING_TEMPLATE002K", group="city")
    BLOCKS.append((x0, y0, x1, y1, h))


TPL = {  # ground, ground alt (doors/shops), upper, top band
    "002": ("BUILDING_TEMPLATE/BUILDING_TEMPLATE002F", "BUILDING_TEMPLATE/BUILDING_TEMPLATE002E",
            "BUILDING_TEMPLATE/BUILDING_TEMPLATE002B", "BUILDING_TEMPLATE/BUILDING_TEMPLATE002C"),
    "012": ("BUILDING_TEMPLATE/BUILDING_TEMPLATE012I", "BUILDING_TEMPLATE/BUILDING_TEMPLATE012L",
            "BUILDING_TEMPLATE/BUILDING_TEMPLATE012B", "BUILDING_TEMPLATE/BUILDING_TEMPLATE012K"),
    "003": ("STOREFRONT_TEMPLATE/STOREFRONT_TEMPLATE001A", "STOREFRONT_TEMPLATE/STOREFRONT_TEMPLATE001C",
            "BUILDING_TEMPLATE/BUILDING_TEMPLATE003E", "BUILDING_TEMPLATE/BUILDING_TEMPLATE003E"),
    "021": ("BUILDING_TEMPLATE/BUILDING_TEMPLATE021A", "BUILDING_TEMPLATE/BUILDING_TEMPLATE021B",
            "BUILDING_TEMPLATE/BUILDING_TEMPLATE021C", "BUILDING_TEMPLATE/BUILDING_TEMPLATE021C"),
    "019": ("BUILDING_TEMPLATE/BUILDING_TEMPLATE019A", "BUILDING_TEMPLATE/BUILDING_TEMPLATE019H",
            "BUILDING_TEMPLATE/BUILDING_TEMPLATE019C", "BUILDING_TEMPLATE/BUILDING_TEMPLATE019D"),
}
BLOCKS = []
ROW = [(-2560, -2048, 512, "002"), (-2048, -1536, 640, "012"), (-1536, -1152, 576, "003"), (-1152, -512, 512, "021"),
       (-512, 0, 640, "002"), (0, 384, 576, "019"), (384, 1024, 640, "012"), (1024, 1536, 512, "003"),
       (1536, 2304, 576, "021")]
for (x0, x1, h, t) in ROW:
    block(x0, -1344, x1, -1024, h, ["back"], t)
# west and east of the compound (north side of the street): facades to the street and to the yard
block(-2560, -384, CX0, 1856, 576, ["front", "right"], "012")
block(CX1, -384, SX1, 1856, 640, ["front", "left"], "002")
# the Nexus behind the north wall: Combine wall with buttresses
M.world((CX0, 1664, -32), (CX1, 1856, 960), {"front": NEXUS, "top": NEXUS2, "all": NODRAW})
M.world((CX0, CY1, -32), (CX1, 1664, 0), {"all": NODRAW, "top": "CONCRETE/CONCRETEFLOOR013A"})
for i, x in enumerate(range(CX0 + 128, CX1 - 128, 384)):
    M.detail((x, 1632, 0), (x + 96, 1664, 960 + (64 if i % 2 else 0)), {"all": NEXUS2, "back": NODRAW}, group="nexus")
# street ends: west block with the arch to the plaza, east block closing the street
WX0, WX1 = -2880, SX0
PASS_Y = (-768, -512)
M.world((WX0, -1344, -32), (WX1, PASS_Y[0], 640), {"right": TPL["002"][2], "back": "BRICK/BRICKWALL017A", "top": ROOF, "all": NODRAW})
M.world((WX0, PASS_Y[1], -32), (WX1, 1856, 640), {"right": TPL["002"][2], "front": "BRICK/BRICKWALL017A", "top": ROOF, "all": NODRAW})
M.world((WX0, PASS_Y[0], 224), (WX1, PASS_Y[1], 640), {"right": TPL["002"][2], "bottom": "CONCRETE/CONCRETEWALL004A",
                                                        "top": ROOF, "all": NODRAW})
M.world((WX0, PASS_Y[0], -48), (WX1, PASS_Y[1], -8), {"all": NODRAW, "top": ROAD})
M.detail((WX0 + 1, PASS_Y[0], -8), (WX0 + 2, PASS_Y[1], 224), {"all": NODRAW, "right": "EFFECTS/FOG_D1_TRAINSTATION_02"},
         group="fogcard")
# east end: the block with the arch to the Old Town (rp_c17_04_oldtown, lm_08_04), like the west arch
EX0, EX1 = SX1, 2624
M.world((EX0, -1344, -32), (EX1, PASS_Y[0], 576), {"left": TPL["021"][2], "front": "BRICK/BRICKWALL017A", "top": ROOF, "all": NODRAW})
M.world((EX0, PASS_Y[1], -32), (EX1, 1856, 576), {"left": TPL["021"][2], "back": "BRICK/BRICKWALL017A", "top": ROOF, "all": NODRAW})
M.world((EX0, PASS_Y[0], 224), (EX1, PASS_Y[1], 576), {"left": TPL["021"][2], "bottom": "CONCRETE/CONCRETEWALL004A",
                                                       "top": ROOF, "all": NODRAW})
M.world((EX0, PASS_Y[0], -48), (EX1, PASS_Y[1], -8), {"all": NODRAW, "top": ROAD})
M.detail((EX1 - 2, PASS_Y[0], -8), (EX1 - 1, PASS_Y[1], 224), {"all": NODRAW, "left": "EFFECTS/FOG_D1_TRAINSTATION_02"},
         group="fogcard")

# ================================================================ hull (sky)
HX0, HY0, HZ0, HX1, HY1, HZ1 = -2896, -1360, -64, 2640, 1872, 1536
M.world((HX0, HY0, HZ0), (HX1, HY1, HZ0 + 16), NODRAW)
M.world((HX0, HY0, HZ1), (HX1, HY1, HZ1 + 16), SKY)
M.world((HX0, HY0, HZ0 + 16), (HX0 + 16, HY1, HZ1), SKY)
M.world((HX1 - 16, HY0, HZ0 + 16), (HX1, HY1, HZ1), SKY)
M.world((HX0 + 16, HY0, HZ0 + 16), (HX1 - 16, HY0 + 16, HZ1), SKY)
M.world((HX0 + 16, HY1 - 16, HZ0 + 16), (HX1 - 16, HY1, HZ1), SKY)

# ================================================================ helpers for the interiors
def P(model, x, y, z, yaw=0, **kw):
    return M.prop("models/" + model + ".mdl", x, y, z, yaw, **kw)


def PH(model, x, y, z, yaw=0, **kw):
    """physics clutter (prop_physics when the model has a collision model)"""
    return M.prop("models/" + model + ".mdl", x, y, z, yaw, cls="prop_physics", **kw)


def PD(model, x, y, z, yaw=0, **kw):
    return M.prop("models/" + model + ".mdl", x, y, z, yaw, cls="prop_dynamic", **kw)


def jit(v, a=6):
    return v + rnd.uniform(-a, a)


LIGHTS = []


def spot(x, y, z, bright=420, color="254 216 146", cone=60, inner=30, name=None, pitch=-90, yaw=0):
    kv = dict(_light="%s %d" % (color, bright), _lightHDR="-1 -1 -1 1", _lightscaleHDR="1",
              _inner_cone=str(inner), _cone=str(cone), _exponent="1", _quadratic_attn="1", _linear_attn="0",
              _constant_attn="0", angles="%d %d 0" % (pitch, yaw), pitch=str(pitch), style="0", _distance="0")
    if name:
        kv["targetname"] = name
    LIGHTS.append((x, y, z))
    return M.ent("light_spot", (x, y, z), **kv)


def fill(x, y, z, bright=30, color="254 216 146"):
    LIGHTS.append((x, y, z))
    return M.ent("light", (x, y, z), _light="%s %d" % (color, bright), _lightHDR="-1 -1 -1 1", _lightscaleHDR="1",
                 _quadratic_attn="1", _linear_attn="0", _constant_attn="0", style="0")


def fluor(x, y, ceil, along="y", bright=420, color="210 230 240", light=True, model="props_interiors/lights_florescent01a"):
    """ceiling fluorescent fixture with its light"""
    yaw = 0 if along == "y" else 90
    P(model, x, y, ceil - 3, yaw, on_floor=False, block=False)
    if light:
        spot(x, y, ceil - 10, bright, color)


def radiator(x, y, z, yaw):
    P("props_interiors/radiator01a", x, y, z + 6, yaw, block=False)


def under_windows(s):
    """radiators under the windows of a room (inside, on the wall)"""
    for w in WINDOWS:
        if not w["board"]:
            continue
        if w["axis"] == "y" and w["z0"] > s.a[2] and w["z0"] < s.b[2]:
            wc = (w["w0"] + w["w1"]) / 2.0
            if not (s.a[0] < wc < s.b[0]):
                continue
            t_in = w["t1"] if w["out"] < 0 else w["t0"]
            if not (abs(t_in - s.a[1]) < 1 or abs(t_in - s.b[1]) < 1):
                continue
            if w["w1"] - w["w0"] < 40:
                continue
            yy = t_in + (6 if w["out"] < 0 else -6)
            radiator(wc, yy, s.a[2], 90 if w["out"] < 0 else 270)


def wall_panel(axis, w0, w1, t, z0, z1, sg, mat=COMBINE_PANEL, depth=4):
    """Combine cladding on a wall (sg: +1 the panel grows into +t)"""
    a, b = _box(axis, w0, w1, t, t + sg * depth, z0, z1)
    M.detail(a, b, mat, group="combine")


def table_clutter(x, y, z, n=3, spread=14):
    items = ["props_junk/garbage_coffeemug001a", "props_lab/clipboard", "props_c17/paper01", "props_junk/garbage_metalcan001a",
             "props_lab/binderblue", "props_junk/garbage_newspaper001a", "props_junk/popcan01a"]
    for i in range(n):
        PH(rnd.choice(items), jit(x, spread), jit(y, spread), z, rnd.uniform(0, 360))


def floor_trash(s, n=4):
    items = ["props_junk/garbage_newspaper001a", "props_junk/garbage_carboard002a", "props_c17/paper01",
             "props_junk/garbage_milkcarton002a", "props_junk/garbage_metalcan002a", "props_junk/garbage_plasticbottle003a"]
    for i in range(n):
        x = rnd.uniform(s.a[0] + 24, s.b[0] - 24)
        y = rnd.uniform(s.a[1] + 24, s.b[1] - 24)
        if blocked(x, y, s.a[2], 8):
            continue
        PH(rnd.choice(items), x, y, s.a[2], rnd.uniform(0, 360))


def blocked(x, y, z, r):
    for (x0, y0, x1, y1, z0, z1) in M.obstacles:
        if x0 - r < x < x1 + r and y0 - r < y < y1 + r and z0 - 4 < z + 32 and z1 > z + 2:
            return True
    return False


def cam(name, x, y, z, pitch, yaw, extra=""):
    """z: floor under the camera (setpos takes the feet)"""
    STEPS.append("shot|setpos %d %d %d; setang %d %d 0%s  // %s" % (x, y, z, pitch, yaw, ("; " + extra) if extra else "", name))


# ================================================================ ground floor
# ---- lobby: the duty desk hall
L = lobby
z = FL(0)
# duty counter: L-shaped, wood top over a Combine-panelled front
M.detail((24, 560, z), (232, 592, z + 40), {"all": COMBINE_PANEL, "top": "WOOD/WOODFLOOR005A"}, group="lobby", block=True)
M.detail((24, 592, z), (56, 688, z + 40), {"all": COMBINE_PANEL, "top": "WOOD/WOODFLOOR005A"}, group="lobby", block=True)
M.detail((20, 556, z + 40), (236, 596, z + 43), "WOOD/WOODFLOOR005A", group="lobby")
P("props_lab/monitor01a", 120, 580, z + 43, 270)
P("props_c17/computer01_keyboard", 120, 566, z + 43, 270, block=False)
PD("props_combine/combine_intmonitor001", 200, 640, z, 270, targetname="opora_terminal")
P("props_wasteland/controlroom_chair001a", 130, 620, z, 270)
P("props_wasteland/controlroom_filecabinet002a", 220, 700, z, 180)
P("props_wasteland/controlroom_filecabinet002a", 220, 728, z, 180)
P("props_lab/desklamp01", 70, 576, z + 43, 300, block=False)
table_clutter(170, 576, z + 43, 3, 10)
M.ent("info_target", (130, 640, z), targetname="spot_duty_officer", angles="0 270 0")
M.ent("info_target", (180, 560, z + 44), targetname="spot_terminal", angles="0 270 0")
# Combine cladding behind the desk, the wanted board on the west wall
wall_panel("y", 64, 248, YC1, z, z + 120, -1)
P("props_lab/corkboard001", -248, 540, z + 56, 0, on_floor=False, block=False, targetname="opora_wanted_board")
M.ent("info_target", (-200, 540, z), targetname="spot_wanted_board", angles="0 180 0")
for i, (yy, zz, w_, h_) in enumerate(((452, 70, 40, 40), (620, 64, 48, 24), (468, 30, 40, 20), (630, 92, 40, 40))):
    M.overlay("DECALS/BILLS0%dA" % (1 + i), (-248, yy, z + zz), (1, 0, 0), w_, h_)
# detainee benches along the west wall, a trash can, the clock
P("props_trainstation/bench_indoor001a", -224, 420, z, 0)
P("props_trainstation/bench_indoor001a", -224, 700, z, 0)
P("props_trainstation/trashcan_indoor001a", -230, 620, z, 0)
P("props_trainstation/trainstation_clock001", 140, YC1, z + 88, 270, on_floor=False, block=False)
P("props_combine/combine_interface001", -150, 780, z, 270)
for (xx, yy) in ((-128, 450), (128, 450), (-128, 700), (128, 700)):
    fluor(xx, yy, CL(0), "x", 380)
M.overlay("DECALS/DECAL_POSTERBREEN", (-120, YC1, z + 72), (0, -1, 0), 64, 64)
M.overlay("DECALS/DECAL_POSTERS002A", (-150, YS0, z + 76), (0, 1, 0), 64, 32)
cam("lobby from the entrance", 0, 380, z, 4, 70)
cam("lobby duty desk", -150, 700, z, 6, -20)

# ---- medblock
s = med
z = FL(0)
P("props_c17/hospital_bed01", s.a[0] + 46, 420, z, 0)
P("props_c17/hospital_bed01", s.a[0] + 46, 540, z, 0)
P("props_c17/hospital_ivstand01", s.a[0] + 100, 470, z, 30)
P("props_c17/hospital_ivstand01", s.a[0] + 100, 590, z, 80)
P("props_c17/furnituresink001a", s.b[0] - 60, s.b[1] - 14, z + 10, 270)
P("props_wasteland/controlroom_storagecloset001a", s.b[0] - 20, 400, z, 180)
P("props_interiors/furniture_desk01a", -340, 600, z, 90)
P("props_c17/chair_office01a", -340, 570, z, 90)
P("props_c17/hospital_cart01", -300, 470, z, 200)
PH("props_lab/jar01a", -330, 604, z + 40, 0)
PH("props_lab/jar01b", -350, 598, z + 40, 0)
M.ent("item_healthcharger", (s.b[0], 520, z + 48), angles="0 180 0", targetname="opora_health_station")
M.ent("info_target", (-320, 590, z), targetname="spot_medic", angles="0 270 0")
fluor((s.a[0] + s.b[0]) / 2, 430, CL(0), "x", 400, "220 235 245")
fluor((s.a[0] + s.b[0]) / 2, 570, CL(0), "x", 300, "220 235 245")
under_windows(s)
cam("medblock", s.b[0] - 30, 620, z, 8, 225)

# ---- armoury (locked)
s = armoury
z = FL(0)
for i in range(4):
    P("props_wasteland/controlroom_storagecloset001a", s.b[0] - 20, 400 + 50 * i, z, 180)
P("props_c17/shelfunit01a", 330, s.a[1] + 12, z, 0)
PD("items/ammocrate_smg1", 300, 560, z, 0)
PD("items/ammocrate_ar2", 360, 560, z, 0)
PD("items/ammocrate_pistol", 300, 600, z, 0)
P("props_wasteland/controlroom_desk001b", 380, 470, z, 90)
PH("props_junk/cardboard_box001a", 320, s.a[1] + 14, z + 52, 0)
fluor(384, 496, CL(0), "x", 360, "210 230 240")
wall_panel("y", 264, 504, YS1, z, z + 120, -1)
M.ent("info_target", (400, 620, z), targetname="spot_armoury_door", angles="0 90 0")
cam("armoury", 290, 620, z, 10, 300)

# ---- corridors (all floors): radiators, benches, notice boards, lights
for c in (corr0W, corr0E, corr1, corr2):
    z = c.a[2]
    x0, x1 = c.a[0], c.b[0]
    n = max(1, int((x1 - x0) // 240))
    for i in range(n):
        xx = x0 + (x1 - x0) * (i + 0.5) / n
        fluor(xx, (YC0 + YC1) / 2, c.b[2], "x", 330 if i % 2 == 0 else 0, light=(i % 2 == 0))
    under_windows(c)
    for i in range(int((x1 - x0) // 400)):
        xx = x0 + 160 + 400 * i
        if not blocked(xx, YC0 + 20, z, 40):
            P("props_trainstation/bench_indoor001a", xx, YC0 + 18, z, 90) if rnd.random() < 0.5 else \
                P("props_lab/corkboard002", xx, YC0, z + 70, 90, on_floor=False, block=False)
    P("props_trainstation/trashcan_indoor001a", x0 + 40, YC1 - 24, z, 0)
    floor_trash(c, 3)
cam("corridor ground floor east", 680, 736, FL(0), 4, 180)
cam("corridor first floor", -680, 736, FL(1), 4, 0)
cam("corridor second floor", 680, 736, FL(2), 4, 180)

# ================================================================ first floor: interrogation rooms
for i, s in enumerate(ints):
    z = FL(1)
    cx, cy = (s.a[0] + s.b[0]) / 2, (s.a[1] + s.b[1]) / 2 - 10
    P("props_c17/furnituretable002a", cx, cy, z, 0 if i % 2 else 90)
    P("props_c17/furniturechair001a", cx - 40, cy, z, 0)
    P("props_wasteland/controlroom_chair001a", cx + 44, cy, z, 180)
    P("props_c17/lamp_bell_on", cx, cy, CL(1) - 40, 0, on_floor=False, block=False)
    spot(cx, cy, CL(1) - 58, 520, "255 224 170", cone=40, inner=20)
    P("props_wasteland/controlroom_filecabinet002a", s.a[0] + 20, s.b[1] - 24, z, 0)
    P("props_combine/combine_smallmonitor001", cx + 10, cy - 6, z + 38, 180 + 20 * (i - 1.5), block=False)
    PH("props_lab/clipboard", cx - 8, cy + 10, z + 38, 30)
    P("props_c17/furniturechair001a", s.a[0] + 24, s.a[1] + 40, z, 45)
    wall_panel("x", s.a[1], s.b[1], s.b[0], z, z + 120, -1)
    fill(cx, cy + 80, CL(1) - 20, 25)
    M.ent("info_target", (cx + 44, cy, z), targetname="spot_interrogation_%d" % (i + 1), angles="0 180 0")
    M.ent("info_target", (cx - 40, cy, z), targetname="spot_interrogation_%d_chair" % (i + 1), angles="0 0 0")
    under_windows(s)
    if i in (0, 2):
        cam("interrogation %d" % (i + 1), s.b[0] - 30, s.b[1] - 20, z, 12, 225)

# ================================================================ second floor
s = barracks
z = FL(2)
for i in range(4):
    xx = s.a[0] + 70 + i * 120
    P("props_forest/bunkbed", xx, s.b[1] - 54, z, 90)
    P("props_forest/footlocker01_closed", xx, s.b[1] - 120, z, 90)
for i in range(3):
    xx = s.a[0] + 130 + i * 120
    P("props_forest/bunkbed", xx, s.a[1] + 54, z, 90)
for i in range(5):
    P("props_c17/lockers001a", s.a[0] + 12, 420 + 40 * i if i < 3 else 420 + 40 * i + 10, z, 0)
P("props_c17/furnituretable002a", -150, 470, z, 0)
P("props_c17/furniturechair001a", -190, 470, z, 0)
P("props_c17/furniturechair001a", -110, 480, z, 190)
P("props_lab/citizenradio", -150, 480, z + 38, 200, block=False)
table_clutter(-150, 462, z + 38, 3, 10)
M.ent("info_target", (s.a[0] + 70, s.b[1] - 110, z), targetname="spot_hero_bunk", angles="0 270 0")
for xx in (-400, -150):
    fluor(xx, 496, CL(2), "y", 360)
under_windows(s)
floor_trash(s, 4)
cam("barracks", -40, 600, z, 8, 200)

s = schulz
z = FL(2)
P("props_combine/breendesk", 70, 500, z, 0)
PD("props_combine/breenchair", 32, 500, z, 0)
P("props_c17/chair_office01a", 130, 470, z, 180)
P("props_c17/chair_office01a", 130, 530, z, 180)
PD("props_combine/breenglobe", 76, 470, z + 31, 30, block=False)
PD("props_combine/breenclock", 76, 532, z + 31, 0, block=False)
P("props_wasteland/controlroom_storagecloset001b", 30, 610, z, 270, targetname="opora_schulz_safe")
P("props_wasteland/controlroom_filecabinet002a", 30, 380, z, 0)
P("props_wasteland/controlroom_filecabinet002a", 60, 380, z, 90)
P("props_c17/furniturecouch001a", s.b[0] - 26, 440, z, 180)
PD("props_combine/breenbust", 30, 380, z + 70, 0, block=False)
M.overlay("DECALS/DECAL_POSTERBREEN", (s.a[0] + 0, 500, z + 72), (1, 0, 0), 64, 64)
M.ent("info_target", (40, 500, z), targetname="spot_schulz_office", angles="0 0 0")
M.ent("info_target", (30, 610, z), targetname="spot_safe", angles="0 270 0")
fluor(128, 496, CL(2), "y", 320, "255 220 170")
fill(60, 500, CL(2) - 20, 30)
under_windows(s)
cam("Schulz's office", s.b[0] - 30, 620, z, 10, 200)

s = archive
z = FL(2)
for i in range(3):
    P("props_wasteland/kitchen_shelf001a", 330 + 70 * i, 440, z, 90)
    P("props_wasteland/kitchen_shelf001a", 330 + 70 * i, 560, z, 90)
for i in range(4):
    P("props_wasteland/controlroom_filecabinet002a", s.b[0] - 20, 380 + 30 * i, z, 180)
for i in range(6):
    PH(rnd.choice(["props_junk/cardboard_box001a", "props_junk/cardboard_box002a", "props_junk/cardboard_box003a"]),
       jit(380, 80), jit(500, 20), z, rnd.uniform(0, 360))
fluor(384, 500, CL(2), "y", 300)
under_windows(s)
cam("archive", 280, 620, z, 10, 300)

# ================================================================ annexes
# ---- cell block
s = cellsCorr
z = FL(0)
P("props_wasteland/controlroom_desk001b", s.b[0] - 40, 900, z, 180)
P("props_wasteland/controlroom_chair001a", s.b[0] - 80, 900, z, 0)
P("props_lab/monitor01a", s.b[0] - 36, 920, z + 34, 180, block=False)
for i in range(4):
    x0 = -712 + 120 * i
    # bars across the front, the cell door
    M.detail((x0, 976, z), (x0 + 104, 980, CL(0)), {"all": NODRAW, "front": BARS, "back": BARS}, group="bars")
    PD("props_wasteland/prison_celldoor001a", x0 + 52, 970, z + 51, 0, on_floor=False,
       targetname="opora_cell%d_door" % (i + 1))
    cs = cells[i]
    P("props_wasteland/prison_bedframe001a", x0 + 52, 1110, z, 0)
    P("props_wasteland/prison_toilet01", x0 + 14, 1000, z, 0)
    P("props_wasteland/prison_sink001a", x0 + 90, 1010, z + 30, 180, block=False)
    P("props_wasteland/prison_cagedlight001a", x0 + 52, 1060, CL(0) - 8, 0, on_floor=False, block=False)
    fill(x0 + 52, 1060, CL(0) - 20, 40, "220 230 220")
    M.ent("info_target", (x0 + 52, 1060, z), targetname="spot_cell_%d" % (i + 1), angles="0 270 0")
for xx in (-600, -360):
    fluor(xx, 912, CL(0), "x", 360, "210 230 240", model="props_wasteland/prison_flourescentlight002b")
cam("cell block", -280, 900, z, 6, 180)

# ---- canteen
s = canteen
z = FL(0)
for i, xx in enumerate((-110, 0, 110)):
    P("props_wasteland/cafeteria_table001a", xx, 940, z, 0)
    P("props_wasteland/cafeteria_bench001a", xx - 34, 940, z, 0)
    P("props_wasteland/cafeteria_bench001a", xx + 34, 940, z, 0)
    table_clutter(xx, 940, z + 32, 2, 18)
M.detail((-184, 1040, z), (130, 1068, z + 40), {"all": "TILE/TILEWALL009B", "top": "METAL/METALWALL021A"}, group="canteen", block=True)
P("props_wasteland/kitchen_stove001a", -150, 1110, z, 270)
P("props_wasteland/kitchen_fridge001a", 150, 1100, z, 180)
P("props_wasteland/kitchen_counter001a", -70, 1104, z, 270)
P("props_wasteland/kitchen_shelf001a", 40, 1120, z, 0)
PH("props_c17/metalpot001a", -150, 1110, z + 47, 0)
PH("props_interiors/pot01a", -70, 1100, z + 40, 0)
for xx in (-60, 20, 90):
    PH("props_junk/garbage_coffeemug001a", jit(xx, 8), 1054, z + 40, rnd.uniform(0, 360))
for xx in (-90, 90):
    fluor(xx, 940, CL(0), "x", 340)
fluor(0, 1090, CL(0), "x", 300)
under_windows(s)
cam("canteen", 160, 870, z, 8, 140)

# ---- gym: shooting range (west) and baton training (east)
s = gym
z = FL(0)
M.detail((264, 960, z), (552, 984, z + 40), {"all": "WOOD/WOODWALL009A", "top": "WOOD/WOODFLOOR005A"}, group="gym", block=True)
for xx in (336, 408, 480):
    M.detail((xx - 2, 984, z), (xx + 2, 1060, z + 72), "WOOD/WOODWALL003A", group="gym", block=True)
for i, xx in enumerate((300, 372, 444, 516)):
    yy = 1200 + (i % 2) * 40
    M.detail((xx - 2, yy - 2, z), (xx + 2, yy + 2, z + 40), "WOOD/WOODBEAM001A", group="gym")
    M.detail((xx - 16, yy - 1, z + 40), (xx + 16, yy + 1, z + 88), "WOOD/WOODWALL003A", group="gym", block=True)
for i in range(6):
    P("props_c17/furnituremattress001a", 280 + 52 * i, s.b[1] - 6, z + 38, 90, on_floor=False, roll=0, pitch=90, block=False)
PD("items/ammocrate_smg1", 300, 930, z, 90)
P("props_wasteland/controlroom_desk001b", 520, 920, z, 0)
M.ent("info_target", (420, 930, z), targetname="spot_gannon_range", angles="0 90 0")
for i, yy in enumerate((1000, 1120, 1240)):
    P("props_silo/tirestack", 660, yy, z, 30 * i)
    P("props_c17/furnituremattress001a", 640, yy, z, 0, block=False)
for i in range(4):
    P("props_c17/metalladder001", s.b[0] - 3, 940 + 40 * i, z, 180, block=False)
P("props_trainstation/bench_indoor001a", 600, 880, z, 90)
M.ent("info_target", (600, 960, z), targetname="spot_instructor", angles="0 0 0")
for xx in (360, 600):
    for yy in (980, 1200):
        P("props_wasteland/prison_flourescentlight002b", xx, yy, 248 - 4, 0, on_floor=False, block=False)
        spot(xx, yy, 236, 460, "200 225 240", cone=65)
cam("gym range", 600, 880, z, 6, 120)
cam("gym from the range", 300, 1300, z, 8, 300)

# ---- garage
s = garage
PD("combine_apc", 1232, 330, 0, 270, targetname="opora_apc", solid="6")
P("props_c17/shelfunit01a", 1100, s.b[1] - 12, 0, 0)
P("props_c17/shelfunit01a", 1190, s.b[1] - 12, 0, 0)
P("props_silo/tirestack", 1410, 540, 0, 0)
P("props_vehicles/apc_tire001", 1400, 470, 0, 0)
for (xx, yy) in ((1060, 140), (1090, 150), (1060, 180)):
    PH("props_c17/oildrum001", xx, yy, 0, rnd.uniform(0, 360))
PH("props_junk/metalgascan", 1130, 560, 0, 10)
PH("props_junk/gascan001a", 1150, 556, 0, 80)
P("props_wasteland/controlroom_desk001b", 1410, 260, 0, 180)
PH("props_c17/tools_wrench01a", 1410, 250, 34, 30)
PH("props_c17/tools_pliers01a", 1406, 280, 34, 70)
for xx in (1130, 1330):
    for yy in (220, 440):
        P("props_wasteland/prison_flourescentlight002b", xx, yy, 224 - 4, 0, on_floor=False, block=False)
        spot(xx, yy, 212, 420, "200 225 240", cone=65)
cam("garage", 1060, 120, 0, 10, 30)

# ================================================================ outside: yard, smoking corner, loyalty post
# loyalty test post (a pole with a ring), trampled dirt
M.detail((-504, -84, 0), (-496, -76, 104), "METAL/METALWALL021A", group="yard")
M.detail((-506, -86, 96), (-494, -74, 100), "METAL/METALWALL021A", group="yard")
M.decal("DECALS/DECALSTAIN002A", (-500, -60, 1))
M.ent("info_target", (-500, -58, 0), targetname="spot_violator_yard", angles="0 270 0")
# smoking corner: a lean-to by the west compound wall
SCX, SCY = -1200, 330
M.detail((-1248, 260, 112), (-1136, 400, 116), {"all": "METAL/METALROOF006A"}, group="yard")
for yy in (264, 392):
    M.detail((-1140, yy - 2, 0), (-1136, yy + 2, 112), "METAL/METALWALL021A", group="yard")
P("props_c17/bench01a", -1226, SCY, 0, 0)
P("props_c17/oildrum001", -1222, 280, 0, 0)
PH("props_junk/wood_crate001a", -1220, 384, 0, 0)
PH("props_c17/metalpot001a", -1220, 384, 40, 30)
PH("props_junk/garbage_coffeemug001a", -1212, 374, 40, 100)
PH("props_junk/garbage_coffeemug001a", -1228, 390, 40, 200)
for i, yy in enumerate((312, 330, 348)):
    PH("barneyhelmet_faceplate", -1226, yy, 21, 90 + 15 * i, pitch=0, roll=180, on_floor=False)
M.decal("DECALS/TRASHDECAL01A", (-1180, 320, 1))
M.ent("info_target", (-1180, 330, 0), targetname="spot_cp_smoker", angles="0 180 0")
# trees in the grass beds, benches, the driveway clutter
for (xx, yy) in ((-1140, -200), (-1150, 40), (-1050, 1300), (-900, 1450), (1200, 1450), (1400, -280)):
    P("props_foliage/tree_deciduous_01a-lod", xx, yy, 2, rnd.uniform(0, 360))
for (xx, yy, yaw) in ((-300, 240, 270), (300, 240, 270)):
    P("props_trainstation/benchoutdoor01a", xx, yy, 0, yaw)
for (xx, yy) in ((1430, 700), (1430, 760), (1380, 730)):
    PH("props_c17/oildrum001", xx, yy, 0, rnd.uniform(0, 360))
P("props_junk/trashdumpster01a", -1180, 1100, 0, 90)
P("props_junk/trashdumpster01a", 900, 1500, 0, 0)
PH("props_junk/wood_pallet001a", 1000, 1520, 0, 20)
PH("props_junk/wood_crate002a", 1060, 1500, 0, 5)
PH("props_junk/wood_crate001a", 1070, 1460, 0, 40)
P("props_c17/playground_jungle_gym01a", -400, 1400, 0, 0)
P("props_c17/playgroundtick-tack-toe_post01", -560, 1420, 0, 90)
P("props_combine/combine_barricade_short01a", -200, -330, 0, 90)
P("props_combine/combine_barricade_short01a", 200, -330, 0, 90)
# a Combine van in the yard
P("props_vehicles/van001a", 760, -140, 0, 180)

# ================================================================ checkpoint (street side)
GZ = 0
PD("props_combine/combine_door01", 320 - 12.5, -380, 96, 0, on_floor=False, DefaultAnim="idle_open",
   targetname="opora_gate_door", solid="6")
P("props_combine/combine_lock01", 440, -386, 70, 270, on_floor=False, block=False)
P("props_combine/combine_fence01a", GATE[0], -368, 40, 90, on_floor=False)
P("props_combine/combine_fence01b", GATE[1], -368, 40, 90, on_floor=False)
field = M.brush_entity("func_brush", [M.box_solid((GATE[0] + 20, -370, 0), (GATE[1] - 20, -366, 192),
                                                  {"all": NODRAW, "front": "EFFECTS/COM_SHIELD003A",
                                                   "back": "EFFECTS/COM_SHIELD003A"})],
                       targetname="opora_gate_field", solidity="2", spawnflags="2", rendermode="0",
                       disablereceiveshadows="1", disableshadows="1", InputFilter="0", invert_exclusion="0")
M.ent("ambient_generic", (0, -368, 96), message="ambient/machines/combine_shield_loop3.wav", health="6", radius="600",
      spawnflags="16", targetname="opora_gate_field_snd", pitch="100", pitchstart="100")
P("props_combine/combine_booth_short01a", 520, -470, 0, 90)
for xx in (-120, 0, 120):
    P("props_c17/concrete_barrier001a", xx, -540, 0, 90)
for i, xx in enumerate((200, 260, 380, 440)):
    P("props_combine/combine_barricade_short02a", xx, -440, 0, 0)
P("props_combine/combine_barricade_tall01a", -260, -430, 0, 0)
P("props_combine/combine_barricade_tall01a", 660, -430, 0, 0)
P("props_combine/combine_light002a", 160 + 24, -390, 150, 270, on_floor=False, block=False)
M.ent("npc_combine_camera", (500, -392, 200), angles="0 270 0", targetname="opora_gate_camera", spawnflags="0",
      innerradius="300", outerradius="450")
M.ent("info_target", (320, -440, 0), targetname="spot_cp_checkpoint", angles="0 270 0")
cam("checkpoint", 420, -760, -8, -6, 100)
cam("checkpoint from the yard", 300, -150, 0, -4, 270)

# ================================================================ street furniture
for xx in range(SX0 + 256, SX1 - 128, 512):
    P("props_c17/lamppost03a_off", xx, -496, 0, 180)
    P("props_c17/lamppost03a_off", xx + 256, -912, 0, 0)
POLES = []
for xx in range(SX0 + 128, SX1 - 64, 640):
    P("props_c17/utilitypole02b", xx, -1004, 0, 0)
    POLES.append((xx, -1000, 440))
for i in range(len(POLES) - 1):
    a, b = POLES[i], POLES[i + 1]
    na, nb = "rope_%d" % i, "rope_%d" % (i + 1)
    for dz in (0, -20):
        M.ent("move_rope", (a[0], a[1], a[2] + dz), targetname=na + ("" if dz == 0 else "b"),
              NextKey=nb + ("" if dz == 0 else "b"), RopeMaterial="cable/cable.vmt", Slack="120", Width="2", Subdiv="2",
              Type="0", MoveSpeed="64", PositionInterpolator="2", TextureScale="1", Collide="0", Dangling="0",
              Breakable="0", Barbed="0", spawnflags="1")
for i in range(len(POLES) - 1, len(POLES)):
    b = POLES[i]
    for dz in (0, -20):
        M.ent("keyframe_rope", (b[0], b[1], b[2] + dz), targetname="rope_%d" % i + ("" if dz == 0 else "b"),
              RopeMaterial="cable/cable.vmt", Slack="120", Width="2", Subdiv="2", Type="0", MoveSpeed="64",
              PositionInterpolator="2", TextureScale="1", Collide="0", Dangling="0", Breakable="0", Barbed="0")
for (xx, yy, yaw) in ((-1800, -990, 270), (-900, -990, 270), (700, -990, 270), (1800, -990, 270)):
    P("props_junk/trashdumpster01a", xx, yy, 0, yaw)
for xx in (-2200, -1400, 1100, 2000):
    P("props_c17/awning001a", xx, -1024 + 15, 150, 180, on_floor=False, block=False)
P("props_trainstation/payphone001a", -1600, -1016, 0, 90)
for i in range(16):
    xx = rnd.uniform(SX0 + 64, SX1 - 64)
    yy = rnd.choice((rnd.uniform(-1010, -910), rnd.uniform(-500, -400)))
    PH(rnd.choice(["props_junk/garbage_newspaper001a", "props_junk/garbage_metalcan001a", "props_junk/garbage_glassbottle003a",
                   "props_junk/garbage_milkcarton002a", "props_junk/garbage_carboard002a"]), xx, yy, 0, rnd.uniform(0, 360))
for i, xx in enumerate((-2300, -1700, -1300, -600, 200, 900, 1500, 2100)):
    M.decal("DECALS/TRASHDECAL0%dA" % (1 + i % 5), (xx, -950 if i % 2 else -450, 1))
    M.decal("DECALS/DECALSTAIN00%dA" % (2 + i % 4) if (2 + i % 4) != 4 else "DECALS/DECALSTAIN005A",
            (xx + 200, -800, -7))
cam("street west", -2300, -640, -8, -4, 0)
cam("street east", 2100, -640, -8, -4, 180)

# ================================================================ roof: helipad, antenna, vents, pigeons
RZ = 392
M.detail((-560, 440, RZ), (-240, 760, RZ + 6), {"all": "CONCRETE/CONCRETEFLOOR039A", "top": "CONCRETE/CONCRETEFLOOR039A"},
         group="roof")
for (a, b) in (((-450, 520, RZ + 6), (-434, 680, RZ + 7)), ((-366, 520, RZ + 6), (-350, 680, RZ + 7)),
               ((-434, 592, RZ + 6), (-366, 608, RZ + 7))):
    M.detail(a, b, "PLASTER/PLASTERWALL016A", group="roof")
for (xx, yy) in ((-560, 440), (-560, 760), (-240, 440), (-240, 760)):
    P("props_c17/light_domelight02_on", xx, yy, RZ + 6, 0, block=False)
P("props_rooftop/antenna01a", 300, 760, RZ, 0)
P("props_rooftop/antennaclusters01a", 120, 700, RZ, 0)
P("props_rooftop/roof_dish001", 420, 760, RZ, 200)
for (xx, yy, mdl_) in ((-120, 400, "props_rooftop/roof_vent004"), (40, 420, "props_rooftop/roof_vent002"),
                       (-640, 700, "props_rooftop/roof_vent001"), (220, 380, "props_rooftop/chimneypipe_cluster02a")):
    P(mdl_, xx, yy, RZ, 0)
PH("props_junk/metalbucket01a", -160, 700, RZ, 0)
for i in range(3):
    M.ent("npc_pigeon", (jit(-120, 30), jit(720, 30), RZ + 8), angles="0 %d 0" % rnd.randint(0, 359),
          targetname="opora_pigeon_%d" % i, spawnflags="0")
M.ent("info_target", (-140, 700, RZ), targetname="spot_pigeon_feeder", angles="0 90 0")
M.ent("info_target", (300, 720, RZ), targetname="spot_antenna", angles="0 90 0")
cam("roof helipad", -700, 360, RZ, 10, 30)
cam("roof antenna", 0, 600, RZ, -4, 30)
cam("yard from the gate", 0, -320, 0, -10, 90)
cam("smoking corner", -1100, 200, 0, 12, 140)
cam("yard loyalty post", -380, -200, 0, 4, 140)
cam("back yard", 500, 1500, 0, -6, 220)

# ================================================================ stairwell lights, head-house
for sp, lv in ((stairW, 2), (stairE, 3)):
    xc = (sp.a[0] + sp.b[0]) / 2
    for k in range(lv + 1):
        if FL(k) + 120 > sp.b[2]:
            break
        P("props_c17/light_domelight02_on", xc - 48, 586, FL(k) + 120 - 8 - 0, 0, on_floor=False, block=False) \
            if k < lv else None
        fill(xc, 586, FL(k) + 100, 60, "254 216 146")
    spot(xc, 400, sp.b[2] - 12, 300, "254 216 146", cone=70)

# ================================================================ transition to the plaza, spawn, zones
M.ent("info_landmark", (-2720, -640, 24), targetname="lm_02_08")
# the Old Town through the east arch (rp_c17_04_oldtown: landmark (-2240, 224, 24) in its west arch)
M.ent("info_landmark", (2496, -640, 24), targetname="lm_08_04")
M.brush_entity("trigger_changelevel", [M.box_solid((2560, PASS_Y[0], -8), (2624, PASS_Y[1], 216), TRIGGER)],
               map="rp_c17_04_oldtown", landmark="lm_08_04", spawnflags="0", targetname="opora_to_oldtown")
M.brush_entity("trigger_transition", [M.box_solid((2368, PASS_Y[0] - 64, -8), (2624, PASS_Y[1] + 64, 216), TRIGGER)],
               targetname="lm_08_04")
M.brush_entity("trigger_changelevel", [M.box_solid((-2816, PASS_Y[0], -8), (-2752, PASS_Y[1], 216), TRIGGER)],
               map="rp_c17_02_plaza", landmark="lm_02_08", spawnflags="0", targetname="opora_to_plaza")


def zone(a, b, kind, name):
    M.brush_entity("trigger_rpg_zone", [M.box_solid(a, b, TRIGGER)], zone=str(kind), targetname=name, spawnflags="1",
                   StartDisabled="0")


zone((CX0, CY0 - 640, -16), (CX1, CY1, 640), 1, "opora_zone_precinct")           # street in front + compound
zone((armoury.a[0], armoury.a[1], armoury.a[2]), (armoury.b[0], armoury.b[1], armoury.b[2]), 2, "opora_zone_armoury")
zone((schulz.a[0], schulz.a[1], schulz.a[2]), (schulz.b[0], schulz.b[1], schulz.b[2]), 2, "opora_zone_office")
zone((-712, 848, FL(0)), (-248, 1136, CL(0)), 2, "opora_zone_cells")
zone((garage.a[0], garage.a[1], 0), (garage.b[0], garage.b[1], 224), 2, "opora_zone_garage")

# ================================================================ cubemaps, soundscapes
SOUND = {"corridor": "general.concrete_quiet", "lobby": "d1_trainstation.Interrogation", "stair": "general.concrete_quiet",
         "class": "general.concrete_quiet", "interrogation": "d1_trainstation.Interrogation", "med": "general.concrete_quiet",
         "armoury": "general.concrete_quiet", "office": "general.concrete_quiet", "archive": "general.concrete_quiet",
         "cells": "d1_trainstation.Interrogation", "cell": None, "canteen": "general.concrete_quiet",
         "gym": "d1_trainstation.Turnstyle", "garage": "general.concrete_quiet"}
for s in M.spaces:
    if s.kind != "in":
        continue
    c = s.center
    if s.name.startswith("stair"):
        for k in range(3 if s is stairE else 2):
            M.ent("env_cubemap", (c[0], 590, FL(k) + 64), cubemapsize="0")
        continue
    if s.name.startswith("cell") and s.name != "cellblock":
        M.ent("env_cubemap", (c[0], c[1], s.a[2] + 56), cubemapsize="0")
        continue
    M.ent("env_cubemap", (c[0], c[1], s.a[2] + 64), cubemapsize="0")
    if (s.b[0] - s.a[0]) > 700:
        for fx in (0.2, 0.8):
            M.ent("env_cubemap", (s.a[0] + (s.b[0] - s.a[0]) * fx, c[1], s.a[2] + 64), cubemapsize="0")
preset_of = {sp.name: pr for sp, pr, k in ROOMS}
preset_of.update({"stairW": "stair", "stairE": "stair"})
for s in M.spaces:
    if s.kind != "in":
        continue
    snd = SOUND.get(preset_of.get(s.name, ""), None)
    if not snd:
        continue
    c = s.center
    r = int(min(256, max(96, min(s.b[0] - s.a[0], s.b[1] - s.a[1]) * 0.5)))
    keys = dict(soundscape=snd, radius=str(r), targetname="ss_" + s.name)
    if snd == "d1_trainstation.Interrogation":
        keys["position0"] = "ss_pos_" + s.name
        M.ent("info_target", (c[0], c[1], s.a[2] + 60), targetname="ss_pos_" + s.name)
    M.ent("env_soundscape", (c[0], c[1], s.a[2] + 64), **keys)
for (pos, snd, r) in (((0, -700, 64), "d1_trainstation.TerminalSquare", 512), ((-1800, -700, 64), "d1_trainstation.TerminalSquare", 512),
                      ((1600, -700, 64), "d1_trainstation.TerminalSquare", 512), ((0, -100, 64), "d1_trainstation.QuietCourtyard", 400),
                      ((-1000, 300, 64), "d1_trainstation.QuietCourtyard", 300), ((1100, -150, 64), "d1_trainstation.QuietCourtyard", 300),
                      ((0, 1450, 64), "d1_trainstation.QuietCourtyard", 400), ((0, 600, 456), "d1_trainstation.QuietCourtyard", 512),
                      ((-2720, -640, 64), "d1_trainstation.TerminalSquare", 200)):
    M.ent("env_soundscape", pos, soundscape=snd, radius=str(r))
for pos in ((0, -700, 64), (-1800, -700, 64), (1600, -700, 64), (0, -100, 64), (-1000, 300, 64), (1100, -150, 64),
            (0, 1450, 64), (0, 600, 456), (-500, 0, 64), (600, 200, 64)):
    M.ent("env_cubemap", pos, cubemapsize="0")

# ================================================================ NPC navigation: info_node grid
NODES = []


def node_ok(x, y, z):
    for (x0, y0, x1, y1, z0, z1) in M.obstacles:
        if x0 - 20 < x < x1 + 20 and y0 - 20 < y < y1 + 20 and z0 < z + 72 and z1 > z + 4:
            return False
    return True


def add_node(x, y, z):
    if node_ok(x, y, z):
        NODES.append((x, y, z))


for s in M.spaces:
    if s.kind != "in" or s.name.startswith("stair"):
        continue
    x0, y0, x1, y1 = s.a[0] + 24, s.a[1] + 24, s.b[0] - 24, s.b[1] - 24
    step = 96
    nx = max(1, int((x1 - x0) // step) + 1)
    ny = max(1, int((y1 - y0) // step) + 1)
    for i in range(nx):
        for j in range(ny):
            xx = x0 + (x1 - x0) * (i / max(1, nx - 1) if nx > 1 else 0.5)
            yy = y0 + (y1 - y0) * (j / max(1, ny - 1) if ny > 1 else 0.5)
            add_node(xx, yy, s.a[2])
for sp, lv in ((stairW, 2), (stairE, 3)):
    x0, x1 = sp.a[0], sp.b[0]
    xm = (x0 + x1) / 2
    for k in range(lv + 1):
        add_node((x0 + xm) / 2, 586, FL(k))
        add_node((xm + x1) / 2, 586, FL(k))
        if k < lv:
            add_node((x0 + x1) / 2, 400, FL(k) + 64)
            add_node((x0 + xm - 8) / 2, 490, FL(k) + 32)
            add_node((xm + 8 + x1) / 2, 490, FL(k) + 96)
for d in DOORS:
    w = (d["w0"] + d["w1"]) / 2.0
    t = (d["t0"] + d["t1"]) / 2.0
    pos = (w, t) if d["axis"] == "y" else (t, w)
    NODES.append((pos[0], pos[1], d["z0"]))
for s in M.spaces:
    if s.kind == "open" and s.name == "passage":
        c = s.center
        NODES.append((c[0], c[1], s.a[2]))
# outside: yard, back yard, street, roof
OUT_AREAS = [((CX0 + W + 32, CY0 + W + 32, CX1 - W - 32, CY1 - W - 32), 0, 128),
             ((SX0 + 64, -1000, SX1 - 64, -400), 0, 128)]
for (x0, y0, x1, y1), zz, step in OUT_AREAS:
    for xx in range(int(x0), int(x1), step):
        for yy in range(int(y0), int(y1), step):
            if any(f[0] - 24 < xx < f[2] + 24 and f[1] - 24 < yy < f[3] + 24 for f in FOOT):
                continue
            z_ = -8 if -896 < yy < -512 else zz
            add_node(xx, yy, z_)
for xx in range(-700, 480, 128):
    for yy in (380, 520, 700, 800):
        add_node(xx, yy, RZ)
for i, (x, y, z) in enumerate(NODES):
    M.ent("info_node", (round(x), round(y), z + 8), nodeid=str(i + 1))
print("info_node:", len(NODES))

# ================================================================ ambient Civil Protection
CP = dict(additionalequipment="weapon_stunstick", manhacks="0", squadname="opora_cp", rpg_faction="cp",
          spawnflags="0", sleepstate="0", physdamagescale="1.0", model="models/police.mdl")


def patrol(name, pts, z):
    for i, (x, y) in enumerate(pts):
        nxt = "%s_p%d" % (name, (i + 1) % len(pts))
        M.ent("path_corner", (x, y, z + 4), targetname="%s_p%d" % (name, i), target=nxt, wait=str(rnd.choice((2, 4, 6))),
              speed="0", spawnflags="0")
    return "%s_p0" % name


M.ent("npc_metropolice", (300, -470, 0), angles="0 270 0", targetname="npc_cp_gate", **CP)
M.ent("npc_metropolice", (-700, -230, 0), angles="0 0 0", targetname="npc_cp_yard1",
      target=patrol("cp_yard1", [(-700, -230), (700, -230), (700, 150), (-700, 150)], 0), **CP)
M.ent("npc_metropolice", (900, 900, 0), angles="0 90 0", targetname="npc_cp_backyard",
      target=patrol("cp_backyard", [(900, 900), (900, 1480), (-1100, 1480), (-1100, 1250)], 0), **CP)
M.ent("npc_metropolice", (-600, 736, FL(0)), angles="0 0 0", targetname="npc_cp_hall",
      target=patrol("cp_hall", [(-600, 736), (-200, 760), (200, 760), (650, 736), (200, 500), (-200, 500)], FL(0)), **CP)
M.ent("npc_metropolice", (-650, 736, FL(1)), angles="0 0 0", targetname="npc_cp_floor1",
      target=patrol("cp_floor1", [(-650, 736), (650, 736)], FL(1)), **CP)
M.ent("npc_metropolice", (-1600, -460, 0), angles="0 0 0", targetname="npc_cp_street",
      target=patrol("cp_street", [(-1600, -460), (1300, -460), (1300, -960), (-1600, -960)], 0), **CP)
M.ent("ai_relationship", (0, -600, 64), targetname="opora_cp_neutral", subject="npc_metropolice", target="player",
      disposition="4", rank="99", reciprocal="0", StartActive="1")

# ================================================================ environment, save
M.ent("light_environment", (0, -600, 1200), angles="0 165 0", pitch="-35", _light="237 218 143 800",
      _ambient="190 201 220 100", _lightHDR="-1 -1 -1 1", _lightscaleHDR="0.7", _ambientHDR="-1 -1 -1 1",
      _AmbientScaleHDR="1", SunSpreadAngle="0")
M.ent("shadow_control", (0, -600, 1180), angles="80 165 0", color="128 128 128", distance="75", disableallshadows="0")
M.ent("env_fog_controller", (0, -600, 1160), targetname="fog_opora", fogenable="1", fogblend="0",
      fogcolor="223 216 173", fogcolor2="106 108 115", fogstart="512", fogend="14000", fogmaxdensity="0.8",
      fogdir="1 0 0", angles="0 0 0", use_angles="0", farz="-1", spawnflags="1", foglerptime="0")
M.ent("env_tonemap_controller", (0, -600, 1140), targetname="tonemap_opora")
M.ent("info_player_start", (-640, -640, 0), angles="0 20 0")

M.m.world.set("comment", "rp_c17_08_opora: C17-08 Civil Protection precinct 'Opora' (former school No. 3). "
              "Generated by hl2rpm_extras/tools/make_opora.py - edit the generator, not this file.")
M.m.world.set("detailmaterial", "detail/detailsprites")
M.m.world.set("detailvbsp", "detail.vbsp")
M.finish()
# RPG layer (GO-01 scene 2, the terminal, the world clock): phaseb_rp_c17_08.py
import phaseb_rp_c17_08  # noqa: E402
_phaseb_log = []
phaseb_rp_c17_08.build_phase_b(M.m, _phaseb_log)
for _line in _phaseb_log:
    print('phase B:', _line)
M.m.save(OUT, backup=os.path.exists(OUT))
print("wrote", OUT)
print("counts:", M.counts, "spaces:", len(M.spaces), "faces recorded:", len(M.faces))
if MISSING:
    print("MISSING (%d):" % len(MISSING))
    for x in sorted(MISSING):
        print("   ", x)
if STEPS_OUT:
    with open(STEPS_OUT, 'w') as f:
        f.write("\n".join(STEPS) + "\n")
    print('steps:', STEPS_OUT, len(STEPS))
