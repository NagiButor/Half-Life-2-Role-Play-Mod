"""The Beretta 92 of the mod (the user's "Dual Berettas" addon, rethought as one pistol):

    python make_beretta.py [--no-upscale] [--report]

The addon is only a Garry's Mod script around the Counter-Strike: Source dual Elites, so the model
comes from the CS:S install (cstrike_pak_dir.vpk):
- the gun: the right Beretta of v_pist_elite (gun, slide, hammer, trigger, magazine), its texture;
- the hands and their animations: v_pist_p228 - a single pistol held in both hands (idle, three
  shots, dry fire, reload with a magazine change, draw). CS:S view models are built for the left
  hand and mirrored by the game; here the animation is mirrored once into a right-handed one;
- the arms are not CS:S's: the animation is moved onto the ValveBiped arm bones of the Garry's Mod
  hands rig, and the mod's hands of the suit (models/hl2rpm/hands/*, make_hands.py) are bone-merged
  onto them ("uses_hands"). The bones do not have the same axes: each ValveBiped bone gets the turn
  that lines its own palm / finger frame (made of the joint positions: along the bone, across the
  palm) up with the CS:S one. CS:S hands are smaller than a character's: the whole animation is
  scaled about the eye (the picture does not change) so the joints fit the hands. CS:S has no upper
  arms: they are made from the elbow towards a shoulder behind the camera;
- the Beretta sits in the right hand as it does in the Elites' animation (the hand frame of the P228
  rig = the hand frame of the Elite rig; mirrored, the P228's "Left" bones are the right hand); its slide / hammer / trigger / magazine follow the P228's
  parts (each part's own turn and the shift of its origin, turned into the Beretta's frame);
- the sequences the weapon code plays (game/server/hl2rpm_beretta.cpp): idle, idle_empty (slide
  locked back), shoot1-3, shoot_last (the slide stays back), dryfire, reload (magazine change with a
  round in the chamber: the slide stays forward), reload_empty (the P228's: slide back, release),
  draw, draw_empty, holster;
- the world model: the same Beretta on ValveBiped.Bip01_R_Hand -> Weapon_bone, placed against the
  hand as in first person (NPCs and the player's body hold it on their own hand bone), a convex hull;
- sounds: the CS:S Elite's shot and magazine sounds (sound/weapons/hl2rpm_beretta), the sound
  scripts at the end of scripts/game_sounds_weapons.txt, scripts/weapon_rpm_beretta.txt.

Results: models/weapons/hl2rpm/{v,w}_beretta.mdl, materials/models/weapons/hl2rpm/beretta;
sources in modelsrc\\hl2rpm_weapons\\beretta. --report prints the fit (scale, joint errors).
"""
import math
import os
import shutil
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(HERE, "dialog_editor"))
import mdl_decompile as dec  # noqa: E402
from make_hands import VIEW_BONES  # noqa: E402
from dlgedit.world import gamefs, vpk  # noqa: E402

MOD = dec.MOD
CSS_VPK = r"E:\Steam\steamapps\common\Counter-Strike Source\cstrike\cstrike_pak_dir.vpk"
SRC = r"E:\XBLAH's modding tool\content\Mapbase\hl2rpm\modelsrc\hl2rpm_weapons\beretta"
MAT = "models/weapons/hl2rpm/beretta"
SND = "weapons/hl2rpm_beretta"
VB_REF = "models/hl2rpm/hands/citizen.mdl"      # the ValveBiped skeleton the hands are made on
FINGERS = ("Thumb", "Index", "Middle", "Ring", "Pinky")
PARTS = (("slide", "slide_right", "p228_Slide"), ("hammer", "hammer_right", "p228_Hammer"),
         ("trigger", "trigger_right", "p228_Trigger"), ("mag", "magazine_right", "p228_Clip"))
# where the shoulders are (scaled view model space: x forward, y left, z up; the eye at 0)
SHOULDER = {"R": np.array([-3.0, -9.0, -11.0]), "L": np.array([-3.0, 9.0, -11.0])}
MIRROR = np.diag([1.0, -1.0, 1.0, 1.0])
# CS:S builds its view models for the left hand (the game mirrors them): in the P228 the gun is in
# "Left_Hand". Mirrored, that hand is a right hand - the ValveBiped R side takes the CS:S Left bones.
P228_SIDE = {"R": "Left", "L": "Right"}


# ------------------------------------------------------------------ math
def euler_m(e):
    x, y, z = e
    cx, sx, cy, sy, cz, sz = math.cos(x), math.sin(x), math.cos(y), math.sin(y), math.cos(z), math.sin(z)
    return np.array([[cz, -sz, 0], [sz, cz, 0], [0, 0, 1]]) @ np.array([[cy, 0, sy], [0, 1, 0], [-sy, 0, cy]]) @ \
        np.array([[1, 0, 0], [0, cx, -sx], [0, sx, cx]])


def m_euler(R):
    p, y, r = dec.matrix_angles(R)
    return np.radians([r, p, y])


def fk(m, P, E):
    W = []
    for i in range(len(m.bone_names)):
        L = np.eye(4)
        L[:3, :3] = euler_m(E[i])
        L[:3, 3] = P[i]
        p = m.bone_parent[i]
        W.append(W[p] @ L if p >= 0 else L)
    return W


def norm(v):
    return v / max(np.linalg.norm(v), 1e-9)


def orth(a, x):
    return norm(a - np.dot(a, x) * x)


def frame(x, y, origin):
    M = np.eye(4)
    M[:3, 0], M[:3, 1], M[:3, 2], M[:3, 3] = x, y, np.cross(x, y), origin
    return M


def rot_only(M):
    M = M.copy()
    M[:3, 3] = 0
    return M


def inv(M):
    return np.linalg.inv(M)


def prepare(W, scale, mirror):
    out = []
    for M in W:
        M = MIRROR @ M @ MIRROR if mirror else M.copy()
        M[:3, 3] *= scale
        out.append(M)
    return out


# ------------------------------------------------------------------ rigs
def css_index(m, side):
    """the CS:S arm bones of a side ("Right" / "Left"): forearm, hand, fingers [5][3]"""
    key = {b.lower().replace("_", ""): i for i, b in enumerate(m.bone_names)}
    get = lambda part: key["vweapon.%s%s" % (side.lower(), part.lower())]
    return {"fore": get("arm"), "hand": get("hand"),
            "f": [[get("%s%02d" % (f, k)) for k in (1, 2, 3)] for f in FINGERS]}


def vb_names(s):
    b = "ValveBiped.Bip01_%s_" % s
    return {"fore": b + "Forearm", "hand": b + "Hand", "up": b + "UpperArm", "clav": b + "Clavicle",
            "f": [[b + "Finger%d%s" % (f, k) for k in ("", "1", "2")] for f in range(5)]}


def vb_index(m, s):
    low = {b.lower(): i for i, b in enumerate(m.bone_names)}
    n = vb_names(s)
    return {"fore": low[n["fore"].lower()], "hand": low[n["hand"].lower()], "up": low[n["up"].lower()],
            "clav": low[n["clav"].lower()], "f": [[low[x.lower()] for x in row] for row in n["f"]]}


def across(W, ix):
    """across the palm: index finger base minus little finger base"""
    return norm(W[ix["f"][1][0]][:3, 3] - W[ix["f"][4][0]][:3, 3])


def geo_frames(W, ix):
    """frames made of joint positions alone - the same on any rig: x along the bone, y across the palm"""
    P = lambda i: W[i][:3, 3]
    a = across(W, ix)
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


class Retarget:
    """CS:S arm bones -> ValveBiped ones: W_vb(t) = W_css(t) @ C (a turn per bone)"""

    def __init__(self, Wc, Wv, mc, mv):
        self.C, self.ic, self.iv, self.vb = {}, {}, {}, {}
        for s, side in P228_SIDE.items():
            ic, iv = css_index(mc, side), vb_index(mv, s)
            Gc, Gv = geo_frames(Wc, ic), geo_frames(Wv, iv)
            C = {}
            for key in ("fore", "hand"):
                C[key] = rot_only(inv(Wc[ic[key]]) @ Gc[key] @ inv(Gv[key]) @ Wv[iv[key]])
            for f in range(5):
                for k in range(2):
                    C[(f, k)] = rot_only(inv(Wc[ic["f"][f][k]]) @ Gc[(f, k)] @ inv(Gv[(f, k)]) @ Wv[iv["f"][f][k]])
                # the finger tip has no joint after it: it keeps the bind turn against its parent
                par = Wc[ic["f"][f][1]] @ C[(f, 1)] @ inv(Wv[iv["f"][f][1]]) @ Wv[iv["f"][f][2]]
                C[(f, 2)] = rot_only(inv(Wc[ic["f"][f][2]]) @ par)
            # the upper arm: along elbow - shoulder, its roll as the forearm's
            up, fore = Wv[iv["up"]], Wv[iv["fore"]]
            x = norm(fore[:3, 3] - up[:3, 3])
            Gup = frame(x, orth(fore[:3, 1], x), up[:3, 3])
            self.vb[s] = dict(L_up=float(np.linalg.norm(fore[:3, 3] - up[:3, 3])), Cup=rot_only(inv(Gup) @ up),
                              clav=Wv[iv["clav"]].copy(), up=up.copy())
            self.C[s], self.ic[s], self.iv[s] = C, ic, iv
        sp = mv.bone_names.index("ValveBiped.Bip01_Spine4")
        self.spine = Wv[sp].copy()

    def frame_bones(self, Wc):
        """{ValveBiped name: world matrix} for one frame of the CS:S rig"""
        out = {}
        for s in ("R", "L"):
            C, ic, n = self.C[s], self.ic[s], vb_names(s)
            fore = Wc[ic["fore"]] @ C["fore"]
            out[n["fore"]] = fore
            out[n["hand"]] = Wc[ic["hand"]] @ C["hand"]
            for f in range(5):
                for k in range(3):
                    out[n["f"][f][k]] = Wc[ic["f"][f][k]] @ C[(f, k)]
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


def skeleton_order():
    """the bones of the new model: (name, parent name)"""
    out = [("ValveBiped.Bip01_Spine4", None)]
    for s in ("L", "R"):
        n = vb_names(s)
        out += [(n["clav"], "ValveBiped.Bip01_Spine4"), (n["up"], n["clav"]), (n["fore"], n["up"]), (n["hand"], n["fore"])]
        for f in range(5):
            out += [(n["f"][f][0], n["hand"]), (n["f"][f][1], n["f"][f][0]), (n["f"][f][2], n["f"][f][1])]
    out.append(("beretta", None))
    out += [("beretta_" + p[0], "beretta") for p in PARTS]
    return out


# ------------------------------------------------------------------ the gun
class Gun:
    """the Beretta of the Elite rig moved onto the P228 rig"""

    def __init__(self, me, We0, We_bind, mp, Wp0, scale):
        le = {b.lower(): i for i, b in enumerate(me.bone_names)}
        lp = {b.lower(): i for i, b in enumerate(mp.bone_names)}
        self.ie = {k: le["v_weapon." + v.lower()] for k, v in
                   [("gun", "elite_right")] + [(p[0], p[1]) for p in PARTS] + [("muzzle", "muzzleflash_right"), ("shell", "shelleject_right")]}
        self.ip = {k: lp["v_weapon." + v.lower()] for k, v in [("gun", "p228_Parent")] + [(p[0], p[2]) for p in PARTS]}
        # the hand frames of both rigs (joints only: their bones' axes need not agree)
        G_p = geo_frames(Wp0, css_index(mp, P228_SIDE["R"]))["hand"]
        G_e = geo_frames(We0, css_index(me, "Right"))["hand"]
        self.K = inv(Wp0[self.ip["gun"]]) @ G_p @ inv(G_e) @ We0[self.ie["gun"]]
        self.KR = self.K[:3, :3]
        gun_e = We0[self.ie["gun"]]
        self.E = {p[0]: inv(gun_e) @ We0[self.ie[p[0]]] for p in PARTS}
        self.rest = {p[0]: inv(Wp0[self.ip["gun"]]) @ Wp0[self.ip[p[0]]] for p in PARTS}
        self.bind = We_bind
        self.att = {k: inv(gun_e) @ We0[self.ie[k]] for k in ("muzzle", "shell")}
        self.scale = scale

    def delta(self, Wp, part):
        P = inv(Wp[self.ip["gun"]]) @ Wp[self.ip[part]]
        R0 = self.rest[part]
        return P[:3, :3] @ R0[:3, :3].T, P[:3, 3] - R0[:3, 3]

    def bones(self, Wp, override=None):
        """{bone: world} of the gun for one P228 frame; override: {part: (R_d, o_d)}"""
        gun = Wp[self.ip["gun"]] @ self.K
        out = {"beretta": gun}
        for p in PARTS:
            Rd, od = (override or {}).get(p[0]) or self.delta(Wp, p[0])
            E = self.E[p[0]]
            Q = np.eye(4)
            Q[:3, :3] = self.KR.T @ Rd @ self.KR @ E[:3, :3]
            Q[:3, 3] = E[:3, 3] + self.KR.T @ od
            out["beretta_" + p[0]] = gun @ Q
        return out


# ------------------------------------------------------------------ files
def extract_css(v, work):
    os.makedirs(work, exist_ok=True)
    for base in ("v_pist_elite", "v_pist_p228"):
        for ext in (".mdl", ".vvd", ".dx90.vtx"):
            with open(os.path.join(work, "_css_" + base + ext), "wb") as f:
                f.write(v.read("models/weapons/" + base + ext))
    return os.path.join(work, "_css_v_pist_elite.mdl"), os.path.join(work, "_css_v_pist_p228.mdl")


def gun_mesh(me, We_bind, ie, scale):
    """the right Beretta's triangles: [(pos, normal, uv, part)] x3 per triangle, gun-part local"""
    gun_bones = {ie[k]: k for k in ("gun",) + tuple(p[0] for p in PARTS)}
    for k in ("muzzle", "shell"):
        gun_bones[ie[k]] = "gun"
    tris = []
    for _bn, _b, models in me.bodyparts():
        for (skinref, P, N, U, I, Wt, B) in models[0][1]:
            P, N, U = np.asarray(P, float), np.asarray(N, float), np.asarray(U, float)
            dom = B[np.arange(len(B)), np.argmax(Wt, 1)]
            for tri in I.reshape(-1, 3):
                parts = [gun_bones.get(int(dom[v])) for v in tri]
                if None in parts:
                    continue
                corners = []
                for v, part in zip(tri, parts):
                    Wb = We_bind[ie[part]]
                    p = inv(Wb) @ np.r_[P[v] * scale, 1.0]
                    n = Wb[:3, :3].T @ N[v]
                    corners.append((p[:3], n, U[v], part))
                tris.append(corners)
    return tris


def write_smd(path, names, parents, frames, tris=None, place=None, gun_bone="beretta"):
    """frames: [{name: world}]; tris: gun triangles placed by place(bone) -> world of the bone;
    gun_bone: the name of the gun's body bone ("beretta" / the world model's Weapon_bone)"""
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
        for corners in tris:
            out.append("beretta")
            for p, n, uv, part in reversed(corners):
                bone = gun_bone if part == "gun" else "beretta_" + part
                M = place(bone)
                wp = M @ np.r_[p, 1.0]
                wn = norm(M[:3, :3] @ n)
                out.append("%d %.6f %.6f %.6f %.6f %.6f %.6f %.6f %.6f 1 %d 1.0" % (
                    idx[bone], wp[0], wp[1], wp[2], wn[0], wn[1], wn[2], uv[0], 1.0 - uv[1], idx[bone]))
        out.append("end")
    with open(path, "w", encoding="ascii", newline="\n") as f:
        f.write("\n".join(out) + "\n")


def att_line(name, bone, M):
    p, y, r = dec.matrix_angles(M[:3, :3])
    return '$attachment "%s" "%s" %.4f %.4f %.4f rotate %.3f %.3f %.3f' % (name, bone, M[0, 3], M[1, 3], M[2, 3], p, y, r)


# ------------------------------------------------------------------ preview
PREVIEW_SHOTS = (("idle", 0), ("shoot1", 2), ("shoot_last", 20), ("reload_empty", 20), ("reload_empty", 40),
                 ("reload_empty", 55), ("reload_empty", 85), ("draw", 6), ("draw", 17))


def preview(fs, out_dir, allframes, tris, muzzle, hands=VB_REF, fov=74.0, size=(800, 450)):
    """what the eye sees (no textures): the gun and the hands bone-merged as the game does it"""
    from PIL import Image, ImageDraw
    os.makedirs(out_dir, exist_ok=True)
    mh = dec.Model(fs, hands)
    Wh = fk(mh, np.array(mh.bone_pos), np.array(mh.bone_rot))
    hm = []
    for _bn, _b, models in mh.bodyparts():
        if models and models[0][1]:
            for (skinref, P, N, U, I, Wt, B) in models[0][1]:
                hm.append((np.asarray(P, float), np.asarray(I).reshape(-1, 3), np.asarray(Wt, float), np.asarray(B)))
    W, H = size
    # the engine's view model FOV is horizontal at 4:3, widened for the screen
    half = math.atan(math.tan(math.radians(fov / 2)) * (W / H) / (4 / 3))
    foc = (W / 2) / math.tan(half)
    sheet = []
    for name, t in PREVIEW_SHOTS:
        fr = allframes[name][min(t, len(allframes[name]) - 1)]
        polys = []
        for corners in tris:
            pts = []
            for p, n, uv, part in corners:
                M = fr["beretta" if part == "gun" else "beretta_" + part]
                pts.append((M @ np.r_[p, 1.0])[:3])
            polys.append((np.array(pts), (150, 160, 175)))
        for P, I, Wt, B in hm:
            Ms = []
            for bi in range(len(mh.bone_names)):
                nm = mh.bone_names[bi]
                Ms.append(fr[nm] @ inv(Wh[bi]) if nm in fr else None)
            V = np.zeros((len(P), 3))
            for vi in range(len(P)):
                acc, tot = np.zeros(3), 0.0
                for k in range(3):
                    if Wt[vi][k] > 1e-5 and Ms[int(B[vi][k])] is not None:
                        acc += Wt[vi][k] * (Ms[int(B[vi][k])] @ np.r_[P[vi], 1.0])[:3]
                        tot += Wt[vi][k]
                V[vi] = acc / tot if tot else np.nan
            for tri in I:
                polys.append((V[tri], (205, 160, 130)))
        zbuf = np.full((H, W), np.inf)
        img = np.full((H, W, 3), 40, np.uint8)
        for pts, col in polys:
            if np.isnan(pts).any() or (pts[:, 0] < 1.0).any():
                continue
            n = np.cross(pts[1] - pts[0], pts[2] - pts[0])
            ln = np.linalg.norm(n)
            if ln < 1e-9:
                continue
            shade = 0.35 + 0.65 * abs(np.dot(n / ln, norm(-pts.mean(0))))
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
            c = 1 - a - b
            inside = (a >= 0) & (b >= 0) & (c >= 0)
            if not inside.any():
                continue
            z = a * pts[0, 0] + b * pts[1, 0] + c * pts[2, 0]
            sub = zbuf[y0:y1 + 1, x0:x1 + 1]
            win = inside & (z < sub)
            sub[win] = z[win]
            img[y0:y1 + 1, x0:x1 + 1][win] = (np.array(col) * shade).astype(np.uint8)
        im = Image.fromarray(img)
        mz = fr["beretta"] @ muzzle
        p0, p1 = mz[:3, 3], mz[:3, 3] + mz[:3, 0] * 4
        if p0[0] > 1 and p1[0] > 1:
            ImageDraw.Draw(im).line([(W / 2 - foc * p0[1] / p0[0], H / 2 - foc * p0[2] / p0[0]),
                                     (W / 2 - foc * p1[1] / p1[0], H / 2 - foc * p1[2] / p1[0])], fill=(255, 220, 0), width=2)
        ImageDraw.Draw(im).text((6, 4), "%s %d" % (name, t), fill=(255, 255, 255))
        sheet.append(im)
    cols = 3
    rows = (len(sheet) + cols - 1) // cols
    out = Image.new("RGB", (W * cols, H * rows))
    for i, im in enumerate(sheet):
        out.paste(im, ((i % cols) * W, (i // cols) * H))
    path = os.path.join(out_dir, "beretta_preview.png")
    out.save(path)
    print("preview", path)


# ------------------------------------------------------------------ build
def build(upscale_on=True, report=False, preview_dir=None):
    fs = gamefs.GameFS(MOD, dec.SDK)
    v = vpk.VPK(CSS_VPK)
    work = SRC
    elite_path, p228_path = extract_css(v, work)
    me, mp = dec.Model(fs, elite_path), dec.Model(fs, p228_path)
    mv = dec.Model(fs, VB_REF)
    Wv = fk(mv, np.array(mv.bone_pos), np.array(mv.bone_rot))

    # scale: the CS:S joints against the hands' (forearm, palm, index finger)
    Wpb_raw = fk(mp, np.array(mp.bone_pos), np.array(mp.bone_rot))
    icr, ivr = css_index(mp, P228_SIDE["R"]), vb_index(mv, "R")
    seg = lambda W, a, b: np.linalg.norm(W[a][:3, 3] - W[b][:3, 3])
    ratios = [seg(Wv, ivr["fore"], ivr["hand"]) / seg(Wpb_raw, icr["fore"], icr["hand"]),
              seg(Wv, ivr["hand"], ivr["f"][2][0]) / seg(Wpb_raw, icr["hand"], icr["f"][2][0]),
              seg(Wv, ivr["f"][1][0], ivr["f"][1][1]) / seg(Wpb_raw, icr["f"][1][0], icr["f"][1][1]),
              seg(Wv, ivr["f"][2][0], ivr["f"][2][1]) / seg(Wpb_raw, icr["f"][2][0], icr["f"][2][1])]
    scale = float(np.median(ratios))
    print("scale %.3f (forearm, palm, index, middle: %s)" % (scale, ", ".join("%.2f" % r for r in ratios)))

    seq_p = {s["name"]: s for s in mp.sequences()}
    seq_e = {s["name"]: s for s in me.sequences()}
    frames_p = lambda name: [prepare(fk(mp, *fr), scale, True) for fr in dec.sample_frames(fs, mp, seq_p[name])]
    Wpb = prepare(Wpb_raw, scale, True)
    retarget = Retarget(Wpb, Wv, mp, mv)

    We_bind = prepare(fk(me, np.array(me.bone_pos), np.array(me.bone_rot)), scale, False)
    We0 = prepare(fk(me, *dec.sample_frames(fs, me, seq_e["idle"])[0]), scale, False)
    idle = frames_p("idle")
    gun = Gun(me, We0, We_bind, mp, idle[0], scale)

    # the slide locked back: as at the start of the P228's reload
    reload_p = frames_p("reload")
    locked = {"slide": gun.delta(reload_p[5], "slide")}
    forward = {"slide": (np.eye(3), np.zeros(3))}

    def frames_of(Wlist, override=None, hold_slide_after=None):
        """hold_slide_after: from that frame the slide stays where it went furthest (the last shot)"""
        out = []
        held = None
        for t, Wp in enumerate(Wlist):
            ov = dict(override or {})
            if hold_slide_after is not None:
                d = gun.delta(Wp, "slide")
                if held is None or np.linalg.norm(d[1]) >= np.linalg.norm(held[1]):
                    held = d
                if t >= hold_slide_after:
                    ov["slide"] = held
            f = retarget.frame_bones(Wp)
            f.update(gun.bones(Wp, ov))
            out.append(f)
        return out

    sk = skeleton_order()
    names = [n for n, _p in sk]
    parents = [p for _n, p in sk]
    ref_frame = frames_of(idle[:1])[0]

    # the mesh: each corner on its part, placed by the part's bind (the idle's first frame)
    tris = gun_mesh(me, We_bind, gun.ie, scale)
    print("beretta: %d triangles" % len(tris))
    write_smd(os.path.join(work, "v_beretta_ref.smd"), names, parents, [ref_frame], tris, lambda b: ref_frame[b])

    seqs = []
    allframes = {}

    def seq(name, frames, fps, act, extra="", events=()):
        allframes[name] = frames
        write_smd(os.path.join(work, "v_beretta_%s.smd" % name), names, parents, frames)
        ev = " ".join('{ event %s %d "%s" }' % e for e in events)
        seqs.append('$sequence "%s" "v_beretta_%s.smd" fps %.1f %s activity %s 1 %s' % (name, name, fps, extra, act, ev))
        return frames

    fps = lambda n: seq_p[n]["fps"]
    nfr = lambda n: seq_p[n]["frames"]
    at = lambda n, cyc: int(round(cyc * (nfr(n) - 1)))
    shot_ev = (("AE_MUZZLEFLASH", 0, "PISTOL MUZZLE"), ("6001", 1, "0"))
    snd = lambda frame, s: ("5004", frame, "Weapon_RPMBeretta." + s)

    seq("idle", frames_of(idle), fps("idle"), "ACT_VM_IDLE", "loop")
    seq("idle_empty", frames_of(idle, locked), fps("idle"), "ACT_VM_IDLE_EMPTY", "loop")
    for k in (1, 2, 3):
        seq("shoot%d" % k, frames_of(frames_p("shoot%d" % k)), fps("shoot%d" % k), "ACT_VM_PRIMARYATTACK", "", shot_ev)
    shoot1 = frames_p("shoot1")
    seq("shoot_last", frames_of(shoot1, hold_slide_after=1), fps("shoot1"), "ACT_VM_PRIMARYATTACK_EMPTY", "", shot_ev)
    seq("dryfire", frames_of(frames_p("shoot_empty"), locked), fps("shoot_empty"), "ACT_VM_DRYFIRE")
    cyc = {o.split(".")[-1].lower(): c for c, _e, o in seq_p["reload"]["events"]}
    seq("reload", frames_of(reload_p, forward), fps("reload"), "ACT_VM_RELOAD", "",
        (snd(at("reload", cyc["clipout"]), "Clipout"), snd(at("reload", cyc["clipin"]), "Clipin")))
    seq("reload_empty", frames_of(reload_p), fps("reload"), "ACT_VM_RELOAD_EMPTY", "",
        (snd(at("reload", cyc["clipout"]), "Clipout"), snd(at("reload", cyc["clipin"]), "Clipin"),
         snd(at("reload", cyc["sliderelease"]), "Sliderelease")))
    draw = frames_p("draw")
    dcyc = {o.split(".")[-1].lower(): c for c, _e, o in seq_p["draw"]["events"]}
    seq("draw", frames_of(draw), fps("draw"), "ACT_VM_DRAW", "", (snd(0, "Deploy"), snd(at("draw", dcyc["slidepull"]), "Slidepull")))
    seq("draw_empty", frames_of(draw, locked), fps("draw"), "ACT_VM_DRAW_EMPTY", "", (snd(0, "Deploy"),))
    seq("holster", frames_of(list(reversed(draw)), forward), fps("draw") * 1.6, "ACT_VM_HOLSTER")

    # attachments: the muzzle looks along the barrel (the way the slide recoils), the shell port
    # throws to the right, a little up and back (the Elite's ejection bone points 60 degrees up:
    # with HL2's 150-200 units/s the cases flew into the sky)
    sh = frames_of(frames_p("shoot1"))
    bar = None
    for f in sh:
        d = inv(f["beretta"]) @ f["beretta_slide"]
        r = inv(ref_frame["beretta"]) @ ref_frame["beretta_slide"]
        if bar is None or np.linalg.norm(d[:3, 3] - r[:3, 3]) > np.linalg.norm(bar):
            bar = d[:3, 3] - r[:3, 3]
    barrel = norm(-bar)
    up_w = np.array([0.0, 0, 1])
    up = orth(inv(ref_frame["beretta"])[:3, :3] @ up_w, barrel)
    mz = gun.att["muzzle"].copy()
    muzzle = frame(barrel, np.cross(up, barrel), mz[:3, 3])
    se = gun.att["shell"].copy()
    right = np.cross(barrel, up)
    eject = norm(0.85 * right + 0.45 * up - 0.2 * barrel)
    shell = frame(eject, orth(barrel, eject), se[:3, 3])

    qc = ['$modelname "weapons/hl2rpm/v_beretta.mdl"', "$origin 0 0 0 -90",
          '$body "weapon" "v_beretta_ref.smd"', '$cdmaterials "%s"' % MAT]
    qc += ['$bonemerge "%s"' % b for b in VIEW_BONES if b in names]
    qc += [att_line("muzzle", "beretta", muzzle), att_line("shell", "beretta", shell)]
    qc += seqs
    qc_path = os.path.join(work, "v_beretta.qc")
    with open(qc_path, "w", encoding="ascii", newline="\n") as f:
        f.write("// make_beretta.py: the view model of the Beretta 92\n" + "\n".join(qc) + "\n")
    rc, txt, bad = dec.compile_qc(qc_path)
    print("v_beretta: %s" % ("ok" if rc == 0 else "FAILED " + " | ".join(bad[:4])))

    # ---------------- the world model: the gun against the right hand as in first person
    hand = ref_frame["ValveBiped.Bip01_R_Hand"]
    wnames = ["ValveBiped.Bip01_R_Hand", "ValveBiped.Weapon_bone"] + ["beretta_" + p[0] for p in PARTS]
    wpar = [None, "ValveBiped.Bip01_R_Hand"] + ["ValveBiped.Weapon_bone"] * len(PARTS)
    rel = {"ValveBiped.Weapon_bone": inv(hand) @ ref_frame["beretta"]}
    for p in PARTS:
        rel["beretta_" + p[0]] = rel["ValveBiped.Weapon_bone"] @ inv(ref_frame["beretta"]) @ ref_frame["beretta_" + p[0]]
    wb = "ValveBiped.Weapon_bone"
    # the model's own pose (on the ground, in the CK, on the item card): the hand bone turned so the gun
    # lies on its left side (barrel +x, its top +y); in a hand only the gun against the hand counts
    H = np.eye(4)
    H[:3, :3] = np.column_stack([barrel, up, np.cross(barrel, up)]).T @ rel[wb][:3, :3].T
    pts = np.array([(H @ rel[wb if part == "gun" else "beretta_" + part] @ np.r_[q, 1.0])[:3] for c in tris for q, _n, _uv, part in c])
    lo, hi = pts.min(0), pts.max(0)
    H[:3, 3] = -np.array([(lo[0] + hi[0]) / 2, (lo[1] + hi[1]) / 2, lo[2]])
    wf = {"ValveBiped.Bip01_R_Hand": H}
    for k, M in rel.items():
        wf[k] = H @ M
    write_smd(os.path.join(work, "w_beretta_ref.smd"), wnames, wpar, [wf], tris, lambda b: wf[b], gun_bone=wb)
    write_smd(os.path.join(work, "w_beretta_idle.smd"), wnames, wpar, [wf])
    # the convex hull: every corner on the root
    he = m_euler(H[:3, :3])
    hull = ["version 1", "nodes", '0 "ValveBiped.Bip01_R_Hand" -1', "end", "skeleton", "time 0",
            "0 %.6f %.6f %.6f %.6f %.6f %.6f" % (H[0, 3], H[1, 3], H[2, 3], he[0], he[1], he[2]), "end", "triangles"]
    for c in tris:
        hull.append("phy")
        for p, n, uv, part in reversed(c):
            M = wf[wb if part == "gun" else "beretta_" + part]
            q = M @ np.r_[p, 1.0]
            hull.append("0 %.6f %.6f %.6f 0 0 1 0 0" % (q[0], q[1], q[2]))
    hull.append("end")
    with open(os.path.join(work, "w_beretta_phys.smd"), "w", encoding="ascii", newline="\n") as f:
        f.write("\n".join(hull) + "\n")
    wmz = muzzle
    wqc = ['$modelname "weapons/hl2rpm/w_beretta.mdl"', "$origin 0 0 0 -90",
           '$body "weapon" "w_beretta_ref.smd"', '$cdmaterials "%s"' % MAT, '$surfaceprop "weapon"',
           att_line("muzzle", "ValveBiped.Weapon_bone", wmz), att_line("1", "ValveBiped.Weapon_bone", shell),
           '$sequence "idle" "w_beretta_idle.smd" fps 30 activity ACT_VM_IDLE 1',
           '$sequence "attack1" "w_beretta_idle.smd" fps 30 activity ACT_RANGE_ATTACK_PISTOL 1 '
           '{ event AE_NPC_MUZZLEFLASH 0 "PISTOL MUZZLE" } { event 6001 0 "0" }',
           '$collisionmodel "w_beretta_phys.smd"\n{\n\t$mass 1.2\n}']
    wqc_path = os.path.join(work, "w_beretta.qc")
    with open(wqc_path, "w", encoding="ascii", newline="\n") as f:
        f.write("// make_beretta.py: the world model of the Beretta 92\n" + "\n".join(wqc) + "\n")
    rc2, txt2, bad2 = dec.compile_qc(wqc_path)
    print("w_beretta: %s" % ("ok" if rc2 == 0 else "FAILED " + " | ".join(bad2[:4])))

    if preview_dir:
        preview(fs, preview_dir, allframes, tris, muzzle)
        return
    make_materials(v, upscale_on)
    make_sounds(v)
    write_script()
    if report:
        size = np.ptp(np.array([c[0] for t in tris for c in t]), 0)
        print("gun extent (gun frame):", np.round(size, 2))
        for k in ("ValveBiped.Bip01_R_Hand", "ValveBiped.Bip01_R_Forearm", "ValveBiped.Bip01_R_UpperArm", "ValveBiped.Bip01_L_Hand", "beretta"):
            print("  %-30s %s" % (k, np.round(ref_frame[k][:3, 3], 2)))


def make_materials(v, upscale_on):
    out_dir = os.path.join(MOD, "materials", *MAT.split("/"))
    os.makedirs(out_dir, exist_ok=True)
    for src, dst in (("pist_elite", "beretta"), ("pist_elite_ref", "beretta_ref")):
        d = v.read("materials/models/weapons/v_models/pist_elite/%s.vtf" % src)
        target = os.path.join(out_dir, dst + ".vtf")
        with open(target, "wb") as f:
            f.write(d)
    made = os.path.join(SRC, "_hd", "beretta.vtf")
    if not upscale_on and os.path.exists(made):
        # (--no-upscale: the upscale made before stays)
        shutil.copy(made, os.path.join(out_dir, "beretta.vtf"))
    if upscale_on:
        # (hd_textures.upscale looks for stock textures only: the batch is given the file itself)
        try:
            import hd_textures
            src = os.path.join(SRC, "_css_pist_elite.vtf")
            shutil.copy(os.path.join(out_dir, "beretta.vtf"), src)
            up = os.path.join(SRC, "_hd", "beretta.vtf")
            os.makedirs(os.path.dirname(up), exist_ok=True)
            hd_textures.upscale_batch([(MAT + "/beretta", src, up)])
            if os.path.exists(up):
                shutil.copy(up, os.path.join(out_dir, "beretta.vtf"))
                print("texture upscaled (Real-ESRGAN, detail transfer)")
        except Exception as e:  # (no GPU tool: the original stays)
            print("no upscale:", e)
    vmt = ('// make_beretta.py: the CS:S Elite texture (Counter-Strike: Source)\n"VertexLitGeneric"\n{\n'
           '\t"$basetexture" "%s/beretta"\n\t"$envmap" "env_cubemap"\n\t"$envmapmask" "%s/beretta_ref"\n'
           '\t"$envmaptint" "[0.35 0.35 0.35]"\n\t"$phong" "1"\n\t"$phongexponent" "24"\n\t"$phongboost" "0.8"\n'
           '\t"$phongfresnelranges" "[0.3 0.8 1]"\n\t"$surfaceprop" "weapon"\n}\n') % (MAT, MAT)
    with open(os.path.join(out_dir, "beretta.vmt"), "w", encoding="ascii", newline="\n") as f:
        f.write(vmt)


SOUNDS = {  # script name: (CS:S file, channel, volume, level)
    "Single": ("elite/elite-1.wav", "CHAN_WEAPON", "0.9", "SNDLVL_GUNFIRE"),
    "NPC_Single": ("elite/elite-1.wav", "CHAN_WEAPON", "0.8", "SNDLVL_GUNFIRE"),
    "Clipout": ("elite/elite_clipout.wav", "CHAN_ITEM", "0.8", "SNDLVL_NORM"),
    "Clipin": ("elite/elite_rightclipin.wav", "CHAN_ITEM", "0.8", "SNDLVL_NORM"),
    "Sliderelease": ("elite/elite_sliderelease.wav", "CHAN_ITEM", "0.8", "SNDLVL_NORM"),
    "Slidepull": ("p228/p228_slidepull.wav", "CHAN_ITEM", "0.7", "SNDLVL_NORM"),
    "Deploy": ("elite/elite_deploy.wav", "CHAN_ITEM", "0.6", "SNDLVL_NORM"),
}


def make_sounds(v):
    out_dir = os.path.join(MOD, "sound", *SND.split("/"))
    os.makedirs(out_dir, exist_ok=True)
    blocks = ["// make_beretta.py: the Beretta 92 (weapon_rpm_beretta), Counter-Strike: Source sounds"]
    for name, (src, chan, vol, lvl) in SOUNDS.items():
        base = os.path.basename(src)
        with open(os.path.join(out_dir, base), "wb") as f:
            f.write(v.read("sound/weapons/" + src))
        pitch = '\t"pitch"\t\t"97,103"\n' if "Single" in name else ""
        blocks.append('"Weapon_RPMBeretta.%s"\n{\n\t"channel"\t\t"%s"\n\t"volume"\t\t"%s"\n\t"soundlevel"\t"%s"\n%s\t"wave"\t\t"%s%s/%s"\n}\n'
                      % (name, chan, vol, lvl, pitch, ")" if chan == "CHAN_WEAPON" else "", SND, base))
    path = os.path.join(MOD, "scripts", "game_sounds_weapons.txt")
    txt = open(path, encoding="utf-8", errors="replace").read()
    mark = "// make_beretta.py:"
    if mark in txt:
        txt = txt[:txt.index(mark)].rstrip() + "\n"
    txt = txt.rstrip() + "\n\n" + "\n".join(blocks)
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write(txt)


def write_script():
    script = '''// make_beretta.py: the Beretta 92 - game/server/hl2rpm_beretta.cpp
WeaponData
{
	"printname"		"Beretta 92"
	"viewmodel"		"models/weapons/hl2rpm/v_beretta.mdl"
	"playermodel"	"models/weapons/hl2rpm/w_beretta.mdl"
	"anim_prefix"	"pistol"
	"bucket"		"1"
	"bucket_position"	"1"
	"viewmodel_fov"	"74"
	"uses_hands"	"1"
	"clip_size"		"15"
	"default_clip"	"15"
	"primary_ammo"	"Pistol"
	"secondary_ammo"	"None"
	"weight"		"2"
	"item_flags"	"0"
	"SoundData"
	{
		"single_shot"		"Weapon_RPMBeretta.Single"
		"single_shot_npc"	"Weapon_RPMBeretta.NPC_Single"
		"empty"				"Weapon_Pistol.Empty"
		"reload_npc"		"Weapon_Pistol.NPC_Reload"
	}
	TextureData
	{
		"weapon"			{ "font" "WeaponIcons" "character" "d" }
		"weapon_s"			{ "font" "WeaponIconsSelected" "character" "d" }
		"ammo"				{ "font" "WeaponIconsSmall" "character" "p" }
		"crosshair"			{ "font" "Crosshairs" "character" "Q" }
		"autoaim"			{ "file" "sprites/crosshairs" "x" "0" "y" "48" "width" "24" "height" "24" }
	}
}
'''
    with open(os.path.join(MOD, "scripts", "weapon_rpm_beretta.txt"), "w", encoding="ascii", newline="\n") as f:
        f.write(script)


if __name__ == "__main__":
    pv = sys.argv[sys.argv.index("--preview") + 1] if "--preview" in sys.argv else None
    build("--no-upscale" not in sys.argv, "--report" in sys.argv, pv)
