# -*- coding: utf-8 -*-
"""The turning valve wheel of the loading screen (game/client/hl2rpm_mainmenu.cpp, DrawLoading):
a red cast gate-valve handle (the user's reference: hexagonal, round lobes at the corners, a square
nut turned as a diamond) rendered in Blender (item_models/blender_valve.py), 32 frames of a sixth
of a turn (six lobes: the loop is seamless) in an 8 x 4 sheet of 128 px frames.

    python make_loading_valve.py [--size 256] [--keep <dir>]

-> materials/vgui/hl2rpm/menu/loading_valve.vtf / .vmt (BGRA8888, no mips: the frames are
   picked with DrawTexturedSubRect, a mip would bleed one frame into the next)
The user's reference picture: hl2rpm_extras/art/loading_valve_ref.png.
"""
import os
import shutil
import subprocess
import sys
import tempfile

import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import make_menu_assets as mm  # noqa: E402

BLENDER = r"C:\Program Files\Blender Foundation\Blender 5.1\blender.exe"
FRAMES, COLS, ROWS, CELL = 32, 8, 4, 128


def main(argv):
    size = int(argv[argv.index("--size") + 1]) if "--size" in argv else 256
    work = argv[argv.index("--keep") + 1] if "--keep" in argv else tempfile.mkdtemp(prefix="hl2rpm_valve_")
    os.makedirs(work, exist_ok=True)
    print("blender: %d frames of %d px -> %s" % (FRAMES, size, work))
    subprocess.run([BLENDER, "--background", "--factory-startup", "--python", os.path.join(HERE, "item_models", "blender_valve.py"),
                    "--", work, str(FRAMES), str(size)], check=True, stdout=subprocess.DEVNULL)
    sheet = np.zeros((ROWS * CELL, COLS * CELL, 4), np.float32)
    for f in range(FRAMES):
        im = Image.open(os.path.join(work, "valve_%02d.png" % f)).convert("RGBA")
        a = np.asarray(im, np.float32)
        # downscale premultiplied: the edges must not go dark
        pre = np.dstack([a[..., :3] * a[..., 3:4] / 255, a[..., 3:4]])
        small = np.dstack([np.asarray(Image.fromarray(pre[..., c].astype(np.float32), "F").resize((CELL, CELL), Image.LANCZOS)) for c in range(4)])
        al = np.clip(small[..., 3:4], 0, 255)
        rgb = small[..., :3] * 255 / np.maximum(al, 1)
        x, y = (f % COLS) * CELL, (f // COLS) * CELL
        sheet[y:y + CELL, x:x + CELL] = np.dstack([np.clip(rgb, 0, 255), al])
    os.makedirs(mm.OUT, exist_ok=True)
    mm.write_bgra(os.path.join(mm.OUT, "loading_valve.vtf"), sheet, mips=False)
    mm.write_vmt("loading_valve")
    Image.fromarray(sheet.astype(np.uint8), "RGBA").save(os.path.join(work, "sheet.png"))
    print("done:", os.path.join(mm.OUT, "loading_valve.vtf"))
    if "--keep" not in argv:
        shutil.rmtree(work, ignore_errors=True)


if __name__ == "__main__":
    main(sys.argv[1:])
