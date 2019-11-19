#include "wm_webserver.h"

static const char* TAG = "NetworkChoiceWebServer";

static const char* index_html_head = "<!DOCTYPE html><html><head><meta name=\"viewport\" content=\"initial-scale=1\"><title>Select WiFi</title><style>*{border:none;border-radius:3px;font-family:sans-serif}form{display:flex;flex-direction:column;align-items:center}label,input{width:250px}input{border:1px solid;padding:7px}button{font:bold 16px sans-serif;padding:10px 40px}</style></head><body><form action=\"ssid\" method=\"post\"><label>SSID</label><input id=\"ssid\" type=\"text\" autocorrect=\"off\" autocapitalize=\"none\" name=\"ssid\"/><style>select{width:264px;height:30px;border:1px solid}option{padding:3px 10px}</style><select id=\"ssidlist\"><option hidden>Select network</option>";
static const char* index_html_record = "<option>%s</option>";
static const char* index_html_tail = "</select><script>var l=document.getElementById(\"ssidlist\");l.onchange=function(){document.getElementsByName(\"ssid\")[0].value=l.value;}</script><br><label>Password</label><input type=\"password\" autocorrect=\"off\" autocapitalize=\"none\" name=\"password\"/><br><button type=\"submit\">Connect</button></form></body></html>";


static esp_err_t index_get_handler(httpd_req_t *req)
{
    // Get available Access Points
    uint16_t ap_count = 20;
    wifi_ap_record_t ap_records[ap_count];
    ESP_ERROR_CHECK(wm_scan_networks(ap_records, &ap_count));

    char* index_html;
    index_html = (char *)malloc(3072 * sizeof(char));
    int cursor = 0;
    cursor += sprintf(index_html, index_html_head);
    for(int ap = 0; ap < ap_count; ap++)
        cursor += sprintf(index_html+cursor*sizeof(char), index_html_record, ap_records[ap].ssid);
    
    cursor += sprintf(index_html+cursor*sizeof(char), index_html_tail);
    if(cursor >= 3072) index_html[3071] = '\0';

    httpd_resp_send(req, index_html, cursor*sizeof(char));
    free(index_html);

    return ESP_OK;
}

static const httpd_uri_t index_uri = {
    .uri       = "/",
    .method    = HTTP_GET,
    .handler   = index_get_handler,
    .user_ctx  = NULL
};

esp_err_t ssid_post_handler(httpd_req_t *req) {
    char content[115];
    memset(content, 0, 115*sizeof(char));

    /* Truncate if content length larger than the buffer */
    size_t recv_size = MIN(req->content_len, sizeof(content));

    int ret = httpd_req_recv(req, content, recv_size);
    if (ret < 0) {  // Check if connection was closed
        if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
            httpd_resp_send_408(req);
        }
        return ESP_FAIL; // To ensure the socket is closed
    }
    
    wm_network_info_t network_info = wm_network_info_default;
    if(httpd_query_key_value(content, "ssid", network_info.ssid, 32) == ESP_ERR_NOT_FOUND) {
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_send(req, "SSID required", HTTPD_RESP_USE_STRLEN);
        return ESP_FAIL;
    }
    if(strlen(network_info.ssid) < 1) {
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_send(req, "Empty SSID not valid", HTTPD_RESP_USE_STRLEN);
        return ESP_FAIL;
    }
    if(httpd_query_key_value(content, "password", network_info.password, 64) == ESP_ERR_NOT_FOUND) {
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_send(req, "Password required", HTTPD_RESP_USE_STRLEN);
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "Received credentials for SSID '%s'. Rebooting...", network_info.ssid);
    httpd_resp_send(req, "OK, rebooting...", HTTPD_RESP_USE_STRLEN);
 
    /*ESP_ERROR_CHECK(esp_wifi_stop());
    ESP_ERROR_CHECK(esp_wifi_restore());
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA) );
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config) );
    ESP_ERROR_CHECK(esp_wifi_start());*/

    wm_storage_save(&network_info);
    
    return ESP_OK;
}

httpd_uri_t ssid_uri = {
    .uri      = "/ssid",
    .method   = HTTP_POST,
    .handler  = ssid_post_handler,
    .user_ctx = NULL
};

esp_err_t http_404_error_handler(httpd_req_t *req, httpd_err_code_t err)
{
    httpd_resp_set_hdr(req, "Location", WM_DNS_HOST_URL);
    httpd_resp_set_status(req, "302 Found");
    const char* resp = "Moved temporarily";
    httpd_resp_send(req, resp, strlen(resp));
    return ESP_FAIL;
}

void wm_start_webserver() {
    wifi_mode_t mode;
    ESP_ERROR_CHECK(esp_wifi_get_mode(&mode));
    if(mode != WIFI_MODE_STA || mode != WIFI_MODE_APSTA) {
        if(mode == WIFI_MODE_AP) {
            ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
        } else {
            ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
        }
    }
    wifi_config_t wifi_config;
    ESP_ERROR_CHECK(esp_wifi_get_config(ESP_IF_WIFI_STA, &wifi_config));
    wifi_config.sta.scan_method = WIFI_FAST_SCAN;
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config));
    
    ESP_ERROR_CHECK(esp_wifi_start());

    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();

    // Start the httpd server
    ESP_LOGI(TAG, "Starting server on port: '%d'", config.server_port);
    if (httpd_start(&server, &config) == ESP_OK) {
        // Set URI handlers
        ESP_LOGI(TAG, "Registering URI handlers");
        httpd_register_uri_handler(server, &index_uri);
        httpd_register_uri_handler(server, &ssid_uri);
        httpd_register_err_handler(server, HTTPD_404_NOT_FOUND, http_404_error_handler);
        return;// server;
    }

    ESP_LOGI(TAG, "Error starting server!");
    //return NULL;
}
