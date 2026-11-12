# -*- coding: utf-8 -*-
"""HL2RPM: the suits as items lying on the ground, as armour lies in New Vegas or S.T.A.L.K.E.R.:
the cloth folded in a pile, the hard parts - the helmet or the mask, the chest plate, an air tank -
next to it or on top (the user, 2026-11-02: "not mannequins on a stand").

Everything is of the character that wears the suit, with the textures of the game:
- the pile: a folded jacket on folded trousers (+ a folded sleeve across the top), soft slabs made
  here; their faces are mapped onto a piece of the character's own texture - the biggest square
  inside the triangles of its torso / of its legs (so the cloth is the uniform's cloth);
- the hard parts: the triangles of the character's head (helmet, mask, hood), of the front of its
  torso (the chest plate) or of a body group (the air tank of the hazmat suit), in the rest pose,
  made two-sided (a shell has no inside of its own);
- one bone, a box for physics, $staticprop; the materials are the character's.

    python make_suit_piles.py [suit_hev suit_pcv suit_cp suit_soldier suit_prison suit_elite suit_worker suit_hazmat]
                              [--preview <dir>] [--no-compile]

Sources: modelsrc\\hl2rpm_items\\<model>; results: hl2rpm\\models\\hl2rpm\\items\\<model>.mdl.
--preview renders each in Blender (blender_preview.py) to <dir>/<model>.png.
"""
import math
import os
import subprocess
import sys

import numpy as np
from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
TOOLS = os.path.dirname(HERE)
sys.path.insert(0, HERE)
sys.path.insert(0, TOOLS)
sys.path.insert(0, os.path.join(TOOLS, "dialog_editor"))
import make_item_models as items  # noqa: E402
import make_vm_hands as vmh  # noqa: E402  (fs, material_texture)
import mdl_decompile as md  # noqa: E402

BLENDER = items.BLENDER

SUITS = {
    "suit_hev": dict(src="models/hl2rpm/characters/hev_mk5.mdl", skin=0, hard=["head", "chest"], prop="metal", mass=22, yaw=20),
    "suit_pcv": dict(src="models/hl2rpm/characters/hev_mk5.mdl", skin=1, hard=["head", "chest"], prop="metal", mass=22, yaw=-15),
    "suit_cp": dict(src="models/police.mdl", hard=["head"], prop="cloth", mass=8, yaw=10, skip=["manhack"]),
    "suit_soldier": dict(src="models/combine_soldier.mdl", hard=["head", "chest"], prop="metal", mass=18, yaw=-25),
    "suit_prison": dict(src="models/combine_soldier_prisonguard.mdl", hard=["head", "chest"], prop="metal", mass=18, yaw=30),
    "suit_elite": dict(src="models/combine_super_soldier.mdl", hard=["head", "chest"], prop="metal", mass=20, yaw=-10),
    "suit_worker": dict(src="models/puntleycmb/workernpc.mdl", hard=["head"], prop="cloth", mass=7, yaw=15),
    # (its torso is mostly the harness of the tank: the folded suit takes the yellow cloth of the legs)
    "suit_hazmat": dict(src="models/hl2_combine_hazmat_cit.mdl", hard=["head", "group:Backpack"], prop="rubber", mass=12, yaw=-20,
                        groups={"Hood": 0}, jacket="legs"),
}

HEAD = ("head",)          # (not the neck: its triangles run down into the collar in spikes)
TORSO = ("spine", "clavicle")
CHEST = ("spine1", "spine2", "spine4")
ARMS = ("upperarm",)
LEGS = ("thigh", "calf")


# ------------------------------------------------------------------ the character
class Part:
    """triangles of one material: pos (n,3), nrm (n,3), uv (n,2), idx (m*3,)"""

    def __init__(self, mat, pos, nrm, uv, idx):
        self.mat, self.pos, self.nrm, self.uv, self.idx = mat, np.asarray(pos, np.float64), np.asarray(nrm, np.float64), np.asarray(uv, np.float64), np.asarray(idx, np.int64)

    def copy(self):
        return Part(self.mat, self.pos.copy(), self.nrm.copy(), self.uv.copy(), self.idx.copy())


def bone_kind(name):
    n = name.lower()
    if any(n.endswith(k) for k in CHEST):
        return "chest"
    for kind, keys in (("head", HEAD), ("arms", ARMS), ("torso", TORSO), ("legs", LEGS)):
        if any(k in n for k in keys):
            return kind
    return "other"


def character(cfg):
    """the meshes of the default body groups: [(material, P, N, U, I, kind per vertex)], the group meshes by name"""
    m = md.Model(vmh.fs(), cfg["src"])
    fam = m.skins[min(cfg.get("skin", 0), len(m.skins) - 1)]
    kinds = [bone_kind(b) for b in m.bone_names]
    body, groups = [], {}
    for bname, _base, models in m.bodyparts():
        pick = cfg.get("groups", {}).get(bname, 0)
        if not models:
            continue
        mi = pick if pick < len(models) and models[pick][1] else 0
        for (skinref, P, N, U, I, W, B) in models[mi][1]:
            mat = m.textures[fam[skinref]]
            if any(s in mat.lower() for s in cfg.get("skip", [])):
                continue
            dom = B[np.arange(len(B)), np.argmax(W, 1)]
            kv = np.array([kinds[int(b)] for b in dom])
            entry = (mat, P, N, U, I, kv)
            groups.setdefault(bname, []).append(entry)
            body.append(entry)
    return m, body, groups


def extract(entries, keep_tri):
    """Parts of the triangles keep_tri(P, N, kinds, tri) says yes to"""
    out = []
    for (mat, P, N, U, I, kv) in entries:
        tris = I.reshape(-1, 3)
        sel = np.array([keep_tri(P, N, kv, t) for t in tris], bool) if len(tris) else np.zeros(0, bool)
        if not sel.any():
            continue
        t = tris[sel]
        used, inv = np.unique(t.reshape(-1), return_inverse=True)
        out.append(Part(mat, P[used], N[used], U[used], inv))
    return out


def main_pieces(parts, keep_frac=0.25):
    """only the big connected pieces (stray triangles of a selection go): vertices welded by position"""
    key_id = {}
    tri_parts = []
    for pi, p in enumerate(parts):
        keys = [key_id.setdefault(tuple(np.round(v, 2)), len(key_id)) for v in p.pos]
        for t in p.idx.reshape(-1, 3):
            tri_parts.append((pi, t, [keys[i] for i in t]))
    parent = list(range(len(key_id)))

    def find(a):
        while parent[a] != a:
            parent[a] = parent[parent[a]]
            a = parent[a]
        return a
    for _pi, _t, ks in tri_parts:
        a = find(ks[0])
        for k in ks[1:]:
            b = find(k)
            if a != b:
                parent[b] = a
    comp = {}
    for i, (_pi, _t, ks) in enumerate(tri_parts):
        comp.setdefault(find(ks[0]), []).append(i)
    if not comp:
        return parts
    big = max(len(v) for v in comp.values())
    keep = set()
    for v in comp.values():
        if len(v) >= big * keep_frac:
            keep.update(v)
    out = []
    for pi, p in enumerate(parts):
        sel = [t for i, (pj, t, _k) in enumerate(tri_parts) if pj == pi and i in keep]
        if not sel:
            continue
        t = np.array(sel)
        used, inv = np.unique(t.reshape(-1), return_inverse=True)
        out.append(Part(p.mat, p.pos[used], p.nrm[used], p.uv[used], inv))
    return out


def two_sided(parts, inset=0.06):
    """each triangle again, the other way round, a hair inside: a shell seen from inside is not empty"""
    res = []
    for p in parts:
        q = p.copy()
        q.pos = q.pos - q.nrm * inset
        q.nrm = -q.nrm
        q.idx = q.idx.reshape(-1, 3)[:, ::-1].reshape(-1)
        res.append(p)
        res.append(q)
    return res


def bounds(parts):
    lo = np.min([p.pos.min(0) for p in parts], 0)
    hi = np.max([p.pos.max(0) for p in parts], 0)
    return lo, hi


def transform(parts, R=None, t=(0, 0, 0)):
    R = np.eye(3) if R is None else np.asarray(R, np.float64)
    out = []
    for p in parts:
        q = p.copy()
        q.pos = q.pos @ R.T + np.asarray(t, np.float64)
        q.nrm = q.nrm @ R.T
        out.append(q)
    return out


def rot_z(deg):
    a = math.radians(deg)
    return np.array([[math.cos(a), -math.sin(a), 0], [math.sin(a), math.cos(a), 0], [0, 0, 1]])


def rot_y(deg):
    a = math.radians(deg)
    return np.array([[math.cos(a), 0, math.sin(a)], [0, 1, 0], [-math.sin(a), 0, math.cos(a)]])


def rot_x(deg):
    a = math.radians(deg)
    return np.array([[1, 0, 0], [0, math.cos(a), -math.sin(a)], [0, math.sin(a), math.cos(a)]])


# ------------------------------------------------------------------ the cloth of the suit
_TEX = {}


def texture(m, mat):
    if mat not in _TEX:
        _TEX[mat] = vmh.material_texture(m.cdmaterials, mat)
    return _TEX[mat]


def cloth_patch(m, body, kinds):
    """(material, (u0, v0, u1, v1)) - a square of the texture inside the triangles of those bones"""
    best = None
    for (mat, P, N, U, I, kv) in body:
        tris = [t for t in I.reshape(-1, 3) if all(kv[v] in kinds for v in t)]
        if not tris:
            continue
        area = sum(abs((U[b][0] - U[a][0]) * (U[c][1] - U[a][1]) - (U[c][0] - U[a][0]) * (U[b][1] - U[a][1])) for a, b, c in tris)
        if best is None or area > best[0]:
            best = (area, mat, U, tris)
    if best is None:
        return None
    _a, mat, U, tris = best
    img = texture(m, mat)
    S = 512
    if img is not None:
        h, w = img.shape[:2]
    else:
        h = w = S
    mk = Image.new("L", (w, h), 0)
    d = ImageDraw.Draw(mk)
    for a, b, c in tris:
        d.polygon([((U[v][0] % 1.0) * w, (U[v][1] % 1.0) * h) for v in (a, b, c)], fill=255)
    mask = np.asarray(mk) > 0
    y0, x0, s = vmh.largest_square(mask)
    y0, x0 = max(0, y0), max(0, x0)
    s = min(s, w - x0, h - y0)
    # a hair inside: the mip levels must not reach past the piece
    pad = max(1, s // 16)
    return mat, ((x0 + pad) / w, (y0 + pad) / h, (x0 + s - pad) / w, (y0 + s - pad) / h)


# ------------------------------------------------------------------ folded cloth
def noise2(x, y, seed, scale):
    r = np.random.default_rng(seed)
    v = np.zeros_like(x)
    for i in range(5):
        f = (1.7 ** i) / scale
        a, b, ph = r.uniform(-1, 1), r.uniform(-1, 1), r.uniform(0, 6.28)
        n = math.hypot(a, b) or 1
        v += np.sin((a * x + b * y) / n * f * 6.28 + ph) / (1.6 ** i)
    return v


def slab(size, radius, rect, mat, seed, n=(28, 22, 10), creases=4, wrinkle=0.14, sag=0.0, layers=3):
    """a folded piece of cloth: a box with rounded edges (the fold is round, the cloth thick),
    creases and small wrinkles on the top; its faces mapped onto rect of the texture"""
    sx, sy, sz = size
    h = np.array([sx, sy, sz], np.float64) / 2
    r = np.minimum(np.array([radius, radius, radius], np.float64), h * 0.98)
    hin = h - r
    u0, v0, u1, v1 = rect
    span = max(sx, sy)
    rng = np.random.default_rng(seed)
    lines = [(rng.uniform(-sx / 2, sx / 2), rng.uniform(-sy / 2, sy / 2), rng.uniform(0, math.pi)) for _ in range(creases)]
    P, N, UV, I = [], [], [], []
    base = 0
    for axis in range(3):
        for sign in (-1, 1):
            a1, a2 = [k for k in range(3) if k != axis]
            na, nb = n[a1], n[a2]
            ga = np.linspace(-h[a1], h[a1], na + 1)
            gb = np.linspace(-h[a2], h[a2], nb + 1)
            A, B = np.meshgrid(ga, gb, indexing="ij")
            p = np.zeros(A.shape + (3,))
            p[..., axis] = sign * h[axis]
            p[..., a1] = A
            p[..., a2] = B
            p = p.reshape(-1, 3)
            c = np.clip(p, -hin, hin)
            dvec = p - c
            dl = np.linalg.norm(dvec / r, axis=1, keepdims=True)
            dn = (dvec / r) / np.maximum(dl, 1e-9)
            q = c + dn * r
            nr = dn / r
            nr /= np.linalg.norm(nr, axis=1, keepdims=True)
            # the top: creases (lines pressed in) and small wrinkles, more in the middle than at the folds
            top = np.clip(nr[:, 2], 0, 1) ** 2
            inner = np.clip(1 - np.maximum(np.abs(q[:, 0]) / h[0], np.abs(q[:, 1]) / h[1]), 0, 1) ** 0.5
            disp = wrinkle * noise2(q[:, 0], q[:, 1], seed, span * 0.35) * 0.5
            for (lx, ly, la) in lines:
                dd = (q[:, 0] - lx) * math.sin(la) - (q[:, 1] - ly) * math.cos(la)
                disp -= 0.3 * np.exp(-(dd / 0.4) ** 2)
            disp += sag * (1 - (q[:, 0] / h[0]) ** 2) * (1 - (q[:, 1] / h[1]) ** 2)
            q = q + nr * (disp * top * inner)[:, None]
            # the sides: the layers of the folded cloth (grooves between them), a little uneven
            side = np.clip(1 - np.abs(nr[:, 2]) * 1.6, 0, 1)
            if layers:
                zz = (q[:, 2] + h[2]) / (2 * h[2]) * layers + 0.15 * noise2(q[:, 0], q[:, 1], seed + 5, span * 0.5)
                groove = np.clip(np.cos(zz * 2 * math.pi), 0, 1) ** 6
                q = q - nr * (0.16 * groove * side)[:, None]
            # uv: the top and the bottom from above, the sides across - one texel size everywhere
            if axis == 2:
                uu, vv = (q[:, 0] + span / 2) / span, (q[:, 1] + span / 2) / span
            elif axis == 0:
                uu, vv = (q[:, 1] + span / 2) / span, (q[:, 2] + span / 2) / span
            else:
                uu, vv = (q[:, 0] + span / 2) / span, (q[:, 2] + span / 2) / span
            uv = np.stack([u0 + np.clip(uu, 0, 1) * (u1 - u0), v0 + np.clip(vv, 0, 1) * (v1 - v0)], 1)
            P.append(q)
            N.append(nr)
            UV.append(uv)
            for i in range(na):
                for j in range(nb):
                    k = base + i * (nb + 1) + j
                    quad = (k, k + nb + 1, k + nb + 2, k + 1)
                    if (sign > 0) != (axis == 1):
                        quad = quad[::-1]
                    I += [quad[0], quad[1], quad[2], quad[0], quad[2], quad[3]]
            base += (na + 1) * (nb + 1)
    part = Part(mat, np.concatenate(P), np.concatenate(N), np.concatenate(UV), np.array(I))
    fix_winding(part)
    return part


WINDING = [1.0]     # the turn of a studio mesh's triangles against their normals (measured on the character)


def winding_sign(P, N, I):
    t = I.reshape(-1, 3)
    fn = np.cross(P[t[:, 1]] - P[t[:, 0]], P[t[:, 2]] - P[t[:, 0]])
    vn = N[t[:, 0]] + N[t[:, 1]] + N[t[:, 2]]
    return float(np.sign(np.median(np.sign((fn * vn).sum(1)))))


def fix_winding(part):
    """triangles turned as the character's are (the SMD writer reverses them all)"""
    t = part.idx.reshape(-1, 3)
    a, b, c = part.pos[t[:, 0]], part.pos[t[:, 1]], part.pos[t[:, 2]]
    fn = np.cross(b - a, c - a)
    vn = part.nrm[t[:, 0]] + part.nrm[t[:, 1]] + part.nrm[t[:, 2]]
    flip = (fn * vn).sum(1) * WINDING[0] < 0
    t[flip] = t[flip][:, ::-1]
    part.idx = t.reshape(-1)


def pile(m, body, seed, cfg=None):
    """trousers folded under, the jacket folded on top, a sleeve folded across it"""
    jacket = None
    if (cfg or {}).get("jacket") != "legs":
        jacket = cloth_patch(m, body, ("torso", "chest", "arms"))
    jacket = jacket or cloth_patch(m, body, ("legs",))
    pants = cloth_patch(m, body, ("legs",)) or jacket
    sleeve = jacket
    parts = []
    p1 = slab((18.0, 13.5, 2.6), 1.15, pants[1], pants[0], seed, sag=0.15)
    parts += transform([p1], rot_z(-4), (0, 0, 1.3))
    p2 = slab((15.5, 12.0, 2.4), 1.05, jacket[1], jacket[0], seed + 1, sag=0.25)
    parts += transform([p2], rot_z(5), (0.4, -0.2, 2.6 + 1.15))
    p3 = slab((13.5, 3.0, 1.5), 0.72, sleeve[1], sleeve[0], seed + 2, n=(20, 8, 6), creases=1, wrinkle=0.06, layers=0)
    parts += transform([p3], rot_z(-28), (-0.8, 2.2, 2.6 + 2.35 + 0.65))
    top = 2.6 + 2.4 + 0.1
    return parts, top


# ------------------------------------------------------------------ the hard parts
def head_parts(body):
    return extract(body, lambda P, N, kv, t: all(kv[v] == "head" for v in t))


def chest_parts(body):
    """the armour of the torso, taken off and laid down: the triangles of the chest and the back
    (spine, clavicles), pressed flat front to back - an empty vest lying on the ground, as armour
    lies in New Vegas (a character faces +X: its depth is X)"""
    parts = extract(body, lambda P, N, kv, t: all(kv[v] in ("chest", "torso") for v in t))
    parts = main_pieces(parts, 0.3)
    if not parts:
        return parts
    lo, hi = bounds(parts)
    cx = (lo[0] + hi[0]) / 2
    for p in parts:
        p.pos[:, 0] = cx + (p.pos[:, 0] - cx) * 0.3
        n = p.nrm.copy()
        n[:, 0] /= 0.3
        p.nrm = n / np.linalg.norm(n, axis=1, keepdims=True)
    return parts


def group_parts(groups, name, mat_key=None):
    entries = groups.get(name, [])
    if mat_key:
        # (the air tank without its harness: the straps would stick out of the pile in loops)
        only = [e for e in entries if mat_key in e[0].lower()]
        entries = only or entries
    return extract(entries, lambda P, N, kv, t: True)


def place_on(parts, z, at=(0, 0), R=None):
    """parts turned by R, their lowest point at height z, the middle of their footprint at 'at'"""
    q = transform(parts, R)
    lo, hi = bounds(q)
    return transform(q, None, (at[0] - (lo[0] + hi[0]) / 2, at[1] - (lo[1] + hi[1]) / 2, z - lo[2]))


def build(name, cfg):
    m, body, groups = character(cfg)
    WINDING[0] = winding_sign(body[0][1], body[0][2], body[0][4]) or 1.0
    seed = sum(map(ord, name))
    parts, top = pile(m, body, seed, cfg)
    lo, hi = bounds(parts)
    for kind in cfg["hard"]:
        if kind == "chest":
            chest = chest_parts(body)
            if chest:
                # on the ground beside the pile, face up (its front, +X, to the sky; the neck towards -X):
                # a curved plate rests on its edges
                cb = bounds(place_on(chest, 0.0, (0, 0), rot_y(-90)))
                wy = cb[1][1] - cb[0][1]
                parts += two_sided(place_on(chest, 0.0, (-1.5, lo[1] - wy * 0.5 - 1.5), rot_z(-12) @ rot_y(-90)))
        elif kind == "head":
            head = main_pieces(head_parts(body), 0.15)
            if head:
                hb_lo, hb_hi = bounds(head)
                w = max(hb_hi[0] - hb_lo[0], hb_hi[1] - hb_lo[1])
                # on the ground next to the pile, turned a little, as if put down
                parts += two_sided(place_on(head, 0.0, (hi[0] + w * 0.45 + 1.0, -3.5), rot_z(cfg.get("yaw", 0) + 155)))
        elif kind.startswith("group:"):
            g = main_pieces(group_parts(groups, kind[6:], "tank"), 0.3)
            if g:
                # the air tank: lying on its side behind the pile
                gb = bounds(transform(g, rot_z(80) @ rot_y(90)))
                parts += two_sided(place_on(g, 0.0, (-1.0, hi[1] + (gb[1][1] - gb[0][1]) / 2 + 1.0), rot_z(80) @ rot_y(90)))
    # the whole item turned for variety, centred, on the ground
    parts = transform(parts, rot_z(cfg.get("yaw", 0) * 0.2))
    lo, hi = bounds(parts)
    parts = transform(parts, None, (-(lo[0] + hi[0]) / 2, -(lo[1] + hi[1]) / 2, -lo[2]))
    return m, parts


# ------------------------------------------------------------------ output
def write_smd(path, parts):
    with open(path, "w", encoding="ascii", newline="\n") as f:
        f.write("version 1\nnodes\n0 \"root\" -1\nend\nskeleton\ntime 0\n0 0 0 0 0 0 0\nend\ntriangles\n")
        for p in parts:
            for tri in p.idx.reshape(-1, 3):
                f.write(p.mat + "\n")
                # (the order of the vertices of a studio mesh is the other way round to an SMD)
                for i in (tri[2], tri[1], tri[0]):
                    q, n, t = p.pos[i], p.nrm[i], p.uv[i]
                    f.write("0 %.5f %.5f %.5f %.5f %.5f %.5f %.5f %.5f\n" % (q[0], q[1], q[2], n[0], n[1], n[2], t[0], 1.0 - t[1]))
        f.write("end\n")


def compile_item(name, cfg, m, parts, work):
    write_smd(os.path.join(work, name + ".smd"), parts)
    lo, hi = bounds(parts)
    items.phys_smd(os.path.join(work, name + "_phys.smd"), lo, hi, parts[0].mat)
    with open(os.path.join(work, name + "_idle.smd"), "w", encoding="ascii", newline="\n") as f:
        f.write("version 1\nnodes\n0 \"root\" -1\nend\nskeleton\ntime 0\n0 0 0 0 0 0 0\nend\n")
    qc = '$modelname "hl2rpm/items/%s.mdl"\n$body "suit" "%s.smd"\n$staticprop\n$surfaceprop "%s"\n' % (name, name, cfg["prop"])
    for cd in m.cdmaterials:
        qc += '$cdmaterials "%s"\n' % cd.strip("/")
    qc += '$sequence "idle" "%s_idle.smd" fps 1\n$collisionmodel "%s_phys.smd"\n{\n\t$mass %d\n\t$rotdamping 2\n}\n' % (name, name, cfg["mass"])
    path = os.path.join(work, name + ".qc")
    with open(path, "w", encoding="ascii", newline="\n") as f:
        f.write(qc)
    r = subprocess.run([os.path.join(items.SDK_BIN, "studiomdl.exe"), "-game", items.MOD, "-nop4", path], cwd=work, capture_output=True, text=True, errors="replace")
    txt = r.stdout + r.stderr
    bad = [l for l in txt.splitlines() if "ERROR" in l.upper() or ("WARNING" in l.upper() and "bone" not in l.lower())]
    ok = r.returncode == 0 and os.path.exists(os.path.join(items.MOD, "models", "hl2rpm", "items", name + ".phy"))
    print("  %s: %.0f x %.0f x %.0f units, %d triangles - %s" % (name, *(hi - lo), sum(len(p.idx) // 3 for p in parts), "ok" if ok else "FAILED"))
    for l in bad[:8]:
        print("    " + l[:160])
    if not ok:
        print(txt[-1500:])


def preview(name, m, parts, out_dir, work):
    """the item rendered in Blender (blender_preview.py): parts and their textures as PNG"""
    os.makedirs(out_dir, exist_ok=True)
    mats = sorted({p.mat for p in parts})
    tex = {}
    for mat in mats:
        img = texture(m, mat)
        if img is not None:
            pth = os.path.join(work, "_prev_%s.png" % mat.replace("/", "_"))
            Image.fromarray(img[..., :3] if img.shape[2] >= 3 else img).save(pth)
            tex[mat] = pth
    npz = os.path.join(work, "_preview.npz")
    data = {}
    for i, p in enumerate(parts):
        data["pos%d" % i], data["nrm%d" % i], data["uv%d" % i], data["idx%d" % i] = p.pos, p.nrm, p.uv, p.idx
    data["mats"] = np.array([tex.get(p.mat, "") for p in parts])
    np.savez(npz, **data)
    out = os.path.join(out_dir, name + ".png")
    subprocess.run([BLENDER, "--background", "--factory-startup", "--python", os.path.join(HERE, "blender_preview.py"), "--", npz, out],
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    views = [out[:-4] + "_%d.png" % k for k in range(2)]
    if all(os.path.exists(v) for v in views):
        ims = [Image.open(v) for v in views]
        both = Image.new("RGB", (sum(i.width for i in ims), ims[0].height))
        x = 0
        for i in ims:
            both.paste(i.convert("RGB"), (x, 0))
            x += i.width
        both.save(out)
        for v in views:
            os.remove(v)
    print("  preview:", out)


def main(argv):
    names = [a for a in argv if a in SUITS] or list(SUITS)
    prev = argv[argv.index("--preview") + 1] if "--preview" in argv else None
    for name in names:
        cfg = SUITS[name]
        work = os.path.join(items.SRC, name)
        os.makedirs(work, exist_ok=True)
        print("== " + name)
        m, parts = build(name, cfg)
        if "--no-compile" not in argv:
            compile_item(name, cfg, m, parts, work)
        if prev:
            preview(name, m, parts, prev, work)


if __name__ == "__main__":
    main(sys.argv[1:])
