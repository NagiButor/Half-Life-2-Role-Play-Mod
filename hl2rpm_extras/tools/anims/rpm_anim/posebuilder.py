"""Poses from a few numbers, and motion from keyframed numbers.

A pose is described in the character's terms (model space, the character faces +x, left is +y,
up +z): where the pelvis is and how it is tilted, how the spine bends, where the head looks,
where the hands and the feet are (IK), how the fingers curl. Between keyframes the numbers are
interpolated - positions of hands and feet move along smooth paths, the IK keeps contacts - and
every channel has its own timing (the hands start before the hips, the head settles last), which
is what makes a transition look like a person rather than two poses cross-faded.

Channels (Track names):
  base        0..1  blend of the two base frames (standing / sitting stock poses): non-IK bones;
              with a list of bases (base_b = [b1, b2, ...]) 0..n: 0 base_a, 1 b1, 2 b2, between - blended
  feet_flat   0..1  (with flat_feet) how much the feet keep the flat rotation (0 - the base's own feet)
  pelvis      (x, y, z)                     pelvis bone position
  pelvis_rot  (pitch, yaw, roll) degrees    on top of the base pelvis (pitch + = lean forward)
  spine       (pitch, yaw, roll) degrees    spread over Spine..Spine4
  head        (pitch, yaw, roll) degrees    spread over Neck1 / Head1 (pitch + = look down)
  l_hand / r_hand   (x, y, z)  hand bone target (None / missing = keep FK of the base)
  l_hand_rot / r_hand_rot  (pitch, yaw, roll) degrees on top of the base hand world rotation
  l_elbow / r_elbow (x, y, z)  direction the elbow points
  l_foot / r_foot   (x, y, z)  foot bone target
  l_foot_rot / r_foot_rot (pitch, yaw, roll) degrees on top of the base foot world rotation
  l_knee / r_knee   (x, y, z)  direction the knee points
  l_clav / r_clav   (raise, forward) degrees  shoulder shrug / roll
  l_fingers / r_fingers  curl 0..1 (added to the base)
"""
import numpy as np

from . import ik
from . import qmath as Q
from . import rig as R

AX_F = np.array([1.0, 0, 0])
AX_L = np.array([0, 1.0, 0])
AX_U = np.array([0, 0, 1.0])

EASES = {
    "linear": lambda t: t,
    "smooth": lambda t: t * t * (3 - 2 * t),
    "smoother": lambda t: t * t * t * (t * (t * 6 - 15) + 10),
    "in": lambda t: t * t,
    "out": lambda t: 1 - (1 - t) * (1 - t),
    "in3": lambda t: t * t * t,
    "out3": lambda t: 1 - (1 - t) ** 3,
    # a little past the target and back (settling)
    "overshoot": lambda t: 1 - (1 - t) ** 3 + np.sin(np.clip(t, 0, 1) * np.pi) * 0.08 * (1 - t),
}


class Track:
    """keys: [(time seconds, value, ease_into_this_key)]; values are numbers or vectors"""

    def __init__(self, *keys):
        self.keys = []
        for k in keys:
            t, v = k[0], k[1]
            e = k[2] if len(k) > 2 else "smoother"
            self.keys.append((float(t), None if v is None else np.asarray(v, dtype=np.float64), e))
        self.keys.sort(key=lambda k: k[0])

    def sample(self, times):
        ks = self.keys
        if any(k[1] is None for k in ks):
            raise ValueError("None keys are not interpolable")
        out = []
        for t in times:
            if t <= ks[0][0]:
                out.append(ks[0][1])
                continue
            if t >= ks[-1][0]:
                out.append(ks[-1][1])
                continue
            for a, b in zip(ks, ks[1:]):
                if a[0] <= t <= b[0]:
                    u = (t - a[0]) / max(1e-9, b[0] - a[0])
                    w = EASES[b[2]](u)
                    out.append(a[1] + (b[1] - a[1]) * w)
                    break
        return np.array(out)


def _rot_xyz(deg):
    """(pitch, yaw, roll) degrees -> quaternion in the character frame: yaw about up, pitch about
    left (forward lean +), roll about forward (lean to the right +)"""
    deg = np.asarray(deg, dtype=np.float64)
    p, y, r = np.radians(deg[..., 0]), np.radians(deg[..., 1]), np.radians(deg[..., 2])
    qy = Q.from_axis_angle(np.broadcast_to(AX_U, deg.shape), y)
    qp = Q.from_axis_angle(np.broadcast_to(AX_L, deg.shape), p)
    qr = Q.from_axis_angle(np.broadcast_to(AX_F, deg.shape), r)
    return Q.mul(qy, Q.mul(qp, qr))


def _aim_quat(aim, current):
    """aim (T,6): fingers dir (3) and palm dir (3) - ValveBiped hands: local x along the fingers, the palm
    toward local -y. The length of the finger vector (0..1) weights it against the current rotation."""
    f = aim[:, :3]
    w = np.clip(np.linalg.norm(f, axis=1), 0.0, 1.0)
    x = f / np.maximum(np.linalg.norm(f, axis=1, keepdims=True), 1e-9)
    palm = aim[:, 3:6]
    y = -palm
    y = y - np.sum(y * x, axis=1, keepdims=True) * x
    y /= np.maximum(np.linalg.norm(y, axis=1, keepdims=True), 1e-9)
    z = np.cross(x, y)
    M = np.stack([x, y, z], axis=2)
    q = Q.from_matrix(M)
    return Q.slerp(current, q, w)


SPINE = (("spine", 0.15), ("spine1", 0.25), ("spine2", 0.3), ("spine4", 0.3))
NECK = (("neck", 0.4), ("head", 0.6))


def build(rig, base_a, base_b, tracks, duration, fps=30.0, name="", loop=False, flat_feet=None):
    """base_a / base_b: one-frame clips (frame 0 is used); tracks: dict channel -> Track;
    flat_feet: a one-frame clip whose foot world rotations (flat on the floor) the IK feet keep"""
    T = int(round(duration * fps)) + 1
    times = np.arange(T) / fps
    get = lambda ch, default=None: tracks[ch].sample(times) if ch in tracks else default  # noqa: E731
    wb = get("base", np.zeros((T, 1)))
    wb = np.reshape(wb, (T,))
    bases = [base_a] + (list(base_b) if isinstance(base_b, (list, tuple)) else [base_b])
    seg = np.clip(np.floor(wb).astype(int), 0, len(bases) - 2)
    u = np.clip(wb - seg, 0.0, 1.0)
    bq = np.stack([b.quat[0] for b in bases])
    bp = np.stack([b.pos[0] for b in bases])
    qa, qb = bq[seg], bq[seg + 1]
    pa, pb = bp[seg], bp[seg + 1]
    clip = R.Clip(rig, pa * (1 - u[:, None, None]) + pb * u[:, None, None],
                  Q.slerp(qa, qb, np.repeat(u[:, None], qa.shape[1], 1)), fps, name, loop)
    # keep the feet / hands of the base as defaults for the IK
    wp0, wq0 = clip.world()
    pel = rig.index("pelvis")
    # pelvis position and tilt
    if "pelvis" in tracks:
        clip.pos[:, pel] = get("pelvis")
    if "pelvis_rot" in tracks:
        q = _rot_xyz(get("pelvis_rot"))
        clip.quat[:, pel] = Q.mul(q, clip.quat[:, pel])
    # spine / neck / head bends: world rotations in the character frame (yaw of the pelvis track)
    # the bends follow the yaw of pelvis_rot (a body turned to lie along the bed bends about its own left axis)
    qyaw = None
    if "pelvis_rot" in tracks:
        yaw = np.radians(get("pelvis_rot")[:, 1])
        if np.abs(yaw).max() > 1e-6:
            qyaw = Q.from_axis_angle(np.broadcast_to(AX_U, (T, 3)), yaw)
    for ch, chain in (("spine", SPINE), ("head", NECK)):
        if ch not in tracks:
            continue
        ang = get(ch)
        for bone, share in chain:
            q = _rot_xyz(ang * share)
            if qyaw is not None:
                q = Q.mul(qyaw, Q.mul(q, Q.conj(qyaw)))
            ik.set_world_rotation(clip, bone, Q.mul(q, clip.world()[1][:, rig.index(bone)]))
    # shoulders
    for s in ("l", "r"):
        ch = s + "_clav"
        if ch in tracks:
            v = get(ch)
            sgn = 1.0 if s == "l" else -1.0
            ik.rotate_world(clip, s + "_clavicle", AX_F, np.radians(v[:, 0]) * sgn)
            ik.rotate_world(clip, s + "_clavicle", AX_U, np.radians(v[:, 1]) * sgn)
    # legs
    for s in ("l", "r"):
        if s + "_foot" not in tracks:
            continue
        target = get(s + "_foot")
        knee = get(s + "_knee", np.repeat([[1.0, 0.15 if s == "l" else -0.15, 0]], T, 0))
        wp, wq = clip.world()
        fw = wq0[:, rig.index(s + "_foot")]
        if flat_feet is not None:
            _fp, fq = flat_feet.world()
            flat = np.repeat(fq[:1, rig.index(s + "_foot")], T, 0)
            if "feet_flat" in tracks:
                ff = np.reshape(get("feet_flat"), (T,))
                fw = Q.slerp(fw, flat, np.clip(ff, 0.0, 1.0))
            else:
                fw = flat
        if s + "_foot_rot" in tracks:
            fw = Q.mul(_rot_xyz(get(s + "_foot_rot")), fw)
        thigh = wp[:, rig.index(s + "_thigh")]
        ik.two_bone(clip, s + "_thigh", s + "_calf", s + "_foot", target, thigh + knee * 40.0, end_world=fw)
    # arms
    for s in ("l", "r"):
        if s + "_hand" not in tracks:
            continue
        target = get(s + "_hand")
        elbow = get(s + "_elbow", np.repeat([[-0.4, 0.5 if s == "l" else -0.5, -1.0]], T, 0))
        wp, wq = clip.world()
        hw = wq[:, rig.index(s + "_hand")]
        if s + "_hand_rot" in tracks:
            hw = Q.mul(_rot_xyz(get(s + "_hand_rot")), hw)
        if s + "_hand_aim" in tracks:
            # (fingers direction, palm direction) in model space -> the hand's world rotation; blended in by the
            # track's length 0..1 of the finger vector (0 = keep the FK hand)
            aim = get(s + "_hand_aim")
            hw = _aim_quat(aim, hw)
        ua = wp[:, rig.index(s + "_upperarm")]
        ik.two_bone(clip, s + "_upperarm", s + "_forearm", s + "_hand", target, ua + elbow * 40.0, end_world=hw)
    # fingers
    for s in ("l", "r"):
        if s + "_fingers" not in tracks:
            continue
        c = np.reshape(get(s + "_fingers"), (T,))
        curl_fingers(clip, s, c)
    return clip


def curl_fingers(clip, side, amount):
    """amount (T,) 0..1: bend the four fingers (and the thumb a little) toward a fist"""
    rig = clip.rig
    sgn = -1.0          # both hands: local x along the finger, the palm toward -y, so -z bends toward the palm
    for f in range(0, 5):
        for k, share in (("", 0.55), ("1", 1.0), ("2", 0.75)):
            name = "%s_finger%d%s" % (side, f, k)
            if not rig.has(name):
                continue
            a = np.radians(80.0 if f else 30.0) * share * amount
            ik.rotate_local(clip, name, (0, 0, sgn), a)


def place(clip, name):
    """world position of a bone, frame by frame"""
    wp, _ = clip.world()
    return wp[:, clip.rig.index(name)]


def probe(rig, base_a, base_b, values, flat_feet=None):
    """build one frame of a pose given as channel -> value and return the world positions of its bones
    (to place hand / foot targets relative to where the body really is)"""
    tr = {k: Track((0, v)) for k, v in values.items() if v is not None}
    c = build(rig, base_a, base_b, tr, 0.0, flat_feet=flat_feet)
    wp, _ = c.world()
    return {n: wp[0, i] for i, n in enumerate(rig.names)}, (lambda name: wp[0, rig.index(name)])
