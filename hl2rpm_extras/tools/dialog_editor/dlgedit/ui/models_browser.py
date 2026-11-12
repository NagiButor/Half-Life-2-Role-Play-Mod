"""Models of the game as a grid of thumbnails: search, folder filter, drag onto the
map window. Used as a left tab (place props) and as the "choose a model" dialog."""
import json
import os
import time

from PySide6.QtCore import QMimeData, QSize, Qt, QTimer, Signal
from PySide6.QtGui import QColor, QIcon, QPixmap
from PySide6.QtWidgets import (QComboBox, QDialog, QDialogButtonBox, QHBoxLayout, QLabel, QLineEdit, QListWidget,
                               QListWidgetItem, QVBoxLayout, QWidget)

PLACE_MIME = "application/x-hl2rpm-place"


class _Grid(QListWidget):
    def __init__(self, owner):
        super().__init__()
        self.owner = owner

    def mimeData(self, items):
        md = QMimeData()
        if items:
            payload = self.owner.payload_for(items[0].data(Qt.UserRole))
            if payload:
                md.setData(PLACE_MIME, json.dumps(payload).encode("utf-8"))
                md.setText(items[0].data(Qt.UserRole))
        return md

    def mimeTypes(self):
        return [PLACE_MIME, "text/plain"]


class ModelGrid(QWidget):
    activated = Signal(str)          # double click: the model path

    def __init__(self, ctx, names_fn, payload_fn=None, icon=96):
        super().__init__()
        self.ctx = ctx
        self.names_fn = names_fn
        self.payload_fn = payload_fn
        self.icon = icon
        self.names = None
        self._queue = []
        lay = QVBoxLayout(self)
        lay.setContentsMargins(4, 4, 4, 4)
        lay.setSpacing(4)
        top = QHBoxLayout()
        self.flt = QLineEdit()
        self.flt.setPlaceholderText("поиск: crate, chair, humans/group02…")
        self.flt.setClearButtonEnabled(True)
        top.addWidget(self.flt, 1)
        self.folder = QComboBox()
        self.folder.setMinimumContentsLength(10)
        self.folder.setSizeAdjustPolicy(QComboBox.AdjustToMinimumContentsLengthWithIcon)
        top.addWidget(self.folder)
        lay.addLayout(top)
        self.grid = _Grid(self)
        self.grid.setViewMode(QListWidget.IconMode)
        self.grid.setIconSize(QSize(icon, icon))
        self.grid.setGridSize(QSize(icon + 18, icon + 34))
        self.grid.setResizeMode(QListWidget.Adjust)
        self.grid.setMovement(QListWidget.Static)
        self.grid.setUniformItemSizes(True)
        self.grid.setWordWrap(True)
        self.grid.setDragEnabled(payload_fn is not None)
        self.grid.itemDoubleClicked.connect(lambda it: self.activated.emit(it.data(Qt.UserRole)))
        self.grid.verticalScrollBar().valueChanged.connect(lambda _: self._queue_visible())
        lay.addWidget(self.grid, 1)
        self.status = QLabel("")
        self.status.setObjectName("hint")
        lay.addWidget(self.status)
        self.timer = QTimer(self)
        self.timer.setInterval(15)
        self.timer.timeout.connect(self._work)
        self._fill_timer = QTimer(self)
        self._fill_timer.setSingleShot(True)
        self._fill_timer.setInterval(180)
        self._fill_timer.timeout.connect(self.fill)
        self.flt.textChanged.connect(lambda _: self._fill_timer.start())
        self.folder.currentIndexChanged.connect(lambda _: self.fill())
        blank = QPixmap(icon, icon)
        blank.fill(QColor(48, 52, 58))
        self._blank = QIcon(blank)

    def payload_for(self, model):
        return self.payload_fn(model) if self.payload_fn else None

    def ensure_names(self):
        if self.names is None:
            self.names = self.names_fn() or []
            folders = sorted({n.split("/")[1] for n in self.names if n.count("/") >= 2})
            self.folder.blockSignals(True)
            self.folder.clear()
            self.folder.addItem("все папки", "")
            for f in folders:
                self.folder.addItem(f, f)
            self.folder.blockSignals(False)

    def showEvent(self, e):
        super().showEvent(e)
        if self.names is None:
            QTimer.singleShot(0, self.fill)

    def set_filter(self, text):
        self.flt.setText(text)
        self.fill()

    def fill(self):
        self.ensure_names()
        t = self.flt.text().strip().lower()
        fo = self.folder.currentData() or ""
        self.grid.clear()
        n = 0
        for m in self.names:
            if fo and not m.startswith("models/%s/" % fo):
                continue
            if t and t not in m:
                continue
            it = QListWidgetItem(self._blank, os.path.basename(m)[:-4])
            it.setData(Qt.UserRole, m)
            it.setToolTip(m)
            self.grid.addItem(it)
            n += 1
            if n >= 1500:
                break
        self.status.setText("%d моделей%s" % (n, " (показаны первые 1500 — уточните поиск)" if n >= 1500 else ""))
        self._queue_visible()

    def _queue_visible(self):
        th = self.ctx.thumbnailer()
        if th is None:
            return
        vp = self.grid.viewport().rect()
        q = []
        for i in range(self.grid.count()):
            it = self.grid.item(i)
            if it.data(Qt.UserRole + 1):
                continue
            r = self.grid.visualItemRect(it)
            if r.intersects(vp.adjusted(0, -200, 0, 400)):
                q.append(it)
        self._queue = q
        if q and not self.timer.isActive():
            self.timer.start()

    def _work(self):
        th = self.ctx.thumbnailer()
        t0 = time.perf_counter()
        while self._queue and time.perf_counter() - t0 < 0.03:
            it = self._queue.pop(0)
            try:
                if it.listWidget() is None:
                    continue
            except RuntimeError:
                continue
            img = th.get(it.data(Qt.UserRole), self.icon) if th else None
            it.setData(Qt.UserRole + 1, True)
            if img is not None:
                it.setIcon(QIcon(QPixmap.fromImage(img)))
        if not self._queue:
            self.timer.stop()

    def current(self):
        it = self.grid.currentItem()
        return it.data(Qt.UserRole) if it else ""


class ModelBrowser(QDialog):
    def __init__(self, parent, ctx, names_fn, current):
        super().__init__(parent)
        self.setWindowTitle("Модель из игры")
        self.resize(900, 680)
        lay = QVBoxLayout(self)
        self.g = ModelGrid(ctx, names_fn)
        self.g.activated.connect(lambda _: self.accept())
        lay.addWidget(self.g, 1)
        bb = QDialogButtonBox(QDialogButtonBox.Ok | QDialogButtonBox.Cancel)
        bb.accepted.connect(self.accept)
        bb.rejected.connect(self.reject)
        lay.addWidget(bb)
        if current:
            self.g.flt.setText(os.path.dirname(current).replace("models/", ""))

    def value(self):
        return self.g.current()
