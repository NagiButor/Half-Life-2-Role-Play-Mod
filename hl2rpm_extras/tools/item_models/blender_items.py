# -*- coding: utf-8 -*-
# HL2RPM: small item models, built by code in Blender (see make_item_models.py, which runs this).
#
#   blender --background --factory-startup --python blender_items.py -- <model> <work dir> [render] [bake]
#
# Everything is modelled in centimetres, Z up, lying as it lies on a table; the SMD is written
# with the scale of make_item_models.py. Materials are procedural (worn edges from the Bevel node,
# dirt from AO, scratches from stretched noise); "bake" turns them into a base color, a roughness
# and a tangent space normal map in one atlas.
import math
import os
import sys

import bmesh
import bpy
from mathutils import Matrix, Vector

CM_TO_UNIT = 0.512      # 1 unit = 2.54 cm, things this small are 1.3 times larger: they have to be found


# ---------------------------------------------------------------------------- helpers: shapes
def link(obj):
    bpy.context.scene.collection.objects.link(obj)
    return obj


def activate(obj):
    bpy.ops.object.select_all(action="DESELECT")
    obj.select_set(True)
    bpy.context.view_layer.objects.active = obj


def apply_modifiers(obj):
    dg = bpy.context.evaluated_depsgraph_get()
    me = bpy.data.meshes.new_from_object(obj.evaluated_get(dg))
    obj.modifiers.clear()
    old = obj.data
    obj.data = me
    if old.users == 0:
        bpy.data.meshes.remove(old)


def weld(obj, dist=1e-4):
    bm = bmesh.new()
    bm.from_mesh(obj.data)
    bmesh.ops.remove_doubles(bm, verts=bm.verts, dist=dist)
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    bm.to_mesh(obj.data)
    bm.free()


def arc(cx, cy, r, a0, a1, n):
    """n + 1 points, angles in degrees"""
    return [(cx + r * math.cos(math.radians(a0 + (a1 - a0) * i / n)),
             cy + r * math.sin(math.radians(a0 + (a1 - a0) * i / n))) for i in range(n + 1)]


def extrude2d(name, outlines, thickness):
    """closed polylines in XY (the inner ones are holes) -> a solid centred on z = 0"""
    cu = bpy.data.curves.new(name, "CURVE")
    cu.dimensions = "2D"
    cu.fill_mode = "BOTH"
    cu.extrude = thickness * 0.5
    for o in outlines:
        sp = cu.splines.new("POLY")
        sp.points.add(len(o) - 1)
        for p, (x, y) in zip(sp.points, o):
            p.co = (x, y, 0, 1)
        sp.use_cyclic_u = True
    tmp = link(bpy.data.objects.new(name + "_c", cu))
    bpy.context.view_layer.update()
    dg = bpy.context.evaluated_depsgraph_get()
    me = bpy.data.meshes.new_from_object(tmp.evaluated_get(dg))
    bpy.data.objects.remove(tmp)
    bpy.data.curves.remove(cu)
    obj = link(bpy.data.objects.new(name, me))
    weld(obj)
    return obj


def box(name, lo, hi):
    me = bpy.data.meshes.new(name)
    x0, y0, z0 = lo
    x1, y1, z1 = hi
    v = [(x0, y0, z0), (x1, y0, z0), (x1, y1, z0), (x0, y1, z0), (x0, y0, z1), (x1, y0, z1), (x1, y1, z1), (x0, y1, z1)]
    f = [(3, 2, 1, 0), (4, 5, 6, 7), (0, 1, 5, 4), (1, 2, 6, 5), (2, 3, 7, 6), (3, 0, 4, 7)]
    me.from_pydata(v, [], f)
    me.update()
    return link(bpy.data.objects.new(name, me))


def cut(obj, cutter):
    m = obj.modifiers.new("cut", "BOOLEAN")
    m.operation = "DIFFERENCE"
    m.solver = "EXACT"
    m.object = cutter
    bpy.context.view_layer.update()
    apply_modifiers(obj)
    me = cutter.data
    bpy.data.objects.remove(cutter)
    bpy.data.meshes.remove(me)


def bevel(obj, width, segments=2, angle=28.0):
    m = obj.modifiers.new("bevel", "BEVEL")
    m.limit_method = "ANGLE"
    m.angle_limit = math.radians(angle)
    m.width = width
    m.segments = segments
    m.profile = 0.6
    m.use_clamp_overlap = True
    m.miter_outer = "MITER_ARC"
    bpy.context.view_layer.update()
    apply_modifiers(obj)


def sweep(name, path, profile, closed=False, smooth_cap=True, uv=None, normal0=None):
    """a closed profile [(a, b)] along a path of Vectors; the frame is carried along by parallel
    transport; uv = (u0, v0, u1, v1): u along the path, v around the profile"""
    n = len(path)
    m = len(profile)
    tang = []
    for i in range(n):
        a = path[(i - 1) % n] if closed or i > 0 else path[i]
        b = path[(i + 1) % n] if closed or i < n - 1 else path[i]
        tang.append((b - a).normalized())
    side = normal0 if normal0 is not None else tang[0].cross(Vector((0, 0, 1)))
    if side.length < 1e-6:
        side = tang[0].cross(Vector((0, 1, 0)))
    side = (side - tang[0] * side.dot(tang[0])).normalized()
    verts = []
    frames = []
    for i in range(n):
        if i:
            side = side - tang[i] * side.dot(tang[i])
            side.normalize()
        up = tang[i].cross(side).normalized()
        frames.append((side.copy(), up))
        for (a, b) in profile:
            verts.append(path[i] + side * a + up * b)
    faces = []
    last = n if closed else n - 1
    for i in range(last):
        i2 = (i + 1) % n
        for j in range(m):
            j2 = (j + 1) % m
            faces.append((i * m + j, i * m + j2, i2 * m + j2, i2 * m + j))
    if not closed:
        faces.append(tuple(reversed(range(m))))
        faces.append(tuple((n - 1) * m + j for j in range(m)))
    me = bpy.data.meshes.new(name)
    me.from_pydata([tuple(v) for v in verts], [], faces)
    me.update()
    if uv:
        lay = me.uv_layers.new(name="UVMap")
        lens = [0.0]
        for i in range(1, n + (1 if closed else 0)):
            lens.append(lens[-1] + (path[i % n] - path[i - 1]).length)
        total = lens[-1] or 1.0
        u0, v0, u1, v1 = uv
        for p in me.polygons:
            if len(p.vertices) == m and not closed and (p.index >= len(faces) - 2):
                for li in p.loop_indices:      # the caps: a corner of the strip
                    lay.uv[li].vector = (u0, v0)
                continue
            i0, j0 = p.index // m, p.index % m
            co = [(i0, j0), (i0, j0 + 1), (i0 + 1, j0 + 1), (i0 + 1, j0)]
            for li, (a, b) in zip(p.loop_indices, co):
                lay.uv[li].vector = (u0 + (u1 - u0) * lens[a] / total, v0 + (v1 - v0) * b / m)
    obj = link(bpy.data.objects.new(name, me))
    me.shade_smooth()
    return obj


def ellipse(a, b, n):
    return [(a * math.cos(2 * math.pi * i / n), b * math.sin(2 * math.pi * i / n)) for i in range(n)]


def rounded_rect(a, b, r, per_corner=2):
    """half sizes a, b"""
    pts = []
    for (cx, cy, s) in ((a - r, b - r, 0), (-a + r, b - r, 90), (-a + r, -b + r, 180), (a - r, -b + r, 270)):
        for k in range(per_corner + 1):
            t = math.radians(s + 90.0 * k / per_corner)
            pts.append((cx + r * math.cos(t), cy + r * math.sin(t)))
    return pts


def lathe(name, profile, n=16):
    """[(x, r)] around the X axis, closed at both ends if r = 0 there"""
    verts = []
    faces = []
    m = len(profile)
    for i, (x, r) in enumerate(profile):
        for j in range(n):
            t = 2 * math.pi * j / n
            verts.append((x, r * math.cos(t), r * math.sin(t)))
    for i in range(m - 1):
        for j in range(n):
            j2 = (j + 1) % n
            faces.append((i * n + j, i * n + j2, (i + 1) * n + j2, (i + 1) * n + j))
    faces.append(tuple(reversed(range(n))))
    faces.append(tuple((m - 1) * n + j for j in range(n)))
    me = bpy.data.meshes.new(name)
    me.from_pydata(verts, [], faces)
    me.update()
    return link(bpy.data.objects.new(name, me))


def transform(obj, mat):
    obj.data.transform(mat)
    obj.data.update()


def join(objs, name):
    activate(objs[0])
    for o in objs[1:]:
        o.select_set(True)
    bpy.ops.object.join()
    o = bpy.context.view_layer.objects.active
    o.name = name
    o.data.name = name
    return o


def to_bottom(obj, center_xy=True):
    """the lowest point on z = 0, the middle of the outline on the origin"""
    co = [v.co for v in obj.data.vertices]
    lo = Vector((min(c.x for c in co), min(c.y for c in co), min(c.z for c in co)))
    hi = Vector((max(c.x for c in co), max(c.y for c in co), max(c.z for c in co)))
    d = Vector(((lo.x + hi.x) * 0.5 if center_xy else 0.0, (lo.y + hi.y) * 0.5 if center_xy else 0.0, lo.z))
    transform(obj, Matrix.Translation(-d))
    return (hi - lo)


def stamp_uv(obj, x0, y0, x1, y1):
    """the UV layer of the stamps: the part as it lies before it is turned into place"""
    lay = obj.data.uv_layers.new(name="stamp")
    for p in obj.data.polygons:
        for li in p.loop_indices:
            v = obj.data.vertices[obj.data.loops[li].vertex_index].co
            lay.uv[li].vector = ((v.x - x0) / (x1 - x0), (v.y - y0) / (y1 - y0))


def set_material(obj, mat):
    obj.data.materials.clear()
    obj.data.materials.append(mat)


# ---------------------------------------------------------------------------- helpers: materials
class G:
    """a small language for node trees"""

    def __init__(self, name):
        self.mat = bpy.data.materials.new(name)
        self.mat.use_nodes = True
        self.nt = self.mat.node_tree
        self.nt.nodes.clear()
        self.out = self.add("ShaderNodeOutputMaterial")
        self.p = self.add("ShaderNodeBsdfPrincipled")
        self.geo = self.add("ShaderNodeNewGeometry")
        self.tc = self.add("ShaderNodeTexCoord")
        self.link(self.p.outputs[0], self.out.inputs["Surface"])
        self.bake = self.add("ShaderNodeTexImage")
        self.bake.name = "__bake"
        self.emit = self.add("ShaderNodeEmission")
        self.outs = {}

    def add(self, t, **kw):
        n = self.nt.nodes.new(t)
        for k, v in kw.items():
            setattr(n, k, v)
        return n

    def link(self, a, b):
        self.nt.links.new(a, b)

    def _set(self, sock, v):
        if hasattr(v, "is_linked") or hasattr(v, "links"):
            self.link(v, sock)
        else:
            sock.default_value = v

    def m(self, op, a, b=None, clamp=False):
        n = self.add("ShaderNodeMath", operation=op, use_clamp=clamp)
        self._set(n.inputs[0], a)
        if b is not None:
            self._set(n.inputs[1], b)
        return n.outputs[0]

    def rng(self, v, lo, hi, tlo=0.0, thi=1.0):
        n = self.add("ShaderNodeMapRange", interpolation_type="SMOOTHSTEP")
        self._set(n.inputs["Value"], v)
        n.inputs["From Min"].default_value = lo
        n.inputs["From Max"].default_value = hi
        n.inputs["To Min"].default_value = tlo
        n.inputs["To Max"].default_value = thi
        return n.outputs[0]

    def mix(self, f, a, b):
        n = self.add("ShaderNodeMix", data_type="RGBA", clamp_factor=True)
        self._set(n.inputs[0], f)
        for s, v in ((n.inputs[6], a), (n.inputs[7], b)):
            if isinstance(v, (tuple, list)):
                s.default_value = (v[0], v[1], v[2], 1.0)
            else:
                self.link(v, s)
        return n.outputs[2]

    def noise(self, scale, detail=4.0, rough=0.55, stretch=(1, 1, 1), offset=(0, 0, 0), distort=0.0):
        mp = self.add("ShaderNodeMapping")
        self.link(self.tc.outputs["Object"], mp.inputs["Vector"])
        mp.inputs["Scale"].default_value = stretch
        mp.inputs["Location"].default_value = offset
        n = self.add("ShaderNodeTexNoise")
        self.link(mp.outputs[0], n.inputs["Vector"])
        n.inputs["Scale"].default_value = scale
        n.inputs["Detail"].default_value = detail
        n.inputs["Roughness"].default_value = rough
        n.inputs["Distortion"].default_value = distort
        return n.outputs[0]

    def voronoi(self, scale, offset=(0, 0, 0)):
        mp = self.add("ShaderNodeMapping")
        self.link(self.tc.outputs["Object"], mp.inputs["Vector"])
        mp.inputs["Location"].default_value = offset
        n = self.add("ShaderNodeTexVoronoi")
        self.link(mp.outputs[0], n.inputs["Vector"])
        n.inputs["Scale"].default_value = scale
        return n.outputs[0]

    def edges(self, radius, lo=0.02, hi=0.18):
        """1 on a worn edge: how far the rounded normal is from the real one"""
        b = self.add("ShaderNodeBevel", samples=8)
        b.inputs["Radius"].default_value = radius
        d = self.add("ShaderNodeVectorMath", operation="DOT_PRODUCT")
        self.link(b.outputs[0], d.inputs[0])
        self.link(self.geo.outputs["Normal"], d.inputs[1])
        return self.rng(self.m("SUBTRACT", 1.0, d.outputs["Value"]), lo, hi)

    def ao(self, dist):
        n = self.add("ShaderNodeAmbientOcclusion", samples=16, only_local=True)
        n.inputs["Distance"].default_value = dist
        return n.outputs["AO"]

    def stamp(self, path, only_top=True):
        """an image laid on the part through the UV layer "stamp" (stamp_uv); 1 where the surface stays"""
        img = bpy.data.images.load(path)
        img.colorspace_settings.name = "Non-Color"
        uv = self.add("ShaderNodeUVMap", uv_map="stamp")
        t = self.add("ShaderNodeTexImage", image=img, extension="EXTEND", interpolation="Cubic")
        self.link(uv.outputs[0], t.inputs["Vector"])
        v = t.outputs["Color"]
        if not only_top:
            return v
        sep = self.add("ShaderNodeSeparateXYZ")
        self.link(self.geo.outputs["True Normal"], sep.inputs[0])
        top = self.rng(sep.outputs["Z"], 0.3, 0.6)
        return self.m("MAXIMUM", v, self.m("SUBTRACT", 1.0, top))

    def finish(self, base, rough, metal, height=None, bump=1.0, dist=0.02):
        self._set(self.p.inputs["Base Color"], base)
        self._set(self.p.inputs["Roughness"], rough)
        self.p.inputs["Metallic"].default_value = metal
        if height is not None:
            b = self.add("ShaderNodeBump")
            b.inputs["Strength"].default_value = bump
            b.inputs["Distance"].default_value = dist
            self.link(height, b.inputs["Height"])
            self.link(b.outputs[0], self.p.inputs["Normal"])
        self.outs = {"base": base, "rough": rough}
        return self.mat


# ---------------------------------------------------------------------------- materials
def mat_brass(work):
    g = G("brass")
    edge = g.edges(0.035, 0.02, 0.14)
    ao = g.ao(0.35)
    big = g.noise(1.4, 5, 0.5, offset=(3.1, 1.7, 0.4))
    mid = g.noise(7, 5, 0.55, offset=(1.1, 8.7, 2.4))
    fine = g.noise(46, 4, 0.6)
    scr = g.noise(9, 2, 0.5, stretch=(0.5, 22, 1), distort=0.2)
    scr2 = g.noise(9, 2, 0.5, stretch=(20, 0.7, 1), offset=(5, 2, 1), distort=0.25)
    scratches = g.m("MAXIMUM", g.rng(scr, 0.63, 0.69), g.rng(scr2, 0.64, 0.70))
    # polished by pockets and fingers: the edges; the flats keep an even, slightly tarnished tone
    rub = g.m("MULTIPLY", edge, g.rng(ao, 0.45, 0.9))
    col = g.mix(g.rng(big, 0.38, 0.62), (0.37, 0.235, 0.075), (0.47, 0.31, 0.105))
    col = g.mix(g.m("MULTIPLY", g.rng(mid, 0.5, 0.68), 0.45), col, (0.20, 0.125, 0.05))
    col = g.mix(g.m("MAXIMUM", rub, g.m("MULTIPLY", scratches, 0.55)), col, (0.80, 0.585, 0.25))
    col = g.mix(g.rng(ao, 0.8, 0.25), col, (0.03, 0.02, 0.01))
    rough = g.m("ADD", g.m("MULTIPLY", g.m("SUBTRACT", 1.0, rub), 0.17), 0.25)
    rough = g.m("ADD", rough, g.m("MULTIPLY", mid, 0.14))
    h = g.m("ADD", g.m("SUBTRACT", 1.0, g.m("MULTIPLY", scratches, 0.5)), g.m("MULTIPLY", fine, 0.1))
    stamp = g.stamp(os.path.join(work, "stamp_bow.png"))
    col = g.mix(g.rng(stamp, 0.8, 0.2), col, (0.05, 0.032, 0.016))
    h = g.m("MULTIPLY", h, g.m("ADD", g.m("MULTIPLY", stamp, 0.7), 0.3))
    return g, g.finish(col, rough, 1.0, h, 1.0, 0.035)


def mat_steel(work):
    g = G("steel")
    edge = g.edges(0.02)
    ao = g.ao(0.3)
    big = g.noise(3, 5, 0.55)
    fine = g.noise(40, 4, 0.6)
    rust = g.rng(g.noise(9, 6, 0.6, offset=(1, 7, 2)), 0.60, 0.70)
    col = g.mix(g.rng(big, 0.35, 0.65), (0.42, 0.43, 0.44), (0.55, 0.555, 0.56))
    col = g.mix(edge, col, (0.76, 0.76, 0.77))
    col = g.mix(g.m("MULTIPLY", rust, g.rng(ao, 0.95, 0.5)), col, (0.17, 0.075, 0.025))
    col = g.mix(g.rng(ao, 0.7, 0.2), col, (0.02, 0.018, 0.016))
    rough = g.m("ADD", g.m("ADD", 0.26, g.m("MULTIPLY", big, 0.2)), g.m("MULTIPLY", rust, 0.3))
    return g, g.finish(col, rough, 1.0, fine, 0.2, 0.01)


def mat_aluminium(work):
    g = G("aluminium")
    edge = g.edges(0.03)
    ao = g.ao(0.3)
    big = g.noise(1.6, 5, 0.5, offset=(9, 1, 3))
    mid = g.noise(9, 5, 0.55, offset=(2, 3, 7))
    fine = g.noise(44, 5, 0.6)
    scr = g.noise(7, 2, 0.5, stretch=(1.0, 14, 1), offset=(2, 2, 2), distort=0.35)
    scr2 = g.noise(7, 2, 0.5, stretch=(12, 1.4, 1), offset=(4, 8, 1), distort=0.35)
    scratches = g.m("MAXIMUM", g.rng(scr, 0.62, 0.68), g.rng(scr2, 0.63, 0.69))
    col = g.mix(g.rng(big, 0.38, 0.62), (0.26, 0.262, 0.265), (0.33, 0.333, 0.337))
    col = g.mix(g.m("MULTIPLY", g.rng(mid, 0.5, 0.7), 0.4), col, (0.17, 0.172, 0.175))
    col = g.mix(g.m("MAXIMUM", edge, g.m("MULTIPLY", scratches, 0.6)), col, (0.62, 0.62, 0.63))
    col = g.mix(g.rng(ao, 0.7, 0.25), col, (0.03, 0.03, 0.028))
    rough = g.m("ADD", 0.36, g.m("MULTIPLY", mid, 0.26))
    h = g.m("ADD", g.m("MULTIPLY", g.m("SUBTRACT", 1.0, scratches), 0.3), g.m("MULTIPLY", fine, 0.1))
    stamp = g.stamp(os.path.join(work, "stamp_tag.png"))
    col = g.mix(g.rng(stamp, 0.8, 0.2), col, (0.022, 0.022, 0.022))
    h = g.m("MULTIPLY", h, g.m("ADD", g.m("MULTIPLY", stamp, 0.72), 0.28))
    return g, g.finish(col, rough, 1.0, h, 1.0, 0.06)


def mat_iron(work):
    g = G("iron")
    edge = g.edges(0.05, 0.02, 0.15)
    ao = g.ao(0.6)
    big = g.noise(1.1, 7, 0.58, offset=(2, 5, 1))
    mid = g.noise(6, 6, 0.6, offset=(7, 1, 4))
    fine = g.noise(40, 4, 0.6)
    pits = g.rng(g.voronoi(15), 0.05, 0.3)
    rust_mask = g.m("MULTIPLY", g.rng(big, 0.44, 0.58), g.rng(mid, 0.36, 0.56))
    rust_mask = g.m("MULTIPLY", rust_mask, g.m("SUBTRACT", 1.0, g.m("MULTIPLY", edge, 0.85)))
    col = g.mix(g.rng(mid, 0.38, 0.62), (0.085, 0.082, 0.08), (0.17, 0.165, 0.158))
    col = g.mix(g.m("MULTIPLY", edge, g.rng(ao, 0.5, 0.9)), col, (0.56, 0.55, 0.53))
    rust = g.mix(g.rng(fine, 0.4, 0.62), (0.15, 0.05, 0.02), (0.43, 0.175, 0.05))
    col = g.mix(rust_mask, col, rust)
    col = g.mix(g.rng(ao, 0.75, 0.2), col, (0.014, 0.012, 0.011))
    rough = g.m("ADD", g.m("MULTIPLY", g.m("SUBTRACT", 1.0, edge), 0.24), 0.3)
    rough = g.m("ADD", rough, g.m("MULTIPLY", rust_mask, 0.36))
    h = g.m("ADD", g.m("MULTIPLY", mid, 0.5), g.m("MULTIPLY", fine, 0.2))
    h = g.m("ADD", h, g.m("MULTIPLY", rust_mask, 0.4))
    h = g.m("SUBTRACT", h, g.m("MULTIPLY", g.m("SUBTRACT", 1.0, pits), 0.3))
    return g, g.finish(col, rough, 1.0, h, 1.0, 0.03)


def mat_lacquer(work):
    g = G("lacquer")
    edge = g.edges(0.02, 0.015, 0.11)
    big = g.noise(5, 6, 0.6, offset=(1, 4, 2))
    fine = g.noise(44, 3, 0.55)
    # a dark bronze finish, gone on the bends and in a few patches along the wire
    bare = g.m("MAXIMUM", g.m("MULTIPLY", edge, g.rng(g.noise(3.2, 4, 0.5, offset=(4, 2, 7)), 0.5, 0.64)), g.rng(big, 0.66, 0.72))
    col = g.mix(g.rng(big, 0.4, 0.6), (0.075, 0.052, 0.036), (0.125, 0.09, 0.062))
    col = g.mix(bare, col, (0.62, 0.53, 0.40))
    rough = g.mix(bare, (0.22, 0.22, 0.22), (0.40, 0.40, 0.40))
    return g, g.finish(col, rough, 0.5, fine, 0.1, 0.006)

def mat_stand(work):
    g = G("stand")
    edge = g.edges(0.25, 0.02, 0.14)
    ao = g.ao(3.0)
    big = g.noise(0.09, 6, 0.55, offset=(2, 1, 5))
    fine = g.noise(1.4, 5, 0.6)
    chips = g.m("MULTIPLY", edge, g.rng(g.noise(0.5, 4, 0.55, offset=(7, 3, 1)), 0.42, 0.6))
    rust = g.rng(g.noise(0.16, 6, 0.6, offset=(4, 9, 2)), 0.6, 0.7)
    col = g.mix(g.rng(big, 0.35, 0.65), (0.05, 0.052, 0.055), (0.085, 0.088, 0.092))
    col = g.mix(chips, col, (0.42, 0.42, 0.41))
    col = g.mix(g.m("MULTIPLY", rust, g.rng(ao, 0.9, 0.5)), col, (0.15, 0.06, 0.022))
    col = g.mix(g.rng(ao, 0.75, 0.2), col, (0.01, 0.01, 0.01))
    rough = g.m("ADD", g.m("ADD", 0.42, g.m("MULTIPLY", big, 0.2)), g.m("MULTIPLY", rust, 0.3))
    return g, g.finish(col, rough, 0.6, fine, 0.3, 0.05)


def mat_rubber(work):
    g = G("rubber")
    fine = g.noise(40, 4, 0.6)
    col = g.mix(g.rng(fine, 0.4, 0.6), (0.012, 0.012, 0.013), (0.03, 0.03, 0.031))
    return g, g.finish(col, 0.55, 0.0, fine, 0.1, 0.01)


# ---------------------------------------------------------------------------- the models
def build_key_flat(work):
    """a cylinder lock key of brass, on a split ring with a stamped aluminium tag"""
    th = 0.21
    # the teeth, from the tip to the shoulder
    top = 0.375
    tip = 3.05
    teeth = [(2.56, 0.13), (2.17, 0.26), (1.79, 0.09), (1.39, 0.23), (1.00, 0.15), (0.60, 0.27)]
    o = [(0.0, -top), (tip - 0.22, -top), (tip, -0.12), (tip, 0.08), (tip - 0.14, top - 0.02)]
    for x, d in teeth:
        o += [(x + 0.17, top), (x + 0.09, top), (x + 0.015, top - d), (x - 0.015, top - d), (x - 0.09, top), (x - 0.17, top)]
    o += [(0.12, top), (0.0, top), (0.0, 0.54), (-0.10, 0.54)]
    bow = (-1.25, 0.0, 1.17)
    a = math.degrees(math.asin(0.54 / bow[2]))
    o += arc(bow[0], bow[1], bow[2], a, 360 - a, 48)
    o += [(-0.10, -0.54), (0.0, -0.54)]
    hole = arc(-1.72, 0.0, 0.37, 0, 354, 30)[:-1]
    key = extrude2d("key", [o, hole], th)
    stamp_uv(key, -2.95, -1.2, 0.05, 1.2)
    # the millings along the blade
    for (y0, y1, z0, z1) in ((-0.20, -0.075, th * 0.5 - 0.055, th), (0.03, 0.15, -th, -th * 0.5 + 0.055), (-0.31, -0.25, -th, -th * 0.5 + 0.04)):
        cut(key, box("groove", (0.14, y0, z0), (tip + 0.3, y1, z1)))
    bevel(key, 0.016, 2)
    g_key, m_key = mat_brass(work)
    set_material(key, m_key)

    # the ring goes through the hole of the bow, tilted; its low side rests on the table
    tilt = math.radians(20)
    r = 1.16
    wire = rounded_rect(0.085, 0.036, 0.03, 1)
    n = 72
    turns = 2.1
    pts = []
    for i in range(n + 1):
        t = 2 * math.pi * turns * i / n - math.pi * 0.35
        z = 0.075 * (i / n - 0.5) * turns
        rr = r + 0.012 * math.sin(i / n * math.pi)
        pts.append(Vector((rr * math.cos(t), rr * math.sin(t), z)))
    ring = sweep("ring", pts, wire)
    transform(ring, Matrix.Translation((-1.72 - r, 0, 0)) @ Matrix.Rotation(tilt, 4, "X"))
    g_ring, m_ring = mat_steel(work)
    set_material(ring, m_ring)

    # the key leans on the ring, its tip on the table
    transform(key, Matrix.Translation((-1.72, 0, 0)) @ Matrix.Rotation(math.radians(5.0), 4, "Y") @ Matrix.Rotation(math.radians(-3), 4, "X") @ Matrix.Translation((1.72, 0, 0)))

    # the tag: an aluminium pill with a hole, stamped; hangs from the far side of the ring
    tl = 3.0
    tw = 0.68
    pill = arc(0.0, 0.0, tw, 90, 270, 20) + arc(tl - 2 * tw + 0.0, 0.0, tw, -90, 90, 20)
    tag_hole = arc(-0.28, 0.0, 0.21, 0, 345, 24)[:-1]
    tag = extrude2d("tag", [pill, tag_hole], 0.11)
    stamp_uv(tag, -tw, -tw, tl - tw, tw)
    bevel(tag, 0.02, 2)
    g_tag, m_tag = mat_aluminium(work)
    set_material(tag, m_tag)
    transform(tag, Matrix.Translation((-1.72 - 2 * r, 0, 0.0)) @ Matrix.Rotation(math.radians(158), 4, "Z") @ Matrix.Rotation(math.radians(8.5), 4, "Y") @ Matrix.Translation((0.28, 0, 0)))

    obj = join([key, ring, tag], "key_flat")
    to_bottom(obj)
    return obj, [g_key, g_ring, g_tag], (1024, 1024)


def build_key_old(work):
    """an old cast iron key: a trefoil bow, a shaft with collars, a bit with wards"""
    # the bow, swept along a three-lobed path
    pts = []
    n = 96
    cx = -1.55
    for i in range(n):
        t = 2 * math.pi * i / n
        rr = 1.16 + 0.27 * math.cos(3 * (t - math.pi))
        pts.append(Vector((cx + rr * math.cos(t), rr * math.sin(t) * 1.04, 0)))
    bow = sweep("bow", pts, ellipse(0.17, 0.205, 14), closed=True)

    # the shaft: from the collars at the bow to the pin at the tip
    p = [(-0.55, 0.0), (-0.55, 0.20), (-0.42, 0.31), (-0.28, 0.30), (-0.22, 0.42), (-0.12, 0.46), (-0.02, 0.42), (0.04, 0.33), (0.2, 0.30),
         (0.26, 0.37), (0.34, 0.39), (0.42, 0.37), (0.48, 0.29), (1.2, 0.265), (4.6, 0.255), (5.0, 0.285), (5.06, 0.33), (5.14, 0.34), (5.2, 0.30),
         (5.26, 0.255), (6.6, 0.255), (6.7, 0.22), (6.76, 0.0)]
    shaft = lathe("shaft", p, 18)
    shaft.data.shade_smooth()

    # the bit: a plate with the wards cut into it
    x0, x1 = 5.3, 6.5
    y0, y1 = 0.1, 1.5
    o = [(x0, y0), (x1, y0), (x1, y1 - 0.04), (x1 - 0.05, y1), (x1 - 0.32, y1), (x1 - 0.36, y1 - 0.05), (x1 - 0.36, y1 - 0.5),
         (x1 - 0.5, y1 - 0.5), (x1 - 0.5, y1 - 0.05), (x1 - 0.54, y1), (x0 + 0.05, y1), (x0, y1 - 0.04),
         (x0, y0 + 0.92), (x0 + 0.34, y0 + 0.92), (x0 + 0.34, y0 + 0.74), (x0, y0 + 0.74)]
    bit = extrude2d("bit", [o], 0.3)
    bevel(bit, 0.022, 2)

    obj = join([bow, shaft, bit], "key_old")
    g, m = mat_iron(work)
    set_material(obj, m)
    to_bottom(obj)
    return obj, [g], (1024, 1024)


def build_bobby_pin(work):
    """a bobby pin: flat wire bent double, one leg straight, the other crimped, rubber drops on the tips"""
    L = 5.0
    w = 0.085       # half the width of the wire, across the plane of the pin
    t = 0.036       # half its thickness
    pts = []

    def top(x):
        # the straight leg: a slight bow that meets the other leg, then flares
        f = (x - 0.25) / (L - 0.25)
        return 0.20 - 0.17 * math.sin(min(f, 1.0) * math.pi * 0.62) + 0.34 * max(0.0, f - 0.8) ** 2 * 6.0

    def low(x):
        # the crimped leg
        base = -0.20 + 0.098 * min(1.0, (x - 0.25) / 2.6)
        env = max(0.0, min(1.0, (x - 0.55) / 0.3)) * max(0.0, min(1.0, (4.35 - x) / 0.3))
        w_ = 0.085 * env * math.sin(2 * math.pi * (x - 0.55) / 0.74)
        flare = -0.42 * max(0.0, (x - 4.3) / 0.7) ** 2
        return base + w_ + flare

    # from the tip of the straight leg to the bend...
    n = 60
    for i in range(n):
        x = L - (L - 0.25) * i / (n - 1)
        pts.append(Vector((x, top(x), 0)))
    # ...round the bend...
    y0, y1 = top(0.25), low(0.25)
    cy = (y0 + y1) * 0.5
    ry = (y0 - y1) * 0.5
    for i in range(1, 12):
        a = math.pi * 0.5 + math.pi * i / 12
        pts.append(Vector((0.25 + 0.27 * math.cos(a), cy + ry * math.sin(a), 0)))
    # ...and along the crimped leg
    n = 96
    L2 = L - 0.12
    for i in range(n):
        x = 0.25 + (L2 - 0.25) * i / (n - 1)
        pts.append(Vector((x, low(x), 0)))
    wire = sweep("pin", pts, rounded_rect(t, w, 0.03, 1), uv=(0.0, 0.0, 1.0, 0.75), normal0=Vector((0, 0, 1)).cross(Vector((-1, 0, 0))))
    g_w, m_w = mat_lacquer(work)
    set_material(wire, m_w)

    # the rubber drops
    drops = []
    for k, (p, q) in enumerate(((pts[0], pts[1]), (pts[-1], pts[-2]))):
        d = (p - q).normalized()
        # a lathe along X, turned to the end of the wire
        lp = [(-0.14, 0.0)]
        for i in range(11):
            s = i / 10.0
            r = 0.095 * math.sin(math.pi * (0.06 + 0.9 * s)) ** 0.62 * (0.78 + 0.22 * s)
            lp.append((-0.13 + 0.33 * s, r))
        lp.append((0.205, 0.0))
        dr = lathe("drop", lp, 12)
        dr.data.shade_smooth()
        ang = math.atan2(d.y, d.x)
        transform(dr, Matrix.Translation(p) @ Matrix.Rotation(ang, 4, "Z"))
        lay = dr.data.uv_layers.new(name="UVMap")
        for poly in dr.data.polygons:
            for li in poly.loop_indices:
                v = dr.data.vertices[dr.data.loops[li].vertex_index].co
                # a corner of the atlas is enough: the rubber has no pattern worth a texel
                lay.uv[li].vector = (0.02 + 0.2 * k + 0.16 * ((v.x % 0.4) / 0.4), 0.8 + 0.16 * ((v.y % 0.4) / 0.4))
        drops.append(dr)
    g_r, m_r = mat_rubber(work)
    for d in drops:
        set_material(d, m_r)
    obj = join([wire] + drops, "bobby_pin")
    to_bottom(obj)
    return obj, [g_w, g_r], (1024, 128)


def build_suit_stand(work):
    """what a suit stands on: a round plate with a rim and bolts, a pole up to the chest, a bar for the shoulders.
    In centimetres, the same 0.512 units each: 54 cm across, 100 cm tall"""
    plate = lathe("plate", [(0.0, 0.0), (0.0, 25.0), (0.5, 26.6), (1.4, 27.0), (2.2, 27.0), (2.35, 25.6), (2.35, 24.2), (2.6, 23.6), (2.6, 7.0), (4.0, 5.2), (6.0, 2.4), (6.4, 0.0)], 48)
    transform(plate, Matrix.Rotation(math.radians(-90), 4, "Y"))
    plate.data.shade_smooth()
    pole = lathe("pole", [(0.0, 0.0), (0.0, 1.1), (118.0, 1.1), (118.0, 0.0)], 12)
    transform(pole, Matrix.Rotation(math.radians(-90), 4, "Y"))
    pole.data.shade_smooth()
    bar = lathe("bar", [(-24.0, 0.0), (-24.0, 0.9), (24.0, 0.9), (24.0, 0.0)], 10)
    transform(bar, Matrix.Rotation(math.radians(90), 4, "Z") @ Matrix.Translation((0, 0, 0)))
    transform(bar, Matrix.Translation((0, 0, 112.0)))
    bar.data.shade_smooth()
    parts = [plate, pole, bar]
    for i in range(8):
        a = 2 * math.pi * (i + 0.5) / 8
        bolt = lathe("bolt", [(0.0, 0.0), (0.0, 1.1), (0.5, 1.1), (0.75, 0.75), (0.8, 0.0)], 6)
        transform(bolt, Matrix.Translation((21.0 * math.cos(a), 21.0 * math.sin(a), 2.5)) @ Matrix.Rotation(math.radians(-90), 4, "Y"))
        bolt.data.shade_smooth()
        parts.append(bolt)
    obj = join(parts, "suit_stand")
    weld(obj, 1e-3)
    g, m = mat_stand(work)
    set_material(obj, m)
    return obj, [g], (512, 512)


BUILD = {"key_flat": build_key_flat, "key_old": build_key_old, "bobby_pin": build_bobby_pin, "suit_stand": build_suit_stand}
OWN_UV = {"bobby_pin"}


# ---------------------------------------------------------------------------- UV, bake, SMD
def unwrap(obj, own_uv):
    """own_uv: the builder laid the atlas itself. Otherwise whatever "UVMap" is there goes: a mesh made
    from a curve comes with one of that name, the parameters of the curve (mostly lines)"""
    activate(obj)
    lay = obj.data.uv_layers.get("UVMap")
    if lay is not None and not own_uv:
        obj.data.uv_layers.remove(lay)
        lay = None
    if lay is None:
        lay = obj.data.uv_layers.new(name="UVMap")
    obj.data.uv_layers.active = lay
    lay.active_render = True
    if own_uv:
        return
    bpy.ops.object.mode_set(mode="EDIT")
    bpy.ops.mesh.select_all(action="SELECT")
    bpy.ops.uv.smart_project(angle_limit=math.radians(55), island_margin=0.012, area_weight=0.6, scale_to_bounds=False)
    bpy.ops.uv.select_all(action="SELECT")
    bpy.ops.uv.pack_islands(rotate=True, margin=0.012)
    bpy.ops.object.mode_set(mode="OBJECT")


def smooth(obj, angle=38.0):
    obj.data.shade_smooth()
    obj.data.set_sharp_from_angle(angle=math.radians(angle))
    obj.data.update()


def bake(obj, graphs, size, out):
    scene = bpy.context.scene
    scene.render.engine = "CYCLES"
    scene.cycles.device = "CPU"
    scene.cycles.samples = 32
    scene.cycles.use_denoising = False
    scene.render.bake.margin = 24
    scene.render.bake.margin_type = "EXTEND"
    activate(obj)
    images = {}

    def run(kind, name, colorspace, cycles_type, **kw):
        img = bpy.data.images.new(name, size[0], size[1], alpha=False, float_buffer=False)
        img.colorspace_settings.name = colorspace
        for g in graphs:
            g.bake.image = img
            g.nt.nodes.active = g.bake
            g.bake.select = True
            if kind in g.outs:
                v = g.outs[kind]
                if hasattr(v, "links"):
                    g.link(v, g.emit.inputs["Color"])
                else:
                    g.emit.inputs["Color"].default_value = (v, v, v, 1)
                g.link(g.emit.outputs[0], g.out.inputs["Surface"])
            else:
                g.link(g.p.outputs[0], g.out.inputs["Surface"])
        bpy.ops.object.bake(type=cycles_type, use_clear=True, **kw)
        img.filepath_raw = os.path.join(out, name + ".png")
        img.file_format = "PNG"
        img.save()
        images[kind] = img

    run("base", obj.name + "_base", "sRGB", "EMIT")
    run("rough", obj.name + "_rough", "Non-Color", "EMIT")
    run("normal", obj.name + "_normal", "Non-Color", "NORMAL", normal_space="TANGENT", normal_r="POS_X", normal_g="NEG_Y", normal_b="POS_Z")
    for g in graphs:
        g.link(g.p.outputs[0], g.out.inputs["Surface"])


def write_smd(obj, path, material):
    me = obj.data
    me.calc_loop_triangles()
    uv = me.uv_layers["UVMap"].uv
    cn = me.corner_normals
    with open(path, "w", encoding="ascii", newline="\n") as f:
        f.write("version 1\nnodes\n0 \"root\" -1\nend\nskeleton\ntime 0\n0 0 0 0 0 0 0\nend\ntriangles\n")
        for t in me.loop_triangles:
            f.write(material + "\n")
            for li in t.loops:
                v = me.vertices[me.loops[li].vertex_index].co * CM_TO_UNIT
                n = cn[li].vector
                u = uv[li].vector
                f.write("0 %.6f %.6f %.6f %.5f %.5f %.5f %.6f %.6f\n" % (v.x, v.y, v.z, n.x, n.y, n.z, u.x, u.y))
        f.write("end\n")


def render(obj, path):
    scene = bpy.context.scene
    scene.render.engine = "CYCLES"
    scene.cycles.device = "CPU"
    scene.cycles.samples = 48
    scene.cycles.use_denoising = False
    scene.render.resolution_x = 1400
    scene.render.resolution_y = 800
    scene.render.film_transparent = False
    world = bpy.data.worlds.new("w")
    world.use_nodes = True
    world.node_tree.nodes["Background"].inputs[0].default_value = (0.55, 0.56, 0.58, 1)
    scene.world = world
    co = [v.co for v in obj.data.vertices]
    lo = Vector((min(c.x for c in co), min(c.y for c in co), min(c.z for c in co)))
    hi = Vector((max(c.x for c in co), max(c.y for c in co), max(c.z for c in co)))
    c = (lo + hi) * 0.5
    size = (hi - lo).length
    cam = bpy.data.cameras.new("cam")
    cam.lens = 85
    co = link(bpy.data.objects.new("cam", cam))
    co.location = c + Vector((0.0, -1.5, 3.0)).normalized() * size * 3.3
    co.rotation_euler = (c - co.location).to_track_quat("-Z", "Y").to_euler()
    scene.camera = co
    for (loc, en, sz) in (((-1.0, -1.0, 2.2), 80.0, 6), ((2, 0.6, 1.2), 18.0, 3)):
        la = bpy.data.lights.new("l", "AREA")
        la.energy = en * (size / 8.0) ** 2
        la.size = sz * size / 8.0
        lo_ = link(bpy.data.objects.new("l", la))
        lo_.location = c + Vector(loc) * size * 0.6
        lo_.rotation_euler = (c - lo_.location).to_track_quat("-Z", "Y").to_euler()
    pl = bpy.data.meshes.new("pl")
    pl.from_pydata([(-100, -100, -0.001), (100, -100, -0.001), (100, 100, -0.001), (-100, 100, -0.001)], [], [(0, 1, 2, 3)])
    pl.update()
    link(bpy.data.objects.new("pl", pl))
    scene.render.filepath = path
    bpy.ops.render.render(write_still=True)


def main():
    argv = sys.argv[sys.argv.index("--") + 1:]
    name, work = argv[0], argv[1]
    flags = argv[2:]
    bpy.ops.wm.read_factory_settings(use_empty=True)
    obj, graphs, size = BUILD[name](work)
    smooth(obj)
    unwrap(obj, name in OWN_UV)
    print("[items] %s: %d vertices, %d triangles" % (name, len(obj.data.vertices), sum(len(p.vertices) - 2 for p in obj.data.polygons)))
    co = [v.co for v in obj.data.vertices]
    print("[items] size, cm: %.2f x %.2f x %.2f" % (max(c.x for c in co) - min(c.x for c in co), max(c.y for c in co) - min(c.y for c in co), max(c.z for c in co) - min(c.z for c in co)))
    if "bake" in flags:
        bake(obj, graphs, size, work)
        write_smd(obj, os.path.join(work, name + ".smd"), name)
        bpy.ops.wm.save_as_mainfile(filepath=os.path.join(work, name + ".blend"), check_existing=False)
    if "render" in flags:
        render(obj, os.path.join(work, name + "_preview.png"))


try:
    main()
except Exception:
    import traceback
    traceback.print_exc()
    sys.exit(1)
