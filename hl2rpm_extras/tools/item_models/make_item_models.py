# -*- coding: utf-8 -*-
"""HL2RPM: small item models made in Blender by code: a bobby pin and two keys.

    python make_item_models.py [bobby_pin key_flat key_old] [--no-bake] [--no-compile] [--render]

For each model: the stamps (PIL) -> blender_items.py (geometry, procedural materials, baked atlas,
SMD, .blend) -> VTF and VMT in the mod -> QC with a box for physics -> studiomdl.

The sources stay in modelsrc\\hl2rpm_items\\<model> next to the map sources: <model>.blend opens in
Blender with its procedural materials, PNG maps, SMD, QC. Results: hl2rpm\\models\\hl2rpm\\items,
hl2rpm\\materials\\models\\hl2rpm\\items.
"""
import argparse
import os
import random
import shutil
import struct
import subprocess
import sys

import numpy as np
from PIL import Image, ImageDraw, ImageFilter, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))
import hd_textures  # noqa: E402  (the DXT coder and the VTF header of the HD textures)

BLENDER = r"C:\Program Files\Blender Foundation\Blender 5.1\blender.exe"
SDK_BIN = r"E:\Steam\steamapps\common\Source SDK Base 2013 Singleplayer\bin"
MOD = r"E:\Steam\steamapps\sourcemods\hl2rpm"
SRC = r"E:\XBLAH's modding tool\content\Mapbase\hl2rpm\modelsrc\hl2rpm_items"
FONT = r"C:\Windows\Fonts\arialbd.ttf"
MAT = "models/hl2rpm/items"

# gloss = alpha of the normal map ($pbr_roughness 0 leaves it alone)
MODELS = {
    "bobby_pin": dict(metal=0.55, prop="chainlink", mass=0.2, phong=16, boost=2.0),
    "key_flat": dict(metal=0.85, prop="chainlink", mass=0.3, phong=24, boost=3.0),
    "key_old": dict(metal=0.70, prop="metal_box", mass=0.5, phong=14, boost=1.5),
    # (the stand of the old suit items - make_suit_piles.py lays them on the ground now; textures, no model of its own)
    "suit_stand": dict(metal=0.60, prop="metal", mass=10, phong=14, boost=1.0, part=True),
}
MIN_THICK = 0.55    # units: studiomdl drops a hull thinner than half a unit without a word (no .phy)


def stamps(work):
    """1 = the surface, 0 = pressed in. The sizes are those of stamp_uv in blender_items.py"""
    cm = 1024 / 3.0
    w, h = 1024, int(2.4 * cm)
    img = Image.new("L", (w, h), 255)
    d = ImageDraw.Draw(img)

    def pt(x, y):
        return ((x + 2.95) * cm, (1.2 - y) * cm)

    # a line pressed along the rim of the bow, around the hole and the text
    x, y = pt(-1.25 - 1.03, 1.03)
    x2, y2 = pt(-1.25 + 1.03, -1.03)
    d.ellipse((x, y, x2, y2), outline=0, width=int(0.03 * cm))
    d.ellipse((x + 0.06 * cm, y + 0.06 * cm, x2 - 0.06 * cm, y2 - 0.06 * cm), outline=255, width=int(0.03 * cm))
    f = ImageFont.truetype(FONT, int(0.2 * cm))
    for (txt, yy) in (("ГОСТ", 0.15), ("5089", -0.15)):
        cx, cy = pt(-0.68, yy)
        d.text((cx, cy), txt, font=f, fill=0, anchor="mm")
    img.filter(ImageFilter.GaussianBlur(1.6)).save(os.path.join(work, "stamp_bow.png"))

    # the number of the tag: digit by digit with a hand punch, none quite straight
    w, h = 1024, 465
    img = Image.new("L", (w, h), 255)
    d = ImageDraw.Draw(img)
    cm = 1024 / 3.0
    random.seed(17)
    x = 450
    for ch, adv in (("№", 220), ("1", 128), ("7", 0)):
        g = Image.new("L", (360, 360), 0)
        ImageDraw.Draw(g).text((180, 180), ch, font=ImageFont.truetype(FONT, 215), fill=255, anchor="mm")
        g = g.rotate(random.uniform(-5, 5), resample=Image.BICUBIC)
        img.paste(0, (x - 180, h // 2 - 180 + random.randint(-9, 9)), g)
        x += adv
    img.filter(ImageFilter.GaussianBlur(2.2)).save(os.path.join(work, "stamp_tag.png"))


def run_blender(name, work, flags):
    cmd = [BLENDER, "--background", "--factory-startup", "--python", os.path.join(HERE, "blender_items.py"), "--", name, work] + flags
    r = subprocess.run(cmd, capture_output=True, text=True, encoding="utf-8", errors="replace")
    for line in r.stdout.splitlines():
        if line.startswith("[items]") or "Traceback" in line or "Error" in line:
            print("  " + line)
    if r.returncode != 0:
        print(r.stdout[-3000:], r.stderr[-1500:])
        raise SystemExit("blender failed on " + name)


def normal_mips(img):
    """RGBA uint8: the normals are averaged and made unit length again, the gloss is averaged"""
    levels = [img]
    cur = img.astype(np.float32)
    while cur.shape[0] > 1 or cur.shape[1] > 1:
        h, w = cur.shape[:2]
        nh, nw = max(1, h // 2), max(1, w // 2)
        if h > 1 and w > 1:
            cur = cur.reshape(nh, 2, nw, 2, 4).mean((1, 3))
        elif h > 1:
            cur = cur.reshape(nh, 2, 1, 4).mean(1)
        else:
            cur = cur.reshape(1, nw, 2, 4).mean(2)
        n = cur[..., :3] / 127.5 - 1.0
        n /= np.maximum(np.linalg.norm(n, axis=-1, keepdims=True), 1e-5)
        cur = np.concatenate([(n + 1.0) * 127.5, cur[..., 3:]], -1)
        levels.append(np.clip(np.rint(cur), 0, 255).astype(np.uint8))
    return levels


def write_normal_vtf(path, img):
    """DXT5: xyz + gloss; 0x80 = normal map, 0x2000 = 8 bit alpha, 0x10 = anisotropic"""
    h, w = img.shape[:2]
    levels = normal_mips(img)
    thumb = np.array(Image.fromarray(levels[0][..., :3]).resize((16, 16), Image.BOX))
    header = struct.pack("<4sII", b"VTF\0", 7, 2)
    header += struct.pack("<I", 80)
    header += struct.pack("<HHIHH", w, h, 0x80 | 0x2000 | 0x10, 1, 0)
    header += b"\0" * 4
    header += struct.pack("<3f", 0.5, 0.5, 1.0)
    header += b"\0" * 4
    header += struct.pack("<f", 1.0)
    header += struct.pack("<iBiBB", 15, len(levels), 13, 16, 16)
    header += struct.pack("<H", 1)
    header += b"\0" * (80 - len(header))
    body = hd_textures.encode_dxt(np.concatenate([thumb, np.full((16, 16, 1), 255, np.uint8)], -1), False)
    for lv in reversed(levels):
        body += hd_textures.encode_dxt(lv, True)
    with open(path, "wb") as f:
        f.write(header + body)


def textures(name, work, cfg):
    out = os.path.join(MOD, "materials", *MAT.split("/"))
    os.makedirs(out, exist_ok=True)
    base = np.array(Image.open(os.path.join(work, name + "_base.png")).convert("RGB"))
    rough = np.array(Image.open(os.path.join(work, name + "_rough.png")).convert("L"))
    norm = np.array(Image.open(os.path.join(work, name + "_normal.png")).convert("RGB"))
    rgba = np.concatenate([base, np.full(base.shape[:2] + (1,), 255, np.uint8)], -1)
    hd_textures.write_vtf(os.path.join(out, name + ".vtf"), rgba, 0x10, False)
    gloss = 255 - rough
    write_normal_vtf(os.path.join(out, name + "_n.vtf"), np.dstack([norm, gloss]))
    vmt = '''"VertexLitGeneric"
{
	"$basetexture"		"%(mat)s/%(name)s"
	"$bumpmap"			"%(mat)s/%(name)s_n"
	"$surfaceprop"		"%(prop)s"

	// the deferred renderer: the gloss is in the alpha of the bump map
	"$pbr_roughness"	"0"
	"$pbr_metalness"	"%(metal).2f"
	"$pbr_specular"		"1"
	"$pbr_glossmask"	"1"

	// the stock renderer (-nodeferred)
	"$phong"			"1"
	"$phongexponent"	"%(phong)d"
	"$phongboost"		"%(boost).1f"
	"$phongalbedotint"	"1"
	"$phongfresnelranges"	"[0.7 0.85 1]"
}
''' % dict(mat=MAT, name=name, prop=cfg["prop"], metal=cfg["metal"], phong=cfg["phong"], boost=cfg["boost"])
    with open(os.path.join(out, name + ".vmt"), "w", encoding="ascii", newline="\n") as f:
        f.write(vmt)


def smd_bounds(path):
    lo = np.full(3, 1e9)
    hi = np.full(3, -1e9)
    with open(path) as f:
        for line in f:
            p = line.split()
            if len(p) == 9 and p[0] == "0":
                v = np.array([float(x) for x in p[1:4]])
                lo = np.minimum(lo, v)
                hi = np.maximum(hi, v)
    return lo, hi


def phys_smd(path, lo, hi, material):
    lo = lo.copy()
    hi = hi.copy()
    for i in range(3):
        if hi[i] - lo[i] < MIN_THICK:
            if i == 2:
                hi[i] = lo[i] + MIN_THICK       # on the table, upwards
            else:
                c = (lo[i] + hi[i]) * 0.5
                lo[i], hi[i] = c - MIN_THICK * 0.5, c + MIN_THICK * 0.5
    x0, y0, z0 = lo
    x1, y1, z1 = hi
    v = [(x0, y0, z0), (x1, y0, z0), (x1, y1, z0), (x0, y1, z0), (x0, y0, z1), (x1, y0, z1), (x1, y1, z1), (x0, y1, z1)]
    faces = [((3, 2, 1, 0), (0, 0, -1)), ((4, 5, 6, 7), (0, 0, 1)), ((0, 1, 5, 4), (0, -1, 0)), ((1, 2, 6, 5), (1, 0, 0)), ((2, 3, 7, 6), (0, 1, 0)), ((3, 0, 4, 7), (-1, 0, 0))]
    with open(path, "w", encoding="ascii", newline="\n") as f:
        f.write("version 1\nnodes\n0 \"root\" -1\nend\nskeleton\ntime 0\n0 0 0 0 0 0 0\nend\ntriangles\n")
        for (q, n) in faces:
            for tri in ((q[0], q[1], q[2]), (q[0], q[2], q[3])):
                f.write(material + "\n")
                for i in tri:
                    f.write("0 %.6f %.6f %.6f %d %d %d 0 0\n" % (*v[i], *n))
        f.write("end\n")


def compile_model(name, work, cfg):
    lo, hi = smd_bounds(os.path.join(work, name + ".smd"))
    print("  size, units: %.2f x %.2f x %.2f" % tuple(hi - lo))
    phys_smd(os.path.join(work, name + "_phys.smd"), lo, hi, name)
    with open(os.path.join(work, name + "_idle.smd"), "w", encoding="ascii", newline="\n") as f:
        f.write("version 1\nnodes\n0 \"root\" -1\nend\nskeleton\ntime 0\n0 0 0 0 0 0 0\nend\n")
    qc = '''$modelname "hl2rpm/items/%(name)s.mdl"
$body "body" "%(name)s.smd"
$staticprop
$surfaceprop "%(prop)s"
$cdmaterials "%(mat)s"
$sequence "idle" "%(name)s_idle.smd" fps 1
$collisionmodel "%(name)s_phys.smd"
{
	$mass %(mass).2f
	$damping 0.1
	$rotdamping 0.3
}
''' % dict(name=name, prop=cfg["prop"], mat=MAT, mass=cfg["mass"])
    qcp = os.path.join(work, name + ".qc")
    with open(qcp, "w", encoding="ascii", newline="\n") as f:
        f.write(qc)
    r = subprocess.run([os.path.join(SDK_BIN, "studiomdl.exe"), "-game", MOD, "-nop4", qcp], cwd=work, capture_output=True, text=True, errors="replace")
    txt = r.stdout + r.stderr
    bad = [l for l in txt.splitlines() if "ERROR" in l.upper() or "WARNING" in l.upper()]
    for l in bad[:12]:
        print("  " + l)
    print("  studiomdl exit %d" % r.returncode)
    if r.returncode != 0 or not os.path.exists(os.path.join(MOD, "models", "hl2rpm", "items", name + ".phy")):
        print(txt[-2500:])
        raise SystemExit("studiomdl failed on " + name)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("models", nargs="*")
    ap.add_argument("--no-bake", action="store_true", help="use the maps already baked")
    ap.add_argument("--no-compile", action="store_true")
    ap.add_argument("--render", action="store_true", help="a preview render of Blender as well")
    a = ap.parse_args()
    for name in a.models or list(MODELS):
        cfg = MODELS[name]
        work = os.path.join(SRC, name)
        os.makedirs(work, exist_ok=True)
        print("== " + name)
        stamps(work)
        flags = ([] if a.no_bake else ["bake"]) + (["render"] if a.render else [])
        if flags:
            run_blender(name, work, flags)
        if a.no_bake and a.no_compile:
            continue
        textures(name, work, cfg)
        if not a.no_compile and not cfg.get("part"):
            compile_model(name, work, cfg)


if __name__ == "__main__":
    main()
