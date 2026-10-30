"""Inspector building blocks: texts in all languages with voice slots, the
voice widget (file, playback, waveform with lip sync words), node/quest pickers."""
import os

from PySide6.QtCore import Qt, Signal, QTimer, QRectF, QPointF
from PySide6.QtGui import QColor, QPainter, QPen, QFont, QFontMetrics
from PySide6.QtWidgets import (QComboBox, QFileDialog, QFrame, QHBoxLayout, QLabel, QLineEdit, QPlainTextEdit,
                               QPushButton, QSizePolicy, QToolButton, QVBoxLayout, QWidget, QCompleter, QSpinBox,
                               QMessageBox)

from .. import audio
from ..model import LANGUAGE_NAMES, Voice, clean_text, short_lang
from . import theme
from .graph import voice_status, STATUS_COLOR, STATUS_TIP

AUDIO_FILTER = "Звук (*.wav *.mp3 *.ogg *.flac *.m4a *.opus);;WAV (*.wav);;Все файлы (*)"


class AutoText(QPlainTextEdit):
    """Plain text edit that grows with its content."""
    edited = Signal(str)

    def __init__(self, text="", min_lines=2, max_lines=10, placeholder=""):
        super().__init__()
        self.min_lines = min_lines
        self.max_lines = max_lines
        self.setPlainText(text)
        self.setPlaceholderText(placeholder)
        self.setTabChangesFocus(True)
        self.setLineWrapMode(QPlainTextEdit.WidgetWidth)
        self.textChanged.connect(self._changed)
        self.document().documentLayout().documentSizeChanged.connect(self._fit)
        self._fit()

    def _changed(self):
        t = self.toPlainText()
        if '"' in t:
            # straight quotes would end a KeyValues value: typographic ones instead
            c = self.textCursor()
            pos = c.position()
            self.blockSignals(True)
            self.setPlainText(clean_text(t))
            c = self.textCursor()
            c.setPosition(min(pos, len(self.toPlainText())))
            self.setTextCursor(c)
            self.blockSignals(False)
            t = self.toPlainText()
        self.edited.emit(t)

    def _fit(self, *_):
        fm = QFontMetrics(self.font())
        lines = max(self.min_lines, min(self.max_lines, int(self.document().size().height()) or 1))
        self.setFixedHeight(int(lines * fm.lineSpacing() + 12))


class Waveform(QWidget):
    def __init__(self):
        super().__init__()
        self.peaks = []
        self.words = []
        self.duration = 0.0
        self.setFixedHeight(34)
        self.setSizePolicy(QSizePolicy.Expanding, QSizePolicy.Fixed)

    def set_file(self, path):
        self.peaks = audio.waveform(path, 300) if path and os.path.exists(path) else []
        self.words = audio.read_phonemes(path) if path and os.path.exists(path) else []
        info = audio.wav_info(path) if path and os.path.exists(path) and path.lower().endswith(".wav") else None
        self.duration = info.duration if info and info.ok else 0.0
        tips = ["%s %.2f–%.2f" % w for w in self.words[:40]]
        self.setToolTip(("Слова липсинка (из WAV):\n" + "\n".join(tips)) if tips else "")
        self.update()

    def paintEvent(self, e):
        qp = QPainter(self)
        r = self.rect()
        qp.fillRect(r, QColor("#16191d"))
        if not self.peaks:
            qp.setPen(theme.TEXT_DIM)
            qp.drawText(r, Qt.AlignCenter, "нет звука")
            return
        mid = r.height() / 2
        n = len(self.peaks)
        qp.setPen(QPen(QColor("#5d9bd8"), 1))
        for i, pk in enumerate(self.peaks):
            x = r.left() + i * r.width() / n
            h = max(1.0, pk * (r.height() / 2 - 2))
            qp.drawLine(QPointF(x, mid - h), QPointF(x, mid + h))
        if self.words and self.duration > 0:
            qp.setFont(QFont("Segoe UI", 7))
            for w, s, en in self.words:
                x0 = r.width() * s / self.duration
                x1 = r.width() * en / self.duration
                qp.fillRect(QRectF(x0, r.height() - 5, max(1.0, x1 - x0 - 1), 4), QColor(76, 175, 110, 200))


class VoiceWidget(QFrame):
    """Voice of one text in one language."""
    changed = Signal()

    def __init__(self, ctx, voices, lang, texts):
        super().__init__()
        self.ctx = ctx            # main window (project, settings, audio)
        self.voices = voices
        self.lang = lang
        self.texts = texts
        self.setAcceptDrops(True)
        lay = QVBoxLayout(self)
        lay.setContentsMargins(0, 2, 0, 0)
        lay.setSpacing(3)
        row = QHBoxLayout()
        row.setSpacing(4)
        self.status = QLabel()
        self.status.setFixedSize(12, 12)
        row.addWidget(self.status)
        self.label = QLabel()
        self.label.setObjectName("hint")
        self.label.setWordWrap(True)
        self.label.setMinimumWidth(60)
        self.label.setTextInteractionFlags(Qt.TextSelectableByMouse)
        row.addWidget(self.label, 1)
        self.play_btn = QToolButton(text="▶")
        self.play_btn.setToolTip("Прослушать")
        self.play_btn.clicked.connect(self.play)
        row.addWidget(self.play_btn)
        stop = QToolButton(text="■")
        stop.setToolTip("Стоп")
        stop.clicked.connect(audio.stop)
        row.addWidget(stop)
        self.browse_btn = QToolButton(text="Озвучка…")
        self.browse_btn.setToolTip("Выбрать файл озвучки (WAV/MP3/OGG/FLAC) — его можно и перетащить сюда.\n"
                                   "При сохранении файл будет сконвертирован и положен в sound/dialogs/<npc>/<язык>/")
        self.browse_btn.clicked.connect(self.browse)
        row.addWidget(self.browse_btn)
        self.more = QToolButton(text="⋯")
        self.more.setPopupMode(QToolButton.InstantPopup)
        from PySide6.QtWidgets import QMenu
        m = QMenu(self)
        m.addAction("Пересоздать липсинк при сохранении", self.restale)
        m.addAction("Своя сцена (.vcd из Faceposer)…", self.pick_vcd)
        m.addAction("Вернуть автоматический липсинк", self.auto_vcd)
        m.addSeparator()
        m.addAction("Показать файл в проводнике", self.reveal)
        m.addAction("Убрать озвучку", self.clear)
        self.more.setMenu(m)
        row.addWidget(self.more)
        lay.addLayout(row)
        self.wave = Waveform()
        lay.addWidget(self.wave)
        self.refresh()

    def voice(self):
        return self.voices.get(self.lang)

    def refresh(self):
        v = self.voice()
        p = self.ctx.project
        st = voice_status(p, self.texts, self.voices, self.lang)
        self.status.setStyleSheet("background:%s; border-radius:6px;" % STATUS_COLOR[st].name())
        self.status.setToolTip(STATUS_TIP[st])
        if not v or v.is_empty():
            self.label.setText("<i>нет озвучки — перетащите файл сюда</i>")
            self.wave.setVisible(False)
            self.play_btn.setEnabled(False)
            return
        path = p.game_path(v.wav) if v.wav else ""
        name = v.wav.replace("\\", "/") if v.wav else "(только сцена)"
        info = audio.wav_info(path) if path and os.path.exists(path) and path.lower().endswith(".wav") else None
        extra = ""
        if path and not os.path.exists(path):
            extra = " <span style='color:#e07b6a'>— файл не найден</span>"
        elif info is not None:
            extra = " · %s" % info.describe()
            if not info.source_ready():
                extra += " <span style='color:#d6a23a'>(сконвертируется)</span>"
        if v.vcd_kind == "custom":
            extra += " · своя сцена %s" % os.path.basename(v.vcd)
        self.label.setText("%s%s" % (name.replace("/", "/​"), extra))
        self.label.setToolTip(path)
        exists = bool(path and os.path.exists(path))
        self.play_btn.setEnabled(exists)
        self.wave.setVisible(exists)
        if exists:
            self.wave.set_file(path)

    def play(self):
        v = self.voice()
        if v and v.wav:
            audio.play(self.ctx.project.game_path(v.wav))

    def set_file(self, path):
        if not path:
            return
        if os.path.splitext(path)[1].lower() not in audio.AUDIO_EXTS:
            QMessageBox.warning(self, "Озвучка", "Это не звуковой файл: %s" % path)
            return
        v = self.voices.get(self.lang) or Voice()
        v.wav = self.ctx.project.rel_to_game(path)
        v.vcd_kind = "auto"
        v.lips_text = ""
        v.lips_audio = ""
        info = audio.wav_info(path) if path.lower().endswith(".wav") else None
        v.duration = info.duration if info and info.ok else 0.0
        self.voices[self.lang] = v
        self.ctx.last_audio_dir = os.path.dirname(path)
        self.refresh()
        self.changed.emit()

    def browse(self):
        start = getattr(self.ctx, "last_audio_dir", "") or self.ctx.project.settings.game_dir
        path, _ = QFileDialog.getOpenFileName(self, "Озвучка (%s)" % LANGUAGE_NAMES.get(self.lang, self.lang), start, AUDIO_FILTER)
        self.set_file(path)

    def pick_vcd(self):
        start = os.path.join(self.ctx.project.settings.game_dir, "scenes")
        path, _ = QFileDialog.getOpenFileName(self, "Сцена Faceposer", start, "Сцена (*.vcd)")
        if not path:
            return
        v = self.voices.get(self.lang) or Voice()
        v.vcd = self.ctx.project.rel_to_game(path)
        v.vcd_kind = "custom"
        self.voices[self.lang] = v
        self.refresh()
        self.changed.emit()

    def auto_vcd(self):
        v = self.voice()
        if v:
            v.vcd_kind = "auto"
            v.lips_text = ""
            self.refresh()
            self.changed.emit()

    def restale(self):
        v = self.voice()
        if v:
            v.lips_text = ""
            v.lips_audio = ""
            self.refresh()
            self.changed.emit()

    def reveal(self):
        v = self.voice()
        if v and v.wav:
            path = self.ctx.project.game_path(v.wav)
            if os.path.exists(path):
                import subprocess
                subprocess.Popen(["explorer", "/select,", os.path.normpath(path)])

    def clear(self):
        if self.lang in self.voices:
            del self.voices[self.lang]
            audio.stop()
            self.refresh()
            self.changed.emit()

    def dragEnterEvent(self, e):
        if e.mimeData().hasUrls():
            e.acceptProposedAction()

    def dropEvent(self, e):
        for u in e.mimeData().urls():
            if u.isLocalFile():
                self.set_file(u.toLocalFile())
                break


class LangTexts(QWidget):
    """One text in every project language (+ optional voice per language)."""
    changed = Signal()

    def __init__(self, ctx, texts, voices=None, multiline=True, byte_limit=0, placeholder=""):
        super().__init__()
        self.ctx = ctx
        self.texts = texts
        self.voices = voices
        self.byte_limit = byte_limit
        lay = QVBoxLayout(self)
        lay.setContentsMargins(0, 0, 0, 0)
        lay.setSpacing(6)
        self.edits = {}
        self.voice_widgets = {}
        self.counters = {}
        p = ctx.project
        for lang in p.languages:
            box = QFrame()
            box.setObjectName("langbox")
            bl = QVBoxLayout(box)
            bl.setContentsMargins(6, 4, 6, 6)
            bl.setSpacing(3)
            head = QHBoxLayout()
            name = QLabel("<b>%s</b> %s" % (short_lang(lang), LANGUAGE_NAMES.get(lang, lang)) +
                          ("  <span style='color:#8d97a4'>(основной)</span>" if lang == p.base_language else ""))
            head.addWidget(name)
            head.addStretch(1)
            if byte_limit:
                cnt = QLabel()
                cnt.setObjectName("hint")
                head.addWidget(cnt)
                self.counters[lang] = cnt
            bl.addLayout(head)
            if multiline:
                ed = AutoText(texts.get(lang, ""), 2, 9, placeholder)
                ed.edited.connect(lambda t, lg=lang: self._edit(lg, t))
            else:
                ed = QLineEdit(texts.get(lang, ""))
                ed.setPlaceholderText(placeholder)
                ed.textEdited.connect(lambda t, lg=lang, e=None: self._edit(lg, t))
            bl.addWidget(ed)
            self.edits[lang] = ed
            if voices is not None:
                vw = VoiceWidget(ctx, voices, lang, texts)
                vw.changed.connect(self.changed)
                bl.addWidget(vw)
                self.voice_widgets[lang] = vw
            lay.addWidget(box)
            self._count(lang)

    def _edit(self, lang, t):
        t = clean_text(t)
        if t:
            self.texts[lang] = t
        elif lang in self.texts:
            del self.texts[lang]
        self._count(lang)
        for vw in self.voice_widgets.values():
            vw.refresh() if vw.lang == lang else None
        self.changed.emit()

    def _count(self, lang):
        if lang in self.counters:
            n = len(self.texts.get(lang, "").encode("utf-8"))
            col = "#e07b6a" if n > self.byte_limit else "#8d97a4"
            self.counters[lang].setText("<span style='color:%s'>%d / %d байт</span>" % (col, n, self.byte_limit))

    def focus_lang(self, lang):
        ed = self.edits.get(lang)
        if ed:
            ed.setFocus()


class NodeCombo(QComboBox):
    """Pick a node of the NPC (or none)."""
    picked = Signal(int)

    def __init__(self, ctx, npc, current, none_label="— нет —", allow_none=True):
        super().__init__()
        self.setMaxVisibleItems(25)
        self.setSizeAdjustPolicy(QComboBox.AdjustToMinimumContentsLengthWithIcon)
        self.setMinimumContentsLength(10)
        self.setSizePolicy(QSizePolicy.Expanding, QSizePolicy.Fixed)
        self.view().setMinimumWidth(420)
        if allow_none:
            self.addItem(none_label, -1)
        lang = ctx.display_lang
        for n in sorted(npc.nodes, key=lambda x: x.id):
            t = n.line.get(lang) or n.line.get(ctx.project.base_language) or ""
            self.addItem("#%d  %s" % (n.id, (t[:60] + "…") if len(t) > 60 else t), n.id)
        i = self.findData(current)
        self.setCurrentIndex(i if i >= 0 else 0)
        self.currentIndexChanged.connect(lambda _: self.picked.emit(int(self.currentData())))


class QuestCombo(QComboBox):
    picked = Signal(str)

    def __init__(self, ctx, current, none_label="— нет —"):
        super().__init__()
        self.setSizeAdjustPolicy(QComboBox.AdjustToMinimumContentsLengthWithIcon)
        self.setMinimumContentsLength(10)
        self.setSizePolicy(QSizePolicy.Expanding, QSizePolicy.Fixed)
        self.view().setMinimumWidth(360)
        self.addItem(none_label, "")
        lang = ctx.display_lang
        for q in ctx.project.quests:
            t = q.title.get(lang) or q.title.get(ctx.project.base_language) or ""
            self.addItem("%s  — %s" % (q.id, t) if t else q.id, q.id)
        if current and self.findData(current) < 0:
            self.addItem("%s  (не описан!)" % current, current)
        i = self.findData(current or "")
        self.setCurrentIndex(i if i >= 0 else 0)
        self.currentIndexChanged.connect(lambda _: self.picked.emit(str(self.currentData())))


def completer_line(text, words, placeholder=""):
    ed = QLineEdit(text)
    ed.setPlaceholderText(placeholder)
    if words:
        c = QCompleter(sorted(set(words), key=str.lower))
        c.setCaseSensitivity(Qt.CaseInsensitive)
        c.setFilterMode(Qt.MatchContains)
        ed.setCompleter(c)
    return ed


def hint(text):
    l = QLabel(text)
    l.setObjectName("hint")
    l.setWordWrap(True)
    return l
