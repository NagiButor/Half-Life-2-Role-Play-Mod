"""Rasterize reference triangles to inspect coverage and heights, not game geometry."""
import argparse
import json
from pathlib import Path

import cv2
import numpy as np
from PIL import Image, ImageDraw

p = argparse.ArgumentParser()
p.add_argument("directory", type=Path)
p.add_argument("--resolution", type=int, default=1600)
args = p.parse_args()
out = args.directory
meshes = [np.load(f) for f in sorted(out.glob("mesh_*.npz"))]
allv = np.concatenate([m["vertices"] for m in meshes])
lo, hi = allv.min(0), allv.max(0)
res = args.resolution
pixel = max(hi[:2] - lo[:2]) / (res - 1)
shape = tuple((np.ceil((hi[:2] - lo[:2]) / pixel).astype(int) + 1)[::-1])
maps = []
for i, mesh in enumerate(meshes):
    verts, tris = mesh["vertices"], mesh["triangles"]
    depth = np.full(shape, -100, dtype=np.float32)
    xy = (verts[:, :2] - lo[:2]) / pixel
    # Orthographic z buffer, with interpolated triangle heights.
    for tri in tris:
        pts = xy[tri]
        xmin, ymin = np.maximum(np.floor(pts.min(0)).astype(int), 0)
        xmax, ymax = np.minimum(np.ceil(pts.max(0)).astype(int), np.array(shape[::-1]) - 1)
        if xmax < xmin or ymax < ymin:
            continue
        a, b, c = pts
        den = (b[1] - c[1]) * (a[0] - c[0]) + (c[0] - b[0]) * (a[1] - c[1])
        if abs(den) < 1e-8:
            continue
        xx, yy = np.meshgrid(np.arange(xmin, xmax + 1), np.arange(ymin, ymax + 1))
        u = ((b[1] - c[1]) * (xx - c[0]) + (c[0] - b[0]) * (yy - c[1])) / den
        v = ((c[1] - a[1]) * (xx - c[0]) + (a[0] - c[0]) * (yy - c[1])) / den
        inside = (u >= -0.001) & (v >= -0.001) & (u + v <= 1.001)
        z = u * verts[tri[0], 2] + v * verts[tri[1], 2] + (1 - u - v) * verts[tri[2], 2]
        view = depth[ymin:ymax + 1, xmin:xmax + 1]
        np.maximum(view, np.where(inside, z, -100), out=view)
    maps.append(depth)
    valid = depth > -99
    norm = np.clip((depth - lo[2]) / (hi[2] - lo[2]) * 255, 0, 255).astype(np.uint8)
    rgb = cv2.cvtColor(cv2.applyColorMap(norm, cv2.COLORMAP_TURBO), cv2.COLOR_BGR2RGB)
    rgb[~valid] = [22, 25, 30]
    Image.fromarray(rgb[::-1]).save(out / f"height_{i}.png")
    print(f"mesh {i}: covered {valid.sum()} pixels", flush=True)
stack = np.stack(maps)
merged = stack.max(0)
np.savez_compressed(out / "heightfields.npz", depth=stack, lo=lo, hi=hi, pixel=pixel)
norm = np.clip((merged - lo[2]) / (hi[2] - lo[2]) * 255, 0, 255).astype(np.uint8)
rgb = cv2.cvtColor(cv2.applyColorMap(norm, cv2.COLORMAP_TURBO), cv2.COLOR_BGR2RGB)
rgb[merged < -99] = [22, 25, 30]
image = Image.fromarray(rgb[::-1])
d = ImageDraw.Draw(image)
for x in np.arange(np.ceil(lo[0]), hi[0], 1):
    px = int((x - lo[0]) / pixel)
    d.line((px, 0, px, image.height), fill=(70, 70, 70), width=1)
    d.text((px + 2, 3), str(int(x)), fill="white")
for y in np.arange(np.ceil(lo[1]), hi[1], 1):
    py = image.height - 1 - int((y - lo[1]) / pixel)
    d.line((0, py, image.width, py), fill=(70, 70, 70), width=1)
    d.text((2, py + 2), str(int(y)), fill="white")
image.save(out / "reference_plan.png")
(out / "raster.json").write_text(json.dumps({"lo":lo.tolist(),"hi":hi.tolist(),
    "pixel":float(pixel),"shape":[int(s) for s in shape]}, indent=2))
