"""Motions of Kimodo (kimodo.cpp, NVIDIA's text-to-motion on the SOMA human skeleton, generated offline -
E:\\SourceModding\\HL2RPMSOURCE\\kimodo.cpp\\hl2rpm_out\\*.npz) moved onto a ValveBiped rig.

SOMA: +Y up, +Z forward, +X the character's left, metres, 30 fps; local rotations (xyzw) to the parent,
the identity pose is a T-pose. ValveBiped: X forward, Y left, Z up, inches; its bind pose is an A-pose
(arms ~50 degrees down). The transfer: a SOMA joint's world rotation is its rotation away from the T-pose;
the ValveBiped bone takes the same world rotation on top of its own T-pose rotation (the bind rotation,
with the arm bones turned up / out to point where SOMA's do in the T-pose). The pelvis follows the hips
(scaled by the hip heights); then the legs are solved again (two-bone IK) so the ankles land where SOMA's
do - the contacts don't slide and the feet don't sink."""
import numpy as np

from . import ik
from . import qmath as Q
from . import rig as R

# ValveBiped bone (short name) -> the SOMA joint whose world rotation it takes
BONES = [
    ("pelvis", "Hips"), ("spine", "Spine1"), ("spine1", "Spine2"), ("spine2", "Chest"), ("spine4", "Chest"),
    ("neck", "Neck2"), ("head", "Head"),
    ("l_clavicle", "LeftShoulder"), ("l_upperarm", "LeftArm"), ("l_forearm", "LeftForeArm"), ("l_hand", "LeftHand"),
    ("r_clavicle", "RightShoulder"), ("r_upperarm", "RightArm"), ("r_forearm", "RightForeArm"), ("r_hand", "RightHand"),
    ("l_thigh", "LeftLeg"), ("l_calf", "LeftShin"), ("l_foot", "LeftFoot"),
    ("r_thigh", "RightLeg"), ("r_calf", "RightShin"), ("r_foot", "RightFoot"),
]
# the arm bones point along +-Y in the T-pose (ValveBiped's bind pose has them down): (bone, child to aim with)
ARM_AIM = [("l_clavicle", "l_upperarm", 1.0), ("l_upperarm", "l_forearm", 1.0), ("l_forearm", "l_hand", 1.0),
           ("r_clavicle", "r_upperarm", -1.0), ("r_upperarm", "r_forearm", -1.0), ("r_forearm", "r_hand", -1.0)]


def _soma_to_src_vec(v):
    v = np.asarray(v, dtype=np.float64)
    return np.stack([v[..., 2], v[..., 0], v[..., 1]], axis=-1)


def _soma_to_src_quat(q):
    q = np.asarray(q, dtype=np.float64)
    return np.stack([q[..., 2], q[..., 0], q[..., 1], q[..., 3]], axis=-1)


def load(path):
    d = np.load(path, allow_pickle=True)
    names = [str(n) for n in d["joint_names"]]
    parents = np.asarray(d["joint_parents"], dtype=int)
    loc = np.asarray(d["local_rotations_xyzw"], dtype=np.float64)
    root = np.asarray(d["root_translations"], dtype=np.float64)
    offs = np.asarray(d["rest_offsets"], dtype=np.float64)
    fps = float(d["fps"]) if "fps" in d.files else 30.0
    # FK (SOMA axes)
    T, J = loc.shape[:2]
    wq = np.zeros_like(loc)
    wp = np.zeros((T, J, 3))
    for j in range(J):
        p = parents[j]
        if p < 0:
            wq[:, j] = loc[:, j]
            wp[:, j] = root
        else:
            wq[:, j] = Q.mul(wq[:, p], loc[:, j])
            wp[:, j] = wp[:, p] + Q.rotate(wq[:, p], np.repeat(offs[None, j], T, 0))
    return {"names": names, "parents": parents, "wq": wq, "wp": wp, "fps": fps, "offsets": offs}


def _tpose_rotations(rig):
    """world rotations of the rig's bones in a T-pose: the bind pose with the arm chains turned to +-Y"""
    rest = rig.rest_clip()
    wp, wq = rest.world()
    # the bind pose of a model faces -Y (studiomdl turns it by $origin ... -90); the animations face +X
    qz = Q.from_axis_angle(np.array([[0, 0, 1.0]]), np.array([np.pi / 2]))
    wq = Q.mul(np.repeat(qz, wq.shape[1], 0), wq[0])
    wp = Q.rotate(np.repeat(qz, wp.shape[1], 0), wp[0])
    out = {}
    for name, _ in BONES:
        out[name] = wq[rig.index(name)].copy()
    # turn each arm bone (and everything after it) so it points sideways; parents first
    for bone, child, side in ARM_AIM:
        i, c = rig.index(bone), rig.index(child)
        d = wp[c] - wp[i]
        d /= np.linalg.norm(d)
        target = np.array([0.0, side, 0.0])
        q = Q.between(d[None], target[None])[0]
        # apply to this bone and its descendants in the working pose
        for k in [i] + list(rig.descendants(i)):
            wq[k] = Q.mul(q[None], wq[k][None])[0]
            if k != i:
                wp[k] = wp[i] + Q.rotate(q[None], (wp[k] - wp[i])[None])[0]
        out[bone] = wq[i].copy()
    for s in ("l", "r"):
        out[s + "_hand"] = wq[rig.index(s + "_hand")].copy()
    return out


def retarget(motion, rig, name, fps=30.0, foot_ik=True, loop=False):
    """a SOMA motion (load()) -> a Clip of the rig"""
    T = motion["wq"].shape[0]
    sidx = {n: k for k, n in enumerate(motion["names"])}
    tpose = _tpose_rotations(rig)
    rest = rig.rest_clip(T, fps, name)
    wp, wq = rest.world()
    wp, wq = wp.copy(), wq.copy()
    # scale: the hips of SOMA standing ~0.99 m, the rig's pelvis
    s_hip = motion["wp"][0, sidx["Hips"], 1] - min(motion["wp"][0, sidx["LeftToeBase"], 1], motion["wp"][0, sidx["RightToeBase"], 1])
    r_hip = rig.rest_clip().world()[0][0, rig.index("pelvis"), 2]
    scale = r_hip / max(0.3, s_hip + 0.03)
    # world rotations: the SOMA rotation away from its T-pose (identity) on top of the rig's T-pose
    for bone, joint in BONES:
        d = _soma_to_src_quat(motion["wq"][:, sidx[joint]])
        wq[:, rig.index(bone)] = Q.mul(d, np.repeat(tpose[bone][None], T, 0))
    # bones the table doesn't name (fingers, toes, attachments): keep their bind rotation relative to the parent
    named = {rig.index(b) for b, _ in BONES}
    _rp, rq = rig.rest_clip().world()
    for i, p in enumerate(rig.parents):
        if i in named or p < 0:
            continue
        rel = Q.mul(Q.conj(rq[0, p]), rq[0, i])
        wq[:, i] = Q.mul(wq[:, p], np.repeat(rel[None], T, 0))
    # positions: the pelvis from the hips; everything else from the rig's own bone lengths
    hips = _soma_to_src_vec(motion["wp"][:, sidx["Hips"]]) * scale
    floor = min(motion["wp"][:, sidx["LeftToeBase"], 1].min(), motion["wp"][:, sidx["RightToeBase"], 1].min())
    hips[:, 2] = (motion["wp"][:, sidx["Hips"], 1] - floor) * scale + (r_hip - s_hip * scale) * 0.0
    lp, lq = rig.local_from_world(wp, wq)
    rest_local_pos = rig.rest_clip(T).pos
    lp = rest_local_pos.copy()
    lp[:, rig.index("pelvis")] = hips
    clip = R.Clip(rig, lp, lq, fps, name, loop)
    if foot_ik:
        for s, joint in (("l", "LeftFoot"), ("r", "RightFoot")):
            target = _soma_to_src_vec(motion["wp"][:, sidx[joint]]) * scale
            target[:, 2] = (motion["wp"][:, sidx[joint], 1] - floor) * scale
            # the rig's ankle stands a little higher than SOMA's foot joint: keep the rig's own ankle height
            ankle = rig.rest_clip().world()[0][0, rig.index(s + "_foot"), 2]
            target[:, 2] += ankle - (motion["wp"][0, sidx[joint], 1] - floor) * scale
            cwp, cwq = clip.world()
            knee = cwp[:, rig.index(s + "_calf")] - cwp[:, rig.index(s + "_thigh")]
            pole = cwp[:, rig.index(s + "_thigh")] + knee * 2.0
            ik.two_bone(clip, s + "_thigh", s + "_calf", s + "_foot", target, pole, end_world=cwq[:, rig.index(s + "_foot")])
    return clip
