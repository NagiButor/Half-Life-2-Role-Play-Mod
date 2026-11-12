"""Measure furniture props: where the seat (or the mattress) is, how high, which way it faces,
where the backrest is. Model space of the prop (Source: +z up).

The animation library fits its sit / lie poses to these numbers, and the penetration check
(contact.py) uses the triangles."""
import numpy as np

from . import rig as R


def triangles(model):
    """(N, 3, 3) triangles of body 0 of a prop, model space"""
    m = R.dec.Model(R.game_fs(), model)
    tris = []
    for _bname, _bbase, models in m.bodyparts():
        if not models:
            continue
        _mname, meshes = models[0]
        for (skinref, P, N, U, I, W, B) in meshes:
            P = np.asarray(P, dtype=np.float64)
            I = np.asarray(I, dtype=np.int64).reshape(-1, 3)
            tris.append(P[I])
    if not tris:
        return np.zeros((0, 3, 3))
    return np.concatenate(tris)


def analyze(model, lo=6.0, hi=40.0):
    """the seat of a prop (dlgedit.world.seatfit.analyze on its triangles), plus model and tris"""
    from dlgedit.world import seatfit
    T = triangles(model)
    out = seatfit.analyze(T, lo, hi)
    if out is None:
        return None
    out["model"] = model
    out["tris"] = T
    return out


def describe(info):
    if info is None:
        return "no geometry"
    s = "%s: box %s..%s" % (info["model"], np.round(info["mins"], 1), np.round(info["maxs"], 1))
    if "seat_z" in info:
        s += "\n   seat %.1f above the floor (z %.1f), center %s, size %s, area %.0f" % (
            info["seat_h"], info["seat_z"], np.round(info["seat_center"][:2], 1),
            np.round(info["seat_max"][:2] - info["seat_min"][:2], 1), info["seat_area"])
    if "front" in info:
        s += "\n   faces %s, backrest %.1f behind the center (top z %.1f), front edge %.1f ahead" % (
            np.round(info["front"], 2), info["back_dist"], info["back_top"], info["front_edge"])
    return s


def ray_down(T, xy, z0):
    """height of the first surface under (x, y) from z0 for many points xy (N,2); NaN = nothing"""
    v0, v1, v2 = T[:, 0], T[:, 1], T[:, 2]
    e1, e2 = v1 - v0, v2 - v0
    d = np.array([0, 0, -1.0])
    h = np.cross(d, e2)
    a = np.einsum("mi,mi->m", e1, h)
    ok = np.abs(a) > 1e-9
    e1, e2, v0, h, a = e1[ok], e2[ok], v0[ok], h[ok], a[ok]
    f = 1.0 / a
    P = np.c_[xy, np.full(len(xy), z0)]
    sv = P[:, None, :] - v0[None]
    u = f[None] * np.einsum("nmi,mi->nm", sv, h)
    q = np.cross(sv, e1[None])
    v = f[None] * np.einsum("i,nmi->nm", d, q)
    t = f[None] * np.einsum("mi,nmi->nm", e2, q)
    hit = (u >= 0) & (u <= 1) & (v >= 0) & (u + v <= 1) & (t > 0)
    t = np.where(hit, t, np.inf)
    tmin = t.min(1)
    return np.where(np.isfinite(tmin), z0 - tmin, np.nan)


def surface_height(T, center, half, seat_z, frac=0.45):
    """median top surface over the middle part (frac of the half sizes) of the seat"""
    gx = np.linspace(-half[0] * frac, half[0] * frac, 7) + center[0]
    gy = np.linspace(-half[1] * frac, half[1] * frac, 9) + center[1]
    xy = np.array([(x, y) for x in gx for y in gy])
    z = ray_down(T, xy, seat_z + 40.0)
    z = z[np.isfinite(z) & (z < seat_z + 12)]
    return float(np.median(z)) if len(z) else None


def marker_for(model, origin, yaw_deg, pelvis_back, knee_reach, info=None):
    """where a sitting marker of the animation library goes for a prop at origin / yaw:
    the pelvis 7.5 in front of the backrest, or further forward so the knees clear the seat's front
    edge; the marker stands pelvis_back in front of the pelvis, facing away from the seat.
    -> (marker origin (3,), marker yaw degrees, seat height)"""
    info = info or analyze(model)
    th = np.radians(yaw_deg)
    Rz = np.array([[np.cos(th), -np.sin(th), 0], [np.sin(th), np.cos(th), 0], [0, 0, 1.0]])
    f = info.get("front", np.array([1.0, 0.0]))
    a = np.degrees(np.arctan2(f[1], f[0]))
    snapped = round(a / 90.0) * 90.0
    if abs(a - snapped) < 6.0:          # furniture is modelled along the axes; the backrest estimate wobbles
        a = snapped
    fwd_model = np.array([np.cos(np.radians(a)), np.sin(np.radians(a)), 0.0])
    pel = -(info["back_dist"] - 7.5) if "back_dist" in info else -1.5
    if "front_edge" in info:
        pel = max(pel, info["front_edge"] + 1.5 - knee_reach)
    center = np.array([info["seat_center"][0], info["seat_center"][1], info["floor"]])
    pelvis_model = center + fwd_model * pel
    fwd = Rz @ fwd_model
    pelvis_world = np.asarray(origin, dtype=np.float64) + Rz @ pelvis_model
    marker = pelvis_world + fwd * pelvis_back
    yaw = np.degrees(np.arctan2(fwd[1], fwd[0]))
    return marker, yaw, info["seat_h"]
