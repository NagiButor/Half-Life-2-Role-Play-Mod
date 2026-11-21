"""Create a non-destructive planning overlay over the Blender orthographic render.

Coordinates below use a 2000 x 2000 normalized image, not Hammer units. Boundaries
and connection corridors are proposals, not measured building outlines or BSPs.
"""
from pathlib import Path
import math
from PIL import Image, ImageDraw, ImageFont, ImageEnhance

OUT = Path(r'E:\SourceModding\HL2RPMSOURCE\hl2rpm_map_developing')
raw = Image.open(OUT / 'city27_scan_top.png').convert('RGB')
assert raw.size == (3200, 3200), raw.size
base = ImageEnhance.Brightness(raw).enhance(2.1)
W,H=4320,3200
im=Image.new('RGB',(W,H),'#253033'); im.paste(base,(0,0))
d=ImageDraw.Draw(im)
s=1.6
def pt(p): return tuple(round(v*s) for v in p)
def font(size): return ImageFont.truetype(r'C:\Windows\Fonts\arialbd.ttf',size)
def dashline(points, color, width=5, dash=19, gap=12):
    for a,b in zip(points,points[1:]):
        x,y=pt(a); x2,y2=pt(b); length=math.hypot(x2-x,y2-y)
        if not length: continue
        u=0
        while u<length:
            v=min(u+dash,length)
            d.line((x+(x2-x)*u/length,y+(y2-y)*u/length,x+(x2-x)*v/length,y+(y2-y)*v/length),fill=color,width=width)
            u+=dash+gap
def text(x,y,string,size=26,fill='#f5f2de'):
    d.text((x,y),string,font=font(size),fill=fill,stroke_width=1,stroke_fill='#182124')
def caption(point,string):
    x,y=pt(point); f=font(27); box=d.textbbox((x,y),string,font=f)
    d.rounded_rectangle((box[0]-7,box[1]-5,box[2]+7,box[3]+7),radius=5,fill='#162426',outline='#d2e2d1',width=2)
    d.text((x,y),string,font=f,fill='#fff6dc')

# Boundaries are drawn mostly outside scans. They enclose reference fragments;
# missing context between them is not claimed to be actual streets or buildings.
regions=[
 ('ОКРАИНЫ',[(330,95),(685,95),(685,730),(330,730),(330,95)],(350,75)),
 ('ПРОМЗОНА',[(700,185),(1050,45),(1350,730),(1005,885),(700,185)],(790,65)),
 ('ДОРОГА',[(1328,225),(1632,225),(1632,910),(1328,910),(1328,225)],(1350,196)),
 ('ЖИЛОЙ КВАРТАЛ',[(416,963),(930,963),(930,1872),(416,1872),(416,963)],(425,935)),
 ('ВОКЗАЛ',[(1068,890),(1335,890),(1335,1408),(1068,1408),(1068,890)],(1080,862)),
 ('ЦЕНТР',[(922,1407),(1310,1407),(1310,1990),(922,1990),(922,1407)],(935,1376)),
 ('ПЛОЩАДЬ',[(1565,1050),(1955,1080),(1890,1820),(1500,1790),(1565,1050)],(1515,1020)),
]
for _,poly,_ in regions: dashline(poly,'#f4c66e',5,21,13)

# Suggested connection corridors in the empty regions, not exact road alignments.
links=[
 ('01',[(1160,1395),(1160,1418)]), # centre station
 ('02',[(915,1165),(995,1165),(1072,1165)]), # homes station
 ('03',[(880,1750),(900,1810),(930,1810)]), # homes centre
 ('04',[(1325,1310),(1430,1310),(1550,1240)]), # station plaza
 ('05',[(1310,1545),(1400,1545),(1515,1500)]), # centre plaza
 ('06',[(1150,895),(1120,875)]), # station industrial
 ('07',[(675,445),(850,445)]), # outskirts industry
 ('08',[(1325,730),(1338,730)]), # industry road
 ('09',[(1480,900),(1480,970),(1600,1050)]), # road plaza
 ('10',[(500,730),(500,960)]), # outskirts homes via canal provisional
]
for ident,path in links:
    dashline(path,'#69e2dc',6,12,11)
    a,b=path[len(path)//2-1],path[len(path)//2]
    x,y=pt(((a[0]+b[0])/2,(a[1]+b[1])/2))
    d.ellipse((x-21,y-21,x+21,y+21),fill='#10272a',outline='#a7f6ef',width=3)
    d.text((x,y),ident,font=font(21),fill='white',anchor='mm')
for name,_,location in regions: caption(location,name)

# Citadel axis from Blender mesh Object_8, approximately (14.47, 2.37).
# Render camera was centred (-3.7, 7), orthographic width 35.
cx=(14.47+21.2)/35*3200; cy=(24.5-2.37)/35*3200
d.ellipse((cx-15,cy-15,cx+15,cy+15),fill='#f58b5e',outline='white',width=3)
text(cx+24,cy-22,'ОСЬ ЦИТАДЕЛИ',27,'#ffd0ac')

left=3570
text(left,45,'СИТИ-27',40)
text(left,97,'ПЛАН ПО СКАНАМ',30)
text(left,145,'BLENDER',30)
text(left,215,'УСЛОВНЫЕ ЗНАКИ',26)
text(left,260,'- - -  Граница участка*',24,'#f4c66e')
text(left,300,'- - -  Связь / переход*',24,'#69e2dc')
text(left,340,'●  Ось Цитадели',24,'#ffd0ac')
text(left,395,'* Предложение, не BSP.',22)
text(left,425,'Серая область — нет',22)
text(left,455,'детального локального',22)
text(left,485,'скана в этом виде.',22)
text(left,550,'ПЕРЕХОДЫ',28)
for i,line in enumerate([
 '01  Центр — вокзал', '02  Жильё — вокзал',
 '03  Жильё — центр', '04  Вокзал — площадь',
 '05  Центр — площадь', '06  Вокзал — промзона',
 '07  Окраины — промзона', '08  Промзона — дорога',
 '09  Дорога — площадь', '10  Окраины — жильё',
]): text(left,600+i*43,line,22)
text(left,1115,'10: возможный маршрут',21)
text(left,1147,'через каналы. Положение',21)
text(left,1179,'и русло уточнить.',21)
text(left,1250,'Карантин и пустоши:',23)
text(left,1282,'участки ещё не нанесены;',21)
text(left,1314,'локальных сканов здесь нет.',21)
text(left,1390,'Границы и маршруты',21)
text(left,1422,'нуждаются в проверке',21)
text(left,1454,'по улицам и фасадам.',21)
im.save(OUT/'city27_scan_plan_v2.png',optimize=True)

# Full-resolution source crops for inspecting individual scan fragments.
cropdir=OUT/'city27_scan_details'; cropdir.mkdir(exist_ok=True)
for name,box in [
 ('01_outskirts',(320,85,695,740)),('02_industrial',(690,35,1360,895)),
 ('03_road',(1320,215,1640,920)),('04_apartments',(405,950,940,1885)),
 ('05_station',(1055,880,1345,1415)),('06_centre',(910,1400,1320,1999)),
 ('07_plaza',(1490,1040,1965,1830))]:
    base.crop(tuple(round(v*s) for v in box)).save(cropdir/(name+'.png'),optimize=True)
print(OUT/'city27_scan_plan_v2.png')
