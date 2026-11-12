"""Skeletons (ValveBiped and others) read from compiled models, forward kinematics over frames,
and clips of local bone transforms."""
import os
import sys

import numpy as np

from . import qmath as Q

TOOLS = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
if TOOLS not in sys.path:
    sys.path.insert(0, TOOLS)
if os.path.join(TOOLS, "dialog_editor") not in sys.path:
    sys.path.insert(0, os.path.join(TOOLS, "dialog_editor"))

import mdl_decompile as dec  # noqa: E402

_FS = None


def game_fs():
    global _FS
    if _FS is None:
        from dlgedit.world import gamefs
        _FS = gamefs.GameFS(dec.MOD, dec.SDK)
    return _FS


# short names -> ValveBiped
VB = {
    "pelvis": "ValveBiped.Bip01_Pelvis", "spine": "ValveBiped.Bip01_Spine", "spine1": "ValveBiped.Bip01_Spine1",
    "spine2": "ValveBiped.Bip01_Spine2", "spine4": "ValveBiped.Bip01_Spine4", "neck": "ValveBiped.Bip01_Neck1",
    "head": "ValveBiped.Bip01_Head1",
}
for _s, _S in (("l", "L"), ("r", "R")):
    VB.update({
        _s + "_clavicle": "ValveBiped.Bip01_%s_Clavicle" % _S, _s + "_upperarm": "ValveBiped.Bip01_%s_UpperArm" % _S,
        _s + "_forearm": "ValveBiped.Bip01_%s_Forearm" % _S, _s + "_hand": "ValveBiped.Bip01_%s_Hand" % _S,
        _s + "_thigh": "ValveBiped.Bip01_%s_Thigh" % _S, _s + "_calf": "ValveBiped.Bip01_%s_Calf" % _S,
        _s + "_foot": "ValveBiped.Bip01_%s_Foot" % _S, _s + "_toe": "ValveBiped.Bip01_%s_Toe0" % _S,
    })
    for _f in range(5):
        for _k in ("", "1", "2"):
            VB["%s_finger%d%s" % (_s, _f, _k)] = "ValveBiped.Bip01_%s_Finger%d%s" % (_S, _f, _k)


class Rig:
    def __init__(self, names, parents, rest_pos, rest_quat, source=""):
        self.names = list(names)
        self.parents = list(parents)
        self.rest_pos = np.asarray(rest_pos, dtype=np.float64)
        self.rest_quat = Q.normalize(rest_quat)
        self.source = source
        self._idx = {n.lower(): i for i, n in enumerate(self.names)}

    @staticmethod
    def from_model(path):
        m = dec.Model(game_fs(), path)
        quat = Q.euler_to_quat(np.array(m.bone_rot))
        return Rig(m.bone_names, m.bone_parent, np.array(m.bone_pos), quat, path)

    def __len__(self):
        return len(self.names)

    def index(self, name):
        n = VB.get(name, name).lower()
        if n in self._idx:
            return self._idx[n]
        raise KeyError(name)

    def has(self, name):
        return VB.get(name, name).lower() in self._idx

    def children(self, i):
        return [j for j, p in enumerate(self.parents) if p == i]

    def descendants(self, i):
        out = []
        todo = [i]
        while todo:
            k = todo.pop()
            for c in self.children(k):
                out.append(c)
                todo.append(c)
        return out

    # ---- kinematics: pos (T, B, 3), quat (T, B, 4) local -> world
    def fk(self, pos, quat):
        T = pos.shape[0]
        wp = np.zeros_like(pos)
        wq = np.zeros_like(quat)
        for i, p in enumerate(self.parents):
            if p < 0:
                wp[:, i] = pos[:, i]
                wq[:, i] = quat[:, i]
            else:
                wq[:, i] = Q.mul(wq[:, p], quat[:, i])
                wp[:, i] = wp[:, p] + Q.rotate(wq[:, p], pos[:, i])
        return wp, Q.normalize(wq)

    def local_from_world(self, wpos, wquat):
        pos = np.zeros_like(wpos)
        quat = np.zeros_like(wquat)
        for i, p in enumerate(self.parents):
            if p < 0:
                pos[:, i] = wpos[:, i]
                quat[:, i] = wquat[:, i]
            else:
                inv = Q.conj(wquat[:, p])
                quat[:, i] = Q.mul(inv, wquat[:, i])
                pos[:, i] = Q.rotate(inv, wpos[:, i] - wpos[:, p])
        return pos, Q.normalize(quat)

    def rest_clip(self, frames=1, fps=30.0, name="rest"):
        return Clip(self, np.repeat(self.rest_pos[None], frames, 0), np.repeat(self.rest_quat[None], frames, 0), fps, name)


class Clip:
    """local transforms of every bone of a rig over frames"""

    def __init__(self, rig, pos, quat, fps=30.0, name="", loop=False):
        self.rig = rig
        self.pos = np.array(pos, dtype=np.float64)
        self.quat = Q.normalize(np.array(quat, dtype=np.float64))
        self.fps = float(fps)
        self.name = name
        self.loop = loop
        self.events = []      # (frame, event, options)

    @property
    def frames(self):
        return self.pos.shape[0]

    @property
    def duration(self):
        return (self.frames - 1) / self.fps

    def copy(self, name=None):
        c = Clip(self.rig, self.pos.copy(), self.quat.copy(), self.fps, name or self.name, self.loop)
        c.events = list(self.events)
        return c

    def world(self):
        return self.rig.fk(self.pos, self.quat)

    def set_world(self, wpos, wquat):
        self.pos, self.quat = self.rig.local_from_world(wpos, wquat)

    def joint(self, name):
        wp, _wq = self.world()
        return wp[:, self.rig.index(name)]

    def slice(self, a, b=None):
        c = self.copy()
        c.pos = c.pos[a:b]
        c.quat = c.quat[a:b]
        return c

    def resample(self, fps):
        n = max(2, int(round(self.duration * fps)) + 1)
        t = np.linspace(0, self.frames - 1, n)
        i0 = np.floor(t).astype(int)
        i1 = np.minimum(i0 + 1, self.frames - 1)
        s = (t - i0)[:, None, None]
        c = self.copy()
        c.pos = self.pos[i0] * (1 - s) + self.pos[i1] * s
        c.quat = Q.slerp(self.quat[i0], self.quat[i1], np.repeat(s[..., 0], self.quat.shape[1], 1))
        c.fps = fps
        return c

    def then(self, other, blend_frames=8):
        """this clip, then the other one, crossfaded over blend_frames"""
        b = max(0, min(blend_frames, self.frames - 1, other.frames - 1))
        c = self.copy()
        if b == 0:
            c.pos = np.concatenate([self.pos, other.pos[1:]])
            c.quat = np.concatenate([self.quat, other.quat[1:]])
            return c
        w = smoothstep(np.linspace(0, 1, b))[:, None, None]
        mp = self.pos[-b:] * (1 - w) + other.pos[:b] * w
        mq = Q.slerp(self.quat[-b:], other.quat[:b], np.repeat(w[..., 0], self.quat.shape[1], 1))
        c.pos = np.concatenate([self.pos[:-b], mp, other.pos[b:]])
        c.quat = np.concatenate([self.quat[:-b], mq, other.quat[b:]])
        c.events = list(self.events) + [(f + self.frames - b, e, o) for f, e, o in other.events]
        return c

    def make_loop(self, blend_frames=10):
        """blend the end into the start so the clip loops without a pop"""
        b = max(1, min(blend_frames, self.frames // 3))
        c = self.copy()
        w = smoothstep(np.linspace(0, 1, b))[:, None, None]
        c.pos[-b:] = self.pos[-b:] * (1 - w) + self.pos[0][None] * w
        c.quat[-b:] = Q.slerp(self.quat[-b:], np.repeat(self.quat[0][None], b, 0), np.repeat(w[..., 0], self.quat.shape[1], 1))
        c.loop = True
        return c


_STOCK_MODELS = {}


def stock_clip(rig, model, seq_name):
    """a sequence of a game model (its own, e.g. humans/male_ss.mdl "sitchair1") as a clip of rig"""
    fs = game_fs()
    if model not in _STOCK_MODELS:
        _STOCK_MODELS[model] = dec.Model(fs, model)
    mm = _STOCK_MODELS[model]
    seqs = [x for x in mm.sequences() if x["name"].lower() == seq_name.lower()]
    if not seqs:
        raise KeyError("%s has no sequence %s" % (model, seq_name))
    s = seqs[0]
    fr = dec.sample_frames(fs, mm, s)
    T = len(fr)
    pos = np.repeat(rig.rest_pos[None], T, 0).copy()
    quat = np.repeat(rig.rest_quat[None], T, 0).copy()
    for i, n in enumerate(mm.bone_names):
        if rig.has(n):
            j = rig.index(n)
            pos[:, j] = [f[0][i] for f in fr]
            quat[:, j] = Q.euler_to_quat(np.array([f[1][i] for f in fr]))
    c = Clip(rig, pos, Q.continuous(quat), s["fps"] or 30.0, seq_name, bool(s["flags"] & 1))
    return c


def hold(clip, seconds, frame=0, name=None):
    """a still clip of one frame of another"""
    n = max(2, int(round(seconds * clip.fps)) + 1)
    c = clip.copy(name)
    c.pos = np.repeat(clip.pos[frame:frame + 1], n, 0)
    c.quat = np.repeat(clip.quat[frame:frame + 1], n, 0)
    c.events = []
    return c


def smoothstep(t):
    t = np.clip(t, 0, 1)
    return t * t * (3 - 2 * t)


def smootherstep(t):
    t = np.clip(t, 0, 1)
    return t * t * t * (t * (t * 6 - 15) + 10)


def remap(clip, rig):
    """the clip on another rig with the same bone names (police / soldiers share the citizen skeleton);
    bones the clip doesn't have stay at the target's reference pose"""
    T = clip.frames
    pos = np.repeat(rig.rest_pos[None], T, 0).copy()
    quat = np.repeat(rig.rest_quat[None], T, 0).copy()
    for i, n in enumerate(rig.names):
        if clip.rig.has(n):
            j = clip.rig.index(n)
            pos[:, i] = clip.pos[:, j]
            quat[:, i] = clip.quat[:, j]
    c = Clip(rig, pos, quat, clip.fps, clip.name, clip.loop)
    c.events = list(clip.events)
    return c
