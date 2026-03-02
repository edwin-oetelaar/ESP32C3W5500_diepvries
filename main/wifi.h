// Minimal WiFi STA initializer for ESP32
#pragma once

#include "esp_err.h"

/**
 * Initialize WiFi in station mode and connect to given SSID/password.
 * Returns ESP_OK on success or an esp_err_t on failure.
 */
esp_err_t app_wifi_init_sta(const char *ssid, const char *password);

/**
 * Register a callback that will be invoked when the STA interface receives an IP.
 * The callback is invoked from the IP event handler context.
 */
typedef void (*wifi_got_ip_cb_t)(void *arg);
esp_err_t wifi_register_got_ip_cb(wifi_got_ip_cb_t cb, void *arg);
