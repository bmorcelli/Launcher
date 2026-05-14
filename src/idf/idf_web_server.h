#ifndef LAUNCHER_IDF_WEB_SERVER_H
#define LAUNCHER_IDF_WEB_SERVER_H

#include "esp_http_server.h"
#include <stdint.h>

httpd_handle_t launcherWebServerStart(uint16_t port);
void launcherWebServerStop(httpd_handle_t server);

#endif
