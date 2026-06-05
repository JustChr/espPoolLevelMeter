#pragma once
#include <Arduino.h>

#define FW_VERSION        "1.2.0"
#define CONFIG_FILE       "/config.json"
#define MAX_SWITCHES      4
#define AP_SSID           "PoolLevel-Setup"
#define AP_PASSWORD       "poolsetup"
#define DNS_PORT          53
#define HTTP_PORT         80
#define STATUS_LED_PIN    2           // GPIO2 onboard LED, active LOW
#define WIFI_TIMEOUT_MS   15000
#define MQTT_RECONNECT_MS 5000
#define REPUBLISH_MS      60000
#define OTA_PASSWORD      "ota-password"   // change before deploying!
#define OTA_PORT          8266

// Default GPIO assignments (NodeMCU / Wemos D1 mini safe pins)
static const int DEFAULT_GPIO[MAX_SWITCHES] = { 4, 5, 12, 14 };

struct SwitchConfig {
    int    gpio      = 0;
    String name      = "";
    bool   activeLow = true;    // true → INPUT_PULLUP, closed = LOW = ON
    bool   enabled   = false;
};

struct AppConfig {
    // WiFi
    String wifiSSID;
    String wifiPassword;
    // MQTT
    String mqttHost;
    int    mqttPort      = 1883;
    String mqttUser;
    String mqttPassword;
    String mqttBaseTopic = "pool/level";
    String deviceName    = "PoolLevel";
    // clientId is also the mDNS hostname → <clientId>.local
    String clientId      = "poollevel";
    bool   haDiscovery   = true;
    // Switches
    SwitchConfig sw[MAX_SWITCHES];
};