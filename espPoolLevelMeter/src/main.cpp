#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <DNSServer.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <Ticker.h>
#include "config.h"

bool loadConfig(AppConfig &cfg);
bool saveConfig(const AppConfig &cfg);
void webui_setup(ESP8266WebServer &server, AppConfig &cfg);

AppConfig        cfg;
ESP8266WebServer server(HTTP_PORT);
DNSServer        dns;
WiFiClient       wifiClient;
PubSubClient     mqtt(wifiClient);
Ticker           ledTicker;

bool     apMode          = false;
uint32_t lastMqttAttempt = 0;
uint32_t lastRepublish   = 0;
int      lastState[MAX_SWITCHES];

void ledOn()     { digitalWrite(STATUS_LED_PIN, LOW);  }
void ledOff()    { digitalWrite(STATUS_LED_PIN, HIGH); }
void ledToggle() { digitalWrite(STATUS_LED_PIN, !digitalRead(STATUS_LED_PIN)); }

void setupGPIOs() {
    for (int i = 0; i < MAX_SWITCHES; i++) {
        if (!cfg.sw[i].enabled) continue;
        pinMode(cfg.sw[i].gpio, cfg.sw[i].activeLow ? INPUT_PULLUP : INPUT);
        lastState[i] = -1;
    }
}

bool readSwitch(int i) {
    int raw = digitalRead(cfg.sw[i].gpio);
    return cfg.sw[i].activeLow ? (raw == LOW) : (raw == HIGH);
}

String stateTopic(int i) { return cfg.mqttBaseTopic + "/" + cfg.sw[i].name + "/state"; }
String availTopic()       { return cfg.mqttBaseTopic + "/availability"; }

void publishDiscovery(int i) {
    if (!cfg.haDiscovery) return;
    String dt = "homeassistant/binary_sensor/" + cfg.clientId + "_sw" + String(i) + "/config";
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
    JsonObject dev = doc.createNestedObject("device");
    dev["identifiers"][0]     = cfg.clientId;
    dev["name"]               = cfg.deviceName;
    dev["model"]              = "PoolLevel ESP8266";
    dev["sw_version"]         = FW_VERSION;
    String payload;
    serializeJson(doc, payload);
    mqtt.publish(dt.c_str(), payload.c_str(), true);
}

bool mqttConnect() {
    if (cfg.mqttHost.isEmpty()) return false;
    mqtt.setServer(cfg.mqttHost.c_str(), cfg.mqttPort);
    String lwt = availTopic();
    bool ok = cfg.mqttUser.length()
        ? mqtt.connect(cfg.clientId.c_str(), cfg.mqttUser.c_str(),
                       cfg.mqttPassword.c_str(), lwt.c_str(), 0, true, "offline")
        : mqtt.connect(cfg.clientId.c_str(), lwt.c_str(), 0, true, "offline");
    if (ok) {
        mqtt.publish(lwt.c_str(), "online", true);
        for (int i = 0; i < MAX_SWITCHES; i++)
            if (cfg.sw[i].enabled) publishDiscovery(i);
    }
    return ok;
}

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

bool connectWiFi() {
    if (cfg.wifiSSID.isEmpty()) return false;
    WiFi.mode(WIFI_STA);
    WiFi.begin(cfg.wifiSSID.c_str(), cfg.wifiPassword.c_str());
    ledTicker.attach(0.2f, ledToggle);
    uint32_t t = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t < WIFI_TIMEOUT_MS) delay(100);
    ledTicker.detach();
    bool ok = WiFi.status() == WL_CONNECTED;
    ok ? ledOn() : ledOff();
    return ok;
}

void startAP() {
    apMode = true;
    WiFi.mode(WIFI_AP);
    WiFi.softAP(AP_SSID, AP_PASSWORD);
    dns.start(DNS_PORT, "*", WiFi.softAPIP());
    ledTicker.attach(1.0f, ledToggle);
}

static void sendJson(const String &j, int c=200) { server.send(c,"application/json",j); }
static void sendOk()                              { sendJson(R"({"ok":true})"); }
static void sendFail(const char *e)               { sendJson(String(R"({"ok":false,"error":")") + e + "\"}"); }

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
        JsonObject sw = doc.createNestedObject("sw"+String(i));
        sw["name"]    = cfg.sw[i].name;
        sw["gpio"]    = cfg.sw[i].gpio;
        sw["actlow"]  = cfg.sw[i].activeLow;
        sw["enabled"] = cfg.sw[i].enabled;
    }
    String out; serializeJson(doc,out); sendJson(out);
}

void apiGetStatus() {
    DynamicJsonDocument doc(1024);
    doc["wifi_connected"] = (WiFi.status()==WL_CONNECTED);
    doc["wifi_ssid"]      = cfg.wifiSSID;
    doc["ip"]             = apMode ? WiFi.softAPIP().toString() : WiFi.localIP().toString();
    doc["mqtt_connected"] = mqtt.connected();
    doc["mqtt_host"]      = cfg.mqttHost;
    doc["ap_mode"]        = apMode;
    JsonArray arr = doc.createNestedArray("switches");
    for (int i = 0; i < MAX_SWITCHES; i++) {
        JsonObject s = arr.createNestedObject();
        s["name"]    = cfg.sw[i].name;
        s["gpio"]    = cfg.sw[i].gpio;
        s["enabled"] = cfg.sw[i].enabled;
        s["state"]   = cfg.sw[i].enabled && readSwitch(i);
    }
    String out; serializeJson(doc,out); sendJson(out);
}

void apiSaveWifi() {
    DynamicJsonDocument doc(512);
    if (deserializeJson(doc, server.arg("plain"))) { sendFail("parse error"); return; }
    cfg.wifiSSID     = doc["wifi_ssid"] | "";
    cfg.wifiPassword = doc["wifi_pass"] | "";
    if (!saveConfig(cfg)) { sendFail("LittleFS write failed"); return; }
    sendOk(); delay(400); ESP.restart();
}

void apiSaveMqtt() {
    DynamicJsonDocument doc(512);
    if (deserializeJson(doc, server.arg("plain"))) { sendFail("parse error"); return; }
    cfg.deviceName    = doc["device_name"] | cfg.deviceName;
    cfg.clientId      = doc["client_id"]   | cfg.clientId;
    cfg.mqttHost      = doc["mqtt_host"]   | "";
    cfg.mqttPort      = doc["mqtt_port"]   | 1883;
    cfg.mqttBaseTopic = doc["mqtt_topic"]  | "pool/level";
    cfg.mqttUser      = doc["mqtt_user"]   | "";
    cfg.mqttPassword  = doc["mqtt_pass"]   | "";
    cfg.haDiscovery   = doc["ha_discovery"]| true;
    saveConfig(cfg) ? sendOk() : sendFail("LittleFS write failed");
}

void apiSaveSwitches() {
    DynamicJsonDocument doc(1024);
    if (deserializeJson(doc, server.arg("plain"))) { sendFail("parse error"); return; }
    for (int i = 0; i < MAX_SWITCHES; i++) {
        JsonObject sw = doc["sw"+String(i)];
        cfg.sw[i].name      = sw["name"]   | cfg.sw[i].name;
        cfg.sw[i].gpio      = sw["gpio"]   | cfg.sw[i].gpio;
        cfg.sw[i].activeLow = sw["actlow"] | true;
        cfg.sw[i].enabled   = sw["enabled"]| false;
    }
    if (!saveConfig(cfg)) { sendFail("LittleFS write failed"); return; }
    sendOk(); delay(400); ESP.restart();
}

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
    String out; serializeJson(doc,out); sendJson(out);
}

void apiReset() {
    LittleFS.remove(CONFIG_FILE); sendOk(); delay(400); ESP.restart();
}

void handleNotFound() {
    server.sendHeader("Location","http://192.168.4.1/"); server.send(302);
}

void setup() {
    Serial.begin(115200);
    Serial.printf("\n\n=== PoolLevel v%s ===\n", FW_VERSION);
    pinMode(STATUS_LED_PIN, OUTPUT); ledOff();
    if (!LittleFS.begin()) {
        Serial.println(F("LittleFS mount failed – formatting…"));
        LittleFS.format();
        LittleFS.begin();
    }
    bool cfgOk = loadConfig(cfg);
    for (int i = 0; i < MAX_SWITCHES; i++) {
        if (cfg.sw[i].name.isEmpty()) cfg.sw[i].name = "Switch_"+String(i+1);
        if (cfg.sw[i].gpio == 0)      cfg.sw[i].gpio = DEFAULT_GPIO[i];
    }
    Serial.printf("Config: %s\n", cfgOk?"loaded":"defaults");
    setupGPIOs();
    if (!connectWiFi()) { Serial.println(F("WiFi failed → AP")); startAP(); }
    else Serial.printf("WiFi OK  IP: %s\n", WiFi.localIP().toString().c_str());

    webui_setup(server, cfg);
    server.on("/api/config",        HTTP_GET,  apiGetConfig);
    server.on("/api/status",        HTTP_GET,  apiGetStatus);
    server.on("/api/save/wifi",     HTTP_POST, apiSaveWifi);
    server.on("/api/save/mqtt",     HTTP_POST, apiSaveMqtt);
    server.on("/api/save/switches", HTTP_POST, apiSaveSwitches);
    server.on("/api/scan",          HTTP_GET,  apiScan);
    server.on("/api/reset",         HTTP_POST, apiReset);
    server.onNotFound(handleNotFound);
    server.begin();
    Serial.println(F("HTTP started"));
}

void loop() {
    if (apMode) dns.processNextRequest();
    server.handleClient();
    if (!apMode && !cfg.mqttHost.isEmpty()) {
        if (!mqtt.connected()) {
            uint32_t now = millis();
            if (now - lastMqttAttempt > MQTT_RECONNECT_MS) {
                lastMqttAttempt = now;
                if (mqttConnect()) {
                    Serial.println(F("MQTT connected"));
                    publishStates(true);
                    lastRepublish = millis();
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