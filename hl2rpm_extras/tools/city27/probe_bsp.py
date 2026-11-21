"""Read-only collision/content probes of the actual compiled BSP20 world."""
import argparse
import json
import struct
from pathlib import Path

from math import hypot
from shapely import Polygon
from shapely.geometry.polygon import orient

p=argparse.ArgumentParser()
p.add_argument("bsp",type=Path)
p.add_argument("--report",type=Path)
args=p.parse_args()
blob=args.bsp.read_bytes()
assert blob[:4]==b"VBSP" and struct.unpack_from("<i",blob,4)[0]==20
lumps=[struct.unpack_from("<4i",blob,8+16*i) for i in range(64)]

def lump(i):
    offset,length,_,_=lumps[i]
    return blob[offset:offset+length]

planes=list(struct.iter_unpack("<4fi",lump(1)))
nodes=list(struct.iter_unpack("<iii6hHHhh",lump(5)))
leafdata=lump(10)
leafsize=32 if lumps[10][2]==1 else 56
leaves=[struct.unpack_from("<ihH6h4Hh",leafdata,i) for i in range(0,len(leafdata),leafsize)]
brushes=list(struct.iter_unpack("<3i",lump(18)))
brushsides=list(struct.iter_unpack("<Hhhh",lump(19)))
leafbrush=list(struct.iter_unpack("<H",lump(17)))

def contents(point):
    node=0
    while node>=0:
        n=nodes[node]
        plane=planes[n[0]]
        distance=sum(point[k]*plane[k] for k in range(3))-plane[3]
        node=n[1 if distance>=0 else 2]
    leaf=leaves[-1-node]
    result=leaf[0]
    # Detail brushes are in the leaf brush lists, not necessarily the BSP tree.
    first,count=leaf[11],leaf[12]
    for i in range(first,first+count):
        b=brushes[leafbrush[i][0]]
        hit=True
        for j in range(b[0],b[0]+b[1]):
            plane=planes[brushsides[j][0]]
            if sum(point[k]*plane[k] for k in range(3))-plane[3]>0.01:
                hit=False
                break
        if hit:
            result|=b[2]
    return result

layout=json.loads((Path(__file__).parent/"layout.json").read_text())
scale=layout["source_units_to_hammer"]
cx,cy=layout["source_center"]
def at(x,y,z):
    return (round((x-cx)*scale),round((y-cy)*scale),z)

checks=[]
def check(name,point,mask,expected):
    c=contents(point)
    checks.append({"name":name,"point":point,"contents":c,"ok":bool(c&mask)==expected})

for name,x,y in [("spawn",-.35,-2.4),("east street",4.6,-4.65),
                 ("south street",1.1,-8.2),("station forecourt",-.5,-1.9)]:
    check(name+" ground",at(x,y,120),1,True)
    check(name+" air",at(x,y,180),1,False)
check("river water",at(-8.3,-5.9,-192),32,True)
check("river air",at(-8.3,-5.9,192),1|32,False)
check("river bed",at(-8.3,-5.9,-800),1,True)
check("outside sealing hull",at(12.01,0,0),1,True)
check("station wall",at(-2,1.5,300),1,True)
for block in layout["blocks"]:
    inset=block.get("inset")
    if not inset:
        continue
    ring=list(orient(Polygon(block["outline"])).exterior.coords)[:-1]
    edge=block.get("gateway_edge",0)
    a,b=ring[edge],ring[(edge+1)%len(ring)]
    dx,dy=b[0]-a[0],b[1]-a[1]
    length=hypot(dx,dy)
    nx,ny=-dy/length,dx/length
    x=(a[0]+b[0])/2+nx*inset/2
    y=(a[1]+b[1])/2+ny*inset/2
    check(block["name"]+" open gateway",at(x,y,180),1,False)
    check(block["name"]+" gateway lintel",at(x,y,320),1,True)
report={"bsp":str(args.bsp),"brushes":len(brushes),"planes":len(planes),
        "nodes":len(nodes),"leaves":len(leaves),"faces":len(lump(7))//56,
        "worldlights_ldr":len(lump(15))//88,"worldlights_hdr":len(lump(54))//88,
        "vis_bytes":len(lump(4)),"checks":checks,"errors":sum(not c["ok"] for c in checks)}
if args.report:
    args.report.write_text(json.dumps(report,indent=2))
print(json.dumps(report,indent=2))
raise SystemExit(1 if report["errors"] else 0)
