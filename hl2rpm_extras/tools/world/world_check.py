"""Validate the City-27 world layout and print derived numbers.

usage:  python world_check.py [world_layout.json]

Checks: unique ids, every map fits one Source map (<= map_limit_m on each side),
maps do not overlap, every transition joins two existing maps and lies on (or near)
both of them, every map has at least one transition, all maps are reachable.
Prints: map sizes in m and Hammer units, distance and elevation angle to the Citadel
top for each compared height, real vs game distances for the compression pairs.
Exit code 0 = no errors.
"""
import json
import math
import os
import sys


def haversine_m(a, b):
    r = 6371000.0
    la1, lo1, la2, lo2 = map(math.radians, (a["lat"], a["lon"], b["lat"], b["lon"]))
    h = math.sin((la2 - la1) / 2) ** 2 + math.cos(la1) * math.cos(la2) * math.sin((lo2 - lo1) / 2) ** 2
    return 2 * r * math.asin(math.sqrt(h))


def centre(rect):
    return ((rect[0] + rect[2]) / 2.0, (rect[1] + rect[3]) / 2.0)


def dist_to_rect(p, rect):
    dx = max(rect[0] - p[0], 0, p[0] - rect[2])
    dy = max(rect[1] - p[1], 0, p[1] - rect[3])
    return math.hypot(dx, dy)


def main():
    path = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(os.path.abspath(__file__)), "world_layout.json")
    w = json.load(open(path, encoding="utf-8"))
    errors = []
    limit = w["frame"]["map_limit_m"]
    hu = w["frame"]["hu_per_m"]
    maps = {m["id"]: m for m in w["maps"]}
    if len(maps) != len(w["maps"]):
        errors.append("duplicate map ids")
    orders = sorted(m.get("order") for m in w["maps"])
    if orders != list(range(1, len(w["maps"]) + 1)):
        errors.append(f"map orders must be unique 1..{len(w['maps'])}: {orders}")

    print(f"{'map':14} {'bsp':18} {'kind':6} {'w x h, m':>12} {'w x h, HU':>15}")
    for m in w["maps"]:
        x0, y0, x1, y1 = m["rect"]
        sw, sh = x1 - x0, y1 - y0
        if sw <= 0 or sh <= 0:
            errors.append(f"{m['id']}: empty rect")
        if sw > limit or sh > limit:
            errors.append(f"{m['id']}: {sw}x{sh} m is larger than one map ({limit} m)")
        safe_limit = w["frame"].get("safe_playable_limit_m", limit)
        if sw > safe_limit or sh > safe_limit:
            errors.append(f"{m['id']}: {sw}x{sh} m leaves no safe Source margin ({safe_limit} m playable)")
        if len(m["bsp"]) > 31:
            errors.append(f"{m['id']}: bsp name longer than 31 chars")
        print(f"{m['id']:14} {m['bsp']:18} {m['kind']:6} {sw:5.0f} x {sh:4.0f} {sw*hu:7.0f} x {sh*hu:6.0f}")

    ids = list(maps)
    for i, a in enumerate(ids):
        for b in ids[i + 1:]:
            ra, rb = maps[a]["rect"], maps[b]["rect"]
            ox = min(ra[2], rb[2]) - max(ra[0], rb[0])
            oy = min(ra[3], rb[3]) - max(ra[1], rb[1])
            if ox > 0.5 and oy > 0.5:
                errors.append(f"maps overlap: {a} / {b} ({ox:.0f} x {oy:.0f} m)")

    links = {k: set() for k in ids}
    tids = set()
    for t in w["transitions"]:
        if t["id"] in tids:
            errors.append(f"duplicate transition id {t['id']}")
        tids.add(t["id"])
        if t["type"] not in w["transition_types"]:
            errors.append(f"{t['id']}: unknown type {t['type']}")
        for side in ("a", "b"):
            if t[side] not in maps:
                errors.append(f"{t['id']}: unknown map {t[side]}")
        if t["a"] in maps and t["b"] in maps:
            links[t["a"]].add(t["b"])
            links[t["b"]].add(t["a"])
            for side in ("a", "b"):
                d = dist_to_rect(t["at"], maps[t[side]]["rect"])
                if d > 0.5:
                    errors.append(f"{t['id']}: point {t['at']} is {d:.0f} m outside {t[side]}")
    for k, v in links.items():
        if not v:
            errors.append(f"{k}: no transitions")
    seen, todo = set(), [ids[0]]
    while todo:
        k = todo.pop()
        if k in seen:
            continue
        seen.add(k)
        todo.extend(links[k] - seen)
    if len(seen) != len(ids):
        errors.append("unreachable maps: " + ", ".join(sorted(set(ids) - seen)))

    c = w["citadel"]
    nexus = next((z for z in w.get("zones", []) if z.get("id") == "nexus"), None)
    apron = nexus.get("citadel_apron") if nexus else None
    if not apron:
        errors.append("nexus has no citadel_apron containing the axis")
    elif math.hypot(apron["center"][0]-c["x"], apron["center"][1]-c["y"]) > apron["radius_m"]:
        errors.append("Citadel axis lies outside the protected apron")
    heights = c["compare_heights_m"]
    print()
    print(f"{'map':14} {'dist, m':>8} " + " ".join(f"{'top@' + str(h) + 'm':>9}" for h in heights))
    for m in sorted(w["maps"], key=lambda m: math.hypot(*(p - q for p, q in zip(centre(m["rect"]), (c["x"], c["y"]))))):
        d = max(math.hypot(centre(m["rect"])[0] - c["x"], centre(m["rect"])[1] - c["y"]), 1.0)
        print(f"{m['id']:14} {d:8.0f} " + " ".join(f"{math.degrees(math.atan2(h, d)):8.1f}°" for h in heights))

    print()
    print(f"{'pair':28} {'real, km':>9} {'game, m':>8} {'x':>6}")
    for a, b in w["compression_pairs"]:
        real = haversine_m(maps[a]["real"], maps[b]["real"])
        ca, cb = centre(maps[a]["rect"]), centre(maps[b]["rect"])
        game = math.hypot(ca[0] - cb[0], ca[1] - cb[1])
        print(f"{a + ' - ' + b:28} {real/1000:9.2f} {game:8.0f} {real/game:6.1f}")

    xs = [v for m in w["maps"] for v in (m["rect"][0], m["rect"][2])]
    ys = [v for m in w["maps"] for v in (m["rect"][1], m["rect"][3])]
    print()
    print(f"maps: {len(ids)}, transitions: {len(w['transitions'])}, world: {max(xs)-min(xs):.0f} x {max(ys)-min(ys):.0f} m")
    for e in errors:
        print("ERROR:", e)
    print("OK" if not errors else f"{len(errors)} error(s)")
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main())
