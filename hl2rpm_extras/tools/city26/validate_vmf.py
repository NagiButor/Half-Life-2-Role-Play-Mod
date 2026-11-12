"""Check every reconstructed brush as a bounded convex polyhedron."""
import argparse
import json
import sys
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0,str(ROOT / "hl2rpm_extras/tools/dialog_editor"))
from dlgedit.world.vmfgeom import parse, plane_of, base_winding, clip

p=argparse.ArgumentParser()
p.add_argument("vmf",type=Path)
p.add_argument("--report",type=Path)
args=p.parse_args()
root=parse(args.vmf.read_text(encoding="utf-8"))
errors=[]
volumes=[]
ids=[]
solids=[]
for owner in root.sub("world")+root.sub("entity"):
    solids.extend(owner.sub("solid"))
for solid in solids:
    sid=solid.get("id")
    ids.append(sid)
    sides=solid.sub("side")
    planes=[plane_of(s) for s in sides]
    if any(p is None for p in planes):
        errors.append([sid,"invalid plane"])
        continue
    faces=[]
    for i,(n,d) in enumerate(planes):
        w=base_winding(n,d)
        for j,pj in enumerate(planes):
            if j!=i:
                w=clip(w,pj[0],pj[1],eps=0.001)
            if len(w)<3:
                break
        if len(w)>=3:
            faces.append(np.array(w))
    if len(faces)<4:
        errors.append([sid,"unbounded/empty brush"])
        continue
    vertices=np.concatenate(faces)
    center=vertices.mean(0)
    volume=0.0
    for face in faces:
        for j in range(1,len(face)-1):
            volume+=abs(np.linalg.det(np.array([face[0]-center,face[j]-center,face[j+1]-center])))/6
    volumes.append(volume)
    if volume<1:
        errors.append([sid,"zero volume"])
    if np.max(np.abs(vertices))>=16384:
        errors.append([sid,"outside Source world"])
    if any(np.max(vertices@n-d)>0.1 for n,d in planes):
        errors.append([sid,"vertex outside convex halfspaces"])
report={"vmf":str(args.vmf),"brushes":len(solids),"checked_volumes":len(volumes),
        "min_volume":min(volumes),"errors":errors,
        "checks":["valid planes","bounded closed convex volumes","positive volume","Source +/-16384 bounds"]}
if args.report:
    args.report.write_text(json.dumps(report,indent=2))
print(json.dumps(report,indent=2))
sys.exit(1 if errors else 0)
