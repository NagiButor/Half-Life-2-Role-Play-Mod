"""Run in a separate background Blender; never touches the user's open scene.

blender --background --factory-startup --python inspect_fbx.py -- input.fbx outdir
"""
import json
import sys
from pathlib import Path

import bpy
import numpy as np

src, out = sys.argv[sys.argv.index("--") + 1:]
out = Path(out)
out.mkdir(parents=True, exist_ok=True)
bpy.ops.object.select_all(action="SELECT")
bpy.ops.object.delete(use_global=False)
bpy.ops.import_scene.fbx(filepath=src, use_image_search=False)
report = {"source": src, "unit_scale": bpy.context.scene.unit_settings.scale_length,
          "objects": []}
for i, obj in enumerate(o for o in bpy.context.scene.objects if o.type == "MESH"):
    mesh = obj.data
    mesh.calc_loop_triangles()
    verts = np.empty(len(mesh.vertices) * 3, dtype=np.float32)
    mesh.vertices.foreach_get("co", verts)
    verts = verts.reshape(-1, 3)
    mat = np.array(obj.matrix_world, dtype=np.float64)
    verts = (verts @ mat[:3, :3].T + mat[:3, 3]).astype(np.float32)
    tris = np.empty(len(mesh.loop_triangles) * 3, dtype=np.int32)
    mesh.loop_triangles.foreach_get("vertices", tris)
    tris = tris.reshape(-1, 3)
    np.savez_compressed(out / f"mesh_{i}.npz", vertices=verts, triangles=tris)
    entry = {"index": i, "name": obj.name, "vertices": len(verts), "triangles": len(tris),
             "min": verts.min(axis=0).tolist(), "max": verts.max(axis=0).tolist(),
             "percentiles": {str(p): np.percentile(verts, p, axis=0).tolist()
                             for p in (1, 10, 25, 50, 75, 90, 99)},
             "matrix": mat.tolist(), "materials": len(mesh.materials)}
    report["objects"].append(entry)
    print(json.dumps(entry), flush=True)
(out / "inspection.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
