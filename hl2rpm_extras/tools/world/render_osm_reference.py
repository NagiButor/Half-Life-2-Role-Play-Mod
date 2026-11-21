"""Render actual OSM buildings and streets for City-27 reference planning."""
from pathlib import Path
from xml.etree import ElementTree as ET
from PIL import Image,ImageDraw,ImageFont
import math,json

ROOT=Path(r'E:\SourceModding\HL2RPMSOURCE\hl2rpm_map_developing')
nodes={}; ways={}
for path in sorted((ROOT/'osm_reference').glob('city_*.osm')):
    for _,e in ET.iterparse(path,events=('end',)):
        if e.tag=='node': nodes[e.attrib['id']]=(float(e.attrib['lat']),float(e.attrib['lon']))
        elif e.tag=='way':
            tags={a.attrib['k']:a.attrib['v'] for a in e.findall('tag')}
            if 'building' in tags or 'highway' in tags or tags.get('waterway') or tags.get('natural')=='water':
                ways[e.attrib['id']]=([a.attrib['ref'] for a in e.findall('nd')],tags)
            e.clear()
        elif e.tag in ('relation','bounds'): e.clear()

W,H=4800,5100
lat0,lat1=47.497,47.527
lon0,lon1=19.039,19.084
margin=110
def xy(lat,lon): return (int(margin+(lon-lon0)/(lon1-lon0)*(W-2*margin)),int(margin+(lat1-lat)/(lat1-lat0)*(H-2*margin)))
im=Image.new('RGB',(W,H),'#e3e2d5'); d=ImageDraw.Draw(im)
roadways=[]; count=0
for nd,t in ways.values():
    pts=[xy(*nodes[n]) for n in nd if n in nodes]
    if len(pts)<2: continue
    if t.get('natural')=='water' and len(pts)>=3: d.polygon(pts,fill='#9dbdc5')
    if 'building' in t and len(pts)>=3:
        d.polygon(pts,fill='#9e8c7a',outline='#806c5d');count+=1
    if 'highway' in t: roadways.append((pts,t))
for pts,t in roadways:
    h=t['highway']; width={'primary':10,'secondary':8,'tertiary':7,'residential':5,'service':3,'footway':2,'path':2}.get(h,4)
    d.line(pts,fill='#fbf9e9',width=width,joint='curve')

# Three real street corridors: highlighted by named OSM ways. Split between
# the actual regional endpoints, not claimed to coincide with Blender offsets.
routes={
 'Bajcsy-Zsilinszky út':'#cf5147',
 'József Attila utca':'#cf5147',
 'Teréz körút':'#387faf',
 'Andrássy út':'#387faf',
 'Szent István körút':'#5b9560',
 'Tátra utca':'#5b9560',
 'Pozsonyi út':'#7a9c63',
}
for pts,t in roadways:
    name=t.get('name','')
    if name in routes: d.line(pts,fill=routes[name],width=8,joint='curve')
font=r'C:\Windows\Fonts\arialbd.ttf'
def tag(lat,lon,num,label):
    x,y=xy(lat,lon);d.ellipse((x-26,y-26,x+26,y+26),fill='#18353d',outline='white',width=4)
    d.text((x,y),num,font=ImageFont.truetype(font,25),anchor='mm',fill='white')
    f=ImageFont.truetype(font,27); bb=d.textbbox((x+34,y-17),label,font=f)
    d.rectangle((bb[0]-4,bb[1]-3,bb[2]+4,bb[3]+3),fill='#18353d')
    d.text((x+34,y-17),label,font=f,fill='white')
tag(47.510139,19.056664,'1','ВОКЗАЛ')
tag(47.500571,19.046584,'2','ЦЕНТР')
tag(47.515024,19.077592,'3','ПЛОЩАДЬ')
tag(47.523269,19.055583,'4','ЖИЛЬЁ')

# Candidate transfer points, approximate intersection coordinates, to be
# checked against the street geometry in detail before map construction.
for lat,lon,num in [(47.4995112,19.0548448,'A'),(47.5053009,19.0634462,'B'),(47.5125894,19.0498605,'C')]:
    x,y=xy(lat,lon);d.rectangle((x-18,y-18,x+18,y+18),fill='#f1c64f',outline='#17383d',width=4)
    d.text((x,y),num,font=ImageFont.truetype(font,20),anchor='mm',fill='#14292c')
f=ImageFont.truetype(font,28)
d.rectangle((15,15,3200,65),fill='#173139')
d.text((30,23),'БУДАПЕШТ: РЕАЛЬНАЯ ГЕОГРАФИЯ • КОНТУРЫ И УЛИЦЫ OPENSTREETMAP',font=f,fill='white')
d.rectangle((15,H-66,3500,H-12),fill='#173139')
d.text((30,H-58),'© OpenStreetMap contributors • ODbL • ЭТО НЕ СЖАТАЯ КОМПОЗИЦИЯ BLENDER',font=f,fill='white')
im.save(ROOT/'city27_real_geography_osm.png',optimize=True)
print('nodes',len(nodes),'ways',len(ways),'building polygons',count)
print('route ways', {name:sum(t.get('name')==name for _,t in roadways) for name in routes})
print('example intersections')
for lat,lon in [(47.5047,19.0549),(47.5066,19.0616),(47.512,19.0498)]:
    nearby=[]
    for pts,t in roadways:
        if not t.get('name'):continue
        target=xy(lat,lon); distance=min(math.hypot(x-target[0],y-target[1]) for x,y in pts)
        if distance<100: nearby.append((round(distance),t['name']))
    print(lat,lon,sorted(set(nearby))[:12])
