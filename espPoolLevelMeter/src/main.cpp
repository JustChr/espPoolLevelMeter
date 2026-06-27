/*
  PoolLevel v2.2.0
  ─────────────────────────────────────────────────────────────────────────────
  Changes vs 2.1.0
  ─────────────────
  • WiFi reliability for always-on/mesh use:
      - modem sleep disabled (WIFI_NONE_SLEEP) so the AP never ages out a dozing client
      - explicit auto-reconnect + stable hostname (cfg.clientId)
      - loop watchdog: re-kicks WiFi.begin() when offline, reboots after 10 min down

  Changes 2.1.0 vs 2.0.0
  ─────────────────
  • 2–4 switches report ONE level state instead of N binary sensors
  • Level = count of active switches mapped to a zone label:
      2 switches → TOO_LOW / LOW / HIGH
      3 switches → TOO_LOW / LOW / OK / HIGH
      4 switches → TOO_LOW / LOW / OK / HIGH / OVERFLOW
  • HA discovery: single 'sensor' entity with named states
  • Switches tab simplified: just GPIO + logic + count selector
*/

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESPAsyncWebServer.h>
#include <ESP8266mDNS.h>
#include <DNSServer.h>
#include <AsyncJson.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <Ticker.h>

#include "config.h"

// Zone labels — index = number of active switches (0 = none, numSwitches = all)
static const char* LEVEL_LABELS_2[3] = { "TOO_LOW", "OK",  "HIGH" };
static const char* LEVEL_LABELS_3[4] = { "TOO_LOW", "LOW", "OK",   "HIGH" };
static const char* LEVEL_LABELS_4[5] = { "TOO_LOW", "LOW", "OK",   "HIGH", "TOO_HIGH" };

bool loadConfig(AppConfig &cfg);
bool saveConfig(const AppConfig &cfg);

AppConfig          cfg;
AsyncWebServer     server(HTTP_PORT);
AsyncEventSource   events("/events");
DNSServer          dns;
WiFiClient         wifiClient;
PubSubClient       mqtt(wifiClient);
Ticker             ledTicker;

bool     apMode          = false;
bool     restartPending  = false;
uint32_t restartAt       = 0;
uint32_t lastMqttAttempt = 0;
uint32_t lastRepublish   = 0;
uint32_t lastTelemetry   = 0;
uint32_t lastSseUpdate   = 0;
String   lastLevelState  = "";   // track last published state

void ledOn()     { digitalWrite(STATUS_LED_PIN, LOW); }
void ledOff()    { digitalWrite(STATUS_LED_PIN, HIGH); }
void ledToggle() { digitalWrite(STATUS_LED_PIN, !digitalRead(STATUS_LED_PIN)); }

void setupGPIOs() {
    for (int i = 0; i < cfg.numSwitches; i++)
        pinMode(cfg.sw[i].gpio, cfg.sw[i].activeLow ? INPUT_PULLUP : INPUT);
}

bool readSwitch(int i) {
    int raw = digitalRead(cfg.sw[i].gpio);
    return cfg.sw[i].activeLow ? (raw == LOW) : (raw == HIGH);
}

String computeLevel() {
    // Find the highest active switch
    int highest = -1;
    for (int i = 0; i < cfg.numSwitches; i++)
        if (readSwitch(i)) highest = i;

    // Validate bottom-up order: every switch below the highest
    // active one must also be active — otherwise it's a wiring/sensor fault.
    if (highest >= 0) {
        for (int i = 0; i < highest; i++) {
            if (!readSwitch(i)) {
                Serial.printf("SENSOR_ERROR: sw%d inactive but sw%d active\n", i, highest);
                return String("SENSOR_ERROR");
            }
        }
    }

    const char **labels;
    switch (cfg.numSwitches) {
        case 2:  labels = LEVEL_LABELS_2; break;
        case 3:  labels = LEVEL_LABELS_3; break;
        default: labels = LEVEL_LABELS_4; break;
    }

    // highest == -1 → TOO_LOW (index 0)
    // highest ==  0 → index 1
    // highest ==  n → index n+1 (top label)
    return String(labels[highest + 1]);
}

// ─── MQTT topics ──────────────────────────────────────────────────────────────
String stateTopic()     { return cfg.mqttBaseTopic + "/state"; }
String availTopic()     { return cfg.mqttBaseTopic + "/availability"; }
String telemetryTopic() { return cfg.mqttBaseTopic + "/telemetry"; }

// Shared HA "device" block so every entity groups under one device.
void addDeviceBlock(JsonDocument &doc) {
    JsonObject dev        = doc["device"].to<JsonObject>();
    dev["identifiers"][0] = cfg.clientId;
    dev["name"]           = cfg.deviceName;
    dev["model"]          = "PoolLevel ESP8266";
    dev["sw_version"]     = FW_VERSION;
}

// ─── HA discovery — single sensor entity ─────────────────────────────────────
void publishDiscovery() {
    if (!cfg.haDiscovery) return;
    String topic = "homeassistant/sensor/" + cfg.clientId + "/config";

    JsonDocument doc;
    doc["name"]                  = cfg.deviceName + " Level";
    doc["unique_id"]             = cfg.clientId + "_level";
    doc["state_topic"]           = stateTopic();
    doc["availability_topic"]    = availTopic();
    doc["payload_available"]     = "online";
    doc["payload_not_available"] = "offline";
    doc["icon"]                  = "mdi:water-percent";

    // Build options list based on numSwitches
    JsonArray opts = doc["options"].to<JsonArray>();
    const char **labels;
    int zones;
    switch (cfg.numSwitches) {
        case 2:  labels = LEVEL_LABELS_2; zones = 3; break;
        case 3:  labels = LEVEL_LABELS_3; zones = 4; break;
        default: labels = LEVEL_LABELS_4; zones = 5; break;
    }
    for (int i = 0; i < zones; i++) opts.add(labels[i]);
    opts.add("SENSOR_ERROR");   // HA needs to know this is a valid state value

    doc["device_class"] = "enum";

    addDeviceBlock(doc);

    String payload;
    serializeJson(doc, payload);
    mqtt.publish(topic.c_str(), payload.c_str(), true);
}

// ─── HA discovery — diagnostic telemetry sensors ─────────────────────────────
// Each reads a field from the shared telemetry JSON topic via value_template.
void publishTelemetryDiscovery() {
    if (!cfg.haDiscovery) return;

    struct TelemetrySensor {
        const char *key;        // suffix for unique_id/object_id + value_json field
        const char *name;       // friendly name
        const char *unit;       // unit_of_measurement (nullptr = none)
        const char *devClass;   // device_class (nullptr = none)
        const char *stateClass; // state_class, e.g. "measurement" (nullptr = none)
        const char *icon;       // icon (nullptr = none)
    };
    static const TelemetrySensor SENSORS[] = {
        { "rssi",       "WiFi Signal",        "dBm", "signal_strength", "measurement", nullptr },
        { "ssid",       "WiFi SSID",          nullptr, nullptr,         nullptr,       "mdi:wifi" },
        { "bssid",      "WiFi BSSID",         nullptr, nullptr,         nullptr,       "mdi:access-point" },
        { "channel",    "WiFi Channel",       nullptr, nullptr,         nullptr,       "mdi:wifi-settings" },
        { "ip",         "IP Address",         nullptr, nullptr,         nullptr,       "mdi:ip-network" },
        { "free_heap",  "Free Heap",          "B",   nullptr,          "measurement", "mdi:memory" },
        { "heap_frag",  "Heap Fragmentation", "%",   nullptr,          "measurement", "mdi:memory" },
        { "max_block",  "Largest Heap Block", "B",   nullptr,          "measurement", "mdi:memory" },
        { "uptime",     "Uptime",             "s",   "duration",       nullptr,       nullptr },
    };

    for (const auto &s : SENSORS) {
        String topic = "homeassistant/sensor/" + cfg.clientId + "_" + s.key + "/config";

        JsonDocument doc;
        doc["name"]                  = String(cfg.deviceName) + " " + s.name;
        doc["unique_id"]             = cfg.clientId + "_" + s.key;
        doc["state_topic"]           = telemetryTopic();
        doc["value_template"]        = String("{{ value_json.") + s.key + " }}";
        doc["availability_topic"]    = availTopic();
        doc["payload_available"]     = "online";
        doc["payload_not_available"] = "offline";
        doc["entity_category"]       = "diagnostic";
        if (s.unit)       doc["unit_of_measurement"] = s.unit;
        if (s.devClass)   doc["device_class"]        = s.devClass;
        if (s.stateClass) doc["state_class"]         = s.stateClass;
        if (s.icon)       doc["icon"]                = s.icon;

        addDeviceBlock(doc);

        String payload;
        serializeJson(doc, payload);
        mqtt.publish(topic.c_str(), payload.c_str(), true);
    }
}

void publishTelemetry() {
    if (!mqtt.connected()) return;
    JsonDocument doc;
    doc["rssi"]      = WiFi.RSSI();
    doc["ssid"]      = WiFi.SSID();
    doc["bssid"]     = WiFi.BSSIDstr();      // which mesh AP we're roamed onto
    doc["channel"]   = WiFi.channel();
    doc["ip"]        = WiFi.localIP().toString();
    doc["free_heap"] = ESP.getFreeHeap();
    doc["heap_frag"] = ESP.getHeapFragmentation();
    doc["max_block"] = ESP.getMaxFreeBlockSize();
    doc["uptime"]    = millis() / 1000;
    String payload;
    serializeJson(doc, payload);
    mqtt.publish(telemetryTopic().c_str(), payload.c_str(), true);
}

bool mqttConnect() {
    if (cfg.mqttHost.isEmpty()) return false;
    mqtt.setServer(cfg.mqttHost.c_str(), cfg.mqttPort);
    String lwt = availTopic();
    bool ok = cfg.mqttUser.length()
        ? mqtt.connect(cfg.clientId.c_str(),
                       cfg.mqttUser.c_str(), cfg.mqttPassword.c_str(),
                       lwt.c_str(), 0, true, "offline")
        : mqtt.connect(cfg.clientId.c_str(), lwt.c_str(), 0, true, "offline");
    if (ok) {
        mqtt.publish(lwt.c_str(), "online", true);
        publishDiscovery();
        publishTelemetryDiscovery();
    }
    return ok;
}

void publishLevel(bool force = false) {
    if (!mqtt.connected()) return;
    String level = computeLevel();
    if (force || level != lastLevelState) {
        mqtt.publish(stateTopic().c_str(), level.c_str(), true);
        lastLevelState = level;
    }
}

// ─── Status JSON (for SSE + /api/status) ─────────────────────────────────────
String buildStatusJson() {
    JsonDocument doc;
    doc["wifi_connected"] = (WiFi.status() == WL_CONNECTED);
    doc["wifi_ssid"]      = cfg.wifiSSID;
    doc["ip"]             = apMode ? WiFi.softAPIP().toString() : WiFi.localIP().toString();
    doc["mqtt_connected"] = mqtt.connected();
    doc["mqtt_host"]      = cfg.mqttHost;
    doc["ap_mode"]        = apMode;
    doc["mdns_hostname"]  = apMode ? "" : (cfg.clientId + ".local");
    doc["fw_version"]     = FW_VERSION;
    doc["level"]          = computeLevel();
    doc["num_switches"]   = cfg.numSwitches;

    // Raw switch states for UI indicators
    JsonArray arr = doc["switches"].to<JsonArray>();
    for (int i = 0; i < cfg.numSwitches; i++) {
        JsonObject s = arr.add<JsonObject>();
        s["gpio"]  = cfg.sw[i].gpio;
        s["state"] = readSwitch(i);
        // label for this switch position
        const char *lbl;
        if (cfg.numSwitches == 2) lbl = (i == 0) ? "TOO_LOW" : "HIGH";
        else if (cfg.numSwitches == 3) lbl = (i == 0) ? "TOO_LOW" : (i == 1) ? "LOW" : "HIGH";
        else lbl = (i == 0) ? "TOO_LOW" : (i == 1) ? "LOW" : (i == 2) ? "OK" : "HIGH";
        s["label"] = lbl;
    }

    String out; serializeJson(doc, out);
    return out;
}

void scheduleRestart(uint32_t ms = 600) {
    restartPending = true;
    restartAt = millis() + ms;
}

void setupMDNS() {
    if (MDNS.begin(cfg.clientId.c_str())) {
        MDNS.addService("http", "tcp", HTTP_PORT);
        Serial.printf("mDNS: http://%s.local\n", cfg.clientId.c_str());
    } else {
        Serial.println(F("mDNS start failed"));
    }
}

bool connectWiFi() {
    if (cfg.wifiSSID.isEmpty()) return false;
    WiFi.persistent(false);
    WiFi.mode(WIFI_STA);
    WiFi.setSleepMode(WIFI_NONE_SLEEP);   // always-on: avoids Fritz! dropping a dozing client
    WiFi.setAutoReconnect(true);
    WiFi.hostname(cfg.clientId.c_str());   // stable name in the Fritz! device list
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

void startAP() {
    apMode = true;
    WiFi.persistent(false);
    WiFi.mode(WIFI_AP);
    WiFi.softAP(AP_SSID, AP_PASSWORD);
    dns.start(DNS_PORT, "*", WiFi.softAPIP());
    ledTicker.attach(1.0f, ledToggle);
    Serial.printf("AP  SSID:%s  IP:%s\n", AP_SSID, WiFi.softAPIP().toString().c_str());
}

// Keeps the station link alive forever: kicks a fresh reconnect when the SDK's
// auto-reconnect wedges (common after Fritz! mesh band-steering), and reboots
// as a last resort if the link stays down past WIFI_REBOOT_MS.
void wifiWatchdog() {
    static uint32_t lastReconnect = 0;
    static uint32_t downSince     = 0;

    if (WiFi.status() == WL_CONNECTED) {
        downSince = 0;
        return;
    }

    uint32_t now = millis();
    if (downSince == 0) downSince = now;

    if (now - lastReconnect > WIFI_RECONNECT_MS) {
        lastReconnect = now;
        Serial.println(F("WiFi down -> reconnect"));
        WiFi.disconnect();
        WiFi.begin(cfg.wifiSSID.c_str(), cfg.wifiPassword.c_str());
    }

    if (now - downSince > WIFI_REBOOT_MS) {
        Serial.println(F("WiFi down too long -> restart"));
        ESP.restart();
    }
}

// ─── Web UI HTML ──────────────────────────────────────────────────────────────
// The UI is updated to show the level state prominently and the
// Switches tab now has a "Number of switches" selector + per-switch GPIO/logic.
static const char UI_HTML[] PROGMEM = R"HTML(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>PoolLevel Setup</title>
<style>
:root{
  --bg:#0f1117;--surface:#1a1d27;--s2:#22263a;--border:#2e3348;
  --text:#e8eaf0;--muted:#7880a0;--faint:#404660;
  --accent:#00b4cc;--ok:#22c55e;--err:#ef4444;--warn:#f59e0b;--r:10px;
}
*{box-sizing:border-box;margin:0;padding:0}
body{background:var(--bg);color:var(--text);font-family:'Segoe UI',system-ui,sans-serif;min-height:100vh;padding:16px}
header{display:flex;align-items:center;gap:12px;margin-bottom:20px;padding-bottom:14px;border-bottom:1px solid var(--border)}
.logo{width:38px;height:38px}
h1{font-size:1.2rem;font-weight:700}
h1 small{display:block;color:var(--muted);font-size:.76rem;font-weight:400;margin-top:2px}
.tabs{display:flex;gap:3px;background:var(--surface);border:1px solid var(--border);border-radius:var(--r);padding:4px;margin-bottom:18px;flex-wrap:wrap}
.tab{flex:1;min-width:60px;padding:7px 2px;border:none;background:transparent;color:var(--muted);cursor:pointer;border-radius:8px;font-size:.78rem;font-weight:500;transition:all .15s;white-space:nowrap}
.tab.active{background:var(--s2);color:var(--text)}
.tab:hover:not(.active){color:var(--text)}
.pane{display:none}.pane.active{display:block}
.card{background:var(--surface);border:1px solid var(--border);border-radius:var(--r);padding:16px;margin-bottom:14px}
.card-title{font-size:.88rem;font-weight:600;color:var(--accent);margin-bottom:12px;display:flex;align-items:center;gap:7px}
label{display:block;font-size:.78rem;color:var(--muted);margin-top:10px;margin-bottom:4px;font-weight:500}
label:first-of-type{margin-top:0}
input,select{width:100%;padding:8px 11px;background:var(--s2);border:1px solid var(--border);border-radius:7px;color:var(--text);font-size:.88rem;outline:none;transition:border-color .15s}
input:focus,select:focus{border-color:var(--accent)}
.row{display:grid;grid-template-columns:1fr 1fr;gap:8px}
.chk{display:flex;align-items:center;gap:7px;margin-top:10px}
.chk input{width:auto}.chk label{margin:0;color:var(--text)}
.btn{display:flex;align-items:center;justify-content:center;gap:6px;padding:9px 18px;border:none;border-radius:7px;font-size:.88rem;font-weight:600;cursor:pointer;transition:all .15s;width:100%;margin-top:10px}
.btn:disabled{opacity:.45;cursor:not-allowed}
.btn-p{background:var(--accent);color:#000}.btn-p:hover:not(:disabled){filter:brightness(1.1)}
.btn-s{background:var(--s2);color:var(--text);border:1px solid var(--border)}
.btn-d{background:var(--err);color:#fff}
.btns{display:flex;gap:8px}.btns .btn{margin-top:0}
.msg{padding:9px 13px;border-radius:7px;font-size:.83rem;margin-top:8px;display:none}
.msg.ok{background:#22c55e18;color:var(--ok);border:1px solid #22c55e33}
.msg.err{background:#ef444418;color:var(--err);border:1px solid #ef444433}
.info-row{display:flex;justify-content:space-between;align-items:center;padding:9px 13px;background:var(--s2);border:1px solid var(--border);border-radius:7px;margin-bottom:7px;font-size:.83rem}
.info-row .lbl{color:var(--muted)}
.chip{display:inline-block;padding:2px 8px;border-radius:20px;font-size:.72rem;font-weight:700}
.chip-ok{background:#22c55e18;color:var(--ok);border:1px solid #22c55e33}
.chip-err{background:#ef444418;color:var(--err);border:1px solid #ef444433}
/* Level display */
.level-display{text-align:center;padding:22px 16px;background:var(--s2);border:1px solid var(--border);border-radius:var(--r);margin-bottom:14px}
.level-value{font-size:2rem;font-weight:800;letter-spacing:.05em;margin-bottom:6px;transition:color .4s}
.level-TOO_LOW{color:var(--err)}
.level-LOW{color:var(--warn)}
.level-OK{color:var(--ok)}
.level-HIGH{color:var(--accent)}
.level-OVERFLOW{color:#a855f7}
.level-SENSOR_ERROR { color: var(--err); animation: blink 1s step-start infinite; }
@keyframes blink { 50% { opacity: 0; } }
/* Tank visualizer */
.tank-wrap{display:flex;justify-content:center;margin-bottom:14px}
.tank{width:80px;border:3px solid var(--border);border-top:none;border-radius:0 0 8px 8px;height:120px;position:relative;overflow:hidden;background:var(--s2)}
.tank-fill{position:absolute;bottom:0;left:0;right:0;transition:height .6s ease,background .4s;background:var(--accent)}
.tank-sw{position:absolute;left:-10px;right:-10px;height:2px;background:var(--border)}
.tank-sw-lbl{position:absolute;left:90px;font-size:.65rem;color:var(--muted);white-space:nowrap;transform:translateY(-50%)}
/* Switch indicators */
.sw-row{display:flex;gap:8px;flex-wrap:wrap;justify-content:center;margin-top:8px}
.sw-pill{display:flex;align-items:center;gap:6px;padding:5px 10px;border-radius:20px;font-size:.75rem;font-weight:600;border:1px solid var(--border);background:var(--s2)}
.sw-dot-sm{width:8px;height:8px;border-radius:50%}
.sw-dot-sm.on{background:var(--ok)}.sw-dot-sm.off{background:var(--err)}
/* Switch config forms */
.sw-sect{background:var(--s2);border:1px solid var(--border);border-radius:var(--r);padding:13px;margin-bottom:9px}
.sw-sect-hdr{font-size:.83rem;font-weight:600;display:flex;align-items:center;gap:7px;margin-bottom:10px}
.badge{background:var(--accent);color:#000;border-radius:20px;padding:1px 8px;font-size:.7rem;font-weight:700}
.badge-label{background:var(--s2);color:var(--muted);border:1px solid var(--border);border-radius:20px;padding:1px 8px;font-size:.7rem}
.net-item{display:flex;justify-content:space-between;align-items:center;padding:8px 11px;background:var(--s2);border:1px solid var(--border);border-radius:7px;margin-bottom:5px;cursor:pointer;font-size:.83rem;transition:border-color .15s}
.net-item:hover{border-color:var(--accent)}
code{background:var(--s2);padding:1px 6px;border-radius:4px;font-size:.85em}
#ota-drop{border:2px dashed var(--border);border-radius:var(--r);padding:28px;text-align:center;cursor:pointer;transition:border-color .15s;margin-bottom:12px}
#ota-drop:hover{border-color:var(--accent)}
.progress-wrap{display:none;margin-top:12px}
.progress-track{background:var(--s2);border-radius:20px;height:8px;overflow:hidden}
.progress-bar{height:100%;width:0%;background:var(--accent);transition:width .3s}
.progress-pct{font-size:.8rem;color:var(--muted);text-align:center;margin-top:6px}
.sse-dot{display:inline-block;width:7px;height:7px;border-radius:50%;background:var(--err);margin-right:5px;transition:background .3s}
.sse-dot.live{background:var(--ok)}
@media(max-width:480px){.row{grid-template-columns:1fr}.btns{flex-direction:column}}
</style>
</head>
<body>
<header>
  <svg class="logo" viewBox="0 0 38 38" fill="none">
    <rect width="38" height="38" rx="9" fill="#00b4cc1a"/>
    <rect x="7" y="26" width="24" height="4" rx="2" fill="#00b4cc"/>
    <rect x="7" y="20" width="24" height="4" rx="2" fill="#00b4cc88"/>
    <rect x="7" y="14" width="24" height="4" rx="2" fill="#00b4cc44"/>
    <rect x="7" y="8"  width="24" height="4" rx="2" fill="#00b4cc22"/>
    <circle cx="19" cy="28" r="2" fill="#fff"/>
  </svg>
  <h1>PoolLevel <small id="mode-badge"></small></h1>
</header>

<div class="tabs">
  <button class="tab active" onclick="showTab('status')">&#x1F4CA; Status</button>
  <button class="tab" onclick="showTab('wifi')">&#x1F4F6; WiFi</button>
  <button class="tab" onclick="showTab('mqtt')">&#x1F4E1; MQTT</button>
  <button class="tab" onclick="showTab('switches')">&#x1F50C; Switches</button>
  <button class="tab" onclick="showTab('ota')">&#x2B06; OTA</button>
</div>

<!-- STATUS -->
<div class="pane active" id="p-status">
  <div class="level-display">
    <div style="font-size:.75rem;color:var(--muted);margin-bottom:8px;letter-spacing:.08em;text-transform:uppercase">Tank Level</div>
    <div class="level-value" id="level-value">--</div>
  </div>
  <div class="tank-wrap"><div class="tank" id="tank-viz"></div></div>
  <div class="sw-row" id="sw-pills"></div>
  <div style="margin-top:14px" id="info-rows"></div>
  <div style="text-align:right;font-size:.72rem;color:var(--faint);margin-top:8px">
    <span class="sse-dot" id="sse-dot"></span>Live push &bull; FW <span id="fw-ver">--</span>
  </div>
</div>

<!-- WIFI -->
<div class="pane" id="p-wifi">
  <div class="card">
    <div class="card-title">&#x1F4F6; WiFi Credentials</div>
    <label>SSID</label>
    <input id="w-ssid" type="text" placeholder="Network name" autocomplete="off">
    <label>Password</label>
    <input id="w-pass" type="password" placeholder="WiFi password" autocomplete="off">
    <div class="btns" style="margin-top:12px">
      <button class="btn btn-p" onclick="saveWifi()">&#x1F4BE; Save &amp; Reboot</button>
      <button class="btn btn-s" id="scan-btn" onclick="scanWifi()">&#x1F50D; Scan</button>
    </div>
    <div id="net-list" style="margin-top:10px"></div>
    <div class="msg" id="wifi-msg"></div>
  </div>
</div>

<!-- MQTT -->
<div class="pane" id="p-mqtt">
  <div class="card">
    <div class="card-title">&#x1F3F7; Device Identity</div>
    <label>Device Name</label>
    <input id="m-dname" type="text" placeholder="PoolLevel">
    <label>MQTT Client ID &mdash; mDNS: <code id="mdns-hint">poollevel.local</code></label>
    <input id="m-cid" type="text" placeholder="poollevel" oninput="updateMdnsHint()">
  </div>
  <div class="card">
    <div class="card-title">&#x1F4E1; MQTT Broker</div>
    <label>Host / IP</label>
    <input id="m-host" type="text" placeholder="192.168.1.100">
    <div class="row">
      <div><label>Port</label><input id="m-port" type="number" placeholder="1883" min="1" max="65535"></div>
      <div><label>Base Topic</label><input id="m-topic" type="text" placeholder="pool/level"></div>
    </div>
    <label>Username (optional)</label><input id="m-user" type="text" autocomplete="off">
    <label>Password (optional)</label><input id="m-pass" type="password" autocomplete="off">
    <div class="chk"><input id="m-ha" type="checkbox"><label for="m-ha">Home Assistant Auto-Discovery</label></div>
    <button class="btn btn-p" onclick="saveMqtt()">&#x1F4BE; Save &amp; Reboot</button>
    <div class="msg" id="mqtt-msg"></div>
  </div>
</div>

<!-- SWITCHES -->
<div class="pane" id="p-switches">
  <div class="card">
    <div class="card-title">&#x1F50C; Switch Configuration</div>
    <label>Number of switches attached</label>
    <select id="num-sw" onchange="buildSwForms()">
      <option value="2">2 switches (TOO_LOW / HIGH)</option>
      <option value="3">3 switches (TOO_LOW / LOW / HIGH)</option>
      <option value="4">4 switches (TOO_LOW / LOW / OK / HIGH)</option>
    </select>
    <div style="margin-top:14px" id="sw-forms"></div>
    <p style="font-size:.75rem;color:var(--muted);margin-top:10px;line-height:1.5">
      &#x2139;&#xFE0F; Wire switches bottom-to-top of the tank in the order shown above.
      Switch 1 is the lowest (empty), last switch is the highest (full).
    </p>
    <button class="btn btn-p" onclick="saveSwitches()">&#x1F4BE; Save &amp; Reboot</button>
    <div class="msg" id="sw-msg"></div>
  </div>
  <div class="card">
    <div class="card-title" style="color:var(--err)">&#x26A0; Danger Zone</div>
    <p style="font-size:.8rem;color:var(--muted);margin-bottom:10px">Erase all settings and reboot into setup AP mode.</p>
    <button class="btn btn-d" onclick="doReset()">&#x1F5D1; Factory Reset</button>
  </div>
</div>

<!-- OTA -->
<div class="pane" id="p-ota">
  <div class="card">
    <div class="card-title">&#x2B06; HTTP Firmware Update</div>
    <p style="font-size:.82rem;color:var(--muted);margin-bottom:14px;line-height:1.6">
      Upload <code>.bin</code> from <code>.pio/build/d1_mini/firmware.bin</code>.<br>
      <strong style="color:var(--ok)">&#x2705; LittleFS untouched &mdash; settings survive.</strong>
    </p>
    <div id="ota-drop" onclick="document.getElementById('ota-file').click()"
         ondragover="event.preventDefault();this.style.borderColor='var(--accent)'"
         ondragleave="this.style.borderColor=''"
         ondrop="otaDrop(event)">
      <div style="font-size:2rem;margin-bottom:8px">&#x1F4E6;</div>
      <div id="ota-lbl" style="font-size:.85rem;color:var(--muted)">Click or drag &amp; drop <code>.bin</code></div>
    </div>
    <input type="file" id="ota-file" accept=".bin" style="display:none" onchange="otaFileChosen(this)">
    <button class="btn btn-p" id="ota-btn" onclick="doOta()" disabled>&#x2B06; Upload Firmware</button>
    <div class="progress-wrap" id="ota-progress">
      <div class="progress-track"><div class="progress-bar" id="ota-bar"></div></div>
      <div class="progress-pct" id="ota-pct">0%</div>
    </div>
    <div class="msg" id="ota-msg"></div>
  </div>
  <div class="card">
    <div class="card-title">&#x1F4BB; Device Info</div>
    <div class="info-row"><span class="lbl">mDNS</span><strong><a id="ota-mdns" href="#" style="color:var(--accent);text-decoration:none">--</a></strong></div>
    <div class="info-row"><span class="lbl">IP</span><strong id="ota-ip">--</strong></div>
    <div class="info-row"><span class="lbl">OTA path</span><code>/update</code></div>
  </div>
</div>

<script>
const TABS=['status','wifi','mqtt','switches','ota'];
const GPIOS=[
  {v:0,l:'GPIO0/D3'},{v:2,l:'GPIO2/D4'},{v:4,l:'GPIO4/D2'},
  {v:5,l:'GPIO5/D1'},{v:12,l:'GPIO12/D6'},{v:13,l:'GPIO13/D7'},
  {v:14,l:'GPIO14/D5'},{v:16,l:'GPIO16/D0'}
];
const SW_LABELS = {
    2: ['TOO_LOW', 'OK',  'HIGH'],
    3: ['TOO_LOW', 'LOW', 'OK',  'HIGH'],
    4: ['TOO_LOW', 'LOW', 'OK',  'HIGH', 'TOO_HIGH']
};
const LEVEL_COLOR = {
    'TOO_LOW':      'var(--err)',
    'LOW':          'var(--warn)',
    'OK':           'var(--ok)',
    'HIGH':         'var(--accent)',
    'TOO_HIGH':     '#a855f7',
    'SENSOR_ERROR': 'var(--err)'
};
const FILL_PCT = {
    'TOO_LOW':      3,
    'LOW':          30,
    'OK':           55,
    'HIGH':         80,
    'TOO_HIGH':     98,
    'SENSOR_ERROR': 0
};

function showTab(n){
  TABS.forEach((x,i)=>{
    document.querySelectorAll('.tab')[i].classList.toggle('active',x===n);
    document.getElementById('p-'+x).classList.toggle('active',x===n);
  });
}
async function api(path,method,body){
  const o={method:method||'GET',headers:{'Content-Type':'application/json'}};
  if(body!==undefined) o.body=JSON.stringify(body);
  return (await fetch(path,o)).json();
}
function showMsg(id,ok,text){
  const el=document.getElementById(id);
  el.className='msg '+(ok?'ok':'err');
  el.textContent=text;el.style.display='block';
  setTimeout(()=>el.style.display='none',5000);
}

const es=new EventSource('/events');
const sseDot=document.getElementById('sse-dot');
es.addEventListener('status',e=>{sseDot.className='sse-dot live';updateStatusUI(JSON.parse(e.data));});
es.onerror=()=>sseDot.className='sse-dot';

function updateStatusUI(r){
  document.getElementById('mode-badge').textContent=r.ap_mode?'AP Mode':'';
  document.getElementById('fw-ver').textContent=r.fw_version||'--';

  // Level
  const lv = r.level || '--';
  const lvEl = document.getElementById('level-value');
  lvEl.textContent = lv === 'SENSOR_ERROR' ? '⚠ SENSOR ERROR' : lv;
  lvEl.className = 'level-value level-' + lv;

  // Tank fill
  const pct=FILL_PCT[lv]||0;
  const color=LEVEL_COLOR[lv]||'var(--accent)';
  const tank=document.getElementById('tank-viz');
  const numSw=r.num_switches||2;
  // Rebuild tank lines + fill
  let tankHtml='<div class="tank-fill" style="height:'+pct+'%;background:'+color+'"></div>';
  const sws=r.switches||[];
  sws.forEach((s,i)=>{
    const bot=Math.round((i+1)/(numSw+1)*100);
    tankHtml+='<div class="tank-sw" style="bottom:'+bot+'%"></div>'+
      '<div class="tank-sw-lbl" style="bottom:'+bot+'%">'+s.label+'</div>';
  });
  tank.innerHTML=tankHtml;

  // Switch pills
  document.getElementById('sw-pills').innerHTML=(r.switches||[]).map((s,i)=>
    '<div class="sw-pill">'+
      '<div class="sw-dot-sm '+(s.state?'on':'off')+'"></div>'+
      s.label+' <span style="color:var(--faint);font-size:.68rem">GPIO'+s.gpio+'</span>'+
    '</div>'
  ).join('');

  // Info rows
  const mdns=r.mdns_hostname||'';
  document.getElementById('info-rows').innerHTML=
    infoRow('WiFi',r.wifi_ssid||'--',r.wifi_connected)+
    infoRow('IP','<strong>'+(r.ip||'--')+'</strong>')+
    infoRow('mDNS',mdns?'<a href="http://'+mdns+'" style="color:var(--accent);text-decoration:none">'+mdns+'</a>':'--')+
    infoRow('MQTT',(r.mqtt_host||'--'),r.mqtt_connected);

  document.getElementById('ota-ip').textContent=r.ip||'--';
  if(mdns){const a=document.getElementById('ota-mdns');a.textContent=mdns;a.href='http://'+mdns;}
}

function infoRow(l,v,conn){
  let chip='';
  if(conn!==undefined) chip=' <span class="chip '+(conn?'chip-ok':'chip-err')+'">'+(conn?'Online':'Offline')+'</span>';
  return '<div class="info-row"><span class="lbl">'+l+'</span><div>'+v+chip+'</div></div>';
}

async function load(){
  cfg=await api('/api/config');
  document.getElementById('w-ssid').value =cfg.wifi_ssid ||'';
  document.getElementById('w-pass').value =cfg.wifi_pass ||'';
  document.getElementById('m-dname').value=cfg.device_name||'PoolLevel';
  document.getElementById('m-cid').value  =cfg.client_id ||'poollevel';
  document.getElementById('m-host').value =cfg.mqtt_host ||'';
  document.getElementById('m-port').value =cfg.mqtt_port ||1883;
  document.getElementById('m-topic').value=cfg.mqtt_topic||'pool/level';
  document.getElementById('m-user').value =cfg.mqtt_user ||'';
  document.getElementById('m-pass').value =cfg.mqtt_pass ||'';
  document.getElementById('m-ha').checked =cfg.ha_discovery!==false;
  const ns=document.getElementById('num-sw');
  ns.value=cfg.num_switches||2;
  updateMdnsHint();
  buildSwForms();
  api('/api/status').then(updateStatusUI).catch(()=>{});
}

function updateMdnsHint(){
  document.getElementById('mdns-hint').textContent=
    (document.getElementById('m-cid').value||'poollevel')+'.local';
}

function gpioSelect(id,val){
  return '<select id="'+id+'">'+GPIOS.map(g=>
    '<option value="'+g.v+'"'+(g.v==val?' selected':'')+'>'+g.l+'</option>').join('')+'</select>';
}

function buildSwForms(){
  const n=parseInt(document.getElementById('num-sw').value)||2;
  const labels=SW_LABELS[n]||SW_LABELS[2];
  let h='';
  for(let i=0;i<n;i++){
    const s=cfg['sw'+i]||{};
    const dfltGpio=[4,5,12,14][i];
    h+='<div class="sw-sect">'+
      '<div class="sw-sect-hdr">'+
        '<span class="badge">'+(i+1)+'</span>'+
        '<span class="badge-label">'+labels[i]+'</span>'+
        '<span style="color:var(--faint);font-size:.72rem;margin-left:auto">Switch '+(i+1)+' (bottom→top)</span>'+
      '</div>'+
      '<div class="row">'+
        '<div><label>GPIO</label>'+gpioSelect('s'+i+'gpio',s.gpio||dfltGpio)+'</div>'+
        '<div><label>Logic</label>'+
          '<select id="s'+i+'al">'+
            '<option value="1"'+(s.actlow!==false?' selected':'')+'>Active LOW (pull-up)</option>'+
            '<option value="0"'+(s.actlow===false?' selected':'')+'>Active HIGH</option>'+
          '</select>'+
        '</div>'+
      '</div>'+
    '</div>';
  }
  document.getElementById('sw-forms').innerHTML=h;
}

async function saveWifi(){
  const r=await api('/api/save/wifi','POST',{
    wifi_ssid:document.getElementById('w-ssid').value,
    wifi_pass:document.getElementById('w-pass').value});
  showMsg('wifi-msg',r.ok,r.ok?'Saved - rebooting...':'Error: '+r.error);
}

async function scanWifi(){
  const btn=document.getElementById('scan-btn');
  btn.disabled=true;
  document.getElementById('net-list').innerHTML=
    '<div style="font-size:.8rem;color:var(--muted);padding:5px">Scanning...</div>';
  await api('/api/scan');
  for(let i=0;i<8;i++){
    await new Promise(r=>setTimeout(r,1000));
    const r=await api('/api/scan');
    if(!r.scanning){
      const nets=r.networks||[];
      document.getElementById('net-list').innerHTML=nets.length
        ?nets.map(n=>'<div class="net-item" onclick="document.getElementById(\'w-ssid\').value=\''+
          n.ssid.replace(/'/g,"\\'")+'\'">'+'<span>&#x1F4F6; '+n.ssid+'</span>'+
          '<span style="color:var(--muted);font-size:.75rem">'+n.rssi+' dBm '+(n.enc?'&#x1F512;':'')+'</span></div>').join('')
        :'<div style="font-size:.8rem;color:var(--muted)">No networks found.</div>';
      btn.disabled=false;return;
    }
  }
  document.getElementById('net-list').innerHTML=
    '<div style="font-size:.8rem;color:var(--err)">Scan timed out.</div>';
  btn.disabled=false;
}

async function saveMqtt(){
  const r=await api('/api/save/mqtt','POST',{
    device_name:document.getElementById('m-dname').value,
    client_id:document.getElementById('m-cid').value,
    mqtt_host:document.getElementById('m-host').value,
    mqtt_port:parseInt(document.getElementById('m-port').value)||1883,
    mqtt_topic:document.getElementById('m-topic').value,
    mqtt_user:document.getElementById('m-user').value,
    mqtt_pass:document.getElementById('m-pass').value,
    ha_discovery:document.getElementById('m-ha').checked});
  showMsg('mqtt-msg',r.ok,r.ok?'Saved - rebooting...':'Error: '+r.error);
}

async function saveSwitches(){
  const n=parseInt(document.getElementById('num-sw').value)||2;
  const body={num_switches:n};
  for(let i=0;i<n;i++) body['sw'+i]={
    gpio:parseInt(document.getElementById('s'+i+'gpio').value),
    actlow:document.getElementById('s'+i+'al').value==='1'};
  const r=await api('/api/save/switches','POST',body);
  showMsg('sw-msg',r.ok,r.ok?'Saved - rebooting...':'Error: '+r.error);
}

async function doReset(){
  if(!confirm('Erase ALL settings?')) return;
  await api('/api/reset','POST');
  alert('Reset done. Connect to "PoolLevel-Setup" (password: poolsetup).');
}

function otaFileChosen(i){
  const f=i.files[0];if(!f)return;
  document.getElementById('ota-lbl').textContent=f.name+' ('+(f.size/1024).toFixed(1)+' KB)';
  document.getElementById('ota-drop').style.borderColor='var(--accent)';
  document.getElementById('ota-btn').disabled=false;
}
function otaDrop(e){
  e.preventDefault();document.getElementById('ota-drop').style.borderColor='';
  const dt=e.dataTransfer;
  if(dt&&dt.files.length){try{document.getElementById('ota-file').files=dt.files;}catch(_){}otaFileChosen({files:dt.files});}
}
function doOta(){
  const file=document.getElementById('ota-file').files[0];if(!file)return;
  const fd=new FormData();fd.append('firmware',file);
  const xhr=new XMLHttpRequest();xhr.open('POST','/update');
  document.getElementById('ota-progress').style.display='block';
  document.getElementById('ota-btn').disabled=true;
  xhr.upload.onprogress=e=>{if(e.lengthComputable){const p=Math.round(e.loaded*100/e.total);
    document.getElementById('ota-bar').style.width=p+'%';
    document.getElementById('ota-pct').textContent=p+'%';}};
  xhr.onload=()=>{const ok=xhr.status===200;
    showMsg('ota-msg',ok,ok?'Update OK - rebooting...':'Failed: '+xhr.responseText);
    if(!ok)document.getElementById('ota-btn').disabled=false;};
  xhr.onerror=()=>{showMsg('ota-msg',false,'Network error.');
    document.getElementById('ota-btn').disabled=false;};
  xhr.send(fd);
}
load();
</script>
</body>
</html>
)HTML";

// ─── HTTP OTA ─────────────────────────────────────────────────────────────────
void setupHttpOTA() {
    server.on("/update", HTTP_GET, [](AsyncWebServerRequest *req) {
        req->send(200, "text/html",
            "<form method='POST' action='/update' enctype='multipart/form-data'>"
            "<input type='file' name='firmware'><input type='submit' value='Upload'></form>");
    });
    server.on("/update", HTTP_POST,
        [](AsyncWebServerRequest *req) {
            bool ok = !Update.hasError();
            String msg = ok ? "OK" : ("Update failed: " + Update.getErrorString());
            AsyncWebServerResponse *r =
                req->beginResponse(ok ? 200 : 500, "text/plain", msg);
            r->addHeader("Connection","close");
            req->send(r);
            if (ok) { delay(200); ESP.restart(); }
        },
        [](AsyncWebServerRequest *req, String filename, size_t index,
           uint8_t *data, size_t len, bool final) {
            if (!index) {
                Serial.printf("OTA start: %s\n", filename.c_str());
                if (mqtt.connected()) {
                    mqtt.publish(availTopic().c_str(), "offline", true);
                    mqtt.disconnect();
                }
                Update.runAsync(true);
                if (!Update.begin((ESP.getFreeSketchSpace()-0x1000)&0xFFFFF000)) {
                    Serial.print(F("OTA begin failed: ")); Update.printError(Serial);
                }
            }
            if (!Update.hasError() && Update.write(data, len) != len) {
                Serial.print(F("OTA write failed: ")); Update.printError(Serial);
            }
            if (final) {
                if (!Update.hasError() && Update.end(true))
                    Serial.printf("OTA success: %u bytes\n", index + len);
                else {
                    Serial.print(F("OTA end failed: ")); Update.printError(Serial);
                }
            }
        });
}

// ─── Routes ───────────────────────────────────────────────────────────────────
void setupRoutes() {
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *req) {
        req->send_P(200, "text/html", UI_HTML);
    });

    server.on("/api/config", HTTP_GET, [](AsyncWebServerRequest *req) {
        JsonDocument doc;
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
        doc["num_switches"] = cfg.numSwitches;
        for (int i = 0; i < MAX_SWITCHES; i++) {
            JsonObject sw = doc["sw"+String(i)].to<JsonObject>();
            sw["gpio"]   = cfg.sw[i].gpio;
            sw["actlow"] = cfg.sw[i].activeLow;
        }
        String out; serializeJson(doc, out);
        req->send(200, "application/json", out);
    });

    server.on("/api/status", HTTP_GET, [](AsyncWebServerRequest *req) {
        req->send(200, "application/json", buildStatusJson());
    });

    server.on("/api/scan", HTTP_GET, [](AsyncWebServerRequest *req) {
        int n = WiFi.scanComplete();
        if (n == WIFI_SCAN_FAILED || n == 0) {
            WiFi.scanNetworks(true);
            req->send(200,"application/json",F("{\"scanning\":true,\"networks\":[]}"));
            return;
        }
        if (n == WIFI_SCAN_RUNNING) {
            req->send(200,"application/json",F("{\"scanning\":true,\"networks\":[]}"));
            return;
        }
        JsonDocument doc;
        doc["scanning"] = false;
        JsonArray arr = doc["networks"].to<JsonArray>();
        for (int i = 0; i < n; i++) {
            JsonObject net = arr.add<JsonObject>();
            net["ssid"] = WiFi.SSID(i);
            net["rssi"] = WiFi.RSSI(i);
            net["enc"]  = (WiFi.encryptionType(i) != ENC_TYPE_NONE);
        }
        WiFi.scanDelete();
        String out; serializeJson(doc, out);
        req->send(200, "application/json", out);
    });

    auto *wifiH = new AsyncCallbackJsonWebHandler("/api/save/wifi",
        [](AsyncWebServerRequest *req, JsonVariant &j) {
            cfg.wifiSSID     = j["wifi_ssid"] | "";
            cfg.wifiPassword = j["wifi_pass"] | "";
            bool ok = saveConfig(cfg);
            req->send(200,"application/json", ok?"{\"ok\":true}":"{\"ok\":false,\"error\":\"save failed\"}");
            if (ok) scheduleRestart();
        });
    server.addHandler(wifiH);

    auto *mqttH = new AsyncCallbackJsonWebHandler("/api/save/mqtt",
        [](AsyncWebServerRequest *req, JsonVariant &j) {
            cfg.deviceName    = j["device_name"] | cfg.deviceName;
            cfg.clientId      = j["client_id"]   | cfg.clientId;
            cfg.mqttHost      = j["mqtt_host"]   | "";
            cfg.mqttPort      = j["mqtt_port"]   | 1883;
            cfg.mqttBaseTopic = j["mqtt_topic"]  | "pool/level";
            cfg.mqttUser      = j["mqtt_user"]   | "";
            cfg.mqttPassword  = j["mqtt_pass"]   | "";
            cfg.haDiscovery   = j["ha_discovery"]| true;
            bool ok = saveConfig(cfg);
            req->send(200,"application/json", ok?"{\"ok\":true}":"{\"ok\":false,\"error\":\"save failed\"}");
            if (ok) scheduleRestart();
        });
    server.addHandler(mqttH);

    auto *swH = new AsyncCallbackJsonWebHandler("/api/save/switches",
        [](AsyncWebServerRequest *req, JsonVariant &j) {
            int n = j["num_switches"] | 2;
            if (n < MIN_SWITCHES) n = MIN_SWITCHES;
            if (n > MAX_SWITCHES) n = MAX_SWITCHES;
            cfg.numSwitches = n;
            for (int i = 0; i < n; i++) {
                JsonObject sw    = j["sw"+String(i)];
                cfg.sw[i].gpio      = sw["gpio"]   | DEFAULT_GPIO[i];
                cfg.sw[i].activeLow = sw["actlow"] | true;
            }
            bool ok = saveConfig(cfg);
            req->send(200,"application/json", ok?"{\"ok\":true}":"{\"ok\":false,\"error\":\"save failed\"}");
            if (ok) scheduleRestart();
        });
    server.addHandler(swH);

    server.on("/api/reset", HTTP_POST, [](AsyncWebServerRequest *req) {
        LittleFS.remove(CONFIG_FILE);
        req->send(200,"application/json","{\"ok\":true}");
        scheduleRestart();
    });

    server.onNotFound([](AsyncWebServerRequest *req) {
        req->redirect("http://192.168.4.1/");
    });

    events.onConnect([](AsyncEventSourceClient *client) {
        client->send(buildStatusJson().c_str(), "status", millis(), 1000);
    });
    server.addHandler(&events);
    setupHttpOTA();
}

// ═══════════════════════════════════════════════════════════════════════════════
void setup() {
    Serial.begin(115200);
    Serial.printf("\n\n=== PoolLevel v%s ===\n", FW_VERSION);

    pinMode(STATUS_LED_PIN, OUTPUT);
    ledOff();

    if (!LittleFS.begin()) {
        Serial.println(F("LittleFS mount failed - formatting..."));
        LittleFS.format();
        LittleFS.begin();
    }

    if (!loadConfig(cfg)) Serial.println(F("No config - using defaults"));

    for (int i = 0; i < MAX_SWITCHES; i++)
        if (cfg.sw[i].gpio == 0) cfg.sw[i].gpio = DEFAULT_GPIO[i];

    setupGPIOs();

    mqtt.setKeepAlive(60);
    mqtt.setSocketTimeout(10);
    mqtt.setBufferSize(1024);   // discovery configs (esp. RSSI) exceed the 256/512 default

    if (!connectWiFi()) {
        Serial.println(F("WiFi failed -> AP mode"));
        startAP();
    } else {
        Serial.printf("WiFi OK  IP: %s\n", WiFi.localIP().toString().c_str());
        setupMDNS();
        if (!cfg.mqttHost.isEmpty())
            mqtt.setServer(cfg.mqttHost.c_str(), cfg.mqttPort);
    }

    setupRoutes();
    server.begin();
    Serial.printf("HTTP started  switches:%d  level:%s\n",
                  cfg.numSwitches, computeLevel().c_str());
}

// ═══════════════════════════════════════════════════════════════════════════════
void loop() {
    if (apMode) dns.processNextRequest();
    if (!apMode) { wifiWatchdog(); MDNS.update(); }

    if (restartPending && millis() > restartAt) ESP.restart();

    if (!apMode && !cfg.mqttHost.isEmpty()) {
        if (!mqtt.connected()) {
            uint32_t now = millis();
            if (now - lastMqttAttempt > MQTT_RECONNECT_MS) {
                lastMqttAttempt = now;
                if (mqttConnect()) {
                    Serial.println(F("MQTT connected"));
                    publishLevel(true);
                    publishTelemetry();
                    lastRepublish = millis();
                    lastTelemetry = millis();
                } else {
                    Serial.printf("MQTT failed (state=%d)\n", mqtt.state());
                }
            }
        } else {
            mqtt.loop();
            uint32_t now = millis();
            bool force = (now - lastRepublish > REPUBLISH_MS);
            publishLevel(force);
            if (force) lastRepublish = now;

            if (now - lastTelemetry > TELEMETRY_MS) {
                lastTelemetry = now;
                publishTelemetry();
            }
        }
    }

    if (!apMode && millis() - lastSseUpdate > 2000) {
        lastSseUpdate = millis();
        events.send(buildStatusJson().c_str(), "status", millis());
    }
}