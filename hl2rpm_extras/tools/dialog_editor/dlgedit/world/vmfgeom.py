"""Map geometry straight from a Hammer VMF - no compile: every brush is cut from its planes (as vbsp does:
a big polygon on each side's plane clipped by the other planes), displacements from their dispinfo, texture
coordinates from uaxis / vaxis. The result looks like bsp.BSPMap to the map window (batches by material, water,
entities, bounds, triangles), so the window shows what Hammer++ has just saved.
Also the user's task markers for Claude (rpm_claude_task, func_rpm_claude_area): self.tasks."""
import re
import struct

import numpy as np

from .bsp import Batch

_TOKEN = re.compile(r'"((?:[^"\\]|\\.)*)"|([{}])|([^\s{}"]+)')
BIG = 65536.0


class Node:
    __slots__ = ("name", "props", "children")

    def __init__(self, name):
        self.name, self.props, self.children = name, [], []

    def get(self, key, default=""):
        k = key.lower()
        for a, b in self.props:
            if a.lower() == k:
                return b
        return default

    def sub(self, name):
        n = name.lower()
        return [c for c in self.children if c.name.lower() == n]


def parse(text):
    root = Node("root")
    stack = [root]
    pending = None
    for m in _TOKEN.finditer(text):
        s, brace, word = m.group(1), m.group(2), m.group(3)
        if brace == "{":
            n = Node(pending or "")
            stack[-1].children.append(n)
            stack.append(n)
            pending = None
        elif brace == "}":
            if len(stack) > 1:
                stack.pop()
            pending = None
        else:
            tok = s if s is not None else word
            if pending is None:
                pending = tok
            else:
                stack[-1].props.append((pending, tok))
                pending = None
    return root


def human(v):
    """Hammer writes what the user typed in the ANSI code page (cp1251 on a Russian Windows)."""
    raw = v.encode("utf-8", errors="surrogateescape")
    try:
        return raw.decode("utf-8")
    except UnicodeDecodeError:
        return raw.decode("cp1251", errors="replace")


_PT = re.compile(r"\(\s*([-\d.eE+]+)\s+([-\d.eE+]+)\s+([-\d.eE+]+)\s*\)")
_AX = re.compile(r"\[\s*([-\d.eE+]+)\s+([-\d.eE+]+)\s+([-\d.eE+]+)\s+([-\d.eE+]+)\s*\]\s*([-\d.eE+]+)")


def plane_of(side):
    p = [np.array([float(x) for x in m.groups()]) for m in _PT.finditer(side.get("plane"))]
    if len(p) < 3:
        return None
    n = np.cross(p[0] - p[1], p[2] - p[1])          # vbsp PlaneFromPoints: outward normal
    ln = np.linalg.norm(n)
    if ln < 1e-9:
        return None
    n /= ln
    return n, float(n @ p[0])


def base_winding(n, d):
    ax = int(np.argmax(np.abs(n)))
    up = np.array([0.0, 0.0, 1.0]) if ax != 2 else np.array([1.0, 0.0, 0.0])
    up -= n * (up @ n)
    up /= np.linalg.norm(up)
    right = np.cross(up, n)
    org = n * d
    up, right = up * BIG, right * BIG
    # (p0 - p1) x (p2 - p1) points along n, the same as the plane's points
    return [org - right + up, org + right + up, org + right - up, org - right - up]


def clip(w, n, d, eps=0.01):
    """the part of polygon w behind plane (n, d)"""
    out = []
    k = len(w)
    for i in range(k):
        a, b = w[i], w[(i + 1) % k]
        da, db = a @ n - d, b @ n - d
        if da <= eps:
            out.append(a)
        if (da < -eps and db > eps) or (da > eps and db < -eps):
            t = da / (da - db)
            out.append(a + (b - a) * t)
    return out


def _axis(s):
    m = _AX.search(s or "")
    if not m:
        return np.array([1.0, 0, 0]), 0.0, 0.25
    x, y, z, shift, scale = (float(v) for v in m.groups())
    return np.array([x, y, z]), shift, scale if abs(scale) > 1e-6 else 0.25


def _rows(block, key, n, width):
    """dispinfo rows (row0..rowN) as an array [n][n][width]"""
    out = np.zeros((n, n, width), np.float32)
    b = block.sub(key)
    if not b:
        return out
    b = b[0]
    for i in range(n):
        vals = b.get("row%d" % i)
        if vals:
            a = np.array([float(x) for x in vals.split()], np.float32)
            if a.size >= n * width:
                out[i] = a[:n * width].reshape(n, width)
    return out


class VMFMap:
    def __init__(self, path, materials=None):
        self.path = path
        with open(path, "rb") as f:
            text = f.read().decode("utf-8", errors="surrogateescape")
        self.root = parse(text)
        self.materials = materials
        self._tex_size = {}
        self.batches, self.water = {}, {}
        self.entities = []
        self.static_props = []
        self.tasks = []
        self.solid_count = self.side_count = self.disp_count = 0
        self.bounds = (np.zeros(3), np.zeros(3))
        self._build()

    # --- textures
    def _size(self, material):
        s = self._tex_size.get(material)
        if s is not None:
            return s
        s = (512, 512)
        if self.materials is not None:
            mi = self.materials.info(material)
            data = self.materials.fs.read("materials/%s.vtf" % mi.base) if mi.base else None
            if data and len(data) >= 20 and data[:4] == b"VTF\0":
                w, h = struct.unpack_from("<HH", data, 16)
                s = (max(1, w), max(1, h))
        self._tex_size[material] = s
        return s

    def _uv(self, side, mat, pos):
        ua, us, usc = _axis(side.get("uaxis"))
        va, vs, vsc = _axis(side.get("vaxis"))
        w, h = self._size(mat)
        u = (pos @ ua / usc + us) / w
        v = (pos @ va / vsc + vs) / h
        return np.stack([u, v], axis=1).astype(np.float32)

    def _add(self, mat, pos, nrm, uv, alpha, water):
        store = self.water if water else self.batches
        b = store.get(mat)
        if b is None:
            b = store[mat] = Batch(mat)
        b.pos.append(pos.astype(np.float32))
        b.nrm.append(nrm.astype(np.float32))
        b.uv.append(uv)
        b.alpha.append(alpha.astype(np.float32))

    # --- geometry
    def solid_faces(self, solid):
        """[(side, material, normal, polygon np.array(k, 3))] of one brush"""
        sides = solid.sub("side")
        planes = [plane_of(s) for s in sides]
        out = []
        for i, s in enumerate(sides):
            if planes[i] is None:
                continue
            n, d = planes[i]
            w = base_winding(n, d)
            for j, pj in enumerate(planes):
                if j == i or pj is None:
                    continue
                w = clip(w, pj[0], pj[1])
                if len(w) < 3:
                    break
            if len(w) >= 3:
                out.append((s, s.get("material").replace("\\", "/").lower(), n, np.array(w)))
        return out

    def _face(self, side, mat, n, poly):
        if mat.startswith("tools/"):
            return
        water = "water" in mat and not mat.startswith("models/")
        disp = side.sub("dispinfo")
        if disp:
            self._displacement(side, disp[0], mat, n, poly, water)
            return
        tri = np.empty(((len(poly) - 2) * 3, 3))
        tri[0::3] = poly[0]
        tri[1::3] = poly[1:-1]
        tri[2::3] = poly[2:]
        # the polygon keeps the plane points' winding - the same as vbsp's faces (checked against ck_test.bsp)
        self._add(mat, tri, np.broadcast_to(n, tri.shape).copy(), self._uv(side, mat, tri),
                  np.ones(len(tri)), water)

    def _displacement(self, side, di, mat, n, poly, water):
        if len(poly) != 4:
            return
        self.disp_count += 1
        power = int(float(di.get("power", "3")))
        cnt = (1 << power) + 1
        m = re.findall(r"[-\d.eE+]+", di.get("startposition"))
        start = np.array([float(x) for x in m[:3]]) if len(m) >= 3 else poly[0]
        k = int(np.argmin(((poly - start) ** 2).sum(1)))
        c = np.roll(poly, -k, axis=0)
        t = np.linspace(0.0, 1.0, cnt)
        a = c[0][None, :] + (c[1] - c[0])[None, :] * t[:, None]
        b = c[3][None, :] + (c[2] - c[3])[None, :] * t[:, None]
        grid = (a[:, None, :] + (b - a)[:, None, :] * t[None, :, None]).transpose(1, 0, 2)   # [row 0->3][col 0->1]
        nr = _rows(di, "normals", cnt, 3)
        dist = _rows(di, "distances", cnt, 1)
        off = _rows(di, "offsets", cnt, 3)
        alpha = _rows(di, "alphas", cnt, 1)[..., 0] / 255.0
        elev = float(di.get("elevation", "0") or 0)
        pos = grid + nr * dist + off + n * elev
        gy, gx = np.gradient(pos, axis=(0, 1))
        nrm = np.cross(gx, gy)
        nrm /= np.maximum(np.linalg.norm(nrm, axis=2, keepdims=True), 1e-6)
        if (nrm.reshape(-1, 3) @ n).mean() < 0:
            nrm = -nrm
        i, j = np.meshgrid(np.arange(cnt - 1), np.arange(cnt - 1), indexing="ij")
        i, j = i.ravel(), j.ravel()
        q00, q01, q10, q11 = i * cnt + j, i * cnt + j + 1, (i + 1) * cnt + j, (i + 1) * cnt + j + 1
        idx = np.stack([q00, q10, q11, q00, q11, q01], axis=1).ravel()
        P = pos.reshape(-1, 3)[idx]
        e1, e2 = P[1::3] - P[0::3], P[2::3] - P[0::3]
        if (np.cross(e1, e2) @ n).sum() < 0:
            idx = idx.reshape(-1, 3)[:, ::-1].ravel()
            P = pos.reshape(-1, 3)[idx]
        self._add(mat, P, nrm.reshape(-1, 3)[idx], self._uv(side, mat, P), alpha.reshape(-1)[idx], water)

    def _task(self, ent, solids):
        rec = {"id": ent.get("id"), "class": ent.get("classname"), "name": human(ent.get("targetname")),
               "task": " ".join(human(ent.get(k)) for k in ("task", "task2", "task3") if ent.get(k)),
               "kind": ent.get("kind"), "priority": ent.get("priority", "2"), "status": ent.get("status", "todo"),
               "reply": human(ent.get("reply"))}
        if solids:
            pts = np.concatenate([poly for s in solids for (_, _, _, poly) in self.solid_faces(s)] or [np.zeros((1, 3))])
            rec["mins"], rec["maxs"] = pts.min(0).tolist(), pts.max(0).tolist()
        else:
            o = [float(x) for x in (ent.get("origin") or "0 0 0").split()[:3]]
            rec["origin"] = o
            rec["mins"], rec["maxs"] = [v - 16 for v in o], [v + 16 for v in o]
        self.tasks.append(rec)

    def _build(self):
        owners = []
        for w in self.root.sub("world"):
            owners.append((None, w.sub("solid")))
        for e in self.root.sub("entity"):
            cls = e.get("classname").lower()
            solids = e.sub("solid")
            if cls.startswith(("rpm_claude_", "func_rpm_claude_")):
                self._task(e, solids)
                continue
            d = {k: human(v) for k, v in e.props}
            self.entities.append(d)
            if solids and not cls.startswith("trigger_") and cls not in (
                    "func_areaportal", "func_occluder", "func_viscluster", "func_areaportalwindow", "func_clip_vphysics"):
                owners.append((d, solids))
        for owner, solids in owners:
            for s in solids:
                self.solid_count += 1
                for side, mat, n, poly in self.solid_faces(s):
                    self.side_count += 1
                    self._face(side, mat, n, poly)
        for store in (self.batches, self.water):
            for b in store.values():
                b.pos = np.concatenate(b.pos)
                b.nrm = np.concatenate(b.nrm)
                b.uv = np.concatenate(b.uv)
                b.alpha = np.concatenate(b.alpha)
        allpos = [b.pos for b in self.batches.values()]
        if allpos:
            P = np.concatenate(allpos)
            self.bounds = (P.min(0), P.max(0))

    def triangles(self):
        if not self.batches:
            return np.zeros((0, 3, 3), dtype=np.float32)
        return np.concatenate([b.pos for b in self.batches.values()]).reshape(-1, 3, 3)
