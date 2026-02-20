/**
 * @file th_sensor.h
 * @brief Thermocouple I2C sensor driver (scalable C style).
 *
 * This module provides a small, stateful object (`th_t`) representing a
 * thermocouple sensor connected via an I2C master bus. The API follows the
 * project's tagged-union result pattern: functions return `th_result_t` which
 * carries both a status tag and any returned value.
 */

#ifndef TH_SENSOR_H
#define TH_SENSOR_H

#include <stdint.h>
#include <stdbool.h>
#include <esp_err.h>
#include "driver/i2c_master.h"

#define KMETER_DEFAULT_ADDR                        0x66 /* 1 byte */
#define KMETER_TEMP_VAL_REG                        0x00 /* 4 bytes: float */
#define KMETER_INTERNAL_TEMP_VAL_REG               0x10 /* 4 bytes */
#define KMETER_KMETER_ERROR_STATUS_REG             0x20 
#define KMETER_TEMP_CELSIUS_STRING_REG             0x30 /* 8 bytes */
#define KMETER_TEMP_FAHRENHEIT_STRING_REG          0x40 /* 8 bytes */
#define KMETER_INTERNAL_TEMP_CELSIUS_STRING_REG    0x50 /* 8 bytes */
#define KMETER_INTERNAL_TEMP_FAHRENHEIT_STRING_REG 0x60
#define KMETER_FIRMWARE_VERSION_REG                0xFE
#define KMETER_I2C_ADDRESS_REG                     0xFF
/**
 * @brief Status tags for thermocouple operations.
 */
typedef enum th_status_tag_e {
    TH_STATUS_OK = 0,
    TH_STATUS_I2C_ERR,
    TH_STATUS_ARG_ERR,
    TH_STATUS_SENSOR_ERR,
} th_status_tag_t;

/**
 * @brief Per-instance object for a thermocouple sensor.
 */
typedef struct th_t {
    i2c_master_bus_handle_t i2c_bus;
    i2c_master_dev_handle_t dev; /**< device handle created on init (may be NULL) */
    uint8_t i2c_addr;    /**< 7-bit I2C address */
    uint32_t timeout_ms; /**< transaction timeout */
    bool initialized;
} th_t;

/**
 * @brief Tagged-union return for thermocouple API calls.
 */
typedef struct th_result_s {
    th_status_tag_t tag;
    union {
        esp_err_t esp_code; /**< underlying esp_err when i2c fails */
        float temp_c;       /**< returned by `th_get_temp_c` on success */
        char str_c[8];      /**< string buffer for string results */
        uint8_t version;    /**< returned by `th_get_version` */
        uint32_t status;    /**< device status code */
    } value;
} th_result_t;

/**
 * @brief Initialize a `th_t` instance and create a device handle on the bus.
 *
 * The function attempts to add a device to `i2c_bus` for `i2c_addr`. On
 * success the created device handle is stored in `self->dev`.
 *
 * @param self user-allocated `th_t` instance
 * @param i2c_bus I2C master bus handle from `i2c_new_master_bus()`
 * @param i2c_addr 7-bit I2C address (use 0x66 by default)
 * @param timeout_ms transaction timeout in milliseconds (0 -> default 200ms)
 * @return th_result_t tagged result; on I2C error `value.esp_code` is set
 */
th_result_t th_init(th_t *self, i2c_master_bus_handle_t i2c_bus, uint8_t i2c_addr, uint32_t timeout_ms);

/**
 * @brief Deinitialize a `th_t` instance and release device handles.
 */
th_result_t th_deinit(th_t *self);

/**
 * @brief Read temperature in degrees Celsius as a string of 8 bytes.
 *
 * This function reads the appropriate registers from the sensor, converts the
 * raw value to degrees Celsius, and returns it in `value.c_str` on success.
 */
th_result_t th_get_temp_c_str(th_t *self);

/* read temp as float */

th_result_t th_get_temp_c(th_t *self); // read string data and convert to float
th_result_t th_get_temp_c_float(th_t *self); // read raw data from registers and convert to float

/**
 * @brief Read device version (register 0xFE assumed).
 */
th_result_t th_get_version(th_t *self);

/**
 * @brief Read device status register (register 0x01 assumed).
 */
th_result_t th_get_status(th_t *self);

#endif // TH_SENSOR_H
