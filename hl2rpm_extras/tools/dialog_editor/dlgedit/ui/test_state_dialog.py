"""Игра → Состояние для запуска: the state the game takes when it is started from the editor
(F6, «Игра отсюда»): quests at a stage, flags, variables, skills, items, level, time, weather.
The game file is scripts/rpg/test_state.txt, applied by the convar rpm_test_state
(game/server/hl2rpm_logic.cpp)."""
import os

from PySide6.QtCore import Qt
from PySide6.QtWidgets import (QCheckBox, QComboBox, QDialog, QDialogButtonBox, QDoubleSpinBox, QFormLayout, QHBoxLayout,
                               QHeaderView, QLineEdit, QSpinBox, QTableWidget, QTableWidgetItem, QVBoxLayout)

from .. import kv, logic
from .widgets import hint

STATES = [("", "— не трогать —"), ("active", "активно"), ("completed", "выполнено"), ("failed", "провалено"), ("inactive", "не взято")]
REL_PATH = "scripts/rpg/test_state.txt"


def _pairs(text):
    out = {}
    for part in (text or "").replace(";", ",").split(","):
        if "=" in part:
            k, v = part.split("=", 1)
            try:
                out[k.strip()] = int(float(v))
            except ValueError:
                pass
    return out


def _fmt(d):
    return ", ".join("%s=%d" % kv_ for kv_ in sorted(d.items()))


def state_kv(st):
    root = kv.KV("TestState")
    qb = root.add("quests")
    for qid, (state, stage) in sorted(st.get("quests", {}).items()):
        b = qb.add(qid)
        b.add("state", state)
        b.add("stage", str(int(stage)))
        b.inline = True
    for key in ("flags",):
        b = root.add(key)
        for f in st.get(key, []):
            b.add(f, "1")
    for key in ("vars", "skills", "stats", "items"):
        b = root.add(key)
        for k, v in sorted(st.get(key, {}).items()):
            b.add(k, str(int(v)))
    if st.get("level"):
        root.add("level", str(int(st["level"])))
    if st.get("rep") is not None and st.get("rep") != 0:
        root.add("rep", str(int(st["rep"])))
    if st.get("hour") is not None:
        root.add("hour", "%.2f" % float(st["hour"]))
    if st.get("weather"):
        root.add("weather", st["weather"])
    return root


def launch_args(project):
    """["+rpm_test_state", path] if the state is on (the file is written into the game folder)."""
    st = project.test_state or {}
    if not st.get("enabled"):
        return []
    path = os.path.join(project.settings.game_dir, *REL_PATH.split("/"))
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write(kv.dumps(state_kv(st), header="HL2RPM Creation Kit: the state of «Play from here» (Игра → Состояние для запуска)"))
    return ["+rpm_test_state", REL_PATH]


class TestStateDialog(QDialog):
    def __init__(self, ctx):
        super().__init__(ctx)
        self.ctx = ctx
        p = ctx.project
        st = p.test_state = dict(p.test_state or {})
        self.setWindowTitle("Состояние для запуска")
        self.setMinimumSize(620, 620)
        lay = QVBoxLayout(self)
        self.on = QCheckBox("Применять при запуске из редактора (F6, «Игра отсюда»)")
        self.on.setChecked(bool(st.get("enabled")))
        lay.addWidget(self.on)
        lay.addWidget(hint("Игра стартует так, будто игрок уже прошёл часть сюжета: задания на нужном этапе, флаги, предметы, "
                           "навыки. Действия этапов при этом не выполняются — ставится только состояние."))
        self.qt = QTableWidget(len(p.quests), 3)
        self.qt.setHorizontalHeaderLabels(["Задание", "Состояние", "Этап"])
        self.qt.horizontalHeader().setSectionResizeMode(0, QHeaderView.Stretch)
        self.qt.verticalHeader().setVisible(False)
        quests = st.get("quests", {})
        lang = ctx.display_lang
        for r, q in enumerate(p.quests):
            it = QTableWidgetItem("%s — %s" % (q.id, q.title.get(lang) or q.title.get(p.base_language) or ""))
            it.setFlags(it.flags() & ~Qt.ItemIsEditable)
            it.setData(Qt.UserRole, q.id)
            self.qt.setItem(r, 0, it)
            cb = QComboBox()
            for v, label in STATES:
                cb.addItem(label, v)
            cur = quests.get(q.id, ["", 0])
            cb.setCurrentIndex(max(0, cb.findData(cur[0])))
            self.qt.setCellWidget(r, 1, cb)
            sp = QSpinBox()
            sp.setRange(0, max(0, len(q.stages) - 1))
            sp.setValue(int(cur[1]))
            self.qt.setCellWidget(r, 2, sp)
        lay.addWidget(self.qt, 1)
        f = QFormLayout()
        self.flags = QLineEdit(", ".join(st.get("flags", [])))
        self.flags.setPlaceholderText("через запятую: door_open, met_boss")
        flags, vars_, _e = logic.known_names(p)
        self.flags.setToolTip("Известные флаги: " + (", ".join(flags) or "нет"))
        f.addRow("Флаги:", self.flags)
        self.vars = QLineEdit(_fmt(st.get("vars", {})))
        self.vars.setPlaceholderText("rats_killed=3")
        self.vars.setToolTip("Известные переменные: " + (", ".join(vars_) or "нет"))
        f.addRow("Переменные:", self.vars)
        sk = dict(st.get("skills", {}))
        sk.update(st.get("stats", {}))
        self.skills = QLineEdit(_fmt(sk))
        self.skills.setPlaceholderText("speech=60, lockpick=40, cha=7")
        self.skills.setToolTip("Навыки: " + ", ".join("%s (%s)" % (n, i) for i, n in logic.SKILLS) +
                               "\nХарактеристики: " + ", ".join("%s (%s)" % (n, i) for i, n in logic.STATS))
        f.addRow("Навыки:", self.skills)
        self.items = QLineEdit(_fmt(st.get("items", {})))
        self.items.setPlaceholderText("rpm_credits=200, item_healthkit=2")
        f.addRow("Выдать предметы:", self.items)
        row = QHBoxLayout()
        self.level = QSpinBox()
        self.level.setRange(1, 50)
        self.level.setValue(int(st.get("level", 1) or 1))
        self.rep = QSpinBox()
        self.rep.setRange(-1000, 1000)
        self.rep.setValue(int(st.get("rep", 0) or 0))
        row.addWidget(self.level)
        row.addWidget(hint("  репутация:"))
        row.addWidget(self.rep)
        row.addStretch(1)
        f.addRow("Уровень:", row)
        trow = QHBoxLayout()
        self.hour_on = QCheckBox()
        self.hour = QDoubleSpinBox()
        self.hour.setRange(0, 23.99)
        self.hour.setDecimals(2)
        self.hour_on.setChecked(st.get("hour") is not None)
        self.hour.setValue(float(st.get("hour") or 12))
        self.weather = QComboBox()
        self.weather.addItem("— как на карте —", "")
        for w in logic.WEATHERS:
            self.weather.addItem(w, w)
        self.weather.setCurrentIndex(max(0, self.weather.findData(st.get("weather", ""))))
        trow.addWidget(self.hour_on)
        trow.addWidget(self.hour)
        trow.addWidget(hint("  погода:"))
        trow.addWidget(self.weather, 1)
        f.addRow("Время:", trow)
        lay.addLayout(f)
        bb = QDialogButtonBox(QDialogButtonBox.Ok | QDialogButtonBox.Cancel)
        bb.accepted.connect(self._ok)
        bb.rejected.connect(self.reject)
        lay.addWidget(bb)

    def _ok(self):
        p = self.ctx.project
        st = {"enabled": self.on.isChecked()}
        quests = {}
        for r in range(self.qt.rowCount()):
            qid = self.qt.item(r, 0).data(Qt.UserRole)
            state = self.qt.cellWidget(r, 1).currentData()
            if state:
                quests[qid] = [state, self.qt.cellWidget(r, 2).value()]
        st["quests"] = quests
        st["flags"] = [x.strip() for x in self.flags.text().replace(";", ",").split(",") if x.strip()]
        st["vars"] = _pairs(self.vars.text())
        sk = _pairs(self.skills.text())
        stat_ids = {s for s, _ in logic.STATS}
        st["skills"] = {k: v for k, v in sk.items() if k not in stat_ids}
        st["stats"] = {k: v for k, v in sk.items() if k in stat_ids}
        st["items"] = _pairs(self.items.text())
        st["level"] = self.level.value()
        st["rep"] = self.rep.value()
        st["hour"] = round(self.hour.value(), 2) if self.hour_on.isChecked() else None
        st["weather"] = self.weather.currentData() or ""
        p.test_state = st
        self.ctx.model_changed(False)
        self.accept()
