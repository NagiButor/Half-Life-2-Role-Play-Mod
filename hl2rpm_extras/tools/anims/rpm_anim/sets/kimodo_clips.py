"""Library sequences made of Kimodo motions (rpm_anim/kimodo.py; the clips are generated offline by
kimodo.cpp into E:\\SourceModding\\HL2RPMSOURCE\\kimodo.cpp\\hl2rpm_out - see HL2RPM_NOTES.md there):

  rpm_idle_bored      a standing idle: looks around, scratches the back of the head (ACT_IDLE variant)
  rpm_talk_idle       talking with the hands, palms up and out (an actbusy for "work" / "idle" markers:
                      two people chatting at two markers that face each other)
  rpm_search_*        squat down, look over something in the hands, get up (enter / loop / exit)

The motion is retargeted onto the rig it is built for (every skeleton gets its own retarget), fingers
relaxed, loops blended end to start."""
import os

import numpy as np

from .. import kimodo as K
from .. import layers as L
from .. import rig as R

KIMODO_OUT = r"E:\SourceModding\HL2RPMSOURCE\kimodo.cpp\hl2rpm_out"
_CACHE = {}


def _clip(rig, name, tag):
    key = (id(rig), name)
    if key not in _CACHE:
        path = os.path.join(KIMODO_OUT, name + ".npz")
        if not os.path.exists(path):
            path = os.path.join(KIMODO_OUT, "variants", name + ".npz")
        _CACHE[key] = K.retarget(K.load(path), rig, tag)
    return _CACHE[key].copy(tag)


def _finish(c, loop):
    L.fingers_relax(c, curl=0.35, variation=0.08, seed=7)
    if loop:
        c = c.make_loop(blend_frames=24)
    else:
        c.loop = False
    return c


def idle_bored(rig, skel, name="rpm_idle_bored"):
    c = _clip(rig, "idle_bored_s1", name)
    return _finish(c, True)


def talk_idle(rig, skel, name="rpm_talk_idle"):
    # the clip starts and ends with the arms down: the part with the hands up, looped
    c = _clip(rig, "talk_gesture_s1", name).slice(12, 170)
    return _finish(c, True)


# the squat: 0..SQUAT_IN stand -> down, SQUAT_IN..SQUAT_OUT down (looped), SQUAT_OUT.. up
SQUAT_IN, SQUAT_OUT = 30, 140


def search_enter(rig, skel, name="rpm_search_enter"):
    c = _clip(rig, "crouch_search_box_s1", name).slice(0, SQUAT_IN + 1)
    return _finish(c, False)


def search_idle(rig, skel, name="rpm_search_idle"):
    c = _clip(rig, "crouch_search_box_s1", name).slice(SQUAT_IN, SQUAT_OUT + 1)
    return _finish(c, True)


def search_exit(rig, skel, name="rpm_search_exit"):
    full = _clip(rig, "crouch_search_box_s1", name)
    # the loop ends on its first frame (SQUAT_IN): start the getting up from there
    start = R.hold(full.slice(SQUAT_IN, SQUAT_IN + 1), 0.2, name=name)
    c = start.then(full.slice(SQUAT_OUT), blend_frames=10)
    c.name = name
    return _finish(c, False)
