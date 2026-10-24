"""Graphics showcase map: the stock content of HL2 / EP1 / EP2 under the deferred renderer.

    python make_showcase.py <out.vmf> [--no-labels]

Twelve rooms open to the sky (3072 x 3072 each, walls up to the sky, doors between neighbours) in
a grid of 4 x 3 - row 0 (south) to row 2 (north), column 0 (west) to 3 (east):

    row 2:  water & glass   | hall (indoor, lamps) | materials B      | fences, decals, canopy
    row 1:  fire, explosions| NPCs in booths       | big NPCs, vehicles| shooting range
    row 0:  hub (spawn)     | materials A          | props            | terrain, trees, grass

Materials are picked from the stock VMTs (each folder's materials evenly, every feature -
$detail, $bumpmap, $ssbump, $envmap, $selfillum, $alphatest, $translucent - covered), the models
of the props room by their bounding boxes (read from the .mdl headers). Labels are Mapbase's
vgui_text_display. Written next to the map: <out>_legend.txt (what stands where) and
<out>_steps.txt (camera steps for run_game.ps1).
"""
import math
import os
import random
import re
import struct
import sys

sys.path.insert(0, r"E:\SourceModding\HL2RPMSOURCE\.claude\skills\hl2rpm-hammer\scripts")
from vmf import VMF, Node, parse  # noqa: E402
import new_test_map  # noqa: E402

REF = r"E:\SourceModding\UsefulStuff\Source sdk 2013 HL2 unpacked vpk"
GAMES = ["ep2", "episodic", "hl2"]

out = sys.argv[1]
LABELS = "--no-labels" not in sys.argv
random.seed(20261023)

m = VMF(parse(new_test_map.HEADER))
m.path = out
m.world.set("skyname", "sky_day01_01")

S = 3072        # room interior
T = 32          # wall thickness
ZT = 2048       # sky
ZB = -400       # bottom of the floors (pools are dug into them)
OX = -(4 * S + 3 * T) // 2
OY = -(3 * S + 2 * T) // 2
DOOR_W, DOOR_H = 320, 320

NODRAW = "TOOLS/TOOLSNODRAW"
SKY = "TOOLS/TOOLSSKYBOX"
NPCCLIP = "TOOLS/TOOLSNPCCLIP"
WALL = "CONCRETE/CONCRETEWALL001A"
PAD = "CONCRETE/CONCRETEFLOOR033A"

legend = []
steps = []


def room(c, r):
    x0 = OX + c * (S + T)
    y0 = OY + r * (S + T)
    return x0, y0, x0 + S, y0 + S


def box(a, b, mat, **kw):
    check_material(mat)
    for fm in (kw.get("face_materials") or {}).values():
        check_material(fm)
    if a[0] < b[0] and a[1] < b[1] and a[2] < b[2]:
        return m.add_box(a, b, mat, **kw)


def detail(a, b, mat, **kw):
    check_material(mat)
    for fm in (kw.get("face_materials") or {}).values():
        check_material(fm)
    if a[0] < b[0] and a[1] < b[1] and a[2] < b[2]:
        return m.add_brush_entity("func_detail", [m.make_box(a, b, mat, **kw)])


MISSING = set()


def check_model(mdl):
    if mdl and mdl.lower().endswith(".mdl") and not mdl_exists(mdl.lower()):
        MISSING.add("model " + mdl)
        return False
    return True


def check_material(mat):
    if mat and not mat.upper().startswith("TOOLS/") and read_vmt(mat.lower()) is None and not mat.upper().startswith("GLASS/REFLECTIVE"):
        MISSING.add("material " + mat)


def ent(cls, pos, **kw):
    if "model" in kw and not check_model(kw["model"]):
        return None
    # (vbsp refuses a prop_static of a model not compiled as a static prop - and the Mapbase
    # content mounted first may replace the stock model: dynamic unless the stock one is static
    # and it is no physics model of the props / items folders)
    if cls == "prop_static":
        info = mdl_info(kw["model"].lower())
        if not info or not (info[2] & 0x10) or "/items/" in kw["model"].lower() or "physics" in kw["model"].lower():
            cls = "prop_dynamic"
            kw.pop("fademindist", None)
            kw.pop("fadescale", None)
            kw.pop("skin", None)
    return m.add_entity(cls, pos, **kw)


def label(pos, text, yaw, size=96, color="255 255 255"):
    if not LABELS:
        return
    # (the font is a fixed number of pixels: a low resolution makes the letters big on the panel)
    ent("vgui_text_display", pos, angles="0 %g 0" % yaw, message=text[:60], alignment="4",
        resolution="96", textsize=str(size), rendercolor=color, spawnflags="0")


def floor_box(x0, y0, x1, y1, mat, top=0):
    box((x0, y0, ZB), (x1, y1, top), NODRAW, face_materials={"top": mat})


# ---------------------------------------------------------------- the stock content
def read_vmt(rel):
    for g in GAMES:
        p = os.path.join(REF, g, "materials", rel + ".vmt")
        if os.path.exists(p):
            try:
                t = open(p, encoding="latin-1").read()
            except OSError:
                return None
            return re.sub(r"//[^\n]*", "", t)
    return None


def vmt_shader(t):
    mm = re.match(r'\s*"?([A-Za-z0-9_]+)', t)
    return mm.group(1).lower() if mm else ""


def flag(t, key):
    return re.search(r'"?' + re.escape(key) + r'"?\s+"?1', t, re.I) is not None


FEATURES = ["$detail", "$bumpmap", "$ssbump", "$envmap", "$selfillum", "$parallaxmap", "$envmapmask",
            "$normalmapalphaenvmapmask", "$basealphaenvmapmask"]


def list_folder(folder):
    names = set()
    for g in GAMES:
        d = os.path.join(REF, g, "materials", folder)
        if os.path.isdir(d):
            for f in os.listdir(d):
                if f.lower().endswith(".vmt"):
                    names.add(f[:-4].lower())
    return sorted(names)


def feature_set(t):
    fs = set()
    for k in FEATURES:
        if re.search(r'"?' + re.escape(k) + r'"?\s', t, re.I):
            fs.add(k)
    return fs


def pick_materials(folder, n, want=("lightmappedgeneric",), translucent=False, alphatest=None, need=None):
    """n materials of a folder, evenly over the alphabet but every feature first (a bucket each)."""
    cands = []
    for name in list_folder(folder):
        rel = folder + "/" + name
        t = read_vmt(rel)
        if not t or vmt_shader(t) not in want:
            continue
        if not re.search(r"\$basetexture", t, re.I) or flag(t, "$decal") or flag(t, "$additive"):
            continue
        is_tr = flag(t, "$translucent")
        if is_tr != translucent:
            continue
        if alphatest is not None and flag(t, "$alphatest") != alphatest:
            continue
        if "_dx6" in name or "_dx7" in name or "dx80" in name or "_nodraw" in name or "_noprop" in name:
            continue
        fs = feature_set(t)
        if need and not (fs & set(need)):
            continue
        cands.append((rel, fs))
    if len(cands) <= n:
        return cands
    chosen = []
    used = set()
    # one of each feature present in the folder
    for k in FEATURES:
        with_k = [c for c in cands if k in c[1] and c[0] not in used]
        if with_k and len(chosen) < n:
            c = with_k[len(with_k) // 2]
            chosen.append(c)
            used.add(c[0])
    rest = [c for c in cands if c[0] not in used]
    k = n - len(chosen)
    if k > 0 and rest:
        step = len(rest) / float(k)
        for i in range(k):
            chosen.append(rest[int(i * step)])
    return sorted(chosen)


def mdl_exists(rel):
    for g in GAMES:
        if os.path.exists(os.path.join(REF, g, rel)):
            return True
    return False


def mdl_info(rel):
    """(hull min, hull max, flags) of a model, or None"""
    for g in GAMES:
        p = os.path.join(REF, g, rel)
        if os.path.exists(p):
            with open(p, "rb") as f:
                h = f.read(160)
            if len(h) < 160 or h[:4] != b"IDST":
                return None
            hmin = struct.unpack_from("<3f", h, 104)
            hmax = struct.unpack_from("<3f", h, 116)
            vmin = struct.unpack_from("<3f", h, 128)
            vmax = struct.unpack_from("<3f", h, 140)
            flags = struct.unpack_from("<i", h, 152)[0]
            if all(abs(v) < 1e-3 for v in vmin + vmax):
                vmin, vmax = hmin, hmax
            return vmin, vmax, flags
    return None


def _cstr(data, off):
    return data[off:data.index(b"\0", off)].decode("latin-1")


def model_materials_ok(rel):
    """False if a material the model names is in none of its material folders (the stock content
    has a few such models - car001b_cluster02, transponder - pink in the stock game too)"""
    for g in GAMES:
        p = os.path.join(REF, g, rel)
        if os.path.exists(p):
            data = open(p, "rb").read()
            numtex, texidx, numcd, cdidx = struct.unpack_from("<4i", data, 204)
            cds = [_cstr(data, struct.unpack_from("<i", data, cdidx + i * 4)[0]) for i in range(numcd)]
            for i in range(numtex):
                base = texidx + i * 64
                name = _cstr(data, base + struct.unpack_from("<i", data, base)[0])
                if not any(read_vmt((d + name).replace("\\", "/").lower()) is not None for d in cds):
                    return False
            return True
    return False


def list_models(folder):
    names = set()
    for g in GAMES:
        d = os.path.join(REF, g, "models", folder)
        if os.path.isdir(d):
            for f in os.listdir(d):
                fl = f.lower()
                if not fl.endswith(".mdl"):
                    continue
                if any(k in fl for k in ("_anims", "_animations", "_gestures", "_postures", "_shared", "-lod", "_lod",
                                         "skybox", "_gib", "gib0", "gib1", "chunk", "_physics", "_ref", "_reference",
                                         "_break", "_dynamic", "lowpoly", "_low")):
                    continue
                names.add("models/" + folder + "/" + fl)
    return sorted(names)


# ---------------------------------------------------------------- shell: floors, walls, sky
for c in range(4):
    for r in range(3):
        x0, y0, x1, y1 = room(c, r)
        if (c, r) == (0, 2):
            continue  # the water room digs its own floor
        if (c, r) == (3, 0):
            floor_box(x0, y0, x1, y1, "NATURE/DIRTFLOOR006A", top=-32)  # under the terrain displacements
            continue
        floor_box(x0, y0, x1, y1, {(0, 0): "CONCRETE/CONCRETEFLOOR033A", (1, 0): "CONCRETE/CONCRETEFLOOR001A",
                                   (2, 0): "CONCRETE/CONCRETEFLOOR028A", (0, 1): "CONCRETE/CONCRETEFLOOR005A",
                                   (1, 1): "TILE/TILEFLOOR012A", (2, 1): "CONCRETE/CONCRETEFLOOR028A",
                                   (3, 1): "CONCRETE/CONCRETEFLOOR011A", (1, 2): "WOOD/WOODFLOOR001A",
                                   (2, 2): "CONCRETE/CONCRETEFLOOR001A", (3, 2): "CONCRETE/CONCRETEFLOOR019A"}[(c, r)])

XA, YA = OX - T, OY - T
XB, YB = OX + 4 * S + 3 * T + T, OY + 3 * S + 2 * T + T
box((XA, YA, ZB - 16), (XB, YB, ZB), NODRAW)                      # bottom
box((XA, YA, ZT), (XB, YB, ZT + 16), SKY)                          # sky
box((XA, YA, ZB), (OX, YB, ZT), WALL)                              # west
box((XB - T, YA, ZB), (XB, YB, ZT), WALL)                          # east
box((OX, YA, ZB), (XB - T, OY, ZT), WALL)                          # south
box((OX, YB - T, ZB), (XB - T, YB, ZT), WALL)                      # north


def wall_x_door(x, ylo, yhi, door_y):
    """a wall along y at x..x+T from ylo to yhi with a door centered at door_y"""
    a, b = door_y - DOOR_W / 2, door_y + DOOR_W / 2
    box((x, ylo, ZB), (x + T, a, ZT), WALL)
    box((x, b, ZB), (x + T, yhi, ZT), WALL)
    box((x, a, ZB), (x + T, b, 0), WALL)
    box((x, a, DOOR_H), (x + T, b, ZT), WALL)


def wall_y_door(y, xlo, xhi, door_x):
    a, b = door_x - DOOR_W / 2, door_x + DOOR_W / 2
    box((xlo, y, ZB), (a, y + T, ZT), WALL)
    box((b, y, ZB), (xhi, y + T, ZT), WALL)
    box((a, y, ZB), (b, y + T, 0), WALL)
    box((a, y, DOOR_H), (b, y + T, ZT), WALL)


for r in range(3):
    for c in range(3):
        x0, y0, x1, y1 = room(c, r)
        wall_x_door(x1, y0, y1, (y0 + y1) / 2)
for r in range(2):
    for c in range(4):
        x0, y0, x1, y1 = room(c, r)
        wall_y_door(y1, x0 - (T if c > 0 else 0), x1 + (T if c < 3 else 0), (x0 + x1) / 2)
# (the corners where the walls cross)
for r in range(2):
    for c in range(3):
        x0, y0, x1, y1 = room(c, r)
        box((x1, y1, ZB), (x1 + T, y1 + T, ZT), WALL)

ent("light_environment", (OX + 256, OY + 256, 1800), angles="-52 35 0", pitch="-52",
    _light="255 238 215 380", _ambient="135 160 205 70", _lightHDR="-1 -1 -1 1", _lightscaleHDR="1",
    _ambientHDR="-1 -1 -1 1", _AmbientScaleHDR="1", SunSpreadAngle="4")

# cubemaps: a few per room (built with buildcubemaps in the game, if wanted)
for c in range(4):
    for r in range(3):
        x0, y0, x1, y1 = room(c, r)
        for fx, fy in ((0.25, 0.25), (0.75, 0.25), (0.25, 0.75), (0.75, 0.75)):
            ent("env_cubemap", (x0 + S * fx, y0 + S * fy, 96 if (c, r) != (1, 2) else 128), cubemapsize="0")


def cam(name, x, y, z, pitch, yaw, extra=""):
    """a camera step; z is the height of the eye (setpos places the feet: the eye is 64 above)"""
    steps.append("shot|setpos %d %d %d; setang %d %d 0%s  // %s" % (x, y, z - 64, pitch, yaw, ("; " + extra) if extra else "", name))


# ---------------------------------------------------------------- (0,0) hub
x0, y0, x1, y1 = room(0, 0)
cx, cy = (x0 + x1) / 2, (y0 + y1) / 2
ent("info_player_start", (cx, cy, 8), angles="0 0 0")
# (Mapbase: the stunstick is a weapon of the player here, not a battery)
ent("hl2_gamerules", (cx, cy + 64, 64), SetStunstickPickupBehavior="3")
label((cx + 300, cy, 200), "HL2RPM graphics showcase", 180, size=256, color="255 220 160")
for (dx, dy, text, yaw) in ((S / 2 - 64, 0, "materials A ->", 180), (0, S / 2 - 64, "fire, explosions ->", 270)):
    label((cx + dx, cy + dy, DOOR_H + 64), text, yaw, size=200)
legend.append("hub: spawn %d %d" % (cx, cy))


# ---------------------------------------------------------------- materials rooms
def materials_room(c, r, rows, title):
    x0, y0, x1, y1 = room(c, r)
    ROW = 460
    CELL = 160
    for ri, (rname, mats) in enumerate(rows):
        rx = x0 + 192 + ri * ROW
        for ci, (mat, fs) in enumerate(mats):
            yy = y0 + 192 + ci * CELL
            up = mat.upper()
            # the floor tile (2 units over the floor) and the wall panel across its middle
            detail((rx, yy, 0), (rx + 384, yy + 128, 2), PAD, face_materials={"top": up})
            detail((rx + 188, yy, 2), (rx + 196, yy + 128, 130), up)
            short = mat.split("/", 1)[1]
            tags = "".join(t for t, k in (("D", "$detail"), ("B", "$bumpmap"), ("S", "$ssbump"), ("E", "$envmap"),
                                          ("I", "$selfillum"), ("P", "$parallaxmap")) if k in fs)
            label((rx + 184, yy + 64, 170), short + (" [" + tags + "]" if tags else ""), 180, size=128)
            legend.append("%s row %d (%s) cell %d at %d %d: %s %s" % (title, ri, rname, ci, rx + 192, yy + 64, mat, sorted(fs)))
        n = len(mats)
        cam("%s %s from -x, first half" % (title, rname), rx - 200, y0 + 192 + n * CELL * 0.25, 40, 10, 0)
        cam("%s %s from -x, second half" % (title, rname), rx - 200, y0 + 192 + n * CELL * 0.75, 40, 10, 0)
        cam("%s %s from +x (against the sun)" % (title, rname), rx + 600, y0 + 192 + n * CELL * 0.5, 40, 10, 180)
    # a lamp over each row (reflections of lamps at night)
    for ri in range(len(rows)):
        ent("light", (x0 + 192 + ri * ROW + 96, y0 + 260, 170), _light="255 225 180 420", _constant_attn="0",
            _linear_attn="0", _quadratic_attn="1", _fifty_percent_distance="0", _zero_percent_distance="0", style="0")


N = 16
rows_a = [(f, pick_materials(f, N)) for f in ("brick", "concrete", "plaster", "stone", "tile", "wood")]
materials_room(1, 0, rows_a, "materials A")

rows_b = [("metal", pick_materials("metal", N)),
          ("building_template", pick_materials("building_template", N)),
          ("nature", pick_materials("nature", N)),
          ("props/composite", (pick_materials("props", 8) + pick_materials("composite", 8))[:N]),
          ("glass", pick_materials("glass", N, translucent=True)),
          ("special", [])]
# the features the folders above may have missed: self-illuminated, ssbump, parallax maps, alpha test
special = []
for folder in ("lights", "metal", "building_template", "brick", "concrete", "stone", "plaster", "nature", "tile", "wood"):
    for mat, fs in pick_materials(folder, 40):
        if fs & {"$ssbump", "$parallaxmap", "$selfillum", "$envmapmask", "$normalmapalphaenvmapmask"} and len(special) < N:
            if mat not in [s[0] for s in special] and not any(mat == x[0] for _, rr in rows_a + rows_b for x in rr):
                special.append((mat, fs))
rows_b[5] = ("special", special)
materials_room(2, 2, rows_b, "materials B")


# ---------------------------------------------------------------- (1,1) NPCs in booths
x0, y0, x1, y1 = room(1, 1)
NPCS = [
    ("npc_citizen", {"citizentype": "1"}, "citizen (downtrodden)"),
    ("npc_citizen", {"citizentype": "4", "model": "models/Humans/Group01/male_07.mdl"}, "citizen male_07"),
    ("npc_citizen", {"citizentype": "4", "model": "models/Humans/Group01/female_01.mdl"}, "citizen female_01"),
    ("npc_citizen", {"citizentype": "4", "model": "models/Humans/Group02/male_04.mdl"}, "refugee male_04"),
    ("npc_citizen", {"citizentype": "4", "model": "models/Humans/Group02/female_04.mdl"}, "refugee female_04"),
    ("npc_citizen", {"citizentype": "4", "model": "models/Humans/Group03/male_02.mdl", "additionalequipment": "weapon_smg1"}, "rebel male_02"),
    ("npc_citizen", {"citizentype": "4", "model": "models/Humans/Group03/female_03.mdl", "additionalequipment": "weapon_ar2"}, "rebel female_03"),
    ("npc_citizen", {"citizentype": "4", "model": "models/Humans/Group03m/male_01.mdl", "spawnflags": "131072"}, "medic male_01"),
    ("npc_citizen", {"citizentype": "4", "model": "models/odessa.mdl"}, "odessa"),
    ("npc_alyx", {"additionalequipment": "weapon_alyxgun"}, "alyx"),
    ("npc_barney", {"additionalequipment": "weapon_ar2"}, "barney"),
    ("npc_kleiner", {}, "kleiner"),
    ("npc_eli", {}, "eli"),
    ("npc_mossman", {}, "mossman"),
    ("npc_breen", {}, "breen"),
    ("npc_magnusson", {}, "magnusson"),
    ("npc_monk", {"additionalequipment": "weapon_annabelle"}, "monk"),
    ("npc_gman", {}, "gman"),
    ("npc_fisherman", {}, "fisherman"),
    ("npc_vortigaunt", {}, "vortigaunt"),
    ("npc_vortigaunt", {"model": "models/vortigaunt_doctor.mdl"}, "vortigaunt doctor"),
    ("npc_vortigaunt", {"model": "models/vortigaunt_blue.mdl"}, "vortigaunt blue"),
    ("npc_metropolice", {"additionalequipment": "weapon_stunstick"}, "metrocop stunstick"),
    ("npc_metropolice", {"additionalequipment": "weapon_pistol", "manhacks": "0"}, "metrocop pistol"),
    ("npc_combine_s", {"model": "models/combine_soldier.mdl", "additionalequipment": "weapon_smg1"}, "combine soldier"),
    ("npc_combine_s", {"model": "models/combine_super_soldier.mdl", "additionalequipment": "weapon_ar2"}, "combine elite"),
    ("npc_combine_s", {"model": "models/combine_soldier_prisonguard.mdl", "additionalequipment": "weapon_shotgun"}, "prison guard"),
    ("npc_stalker", {}, "stalker"),
    ("npc_zombie", {}, "zombie"),
    ("npc_zombie_torso", {}, "zombie torso"),
    ("npc_fastzombie", {}, "fast zombie"),
    ("npc_poisonzombie", {}, "poison zombie"),
    ("npc_zombine", {}, "zombine"),
    ("npc_headcrab", {}, "headcrab"),
    ("npc_headcrab_fast", {}, "fast headcrab"),
    ("npc_headcrab_black", {}, "poison headcrab"),
    ("npc_antlion", {}, "antlion"),
    ("npc_antlion", {"spawnflags": "262144"}, "antlion worker"),
    ("npc_turret_floor", {}, "floor turret"),
    ("npc_rollermine", {}, "rollermine"),
    ("npc_manhack", {}, "manhack"),
    ("npc_cscanner", {}, "city scanner"),
    ("npc_clawscanner", {}, "claw scanner"),
    ("npc_crow", {}, "crow"),
    ("npc_pigeon", {}, "pigeon"),
    ("npc_barnacle", {"ceiling": True}, "barnacle"),
    ("npc_turret_ceiling", {"ceiling": True, "spawnflags": "32"}, "ceiling turret"),
    ("npc_combine_camera", {"ceiling": True}, "combine camera"),
]
BW, BD, BH = 176, 232, 256
PER_ROW = 16
for i, (cls, keys, name) in enumerate(NPCS):
    row, col = divmod(i, PER_ROW)
    yb = y1 - 96 - row * 1000            # back wall of the row
    bx0 = x0 + 128 + col * (BW + 8)
    bx1 = bx0 + BW
    # side walls, back wall
    detail((bx0 - 8, yb - BD, 0), (bx0, yb, BH), "PLASTER/PLASTERWALL005C")
    if col == PER_ROW - 1 or i == len(NPCS) - 1:
        detail((bx1, yb - BD, 0), (bx1 + 8, yb, BH), "PLASTER/PLASTERWALL005C")
    detail((bx0, yb, 0), (bx1, yb + 16, BH), "PLASTER/PLASTERWALL005C")
    keys = dict(keys)
    ceiling = keys.pop("ceiling", False)
    if ceiling:
        detail((bx0, yb - BD, BH - 16), (bx1, yb, BH), "CONCRETE/CONCRETECEILING001A")
    # NPC clip across the open front
    box((bx0, yb - BD - 8, 0), (bx1, yb - BD, BH), NPCCLIP)
    pos = ((bx0 + bx1) / 2, yb - BD / 2, (BH - 17) if ceiling else 4)
    keys.setdefault("spawnflags", "0")
    # (efficient: no think while not seen? no - they idle; don't drop weapons, don't follow)
    sf = int(keys["spawnflags"]) | 8192 | 512   # 8192 = don't drop weapons, 512 = fade corpse
    keys["spawnflags"] = str(sf)
    keys["targetname"] = "booth_%02d" % i
    if cls in ("npc_turret_ceiling", "npc_combine_camera", "npc_barnacle"):
        ent(cls, pos, angles="0 270 0", **keys)
    else:
        ent(cls, pos, angles="0 270 0", sleepstate="0", **keys)
    label(((bx0 + bx1) / 2, yb - BD - 4, BH + 24), name, 270, size=110)
    legend.append("npc booth %02d (%s) at %d %d: %s %s" % (i, name, pos[0], pos[1], cls, keys))
for row in range((len(NPCS) + PER_ROW - 1) // PER_ROW):
    yb = y1 - 96 - row * 1000
    for k in range(4):
        cam("npc row %d part %d" % (row, k), x0 + 128 + (k + 0.5) * 4 * (BW + 8), yb - BD - 300, 90, 6, 90)
for i, (cls, keys, name) in enumerate(NPCS):
    row, col = divmod(i, PER_ROW)
    yb = y1 - 96 - row * 1000
    bx = x0 + 128 + col * (BW + 8) + BW / 2
    ceiling = keys.get("ceiling", False)
    small = cls in ("npc_headcrab", "npc_headcrab_fast", "npc_headcrab_black", "npc_rollermine", "npc_crow", "npc_pigeon",
                    "npc_zombie_torso", "npc_manhack", "npc_cscanner", "npc_clawscanner")
    eye = 200 if ceiling else (36 if small else 64)
    cam("npc %02d %s face" % (i, name), bx, yb - BD / 2 - (90 if small else 62), eye, 28 if ceiling else (12 if small else 2), 90)

# ---------------------------------------------------------------- (2,1) big NPCs, vehicles
x0, y0, x1, y1 = room(2, 1)
cx, cy = (x0 + x1) / 2, (y0 + y1) / 2
# three yards: combine (north), creatures (south-west), friends and vehicles (south-east)
detail((x0, cy - 16, 0), (x1, cy + 16, 640), "CONCRETE/CONCRETEWALL034A")
detail((cx - 16, y0, 0), (cx + 16, cy - 16, 640), "CONCRETE/CONCRETEWALL034A")
ent("npc_strider", (x0 + 700, cy + 900, 0), angles="0 270 0", targetname="big_strider", spawnflags="8192")
ent("npc_hunter", (x0 + 1500, cy + 700, 4), angles="0 270 0", targetname="big_hunter", spawnflags="8192")
for k, (mdl, z) in enumerate((("models/gunship.mdl", 520), ("models/combine_helicopter.mdl", 560),
                              ("models/combine_dropship.mdl", 600))):
    ent("prop_dynamic", (x0 + 1900 + k * 400, cy + 900, z), angles="0 270 0", model=mdl, solid="0",
        DefaultAnim="idle", targetname="big_prop_%d" % k)
ent("prop_dynamic", (x1 - 500, cy + 500, 0), angles="0 200 0", model="models/combine_apc.mdl", solid="6")
ent("prop_dynamic", (x0 + 1300, cy + 300, 60), angles="0 270 0", model="models/advisor.mdl", solid="0", DefaultAnim="idle")
ent("npc_antlionguard", (x0 + 700, cy - 900, 4), angles="0 90 0", targetname="big_guard", spawnflags="8192")
ent("npc_antlionguard", (x0 + 1100, cy - 500, 4), angles="0 60 0", targetname="big_guardian", cavernbreed="1", incavern="1", spawnflags="8192")
ent("prop_dynamic", (x0 + 400, cy - 400, 0), angles="0 0 0", model="models/antlion_grub.mdl", solid="0", DefaultAnim="idle")
ent("prop_dynamic", (x0 + 1200, cy - 1200, 0), angles="0 45 0", model="models/ministrider.mdl", solid="0")
ent("npc_dog", (cx + 500, cy - 500, 4), angles="0 180 0", targetname="big_dog", spawnflags="8192")
ent("prop_vehicle_jeep", (cx + 400, cy - 1100, 24), angles="0 90 0", model="models/buggy.mdl",
    vehiclescript="scripts/vehicles/jeep_test.txt", targetname="car_buggy", EnableGun="1")
ent("prop_vehicle_jeep", (cx + 900, cy - 1100, 24), angles="0 90 0", model="models/vehicle.mdl",
    vehiclescript="scripts/vehicles/jalopy.txt", targetname="car_jalopy")
ent("prop_vehicle_airboat", (cx + 1300, cy - 1100, 24), angles="0 90 0", model="models/airboat.mdl",
    vehiclescript="scripts/vehicles/airboat.txt", targetname="car_airboat", EnableGun="1")
ent("prop_vehicle_prisoner_pod", (cx + 1300, cy - 500, 8), angles="0 180 0",
    model="models/vehicles/prisoner_pod_inner.mdl", vehiclescript="scripts/vehicles/prisoner_pod.txt")
legend.append("yard: combine north (strider, hunter, gunship/heli/dropship props, APC, advisor), creatures SW (guards), dog + vehicles SE")
cam("yard combine", x0 + 1500, cy + 120, 120, -12, 90)
cam("yard combine air", x0 + 1200, cy + 200, 300, -18, 60)
cam("yard creatures", x0 + 900, y0 + 200, 100, -6, 90)
cam("yard vehicles", cx + 800, cy - 400, 90, 12, 270)

# ---------------------------------------------------------------- (2,0) props
x0, y0, x1, y1 = room(2, 0)
PROP_FOLDERS = [("props_c17", 26), ("props_junk", 24), ("props_interiors", 18), ("props_wasteland", 22),
                ("props_combine", 20), ("props_lab", 16), ("props_debris", 8), ("props_pipes", 6),
                ("props_canal", 8), ("props_trainstation", 10), ("props_rooftop", 6), ("props_docks", 6),
                ("props_vehicles", 14), ("props_vents", 4), ("props_citizen_tech", 5), ("props_borealis", 3),
                ("props_doors", 3), ("props_mining", 10), ("props_silo", 8), ("props_forest", 8),
                ("props_outland", 6), ("props_radiostation", 4), ("props_hive", 3), ("props_explosive", 3),
                ("items", 12), ("combine_turrets", 3), ("props_buildings", 3), ("props_animated_breakable", 2),
                ("props_building_details", 3), ("props_c17/furniture", 0)]
props = []
for folder, n in PROP_FOLDERS:
    if n <= 0:
        continue
    cands = []
    for rel in list_models(folder):
        info = mdl_info(rel)
        if not info or not model_materials_ok(rel):
            continue
        mn, mx, fl = info
        size = [mx[i] - mn[i] for i in range(3)]
        if max(size) > 900 or max(size) < 4:
            continue
        cands.append((rel, mn, mx, fl))
    if not cands:
        continue
    step = max(1.0, len(cands) / float(n))
    seen = set()
    for i in range(min(n, len(cands))):
        c_ = cands[int(i * step)]
        if c_[0] not in seen:
            seen.add(c_[0])
            props.append((folder,) + c_)
# shelf packing in rows along x, the short ones in front
props.sort(key=lambda p: (p[3][2] - p[2][2]))
GAP = 40
cur_x, cur_y, row_depth = x0 + 160, y0 + 520, 0
rows_y = []
for folder, rel, mn, mx, fl in props:
    w = mx[0] - mn[0]
    d = mx[1] - mn[1]
    if cur_x + w > x1 - 160:
        rows_y.append((cur_y, row_depth))
        cur_y += row_depth + GAP + 64
        cur_x, row_depth = x0 + 160, 0
    if cur_y + d > y1 - 160:
        print("props: out of room at", rel)
        break
    ox = cur_x - mn[0]
    oy = cur_y - mn[1]
    oz = -mn[2]
    static = (fl & 0x10) != 0
    if static:
        ent("prop_static", (ox, oy, oz), angles="0 0 0", model=rel, solid="0", skin="0", fademindist="-1", fadescale="1")
    else:
        ent("prop_dynamic", (ox, oy, oz), angles="0 0 0", model=rel, solid="0", DisableBoneFollowers="1")
    legend.append("prop %s at %d %d %d (%s)" % (rel, ox, oy, oz, "static" if static else "dynamic"))
    cur_x += w + GAP
    row_depth = max(row_depth, d)
rows_y.append((cur_y, row_depth))
for i, (ry, rd) in enumerate(rows_y):
    for k in (0.2, 0.5, 0.8):
        cam("props row %d at %.1f" % (i, k), x0 + S * k, ry - 260, 150, 14, 90)
legend.append("props: %d models in %d rows" % (len(props), len(rows_y)))

# ---------------------------------------------------------------- (3,0) terrain, trees, grass
x0, y0, x1, y1 = room(3, 0)
TILE = 512
POWER = 4
NV = 2 ** POWER + 1
BLENDS = ["NATURE/BLENDDIRTGRASS001A", "NATURE/BLENDGRASSGRAVEL001A", "NATURE/BLENDROCKGRASS004A",
          "NATURE/BLENDSANDGRASS008A", "NATURE/BLENDGRASSDIRT01", "NATURE/BLENDDIRTMUD01"]


def height(x, y):
    # rolling ground, zero at the tile edges? no: one smooth field shared by all tiles (edges match)
    u = (x - x0) / S
    v = (y - y0) / S
    h = 60 * math.sin(u * 5.1 + 0.7) * math.sin(v * 4.3 + 1.9) + 34 * math.sin(u * 13.0 + v * 7.0)
    h += 120 * math.exp(-((u - 0.7) ** 2 + (v - 0.65) ** 2) / 0.02)   # a hill
    edge = min(u, 1 - u, v, 1 - v)
    return h * min(1.0, edge / 0.06) + 16


def alpha(x, y):
    u = (x - x0) / S
    v = (y - y0) / S
    a = 0.5 + 0.5 * math.sin(u * 9.0 + math.sin(v * 5.0) * 2.0) * math.cos(v * 7.0 - u * 3.0)
    return max(0.0, min(255.0, a * 255.0))


def displacement(tx0, ty0, mat):
    """one TILE x TILE displacement at z = -16, heights / alphas of the shared fields"""
    tx1, ty1 = tx0 + TILE, ty0 + TILE
    s = m.make_box((tx0, ty0, -32), (tx1, ty1, -16), NODRAW, face_materials={"top": mat})
    top = None
    for side in s.children_named("side"):
        if side.get("material") == mat:
            top = side
    di = Node("dispinfo")
    di.add("power", str(POWER))
    di.add("startposition", "[%g %g %g]" % (tx0, ty0, -16))
    di.add("flags", "0")
    di.add("elevation", "0")
    di.add("subdiv", "0")
    blocks = {}
    for nm in ("normals", "distances", "offsets", "offset_normals", "alphas", "triangle_tags"):
        blocks[nm] = Node(nm)
    for row in range(NV):
        yy = ty0 + row * TILE / (NV - 1)
        normals, dists, offs, onorm, alph = [], [], [], [], []
        for col in range(NV):
            xx = tx0 + col * TILE / (NV - 1)
            normals.append("0 0 1")
            dists.append("%g" % height(xx, yy))
            offs.append("0 0 0")
            onorm.append("0 0 1")
            alph.append("%g" % alpha(xx, yy))
        blocks["normals"].add("row%d" % row, " ".join(normals))
        blocks["distances"].add("row%d" % row, " ".join(dists))
        blocks["offsets"].add("row%d" % row, " ".join(offs))
        blocks["offset_normals"].add("row%d" % row, " ".join(onorm))
        blocks["alphas"].add("row%d" % row, " ".join(alph))
        if row < NV - 1:
            blocks["triangle_tags"].add("row%d" % row, " ".join(["9"] * (2 * (NV - 1))))
    for nm in ("normals", "distances", "offsets", "offset_normals", "alphas", "triangle_tags"):
        di.add_child(blocks[nm])
    av = Node("allowed_verts")
    av.add("10", " ".join(["-1"] * 10))
    di.add_child(av)
    top.add_child(di)
    # world brush (displacements can't be detail brushes)
    idx = len(m.world.children)
    for i, ch in enumerate(m.world.children):
        if ch.name.lower() in ("editor", "hidden", "group"):
            idx = i
            break
    m.world.add_child(s, idx)


for ty in range(S // TILE):
    for tx in range(S // TILE):
        displacement(x0 + tx * TILE, y0 + ty * TILE, BLENDS[(tx // 2 + 3 * (ty // 2)) % len(BLENDS)])
legend.append("terrain: displacements %s" % ", ".join(BLENDS))
TREES = ["models/props_foliage/tree_deciduous_01a.mdl", "models/props_foliage/tree_deciduous_02a.mdl",
         "models/props_foliage/tree_deciduous_03a.mdl", "models/props_foliage/tree_poplar_01.mdl",
         "models/props_foliage/oak_tree01.mdl", "models/props_foliage/tree_pine04.mdl",
         "models/props_foliage/tree_pine05.mdl", "models/props_foliage/tree_pine06.mdl",
         "models/props_foliage/tree_pine_large.mdl", "models/props_foliage/tree_dead01.mdl",
         "models/props_foliage/tree_dry01.mdl", "models/props_foliage/tree_cliff_01a.mdl",
         "models/props_foliage/fallentree01.mdl", "models/props_foliage/tree_stump01.mdl"]
BUSHES = ["models/props_foliage/bush2.mdl", "models/props_foliage/shrub_01a.mdl", "models/props_foliage/ferns01.mdl",
          "models/props_foliage/fern01.mdl", "models/props_foliage/hedge_128.mdl", "models/props_foliage/bramble001a.mdl",
          "models/props_foliage/grass_cluster01.mdl", "models/props_foliage/grass_cluster03.mdl",
          "models/props_foliage/cattails.mdl", "models/props_foliage/ivy_01.mdl", "models/props_foliage/detail_cluster01.mdl",
          "models/props_foliage/rock_forest01.mdl", "models/props_foliage/rock_coast02a.mdl",
          "models/props_foliage/forestrock_cluster01.mdl", "models/props_foliage/driftwood_01a.mdl"]
ROCKS = ["models/props_wasteland/rockcliff01b.mdl", "models/props_wasteland/rockcliff01g.mdl",
         "models/props_wasteland/rockgranite02a.mdl", "models/props_wasteland/rockgranite03b.mdl",
         "models/props_wasteland/rockcliff_cluster02c.mdl"]
k = 0
for i, mdl in enumerate(TREES):
    if not mdl_exists(mdl):
        continue
    gx = x0 + 300 + (i % 5) * 560 + random.uniform(-60, 60)
    gy = y0 + 1500 + (i // 5) * 520 + random.uniform(-60, 60)
    ent("prop_static", (gx, gy, height(gx, gy) - 24), angles="0 %d 0" % random.randint(0, 359), model=mdl, solid="6",
        fademindist="-1", fadescale="1")
    legend.append("tree %s at %d %d" % (mdl, gx, gy))
for i, mdl in enumerate(BUSHES + ROCKS):
    if not mdl_exists(mdl):
        continue
    gx = x0 + 250 + (i % 7) * 400 + random.uniform(-40, 40)
    gy = y0 + 300 + (i // 7) * 380 + random.uniform(-40, 40)
    ent("prop_static", (gx, gy, height(gx, gy) - 6), angles="0 %d 0" % random.randint(0, 359), model=mdl, solid="6",
        fademindist="-1", fadescale="1")
    legend.append("plant/rock %s at %d %d" % (mdl, gx, gy))
cam("terrain trees", x0 + 1400, y0 + 900, 260, 8, 90)
cam("terrain trees against the sun", x0 + 1600, y0 + 2900, 260, 6, 215)
cam("terrain ground close", x0 + 600, y0 + 400, 120, 30, 60)
cam("terrain bushes", x0 + 1200, y0 + 150, 140, 18, 80)

# ---------------------------------------------------------------- (0,1) fire, explosions
x0, y0, x1, y1 = room(0, 1)
stations = [
    ("env_fire small", lambda p, n: ent("env_fire", p, targetname=n, firesize="48", fireattack="2", firetype="0",
                                         spawnflags="13", health="30", damagescale="0", ignitionpoint="32")),
    ("env_fire medium", lambda p, n: ent("env_fire", p, targetname=n, firesize="96", fireattack="2", firetype="0",
                                          spawnflags="13", health="30", damagescale="0", ignitionpoint="32")),
    ("env_fire large", lambda p, n: ent("env_fire", p, targetname=n, firesize="192", fireattack="2", firetype="0",
                                         spawnflags="13", health="30", damagescale="0", ignitionpoint="32")),
    ("env_fire plasma", lambda p, n: ent("env_fire", p, targetname=n, firesize="96", fireattack="2", firetype="1",
                                          spawnflags="13", health="30", damagescale="0", ignitionpoint="32")),
    ("fire_small_01", lambda p, n: ent("info_particle_system", p, targetname=n, effect_name="fire_small_01", start_active="1")),
    ("fire_medium_01", lambda p, n: ent("info_particle_system", p, targetname=n, effect_name="fire_medium_01", start_active="1")),
    ("fire_large_01", lambda p, n: ent("info_particle_system", p, targetname=n, effect_name="fire_large_01", start_active="1")),
    ("fire_large_02", lambda p, n: ent("info_particle_system", p, targetname=n, effect_name="fire_large_02", start_active="1")),
    ("burning_wood_01", lambda p, n: ent("info_particle_system", p, targetname=n, effect_name="burning_wood_01", start_active="1")),
    ("smoke_large_01", lambda p, n: ent("info_particle_system", p, targetname=n, effect_name="smoke_large_01", start_active="1")),
    ("env_explosion (every 5 s)", None),
    ("env_explosion big (every 7 s)", None),
    ("Explosion_2 (every 4 s)", None),
    ("explosion_turret_break (every 6 s)", None),
    ("grenade_explosion_01 (every 5 s)", None),
    ("burning barrels", None),
    ("burning ragdoll", None),
    ("env_spark", lambda p, n: ent("env_spark", p, targetname=n, MaxDelay="1", Magnitude="2", TrailLength="1", spawnflags="192")),
    ("env_steam", lambda p, n: ent("env_steam", p, targetname=n, angles="-90 0 0", InitialState="1", type="0", SpreadSpeed="15",
                                    Speed="120", StartSize="10", EndSize="25", Rate="26", rendercolor="255 255 255",
                                    JetLength="80", renderamt="255")),
    ("env_smokestack", lambda p, n: ent("env_smokestack", p, targetname=n, InitialState="1", BaseSpread="20", SpreadSpeed="15",
                                         Speed="30", StartSize="20", EndSize="40", Rate="20", JetLength="200",
                                         SmokeMaterial="particle/SmokeStack.vmt", rendercolor="110 110 110", renderamt="255",
                                         WindAngle="0 0 0", WindSpeed="0", twist="0", roll="10")),
    ("steam_jet_80", lambda p, n: ent("info_particle_system", p, targetname=n, angles="-90 0 0", effect_name="steam_jet_80", start_active="1")),
    ("env_sprite glows", None),
    ("point_spotlight", lambda p, n: ent("point_spotlight", (p[0], p[1], p[2] + 220), targetname=n, angles="60 0 0",
                                          spotlightlength="400", spotlightwidth="48", rendercolor="255 245 230",
                                          spawnflags="1", HDRColorScale="1")),
    ("light_dynamic flicker", lambda p, n: ent("light_dynamic", (p[0], p[1], p[2] + 96), targetname=n, _light="255 140 50 600",
                                                brightness="4", distance="320", _inner_cone="60", _cone="80", pitch="-90",
                                                style="6", spawnflags="0")),
    ("env_beam lightning", None),
    ("env_laser", None),
    ("point_tesla (every 2 s)", None),
    ("electrical_arc_01_system", lambda p, n: ent("info_particle_system", (p[0], p[1], p[2] + 64), targetname=n,
                                                   effect_name="electrical_arc_01_system", start_active="1")),
    ("env_flare", lambda p, n: ent("env_flare", p, targetname=n, scale="2", duration="30", spawnflags="4")),
    ("combine ball launcher (every 6 s)", None),
    ("env_citadel_energy_core", lambda p, n: ent("env_citadel_energy_core", (p[0], p[1], p[2] + 64), targetname=n, scale="1",
                                                  spawnflags="1")),
    ("fireplace (indoor fire)", None),
    ("func_dustmotes", None),
    ("burning_character on a prop", lambda p, n: (ent("prop_dynamic", p, targetname=n + "_p", model="models/Humans/Charple01.mdl",
                                                      solid="0", DefaultAnim="idle"),
                                                  ent("info_particle_system", (p[0], p[1], p[2] + 32), targetname=n,
                                                      effect_name="burning_character", start_active="1"))),
]
SP = 440
cols = 6
for i, (name, make) in enumerate(stations):
    rr, cc = divmod(i, cols)
    px = x0 + 256 + cc * SP
    py = y0 + 700 + rr * SP
    detail((px - 96, py - 96, 0), (px + 96, py + 96, 4), "CONCRETE/CONCRETEFLOOR011A")
    p = (px, py, 4)
    n = "fx_%02d" % i
    label((px, py - 110, 170), name, 270, size=180)
    legend.append("effect %02d %s at %d %d" % (i, name, px, py))
    if make:
        make(p, n)
        continue
    if name.startswith("env_explosion"):
        big = "big" in name
        e = ent("env_explosion", (px, py, 24), targetname=n, iMagnitude="250" if big else "100",
                iRadiusOverride="0", spawnflags="19", fireballsprite="sprites/zerogxplode.spr", rendermode="5")
        t = ent("logic_timer", (px + 32, py, 24), targetname=n + "_t", RefireTime="7" if big else "5", StartDisabled="0")
        m.add_output(t, "OnTimer", n, "Explode")
    elif name.startswith("Explosion_2") or name.startswith("explosion_turret") or name.startswith("grenade_explosion"):
        effect = name.split(" ")[0]
        ent("info_particle_system", (px, py, 16), targetname=n, effect_name=effect, start_active="0")
        t = ent("logic_timer", (px + 32, py, 24), targetname=n + "_t",
                RefireTime={"Explosion_2": "4", "explosion_turret_break": "6", "grenade_explosion_01": "5"}[effect], StartDisabled="0")
        m.add_output(t, "OnTimer", n, "Stop")
        m.add_output(t, "OnTimer", n, "Start", delay=0.05)
    elif name == "burning barrels":
        for k2, (dx, dy) in enumerate(((-40, -30), (40, -20), (0, 40))):
            ent("prop_physics", (px + dx, py + dy, 28), targetname=n + "_b%d" % k2, model="models/props_c17/oildrum001.mdl",
                spawnflags="8", health="0")
        a = ent("logic_auto", (px, py, 64), spawnflags="1")
        for k2 in range(3):
            m.add_output(a, "OnMapSpawn", n + "_b%d" % k2, "Ignite", delay=1.0)
    elif name == "burning ragdoll":
        ent("prop_ragdoll", (px, py, 16), targetname=n + "_r", model="models/Zombie/Classic.mdl", angles="0 0 0", spawnflags="4")
        a = ent("logic_auto", (px, py, 64), spawnflags="1")
        m.add_output(a, "OnMapSpawn", n + "_r", "Ignite", delay=1.0)
    elif name == "env_sprite glows":
        for k2, (mdl, col) in enumerate((("sprites/light_glow03.vmt", "255 200 140"), ("sprites/glow04_noz.vmt", "140 200 255"),
                                         ("sprites/redglow1.vmt", "255 60 40"))):
            ent("env_sprite", (px - 50 + k2 * 50, py, 80), model=mdl, rendermode="9", renderamt="255", rendercolor=col,
                scale="0.6", GlowProxySize="4", spawnflags="1", HDRColorScale="1")
        ent("env_lightglow", (px, py + 50, 120), rendercolor="255 220 180", VerticalGlowSize="40", HorizontalGlowSize="40",
            MinDist="200", MaxDist="2000", OuterMaxDist="0", GlowProxySize="2", HDRColorScale="1", angles="0 0 0")
    elif name == "env_beam lightning":
        ent("info_target", (px - 80, py, 20), targetname=n + "_a")
        ent("info_target", (px + 80, py, 180), targetname=n + "_b")
        ent("env_beam", (px, py, 100), targetname=n, LightningStart=n + "_a", LightningEnd=n + "_b", life="0", BoltWidth="3",
            NoiseAmplitude="6", texture="sprites/laserbeam.spr", TextureScroll="35", framerate="0", framestart="0",
            StrikeTime="0", damage="0", rendercolor="170 200 255", renderamt="255", Radius="256", spawnflags="1",
            HDRColorScale="1", ClipStyle="0")
    elif name == "env_laser":
        ent("info_target", (px + 90, py + 60, 30), targetname=n + "_t")
        ent("env_laser", (px - 90, py - 60, 120), targetname=n, LaserTarget=n + "_t", renderamt="160", rendercolor="255 40 20",
            width="2", NoiseAmplitude="0", texture="sprites/laserbeam.spr", TextureScroll="35", damage="0", spawnflags="1",
            dissolvetype="None")
    elif name.startswith("point_tesla"):
        ent("point_tesla", (px, py, 80), targetname=n, m_SoundName="DoSpark", texture="sprites/physbeam.vmt",
            m_Color="255 255 255", m_flRadius="160", beamcount_min="6", beamcount_max="8", thick_min="4", thick_max="5",
            lifetime_min="0.3", lifetime_max="0.3", interval_min="0.5", interval_max="2")
        t = ent("logic_timer", (px + 32, py, 24), targetname=n + "_t", RefireTime="2", StartDisabled="0")
        m.add_output(t, "OnTimer", n, "DoSpark")
    elif name.startswith("combine ball"):
        ent("point_combine_ball_launcher", (px, py, 60), targetname=n, angles="-60 0 0", launchconenoise="15", bullseyename="",
            maxballbounces="6", ballcount="1", minspeed="300", maxspeed="400", ballradius="20", balltype="0",
            ballrespawntime="6", spawnflags="0")
        t = ent("logic_timer", (px + 32, py, 24), targetname=n + "_t", RefireTime="6", StartDisabled="0")
        m.add_output(t, "OnTimer", n, "LaunchBall")
        # a cage around it (the ball bounces inside)
        for (a, b) in (((px - 160, py - 160, 0), (px + 160, py - 152, 240)), ((px - 160, py + 152, 0), (px + 160, py + 160, 240)),
                       ((px - 160, py - 152, 0), (px - 152, py + 152, 240)), ((px + 152, py - 152, 0), (px + 160, py + 152, 240)),
                       ((px - 160, py - 160, 240), (px + 160, py + 160, 248))):
            detail(a, b, "METAL/METALGRATE011A" if a[2] < 200 else "METAL/METALGRATE011A")
    elif name.startswith("fireplace"):
        detail((px - 96, py + 40, 0), (px + 96, py + 96, 200), "BRICK/BRICKWALL031D")
        detail((px - 96, py - 40, 120), (px + 96, py + 96, 136), "BRICK/BRICKWALL031D")
        detail((px - 96, py - 40, 0), (px - 80, py + 40, 120), "BRICK/BRICKWALL031D")
        detail((px + 80, py - 40, 0), (px + 96, py + 40, 120), "BRICK/BRICKWALL031D")
        ent("env_fire", (px, py + 10, 6), targetname=n, firesize="64", fireattack="2", firetype="0", spawnflags="13",
            health="30", damagescale="0", ignitionpoint="32")
    elif name == "func_dustmotes":
        m.add_brush_entity("func_dustmotes", [m.make_box((px - 90, py - 90, 8), (px + 90, py + 90, 200), "TOOLS/TOOLSTRIGGER")],
                           SpawnRate="60", Color="255 255 255", SizeMin="6", SizeMax="12", Alpha="255", LifetimeMin="3",
                           LifetimeMax="5", DistMax="1024", FallSpeed="0", StartDisabled="0")
for rr in range((len(stations) + cols - 1) // cols):
    py = y0 + 700 + rr * SP
    cam("effects row %d left" % rr, x0 + 256 + 1.0 * SP, py - 330, 110, 8, 90)
    cam("effects row %d right" % rr, x0 + 256 + 4.0 * SP, py - 330, 110, 8, 90)

# ---------------------------------------------------------------- (3,1) shooting range
x0, y0, x1, y1 = room(3, 1)
FIRE_X = x0 + 300
cy = (y0 + y1) / 2
TARGETS = ["METAL/METALWALL001A", "METAL/METALCOMBINE001", "WOOD/WOODWALL009A", "WOOD/WOODFLOOR005A", "CONCRETE/CONCRETEWALL001A",
           "PLASTER/PLASTERWALL003A", "BRICK/BRICKWALL001A", "TILE/TILEWALL006B", "NATURE/DIRTFLOOR006A", "NATURE/SANDFLOOR010A",
           "STONE/STONEWALL017A", "TILE/TILEWALL006A"]
for i, mat in enumerate(TARGETS):
    ty = y0 + 256 + i * 216
    detail((x0 + 1400, ty, 0), (x0 + 1424, ty + 192, 192), mat)
    label((x0 + 1396, ty + 96, 212), mat.split("/")[1].lower(), 180, size=120)
    legend.append("range target %s at %d %d" % (mat, x0 + 1400, ty + 96))
# breakable glass panes, a fence, a water trough
for i in range(3):
    ty = y0 + 400 + i * 400
    m.add_brush_entity("func_breakable", [m.make_box((x0 + 1000, ty, 0), (x0 + 1002, ty + 160, 160), "GLASS/GLASSWINDOW007A")],
                       material="0", health="1", propdata="0", spawnflags="0", explosion="0", gibdir="0 0 0",
                       targetname="range_glass_%d" % i)
# (a shallow trough: water brush over the floor - its bottom is the floor top)
detail((x0 + 900, y1 - 900, 0), (x0 + 1300, y1 - 892, 48), "CONCRETE/CONCRETEWALL001A")
detail((x0 + 900, y1 - 508, 0), (x0 + 1300, y1 - 500, 48), "CONCRETE/CONCRETEWALL001A")
detail((x0 + 900, y1 - 892, 0), (x0 + 908, y1 - 508, 48), "CONCRETE/CONCRETEWALL001A")
detail((x0 + 1292, y1 - 892, 0), (x0 + 1300, y1 - 508, 48), "CONCRETE/CONCRETEWALL001A")
box((x0 + 908, y1 - 892, 0), (x0 + 1292, y1 - 508, 40), NODRAW, face_materials={"top": "NATURE/WATER_CANALS_WATER2"})
PHYS = [("models/props_c17/oildrum001.mdl", 16), ("models/props_c17/oildrum001_explosive.mdl", 16),
        ("models/props_junk/wood_crate001a.mdl", 20), ("models/props_junk/wood_crate002a.mdl", 20),
        ("models/props_junk/watermelon01.mdl", 8), ("models/props_junk/garbage_glassbottle001a.mdl", 6),
        ("models/props_junk/PopCan01a.mdl", 4), ("models/props_junk/cardboard_box001a.mdl", 16),
        ("models/props_vehicles/carparts_tire01a.mdl", 14), ("models/props_junk/metal_paintcan001a.mdl", 8),
        ("models/props_c17/FurnitureChair001a.mdl", 20), ("models/props_interiors/BathTub01a.mdl", 16),
        ("models/props_junk/propane_tank001a.mdl", 12), ("models/props_c17/canister01a.mdl", 20)]
for i, (mdl, z) in enumerate(PHYS):
    ty = y0 + 300 + i * 170
    ent("prop_physics", (x0 + 1150, ty, z + 4), model=mdl, spawnflags="0", targetname="range_phys_%d" % i)
# the firing line: a table with the weapons, ammo crates, a roofed booth (muzzle flashes in shade)
detail((FIRE_X - 40, cy - 400, 0), (FIRE_X + 40, cy + 400, 40), "WOOD/WOODFLOOR005A")
WEAPONS = ["weapon_crowbar", "weapon_stunstick", "weapon_pistol", "weapon_357", "weapon_smg1", "weapon_ar2",
           "weapon_shotgun", "weapon_crossbow", "weapon_rpg", "weapon_frag", "weapon_physcannon", "weapon_bugbait",
           "weapon_xm1014"]
for i, w in enumerate(WEAPONS):
    ent(w, (FIRE_X, cy - 360 + i * 60, 48), angles="0 90 0", spawnflags="0")
for i in range(10):
    ent("item_ammo_crate", (FIRE_X - 120, cy - 450 + i * 100, 8), angles="0 0 0", AmmoType=str(i))
detail((FIRE_X - 200, cy + 600, 0), (FIRE_X + 200, cy + 616, 160), "CONCRETE/CONCRETEWALL001A")
detail((FIRE_X - 200, cy + 1016, 0), (FIRE_X + 200, cy + 1032, 160), "CONCRETE/CONCRETEWALL001A")
detail((FIRE_X - 200, cy + 600, 160), (FIRE_X + 200, cy + 1032, 176), "CONCRETE/CONCRETECEILING001A")
legend.append("range: fire from x %d facing +x; weapons on the table at %d %d; roofed booth y %d..%d" % (FIRE_X, FIRE_X, cy, cy + 600, cy + 1032))
cam("range targets", FIRE_X + 60, cy, 64, 4, 0)
cam("range booth (shade)", FIRE_X, cy + 800, 64, 4, 0)

# ---------------------------------------------------------------- (0,2) water, glass, mirror
x0, y0, x1, y1 = room(0, 2)
POOLS = [("NATURE/WATER_CANALS_WATER_CLEAR", 256), ("NATURE/WATER_CANALS_CITY_MURKY", 160), ("NATURE/WATER_COAST01", 256),
         ("NATURE/WATER_LOWDIRTY001", 128)]
PW = 640
pool_rects = []
for i, (mat, depth) in enumerate(POOLS):
    px0 = x0 + 256 + i * (PW + 96)
    py0 = y0 + 512
    pool_rects.append((px0, py0, px0 + PW, py0 + PW, mat, depth))
# the floor around the pools (in strips), the pools' bottoms and walls, the water
xs = sorted(set([x0, x1] + [p[0] for p in pool_rects] + [p[2] for p in pool_rects]))
for i in range(len(xs) - 1):
    a, b = xs[i], xs[i + 1]
    inpool = [p for p in pool_rects if p[0] == a and p[2] == b]
    if inpool:
        p = inpool[0]
        floor_box(a, y0, b, p[1], "CONCRETE/CONCRETEFLOOR033A")
        floor_box(a, p[3], b, y1, "CONCRETE/CONCRETEFLOOR033A")
        floor_box(a, p[1], b, p[3], "STONE/STONEFLOOR006A", top=-p[5])
        box((a, p[1], -p[5]), (b, p[3], -10), NODRAW, face_materials={"top": p[4]})
        legend.append("pool %s depth %d at %d..%d %d..%d" % (p[4], p[5], a, b, p[1], p[3]))
        cam("pool %s" % p[4].split("/")[1].lower(), (a + b) / 2, p[1] - 260, 90, 18, 90)
    else:
        floor_box(a, y0, b, y1, "CONCRETE/CONCRETEFLOOR033A")
# (pool walls: the floor boxes' sides above are nodraw - tiles on them)
for p in pool_rects:
    a, c_, b, d, mat, depth = p      # x0, y0, x1, y1
    detail((a, c_, -depth), (a + 4, d, 0), "TILE/TILEWALL006B")
    detail((b - 4, c_, -depth), (b, d, 0), "TILE/TILEWALL006B")
    detail((a + 4, c_, -depth), (b - 4, c_ + 4, 0), "TILE/TILEWALL006B")
    detail((a + 4, d - 4, -depth), (b - 4, d, 0), "TILE/TILEWALL006B")
    ent("prop_static", (a + PW / 2, c_ + PW / 2, -depth), model="models/props_c17/oildrum001.mdl", solid="6", angles="0 0 0")
ent("water_lod_control", (x0 + 64, y0 + 64, 64), cheapwaterstartdistance="1500", cheapwaterenddistance="2500")
# a mirror and glass panes
gx, gy = x0 + 600, y1 - 700
m.add_brush_entity("func_reflective_glass", [m.make_box((gx, gy, 0), (gx + 256, gy + 4, 256), NODRAW,
                                                        face_materials={"front": "GLASS/REFLECTIVEGLASS003"})])
detail((gx - 8, gy, 0), (gx, gy + 4, 256), "METAL/METALWALL001A")
detail((gx + 256, gy, 0), (gx + 264, gy + 4, 256), "METAL/METALWALL001A")
ent("prop_static", (gx + 128, gy - 220, 0), model="models/props_c17/FurnitureChair001a.mdl", solid="6", angles="0 90 0")
ent("npc_citizen", (gx + 160, gy - 140, 4), angles="0 80 0", citizentype="4", model="models/Humans/Group01/female_02.mdl",
    targetname="mirror_npc", spawnflags="8704")
for i, gm in enumerate(["GLASS/GLASSWINDOW007A", "GLASS/GLASSWINDOW005A", "GLASS/GLASSWINDOW002C", "GLASS/GLASSWINDOW002A"]):
    wx = x0 + 1300 + i * 320
    detail((wx, y1 - 700, 0), (wx + 256, y1 - 698, 256), gm)
    detail((wx - 8, y1 - 700, 0), (wx, y1 - 692, 256), "WOOD/WOODWALL009A")
    label((wx + 128, y1 - 706, 280), gm.split("/")[1].lower(), 270, size=120)
legend.append("mirror at %d %d (faces -y), glass panes at y %d" % (gx, gy, y1 - 700))
cam("mirror", gx + 128, gy - 420, 64, 2, 90)
cam("glass panes", x0 + 1800, y1 - 1300, 80, 4, 90)

# ---------------------------------------------------------------- (1,2) hall: indoor, lamps
x0, y0, x1, y1 = room(1, 2)
ROOF = 320
box((x0, y0, ROOF), (x1, y1, ROOF + 16), "CONCRETE/CONCRETECEILING001A")
cx, cy = (x0 + x1) / 2, (y0 + y1) / 2
# a dark room (no lamps) in the north-west corner with a door
detail((x0, y1 - 1024, 0), (x0 + 1024, y1 - 1008, ROOF), "PLASTER/PLASTERWALL003A")
detail((x0 + 1008, y1 - 1008, 0), (x0 + 1024, y1 - 600, ROOF), "PLASTER/PLASTERWALL003A")
detail((x0 + 1008, y1 - 440, 0), (x0 + 1024, y1, ROOF), "PLASTER/PLASTERWALL003A")
detail((x0 + 1008, y1 - 600, 128), (x0 + 1024, y1 - 440, ROOF), "PLASTER/PLASTERWALL003A")
ent("npc_citizen", (x0 + 400, y1 - 400, 4), angles="0 0 0", citizentype="4", model="models/Humans/Group01/male_02.mdl",
    targetname="dark_npc", spawnflags="8704")
ent("prop_static", (x0 + 600, y1 - 700, 0), model="models/props_c17/FurnitureShelf001a.mdl", solid="6", angles="0 90 0")
LAMPS = [((255, 214, 170), 520), ((255, 190, 120), 420), ((190, 215, 255), 420), ((255, 255, 255), 600),
         ((255, 120, 60), 380), ((120, 255, 140), 300)]
for i, (col, pw) in enumerate(LAMPS):
    lx = x0 + 1400 + (i % 3) * 520
    ly = y0 + 600 + (i // 3) * 900
    ent("light", (lx, ly, ROOF - 40), _light="%d %d %d %d" % (col + (pw,)), _constant_attn="0", _linear_attn="0",
        _quadratic_attn="1", _fifty_percent_distance="0", _zero_percent_distance="0", style="0")
    ent("prop_static", (lx, ly, ROOF), model="models/props_c17/light_cagelight02_on.mdl", solid="0", angles="0 0 0")
for i in range(3):
    lx = x0 + 300
    ly = y0 + 400 + i * 500
    ent("light_spot", (lx, ly, 260), angles="-30 0 0", pitch="-30", _light="255 240 220 1400", _cone="35", _inner_cone="20",
        _exponent="1", _constant_attn="0", _linear_attn="0", _quadratic_attn="1", _fifty_percent_distance="0",
        _zero_percent_distance="0", style="0")
FURN = [("models/props_interiors/Furniture_Couch01a.mdl", (1500, 900)), ("models/props_c17/FurnitureTable002a.mdl", (1900, 900)),
        ("models/props_c17/FurnitureChair001a.mdl", (1980, 980)), ("models/props_interiors/Furniture_Lamp01a.mdl", (1300, 1000)),
        ("models/props_c17/FurnitureBed001a.mdl", (2300, 1500)), ("models/props_c17/FurnitureDrawer001a.mdl", (2500, 1100)),
        ("models/props_c17/tv_monitor01.mdl", (1800, 1500)), ("models/props_lab/monitor01a.mdl", (2000, 1500)),
        ("models/props_interiors/BathTub01a.mdl", (2700, 600)), ("models/props_c17/FurnitureFridge001a.mdl", (2800, 900)),
        ("models/props_interiors/Furniture_Desk01a.mdl", (1600, 1900)), ("models/props_interiors/Furniture_shelf01a.mdl", (2200, 2300)),
        ("models/props_combine/breenglobe.mdl", (1600, 2100)), ("models/props_interiors/Radiator01a.mdl", (2900, 1700)),
        ("models/props_c17/FurnitureCouch002a.mdl", (2500, 2600)), ("models/props_lab/kennel_physics.mdl", (1400, 2700))]
for mdl, (fx, fy) in FURN:
    info = mdl_info(mdl.lower())
    oz = -info[0][2] if info else 0
    ent("prop_static", (x0 + fx, y0 + fy, oz), model=mdl, solid="6", angles="0 %d 0" % random.choice((0, 90, 180, 270)))
ent("npc_alyx", (x0 + 1700, y0 + 1200, 4), angles="0 225 0", targetname="hall_alyx", spawnflags="8704")
ent("npc_citizen", (x0 + 2100, y0 + 700, 4), angles="0 135 0", citizentype="4", model="models/Humans/Group03/male_05.mdl",
    targetname="hall_rebel", spawnflags="8704")
# a fireplace
fx, fy = x0 + 2600, y1 - 64
detail((fx - 128, fy - 16, 0), (fx + 128, fy + 32, ROOF), "BRICK/BRICKWALL031D")
detail((fx - 128, fy - 112, 140), (fx + 128, fy - 16, 160), "BRICK/BRICKWALL031D")
detail((fx - 128, fy - 112, 0), (fx - 104, fy - 16, 140), "BRICK/BRICKWALL031D")
detail((fx + 104, fy - 112, 0), (fx + 128, fy - 16, 140), "BRICK/BRICKWALL031D")
ent("env_fire", (fx, fy - 60, 4), targetname="hall_fire", firesize="72", fireattack="2", firetype="0", spawnflags="13",
    health="30", damagescale="0", ignitionpoint="32")
legend.append("hall: roof %d, dark room NW (citizen, flashlight test), lamps, fireplace at %d %d" % (ROOF, fx, fy))
cam("hall lamps", x0 + 1200, y0 + 300, 100, 10, 45)
cam("hall alyx", x0 + 1700, y0 + 1060, 64, 4, 90)
cam("hall fireplace", fx, fy - 600, 90, 6, 90)
cam("hall dark room", x0 + 1200, y1 - 520, 64, 2, 180)

# ---------------------------------------------------------------- (3,2) fences, decals, a canopy
x0, y0, x1, y1 = room(3, 2)
FENCES = pick_materials("metal", 12, alphatest=True) + pick_materials("glass", 4, alphatest=True)
for i, (mat, fs) in enumerate(FENCES[:12]):
    fx = x0 + 256 + i * 220
    detail((fx, y0 + 400, 0), (fx + 192, y0 + 402, 192), mat.upper())
    label((fx + 96, y0 + 394, 216), mat.split("/", 1)[1], 270, size=110)
    legend.append("fence %s at %d %d" % (mat, fx + 96, y0 + 400))
cam("fences", x0 + 1200, y0 + 100, 90, 6, 90)
# a canopy of grates on posts (the sun through it onto the floor)
gx0, gy0 = x0 + 400, y0 + 900
for ix in range(3):
    for iy in range(2):
        detail((gx0 + ix * 384, gy0 + iy * 384, 0), (gx0 + ix * 384 + 16, gy0 + iy * 384 + 16, 256), "METAL/METALWALL001A")
detail((gx0, gy0, 256), (gx0 + 784, gy0 + 400, 260), "METAL/METALGRATE011A")
cam("grate canopy", gx0 + 400, gy0 - 300, 80, 16, 90)
# a long wall with decals
dw_y = y1 - 400
detail((x0 + 200, dw_y, 0), (x1 - 200, dw_y + 32, 320), "CONCRETE/CONCRETEWALL001A")
DECALS = []
for g in GAMES:
    d = os.path.join(REF, g, "materials", "decals")
    if os.path.isdir(d):
        for f in sorted(os.listdir(d)):
            fl = f.lower()
            if fl.endswith(".vmt") and "subrect" not in fl and "_model" not in fl and "rendershadow" not in fl:
                t = read_vmt("decals/" + fl[:-4])
                if t and vmt_shader(t) in ("lightmappedgeneric", "decalmodulate", "vertexlitgeneric", "unlitgeneric"):
                    DECALS.append("decals/" + fl[:-4])
DECALS = sorted(set(DECALS))
step = max(1, len(DECALS) // 24)
for i, dname in enumerate(DECALS[::step][:24]):
    dx = x0 + 300 + i * 100
    ent("infodecal", (dx, dw_y - 1, 120 + (i % 3) * 60), texture=dname)
    legend.append("decal %s at %d %d" % (dname, dx, dw_y))
cam("decal wall", x0 + 1500, dw_y - 700, 140, 0, 90)
# stairs and railings
for k in range(8):
    detail((x1 - 900, y0 + 1800 + k * 24, 0), (x1 - 600, y0 + 1800 + (k + 1) * 24, 16 + k * 16), "CONCRETE/CONCRETEFLOOR019A")
ent("prop_static", (x1 - 1000, y0 + 1700, 0), model="models/props_c17/fence01a.mdl", solid="6", angles="0 0 0")
ent("prop_static", (x1 - 1300, y0 + 1700, 0), model="models/props_c17/fence02a.mdl", solid="6", angles="0 0 0")
ent("prop_static", (x1 - 1600, y0 + 1700, 0), model="models/props_wasteland/fence_corrugated01a.mdl" if mdl_exists("models/props_wasteland/fence_corrugated01a.mdl") else "models/props_c17/fence03a.mdl", solid="6", angles="0 0 0")

# ---------------------------------------------------------------- write
m.save(out, backup=os.path.exists(out))
base = os.path.splitext(out)[0]
open(base + "_legend.txt", "w", encoding="utf-8").write("\n".join(legend) + "\n")
open(base + "_steps.txt", "w", encoding="utf-8").write(
    "|cl_weather_tour_step 10; sv_timecycle_set_time 13; sv_weather clear 0; noclip; notarget\n" + "\n".join(steps) + "\n")
for x in sorted(MISSING):
    print("MISSING", x)
print("wrote", out, "-", len(legend), "legend lines,", len(steps), "camera steps,", len(props), "props")
