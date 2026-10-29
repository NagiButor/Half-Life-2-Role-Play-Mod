#!/usr/bin/env python3
"""Main menu logo and New Game chapter picture as game materials.

    python -I make_menu_assets.py <MOD> [--chapter <chapter1_shot.jpg>] [--loading <menu_background_shot.jpg>]

  MOD/materials/vgui/hl2rpm/menu_logo.vtf/.vmt   from art/menu_logo_1024x256_en.png (DXT5, alpha)
  MOD/resource/GameLogo.res                      the ImagePanel GameUI shows with gameinfo "gamelogo 1"
  MOD/materials/vgui/chapters/chapter1.vtf/.vmt  256x128 crop of an in-game frame (the New Game list)
  MOD/materials/console/background_c17[_widescreen].vtf/.vmt  the loading screen: the engine draws
      console/<chapter background map>(_widescreen) behind "Loading..." - without it a pink checkerboard
"""
import argparse
import os
import sys

import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
ART = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(os.path.dirname(ART), "tools"))
from hd_textures import write_vtf  # noqa: E402

CLAMP = 0x4 | 0x8

GAMELOGO_RES = '''"Resource/GameLogo.res"
{
	"GameLogo"
	{
		"ControlName"	"EditablePanel"
		"fieldName"		"GameLogo"
		"xpos"			"0"
		"ypos"			"0"
		"zpos"			"50"
		"wide"			"480"
		"tall"			"120"
		"autoResize"	"1"
		"pinCorner"		"0"
		"visible"		"1"
		"enabled"		"1"
		"offsetX"		"-12"
		"offsetY"		"-24"
	}
	"Logo"
	{
		"ControlName"	"ImagePanel"
		"fieldName"		"Logo"
		"xpos"			"0"
		"ypos"			"0"
		"zpos"			"50"
		"wide"			"480"
		"tall"			"120"
		"visible"		"1"
		"enabled"		"1"
		"image"			"../vgui/hl2rpm/menu_logo"
		"scaleImage"	"1"
	}
}
'''


def vmt(path, base, translucent):
    with open(path, "w", encoding="ascii") as f:
        f.write('"UnlitGeneric"\n{\n\t"$basetexture" "%s"\n\t"$vertexcolor" "1"\n\t"$vertexalpha" "1"\n'
                '\t"$no_fullbright" "1"\n\t"$ignorez" "1"\n%s}\n'
                % (base, '\t"$translucent" "1"\n' if translucent else ''))


def loading_backgrounds(mod, shot, name="background_c17"):
    """4:3 and 16:9 crops of a menu background frame, squeezed into 1024x1024 like Valve's, darker at the
    edges and at the bottom (the "Loading..." box)."""
    im = Image.open(shot).convert("RGB")
    w, h = im.size
    d = os.path.join(mod, "materials", "console")
    os.makedirs(d, exist_ok=True)
    for suffix, aspect in (("", 4.0 / 3.0), ("_widescreen", 16.0 / 9.0)):
        cw = min(w, int(round(h * aspect)))
        x0 = (w - cw) // 2
        a = np.asarray(im.crop((x0, 0, x0 + cw, h)).resize((1024, 1024), Image.LANCZOS)).astype(np.float32) / 255.0
        yy, xx = np.mgrid[0:1024, 0:1024] / 1023.0
        vig = 1.0 - 0.45 * np.clip(((xx - 0.5) ** 2 + (yy - 0.45) ** 2) * 2.2, 0, 1) - 0.25 * np.clip((yy - 0.7) / 0.3, 0, 1)
        a = np.clip(a * vig[..., None] * 1.08, 0, 1)
        rgba = np.concatenate([(a * 255 + 0.5).astype(np.uint8), np.full((1024, 1024, 1), 255, np.uint8)], -1)
        write_vtf(os.path.join(d, name + suffix + ".vtf"), rgba, CLAMP, False)
        with open(os.path.join(d, name + suffix + ".vmt"), "w", encoding="ascii") as f:
            f.write('UnlitGeneric\n{\n\t$basetexture\t"console/%s%s"\n\t$vertexcolor\t"1"\n\t$vertexalpha\t"1"\n'
                    '\t$ignorez\t"1"\n\t$no_fullbright\t"1"\n\t$nolod\t"1"\n}\n' % (name, suffix))
    print("loading backgrounds console/%s(_widescreen) written" % name)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("mod")
    ap.add_argument("--chapter", help="in-game frame for the New Game chapter picture")
    ap.add_argument("--loading", help="frame of the menu background map for the loading screen")
    args = ap.parse_args()
    mod = args.mod
    # logo: 1024x256 RGBA, premultiply-free; DXT5
    logo = np.asarray(Image.open(os.path.join(ART, "menu_logo_1024x256_en.png")).convert("RGBA"))
    d = os.path.join(mod, "materials", "vgui", "hl2rpm")
    os.makedirs(d, exist_ok=True)
    write_vtf(os.path.join(d, "menu_logo.vtf"), logo, CLAMP, True)
    vmt(os.path.join(d, "menu_logo.vmt"), "vgui/hl2rpm/menu_logo", True)
    with open(os.path.join(mod, "resource", "GameLogo.res"), "w", encoding="ascii") as f:
        f.write(GAMELOGO_RES)
    if args.loading:
        loading_backgrounds(mod, args.loading)
    if not args.chapter:
        print("menu logo, GameLogo.res written to", mod)
        return
    # chapter picture: 2:1 crop, 256x128
    im = Image.open(args.chapter).convert("RGB")
    w, h = im.size
    ch = int(w / 2)
    y0 = max(0, (h - ch) // 2 - h // 10)
    pic = im.crop((0, y0, w, y0 + ch)).resize((256, 128), Image.LANCZOS)
    rgba = np.concatenate([np.asarray(pic), np.full((128, 256, 1), 255, np.uint8)], -1)
    d = os.path.join(mod, "materials", "vgui", "chapters")
    os.makedirs(d, exist_ok=True)
    write_vtf(os.path.join(d, "chapter1.vtf"), rgba, CLAMP, False)
    vmt(os.path.join(d, "chapter1.vmt"), "vgui/chapters/chapter1", False)
    print("menu logo, GameLogo.res, chapter1 picture written to", mod)


if __name__ == "__main__":
    main()
