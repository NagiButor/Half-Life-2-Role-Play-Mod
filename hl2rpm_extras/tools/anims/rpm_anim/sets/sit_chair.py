"""Sitting on a chair / bench / couch with a seat height h (surface above the floor).

The origin is where the character stands before sitting down, facing away from the seat; the
pelvis ends PELVIS_BACK units behind it, PELVIS_OVER_SEAT above the seat surface. The library
compiles every sequence for SEAT_HEIGHTS and blends them by the pose parameter rpm_seat_h, so a
marker of the Creation Kit only has to say how high its seat is (and stand PELVIS_BACK in front
of where the pelvis goes - the kit computes it from the furniture)."""
import numpy as np

from .. import layers as L
from .. import posebuilder as PB
from .. import rig as R

SEAT_HEIGHTS = (10.0, 17.0, 24.0)
PELVIS_BACK = 24.0          # origin -> pelvis, along the facing (the standing spot clears the seat)
PELVIS_OVER_SEAT = 6.5      # pelvis bone above the seat surface (male mesh, thighs on the seat)
SEAT_SPINE = 0.0
SEAT_HEAD = -3.0
ANKLE_Z = 4.2
SHIN = 16.53
THIGH = 17.85


# the pelvis bone above the seat surface for each skeleton's mesh (thighs resting on the seat)
OVER_SEAT_BY_SKEL = {"male": 6.5, "female": 4.6, "police": 6.5, "combine": 6.5}


def configure(rig, skel):
    """leg lengths and the seat offset of this skeleton (module constants used by the sitting sets)"""
    global THIGH, SHIN, PELVIS_OVER_SEAT, ANKLE_Z
    THIGH = float(np.linalg.norm(rig.rest_pos[rig.index("l_calf")]))
    SHIN = float(np.linalg.norm(rig.rest_pos[rig.index("l_foot")]))
    PELVIS_OVER_SEAT = OVER_SEAT_BY_SKEL.get(skel, 6.5)
    ANKLE_Z = 4.2


def bases(rig, skel):
    from ..library import STOCK
    ss, shared = STOCK[skel]
    stand = R.stock_clip(rig, shared, "idle_subtle").slice(0, 1)
    sit = R.stock_clip(rig, ss, "d1_t02_plaza_sit02").slice(0, 1)   # upright, head up
    return stand, sit


def seat_geometry(h, pelvis_back=PELVIS_BACK, over_seat=None, feet_forward=2.5):
    """pelvis, feet, hands of the upright seated pose for seat height h"""
    pz = h + (PELVIS_OVER_SEAT if over_seat is None else over_seat)
    px = -pelvis_back
    # shins lean forward a little, knees as high as the shin allows
    knee_z = ANKLE_Z + np.sqrt(SHIN ** 2 - feet_forward ** 2)
    dz = np.clip(knee_z - pz, -THIGH * 0.95, THIGH * 0.95)
    reach = np.sqrt(THIGH ** 2 - dz ** 2)
    fx = px + reach + feet_forward
    if knee_z < pz - THIGH * 0.95:      # a high seat: the feet can't reach - they dangle under the knees
        fx = px + 6.0
    g = {
        "pelvis": np.array([px, 0.0, pz]),
        "l_foot": np.array([fx, 5.2, ANKLE_Z]), "r_foot": np.array([fx - 1.0, -5.2, ANKLE_Z]),
        # hands resting on the thighs, half way to the knees
        "l_hand": np.array([px + reach * 0.55, 5.0, pz + 2.6]), "r_hand": np.array([px + reach * 0.6, -5.5, pz + 2.4]),
        "knee_x": px + reach, "knee_z": pz + dz,
    }
    return g


def stand_geometry(stand):
    wp, _ = stand.world()
    r = stand.rig
    return {k: wp[0, r.index(k)].copy() for k in ("pelvis", "l_foot", "r_foot", "l_hand", "r_hand")}


CHAIR_POSTURE = {"pelvis_rot": [0, 0, 0], "spine": [SEAT_SPINE, 0, 0], "head": [SEAT_HEAD, 0, 0]}


def enter(rig, skel, h, name="rpm_sit_chair_enter", g=None, post=None, settle=None):
    """settle: extra keys after the landing (a couch: lean back into the backrest)"""
    stand, sit = bases(rig, skel)
    s = stand_geometry(stand)
    g = g or seat_geometry(h)
    post = post or CHAIR_POSTURE
    p0, p1 = s["pelvis"], g["pelvis"]
    mid = np.array([p1[0] * 0.55, 0.0, p1[2] + 9.0])
    # the feet: planted where the character stands, then shuffled into the seated place
    lf_mid = s["l_foot"].copy()
    rf_mid = s["r_foot"].copy()
    tr = {
        "base": PB.Track((0, 0), (0.5, 0.0), (1.9, 1.0)),
        "pelvis": PB.Track((0, p0), (0.35, p0 + [-1.0, 0.6, -0.6]), (1.05, mid, "smooth"), (1.45, p1 + [0, 0, -0.7], "out"),
                           (1.8, p1 + [0, 0, 0.25]), (2.3, p1)),
        "pelvis_rot": PB.Track((0, [0, 0, 0]), (0.35, [0, 4, -2]), (1.05, [14, 0, 0]), (1.45, [6, 0, 0]), (2.0, post["pelvis_rot"])),
        "spine": PB.Track((0, [0, 0, 0]), (0.35, [4, 10, 0]), (1.05, [28, 0, 0], "smooth"), (1.45, [14, 0, 0]),
                          (1.85, np.add(post["spine"], [-3, 0, 0])), (2.3, post["spine"])),
        "head": PB.Track((0, [0, 0, 0]), (0.35, [18, 25, 4]), (0.8, [10, 8, 0]), (1.1, [-14, 0, 0]), (1.5, post["head"]),
                         (2.3, post["head"])),
        "l_foot": PB.Track((0, s["l_foot"]), (1.6, lf_mid), (1.95, g["l_foot"] + [0, 0, 0.0])),
        "r_foot": PB.Track((0, s["r_foot"]), (1.75, rf_mid), (2.15, g["r_foot"])),
        "l_hand": PB.Track((0, s["l_hand"]), (0.5, s["l_hand"] + [1, 0.5, 1]), (1.15, [g["knee_x"] - 4, 6.5, g["knee_z"] + 4.5]),
                           (1.6, [g["knee_x"] - 4.5, 5.6, g["knee_z"] + 3.0]), (2.3, g["l_hand"])),
        "r_hand": PB.Track((0, s["r_hand"]), (0.55, s["r_hand"] + [1, -0.5, 1]), (1.2, [g["knee_x"] - 3.5, -6.5, g["knee_z"] + 4.5]),
                           (1.65, [g["knee_x"] - 4.0, -6.0, g["knee_z"] + 3.0]), (2.3, g["r_hand"])),
        "l_knee": PB.Track((0, [1.0, 0.1, 0]), (2.3, [1.0, 0.18, 0.1])),
        "r_knee": PB.Track((0, [1.0, -0.1, 0]), (2.3, [1.0, -0.18, 0.1])),
        "l_fingers": PB.Track((0, 0.15), (1.2, 0.05), (2.3, 0.3)),
        "r_fingers": PB.Track((0, 0.15), (1.2, 0.05), (2.3, 0.3)),
    }
    _lift_steps(tr, [("l_foot", 1.6, 1.95), ("r_foot", 1.75, 2.15)])
    dur = 2.4
    if settle:
        # more keys after the landing: (time, {channel: value})
        for t, vals in settle:
            for ch, v in vals.items():
                if ch in tr:
                    tr[ch] = PB.Track(*(tr[ch].keys + [(t, np.asarray(v, dtype=np.float64), "smoother")]))
                else:
                    tr[ch] = PB.Track((0, v), (t, v))
            dur = max(dur, t + 0.1)
    c = PB.build(rig, stand, sit, tr, dur, name=name, flat_feet=stand)
    L.breathe(c, seated=True, amount=0.6)
    return c


def exit_(rig, skel, h, name="rpm_sit_chair_exit", g=None, post=None, feet_min=None):
    """feet_min: the feet may not go further back than this (relative to the pelvis): a couch is
    solid down to the floor, the feet can't tuck under it"""
    stand, sit = bases(rig, skel)
    s = stand_geometry(stand)
    g = g or seat_geometry(h)
    post = post or CHAIR_POSTURE
    p0, p1 = g["pelvis"], s["pelvis"]
    # to rise the feet go under the body first
    lim = -1e9 if feet_min is None else p0[0] + feet_min
    lf_t = np.array([max(lim, min(g["l_foot"][0], p0[0] + 13.0)), 5.0, ANKLE_Z])
    rf_t = np.array([max(lim, min(g["r_foot"][0], p0[0] + 12.0)), -5.0, ANKLE_Z])
    tr = {
        "base": PB.Track((0, 1.0), (1.0, 1.0), (2.2, 0.0)),
        "pelvis": PB.Track((0, p0), (0.55, p0 + [0.5, 0, 0.2]), (1.0, p0 + [3.0, 0, 1.0], "in"),
                           (1.55, [p0[0] * 0.45, 0, p1[2] - 6.0], "smooth"), (2.0, p1 + [0, 0, 0.3]), (2.5, p1)),
        "pelvis_rot": PB.Track((0, post["pelvis_rot"]), (0.55, [4, 0, 0]), (1.0, [16, 0, 0]), (1.55, [10, 0, 0]), (2.1, [0, 0, 0])),
        "spine": PB.Track((0, post["spine"]), (0.55, [10, 0, 0]), (1.0, [34, 0, 0], "smooth"), (1.55, [16, 0, 0]),
                          (2.05, [-3, 0, 0]), (2.5, [0, 0, 0])),
        "head": PB.Track((0, post["head"]), (0.5, [-4, 0, 0]), (1.0, [-22, 0, 0]), (1.6, [-8, 0, 0]), (2.5, [0, 0, 0])),
        "l_foot": PB.Track((0, g["l_foot"]), (0.25, g["l_foot"]), (0.55, lf_t), (1.6, lf_t), (2.2, s["l_foot"])),
        "r_foot": PB.Track((0, g["r_foot"]), (0.35, g["r_foot"]), (0.7, rf_t), (1.7, rf_t), (2.35, s["r_foot"])),
        "l_hand": PB.Track((0, g["l_hand"]), (0.6, [g["knee_x"] - 2.0, 5.5, g["knee_z"] + 3.0]),
                           (1.2, [g["knee_x"] - 1.0, 5.8, g["knee_z"] + 2.0]), (1.7, [p1[0] + 2, 9.0, p1[2] + 2]), (2.5, s["l_hand"])),
        "r_hand": PB.Track((0, g["r_hand"]), (0.65, [g["knee_x"] - 1.5, -5.8, g["knee_z"] + 3.0]),
                           (1.25, [g["knee_x"] - 0.5, -6.0, g["knee_z"] + 2.0]), (1.75, [p1[0] + 2, -9.0, p1[2] + 2]), (2.5, s["r_hand"])),
        "l_fingers": PB.Track((0, 0.3), (0.6, 0.1), (1.6, 0.1), (2.5, 0.15)),
        "r_fingers": PB.Track((0, 0.3), (0.6, 0.1), (1.6, 0.1), (2.5, 0.15)),
    }
    _lift_steps(tr, [("l_foot", 0.25, 0.55), ("r_foot", 0.35, 0.7), ("l_foot", 1.6, 2.2), ("r_foot", 1.7, 2.35)])
    c = PB.build(rig, sit, stand, _swap_base(tr), 2.6, name=name, flat_feet=stand)
    L.breathe(c, amount=0.6)
    return c


def _swap_base(tr):
    # build() blends base_a -> base_b by "base"; exit starts seated: give it (sit, stand) and 1 - base
    t = tr["base"]
    tr["base"] = PB.Track(*[(k[0], 1.0 - float(k[1]), k[2]) for k in t.keys])
    return tr


def _lift_steps(tr, steps, height=2.2):
    """add a lift arc to foot tracks between their keys at the given times"""
    for ch, a, b in steps:
        keys = tr[ch].keys
        va = tr[ch].sample([a])[0]
        vb = tr[ch].sample([b])[0]
        if np.linalg.norm(vb - va) < 0.8:
            continue
        mid = (va + vb) / 2 + [0, 0, height]
        keys = [k for k in keys if not (a < k[0] < b)] + [((a + b) / 2, mid, "smooth")]
        tr[ch] = PB.Track(*sorted(keys, key=lambda k: k[0]))


# ------------------------------------------------------------------ the long sitting loop
def idle(rig, skel, h, name="rpm_sit_chair_idle", seed=11):
    stand, sit = bases(rig, skel)
    g = seat_geometry(h)
    P = g["pelvis"]
    kx, kz = g["knee_x"], g["knee_z"]
    # key poses (channel values)
    up = {"pelvis": P, "pelvis_rot": [0, 0, 0], "spine": [SEAT_SPINE, 0, 0], "head": [SEAT_HEAD, 0, 0],
          "l_hand": g["l_hand"], "r_hand": g["r_hand"], "l_foot": g["l_foot"], "r_foot": g["r_foot"],
          "l_elbow": [-0.3, 0.6, -1.0], "r_elbow": [-0.3, -0.6, -1.0], "r_knee": [1.0, -0.18, 0.1],
          "l_fingers": 0.3, "r_fingers": 0.3}
    # leaning forward, elbows on the knees, hands hanging between them
    lean = dict(up, **{"pelvis": P + [1.0, 0, -0.2], "pelvis_rot": [16, 0, 0], "spine": [36, 0, 0], "head": [-30, 0, 0],
                       "l_elbow": [0.55, 0.15, -0.85], "r_elbow": [0.55, -0.15, -0.85], "l_fingers": 0.5, "r_fingers": 0.5})
    _, j = PB.probe(rig, stand, sit, {k: v for k, v in lean.items() if not k.endswith("_hand")}, flat_feet=stand)
    lk, rk = j("l_calf"), j("r_calf")
    lean["l_hand"] = (lk + rk) / 2 + [5.5, 1.4, 1.5]
    lean["r_hand"] = (lk + rk) / 2 + [6.0, -1.2, 2.2]
    # leaning back, arms crossed on the chest
    back = dict(up, **{"pelvis": P + [-0.8, 0, -0.3], "pelvis_rot": [-6, 0, 0], "spine": [-8, 0, 0], "head": [-1, 0, 0],
                       "l_elbow": [0.5, 0.8, -0.6], "r_elbow": [0.5, -0.8, -0.6], "l_fingers": 0.25, "r_fingers": 0.2})
    _, j = PB.probe(rig, stand, sit, {k: v for k, v in back.items() if not k.endswith("_hand")}, flat_feet=stand)
    chest = j("spine4")
    back["l_hand"] = chest + [7.5, -5.5, -6.5]     # the left hand under the right elbow
    back["r_hand"] = chest + [8.5, 5.0, -4.5]      # the right hand over the left forearm
    # right ankle on the left knee (figure four)
    lk = np.array([kx, 4.5, kz])
    cross = dict(up, **{"pelvis": P + [-0.3, 0.6, 0.2], "pelvis_rot": [-3, -4, 3], "spine": [2, 6, -2], "head": [-4, 8, 0],
                        "r_foot": lk + [1.5, -1.0, 5.5], "r_knee": [0.45, -1.0, 0.5],
                        "l_foot": g["l_foot"] + [-1.5, -1.0, 0.0],
                        "r_hand": lk + [-1.0, -6.0, 9.0], "l_hand": lk + [-5.0, -1.5, 7.0],
                        "l_fingers": 0.4, "r_fingers": 0.45})
    seq = [(up, 0, 6.0), (lean, 1.7, 7.0), (up, 1.5, 4.5), (back, 1.6, 8.0), (up, 1.5, 3.5), (cross, 2.2, 9.0),
           (up, 2.0, 3.0)]
    chans = list(up.keys())
    tr = {}
    t = 0.0
    keys = {c: [] for c in chans}
    for pose, move, hold in seq:
        t += move
        for c in chans:
            keys[c].append((t, pose[c], "smoother"))
        t += hold
        for c in chans:
            keys[c].append((t, pose[c], "smoother"))
    keys = {c: [(0.0, up[c])] + v[1:] for c, v in keys.items()}
    for c in chans:
        tr[c] = PB.Track(*keys[c])
    tr["base"] = PB.Track((0, 1.0), (t, 1.0))
    tr["r_foot"] = _with_lift(tr["r_foot"], 1.2)
    c = PB.build(rig, stand, sit, tr, t, name=name, loop=True, flat_feet=stand)
    c.loop = True
    L.breathe(c, seated=True, amount=1.0)
    L.glance(c, n=9, yaw_deg=38, pitch_deg=10, seed=seed, weight=0.8)
    return c


def _with_lift(track, height):
    """feet moving between holds rise a little in the middle of the move"""
    ks = track.keys
    out = [ks[0]]
    for a, b in zip(ks, ks[1:]):
        if np.linalg.norm(b[1] - a[1]) > 1.0:
            out.append(((a[0] + b[0]) / 2, (a[1] + b[1]) / 2 + [0, 0, height], "smooth"))
        out.append(b)
    return PB.Track(*out)
