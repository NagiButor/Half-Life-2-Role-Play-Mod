#!/usr/bin/env python3
"""Generate a small sealed test map (VMF) for HL2RPM experiments.

    python new_test_map.py <out.vmf> [--size 1024] [--height 512] [--landmark NAME]
                           [--lamp] [--indoor] [--changelevel MAP] [--shelters]

A hollow box of world brushes with a sky ceiling (or a solid roof with
--indoor), a light_environment (sun, sky ambient), info_player_start in the
middle, optionally an info_landmark (level transitions with changelevel2
<map> <landmark>), a named lamp (--lamp, tests light conversion) and a
trigger_changelevel on one wall (--changelevel MAP, uses the landmark).
Compile with compile_map.ps1 -Vmf <out.vmf>.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from vmf import VMF, Node, parse  # noqa: E402

HEADER = """versioninfo
{
	"editorversion" "400"
	"editorbuild" "9540"
	"mapversion" "1"
	"formatversion" "100"
	"prefab" "0"
}
visgroups
{
}
viewsettings
{
	"bSnapToGrid" "1"
	"bShowGrid" "1"
	"bShowLogicalGrid" "0"
	"nGridSpacing" "64"
	"bShow3DGrid" "0"
}
world
{
	"id" "1"
	"mapversion" "1"
	"classname" "worldspawn"
	"skyname" "sky_day01_01"
	"maxpropscreenwidth" "-1"
	"detailvbsp" "detail.vbsp"
	"detailmaterial" "detail/detailsprites"
}
cameras
{
	"activecamera" "-1"
}
cordons
{
	"active" "0"
}
"""


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 1
    out = argv[1]
    rest = argv[2:]

    def opt(name, default=None):
        if name in rest:
            i = rest.index(name)
            v = rest[i + 1]
            del rest[i:i + 2]
            return v
        return default

    size = float(opt("--size", "1024"))
    height = float(opt("--height", "512"))
    landmark = opt("--landmark")
    changelevel = opt("--changelevel")
    lamp = "--lamp" in rest
    indoor = "--indoor" in rest
    shelters = "--shelters" in rest

    m = VMF(parse(HEADER))
    m.path = out
    h = size / 2
    t = 16
    wall = "DEV/DEV_MEASUREWALL01A"
    floor = "DEV/DEV_MEASUREGENERIC01B"
    roof = "DEV/DEV_MEASUREGENERIC01" if indoor else "TOOLS/TOOLSSKYBOX"
    m.add_box((-h - t, -h - t, -t), (h + t, h + t, 0), floor)                      # floor
    m.add_box((-h - t, -h - t, height), (h + t, h + t, height + t), roof)            # roof / sky
    m.add_box((-h - t, -h - t, 0), (-h, h + t, height), wall)                        # -x
    m.add_box((h, -h - t, 0), (h + t, h + t, height), wall)                          # +x
    m.add_box((-h, -h - t, 0), (h, -h, height), wall)                                # -y
    m.add_box((-h, h, 0), (h, h + t, height), wall)                                  # +y
    # a pillar to cast shadows
    m.add_box((-64, -64, 0), (64, 64, height * 0.5), wall)

    m.add_entity("light_environment", (0, 0, height - 64), angles="-60 45 0", pitch="-60",
                 _light="255 240 220 300", _ambient="140 160 200 60", _lightHDR="-1 -1 -1 1",
                 _lightscaleHDR="1", _ambientHDR="-1 -1 -1 1", _AmbientScaleHDR="1", SunSpreadAngle="5")
    m.add_entity("info_player_start", (-h * 0.5, -h * 0.5, 8), angles="0 45 0")
    if landmark:
        m.add_entity("info_landmark", (h * 0.25, -h * 0.25, 32), targetname=landmark)
    if lamp:
        m.add_entity("light", (h * 0.5, h * 0.5, height * 0.4), targetname="test_lamp",
                     _light="255 200 150 400", _constant_attn="0", _linear_attn="0", _quadratic_attn="1",
                     _fifty_percent_distance="0", _zero_percent_distance="0", style="0")
    if shelters:
        # three 256x256 roofs 192 u above the floor on thin posts: world brush,
        # func_detail and func_brush (precipitation/wetness tests); centers at
        # (-h/2, h/2), (0, h/2), (h/2, h/2)... listed in the console by vmf.py ents
        roof = "DEV/DEV_MEASUREGENERIC01"
        for i, kind in enumerate(("world", "func_detail", "func_brush")):
            cx = -h * 0.5 + i * h * 0.5
            cy = h * 0.45
            box = ((cx - 128, cy - 128, 192), (cx + 128, cy + 128, 208))
            if kind == "world":
                m.add_box(box[0], box[1], roof)
            else:
                keys = {"solidity": "2", "disablereceiveshadows": "0"} if kind == "func_brush" else {}
                m.add_brush_entity(kind, [m.make_box(box[0], box[1], roof)], **keys)
            for dx in (-120, 112):
                for dy in (-120, 112):
                    m.add_box((cx + dx, cy + dy, 0), (cx + dx + 8, cy + dy + 8, 192), wall)
            m.add_entity("info_target", (cx, cy, 64), targetname="shelter_%s" % kind)
    if changelevel:
        trig = m.add_brush_entity("trigger_changelevel",
                                  [m.make_box((h - 48, -64, 0), (h, 64, 128), "TOOLS/TOOLSTRIGGER")],
                                  map=changelevel, landmark=landmark or "", spawnflags="0")
    m.save(out, backup=os.path.exists(out))
    print("wrote", out)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
