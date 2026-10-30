"""Write the editor icon (the one the window draws) as a multi-size .ico."""
import os
import sys

os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from PySide6.QtGui import QGuiApplication


def main(out):
    app = QGuiApplication(sys.argv)
    from dlgedit.ui.main_window import app_icon
    icon = app_icon()
    pm = icon.pixmap(64, 64)
    png = out + ".png"
    pm.save(png)
    try:
        from PIL import Image
        Image.open(png).save(out, sizes=[(16, 16), (24, 24), (32, 32), (48, 48), (64, 64)])
    except ImportError:
        pm.save(out)   # Qt writes a single-size .ico
    os.remove(png)


if __name__ == "__main__":
    main(sys.argv[1])
