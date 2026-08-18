#include "idf_web_server.h"

httpd_handle_t launcherWebServerStart(uint16_t port) {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = port;
    // The httpd task serves one request at a time, so extra sockets cost almost nothing
    // but they are what keeps a batch upload alive: the browser holds the page's
    // keep-alive connection plus the one it is uploading on, and with only two slots
    // lru_purge_enable silently killed a queued upload whenever a third connection
    // appeared. LWIP is built with 16 sockets, so five is comfortably within budget.
    config.max_open_sockets = 5;
    config.max_uri_handlers = 24;
    config.lru_purge_enable = true;
    // A queued upload can sit idle while the previous file is written to the SD card;
    // ten seconds was short enough for slow cards to time it out mid-batch.
    config.recv_wait_timeout = 30;
    config.send_wait_timeout = 30;
    config.uri_match_fn = httpd_uri_match_wildcard;
    // Big multipart bodies arrive in many segments; a deeper backlog keeps the parser
    // fed instead of stalling on every recv.
    config.backlog_conn = 5;

    httpd_handle_t server = nullptr;
    if (httpd_start(&server, &config) != ESP_OK) return nullptr;
    return server;
}

void launcherWebServerStop(httpd_handle_t server) {
    if (server) httpd_stop(server);
}
