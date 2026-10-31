"""Test map for the HL2RPM Creation Kit (world layer, AI packages, nav generation).

    python make_ck_test.py [out.vmf]

ck_test: a 3072 x 3072 yard under the sky, a house with a doorway (bed, table and
chairs inside), crates and a fence in the yard. No info_node on purpose: the nodes
come from rpm_nav_generate (scripts/world/ck_test_nav.txt). Compile:
    compile_map.ps1 -Vmf "<mapsrc>\\test\\ck_test.vmf"
"""
import os
import sys

sys.path.insert(0, r"E:\SourceModding\HL2RPMSOURCE\.claude\skills\hl2rpm-hammer\scripts")
from vmf import VMF, parse  # noqa: E402
from new_test_map import HEADER  # noqa: E402

OUT = r"E:\XBLAH's modding tool\content\Mapbase\hl2rpm\mapsrc\test\ck_test.vmf"


def main(argv):
    out = argv[1] if len(argv) > 1 else OUT
    m = VMF(parse(HEADER))
    m.path = out
    h, t, height = 1536, 16, 1024
    wall = "BRICK/BRICKWALL003A"
    ground = "NATURE/BLENDDIRTGRASS008A"
    floor = "WOOD/WOODFLOOR005A"
    plaster = "PLASTER/PLASTERWALL005C"
    roofmat = "CONCRETE/CONCRETEFLOOR001A"

    # yard
    m.add_box((-h - t, -h - t, -t), (h + t, h + t, 0), ground)
    m.add_box((-h - t, -h - t, height), (h + t, h + t, height + t), "TOOLS/TOOLSSKYBOX")
    m.add_box((-h - t, -h - t, 0), (-h, h + t, height), "TOOLS/TOOLSSKYBOX")
    m.add_box((h, -h - t, 0), (h + t, h + t, height), "TOOLS/TOOLSSKYBOX")
    m.add_box((-h, -h - t, 0), (h, -h, height), "TOOLS/TOOLSSKYBOX")
    m.add_box((-h, h, 0), (h, h + t, height), "TOOLS/TOOLSSKYBOX")

    # house: x 384..1024, y -320..320, walls 160 high, doorway in the west wall
    x0, x1, y0, y1, wh = 384, 1024, -320, 320, 160
    m.add_box((x0, y0, 0), (x1, y1, 4), floor)                       # floor boards
    m.add_box((x0 - t, y0 - t, 0), (x0, -56, wh), wall)               # west, south of the door
    m.add_box((x0 - t, 56, 0), (x0, y1 + t, wh), wall)                # west, north of the door
    m.add_box((x0 - t, -56, 112), (x0, 56, wh), wall)                 # lintel
    m.add_box((x1, y0 - t, 0), (x1 + t, y1 + t, wh), wall)            # east
    m.add_box((x0, y0 - t, 0), (x1, y0, wh), wall)                    # south
    m.add_box((x0, y1, 0), (x1, y1 + t, wh), wall)                    # north
    m.add_box((x0 - t, y0 - t, wh), (x1 + t, y1 + t, wh + t), roofmat)  # roof
    # a partition: bedroom (north) / kitchen (south), doorway in the middle
    m.add_box((x0 + 320, 0, 4), (x0 + 336, y1, wh), plaster)
    m.add_box((x0 + 320, y0, 4), (x0 + 336, -96, wh), plaster)

    # a fence in the yard (patrol route goes around it)
    m.add_box((-640, -512, 0), (-624, 512, 96), wall)

    def prop(model, pos, yaw=0, static=True):
        m.add_entity("prop_static" if static else "prop_dynamic", pos, model=model, angles="0 %d 0" % yaw, solid="6", skin="0")

    prop("models/props_c17/furniturebed001a.mdl", (930, 220, 4), 180)
    prop("models/props_c17/furnituretable001a.mdl", (520, -200, 4), 0)
    prop("models/props_c17/furniturechair001a.mdl", (520, -260, 4), 90)
    prop("models/props_c17/furniturechair001a.mdl", (520, -140, 4), 270)
    prop("models/props_c17/furniturecouch001a.mdl", (930, -220, 4), 180)
    for i, (x, y) in enumerate(((-200, 700), (-140, 760), (-260, 780), (300, -900))):
        prop("models/props_junk/wood_crate001a.mdl", (x, y, 20), 15 * i)
    prop("models/props_c17/oildrum001.mdl", (-700, 900, 0), 0)
    prop("models/props_trainstation/trashcan_indoor001a.mdl", (300, 380, 0), 0)

    m.add_entity("light_environment", (0, 0, height - 64), angles="-60 45 0", pitch="-60",
                 _light="255 240 220 300", _ambient="140 160 200 60", _lightHDR="-1 -1 -1 1",
                 _lightscaleHDR="1", _ambientHDR="-1 -1 -1 1", _AmbientScaleHDR="1", SunSpreadAngle="5")
    m.add_entity("light", (700, 160, 140), _light="255 210 160 150", style="0")
    m.add_entity("light", (520, -160, 140), _light="255 210 160 150", style="0")
    m.add_entity("info_player_start", (-1100, 0, 8), angles="0 0 0")
    m.save(out, backup=os.path.exists(out))
    print("wrote", out)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
