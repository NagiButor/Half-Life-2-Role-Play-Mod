"""Stick-figure sheets of clips (front / side / top views) - a quick look before the game."""
import numpy as np
from PIL import Image, ImageDraw

VIEWS = {
    "side": (1, 2, -1),     # x = forward (Source +x) shown left->right, up = z
    "front": (2, 2, 1),     # y shown, up = z
    "top": (1, 0, 1),
}


def _proj(p, view):
    if view == "side":
        return p[..., 0], p[..., 2]
    if view == "front":
        return -p[..., 1], p[..., 2]
    return -p[..., 1], p[..., 0]


def sheet(clips, path, frames_per_clip=6, views=("side", "front"), cell=180, boxes=(), labels=None, meshes=None):
    """clips: list of Clip; boxes: [(mins, maxs)] in model space drawn in every cell (furniture);
    meshes: per clip a list of (N,3,3) triangle arrays (model space) drawn under the skeleton"""
    rows = []
    for c in clips:
        wp, _wq = c.world()
        idx = np.linspace(0, c.frames - 1, frames_per_clip).round().astype(int)
        rows.append((c, wp, idx))
    W = cell * frames_per_clip * len(views)
    H = cell * len(rows) + 4
    img = Image.new("RGB", (W, H), (24, 24, 28))
    d = ImageDraw.Draw(img)
    for r, (c, wp, idx) in enumerate(rows):
        rig = c.rig
        allp = wp.reshape(-1, 3)
        for vi, view in enumerate(views):
            u, v = _proj(allp, view)
            umin, umax = u.min() - 10, u.max() + 10
            vmin, vmax = min(v.min(), 0) - 5, v.max() + 10
            for (mn, mx) in boxes:
                cu, cv = _proj(np.array([mn, mx]), view)
                umin, umax = min(umin, cu.min()), max(umax, cu.max())
                vmin, vmax = min(vmin, cv.min()), max(vmax, cv.max())
            cell_meshes = meshes[r] if meshes else []
            for tri in cell_meshes:
                cu, cv = _proj(tri.reshape(-1, 3), view)
                umin, umax = min(umin, cu.min()), max(umax, cu.max())
                vmin, vmax = min(vmin, cv.min()), max(vmax, cv.max())
            span = max(umax - umin, vmax - vmin)
            sc = (cell - 12) / span
            for k, t in enumerate(idx):
                ox = (vi * frames_per_clip + k) * cell
                oy = r * cell

                def P(p):
                    uu, vv = _proj(np.asarray(p), view)
                    return (ox + 6 + (uu - umin) * sc, oy + cell - 6 - (vv - vmin) * sc)
                # floor
                y0 = P(np.array([0, 0, 0.0]))[1]
                d.line([(ox, y0), (ox + cell, y0)], fill=(70, 70, 80))
                for tri in cell_meshes:
                    for tr in tri:
                        d.polygon([P(tr[0]), P(tr[1]), P(tr[2])], fill=(70, 60, 50), outline=(95, 80, 62))
                for (mn, mx) in boxes:
                    a, b = P(np.array(mn)), P(np.array(mx))
                    d.rectangle([min(a[0], b[0]), min(a[1], b[1]), max(a[0], b[0]), max(a[1], b[1])], outline=(120, 90, 60))
                for i, p in enumerate(rig.parents):
                    if p < 0:
                        continue
                    nm = rig.names[i].lower()
                    col = (220, 220, 220)
                    if "_l_" in nm or nm.endswith("_l"):
                        col = (90, 170, 255)
                    elif "_r_" in nm or nm.endswith("_r"):
                        col = (255, 120, 90)
                    if "finger" in nm:
                        continue
                    d.line([P(wp[t, p]), P(wp[t, i])], fill=col, width=2)
                d.text((ox + 4, oy + 2), "%s %s f%d" % ((labels[r] if labels else c.name)[:18], view[0], t), fill=(200, 200, 120))
    img.save(path)
    return path
