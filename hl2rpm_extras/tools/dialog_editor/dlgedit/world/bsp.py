"""Compiled maps (.bsp v20/21) for the render window: world faces and displacements
grouped by material (positions, normals, texture coordinates, blend alpha), brush
entities, static props and the entity lump."""
import math
import re
import struct

import numpy as np

LUMP_ENTITIES, LUMP_PLANES, LUMP_TEXDATA, LUMP_VERTEXES = 0, 1, 2, 3
LUMP_TEXINFO, LUMP_FACES, LUMP_EDGES, LUMP_SURFEDGES, LUMP_MODELS = 6, 7, 12, 13, 14
LUMP_DISPINFO, LUMP_DISP_VERTS, LUMP_GAME_LUMP = 26, 33, 35
LUMP_TEXDATA_STRING_DATA, LUMP_TEXDATA_STRING_TABLE = 43, 44

SURF_SKY2D, SURF_SKY, SURF_TRIGGER, SURF_NODRAW, SURF_HINT, SURF_SKIP = 0x2, 0x4, 0x40, 0x80, 0x100, 0x200
SKIP_FLAGS = SURF_SKY2D | SURF_SKY | SURF_TRIGGER | SURF_NODRAW | SURF_HINT | SURF_SKIP

_PATCHED = re.compile(r"^maps/[^/]+/(.+?)(_-?\d+_-?\d+_-?\d+)?(_wvt_patch)?$", re.I)


def base_material(name):
    """maps/<map>/<material>_x_y_z (cubemap patch) and _wvt_patch -> <material>."""
    n = name.replace("\\", "/").lower()
    m = _PATCHED.match(n)
    if m and (m.group(2) or m.group(3)):
        return m.group(1)
    return n


def parse_entities(text):
    ents = []
    cur = None
    for m in re.finditer(r'"([^"]*)"|(\{)|(\})', text):
        q, lb, rb = m.groups()
        if lb:
            cur = {}
            key = None
        elif rb:
            if cur is not None:
                ents.append(cur)
            cur = None
        elif cur is not None:
            if key is None:
                key = q
            else:
                # outputs repeat their key: keep the first (classname etc. never repeat)
                cur.setdefault(key, q)
                key = None
    return ents


def angle_matrix(pitch, yaw, roll):
    """Source QAngle -> 3x3 rotation (columns: forward, left, up)."""
    p, y, r = (math.radians(a) for a in (pitch, yaw, roll))
    sp, cp = math.sin(p), math.cos(p)
    sy, cy = math.sin(y), math.cos(y)
    sr, cr = math.sin(r), math.cos(r)
    fwd = (cp * cy, cp * sy, -sp)
    left = (-(sr * sp * cy - cr * sy), -(sr * sp * sy + cr * cy), -(sr * cp))
    up = (cr * sp * cy + sr * sy, cr * sp * sy - sr * cy, cr * cp)
    # x along forward, y along left, z along up
    return np.array([[fwd[0], left[0], up[0]], [fwd[1], left[1], up[1]], [fwd[2], left[2], up[2]]], dtype=np.float32)


class Batch:
    """Triangles of one material: arrays of N*3 vertices."""
    __slots__ = ("material", "pos", "nrm", "uv", "alpha")

    def __init__(self, material):
        self.material = material
        self.pos, self.nrm, self.uv, self.alpha = [], [], [], []


class StaticProp:
    __slots__ = ("model", "origin", "angles", "skin", "solid")

    def __init__(self, model, origin, angles, skin, solid):
        self.model, self.origin, self.angles, self.skin, self.solid = model, origin, angles, skin, solid


class BSPMap:
    def __init__(self, path):
        self.path = path
        with open(path, "rb") as f:
            self.data = f.read()
        ident, self.version = struct.unpack_from("<4si", self.data, 0)
        if ident != b"VBSP":
            raise ValueError("not a BSP: %s" % path)
        self.lumps = [struct.unpack_from("<iiii", self.data, 8 + i * 16) for i in range(64)]
        self.entities = parse_entities(self._lump(LUMP_ENTITIES).decode("utf-8", "replace"))
        self._read_tables()
        self.batches = {}           # material -> Batch (world + brush entities)
        self.water = {}             # material -> Batch (drawn translucent)
        self.static_props = []
        self.bounds = (np.zeros(3), np.zeros(3))
        self._build()
        self._read_static_props()

    def _lump(self, i):
        ofs, ln, ver, _ = self.lumps[i]
        return self.data[ofs:ofs + ln]

    def _read_tables(self):
        self.vertexes = np.frombuffer(self._lump(LUMP_VERTEXES), dtype="<f4").reshape(-1, 3)
        self.edges = np.frombuffer(self._lump(LUMP_EDGES), dtype="<u2").reshape(-1, 2)
        self.surfedges = np.frombuffer(self._lump(LUMP_SURFEDGES), dtype="<i4")
        self.planes = np.frombuffer(self._lump(LUMP_PLANES), dtype=np.dtype([("n", "<f4", 3), ("d", "<f4"), ("t", "<i4")]))
        self.texinfo = np.frombuffer(self._lump(LUMP_TEXINFO), dtype=np.dtype([("tv", "<f4", (2, 4)), ("lv", "<f4", (2, 4)), ("flags", "<i4"), ("texdata", "<i4")]))
        self.texdata = np.frombuffer(self._lump(LUMP_TEXDATA), dtype=np.dtype([("refl", "<f4", 3), ("name", "<i4"), ("w", "<i4"), ("h", "<i4"), ("vw", "<i4"), ("vh", "<i4")]))
        strdata = self._lump(LUMP_TEXDATA_STRING_DATA)
        table = np.frombuffer(self._lump(LUMP_TEXDATA_STRING_TABLE), dtype="<i4")
        self.texnames = []
        for i in range(len(self.texdata)):
            o = table[self.texdata[i]["name"]]
            e = strdata.index(b"\0", o)
            self.texnames.append(base_material(strdata[o:e].decode("utf-8", "replace")))
        self.faces = np.frombuffer(self._lump(LUMP_FACES), dtype=np.dtype([
            ("plane", "<u2"), ("side", "u1"), ("onnode", "u1"), ("firstedge", "<i4"), ("numedges", "<i2"),
            ("texinfo", "<i2"), ("dispinfo", "<i2"), ("fog", "<i2"), ("styles", "u1", 4), ("lightofs", "<i4"),
            ("area", "<f4"), ("lmmins", "<i4", 2), ("lmsize", "<i4", 2), ("origface", "<i4"), ("numprims", "<u2"),
            ("firstprim", "<u2"), ("smoothing", "<u4")]))
        self.models = np.frombuffer(self._lump(LUMP_MODELS), dtype=np.dtype([
            ("mins", "<f4", 3), ("maxs", "<f4", 3), ("origin", "<f4", 3), ("headnode", "<i4"), ("firstface", "<i4"), ("numfaces", "<i4")]))
        dispraw = self._lump(LUMP_DISPINFO)
        self.dispinfo = [struct.unpack_from("<3fiiiifiH", dispraw, i * 176) for i in range(len(dispraw) // 176)]
        self.dispverts = np.frombuffer(self._lump(LUMP_DISP_VERTS), dtype=np.dtype([("vec", "<f4", 3), ("dist", "<f4"), ("alpha", "<f4")]))

    # ------------------------------------------------------------------ geometry
    def _face_verts(self, f):
        se = self.surfedges[f["firstedge"]:f["firstedge"] + f["numedges"]]
        idx = np.where(se >= 0, self.edges[np.abs(se), 0], self.edges[np.abs(se), 1])
        return self.vertexes[idx]

    def _uv(self, ti, pos):
        tv = self.texinfo[ti]["tv"]
        td = self.texdata[self.texinfo[ti]["texdata"]]
        w = max(1, td["w"])
        h = max(1, td["h"])
        u = (pos @ tv[0, :3] + tv[0, 3]) / w
        v = (pos @ tv[1, :3] + tv[1, 3]) / h
        return np.stack([u, v], axis=1)

    def _add(self, material, pos, nrm, uv, alpha, water):
        store = self.water if water else self.batches
        b = store.get(material)
        if b is None:
            b = store[material] = Batch(material)
        b.pos.append(pos)
        b.nrm.append(nrm)
        b.uv.append(uv)
        b.alpha.append(alpha)

    def _face(self, fi, xform=None):
        f = self.faces[fi]
        ti = f["texinfo"]
        if ti < 0:
            return
        tinfo = self.texinfo[ti]
        if tinfo["flags"] & SKIP_FLAGS:
            return
        mat = self.texnames[tinfo["texdata"]]
        if mat.startswith("tools/"):
            return
        water = "water" in mat and not mat.startswith("models/")
        if f["dispinfo"] >= 0:
            self._displacement(f, mat, water)
            return
        v = self._face_verts(f)
        if len(v) < 3:
            return
        n = self.planes[f["plane"]]["n"].copy()
        if f["side"]:
            n = -n
        # fan triangulation
        tri = np.empty(((len(v) - 2) * 3, 3), dtype=np.float32)
        tri[0::3] = v[0]
        tri[1::3] = v[1:-1]
        tri[2::3] = v[2:]
        uv = self._uv(ti, tri).astype(np.float32)
        if xform is not None:
            rot, org = xform
            tri = tri @ rot.T + org
            n = rot @ n
        nrm = np.broadcast_to(n, tri.shape).astype(np.float32)
        self._add(mat, tri, nrm, uv, np.ones(len(tri), dtype=np.float32), water)

    def _displacement(self, f, mat, water):
        d = self.dispinfo[f["dispinfo"]]
        start = np.array(d[0:3], dtype=np.float32)
        vstart, power = d[3], d[5]
        corners = self._face_verts(f)
        if len(corners) != 4:
            return
        k = int(np.argmin(((corners - start) ** 2).sum(1)))
        c = np.roll(corners, -k, axis=0)
        n = (1 << power) + 1
        t = np.linspace(0.0, 1.0, n, dtype=np.float32)
        # rows go from corner 0 to corner 1 (and 3 to 2), columns across
        a = c[0][None, :] + (c[1] - c[0])[None, :] * t[:, None]
        b = c[3][None, :] + (c[2] - c[3])[None, :] * t[:, None]
        grid = a[:, None, :] + (b - a)[:, None, :] * t[None, :, None]        # [i (0->1), j (0->3)]
        dv = self.dispverts[vstart:vstart + n * n]
        off = (dv["vec"] * dv["dist"][:, None]).reshape(n, n, 3)
        # disp verts are indexed [row along 0->3][column along 0->1]
        pos = grid.transpose(1, 0, 2) + off
        alpha = (dv["alpha"] / 255.0).reshape(n, n).astype(np.float32)
        # normals of the grid
        gy, gx = np.gradient(pos, axis=(0, 1))
        nrm = np.cross(gx, gy)
        ln = np.linalg.norm(nrm, axis=2, keepdims=True)
        nrm = nrm / np.maximum(ln, 1e-6)
        face_n = self.planes[f["plane"]]["n"] * (-1 if f["side"] else 1)
        if (nrm.reshape(-1, 3) @ face_n).mean() < 0:
            nrm = -nrm
        i, j = np.meshgrid(np.arange(n - 1), np.arange(n - 1), indexing="ij")
        i, j = i.ravel(), j.ravel()
        q00, q01, q10, q11 = i * n + j, i * n + j + 1, (i + 1) * n + j, (i + 1) * n + j + 1
        idx = np.stack([q00, q10, q11, q00, q11, q01], axis=1).ravel()
        P = pos.reshape(-1, 3)[idx].astype(np.float32)
        # keep the winding of the base face
        e1 = P[1::3] - P[0::3]
        e2 = P[2::3] - P[0::3]
        if (np.cross(e1, e2) @ face_n).sum() < 0:
            idx = idx.reshape(-1, 3)[:, ::-1].ravel()
            P = pos.reshape(-1, 3)[idx].astype(np.float32)
        N = nrm.reshape(-1, 3)[idx].astype(np.float32)
        A = alpha.reshape(-1)[idx]
        uv = self._uv(f["texinfo"], P).astype(np.float32)
        self._add(mat, P, N, uv, A, water)

    def _build(self):
        m0 = self.models[0]
        for fi in range(m0["firstface"], m0["firstface"] + m0["numfaces"]):
            self._face(fi)
        # brush entities: their model at the entity's origin and angles
        for e in self.entities:
            mdl = e.get("model", "")
            if not mdl.startswith("*"):
                continue
            cls = e.get("classname", "")
            if cls.startswith("trigger_") or cls in ("func_areaportal", "func_occluder", "func_viscluster", "func_areaportalwindow"):
                continue
            try:
                mi = int(mdl[1:])
                m = self.models[mi]
            except (ValueError, IndexError):
                continue
            org = np.array([float(x) for x in e.get("origin", "0 0 0").split()[:3]] or [0, 0, 0], dtype=np.float32)
            ang = [float(x) for x in e.get("angles", "0 0 0").split()[:3]] or [0, 0, 0]
            xf = (angle_matrix(*ang), org)
            for fi in range(m["firstface"], m["firstface"] + m["numfaces"]):
                self._face(fi, xf)
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
        """(M, 3, 3) world triangles for picking and snapping to the ground."""
        if not self.batches:
            return np.zeros((0, 3, 3), dtype=np.float32)
        return np.concatenate([b.pos for b in self.batches.values()]).reshape(-1, 3, 3)

    # ------------------------------------------------------------------ static props
    def _read_static_props(self):
        gl = self._lump(LUMP_GAME_LUMP)
        if len(gl) < 4:
            return
        count = struct.unpack_from("<i", gl, 0)[0]
        for i in range(count):
            gid, flags, ver, fofs, flen = struct.unpack_from("<iHHii", gl, 4 + i * 16)
            if gid != 0x73707270:       # 'sprp'
                continue
            d = self.data[fofs:fofs + flen]
            pos = 0
            ndict = struct.unpack_from("<i", d, pos)[0]
            pos += 4
            names = []
            for k in range(ndict):
                names.append(d[pos:pos + 128].split(b"\0")[0].decode("utf-8", "replace").replace("\\", "/"))
                pos += 128
            nleaf = struct.unpack_from("<i", d, pos)[0]
            pos += 4 + nleaf * 2
            nprops = struct.unpack_from("<i", d, pos)[0]
            pos += 4
            if nprops <= 0:
                return
            size = (len(d) - pos) // nprops
            for k in range(nprops):
                o = pos + k * size
                ox, oy, oz, ap, ay, ar = struct.unpack_from("<6f", d, o)
                ptype = struct.unpack_from("<H", d, o + 24)[0]
                solid = d[o + 30]
                skin = struct.unpack_from("<i", d, o + 32)[0]
                if 0 <= ptype < len(names):
                    self.static_props.append(StaticProp(names[ptype], (ox, oy, oz), (ap, ay, ar), skin, solid))
            return
