"""Objectives of quest stages: the list on the quest page and the editor of one objective."""
import copy

from PySide6.QtCore import Signal
from PySide6.QtWidgets import (QCheckBox, QComboBox, QDialog, QDialogButtonBox, QFormLayout, QHBoxLayout, QListWidget,
                               QSpinBox, QToolButton, QVBoxLayout, QWidget)

from .. import logic
from ..model import Objective
from .logic_widgets import LogicList, _Combo, choices
from .widgets import LangTexts, hint

OBJ_HELP = {
    "kill": "Засчитывается каждое убийство подходящего NPC игроком. Имя — targetname NPC (у NPC Creation Kit — его имя в проекте), "
            "* — любые символы, несколько имён через ;.",
    "collect": "Выполнена, пока у игрока есть нужное количество (проверяется постоянно).",
    "talk": "Выполнена, когда игрок заговорит с NPC.",
    "go": "Зона — игрок внутри неё; NPC или объект — ближе заданного расстояния.",
    "use": "Игрок нажал E на объекте с этим targetname.",
    "cond": "Выполнена, когда все условия станут верны.",
    "custom": "Выполняется действием «Задание: отметить цель» — в ответе диалога, событии, другом этапе.",
}


class ObjectiveDialog(QDialog):
    """Edit one objective of a quest stage."""

    def __init__(self, ctx, obj, parent=None):
        super().__init__(parent)
        self.ctx = ctx
        self.obj = obj                   # updated on OK; a copy is edited until then
        self.o = copy.deepcopy(obj)
        self.setWindowTitle("Цель %s" % obj.id)
        self.setMinimumWidth(540)
        lay = QVBoxLayout(self)
        f = QFormLayout()
        self.tc = QComboBox()
        for tid, label, _k in logic.OBJ_TYPES:
            self.tc.addItem(label, tid)
        self.tc.setCurrentIndex(max(0, self.tc.findData(self.o.type)))
        f.addRow("Что сделать:", self.tc)
        self.text = LangTexts(ctx, self.o.text, None, multiline=False, placeholder="строка в журнале, например: Найти 3 аптечки")
        f.addRow("Текст:", self.text)
        lay.addLayout(f)
        self.host = QWidget()
        self.f2 = QFormLayout(self.host)
        self.f2.setContentsMargins(0, 0, 0, 0)
        lay.addWidget(self.host)
        row = QHBoxLayout()
        self.optional = QCheckBox("Необязательная")
        self.optional.setToolTip("Этап переходит дальше и без неё")
        self.optional.setChecked(self.o.optional)
        self.marker = QCheckBox("Маркер на экране")
        self.marker.setToolTip("Ромб с расстоянием над целью (NPC, объект, зона)")
        self.marker.setChecked(self.o.marker)
        row.addWidget(self.optional)
        row.addWidget(self.marker)
        row.addStretch(1)
        lay.addLayout(row)
        self.help = hint("")
        lay.addWidget(self.help)
        bb = QDialogButtonBox(QDialogButtonBox.Ok | QDialogButtonBox.Cancel)
        bb.accepted.connect(self._ok)
        bb.rejected.connect(self.reject)
        lay.addWidget(bb)
        self.tc.currentIndexChanged.connect(self._build)
        self._build()

    def _build(self, *_):
        while self.f2.rowCount():
            self.f2.removeRow(0)
        t = self.tc.currentData()
        self.o.type = t
        self.target = self.item = self.count = self.radius = self.conds = None
        if t in ("kill", "talk", "go", "use"):
            kind = {"kill": "npc", "talk": "npc", "go": "target", "use": "target"}[t]
            self.target = _Combo(choices(self.ctx, kind), self.o.target, editable=True)
            self.f2.addRow({"kill": "Кого:", "talk": "С кем:", "go": "Куда:", "use": "Что (targetname):"}[t], self.target)
        if t == "collect":
            self.item = _Combo(choices(self.ctx, "item"), self.o.item, editable=True)
            self.f2.addRow("Предмет:", self.item)
            self.target = _Combo(choices(self.ctx, "target"), self.o.target, editable=True)
            self.target.setToolTip("Где лежит — для маркера (необязательно)")
            self.f2.addRow("Маркер у:", self.target)
        if t in ("kill", "collect"):
            self.count = QSpinBox()
            self.count.setRange(1, 999)
            self.count.setValue(int(self.o.count or 1))
            self.f2.addRow("Сколько:", self.count)
        if t == "go":
            self.radius = QSpinBox()
            self.radius.setRange(16, 4096)
            self.radius.setSingleStep(16)
            self.radius.setSuffix(" ед.")
            self.radius.setValue(int(self.o.radius or 128))
            self.f2.addRow("Ближе чем:", self.radius)
        if t == "cond":
            self.conds = LogicList(self.ctx, self.o.conditions, max_height=110)
            self.f2.addRow("Условия:", self.conds)
        self.help.setText(OBJ_HELP.get(t, ""))

    def _ok(self):
        o = self.o
        o.optional = self.optional.isChecked()
        o.marker = self.marker.isChecked()
        o.target = self.target.value() if self.target is not None else ""
        o.item = self.item.value() if self.item is not None else ""
        if self.count is not None:
            o.count = self.count.value()
        if self.radius is not None:
            o.radius = self.radius.value()
        if o.type != "cond":
            o.conditions = []
        self.obj.__dict__.update(copy.deepcopy(o.__dict__))
        self.accept()


def describe_objective(ctx, o):
    p = ctx.project
    lang = getattr(ctx, "display_lang", p.base_language)
    text = o.text.get(lang) or o.text.get(p.base_language) or ""
    kind = logic.OBJ_BY_ID.get(o.type, (o.type, o.type, ""))[1]
    what = ""
    if o.type in ("kill", "talk", "go", "use") and o.target:
        what = " " + o.target
    elif o.type == "collect":
        what = " %s" % (o.item or "?")
    if o.type in ("kill", "collect") and o.count > 1:
        what += " × %d" % o.count
    if o.type == "cond":
        what = ": " + logic.describe_conds(o.conditions, p, lang)
    s = "%s  [%s%s]" % (text or "(без текста)", kind.lower(), what)
    if o.optional:
        s += "  необяз."
    return s


class ObjectiveList(QWidget):
    changed = Signal()

    def __init__(self, ctx, quest, stage):
        super().__init__()
        self.ctx = ctx
        self.quest = quest
        self.stage = stage
        lay = QVBoxLayout(self)
        lay.setContentsMargins(0, 0, 0, 0)
        lay.setSpacing(4)
        self.list = QListWidget()
        self.list.setWordWrap(True)
        self.list.setMaximumHeight(120)
        self.list.itemDoubleClicked.connect(lambda _: self.edit())
        lay.addWidget(self.list)
        self.empty = hint("Нет целей — этап ждёт действий (ответ в диалоге, событие).")
        lay.addWidget(self.empty)
        bl = QHBoxLayout()
        bl.setSpacing(4)
        add = QToolButton(text="+ цель")
        add.clicked.connect(self.add)
        bl.addWidget(add)
        for text, tip, fn in (("✎", "Изменить", self.edit), ("✕", "Удалить", self.remove),
                              ("↑", "Выше", lambda: self.move(-1)), ("↓", "Ниже", lambda: self.move(1))):
            b = QToolButton(text=text)
            b.setToolTip(tip)
            b.clicked.connect(fn)
            bl.addWidget(b)
        bl.addStretch(1)
        lay.addLayout(bl)
        self.refresh()

    def refresh(self):
        self.list.clear()
        for o in self.stage.objectives:
            self.list.addItem("%s: %s" % (o.id, describe_objective(self.ctx, o)))
        self.list.setVisible(bool(self.stage.objectives))
        self.empty.setVisible(not self.stage.objectives)

    def add(self):
        o = Objective(self.quest.new_objective_id())
        o.type = "talk"
        if ObjectiveDialog(self.ctx, o, self).exec():
            self.stage.objectives.append(o)
            self.refresh()
            self.changed.emit()

    def edit(self):
        i = self.list.currentRow()
        if 0 <= i < len(self.stage.objectives):
            if ObjectiveDialog(self.ctx, self.stage.objectives[i], self).exec():
                self.refresh()
                self.list.setCurrentRow(i)
                self.changed.emit()

    def remove(self):
        i = self.list.currentRow()
        if 0 <= i < len(self.stage.objectives):
            del self.stage.objectives[i]
            self.refresh()
            self.changed.emit()

    def move(self, d):
        i = self.list.currentRow()
        j = i + d
        objs = self.stage.objectives
        if 0 <= i < len(objs) and 0 <= j < len(objs):
            objs[i], objs[j] = objs[j], objs[i]
            self.refresh()
            self.list.setCurrentRow(j)
            self.changed.emit()
