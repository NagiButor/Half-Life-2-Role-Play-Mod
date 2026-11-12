"""Eating on a chair (a marker of kind "eat"): the same sitting down and getting up as the chair
(rpm_sit_chair_enter / _exit - the actbusy entry uses them), a long idle in between: the left hand
holds a bowl in front of the belly, the right one scoops and brings the spoon to the mouth, the
head meets it, chewing pauses, a drink from a can, the bowl rested on the thigh. No props - the
hands mime them, the way HL2 citizens hold nothing. Blended by the seat height like the chair."""
import numpy as np

from .. import layers as L
from .. import posebuilder as PB
from . import sit_chair as SC

SEAT_HEIGHTS = SC.SEAT_HEIGHTS
PELVIS_BACK = SC.PELVIS_BACK
NOAIM = [0, 0, 0, 0, 0, -1.0]


def _poses(rig, skel, h):
    stand, sit = SC.bases(rig, skel)
    g = SC.seat_geometry(h)
    P = g["pelvis"]
    up = {"pelvis": P, "pelvis_rot": [0, 0, 0], "spine": [SC.SEAT_SPINE, 0, 0], "head": [SC.SEAT_HEAD, 0, 0],
          "l_hand": g["l_hand"], "r_hand": g["r_hand"], "l_foot": g["l_foot"], "r_foot": g["r_foot"],
          "l_elbow": [-0.3, 0.6, -1.0], "r_elbow": [-0.3, -0.6, -1.0], "r_knee": [1.0, -0.18, 0.1],
          "l_fingers": 0.3, "r_fingers": 0.3, "l_hand_aim": NOAIM, "r_hand_aim": NOAIM}
    # holding the bowl: a little forward, looking down into it
    bowl = P + np.array([10.5, 2.5, 11.0])
    hold = dict(up, **{"pelvis": P + [0.6, 0, 0], "pelvis_rot": [6, 0, 0], "spine": [10, 0, 0], "head": [22, 0, 0],
                       "l_hand": bowl + [-1.0, 3.0, -2.0], "l_elbow": [-0.2, 1.0, -0.6], "l_fingers": 0.45,
                       "l_hand_aim": [0.55, -0.8, 0.0, 0, 0, 1.0],              # fingers under the bowl, palm up
                       "r_hand": bowl + [1.5, -4.0, 2.5], "r_elbow": [-0.2, -1.0, -0.5], "r_fingers": 0.65,
                       "r_hand_aim": [0.5, 0.75, -0.45, 0, 0.3, -1.0]})       # the spoon dipping into the bowl
    scoop = dict(hold, **{"r_hand": bowl + [0.5, -2.0, 2.0], "head": [24, 0, 0]})
    # the spoon to the mouth: probe where the head is with the posture of eating
    lift_core = dict(hold, **{"spine": [8, 0, 0], "head": [12, 0, 0], "pelvis_rot": [5, 0, 0]})
    _, j = PB.probe(rig, stand, sit, {k: v for k, v in lift_core.items() if not k.endswith("_hand") and not k.endswith("_aim")},
                    flat_feet=stand)
    head = j("head")       # Head1: the top of the neck - the lips are a little in front of it and lower
    # the wrist ~5 under and in front of the lips (the spoon goes up from the fist to the mouth)
    lift = dict(lift_core, **{"r_hand": head + [7.0, -3.0, -6.5], "r_elbow": [-0.3, -1.0, -0.7],
                              "r_hand_aim": [-0.2, 0.6, 0.75, -0.8, 0.0, 0.3], "r_fingers": 0.7,
                              "l_hand": hold["l_hand"] + [1.5, 0, 2.0]})     # the bowl comes up a little too
    chew = dict(hold, **{"head": [16, 0, 0], "spine": [9, 0, 0], "r_hand": bowl + [2.5, -5.0, 3.5]})
    # a drink: the right hand (a can) to the mouth, the head back
    drink_core = dict(hold, **{"spine": [2, 0, 0], "head": [-16, 0, 0], "pelvis_rot": [0, 0, 0]})
    _, j = PB.probe(rig, stand, sit, {k: v for k, v in drink_core.items() if not k.endswith("_hand") and not k.endswith("_aim")},
                    flat_feet=stand)
    drink = dict(drink_core, **{"r_hand": j("head") + [5.5, -2.5, -4.5], "r_elbow": [0.0, -1.0, -0.6], "r_fingers": 0.65,
                                "r_hand_aim": [0.0, 1.0, 0.1, -1.0, 0.0, 0.0]})
    # the bowl resting on the left thigh, the right hand on the right thigh
    rest = dict(up, **{"l_hand": g["l_hand"] + [1.0, 0.5, 1.5], "l_fingers": 0.45, "l_hand_aim": [0.55, -0.8, 0.0, 0, 0, 1.0],
                       "head": [8, 0, 0]})
    return stand, sit, up, hold, scoop, lift, chew, drink, rest


def idle(rig, skel, h, name="rpm_eat_chair_idle", seed=81):
    stand, sit, up, hold, scoop, lift, chew, drink, rest = _poses(rig, skel, h)
    bite = [(scoop, 0.7, 0.2), (lift, 0.65, 0.35), (chew, 0.55, 1.6)]
    seq = [(up, 0, 2.5), (hold, 1.4, 0.8)] + bite + bite + [(chew, 0.4, 1.5)] + bite + \
          [(drink, 1.1, 1.6), (chew, 1.0, 2.0)] + bite + bite + [(rest, 1.3, 4.0), (hold, 1.2, 0.5)] + bite + [(up, 1.6, 1.5)]
    chans = list(up.keys())
    keys = {c: [(0.0, up[c])] for c in chans}
    t = 0.0
    for k, (pose, move, hold_t) in enumerate(seq):
        if k:
            t += move
            for c in chans:
                keys[c].append((t, pose[c], "smoother"))
        t += hold_t
        for c in chans:
            keys[c].append((t, pose[c], "smoother"))
    tr = {c: PB.Track(*keys[c]) for c in chans}
    tr["base"] = PB.Track((0, 1.0), (t, 1.0))
    c = PB.build(rig, stand, sit, tr, t, name=name, loop=True, flat_feet=stand)
    c.loop = True
    L.breathe(c, seated=True, amount=1.0)
    L.glance(c, n=4, yaw_deg=25, pitch_deg=6, seed=seed, weight=0.5)
    return c


def enter(rig, skel, h, name="rpm_eat_chair_enter"):
    return SC.enter(rig, skel, h, name=name)


def exit_(rig, skel, h, name="rpm_eat_chair_exit"):
    return SC.exit_(rig, skel, h, name=name)


def geometry(h):
    return SC.seat_geometry(h)
