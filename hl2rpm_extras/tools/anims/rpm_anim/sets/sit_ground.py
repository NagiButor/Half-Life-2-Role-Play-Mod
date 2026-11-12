"""Sitting on the ground (a marker of kind "ground"): squat down with a hand on the ground, sit;
a long idle - knees up with the arms on them, leaning back on the hands with a leg out,
cross-legged; get up through a squat. The origin is where the character stands; the pelvis
ends PELVIS_BACK behind it on the ground."""
import numpy as np

from .. import layers as L
from .. import posebuilder as PB
from .. import rig as R
from . import sit_chair as SC
from .sit_couch import keyed

PELVIS_BACK = 9.0
NOAIM = [0, 0, 0, 0, 0, -1.0]         # no hand aim (weight 0)
PALM_DOWN_BACK = [-1.0, 0.0, 0.0, 0, 0, -1.0]      # fingers back, palm on the ground
PLANT_R = [-0.37, -0.93, 0.0, 0, 0, -1.0]          # the right hand pushing off the ground beside: fingers out, palm down


def bases(rig, skel):
    from ..library import STOCK
    ss, shared = STOCK[skel]
    stand = R.stock_clip(rig, shared, "idle_subtle").slice(0, 1)
    try:
        ground = R.stock_clip(rig, shared, "Sit_Ground").slice(0, 1)
    except KeyError:
        ground = stand
    return stand, ground


def geometry(rig, skel):
    stand, ground = bases(rig, skel)
    wp, _ = stand.world()
    j = lambda n: wp[0, rig.index(n)].copy()  # noqa: E731
    over = SC.PELVIS_OVER_SEAT * 0.85          # the buttocks flatten on a hard floor
    P = np.array([-PELVIS_BACK, 0.0, over])
    return stand, ground, {"stand_pelvis": j("pelvis"), "l_foot": j("l_foot"), "r_foot": j("r_foot"),
                           "l_hand": j("l_hand"), "r_hand": j("r_hand"), "P": P}


def _poses(rig, skel):
    stand, ground, g = geometry(rig, skel)
    P = g["P"]
    AZ = 0.5 * (g["l_foot"][2] + g["r_foot"][2])     # this skeleton's ankle height standing (heels differ)
    knees = {"pelvis": P, "pelvis_rot": [-6, 0, 0], "spine": [16, 0, 0], "head": [-14, 0, 0],
             "l_foot": P + [17.0, 6.5, AZ - P[2]], "r_foot": P + [16.0, -6.5, AZ - P[2]],
             "l_knee": [0.6, 0.3, 1.0], "r_knee": [0.6, -0.3, 1.0],
             "l_elbow": [-0.2, 0.7, -0.7], "r_elbow": [-0.2, -0.7, -0.7], "l_fingers": 0.45, "r_fingers": 0.5,
             "l_hand_aim": NOAIM, "r_hand_aim": NOAIM, "base": 1.0}
    _, j = PB.probe(rig, stand, ground, {k: v for k, v in knees.items() if not k.endswith("_hand") and not k.endswith("_aim")}, flat_feet=stand)
    lk, rk = j("l_calf"), j("r_calf")
    knees["l_hand"] = lk + [3.0, -1.0, 2.0]          # forearms resting on the knees, hands hanging in front
    knees["r_hand"] = rk + [3.5, 1.0, 1.5]
    back = dict(knees, **{"pelvis": P + [0.5, 0, 0.4], "pelvis_rot": [-16, 0, 0], "spine": [-14, 0, 0], "head": [8, 0, 0],
                          "l_foot": P + [31.0, 4.0, AZ - P[2] + 0.3], "l_knee": [0.3, 0.1, 1.0],
                          "r_foot": P + [17.0, -6.0, AZ - P[2]],
                          "l_hand": P + [-11.0, 9.5, -P[2] + 3.6], "r_hand": P + [-11.0, -9.5, -P[2] + 3.6],
                          "l_elbow": [-0.6, 0.3, 0.2], "r_elbow": [-0.6, -0.3, 0.2], "l_fingers": 0.0, "r_fingers": 0.0,
                          "l_hand_aim": PALM_DOWN_BACK, "r_hand_aim": PALM_DOWN_BACK})
    cross = dict(knees, **{"pelvis": P + [0, 0, 0.2], "pelvis_rot": [2, 0, 0], "spine": [10, 0, 0], "head": [-8, 0, 0],
                           "l_foot": P + [10.0, -4.5, AZ - P[2] + 0.6], "r_foot": P + [12.5, 4.5, AZ - P[2] + 1.0],
                           "l_knee": [0.4, 1.0, 0.25], "r_knee": [0.4, -1.0, 0.25]})
    _, j = PB.probe(rig, stand, ground, {k: v for k, v in cross.items() if not k.endswith("_hand") and not k.endswith("_aim")}, flat_feet=stand)
    cross["l_hand"] = j("l_calf") + [-1.0, -2.0, 3.5]
    cross["r_hand"] = j("r_calf") + [-1.0, 2.0, 3.5]
    return stand, ground, g, knees, back, cross


def enter_keys(rig, skel):
    """(stand, ground, [(pose, move, hold)]) from standing to sitting with the knees up (also the start of
    lying down on the ground, lie_ground.py)"""
    stand, ground, g, knees, back, cross = _poses(rig, skel)
    s0 = g["stand_pelvis"]
    lf, rf = g["l_foot"], g["r_foot"]
    squat = {"pelvis": np.array([-3.0, 0, 17.0]), "pelvis_rot": [18, 0, 0], "spine": [26, 0, 0], "head": [-24, 0, 0],
             "l_foot": lf, "r_foot": rf, "l_knee": [1.0, 0.35, 0.1], "r_knee": [1.0, -0.35, 0.1],
             "l_hand": np.array([8.0, 6.0, 18.0]), "r_hand": np.array([-6.0, -11.0, 3.6]),
             "l_elbow": [-0.2, 0.7, -0.7], "r_elbow": [-0.5, -0.6, 0.0], "l_fingers": 0.3, "r_fingers": 0.0,
             "l_hand_aim": NOAIM, "r_hand_aim": PLANT_R, "base": 0.0}
    st = dict(squat, **{"pelvis": s0, "pelvis_rot": [0, 0, 0], "spine": [0, 0, 0], "head": [0, 0, 0],
                         "l_hand": g["l_hand"], "r_hand": g["r_hand"], "l_knee": [1.0, 0.1, 0], "r_knee": [1.0, -0.1, 0],
                         "l_elbow": [-0.5, 0.6, -0.6], "r_elbow": [-0.5, -0.6, -0.6], "r_hand_aim": NOAIM, "l_fingers": 0.25, "r_fingers": 0.25})
    mid = dict(squat, **{"pelvis": np.array([-1.5, 0, 28.0]), "spine": [16, 0, 0], "head": [-12, 0, 0],
                          "r_hand": np.array([-2.0, -10.0, 14.0]), "r_hand_aim": NOAIM})
    land = dict(knees, **{"pelvis": knees["pelvis"] + [0, 0, -0.4], "spine": [22, 0, 0],
                          "l_foot": lf + [6, 0, 0], "r_foot": rf + [6, 0, 0], "r_hand": squat["r_hand"] + [-2, 0, 0],
                          "r_hand_aim": squat["r_hand_aim"]})
    seq = [(st, 0, 0.1), (mid, 0.6, 0.0), (squat, 0.55, 0.05), (land, 0.55, 0.0), (knees, 0.9, 0.2)]
    return stand, ground, seq


def enter(rig, skel, name="rpm_sit_ground_enter"):
    stand, ground, seq = enter_keys(rig, skel)
    c = keyed(rig, stand, ground, seq[0][0], seq, name, lift=1.0)
    c.loop = False
    L.breathe(c, seated=True, amount=0.6)
    return c


def idle(rig, skel, name="rpm_sit_ground_idle", seed=51):
    stand, ground, g, knees, back, cross = _poses(rig, skel)
    seq = [(knees, 0, 7.0), (back, 2.0, 9.0), (knees, 1.8, 4.0), (cross, 2.4, 10.0), (knees, 2.2, 2.5)]
    c = keyed(rig, stand, ground, knees, seq, name, lift=1.2)
    L.breathe(c, seated=True, amount=1.0)
    L.glance(c, n=7, yaw_deg=40, pitch_deg=10, seed=seed, weight=0.75)
    return c


def exit_keys(rig, skel):
    """(stand, ground, [(pose, move, hold)]) from sitting with the knees up to standing"""
    stand, ground, g, knees, back, cross = _poses(rig, skel)
    s0 = g["stand_pelvis"]
    lf, rf = g["l_foot"], g["r_foot"]
    tuck = dict(knees, **{"l_foot": lf + [3, 0, 0], "r_foot": rf + [3, 0, 0], "spine": [30, 0, 0], "pelvis_rot": [10, 0, 0],
                          "r_hand": knees["pelvis"] + [-4.0, -11.0, -knees["pelvis"][2] + 3.6], "r_fingers": 0.0,
                          "r_hand_aim": PLANT_R, "head": [-18, 0, 0]})
    squat = dict(tuck, **{"pelvis": np.array([-3.0, 0, 17.0]), "pelvis_rot": [20, 0, 0], "spine": [28, 0, 0], "base": 0.0,
                          "l_foot": lf, "r_foot": rf, "l_knee": [1.0, 0.35, 0.1], "r_knee": [1.0, -0.35, 0.1],
                          "l_hand": np.array([9.0, 6.0, 19.0])})
    st = dict(squat, **{"pelvis": s0, "pelvis_rot": [0, 0, 0], "spine": [0, 0, 0], "head": [0, 0, 0],
                         "l_hand": g["l_hand"], "r_hand": g["r_hand"], "l_knee": [1.0, 0.1, 0], "r_knee": [1.0, -0.1, 0],
                         "l_elbow": [-0.5, 0.6, -0.6], "r_elbow": [-0.5, -0.6, -0.6], "r_hand_aim": NOAIM,
                         "l_fingers": 0.25, "r_fingers": 0.25})
    mid = dict(squat, **{"pelvis": np.array([-1.5, 0, 28.0]), "spine": [18, 0, 0], "head": [-12, 0, 0],
                          "r_hand": np.array([-2.0, -10.0, 14.0]), "r_hand_aim": NOAIM, "r_fingers": 0.2})
    seq = [(knees, 0, 0.1), (tuck, 0.7, 0.05), (squat, 0.7, 0.05), (mid, 0.6, 0.0), (st, 0.7, 0.2)]
    return stand, ground, seq


def exit_(rig, skel, name="rpm_sit_ground_exit"):
    stand, ground, seq = exit_keys(rig, skel)
    c = keyed(rig, stand, ground, seq[0][0], seq, name, lift=1.0)
    c.loop = False
    L.breathe(c, amount=0.6)
    return c
