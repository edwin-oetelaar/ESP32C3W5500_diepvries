#pragma once

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

/* Initialize LED module: creates timer (10Hz) and starts internal event handling. */
esp_err_t led_init(void);

/* Deinitialize LED module. */
esp_err_t led_deinit(void);

/* Post requests to change LED color (steady override). */
esp_err_t led_post_color(uint8_t r, uint8_t g, uint8_t b);

/* Post a temperature alarm state (true = alarm). */
esp_err_t led_post_temp_alarm(bool alarm);

/* Post WireGuard status (0 = down, 1 = up). */
esp_err_t led_post_wg_status(int status);
