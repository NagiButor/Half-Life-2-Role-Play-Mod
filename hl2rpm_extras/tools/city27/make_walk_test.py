"""Generate repeatable player-hull walking probes for a courtyard and quay stair."""
import argparse
import json
import math
from pathlib import Path

import numpy as np
from shapely import Polygon, LineString
from shapely.geometry.polygon import orient

p=argparse.ArgumentParser()
p.add_argument("out",type=Path)
args=p.parse_args()
layout=json.loads((Path(__file__).parent/"layout.json").read_text())
scale=layout["source_units_to_hammer"]
center=np.array(layout["source_center"])
def position(xy,z):
    q=np.rint((np.array(xy)-center)*scale).astype(int)
    return f"{q[0]} {q[1]} {z}"
def yaw(v):
    return math.degrees(math.atan2(v[1],v[0]))

bank=LineString(layout["river_bank"])
d=bank.length*.36
c=np.array(bank.interpolate(d).coords[0])
t=np.array(bank.interpolate(d+.02).coords[0])-c
t/=np.linalg.norm(t)
inside=np.array([-t[1],t[0]])
stair_start=c+inside*.23
stair_end=c+inside*.64
block=next(b for b in layout["blocks"] if b["name"]=="east_court_03")
ring=np.array(orient(Polygon(block["outline"])).exterior.coords)[:-1]
a,b=ring[:2]
edge=b-a
normal=np.array([-edge[1],edge[0]])/np.linalg.norm(edge)
mid=(a+b)/2
gate_start=mid-normal*.06
lines=[
    "|cl_weather_tour_step 6; sv_weather clear 0; sv_timecycle_set_time 14; r_drawviewmodel 0; cl_rpm_tp 0",
    f"|cl_weather_tour_step 3; setpos {position(stair_start,48)}; setang 0 {yaw(inside):.4f} 0; echo C27_STAIR_SETTLE",
    f"shot|setang 0 {yaw(inside):.4f} 0; getpos; echo C27_STAIR_START; +forward",
    f"shot|setang 0 {yaw(inside):.4f} 0; -forward; getpos; echo C27_STAIR_END",
    f"|setpos {position(gate_start,144)}; setang 0 {yaw(normal):.4f} 0; echo C27_GATE_SETTLE",
    f"shot|setang 0 {yaw(normal):.4f} 0; getpos; echo C27_GATE_START; +forward",
    f"shot|setang 0 {yaw(normal):.4f} 0; -forward; getpos; echo C27_GATE_END",
    "|cl_weather_tour_step 8; noclip; cl_playermodel_legs_override 0; setpos 800 -4000 2800; setang 65 105 0",
    "shot|setpos 800 -4000 2800; setang 65 105 0",
    "shot|setpos -4200 5400 2700; setang 50 -20 0",
    "shot|setpos -6963 -2970 192; setang -3 46 0",
    "shot|setpos 1178 614 192; setang -4 150 0",
    "shot|setpos 6246 -1690 192; setang 0 180 0",
]
args.out.write_text("\n".join(lines)+"\n")
print(json.dumps({"stair_start":position(stair_start,48),"stair_expected_landing":position(stair_end,128),
                  "stair_yaw":yaw(inside),"gate_start":position(gate_start,144),"gate_yaw":yaw(normal)},indent=2))
