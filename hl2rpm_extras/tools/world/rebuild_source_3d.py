"""Build a geometrically compressed, fully 3D City-27 Hammer masterplan.

Run with Blender background mode. This creates a NEW blend file; the user's
hl2rpm_map.blend is only a reference and is never opened or overwritten here.
Metres are used in the planning scene. Every Source BSP has its own local origin.
"""
import bpy
import hashlib
import json
import math
import random
from pathlib import Path
from mathutils import Vector

ROOT = Path(r"E:\SourceModding\HL2RPMSOURCE\hl2rpm_map_developing")
OUT = ROOT / "source_3d"
OUT.mkdir(parents=True, exist_ok=True)
HERE = Path(__file__).resolve().parent
WORLD = json.loads((HERE / "world_layout.json").read_text(encoding="utf-8"))
BBOXES = json.loads((ROOT / "osm_world" / "source_bboxes.json").read_text(encoding="utf-8")) if (ROOT / "osm_world" / "source_bboxes.json").exists() else None
if BBOXES is None:
    import sys
    sys.path.insert(0, str(HERE))
    from fetch_world_osm import BBOXES
K = WORLD["frame"]["hu_per_m"]
PLAY_LIMIT = WORLD["frame"]["safe_playable_limit_m"]
MAPS = {m["id"]: m for m in WORLD["maps"]}

bpy.ops.object.select_all(action="SELECT")
bpy.ops.object.delete(use_global=False)
for block in list(bpy.data.collections):
    if block.name != "Collection":
        bpy.data.collections.remove(block)
scene = bpy.context.scene
bpy.context.preferences.filepaths.save_version = 0
scene.unit_settings.system = "METRIC"
scene.unit_settings.scale_length = 1.0


def rgb(h):
    h = h.lstrip("#")
    return tuple(int(h[i:i+2], 16)/255 for i in (0, 2, 4))


def material(name, color, roughness=0.8, metallic=0.0):
    m = bpy.data.materials.new(name)
    m.diffuse_color = (*rgb(color), 1)
    m.use_nodes = True
    bs = m.node_tree.nodes.get("Principled BSDF")
    bs.inputs["Base Color"].default_value = (*rgb(color), 1)
    bs.inputs["Roughness"].default_value = roughness
    bs.inputs["Metallic"].default_value = metallic
    return m


MAT = {
    "city_ground": material("Source cell / city", "#81928d"),
    "belt_ground": material("Source cell / belt", "#a89b78"),
    "waste_ground": material("Source cell / waste", "#829363"),
    "road": material("Asphalt / planning volume", "#626765"),
    "rail": material("Rail volume", "#51422f", 0.55, 0.45),
    "water": material("Danube and canals", "#437f91", 0.18, 0.15),
    "forest": material("Woodland ground", "#4f704d"),
    "meadow": material("Fields and dry grass", "#91a46a"),
    "border": material("Source cell boundary", "#eee2b4"),
    "header": material("Map label band", "#17363b"),
    "text": material("Plan text", "#f1edda"),
    "trim": material("Budapest cornices and stone", "#d4c6a5"),
    "window": material("Recessed facade glazing", "#263b40", 0.25, 0.15),
    "roof": material("Hungarian tile roof", "#7c4538"),
    "roof_flat": material("Modern and industrial roof", "#666862"),
    "metal": material("Combine dark alloy", "#35434a", 0.4, 0.65),
    "combine": material("Combine protected zone", "#6f8790", 0.45, 0.5),
    "gate": material("Transition marker", "#e9b558"),
    "brick": material("Historic industrial brick", "#875b47"),
    "sand": material("Dry wasteland sand", "#b4a272"),
    "landmark": material("Landmark pale stone", "#c2b497"),
}
PLASTERS = [material("Budapest plaster %02d" % i, c) for i, c in enumerate(
    ["#c4ae84", "#bca08d", "#c6c0aa", "#a5b0a4", "#d0b98e", "#ad9b8d", "#b2a483"])]


class MeshBuilder:
    def __init__(self, name, collection):
        self.name, self.collection = name, collection
        self.v, self.f, self.mi = [], [], []
        self.materials = []
        self.lookup = {}

    def mid(self, mat):
        if mat.name not in self.lookup:
            self.lookup[mat.name] = len(self.materials)
            self.materials.append(mat)
        return self.lookup[mat.name]

    def face(self, verts, mat):
        start = len(self.v)
        self.v.extend(verts)
        self.f.append(tuple(range(start, start+len(verts))))
        self.mi.append(self.mid(mat))

    def prism(self, poly, z0, z1, mat):
        p = list(poly)
        if len(p) < 3 or z1 <= z0:
            return
        self.face([(x, y, z0) for x, y in reversed(p)], mat)
        self.face([(x, y, z1) for x, y in p], mat)
        for a, b in zip(p, p[1:]+p[:1]):
            self.face([(a[0], a[1], z0), (b[0], b[1], z0),
                       (b[0], b[1], z1), (a[0], a[1], z1)], mat)

    def box(self, x, y, z, sx, sy, sz, mat):
        self.prism([(x-sx/2, y-sy/2), (x+sx/2, y-sy/2),
                    (x+sx/2, y+sy/2), (x-sx/2, y+sy/2)], z, z+sz, mat)

    def cyl(self, x, y, z, r, h, mat, n=24, rtop=None):
        r2 = r if rtop is None else rtop
        low = [(x+r*math.cos(i*math.tau/n), y+r*math.sin(i*math.tau/n)) for i in range(n)]
        self.face([(a, b, z) for a, b in reversed(low)], mat)
        high = [(x+r2*math.cos(i*math.tau/n), y+r2*math.sin(i*math.tau/n)) for i in range(n)]
        self.face([(a, b, z+h) for a, b in high], mat)
        for i in range(n):
            j = (i+1) % n
            self.face([(low[i][0], low[i][1], z), (low[j][0], low[j][1], z),
                       (high[j][0], high[j][1], z+h), (high[i][0], high[i][1], z+h)], mat)

    def roof(self, poly, z, mansard=True):
        if len(poly) < 3:
            return
        cx = sum(p[0] for p in poly)/len(poly)
        cy = sum(p[1] for p in poly)/len(poly)
        if mansard:
            inner = [(cx+(x-cx)*0.84, cy+(y-cy)*0.84) for x, y in poly]
            for a, b, c, d in zip(poly, poly[1:]+poly[:1], inner, inner[1:]+inner[:1]):
                self.face([(a[0], a[1], z), (b[0], b[1], z),
                           (d[0], d[1], z+3.2), (c[0], c[1], z+3.2)], MAT["roof"])
            self.face([(x, y, z+3.2) for x, y in inner], MAT["roof_flat"])
        else:
            self.prism(poly, z, z+0.45, MAT["roof_flat"])

    def panel(self, a, b, t, z, width, height, mat):
        dx, dy = b[0]-a[0], b[1]-a[1]
        length = math.hypot(dx, dy)
        if length < 0.01:
            return
        ux, uy = dx/length, dy/length
        cx, cy = a[0]+dx*t, a[1]+dy*t
        # A facade panel is real geometry, not a texture or a GIS symbol.
        nx, ny = uy, -ux
        p0 = (cx-ux*width/2+nx*0.07, cy-uy*width/2+ny*0.07, z-height/2)
        p1 = (cx+ux*width/2+nx*0.07, cy+uy*width/2+ny*0.07, z-height/2)
        p2 = (p1[0], p1[1], z+height/2)
        p3 = (p0[0], p0[1], z+height/2)
        self.face([p0, p1, p2, p3], mat)

    def ribbon(self, a, b, width, z, mat):
        dx, dy = b[0]-a[0], b[1]-a[1]
        length = math.hypot(dx, dy)
        if length < 0.05:
            return
        nx, ny = -dy/length*width/2, dx/length*width/2
        self.face([(a[0]+nx, a[1]+ny, z), (b[0]+nx, b[1]+ny, z),
                   (b[0]-nx, b[1]-ny, z), (a[0]-nx, a[1]-ny, z)], mat)

    def finish(self):
        if not self.f:
            return None
        mesh = bpy.data.meshes.new(self.name)
        mesh.from_pydata(self.v, [], self.f)
        mesh.materials.clear()
        for m in self.materials:
            mesh.materials.append(m)
        mesh.update()
        obj = bpy.data.objects.new(self.name, mesh)
        self.collection.objects.link(obj)
        for poly, index in zip(mesh.polygons, self.mi):
            poly.material_index = index
        return obj


def collection(name):
    c = bpy.data.collections.new(name)
    scene.collection.children.link(c)
    return c


def poly_area(p):
    return abs(sum(a[0]*b[1]-b[0]*a[1] for a, b in zip(p, p[1:]+p[:1])))/2


def simplify(p):
    out = []
    for q in p:
        if not out or math.dist(q, out[-1]) > 0.45:
            out.append(q)
    if len(out) > 2 and math.dist(out[0], out[-1]) < 0.5:
        out.pop()
    return out


def district_scale(zone):
    rect = MAPS[zone]["rect"]
    south, west, north, east = BBOXES[zone]
    midlat = (south+north)/2
    sw = (north-south)*111300
    sh = (east-west)*111300*math.cos(math.radians(midlat))
    return max((rect[2]-rect[0])/sw, (rect[3]-rect[1])/sh)


def architecture_scale(zone):
    """Keep human-scale massing while streets/district distances are compressed.

    Raw OSM polygons were already shrunk with the district. Restore only their
    local footprint partially, not the whole district back to 1:1. The thinned
    building set leaves room for the enlarged but still compact Budapest blocks.
    """
    return min(3.5, max(1.15, district_scale(zone)**-0.60))


def source_xy(zone, lat, lon):
    rect = MAPS[zone]["rect"]
    south, west, north, east = BBOXES[zone]
    midlat, midlon = (south+north)/2, (west+east)/2
    scale = district_scale(zone)
    cx, cy = (rect[0]+rect[2])/2, (rect[1]+rect[3])/2
    x = cx+(midlat-lat)*111300*scale
    y = cy+(lon-midlon)*111300*math.cos(math.radians(midlat))*scale
    return max(rect[0], min(rect[2], x)), max(rect[1], min(rect[3], y))


def outer_parts(e):
    if e.get("type") == "way":
        return [e]
    if e.get("type") == "relation":
        return [m for m in e.get("members", []) if m.get("type") == "way" and m.get("role", "outer") == "outer"]
    return []


def geometry(zone, e):
    return [source_xy(zone, p["lat"], p["lon"]) for p in e.get("geometry", []) if "lat" in p]


def buildings(zone):
    path = ROOT / "osm_world" / f"{zone}.json"
    elements = json.loads(path.read_text(encoding="utf-8"))["elements"]
    candidates, seen = [], set()
    for e in elements:
        tags = e.get("tags", {})
        if "building" not in tags:
            continue
        for part in outer_parts(e):
            p = simplify(geometry(zone, part))
            if len(p) < 3 or poly_area(p) < 8:
                continue
            key = tuple(sorted((round(x, 1), round(y, 1)) for x, y in p))
            if key in seen:
                continue
            seen.add(key)
            area = poly_area(p)
            try:
                levels = float(tags.get("building:levels", "0").split(";")[0])
            except ValueError:
                levels = 0
            digest = hashlib.sha1(repr(key).encode()).digest()
            score = int.from_bytes(digest[:4], "little")/2**32 + min(area, 1200)/4000
            if tags.get("historic") or tags.get("railway") == "station" or tags.get("building") in ("church", "cathedral", "palace", "train_station"):
                score += 50
            candidates.append((score, p, levels, tags, sum(x for x, _ in p)/len(p), sum(y for _, y in p)/len(p)))
    imported_count = len(candidates)
    m = MAPS[zone]
    anchor = source_xy(zone, m["real"]["lat"], m["real"]["lon"])
    reserved = {"station": 95, "plaza": 112, "apartments": 75, "industrial": 100,
                "quarantine": 85, "kobanya": 75, "airbase": 85, "danube_flats": 65,
                "budahills": 35, "fields": 35}
    if zone in reserved:
        candidates = [c for c in candidates if math.hypot(c[4]-anchor[0], c[5]-anchor[1]) > reserved[zone]]
    kind = MAPS[zone]["kind"]
    ratio = {"city": 0.28, "belt": 0.22, "waste": 0.13}[kind]
    if zone in ("north_hills", "m0", "airbase"):
        ratio = 0.7
    elif zone == "fields":
        ratio = 0.65
    target = max(12, round(len(candidates)*ratio))
    candidates.sort(key=lambda c: c[0], reverse=True)
    # Large landmarks are already promoted in score; the rest is a deterministic
    # distributed subset, so whole streets are not discarded from one neighbourhood.
    selected = candidates[:target]
    selected.sort(key=lambda c: (c[4], c[5]))
    return selected, imported_count


def building_levels(zone, levels, tags):
    if levels > 0:
        return max(1, min(8, round(levels)))
    if zone in ("station", "centre", "plaza", "apartments", "quarantine"):
        return random.Random(tags.get("name", "")+zone).choice([4, 5, 5, 6])
    if zone in ("industrial", "kobanya", "danube_flats"):
        return random.Random(str(tags)).choice([1, 2, 2, 3])
    return random.Random(str(tags)).choice([1, 1, 2])


def add_building(buf, zone, record):
    _, p, raw_levels, tags, cx, cy = record
    expand = architecture_scale(zone)
    r = MAPS[zone]["rect"]
    p = [(max(r[0], min(r[2], cx+(x-cx)*expand)),
          max(r[1], min(r[3], cy+(y-cy)*expand))) for x, y in p]
    levels = building_levels(zone, raw_levels, tags)
    storey = 3.35 if MAPS[zone]["kind"] == "city" else 3.0
    h = levels*storey
    plaster = PLASTERS[int((cx*17+cy*29).__hash__()) % len(PLASTERS)]
    if zone in ("industrial", "kobanya"):
        plaster = MAT["brick"]
    buf.prism(p, 0, 0.65, MAT["trim"])
    buf.prism(p, 0.65, h, plaster)
    modern = levels <= 3 or tags.get("building") in ("warehouse", "industrial", "garage")
    buf.roof(p, h, not modern)
    # The projecting stone cornice is a separate 3D volume.
    mx = sum(x for x, _ in p)/len(p); my = sum(y for _, y in p)/len(p)
    band = [(mx+(x-mx)*1.008, my+(y-my)*1.008) for x, y in p]
    buf.prism(band, h-0.40, h+0.05, MAT["trim"])
    for a, b in zip(p, p[1:]+p[:1]):
        length = math.dist(a, b)
        if length < 6:
            continue
        count = min(8, max(1, int(length/3.4)))
        for floor in range(levels):
            if floor == 0 and length < 12:
                continue
            for j in range(count):
                t = (j+0.5)/count
                width = 1.15 if not modern else 1.65
                buf.panel(a, b, t, storey*(floor+0.55), width, 1.65, MAT["window"])
    if levels >= 3 and poly_area(p) > 250:
        buf.box(cx, cy, h+3.0, 1.4, 1.4, 2.1, MAT["brick"])


def build_roads(zone, collection_):
    buf = MeshBuilder(zone+" / streets", collection_)
    elements = json.loads((ROOT / "osm_world" / f"{zone}.json").read_text(encoding="utf-8"))["elements"]
    widths = {"motorway": 9, "trunk": 9, "primary": 8, "secondary": 7,
              "tertiary": 6, "residential": 4.5, "living_street": 4, "unclassified": 4}
    for e in elements:
        if e.get("type") != "way":
            continue
        tags = e.get("tags", {})
        p = []
        for part in outer_parts(e):
            p = geometry(zone, part)
            if len(p) < 3:
                continue
            if tags.get("natural") == "water" or tags.get("waterway") == "riverbank":
                buf.face([(a, b, 0.07) for a, b in p], MAT["water"])
            elif tags.get("landuse") == "forest" or tags.get("natural") == "wood":
                buf.face([(a, b, 0.025) for a, b in p], MAT["forest"])
            elif tags.get("landuse") in ("farmland", "grass", "meadow", "orchard"):
                buf.face([(a, b, 0.02) for a, b in p], MAT["meadow"])
        if e.get("type") != "way":
            continue
        h = tags.get("highway")
        p = geometry(zone, e)
        if tags.get("waterway"):
            width = 12 if tags["waterway"] == "river" else 7 if tags["waterway"] in ("canal", "stream") else 3
            for a, b in zip(p, p[1:]):
                buf.ribbon(a, b, width, 0.08, MAT["water"])
        if h in widths:
            for a, b in zip(p, p[1:]):
                buf.ribbon(a, b, widths[h], 0.09, MAT["road"])
        if tags.get("railway") in ("rail", "light_rail"):
            for a, b in zip(p, p[1:]):
                buf.ribbon(a, b, 1.3, 0.16, MAT["rail"])
        if tags.get("aeroway") in ("runway", "taxiway"):
            for a, b in zip(p, p[1:]):
                buf.ribbon(a, b, 15 if tags["aeroway"] == "runway" else 8, 0.08, MAT["road"])
    return buf.finish()


def text(name, body, position, size, collection_, align="LEFT"):
    curve = bpy.data.curves.new(name, "FONT")
    curve.body = body
    curve.size = size
    curve.align_x = align
    curve.extrude = 0
    obj = bpy.data.objects.new(name, curve)
    collection_.objects.link(obj)
    obj.location = position
    curve.materials.append(MAT["text"])
    return obj


def custom_landmarks(zone, rect, c):
    buf = MeshBuilder(zone+" / architectural landmarks", c)
    m = MAPS[zone]
    x, y = source_xy(zone, m["real"]["lat"], m["real"]["lon"])
    def barrel(cx, cy, width, length, base, radius, mat):
        steps = 16
        for i in range(steps):
            a, b = math.pi*i/steps, math.pi*(i+1)/steps
            xa, za = cx+radius*math.cos(a), base+radius*math.sin(a)
            xb, zb = cx+radius*math.cos(b), base+radius*math.sin(b)
            buf.face([(xa, cy-length/2, za), (xa, cy+length/2, za),
                      (xb, cy+length/2, zb), (xb, cy-length/2, zb)], mat)
    if zone == "station":
        buf.box(x, y, 0, 38, 110, 10, MAT["landmark"])
        barrel(x, y, 38, 110, 10, 19, MAT["roof_flat"])
        for dx in (-48, 48):
            buf.box(x+dx, y+20, 0, 45, 70, 18, MAT["landmark"])
            buf.box(x+dx, y+20, 18, 47, 72, 1.2, MAT["trim"])
            buf.roof([(x+dx-23.5, y-16), (x+dx+23.5, y-16), (x+dx+23.5, y+56), (x+dx-23.5, y+56)], 19.2, True)
    elif zone == "centre":
        bx = -510
        # The bridge is split exactly at the Source map transition y=-600.
        buf.box(bx, -501, 9, 20, 198, 2.4, MAT["landmark"])
        for by in (-402, -600):
            for dx in (-9, 9):
                buf.box(bx+dx, by, 9, 6, 10, 26, MAT["landmark"])
                buf.cyl(bx+dx, by, 35, 5, 7, MAT["roof"], 4, 0)
    elif zone == "plaza":
        buf.box(x, y, 0, 95, 110, 0.4, MAT["landmark"])
        buf.cyl(x, y, 0, 5, 33, MAT["landmark"], 12, 2.4)
        for side in (-1, 1):
            for i in range(11):
                a = (-0.9+1.8*i/10)+(0 if side == 1 else math.pi)
                cx, cy = x+side*55*math.cos(a), y+45*math.sin(a)
                buf.cyl(cx, cy, 0, 1.1, 13, MAT["landmark"], 10)
                buf.box(cx, cy, 13, 5, 5, 3.2, MAT["trim"])
        for dx in (-82, 82):
            buf.box(x+dx, y+34, 0, 46, 28, 17, MAT["landmark"])
            buf.cyl(x+dx, y+34, 17, 9, 8, MAT["roof"], 24, 1)
    elif zone == "apartments":
        for dx in (-48, 48):
            for dy in (-42, 42):
                buf.box(x+dx, y+dy, 0, 80, 15, 22, PLASTERS[3])
                for zz in range(4, 22, 4):
                    buf.box(x+dx, y+dy-7.55, zz, 72, 0.15, 1.2, MAT["window"])
        buf.cyl(x, y, 0, 18, 25, MAT["landmark"], 40)
        buf.cyl(x, y, 25, 20, 9, MAT["roof"], 40, 4)
        buf.cyl(x+25, y+8, 0, 11, 22, MAT["landmark"], 32)
        buf.cyl(x+25, y+8, 22, 12, 8, MAT["roof"], 32, 3)
    elif zone == "industrial":
        for dx in (-35, 35):
            buf.cyl(x+dx, y, 0, 23, 20, MAT["metal"], 48)
            buf.cyl(x+dx, y, 20, 24, 2, MAT["trim"], 48)
        buf.box(x+75, y+20, 0, 75, 35, 18, MAT["brick"])
        buf.cyl(x+83, y+35, 0, 4.5, 92, MAT["brick"], 24, 2.4)
    elif zone == "quarantine":
        # Southern continuation of the Chain Bridge, ending at this BSP border.
        buf.box(-510, -661, 9, 20, 122, 2.4, MAT["landmark"])
        for by in (-600, -722):
            for dx in (-9, 9):
                buf.box(-510+dx, by, 9, 6, 10, 26, MAT["landmark"])
                buf.cyl(-510+dx, by, 35, 5, 7, MAT["roof"], 4, 0)
        for level in range(3):
            buf.box(x, y+level*14, level*5, 130-level*18, 42, 5, MAT["landmark"])
        buf.box(x, y+27, 15, 98, 28, 14, MAT["landmark"])
        buf.roof([(x-49, y+13), (x+49, y+13), (x+49, y+41), (x-49, y+41)], 29, True)
    elif zone == "outskirts":
        rng = random.Random(27)
        for i in range(22):
            buf.box(x+rng.uniform(-45, 45), y+rng.uniform(-35, 35), 0,
                    rng.uniform(2, 4.5), rng.uniform(4, 7), rng.uniform(1.2, 2.2), MAT["metal"])
    elif zone == "kobanya":
        buf.box(x, y, 0, 105, 32, 20, MAT["brick"])
        buf.cyl(x+42, y+22, 0, 4, 68, MAT["brick"], 20, 2.5)
        for dx in range(-40, 41, 16):
            buf.box(x+dx, y-16.1, 4, 2.4, 0.15, 11, MAT["window"])
    elif zone == "fields":
        for dx, dy in ((-12, 0), (0, 0), (12, 0)):
            buf.cyl(x+dx, y+dy, 0, 4.5, 16, MAT["roof_flat"], 24)
            buf.cyl(x+dx, y+dy, 16, 5, 4, MAT["roof"], 24, 0)
    elif zone == "north_hills":
        buf.cyl(x, y, -1, 75, 18, MAT["forest"], 40, 9)
        buf.cyl(x, y, 0, 3.5, 65, MAT["metal"], 8, 1.4)
        for z in (28, 45, 60):
            buf.box(x, y, z, 16-z/8, 3, 1.2, MAT["metal"])
    elif zone == "airbase":
        for dy in (-50, 0, 50):
            buf.box(x, y+dy, 0, 68, 34, 10, MAT["metal"])
            barrel(x, y+dy, 68, 34, 10, 17, MAT["roof_flat"])
    elif zone == "danube_flats":
        buf.box(x, y, 0, 70, 40, 17, MAT["brick"])
        for dx in (-22, 22):
            buf.cyl(x+dx, y+25, 0, 8, 25, MAT["metal"], 24)
    elif zone == "budahills":
        for z, r, h in ((0, 12, 20), (20, 9, 12), (32, 6, 9)):
            buf.cyl(x, y, z, r, h, MAT["landmark"], 24)
        buf.cyl(x, y, 41, 9, 3, MAT["roof"], 24)
    elif zone == "gellert":
        for dx, dy, sx, sy in ((0, -24, 80, 5), (-37.5, 0, 5, 48), (37.5, 0, 5, 48)):
            buf.box(x+dx, y+dy, 8, sx, sy, 13, MAT["landmark"])
        buf.box(x, y-15, 8, 18, 12, 25, MAT["landmark"])
    elif zone == "m0":
        buf.box(x, y, 9, 120, 7, 2, MAT["metal"])
        for dx in (-55, 55):
            buf.box(x+dx, y, 0, 5, 6, 9, MAT["metal"])
    buf.finish()


def make_world():
    manifest = {}
    for m in WORLD["maps"]:
        zone = m["id"]
        c = collection(f"{m['order']:02d}_{m['bsp']}__LOCAL_ORIGIN_{m['rect'][0]}_{m['rect'][1]}")
        r = m["rect"]
        ground = MeshBuilder(m["bsp"]+" / Source-safe cell", c)
        ground.box((r[0]+r[2])/2, (r[1]+r[3])/2, -0.5, r[2]-r[0], r[3]-r[1], 0.48,
                   MAT[m["kind"]+"_ground"])
        for a, b in zip([(r[0], r[1]), (r[2], r[1]), (r[2], r[3]), (r[0], r[3])],
                        [(r[2], r[1]), (r[2], r[3]), (r[0], r[3]), (r[0], r[1])]):
            ground.ribbon(a, b, 1.2, 0.04, MAT["border"])
        # Every cell is <=600 m, leaving Source coordinate reserve on each side.
        band_width = min(355, r[2]-r[0]-12)
        ground.box(r[0]+band_width/2+6, r[3]-15, 0.04, band_width, 27, 0.08, MAT["header"])
        ground.finish()
        text(m["bsp"]+" dimensions", f"{m['order']:02d} {m['bsp']} | {r[2]-r[0]} x {r[3]-r[1]} m | {round((r[2]-r[0])*K)} x {round((r[3]-r[1])*K)} HU",
             (r[0]+12, r[3]-23, 0.18), 13, c)
        records, imported = buildings(zone)
        arch = MeshBuilder(m["bsp"]+" / compressed architecture", c)
        for record in records:
            add_building(arch, zone, record)
        obj = arch.finish()
        roads = build_roads(zone, c)
        custom_landmarks(zone, r, c)
        manifest[zone] = {
            "bsp": m["bsp"], "source_building_footprints": imported,
            "masterplan_building_volumes": len(records),
            "reduction": f"{imported}:{len(records)}" if imported else "0:0",
            "render_mesh_polygons": len(obj.data.polygons) if obj else 0,
            "street_mesh_polygons": len(roads.data.polygons) if roads else 0,
            "working_size_m": [r[2]-r[0], r[3]-r[1]],
            "working_size_hu": [round((r[2]-r[0])*K), round((r[3]-r[1])*K)],
            "district_distance_scale": round(district_scale(zone), 4),
            "building_footprint_restore_scale": round(architecture_scale(zone), 4),
            "local_origin_world_m": [(r[0]+r[2])/2, (r[1]+r[3])/2, 0],
        }
        print("ZONE", zone, imported, "->", len(records), "buildings", flush=True)

    # Continuous geography and gameplay boundaries, separate from district meshes.
    c = collection("90_CONTINUOUS_DANUBE_AND_NEXUS")
    b = MeshBuilder("Continuous authored context", c)
    river = WORLD["features"]["danube_pest_bank"] + list(reversed(WORLD["features"]["danube_buda_bank"]))
    b.face([(x, y, 0.13) for x, y in river], MAT["water"])
    nexus = WORLD["zones"][0]["rect"]
    b.prism([(nexus[0], nexus[1]), (nexus[2], nexus[1]), (nexus[2], nexus[3]), (nexus[0], nexus[3])], -0.3, 0, MAT["combine"])
    apron = WORLD["zones"][0]["citadel_apron"]
    b.cyl(0, 0, -0.1, apron["radius_m"], 0.25, MAT["combine"], 96)
    for i in range(28):
        if i in (18, 19, 20):
            continue
        a = math.tau*i/28
        x, y = 200*math.cos(a), 200*math.sin(a)
        pts = [(x-1.2, y-1.2), (x+1.2, y+1.2)]
        b.box(x, y, 0, 8, 3, 13, MAT["metal"])
    b.finish()

    c = collection("91_TRANSITIONS__SEE_HAMMER_JSON")
    for t in WORLD["transitions"]:
        b = MeshBuilder(t["id"], c)
        x, y = t["at"]
        b.cyl(x, y, 0, 7, 2.5, MAT["gate"], 20)
        b.finish()
        text(t["id"]+" label", t["id"].upper(), (x+8, y+4, 3.0), 10, c)

    c = collection("92_CITADEL__600M_REFERENCE")
    b = MeshBuilder("One Citadel / full-height planning reference", c)
    b.cyl(0, 0, 0, 72, 38, MAT["metal"], 16, 60)
    b.cyl(0, 0, 38, 49, 110, MAT["combine"], 16, 39)
    b.cyl(0, 0, 148, 38, 360, MAT["metal"], 16, 25)
    b.cyl(0, 0, 508, 45, 45, MAT["combine"], 12, 36)
    b.cyl(0, 0, 553, 33, 47, MAT["metal"], 12, 17)
    for z in (170, 290, 410):
        b.cyl(0, 0, z, 52, 5, MAT["combine"], 16, 52)
    b.finish()
    text("Citadel height label", "CITADEL H=600 m / AXIS 0,0", (85, 0, 5), 17, c)

    tcol = collection("99_WORLD_STAGE")
    b = MeshBuilder("Presentation base", tcol)
    b.box(-100, 0, -1.4, 4600, 4500, 0.5, MAT["metal"])
    b.finish()
    return manifest


def camera(name, location, target, ortho):
    data = bpy.data.cameras.new(name)
    obj = bpy.data.objects.new(name, data)
    scene.collection.objects.link(obj)
    obj.location = location
    obj.rotation_euler = (Vector(target)-obj.location).to_track_quat("-Z", "Y").to_euler()
    data.type = "ORTHO"
    data.ortho_scale = ortho
    data.clip_end = 20000
    return obj


def render(master_iso, top):
    scene.render.engine = "CYCLES"
    scene.cycles.device = "CPU"
    scene.cycles.samples = 16
    scene.cycles.use_denoising = True
    scene.render.image_settings.file_format = "PNG"
    scene.render.resolution_percentage = 100
    scene.world.use_nodes = True
    bg = scene.world.node_tree.nodes.get("Background")
    bg.inputs["Color"].default_value = (0.45, 0.5, 0.56, 1)
    bg.inputs["Strength"].default_value = 0.65
    light = bpy.data.lights.new("Master sun", "SUN")
    light.energy = 2.3
    light.angle = math.radians(18)
    sun = bpy.data.objects.new("Master sun", light)
    scene.collection.objects.link(sun)
    sun.rotation_euler = (math.radians(28), math.radians(-22), math.radians(-35))
    scene.view_settings.view_transform = "Standard"
    scene.camera = top
    scene.render.resolution_x = 4096
    scene.render.resolution_y = 4096
    scene.render.filepath = str(OUT / "city27_masterplan_top.png")
    bpy.ops.render.render(write_still=True)
    scene.camera = master_iso
    scene.render.resolution_x = 3600
    scene.render.resolution_y = 2750
    scene.render.filepath = str(OUT / "city27_masterplan_iso.png")
    bpy.ops.render.render(write_still=True)


def export_spec(manifest):
    maps = []
    for m in WORLD["maps"]:
        r = m["rect"]
        center = [(r[0]+r[2])/2, (r[1]+r[3])/2, 0]
        maps.append({**m, **manifest[m["id"]], "local_origin_world_m": center,
                     "local_bounds_hu": [[-round((r[2]-r[0])*K/2), -round((r[3]-r[1])*K/2), 0],
                                          [round((r[2]-r[0])*K/2), round((r[3]-r[1])*K/2), 0]],
                     "margin_to_coordinate_edge_hu": [16384-round((r[2]-r[0])*K/2), 16384-round((r[3]-r[1])*K/2)]})
    transitions = []
    origins = {m["id"]: m["local_origin_world_m"] for m in maps}
    for t in WORLD["transitions"]:
        ends = []
        for side in ("a", "b"):
            origin = origins[t[side]]
            ends.append({"map": t[side], "bsp": MAPS[t[side]]["bsp"],
                         "entry_world_m": t["at"], "local_origin_world_m": origin,
                         "entry_local_hu": [round((t["at"][0]-origin[0])*K, 2), round((t["at"][1]-origin[1])*K, 2), 0]})
        transitions.append({**t, "ends": ends})
    spec = {"version": 3, "purpose": "Fully 3D, geometrically compressed Source/Hammer masterplan",
            "units": {"scene": "metres", "hu_per_m": K, "source_coordinate_limit_hu": [-16384, 16384],
                      "hard_map_span_m": 32768/K, "safe_playable_limit_m": PLAY_LIMIT,
                      "conversion": "HammerLocal = (PlanWorld - local_origin_world_m) * 52.49"},
            "blender_file": "city27_masterplan.blend", "maps": maps, "transitions": transitions,
            "zones": WORLD["zones"], "citadel": WORLD["citadel"],
            "generation_note": "This is the layout reference, not production brush counts or a compiled map. Geometry is merged by district in Blender; preserve architectural grammar during Hammer regularisation."}
    (OUT / "city27_hammer_layout.json").write_text(json.dumps(spec, ensure_ascii=False, indent=2), encoding="utf-8")

    lines = ["# Сити-27 — рабочая 3D-схема для Hammer (v3)", "",
             "**Это новая основа вместо прежнего OSM-атласа 1:1.** Композиция выполнена объёмной геометрией в `city27_masterplan.blend`. Мировая .blend-сцена не является одной гигантской VMF.", "",
             "## Жёсткие и рабочие пределы Source", "",
             f"- Координаты геометрии: **−16384…+16384 HU** по каждой оси, полный диапазон 32768 HU.",
             f"- В проектном масштабе **1 м = {K} HU**. Теоретический предел — {32768/K:.2f} м на ось.",
             f"- Игровая клетка ограничена **{PLAY_LIMIT} × {PLAY_LIMIT} м = {round(PLAY_LIMIT*K)} × {round(PLAY_LIMIT*K)} HU**.",
             f"- Даже максимальная клетка оставляет **{16384-round(PLAY_LIMIT*K/2)} HU запаса с каждой стороны** до координатного предела для неба/оболочки и технико-визуальных элементов.",
             "- Центр клетки в мировой схеме превращается в `(0,0,0)` локальной VMF. Никакие глобальные координаты трёхкилометрового мира напрямую в Hammer не вставляются.",
             "- Локальные XY на планах — метры Blender; перевод: `HammerLocal = (PlanWorld − local_origin_world_m) × 52.49`.", "",
             "## Что именно сжато", "",
             "- Сохранены реальные улицы, взаимное положение кварталов, дворы, вода и отдельные узнаваемые силуэты.",
             "- Плотность мелкой застройки снижена выборочно: город 28 %, пояс 22 %, пустошь 13 % исходных контуров; фермы и особые объекты сохраняются плотнее.",
             "- **Дома не сжимаются до игрушечных коробок:** сжимаются расстояния и число рядов, а футпринт сохранённого здания частично возвращается к человеческому масштабу на коэффициент `building_footprint_restore_scale` из JSON. Этаж остаётся 3.0–3.35 м.",
             "- Здания — не плоские полигоны: цоколь, этажная масса, карнизы, оконные ниши, мансардные или плоские кровли. Дебаркадер, мост, колоннады, дворцы, газгольдеры и прочие ориентиры заданы отдельными объёмами.",
             "- Для окончательной VMF соседние мелкие объёмы объединяются в фасадные секции; мелкие фасадные детали переходят в `func_detail`, визуальный дальний город — в skybox. Количество Blender-полигонов не является бюджетом brush/BSP.", "",
             "## Районы и локальные границы", "",
             "| № | BSP | Мировой rect, м | Рабочий размер, HU | Запас до ±16384 по X/Y | Объёмы |", "|---:|---|---|---|---|---:|"]
    for m in maps:
        lines.append(f"| {m['order']:02d} | `{m['bsp']}` | `{m['rect']}` | {m['working_size_hu'][0]} × {m['working_size_hu'][1]} | {m['margin_to_coordinate_edge_hu'][0]} / {m['margin_to_coordinate_edge_hu'][1]} | {m['masterplan_building_volumes']} / {m['source_building_footprints']} |")
    lines += ["", "## Переходы: парные локальные точки", "",
              "Точки — центры будущих `trigger_changelevel`, не готовые проёмы. Перед greybox для каждого подбирается реальная улица/проход; landmarks ставятся зеркально по направлению движения.", "",
              "| № | Переход | A: локальные HU | B: локальные HU |", "|---:|---|---|---|"]
    for t in transitions:
        a, b = t["ends"]
        lines.append(f"| {t['id'][1:]} | {t['a']} ↔ {t['b']} | `{a['bsp']}` `{a['entry_local_hu']}` | `{b['bsp']}` `{b['entry_local_hu']}` |")
    lines += ["", "## Как переносить в Hammer", "",
              "1. Открыть коллекцию нужной карты в `city27_masterplan.blend` и её запись в `city27_hammer_layout.json`.",
              "2. Вычесть `local_origin_world_m`; перевести метры в HU и проверить `local_bounds_hu`.",
              "3. Начать с landmass, улиц и главных фасадных периметров. Дворы и проходы делать сразу, а не после заполнения карты декорациями.",
              "4. Герметичный world только из простых выпуклых brush; архитектурные мелочи и кровли — `func_detail`/модели. Blender не импортировать в BSP как единый многоугольный меш.",
              "5. Сохранять высоту этажа около 3.0–3.35 м, Budapest-масштаб карнизов/мансард и тип квартала. Не вытягивать здания обратно к OSM 1:1.",
              "6. Переходы проверить парой в игре: одинаковое направление выхода, буфер против ping-pong, вид Цитадели и продолжение дороги.",
              "7. Сначала `c27_station`, затем `c27_centre`, `c27_plaza`, затем пробная пустошь `road + m0 + fields`. Остальные карты после приёмки масштаба.", "",
              "## Цитадель", "",
              "В мировой сцене показана полноразмерная 600-метровая референсная башня. В игровых skybox: scale 64, модель около 492.09 HU; `sky_camera` вычисляется по формуле из основного мирового плана. Фактическая видимость проверяется на greybox.", "",
              "## Проверка", "",
              "Схема не утверждает готовность BSP: число брашей, T-junction, VIS, FPS, no-leak и проходы появятся только после настоящего Hammer-greybox. Этот файл задаёт рабочие размеры, границы, локальные координаты и архитектурную композицию."]
    (OUT / "city27_hammer_build_spec.md").write_text("\n".join(lines), encoding="utf-8")


manifest = make_world()
export_spec(manifest)
iso = camera("ISO entire compressed world", (3250, -4050, 3150), (-120, 50, 100), 4050)
top = camera("TOP Hammer geometry", (-120, 30, 5000), (-120, 30, 0), 3320)
scene.camera = iso
bpy.ops.wm.save_as_mainfile(filepath=str(OUT / "city27_masterplan.blend"))
render(iso, top)
scene.camera = iso
for screen in bpy.data.screens:
    for area in screen.areas:
        if area.type == "VIEW_3D":
            area.spaces.active.region_3d.view_perspective = "CAMERA"
            area.spaces.active.shading.type = "SOLID"
            area.spaces.active.shading.color_type = "MATERIAL"
            area.spaces.active.shading.show_cavity = True
bpy.ops.wm.save_as_mainfile(filepath=str(OUT / "city27_masterplan.blend"))
print("DONE", OUT, flush=True)
