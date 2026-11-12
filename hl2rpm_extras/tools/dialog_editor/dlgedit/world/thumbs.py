"""Thumbnails of studio models for the object window: the model's triangles painted
back to front with QPainter (flat shading, the average color of each material),
cached as PNG under %LOCALAPPDATA%."""
import hashlib
import math
import os
import struct

import numpy as np
from PySide6.QtCore import QPointF, Qt
from PySide6.QtGui import QColor, QImage, QPainter, QPolygonF

from . import mdl as mdlmod

CACHE_DIR = os.path.join(os.environ.get("LOCALAPPDATA", os.path.expanduser("~")), "HL2RPM Creation Kit", "thumbs")
CACHE_VERSION = 2


def _dxt_color(level, kind):
    """Average color of the smallest DXT mip (its first 4x4 block): the mean of the two endpoints."""
    w, h, blob = level
    if len(blob) < 8:
        return None
    off = 8 if kind in ("dxt3", "dxt5") else 0
    if len(blob) < off + 4:
        return None
    c0, c1 = struct.unpack_from("<HH", blob, off)

    def rgb(c):
        return (((c >> 11) & 31) / 31.0, ((c >> 5) & 63) / 63.0, (c & 31) / 31.0)
    a, b = rgb(c0), rgb(c1)
    return ((a[0] + b[0]) / 2, (a[1] + b[1]) / 2, (a[2] + b[2]) / 2)


class Thumbnailer:
    def __init__(self, fs, matsys):
        self.fs = fs
        self.ms = matsys
        self._mat_colors = {}
        self._mem = {}
        os.makedirs(CACHE_DIR, exist_ok=True)

    def material_color(self, name):
        c = self._mat_colors.get(name)
        if c is not None:
            return c
        c = (0.62, 0.62, 0.62)
        try:
            info = self.ms.info(name)
            t = self.ms.texture(info.base) if info.base else None
            if t is not None:
                if t.kind == "rgba":
                    a = np.asarray(t.data, np.float32).reshape(-1, 4)
                    c = tuple((a[:, :3].mean(0) / 255.0).tolist())
                elif t.data:
                    c = _dxt_color(t.data[-1], t.kind) or c
            tint = info.tint
            c = (c[0] * tint[0], c[1] * tint[1], c[2] * tint[2])
        except Exception:
            pass
        self._mat_colors[name] = c
        return c

    def path(self, model):
        h = hashlib.md5(("%d:%s" % (CACHE_VERSION, model.lower())).encode()).hexdigest()
        return os.path.join(CACHE_DIR, h + ".png")

    def get(self, model, size=112):
        """QImage of the model (cached), or None if the model can't be read."""
        model = model.replace("\\", "/").lower()
        img = self._mem.get(model)
        if img is not None:
            return img
        p = self.path(model)
        if os.path.isfile(p):
            img = QImage(p)
            if not img.isNull():
                self._mem[model] = img
                return img
        m = mdlmod.load(self.fs, model)
        if not m.ok:
            return None
        img = self.render(m, size)
        try:
            img.save(p)
        except OSError:
            pass
        self._mem[model] = img
        return img

    def render(self, m, size=112, yaw=135.0, pitch=24.0):
        img = QImage(size, size, QImage.Format_ARGB32_Premultiplied)
        img.fill(QColor(0, 0, 0, 0))
        tris, cols = [], []
        for mesh in m.meshes:
            t = mesh.pos[mesh.idx.reshape(-1)].reshape(-1, 3, 3)
            if len(t) == 0:
                continue
            tris.append(t)
            cols.append(np.repeat([self.material_color(m.material_for(mesh.material, 0))], len(t), axis=0))
        if not tris:
            return img
        t = np.concatenate(tris).astype(np.float64)
        c = np.concatenate(cols)
        if len(t) > 12000:                      # very dense models: every n-th triangle is enough at 112 px
            k = int(math.ceil(len(t) / 12000))
            t, c = t[::k], c[::k]
        # the camera sits in direction d (yaw, pitch above the horizon) from the model:
        # rows = screen right, screen up, towards the viewer
        cy, sy = math.cos(math.radians(yaw)), math.sin(math.radians(yaw))
        cp, sp = math.cos(math.radians(pitch)), math.sin(math.radians(pitch))
        R = np.array([[-sy, cy, 0.0], [-sp * cy, -sp * sy, cp], [cp * cy, cp * sy, sp]])
        v = t.reshape(-1, 3) @ R.T
        lo, hi = v.min(0), v.max(0)
        ext = max(hi[0] - lo[0], hi[1] - lo[1], 1e-3)
        s = (size - 10) / ext
        cx, cyy = (lo[0] + hi[0]) / 2, (lo[1] + hi[1]) / 2
        sx = (v[:, 0] - cx) * s + size / 2
        syy = size / 2 - (v[:, 1] - cyy) * s
        v = v.reshape(-1, 3, 3)
        sx = sx.reshape(-1, 3)
        syy = syy.reshape(-1, 3)
        n = np.cross(v[:, 1] - v[:, 0], v[:, 2] - v[:, 0])
        ln = np.linalg.norm(n, axis=1)
        ok = ln > 1e-9
        n[ok] /= ln[ok, None]
        L = np.array([0.35, 0.55, 0.76])
        shade = 0.38 + 0.62 * np.abs(n @ L)
        order = np.argsort(v[:, :, 2].mean(1))      # far first
        qp = QPainter(img)
        qp.setRenderHint(QPainter.Antialiasing)
        qp.setPen(Qt.NoPen)
        for i in order:
            if not ok[i]:
                continue
            col = c[i] * shade[i]
            qp.setBrush(QColor.fromRgbF(min(1.0, col[0]), min(1.0, col[1]), min(1.0, col[2])))
            qp.drawPolygon(QPolygonF([QPointF(sx[i, 0], syy[i, 0]), QPointF(sx[i, 1], syy[i, 1]), QPointF(sx[i, 2], syy[i, 2])]))
        qp.end()
        return img
