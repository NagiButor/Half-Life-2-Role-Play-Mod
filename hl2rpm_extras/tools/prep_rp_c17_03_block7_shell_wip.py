#!/usr/bin/env python3
"""prep_rp_c17_03_block7.py - builds rp_c17_03_block7.vmf (sector C17-03 "Квартал 7" + C17-04 Old Town
street) from the BSPSource decompile of Valve's retail d1_trainstation_03.

    python prep_rp_c17_03_block7.py [--src <decompiled.vmf>] [--out <vmf>]

Re-runnable: it always starts again from the untouched decompile
(E:\\SourceModding\\tools\\decomp\\d1_trainstation_03_d.vmf) and overwrites the output VMF in mapsrc.
Do not edit rp_c17_03_block7.vmf in Hammer and then re-run this script - the edits would be lost;
put changes into this script instead.

What it does (stages below, in order):
  1. strip   - removes Gordon's story (the CP raid on the apartments, the door bracer, scripted cops,
               window cops, APC, autosave/teleport/fade/invulnerability, changelevels to Valve maps)
               and keeps the atmosphere (Breencast TVs, ambient citizens on actbusy, sounds, props).
  2. shell   - builds what Valve never built because the player only saw it through windows: the
               outside of house 7/3 (facades on the yard and the street, roof), the ground floor
               (entrance hall, mailboxes, kv. 2 and kv. 4), the 5th floor (kv. 17, kv. 20), the
               basement (boiler room, Tiago's club), facades of the neighbouring houses.
  3. oldtown - the Old Town street (C17-04): Lukich's watch shop with a back room, the bath house
               (door, changing room, steam room), the canal edge with the lock-keeper hut "Шлюз-3"
               and a bridge; barricades at both street ends.
  4. rpg     - spots (info_target spot_<npc>), door number plates, the hero's bed (prop_rpg_usable
               bed_room11), location triggers (trigger_rpg discover C17-03 / C17-04), the
               transition lm_02_03 back to rp_c17_02_plaza, yard furniture (ration dispenser, benches,
               washing lines, yard screen).
  5. nodes   - an info_node grid on every walkable floor that has no Valve node nearby.

The leaked Valve VMFs (E:\\SourceModding\\UsefulStuff\\HL2Leak-VMFs) were only looked at as a reference;
nothing is copied from them.
"""
import argparse
import math
import os
import re
import struct
import sys

sys.path.insert(0, r"E:\SourceModding\HL2RPMSOURCE\.claude\skills\hl2rpm-hammer\scripts")
from vmf import VMF, Node  # noqa: E402

SRC = r"E:\SourceModding\tools\decomp\d1_trainstation_03_d.vmf"
OUT = r"E:\XBLAH's modding tool\content\Mapbase\hl2rpm\mapsrc\rp_c17_03_block7.vmf"
REF = r"E:\SourceModding\UsefulStuff\Source sdk 2013 HL2 unpacked vpk"
GAMES = ("ep2", "episodic", "hl2")
MOD = r"E:\Steam\steamapps\sourcemods\hl2rpm"

NODRAW = "TOOLS/TOOLSNODRAW"
SKY = "TOOLS/TOOLSSKYBOX"
CLIP = "TOOLS/TOOLSPLAYERCLIP"
NPCCLIP = "TOOLS/TOOLSNPCCLIP"
TRIGGER = "TOOLS/TOOLSTRIGGER"
BLOCKLIGHT = "TOOLS/TOOLSBLOCKLIGHT"

LOG = []


def log(*a):
    s = " ".join(str(x) for x in a)
    LOG.append(s)
    print(s)


# =============================================================================================
# content checks (the stock content the mod mounts)
# =============================================================================================
_vmt_cache = {}


def read_vmt(rel):
    rel = rel.lower().replace("\\", "/")
    if rel in _vmt_cache:
        return _vmt_cache[rel]
    t = None
    for root in [os.path.join(REF, g) for g in GAMES] + [MOD]:
        p = os.path.join(root, "materials", rel + ".vmt")
        if os.path.exists(p):
            t = open(p, encoding="latin-1").read()
            break
    _vmt_cache[rel] = t
    return t


def mdl_path(rel):
    rel = rel.lower().replace("\\", "/")
    for root in [os.path.join(REF, g) for g in GAMES] + [MOD]:
        p = os.path.join(root, rel)
        if os.path.exists(p):
            return p
    return None


def mdl_static_ok(rel):
    """True if the model was compiled as a static prop ($staticprop flag)"""
    p = mdl_path(rel)
    if not p:
        return False
    with open(p, "rb") as f:
        h = f.read(160)
    if len(h) < 160 or h[:4] != b"IDST":
        return False
    flags = struct.unpack_from("<i", h, 152)[0]
    return bool(flags & 0x10)


MISSING = set()


def mat(name):
    if not name.upper().startswith("TOOLS/") and read_vmt(name) is None:
        MISSING.add("material " + name)
    return name


# =============================================================================================
# brush geometry
# =============================================================================================
PT = re.compile(r"\(\s*([-\d.eE+]+)\s+([-\d.eE+]+)\s+([-\d.eE+]+)\s*\)")


def sub(a, b): return (a[0] - b[0], a[1] - b[1], a[2] - b[2])
def add(a, b): return (a[0] + b[0], a[1] + b[1], a[2] + b[2])
def mul(a, s): return (a[0] * s, a[1] * s, a[2] * s)
def dot(a, b): return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]
def cross(a, b): return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])


def norm(a):
    L = math.sqrt(dot(a, a))
    return (a[0] / L, a[1] / L, a[2] / L) if L > 1e-12 else (0.0, 0.0, 0.0)


def side_plane(side):
    """(outward normal, d): the brush is the intersection of dot(n, p) <= d"""
    pts = [tuple(map(float, m_.groups())) for m_ in PT.finditer(side.get("plane", ""))]
    if len(pts) < 3:
        return None
    p0, p1, p2 = pts[:3]
    n = norm(cross(sub(p0, p1), sub(p2, p1)))
    if n == (0.0, 0.0, 0.0):
        return None
    return n, dot(n, p1)


def _base_winding(n, d, size=65536.0):
    ax = max(range(3), key=lambda i: abs(n[i]))
    up = (0, 0, 1) if ax != 2 else (1, 0, 0)
    t = dot(up, n)
    up = norm(sub(up, mul(n, t)))
    rt = cross(up, n)
    org = mul(n, d)
    up = mul(up, size)
    rt = mul(rt, size)
    return [add(sub(org, rt), up), add(add(org, rt), up), sub(add(org, rt), up), sub(sub(org, rt), up)]


def _clip_poly(poly, n, d, eps=0.01):
    out = []
    m_ = len(poly)
    if not m_:
        return out
    dists = [dot(n, p) - d for p in poly]
    for i in range(m_):
        p, q = poly[i], poly[(i + 1) % m_]
        dp, dq = dists[i], dists[(i + 1) % m_]
        if dp <= eps:
            out.append(p)
        if (dp > eps and dq < -eps) or (dp < -eps and dq > eps):
            t = dp / (dp - dq)
            out.append(add(p, mul(sub(q, p), t)))
    return out


def planes_polys(planes):
    polys = []
    for i, (n, d) in enumerate(planes):
        poly = _base_winding(n, d)
        for j, (nj, dj) in enumerate(planes):
            if j != i:
                poly = _clip_poly(poly, nj, dj)
                if not poly:
                    break
        polys.append(poly if len(poly) >= 3 else [])
    return polys


def solid_planes(solid):
    res = []
    for sd in solid.children_named("side"):
        pl = side_plane(sd)
        if pl:
            res.append(pl)
    return res


def solid_bbox(solid):
    pts = [p for poly in planes_polys(solid_planes(solid)) for p in poly]
    if not pts:
        return None
    return (tuple(min(p[k] for p in pts) for k in range(3)), tuple(max(p[k] for p in pts) for k in range(3)))


def solid_volume_ok(planes, eps=0.5):
    polys = planes_polys(planes)
    pts = [p for poly in polys for p in poly]
    if len([p for p in polys if p]) < 4 or not pts:
        return False
    lo = [min(p[k] for p in pts) for k in range(3)]
    hi = [max(p[k] for p in pts) for k in range(3)]
    return all(hi[k] - lo[k] > eps for k in range(3))


def solid_materials(solid):
    return [sd.get("material", "").upper() for sd in solid.children_named("side")]


def is_disp(solid):
    return any(sd.child("dispinfo") is not None for sd in solid.children_named("side"))


def world_axes(n, scale=0.25):
    ax = max(range(3), key=lambda i: abs(n[i]))
    if ax == 2:
        return "[1 0 0 0] %g" % scale, "[0 -1 0 0] %g" % scale
    if ax == 0:
        return "[0 1 0 0] %g" % scale, "[0 0 -1 0] %g" % scale
    return "[1 0 0 0] %g" % scale, "[0 0 -1 0] %g" % scale


def plane_points(n, d):
    """three points (Hammer order) of the plane dot(n,p)=d with outward normal n"""
    q = mul(n, d)
    ax = max(range(3), key=lambda i: abs(n[i]))
    a = (0, 0, 1) if ax != 2 else (1, 0, 0)
    t1 = norm(cross(a, n))
    t2 = cross(n, t1)          # cross(t1, t2) = n
    L = 64.0
    p1 = q
    p0 = add(q, mul(t1, L))
    p2 = add(q, mul(t2, L))
    # snap almost-integers (axis aligned planes stay exact)
    def fmt(p):
        return "(%s %s %s)" % tuple(("%d" % round(v)) if abs(v - round(v)) < 1e-6 else ("%.6f" % v) for v in p)
    return "%s %s %s" % (fmt(p0), fmt(p1), fmt(p2))


class Geo:
    """brush building on top of a VMF"""

    def __init__(self, m):
        self.m = m

    # ---------------------------------------------------------------- new brushes
    def box(self, mins, maxs, material, faces=None, entity=None, lightmapscale=16, scale=0.25, offsets=None):
        """axis aligned box; faces: dict top/bottom/north(+y)/south(-y)/east(+x)/west(-x) -> material.
        Returns the solid (added to the world, or to `entity` if given) or None for an empty box."""
        x1, y1, z1 = mins
        x2, y2, z2 = maxs
        if not (x2 - x1 > 0.01 and y2 - y1 > 0.01 and z2 - z1 > 0.01):
            return None
        faces = faces or {}
        defs = [
            ("top", (0, 0, 1), z2), ("bottom", (0, 0, -1), -z1),
            ("east", (1, 0, 0), x2), ("west", (-1, 0, 0), -x1),
            ("north", (0, 1, 0), y2), ("south", (0, -1, 0), -y1),
        ]
        s = Node("solid")
        s.add("id", self.m.new_id())
        for name, n, d in defs:
            s.add_child(self._side(n, d, mat(faces.get(name, material)), lightmapscale, scale,
                                   (offsets or {}).get(name)))
        s.add_child(Node("editor", [["color", "0 180 220"], ["visgroupshown", "1"], ["visgroupautoshown", "1"]]))
        if entity is None:
            self.m.world.add_child(s, self._world_solid_index())
        else:
            idx = len([c for c in entity.children if c.name.lower() == "solid"])
            entity.add_child(s, idx)
        return s

    def _world_solid_index(self):
        return len([c for c in self.m.world.children if c.name.lower() == "solid"])

    def _side(self, n, d, material, lightmapscale=16, scale=0.25, offset=None):
        sd = Node("side")
        sd.add("id", self.m.new_side_id())
        sd.add("plane", plane_points(n, d))
        sd.add("material", material)
        u, v = world_axes(n, scale)
        if offset:
            u = re.sub(r"\[([^\]]*) [-\d.]+\]", lambda mm: "[%s %g]" % (mm.group(1), offset[0]), u)
            v = re.sub(r"\[([^\]]*) [-\d.]+\]", lambda mm: "[%s %g]" % (mm.group(1), offset[1]), v)
        sd.add("uaxis", u)
        sd.add("vaxis", v)
        sd.add("rotation", "0")
        sd.add("lightmapscale", str(lightmapscale))
        sd.add("smoothing_groups", "0")
        return sd

    def detail(self, mins, maxs, material, faces=None, **kw):
        s_ent = self.m.add_brush_entity("func_detail", [])
        s = self.box(mins, maxs, material, faces, entity=s_ent, **kw)
        if s is None:
            self.m.delete(s_ent)
            return None
        return s_ent

    def brush_entity(self, classname, boxes, material=TRIGGER, **keys):
        e = self.m.add_brush_entity(classname, [], **keys)
        for b in boxes:
            if len(b) == 2:
                self.box(b[0], b[1], material, entity=e)
            else:
                self.box(b[0], b[1], b[2], faces=b[3] if len(b) > 3 else None, entity=e)
        return e

    # ---------------------------------------------------------------- cutting existing brushes
    def clip(self, solid, n, d, material=NODRAW):
        """new solid = solid with an extra side keeping dot(n,p) <= d; None if nothing is left"""
        planes = solid_planes(solid) + [(n, d)]
        if not solid_volume_ok(planes):
            return None
        s = Node("solid")
        s.add("id", self.m.new_id())
        for it in solid.items:
            if isinstance(it, Node):
                if it.name.lower() == "side":
                    c = _copy_node(it)
                    c.set("id", str(self.m.new_side_id()))
                    s.add_child(c)
                elif it.name.lower() == "editor":
                    s.add_child(_copy_node(it))
        s.add_child(self._side(n, d, mat(material)), len(s.children_named("side")))
        return s

    def carve(self, solid, mins, maxs, material=NODRAW):
        """replace `solid` in its owner by the pieces outside the box mins..maxs. Returns the list of
        pieces (empty if the solid was entirely inside). Displacements are left alone."""
        bb = solid_bbox(solid)
        if bb is None or is_disp(solid):
            return [solid]
        lo, hi = bb
        if any(hi[k] <= mins[k] + 0.01 or lo[k] >= maxs[k] - 0.01 for k in range(3)):
            return [solid]
        owner = solid.parent
        pieces = []
        cur = solid
        for k in range(3):
            for sign in (-1, 1):
                n = [0.0, 0.0, 0.0]
                n[k] = float(sign)
                if sign < 0:
                    # outside part: x <= mins  ->  n=(+1) d=mins ; remaining: x >= mins -> n=(-1) d=-mins
                    outside = self.clip(cur, tuple(-v for v in n), -mins[k] * 1.0, material)
                    inside = self.clip(cur, tuple(n), -mins[k], material)
                else:
                    outside = self.clip(cur, tuple(-v for v in n), -maxs[k], material)
                    inside = self.clip(cur, tuple(n), maxs[k], material)
                if outside is not None:
                    pieces.append(outside)
                if inside is None:
                    cur = None
                    break
                cur = inside
            if cur is None:
                break
        # insert the pieces where the solid was
        idx = owner.items.index(solid)
        owner.items[idx:idx + 1] = pieces
        for p in pieces:
            p.parent = owner
        solid.parent = None
        return pieces

    def carve_world(self, mins, maxs, pred=None, material=NODRAW, entities=False):
        """carve every world solid (and, with entities=True, func_detail solids) that matches pred"""
        owners = [self.m.world]
        if entities:
            owners += [e for e in self.m.entities("func_detail")]
        n = 0
        for owner in owners:
            for s in list(owner.children_named("solid")):
                if pred is not None and not pred(s):
                    continue
                bb = solid_bbox(s)
                if bb is None:
                    continue
                lo, hi = bb
                if any(hi[k] <= mins[k] + 0.01 or lo[k] >= maxs[k] - 0.01 for k in range(3)):
                    continue
                self.carve(s, mins, maxs, material)
                n += 1
            if owner is not self.m.world and not owner.children_named("solid"):
                self.m.delete(owner)
        return n


def _copy_node(node):
    c = Node(node.name)
    for it in node.items:
        if isinstance(it, Node):
            c.add_child(_copy_node(it))
        else:
            c.items.append([it[0], it[1]])
    return c


def only_sky(s):
    return all(mm == SKY for mm in solid_materials(s))


def only_tools(s):
    return all(mm.startswith("TOOLS/") for mm in solid_materials(s))
