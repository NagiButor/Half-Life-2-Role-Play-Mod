"""Port CS:GO MP7 (Combine logo + holographic sight) and HK USP Match (extended barrel,
barrel weight compensator, Match FCG, adjustable Match target sights) into HL2RPM,
completely replacing stock HL2 weapon_smg1 (v_smg1/w_smg1) and weapon_pistol (v_pistol/w_pistol),
with ValveBiped c_arms (uses_hands=1) and deferred PBR textures.

Usage:
    python make_csgo_weapons.py [--only mp7|usp] [--preview <dir>] [--fov 68]
"""
import argparse
import os
import shutil
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
if HERE not in sys.path:
    sys.path.insert(0, HERE)

from csgo_weapons import csgo_core as C
from csgo_weapons.mp7_builder import build_mp7
from csgo_weapons.usp_match_builder import build_usp_match

SOUND_BEGIN = "// === make_csgo_weapons.py BEGIN ==="
SOUND_END = "// === make_csgo_weapons.py END ==="


def update_sound_manifest(sound_blocks):
    path = os.path.join(C.MOD, "scripts", "game_sounds_weapons.txt")
    txt = open(path, encoding="utf-8", errors="replace").read()
    if SOUND_BEGIN in txt and SOUND_END in txt:
        i0 = txt.index(SOUND_BEGIN)
        i1 = txt.index(SOUND_END) + len(SOUND_END)
        txt = (txt[:i0].rstrip() + "\n" + txt[i1:].lstrip()).rstrip()
    section = "\n\n" + SOUND_BEGIN + "\n" + "\n\n".join(sound_blocks) + "\n" + SOUND_END + "\n"
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write(txt.rstrip() + section)
    print("Updated:", path)


def remove_old_stock_overrides():
    """Remove obsolete stock HL2 SMG1 & Pistol override textures and Mapbase hl2_c protagonist overrides."""
    base = os.path.join(C.MOD, "materials", "models", "weapons")
    for folder in ("v_smg1", "w_smg1", "v_pistol", "w_pistol"):
        p = os.path.join(base, folder)
        if os.path.isdir(p):
            shutil.rmtree(p)
            print("Removed old stock weapon override dir:", p)
    prot = os.path.join(C.MOD, "scripts", "protagonists", "protagonists_hl2_base.txt")
    if os.path.isfile(prot):
        lines = open(prot, encoding="utf-8", errors="replace").readlines()
        new_lines = [l for l in lines if "hl2_c/v_pistol.mdl" not in l.lower() and "hl2_c/v_smg1.mdl" not in l.lower()]
        if len(new_lines) != len(lines):
            with open(prot, "w", encoding="utf-8", newline="") as f:
                f.writelines(new_lines)
            print("Stripped stock hl2_c pistol/smg1 overrides from:", prot)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--only", choices=("mp7", "usp"), default=None)
    ap.add_argument("--preview", default=None, help="Directory for offline FP + 3D turntable previews")
    ap.add_argument("--fov", type=float, default=None, help="viewmodel_fov (default: the preset's, classic = 54)")
    ap.add_argument("--vm-preset", choices=sorted(C.VM_PRESETS), default="classic",
                    help="CS:GO viewmodel preset: classic (fov 54, x 2.5, y 0, z -1.5) or desktop (fov 60, x 1, y 1, z -1)")
    ap.add_argument("--keep-old", action="store_true", help="Do not delete old stock v_smg1/v_pistol material overrides")
    args = ap.parse_args()

    fs = C.gamefs.GameFS(C.MOD, C.SDK)
    v = C.vpk.VPK(C.CSGO_VPK)

    sound_blocks = []
    if args.only in (None, "mp7"):
        sound_blocks.append(build_mp7(v, fs, preview_dir=args.preview, fov=args.fov, vm_preset=args.vm_preset))
    if args.only in (None, "usp"):
        sound_blocks.append(build_usp_match(v, fs, preview_dir=args.preview, fov=args.fov, vm_preset=args.vm_preset))

    if args.only is None:
        update_sound_manifest(sound_blocks)
        if not args.keep_old:
            remove_old_stock_overrides()


if __name__ == "__main__":
    main()
