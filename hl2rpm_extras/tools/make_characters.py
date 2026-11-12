"""Characters of the mod rebuilt from addon models (mdl_decompile.py -> QC edits -> studiomdl):

    hl2rpm/characters/hev_mk5.mdl          the HEV Mk V of the user (models/hev_npc.mdl) with the hard
                                           visor helmet by default, skin 0 = H.E.V., skin 1 = P.C.V. (olive)
    hl2rpm/characters/synth_elite.mdl      beta Combine units (DPFilms "Beta Combine Units Pack", MDL v49,
    hl2rpm/characters/synth_elite_green.mdl  ragdolls only) with the animations of the Combine soldier
    hl2rpm/characters/super_soldier_beta.mdl (npc_combine_s can use them); skins as in the pack
    hl2rpm/characters/cremator.mdl
    hl2rpm/props/combine_immolator.mdl     their weapons as props
    hl2rpm/props/combine_spear.mdl

    python make_characters.py [names...]

Sources go to modelsrc\\hl2rpm_characters\\<name> (SMD + QC, to edit and compile again).
"""
import os
import re
import subprocess
import sys

import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(HERE, "dialog_editor"))
import hd_textures  # noqa: E402
import import_gma  # noqa: E402
import mdl_decompile as dec  # noqa: E402
from dlgedit.world import gamefs  # noqa: E402

MOD = dec.MOD
SDK = dec.SDK
SRC = r"E:\XBLAH's modding tool\content\Mapbase\hl2rpm\modelsrc\hl2rpm_characters"
ADDONS = r"E:\SourceModding\HL2RPMSOURCE\Downloaded models"
BETA_GMA = os.path.join(ADDONS, "Combine beta units pack", "Beta_Combine_Units_Pack_beta_combine_units_pack_2014-08-31-0014.gma")
BETA_DIR = "models/dpfilms/evilgarlic/beta/"
PCV_MAT = "models/hl2rpm/characters/pcv"

COMBINE_ANIMS = '$includemodel "combine_soldier_anims.mdl"'

CHARS = {
    "hev_mk5": dict(src="models/hev_npc.mdl", out="hl2rpm/characters/hev_mk5", order={"Helmet": [1, 0]}, pcv=True),
    "synth_elite": dict(beta="elite_brown", out="hl2rpm/characters/synth_elite", extra=COMBINE_ANIMS),
    "synth_elite_green": dict(beta="elite_green", out="hl2rpm/characters/synth_elite_green", extra=COMBINE_ANIMS),
    # its own gun is a body group: the hand without it first, the NPC holds a real weapon
    "super_soldier_beta": dict(beta="synth_soldier", out="hl2rpm/characters/super_soldier_beta", extra=COMBINE_ANIMS, order={"gun": [1, 0]}),
    "cremator": dict(beta="cremator", out="hl2rpm/characters/cremator", extra=COMBINE_ANIMS),
    "combine_immolator": dict(beta="immolator", out="hl2rpm/props/combine_immolator", prop=True),
    "combine_spear": dict(beta="spear", out="hl2rpm/props/combine_spear", prop=True),
}


def beta_files():
    """the pack's model files (only those: its textures are imported into the mod as they are)"""
    _name, files = import_gma.gma_files(BETA_GMA)
    return {rel.lower(): data for rel, data in files}


def extract_beta(name, work):
    files = beta_files()
    os.makedirs(work, exist_ok=True)
    base = BETA_DIR + name
    for ext in (".mdl", ".vvd", ".dx90.vtx", ".phy"):
        d = files.get(base + ext)
        if d is not None:
            with open(os.path.join(work, "_src_" + name + ext), "wb") as f:
                f.write(d)
    return os.path.join(work, "_src_" + name + ".mdl")


def reorder_bodygroup(qc, group, order):
    m = re.search(r'\$bodygroup "%s"\n\{\n(.*?)\n\}' % re.escape(group), qc, re.S)
    if not m:
        return qc
    entries = m.group(1).split("\n")
    new = [entries[i] for i in order if i < len(entries)] + [e for i, e in enumerate(entries) if i not in order]
    return qc[:m.start(1)] + "\n".join(new) + qc[m.end(1):]


def repaint_olive(src_rel, dst_name):
    """the orange of the H.E.V. -> the olive of the P.C.V. (the P.C.V. of the mod is skin 1 of hev_mk5)"""
    import struct
    fs = gamefs.GameFS(MOD, SDK)
    data = fs.read(src_rel)
    tmp = os.path.join(SRC, "_tmp")
    os.makedirs(tmp, exist_ok=True)
    p = os.path.join(tmp, os.path.basename(src_rel))
    with open(p, "wb") as f:
        f.write(data)
    subprocess.run([os.path.join(SDK, "bin", "vtf2tga.exe"), "-i", p, "-o", p[:-4]], capture_output=True)
    a = np.array(Image.open(p[:-4] + ".tga").convert("RGBA")).astype(np.float32) / 255.0
    r, g, b = a[..., 0], a[..., 1], a[..., 2]
    orange = np.clip(((r - b) - 0.12) / 0.25, 0, 1) * np.clip(1.0 - np.abs((g + 1e-3) / (r + 1e-3) - 0.5) / 0.45, 0, 1)
    luma = 0.3 * r + 0.59 * g + 0.11 * b
    olive = np.stack([luma * 0.62, luma * 0.78, luma * 0.36], -1) * 1.05
    rgb = a[..., :3] * (1.0 - orange[..., None]) * 0.88 + olive * orange[..., None]
    rgba = (np.concatenate([np.clip(rgb, 0, 1), a[..., 3:]], -1) * 255 + 0.5).astype(np.uint8)
    out = os.path.join(MOD, "materials", *PCV_MAT.split("/"))
    os.makedirs(out, exist_ok=True)
    flags = struct.unpack_from("<I", data, 20)[0]
    hd_textures.write_vtf(os.path.join(out, dst_name + ".vtf"), rgba, flags & ~0x2000, True)
    return out


def make_pcv_materials():
    out = repaint_olive("materials/models/hev/hev_sheet.vtf", "pcv_sheet")
    repaint_olive("materials/models/hev/helmet_sheet.vtf", "pcv_helmet_sheet")
    vmts = {
        "pcv_sheet": '''"VertexLitGeneric"
{
	"$basetexture" "%s/pcv_sheet"
	"$bumpmap" "models/HEV/hev_normal"
	"$halflambert" "1"
	"$phong" "1"
	"$phongexponent" "25"
	"$phongboost" "2"
	"$phongfresnelranges" "[.2 0.4 1]"
}
''' % PCV_MAT,
        "pcv_helmet_sheet": '''"VertexLitGeneric"
{
	"$basetexture" "%s/pcv_helmet_sheet"
	"$bumpmap" "models/HEV/helmet_sheet_normal"
	"$halflambert" "1"
	"$phong" "1"
	"$phongexponent" "15"
	"$phongboost" "1.5"
	"$phongfresnelranges" "[0.25 0.5 1]"
}
''' % PCV_MAT,
    }
    for n, t in vmts.items():
        with open(os.path.join(out, n + ".vmt"), "w", newline="\n") as f:
            f.write(t)


def build(name, cfg):
    fs = gamefs.GameFS(MOD, SDK)
    work = os.path.join(SRC, name)
    os.makedirs(work, exist_ok=True)
    src = cfg.get("src") or extract_beta(cfg["beta"], work)
    qc_path, m = dec.decompile(fs, src, work, new_name=cfg["out"], anims=False,
                               ik_from="models/combine_soldier_anims.mdl" if cfg.get("extra") == COMBINE_ANIMS else None)
    qc = open(qc_path, encoding="ascii", errors="replace").read()
    for group, order in cfg.get("order", {}).items():
        qc = reorder_bodygroup(qc, group, order)
    if cfg.get("pcv"):
        make_pcv_materials()
        qc = qc.replace('$cdmaterials "models/HEV"', '$cdmaterials "models/HEV"\n$cdmaterials "%s"' % PCV_MAT)
        qc += '$texturegroup "skinfamilies"\n{\n\t{ "hev_sheet" "helmet_sheet" }\n\t{ "pcv_sheet" "pcv_helmet_sheet" }\n}\n'
    if cfg.get("prop"):
        qc = qc.replace("$collisionjoints", "$collisionmodel")
        if "$staticprop" not in qc and len(m.bone_names) <= 1:
            # a static prop has no bones: its hit box and attachments would name one that is gone
            qc = "\n".join(l for l in qc.split("\n") if not l.startswith(("$hbox", "$attachment")))
            qc += "\n$staticprop\n"
    if cfg.get("extra"):
        qc += cfg["extra"] + "\n"
    with open(qc_path, "w", encoding="ascii", newline="\n") as f:
        f.write(qc)
    rc, txt, bad = dec.compile_qc(qc_path)
    ok = rc == 0 and os.path.exists(os.path.join(MOD, "models", *(cfg["out"] + ".mdl").split("/")))
    print("%-20s %s  (%d bones, includes %s)" % (name, "ok" if ok else "FAILED", len(m.bone_names), m.includes or "-"))
    for line in bad[:10]:
        print("   ", line)
    if not ok:
        print(txt[-1500:])
    return ok


def main(argv):
    names = [a for a in argv if not a.startswith("-")] or list(CHARS)
    if any(CHARS[n].get("beta") for n in names):
        # the pack's textures (its models are rebuilt here: v49 does not load in this engine)
        import_gma.main([BETA_GMA, "--only", "materials/"])
    for n in names:
        build(n, CHARS[n])


if __name__ == "__main__":
    main(sys.argv[1:])
