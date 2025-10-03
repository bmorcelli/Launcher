// Copyright 2020 Espressif Systems (Shanghai) PTE LTD
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include <esp_log.h>
#include <esp_wifi.h>
#include <esp_console.h>
#include <esp_rmaker_core.h>
#include <esp_rmaker_user_mapping.h>
#include <esp_rmaker_utils.h>
#include <esp_rmaker_cmd_resp.h>

#include <esp_rmaker_internal.h>
#include <esp_rmaker_console_internal.h>
#if !CONFIG_IDF_TARGET_ESP32P4
#if RMAKER_USING_NETWORK_PROV
#include <network_provisioning/manager.h>
#else
#include <wifi_provisioning/manager.h>
#endif
#endif /* !CONFIG_IDF_TARGET_ESP32P4 */

static const char *TAG = "esp_rmaker_commands";

static int user_node_mapping_handler(int argc, char** argv)
{
    if (argc == 3) {
        printf("%s: Starting user-node mapping\n", TAG);
        return esp_rmaker_start_user_node_mapping(argv[1], argv[2]);
    } else {
        printf("%s: Invalid Usage.\n", TAG);
        return ESP_ERR_INVALID_ARG;
    }

    return ESP_OK;
}

static void register_user_node_mapping()
{
    const esp_console_cmd_t cmd = {
        .command = "add-user",
        .help = "Initiate the User-Node mapping from the node. Usage: add-user <user_id> <secret_key>",
        .func = &user_node_mapping_handler,
    };
    ESP_LOGI(TAG, "Registering command: %s", cmd.command);
    esp_console_cmd_register(&cmd);
}

static int get_node_id_handler(int argc, char** argv)
{
    printf("%s: Node ID: %s\n", TAG, esp_rmaker_get_node_id());
    return ESP_OK;
}

static void register_get_node_id()
{
    const esp_console_cmd_t cmd = {
        .command = "get-node-id",
        .help = "Get the Node ID for this board",
        .func = &get_node_id_handler,
    };
    ESP_LOGI(TAG, "Registering command: %s", cmd.command);
    esp_console_cmd_register(&cmd);
}

#if !CONFIG_IDF_TARGET_ESP32P4
static int wifi_prov_handler(int argc, char** argv)
{
#ifdef CONFIG_ESP_RMAKER_NETWORK_OVER_WIFI
    if (argc < 2) {
        printf("%s: Invalid Usage.\n", TAG);
        return ESP_ERR_INVALID_ARG;
    }
    wifi_config_t wifi_config;
    memset(&wifi_config, 0, sizeof(wifi_config));
    memcpy(wifi_config.sta.ssid, argv[1], strlen(argv[1]));
    if (argc == 3) {
        memcpy(wifi_config.sta.password, argv[2], strlen(argv[2]));
    }

    /* If device is still provisioning, use  network_prov_mgr_configure_wifi_sta/wifi_prov_mgr_configure_sta */
    bool provisioned = false;
#if RMAKER_USING_NETWORK_PROV
    network_prov_mgr_is_wifi_provisioned(&provisioned);
#else
    wifi_prov_mgr_is_provisioned(&provisioned);
#endif
    if (!provisioned) { // provisioning in progress
#if RMAKER_USING_NETWORK_PROV
        network_prov_mgr_configure_wifi_sta(&wifi_config);
#else
        wifi_prov_mgr_configure_sta(&wifi_config);
#endif
        return ESP_OK;
    }

    /* If already provisioned, just set the new credentials */
    /* Stop the Wi-Fi */
    if (esp_wifi_stop() != ESP_OK) {
        printf("%s: Failed to stop wifi\n", TAG);
    }
    /* Configure Wi-Fi station with provided host credentials */
    if (esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config) != ESP_OK) {
        printf("%s: Failed to set WiFi configuration\n", TAG);
        return ESP_FAIL;
    }
    /* (Re)Start Wi-Fi */
    if (esp_wifi_start() != ESP_OK) {
        printf("%s: Failed to start WiFi\n", TAG);
        return ESP_FAIL;
    }
    /* Connect to AP */
    if (esp_wifi_connect() != ESP_OK) {
        printf("%s: Failed to connect WiFi\n", TAG);
        return ESP_FAIL;
    }
    return ESP_OK;
#else
    return ESP_ERR_NOT_SUPPORTED;
#endif /* CONFIG_ESP_RMAKER_NETWORK_OVER_WIFI */
}

static void register_wifi_prov()
{
    const esp_console_cmd_t cmd = {
        .command = "wifi-prov",
        .help = "Wi-Fi Provision the node. Usage: wifi-prov <ssid> [<passphrase>]",
        .func = &wifi_prov_handler,
    };
    ESP_LOGI(TAG, "Registering command: %s", cmd.command);
    esp_console_cmd_register(&cmd);
}
#else
static void register_wifi_prov(void)
{
    ESP_LOGW(TAG, "Wi-Fi provisioning console command is not supported on ESP32-P4");
}
#endif /* !CONFIG_IDF_TARGET_ESP32P4 */

static int cmd_resp_cli_handler(int argc, char *argv[])
{
    if (argc != 5) {
        printf("Usage: cmd <req_id> <user_role> <cmd> <data>\n");
        return -1;
    }
    char *req_id = argv[1];
    uint8_t user_role = atoi(argv[2]);
    uint16_t cmd = atoi(argv[3]);
    esp_rmaker_cmd_resp_test_send(req_id, user_role, cmd, (void *)argv[4], strlen(argv[4]), esp_rmaker_test_cmd_resp, NULL);
    return 0;
}

static void register_cmd_resp_command()
{
    const esp_console_cmd_t cmd_resp_cmd = {
        .command = "cmd",
        .help = "Send command to command-response module. Usage cmd <req_id> <cmd> <user_role> <data>",
        .func = &cmd_resp_cli_handler,
    };
    ESP_LOGI(TAG, "Registering command: %s", cmd_resp_cmd.command);
    esp_console_cmd_register(&cmd_resp_cmd);
}

void register_commands()
{
    register_user_node_mapping();
    register_get_node_id();
    register_wifi_prov();
    register_cmd_resp_command();
}
