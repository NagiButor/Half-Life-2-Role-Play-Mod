"""Transform gizmo of the render window (like Hammer++): arrows and plane squares to
move along an axis / in a plane, rings to turn about an axis. Drawn as triangles on top
of everything with a constant size on screen; hit tests and drags work in screen space
against the mouse ray."""
import math

import numpy as np

AXES = np.eye(3)
COLORS = {0: (0.95, 0.32, 0.30, 1.0), 1: (0.40, 0.88, 0.38, 1.0), 2: (0.36, 0.56, 1.0, 1.0)}
HOT = (1.0, 0.86, 0.25, 1.0)
PLANES = {"xy": (0, 1), "xz": (0, 2), "yz": (1, 2)}
PLANE_NORMAL = {"xy": 2, "xz": 1, "yz": 0}


class Gizmo:
    def __init__(self):
        self.mode = "move"            # move / rotate
        self.center = None            # np.array(3) or None (nothing selected)
        self.rot_axes = (0, 1, 2)     # rings shown in rotate mode (NPCs / markers: only z)
        self.hot = None               # handle under the mouse
        self.active = None            # handle being dragged
        self.size = 64.0
        self._start = None

    # --- geometry
    def update_size(self, cam_pos, fov):
        if self.center is None:
            return
        d = float(np.linalg.norm(self.center - cam_pos))
        self.size = max(4.0, d * math.tan(math.radians(fov) / 2) * 0.24)

    def triangles(self, cam_pos):
        """[(x, y, z, r, g, b, a)] triangles of the gizmo."""
        out = []
        if self.center is None:
            return out
        c = self.center
        s = self.size
        view = c - cam_pos
        view /= max(1e-6, np.linalg.norm(view))
        if self.mode == "move":
            for ax in range(3):
                col = HOT if self.hot == ax or self.active == ax else COLORS[ax]
                a = AXES[ax]
                side = np.cross(a, view)
                if np.linalg.norm(side) < 1e-4:
                    side = np.cross(a, [0.3, 0.5, 0.8])
                side = side / np.linalg.norm(side) * s * 0.022
                p0 = c + a * s * 0.12
                p1 = c + a * s * 0.8
                _quad(out, p0 - side, p0 + side, p1 + side, p1 - side, col)
                # cone head
                u = np.cross(a, side)
                u = u / max(1e-6, np.linalg.norm(u))
                v = np.cross(a, u)
                tip = c + a * s
                rad = s * 0.075
                seg = 10
                for k in range(seg):
                    a0 = 2 * math.pi * k / seg
                    a1 = 2 * math.pi * (k + 1) / seg
                    q0 = p1 + (u * math.cos(a0) + v * math.sin(a0)) * rad
                    q1 = p1 + (u * math.cos(a1) + v * math.sin(a1)) * rad
                    _tri(out, tip, q0, q1, col)
                    _tri(out, p1, q1, q0, _dim(col))
            for name, (i, j) in PLANES.items():
                col = HOT if self.hot == name or self.active == name else _mix(COLORS[i], COLORS[j])
                col = (col[0], col[1], col[2], 0.55 if col is not HOT else 0.8)
                a, b = AXES[i] * s, AXES[j] * s
                p = c + a * 0.25 + b * 0.25
                _quad(out, p, p + a * 0.18, p + a * 0.18 + b * 0.18, p + b * 0.18, col)
        else:
            for ax in self.rot_axes:
                col = HOT if self.hot == ("r", ax) or self.active == ("r", ax) else COLORS[ax]
                u, v = self._ring_basis(ax)
                r0, r1 = s * 0.78, s * 0.84
                seg = 72
                for k in range(seg):
                    a0 = 2 * math.pi * k / seg
                    a1 = 2 * math.pi * (k + 1) / seg
                    d0 = u * math.cos(a0) + v * math.sin(a0)
                    d1 = u * math.cos(a1) + v * math.sin(a1)
                    _quad(out, c + d0 * r0, c + d0 * r1, c + d1 * r1, c + d1 * r0, col)
        return out

    def _ring_basis(self, ax):
        others = [i for i in range(3) if i != ax]
        return AXES[others[0]], AXES[others[1]]

    # --- picking
    def hit(self, project, x, y):
        """The handle under the mouse (screen px), or None. project: world -> (QPointF, w) or None."""
        if self.center is None:
            return None
        c = self.center
        s = self.size
        best = None

        def scr(p):
            r = project(p)
            return None if r is None else np.array([r[0].x(), r[0].y()])
        m = np.array([x, y], float)
        if self.mode == "move":
            for name, (i, j) in PLANES.items():
                a, b = AXES[i] * s, AXES[j] * s
                p = c + a * 0.25 + b * 0.25
                poly = [scr(q) for q in (p, p + a * 0.18, p + a * 0.18 + b * 0.18, p + b * 0.18)]
                if all(q is not None for q in poly) and _in_poly(m, poly):
                    return name
            for ax in range(3):
                p0 = scr(c + AXES[ax] * s * 0.1)
                p1 = scr(c + AXES[ax] * s)
                if p0 is None or p1 is None:
                    continue
                d = _seg_dist(m, p0, p1)
                if d < 9 and (best is None or d < best[0]):
                    best = (d, ax)
        else:
            for ax in self.rot_axes:
                u, v = self._ring_basis(ax)
                pts = [scr(c + (u * math.cos(2 * math.pi * k / 48) + v * math.sin(2 * math.pi * k / 48)) * s * 0.81) for k in range(49)]
                for k in range(48):
                    if pts[k] is None or pts[k + 1] is None:
                        continue
                    d = _seg_dist(m, pts[k], pts[k + 1])
                    if d < 8 and (best is None or d < best[0]):
                        best = (d, ("r", ax))
        return best[1] if best else None

    # --- dragging
    def begin(self, handle, ray):
        self.active = handle
        self._start = self._param(handle, ray)
        return self._start is not None

    def _param(self, handle, ray):
        o, d = ray
        c = self.center
        if isinstance(handle, int):
            # parameter t of the axis point c + a*t closest to the mouse ray o + d*s
            a = AXES[handle]
            w0 = c - o
            b = d @ a
            den = 1.0 - b * b
            if den < 1e-5:
                return None
            return (b * (d @ w0) - (a @ w0)) / den
        if isinstance(handle, str):
            n = AXES[PLANE_NORMAL[handle]]
            den = d @ n
            if abs(den) < 1e-5:
                return None
            t = ((c - o) @ n) / den
            return o + d * t
        if isinstance(handle, tuple):
            ax = handle[1]
            n = AXES[ax]
            den = d @ n
            if abs(den) < 1e-5:
                return None
            t = ((c - o) @ n) / den
            p = o + d * t - c
            u, v = self._ring_basis(ax)
            return math.degrees(math.atan2(p @ v, p @ u))
        return None

    def drag(self, ray):
        """Current offset from the drag start: ("move", vec3) or ("rotate", axis, degrees) or None."""
        if self.active is None or self._start is None:
            return None
        cur = self._param(self.active, ray)
        if cur is None:
            return None
        h = self.active
        if isinstance(h, int):
            return ("move", AXES[h] * (cur - self._start))
        if isinstance(h, str):
            return ("move", cur - self._start)
        deg = cur - self._start
        deg = (deg + 180.0) % 360.0 - 180.0
        # rings: positive = counter-clockwise looking down the axis (right-handed)
        return ("rotate", h[1], deg)

    def end(self):
        self.active = None
        self._start = None


def _tri(out, a, b, c, col):
    out.append((float(a[0]), float(a[1]), float(a[2])) + tuple(col))
    out.append((float(b[0]), float(b[1]), float(b[2])) + tuple(col))
    out.append((float(c[0]), float(c[1]), float(c[2])) + tuple(col))


def _quad(out, a, b, c, d, col):
    _tri(out, a, b, c, col)
    _tri(out, a, c, d, col)


def _dim(col):
    return (col[0] * 0.6, col[1] * 0.6, col[2] * 0.6, col[3])


def _mix(a, b):
    return ((a[0] + b[0]) / 2, (a[1] + b[1]) / 2, (a[2] + b[2]) / 2, 1.0)


def _seg_dist(p, a, b):
    ab = b - a
    ln = ab @ ab
    t = 0.0 if ln < 1e-9 else max(0.0, min(1.0, ((p - a) @ ab) / ln))
    return float(np.linalg.norm(p - (a + ab * t)))


def _in_poly(p, poly):
    inside = False
    n = len(poly)
    for i in range(n):
        a, b = poly[i], poly[(i + 1) % n]
        if (a[1] > p[1]) != (b[1] > p[1]):
            x = a[0] + (p[1] - a[1]) * (b[0] - a[0]) / (b[1] - a[1])
            if p[0] < x:
                inside = not inside
    return inside
