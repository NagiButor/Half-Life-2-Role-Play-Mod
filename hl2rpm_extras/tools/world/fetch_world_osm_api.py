"""Fallback OSM map-API loader for zones where public Overpass is saturated.

Downloads small bbox tiles, then normalises XML to the same way/relation JSON
shape that render_world_atlas.py consumes. Existing zone JSON is preserved.
"""
from concurrent.futures import ThreadPoolExecutor
import json
from pathlib import Path
import time
from xml.etree import ElementTree as ET
import requests

from fetch_world_osm import BBOXES, OUT
TILES = OUT / "api_tiles"
TILES.mkdir(parents=True, exist_ok=True)
DEFAULT_ZONES = ["dunes", "danube_flats", "budahills", "gellert"]


def fetch_tile(item):
    zone, i, j, bbox = item
    path = TILES / f"{zone}_{i}_{j}.osm"
    bbox_path = path.with_suffix(".bbox.json")
    if path.exists() and path.stat().st_size > 1000 and bbox_path.exists():
        try:
            if json.loads(bbox_path.read_text(encoding="utf-8"))["bbox"] == list(bbox):
                return path
        except (OSError, ValueError, KeyError):
            pass
    south, west, north, east = bbox
    url = "https://api.openstreetmap.org/api/0.6/map"
    last = None
    for attempt in range(4):
        try:
            r = requests.get(url, params={"bbox": f"{west},{south},{east},{north}"},
                             headers={"User-Agent": "HL2RPM-City27-reference/1.0 (local game planning)"}, timeout=100)
            r.raise_for_status()
            if not r.content.startswith(b"<?xml"):
                raise ValueError("OSM API did not return XML")
            path.write_bytes(r.content)
            bbox_path.write_text(json.dumps({"bbox": list(bbox)}), encoding="utf-8")
            return path
        except Exception as exc:
            last = exc
            time.sleep(3*(attempt+1))
    raise RuntimeError(f"{zone} tile {i}/{j}: {last}")


def normalise(zone, paths):
    nodes, ways, relations = {}, {}, {}
    for path in paths:
        for _, e in ET.iterparse(path, events=("end",)):
            if e.tag == "node":
                nodes[e.attrib["id"]] = {"lat": float(e.attrib["lat"]), "lon": float(e.attrib["lon"])}
            elif e.tag == "way":
                ways[e.attrib["id"]] = {
                    "refs": [n.attrib["ref"] for n in e.findall("nd")],
                    "tags": {t.attrib["k"]: t.attrib["v"] for t in e.findall("tag")},
                }
            elif e.tag == "relation":
                relations[e.attrib["id"]] = {
                    "members": [dict(m.attrib) for m in e.findall("member")],
                    "tags": {t.attrib["k"]: t.attrib["v"] for t in e.findall("tag")},
                }
            if e.tag in ("node", "way", "relation"):
                e.clear()
    elements = []
    for ident, way in ways.items():
        elements.append({"type": "way", "id": int(ident), "tags": way["tags"],
                         "geometry": [nodes[r] for r in way["refs"] if r in nodes]})
    for ident, rel in relations.items():
        members = []
        for m in rel["members"]:
            if m["type"] != "way" or m["ref"] not in ways:
                continue
            way = ways[m["ref"]]
            members.append({"type": "way", "ref": int(m["ref"]), "role": m.get("role", "outer"),
                            "geometry": [nodes[r] for r in way["refs"] if r in nodes]})
        elements.append({"type": "relation", "id": int(ident), "tags": rel["tags"], "members": members})
    (OUT / f"{zone}.json").write_text(json.dumps({"elements": elements}, ensure_ascii=False, separators=(",", ":")), encoding="utf-8")
    meta = {"source": "OpenStreetMap map API 0.6", "copyright": "© OpenStreetMap contributors, ODbL 1.0",
            "bbox_south_west_north_east": BBOXES[zone], "tiles": len(paths), "elements": len(elements), "query_version": 2}
    (OUT / f"{zone}.meta.json").write_text(json.dumps(meta, ensure_ascii=False, indent=2), encoding="utf-8")
    return zone, len(elements), len(ways), len(relations)


def run(zones=DEFAULT_ZONES):
    jobs = []
    for zone in zones:
        south, west, north, east = BBOXES[zone]
        dy, dx = 0.015, 0.020
        ny = int((north-south)/dy+0.999999)
        nx = int((east-west)/dx+0.999999)
        for i in range(ny):
            for j in range(nx):
                bb = (south+i*dy, west+j*dx, min(north, south+(i+1)*dy), min(east, west+(j+1)*dx))
                jobs.append((zone, i, j, bb))
    with ThreadPoolExecutor(max_workers=3) as pool:
        for path in pool.map(fetch_tile, jobs):
            print("TILE", path.name, path.stat().st_size, flush=True)
    for zone in zones:
        zone_jobs = [job for job in jobs if job[0] == zone]
        zone_paths = [TILES / f"{z}_{i}_{j}.osm" for z, i, j, _ in zone_jobs]
        print("ZONE", *normalise(zone, zone_paths), flush=True)


if __name__ == "__main__":
    run()
