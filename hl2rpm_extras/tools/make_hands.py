"""First-person hands for the weapons that have none in their view model (the melee weapons of the
"HL2 Melee Pack": its v_ models are made for Garry's Mod hands, a separate model bone-merged onto
the view model's ValveBiped arm bones).

The hands are cut out of the body the player wears: the triangles of the upper arms, forearms,
hands and fingers of the character model, with its textures and skins - so the hands match the
suit (citizen shirt, H.E.V., Civil Protection gloves...). The client draws them bone-merged onto
the view model (hl2rpm_hands.cpp).

    python make_hands.py [citizen hev cp soldier prison elite worker hazmat]

-> models/hl2rpm/hands/<name>.mdl; sources in modelsrc\\hl2rpm_hands\\<name>.
"""
import os
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(HERE, "dialog_editor"))
import mdl_decompile as dec  # noqa: E402
from dlgedit.world import gamefs  # noqa: E402

SRC = r"E:\XBLAH's modding tool\content\Mapbase\hl2rpm\modelsrc\hl2rpm_hands"

HANDS = {
    "citizen": "models/humans/group01/male_07.mdl",
    "hev": "models/hl2rpm/characters/hev_mk5.mdl",        # skin 1 = the P.C.V.
    "cp": "models/police.mdl",
    "soldier": "models/combine_soldier.mdl",
    "prison": "models/combine_soldier_prisonguard.mdl",
    "elite": "models/combine_super_soldier.mdl",
    # (2026-11-02: the work suits - the bodies they put on the player, NPC models of the addons)
    "worker": "models/puntleycmb/workernpc.mdl",
    "hazmat": "models/hl2_combine_hazmat_cit.mdl",
}

# the bones of the view models (ValveBiped arms of the Garry's Mod hands rig)
ARM_PARTS = ("UpperArm", "Forearm", "Hand", "Finger")
VIEW_BONES = ["ValveBiped.Bip01_Spine4"] + ["ValveBiped.Bip01_%s_%s" % (s, b) for s in "LR" for b in ("Clavicle", "UpperArm", "Forearm", "Hand")] + \
    ["ValveBiped.Bip01_%s_Finger%d%s" % (s, f, k) for s in "LR" for f in range(5) for k in ("", "1", "2")]


def build(name, src):
    fs = gamefs.GameFS(dec.MOD, dec.SDK)
    m = dec.Model(fs, src)
    low = [b.lower() for b in m.bone_names]
    view = {b.lower() for b in VIEW_BONES}
    # each bone -> the nearest one up its chain that the view model has (Ulna, Wrist... -> Forearm)
    target = []
    for i in range(len(m.bone_names)):
        j = i
        while j >= 0 and low[j] not in view:
            j = m.bone_parent[j]
        target.append(j)
    is_arm = [j >= 0 and any(p.lower() in low[j] for p in ARM_PARTS) for j in target]
    meshes = []
    for bname, _base, models in m.bodyparts():
        if not models or not models[0][1]:
            continue
        # body parts: the first model of each (a helmet, hands variants...) - the hands variant 0
        for (skinref, P, N, U, I, W, B) in models[0][1]:
            dom = B[np.arange(len(B)), np.argmax(W, 1)]
            arm_v = np.array([is_arm[int(b)] for b in dom])
            tris = I.reshape(-1, 3)
            keep = arm_v[tris].all(1)
            if not keep.any():
                continue
            # weights onto the view model's bones
            W2, B2 = W.copy(), B.copy()
            for v in range(len(B2)):
                for k in range(3):
                    t = target[int(B2[v][k])]
                    if t < 0:
                        W2[v][k] = 0
                    else:
                        B2[v][k] = t
            meshes.append((skinref, P, N, U, tris[keep].reshape(-1), W2, B2))
    work = os.path.join(SRC, name)
    os.makedirs(work, exist_ok=True)
    skel = dec.smd_skeleton(m)
    with open(os.path.join(work, name + "_hands.smd"), "w", encoding="ascii", newline="\n") as f:
        f.write(skel + dec.mesh_triangles(m, meshes))
    with open(os.path.join(work, name + "_idle.smd"), "w", encoding="ascii", newline="\n") as f:
        f.write(skel)
    qc = ['$modelname "hl2rpm/hands/%s.mdl"' % name, "$origin 0 0 0 -90", '$body "hands" "%s_hands.smd"' % name]
    for cd in m.cdmaterials:
        qc.append('$cdmaterials "%s"' % cd.strip("/"))
    if len(m.skins) > 1:
        cols = [c for c in range(len(m.skins[0])) if len({fam[c] for fam in m.skins}) > 1]
        if cols:
            qc.append('$texturegroup "skinfamilies"\n{\n%s\n}' % "\n".join(
                "\t{ %s }" % " ".join('"%s"' % m.textures[fam[c]] for c in cols) for fam in m.skins))
    for b in VIEW_BONES:
        if b.lower() in low:
            qc.append('$bonemerge "%s"' % m.bone_names[low.index(b.lower())])
    qc.append('$sequence "idle" "%s_idle.smd" fps 30' % name)
    qc_path = os.path.join(work, name + ".qc")
    with open(qc_path, "w", encoding="ascii", newline="\n") as f:
        f.write("// make_hands.py: the arms of %s\n" % src + "\n".join(qc) + "\n")
    rc, txt, bad = dec.compile_qc(qc_path)
    ntri = sum(len(x[4]) // 3 for x in meshes)
    print("%-8s %s  %d triangles from %s" % (name, "ok" if rc == 0 else "FAILED", ntri, src))
    for line in bad[:6]:
        print("   ", line)


def main(argv):
    for n in (argv or list(HANDS)):
        build(n, HANDS[n])


if __name__ == "__main__":
    main(sys.argv[1:])
