"""rp_c17_04_oldtown: sector C17-04, the Old Town - Klyuchevaya street, the embankment, "Shlyuz-3".

    python make_oldtown.py [out.vmf]

Layout (x east, y north, z up; 1 storey = 128):

    y  912 -------- hull ----------------------------------------------------------------
           north row of houses (y 512..896): Lukich's watch workshop (x -896..-640, shop + cellar
           safehouse "Chasovshchik"), Kadar's bathhouse (x 256..768, lobby + steam room), the rest
           closed city blocks; the "Tolkuchka" flea market on the sidewalk in the east
    y  512 ---- north sidewalk (y 384..512) / Klyuchevaya street, cobbles (y 64..384) / embankment (y -64..64)
    y  -64 ---- stone parapet; the canal below (water z -120, bed z -256); the bridge x 512..704
           with the Civil Protection post (LM-01: the raid)
    y-1024 ---- far quay (y -1280..-1024): the lock keeper's booth "Shlyuz-3" (x 1296..1488), boats
    y-1280 ---- south row of houses (facades to the canal)
    west end: arch passage -> trigger_changelevel rp_c17_08_opora (lm_08_04)

Quests on this map: LM-01 "Sign of the Lambda" (Lukich, Leila, the raid post Victor-5, Ernesto),
SM-01 "The Bathhouse" scenes 1-2 (Mateo at the door, Kadar in the steam room, Irma "Silence").
The architecture kit is opora_kit.py (as for make_opora.py). Edit this generator, not the VMF.
"""
import os
import random
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from opora_kit import Map, MISSING, NODRAW, SKY, PCLIP, TRIGGER  # noqa: E402

OUT = sys.argv[1] if len(sys.argv) > 1 else r"E:\XBLAH's modding tool\content\Mapbase\hl2rpm\mapsrc\rp_c17_04_oldtown.vmf"
M = Map(OUT)
rnd = random.Random(404)

# ---------------------------------------------------------------- materials
COBBLE = "STONE/STONEFLOOR006A"
SIDEWALK = "CONCRETE/CONCRETEFLOOR028C"
QUAY = "STONE/STONEFLOOR006B"
CANAL_WALL = "STONE/STONEWALL032A"
CANAL_BED = "CONCRETE/CONCRETEFLOOR011A"
WATER = "NATURE/WATER_CANALS03"
PARAPET = "STONE/STONEWALL013A"
ROOF = "PROPS/RUBBERROOF002A"
FRAME = "WOOD/WOODBEAM001A"
REVEAL = "PLASTER/PLASTERWALL034A"
GLASS = "GLASS/GLASSWINDOW007A"
STEP_TOP = "STONE/STONESTAIR001A"
STEP_SIDE = "CONCRETE/CONCRETEWALL004A"
BRICK = "BRICK/BRICKWALL017A"
TILE_W = "TILE/TILEWALL009B"
TILE_F = "TILE/TILEFLOOR012A"
SHOP = dict(floor="WOOD/WOODFLOOR005A", wall="PLASTER/PLASTERWALL030C", ceil="PLASTER/PLASTERCEILING008A")
CELLAR = dict(floor="CONCRETE/CONCRETEFLOOR011A", wall="BRICK/BRICKWALL031A", ceil="CONCRETE/CONCRETEWALL004A")
LOBBY = dict(floor=TILE_F, wall="PLASTER/PLASTERWALL034C", ceil="PLASTER/PLASTERCEILING008A")
STEAM = dict(floor=TILE_F, wall=TILE_W, ceil="PLASTER/PLASTERWALL003A")
BOOTH = dict(floor="WOOD/WOODFLOOR007A", wall="WOOD/WOODWALL009A", ceil="WOOD/WOODWALL003A")

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

SX0, SX1 = -2048, 2048          # the street
WX0 = -2368                     # west arch block
EX1 = 2368                      # east closing block
PASS_Y = (96, 352)              # the arch passage (centre 224) - mirrors Opora's east arch (y -768..-512)
CY0, CY1 = -1024, -64           # the canal
BRIDGE = (512, 704)
LM_POS = (-2240, 224, 24)       # lm_08_04 (Opora: (2496, -640, 24))
STEPS = []


def P(model, x, y, z, yaw=0, **kw):
    return M.prop("models/" + model + ".mdl", x, y, z, yaw, **kw)


def PD(model, x, y, z, yaw=0, **kw):
    return M.prop("models/" + model + ".mdl", x, y, z, yaw, cls="prop_dynamic", **kw)


def PH(model, x, y, z, yaw=0, **kw):
    """models with propdata (breakable / physics): prop_physics, frozen with motion disabled if asked"""
    return M.prop("models/" + model + ".mdl", x, y, z, yaw, cls="prop_physics", **kw)


def light(x, y, z, bright=80, color="255 214 160", name=None):
    kv = dict(_light="%s %d" % (color, bright), _lightHDR="-1 -1 -1 1", _lightscaleHDR="1",
              _quadratic_attn="1", _linear_attn="0", _constant_attn="0", style="0")
    if name:
        kv["targetname"] = name
    return M.ent("light", (x, y, z), **kv)


def spot(x, y, z, bright=420, color="254 216 146", cone=60, inner=30, pitch=-90, yaw=0):
    return M.ent("light_spot", (x, y, z), _light="%s %d" % (color, bright), _lightHDR="-1 -1 -1 1",
                 _lightscaleHDR="1", _inner_cone=str(inner), _cone=str(cone), _exponent="1",
                 _quadratic_attn="1", _linear_attn="0", _constant_attn="0", angles="%d %d 0" % (pitch, yaw),
                 pitch=str(pitch), style="0", _distance="0")


def cam(name, x, y, z, pitch, yaw):
    STEPS.append("shot|setpos %d %d %d; setang %d %d 0  // %s" % (x, y, z, pitch, yaw, name))


# ================================================================ ground: street, embankment, canal, quay
M.world((SX0, CY1, -256), (SX1, 512, -32), {"all": NODRAW, "front": CANAL_WALL})                     # under the street
M.world((SX0, CY1, -32), (SX1, 64, 0), {"all": NODRAW, "top": SIDEWALK, "back": SIDEWALK, "front": CANAL_WALL})
M.world((SX0, 64, -32), (SX1, 384, -8), {"all": NODRAW, "top": COBBLE})
M.world((SX0, 384, -32), (SX1, 512, 0), {"all": NODRAW, "top": SIDEWALK, "front": SIDEWALK})
M.world((SX0, CY0, -288), (SX1, CY1, -256), {"all": NODRAW, "top": CANAL_BED})                          # canal bed
M.world((SX0, CY0, -256), (SX1, CY1, -120), {"all": NODRAW, "top": WATER})                              # water
M.world((SX0, -1600, -256), (SX1, CY0, -32), {"all": NODRAW, "back": CANAL_WALL})                       # under the quay
M.world((SX0, -1280, -32), (SX1, CY0, 0), {"all": NODRAW, "top": QUAY, "back": CANAL_WALL})
# canal ends: the water runs under the houses at both ends (dark arches)
for x0, x1, face in ((SX0 - 32, SX0, "right"), (SX1, SX1 + 32, "left")):
    M.world((x0, CY0, -288), (x1, CY1, 0), {"all": NODRAW, face: BRICK})
# parapets (gap for the bridge)
for (a0, a1) in ((SX0, BRIDGE[0]), (BRIDGE[1], SX1)):
    M.detail((a0, CY1 - 8, 0), (a1, CY1, 36), PARAPET, group="parapet")
    M.detail((a0, CY1 - 10, 36), (a1, CY1 + 2, 40), PARAPET, group="parapet")
    M.detail((a0, CY0, 0), (a1, CY0 + 8, 36), PARAPET, group="parapet")
    M.detail((a0, CY0 - 2, 36), (a1, CY0 + 10, 40), PARAPET, group="parapet")

# ---- the bridge: deck, railings, a pier
M.world((BRIDGE[0], CY0, -40), (BRIDGE[1], CY1, -8), {"all": "CONCRETE/CONCRETEWALL004A", "top": COBBLE})
for x0, x1 in ((BRIDGE[0], BRIDGE[0] + 8), (BRIDGE[1] - 8, BRIDGE[1])):
    M.detail((x0, CY0, -8), (x1, CY1, 32), PARAPET, group="bridge")
    M.detail((x0 - 2, CY0, 32), (x1 + 2, CY1, 38), PARAPET, group="bridge")
M.detail((BRIDGE[0] + 48, -580, -288), (BRIDGE[1] - 48, -508, -40), CANAL_WALL, group="bridge")
cam("bridge from the street", 400, 200, 0, 4, -60)


# ================================================================ city blocks (closed)
def block(x0, y0, x1, y1, h, sides, tpl, bay=128):
    """a closed city block: facades on 'sides' (front -y, back +y, left -x, right +x); the first side gets
    the ground storey in bays, every third an alternate (doors, shops)"""
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
        band = "BUILDING_TEMPLATE/BUILDING_TEMPLATE002K"
        if s_ == "back":
            M.detail((x0, y1, h - 8), (x1, y1 + 8, h), band, group="city")
        elif s_ == "front":
            M.detail((x0, y0 - 8, h - 8), (x1, y0, h), band, group="city")
        elif s_ == "right":
            M.detail((x1, y0, h - 8), (x1 + 8, y1, h), band, group="city")
        else:
            M.detail((x0 - 8, y0, h - 8), (x0, y1, h), band, group="city")


# north row: closed blocks around the two open houses
NORTH = [(SX0, -1536, 576, "012"), (-1536, -1152, 512, "003"), (-1152, -896, 640, "002"),
         (-640, -256, 576, "021"), (-256, 256, 512, "019"), (768, 1280, 640, "012"),
         (1280, 1664, 512, "003"), (1664, SX1, 576, "002")]
for (x0, x1, h, t) in NORTH:
    block(x0, 512, x1, 896, h, ["front"], t)
# south row over the far quay
SOUTH = [(SX0, -1408, 512, "021"), (-1408, -768, 576, "002"), (-768, -128, 512, "012"), (-128, 512, 640, "019"),
         (512, 1152, 512, "003"), (1152, 1296, 448, "021"), (1488, SX1, 576, "012")]
for (x0, x1, h, t) in SOUTH:
    block(x0, -1600, x1, -1280, h, ["back"], t)
block(1296, -1600, 1488, -1280, 448, ["back"], "021")
# east end
M.world((SX1, -1600, -32), (EX1, 896, 640), {"left": TPL["002"][2], "top": ROOF, "all": NODRAW})
# west end: the block with the arch passage to Opora
M.world((WX0, -1600, -32), (SX0, PASS_Y[0], 640), {"right": TPL["012"][2], "top": ROOF, "all": NODRAW})
M.world((WX0, PASS_Y[1], -32), (SX0, 896, 640), {"right": TPL["012"][2], "top": ROOF, "all": NODRAW})
M.world((WX0, PASS_Y[0], 224), (SX0, PASS_Y[1], 640), {"right": TPL["012"][2], "bottom": "CONCRETE/CONCRETEWALL004A",
                                                       "top": ROOF, "all": NODRAW})
M.world((WX0, PASS_Y[0], -48), (SX0, PASS_Y[1], -8), {"all": NODRAW, "top": COBBLE})
M.detail((WX0 + 1, PASS_Y[0], -8), (WX0 + 2, PASS_Y[1], 224), {"all": NODRAW, "right": "EFFECTS/FOG_D1_TRAINSTATION_02"},
         group="fogcard")

# ================================================================ Lukich's watch workshop (x -896..-640)
LX0, LX1 = -896, -640
M.envelope("lukich", (LX0, 512, -160), (LX1, 896, 392),
           lambda z0, z1, f, lo, hi: (TPL["003"][0], 128) if z1 <= 128.5 else (TPL["003"][2], 512),
           zlines=(0, 128, 256))
shop = M.space("lukich_shop", (LX0 + 16, 528, 8), (LX1 - 16, 720, 136), "in", **SHOP)
stair = M.space("lukich_stair", (-800, 736, -136), (-744, 880, 136), "in", **CELLAR)
cellar = M.space("lukich_cellar", (LX0 + 16, 528, -136), (LX1 - 16, 880, -8), "in", **CELLAR)
M.opening((-848, 512, 8), (-792, 528, 120), FRAME, "door")                      # street door
M.opening((-760, 512, 32), (-672, 528, 104), REVEAL, "window")                  # shop window
M.opening((-800, 720, 8), (-744, 736, 120), FRAME, "door")                      # shop -> stairs
# glass in the shop window, a sign over the door
M.detail((-760, 518, 32), (-672, 520, 104), GLASS, group="glass")

# ================================================================ Kadar's bathhouse (x 256..768)
BX0, BX1 = 256, 768
M.envelope("bathhouse", (BX0, 512, -16), (BX1, 896, 264),
           lambda z0, z1, f, lo, hi: (TPL["019"][0], 128) if z1 <= 128.5 else (TPL["019"][3], 256), zlines=(0, 128))
lobby = M.space("bath_lobby", (BX0 + 16, 528, 8), (BX1 - 16, 688, 152), "in", **LOBBY)
steam = M.space("bath_steam", (BX0 + 16, 704, 8), (BX1 - 16, 880, 168), "in", **STEAM)
M.opening((488, 512, 8), (544, 528, 120), FRAME, "door")                        # street door
M.opening((488, 688, 8), (544, 704, 120), FRAME, "door")                        # lobby -> steam room
for wx in (320, 640):
    M.opening((wx, 512, 40), (wx + 64, 528, 104), REVEAL, "window")
    M.detail((wx, 518, 40), (wx + 64, 520, 104), GLASS, group="glass")

# ================================================================ the lock keeper's booth "Shlyuz-3" (far quay)
KX0, KX1 = 1296, 1488
M.envelope("booth", (KX0, -1264, -16), (KX1, -1104, 152),
           lambda z0, z1, f, lo, hi: ("WOOD/WOODWALL009A", None), top="METAL/METALROOF005A", zlines=(0,))
booth = M.space("booth", (KX0 + 12, -1252, 8), (KX1 - 12, -1116, 136), "in", **BOOTH)
M.opening((1368, -1116, 8), (1424, -1104, 120), FRAME, "door")
M.opening((1312, -1116, 48), (1352, -1104, 104), REVEAL, "window")
M.detail((1312, -1110, 48), (1352, -1109, 104), GLASS, group="glass")

# ---- hull (sky)
HX0, HY0, HZ0, HX1, HY1, HZ1 = WX0 - 16, -1616, -320, EX1 + 16, 912, 1200
M.world((HX0, HY0, HZ0), (HX1, HY1, HZ0 + 16), NODRAW)
M.world((HX0, HY0, HZ1), (HX1, HY1, HZ1 + 16), SKY)
M.world((HX0, HY0, HZ0 + 16), (HX0 + 16, HY1, HZ1), SKY)
M.world((HX1 - 16, HY0, HZ0 + 16), (HX1, HY1, HZ1), SKY)
M.world((HX0 + 16, HY0, HZ0 + 16), (HX1 - 16, HY0 + 16, HZ1), SKY)
M.world((HX0 + 16, HY1 - 16, HZ0 + 16), (HX1 - 16, HY1, HZ1), SKY)

M.build_envelopes()
print("envelopes:", ", ".join("%s %d" % (e.name, e.brushes) for e in M.envelopes))


# ================================================================ details inside
def dbox(a, b, mats, group="furniture", block=True):
    M.detail(a, b, mats, group=group, block=block)


def door_prop(x, y, z, yaw, name=None, locked=False):
    keys = dict(model="models/props_c17/door01_left.mdl", angles="0 %d 0" % yaw, skin="0", distance="90",
                speed="100", spawnpos="0", opendir="0", hardware="1", returndelay="-1",
                spawnflags=str(8192 | (2048 if locked else 0)), forceclosed="0", health="0",
                ajarangles="0 0 0", fademindist="-1")
    if name:
        keys["targetname"] = name
    return M.ent("prop_door_rotating", (x, y, z + 54), **keys)


# ---- Lukich: counter, clocks, workbench; the stairs down; the cellar safehouse
z = 8
# the counter leaves a passage on the east side: behind it - the door to the cellar stairs
dbox((-880, 612, z), (-728, 636, z + 40), {"all": "WOOD/WOODWALL009A", "top": "WOOD/WOODFLOOR005A"})
for i, (xx, zz) in enumerate(((-860, 96), (-820, 108), (-780, 92), (-720, 104), (-680, 96))):
    P("props_c17/clock01", xx, 716, zz, 270, on_floor=False, block=False)
P("props_c17/FurnitureTable002a", -720, 680, z, 0)
P("props_lab/desklamp01", -740, 680, z + 30, 200, block=False)
P("props_c17/chair02a", -770, 690, z, 300)
PH("props_lab/clipboard", -700, 676, z + 30, 40, block=False)
P("props_c17/FurnitureShelf001a", -872, 560, z, 0)
light(-768, 600, 112, 60, "255 210 150", name="lukich_lamp")
door_prop(-848 + 4, 520, 8, 270, name="lukich_door")
# stairs: 9 steps of 16 x 16 down the lane x -800..-744, from the shop level (8) to the cellar (-136)
for i in range(9):
    top = 8 - 16 * (i + 1)
    ya, yb = 736 + 16 * i, 736 + 16 * (i + 1)
    dbox((-800, ya, -136), (-744, yb, top), {"top": STEP_TOP, "all": STEP_SIDE}, group="stairs")
M.detail_solid(M.ramp_solid("y", -800, -744, 736, 8, 880, -136, 16, PCLIP), "stairclip")
# cellar: the Lambda safehouse: table, radio, maps, a cot, crates of paper
zc = -136
P("props_c17/FurnitureTable002a", -770, 620, zc, 0)
P("props_lab/citizenradio", -760, 620, zc + 30, 160, block=False)
P("props_c17/chair02a", -730, 590, zc, 150)
P("props_c17/chair02a", -800, 650, zc, 330)
P("props_c17/furniturebed001a", -840, 820, zc, 90)
P("props_junk/cardboard_box001a", -690, 840, zc, 15)
P("props_junk/cardboard_box002a", -690, 800, zc, 70)
P("props_lab/corkboard001", -660, 640, zc + 64, 180, on_floor=False, block=False)
light(-768, 640, -30, 45, "255 190 130", name="cellar_lamp")
cam("Lukich's shop", -700, 560, 8, 8, 160)
cam("the cellar", -700, 560, -136, 6, 150)

# ---- bathhouse: lobby (counter, benches), steam room (tiers, steam)
z = 8
dbox((300, 600, z), (420, 624, z + 40), {"all": "WOOD/WOODWALL009A", "top": "WOOD/WOODFLOOR005A"})
P("props_trainstation/bench_indoor001a", 640, 560, z, 0)
P("props_c17/lockers001a", 740, 610, z, 180)
P("props_c17/lockers001a", 740, 650, z, 180)
door_prop(488 + 4, 520, 8, 270, name="bath_door")
for i, (y0, h) in enumerate(((844, 48), (812, 32), (780, 16))):
    dbox((288, y0, z), (736, y0 + 32 if i == 0 else y0 + 32, z + h), {"all": "WOOD/WOODWALL003A", "top": "WOOD/WOODFLOOR005A"},
         group="steam_benches")
# two thin wisps over the stove corners (dense steam turns the deferred-lit room into a white-out)
for xx in (300, 724):
    M.ent("env_steam", (xx, 860, 12), angles="-90 0 0", spawnflags="1", InitialState="1", type="0",
          SpreadSpeed="6", Speed="14", StartSize="10", EndSize="36", Rate="4", rendercolor="190 190 190",
          JetLength="70", renderamt="28", rollspeed="6")
light(512, 600, 136, 70, "255 220 180")
light(512, 790, 150, 55, "255 200 160")
cam("bathhouse lobby", 700, 560, 8, 6, 150)
cam("steam room", 700, 730, 8, 6, 150)

# ---- booth: desk, stove, nets; boats on chains
z = 8
P("props_c17/FurnitureTable002a", 1392, -1220, z, 0)
P("props_c17/chair02a", 1420, -1190, z, 200)
P("props_c17/furniturestove001a", 1460, -1150, z, 180)
light(1392, -1180, 120, 45, "255 200 140")
door_prop(1368 + 4, -1110, 8, 270, name="booth_door")
for i, (xx, yy, yaw) in enumerate(((1180, -960, 10), (1560, -980, 170), (1700, -940, 190))):
    PH("props_canal/boat001a", xx, yy, -126, yaw, on_floor=False, block=False, spawnflags="8")
cam("Shlyuz-3", 1250, -900, 0, 6, -30)

# ================================================================ street furniture, lamps, market
for i, x in enumerate(range(SX0 + 256, SX1 - 128, 512)):
    P("props_c17/lamppost03a_off", x, 440, 0, 270)
    spot(x, 418, 230, 360, "255 205 150", cone=70)
    if i % 2 == 0:
        P("props_c17/bench01a", x + 160, 30, 0, 90)
for i in range(6):
    x = 1240 + 110 * i
    P("props_c17/FurnitureTable002a", x, 470, 0, 0)
    P(("props_junk/cardboard_box002a", "props_junk/garbage_metalcan001a", "props_c17/clock01",
       "props_junk/garbage_glassbottle001a", "props_junk/garbage_newspaper001a", "props_junk/cardboard_box004a")[i],
      x, 470, 30, rnd.randint(0, 359), block=False)
for x in (-1700, -300, 1000, 1900):
    P("props_junk/trashbin01a", x, 470, 0, 90)
P("props_combine/combine_barricade_short01a", BRIDGE[0] + 30, -80, 0, 0)
P("props_combine/combine_barricade_short01a", BRIDGE[1] - 30, -80, 0, 180)
cam("street west", -1900, 224, 0, 2, 0)
cam("street east", 1900, 224, 0, 2, 180)
cam("market", 1500, 300, 0, 6, 70)

# ================================================================ transition, spawn, zones, logic
M.ent("info_landmark", LM_POS, targetname="lm_08_04")
M.brush_entity("trigger_changelevel", [M.box_solid((WX0, PASS_Y[0], -8), (WX0 + 64, PASS_Y[1], 216), TRIGGER)],
               map="rp_c17_08_opora", landmark="lm_08_04", spawnflags="0", targetname="oldtown_to_opora")
M.brush_entity("trigger_transition", [M.box_solid((WX0, PASS_Y[0] - 64, -8), (WX0 + 256, PASS_Y[1] + 64, 216), TRIGGER)],
               targetname="lm_08_04")
M.ent("info_player_start", (-1900, 224, 0), angles="0 0 0")
M.brush_entity("trigger_rpg_zone", [M.box_solid((380, CY0 - 64, -16), (840, 120, 256), TRIGGER)], zone="1",
               targetname="oldtown_zone_bridge", spawnflags="1", StartDisabled="0")
M.ent("light_environment", (0, 0, 1100), angles="0 200 0", pitch="-40", _light="237 218 160 700",
      _ambient="180 195 215 90", _lightHDR="-1 -1 -1 1", _lightscaleHDR="0.7", _ambientHDR="-1 -1 -1 1",
      _AmbientScaleHDR="1", SunSpreadAngle="0")
M.ent("env_fog_controller", (0, 0, 1080), targetname="fog_oldtown", fogenable="1", fogblend="0",
      fogcolor="200 205 195", fogcolor2="110 115 120", fogstart="600", fogend="9000", fogmaxdensity="0.7",
      fogdir="1 0 0", angles="0 0 0", use_angles="0", farz="-1", spawnflags="1", foglerptime="0")
M.ent("env_tonemap_controller", (0, 0, 1060), targetname="tonemap_oldtown")

M.m.world.set("comment", "rp_c17_04_oldtown: C17-04 the Old Town (Klyuchevaya, the embankment, Shlyuz-3). "
              "Generated by hl2rpm_extras/tools/make_oldtown.py - edit the generator, not this file.")
M.m.world.set("detailmaterial", "detail/detailsprites")
M.m.world.set("detailvbsp", "detail.vbsp")
M.finish()
# RPG layer: NPCs, dialogs, quests (LM-01, SM-01), the world clock: phaseb_rp_c17_04.py
import phaseb_rp_c17_04  # noqa: E402
_phaseb_log = []
phaseb_rp_c17_04.build_phase_b(M.m, _phaseb_log)
for _line in _phaseb_log:
    print("phase B:", _line)
M.m.save(OUT, backup=os.path.exists(OUT))
print("wrote", OUT)
if MISSING:
    print("MISSING (%d):" % len(MISSING))
    for x in sorted(MISSING):
        print("   ", x)
steps_out = os.path.splitext(OUT)[0] + "_steps.txt"
with open(steps_out, "w") as f:
    f.write("\n".join(STEPS) + "\n")
print("steps:", steps_out, len(STEPS))
