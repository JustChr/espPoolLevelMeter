# PoolLevel ESP8266

> MQTT-connected pool expansion tank level monitor — up to 4 float switches, Home Assistant auto-discovery, captive-portal setup UI.

![Platform](https://img.shields.io/badge/platform-ESP8266-blue)
![Framework](https://img.shields.io/badge/framework-Arduino%20%2F%20PlatformIO-orange)
![HA](https://img.shields.io/badge/Home%20Assistant-MQTT%20Discovery-41BDF5)
![License](https://img.shields.io/badge/license-MIT-green)

---

## Overview

PoolLevel monitors the water level of a pool expansion/buffer tank using 2–4 vertical float switches wired to an ESP8266. It publishes a **single named state** to MQTT (`TOO_LOW` / `OK` / `HIGH` etc.) and registers itself automatically with Home Assistant via MQTT discovery. Configuration is done entirely through a browser-based UI — no flashing required after initial upload.

---

## Features

- **2–4 float switches** — configurable count, each mapped to any safe GPIO
- **Single MQTT state sensor** — named states instead of raw binary values
- **Bottom-up validation** — detects wiring/sensor faults if switches don't trigger in order, publishes `SENSOR_ERROR`
- **Home Assistant auto-discovery** — appears as a named `sensor` entity automatically
- **Captive-portal setup** — spawns a WiFi access point with a full config UI if no credentials are stored
- **Async HTTP server** — UI never blocks MQTT or switch polling (fixes lwIP slot exhaustion)
- **SSE live push** — status page updates in real time without polling
- **HTTP OTA** — drag-and-drop firmware update from the UI, LittleFS config survives
- **mDNS** — reachable at `http://<clientid>.local`
- **Factory reset** — one button in UI erases config and returns to AP mode

---

## Hardware

### Recommended board

**Wemos D1 Mini** (or any ESP8266 with ≥4 free GPIOs)

### Default GPIO mapping

| Switch # | Position in tank | Default GPIO | D1 Mini pin |
|----------|-----------------|--------------|-------------|
| 1        | Bottom (TOO_LOW) | GPIO 4      | D2          |
| 2        | —               | GPIO 5       | D1          |
| 3        | —               | GPIO 12      | D6          |
| 4        | Top             | GPIO 14      | D5          |

All GPIOs are remappable in the setup UI.

### Wiring (active-LOW default)

```
GND ──[float switch]── GPIOx
```

Each switch is wired between GND and its GPIO pin. The firmware uses `INPUT_PULLUP` by default — float closed = LOW = water reached this level. Active-HIGH mode is also selectable per switch in the UI.

> **Important:** Wire switches in physical order, bottom to top of the tank. Switch 1 must be the lowest, Switch N the highest.

---

## Level States

The firmware derives a single level state from how many switches are active, after validating that they triggered in the correct bottom-up order.

| Active switches | 2-switch setup | 3-switch setup | 4-switch setup |
|:-:|---|---|---|
| 0 | `TOO_LOW` | `TOO_LOW` | `TOO_LOW` |
| 1 | `OK`      | `LOW`     | `LOW`     |
| 2 | `HIGH`    | `OK`      | `OK`      |
| 3 | —         | `HIGH`    | `HIGH`    |
| 4 | —         | —         | `TOO_HIGH`|
| gap detected | `SENSOR_ERROR` | `SENSOR_ERROR` | `SENSOR_ERROR` |

`SENSOR_ERROR` is published when a higher switch is active but a lower one is not — indicating a wiring fault, failed switch, or physical impossibility.

---

## MQTT Topics

| Topic | Direction | Retained | Description |
|-------|-----------|----------|-------------|
| `<base>/state` | publish | ✅ | Current level state |
| `<base>/availability` | publish | ✅ | `online` / `offline` (LWT) |
| `homeassistant/sensor/<clientId>/config` | publish | ✅ | HA discovery payload |

Default base topic: `pool/level`

---

## Home Assistant

The device registers itself automatically via MQTT discovery. A single `sensor` entity appears under the device with:

- **Device class:** `enum`
- **Icon:** `mdi:water-percent`
- **Options:** all valid states for the configured switch count + `SENSOR_ERROR`

No manual HA configuration needed. To use in automations:

```yaml
trigger:
  - platform: state
    entity_id: sensor.poollevel_level
    to: "TOO_LOW"
action:
  - service: notify.mobile_app
    data:
      message: "Pool expansion tank is empty!"
```

---

## Software Stack

| Library | Version | Purpose |
|---------|---------|---------|
| `ESPAsyncWebServer` | ^3.7 | Non-blocking HTTP server |
| `PubSubClient` | ^2.8 | MQTT client |
| `ArduinoJson` | ^7.0 | JSON config serialisation |
| `ESP8266mDNS` | built-in | `<clientid>.local` hostname |
| `LittleFS` | built-in | Config persistence |

> **Why ESPAsyncWebServer?** The ESP8266 lwIP stack has only 5 TCP/UDP PCB slots. `ESP8266WebServer` + mDNS exhausts them, leaving no free slot for the MQTT TCP connection (manifests as `state=-4` timeout). `ESPAsyncWebServer` uses far fewer slots, solving this completely.

---

## Getting Started

### 1. Prerequisites

- [PlatformIO](https://platformio.org/) (VS Code extension or CLI)
- ESP8266 board (Wemos D1 Mini recommended)

### 2. Clone & build

```bash
git clone https://github.com/<your-username>/poollevel-esp8266.git
cd poollevel-esp8266
pio run
```

### 3. Flash

```bash
pio run --target upload
pio run --target uploadfs   # upload LittleFS (first time only)
```

### 4. First-time setup

1. Power on the board — it will start as WiFi AP **`PoolLevel-Setup`** (password: `poolsetup`)
2. Connect your phone or laptop to that network
3. Open **`http://192.168.4.1`** (or any URL — captive portal redirects automatically)
4. Configure **WiFi**, **MQTT**, and **Switches** tabs
5. Click **Save & Reboot**
6. Device connects to your network, registers with Home Assistant

### 5. Subsequent updates

Navigate to the **OTA** tab in the UI, drag your `.pio/build/d1_mini/firmware.bin` onto the upload area. Settings are preserved.

---

## Project Structure

```
poollevel-esp8266/
├── platformio.ini
└── src/
    ├── config.h        # structs, constants, GPIO defaults
    ├── config.cpp      # LittleFS JSON load/save
    └── main.cpp        # WiFi, MQTT, HTTP server, level logic, UI
```

---

## Configuration Reference

All settings are stored in `/config.json` on LittleFS and editable via the web UI.

| Key | Default | Description |
|-----|---------|-------------|
| `wifi_ssid` | — | WiFi network name |
| `wifi_pass` | — | WiFi password |
| `mqtt_host` | — | MQTT broker IP or hostname |
| `mqtt_port` | `1883` | MQTT broker port |
| `mqtt_user` | — | MQTT username (optional) |
| `mqtt_pass` | — | MQTT password (optional) |
| `mqtt_topic` | `pool/level` | MQTT base topic |
| `device_name` | `PoolLevel` | Human-readable HA device name |
| `client_id` | `poollevel` | MQTT client ID + mDNS hostname |
| `ha_discovery` | `true` | Enable HA MQTT auto-discovery |
| `num_switches` | `2` | Number of switches (2, 3, or 4) |
| `sw0_gpio` … `sw3_gpio` | `4,5,12,14` | GPIO pin per switch |
| `sw0_actlow` … `sw3_actlow` | `true` | Active-LOW logic per switch |

---

## Migrating from v1.x / v2.0

Previous firmware versions published individual `binary_sensor` entities per switch. These leave stale retained discovery messages in your broker. Clean them up:

```bash
CLIENTID="poollevel"   # change if different
for i in 0 1 2 3; do
  mosquitto_pub -h <broker-ip> \
    -t "homeassistant/binary_sensor/${CLIENTID}_sw${i}/config" \
    -n -r
done
```

The v2.1+ firmware also does this automatically on every MQTT connect.

---

## Troubleshooting

| Symptom | Cause | Fix |
|---------|-------|-----|
| MQTT `state=-4` | lwIP slots exhausted | Ensure `ESPAsyncWebServer` is used, not `ESP8266WebServer` |
| `SENSOR_ERROR` in HA | Switch gap detected | Check wiring order — sw1 must be below sw2, etc. |
| Device not in HA | Discovery not received | Verify `ha_discovery=true`, check broker retained messages |
| Can't reach `device.local` | mDNS not resolving | Use IP address directly; mDNS can be unreliable on some networks |
| Settings lost after OTA | Wrong OTA target | Use HTTP OTA from UI — never `pio run --target uploadfs` after first setup |

---

## License

MIT — free to use, modify, and distribute.
