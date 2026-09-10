#pragma once
/*
 * config.h – Zentrale Konfiguration für die Bewässerungs-Tanksteuerung
 * -----------------------------------------------------------------
 * Hier alle Werte an die eigene Installation anpassen.
 */

// ---------- WLAN ----------
#define WIFI_SSID       "DEIN_WLAN_NAME"
#define WIFI_PASSWORD   "DEIN_WLAN_PASSWORT"
// Optional: fester Hostname, damit man z.B. http://bewaesserung.local aufrufen kann
#define HOSTNAME        "bewaesserung"

// ---------- Pinbelegung (ESP32 DevKit) ----------
// Ultraschallsensoren JSN-SR04T (wasserdicht), je einer pro Tank, oben im Deckel montiert
#define TANK1_TRIG_PIN  5
#define TANK1_ECHO_PIN  18
#define TANK2_TRIG_PIN  19
#define TANK2_ECHO_PIN  21

// Relais für die 12V-Magnetventile (stromlos geschlossen / NC), Ansteuerung über Relaismodul
// ACHTUNG: viele Relaismodule sind "active LOW" (Relais zieht an, wenn Pin LOW ist).
// Falls dein Modul active HIGH ist, RELAY_ACTIVE_LEVEL unten auf HIGH ändern.
#define TANK1_VALVE_RELAY_PIN 25
#define TANK2_VALVE_RELAY_PIN 26
#define RELAY_ACTIVE_LEVEL    LOW

// Status-LED (onboard oder extern), zeigt Alarmzustand (beide Tanks leer)
#define STATUS_LED_PIN  2

// ---------- Tank-Geometrie (für Füllstandsberechnung) ----------
// IBC 1000L: Innenhöhe bis zur max. Füllhöhe ca. 116 cm (typischer Wert, unbedingt selbst nachmessen!)
// SENSOR_BLIND_CM = minimaler Messabstand / Montageabstand des Sensors zur maximalen Wasseroberfläche
#define TANK_HEIGHT_CM      116.0f   // Distanz Sensor -> Boden (Tank leer)
#define SENSOR_BLIND_CM     5.0f     // Distanz Sensor -> Wasseroberfläche bei Tank voll
#define TANK_VOLUME_LITERS  1000.0f  // Nennvolumen des IBC

// ---------- Umschalt-Logik ----------
#define SWITCH_LOW_THRESHOLD_PCT   10.0f  // Aktiver Tank schaltet um, wenn er unter diesen Wert fällt
#define MIN_LEVEL_TO_ACTIVATE_PCT  5.0f   // Zieltank muss mindestens so voll sein, um aktiviert zu werden
#define DEBOUNCE_READINGS         5       // Anzahl aufeinanderfolgender Messungen unter Schwelle, bevor geschaltet wird
#define MEASURE_INTERVAL_MS       2000    // Messintervall
#define MIN_SWITCH_INTERVAL_MS    30000   // Mindestabstand zwischen zwei automatischen Umschaltungen (Prellschutz)

// ---------- Sonstiges ----------
#define SOUND_SPEED_CM_PER_US 0.0343f   // Schallgeschwindigkeit in Luft (cm/µs), für Ultraschall-Messung
#define ECHO_TIMEOUT_US       30000UL   // Timeout für pulseIn (~5m Reichweite)

// ---------- Schnittstelle zum Drip-/Tropfbewässerungssystem ----------
// Das externe Bewässerungssystem (eigener Controller, z.B. Bewässerungscomputer
// oder ein weiterer ESP32 mit Bodenfeuchtesensoren) meldet hier "Wasserbedarf" an.
// Die Tanksteuerung öffnet das Versorgungsventil des aktiven Tanks NUR dann,
// wenn tatsächlich Bedarf gemeldet ist (spart Druck/Wasser im Leerlauf) und
// meldet per Rückkanal, ob überhaupt Wasser verfügbar ist.

// Variante A (empfohlen): REST-API über WLAN, siehe /api/demand im Sketch.
// Variante B: einfacher Trockenkontakt/GPIO, falls der Drip-Controller kein WLAN kann.
#define USE_HARDWARE_DEMAND_INTERFACE true

#define DEMAND_INPUT_PIN         27   // Eingang: Drip-Controller zieht HIGH = "Wasser angefordert"
#define SUPPLY_READY_OUTPUT_PIN  33   // Ausgang: ESP32 signalisiert "Wasser verfügbar, Freigabe" zurück

// Sicherheits-Timeout: kommt über die REST-API (oder Hardware-Pin) keine erneute
// Anforderung mehr, wird die Anforderung automatisch verworfen und das Ventil
// geschlossen (Schutz bei Verbindungsabbruch/Absturz des Drip-Controllers).
#define DEMAND_TIMEOUT_MS  300000UL   // 5 Minuten

// ---------- Nous L14 Zigbee-Bewässerungsventil (an der Tropfleitung) ----------
// Das Nous L14 ist ein batteriebetriebenes Zigbee-3.0-Ventil (Tuya-Datenpunkte),
// das direkt am Schlauchausgang zur Tropfleitung sitzt (z.B. nach dem
// Tank-Sammelpunkt). Es benötigt zwingend einen Tuya-kompatiblen
// Zigbee-Koordinator (z.B. Nous E1/E7 oder einen generischen Zigbee-Stick)
// mit Zigbee2MQTT – der ESP32 selbst spricht kein Zigbee, sondern koppelt
// sich über den MQTT-Broker an, den Zigbee2MQTT ohnehin benötigt.
// Öffnet/schließt sich das L14 (z.B. per Zeitplan in Zigbee2MQTT/Home
// Assistant oder manuell in der Nous/Tuya-App), wertet der ESP32 das als
// Wasseranforderung und übernimmt das wie /api/demand.
#define USE_MQTT_DRIP_VALVE   true

#define MQTT_BROKER_HOST     "192.168.1.10"   // IP/Hostname des MQTT-Brokers (z.B. Home Assistant / Mosquitto)
#define MQTT_BROKER_PORT     1883
#define MQTT_CLIENT_ID       "bewaesserung-esp32"
#define MQTT_USERNAME        ""               // leer lassen, falls kein Login nötig
#define MQTT_PASSWORD        ""

// Zigbee2MQTT-"friendly_name", den du dem L14 beim Pairing gegeben hast
#define MQTT_L14_FRIENDLY_NAME  "drip_ventil"
#define MQTT_TOPIC_L14_STATE    "zigbee2mqtt/" MQTT_L14_FRIENDLY_NAME
#define MQTT_TOPIC_L14_SET       "zigbee2mqtt/" MQTT_L14_FRIENDLY_NAME "/set"

// ---------- Zeit (NTP) – für Zeitstempel im Verlaufsdiagramm ----------
#define NTP_SERVER          "pool.ntp.org"
#define GMT_OFFSET_SEC       3600     // MEZ = UTC+1
#define DAYLIGHT_OFFSET_SEC  3600     // Sommerzeit +1h (wird von der Fritzbox/dem Router i.d.R. nicht benötigt)

// ---------- Verlaufsdiagramm ----------
// Füllstände werden in einem Ringpuffer im RAM gespeichert (kein Flash-Verschleiß)
// und über /api/history bereitgestellt. Bei Neustart ist der Verlauf leer.
#define HISTORY_MAX_POINTS   288            // 288 × 15 Min = 3 Tage Verlauf
#define HISTORY_INTERVAL_MS  (15UL * 60UL * 1000UL)

// ---------- Sensor-Überwachung (für Verbindungsstatus im Dashboard) ----------
// Ab wann gilt eine Sensormessung als "veraltet" (z.B. Sensor abgeklemmt/defekt)?
#define SENSOR_STALE_MS  (2UL * MEASURE_INTERVAL_MS + 20000UL)

// ---------- Alarm-Push-Benachrichtigung ----------
// Wird ausgelöst, wenn beide Tanks leer sind (alarmBothEmpty). Danach erneute
// Erinnerung alle ALARM_RENOTIFY_INTERVAL_MS, solange der Alarm anhält.
// NOTIFY_PROVIDER: 0 = aus, 1 = ntfy.sh, 2 = Pushover, 3 = Telegram
#define NOTIFY_PROVIDER  1
#define ALARM_RENOTIFY_INTERVAL_MS (2UL * 60UL * 60UL * 1000UL)  // alle 2h erneut erinnern

// --- Variante 1: ntfy.sh (kein Account nötig, auch selbst hostbar) ---
// App installieren (iOS/Android) oder Browser, Topic abonnieren, fertig.
#define NTFY_SERVER "https://ntfy.sh"
#define NTFY_TOPIC  "flo-garten-bewaesserung-xk92"  // eindeutigen, schwer erratbaren Namen wählen!

// --- Variante 2: Pushover (https://pushover.net, kostenpflichtige App-Lizenz einmalig) ---
#define PUSHOVER_API_TOKEN  "DEIN_PUSHOVER_APP_TOKEN"
#define PUSHOVER_USER_KEY   "DEIN_PUSHOVER_USER_KEY"

// --- Variante 3: Telegram-Bot (kostenlos, via @BotFather erstellen) ---
#define TELEGRAM_BOT_TOKEN  "DEIN_TELEGRAM_BOT_TOKEN"
#define TELEGRAM_CHAT_ID    "DEINE_CHAT_ID"
