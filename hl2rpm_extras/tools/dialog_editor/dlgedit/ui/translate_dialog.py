"""Проект → Автоперевод: fill the missing translations of the project with Google Translate."""
from PySide6.QtCore import QObject, QThread, Signal
from PySide6.QtWidgets import (QButtonGroup, QCheckBox, QComboBox, QDialog, QFormLayout, QHBoxLayout, QLabel, QPlainTextEdit,
                               QProgressBar, QPushButton, QRadioButton, QVBoxLayout)

from .. import translate
from ..model import LANGUAGE_NAMES
from .widgets import hint


class _Worker(QObject):
    progress = Signal(int, int)
    finished = Signal(object, object)

    def __init__(self, jobs, src):
        super().__init__()
        self.jobs = jobs
        self.src = src
        self.stop = False

    def run(self):
        res, errs = translate.run(self.jobs, self.src, lambda d, t: self.progress.emit(d, t), lambda: self.stop)
        self.finished.emit(res, errs)


class TranslateDialog(QDialog):
    def __init__(self, ctx):
        super().__init__(ctx)
        self.ctx = ctx
        p = ctx.project
        self.setWindowTitle("Автоперевод (Google)")
        self.setMinimumWidth(560)
        lay = QVBoxLayout(self)
        lay.addWidget(hint("Заполняет переводы машинным переводом Google (без ключа, нужен интернет). Такие тексты помечены "
                           "«МП», пока их не поправят вручную; ручные переводы автоперевод не трогает. Имена NPC из списков "
                           "шаблонов не переводятся."))
        f = QFormLayout()
        self.src = QComboBox()
        for lg in p.languages:
            self.src.addItem(LANGUAGE_NAMES.get(lg, lg), lg)
        self.src.setCurrentIndex(max(0, self.src.findData(p.base_language)))
        self.src.currentIndexChanged.connect(self._recount)
        f.addRow("С языка:", self.src)
        trow = QHBoxLayout()
        self.targets = {}
        for lg in p.languages:
            cb = QCheckBox(LANGUAGE_NAMES.get(lg, lg))
            cb.setChecked(lg != p.base_language)
            cb.toggled.connect(self._recount)
            trow.addWidget(cb)
            self.targets[lg] = cb
        trow.addStretch(1)
        f.addRow("На языки:", trow)
        self.mode = QButtonGroup(self)
        m1 = QRadioButton("Только пустые")
        m2 = QRadioButton("Пустые и прежний машинный перевод")
        m1.setChecked(True)
        self.mode.addButton(m1, 0)
        self.mode.addButton(m2, 1)
        self.mode.idClicked.connect(self._recount)
        mrow = QVBoxLayout()
        mrow.addWidget(m1)
        mrow.addWidget(m2)
        f.addRow("Что:", mrow)
        self.scope = QButtonGroup(self)
        s1 = QRadioButton("Весь проект (диалоги, задания, предметы, фракции, события)")
        s2 = QRadioButton("Только диалог текущего NPC")
        s1.setChecked(True)
        s2.setEnabled(ctx.npc is not None)
        self.scope.addButton(s1, 0)
        self.scope.addButton(s2, 1)
        self.scope.idClicked.connect(self._recount)
        srow = QVBoxLayout()
        srow.addWidget(s1)
        srow.addWidget(s2)
        f.addRow("Где:", srow)
        lay.addLayout(f)
        self.count = QLabel()
        lay.addWidget(self.count)
        self.bar = QProgressBar()
        self.bar.setVisible(False)
        lay.addWidget(self.bar)
        self.log = QPlainTextEdit()
        self.log.setReadOnly(True)
        self.log.setMaximumHeight(140)
        self.log.setVisible(False)
        lay.addWidget(self.log)
        bl = QHBoxLayout()
        bl.addStretch(1)
        self.go = QPushButton("Перевести")
        self.go.clicked.connect(self.start)
        self.cancel = QPushButton("Закрыть")
        self.cancel.clicked.connect(self._close)
        bl.addWidget(self.go)
        bl.addWidget(self.cancel)
        lay.addLayout(bl)
        self.thread = None
        self.worker = None
        self._recount()

    def _plan(self):
        p = self.ctx.project
        src = self.src.currentData()
        targets = [lg for lg, cb in self.targets.items() if cb.isChecked() and lg != src]
        npcs = {self.ctx.npc.targetname} if self.scope.checkedId() == 1 and self.ctx.npc else None
        return translate.plan(p, src, targets, "machine" if self.mode.checkedId() == 1 else "empty", npcs), src

    def _recount(self, *_):
        jobs, _src = self._plan()
        self.count.setText("Текстов к переводу: %d" % len(jobs))
        self.go.setEnabled(bool(jobs) and self.thread is None)

    def start(self):
        jobs, src = self._plan()
        if not jobs:
            return
        self.go.setEnabled(False)
        self.bar.setVisible(True)
        self.bar.setRange(0, len(jobs))
        self.bar.setValue(0)
        self.log.setVisible(True)
        self.log.appendPlainText("Перевод %d текстов…" % len(jobs))
        self.thread = QThread(self)
        self.worker = _Worker(jobs, src)
        self.worker.moveToThread(self.thread)
        self.thread.started.connect(self.worker.run)
        self.worker.progress.connect(lambda d, t: self.bar.setValue(d))
        self.worker.finished.connect(self._done)
        self.thread.start()

    def _done(self, results, errors):
        self.thread.quit()
        self.thread.wait()
        self.thread = None
        n = translate.apply(self.ctx.project, results)
        for e in errors[:20]:
            self.log.appendPlainText("⚠ " + e)
        if len(errors) > 20:
            self.log.appendPlainText("… ещё ошибок: %d" % (len(errors) - 20))
        self.log.appendPlainText("Готово: переведено %d. Сохраните проект (Ctrl+S), чтобы записать в игру." % n)
        if n:
            self.ctx.model_changed(True)
            self.ctx.inspector.show_target(self.ctx.inspector.target)
        self._recount()

    def _close(self):
        if self.worker is not None and self.thread is not None:
            self.worker.stop = True
            self.thread.quit()
            self.thread.wait(5000)
        self.accept()
