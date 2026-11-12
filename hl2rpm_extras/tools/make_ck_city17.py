"""ck_test as a City 17 courtyard ("Block 7 yard"): apartment blocks around the yard.

    python make_ck_city17.py [path\\to\\ck_test.vmf]

Edits the existing ck_test.vmf (vmf.py keeps a .bak first). Everything it makes is in the
visgroup "ck_city17" and is replaced on the next run; what the user or the Creation Kit put
in the yard is not touched. Nothing is built inside the yard (x, y within +-1536): the NPCs,
markers, items, patrol paths and the 569 nav nodes of the Creation Kit stay where they are.
The old sky walls at +-1536 go (the blocks close the yard), a new sky shell at +-2048.

The facades are the stock City 17 building textures (materials/building_template, as the
HL2 squares use them: scale 0.25, a 512-px tile = one 128-unit storey), banded per storey,
each band its own brush aligned to the storey; plinths and cornices stick out of the wall.
Doors with a canopy and a lamp over them, street lamps, a Combine post in the south-west
corner (the "Overwatch post 17" zone) and a burnt-out block at the wasteland (north-east).
The lamps are classic light entities: the deferred renderer converts them.
Compile: compile_map.ps1 -Map ck_test
"""
import os
import sys

sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", "AI_instructions", "skills", "hl2rpm-hammer", "scripts")))
from vmf import VMF, Node, parse  # noqa: E402

VMF_PATH = r"E:\XBLAH's modding tool\content\Mapbase\hl2rpm\mapsrc\test\ck_test.vmf"
VISGROUP = "ck_city17"

B = 1536        # half the yard
O = 2048        # the outer edge (sky shell inside)
SKY_TOP = 1536
BT = "BUILDING_TEMPLATE/BUILDING_TEMPLATE"
ROOF = "BUILDING_TEMPLATE/ROOF_TEMPLATE001B"
LEDGE = "CONCRETE/CONCRETEFLOOR028C"
NODRAW = "TOOLS/TOOLSNODRAW"
SKY = "TOOLS/TOOLSSKYBOX"
FLOOR = "DEV/DEV_MEASUREGENERIC01B"

# style: plinth (material, tex px height), ground floor, door, upper floors (cycled), side wall,
# cornice (material, px height)
STYLES = {
    "yellow": dict(plinth=(BT + "014F", 256), ground=BT + "012G", door=BT + "014I", floors=[BT + "012C", BT + "012D", BT + "012B"],
                   plain="BUILDING_TEMPLATE/COURTYARD_TEMPLATE002B", cornice=(BT + "002K", 128), balcony=True),
    "grey": dict(plinth=(BT + "028A", 512), ground=BT + "002C", door=BT + "002E", floors=[BT + "003B", BT + "002I", BT + "002C"],
                 plain=BT + "002A", cornice=(BT + "002K", 128), balcony=False),
    "brick": dict(plinth=(BT + "028A", 512), ground=BT + "011B", door=BT + "002E", floors=[BT + "011D"],
                  plain=BT + "011B", cornice=(BT + "011I", 128), balcony=False),
    "orange": dict(plinth=(BT + "028A", 512), ground=BT + "011M", door=BT + "014I", floors=[BT + "011G", BT + "011M"],
                   plain=BT + "011M", cornice=(BT + "011H", 128), balcony=False),
    "ruin": dict(plinth=(BT + "028A", 512), ground=BT + "003D", door=None, floors=[BT + "002C"],
                 plain=BT + "003A", cornice=None, balcony=False),
}

# Each side: the buildings along it (from, to along the side's axis), style, storeys, setback
# (how far the facade stands back from the yard edge), the doors (start of a 128-wide bay).
# S/N run along x, W/E along y. Corners belong to S and N.
SIDES = {
    "S": [(-2048, -896, "combine", 3, 0, []), (-896, -128, "yellow", 4, 0, [-576]), (-128, 640, "grey", 5, 32, [128]),
          (640, 2048, "brick", 4, 0, [1024])],
    "W": [(-1536, -896, "combine", 3, 0, []), (-896, 128, "orange", 3, 0, [-384]), (128, 1536, "yellow", 5, 32, [640])],
    "N": [(-2048, -640, "grey", 4, 0, [-1280]), (-640, 384, "brick", 5, 0, [-256]), (384, 1000, "yellow", 4, 32, [640]),
          (1000, 2048, "ruin", 2, 0, [])],
    "E": [(-1536, -512, "grey", 4, 0, [-1024]), (-512, -128, "yellow", 3, 0, []), (128, 900, "orange", 4, 0, [384]),
          (900, 1536, "ruin", 2, 0, [])],
}
ALLEY = (-128, 128)     # a dead-end alley in the east side (y range)


class Gen:
    def __init__(self, m):
        self.m = m
        self.vis = self._visgroup()
        self.lamps = []

    # -- the VMF -------------------------------------------------------------
    def _visgroup(self):
        vg = self.m.root.child("visgroups")
        if vg is None:
            vg = Node("visgroups")
            self.m.root.add_child(vg, 1)
        for g in vg.children_named("visgroup"):
            if g.get("name") == VISGROUP:
                return g.get("visgroupid")
        ids = [int(g.get("visgroupid")) for g in vg.children_named("visgroup")] or [0]
        vid = str(max(ids + [100]) + 1)
        g = Node("visgroup")
        g.add("name", VISGROUP)
        g.add("visgroupid", vid)
        g.add("color", "220 180 60")
        vg.add_child(g)
        return vid

    def _tag(self, node):
        ed = node.child("editor")
        if ed is None:
            ed = Node("editor", [["color", "220 180 60"], ["visgroupshown", "1"], ["visgroupautoshown", "1"]])
            node.add_child(ed)
        ed.delete("visgroupid")
        ed.add("visgroupid", self.vis)

    def clear_old(self):
        """Remove what an earlier run made (the visgroup) and the original sky walls at +-1536."""
        n = 0
        for owner in [self.m.world] + list(self.m.entities()):
            for s in list(owner.children_named("solid")):
                ed = s.child("editor")
                if ed is not None and ed.get("visgroupid") == self.vis:
                    owner.remove_child(s)
                    n += 1
        for e in list(self.m.entities()):
            ed = e.child("editor")
            if ed is not None and ed.get("visgroupid") == self.vis:
                self.m.delete(e)
                n += 1
        # the sky walls of make_ck_test.py: toolsskybox boxes on the yard edge
        for s in list(self.m.world.children_named("solid")):
            mats = {side.get("material") for side in s.children_named("side")}
            if mats == {SKY}:
                self.m.world.remove_child(s)
                n += 1
        print("removed", n, "old pieces")

    def box(self, mins, maxs, faces, default=NODRAW, owner=None):
        """faces: name -> material or (material, uaxis, vaxis). Names: top bottom left(-x) right(+x)
        back(+y) front(-y)."""
        s = self.m.make_box(mins, maxs, default)
        names = ("top", "bottom", "left", "right", "back", "front")
        for side, name in zip(s.children_named("side"), names):
            f = faces.get(name)
            if f is None:
                continue
            if isinstance(f, tuple):
                side.set("material", f[0])
                side.set("uaxis", f[1])
                side.set("vaxis", f[2])
            else:
                side.set("material", f)
        self._tag(s)
        owner = owner if owner is not None else self.m.world
        idx = len(owner.children)
        for i, c in enumerate(owner.children):
            if c.name.lower() in ("editor", "hidden", "group"):
                idx = i
                break
        owner.add_child(s, idx)
        return s

    def entity(self, classname, origin, **keys):
        e = self.m.add_entity(classname, origin, **keys)
        self._tag(e)
        return e

    def detail(self, boxes):
        """A func_detail of (mins, maxs, faces) boxes (ledges, canopies - not part of the hull)."""
        e = self.m.add_brush_entity("func_detail", [])
        self._tag(e)
        for mins, maxs, faces in boxes:
            self.box(mins, maxs, faces, owner=e)
        return e

    # -- facades -------------------------------------------------------------
    @staticmethod
    def facade_face(side):
        return {"S": "back", "N": "front", "W": "right", "E": "left"}[side]

    @staticmethod
    def uv(side, a0, a1, ztop, tex_h=512):
        """The facade's texture axes: u to the viewer's right from the building's left edge, one
        tile per storey down from ztop."""
        if side == "S":
            u = "[-1 0 0 %d] 0.25" % ((4 * a1) % 512)
        elif side == "N":
            u = "[1 0 0 %d] 0.25" % ((-4 * a0) % 512)
        elif side == "W":
            u = "[0 1 0 %d] 0.25" % ((-4 * a0) % 512)
        else:
            u = "[0 -1 0 %d] 0.25" % ((4 * a1) % 512)
        v = "[0 0 -1 %d] 0.25" % ((4 * ztop) % tex_h)
        return u, v

    @staticmethod
    def slab(side, a0, a1, d, z0, z1):
        """The box of a band: along a0..a1, from the facade at distance d from the centre out to O."""
        if side == "S":
            return (a0, -O, z0), (a1, -d, z1)
        if side == "N":
            return (a0, d, z0), (a1, O, z1)
        if side == "W":
            return (-O, a0, z0), (-d, a1, z1)
        return (d, a0, z0), (O, a1, z1)

    def out_box(self, side, a0, a1, dn, df, z0, z1):
        """A box along a0..a1 between the distances dn < df from the centre (a ledge on a facade at df)."""
        p0 = self.out_point(side, a0, dn, z0)
        p1 = self.out_point(side, a1, df, z1)
        return tuple(min(p0[i], p1[i]) for i in range(3)), tuple(max(p0[i], p1[i]) for i in range(3))

    @staticmethod
    def out_point(side, a, d, z):
        """A point at along-axis a, distance d from the centre (d < B: in the yard), height z."""
        if side == "S":
            return (a, -d, z)
        if side == "N":
            return (a, d, z)
        if side == "W":
            return (-d, a, z)
        return (d, a, z)

    @staticmethod
    def yaw_in(side):
        """The yaw that looks from the facade into the yard."""
        return {"S": 90, "N": 270, "W": 0, "E": 180}[side]

    def band(self, side, a0, a1, d, z0, z1, mat, tex_h, top=None, plain=None, bottom=None):
        mins, maxs = self.slab(side, a0, a1, d, z0, z1)
        u, v = self.uv(side, a0, a1, z1, tex_h)
        faces = {n: (plain or mat) for n in ("left", "right", "back", "front")}
        faces[self.facade_face(side)] = (mat, u, v)
        if top:
            faces["top"] = top
        if bottom:
            faces["bottom"] = bottom
        return self.box(mins, maxs, faces)

    def building(self, side, a0, a1, style, storeys, setback, doors):
        if style == "combine":
            return self.combine_block(side, a0, a1)
        st = STYLES[style]
        d = B + setback
        top = 192 + 128 * (storeys - 1)
        plain = st["plain"]
        # the hull: plinth, the ground floor (doors as their own bays), the storeys
        pm, ph = st["plinth"]
        self.band(side, a0, a1, d, 0, 64, pm, ph, plain=plain)
        bays = []
        a = a0
        while a < a1:
            b = min(a + 128, a1)
            bays.append((a, b))
            a = b
        run = None
        for (b0, b1) in bays:
            is_door = st["door"] and b0 in doors
            mat = st["door"] if is_door else st["ground"]
            if run and run[2] == mat and not is_door:
                run[1] = b1
            else:
                if run:
                    self.band(side, run[0], run[1], d, 64, 192, run[2], 512, plain=plain)
                run = [b0, b1, mat]
            if is_door:
                self.band(side, run[0], run[1], d, 64, 192, run[2], 512, plain=plain)
                run = None
                self.door_dressing(side, b0, b1, d)
        if run:
            self.band(side, run[0], run[1], d, 64, 192, run[2], 512, plain=plain)
        for k in range(storeys - 1):
            z0 = 192 + 128 * k
            mat = st["floors"][k % len(st["floors"])]
            is_top = k == storeys - 2
            self.band(side, a0, a1, d, z0, z0 + 128, mat, 512, plain=plain, top=(ROOF if is_top and not st["cornice"] else None))
        # out of the wall: the plinth's ledge, a string course over the ground floor, the cornice
        all6 = ("top", "bottom", "left", "right", "back", "front")
        parts = []
        if setback > 0:
            # (not into the yard: the Creation Kit's nav nodes run 20 units off its edge)
            mins, maxs = self.out_box(side, a0, a1, d - 8, d, 0, 64)
            parts.append((mins, maxs, self._ledge_faces(side, a0, a1, pm, ph, 64)))
        mins, maxs = self.out_box(side, a0, a1, d - 6, d, 192, 200)
        parts.append((mins, maxs, {n: LEDGE for n in all6}))
        if st["cornice"]:
            cm, chh = st["cornice"]
            mins, maxs = self.out_box(side, a0, a1, d - 16, d + 64, top, top + 32)
            faces = self._ledge_faces(side, a0, a1, cm, chh, top + 32)
            faces["top"] = ROOF
            parts.append((mins, maxs, faces))
        else:
            # a burnt-out top: the storey above is gone, a jagged parapet
            for i, (b0, b1) in enumerate(bays):
                h = (16, 48, 8, 72, 24, 40, 0, 56)[i % 8]
                if h:
                    mins, maxs = self.out_box(side, b0, b1, d, d + 24, top, top + h)
                    parts.append((mins, maxs, {n: plain for n in all6}))
        if st["balcony"] and storeys >= 3:
            for i, (b0, b1) in enumerate(bays):
                if i % 3 != 1 or b1 - b0 < 128 or b0 in doors:
                    continue
                for k in range(1, storeys - 1):
                    zf = 192 + 128 * k - 8
                    mins, maxs = self.out_box(side, b0 + 8, b1 - 8, d - 40, d, zf, zf + 8)
                    parts.append((mins, maxs, {n: LEDGE for n in all6}))
                    # the parapet: a low wall on the outer edge
                    mins, maxs = self.out_box(side, b0 + 8, b1 - 8, d - 40, d - 36, zf + 8, zf + 40)
                    parts.append((mins, maxs, {n: plain for n in all6}))
        self.detail(parts)
        # a chimney here and there on the roof
        roof_z = top + (32 if st["cornice"] else 0)
        for i, (b0, b1) in enumerate(bays):
            if i % 4 == 2 and style != "ruin":
                mins, maxs = self.out_box(side, b0 + 40, b0 + 72, d + 96, d + 128, roof_z, roof_z + 72)
                self.box(mins, maxs, {n: BT + "011B" for n in ("top", "left", "right", "back", "front")})

    def _ledge_faces(self, side, a0, a1, mat, tex_h, ztop):
        u, v = self.uv(side, a0, a1, ztop, tex_h)
        f = {n: LEDGE for n in ("top", "bottom", "left", "right", "back", "front")}
        f[self.facade_face(side)] = (mat, u, v)
        return f

    def _parapet(self, side, a0, a1, d, z0, z1, mat):
        """A balcony's low wall: 4 thick, 40 out of the facade."""
        p0 = self.out_point(side, a0, d - 40, z0)
        p1 = self.out_point(side, a1, d - 36, z1)
        mins = tuple(min(p0[i], p1[i]) for i in range(3))
        maxs = tuple(max(p0[i], p1[i]) for i in range(3))
        return mins, maxs, {n: mat for n in ("top", "bottom", "left", "right", "back", "front")}

    def door_dressing(self, side, b0, b1, d):
        """A concrete canopy over the door and a lamp under it."""
        mid = (b0 + b1) / 2.0
        mins, maxs = self.out_box(side, b0 + 16, b1 - 16, d - 40, d, 180, 188)
        self.detail([(mins, maxs, {n: LEDGE for n in ("top", "bottom", "left", "right", "back", "front")})])
        self.entity("prop_static", self.out_point(side, mid, d - 22, 180), model="models/props_c17/light_domelight02_on.mdl",
                    angles="0 %d 0" % self.yaw_in(side), solid="0", disableshadows="1")
        # (under 0.8 at 100 units: the conversion gives it no cube shadow - a dozen of these did)
        self.light(self.out_point(side, mid, d - 22, 168), "255 214 160 190")

    def light(self, pos, color, **keys):
        self.entity("light", pos, _light=color, _lightHDR="-1 -1 -1 1", _lightscaleHDR="1",
                    _constant_attn="0", _linear_attn="0", _quadratic_attn="1", spawnflags="0", **keys)

    def combine_block(self, side, a0, a1):
        """The Overwatch post's backdrop: a dark concrete wall with a band of Combine metal."""
        d = B
        self.band(side, a0, a1, d, 0, 64, BT + "028A", 512, plain=BT + "021A")
        self.band(side, a0, a1, d, 64, 320, "METAL/METALCOMBINE001", 512, plain=BT + "021A")
        self.band(side, a0, a1, d, 320, 448, BT + "021A", 512, plain=BT + "021A", top=ROOF)
        mins, maxs = self.slab(side, a0, a1, d - 12, 440, 456)
        self.detail([(mins, maxs, {n: "METAL/METALCOMBINE001" for n in ("top", "bottom", "left", "right", "back", "front")})])

    # -- the rest ------------------------------------------------------------
    def shell(self):
        t = 16
        self.box((-O - t, -O - t, -32), (O + t, O + t, -16), {}, default=NODRAW)
        self.box((-O - t, -O - t, SKY_TOP), (O + t, O + t, SKY_TOP + t), {}, default=SKY)
        self.box((-O - t, -O - t, -16), (-O, O + t, SKY_TOP), {}, default=SKY)
        self.box((O, -O - t, -16), (O + t, O + t, SKY_TOP), {}, default=SKY)
        self.box((-O, -O - t, -16), (O, -O, SKY_TOP), {}, default=SKY)
        self.box((-O, O, -16), (O, O + t, SKY_TOP), {}, default=SKY)
        # the floor between the yard and the blocks (under the blocks, in the alley, behind setbacks)
        f = {"top": FLOOR}
        self.box((-O, -O, -16), (O, -B, 0), f)
        self.box((-O, B, -16), (O, O, 0), f)
        self.box((-O, -B, -16), (-B, B, 0), f)
        self.box((B, -B, -16), (O, B, 0), f)

    def alley(self):
        """The dead end between the east blocks: a gate halfway, rubbish in front of it."""
        y0, y1 = ALLEY
        self.entity("prop_static", (1792, 0, 64), model="models/props_c17/gate_door02a.mdl", angles="0 0 0")
        # the gate's frame: two posts and a lintel
        self.detail([((1788, y0, 0), (1796, y0 + 64 - 64 + 8, 136), {n: "METAL/METALGATE004A" for n in ("top", "bottom", "left", "right", "back", "front")}),
                     ((1788, y1 - 8, 0), (1796, y1, 136), {n: "METAL/METALGATE004A" for n in ("top", "bottom", "left", "right", "back", "front")})])
        # (the gate model is 128 wide: the rest of the width is a wall)
        self.box((1788, y0 + 8, 0), (1796, -64, 136), {n: BT + "003A" for n in ("top", "left", "right", "back", "front")})
        self.box((1788, 64, 0), (1796, y1 - 8, 136), {n: BT + "003A" for n in ("top", "left", "right", "back", "front")})
        # the far end: the wall of the next block (not the sky at the ground)
        self.box((2000, y0, 0), (O, y1, 704), {"left": BT + "003D", "top": ROOF})
        self.entity("prop_static", (1700, -90, 26), model="models/props_junk/trashdumpster01a.mdl", angles="0 90 0")
        self.entity("prop_static", (1650, 95, 0), model="models/props_c17/oildrum001.mdl", angles="0 30 0")
        self.light((1600, 0, 150), "255 200 140 90")

    def street_lamps(self):
        # (x, y, yaw): the arm (model +y) toward the yard; away from the patrol paths and props
        import math
        # (the model's arm points along its +y; yaw turns +y to (-sin, cos))
        # (clear of the Creation Kit's nav nodes; the lamps stand 450 high - the light at the head is
        # bright, its pool falls off as an engine light does)
        for x, y, yaw in ((1460, -760, 90), (1460, 640, 90), (-1500, -440, 270), (-1500, 880, 270),
                          (-620, 1460, 180), (800, 1500, 180), (300, -1460, 0), (1300, -1460, 0)):
            self.entity("prop_static", (x, y, 0), model="models/props_c17/lamppost03a_on.mdl", angles="0 %d 0" % yaw)
            r = math.radians(yaw)
            hx, hy = -96 * math.sin(r), 96 * math.cos(r)
            # a lamp shines down (a spot: its shadow map is cached while it stands still; a point
            # light this bright would get a cube of 6 shadow views)
            self.entity("light_spot", (x + hx, y + hy, 418), angles="-90 0 0", pitch="-90", _light="255 205 140 1600",
                        _lightHDR="-1 -1 -1 1", _lightscaleHDR="1", _inner_cone="50", _cone="75", _exponent="1",
                        _constant_attn="0", _linear_attn="0", _quadratic_attn="1")

    def combine_post(self):
        """Props of the Overwatch post (south-west corner) - the CK places the terminal, the turret,
        the barricade and the cache; this adds the hardware on the walls."""
        self.entity("prop_static", (-1536 + 6, -1290, 300), model="models/props_combine/combine_light001a.mdl", angles="0 0 0")
        self.entity("prop_static", (-1180, -1536 + 6, 300), model="models/props_combine/combine_light001a.mdl", angles="0 90 0")
        self.entity("light_spot", (-1500, -1290, 330), angles="-60 30 0", pitch="-60", _light="170 210 255 2000",
                    _lightHDR="-1 -1 -1 1", _lightscaleHDR="1", _inner_cone="30", _cone="55", _exponent="1",
                    _constant_attn="0", _linear_attn="0", _quadratic_attn="1")
        self.entity("light_spot", (-1180, -1500, 330), angles="-60 60 0", pitch="-60", _light="170 210 255 2000",
                    _lightHDR="-1 -1 -1 1", _lightscaleHDR="1", _inner_cone="30", _cone="55", _exponent="1",
                    _constant_attn="0", _linear_attn="0", _quadratic_attn="1")
        self.entity("prop_static", (-1536 + 4, -1040, 360), model="models/props_combine/combinecamera001.mdl", angles="0 0 0")
        # (the south wall has nav nodes all along it: only the west one)
        self.entity("prop_static", (-1500, -1300, 30), model="models/props_combine/combine_barricade_tall01a.mdl", angles="0 90 0")

    def house(self):
        """The caretaker's house (make_ck_test.py): the outside of its walls in the stone of the
        blocks, a canopy and a lamp over the door. Its inside (plaster, wood floor) stays."""
        stone = BT + "003D"     # concrete with windows over a stone base: one tile = the wall
        plain = BT + "003A"
        u_x = "[1 0 0 0] 0.25"
        u_y = "[0 1 0 0] 0.25"
        v = "[0 0 -1 0] 0.3125"     # 512 px = the 160 units of the wall
        want = {
            ((368, -336, 0), (384, -56, 160)): ("left", plain, u_y),
            ((368, 56, 0), (384, 336, 160)): ("left", plain, u_y),
            ((368, -56, 112), (384, 56, 160)): ("left", plain, u_y),
            ((1024, -336, 0), (1040, 336, 160)): ("right", stone, u_y),
            ((384, -336, 0), (1024, -320, 160)): ("front", stone, u_x),
            ((384, 320, 0), (1024, 336, 160)): ("back", stone, u_x),
        }
        import re
        names = ("top", "bottom", "left", "right", "back", "front")
        n = 0
        for s in self.m.world.children_named("solid"):
            pts, sides = [], list(s.children_named("side"))
            per_side = []
            for side in sides:
                nums = list(map(float, re.findall(r"-?[\d.]+", side.get("plane"))))
                p3 = [nums[i:i + 3] for i in range(0, 9, 3)]
                per_side.append(p3)
                pts += p3
            if len(sides) != 6:
                continue
            lo = tuple(int(round(min(p[i] for p in pts))) for i in range(3))
            hi = tuple(int(round(max(p[i] for p in pts))) for i in range(3))

            def face_name(p3):
                # the axis the three points share, and on which end of the box
                for ax, (a, b) in enumerate(((("left", "right")), ("front", "back"), ("bottom", "top"))):
                    vals = {round(p[ax]) for p in p3}
                    if len(vals) == 1:
                        return a if vals.pop() == lo[ax] else b
                return None
            named = {face_name(p3): side for p3, side in zip(per_side, sides)}
            key = (lo, hi)
            if key in want:
                face, mat, u = want[key]
                side = named.get(face)
                if side is None:
                    continue
                side.set("material", mat)
                side.set("uaxis", u)
                side.set("vaxis", v)
                n += 1
            elif key == ((368, -336, 160), (1040, 336, 176)):
                for nm, side in named.items():
                    if nm in ("left", "right", "back", "front"):
                        side.set("material", BT + "002K")
                        side.set("uaxis", u_x if nm in ("back", "front") else u_y)
                        side.set("vaxis", "[0 0 -1 0] 0.125")
                    elif nm == "top":
                        side.set("material", ROOF)
                n += 1
        print("house faces retextured:", n)
        self.detail([((328, -72, 116), (368, 72, 124), {n: LEDGE for n in names})])
        self.entity("prop_static", (346, 0, 116), model="models/props_c17/light_domelight02_on.mdl", angles="0 180 0",
                    solid="0", disableshadows="1")
        self.light((346, 0, 104), "255 214 160 160")

    def wasteland(self):
        """Rubble at the burnt-out block (north-east, the "Wasteland" zone)."""
        for pos, mdl, yaw in (((1410, 1430, 12), "models/props_debris/concrete_debris256pile001a.mdl", 20),
                              ((1470, 1080, 8), "models/props_debris/concrete_debris128pile001a.mdl", 70),
                              ((1160, 1480, 20), "models/props_debris/concrete_wallpile01a.mdl", 0),
                              ((1495, 1495, 16), "models/props_debris/concrete_cornerpile01a.mdl", 0),
                              ((1060, 1490, 20), "models/props_junk/trashbin01a.mdl", 10)):
            self.entity("prop_static", pos, model=mdl, angles="0 %d 0" % yaw)


def main(argv):
    path = argv[1] if len(argv) > 1 else VMF_PATH
    m = VMF.load(path)
    g = Gen(m)
    g.clear_old()
    g.shell()
    for side, blds in SIDES.items():
        for (a0, a1, style, storeys, setback, doors) in blds:
            if side == "E" and a0 < ALLEY[1] and a1 > ALLEY[0]:
                continue
            g.building(side, a0, a1, style, storeys, setback, doors)
    g.alley()
    g.street_lamps()
    g.combine_post()
    g.wasteland()
    g.house()
    m.save()
    print("saved", path)


if __name__ == "__main__":
    main(sys.argv)
