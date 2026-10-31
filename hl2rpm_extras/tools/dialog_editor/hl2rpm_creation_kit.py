"""HL2RPM Creation Kit - the mod's content editor in the spirit of GECK: maps in 3D with
NPCs, markers, items, containers and patrol paths placed on them; NPCs with their
daily routine (AI packages), factions, dialogs, quests and voice-over.

    hl2rpm_creation_kit.py [project.hl2dlg]

Screenshot mode (for reports/docs): --shot out.png [--npc name] [--select node:105 |
option:105:0 | returning | quest:<id> | ref:<id> | hammer:<index> | faction:<id>]
[--lang russian] [--size 1600x900] [--zoom 0.7 | --fit] [--center node:105] [--sim]
[--tab map|dialog] [--map name] [--cam x,y,z,pitch,yaw] [--hour 23.5] [--npc-tab 0|1|2] [--left N]
[--delay 800]
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
    if parts[0] in ("quest", "faction"):
        return (parts[0], parts[1])
    if parts[0] in ("ref", "hammer", "nav"):
        return (parts[0], int(parts[1]))
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
    ap.add_argument("--tab", default="")
    ap.add_argument("--map", default="")
    ap.add_argument("--cam", default="")
    ap.add_argument("--hour", type=float)
    ap.add_argument("--npc-tab", type=int)
    ap.add_argument("--left", type=int)
    ap.add_argument("--delay", type=int, default=900)
    args = ap.parse_args()

    QApplication.setHighDpiScaleFactorRoundingPolicy(Qt.HighDpiScaleFactorRoundingPolicy.PassThrough)
    app = QApplication(sys.argv)
    app.setApplicationName("HL2RPM Creation Kit")
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
        if getattr(win, "_loading_map", False):
            QTimer.singleShot(200, setup_view)     # the last map of the project is still loading
            return
        if args.map and win.project:
            win.open_map(args.map)
        if args.hour is not None:
            win.set_preview_hour(args.hour)
        if args.cam:
            c = [float(x) for x in args.cam.split(",")]
            win.worldview.cam.pos[:] = c[:3]
            win.worldview.cam.pitch, win.worldview.cam.yaw = c[3], c[4]
            win.worldview.update()
        if args.tab == "map":
            win.central_tabs.setCurrentWidget(win.map_panel)
        elif args.tab == "dialog":
            win.central_tabs.setCurrentWidget(win.view)
        if args.fit:
            win.view.fit_all()
        if args.zoom:
            win.view.set_zoom(args.zoom)
        if args.left is not None:
            win.left_tabs.setCurrentIndex(args.left)
        if args.npc_tab is not None:
            win.inspector.npc_tab = args.npc_tab
            win.inspector.show_target(("npc",))
        sel = parse_target(args.select)
        if sel:
            if sel[0] == "quest":
                win.left_tabs.setCurrentWidget(win.quest_tab)
                win.inspector.show_target(sel)
            elif sel[0] in ("ref", "hammer", "nav"):
                win.worldview.select(sel)
            elif sel[0] == "faction":
                win.inspector.show_target(sel)
            else:
                win.navigate(sel)
        c = parse_target(args.center)
        if c and c[0] == "node" and c[1] in win.gscene.node_items:
            win.view.centerOn(win.gscene.node_items[c[1]])
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
            target = win.sim if (args.sim and win.sim) else win
            # the screen, not grab(): grab() leaves out what the render window paints over its 3D
            g = target.frameGeometry()
            img = QApplication.primaryScreen().grabWindow(0, g.x(), g.y(), g.width(), g.height())
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
