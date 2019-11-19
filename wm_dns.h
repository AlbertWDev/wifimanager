#pragma once

#include "esp_log.h"

#include "sdkconfig.h"

#include "wifi_manager.h"

#define WM_DNS_HOST_URL CONFIG_WM_AP_DNS_URL

#define WM_DNS_CAPTIVE_TASK_NAME "wm_dns_captive_task"

#define DNS_PACKET_LEN 512
#define DNS_PORT 53


typedef struct wm_config_t wm_config_t;


bool wm_dns_running;

void wm_dns_captive_start(wm_config_t* wm_config);
void wm_dns_captive_stop();
