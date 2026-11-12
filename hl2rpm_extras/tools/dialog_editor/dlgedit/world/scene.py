"""A loaded map for the render window (CPU side): the BSP, the objects placed in Hammer
(from the map's VMF when it is found - editable - or from the BSP - view only), their
models, and ray casts against all of it."""
import math
import os

import numpy as np

from . import bsp as bspmod
from . import mdl as mdlmod
from .materials import MaterialSystem

ITEM_MODELS = {
    "item_healthkit": "models/items/healthkit.mdl", "item_healthvial": "models/healthvial.mdl",
    "item_battery": "models/items/battery.mdl", "item_suit": "models/hl2rpm/items/suit_hev.mdl",
    "item_suit_pcv": "models/hl2rpm/items/suit_pcv.mdl", "item_suit_cp": "models/hl2rpm/items/suit_cp.mdl",
    "item_combine_soldier_suit": "models/hl2rpm/items/suit_soldier.mdl", "item_suit_prison": "models/hl2rpm/items/suit_prison.mdl",
    "item_suit_elite": "models/hl2rpm/items/suit_elite.mdl", "item_suit_worker": "models/hl2rpm/items/suit_worker.mdl",
    "item_suit_hazmat": "models/hl2rpm/items/suit_hazmat.mdl",
    "item_ammo_pistol": "models/items/boxsrounds.mdl", "item_ammo_smg1": "models/items/boxmrounds.mdl",
    "item_ammo_ar2": "models/items/combine_rifle_cartridge01.mdl", "item_box_buckshot": "models/items/boxbuckshot.mdl",
    "item_ammo_357": "models/items/357ammo.mdl", "item_ammo_crossbow": "models/items/crossbowrounds.mdl",
    "item_rpg_round": "models/weapons/w_missile_closed.mdl", "item_ammo_smg1_grenade": "models/items/ar2_grenade.mdl",
    "weapon_crowbar": "models/weapons/w_crowbar.mdl", "weapon_stunstick": "models/weapons/w_stunbaton.mdl",
    "weapon_pistol": "models/weapons/w_pistol.mdl", "weapon_357": "models/weapons/w_357.mdl",
    "weapon_smg1": "models/weapons/w_smg1.mdl", "weapon_ar2": "models/weapons/w_irifle.mdl",
    "weapon_shotgun": "models/weapons/w_shotgun.mdl", "weapon_crossbow": "models/weapons/w_crossbow.mdl",
    "weapon_rpg": "models/weapons/w_rocket_launcher.mdl", "weapon_frag": "models/weapons/w_grenade.mdl",
    "weapon_physcannon": "models/weapons/w_physics.mdl", "weapon_bugbait": "models/weapons/w_bugbait.mdl",
    "weapon_rpm_axe": "models/weapons/hl2rpm/w_axe.mdl",
    "weapon_rpm_beretta": "models/weapons/hl2rpm/w_beretta.mdl",
    "weapon_rpm_bottle": "models/weapons/hl2rpm/w_bottle.mdl",
    "weapon_rpm_brokenbottle": "models/weapons/hl2rpm/w_brokenbottle.mdl",
    "weapon_rpm_hook": "models/weapons/hl2rpm/w_hook.mdl",
    "weapon_rpm_pan": "models/weapons/hl2rpm/w_pan.mdl",
    "weapon_rpm_pickaxe": "models/weapons/hl2rpm/w_pickaxe.mdl",
    "weapon_rpm_pipe": "models/weapons/hl2rpm/w_pipe.mdl",
    "weapon_rpm_pot": "models/weapons/hl2rpm/w_pot.mdl",
    "weapon_rpm_shovel": "models/weapons/hl2rpm/w_shovel.mdl",
    # built-in items of the mod (RPMItems::IsBuiltin) and the default of an item of the database without a model
    "bobby_pin": "models/hl2rpm/items/bobby_pin.mdl", "rpm_credits": "models/props_lab/clipboard.mdl",
    "item_rpm": "models/props_lab/clipboard.mdl", "item_rpm_key": "models/hl2rpm/items/key_flat.mdl",
}
# entities of the layer (type "entity") that set their model themselves in the game
ENTITY_MODELS = {
    "npc_turret_floor": "models/combine_turrets/floor_turret.mdl", "npc_turret_ceiling": "models/combine_turrets/ceiling_turret.mdl",
    "npc_turret_ground": "models/combine_turrets/ground_turret.mdl", "npc_combine_camera": "models/combine_camera/combine_camera.mdl",
}


def model_matrix(origin, angles):
    m = np.eye(4, dtype=np.float32)
    m[:3, :3] = bspmod.angle_matrix(*angles)
    m[:3, 3] = origin
    return m


def matrix_angles(r):
    """3x3 rotation (columns forward, left, up) -> Source QAngle (pitch, yaw, roll); Source's MatrixAngles."""
    fwd = r[:, 0]
    left = r[:, 1]
    up = r[:, 2]
    xy = math.hypot(fwd[0], fwd[1])
    if xy > 0.001:
        yaw = math.degrees(math.atan2(fwd[1], fwd[0]))
        pitch = math.degrees(math.atan2(-fwd[2], xy))
        roll = math.degrees(math.atan2(left[2], up[2]))
    else:
        yaw = math.degrees(math.atan2(-left[0], left[1]))
        pitch = math.degrees(math.atan2(-fwd[2], xy))
        roll = 0.0
    return [_tidy(pitch), _tidy(yaw % 360.0), _tidy(roll)]


def _tidy(a):
    a = round(a, 3)
    return 0.0 if abs(a) < 1e-3 else a


def axis_rotation(axis, deg):
    """3x3 rotation about a world axis (0 x, 1 y, 2 z) by deg (right-handed)."""
    c, s = math.cos(math.radians(deg)), math.sin(math.radians(deg))
    if axis == 2:
        return np.array([[c, -s, 0], [s, c, 0], [0, 0, 1]], np.float64)
    if axis == 1:
        return np.array([[c, 0, s], [0, 1, 0], [-s, 0, c]], np.float64)
    return np.array([[1, 0, 0], [0, c, -s], [0, s, c]], np.float64)


class RayMesh:
    """Triangles split into spatial chunks with bounding boxes: a ray tests only the
    chunks it passes through."""

    def __init__(self, tris, chunk=512):
        tris = np.asarray(tris, dtype=np.float32).reshape(-1, 3, 3)
        self.n = len(tris)
        if self.n == 0:
            self.v0 = self.e1 = self.e2 = np.zeros((0, 3), np.float32)
            self.cmin = self.cmax = np.zeros((0, 3), np.float32)
            self.starts = np.zeros(1, np.int64)
            return
        c = tris.mean(1)
        key = np.floor(c / 384.0).astype(np.int64)
        key -= key.min(0)
        k = (key[:, 0] * 1_000_003 + key[:, 1]) * 1_000_003 + key[:, 2]
        order = np.argsort(k, kind="stable")
        tris = tris[order]
        self.v0 = tris[:, 0].copy()
        self.e1 = tris[:, 1] - tris[:, 0]
        self.e2 = tris[:, 2] - tris[:, 0]
        self.starts = np.arange(0, self.n + chunk, chunk)
        self.starts[-1] = self.n
        lo = np.minimum(np.minimum(tris[:, 0], tris[:, 1]), tris[:, 2])
        hi = np.maximum(np.maximum(tris[:, 0], tris[:, 1]), tris[:, 2])
        nchunks = len(self.starts) - 1
        self.cmin = np.array([lo[self.starts[i]:self.starts[i + 1]].min(0) for i in range(nchunks)], np.float32)
        self.cmax = np.array([hi[self.starts[i]:self.starts[i + 1]].max(0) for i in range(nchunks)], np.float32)

    def cast(self, o, d, tmax=1e9):
        """Nearest hit (t, normal) or None."""
        if self.n == 0:
            return None
        o = np.asarray(o, np.float64)
        d = np.asarray(d, np.float64)
        inv = 1.0 / np.where(np.abs(d) < 1e-12, 1e-12, d)
        t1 = (self.cmin - o) * inv
        t2 = (self.cmax - o) * inv
        tn = np.minimum(t1, t2).max(1)
        tf = np.maximum(t1, t2).min(1)
        cand = np.nonzero((tf >= np.maximum(tn, 0)) & (tn < tmax))[0]
        if len(cand) == 0:
            return None
        cand = cand[np.argsort(tn[cand])]
        best = None
        for ci in cand:
            if best is not None and tn[ci] > best[0]:
                break
            a, b = self.starts[ci], self.starts[ci + 1]
            r = _mt(o, d, self.v0[a:b], self.e1[a:b], self.e2[a:b])
            if r is not None and (best is None or r[0] < best[0]):
                best = (r[0], a + r[1])
        if best is None or best[0] > tmax:
            return None
        n = np.cross(self.e1[best[1]], self.e2[best[1]])
        ln = np.linalg.norm(n)
        n = n / ln if ln > 0 else np.array([0, 0, 1.0])
        if np.dot(n, d) > 0:
            n = -n
        return best[0], n


def _mt(o, d, v0, e1, e2):
    p = np.cross(d, e2)
    det = (e1 * p).sum(1)
    ok = np.abs(det) > 1e-9
    inv = np.where(ok, 1.0 / np.where(ok, det, 1.0), 0.0)
    s = o - v0
    u = (s * p).sum(1) * inv
    q = np.cross(s, e1)
    v = (q @ d) * inv
    t = (e2 * q).sum(1) * inv
    hit = ok & (u >= 0) & (v >= 0) & (u + v <= 1) & (t > 0.01)
    if not hit.any():
        return None
    idx = np.nonzero(hit)[0]
    i = idx[np.argmin(t[idx])]
    return float(t[i]), int(i)


# npc_* classes that are not characters
_NOT_NPC = ("npc_bullseye", "npc_enemyfinder", "npc_enemyfinder_combinecannon", "npc_heli_avoidsphere",
            "npc_heli_avoidbox", "npc_heli_nobomb", "npc_launcher", "npc_missiledefense", "npc_puppet",
            "npc_spotlight", "npc_furniture", "npc_vehicledriver", "npc_apcdriver", "npc_grenade_frag",
            "npc_satchel", "npc_tripmine", "npc_concussiongrenade", "npc_contactgrenade", "npc_handgrenade",
            "npc_combinedropship", "npc_combinegunship", "npc_helicopter", "npc_strider", "npc_cranedriver")


def is_npc_class(cls):
    return cls.startswith("npc_") and not cls.endswith("_maker") and "maker" not in cls and cls not in _NOT_NPC


NPC_DEFAULT_MODELS = {}       # filled by the UI from worlddata (avoids an import cycle)


class MapObject:
    """Something placed in Hammer: a prop, a light, an NPC, an info_* entity.
    key: ("v", vmf id) - editable, from the VMF; ("s", i) static prop / ("b", i) entity of the
    BSP - view only (no VMF found)."""
    __slots__ = ("key", "classname", "origin", "angles", "model", "skin", "ent", "kv", "matrix")

    def __init__(self, key, classname, origin, angles, model="", skin=0, ent=None, kv=None):
        self.key = key
        self.classname = classname
        self.origin = [float(x) for x in origin]
        self.angles = [float(x) for x in angles]
        self.model = (model or "").replace("\\", "/").lower()
        self.skin = int(skin or 0)
        self.ent = ent
        self.kv = kv or {}
        self.matrix = None
        self.update_matrix()

    @property
    def editable(self):
        return self.ent is not None

    def get(self, k, default=""):
        if self.ent is not None:
            return self.ent.get(k, default)
        return self.kv.get(k, default)

    def is_npc(self):
        return is_npc_class(self.classname)

    def draw_model(self):
        if self.model.endswith(".mdl"):
            return self.model
        if self.is_npc():
            return NPC_DEFAULT_MODELS.get(self.classname, "")
        return ""

    def draw_angles(self):
        return [0.0, self.angles[1], 0.0] if self.is_npc() else self.angles

    def update_matrix(self):
        self.matrix = model_matrix(self.origin, self.draw_angles())

    def set_transform(self, origin=None, angles=None):
        if origin is not None:
            self.origin = [float(x) for x in origin]
            if self.ent is not None:
                self.ent.origin = self.origin
        if angles is not None:
            self.angles = [float(x) for x in angles]
            if self.ent is not None:
                self.ent.angles = self.angles
        self.update_matrix()

    def set_key(self, k, v):
        if self.ent is None:
            return
        self.ent.set(k, v)
        if k.lower() == "model":
            self.model = str(v).replace("\\", "/").lower()
        elif k.lower() == "skin":
            try:
                self.skin = int(float(v or 0))
            except ValueError:
                self.skin = 0
        elif k.lower() == "classname":
            self.classname = str(v)

    def label(self):
        tn = self.get("targetname")
        if self.model:
            base = self.model.rsplit("/", 1)[-1][:-4]
            return "%s  (%s)" % (tn, base) if tn else base
        return tn or self.classname


class MapScene:
    def __init__(self, path, fs, materials=None, progress=None, vmf=None, geometry="bsp"):
        """geometry: "bsp" - the compiled map; "vmf" - brushes straight from the VMF (what Hammer++ has saved,
        no compile; also the user's task markers for Claude)"""
        self.path = path
        self.name = os.path.splitext(os.path.basename(path))[0]
        self.fs = fs
        self.vmf = vmf
        self.geometry = geometry if (geometry == "vmf" and vmf is not None) else "bsp"
        self.materials = materials or MaterialSystem(fs)
        progress = progress or (lambda f, m: None)
        progress(0.05, "Карта %s" % self.name)
        if self.geometry == "vmf":
            from .vmfgeom import VMFMap
            self.bsp = VMFMap(vmf.path, self.materials)
        else:
            self.bsp = bspmod.BSPMap(path)
        self.models = {}
        self._mray = {}
        self._mverts = {}
        self.objects = []
        progress(0.35, "Объекты")
        # BSP entities with an origin (also kept when a VMF is used: player start fallback, brush entity names)
        self.entities = []
        for i, e in enumerate(self.bsp.entities):
            o, a = _origin_angles(e)
            if o is None:
                continue
            self.entities.append((i, e.get("classname", ""), o, a, e))
        if vmf is not None:
            for ent in vmf.live():
                o = ent.get("origin")
                if not o:
                    continue
                self.objects.append(MapObject(("v", ent.id), ent.classname, ent.origin, ent.angles,
                                              ent.model, _int(ent.get("skin", "0")), ent=ent))
        else:
            for k, sp in enumerate(self.bsp.static_props):
                self.objects.append(MapObject(("s", k), "prop_static", sp.origin, sp.angles, sp.model, sp.skin,
                                              kv={"model": sp.model, "skin": str(sp.skin), "solid": str(sp.solid)}))
            for (i, cls, o, a, e) in self.entities:
                self.objects.append(MapObject(("b", i), cls, o, a, e.get("model", ""), _int(e.get("skin", "0")), kv=e))
        for ob in self.objects:
            if ob.draw_model():
                self.model(ob.draw_model())
        progress(0.6, "Поверхности для мыши")
        self.ray = RayMesh(self.bsp.triangles())
        self.bounds = self.bsp.bounds
        self._index = None
        progress(1.0, "Готово")

    # --- models
    def model(self, name):
        name = name.replace("\\", "/").lower()
        m = self.models.get(name)
        if m is None:
            m = mdlmod.load(self.fs, name)
            self.models[name] = m
        return m

    def model_ray(self, name):
        """The model's triangles (model space) for ray casts; None for broken / huge models."""
        r = self._mray.get(name, 0)
        if r == 0:
            m = self.model(name)
            r = None
            if m is not None and m.ok:
                t = m.triangles()
                if 0 < len(t) <= 80000:
                    r = RayMesh(t, chunk=128)
            self._mray[name] = r
        return r

    def model_vertices(self, name):
        """Unique vertex positions of the model (model space), at most ~3000."""
        v = self._mverts.get(name)
        if v is None:
            m = self.model(name)
            if m is None or not m.ok:
                v = np.zeros((0, 3), np.float32)
            else:
                v = np.unique(np.concatenate([x.pos for x in m.meshes]).round(2), axis=0)
                if len(v) > 3000:
                    v = v[np.linspace(0, len(v) - 1, 3000).astype(int)]
            self._mverts[name] = v
        return v

    def model_bounds(self, name):
        v = self.model_vertices(name)
        if len(v) == 0:
            m = self.model(name)
            return np.array(m.mins, np.float64), np.array(m.maxs, np.float64)
        return v.min(0).astype(np.float64), v.max(0).astype(np.float64)

    # --- object index (for ray casts against moved objects)
    def invalidate(self):
        self._index = None

    def _build_index(self, extra):
        return self._build_index_items([(ob.key, ob.draw_model(), ob.matrix) for ob in self.objects if ob.draw_model()])

    def cast(self, o, d, tmax=1e9, skip=(), extra=(), objects=True):
        """Nearest hit of the world and the objects: (t, normal, key or None) or None.
        skip: object keys to ignore; extra: [(key, model, matrix)] (the project's objects)."""
        o = np.asarray(o, np.float64)
        d = np.asarray(d, np.float64)
        best = None
        r = self.ray.cast(o, d, tmax)
        if r is not None:
            best = (r[0], r[1], None)
        if not objects:
            return best
        if self._index is None:
            self._index = self._build_index(())
        for items, cs, rs in (self._index, self._build_index_items(extra)):
            if not items:
                continue
            # ray vs bounding spheres
            oc = cs - o
            tc = oc @ d
            dist2 = (oc * oc).sum(1) - tc * tc
            lim = best[0] if best else tmax
            cand = np.nonzero((dist2 <= rs * rs) & (tc + rs > 0) & (tc - rs < lim))[0]
            for ci in cand[np.argsort(tc[cand] - rs[cand])]:
                key, name, m = items[ci]
                if key in skip:
                    continue
                if best is not None and tc[ci] - rs[ci] > best[0]:
                    break
                R = m[:3, :3].astype(np.float64)
                lo = R.T @ (o - m[:3, 3])
                ld = R.T @ d
                h = self.model_ray(name).cast(lo, ld, best[0] if best else tmax)
                if h is not None and (best is None or h[0] < best[0]):
                    best = (h[0], R @ h[1], key)
        return best

    def _build_index_items(self, extra):
        items = [x for x in extra if x[1] and self.model_ray(x[1]) is not None]
        if not items:
            return ([], None, None)
        cs, rs = [], []
        for _, name, m in items:
            lo, hi = self.model_bounds(name)
            cs.append(m[:3, :3].astype(np.float64) @ ((lo + hi) * 0.5) + m[:3, 3])
            rs.append(np.linalg.norm(hi - lo) * 0.5 + 1.0)
        return items, np.array(cs), np.array(rs)

    def drop_to_floor(self, pos, up=48.0, down=4096.0, skip=(), extra=()):
        """A point (NPC feet, a marker) onto the surface below it."""
        r = self.cast(np.array(pos, float) + [0, 0, up], np.array([0, 0, -1.0]), up + down, skip, extra)
        if r is None:
            return list(pos)
        return [pos[0], pos[1], pos[2] + up - r[0]]

    def lowest_offset(self, name, angles):
        """How far below the origin the model's lowest vertex is with these angles (>= 0 below)."""
        v = self.model_vertices(name)
        if len(v) == 0:
            lo, _ = self.model_bounds(name)
            return -float(lo[2])
        R = bspmod.angle_matrix(*angles).astype(np.float64)
        return -float((v @ R.T)[:, 2].min())

    def land(self, name, origin, angles, skip=(), extra=(), align=False):
        """Put a model on what is below it so that its lowest point touches it (not its origin).
        Returns (origin, angles) or None if there is nothing below. align: tilt to the surface."""
        v = self.model_vertices(name)
        origin = np.array(origin, np.float64)
        angles = list(angles)
        if align:
            r = self.cast(origin + [0, 0, 8], np.array([0, 0, -1.0]), 8192, skip, extra)
            if r is not None:
                angles = align_to_normal(angles, r[1])
        if len(v) == 0:
            lo, _ = self.model_bounds(name)
            r = self.cast(origin + [0, 0, 8], np.array([0, 0, -1.0]), 8192, skip, extra)
            if r is None:
                return None
            origin[2] = origin[2] + 8 - r[0] - float(lo[2])
            return list(origin), angles
        R = bspmod.angle_matrix(*angles).astype(np.float64)
        w = v @ R.T + origin
        zmin = w[:, 2].min()
        height = max(1.0, w[:, 2].max() - zmin)
        low = w[w[:, 2] <= zmin + max(2.0, height * 0.12)]
        if len(low) > 160:
            # spread over the footprint: keep the lowest vertex of each cell
            cell = np.floor((low[:, :2] - low[:, :2].min(0)) / max(4.0, np.ptp(low[:, :2], 0).max() / 12)).astype(int)
            keyv = cell[:, 0] * 100000 + cell[:, 1]
            order = np.lexsort((low[:, 2], keyv))
            low = low[order][np.r_[True, keyv[order][1:] != keyv[order][:-1]]]
        drop = None
        # rays start inside the object (up to its height above the vertex): a sunk object
        # finds the floor above its lowest point and rises, one in the air falls
        lift = min(height, 64.0) + 2.0
        for p in low:
            r = self.cast(p + [0, 0, lift], np.array([0, 0, -1.0]), 8192 + lift, skip, extra)
            if r is None:
                continue
            gap = r[0] - lift
            if drop is None or gap < drop:
                drop = gap
        if drop is None:
            return None
        origin[2] -= drop
        return list(origin), angles

    def player_start(self):
        for ob in self.objects:
            if ob.classname == "info_player_start":
                return ob.origin, ob.angles
        for i, cls, o, a, e in self.entities:
            if cls == "info_player_start":
                return o, a
        lo, hi = self.bounds
        return [(lo[0] + hi[0]) / 2, (lo[1] + hi[1]) / 2, hi[2]], [0, 0, 0]

    def npc_objects(self):
        return [ob for ob in self.objects if ob.is_npc()]

    def find(self, key):
        for ob in self.objects:
            if ob.key == key:
                return ob
        return None

    # --- edits (VMF objects)
    def add_object(self, ent):
        ob = MapObject(("v", ent.id), ent.classname, ent.origin, ent.angles, ent.model, _int(ent.get("skin", "0")), ent=ent)
        self.objects.append(ob)
        if ob.draw_model():
            self.model(ob.draw_model())
        self._index = None
        return ob

    def remove_object(self, ob):
        if ob in self.objects:
            self.objects.remove(ob)
            if ob.ent is not None and self.vmf is not None:
                self.vmf.remove(ob.ent)
            self._index = None


def align_to_normal(angles, n):
    """Keep the yaw, tilt the up axis to the surface normal."""
    n = np.asarray(n, np.float64)
    if n[2] < 0.2:
        return list(angles)
    yaw = math.radians(angles[1])
    fwd = np.array([math.cos(yaw), math.sin(yaw), 0.0])
    fwd = fwd - n * (fwd @ n)
    fwd /= max(1e-6, np.linalg.norm(fwd))
    left = np.cross(n, fwd)
    return matrix_angles(np.column_stack([fwd, left, n]))


def _origin_angles(e):
    org = e.get("origin")
    if not org:
        return None, None
    try:
        o = [float(x) for x in org.split()[:3]]
        a = [float(x) for x in e.get("angles", "0 0 0").split()[:3]]
    except ValueError:
        return None, None
    if len(o) < 3:
        return None, None
    if len(a) < 3:
        a = [0.0, 0.0, 0.0]
    return o, a


def _int(s):
    try:
        return int(float(s or 0))
    except ValueError:
        return 0
