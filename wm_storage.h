#pragma once

#include "wifi_manager.h"

#include "nvs_flash.h"
#include "nvs.h"

#include "sdkconfig.h"
#include "esp_log.h"

#define WM_STORAGE_NAMESPACE "wifimanager"
#define WM_STORAGE_NETWORK_KEY "network%d"
#define WM_STORAGE_VERSION_KEY "version"
#define WM_STORAGE_MAX_NETWORKS CONFIG_WM_STORAGE_MAX_NETWORKS


typedef struct wm_network_info_t wm_network_info_t;



/*
 * Read a maximum of 'count' networks from the NVS storage.
 * 
 * Version check will be performed. If it doesn't match, all stored networks
 * will be erased.
 */
esp_err_t wm_storage_read(wm_network_info_t* networks, size_t* count);


/*
 * Save a network in the NVS storage. Index will be selected according to
 * space availability, network usage and SSID matching.
 * 
 * Version check will be performed. If it doesn't match, all stored networks
 * will be erased.
 */
esp_err_t wm_storage_save(wm_network_info_t* network);

/*
 * Find the most suitable index where a network should be written.
 * If the storage is full, the index will correspond to the least used network.
 * If the SSID matches a saved network, its index will be given.
 * 
 * Version check will be performed. If it doesn't match, all stored networks
 * will be erased.
 */
esp_err_t wm_storage_find_writeable(char* ssid, int8_t* index);


/*
 * [INTERNAL FUNCTION]
 * Save network info at the given index in NVS storage. Version number will
 * be saved too.
 * 
 * Version check NOT performed. Data corruption might happen if not used
 * properly.
 */
esp_err_t wm_storage_save_at(wm_network_info_t* network, uint8_t index);


/*
 * @brief       Delete network from WifiManager storage
 * 
 * @param[in]   ssid     SSID of the network that will be removed.
 * 
 * @return
 *          - ESP_OK if partition was removed successfully
 *          - ESP_ERR_NVS_NOT_FOUND if the partition was not found
 * 
 * Note: Version check will be performed. If it doesn't match, all stored
 * networks will be erased
 */
esp_err_t wm_storage_delete(char* ssid);

/*
 * Erase NVS storage for the WiFiManager namespace. All saved networks will
 * be deleted.
 */
esp_err_t wm_storage_clear();
