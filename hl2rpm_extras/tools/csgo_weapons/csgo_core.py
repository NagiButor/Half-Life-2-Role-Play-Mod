"""Core utilities for porting CS:GO legacy (MDL v49 + .ani STUDIO_FRAMEANIM + VTF v7.5) weapons
to HL2RPM (Source SDK 2013 Mapbase, deferred PBR renderer, ValveBiped c_arms hand_viewmodel).
"""
import io
import math
import os
import struct
import sys
import wave

import numpy as np
from PIL import Image, ImageDraw, ImageFilter

TOOLS_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
if TOOLS_DIR not in sys.path:
    sys.path.insert(0, TOOLS_DIR)
DLG_DIR = os.path.join(TOOLS_DIR, "dialog_editor")
if DLG_DIR not in sys.path:
    sys.path.insert(0, DLG_DIR)

import hd_textures  # noqa: E402
import mdl_decompile as dec  # noqa: E402
import vpktex  # noqa: E402
from make_hands import VIEW_BONES  # noqa: E402
from dlgedit.world import anim, gamefs, vpk  # noqa: E402

MOD = dec.MOD
SDK = dec.SDK
CSGO_VPK = r"E:\Steam\steamapps\common\csgo legacy\csgo\pak01_dir.vpk"
SRC_ROOT = r"E:\XBLAH's modding tool\content\Mapbase\hl2rpm\modelsrc\hl2rpm_weapons"
VB_REF = "models/hl2rpm/hands/citizen.mdl"

# Shoulders behind & below camera eye in scaled viewmodel space (+X fwd, +Y left, +Z up)
SHOULDER = {"R": np.array([-3.0, -9.0, -11.0]), "L": np.array([-3.0, 9.0, -11.0])}

# CS:GO viewmodel presets (viewmodel_presetpos 1 / 3): viewmodel_fov, viewmodel_offset_x (right),
# viewmodel_offset_y (forward), viewmodel_offset_z (up). CS:GO adds them in eye space in
# C_BaseViewModel::CalcViewModelView: origin += forward*y + right*x + up*z.
VM_PRESETS = {
    "desktop": (60.0, 1.0, 1.0, -1.0),
    "classic": (54.0, 2.5, 0.0, -1.5),   # "Классический" вид оружия
}


def vm_preset_shift(preset, scale):
    """Eye-space offset matrix (+X fwd, +Y left, +Z up) and viewmodel FOV of a CS:GO preset.
    The CS:GO frames are scaled about the eye by `scale` (same picture), so the offset is too."""
    fov, ox, oy, oz = VM_PRESETS[preset]
    M = np.eye(4)
    M[:3, 3] = np.array([oy, -ox, oz]) * scale
    return M, fov


# ------------------------------------------------------------------ math
def euler_m(e):
    x, y, z = e
    cx, sx, cy, sy, cz, sz = math.cos(x), math.sin(x), math.cos(y), math.sin(y), math.cos(z), math.sin(z)
    return (
        np.array([[cz, -sz, 0], [sz, cz, 0], [0, 0, 1]])
        @ np.array([[cy, 0, sy], [0, 1, 0], [-sy, 0, cy]])
        @ np.array([[1, 0, 0], [0, cx, -sx], [0, sx, cx]])
    )


def m_euler(R):
    p, y, r = dec.matrix_angles(R)
    return np.radians([r, p, y])


def norm(v):
    v = np.asarray(v, dtype=np.float64)
    return v / max(float(np.linalg.norm(v)), 1e-9)


def orth(a, x):
    return norm(a - np.dot(a, x) * x)


def frame(x, y, origin):
    M = np.eye(4)
    M[:3, 0], M[:3, 1], M[:3, 2], M[:3, 3] = x, y, np.cross(x, y), origin
    return M


def rot_only(M):
    M = M.copy()
    M[:3, 3] = 0.0
    return M


def inv(M):
    return np.linalg.inv(M)


def fk_pq(m, pos, quat):
    W = []
    for i in range(len(m.bone_names)):
        L = np.eye(4)
        L[:3, :3] = anim._quat_matrix(np.asarray(quat[i], np.float64))
        L[:3, 3] = pos[i]
        p = m.bone_parent[i]
        W.append(W[p] @ L if p >= 0 else L)
    return W


def fk_bind(m):
    return fk_pq(m, np.array(m.bone_pos), np.array(m.bone_quat))


def scale_world(W, scale):
    out = []
    for M in W:
        M2 = M.copy()
        M2[:3, 3] *= scale
        out.append(M2)
    return out


# ------------------------------------------------------------------ CS:GO STUDIO_FRAMEANIM decoder
def decode_quat48s(buf, off):
    """Decode CS:GO MDL v49 Quaternion48S (6 bytes, smallest-3 cyclic, 15-bit per component)."""
    w0, w1, w2 = struct.unpack_from("<HHH", buf, off)
    s = 1.0 / (16384.0 * math.sqrt(2.0))
    a = ((w0 & 0x7FFF) - 16384) * s
    b = ((w1 & 0x7FFF) - 16384) * s
    c = ((w2 & 0x7FFF) - 16384) * s
    d = math.sqrt(max(0.0, 1.0 - a * a - b * b - c * c))
    if w2 >> 15:
        d = -d
    idx = ((w0 >> 15) << 1) | (w1 >> 15)
    miss = (idx - 1) & 3
    q = np.zeros(4, dtype=np.float64)
    q[miss] = d
    q[(miss + 1) % 4] = a
    q[(miss + 2) % 4] = b
    q[(miss + 3) % 4] = c
    return q


def decode_quat48(buf, off):
    x, y, zw = struct.unpack_from("<HHH", buf, off)
    qx = (x - 32768) / 32768.0
    qy = (y - 32768) / 32768.0
    qz = ((zw & 0x7FFF) - 16384) / 16384.0
    qw = math.sqrt(max(0.0, 1.0 - qx * qx - qy * qy - qz * qz))
    if zw & 0x8000:
        qw = -qw
    return np.array([qx, qy, qz, qw], dtype=np.float64)


def decode_vec48(buf, off):
    h = struct.unpack_from("<HHH", buf, off)
    return np.array([anim._half(x) for x in h], dtype=np.float64)


def sample_csgo_frame(src, anim_idx, frame_idx):
    """Sample local (pos, quat) of a CS:GO MDL v49 animation (handles both RLE and STUDIO_FRAMEANIM)."""
    bones = src.bones
    n = len(bones)
    pos = bones.pos.copy()
    quat = bones.quat.copy()
    fps, flags, frames = src.anim_info(anim_idx)
    frame_idx = int(max(0, min(frame_idx, max(0, frames - 1))))
    buf, off, f = src._anim_data(anim_idx, frame_idx)
    if buf is None:
        return pos, quat
    if not (flags & 0x40):  # STUDIO_FRAMEANIM
        return src.sample(anim_idx, frame_idx)
    const_off, frame_off, frame_len = struct.unpack_from("<iii", buf, off)
    bflags = buf[off + 24 : off + 24 + n]
    p = off + const_off
    for bi in range(n):
        fl = bflags[bi]
        if fl & 0x02:
            quat[bi] = decode_quat48(buf, p)
            p += 6
        if fl & 0x40:
            quat[bi] = decode_quat48s(buf, p)
            p += 6
        if fl & 0x01:
            pos[bi] = decode_vec48(buf, p)
            p += 6
        if fl & 0x20:
            pos[bi] = struct.unpack_from("<3f", buf, p)
            p += 12
    if frame_len > 0:
        p = off + frame_off + f * frame_len
        for bi in range(n):
            fl = bflags[bi]
            if fl & 0x08:
                quat[bi] = decode_quat48(buf, p)
                p += 6
            if fl & 0x80:
                quat[bi] = decode_quat48s(buf, p)
                p += 6
            if fl & 0x04:
                pos[bi] = decode_vec48(buf, p)
                p += 6
            if fl & 0x10:
                pos[bi] = struct.unpack_from("<3f", buf, p)
                p += 12
    return pos, quat


def sample_csgo_seq(m, ani_data, fs, seq, scale):
    """Sample all frames of a CS:GO sequence -> list of scaled world bone matrix lists."""
    src = anim.Source(fs, "", m.data, None)
    src.ani = ani_data
    out = []
    for fr in range(seq["frames"]):
        pos, quat = sample_csgo_frame(src, seq["anim"], fr)
        out.append(scale_world(fk_pq(m, pos, quat), scale))
    return out


def extract_csgo_model(v, base, work):
    """Extract .mdl/.vvd/.dx90.vtx/.ani from CS:GO VPK into work dir."""
    os.makedirs(work, exist_ok=True)
    nm = os.path.basename(base)
    for ext in (".mdl", ".vvd", ".dx90.vtx", ".ani"):
        d = v.read(base + ext)
        if d is not None:
            with open(os.path.join(work, "_csgo_" + nm + ext), "wb") as f:
                f.write(d)
    mdl_path = os.path.join(work, "_csgo_" + nm + ".mdl")
    ani_path = os.path.join(work, "_csgo_" + nm + ".ani")
    ani_data = open(ani_path, "rb").read() if os.path.exists(ani_path) else None
    return mdl_path, ani_data


# ------------------------------------------------------------------ ValveBiped c_arms retargeting
def csgo_arm_index(m, side):
    """CS:GO right-handed arm bones for side 'R' or 'L': forearm, hand, fingers[5][3]."""
    low = {b.lower(): i for i, b in enumerate(m.bone_names)}
    pfx = "v_weapon.bip01_%s_" % side.lower()
    return {
        "fore": low[pfx + "forearm"],
        "hand": low[pfx + "hand"],
        "f": [[low[pfx + "finger%d%s" % (f, k)] for k in ("", "1", "2")] for f in range(5)],
    }


def vb_names(s):
    b = "ValveBiped.Bip01_%s_" % s
    return {
        "fore": b + "Forearm",
        "hand": b + "Hand",
        "up": b + "UpperArm",
        "clav": b + "Clavicle",
        "f": [[b + "Finger%d%s" % (f, k) for k in ("", "1", "2")] for f in range(5)],
    }


def vb_index(m, s):
    low = {b.lower(): i for i, b in enumerate(m.bone_names)}
    n = vb_names(s)
    return {
        "fore": low[n["fore"].lower()],
        "hand": low[n["hand"].lower()],
        "up": low[n["up"].lower()],
        "clav": low[n["clav"].lower()],
        "f": [[low[x.lower()] for x in row] for row in n["f"]],
    }


def geo_frames(W, ix):
    """Construct rig-independent joint frames: X along bone, Y across palm (index - pinky)."""
    P = lambda i: W[i][:3, 3]
    a = norm(P(ix["f"][1][0]) - P(ix["f"][4][0]))
    G = {}
    x = norm(P(ix["f"][2][0]) - P(ix["hand"]))
    G["hand"] = frame(x, orth(a, x), P(ix["hand"]))
    x = norm(P(ix["hand"]) - P(ix["fore"]))
    G["fore"] = frame(x, orth(a, x), P(ix["fore"]))
    for f in range(5):
        for k in range(2):
            x = norm(P(ix["f"][f][k + 1]) - P(ix["f"][f][k]))
            G[(f, k)] = frame(x, orth(a, x), P(ix["f"][f][k]))
    return G


def compute_arm_scale(mc, mv):
    Wb = fk_bind(mc)
    Wv = fk_bind(mv)
    icr, ivr = csgo_arm_index(mc, "R"), vb_index(mv, "R")
    seg = lambda W, a, b: float(np.linalg.norm(W[a][:3, 3] - W[b][:3, 3]))
    ratios = [
        seg(Wv, ivr["fore"], ivr["hand"]) / seg(Wb, icr["fore"], icr["hand"]),
        seg(Wv, ivr["hand"], ivr["f"][2][0]) / seg(Wb, icr["hand"], icr["f"][2][0]),
        seg(Wv, ivr["f"][1][0], ivr["f"][1][1]) / seg(Wb, icr["f"][1][0], icr["f"][1][1]),
        seg(Wv, ivr["f"][2][0], ivr["f"][2][1]) / seg(Wb, icr["f"][2][0], icr["f"][2][1]),
    ]
    return float(np.median(ratios)), ratios


class CSGORetarget:
    """Retarget CS:GO right-handed v_weapon.Bip01_* arm bones onto HL2RPM ValveBiped.Bip01_* c_arms."""

    def __init__(self, Wc_ref, Wv, mc, mv):
        self.C, self.ic, self.iv, self.vb = {}, {}, {}, {}
        for s in ("R", "L"):
            ic, iv = csgo_arm_index(mc, s), vb_index(mv, s)
            Gc, Gv = geo_frames(Wc_ref, ic), geo_frames(Wv, iv)
            C = {}
            for key in ("fore", "hand"):
                C[key] = rot_only(inv(Wc_ref[ic[key]]) @ Gc[key] @ inv(Gv[key]) @ Wv[iv[key]])
            for f in range(5):
                for k in range(2):
                    C[(f, k)] = rot_only(
                        inv(Wc_ref[ic["f"][f][k]]) @ Gc[(f, k)] @ inv(Gv[(f, k)]) @ Wv[iv["f"][f][k]]
                    )
                par = Wc_ref[ic["f"][f][1]] @ C[(f, 1)] @ inv(Wv[iv["f"][f][1]]) @ Wv[iv["f"][f][2]]
                C[(f, 2)] = rot_only(inv(Wc_ref[ic["f"][f][2]]) @ par)
            up, fore = Wv[iv["up"]], Wv[iv["fore"]]
            x = norm(fore[:3, 3] - up[:3, 3])
            Gup = frame(x, orth(fore[:3, 1], x), up[:3, 3])
            self.vb[s] = dict(
                L_up=float(np.linalg.norm(fore[:3, 3] - up[:3, 3])),
                Cup=rot_only(inv(Gup) @ up),
                clav=Wv[iv["clav"]].copy(),
                up=up.copy(),
            )
            self.C[s], self.ic[s], self.iv[s] = C, ic, iv
        sp = mv.bone_names.index("ValveBiped.Bip01_Spine4")
        self.spine = Wv[sp].copy()

    def frame_bones(self, Wc, offset_M=None):
        """Return {ValveBiped bone name: world matrix} for one CS:GO frame, optionally transformed by offset_M."""
        out = {}
        for s in ("R", "L"):
            C, ic, n = self.C[s], self.ic[s], vb_names(s)
            fore = Wc[ic["fore"]] @ C["fore"]
            hand = Wc[ic["hand"]] @ C["hand"]
            if offset_M is not None:
                fore = offset_M @ fore
                hand = offset_M @ hand
            out[n["fore"]] = fore
            out[n["hand"]] = hand
            for f in range(5):
                for k in range(3):
                    M = Wc[ic["f"][f][k]] @ C[(f, k)]
                    if offset_M is not None:
                        M = offset_M @ M
                    out[n["f"][f][k]] = M
            vb = self.vb[s]
            e = fore[:3, 3]
            d = norm(e - SHOULDER[s])
            sh = e - vb["L_up"] * d
            out[n["up"]] = frame(d, orth(fore[:3, 1], d), sh) @ vb["Cup"]
            clav = vb["clav"].copy()
            clav[:3, 3] = sh - (vb["up"][:3, 3] - vb["clav"][:3, 3])
            out[n["clav"]] = clav
            if s == "R":
                spine = self.spine.copy()
                spine[:3, 3] = clav[:3, 3] - (vb["clav"][:3, 3] - self.spine[:3, 3])
                out["ValveBiped.Bip01_Spine4"] = spine
        return out


def arm_skeleton_order():
    out = [("ValveBiped.Bip01_Spine4", None)]
    for s in ("L", "R"):
        n = vb_names(s)
        out += [
            (n["clav"], "ValveBiped.Bip01_Spine4"),
            (n["up"], n["clav"]),
            (n["fore"], n["up"]),
            (n["hand"], n["fore"]),
        ]
        for f in range(5):
            out += [
                (n["f"][f][0], n["hand"]),
                (n["f"][f][1], n["f"][f][0]),
                (n["f"][f][2], n["f"][f][1]),
            ]
    return out


# ------------------------------------------------------------------ Mesh extraction & procedural 3D builder
def extract_gun_tris(m, Wb_scaled, bone_map, scale, mat_name, bodypart_idx=0, model_idx=0, tri_filter=None):
    """Extract triangles from a CS:GO model bodypart into our unified CCW-outward format:
    list of (mat_name, [(local_pos, local_nrm, uv, target_bone_name) x 3]).
    Note: VTX triangles are reversed so every triangle is strictly CCW outward!
    """
    bp = m.bodyparts()[bodypart_idx]
    _mname, meshes = bp[2][model_idx]
    tris_out = []
    for _skinref, P, N, U, I, Wt, B in meshes:
        P = np.asarray(P, dtype=np.float64)
        N = np.asarray(N, dtype=np.float64)
        U = np.asarray(U, dtype=np.float64)
        dom = B[np.arange(len(B)), np.argmax(Wt, 1)]
        for tri in I.reshape(-1, 3):
            bnames = [bone_map.get(int(dom[v])) for v in tri]
            if None in bnames:
                continue
            if tri_filter is not None and not tri_filter(tri, P, N, U, dom):
                continue
            # Reverse order from VTX so winding is CCW outward (ready for SMD without flipping again)
            corners = []
            for v, bname in zip(reversed(tri), reversed(bnames)):
                bi = int(dom[v])
                Wb = Wb_scaled[bi]
                p = (inv(Wb) @ np.r_[P[v] * scale, 1.0])[:3]
                n = norm(Wb[:3, :3].T @ N[v])
                corners.append((p, n, (float(U[v, 0]), float(U[v, 1])), bname))
            tris_out.append((mat_name, corners))
    return tris_out


class MeshBuilder:
    """Builds clean, watertight, strictly CCW-outward procedural 3D weapon hardware in gun-parent space,
    then transforms each vertex into its target bone's local space (scaled by `scale`)."""

    def __init__(self, parent_to_bone, scale=1.0):
        """parent_to_bone: {bone_name: 4x4 matrix from bone_local to parent_local (unscaled)}"""
        self.p2b = {k: inv(M) for k, M in parent_to_bone.items()}
        self.scale = scale
        self.tris = []  # (mat_name, [(local_pos, local_nrm, uv, bone_name) x 3])

    def _emit_tri(self, mat, bone, p0, p1, p2, n0, n1, n2, uv0, uv1, uv2):
        p0, p1, p2 = np.asarray(p0, float), np.asarray(p1, float), np.asarray(p2, float)
        n0, n1, n2 = norm(n0), norm(n1), norm(n2)
        g = np.cross(p1 - p0, p2 - p0)
        if np.linalg.norm(g) < 1e-12:
            return
        navg = n0 + n1 + n2
        if np.dot(g, navg) < 0:
            p1, p2 = p2, p1
            n1, n2 = n2, n1
            uv1, uv2 = uv2, uv1
        M = self.p2b[bone]
        R = M[:3, :3]
        corners = []
        for p, n, uv in ((p0, n0, uv0), (p1, n1, uv1), (p2, n2, uv2)):
            lp = (M @ np.r_[p, 1.0])[:3] * self.scale
            ln = norm(R @ n)
            corners.append((lp, ln, (float(uv[0]), float(uv[1])), bone))
        self.tris.append((mat, corners))

    def quad(self, mat, bone, p0, p1, p2, p3, uv_rect=(0.0, 0.0, 1.0, 1.0), nrm=None):
        """Emit a flat quad (p0,p1,p2,p3 in CCW order looking at front face)."""
        p0, p1, p2, p3 = [np.asarray(p, float) for p in (p0, p1, p2, p3)]
        if nrm is None:
            nrm = norm(np.cross(p1 - p0, p2 - p0))
        u0, v0, u1, v1 = uv_rect
        self._emit_tri(mat, bone, p0, p1, p2, nrm, nrm, nrm, (u0, v0), (u1, v0), (u1, v1))
        self._emit_tri(mat, bone, p0, p2, p3, nrm, nrm, nrm, (u0, v0), (u1, v1), (u0, v1))

    def box(self, mat, bone, x0, x1, y0, y1, z0, z1, uv=(0.05, 0.05, 0.95, 0.95), skip_faces=()):
        """Axis-aligned solid box with outward normals."""
        if "px" not in skip_faces:
            self.quad(mat, bone, (x1, y0, z0), (x1, y1, z0), (x1, y1, z1), (x1, y0, z1), uv, (1, 0, 0))
        if "nx" not in skip_faces:
            self.quad(mat, bone, (x0, y0, z1), (x0, y1, z1), (x0, y1, z0), (x0, y0, z0), uv, (-1, 0, 0))
        if "py" not in skip_faces:
            self.quad(mat, bone, (x0, y1, z0), (x0, y1, z1), (x1, y1, z1), (x1, y1, z0), uv, (0, 1, 0))
        if "ny" not in skip_faces:
            self.quad(mat, bone, (x0, y0, z1), (x0, y0, z0), (x1, y0, z0), (x1, y0, z1), uv, (0, -1, 0))
        if "pz" not in skip_faces:
            self.quad(mat, bone, (x0, y0, z1), (x1, y0, z1), (x1, y1, z1), (x0, y1, z1), uv, (0, 0, 1))
        if "nz" not in skip_faces:
            self.quad(mat, bone, (x1, y0, z0), (x0, y0, z0), (x0, y1, z0), (x1, y1, z0), uv, (0, 0, -1))

    def loft_z(self, mat, bone, rings, uv_rect=(0.05, 0.05, 0.95, 0.95), cap_start=True, cap_end=True, smooth=False):
        """Loft between closed 2D cross-section rings along Z.
        rings: list of [(x, y, z), ...] each with identical count N, ordered CCW looking from +Z.
        """
        u0, v0, u1, v1 = uv_rect
        n_rings = len(rings)
        N = len(rings[0])
        for r in range(n_rings - 1):
            A, B = rings[r], rings[r + 1]
            va = v0 + (v1 - v0) * (r / max(1, n_rings - 1))
            vb = v0 + (v1 - v0) * ((r + 1) / max(1, n_rings - 1))
            for i in range(N):
                j = (i + 1) % N
                ua = u0 + (u1 - u0) * (i / N)
                ub = u0 + (u1 - u0) * ((i + 1) / N)
                p0, p1, p2, p3 = np.array(A[i]), np.array(A[j]), np.array(B[j]), np.array(B[i])
                if smooth:
                    ca = np.mean(A, axis=0)
                    cb = np.mean(B, axis=0)
                    n0 = norm(p0 - ca); n1 = norm(p1 - ca)
                    n2 = norm(p2 - cb); n3 = norm(p3 - cb)
                    self._emit_tri(mat, bone, p0, p1, p2, n0, n1, n2, (ua, va), (ub, va), (ub, vb))
                    self._emit_tri(mat, bone, p0, p2, p3, n0, n2, n3, (ua, va), (ub, vb), (ua, vb))
                else:
                    fn = norm(np.cross(p1 - p0, p3 - p0))
                    # Ensure outward away from ring center
                    ctr = 0.25 * (p0 + p1 + p2 + p3) - 0.5 * (np.mean(A, 0) + np.mean(B, 0))
                    if np.dot(fn, ctr) < 0:
                        fn = -fn
                    self._emit_tri(mat, bone, p0, p1, p2, fn, fn, fn, (ua, va), (ub, va), (ub, vb))
                    self._emit_tri(mat, bone, p0, p2, p3, fn, fn, fn, (ua, va), (ub, vb), (ua, vb))
        if cap_start:
            A = [np.array(p, float) for p in rings[0]]
            ctr = np.mean(A, 0)
            for i in range(N):
                j = (i + 1) % N
                self._emit_tri(mat, bone, ctr, A[j], A[i], (0, 0, -1), (0, 0, -1), (0, 0, -1),
                               ((u0+u1)/2, (v0+v1)/2), (u0, v0), (u1, v0))
        if cap_end:
            B = [np.array(p, float) for p in rings[-1]]
            ctr = np.mean(B, 0)
            for i in range(N):
                j = (i + 1) % N
                self._emit_tri(mat, bone, ctr, B[i], B[j], (0, 0, 1), (0, 0, 1), (0, 0, 1),
                               ((u0+u1)/2, (v0+v1)/2), (u0, v1), (u1, v1))

    def extrude_x(self, mat, bone, poly_zy, x0, x1, uv_rect=(0.05, 0.05, 0.95, 0.95), cap_l=True, cap_r=True):
        """Extrude a 2D (z, y) polygon along X from x0 to x1 (handles convex/star-shaped around centroid)."""
        u0, v0, u1, v1 = uv_rect
        pts = [(float(z), float(y)) for z, y in poly_zy]
        N = len(pts)
        zs = [p[0] for p in pts]; ys = [p[1] for p in pts]
        zlo, zhi = min(zs), max(zs)
        ylo, yhi = min(ys), max(ys)
        zspan = max(zhi - zlo, 1e-6); yspan = max(yhi - ylo, 1e-6)
        # Signed area in (z, y) to ensure CCW
        area = sum(pts[i][0] * pts[(i + 1) % N][1] - pts[(i + 1) % N][0] * pts[i][1] for i in range(N))
        if area < 0:
            pts = list(reversed(pts))
        uv_of = lambda z, y: (u0 + (u1 - u0) * (z - zlo) / zspan, v0 + (v1 - v0) * (1.0 - (y - ylo) / yspan))
        # Side walls
        for i in range(N):
            j = (i + 1) % N
            zi, yi = pts[i]; zj, yj = pts[j]
            dz, dy = zj - zi, yj - yi
            # Outward normal in (x, y, z) for CCW polygon in (z, y): (0, -dz, dy)
            n = norm((0.0, -dz, dy))
            p0 = (x0, yi, zi); p1 = (x0, yj, zj); p2 = (x1, yj, zj); p3 = (x1, yi, zi)
            self.quad(mat, bone, p0, p1, p2, p3, uv_rect, n)
        # Caps (ear clipping works for concave polygons too!)
        tris2d = triangulate_2d(pts)
        for a, b, c in tris2d:
            za, ya = pts[a]; zb, yb = pts[b]; zc, yc = pts[c]
            if cap_r:
                self._emit_tri(mat, bone, (x1, ya, za), (x1, yb, zb), (x1, yc, zc),
                               (1, 0, 0), (1, 0, 0), (1, 0, 0), uv_of(za, ya), uv_of(zb, yb), uv_of(zc, yc))
            if cap_l:
                self._emit_tri(mat, bone, (x0, ya, za), (x0, yc, zc), (x0, yb, zb),
                               (-1, 0, 0), (-1, 0, 0), (-1, 0, 0), uv_of(za, ya), uv_of(zc, yc), uv_of(zb, yb))

    def extrude_x_ring(self, mat, bone, outer_zy, inner_zy, x0, x1, uv_rect=(0.05, 0.05, 0.95, 0.95)):
        """Extrude a hollow ring (outer loop + inner hole loop of equal vertex count N) along X from x0 to x1."""
        assert len(outer_zy) == len(inner_zy)
        u0, v0, u1, v1 = uv_rect
        N = len(outer_zy)
        out = [(float(z), float(y)) for z, y in outer_zy]
        inn = [(float(z), float(y)) for z, y in inner_zy]
        area = sum(out[i][0] * out[(i + 1) % N][1] - out[(i + 1) % N][0] * out[i][1] for i in range(N))
        if area < 0:
            out = list(reversed(out))
            inn = list(reversed(inn))
        zs = [p[0] for p in out]; ys = [p[1] for p in out]
        zlo, zhi = min(zs), max(zs); ylo, yhi = min(ys), max(ys)
        zspan = max(zhi - zlo, 1e-6); yspan = max(yhi - ylo, 1e-6)
        uv_of = lambda z, y: (u0 + (u1 - u0) * (z - zlo) / zspan, v0 + (v1 - v0) * (1.0 - (y - ylo) / yspan))
        for i in range(N):
            j = (i + 1) % N
            oz0, oy0 = out[i]; oz1, oy1 = out[j]
            iz0, iy0 = inn[i]; iz1, iy1 = inn[j]
            # Outer wall (outward normal)
            no = norm((0.0, -(oz1 - oz0), oy1 - oy0))
            self.quad(mat, bone, (x0, oy0, oz0), (x0, oy1, oz1), (x1, oy1, oz1), (x1, oy0, oz0), uv_rect, no)
            # Inner wall (inward normal toward hole center)
            ni = norm((0.0, (iz1 - iz0), -(iy1 - iy0)))
            self.quad(mat, bone, (x1, iy0, iz0), (x1, iy1, iz1), (x0, iy1, iz1), (x0, iy0, iz0), uv_rect, ni)
            # +X side cap quad (outer -> inner)
            self._emit_tri(mat, bone, (x1, oy0, oz0), (x1, oy1, oz1), (x1, iy1, iz1),
                           (1, 0, 0), (1, 0, 0), (1, 0, 0), uv_of(oz0, oy0), uv_of(oz1, oy1), uv_of(iz1, iy1))
            self._emit_tri(mat, bone, (x1, oy0, oz0), (x1, iy1, iz1), (x1, iy0, iz0),
                           (1, 0, 0), (1, 0, 0), (1, 0, 0), uv_of(oz0, oy0), uv_of(iz1, iy1), uv_of(iz0, iy0))
            # -X side cap quad
            self._emit_tri(mat, bone, (x0, oy0, oz0), (x0, iy1, iz1), (x0, oy1, oz1),
                           (-1, 0, 0), (-1, 0, 0), (-1, 0, 0), uv_of(oz0, oy0), uv_of(iz1, iy1), uv_of(oz1, oy1))
            self._emit_tri(mat, bone, (x0, oy0, oz0), (x0, iy0, iz0), (x0, iy1, iz1),
                           (-1, 0, 0), (-1, 0, 0), (-1, 0, 0), uv_of(oz0, oy0), uv_of(iz0, iy0), uv_of(iz1, iy1))

    def cylinder_axis(self, mat, bone, p_start, p_end, r0, r1=None, segs=20, uv_rect=(0.05, 0.05, 0.95, 0.95),
                      cap_start=True, cap_end=True):
        """Smooth cylinder / frustum along arbitrary 3D axis from p_start to p_end."""
        if r1 is None:
            r1 = r0
        p0, p1 = np.asarray(p_start, float), np.asarray(p_end, float)
        axis = norm(p1 - p0)
        ref = np.array([1.0, 0.0, 0.0]) if abs(axis[0]) < 0.8 else np.array([0.0, 1.0, 0.0])
        u = orth(ref, axis)
        v = np.cross(axis, u)
        u0, v0, u1, v1 = uv_rect
        ring0, ring1, nrms = [], [], []
        for i in range(segs):
            a = 2.0 * math.pi * i / segs
            d = math.cos(a) * u + math.sin(a) * v
            ring0.append(p0 + r0 * d)
            ring1.append(p1 + r1 * d)
            nrms.append(d)
        for i in range(segs):
            j = (i + 1) % segs
            ua = u0 + (u1 - u0) * (i / segs)
            ub = u0 + (u1 - u0) * ((i + 1) / segs)
            self._emit_tri(mat, bone, ring0[i], ring0[j], ring1[j], nrms[i], nrms[j], nrms[j], (ua, v0), (ub, v0), (ub, v1))
            self._emit_tri(mat, bone, ring0[i], ring1[j], ring1[i], nrms[i], nrms[j], nrms[i], (ua, v0), (ub, v1), (ua, v1))
            if cap_start:
                self._emit_tri(mat, bone, p0, ring0[j], ring0[i], -axis, -axis, -axis,
                               ((u0+u1)/2, (v0+v1)/2), (ub, v0), (ua, v0))
            if cap_end:
                self._emit_tri(mat, bone, p1, ring1[i], ring1[j], axis, axis, axis,
                               ((u0+u1)/2, (v0+v1)/2), (ua, v1), (ub, v1))

    def tube_z(self, mat_outer, mat_inner, bone, cx, cy, z0, z1, r_out, r_in, segs=24,
               uv_outer=(0.05, 0.05, 0.95, 0.95), uv_crown=(0.05, 0.05, 0.95, 0.95),
               uv_inner=(0.05, 0.05, 0.25, 0.25), crown_inset=0.04, inner_depth=1.5):
        """Hollow barrel tube along +Z with smooth outer wall, 11-deg recessed target crown, and deep dark inner bore."""
        u0, v0, u1, v1 = uv_outer
        r_mid = r_out - 0.025
        z_in_back = max(z0, z1 - inner_depth)
        for i in range(segs):
            j = (i + 1) % segs
            a0 = 2.0 * math.pi * i / segs
            a1 = 2.0 * math.pi * j / segs
            c0, s0 = math.cos(a0), math.sin(a0)
            c1, s1 = math.cos(a1), math.sin(a1)
            ua = u0 + (u1 - u0) * (i / segs)
            ub = u0 + (u1 - u0) * ((i + 1) / segs)
            # Outer barrel wall
            po0 = (cx + r_out * c0, cy + r_out * s0, z0)
            po1 = (cx + r_out * c1, cy + r_out * s1, z0)
            po2 = (cx + r_out * c1, cy + r_out * s1, z1)
            po3 = (cx + r_out * c0, cy + r_out * s0, z1)
            n0, n1 = (c0, s0, 0.0), (c1, s1, 0.0)
            self._emit_tri(mat_outer, bone, po0, po1, po2, n0, n1, n1, (ua, v0), (ub, v0), (ub, v1))
            self._emit_tri(mat_outer, bone, po0, po2, po3, n0, n1, n0, (ua, v0), (ub, v1), (ua, v1))
            # Outer chamfer lip to crown
            pm0 = (cx + r_mid * c0, cy + r_mid * s0, z1 + 0.015)
            pm1 = (cx + r_mid * c1, cy + r_mid * s1, z1 + 0.015)
            nf = norm((c0 * 0.4, s0 * 0.4, 0.9))
            self._emit_tri(mat_outer, bone, po3, po2, pm1, nf, nf, nf, uv_crown[:2], (uv_crown[2], uv_crown[1]), uv_crown[2:])
            self._emit_tri(mat_outer, bone, po3, pm1, pm0, nf, nf, nf, uv_crown[:2], uv_crown[2:], (uv_crown[0], uv_crown[3]))
            # Recessed 11-deg target crown inward to bore
            pi0 = (cx + r_in * c0, cy + r_in * s0, z1 - crown_inset)
            pi1 = (cx + r_in * c1, cy + r_in * s1, z1 - crown_inset)
            ncr = norm((-c0 * 0.2, -s0 * 0.2, 0.98))
            self._emit_tri(mat_outer, bone, pm0, pm1, pi1, ncr, ncr, ncr, uv_crown[:2], (uv_crown[2], uv_crown[1]), uv_crown[2:])
            self._emit_tri(mat_outer, bone, pm0, pi1, pi0, ncr, ncr, ncr, uv_crown[:2], uv_crown[2:], (uv_crown[0], uv_crown[3]))
            # Inner bore wall (normals pointing inward)
            pb0 = (cx + r_in * c0, cy + r_in * s0, z_in_back)
            pb1 = (cx + r_in * c1, cy + r_in * s1, z_in_back)
            ni0, ni1 = (-c0, -s0, 0.0), (-c1, -s1, 0.0)
            self._emit_tri(mat_inner, bone, pi0, pi1, pb1, ni0, ni1, ni1, uv_inner[:2], (uv_inner[2], uv_inner[1]), uv_inner[2:])
            self._emit_tri(mat_inner, bone, pi0, pb1, pb0, ni0, ni1, ni0, uv_inner[:2], uv_inner[2:], (uv_inner[0], uv_inner[3]))
            # Dark bore back cap
            self._emit_tri(mat_inner, bone, (cx, cy, z_in_back), pb0, pb1, (0, 0, 1), (0, 0, 1), (0, 0, 1),
                           uv_inner[:2], (uv_inner[2], uv_inner[1]), uv_inner[2:])


def triangulate_2d(pts):
    """Simple ear-clipping triangulation of a CCW 2D polygon [(x,y), ...]."""
    idx = list(range(len(pts)))
    out = []
    guard = 0
    while len(idx) > 3 and guard < 1000:
        guard += 1
        clipped = False
        m = len(idx)
        for i in range(m):
            a, b, c = idx[(i - 1) % m], idx[i], idx[(i + 1) % m]
            ax, ay = pts[a]; bx, by = pts[b]; cx, cy = pts[c]
            cross = (bx - ax) * (cy - ay) - (by - ay) * (cx - ax)
            if cross <= 1e-10:
                continue
            # Ensure no other vertex lies inside triangle (a, b, c)
            inside = False
            for k in idx:
                if k in (a, b, c):
                    continue
                px, py = pts[k]
                c1 = (bx - ax) * (py - ay) - (by - ay) * (px - ax)
                c2 = (cx - bx) * (py - by) - (cy - by) * (px - bx)
                c3 = (ax - cx) * (py - cy) - (ay - cy) * (px - cx)
                if c1 >= -1e-9 and c2 >= -1e-9 and c3 >= -1e-9:
                    inside = True
                    break
            if not inside:
                out.append((a, b, c))
                idx.pop(i)
                clipped = True
                break
        if not clipped:
            break
    if len(idx) == 3:
        out.append((idx[0], idx[1], idx[2]))
    return out


# ------------------------------------------------------------------ SMD & QC writing
def write_smd_multi(path, names, parents, frames, tris=None, place=None):
    """Write SMD with multi-material triangles (strictly CCW outward; no reversal applied).
    tris: list of (mat_name, [(local_pos, local_nrm, uv, bone_name) x 3])
    place: callable(bone_name) -> 4x4 world matrix in reference pose
    """
    idx = {n: i for i, n in enumerate(names)}
    out = ["version 1", "nodes"]
    for i, n in enumerate(names):
        out.append('%d "%s" %d' % (i, n, idx[parents[i]] if parents[i] else -1))
    out += ["end", "skeleton"]
    for t, Wf in enumerate(frames):
        out.append("time %d" % t)
        for i, n in enumerate(names):
            L = inv(Wf[parents[i]]) @ Wf[n] if parents[i] else Wf[n]
            e = m_euler(L[:3, :3])
            out.append("%d %.6f %.6f %.6f %.6f %.6f %.6f" % (i, L[0, 3], L[1, 3], L[2, 3], e[0], e[1], e[2]))
    out.append("end")
    if tris is not None:
        out.append("triangles")
        for mat, corners in tris:
            out.append(mat)
            for p, n, uv, bone in corners:
                M = place(bone)
                wp = M @ np.r_[p, 1.0]
                wn = norm(M[:3, :3] @ n)
                bi = idx[bone]
                out.append(
                    "%d %.6f %.6f %.6f %.6f %.6f %.6f %.6f %.6f 1 %d 1.0"
                    % (bi, wp[0], wp[1], wp[2], wn[0], wn[1], wn[2], uv[0], 1.0 - uv[1], bi)
                )
        out.append("end")
    with open(path, "w", encoding="ascii", newline="\n") as f:
        f.write("\n".join(out) + "\n")


def write_phys_hull(path, root_name, H, tris, place):
    he = m_euler(H[:3, :3])
    lines = [
        "version 1",
        "nodes",
        '0 "%s" -1' % root_name,
        "end",
        "skeleton",
        "time 0",
        "0 %.6f %.6f %.6f %.6f %.6f %.6f" % (H[0, 3], H[1, 3], H[2, 3], he[0], he[1], he[2]),
        "end",
        "triangles",
    ]
    for _mat, corners in tris:
        lines.append("phy")
        for p, _n, _uv, bone in corners:
            q = place(bone) @ np.r_[p, 1.0]
            lines.append("0 %.6f %.6f %.6f 0 0 1 0 0" % (q[0], q[1], q[2]))
    lines.append("end")
    with open(path, "w", encoding="ascii", newline="\n") as f:
        f.write("\n".join(lines) + "\n")


def att_line(name, bone, M):
    p, y, r = dec.matrix_angles(M[:3, :3])
    return '$attachment "%s" "%s" %.4f %.4f %.4f rotate %.3f %.3f %.3f' % (
        name, bone, M[0, 3], M[1, 3], M[2, 3], p, y, r
    )


# ------------------------------------------------------------------ VTF / Texture / PBR helpers
def vtf_to_rgba(data):
    """Decode any VTF (including CS:GO v7.5 DXT1/DXT5) into PIL RGBA Image."""
    sig, vmaj, vmin, hsize, w, h, flags, frames, first = struct.unpack("<4sIIIHHIHH", data[:28])
    assert sig == b"VTF\0"
    fmt, mips, lrfmt, lrw, lrh = struct.unpack("<iBiBB", data[52:63])
    offset = hsize
    if vmin < 3:
        offset += vpktex.mip_size(lrw, lrh, lrfmt) if lrfmt >= 0 else 0
    else:
        nres = struct.unpack("<I", data[68:72])[0]
        for i in range(nres):
            tag, rflags, roff = struct.unpack("<3sBI", data[80 + i * 8 : 88 + i * 8])
            if tag == b"\x30\0\0":
                offset = roff
    for m in range(mips - 1, 0, -1):
        mw, mh = max(1, w >> m), max(1, h >> m)
        offset += vpktex.mip_size(mw, mh, fmt) * frames
    size = vpktex.mip_size(w, h, fmt)
    raw = data[offset : offset + size]
    name = vpktex.FMT[fmt][0]
    if name.startswith("DXT"):
        fourcc = name.encode()
        hdr = struct.pack(
            "<4s7I44s2I4s5I5I",
            b"DDS ", 124, 0x1 | 0x2 | 0x4 | 0x1000 | 0x80000,
            h, w, len(raw), 0, 1, b"\0" * 44, 32, 0x4, fourcc,
            0, 0, 0, 0, 0, 0x1000, 0, 0, 0, 0,
        )
        return Image.open(io.BytesIO(hdr + raw)).convert("RGBA")
    mode = {
        "RGBA8888": ("RGBA", "RGBA"),
        "RGB888": ("RGB", "RGB"),
        "BGR888": ("RGB", "BGR"),
        "BGRA8888": ("RGBA", "BGRA"),
        "BGRX8888": ("RGBA", "BGRX"),
    }[name]
    return Image.frombytes(mode[0], (w, h), raw, "raw", mode[1]).convert("RGBA")


def height_to_dx_normal(height_f, strength=2.2):
    """Convert float height field (0..1, HxW) into DirectX tangent-space normal RGB uint8 (+X right, +Y down)."""
    h = height_f.astype(np.float32)
    dx = (np.roll(h, -1, axis=1) - np.roll(h, 1, axis=1)) * 0.5 * strength
    dy = (np.roll(h, -1, axis=0) - np.roll(h, 1, axis=0)) * 0.5 * strength
    # DirectX tangent space: +X right (slope -dx), +Y down (slope -dy -> flipped +dy in Source DX)
    nx = -dx
    ny = dy
    nz = np.ones_like(h)
    ln = np.sqrt(nx * nx + ny * ny + nz * nz)
    nx /= ln; ny /= ln; nz /= ln
    rgb = np.stack([(nx * 0.5 + 0.5) * 255.0, (ny * 0.5 + 0.5) * 255.0, (nz * 0.5 + 0.5) * 255.0], axis=-1)
    return np.clip(np.rint(rgb), 0, 255).astype(np.uint8)


def normal_mips(img):
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


def write_normal_gloss_vtf(path, normal_rgb, gloss_u8):
    """Write VTF v7.2 DXT5 normal map (RGB = DX normal, A = PBR gloss mask)."""
    img = np.dstack([normal_rgb.astype(np.uint8), gloss_u8.astype(np.uint8)])
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


def write_albedo_vtf(path, rgb_u8, alpha_u8=None, flags=0x10):
    """Write VTF v7.2 DXT1 (opaque) or DXT5 (with alpha)."""
    if alpha_u8 is None:
        rgba = np.concatenate([rgb_u8.astype(np.uint8), np.full(rgb_u8.shape[:2] + (1,), 255, np.uint8)], axis=-1)
        hd_textures.write_vtf(path, rgba, flags, False)
    else:
        rgba = np.concatenate([rgb_u8.astype(np.uint8), alpha_u8[..., None].astype(np.uint8)], axis=-1)
        hd_textures.write_vtf(path, rgba, flags | 0x2000, True)


def write_pbr_vmt(path, base_rel, bump_rel, metalness=0.35, specular=1.0, phong_exp=24, phong_boost=1.4,
                  selfillum_mask=None, selfillum_tint=None, extra=""):
    lines = [
        '"VertexLitGeneric"',
        "{",
        '\t"$basetexture"\t\t"%s"' % base_rel,
        '\t"$bumpmap"\t\t\t"%s"' % bump_rel,
        '\t"$surfaceprop"\t\t"weapon"',
        "",
        "\t// HL2RPM deferred PBR (gloss is stored in the alpha of the normal map)",
        '\t"$pbr_roughness"\t"0"',
        '\t"$pbr_metalness"\t"%.2f"' % metalness,
        '\t"$pbr_specular"\t\t"%.2f"' % specular,
        '\t"$pbr_glossmask"\t"1"',
        "",
        "\t// stock renderer fallback (-nodeferred)",
        '\t"$phong"\t\t\t"1"',
        '\t"$phongexponent"\t"%d"' % phong_exp,
        '\t"$phongboost"\t\t"%.2f"' % phong_boost,
        '\t"$phongfresnelranges"\t"[0.5 0.8 1]"',
        '\t"$normalmapalphaenvmapmask"\t"1"',
    ]
    if selfillum_mask:
        lines += [
            '\t"$selfillum"\t\t"1"',
            '\t"$selfillummask"\t"%s"' % selfillum_mask,
        ]
        if selfillum_tint:
            lines.append('\t"$selfillumtint"\t"%s"' % selfillum_tint)
    if extra:
        lines.append(extra)
    lines.append("}")
    with open(path, "w", encoding="ascii", newline="\n") as f:
        f.write("\n".join(lines) + "\n")


# ------------------------------------------------------------------ Audio helpers
def extract_wav(v, src_vpk_path, dst_path, force_mono=False):
    """Extract WAV from CS:GO VPK, optionally mixing stereo down to 16-bit mono PCM for 3D Source audio."""
    raw = v.read(src_vpk_path)
    if raw is None:
        raise FileNotFoundError("Missing CS:GO sound: " + src_vpk_path)
    os.makedirs(os.path.dirname(dst_path), exist_ok=True)
    if not force_mono:
        with open(dst_path, "wb") as f:
            f.write(raw)
        return
    with wave.open(io.BytesIO(raw), "rb") as w:
        ch = w.getnchannels()
        sw = w.getsampwidth()
        rate = w.getframerate()
        frames = w.readframes(w.getnframes())
    if ch == 1:
        with open(dst_path, "wb") as f:
            f.write(raw)
        return
    assert sw == 2, f"Unexpected WAV sampwidth {sw} in {src_vpk_path}"
    arr = np.frombuffer(frames, dtype=np.int16).reshape(-1, ch).astype(np.float32)
    mono = np.clip(np.rint(arr.mean(axis=1)), -32768, 32767).astype(np.int16)
    with wave.open(dst_path, "wb") as wo:
        wo.setnchannels(1)
        wo.setsampwidth(2)
        wo.setframerate(rate)
        wo.writeframes(mono.tobytes())


# ------------------------------------------------------------------ Offline visual preview renderer
def render_fp_preview(fs, out_path, shots, allframes, tris, mat_colors, muzzle, gun_root,
                      hands=VB_REF, fov=68.0, size=(720, 405)):
    """Render first-person eye camera with ValveBiped c_arms bonemerged exactly as the game does."""
    mh = dec.Model(fs, hands)
    Wh = fk_bind(mh)
    hm = []
    for _bn, _b, models in mh.bodyparts():
        if models and models[0][1]:
            for (_sr, P, N, U, I, Wt, B) in models[0][1]:
                hm.append((np.asarray(P, float), np.asarray(I).reshape(-1, 3), np.asarray(Wt, float), np.asarray(B)))
    W, H = size
    half = math.atan(math.tan(math.radians(fov / 2.0)) * (W / H) / (4.0 / 3.0))
    foc = (W / 2.0) / math.tan(half)
    panels = []
    for name, t in shots:
        fr = allframes[name][min(t, len(allframes[name]) - 1)]
        polys = []
        for mat, corners in tris:
            col = mat_colors.get(mat, (160, 168, 180))
            pts = np.array([(fr[bone] @ np.r_[p, 1.0])[:3] for p, _n, _uv, bone in corners])
            polys.append((pts, col, mat))
        for P, I, Wt, B in hm:
            Ms = [fr[nm] @ inv(Wh[bi]) if nm in fr else None for bi, nm in enumerate(mh.bone_names)]
            V = np.zeros((len(P), 3))
            for vi in range(len(P)):
                acc, tot = np.zeros(3), 0.0
                for k in range(3):
                    if Wt[vi][k] > 1e-5 and Ms[int(B[vi][k])] is not None:
                        acc += Wt[vi][k] * (Ms[int(B[vi][k])] @ np.r_[P[vi], 1.0])[:3]
                        tot += Wt[vi][k]
                V[vi] = acc / tot if tot else np.nan
            for tri in I:
                polys.append((V[tri], (200, 158, 130), "_hand"))
        zbuf = np.full((H, W), np.inf)
        img = np.full((H, W, 3), 38, np.uint8)
        additive = []
        for pts, col, mat in polys:
            if np.isnan(pts).any() or (pts[:, 0] < 0.8).any():
                continue
            if "reticle" in mat or "glass" in mat:
                additive.append((pts, col, mat))
                continue
            n = np.cross(pts[1] - pts[0], pts[2] - pts[0])
            ln = np.linalg.norm(n)
            if ln < 1e-9:
                continue
            shade = 0.30 + 0.70 * abs(np.dot(n / ln, norm(-pts.mean(0) + np.array([0, 4, 8]))))
            sx = W / 2 - foc * pts[:, 1] / pts[:, 0]
            sy = H / 2 - foc * pts[:, 2] / pts[:, 0]
            x0, x1 = int(max(0, math.floor(sx.min()))), int(min(W - 1, math.ceil(sx.max())))
            y0, y1 = int(max(0, math.floor(sy.min()))), int(min(H - 1, math.ceil(sy.max())))
            if x0 > x1 or y0 > y1:
                continue
            xs, ys = np.meshgrid(np.arange(x0, x1 + 1) + 0.5, np.arange(y0, y1 + 1) + 0.5)
            d = (sy[1] - sy[2]) * (sx[0] - sx[2]) + (sx[2] - sx[1]) * (sy[0] - sy[2])
            if abs(d) < 1e-9:
                continue
            a = ((sy[1] - sy[2]) * (xs - sx[2]) + (sx[2] - sx[1]) * (ys - sy[2])) / d
            b = ((sy[2] - sy[0]) * (xs - sx[2]) + (sx[0] - sx[2]) * (ys - sy[2])) / d
            c = 1.0 - a - b
            inside = (a >= 0) & (b >= 0) & (c >= 0)
            if not inside.any():
                continue
            z = a * pts[0, 0] + b * pts[1, 0] + c * pts[2, 0]
            sub = zbuf[y0 : y1 + 1, x0 : x1 + 1]
            win = inside & (z < sub)
            sub[win] = z[win]
            img[y0 : y1 + 1, x0 : x1 + 1][win] = np.clip(np.array(col) * shade, 0, 255).astype(np.uint8)
        im = Image.fromarray(img)
        dr = ImageDraw.Draw(im)
        for pts, col, mat in additive:
            sx = W / 2 - foc * pts[:, 1] / pts[:, 0]
            sy = H / 2 - foc * pts[:, 2] / pts[:, 0]
            if "reticle" in mat:
                cx, cy = float(sx.mean()), float(sy.mean())
                dr.ellipse((cx - 10, cy - 10, cx + 10, cy + 10), outline=(255, 60, 40), width=2)
                dr.ellipse((cx - 2, cy - 2, cx + 2, cy + 2), fill=(255, 220, 180))
        mz = fr[gun_root] @ muzzle
        p0, p1 = mz[:3, 3], mz[:3, 3] + mz[:3, 0] * 3.0
        if p0[0] > 1 and p1[0] > 1:
            dr.line(
                [(W / 2 - foc * p0[1] / p0[0], H / 2 - foc * p0[2] / p0[0]),
                 (W / 2 - foc * p1[1] / p1[0], H / 2 - foc * p1[2] / p1[0])],
                fill=(255, 220, 0), width=2,
            )
        dr.text((8, 6), "%s f%d (fov %.0f)" % (name, t, fov), fill=(255, 255, 255))
        panels.append(im)
    cols = 3
    rows = (len(panels) + cols - 1) // cols
    sheet = Image.new("RGB", (W * cols, H * rows))
    for i, im in enumerate(panels):
        sheet.paste(im, ((i % cols) * W, (i // cols) * H))
    os.makedirs(os.path.dirname(out_path), exist_ok=True)
    sheet.save(out_path)
    print("preview:", out_path)


def render_gun_turntable(out_path, tris, bone_to_parent, mat_textures, title="", size=(700, 500)):
    """Textured 3D close-up multi-angle render of a weapon (left side, right side, 3/4 front-left, 3/4 rear-left)
    in gun-parent space (+Z fwd, +Y up, +X left) to inspect textures, logos, holosight & Match parts!"""
    W, H = size
    # Gather vertices in gun-parent space
    world_tris = []
    all_pts = []
    for mat, corners in tris:
        pts = np.array([(bone_to_parent[bone] @ np.r_[p, 1.0])[:3] for p, _n, _uv, bone in corners])
        uvs = np.array([uv for _p, _n, uv, _b in corners])
        world_tris.append((mat, pts, uvs))
        all_pts.append(pts)
    all_pts = np.concatenate(all_pts, axis=0)
    ctr = (all_pts.min(0) + all_pts.max(0)) * 0.5
    rad = float(np.linalg.norm(all_pts.max(0) - all_pts.min(0))) * 0.55

    # Camera views (eye dir in gun-parent space +Z fwd, +Y up, +X left)
    views = [
        ("Left (+X)", np.array([1.0, 0.08, 0.0])),
        ("Right (-X)", np.array([-1.0, 0.08, 0.0])),
        ("3/4 Front-Left", norm([0.75, 0.25, 0.72])),
        ("3/4 Rear-Left (FPS angle)", norm([0.65, 0.28, -0.78])),
    ]
    panels = []
    for vname, vdir in views:
        eye = ctr + vdir * (rad * 2.8)
        fwd = norm(ctr - eye)
        right = norm(np.cross(fwd, np.array([0.0, 1.0, 0.0])))
        up = np.cross(right, fwd)
        foc = W * 1.15
        zbuf = np.full((H, W), np.inf)
        img = np.full((H, W, 3), 34, np.uint8)
        light_dir = norm(vdir * 0.6 + np.array([0.3, 0.85, 0.4]))
        for mat, pts, uvs in world_tris:
            if "glass" in mat or "reticle" in mat:
                continue
            rel = pts - eye
            cz = rel @ fwd
            if (cz < 0.2).any():
                continue
            cx = rel @ right
            cy = rel @ up
            sx = W / 2 + foc * cx / cz
            sy = H / 2 - foc * cy / cz
            n = np.cross(pts[1] - pts[0], pts[2] - pts[0])
            ln = np.linalg.norm(n)
            if ln < 1e-10:
                continue
            nn = n / ln
            if np.dot(nn, pts.mean(0) - eye) > 0:
                continue  # backface cull!
            x0, x1 = int(max(0, math.floor(sx.min()))), int(min(W - 1, math.ceil(sx.max())))
            y0, y1 = int(max(0, math.floor(sy.min()))), int(min(H - 1, math.ceil(sy.max())))
            if x0 > x1 or y0 > y1:
                continue
            xs, ys = np.meshgrid(np.arange(x0, x1 + 1) + 0.5, np.arange(y0, y1 + 1) + 0.5)
            d = (sy[1] - sy[2]) * (sx[0] - sx[2]) + (sx[2] - sx[1]) * (sy[0] - sy[2])
            if abs(d) < 1e-9:
                continue
            a = ((sy[1] - sy[2]) * (xs - sx[2]) + (sx[2] - sx[1]) * (ys - sy[2])) / d
            b = ((sy[2] - sy[0]) * (xs - sx[2]) + (sx[0] - sx[2]) * (ys - sy[2])) / d
            c = 1.0 - a - b
            inside = (a >= 0) & (b >= 0) & (c >= 0)
            if not inside.any():
                continue
            z = a * cz[0] + b * cz[1] + c * cz[2]
            sub = zbuf[y0 : y1 + 1, x0 : x1 + 1]
            win = inside & (z < sub)
            if not win.any():
                continue
            sub[win] = z[win]
            diff = 0.35 + 0.65 * max(0.0, float(np.dot(nn, light_dir)))
            hv = norm(light_dir - fwd)
            spec = (max(0.0, float(np.dot(nn, hv))) ** 18) * 55.0
            tex = mat_textures.get(mat)
            if tex is not None:
                th, tw = tex.shape[:2]
                uu = (a[win] * uvs[0, 0] + b[win] * uvs[1, 0] + c[win] * uvs[2, 0]) % 1.0
                vv = (a[win] * uvs[0, 1] + b[win] * uvs[1, 1] + c[win] * uvs[2, 1]) % 1.0
                tx = np.clip((uu * (tw - 1)).astype(np.int32), 0, tw - 1)
                ty = np.clip((vv * (th - 1)).astype(np.int32), 0, th - 1)
                col = tex[ty, tx].astype(np.float32) * diff + spec
            else:
                col = np.full((win.sum(), 3), 160.0 * diff + spec, np.float32)
            img[y0 : y1 + 1, x0 : x1 + 1][win] = np.clip(col, 0, 255).astype(np.uint8)
        im = Image.fromarray(img)
        ImageDraw.Draw(im).text((10, 8), f"{title} — {vname}", fill=(255, 255, 255))
        panels.append(im)
    sheet = Image.new("RGB", (W * 2, H * 2))
    for i, im in enumerate(panels):
        sheet.paste(im, ((i % 2) * W, (i // 2) * H))
    os.makedirs(os.path.dirname(out_path), exist_ok=True)
    sheet.save(out_path)
    print("turntable:", out_path)
