#pragma once

#include "esp_err.h"
#include "driver/i2c_master.h"

/* Initialize i2c bus, returns ESP_OK on success. */
esp_err_t i2c_init(void);

/* Run i2c scan and log results. */
void i2c_scan_and_report(void);

/* Return the internal I2C master bus handle (or NULL if not initialized). */
i2c_master_bus_handle_t i2c_get_bus(void);
