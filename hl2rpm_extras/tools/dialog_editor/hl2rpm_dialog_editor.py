"""HL2RPM Dialog Editor - visual editor of NPC dialogs, quests and voice-over.

    hl2rpm_dialog_editor.py [project.hl2dlg]

Screenshot mode (for reports/docs): --shot out.png [--npc name] [--select node:105 |
option:105:0 | returning | quest:<id>] [--lang russian] [--size 1600x900]
[--zoom 0.7 | --fit] [--center node:105] [--sim] [--delay 800]
"""
import argparse
import os
import sys

if getattr(sys, "frozen", False):
    sys.path.insert(0, os.path.dirname(sys.executable))
else:
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from PySide6.QtCore import QTimer, Qt
from PySide6.QtWidgets import QApplication

from dlgedit.ui import theme
from dlgedit.ui.main_window import MainWindow


def parse_target(s):
    if not s:
        return None
    parts = s.split(":")
    if parts[0] == "node":
        return ("node", int(parts[1]))
    if parts[0] == "option":
        return ("option", int(parts[1]), int(parts[2]))
    if parts[0] == "quest":
        return ("quest", parts[1])
    return (parts[0],)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("project", nargs="?")
    ap.add_argument("--shot")
    ap.add_argument("--npc")
    ap.add_argument("--select")
    ap.add_argument("--center")
    ap.add_argument("--lang")
    ap.add_argument("--size", default="")
    ap.add_argument("--zoom", type=float)
    ap.add_argument("--fit", action="store_true")
    ap.add_argument("--sim", action="store_true")
    ap.add_argument("--sim-steps", default="")
    ap.add_argument("--delay", type=int, default=900)
    args = ap.parse_args()

    QApplication.setHighDpiScaleFactorRoundingPolicy(Qt.HighDpiScaleFactorRoundingPolicy.PassThrough)
    app = QApplication(sys.argv)
    app.setApplicationName("HL2RPM Dialog Editor")
    theme.apply(app)
    win = MainWindow()
    if args.size:
        w, h = (int(x) for x in args.size.lower().split("x"))
        win.resize(w, h)
    win.show()

    path = args.project
    if not path and not args.shot:
        last = win.qs.value("last_project", "")
        if last and os.path.exists(last):
            path = last
    if path:
        win.open_project(os.path.abspath(path))

    if args.lang and win.project:
        i = win.lang_combo.findData(args.lang)
        if i >= 0:
            win.lang_combo.setCurrentIndex(i)
    if args.npc and win.project and win.project.npc(args.npc):
        win.select_npc(win.project.npc(args.npc))

    def setup_view():
        if args.fit:
            win.view.fit_all()
        if args.zoom:
            win.view.set_zoom(args.zoom)
        sel = parse_target(args.select)
        if sel:
            if sel[0] == "quest":
                win.left_tabs.setCurrentIndex(1)
                win.inspector.show_target(sel)
            else:
                win.navigate(sel)
        c = parse_target(args.center)
        if c and c[0] == "node" and c[1] in win.scene.node_items:
            win.view.centerOn(win.scene.node_items[c[1]])
        if args.sim:
            win.simulate()
            for step in [s for s in args.sim_steps.split(",") if s]:
                if step == "c":
                    win.sim.show_screen(win.sim.sim.cont())
                elif step == "r":
                    win.sim.spoken.setChecked(True)
                    win.sim.start()
                else:
                    win.sim.choose(int(step))
            win.sim.sound.setChecked(False)

    if args.shot:
        QTimer.singleShot(150, setup_view)

        def shot():
            from PySide6.QtGui import QGuiApplication
            target = win.sim if (args.sim and win.sim) else win
            img = target.grab()
            img.save(args.shot)
            if args.sim and win.sim:
                win.grab().save(os.path.splitext(args.shot)[0] + "_main.png")
            win.dirty = False
            app.quit()
        QTimer.singleShot(args.delay, shot)
    else:
        QTimer.singleShot(0, setup_view)
    sys.exit(app.exec())


if __name__ == "__main__":
    main()
