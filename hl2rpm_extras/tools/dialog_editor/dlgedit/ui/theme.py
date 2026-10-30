"""Colors and the dark Fusion theme of the editor."""
from PySide6.QtGui import QColor, QPalette
from PySide6.QtWidgets import QApplication

BG = QColor("#1b1e23")
GRID = QColor("#262a31")
GRID_MAJOR = QColor("#2e333b")
CARD = QColor("#2a2f37")
CARD_BORDER = QColor("#3c434e")
HEADER = QColor("#34506e")
HEADER_START = QColor("#2f6b4a")
HEADER_END = QColor("#6b3a3a")
TEXT = QColor("#e6e9ee")
TEXT_DIM = QColor("#9aa4b1")
TEXT_MISSING = QColor("#e07b6a")
ROW = QColor("#30363f")
ROW_HOVER = QColor("#3a424d")
SELECT = QColor("#ffb84d")
SEARCH = QColor("#57d1ff")

EDGE = QColor("#8fa3b8")
EDGE_AUTO = QColor("#6fb1ff")
EDGE_AFTER = QColor("#e0b040")
EDGE_RETURN = QColor("#c38fff")
EDGE_START = QColor("#5fd38d")

BADGE_GRANT = QColor("#3f9b5f")
BADGE_COMPLETE = QColor("#b58a1e")
BADGE_FAIL = QColor("#a84242")
BADGE_STAGE = QColor("#3d78a8")
BADGE_COND = QColor("#5c6470")
BADGE_CLOSE = QColor("#7a4545")
BADGE_SPOIL = QColor("#6d3f7d")

VOICE_OK = QColor("#4caf6e")
VOICE_STALE = QColor("#d6a23a")
VOICE_NONE = QColor("#4a515c")
VOICE_ERR = QColor("#c9503f")


def apply(app: QApplication):
    app.setStyle("Fusion")
    pal = QPalette()
    pal.setColor(QPalette.Window, QColor("#23272e"))
    pal.setColor(QPalette.WindowText, TEXT)
    pal.setColor(QPalette.Base, QColor("#1c1f24"))
    pal.setColor(QPalette.AlternateBase, QColor("#262a31"))
    pal.setColor(QPalette.ToolTipBase, QColor("#2d323a"))
    pal.setColor(QPalette.ToolTipText, TEXT)
    pal.setColor(QPalette.Text, TEXT)
    pal.setColor(QPalette.Button, QColor("#2d323a"))
    pal.setColor(QPalette.ButtonText, TEXT)
    pal.setColor(QPalette.BrightText, QColor("#ff6b6b"))
    pal.setColor(QPalette.Highlight, QColor("#3d6fa8"))
    pal.setColor(QPalette.HighlightedText, QColor("#ffffff"))
    pal.setColor(QPalette.Link, QColor("#7fb6ff"))
    pal.setColor(QPalette.PlaceholderText, QColor("#6f7884"))
    pal.setColor(QPalette.Disabled, QPalette.Text, QColor("#6a717c"))
    pal.setColor(QPalette.Disabled, QPalette.ButtonText, QColor("#6a717c"))
    pal.setColor(QPalette.Disabled, QPalette.WindowText, QColor("#6a717c"))
    app.setPalette(pal)
    app.setStyleSheet("""
        QToolTip { border: 1px solid #4a5260; padding: 4px; }
        QGroupBox { border: 1px solid #3a414b; border-radius: 6px; margin-top: 14px; padding-top: 6px; }
        QGroupBox::title { subcontrol-origin: margin; left: 8px; padding: 0 4px; color: #aeb8c4; }
        QTabBar::tab { padding: 6px 12px; }
        QToolBar { spacing: 4px; padding: 3px; border: none; }
        QToolButton { padding: 4px 6px; }
        QLabel#hint { color: #8d97a4; }
        QLabel#title { font-size: 15px; font-weight: 600; }
        QFrame#langbox { border: 1px solid #39404a; border-radius: 6px; }
        QListWidget::item { padding: 3px; }
    """)
