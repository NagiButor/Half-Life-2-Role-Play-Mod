"""Lying on a bed (a marker of kind "sleep"): the marker stands at the middle of the bed's long side,
facing away from the bed; the head goes to the marker's left (+y). Enter - sit down on the edge,
lean onto the left elbow, swing the legs up and lie back; a long idle - on the back with the hands
on the belly, the hands behind the head, a knee up, a turn of the head; exit - sit up, the legs
down, stand up.

Three base frames: standing (idle_subtle), sitting (Plaza_Sit02) and lying (Lying_Down, flat on
the back, head to -x); the transition slides from one to the next while the pelvis turns by 90
degrees, so the body pivots on the buttocks as people do. The mattress height h comes from the
marker (pose parameter rpm_seat_h, BED_HEIGHTS)."""
import numpy as np

from .. import layers as L
from .. import posebuilder as PB
from .. import rig as R
from . import sit_chair as SC
from .sit_couch import keyed

BED_HEIGHTS = (12.0, 18.0, 24.0)
SEAT_HEIGHTS = BED_HEIGHTS     # the library blends the sequences by rpm_seat_h over these
SIT_BACK = 22.0         # origin -> pelvis sitting on the edge
LIE_X = -32.0           # the body's line on the bed (middle of a 38 wide mattress whose edge is ~13 behind the origin)
EDGE = -13.0            # the bed's long edge (the mattress spans EDGE .. EDGE - 38)
LIE_Y = 3.0             # the pelvis along the bed (the body is centered on the marker)
SINK = 1.2              # into a mattress
LIE_YAW = -90.0         # the lying base has its head to -x; -90 turns it to +y
BODY_UP = (0.0, 1.0, 0.0)
PALM_DOWN = [0.0, 0.0, 0.0, 0, 0, -1.0]


def bases(rig, skel):
    from ..library import STOCK
    ss, shared = STOCK[skel]
    stand, sit = SC.bases(rig, skel)
    lie = R.stock_clip(rig, ss, "Lying_Down").slice(0, 1)
    return stand, sit, lie


_LIE_OVER = {}


def _measure(rig, skel, lie):
    """(the pelvis bone above the lowest point of the lying body, the body's middle relative to the pelvis
    toward the head) - measured on the mesh once per skeleton"""
    if skel not in _LIE_OVER:
        try:
            from .. import body as BD
            from .. import build as B
            b = BD.Body(B.SKELETONS[skel if skel in ("male", "female") else "male"]["rig"], rig)
            wp, wq = lie.world()
            v = b.skin(wp[0], wq[0])
            pel = wp[0, rig.index("pelvis")]
            # the lying base has the head to -x
            mid = 0.5 * (v[:, 0].min() + v[:, 0].max())
            _LIE_OVER[skel] = (float(pel[2] - v[:, 2].min()), float(pel[0] - mid))
        except Exception:
            _LIE_OVER[skel] = (8.1, -3.4)
    return _LIE_OVER[skel]


def lie_over(rig, skel, lie):
    return _measure(rig, skel, lie)[0]


def lie_center(rig, skel):
    """where the middle of the lying body is, relative to the pelvis, along the body toward the head"""
    _stand, _sit, lie = bases(rig, skel)
    return _measure(rig, skel, lie)[1]


def _poses(rig, skel, h):
    stand, sit, lie = bases(rig, skel)
    s = SC.stand_geometry(stand)
    g = SC.seat_geometry(h, SIT_BACK, None, 2.5)
    az = 0.5 * (s["l_foot"][2] + s["r_foot"][2])
    st = {"base": 0.0, "feet_flat": 1.0, "pelvis": s["pelvis"], "pelvis_rot": [0, 0, 0], "spine": [0, 0, 0], "head": [0, 0, 0],
          "l_foot": s["l_foot"], "r_foot": s["r_foot"], "l_knee": [1.0, 0.1, 0], "r_knee": [1.0, -0.1, 0],
          "l_hand": s["l_hand"], "r_hand": s["r_hand"], "l_elbow": [-0.5, 0.6, -0.6], "r_elbow": [-0.5, -0.6, -0.6],
          "l_hand_aim": [0, 0, 0, 0, 0, -1.0], "r_hand_aim": [0, 0, 0, 0, 0, -1.0], "l_fingers": 0.25, "r_fingers": 0.25}
    P = g["pelvis"]
    # a high bed: the knees no lower than the mattress (the thighs would press into its edge), the shins hang
    if g["knee_z"] < h - 1.0:
        kz = h - 1.0
        reach = np.sqrt(max(1.0, SC.THIGH ** 2 - (kz - P[2]) ** 2))
        kx = P[0] + reach
        fz = max(az, kz - np.sqrt(SC.SHIN ** 2 - 4.0))
        g["l_foot"] = np.array([kx - 2.0, 5.2, fz])
        g["r_foot"] = np.array([kx - 3.0, -5.2, fz])
        g["knee_x"], g["knee_z"] = kx, kz
    else:
        g["l_foot"] = g["l_foot"].copy()
        g["r_foot"] = g["r_foot"].copy()
        g["l_foot"][2] = g["r_foot"][2] = az
    sit_p = dict(st, **{"base": 1.0, "pelvis": P, "spine": [6, 0, 0], "head": [-3, 0, 0],
                        # a bed is solid to the floor: the feet stay in front of its edge (a high bed - they dangle there)
                        "l_foot": np.array([max(g["l_foot"][0], EDGE + 5.0), 5.2, g["l_foot"][2]]),
                        "r_foot": np.array([max(g["r_foot"][0], EDGE + 4.0), -5.2, g["r_foot"][2]]),
                        "l_knee": [1.0, 0.18, 0.1], "r_knee": [1.0, -0.18, 0.1],
                        "l_hand": P + [-1.0, 11.5, -SC.PELVIS_OVER_SEAT + 5.0], "r_hand": P + [-1.0, -11.5, -SC.PELVIS_OVER_SEAT + 5.0],
                        "l_elbow": [-0.4, 0.8, -0.3], "r_elbow": [-0.4, -0.8, -0.3],
                        "l_hand_aim": [0.3, 0.6, 0.0, 0, 0, -1.0], "r_hand_aim": [0.3, -0.6, 0.0, 0, 0, -1.0],
                        "l_fingers": 0.05, "r_fingers": 0.05})
    # lying: the pelvis on the bed's line, the base's own feet and hands
    Pl = np.array([LIE_X, LIE_Y, h + lie_over(rig, skel, lie) - SINK])
    lie_core = {"base": 2.0, "feet_flat": 0.0, "pelvis": Pl, "pelvis_rot": [0, LIE_YAW, 0], "spine": [0, 0, 0], "head": [16, 0, 0]}
    _, j = PB.probe(rig, stand, [sit, lie], lie_core, flat_feet=stand)
    lie_p = dict(st, **lie_core)
    lie_p.update({"l_foot": j("l_foot"), "r_foot": j("r_foot"), "l_knee": [0.25, 0, 1.0], "r_knee": [-0.25, 0, 1.0],
                  "l_hand": j("l_hand"), "r_hand": j("r_hand"), "l_elbow": [0.6, 0, -0.5], "r_elbow": [-0.6, 0, -0.5],
                  "l_fingers": 0.35, "r_fingers": 0.35})
    head = j("head")
    # hands on the belly
    belly = dict(lie_p, **{"l_hand": Pl + [2.5, 8.5, 7.0], "r_hand": Pl + [-3.0, 6.5, 7.0],
                           "l_elbow": [0.8, -0.2, -0.3], "r_elbow": [-0.8, -0.2, -0.3], "l_fingers": 0.4, "r_fingers": 0.4})
    # hands behind the head, elbows out
    behind = dict(lie_p, **{"l_hand": head + [2.5, 1.5, -2.5], "r_hand": head + [-2.5, 1.5, -2.5],
                            "l_elbow": [1.0, 0.4, 0.3], "r_elbow": [-1.0, 0.4, 0.3], "head": [22, 0, 0],
                            "l_fingers": 0.5, "r_fingers": 0.5})
    # the right knee up (the foot slides toward the hips on the mattress)
    knee = dict(belly, **{"r_foot": j("r_foot") + [0, 15.0, 0.5], "r_knee": [-0.15, 0.3, 1.0]})
    knee_behind = dict(behind, **{"r_foot": knee["r_foot"], "r_knee": knee["r_knee"]})
    return stand, sit, lie, st, sit_p, lie_p, belly, behind, knee, knee_behind


def _turn(sit_p, lie_p, h, frac):
    """half way from sitting on the edge to lying: on the left elbow, the legs coming up"""
    P = sit_p["pelvis"] + (lie_p["pelvis"] - sit_p["pelvis"]) * frac
    P = P + [0, 0, 1.2]
    t = dict(sit_p, **{"base": 1.0 + frac, "feet_flat": 1.0 - frac, "pelvis": P, "pelvis_rot": [-6, LIE_YAW * frac, 0],
                       "spine": [-4, 0, 0], "head": [6, 0, 0],
                       "l_hand": np.array([P[0] - 6.0, P[1] + 13.0, h + 4.6]), "l_elbow": [0.2, 0.3, -1.0],
                       "r_hand": np.array([P[0] + 3.0, P[1] - 6.0, h + 4.8]), "r_elbow": [0.0, -0.6, -0.8],
                       "l_foot": np.array([max(P[0] + 14.0, EDGE + 3.5), -12.0, h + 9.0]),
                       "r_foot": np.array([max(P[0] + 13.0, EDGE + 2.5), -18.0, h + 8.5]),
                       "l_knee": [0.6, 0.0, 0.8], "r_knee": [0.5, -0.1, 0.8],
                       "l_hand_aim": [0.0, 0.0, 0.0, 0, 0, -1.0], "r_hand_aim": [0.0, -0.8, 0.0, 0, 0, -1.0]})
    return t


def enter(rig, skel, h, name="rpm_lie_bed_enter"):
    stand, sit, lie, st, sit_p, lie_p, belly, behind, knee, knee_behind = _poses(rig, skel, h)
    turn = _turn(sit_p, lie_p, h, 0.45)
    # backing up to the edge: the hips over it first (a high bed - almost a hop up), then they slide back onto it
    down = dict(sit_p, **{"pelvis": np.array([EDGE - 2.0, 0.0, h + SC.PELVIS_OVER_SEAT + 5.0]), "base": 0.6, "spine": [26, 0, 0], "head": [-10, 0, 0],
                          "pelvis_rot": [12, 0, 0], "l_knee": [1.0, 0.2, 0], "r_knee": [1.0, -0.2, 0],
                          "l_foot": np.array([EDGE + 12.0, 5.2, st["l_foot"][2]]), "r_foot": np.array([EDGE + 11.0, -5.2, st["r_foot"][2]])})
    seq = [(st, 0, 0.1), (down, 0.9, 0.0), (sit_p, 0.7, 0.4), (turn, 1.0, 0.1), (belly, 1.3, 0.4)]
    c = keyed(rig, stand, [sit, lie], st, seq, name, lift=0.0)
    c.loop = False
    L.breathe(c, seated=True, amount=0.6)
    return c


def idle(rig, skel, h, name="rpm_lie_bed_idle", seed=71):
    stand, sit, lie, st, sit_p, lie_p, belly, behind, knee, knee_behind = _poses(rig, skel, h)
    seq = [(belly, 0, 9.0), (behind, 1.8, 12.0), (knee_behind, 1.4, 9.0), (behind, 1.4, 4.0), (belly, 1.8, 6.0),
           (knee, 1.4, 8.0), (belly, 1.4, 4.0)]
    c = keyed(rig, stand, [sit, lie], belly, seq, name, lift=0.0)
    L.breathe(c, seated=True, amount=1.2, rate_hz=0.18)
    L.glance(c, n=6, yaw_deg=35, pitch_deg=6, seed=seed, weight=0.8, up=BODY_UP)
    return c


def exit_(rig, skel, h, name="rpm_lie_bed_exit"):
    stand, sit, lie, st, sit_p, lie_p, belly, behind, knee, knee_behind = _poses(rig, skel, h)
    turn = _turn(sit_p, lie_p, h, 0.55)
    up = dict(sit_p, **{"pelvis": np.array([EDGE - 3.0, 0.0, h + SC.PELVIS_OVER_SEAT + 0.5]), "spine": [18, 0, 0], "head": [-8, 0, 0],
                        "l_foot": np.array([EDGE + 8.0, 5.2, st["l_foot"][2]]), "r_foot": np.array([EDGE + 9.0, -5.2, st["r_foot"][2]]),
                        "l_hand": sit_p["l_hand"] + [3, 0, 0], "r_hand": sit_p["r_hand"] + [3, 0, 0]})
    rise = dict(st, **{"pelvis": st["pelvis"] + [-6.0, 0, -9.0], "base": 0.4, "spine": [22, 0, 0], "head": [-8, 0, 0],
                       "pelvis_rot": [10, 0, 0]})
    seq = [(belly, 0, 0.1), (turn, 1.2, 0.1), (sit_p, 1.0, 0.2), (up, 0.5, 0.0), (rise, 0.6, 0.0), (st, 0.6, 0.2)]
    c = keyed(rig, stand, [sit, lie], belly, seq, name, lift=0.0)
    c.loop = False
    L.breathe(c, amount=0.6)
    return c
