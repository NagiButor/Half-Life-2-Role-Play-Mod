"""Render the detailed City-27 world atlas from the approved-layout candidate.

Unlike the rejected rectangle-only concept, every district is filled with real
OpenStreetMap building footprints and streets. Districts are independently
compressed into the composition of the user's Blender scans; the result is a
game-world plan, not a claim of continuous real-world geography.
"""
from collections import defaultdict
import html
import json
import math
from pathlib import Path
import sys
import textwrap

from PIL import Image, ImageDraw, ImageFont

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
from fetch_world_osm import BBOXES

ROOT = Path(r"E:\SourceModding\HL2RPMSOURCE\hl2rpm_map_developing")
DATA = ROOT / "osm_world"
DETAILS = ROOT / "city27_zone_details"
DETAILS.mkdir(parents=True, exist_ok=True)
WORLD = json.loads((HERE / "world_layout.json").read_text(encoding="utf-8"))
MAPS = {m["id"]: m for m in WORLD["maps"]}

FONT = r"C:\Windows\Fonts\arial.ttf"
BOLD = r"C:\Windows\Fonts\arialbd.ttf"
PAPER = "#ece9df"
INK = "#183034"
MUTED = "#60716f"
KIND = {
    "city": ("#d6dfda", "#397b79"),
    "belt": ("#e6dbc1", "#aa7b36"),
    "waste": ("#dce0c5", "#71833e"),
}
TCOL = {
    "checkpoint": "#d85832", "gate": "#b27a10", "street": "#596d69",
    "underground": "#7353a6", "bridge": "#267e96", "rail": "#91563f",
    "canal": "#238f98", "wild": "#537b35", "open": "#6b873c",
}


def font(size, bold=False):
    return ImageFont.truetype(BOLD if bold else FONT, size)


def area(elements, pred):
    return [e for e in elements if pred(e.get("tags", {}), e)]


def load_osm(zone):
    path = DATA / f"{zone}.json"
    if not path.exists():
        return []
    return json.loads(path.read_text(encoding="utf-8")).get("elements", [])


def source_xy(lat, lon, rect, bbox):
    """Uniform local metric scale; Blender orientation east up, north left.

    A district is cropped at its outer reference boundary, never stretched to
    make buildings wider or taller just to fill a BSP rectangle.
    """
    x0, y0, x1, y1 = rect
    south, west, north, east = bbox
    midlat, midlon = (south+north)/2, (west+east)/2
    source_w = (north-south)*111300
    source_h = (east-west)*111300*math.cos(math.radians(midlat))
    scale = max((x1-x0)/source_w, (y1-y0)/source_h)
    cx, cy = (x0+x1)/2, (y0+y1)/2
    x = cx+(midlat-lat)*111300*scale
    y = cy+(lon-midlon)*111300*math.cos(math.radians(midlat))*scale
    # PIL has no per-zone clip. Clamping affects only geometry crossing the
    # reference crop; buildings wholly inside retain their true XY proportions.
    return max(x0, min(x1, x)), max(y0, min(y1, y))


def geom(element, rect, bbox):
    return [source_xy(p["lat"], p["lon"], rect, bbox)
            for p in element.get("geometry", []) if "lat" in p and "lon" in p]


def parts(element, role="outer"):
    if element.get("type") == "way":
        return [element] if role == "outer" else []
    if element.get("type") == "relation":
        return [m for m in element.get("members", [])
                if m.get("type") == "way" and m.get("role", "outer") == role and m.get("geometry")]
    return []


def building_tone(tags):
    try:
        levels = float(tags.get("building:levels", "0").split(";")[0])
    except ValueError:
        levels = 0
    if levels >= 7:
        return "#665f5a"
    if levels >= 4:
        return "#81756a"
    if levels > 0:
        return "#9b8d7d"
    return "#8c8174"


def draw_osm(draw, zone, rect, project, detail=False):
    elements = load_osm(zone)
    if not elements or zone not in BBOXES:
        return {"buildings": 0, "roads": 0, "status": "missing"}
    bbox = BBOXES[zone]
    pts = lambda e: [project(p) for p in geom(e, rect, bbox)]

    # Broad surfaces first. The palette stays quiet so individual buildings win.
    for e in elements:
        tags = e.get("tags", {})
        land = tags.get("landuse", "")
        leisure = tags.get("leisure", "")
        natural = tags.get("natural", "")
        color = None
        if land in ("forest", "wood") or natural == "wood":
            color = "#afbea0"
        elif land in ("grass", "meadow", "farmland", "farmyard", "orchard"):
            color = "#c8d1aa"
        elif leisure in ("park", "garden", "pitch", "playground", "golf_course"):
            color = "#bccca9"
        elif land in ("industrial", "railway", "commercial"):
            color = "#d1c6b2"
        elif natural in ("sand", "beach"):
            color = "#dfcf9f"
        if color:
            for piece in parts(e):
                p = pts(piece)
                if len(p) >= 3:
                    draw.polygon(p, fill=color)

    # Water relations are painted as their outer rings; inner rings become land.
    for e in elements:
        tags = e.get("tags", {})
        is_water = tags.get("natural") == "water" or tags.get("waterway") == "riverbank"
        if is_water:
            for piece in parts(e):
                p = pts(piece)
                if len(p) >= 3:
                    draw.polygon(p, fill="#9fc4cf", outline="#719aa6")
            for piece in parts(e, "inner"):
                p = pts(piece)
                if len(p) >= 3:
                    draw.polygon(p, fill=KIND[MAPS[zone]["kind"]][0], outline="#719aa6")
        elif e.get("type") == "way" and tags.get("waterway"):
            p = pts(e)
            if len(p) >= 2:
                width = 8 if tags["waterway"] in ("river", "canal") else 4
                draw.line(p, fill="#8fbac6", width=max(2, round(width * (1.3 if detail else 0.7))), joint="curve")

    # Aerodrome geometry and streets remain underneath building footprints.
    road_count = 0
    roads = []
    for e in elements:
        if e.get("type") != "way":
            continue
        tags = e.get("tags", {})
        p = pts(e)
        if len(p) < 2:
            continue
        if tags.get("aeroway") in ("runway", "taxiway", "apron"):
            if tags["aeroway"] == "apron" and len(p) >= 3:
                draw.polygon(p, fill="#c7c4ba")
            else:
                draw.line(p, fill="#f5f2e9", width=24 if detail else 12, joint="curve")
                draw.line(p, fill="#a8aaa6", width=15 if detail else 8, joint="curve")
        highway = tags.get("highway")
        if highway and highway not in ("footway", "path", "cycleway", "steps", "pedestrian", "service"):
            roads.append((highway, p, tags.get("name", "")))
            road_count += 1
    rank = {"motorway": 0, "trunk": 0, "primary": 1, "secondary": 2, "tertiary": 3}
    roads.sort(key=lambda item: rank.get(item[0], 4), reverse=True)
    for highway, p, name in roads:
        base = {"motorway": 9, "trunk": 9, "primary": 8, "secondary": 7,
                "tertiary": 6, "residential": 5, "living_street": 4}.get(highway, 4)
        width = max(2, round(base * (1.45 if detail else 0.85)))
        draw.line(p, fill="#b4ac99", width=width + (4 if detail else 2), joint="curve")
        draw.line(p, fill="#f7f4e9", width=width, joint="curve")

    for e in elements:
        if e.get("type") != "way":
            continue
        tags = e.get("tags", {})
        p = pts(e)
        if len(p) >= 2 and tags.get("railway") in ("rail", "light_rail", "subway", "disused", "abandoned"):
            draw.line(p, fill="#7c6659", width=4 if detail else 2, joint="curve")

    building_count = 0
    for e in elements:
        tags = e.get("tags", {})
        if "building" not in tags:
            continue
        color = building_tone(tags)
        outer = parts(e)
        for piece in outer:
            p = pts(piece)
            if len(p) < 3:
                continue
            draw.polygon(p, fill=color, outline="#605c55")
            building_count += 1
        for member in parts(e, "inner"):
            inner = pts(member)
            if len(inner) >= 3:
                draw.polygon(inner, fill=KIND[MAPS[zone]["kind"]][0], outline="#605c55")

    # Fine pedestrian routes go last in the gaps, but never hide the architecture.
    if detail:
        for e in elements:
            if e.get("type") != "way":
                continue
            tags = e.get("tags", {})
            p = pts(e)
            if len(p) >= 2 and tags.get("highway") in ("footway", "path", "cycleway", "steps", "pedestrian", "service"):
                draw.line(p, fill="#eeeade", width=2, joint="curve")
    return {"buildings": building_count, "roads": road_count, "status": "ok"}


def draw_special_features(draw, project, width=4):
    # Continuous authored landmarks connect independently compressed real districts.
    river = WORLD["features"]["danube_pest_bank"] + list(reversed(WORLD["features"]["danube_buda_bank"]))
    draw.polygon([project(p) for p in river], fill="#93bac6")
    draw.line([project(p) for p in WORLD["features"]["danube_pest_bank"]], fill="#d7ecee", width=width)
    draw.line([project(p) for p in WORLD["features"]["danube_buda_bank"]], fill="#d7ecee", width=width)
    # Real OSM streets remain visible inside each district. Only features that
    # must cross independently compressed BSPs are authored over the whole atlas.
    for line in WORLD["features"]["lines"]:
        if line["kind"] not in ("bridge", "view"):
            continue
        kind = line["kind"]
        color = "#eff7f2" if kind == "bridge" else "#d49057"
        p = [project(q) for q in line["pts"]]
        if kind == "view":
            # Dashed sightline, not a walkable connection.
            for a, b in zip(p, p[1:]):
                dx, dy = b[0]-a[0], b[1]-a[1]
                length = math.hypot(dx, dy)
                u = 0
                while u < length:
                    v = min(length, u+18)
                    draw.line((a[0]+dx*u/length, a[1]+dy*u/length,
                               a[0]+dx*v/length, a[1]+dy*v/length), fill=color, width=2)
                    u += 30
        else:
            draw.line(p, fill=color, width=width+2, joint="curve")
    draw.line([project(p) for p in WORLD["features"]["wall"]], fill="#f8eee0", width=width*4, joint="curve")
    draw.line([project(p) for p in WORLD["features"]["wall"]], fill="#bd4d32", width=width*2, joint="curve")
    for a, b in zip(WORLD["features"]["quarantine_fence"], WORLD["features"]["quarantine_fence"][1:]):
        dashed_world(draw, project, a, b, "#b88724")


def label(draw, xy, text, size=24, color=INK, bold=False, anchor="la", plate=False):
    f = font(size, bold)
    if plate:
        bb = draw.textbbox(xy, text, font=f, anchor=anchor)
        draw.rounded_rectangle((bb[0]-8, bb[1]-4, bb[2]+8, bb[3]+4), radius=5, fill=PAPER)
    draw.text(xy, text, font=f, fill=color, anchor=anchor)


def make_view(minx, maxx, miny, maxy, scale, left=0, top=0):
    return lambda p: (left + (p[0]-minx)*scale, top + (maxy-p[1])*scale)


def draw_nexus(draw, project):
    rect = WORLD["zones"][0]["rect"]
    poly = [(rect[0], rect[1]), (rect[2], rect[1]), (rect[2], rect[3]), (rect[0], rect[3])]
    p = [project(q) for q in poly]
    draw.polygon(p, fill="#ddc7bd", outline="#a35039")
    # Parallel diagonal hatching, clipped analytically to the eastern reserve.
    x0, y0, x1, y1 = rect
    k = y0-x1
    while k <= y1-x0:
        hits = []
        for x in (x0, x1):
            y = x+k
            if y0 <= y <= y1:
                hits.append((x, y))
        for y in (y0, y1):
            x = y-k
            if x0 <= x <= x1:
                hits.append((x, y))
        unique = list(dict.fromkeys(hits))
        if len(unique) >= 2:
            draw.line((project(unique[0]), project(unique[-1])), fill="#ceb0a4", width=2)
        k += 40
    # The Citadel apron includes the axis. Its western arc is a non-playable
    # cut-out inside c27_plaza/c27_road, not another Citadel or BSP.
    apron = WORLD["zones"][0]["citadel_apron"]
    acx, acy = project(apron["center"])
    radius = apron["radius_m"]
    xr = abs(project((radius, 0))[0]-acx)
    yr = abs(project((0, radius))[1]-acy)
    draw.ellipse((acx-xr, acy-yr, acx+xr, acy+yr), fill="#dfc4b8", outline="#a35039", width=4)
    for radius in (65, 130):
        xx = abs(project((radius, 0))[0]-acx)
        yy = abs(project((0, radius))[1]-acy)
        draw.ellipse((acx-xx, acy-yy, acx+xx, acy+yy), outline="#bf8d7a", width=2)
    cx = sum(q[0] for q in p)/4
    cy = sum(q[1] for q in p)/4
    label(draw, (cx, cy-22), "НЕКСУС", 32, "#853f31", True, "mm", True)
    label(draw, (cx, cy+20), "запретный сектор • не отдельный BSP", 19, "#853f31", False, "mm", True)


def draw_zones(draw, project, detail=False, only=None):
    stats = {}
    for m in WORLD["maps"]:
        if only and m["id"] != only:
            continue
        rect = m["rect"]
        poly = [(rect[0], rect[1]), (rect[2], rect[1]), (rect[2], rect[3]), (rect[0], rect[3])]
        draw.polygon([project(p) for p in poly], fill=KIND[m["kind"]][0])
        stats[m["id"]] = draw_osm(draw, m["id"], rect, project, detail)
    draw_nexus(draw, project)
    draw_special_features(draw, project, 5 if detail else 4)
    for m in WORLD["maps"]:
        if only and m["id"] != only:
            continue
        rect = m["rect"]
        poly = [(rect[0], rect[1]), (rect[2], rect[1]), (rect[2], rect[3]), (rect[0], rect[3]), (rect[0], rect[1])]
        p = [project(q) for q in poly]
        draw.line(p, fill="#fbfaf3", width=10 if detail else 7, joint="curve")
        draw.line(p, fill=KIND[m["kind"]][1], width=4 if detail else 3, joint="curve")
        # Compact title band leaves the great majority of architecture visible.
        x0, y0 = project((rect[0], rect[3]))
        x1, _ = project((rect[2], rect[3]))
        extra = "  +  " if not m["user"] else "  •  "
        title = f"{m['order']:02d}{extra}{m['name'].upper()}  /  {m['bsp']}"
        label(draw, (x0+15, y0+13), title, 25 if detail else 22, "#f5f3e8", True, "la", False)
        bb = draw.textbbox((x0+15, y0+13), title, font=font(25 if detail else 22, True))
        draw.rectangle((x0, y0, min(x1, bb[2]+15), bb[3]+9), fill="#1b3739")
        draw.text((x0+15, y0+13), title, font=font(25 if detail else 22, True), fill="#f5f3e8")
    return stats


CONNECTOR_CACHE = {}


def connector_target(zone, at, transition_type):
    """Nearest real road/rail/canal point to the planned BSP boundary.

    The short dashed corridor is deliberately new geometry: it stitches two
    compressed real districts without pretending that their source maps touched.
    """
    key = zone, tuple(at), transition_type
    if key in CONNECTOR_CACHE:
        return CONNECTOR_CACHE[key]
    if zone not in BBOXES or transition_type in ("open", "wild"):
        CONNECTOR_CACHE[key] = None
        return None
    elements = load_osm(zone)
    rect = MAPS[zone]["rect"]
    bbox = BBOXES[zone]
    best = None
    best_name = None
    best_source = None
    best_d = float("inf")
    for e in elements:
        if e.get("type") != "way":
            continue
        tags = e.get("tags", {})
        if transition_type == "rail":
            accepted = tags.get("railway") in ("rail", "light_rail", "subway")
        elif transition_type == "canal":
            accepted = bool(tags.get("waterway")) or tags.get("natural") == "water"
        elif transition_type == "underground":
            accepted = tags.get("railway") == "subway" or tags.get("highway") in ("primary", "secondary", "tertiary", "residential")
        else:
            accepted = tags.get("highway") in ("motorway", "trunk", "primary", "secondary", "tertiary", "residential", "living_street")
        if not accepted:
            continue
        points = geom(e, rect, bbox)
        for a, b in zip(points, points[1:]):
            dx, dy = b[0]-a[0], b[1]-a[1]
            length2 = dx*dx+dy*dy
            u = 0 if not length2 else max(0, min(1, ((at[0]-a[0])*dx+(at[1]-a[1])*dy)/length2))
            q = a[0]+u*dx, a[1]+u*dy
            distance = math.hypot(at[0]-q[0], at[1]-q[1])
            if distance < best_d:
                best_d, best = distance, q
                best_name = tags.get("name") or tags.get("ref")
                best_source = f"OSM {e.get('type')}/{e.get('id')}"
    result = {"point": best, "distance_m": round(best_d, 1), "name": best_name, "source": best_source} if best else None
    CONNECTOR_CACHE[key] = result
    return result


def dashed_world(draw, project, a, b, color):
    a, b = project(a), project(b)
    dx, dy = b[0]-a[0], b[1]-a[1]
    length = math.hypot(dx, dy)
    if length < 3:
        return
    u = 0
    while u < length:
        v = min(length, u+13)
        draw.line((a[0]+dx*u/length, a[1]+dy*u/length,
                   a[0]+dx*v/length, a[1]+dy*v/length), fill=color, width=3)
        u += 21


def draw_transitions(draw, project, detail=False):
    for t in WORLD["transitions"]:
        if t["type"] == "open" and "edge" in t:
            draw.line([project(p) for p in t["edge"]], fill="#edf4d8", width=16 if detail else 12)
            draw.line([project(p) for p in t["edge"]], fill=TCOL["open"], width=8 if detail else 5)
        for side in ("a", "b"):
            link = connector_target(t[side], t["at"], t["type"])
            if link and link["distance_m"] > 12:
                dashed_world(draw, project, t["at"], link["point"], TCOL[t["type"]])
        x, y = project(t["at"])
        r = 19 if detail else 16
        color = TCOL[t["type"]]
        draw.ellipse((x-r-3, y-r-3, x+r+3, y+r+3), fill="#f8f5e9")
        draw.ellipse((x-r, y-r, x+r, y+r), fill="#173a3d", outline=color, width=4)
        draw.text((x, y), t["id"][1:], font=font(17 if detail else 15, True), fill="white", anchor="mm")


def draw_citadel(draw, project):
    x, y = project((WORLD["citadel"]["x"], WORLD["citadel"]["y"]))
    draw.ellipse((x-28, y-28, x+28, y+28), fill="#d8633c", outline="#fff3dd", width=5)
    draw.polygon([(x, y-19), (x+12, y+15), (x, y+7), (x-12, y+15)], fill="#fff3dd")
    label(draw, (x+38, y-23), "ЦИТАДЕЛЬ • H 600 м", 23, "#8c3f2a", True, "la", True)


def master():
    minx, maxx, miny, maxy = -1550, 1300, -1350, 1410
    scale = 2.35
    ml, mt = 90, 235
    mw, mh = round((maxx-minx)*scale), round((maxy-miny)*scale)
    panel = 2250
    bottom = 715
    im = Image.new("RGB", (ml+mw+70+panel, mt+mh+bottom), PAPER)
    d = ImageDraw.Draw(im)
    project = make_view(minx, maxx, miny, maxy, scale, ml, mt)
    stats = draw_zones(d, project)
    draw_transitions(d, project)
    draw_citadel(d, project)
    d.rectangle((ml, mt, ml+mw, mt+mh), outline="#536560", width=3)

    label(d, (ml, 30), "СИТИ-27  /  МИРОВАЯ СХЕМА", 62, INK, True)
    label(d, (ml, 112), "ЭТАП 1 • ВСЕ 18 КАРТ • РЕАЛЬНЫЕ КОНТУРЫ ЗДАНИЙ И УЛИЦ • СЖАТАЯ КОМПОЗИЦИЯ BLENDER", 25, MUTED, True)
    label(d, (ml, 157), "Вверх ≈ восток, влево ≈ север внутри референсных районов. Районы намеренно переставлены и сжаты — это не карта Будапешта.", 22, MUTED)
    label(d, (ml, 191), "Номера 01–25 — переходы. Зелёные общие края — открытые стыки пустоши. «+» — дополнительные предлагаемые районы.", 22, MUTED)

    lx = ml+mw+70
    label(d, (lx, 40), "СОСТАВ МИРА", 34, INK, True)
    row = 105
    for m in sorted(WORLD["maps"], key=lambda z: z["order"]):
        color = KIND[m["kind"]][1]
        d.rectangle((lx, row+4, lx+18, row+22), fill=KIND[m["kind"]][0], outline=color, width=3)
        suffix = "  + предложение" if not m["user"] else ""
        label(d, (lx+32, row), f"{m['order']:02d}  {m['name']}  /  {m['bsp']}", 23, INK, True)
        label(d, (lx+32, row+30), f"{m['real']['name']}{suffix}", 18, MUTED)
        stat = stats[m["id"]]
        label(d, (lx+32, row+56), f"площади OSM: {stat['buildings']} зданий • {stat['roads']} улиц", 17, MUTED)
        row += 91
    label(d, (lx, row+10), "ЗДАНИЯ ПО ЭТАЖНОСТИ OSM", 24, INK, True)
    for i, (color, text) in enumerate([("#9b8d7d", "1–3 этажа"), ("#81756a", "4–6"), ("#665f5a", "7+"), ("#8c8174", "этажность не указана")]):
        yy = row+52+i*31
        d.rectangle((lx, yy, lx+24, yy+20), fill=color, outline="#605c55")
        label(d, (lx+38, yy-3), text, 19, MUTED)
    label(d, (lx, row+195), "© OpenStreetMap contributors • ODbL 1.0", 18, MUTED)
    label(d, (lx, row+224), "Контуры — реальные; размеры, соседство и добавки Альянса — игровая композиция.", 18, MUTED)
    yy = row+305
    label(d, (lx, yy), "СПЕЦИАЛЬНЫЕ ЛИНИИ", 25, INK, True); yy += 43
    d.line((lx, yy+12, lx+80, yy+12), fill="#bd4d32", width=8)
    label(d, (lx+100, yy), "Стена Альянса", 20, MUTED); yy += 39
    d.line((lx, yy+12, lx+80, yy+12), fill="#b88724", width=4)
    label(d, (lx+100, yy), "Периметр карантина", 20, MUTED); yy += 39
    for a, b in ((0, 12), (25, 37)):
        d.line((lx+a, yy+12, lx+b, yy+12), fill="#596d69", width=3)
    label(d, (lx+100, yy), "Новый коридор до реальной улицы", 20, MUTED); yy += 58
    label(d, (lx, yy), "ЦИТАДЕЛЬ И ГОРИЗОНТ", 25, INK, True); yy += 42
    for line in ("H 600 м • охраняемый радиус 200 м",
                 "общий большой 3D-скайбокс: 1:64",
                 "модель Цитадели: 492.09 HU",
                 "одна ось и один угловой размер во всех BSP",
                 "фактическая видимость — проверка на greybox"):
        label(d, (lx, yy), line, 20, MUTED); yy += 31
    yy += 24
    label(d, (lx, yy), "ПУСТОШЬ", 25, INK, True); yy += 42
    for line in ("Дорога / М0 / аэродром — дорожный каркас",
                 "Открытые края — поля, опушка и натуральный рельеф",
                 "Точки интереса вместо сплошной застройки",
                 "Локальный origin у каждой карты",
                 "Габарит клетки не превышает 624 × 624 м"):
        for part in textwrap.wrap(line, 56):
            label(d, (lx, yy), part, 20, MUTED); yy += 29
        yy += 4

    ty = mt+mh+35
    label(d, (ml, ty), "ПЕРЕХОДЫ", 29, INK, True)
    names = {m["id"]: m["name"] for m in WORLD["maps"]}
    colw = (ml+mw+70+panel-ml)//3
    for i, t in enumerate(WORLD["transitions"]):
        col, rr = divmod(i, 9)
        x = ml+col*colw
        y = ty+55+rr*52
        label(d, (x, y), t["id"][1:], 22, TCOL[t["type"]], True)
        label(d, (x+48, y), f"{names[t['a']]} — {names[t['b']]}", 22, INK, True)
        label(d, (x+48, y+27), t["name"], 18, MUTED)
    im.save(ROOT / "city27_world_atlas.png", optimize=True)
    (ROOT / "city27_world_atlas_stats.json").write_text(json.dumps(stats, ensure_ascii=False, indent=2), encoding="utf-8")
    audit = []
    resolved_maps = []
    for m in WORLD["maps"]:
        item = dict(m)
        item["local_origin_world_m"] = [(m["rect"][0]+m["rect"][2])/2, (m["rect"][1]+m["rect"][3])/2, 0]
        resolved_maps.append(item)
    resolved_transitions = []
    origins = {m["id"]: m["local_origin_world_m"] for m in resolved_maps}
    for t in WORLD["transitions"]:
        resolved = dict(t)
        ends = []
        for side in ("a", "b"):
            link = connector_target(t[side], t["at"], t["type"])
            zone = t[side]
            origin = origins[zone]
            local_hu = [(t["at"][0]-origin[0])*WORLD["frame"]["hu_per_m"],
                        (t["at"][1]-origin[1])*WORLD["frame"]["hu_per_m"], 0]
            entry = {"map": zone, "bsp": MAPS[zone]["bsp"], "entry_world_m": t["at"],
                     "local_origin_world_m": origin, "entry_local_hu": local_hu,
                     "real_connection": link}
            ends.append(entry)
            audit.append({"transition": t["id"], "zone": zone, "type": t["type"], **(link or {})})
        resolved["ends"] = ends
        resolved_transitions.append(resolved)
    (ROOT / "city27_connector_audit.json").write_text(json.dumps(audit, ensure_ascii=False, indent=2), encoding="utf-8")
    resolved_world = {k: v for k, v in WORLD.items() if k not in ("maps", "transitions")}
    resolved_world["maps"] = resolved_maps
    resolved_world["transitions"] = resolved_transitions
    (ROOT / "city27_world_resolved.json").write_text(json.dumps(resolved_world, ensure_ascii=False, indent=2), encoding="utf-8")
    print("MASTER", im.size, flush=True)
    return im.size


def zone_detail(m):
    rect = m["rect"]
    scale = 4.3
    ml, mt = 70, 130
    mw, mh = round((rect[2]-rect[0])*scale), round((rect[3]-rect[1])*scale)
    side = 800
    im = Image.new("RGB", (ml+mw+60+side, mt+mh+95), PAPER)
    layer = Image.new("RGB", im.size, PAPER)
    ld = ImageDraw.Draw(layer)
    project = make_view(rect[0], rect[2], rect[1], rect[3], scale, ml, mt)
    draw_zones(ld, project, detail=True, only=m["id"])
    draw_transitions(ld, project, detail=True)
    draw_citadel(ld, project)
    x0, y0 = project((rect[0], rect[3]))
    x1, y1 = project((rect[2], rect[1]))
    mask = Image.new("L", im.size, 0)
    # A narrow margin preserves the outer half of circular transition markers.
    ImageDraw.Draw(mask).rectangle((x0-26, y0-26, x1+26, y1+26), fill=255)
    im.paste(layer, (0, 0), mask)
    d = ImageDraw.Draw(im)
    d.rectangle((ml, mt, ml+mw, mt+mh), outline="#536560", width=3)
    label(d, (ml, 24), f"{m['order']:02d}  {m['name'].upper()}  /  {m['bsp']}", 48, INK, True)
    label(d, (ml, 82), "Детализация района • OSM + игровые связи • ориентация как в Blender", 22, MUTED)
    lx = ml+mw+60
    label(d, (lx, 40), "НАЗНАЧЕНИЕ", 27, INK, True)
    yy = 88
    for paragraph in (m["real"]["name"], m["about"], m["role"]):
        lines = textwrap.wrap(paragraph, 42)
        for line in lines:
            label(d, (lx, yy), line, 20, MUTED)
            yy += 27
        yy += 20
    label(d, (lx, yy), "ОРИЕНТИРЫ", 25, INK, True); yy += 43
    for landmark in m["landmarks"]:
        for j, line in enumerate(textwrap.wrap(landmark, 40)):
            label(d, (lx, yy), ("• " if j == 0 else "  ")+line, 20, MUTED)
            yy += 28
    yy += 25
    label(d, (lx, yy), "ГАБАРИТЫ GREYBOX", 25, INK, True); yy += 43
    w, h = rect[2]-rect[0], rect[3]-rect[1]
    for line in (f"{w} × {h} м", f"{round(w*WORLD['frame']['hu_per_m'])} × {round(h*WORLD['frame']['hu_per_m'])} HU",
                 "лимит одного BSP: 624 × 624 м", "переходы: см. номера на общих краях"):
        label(d, (lx, yy), line, 20, MUTED); yy += 29
    yy += 20
    for line in textwrap.wrap("Контуры зданий реальные, но не являются готовой геометрией. Фасады, высоты и проходимость уточняются на этапе greybox по сканам и Street View.", 43):
        label(d, (lx, yy), line, 18, MUTED); yy += 24
    out = DETAILS / f"{m['order']:02d}_{m['id']}.png"
    im.save(out, optimize=True)
    print("DETAIL", out.name, im.size, flush=True)
    return out.name


def scan_alignment():
    path = ROOT / "city27_scan_top_projection.json"
    if not path.exists():
        print("SCAN alignment skipped: no projection", flush=True)
        return
    meta = json.loads(path.read_text(encoding="utf-8"))
    base = Image.open(ROOT / meta["image"]).convert("RGB")
    iw, ih = base.size
    side = 1420
    im = Image.new("RGB", (iw+side, ih), "#283335")
    im.paste(base, (0, 0))
    d = ImageDraw.Draw(im)
    cx, cy = meta["center_blender"]
    scale = meta["ortho_scale"]
    bx, by = meta["citadel_blender"]
    k = meta["world_m_per_blender_unit"]
    def project(p):
        x = p[0]/k+bx
        y = p[1]/k+by
        return ((x-(cx-scale/2))/scale*iw, ((cy+scale/2)-y)/scale*ih)
    for m in WORLD["maps"]:
        rect = m["rect"]
        corners = [(rect[0], rect[1]), (rect[2], rect[1]), (rect[2], rect[3]), (rect[0], rect[3]), (rect[0], rect[1])]
        p = [project(q) for q in corners]
        color = KIND[m["kind"]][1]
        d.line(p, fill="#f4edd9", width=8, joint="curve")
        d.line(p, fill=color, width=4, joint="curve")
        x, y = project((rect[0], rect[3]))
        if -100 < x < iw+100 and -100 < y < ih+100:
            label(d, (x+10, y+8), f"{m['order']:02d} {m['name'].upper()}", 25, "#f7f0d9", True, "la", True)
    for t in WORLD["transitions"]:
        x, y = project(t["at"])
        if -30 < x < iw+30 and -30 < y < ih+30:
            d.ellipse((x-17, y-17, x+17, y+17), fill="#173a3d", outline=TCOL[t["type"]], width=4)
            d.text((x, y), t["id"][1:], font=font(16, True), fill="white", anchor="mm")
    x, y = project((0, 0))
    d.ellipse((x-16, y-16, x+16, y+16), fill="#db633c", outline="white", width=3)
    label(d, (x+24, y-20), "ОСЬ ЦИТАДЕЛИ", 24, "#ffd6b0", True, "la", True)
    # Do not let off-screen district rectangles leak into the information panel.
    d.rectangle((iw, 0, iw+side, ih), fill="#283335")
    lx = iw+45
    label(d, (lx, 45), "СИТИ-27", 52, "#f2efdf", True)
    label(d, (lx, 120), "ПРИВЯЗКА К ЛОКАЛЬНЫМ", 29, "#d2ded8", True)
    label(d, (lx, 161), "СКАНАМ BLENDER", 29, "#d2ded8", True)
    yy = 240
    for line in ("Цветные рамки — игровые клетки.", "Номера — те же переходы, что в атласе.", "Это исходная композиция, а не BSP.", "Скан — референс, не готовая геометрия.", "Большой 3D-скайбокс здесь скрыт."):
        for part in textwrap.wrap(line, 34):
            label(d, (lx, yy), part, 23, "#d2ded8"); yy += 31
        yy += 13
    yy += 25
    for m in WORLD["maps"]:
        if not m.get("scan"):
            continue
        label(d, (lx, yy), f"{m['order']:02d}  {m['name']}", 27, "#f2efdf", True); yy += 37
        label(d, (lx, yy), m["scan"], 21, "#a9bfba"); yy += 49
    label(d, (lx, ih-95), ".blend не изменялся • City-27", 22, "#a9bfba")
    im.save(ROOT / "city27_blender_alignment.png", optimize=True)
    print("SCAN alignment written", flush=True)


def write_viewer(detail_files):
    cards = []
    for m in sorted(WORLD["maps"], key=lambda z: z["order"]):
        cards.append(f'<button data-file="city27_zone_details/{html.escape(detail_files[m["id"]])}"><b>{m["order"]:02d} · {html.escape(m["name"])}</b><span>{m["bsp"]}</span></button>')
    doc = '''<!doctype html><html lang="ru"><head><meta charset="utf-8"><title>Сити-27 — атлас мира</title>
<style>body{margin:0;background:#16282b;color:#eee;font:16px Arial}header{position:sticky;top:0;z-index:2;background:#16282bee;padding:14px 20px;border-bottom:1px solid #617473}button{background:#28464a;color:#eee;border:1px solid #6c8584;padding:9px 13px;cursor:pointer}#viewport{height:75vh;overflow:auto;background:#0d191b}#map{display:block;width:100%;height:auto;transform-origin:top left}#districts{display:flex;flex-wrap:wrap;gap:7px;padding:14px}#districts button{text-align:left}#districts span{display:block;color:#adc0bd;font-size:12px;margin-top:4px}.hint{color:#b5c7c4;margin-left:12px}</style></head><body>
<header><b>СИТИ-27 · МИРОВОЙ АТЛАС</b> <button id="whole">Весь мир</button><button id="minus">−</button><button id="plus">+</button><span class="hint">Колесо/полосы прокрутки — навигация. Районы ниже открывают детальные планы.</span></header>
<div id="viewport"><img id="map" src="city27_world_atlas.png"></div><div id="districts">''' + "".join(cards) + '''</div>
<script>const img=document.querySelector('#map');let z=1;function zoom(v){z=Math.max(.25,Math.min(4,v));img.style.width=(z*100)+'%'}document.querySelector('#whole').onclick=()=>{img.src='city27_world_atlas.png';zoom(1)};document.querySelector('#plus').onclick=()=>zoom(z*1.25);document.querySelector('#minus').onclick=()=>zoom(z/1.25);document.querySelectorAll('[data-file]').forEach(b=>b.onclick=()=>{img.src=b.dataset.file;zoom(1);document.querySelector('#viewport').scrollTo(0,0)});</script></body></html>'''
    (ROOT / "city27_world_atlas.html").write_text(doc, encoding="utf-8")


if __name__ == "__main__":
    master()
    scan_alignment()
    files = {m["id"]: zone_detail(m) for m in WORLD["maps"]}
    write_viewer(files)
    print("WROTE", ROOT / "city27_world_atlas.html")
