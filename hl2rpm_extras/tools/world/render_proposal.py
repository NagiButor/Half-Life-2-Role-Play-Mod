"""Draw a reviewable concept map from the existing world-layout draft."""
import json
import math
import os
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

HERE = Path(__file__).resolve().parent
OUT = Path(r'E:\SourceModding\HL2RPMSOURCE\hl2rpm_map_developing')
w = json.loads((HERE / 'world_layout.json').read_text(encoding='utf-8'))
S = 2200
im = Image.new('RGB', (S, 1900), '#151d20')
d = ImageDraw.Draw(im)
font = r'C:\Windows\Fonts\arial.ttf'
bold = r'C:\Windows\Fonts\arialbd.ttf'
def f(n, strong=False): return ImageFont.truetype(bold if strong else font, n)
def text(xy, s, n=21, color='#e8e8db', strong=False, anchor=None):
    d.text(xy, s, font=f(n,strong), fill=color, anchor=anchor, stroke_width=0)
text((55,32), 'СИТИ-27  /  МИР И ОКРЕСТНОСТИ', 42, strong=True)
text((57,91), 'Концепция для обсуждения • вид сверху • границы и масштабы предварительные', 23, '#aabbb6')
text((57,122), '↑ Восток   •   ← Север   •   система повёрнута по черновику сцены', 20, '#aabbb6')

X0,Y0=70,165
scale=.47
minx,maxy=-1550,1420
def pt(p): return (round(X0+(p[0]-minx)*scale), round(Y0+(maxy-p[1])*scale))
colors={'city':'#425a54','belt':'#7b674b','waste':'#626344'}
short={'station':['ВОКЗАЛ'],'centre':['ЦЕНТР'],'plaza':['ПЛОЩАДЬ','ГЕРОЕВ'], 'apartments':['ЖИЛОЙ','КВАРТАЛ'], 'quarantine':['КАРАНТИН','БУДА'], 'industrial':['ПРОМЗОНА'], 'canals':['КАНАЛЫ'], 'outskirts':['ОКРАИНЫ'], 'kobanya':['КЁБАНЬЯ'], 'road':['ДОРОГА'], 'm0':['РАЗВЯЗКА М0'], 'fields':['ПОЛЯ И','СОВХОЗ'], 'north_hills':['СЕВЕРНЫЕ','ХОЛМЫ'], 'airbase':['АЭРОДРОМ'], 'dunes':['ДЮНЫ'], 'danube_flats':['ДУНАЙСКАЯ','ОТМЕЛЬ'], 'budahills':['БУДАЙСКИЕ','ХОЛМЫ'], 'gellert':['ГОРА','ГЕЛЛЕРТ']}
for m in w['maps']:
    x0,y0,x1,y1=m['rect']; a=pt((x0,y1)); b=pt((x1,y0))
    d.rounded_rectangle((a,b),radius=8,fill=colors[m['kind']],outline='#c4cbb6',width=2)
    if not m['user']: text((a[0]+8,a[1]+6),'+',20,'#f1dca2',True)

# River and boundaries are conceptual overlays, not exact playable geometry.
p=w['features']['danube_pest_bank']; q=w['features']['danube_buda_bank']
d.polygon([pt(v) for v in p+list(reversed(q))], fill='#304c5b')
d.line([pt(v) for v in p],fill='#81a8af',width=3)
d.line([pt(v) for v in q],fill='#81a8af',width=3)
text(pt((-1460,-515)), 'ДУНАЙ',18,'#c1dadd',True,'mm')
for ln in w['features']['lines']:
    color={'road':'#dec48b','rail':'#d39a7d','metro':'#a7a1c5','canal':'#8dc4c7','bridge':'#f1e5ce','view':'#d1c1a1'}[ln['kind']]
    d.line([pt(p) for p in ln['pts']],fill=color,width=4 if ln['kind']!='view' else 2)
d.line([pt(p) for p in w['features']['wall']],fill='#e6a577',width=6)
for m in w['maps']:
    x0,y0,x1,y1=m['rect']; a=pt((x0,y1)); b=pt((x1,y0))
    cx=(a[0]+b[0])//2; cy=(a[1]+b[1])//2
    if m['id']=='danube_flats': cy=b[1]-35
    if m['id']=='centre': cx,cy=pt((-550,-300))
    for j,line in enumerate(short[m['id']]):
        text((cx,cy+(j-(len(short[m['id']])-1)/2)*23),line,18,'#f8f5e9',True,'mm')
cx,cy=pt((w['citadel']['x'],w['citadel']['y']))
d.ellipse((cx-13,cy-13,cx+13,cy+13),fill='#e99b61',outline='#fff1d7',width=3)
text((cx+18,cy-30),'ЦИТАДЕЛЬ',18,'#fff1d7',True)
for t in w['transitions']:
    x,y=pt(t['at']); num=t['id'][1:]
    d.ellipse((x-13,y-13,x+13,y+13),fill='#182326',outline='#f9e8ba',width=2)
    text((x,y),num,13,'#fff5d6',True,'mm')

left=1510
text((left,170),'УСЛОВНЫЕ ОБОЗНАЧЕНИЯ',25,strong=True)
for i,(color,label) in enumerate([('#425a54','Город'),('#7b674b','Промпояс и окраины'),('#626344','Пустоши'),('#304c5b','Дунай')]):
    y=215+i*35; d.rectangle((left,y,left+24,y+22),fill=color,outline='#cad2c6'); text((left+36,y),label,20)
text((left,372),'●  Цитадель  •  линия Стены',20,'#f2c497')
text((left,405),'01  Номер перехода',20)
text((left,438),'+   Дополнительная идея',20,'#f1dca2')
text((left,488),'ПЕРЕХОДЫ',25,strong=True)
names={m['id']:m['name'] for m in w['maps']}
for i,t in enumerate(w['transitions']):
    label=f"{t['id'][1:]}  {names[t['a']]} — {names[t['b']]}"
    text((left,528+i*34),label,17,'#e8e8db')
text((55,1575),'Маршруты: золотой — шоссе  •  терракотовый — железная дорога  •  голубой — канал  •  фиолетовый — метро',20,'#ced3c5')
text((55,1614),'Прямоугольники показывают состав и соседство, а не окончательную форму карт и не границы доступной поверхности.',21,'#aabbb6')
text((55,1648),'Точки переходов приблизительные; открытые стыки отмечены точкой на общем краю. Внутри районов возможны дополнительные проходы.',20,'#aabbb6')
text((55,1698),'ОСНОВА: расположение сканов Blender + координаты вдохновения; новые места помечены знаком «+».',20,'#aabbb6')
OUT.mkdir(exist_ok=True)
out=OUT / 'city27_world_concept.png'
im.save(out, optimize=True)
print(out)
