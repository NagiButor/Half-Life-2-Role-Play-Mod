"""Leaning on a wall (a marker of kind "lean"): the marker stands WALL_DIST in front of the wall,
facing away from it. Enter - the hips slide back until the buttocks and the shoulder blades
touch the wall; a long idle - arms hanging, arms crossed, a foot up against the wall, thumbs in
the pockets; exit - a push off with the shoulders. Contact is measured on the body meshes
(BACK_DEPTH: how far the buttocks / shoulder blades stick out behind the pelvis / spine4 bones)."""
import numpy as np

from .. import layers as L
from .. import posebuilder as PB
from . import sit_chair as SC
from .sit_couch import keyed

WALL_DIST = 14.0
WALL_X = -WALL_DIST
# behind the pelvis bone (buttocks) and the spine4 bone (shoulder blades), measured on the meshes (standing)
BACK_DEPTH = {"male": (6.9, 3.4), "female": (5.7, 2.5), "police": (6.9, 3.4), "combine": (12.8, 9.4)}
FOOT_WALL = [72, 0, 0]          # a foot turned toe-down, the sole against the wall behind


def _base(rig, skel):
    stand, sit = SC.bases(rig, skel)
    wp, _ = stand.world()
    j = lambda n: wp[0, rig.index(n)].copy()  # noqa: E731
    g = {k: j(k) for k in ("pelvis", "l_foot", "r_foot", "l_hand", "r_hand", "spine4", "head")}
    return stand, sit, g


def _poses(rig, skel, depth=None):
    """depth: the skeleton whose body leans (its BACK_DEPTH) - police / combine clips are made on the
    citizen's rig, but a soldier's backpack sticks out further"""
    stand, sit, g = _base(rig, skel)
    butt, blades = BACK_DEPTH.get(depth or skel, BACK_DEPTH["male"])
    P0 = g["pelvis"]
    px = WALL_X + 0.8 + butt
    # the legs almost straight (a lean rests on locked knees): hip height from the leg length
    leg = 0.988 * (np.linalg.norm(rig.rest_pos[rig.index("l_calf")]) + np.linalg.norm(rig.rest_pos[rig.index("l_foot")]))
    dx = px - 0.5 * (g["l_foot"][0] + g["r_foot"][0]) - 0.8
    az = 0.5 * (g["l_foot"][2] + g["r_foot"][2])
    P = np.array([px, P0[1], az + np.sqrt(max(1.0, leg ** 2 - dx ** 2))])
    # the trunk leans back until the shoulder blades touch: spine4 must be at WALL_X + blades (+0.2)
    rise = g["spine4"][2] - P0[2]
    want = (WALL_X + blades + 2.4) - px - (g["spine4"][0] - P0[0])
    lean = np.degrees(np.arctan2(-want, rise))          # positive - backwards
    relaxed = {"pelvis": P, "pelvis_rot": [-lean * 0.6, 0, 0], "spine": [-lean * 0.4, 0, 0], "head": [lean * 0.6 - 2, 0, 0],
               "l_foot": g["l_foot"] + [0.5, 0.5, 0], "r_foot": g["r_foot"] + [1.5, -0.5, 0],
               "l_knee": [1.0, 0.2, 0], "r_knee": [1.0, -0.2, 0], "l_foot_rot": [0, 0, 0], "r_foot_rot": [0, 0, 0],
               "l_hand": P + [3.0, 9.0, -9.0], "r_hand": P + [3.0, -9.0, -9.0],
               "l_elbow": [-0.6, 0.6, -0.4], "r_elbow": [-0.6, -0.6, -0.4], "l_fingers": 0.3, "r_fingers": 0.3}
    _, j = PB.probe(rig, stand, sit, {k: v for k, v in relaxed.items() if not k.endswith("_hand")}, flat_feet=stand)
    ch = j("spine4")
    crossed = dict(relaxed, **{"l_hand": ch + [7.5, -5.5, -8.0], "r_hand": ch + [8.5, 5.0, -6.5],
                               "l_elbow": [0.5, 0.8, -0.5], "r_elbow": [0.5, -0.8, -0.5], "l_fingers": 0.3, "r_fingers": 0.25,
                               "head": [lean * 0.6 - 5, 0, 0]})
    # the right foot up against the wall, the weight on the left leg
    foot = dict(crossed, **{"pelvis": P + [0, 1.0, -0.4], "pelvis_rot": [-lean * 0.6, -3, 3],
                            "r_foot": np.array([WALL_X + 5.0, -6.0, 16.0]), "r_foot_rot": FOOT_WALL, "r_knee": [1.0, -0.35, 0.3],
                            "l_foot": g["l_foot"] + [0.0, 1.0, 0]})
    pockets = dict(relaxed, **{"l_hand": P + [3.8, 8.6, -4.0], "r_hand": P + [3.8, -8.6, -4.0],
                               "l_elbow": [-0.5, 0.9, 0.0], "r_elbow": [-0.5, -0.9, 0.0], "l_fingers": 0.6, "r_fingers": 0.6,
                               "l_foot": g["l_foot"] + [3.0, 0.5, 0], "head": [lean * 0.6 + 4, 0, 0]})
    st = {"pelvis": P0, "pelvis_rot": [0, 0, 0], "spine": [0, 0, 0], "head": [0, 0, 0],
          "l_foot": g["l_foot"], "r_foot": g["r_foot"], "l_knee": [1.0, 0.1, 0], "r_knee": [1.0, -0.1, 0],
          "l_foot_rot": [0, 0, 0], "r_foot_rot": [0, 0, 0],
          "l_hand": g["l_hand"], "r_hand": g["r_hand"], "l_elbow": [-0.5, 0.6, -0.6], "r_elbow": [-0.5, -0.6, -0.6],
          "l_fingers": 0.25, "r_fingers": 0.25}
    return stand, sit, g, st, relaxed, crossed, foot, pockets


def enter(rig, skel, name="rpm_lean_wall_enter", depth=None):
    stand, sit, g, st, relaxed, crossed, foot, pockets = _poses(rig, skel, depth)
    # a glance back at the wall, the hips go back, the shoulders settle onto it
    look = dict(st, **{"head": [6, 35, 0], "spine": [2, 8, 0]})
    near = dict(relaxed, **{"pelvis": relaxed["pelvis"] + [1.2, 0, 0.3], "spine": [relaxed["spine"][0] * 0.4, 0, 0],
                            "head": [4, 10, 0]})
    seq = [(st, 0, 0.1), (look, 0.45, 0.1), (near, 0.6, 0.0), (relaxed, 0.45, 0.3)]
    c = keyed(rig, stand, sit, st, seq, name, lift=0.0, base=0.0)
    c.loop = False
    L.breathe(c, amount=0.6)
    return c


def idle(rig, skel, name="rpm_lean_wall_idle", seed=61, depth=None):
    stand, sit, g, st, relaxed, crossed, foot, pockets = _poses(rig, skel, depth)
    seq = [(relaxed, 0, 4.0), (crossed, 1.4, 9.0), (foot, 1.3, 9.0), (crossed, 1.2, 4.0), (relaxed, 1.3, 3.0),
           (pockets, 1.3, 9.0), (relaxed, 1.3, 2.5)]
    c = keyed(rig, stand, sit, relaxed, seq, name, lift=1.6, base=0.0)
    L.breathe(c, amount=1.0)
    L.glance(c, n=8, yaw_deg=50, pitch_deg=8, seed=seed)
    return c


def exit_(rig, skel, name="rpm_lean_wall_exit", depth=None):
    stand, sit, g, st, relaxed, crossed, foot, pockets = _poses(rig, skel, depth)
    # the shoulders push off, the hips come forward over the feet
    push = dict(relaxed, **{"spine": [8, 0, 0], "pelvis_rot": [4, 0, 0], "pelvis": relaxed["pelvis"] + [2.0, 0, 0.4],
                            "head": [-4, 0, 0]})
    over = dict(st, **{"pelvis": st["pelvis"] + [1.2, 0, -0.2], "spine": [5, 0, 0], "head": [-2, 0, 0]})
    seq = [(relaxed, 0, 0.1), (push, 0.35, 0.0), (over, 0.45, 0.05), (st, 0.4, 0.2)]
    c = keyed(rig, stand, sit, relaxed, seq, name, lift=0.0, base=0.0)
    c.loop = False
    L.breathe(c, amount=0.6)
    return c
