/*
  PoolLevel v1.2.0
  ─────────────────────────────────────────────────────────────────────────────
  ESP8266 pool extension tank level monitor.

  Features
  --------
  • Up to 4 float switches → MQTT → Home Assistant binary_sensors
  • LittleFS config storage (survives firmware OTA updates)
  • Captive-portal AP (PoolLevel-Setup / poolsetup) for first-run setup
  • mDNS: <clientId>.local  (default: poollevel.local)
  • ArduinoOTA push from PlatformIO  (env: d1_mini_ota)
  • HTTP OTA upload via web UI  (/update)
  • 5-tab dark web UI: Status / WiFi / MQTT / Switches / OTA

  Hardware (Wemos D1 mini defaults)
  ----------------------------------
  Switch 1 → GPIO 4  (D2)
  Switch 2 → GPIO 5  (D1)
  Switch 3 → GPIO 12 (D6)
  Switch 4 → GPIO 14 (D5)
  Status LED → GPIO 2 (D4, active LOW, onboard)

  Wiring (active-LOW / INPUT_PULLUP, default)
  --------------------------------------------
  GND ──[float switch]── GPIOx
  Float closed → GPIO LOW → state ON → water level reached
*/

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <ESP8266HTTPUpdateServer.h>
#include <DNSServer.h>
#include <ArduinoOTA.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <Ticker.h>

#include "config.h"

// ─── Forward declarations ────────────────────────────────────────────────────
bool loadConfig(AppConfig &cfg);
bool saveConfig(const AppConfig &cfg);
void webui_setup(ESP8266WebServer &server, AppConfig &cfg);

// ─── Globals ─────────────────────────────────────────────────────────────────
AppConfig               cfg;
ESP8266WebServer        server(HTTP_PORT);
ESP8266HTTPUpdateServer httpUpdater;
DNSServer               dns;
WiFiClient              wifiClient;
PubSubClient            mqtt(wifiClient);
Ticker                  ledTicker;

bool     apMode          = false;
uint32_t lastMqttAttempt = 0;
uint32_t lastRepublish   = 0;
int      lastState[MAX_SWITCHES];   // -1 = never published

// ─── LED helpers ─────────────────────────────────────────────────────────────
void ledOn()     { digitalWrite(STATUS_LED_PIN, LOW);  }
void ledOff()    { digitalWrite(STATUS_LED_PIN, HIGH); }
void ledToggle() { digitalWrite(STATUS_LED_PIN, !digitalRead(STATUS_LED_PIN)); }

// ─── GPIO setup & reading ────────────────────────────────────────────────────
void setupGPIOs() {
    for (int i = 0; i < MAX_SWITCHES; i++) {
        lastState[i] = -1;
        if (!cfg.sw[i].enabled) continue;
        pinMode(cfg.sw[i].gpio,
                cfg.sw[i].activeLow ? INPUT_PULLUP : INPUT);
    }
}

bool readSwitch(int i) {
    int raw = digitalRead(cfg.sw[i].gpio);
    return cfg.sw[i].activeLow ? (raw == LOW) : (raw == HIGH);
}

// ─── MQTT topic helpers ──────────────────────────────────────────────────────
String stateTopic(int i) {
    return cfg.mqttBaseTopic + "/" + cfg.sw[i].name + "/state";
}
String availTopic() {
    return cfg.mqttBaseTopic + "/availability";
}

// ─── HA auto-discovery payload ───────────────────────────────────────────────
void publishDiscovery(int i) {
    if (!cfg.haDiscovery) return;
    String discTopic = "homeassistant/binary_sensor/"
                     + cfg.clientId + "_sw" + String(i) + "/config";

    DynamicJsonDocument doc(512);
    doc["name"]               = cfg.deviceName + " " + cfg.sw[i].name;
    doc["unique_id"]          = cfg.clientId + "_sw" + String(i);
    doc["state_topic"]        = stateTopic(i);
    doc["payload_on"]         = "ON";
    doc["payload_off"]        = "OFF";
    doc["availability_topic"] = availTopic();
    doc["payload_available"]  = "online";
    doc["payload_not_available"] = "offline";
    doc["device_class"]       = "moisture";

    JsonObject dev        = doc.createNestedObject("device");
    dev["identifiers"][0] = cfg.clientId;
    dev["name"]           = cfg.deviceName;
    dev["model"]          = "PoolLevel ESP8266";
    dev["sw_version"]     = FW_VERSION;

    String payload;
    serializeJson(doc, payload);
    mqtt.publish(discTopic.c_str(), payload.c_str(), true);
}

// ─── MQTT connect ────────────────────────────────────────────────────────────
bool mqttConnect() {
    if (cfg.mqttHost.isEmpty()) return false;
    mqtt.setServer(cfg.mqttHost.c_str(), cfg.mqttPort);

    const char *lwt = availTopic().c_str();
    bool ok = cfg.mqttUser.length()
        ? mqtt.connect(cfg.clientId.c_str(),
                       cfg.mqttUser.c_str(), cfg.mqttPassword.c_str(),
                       lwt, 0, true, "offline")
        : mqtt.connect(cfg.clientId.c_str(), lwt, 0, true, "offline");

    if (ok) {
        mqtt.publish(lwt, "online", true);
        for (int i = 0; i < MAX_SWITCHES; i++)
            if (cfg.sw[i].enabled) publishDiscovery(i);
    }
    return ok;
}

// ─── Publish switch states ───────────────────────────────────────────────────
void publishStates(bool force = false) {
    if (!mqtt.connected()) return;
    for (int i = 0; i < MAX_SWITCHES; i++) {
        if (!cfg.sw[i].enabled) continue;
        int s = readSwitch(i) ? 1 : 0;
        if (force || s != lastState[i]) {
            mqtt.publish(stateTopic(i).c_str(), s ? "ON" : "OFF", true);
            lastState[i] = s;
        }
    }
}

// ─── mDNS ────────────────────────────────────────────────────────────────────
void setupMDNS() {
    if (MDNS.begin(cfg.clientId.c_str())) {
        MDNS.addService("http", "tcp", HTTP_PORT);
        Serial.printf("mDNS: http://%s.local\n", cfg.clientId.c_str());
    } else {
        Serial.println(F("mDNS start failed"));
    }
}

// ─── ArduinoOTA ──────────────────────────────────────────────────────────────
void setupOTA() {
    ArduinoOTA.setPort(OTA_PORT);
    ArduinoOTA.setHostname(cfg.clientId.c_str());
    ArduinoOTA.setPassword(OTA_PASSWORD);

    ArduinoOTA.onStart([]() {
        String type = (ArduinoOTA.getCommand() == U_FLASH) ? "firmware" : "filesystem";
        Serial.println("OTA start: " + type);
        if (mqtt.connected()) {
            mqtt.publish(availTopic().c_str(), "offline", true);
            mqtt.disconnect();
        }
        ledTicker.attach(0.05f, ledToggle);
    });
    ArduinoOTA.onEnd([]() {
        ledTicker.detach(); ledOff();
        Serial.println(F("\nOTA complete"));
    });
    ArduinoOTA.onProgress([](unsigned int done, unsigned int total) {
        Serial.printf("OTA %u%%\r", done * 100 / total);
    });
    ArduinoOTA.onError([](ota_error_t err) {
        ledTicker.detach(); ledOn();
        Serial.printf("OTA error [%u]\n", (unsigned)err);
    });

    ArduinoOTA.begin();
    Serial.printf("ArduinoOTA ready  port:%d  host:%s.local\n",
                  OTA_PORT, cfg.clientId.c_str());
}

// ─── WiFi connect ────────────────────────────────────────────────────────────
bool connectWiFi() {
    if (cfg.wifiSSID.isEmpty()) return false;
    WiFi.persistent(false);
    WiFi.mode(WIFI_STA);
    WiFi.begin(cfg.wifiSSID.c_str(), cfg.wifiPassword.c_str());

    ledTicker.attach(0.2f, ledToggle);
    uint32_t t0 = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t0 < WIFI_TIMEOUT_MS)
        delay(100);
    ledTicker.detach();

    bool ok = (WiFi.status() == WL_CONNECTED);
    ok ? ledOn() : ledOff();
    return ok;
}

// ─── AP mode ─────────────────────────────────────────────────────────────────
void startAP() {
    apMode = true;
    WiFi.persistent(false);
    WiFi.mode(WIFI_AP);
    WiFi.softAP(AP_SSID, AP_PASSWORD);
    dns.start(DNS_PORT, "*", WiFi.softAPIP());
    ledTicker.attach(1.0f, ledToggle);
    Serial.printf("AP mode  SSID:%s  IP:%s\n",
                  AP_SSID, WiFi.softAPIP().toString().c_str());
}

// ─── API helpers ─────────────────────────────────────────────────────────────
static void sendJson(const String &j, int code = 200) {
    server.send(code, "application/json", j);
}
static void sendOk()                { sendJson(F("{\"ok\":true}")); }
static void sendFail(const char *m) { sendJson(String(F("{\"ok\":false,\"error\":\"")) + m + "\"}"); }

// ─── GET /api/config ─────────────────────────────────────────────────────────
void apiGetConfig() {
    DynamicJsonDocument doc(2048);
    doc["wifi_ssid"]    = cfg.wifiSSID;
    doc["wifi_pass"]    = cfg.wifiPassword;
    doc["mqtt_host"]    = cfg.mqttHost;
    doc["mqtt_port"]    = cfg.mqttPort;
    doc["mqtt_user"]    = cfg.mqttUser;
    doc["mqtt_pass"]    = cfg.mqttPassword;
    doc["mqtt_topic"]   = cfg.mqttBaseTopic;
    doc["device_name"]  = cfg.deviceName;
    doc["client_id"]    = cfg.clientId;
    doc["ha_discovery"] = cfg.haDiscovery;
    for (int i = 0; i < MAX_SWITCHES; i++) {
        JsonObject sw = doc.createNestedObject("sw" + String(i));
        sw["name"]    = cfg.sw[i].name;
        sw["gpio"]    = cfg.sw[i].gpio;
        sw["actlow"]  = cfg.sw[i].activeLow;
        sw["enabled"] = cfg.sw[i].enabled;
    }
    String out; serializeJson(doc, out); sendJson(out);
}

// ─── GET /api/status ─────────────────────────────────────────────────────────
void apiGetStatus() {
    DynamicJsonDocument doc(1024);
    doc["wifi_connected"] = (WiFi.status() == WL_CONNECTED);
    doc["wifi_ssid"]      = cfg.wifiSSID;
    doc["ip"]             = apMode ? WiFi.softAPIP().toString()
                                   : WiFi.localIP().toString();
    doc["mqtt_connected"] = mqtt.connected();
    doc["mqtt_host"]      = cfg.mqttHost;
    doc["ap_mode"]        = apMode;
    doc["mdns_hostname"]  = apMode ? "" : (cfg.clientId + ".local");
    doc["fw_version"]     = FW_VERSION;
    JsonArray arr = doc.createNestedArray("switches");
    for (int i = 0; i < MAX_SWITCHES; i++) {
        JsonObject s = arr.createNestedObject();
        s["name"]    = cfg.sw[i].name;
        s["gpio"]    = cfg.sw[i].gpio;
        s["enabled"] = cfg.sw[i].enabled;
        s["state"]   = cfg.sw[i].enabled && readSwitch(i);
    }
    String out; serializeJson(doc, out); sendJson(out);
}

// ─── POST /api/save/wifi ─────────────────────────────────────────────────────
void apiSaveWifi() {
    DynamicJsonDocument doc(256);
    if (deserializeJson(doc, server.arg("plain"))) { sendFail("JSON parse error"); return; }
    cfg.wifiSSID     = doc["wifi_ssid"] | "";
    cfg.wifiPassword = doc["wifi_pass"] | "";
    if (!saveConfig(cfg)) { sendFail("LittleFS write failed"); return; }
    sendOk(); delay(400); ESP.restart();
}

// ─── POST /api/save/mqtt ─────────────────────────────────────────────────────
void apiSaveMqtt() {
    DynamicJsonDocument doc(512);
    if (deserializeJson(doc, server.arg("plain"))) { sendFail("JSON parse error"); return; }
    cfg.deviceName    = doc["device_name"] | cfg.deviceName;
    cfg.clientId      = doc["client_id"]   | cfg.clientId;
    cfg.mqttHost      = doc["mqtt_host"]   | "";
    cfg.mqttPort      = doc["mqtt_port"]   | 1883;
    cfg.mqttBaseTopic = doc["mqtt_topic"]  | "pool/level";
    cfg.mqttUser      = doc["mqtt_user"]   | "";
    cfg.mqttPassword  = doc["mqtt_pass"]   | "";
    cfg.haDiscovery   = doc["ha_discovery"]| true;
    if (!saveConfig(cfg)) { sendFail("LittleFS write failed"); return; }
    sendOk(); delay(400); ESP.restart();
}

// ─── POST /api/save/switches ─────────────────────────────────────────────────
void apiSaveSwitches() {
    DynamicJsonDocument doc(1024);
    if (deserializeJson(doc, server.arg("plain"))) { sendFail("JSON parse error"); return; }
    for (int i = 0; i < MAX_SWITCHES; i++) {
        JsonObject sw   = doc["sw" + String(i)];
        cfg.sw[i].name      = sw["name"]   | cfg.sw[i].name;
        cfg.sw[i].gpio      = sw["gpio"]   | cfg.sw[i].gpio;
        cfg.sw[i].activeLow = sw["actlow"] | true;
        cfg.sw[i].enabled   = sw["enabled"]| false;
    }
    if (!saveConfig(cfg)) { sendFail("LittleFS write failed"); return; }
    sendOk(); delay(400); ESP.restart();
}

// ─── GET /api/scan ───────────────────────────────────────────────────────────
void apiScan() {
    int n = WiFi.scanNetworks();
    DynamicJsonDocument doc(1024);
    JsonArray arr = doc.createNestedArray("networks");
    for (int i = 0; i < n; i++) {
        JsonObject net = arr.createNestedObject();
        net["ssid"] = WiFi.SSID(i);
        net["rssi"] = WiFi.RSSI(i);
        net["enc"]  = (WiFi.encryptionType(i) != ENC_TYPE_NONE);
    }
    String out; serializeJson(doc, out); sendJson(out);
}

// ─── POST /api/reset ─────────────────────────────────────────────────────────
void apiReset() {
    LittleFS.remove(CONFIG_FILE);
    sendOk(); delay(400); ESP.restart();
}

// ─── Captive-portal redirect ─────────────────────────────────────────────────
void handleNotFound() {
    server.sendHeader("Location", "http://192.168.4.1/");
    server.send(302, "text/plain", "");
}

// ═══════════════════════════════════════════════════════════════════════════════
void setup() {
    Serial.begin(115200);
    Serial.printf("\n\n=== PoolLevel v%s ===\n", FW_VERSION);

    pinMode(STATUS_LED_PIN, OUTPUT);
    ledOff();

    if (!LittleFS.begin()) {
        Serial.println(F("LittleFS mount failed — formatting…"));
        LittleFS.format();
        if (!LittleFS.begin())
            Serial.println(F("LittleFS format failed!"));
    }

    if (!loadConfig(cfg))
        Serial.println(F("No config — using defaults"));

    for (int i = 0; i < MAX_SWITCHES; i++) {
        if (cfg.sw[i].name.isEmpty()) cfg.sw[i].name = "Switch_" + String(i + 1);
        if (cfg.sw[i].gpio == 0)      cfg.sw[i].gpio = DEFAULT_GPIO[i];
    }

    setupGPIOs();

    if (!connectWiFi()) {
        Serial.println(F("WiFi unavailable → AP mode"));
        startAP();
    } else {
        Serial.printf("WiFi OK  IP: %s\n", WiFi.localIP().toString().c_str());
        setupMDNS();
        setupOTA();
    }

    webui_setup(server, cfg);
    server.on("/api/config",        HTTP_GET,  apiGetConfig);
    server.on("/api/status",        HTTP_GET,  apiGetStatus);
    server.on("/api/save/wifi",     HTTP_POST, apiSaveWifi);
    server.on("/api/save/mqtt",     HTTP_POST, apiSaveMqtt);
    server.on("/api/save/switches", HTTP_POST, apiSaveSwitches);
    server.on("/api/scan",          HTTP_GET,  apiScan);
    server.on("/api/reset",         HTTP_POST, apiReset);
    server.onNotFound(handleNotFound);

    if (!apMode) {
        httpUpdater.setup(&server, "/update", "admin", OTA_PASSWORD);
        Serial.println(F("HTTP OTA at /update"));
    }

    server.begin();
    Serial.println(F("HTTP server started"));
}

// ═══════════════════════════════════════════════════════════════════════════════
void loop() {
    if (apMode) dns.processNextRequest();
    server.handleClient();

    if (!apMode) {
        MDNS.update();
        ArduinoOTA.handle();
    }

    if (!apMode && !cfg.mqttHost.isEmpty()) {
        if (!mqtt.connected()) {
            uint32_t now = millis();
            if (now - lastMqttAttempt > MQTT_RECONNECT_MS) {
                lastMqttAttempt = now;
                if (mqttConnect()) {
                    Serial.println(F("MQTT connected"));
                    publishStates(true);
                    lastRepublish = millis();
                } else {
                    Serial.println(F("MQTT connect failed — retrying…"));
                }
            }
        } else {
            mqtt.loop();
            uint32_t now = millis();
            bool force = (now - lastRepublish > REPUBLISH_MS);
            publishStates(force);
            if (force) lastRepublish = now;
        }
    }
}