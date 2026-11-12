"""Inverse kinematics and world-space edits of clips (ValveBiped, model space: +x forward, +z up)."""
import numpy as np

from . import qmath as Q


def _as_frames(v, T, dim):
    v = np.asarray(v, dtype=np.float64)
    if v.ndim == 1:
        v = np.repeat(v[None], T, 0)
    assert v.shape == (T, dim), v.shape
    return v


def _weights(w, T):
    if w is None:
        return np.ones(T)
    w = np.asarray(w, dtype=np.float64)
    return np.full(T, float(w)) if w.ndim == 0 else w


def rotate_world(clip, bone, axis, angle, weight=None):
    """turn a bone (and everything under it) about a world axis through the bone: axis (3,)/(T,3), angle (T,)"""
    rig = clip.rig
    i = rig.index(bone)
    T = clip.frames
    axis = _as_frames(axis, T, 3)
    angle = np.asarray(angle, dtype=np.float64) * _weights(weight, T)
    if angle.ndim == 0:
        angle = np.full(T, float(angle))
    wp, wq = clip.world()
    R = Q.from_axis_angle(axis, angle)
    p = rig.parents[i]
    pw = wq[:, p] if p >= 0 else np.tile([0, 0, 0, 1.0], (T, 1))
    new_w = Q.mul(R, wq[:, i])
    clip.quat[:, i] = Q.mul(Q.conj(pw), new_w)
    if p < 0:
        clip.quat[:, i] = new_w
    return clip


def rotate_local(clip, bone, axis_local, angle):
    """turn a bone about an axis of its own frame (degrees-free: radians)"""
    i = clip.rig.index(bone)
    T = clip.frames
    angle = np.asarray(angle, dtype=np.float64)
    if angle.ndim == 0:
        angle = np.full(T, float(angle))
    R = Q.from_axis_angle(_as_frames(axis_local, T, 3), angle)
    clip.quat[:, i] = Q.mul(clip.quat[:, i], R)
    return clip


def set_world_rotation(clip, bone, wquat, weight=None):
    rig = clip.rig
    i = rig.index(bone)
    T = clip.frames
    wquat = _as_frames(wquat, T, 4)
    _wp, wq = clip.world()
    p = rig.parents[i]
    new_local = Q.mul(Q.conj(wq[:, p]), wquat) if p >= 0 else wquat
    w = _weights(weight, T)
    clip.quat[:, i] = Q.slerp(clip.quat[:, i], new_local, w)
    return clip


def move_root(clip, delta):
    """move the root bone in model space: delta (3,)/(T,3)"""
    clip.pos[:, 0] += _as_frames(delta, clip.frames, 3)
    return clip


def two_bone(clip, upper, mid, end, target, pole, weight=None, end_world=None):
    """solve upper/mid so that end reaches target (T,3); pole: a point the middle joint bends toward.
    end keeps its world rotation (or takes end_world (T,4)). weight (T,) blends with the input."""
    rig = clip.rig
    a, b, c = rig.index(upper), rig.index(mid), rig.index(end)
    T = clip.frames
    target = _as_frames(target, T, 3)
    pole = _as_frames(pole, T, 3)
    w = _weights(weight, T)
    wp, wq = clip.world()
    A, B, C = wp[:, a], wp[:, b], wp[:, c]
    la = np.linalg.norm(B - A, axis=-1)
    lb = np.linalg.norm(C - B, axis=-1)
    d_vec = target - A
    d = np.linalg.norm(d_vec, axis=-1)
    d = np.clip(d, np.abs(la - lb) + 1e-3, la + lb - 1e-3)
    dirAT = d_vec / np.maximum(np.linalg.norm(d_vec, axis=-1, keepdims=True), 1e-9)
    pv = pole - A
    pv = pv - np.sum(pv * dirAT, -1, keepdims=True) * dirAT
    # no usable pole: keep the current bend plane
    cur = B - A
    cur = cur - np.sum(cur * dirAT, -1, keepdims=True) * dirAT
    pn = np.linalg.norm(pv, axis=-1, keepdims=True)
    pv = np.where(pn > 1e-4, pv / np.maximum(pn, 1e-9), cur / np.maximum(np.linalg.norm(cur, axis=-1, keepdims=True), 1e-9))
    cosA = np.clip((la * la + d * d - lb * lb) / (2 * la * d), -1, 1)
    sinA = np.sqrt(1 - cosA * cosA)
    Bn = A + la[:, None] * (cosA[:, None] * dirAT + sinA[:, None] * pv)
    Cn = A + dirAT * d[:, None]
    q1 = Q.between(B - A, Bn - A)
    wq_a = Q.mul(q1, wq[:, a])
    # mid after the upper turned
    C1 = Bn + Q.rotate(q1, C - B)
    q2 = Q.between(C1 - Bn, Cn - Bn)
    wq_b = Q.mul(q2, Q.mul(q1, wq[:, b]))
    wq_c = wq[:, c] if end_world is None else _as_frames(end_world, T, 4)
    pa = rig.parents[a]
    la_q = Q.mul(Q.conj(wq[:, pa]), wq_a) if pa >= 0 else wq_a
    lb_q = Q.mul(Q.conj(wq_a), wq_b)
    lc_q = Q.mul(Q.conj(wq_b), wq_c)
    W = w[:, None]
    clip.quat[:, a] = Q.slerp(clip.quat[:, a], la_q, w)
    clip.quat[:, b] = Q.slerp(clip.quat[:, b], lb_q, w)
    if end_world is not None or True:
        # the end's world rotation stays (feet flat on the floor, hands as they were)
        _wp2, wq2 = clip.world()
        lc_keep = Q.mul(Q.conj(wq2[:, b]), wq_c)
        clip.quat[:, c] = Q.slerp(clip.quat[:, c], lc_keep, w)
    del W
    return clip


def leg(clip, side, foot_target, knee_dir=(1.0, 0, 0), weight=None, foot_world=None):
    """foot_target: where the foot bone goes (T,3); knee_dir: the knee points this way (model space)"""
    wp, _wq = clip.world()
    thigh = clip.rig.index(side + "_thigh")
    pole = wp[:, thigh] + np.asarray(knee_dir, dtype=np.float64) * 40.0 if np.ndim(knee_dir) == 1 else wp[:, thigh] + np.asarray(knee_dir) * 40.0
    return two_bone(clip, side + "_thigh", side + "_calf", side + "_foot", foot_target, pole, weight, foot_world)


def arm(clip, side, hand_target, elbow_dir=(-0.3, 0, -1.0), weight=None, hand_world=None):
    wp, _wq = clip.world()
    ua = clip.rig.index(side + "_upperarm")
    ed = np.asarray(elbow_dir, dtype=np.float64)
    if side == "l" and ed.ndim == 1:
        pass
    pole = wp[:, ua] + (ed * 40.0 if ed.ndim == 1 else ed * 40.0)
    return two_bone(clip, side + "_upperarm", side + "_forearm", side + "_hand", hand_target, pole, weight, hand_world)


def look_at(clip, target, weight=None, neck_share=0.4, spine_share=0.15, limit_deg=70):
    """turn the head (and some of the neck / upper spine) toward a point (T,3) in model space"""
    rig = clip.rig
    T = clip.frames
    target = _as_frames(target, T, 3)
    w = _weights(weight, T)
    for bone, share in (("spine4", spine_share), ("neck", neck_share), ("head", 1.0)):
        wp, wq = clip.world()
        h = rig.index("head")
        i = rig.index(bone)
        # the face looks along the head's ... we take the current gaze as head forward = model +x turned by the head
        fwd = Q.rotate(wq[:, h], _head_forward(rig))
        want = target - wp[:, h]
        want /= np.maximum(np.linalg.norm(want, axis=-1, keepdims=True), 1e-9)
        q = Q.between(fwd, want)
        ang = Q.angle_between(np.tile([0, 0, 0, 1.0], (T, 1)), q)
        lim = np.radians(limit_deg)
        scale = np.where(ang > lim, lim / np.maximum(ang, 1e-9), 1.0)
        q = Q.slerp(np.tile([0, 0, 0, 1.0], (T, 1)), q, scale * share * w)
        p = rig.parents[i]
        new_w = Q.mul(q, wq[:, i])
        clip.quat[:, i] = Q.mul(Q.conj(wq[:, p]), new_w)
    return clip


_HEAD_FWD = {}


def _head_forward(rig):
    """the head bone's local axis that points where the face looks (from the reference: model +x... )"""
    key = id(rig)
    if key not in _HEAD_FWD:
        # in the stock idle the face looks along model +x; the reference pose faces +y. Take the
        # rest world rotation of the head and the reference facing (+y) to find the local axis.
        wp, wq = rig.fk(rig.rest_pos[None], rig.rest_quat[None])
        h = rig.index("head")
        face_world = _rest_facing(rig)
        _HEAD_FWD[key] = Q.rotate(Q.conj(wq[0, h]), face_world)
    return _HEAD_FWD[key]


def _rest_facing(rig):
    """the direction the reference pose faces (toes point forward): from the toe bones"""
    wp, _ = rig.fk(rig.rest_pos[None], rig.rest_quat[None])
    f = (wp[0, rig.index("l_toe")] + wp[0, rig.index("r_toe")]) / 2 - (wp[0, rig.index("l_foot")] + wp[0, rig.index("r_foot")]) / 2
    f[2] = 0
    return f / np.linalg.norm(f)
