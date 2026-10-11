"""Water test map: a row of bays, each with a pool of one stock water material.

    python make_water_test.py <out.vmf>

Bay i spans x = i * 640 .. i * 640 + 640: a brick divider (x 0..32), the pool at x 144..528,
y 0..512. The water level is z = -8; the bottom goes down in four steps of 128 units along y:
16, 64, 128 and 256 units of water (the fog of each material against a known depth). A pillar
stands in the pool, a drum lies half in the water on the second step, a wall and a tall block
stand behind (reflections). Camera for bay i: setpos (i * 640 + 336) -420 150; setang 24 90 0.
The list of materials is printed (and is in WATERS below).
"""
import os
import sys

sys.path.insert(0, r"E:\SourceModding\HL2RPMSOURCE\.claude\skills\hl2rpm-hammer\scripts")
from vmf import VMF, parse  # noqa: E402
import new_test_map  # noqa: E402

WATERS = [
    "nature/water_canals_city_murky",   # 0 brown, opaque within 120 units (canals of City 17)
    "nature/water_canals_water2",       # 1 grey-brown, 300
    "nature/water_canals_water_clear",  # 2 clear, 800
    "nature/water_coast01",             # 3 the sea of the coast: teal, 100..500
    "nature/water_lowdirty001",         # 4 refraction only + cubemap: dark olive, 150
    "nature/sewer_water001",            # 5 refraction only + cubemap, 250
    "nature/water_riverbed01",          # 6 EP2 river: dark teal, 196
    "nature/water_canals_cheap001",     # 7 $forcecheap: cubemap + fog color only
    "nature/water_canals03",            # 8 dark brown, 330
    "nature/water_tunnels01",           # 9 EP2 tunnels: nearly black green
]

out = sys.argv[1]
m = VMF(parse(new_test_map.HEADER))
m.path = out

SKY = "TOOLS/TOOLSSKYBOX"
NODRAW = "TOOLS/TOOLSNODRAW"
GROUND = "CONCRETE/CONCRETEFLOOR033A"
LINING = "TILE/TILEFLOOR013A"
BRICK = "BRICK/BRICKWALL001A"
PLASTER = "PLASTER/PLASTERWALL003A"
PILLAR = "CONCRETE/CONCRETEWALL004A"
STEPS = ["NATURE/SANDFLOOR010A", "NATURE/GRAVELFLOOR002A", "STONE/STONEFLOOR006A", "CONCRETE/CONCRETEFLOOR005A"]

CELL = 640
N = len(WATERS)
WATER_Z = -8
DEPTHS = [16, 64, 128, 256]
BASE = -320


def box(a, b, mat, **kw):
    assert a[0] < b[0] and a[1] < b[1] and a[2] < b[2], (a, b)
    return m.add_box(a, b, mat, **kw)


X0, X1, Y0, Y1, ZT = 0, N * CELL + 32, -1024, 1024, 1280
# the sealing shell
box((X0 - 16, Y0 - 16, BASE - 16), (X1 + 16, Y1 + 16, BASE), NODRAW)
box((X0 - 16, Y0 - 16, ZT), (X1 + 16, Y1 + 16, ZT + 16), SKY)
box((X0 - 16, Y0 - 16, BASE), (X0, Y1 + 16, ZT), SKY)
box((X1, Y0 - 16, BASE), (X1 + 16, Y1 + 16, ZT), SKY)
box((X0, Y0 - 16, BASE), (X1, Y0, ZT), SKY)
box((X0, Y1, BASE), (X1, Y1 + 16, ZT), SKY)

# the ground in front of and behind the pools
box((X0, Y0, BASE), (X1, 0, 0), GROUND)
box((X0, 512, BASE), (X1, Y1, 0), GROUND)
# the back wall
box((X0, 640, 0), (X1, 672, 320), BRICK)

for i, water in enumerate(WATERS):
    bx = i * CELL
    px0, px1 = bx + 144, bx + 528
    # the ground beside the pool
    box((bx, 0, BASE), (px0, 512, 0), GROUND)
    box((px1, 0, BASE), (bx + CELL, 512, 0), GROUND)
    # the divider
    box((bx, -64, 0), (bx + 32, 640, 288), BRICK)
    # a tall block behind the back wall
    box((bx + 216, 704, 0), (bx + 456, 832, 640), PLASTER)
    for j, depth in enumerate(DEPTHS):
        top = WATER_Z - depth
        y0, y1 = j * 128, j * 128 + 128
        box((px0, y0, BASE), (px1, y1, top), LINING, face_materials={"top": STEPS[j]})
        box((px0, y0, top), (px1, y1, WATER_Z), NODRAW, face_materials={"top": water})
    # a pillar on the third step, 96 over the water
    m.add_brush_entity("func_detail", [m.make_box((bx + 400, 288, WATER_Z - DEPTHS[2]), (bx + 448, 336, 96), PILLAR)])
    # a drum half in the water on the second step, a crate on the bank
    m.add_entity("prop_dynamic_override", (bx + 250, 190, WATER_Z - 6), model="models/props_c17/oildrum001.mdl",
                 angles="0 30 90", solid="6", disableshadows="0")
    m.add_entity("prop_dynamic_override", (bx + 90, 420, 0), model="models/props_junk/wood_crate001a.mdl",
                 angles="0 20 0", solid="6", disableshadows="0")
    m.add_entity("env_cubemap", (bx + 336, 256, 56), cubemapsize="0")
    m.add_entity("info_target", (bx + 336, -420, 150), targetname="bay_%d" % i, angles="24 90 0")
# the last divider
box((N * CELL, -64, 0), (N * CELL + 32, 640, 288), BRICK)

# (the sun of the deferred renderer at 13:00 - sv_timecycle_set_time 13 - for comparisons with -nodeferred)
m.add_entity("light_environment", (64, -512, 512), angles="-58 14 0", pitch="-58",
             _light="255 240 220 300", _ambient="140 160 200 60", _lightHDR="-1 -1 -1 1",
             _lightscaleHDR="1", _ambientHDR="-1 -1 -1 1", _AmbientScaleHDR="1", SunSpreadAngle="5")
m.add_entity("water_lod_control", (64, -448, 64), cheapwaterstartdistance="1000", cheapwaterenddistance="2000")
m.add_entity("info_player_start", (336, -420, 8), angles="0 90 0")
m.save(out, backup=os.path.exists(out))
print("wrote", out)
for i, w in enumerate(WATERS):
    print("  bay %d  x %5d  %s" % (i, i * CELL + 336, w))
