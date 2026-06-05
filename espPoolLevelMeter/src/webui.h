#pragma once
#include <ESP8266WebServer.h>
#include "config.h"

// Registers the "/" route on the provided server instance.
void webui_setup(ESP8266WebServer &server, AppConfig &cfg);