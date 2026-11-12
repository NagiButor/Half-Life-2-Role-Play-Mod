# -*- coding: utf-8 -*-
"""A classic headcrab zombie in a hazmat suit (the user, 2026-11-02: "the zombie for the hazmat
workers you will have to make yourself" - the addon has only a Zombine in it).

Made the way the addon authors made their zombies (the Combine worker zombie, the hazmat Zombine):
the suit is only the clothes - the head under the headcrab and the hands are the zombie's own.
(The first version kept the suit's helmet and hood - the helmet stuck out through the headcrab - and
its gloves; the user: the hands of a zombie are mutated limbs, meat and blood.)

The animations are the stock zombie's own, through $includemodel "zombie/classic.mdl" - with their
movement: sequences rebuilt from decompiled frames have none (mdl_decompile.py doesn't put the
piecewise movement back), and a zombie with a walk of speed 0 stood where it was.

    body part 0 "zombie":   the head, the clawed hands and the far third of the forearms (the wrist and the
                            heel of the hand are on the forearm bone; the flesh comes out of the sleeve)
                            of the stock zombie (models/zombie/classic.mdl, decompiled with
                            mdl_decompile.py) - its skeleton, hit boxes and ragdoll are the model's;
    body part 1 "headcrab1": the stock zombie's headcrab (npc_zombie switches body part 1 by number);
    body part 2 "suit":     the body of the addon's hazmat worker (models/hl2_combine_hazmat_cit.mdl)
                            without the helmet (only the neck seal ring it has stays, as on the addon's
                            Zombine), the hood and the gloves with their cuffs -
                            studiomdl joins the skeletons by the ValveBiped names, the suit is bound in
                            its own rest pose.

    One skin (the user: only the bloody one): the suit torn and bloody - hazmatbody_zombie of the addon,
    the Zombine's sheet on the belt.

    python make_zombie_hazmat.py

-> models/hl2rpm/characters/zombie_hazmat.mdl (npc_zombie; hl2rpm_zombies.cpp gives it to some of
   the zombies; a mapper or the Creation Kit can set it); sources in modelsrc\\hl2rpm_characters.
"""
import math
import os
import re
import shutil
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(HERE, "dialog_editor"))
import mdl_decompile as dec  # noqa: E402
from dlgedit.world import gamefs  # noqa: E402

SRC = r"E:\XBLAH's modding tool\content\Mapbase\hl2rpm\modelsrc\hl2rpm_characters"
OUT = "hl2rpm/characters/zombie_hazmat"

# the stock zombie has no neck or head bones: its head is a part of Spine4 - the vertices above the
# collar, as wide as the head (the collar flaps of the shirt are wider)
HEAD_MIN_Z = 59.5
HEAD_HALF_WIDTH = 5.0
# the zombie's forearm farther than this from the elbow (units along the bone; the hand bone is at 12.1,
# the suit's sleeve ends at ~11.3) - it starts inside the sleeve
FOREARM_FROM = 8.5


def _euler_matrix(rx, ry, rz):
    """an SMD bone rotation (radians, applied x, y, z) as a 4x4"""
    cx, sx, cy, sy, cz, sz = math.cos(rx), math.sin(rx), math.cos(ry), math.sin(ry), math.cos(rz), math.sin(rz)
    return [[cy * cz, sx * sy * cz - cx * sz, cx * sy * cz + sx * sz, 0],
            [cy * sz, sx * sy * sz + cx * cz, cx * sy * sz - sx * cz, 0],
            [-sy, sx * cy, cx * cy, 0],
            [0, 0, 0, 1]]


def _mul(a, b):
    return [[sum(a[r][k] * b[k][c] for k in range(4)) for c in range(4)] for r in range(4)]


def _along(m, p):
    """how far point p is along the x axis (the length) of a bone with world matrix m"""
    d = [p[k] - m[k][3] for k in range(3)]
    return sum(d[k] * m[k][0] for k in range(3))


class Smd:
    """a reference SMD as text: the header (nodes + skeleton) and the triangles"""

    def __init__(self, path):
        lines = open(path, encoding="ascii", errors="replace").read().split("\n")
        t = lines.index("triangles")
        self.head = lines[:t]
        self.names, self.parent = {}, {}
        for ln in lines[lines.index("nodes") + 1:lines.index("end")]:
            m = re.match(r'\s*(-?\d+)\s+"([^"]*)"\s+(-?\d+)', ln)
            self.names[int(m.group(1))] = m.group(2)
            self.parent[int(m.group(1))] = int(m.group(3))
        # the rest pose: bone -> world matrix
        sk = lines.index("skeleton")
        local = {}
        for ln in lines[sk + 2:]:
            if ln == "end":
                break
            v = ln.split()
            mt = _euler_matrix(*[float(x) for x in v[4:7]])
            for k in range(3):
                mt[k][3] = float(v[1 + k])
            local[int(v[0])] = mt
        self.world = {}
        for i in sorted(self.names):
            par = self.parent[i]
            self.world[i] = _mul(self.world[par], local[i]) if par >= 0 else local[i]
        self.index = {n: i for i, n in self.names.items()}
        self.tris = []
        j = t + 1
        while j + 3 < len(lines) and lines[j] != "end":
            self.tris.append((lines[j], lines[j + 1:j + 4]))
            j += 4

    def bone(self, vline):
        """the bone with the most weight of a vertex line"""
        v = vline.split()
        if len(v) > 10 and int(v[9]) > 0:
            links = [(float(v[11 + 2 * k]), int(v[10 + 2 * k])) for k in range(int(v[9]))]
            return self.names[max(links)[1]]
        return self.names[int(v[0])]

    def bones(self, verts):
        return [self.bone(v) for v in verts]

    def write(self, path, tris):
        with open(path, "w", encoding="ascii", newline="\n") as f:
            f.write("\n".join(self.head) + "\ntriangles\n")
            for mat, verts in tris:
                f.write(mat + "\n" + "\n".join(verts) + "\n")
            f.write("end\n")


def is_hand(bone):
    return "_Hand" in bone or "_Finger" in bone


def zombie_parts(z):
    """the head, the hands and the far part of the forearms of the stock zombie"""
    out = []
    forearm = {side: z.world[z.index["ValveBiped.Bip01_%s_Forearm" % side]] for side in "LR"}
    for mat, verts in z.tris:
        bones = z.bones(verts)
        pos = [[float(x) for x in v.split()[1:4]] for v in verts]
        side = "L" if "_L_" in bones[0] else "R"
        arm = all(("_%s_" % side) in b and (is_hand(b) or b.endswith("_Forearm")) for b in bones)
        if arm and all(_along(forearm[side], q) > FOREARM_FROM for q in pos):
            out.append((mat, verts))
            continue
        if all(b.endswith("Spine4") for b in bones) and all(q[2] > HEAD_MIN_Z and abs(q[0]) < HEAD_HALF_WIDTH for q in pos):
            out.append((mat, verts))
    return out


def suit_parts(s):
    """the suit without the helmet (the neck seal ring stays), the hood and the gloves; the materials of the dead"""
    out = []
    dropped = {"helmet": 0, "gloves": 0}
    for mat, verts in s.tris:
        bones = s.bones(verts)
        # (the ring below the helmet - 96 triangles at the collar, on the spine and the clavicle - stays)
        if mat == "cmbworker_helmet" and any(b.endswith(("Head1", "Neck1", "Spine4")) for b in bones):
            dropped["helmet"] += 1
            continue
        # (the gloves and the cuffs of the gloves - the cuffs are on the Zombine's sheet: blue patches at the wrist)
        if any(is_hand(b) for b in bones) or (mat == "combinesoldiersheet" and any(b.endswith("_Forearm") for b in bones)):
            dropped["gloves"] += 1
            continue
        mat = {"hazmatbody": "hazmatbody_zombie", "combinesoldiersheet": "combinesoldiersheet_zombie"}.get(mat, mat)
        out.append((mat, verts))
    return out, dropped


def main():
    fs = gamefs.GameFS(dec.MOD, dec.SDK)
    work = os.path.join(SRC, "zombie_hazmat")
    tmp = os.path.join(work, "_hazmat_src")
    os.makedirs(tmp, exist_ok=True)
    # (the first version's hood and its animations rebuilt from frames)
    for old in os.listdir(work) if os.path.isdir(work) else []:
        if old == "hazmat_hood.smd" or ("_anim_" in old and not old.endswith("_anim_ragdoll.smd")):
            os.remove(os.path.join(work, old))
    # the suit
    dec.decompile(fs, "models/hl2_combine_hazmat_cit.mdl", tmp, new_name="hazmat_src", anims=False)
    # the zombie
    # (only the ragdoll pose of its own: the sequences come from the stock model, with their movement,
    # events, activities and IK; the IK chains are written by the decompiler in the stock order)
    qc_path, m = dec.decompile(fs, "models/zombie/classic.mdl", work, new_name=OUT, anims=False,
                               extra_qc='$includemodel "zombie/classic.mdl"')
    qc = open(qc_path, encoding="ascii", errors="replace").read()
    body = re.search(r'^\$body "studio" "([^"]+)"$', qc, re.M)
    if not body:
        raise SystemExit("no studio body in the zombie's QC:\n" + qc[:800])
    z = Smd(os.path.join(work, body.group(1)))
    zt = zombie_parts(z)
    z.write(os.path.join(work, "zombie_parts.smd"), zt)
    s = Smd(os.path.join(tmp, "hazmat_src_Soldier_0.smd"))
    st, dropped = suit_parts(s)
    s.write(os.path.join(work, "hazmat_body.smd"), st)
    print("zombie: %d triangles (head, hands); suit: %d (dropped: helmet %d, gloves %d)" % (len(zt), len(st), dropped["helmet"], dropped["gloves"]))
    # (the headcrab must stay body part 1: npc_zombie switches it by that number - the suit goes after it)
    qc = qc.replace(body.group(0), '$body "zombie" "zombie_parts.smd"')
    hc = re.search(r'\$bodygroup "headcrab1"\n\{.*?\n\}', qc, re.S)
    if not hc:
        raise SystemExit("no headcrab body group in the zombie's QC")
    qc = qc.replace(hc.group(0), hc.group(0) + '\n$body "suit" "hazmat_body.smd"')
    qc = qc.replace('$cdmaterials "models/zombie_classic"', '$cdmaterials "models/zombie_classic"\n$cdmaterials "models/hl2_hazmat"\n$cdmaterials "models/combine_soldier"')
    with open(qc_path, "w", encoding="ascii", newline="\n") as f:
        f.write(qc)
    rc, txt, bad = dec.compile_qc(qc_path)
    ok = rc == 0 and os.path.exists(os.path.join(dec.MOD, "models", *(OUT + ".mdl").split("/")))
    print("zombie_hazmat %s (%d bones of the zombie)" % ("ok" if ok else "FAILED", len(m.bone_names)))
    for line in bad[:10]:
        print("   ", line)
    if not ok:
        print(txt[-2000:])
    shutil.rmtree(tmp, ignore_errors=True)


if __name__ == "__main__":
    main()
