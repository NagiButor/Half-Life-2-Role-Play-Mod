"""PBR gallery map: rows of stock world materials, each on a floor tile and on a wall panel.

    python make_pbr_gallery.py <out.vmf> [materials per folder]

Row r (one folder of materials: concrete, brick, metal ...) runs along y at x = r * 704; cell c
of a row is at y = c * 160: a floor tile 392 x 128 units with a wall panel 128 units high across
its middle (the material on both). Looking at a row from -x and from +x shows the panels lit by
the sun and against it (the glare on the floor). The cameras and the materials of every row are
printed and written next to the map as <out>.txt (steps for run_game.ps1).
"""
import os
import re
import sys

sys.path.insert(0, r"E:\SourceModding\HL2RPMSOURCE\.claude\skills\hl2rpm-hammer\scripts")
from vmf import VMF, parse  # noqa: E402
import new_test_map  # noqa: E402

REF = r"E:\SourceModding\UsefulStuff\Source sdk 2013 HL2 unpacked vpk\hl2\materials"
FOLDERS = ["concrete", "brick", "plaster", "stone", "tile", "wood", "metal", "nature", "building_template", "composite"]

out = sys.argv[1]
per_row = int(sys.argv[2]) if len(sys.argv) > 2 else 6


def pick(folder, n):
    d = os.path.join(REF, folder)
    good = []
    for f in sorted(os.listdir(d)):
        if not f.lower().endswith(".vmt"):
            continue
        try:
            t = open(os.path.join(d, f), encoding="latin-1").read()
        except OSError:
            continue
        t = re.sub(r"//.*", "", t)
        head = t.lstrip().split("\n")[0].strip().strip('"').lower()
        if head != "lightmappedgeneric":
            continue
        if re.search(r'\$(translucent|alphatest|decal|selfillum)"?\s+"?1', t, re.I):
            continue
        if not re.search(r"\$basetexture", t, re.I):
            continue
        good.append(folder + "/" + f[:-4])
    if len(good) <= n:
        return good
    step = len(good) / float(n)
    return [good[int(i * step)] for i in range(n)]


m = VMF(parse(new_test_map.HEADER))
m.path = out

SKY = "TOOLS/TOOLSSKYBOX"
GROUND = "CONCRETE/CONCRETEFLOOR033A"
ROW = 704
CELL = 160

rows = [(f, pick(f, per_row)) for f in FOLDERS]
rows = [r for r in rows if r[1]]
X0, X1 = -640, len(rows) * ROW + 256
Y0, Y1 = -512, per_row * CELL + 512
ZT = 1024


def box(a, b, mat, **kw):
    return m.add_box(a, b, mat, **kw)


box((X0 - 16, Y0 - 16, -32), (X1 + 16, Y1 + 16, -16), GROUND)
box((X0, Y0, -16), (X1, Y1, 0), GROUND)
box((X0 - 16, Y0 - 16, ZT), (X1 + 16, Y1 + 16, ZT + 16), SKY)
box((X0 - 16, Y0 - 16, -16), (X0, Y1 + 16, ZT), SKY)
box((X1, Y0 - 16, -16), (X1 + 16, Y1 + 16, ZT), SKY)
box((X0, Y0 - 16, -16), (X1, Y0, ZT), SKY)
box((X0, Y1, -16), (X1, Y1 + 16, ZT), SKY)

steps = ["|cl_weather_tour_step 12; sv_timecycle_set_time 13; sv_weather clear 0; noclip"]
notes = []
for r, (folder, mats) in enumerate(rows):
    rx = r * ROW
    for c, mat in enumerate(mats):
        y = c * CELL
        # the floor tile (2 units over the ground) and the wall panel across its middle
        box((rx, y, 0), (rx + 392, y + 128, 2), GROUND, face_materials={"top": mat})
        box((rx + 192, y, 2), (rx + 200, y + 128, 130), mat)
    notes.append("row %d  x %5d  %s: %s" % (r, rx, folder, ", ".join(x.split("/")[1] for x in mats)))
    half = per_row * CELL / 2.0
    for k in (0, 1):
        yc = half / 2.0 + k * half - 16
        steps.append("shot|setpos %d %d 20; setang 12 0 0" % (rx - 190, yc))
        steps.append("shot|setpos %d %d 20; setang 12 180 0" % (rx + 582, yc))

# a lamp over the first cells of every row (the reflections of lamps at night)
for r in range(len(rows)):
    m.add_entity("light", (r * ROW + 96, 64, 150), _light="255 230 190 500", _constant_attn="0", _linear_attn="0",
                 _quadratic_attn="1", _fifty_percent_distance="0", _zero_percent_distance="0", style="0")

m.add_entity("light_environment", (0, -256, 512), angles="-58 14 0", pitch="-58",
             _light="255 240 220 300", _ambient="140 160 200 60", _lightHDR="-1 -1 -1 1",
             _lightscaleHDR="1", _ambientHDR="-1 -1 -1 1", _AmbientScaleHDR="1", SunSpreadAngle="5")
m.add_entity("info_player_start", (-320, 0, 8), angles="0 0 0")
m.save(out, backup=os.path.exists(out))
open(os.path.splitext(out)[0] + ".txt", "w").write("\n".join(steps) + "\n")
print("wrote", out)
print("\n".join(notes))
