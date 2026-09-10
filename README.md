# 💧 Gartenbewässerung

**Automatische Tanksteuerung für 2× IBC-Container (1000 L) — Flo's Garten**

Füllstandsüberwachung mit Web-Dashboard, automatischer Umschaltung zwischen
zwei IBC-Tanks, Schnittstelle zum Drip-Bewässerungssystem und Anbindung des
Nous L14 (Zigbee-Ventil).

📄 Live-Demo (statisch, mit simulierten Daten): [`docs/demo.html`](docs/demo.html) —
zum Ausprobieren im Browser öffnen oder per GitHub Pages hosten (siehe unten).

📋 Änderungshistorie: [`CHANGELOG.md`](CHANGELOG.md)

## Funktionsprinzip

- Beide Tanks werden per Ultraschallsensor kontinuierlich gemessen.
- Ein Tank ist immer "aktiv" (Ventil offen), der andere steht auf Standby (Ventil zu).
- Fällt der aktive Tank unter die Schwelle (Standard 10 %), schaltet die Steuerung
  automatisch auf den anderen Tank um – vorausgesetzt, dieser hat genug Wasser.
- Sind beide Tanks leer, schließen beide Ventile (Trockenlaufschutz) und das
  Dashboard zeigt einen Alarm.
- Über das Web-Dashboard lässt sich der Automatikmodus deaktivieren und der
  aktive Tank manuell wählen.

## Repository-Struktur

```
gartenbewaesserung/
├── README.md              – diese Datei
├── CHANGELOG.md           – Änderungshistorie
├── LICENSE
├── firmware/
│   ├── bewaesserung_controller.ino   – ESP32-Sketch (Arduino IDE)
│   └── config.h                      – Pins, WLAN, Schwellwerte, MQTT
├── data/
│   └── index.html                    – Web-Dashboard, wird per LittleFS auf den ESP32 geladen
└── docs/
    └── demo.html                     – eigenständige Live-Demo mit simulierten Daten (auch für GitHub Pages)
```

## 1. Hardware – Stückliste (BOM)

| Menge | Bauteil | Zweck | Hinweis |
|---|---|---|---|
| 1 | ESP32 DevKit (z. B. ESP32-WROOM-32) | Steuerung, WLAN, Webserver | 5V per USB-Netzteil |
| 2 | Ultraschallsensor **JSN-SR04T** (wasserdicht, mit Sensorkopf am Kabel) | Füllstandsmessung, im Deckel/Tankoben montiert, berührungslos | Alternativ: Drucksensor (Tauchsonde) 0–1 bar, falls Sensor im Wasser hängen darf |
| 2 | 12V/24V-**Magnetventil**, NC (stromlos geschlossen), passend zum Rohr-/Schlauchdurchmesser | Freigabe/Sperrung des jeweiligen Tankauslaufs | Für drucklosen Gravitationsablauf reichen einfache Kunststoff-Magnetventile |
| 1 | 2-Kanal-Relaismodul (5V-Logik, potentialgetrennt) | Schaltet die Magnetventile | Optokoppler-Modul bevorzugen |
| 1 | Netzteil 12V/24V (passend zu den Ventilen), ausreichend Strom (Anlaufstrom beachten) | Versorgung der Ventile | |
| 1 | Spannungswandler / Netzteil 5V (z. B. USB-Netzteil oder Buck-Konverter ab 12V) | Versorgung ESP32 | |
| 1 | Wetterfestes Gehäuse (IP65) | Für ESP32 + Relais | z. B. Hutschienengehäuse |
| – | Kabel, Kabelverschraubungen, Klemmen, Schrumpfschlauch | Verkabelung | Außenbereich = UV-/wetterbeständige Leitungen |
| optional | Durchflusssensor (z. B. YF-S201) | Verbrauchsmessung | im Code vorbereitbar, aktuell nicht implementiert |
| optional | 5A-Sicherung / Verpolschutzdiode je Ventilstromkreis | Schutz | empfehlenswert |
| 1 | **Nous L14** – Zigbee-Bewässerungsventil mit Durchflusssensor, IP66, batteriebetrieben | Eigentliches Ventil an der Tropfleitung, öffnet/schließt den Wasserfluss zu den Trippern | Schraubt direkt auf den Schlauchanschluss (Gardena-/GHT-Gewinde) am Sammelpunkt nach den beiden Tankventilen |
| 1 | Tuya-kompatibler Zigbee-Koordinator (z. B. Nous E1/E7 oder ein generischer Zigbee-Stick) | Bindet das L14 per Zigbee 3.0 ein | Wird von Zigbee2MQTT bzw. der Nous/Tuya-App benötigt |
| 1 | MQTT-Broker (z. B. Mosquitto, oft Teil einer Home-Assistant-Installation) + Zigbee2MQTT | Lokale, cloudfreie Brücke zwischen L14 und ESP32 | Läuft z. B. auf einem Raspberry Pi im gleichen Netzwerk |

> **Wichtig:** Falls die Bewässerung mit einer Pumpe arbeitet statt rein
> gravitationsbetrieben zu sein, zusätzlich ein Pumpenrelais vorsehen und die
> Pumpe im Alarmfall (beide Tanks leer) zwingend mit abschalten
> (Trockenlaufschutz) – dafür ist in der Firmware bereits die Alarm-Logik
> vorhanden, die Pumpe müsste nur noch an ein drittes Relais angeschlossen
> und in `applyValveStates()` mit berücksichtigt werden.

## 2. Verkabelung

### Ultraschallsensoren (JSN-SR04T)

| Sensor | ESP32 Pin |
|---|---|
| Tank 1 – TRIG | GPIO 5 |
| Tank 1 – ECHO | GPIO 18 |
| Tank 2 – TRIG | GPIO 19 |
| Tank 2 – ECHO | GPIO 21 |
| VCC | 5V |
| GND | GND |

Montage: Sensorkopf mittig im Deckel des IBC, senkrecht nach unten zeigend,
mit etwas Abstand zu Kanten/Streben (Echo darf nicht an der Wand reflektieren).
Der Abstand Sensor→Wasseroberfläche bei vollem Tank (`SENSOR_BLIND_CM`) und
Sensor→Boden bei leerem Tank (`TANK_HEIGHT_CM`) müssen einmalig ausgemessen
und in `config.h` eingetragen werden.

### Relais → Magnetventile

| Relais-Kanal | ESP32 Pin | Schaltet |
|---|---|---|
| Kanal 1 | GPIO 25 | Magnetventil Tank 1 |
| Kanal 2 | GPIO 26 | Magnetventil Tank 2 |

Die Ventile hängen am separaten 12V/24V-Kreis, der Relaisausgang schaltet
nur die Versorgung des Ventils – **nicht** direkt am ESP32 anschließen.

### Status-LED

GPIO 2 → LED (mit Vorwiderstand) → GND. Leuchtet dauerhaft im Alarmfall
(beide Tanks leer).

### Blockschaltbild

```
        ┌───────────────┐        ┌──────────────┐
 Tank 1 │ JSN-SR04T      │        │  Magnetventil │──── zur Bewässerung
        └──────┬────────┘        └──────┬───────┘
               │ TRIG/ECHO               │ 12V (über Relais 1)
               │                         │
        ┌──────▼─────────────────────────▼───────┐
        │                ESP32                     │──WLAN──► Web-Dashboard
        │   (Messung, Umschalt-Logik, Webserver)    │
        └──────┬─────────────────────────┬───────┘
               │ TRIG/ECHO               │ 12V (über Relais 2)
        ┌──────▼────────┐        ┌──────▼───────┐
 Tank 2 │ JSN-SR04T      │        │  Magnetventil │──── zur Bewässerung
        └───────────────┘        └──────────────┘
```

Der ESP32 öffnet die Ventile dabei nur, wenn zusätzlich eine Wasseranforderung
vom Drip-System vorliegt (REST-API `/api/demand` oder Trockenkontakt an
GPIO27/33, siehe Abschnitt 5).

## 3. Software-Installation

1. **Arduino IDE** vorbereiten:
   - ESP32-Boardverwaltung installieren (Boards Manager URL:
     `https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json`)
   - Libraries **ArduinoJson** (Benoit Blanchon) und **PubSubClient** (Nick O'Leary)
     über den Bibliotheksverwalter installieren. `HTTPClient`, `WiFiClientSecure`
     und `time.h` (für Push-Benachrichtigungen, NTP) sind bereits Teil des
     ESP32-Boardpakets und müssen nicht separat installiert werden.
2. `firmware/config.h` anpassen:
   - WLAN-Zugangsdaten eintragen
   - Pins ggf. an eigene Verdrahtung anpassen
   - Tankmaße (`TANK_HEIGHT_CM`, `SENSOR_BLIND_CM`) nach dem Ausmessen eintragen
   - Optional: `NOTIFY_PROVIDER` und die zugehörigen Zugangsdaten für
     Push-Benachrichtigungen setzen (siehe Abschnitt 7)
3. **Dateisystem-Upload**: Das "ESP32 Sketch Data Upload"-Plugin (bzw. bei neueren
   IDE-Versionen den LittleFS-Uploader) installieren und den Ordner `data/`
   (enthält `index.html`) auf den ESP32 hochladen.
4. `firmware/bewaesserung_controller.ino` auf den ESP32 flashen.
5. Seriellen Monitor öffnen (115200 Baud) – dort erscheint die IP-Adresse des ESP32.
6. Im Browser `http://<IP-Adresse>` öffnen (oder `http://bewaesserung.local`,
   falls mDNS im Router funktioniert) → Dashboard erscheint.

## 4. Kalibrierung

1. Tank leer: Abstand Sensor→Boden messen → `TANK_HEIGHT_CM`.
2. Tank randvoll: Abstand Sensor→Wasseroberfläche messen → `SENSOR_BLIND_CM`.
3. Optional zwischendurch mit bekannter Literzahl (z. B. per Wasseruhr) die
   Anzeige im Dashboard gegenprüfen, da ein IBC unten am Palettensockel nicht
   exakt linear ist – für den Praxiseinsatz (Bewässerung) ist die lineare
   Näherung aber ausreichend genau.
4. Schwellwerte (`SWITCH_LOW_THRESHOLD_PCT`, `MIN_LEVEL_TO_ACTIVATE_PCT`)
   lassen sich zusätzlich zur Laufzeit über `POST /api/config` ändern,
   ohne neu zu flashen.

## 5. Schnittstelle zum Drip-/Tropfbewässerungssystem

Die Tanksteuerung öffnet das Versorgungsventil des aktiven Tanks **nur, wenn
das Drip-System tatsächlich Wasser anfordert** – im Ruhezustand bleiben beide
Ventile geschlossen, damit die Leitung nicht dauerhaft unter Druck steht.
Dafür gibt es zwei gleichzeitig nutzbare Wege:

**Variante A – REST-API (empfohlen, wenn der Drip-Controller WLAN kann):**
Der Drip-Controller ruft beim Start eines Bewässerungszyklus
`POST /api/demand {"active": true}` auf und meldet sich bei längeren Zyklen
regelmäßig erneut (Heartbeat). Ist der Zyklus fertig, sendet er
`{"active": false}`. Vor dem Start kann er per `GET /api/demand` prüfen, ob
überhaupt Wasser verfügbar ist, und den Zyklus ggf. überspringen/verschieben.

**Variante B – einfacher Trockenkontakt (falls der Drip-Controller kein WLAN hat):**
Ein digitaler Ausgang des Drip-Controllers (z. B. ein Relais eines
klassischen Bewässerungscomputers) wird auf `GPIO27` (`DEMAND_INPUT_PIN`)
gelegt und bei Bedarf auf HIGH gezogen. Umgekehrt gibt `GPIO33`
(`SUPPLY_READY_OUTPUT_PIN`) ein Freigabesignal zurück, sobald Wasser
verfügbar ist – das kann der Drip-Controller z. B. als Freigabebedingung
für seine eigenen Zonenventile nutzen.

Beide Varianten sind gleichzeitig aktiv (ODER-Verknüpfung); welche man
braucht, hängt vom eingesetzten Drip-Controller ab.

**Sicherheits-Timeout:** Kommt über keinen der beiden Wege innerhalb von
`DEMAND_TIMEOUT_MS` (Standard 5 Minuten) eine erneute Anforderung, wird die
Anforderung automatisch zurückgesetzt und das Ventil geschlossen – Schutz bei
Verbindungsabbruch oder Absturz des Drip-Controllers.

### Zusätzliche Verkabelung für die Schnittstelle

| Signal | ESP32 Pin | Richtung |
|---|---|---|
| Wasseranforderung (Trockenkontakt) | GPIO 27 | Eingang, vom Drip-Controller |
| Versorgung verfügbar (Freigabesignal) | GPIO 33 | Ausgang, zum Drip-Controller |

## 6. Anbindung des Nous L14 (Zigbee-Ventil an der Tropfleitung)

Als konkretes Ventil an der Tropfleitung selbst kommt das **Nous L14** zum
Einsatz – ein batteriebetriebenes Zigbee-3.0-Ventil (Tuya-Protokoll, IP66),
das direkt auf den Schlauchanschluss geschraubt wird und Zeitpläne/manuelle
Steuerung über die Nous- bzw. Tuya-Smart-Life-App oder einen Zigbee-Hub
unterstützt.

**Wichtige Eigenschaft:** Das L14 spricht **Zigbee**, nicht WLAN – der ESP32
kann es nicht direkt ansprechen. Es wird daher über den ohnehin für
Zigbee2MQTT benötigten **MQTT-Broker** eingebunden:

1. L14 per Zigbee2MQTT mit einem Tuya-kompatiblen Zigbee-Koordinator koppeln
   (z. B. Nous E1/E7-Hub oder ein generischer Zigbee-Stick an einem
   Raspberry Pi) und einen `friendly_name` vergeben, z. B. `drip_ventil`.
2. In `config.h` unter `MQTT_BROKER_HOST`, `MQTT_L14_FRIENDLY_NAME` etc. die
   eigenen Werte eintragen.
3. Der ESP32 abonniert automatisch `zigbee2mqtt/drip_ventil` und wertet die
   von Zigbee2MQTT veröffentlichten Statusänderungen aus: Meldet das L14
   `{"state":"ON"}` (z. B. weil ein Zeitplan in Zigbee2MQTT/Home Assistant
   angesprungen ist oder jemand die App genutzt hat), wertet die
   Tanksteuerung das exakt wie eine Anforderung über `/api/demand` und
   öffnet das passende Tankventil. `{"state":"OFF"}` beendet die Anforderung.
4. **Sicherheits-Verbund:** Fällt die Tanksteuerung in den Alarmzustand
   (beide Tanks leer), schickt der ESP32 zusätzlich aktiv
   `{"state":"OFF"}` an `zigbee2mqtt/drip_ventil/set`, um das L14
   zwangsweise mitzuschließen.

> **Hinweis:** Der Community-Support des L14 in Zigbee2MQTT ist noch relativ
> neu; stelle sicher, dass Zigbee2MQTT aktuell ist und das Gerät nach dem
> Pairing tatsächlich die Eigenschaft `state` (ON/OFF) exponiert (in der
> Zigbee2MQTT-Oberfläche unter "Exposes" prüfbar), bevor du dich auf die
> Kopplung verlässt. Die vom Hersteller beworbene Durchflussmessung ist je
> nach Zigbee2MQTT-Version aktuell u. U. noch nicht mit ausgelesen.

**Alternative ohne eigenen MQTT-Code:** Wer ohnehin Home Assistant nutzt,
kann statt der MQTT-Anbindung im ESP32 auch eine simple Home-Assistant-
Automatisierung bauen ("wenn L14 → ON, rufe `POST /api/demand
{"active":true}` beim ESP32 auf") und `USE_MQTT_DRIP_VALVE` in `config.h`
auf `false` setzen.

Benötigte zusätzliche Library: **PubSubClient** (Nick O'Leary) über den
Arduino-Bibliotheksverwalter installieren.

## 7. Dashboard: Verlauf, Push-Alarm und Verbindungsstatus

**Verlaufsdiagramm.** Der ESP32 legt alle `HISTORY_INTERVAL_MS` (Standard
15 Min.) einen Messpunkt in einem Ringpuffer im RAM ab (Standard 288 Punkte
= 3 Tage, in `config.h` änderbar) und stellt ihn über `GET /api/history`
bereit. Das Dashboard (`data/index.html`) lädt diesen Verlauf per
**Chart.js** (von cdnjs.cloudflare.com, benötigt also Internetzugang im
WLAN) und zeichnet die Füllstände beider Tanks über die Zeit. Für echte
Zeitstempel synchronisiert der ESP32 beim Start per NTP die Uhrzeit
(`NTP_SERVER`, `GMT_OFFSET_SEC` in `config.h`); ohne Internetzugang bleiben
die Zeitstempel auf 0 stehen, die Werte selbst sind davon nicht betroffen.
Da der Puffer nur im RAM liegt, geht der Verlauf bei einem Neustart
verloren (kein Flash-Verschleiß) – für dauerhafte Historie böte sich später
z. B. InfluxDB/Grafana per MQTT an (siehe Erweiterungen).

**Push-Benachrichtigung bei Alarm.** Sobald beide Tanks leer sind, schickt
der ESP32 automatisch eine Push-Nachricht und danach alle
`ALARM_RENOTIFY_INTERVAL_MS` (Standard 2h) eine Erinnerung, solange der
Alarm anhält. Drei Anbieter stehen wahlweise zur Verfügung
(`NOTIFY_PROVIDER` in `config.h`):

| Wert | Anbieter | Einrichtung |
|---|---|---|
| `0` | aus | – |
| `1` (Standard) | **ntfy.sh** | Kein Account nötig: App installieren, eigenen (schwer erratbaren) Topic-Namen in `NTFY_TOPIC` eintragen und in der App abonnieren. Auch selbst hostbar. |
| `2` | **Pushover** | Account + App-Token unter pushover.net anlegen, `PUSHOVER_API_TOKEN`/`PUSHOVER_USER_KEY` eintragen (einmalige Kosten für die App). |
| `3` | **Telegram** | Bot über [@BotFather](https://t.me/BotFather) anlegen, Token + eigene Chat-ID in `TELEGRAM_BOT_TOKEN`/`TELEGRAM_CHAT_ID` eintragen. |

> **Sicherheitshinweis:** Die HTTPS-Aufrufe nutzen aus Einfachheitsgründen
> `setInsecure()` (keine Zertifikatsprüfung) – für die meisten Hobby-
> Installationen ausreichend, für höhere Sicherheitsanforderungen stattdessen
> das jeweilige Root-CA-Zertifikat mit `setCACert()` hinterlegen.

**Verbindungsstatus.** `GET /api/status` liefert zusätzlich:
- `mqttConnected` – ist die Verbindung zum MQTT-Broker (und damit die
  Kopplung zum Nous L14) gerade aktiv?
- `wifiRssi` – aktuelle WLAN-Signalstärke in dBm
- je Tank `sensorOk` (bool) und `lastReadAgoSec` – wie viele Sekunden liegt
  die letzte gültige Sensormessung zurück? Bleibt ein Sensor länger als
  `SENSOR_STALE_MS` (Standard ca. 24 Sekunden) ohne gültige Messung, gilt er
  als gestört und das Dashboard zeigt eine Warnung, statt einfach den alten
  Wert unkommentiert weiter anzuzeigen.

Das Dashboard stellt beides als kleine Statuszeile mit farbigen Punkten dar
(🟢 ok / 🟡 Sensor veraltet / 🔴 keine Verbindung).

## 8. REST-API (für eigene Erweiterungen / Home Assistant etc.)

| Endpoint | Methode | Beschreibung |
|---|---|---|
| `/api/status` | GET | Füllstände, aktiver Tank, Ventilzustände, Alarm, Anforderungsstatus, MQTT-/Sensor-/WLAN-Status |
| `/api/history` | GET | Verlauf der Füllstände (Ringpuffer, siehe Abschnitt 7) |
| `/api/mode` | POST `{"auto": true|false}` | Automatikmodus umschalten |
| `/api/valve` | POST `{"tank": 0|1}` | Manuell aktiven Tank wählen (nur bei `auto:false`) |
| `/api/config` | POST `{"lowThresholdPct": ..., "minActivatePct": ...}` | Schwellwerte ändern (persistent) |
| `/api/demand` | GET | Anforderungs-/Versorgungsstatus fürs Drip-System (`demand`, `supplyAvailable`, `activeTank`, `alarm`) |
| `/api/demand` | POST `{"active": true|false}` | Wasseranforderung vom Drip-System setzen (Heartbeat bei längeren Zyklen wiederholen) |

## 9. Sicherheitshinweise

- Alle 12/24V-Ventilstromkreise galvanisch vom ESP32-Logikteil trennen
  (Optokoppler-Relais verwenden).
- Außenverkabelung IP65-tauglich ausführen, Steckverbindungen vor Regen schützen.
- Für den Fall eines WLAN-/Systemausfalls schalten die Relais standardmäßig
  in den "aus"-Zustand (NC-Ventile schließen dann automatisch) – das
  verhindert unkontrolliertes Auslaufen.
- Bei Nutzung mit einer Pumpe: Trockenlaufschutz unbedingt mit einbauen
  (siehe Hinweis oben).

## 10. Mögliche Erweiterungen

- Zustand nach Neustart (zuletzt aktiver Tank) in `Preferences` persistieren
- Durchflusssensor für Verbrauchsstatistik / Leckage-Erkennung
- Langzeit-Historie extern speichern (z. B. InfluxDB/Grafana per MQTT), statt nur im RAM-Ringpuffer
- Zusätzliche Regensensor-/Bodenfeuchte-Integration zur Bewässerungssteuerung
- HTTP Basic-Auth oder Access-Token für den Webserver, da aktuell offen im lokalen Netz

## 11. Live-Demo per GitHub Pages hosten

Die Datei `docs/demo.html` läuft komplett im Browser (simulierte Daten, kein
Backend nötig) und lässt sich direkt über GitHub Pages veröffentlichen:

1. Im GitHub-Repo unter **Settings → Pages** als Quelle „Deploy from a
   branch" wählen, Branch `main` und Ordner `/docs` auswählen.
2. Nach kurzer Zeit ist die Demo unter
   `https://<dein-github-name>.github.io/<repo-name>/demo.html` erreichbar.
