"""Editor assets of the Claude task markers (hl2rpm.fgd: rpm_claude_task, func_rpm_claude_area):
  materials/hl2rpm/tools/claude_area.vmt/.vtf - translucent orange with diagonal stripes, a tool texture
                                               (nodraw / nonsolid if it is ever compiled), for the area brushes;
  materials/editor/hl2rpm/claude_task.vmt/.vtf - the point marker's icon: the Claude Code spark (the user's wish) -
                                               terracotta rays from the centre on a dark disc, drawn here procedurally.
Written into the mod folder. usage: python make_claude_task_assets.py [mod dir]"""
import os
import sys

import numpy as np
from PIL import Image, ImageDraw, ImageFont

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from make_vitals_assets import write_vtf_bgra  # noqa: E402

MOD = sys.argv[1] if len(sys.argv) > 1 else r"E:\Steam\steamapps\sourcemods\hl2rpm"
ORANGE = (217, 119, 87)        # Claude terracotta


def area_texture(n=128):
    y, x = np.mgrid[0:n, 0:n]
    stripe = ((x + y) % 32) < 10
    rgba = np.zeros((n, n, 4), np.float32)
    rgba[..., 0], rgba[..., 1], rgba[..., 2] = ORANGE[0] / 255, ORANGE[1] / 255, ORANGE[2] / 255
    rgba[..., 3] = np.where(stripe, 0.55, 0.22)
    edge = (x < 2) | (y < 2) | (x >= n - 2) | (y >= n - 2)
    rgba[edge, 3] = 0.75
    return rgba


def icon_texture(n=64, ss=4):
    """the Claude spark: rays of different length from the centre (supersampled, then reduced)"""
    import math
    N = n * ss
    img = Image.new("RGBA", (N, N), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    c = N / 2
    d.ellipse([c - N * 0.47, c - N * 0.47, c + N * 0.47, c + N * 0.47], fill=(24, 20, 18, 235))   # dark disc: readable on any map
    rays = 12
    lengths = [0.40, 0.31, 0.37, 0.28, 0.41, 0.33, 0.38, 0.29, 0.40, 0.32, 0.36, 0.30]
    for i in range(rays):
        a = 2 * math.pi * i / rays - math.pi / 2 + 0.13
        L = lengths[i] * N
        w = N * 0.055
        ux, uy = math.cos(a), math.sin(a)
        px, py = -uy, ux
        r0 = N * 0.05
        pts = [(c + ux * r0 + px * w * 0.55, c + uy * r0 + py * w * 0.55),
               (c + ux * L + px * w * 0.35, c + uy * L + py * w * 0.35),
               (c + ux * L - px * w * 0.35, c + uy * L - py * w * 0.35),
               (c + ux * r0 - px * w * 0.55, c + uy * r0 - py * w * 0.55)]
        d.polygon(pts, fill=ORANGE + (255,))
        d.ellipse([c + ux * L - w * 0.35, c + uy * L - w * 0.35, c + ux * L + w * 0.35, c + uy * L + w * 0.35], fill=ORANGE + (255,))
    d.ellipse([c - N * 0.07, c - N * 0.07, c + N * 0.07, c + N * 0.07], fill=ORANGE + (255,))
    img = img.resize((n, n), Image.LANCZOS)
    img.save(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "art", "claude_task_icon.png"))
    return np.asarray(img, np.float32) / 255.0


def write(rel, rgba, vmt):
    write_vtf_bgra(os.path.join(MOD, "materials", rel + ".vtf"), rgba)
    with open(os.path.join(MOD, "materials", rel + ".vmt"), "w", encoding="utf-8", newline="\n") as f:
        f.write(vmt)
    print("written", os.path.join(MOD, "materials", rel + ".vmt"))


write("hl2rpm/tools/claude_area", area_texture(), '''"UnlitGeneric"
{
	"$basetexture" "hl2rpm/tools/claude_area"
	"$translucent" "1"
	"$nocull" "1"
	"%tooltexture" "hl2rpm/tools/claude_area"
	"%compilenodraw" "1"
	"%compilenonsolid" "1"
	"%keywords" "hl2rpm,tools,claude"
}
''')
write("editor/hl2rpm/claude_task", icon_texture(), '''"Sprite"
{
	"$spriteorientation" "vp_parallel"
	"$spriteorigin" "[ 0.50 0.50 ]"
	"$basetexture" "editor/hl2rpm/claude_task"
	"$translucent" "1"
}
''')
