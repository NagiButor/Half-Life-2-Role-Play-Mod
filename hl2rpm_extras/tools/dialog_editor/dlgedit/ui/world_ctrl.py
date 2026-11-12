"""The world part of the main window: the map panel (render window + its toolbar),
the object window (what can be placed), the cell view (what is on the map),
factions, placing / moving / deleting, AI nodes, running the game at the camera."""
import glob
import json
import math
import os
import shutil
import subprocess
import time

from PySide6.QtCore import QMimeData, QProcess, Qt, QTimer
from PySide6.QtGui import QColor, QKeySequence, QShortcut
from PySide6.QtWidgets import (QApplication, QCheckBox, QComboBox, QDialog, QDialogButtonBox, QFileDialog, QHBoxLayout, QInputDialog,
                               QLabel, QLineEdit, QListWidget, QListWidgetItem, QMenu, QMessageBox, QProgressDialog,
                               QPushButton, QSlider, QToolButton, QTreeWidget, QTreeWidgetItem, QVBoxLayout, QWidget)

from .. import world_import
from .. import worlddata as wd
from ..model import NPC, Node, TARGETNAME_RE
from ..world import mdl as mdlmod
from ..world.gamefs import GameFS
from ..world.materials import MaterialSystem
from ..world.scene import MapScene, is_npc_class
from ..world.vmfdoc import VMFDoc, find_vmf
from ..world.thumbs import Thumbnailer
from .models_browser import PLACE_MIME, ModelBrowser, ModelGrid
from .worldview import LIGHT_CLASSES, WorldView

SDK_BIN = r"E:\Steam\steamapps\common\Source SDK Base 2013 Singleplayer\bin"
MAP_BACKUPS = r"E:\SourceModding\HL2RPMSOURCE\map_backups"
# new lights / props put into the VMF
NEW_LIGHT_KEYS = {"spawnflags": "1", "diffuse": "255 228 196 255", "ambient": "0 0 0 255", "radius": "384",
                  "power": "2", "spot_cone_inner": "30", "spot_cone_outer": "45", "vis_dist": "2048", "vis_range": "512",
                  "shadow_dist": "1536", "shadow_range": "512", "autoshadow": "1", "style_seed": "-1",
                  "enable_from_hour": "-1", "enable_to_hour": "-1"}

HUMAN_MODELS = (["models/humans/group01/male_%02d.mdl" % i for i in range(1, 10)] +
                ["models/humans/group01/female_%02d.mdl" % i for i in (1, 2, 3, 4, 6, 7)] +
                ["models/humans/group02/male_%02d.mdl" % i for i in range(1, 10)] +
                ["models/humans/group02/female_%02d.mdl" % i for i in (1, 2, 3, 4, 6, 7)] +
                ["models/humans/group03/male_%02d.mdl" % i for i in range(1, 10)] +
                ["models/humans/group03/female_%02d.mdl" % i for i in (1, 2, 3, 4, 6, 7)] +
                ["models/humans/group03m/male_%02d.mdl" % i for i in range(1, 10)])


class PaletteTree(QTreeWidget):
    """The object window: items can be dragged onto the map window."""

    def mimeData(self, items):
        md = QMimeData()
        d = items[0].data(0, Qt.UserRole) if items else None
        # only what can be placed without a question (a concrete model / kind)
        if d and d[0] in ("npc", "marker", "item", "container", "prop", "terminal", "vmf") and None not in d and (d[0] != "vmf" or d[1]):
            md.setData(PLACE_MIME, json.dumps(list(d)).encode("utf-8"))
        return md

    def mimeTypes(self):
        return [PLACE_MIME]


class MapPanel(QWidget):
    def __init__(self, ctx):
        super().__init__()
        self.ctx = ctx
        lay = QVBoxLayout(self)
        lay.setContentsMargins(0, 0, 0, 0)
        lay.setSpacing(0)
        bar = QWidget()
        bar.setObjectName("mapbar")
        rows = QVBoxLayout(bar)
        rows.setContentsMargins(6, 3, 6, 3)
        rows.setSpacing(3)
        bl = QHBoxLayout()
        tl = QHBoxLayout()
        rows.addLayout(bl)
        rows.addLayout(tl)
        # row 1: map, time, what to show, navigation, game, build
        bl.addWidget(QLabel("Карта:"))
        self.maps = QComboBox()
        self.maps.setMinimumContentsLength(24)
        self.maps.setSizeAdjustPolicy(QComboBox.AdjustToMinimumContentsLengthWithIcon)
        self.maps.setMaxVisibleItems(30)
        bl.addWidget(self.maps)
        self.reload = QToolButton(text="⟳")
        self.reload.setToolTip("Перечитать карту (после компиляции в Hammer)")
        bl.addWidget(self.reload)
        bl.addSpacing(12)
        bl.addWidget(QLabel("Время:"))
        self.hour = QSlider(Qt.Horizontal)
        self.hour.setRange(0, 24 * 4 - 1)
        self.hour.setValue(12 * 4)
        self.hour.setFixedWidth(170)
        self.hour.setToolTip("Время суток для просмотра распорядка: подпись над NPC и линии к месту текущего пакета")
        bl.addWidget(self.hour)
        self.hour_lbl = QLabel("12:00")
        self.hour_lbl.setMinimumWidth(40)
        bl.addWidget(self.hour_lbl)
        bl.addSpacing(8)
        self.show_btn = QToolButton(text="Показать ▾")
        self.show_btn.setPopupMode(QToolButton.InstantPopup)
        sm = QMenu(self.show_btn)
        self.toggles = {}
        for key, text, tip in (("labels", "Подписи", "Имена над объектами"), ("props", "Пропы карты", "Статические, динамические, физические пропы"),
                               ("hammer", "NPC, свет и сущности Hammer", "Что поставлено в Hammer, кроме пропов"),
                               ("nav", "Узлы навигации", "Точки, по которым ходят NPC"), ("packages", "Распорядок выбранного NPC", "Линии к местам его пакетов"),
                               ("mannequins", "Манекены на маркерах", "Как в GECK: на каждом маркере стоит фигура и играет его анимацию "
                                                                     "(лежит на кровати, сидит на стуле) — видно, как это будет в игре"),
                               ("anims", "Анимации NPC (распорядок часа)", "NPC проекта играют то, что делают в выбранный час: на маркере — "
                                                                         "его анимацию (сидят, лежат, работают), иначе стоят"),
                               ("light", "Освещение: лампы и солнце часа", "Превью света: лампы карты (light_deferred, прожекторы, классические), "
                                                                          "у 4 ближайших — тени; солнце и небо по ползунку времени"),
                               ("night", "Ночь (только лампы)", "Без солнца и неба: видно, что освещают лампы")):
            act = sm.addAction(text)
            act.setCheckable(True)
            act.setChecked(key not in ("anims", "light", "night"))      # mannequins on by default, like GECK
            act.setToolTip(tip)
            self.toggles[key] = act
        self.show_btn.setMenu(sm)
        bl.addWidget(self.show_btn)
        self.nav_btn = QToolButton(text="Навигация ▾")
        self.nav_btn.setPopupMode(QToolButton.InstantPopup)
        bl.addWidget(self.nav_btn)
        self.hammer_btn = QToolButton(text="Hammer++ ▾")
        self.hammer_btn.setPopupMode(QToolButton.InstantPopup)
        self.hammer_btn.setToolTip("Hammer++: открыть карту, видеть его сохранения без компиляции (геометрия из VMF), задачи для Claude")
        bl.addWidget(self.hammer_btn)
        bl.addStretch(1)
        self.build = QPushButton("⚙ Собрать карту")
        self.build.setToolTip("Записать правки объектов Hammer в VMF и пересобрать карту (F9). Только сущности — секунды, пропы — полная сборка")
        bl.addWidget(self.build)
        self.play = QPushButton("▶ Игра отсюда")
        self.play.setToolTip("Сохранить и запустить карту: игрок появится там, где камера (F6)")
        bl.addWidget(self.play)
        # row 2: tools for the selection
        self.giz_move = QToolButton(text="✥ Двигать")
        self.giz_move.setCheckable(True)
        self.giz_move.setChecked(True)
        self.giz_move.setToolTip("Гизмо: стрелки — по оси, квадраты — в плоскости (Ctrl — по сетке 16). Щелчок ПКМ по выделенному или Пробел — переключить")
        self.giz_rot = QToolButton(text="⟳ Вращать")
        self.giz_rot.setCheckable(True)
        self.giz_rot.setToolTip("Гизмо: кольца поворота (Ctrl — шаг 15°). Щелчок ПКМ по выделенному или Пробел — переключить")
        tl.addWidget(self.giz_move)
        tl.addWidget(self.giz_rot)
        self.land = QToolButton(text="⤓ Приземлить")
        self.land.setToolTip("Поставить выделенное на то, что под ним: модель касается земли самой нижней точкой (End)")
        tl.addWidget(self.land)
        self.align = QCheckBox("по наклону поверхности")
        self.align.setToolTip("Приземлять пропы с наклоном по поверхности (склоны, лестницы)")
        tl.addWidget(self.align)
        tl.addSpacing(16)
        keys = QLabel("Z — полёт · Ctrl+C / Ctrl+V — копировать / вставить (и на другую карту) · Ctrl+D — копия · Delete · F — к выделенному")
        keys.setObjectName("hint")
        tl.addWidget(keys)
        tl.addStretch(1)
        lay.addWidget(bar)
        self.view = WorldView(ctx)
        lay.addWidget(self.view, 1)


class WorldMixin:
    # ------------------------------------------------------------------ build
    def _build_world_ui(self):
        self.fs = None
        self.matsys = None
        self.scene = None
        self.layer = None
        self.cur_map = ""
        self._seq_cache = {}
        self._model_names = None
        self.vmfdocs = {}            # map name -> VMFDoc (kept while the program runs: unsaved edits survive a map switch)
        self._build_proc = None
        self._clip = None
        self.map_panel = MapPanel(self)
        self.worldview = self.map_panel.view
        mp = self.map_panel
        mp.maps.activated.connect(lambda _: self.open_map(mp.maps.currentData()))
        mp.reload.clicked.connect(lambda: self.open_map(self.cur_map, force=True))
        mp.hour.valueChanged.connect(self._hour_changed)
        for key, cb in mp.toggles.items():
            cb.toggled.connect(lambda v, key=key: (self.worldview.opts.__setitem__(key, bool(v)), self.worldview.invalidate_overlay()))
        m = QMenu(mp)
        m.addAction("Сгенерировать в игре…", self.nav_generate)
        m.addAction("Править узлы (клик — добавить, Delete — удалить)", self.nav_edit_mode)
        m.addAction("Удалить все узлы этой карты", self.nav_clear)
        mp.nav_btn.setMenu(m)
        hm = QMenu(mp)
        hm.addAction("Открыть карту в Hammer++", self.open_in_hammer)
        self.act_vmf_geom = hm.addAction("Геометрия из VMF (без компиляции)")
        self.act_vmf_geom.setCheckable(True)
        self.act_vmf_geom.setToolTip("Браши, дисплейсменты и метки задач строятся прямо из VMF: сохранил в Hammer++ — "
                                     "окно обновилось само, компилировать не нужно")
        self.act_vmf_geom.toggled.connect(self._geometry_toggled)
        hm.addAction("Открыть VMF (набросок без BSP)…", self.open_vmf_file)
        hm.addSeparator()
        hm.addAction("Задачи для Claude на этой карте…", self.show_claude_tasks)
        hm.addAction("Снимки карты для Claude (сверху, спереди, сбоку, 3D)", self.vmf_shots)
        mp.hammer_btn.setMenu(hm)
        self._vmf_watch = QTimer(self)
        self._vmf_watch.setInterval(1500)
        self._vmf_watch.timeout.connect(self._watch_vmf)
        self._vmf_watch.start()
        mp.play.clicked.connect(self.run_game_here)
        mp.build.clicked.connect(self.build_map)
        mp.land.clicked.connect(lambda: self.worldview.land_selected())
        mp.align.toggled.connect(lambda v: self.worldview.opts.__setitem__("align", bool(v)))
        mp.giz_move.clicked.connect(lambda: self.worldview.set_gizmo_mode("move"))
        mp.giz_rot.clicked.connect(lambda: self.worldview.set_gizmo_mode("rotate"))
        QShortcut(QKeySequence("F9"), self, activated=self.build_map)
        wv = self.worldview
        wv.gizmoModeChanged.connect(self._gizmo_mode_changed)
        wv.copyRequested.connect(self.copy_world_selected)
        wv.dropped.connect(self.on_world_drop)
        wv.pasteRequested.connect(self.paste_world)
        wv.selected.connect(self.on_world_selected)
        wv.edited.connect(self.on_world_edited)
        wv.placeRequested.connect(self.on_place)
        wv.contextRequested.connect(self.on_world_context)
        wv.activated.connect(self.on_world_activated)
        wv.deleteRequested.connect(self.delete_world_selected)
        wv.duplicateRequested.connect(self.duplicate_world_selected)
        wv.status.connect(lambda s: self.statusBar().showMessage(s, 5000))
        self.place_payload = None
        self.cell_timer = QTimer(self)
        self.cell_timer.setSingleShot(True)
        self.cell_timer.setInterval(250)
        self.cell_timer.timeout.connect(self.refresh_cell_view)

    def _build_palette(self):
        w = QWidget()
        l = QVBoxLayout(w)
        l.setContentsMargins(4, 4, 4, 4)
        self.palette = PaletteTree()
        self.palette.setHeaderHidden(True)
        self.palette.setDragEnabled(True)
        self.palette.itemDoubleClicked.connect(lambda it, _: self._palette_place(it))
        self.palette.setContextMenuPolicy(Qt.CustomContextMenu)
        self.palette.customContextMenuRequested.connect(self._palette_menu)
        l.addWidget(self.palette, 1)
        b = QPushButton("Поставить на карту")
        b.setToolTip("Или двойной клик по объекту, затем клик в окне карты (Shift — поставить несколько), или перетащите его на карту")
        b.clicked.connect(lambda: self._palette_place(self.palette.currentItem()))
        l.addWidget(b)
        return w

    def _build_cell_view(self):
        w = QWidget()
        l = QVBoxLayout(w)
        l.setContentsMargins(4, 4, 4, 4)
        self.cell_filter = QLineEdit()
        self.cell_filter.setPlaceholderText("фильтр…")
        self.cell_filter.textChanged.connect(lambda _: self.refresh_cell_view())
        l.addWidget(self.cell_filter)
        self.cell = QTreeWidget()
        self.cell.setHeaderLabels(["Объект", "Где"])
        self.cell.setColumnWidth(0, 190)
        self.cell.itemClicked.connect(self._cell_clicked)
        self.cell.itemDoubleClicked.connect(self._cell_double)
        l.addWidget(self.cell, 1)
        return w

    def _build_factions(self):
        w = QWidget()
        l = QVBoxLayout(w)
        l.setContentsMargins(4, 4, 4, 4)
        self.faction_list = QListWidget()
        self.faction_list.itemClicked.connect(lambda it: self.inspector.show_target(("faction", it.data(Qt.UserRole))))
        l.addWidget(self.faction_list, 1)
        b = QPushButton("+ фракция")
        b.clicked.connect(lambda: self.new_faction())
        l.addWidget(b)
        return w

    # ------------------------------------------------------------------ project / maps
    def init_world(self):
        p = self.project
        s = p.settings
        sdk = os.path.dirname(s.hl2_exe) if s.hl2_exe else ""
        extra = [s.game_dir] if s.game_dir and os.path.normcase(os.path.abspath(s.game_dir)) != os.path.normcase(os.path.abspath(s.mod_dir)) else []
        try:
            self.fs = GameFS(s.mod_dir, sdk, extra)
            self.matsys = MaterialSystem(self.fs)
        except Exception as e:
            self.fs = None
            self.logln("Файлы игры не прочитаны: %s" % e)
        self._seq_cache = {}
        self._model_names = None
        self.refresh_map_list()
        self.refresh_palette()
        self.refresh_faction_list()
        last = p.view.get("_last_map", "")
        files = self.map_files()
        if last not in files:
            last = next((m for m in sorted(p.maps) if m in files and p.maps[m].refs), "")
        self.scene = None
        self.layer = None
        self.cur_map = ""
        self.worldview.set_map(None, None)
        if last:
            QTimer.singleShot(50, lambda: self.open_map(last))

    def map_files(self):
        s = self.project.settings if self.project else None
        if s is None:
            return {}
        # the game's order: the mod's maps shadow testmaps (a fresh compile into hl2rpm\maps wins)
        dirs = [os.path.join(s.mod_dir, "maps"), os.path.join(s.mod_dir, "testmaps", "maps"), os.path.join(s.game_dir, "maps")]
        out = {}
        for d in dirs:
            for f in sorted(glob.glob(os.path.join(d, "*.bsp"))):
                out.setdefault(os.path.splitext(os.path.basename(f))[0], f)
        return out

    def vmf_sketches(self):
        """Maps that exist only as a VMF in the source folders (never compiled): name -> VMF path"""
        have = self.map_files()
        out = {}
        for d in self._vmf_dirs() if self.project else []:
            for f in sorted(glob.glob(os.path.join(d, "*.vmf"))):
                n = os.path.splitext(os.path.basename(f))[0]
                if n not in have:
                    out.setdefault(n, f)
        return out

    # ------------------------------------------------------------------ Hammer++
    def _hammer_exe(self):
        s = self.project.settings if self.project else None
        base = os.path.dirname(s.hl2_exe) if s else r"E:\Steam\steamapps\common\Source SDK Base 2013 Singleplayer"
        for p in (os.path.join(base, "bin", "hammerplusplus.exe"), os.path.join(base, "bin", "hammerplusplus", "hammerplusplus.exe")):
            if os.path.isfile(p):
                return p
        return ""

    def open_in_hammer(self):
        doc = self._vmf_doc(self.cur_map) if self.cur_map else None
        exe = self._hammer_exe()
        if not exe:
            QMessageBox.warning(self, "Hammer++", "Не найден hammerplusplus.exe рядом с hl2.exe (Проект → Настройки).")
            return
        args = [exe] + ([doc.path] if doc is not None else [])
        subprocess.Popen(args, cwd=os.path.dirname(exe))
        self.logln("Hammer++: " + (doc.path if doc is not None else "без карты") +
                   ("  — включите «Геометрия из VMF», чтобы видеть его сохранения здесь без компиляции" if not self.act_vmf_geom.isChecked() else ""))

    def _geometry_toggled(self, on):
        if self.project:
            self.project.view["_geometry"] = "vmf" if on else "bsp"
        if self.cur_map:
            self.open_map(self.cur_map, force=True)

    def _watch_vmf(self):
        """Hammer++ saved the map: rebuild the VMF geometry (camera kept)"""
        sc = self.scene
        if sc is None or sc.geometry != "vmf" or sc.vmf is None or getattr(self, "_loading_map", False):
            return
        doc = sc.vmf
        if doc.changed_on_disk() and not doc.dirty:
            self.logln("Hammer++ сохранил %s — карта перестроена из VMF" % os.path.basename(doc.path))
            self.open_map(self.cur_map, force=True)

    def open_vmf_file(self):
        start = (self._vmf_dirs() or [""])[0]
        path, _ = QFileDialog.getOpenFileName(self, "VMF (набросок)", start, "Hammer map (*.vmf)")
        if not path:
            return
        name = os.path.splitext(os.path.basename(path))[0]
        if name not in self.vmfdocs:
            try:
                self.vmfdocs[name] = VMFDoc(path)
            except Exception as e:
                QMessageBox.critical(self, "VMF", "%s:\n%s" % (path, e))
                return
        if not self.act_vmf_geom.isChecked():
            self.act_vmf_geom.blockSignals(True)
            self.act_vmf_geom.setChecked(True)
            self.act_vmf_geom.blockSignals(False)
        self.open_map(name, force=True)

    def show_claude_tasks(self):
        sc = self.scene
        tasks = getattr(sc.bsp, "tasks", None) if sc is not None else None
        if tasks is None:
            QMessageBox.information(self, "Задачи для Claude", "Задачи видны в режиме «Геометрия из VMF» (меню Hammer++).")
            return
        dlg = QDialog(self)
        dlg.setWindowTitle("Задачи для Claude — %s" % self.cur_map)
        lay = QVBoxLayout(dlg)
        lst = QListWidget()
        st = {"todo": "сделать", "doing": "в работе", "question": "вопрос", "done": "готово"}
        for t in sorted(tasks, key=lambda t: (t.get("status") == "done", t.get("priority", "2"))):
            it = QListWidgetItem("[%s] %s — %s%s" % (st.get(t.get("status"), t.get("status")), t.get("name") or "(без имени)",
                                                     t.get("task") or "", ("\n   ответ: " + t["reply"]) if t.get("reply") else ""))
            it.setData(Qt.UserRole, [(t["mins"][i] + t["maxs"][i]) / 2 for i in range(3)])
            lst.addItem(it)
        lst.itemDoubleClicked.connect(lambda it: self.worldview.focus_on(it.data(Qt.UserRole), 900))
        lay.addWidget(QLabel("Метки rpm_claude_task / func_rpm_claude_area из Hammer++. Двойной щелчок — показать на карте."))
        lay.addWidget(lst)
        bb = QDialogButtonBox(QDialogButtonBox.Close)
        bb.rejected.connect(dlg.reject)
        lay.addWidget(bb)
        dlg.resize(760, 420)
        dlg.show()
        self._tasks_dlg = dlg

    def vmf_shots(self):
        doc = self._vmf_doc(self.cur_map) if self.cur_map else None
        if doc is None:
            QMessageBox.information(self, "Снимки", "У карты нет VMF в папках исходников.")
            return
        from ..world import vmfshot
        out = os.path.join(os.path.dirname(doc.path), "_claude_shots", self.cur_map)
        files = vmfshot.render_all(doc.path, out, materials=self.matsys)
        self.logln("Снимки карты: " + out + "  (" + ", ".join(os.path.basename(f) for f in files) + ")")
        subprocess.Popen(["explorer", out])

    def _map_label(self, name):
        layer = self.project.layer(name) if self.project else None
        n = len(layer.refs) if layer else 0
        sketch = name not in (getattr(self, "_bsp_names", None) or ())
        return "%s%s%s" % (name, ("  · объектов %d" % n) if n else "", "  · набросок (VMF)" if sketch else "")

    def refresh_map_list(self):
        mp = self.map_panel
        if self.project is not None and getattr(self, "_geom_project", None) is not self.project:
            # the "geometry from VMF" choice is the project's
            self._geom_project = self.project
            v = self.project.view if isinstance(self.project.view, dict) else {}
            self.act_vmf_geom.blockSignals(True)
            self.act_vmf_geom.setChecked(v.get("_geometry") == "vmf")
            self.act_vmf_geom.blockSignals(False)
        files = dict(self.map_files())
        self._bsp_names = set(files)
        for n, f in self.vmf_sketches().items():
            files.setdefault(n, f)
        names = sorted(files, key=lambda n: (not (self.project and self.project.layer(n) and self.project.layer(n).refs), n.lower()))
        have = [mp.maps.itemData(i) for i in range(1, mp.maps.count())]
        if sorted(have) == sorted(names) and mp.maps.count():
            # same maps: only the object counts change (clear() would close an open list)
            for i in range(1, mp.maps.count()):
                t = self._map_label(mp.maps.itemData(i))
                if mp.maps.itemText(i) != t:
                    mp.maps.setItemText(i, t)
            return
        if mp.maps.view().isVisible():
            return
        mp.maps.blockSignals(True)
        mp.maps.clear()
        mp.maps.addItem("— выберите карту —", "")
        for name in names:
            mp.maps.addItem(self._map_label(name), name)
        mp.maps.setCurrentIndex(max(0, mp.maps.findData(self.cur_map)))
        mp.maps.blockSignals(False)

    def current_map_name(self):
        return self.cur_map

    def open_map(self, name, force=False):
        if not name or not self.project or getattr(self, "_loading_map", False):
            return
        if name == self.cur_map and self.scene is not None and not force:
            self.central_tabs.setCurrentWidget(self.map_panel)
            return
        files = self.map_files()
        path = files.get(name)
        geometry = "vmf" if self.act_vmf_geom.isChecked() else "bsp"
        if not path:
            # a sketch that exists only as a VMF (never compiled): geometry from the VMF
            vdoc = self._vmf_doc(name)
            if vdoc is None:
                QMessageBox.warning(self, "Карта", "Не найден %s.bsp в папках maps игры и мода (и VMF в папках исходников)." % name)
                return
            path, geometry = vdoc.path, "vmf"
        if self.fs is None:
            QMessageBox.warning(self, "Карта", "Не удалось прочитать файлы игры (Проект → Настройки: папка мода и hl2.exe).")
            return
        if self.layer is not None and self.cur_map:
            self.layer.camera = self.worldview.camera_state()
        prog = QProgressDialog("Загрузка карты %s…" % name, None, 0, 100, self)
        prog.setWindowTitle("Карта")
        prog.setWindowModality(Qt.WindowModal)
        prog.setMinimumDuration(200)

        def progress(f, msg):
            prog.setValue(int(f * 100))
            prog.setLabelText(msg)
            QApplication.processEvents()
        t0 = time.time()
        doc = self._vmf_doc(name)
        # the progress dialog runs the event loop: no second load from a click or a timer meanwhile
        self._loading_map = True
        try:
            scene = MapScene(path, self.fs, self.matsys, progress, vmf=doc, geometry=geometry)
        except Exception as e:
            prog.close()
            QMessageBox.critical(self, "Карта", "Не удалось открыть %s:\n%s" % (path, e))
            return
        finally:
            self._loading_map = False
        prog.close()
        keep_cam = force and name == self.cur_map
        self.scene = scene
        self.cur_map = name
        self.layer = self.project.layer(name, create=True)
        self.project.view["_last_map"] = name
        self.worldview.set_map(scene, self.layer, self.matsys, keep_camera=keep_cam)
        self.map_panel.maps.setCurrentIndex(max(0, self.map_panel.maps.findData(name)))
        self.refresh_cell_view()
        nprops = sum(1 for ob in scene.objects if ob.draw_model() and not ob.is_npc())
        self.logln("Карта %s: %d треугольников, объектов Hammer %d (пропов %d), объектов проекта %d, узлов навигации %d (%.1f с)" % (
            name, scene.ray.n, len(scene.objects), nprops, len(self.layer.refs), len(self.layer.nav), time.time() - t0))
        if scene.geometry == "vmf":
            g = scene.bsp
            self.logln("  Геометрия из VMF (без компиляции): брашей %d, граней %d, дисплейсментов %d, задач для Claude %d — "
                       "сохранения Hammer++ подхватываются сами" % (g.solid_count, g.side_count, g.disp_count, len(g.tasks)))
        if doc is not None:
            self.logln("  VMF: %s — объекты Hammer можно двигать и править, F9 — собрать карту" % doc.path)
        else:
            self.logln("  VMF карты не найден в папках исходников (Проект → Настройки) — объекты Hammer только для просмотра")
        if self.central_tabs.currentWidget() is not self.map_panel:
            self.central_tabs.setCurrentWidget(self.map_panel)

    # ------------------------------------------------------------------ favourites of the objects window
    def _favorites(self):
        v = self.project.view if isinstance(self.project.view, dict) else {}
        self.project.view = v
        return v.setdefault("favorites", [])

    def _palette_menu(self, pos):
        it = self.palette.itemAt(pos)
        data = it.data(0, Qt.UserRole) if it is not None else None
        if not data or not self.project:
            return
        from PySide6.QtWidgets import QMenu
        favs = self._favorites()
        key = list(data)
        inside = [f for f in favs if list(f[1]) == key]
        m = QMenu(self.palette)
        a = m.addAction("Убрать из избранного" if inside else "\u2605 В избранное")
        if m.exec(self.palette.viewport().mapToGlobal(pos)) is not a:
            return
        if inside:
            for f in inside:
                favs.remove(f)
        else:
            favs.append([it.text(0).strip(), key])
        self.model_changed(False)
        self.refresh_palette()

    # ------------------------------------------------------------------ lists
    def refresh_palette(self):
        pt = self.palette
        pt.clear()
        if not self.project:
            return

        def cat(title):
            it = QTreeWidgetItem([title])
            f = it.font(0)
            f.setBold(True)
            it.setFont(0, f)
            pt.addTopLevelItem(it)
            return it

        def add(parent, text, data, tip=""):
            it = QTreeWidgetItem([text])
            it.setData(0, Qt.UserRole, data)
            if tip:
                it.setToolTip(0, tip)
            parent.addChild(it)
            return it
        favs = self._favorites()
        if favs:
            c = cat("\u2605 Избранное")
            for text, data in favs:
                add(c, text, tuple(data), "ПКМ — убрать из избранного")
        c = cat("Персонажи (NPC проекта)")
        for n in sorted(self.project.npcs, key=lambda x: x.targetname.lower()):
            disp = n.display_name.get(self.display_lang) or n.display_name.get(self.project.base_language, "")
            add(c, "%s%s" % (n.targetname, ("  — " + disp) if disp else ""), ("npc", n.targetname),
                wd.NPC_CLASS_NAMES.get(n.actor.classname, n.actor.classname))
        add(c, "＋ новый персонаж…", ("new_npc",))
        c = cat("Маркеры (кровать, стул, стена…)")
        for k, n, col in wd.MARKER_KINDS:
            it = add(c, n, ("marker", k))
            it.setForeground(0, QColor(col))
        c = cat("Предметы")
        lang, base = self.display_lang, self.project.base_language
        for name in self.all_item_ids():
            it = self.project.item(name)
            add(c, "%s  (%s)" % (it.label(lang, base), name) if it else name, ("item", name))
        c = cat("Контейнеры (с лутом)")
        for m, n in wd.CONTAINER_MODELS:
            add(c, n, ("container", m), m)
        add(c, "другая модель…", ("container", None))
        c = cat("Терминалы (компьютеры, консоли Альянса)")
        from ..termdata import MODELS as TERM_MODELS
        for m, style, n in TERM_MODELS:
            add(c, n, ("terminal", m), m)
        c = cat("Пропы (мебель, декор)")
        for m, n in wd.PROP_MODELS:
            add(c, n, ("prop", m), m)
        add(c, "другая модель…", ("prop", None))
        c = cat("Прочее")
        add(c, "Переход на другую карту (дверь)", ("transition",),
            "Коробка-триггер: игрок входит — грузится другая карта, он появляется перед парной дверью там. "
            "Инвентарь, квесты, время и погода переносятся")
        add(c, "Зона (для событий и целей заданий)", ("zone",),
            "Невидимая коробка с именем: события «вошёл / вышел из зоны», цель задания «Дойти до», условие «игрок в зоне»")
        add(c, "Звук на карте (гул, капли, радио)", ("sound",),
            "Звук в точке: петля звучит всё время. Время суток и задания — условием присутствия")
        add(c, "Точка появления (враги, массовка)", ("spawner",),
            "Держит рядом с собой несколько NPC из списка, пока игрок неподалёку; после их смерти пополняется через N часов")
        add(c, "Маршрут патруля", ("path",), "Кликайте точки на карте, Enter или правая кнопка — готово")
        add(c, "Своя сущность (classname)…", ("entity",), "ambient_generic, light_deferred, info_target…")
        c = cat("В саму карту (Hammer, после правки — F9)")
        tip = "Записывается в VMF карты, как будто поставлено в Hammer; в игре появится после сборки карты (F9)"
        add(c, "Статический проп…", ("vmf", "prop_static", None), "Самый дешёвый для FPS проп (не двигается). " + tip)
        for m, n in wd.PROP_MODELS:
            add(c, "   " + n, ("vmf", "prop_static", m), m)
        add(c, "Динамический проп…", ("vmf", "prop_dynamic", None), "Проп с анимацией / входами. " + tip)
        add(c, "Физический проп…", ("vmf", "prop_physics", None), "Падает и толкается. " + tip)
        add(c, "Лампа (deferred)", ("vmf", "light_deferred", ""), "Точечный свет с тенью. Сборка — секунды. " + tip)
        add(c, "Прожектор (deferred)", ("vmf", "light_deferred_spot", ""), "Направленный свет: повернуть — Alt+тащить или кольца. " + tip)
        add(c, "Сущность Hammer (classname)…", ("vmf", "", ""), "info_target, ambient_generic, env_* … " + tip)
        pt.expandItem(pt.topLevelItem(0))
        pt.expandItem(pt.topLevelItem(1))
        if favs:
            pt.expandItem(pt.topLevelItem(2))

    def refresh_faction_list(self):
        self.faction_list.clear()
        if not self.project:
            return
        for f in self.project.factions:
            name = f.name.get(self.display_lang) or f.name.get(self.project.base_language, "")
            it = QListWidgetItem("%s%s\n   к игроку: %s" % (f.id, ("  — " + name) if name else "", wd.RELATION_NAMES.get(f.player, "по классу")))
            it.setData(Qt.UserRole, f.id)
            self.faction_list.addItem(it)

    def refresh_cell_view(self):
        c = self.cell
        c.clear()
        if self.layer is None:
            return
        flt = self.cell_filter.text().strip().lower()
        groups = {}
        for t, n in wd.REF_TYPES:
            it = QTreeWidgetItem([n, ""])
            f = it.font(0)
            f.setBold(True)
            it.setFont(0, f)
            groups[t] = it
        for r in self.layer.refs:
            lbl = r.label()
            if r.type == "marker":
                lbl = "%s  (%s%s)" % (lbl, wd.MARKER_NAMES.get(r.kind, r.kind), (", " + r.owner) if r.owner else "")
            if flt and flt not in lbl.lower():
                continue
            it = QTreeWidgetItem([lbl, "%.0f %.0f %.0f" % tuple(r.pos) if r.type != "path" else "%d точек" % len(r.points)])
            it.setData(0, Qt.UserRole, ("ref", r.id))
            groups[r.type].addChild(it)
        for t, it in groups.items():
            if it.childCount():
                it.setText(0, "%s (%d)" % (it.text(0), it.childCount()))
                c.addTopLevelItem(it)
                it.setExpanded(True)
        if self.scene is not None:
            ro = "" if self.scene.vmf is not None else "  · только просмотр"
            heads = {"npc": "NPC из Hammer", "prop": "Пропы Hammer", "light": "Свет Hammer", "other": "Сущности Hammer"}
            hgroups = {}
            for ob in self.scene.objects:
                kind = "npc" if ob.is_npc() else "prop" if ob.draw_model() else "light" if ob.classname in LIGHT_CLASSES else "other"
                tn = ob.get("targetname")
                if kind == "npc":
                    lbl = "%s  (%s)" % (tn or "без имени", ob.classname)
                elif kind == "prop":
                    lbl = "%s  · %s" % (ob.label(), ob.classname.replace("prop_", ""))
                else:
                    lbl = "%s  (%s)" % (tn, ob.classname) if tn else ob.classname
                if flt and flt not in lbl.lower():
                    continue
                if kind not in hgroups:
                    hi = QTreeWidgetItem(["", ""])
                    f = hi.font(0)
                    f.setBold(True)
                    hi.setFont(0, f)
                    hgroups[kind] = hi
                it = QTreeWidgetItem([lbl, "%.0f %.0f %.0f" % tuple(ob.origin)])
                it.setData(0, Qt.UserRole, ("obj", ob.key))
                if kind == "npc" and tn and self.project.npc(tn):
                    it.setForeground(0, QColor("#7fd6ff"))
                if ob.ent is not None and ob.ent.dirty:
                    it.setForeground(1, QColor("#ffd27a"))
                hgroups[kind].addChild(it)
            for kind in ("npc", "prop", "light", "other"):
                hi = hgroups.get(kind)
                if hi is not None:
                    hi.setText(0, "%s (%d)%s" % (heads[kind], hi.childCount(), ro))
                    c.addTopLevelItem(hi)
                    hi.setExpanded(kind in ("npc", "light") or bool(flt))
        self.refresh_map_list()

    def _cell_clicked(self, it, col):
        t = it.data(0, Qt.UserRole)
        if t:
            self.worldview.select(t)

    def _cell_double(self, it, col):
        t = it.data(0, Qt.UserRole)
        if t:
            self.worldview.select(t)
            self.worldview.focus_selected()
            self.central_tabs.setCurrentWidget(self.map_panel)

    # ------------------------------------------------------------------ selection / edits
    def world_changed(self, what=""):
        self.worldview.invalidate_overlay()
        self.cell_timer.start()
        if what in ("faction",):
            self.refresh_faction_list()

    def on_world_selected(self, target):
        if target is None:
            return
        if target[0] == "multi":
            self.inspector.show_target(("worldmulti", target[1]))
            return
        if target[0] == "ref" and self.layer is not None:
            r = self.layer.ref(target[1])
            self.worldview.focus_npc = r.npc if r is not None and r.type == "npc" else ""
        self.inspector.show_target(target)

    def on_world_edited(self, what):
        self.model_changed(False, moved=True)
        self.cell_timer.start()
        t = self.inspector.target
        if t and t[0] in ("ref", "pathpt", "nav", "obj", "worldmulti"):
            self.inspector.rebuild_soon()

    def on_world_activated(self, target):
        if target and target[0] == "ref" and self.layer is not None:
            r = self.layer.ref(target[1])
            if r is not None and r.type == "npc" and self.project.npc(r.npc):
                self.open_npc(r.npc)

    def _sel_ref(self):
        t = self.worldview.sel
        if t is not None and t[0] in ("ref", "pathpt") and self.layer is not None:
            return self.layer.ref(t[1])
        return None

    def delete_world_selected(self):
        t = self.worldview.sel
        if t is None or self.layer is None:
            return
        many = [x for x in self.worldview.editable_selection() if x[0] in ("ref", "obj")]
        if len(many) > 1 or (t[0] == "obj"):
            self.delete_targets(many or [t])
            return
        if t[0] == "pathpt":
            r = self.layer.ref(t[1])
            if r is not None and t[2] < len(r.points):
                del r.points[t[2]]
                self.worldview.select(("ref", r.id))
                self.on_world_edited("delete")
            return
        if t[0] == "nav":
            self.delete_nav(t[1])
            return
        r = self._sel_ref()
        if r is not None:
            self.delete_ref(r)

    def duplicate_world_selected(self):
        sel = [x for x in self.worldview.editable_selection() if x[0] in ("ref", "obj")]
        if len(sel) > 1 or (sel and sel[0][0] == "obj"):
            self._paste_items(self._copy_items(sel), offset=[48, 0, 0])
            return
        r = self._sel_ref()
        if r is not None:
            self.duplicate_ref(r)

    def delete_ref(self, ref):
        if self.layer is None or ref not in self.layer.refs:
            return
        self.layer.refs.remove(ref)
        self.worldview.select(None, emit=False)
        self.inspector.show_target(("npc",) if self.npc else None)
        self.model_changed(False, moved=True)
        self.world_changed("delete")

    def duplicate_ref(self, ref):
        if self.layer is None:
            return
        r = ref.clone(self.layer.new_id())
        r.pos[0] += 48
        if r.name:
            r.name = self._unique_name(r.name)
        self.layer.refs.append(r)
        self.model_changed(False, moved=True)
        self.world_changed("dup")
        self.worldview.select(("ref", r.id))

    def delete_nav(self, i):
        if self.layer is not None and 0 <= i < len(self.layer.nav):
            del self.layer.nav[i]
            self.worldview.select(None, emit=False)
            self.model_changed(False, moved=True)
            self.world_changed("nav")

    def focus_ref(self, mapname, rid):
        if mapname != self.cur_map:
            self.open_map(mapname)
        if self.layer is None or self.cur_map != mapname:
            return
        self.central_tabs.setCurrentWidget(self.map_panel)
        self.worldview.select(("ref", rid))
        self.worldview.focus_selected()

    def _unique_name(self, base):
        used = set()
        for layer in self.project.maps.values():
            for r in layer.refs:
                if r.name:
                    used.add(r.name.lower())
        stem = base.rstrip("0123456789").rstrip("_") or base
        if base.lower() not in used:
            return base
        k = 2
        while ("%s_%d" % (stem, k)).lower() in used:
            k += 1
        return "%s_%d" % (stem, k)

    def rename_world_name(self, old, new):
        for n in self.project.npcs:
            for pk in n.actor.packages:
                if pk.ref == old:
                    pk.ref = new
                if pk.path == old:
                    pk.path = new
                if pk.target == old:
                    pk.target = new
        for layer in self.project.maps.values():
            for r in layer.refs:
                if r.type == "transition" and r.target == old:      # the pair of a renamed transition
                    r.target = new

    def ref_names_for(self, tn):
        maps = [m for m, _ in self.project.placements(tn)] or ([self.cur_map] if self.cur_map else [])
        names = set()
        for m in maps:
            layer = self.project.layer(m)
            if layer:
                names.update(r.name for r in layer.refs if r.name and r.type != "path")
        return sorted(names)

    def path_names_for(self, tn):
        maps = [m for m, _ in self.project.placements(tn)] or ([self.cur_map] if self.cur_map else [])
        names = set()
        for m in maps:
            layer = self.project.layer(m)
            if layer:
                names.update(layer.path_names())
        return sorted(names)

    # ------------------------------------------------------------------ placing
    def _palette_place(self, it):
        if it is None:
            return
        d = it.data(0, Qt.UserRole)
        if not d:
            return
        if d[0] == "new_npc":
            self.new_npc()
            return
        if d[0] in ("container", "prop") and d[1] is None:
            m = self.browse_model("", "models/")
            if not m:
                return
            d = (d[0], m)
        if d[0] == "entity":
            cls, ok = QInputDialog.getText(self, "Сущность", "classname:", text="ambient_generic")
            if not ok or not cls.strip():
                return
            d = ("entity", cls.strip())
        if d[0] == "vmf":
            if self.scene is not None and self.scene.vmf is None:
                QMessageBox.information(self, "В карту", "Для этой карты не найден VMF (папки исходников карт — Проект → Настройки), "
                                        "поэтому в саму карту ставить нельзя. Можно поставить объект программы (выше).")
                return
            if d[1].startswith("prop_") and d[2] is None:
                mdl = self.browse_model("", "models/")
                if not mdl:
                    return
                d = (d[0], d[1], mdl)
            if not d[1]:
                cls, ok = QInputDialog.getText(self, "Сущность Hammer", "classname:", text="info_target")
                if not ok or not cls.strip():
                    return
                d = (d[0], cls.strip(), "")
        self.start_place(d)

    def start_place(self, payload):
        if self.scene is None:
            QMessageBox.information(self, "Карта", "Сначала откройте карту (список «Карта» над окном карты).")
            return
        self.central_tabs.setCurrentWidget(self.map_panel)
        self.place_payload = payload
        names = {"npc": "NPC «%s»" % payload[1] if len(payload) > 1 else "NPC", "marker": "маркер «%s»" % wd.MARKER_NAMES.get(payload[1], ""),
                 "item": "предмет %s" % (payload[1] if len(payload) > 1 else ""), "container": "контейнер", "prop": "проп",
                 "path": "маршрут", "entity": "сущность %s" % (payload[1] if len(payload) > 1 else ""),
                 "vmf": "%s в карту" % (payload[1] if len(payload) > 1 else ""), "spawner": "точку появления", "transition": "переход", "zone": "зону", "sound": "звук",
                 "terminal": "терминал"}
        self.worldview.set_mode("place", "Клик — поставить %s · Shift+клик — поставить ещё · Esc — отмена" % names.get(payload[0], ""))
        self.worldview.setFocus()

    def start_place_npc(self, tn):
        self.start_place(("npc", tn))

    def on_place(self, pos, normal):
        d = self.place_payload
        if d is None or self.layer is None:
            return
        if d[0] == "vmf":
            self._place_vmf(d, pos, normal)
            return
        L = self.layer
        cam = self.worldview.cam.pos
        yaw = math.degrees(math.atan2(cam[1] - pos[1], cam[0] - pos[0])) % 360   # face the camera
        r = wd.Ref(L.new_id(), d[0], pos)
        r.angles = [0.0, round(yaw / 15) * 15 % 360, 0.0]
        if d[0] == "npc":
            r.npc = d[1]
        elif d[0] == "marker":
            r.kind = d[1]
            r.anim = wd.DEFAULT_MARKER_ANIM.get(d[1], "")
            r.name = self._unique_name("%s_%d" % (d[1], r.id))
            if d[1] in ("lean",):
                r.angles[1] = (math.degrees(math.atan2(normal[1], normal[0])) % 360) if abs(normal[2]) < 0.5 else r.angles[1]
        elif d[0] == "item":
            r.classname = d[1]
            r.pos[2] += 4
        elif d[0] == "terminal":
            from ..termdata import MODELS as TERM_MODELS
            r.model = d[1]
            m = self.scene.model(d[1])
            if m is not None and m.ok:
                r.pos[2] += self.scene.lowest_offset(d[1], r.angles)
            style = next((s for mm, s, _n in TERM_MODELS if mm == d[1]), "pc")
            same = [t for t in self.project.terminals if t.style == style] or list(self.project.terminals)
            r.terminal = same[0].id if same else ""
        elif d[0] in ("container", "prop"):
            r.model = d[1]
            m = self.scene.model(d[1])
            if m is not None and m.ok:
                r.pos[2] += self.scene.lowest_offset(d[1], r.angles)      # the lowest point on the surface
            if d[0] == "container":
                label = dict(wd.CONTAINER_MODELS).get(d[1], "")
                if label and "russian" in self.project.languages:
                    r.display_name["russian"] = label
        elif d[0] == "path":
            r.name = self._unique_name("patrol_%d" % r.id)
            r.points = [wd.PathPoint(pos)]
            L.refs.append(r)
            self.model_changed(False, moved=True)
            self.world_changed("place")
            self.worldview.select(("ref", r.id))
            self.start_path_draw(r)
            return
        elif d[0] == "entity":
            r.classname = d[1]
        elif d[0] == "transition":
            r.name = self._unique_name("tr_%s_%d" % (self.cur_map, r.id))
        elif d[0] == "zone":
            r.name = self._unique_name("zone_%d" % r.id)
            r.size = [256.0, 256.0, 128.0]
        elif d[0] == "sound":
            r.pos[2] += 32
            r.sound = "ambient/machines/transformer_loop.wav"
            r.radius = 800.0
        elif d[0] == "spawner":
            r.name = self._unique_name("spawn_%d" % r.id)
            r.keyvalues = {"npcs": "", "count_min": "1", "count_max": "3", "radius": "384", "respawn_hours": "24"}
        L.refs.append(r)
        self.model_changed(False, moved=True)
        self.world_changed("place")
        self.worldview.select(("ref", r.id))

    def start_pick_point(self, cb, text):
        if self.scene is None:
            QMessageBox.information(self, "Карта", "Откройте карту.")
            return
        self.central_tabs.setCurrentWidget(self.map_panel)
        self.worldview.set_mode("pick", text, cb)
        self.worldview.setFocus()

    def start_path_draw(self, ref):
        if self.scene is None or self.layer is None or ref not in self.layer.refs:
            return
        self.central_tabs.setCurrentWidget(self.map_panel)

        def add(pos):
            if pos is None:
                self.model_changed(False, moved=True)
                self.world_changed("path")
                self.inspector.show_target(("ref", ref.id))
                return
            ref.points.append(wd.PathPoint(pos))
            self.world_changed("path")
        self.worldview.set_mode("path", "Маршрут «%s»: клик — точка · Enter, Esc или правая кнопка — готово" % ref.name, add)
        self.worldview.setFocus()

    def start_new_path(self, tn, pkg):
        if self.layer is None:
            QMessageBox.information(self, "Карта", "Откройте карту, на которой будет маршрут.")
            return
        r = wd.Ref(self.layer.new_id(), "path", [0, 0, 0])
        r.name = self._unique_name("%s_patrol" % tn)
        self.layer.refs.append(r)
        pkg.path = r.name
        self.model_changed(True)
        self.worldview.select(("ref", r.id), emit=False)
        self.start_path_draw(r)

    # ------------------------------------------------------------------ context menu
    def on_world_context(self, target, gpos, wpos):
        m = QMenu(self)
        if target is not None and target[0] in ("ref", "pathpt"):
            r = self.layer.ref(target[1])
            if r is not None:
                if r.type == "npc" and self.project.npc(r.npc):
                    m.addAction("Открыть NPC «%s»" % r.npc, lambda: self.open_npc(r.npc))
                    m.addAction("Распорядок…", lambda: self.open_npc(r.npc, tab=2))
                if r.type == "path":
                    m.addAction("Добавить точки…", lambda: self.start_path_draw(r))
                mname = self.worldview._ref_model_name(r)
                if mname:
                    m.addAction("Просмотр модели и анимаций…", lambda r=r, mname=mname: self.view_model_of_ref(r, mname))
                elif r.type == "marker":
                    m.addAction("Анимация маркера…", lambda r=r: self.view_marker_anim(r))
                m.addAction("Опустить на пол", lambda: (setattr(r, "pos", self.scene.drop_to_floor(r.pos)), self.on_world_edited("drop"), self.world_changed("drop")))
                m.addAction("Повернуть на 90°", lambda: self.worldview.rotate_selected(90))
                m.addAction("Дублировать", lambda: self.duplicate_ref(r))
                m.addSeparator()
                if target[0] == "pathpt":
                    m.addAction("Удалить точку", self.delete_world_selected)
                m.addAction("Удалить", lambda: self.delete_ref(r))
                m.addSeparator()
        elif target is not None and target[0] == "obj":
            ob = self.scene.find(target[1])
            if ob is not None:
                tn = ob.get("targetname")
                if ob.is_npc():
                    if tn and self.project.npc(tn):
                        m.addAction("Открыть NPC «%s»" % tn, lambda tn=tn: self.open_npc(tn))
                    elif tn:
                        m.addAction("Добавить «%s» в проект" % tn, lambda ob=ob, tn=tn: self.npc_from_hammer(tn, ob.classname, ob))
                if ob.draw_model():
                    m.addAction("Просмотр модели и анимаций…", lambda ob=ob: self.view_model(ob.draw_model(), ob.skin))
                if ob.editable:
                    m.addAction("Приземлить (End)", self.worldview.land_selected)
                    m.addAction("Повернуть на 90°", lambda: self.worldview.rotate_selected(90))
                    m.addAction("Дублировать (Ctrl+D)", self.duplicate_world_selected)
                    m.addAction("Копировать (Ctrl+C)", self.copy_world_selected)
                    m.addSeparator()
                    m.addAction("Удалить", self.delete_world_selected)
                else:
                    a = m.addAction("VMF карты не найден — только просмотр")
                    a.setEnabled(False)
            m.addSeparator()
        if self._clip:
            m.addAction("Вставить сюда (Ctrl+V)", lambda: self.paste_world(wpos))
        if wpos is not None:
            sub = m.addMenu("Поставить сюда")
            for n in sorted(self.project.npcs, key=lambda x: x.targetname.lower())[:40]:
                sub.addAction("NPC " + n.targetname, lambda tn=n.targetname: self._place_at(("npc", tn), wpos))
            sub.addSeparator()
            for k, n, _ in wd.MARKER_KINDS:
                sub.addAction("Маркер: " + n, lambda k=k: self._place_at(("marker", k), wpos))
            sub.addSeparator()
            sub.addAction("Ящик с лутом", lambda: self._place_at(("container", wd.CONTAINER_MODELS[0][0]), wpos))
            sub.addAction("Терминал: компьютер", lambda: self._place_at(("terminal", "models/props_lab/monitor01a.mdl"), wpos))
            sub.addAction("Терминал: экран Альянса", lambda: self._place_at(("terminal", "models/props_combine/combine_intmonitor001.mdl"), wpos))
            sub.addAction("Аптечка", lambda: self._place_at(("item", "item_healthkit"), wpos))
            sub.addAction("Маршрут патруля отсюда", lambda: self._place_at(("path",), wpos))
            m.addAction("Игра отсюда (F6)", lambda: self.run_game_here(wpos))
        m.addAction("Вид сверху сюда", lambda: self._top_view(wpos))
        m.exec(gpos)

    # ------------------------------------------------------------------ model viewer (M7)
    def view_model(self, model_name, skin=0, sequence="", title=""):
        from .model_viewer import open_viewer
        return open_viewer(self, model_name, skin, sequence, title)

    def view_model_of_ref(self, r, model_name):
        skin, seq = r.skin, ""
        if r.type == "npc":
            npc = self.project.npc(r.npc)
            skin = npc.actor.skin if npc else 0
            seq = self.npc_preview_sequence(npc) if npc else ""
        return self.view_model(model_name, skin, seq, "Просмотр: %s" % r.label())

    def npc_preview_sequence(self, npc, hour=None):
        """The animation an NPC plays at this hour of the map view: its package's marker animation, else idle."""
        hour = self.worldview.hour if hour is None else hour
        pk = npc.actor.package_at(hour) if npc else None
        if pk is not None and pk.anim:
            return self.marker_sequence(pk.anim)
        mk = self.marker_of_package(pk) if pk is not None else None
        if mk is not None and mk.anim:
            return self.marker_sequence(mk.anim)
        return "ACT_IDLE"

    def marker_of_package(self, pk):
        """A marker ref of the current map a package uses (use_marker by name, sleep / eat / sit kinds), or None."""
        if self.layer is None or pk is None:
            return None
        name = pk.ref if pk.type in ("use_marker", "sleep", "eat", "sandbox") or pk.location == "ref" else ""
        kinds = tuple(pk.markers) or {"sleep": ("sleep",), "eat": ("eat", "sit")}.get(pk.type, ())
        for r in self.layer.refs:
            if r.type != "marker":
                continue
            if name and r.name == name:
                return r
        for r in self.layer.refs:
            if r.type == "marker" and kinds and r.kind in kinds:
                return r
        return None

    def marker_sequence(self, anim_name):
        """actbusy name -> its busy sequence / activity (scripts/actbusy.txt); an ACT_ / sequence name as is."""
        if not anim_name:
            return ""
        if anim_name.upper().startswith("ACT_"):
            return anim_name
        if getattr(self, "_actbusy", None) is None:
            from ..world.anim import actbusy_sequences
            self._actbusy = actbusy_sequences(self.fs) if self.fs is not None else {}
        busy = self._actbusy.get(anim_name.lower())
        return busy[0] if busy and busy[0] else anim_name

    # --- sitting markers of the animation library: fit to the furniture
    def seat_params(self):
        """{actbusy name: numbers} of scripts/hl2rpm_anim_seats.txt (make_anims.py)"""
        if getattr(self, "_seat_params", None) is None:
            from ..world import seatfit
            self._seat_params = seatfit.load_params(self.fs)
        return self._seat_params

    def fit_marker_to_furniture(self, ref, radius=110.0):
        """put a sitting marker where its animation lands the character on the nearest seat (a prop of the
        map or of the layer): position, facing, seat height -> (True, message) / (False, why)"""
        from ..world import seatfit
        params = self.seat_params().get((ref.anim or "").lower())
        if not params:
            return False, "У анимации «%s» нет подгонки (нужна анимация библиотеки: rpm_sit_chair, rpm_sit_couch)" % (ref.anim or "—")
        if "over_seat" not in params:
            return False, "«%s» — не мебель: для неё «Прислонить к стене» / «Поставить на пол»" % ref.anim
        cands = []
        if self.scene is not None:
            for ob in self.scene.objects:
                if ob.model and ob.model.endswith(".mdl"):
                    cands.append((ob.model, ob.origin, ob.angles[1]))
        for r in (self.layer.refs if self.layer else []):
            if r.type in ("prop", "container") and r.model:
                cands.append((r.model, r.pos, r.angles[1]))
        best = None
        for model, origin, yaw in cands:
            d = math.hypot(origin[0] - ref.pos[0], origin[1] - ref.pos[1])
            if d > radius or abs(origin[2] - ref.pos[2]) > 80:
                continue
            if best is None or d < best[0]:
                info = self._seat_info(model)
                if info and "seat_h" in info:
                    best = (d, model, origin, yaw, info)
        if best is None:
            return False, "Рядом с маркером (%.0f ед.) нет мебели с сиденьем" % radius
        _d, model, origin, yaw, info = best
        res = seatfit.marker_for(info, origin, yaw, params)
        if res is None:
            return False, "Не удалось найти сиденье у %s" % model
        pos, myaw, h = res
        ref.pos = [round(pos[0], 1), round(pos[1], 1), round(pos[2], 1)]
        ref.angles = [0.0, round(myaw, 1), 0.0]
        ref.seat_h = round(h, 1)
        return True, "%s: сиденье %.1f ед., маркер поставлен" % (model.rsplit("/", 1)[-1], h)

    def fit_marker_to_bed(self, ref, radius=120.0):
        """a lying marker (rpm_lie_bed): the nearest bed (and the mattress on it), the marker at the middle of the
        long side whose left is the headboard (a free side), the mattress height -> (True, message) / (False, why)"""
        import numpy as np
        from ..world import seatfit
        from ..world.scene import model_matrix
        params = self.seat_params().get((ref.anim or "").lower()) or {}
        if self.scene is None:
            return False, "Карта не загружена"
        cands = []
        for ob in self.scene.objects:
            if ob.model and ob.model.endswith(".mdl"):
                cands.append((ob.model, ob.origin, ob.angles))
        for r in (self.layer.refs if self.layer else []):
            if r.type in ("prop", "container") and r.model:
                cands.append((r.model, r.pos, r.angles))
        beds = [c for c in cands if any(k in c[0].lower() for k in ("bed", "cot", "mattress"))
                and math.hypot(c[1][0] - ref.pos[0], c[1][1] - ref.pos[1]) < radius]
        if not beds:
            return False, "Рядом с маркером (%.0f ед.) нет кровати (модели с bed / cot / mattress в имени)" % radius
        main = min(beds, key=lambda c: ("mattress" in c[0].lower(), math.hypot(c[1][0] - ref.pos[0], c[1][1] - ref.pos[1])))
        yaw = math.radians(main[2][1])
        cs, sn = math.cos(yaw), math.sin(yaw)
        o = np.array(main[1], dtype=np.float64)
        tris = []
        for model, origin, angles in beds:
            m = self.scene.model(model)
            if m is None or not m.ok:
                continue
            T = m.triangles().astype(np.float64)
            M = model_matrix(origin, angles).astype(np.float64)
            W = T @ M[:3, :3].T + M[:3, 3]
            # into the main bed's frame (its yaw only): the bed's axes become x / y
            d = W - o
            tris.append(np.stack([d[..., 0] * cs + d[..., 1] * sn, -d[..., 0] * sn + d[..., 1] * cs, d[..., 2]], axis=-1))
        if not tris:
            return False, "Модель кровати не прочитана"
        T = np.concatenate(tris)

        def to_world(v):
            return o + np.array([v[0] * cs - v[1] * sn, v[0] * sn + v[1] * cs, v[2]])

        def blocked(pos, out):
            wp = to_world(pos)
            wd = np.array([out[0] * cs - out[1] * sn, out[0] * sn + out[1] * cs, 0.0])
            start = wp - wd * 10.0 + np.array([0.0, 0.0, 30.0])
            return self.scene.cast(start, wd, 30.0) is not None
        res = seatfit.bed_marker(T, params, near=None, blocked=blocked)
        if res is None:
            return False, "Не найден матрас у %s" % main[0]
        pos = to_world(res["pos"])
        pos = self.scene.drop_to_floor([float(pos[0]), float(pos[1]), float(pos[2]) + 2.0])
        ref.pos = [round(pos[0], 1), round(pos[1], 1), round(pos[2], 1)]
        ref.angles = [0.0, round((res["yaw"] + math.degrees(yaw)) % 360.0, 1), 0.0]
        ref.seat_h = round(res["h"], 1)
        msg = "%s: матрас %.1f ед., маркер у длинной стороны" % (main[0].rsplit("/", 1)[-1], res["h"])
        if not res["head_ok"]:
            msg += "; со стороны изголовья не встать - голова будет в ногах кровати"
        if res["blocked"]:
            msg += "; обе стороны заняты - проверьте место"
        return True, msg

    def fit_marker_to_wall(self, ref, radius=72.0):
        """a leaning marker (rpm_lean_wall): find the nearest wall around the marker (rays at the height of the
        back), stand wall_dist in front of it, facing away from it, on the floor -> (True, message) / (False, why)"""
        import numpy as np
        params = self.seat_params().get((ref.anim or "").lower()) or {}
        dist = float(params.get("wall_dist", 14.0))
        if self.scene is None:
            return False, "Карта не загружена"
        base = self.scene.drop_to_floor(list(ref.pos))
        best = None
        yaw0 = math.radians(ref.angles[1] if ref.angles else 0.0)
        for k in range(24):
            a = yaw0 + math.pi + k * (2.0 * math.pi / 24.0)          # behind the marker first
            d = np.array([math.cos(a), math.sin(a), 0.0])
            hits = []
            for z in (30.0, 50.0):
                r = self.scene.cast(np.array(base, float) + [0, 0, z], d, radius)
                if r is None or abs(r[1][2]) > 0.3:
                    break
                hits.append(r)
            if len(hits) < 2 or abs(hits[0][0] - hits[1][0]) > 6.0:
                continue                                              # not a wall at both heights
            t = 0.5 * (hits[0][0] + hits[1][0])
            if best is None or t < best[0] - 0.5:
                n = np.array(hits[0][1], float)
                if n @ d > 0:
                    n = -n
                best = (t, d, n)
        if best is None:
            return False, "В %.0f ед. от маркера нет стены (нужна вертикальная поверхность на высоте спины)" % radius
        t, d, n = best
        n2 = np.array([n[0], n[1], 0.0])
        n2 /= max(1e-6, np.linalg.norm(n2))
        hit = np.array(base, float) + d * t
        pos = hit + n2 * dist
        pos = self.scene.drop_to_floor([float(pos[0]), float(pos[1]), float(base[2])])
        ref.pos = [round(pos[0], 1), round(pos[1], 1), round(pos[2], 1)]
        ref.angles = [0.0, round(math.degrees(math.atan2(n2[1], n2[0])) % 360.0, 1), 0.0]
        return True, "Маркер у стены: %.0f ед. от неё, спиной к ней" % dist

    def _seat_info(self, model):
        cache = getattr(self, "_seat_cache", None)
        if cache is None:
            cache = self._seat_cache = {}
        if model not in cache:
            from ..world import seatfit
            info = None
            try:
                m = self.scene.model(model) if self.scene is not None else None
                if m is not None and m.ok:
                    info = seatfit.analyze(m.triangles())
            except Exception:
                info = None
            cache[model] = info
        return cache[model]

    def view_marker_anim(self, r):
        from ..worlddata import NPC_DEFAULT_MODEL
        model = NPC_DEFAULT_MODEL.get("npc_citizen", "")
        owner = self.project.npc(r.owner) if r.owner else None
        if owner is not None:
            model = owner.actor.model_or_default()
        if not model:
            model = "models/humans/group01/male_07.mdl"
        return self.view_model(model, 0, self.marker_sequence(r.anim), "Анимация маркера «%s»" % (r.name or r.kind))

    def _place_at(self, payload, pos):
        self.place_payload = payload
        self.on_place(list(pos), [0, 0, 1])

    def _top_view(self, pos):
        if pos is None:
            return
        cam = self.worldview.cam
        cam.pos[:] = [pos[0], pos[1], pos[2] + 1400]
        cam.pitch = -89.0
        self.worldview.update()

    # ------------------------------------------------------------------ NPC helpers
    def open_npc(self, tn, tab=None):
        npc = self.project.npc(tn)
        if npc is None:
            return
        if npc is not self.npc:
            self.select_npc(npc)
        if tab is not None:
            self.inspector.npc_tab = tab
        elif self.inspector.npc_tab == 0 and self.central_tabs.currentWidget() is self.map_panel:
            self.inspector.npc_tab = 1
        self.worldview.focus_npc = tn
        self.worldview.invalidate_overlay()
        self.inspector.show_target(("npc",))

    def npc_from_hammer(self, tn, cls, e):
        if self.project.npc(tn):
            self.open_npc(tn)
            return
        npc = self._make_npc(tn, e.get("loot_displayname", ""))
        npc.actor.classname = cls
        mdl = e.get("model", "")
        if mdl.endswith(".mdl") and mdl.lower() != wd.NPC_DEFAULT_MODEL.get(cls, ""):
            npc.actor.model = mdl.lower()
        if e.get("ftag"):
            npc.actor.faction = e["ftag"]
        self.project.npcs.append(npc)
        self.model_changed(True)
        self.refresh_palette()
        self.select_npc(npc)
        self.open_npc(tn, tab=2)

    def human_models(self):
        return HUMAN_MODELS

    def model_names(self):
        if self.fs is None:
            return []
        if self._model_names is None:
            QApplication.setOverrideCursor(Qt.WaitCursor)
            try:
                self._model_names = self.fs.listdir("models", ".mdl")
            finally:
                QApplication.restoreOverrideCursor()
        return self._model_names

    def thumbnailer(self):
        if self.fs is None or self.matsys is None:
            return None
        if getattr(self, "_thumbs", None) is None or self._thumbs.fs is not self.fs:
            self._thumbs = Thumbnailer(self.fs, self.matsys)
        return self._thumbs

    def browse_model(self, current, prefix="models/"):
        if self.fs is None:
            return ""
        dlg = ModelBrowser(self, self, self.model_names, current)
        if dlg.exec() == QDialog.Accepted:
            return dlg.value()
        return ""

    # --- the "Модели" tab: every model of the game, drag one onto the map
    def _build_models_tab(self):
        w = QWidget()
        l = QVBoxLayout(w)
        l.setContentsMargins(0, 0, 0, 0)
        row = QHBoxLayout()
        row.setContentsMargins(4, 4, 4, 0)
        row.addWidget(QLabel("Ставить как:"))
        self.models_as = QComboBox()
        for data, text in (("vmf:prop_static", "статический проп в карту (F9)"), ("vmf:prop_dynamic", "динамический проп в карту (F9)"),
                           ("vmf:prop_physics", "физический проп в карту (F9)"), ("prop", "проп программы (без сборки)"),
                           ("container", "контейнер с лутом")):
            self.models_as.addItem(text, data)
        row.addWidget(self.models_as, 1)
        l.addLayout(row)
        self.models_grid = ModelGrid(self, self.model_names, self._model_payload)
        self.models_grid.activated.connect(lambda m: self.start_place(self._model_payload(m)))
        l.addWidget(self.models_grid, 1)
        h = QLabel("Перетащите модель на карту или дважды щёлкните, затем щёлкните на карте.")
        h.setObjectName("hint")
        h.setWordWrap(True)
        l.addWidget(h)
        return w

    def _model_payload(self, model):
        kind = self.models_as.currentData() or "prop"
        if kind.startswith("vmf:"):
            return ["vmf", kind[4:], model]
        return [kind, model]

    def on_world_drop(self, payload, pos, normal):
        if not payload or self.layer is None:
            return
        payload = tuple(payload)
        if payload[0] == "vmf" and self.scene is not None and self.scene.vmf is None:
            QMessageBox.information(self, "В карту", "У этой карты нет VMF — поставлю объект программы.")
            payload = ("prop", payload[2])
        self.place_payload = payload
        self.on_place(list(pos), list(normal))

    def model_sequences(self, model):
        if self.fs is None or not model:
            return None
        key = model.lower()
        s = self._seq_cache.get(key)
        if s is None:
            seqs = mdlmod.sequence_names(self.fs, key)
            s = set()
            for name, act in seqs:
                s.add(name.lower())
                if act:
                    s.add(act.lower())
            self._seq_cache[key] = s
        return s

    def preview_hour(self):
        return self.map_panel.hour.value() / 4.0

    def set_preview_hour(self, h):
        self.map_panel.hour.setValue(int(round(h * 4)) % 96)

    def _hour_changed(self, v):
        h = v / 4.0
        self.map_panel.hour_lbl.setText("%02d:%02d" % (int(h), int((h % 1) * 60)))
        self.worldview.hour = h
        self.worldview.invalidate_overlay()

    # ------------------------------------------------------------------ factions
    def new_faction(self, assign=None):
        fid, ok = QInputDialog.getText(self, "Новая фракция", "id фракции (ftag — латиница, без пробелов):", text="rebels")
        fid = (fid or "").strip()
        if not ok or not fid:
            return
        if not TARGETNAME_RE.match(fid) or self.project.faction(fid):
            QMessageBox.warning(self, "Фракция", "Недопустимые символы или уже есть")
            return
        f = wd.Faction(fid)
        self.project.factions.append(f)
        if assign is not None:
            assign.faction = fid
        self.model_changed(True)
        self.refresh_faction_list()
        self.inspector.show_target(("faction", fid) if assign is None else self.inspector.target)

    def rename_faction(self, fac, new):
        old = fac.id
        fac.id = new
        for n in self.project.npcs:
            if n.actor.faction == old:
                n.actor.faction = new
        for f in self.project.factions:
            if old in f.relations:
                f.relations[new] = f.relations.pop(old)
        self.model_changed(True)
        self.refresh_faction_list()
        self.inspector.show_target(("faction", new))

    def delete_faction(self, fac):
        users = [n.targetname for n in self.project.npcs if n.actor.faction == fac.id]
        if QMessageBox.question(self, "Фракция", "Удалить фракцию «%s»?%s" % (fac.id, ("\nОна указана у NPC: " + ", ".join(users)) if users else "")) != QMessageBox.Yes:
            return
        self.project.factions.remove(fac)
        for f in self.project.factions:
            f.relations.pop(fac.id, None)
        self.model_changed(True)
        self.refresh_faction_list()
        self.inspector.show_target(None)

    # ------------------------------------------------------------------ navigation
    def nav_edit_mode(self):
        if self.layer is None:
            return
        self.map_panel.toggles["nav"].setChecked(True)
        self.worldview.set_mode("nav", "Узлы навигации: клик по полу — новый узел · тащить — двигать · Delete — удалить · Esc — выйти")
        self.worldview.setFocus()

    def nav_clear(self):
        if self.layer is None or not self.layer.nav:
            return
        if QMessageBox.question(self, "Навигация", "Удалить все %d узлов карты %s?" % (len(self.layer.nav), self.cur_map)) != QMessageBox.Yes:
            return
        self.layer.nav = []
        self.model_changed(False, moved=True)
        self.world_changed("nav")

    def nav_generate(self):
        if self.layer is None or not self.project:
            QMessageBox.information(self, "Навигация", "Откройте карту.")
            return
        s = self.project.settings
        if not os.path.isfile(s.hl2_exe):
            QMessageBox.warning(self, "Навигация", "Не найден hl2.exe (Проект → Настройки).")
            return
        sp, ok = QInputDialog.getInt(self, "Навигация", "Шаг сетки узлов (единиц; 128 — улицы и комнаты, 96 — тесные помещения):",
                                     self.layer.nav_spacing or 128, 48, 512, 16)
        if not ok:
            return
        if QMessageBox.question(self, "Навигация", "Игра загрузит карту %s, разложит узлы там, куда может пройти NPC "
                                "(от игрока, NPC и маркеров), и закроется. Это займёт около минуты.\n\nПродолжить?" % self.cur_map) != QMessageBox.Yes:
            return
        rel = "cache/ck_nav_%s.txt" % self.cur_map
        out = os.path.join(s.mod_dir, rel)
        try:
            if os.path.exists(out):
                os.remove(out)
        except OSError:
            pass
        # rpm_nav_autogen: the game generates 3 s after the map is loaded and quits
        args = [s.hl2_exe, "-game", s.mod_dir, "-novid", "-windowed", "-w", "1024", "-h", "640",
                "+rpm_nav_autogen", "%d 6000 %s" % (sp, rel), "+map", self.cur_map]
        self.logln("Навигация: " + " ".join(args))
        proc = subprocess.Popen(args, cwd=os.path.dirname(s.hl2_exe))
        prog = QProgressDialog("Игра раскладывает узлы навигации на %s…" % self.cur_map, "Отмена", 0, 0, self)
        prog.setWindowTitle("Навигация")
        prog.setWindowModality(Qt.WindowModal)
        prog.show()
        t0 = time.time()
        while True:
            QApplication.processEvents()
            time.sleep(0.1)
            if prog.wasCanceled():
                proc.kill()
                break
            if os.path.isfile(out) and proc.poll() is not None:
                break
            if proc.poll() is not None and time.time() - t0 > 5:
                break
            if time.time() - t0 > 600:
                proc.kill()
                break
        prog.close()
        if not os.path.isfile(out):
            QMessageBox.warning(self, "Навигация", "Игра не записала узлы (%s). Проверьте, что server.dll мода свежий." % rel)
            return
        nodes = world_import.read_nav(out)
        try:
            os.remove(out)
        except OSError:
            pass
        self.layer.nav = nodes
        self.layer.nav_spacing = sp
        self.model_changed(False, moved=True)
        self.world_changed("nav")
        self.map_panel.toggles["nav"].setChecked(True)
        self.logln("Навигация %s: %d узлов (шаг %d). Сохраните проект — игра построит граф при следующей загрузке карты." % (self.cur_map, len(nodes), sp))
        QMessageBox.information(self, "Навигация", "Узлов: %d.\nСохраните проект (Ctrl+S): при следующей загрузке карты игра построит по ним граф пути "
                                "(в консоли «Node Graph out of Date. Rebuilding…»)." % len(nodes))

    # ------------------------------------------------------------------ game
    def run_game_here(self, pos=None):
        if not self.project or not self.cur_map:
            QMessageBox.information(self, "Игра", "Откройте карту.")
            return
        s = self.project.settings
        if not os.path.isfile(s.hl2_exe):
            QMessageBox.warning(self, "Игра", "Не найден hl2.exe: %s (Проект → Настройки)" % s.hl2_exe)
            return
        if self.dirty and not self.save():
            return
        cam = self.worldview.cam
        if pos is None:
            p = cam.pos - [0, 0, 64]
            if self.scene is not None:
                p = self.scene.drop_to_floor(list(cam.pos), up=0, down=8192)
        else:
            p = pos
        # rpm_start: the game puts the player there once the map is loaded (Source pitch is down-positive)
        start = "%.0f %.0f %.0f %.0f %.0f %.2f" % (p[0], p[1], p[2] + 4, -cam.pitch if pos is None else 0, cam.yaw, self.preview_hour())
        from .test_state_dialog import launch_args
        args = [s.hl2_exe, "-game", s.mod_dir, "-dev", "-console", "-novid", "+dialog_language", self.display_lang,
                "+rpm_start", start] + launch_args(self.project) + ["+map", self.cur_map]
        self.logln("Запуск: " + " ".join('"%s"' % a if " " in a else a for a in args))
        subprocess.Popen(args, cwd=os.path.dirname(s.hl2_exe))

    # ------------------------------------------------------------------ the map's VMF (objects placed in Hammer)
    def _vmf_dirs(self):
        out = []
        for d in (self.project.settings.maps_dirs if self.project else []):
            out += [d, os.path.join(d, "test")]
        return out

    def _vmf_doc(self, name):
        """The VMF of a map (kept between map switches so unsaved edits survive), or None."""
        doc = self.vmfdocs.get(name)
        if doc is not None:
            if doc.changed_on_disk() and not doc.dirty:
                try:
                    doc.load()
                    self.logln("VMF %s изменён вне программы (Hammer) — перечитан" % os.path.basename(doc.path))
                except OSError as e:
                    self.logln("VMF %s: %s" % (doc.path, e))
            return doc
        path = find_vmf(name, self._vmf_dirs())
        if not path:
            return None
        try:
            doc = VMFDoc(path)
        except Exception as e:
            self.logln("VMF %s не прочитан: %s" % (path, e))
            return None
        self.vmfdocs[name] = doc
        return doc

    def sync_scene_from_vmf(self):
        """After undo / redo: rebuild the map objects from the VMF state."""
        if self.scene is None or self.scene.vmf is None:
            return
        self.scene.objects = []
        doc = self.scene.vmf
        for ent in doc.live():
            if ent.get("origin"):
                self.scene.add_object(ent)
        self.worldview.refresh_objects()
        self.worldview._prune_selection()
        self.refresh_cell_view()

    def _place_vmf(self, d, pos, normal):
        sc = self.scene
        if sc is None or sc.vmf is None:
            return
        cls, mdl = d[1], d[2] or ""
        cam = self.worldview.cam.pos
        yaw = round(math.degrees(math.atan2(cam[1] - pos[1], cam[0] - pos[0])) / 15) * 15 % 360
        keys = {}
        ang = [0, yaw, 0]
        p = list(pos)
        if cls.startswith("prop_"):
            keys = {"model": mdl, "skin": "0", "solid": "6"}
            if cls == "prop_static":
                keys.update({"fademindist": "-1", "fadescale": "1"})
            sc.model(mdl)
            p[2] += sc.lowest_offset(mdl, ang)
        elif cls in ("light_deferred", "light_deferred_spot"):
            keys = dict(NEW_LIGHT_KEYS)
            keys["light_type"] = "1" if cls.endswith("spot") else "0"
            if cls.endswith("spot"):
                ang = [60, yaw, 0]
            p[2] += 96 if abs(normal[2]) > 0.5 else 0
            if abs(normal[2]) <= 0.5:
                p = [p[0] + normal[0] * 16, p[1] + normal[1] * 16, p[2]]
        else:
            p[2] += 8
        ent = sc.vmf.add(cls, p, ang, **keys)
        ob = sc.add_object(ent)
        self.worldview.refresh_objects()
        self.model_changed(False, moved=True)
        self.world_changed("place")
        self.worldview.select(("obj", ob.key))
        self.logln("В карту: %s %s (VMF изменён — сохраните и соберите карту, F9)" % (cls, mdl))

    def delete_targets(self, targets):
        n = 0
        for t in targets:
            if t[0] == "ref":
                r = self.layer.ref(t[1]) if self.layer else None
                if r is not None and r in self.layer.refs:
                    self.layer.refs.remove(r)
                    n += 1
            elif t[0] == "obj" and self.scene is not None:
                ob = self.scene.find(t[1])
                if ob is not None and ob.editable:
                    self.scene.remove_object(ob)
                    n += 1
        if not n:
            return
        self.worldview.select(None, emit=False)
        self.worldview.refresh_objects()
        self.inspector.show_target(("npc",) if self.npc else None)
        self.model_changed(False, moved=True)
        self.world_changed("delete")
        self.statusBar().showMessage("Удалено объектов: %d" % n, 4000)

    # --- copy / paste (also between maps)
    def _copy_items(self, targets):
        items = []
        for t in targets:
            if t[0] == "ref":
                r = self.layer.ref(t[1]) if self.layer else None
                if r is not None:
                    items.append({"ref": r.to_dict()})
            elif t[0] == "obj" and self.scene is not None:
                ob = self.scene.find(t[1])
                if ob is not None and ob.ent is not None:
                    items.append({"ent": [list(x) for x in ob.ent.kv]})
        return items

    def copy_world_selected(self):
        sel = [x for x in self.worldview.selset if x[0] in ("ref", "obj")]
        items = self._copy_items(sel)
        if not items:
            return
        self._clip = items
        QApplication.clipboard().setText(json.dumps({"hl2rpm_ck": items}, ensure_ascii=False))
        self.statusBar().showMessage("Скопировано объектов: %d — Ctrl+V вставит под курсор (и на другой карте)" % len(items), 5000)

    def paste_world(self, pos=None):
        items = self._clip
        if not items:
            try:
                d = json.loads(QApplication.clipboard().text())
                items = d.get("hl2rpm_ck")
            except (ValueError, AttributeError):
                items = None
        if not items:
            return
        self._paste_items(items, at=pos)

    def _paste_items(self, items, offset=None, at=None):
        if self.layer is None:
            return
        # anchor = the first item's position
        def item_pos(it):
            if "ref" in it:
                r = it["ref"]
                pts = r.get("points") or []
                return list(r.get("pos") or (pts[0]["pos"] if pts else [0, 0, 0]))
            for k, v in it["ent"]:
                if k == "origin":
                    return [float(x) for x in v.split()[:3]]
            return [0, 0, 0]
        anchor = item_pos(items[0])
        off = list(offset) if offset is not None else ([at[i] - anchor[i] for i in range(3)] if at is not None else [48, 0, 0])
        new = []
        skipped = 0
        for it in items:
            if "ref" in it:
                r = wd.Ref.from_dict(dict(it["ref"]))
                r.id = self.layer.new_id()
                r.pos = [r.pos[i] + off[i] for i in range(3)]
                for pt in r.points:
                    pt.pos = [pt.pos[i] + off[i] for i in range(3)]
                if r.name:
                    r.name = self._unique_name(r.name)
                self.layer.refs.append(r)
                new.append(("ref", r.id))
            else:
                if self.scene is None or self.scene.vmf is None:
                    skipped += 1
                    continue
                kv = [list(x) for x in it["ent"] if x[0] != "id"]
                cls = next((v for k, v in kv if k == "classname"), "info_target")
                ent = self.scene.vmf.add(cls, [0, 0, 0])
                for k, v in kv:
                    if k != "classname":
                        ent.set(k, v)
                p = item_pos(it)
                ent.origin = [p[i] + off[i] for i in range(3)]
                ob = self.scene.add_object(ent)
                new.append(("obj", ob.key))
        self.worldview.refresh_objects()
        self.model_changed(False, moved=True)
        self.world_changed("paste")
        self.worldview.select_many(new)
        msg = "Вставлено объектов: %d" % len(new)
        if skipped:
            msg += " (объекты Hammer пропущены: у карты нет VMF)"
        self.statusBar().showMessage(msg, 5000)

    def _gizmo_mode_changed(self, mode):
        mp = self.map_panel
        mp.giz_move.setChecked(mode == "move")
        mp.giz_rot.setChecked(mode == "rotate")

    # --- save the VMF / build the map
    def dirty_vmfs(self):
        return [d for d in self.vmfdocs.values() if d.dirty]

    def save_vmfs(self):
        """Write the changed VMFs (with a .bak copy). False if the user cancelled."""
        for name, doc in list(self.vmfdocs.items()):
            if not doc.dirty:
                continue
            if doc.changed_on_disk():
                r = QMessageBox.question(self, "VMF изменён в Hammer",
                                         "%s сохранили в Hammer после того, как программа его прочитала.\n\n"
                                         "Да — записать правки программы поверх (изменения Hammer в этих сущностях пропадут, "
                                         "остальное — нет: программа меняет только свои сущности).\n"
                                         "Нет — не сохранять VMF сейчас." % os.path.basename(doc.path),
                                         QMessageBox.Yes | QMessageBox.No | QMessageBox.Cancel)
                if r == QMessageBox.Cancel:
                    return False
                if r == QMessageBox.No:
                    continue
                st = doc.state()
                doc.load()
                doc.restore(st)
            try:
                bak = doc.save()
            except OSError as e:
                QMessageBox.critical(self, "VMF", "Не удалось записать %s:\n%s" % (doc.path, e))
                return False
            self.logln("VMF %s сохранён (копия: %s). Соберите карту (F9), чтобы изменения появились в игре." % (doc.path, os.path.basename(bak) if bak else "—"))
        if self.scene is not None and self.scene.vmf is not None:
            self.refresh_cell_view()
        return True

    def build_map(self):
        if self.scene is None or not self.cur_map:
            QMessageBox.information(self, "Сборка", "Откройте карту.")
            return
        doc = self.scene.vmf
        if doc is None:
            QMessageBox.information(self, "Сборка", "Для карты %s не найден VMF (Проект → Настройки → папки исходников карт)." % self.cur_map)
            return
        if self._build_proc is not None:
            QMessageBox.information(self, "Сборка", "Карта уже собирается — см. «Журнал».")
            return
        kind = doc.kinds_changed()
        if doc.dirty and not self.save_vmfs():
            return
        bin_dir = os.path.join(os.path.dirname(self.project.settings.hl2_exe), "bin") if self.project.settings.hl2_exe else SDK_BIN
        exe = {k: os.path.join(bin_dir, "hl2rpm_%s.exe" % k) for k in ("vbsp", "vvis", "vrad")}
        if not all(os.path.isfile(x) for x in exe.values()):
            exe = {k: os.path.join(bin_dir, "%s.exe" % k) for k in ("vbsp", "vvis", "vrad")}
        base = os.path.splitext(doc.path)[0]
        game = self.project.settings.mod_dir
        if not kind:
            r = QMessageBox.question(self, "Сборка", "Правок объектов Hammer нет. Собрать карту %s полностью?" % self.cur_map)
            if r != QMessageBox.Yes:
                return
            kind = "full"
        if kind == "ents" and not os.path.isfile(base + ".bsp"):
            kind = "full"      # -onlyents needs the previous BSP next to the VMF
        if kind == "ents":
            steps = [(exe["vbsp"], ["-game", game, "-onlyents", base])]
        else:
            steps = [(exe["vbsp"], ["-game", game, base]), (exe["vvis"], ["-game", game, base]), (exe["vrad"], ["-game", game, "-both", base])]
        self.logln("Сборка %s (%s): %d шаг(а)" % (self.cur_map, "только сущности" if kind == "ents" else "полная", len(steps)))
        self.map_panel.build.setEnabled(False)
        self.map_panel.build.setText("⚙ Собирается…")
        self._build_t0 = time.time()
        self._build_steps = steps
        self._build_base = base
        self._run_build_step()

    def _run_build_step(self):
        if not self._build_steps:
            self._build_done(True)
            return
        exe, args = self._build_steps.pop(0)
        p = QProcess(self)
        p.setWorkingDirectory(os.path.dirname(exe))
        p.setProcessChannelMode(QProcess.MergedChannels)
        self._build_proc = p
        self._build_leak = False

        def out():
            data = bytes(p.readAll()).decode("utf-8", "replace")
            for line in data.splitlines():
                low = line.lower()
                if "leaked" in low:
                    self._build_leak = True
                if any(k in low for k in ("error", "warning", "leak", "elapsed", "too many", "exceeded", "***")):
                    self.logln("    " + line.strip())

        def done(code, status):
            self._build_proc = None
            out()
            if code != 0 or self._build_leak:
                self.logln("  %s завершился с кодом %d%s" % (os.path.basename(exe), code, " — карта протекает (leak)" if self._build_leak else ""))
                self._build_done(False)
                return
            self._run_build_step()
        p.readyRead.connect(out)
        p.finished.connect(done)
        self.logln("  ==> %s %s" % (os.path.basename(exe), " ".join('"%s"' % a if " " in a else a for a in args)))
        p.start(exe, args)

    def _build_done(self, ok):
        self.map_panel.build.setEnabled(True)
        self.map_panel.build.setText("⚙ Собрать карту")
        if not ok:
            QMessageBox.warning(self, "Сборка", "Сборка %s не удалась — подробности во вкладке «Журнал»." % self.cur_map)
            return
        bsp = self._build_base + ".bsp"
        name = os.path.basename(self._build_base)
        s = self.project.settings
        test_dirs = [os.path.normcase(os.path.join(d, "test")) for d in s.maps_dirs]
        is_test = os.path.normcase(os.path.dirname(self._build_base)) in test_dirs or \
            os.path.isfile(os.path.join(s.mod_dir, "testmaps", "maps", name + ".bsp"))
        dest = os.path.join(s.mod_dir, "testmaps" if is_test else "", "maps", name + ".bsp")
        try:
            if os.path.isfile(dest):
                os.makedirs(MAP_BACKUPS, exist_ok=True)
                shutil.copy2(dest, os.path.join(MAP_BACKUPS, "%s_%s.bsp" % (name, time.strftime("%Y%m%d_%H%M%S"))))
            os.makedirs(os.path.dirname(dest), exist_ok=True)
            shutil.copy2(bsp, dest)
        except OSError as e:
            QMessageBox.warning(self, "Сборка", "Карта собрана, но не скопирована в мод (игра запущена?):\n%s" % e)
            return
        self.logln("Карта %s собрана за %.0f с → %s" % (name, time.time() - self._build_t0, dest))
        if self.cur_map == name:
            self.open_map(name, force=True)

    def import_world(self):
        if not self.project:
            return
        log = []
        n_npc, n_maps = world_import.import_world(self.project, self.project.settings.game_dir, log)
        from .. import rpg_export
        rpg_export.import_rpg(self.project, self.project.settings.game_dir, log)
        self.refresh_item_tree()
        self.logln("Импорт мира: " + "; ".join(log))
        self.model_changed(True)
        self.refresh_npc_list()
        self.refresh_palette()
        self.refresh_faction_list()
        self.refresh_map_list()
        if self.cur_map:
            self.layer = self.project.layer(self.cur_map, create=True)
            self.worldview.set_layer(self.layer)
            self.refresh_cell_view()
        QMessageBox.information(self, "Импорт мира", "NPC: %d, карт: %d, фракций: %d" % (n_npc, n_maps, len(self.project.factions)))
