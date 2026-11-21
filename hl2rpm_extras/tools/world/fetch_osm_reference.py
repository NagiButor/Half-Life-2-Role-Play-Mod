"""Fetch small OpenStreetMap map-API tiles for the central reference area."""
import concurrent.futures
from pathlib import Path
import requests

OUT=Path(r'E:\SourceModding\HL2RPMSOURCE\hl2rpm_map_developing\osm_reference')
OUT.mkdir(exist_ok=True)
lats=[47.497,47.503,47.509,47.515,47.521,47.527]
lons=[19.039,19.048,19.057,19.066,19.075,19.084]
def tile(args):
    i,j=args; p=OUT/f'city_{i}_{j}.osm'
    if p.exists() and p.stat().st_size>1000: return p.name,'cached'
    bbox=f'{lons[j]},{lats[i]},{lons[j+1]},{lats[i+1]}'
    r=requests.get('https://api.openstreetmap.org/api/0.6/map',params={'bbox':bbox},headers={'User-Agent':'HL2RPM-reference/1.0 (local research)'},timeout=100)
    r.raise_for_status()
    if not r.content.startswith(b'<?xml'): raise ValueError('not OSM XML')
    p.write_bytes(r.content)
    return p.name,len(r.content)
with concurrent.futures.ThreadPoolExecutor(max_workers=3) as pool:
    for result in pool.map(tile,((i,j) for i in range(5) for j in range(5))): print(*result,flush=True)
