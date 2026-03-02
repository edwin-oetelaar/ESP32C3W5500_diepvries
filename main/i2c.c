#include "i2c.h"
#include "esp_err.h"
#include "esp_log.h"
#include "driver/i2c_master.h"

#include <stdint.h>

static const char *TAG = "app_i2c";

/* Reuse the same constants used previously in main.c */
static const i2c_port_t g_i2c_port = I2C_NUM_0;
static const gpio_num_t g_pin_i2c_sda = GPIO_NUM_16;
static const gpio_num_t g_pin_i2c_scl = GPIO_NUM_17;
static const uint32_t g_i2c_clk_hz = 400000;
static const int g_i2c_probe_timeout_ms = 20;

/* keep bus handle local to module */
static i2c_master_bus_handle_t s_i2c_bus = NULL;

esp_err_t i2c_init(void) {
    const i2c_master_bus_config_t cfg = {
        .i2c_port = g_i2c_port,
        .sda_io_num = g_pin_i2c_sda,
        .scl_io_num = g_pin_i2c_scl,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .intr_priority = 0,
        .trans_queue_depth = 0,
        .flags.enable_internal_pullup = true,
    };

    const esp_err_t rc = i2c_new_master_bus(&cfg, &s_i2c_bus);
    if (rc != ESP_OK) {
        ESP_LOGE(TAG, "i2c_new_master_bus failed: %s", esp_err_to_name(rc));
        return rc;
    }
    return ESP_OK;
}

i2c_master_bus_handle_t i2c_get_bus(void) {
    return s_i2c_bus;
}

static bool i2c_probe_addr(uint8_t addr) {
    if (s_i2c_bus == NULL) {
        return false;
    }
    const esp_err_t rc = i2c_master_probe(s_i2c_bus, addr, g_i2c_probe_timeout_ms);
    return rc == ESP_OK;
}

static bool i2c_contains_u8(const uint8_t *items, uint32_t count, uint8_t target) {
    uint32_t i = 0;
    for (i = 0; i < count; ++i) {
        if (items[i] == target) {
            return true;
        }
    }
    return false;
}

void i2c_scan_and_report(void) {
    uint8_t found_ids[128] = {0};
    uint32_t found_count = 0;
    uint8_t addr = 0;

    ESP_LOGI(TAG, "i2c scan start: port=%d sda=%d scl=%d clk=%lu", (int)g_i2c_port,
             (int)g_pin_i2c_sda, (int)g_pin_i2c_scl, (unsigned long)g_i2c_clk_hz);

    for (addr = 0x03; addr <= 0x77; ++addr) {
        if (i2c_probe_addr(addr)) {
            found_ids[found_count] = addr;
            found_count += 1;
            ESP_LOGI(TAG, "i2c found id=0x%02X", addr);
        }
    }

    ESP_LOGI(TAG, "i2c scan done: %u device(s)", (unsigned)found_count);

    const uint8_t expected_ids[] = {0x50, 0x66};
    for (addr = 0; addr < (uint8_t)(sizeof(expected_ids)); ++addr) {
        const uint8_t expected = expected_ids[addr];
        const bool present = i2c_contains_u8(found_ids, found_count, expected);
        ESP_LOGI(TAG, "i2c expected id=0x%02X => %s", expected,
                 present ? "MATCH" : "MISSING");
    }
}
