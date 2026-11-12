"""Studio models (.mdl v44-49 + .vvd + .dx90.vtx + .phy) back into SMD + QC, to compile again with
studiomdl (no Crowbar here): when an addon model needs other animations ($includemodel), other
textures, more skins, more polygons or a hand model.

    python mdl_decompile.py <game path of the .mdl | file path> <out dir> [--no-anims] [--name <new name>]

What it rebuilds:
- the skeleton (names as written, parents, reference pose) -> "nodes" / "skeleton" of every SMD;
- every model of every body part (LOD 0; vertices in the reference pose, up to 3 weights,
  normals, UVs; material = the texture of skin family 0) -> <name>_<bodypart>_<model>.smd;
- skins -> $texturegroup, body parts -> $bodygroup (with "blank"), $cdmaterials, $surfaceprop,
  $eyeposition, $illumposition, $attachment (bone, offset, rotation), $hbox, $includemodel;
- the model's own sequences (the first animation of each; fps, loop, activity, events) ->
  <name>_anim_<sequence>.smd; sequences of included models are not copied (they come with them);
- .phy: the text part (solid masses, ragdoll constraints, self collisions) -> $collisionjoints
  with $jointconstrain for a ragdoll; one solid -> $collisionmodel. The hull of each physics bone is
  built by studiomdl from the reference vertices bound to it (<name>_phys.smd).

Not rebuilt: flexes (facial animation), LODs, IK chains, pose parameters, procedural bones.
Used by import_characters.py / make_melee_weapons.py; the result is meant to be compiled again.
"""
import math
import os
import re
import struct
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "dialog_editor"))
from dlgedit.world import anim, gamefs  # noqa: E402
from dlgedit.world.mdl import _vvd_vertices  # noqa: E402

MOD = r"E:\Steam\steamapps\sourcemods\hl2rpm"
SDK = r"E:\Steam\steamapps\common\Source SDK Base 2013 Singleplayer"


def cstr(d, o):
    if o <= 0 or o >= len(d):
        return ""
    e = d.find(b"\0", o)
    return d[o:e if e >= 0 else len(d)].decode("utf-8", "replace")


# ------------------------------------------------------------------ angles
def matrix_angles(m):
    """3x3 -> QAngle (pitch, yaw, roll) in degrees, as mathlib's MatrixAngles"""
    fwd = (m[0][0], m[1][0], m[2][0])
    left = (m[0][1], m[1][1], m[2][1])
    up = m[2][2]
    xy = math.sqrt(fwd[0] ** 2 + fwd[1] ** 2)
    if xy > 0.001:
        yaw = math.atan2(fwd[1], fwd[0])
        pitch = math.atan2(-fwd[2], xy)
        roll = math.atan2(left[2], up)
    else:
        yaw = math.atan2(-left[0], left[1])
        pitch = math.atan2(-fwd[2], xy)
        roll = 0.0
    return math.degrees(pitch), math.degrees(yaw), math.degrees(roll)


def quat_radian_euler(q):
    """quaternion -> RadianEuler (x = roll, y = pitch, z = yaw), what SMD skeletons hold"""
    m = anim._quat_matrix(np.asarray(q, np.float64))
    p, y, r = matrix_angles(m)
    return math.radians(r), math.radians(p), math.radians(y)


def strip_group_size(vtx, mesh0):
    """25 bytes in the VTX of this engine, 33 in newer ones (CS:GO / SFM compilers add topology
    fields): the one whose first strip group points at sane data"""
    nsg, sgofs, _f = struct.unpack_from("<iiB", vtx, mesh0)
    if nsg <= 1:
        return 25
    for size in (25, 33):
        sgp = mesh0 + sgofs + size
        if sgp + 25 > len(vtx):
            continue
        nv, vofs, ni, iofs = struct.unpack_from("<iiii", vtx, sgp)
        if 0 < nv < 65536 and 0 < ni < 1 << 20 and 0 < sgp + vofs < len(vtx) and 0 < sgp + iofs < len(vtx) and ni % 3 == 0:
            return size
    return 25


# ------------------------------------------------------------------ the model
class Model:
    def __init__(self, fs, path):
        if os.path.isfile(path):
            self.data = open(path, "rb").read()
            base = path[:-4]
            self.vvd = open(base + ".vvd", "rb").read() if os.path.exists(base + ".vvd") else None
            self.vtx = None
            for ext in (".dx90.vtx", ".dx80.vtx", ".sw.vtx"):
                if os.path.exists(base + ext):
                    self.vtx = open(base + ext, "rb").read()
                    break
            self.phy = open(base + ".phy", "rb").read() if os.path.exists(base + ".phy") else None
            self.game_name = None
        else:
            name = path.replace("\\", "/").lower()
            self.data = fs.read(name)
            if self.data is None:
                raise SystemExit("no model " + name)
            self.vvd = fs.read(name[:-4] + ".vvd")
            self.vtx = None
            for ext in (".dx90.vtx", ".dx80.vtx", ".sw.vtx"):
                self.vtx = fs.read(name[:-4] + ext)
                if self.vtx is not None:
                    break
            self.phy = fs.read(name[:-4] + ".phy")
            self.game_name = name
        d = self.data
        self.version = struct.unpack_from("<i", d, 4)[0]
        self.internal_name = d[12:76].split(b"\0")[0].decode("utf-8", "replace")
        self.eye = struct.unpack_from("<3f", d, 80)
        self.illum = struct.unpack_from("<3f", d, 92)
        self.flags = struct.unpack_from("<i", d, 152)[0]
        self.read_bones()
        self.read_textures()
        self.surfaceprop = cstr(d, struct.unpack_from("<i", d, 308)[0])
        n_inc, inc_idx = struct.unpack_from("<ii", d, 336)
        self.includes = []
        for i in range(n_inc):
            b = inc_idx + i * 8
            self.includes.append(cstr(d, b + struct.unpack_from("<i", d, b + 4)[0]).replace("\\", "/"))
        self.nflex = struct.unpack_from("<i", d, 260)[0]

    def read_bones(self):
        d = self.data
        n, idx = struct.unpack_from("<ii", d, 156)
        self.bone_names, self.bone_parent, self.bone_pos, self.bone_rot, self.bone_quat = [], [], [], [], []
        for i in range(n):
            b = idx + i * 216
            nm, par = struct.unpack_from("<ii", d, b)
            self.bone_names.append(cstr(d, b + nm))
            self.bone_parent.append(par)
            self.bone_pos.append(struct.unpack_from("<3f", d, b + 32))
            self.bone_quat.append(struct.unpack_from("<4f", d, b + 44))
            self.bone_rot.append(struct.unpack_from("<3f", d, b + 60))

    def read_textures(self):
        d = self.data
        ntex, texidx, ncd, cdidx = struct.unpack_from("<iiii", d, 204)
        self.textures = []
        for i in range(ntex):
            b = texidx + i * 64
            self.textures.append(cstr(d, b + struct.unpack_from("<i", d, b)[0]).replace("\\", "/"))
        self.cdmaterials = [cstr(d, struct.unpack_from("<i", d, cdidx + i * 4)[0]).replace("\\", "/") for i in range(ncd)]
        nref, nfam, skinidx = struct.unpack_from("<iii", d, 220)
        self.skins = [list(struct.unpack_from("<%dh" % nref, d, skinidx + f * nref * 2)) for f in range(nfam)] if nref else [[]]

    # -------------------------------------------------------------- body parts
    def bodyparts(self):
        """[(name, base, [(model name, [mesh...]) ...])]; mesh = (skinref, pos, nrm, uv, idx, bw, bb)"""
        d = self.data
        nbody, bodyidx = struct.unpack_from("<ii", d, 232)
        verts = _vvd_vertices(self.vvd) if self.vvd else None
        vtx = self.vtx
        out = []
        vnbody, vbodyofs = struct.unpack_from("<ii", vtx, 28) if vtx else (0, 0)
        for bi in range(nbody):
            bp = bodyidx + bi * 16
            bname, nmodels, bbase, modelidx = struct.unpack_from("<iiii", d, bp)
            models = []
            for mi in range(nmodels):
                mp = bp + modelidx + mi * 148
                mname = d[mp:mp + 64].split(b"\0")[0].decode("utf-8", "replace")
                nmeshes, meshidx, nverts, vertindex = struct.unpack_from("<iiii", d, mp + 72)
                meshes = []
                if nmeshes and verts is not None and bi < vnbody:
                    vbp = vbodyofs + bi * 8
                    vnm, vmodelofs = struct.unpack_from("<ii", vtx, vbp)
                    vmp = vbp + vmodelofs + mi * 8
                    vnlod, vlodofs = struct.unpack_from("<ii", vtx, vmp)
                    vlp = vmp + vlodofs
                    vnmesh, vmeshofs, _sw = struct.unpack_from("<iif", vtx, vlp)
                    model_vert0 = vertindex // 48
                    sg_size = strip_group_size(vtx, vlp + vmeshofs)
                    for k in range(min(nmeshes, vnmesh)):
                        mmp = mp + meshidx + k * 116
                        skinref, _mmi, mnverts, mvertoffset = struct.unpack_from("<iiii", d, mmp)
                        vmeshp = vlp + vmeshofs + k * 9
                        nsg, sgofs, _mf = struct.unpack_from("<iiB", vtx, vmeshp)
                        P, N, U, W, B, I = [], [], [], [], [], []
                        base = 0
                        for g in range(nsg):
                            sgp = vmeshp + sgofs + g * sg_size
                            nv, vofs, ni, iofs, _ns, _so, _f = struct.unpack_from("<iiiiiiB", vtx, sgp)
                            if nv <= 0 or ni <= 0:
                                continue
                            vt = np.frombuffer(vtx, dtype=np.dtype([("bw", "u1", 3), ("nb", "u1"), ("orig", "<u2"), ("bid", "i1", 3)]), count=nv, offset=sgp + vofs)
                            ind = np.frombuffer(vtx, dtype="<u2", count=ni, offset=sgp + iofs).astype(np.int64)
                            src = np.clip(model_vert0 + mvertoffset + vt["orig"].astype(np.int64), 0, len(verts) - 1)
                            v = verts[src]
                            P.append(v["pos"]); N.append(v["nrm"]); U.append(v["uv"])
                            w = v["w"].copy()
                            w[np.arange(3)[None, :] >= v["nb"][:, None]] = 0
                            W.append(w); B.append(v["b"].astype(np.int64))
                            I.append(ind[: len(ind) // 3 * 3] + base)
                            base += nv
                        if P:
                            meshes.append((skinref, np.concatenate(P).astype(np.float64), np.concatenate(N).astype(np.float64),
                                           np.concatenate(U).astype(np.float64), np.concatenate(I), np.concatenate(W), np.concatenate(B)))
                models.append((mname, meshes))
            out.append((cstr(d, bp + bname), bbase, models))
        return out

    def attachments(self):
        d = self.data
        n, idx = struct.unpack_from("<ii", d, 240)
        out = []
        for i in range(n):
            b = idx + i * 92
            nm, flags, bone = struct.unpack_from("<iIi", d, b)
            m = np.array(struct.unpack_from("<12f", d, b + 12)).reshape(3, 4)
            out.append((cstr(d, b + nm), bone, m[:, 3], matrix_angles(m[:, :3]), flags))
        return out

    def hitboxes(self):
        d = self.data
        nsets, setidx = struct.unpack_from("<ii", d, 172)
        out = []
        for s in range(nsets):
            sb = setidx + s * 12
            snm, nh, hidx = struct.unpack_from("<iii", d, sb)
            boxes = []
            for h in range(nh):
                hb = sb + hidx + h * 68
                bone, group = struct.unpack_from("<ii", d, hb)
                mins = struct.unpack_from("<3f", d, hb + 8)
                maxs = struct.unpack_from("<3f", d, hb + 20)
                boxes.append((bone, group, mins, maxs))
            out.append((cstr(d, sb + snm), boxes))
        return out

    def sequences(self):
        """own sequences: (name, activity, actweight, flags, fps, frames, anim index, events)"""
        d = self.data
        numseq, seqidx = struct.unpack_from("<ii", d, 188)
        numanim, animidx = struct.unpack_from("<ii", d, 180)
        out = []
        for i in range(numseq):
            b = seqidx + i * 212
            lab, act, flags, _a, weight, nev, evidx = struct.unpack_from("<iiiiiii", d, b + 4)
            if flags & anim.SEQ_OVERRIDE:
                continue
            aii = struct.unpack_from("<i", d, b + 60)[0]
            a = struct.unpack_from("<h", d, b + aii)[0] if aii else 0
            if not (0 <= a < numanim):
                continue
            ad = animidx + a * 100
            fps, aflags, frames = struct.unpack_from("<fii", d, ad + 8)
            events = []
            for e in range(nev):
                eb = b + evidx + e * 80
                cycle, ev, typ = struct.unpack_from("<fii", d, eb)
                opts = d[eb + 12:eb + 76].split(b"\0")[0].decode("utf-8", "replace")
                ename = cstr(d, eb + struct.unpack_from("<i", d, eb + 76)[0]) if typ & (1 << 10) else ""
                events.append((cycle, ename or str(ev), opts))
            fadein, fadeout = struct.unpack_from("<ff", d, b + 104)
            out.append(dict(name=cstr(d, b + lab), activity=cstr(d, b + act), weight=weight, flags=flags | aflags,
                            fps=fps if fps > 0 else 30.0, frames=max(1, frames), anim=a, events=events,
                            fadein=fadein, fadeout=fadeout))
        return out

    def ikchains(self):
        """[(name, end bone name, knee)] - the IK rules of the animations name chains by index"""
        d = self.data
        n, idx = struct.unpack_from("<ii", d, 284)
        out = []
        for i in range(n):
            b = idx + i * 16
            nm, _lt, nlinks, linkidx = struct.unpack_from("<iiii", d, b)
            if nlinks <= 0:
                continue
            lb = b + linkidx + (nlinks - 1) * 28
            bone = struct.unpack_from("<i", d, lb)[0]
            knee = struct.unpack_from("<3f", d, b + linkidx + 4)
            out.append((cstr(d, b + nm), self.bone_names[bone] if 0 <= bone < len(self.bone_names) else "", knee))
        return out

    def iklocks(self):
        d = self.data
        n, idx = struct.unpack_from("<ii", d, 320)
        return [struct.unpack_from("<iff", d, idx + i * 32) for i in range(n)]

    def physics(self):
        """the text part of the .phy: solids (index -> bone name, mass), constraints, self collisions"""
        if not self.phy:
            return None
        txt = self.phy.decode("latin-1")
        i = txt.find("solid {")
        if i < 0:
            return None
        text = txt[i:]
        solids, cons, rules = {}, [], []
        for m in re.finditer(r"(solid|ragdollconstraint|collisionrules|editparams)\s*\{([^}]*)\}", text):
            kv = dict(re.findall(r'"([^"]+)"\s*"([^"]*)"', m.group(2)))
            if m.group(1) == "solid":
                solids[int(kv.get("index", len(solids)))] = kv
            elif m.group(1) == "ragdollconstraint":
                cons.append(kv)
            elif m.group(1) == "collisionrules":
                rules = re.findall(r'"([^"]+)"\s*"([^"]*)"', m.group(2))
        nsolids = struct.unpack_from("<i", self.phy, 8)[0] if len(self.phy) >= 16 else len(solids)
        return dict(solids=solids, constraints=cons, rules=rules, count=nsolids)


# ------------------------------------------------------------------ writing
def smd_skeleton(m, poses=None):
    """nodes + skeleton; poses: list of frames [(pos n3, euler n3)] or None = the reference pose"""
    out = ["version 1", "nodes"]
    for i, nm in enumerate(m.bone_names):
        out.append('%d "%s" %d' % (i, nm, m.bone_parent[i]))
    out += ["end", "skeleton"]
    if poses is None:
        poses = [(np.array(m.bone_pos), np.array(m.bone_rot))]
    for t, (P, E) in enumerate(poses):
        out.append("time %d" % t)
        for i in range(len(m.bone_names)):
            out.append("%d %.6f %.6f %.6f %.6f %.6f %.6f" % (i, P[i][0], P[i][1], P[i][2], E[i][0], E[i][1], E[i][2]))
    out.append("end")
    return "\n".join(out) + "\n"


def mesh_triangles(m, meshes, material_of=None):
    lines = ["triangles"]
    for (skinref, P, N, U, I, W, B) in meshes:
        tex = m.textures[m.skins[0][skinref]] if m.skins and m.skins[0] and skinref < len(m.skins[0]) else m.textures[min(skinref, len(m.textures) - 1)]
        mat = material_of(tex) if material_of else tex
        for tri in I.reshape(-1, 3):
            lines.append(mat)
            for v in (tri[2], tri[1], tri[0]):
                w, b = W[v], B[v]
                links = [(int(b[k]), float(w[k])) for k in range(3) if w[k] > 1e-5]
                tot = sum(x[1] for x in links) or 1.0
                links = [(bb, ww / tot) for bb, ww in links] or [(0, 1.0)]
                s = "%d %.6f %.6f %.6f %.6f %.6f %.6f %.6f %.6f %d" % (links[0][0], P[v][0], P[v][1], P[v][2], N[v][0], N[v][1], N[v][2],
                                                                     U[v][0], 1.0 - U[v][1], len(links))
                s += "".join(" %d %.6f" % ln for ln in links)
                lines.append(s)
    lines.append("end")
    return "\n".join(lines) + "\n"


def phys_triangles(m, meshes, phys_bones):
    """the reference meshes with each vertex rigidly on its physics bone (the nearest one up the chain)"""
    lower = {nm.lower(): i for i, nm in enumerate(m.bone_names)}
    is_phys = np.zeros(len(m.bone_names), bool)
    for nm in phys_bones:
        if nm.lower() in lower:
            is_phys[lower[nm.lower()]] = True
    owner = []
    for i in range(len(m.bone_names)):
        j = i
        while j >= 0 and not is_phys[j]:
            j = m.bone_parent[j]
        owner.append(j if j >= 0 else int(np.argmax(is_phys)) if is_phys.any() else 0)
    lines = ["triangles"]
    for (skinref, P, N, U, I, W, B) in meshes:
        for tri in I.reshape(-1, 3):
            bones = [owner[int(B[v][int(np.argmax(W[v]))])] for v in tri]
            # a triangle across two bodies would join their hulls: it goes to the bone of most of its corners
            ob = max(set(bones), key=bones.count)
            lines.append("phy")
            for v in (tri[2], tri[1], tri[0]):
                lines.append("%d %.6f %.6f %.6f 0 0 1 0 0" % (ob, P[v][0], P[v][1], P[v][2]))
    lines.append("end")
    return "\n".join(lines) + "\n"


def sample_frames(fs, m, seq):
    """[(pos, euler)] per frame of a sequence of the model itself"""
    src = anim.Source(fs, m.game_name or "", m.data, None)
    frames = []
    for f in range(seq["frames"]):
        pos, quat = src.sample(seq["anim"], f)
        eul = np.array([quat_radian_euler(q) for q in quat])
        frames.append((pos, eul))
    return frames


def decompile(fs, path, out_dir, new_name=None, anims=True, material_of=None, extra_qc="", ik_from=None):
    """-> (qc path, Model); the SMDs next to it.
    anims=True rebuilds the sequences from sampled frames WITHOUT their movement (the piecewise
    movement is not put back): an NPC on them walks with speed 0 and stays where it is - for an NPC
    take the animations of the source model with $includemodel instead (make_zombie_hazmat.py)."""
    os.makedirs(out_dir, exist_ok=True)
    m = Model(fs, path)
    base = os.path.basename(new_name) if new_name else os.path.splitext(os.path.basename(m.game_name or path))[0]
    skel = smd_skeleton(m)
    if new_name:
        model_name = new_name
    elif (m.game_name or "").startswith("models/"):
        model_name = m.game_name[len("models/"):-4]
    else:
        model_name = base
    # the SMDs hold engine-space data (bones, VVD vertices, animations as stored): studiomdl must not
    # turn them by its default 90 degrees about Z ($origin's angle is that minus 90)
    qc = ['$modelname "%s.mdl"' % model_name, '$origin 0 0 0 -90']
    bodies = m.bodyparts()
    all_meshes = []
    for bname, _bbase, models in bodies:
        entries = []
        for mi, (mname, meshes) in enumerate(models):
            if not meshes:
                entries.append("blank")
                continue
            fn = "%s_%s_%d.smd" % (base, re.sub(r"[^A-Za-z0-9_]+", "_", bname), mi)
            with open(os.path.join(out_dir, fn), "w", encoding="ascii", errors="replace", newline="\n") as f:
                f.write(skel + mesh_triangles(m, meshes, material_of))
            entries.append('studio "%s"' % fn)
            if mi == 0:
                all_meshes += meshes
        if len(entries) == 1 and entries[0] != "blank":
            qc.append('$body "%s" "%s"' % (bname, entries[0].split('"')[1]))
        else:
            qc.append('$bodygroup "%s"\n{\n%s\n}' % (bname, "\n".join("\t" + e for e in entries)))
    if m.surfaceprop:
        qc.append('$surfaceprop "%s"' % m.surfaceprop)
    if any(m.eye):
        qc.append("$eyeposition %.3f %.3f %.3f" % m.eye)
    if any(m.illum):
        qc.append("$illumposition %.3f %.3f %.3f" % m.illum)
    for cd in m.cdmaterials:
        qc.append('$cdmaterials "%s"' % cd.strip("/"))
    if len(m.skins) > 1:
        cols = [c for c in range(len(m.skins[0])) if len({fam[c] for fam in m.skins}) > 1]
        if cols:
            rows = ["\t{ %s }" % " ".join('"%s"' % (material_of(m.textures[fam[c]]) if material_of else m.textures[fam[c]]) for c in cols) for fam in m.skins]
            qc.append('$texturegroup "skinfamilies"\n{\n%s\n}' % "\n".join(rows))
    for inc in m.includes:
        qc.append('$includemodel "%s"' % (inc[len("models/"):] if inc.lower().startswith("models/") else inc))
    for nm, bone, off, ang, flags in m.attachments():
        qc.append('$attachment "%s" "%s" %.4f %.4f %.4f rotate %.3f %.3f %.3f' % (nm, m.bone_names[bone], off[0], off[1], off[2], ang[0], ang[1], ang[2]))
    sets = m.hitboxes()
    for si, (sname, boxes) in enumerate(sets):
        if len(sets) > 1:
            qc.append('$hboxset "%s"' % sname)
        for bone, group, mins, maxs in boxes:
            qc.append('$hbox %d "%s" %.3f %.3f %.3f %.3f %.3f %.3f' % (group, m.bone_names[bone], *mins, *maxs))
    # IK chains: the IK rules / locks of the animations (own or included) name chains by their
    # index in the model - without them the game writes past the end (CIKContext::SolveLock).
    # A model without chains takes those of the model its animations come from, in their order.
    chains, locks = m.ikchains(), m.iklocks()
    if not chains:
        for src in ([ik_from] if ik_from else []) + list(m.includes):
            data = fs.read(src.replace("\\", "/").lower())
            if data is None:
                continue
            inc = Model.__new__(Model)
            inc.data = data
            inc.read_bones()
            chains = inc.ikchains()
            locks = locks or inc.iklocks()
            if chains:
                break
    lower = {b.lower() for b in m.bone_names}
    for nm, bone, knee in chains:
        if bone.lower() in lower:
            qc.append('$ikchain "%s" "%s" knee %.3f %.3f %.3f' % (nm, bone, knee[0], knee[1], knee[2]))
        else:
            print("  no bone %s for the IK chain %s" % (bone, nm))
    for chain, posw, qw in locks:
        if 0 <= chain < len(chains):
            qc.append('$ikautoplaylock "%s" %.2f %.2f' % (chains[chain][0], posw, qw))
    # sequences
    seqs = m.sequences()
    have_seq = False
    for s in seqs:
        if not anims and s["name"].lower() not in ("ragdoll", "idle", "ref", "reference"):
            continue
        fn = "%s_anim_%s.smd" % (base, re.sub(r"[^A-Za-z0-9_]+", "_", s["name"]))
        try:
            frames = sample_frames(fs, m, s)
        except Exception as e:      # data we can't read: the reference pose
            print("  %s: %s, reference pose" % (s["name"], e))
            frames = [(np.array(m.bone_pos), np.array(m.bone_rot))]
        with open(os.path.join(out_dir, fn), "w", encoding="ascii", newline="\n") as f:
            f.write(smd_skeleton(m, frames))
        line = '$sequence "%s" "%s" fps %.3f' % (s["name"], fn, s["fps"])
        if s["flags"] & anim.SEQ_LOOPING:
            line += " loop"
        if s["activity"]:
            line += " activity %s %d" % (s["activity"], max(1, s["weight"]))
        if s["events"]:
            line += " {\n" + "\n".join('\t{ event %s %d "%s" }' % (en, int(round(cyc * max(1, s["frames"] - 1))), opts) for cyc, en, opts in s["events"]) + "\n}"
        qc.append(line)
        have_seq = True
    if not have_seq:
        fn = "%s_anim_idle.smd" % base
        with open(os.path.join(out_dir, fn), "w", encoding="ascii", newline="\n") as f:
            f.write(skel)
        qc.append('$sequence "idle" "%s" fps 30' % fn)
    # physics
    phy = m.physics()
    if phy and phy["solids"]:
        names = [phy["solids"][k].get("name", "") for k in sorted(phy["solids"])]
        total = sum(float(phy["solids"][k].get("mass", 1)) for k in phy["solids"])
        fn = "%s_phys.smd" % base
        with open(os.path.join(out_dir, fn), "w", encoding="ascii", newline="\n") as f:
            f.write(skel + phys_triangles(m, all_meshes, names))
        if len(names) > 1:
            body = ["\t$mass %.2f" % total, "\t$inertia 10", "\t$damping 0.01", "\t$rotdamping 1.5"]
            root = names[0]
            body.append('\t$rootbone "%s"' % root)
            for c in phy["constraints"]:
                try:
                    child = names[int(c["child"])]
                except (KeyError, IndexError, ValueError):
                    continue
                for ax in "xyz":
                    lo, hi, fr = float(c.get(ax + "min", 0)), float(c.get(ax + "max", 0)), float(c.get(ax + "friction", 0))
                    if lo == 0 and hi == 0:
                        body.append('\t$jointconstrain "%s" %s limit 0 0 0' % (child, ax))
                    else:
                        body.append('\t$jointconstrain "%s" %s limit %.2f %.2f %.2f' % (child, ax, lo, hi, fr))
            if not any(k == "selfcollisions" and v == "0" for k, v in phy["rules"]):
                pass
            else:
                body.append("\t$noselfcollisions")
            qc.append('$collisionjoints "%s"\n{\n%s\n}' % (fn, "\n".join(body)))
        else:
            qc.append('$collisionmodel "%s"\n{\n\t$mass %.2f\n\t$concave\n}' % (fn, total))
    if extra_qc:
        qc.append(extra_qc)
    qc_path = os.path.join(out_dir, base + ".qc")
    with open(qc_path, "w", encoding="ascii", errors="replace", newline="\n") as f:
        f.write("// mdl_decompile.py: %s (v%d, %d bones, %d flexes not rebuilt)\n" % (m.game_name or path, m.version, len(m.bone_names), m.nflex))
        f.write("\n".join(qc) + "\n")
    return qc_path, m


def compile_qc(qc_path, game=MOD):
    import subprocess
    exe = os.path.join(SDK, "bin", "studiomdl.exe")
    r = subprocess.run([exe, "-game", game, "-nop4", "-nox360", qc_path], cwd=os.path.dirname(qc_path), capture_output=True, text=True, errors="replace")
    txt = r.stdout + r.stderr
    bad = [l for l in txt.splitlines() if "ERROR" in l.upper()]
    return r.returncode, txt, bad


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return
    fs = gamefs.GameFS(MOD, SDK)
    name = argv[argv.index("--name") + 1] if "--name" in argv else None
    qc, m = decompile(fs, argv[0], argv[1], new_name=name, anims="--no-anims" not in argv)
    print("%s: v%d, %d bones, %d textures, %d skins, includes %s -> %s" % (argv[0], m.version, len(m.bone_names), len(m.textures), len(m.skins), m.includes, qc))


if __name__ == "__main__":
    main(sys.argv[1:])
