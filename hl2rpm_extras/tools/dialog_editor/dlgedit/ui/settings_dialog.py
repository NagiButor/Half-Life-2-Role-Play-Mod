"""Project settings: where the game files go, languages, maps, tools."""
import os

from PySide6.QtCore import Qt
from PySide6.QtWidgets import (QComboBox, QDialog, QDialogButtonBox, QFileDialog, QFormLayout, QHBoxLayout, QLabel,
                               QLineEdit, QListWidget, QListWidgetItem, QPushButton, QVBoxLayout, QWidget, QGroupBox)

from ..model import KNOWN_LANGUAGES, LANGUAGE_NAMES
from .widgets import hint


def _path_row(parent, line, directory=True, filt=""):
    w = QWidget()
    l = QHBoxLayout(w)
    l.setContentsMargins(0, 0, 0, 0)
    l.addWidget(line, 1)
    b = QPushButton("…")
    b.setFixedWidth(30)

    def pick():
        if directory:
            d = QFileDialog.getExistingDirectory(parent, "Папка", line.text())
        else:
            d, _ = QFileDialog.getOpenFileName(parent, "Файл", line.text(), filt)
        if d:
            line.setText(os.path.normpath(d))
    b.clicked.connect(pick)
    l.addWidget(b)
    return w


class SettingsDialog(QDialog):
    def __init__(self, parent, project):
        super().__init__(parent)
        self.p = project
        s = project.settings
        self.setWindowTitle("Настройки проекта")
        self.resize(720, 620)
        lay = QVBoxLayout(self)

        g = QGroupBox("Куда сохранять")
        f = QFormLayout(g)
        self.game = QLineEdit(s.game_dir)
        f.addRow("Папка игры:", _path_row(self, self.game))
        quick = QHBoxLayout()
        for label, sub in (("мод (релиз)", ""), ("testmaps (тестовые)", "testmaps")):
            b = QPushButton(label)
            b.clicked.connect(lambda _=False, sub=sub: self.game.setText(os.path.normpath(os.path.join(self.mod.text(), sub)) if sub else self.mod.text()))
            quick.addWidget(b)
        quick.addStretch(1)
        f.addRow("", quick)
        f.addRow("", hint("Туда пишутся scripts/dialogs/<npc>.txt, sound/dialogs/<npc>/<язык>/, scenes/dialogs/<npc>/<язык>/."))
        self.mod = QLineEdit(s.mod_dir)
        f.addRow("Папка мода:", _path_row(self, self.mod))
        f.addRow("", hint("Для поиска уже существующих звуков/сцен, списка предметов и запуска игры."))
        lay.addWidget(g)

        g = QGroupBox("Языки")
        gl = QVBoxLayout(g)
        self.langs = QListWidget()
        self.langs.setMaximumHeight(170)
        for lid, name in KNOWN_LANGUAGES:
            it = QListWidgetItem("%s  (%s)" % (name, lid))
            it.setData(Qt.UserRole, lid)
            it.setFlags(it.flags() | Qt.ItemIsUserCheckable)
            it.setCheckState(Qt.Checked if lid in s.languages else Qt.Unchecked)
            self.langs.addItem(it)
        gl.addWidget(self.langs)
        bl = QHBoxLayout()
        bl.addWidget(QLabel("Основной язык:"))
        self.base = QComboBox()
        for lid, name in KNOWN_LANGUAGES:
            self.base.addItem(name, lid)
        self.base.setCurrentIndex(max(0, self.base.findData(s.base_language)))
        bl.addWidget(self.base)
        bl.addStretch(1)
        gl.addLayout(bl)
        gl.addWidget(hint("Основной язык пишется в обычные ключи файла и показывается, если перевода нет. "
                          "Игра выбирает язык сама (язык Steam / cl_language), для проверки — dialog_language russian."))
        ml = QHBoxLayout()
        ml.addWidget(QLabel("Нет озвучки на языке:"))
        self.missing = QComboBox()
        self.missing.addItem("только текст (без звука)", "silent")
        self.missing.addItem("играть озвучку основного языка", "base")
        self.missing.setCurrentIndex(max(0, self.missing.findData(s.missing_voice)))
        ml.addWidget(self.missing)
        ml.addStretch(1)
        gl.addLayout(ml)
        lay.addWidget(g)

        g = QGroupBox("Карты (.vmf) — для поиска NPC и целей квестов")
        gl = QVBoxLayout(g)
        self.maps = QListWidget()
        self.maps.setMaximumHeight(80)
        for d in s.maps_dirs:
            self.maps.addItem(d)
        gl.addWidget(self.maps)
        ml = QHBoxLayout()
        a = QPushButton("+ папка")
        a.clicked.connect(self._add_map)
        r = QPushButton("− убрать")
        r.clicked.connect(lambda: self.maps.takeItem(self.maps.currentRow()))
        ml.addWidget(a)
        ml.addWidget(r)
        ml.addStretch(1)
        gl.addLayout(ml)
        lay.addWidget(g)

        g = QGroupBox("Инструменты")
        f = QFormLayout(g)
        self.hl2 = QLineEdit(s.hl2_exe)
        f.addRow("hl2.exe:", _path_row(self, self.hl2, False, "hl2.exe (hl2.exe)"))
        self.ff = QLineEdit(s.ffmpeg)
        self.ff.setPlaceholderText("пусто — найти автоматически (для MP3/OGG/FLAC)")
        f.addRow("ffmpeg.exe:", _path_row(self, self.ff, False, "ffmpeg.exe (ffmpeg.exe)"))
        self.map = QLineEdit(s.test_map)
        self.map.setPlaceholderText("карта для «Запустить в игре» (пусто — где стоит NPC)")
        f.addRow("Тестовая карта:", self.map)
        lay.addWidget(g)

        bb = QDialogButtonBox(QDialogButtonBox.Ok | QDialogButtonBox.Cancel)
        bb.accepted.connect(self.accept)
        bb.rejected.connect(self.reject)
        lay.addWidget(bb)

    def _add_map(self):
        d = QFileDialog.getExistingDirectory(self, "Папка с .vmf")
        if d:
            self.maps.addItem(os.path.normpath(d))

    def apply(self):
        s = self.p.settings
        s.game_dir = self.game.text().strip()
        s.mod_dir = self.mod.text().strip()
        langs = [self.langs.item(i).data(Qt.UserRole) for i in range(self.langs.count())
                 if self.langs.item(i).checkState() == Qt.Checked]
        base = self.base.currentData()
        if base not in langs:
            langs.insert(0, base)
        # keep base first
        langs = [base] + [l for l in langs if l != base]
        s.languages = langs
        s.base_language = base
        s.missing_voice = self.missing.currentData()
        s.maps_dirs = [self.maps.item(i).text() for i in range(self.maps.count())]
        s.hl2_exe = self.hl2.text().strip()
        s.ffmpeg = self.ff.text().strip()
        s.test_map = self.map.text().strip()
