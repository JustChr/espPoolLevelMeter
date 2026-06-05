#include "config.h"
#include <ArduinoJson.h>
#include <LittleFS.h>

bool loadConfig(AppConfig &cfg) {
    if (!LittleFS.exists(CONFIG_FILE)) return false;
    File f = LittleFS.open(CONFIG_FILE, "r");
    if (!f) return false;
    JsonDocument doc;
    if (deserializeJson(doc, f)) { f.close(); return false; }
    f.close();

    cfg.wifiSSID      = doc["wifi_ssid"]    | "";
    cfg.wifiPassword  = doc["wifi_pass"]    | "";
    cfg.mqttHost      = doc["mqtt_host"]    | "";
    cfg.mqttPort      = doc["mqtt_port"]    | 1883;
    cfg.mqttUser      = doc["mqtt_user"]    | "";
    cfg.mqttPassword  = doc["mqtt_pass"]    | "";
    cfg.mqttBaseTopic = doc["mqtt_topic"]   | "pool/level";
    cfg.deviceName    = doc["device_name"]  | "PoolLevel";
    cfg.clientId      = doc["client_id"]    | "poollevel";
    cfg.haDiscovery   = doc["ha_discovery"] | true;
    cfg.numSwitches   = doc["num_switches"] | 2;
    // clamp to valid range
    if (cfg.numSwitches < MIN_SWITCHES) cfg.numSwitches = MIN_SWITCHES;
    if (cfg.numSwitches > MAX_SWITCHES) cfg.numSwitches = MAX_SWITCHES;

    for (int i = 0; i < MAX_SWITCHES; i++) {
        String k = "sw" + String(i);
        cfg.sw[i].gpio      = doc[k + "_gpio"]   | DEFAULT_GPIO[i];
        cfg.sw[i].activeLow = doc[k + "_actlow"] | true;
    }
    return true;
}

bool saveConfig(const AppConfig &cfg) {
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
        String k = "sw" + String(i);
        doc[k + "_gpio"]   = cfg.sw[i].gpio;
        doc[k + "_actlow"] = cfg.sw[i].activeLow;
    }

    File f = LittleFS.open(CONFIG_FILE, "w");
    if (!f) return false;
    serializeJson(doc, f);
    f.close();
    return true;
}