# -*- coding: utf-8 -*-
# HL2RPM: a quick look at a model made by a script (make_suit_piles.py --preview):
#   blender --background --factory-startup --python blender_preview.py -- <parts.npz> <out.png>
# parts.npz: pos<i>, nrm<i>, uv<i>, idx<i> and "mats" (a PNG per part, "" = grey). Two views side by side.
import math
import sys

import bpy
import numpy as np
from mathutils import Matrix, Vector

argv = sys.argv[sys.argv.index("--") + 1:]
NPZ, OUT = argv[0], argv[1]
d = np.load(NPZ, allow_pickle=True)
mats = list(d["mats"])
bpy.ops.wm.read_factory_settings(use_empty=True)
scene = bpy.context.scene
cache = {}


def material(png):
    if png in cache:
        return cache[png]
    m = bpy.data.materials.new("m%d" % len(cache))
    m.use_nodes = True
    p = m.node_tree.nodes["Principled BSDF"]
    p.inputs["Roughness"].default_value = 0.6
    if png:
        t = m.node_tree.nodes.new("ShaderNodeTexImage")
        t.image = bpy.data.images.load(png)
        m.node_tree.links.new(t.outputs["Color"], p.inputs["Base Color"])
    m.use_backface_culling = False
    cache[png] = m
    return m


lo, hi = np.full(3, 1e9), np.full(3, -1e9)
for i, png in enumerate(mats):
    pos, nrm, uv, idx = d["pos%d" % i], d["nrm%d" % i], d["uv%d" % i], d["idx%d" % i]
    lo, hi = np.minimum(lo, pos.min(0)), np.maximum(hi, pos.max(0))
    me = bpy.data.meshes.new("p%d" % i)
    tris = idx.reshape(-1, 3).tolist()
    me.from_pydata(pos.tolist(), [], tris)
    lay = me.uv_layers.new()
    loops = idx.reshape(-1)
    uvs = uv[loops]
    lay.data.foreach_set("uv", np.stack([uvs[:, 0], 1 - uvs[:, 1]], 1).reshape(-1).tolist())
    me.normals_split_custom_set([tuple(nrm[v]) for v in loops])
    me.materials.append(material(png))
    o = bpy.data.objects.new("p%d" % i, me)
    scene.collection.objects.link(o)
# a floor
bpy.ops.mesh.primitive_plane_add(size=400, location=(0, 0, lo[2] - 0.02))
fl = bpy.context.active_object
fm = bpy.data.materials.new("floor")
fm.use_nodes = True
fm.node_tree.nodes["Principled BSDF"].inputs["Base Color"].default_value = (0.18, 0.18, 0.17, 1)
fl.data.materials.append(fm)

c = (lo + hi) / 2
size = float(np.max(hi - lo))
cam = bpy.data.objects.new("cam", bpy.data.cameras.new("cam"))
scene.collection.objects.link(cam)
cam.data.lens = 50
scene.camera = cam
sun = bpy.data.objects.new("sun", bpy.data.lights.new("sun", "SUN"))
sun.data.energy = 3.5
sun.rotation_euler = (math.radians(40), math.radians(15), math.radians(30))
scene.collection.objects.link(sun)
world = bpy.data.worlds.new("w")
world.use_nodes = True
world.node_tree.nodes["Background"].inputs["Color"].default_value = (0.5, 0.55, 0.6, 1)
world.node_tree.nodes["Background"].inputs["Strength"].default_value = 0.6
scene.world = world
r = scene.render
r.engine = "BLENDER_EEVEE" if "BLENDER_EEVEE" in [e.identifier for e in bpy.types.RenderSettings.bl_rna.properties["engine"].enum_items] else "CYCLES"
if r.engine == "CYCLES":
    scene.cycles.samples = 24
r.resolution_x, r.resolution_y = 900, 600
r.image_settings.file_format = "PNG"


def look(direction, dist):
    dvec = Vector(direction).normalized()
    cam.location = Vector(c) + dvec * dist
    f = -dvec
    rr = f.cross(Vector((0, 0, 1))).normalized()
    u = rr.cross(f)
    cam.matrix_world = Matrix(((rr.x, u.x, -f.x, cam.location.x), (rr.y, u.y, -f.y, cam.location.y), (rr.z, u.z, -f.z, cam.location.z), (0, 0, 0, 1)))


shots = []
for k, dirv in enumerate(((1.0, -1.1, 0.9), (-1.2, 0.6, 0.55))):
    look(dirv, size * 1.55)
    r.filepath = OUT[:-4] + "_%d.png" % k
    bpy.ops.render.render(write_still=True)
    shots.append(r.filepath)
