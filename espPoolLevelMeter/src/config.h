#pragma once
#include <Arduino.h>

#define FW_VERSION        "2.1.0"
#define CONFIG_FILE       "/config.json"
#define MAX_SWITCHES      4
#define MIN_SWITCHES      2
#define AP_SSID           "PoolLevel-Setup"
#define AP_PASSWORD       "poolsetup"
#define DNS_PORT          53
#define HTTP_PORT         80
#define STATUS_LED_PIN    2
#define WIFI_TIMEOUT_MS   15000
#define MQTT_RECONNECT_MS 5000
#define REPUBLISH_MS      60000

static const int DEFAULT_GPIO[MAX_SWITCHES] = { 4, 5, 12, 14 };

struct SwitchConfig {
    int  gpio      = 0;
    bool activeLow = true;
};

struct AppConfig {
    String wifiSSID;
    String wifiPassword;
    String mqttHost;
    int    mqttPort      = 1883;
    String mqttUser;
    String mqttPassword;
    String mqttBaseTopic = "pool/level";
    String deviceName    = "PoolLevel";
    String clientId      = "poollevel";
    bool   haDiscovery   = true;
    int    numSwitches   = 2;
    SwitchConfig sw[MAX_SWITCHES];
};