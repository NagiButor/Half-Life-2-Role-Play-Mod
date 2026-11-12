"""Pictures of a map straight from its VMF - for Claude to look at a map the user is building in Hammer++
without compiling or starting the game (and for the user: CK, Hammer++ menu -> "Снимки карты для Claude").

  top.png / front.png / side.png - Hammer-like 2D views: dark grid, brush outlines (world grey, brush entities
                                   blue, displacements green), point entities as dots, the user's task markers
                                   (rpm_claude_task / func_rpm_claude_area) in Claude terracotta with their names;
  3d.png                         - a shaded perspective view (colors = average color of each material's texture);
  task_<name>_top.png / _3d.png  - the same around every task;
  info.json                      - bounds, solids/sides/displacements, materials, entities, tasks.

CLI:  python -m dlgedit.world.vmfshot <map.vmf> <out dir> [--mod <mod dir> --sdk <SDK dir>] [--size 1400]"""
import hashlib
import json
import math
import os
import re
import sys

import numpy as np
from PIL import Image, ImageDraw, ImageFont

from .vmfgeom import VMFMap

BG = (30, 31, 34)
GRID_FINE = (44, 46, 50)
GRID_BIG = (62, 65, 70)
WORLD = (205, 208, 214)
BRUSH_ENT = (110, 170, 255)
DISP = (120, 210, 120)
TASK = (217, 119, 87)
ENT_COLORS = [("npc_", (255, 96, 96)), ("light", (255, 220, 120)), ("prop_", (220, 140, 255)), ("info_player", (120, 255, 160)),
              ("item_", (120, 220, 255)), ("weapon_", (120, 220, 255))]


def _font(size):
    for f in ("segoeui.ttf", "arial.ttf"):
        try:
            return ImageFont.truetype(f, size)
        except OSError:
            pass
    return ImageFont.load_default()


def _hash_color(name):
    h = hashlib.md5(name.encode()).digest()
    return (90 + h[0] % 120, 90 + h[1] % 120, 90 + h[2] % 120)


def _dxt_mean(t):
    """average color of a DXT texture from the 565 end points of its smallest mip level's blocks"""
    try:
        w, h, blocks = t.data[-1]
        b = np.frombuffer(blocks, np.uint8)
        step = 8 if t.kind in ("dxt1", "dxt1a") else 16
        b = b[: len(b) // step * step].reshape(-1, step)
        cb = b[:, step - 8:step - 4].view("<u2").astype(np.int32)
        cols = []
        for k in range(2):
            c = cb[:, k]
            cols.append(np.stack([(c >> 11) * 255 / 31, ((c >> 5) & 63) * 255 / 63, (c & 31) * 255 / 31], 1))
        m = np.concatenate(cols).mean(0)
        return tuple(int(x) for x in m)
    except Exception:
        return None


def material_color(materials, name):
    if materials is not None:
        try:
            mi = materials.info(name)
            t = materials.texture(mi.base) if mi.base else None
            if t is not None:
                if t.kind == "rgba":
                    m = np.asarray(t.data, np.float32).reshape(-1, 4)[:, :3].mean(0)
                    return tuple(int(x * mi.tint[i]) for i, x in enumerate(m))
                c = _dxt_mean(t)
                if c:
                    return tuple(int(c[i] * mi.tint[i]) for i in range(3))
        except Exception:
            pass
    return _hash_color(name)


class Shot:
    def __init__(self, vmf_path, materials=None):
        self.path = vmf_path
        self.g = VMFMap(vmf_path, materials)
        self.materials = materials
        self.colors = {}
        # outlines: (polygon, color) for the 2D views
        self.outlines = []
        world_ids = set()
        for w in self.g.root.sub("world"):
            for s in w.sub("solid"):
                world_ids.add(id(s))
        owners = [(None, s) for w in self.g.root.sub("world") for s in w.sub("solid")]
        for e in self.g.root.sub("entity"):
            cls = e.get("classname").lower()
            if cls.startswith(("rpm_claude_", "func_rpm_claude_")):
                continue
            owners += [(cls, s) for s in e.sub("solid")]
        for cls, s in owners:
            for side, mat, n, poly in self.g.solid_faces(s):
                col = DISP if side.sub("dispinfo") else (WORLD if cls is None else BRUSH_ENT)
                self.outlines.append((poly, col))
        self.points = []
        for e in self.g.entities:
            o = e.get("origin")
            if not o:
                continue
            try:
                p = [float(x) for x in o.split()[:3]]
            except ValueError:
                continue
            cls = e.get("classname", "")
            col = next((c for k, c in ENT_COLORS if cls.startswith(k) or k in cls), (200, 200, 200))
            self.points.append((p, col, e.get("targetname", "") or "", cls))
        lo, hi = self.g.bounds
        if not np.any(hi - lo):
            pts = np.concatenate([p for p, _ in self.outlines]) if self.outlines else np.zeros((2, 3))
            lo, hi = pts.min(0), pts.max(0)
        self.lo, self.hi = np.array(lo, float), np.array(hi, float)

    def color(self, mat):
        c = self.colors.get(mat)
        if c is None:
            c = self.colors[mat] = material_color(self.materials, mat)
        return c

    # ------------------------------------------------------------------ 2D
    def view2d(self, axis, size, lo=None, hi=None, title=""):
        """axis: "top" (x right, y up), "front" (x right, z up), "side" (y right, z up)"""
        lo = self.lo if lo is None else np.array(lo, float)
        hi = self.hi if hi is None else np.array(hi, float)
        ia, ib = {"top": (0, 1), "front": (0, 2), "side": (1, 2)}[axis]
        span = max(hi[ia] - lo[ia], hi[ib] - lo[ib], 64.0) * 1.08
        ca, cb = (lo[ia] + hi[ia]) / 2, (lo[ib] + hi[ib]) / 2
        W = H = size
        sc = size / span

        def to(p):
            return ((p[..., ia] - ca) * sc + W / 2, H / 2 - (p[..., ib] - cb) * sc)
        img = Image.new("RGB", (W, H), BG)
        d = ImageDraw.Draw(img)
        # grid: a power of two that gives lines 12+ px apart, big lines every 8
        step = 16
        while step * sc < 12:
            step *= 2
        a0, a1 = ca - W / 2 / sc, ca + W / 2 / sc
        b0, b1 = cb - H / 2 / sc, cb + H / 2 / sc
        for k in range(int(math.floor(a0 / step)), int(math.ceil(a1 / step)) + 1):
            x = (k * step - ca) * sc + W / 2
            d.line([(x, 0), (x, H)], fill=GRID_BIG if k % 8 == 0 else GRID_FINE)
        for k in range(int(math.floor(b0 / step)), int(math.ceil(b1 / step)) + 1):
            y = H / 2 - (k * step - cb) * sc
            d.line([(0, y), (W, y)], fill=GRID_BIG if k % 8 == 0 else GRID_FINE)
        for poly, col in self.outlines:
            x, y = to(poly)
            if x.max() < 0 or x.min() > W or y.max() < 0 or y.min() > H:
                continue
            d.line(list(zip(x, y)) + [(x[0], y[0])], fill=col, width=1)
        f = _font(12)
        for p, col, name, cls in self.points:
            x, y = to(np.array(p))
            if 0 <= x < W and 0 <= y < H:
                d.rectangle([x - 2, y - 2, x + 2, y + 2], fill=col)
        for t in self.g.tasks:
            mn, mx = np.array(t["mins"]), np.array(t["maxs"])
            x0, y0 = to(mn)
            x1, y1 = to(mx)
            d.rectangle([min(x0, x1), min(y0, y1), max(x0, x1), max(y0, y1)], outline=TASK, width=2)
            d.text((min(x0, x1) + 3, min(y0, y1) + 2), "AI: " + (t.get("name") or "?"), fill=TASK, font=_font(14))
        hdr = "%s  %s  |  grid %d u  |  %s" % (os.path.basename(self.path), axis, step,
                                             "x %.0f..%.0f  %s %.0f..%.0f" % (ca - W / 2 / sc, ca + W / 2 / sc, "yz"[ib - 1] if ib else "y",
                                                                              cb - H / 2 / sc, cb + H / 2 / sc))
        d.rectangle([0, 0, W, 20], fill=(20, 20, 22))
        d.text((6, 3), (title + "  " if title else "") + hdr, fill=(220, 220, 220), font=f)
        return img

    # ------------------------------------------------------------------ 3D
    def view3d(self, size, lo=None, hi=None, yaw=-135.0, pitch=35.0, title=""):
        lo = self.lo if lo is None else np.array(lo, float)
        hi = self.hi if hi is None else np.array(hi, float)
        W, H = size, int(size * 0.62)
        center = (lo + hi) / 2
        radius = max(np.linalg.norm(hi - lo) / 2, 64.0)
        fov = math.radians(60)
        dist = radius / math.sin(fov / 2) * 0.95
        cy, sy = math.cos(math.radians(yaw)), math.sin(math.radians(yaw))
        cp, sp = math.cos(math.radians(pitch)), math.sin(math.radians(pitch))
        fwd = np.array([cy * cp, sy * cp, -sp])
        eye = center - fwd * dist
        right = np.cross(fwd, [0, 0, 1.0])
        right /= np.linalg.norm(right)
        up = np.cross(right, fwd)
        f = (W / 2) / math.tan(fov / 2)
        light = np.array([0.35, 0.55, 0.76])
        light /= np.linalg.norm(light)
        tris, cols = [], []
        for store in (self.g.batches, self.g.water):
            for mat, b in store.items():
                P = b.pos.reshape(-1, 3, 3).astype(np.float64)
                N = b.nrm.reshape(-1, 3, 3)[:, 0].astype(np.float64)
                base = np.array(self.color(mat), float)
                shade = 0.45 + 0.55 * np.abs(N @ light)
                tris.append(P)
                cols.append(np.clip(base[None, :] * shade[:, None], 0, 255))
        frame = np.empty((H, W, 3), np.float32)
        frame[:] = (88, 104, 122)
        zbuf = np.full((H, W), np.inf, np.float32)
        if tris:
            P = np.concatenate(tris)
            C = np.concatenate(cols)
            rel = P - eye
            cam = np.stack([rel @ right, rel @ up, rel @ fwd], -1)       # x right, y up, z forward
            near = 4.0
            for i in range(len(cam)):
                tri = cam[i]
                if (tri[:, 2] <= near).all():
                    continue
                poly = list(tri)
                if (tri[:, 2] <= near).any():          # clip against the near plane
                    out = []
                    for k in range(3):
                        a, b = poly[k], poly[(k + 1) % 3]
                        if a[2] > near:
                            out.append(a)
                        if (a[2] > near) != (b[2] > near):
                            t = (near - a[2]) / (b[2] - a[2])
                            out.append(a + (b - a) * t)
                    poly = out
                if len(poly) < 3:
                    continue
                pts = np.array(poly)
                sx = W / 2 + pts[:, 0] / pts[:, 2] * f
                sy = H / 2 - pts[:, 1] / pts[:, 2] * f
                iz = 1.0 / pts[:, 2]
                for k in range(1, len(pts) - 1):
                    _raster(frame, zbuf, (sx[0], sx[k], sx[k + 1]), (sy[0], sy[k], sy[k + 1]),
                            (iz[0], iz[k], iz[k + 1]), C[i])
        img = Image.fromarray(np.clip(frame, 0, 255).astype(np.uint8))
        d = ImageDraw.Draw(img)

        def proj(p):
            r = np.array(p, float) - eye
            zz = r @ fwd
            if zz <= 4:
                return None
            return (W / 2 + (r @ right) / zz * f, H / 2 - (r @ up) / zz * f)
        for t in self.g.tasks:
            mn, mx = t["mins"], t["maxs"]
            cs = [(a, b, c) for a in (mn[0], mx[0]) for b in (mn[1], mx[1]) for c in (mn[2], mx[2])]
            pp = [proj(c) for c in cs]
            for a, b in ((0, 1), (2, 3), (4, 5), (6, 7), (0, 2), (1, 3), (4, 6), (5, 7), (0, 4), (1, 5), (2, 6), (3, 7)):
                if pp[a] and pp[b]:
                    d.line([pp[a], pp[b]], fill=TASK, width=2)
            top = proj([(mn[0] + mx[0]) / 2, (mn[1] + mx[1]) / 2, mx[2]])
            if top:
                d.text((top[0] + 4, top[1] - 16), "AI: " + (t.get("name") or "?"), fill=TASK, font=_font(15))
        d.rectangle([0, 0, W, 20], fill=(20, 20, 22))
        d.text((6, 3), "%s%s  3D  yaw %.0f pitch %.0f" % ((title + "  ") if title else "", os.path.basename(self.path), yaw, pitch),
               fill=(220, 220, 220), font=_font(12))
        return img

    def info(self):
        mats = {}
        for store in (self.g.batches, self.g.water):
            for m, b in store.items():
                mats[m] = mats.get(m, 0) + len(b.pos) // 3
        return {"vmf": self.path, "bounds": [self.lo.tolist(), self.hi.tolist()],
                "solids": self.g.solid_count, "sides": self.g.side_count, "displacements": self.g.disp_count,
                "materials": dict(sorted(mats.items(), key=lambda x: -x[1])),
                "entities": [{"class": e.get("classname"), "name": e.get("targetname", ""), "origin": e.get("origin", "")}
                             for e in self.g.entities],
                "tasks": self.g.tasks}


def _raster(frame, zbuf, xs, ys, izs, color):
    """one triangle into the frame with a depth test (depth = 1/z, interpolated linearly on the screen)"""
    H, W = zbuf.shape
    x0, x1 = max(int(math.floor(min(xs))), 0), min(int(math.ceil(max(xs))), W - 1)
    y0, y1 = max(int(math.floor(min(ys))), 0), min(int(math.ceil(max(ys))), H - 1)
    if x0 > x1 or y0 > y1:
        return
    (ax, bx, cx), (ay, by, cy) = xs, ys
    den = (by - cy) * (ax - cx) + (cx - bx) * (ay - cy)
    if abs(den) < 1e-9:
        return
    gy, gx = np.mgrid[y0:y1 + 1, x0:x1 + 1]
    px, py = gx + 0.5, gy + 0.5
    w0 = ((by - cy) * (px - cx) + (cx - bx) * (py - cy)) / den
    w1 = ((cy - ay) * (px - cx) + (ax - cx) * (py - cy)) / den
    w2 = 1.0 - w0 - w1
    inside = (w0 >= -1e-4) & (w1 >= -1e-4) & (w2 >= -1e-4)
    if not inside.any():
        return
    iz = w0 * izs[0] + w1 * izs[1] + w2 * izs[2]
    z = 1.0 / np.maximum(iz, 1e-9)
    sub = zbuf[y0:y1 + 1, x0:x1 + 1]
    win = inside & (z < sub)
    sub[win] = z[win]
    frame[y0:y1 + 1, x0:x1 + 1][win] = color


def _safe(s):
    return re.sub(r"[^\w.-]+", "_", s or "task")[:40]


def render_all(vmf_path, out_dir, materials=None, size=1400):
    os.makedirs(out_dir, exist_ok=True)
    s = Shot(vmf_path, materials)
    files = []
    for axis in ("top", "front", "side"):
        p = os.path.join(out_dir, axis + ".png")
        s.view2d(axis, size).save(p)
        files.append(p)
    p = os.path.join(out_dir, "3d.png")
    s.view3d(size).save(p)
    files.append(p)
    for t in s.g.tasks:
        mn, mx = np.array(t["mins"]), np.array(t["maxs"])
        pad = np.maximum((mx - mn) * 0.6, 256.0)
        lo, hi = mn - pad, mx + pad
        name = _safe(t.get("name"))
        for kind, img in (("top", s.view2d("top", size // 2 + 300, lo, hi, "AI: " + (t.get("name") or ""))),
                          ("3d", s.view3d(size // 2 + 300, lo, hi, title="AI: " + (t.get("name") or "")))):
            p = os.path.join(out_dir, "task_%s_%s.png" % (name, kind))
            img.save(p)
            files.append(p)
    p = os.path.join(out_dir, "info.json")
    with open(p, "w", encoding="utf-8") as f:
        json.dump(s.info(), f, ensure_ascii=False, indent=1)
    files.append(p)
    return files


def main(argv):
    if len(argv) < 3:
        print(__doc__)
        return 1
    vmf, out = argv[1], argv[2]
    rest = argv[3:]

    def opt(k, d=None):
        return rest[rest.index(k) + 1] if k in rest else d
    materials = None
    mod = opt("--mod", r"E:\Steam\steamapps\sourcemods\hl2rpm")
    sdk = opt("--sdk", r"E:\Steam\steamapps\common\Source SDK Base 2013 Singleplayer")
    if mod and os.path.isdir(mod):
        try:
            from .gamefs import GameFS
            from .materials import MaterialSystem
            materials = MaterialSystem(GameFS(mod, sdk, [os.path.join(mod, "testmaps")]), 64)
        except Exception as e:
            print("materials unavailable:", e)
    files = render_all(vmf, out, materials, int(opt("--size", "1400")))
    for f in files:
        print(f)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
