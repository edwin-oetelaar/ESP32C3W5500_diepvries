// Simple WiFi STA implementation. Connects to a given SSID and logs IP.
#include <string.h>
#include "wifi.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_wifi.h"

static const char *TAG = "wifi_sta";
static void invoke_got_ip_cb_if_any(void); /* Forward declaration of internal got-ip callback invoker */

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data) {
    (void)arg;
    if (event_base == WIFI_EVENT) {
        if (event_id == WIFI_EVENT_STA_START) {
            esp_wifi_connect();
        } else if (event_id == WIFI_EVENT_STA_DISCONNECTED) {
            ESP_LOGW(TAG, "WiFi disconnected, retrying...");
            esp_wifi_connect();
        }
    }
}

static void ip_event_handler(void *arg, esp_event_base_t event_base,
                             int32_t event_id, void *event_data) {
    (void)arg;
    if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        esp_netif_ip_info_t ip = event->ip_info;
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&ip.ip));
    //    esp_netif_dhcpc_stop(); /* stop DHCP client if you want to switch to static later */ 
        /* If a user callback is registered, call it so other modules can react. */
        invoke_got_ip_cb_if_any();
    }
}

/* Internal storage for optional got-ip callback */
static wifi_got_ip_cb_t s_got_ip_cb = NULL;
static void *s_got_ip_cb_arg = NULL;

esp_err_t wifi_register_got_ip_cb(wifi_got_ip_cb_t cb, void *arg) {
    s_got_ip_cb = cb;
    s_got_ip_cb_arg = arg;
    return ESP_OK;
}

/* Invoke callback from IP event handler context */
static void invoke_got_ip_cb_if_any(void) {
    if (s_got_ip_cb) {
        s_got_ip_cb(s_got_ip_cb_arg);
    }
}
esp_err_t app_wifi_init_sta(const char *ssid, const char *password) {
    if (ssid == NULL || ssid[0] == '\0') {
        ESP_LOGE(TAG, "SSID is empty");
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t rc = esp_netif_init();
    if (rc != ESP_OK && rc != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "esp_netif_init failed: %s", esp_err_to_name(rc));
        return rc;
    }

    rc = esp_event_loop_create_default();
    if (rc != ESP_OK && rc != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "esp_event_loop_create_default failed: %s", esp_err_to_name(rc));
        return rc;
    }

    esp_netif_t *sta_netif = esp_netif_create_default_wifi_sta();
    if (sta_netif == NULL) {
        ESP_LOGE(TAG, "esp_netif_create_default_wifi_sta failed");
        return ESP_FAIL;
    }

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    rc = esp_wifi_init(&cfg);
    if (rc != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_init failed: %s", esp_err_to_name(rc));
        return rc;
    }

    rc = esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL);
    if (rc != ESP_OK) {
        ESP_LOGE(TAG, "event register wifi failed: %s", esp_err_to_name(rc));
        return rc;
    }

    rc = esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &ip_event_handler, NULL);
    if (rc != ESP_OK) {
        ESP_LOGE(TAG, "event register ip failed: %s", esp_err_to_name(rc));
        return rc;
    }

    wifi_config_t wifi_config = { 0 };
    strncpy((char *)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid) - 1);
    if (password) {
        strncpy((char *)wifi_config.sta.password, password, sizeof(wifi_config.sta.password) - 1);
    }

    rc = esp_wifi_set_mode(WIFI_MODE_STA);
    if (rc != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_set_mode failed: %s", esp_err_to_name(rc));
        return rc;
    }

    rc = esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    if (rc != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_set_config failed: %s", esp_err_to_name(rc));
        return rc;
    }
#if  0
    esp_netif_dhcp_status_t status;
    esp_netif_dhcpc_get_status(sta_netif, &status);
    ESP_LOGE(TAG, "XXXXDHCP client status: %d", status);
    esp_netif_dhcpc_stop(sta_netif);
    ESP_LOGI(TAG, "DHCP client stopped, switching to static IP (if need be)");
#endif
    rc = esp_wifi_start();
    if (rc != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_start failed: %s", esp_err_to_name(rc));
        return rc;
    }

    rc = esp_wifi_connect();
    if (rc != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_connect failed: %s", esp_err_to_name(rc));
        return rc;
    }

    ESP_LOGI(TAG, "WiFi STA init started for SSID '%s'", ssid);
    
    return ESP_OK;
}
