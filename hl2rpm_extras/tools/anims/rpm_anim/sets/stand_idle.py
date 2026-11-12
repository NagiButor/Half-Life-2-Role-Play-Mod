"""Standing idles (ACT_IDLE variants for unarmed characters): weight shifts, crossed arms, hands
behind the back, rubbing the neck, checking the wrist. Long loops with their own small story,
breathing and glances on top - every NPC that stands around picks one of them at random."""
import numpy as np

from .. import layers as L
from .. import posebuilder as PB
from . import sit_chair as SC
from .sit_couch import keyed


def _stand(rig, skel):
    stand, sit = SC.bases(rig, skel)
    wp, _ = stand.world()
    j = lambda n: wp[0, rig.index(n)].copy()  # noqa: E731
    g = {k: j(k) for k in ("pelvis", "l_foot", "r_foot", "l_hand", "r_hand", "spine4", "neck", "head", "l_upperarm", "r_upperarm")}
    return stand, sit, g


def _base(g):
    return {"pelvis": g["pelvis"], "pelvis_rot": [0, 0, 0], "spine": [0, 0, 0], "head": [0, 0, 0],
            "l_foot": g["l_foot"], "r_foot": g["r_foot"], "l_hand": g["l_hand"], "r_hand": g["r_hand"],
            "l_elbow": [-0.5, 0.6, -0.6], "r_elbow": [-0.5, -0.6, -0.6], "l_knee": [1.0, 0.1, 0], "r_knee": [1.0, -0.1, 0],
            "l_fingers": 0.25, "r_fingers": 0.25}


def _on(pose, **kw):
    out = dict(pose)
    out.update(kw)
    return out


def _weight(pose, side, g, amount=1.0):
    """weight on one leg: the hips slide over it and drop on the other side, the other knee bends"""
    s = 1.0 if side == "l" else -1.0
    return _on(pose, pelvis=pose["pelvis"] + np.array([0.3, 1.6 * s, -0.5]) * amount,
               pelvis_rot=np.add(pose["pelvis_rot"], [0, -3 * s, 4 * s]) * 1.0,
               spine=np.add(pose["spine"], [0, 2 * s, -5 * s * amount]))


def shift(rig, skel, name="rpm_idle_shift", seed=31):
    stand, sit, g = _stand(rig, skel)
    b = _base(g)
    l = _weight(b, "l", g)
    r = _weight(b, "r", g)
    r2 = _on(r, l_hand=g["l_hand"] + [2.0, -1.0, 2.0], l_fingers=0.4)
    seq = [(b, 0, 2.0), (l, 1.4, 5.0), (b, 1.2, 1.0), (r, 1.4, 4.5), (r2, 0.8, 2.5), (r, 0.8, 1.0), (b, 1.3, 1.5)]
    c = keyed(rig, stand, sit, b, seq, name, lift=0.0, base=0.0)
    L.breathe(c, amount=1.0)
    L.glance(c, n=5, yaw_deg=45, pitch_deg=8, seed=seed)
    return c


def arms(rig, skel, name="rpm_idle_arms", seed=32):
    stand, sit, g = _stand(rig, skel)
    b = _base(g)
    ch = g["spine4"]
    crossed = _on(_weight(b, "r", g, 0.8), l_hand=ch + [7.5, -5.5, -8.0], r_hand=ch + [8.5, 5.0, -6.5],
                  l_elbow=[0.5, 0.8, -0.5], r_elbow=[0.5, -0.8, -0.5], l_fingers=0.3, r_fingers=0.25, spine=[-3, 0, -3])
    crossed2 = _on(_weight(crossed, "l", g, 0.9), spine=[-2, 0, 3], head=[4, -10, 0])
    seq = [(b, 0, 0.8), (crossed, 1.2, 9.0), (crossed2, 1.6, 9.0), (crossed, 1.6, 4.0), (b, 1.3, 0.8)]
    c = keyed(rig, stand, sit, b, seq, name, lift=0.0, base=0.0)
    L.breathe(c, amount=1.0)
    L.glance(c, n=6, yaw_deg=40, pitch_deg=8, seed=seed)
    return c


def back(rig, skel, name="rpm_idle_back", seed=33):
    stand, sit, g = _stand(rig, skel)
    b = _base(g)
    P = g["pelvis"]
    hands = _on(b, l_hand=P + [-7.5, 2.0, 3.0], r_hand=P + [-7.5, -1.5, 3.5],
                l_elbow=[-0.6, 0.8, -0.3], r_elbow=[-0.6, -0.8, -0.3], spine=[-3, 0, 0], head=[-3, 0, 0],
                l_fingers=0.45, r_fingers=0.45)
    toes = _on(hands, pelvis=P + [1.0, 0, 0.6], spine=[-1, 0, 0])
    heels = _on(hands, pelvis=P + [-0.6, 0, -0.1], spine=[-5, 0, 0])
    seq = [(b, 0, 0.6), (hands, 1.3, 3.0), (toes, 1.0, 0.6), (heels, 1.2, 0.8), (hands, 1.0, 5.0),
           (_weight(hands, "l", g, 0.7), 1.5, 6.0), (hands, 1.3, 2.0), (b, 1.2, 0.6)]
    c = keyed(rig, stand, sit, b, seq, name, lift=0.0, base=0.0)
    L.breathe(c, amount=1.0)
    L.glance(c, n=5, yaw_deg=55, pitch_deg=6, seed=seed)
    return c


def neck(rig, skel, name="rpm_idle_neck", seed=34):
    stand, sit, g = _stand(rig, skel)
    b = _base(g)
    nk = g["neck"]
    rub = _on(b, r_hand=nk + [-4.0, -1.0, -0.5], r_elbow=[0.3, -0.5, 0.9], head=[12, 8, 6], spine=[4, 0, 0], r_fingers=0.15)
    rub2 = _on(rub, r_hand=nk + [-4.2, 1.5, 0.5], head=[16, -5, -4])
    hips = _on(_weight(b, "l", g, 0.6), l_hand=g["pelvis"] + [0.5, 9.5, 4.0], r_hand=g["pelvis"] + [0.5, -9.5, 4.0],
               l_elbow=[-0.2, 1.0, 0.0], r_elbow=[-0.2, -1.0, 0.0], spine=[-6, 0, 0], head=[-8, 0, 0],
               l_fingers=0.35, r_fingers=0.35)
    shrug = _on(b, l_clav=[10, 4], r_clav=[10, 4])
    b2 = _on(b, l_clav=[0, 0], r_clav=[0, 0])
    b = _on(b, l_clav=[0, 0], r_clav=[0, 0])
    rub = _on(rub, l_clav=[0, 0], r_clav=[0, 4])
    rub2 = _on(rub2, l_clav=[0, 0], r_clav=[0, 4])
    hips = _on(hips, l_clav=[0, 0], r_clav=[0, 0])
    seq = [(b, 0, 3.0), (rub, 1.0, 0.6), (rub2, 0.6, 0.4), (rub, 0.6, 0.4), (rub2, 0.6, 0.6), (b2, 1.0, 1.5),
           (shrug, 0.6, 0.4), (b2, 0.8, 3.0), (hips, 1.3, 7.0), (b2, 1.3, 2.5)]
    c = keyed(rig, stand, sit, b, seq, name, lift=0.0, base=0.0)
    L.breathe(c, amount=1.2)
    L.glance(c, n=3, yaw_deg=35, pitch_deg=6, seed=seed, weight=0.6)
    return c


def wait(rig, skel, name="rpm_idle_wait", seed=35):
    stand, sit, g = _stand(rig, skel)
    b = _base(g)
    ch = g["spine4"]
    watch = _on(b, l_hand=ch + [12.0, 1.5, -7.0], l_elbow=[0.1, 1.0, -0.6], head=[24, -12, 0], spine=[6, 0, 0],
                r_hand=ch + [10.0, -2.0, -9.5], r_elbow=[0.1, -1.0, -0.6], l_fingers=0.2, r_fingers=0.35)
    tap_up = _on(_weight(b, "l", g, 0.8), r_foot=g["r_foot"] + [0.5, 0, 1.6])
    tap_dn = _weight(b, "l", g, 0.8)
    seq = [(b, 0, 3.0), (watch, 1.1, 2.0), (b, 1.0, 3.0)]
    for _ in range(4):
        seq += [(tap_up, 0.25, 0.05), (tap_dn, 0.25, 0.1)]
    seq += [(b, 1.0, 4.0), (watch, 1.1, 1.6), (b, 1.0, 2.0)]
    c = keyed(rig, stand, sit, b, seq, name, lift=0.0, base=0.0)
    L.breathe(c, amount=1.0)
    L.glance(c, n=4, yaw_deg=50, pitch_deg=6, seed=seed, weight=0.8)
    return c


ALL = [("rpm_idle_shift", shift), ("rpm_idle_arms", arms), ("rpm_idle_back", back), ("rpm_idle_neck", neck),
       ("rpm_idle_wait", wait)]
