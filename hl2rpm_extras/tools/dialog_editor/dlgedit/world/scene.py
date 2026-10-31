"""A loaded map for the render window (CPU side): the BSP, its static props with
their models, the entities placed in Hammer, and ray casts against all of it."""
import os

import numpy as np

from . import bsp as bspmod
from . import mdl as mdlmod
from .materials import MaterialSystem

ITEM_MODELS = {
    "item_healthkit": "models/items/healthkit.mdl", "item_healthvial": "models/healthvial.mdl",
    "item_battery": "models/items/battery.mdl", "item_suit": "models/items/hevsuit.mdl",
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
}


def model_matrix(origin, angles):
    m = np.eye(4, dtype=np.float32)
    m[:3, :3] = bspmod.angle_matrix(*angles)
    m[:3, 3] = origin
    return m


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
        if best is None:
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


class PropInstance:
    __slots__ = ("model", "origin", "angles", "skin", "matrix")

    def __init__(self, model, origin, angles, skin):
        self.model, self.origin, self.angles, self.skin = model, origin, angles, skin
        self.matrix = model_matrix(origin, angles)


class MapScene:
    def __init__(self, path, fs, materials=None, progress=None):
        self.path = path
        self.name = os.path.splitext(os.path.basename(path))[0]
        self.fs = fs
        self.materials = materials or MaterialSystem(fs)
        progress = progress or (lambda f, m: None)
        progress(0.05, "Карта %s" % self.name)
        self.bsp = bspmod.BSPMap(path)
        self.models = {}
        self.props = []
        progress(0.35, "Пропы")
        for sp in self.bsp.static_props:
            self.model(sp.model)
            self.props.append(PropInstance(sp.model.lower(), sp.origin, sp.angles, sp.skin))
        # dynamic props / physics props / NPCs placed in Hammer
        self.entities = []
        for i, e in enumerate(self.bsp.entities):
            cls = e.get("classname", "")
            org = e.get("origin")
            if not org:
                continue
            try:
                o = [float(x) for x in org.split()[:3]]
                a = [float(x) for x in e.get("angles", "0 0 0").split()[:3]]
            except ValueError:
                continue
            if len(o) < 3:
                continue
            if len(a) < 3:
                a = [0.0, 0.0, 0.0]
            mdl = e.get("model", "")
            if cls.startswith("prop_") and mdl.endswith(".mdl"):
                self.model(mdl)
                self.props.append(PropInstance(mdl.lower(), tuple(o), tuple(a), int(float(e.get("skin", "0") or 0))))
                continue
            self.entities.append((i, cls, o, a, e))
        progress(0.6, "Поверхности для мыши")
        tris = [self.bsp.triangles()]
        for p in self.props:
            m = self.models.get(p.model)
            if m is None or not m.ok:
                continue
            t = m.triangles()
            if len(t) == 0 or len(t) > 20000:
                continue
            w = t.reshape(-1, 3) @ p.matrix[:3, :3].T + p.matrix[:3, 3]
            tris.append(w.reshape(-1, 3, 3))
        self.ray = RayMesh(np.concatenate(tris) if tris else np.zeros((0, 3, 3)))
        self.bounds = self.bsp.bounds
        progress(1.0, "Готово")

    def model(self, name):
        name = name.replace("\\", "/").lower()
        m = self.models.get(name)
        if m is None:
            m = mdlmod.load(self.fs, name)
            self.models[name] = m
        return m

    def cast(self, o, d, tmax=1e9):
        return self.ray.cast(o, d, tmax)

    def drop_to_floor(self, pos, up=48.0, down=4096.0):
        r = self.cast(np.array(pos) + [0, 0, up], np.array([0, 0, -1.0]), up + down)
        if r is None:
            return list(pos)
        return [pos[0], pos[1], pos[2] + up - r[0]]

    def player_start(self):
        for i, cls, o, a, e in self.entities:
            if cls == "info_player_start":
                return o, a
        lo, hi = self.bounds
        return [(lo[0] + hi[0]) / 2, (lo[1] + hi[1]) / 2, hi[2]], [0, 0, 0]

    def npc_entities(self):
        return [(i, cls, o, a, e) for (i, cls, o, a, e) in self.entities if is_npc_class(cls)]


# npc_* classes that are not characters
_NOT_NPC = ("npc_bullseye", "npc_enemyfinder", "npc_enemyfinder_combinecannon", "npc_heli_avoidsphere",
            "npc_heli_avoidbox", "npc_heli_nobomb", "npc_launcher", "npc_missiledefense", "npc_puppet",
            "npc_spotlight", "npc_furniture", "npc_vehicledriver", "npc_apcdriver", "npc_grenade_frag",
            "npc_satchel", "npc_tripmine", "npc_concussiongrenade", "npc_contactgrenade", "npc_handgrenade",
            "npc_combinedropship", "npc_combinegunship", "npc_helicopter", "npc_strider", "npc_cranedriver")


def is_npc_class(cls):
    return cls.startswith("npc_") and not cls.endswith("_maker") and "maker" not in cls and cls not in _NOT_NPC
