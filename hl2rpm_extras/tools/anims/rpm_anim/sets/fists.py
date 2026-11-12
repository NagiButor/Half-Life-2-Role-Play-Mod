"""Fist fighting for NPCs (weapon_rpm_fists, hl2rpm_fists.cpp): a boxing guard (the angry idle),
jab, cross, hook, uppercut (ACT_MELEE_ATTACK1, weighted) with the blow event 3001
(EVENT_WEAPON_MELEE_HIT) on the frame the fist lands, a hit reaction, and a shove."""
import numpy as np

from .. import layers as L
from .. import posebuilder as PB
from . import sit_chair as SC
from .sit_couch import keyed

HIT = 3001          # EVENT_WEAPON_MELEE_HIT
SWISH = 3003        # EVENT_WEAPON_MELEE_SWISH (sound)


def _stand(rig, skel):
    stand, sit = SC.bases(rig, skel)
    wp, _ = stand.world()
    j = lambda n: wp[0, rig.index(n)].copy()  # noqa: E731
    return stand, sit, {k: j(k) for k in ("pelvis", "l_foot", "r_foot", "head", "spine4", "neck", "l_upperarm", "r_upperarm")}


def guard_pose(g):
    """orthodox stance: left foot forward, knees bent, chin down, fists at the cheeks"""
    P = g["pelvis"]
    return {
        "pelvis": P + [-1.0, 0.0, -2.2], "pelvis_rot": [4, 18, 0], "spine": [8, 6, 0], "head": [8, -22, 0],
        "l_foot": np.array([6.0, 5.5, 4.2]), "r_foot": np.array([-7.0, -5.0, 4.2]),
        "l_knee": [1.0, 0.25, 0.0], "r_knee": [1.0, -0.15, 0.0],
        "l_hand": g["head"] + [8.0, 2.5, -7.5], "r_hand": g["head"] + [6.5, -4.5, -9.0],
        "l_elbow": [0.0, 0.4, -1.0], "r_elbow": [0.0, -0.4, -1.0],
        "l_fingers": 1.0, "r_fingers": 1.0, "l_hand_rot": [-90, 0, 0], "r_hand_rot": [-90, 0, 0],
    }


def _on(p, **kw):
    out = dict(p)
    out.update(kw)
    return out


def stance(rig, skel, name="rpm_fist_stance", seed=41):
    stand, sit, g = _stand(rig, skel)
    gp = guard_pose(g)
    # light bouncing and guard shuffles (loop 2.4 s)
    up = _on(gp, pelvis=gp["pelvis"] + [0, 0, 0.9])
    lean = _on(gp, pelvis=gp["pelvis"] + [0.6, 0.4, -0.2], spine=[10, 8, 0], l_hand=gp["l_hand"] + [1.5, 0, 0.5])
    seq = [(gp, 0, 0.15), (up, 0.3, 0.05), (gp, 0.3, 0.1), (lean, 0.45, 0.1), (up, 0.35, 0.05), (gp, 0.35, 0.2)]
    c = keyed(rig, stand, sit, gp, seq, name, lift=0.0, base=0.0)
    L.breathe(c, amount=1.3, rate_hz=0.8)
    return c


def _strike(rig, skel, name, side, fist, extra, t_hit=0.24, dur=0.75):
    """a punch: wind up, the fist to `fist` (relative to the head) at t_hit, recover to the guard"""
    stand, sit, g = _stand(rig, skel)
    gp = guard_pose(g)
    s = side + "_hand"
    wind = _on(gp, **extra.get("wind", {}))
    hit = _on(gp, **{s: g["head"] + fist}, **extra.get("hit", {}))
    seq = [(gp, 0, 0.0), (wind, t_hit * 0.45, 0.0), (hit, t_hit * 0.55, 0.06), (gp, dur - t_hit - 0.06, 0.0)]
    c = keyed(rig, stand, sit, gp, seq, name, lift=0.0, base=0.0)
    c.loop = False
    c.events = [(int(round((t_hit * 0.4) * c.fps)), SWISH, ""), (int(round(t_hit * c.fps)), HIT, "")]
    return c


def jab(rig, skel, name="rpm_fist_jab"):
    return _strike(rig, skel, name, "l", np.array([24.0, 1.0, -6.0]), {
        "wind": {"spine": [9, 10, 0]},
        "hit": {"spine": [12, 0, 0], "pelvis_rot": [6, 10, 0], "l_elbow": [-0.2, 0.6, -0.6], "head": [10, -14, 0]}},
        t_hit=0.18, dur=0.55)


def cross(rig, skel, name="rpm_fist_cross"):
    return _strike(rig, skel, name, "r", np.array([25.0, 3.0, -6.5]), {
        "wind": {"spine": [8, 14, 0], "pelvis_rot": [4, 22, 0]},
        "hit": {"spine": [16, -14, 0], "pelvis_rot": [8, -8, 0], "r_elbow": [-0.2, -0.6, -0.6],
                "l_hand": guard_pose(_stand(rig, skel)[2])["l_hand"] + [-2, 2, -1], "head": [14, 6, 0]}},
        t_hit=0.26, dur=0.75)


def hook(rig, skel, name="rpm_fist_hook"):
    return _strike(rig, skel, name, "l", np.array([17.0, -6.0, -5.0]), {
        "wind": {"spine": [8, 26, 0], "pelvis_rot": [4, 26, 0], "l_hand": guard_pose(_stand(rig, skel)[2])["l_hand"] + [-2, 6, -2]},
        "hit": {"spine": [10, -20, -6], "pelvis_rot": [6, -6, 0], "l_elbow": [0.1, 1.0, 0.1], "head": [12, -4, 0]}},
        t_hit=0.28, dur=0.8)


def uppercut(rig, skel, name="rpm_fist_uppercut"):
    return _strike(rig, skel, name, "r", np.array([16.0, 1.0, -2.0]), {
        "wind": {"spine": [16, 18, 6], "pelvis": guard_pose(_stand(rig, skel)[2])["pelvis"] + [0, 0, -3.5],
                 "r_hand": _stand(rig, skel)[2]["pelvis"] + [6.0, -6.0, 6.0]},
        "hit": {"spine": [2, -14, 0], "pelvis": guard_pose(_stand(rig, skel)[2])["pelvis"] + [1.0, 0, 0.5],
                "r_elbow": [0.4, -0.4, -1.0], "head": [0, 4, 0]}},
        t_hit=0.3, dur=0.85)


def flinch(rig, skel, name="rpm_fist_flinch"):
    """taking a blow in the guard: head snaps back, a step back"""
    stand, sit, g = _stand(rig, skel)
    gp = guard_pose(g)
    hit = _on(gp, pelvis=gp["pelvis"] + [-2.5, 0.5, -0.5], spine=[-12, -10, 4], head=[-22, 10, 8],
              l_hand=gp["l_hand"] + [-2, 2, 2], r_hand=gp["r_hand"] + [-3, -1, 3])
    seq = [(gp, 0, 0.0), (hit, 0.1, 0.08), (gp, 0.45, 0.0)]
    c = keyed(rig, stand, sit, gp, seq, name, lift=0.0, base=0.0)
    c.loop = False
    return c


def shove(rig, skel, name="rpm_fist_shove"):
    """both hands shove forward (the gentle way to make someone back off)"""
    stand, sit, g = _stand(rig, skel)
    gp = guard_pose(g)
    ch = g["spine4"]
    wind = _on(gp, l_hand=ch + [6, 6, -6], r_hand=ch + [6, -6, -6], spine=[2, 0, 0])
    push = _on(gp, l_hand=ch + [21, 5, -7], r_hand=ch + [21, -5, -7], spine=[16, 0, 0], pelvis_rot=[8, 0, 0],
               l_elbow=[-0.3, 1, -0.6], r_elbow=[-0.3, -1, -0.6], l_fingers=0.2, r_fingers=0.2)
    seq = [(gp, 0, 0.0), (wind, 0.15, 0.0), (push, 0.15, 0.08), (gp, 0.45, 0.0)]
    c = keyed(rig, stand, sit, gp, seq, name, lift=0.0, base=0.0)
    c.loop = False
    c.events = [(int(round(0.3 * c.fps)), HIT, "")]
    return c
