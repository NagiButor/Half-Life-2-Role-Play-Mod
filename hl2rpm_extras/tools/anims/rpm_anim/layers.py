"""Procedural layers that bring still poses to life: breathing, weight shifts, glances, fidgets.
Every layer is loop-safe when the clip loops (periodic over the clip length)."""
import numpy as np

from . import ik
from . import qmath as Q

UP = np.array([0, 0, 1.0])
FWD = np.array([1.0, 0, 0])
LEFT = np.array([0, 1.0, 0])


def periodic_noise(T, cycles, rng, octaves=3):
    """smooth noise in [-1, 1] that repeats every T frames (sum of harmonics with random phases)"""
    t = np.arange(T) / T * 2 * np.pi
    out = np.zeros(T)
    amp = 1.0
    norm = 0.0
    for o in range(octaves):
        k = max(1, int(round(cycles * (2 ** o))))
        out += amp * np.sin(k * t + rng.uniform(0, 2 * np.pi))
        norm += amp
        amp *= 0.5
    return out / norm


def holds(T, n_targets, rng, hold=(0.5, 0.8), lo=-1.0, hi=1.0, loop=True):
    """a value that moves between random targets and holds them (glances, shifts); periodic if loop"""
    keys = np.sort(rng.choice(np.arange(1, T - 1), size=max(1, n_targets), replace=False)) if T > 3 else np.array([0])
    vals = rng.uniform(lo, hi, size=len(keys))
    if loop:
        vals[-1] = 0.0
    out = np.zeros(T)
    prev_v, prev_t = 0.0, 0
    for k, v in list(zip(keys, vals)) + [(T - 1, 0.0)]:
        span = max(1, k - prev_t)
        move = max(1, int(span * (1 - rng.uniform(*hold))))
        seg = np.arange(prev_t, k + 1)
        s = np.clip((seg - prev_t) / move, 0, 1)
        s = s * s * s * (s * (s * 6 - 15) + 10)
        out[seg] = prev_v + (v - prev_v) * s
        prev_v, prev_t = v, k
    return out


def breathe(clip, rate_hz=0.22, amount=1.0, seated=False):
    """chest rises and falls; shoulders follow a little"""
    T = clip.frames
    if clip.loop:
        cycles = max(1, round(clip.duration * rate_hz))
        ph = np.arange(T) / (T - 1) * cycles * 2 * np.pi
    else:
        ph = np.arange(T) / clip.fps * rate_hz * 2 * np.pi
    b = (np.sin(ph) * 0.5 + 0.5) ** 1.5      # quicker inhale, slower exhale
    a = np.radians(1.6) * amount * b
    _wp, wq = clip.world()
    # chest opens: spine2/spine4 tip back a hair, clavicles lift
    for bone, k in (("spine2", 0.5), ("spine4", 0.7)):
        ik.rotate_world(clip, bone, _side_axis(clip, bone), -a * k)
    for side, sgn in (("l", 1), ("r", -1)):
        ik.rotate_world(clip, side + "_clavicle", _forward_axis(clip), sgn * a * 0.8)
    if not seated:
        clip.pos[:, 0, 2] += 0.08 * amount * b
    return clip


def sway(clip, amount=1.0, seed=1):
    """standing weight shift: the pelvis drifts side to side and the upper body balances it"""
    rng = np.random.default_rng(seed)
    T = clip.frames
    n = periodic_noise(T, max(1, clip.duration / 7.0), rng, 2)
    clip.pos[:, 0, 1] += n * 0.6 * amount
    ik.rotate_world(clip, "pelvis", FWD, np.radians(1.2) * n * amount)
    ik.rotate_world(clip, "spine2", FWD, -np.radians(1.4) * n * amount)
    return clip


def glance(clip, n=3, yaw_deg=35, pitch_deg=10, seed=2, weight=1.0, up=None):
    """the head looks around: holds at random points (periodic when the clip loops); up - the body's up
    axis in model space (lying along the bed: (0, 1, 0)), the world up by default"""
    rng = np.random.default_rng(seed)
    T = clip.frames
    yaw = holds(T, n, rng, loop=clip.loop) * np.radians(yaw_deg) * weight
    pitch = holds(T, n, rng, loop=clip.loop) * np.radians(pitch_deg) * weight
    for bone, share in (("spine4", 0.15), ("neck", 0.35), ("head", 0.5)):
        ik.rotate_world(clip, bone, UP if up is None else np.asarray(up, dtype=np.float64), yaw * share)
        ik.rotate_world(clip, bone, _side_axis(clip, bone), pitch * share)
    return clip


def fingers_relax(clip, curl=0.35, variation=0.1, seed=3):
    """fingers bend like a relaxed hand (stock poses often keep them stiff and straight)"""
    rng = np.random.default_rng(seed)
    T = clip.frames
    rig = clip.rig
    for side in ("l", "r"):
        for f in range(1, 5):
            for k, share in (("", 0.6), ("1", 1.0), ("2", 0.8)):
                name = "%s_finger%d%s" % (side, f, k)
                if not rig.has(name):
                    continue
                c = curl * (1 + rng.uniform(-variation, variation)) * (0.8 + 0.1 * f)
                ik.rotate_local(clip, name, (0, 0, -1.0), np.full(T, np.radians(55) * c * share))
    return clip


def _side_axis(clip, bone):
    """the character's left axis (model space) - for pitch of upper body bones; per frame from the pelvis"""
    wp, wq = clip.world()
    p = clip.rig.index("pelvis")
    # the stock pelvis: model +x forward when standing; take the facing from the hips
    l = wp[:, clip.rig.index("l_thigh")] - wp[:, clip.rig.index("r_thigh")]
    l[:, 2] = 0
    return l / np.maximum(np.linalg.norm(l, axis=-1, keepdims=True), 1e-9)


def _forward_axis(clip):
    l = _side_axis(clip, "pelvis")
    f = np.cross(l, UP)
    return f / np.maximum(np.linalg.norm(f, axis=-1, keepdims=True), 1e-9)
