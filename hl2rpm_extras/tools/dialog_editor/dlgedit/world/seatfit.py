"""Furniture for the sitting animations of the library (hl2rpm_extras/tools/anims):
where the seat of a prop is, how high, which way it faces, where the backrest and the front edge
are - and where a marker goes so a character playing a sitting animation lands on it.

The parameters of every sitting animation (how far its pelvis ends behind the marker, how high
over the seat, leg lengths) come from scripts/hl2rpm_anim_seats.txt, written by make_anims.py.
Triangles are model space, Source winding (clockwise front faces)."""
import math

import numpy as np

from .. import kv


def analyze(T, lo=6.0, hi=40.0):
    """T: (N,3,3) triangles of a prop -> dict (floor, seat_z, seat_h, seat_center, seat_min/max, front,
    back_dist, back_top, front_edge) or None when there is nothing to sit on"""
    T = np.asarray(T, dtype=np.float64)
    if not len(T):
        return None
    a, b, c = T[:, 0], T[:, 1], T[:, 2]
    n = np.cross(c - a, b - a)
    area = np.linalg.norm(n, axis=1) / 2
    nn = n / np.maximum(np.linalg.norm(n, axis=1, keepdims=True), 1e-9)
    cen = T.mean(axis=1)
    mins, maxs = T.reshape(-1, 3).min(0), T.reshape(-1, 3).max(0)
    floor = mins[2]
    rz = cen[:, 2] - floor
    up = (nn[:, 2] > 0.8) & (rz > lo) & (rz < hi)
    out = {"mins": mins, "maxs": maxs, "floor": float(floor)}
    if not up.any():
        return out
    bins = np.arange(lo, hi + 2, 2.0)
    hist, edges = np.histogram(rz[up], bins=bins, weights=area[up])
    k = int(np.argmax(hist))
    seat = up & (rz >= edges[k] - 1.5) & (rz <= edges[k + 1] + 1.5)
    sp = T[seat].reshape(-1, 3)
    w = np.repeat(area[seat], 3)
    seat_z = float(np.average(sp[:, 2], weights=w))
    smin, smax = sp.min(0), sp.max(0)
    out.update({"seat_z": seat_z, "seat_h": seat_z - floor, "seat_min": smin, "seat_max": smax,
                "seat_center": np.average(sp, axis=0, weights=w), "seat_area": float(area[seat].sum())})
    top = surface_height(T, out["seat_center"], (smax - smin)[:2] * 0.5, seat_z)
    if top is not None and top > seat_z:
        out["seat_z"] = seat_z = top
        out["seat_h"] = top - floor
    side = (np.abs(nn[:, 2]) < 0.45) & (cen[:, 2] > seat_z + 6)
    if side.any() and area[side].sum() > 30:
        bc = np.average(cen[side], axis=0, weights=area[side])
        d = (out["seat_center"] - bc)[:2]
        if np.linalg.norm(d) > 1.0:
            f = d / np.linalg.norm(d)
            out["front"] = f
            out["back_top"] = float(cen[side, 2].max())
            proj = (cen[side, :2] - out["seat_center"][:2]) @ f
            out["back_dist"] = float(-np.percentile(proj, 50))
            out["front_edge"] = float(((sp[:, :2] - out["seat_center"][:2]) @ f).max())
    return out


def ray_down(T, xy, z0):
    """height of the first surface under each (x, y) of xy (N,2), looking down from z0; NaN = nothing"""
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
    """median top surface over the middle of the seat: cushions are crowned, the flat faces found
    first are often the frame between them"""
    gx = np.linspace(-half[0] * frac, half[0] * frac, 7) + center[0]
    gy = np.linspace(-half[1] * frac, half[1] * frac, 9) + center[1]
    xy = np.array([(x, y) for x in gx for y in gy])
    z = ray_down(T, xy, seat_z + 40.0)
    z = z[np.isfinite(z) & (z < seat_z + 12)]
    return float(np.median(z)) if len(z) else None


# ------------------------------------------------------------------ the animations' numbers
def load_params(fs):
    """{actbusy name: {pelvis_back, over_seat, feet_forward, thigh, shin, ankle_z, h_min, h_max}} of
    scripts/hl2rpm_anim_seats.txt (male skeleton: the markers are fitted for it)"""
    data = fs.read("scripts/hl2rpm_anim_seats.txt") if fs is not None else None
    out = {}
    if not data:
        return out
    try:
        root = kv.parse(data.decode("utf-8", "replace"))
    except Exception:
        return out
    blk = root.block("AnimSeats") or (root.blocks()[0] if root.blocks() else None)
    if blk is None:
        return out
    for b in blk.blocks():
        out[b.name] = {c.name: float(c.value) for c in b.values()}
    return out


def bed_marker(T, p, near=None, blocked=None):
    """a bed (triangles of the bed and what lies on it - a mattress - turned so the bed's axes are x / y)
    for rpm_lie_bed with parameters p -> dict(pos, yaw, h, head_ok, blocked) in the same frame, or None.
    The marker stands at the middle of a long side, facing away; the body lies along the bed with its head
    to the marker's left - so the side is the one whose left is the headboard (the taller end).
    near: (x, y) - with ends alike, the side nearer to it; blocked(pos, outward) -> True when the side
    can not be used (a wall)."""
    info = analyze(T, lo=4.0, hi=40.0)
    if not info or "seat_h" not in info:
        return None
    smin, smax = info["seat_min"], info["seat_max"]
    c = (smin + smax) * 0.5
    ext = smax - smin
    long_ax = 0 if ext[0] >= ext[1] else 1
    short_ax = 1 - long_ax
    if ext[long_ax] < 50.0 or ext[short_ax] < 18.0:
        return None                 # not a bed: too small to lie on
    Tn = np.asarray(T, dtype=np.float64)
    cen = Tn.mean(axis=1)
    high = cen[:, 2] > info["seat_z"] + 4.0
    ends = []
    for sgn in (-1.0, 1.0):
        m = high & ((cen[:, long_ax] - c[long_ax]) * sgn > ext[long_ax] * 0.35)
        ends.append(float(cen[m, 2].max()) if m.any() else info["seat_z"])
    head_sgn = 1.0 if ends[1] > ends[0] + 2.0 else (-1.0 if ends[0] > ends[1] + 2.0 else 0.0)
    lie_x = p.get("lie_x", 32.0)
    lie_y = p.get("lie_y", 3.0)
    options = []
    for side in (-1.0, 1.0):
        out = np.zeros(3)
        out[short_ax] = side
        # the marker's left = out turned by +90 degrees
        left = np.array([-out[1], out[0], 0.0])
        hs = float(left[long_ax])
        pos = c.copy()
        pos[short_ax] += side * lie_x
        # the body centered on the bed: the pelvis is lie_y along the marker's left, the body's middle lie_center
        # from the pelvis toward the head (negative - toward the feet: they stick out further than the head)
        pos[long_ax] -= hs * (lie_y + p.get("lie_center", -3.4))
        pos[2] = info["floor"]
        yaw = math.degrees(math.atan2(out[1], out[0]))
        score = 0.0
        if head_sgn != 0.0:
            score += 10.0 if hs == head_sgn else 0.0
        elif near is not None:
            score -= math.hypot(near[0] - pos[0], near[1] - pos[1]) * 0.01
        if blocked is not None and blocked(pos, out):
            score -= 100.0
        options.append((score, pos, yaw, hs == head_sgn))
    options.sort(key=lambda o: -o[0])
    score, pos, yaw, head_ok = options[0]
    return {"pos": pos, "yaw": yaw, "h": float(info["seat_h"]), "head_ok": head_ok or head_sgn == 0.0,
            "blocked": score < -50.0}


def knee_reach(p, h):
    """how far forward of the pelvis the knee ends (the same formula as sit_chair.seat_geometry)"""
    pz = h + p["over_seat"]
    ff = p.get("feet_forward", 2.5)
    knee_z = p.get("ankle_z", 4.2) + math.sqrt(max(0.0, p["shin"] ** 2 - ff ** 2))
    dz = max(-p["thigh"] * 0.95, min(p["thigh"] * 0.95, knee_z - pz))
    return math.sqrt(p["thigh"] ** 2 - dz ** 2)


def marker_for(info, origin, yaw_deg, p):
    """the marker of a prop (analyze() of its model, placed at origin / yaw degrees) for the
    animation with parameters p -> (marker origin [3], marker yaw degrees, seat height) or None.
    The pelvis goes 7.5 in front of the backrest, or further forward so the knees clear the seat's
    front edge; the marker stands pelvis_back in front of the pelvis, facing away from the seat."""
    if not info or "seat_h" not in info:
        return None
    h = float(np.clip(info["seat_h"], p.get("h_min", 0.0), p.get("h_max", 40.0)))
    th = math.radians(yaw_deg)
    Rz = np.array([[math.cos(th), -math.sin(th), 0], [math.sin(th), math.cos(th), 0], [0, 0, 1.0]])
    f = info.get("front", np.array([1.0, 0.0]))
    a = math.degrees(math.atan2(f[1], f[0]))
    snapped = round(a / 90.0) * 90.0
    if abs(a - snapped) < 6.0:
        a = snapped
    fwd_model = np.array([math.cos(math.radians(a)), math.sin(math.radians(a)), 0.0])
    pel = -(info["back_dist"] - 7.5) if "back_dist" in info else -1.5
    if "front_edge" in info:
        pel = max(pel, info["front_edge"] + 1.5 - knee_reach(p, h))
    center = np.array([info["seat_center"][0], info["seat_center"][1], info["floor"]])
    fwd = Rz @ fwd_model
    pelvis = np.asarray(origin, dtype=np.float64) + Rz @ (center + fwd_model * pel)
    marker = pelvis + fwd * p["pelvis_back"]
    return [float(x) for x in marker], math.degrees(math.atan2(fwd[1], fwd[0])), float(info["seat_h"])
