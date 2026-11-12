"""Lights of a map for the lighting preview of the map window (M7): lamps placed in Hammer or from
the kit into the VMF (light_deferred, light_deferred_spot, classic light / light_spot) and the sun of
the chosen hour. A preview of where light falls, not a copy of the game's deferred renderer."""
import math

import numpy as np

LIGHT_CLASSES = ("light_deferred", "light_deferred_spot", "light", "light_spot")


def _floats(s, n, default):
    try:
        v = [float(x) for x in str(s).split()]
    except ValueError:
        v = []
    return (v + list(default))[:n] if len(v) < n else v[:n]


def _forward(pitch, yaw):
    p, y = math.radians(pitch), math.radians(yaw)
    return np.array([math.cos(p) * math.cos(y), math.cos(p) * math.sin(y), -math.sin(p)])


class Light:
    __slots__ = ("pos", "color", "radius", "power", "spot", "cos_outer", "cos_inner", "shadow", "key")

    def __init__(self, pos, color, radius, power=2.0, spot=None, outer=45.0, inner=35.0, shadow=True, key=None):
        self.pos = np.asarray(pos, float)
        self.color = np.asarray(color, float)
        self.radius = float(radius)
        self.power = float(power)
        self.spot = None if spot is None else np.asarray(spot, float) / max(np.linalg.norm(spot), 1e-6)
        self.cos_outer = math.cos(math.radians(outer))
        self.cos_inner = math.cos(math.radians(min(inner, outer - 0.5)))
        self.shadow = shadow
        self.key = key


def light_of(ob):
    """A Light of a scene object (scene.MapObject) or None."""
    cls = ob.classname.lower()
    if cls not in LIGHT_CLASSES:
        return None

    class _KV:          # keys of the BSP entity or of the VMF entity (lamps placed from the kit)
        def get(self, k, default=None):
            v = ob.get(k, "")
            return default if v in ("", None) else v

        def __contains__(self, k):
            return ob.get(k, "") not in ("", None)

        def __getitem__(self, k):
            return ob.get(k, "")
    kv = _KV()
    flags = int(float(kv.get("spawnflags", "1") or 1)) if cls.startswith("light_deferred") else 0
    if cls.startswith("light_deferred"):
        if flags and not (flags & 1):
            return None                         # disabled
        d = _floats(kv.get("diffuse", "255 255 255 255"), 4, (255, 255, 255, 255))
        color = np.array(d[:3]) / 255.0 * (d[3] / 255.0) * 1.6
        radius = float(kv.get("radius", 256) or 256)
        power = float(kv.get("power", 2) or 2)
        spot = cls == "light_deferred_spot" or str(kv.get("light_type", "0")) == "1"
        outer = float(kv.get("spot_cone_outer", 45) or 45)
        inner = float(kv.get("spot_cone_inner", 35) or 35)
        # the deferred cone is the full angle
        outer, inner = outer * 0.5, inner * 0.5
        shadow = bool(flags & 2) or str(kv.get("autoshadow", "1")) != "0"
    else:
        lv = _floats(kv.get("_light", "255 255 255 200"), 4, (255, 255, 255, 200))
        color = np.array(lv[:3]) / 255.0 * (lv[3] / 150.0)
        dist = float(kv.get("_distance", 0) or 0)
        radius = dist if dist > 0 else 25.0 * math.sqrt(max(lv[3], 1.0))
        power = 2.0
        spot = cls == "light_spot"
        outer = float(kv.get("_cone", 45) or 45)
        inner = float(kv.get("_inner_cone", 30) or 30)
        shadow = True
    direction = None
    if spot:
        ang = list(ob.angles)
        if "pitch" in kv and cls == "light_spot":
            try:
                ang[0] = float(kv["pitch"])
            except ValueError:
                pass
        direction = _forward(ang[0], ang[1])
    return Light(ob.origin, color, radius, power, direction, outer, inner, shadow, ob.key)


def map_lights(scene):
    return [x for x in (light_of(ob) for ob in scene.objects) if x is not None]


def sun(scene, hour, night_only=False):
    """(direction towards the sun, color, ambient up, ambient down, sky color) of the hour."""
    yaw0, pitch0 = 135.0, 50.0
    for ob in scene.objects:
        if ob.classname.lower() in ("light_environment", "light_deferred_global"):
            yaw0 = ob.angles[1]
            try:
                pitch0 = abs(float(ob.kv.get("pitch", ob.angles[0]) or ob.angles[0])) or pitch0
            except ValueError:
                pass
            break
    h = hour % 24.0
    elev = math.sin(math.pi * (h - 6.0) / 12.0) * max(pitch0, 20.0) if 6.0 <= h <= 18.0 else -10.0
    day = 0.0 if night_only else max(0.0, min(1.0, (elev + 4.0) / 18.0))
    yaw = yaw0 + (h - 12.0) * 12.0
    e, y = math.radians(max(elev, 1.0)), math.radians(yaw + 180.0)
    d = np.array([math.cos(e) * math.cos(y), math.cos(e) * math.sin(y), math.sin(e)])
    warm = max(0.0, min(1.0, 1.0 - elev / 25.0))
    col = np.array([1.0, 0.93 - 0.25 * warm, 0.82 - 0.45 * warm]) * 1.05 * day
    night = np.array([0.035, 0.045, 0.075])
    amb_up = np.array([0.30, 0.36, 0.48]) * day + night * (1.0 - day)
    amb_dn = np.array([0.20, 0.18, 0.16]) * day + night * 0.6 * (1.0 - day)
    sky = np.array([0.56, 0.63, 0.71]) * day + np.array([0.04, 0.05, 0.09]) * (1.0 - day)
    if 5.0 < h < 8.0 or 16.5 < h < 19.5:
        sky = sky * 0.8 + np.array([0.35, 0.22, 0.15]) * warm * day
    return d, col, amb_up, amb_dn, sky
