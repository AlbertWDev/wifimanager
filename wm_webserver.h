#pragma once

#include <esp_wifi.h>
#include <esp_event.h>
#include <esp_log.h>
#include <esp_system.h>
#include <nvs_flash.h>
#include <sys/param.h>
#include "nvs_flash.h"
#include "tcpip_adapter.h"

#include "esp_http_server.h"

#include "wifi_manager.h"
#include "wm_dns.h"

#include "sdkconfig.h"

void wm_start_webserver();