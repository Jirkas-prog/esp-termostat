# Zapojení — revize 1, 29. 10. 2026

[Otevřít přehledový výkres](ZAPOJENI.svg). Výkres obsahuje konkrétní nízkonapěťové
signály a princip silových větví. Silová část není výrobní dokumentace rozvaděče:
chybí přesný model 5V relé, parametry čerpadla, jištění a požadavky výrobce kamen.
Finální silové zapojení a dimenzování musí ověřit elektrikář. Během vývoje používat
zkušební zátěž na bezpečném nízkém napětí.

## 1. Dva samostatné řídicí kanály

Každý kanál: vlastní 5V zdroj → deska ESP32 → vlastní sonda → vlastní budič relé.
Napájecí výstupy 5V zdrojů **nepropojovat**. Každá větev má své GND a 3,3 V.
Napájení cívky nebrat z GPIO ani z 3,3V regulátoru desky. USB a externí 5V napájení
současně používat pouze s ověřeným oddělením proti zpětnému napájení USB.

UART odděluje dvoukanálový digitální izolátor s jedním kanálem každým směrem,
např. **TI ISO7721 (default HIGH, nikoliv varianta F s default LOW)**, na obou
stranách napájený z příslušných 3,3 V. Zvolit pouzdro a modul s doloženým pinoutem.
Na izolátoru se GND_A a GND_B nespojují. Výchozí HIGH zachová klidovou úroveň UART;
porucha/ticho se stejně zjistí časovým limitem a zpětným echo rámcem.

Pro krátké spojení v jedné skříni:

| Strana A | Směr přes izolátor | Strana B |
|---|---|---|
| GPIO21 TX | A → B | GPIO17 RX (XIAO D7) |
| GPIO22 RX | A ← B | GPIO16 TX (XIAO D6) |
| 3V3_A / GND_A | napájení levé strany | žádný přímý vodič na GND_B |
| žádný přímý vodič na 3V3_B | napájení pravé strany | 3V3_B / GND_B |

Obě strany blokovat 100 nF u napájecích pinů. Pro delší kabel mimo skříň použít
vhodný **galvanicky oddělený plně duplexní RS-422** převod UART a jeho zakončení;
nejde o firmware pro polo-duplexní RS-485 s automaticky předpokládaným DE.
Přímý společný GND UART je možný pro test, ale snižuje oddělení napájecích poruch.

## 2. Konkrétní GPIO

Čísla jsou **GPIO čipu**, nikoliv pořadí vývodů konektoru. U T-Display se používá
původní deska, nikoliv T-Display-S3. Před montáží ověř potisk svého kusu.

| Funkce | T-Display A | XIAO C6 B | C3-DevKitM-1 A (alternativa) |
|---|---:|---:|---:|
| DS18B20 DATA | 27 | 0 / D0 | 3 |
| Relé IN / H-můstek IN1 | 25 | 1 / D1 | 4 |
| H-můstek IN2 | 26 | 2 / D2 | 5 |
| H-můstek nSLEEP / pulzní EN | 17 | 21 / D3 | 6 |
| UART TX k druhé desce | 21 | 16 / D6 | 20 |
| UART RX od druhé desky | 22 | 17 / D7 | 21 |
| Externí watchdog WDI | 13 | 18 / D10 | 7 |
| TTP224 Q1 zámek | 32 | — | — |
| TTP224 Q2 − | 33 | — | — |
| TTP224 Q3 + | 36 | — | — |
| TTP224 Q4 uložit | 39 | — | — |

LCD T-Display je již připojený: MOSI19, CLK18, CS5, DC16, RESET23, BL4.
GPIO34 měří napájení desky; GPIO35/0 jsou vestavěná tlačítka a nejsou použita
pro TTP224. GPIO3 a GPIO14 na XIAO C6 patří RF přepínači; firmware je obsluhuje.
U C3 je konzole přes nativní USB Serial/JTAG, GPIO20/21 jsou vyhrazené druhé desce.

## 3. DS18B20 — každá sonda jen k jedné desce

- VCC červená → **3,3 V příslušné desky**; GND černá → její GND.
- DATA žlutá → GPIO DATA z tabulky. Barvy na skutečné sondě ověřit.
- DATA → pull-up přibližně 4,7 kΩ → stejné 3,3 V. Adaptér už může odpor obsahovat;
  ověřit osazení a zbytečně nezdvojovat. Žádný pull-up na 5 V.
- Použít všechny tři vodiče; parazitní napájení není podporováno.
- DQ vést krátce, mimo spínané silové kabely. 100 nF u adaptéru VCC–GND.
- Firmware dovoluje přesně jedno čidlo na sběrnici, kontroluje CRC, připoutá ROM
  identitu a vyžaduje tři platná měření po chybě. Výchozí hodnota 85 °C po resetu
  není přijata bez dokončené konverze a platné provozní značky v TH/TL.

Čip má rozsah −55 až +125 °C; samotný údaj „vodotěsný“ **nepotvrzuje** teplotní,
tlakovou ani dlouhodobou odolnost kabelu, těsnění a sondy v konkrétním výměníku.
Použít vhodnou jímku a doložené limity kompletní sondy. Sonda do vody není sonda
pro měření plamene, spalin nebo povrchu kamen o stovkách stupňů.

## 4. Preferované klasické 5V relé

Na každé desce samostatně, pro holé relé nebo elektricky ekvivalentní budič:

```text
+5V_i ---- cívka K_i ---- drain Q_i (logic-level N-MOSFET)
  |                       source ---- GND_i
  +----|<|-----------------+
       D_i: katoda +5 V, anoda drain

GPIO RELAY1 ---- 100 ohm ---- gate Q_i
                              |
                             10k
                              |
                             GND_i
```

Q musí mít doložený nízký RDS(on) při 2,5–3,3 V, správné VDS a rezervu pro proud
cívky. Ochranná dioda musí snést proud cívky a její energii; přesný typ vybrat podle
relé. Pro rychlé odpadnutí může projektant zvolit diodu se Zenerovou svorkou místo
samotné diody. Přidat lokální filtraci 5 V (např. 100 nF + vhodná kapacita podle
zdroje a měření). Nezaměnit ochrannou diodu cívky s odrušením kontaktů pro motor.

V režimu `relay_mode=0` (výchozí): HIGH → cívka přitažena → NC rozpojen → tato
větev čerpadlo nenapájí. LOW nebo neaktivní GPIO → cívka odpadlá → NC sepnut.
Oba NC kontakty jsou **paralelně**: čerpadlo stojí jen tehdy, když obě desky
vědomě přitáhnou své cívky. Režim `1` používá NO kontakt; ztráta napájení pak
čerpadlo přes danou větev nespustí a nedoporučujeme jej pro tuto instalaci.

Běžné hotové „Arduino reléové moduly 5 V“ bývají aktivní LOW a jejich optočlen
nemusí znamenat galvanické oddělení. **Bez ověření nejsou záměnou za tento budič.**
Klasické relé znamená napětí cívky 5 V, nikoliv povolení 5V logiky na ESP32.

## 5. Silový princip (stejný chráněný okruh)

```text
L z UPS -- jištění --+-- COM K_A / NC --+-- L čerpadla
                    +-- COM K_B / NC --+
                    +-- havarijní T ---+   T sepne při vysoké teplotě
                    +-- ruční ON -----+   AUTO / trvalý ON, žádné vzdálené OFF
N z téhož okruhu -------------------------- N čerpadla
PE --------------------------------------- PE čerpadla (nepřerušovaný)
```

Kontakty paralelně **nezvyšují garantovaný proud**: každé relé musí samostatně
zvládnout celý motorový proud i rozběh. Označení 5/10 A pro odporovou zátěž
negarantuje vhodnost pro motor ani elektronické oběhové čerpadlo s nárazovým
proudem. Oba COM musí pocházet ze stejného jištěného okruhu a fáze; nikdy z různých
zdrojů/fází. Žádné propojení 230 V s GND. Havarijní kontakt a ruční bypass také
musí odpovídat motorové zátěži. Společný stykač vložený za obě relé je další
společný bod poruchy, který musí návrh zohlednit.

Externí watchdog má po ztrátě WDI vypnout cívku NC relé hardwarově, např. přes
hradlo `CMD AND WD_OK` před MOSFETem. WDI mění stav každých 100 ms až po regulačním
kroku. Zvolit přibližně sekundový timeout a ověřit chování při startu, brownoutu,
zkratu WDI a trvale HIGH/LOW. Firmware samotný nedokáže emulovat tento nezávislý
hardware. Watchdog resetu nenahradí nezávislý termostat ani odvod tepla.

## 6. Alternativa: bistabilní PE014F12

**PE014F12 má jednu polarizovanou 12V cívku, není to 5V relé.** Výrobce uvádí
650 Ω, 222 mW, minimální impulz 20 ms a zatěžovatel <10 %. Výchozí firmware
používá 50 ms, povoluje 20–200 ms a nejméně 2,1 s mezi impulzy. Při startu pošle
SET/ON, protože polohu bez pomocného kontaktu nezná. Zda kontakt opravdu přepnul,
nedokáže potvrdit. Při resetu/výpadku během pulzu zůstává výsledek neznámý.

DRV8833 je referenční můstek ze světýlek a má napájení **2,7–10,8 V**. Není
vhodný pro připojení 12 V. Ani napájení PE014F12 sníženým napětím není spolehlivá
náhrada správného budiče v celém teplotním rozsahu. Pro PE014F12 lze zvolit např.
**DRV8871** (provoz 6,5–45 V, IN1/IN2 kompatibilní s 3,3V logikou), 12V zdroj a
ověřený modul; konkrétní ILIM, blokování a ochrany osadit dle datasheetu.

| Signál | DRV8833 pro vhodné nízkonapěťové relé | DRV8871 pro PE014F12 |
|---|---|---|
| RELAY1 | AIN1 | IN1 |
| RELAY2 | AIN2 | IN2 |
| RELAY_ENABLE | nSLEEP | externí hardwarová pulzní brána; DRV8871 nemá nSLEEP pin |
| Výstupy | AOUT1/AOUT2 mezi cívku | OUT1/OUT2 mezi A1/A2 cívky |
| Napájení | podle cívky, max. 10,8 V | 12 V, zvlášť měnič 5 V pro desku |

IN1=1, IN2=0 je ON; opačná polarita OFF; obě nuly odpojí buzení. U PE014F12
varianta F: kladné A1 / záporné A2 znamená SET, opačně RESET. COM–NO pak použít
jako větev ON. Orientaci a číslování pinů ověřit podle **pohledu zespodu**
v datasheetu, nikoliv podle domnělého pořadí pinů hotového modulu.

Na vstupy 10k pull-down. Cívka patří **mezi oba výstupy**, nikoliv proti GND.
Přes obousměrně buzenou cívku se **nesmí připojit obyčejná jednosměrná ochranná
dioda** jako u monostabilního relé. Použít ochrany H-můstku dle výrobce.
Hardwarový monostabilní omezovač má nezávisle omezit buzení; softwarový časovač
nepokrývá poruchu CPU/hodin. Bistabilní větev musí mít nezávislý termostat,
protože ztráta 12 V při OFF nepřejde sama do ON.

## 7. TTP224 a tištěné pouzdro

TTP224 napájet 3,3 V, Q1–Q4 zapojit dle tabulky, každý vstup doplnit 10k pull-down
do GND_A. GPIO36/39 žádné interní pull-down nemají. Nastavit **momentové výstupy,
aktivní HIGH, nikoliv toggle**. S modulem v jiném režimu není ovládání kompatibilní.
Výrobce modulu uvádí odezvu 60–200 ms; firmware přidává 60ms filtr.

Na čelo vytisknout zámek, −, + a uložit. Tloušťku nevodivé stěny nad ploškami
ověřit prakticky s konkrétním materiálem a vlhkostí; nezačínat hotovou silnou stěnou
bez zkoušky. Nepoužívat vodivý filament nad elektrodami. Desku a kabely pevně
uchytit, chránit před kondenzací a dotykovým modulem neoddělovat síťovou část.
3D výtisk sám o sobě není certifikované elektrické/požární pouzdro. Síťové svorky
a jištění patří do odpovídající oddělené krabice.
