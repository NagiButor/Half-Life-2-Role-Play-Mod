"""Read-only verification of the generated City-27 3D masterplan.
Run: blender -b city27_masterplan.blend --python verify_source_3d.py
"""
import bpy
import json
from pathlib import Path
from mathutils import Vector

OUT = Path(r"E:\SourceModding\HL2RPMSOURCE\hl2rpm_map_developing\source_3d")
spec = json.loads((OUT / "city27_hammer_layout.json").read_text(encoding="utf-8"))
report = {"blend": bpy.data.filepath, "maps": [], "errors": [], "warnings": []}
for m in spec["maps"]:
    prefix = f"{m['order']:02d}_{m['bsp']}__"
    collections = [c for c in bpy.data.collections if c.name.startswith(prefix)]
    if len(collections) != 1:
        report["errors"].append(f"{m['bsp']}: expected one collection, got {len(collections)}")
        continue
    points = []
    for obj in collections[0].objects:
        if obj.type != "MESH":
            continue
        points.extend(obj.matrix_world @ Vector(p) for p in obj.bound_box)
    rect = m["rect"]
    actual = None
    if points:
        actual = {"min": [min(p[i] for p in points) for i in range(3)],
                  "max": [max(p[i] for p in points) for i in range(3)]}
        overshoot = [max(rect[0]-actual["min"][0], actual["max"][0]-rect[2], 0),
                     max(rect[1]-actual["min"][1], actual["max"][1]-rect[3], 0)]
        if max(overshoot) > 12:
            report["errors"].append(f"{m['bsp']}: geometry exceeds cell + Source margin by {overshoot} m")
        elif max(overshoot) > 0.1:
            report["warnings"].append(f"{m['bsp']}: transition/detail geometry outside playable edge by {overshoot} m")
    width, height = m["working_size_m"]
    if width > 600 or height > 600:
        report["errors"].append(f"{m['bsp']}: unsafe playable size {width} x {height}")
    report["maps"].append({"bsp": m["bsp"], "objects_before_merge": len(collections[0].objects),
                           "geometry_bounds_m": actual,
                           "working_size_hu": m["working_size_hu"],
                           "source_footprints": m["source_building_footprints"],
                           "selected_volumes": m["masterplan_building_volumes"]})
for t in spec["transitions"]:
    for e in t["ends"]:
        if any(abs(v) > 16384 for v in e["entry_local_hu"]):
            report["errors"].append(f"{t['id']}/{e['bsp']}: transition outside Source")
report["collections"] = len(bpy.data.collections)
report["objects"] = len(bpy.data.objects)
report["mesh_polygons"] = sum(len(o.data.polygons) for o in bpy.data.objects if o.type == "MESH")
report["mesh_vertices"] = sum(len(o.data.vertices) for o in bpy.data.objects if o.type == "MESH")
(OUT / "city27_masterplan_verification.json").write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")
print(json.dumps({k: report[k] for k in ("collections", "objects", "mesh_polygons", "mesh_vertices", "errors", "warnings")}, ensure_ascii=False, indent=2))
if report["errors"]:
    raise RuntimeError("3D masterplan verification failed")
