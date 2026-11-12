"""The model viewer (M7): one studio model with its animations - every sequence of the model and of
the models it includes ($includemodel: male_shared, *_ss, gestures, postures), played on the
skeleton (dlgedit/world/anim.py), orbit camera, frame slider, speed, bones, skins.
Opened from the NPC page, the map (context menu of NPCs and models) and markers (their animation)."""
import math
import time

import numpy as np
from PySide6.QtCore import Qt, QTimer
from PySide6.QtOpenGLWidgets import QOpenGLWidget
from PySide6.QtWidgets import (QCheckBox, QComboBox, QDialog, QHBoxLayout, QLabel, QLineEdit, QListWidget, QListWidgetItem,
                               QPushButton, QSlider, QSpinBox, QSplitter, QVBoxLayout, QWidget)

from ..world import anim as animmod
from ..world import mdl as mdlmod
from . import render_gl as rgl
from .render_gl import GL
from .worldview import perspective


class ModelGL(QOpenGLWidget):
    def __init__(self, fs, matsys, model_name, skin=0):
        super().__init__()
        self.fs, self.matsys = fs, matsys
        self.name = model_name
        self.skin = skin
        self.model = mdlmod.load(fs, model_name)
        self.anims = animmod.AnimSet(fs, model_name)
        self.seq = None
        self.t = 0.0
        self.playing = True
        self.speed = 1.0
        self.show_bones = False
        self.yaw, self.pitch = 200.0, 12.0
        lo, hi = (self.model.mins, self.model.maxs) if self.model.ok else (np.array([-16, -16, 0]), np.array([16, 16, 72]))
        self.center = (np.asarray(lo) + np.asarray(hi)) * 0.5
        # (no floor of a person's size: a key is 5 units long)
        self.dist = max(5.0, float(np.linalg.norm(np.asarray(hi) - np.asarray(lo))) * 1.15)
        self.gl_ready = False
        self.error = ""
        self._drag = None
        self._last = time.perf_counter()
        self.timer = QTimer(self)
        self.timer.setInterval(33)
        self.timer.timeout.connect(self._tick)
        self.timer.start()
        self.setMinimumSize(420, 360)
        self.frame_changed = None       # callback(frame, frames)

    def set_sequence(self, seq):
        self.seq = seq
        self.t = 0.0
        self.update()

    def frame(self):
        if self.seq is None:
            return 0, 1
        f = self.t * self.seq.fps
        n = self.seq.frames
        if n > 1:
            f = f % (n - 1) if self.seq.looping else min(f, n - 1)
        return int(f), n

    def set_frame(self, f):
        if self.seq is not None:
            self.t = f / max(self.seq.fps, 1e-3)
            self.update()

    def _tick(self):
        now = time.perf_counter()
        dt = min(0.1, now - self._last)
        self._last = now
        if self.playing and self.seq is not None:
            self.t += dt * self.speed
            if not self.seq.looping and self.t * self.seq.fps >= self.seq.frames - 1:
                self.t = 0.0 if self.seq.frames > 1 else self.t     # non-looping: restart for the preview
            if self.frame_changed:
                self.frame_changed(*self.frame())
            self.update()

    # ------------------------------------------------------------------ GL
    def initializeGL(self):
        try:
            self.prog = rgl.Program(rgl.VS_MESH, rgl.FS_MESH)
            self.lprog = rgl.Program(rgl.VS_LINE, rgl.FS_LINE)
            self.lines = rgl.LineBuffer()
            self.matcache = rgl.MaterialCache(self.matsys)
            self.mesh = rgl.ModelMesh(self.model) if self.model.ok else None
            self.skinned = rgl.SkinnedMesh(self.mesh) if self.mesh is not None else None
            self.gl_ready = True
        except Exception as e:
            self.error = str(e)

    def vp(self):
        w, h = max(1, self.width()), max(1, self.height())
        y, p = math.radians(self.yaw), math.radians(self.pitch)
        f = np.array([math.cos(p) * math.cos(y), math.cos(p) * math.sin(y), math.sin(p)])
        eye = self.center - f * self.dist
        r = np.array([math.sin(y), -math.cos(y), 0.0])
        u = np.cross(r, f)
        v = np.eye(4, dtype=np.float32)
        v[0, :3], v[1, :3], v[2, :3] = r, u, -f
        v[0, 3], v[1, 3], v[2, 3] = -r @ eye, -u @ eye, f @ eye
        return (perspective(50.0, w / h, max(0.2, min(2.0, self.dist * 0.05)), 8000.0) @ v).astype(np.float32), eye

    def paintGL(self):
        GL.glClearColor(0.16, 0.18, 0.21, 1.0)
        GL.glClear(GL.GL_COLOR_BUFFER_BIT | GL.GL_DEPTH_BUFFER_BIT)
        if not self.gl_ready or self.skinned is None:
            return
        vp, eye = self.vp()
        mats = None
        if self.anims.ok:
            pos, quat = self.anims.pose(self.seq, self.t)
            b2m = self.anims.matrices(pos, quat)
            mats = self.anims.skin_matrices(b2m)
            p, n = animmod.skin(self.skinned.pos, self.skinned.nrm, self.skinned.bw, self.skinned.bb, mats)
            self.skinned.update(p, n)
        GL.glEnable(GL.GL_DEPTH_TEST)
        GL.glDepthFunc(GL.GL_LEQUAL)
        GL.glDisable(GL.GL_CULL_FACE)
        pr = self.prog
        pr.use()
        pr.mat4("uVP", vp)
        pr.mat4("uModel", np.eye(4, dtype=np.float32))
        pr.v3("uCam", eye)
        pr.f("uFog", 100000.0)
        pr.i("uTex", 0)
        pr.i("uTex2", 1)
        pr.i("uUnlit", 0)
        pr.i("uInstanced", 0)
        pr.v3("uHighlight", (0, 0, 0))
        pr.i("uLightMode", 0)
        for k in range(4):
            pr.i("uShadow%d" % k, 2 + k)
        self.matcache.process(0.05)
        GL.glBindVertexArray(self.skinned.vao)
        late = []
        for skinref, first, count in self.skinned.parts:
            m = self.matcache.get(self.model.material_for(skinref, self.skin))
            if m.translucent:
                late.append((m, first, count))
                continue
            self._bind(m)
            GL.glDrawElements(GL.GL_TRIANGLES, count, GL.GL_UNSIGNED_INT, rgl.ctypes.c_void_p(first * 4))
        if late:
            GL.glEnable(GL.GL_BLEND)
            GL.glBlendFunc(GL.GL_SRC_ALPHA, GL.GL_ONE_MINUS_SRC_ALPHA)
            for m, first, count in late:
                self._bind(m)
                GL.glDrawElements(GL.GL_TRIANGLES, count, GL.GL_UNSIGNED_INT, rgl.ctypes.c_void_p(first * 4))
            GL.glDisable(GL.GL_BLEND)
        # floor grid and bones
        lines = []
        col = (0.45, 0.5, 0.56, 1.0)
        st = 16.0 if self.dist >= 40.0 else 1.0         # (a small model: a grid of single units)
        for k in range(-4, 5):
            lines += [(k * st, -4 * st, 0.0) + col, (k * st, 4 * st, 0.0) + col, (-4 * st, k * st, 0.0) + col, (4 * st, k * st, 0.0) + col]
        if self.show_bones and mats is not None:
            b2m = self.anims.matrices(*self.anims.pose(self.seq, self.t))
            bc = (1.0, 0.75, 0.2, 1.0)
            for i, par in enumerate(self.anims.bones.parent):
                if par >= 0:
                    lines += [tuple(b2m[par][:, 3]) + bc, tuple(b2m[i][:, 3]) + bc]
        GL.glDisable(GL.GL_DEPTH_TEST) if self.show_bones else None
        self.lprog.use()
        self.lprog.mat4("uVP", vp)
        self.lprog.f("uPointSize", 1.0)
        self.lines.upload(np.array(lines, dtype=np.float32))
        self.lines.draw(GL.GL_LINES)
        GL.glBindVertexArray(0)
        if self.matcache.queue:
            QTimer.singleShot(0, self.update)

    def _bind(self, m):
        p = self.prog
        GL.glActiveTexture(GL.GL_TEXTURE0)
        GL.glBindTexture(GL.GL_TEXTURE_2D, m.tex)
        p.i("uHasTex", 1 if m.tex else 0)
        p.i("uBlend", 0)
        p.i("uAlphaTest", 1 if m.alphatest else 0)
        p.v4("uTint", m.tint)

    # ------------------------------------------------------------------ mouse
    def mousePressEvent(self, e):
        self._drag = (e.position().x(), e.position().y(), self.yaw, self.pitch, e.button(), self.center.copy())

    def mouseMoveEvent(self, e):
        if not self._drag:
            return
        x0, y0, yaw0, pitch0, btn, c0 = self._drag
        dx, dy = e.position().x() - x0, e.position().y() - y0
        if btn == Qt.RightButton or btn == Qt.MiddleButton:
            y = math.radians(self.yaw)
            r = np.array([math.sin(y), -math.cos(y), 0.0])
            self.center = c0 - r * dx * self.dist * 0.002 + np.array([0, 0, 1.0]) * dy * self.dist * 0.002
        else:
            self.yaw = yaw0 - dx * 0.5
            self.pitch = max(-85.0, min(85.0, pitch0 + dy * 0.4))
        self.update()

    def mouseReleaseEvent(self, e):
        self._drag = None

    def wheelEvent(self, e):
        self.dist = max(2.0, min(4000.0, self.dist * (0.88 if e.angleDelta().y() > 0 else 1.14)))
        self.update()

    def free(self):
        self.timer.stop()
        if self.gl_ready:
            self.makeCurrent()
            if self.skinned is not None:
                self.skinned.delete()
            if self.mesh is not None:
                self.mesh.delete()
            self.matcache.delete()
            self.lines.delete()
            self.doneCurrent()
            self.gl_ready = False


class ModelViewer(QDialog):
    def __init__(self, parent, fs, matsys, model_name, skin=0, sequence="", title=""):
        super().__init__(parent)
        self.setWindowTitle(title or ("Просмотр модели — " + model_name))
        self.resize(1100, 720)
        lay = QHBoxLayout(self)
        split = QSplitter()
        lay.addWidget(split)
        left = QWidget()
        ll = QVBoxLayout(left)
        ll.setContentsMargins(0, 0, 0, 0)
        self.filter = QLineEdit()
        self.filter.setPlaceholderText("поиск: имя или ACT_…")
        self.filter.textChanged.connect(self.fill)
        ll.addWidget(self.filter)
        self.only_act = QCheckBox("только с активностью")
        self.only_act.toggled.connect(self.fill)
        ll.addWidget(self.only_act)
        self.list = QListWidget()
        self.list.currentItemChanged.connect(self._pick)
        ll.addWidget(self.list, 1)
        self.count = QLabel()
        self.count.setObjectName("hint")
        ll.addWidget(self.count)
        split.addWidget(left)
        right = QWidget()
        rl = QVBoxLayout(right)
        rl.setContentsMargins(0, 0, 0, 0)
        self.view = ModelGL(fs, matsys, model_name, skin)
        rl.addWidget(self.view, 1)
        row = QHBoxLayout()
        self.play = QPushButton("❚❚")
        self.play.setFixedWidth(40)
        self.play.clicked.connect(self._toggle)
        row.addWidget(self.play)
        self.slider = QSlider(Qt.Horizontal)
        self.slider.valueChanged.connect(self._scrub)
        row.addWidget(self.slider, 1)
        self.flabel = QLabel("0 / 0")
        self.flabel.setMinimumWidth(80)
        row.addWidget(self.flabel)
        sp = QComboBox()
        for v in ("0.25", "0.5", "1", "2"):
            sp.addItem("×" + v, float(v))
        sp.setCurrentIndex(2)
        sp.currentIndexChanged.connect(lambda _: setattr(self.view, "speed", sp.currentData()))
        row.addWidget(sp)
        rl.addLayout(row)
        row2 = QHBoxLayout()
        bones = QCheckBox("кости")
        bones.toggled.connect(lambda v: (setattr(self.view, "show_bones", bool(v)), self.view.update()))
        row2.addWidget(bones)
        row2.addWidget(QLabel("скин:"))
        sk = QSpinBox()
        sk.setRange(0, max(0, len(self.view.model.skins) - 1))
        sk.setValue(skin)
        sk.valueChanged.connect(lambda v: (setattr(self.view, "skin", v), self.view.update()))
        row2.addWidget(sk)
        self.info = QLabel()
        self.info.setObjectName("hint")
        row2.addWidget(self.info, 1)
        rl.addLayout(row2)
        hint = QLabel("ЛКМ — вращать, ПКМ — двигать, колесо — ближе / дальше.")
        hint.setObjectName("hint")
        rl.addWidget(hint)
        split.addWidget(right)
        split.setSizes([300, 800])
        self.view.frame_changed = self._frame
        self._scrubbing = False
        a = self.view.anims
        if not self.view.model.ok:
            self.info.setText("Модель не читается: " + (self.view.model.error or model_name))
        elif not a.ok:
            self.info.setText("У модели нет костей: " + a.error)
        else:
            self.info.setText("костей %d, анимаций %d (из %d файлов: %s)" % (
                len(a.bones), len(a.sequences), len(a.sources), ", ".join(s.name.rsplit("/", 1)[-1] for s in a.sources)))
        self.fill()
        self.select(sequence or "ACT_IDLE")

    def fill(self):
        a = self.view.anims
        self.list.blockSignals(True)
        cur = self.view.seq
        self.list.clear()
        flt = self.filter.text().strip().lower()
        n = 0
        for s in (a.sequences if a.ok else []):
            if self.only_act.isChecked() and not s.activity:
                continue
            label = s.name + ("   " + s.activity if s.activity else "")
            if flt and flt not in label.lower():
                continue
            it = QListWidgetItem(label)
            it.setData(Qt.UserRole, s.name)
            it.setToolTip("%s · %d кадров, %g к/с%s" % (s.src.name, s.frames, s.fps, ", зациклена" if s.looping else ""))
            self.list.addItem(it)
            if cur is not None and s is cur:
                self.list.setCurrentItem(it)
            n += 1
        self.list.blockSignals(False)
        self.count.setText("показано %d" % n)

    def select(self, name):
        s = self.view.anims.find(name) if self.view.anims.ok else None
        if s is None:
            return False
        for i in range(self.list.count()):
            if self.list.item(i).data(Qt.UserRole) == s.name:
                self.list.setCurrentRow(i)
                return True
        self.view.set_sequence(s)
        return True

    def _pick(self, it, _prev=None):
        if it is None:
            return
        s = self.view.anims.find(it.data(Qt.UserRole))
        self.view.set_sequence(s)
        if s is not None:
            self.slider.blockSignals(True)
            self.slider.setRange(0, max(0, s.frames - 1))
            self.slider.blockSignals(False)
            self._frame(0, s.frames)

    def _toggle(self):
        self.view.playing = not self.view.playing
        self.play.setText("❚❚" if self.view.playing else "▶")

    def _scrub(self, v):
        if self._scrubbing:
            return
        self.view.playing = False
        self.play.setText("▶")
        self.view.set_frame(v)
        self.flabel.setText("%d / %d" % (v, self.slider.maximum()))

    def _frame(self, f, n):
        self._scrubbing = True
        self.slider.setValue(f)
        self._scrubbing = False
        self.flabel.setText("%d / %d" % (f, max(0, n - 1)))

    def done(self, r):
        self.view.free()
        super().done(r)


def open_viewer(ctx, model_name, skin=0, sequence="", title=""):
    """Opens the viewer (needs the game files of the project: Project → Settings)."""
    fs = getattr(ctx, "fs", None)
    ms = getattr(ctx, "matsys", None)
    if fs is None or ms is None:
        from PySide6.QtWidgets import QMessageBox
        QMessageBox.information(ctx, "Просмотр модели", "Не найдены файлы игры (Проект → Настройки: папка мода).")
        return None
    dlg = ModelViewer(ctx, fs, ms, model_name, skin, sequence, title)
    dlg.setAttribute(Qt.WA_DeleteOnClose)
    dlg.show()
    return dlg
