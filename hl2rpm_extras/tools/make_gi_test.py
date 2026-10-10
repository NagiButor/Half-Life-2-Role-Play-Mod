"""GI test map: a one-storey building with rooms lit only through their openings.

  room A (x 16..504, y -496..-64): two windows south, one window west, door to the corridor
  room B (x 520..1008, y -496..-64): closed, only a door to the corridor (should be dark)
  room C (x 16..504, y 64..496): skylight in the roof
  room D (x 520..1008, y 64..496): garage door north
  corridor (x 16..1008, y -48..48): entrance from outside at the west end
"""
import os
import sys

sys.path.insert(0, r"E:\SourceModding\HL2RPMSOURCE\.claude\skills\hl2rpm-hammer\scripts")
from vmf import VMF, parse  # noqa: E402
import new_test_map  # noqa: E402

out = sys.argv[1]
m = VMF(parse(new_test_map.HEADER))
m.path = out

SKY = "TOOLS/TOOLSSKYBOX"
GROUND = "CONCRETE/CONCRETEFLOOR005A"
OUTER = "BRICK/BRICKWALL001A"
INNER = "PLASTER/PLASTERWALL003A"
ROOF = "CONCRETE/CONCRETEFLOOR001A"
FLOOR_A = "WOOD/WOODFLOOR001A"
FLOOR = "CONCRETE/CONCRETEFLOOR002A"


def box(a, b, mat):
    if a[0] < b[0] and a[1] < b[1] and a[2] < b[2]:
        m.add_box(a, b, mat)


def wall_x(x0, x1, y0, y1, z0, z1, mat, openings=()):
    """wall spanning x0..x1, thickness y0..y1; openings (a0, a1, b0, b1) in x and z"""
    cur = x0
    for a0, a1, b0, b1 in sorted(openings):
        box((cur, y0, z0), (a0, y1, z1), mat)
        box((a0, y0, z0), (a1, y1, b0), mat)
        box((a0, y0, b1), (a1, y1, z1), mat)
        cur = a1
    box((cur, y0, z0), (x1, y1, z1), mat)


def wall_y(y0, y1, x0, x1, z0, z1, mat, openings=()):
    """wall spanning y0..y1, thickness x0..x1; openings (a0, a1, b0, b1) in y and z"""
    cur = y0
    for a0, a1, b0, b1 in sorted(openings):
        box((x0, cur, z0), (x1, a0, z1), mat)
        box((x0, a0, z0), (x1, a1, b0), mat)
        box((x0, a0, b1), (x1, a1, z1), mat)
        cur = a1
    box((x0, cur, z0), (x1, y1, z1), mat)


# outdoor area sealed by sky
X0, X1, Y0, Y1, ZT = -1280, 1792, -1280, 1280, 1536
box((X0 - 16, Y0 - 16, -16), (X1 + 16, Y1 + 16, 0), GROUND)
box((X0 - 16, Y0 - 16, ZT), (X1 + 16, Y1 + 16, ZT + 16), SKY)
box((X0 - 16, Y0 - 16, 0), (X0, Y1 + 16, ZT), SKY)
box((X1, Y0 - 16, 0), (X1 + 16, Y1 + 16, ZT), SKY)
box((X0, Y0 - 16, 0), (X1, Y0, ZT), SKY)
box((X0, Y1, 0), (X1, Y1 + 16, ZT), SKY)

H = 192
# floors inside (2 units over the ground)
box((16, -496, 0), (504, -64, 2), FLOOR_A)
box((520, -496, 0), (1008, -64, 2), FLOOR)
box((16, 64, 0), (504, 496, 2), FLOOR)
box((520, 64, 0), (1008, 496, 2), FLOOR)
box((16, -48, 0), (1008, 48, 2), FLOOR)

# outer walls
wall_x(0, 1024, -512, -496, 0, H, OUTER, [(128, 224, 48, 144), (288, 384, 48, 144)])   # south, room A windows
wall_x(0, 1024, 496, 512, 0, H, OUTER, [(640, 896, 0, 160)])                        # north, garage door of D
wall_y(-496, 496, 0, 16, 0, H, OUTER, [(-352, -256, 48, 144), (-48, 48, 0, 112)])    # west: window A, corridor entrance
wall_y(-496, 496, 1008, 1024, 0, H, OUTER)                                          # east

# corridor walls with doors
wall_x(16, 1008, -64, -48, 0, H, INNER, [(400, 464, 0, 112), (560, 624, 0, 112)])
wall_x(16, 1008, 48, 64, 0, H, INNER, [(400, 464, 0, 112), (560, 624, 0, 112)])
# walls between the rooms
wall_y(-496, -64, 504, 520, 0, H, INNER)
wall_y(64, 496, 504, 520, 0, H, INNER)

# roof with the skylight of room C (x 192..320, y 256..384)
box((0, -512, H), (1024, 256, H + 16), ROOF)
box((0, 384, H), (1024, 512, H + 16), ROOF)
box((0, 256, H), (192, 384, H + 16), ROOF)
box((320, 256, H), (1024, 384, H + 16), ROOF)

# a pillar outside for a shadow
box((-384, -96, 0), (-320, -32, 256), OUTER)

m.add_entity("light_environment", (-512, 0, 512), angles="-50 60 0", pitch="-50",
             _light="255 240 220 300", _ambient="140 160 200 60", _lightHDR="-1 -1 -1 1",
             _lightscaleHDR="1", _ambientHDR="-1 -1 -1 1", _AmbientScaleHDR="1", SunSpreadAngle="5")
m.add_entity("info_player_start", (96, 0, 8), angles="0 0 0")
m.save(out, backup=os.path.exists(out))
print("wrote", out)
