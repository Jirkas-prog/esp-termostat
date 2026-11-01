# Home Assistant / Zigbee2MQTT

Obě jednotky topí/regulují samostatně i bez HA. Tento balíček slouží k dohledům,
měření a protokolování, ne ke spínání čerpadla.

## Wi-Fi větev A

Po první USB instalaci je síť nenastavená. Na sériové konzoli zadej celý objekt
(příklad obsahuje zástupné hodnoty, ne uložené přihlašovací údaje):

```json
{"network":{"ssid":"MOJE_WIFI","password":"HESLO_WIFI","mqtt_uri":"mqtt://192.0.2.10:1883","mqtt_user":"termostat","mqtt_password":"HESLO_MQTT"}}
```

Síťové nastavení se aplikuje po restartu na stole. Wi-Fi vyžaduje WPA2 nebo vyšší,
heslo 8–63 znaků. Tato verze podporuje lokální `mqtt://`, nikoliv TLS/provisioning
certifikátu; použít důvěryhodnou oddělenou IoT síť a účet brokeru s ACL.
Kredence jsou v lokální NVS bez zapnutého šifrování flash. Nenastavuje se otevřený AP.
Přihlašovací údaje firmware nevypisuje do diagnostiky.

| MQTT téma | Obsah |
|---|---|
| `kamna/termostat/A/state` | JSON, vlastní i peer teplota, diagnostika, nastavení; QoS1, bez retain |
| `kamna/termostat/A/peer_alarm` | **samostatný kanál** `ON` / `OFF` pro poruchu dohledu B |
| `kamna/termostat/A/event` | snímek při změně poruchové masky doručitelné přes Wi-Fi |
| `kamna/termostat/A/availability` | retained LWT `offline`, po připojení `online` |
| `kamna/termostat/A/ack` | desetinné sequence, bez retain, potvrzení HA |

Připojení k AP a brokeru není totéž co doručení do HA. A čeká na odpověď s číslem
konkrétní odeslané zprávy; ignoruje retained ACK, příliš staré, opakované nebo
neshodné odpovědi. Při výpadku Wi-Fi pokračuje v lokální regulaci a B předá její
teplotu a chybu vlastní cesty přes Zigbee.

## Zigbee větev B

1. Do složky `external_converters` Zigbee2MQTT vlož `esp-termostat.mjs`.
   Novější Zigbee2MQTT může vyžadovat `enable_external_js`; postup se řídí jeho
   [aktuální dokumentací](https://www.zigbee2mqtt.io/advanced/more/external_converters.html).
2. Restartuj/reloadni Zigbee2MQTT, povol připojení nových zařízení a zapni C6.
   Zařízení se hlásí jako **ESP-TERMOSTAT-1**, výrobce `Jiri DIY`, Zigbee router.
   Existující koordinátor a síť zůstávají zachované; C6 nevytváří vlastní síť.
3. Nastav jeho `friendly_name` na **kamna_b**. Zprávy přijdou do
   `zigbee2mqtt/kamna_b`, příkazy do `zigbee2mqtt/kamna_b/set`.
4. Pro zařízení nastav **retain: false**. V globální sekci `advanced` nastav
   `cache_state_send_on_startup: false`, aby Z2M při startu nepublikovalo starou
   cache; tato volba se týká všech zařízení. Umístění voleb popisuje
   [konfigurace Zigbee2MQTT](https://www.zigbee2mqtt.io/guide/configuration/all-settings.html).
   Živost se posuzuje pomocí periodických nových zpráv; stará retained zpráva
   není důkaz života.
5. C6 používá standardní cluster teploty a vlastní cluster 0xFC10. Atribut 0
   obsahuje binární snímek 68 B; atribut 1 přijímá uint32 `delivery_ack` z HA.
   Převodník vytváří JSON obdobný Wi-Fi větvi a navíc ploché entity pro discovery.

Zigbee firmware drží párování ve svých perzistentních flash oddílech. Při neúspěšném připojení opakuje steering,
při oznámeném leave se znovu připojuje. `link` znamená členství v síti; výpadek
koordinátoru se spolehlivěji pozná absencí aplikačního ACK, nikoliv z této jediné
proměnné. **Bez fyzického testu nelze potvrdit interoperabilitu všech verzí
koordinátoru, Zigbee2MQTT a ESP Zigbee SDK.** Dodaný převodník je otestovaný na
binárních vektorech; párování a skutečný přenos zůstávají v přejímacích zkouškách.

## Instalace balíčku Home Assistant

Vlož `termostat.yaml` jako `packages/termostat.yaml`. Pokud ještě nepoužíváš balíčky,
sluč následující s existující sekcí `homeassistant` (nevytvářej duplicitní klíč):

```yaml
homeassistant:
  packages: !include_dir_named packages
```

Před restartem spusť kontrolu konfigurace HA. Balíček obsahuje čtyři nezávislé
teplotní entity (A a B přes každou cestu), dohled cesty, dohled druhé desky,
diagnostiku se všemi JSON atributy, ACK pro každou cestu a zápisy změn do Logbooku.
Home Assistant Recorder standardně uchová stavy; ověř, že nejsou vyloučené.
Žádný příkaz k čerpadlu se neposílá. Soubor se do tvého HA automaticky neinstaloval.

Při funkčním kabelu a výpadku Zigbee bude v A zprávě `peer.ha_delivery=false`;
při výpadku Wi-Fi bude naopak B hlásit stejnou vlastnost svého peeru.
To znamená poruchu celé doručovací cesty (rádio, AP, koordinátor, Z2M, broker,
HA nebo ACK automatizace); **nelze tím samotným určit, že fyzicky selhalo rádio**.
Při ztrátě UART se hlásí „peer/deska nebo kabel nedostupný“, nikoliv nepodložené
tvrzení, že je procesor mrtvý.

Po prvním startu se OwnRoute/PeerRoute zobrazují, dokud neprojde ACK z HA. Sada
ACK automatizací je proto součástí instalace. Výpadek HA nezpůsobí zastavení oběhu.
Při výpadku obou rádiových cest nelze alarm poslat do HA; právě proto je vhodný
nezávislý místní alarm a hardwarová ochrana. Obě rádiové sítě navíc používají 2,4 GHz.

Pro údržbu kontroluj nejen aktuální masku, ale i `fault_events`, `last_fault`
a `boot` v diagnostických atributech. Počítadlo indikuje i přechod, který se během
výpadku nestihl živě zalogovat. Nejde o trvalý kompletní offline žurnál.
