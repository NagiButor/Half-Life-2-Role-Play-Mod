"""Rotations for the animation tools: quaternions x y z w (Source / Kimodo order), vectorized over
leading axes with numpy. Source bone eulers (RadianEuler x y z) are R = Rz(z) Ry(y) Rx(x)."""
import numpy as np


def normalize(q):
    q = np.asarray(q, dtype=np.float64)
    return q / np.maximum(np.linalg.norm(q, axis=-1, keepdims=True), 1e-12)


def mul(a, b):
    a = np.asarray(a, dtype=np.float64)
    b = np.asarray(b, dtype=np.float64)
    ax, ay, az, aw = np.moveaxis(a, -1, 0)
    bx, by, bz, bw = np.moveaxis(b, -1, 0)
    return np.stack([aw * bx + ax * bw + ay * bz - az * by,
                     aw * by - ax * bz + ay * bw + az * bx,
                     aw * bz + ax * by - ay * bx + az * bw,
                     aw * bw - ax * bx - ay * by - az * bz], axis=-1)


def conj(q):
    q = np.array(q, dtype=np.float64, copy=True)
    q[..., :3] *= -1.0
    return q


def rotate(q, v):
    """rotate vectors v (..., 3) by q (..., 4)"""
    q = np.asarray(q, dtype=np.float64)
    v = np.asarray(v, dtype=np.float64)
    u = q[..., :3]
    w = q[..., 3:4]
    t = 2.0 * np.cross(u, v)
    return v + w * t + np.cross(u, t)


def to_matrix(q):
    q = normalize(q)
    x, y, z, w = np.moveaxis(q, -1, 0)
    m = np.empty(q.shape[:-1] + (3, 3))
    m[..., 0, 0] = 1 - 2 * (y * y + z * z)
    m[..., 0, 1] = 2 * (x * y - z * w)
    m[..., 0, 2] = 2 * (x * z + y * w)
    m[..., 1, 0] = 2 * (x * y + z * w)
    m[..., 1, 1] = 1 - 2 * (x * x + z * z)
    m[..., 1, 2] = 2 * (y * z - x * w)
    m[..., 2, 0] = 2 * (x * z - y * w)
    m[..., 2, 1] = 2 * (y * z + x * w)
    m[..., 2, 2] = 1 - 2 * (x * x + y * y)
    return m


def from_matrix(m):
    m = np.asarray(m, dtype=np.float64)
    shp = m.shape[:-2]
    m = m.reshape(-1, 3, 3)
    q = np.empty((m.shape[0], 4))
    tr = m[:, 0, 0] + m[:, 1, 1] + m[:, 2, 2]
    for i in range(m.shape[0]):
        r = m[i]
        if tr[i] > 0:
            s = np.sqrt(tr[i] + 1.0) * 2
            q[i] = [(r[2, 1] - r[1, 2]) / s, (r[0, 2] - r[2, 0]) / s, (r[1, 0] - r[0, 1]) / s, 0.25 * s]
        elif r[0, 0] > r[1, 1] and r[0, 0] > r[2, 2]:
            s = np.sqrt(1.0 + r[0, 0] - r[1, 1] - r[2, 2]) * 2
            q[i] = [0.25 * s, (r[0, 1] + r[1, 0]) / s, (r[0, 2] + r[2, 0]) / s, (r[2, 1] - r[1, 2]) / s]
        elif r[1, 1] > r[2, 2]:
            s = np.sqrt(1.0 + r[1, 1] - r[0, 0] - r[2, 2]) * 2
            q[i] = [(r[0, 1] + r[1, 0]) / s, 0.25 * s, (r[1, 2] + r[2, 1]) / s, (r[0, 2] - r[2, 0]) / s]
        else:
            s = np.sqrt(1.0 + r[2, 2] - r[0, 0] - r[1, 1]) * 2
            q[i] = [(r[0, 2] + r[2, 0]) / s, (r[1, 2] + r[2, 1]) / s, 0.25 * s, (r[1, 0] - r[0, 1]) / s]
    return normalize(q).reshape(shp + (4,))


def from_axis_angle(axis, angle):
    axis = np.asarray(axis, dtype=np.float64)
    axis = axis / np.maximum(np.linalg.norm(axis, axis=-1, keepdims=True), 1e-12)
    angle = np.asarray(angle, dtype=np.float64)[..., None]
    return np.concatenate([axis * np.sin(angle / 2), np.cos(angle / 2)], axis=-1)


def between(a, b):
    """shortest rotation taking direction a to direction b"""
    a = np.asarray(a, dtype=np.float64)
    b = np.asarray(b, dtype=np.float64)
    a = a / np.maximum(np.linalg.norm(a, axis=-1, keepdims=True), 1e-12)
    b = b / np.maximum(np.linalg.norm(b, axis=-1, keepdims=True), 1e-12)
    c = np.cross(a, b)
    d = np.sum(a * b, axis=-1, keepdims=True)
    q = np.concatenate([c, 1.0 + d], axis=-1)
    # opposite directions: any perpendicular axis
    bad = (q[..., 3] < 1e-8)
    if np.any(bad):
        ax = np.cross(a[bad] if a.ndim > 1 else a, [1.0, 0, 0])
        if np.linalg.norm(ax) < 1e-6:
            ax = np.cross(a[bad] if a.ndim > 1 else a, [0, 1.0, 0])
        q[bad] = np.concatenate([ax / np.linalg.norm(ax, axis=-1, keepdims=True), np.zeros(ax.shape[:-1] + (1,))], axis=-1)
    return normalize(q)


def slerp(a, b, t):
    a = normalize(a)
    b = normalize(b)
    t = np.asarray(t, dtype=np.float64)
    d = np.sum(a * b, axis=-1, keepdims=True)
    b = np.where(d < 0, -b, b)
    d = np.abs(d)
    t = t[..., None] if t.ndim == a.ndim - 1 or t.ndim == 0 else t
    lin = d > 0.9995
    th = np.arccos(np.clip(d, -1, 1))
    s = np.sin(th)
    s = np.where(s < 1e-9, 1.0, s)
    wa = np.where(lin, 1 - t, np.sin((1 - t) * th) / s)
    wb = np.where(lin, t, np.sin(t * th) / s)
    return normalize(wa * a + wb * b)


def euler_to_quat(e):
    """Source RadianEuler (x, y, z) -> quaternion"""
    e = np.asarray(e, dtype=np.float64)
    sr, cr = np.sin(e[..., 0] * .5), np.cos(e[..., 0] * .5)
    sp, cp = np.sin(e[..., 1] * .5), np.cos(e[..., 1] * .5)
    sy, cy = np.sin(e[..., 2] * .5), np.cos(e[..., 2] * .5)
    return np.stack([sr * cp * cy - cr * sp * sy,
                     cr * sp * cy + sr * cp * sy,
                     cr * cp * sy - sr * sp * cy,
                     cr * cp * cy + sr * sp * sy], axis=-1)


def matrix_to_euler(m):
    """R = Rz(z) Ry(y) Rx(x) -> (x, y, z) radians (the SMD / RadianEuler convention)"""
    m = np.asarray(m, dtype=np.float64)
    sy = np.clip(-m[..., 2, 0], -1.0, 1.0)
    y = np.arcsin(sy)
    cy = np.cos(y)
    ok = np.abs(cy) > 1e-6
    x = np.where(ok, np.arctan2(m[..., 2, 1], m[..., 2, 2]), np.arctan2(-m[..., 1, 2], m[..., 1, 1]))
    z = np.where(ok, np.arctan2(m[..., 1, 0], m[..., 0, 0]), 0.0)
    return np.stack([x, y, z], axis=-1)


def quat_to_euler(q):
    return matrix_to_euler(to_matrix(q))


def angle_between(a, b):
    """angle of the rotation a^-1 b"""
    d = np.abs(np.sum(normalize(a) * normalize(b), axis=-1))
    return 2 * np.arccos(np.clip(d, -1, 1))


def continuous(q):
    """flip signs along the first axis so that consecutive quaternions stay on one hemisphere"""
    q = np.array(q, dtype=np.float64, copy=True)
    for t in range(1, q.shape[0]):
        d = np.sum(q[t] * q[t - 1], axis=-1)
        q[t][d < 0] *= -1
    return q
