"""Node canvas: NPC lines as cards, player replies as rows with ports, links as
curves. Zoom with the wheel (towards the cursor), pan with the middle/right
mouse button or Space+drag, connect by dragging from a port."""
import math
import os

from PySide6.QtCore import QPointF, QRectF, Qt, Signal, QTimer
from PySide6.QtGui import (QBrush, QColor, QFont, QFontMetricsF, QPainter, QPainterPath, QPen,
                           QPolygonF, QTransform)
from PySide6.QtWidgets import QGraphicsItem, QGraphicsObject, QGraphicsPathItem, QGraphicsScene, QGraphicsView

from ..model import short_lang, text_for, media_key_node
from .. import audio
from . import theme

W = 300.0
HEADER_H = 30.0
BODY_H = 62.0
ROW_H = 30.0
ROWS_TOP = HEADER_H + BODY_H + 6.0
PORT_R = 6.0

FONT = QFont("Segoe UI", 9)
FONT_B = QFont("Segoe UI", 9, QFont.Bold)
FONT_SMALL = QFont("Segoe UI", 7, QFont.Bold)
FONT_BIG = QFont("Segoe UI", 26, QFont.Bold)
for _f in (FONT, FONT_B, FONT_SMALL, FONT_BIG):
    # glyph positions that scale with the canvas (hinted metrics break letter spacing when zoomed)
    _f.setHintingPreference(QFont.PreferNoHinting)


def voice_status(project, texts, voices, lang):
    """ok | stale | none | missing | custom"""
    v = voices.get(lang)
    if not v or v.is_empty():
        return "none"
    if v.wav:
        path = project.game_path(v.wav)
        if not os.path.exists(path):
            return "missing"
    if v.vcd_kind == "custom":
        return "custom" if v.vcd and os.path.exists(project.game_path(v.vcd)) else "missing"
    t = texts.get(lang) or ""
    if not t:
        return "stale"
    if not v.vcd or v.lips_text != t:
        return "stale"
    if not os.path.exists(project.game_path(v.vcd)):
        return "stale"
    return "ok"


STATUS_COLOR = {"ok": theme.VOICE_OK, "custom": theme.VOICE_OK, "stale": theme.VOICE_STALE,
                "none": theme.VOICE_NONE, "missing": theme.VOICE_ERR}
STATUS_TIP = {"ok": "озвучка + липсинк", "custom": "озвучка + своя сцена", "stale": "озвучка есть, липсинк создастся при сохранении",
              "none": "нет озвучки", "missing": "файл озвучки не найден"}


def _elide_lines(fm, text, width, max_lines):
    """Word-wrap `text` to at most `max_lines` lines of `width`, last one elided."""
    words = text.replace("\n", " ").split(" ")
    lines = []
    cur = ""
    for w in words:
        cand = (cur + " " + w) if cur else w
        if fm.horizontalAdvance(cand) <= width:
            cur = cand
        else:
            if cur:
                lines.append(cur)
            cur = w
            if len(lines) == max_lines:
                break
    if len(lines) < max_lines and cur:
        lines.append(cur)
    if len(lines) > max_lines:
        lines = lines[:max_lines]
    rest = " ".join(words)
    shown = " ".join(lines)
    if len(shown) < len(rest.strip()) and lines:
        lines[-1] = fm.elidedText(lines[-1] + " …", Qt.ElideRight, width)
        if not lines[-1].endswith("…"):
            lines[-1] = fm.elidedText(lines[-1] + "…", Qt.ElideRight, width)
    return [fm.elidedText(l, Qt.ElideRight, width) for l in lines]


class Badge:
    def __init__(self, text, color, tip):
        self.text = text
        self.color = color
        self.tip = tip


def option_badges(o, project):
    b = []
    if o.grant:
        b.append(Badge("+ " + o.grant, theme.BADGE_GRANT, "выдаёт квест %s" % o.grant))
    if o.complete:
        b.append(Badge("✓ " + o.complete, theme.BADGE_COMPLETE, "завершает квест %s (награда)" % o.complete))
    if o.fail:
        b.append(Badge("✗ " + o.fail, theme.BADGE_FAIL, "проваливает квест %s" % o.fail))
    if o.set_stage and o.set_stage[0]:
        b.append(Badge("этап %d" % o.set_stage[1], theme.BADGE_STAGE, "ставит квесту %s этап %d" % tuple(o.set_stage)))
    conds = []
    if o.require_rep is not None:
        conds.append("репутация ≥ %d" % o.require_rep)
    if o.require_state:
        conds.append("%s: %s" % (o.require_state[0], o.require_state[1]))
    if o.require_stage:
        conds.append("%s: этап ≥ %d" % tuple(o.require_stage))
    if conds:
        b.append(Badge("если", theme.BADGE_COND, "показывается, только если " + "; ".join(conds)))
    if o.spoils:
        b.append(Badge("конец NPC", theme.BADGE_SPOIL, "после этого NPC больше не разговаривает"))
    elif o.close and o.next == -1:
        b.append(Badge("✕", theme.BADGE_CLOSE, "закрывает диалог"))
    if o.server_sequence:
        b.append(Badge("▶ " + o.server_sequence, theme.BADGE_STAGE, "запускает на карте %s (BeginSequence)" % o.server_sequence))
    return b


class NodeItem(QGraphicsObject):
    def __init__(self, gscene, node):
        super().__init__()
        self.gs = gscene
        self.node = node
        self.nid = node.id
        self.hover_row = -2
        self.search_hit = False
        self.sim_hit = False
        self.edges = []
        self.setFlags(QGraphicsItem.ItemIsMovable | QGraphicsItem.ItemIsSelectable |
                      QGraphicsItem.ItemSendsGeometryChanges)
        self.setAcceptHoverEvents(True)
        self.setCacheMode(QGraphicsItem.DeviceCoordinateCache)
        self.setPos(node.pos[0], node.pos[1])
        self.refresh()

    # --- geometry --------------------------------------------------------------
    def rows(self):
        n = self.node
        return len(n.options) if n.options else 1

    def height(self):
        return ROWS_TOP + self.rows() * ROW_H + 6.0

    def boundingRect(self):
        return QRectF(-PORT_R - 2, -4, W + 2 * PORT_R + 4, self.height() + 8)

    def row_rect(self, i):
        return QRectF(6, ROWS_TOP + i * ROW_H, W - 12, ROW_H - 4)

    def port_pos(self, i):
        return QPointF(W, ROWS_TOP + i * ROW_H + (ROW_H - 4) / 2)

    def input_pos(self):
        return QPointF(0, HEADER_H / 2)

    def row_at(self, p):
        if p.y() < ROWS_TOP or p.y() > ROWS_TOP + self.rows() * ROW_H:
            return -1 if p.y() >= 0 else -2
        return int((p.y() - ROWS_TOP) // ROW_H)

    def port_at(self, p):
        for i in range(self.rows()):
            c = self.port_pos(i)
            if (p - c).manhattanLength() <= PORT_R + 5:
                return i
        return None

    # --- data --------------------------------------------------------------------
    def refresh(self):
        self.prepareGeometryChange()
        p = self.gs.project
        n = self.node
        lang = self.gs.lang
        self.statuses = [(lg, voice_status(p, n.line, n.voice, lg), bool(n.line.get(lg))) for lg in p.languages]
        self.update()

    def paint(self, qp, opt, widget=None):
        lod = QStyleOptionLOD(qp)
        n = self.node
        p = self.gs.project
        lang = self.gs.lang
        npc = self.gs.npc
        h = self.height()
        rect = QRectF(0, 0, W, h)
        is_start = (n.id == npc.start_node)
        hdr = theme.HEADER_START if is_start else theme.HEADER
        if not n.options and n.auto_next == -1 and (n.end or n.spoils):
            hdr = theme.HEADER_END if not n.spoils else theme.BADGE_SPOIL

        # card
        qp.setRenderHint(QPainter.Antialiasing, True)
        border = theme.SELECT if self.isSelected() else (theme.SEARCH if (self.search_hit or self.sim_hit) else theme.CARD_BORDER)
        qp.setPen(QPen(border, 2.2 if (self.isSelected() or self.search_hit or self.sim_hit) else 1.0))
        qp.setBrush(theme.CARD)
        qp.drawRoundedRect(rect, 8, 8)
        # header
        path = QPainterPath()
        path.addRoundedRect(QRectF(0, 0, W, HEADER_H + 8), 8, 8)
        clip = QPainterPath()
        clip.addRect(QRectF(0, 0, W, HEADER_H))
        qp.setPen(Qt.NoPen)
        qp.setBrush(hdr)
        qp.drawPath(path.intersected(clip))

        if lod < 0.45:
            qp.setPen(theme.TEXT)
            qp.setFont(FONT_BIG)
            qp.drawText(QRectF(0, HEADER_H, W, h - HEADER_H), Qt.AlignCenter, "#%d" % n.id)
            return

        qp.setPen(theme.TEXT)
        qp.setFont(FONT_B)
        title = "#%d" % n.id
        if is_start:
            title += "   СТАРТ"
        qp.drawText(QRectF(10, 0, 150, HEADER_H), Qt.AlignVCenter | Qt.AlignLeft, title)
        # language pills (voice status)
        x = W - 8
        qp.setFont(FONT_SMALL)
        for lg, st, has_text in reversed(self.statuses):
            label = short_lang(lg)
            pw = 26
            x -= pw
            r = QRectF(x, 7, pw - 3, 16)
            qp.setPen(Qt.NoPen)
            qp.setBrush(STATUS_COLOR[st])
            qp.drawRoundedRect(r, 4, 4)
            qp.setPen(theme.TEXT if has_text else theme.TEXT_MISSING)
            qp.drawText(r, Qt.AlignCenter, label)
        if n.sequence or n.server_sequence:
            x -= 18
            qp.setPen(theme.TEXT_DIM)
            qp.drawText(QRectF(x, 7, 16, 16), Qt.AlignCenter, "▶")

        # line text
        text, fb = text_for(n.line, lang, p.base_language)
        fm = QFontMetricsF(FONT)
        qp.setFont(FONT)
        if not text:
            qp.setPen(theme.TEXT_MISSING)
            qp.drawText(QRectF(10, HEADER_H + 4, W - 20, BODY_H - 6), Qt.AlignLeft | Qt.AlignTop, "(нет текста)")
        else:
            lines = _elide_lines(fm, text, W - 20, 3)
            qp.setPen(theme.TEXT_MISSING if fb else theme.TEXT)
            if fb:
                f = QFont(FONT)
                f.setItalic(True)
                qp.setFont(f)
            y = HEADER_H + 6
            for ln in lines:
                qp.drawText(QPointF(10, y + fm.ascent()), ln)
                y += fm.height() + 1
            qp.setFont(FONT)

        # rows
        if n.options:
            for i, o in enumerate(n.options):
                self._paint_option(qp, i, o, fm)
        else:
            r = self.row_rect(0)
            qp.setPen(Qt.NoPen)
            qp.setBrush(theme.ROW_HOVER if self.hover_row == 0 else theme.ROW)
            qp.drawRoundedRect(r, 5, 5)
            if n.auto_next != -1:
                label, col = "▶  далее → #%d" % n.auto_next, theme.EDGE_AUTO
            elif n.spoils:
                label, col = "■  конец — NPC больше не говорит", theme.TEXT_MISSING
            elif n.end:
                label, col = "■  конец диалога", theme.TEXT_MISSING
            else:
                label, col = "… тянуть отсюда: «далее»", theme.TEXT_DIM
            qp.setPen(col)
            qp.drawText(r.adjusted(10, 0, -10, 0), Qt.AlignVCenter | Qt.AlignLeft, label)
            self._paint_port(qp, 0, n.auto_next != -1, theme.EDGE_AUTO)

    def _paint_option(self, qp, i, o, fm):
        p = self.gs.project
        r = self.row_rect(i)
        qp.setPen(Qt.NoPen)
        qp.setBrush(theme.ROW_HOVER if self.hover_row == i else theme.ROW)
        if self.gs.selected_option == (self.nid, i):
            qp.setPen(QPen(theme.SELECT, 1.4))
        qp.drawRoundedRect(r, 5, 5)
        badges = option_badges(o, p)
        # badges from the right
        qp.setFont(FONT_SMALL)
        bfm = QFontMetricsF(FONT_SMALL)
        x = r.right() - 8
        for b in reversed(badges):
            t = bfm.elidedText(b.text, Qt.ElideRight, 86)
            bw = bfm.horizontalAdvance(t) + 10
            x -= bw
            br = QRectF(x, r.top() + 6, bw, r.height() - 12)
            qp.setPen(Qt.NoPen)
            qp.setBrush(b.color)
            qp.drawRoundedRect(br, 4, 4)
            qp.setPen(theme.TEXT)
            qp.drawText(br, Qt.AlignCenter, t)
            x -= 3
        text, fb = text_for(o.text, self.gs.lang, p.base_language)
        qp.setFont(FONT)
        qp.setPen(theme.TEXT_MISSING if (fb or not text) else theme.TEXT)
        avail = x - (r.left() + 10) - 4
        shown = fm.elidedText((str(i + 1) + ". ") + (text or "(нет текста)"), Qt.ElideRight, max(20, avail))
        qp.drawText(QRectF(r.left() + 10, r.top(), avail, r.height()), Qt.AlignVCenter | Qt.AlignLeft, shown)
        connected = o.next != -1 or (o.has_after() and o.after_next != -1)
        col = theme.EDGE_AFTER if (o.next == -1 and o.has_after()) else theme.EDGE
        self._paint_port(qp, i, connected, col)

    def _paint_port(self, qp, i, connected, color):
        c = self.port_pos(i)
        qp.setPen(QPen(color, 2))
        qp.setBrush(color if connected else theme.CARD)
        qp.drawEllipse(c, PORT_R, PORT_R)

    # --- interaction -----------------------------------------------------------------
    def hoverMoveEvent(self, e):
        r = self.row_at(e.pos())
        if r != self.hover_row:
            self.hover_row = r
            self.update()
        tip = ""
        n = self.node
        if r >= 0 and n.options and r < len(n.options):
            o = n.options[r]
            t, _ = text_for(o.text, self.gs.lang, self.gs.project.base_language)
            parts = [t]
            parts += [b.tip for b in option_badges(o, self.gs.project)]
            if o.note:
                parts.append("заметка: " + o.note)
            tip = "\n".join(x for x in parts if x)
        elif r == -1 or r == -2:
            if e.pos().y() < HEADER_H:
                tip = "\n".join("%s: %s" % (lg, STATUS_TIP[st]) for lg, st, _ in self.statuses)
            else:
                t, _ = text_for(n.line, self.gs.lang, self.gs.project.base_language)
                tip = t + (("\nзаметка: " + n.note) if n.note else "")
        self.setToolTip(tip)

    def hoverLeaveEvent(self, e):
        self.hover_row = -2
        self.update()

    def mousePressEvent(self, e):
        if e.button() == Qt.LeftButton:
            port = self.port_at(e.pos())
            if port is not None:
                src = ("option", self.nid, port) if self.node.options else ("auto", self.nid)
                self.gs.begin_connect(src, self.mapToScene(self.port_pos(port)))
                e.accept()
                return
            r = self.row_at(e.pos())
            if r >= 0 and self.node.options:
                self.gs.select_option(self.nid, r)
            else:
                self.gs.selected_option = None
        super().mousePressEvent(e)

    def mouseMoveEvent(self, e):
        if self.gs.connecting:
            self.gs.update_connect(e.scenePos())
            return
        super().mouseMoveEvent(e)

    def mouseReleaseEvent(self, e):
        if self.gs.connecting:
            self.gs.end_connect(e.scenePos())
            return
        moved = self.pos() != QPointF(*self.node.pos)
        super().mouseReleaseEvent(e)
        if moved:
            self.gs.items_moved()

    def mouseDoubleClickEvent(self, e):
        r = self.row_at(e.pos())
        if r >= 0 and self.node.options:
            self.gs.editRequested.emit(("option", self.nid, r))
        else:
            self.gs.editRequested.emit(("node", self.nid))

    def itemChange(self, change, value):
        if change == QGraphicsItem.ItemPositionHasChanged:
            for ed in self.edges:
                ed.update_path()
        elif change == QGraphicsItem.ItemSelectedHasChanged:
            for ed in self.edges:
                ed.update()
        return super().itemChange(change, value)


def QStyleOptionLOD(qp):
    t = qp.worldTransform()
    return math.sqrt(t.m11() * t.m11() + t.m12() * t.m12())


class EntryItem(QGraphicsObject):
    """The start pill or the returning-visit card."""

    def __init__(self, gscene, kind):
        super().__init__()
        self.gs = gscene
        self.kind = kind   # "start" | "returning"
        self.edges = []
        self.hover = False
        self.setFlags(QGraphicsItem.ItemIsMovable | QGraphicsItem.ItemIsSelectable |
                      QGraphicsItem.ItemSendsGeometryChanges)
        self.setAcceptHoverEvents(True)
        npc = gscene.npc
        pos = npc.start_pos if kind == "start" else npc.returning.pos
        self.setPos(pos[0], pos[1])
        if kind == "start":
            self.setToolTip("Первый разговор начинается здесь. Тяните от точки к реплике, чтобы сменить стартовую.")
        else:
            self.setToolTip("Повторный визит: что NPC говорит, когда с ним уже поговорили.\n"
                            "Двойной клик — редактировать. Тяните от точки к реплике, чьи варианты ответа показать.")

    def size(self):
        return (150.0, 34.0) if self.kind == "start" else (260.0, 112.0)

    def boundingRect(self):
        w, h = self.size()
        return QRectF(-2, -2, w + PORT_R * 2 + 4, h + 4)

    def port_pos(self):
        w, h = self.size()
        return QPointF(w, h / 2 if self.kind == "start" else h - 16)

    def paint(self, qp, opt, widget=None):
        qp.setRenderHint(QPainter.Antialiasing, True)
        w, h = self.size()
        sel = self.isSelected()
        if self.kind == "start":
            qp.setPen(QPen(theme.SELECT if sel else theme.EDGE_START, 2))
            qp.setBrush(QColor("#21402f"))
            qp.drawRoundedRect(QRectF(0, 0, w, h), h / 2, h / 2)
            qp.setPen(theme.TEXT)
            qp.setFont(FONT_B)
            qp.drawText(QRectF(0, 0, w - 10, h), Qt.AlignCenter, "▶  НАЧАЛО")
            qp.setPen(QPen(theme.EDGE_START, 2))
            qp.setBrush(theme.EDGE_START)
            qp.drawEllipse(self.port_pos(), PORT_R, PORT_R)
            return
        r = self.gs.npc.returning
        enabled = r.enabled()
        col = theme.EDGE_RETURN if enabled else theme.CARD_BORDER
        qp.setPen(QPen(theme.SELECT if sel else col, 2 if sel else 1.4, Qt.SolidLine if enabled else Qt.DashLine))
        qp.setBrush(QColor("#2f2838") if enabled else theme.CARD)
        qp.drawRoundedRect(QRectF(0, 0, w, h), 8, 8)
        qp.setFont(FONT_B)
        qp.setPen(theme.TEXT if enabled else theme.TEXT_DIM)
        qp.drawText(QRectF(10, 4, w - 20, 22), Qt.AlignVCenter | Qt.AlignLeft, "↺  ПОВТОРНЫЙ ВИЗИТ")
        # pills
        if enabled:
            x = w - 8
            qp.setFont(FONT_SMALL)
            for lg in reversed(self.gs.project.languages):
                st = voice_status(self.gs.project, r.line, r.voice, lg)
                x -= 26
                rr = QRectF(x, 7, 23, 16)
                qp.setPen(Qt.NoPen)
                qp.setBrush(STATUS_COLOR[st])
                qp.drawRoundedRect(rr, 4, 4)
                qp.setPen(theme.TEXT if r.line.get(lg) else theme.TEXT_MISSING)
                qp.drawText(rr, Qt.AlignCenter, short_lang(lg))
        qp.setFont(FONT)
        fm = QFontMetricsF(FONT)
        if enabled:
            t, fb = text_for(r.line, self.gs.lang, self.gs.project.base_language)
            qp.setPen(theme.TEXT_MISSING if fb else theme.TEXT)
            y = 30
            for ln in _elide_lines(fm, t, w - 20, 3):
                qp.drawText(QPointF(10, y + fm.ascent()), ln)
                y += fm.height() + 1
            npc = self.gs.npc
            if r.no_options:
                if r.next != -1:
                    flow = "без вариантов → #%d" % r.next
                elif r.auto_close:
                    flow = "без вариантов → закрыть"
                else:
                    sn = npc.node(npc.start_node)
                    flow = ("без вариантов → #%d" % sn.auto_next) if (sn and sn.auto_next != -1) else "без вариантов"
            else:
                flow = "варианты ответа реплики #%d" % npc.returning_target()
            qp.setPen(theme.EDGE_RETURN)
            qp.drawText(QRectF(10, h - 28, w - 26, 24), Qt.AlignVCenter | Qt.AlignLeft, flow)
            qp.setPen(QPen(theme.EDGE_RETURN, 2))
            qp.setBrush(theme.EDGE_RETURN)
            qp.drawEllipse(self.port_pos(), PORT_R, PORT_R)
        else:
            qp.setPen(theme.TEXT_DIM)
            qp.drawText(QRectF(10, 30, w - 20, h - 36), Qt.AlignLeft | Qt.TextWordWrap,
                        "Нет. На каждом визите NPC начинает с начала.\nДвойной клик — добавить повторную реплику.")

    def mousePressEvent(self, e):
        if e.button() == Qt.LeftButton:
            if (e.pos() - self.port_pos()).manhattanLength() <= PORT_R + 5 and (self.kind == "start" or self.gs.npc.returning.enabled()):
                self.gs.begin_connect((self.kind,), self.mapToScene(self.port_pos()))
                e.accept()
                return
        super().mousePressEvent(e)

    def mouseMoveEvent(self, e):
        if self.gs.connecting:
            self.gs.update_connect(e.scenePos())
            return
        super().mouseMoveEvent(e)

    def mouseReleaseEvent(self, e):
        if self.gs.connecting:
            self.gs.end_connect(e.scenePos())
            return
        super().mouseReleaseEvent(e)
        self.gs.items_moved()

    def mouseDoubleClickEvent(self, e):
        self.gs.editRequested.emit((self.kind,))

    def itemChange(self, change, value):
        if change == QGraphicsItem.ItemPositionHasChanged:
            for ed in self.edges:
                ed.update_path()
        return super().itemChange(change, value)


class EdgeItem(QGraphicsPathItem):
    def __init__(self, src_item, src_row, dst_item, kind, ref):
        super().__init__()
        self.src = src_item
        self.row = src_row
        self.dst = dst_item
        self.kind = kind     # next | auto | after | returning | start
        self.ref = ref       # what to unlink: ("option", nid, oi) / ("auto", nid) / ("after", nid, oi) / ("returning",) / ("start",)
        self.setZValue(-1)
        self.setAcceptHoverEvents(True)
        self.setFlag(QGraphicsItem.ItemIsSelectable, True)
        self.hover = False
        src_item.edges.append(self)
        dst_item.edges.append(self)
        self.update_path()

    def color(self):
        return {"next": theme.EDGE, "auto": theme.EDGE_AUTO, "after": theme.EDGE_AFTER,
                "returning": theme.EDGE_RETURN, "start": theme.EDGE_START}[self.kind]

    def endpoints(self):
        if isinstance(self.src, NodeItem):
            a = self.src.mapToScene(self.src.port_pos(self.row))
        else:
            a = self.src.mapToScene(self.src.port_pos())
        b = self.dst.mapToScene(self.dst.input_pos())
        return a, b

    def update_path(self):
        a, b = self.endpoints()
        self.setPath(curve(a, b))

    def shape(self):
        from PySide6.QtGui import QPainterPathStroker
        s = QPainterPathStroker()
        s.setWidth(10)
        return s.createStroke(self.path())

    def paint(self, qp, opt, widget=None):
        qp.setRenderHint(QPainter.Antialiasing, True)
        hot = self.hover or self.isSelected() or self.src.isSelected() or self.dst.isSelected()
        col = QColor(self.color())
        if not hot:
            col.setAlpha(190)
        pen = QPen(theme.SELECT if self.isSelected() else col, 3.0 if hot else 1.8)
        if self.kind in ("auto", "after"):
            pen.setStyle(Qt.DashLine)
        qp.setPen(pen)
        qp.setBrush(Qt.NoBrush)
        qp.drawPath(self.path())
        # arrow
        path = self.path()
        end = path.pointAtPercent(1.0)
        before = path.pointAtPercent(0.97)
        ang = math.atan2(end.y() - before.y(), end.x() - before.x())
        s = 9.0
        p1 = QPointF(end.x() - s * math.cos(ang - 0.45), end.y() - s * math.sin(ang - 0.45))
        p2 = QPointF(end.x() - s * math.cos(ang + 0.45), end.y() - s * math.sin(ang + 0.45))
        qp.setPen(Qt.NoPen)
        qp.setBrush(pen.color())
        qp.drawPolygon(QPolygonF([end, p1, p2]))

    def hoverEnterEvent(self, e):
        self.hover = True
        self.update()

    def hoverLeaveEvent(self, e):
        self.hover = False
        self.update()


def curve(a, b):
    path = QPainterPath(a)
    dx = b.x() - a.x()
    if dx >= 40:
        k = max(50.0, dx * 0.5)
        path.cubicTo(QPointF(a.x() + k, a.y()), QPointF(b.x() - k, b.y()), b)
        return path
    # link to a node left of the port (a stacked monologue, a loop back):
    # out to the right, through the gap above the target header, in from the left
    x1 = a.x() + 22.0
    x2 = b.x() - 22.0
    ym = b.y() - 27.0
    pts = [a, QPointF(x1, a.y()), QPointF(x1, ym), QPointF(x2, ym), QPointF(x2, b.y()), b]
    r = 9.0
    for i in range(1, len(pts) - 1):
        p0, p1, p2 = pts[i - 1], pts[i], pts[i + 1]
        d1 = QPointF(p1.x() - p0.x(), p1.y() - p0.y())
        d2 = QPointF(p2.x() - p1.x(), p2.y() - p1.y())
        l1 = max(1e-3, math.hypot(d1.x(), d1.y()))
        l2 = max(1e-3, math.hypot(d2.x(), d2.y()))
        rr = min(r, l1 / 2, l2 / 2)
        path.lineTo(QPointF(p1.x() - d1.x() / l1 * rr, p1.y() - d1.y() / l1 * rr))
        path.quadTo(p1, QPointF(p1.x() + d2.x() / l2 * rr, p1.y() + d2.y() / l2 * rr))
    path.lineTo(b)
    return path


class GraphScene(QGraphicsScene):
    selectedItem = Signal(object)
    itemsMoved = Signal()
    connectRequested = Signal(object, int, bool)       # src, target node id, ask_after
    createRequested = Signal(object, QPointF)           # src (or None), scene pos
    contextRequested = Signal(object, object, QPointF)  # target, screen pos (QPoint), scene pos
    editRequested = Signal(object)
    deleteRequested = Signal()

    def __init__(self):
        super().__init__()
        self.project = None
        self.npc = None
        self.lang = "english"
        self.node_items = {}
        self.entries = {}
        self.edges = []
        self.connecting = None
        self.temp_edge = None
        self.selected_option = None
        self.setSceneRect(QRectF(-20000, -20000, 40000, 40000))
        self.selectionChanged.connect(self._on_selection)
        self._suppress_sel = False

    # --- building ----------------------------------------------------------------------
    def build(self, project, npc, lang):
        self._suppress_sel = True
        self.clear()
        self.node_items = {}
        self.entries = {}
        self.edges = []
        self.temp_edge = None
        self.connecting = None
        self.project = project
        self.npc = npc
        self.lang = lang
        if npc is None:
            self._suppress_sel = False
            return
        for n in npc.nodes:
            it = NodeItem(self, n)
            self.addItem(it)
            self.node_items[n.id] = it
        for kind in ("start", "returning"):
            e = EntryItem(self, kind)
            self.addItem(e)
            self.entries[kind] = e
        self.rebuild_edges()
        self._suppress_sel = False

    def rebuild_edges(self):
        for e in self.edges:
            if e.src and self in (e.scene(),):
                pass
            self.removeItem(e)
        for it in list(self.node_items.values()) + list(self.entries.values()):
            it.edges = []
        self.edges = []
        npc = self.npc
        ni = self.node_items
        for n in npc.nodes:
            src = ni[n.id]
            if n.options:
                for i, o in enumerate(n.options):
                    if o.next != -1 and o.next in ni:
                        self._edge(src, i, ni[o.next], "next", ("option", n.id, i))
                    elif o.has_after() and o.after_next in ni:
                        self._edge(src, i, ni[o.after_next], "after", ("after", n.id, i))
            elif n.auto_next != -1 and n.auto_next in ni:
                self._edge(src, 0, ni[n.auto_next], "auto", ("auto", n.id))
        if npc.start_node in ni:
            self._edge(self.entries["start"], 0, ni[npc.start_node], "start", ("start",))
        r = npc.returning
        if r.enabled():
            tgt = r.next if r.no_options and r.next != -1 else (npc.returning_target() if not r.no_options else None)
            if tgt is not None and tgt in ni:
                self._edge(self.entries["returning"], 0, ni[tgt], "returning", ("returning",))

    def _edge(self, src, row, dst, kind, ref):
        e = EdgeItem(src, row, dst, kind, ref)
        self.addItem(e)
        self.edges.append(e)
        return e

    def refresh_all(self):
        """Bring the cards in line with the model: new nodes get cards, removed ones lose
        them, renumbered ones keep theirs (the card may be the one under the mouse right now,
        e.g. a link dragged from its port - it must not be destroyed)."""
        if self.npc is None:
            return
        self._suppress_sel = True
        by_obj = {id(it.node): it for it in self.node_items.values()}
        items = {}
        for n in self.npc.nodes:
            it = by_obj.pop(id(n), None)
            if it is None:
                it = NodeItem(self, n)
                self.addItem(it)
            else:
                it.nid = n.id
                if it.pos() != QPointF(n.pos[0], n.pos[1]):
                    it.setPos(n.pos[0], n.pos[1])
                it.refresh()
            items[n.id] = it
        for it in by_obj.values():
            if self.selected_option and self.selected_option[0] == it.nid:
                self.selected_option = None
            self.removeItem(it)
        self.node_items = items
        for k, e in self.entries.items():
            pos = self.npc.start_pos if k == "start" else self.npc.returning.pos
            if e.pos() != QPointF(pos[0], pos[1]):
                e.setPos(pos[0], pos[1])
            e.update()
        self.rebuild_edges()
        self._suppress_sel = False

    def set_lang(self, lang):
        self.lang = lang
        for it in self.node_items.values():
            it.update()
        for e in self.entries.values():
            e.update()

    # --- selection -----------------------------------------------------------------------
    def select_option(self, nid, oi):
        self.selected_option = (nid, oi)
        for it in self.node_items.values():
            it.update()
        self.selectedItem.emit(("option", nid, oi))

    def _on_selection(self):
        if self._suppress_sel:
            return
        try:
            sel = self.selectedItems()
        except RuntimeError:
            return
        if self.selected_option and not any(isinstance(s, NodeItem) and s.nid == self.selected_option[0] for s in sel):
            self.selected_option = None
        if len(sel) == 1:
            s = sel[0]
            if isinstance(s, NodeItem):
                if self.selected_option and self.selected_option[0] == s.nid:
                    return
                self.selectedItem.emit(("node", s.nid))
            elif isinstance(s, EntryItem):
                self.selectedItem.emit((s.kind,))
            elif isinstance(s, EdgeItem):
                self.selectedItem.emit(("edge",) + tuple(s.ref))
        elif not sel:
            self.selectedItem.emit(("npc",))
        else:
            self.selectedItem.emit(("multi", len(sel)))

    def select_target(self, target, center_view=None):
        """Select the item of ("node", id) / ("option", id, i) / ("returning",) / ("start",)."""
        self._suppress_sel = True
        self.clearSelection()
        self.selected_option = None
        item = None
        if target[0] in ("node", "option") and target[1] in self.node_items:
            item = self.node_items[target[1]]
            item.setSelected(True)
            if target[0] == "option":
                self.selected_option = (target[1], target[2])
        elif target[0] in self.entries:
            item = self.entries[target[0]]
            item.setSelected(True)
        for it in self.node_items.values():
            it.update()
        self._suppress_sel = False
        if item is not None and center_view is not None:
            center_view.centerOn(item.sceneBoundingRect().center())
        return item

    def selected_node_ids(self):
        return [s.nid for s in self.selectedItems() if isinstance(s, NodeItem)]

    # --- moving --------------------------------------------------------------------------
    def items_moved(self):
        changed = False
        for it in self.node_items.values():
            p = [it.pos().x(), it.pos().y()]
            if p != it.node.pos:
                it.node.pos = p
                changed = True
        for k, e in self.entries.items():
            p = [e.pos().x(), e.pos().y()]
            tgt = self.npc.start_pos if k == "start" else self.npc.returning.pos
            if p != tgt:
                tgt[0], tgt[1] = p
                changed = True
        if changed:
            self.itemsMoved.emit()

    # --- connecting ----------------------------------------------------------------------
    def begin_connect(self, src, scene_pos):
        self.connecting = (src, scene_pos)
        self.temp_edge = QGraphicsPathItem()
        pen = QPen(theme.SELECT, 2, Qt.DashLine)
        self.temp_edge.setPen(pen)
        self.temp_edge.setZValue(10)
        self.addItem(self.temp_edge)
        self.temp_edge.setPath(curve(scene_pos, scene_pos))

    def update_connect(self, pos):
        if not self.connecting:
            return
        self.temp_edge.setPath(curve(self.connecting[1], pos))

    def end_connect(self, pos):
        src, _ = self.connecting
        self.connecting = None
        if self.temp_edge is not None:
            self.removeItem(self.temp_edge)
            self.temp_edge = None
        target = None
        for it in self.items(pos):
            if isinstance(it, NodeItem):
                target = it
                break
        if target is not None:
            ask_after = False
            if src[0] == "option":
                o = self.npc.node(src[1]).options[src[2]]
                ask_after = bool(o.complete)
            self.connectRequested.emit(src, target.nid, ask_after)
        else:
            self.createRequested.emit(src, pos)

    def contextMenuEvent(self, e):
        target = None
        for it in self.items(e.scenePos()):
            if isinstance(it, NodeItem):
                lp = it.mapFromScene(e.scenePos())
                r = it.row_at(lp)
                if r >= 0 and it.node.options:
                    target = ("option", it.nid, r)
                else:
                    target = ("node", it.nid)
                break
            if isinstance(it, EntryItem):
                target = (it.kind,)
                break
            if isinstance(it, EdgeItem):
                target = ("edge",) + tuple(it.ref)
                break
        self.contextRequested.emit(target, e.screenPos(), e.scenePos())
        e.accept()

    def mouseDoubleClickEvent(self, e):
        if not self.items(e.scenePos()):
            self.createRequested.emit(None, e.scenePos())
            return
        super().mouseDoubleClickEvent(e)

    def highlight(self, ids, attr="search_hit"):
        for nid, it in self.node_items.items():
            v = nid in ids
            if getattr(it, attr) != v:
                setattr(it, attr, v)
                it.update()


class GraphView(QGraphicsView):
    zoomChanged = Signal(float)

    def __init__(self, scene):
        super().__init__(scene)
        self.setRenderHints(QPainter.Antialiasing | QPainter.TextAntialiasing | QPainter.SmoothPixmapTransform)
        self.setViewportUpdateMode(QGraphicsView.SmartViewportUpdate)
        self.setTransformationAnchor(QGraphicsView.NoAnchor)
        self.setResizeAnchor(QGraphicsView.AnchorViewCenter)
        self.setDragMode(QGraphicsView.RubberBandDrag)
        self.setHorizontalScrollBarPolicy(Qt.ScrollBarAlwaysOff)
        self.setVerticalScrollBarPolicy(Qt.ScrollBarAlwaysOff)
        self.setFocusPolicy(Qt.StrongFocus)
        self._pan = None
        self._space = False
        self.setAcceptDrops(True)

    def zoom(self):
        return self.transform().m11()

    def set_zoom(self, z, anchor=None):
        z = max(0.12, min(2.5, z))
        if anchor is None:
            anchor = self.viewport().rect().center()
        before = self.mapToScene(anchor)
        self.setTransform(QTransform.fromScale(z, z))
        after = self.mapToScene(anchor)
        d = after - before
        self.translate(d.x(), d.y())
        self.zoomChanged.emit(z)

    def wheelEvent(self, e):
        steps = e.angleDelta().y() / 120.0
        if e.modifiers() & Qt.ShiftModifier:
            self.translate(e.angleDelta().y() / self.zoom() * 0.5, 0)
            return
        self.set_zoom(self.zoom() * (1.15 ** steps), e.position().toPoint())

    def fit_all(self):
        sc = self.scene()
        items = [i for i in sc.items() if isinstance(i, (NodeItem, EntryItem))]
        if not items:
            self.resetTransform()
            self.centerOn(0, 0)
            self.zoomChanged.emit(1.0)
            return
        r = items[0].sceneBoundingRect()
        for i in items[1:]:
            r = r.united(i.sceneBoundingRect())
        r.adjust(-60, -60, 60, 60)
        vw, vh = self.viewport().width(), self.viewport().height()
        z = min(vw / max(1, r.width()), vh / max(1, r.height()), 1.2)
        self.setTransform(QTransform.fromScale(max(0.12, z), max(0.12, z)))
        self.centerOn(r.center())
        self.zoomChanged.emit(self.zoom())

    def smart_fit(self):
        """Fit everything; a big graph instead opens readable at its start."""
        self.fit_all()
        if self.zoom() < 0.5:
            sc = self.scene()
            entry = getattr(sc, "entries", {}).get("start")
            z = 0.62
            self.setTransform(QTransform.fromScale(z, z))
            if entry is not None:
                tl = entry.sceneBoundingRect().topLeft()
                vw, vh = self.viewport().width(), self.viewport().height()
                self.centerOn(tl.x() - 40 + vw / (2 * z), tl.y() - 60 + vh / (2 * z))
            self.zoomChanged.emit(self.zoom())

    def view_state(self):
        c = self.mapToScene(self.viewport().rect().center())
        return {"zoom": round(self.zoom(), 4), "cx": round(c.x(), 1), "cy": round(c.y(), 1)}

    def set_view_state(self, st):
        self.setTransform(QTransform.fromScale(st.get("zoom", 1.0), st.get("zoom", 1.0)))
        self.centerOn(st.get("cx", 0), st.get("cy", 0))
        self.zoomChanged.emit(self.zoom())

    def mousePressEvent(self, e):
        if e.button() in (Qt.MiddleButton,) or (e.button() == Qt.LeftButton and self._space):
            self._pan = e.position()
            self.viewport().setCursor(Qt.ClosedHandCursor)
            e.accept()
            return
        if e.button() == Qt.RightButton:
            self._pan_r = e.position()
            self._pan_r_moved = False
        super().mousePressEvent(e)

    def mouseMoveEvent(self, e):
        if self._pan is not None:
            d = e.position() - self._pan
            self._pan = e.position()
            z = self.zoom()
            self.translate(d.x() / z, d.y() / z)
            return
        if e.buttons() & Qt.RightButton and getattr(self, "_pan_r", None) is not None:
            d = e.position() - self._pan_r
            if d.manhattanLength() > 3 or self._pan_r_moved:
                self._pan_r_moved = True
                self._pan_r = e.position()
                z = self.zoom()
                self.translate(d.x() / z, d.y() / z)
                self.viewport().setCursor(Qt.ClosedHandCursor)
                return
        super().mouseMoveEvent(e)

    def mouseReleaseEvent(self, e):
        if self._pan is not None and e.button() in (Qt.MiddleButton, Qt.LeftButton):
            self._pan = None
            self.viewport().setCursor(Qt.OpenHandCursor if self._space else Qt.ArrowCursor)
            return
        if e.button() == Qt.RightButton:
            moved = getattr(self, "_pan_r_moved", False)
            self._pan_r = None
            self.viewport().setCursor(Qt.ArrowCursor)
            if moved:
                self._pan_r_moved = False
                e.accept()
                return
        super().mouseReleaseEvent(e)

    def contextMenuEvent(self, e):
        if getattr(self, "_pan_r_moved", False):
            self._pan_r_moved = False
            return
        super().contextMenuEvent(e)

    def keyPressEvent(self, e):
        if e.key() == Qt.Key_Space and not e.isAutoRepeat():
            self._space = True
            self.viewport().setCursor(Qt.OpenHandCursor)
            return
        if e.key() in (Qt.Key_Delete,):
            self.scene().deleteRequested.emit()
            return
        super().keyPressEvent(e)

    def keyReleaseEvent(self, e):
        if e.key() == Qt.Key_Space and not e.isAutoRepeat():
            self._space = False
            self.viewport().setCursor(Qt.ArrowCursor)
            return
        super().keyReleaseEvent(e)

    def drawBackground(self, qp, rect):
        qp.fillRect(rect, theme.BG)
        z = self.zoom()
        step = 40.0
        while step * z < 14:
            step *= 4
        left = math.floor(rect.left() / step) * step
        top = math.floor(rect.top() / step) * step
        qp.setPen(QPen(theme.GRID, 0))
        x = left
        lines = []
        from PySide6.QtCore import QLineF
        while x < rect.right():
            lines.append(QLineF(x, rect.top(), x, rect.bottom()))
            x += step
        y = top
        while y < rect.bottom():
            lines.append(QLineF(rect.left(), y, rect.right(), y))
            y += step
        qp.drawLines(lines)
        major = step * 5
        qp.setPen(QPen(theme.GRID_MAJOR, 0))
        lines = []
        x = math.floor(rect.left() / major) * major
        while x < rect.right():
            lines.append(QLineF(x, rect.top(), x, rect.bottom()))
            x += major
        y = math.floor(rect.top() / major) * major
        while y < rect.bottom():
            lines.append(QLineF(rect.left(), y, rect.right(), y))
            y += major
        qp.drawLines(lines)

    # drop audio files onto a node: attach as voice of the current language
    def dragEnterEvent(self, e):
        if e.mimeData().hasUrls():
            e.acceptProposedAction()
        else:
            super().dragEnterEvent(e)

    def dragMoveEvent(self, e):
        if e.mimeData().hasUrls():
            e.acceptProposedAction()
        else:
            super().dragMoveEvent(e)

    def dropEvent(self, e):
        if not e.mimeData().hasUrls():
            return super().dropEvent(e)
        files = [u.toLocalFile() for u in e.mimeData().urls() if u.isLocalFile()]
        pos = self.mapToScene(e.position().toPoint())
        target = None
        for it in self.scene().items(pos):
            if isinstance(it, NodeItem):
                target = ("node", it.nid)
                break
            if isinstance(it, EntryItem) and it.kind == "returning":
                target = ("returning",)
                break
        self.filesDropped(files, target)
        e.acceptProposedAction()

    def filesDropped(self, files, target):
        pass   # replaced by the main window
