"""The "Sound" tab: music tracks, soundscapes of locations, shared NPC bark sets; the "Barks" tab
of an NPC; the location part of a zone and a sound placed on a map. Game files:
scripts/rpg/music.txt, scripts/rpg/barks.txt, scripts/soundscapes_hl2rpm.txt, the barks of
scripts/npcs/<npc>.txt (game/server/hl2rpm_audio.cpp, client music game/client/hl2rpm_music.cpp)."""
import os
import re
import shutil
import subprocess
import sys
import tempfile

from PySide6.QtCore import Qt, QTimer, Signal
from PySide6.QtWidgets import (QAbstractItemView, QCheckBox, QComboBox, QDialog, QDialogButtonBox, QDoubleSpinBox,
                               QFileDialog, QFormLayout, QGroupBox, QHBoxLayout, QHeaderView, QInputDialog, QLabel,
                               QLineEdit, QListWidget, QListWidgetItem, QMessageBox, QPushButton, QSpinBox, QTableWidget,
                               QTableWidgetItem, QToolButton, QTreeWidget, QTreeWidgetItem, QVBoxLayout, QWidget)

from .. import audio
from .. import logic
from .. import sounddata as sd
from .. import worlddata as wd
from .logic_widgets import LogicList
from .widgets import AUDIO_FILTER, AutoText, LangTexts, completer_line, hint

ID_RE = re.compile(r"^[A-Za-z0-9_\-]+$")
SCAPE_RE = re.compile(r"^[A-Za-z0-9_\-\.]+$")
MUSIC_DIR = "music/hl2rpm"       # imported music goes to sound/<this>/


def _group(title):
    g = QGroupBox(title)
    lay = QVBoxLayout(g)
    lay.setSpacing(6)
    return g, lay


def _form():
    f = QFormLayout()
    f.setLabelAlignment(Qt.AlignRight)
    f.setFieldGrowthPolicy(QFormLayout.AllNonFixedFieldsGrow)
    return f


def _combo(items, current):
    c = QComboBox()
    for data, label in items:
        c.addItem(label, data)
    i = c.findData(current)
    c.setCurrentIndex(i if i >= 0 else 0)
    return c


# --- game sounds -------------------------------------------------------------------------------------------
def sound_names(ctx):
    """Every sound of the game (paths under sound/, lower case), cached on the main window."""
    names = getattr(ctx, "_sound_names", None)
    if names is None:
        names = []
        fs = getattr(ctx, "fs", None)
        if fs is not None:
            try:
                names = [n[len("sound/"):] for n in fs.listdir("sound") if n.endswith((".wav", ".mp3"))]
            except Exception:
                names = []
        ctx._sound_names = names
    return names


def sound_abs(ctx, rel):
    """A file on disk for a sound path under sound/ (extracted from a VPK into a temporary folder if needed)."""
    if not rel:
        return ""
    rel = rel.replace("\\", "/").lstrip("/")
    if rel.startswith("sound/"):
        rel = rel[len("sound/"):]
    p = ctx.project.find_content("sound/" + rel)
    if p:
        return p
    fs = getattr(ctx, "fs", None)
    if fs is None:
        return ""
    kind, src = fs.find("sound/" + rel)
    if kind == "dir":
        return src
    if kind == "vpk":
        out = os.path.join(tempfile.gettempdir(), "hl2rpm_ck_sounds", rel.replace("/", os.sep))
        if not os.path.exists(out):
            data = fs.read("sound/" + rel)
            if data is None:
                return ""
            os.makedirs(os.path.dirname(out), exist_ok=True)
            with open(out, "wb") as f:
                f.write(data)
        return out
    return ""


def sound_exists(ctx, rel):
    if not rel:
        return False
    rel = rel.replace("\\", "/").lstrip("/")
    if ctx.project.find_content("sound/" + rel):
        return True
    fs = getattr(ctx, "fs", None)
    return bool(fs is not None and fs.exists("sound/" + rel))


def play_sound(ctx, rel):
    path = sound_abs(ctx, rel)
    if not path:
        QMessageBox.information(ctx, "Звук", "Файл не найден: sound/%s" % rel)
        return
    if not audio.play(path):
        QMessageBox.information(ctx, "Звук", "Не удалось проиграть %s (для MP3 нужен ffmpeg — Проект → Настройки)" % rel)


def _safe_name(name):
    base, ext = os.path.splitext(os.path.basename(name))
    base = re.sub(r"[^A-Za-z0-9_\-]+", "_", base).strip("_").lower() or "track"
    return base, ext.lower()


def import_music_file(ctx, src):
    """Copy (convert if needed) a music file into <game>/sound/music/hl2rpm/. Returns (rel under sound/, message)."""
    p = ctx.project
    gd = p.settings.game_dir
    base, ext = _safe_name(src)
    dst_dir = os.path.join(gd, "sound", *MUSIC_DIR.split("/"))
    os.makedirs(dst_dir, exist_ok=True)
    if ext == ".mp3":
        out_ext = ".mp3"
    elif ext == ".wav":
        info = audio.wav_info(src)
        out_ext = ".wav" if (info.ok and info.fmt == 1 and info.bits == 16 and info.rate in audio.SOURCE_RATES) else ".mp3"
    else:
        out_ext = ".mp3"
    dst = os.path.join(dst_dir, base + out_ext)
    if os.path.normcase(os.path.abspath(src)) != os.path.normcase(os.path.abspath(dst)):
        if out_ext == ext:
            shutil.copyfile(src, dst)
            msg = "скопирован"
        else:
            ff = audio.find_ffmpeg(p.settings.ffmpeg)
            if not ff:
                return "", "для %s нужен ffmpeg (Проект → Настройки) — или выберите MP3/WAV" % ext
            flags = 0x08000000 if sys.platform == "win32" else 0
            r = subprocess.run([ff, "-y", "-loglevel", "error", "-i", src, "-vn", "-ar", "44100", "-c:a", "libmp3lame", "-q:a", "2", dst],
                               capture_output=True, creationflags=flags)
            if r.returncode != 0:
                return "", "ffmpeg: " + r.stderr.decode("utf-8", "replace")[-300:]
            msg = "сконвертирован в MP3"
    else:
        msg = "уже на месте"
    ctx._sound_names = None
    return "%s/%s%s" % (MUSIC_DIR, base, out_ext), msg


class SoundPicker(QDialog):
    """Choose a sound of the game (also inside VPKs) with a filter and playback."""

    def __init__(self, ctx, current="", prefix="", title="Звук игры", parent=None):
        super().__init__(parent)
        self.ctx = ctx
        self.setWindowTitle(title)
        self.resize(640, 560)
        lay = QVBoxLayout(self)
        self.filter = QLineEdit(prefix)
        self.filter.setPlaceholderText("фильтр: часть пути (ambient/, music/, wind…), несколько слов через пробел")
        lay.addWidget(self.filter)
        self.list = QListWidget()
        self.list.itemDoubleClicked.connect(lambda _: self.accept())
        lay.addWidget(self.list, 1)
        self.count = hint("")
        lay.addWidget(self.count)
        row = QHBoxLayout()
        b = QPushButton("▶ Прослушать")
        b.clicked.connect(self.play)
        row.addWidget(b)
        b = QPushButton("■")
        b.clicked.connect(audio.stop)
        row.addWidget(b)
        row.addStretch(1)
        bb = QDialogButtonBox(QDialogButtonBox.Ok | QDialogButtonBox.Cancel)
        bb.accepted.connect(self.accept)
        bb.rejected.connect(self.reject)
        row.addWidget(bb)
        lay.addLayout(row)
        self.names = sound_names(ctx)
        self._timer = QTimer(self)
        self._timer.setSingleShot(True)
        self._timer.setInterval(150)
        self._timer.timeout.connect(self.fill)
        self.filter.textChanged.connect(lambda _: self._timer.start())
        self.current = (current or "").lower()
        self.fill()

    def fill(self):
        words = [w for w in self.filter.text().lower().split() if w]
        self.list.clear()
        shown = 0
        total = 0
        for n in self.names:
            if all(w in n for w in words):
                total += 1
                if shown < 3000:
                    it = QListWidgetItem(n)
                    self.list.addItem(it)
                    if n == self.current:
                        self.list.setCurrentItem(it)
                    shown += 1
        self.count.setText("найдено %d%s" % (total, " (показаны первые 3000 — уточните фильтр)" if total > shown else ""))
        if not self.names:
            self.count.setText("Звуки игры не прочитаны (нет папки игры или VPK)")

    def value(self):
        it = self.list.currentItem()
        return it.text() if it else ""

    def play(self):
        if self.value():
            play_sound(self.ctx, self.value())

    def done(self, r):
        audio.stop()
        super().done(r)


class SoundField(QWidget):
    """A sound path under sound/: text, pick from the game, play, stop."""
    changed = Signal(str)

    def __init__(self, ctx, value="", prefix="", placeholder="путь в sound/, например ambient/wind1.wav"):
        super().__init__()
        self.ctx = ctx
        self.prefix = prefix
        lay = QHBoxLayout(self)
        lay.setContentsMargins(0, 0, 0, 0)
        lay.setSpacing(3)
        self.edit = QLineEdit(value)
        self.edit.setPlaceholderText(placeholder)
        self.edit.editingFinished.connect(self._edited)
        lay.addWidget(self.edit, 1)
        b = QToolButton(text="…")
        b.setToolTip("Выбрать звук игры")
        b.clicked.connect(self.pick)
        lay.addWidget(b)
        b = QToolButton(text="▶")
        b.setToolTip("Прослушать")
        b.clicked.connect(lambda: play_sound(self.ctx, self.value()) if self.value() else None)
        lay.addWidget(b)
        b = QToolButton(text="■")
        b.clicked.connect(audio.stop)
        lay.addWidget(b)
        self._last = value
        self._mark()

    def value(self):
        return self.edit.text().strip().replace("\\", "/")

    def set_value(self, v):
        self.edit.setText(v)
        self._edited()

    def _mark(self):
        ok = not self.value() or sound_exists(self.ctx, self.value())
        self.edit.setStyleSheet("" if ok else "color:#e07b6a;")
        self.edit.setToolTip("" if ok else "Такого файла нет в игре")

    def _edited(self):
        v = self.value()
        self._mark()
        if v != self._last:
            self._last = v
            self.changed.emit(v)

    def pick(self):
        d = SoundPicker(self.ctx, self.value(), self.prefix, parent=self)
        if d.exec() and d.value():
            self.set_value(d.value())


# --- barks ------------------------------------------------------------------------------------------------
def describe_bark(ctx, b):
    lang = getattr(ctx, "display_lang", ctx.project.base_language)
    t = b.text.get(lang) or b.text.get(ctx.project.base_language) or next((x for x in b.text.values() if x), "")
    ev = sd.BARK_EVENT_NAMES.get(b.event, b.event)
    extra = []
    if b.value:
        extra.append(b.value)
    if b.conditions:
        extra.append("условия: %d" % len(b.conditions))
    if b.once:
        extra.append("1 раз")
    if any(v and not v.is_empty() for v in b.voices.values()) or b.sound:
        extra.append("🔊")
    return "[%s] %s%s" % (ev, t or "(без текста)", ("   · " + ", ".join(extra)) if extra else "")


class BarkDialog(QDialog):
    def __init__(self, ctx, bark, others, parent=None):
        super().__init__(parent)
        self.ctx = ctx
        self.b = bark.clone()
        self.others = others
        self.setWindowTitle("Фраза NPC")
        self.setMinimumWidth(620)
        lay = QVBoxLayout(self)
        f = _form()
        self.ev = _combo([(e[0], e[1]) for e in sd.BARK_EVENTS], self.b.event)
        self.ev.currentIndexChanged.connect(self._ev_changed)
        f.addRow("Когда:", self.ev)
        self.val = QLineEdit(self.b.value)
        self.val.textEdited.connect(lambda t: setattr(self.b, "value", t.strip()))
        self.val_label = QLabel()
        f.addRow(self.val_label, self.val)
        self.idl = QLineEdit(self.b.id)
        self.idl.setToolTip("Латиница, цифры, _ (озвучка ляжет в файл bark_<id>.wav)")
        f.addRow("id:", self.idl)
        lay.addLayout(f)
        g, gl = _group("Текст (субтитр) и озвучка")
        gl.addWidget(hint("Озвучку можно не давать — тогда только субтитр. Своя озвучка получает липсинк, как реплика диалога."))
        lt = LangTexts(ctx, self.b.text, self.b.voices, multiline=False, placeholder="что говорит NPC")
        gl.addWidget(lt)
        sf = _form()
        self.snd = SoundField(ctx, self.b.sound, "vo/", "или звук игры: vo/npc/male01/hi01.wav")
        self.snd.changed.connect(lambda v: setattr(self.b, "sound", v))
        sf.addRow("Звук игры:", self.snd)
        gl.addLayout(sf)
        lay.addWidget(g)
        row = QHBoxLayout()
        row.addWidget(QLabel("Вес:"))
        self.weight = QDoubleSpinBox()
        self.weight.setRange(0.05, 100)
        self.weight.setSingleStep(0.5)
        self.weight.setValue(self.b.weight)
        self.weight.setToolTip("Из подходящих фраз выбирается случайная — с весом 2 вдвое чаще, чем с весом 1")
        row.addWidget(self.weight)
        row.addWidget(QLabel("  Пауза:"))
        self.cool = QSpinBox()
        self.cool.setRange(0, 86400)
        self.cool.setSuffix(" с")
        self.cool.setValue(int(self.b.cooldown))
        self.cool.setToolTip("Не повторять эту фразу раньше, чем через столько секунд")
        row.addWidget(self.cool)
        row.addWidget(QLabel("  Шанс:"))
        self.chance = QSpinBox()
        self.chance.setRange(1, 100)
        self.chance.setSuffix(" %")
        self.chance.setValue(self.b.chance)
        row.addWidget(self.chance)
        self.once = QCheckBox("Один раз за игру")
        self.once.setChecked(self.b.once)
        row.addWidget(self.once)
        row.addStretch(1)
        lay.addLayout(row)
        g, gl = _group("Условия (фраза звучит, только когда они верны)")
        cl = LogicList(ctx, self.b.conditions, self_label="этот NPC", max_height=110,
                       empty_text="Нет условий — всегда. Например: этап задания, время суток, погода, костюм игрока.")
        gl.addWidget(cl)
        lay.addWidget(g)
        lay.addStretch(1)
        bb = QDialogButtonBox(QDialogButtonBox.Ok | QDialogButtonBox.Cancel)
        bb.accepted.connect(self._ok)
        bb.rejected.connect(self.reject)
        lay.addWidget(bb)
        self._ev_changed()

    def _ev_changed(self, *_):
        self.b.event = self.ev.currentData()
        lbl = sd.BARK_EVENT_VALUE.get(self.b.event, "")
        self.val_label.setText((lbl + ":") if lbl else "")
        self.val.setVisible(bool(lbl))
        self.val_label.setVisible(bool(lbl))
        if not lbl:
            self.b.value = ""
            self.val.setText("")

    def _ok(self):
        bid = self.idl.text().strip()
        if not bid or not ID_RE.match(bid):
            QMessageBox.warning(self, "Фраза", "id — латиница, цифры, _ и -")
            return
        if any(o.id == bid for o in self.others):
            QMessageBox.warning(self, "Фраза", "Фраза с id «%s» уже есть" % bid)
            return
        self.b.id = bid
        self.b.weight = round(self.weight.value(), 2)
        self.b.cooldown = float(self.cool.value())
        self.b.chance = self.chance.value()
        self.b.once = self.once.isChecked()
        self.accept()


class BarkList(QWidget):
    """Barks of an NPC or a set: list + add / edit / duplicate / delete / move."""
    changed = Signal()

    def __init__(self, ctx, barks, max_height=260):
        super().__init__()
        self.ctx = ctx
        self.barks = barks
        lay = QVBoxLayout(self)
        lay.setContentsMargins(0, 0, 0, 0)
        lay.setSpacing(4)
        self.list = QListWidget()
        self.list.setWordWrap(True)
        self.list.setMaximumHeight(max_height)
        self.list.itemDoubleClicked.connect(lambda _: self.edit())
        lay.addWidget(self.list)
        self.empty = hint("Фраз нет.")
        lay.addWidget(self.empty)
        bl = QHBoxLayout()
        bl.setSpacing(4)
        add = QToolButton(text="+ фраза")
        add.clicked.connect(self.add)
        bl.addWidget(add)
        for text, tip, fn in (("✎", "Изменить", self.edit), ("⧉", "Дублировать", self.duplicate), ("✕", "Удалить", self.remove),
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
        for b in self.barks:
            self.list.addItem(QListWidgetItem(describe_bark(self.ctx, b)))
        self.list.setVisible(bool(self.barks))
        self.empty.setVisible(not self.barks)

    def _dialog(self, b, others):
        d = BarkDialog(self.ctx, b, others, self)
        return d.b if d.exec() else None

    def add(self):
        b = sd.Bark(sd.new_bark_id(self.barks, "idle"), "idle")
        r = self._dialog(b, self.barks)
        if r is not None:
            self.barks.append(r)
            self.refresh()
            self.list.setCurrentRow(len(self.barks) - 1)
            self.changed.emit()

    def edit(self):
        i = self.list.currentRow()
        if not 0 <= i < len(self.barks):
            return
        r = self._dialog(self.barks[i], [x for x in self.barks if x is not self.barks[i]])
        if r is not None:
            self.barks[i] = r
            self.refresh()
            self.list.setCurrentRow(i)
            self.changed.emit()

    def duplicate(self):
        i = self.list.currentRow()
        if not 0 <= i < len(self.barks):
            return
        b = self.barks[i].clone()
        b.id = sd.new_bark_id(self.barks, b.event)
        b.voices = {}
        self.barks.insert(i + 1, b)
        self.refresh()
        self.list.setCurrentRow(i + 1)
        self.changed.emit()

    def remove(self):
        i = self.list.currentRow()
        if 0 <= i < len(self.barks):
            del self.barks[i]
            self.refresh()
            self.changed.emit()

    def move(self, d):
        i = self.list.currentRow()
        j = i + d
        if 0 <= i < len(self.barks) and 0 <= j < len(self.barks):
            self.barks[i], self.barks[j] = self.barks[j], self.barks[i]
            self.refresh()
            self.list.setCurrentRow(j)
            self.changed.emit()


def barks_tab(insp, npc):
    """NPC tab "Barks": HL2 speech, shared sets, own barks."""
    ctx = insp.ctx
    p = ctx.project
    a = npc.actor
    w = QWidget()
    lay = QVBoxLayout(w)
    lay.setContentsMargins(0, 6, 0, 0)
    lay.addWidget(hint("Фразы, которые NPC говорит сам (с субтитрами): подошёл игрок, бродит, ранен, бой, игрок с оружием, "
                       "прощание после диалога… У каждой — условия: так фразы меняются по ходу сюжета."))

    def ch():
        insp.changed(False)
        ctx.world_changed("actor")
    g, gl = _group("Фразы HL2")
    f = _form()
    ss = _combo(sd.STOCK_SPEECH, a.stock_speech)

    def set_ss(_):
        a.stock_speech = ss.currentData()
        ch()
    ss.currentIndexChanged.connect(set_ss)
    f.addRow("Говорит:", ss)
    gl.addLayout(f)
    gl.addWidget(hint("Стоковые реплики Half-Life 2 (приветствия горожан, болтовня солдат). Фразы о Фримене и героях HL2 "
                      "не звучат никогда (scripts/hl2rpm_speech_block.txt)."))
    lay.addWidget(g)
    g, gl = _group("Общие наборы фраз")
    if not p.bark_sets:
        gl.addWidget(hint("Наборов пока нет — вкладка «Звук» → «+ набор фраз» (например, фразы всех горожан)."))
    else:
        gl.addWidget(hint("Ничего не отмечено — NPC говорит наборы своего класса (как NPC из Hammer). "
                          "Отмечено — только отмеченные."))
        lst = QListWidget()
        lst.setMaximumHeight(120)
        for s in p.bark_sets:
            it = QListWidgetItem("%s  (%d фраз%s)" % (s.id, len(s.barks), (", классы: " + ", ".join(s.classes)) if s.classes else ""))
            it.setData(Qt.UserRole, s.id)
            it.setFlags(it.flags() | Qt.ItemIsUserCheckable)
            it.setCheckState(Qt.Checked if s.id in a.bark_sets else Qt.Unchecked)
            lst.addItem(it)
        none = QCheckBox("Без общих фраз вовсе")
        none.setChecked(a.bark_sets == ["-"])

        def store(*_):
            if none.isChecked():
                a.bark_sets = ["-"]
            else:
                a.bark_sets = [lst.item(i).data(Qt.UserRole) for i in range(lst.count()) if lst.item(i).checkState() == Qt.Checked]
            lst.setEnabled(not none.isChecked())
            ch()
        lst.setEnabled(not none.isChecked())
        lst.itemChanged.connect(store)
        none.toggled.connect(store)
        gl.addWidget(lst)
        gl.addWidget(none)
    lay.addWidget(g)
    g, gl = _group("Свои фразы (%d)" % len(a.barks))
    if not a.barks and a.template and p.npc(a.template) and p.npc(a.template).actor.barks:
        gl.addWidget(hint("Своих нет — говорит фразы шаблона «%s»." % a.template))
    bl = BarkList(ctx, a.barks)
    bl.changed.connect(ch)
    gl.addWidget(bl)
    lay.addWidget(g)
    lay.addStretch(1)
    return w


# --- the "Sound" tab -------------------------------------------------------------------------------------------
class SoundMixin:
    def _build_sound_tab(self):
        w = QWidget()
        l = QVBoxLayout(w)
        l.setContentsMargins(4, 4, 4, 4)
        l.addWidget(hint("Музыка (исследование день/ночь, бой, темы локаций, вставки), звуковые сцены локаций и общие фразы NPC."))
        self.sound_tree = QTreeWidget()
        self.sound_tree.setHeaderHidden(True)
        self.sound_tree.itemClicked.connect(self._sound_clicked)
        l.addWidget(self.sound_tree, 1)
        bl = QHBoxLayout()
        b = QPushButton("+ трек")
        b.clicked.connect(lambda: self.new_track())
        bl.addWidget(b)
        b = QPushButton("Импорт музыки…")
        b.setToolTip("MP3/WAV/OGG/FLAC с диска: файлы лягут в sound/%s/ папки игры, для каждого — трек" % MUSIC_DIR)
        b.clicked.connect(self.import_music)
        bl.addWidget(b)
        l.addLayout(bl)
        bl = QHBoxLayout()
        b = QPushButton("+ звуковая сцена")
        b.clicked.connect(lambda: self.new_soundscape())
        bl.addWidget(b)
        b = QPushButton("+ набор фраз")
        b.clicked.connect(lambda: self.new_bark_set())
        bl.addWidget(b)
        l.addLayout(bl)
        b = QPushButton("Удалить выбранное")
        b.clicked.connect(self.delete_sound_selected)
        l.addWidget(b)
        self.sound_tab = w
        return w

    def refresh_sound_tree(self):
        if not hasattr(self, "sound_tree"):
            return
        t = self.sound_tree
        t.clear()
        if not self.project:
            return
        p = self.project
        top = QTreeWidgetItem(["Музыка (%d)" % len(p.music)])
        top.setData(0, Qt.UserRole, ("music",))
        t.addTopLevelItem(top)
        for kind, kname in sd.MUSIC_KINDS:
            tracks = [x for x in p.music if x.kind == kind]
            if not tracks:
                continue
            k = QTreeWidgetItem([kname])
            k.setData(0, Qt.UserRole, ("music",))
            top.addChild(k)
            for x in tracks:
                extra = ""
                if x.kind in ("explore", "combat", "location") and x.time != "any":
                    extra = "  · " + sd.MUSIC_TIME_NAMES.get(x.time, x.time)
                if x.kind == "stinger" and x.event:
                    extra = "  · " + sd.STINGER_EVENT_NAMES.get(x.event, x.event)
                it = QTreeWidgetItem([x.label() + extra])
                it.setData(0, Qt.UserRole, ("track", x.id))
                if x.file and not sound_exists(self, x.file):
                    it.setForeground(0, Qt.red)
                    it.setToolTip(0, "Нет файла sound/%s" % x.file)
                k.addChild(it)
        top.setExpanded(True)
        for i in range(top.childCount()):
            top.child(i).setExpanded(True)
        top = QTreeWidgetItem(["Звуковые сцены локаций (%d)" % len(p.soundscapes)])
        top.setData(0, Qt.UserRole, ("soundscapes",))
        t.addTopLevelItem(top)
        for s in p.soundscapes:
            it = QTreeWidgetItem(["%s  (%d петель, %d случайных)" % (s.name, len(s.loops), len(s.randoms))])
            it.setData(0, Qt.UserRole, ("soundscape", s.name))
            top.addChild(it)
        top.setExpanded(True)
        top = QTreeWidgetItem(["Наборы фраз NPC (%d)" % len(p.bark_sets)])
        top.setData(0, Qt.UserRole, ("barksets",))
        t.addTopLevelItem(top)
        for s in p.bark_sets:
            it = QTreeWidgetItem(["%s  (%d фраз%s)" % (s.id, len(s.barks), (", " + ", ".join(s.classes)) if s.classes else "")])
            it.setData(0, Qt.UserRole, ("barkset", s.id))
            top.addChild(it)
        top.setExpanded(True)

    def _sound_clicked(self, it, _col=0):
        target = it.data(0, Qt.UserRole)
        if target:
            self.inspector.show_target(target)

    def _sound_changed(self, target=None):
        self.model_changed(True)
        self.refresh_sound_tree()
        self.left_tabs.setCurrentWidget(self.sound_tab)
        if target:
            self.inspector.show_target(target)

    def _unique(self, base, used):
        if base not in used:
            return base
        n = 2
        while "%s_%d" % (base, n) in used:
            n += 1
        return "%s_%d" % (base, n)

    def new_track(self, file="", kind="explore", ask=True):
        if not self.project:
            return None
        used = {t.id for t in self.project.music}
        tid = self._unique(_safe_name(file)[0] if file else "track", used)
        if ask:
            tid, ok = QInputDialog.getText(self, "Новый трек", "id трека (латиница, цифры, _):", text=tid)
            tid = (tid or "").strip()
            if not ok or not tid:
                return None
            if tid in used or not ID_RE.match(tid):
                QMessageBox.warning(self, "Трек", "Такой id уже есть или в нём не только латиница, цифры, _ и -")
                return None
        t = sd.MusicTrack(tid)
        t.file = file
        t.kind = kind
        self.project.music.append(t)
        if ask:
            self._sound_changed(("track", tid))
        return t

    def import_music(self):
        if not self.project:
            return
        gd = self.project.settings.game_dir
        if not gd or not os.path.isdir(gd):
            QMessageBox.warning(self, "Музыка", "Папка игры не найдена (Проект → Настройки)")
            return
        start = getattr(self, "last_audio_dir", "") or gd
        paths, _ = QFileDialog.getOpenFileNames(self, "Музыка для игры", start, AUDIO_FILTER)
        if not paths:
            return
        self.last_audio_dir = os.path.dirname(paths[0])
        done, errors, last = [], [], None
        for src in paths:
            rel, msg = import_music_file(self, src)
            if not rel:
                errors.append("%s: %s" % (os.path.basename(src), msg))
                continue
            t = next((x for x in self.project.music if x.file == rel), None) or self.new_track(rel, ask=False)
            last = t
            done.append("%s → sound/%s (%s)" % (os.path.basename(src), rel, msg))
        if last is not None:
            self._sound_changed(("track", last.id))
        text = "\n".join(done + (["", "Ошибки:"] + errors if errors else []))
        QMessageBox.information(self, "Импорт музыки", text or "Ничего не импортировано")

    def new_soundscape(self):
        if not self.project:
            return
        used = {s.name for s in self.project.soundscapes}
        name, ok = QInputDialog.getText(self, "Новая звуковая сцена", "Имя (латиница, точки: hl2rpm.square):",
                                        text=self._unique("hl2rpm.location", used))
        name = (name or "").strip()
        if not ok or not name:
            return
        if name in used or not SCAPE_RE.match(name):
            QMessageBox.warning(self, "Звуковая сцена", "Такое имя уже есть или в нём не только латиница, цифры, _ - .")
            return
        s = sd.Soundscape(name)
        s.loops.append(sd.new_loop("ambient/atmosphere/city_rumble_loop1.wav", 0.5))
        self.project.soundscapes.append(s)
        self._sound_changed(("soundscape", name))

    def new_bark_set(self):
        if not self.project:
            return
        used = {s.id for s in self.project.bark_sets}
        sid, ok = QInputDialog.getText(self, "Новый набор фраз", "id набора (латиница, цифры, _):", text=self._unique("citizens", used))
        sid = (sid or "").strip()
        if not ok or not sid:
            return
        if sid in used or not ID_RE.match(sid):
            QMessageBox.warning(self, "Набор фраз", "Такой id уже есть или в нём не только латиница, цифры, _ и -")
            return
        s = sd.BarkSet(sid)
        self.project.bark_sets.append(s)
        self._sound_changed(("barkset", sid))

    def delete_sound_selected(self):
        it = self.sound_tree.currentItem() if hasattr(self, "sound_tree") else None
        t = it.data(0, Qt.UserRole) if it else None
        if not t or t[0] not in ("track", "soundscape", "barkset"):
            return
        p = self.project
        name = {"track": "трек", "soundscape": "звуковую сцену", "barkset": "набор фраз"}[t[0]]
        if QMessageBox.question(self, "Удалить", "Удалить %s «%s»?" % (name, t[1])) != QMessageBox.Yes:
            return
        if t[0] == "track":
            p.music[:] = [x for x in p.music if x.id != t[1]]
        elif t[0] == "soundscape":
            p.soundscapes[:] = [x for x in p.soundscapes if x.name != t[1]]
        else:
            p.bark_sets[:] = [x for x in p.bark_sets if x.id != t[1]]
        self.model_changed(True)
        self.refresh_sound_tree()
        self.inspector.show_target(("music",))


# --- inspector pages -------------------------------------------------------------------------------------------
def _track_users(p, tid):
    out = []
    for name, layer in sorted(p.maps.items()):
        for r in layer.refs:
            if r.type == "zone" and tid in r.music:
                out.append("локация %s (%s)" % (r.name or r.id, name))
    for where, kind, lst in logic.iter_logic(p):
        if kind != "action":
            continue
        for a in lst or []:
            if a.get("type") in ("music", "stinger") and tid in str(a.get("value", "")).split(";"):
                out.append(where)
    return out


def music_page(insp):
    ctx = insp.ctx
    p = ctx.project
    w, lay = insp._page("Музыка",
                        "Как в New Vegas: фоновая музыка исследования (днём и ночью свои треки, между треками — пауза), "
                        "в бою — боевая, в локации со своей музыкой — её тема. Смены — с плавным переходом. "
                        "Громкость в игре — ползунок «Музыка» в настройках звука.")
    g, gl = _group("Что играет и когда")
    rows = []
    for kind, kname in sd.MUSIC_KINDS:
        n = [x for x in p.music if x.kind == kind]
        rows.append("<b>%s</b>: %d" % (kname, len(n)))
    gl.addWidget(QLabel("<br>".join(rows)))
    gl.addWidget(hint("Порядок: действие «Сменить музыку» (пока его не снимут) → бой (враг атакует игрока, +8 с) → "
                      "музыка локации (зона, Свойства → Локация) → исследование. Стингер (вставка) играет поверх, "
                      "музыка на это время притихает: открыта локация, задание выдано / выполнено / провалено, "
                      "или действием «Музыкальная вставка»."))
    lay.addWidget(g)
    bl = QHBoxLayout()
    b = QPushButton("+ трек")
    b.clicked.connect(lambda: ctx.new_track())
    bl.addWidget(b)
    b = QPushButton("Импорт музыки…")
    b.clicked.connect(ctx.import_music)
    bl.addWidget(b)
    bl.addStretch(1)
    lay.addLayout(bl)
    if not p.music:
        g, gl = _group("Начать со стоковых треков HL2")
        gl.addWidget(hint("Добавит несколько треков Half-Life 2 (они уже есть в игре): исследование днём и ночью, бой, "
                          "вставки «задание выполнено» и «новая локация». Их потом можно заменить своими."))
        b = QPushButton("Добавить треки HL2")

        def add_stock():
            for tid, f, kind, time, ev in (("hl2_day", "music/hl2_song26_trainstation1.mp3", "explore", "day", ""),
                                           ("hl2_night", "music/hl2_song27_trainstation2.mp3", "explore", "night", ""),
                                           ("hl2_combat", "music/hl2_song3.mp3", "combat", "any", ""),
                                           ("hl2_quest_done", "music/hl2_song23_suitsong3.mp3", "stinger", "any", "quest_complete"),
                                           ("hl2_location", "music/hl2_song25_teleporter.mp3", "stinger", "any", "location")):
                t = sd.MusicTrack(tid)
                t.file, t.kind, t.time, t.event = f, kind, time, ev
                p.music.append(t)
            insp.changed(True)
            ctx.refresh_sound_tree()
            insp.rebuild_soon()
        b.clicked.connect(add_stock)
        gl.addWidget(b)
        lay.addWidget(g)
    return w, lay


def track_page(insp, t):
    ctx = insp.ctx
    p = ctx.project
    w, lay = insp._page("Трек  %s" % t.id, "Файл — MP3 или WAV в папке sound/ игры (свои — кнопкой «Импорт файла…»).")

    def changed(structural=False):
        insp.changed(structural)
        ctx.refresh_sound_tree()
    f = _form()
    idl = QLineEdit(t.id)

    def rename():
        new = idl.text().strip()
        if new == t.id:
            return
        if not new or not ID_RE.match(new) or p.track(new):
            QMessageBox.warning(insp, "id трека", "Пустой, не латиница или уже существует")
            idl.setText(t.id)
            return
        old = t.id
        t.id = new
        for layer in p.maps.values():
            for r in layer.refs:
                if r.type == "zone":
                    r.music = [new if x == old else x for x in r.music]
        for _w, kind, lst in logic.iter_logic(p):
            for a in lst or []:
                if kind == "action" and a.get("type") in ("music", "stinger"):
                    a["value"] = ";".join(new if x == old else x for x in str(a.get("value", "")).split(";") if x)
        changed(True)
        insp.show_target(("track", new))
    idl.editingFinished.connect(rename)
    f.addRow("id:", idl)
    sf = SoundField(ctx, t.file, "music/", "music/hl2_song3.mp3")
    sf.changed.connect(lambda v: (setattr(t, "file", v), changed()))
    f.addRow("Файл:", sf)
    ib = QPushButton("Импорт файла…")
    ib.setToolTip("Свой MP3/WAV/OGG/FLAC: ляжет в sound/%s/ папки игры" % MUSIC_DIR)

    def imp():
        start = getattr(ctx, "last_audio_dir", "") or p.settings.game_dir
        path, _ = QFileDialog.getOpenFileName(insp, "Музыка", start, AUDIO_FILTER)
        if not path:
            return
        ctx.last_audio_dir = os.path.dirname(path)
        rel, msg = import_music_file(ctx, path)
        if not rel:
            QMessageBox.warning(insp, "Музыка", msg)
            return
        t.file = rel
        changed()
        insp.rebuild_soon()
    ib.clicked.connect(imp)
    f.addRow("", ib)
    vol = QDoubleSpinBox()
    vol.setRange(0.05, 1.0)
    vol.setSingleStep(0.05)
    vol.setValue(t.volume)
    vol.valueChanged.connect(lambda v: (setattr(t, "volume", round(v, 2)), changed()))
    f.addRow("Громкость:", vol)
    kc = _combo(sd.MUSIC_KINDS, t.kind)

    def set_kind(_):
        t.kind = kc.currentData()
        changed(True)
        insp.rebuild_soon()
    kc.currentIndexChanged.connect(set_kind)
    f.addRow("Что это:", kc)
    if t.kind in ("explore", "combat", "location"):
        tc = _combo(sd.MUSIC_TIMES, t.time)
        tc.currentIndexChanged.connect(lambda _: (setattr(t, "time", tc.currentData()), changed()))
        f.addRow("Когда:", tc)
    if t.kind == "stinger":
        ec = _combo(sd.STINGER_EVENTS, t.event)
        ec.currentIndexChanged.connect(lambda _: (setattr(t, "event", ec.currentData()), changed()))
        f.addRow("Сам играет, когда:", ec)
    if t.kind != "location":
        files = ctx.map_files() if hasattr(ctx, "map_files") else {}
        maps = sorted(set(list(p.maps.keys()) + list(files.keys() if isinstance(files, dict) else files)))
        ml = completer_line(";".join(t.maps), maps, "пусто — на всех картах; несколько — через ;")
        ml.editingFinished.connect(lambda: (setattr(t, "maps", [x.strip() for x in ml.text().split(";") if x.strip()]), changed()))
        f.addRow("Только на картах:", ml)
    lay.addLayout(f)
    tips = {
        "explore": "Играет, когда ничего особенного не происходит. Треки одного времени суток чередуются случайно, "
                   "между ними пауза 12–40 с, как в New Vegas.",
        "combat": "Включается, когда враг атакует игрока (в 2500 ед.), и гаснет через 8 с после боя.",
        "location": "Тема локации: выберите этот трек в зоне (Свойства зоны → Локация → Музыка).",
        "stinger": "Короткая вставка поверх музыки: сама — на выбранное событие, или действием «Музыкальная вставка».",
    }
    lay.addWidget(hint(tips.get(t.kind, "")))
    users = _track_users(p, t.id)
    g, gl = _group("Где используется (%d)" % len(users))
    gl.addWidget(hint(", ".join(users) if users else ("Нигде — " + ("выберите его в зоне." if t.kind == "location" else
                                                                     "играет сам по правилам выше."))))
    lay.addWidget(g)
    g, gl = _group("Заметка")
    note = AutoText(t.note, 1, 4)
    note.edited.connect(lambda s: (setattr(t, "note", s), insp.changed(False)))
    gl.addWidget(note)
    lay.addWidget(g)
    return w, lay


def _range_row(values, lo, hi, step, dec, suffix=""):
    row = QHBoxLayout()
    spins = []
    for i in range(2):
        s = QDoubleSpinBox()
        s.setRange(lo, hi)
        s.setSingleStep(step)
        s.setDecimals(dec)
        s.setSuffix(suffix)
        s.setValue(float(values[i]))
        row.addWidget(s)
        if i == 0:
            row.addWidget(QLabel("—"))
        spins.append(s)
    return row, spins


def soundscape_page(insp, s):
    ctx = insp.ctx
    p = ctx.project
    w, lay = insp._page("Звуковая сцена  %s" % s.name,
                        "Фон локации: петли звучат всё время, случайные звуки — раз в несколько секунд (собака, поезд, "
                        "далёкий бой). Работает в зоне, где она выбрана (Свойства зоны → Локация), — штатная система "
                        "звуковых сцен Source (env_soundscape).")

    def changed(structural=False):
        insp.changed(structural)
        ctx.refresh_sound_tree()
    f = _form()
    nm = QLineEdit(s.name)

    def rename():
        new = nm.text().strip()
        if new == s.name:
            return
        if not new or not SCAPE_RE.match(new) or p.soundscape(new):
            QMessageBox.warning(insp, "Имя сцены", "Пустое, не латиница или уже существует")
            nm.setText(s.name)
            return
        old = s.name
        s.name = new
        for layer in p.maps.values():
            for r in layer.refs:
                if r.type == "zone":
                    for attr in ("soundscape", "soundscape_night", "soundscape_rain"):
                        if getattr(r, attr) == old:
                            setattr(r, attr, new)
        changed(True)
        insp.show_target(("soundscape", new))
    nm.editingFinished.connect(rename)
    f.addRow("Имя:", nm)
    dc = _combo(sd.SOUNDSCAPE_DSP, s.dsp)
    if dc.findData(s.dsp) < 0:
        dc.addItem(str(s.dsp), s.dsp)
        dc.setCurrentIndex(dc.count() - 1)
    dc.currentIndexChanged.connect(lambda _: (setattr(s, "dsp", int(dc.currentData())), changed()))
    f.addRow("Эхо (DSP):", dc)
    lay.addLayout(f)

    g, gl = _group("Петли (звучат всё время)")
    for i, lp in enumerate(s.loops):
        row = QHBoxLayout()
        sf = SoundField(ctx, lp.get("wave", ""), "ambient/", "ambient/atmosphere/city_rumble_loop1.wav")
        sf.changed.connect(lambda v, lp=lp: (lp.__setitem__("wave", v), changed()))
        row.addWidget(sf, 1)
        vs = QDoubleSpinBox()
        vs.setRange(0.0, 1.0)
        vs.setSingleStep(0.05)
        vs.setValue(float(lp.get("volume", 0.6)))
        vs.setToolTip("Громкость")
        vs.valueChanged.connect(lambda v, lp=lp: (lp.__setitem__("volume", round(v, 2)), changed()))
        row.addWidget(vs)
        ps = QSpinBox()
        ps.setRange(30, 250)
        ps.setValue(int(lp.get("pitch", 100)))
        ps.setToolTip("Высота тона, 100 — как есть")
        ps.valueChanged.connect(lambda v, lp=lp: (lp.__setitem__("pitch", v), changed()))
        row.addWidget(ps)
        xb = QToolButton(text="✕")
        xb.clicked.connect(lambda _=False, i=i: (s.loops.pop(i), changed(True), insp.rebuild_soon()))
        row.addWidget(xb)
        gl.addLayout(row)
    ab = QPushButton("+ петля")
    ab.clicked.connect(lambda: (s.loops.append(sd.new_loop()), changed(True), insp.rebuild_soon()))
    gl.addWidget(ab)
    lay.addWidget(g)

    g, gl = _group("Случайные звуки")
    for i, r in enumerate(s.randoms):
        box = QGroupBox("Группа %d — случайный из списка раз в %g–%g с" % (i + 1, r["time"][0], r["time"][1]))
        bl = QVBoxLayout(box)
        for j, wv in enumerate(r["waves"]):
            row = QHBoxLayout()
            sf = SoundField(ctx, wv, "ambient/")
            sf.changed.connect(lambda v, r=r, j=j: (r["waves"].__setitem__(j, v), changed()))
            row.addWidget(sf, 1)
            xb = QToolButton(text="✕")
            xb.clicked.connect(lambda _=False, r=r, j=j: (r["waves"].pop(j), changed(), insp.rebuild_soon()))
            row.addWidget(xb)
            bl.addLayout(row)
        ab = QPushButton("+ звук в группу")

        def add_wave(_=False, r=r):
            d = SoundPicker(ctx, "", "ambient/", parent=insp)
            if d.exec() and d.value():
                r["waves"].append(d.value())
                changed()
                insp.rebuild_soon()
        ab.clicked.connect(add_wave)
        bl.addWidget(ab)
        rf = _form()
        for key, label, lo, hi, step, dec, suf in (("time", "Каждые:", 0.5, 600, 1, 1, " с"), ("volume", "Громкость:", 0, 1, 0.05, 2, ""),
                                                  ("pitch", "Тон:", 30, 250, 5, 0, "")):
            row, spins = _range_row(r[key], lo, hi, step, dec, suf)
            for k in range(2):
                spins[k].valueChanged.connect(lambda v, r=r, key=key, k=k: (r[key].__setitem__(k, round(v, 2)), changed()))
            rf.addRow(label, row)
        bl.addLayout(rf)
        xb = QPushButton("Удалить группу")
        xb.clicked.connect(lambda _=False, i=i: (s.randoms.pop(i), changed(True), insp.rebuild_soon()))
        bl.addWidget(xb)
        gl.addWidget(box)
    ab = QPushButton("+ группа случайных звуков")
    ab.clicked.connect(lambda: (s.randoms.append(sd.new_random()), changed(True), insp.rebuild_soon()))
    gl.addWidget(ab)
    lay.addWidget(g)
    users = ["%s (%s)" % (r.name or r.id, name) for name, layer in sorted(p.maps.items()) for r in layer.refs
             if r.type == "zone" and s.name in (r.soundscape, r.soundscape_night, r.soundscape_rain)]
    g, gl = _group("Локации с этой сценой (%d)" % len(users))
    gl.addWidget(hint(", ".join(users) if users else "Пока нигде: поставьте зону (Объекты → Прочее → Зона) и выберите эту сцену в её свойствах."))
    lay.addWidget(g)
    g, gl = _group("Заметка")
    note = AutoText(s.note, 1, 4)
    note.edited.connect(lambda t: (setattr(s, "note", t), insp.changed(False)))
    gl.addWidget(note)
    lay.addWidget(g)
    return w, lay


def barkset_page(insp, s):
    ctx = insp.ctx
    p = ctx.project
    w, lay = insp._page("Набор фраз  %s" % s.id,
                        "Общие фразы: их говорят NPC перечисленных классов (и поставленные в Hammer) — если у NPC из "
                        "программы не выбраны свои наборы, — и NPC, у которых этот набор отмечен на вкладке «Фразы».")

    def changed(structural=False):
        insp.changed(structural)
        ctx.refresh_sound_tree()
    f = _form()
    idl = QLineEdit(s.id)

    def rename():
        new = idl.text().strip()
        if new == s.id:
            return
        if not new or not ID_RE.match(new) or p.bark_set(new):
            QMessageBox.warning(insp, "id набора", "Пустой, не латиница или уже существует")
            idl.setText(s.id)
            return
        old = s.id
        s.id = new
        for n in p.npcs:
            n.actor.bark_sets = [new if x == old else x for x in n.actor.bark_sets]
        changed(True)
        insp.show_target(("barkset", new))
    idl.editingFinished.connect(rename)
    f.addRow("id:", idl)
    cl = completer_line(";".join(s.classes), [c for c, _n, _a, _b in wd.NPC_CLASSES], "npc_citizen;npc_metropolice")
    cl.editingFinished.connect(lambda: (setattr(s, "classes", [x.strip() for x in cl.text().split(";") if x.strip()]), changed()))
    f.addRow("Классы NPC:", cl)
    lay.addLayout(f)
    users = [n.targetname for n in p.npcs if s.id in n.actor.bark_sets]
    lay.addWidget(hint(("Выбран у NPC: " + ", ".join(users)) if users else "Ни у одного NPC не выбран отдельно — работает по классам."))
    g, gl = _group("Фразы (%d)" % len(s.barks))
    bl = BarkList(ctx, s.barks, 360)
    bl.changed.connect(lambda: changed())
    gl.addWidget(bl)
    lay.addWidget(g)
    g, gl = _group("Заметка")
    note = AutoText(s.note, 1, 4)
    note.edited.connect(lambda t: (setattr(s, "note", t), insp.changed(False)))
    gl.addWidget(note)
    lay.addWidget(g)
    return w, lay


def soundscapes_page(insp):
    w, lay = insp._page("Звуковые сцены локаций",
                        "Фон каждой локации: гул города, ветер, капли в подвале, далёкие выстрелы. Выберите сцену в зоне "
                        "(Свойства зоны → Локация) — игрок, войдя в зону, слышит её (смена — плавная).")
    b = QPushButton("+ звуковая сцена")
    b.clicked.connect(lambda: insp.ctx.new_soundscape())
    lay.addWidget(b)
    return w, lay


def barksets_page(insp):
    w, lay = insp._page("Наборы фраз NPC",
                        "Общие фразы для многих NPC сразу: «все горожане ворчат про пайки», «патруль ГО». Свои фразы "
                        "конкретного NPC — на его вкладке «Фразы».")
    b = QPushButton("+ набор фраз")
    b.clicked.connect(lambda: insp.ctx.new_bark_set())
    lay.addWidget(b)
    return w, lay


# --- world: zone = location, sound on the map ---------------------------------------------------------------
def zone_location_group(insp, ref, ch):
    ctx = insp.ctx
    p = ctx.project
    g, gl = _group("Локация")
    gl.addWidget(hint("Если у зоны есть название, при первом входе игрок видит «Новая локация: …», получает опыт и вставку "
                      "музыки «открыта локация». Музыка и звуковая сцена играют, пока игрок внутри."))
    lt = LangTexts(ctx, ref.display_name, None, multiline=False, placeholder="название локации (пусто — не локация)")
    lt.changed.connect(lambda: ch())
    gl.addWidget(lt)
    f = _form()
    xp = QSpinBox()
    xp.setRange(0, 100000)
    xp.setValue(int(ref.discover_xp))
    xp.valueChanged.connect(lambda v: (setattr(ref, "discover_xp", v), ch()))
    f.addRow("Опыт за открытие:", xp)
    tracks = [t for t in p.music if t.kind == "location"] + [t for t in p.music if t.kind not in ("location", "stinger")]
    ml = QListWidget()
    ml.setMaximumHeight(110)
    for t in tracks:
        it = QListWidgetItem("%s%s" % (t.label(), "" if t.kind == "location" else "  (%s)" % sd.MUSIC_KIND_NAMES.get(t.kind, t.kind)))
        it.setData(Qt.UserRole, t.id)
        it.setFlags(it.flags() | Qt.ItemIsUserCheckable)
        it.setCheckState(Qt.Checked if t.id in ref.music else Qt.Unchecked)
        ml.addItem(it)

    def store_music(*_):
        ref.music = [ml.item(i).data(Qt.UserRole) for i in range(ml.count()) if ml.item(i).checkState() == Qt.Checked]
        ch()
    ml.itemChanged.connect(store_music)
    if tracks:
        f.addRow("Музыка:", ml)
    else:
        f.addRow("Музыка:", hint("Треков нет — вкладка «Звук» → «+ трек» (тип «Тема локации»)."))
    sc = QComboBox()
    sc.addItem("— без своей звуковой сцены —", "")
    for s in p.soundscapes:
        sc.addItem(s.name, s.name)
    if ref.soundscape and sc.findData(ref.soundscape) < 0:
        sc.addItem("%s (нет в проекте)" % ref.soundscape, ref.soundscape)
    sc.setCurrentIndex(max(0, sc.findData(ref.soundscape)))
    sc.currentIndexChanged.connect(lambda _: (setattr(ref, "soundscape", sc.currentData()), ch()))
    srow = QHBoxLayout()
    srow.addWidget(sc, 1)
    nb = QToolButton(text="+")
    nb.setToolTip("Новая звуковая сцена")
    nb.clicked.connect(lambda: ctx.new_soundscape())
    srow.addWidget(nb)
    f.addRow("Звуковая сцена:", srow)

    def variant(attr, empty):
        cb = QComboBox()
        cb.addItem(empty, "")
        for s_ in p.soundscapes:
            cb.addItem(s_.name, s_.name)
        cur = getattr(ref, attr)
        if cur and cb.findData(cur) < 0:
            cb.addItem("%s (нет в проекте)" % cur, cur)
        cb.setCurrentIndex(max(0, cb.findData(cur)))
        cb.currentIndexChanged.connect(lambda _: (setattr(ref, attr, cb.currentData()), ch()))
        return cb
    f.addRow("Ночью (21–6 ч):", variant("soundscape_night", "— та же —"))
    f.addRow("В дождь:", variant("soundscape_rain", "— та же —"))
    gl.addLayout(f)
    gl.addWidget(hint("Дождь (морось, дождь, гроза) важнее ночи. Игра меняет сцену сама, раз в секунду, и в том числе, "
                      "пока игрок внутри локации."))
    return g


def sound_ref_group(insp, ref, ch):
    ctx = insp.ctx
    g, gl = _group("Звук")
    gl.addWidget(hint("Звук в точке карты (ambient_generic): гудит трансформатор, капает вода, играет радио. Петли (WAV с "
                      "циклом) звучат всё время, остальное — один раз при появлении. Время суток и задания — условием "
                      "присутствия ниже."))
    f = _form()
    sf = SoundField(ctx, ref.sound, "ambient/")
    sf.changed.connect(lambda v: (setattr(ref, "sound", v), ch(True)))
    f.addRow("Звук:", sf)
    vs = QDoubleSpinBox()
    vs.setRange(0.1, 1.0)
    vs.setSingleStep(0.1)
    vs.setValue(ref.volume)
    vs.valueChanged.connect(lambda v: (setattr(ref, "volume", round(v, 2)), ch()))
    f.addRow("Громкость:", vs)
    rs = QSpinBox()
    rs.setRange(64, 20000)
    rs.setSingleStep(64)
    rs.setSuffix(" ед.")
    rs.setValue(int(ref.radius))
    rs.valueChanged.connect(lambda v: (setattr(ref, "radius", float(v)), ch()))
    f.addRow("Слышно на:", rs)
    ev = QCheckBox("Слышно везде (без затухания)")
    ev.setChecked(ref.everywhere)
    ev.toggled.connect(lambda v: (setattr(ref, "everywhere", bool(v)), ch()))
    f.addRow("", ev)
    gl.addLayout(f)
    return g
