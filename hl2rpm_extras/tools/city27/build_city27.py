"""Build the editable City-27 greybox from regularized scan reference outlines.

Requires numpy, shapely, Pillow. No mesh is imported into Source. All geometry
is convex, closed brushwork on a 1-unit grid; buildings have no interiors.
"""
import argparse
import json
import math
import sys
from collections import Counter
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw
from shapely import Polygon, LineString, box as rect, constrained_delaunay_triangles, make_valid
from shapely.geometry.polygon import orient
from shapely.ops import unary_union

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
sys.path.insert(0, str(ROOT / "AI_instructions/skills/hl2rpm-hammer/scripts"))
from vmf import VMF, Node, parse
from new_test_map import HEADER
from brushes import prism, NODRAW

WALL = "DEV/DEV_MEASUREGENERIC01B"
ROOF = "DEV/DEV_MEASUREWALL01C"
ROAD = "DEV/DEV_MEASUREGENERIC01B"
PAVING = "DEV/DEV_MEASUREGENERIC01"
ACCENT = "DEV/DEV_MEASUREWALL01A"
SKY = "TOOLS/TOOLSSKYBOX"


def polygons(geom):
    if geom.is_empty:
        return []
    if geom.geom_type == "Polygon":
        return [geom]
    return [p for g in geom.geoms for p in polygons(g)] if hasattr(geom, "geoms") else []


def convex_parts(poly):
    """Constrained triangulation, greedily merged back into larger convex brushes."""
    if poly.area < 1e-7:
        return []
    if not poly.interiors and abs(poly.area - poly.convex_hull.area) < 1e-7:
        return [orient(poly)]
    parts = list(constrained_delaunay_triangles(poly).geoms)
    changed = True
    while changed:
        changed = False
        for i in range(len(parts)):
            for j in range(i + 1, len(parts)):
                if not parts[i].intersects(parts[j]):
                    continue
                u = parts[i].union(parts[j])
                if u.geom_type == "Polygon" and not u.interiors and abs(u.area - u.convex_hull.area) < 1e-7:
                    parts[i] = u.simplify(0.00001)
                    parts.pop(j)
                    changed = True
                    break
            if changed:
                break
    return [orient(p) for p in parts]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("out", type=Path)
    parser.add_argument("--work", type=Path, required=True)
    args = parser.parse_args()
    layout = json.loads((HERE / "layout.json").read_text())
    args.work.mkdir(parents=True, exist_ok=True)
    m = VMF(parse(HEADER), str(args.out))
    m.world.set("skyname", "sky_proc_atmo_01")
    m.world.set("mapversion", "1")
    scale = layout["source_units_to_hammer"]
    center = np.array(layout["source_center"])
    counts = Counter()
    vgroot = m.root.child("visgroups")
    visgroups = {}
    for i, (name, color) in enumerate([
        ("00 Sealing sky / river bed", "90 150 220"),
        ("01 Continuous land / quay", "150 150 150"),
        ("02 Streets / sidewalks", "180 180 180"),
        ("03 Scan blocks / solid walls", "150 170 190"),
        ("04 Roofs / cornices", "220 150 80"),
        ("05 Station / rail platforms", "210 180 100"),
        ("06 Water / piers / stairs", "80 150 200"),
        ("07 Map boundary massing", "140 140 160"),
        ("08 Spawn / environment / views", "220 90 120")], 1):
        vgroot.add_child(Node("visgroup", [["name",name],["visgroupid",str(i)],["color",color]]))
        visgroups[i] = name

    def tag(node, group):
        editor = node.child("editor")
        if editor is None:
            editor = node.add_child(Node("editor"))
        editor.set("visgroupid", str(group))
        editor.set("visgroupshown", "1")
        editor.set("visgroupautoshown", "1")
        return node

    def transform(poly):
        def ring(coords):
            return [tuple(np.rint((np.array(c[:2]) - center) * scale)) for c in coords]
        return make_valid(Polygon(ring(poly.exterior.coords), [ring(r.coords) for r in poly.interiors]))

    def point(xy, z):
        q = np.rint((np.array(xy) - center) * scale)
        return (int(q[0]), int(q[1]), z)

    def solid(geom, bottom, top, wall, roof=None, group=3, detail=False):
        made = []
        for poly in polygons(geom):
            hp = transform(poly)
            for p in polygons(hp):
                for part in convex_parts(p):
                    pts = list(part.exterior.coords)[:-1]
                    # GEOS can retain redundant straight-line vertices: remove these.
                    pts = list(orient(Polygon(pts).simplify(0.1)).exterior.coords)[:-1]
                    if len(pts) < 3 or part.area < 16:
                        continue
                    s = prism(m, pts, bottom, top, wall, roof, lightmap=128)
                    tag(s, group)
                    made.append(s)
                    counts[visgroups[group]] += 1
        if detail and made:
            for s in made:
                m.world.remove_child(s)
            tag(m.add_brush_entity("func_detail", made), group)
        return made

    def hbox(a, b, mat, group=1, **kwargs):
        s = m.add_box(a, b, mat, lightmapscale=128, **kwargs)
        tag(s, group)
        counts[visgroups[group]] += 1
        return s

    def entity(cls, origin, **keys):
        return tag(m.add_entity(cls, origin, **keys), 9)

    # Bounds include all three captures, without stretching the actual scan plan.
    bounds = rect(-15.0, -14.5, 12.0, 8.0)
    x0,y0,_ = point((-15,-14.5),0)
    x1,y1,_ = point((12,8),0)
    bed, ceiling = -768, 3072
    hbox((x0-32,y0-32,bed-64),(x1+32,y1+32,bed),NODRAW)
    hbox((x0-32,y0-32,ceiling),(x1+32,y1+32,ceiling+32),SKY)
    hbox((x0-32,y0-32,bed),(x0,y1+32,ceiling),SKY)
    hbox((x1,y0-32,bed),(x1+32,y1+32,ceiling),SKY)
    hbox((x0,y0-32,bed),(x1,y0,ceiling),SKY)
    hbox((x0,y1,bed),(x1,y1+32,ceiling),SKY)

    bank = LineString(layout["river_bank"])
    bankpts = layout["river_bank"]
    land = Polygon(bankpts + [[12,-15],[12,8],[-15,8]]).intersection(bounds)
    water = bounds.difference(land)
    promenade = land.intersection(bank.buffer(0.5, cap_style=2, join_style=2))
    highland = land.difference(promenade)
    solid(land,bed,32,PAVING,group=2)
    solid(highland,32,128,ROAD,group=2)
    solid(water,bed,-128,NODRAW,roof="HL2RPM/WATER/MURKY",group=7)
    # The water meets solid world retaining walls, not a thin decorative mesh.
    solid(land.intersection(bank.buffer(0.035,cap_style=2,join_style=2)),32,44,ACCENT,group=7,detail=True)

    building_polys = []
    for block in layout["blocks"]:
        poly = orient(Polygon(block["outline"]))
        h = block["height"]
        inset = block.get("inset")
        footprint = poly.difference(poly.buffer(-inset,join_style=2)) if inset else poly
        if block.get("cross"):
            # Two regular wings reproduce the multiple open-air courtyards of the scan.
            minx,miny,maxx,maxy = poly.bounds
            cx,cy = poly.centroid.coords[0]
            cross = rect(cx-0.10,miny,cx+0.10,maxy).union(rect(minx,cy-0.10,maxx,cy+0.10))
            footprint = footprint.union(cross.intersection(poly))
        building_polys.append(poly)
        gateway=None
        if inset:
            ring=np.array(poly.exterior.coords)[:-1]
            edgeidx=block.get("gateway_edge",0)
            a0,a1=ring[edgeidx],ring[(edgeidx+1)%len(ring)]
            edge=a1-a0
            tangent=edge/np.linalg.norm(edge)
            inside=np.array([-tangent[1],tangent[0]])
            midpoint=(a0+a1)/2
            # Exterior courtyard gateway, not a furnished interior.
            p0=midpoint-inside*0.10
            p1=midpoint+inside*(inset+0.15)
            gateway=Polygon([p0-tangent*0.065,p0+tangent*0.065,
                             p1+tangent*0.065,p1-tangent*0.065])
            solid(footprint.difference(gateway),128,272,WALL,ROOF,group=4)
            solid(footprint,272,128+h,WALL,ROOF,group=4)
        else:
            solid(footprint,128,128+h,WALL,ROOF,group=4)
        # Foundation plinth and cornice are detail, do not split visibility leaves.
        outerlip = poly.buffer(0.018,join_style=2).difference(poly.buffer(-0.055,join_style=2))
        solid(outerlip.difference(gateway) if gateway is not None else outerlip,
              128,144,PAVING,group=5,detail=True)
        solid(outerlip,128+h-20,128+h+4,PAVING,group=5,detail=True)
        if block.get("roof") == "hip" and abs(poly.area-poly.convex_hull.area)<1e-7:
            # Matched offset rings make a continuous hipped roof, including corners.
            width = min(inset or 0.36,0.36)
            ridge = poly.buffer(-width/2,join_style=2)
            inner = poly.buffer(-width,join_style=2)
            n=len(poly.exterior.coords)-1
            if (not ridge.is_empty and not inner.is_empty and ridge.geom_type=="Polygon"
                and inner.geom_type=="Polygon" and len(ridge.exterior.coords)==n+1
                and len(inner.exterior.coords)==n+1):
                a=np.array(poly.exterior.coords)[:-1]
                def matched(p):
                    ring=np.array(orient(p).exterior.coords)[:-1]
                    candidates=[np.roll(ring,k,axis=0) for k in range(n)]
                    return min(candidates,key=lambda b:np.linalg.norm(a-b,axis=1).sum())
                r,b=matched(ridge),matched(inner)
                for j in range(n):
                    k=(j+1)%n
                    pa,pb=np.array(point(a[j],0)[:2]),np.array(point(a[k],0)[:2])
                    edge=pb-pa
                    normal=np.array([-edge[1],edge[0]],float)/np.linalg.norm(edge)
                    run=width/2*scale
                    for inside,quad in ((False,Polygon([a[j],a[k],r[k],r[j]])),
                                        (True,Polygon([r[j],r[k],b[k],b[j]]))):
                        def roof_height(x,y,pa=pa,normal=normal,run=run,h=h,inside=inside):
                            t=float(np.dot(np.array([x,y])-pa,normal))/run
                            return 128+h+4+(2-t if inside else t)*64
                        solid(quad,128+h-8,roof_height,ROOF,ROOF,group=5,detail=True)
        # Each block gets a stable identity in Hammer without a runtime entity.
        c = poly.centroid.coords[0]
        entity("info_null",point(c,128+h+128),targetname="c27_"+block["name"])

    # Large station with a raised central train shed, entrance and platform tracks.
    station = orient(Polygon(layout["station"]["outline"]))
    building_polys.append(station)
    solid(station,128,608,WALL,ROOF,group=6)
    a=np.array(station.exterior.coords)[:-1]
    u=(a[1]-a[0]); width=np.linalg.norm(u); u/=width
    v=(a[3]-a[0]); length=np.linalg.norm(v); v/=length
    def local(x0,y0,x1,y1):
        return Polygon([a[0]+u*x0+v*y0,a[0]+u*x1+v*y0,a[0]+u*x1+v*y1,a[0]+u*x0+v*y1])
    solid(local(0,0,width,0.27),608,928,ACCENT,ROOF,group=6)
    solid(local(0,length-0.4,width,length),128,864,WALL,ROOF,group=6)
    solid(local(0,0,0.22,length),608,800,WALL,ROOF,group=6)
    solid(local(width-0.22,0,width,length),608,800,WALL,ROOF,group=6)
    # Pitched longitudinal shed, two planar faces.
    half=width/2
    for side in (0,1):
        p0,p1=(0,half) if side==0 else (half,width)
        poly=local(p0,0.27,p1,length-0.4)
        base=np.array(point(a[0],0)[:2])
        def shed(x,y,side=side,base=base,u=u,width=width):
            t=float(np.dot(np.array([x,y])-base,u))/scale
            return 800+max(0,min(t,width-t))*220
        solid(poly,608,shed,ROOF,ROOF,group=6,detail=True)
    # Five dead-end test tracks continuing north-west, not an inaccessible interior.
    for i in range(5):
        tx=0.25+(width-0.5)*i/4
        path=LineString([a[0]+u*tx-v*2.1,a[0]+u*tx])
        solid(path.buffer(0.055,cap_style=2),128,136,PAVING,group=6,detail=True)
        for dx in (-0.038,0.038):
            rail=LineString([a[0]+u*(tx+dx)-v*2.1,a[0]+u*(tx+dx)])
            solid(rail.buffer(0.004,cap_style=2),136,140,ACCENT,group=6,detail=True)

    built=unary_union(building_polys)
    streets=[]
    for street in layout["streets"]:
        road=LineString(street["points"]).buffer(street["width"]/2,cap_style=2,join_style=2)
        streets.append(road)
    # Union before building avoids overlapping curb strips at intersections.
    roadnet=unary_union(streets)
    walk=roadnet.buffer(0.10,join_style=2).difference(roadnet).intersection(highland).difference(built.buffer(0.03))
    solid(walk,128,136,PAVING,group=3,detail=True)
    # Border streets terminate at continuous backdrops, not holes into the void.
    for i,(a,b) in enumerate([
        ((-13.6,5.4),(-10.8,6.5)),((-10.7,6.5),(-7.5,7.2)),
        ((-0.2,6.5),(3.2,7.2)),((3.4,5.6),(6.0,6.4)),
        ((9.9,-2.5),(11.1,1.5)),((9.6,-8.5),(11.1,-5.6)),
        ((3.5,-13.5),(6.8,-12.6))]):
        p=rect(a[0],a[1],b[0],b[1]).intersection(highland).difference(built)
        solid(p,128,640+(i%3)*128,WALL,ROOF,group=8)

    # Six clean piers follow the quay. Stairs reconnect both waterfront levels.
    for i,along in enumerate((0.12,0.24,0.36,0.47,0.60,0.72)):
        d=bank.length*along
        c=np.array(bank.interpolate(d).coords[0])
        tangent=np.array(bank.interpolate(min(d+0.02,bank.length)).coords[0])-c
        tangent/=np.linalg.norm(tangent)
        # Bank runs downstream; right normal points into the river.
        river=np.array([tangent[1],-tangent[0]])
        q=[c-tangent*0.075+river*0.02,c+tangent*0.075+river*0.02,
           c+tangent*0.075+river*0.68,c-tangent*0.075+river*0.68]
        solid(Polygon(q),-192,16,PAVING,group=7,detail=True)
        head=c+river*0.6
        cross=[head-tangent*0.35-river*0.06,head+tangent*0.35-river*0.06,
               head+tangent*0.35+river*0.06,head-tangent*0.35+river*0.06]
        solid(Polygon(cross),-192,16,PAVING,group=7,detail=True)
        inland=-river
        # 8 x 12-unit risers, 24-unit treads; no oversized player-blocking steps.
        for step in range(8):
            off=0.332+step*0.024
            cc=c+inland*off
            poly=Polygon([cc-tangent*0.12,cc+tangent*0.12,
                          cc+tangent*0.12+inland*0.026,cc-tangent*0.12+inland*0.026])
            solid(poly,32,44+step*12,PAVING,group=7,detail=True)

    spawn=point((-0.35,-2.4),144)
    entity("info_player_start",spawn,angles="0 150 0")
    entity("light_environment",point((-0.2,-2.3),2200),angles="-48 35 0",pitch="-48",
           _light="255 239 215 300",_ambient="145 166 198 70",_lightHDR="-1 -1 -1 1",
           _lightscaleHDR="1",_ambientHDR="-1 -1 -1 1",_AmbientScaleHDR="1",SunSpreadAngle="5")
    entity("water_lod_control",point((-0.4,-2.4),160),cheapwaterstartdistance="3000",cheapwaterenddistance="7000")
    entity("env_fog_controller",point((-0.3,-2.3),160),targetname="c27_fog",fogenable="1",
           fogstart="9000",fogend="22000",farz="26000",fogcolor="170 185 195",fogcolor2="170 185 195",fogblend="0")
    for name,xy,z,angle in [
        ("station_square",(-0.35,-2.4),192,"0 150 0"),
        ("quay",(-6.4,-3.7),192,"0 -42 0"),
        ("river",(-8.3,-5.9),192,"0 46 0"),
        ("east_street",(4.6,-4.65),192,"0 180 0"),
        ("south_street",(1.1,-8.2),192,"0 105 0"),
        ("overview",(-1.5,-3.0),2800,"65 100 0")]:
        entity("info_target",point(xy,z),targetname="c27_view_"+name,angles=angle)
    m.root.child("cameras").set("activecamera","0")
    m.root.child("cameras").add_child(Node("camera",[["position","[%d %d %d]"%spawn],
        ["look","[%d %d %d]"%point((-2,-0.5),400)]]))
    args.out.parent.mkdir(parents=True,exist_ok=True)
    m.save(str(args.out),backup=args.out.exists())
    report={"vmf":str(args.out),"reference":"E:/Google Map 3D Exporter v3.0/city-26-prototype.fbx",
        "transform":{"xy_scale":scale,"center":center.tolist(),"ground":128,"water":-128,
                     "height_policy":"regularized 32/64-unit storeys; no vertical scan noise"},
        "brushes":len(m.solids()),"entities":len(m.entities()),"by_visgroup":dict(counts),
        "bounds":m.bounds(),"spawn":spawn}
    (args.work/"build_report.json").write_text(json.dumps(report,indent=2))
    # Overlay is a construction check against the scan, not presentation artwork.
    raster=args.work/"heightfields.npz"
    if raster.exists():
        data=np.load(raster);lo=data["lo"];pix=float(data["pixel"])
        im=Image.open(args.work/"reference_plan.png").convert("RGB")
        draw=ImageDraw.Draw(im)
        def screen(xy):
            return [(int((x-lo[0])/pix),im.height-1-int((y-lo[1])/pix)) for x,y in xy]
        for block in layout["blocks"]+[layout["station"]]:
            q=screen(block["outline"])
            draw.line(q+[q[0]],fill="white",width=3)
        draw.line(screen(bankpts),fill=(255,100,255),width=4)
        im.save(args.work/"layout_overlay.png")
    print(json.dumps(report,indent=2))


if __name__=="__main__":
    main()
