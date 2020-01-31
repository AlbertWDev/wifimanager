#include "wifi_manager.h"

static const char *TAG = "WiFiManager";

wm_config_t* _wm_config = NULL;
wm_network_info_t wm_network_info_default = {.times_used = 0};



static inline bool wm_available_valid() {
    return _wm_available.count > 0 && _wm_available.index < _wm_available.count;
}
static inline bool wm_available_should_reconnect() {
    return _wm_available.retries < WM_CONNECTION_MAX_RETRIES;
}

static void _event_handler(void* arg, esp_event_base_t event_base, 
                                int32_t event_id, void* event_data)
{
    if(event_base == WIFI_EVENT) {
        switch(event_id) {
            // STA connection
            case WIFI_EVENT_STA_START:
                wm_sta_started = true;
                tcpip_adapter_set_hostname(ESP_IF_WIFI_STA, _wm_config->hostname);

                if(wm_available_valid()) esp_wifi_connect();
                break;
            case WIFI_EVENT_STA_STOP:
                wm_sta_started = false;
                break;

            case WIFI_EVENT_STA_DISCONNECTED:
                if(wm_available_valid() && wm_available_should_reconnect()) {
                    _wm_available.retries++;
                    ESP_LOGW(TAG, "Couldn't connect to '%s'. Retrying... (%d)",
                        _wm_available.networks[_wm_available.index].ssid,
                        _wm_available.retries);
                    esp_wifi_connect();
                } else {
                    _wm_available.index++;
                    if(wm_available_valid()) {
                        wm_connect_to(&_wm_available.networks[_wm_available.index]);
                    } else {
                        free(_wm_available.networks);
                        wm_setup_basic_server(_wm_config);
                    }
                }
                break;
            
            // AP connections
            case WIFI_EVENT_AP_STACONNECTED: {
                wifi_event_ap_staconnected_t* event = (wifi_event_ap_staconnected_t*)event_data;
                ESP_LOGI(TAG, "Device ("MACSTR") joined to AP [AID=%d]",
                    MAC2STR(event->mac), event->aid);
                break;
            }
            case SYSTEM_EVENT_AP_STADISCONNECTED: {
                wifi_event_ap_stadisconnected_t* event = (wifi_event_ap_stadisconnected_t*)event_data;
                ESP_LOGI(TAG, "Device ("MACSTR") left AP [AID=%d]",
                    MAC2STR(event->mac), event->aid);
                break;
            }
        }
    } else if(event_base == IP_EVENT) {
        switch(event_id) {
            case IP_EVENT_STA_GOT_IP:;
                ip_event_got_ip_t* event = (ip_event_got_ip_t*)event_data;
                ESP_LOGI(TAG, "Connected! [%s]", ip4addr_ntoa(&event->ip_info.ip));
                
                if(wm_available_valid()) {
                    // Connected, reset retry count
                    _wm_available.retries = 0;
                    // Update times used to improve this network internal score
                    _wm_available.networks[_wm_available.index].times_used++;
                    wm_storage_save(&_wm_available.networks[_wm_available.index]);
                }
                break;
        }
    }
}

esp_err_t wm_init(wm_config_t* wm_config) {
    esp_err_t err;

    // Copy configuration and apply component default values
    _wm_config = (wm_config_t*)malloc(sizeof(wm_config_t));
    memcpy(_wm_config, wm_config, sizeof(wm_config_t));
    
    if(_wm_config->hostname == NULL || strlen(_wm_config->hostname) == 0) {
        strcpy(_wm_config->hostname, WM_DEFAULT_HOSTNAME);
    }
    if(_wm_config->ap_ssid == NULL || strlen(_wm_config->ap_ssid) == 0) {
        strcpy(_wm_config->ap_ssid, WM_DEFAULT_AP_SSID);
    }
    if(_wm_config->ap_password == NULL || strlen(_wm_config->ap_password) == 0) {
        strcpy(_wm_config->ap_password, WM_DEFAULT_AP_PASSWORD);
    }

    // Initialize NVS
    err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        err = nvs_flash_erase();
        if(err != ESP_OK) return err;
        err = nvs_flash_init();
    }
    if(err != ESP_OK) return err;

    // Setup event loop handler
    err = esp_event_loop_create_default();
    if(err == ESP_ERR_INVALID_STATE) err = ESP_OK;  // Already started
    if(err != ESP_OK) return err;

    err = esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &_event_handler, NULL);
    if(err != ESP_OK) return err;
    err = esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &_event_handler, NULL);
    if(err != ESP_OK) return err;

    // Init TCP/IP layer & WiFi
    tcpip_adapter_init();

    wifi_init_config_t wifi_init_config = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&wifi_init_config);
    if(err != ESP_OK) return err;
    
    // Check if we can connect to any known AP
    memset(&_wm_available, 0, sizeof(_wm_available));
    _wm_available.networks = (wm_network_info_t*)malloc(WM_STORAGE_MAX_NETWORKS*sizeof(wm_network_info_t));
    err = wm_available_connections(_wm_available.networks, &_wm_available.count);
    if(err != ESP_OK) return err;

    for(int i = 0; i < _wm_available.count; i++)
        ESP_LOGI(TAG, "Network found: %s (score: %d)",
            _wm_available.networks[i].ssid,
            _wm_available.networks[i].times_used);

    if(_wm_available.count <= 0) {
        err = wm_setup_basic_server(_wm_config);
    } else {
        err = wm_connect_to(&_wm_available.networks[_wm_available.index]);
    }
    return err;
}

esp_err_t wm_available_connections(wm_network_info_t* found_networks, uint8_t* count) {
    esp_err_t err;
    *count = 0;

    // Get stored networks
    size_t stored_count = WM_STORAGE_MAX_NETWORKS;
    wm_network_info_t stored_networks[stored_count];
    err = wm_storage_read(stored_networks, &stored_count);
    if(err != ESP_OK) return err;
    // No network credentials stored
    if(stored_count == 0) return ESP_OK;

    // Set WiFi mode to STA so it can scan for networks
    wifi_mode_t mode;
    err = esp_wifi_get_mode(&mode);
    if(err != ESP_OK) return err;
    if(mode != WIFI_MODE_STA || mode != WIFI_MODE_APSTA) {
        if(mode == WIFI_MODE_AP) {
            err = esp_wifi_set_mode(WIFI_MODE_APSTA);
        } else {
            err = esp_wifi_set_mode(WIFI_MODE_STA);
        }
        if(err != ESP_OK) return err;
    }

    wifi_config_t wifi_config;
    err = esp_wifi_get_config(ESP_IF_WIFI_STA, &wifi_config);
    if(err != ESP_OK) return err;
    wifi_config.sta.scan_method = WIFI_FAST_SCAN;
    err = esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config);
    if(err != ESP_OK) return err;

    err = esp_wifi_start();
    if(err != ESP_OK) return err;

    // Get available Access Points
    uint16_t ap_count = WM_SCAN_MAX_NETWORKS;
    wifi_ap_record_t ap_records[ap_count];
    err = wm_scan_networks(ap_records, &ap_count);
    if(err != ESP_OK) return err;
    // No AP available
    if(ap_count == 0) return ESP_OK;

    for(int stored = 0; stored < stored_count; stored++) {
        for(int ap = 0; ap < ap_count; ap++) {
            if(strcmp(stored_networks[stored].ssid, (char*)(ap_records[ap].ssid)) == 0) {
                found_networks[(*count)++] = stored_networks[stored];
                break;
            }
        }
    }
    return ESP_OK;
}

esp_err_t wm_scan_networks(wifi_ap_record_t* ap_records, uint16_t* ap_num) {
    esp_err_t err;
    
    wifi_scan_config_t scan_config = {
        .ssid = 0,
        .bssid = 0,
        .channel = 0,
        .show_hidden = true
    };
    err = esp_wifi_scan_start(&scan_config, true);
    if(err != ESP_OK) return err;

    err = esp_wifi_scan_get_ap_records(ap_num, ap_records);
    return err;
}

esp_err_t wm_setup_basic_server(wm_config_t* wm_config) {
    esp_err_t err;
    ESP_LOGI(TAG, "Starting basic configuration server at '%s'", wm_config->ap_ssid);

    err = wm_start_ap(wm_config);
    if(err != ESP_OK) return err;

    wm_dns_captive_start(wm_config);

    wm_start_webserver(); return ESP_OK;
    //err = wm_start_webserver();
    //return err;
}

esp_err_t wm_start_ap(wm_config_t* wm_config) {
    esp_err_t err; 

    // AP configuration
    wifi_config_t wifi_config = {};
    //memset(&wifi_config, 0, sizeof(wifi_config));

    //// SSID
    strcpy((char *)(wifi_config.ap.ssid), wm_config->ap_ssid);

    //// PASSWORD
    size_t pwd_len = wm_config->ap_password == NULL ? 0 : strlen(wm_config->ap_password);
    strcpy((char *)(wifi_config.ap.password),
        pwd_len < 8
        ? WM_DEFAULT_AP_PASSWORD
        : wm_config->ap_password);
    
    //// AUTHENTICATION
    wifi_config.ap.authmode = pwd_len > 0
        ? WIFI_AUTH_WPA_WPA2_PSK
        : WIFI_AUTH_OPEN;

    wifi_config.ap.max_connection = 4;

    err = esp_wifi_set_mode(WIFI_MODE_AP);
    if(err != ESP_OK) return err;
    err = esp_wifi_set_config(ESP_IF_WIFI_AP, &wifi_config);
    if(err != ESP_OK) return err;
    err = esp_wifi_start();
    return err;
}

esp_err_t wm_connect_to(wm_network_info_t* network_info) {
    esp_err_t err;
    _wm_available.retries = 0;

    wifi_config_t wifi_config = {};
    //memset(&wifi_config, 0, sizeof(wifi_config));
    strcpy((char *)(wifi_config.sta.ssid), network_info->ssid);
    strcpy((char *)(wifi_config.sta.password), network_info->password);

    ESP_LOGI(TAG, "Connecting to '%s'", wifi_config.sta.ssid);

    err = esp_wifi_set_mode(WIFI_MODE_STA);
    if(err != ESP_OK) return err;
    err = esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config);
    if(err != ESP_OK) return err;
    err = esp_wifi_start();
    if(err != ESP_OK) return err;

    if(wm_sta_started) err = esp_wifi_connect();
    return err;
}