"""Animations of studio models for previews (M7): the skeleton of a model, its sequences and
those of the models it includes ($includemodel: male_shared, *_gestures, *_postures...),
decoding of the compressed animation data (RLE values, Quaternion48/64, Vector48), demand
loaded blocks (.ani) and sections, bone matrices and skinning of the reference meshes (numpy).

Only the first animation of a blend sequence is sampled (no pose parameters, no layers, no IK):
enough to see what a sequence is - an editor preview, not the game's animation system.
"""
import struct

import numpy as np

from .mdl import _cstr

ANIM_RAWPOS, ANIM_RAWROT, ANIM_ANIMPOS, ANIM_ANIMROT, ANIM_DELTA, ANIM_RAWROT2 = 0x01, 0x02, 0x04, 0x08, 0x10, 0x20
SEQ_LOOPING = 0x0001
SEQ_OVERRIDE = 0x0800      # a forward declared (empty) sequence: the real one comes from an included model
ANIMDESC_DELTA = 0x0004


def _quat_from_euler(a):
    """RadianEuler (x, y, z) -> quaternion (x, y, z, w) like AngleQuaternion( RadianEuler ) of mathlib."""
    sr, cr = np.sin(a[..., 0] * 0.5), np.cos(a[..., 0] * 0.5)
    sp, cp = np.sin(a[..., 1] * 0.5), np.cos(a[..., 1] * 0.5)
    sy, cy = np.sin(a[..., 2] * 0.5), np.cos(a[..., 2] * 0.5)
    srXcp, crXsp = sr * cp, cr * sp
    crXcp, srXsp = cr * cp, sr * sp
    return np.stack([srXcp * cy - crXsp * sy, crXsp * cy + srXcp * sy, crXcp * sy - srXsp * cy, crXcp * cy + srXsp * sy], -1)


def _quat_matrix(q):
    x, y, z, w = q[..., 0], q[..., 1], q[..., 2], q[..., 3]
    m = np.empty(q.shape[:-1] + (3, 3), dtype=np.float64)
    m[..., 0, 0] = 1 - 2 * (y * y + z * z)
    m[..., 0, 1] = 2 * (x * y - w * z)
    m[..., 0, 2] = 2 * (x * z + w * y)
    m[..., 1, 0] = 2 * (x * y + w * z)
    m[..., 1, 1] = 1 - 2 * (x * x + z * z)
    m[..., 1, 2] = 2 * (y * z - w * x)
    m[..., 2, 0] = 2 * (x * z - w * y)
    m[..., 2, 1] = 2 * (y * z + w * x)
    m[..., 2, 2] = 1 - 2 * (x * x + y * y)
    return m


def _quat_mul(a, b):
    ax, ay, az, aw = a
    bx, by, bz, bw = b
    return np.array([aw * bx + ax * bw + ay * bz - az * by, aw * by - ax * bz + ay * bw + az * bx,
                     aw * bz + ax * by - ay * bx + az * bw, aw * bw - ax * bx - ay * by - az * bz])


def _slerp(a, b, t):
    d = float(np.dot(a, b))
    if d < 0:
        b, d = -b, -d
    if d > 0.9995:
        r = a + (b - a) * t
    else:
        th = np.arccos(d)
        r = (np.sin((1 - t) * th) * a + np.sin(t * th) * b) / np.sin(th)
    return r / max(np.linalg.norm(r), 1e-9)


def _half(h):
    return float(np.frombuffer(struct.pack("<H", h), dtype=np.float16)[0])


class Bones:
    def __init__(self, data):
        numbones, boneindex = struct.unpack_from("<ii", data, 156)
        n = max(0, min(numbones, 256))
        self.names, self.parent = [], []
        self.pos = np.zeros((n, 3))
        self.quat = np.zeros((n, 4))
        self.rot = np.zeros((n, 3))
        self.posscale = np.zeros((n, 3))
        self.rotscale = np.zeros((n, 3))
        self.pose_to_bone = np.zeros((n, 3, 4))
        for i in range(n):
            b = boneindex + i * 216
            name, parent = struct.unpack_from("<ii", data, b)
            self.names.append(_cstr(data, b + name).lower())
            self.parent.append(parent)
            self.pos[i] = struct.unpack_from("<3f", data, b + 32)
            self.quat[i] = struct.unpack_from("<4f", data, b + 44)
            self.rot[i] = struct.unpack_from("<3f", data, b + 60)
            self.posscale[i] = struct.unpack_from("<3f", data, b + 72)
            self.rotscale[i] = struct.unpack_from("<3f", data, b + 84)
            self.pose_to_bone[i] = np.array(struct.unpack_from("<12f", data, b + 96)).reshape(3, 4)
        self.index = {nm: i for i, nm in enumerate(self.names)}

    def __len__(self):
        return len(self.names)


class Sequence:
    __slots__ = ("name", "activity", "src", "anim", "fps", "frames", "looping", "forward", "blend_anims", "blend_range")

    def __init__(self, name, activity, src, anim, fps, frames, looping, forward=False):
        self.name, self.activity, self.src, self.anim = name, activity, src, anim
        self.fps, self.frames, self.looping, self.forward = fps, frames, looping, forward
        # a sequence blended by a pose parameter (one axis): its animations and the parameter's range
        # (the library's sitting animations by rpm_seat_h)
        self.blend_anims = [anim]
        self.blend_range = None

    @property
    def duration(self):
        return max(1, self.frames - 1) / max(self.fps, 1e-3)


class Source:
    """One .mdl with animations (the model itself or an included one) and its .ani."""

    def __init__(self, fs, name, data, base_bones):
        self.name = name
        self.data = data
        self.bones = Bones(data)
        # this model's bone -> bone of the model shown (by name; -1 = not there)
        self.remap = np.array([base_bones.index.get(nm, -1) for nm in self.bones.names], dtype=np.int64) if base_bones \
            else np.arange(len(self.bones))
        self.ani = None
        nameidx, numblocks, blockidx = struct.unpack_from("<iii", data, 348)
        self.blocks = []
        if numblocks > 0 and nameidx > 0:
            for k in range(min(numblocks, 4096)):
                self.blocks.append(struct.unpack_from("<ii", data, blockidx + k * 8))
            ani_name = _cstr(data, nameidx).replace("\\", "/").lower()
            if ani_name:
                if not ani_name.startswith("models/"):
                    ani_name = "models/" + ani_name
                self.ani = fs.read(ani_name)
        numanim, animindex = struct.unpack_from("<ii", data, 180)
        self.animdescs = [animindex + i * 100 for i in range(max(0, min(numanim, 8192)))]

    def anim_info(self, k):
        a = self.animdescs[k]
        fps, flags, frames = struct.unpack_from("<fii", self.data, a + 8)
        return fps, flags, frames

    def _anim_data(self, k, frame):
        """(buffer, offset of the first mstudioanim_t, frame inside the section) for an animation and a frame."""
        a = self.animdescs[k]
        block, index = struct.unpack_from("<ii", self.data, a + 52)
        sectionindex, sectionframes = struct.unpack_from("<ii", self.data, a + 80)
        if sectionframes > 0 and sectionindex:
            sec = frame // sectionframes
            block, index = struct.unpack_from("<ii", self.data, a + sectionindex + sec * 8)
            frame -= sec * sectionframes
        if block == 0:
            return self.data, a + index, frame
        if self.ani is None or block >= len(self.blocks):
            return None, 0, frame
        start = self.blocks[block][0]
        return self.ani, start + index, frame

    def sample(self, k, frame):
        """Local (pos, quat) of this model's bones at an integer frame; bones without data keep the reference pose."""
        bones = self.bones
        n = len(bones)
        pos = bones.pos.copy()
        quat = bones.quat.copy()
        fps, flags, frames = self.anim_info(k)
        frame = int(max(0, min(frame, max(0, frames - 1))))
        buf, off, f = self._anim_data(k, frame)
        if buf is None:
            return pos, quat
        delta = bool(flags & ANIMDESC_DELTA)
        guard = 0
        while off and guard < 512:
            guard += 1
            if off + 4 > len(buf):
                break
            bone, aflags, nextoff = struct.unpack_from("<BBh", buf, off)
            if bone >= n:
                break
            p = off + 4
            if aflags & ANIM_RAWROT:
                x, y, zw = struct.unpack_from("<HHH", buf, p)
                qx = (x - 32768) / 32768.0
                qy = (y - 32768) / 32768.0
                qz = ((zw & 0x7FFF) - 16384) / 16384.0
                qw = np.sqrt(max(0.0, 1.0 - qx * qx - qy * qy - qz * qz))
                if zw & 0x8000:
                    qw = -qw
                quat[bone] = (qx, qy, qz, qw)
            elif aflags & ANIM_RAWROT2:
                v = struct.unpack_from("<Q", buf, p)[0]
                qx = ((v & 0x1FFFFF) - 1048576) / 1048576.5
                qy = (((v >> 21) & 0x1FFFFF) - 1048576) / 1048576.5
                qz = (((v >> 42) & 0x1FFFFF) - 1048576) / 1048576.5
                qw = np.sqrt(max(0.0, 1.0 - qx * qx - qy * qy - qz * qz))
                if v >> 63:
                    qw = -qw
                quat[bone] = (qx, qy, qz, qw)
            elif aflags & ANIM_ANIMROT:
                ang = np.zeros(3)
                for j in range(3):
                    vo = struct.unpack_from("<h", buf, p + j * 2)[0]
                    if vo:
                        ang[j] = self._value(buf, p + vo, f) * bones.rotscale[bone][j]
                if not (aflags & ANIM_DELTA) and not delta:
                    ang += bones.rot[bone]
                quat[bone] = _quat_from_euler(ang)
            pp = p + (6 if aflags & (ANIM_RAWROT | ANIM_ANIMROT) else 0) + (8 if aflags & ANIM_RAWROT2 else 0)
            if aflags & ANIM_RAWPOS:
                h = struct.unpack_from("<HHH", buf, pp)
                pos[bone] = (_half(h[0]), _half(h[1]), _half(h[2]))
            elif aflags & ANIM_ANIMPOS:
                v3 = np.zeros(3)
                for j in range(3):
                    vo = struct.unpack_from("<h", buf, pp + j * 2)[0]
                    if vo:
                        v3[j] = self._value(buf, pp + vo, f) * bones.posscale[bone][j]
                if not (aflags & ANIM_DELTA) and not delta:
                    v3 += bones.pos[bone]
                pos[bone] = v3
            if nextoff == 0:
                break
            off += nextoff
        return pos, quat

    @staticmethod
    def _value(buf, p, k):
        """ExtractAnimValue: run-length encoded shorts."""
        guard = 0
        while guard < 4096:
            guard += 1
            valid, total = struct.unpack_from("<BB", buf, p)
            if total == 0:
                return 0.0
            if k < total:
                if k < valid:
                    return float(struct.unpack_from("<h", buf, p + 2 + k * 2)[0])
                return float(struct.unpack_from("<h", buf, p + 2 + (valid - 1) * 2)[0]) if valid else 0.0
            k -= total
            p += 2 + valid * 2
        return 0.0


class AnimSet:
    """The skeleton of a model and every sequence it can play (its own and of included models)."""

    def __init__(self, fs, name):
        self.ok = False
        self.error = ""
        self.sequences = []
        self.by_name = {}
        name = name.replace("\\", "/").lower()
        data = fs.read(name)
        if data is None or data[:4] != b"IDST":
            self.error = "no model"
            return
        self.base = Source(fs, name, data, None)
        self.bones = self.base.bones
        self.sources = [self.base]
        self._add_sequences(self.base)
        seen = {name}
        todo = [name]
        depth = 0
        while todo and depth < 3:
            nxt = []
            for nm in todo:
                d = fs.read(nm)
                if d is None:
                    continue
                n_inc, inc_idx = struct.unpack_from("<ii", d, 336)
                for i in range(min(n_inc, 64)):
                    b = inc_idx + i * 8
                    _lab, off = struct.unpack_from("<ii", d, b)
                    inc = _cstr(d, b + off).replace("\\", "/").lower()
                    if not inc or inc in seen:
                        continue
                    seen.add(inc)
                    idata = fs.read(inc)
                    if idata is None or idata[:4] != b"IDST":
                        continue
                    src = Source(fs, inc, idata, self.bones)
                    self.sources.append(src)
                    self._add_sequences(src)
                    nxt.append(inc)
            todo = nxt
            depth += 1
        self.ok = len(self.bones) > 0

    def _add_sequences(self, src):
        d = src.data
        numseq, seqindex = struct.unpack_from("<ii", d, 188)
        for i in range(min(numseq, 4096)):
            b = seqindex + i * 212
            lab, act, flags = struct.unpack_from("<iii", d, b + 4)
            label = _cstr(d, b + lab)
            if not label:
                continue
            old = self.by_name.get(label.lower())
            if old is not None and not (old.forward and not (flags & SEQ_OVERRIDE)):
                if not old.activity:
                    old.activity = _cstr(d, b + act)
                continue
            animindexindex = struct.unpack_from("<i", d, b + 60)[0]
            anim = struct.unpack_from("<h", d, b + animindexindex)[0] if animindexindex else 0
            if anim < 0 or anim >= len(src.animdescs):
                continue
            fps, aflags, frames = src.anim_info(anim)
            s = Sequence(label, _cstr(d, b + act), src, anim, fps if fps > 0 else 30.0, max(1, frames), bool((flags | aflags) & SEQ_LOOPING),
                         bool(flags & SEQ_OVERRIDE))
            gs0, gs1 = struct.unpack_from("<ii", d, b + 68)
            if animindexindex and gs0 > 1 and gs1 == 1:
                s.blend_anims = [struct.unpack_from("<h", d, b + animindexindex + k * 2)[0] for k in range(gs0)]
                s.blend_range = (struct.unpack_from("<f", d, b + 84)[0], struct.unpack_from("<f", d, b + 92)[0])
            if old is not None:
                # the real sequence of a forward declaration (Mapbase: male_shared -> male_shared_original)
                if not s.activity:
                    s.activity = old.activity
                self.sequences[self.sequences.index(old)] = s
            else:
                self.sequences.append(s)
            self.by_name[label.lower()] = s

    def find(self, name_or_activity):
        """A sequence by name, else the first one with that activity (ACT_IDLE...)."""
        if not name_or_activity:
            return None
        k = name_or_activity.lower()
        if k in self.by_name:
            return self.by_name[k]
        for s in self.sequences:
            if s.activity and s.activity.lower() == k:
                return s
        return None

    def pose(self, seq, t, blend=None):
        """Local (pos, quat) of the model's bones at time t (seconds) of a sequence (loops or holds the end);
        blend: the pose parameter value of a blended sequence (the seat height of the library's sitting)."""
        if seq is not None and blend is not None and seq.blend_range and len(seq.blend_anims) > 1:
            lo, hi = seq.blend_range
            x = (min(max(blend, min(lo, hi)), max(lo, hi)) - lo) / (hi - lo) if hi != lo else 0.0
            x *= len(seq.blend_anims) - 1
            i0 = min(int(x), len(seq.blend_anims) - 2)
            fr = x - i0
            p0, q0 = self._pose_anim(seq, seq.blend_anims[i0], t)
            if fr < 1e-3:
                return p0, q0
            p1, q1 = self._pose_anim(seq, seq.blend_anims[i0 + 1], t)
            return p0 + (p1 - p0) * fr, np.array([_slerp(q0[i], q1[i], fr) for i in range(len(q0))])
        return self._pose_anim(seq, seq.anim if seq is not None else 0, t)

    def _pose_anim(self, seq, anim, t):
        bones = self.bones
        pos = bones.pos.copy()
        quat = bones.quat.copy()
        if seq is None:
            return pos, quat
        frames = seq.frames
        f = t * seq.fps
        if frames > 1:
            f = f % (frames - 1) if seq.looping else min(f, frames - 1)
        else:
            f = 0.0
        f0 = int(f)
        fr = f - f0
        src = seq.src
        p0, q0 = src.sample(anim, f0)
        if fr > 1e-3 and f0 + 1 < frames:
            p1, q1 = src.sample(anim, f0 + 1)
            p0 = p0 + (p1 - p0) * fr
            q0 = np.array([_slerp(q0[i], q1[i], fr) for i in range(len(q0))])
        if src is self.base:
            return p0, q0
        # an included model: its bones by name onto ours
        for i, j in enumerate(src.remap):
            if j >= 0:
                pos[j] = p0[i]
                quat[j] = q0[i]
        return pos, quat

    def matrices(self, pos, quat):
        """Bone-to-model 3x4 matrices from local transforms (parents come first in studio models)."""
        bones = self.bones
        n = len(bones)
        rot = _quat_matrix(np.asarray(quat, dtype=np.float64))
        out = np.zeros((n, 3, 4))
        for i in range(n):
            local = np.zeros((3, 4))
            local[:, :3] = rot[i]
            local[:, 3] = pos[i]
            par = bones.parent[i]
            if 0 <= par < i:
                pm = out[par]
                out[i, :, :3] = pm[:, :3] @ local[:, :3]
                out[i, :, 3] = pm[:, :3] @ local[:, 3] + pm[:, 3]
            else:
                out[i] = local
        return out

    def skin_matrices(self, bone_to_model):
        """bone_to_model x pose_to_bone: what moves a reference-pose vertex with the bone."""
        p2b = self.bones.pose_to_bone
        out = np.empty_like(bone_to_model)
        out[:, :, :3] = bone_to_model[:, :, :3] @ p2b[:, :, :3]
        out[:, :, 3] = (bone_to_model[:, :, :3] @ p2b[:, :, 3:4])[..., 0] + bone_to_model[:, :, 3]
        return out


def skin(pos, nrm, bw, bb, skin_mats):
    """Skinned positions / normals of reference-pose vertices (n, 3) with 3 weights each."""
    n_b = len(skin_mats)
    bb = np.clip(bb, 0, max(0, n_b - 1))
    m = np.einsum("nk,nkij->nij", bw.astype(np.float64), skin_mats[bb])       # (n, 3, 4)
    p = np.einsum("nij,nj->ni", m[:, :, :3], pos.astype(np.float64)) + m[:, :, 3]
    q = np.einsum("nij,nj->ni", m[:, :, :3], nrm.astype(np.float64))
    q /= np.maximum(np.linalg.norm(q, axis=1, keepdims=True), 1e-6)
    return p.astype(np.float32), q.astype(np.float32)


def actbusy_sequences(fs):
    """actbusy.txt of the game: name -> (busy sequence or activity, entry, exit)."""
    from .. import kv
    out = {}
    for path in ("scripts/actbusy.txt",):
        data = fs.read(path)
        if data is None:
            continue
        try:
            root = kv.parse(data.decode("utf-8", "replace"))
        except Exception:
            continue
        top = root.blocks()[0] if root.blocks() else None
        for b in (top.blocks() if top else []):
            busy = b.get("busy_sequence") or b.get("busy_anim") or ""
            out[b.name.lower()] = (busy, b.get("entry_sequence") or b.get("entry_anim") or "", b.get("exit_sequence") or b.get("exit_anim") or "")
    return out
