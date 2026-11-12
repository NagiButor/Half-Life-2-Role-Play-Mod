"""SMD files of clips (skeleton animation) for studiomdl."""
import numpy as np

from . import qmath as Q


def write_clip(path, clip):
    rig = clip.rig
    out = ["version 1", "nodes"]
    for i, n in enumerate(rig.names):
        out.append('%d "%s" %d' % (i, n, rig.parents[i]))
    out += ["end", "skeleton"]
    eul = Q.quat_to_euler(clip.quat)
    for t in range(clip.frames):
        out.append("time %d" % t)
        P = clip.pos[t]
        E = eul[t]
        for i in range(len(rig.names)):
            out.append("%d %.5f %.5f %.5f %.6f %.6f %.6f" % (i, P[i, 0], P[i, 1], P[i, 2], E[i, 0], E[i, 1], E[i, 2]))
    out.append("end")
    with open(path, "w", encoding="ascii", newline="\n") as f:
        f.write("\n".join(out) + "\n")


def write_reference(path, rig):
    """the reference pose with a tiny triangle on every bone: studiomdl drops bones without
    vertices, and an animation library must keep the whole skeleton"""
    out = ["version 1", "nodes"]
    for i, n in enumerate(rig.names):
        out.append('%d "%s" %d' % (i, n, rig.parents[i]))
    out += ["end", "skeleton", "time 0"]
    eul = Q.quat_to_euler(rig.rest_quat)
    for i in range(len(rig.names)):
        p = rig.rest_pos[i]
        out.append("%d %.5f %.5f %.5f %.6f %.6f %.6f" % (i, p[0], p[1], p[2], eul[i, 0], eul[i, 1], eul[i, 2]))
    out += ["end", "triangles"]
    wp, _wq = rig.fk(rig.rest_pos[None], rig.rest_quat[None])
    for i in range(len(rig.names)):
        out.append("dev/dev_blendmeasure")
        c = wp[0, i]
        for v in ((0, 0, 0), (0.05, 0, 0), (0, 0.05, 0)):
            out.append("%d %.4f %.4f %.4f 0 0 1 0 0 1 %d 1.0" % (i, c[0] + v[0], c[1] + v[1], c[2] + v[2], i))
    out.append("end")
    with open(path, "w", encoding="ascii", newline="\n") as f:
        f.write("\n".join(out) + "\n")
