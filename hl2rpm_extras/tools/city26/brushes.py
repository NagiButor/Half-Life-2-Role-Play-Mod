"""Small convex-prism VMF writer. Polygon coordinates are Source units, CCW."""
import math

from vmf import Node

NODRAW = "TOOLS/TOOLSNODRAW"


def prism(m, polygon, bottom, top, wall, roof=None, owner=None, lightmap=64):
    pts = [(round(float(x), 3), round(float(y), 3)) for x, y in polygon]
    area = sum(a[0] * b[1] - b[0] * a[1] for a, b in zip(pts, pts[1:] + pts[:1]))
    if area < 0:
        pts.reverse()
    top_at = top if callable(top) else lambda x, y: top
    if len(pts) < 3 or abs(area) < 1 or min(top_at(x, y) for x, y in pts) - bottom < 0.5:
        raise ValueError(f"degenerate prism: pts={pts}, area={area}, bottom={bottom}, tops={[top_at(x,y) for x,y in pts]}")
    s = Node("solid", [["id", str(m.new_id())]])

    def face(points, material, u, v):
        side = Node("side", [["id", str(m.new_side_id())],
            ["plane", " ".join("(%g %g %g)" % tuple(p) for p in points)],
            ["material", material], ["uaxis", u], ["vaxis", v],
            ["rotation", "0"], ["lightmapscale", str(lightmap)], ["smoothing_groups", "0"]])
        s.add_child(side)

    face([(x, y, bottom) for x, y in pts[:3]], NODRAW,
         "[1 0 0 0] 0.25", "[0 -1 0 0] 0.25")
    face([(x, y, top_at(x, y)) for x, y in reversed(pts[:3])], roof or wall,
         "[1 0 0 0] 0.25", "[0 -1 0 0] 0.25")
    for a, b in zip(pts, pts[1:] + pts[:1]):
        dx, dy = b[0] - a[0], b[1] - a[1]
        length = math.hypot(dx, dy)
        face([(a[0], a[1], top_at(*a)), (b[0], b[1], top_at(*b)), (b[0], b[1], bottom)], wall,
             "[%g %g 0 0] 0.25" % (dx / length, dy / length), "[0 0 -1 0] 0.25")
    s.add_child(Node("editor", [["color", "150 180 200"], ["visgroupshown", "1"],
                                ["visgroupautoshown", "1"]]))
    (owner or m.world).add_child(s)
    return s
