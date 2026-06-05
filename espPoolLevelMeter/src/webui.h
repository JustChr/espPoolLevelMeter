#pragma once
#include <ESP8266WebServer.h>
#include "config.h"

void webui_setup(ESP8266WebServer &server, AppConfig &cfg);