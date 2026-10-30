"""«Проверить диалог»: play the dialog in the editor with voice, quests and reputation."""
from PySide6.QtCore import Qt, Signal, QTimer
from PySide6.QtGui import QFont
from PySide6.QtWidgets import (QCheckBox, QComboBox, QDialog, QFrame, QHBoxLayout, QHeaderView, QLabel, QPlainTextEdit,
                               QPushButton, QSpinBox, QSplitter, QTableWidget, QVBoxLayout, QWidget)

from .. import audio
from ..model import LANGUAGE_NAMES, QUEST_STATES, QUEST_STATE_NAMES, text_for
from ..simulate import Simulator


class SimulatorDialog(QDialog):
    nodeShown = Signal(int)

    def __init__(self, ctx, npc):
        super().__init__(ctx)
        self.ctx = ctx
        self.npc = npc
        self.setWindowTitle("Проверка диалога — %s" % npc.targetname)
        self.resize(1080, 640)
        self.sim = Simulator(ctx.project, npc, ctx.display_lang)
        lay = QHBoxLayout(self)
        split = QSplitter()
        lay.addWidget(split)

        # state panel
        left = QWidget()
        ll = QVBoxLayout(left)
        ll.addWidget(QLabel("<b>Состояние игры</b>"))
        lr = QHBoxLayout()
        lr.addWidget(QLabel("Язык:"))
        self.lang = QComboBox()
        for lg in ctx.project.languages:
            self.lang.addItem(LANGUAGE_NAMES.get(lg, lg), lg)
        self.lang.setCurrentIndex(max(0, self.lang.findData(ctx.display_lang)))
        self.lang.currentIndexChanged.connect(self._lang)
        lr.addWidget(self.lang)
        ll.addLayout(lr)
        self.spoken = QCheckBox("Уже говорили (повторный визит)")
        self.spoken.toggled.connect(lambda v: setattr(self.sim, "spoken", v))
        ll.addWidget(self.spoken)
        self.spoiled = QCheckBox("NPC больше не разговаривает (spoiled)")
        self.spoiled.toggled.connect(lambda v: setattr(self.sim, "spoiled", v))
        ll.addWidget(self.spoiled)
        rr = QHBoxLayout()
        rr.addWidget(QLabel("Репутация:"))
        self.rep = QSpinBox()
        self.rep.setRange(-1000, 1000)
        self.rep.valueChanged.connect(lambda v: setattr(self.sim, "rep", v))
        rr.addWidget(self.rep)
        rr.addStretch(1)
        ll.addLayout(rr)
        self.sound = QCheckBox("Звук")
        self.sound.setChecked(True)
        ll.addWidget(self.sound)
        ll.addWidget(QLabel("Квесты:"))
        self.qt = QTableWidget(0, 3)
        self.qt.setHorizontalHeaderLabels(["Квест", "Состояние", "Этап"])
        self.qt.horizontalHeader().setSectionResizeMode(0, QHeaderView.Stretch)
        self.qt.verticalHeader().setVisible(False)
        ll.addWidget(self.qt, 1)
        self.log = QPlainTextEdit()
        self.log.setReadOnly(True)
        self.log.setMaximumHeight(150)
        ll.addWidget(QLabel("События:"))
        ll.addWidget(self.log)
        bl = QHBoxLayout()
        b = QPushButton("⟲ Начать разговор")
        b.clicked.connect(self.start)
        bl.addWidget(b)
        ll.addLayout(bl)
        split.addWidget(left)

        # game-like panel
        right = QFrame()
        right.setStyleSheet("QFrame#panel { background:#14171b; border:1px solid #3a414b; border-radius:8px; }")
        right.setObjectName("panel")
        rl = QVBoxLayout(right)
        rl.setContentsMargins(24, 20, 24, 20)
        self.name = QLabel()
        self.name.setStyleSheet("color:#ffb84d; font-size:16px; font-weight:600;")
        rl.addWidget(self.name)
        self.where = QLabel()
        self.where.setObjectName("hint")
        rl.addWidget(self.where)
        self.line = QLabel()
        self.line.setWordWrap(True)
        self.line.setStyleSheet("font-size:15px; color:#e6e9ee; padding:8px 0;")
        self.line.setTextInteractionFlags(Qt.TextSelectableByMouse)
        rl.addWidget(self.line)
        self.opts_box = QVBoxLayout()
        rl.addLayout(self.opts_box)
        rl.addStretch(1)
        split.addWidget(right)
        split.setSizes([380, 700])
        self.refresh_quests()
        self.start()

    def _lang(self):
        self.sim.lang = self.lang.currentData()

    def refresh_quests(self):
        p = self.ctx.project
        self.qt.setRowCount(len(p.quests))
        for r, q in enumerate(p.quests):
            st = self.sim.quest(q.id)
            from PySide6.QtWidgets import QTableWidgetItem
            it = QTableWidgetItem(q.id)
            it.setFlags(it.flags() & ~Qt.ItemIsEditable)
            self.qt.setItem(r, 0, it)
            cb = QComboBox()
            for s in QUEST_STATES:
                cb.addItem(QUEST_STATE_NAMES[s], s)
            cb.setCurrentIndex(cb.findData(st.state))
            cb.currentIndexChanged.connect(lambda _=0, cb=cb, qid=q.id: setattr(self.sim.quest(qid), "state", cb.currentData()))
            self.qt.setCellWidget(r, 1, cb)
            sp = QSpinBox()
            sp.setRange(0, 50)
            sp.setValue(st.stage)
            sp.valueChanged.connect(lambda v, qid=q.id: setattr(self.sim.quest(qid), "stage", v))
            self.qt.setCellWidget(r, 2, sp)

    def start(self):
        self.log.appendPlainText("— разговор%s —" % (" (повторный визит)" if self.sim.spoken else ""))
        self.show_screen(self.sim.start())

    def show_screen(self, sc):
        while self.opts_box.count():
            it = self.opts_box.takeAt(0)
            if it.widget():
                it.widget().deleteLater()
        p = self.ctx.project
        if sc is not None:
            for ev in sc.events:
                self.log.appendPlainText("• " + ev)
        self.refresh_quests()
        if sc is None:
            audio.stop()
            self.name.setText("")
            self.where.setText("")
            if self.sim.spoiled:
                self.line.setText("<i>NPC больше не разговаривает.</i>")
            else:
                self.line.setText("<i>Диалог закрыт.</i>")
            self.log.appendPlainText("— диалог закрыт%s —" % (" (уже говорили)" if self.sim.spoken else ""))
            b = QPushButton("Поговорить снова")
            b.clicked.connect(self.start)
            self.opts_box.addWidget(b)
            self.nodeShown.emit(-1)
            return
        nm, _ = text_for(self.npc.display_name, self.sim.lang, p.base_language)
        self.name.setText(nm or self.npc.targetname)
        kind = {"returning": "повторный визит → варианты #%d" % sc.node_id, "after": "после сдачи квеста"}.get(sc.kind, "реплика #%d" % sc.node_id)
        vinfo = ("озвучка: %s" % sc.voice.wav) if sc.voice else "без озвучки"
        self.where.setText("%s · %s" % (kind, vinfo))
        self.line.setText(sc.line if not sc.line_fallback else "<span style='color:#e07b6a'>[нет перевода]</span> " + sc.line)
        if self.sound.isChecked() and sc.voice and sc.voice.wav:
            audio.play(p.game_path(sc.voice.wav))
        else:
            audio.stop()
        self.nodeShown.emit(sc.node_id)
        if sc.options:
            for idx, text, fb in sc.options:
                b = QPushButton("%d. %s" % (idx + 1, text))
                b.setStyleSheet("text-align:left; padding:8px; font-size:13px;" + ("color:#e07b6a;" if fb else ""))
                b.clicked.connect(lambda _=False, i=idx: self.choose(i))
                self.opts_box.addWidget(b)
        else:
            if sc.pending_spoil:
                label = "▶ (реплика закончилась — NPC больше не заговорит)"
            elif sc.auto_next != -1:
                label = "▶ (реплика закончилась — дальше #%d)" % sc.auto_next
            elif sc.auto_close:
                label = "▶ (реплика закончилась — диалог закроется)"
            else:
                label = "⚠ Нет вариантов и продолжения: в игре окно останется открытым"
            b = QPushButton(label)
            b.setStyleSheet("text-align:left; padding:8px;")
            b.clicked.connect(lambda: self.show_screen(self.sim.cont()))
            self.opts_box.addWidget(b)

    def choose(self, idx):
        sc = self.sim.current
        n = self.npc.node(sc.node_id) if sc else None
        if n and idx < len(n.options):
            t, _ = text_for(n.options[idx].text, self.sim.lang, self.ctx.project.base_language)
            self.log.appendPlainText("Игрок: " + t)
        self.show_screen(self.sim.choose(idx))

    def closeEvent(self, e):
        audio.stop()
        self.nodeShown.emit(-1)
        super().closeEvent(e)
