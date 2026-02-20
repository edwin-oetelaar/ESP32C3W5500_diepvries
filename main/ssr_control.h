/**
 * @file ssr_control.h
 * @brief Driver API for the I2C-controlled SSR peripheral.
 *
 * The module provides a small, stateful object (`ssr_t`) that represents a
 * single SSR device on an I2C master bus. All functions return a tagged-union
 * `ssr_result_t` which contains both a status tag and any associated value.
 */

#ifndef SSR_CONTROL_H
#define SSR_CONTROL_H

#include <stdint.h>
#include <stdbool.h>
#include <esp_err.h>
#include "driver/i2c_master.h"

/**
 * @brief Status tag for SSR operations.
 *
 * Functions return an `ssr_result_t` containing this tag; `SSR_STATUS_OK`
 * indicates success. `SSR_STATUS_I2C_ERR` indicates a lower-level I2C
 * failure (an `esp_err_t` is provided in the union). `SSR_STATUS_ARG_ERR`
 * is returned on invalid arguments.
 */
typedef enum ssr_status_tag_e {
    SSR_STATUS_OK = 0,
    SSR_STATUS_I2C_ERR,
    SSR_STATUS_ARG_ERR,
} ssr_status_tag_t;

/* Opaque SSR object that holds per-instance state. */
/**
 * @brief Per-instance object for an SSR device.
 *
 * The user allocates an `ssr_t` instance (stack or static) and passes it to
 * `ssr_init()`. The struct holds the bus handle and the device handle created
 * for the device address.
 */
typedef struct ssr_t {
    /** Handle to the I2C master bus returned by `i2c_new_master_bus()` */
    i2c_master_bus_handle_t i2c_bus;
    /** Device handle returned by `i2c_master_bus_add_device()` (may be NULL) */
    i2c_master_dev_handle_t dev; /* device handle created on init */
    /** 7-bit I2C device address */
    uint8_t i2c_addr;
    /** Transaction timeout in milliseconds */
    uint32_t timeout_ms;
    /** Internal flag indicating successful initialization */
    bool initialized;
} ssr_t;

/** Generic tagged-union result used by SSR APIs. */
/**
 * @brief Tagged-union return for SSR API calls.
 *
 * - If `tag == SSR_STATUS_OK` and the call returns a value, the value can be
 *   read from the appropriate union member (`active` or `version`).
 * - If `tag == SSR_STATUS_I2C_ERR`, `value.esp_code` contains the underlying
 *   `esp_err_t` returned by the driver.
 */
typedef struct ssr_result_s {
    ssr_status_tag_t tag;
    union {
        esp_err_t esp_code; /* when tag indicates an esp error */
        bool active;        /* returned by get_active */
        uint8_t version;    /* returned by get_version */
        uint32_t reserved;
    } value;
} ssr_result_t;

/**
 * @brief Initialize an `ssr_t` instance.
 *
 * This will create a device handle on the provided `i2c_bus` for `i2c_addr`.
 * The `self` object must remain valid until `ssr_deinit()` is called.
 *
 * @param self Pointer to user-allocated `ssr_t`.
 * @param i2c_bus I2C master bus handle (from `i2c_new_master_bus()`).
 * @param i2c_addr 7-bit I2C device address.
 * @param timeout_ms Transaction timeout in milliseconds (0 -> default 200ms).
 * @return ssr_result_t Tagged result; on I2C errors `value.esp_code` is set.
 */
ssr_result_t ssr_init(ssr_t *self, i2c_master_bus_handle_t i2c_bus, uint8_t i2c_addr, uint32_t timeout_ms);

/**
 * Deinitialize/cleanup an `ssr_t` object. Safe to call on partially-initialized objects.
 */
/**
 * @brief Deinitialize an `ssr_t` instance and release any device handles.
 *
 * Safe to call on partially initialized objects. After this call the
 * `self` contents are cleared. pointer is also set to NULL to prevent use-after-free if the caller tries to reuse the
 */
ssr_result_t ssr_deinit(ssr_t *self);

/** Read on/off state from register 0x00. Returns tagged-union with `.value.active`. */
/**
 * @brief Read the SSR on/off state (register 0x00).
 *
 * @param self Initialized `ssr_t` instance.
 * @return ssr_result_t On success `tag==SSR_STATUS_OK` and `value.active`
 *         contains the boolean state.
 */
ssr_result_t ssr_get_active(ssr_t *self);

/** Write on/off state to register 0x00. Returns tagged result. */
/**
 * @brief Write the SSR on/off state to register 0x00.
 *
 * @param self Initialized `ssr_t` instance.
 * @param active Desired state.
 * @return ssr_result_t Tagged result.
 */
ssr_result_t ssr_set_active(ssr_t *self, bool active);

/** Read device version from register 0xFE. Returns tagged-union with `.value.version`. */
/**
 * @brief Read the device version from register 0xFE.
 *
 * @param self Initialized `ssr_t` instance.
 * @return ssr_result_t On success `tag==SSR_STATUS_OK` and `value.version`
 *         contains the version byte.
 */
ssr_result_t ssr_get_version(ssr_t *self);

#endif // SSR_CONTROL_H
