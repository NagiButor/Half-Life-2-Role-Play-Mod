"""Blender (no window): more polygons for a low-poly model - an SMD in, an SMD out.

    blender --background --factory-startup --python blender_subdiv.py -- in.smd out.smd [levels] [hard_angle]

The triangles are welded by position, the hard edges of the original (where its normals break, or
where the faces meet at more than hard_angle degrees) get full crease and stay sharp, everything
else is Catmull-Clark subdivided (UVs follow, their seams kept). Normals: smooth, split at the hard
edges. Bone weights: those of the nearest original vertex (the weapon meshes are rigid anyway).
"""
import math
import sys

import bmesh
import bpy
from mathutils import Vector
from mathutils.kdtree import KDTree

argv = sys.argv[sys.argv.index("--") + 1:]
SRC, DST = argv[0], argv[1]
LEVELS = int(argv[2]) if len(argv) > 2 else 1
HARD = float(argv[3]) if len(argv) > 3 else 35.0


def read_smd(path):
    head, tris = [], []
    lines = open(path, encoding="ascii", errors="replace").read().split("\n")
    i = 0
    while i < len(lines) and lines[i].strip() != "triangles":
        head.append(lines[i])
        i += 1
    i += 1
    while i < len(lines):
        mat = lines[i].strip()
        if mat == "end" or not mat:
            break
        corners = []
        for k in range(3):
            p = lines[i + 1 + k].split()
            pos = tuple(float(x) for x in p[1:4])
            nrm = tuple(float(x) for x in p[4:7])
            uv = (float(p[7]), float(p[8]))
            links = []
            if len(p) > 9:
                n = int(p[9])
                links = [(int(p[10 + 2 * j]), float(p[11 + 2 * j])) for j in range(n)]
            else:
                links = [(int(p[0]), 1.0)]
            corners.append((pos, nrm, uv, links))
        tris.append((mat, corners))
        i += 4
    return head, tris


head, tris = read_smd(SRC)
mats = sorted({t[0] for t in tris})

# weld by position
key_of = {}
verts, vlinks = [], []
faces, face_mats, face_uvs, face_nrms = [], [], [], []
for mat, corners in tris:
    idx = []
    for pos, nrm, uv, links in corners:
        k = tuple(round(c, 4) for c in pos)
        if k not in key_of:
            key_of[k] = len(verts)
            verts.append(pos)
            vlinks.append(links)
        idx.append(key_of[k])
    if len(set(idx)) < 3:
        continue
    faces.append(idx)
    face_mats.append(mats.index(mat))
    face_uvs.append([c[2] for c in corners])
    face_nrms.append([c[1] for c in corners])

me = bpy.data.meshes.new("m")
me.from_pydata(verts, [], faces)
me.update()
for m in mats:
    me.materials.append(bpy.data.materials.new(m))
uvl = me.uv_layers.new(name="uv")
for fi, poly in enumerate(me.polygons):
    poly.material_index = face_mats[fi]
    for k, li in enumerate(poly.loop_indices):
        uvl.data[li].uv = face_uvs[fi][k]

ob = bpy.data.objects.new("o", me)
bpy.context.scene.collection.objects.link(ob)
bpy.context.view_layer.objects.active = ob
ob.select_set(True)

# hard edges: the original normals break there, or the faces fold sharply
bm = bmesh.new()
bm.from_mesh(me)
bm.edges.ensure_lookup_table()
bm.faces.ensure_lookup_table()
crease = bm.edges.layers.float.get("crease_edge") or bm.edges.layers.float.new("crease_edge")
corner_n = {}
for fi, f in enumerate(bm.faces):
    for k, lp in enumerate(f.loops):
        corner_n[(fi, lp.vert.index)] = Vector(face_nrms[fi][k])
nhard = 0
for e in bm.edges:
    if len(e.link_faces) != 2:
        e[crease] = 1.0
        e.smooth = False
        continue
    f0, f1 = e.link_faces
    hard = math.degrees(f0.normal.angle(f1.normal, 0.0)) > HARD
    for v in e.verts:
        a, b = corner_n.get((f0.index, v.index)), corner_n.get((f1.index, v.index))
        if a is not None and b is not None and a.length > 0 and b.length > 0 and math.degrees(a.angle(b, 0.0)) > 15.0:
            hard = True
    if hard:
        e[crease] = 1.0
        e.smooth = False
        nhard += 1
bm.to_mesh(me)
bm.free()

mod = ob.modifiers.new("sub", "SUBSURF")
mod.levels = LEVELS
mod.render_levels = LEVELS
mod.use_creases = True
mod.uv_smooth = "PRESERVE_BOUNDARIES"
mod.boundary_smooth = "PRESERVE_CORNERS"
bpy.ops.object.modifier_apply(modifier="sub")

me = ob.data
# smooth shading split at the hard edges (the modifier carries the sharp flags over)
bm = bmesh.new()
bm.from_mesh(me)
bmesh.ops.triangulate(bm, faces=bm.faces[:])
for f in bm.faces:
    f.smooth = True
for e in bm.edges:
    if len(e.link_faces) == 2:
        f0, f1 = e.link_faces
        if math.degrees(f0.normal.angle(f1.normal, 0.0)) > HARD + 10:
            e.smooth = False
bm.to_mesh(me)
bm.free()
me.update()

# weights of the nearest original vertex
kd = KDTree(len(verts))
for i, v in enumerate(verts):
    kd.insert(v, i)
kd.balance()

normals = me.corner_normals
uvd = me.uv_layers.active.data
out = list(head) + ["triangles"]
for poly in me.polygons:
    mat = mats[poly.material_index]
    lis = list(poly.loop_indices)
    out.append(mat)
    for li in (lis[0], lis[1], lis[2]):
        vi = me.loops[li].vertex_index
        co = me.vertices[vi].co
        n = normals[li].vector
        uv = uvd[li].uv
        _, near, _ = kd.find(co)
        links = vlinks[near]
        s = "%d %.6f %.6f %.6f %.6f %.6f %.6f %.6f %.6f %d" % (links[0][0], co.x, co.y, co.z, n.x, n.y, n.z, uv.x, uv.y, len(links))
        s += "".join(" %d %.6f" % ln for ln in links)
        out.append(s)
out.append("end")
open(DST, "w", encoding="ascii", newline="\n").write("\n".join(out) + "\n")
print("SUBDIV %s: %d -> %d triangles, %d hard edges" % (SRC, len(tris), len(me.polygons), nhard))
