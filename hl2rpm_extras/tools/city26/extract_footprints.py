"""Reconstruct simplified, closed building footprints from orthographic scan heights.

The scan is a reference only: small clutter is removed, contours simplified, height
quantized, and the resulting solids are freshly constructed convex brushes.
"""
import argparse
import json
from pathlib import Path

import cv2
import numpy as np
from PIL import Image, ImageDraw
from scipy import ndimage as ndi
from shapely import Polygon, make_valid

p = argparse.ArgumentParser()
p.add_argument("directory", type=Path)
args = p.parse_args()
out = args.directory
data = np.load(out / "heightfields.npz")
depth, lo, pixel = data["depth"], data["lo"], float(data["pixel"])
buildings = []
masks = []
for zone, z in enumerate(depth):
    valid = z > -99
    _, indices = ndi.distance_transform_edt(~valid, return_indices=True)
    filled = z[tuple(indices)]
    # Eroded local surface estimates street elevation even in a dense urban block.
    ground = ndi.percentile_filter(filled, 15, size=65)
    ground = ndi.gaussian_filter(ground, 14)
    rel = filled - ground
    mask = ((rel > 0.145) & valid).astype(np.uint8)
    mask = cv2.morphologyEx(mask, cv2.MORPH_CLOSE, np.ones((5, 5), np.uint8))
    mask = cv2.morphologyEx(mask, cv2.MORPH_OPEN, np.ones((4, 4), np.uint8))
    n, labels, stats, centroids = cv2.connectedComponentsWithStats(mask)
    cleaned = np.zeros_like(mask)
    for label in range(1, n):
        component = labels == label
        area = stats[label, cv2.CC_STAT_AREA] * pixel ** 2
        if area < 0.055:
            continue
        # Detached tree crowns are small and lack sizeable, connected roof area.
        if area < 0.13 and stats[label, cv2.CC_STAT_WIDTH] < 22 and stats[label, cv2.CC_STAT_HEIGHT] < 22:
            continue
        cleaned[component] = 255
    masks.append(cleaned)
    contours, hierarchy = cv2.findContours(cleaned, cv2.RETR_CCOMP, cv2.CHAIN_APPROX_SIMPLE)
    if hierarchy is None:
        continue
    for j, c in enumerate(contours):
        if hierarchy[0, j, 3] != -1:
            continue
        def coords(contour):
            pts = cv2.approxPolyDP(contour, 2.0, True).reshape(-1, 2)
            return pts * pixel + lo[:2]
        shell = coords(c)
        holes = []
        child = hierarchy[0, j, 2]
        while child != -1:
            if cv2.contourArea(contours[child]) * pixel ** 2 > 0.025:
                holes.append(coords(contours[child]))
            child = hierarchy[0, child, 0]
        if len(shell) < 3:
            continue
        poly = make_valid(Polygon(shell, holes)).simplify(0.035, preserve_topology=True)
        if poly.geom_type != "Polygon" or poly.area < 0.055:
            continue
        cmask = np.zeros_like(mask)
        cv2.drawContours(cmask, contours, j, 1, thickness=-1)
        sel = cmask.astype(bool) & valid
        top = float(np.percentile(z[sel], 65))
        base = float(np.percentile(ground[sel], 20))
        buildings.append({"zone":zone,"outline":list(poly.exterior.coords)[:-1],
            "holes":[list(r.coords)[:-1] for r in poly.interiors],
            "base":base,"top":top,"height":max(0.18,top-base),"area":poly.area})
    print(f"zone {zone}: {sum(b['zone']==zone for b in buildings)} footprints", flush=True)
(out / "footprints.json").write_text(json.dumps(buildings, indent=2))
rgb = np.zeros((*depth.shape[1:], 3), np.uint8) + 26
rgb[depth.max(0) > -99] = [52, 56, 60]
for i, mask in enumerate(masks):
    rgb[mask > 0] = [(205, 120, 90), (100, 180, 130), (110, 145, 220)][i]
im = Image.fromarray(rgb[::-1])
d = ImageDraw.Draw(im)
for i,b in enumerate(buildings):
    xy = np.array(b["outline"])
    coords = [(int((x-lo[0])/pixel), im.height-1-int((y-lo[1])/pixel)) for x,y in xy]
    d.line(coords + [coords[0]], fill="white", width=1)
    center = xy.mean(0)
    d.text((int((center[0]-lo[0])/pixel),im.height-1-int((center[1]-lo[1])/pixel)), str(i), fill="yellow")
im.save(out / "footprints.png")
