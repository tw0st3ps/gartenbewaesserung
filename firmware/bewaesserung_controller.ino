/*
 * Bewässerungs-Tanksteuerung für 2x IBC (1000L)
 * ----------------------------------------------
 * ESP32-Firmware:
 *  - misst den Füllstand beider Tanks per Ultraschall (JSN-SR04T)
 *  - schaltet automatisch per Relais/Magnetventil auf den jeweils anderen
 *    Tank um, sobald der aktive Tank unter einen Schwellwert fällt
 *  - stellt eine Weboberfläche + JSON-REST-API bereit (LittleFS)
 *
 * Benötigte Libraries (über Arduino Library Manager installieren):
 *  - ArduinoJson (Benoit Blanchon)
 *  - ESP32-Board-Unterstützung (enthält WiFi, WebServer, LittleFS, Preferences)
 *
 * Vor dem Hochladen: "ESP32 Sketch Data Upload" nutzen (oder LittleFS-Uploader-Plugin),
 * um den Ordner /data (index.html) auf den ESP32 zu laden.
 */

#include <WiFi.h>
#include <WebServer.h>
#include <LittleFS.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <PubSubClient.h>      // Library "PubSubClient" (Nick O'Leary)
#include <HTTPClient.h>
#include <WiFiClientSecure.h>  // für HTTPS-Aufrufe der Push-Dienste
#include <time.h>
#include "config.h"

WebServer server(80);
Preferences prefs;
WiFiClient mqttNetClient;
PubSubClient mqttClient(mqttNetClient);

// ---------- Zustand ----------
struct TankState {
  float distanceCm = 0;
  float fillPct = 0;
  float liters = 0;
  bool  valveOpen = false;
  int   belowThresholdCount = 0;
  unsigned long lastValidReadMs = 0;  // für Verbindungsstatus: wann kam die letzte gültige Messung?
};

TankState tank[2];              // Index 0 = Tank1, Index 1 = Tank2
int   activeTank = 0;           // aktuell freigegebener Tank
bool  autoMode = true;          // Automatik ein/aus
bool  alarmBothEmpty = false;
unsigned long lastMeasureMs = 0;
unsigned long lastSwitchMs  = 0;

// ---------- Schnittstelle zum Drip-Bewässerungssystem ----------
bool irrigationDemand = false;      // true = externes System fordert gerade Wasser an
unsigned long lastDemandMs = 0;     // Zeitpunkt der letzten Anforderung (für Timeout)
bool lastAlarmState = false;        // für Flankenerkennung (Alarm neu ausgelöst -> L14 mitschließen + Push)
unsigned long lastAlarmNotifyMs = 0;

// ---------- Verlaufsdiagramm ----------
struct HistoryPoint {
  time_t timestamp;
  float pct1;
  float pct2;
};
HistoryPoint history[HISTORY_MAX_POINTS];
int historyCount = 0;   // Anzahl belegter Einträge (bis Puffer voll)
int historyHead = 0;    // nächster Schreibindex (Ringpuffer)
unsigned long lastHistoryMs = 0;

// Aus config.h übernehmbare, aber zur Laufzeit über die Web-API änderbare Werte
float cfgLowThreshold   = SWITCH_LOW_THRESHOLD_PCT;
float cfgMinActivatePct = MIN_LEVEL_TO_ACTIVATE_PCT;

// ---------- Hilfsfunktionen ----------

float readDistanceCm(int trigPin, int echoPin) {
  digitalWrite(trigPin, LOW);
  delayMicroseconds(2);
  digitalWrite(trigPin, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigPin, LOW);

  unsigned long duration = pulseIn(echoPin, HIGH, ECHO_TIMEOUT_US);
  if (duration == 0) {
    return -1.0f; // kein gültiges Echo -> Sensorfehler / außerhalb Reichweite
  }
  return (duration * SOUND_SPEED_CM_PER_US) / 2.0f;
}

// Mehrfachmessung mit Median zur Rauschunterdrückung
float readDistanceMedian(int trigPin, int echoPin, int samples = 5) {
  float values[15];
  int n = 0;
  for (int i = 0; i < samples && i < 15; i++) {
    float d = readDistanceCm(trigPin, echoPin);
    if (d > 0) {
      values[n++] = d;
    }
    delay(15);
  }
  if (n == 0) return -1.0f;
  // einfacher Sortier-/Median-Ansatz
  for (int i = 0; i < n - 1; i++) {
    for (int j = i + 1; j < n; j++) {
      if (values[j] < values[i]) {
        float t = values[i]; values[i] = values[j]; values[j] = t;
      }
    }
  }
  return values[n / 2];
}

void setValve(int idx, bool open) {
  int pin = (idx == 0) ? TANK1_VALVE_RELAY_PIN : TANK2_VALVE_RELAY_PIN;
  digitalWrite(pin, open ? RELAY_ACTIVE_LEVEL : !RELAY_ACTIVE_LEVEL);
  tank[idx].valveOpen = open;
}

void applyValveStates() {
  // Im Alarmfall (beide Tanks leer) werden beide Ventile geschlossen -> Trockenlauf/Pumpenschutz
  if (alarmBothEmpty) {
    setValve(0, false);
    setValve(1, false);
    return;
  }
  // Das Versorgungsventil des aktiven Tanks öffnet nur, wenn das Drip-System
  // tatsächlich Wasser angefordert hat. Ohne Anforderung bleibt alles zu,
  // damit die Leitung nicht dauerhaft unter Druck steht.
  bool supply = irrigationDemand;
  setValve(0, supply && activeTank == 0);
  setValve(1, supply && activeTank == 1);

  // Rückmeldung an den Drip-Controller: Wasser grundsätzlich verfügbar?
  bool waterAvailable = !alarmBothEmpty && (tank[activeTank].fillPct > MIN_LEVEL_TO_ACTIVATE_PCT || tank[activeTank].fillPct > 0);
  digitalWrite(SUPPLY_READY_OUTPUT_PIN, waterAvailable ? HIGH : LOW);
}

// Muss regelmäßig ("Heartbeat") aus loop() aufgerufen werden.
void updateIrrigationDemand() {
#if USE_HARDWARE_DEMAND_INTERFACE
  // Optionaler Trockenkontakt-Eingang vom Drip-Controller
  if (digitalRead(DEMAND_INPUT_PIN) == HIGH) {
    irrigationDemand = true;
    lastDemandMs = millis();
  }
#endif

  // Sicherheits-Timeout: keine erneute Anforderung mehr erhalten -> Ventil schließen
  if (irrigationDemand && (millis() - lastDemandMs) > DEMAND_TIMEOUT_MS) {
    irrigationDemand = false;
    Serial.println("Wasseranforderung per Timeout zurückgesetzt (keine Anfrage mehr erhalten)");
  }
}

void measureTanks() {
  float d1 = readDistanceMedian(TANK1_TRIG_PIN, TANK1_ECHO_PIN);
  float d2 = readDistanceMedian(TANK2_TRIG_PIN, TANK2_ECHO_PIN);

  for (int i = 0; i < 2; i++) {
    float d = (i == 0) ? d1 : d2;
    if (d < 0) continue; // fehlerhafte Messung ignorieren, letzten gültigen Wert behalten

    d = constrain(d, SENSOR_BLIND_CM, TANK_HEIGHT_CM);
    float pct = (TANK_HEIGHT_CM - d) / (TANK_HEIGHT_CM - SENSOR_BLIND_CM) * 100.0f;
    pct = constrain(pct, 0.0f, 100.0f);

    tank[i].distanceCm = d;
    tank[i].fillPct = pct;
    tank[i].liters = pct / 100.0f * TANK_VOLUME_LITERS;
    tank[i].lastValidReadMs = millis();
  }
}

// Wird regelmäßig aus loop() aufgerufen und schreibt einen Punkt in den Verlaufs-Ringpuffer
void recordHistory() {
  if (millis() - lastHistoryMs < HISTORY_INTERVAL_MS) return;
  lastHistoryMs = millis();

  history[historyHead].timestamp = time(nullptr); // Sekunden seit Epoch (0 falls NTP noch nicht synchronisiert)
  history[historyHead].pct1 = tank[0].fillPct;
  history[historyHead].pct2 = tank[1].fillPct;

  historyHead = (historyHead + 1) % HISTORY_MAX_POINTS;
  if (historyCount < HISTORY_MAX_POINTS) historyCount++;
}

void evaluateSwitchLogic() {
  if (!autoMode) return;

  TankState &act = tank[activeTank];
  int other = 1 - activeTank;

  if (act.fillPct <= cfgLowThreshold) {
    act.belowThresholdCount++;
  } else {
    act.belowThresholdCount = 0;
  }

  bool stableLow = act.belowThresholdCount >= DEBOUNCE_READINGS;
  bool switchCooldownOver = (millis() - lastSwitchMs) >= MIN_SWITCH_INTERVAL_MS;

  if (stableLow && switchCooldownOver) {
    if (tank[other].fillPct > cfgMinActivatePct) {
      // Umschalten auf den anderen, ausreichend gefüllten Tank
      activeTank = other;
      act.belowThresholdCount = 0;
      lastSwitchMs = millis();
      alarmBothEmpty = false;
    } else {
      // Beide Tanks (annähernd) leer -> Alarmzustand, Ventile schließen
      alarmBothEmpty = true;
    }
  }

  // Alarm automatisch aufheben, sobald wieder genug Wasser in einem Tank ist
  if (alarmBothEmpty) {
    if (tank[0].fillPct > cfgMinActivatePct || tank[1].fillPct > cfgMinActivatePct) {
      activeTank = (tank[0].fillPct >= tank[1].fillPct) ? 0 : 1;
      alarmBothEmpty = false;
      lastSwitchMs = millis();
    }
  }

  // Flankenerkennung: Alarm neu ausgelöst -> Nous L14 sicherheitshalber mitschließen + Push-Alarm
  if (alarmBothEmpty && !lastAlarmState) {
    closeDripValveViaMqtt();
  }
  checkAlarmNotification();
  lastAlarmState = alarmBothEmpty;
}

// ---------- MQTT / Nous L14 (Zigbee-Ventil an der Tropfleitung) ----------

// Schließt das L14 zwangsweise per MQTT (Sicherheits-Verbund bei Alarm)
void closeDripValveViaMqtt() {
#if USE_MQTT_DRIP_VALVE
  if (mqttClient.connected()) {
    mqttClient.publish(MQTT_TOPIC_L14_SET, "{\"state\":\"OFF\"}");
    Serial.println("Alarm: Nous L14 per MQTT zwangsweise geschlossen");
  }
#endif
}

#if USE_MQTT_DRIP_VALVE
void mqttCallback(char* topic, byte* payload, unsigned int length) {
  StaticJsonDocument<256> doc;
  DeserializationError err = deserializeJson(doc, payload, length);
  if (err) return;

  if (strcmp(topic, MQTT_TOPIC_L14_STATE) == 0 && doc.containsKey("state")) {
    const char* s = doc["state"];
    bool open = (strcmp(s, "ON") == 0);
    irrigationDemand = open;
    lastDemandMs = millis();
    Serial.printf("Nous L14 meldet Zustand '%s' -> Wasseranforderung = %s\n", s, open ? "JA" : "nein");
  }
}

void mqttReconnect() {
  if (mqttClient.connected()) return;
  if (mqttClient.connect(MQTT_CLIENT_ID, MQTT_USERNAME, MQTT_PASSWORD)) {
    mqttClient.subscribe(MQTT_TOPIC_L14_STATE);
    Serial.println("MQTT verbunden, abonniert: " MQTT_TOPIC_L14_STATE);
  }
}
#endif

// ---------- Push-Benachrichtigung bei Alarm ----------
void sendPushNotification(const String &title, const String &message) {
#if NOTIFY_PROVIDER != 0
  // Hinweis: setInsecure() prüft KEIN Server-Zertifikat (vereinfachte Hobby-Lösung).
  // Für produktiven/sicherheitskritischen Einsatz stattdessen das jeweilige
  // Root-CA-Zertifikat hinterlegen (WiFiClientSecure::setCACert()).
  WiFiClientSecure secureClient;
  secureClient.setInsecure();
  HTTPClient http;
#endif

#if NOTIFY_PROVIDER == 1
  // ntfy.sh: einfacher HTTP(S)-POST, Topic dient als "Adresse"
  http.begin(secureClient, String(NTFY_SERVER) + "/" + NTFY_TOPIC);
  http.addHeader("Title", title);
  http.addHeader("Priority", "urgent");
  int code = http.POST(message);
  Serial.printf("ntfy-Benachrichtigung gesendet, HTTP-Status: %d\n", code);
  http.end();

#elif NOTIFY_PROVIDER == 2
  // Pushover
  http.begin(secureClient, "https://api.pushover.net/1/messages.json");
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");
  String body = "token=" + String(PUSHOVER_API_TOKEN) +
                "&user=" + String(PUSHOVER_USER_KEY) +
                "&title=" + title +
                "&message=" + message +
                "&priority=1";
  int code = http.POST(body);
  Serial.printf("Pushover-Benachrichtigung gesendet, HTTP-Status: %d\n", code);
  http.end();

#elif NOTIFY_PROVIDER == 3
  // Telegram-Bot
  String url = "https://api.telegram.org/bot" + String(TELEGRAM_BOT_TOKEN) + "/sendMessage";
  http.begin(secureClient, url);
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");
  String text = title + ": " + message;
  String body = "chat_id=" + String(TELEGRAM_CHAT_ID) + "&text=" + text;
  int code = http.POST(body);
  Serial.printf("Telegram-Benachrichtigung gesendet, HTTP-Status: %d\n", code);
  http.end();
#else
  // NOTIFY_PROVIDER == 0 -> Push-Benachrichtigungen deaktiviert
#endif
}

void checkAlarmNotification() {
#if NOTIFY_PROVIDER != 0
  bool justTriggered = alarmBothEmpty && !lastAlarmState;
  bool dueForRenotify = alarmBothEmpty &&
                         (millis() - lastAlarmNotifyMs) >= ALARM_RENOTIFY_INTERVAL_MS;

  if (justTriggered || dueForRenotify) {
    sendPushNotification("Gartenbewässerung – Alarm",
                          "Beide IBC-Tanks sind leer. Ventile wurden geschlossen.");
    lastAlarmNotifyMs = millis();
  }
#endif
}

// ---------- Webserver / API ----------

void handleRoot() {
  File f = LittleFS.open("/index.html", "r");
  if (!f) {
    server.send(500, "text/plain", "index.html nicht gefunden (LittleFS-Upload durchgeführt?)");
    return;
  }
  server.streamFile(f, "text/html");
  f.close();
}

void handleApiStatus() {
  StaticJsonDocument<768> doc;
  doc["activeTank"] = activeTank;
  doc["autoMode"] = autoMode;
  doc["alarm"] = alarmBothEmpty;
  doc["mqttConnected"] = mqttClient.connected();
  doc["wifiRssi"] = WiFi.RSSI();

  for (int i = 0; i < 2; i++) {
    JsonObject t = doc["tanks"][i].to<JsonObject>();
    t["index"] = i;
    t["fillPct"] = round(tank[i].fillPct * 10) / 10.0;
    t["liters"] = round(tank[i].liters);
    t["distanceCm"] = round(tank[i].distanceCm * 10) / 10.0;
    t["valveOpen"] = tank[i].valveOpen;
    unsigned long ageMs = millis() - tank[i].lastValidReadMs;
    t["lastReadAgoSec"] = ageMs / 1000;
    t["sensorOk"] = ageMs < SENSOR_STALE_MS;
  }

  doc["config"]["lowThresholdPct"] = cfgLowThreshold;
  doc["config"]["minActivatePct"] = cfgMinActivatePct;
  doc["irrigationDemand"] = irrigationDemand;

  String out;
  serializeJson(doc, out);
  server.send(200, "application/json", out);
}

// GET /api/history -> Verlauf der Füllstände (Ringpuffer) für das Diagramm im Dashboard
void handleApiHistory() {
  DynamicJsonDocument doc(16384);
  doc["intervalSec"] = HISTORY_INTERVAL_MS / 1000;
  JsonArray points = doc["points"].to<JsonArray>();

  // Chronologische Reihenfolge herstellen (Ringpuffer beginnt nicht zwingend bei 0)
  int start = (historyCount < HISTORY_MAX_POINTS) ? 0 : historyHead;
  for (int i = 0; i < historyCount; i++) {
    int idx = (start + i) % HISTORY_MAX_POINTS;
    JsonObject p = points.createNestedObject();
    p["t"] = history[idx].timestamp;
    p["p1"] = round(history[idx].pct1 * 10) / 10.0;
    p["p2"] = round(history[idx].pct2 * 10) / 10.0;
  }

  String out;
  serializeJson(doc, out);
  server.send(200, "application/json", out);
}

// POST /api/mode  { "auto": true|false }
void handleApiMode() {
  if (server.hasArg("plain")) {
    StaticJsonDocument<128> doc;
    deserializeJson(doc, server.arg("plain"));
    if (doc.containsKey("auto")) {
      autoMode = doc["auto"];
    }
  }
  handleApiStatus();
}

// POST /api/valve  { "tank": 0|1 }  -> manuelles Umschalten (nur wirksam, wenn autoMode=false)
void handleApiValve() {
  if (!autoMode && server.hasArg("plain")) {
    StaticJsonDocument<128> doc;
    deserializeJson(doc, server.arg("plain"));
    if (doc.containsKey("tank")) {
      int t = doc["tank"];
      if (t == 0 || t == 1) {
        activeTank = t;
        alarmBothEmpty = false;
        lastSwitchMs = millis();
      }
    }
  }
  handleApiStatus();
}

// POST /api/config { "lowThresholdPct": .., "minActivatePct": .. }
void handleApiConfig() {
  if (server.hasArg("plain")) {
    StaticJsonDocument<128> doc;
    deserializeJson(doc, server.arg("plain"));
    if (doc.containsKey("lowThresholdPct")) {
      cfgLowThreshold = doc["lowThresholdPct"];
      prefs.putFloat("lowThr", cfgLowThreshold);
    }
    if (doc.containsKey("minActivatePct")) {
      cfgMinActivatePct = doc["minActivatePct"];
      prefs.putFloat("minAct", cfgMinActivatePct);
    }
  }
  handleApiStatus();
}

// GET /api/demand -> aktueller Anforderungs-/Versorgungsstatus fürs Drip-System
void handleApiDemandGet() {
  StaticJsonDocument<256> doc;
  bool waterAvailable = !alarmBothEmpty;
  doc["demand"] = irrigationDemand;
  doc["supplyAvailable"] = waterAvailable;
  doc["activeTank"] = activeTank;
  doc["activeTankPct"] = round(tank[activeTank].fillPct * 10) / 10.0;
  doc["alarm"] = alarmBothEmpty;
  String out;
  serializeJson(doc, out);
  server.send(200, "application/json", out);
}

// POST /api/demand  { "active": true|false }
// Wird vom Drip-Controller aufgerufen: true beim Start eines Bewässerungszyklus,
// false wenn dieser beendet ist. Muss bei längeren Zyklen regelmäßig mit
// "active": true wiederholt werden (Heartbeat), sonst greift der Sicherheits-Timeout.
void handleApiDemandPost() {
  if (server.hasArg("plain")) {
    StaticJsonDocument<128> doc;
    deserializeJson(doc, server.arg("plain"));
    if (doc.containsKey("active")) {
      irrigationDemand = doc["active"];
      lastDemandMs = millis();
    }
  }
  applyValveStates();
  handleApiDemandGet();
}

void setupWebserver() {
  server.on("/", HTTP_GET, handleRoot);
  server.on("/api/status", HTTP_GET, handleApiStatus);
  server.on("/api/mode", HTTP_POST, handleApiMode);
  server.on("/api/valve", HTTP_POST, handleApiValve);
  server.on("/api/config", HTTP_POST, handleApiConfig);
  server.on("/api/demand", HTTP_GET, handleApiDemandGet);
  server.on("/api/demand", HTTP_POST, handleApiDemandPost);
  server.on("/api/history", HTTP_GET, handleApiHistory);

  server.serveStatic("/", LittleFS, "/"); // weitere statische Dateien (CSS/JS falls ausgelagert)
  server.begin();
}

// ---------- Setup / Loop ----------

void setup() {
  Serial.begin(115200);

  pinMode(TANK1_TRIG_PIN, OUTPUT);
  pinMode(TANK1_ECHO_PIN, INPUT);
  pinMode(TANK2_TRIG_PIN, OUTPUT);
  pinMode(TANK2_ECHO_PIN, INPUT);

  pinMode(TANK1_VALVE_RELAY_PIN, OUTPUT);
  pinMode(TANK2_VALVE_RELAY_PIN, OUTPUT);
  pinMode(STATUS_LED_PIN, OUTPUT);

#if USE_HARDWARE_DEMAND_INTERFACE
  pinMode(DEMAND_INPUT_PIN, INPUT_PULLDOWN);
  pinMode(SUPPLY_READY_OUTPUT_PIN, OUTPUT);
  digitalWrite(SUPPLY_READY_OUTPUT_PIN, LOW);
#endif

  // Ventile beim Start sicher schließen
  digitalWrite(TANK1_VALVE_RELAY_PIN, !RELAY_ACTIVE_LEVEL);
  digitalWrite(TANK2_VALVE_RELAY_PIN, !RELAY_ACTIVE_LEVEL);

  prefs.begin("bewaesserung", false);
  cfgLowThreshold   = prefs.getFloat("lowThr", SWITCH_LOW_THRESHOLD_PCT);
  cfgMinActivatePct = prefs.getFloat("minAct", MIN_LEVEL_TO_ACTIVATE_PCT);

  if (!LittleFS.begin(true)) {
    Serial.println("LittleFS Mount fehlgeschlagen!");
  }

  WiFi.setHostname(HOSTNAME);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Verbinde mit WLAN");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  Serial.print("IP-Adresse: ");
  Serial.println(WiFi.localIP());

  // NTP-Zeit synchronisieren (für Zeitstempel im Verlaufsdiagramm)
  configTime(GMT_OFFSET_SEC, DAYLIGHT_OFFSET_SEC, NTP_SERVER);

  setupWebserver();

#if USE_MQTT_DRIP_VALVE
  mqttClient.setServer(MQTT_BROKER_HOST, MQTT_BROKER_PORT);
  mqttClient.setCallback(mqttCallback);
  mqttReconnect();
#endif

  // Initiale Messung, bevor der aktive Tank bestimmt wird
  measureTanks();
  activeTank = (tank[0].fillPct >= tank[1].fillPct) ? 0 : 1;
  applyValveStates();
}

void loop() {
  server.handleClient();

#if USE_MQTT_DRIP_VALVE
  if (!mqttClient.connected()) {
    static unsigned long lastReconnectAttempt = 0;
    if (millis() - lastReconnectAttempt > 5000) {
      lastReconnectAttempt = millis();
      mqttReconnect();
    }
  }
  mqttClient.loop();
#endif

  updateIrrigationDemand();
  applyValveStates(); // sofort reagieren, falls sich die Anforderung geändert hat

  if (millis() - lastMeasureMs >= MEASURE_INTERVAL_MS) {
    lastMeasureMs = millis();
    measureTanks();
    evaluateSwitchLogic();
    applyValveStates();
    recordHistory();
    digitalWrite(STATUS_LED_PIN, alarmBothEmpty ? HIGH : LOW);

    Serial.printf("Tank1: %.1f%% | Tank2: %.1f%% | aktiv: Tank%d | Alarm: %s\n",
                  tank[0].fillPct, tank[1].fillPct, activeTank + 1,
                  alarmBothEmpty ? "JA" : "nein");
  }
}
