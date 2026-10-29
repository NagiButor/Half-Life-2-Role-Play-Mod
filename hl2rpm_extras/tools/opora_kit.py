"""Architecture kit for procedurally built City 17 maps (used by make_opora.py).

The building shell is made *subtractively*: an envelope (a box of solid masonry) minus the
spaces inside it (rooms, corridors, stairwells) and the openings (doors, windows). The solid
rest is cut into axis aligned cells on the coordinates of all those boxes and merged greedily
into brushes. Every face of a merged brush looks into exactly one space (or into the outside),
so its material is known: a room face gets the room's floor / wall / ceiling material (two-tone
wall textures are aligned to the room floor), a face to the outside gets the envelope's facade
material for that storey, a face against other masonry gets nodraw.

Everything else (trims, frames, stairs, furniture brushes) is additive: func_detail boxes and
wedges with explicit materials. Models are checked against the mounted HL2 / EP1 / EP2 content
(the unpacked VPKs) and placed on the floor from their bounding boxes.
"""
import math
import os
import re
import struct
import sys

sys.path.insert(0, r"E:\SourceModding\HL2RPMSOURCE\.claude\skills\hl2rpm-hammer\scripts")
from vmf import VMF, Node, parse  # noqa: E402
import new_test_map  # noqa: E402

REF = r"E:\SourceModding\UsefulStuff\Source sdk 2013 HL2 unpacked vpk"
GAMES = ["ep2", "episodic", "hl2"]
NODRAW = "TOOLS/TOOLSNODRAW"
SKY = "TOOLS/TOOLSSKYBOX"
PCLIP = "TOOLS/TOOLSPLAYERCLIP"
NPCCLIP = "TOOLS/TOOLSNPCCLIP"
TRIGGER = "TOOLS/TOOLSTRIGGER"
SCALE = 0.25
EPS = 0.01

# ---------------------------------------------------------------- content checks
MISSING = set()
_mdl = {}
_vmt = {}
_tex = {}


def find_ref(rel):
    for g in GAMES:
        p = os.path.join(REF, g, rel)
        if os.path.exists(p):
            return p
    return None


def vmt_text(mat):
    k = mat.lower()
    if k not in _vmt:
        p = find_ref(os.path.join("materials", k + ".vmt"))
        _vmt[k] = re.sub(r"//[^\n]*", "", open(p, encoding="latin-1").read()) if p else None
    return _vmt[k]


def mat_ok(mat):
    if mat.upper().startswith("TOOLS/"):
        return True
    if vmt_text(mat) is None:
        MISSING.add("material " + mat)
        return False
    return True


def tex_size(mat):
    """(w, h) of the base texture in texels (512 if unknown)"""
    k = mat.lower()
    if k in _tex:
        return _tex[k]
    res = (512, 512)
    t = vmt_text(k)
    if t:
        mm = re.search(r'"?\$basetexture"?\s+"?([^"\s]+)', t, re.I)
        if mm:
            p = find_ref(os.path.join("materials", mm.group(1).replace("\\", "/").lower() + ".vtf"))
            if p:
                with open(p, "rb") as f:
                    d = f.read(24)
                w, h = struct.unpack_from("<HH", d, 16)
                res = (w, h)
    _tex[k] = res
    return res


def mdl(rel):
    """{'mins','maxs','static','phy'} of a model or None (recorded as missing)"""
    k = rel.lower().replace("\\", "/")
    if k in _mdl:
        return _mdl[k]
    info = None
    p = find_ref(k)
    if p:
        with open(p, "rb") as f:
            h = f.read(160)
        if len(h) >= 160 and h[:4] == b"IDST":
            hmin = struct.unpack_from("<3f", h, 104)
            hmax = struct.unpack_from("<3f", h, 116)
            vmin = struct.unpack_from("<3f", h, 128)
            vmax = struct.unpack_from("<3f", h, 140)
            fl = struct.unpack_from("<i", h, 152)[0]
            if all(abs(v) < 1e-3 for v in vmin + vmax):
                vmin, vmax = hmin, hmax
            info = {"mins": vmin, "maxs": vmax, "static": bool(fl & 0x10),
                    "phy": find_ref(k[:-4] + ".phy") is not None}
    if info is None:
        MISSING.add("model " + rel)
    _mdl[k] = info
    return info


# ---------------------------------------------------------------- the map
class Map:
    def __init__(self, path, skyname="sky_day01_01"):
        self.m = VMF(parse(new_test_map.HEADER))
        self.m.path = path
        self.m.world.set("skyname", skyname)
        self.spaces = []           # Space
        self.envelopes = []        # Envelope
        self.faces = []            # (side_id, normal, plane_d, rect(mins,maxs)) for overlays
        self.detail_groups = {}    # name -> [solid]
        self.obstacles = []        # (x0,y0,x1,y1,z0,z1) blocking node placement
        self.counts = {"world": 0, "detail": 0, "props": 0}

    # ------------------------------------------------------------ brush primitives
    def _side(self, pts, mat, uaxis, vaxis, uoff=0, voff=0, lm=16, scale=SCALE):
        s = Node("side")
        sid = self.m.new_side_id()
        s.add("id", sid)
        s.add("plane", " ".join("(%g %g %g)" % tuple(p) for p in pts))
        s.add("material", mat)
        s.add("uaxis", "[%g %g %g %g] %g" % (uaxis[0], uaxis[1], uaxis[2], uoff, scale))
        s.add("vaxis", "[%g %g %g %g] %g" % (vaxis[0], vaxis[1], vaxis[2], voff, scale))
        s.add("rotation", "0")
        s.add("lightmapscale", str(lm))
        s.add("smoothing_groups", "0")
        return s, sid

    FACE_N = {"top": (0, 0, 1), "bottom": (0, 0, -1), "left": (-1, 0, 0), "right": (1, 0, 0),
              "back": (0, 1, 0), "front": (0, -1, 0)}
    FACE_UV = {"top": ((1, 0, 0), (0, -1, 0)), "bottom": ((1, 0, 0), (0, -1, 0)),
               "left": ((0, 1, 0), (0, 0, -1)), "right": ((0, 1, 0), (0, 0, -1)),
               "back": ((1, 0, 0), (0, 0, -1)), "front": ((1, 0, 0), (0, 0, -1))}

    def box_solid(self, a, b, mats, offs=None, lm=16, scale=SCALE, record=True):
        """axis aligned box. mats: str or dict face->mat (keys top bottom left right back front,
        also 'sides' and 'all'); offs: dict face->(uoff, voff)."""
        x1, y1, z1 = a
        x2, y2, z2 = b
        assert x1 < x2 and y1 < y2 and z1 < z2, (a, b)
        if isinstance(mats, str):
            mats = {"all": mats}
        planes = {
            "top": ((x1, y2, z2), (x2, y2, z2), (x2, y1, z2)),
            "bottom": ((x1, y1, z1), (x2, y1, z1), (x2, y2, z1)),
            "left": ((x1, y2, z2), (x1, y1, z2), (x1, y1, z1)),
            "right": ((x2, y2, z1), (x2, y1, z1), (x2, y1, z2)),
            "back": ((x2, y2, z2), (x1, y2, z2), (x1, y2, z1)),
            "front": ((x2, y1, z1), (x1, y1, z1), (x1, y1, z2)),
        }
        s = Node("solid")
        s.add("id", self.m.new_id())
        for f in ("top", "bottom", "left", "right", "back", "front"):
            mat = mats.get(f) or (mats.get("sides") if f not in ("top", "bottom") else None) or mats.get("all") or NODRAW
            mat_ok(mat)
            u, v = self.FACE_UV[f]
            uo, vo = (offs or {}).get(f, (0, 0))
            side, sid = self._side(planes[f], mat, u, v, uo, vo, lm, scale)
            s.add_child(side)
            if record and not mat.upper().startswith("TOOLS/"):
                n = self.FACE_N[f]
                ax = [i for i in range(3) if n[i]][0]
                d = (b if n[ax] > 0 else a)[ax]
                lo = list(a)
                hi = list(b)
                lo[ax] = hi[ax] = d
                self.faces.append((sid, n, d, tuple(lo), tuple(hi)))
        s.add_child(Node("editor", [["color", "0 180 220"], ["visgroupshown", "1"], ["visgroupautoshown", "1"]]))
        return s

    def solid_from_points(self, faces, lm=16):
        """faces: [(p0, p1, p2, material)], outward normal = (p0-p1) x (p2-p1)"""
        s = Node("solid")
        s.add("id", self.m.new_id())
        for p0, p1, p2, mat in faces:
            mat_ok(mat)
            t1 = [p0[i] - p1[i] for i in range(3)]
            t2 = [p2[i] - p1[i] for i in range(3)]
            n = (t1[1] * t2[2] - t1[2] * t2[1], t1[2] * t2[0] - t1[0] * t2[2], t1[0] * t2[1] - t1[1] * t2[0])
            an = [abs(c) for c in n]
            if an[2] >= an[0] and an[2] >= an[1]:
                u, v = (1, 0, 0), (0, -1, 0)
            elif an[0] >= an[1]:
                u, v = (0, 1, 0), (0, 0, -1)
            else:
                u, v = (1, 0, 0), (0, 0, -1)
            side, _ = self._side((p0, p1, p2), mat, u, v, 0, 0, lm)
            s.add_child(side)
        s.add_child(Node("editor", [["color", "0 180 220"], ["visgroupshown", "1"], ["visgroupautoshown", "1"]]))
        return s

    def ramp_solid(self, axis, c0, c1, l0, h0, l1, h1, thick, mat):
        """a slab whose top runs from (l0, h0) to (l1, h1) along axis ('x' or 'y'), across the
        other horizontal axis from c0 to c1, 'thick' units thick (vertically)"""
        def P(l, c, z):
            return (l, c, z) if axis == "x" else (c, l, z)
        if l0 > l1:
            l0, h0, l1, h1 = l1, h1, l0, h0
        hb0, hb1 = h0 - thick, h1 - thick
        # 6 faces: top slope, bottom slope, the two ends, the two sides
        A0, A1 = P(l0, c0, h0), P(l1, c0, h1)
        B0, B1 = P(l0, c1, h0), P(l1, c1, h1)
        a0, a1 = P(l0, c0, hb0), P(l1, c0, hb1)
        b0, b1 = P(l0, c1, hb0), P(l1, c1, hb1)
        faces = []

        def add(p0, p1, p2):
            # orient so that the normal points away from the centroid
            cen = [(A0[i] + A1[i] + B0[i] + B1[i] + a0[i] + a1[i] + b0[i] + b1[i]) / 8.0 for i in range(3)]
            t1 = [p0[i] - p1[i] for i in range(3)]
            t2 = [p2[i] - p1[i] for i in range(3)]
            n = (t1[1] * t2[2] - t1[2] * t2[1], t1[2] * t2[0] - t1[0] * t2[2], t1[0] * t2[1] - t1[1] * t2[0])
            dvec = [p1[i] - cen[i] for i in range(3)]
            if sum(n[i] * dvec[i] for i in range(3)) < 0:
                p0, p2 = p2, p0
            faces.append((p0, p1, p2, mat))
        add(A0, A1, B1)          # top
        add(a0, a1, b1)          # bottom
        add(A0, B0, b0)          # low end
        add(A1, B1, b1)          # high end
        add(A0, A1, a1)          # side c0
        add(B0, B1, b1)          # side c1
        return self.solid_from_points(faces)

    # ------------------------------------------------------------ adding brushes
    def world(self, a, b, mats, offs=None, lm=16):
        if not (a[0] < b[0] and a[1] < b[1] and a[2] < b[2]):
            return None
        s = self.box_solid(a, b, mats, offs, lm)
        w = self.m.world
        idx = len(w.children)
        for i, c in enumerate(w.children):
            if c.name.lower() in ("editor", "hidden", "group"):
                idx = i
                break
        w.add_child(s, idx)
        self.counts["world"] += 1
        return s

    def detail(self, a, b, mats, group="detail", offs=None, lm=16, block=False):
        if not (a[0] < b[0] and a[1] < b[1] and a[2] < b[2]):
            return None
        s = self.box_solid(a, b, mats, offs, lm)
        self.detail_groups.setdefault(group, []).append(s)
        self.counts["detail"] += 1
        if block:
            self.obstacles.append((a[0], a[1], b[0], b[1], a[2], b[2]))
        return s

    def detail_solid(self, s, group="detail"):
        self.detail_groups.setdefault(group, []).append(s)
        self.counts["detail"] += 1
        return s

    def brush_entity(self, classname, solids, **keys):
        return self.m.add_brush_entity(classname, solids, **keys)

    def ent(self, classname, pos, **keys):
        return self.m.add_entity(classname, pos, **keys)

    def output(self, e, out, target, inp, param="", delay=0, times=-1):
        return self.m.add_output(e, out, target, inp, param, delay, times)

    # ------------------------------------------------------------ props
    def prop(self, model, x, y, z, yaw=0, cls=None, pitch=0, roll=0, on_floor=True, block=True, **keys):
        """place a model; z is the floor height (the model's bounding box bottom goes there)
        unless on_floor=False (then z is the origin). cls: prop_static (default when the model
        allows it), prop_dynamic, prop_physics."""
        info = mdl(model)
        if not info:
            return None
        oz = z - info["mins"][2] if on_floor and pitch == 0 and roll == 0 else z
        if cls is None:
            cls = "prop_static" if info["static"] else "prop_dynamic"
        if cls == "prop_static" and not info["static"]:
            cls = "prop_dynamic"
        if cls == "prop_physics" and not info["phy"]:
            cls = "prop_static" if info["static"] else "prop_dynamic"
        kv = {"model": model, "angles": "%g %g %g" % (pitch, yaw, roll)}
        if cls == "prop_static":
            kv.setdefault("solid", "6")
            kv.setdefault("skin", "0")
            kv.setdefault("disableshadows", "0")
        elif cls == "prop_dynamic":
            kv.setdefault("solid", "6")
            kv.setdefault("DefaultAnim", "")
            kv.setdefault("spawnflags", "0")
            kv.setdefault("fademindist", "-1")
            kv.setdefault("fadescale", "1")
        elif cls == "prop_physics":
            kv.setdefault("spawnflags", "256")
            kv.setdefault("fademindist", "-1")
            kv.setdefault("fadescale", "1")
            kv.setdefault("physdamagescale", "0.1")
        kv.update(keys)
        e = self.m.add_entity(cls, (x, y, oz), **kv)
        self.counts["props"] += 1
        if block:
            mn, mx = info["mins"], info["maxs"]
            c, s_ = math.cos(math.radians(yaw)), math.sin(math.radians(yaw))
            xs = [x + px * c - py * s_ for px in (mn[0], mx[0]) for py in (mn[1], mx[1])]
            ys = [y + px * s_ + py * c for px in (mn[0], mx[0]) for py in (mn[1], mx[1])]
            if (mx[0] - mn[0]) > 10 or (mx[1] - mn[1]) > 10:
                self.obstacles.append((min(xs), min(ys), max(xs), max(ys), oz + mn[2], oz + mx[2]))
        return e

    def model_size(self, model):
        info = mdl(model)
        if not info:
            return (0, 0, 0)
        return tuple(info["maxs"][i] - info["mins"][i] for i in range(3))

    # ------------------------------------------------------------ spaces / envelopes
    def space(self, name, a, b, kind="in", **mats):
        s = Space(name, a, b, kind, **mats)
        self.spaces.append(s)
        return s

    def opening(self, a, b, reveal, name="opening"):
        return self.space(name, a, b, "open", reveal=reveal)

    def envelope(self, name, a, b, side, top="PROPS/RUBBERROOF002A", bottom=NODRAW, zlines=(), lm=16):
        e = Envelope(name, a, b, side, top, bottom, zlines, lm)
        self.envelopes.append(e)
        return e

    def voids_at(self, p):
        for s in self.spaces:
            if s.kind in ("in", "open") and s.contains(p):
                return s
        return None

    def label_at(self, p):
        """space index for a point outside any envelope cell grid (-1 = masonry, -2 = outside)"""
        for i, s in enumerate(self.spaces):
            if s.kind == "open" and s.contains(p):
                return i
        for i, s in enumerate(self.spaces):
            if s.kind == "in" and s.contains(p):
                return i
        for e in self.envelopes:
            if e.contains(p):
                return -1
        return -2

    def build_envelopes(self):
        for e in self.envelopes:
            self._build_envelope(e)

    def _build_envelope(self, env):
        a, b = env.a, env.b
        coords = [{a[i], b[i]} for i in range(3)]
        for s in self.spaces:
            if s.kind not in ("in", "open"):
                continue
            if not s.intersects(a, b):
                continue
            for i in range(3):
                for v in (s.a[i], s.b[i]):
                    if a[i] < v < b[i]:
                        coords[i].add(v)
            if s.kind == "in" and s.band:
                zb = s.floor_z + s.band
                if a[2] < zb < b[2] and s.a[2] < zb < s.b[2]:
                    coords[2].add(zb)
        for z in env.zlines:
            if a[2] < z < b[2]:
                coords[2].add(z)
        hard_z = set(env.zlines)
        for s in self.spaces:
            if s.kind == "in" and s.band:
                hard_z.add(s.floor_z + s.band)
        X, Y, Z = [sorted(c) for c in coords]
        nx, ny, nz = len(X) - 1, len(Y) - 1, len(Z) - 1
        lab = {}
        for i in range(nx):
            cx = (X[i] + X[i + 1]) * 0.5
            for j in range(ny):
                cy = (Y[j] + Y[j + 1]) * 0.5
                for k in range(nz):
                    cz = (Z[k] + Z[k + 1]) * 0.5
                    s = self.voids_at((cx, cy, cz))
                    lab[(i, j, k)] = -1 if s is None else self.spaces.index(s)

        def L(i, j, k, ax=None, sgn=0):
            """label of cell (i,j,k); outside the grid: global lookup next to the face of the
            neighbour cell it came from"""
            if 0 <= i < nx and 0 <= j < ny and 0 <= k < nz:
                return lab[(i, j, k)]
            # point just outside the envelope
            ci = min(max(i, 0), nx - 1)
            cj = min(max(j, 0), ny - 1)
            ck = min(max(k, 0), nz - 1)
            p = [(X[ci] + X[ci + 1]) * 0.5, (Y[cj] + Y[cj + 1]) * 0.5, (Z[ck] + Z[ck + 1]) * 0.5]
            if i < 0:
                p[0] = X[0] - 0.5
            if i >= nx:
                p[0] = X[-1] + 0.5
            if j < 0:
                p[1] = Y[0] - 0.5
            if j >= ny:
                p[1] = Y[-1] + 0.5
            if k < 0:
                p[2] = Z[0] - 0.5
            if k >= nz:
                p[2] = Z[-1] + 0.5
            return self.label_at(tuple(p))

        used = set()

        def face_labels(i0, i1, j0, j1, k0, k1, f):
            res = set()
            if f == "left":
                cells = [(i0 - 1, j, k) for j in range(j0, j1) for k in range(k0, k1)]
            elif f == "right":
                cells = [(i1, j, k) for j in range(j0, j1) for k in range(k0, k1)]
            elif f == "front":
                cells = [(i, j0 - 1, k) for i in range(i0, i1) for k in range(k0, k1)]
            elif f == "back":
                cells = [(i, j1, k) for i in range(i0, i1) for k in range(k0, k1)]
            elif f == "bottom":
                cells = [(i, j, k0 - 1) for i in range(i0, i1) for j in range(j0, j1)]
            else:
                cells = [(i, j, k1) for i in range(i0, i1) for j in range(j0, j1)]
            for c in cells:
                l_ = L(*c)
                if l_ != -1:
                    res.add(l_)
            return res

        def ok(i0, i1, j0, j1, k0, k1):
            for i in range(i0, i1):
                for j in range(j0, j1):
                    for k in range(k0, k1):
                        if lab[(i, j, k)] != -1 or (i, j, k) in used:
                            return False
            for f in ("left", "right", "front", "back", "bottom", "top"):
                if len(face_labels(i0, i1, j0, j1, k0, k1, f)) > 1:
                    return False
            # never across a hard z line (storey / wall band) unless both sides see nothing
            for k in range(k0 + 1, k1):
                if Z[k] in hard_z:
                    return False
            return True

        boxes = []
        for k in range(nz):
            for j in range(ny):
                for i in range(nx):
                    if lab[(i, j, k)] != -1 or (i, j, k) in used:
                        continue
                    i1, j1, k1 = i + 1, j + 1, k + 1
                    while i1 < nx and ok(i, i1 + 1, j, j1, k, k1):
                        i1 += 1
                    while j1 < ny and ok(i, i1, j, j1 + 1, k, k1):
                        j1 += 1
                    while k1 < nz and ok(i, i1, j, j1, k, k1 + 1):
                        k1 += 1
                    for ii in range(i, i1):
                        for jj in range(j, j1):
                            for kk in range(k, k1):
                                used.add((ii, jj, kk))
                    boxes.append((i, i1, j, j1, k, k1))
        for (i0, i1, j0, j1, k0, k1) in boxes:
            bmin = (X[i0], Y[j0], Z[k0])
            bmax = (X[i1], Y[j1], Z[k1])
            mats = {}
            offs = {}
            for f in ("left", "right", "front", "back", "bottom", "top"):
                labs = face_labels(i0, i1, j0, j1, k0, k1, f)
                if not labs:
                    mats[f] = NODRAW
                    continue
                li = labs.pop()
                mat, off = self._face_material(env, li, f, bmin, bmax)
                mats[f] = mat
                if off:
                    offs[f] = off
            if all(v == NODRAW for v in mats.values()):
                # fully buried masonry: still needed for sealing
                pass
            self.world(bmin, bmax, mats, offs, lm=env.lm)
        env.brushes = len(boxes)

    def _face_material(self, env, li, f, bmin, bmax):
        z0, z1 = bmin[2], bmax[2]
        if li == -2:
            # outside: facade of the envelope
            if f == "top":
                return env.top, None
            if f == "bottom":
                return env.bottom, None
            mat, ztop = env.side(z0, z1, f, bmin, bmax)
            off = None
            if ztop is not None:
                h = tex_size(mat)[1]
                off = (0, (ztop / SCALE) % h)
            return mat, off
        s = self.spaces[li]
        if s.kind == "open":
            return s.reveal, None
        if f == "top":
            return s.floor, None
        if f == "bottom":
            return s.ceil, None
        # wall: a solid face pointing into the space
        if s.band and z0 >= s.floor_z + s.band - EPS:
            return s.wall_hi or s.wall, None
        mat = s.wall
        h = tex_size(mat)[1]
        ztop = s.floor_z + h * SCALE
        return mat, (0, (ztop / SCALE) % h)

    # ------------------------------------------------------------ overlays / decals
    def overlay(self, material, center, normal, w, h, up=None, rotate=0):
        """info_overlay of w x h on the brush faces under it (found among the boxes made here)"""
        mat_ok(material)
        n = normal
        ax = [i for i in range(3) if abs(n[i]) > 0.5][0]
        d = center[ax]
        sides = []
        half = max(w, h) * 0.5 + 1
        for sid, fn, fd, lo, hi in self.faces:
            if fn != tuple(n) or abs(fd - d) > 0.05:
                continue
            ok = True
            for i in range(3):
                if i == ax:
                    continue
                if hi[i] < center[i] - half or lo[i] > center[i] + half:
                    ok = False
            if ok:
                sides.append(sid)
        if not sides:
            MISSING.add("overlay without face at %s %s" % (center, material))
            return None
        if ax == 2:
            U = (1, 0, 0) if not up else up
            V = (0, 1, 0)
            if rotate:
                c, s_ = math.cos(math.radians(rotate)), math.sin(math.radians(rotate))
                U = (c, s_, 0)
                V = (-s_, c, 0)
        else:
            V = (0, 0, 1)
            # U along the wall so that the image is not mirrored when looked at from the front
            U = (-n[1], n[0], 0) if ax == 1 else (-n[1], n[0], 0)
        hw, hh = w * 0.5, h * 0.5
        e = self.m.add_entity("info_overlay", center, material=material,
                              sides=" ".join(str(s) for s in sides[:64]),
                              BasisOrigin="%g %g %g" % tuple(center),
                              BasisNormal="%g %g %g" % tuple(n), BasisU="%g %g %g" % U, BasisV="%g %g %g" % V,
                              StartU="0", EndU="1", StartV="0", EndV="1", fademindist="-1",
                              uv0="%g %g 0" % (-hw, -hh), uv1="%g %g 0" % (-hw, hh),
                              uv2="%g %g 0" % (hw, hh), uv3="%g %g 0" % (hw, -hh))
        e.set("angles", "0 0 0")
        return e

    def decal(self, material, pos):
        mat_ok(material)
        return self.m.add_entity("infodecal", pos, texture=material)

    # ------------------------------------------------------------ finishing
    def finish(self):
        for name, solids in self.detail_groups.items():
            self.m.add_brush_entity("func_detail", solids)
        return self.m


class Space:
    def __init__(self, name, a, b, kind="in", floor=None, wall=None, wall_hi=None, ceil=None, band=None,
                 reveal=None, floor_z=None, soundscape=None, cubemap=True):
        self.name, self.a, self.b, self.kind = name, tuple(a), tuple(b), kind
        self.floor, self.wall, self.wall_hi, self.ceil = floor, wall, wall_hi, ceil
        self.band = band
        self.reveal = reveal
        self.floor_z = a[2] if floor_z is None else floor_z
        self.soundscape = soundscape
        self.cubemap = cubemap

    def contains(self, p):
        return all(self.a[i] + EPS < p[i] < self.b[i] - EPS for i in range(3))

    def intersects(self, a, b):
        return all(self.a[i] < b[i] and self.b[i] > a[i] for i in range(3))

    @property
    def center(self):
        return tuple((self.a[i] + self.b[i]) * 0.5 for i in range(3))


class Envelope:
    def __init__(self, name, a, b, side, top, bottom, zlines, lm):
        self.name, self.a, self.b = name, tuple(a), tuple(b)
        self.side = side if callable(side) else (lambda z0, z1, f, lo, hi, _m=side: (_m, None))
        self.top, self.bottom, self.zlines, self.lm = top, bottom, tuple(zlines), lm
        self.brushes = 0

    def contains(self, p):
        return all(self.a[i] - EPS < p[i] < self.b[i] + EPS for i in range(3))


# ---------------------------------------------------------------- rect utilities
def subtract_rects(area, holes):
    """area (x0,y0,x1,y1) minus holes -> list of rects (merged along x, then y)"""
    x0, y0, x1, y1 = area
    xs = {x0, x1}
    ys = {y0, y1}
    for h in holes:
        for v in (h[0], h[2]):
            if x0 < v < x1:
                xs.add(v)
        for v in (h[1], h[3]):
            if y0 < v < y1:
                ys.add(v)
    X, Y = sorted(xs), sorted(ys)
    free = {}
    for i in range(len(X) - 1):
        for j in range(len(Y) - 1):
            cx, cy = (X[i] + X[i + 1]) / 2, (Y[j] + Y[j + 1]) / 2
            free[(i, j)] = not any(h[0] < cx < h[2] and h[1] < cy < h[3] for h in holes)
    used = set()
    out = []
    for j in range(len(Y) - 1):
        for i in range(len(X) - 1):
            if not free[(i, j)] or (i, j) in used:
                continue
            i1 = i + 1
            while i1 < len(X) - 1 and free[(i1, j)] and (i1, j) not in used:
                i1 += 1
            j1 = j + 1
            while j1 < len(Y) - 1 and all(free[(ii, j1)] and (ii, j1) not in used for ii in range(i, i1)):
                j1 += 1
            for ii in range(i, i1):
                for jj in range(j, j1):
                    used.add((ii, jj))
            out.append((X[i], Y[j], X[i1], Y[j1]))
    return out
