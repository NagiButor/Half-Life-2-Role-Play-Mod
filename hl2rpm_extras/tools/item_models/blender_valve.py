# -*- coding: utf-8 -*-
# HL2RPM: the red valve wheel of the loading screen (make_loading_valve.py runs this):
#   blender --background --factory-startup --python blender_valve.py -- <out dir> <frames> <size>
# A cast hand wheel on a valve stem, seen a little from the side (the user's reference: the wheel
# of Valve's logo). Frames: a sixth of a turn (six lobes - the loop is seamless), PNG with alpha.
import math
import os
import sys

import bmesh
import bpy
from mathutils import Matrix, Vector

argv = sys.argv[sys.argv.index("--") + 1:]
OUT, NFRAMES, SIZE = argv[0], int(argv[1]), int(argv[2])
os.makedirs(OUT, exist_ok=True)

bpy.ops.wm.read_factory_settings(use_empty=True)
scene = bpy.context.scene


def link(obj):
    scene.collection.objects.link(obj)
    return obj


def mesh_obj(name, bm):
    me = bpy.data.meshes.new(name)
    bm.to_mesh(me)
    bm.free()
    for p in me.polygons:
        p.use_smooth = True
    return link(bpy.data.objects.new(name, me))


def ring_tube(name, samples, center_r, tube_r, segs=24, z=0.0):
    """a closed tube around the Z axis: center_r(t), tube_r(t) for t in 0..1 around"""
    bm = bmesh.new()
    rings = []
    for i in range(samples):
        t = i / samples
        th = 2 * math.pi * t
        radial = Vector((math.cos(th), math.sin(th), 0))
        c = radial * center_r(t) + Vector((0, 0, z))
        r = tube_r(t)
        rings.append([bm.verts.new(c + (radial * math.cos(a) + Vector((0, 0, 1)) * math.sin(a)) * r)
                      for a in (2 * math.pi * k / segs for k in range(segs))])
    for i in range(samples):
        a, b = rings[i], rings[(i + 1) % samples]
        for k in range(segs):
            bm.faces.new((a[k], a[(k + 1) % segs], b[(k + 1) % segs], b[k]))
    return mesh_obj(name, bm)


def path_tube(name, pts, radii, segs=16):
    """an open tube along a polyline (its ends are buried in the hub and the rim)"""
    bm = bmesh.new()
    rings = []
    n = len(pts)
    prev_u = None
    for i, p in enumerate(pts):
        t = (pts[min(i + 1, n - 1)] - pts[max(i - 1, 0)]).normalized()
        up = Vector((0, 0, 1)) if abs(t.z) < 0.9 else Vector((1, 0, 0))
        u = t.cross(up).normalized()
        if prev_u is not None and u.dot(prev_u) < 0:
            u = -u
        prev_u = u
        v = t.cross(u).normalized()
        rings.append([bm.verts.new(p + (u * math.cos(a) + v * math.sin(a)) * radii[i])
                      for a in (2 * math.pi * k / segs for k in range(segs))])
    for i in range(n - 1):
        a, b = rings[i], rings[i + 1]
        for k in range(segs):
            bm.faces.new((a[k], a[(k + 1) % segs], b[(k + 1) % segs], b[k]))
    return mesh_obj(name, bm)


def cylinder(name, radius, depth, z, verts=48, bevel=0.02):
    bpy.ops.mesh.primitive_cylinder_add(vertices=verts, radius=radius, depth=depth, location=(0, 0, z))
    o = bpy.context.active_object
    o.name = name
    if bevel > 0:
        m = o.modifiers.new("bevel", "BEVEL")
        m.width = bevel
        m.segments = 3
        m.limit_method = "ANGLE"
    bpy.ops.object.shade_smooth()
    return o


def subsurf(o, lv=1):
    m = o.modifiers.new("sub", "SUBSURF")
    m.levels = lv
    m.render_levels = lv
    return o


# ------------------------------------------------------------------ materials
def paint_material():
    m = bpy.data.materials.new("red_paint")
    m.use_nodes = True
    nt = m.node_tree
    p = nt.nodes["Principled BSDF"]
    tc = nt.nodes.new("ShaderNodeTexCoord")
    noise = nt.nodes.new("ShaderNodeTexNoise")
    noise.inputs["Scale"].default_value = 9.0
    noise.inputs["Detail"].default_value = 8.0
    nt.links.new(tc.outputs["Object"], noise.inputs["Vector"])
    ramp = nt.nodes.new("ShaderNodeValToRGB")
    ramp.color_ramp.elements[0].position = 0.35
    ramp.color_ramp.elements[0].color = (0.30, 0.012, 0.010, 1)
    ramp.color_ramp.elements[1].position = 0.75
    ramp.color_ramp.elements[1].color = (0.62, 0.035, 0.022, 1)
    nt.links.new(noise.outputs["Fac"], ramp.inputs["Fac"])
    nt.links.new(ramp.outputs["Color"], p.inputs["Base Color"])
    p.inputs["Roughness"].default_value = 0.38
    p.inputs["Metallic"].default_value = 0.0
    try:
        p.inputs["Coat Weight"].default_value = 0.35
        p.inputs["Coat Roughness"].default_value = 0.2
    except KeyError:
        pass
    bump = nt.nodes.new("ShaderNodeBump")
    bump.inputs["Strength"].default_value = 0.12
    nt.links.new(noise.outputs["Fac"], bump.inputs["Height"])
    nt.links.new(bump.outputs["Normal"], p.inputs["Normal"])
    return m


def metal_material(name, color, rough):
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    p = m.node_tree.nodes["Principled BSDF"]
    p.inputs["Base Color"].default_value = color
    p.inputs["Roughness"].default_value = rough
    p.inputs["Metallic"].default_value = 1.0
    return m


RED = paint_material()
STEEL = metal_material("steel", (0.55, 0.56, 0.58, 1), 0.32)
CAST = metal_material("cast", (0.16, 0.165, 0.175, 1), 0.62)

# ------------------------------------------------------------------ the wheel (axis Z, front +Z)
# A cast gate-valve handle as in the user's reference (the wheel of Valve's logo): no round rim -
# straight bars between round lobes at the corners of a hexagon, a spoke from the hub to each
# lobe, a square nut turned as a diamond in the middle. Its six corners repeat every sixth of a
# turn: the frames cover 60 degrees.
LOBES = 6
RV = 0.92            # the corners
DISH = 0.16          # the hub stands out of the rim's plane, towards the viewer
wheel = link(bpy.data.objects.new("wheel", None))
parts = []
corners = [Vector((math.cos(2 * math.pi * k / LOBES), math.sin(2 * math.pi * k / LOBES), 0)) * RV for k in range(LOBES)]


def sphere(name, loc, radius, scale=(1, 1, 1), rot_z=0.0):
    bpy.ops.mesh.primitive_uv_sphere_add(segments=32, ring_count=16, radius=radius, location=loc)
    o = bpy.context.active_object
    o.name = name
    o.scale = scale
    o.rotation_euler = (0, 0, rot_z)
    bpy.ops.object.shade_smooth()
    return o


for k in range(LOBES):
    a, b = corners[k], corners[(k + 1) % LOBES]
    # the bar between two lobes: thick, a little thinner in its middle, bowed out a touch
    pts, radii = [], []
    for i in range(17):
        s_ = i / 16
        mid = 4 * s_ * (1 - s_)
        p = a.lerp(b, s_)
        p = p + p.normalized() * 0.035 * mid
        pts.append(p)
        radii.append(0.142 - 0.024 * mid)
    parts.append(path_tube("bar%d" % k, pts, radii, segs=20))
    # the lobe: a knob longer along the rim than across it
    th = 2 * math.pi * k / LOBES
    parts.append(sphere("lobe%d" % k, corners[k] * 1.04, 0.225, scale=(1.0, 1.2, 0.95), rot_z=th))
    # the spoke from the hub to the lobe, dipping from the hub down to the rim's plane
    pts, radii = [], []
    for i in range(17):
        s_ = i / 16
        r = 0.18 + (RV - 0.18) * s_
        pts.append(Vector((math.cos(th) * r, math.sin(th) * r, DISH * (1 - s_) ** 1.4)))
        radii.append(0.122 - 0.022 * math.sin(math.pi * s_))
    parts.append(path_tube("spoke%d" % k, pts, radii, segs=20))
hub = cylinder("hub", 0.30, 0.30, DISH, bevel=0.05)
parts.append(hub)
for o in parts:
    o.data.materials.append(RED)
    if o.name.startswith(("bar", "spoke")):
        subsurf(o, 1)
NUT = metal_material("nut", (0.05, 0.11, 0.08, 1), 0.35)     # the dark green square nut
nut = cylinder("nut", 0.20, 0.12, DISH + 0.19, verts=4, bevel=0.015)
nut.rotation_euler = (0, 0, 0)      # (4 vertices on the axes: a diamond)
nut.data.materials.append(NUT)
stud = cylinder("stud", 0.05, 0.1, DISH + 0.27, verts=24, bevel=0.01)
stud.data.materials.append(STEEL)
for o in parts + [nut, stud]:
    o.parent = wheel
# the stem, the square bonnet and the round plate on the wall behind (they do not turn)
stem = cylinder("stem", 0.08, 1.2, DISH - 0.6, verts=24, bevel=0.01)
stem.data.materials.append(STEEL)
bonnet = cylinder("bonnet", 0.30, 0.55, -0.95, verts=4, bevel=0.04)
bonnet.rotation_euler = (0, 0, math.pi / 4)
bonnet.data.materials.append(CAST)
flange = cylinder("flange", 0.62, 0.14, -1.3, bevel=0.03)
flange.data.materials.append(CAST)

# ------------------------------------------------------------------ camera, lights
cam_data = bpy.data.cameras.new("cam")
cam_data.lens = 85
cam = link(bpy.data.objects.new("cam", cam_data))
d = Vector((0.85, 0.14, 1.0)).normalized()
cam.location = d * 7.2
# look at the wheel with the world's +Y up on the screen: the wheel's axis lies across the picture
_f = -d
_r = _f.cross(Vector((0, 1, 0))).normalized()
_u = _r.cross(_f)
cam.matrix_world = Matrix(((_r.x, _u.x, -_f.x, cam.location.x), (_r.y, _u.y, -_f.y, cam.location.y),
                           (_r.z, _u.z, -_f.z, cam.location.z), (0, 0, 0, 1)))
scene.camera = cam


def area(name, loc, energy, size, color=(1, 1, 1)):
    ld = bpy.data.lights.new(name, "AREA")
    ld.energy = energy
    ld.size = size
    ld.color = color
    o = link(bpy.data.objects.new(name, ld))
    o.location = loc
    o.rotation_euler = (-Vector(loc)).to_track_quat("-Z", "Y").to_euler()
    return o


area("key", (-3.5, 2.5, 5.0), 900, 3.0, (1.0, 0.96, 0.9))
area("fill", (4.0, -1.0, 3.0), 260, 4.0, (0.85, 0.9, 1.0))
area("rim", (1.5, 4.0, -2.5), 500, 2.0)
world = bpy.data.worlds.new("w")
world.use_nodes = True
world.node_tree.nodes["Background"].inputs["Color"].default_value = (0.05, 0.05, 0.055, 1)
world.node_tree.nodes["Background"].inputs["Strength"].default_value = 0.6
scene.world = world

r = scene.render
r.engine = "CYCLES"
scene.cycles.device = "CPU"
scene.cycles.samples = 64
scene.cycles.use_denoising = True
r.film_transparent = True
r.resolution_x = r.resolution_y = SIZE
r.resolution_percentage = 100
r.image_settings.file_format = "PNG"
r.image_settings.color_mode = "RGBA"
try:
    scene.view_settings.view_transform = "Standard"
except TypeError:
    pass

for f in range(NFRAMES):
    wheel.rotation_euler = (0, 0, -2 * math.pi / LOBES * f / NFRAMES)      # clockwise, seen from the front
    r.filepath = os.path.join(OUT, "valve_%02d.png" % f)
    bpy.ops.render.render(write_still=True)
print("valve frames:", NFRAMES)
