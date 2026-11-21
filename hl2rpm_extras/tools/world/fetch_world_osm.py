"""Download real OpenStreetMap geometry for the City-27 planning atlas.

Data is reference material only. The atlas deliberately compresses and rearranges
these real districts; it is not a geographic projection of Budapest.
"""
from concurrent.futures import ThreadPoolExecutor
import json
from pathlib import Path
import time
import requests

ROOT = Path(r"E:\SourceModding\HL2RPMSOURCE\hl2rpm_map_developing")
OUT = ROOT / "osm_world"
OUT.mkdir(parents=True, exist_ok=True)

# south, west, north, east; selected around the coordinates supplied by the user.
BBOXES = {
    "station": (47.502, 19.046, 47.517, 19.068),
    "centre": (47.494, 19.039, 47.506, 19.057),
    "plaza": (47.508, 19.068, 47.520, 19.091),
    "apartments": (47.511, 19.044, 47.529, 19.065),
    "industrial": (47.553, 19.025, 47.575, 19.050),
    "canals": (47.521, 19.089, 47.542, 19.112),
    "outskirts": (47.529, 19.161, 47.549, 19.187),
    "road": (47.538, 19.165, 47.557, 19.193),
    "quarantine": (47.489, 19.029, 47.506, 19.048),
    "kobanya": (47.477, 19.123, 47.494, 19.151),
    "m0": (47.352, 19.150, 47.378, 19.195),
    "fields": (47.480, 19.360, 47.503, 19.390),
    "north_hills": (47.458, 19.449, 47.489, 19.501),
    "airbase": (47.420, 19.235, 47.447, 19.280),
    "dunes": (46.880, 19.423, 46.905, 19.467),
    "danube_flats": (47.416, 19.046, 47.446, 19.085),
    "budahills": (47.501, 18.941, 47.534, 18.979),
    "gellert": (47.480, 19.035, 47.497, 19.058),
}

SELECTORS = [
    'way["building"]', 'way["highway"]', 'way["railway"]',
    'way["waterway"]', 'way["natural"]', 'way["landuse"]',
    'way["leisure"]', 'way["aeroway"]', 'way["man_made"]',
    'relation["building"]', 'relation["natural"="water"]', 'relation["landuse"="forest"]',
]
QUERY_VERSION = 2
ENDPOINTS = [
    "https://overpass-api.de/api/interpreter",
    "https://overpass.kumi.systems/api/interpreter",
]


def fetch(item):
    name, bbox = item
    path = OUT / f"{name}.json"
    meta_path = OUT / f"{name}.meta.json"
    if path.exists() and path.stat().st_size > 1000 and meta_path.exists():
        try:
            if json.loads(meta_path.read_text(encoding="utf-8")).get("query_version") == QUERY_VERSION:
                return name, "cached", path.stat().st_size
        except (OSError, ValueError):
            pass
    query = "[out:json][timeout:90];(" + "".join(
        f'{selector}({bbox[0]},{bbox[1]},{bbox[2]},{bbox[3]});'
        for selector in SELECTORS
    ) + ");out geom;"
    last = None
    for attempt in range(4):
        endpoint = ENDPOINTS[attempt % len(ENDPOINTS)]
        try:
            response = requests.post(
                endpoint,
                data={"data": query},
                headers={"User-Agent": "HL2RPM-City27-reference/1.0 (local game map planning)"},
                timeout=120,
            )
            response.raise_for_status()
            data = response.json()
            if "elements" not in data:
                raise ValueError("Overpass response has no elements")
            path.write_text(json.dumps(data, ensure_ascii=False, separators=(",", ":")), encoding="utf-8")
            meta = {
                "source": "OpenStreetMap / Overpass API",
                "copyright": "© OpenStreetMap contributors, ODbL 1.0",
                "bbox_south_west_north_east": bbox,
                "endpoint": endpoint,
                "query_version": QUERY_VERSION,
                "elements": len(data["elements"]),
            }
            meta_path.write_text(json.dumps(meta, ensure_ascii=False, indent=2), encoding="utf-8")
            return name, len(data["elements"]), path.stat().st_size
        except Exception as exc:
            last = f"{type(exc).__name__}: {exc}"
            if attempt < 3:
                time.sleep(4 * (attempt + 1))
    return name, "ERROR", last


if __name__ == "__main__":
    # Two workers keep this modest research job from hammering the public service.
    with ThreadPoolExecutor(max_workers=2) as pool:
        for result in pool.map(fetch, BBOXES.items()):
            print(*result, flush=True)
