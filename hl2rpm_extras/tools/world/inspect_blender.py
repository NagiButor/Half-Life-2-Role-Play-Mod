"""Inspect the user's City-27 Blender scene without modifying or saving it.

Run:
  blender -b hl2rpm_map.blend --python inspect_blender.py
The JSON and optional orthographic reference render go to hl2rpm_map_developing.
"""
import bpy
import json
import math
import os
from pathlib import Path
from mathutils import Vector

OUT = Path(r"E:\SourceModding\HL2RPMSOURCE\hl2rpm_map_developing")
OUT.mkdir(parents=True, exist_ok=True)

scene = bpy.context.scene
objects = []
for obj in scene.objects:
    item = {
        "name": obj.name,
        "type": obj.type,
        "location": list(obj.location),
        "rotation_euler": list(obj.rotation_euler),
        "scale": list(obj.scale),
        "dimensions": list(obj.dimensions),
        "hide_render": obj.hide_render,
        "hide_viewport": obj.hide_viewport,
    }
    if obj.type == "MESH":
        corners = [obj.matrix_world @ Vector(corner) for corner in obj.bound_box]
        item["world_bounds"] = {
            "min": [min(v[i] for v in corners) for i in range(3)],
            "max": [max(v[i] for v in corners) for i in range(3)],
        }
        item["vertices"] = len(obj.data.vertices)
        item["polygons"] = len(obj.data.polygons)
        item["materials"] = [m.name if m else None for m in obj.data.materials]
    elif obj.type == "CAMERA":
        item["camera"] = {
            "projection": obj.data.type,
            "ortho_scale": obj.data.ortho_scale,
            "lens": obj.data.lens,
            "clip_start": obj.data.clip_start,
            "clip_end": obj.data.clip_end,
        }
    elif obj.type == "EMPTY" and obj.empty_display_type == "IMAGE":
        item["image"] = obj.data.name if obj.data else None
    objects.append(item)

strings = []
for obj in bpy.data.objects:
    if "city-26" in obj.name.lower() or "city_26" in obj.name.lower() or "city26" in obj.name.lower():
        strings.append({"kind": "object", "name": obj.name})
for block in bpy.data.texts:
    strings.append({"kind": "text", "name": block.name, "content": block.as_string()})

report = {
    "blend": bpy.data.filepath,
    "render": {
        "engine": scene.render.engine,
        "resolution": [scene.render.resolution_x, scene.render.resolution_y],
        "resolution_percentage": scene.render.resolution_percentage,
        "camera": scene.camera.name if scene.camera else None,
    },
    "world_bounds": {
        "min": [min(o["world_bounds"]["min"][i] for o in objects if "world_bounds" in o) for i in range(3)],
        "max": [max(o["world_bounds"]["max"][i] for o in objects if "world_bounds" in o) for i in range(3)],
    },
    "objects": objects,
    "texts_and_city26": strings,
}
(OUT / "city27_blender_scene.json").write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")

# A clean, orthographic top view of the local scans. The large contextual scan
# is hidden only in this unsaved session so that it cannot cover the detailed meshes.
for obj in scene.objects:
    if obj.type == "MESH" and not (
        obj.name.startswith("rpm_c27_")
        or obj.name.startswith("rpm_example_c27_")
        or obj.name in ("rpm_example_outskirts", "Appartments")
    ):
        obj.hide_render = True

local = [o for o in scene.objects if o.type == "MESH" and not o.hide_render]
if local:
    points = [o.matrix_world @ Vector(c) for o in local for c in o.bound_box]
    minx = min(v.x for v in points)
    maxx = max(v.x for v in points)
    miny = min(v.y for v in points)
    maxy = max(v.y for v in points)
    camera_data = bpy.data.cameras.new("AI inspection top camera")
    camera = bpy.data.objects.new("AI inspection top camera", camera_data)
    scene.collection.objects.link(camera)
    camera.location = ((minx + maxx) / 2, (miny + maxy) / 2, max(o["world_bounds"]["max"][2] for o in objects if "world_bounds" in o) + 500)
    camera.rotation_euler = (0, 0, 0)
    camera_data.type = "ORTHO"
    camera_data.ortho_scale = max(maxx - minx, maxy - miny) * 1.06
    camera_data.clip_end = 100000
    scene.camera = camera
    scene.render.engine = "CYCLES"
    scene.cycles.device = "CPU"
    scene.cycles.samples = 12
    scene.cycles.use_denoising = True
    scene.render.resolution_x = 3200
    scene.render.resolution_y = 3200
    scene.render.resolution_percentage = 100
    scene.render.image_settings.file_format = "PNG"
    scene.render.film_transparent = False
    scene.world.color = (0.35, 0.35, 0.35)
    sun_data = bpy.data.lights.new("AI inspection sun", "SUN")
    sun_data.energy = 2.0
    sun_data.angle = math.radians(20)
    sun = bpy.data.objects.new("AI inspection sun", sun_data)
    scene.collection.objects.link(sun)
    sun.rotation_euler = (math.radians(28), math.radians(-24), math.radians(35))
    scene.view_settings.view_transform = "Standard"
    scene.render.filepath = str(OUT / "city27_scan_top.png")
    bpy.ops.render.render(write_still=True)
    projection = {
        "image": "city27_scan_top.png",
        "size": [3200, 3200],
        "center_blender": [camera.location.x, camera.location.y],
        "ortho_scale": camera_data.ortho_scale,
        "blender_bounds": {"min": [minx, miny], "max": [maxx, maxy]},
        "citadel_blender": [14.47, 2.37],
        "world_m_per_blender_unit": 37,
        "pixel_from_blender": "x=(bx-(cx-scale/2))/scale*3200; y=((cy+scale/2)-by)/scale*3200",
    }
    (OUT / "city27_scan_top_projection.json").write_text(json.dumps(projection, ensure_ascii=False, indent=2), encoding="utf-8")

print("WROTE", OUT / "city27_blender_scene.json")
