#include "idf_web_server.h"

httpd_handle_t launcherWebServerStart(uint16_t port) {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = port;
    config.max_open_sockets = 2;
    config.max_uri_handlers = 24;
    config.lru_purge_enable = true;
    config.recv_wait_timeout = 10;
    config.send_wait_timeout = 10;
    config.uri_match_fn = httpd_uri_match_wildcard;

    httpd_handle_t server = nullptr;
    if (httpd_start(&server, &config) != ESP_OK) return nullptr;
    return server;
}

void launcherWebServerStop(httpd_handle_t server) {
    if (server) httpd_stop(server);
}
