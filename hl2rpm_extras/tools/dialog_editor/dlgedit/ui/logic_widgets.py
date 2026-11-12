"""Editors of conditions and actions (logic.py): one list widget for every place that has
them - dialog options and lines, quest stages and objectives, events, presence of map
objects, AI packages. A list shows readable lines; a double click opens the editor."""
import copy

from PySide6.QtCore import Qt, Signal
from PySide6.QtWidgets import (QCheckBox, QComboBox, QDialog, QDialogButtonBox, QDoubleSpinBox, QFormLayout, QHBoxLayout,
                               QLabel, QLineEdit, QListWidget, QListWidgetItem, QSizePolicy, QSpinBox, QToolButton,
                               QVBoxLayout, QWidget)

from .. import logic
from .widgets import LangTexts, hint


# --- names the project knows ---------------------------------------------------------------------
def _layer_names(project, types=None):
    out = []
    for _m, layer in sorted(project.maps.items()):
        for r in layer.refs:
            if r.name and (types is None or r.type in types):
                out.append(r.name)
    return out


def choices(ctx, kind, quest=""):
    """[(value, label)] for a parameter kind."""
    p = ctx.project
    lang = getattr(ctx, "display_lang", p.base_language)
    if kind == "quest":
        return [(q.id, "%s — %s" % (q.id, q.title.get(lang) or q.title.get(p.base_language) or "")) for q in p.quests]
    if kind == "obj":
        q = p.quest(quest) if quest else None
        out = []
        for si, s in enumerate(q.stages if q else []):
            for o in s.objectives:
                out.append((o.id, "%s — этап %d: %s" % (o.id, si, o.text.get(lang) or o.text.get(p.base_language) or "")))
        return out
    if kind == "qstate":
        return list(logic.QSTATES)
    if kind == "op":
        return [(o, logic.OP_NAMES[o]) for o in logic.OPS]
    if kind == "varop":
        return list(logic.VAR_OPS)
    if kind == "skill":
        return list(logic.SKILLS)
    if kind == "stat":
        return list(logic.STATS)
    if kind in ("flag", "var", "event"):
        flags, vars_, events = logic.known_names(p)
        names = {"flag": flags, "var": vars_, "event": events}[kind]
        notes = {g.name: g.note for g in p.globals}
        return [(n, "%s — %s" % (n, notes[n]) if notes.get(n) else n) for n in names]
    if kind == "item":
        seen = []
        for it in p.items:
            seen.append((it.id, "%s — %s" % (it.id, it.label(lang, p.base_language))))
        ids = {x[0] for x in seen}
        for n in ctx.item_names() if hasattr(ctx, "item_names") else []:
            if n not in ids:
                seen.append((n, n))
        return seen
    if kind == "npc":
        return [(n.targetname, "%s — %s" % (n.targetname, n.display_name.get(lang) or n.display_name.get(p.base_language) or ""))
                for n in p.npcs]
    if kind == "target":
        out = [(n.targetname, "NPC %s" % n.targetname) for n in p.npcs]
        out += [(z, "зона %s" % z) for z in _layer_names(p, ("zone",))]
        out += [(n, "объект %s" % n) for n in _layer_names(p) if n not in {x[0] for x in out}]
        return out
    if kind == "zone":
        return [(z, z) for z in _layer_names(p, ("zone",))]
    if kind == "weather":
        return [(w, w) for w in logic.WEATHERS]
    if kind == "map":
        files = ctx.map_files() if hasattr(ctx, "map_files") else []
        names = sorted(set(list(p.maps.keys()) + list(files.keys() if isinstance(files, dict) else files)))
        return [(m, m) for m in names]
    if kind == "faction":
        return [(f.id, f.id) for f in p.factions]
    if kind == "music":
        return [(t.id, "%s — %s" % (t.id, t.file.rsplit("/", 1)[-1])) for t in p.music if t.kind != "stinger"]
    if kind == "stinger":
        return [(t.id, "%s — %s" % (t.id, t.file.rsplit("/", 1)[-1])) for t in p.music if t.kind == "stinger"]
    if kind == "perk":
        return [(x.id, "%s — %s" % (x.id, x.label(lang, p.base_language))) for x in p.perks]
    if kind == "doorop":
        return list(logic.DOOR_OPS)
    if kind == "fieldop":
        return list(logic.FIELD_OPS)
    if kind == "turretop":
        return list(logic.TURRET_OPS)
    if kind == "barkevent":
        from ..sounddata import BARK_EVENTS
        return [(e[0], e[1]) for e in BARK_EVENTS]
    return []


class _Combo(QComboBox):
    """value <-> combo of choices; editable kinds accept any text."""

    def __init__(self, items, value, editable=True, empty_label=None):
        super().__init__()
        self.setEditable(editable)
        self.setSizeAdjustPolicy(QComboBox.AdjustToMinimumContentsLengthWithIcon)
        self.setMinimumContentsLength(12)
        self.setSizePolicy(QSizePolicy.Expanding, QSizePolicy.Fixed)
        self.view().setMinimumWidth(320)
        if empty_label is not None:
            self.addItem(empty_label, "")
        for v, label in items:
            self.addItem(label, v)
        self.set_value(value)

    def set_value(self, value):
        value = "" if value is None else str(value)
        i = self.findData(value)
        if i >= 0:
            self.setCurrentIndex(i)
        elif self.isEditable():
            self.setEditText(value)

    def value(self):
        if self.isEditable():
            t = self.currentText().strip()
            i = self.currentIndex()
            if i >= 0 and t == self.itemText(i):
                return str(self.itemData(i) or "")
            # typed text: "id — label" -> id
            return t.split(" — ")[0].strip()
        return str(self.currentData() or "")


EDITABLE = {"quest", "flag", "var", "event", "item", "npc", "target", "zone", "weather", "map", "faction", "obj", "music"}


class LogicItemDialog(QDialog):
    """Edit one condition / action."""

    def __init__(self, ctx, item, actions=False, parent=None, self_label="этот NPC"):
        super().__init__(parent)
        self.ctx = ctx
        self.actions = actions
        self.item = copy.deepcopy(item)
        self.setWindowTitle("Действие" if actions else "Условие")
        self.setMinimumWidth(520)
        lay = QVBoxLayout(self)
        self.type_combo = QComboBox()
        for tid, label, _ps in (logic.ACTION_TYPES if actions else logic.COND_TYPES):
            self.type_combo.addItem(label, tid)
        self.type_combo.setCurrentIndex(max(0, self.type_combo.findData(self.item.get("type"))))
        self.type_combo.setMaxVisibleItems(30)
        tf = QFormLayout()
        tf.addRow("Что:", self.type_combo)
        lay.addLayout(tf)
        self.form_host = QWidget()
        self.form = QFormLayout(self.form_host)
        self.form.setContentsMargins(0, 0, 0, 0)
        lay.addWidget(self.form_host)
        extra = QHBoxLayout()
        if actions:
            self.delay = QDoubleSpinBox()
            self.delay.setRange(0, 3600)
            self.delay.setDecimals(1)
            self.delay.setSuffix(" с")
            self.delay.setValue(float(self.item.get("delay", 0) or 0))
            extra.addWidget(QLabel("Задержка:"))
            extra.addWidget(self.delay)
        else:
            self.not_box = QCheckBox("НЕ (наоборот)")
            self.not_box.setChecked(bool(self.item.get("not")))
            self.or_box = QCheckBox("ИЛИ со следующим")
            self.or_box.setToolTip("Условие и следующее за ним: хватит любого из двух")
            self.or_box.setChecked(bool(self.item.get("or")))
            extra.addWidget(self.not_box)
            extra.addWidget(self.or_box)
        extra.addStretch(1)
        lay.addLayout(extra)
        self.summary = QLabel()
        self.summary.setObjectName("hint")
        self.summary.setWordWrap(True)
        lay.addWidget(self.summary)
        self.note = hint("")
        lay.addWidget(self.note)
        bb = QDialogButtonBox(QDialogButtonBox.Ok | QDialogButtonBox.Cancel)
        bb.accepted.connect(self.accept)
        bb.rejected.connect(self.reject)
        lay.addWidget(bb)
        self.self_label = self_label
        self.editors = {}
        self.type_combo.currentIndexChanged.connect(self._type_changed)
        self._build()

    def _type_changed(self, _):
        t = self.type_combo.currentData()
        if t == self.item.get("type"):
            return
        fresh = logic.new_action(t) if self.actions else logic.new_cond(t)
        # keep values of parameters with the same key and kind
        for key, kind, _l, _d in logic.params(fresh, self.actions):
            for k2, kind2, _l2, _d2 in logic.params(self.item, self.actions):
                if k2 == key and kind2 == kind and self.item.get(key) not in (None, ""):
                    fresh[key] = self.item[key]
        self.item = fresh
        self._build()

    def _build(self):
        while self.form.rowCount():
            self.form.removeRow(0)
        self.editors = {}
        quest_combo = None
        for key, kind, label, default in logic.params(self.item, self.actions):
            v = self.item.get(key, default)
            if kind == "int":
                w = QSpinBox()
                w.setRange(-1000000, 1000000)
                w.setValue(int(float(v or 0)))
                w.valueChanged.connect(self._refresh)
            elif kind in ("hour", "float"):
                w = QDoubleSpinBox()
                w.setRange(0, 24 if kind == "hour" else 3600)
                w.setDecimals(2 if kind == "hour" else 1)
                w.setSingleStep(0.5)
                w.setValue(float(v or 0))
                w.valueChanged.connect(self._refresh)
            elif kind == "bool":
                w = QCheckBox()
                w.setChecked(bool(int(v or 0)))
                w.toggled.connect(self._refresh)
            elif kind == "text":
                if not isinstance(v, dict):
                    v = {self.ctx.project.base_language: v or ""}
                    self.item[key] = v
                w = LangTexts(self.ctx, v, None, multiline=False, placeholder="текст")
                w.changed.connect(self._refresh)
            elif kind in ("str", "sound"):
                w = QLineEdit(str(v or ""))
                w.textChanged.connect(self._refresh)
            else:
                items = choices(self.ctx, kind, self.item.get("quest", ""))
                empty = None
                if kind == "faction" and key == "faction" and self.item.get("type") == "rep":
                    empty = "— общая репутация —"
                elif kind in ("npc", "faction") and self.actions or kind == "npc" and key == "target" and self.item.get("type") in ("alive", "hostile"):
                    empty = "— %s —" % self.self_label
                elif kind == "music":
                    empty = "— обычная музыка (снять смену) —"
                w = _Combo(items, v, editable=kind in EDITABLE, empty_label=empty)
                w.currentIndexChanged.connect(self._refresh)
                w.editTextChanged.connect(self._refresh)
                if kind == "quest":
                    quest_combo = w
            self.editors[key] = (kind, w)
            self.form.addRow((label + ":") if label else "", w)
        if quest_combo is not None and "obj" in self.editors:
            def request_objs(*_):
                kind, ow = self.editors["obj"]
                cur = ow.value()
                ow.blockSignals(True)
                ow.clear()
                for v, label in choices(self.ctx, "obj", quest_combo.value()):
                    ow.addItem(label, v)
                ow.set_value(cur)
                ow.blockSignals(False)
            quest_combo.currentIndexChanged.connect(request_objs)
        notes = {
            "hostile": "В игре сработает, когда окно диалога закроется.",
            "faction_hostile": "Все NPC этой фракции на карте нападают, когда окно диалога закроется.",
            "teleport": "Игрок окажется у NPC / объекта (лицом по его повороту) или в середине зоны.",
            "fire": "Как выход Hammer: ввод сущности карты по targetname (!self, !player тоже).",
            "door": "prop_door_rotating / func_door по имени (Hammer или объект слоя). «Открыть» сначала отпирает.",
            "field": "func_brush / trigger — Disable / Enable; проп (излучатели, модель поля) — скрыть и убрать столкновения.",
            "turret": "npc_turret_floor / ceiling / ground, npc_combine_camera. «Перепрограммировать» — турели любят игрока "
                      "и стреляют по солдатам, ГО, мэнхакам; солдаты отвечают тем же.",
            "near": "Цель — NPC, объект карты или зона.",
            "chance": "Каждая проверка бросает кубик заново.",
            "event": "Событие вкладки «Логика» хоть раз срабатывало.",
            "objective": "Цель задания (вкладка задания → этапы).",
            "perk": "Перки — вкладка «РПГ».",
            "char_create": "Окно, где игрок выбирает имя, характеристики, отмеченные навыки и черты (как у доктора в New Vegas).",
            "rep": "С фракцией: −50 и ниже — вся фракция враждебна. Без фракции — общая репутация заданий.",
            "music": "Играют выбранные треки (несколько — через ;) вместо обычной музыки, пока другое действие не вернёт её "
                     "(пустой выбор). Треки — вкладка «Звук».",
            "stinger": "Короткий трек поверх музыки (музыка на это время притихает).",
            "bark": "NPC говорит фразу этого события из своих фраз или наборов (вкладка NPC «Фразы»); "
                    "«По действию» — фразы, которые звучат только так.",
        }
        self.note.setText(notes.get(self.item.get("type"), ""))
        self._refresh()

    def _collect(self):
        d = {"type": self.type_combo.currentData()}
        for key, (kind, w) in self.editors.items():
            if kind == "int":
                d[key] = w.value()
            elif kind in ("hour", "float"):
                d[key] = round(w.value(), 2)
            elif kind == "bool":
                d[key] = 1 if w.isChecked() else 0
            elif kind == "text":
                d[key] = self.item.get(key, {})
            elif kind in ("str", "sound"):
                d[key] = w.text().strip()
            else:
                d[key] = w.value()
        if self.actions:
            if self.delay.value() > 0:
                d["delay"] = round(self.delay.value(), 1)
        else:
            if self.not_box.isChecked():
                d["not"] = True
            if self.or_box.isChecked():
                d["or"] = True
        return d

    def _refresh(self, *_):
        d = self._collect()
        lang = getattr(self.ctx, "display_lang", "russian")
        s = logic.describe_action(d, self.ctx.project, lang) if self.actions else logic.describe_cond(d, self.ctx.project, lang)
        self.summary.setText("Будет: " + s)

    def result_item(self):
        return self._collect()


class LogicList(QWidget):
    """A list of conditions or actions with add / edit / delete / move."""
    changed = Signal()

    def __init__(self, ctx, items, actions=False, empty_text=None, self_label="этот NPC", max_height=150):
        super().__init__()
        self.ctx = ctx
        self.items = items              # the project's list, edited in place
        self.actions = actions
        self.self_label = self_label
        lay = QVBoxLayout(self)
        lay.setContentsMargins(0, 0, 0, 0)
        lay.setSpacing(4)
        self.list = QListWidget()
        self.list.setWordWrap(True)
        self.list.setMaximumHeight(max_height)
        self.list.itemDoubleClicked.connect(lambda _: self.edit())
        lay.addWidget(self.list)
        self.empty = hint(empty_text or ("Нет действий." if actions else "Нет условий — всегда."))
        lay.addWidget(self.empty)
        bl = QHBoxLayout()
        bl.setSpacing(4)
        add = QToolButton(text="+ действие" if actions else "+ условие")
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
        lang = getattr(self.ctx, "display_lang", "russian")
        p = self.ctx.project
        for i, x in enumerate(self.items):
            if self.actions:
                s = logic.describe_action(x, p, lang)
                if x.get("type") in logic.AFTER_DIALOG:
                    s += "  ⏏"
            else:
                s = logic.describe_cond(x, p, lang)
                if i < len(self.items) - 1:
                    s += "   ИЛИ" if x.get("or") else "   И"
            it = QListWidgetItem("%d. %s" % (i + 1, s))
            if self.actions and x.get("type") in logic.AFTER_DIALOG:
                it.setToolTip("В диалоге срабатывает после закрытия окна")
            self.list.addItem(it)
        self.list.setVisible(bool(self.items))
        self.empty.setVisible(not self.items)

    def add(self):
        d = LogicItemDialog(self.ctx, logic.new_action("give") if self.actions else logic.new_cond("quest_state"),
                            self.actions, self, self.self_label)
        if d.exec():
            self.items.append(d.result_item())
            self.refresh()
            self.list.setCurrentRow(len(self.items) - 1)
            self.changed.emit()

    def edit(self):
        i = self.list.currentRow()
        if not 0 <= i < len(self.items):
            return
        d = LogicItemDialog(self.ctx, self.items[i], self.actions, self, self.self_label)
        if d.exec():
            self.items[i] = d.result_item()
            self.refresh()
            self.list.setCurrentRow(i)
            self.changed.emit()

    def remove(self):
        i = self.list.currentRow()
        if 0 <= i < len(self.items):
            del self.items[i]
            self.refresh()
            self.changed.emit()

    def move(self, d):
        i = self.list.currentRow()
        j = i + d
        if 0 <= i < len(self.items) and 0 <= j < len(self.items):
            self.items[i], self.items[j] = self.items[j], self.items[i]
            self.refresh()
            self.list.setCurrentRow(j)
            self.changed.emit()
