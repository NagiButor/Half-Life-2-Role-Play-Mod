#!/usr/bin/env python3
"""Builds particles/hl2rpm_weather.pcf (the weather precipitation of HL2RPM)
from Mapbase's rain_fx.pcf (func_precipitation's "Particle Rain/Rainstorm/
Snow/Ash" systems).

    python make_weather_pcf.py <rain_fx.pcf> [out.pcf]

rain_fx.pcf lives in mapbase_shared/shared_content_v7_0.vpk:
    python vpk_list.py "...\\mapbase_shared\\shared_content_v7_0_dir.vpk" rain_fx.pcf --extract <dir>

Copies rain, rain_storm, snow and ash (with their mist/splash children) under
"hl2rpm_*" names with new element ids and changes them for a weather that
follows the camera:
 - "Position Within Sphere Random" (a small disc 180 u above the camera; the
   storm's drift left half of the sky empty) -> "HL2RPM Precip Spawn": a
   cylinder around the camera, drops moved upwind so they cross eye level
   inside it, own random numbers (the library's 4096 entry table repeated the
   same drop positions every second);
 - the "Cull relative to Ray Trace Environment" stack and "Lifetime from Time
   to Impact" (48 cached traces, splashes only at those 48 points, drops
   through thin roofs) -> "HL2RPM Precip Cover": roof map test at the spawn,
   exact lifetime to the surface along the path, impact points recorded;
 - splash children: "Position from Parent Cache" -> "HL2RPM Precip Splash Position";
 - velocity noise symmetric (the originals always pushed the rain to -x -y),
   the wind comes from the control point orientation set by the client.
The HL2RPM initializers are in game/client/weather/weather_precip.cpp.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from dmx import DMX, Element, clone  # noqa: E402

DEFAULT_OUT = r"E:\Steam\steamapps\sourcemods\hl2rpm\particles\hl2rpm_weather.pcf"


def op(function_name, **params):
    e = Element("DmeParticleOperator", function_name)
    e.set("functionName", function_name, "string")
    for k, v in params.items():
        name = k.replace("__", " ")
        if isinstance(v, bool):
            t = "bool"
        elif isinstance(v, int):
            t = "int"
        elif isinstance(v, float):
            t = "float"
        elif isinstance(v, tuple) and len(v) == 3:
            t = "vector3"
        else:
            t = "string"
        e.set(name, v, t)
    return e


def ops(system, key):
    a = system.attr(key)
    return a.value if a else []


def find(system, key, function_name):
    return [o for o in ops(system, key) if o.get("functionName") == function_name]


def remove(system, key, function_name):
    a = system.attr(key)
    before = len(a.value)
    a.value = [o for o in a.value if o.get("functionName") != function_name]
    return before - len(a.value)


def replace(system, key, function_name, new_op):
    a = system.attr(key)
    for i, o in enumerate(a.value):
        if o.get("functionName") == function_name:
            a.value[i] = new_op
            return True
    raise KeyError("%s: no %s in %s" % (system.name, function_name, key))


def setp(system, key, function_name, **params):
    lst = find(system, key, function_name)
    if not lst:
        raise KeyError("%s: no %s" % (system.name, function_name))
    for o in lst:
        for k, v in params.items():
            name = k.replace("__", " ")
            a = o.attr(name)
            if isinstance(v, tuple):
                t = "vector3"
            elif isinstance(v, bool):
                t = "bool"
            elif isinstance(v, int) and (a is None or a.type == "int"):
                t = "int"
            else:
                t = "float"
                v = float(v)
            if a:
                a.value = v
                a.type = t
            else:
                o.set(name, v, t)


def set_sys(system, **attrs):
    for k, v in attrs.items():
        name = k.replace("__", " ")
        a = system.attr(name)
        t = "int" if isinstance(v, int) else "float"
        if a:
            a.value = v
        else:
            system.set(name, v, t)


def children(system):
    return [c.get("child") for c in ops(system, "children")]


def precip_spawn(radius, h_min, h_max, s_min, s_max, bias):
    return op("HL2RPM Precip Spawn", control_point_number=1, radius_min=0.0, radius_max=float(radius),
              height_min=float(h_min), height_max=float(h_max), speed_min=float(s_min), speed_max=float(s_max),
              center_bias=float(bias))


def precip_cover(record, step=16.0, accel=(0.0, 0.0, 0.0)):
    return op("HL2RPM Precip Cover", cull__covered__spawn=True, lifetime__to__impact=True,
              record__impacts=record, step=float(step), acceleration=accel)


def convert_falling(s, spawn, cover, lifetime, rate, max_particles, noise):
    """common changes of a rain/snow system"""
    replace(s, "initializers", "Position Within Sphere Random", spawn)
    remove(s, "initializers", "Position Modify Offset Random")
    remove(s, "initializers", "Cull relative to Ray Trace Environment")
    remove(s, "initializers", "Lifetime from Time to Impact")
    s.attr("initializers").value.append(cover)
    setp(s, "initializers", "Lifetime Random", lifetime_min=lifetime[0], lifetime_max=lifetime[1])
    setp(s, "initializers", "Velocity Noise", output__minimum=(-noise, -noise, 0.0), output__maximum=(noise, noise, 0.0),
         Absolute__Value=(0.0, 0.0, 0.0), Invert__Abs__Value=(0.0, 0.0, 0.0))
    setp(s, "emitters", "emit_continuously", emission_rate=float(rate))
    set_sys(s, max_particles=int(max_particles))


def convert_splash(child, rate, max_particles, offset=3.0):
    replace(child, "initializers", "Position from Parent Cache",
            op("HL2RPM Precip Splash Position", offset__radius=float(offset)))
    setp(child, "emitters", "emit_continuously", emission_rate=float(rate))
    set_sys(child, max_particles=int(max_particles))


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 1
    src = DMX.load(argv[1])
    out = argv[2] if len(argv) > 2 else DEFAULT_OUT
    sysmap = {e.name: e for e in src.elements if e.type == "DmeParticleSystemDefinition"}

    memo = {}

    def rename(name, e):
        return "hl2rpm_" + name if e.type == "DmeParticleSystemDefinition" else name

    rain = clone(sysmap["rain"], memo, rename)
    storm = clone(sysmap["rain_storm"], memo, rename)
    snow = clone(sysmap["snow"], memo, rename)
    ash = clone(sysmap["ash"], memo, rename)

    # --- rain: light to heavy rain
    convert_falling(rain, precip_spawn(420, -40, 560, 760, 840, 3.0), precip_cover(True),
                    (1.2, 1.2), rate=4800, max_particles=4200, noise=18.0)
    for c in children(rain):
        if c.name.endswith("impact_04"):
            convert_splash(c, rate=2200, max_particles=900)

    # --- storm: denser, faster, gusty; no longer glued to the camera
    convert_falling(storm, precip_spawn(460, -40, 640, 860, 1000, 3.0), precip_cover(True),
                    (1.2, 1.2), rate=7500, max_particles=6500, noise=40.0)
    remove(storm, "operators", "Movement Lock to Control Point")
    for c in children(storm):
        if c.name.endswith("impact_04"):
            convert_splash(c, rate=3200, max_particles=1400)
        elif c.name.endswith("impact_04b"):
            convert_splash(c, rate=90, max_particles=120, offset=6.0)

    # --- snow and ash: slow flakes all over the volume, landing on roofs
    convert_falling(snow, precip_spawn(420, -80, 300, 35, 70, 2.2), precip_cover(False, 8.0, (0.0, 0.0, -8.0)),
                    (2.5, 4.5), rate=1800, max_particles=7000, noise=14.0)
    setp(snow, "operators", "Movement Basic", gravity=(0.0, 0.0, -8.0))
    convert_falling(ash, precip_spawn(420, -80, 300, 20, 45, 2.2), precip_cover(False, 8.0, (0.0, 0.0, -6.0)),
                    (3.0, 5.0), rate=1300, max_particles=6000, noise=14.0)
    setp(ash, "operators", "Movement Basic", gravity=(0.0, 0.0, -6.0))

    # every system (children too) goes into the root list
    systems = []

    def collect(s):
        if s in systems:
            return
        systems.append(s)
        for c in children(s):
            collect(c)

    for s in (rain, storm, snow, ash):
        collect(s)

    root = Element("DmElement", "untitled")
    root.set("particleSystemDefinitions", systems, "element_array")
    d = DMX(src.header, src.version, [root])
    os.makedirs(os.path.dirname(out), exist_ok=True)
    n = d.save(out)
    print("wrote %s (%d bytes): %s" % (out, n, ", ".join(s.name for s in systems)))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
