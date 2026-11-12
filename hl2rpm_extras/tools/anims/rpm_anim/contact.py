"""Does the body go through the furniture? Vertices of the skinned body tested against the
furniture's triangles: a point is inside if rays from it cross the mesh an odd number of times
(two directions must agree - furniture meshes are not always perfectly closed)."""
import numpy as np


def _crossings(P, tris, d):
    """number of crossings of rays from points P (N,3) along unit d with triangles (M,3,3)
    (Moller-Trumbore, vectorized over triangles in chunks of points)"""
    v0, v1, v2 = tris[:, 0], tris[:, 1], tris[:, 2]
    e1, e2 = v1 - v0, v2 - v0
    h = np.cross(d, e2)                       # (M,3)
    a = np.einsum("mi,mi->m", e1, h)
    ok = np.abs(a) > 1e-9
    e1, e2, v0, h, a = e1[ok], e2[ok], v0[ok], h[ok], a[ok]
    f = 1.0 / a
    out = np.zeros(len(P), dtype=np.int32)
    for s in range(0, len(P), 400):
        p = P[s:s + 400]
        sv = p[:, None, :] - v0[None]          # (n,M,3)
        u = f[None] * np.einsum("nmi,mi->nm", sv, h)
        q = np.cross(sv, e1[None])
        v = f[None] * np.einsum("i,nmi->nm", d, q)
        t = f[None] * np.einsum("mi,nmi->nm", e2, q)
        hit = (u >= 0) & (u <= 1) & (v >= 0) & (u + v <= 1) & (t > 1e-4)
        out[s:s + 400] = hit.sum(1)
    return out


def inside(P, tris):
    """bool (N,) - points inside the (closed) furniture mesh"""
    if not len(tris):
        return np.zeros(len(P), bool)
    mn, mx = tris.reshape(-1, 3).min(0) - 0.01, tris.reshape(-1, 3).max(0) + 0.01
    box = np.all((P >= mn) & (P <= mx), axis=1)
    res = np.zeros(len(P), bool)
    if not box.any():
        return res
    Q = P[box]
    a = _crossings(Q, tris, np.array([0.0, 0.0, 1.0])) % 2 == 1
    b = _crossings(Q, tris, np.array([0.577, 0.577, 0.577])) % 2 == 1
    res[box] = a & b
    return res


def _point_tri_dist(p, a, b, c):
    """distance from points p (N,3) to triangles (M,3) each - returns (N,M)"""
    ab, ac = b - a, c - a
    ap = p[:, None, :] - a[None]
    d1 = np.einsum("nmi,mi->nm", ap, ab)
    d2 = np.einsum("nmi,mi->nm", ap, ac)
    n = np.cross(ab, ac)
    nn = np.linalg.norm(n, axis=1)
    n = n / np.maximum(nn, 1e-12)[:, None]
    # barycentric of the projection
    abab = np.einsum("mi,mi->m", ab, ab)
    abac = np.einsum("mi,mi->m", ab, ac)
    acac = np.einsum("mi,mi->m", ac, ac)
    den = abab * acac - abac * abac
    den = np.where(np.abs(den) < 1e-12, 1e-12, den)
    v = (acac * d1 - abac * d2) / den
    w = (abab * d2 - abac * d1) / den
    inside_t = (v >= 0) & (w >= 0) & (v + w <= 1)
    plane = np.abs(np.einsum("nmi,mi->nm", ap, n))
    # edges / corners otherwise
    def seg(p0, p1):
        d = p1 - p0
        t = np.clip(np.einsum("nmi,mi->nm", p[:, None, :] - p0[None], d) / np.maximum(np.einsum("mi,mi->m", d, d), 1e-12), 0, 1)
        q = p0[None] + t[..., None] * d[None]
        return np.linalg.norm(p[:, None, :] - q, axis=2)
    e = np.minimum(np.minimum(seg(a, b), seg(b, c)), seg(c, a))
    return np.where(inside_t, plane, e)


def depth(P, tris):
    """for points inside: the distance to the nearest triangle (how deep they are)"""
    out = np.zeros(len(P))
    a, b, c = tris[:, 0], tris[:, 1], tris[:, 2]
    for s in range(0, len(P), 64):
        out[s:s + 64] = _point_tri_dist(P[s:s + 64], a, b, c).min(1)
    return out


def report(body, clip, tris, frames=None, label=""):
    """per frame: how many body vertices are inside the furniture and the worst depth"""
    wp, wq = clip.world()
    idx = frames if frames is not None else range(0, clip.frames, max(1, clip.frames // 15))
    rows = []
    for t in idx:
        v = body.skin(wp[t], wq[t])
        ins = inside(v, tris)
        dmax = float(depth(v[ins], tris).max()) if ins.any() else 0.0
        floor = float(np.clip(-v[:, 2], 0, None).max())
        rows.append((int(t), int(ins.sum()), dmax, floor))
    worst = max(rows, key=lambda r: (r[2], r[1]))
    print("%s: worst frame %d - %d vertices inside, ~%.1f deep; floor %.1f" % (label or clip.name, *worst))
    if worst[1]:
        t = worst[0]
        v = body.skin(wp[t], wq[t])
        ins = inside(v, tris)
        bones = {}
        model_bone_names = [clip.rig.names[j] for j in body.map]
        for b in body.B[ins, 0]:
            nm = model_bone_names[min(int(b), len(model_bone_names) - 1)].replace("ValveBiped.Bip01_", "")
            bones[nm] = bones.get(nm, 0) + 1
        print("    inside by bone:", ", ".join("%s %d" % kv for kv in sorted(bones.items(), key=lambda kv: -kv[1])))
        print("    their z %.1f..%.1f, x %.1f..%.1f" % (v[ins, 2].min(), v[ins, 2].max(), v[ins, 0].min(), v[ins, 0].max()))
    return rows
