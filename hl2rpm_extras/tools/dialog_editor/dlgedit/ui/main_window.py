"""Main window of the HL2RPM Creation Kit (dialogs, NPCs, AI packages, world layer)."""
import glob
import json
import os
import re
import subprocess
import sys
import time

from PySide6.QtCore import QPointF, QSettings, Qt, QTimer, QSize
from PySide6.QtGui import QAction, QKeySequence, QColor, QIcon, QPixmap, QPainter, QFont
from PySide6.QtWidgets import (QApplication, QComboBox, QDialog, QDialogButtonBox, QDockWidget, QFileDialog, QHBoxLayout,
                               QInputDialog, QLabel, QLineEdit, QListWidget, QListWidgetItem, QMainWindow, QMenu,
                               QMessageBox, QPlainTextEdit, QProgressDialog, QPushButton, QTabWidget, QToolBar,
                               QToolButton, QTreeWidget, QTreeWidgetItem, QVBoxLayout, QWidget, QSplitter, QStyle)

from .. import audio, exporter, importer, layout
from ..mapscan import MapIndex
from ..model import (LANGUAGE_NAMES, NPC, Node, Option, Project, Quest, Stage, TARGETNAME_RE, media_key_after,
                     short_lang, text_for)
from ..validate import validate
from . import theme
from .graph import GraphScene, GraphView, NodeItem
from .inspector import Inspector
from .settings_dialog import SettingsDialog
from .simulator import SimulatorDialog
from .world_ctrl import WorldMixin

APP_NAME = "HL2RPM Creation Kit"
FILE_FILTER = "Проект HL2RPM (*.hl2dlg)"
DEFAULT_MAPSRC = r"E:\XBLAH's modding tool\content\Mapbase\hl2rpm\mapsrc"
HL2_ITEMS = ["item_healthkit", "item_healthvial", "item_battery", "item_suit", "item_ammo_pistol", "item_ammo_smg1",
             "item_ammo_smg1_grenade", "item_ammo_ar2", "item_ammo_ar2_altfire", "item_box_buckshot", "item_ammo_357",
             "item_ammo_crossbow", "item_rpg_round", "weapon_crowbar", "weapon_stunstick", "weapon_pistol", "weapon_357",
             "weapon_smg1", "weapon_ar2", "weapon_shotgun", "weapon_crossbow", "weapon_rpg", "weapon_frag",
             "weapon_physcannon", "weapon_bugbait"]


def app_icon():
    pm = QPixmap(64, 64)
    pm.fill(Qt.transparent)
    qp = QPainter(pm)
    qp.setRenderHint(QPainter.Antialiasing)
    qp.setBrush(QColor("#34506e"))
    qp.setPen(Qt.NoPen)
    qp.drawRoundedRect(4, 8, 44, 32, 8, 8)
    qp.setBrush(QColor("#ffb84d"))
    qp.drawRoundedRect(18, 26, 42, 30, 8, 8)
    qp.setPen(QColor("#1b1e23"))
    qp.setFont(QFont("Segoe UI", 15, QFont.Bold))
    qp.drawText(18, 26, 42, 30, Qt.AlignCenter, "λ")
    qp.end()
    return QIcon(pm)


class MainWindow(WorldMixin, QMainWindow):
    def __init__(self):
        super().__init__()
        self.setWindowIcon(app_icon())
        self.qs = QSettings("HL2RPM", "DialogEditor")
        self.project = None
        self.npc = None
        self.display_lang = "english"
        self.dirty = False
        self.mapindex = MapIndex()
        self.history = []
        self.hpos = -1
        self.restoring = False
        self.last_audio_dir = ""
        self.search_hits = []
        self.search_pos = 0
        self.sim = None
        self._items_cache = None
        self.snap_timer = QTimer(self)
        self.snap_timer.setSingleShot(True)
        self.snap_timer.setInterval(450)
        self.snap_timer.timeout.connect(self.snapshot)
        self.validate_timer = QTimer(self)
        self.validate_timer.setSingleShot(True)
        self.validate_timer.setInterval(900)
        self.validate_timer.timeout.connect(lambda: self.run_validation(False))

        self.gscene = GraphScene()
        self.view = GraphView(self.gscene)
        self.view.filesDropped = self.files_dropped
        self._build_world_ui()
        self.central_tabs = QTabWidget()
        self.central_tabs.setDocumentMode(True)
        self.central_tabs.addTab(self.map_panel, "Карта")
        self.central_tabs.addTab(self.view, "Диалог")
        self.central_tabs.currentChanged.connect(self._central_changed)
        self.setCentralWidget(self.central_tabs)
        self.gscene.selectedItem.connect(self.on_select)
        self.gscene.itemsMoved.connect(lambda: self.model_changed(False, moved=True))
        self.gscene.connectRequested.connect(self.on_connect)
        self.gscene.createRequested.connect(self.on_create)
        self.gscene.contextRequested.connect(self.on_context)
        self.gscene.editRequested.connect(self.on_edit)
        self.gscene.deleteRequested.connect(self.delete_selected)
        self.view.zoomChanged.connect(self._zoom_label)

        self._build_docks()
        self._build_actions()
        self._build_statusbar()
        self.resize(1600, 950)
        geo = self.qs.value("geometry")
        if geo is not None:
            self.restoreGeometry(geo)
        st = self.qs.value("state")
        if st is not None:
            self.restoreState(st)
        self.update_title()

    # ======================================================================== UI build
    def _build_docks(self):
        # left: NPCs and quests
        left = QTabWidget()
        w = QWidget()
        l = QVBoxLayout(w)
        l.setContentsMargins(4, 4, 4, 4)
        self.npc_filter = QLineEdit()
        self.npc_filter.setPlaceholderText("фильтр NPC…")
        self.npc_filter.textChanged.connect(self.refresh_npc_list)
        l.addWidget(self.npc_filter)
        self.npc_list = QListWidget()
        self.npc_list.currentItemChanged.connect(self._npc_picked)
        self.npc_list.setContextMenuPolicy(Qt.CustomContextMenu)
        self.npc_list.customContextMenuRequested.connect(self._npc_menu)
        l.addWidget(self.npc_list, 1)
        bl = QHBoxLayout()
        b = QPushButton("+ NPC")
        b.clicked.connect(self.new_npc)
        bl.addWidget(b)
        b = QPushButton("С карты…")
        b.setToolTip("Добавить NPC, который стоит на карте (по targetname)")
        b.clicked.connect(self.npc_from_map)
        bl.addWidget(b)
        b = QToolButton(text="⋯")
        b.setPopupMode(QToolButton.InstantPopup)
        m = QMenu(self)
        m.addAction("Дублировать NPC", self.duplicate_npc)
        m.addAction("Удалить NPC", self.delete_npc)
        m.addSeparator()
        m.addAction("Озвучка из папки…", self.voice_from_folder)
        m.addAction("Авто-раскладка", self.auto_layout)
        b.setMenu(m)
        bl.addWidget(b)
        l.addLayout(bl)
        left.addTab(w, "NPC")
        left.addTab(self._build_palette(), "Объекты")
        left.addTab(self._build_cell_view(), "На карте")

        w = QWidget()
        l = QVBoxLayout(w)
        l.setContentsMargins(4, 4, 4, 4)
        self.quest_list = QListWidget()
        self.quest_list.itemClicked.connect(lambda it: self.inspector.show_target(("quest", it.data(Qt.UserRole))))
        l.addWidget(self.quest_list, 1)
        bl = QHBoxLayout()
        b = QPushButton("+ квест")
        b.clicked.connect(lambda: self.new_quest())
        bl.addWidget(b)
        bl.addStretch(1)
        l.addLayout(bl)
        left.addTab(w, "Квесты")
        self.quest_tab = w
        left.addTab(self._build_factions(), "Фракции")
        self.left_tabs = left
        d = QDockWidget("Проект", self)
        d.setObjectName("dock_project")
        d.setWidget(left)
        d.setFeatures(QDockWidget.DockWidgetMovable | QDockWidget.DockWidgetFloatable)
        self.addDockWidget(Qt.LeftDockWidgetArea, d)
        self.dock_left = d

        self.inspector = Inspector(self)
        self.inspector.navigate.connect(self.navigate)
        d = QDockWidget("Свойства", self)
        d.setObjectName("dock_inspector")
        d.setWidget(self.inspector)
        d.setFeatures(QDockWidget.DockWidgetMovable | QDockWidget.DockWidgetFloatable)
        self.addDockWidget(Qt.RightDockWidgetArea, d)
        self.dock_right = d

        bottom = QTabWidget()
        self.problems = QTreeWidget()
        self.problems.setHeaderLabels(["", "NPC", "Где", "Проблема"])
        self.problems.setRootIsDecorated(False)
        self.problems.setColumnWidth(0, 26)
        self.problems.setColumnWidth(1, 140)
        self.problems.setColumnWidth(2, 130)
        self.problems.itemDoubleClicked.connect(self._problem_clicked)
        bottom.addTab(self.problems, "Проверка")
        self.log = QPlainTextEdit()
        self.log.setReadOnly(True)
        bottom.addTab(self.log, "Журнал")
        self.bottom_tabs = bottom
        d = QDockWidget("Проверка и журнал", self)
        d.setObjectName("dock_bottom")
        d.setWidget(bottom)
        d.setFeatures(QDockWidget.DockWidgetMovable | QDockWidget.DockWidgetFloatable)
        self.addDockWidget(Qt.BottomDockWidgetArea, d)
        self.dock_bottom = d
        self.resizeDocks([self.dock_left, self.dock_right], [290, 460], Qt.Horizontal)
        self.resizeDocks([self.dock_bottom], [170], Qt.Vertical)

    def _act(self, text, slot, shortcut=None, tip=""):
        a = QAction(text, self)
        a.triggered.connect(slot)
        if shortcut:
            a.setShortcut(QKeySequence(shortcut))
        if tip:
            a.setToolTip(tip)
            a.setStatusTip(tip)
        self.addAction(a)
        return a

    def _build_actions(self):
        mb = self.menuBar()
        fm = mb.addMenu("&Файл")
        self.a_new = self._act("Новый проект…", self.new_project, "Ctrl+N")
        self.a_open = self._act("Открыть…", self.open_project, "Ctrl+O")
        self.a_save = self._act("Сохранить", self.save, "Ctrl+S", "Сохранить проект и разложить диалоги, озвучку и сцены по папкам игры")
        self.a_saveas = self._act("Сохранить как…", self.save_as, "Ctrl+Shift+S")
        self.a_import = self._act("Импорт диалогов игры…", self.import_dialogs, None, "Загрузить scripts/dialogs/*.txt в проект")
        self.a_import_world = self._act("Импорт мира из игры", self.import_world, None,
                                        "Загрузить scripts/npcs, scripts/world (слои карт, навигация) и фракции из папки игры")
        fm.addActions([self.a_new, self.a_open, self.a_save, self.a_saveas])
        fm.addSeparator()
        fm.addAction(self.a_import)
        fm.addAction(self.a_import_world)
        self.recent_menu = fm.addMenu("Недавние")
        fm.addSeparator()
        fm.addAction(self._act("Выход", self.close))

        em = mb.addMenu("&Правка")
        self.a_undo = self._act("Отменить", self.undo, "Ctrl+Z")
        self.a_redo = self._act("Повторить", self.redo, "Ctrl+Y")
        em.addActions([self.a_undo, self.a_redo])
        em.addSeparator()
        self.a_newnode = self._act("Новая реплика", lambda: self._in_dialog() and self.on_create(None, self.view.mapToScene(self.view.viewport().rect().center())), "N")
        self.a_dup = self._act("Дублировать", self.duplicate_selected, "Ctrl+D")
        self.a_del = self._act("Удалить выделенное", self.delete_selected)
        em.addActions([self.a_newnode, self.a_dup, self.a_del])
        em.addSeparator()
        self.a_find = self._act("Найти…", lambda: (self.search.setFocus(), self.search.selectAll()), "Ctrl+F")
        em.addAction(self.a_find)

        vm = mb.addMenu("&Вид")
        vm.addAction(self._act("Карта", lambda: self.central_tabs.setCurrentWidget(self.map_panel), "Ctrl+1"))
        vm.addAction(self._act("Диалог", lambda: self.central_tabs.setCurrentWidget(self.view), "Ctrl+2"))
        vm.addSeparator()
        self.a_fit = self._act("Вписать всё / к выделенному", self._fit, "F")
        self.a_zin = self._act("Приблизить", lambda: self.view.set_zoom(self.view.zoom() * 1.25), "Ctrl+=")
        self.a_zout = self._act("Отдалить", lambda: self.view.set_zoom(self.view.zoom() / 1.25), "Ctrl+-")
        self.a_z100 = self._act("Масштаб 100%", lambda: self.view.set_zoom(1.0), "Ctrl+0")
        self.a_layout = self._act("Авто-раскладка", self.auto_layout, "Ctrl+L")
        vm.addActions([self.a_fit, self.a_zin, self.a_zout, self.a_z100, self.a_layout])
        vm.addSeparator()
        for d in (self.dock_left, self.dock_right, self.dock_bottom):
            vm.addAction(d.toggleViewAction())

        pm = mb.addMenu("П&роект")
        self.a_settings = self._act("Настройки…", self.edit_settings)
        self.a_validate = self._act("Проверить проект", lambda: self.run_validation(True), "F7")
        self.a_regen = self._act("Пересоздать весь липсинк", self.regenerate_all)
        self.a_rescan = self._act("Перечитать карты", self.rescan_maps)
        self.a_folder = self._act("Открыть папку игры", lambda: self._open_dir(self.project.settings.game_dir if self.project else ""))
        pm.addActions([self.a_settings, self.a_validate, self.a_regen, self.a_rescan, self.a_folder])

        gm = mb.addMenu("&Игра")
        self.a_sim = self._act("Проверить диалог (в редакторе)", self.simulate, "F5", "Пройти диалог прямо в редакторе: озвучка, квесты, условия")
        self.a_run = self._act("Запустить в игре", self.run_game, "F6", "Сохранить и запустить карту: с окна карты — с места камеры, из диалога — карту с этим NPC")
        self.a_reload = self._act("Обновить диалоги и мир в запущенной игре", self.reload_in_game, "F8")
        gm.addActions([self.a_sim, self.a_run, self.a_reload])

        hm = mb.addMenu("Справка")
        hm.addAction(self._act("Управление", self.show_help, "F1"))

        tb = QToolBar("Главная")
        tb.setObjectName("toolbar_main")
        tb.setIconSize(QSize(18, 18))
        tb.setToolButtonStyle(Qt.ToolButtonTextOnly)
        self.addToolBar(tb)
        for a in (self.a_new, self.a_open, self.a_save):
            tb.addAction(a)
        tb.addSeparator()
        tb.addAction(self.a_undo)
        tb.addAction(self.a_redo)
        tb.addSeparator()
        tb.addWidget(QLabel(" Язык: "))
        self.lang_combo = QComboBox()
        self.lang_combo.setToolTip("Язык текстов на холсте")
        self.lang_combo.currentIndexChanged.connect(self._lang_changed)
        tb.addWidget(self.lang_combo)
        tb.addSeparator()
        tb.addAction(self.a_fit)
        tb.addAction(self.a_layout)
        tb.addSeparator()
        tb.addAction(self.a_sim)
        tb.addAction(self.a_run)
        tb.addAction(self.a_validate)
        tb.addSeparator()
        self.search = QLineEdit()
        self.search.setPlaceholderText("Найти в репликах… (Enter — следующая)")
        self.search.setFixedWidth(260)
        self.search.textChanged.connect(self._search)
        self.search.returnPressed.connect(self._search_next)
        tb.addWidget(self.search)
        tb.addAction(self.a_settings)

    def _build_statusbar(self):
        sb = self.statusBar()
        self.st_game = QLabel()
        self.st_game.setObjectName("hint")
        sb.addPermanentWidget(self.st_game)
        zo = QToolButton(text="−")
        zo.clicked.connect(lambda: self.view.set_zoom(self.view.zoom() / 1.25))
        self.st_zoom = QPushButton("100%")
        self.st_zoom.setFlat(True)
        self.st_zoom.setToolTip("Масштаб (колесо мыши). Клик — 100%")
        self.st_zoom.clicked.connect(lambda: self.view.set_zoom(1.0))
        zi = QToolButton(text="+")
        zi.clicked.connect(lambda: self.view.set_zoom(self.view.zoom() * 1.25))
        fit = QToolButton(text="Вписать")
        fit.clicked.connect(self.view.fit_all)
        for x in (zo, self.st_zoom, zi, fit):
            sb.addPermanentWidget(x)

    def _zoom_label(self, z):
        self.st_zoom.setText("%d%%" % round(z * 100))

    # ======================================================================== project
    def set_project(self, project):
        self.project = project
        self.npc = None
        self.dirty = False
        self.history = []
        self.hpos = -1
        self._items_cache = None
        langs = project.languages
        self.display_lang = self.qs.value("display_lang", project.base_language)
        if self.display_lang not in langs:
            self.display_lang = project.base_language
        self.lang_combo.blockSignals(True)
        self.lang_combo.clear()
        for lg in langs:
            self.lang_combo.addItem("%s %s" % (short_lang(lg), LANGUAGE_NAMES.get(lg, lg)), lg)
        self.lang_combo.setCurrentIndex(max(0, self.lang_combo.findData(self.display_lang)))
        self.lang_combo.blockSignals(False)
        self.rescan_maps(quiet=True)
        self.refresh_npc_list()
        self.refresh_quest_list()
        last = project.view.get("_last_npc", "")
        if project.npcs:
            self.select_npc(project.npc(last) or project.npcs[0])
        else:
            self.select_npc(None)
        self.snapshot()
        self.update_title()
        self.update_status()
        self.run_validation(False)
        self.init_world()
        if project.path:
            self._add_recent(project.path)

    def update_title(self):
        name = os.path.basename(self.project.path) if (self.project and self.project.path) else "без имени"
        self.setWindowTitle("%s%s — %s" % (name, " *" if self.dirty else "", APP_NAME))

    def update_status(self):
        if self.project:
            self.st_game.setText("Игра: %s  " % self.project.settings.game_dir)

    def new_project(self):
        if not self.maybe_save():
            return
        start = self.qs.value("last_dir", r"E:\SourceModding\HL2RPMSOURCE")
        path, _ = QFileDialog.getSaveFileName(self, "Новый проект диалогов", os.path.join(start, "dialogs.hl2dlg"), FILE_FILTER)
        if not path:
            return
        p = Project()
        p.path = path
        if os.path.isdir(DEFAULT_MAPSRC):
            p.settings.maps_dirs = [DEFAULT_MAPSRC]
        dlg = SettingsDialog(self, p)
        if dlg.exec() != QDialog.Accepted:
            return
        dlg.apply()
        files = sorted(glob.glob(os.path.join(p.settings.game_dir, "scripts", "dialogs", "*.txt")))
        if files and QMessageBox.question(self, "Импорт", "В папке игры %d файлов диалогов. Загрузить их в проект?" % len(files)) == QMessageBox.Yes:
            log = []
            added = importer.import_files(p, files, log)
            self._finish_import(p, added, log)
        p.save(path)
        self.qs.setValue("last_dir", os.path.dirname(path))
        self.set_project(p)

    def open_project(self, path=None):
        if not self.maybe_save():
            return
        if not path:
            path, _ = QFileDialog.getOpenFileName(self, "Открыть проект", self.qs.value("last_dir", ""), FILE_FILTER)
        if not path:
            return
        try:
            p = Project.load(path)
        except Exception as e:
            QMessageBox.critical(self, "Открыть", "Не удалось открыть %s:\n%s" % (path, e))
            return
        self.qs.setValue("last_dir", os.path.dirname(path))
        self.set_project(p)
        st = p.view.get(self.npc.targetname if self.npc else "", None)
        if st:
            self.view.set_view_state(st)

    def save_as(self):
        if not self.project:
            return
        path, _ = QFileDialog.getSaveFileName(self, "Сохранить проект как", self.project.path or "dialogs.hl2dlg", FILE_FILTER)
        if path:
            self.project.path = path
            self.save()

    def save(self, regenerate_all=False):
        if not self.project:
            return False
        if not self.project.path:
            path, _ = QFileDialog.getSaveFileName(self, "Сохранить проект", "dialogs.hl2dlg", FILE_FILTER)
            if not path:
                return False
            self.project.path = path
        self.snap_timer.stop()
        p = self.project
        self._store_view()
        if self.layer is not None:
            self.layer.camera = self.worldview.camera_state()
        prog = QProgressDialog("Сохранение…", None, 0, 100, self)
        prog.setWindowTitle("Сохранение")
        prog.setWindowModality(Qt.WindowModal)
        prog.setMinimumDuration(300)

        def progress(frac, msg):
            prog.setValue(int(frac * 100))
            prog.setLabelText(msg)
            QApplication.processEvents()
        t0 = time.time()
        ex = exporter.Exporter(p, progress, regenerate_all=regenerate_all)
        try:
            rep = ex.run()
        except Exception as e:
            prog.close()
            QMessageBox.critical(self, "Сохранение", "Ошибка при записи файлов игры:\n%s" % e)
            import traceback
            self.logln(traceback.format_exc())
            return False
        prog.close()
        try:
            p.save()
        except OSError as e:
            QMessageBox.critical(self, "Сохранение", "Не удалось записать проект:\n%s" % e)
            return False
        self.dirty = False
        self.update_title()
        self.logln("Сохранено за %.1f с → %s: %s" % (time.time() - t0, p.settings.game_dir, rep.summary()))
        for x in rep.converted:
            self.logln("  WAV: " + x)
        for x in rep.lipsync:
            self.logln("  липсинк: " + x)
        for x in rep.backups:
            self.logln("  резервная копия старого файла: " + x)
        for x in rep.deleted:
            self.logln("  удалён устаревший: " + x)
        for x in rep.errors:
            self.logln("  ОШИБКА: " + x)
        for x in rep.warnings:
            self.logln("  " + x)
        self.statusBar().showMessage("Сохранено: " + rep.summary(), 8000)
        self.gscene.refresh_all()
        self.inspector.show_target(self.inspector.target)
        self.run_validation(False)
        if rep.errors:
            self.bottom_tabs.setCurrentWidget(self.log)
            QMessageBox.warning(self, "Сохранение", "Сохранено с ошибками (%d) — см. «Журнал»." % len(rep.errors))
        self._snapshot_replace()
        return True

    def maybe_save(self):
        if not self.project or not self.dirty:
            return True
        r = QMessageBox.question(self, APP_NAME, "Сохранить изменения?",
                                 QMessageBox.Save | QMessageBox.Discard | QMessageBox.Cancel)
        if r == QMessageBox.Save:
            return self.save()
        return r == QMessageBox.Discard

    def closeEvent(self, e):
        if not self.maybe_save():
            e.ignore()
            return
        audio.stop()
        self.qs.setValue("geometry", self.saveGeometry())
        self.qs.setValue("state", self.saveState())
        self.qs.setValue("display_lang", self.display_lang)
        super().closeEvent(e)

    def _add_recent(self, path):
        rec = [r for r in (self.qs.value("recent") or []) if r != path]
        rec.insert(0, path)
        rec = rec[:8]
        self.qs.setValue("recent", rec)
        self.qs.setValue("last_project", path)
        self.recent_menu.clear()
        for r in rec:
            self.recent_menu.addAction(r, lambda r=r: self.open_project(r))

    def edit_settings(self):
        if not self.project:
            return
        dlg = SettingsDialog(self, self.project)
        if dlg.exec() == QDialog.Accepted:
            dlg.apply()
            self._items_cache = None
            self.model_changed(True)
            self.set_project_langs()
            self.rescan_maps(quiet=True)
            self.update_status()

    def set_project_langs(self):
        p = self.project
        self.lang_combo.blockSignals(True)
        self.lang_combo.clear()
        for lg in p.languages:
            self.lang_combo.addItem("%s %s" % (short_lang(lg), LANGUAGE_NAMES.get(lg, lg)), lg)
        if self.display_lang not in p.languages:
            self.display_lang = p.base_language
        self.lang_combo.setCurrentIndex(max(0, self.lang_combo.findData(self.display_lang)))
        self.lang_combo.blockSignals(False)
        self.rebuild_scene()
        self.inspector.show_target(self.inspector.target)

    def rescan_maps(self, quiet=False):
        if not self.project:
            return
        t0 = time.time()
        self.mapindex.scan(self.project.settings.maps_dirs)
        if not quiet:
            self.logln("Карты: %d, сущностей с targetname: %d, NPC: %d (%.1f с)" % (
                len(self.mapindex.maps), len(self.mapindex.entities), len(self.mapindex.npcs()), time.time() - t0))
        self.refresh_npc_list()

    # ======================================================================== undo
    def _state(self):
        d = self.project.to_dict()
        d.pop("view", None)
        return json.dumps(d, ensure_ascii=False, sort_keys=True)

    def snapshot(self):
        if not self.project or self.restoring:
            return
        s = self._state()
        if self.hpos >= 0 and self.history[self.hpos] == s:
            return
        self.history = self.history[: self.hpos + 1]
        self.history.append(s)
        if len(self.history) > 150:
            self.history.pop(0)
        self.hpos = len(self.history) - 1

    def _snapshot_replace(self):
        if self.hpos >= 0:
            self.history[self.hpos] = self._state()

    def _restore(self, s):
        self.restoring = True
        sel = self.inspector.target
        cur = self.npc.targetname if self.npc else ""
        d = json.loads(s)
        d["view"] = self.project.view
        p = Project.from_dict(d)
        p.path = self.project.path
        self.project = p
        self.refresh_npc_list()
        self.refresh_quest_list()
        npc = p.npc(cur) or (p.npcs[0] if p.npcs else None)
        self.npc = npc
        self.rebuild_scene()
        self.layer = p.layer(self.cur_map, create=True) if self.cur_map else None
        self.worldview.set_layer(self.layer)
        self.refresh_palette()
        self.refresh_faction_list()
        self.refresh_cell_view()
        self.inspector.show_target(sel)
        if sel and sel[0] in ("node", "option", "returning", "start"):
            self.gscene.select_target(sel)
        self.restoring = False
        self.dirty = True
        self.update_title()
        self.validate_timer.start()

    def undo(self):
        if self.snap_timer.isActive():
            self.snap_timer.stop()
            self.snapshot()
        if self.hpos > 0:
            self.hpos -= 1
            self._restore(self.history[self.hpos])

    def redo(self):
        if self.hpos < len(self.history) - 1:
            self.hpos += 1
            self._restore(self.history[self.hpos])

    # ======================================================================== model changes
    def model_changed(self, structural=False, moved=False):
        if self.restoring or not self.project:
            return
        self.dirty = True
        self.update_title()
        self.snap_timer.start()
        if moved:
            return
        if structural:
            self.gscene.refresh_all()
            self.refresh_quest_list()
        else:
            for it in self.gscene.node_items.values():
                it.refresh()
            for e in self.gscene.entries.values():
                e.update()
        self.refresh_npc_list(keep=True)
        self.validate_timer.start()

    def rebuild_scene(self):
        self.gscene.build(self.project, self.npc, self.display_lang)

    def _lang_changed(self):
        lg = self.lang_combo.currentData()
        if lg:
            self.display_lang = lg
            self.gscene.set_lang(lg)
            self.refresh_npc_list(keep=True)
            self.refresh_quest_list()

    def _store_view(self):
        if self.project and self.npc:
            self.project.view[self.npc.targetname] = self.view.view_state()
            self.project.view["_last_npc"] = self.npc.targetname

    # ======================================================================== NPC list
    def refresh_npc_list(self, keep=False):
        if not self.project:
            self.npc_list.clear()
            return
        flt = self.npc_filter.text().strip().lower()
        self.npc_list.blockSignals(True)
        self.npc_list.clear()
        for n in sorted(self.project.npcs, key=lambda x: x.targetname.lower()):
            name, _ = text_for(n.display_name, self.display_lang, self.project.base_language)
            if flt and flt not in n.targetname.lower() and flt not in (name or "").lower():
                continue
            maps = sorted({e.map for e in self.mapindex.find(n.targetname)} | {m for m, _ in self.project.placements(n.targetname)})
            onmap = (" · " + ", ".join(maps)) if maps else " · нет на картах"
            pk = " · пакетов %d" % len(n.actor.packages) if n.actor.packages else ""
            it = QListWidgetItem("%s\n   %s · %d реплик%s%s" % (n.targetname, name or "(без имени)", len(n.nodes), pk, onmap))
            it.setData(Qt.UserRole, n.targetname)
            self.npc_list.addItem(it)
            if n is self.npc:
                self.npc_list.setCurrentItem(it)
        self.npc_list.blockSignals(False)

    def _npc_picked(self, cur, prev):
        if cur is None or not self.project:
            return
        npc = self.project.npc(cur.data(Qt.UserRole))
        if npc is not self.npc:
            self.select_npc(npc)

    def select_npc(self, npc):
        if self.npc is not None:
            self._store_view()
        self.npc = npc
        self.worldview.focus_npc = npc.targetname if npc is not None else ""
        self.worldview.invalidate_overlay()
        self.rebuild_scene()
        if npc is not None:
            st = self.project.view.get(npc.targetname)
            if st:
                self.view.set_view_state(st)
            else:
                QTimer.singleShot(0, self.view.smart_fit)
        self.inspector.show_target(("npc",))
        self.refresh_npc_list(keep=True)
        self._search(self.search.text())

    def _npc_menu(self, pos):
        it = self.npc_list.itemAt(pos)
        m = QMenu(self)
        m.addAction("+ NPC", self.new_npc)
        if it:
            m.addAction("Дублировать", self.duplicate_npc)
            m.addAction("Удалить", self.delete_npc)
            m.addAction("Озвучка из папки…", self.voice_from_folder)
        m.exec(self.npc_list.mapToGlobal(pos))

    def _make_npc(self, tn, display=""):
        npc = NPC(tn)
        if display:
            npc.display_name[self.project.base_language] = display
        n = Node(0)
        n.pos = [260.0, 0.0]
        n.line = {}
        n.end = True
        npc.nodes.append(n)
        npc.start_pos = [30.0, 6.0]
        npc.returning.pos = [-70.0, 160.0]
        return npc

    def new_npc(self):
        if not self.project:
            return
        names = [e.targetname for e in self.mapindex.npcs() if not self.project.npc(e.targetname)]
        tn, ok = QInputDialog.getItem(self, "Новый NPC", "targetname NPC (как в Hammer):", sorted(set(names)) or [""], 0, True)
        tn = (tn or "").strip()
        if not ok or not tn:
            return
        if not TARGETNAME_RE.match(tn):
            QMessageBox.warning(self, "targetname", "Только латиница, цифры и _ - .")
            return
        if self.project.npc(tn):
            QMessageBox.warning(self, "targetname", "Такой NPC уже есть")
            return
        ents = self.mapindex.find(tn)
        disp = next((e.display for e in ents if e.display), "")
        npc = self._make_npc(tn, disp)
        self.project.npcs.append(npc)
        self.model_changed(True)
        self.select_npc(npc)

    def npc_from_map(self):
        if not self.project:
            return
        if not self.mapindex.maps:
            QMessageBox.information(self, "Карты", "Папки карт не заданы (Проект → Настройки).")
            return
        cands = {}
        for e in self.mapindex.npcs():
            if not self.project.npc(e.targetname):
                cands.setdefault(e.targetname, e)
        if not cands:
            QMessageBox.information(self, "Карты", "Все NPC с targetname на картах уже в проекте.")
            return
        items = ["%s  —  %s, %s%s" % (t, e.map, e.classname, (" «%s»" % e.display) if e.display else "") for t, e in sorted(cands.items())]
        sel, ok = QInputDialog.getItem(self, "NPC с карты", "NPC:", items, 0, False)
        if not ok:
            return
        tn = sel.split("  —  ")[0]
        e = cands[tn]
        npc = self._make_npc(tn, e.display)
        self.project.npcs.append(npc)
        self.model_changed(True)
        self.select_npc(npc)

    def duplicate_npc(self):
        if not self.npc:
            return
        tn, ok = QInputDialog.getText(self, "Дублировать", "targetname копии:", text=self.npc.targetname + "_2")
        tn = (tn or "").strip()
        if not ok or not tn or self.project.npc(tn) or not TARGETNAME_RE.match(tn):
            return
        d = self.npc.to_dict()
        d["targetname"] = tn
        npc = NPC.from_dict(d)
        for n in npc.nodes:
            n.voice = {}
            for o in n.options:
                o.after_voice = {}
        npc.returning.voice = {}
        self.project.npcs.append(npc)
        self.model_changed(True)
        self.select_npc(npc)

    def delete_npc(self):
        if not self.npc:
            return
        if QMessageBox.question(self, "Удалить", "Удалить NPC «%s» из проекта?\nЕго файлы в игре удалятся при сохранении." % self.npc.targetname) != QMessageBox.Yes:
            return
        self.project.npcs.remove(self.npc)
        self.npc = None
        self.model_changed(True)
        self.select_npc(self.project.npcs[0] if self.project.npcs else None)

    def rename_npc(self, npc, new):
        old = npc.targetname
        npc.targetname = new
        for q in self.project.quests:
            if q.owner == old:
                q.owner = new
        if old in self.project.view:
            self.project.view[new] = self.project.view.pop(old)
        self.logln("NPC %s → %s: файлы игры переедут при сохранении" % (old, new))
        self.model_changed(True)
        self.inspector.show_target(("npc",))

    # ======================================================================== quests
    def refresh_quest_list(self):
        self.quest_list.clear()
        if not self.project:
            return
        for q in self.project.quests:
            t, _ = text_for(q.title, self.display_lang, self.project.base_language)
            it = QListWidgetItem("%s\n   %s · этапов %d · %s" % (q.id, t or "(без названия)", len(q.stages), q.owner or "?"))
            it.setData(Qt.UserRole, q.id)
            self.quest_list.addItem(it)

    def new_quest(self, assign=None):
        if not self.project:
            return
        base = (self.npc.targetname + "_quest") if self.npc else "quest"
        qid, ok = QInputDialog.getText(self, "Новый квест", "id квеста (латиница, без пробелов):", text=base)
        qid = (qid or "").strip()
        if not ok or not qid:
            return
        if self.project.quest(qid) or any(c in qid for c in ' "|;~:'):
            QMessageBox.warning(self, "Квест", "Такой id уже есть или содержит пробел/: ; | ~")
            return
        q = Quest(qid)
        q.owner = self.npc.targetname if self.npc else ""
        q.title[self.project.base_language] = qid
        s0 = Stage()
        s0.title[self.project.base_language] = "Выполнить задание"
        s1 = Stage()
        s1.title[self.project.base_language] = "Вернуться"
        q.stages = [s0, s1]
        q.rewards = [["item_healthkit", 1]]
        self.project.quests.append(q)
        if assign:
            setattr(assign[0], assign[1], qid)
        self.model_changed(True)
        self.left_tabs.setCurrentWidget(self.quest_tab)
        self.inspector.show_target(("quest", qid))

    def rename_quest(self, q, new):
        old = q.id
        q.id = new
        for npc in self.project.npcs:
            for n in npc.nodes:
                for o in n.options:
                    for a in ("grant", "complete", "fail"):
                        if getattr(o, a) == old:
                            setattr(o, a, new)
                    for ref in (o.set_stage, o.require_state, o.require_stage):
                        if ref and ref[0] == old:
                            ref[0] = new
        self.model_changed(True)
        self.inspector.show_target(("quest", new))

    def delete_quest(self, q):
        uses = sum(1 for npc in self.project.npcs for n in npc.nodes for o in n.options if q.id in o.quest_refs())
        if QMessageBox.question(self, "Удалить квест", "Удалить квест «%s»?%s" % (q.id, ("\nОн используется в %d ответах — ссылки останутся (ошибка в проверке)." % uses) if uses else "")) != QMessageBox.Yes:
            return
        self.project.quests.remove(q)
        self.model_changed(True)
        self.inspector.show_target(("npc",))

    def item_names(self):
        if self._items_cache is None:
            names = list(HL2_ITEMS)
            path = os.path.join(self.project.settings.mod_dir, "scripts", "inventory_weights.txt") if self.project else ""
            if path and os.path.exists(path):
                from .. import kv
                try:
                    root = kv.parse_file(path)
                    for b in root.children:
                        for c in b.blocks():
                            names.append(c.name)
                except Exception:
                    pass
            self._items_cache = sorted(set(names))
        return self._items_cache

    # ======================================================================== graph actions
    def on_select(self, target):
        if target and target[0] == "edge":
            self.inspector.show_target(target)
            return
        self.inspector.show_target(target)

    def navigate(self, target):
        if target[0] == "quest":
            self.inspector.show_target(target)
            return
        if target[0] in ("node", "option", "returning", "start") and not self._in_dialog():
            self.central_tabs.setCurrentWidget(self.view)
        self.gscene.select_target(target, self.view)
        self.inspector.show_target(target)

    def goto(self, targetname, target):
        npc = self.project.npc(targetname)
        if npc and npc is not self.npc:
            self.select_npc(npc)
        self.navigate(target)

    def on_edit(self, target):
        if target[0] == "returning" and not self.npc.returning.enabled():
            self.npc.returning.line[self.project.base_language] = "…"
            self.model_changed(True)
        self.navigate(target)

    def _set_link(self, src, target, after=False):
        npc = self.npc
        if src[0] == "option":
            o = npc.node(src[1]).options[src[2]]
            if after:
                o.after_next = target
                o.next = -1
            else:
                o.next = target
        elif src[0] == "auto":
            n = npc.node(src[1])
            n.auto_next = target
            n.end = False
        elif src[0] == "start":
            npc.start_node = target
        elif src[0] == "returning":
            npc.returning.next = target
        self.model_changed(True)

    def on_connect(self, src, target, ask_after):
        after = False
        if ask_after:
            m = QMenu(self)
            a1 = m.addAction("Следующая реплика (сразу)")
            a2 = m.addAction("Реплика после сдачи квеста (after_next)")
            r = m.exec(self.cursor().pos())
            if r is None:
                return
            after = (r == a2)
        self._set_link(src, target, after)
        self.navigate(("option",) + tuple(src[1:]) if src[0] == "option" else ("node", target))

    def _new_node(self, pos, near=None):
        npc = self.npc
        nid = npc.new_node_id(near)
        n = Node(nid)
        n.pos = [pos.x(), pos.y()]
        n.end = True
        npc.nodes.append(n)
        return n

    def on_create(self, src, pos):
        if not self.npc:
            return
        if src is None:
            n = self._new_node(pos)
            self.model_changed(True)
            self.navigate(("node", n.id))
            return
        m = QMenu(self)
        a_new = m.addAction("Новая реплика NPC здесь")
        a_close = m.addAction("Закрыть диалог") if src[0] == "option" else None
        a_end = m.addAction("Конец диалога") if src[0] == "auto" else None
        a_un = m.addAction("Отвязать")
        r = m.exec(self.cursor().pos())
        if r is None:
            return
        npc = self.npc
        if r == a_new:
            near = src[1] if src[0] in ("option", "auto") else None
            n = self._new_node(QPointF(pos.x() + 10, pos.y() - 20), near)
            self._set_link(src, n.id)
            self.navigate(("node", n.id))
        elif r == a_close and src[0] == "option":
            o = npc.node(src[1]).options[src[2]]
            o.next = -1
            o.close = True
            self.model_changed(True)
        elif r == a_end:
            n = npc.node(src[1])
            n.auto_next = -1
            n.end = True
            self.model_changed(True)
        elif r == a_un:
            self.unlink(src)

    def unlink(self, ref):
        npc = self.npc
        if ref[0] == "option":
            npc.node(ref[1]).options[ref[2]].next = -1
        elif ref[0] == "after":
            npc.node(ref[1]).options[ref[2]].after_next = -1
        elif ref[0] == "auto":
            npc.node(ref[1]).auto_next = -1
        elif ref[0] == "returning":
            npc.returning.next = -1
        self.model_changed(True)

    def on_context(self, target, screen_pos, scene_pos):
        if not self.npc:
            return
        m = QMenu(self)
        npc = self.npc
        if target is None:
            m.addAction("Новая реплика NPC здесь", lambda: self.on_create(None, scene_pos))
            m.addAction("Вписать всё", self.view.fit_all)
            m.addAction("Авто-раскладка", self.auto_layout)
        elif target[0] == "node":
            nid = target[1]
            n = npc.node(nid)
            m.addAction("Добавить вариант ответа", lambda: self.add_option(npc, n))
            if nid != npc.start_node:
                m.addAction("Сделать стартовой", lambda: (setattr(npc, "start_node", nid), self.model_changed(True)))
            m.addAction("Дублировать", lambda: self.duplicate_nodes([nid]))
            m.addSeparator()
            m.addAction("Озвучка (%s)…" % LANGUAGE_NAMES.get(self.display_lang, self.display_lang),
                        lambda: self._attach_voice_dialog(("node", nid)))
            m.addSeparator()
            m.addAction("Удалить", lambda: self.delete_nodes(self.gscene.selected_node_ids() or [nid]))
        elif target[0] == "option":
            nid, oi = target[1], target[2]
            n = npc.node(nid)
            m.addAction("Открыть вариант", lambda: self.navigate(target))
            if oi > 0:
                m.addAction("Выше", lambda: self._move_option(n, oi, -1))
            if oi < len(n.options) - 1:
                m.addAction("Ниже", lambda: self._move_option(n, oi, 1))
            if n.options[oi].next != -1:
                m.addAction("Отвязать", lambda: self.unlink(("option", nid, oi)))
            m.addAction("Добавить вариант", lambda: self.add_option(npc, n))
            m.addSeparator()
            m.addAction("Удалить вариант", lambda: self._del_option(n, oi))
        elif target[0] == "edge":
            m.addAction("Удалить связь", lambda: self.unlink(target[1:]))
        elif target[0] == "returning":
            m.addAction("Редактировать", lambda: self.navigate(("returning",)))
            if npc.returning.enabled():
                m.addAction("Убрать повторный визит", self._clear_returning)
        else:
            m.addAction("Свойства NPC", lambda: self.inspector.show_target(("npc",)))
        m.exec(screen_pos)

    def _clear_returning(self):
        self.npc.returning.line = {}
        self.npc.returning.voice = {}
        self.model_changed(True)

    def _move_option(self, n, oi, d):
        j = oi + d
        n.options[oi], n.options[j] = n.options[j], n.options[oi]
        self.model_changed(True)
        self.navigate(("option", n.id, j))

    def _del_option(self, n, oi):
        del n.options[oi]
        self.model_changed(True)
        self.navigate(("node", n.id))

    def add_option(self, npc, n):
        n.options.append(Option())
        n.end = False
        n.auto_next = -1
        self.model_changed(True)
        self.navigate(("option", n.id, len(n.options) - 1))

    def delete_selected(self):
        ids = self.gscene.selected_node_ids()
        if ids:
            self.delete_nodes(ids)
            return
        sel = self.gscene.selectedItems()
        for s in sel:
            from .graph import EdgeItem
            if isinstance(s, EdgeItem):
                self.unlink(s.ref)
                return

    def delete_nodes(self, ids):
        npc = self.npc
        if not ids:
            return
        if len(ids) > 1 and QMessageBox.question(self, "Удалить", "Удалить реплик: %d?" % len(ids)) != QMessageBox.Yes:
            return
        if npc.start_node in ids and len(npc.nodes) > len(ids):
            rest = [n.id for n in npc.nodes if n.id not in ids]
            npc.start_node = rest[0]
        if len(npc.nodes) <= len(ids):
            QMessageBox.information(self, "Удалить", "У NPC должна остаться хотя бы одна реплика.")
            return
        for nid in ids:
            npc.remove_node(nid)
        self.model_changed(True)
        self.inspector.show_target(("npc",))

    def duplicate_selected(self):
        if not self._in_dialog():
            self.duplicate_world_selected()
            return
        self.duplicate_nodes(self.gscene.selected_node_ids())

    def duplicate_nodes(self, ids):
        npc = self.npc
        if not ids:
            return
        mapping = {}
        new = []
        for nid in ids:
            src = npc.node(nid)
            d = src.to_dict()
            nn = Node.from_dict(d)
            nn.id = npc.new_node_id(max(npc.node_ids()))
            nn.voice = {}
            for o in nn.options:
                o.after_voice = {}
            nn.pos = [src.pos[0] + 40, src.pos[1] + 40]
            mapping[nid] = nn.id
            npc.nodes.append(nn)
            new.append(nn)
        for nn in new:   # links between copied nodes point to the copies
            if nn.auto_next in mapping:
                nn.auto_next = mapping[nn.auto_next]
            for o in nn.options:
                if o.next in mapping:
                    o.next = mapping[o.next]
        self.model_changed(True)
        self.navigate(("node", new[0].id))

    def renumber_node(self, npc, old, new):
        if npc.renumber_node(old, new):
            for n in npc.nodes:
                pass
            self.logln("#%d → #%d: озвучка и сцена переедут при сохранении" % (old, new))
            self.model_changed(True)
            self.navigate(("node", new))

    def auto_layout(self):
        if not self.npc:
            return
        layout.auto_layout(self.npc)
        self.model_changed(True)
        self.rebuild_scene()
        self.view.fit_all()

    # ======================================================================== voice
    def files_dropped(self, files, target):
        if not self.npc or not files:
            return
        audio_files = [f for f in files if os.path.splitext(f)[1].lower() in audio.AUDIO_EXTS]
        if not audio_files:
            return
        if target is None and len(audio_files) > 1:
            self._voice_batch(audio_files, self.display_lang)
            return
        if target is None:
            return
        self._attach(target, audio_files[0], self.display_lang)

    def _voices_of(self, target):
        npc = self.npc
        if target[0] == "node":
            n = npc.node(target[1])
            return n.line, n.voice
        if target[0] == "returning":
            return npc.returning.line, npc.returning.voice
        return None, None

    def _attach(self, target, path, lang):
        from ..model import Voice
        texts, voices = self._voices_of(target)
        if voices is None:
            return
        v = Voice()
        v.wav = self.project.rel_to_game(path)
        info = audio.wav_info(path) if path.lower().endswith(".wav") else None
        v.duration = info.duration if info and info.ok else 0.0
        voices[lang] = v
        self.logln("Озвучка %s → %s (%s)" % (os.path.basename(path), target, lang))
        self.model_changed(True)
        self.navigate(target)

    def _attach_voice_dialog(self, target):
        from .widgets import AUDIO_FILTER
        path, _ = QFileDialog.getOpenFileName(self, "Озвучка", self.last_audio_dir or self.project.settings.game_dir, AUDIO_FILTER)
        if path:
            self.last_audio_dir = os.path.dirname(path)
            self._attach(target, path, self.display_lang)

    def voice_from_folder(self):
        if not self.npc:
            return
        d = QFileDialog.getExistingDirectory(self, "Папка с озвучкой (файлы <номер реплики>.wav, returning.wav)",
                                             self.last_audio_dir or self.project.settings.game_dir)
        if not d:
            return
        files = [os.path.join(d, f) for f in os.listdir(d) if os.path.splitext(f)[1].lower() in audio.AUDIO_EXTS]
        langs = [LANGUAGE_NAMES.get(l, l) for l in self.project.languages]
        lname, ok = QInputDialog.getItem(self, "Язык озвучки", "Язык файлов:", langs,
                                         self.project.languages.index(self.display_lang), False)
        if not ok:
            return
        lang = self.project.languages[langs.index(lname)]
        self.last_audio_dir = d
        self._voice_batch(files, lang)

    def _voice_batch(self, files, lang):
        from ..model import Voice
        npc = self.npc
        matched = []
        skipped = []
        for f in files:
            base = os.path.splitext(os.path.basename(f))[0].lower()
            target = None
            if base.startswith("returning"):
                target = ("returning",)
                texts, voices = npc.returning.line, npc.returning.voice
            elif re.fullmatch(r"\d+", base) and npc.node(int(base)):
                n = npc.node(int(base))
                target = ("node", n.id)
                texts, voices = n.line, n.voice
            else:
                m = re.fullmatch(r"(\d+)_(\d+)_after", base)
                if m and npc.node(int(m.group(1))) and int(m.group(2)) < len(npc.node(int(m.group(1))).options):
                    o = npc.node(int(m.group(1))).options[int(m.group(2))]
                    target = ("option", int(m.group(1)), int(m.group(2)))
                    texts, voices = o.after_line, o.after_voice
            if target is None:
                skipped.append(os.path.basename(f))
                continue
            v = Voice()
            v.wav = self.project.rel_to_game(f)
            info = audio.wav_info(f) if f.lower().endswith(".wav") else None
            v.duration = info.duration if info and info.ok else 0.0
            voices[lang] = v
            matched.append(os.path.basename(f))
        self.logln("Озвучка (%s): привязано %d: %s" % (lang, len(matched), ", ".join(matched)))
        if skipped:
            self.logln("  не распознаны (нужно <номер>.wav / returning.wav / <номер>_<вариант>_after.wav): " + ", ".join(skipped))
        self.model_changed(True)
        QMessageBox.information(self, "Озвучка", "Привязано файлов: %d\nНе распознано: %d%s" % (
            len(matched), len(skipped), ("\n" + ", ".join(skipped[:12])) if skipped else ""))

    def regenerate_all(self):
        if QMessageBox.question(self, "Липсинк", "Пересоздать липсинк всех озвученных реплик и сохранить?\n"
                                "Свои сцены (Faceposer) не трогаются.") == QMessageBox.Yes:
            self.save(regenerate_all=True)

    # ======================================================================== validation
    def run_validation(self, show=True):
        if not self.project:
            return
        probs = validate(self.project, self.mapindex)
        self.problems.clear()
        icons = {"error": ("✖", QColor("#e05a4a")), "warning": ("▲", QColor("#d6a23a")), "info": ("i", QColor("#6fa8dc"))}
        cnt = {"error": 0, "warning": 0, "info": 0}
        for pr in probs:
            cnt[pr.severity] += 1
            w = pr.where
            where = {"npc": "NPC", "returning": "повторный визит", "quest": "квест"}.get(w[0], "")
            if w[0] == "node":
                where = "реплика #%d" % w[1]
            elif w[0] == "option":
                where = "#%d вариант %d" % (w[1], w[2] + 1)
            elif w[0] == "quest":
                where = "квест %s" % w[1]
            elif w[0] == "package":
                where = "распорядок, пакет %d" % (w[1] + 1)
            elif w[0] == "ref":
                where = "карта %s, #%d" % (w[1], w[2])
            it = QTreeWidgetItem(["", pr.npc, where, pr.message])
            sym, col = icons[pr.severity]
            it.setText(0, sym)
            it.setForeground(0, col)
            it.setData(0, Qt.UserRole, (pr.npc, w))
            self.problems.addTopLevelItem(it)
        self.bottom_tabs.setTabText(0, "Проверка: %d ошибок, %d предупр." % (cnt["error"], cnt["warning"]))
        if show:
            self.bottom_tabs.setCurrentIndex(0)
            self.dock_bottom.show()
            self.statusBar().showMessage("Проверка: ошибок %d, предупреждений %d, заметок %d" % (cnt["error"], cnt["warning"], cnt["info"]), 6000)

    def _problem_clicked(self, it):
        npc_name, w = it.data(0, Qt.UserRole)
        if w[0] == "quest":
            self.left_tabs.setCurrentWidget(self.quest_tab)
            self.inspector.show_target(("quest", w[1]))
            return
        if w[0] == "ref":
            self.focus_ref(w[1], w[2])
            return
        if w[0] == "package":
            self.inspector.pkg_sel = w[1]
            self.open_npc(npc_name, tab=2)
            return
        npc = self.project.npc(npc_name)
        if npc and npc is not self.npc:
            self.select_npc(npc)
        if w[0] in ("node", "option", "returning"):
            self.navigate(w)
        else:
            self.inspector.show_target(("npc",))

    # ======================================================================== search
    def _search(self, text):
        text = (text or "").strip().lower()
        self.search_hits = []
        self.search_pos = 0
        if text and self.npc:
            for n in sorted(self.npc.nodes, key=lambda x: (x.pos[0], x.pos[1])):
                blob = " ".join(list(n.line.values()) + [t for o in n.options for t in o.text.values()] + [n.note, str(n.id)]).lower()
                if text in blob or text == "#%d" % n.id:
                    self.search_hits.append(n.id)
        self.gscene.highlight(set(self.search_hits))
        if text:
            self.statusBar().showMessage("Найдено реплик: %d" % len(self.search_hits), 3000)

    def _search_next(self):
        if not self.search_hits:
            return
        nid = self.search_hits[self.search_pos % len(self.search_hits)]
        self.search_pos += 1
        self.navigate(("node", nid))

    # ======================================================================== import
    def import_dialogs(self):
        if not self.project:
            QMessageBox.information(self, "Импорт", "Сначала создайте или откройте проект.")
            return
        start = os.path.join(self.project.settings.game_dir, "scripts", "dialogs")
        files, _ = QFileDialog.getOpenFileNames(self, "Файлы диалогов игры", start, "Диалоги (*.txt)")
        if not files:
            return
        log = []
        added = importer.import_files(self.project, files, log)
        self._finish_import(self.project, added, log)
        self.model_changed(True)
        self.refresh_npc_list()
        self.refresh_quest_list()
        if added:
            self.select_npc(added[0])

    def _finish_import(self, project, added, log):
        for npc in added:
            layout.auto_layout(npc)
            if not any(npc.display_name.values()):
                ents = self.mapindex.find(npc.targetname)
                if not ents and project.settings.maps_dirs and not self.mapindex.maps:
                    self.mapindex.scan(project.settings.maps_dirs)
                    ents = self.mapindex.find(npc.targetname)
                disp = next((e.display for e in ents if e.display), "")
                if disp:
                    npc.display_name[project.base_language] = disp
                    log.append("%s: имя «%s» взято из Hammer (loot_displayname)" % (npc.targetname, disp))
        self.logln("Импорт:\n  " + "\n  ".join(log))

    # ======================================================================== game
    def simulate(self):
        if not self.npc:
            return
        if self.sim is not None:
            self.sim.close()
        self.sim = SimulatorDialog(self, self.npc)
        self.sim.nodeShown.connect(lambda nid: (self.gscene.highlight({nid} if nid >= 0 else set(), "sim_hit"),
                                                nid >= 0 and nid in self.gscene.node_items and
                                                self.view.centerOn(self.gscene.node_items[nid])))
        self.sim.show()

    def _maps_for(self, npc):
        return sorted({e.map for e in self.mapindex.find(npc.targetname)})

    def run_game(self):
        if not self._in_dialog() and self.cur_map:
            self.run_game_here()
            return
        if not self.project or not self.npc:
            return
        s = self.project.settings
        if not os.path.isfile(s.hl2_exe):
            QMessageBox.warning(self, "Игра", "Не найден hl2.exe: %s (Проект → Настройки)" % s.hl2_exe)
            return
        if self.dirty and not self.save():
            return
        mp = s.test_map
        if not mp:
            maps = self._maps_for(self.npc)
            if len(maps) == 1:
                mp = maps[0]
            elif maps:
                mp, ok = QInputDialog.getItem(self, "Карта", "Карта с NPC %s:" % self.npc.targetname, maps, 0, False)
                if not ok:
                    return
        if not mp:
            mp, ok = QInputDialog.getText(self, "Карта", "Имя карты (без .bsp):")
            if not ok or not mp:
                return
        args = [s.hl2_exe, "-game", s.mod_dir, "-dev", "-console", "-novid", "+dialog_language", self.display_lang,
                "+map", mp]
        self.logln("Запуск: " + " ".join('"%s"' % a if " " in a else a for a in args))
        subprocess.Popen(args, cwd=os.path.dirname(s.hl2_exe))

    def reload_in_game(self):
        if not self.project:
            return
        if self.dirty and not self.save():
            return
        s = self.project.settings
        if not os.path.isfile(s.hl2_exe):
            return
        args = [s.hl2_exe, "-hijack", "-game", s.mod_dir, "+dialog_language", self.display_lang, "+dialog_reload",
                "+rpm_world_reload", "1"]
        subprocess.Popen(args, cwd=os.path.dirname(s.hl2_exe))
        self.logln("В игру отправлено: dialog_language %s; dialog_reload; rpm_world_reload 1 (объекты слоя карты созданы заново)" % self.display_lang)

    # ======================================================================== misc
    def logln(self, s):
        self.log.appendPlainText(time.strftime("%H:%M:%S  ") + s)

    def _open_dir(self, d):
        if d and os.path.isdir(d):
            os.startfile(d)

    def show_help(self):
        QMessageBox.information(self, "Управление", (
            "<b>Карта</b> (как в GECK)<br>"
            "Правая кнопка + WASD — полёт (Q/E — вниз/вверх, Shift — быстрее, колесо при зажатой правой — скорость) · "
            "колесо — вперёд/назад · средняя кнопка — сдвиг · F — к выделенному<br>"
            "Левая кнопка — выделить, тащить — двигать по поверхностям (Ctrl — по сетке 16), Alt+тащить — повернуть, "
            "[ ] — поворот на 15°, Delete — удалить, Ctrl+D — копия<br>"
            "Слева «Объекты» — двойной клик и клик на карте: NPC, маркеры (кровать, стул, стена…), предметы, контейнеры, пропы, маршруты; "
            "правая кнопка на карте — поставить сюда. «На карте» — всё, что стоит на открытой карте.<br>"
            "Распорядок NPC — вкладка «Распорядок» в свойствах NPC; ползунок «Время» над картой показывает, где кто в этот час.<br><br>"
            "<b>Холст</b><br>"
            "Колесо — масштаб к курсору · средняя/правая кнопка или Пробел+ЛКМ — двигать холст · F — вписать всё<br>"
            "Двойной клик по пустому месту — новая реплика · N — новая реплика в центре<br>"
            "Тяните от кружка справа у ответа к реплике — связать; в пустое место — создать новую реплику<br>"
            "Delete — удалить выделенное · Ctrl+D — дублировать · Ctrl+L — авто-раскладка<br>"
            "Перетащите WAV/MP3 на реплику — озвучка на языке, выбранном сверху; несколько файлов на холст — "
            "привязка по номерам (0.wav, 105.wav, returning.wav)<br><br>"
            "<b>Плашки языков</b> в шапке реплики: зелёная — озвучка + липсинк, жёлтая — липсинк создастся при сохранении, "
            "серая — нет озвучки, красная — файл не найден; красные буквы — нет перевода текста.<br><br>"
            "<b>Ctrl+S</b> — сохранить проект и разложить всё по папкам игры: scripts/dialogs, sound/dialogs, scenes/dialogs "
            "(WAV конвертируется, липсинк создаётся для изменённых реплик).<br>"
            "<b>F5</b> — пройти диалог в редакторе · <b>F6</b> — запустить карту в игре (с окна карты — с места камеры) · "
            "<b>F8</b> — обновить диалоги и мир в запущенной игре"))

    # ======================================================================== tabs
    def _in_dialog(self):
        return self.central_tabs.currentWidget() is self.view

    def _central_changed(self, i):
        if self.central_tabs.widget(i) is self.view:
            self.view.setFocus()
        else:
            self.worldview.setFocus()

    def _fit(self):
        if self._in_dialog():
            self.view.fit_all()
        else:
            self.worldview.focus_selected()

    # ======================================================================== screenshots (docs/reports)
    def grab_to(self, path):
        self.grab().save(path)
