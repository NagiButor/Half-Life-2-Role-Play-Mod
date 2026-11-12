"""Holograms of the suits for the boot screen (client hl2rpm_suitboot.cpp): the model of the suit in its
idle pose seen from the front, drawn as light - a faint body, a glowing rim, bright outlines, scanlines.
White on transparent: the HUD tints it with the color of the suit theme.

    <CK venv python> make_suit_holograms.py [--png=<folder for previews>]

Writes materials/vgui/hl2rpm/suit_holo_<theme>.vtf/.vmt into the mod (hev, pcv, combine, elite).
The models are read with the Creation Kit's own reader (dialog_editor/dlgedit/world: mdl, anim)."""
import os
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "dialog_editor"))
sys.path.insert(0, HERE)
from dlgedit.world import anim, mdl  # noqa: E402
from dlgedit.world.gamefs import GameFS  # noqa: E402
from make_vitals_assets import write_vtf_bgra  # noqa: E402

MOD = r"E:\Steam\steamapps\sourcemods\hl2rpm"
SDK = r"E:\Steam\steamapps\common\Source SDK Base 2013 Singleplayer"
W, H = 256, 512
HEV_YAW = float(os.environ.get("HEV_YAW", "-90"))     # the suit item stands with its front along +y

# theme -> (model, idle sequence or activity ("" - the reference pose), yaw that turns its front to +x)
SUITS = {
    "hev": ("models/items/hevsuit.mdl", "", HEV_YAW),
    "pcv": ("models/items/hevsuit.mdl", "", HEV_YAW),          # no model of its own: the HEV placeholder (the plan)
    "combine": ("models/police.mdl", "ACT_IDLE", 0),
    "elite": ("models/combine_super_soldier.mdl", "ACT_IDLE", 0),
}


def posed_mesh(fs, name, seq_name):
    md = mdl.load(fs, name)
    if not md.ok:
        raise RuntimeError("can't read " + name)
    pos, nrm, tris = [], [], []
    base = 0
    skin_mats = None
    if seq_name:
        try:
            aset = anim.AnimSet(fs, name)
            seq = aset.find(seq_name)
            p, q = aset.pose(seq, 0.0)
            skin_mats = aset.skin_matrices(aset.matrices(p, q))
        except Exception as e:      # no animations: the reference pose
            print("  %s: reference pose (%s)" % (name, e))
    for m in md.meshes:
        p, n = m.pos, m.nrm
        if skin_mats is not None and m.bw is not None:
            p, n = anim.skin(m.pos, m.nrm, m.bw, m.bb, skin_mats)
        pos.append(np.asarray(p, dtype=np.float64))
        nrm.append(np.asarray(n, dtype=np.float64))
        tris.append(np.asarray(m.idx, dtype=np.int64).reshape(-1, 3) + base)
        base += len(p)
    return np.concatenate(pos), np.concatenate(nrm), np.concatenate(tris)


def rasterize(pos, nrm, tris):
    """Orthographic, from the front (+x of the model looks at the viewer): x -> -y, y -> z."""
    sx, sy, depth = -pos[:, 1], pos[:, 2], pos[:, 0]
    lo_x, hi_x, lo_y, hi_y = sx.min(), sx.max(), sy.min(), sy.max()
    scale = 0.92 * min(W / max(hi_x - lo_x, 1e-3), H / max(hi_y - lo_y, 1e-3))
    px = (sx - (lo_x + hi_x) / 2) * scale + W / 2
    py = H / 2 - (sy - (lo_y + hi_y) / 2) * scale
    zbuf = np.full((H, W), -1e9)
    nbuf = np.zeros((H, W, 3))
    for a, b, c in tris:
        x0, y0, x1, y1, x2, y2 = px[a], py[a], px[b], py[b], px[c], py[c]
        minx, maxx = int(max(0, np.floor(min(x0, x1, x2)))), int(min(W - 1, np.ceil(max(x0, x1, x2))))
        miny, maxy = int(max(0, np.floor(min(y0, y1, y2)))), int(min(H - 1, np.ceil(max(y0, y1, y2))))
        if minx > maxx or miny > maxy:
            continue
        den = (y1 - y2) * (x0 - x2) + (x2 - x1) * (y0 - y2)
        if abs(den) < 1e-9:
            continue
        gx, gy = np.meshgrid(np.arange(minx, maxx + 1) + 0.5, np.arange(miny, maxy + 1) + 0.5)
        w0 = ((y1 - y2) * (gx - x2) + (x2 - x1) * (gy - y2)) / den
        w1 = ((y2 - y0) * (gx - x2) + (x0 - x2) * (gy - y2)) / den
        w2 = 1 - w0 - w1
        inside = (w0 >= -1e-4) & (w1 >= -1e-4) & (w2 >= -1e-4)
        if not inside.any():
            continue
        z = w0 * depth[a] + w1 * depth[b] + w2 * depth[c]
        sub = zbuf[miny:maxy + 1, minx:maxx + 1]
        upd = inside & (z > sub)
        sub[upd] = z[upd]
        n = (w0[..., None] * nrm[a] + w1[..., None] * nrm[b] + w2[..., None] * nrm[c])
        nb = nbuf[miny:maxy + 1, minx:maxx + 1]
        nb[upd] = n[upd]
    nbuf /= np.maximum(np.linalg.norm(nbuf, axis=2, keepdims=True), 1e-6)
    return zbuf, nbuf, scale


def hologram(zbuf, nbuf, scale):
    cover = zbuf > -1e8
    facing = np.abs(nbuf[..., 0])                     # toward the viewer (+x)
    body = 0.10 + 0.55 * (1.0 - facing) ** 2          # the rim glows
    # outlines: depth steps and folds between neighbours
    z = np.where(cover, zbuf, zbuf[cover].min() - 50 if cover.any() else 0)
    edge = np.zeros_like(body)
    for dy, dx in ((0, 1), (1, 0)):
        zs = np.roll(np.roll(z, -dy, 0), -dx, 1)
        ns = np.roll(np.roll(nbuf, -dy, 0), -dx, 1)
        cs = np.roll(np.roll(cover, -dy, 0), -dx, 1)
        step = (np.abs(z - zs) * scale > 6.0) | (cover != cs)
        fold = (np.sum(nbuf * ns, axis=2) < 0.55) & cover & cs
        e = (step | fold).astype(np.float64)
        edge = np.maximum(edge, e)
        edge = np.maximum(edge, np.roll(np.roll(e, dy, 0), dx, 1))
    a = np.where(cover, body, 0.0)
    a = np.maximum(a, edge * 0.95)
    # a soft glow around the figure
    glow = cover.astype(np.float64)
    for _ in range(4):
        glow = (glow + np.roll(glow, 1, 0) + np.roll(glow, -1, 0) + np.roll(glow, 1, 1) + np.roll(glow, -1, 1)) / 5
    a = np.maximum(a, glow * 0.18)
    # scanlines
    rows = (np.arange(H) % 3 == 2)[:, None]
    a = np.where(rows, a * 0.6, a)
    rgba = np.ones((H, W, 4))
    rgba[..., 3] = np.clip(a, 0, 1)
    return rgba


VMT = '''"UnlitGeneric"
{
	"$basetexture" "vgui/hl2rpm/suit_holo_%s"
	"$translucent" "1"
	"$vertexcolor" "1"
	"$vertexalpha" "1"
	"$ignorez" "1"
	"$nolod" "1"
}
'''


def main():
    fs = GameFS(MOD, SDK, [os.path.join(SDK, "hl2")])
    out = os.path.join(MOD, "materials", "vgui", "hl2rpm")
    os.makedirs(out, exist_ok=True)
    for theme, (model, seq, yaw) in SUITS.items():
        pos, nrm, tris = posed_mesh(fs, model, seq)
        if yaw:
            c, s_ = np.cos(np.radians(yaw)), np.sin(np.radians(yaw))
            rot = np.array([[c, -s_, 0], [s_, c, 0], [0, 0, 1]])
            pos, nrm = pos @ rot.T, nrm @ rot.T
        zbuf, nbuf, scale = rasterize(pos, nrm, tris)
        rgba = hologram(zbuf, nbuf, scale)
        write_vtf_bgra(os.path.join(out, "suit_holo_%s.vtf" % theme), rgba)
        with open(os.path.join(out, "suit_holo_%s.vmt" % theme), "w", encoding="utf-8", newline="\n") as f:
            f.write(VMT % theme)
        print("%s: %s, %d triangles -> suit_holo_%s.vtf" % (theme, model, len(tris), theme))
        png = [a[6:] for a in sys.argv if a.startswith("--png=")]
        if png:
            from PIL import Image
            img = (np.clip(rgba[..., 3:4] * np.array([1.0, 0.7, 0.3]), 0, 1) * 255).astype(np.uint8)
            Image.fromarray(img).save(os.path.join(png[0], "holo_%s.png" % theme))
    return 0


if __name__ == "__main__":
    sys.exit(main())
