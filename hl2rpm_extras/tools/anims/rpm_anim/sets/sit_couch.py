"""Sitting on a couch / an armchair: low, soft and deep - the body sinks in, leans back into the
backrest, the legs go forward. Same origin convention as sit_chair (the standing spot in front,
the pelvis PELVIS_BACK behind it), seat heights SEAT_HEIGHTS blended by rpm_seat_h."""
import numpy as np

from .. import layers as L
from .. import posebuilder as PB
from . import sit_chair as SC

SEAT_HEIGHTS = (10.0, 15.0, 20.0)
PELVIS_BACK = 26.0
OVER_SEAT_EXTRA = 0.5             # the pelvis over the cushion (it can not really give in the game)
FEET_FORWARD = 7.0          # shins slope forward: clear of the cushion's front, relaxed
POSTURE = {"pelvis_rot": [-10, 0, 0], "spine": [-12, 0, 0], "head": [10, 0, 0]}
BACK_TOP = 20.0             # the arms along the backrest: this high above the seat (over a rounded top)


def geometry(h):
    g = SC.seat_geometry(h, pelvis_back=PELVIS_BACK, over_seat=SC.PELVIS_OVER_SEAT + OVER_SEAT_EXTRA, feet_forward=FEET_FORWARD)
    P = g["pelvis"]
    # hands on the lap, closer to the belly when reclined
    g["l_hand"] = np.array([P[0] + 9.0, 4.5, P[2] + 4.0])
    g["r_hand"] = np.array([P[0] + 9.5, -5.0, P[2] + 3.8])
    return g


def enter(rig, skel, h, name="rpm_sit_couch_enter"):
    g = geometry(h)
    P = g["pelvis"]
    # land upright-ish, then sink back into the backrest with an exhale
    settle = [(2.9, {"pelvis": P + [-0.6, 0, -0.15]}), (3.2, {"pelvis": P})]
    c = SC.enter(rig, skel, h, name=name, g=g, post=POSTURE, settle=settle)
    return c


def exit_(rig, skel, h, name="rpm_sit_couch_exit"):
    g = geometry(h)
    # the couch is solid down to the floor: the feet stay in front of it while rising
    return SC.exit_(rig, skel, h, name=name, g=g, post=POSTURE, feet_min=20.0)


def idle(rig, skel, h, name="rpm_sit_couch_idle", seed=21):
    stand, sit = SC.bases(rig, skel)
    g = geometry(h)
    P = g["pelvis"]
    kx, kz = g["knee_x"], g["knee_z"]
    base = {"pelvis": P, "pelvis_rot": POSTURE["pelvis_rot"], "spine": POSTURE["spine"], "head": POSTURE["head"],
            "l_hand": g["l_hand"], "r_hand": g["r_hand"], "l_foot": g["l_foot"], "r_foot": g["r_foot"],
            "l_elbow": [-0.3, 0.7, -1.0], "r_elbow": [-0.3, -0.7, -1.0], "l_knee": [1.0, 0.25, 0.1], "r_knee": [1.0, -0.25, 0.1],
            "l_fingers": 0.35, "r_fingers": 0.35}
    # arms spread along the backrest, knees apart
    spread = dict(base, **{"pelvis": P + [-0.5, 0, -0.3], "spine": [-15, 0, 0], "head": [6, 0, 0],
                           "l_foot": g["l_foot"] + [1.0, 3.0, 0], "r_foot": g["r_foot"] + [1.0, -3.0, 0],
                           "l_knee": [1.0, 0.6, 0.1], "r_knee": [1.0, -0.6, 0.1],
                           "l_elbow": [0.1, 1.0, -0.4], "r_elbow": [0.1, -1.0, -0.4], "l_fingers": 0.2, "r_fingers": 0.2})
    _, j = PB.probe(rig, stand, sit, {k: v for k, v in spread.items() if not k.endswith("_hand")}, flat_feet=stand)
    sh_l, sh_r = j("l_upperarm"), j("r_upperarm")
    spread["l_hand"] = np.array([sh_l[0] - 7.0, sh_l[1] + 15.0, P[2] - SC.PELVIS_OVER_SEAT + BACK_TOP + 1.0])
    spread["r_hand"] = np.array([sh_r[0] - 7.0, sh_r[1] - 15.0, P[2] - SC.PELVIS_OVER_SEAT + BACK_TOP + 1.0])
    # legs stretched, crossed at the ankles, hands on the belly
    ankles = dict(base, **{"pelvis": P + [1.5, 0, -0.6], "pelvis_rot": [-16, 0, 0], "spine": [-10, 0, 0], "head": [14, 0, 0],
                           "l_foot": np.array([P[0] + 33.0, 1.5, SC.ANKLE_Z + 0.8]), "r_foot": np.array([P[0] + 31.0, -1.5, SC.ANKLE_Z + 3.2]),
                           "l_knee": [1.0, 0.1, 0.4], "r_knee": [1.0, -0.1, 0.4],
                           "l_hand": np.array([P[0] + 7.0, 1.5, P[2] + 9.0]), "r_hand": np.array([P[0] + 7.5, -2.0, P[2] + 10.0]),
                           "l_elbow": [0.0, 1.0, -0.6], "r_elbow": [0.0, -1.0, -0.6], "l_fingers": 0.45, "r_fingers": 0.4})
    # head back on the backrest, dozing off
    doze = dict(base, **{"spine": [-17, 0, 0], "head": [-16, 0, 0], "pelvis": P + [0.8, 0, -0.3], "pelvis_rot": [-13, 0, 0],
                         "l_fingers": 0.5, "r_fingers": 0.5})
    # leaning forward, elbows on the knees
    lean = dict(base, **{"pelvis": P + [2.5, 0, 0], "pelvis_rot": [12, 0, 0], "spine": [34, 0, 0], "head": [-28, 0, 0],
                         "l_foot": g["l_foot"] + [-4.0, 0, 0], "r_foot": g["r_foot"] + [-4.0, 0, 0],
                         "l_elbow": [0.55, 0.15, -0.85], "r_elbow": [0.55, -0.15, -0.85], "l_fingers": 0.5, "r_fingers": 0.5})
    _, j = PB.probe(rig, stand, sit, {k: v for k, v in lean.items() if not k.endswith("_hand")}, flat_feet=stand)
    lk, rk = j("l_calf"), j("r_calf")
    lean["l_hand"] = (lk + rk) / 2 + [5.5, 1.4, 1.5]
    lean["r_hand"] = (lk + rk) / 2 + [6.0, -1.2, 2.2]
    seq = [(base, 0, 6.0), (spread, 1.8, 9.0), (base, 1.6, 4.0), (ankles, 2.2, 10.0), (base, 2.0, 2.0),
           (doze, 2.5, 9.0), (base, 2.0, 3.5), (lean, 2.0, 7.0), (base, 2.0, 3.0)]
    c = keyed(rig, stand, sit, base, seq, name)
    L.breathe(c, seated=True, amount=1.0)
    L.glance(c, n=8, yaw_deg=35, pitch_deg=8, seed=seed, weight=0.7)
    return c


def keyed(rig, stand, sit, first, seq, name, lift=1.4, base=1.0):
    """a looping clip through a list of (pose, move seconds, hold seconds); base - the weight of the second
    base clip (sit) under the poses: 1 - seated bases, 0 - standing; a pose may carry its own "base" channel"""
    chans = list(first.keys())
    keys = {c: [(0.0, first[c])] for c in chans}
    t = 0.0
    for k, (pose, move, hold) in enumerate(seq):
        if k:
            t += move
            for c in chans:
                keys[c].append((t, pose[c], "smoother"))
        t += hold
        for c in chans:
            keys[c].append((t, pose[c], "smoother"))
    tr = {c: PB.Track(*keys[c]) for c in chans}
    if "base" not in tr:
        tr["base"] = PB.Track((0, base), (t, base))
    for f in ("l_foot", "r_foot"):
        if f in tr:
            tr[f] = SC._with_lift(tr[f], lift)
    c = PB.build(rig, stand, sit, tr, t, name=name, loop=True, flat_feet=stand)
    c.loop = True
    return c
