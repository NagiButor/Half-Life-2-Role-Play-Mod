"""The melee weapons of the user's "HL2 Melee Pack" (Garry's Mod) as weapons of the mod:

    python make_melee_weapons.py [axe bottle brokenbottle hook pan pickaxe pipe pot shovel] [--no-upscale] [--no-subdiv]

For each weapon:
- the pack's v_ / w_ models are taken apart (mdl_decompile.py): skeleton, mesh, animations;
- the mesh gets more polygons (Blender, item_models/blender_subdiv.py: Catmull-Clark, hard edges kept);
- its textures are upscaled (Real-ESRGAN with the detail transfer of hd_textures.py) into
  materials/models/weapons/hl2rpm_melee/<weapon>/ (the stock textures the pack uses stay untouched);
- the view model is compiled again with the activities the mod's melee code plays: idle, draw,
  holster (the draw played backwards), miss / hit (the pack's swing), secondary miss / hit (its
  shove), and $bonemerge on the arm bones: the hands are a separate model bone-merged onto it
  (models/hl2rpm/hands/*, make_hands.py; the client: hl2rpm_hands.cpp);
- the world model is compiled again (its R_Hand bone: NPCs hold it on theirs);
- scripts/weapon_rpm_<weapon>.txt.

Results: models/weapons/hl2rpm/{v,w}_<weapon>.mdl; sources in modelsrc\\hl2rpm_melee\\<weapon>.
The game code: game/server/hl2rpm_melee.cpp (one class, a table of the weapons).
"""
import os
import shutil
import subprocess
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(HERE, "dialog_editor"))
import hd_textures  # noqa: E402
import import_gma  # noqa: E402
import mdl_decompile as dec  # noqa: E402
from make_hands import VIEW_BONES  # noqa: E402
from dlgedit.world import gamefs  # noqa: E402

MOD = dec.MOD
SRC = r"E:\XBLAH's modding tool\content\Mapbase\hl2rpm\modelsrc\hl2rpm_melee"
PACK = r"E:\SourceModding\HL2RPMSOURCE\Downloaded models\HL2 Melee Pack\Half-Life_2_Melee_Pack_1465467710_1465703167_2016-06-09-1322.gma"
BLENDER = r"C:\Program Files\Blender Foundation\Blender 5.1\blender.exe"
MAT = "models/weapons/hl2rpm_melee"

# name: subdivision levels (the pack's meshes: from ~60 to ~650 triangles), damage type, swing / hit sounds
WEAPONS = {
    "axe": dict(levels=2, en="Axe", ru="Топор", world="Canister.ImpactHard", body="Weapon_Crowbar.Melee_Hit"),
    "bottle": dict(levels=1, en="Bottle", ru="Бутылка", world="GlassBottle.ImpactHard", body="GlassBottle.ImpactHard"),
    "brokenbottle": dict(levels=1, en="Broken bottle", ru="Розочка", world="GlassBottle.ImpactHard", body="Flesh_Bloody.ImpactHard"),
    "hook": dict(levels=2, en="Hook", ru="Крюк", world="Canister.ImpactHard", body="Flesh.ImpactHard"),
    "pan": dict(levels=1, en="Frying pan", ru="Сковорода", world="Metal_Box.ImpactHard", body="Flesh.ImpactHard"),
    "pickaxe": dict(levels=1, en="Pickaxe", ru="Кирка", world="Canister.ImpactHard", body="Flesh.ImpactHard"),
    # (the pack has no w_pipe: its Garry's Mod script holds Matt's pipe of HL2, the same hand bone)
    "pipe": dict(levels=2, en="Pipe", ru="Труба", world="Canister.ImpactHard", body="Flesh.ImpactHard",
                 world_src="models/props_canal/mattpipe.mdl"),
    "pot": dict(levels=1, en="Pot", ru="Кастрюля", world="Metal_Box.ImpactHard", body="Flesh.ImpactHard"),
    "shovel": dict(levels=2, en="Shovel", ru="Лопата", world="Canister.ImpactHard", body="Flesh.ImpactHard"),
}


def pack_files():
    _n, files = import_gma.gma_files(PACK)
    return {rel.lower(): data for rel, data in files}


def extract(files, base, work):
    """the pack's model files into the work folder -> path of the .mdl"""
    for ext in (".mdl", ".vvd", ".dx90.vtx", ".phy"):
        d = files.get("models/weapons/hl2meleepack/" + base + ext)
        if d is not None:
            with open(os.path.join(work, "_src_" + base + ext), "wb") as f:
                f.write(d)
    return os.path.join(work, "_src_" + base + ".mdl")


def subdivide(smd, levels):
    out = smd[:-4] + "_hi.smd"
    r = subprocess.run([BLENDER, "--background", "--factory-startup", "--python", os.path.join(HERE, "item_models", "blender_subdiv.py"),
                        "--", smd, out, str(levels), "35"], capture_output=True, text=True, errors="replace")
    line = [l for l in r.stdout.splitlines() if l.startswith("SUBDIV")]
    if r.returncode != 0 or not os.path.exists(out):
        print(r.stdout[-1500:], r.stderr[-1500:])
        raise SystemExit("blender failed on " + smd)
    print("   ", line[0] if line else "subdivided")
    return out


def material_name(tex):
    return os.path.basename(tex.replace("\\", "/")).lower()


def make_materials(fs, name, textures, cds, upscale_on):
    """each texture of the model -> materials/models/weapons/hl2rpm_melee/<name>/<texture>: an upscaled
    copy of its base texture, the VMT with the original's parameters"""
    out_dir = os.path.join(MOD, "materials", *MAT.split("/"), name)
    os.makedirs(out_dir, exist_ok=True)
    for tex in textures:
        tname = material_name(tex)
        vmt_rel = None
        for cd in cds or [""]:
            cand = ("materials/%s/%s.vmt" % (cd.strip("/\\").replace("\\", "/"), tex)).replace("//", "/").lower()
            if fs.exists(cand):
                vmt_rel = cand
                break
        if not vmt_rel:
            print("    no material", tex, cds)
            continue
        vmt = fs.read(vmt_rel).decode("utf-8", "replace")
        import re
        base = re.search(r'"?\$basetexture"?\s+"([^"]+)"', vmt, re.I)
        base_tex = base.group(1).replace("\\", "/").lower() if base else ""
        new_base = "%s/%s/%s" % (MAT, name, tname)
        dst_vtf = os.path.join(out_dir, tname + ".vtf")
        if base_tex and upscale_on and not os.path.exists(dst_vtf):
            tmp = os.path.join(SRC, "_hd")
            jobs = hd_textures.upscale([base_tex], force=True, out_root=tmp, min_size=16)
            src_up = os.path.join(tmp, base_tex.replace("/", os.sep) + ".vtf")
            if jobs and os.path.exists(src_up):
                shutil.copy(src_up, dst_vtf)
        if not os.path.exists(dst_vtf) and base_tex:
            # (not upscaled: bigger than 512, or --no-upscale) - the original
            d = fs.read("materials/%s.vtf" % base_tex)
            if d:
                with open(dst_vtf, "wb") as f:
                    f.write(d)
        vmt = re.sub(r'("?\$basetexture"?\s+)"[^"]+"', r'\1"%s"' % new_base, vmt, flags=re.I)
        if "$phong" not in vmt.lower() and "unlit" not in vmt.lower():
            vmt = vmt.rstrip().rstrip("}") + '\t"$phong" "1"\n\t"$phongexponent" "18"\n\t"$phongboost" "0.6"\n\t"$phongfresnelranges" "[0.4 0.8 1]"\n}\n'
        with open(os.path.join(out_dir, tname + ".vmt"), "w", encoding="utf-8", newline="\n") as f:
            f.write("// make_melee_weapons.py: %s (texture upscaled)\n" % vmt_rel + vmt)


def anim_smd(m, frames, path, reverse=False):
    with open(path, "w", encoding="ascii", newline="\n") as f:
        f.write(dec.smd_skeleton(m, list(reversed(frames)) if reverse else frames))


def build(name, cfg, files, upscale_on, subdiv_on):
    fs = gamefs.GameFS(MOD, dec.SDK)
    work = os.path.join(SRC, name)
    os.makedirs(work, exist_ok=True)
    print("== " + name)
    # ---------------- the view model
    vm = dec.Model(fs, extract(files, "v_" + name, work))
    meshes = []
    for bname, _b, models in vm.bodyparts():
        for mname, ms in models[:1]:
            meshes += ms
    ref = os.path.join(work, "v_%s_ref.smd" % name)
    with open(ref, "w", encoding="ascii", newline="\n") as f:
        f.write(dec.smd_skeleton(vm) + dec.mesh_triangles(vm, meshes, material_name))
    ref_hi = subdivide(ref, cfg["levels"]) if subdiv_on else ref
    seqs = {s["name"].lower(): s for s in vm.sequences()}
    sm = {}
    for key, s in seqs.items():
        frames = dec.sample_frames(fs, vm, s)
        p = os.path.join(work, "v_%s_%s.smd" % (name, key))
        anim_smd(vm, frames, p)
        sm[key] = (os.path.basename(p), s["fps"])
        if key == "draw":
            p2 = os.path.join(work, "v_%s_holster.smd" % name)
            anim_smd(vm, frames, p2, reverse=True)
            sm["holster"] = (os.path.basename(p2), s["fps"] * 1.4)
    swing = sm.get("misscenter1") or sm.get(next(iter(sm)))
    shove = sm.get("pushback") or swing
    idle = sm.get("idle01") or sm.get("idle") or swing
    lowb = [b.lower() for b in vm.bone_names]
    qc = ['$modelname "weapons/hl2rpm/v_%s.mdl"' % name, "$origin 0 0 0 -90",
          '$body "weapon" "%s"' % os.path.basename(ref_hi), '$cdmaterials "%s/%s"' % (MAT, name)]
    qc += ['$bonemerge "%s"' % vm.bone_names[lowb.index(b.lower())] for b in VIEW_BONES if b.lower() in lowb]
    seq = lambda n, smd, act, fps, extra="": '$sequence "%s" "%s" fps %.2f %s activity %s 1' % (n, smd, fps, extra, act)
    qc.append(seq("idle", idle[0], "ACT_VM_IDLE", idle[1], "loop"))
    if "draw" in sm:
        qc.append(seq("draw", sm["draw"][0], "ACT_VM_DRAW", sm["draw"][1]))
        qc.append(seq("holster", sm["holster"][0], "ACT_VM_HOLSTER", sm["holster"][1]))
    qc.append(seq("misscenter1", swing[0], "ACT_VM_MISSCENTER", swing[1]))
    qc.append(seq("hitcenter1", swing[0], "ACT_VM_HITCENTER", swing[1]))
    qc.append(seq("misscenter2", shove[0], "ACT_VM_MISSCENTER2", shove[1]))
    qc.append(seq("hitcenter2", shove[0], "ACT_VM_HITCENTER2", shove[1]))
    qc_path = os.path.join(work, "v_%s.qc" % name)
    with open(qc_path, "w", encoding="ascii", newline="\n") as f:
        f.write("// make_melee_weapons.py: the view model of the %s\n" % name + "\n".join(qc) + "\n")
    rc, txt, bad = dec.compile_qc(qc_path)
    print("    v_%s: %s" % (name, "ok" if rc == 0 else "FAILED " + " ".join(bad[:3])))
    # ---------------- the world model
    wm = dec.Model(fs, cfg.get("world_src") or extract(files, "w_" + name, work))
    wmeshes = []
    for bname, _b, models in wm.bodyparts():
        for mname, ms in models[:1]:
            wmeshes += ms
    wref = os.path.join(work, "w_%s_ref.smd" % name)
    with open(wref, "w", encoding="ascii", newline="\n") as f:
        f.write(dec.smd_skeleton(wm) + dec.mesh_triangles(wm, wmeshes, material_name))
    wref_hi = subdivide(wref, cfg["levels"]) if subdiv_on else wref
    wphys = os.path.join(work, "w_%s_phys.smd" % name)
    with open(wphys, "w", encoding="ascii", newline="\n") as f:
        f.write(dec.smd_skeleton(wm) + dec.phys_triangles(wm, wmeshes, [wm.bone_names[0]]))
    with open(os.path.join(work, "w_%s_idle.smd" % name), "w", encoding="ascii", newline="\n") as f:
        f.write(dec.smd_skeleton(wm))
    wqc = ['$modelname "weapons/hl2rpm/w_%s.mdl"' % name, "$origin 0 0 0 -90",
           '$body "weapon" "%s"' % os.path.basename(wref_hi), '$cdmaterials "%s/%s"' % (MAT, name),
           '$surfaceprop "%s"' % ("glassbottle" if "bottle" in name else "metal"),
           '$sequence "idle" "w_%s_idle.smd" fps 30' % name,
           '$collisionmodel "w_%s_phys.smd"\n{\n\t$mass 2\n}' % name]
    wqc_path = os.path.join(work, "w_%s.qc" % name)
    with open(wqc_path, "w", encoding="ascii", newline="\n") as f:
        f.write("// make_melee_weapons.py: the world model of the %s\n" % name + "\n".join(wqc) + "\n")
    rc2, txt2, bad2 = dec.compile_qc(wqc_path)
    print("    w_%s: %s" % (name, "ok" if rc2 == 0 else "FAILED " + " ".join(bad2[:3])))
    # ---------------- textures
    make_materials(fs, name, sorted(set(vm.textures) | set(wm.textures)), list(vm.cdmaterials) + list(wm.cdmaterials), upscale_on)
    # ---------------- the weapon script
    script = '''// make_melee_weapons.py: the {en} (HL2 Melee Pack) - game/server/hl2rpm_melee.cpp
WeaponData
{{
	"printname"		"{en}"
	"viewmodel"		"models/weapons/hl2rpm/v_{n}.mdl"
	"playermodel"	"models/weapons/hl2rpm/w_{n}.mdl"
	"anim_prefix"	"crowbar"
	"bucket"		"0"
	"bucket_position"	"1"
	"viewmodel_fov"	"67"
	"uses_hands"	"1"
	"clip_size"		"-1"
	"primary_ammo"	"None"
	"secondary_ammo"	"None"
	"weight"		"0"
	"item_flags"	"0"
	"SoundData"
	{{
		"single_shot"		"Weapon_Crowbar.Single"
		"melee_miss"		"Weapon_Crowbar.Single"
		"melee_hit"			"{body}"
		"melee_hit_world"	"{world}"
	}}
	TextureData
	{{
		"weapon"			{{ "font" "WeaponIcons" "character" "c" }}
		"weapon_s"			{{ "font" "WeaponIconsSelected" "character" "c" }}
		"ammo"				{{ "font" "WeaponIcons" "character" "c" }}
		"crosshair"			{{ "font" "Crosshairs" "character" "Q" }}
		"autoaim"			{{ "file" "sprites/crosshairs" "x" "0" "y" "48" "width" "24" "height" "24" }}
	}}
}}
'''.format(n=name, en=cfg["en"], body=cfg["body"], world=cfg["world"])
    with open(os.path.join(MOD, "scripts", "weapon_rpm_%s.txt" % name), "w", encoding="ascii", newline="\n") as f:
        f.write(script)


def main(argv):
    names = [a for a in argv if not a.startswith("-")] or list(WEAPONS)
    files = pack_files()
    # the pack's own textures (axe, pickaxe) into the mod: the upscale reads them there
    import_gma.main([PACK, "--only", "materials/"])
    for n in names:
        build(n, WEAPONS[n], files, "--no-upscale" not in argv, "--no-subdiv" not in argv)


if __name__ == "__main__":
    main(sys.argv[1:])
