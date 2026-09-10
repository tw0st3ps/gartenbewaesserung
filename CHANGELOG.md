# Changelog

Alle nennenswerten Änderungen an diesem Projekt werden hier dokumentiert.
Format angelehnt an [Keep a Changelog](https://keepachangelog.com/de/1.0.0/).

## [1.4.0] – 2026-09-10

### Hinzugefügt
- **Verlaufsdiagramm** der Füllstände: ESP32 speichert alle 15 Minuten einen
  Messpunkt in einem RAM-Ringpuffer (Standard 3 Tage), neuer Endpoint
  `GET /api/history`. NTP-Zeitsynchronisation beim Start für echte
  Zeitstempel. Anzeige als Liniendiagramm (Chart.js) im echten Dashboard
  und – simuliert – in der Live-Demo.
- **Push-Benachrichtigung bei Alarm**: Wahlweise ntfy.sh (Standard, ohne
  Account), Pushover oder Telegram, konfigurierbar über `NOTIFY_PROVIDER`
  in `config.h`. Erstbenachrichtigung sofort, danach Erinnerung alle 2h,
  solange der Alarm („beide Tanks leer") anhält.
- **Verbindungsstatus im Dashboard**: `/api/status` liefert zusätzlich
  MQTT-Verbindungsstatus, WLAN-Signalstärke sowie je Tank, ob der letzte
  Sensormesswert noch aktuell ist (`sensorOk`, `lastReadAgoSec`). Anzeige
  als Statuszeile mit farbigen Punkten (🟢/🟡/🔴) in Dashboard und Demo;
  die Demo hat zusätzlich Buttons zum Simulieren von MQTT-/Sensorausfall.
- `CHANGELOG.md` angelegt.

### Geändert
- README um Abschnitt 7 „Dashboard: Verlauf, Push-Alarm und
  Verbindungsstatus" sowie die REST-API-Tabelle (`/api/history`) erweitert.

## [1.3.0] – 2026-09-09

### Hinzugefügt
- Anbindung des **Nous L14** (Zigbee-Bewässerungsventil an der
  Tropfleitung) per MQTT/Zigbee2MQTT: ESP32 abonniert den L14-Zustand und
  wertet „ON" als Wasseranforderung; im Alarmfall wird das L14 zusätzlich
  aktiv per MQTT zwangsgeschlossen (Sicherheits-Verbund).
- README-Abschnitt zur Einrichtung (Zigbee-Koordinator, Zigbee2MQTT,
  Broker-Konfiguration) inkl. Hinweis zum noch jungen Community-Support
  des L14 in Zigbee2MQTT.
- Library-Abhängigkeit **PubSubClient** ergänzt.

### Geändert
- Demo-Webseite: Drip-Karte und Hardware-Schema benennen jetzt explizit
  das Nous L14 statt eines generischen Drip-Controllers.

## [1.2.0] – 2026-09-09

### Hinzugefügt
- **Schnittstelle zum Drip-/Tropfbewässerungssystem**: Das Versorgungsventil
  des aktiven Tanks öffnet nur noch bei aktiver Wasseranforderung, nicht
  mehr dauerhaft. Zwei Wege dafür: REST-API (`GET/POST /api/demand`) und
  optionaler Hardware-Trockenkontakt (GPIO27 Eingang / GPIO33
  Freigabe-Ausgang). Sicherheits-Timeout (5 Min.) setzt die Anforderung
  automatisch zurück, falls keine Aktualisierung mehr eintrifft.
- Entsprechende Karte samt Schalter in der Live-Demo sowie Ergänzung im
  Hardware-Schema.

### Geändert
- Projektname in beiden Webseiten auf „Flo's Garten" referenziert.

## [1.1.0] – 2026-09-09

### Hinzugefügt
- **Manuelle Ventilsteuerung** in der Live-Demo: beide Tankventile lassen
  sich im manuellen Modus unabhängig voneinander öffnen/schließen (statt
  nur alternativ einen Tank auszuwählen). Warnhinweis bei simuliertem
  Trockenlauf.
- **Hardware-Schema** (SVG) in der Live-Demo: ESP32, Ultraschallsensoren
  und Relais/Magnetventile inkl. GPIO-Pin-Beschriftung, reagiert live auf
  die Simulation (Füllstände, Ventilzustand, animierter Wasserfluss).

## [1.0.0] – 2026-09-09

### Hinzugefügt
- Initiales Projekt „Bewässerungssteuerung für 2× IBC-Container (1000 L)":
  - **Hardware-Konzept**: 2× JSN-SR04T-Ultraschallsensoren, 2× 12V-Magnetventile
    über Relaismodul, ESP32 als Steuerung, vollständige Stückliste und
    Verkabelungsplan.
  - **Firmware** (`bewaesserung_controller.ino`, `config.h`): Füllstandsmessung
    mit Medianfilter, entprellte Automatik-Umschaltung zwischen beiden
    Tanks samt Prellschutz, Trockenlauf-Alarm (beide Tanks leer → Ventile
    schließen), persistente Schwellwerte über `Preferences`.
  - **Web-Dashboard** (`data/index.html`): Live-Füllstände als Balkengrafik,
    Automatik-/Manuell-Umschalter, REST-API (`/api/status`, `/api/mode`,
    `/api/valve`, `/api/config`).
  - **Interaktive Live-Demo** (`demo.html`) mit simulierten Sensordaten zum
    Ausprobieren ohne Hardware; als eigenständige HTML-Datei zum Download
    bereitgestellt.
  - **README** mit BOM, Verkabelung, Installations- und Kalibrieranleitung.

### Geändert
- Projekt in „Gartenbewässerung" umbenannt (Titel in beiden Webseiten und
  README) und für den Upload zu GitHub vorbereitet: `LICENSE` (MIT),
  `.gitignore`, Repository-Struktur (`firmware/`, `data/`, `docs/`),
  lokales Git-Repo mit Initial-Commit, Anleitung zum Hosten der Demo über
  GitHub Pages.
