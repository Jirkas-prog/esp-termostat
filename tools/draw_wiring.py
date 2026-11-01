"""Reproducible vector wiring overview; exact wiring notes live in docs/ZAPOJENI.md."""
from html import escape
from pathlib import Path

out = Path(__file__).resolve().parents[1] / 'docs' / 'ZAPOJENI.svg'
parts = ['''<svg xmlns="http://www.w3.org/2000/svg" width="1400" height="1700" viewBox="0 0 1400 1700">
<defs><marker id="arrow" markerWidth="10" markerHeight="10" refX="9" refY="5" orient="auto"><path d="M0 0L10 5L0 10" fill="none" stroke="#25686b" stroke-width="1.6"/></marker></defs>
<style>text{font-family:Arial,sans-serif;fill:#172b3a} .wire{fill:none;stroke:#25686b;stroke-width:3;stroke-linecap:round;stroke-linejoin:round}</style>
<rect width="1400" height="1700" fill="#f5f7fa"/>''']

def text(x, y, value, size=19, weight='normal', color='#172b3a'):
    parts.append(f'<text x="{x}" y="{y}" font-size="{size}" font-weight="{weight}" style="fill:{color}">{escape(value)}</text>')

def rect(x, y, w, h, fill='#ffffff', stroke='#b2c6d3', radius=12):
    parts.append(f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="{radius}" fill="{fill}" stroke="{stroke}" stroke-width="2"/>')

def wire(points, arrow=False, color='#25686b', dashed=False):
    pts = ' '.join(f'{x},{y}' for x,y in points)
    extra = (' marker-end="url(#arrow)"' if arrow else '') + (' stroke-dasharray="8 6"' if dashed else '')
    parts.append(f'<polyline points="{pts}" class="wire" style="stroke:{color}"{extra}/>')

def dot(x, y):
    parts.append(f'<circle cx="{x}" cy="{y}" r="4" fill="#25686b"/>')

def labelbox(x,y,w,h,title,lines,fill='#ffffff'):
    rect(x,y,w,h,fill); text(x+18,y+30,title,22,'bold')
    for i,line in enumerate(lines): text(x+18,y+60+i*26,line,18)

text(50,48,'ESP TERMOSTAT · dvě samostatné řídicí větve',32,'bold')
text(50,82,'Revize 1 · 29. 10. 2026 · hlavní varianta: klasická relé 5 V, kontakty NC',20)
rect(50,104,1300,47,'#fff3d9','#dfb76b')
text(68,135,'Vývojový návrh. Silové dimenzování, ochrany kamen a fyzické zkoušky musí být dokončeny před provozem.',18)

labelbox(60,180,360,85,'Zdroj A · 5 V',['Samostatné jištění / GND_A'],'#eaf4f5')
labelbox(980,180,360,85,'Zdroj B · 5 V',['Samostatné jištění / GND_B'],'#eaf4f5')
wire([(240,265),(240,295)],True); wire([(1160,265),(1160,295)],True)
labelbox(60,295,360,215,'A · LILYGO / TTGO T-Display',[
    'Původní ESP32 · LCD ST7789',
    'Lokální regulace + kalibrace A',
    'TX21 →      ← RX22',
    'Čidlo 27 · relé 25 · WDI 13',
    'LCD: 19/18/5/16/23/4'], '#ffffff')
labelbox(980,295,360,215,'B · Seeed XIAO ESP32-C6',[
    'Zigbee router · vlastní sonda',
    'Lokální regulace + kalibrace B',
    '← RX17/D7      TX16/D6 →',
    'Čidlo 0/D0 · relé 1/D1',
    'Externí watchdog WDI18/D10'], '#ffffff')
rect(550,325,300,150,'#eaf4f5','#4e9698')
text(575,356,'ISO7721 · 3,3 V / 3,3 V',21,'bold')
text(580,389,'A → B: TX21 → RX17',18)
text(580,426,'B → A: RX22 ← TX16',18)
text(590,458,'GND_A ≠ GND_B',18,'bold')
wire([(420,385),(550,385)],True); wire([(850,385),(980,385)],True)
wire([(980,425),(850,425)],True); wire([(550,425),(420,425)],True)
text(478,300,'UART 115200 · CRC · obousměrné echo · timeout 5 s',18)

labelbox(60,550,190,100,'DS18B20 A',['3V3_A / GND_A','DQ → GPIO27'])
labelbox(270,550,230,100,'TTP224 · 3,3 V',['Q1–4 → 32/33/36/39','zámek / − / + / uložit'])
wire([(150,510),(150,550)]); wire([(335,510),(335,530),(385,530),(385,550)])
labelbox(980,550,360,100,'DS18B20 B',['3V3_B / GND_B · DQ → GPIO0', 'Každé DQ: pull-up 4k7 na své 3,3 V'])
wire([(1160,510),(1160,550)])
labelbox(550,550,360,100,'Home Assistant',['A: Wi-Fi → MQTT broker', 'B: Zigbee → Z2M → MQTT broker'],'#eef0fb')
wire([(420,480),(515,480),(515,585),(550,585)],True)
wire([(980,480),(945,480),(945,620),(910,620)],True)
text(558,686,'Každá cesta přenáší obě teploty + dohled.',18)
text(558,712,'HA vrací potvrzení zprávy; regulace je lokální.',18)

labelbox(60,685,360,100,'Budič Q_A + cívka K_A 5 V',['GPIO25 · aktivní HIGH', 'LOW / ztráta 5 V → NC sepne'],'#eaf4f5')
labelbox(980,685,360,100,'Budič Q_B + cívka K_B 5 V',['GPIO1 · aktivní HIGH', 'LOW / ztráta 5 V → NC sepne'],'#eaf4f5')
wire([(60,480),(36,480),(36,725),(60,725)],True)
wire([(1340,480),(1364,480),(1364,725),(1340,725)],True)
text(60,825,'Zdroje 5 V ani jejich země neparalelně. Kontakty relé patří do stejného jištěného silového okruhu.',20,'bold')

rect(50,865,1300,350,'#ffffff')
text(75,904,'Silový princip · obě relé samostatně dimenzovat na celou zátěž čerpadla',24,'bold')
text(80,945,'L z UPS',19,'bold'); rect(190,927,110,30,'#fff3d9'); text(201,948,'JIŠTĚNÍ',17)
wire([(80,962),(190,962),(190,942)]); wire([(300,942),(355,942),(355,1125)])
# Four independent parallel contact branches, all from the SAME protected L.
for y, caption, normally_closed in [(955,'K_A · COM–NC',True),(1005,'K_B · COM–NC',True),(1065,'T_HAV · sepne při vysoké teplotě',False),(1125,'S_ON · ruční trvalý oběh',False)]:
    wire([(355,y),(550,y)]); dot(355,y); dot(550,y); dot(650,y)
    wire([(550,y),(650,y if normally_closed else y-20)])
    wire([(650,y),(1000,y)]); dot(1000,y)
    text(670,y-12,caption,18)
wire([(1000,955),(1000,1125)]); wire([(1000,1005),(1090,1005)])
rect(1090,963,205,125,'#eaf4f5'); text(1110,999,'ČERPADLO',24,'bold');text(1110,1028,'L  /  N  /  PE',19)
text(80,1160,'N téhož okruhu',18); wire([(260,1155),(1190,1155),(1190,1088)],color='#3b73a5')
text(80,1192,'PE nepřerušovaný',18); wire([(280,1187),(1230,1187),(1230,1088)],color='#59823c')

rect(50,1240,635,360,'#ffffff'); text(75,1278,'Budič jedné cívky 5 V · zopakovat 2×',23,'bold')
text(470,1315,'+5 V_i',19,'bold'); wire([(490,1322),(490,1345)])
rect(470,1345,40,70,'#eaf4f5','#25686b',0); text(525,1383,'cívka K_i',18)
wire([(490,1415),(490,1443)]); dot(490,1432)
# Flyback diode path, explicitly labeled orientation.
wire([(490,1335),(620,1335),(620,1360)])
wire([(610,1360),(630,1360)])
parts.append('<path d="M620 1360 L610 1386 L630 1386 Z" fill="none" stroke="#25686b" stroke-width="2.5"/>')
wire([(620,1386),(620,1432),(490,1432)])
text(534,1455,'D: K nahoře',16)
text(530,1477,'A dole',16)
text(75,1375,'GPIO RELAY1',18); text(75,1397,'A25 / B1 / C3:4',17)
wire([(80,1420),(230,1420)]); rect(230,1408,65,24,'#ffffff','#25686b',0);text(237,1400,'100 Ω',17)
wire([(295,1420),(380,1420),(380,1465),(425,1465)]); wire([(425,1437),(425,1493)])
wire([(450,1443),(450,1454)]); wire([(450,1460),(450,1470)]); wire([(450,1476),(450,1487)])
wire([(490,1443),(450,1443)]); wire([(450,1487),(490,1487),(490,1515)])
text(335,1375,'Q: MOSFET',18);text(335,1395,'logic 3,3 V',16)
dot(345,1420); wire([(345,1420),(345,1453)]);rect(333,1453,24,40,'#ffffff','#25686b',0)
text(285,1478,'10k',17);wire([(345,1493),(345,1515),(490,1515)])
wire([(470,1515),(510,1515)]); wire([(478,1523),(502,1523)]);wire([(486,1531),(494,1531)])
text(465,1554,'GND_i',18)
text(75,1577,'Pro watchdog: před gate vložit CMD AND WD_OK.',17)

rect(710,1240,640,360,'#fff8ed','#dfb76b')
text(735,1278,'Bistabilní alternativa · ne hlavní varianta',23,'bold')
text(735,1319,'PE014F12: 12 V cívka mezi OUT1 a OUT2.',20)
text(735,1353,'Pro 12 V použít vhodný můstek, např. DRV8871.',19)
text(735,1387,'DRV8833 ze světýlek: max. 10,8 V → ne na 12 V.',19,'bold','#a84220')
text(735,1421,'RELAY1 / RELAY2 → IN1 / IN2; opačné polarity.',19)
text(735,1455,'Pulz 50 ms; přestávka ≥2,1 s; externí limit buzení.',19)
text(735,1489,'Výpadek napájení může ponechat kontakt OFF.',19,'bold')
text(735,1523,'Nezávislý havarijní termostat je stále nutný.',19)
text(735,1563,'Plné podmínky, tabulka GPIO a postup: ZAPOJENI.md',18)

text(55,1637,'Síť 230 V oddělit od SELV. Žádná část výkresu nepotvrzuje skutečný průtok ani bezpečný odvod tepla.',19,'bold')
text(55,1668,'Konečný projekt vyžaduje typ čerpadla, přesné relé, návrh ochran a přejímku. Firmware 0.1.0: pouze zkušební provoz.',18)
parts.append('</svg>')
out.write_text('\n'.join(parts), encoding='utf-8')
print(out)
