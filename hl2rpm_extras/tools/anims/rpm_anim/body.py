"""The character's mesh posed by clips: skinning (numpy), shaded previews together with furniture,
and contact / penetration numbers against a furniture model."""
import struct

import numpy as np
from PIL import Image, ImageDraw

from . import qmath as Q
from . import rig as R


class Body:
    def __init__(self, model, rig):
        m = R.dec.Model(R.game_fs(), model)
        self.rig = rig
        # bone order of the model -> rig
        self.map = np.array([rig.index(n) for n in m.bone_names])
        d = m.data
        n, idx = struct.unpack_from("<ii", d, 156)
        self.p2b = np.zeros((n, 3, 4))
        for i in range(n):
            b = idx + i * 216
            self.p2b[i] = np.array(struct.unpack_from("<12f", d, b + 96)).reshape(3, 4)
        P, W, B, I = [], [], [], []
        base = 0
        for bname, _bbase, models in m.bodyparts():
            if not models or not models[0][1]:
                continue
            for (skinref, p, nrm, uv, ind, w, bb) in models[0][1]:
                P.append(p)
                W.append(w)
                B.append(bb)
                I.append(np.asarray(ind).reshape(-1, 3) + base)
                base += len(p)
        self.P = np.concatenate(P)
        self.W = np.concatenate(W).astype(np.float64)
        s = self.W.sum(1, keepdims=True)
        self.W = self.W / np.maximum(s, 1e-6)
        self.B = np.concatenate(B)
        self.I = np.concatenate(I)

    def skin(self, wp, wq):
        """vertices for one frame: wp (B,3), wq (B,4) of the rig"""
        R3 = Q.to_matrix(wq[self.map])                        # model bone order
        M = np.zeros((len(self.map), 3, 4))
        M[:, :, :3] = R3
        M[:, :, 3] = wp[self.map]
        # bone_to_world x pose_to_bone
        S = np.einsum("bij,bjk->bik", M[:, :, :3], self.p2b[:, :, :3])
        T = np.einsum("bij,bj->bi", M[:, :, :3], self.p2b[:, :, 3]) + M[:, :, 3]
        bb = np.clip(self.B, 0, len(self.map) - 1)
        Sv = np.einsum("nk,nkij->nij", self.W, S[bb])
        Tv = np.einsum("nk,nki->ni", self.W, T[bb])
        return np.einsum("nij,nj->ni", Sv, self.P) + Tv

    def frames(self, clip, idx):
        wp, wq = clip.world()
        return [self.skin(wp[t], wq[t]) for t in idx]


# ------------------------------------------------------------------ previews
VIEW_DIRS = {"side": (0, -1, 0), "front": (-1, 0, 0), "back34": (0.6, -0.8, 0), "front34": (-0.7, -0.7, 0)}


def render(parts, view="side", size=(360, 360), bounds=None, elev=8.0):
    """parts: [(verts (N,3), tris (M,3) int, rgb)]; orthographic, painter's algorithm with flat shading"""
    d = np.array(VIEW_DIRS[view], dtype=np.float64)
    d /= np.linalg.norm(d)
    e = np.radians(elev)
    look = np.array([d[0] * np.cos(e), d[1] * np.cos(e), -np.sin(e)])   # camera looks along look (slightly down)
    right = np.cross(look, [0, 0, 1.0])
    right /= np.linalg.norm(right)
    up = np.cross(right, look)
    allv = np.concatenate([p[0] for p in parts])
    if bounds is None:
        u = allv @ right
        v = allv @ up
        bounds = (u.min(), u.max(), v.min(), v.max())
    umin, umax, vmin, vmax = bounds
    span = max(umax - umin, vmax - vmin) * 1.08
    cu, cv = (umin + umax) / 2, (vmin + vmax) / 2
    sc = min(size) / span
    img = Image.new("RGB", size, (30, 32, 38))
    dr = ImageDraw.Draw(img)
    polys = []
    light = -look * 0.6 + np.array([0.3, 0.2, 0.75])
    light /= np.linalg.norm(light)
    for verts, tris, rgb in parts:
        T = verts[tris]
        n = np.cross(T[:, 2] - T[:, 0], T[:, 1] - T[:, 0])
        n /= np.maximum(np.linalg.norm(n, axis=1, keepdims=True), 1e-9)
        depth = (T.mean(1) @ look)
        sh = np.clip(np.abs(n @ light), 0.15, 1.0)
        su = (T @ right - cu) * sc + size[0] / 2
        sv = size[1] / 2 - (T @ up - cv) * sc
        for k in range(len(T)):
            polys.append((depth[k], [(su[k, 0], sv[k, 0]), (su[k, 1], sv[k, 1]), (su[k, 2], sv[k, 2])],
                          tuple(int(c * sh[k]) for c in rgb)))
    polys.sort(key=lambda p: -p[0])
    for _dep, pts, col in polys:
        dr.polygon(pts, fill=col)
    return img


def sheet(body, clip, path, furniture=None, n=6, views=("side", "front34"), size=300, label=""):
    """a row per view, n frames; furniture: (tris (M,3,3) in the clip's model space) or None"""
    idx = np.linspace(0, clip.frames - 1, n).round().astype(int)
    frames = body.frames(clip, idx)
    fparts = []
    if furniture is not None and len(furniture):
        fv = furniture.reshape(-1, 3)
        ft = np.arange(len(fv)).reshape(-1, 3)
        fparts = [(fv, ft, (150, 120, 85))]
    floor = np.array([[-40, -40, 0], [60, -40, 0], [60, 40, 0], [-40, 40, 0.0]])
    floor_t = np.array([[0, 1, 2], [0, 2, 3]])
    img = Image.new("RGB", (size * n, size * len(views)), (20, 20, 24))
    for r, view in enumerate(views):
        # common bounds over the frames of this view
        allv = np.concatenate(frames + ([fparts[0][0]] if fparts else []))
        d = np.array(VIEW_DIRS[view], dtype=np.float64)
        d /= np.linalg.norm(d)
        e = np.radians(8.0)
        look = np.array([d[0] * np.cos(e), d[1] * np.cos(e), -np.sin(e)])
        right = np.cross(look, [0, 0, 1.0])
        right /= np.linalg.norm(right)
        up = np.cross(right, look)
        b = ((allv @ right).min(), (allv @ right).max(), (allv @ up).min(), (allv @ up).max())
        for k, (t, verts) in enumerate(zip(idx, frames)):
            parts = [(floor, floor_t, (70, 72, 70))] + fparts + [(verts, body.I, (200, 190, 175))]
            im = render(parts, view, (size, size), b)
            ImageDraw.Draw(im).text((4, 2), "%s f%d %s" % (label or clip.name, t, view), fill=(230, 230, 140))
            img.paste(im, (k * size, r * size))
    img.save(path)
    return path


# ------------------------------------------------------------------ contact numbers
def seat_report(body, clip, info, origin=(0, 0, 0), yaw_deg=0.0, frames=None):
    """the body against a measured seat (furniture.analyze placed at origin / yaw in the clip's space):
    how deep vertices go under the seat top, behind the backrest, under the floor"""
    th = np.radians(yaw_deg)
    Rz = np.array([[np.cos(th), -np.sin(th), 0], [np.sin(th), np.cos(th), 0], [0, 0, 1.0]])
    o = np.asarray(origin, dtype=np.float64)
    seat_c = Rz @ (info["seat_center"] - [0, 0, info["floor"]]) + o
    fwd = Rz @ np.array([info.get("front", [1, 0])[0], info.get("front", [1, 0])[1], 0.0])
    side = np.cross([0, 0, 1.0], fwd)
    half = (info["seat_max"] - info["seat_min"])[:2] / 2
    out = []
    idx = frames if frames is not None else range(clip.frames)
    wp, wq = clip.world()
    for t in idx:
        v = body.skin(wp[t], wq[t])
        rel = v - seat_c
        a = rel @ fwd
        s = rel @ side
        over = (np.abs(a) < half[0] * 0.95) & (np.abs(s) < half[1] * 0.95)
        under_seat = np.clip(seat_c[2] - v[over, 2], 0, None) if over.any() else np.zeros(1)
        floor_pen = np.clip(-v[:, 2], 0, None)
        back = 0.0
        if "back_dist" in info:
            above = (v[:, 2] > seat_c[2] + 2) & (v[:, 2] < info["back_top"] - info["floor"] + o[2]) & (np.abs(s) < half[1])
            behind = -(a[above] + info["back_dist"])
            back = float(np.clip(behind, 0, None).max()) if above.any() else 0.0
        out.append({"frame": int(t), "under_seat": float(under_seat.max()), "floor": float(floor_pen.max()),
                    "behind_back": back, "lowest_over_seat": float(v[over, 2].min() - seat_c[2]) if over.any() else None})
    return out
