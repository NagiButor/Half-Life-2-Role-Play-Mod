"""Sleeping on the ground (a camp, a bedroll; a marker of kind "sleep" or "ground" without a bed):
sit down on the ground as rpm_sit_ground does, lean back on the elbows, lie down on the back -
the head goes behind the marker, the legs to where the character faced; a long idle (hands on the
belly, behind the head, a knee up, the head turning); sit up and get up the same way back.
Three bases: standing, sitting on the ground (Sit_Ground) and lying (Lying_Down - its head is to -x
already, nothing turns). The space needed: ~40 behind the marker for the head, ~20 in front."""
import numpy as np

from .. import layers as L
from .. import posebuilder as PB
from .. import rig as R
from . import lie_bed as LB
from . import sit_ground as SG
from .sit_couch import keyed

LIE_BACK = 4.0          # the pelvis slides this much back from the sitting spot while lying down
HARD = 0.4              # sinks into the ground (cloth, a bedroll)
BODY_UP = (-1.0, 0.0, 0.0)   # the head is to -x


def _lie_poses(rig, skel):
    stand, ground, g, knees, back, cross = SG._poses(rig, skel)
    from ..library import STOCK
    lie = R.stock_clip(rig, STOCK[skel][0], "Lying_Down").slice(0, 1)
    bases = [ground, lie]
    P = knees["pelvis"]
    Pl = np.array([P[0] - LIE_BACK, 0.0, LB.lie_over(rig, skel, lie) - HARD])
    lie_core = {"base": 2.0, "feet_flat": 0.0, "pelvis": Pl, "pelvis_rot": [0, 0, 0], "spine": [0, 0, 0], "head": [14, 0, 0]}
    _, j = PB.probe(rig, stand, bases, lie_core, flat_feet=stand)
    lie_p = dict(knees, **lie_core)
    lie_p.update({"l_foot": j("l_foot"), "r_foot": j("r_foot"), "l_knee": [0, 0.25, 1.0], "r_knee": [0, -0.25, 1.0],
                  "l_hand": j("l_hand"), "r_hand": j("r_hand"), "l_elbow": [0, 0.6, -0.5], "r_elbow": [0, -0.6, -0.5],
                  "l_fingers": 0.35, "r_fingers": 0.35, "l_hand_aim": SG.NOAIM, "r_hand_aim": SG.NOAIM})
    head = j("head")
    # the bed set's poses turned into this frame: (body-left, toward-the-head) = (+y, -x) here
    belly = dict(lie_p, **{"l_hand": Pl + [-8.5, 2.5, 7.0], "r_hand": Pl + [-6.5, -3.0, 7.0],
                           "l_elbow": [0.2, 0.8, -0.3], "r_elbow": [0.2, -0.8, -0.3], "l_fingers": 0.4, "r_fingers": 0.4})
    behind = dict(lie_p, **{"l_hand": head + [-1.5, 2.5, -2.5], "r_hand": head + [-1.5, -2.5, -2.5],
                            "l_elbow": [-0.4, 1.0, 0.3], "r_elbow": [-0.4, -1.0, 0.3], "head": [20, 0, 0],
                            "l_fingers": 0.5, "r_fingers": 0.5})
    knee = dict(belly, **{"r_foot": j("r_foot") + [-15.0, 0.0, 0.5], "r_knee": [-0.3, -0.15, 1.0]})
    knee_behind = dict(behind, **{"r_foot": knee["r_foot"], "r_knee": knee["r_knee"]})
    # half way down: leaning back on the hands (the checked pose of sitting on the ground), the legs going out
    elbows = dict(back, **{"feet_flat": 0.7, "base": 1.0})
    return stand, bases, knees, elbows, belly, behind, knee, knee_behind


def _with_flat(seq, value=1.0):
    out = []
    for pose, move, hold in seq:
        p = dict(pose)
        p.setdefault("feet_flat", value)
        p.setdefault("base", 1.0)
        out.append((p, move, hold))
    return out


def enter(rig, skel, name="rpm_lie_ground_enter"):
    stand, bases, knees, elbows, belly, behind, knee, knee_behind = _lie_poses(rig, skel)
    _stand, _ground, sit_seq = SG.enter_keys(rig, skel)
    seq = _with_flat(sit_seq) + [(elbows, 1.1, 0.2), (belly, 1.3, 0.4)]
    c = keyed(rig, stand, bases, seq[0][0], seq, name, lift=1.0)
    c.loop = False
    L.breathe(c, seated=True, amount=0.6)
    return c


def idle(rig, skel, name="rpm_lie_ground_idle", seed=91):
    stand, bases, knees, elbows, belly, behind, knee, knee_behind = _lie_poses(rig, skel)
    seq = [(belly, 0, 10.0), (behind, 1.8, 14.0), (knee_behind, 1.4, 9.0), (behind, 1.4, 5.0), (belly, 1.8, 8.0),
           (knee, 1.4, 8.0), (belly, 1.4, 4.0)]
    c = keyed(rig, stand, bases, belly, seq, name, lift=0.0)
    L.breathe(c, seated=True, amount=1.2, rate_hz=0.17)
    L.glance(c, n=5, yaw_deg=35, pitch_deg=6, seed=seed, weight=0.7, up=BODY_UP)
    return c


def exit_(rig, skel, name="rpm_lie_ground_exit"):
    stand, bases, knees, elbows, belly, behind, knee, knee_behind = _lie_poses(rig, skel)
    _stand, _ground, up_seq = SG.exit_keys(rig, skel)
    seq = [(belly, 0, 0.1), (elbows, 1.0, 0.1)] + _with_flat(up_seq)[0:1] + _with_flat(up_seq)[1:]
    seq[2] = (seq[2][0], 1.0, 0.3)      # sitting up to the knees takes a moment
    c = keyed(rig, stand, bases, belly, seq, name, lift=1.0)
    c.loop = False
    L.breathe(c, amount=0.6)
    return c
