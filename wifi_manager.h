#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "freertos/event_groups.h"
#include <esp_system.h>
#include <esp_wifi.h>
#include <esp_event.h>
#include <nvs_flash.h>

#include "sdkconfig.h"
#include <esp_log.h>

#include "wm_storage.h"
#include "wm_dns.h"
#include "wm_webserver.h"



#define WM_DEFAULT_HOSTNAME     "Esp32"
#define WM_DEFAULT_AP_SSID      "Esp32-board"
#define WM_DEFAULT_AP_PASSWORD  "WM_pa55w0rd"
#define WM_CONNECTION_MAX_RETRIES 2
#define WM_SCAN_MAX_NETWORKS 20

#define WM_STA_CONNECTED_BIT BIT0
//#define WM_AP_STARTED_BIT    BIT1


typedef struct ssl_certs_t {
    const uint8_t* cacert;
    size_t cacert_len;
    const uint8_t* prvtkey;
    size_t prvtkey_len;
} ssl_certs_t;


/** @brief WifiManager configuration settings
 * 
 * Only version is required. Default values:
 *     hostname: Esp32
 *      ap_ssid: Esp32-board
 *  ap_password: WM_pa55w0rd
 * 
 * If ap_password length is less than 8 characters, default password will be used.
 * If version number is changed, all saved networks will be erased from the NVS partition.
 * **PLEASE** change version number whenever the structure of the stored data is changed,
 *      so data corruption is avoided.
*/
typedef struct wm_config_t {
    // TCP/IP adapter hostname. Less than 32 characters. Null terminated string.
    char hostname[32];
    // Privisioning AP SSID. Less than 32 characters. Null terminated string.
    char ap_ssid[32];
    // Provisioning AP password. Length between 8 and 64 characters. Null terminated string.
    char ap_password[64];
    // Version number of the WiFiManager storage in the NVS partition.
    uint32_t version;
} wm_config_t;
wm_config_t* _wm_config;


typedef struct wm_network_info_t {
    char ssid[32];
    char password[64];
    uint16_t times_used;
} wm_network_info_t;
extern wm_network_info_t wm_network_info_default;

struct {
    wm_network_info_t* networks;
    uint8_t count;
    uint8_t index;
    uint8_t retries;
} _wm_available;

uint8_t wm_sta_started;


bool wm_sta_connected();

/*
 * Initialize the WiFiManager
 */
esp_err_t wm_init(wm_config_t* wm_config);

/*
 * Find networks nearby whose credentials are stored.
 * @param found_networks    wm_network_info_t array of available connections
 * @param count             Won't be greater than min(WM_STORAGE_MAX_NETWORKS, WM_SCAN_MAX_NETWORKS)
 */
esp_err_t wm_available_connections(wm_network_info_t* found_networks, uint8_t* count);

/*
 * Blocking scan of nearby networks. WiFi must be already started in STA or STA-SoftAP mode.
 */
esp_err_t wm_scan_networks(wifi_ap_record_t* ap_records, uint16_t* ap_num);

/*
 * Setup the basic configuration server, which includes:
 *  - Access Point
 *  - Captive Portal DNS
 *  - HTTP webserver for SSID/password input
 */
esp_err_t wm_setup_basic_server(wm_config_t* wm_config);

/*
 * Create and Access Point with the given configuration
 */
esp_err_t wm_start_ap(wm_config_t* wm_config);

/*
 * Connect to the given network
 */
esp_err_t wm_connect_to(wm_network_info_t* network_info);
