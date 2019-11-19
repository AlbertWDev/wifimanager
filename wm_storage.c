#include "wm_storage.h"

static const char* TAG = "WMStorage";


esp_err_t wm_storage_read(wm_network_info_t* networks, size_t* count) {
    esp_err_t err;
    size_t max_networks = *count < WM_STORAGE_MAX_NETWORKS ? *count : WM_STORAGE_MAX_NETWORKS;
    *count = 0;

    nvs_handle_t wm_storage;
    err = nvs_open(WM_STORAGE_NAMESPACE, NVS_READONLY, &wm_storage);
    if(err == ESP_ERR_NVS_NOT_FOUND) return ESP_OK; // Namespace not found, no stored networks
    if (err != ESP_OK) return err; // Another NVS error

    // Check version
    uint32_t version;
    err = nvs_get_u32(wm_storage, WM_STORAGE_VERSION_KEY, &version);
    if(err != ESP_OK) {
        nvs_close(wm_storage);
        if(err == ESP_ERR_NVS_NOT_FOUND) {
            return ESP_OK;
        } else {
            return err;
        }
    }
    if(version != _wm_config->version) {
        nvs_close(wm_storage);
        wm_storage_clear();
        return ESP_OK;
    }
    
    char network_key[11];
    size_t size;

    for(uint8_t i = 0; i < max_networks; i++) {
        sprintf(network_key, WM_STORAGE_NETWORK_KEY, i);
        size = sizeof(wm_network_info_t);

        err = nvs_get_blob(wm_storage, network_key, &(networks[i]), &size);
        if(err == ESP_OK) {
            (*count)++;
        } else if (err == ESP_ERR_NVS_NOT_FOUND) {
            break;
        } else {
            nvs_close(wm_storage);
            return err;
        }
    }
    nvs_close(wm_storage);
    return ESP_OK;
}

esp_err_t wm_storage_save(wm_network_info_t* network) {
    esp_err_t err;
    int8_t index;
    err = wm_storage_find_writeable(network->ssid, &index);
    if(err != ESP_OK) return err;

    err = wm_storage_save_at(network, index);
    ESP_LOGI(TAG, "Network '%s' (%s) saved at index %d", network->ssid, network->password, index);
    return err;
}

esp_err_t wm_storage_find_writeable(char* ssid, int8_t* index) {
    *index = -1;
    esp_err_t err;
    nvs_handle_t wm_storage;
    err = nvs_open(WM_STORAGE_NAMESPACE, NVS_READONLY, &wm_storage);
    if(err == ESP_ERR_NVS_NOT_FOUND) {
        // Namespace not found, no stored networks
        *index = 0;
        return ESP_OK;
    }
    if (err != ESP_OK) return err; // Another NVS error

    // Check version
    uint32_t version;
    err = nvs_get_u32(wm_storage, WM_STORAGE_VERSION_KEY, &version);
    if(err != ESP_OK) {
        nvs_close(wm_storage);
        if(err == ESP_ERR_NVS_NOT_FOUND) {
            *index = 0;
            return ESP_OK;
        } else {
            return err;
        }
    }
    if(version != _wm_config->version) {
        nvs_close(wm_storage);
        wm_storage_clear();
        *index = 0;
        return ESP_OK;
    }

    int less_used = 0, less_used_times = -1;
    char network_key[11];
    wm_network_info_t network_info;
    size_t size = sizeof(wm_network_info_t);
    
    for(uint8_t i = 0; i < WM_STORAGE_MAX_NETWORKS; i++) {
        // Get network info
        sprintf(network_key, WM_STORAGE_NETWORK_KEY, i);
        err = nvs_get_blob(wm_storage, network_key, &network_info, &size);
        
        // Check if end of the list was reached
        if(err == ESP_ERR_NVS_NOT_FOUND) {
            *index = i;
            break;
        }
        // Check if we already have that SSID
        if(strcmp(ssid, network_info.ssid) == 0) {
            *index = i;
            break;
        }
        // Update less used network
        if(less_used_times == -1 || network_info.times_used < less_used_times) {
            less_used = i;
            less_used_times = network_info.times_used;
        }
    }
    if(*index == -1) *index = less_used;

    nvs_close(wm_storage);
    return ESP_OK;
}

esp_err_t wm_storage_save_at(wm_network_info_t* network, uint8_t index) {
    if(index >= WM_STORAGE_MAX_NETWORKS) return ESP_FAIL;

    esp_err_t err;
    nvs_handle_t wm_storage;
    err = nvs_open(WM_STORAGE_NAMESPACE, NVS_READWRITE, &wm_storage);
    if(err != ESP_OK) return err;

    // Write/Update version number
    err = nvs_set_u32(wm_storage, WM_STORAGE_VERSION_KEY, _wm_config->version);
    if(err != ESP_OK) {
        nvs_close(wm_storage);
        return err;
    }

    // Get network key at given index and save the network info
    char network_key[10];
    sprintf(network_key, WM_STORAGE_NETWORK_KEY, index);
    size_t size = sizeof(wm_network_info_t);

    err = nvs_set_blob(wm_storage, network_key, network, size);
    if(err != ESP_OK) {
        nvs_close(wm_storage);
        return err;
    }

    err = nvs_commit(wm_storage);
    nvs_close(wm_storage);
    return err;
}

esp_err_t wm_storage_delete(char* ssid) {
    esp_err_t err;

    nvs_handle_t wm_storage;
    err = nvs_open(WM_STORAGE_NAMESPACE, NVS_READWRITE, &wm_storage);
    if (err != ESP_OK) return err;

    // Check version
    uint32_t version;
    err = nvs_get_u32(wm_storage, WM_STORAGE_VERSION_KEY, &version);
    if(err != ESP_OK) goto close;

    if(version != _wm_config->version) {
        nvs_close(wm_storage);
        wm_storage_clear();
        return ESP_OK;
    }

    // Find network
    char network_key[11];
    wm_network_info_t network_info;
    size_t size = sizeof(wm_network_info_t);
    for(uint8_t i = 0; i < WM_STORAGE_MAX_NETWORKS; i++) {
        // Get network info
        sprintf(network_key, WM_STORAGE_NETWORK_KEY, i);
        err = nvs_get_blob(wm_storage, network_key, &network_info, &size);
        if(err != ESP_OK) continue; // No network found with this key

        // Check if SSID matches
        if(strcmp(ssid, network_info.ssid) == 0) {
            err = nvs_erase_key(wm_storage, network_key);
            if(err != ESP_OK) goto close;

            err = nvs_commit(wm_storage);
            goto close;
        }
    }
    // End of list reached, network not found
    err = ESP_ERR_NVS_NOT_FOUND;

close:
    nvs_close(wm_storage);
    return err;
}

esp_err_t wm_storage_clear() {
    ESP_LOGW(TAG, "Erasing NVS storage for '"WM_STORAGE_NAMESPACE"' namespace!");
    esp_err_t err;

    nvs_handle_t wm_storage;
    err = nvs_open(WM_STORAGE_NAMESPACE, NVS_READWRITE, &wm_storage);
    if (err != ESP_OK) return err;
    
    err = nvs_erase_all(wm_storage);
    if(err != ESP_OK) {
        nvs_close(wm_storage);
        return err;
    }
    
    err = nvs_commit(wm_storage);    
    nvs_close(wm_storage);
    return err;
}