#include "ssr_control.h"
#include <esp_err.h>
#include "driver/i2c_master.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <string.h>

/* Internal helpers operate on an ssr_t instance; no globals. */
/**
 * @internal
 * @brief Read a single register from the SSR device.
 *
 * On success the returned `ssr_result_t` has `tag == SSR_STATUS_OK` and the
 * read byte is placed in `value.version`. 
 */
static ssr_result_t read_reg(ssr_t *self, uint8_t reg)
{
    ssr_result_t res = { .tag = SSR_STATUS_OK, .value.reserved = 0 };
    if (!self || !self->initialized) {
        res.tag = SSR_STATUS_ARG_ERR;
        return res;
    }
    uint8_t out = 0;
    /* Prefer using a device-handle if present */
    esp_err_t ret = ESP_FAIL;
    if (self->dev != NULL) {
        ret = i2c_master_transmit_receive(self->dev, &reg, 1, &out, 1, pdMS_TO_TICKS(self->timeout_ms));
    } 

    if (ret != ESP_OK) {
        res.tag = SSR_STATUS_I2C_ERR;
        res.value.esp_code = ret;
        return res;
    }

    res.tag = SSR_STATUS_OK;
    res.value.version = out; /* reuse union; caller interprets */
    return res;
}

/**
 * @internal
 * @brief Write a single byte to an SSR register.
 *
 * Uses the device-handle API
 */
static ssr_result_t write_reg(ssr_t *self, uint8_t reg, uint8_t val)
{
    ssr_result_t res = { .tag = SSR_STATUS_OK, .value.reserved = 0 };
    if (!self || !self->initialized) {
        res.tag = SSR_STATUS_ARG_ERR;
        return res;
    }
    uint8_t buf[2] = { reg, val };
    esp_err_t ret = ESP_FAIL;
    if (self->dev != NULL) {
        ret = i2c_master_transmit(self->dev, buf, sizeof(buf), pdMS_TO_TICKS(self->timeout_ms));
    } 
    if (ret != ESP_OK) {
        res.tag = SSR_STATUS_I2C_ERR;
        res.value.esp_code = ret;
        return res;
    }
    res.tag = SSR_STATUS_OK;
    return res;
}

ssr_result_t ssr_init(ssr_t *self, i2c_master_bus_handle_t i2c_bus, uint8_t i2c_addr, uint32_t timeout_ms)
{
    ssr_result_t res = { .tag = SSR_STATUS_OK, .value.reserved = 0 };
    if (!self || i2c_addr == 0) {
        res.tag = SSR_STATUS_ARG_ERR;
        return res;
    }
    self->i2c_bus = i2c_bus;
    self->i2c_addr = i2c_addr;
    self->timeout_ms = (timeout_ms == 0) ? 200u : timeout_ms;
    self->dev = NULL;

    /* Create a device handle on the bus for this 7-bit address */
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = i2c_addr,
        .scl_speed_hz = 100000,
    };
    esp_err_t add_rc = i2c_master_bus_add_device(i2c_bus, &dev_cfg, &self->dev);
    if (add_rc != ESP_OK) {
        res.tag = SSR_STATUS_I2C_ERR;
        res.value.esp_code = add_rc;
        return res;
    }

    self->initialized = true;
    return res;
}

ssr_result_t ssr_deinit(ssr_t *self)
{
    ssr_result_t res = { .tag = SSR_STATUS_OK, .value.reserved = 0 };
    if (!self ) {
        res.tag = SSR_STATUS_ARG_ERR;
        return res;
    }
    /* remove device from bus if added */
    if (self->dev != NULL && self->i2c_bus != NULL) {
        i2c_master_bus_rm_device(self->dev);
    }
    memset(self, 0, sizeof(*self));
    return res;
}

ssr_result_t ssr_get_active(ssr_t *self)
{
    ssr_result_t r = read_reg(self, 0x00);
    if (r.tag != SSR_STATUS_OK) return r;
    /* stored byte in .value.version — interpret as boolean */
    ssr_result_t out = { .tag = SSR_STATUS_OK };
    out.value.active = (r.value.version != 0);
    return out;
}

ssr_result_t ssr_set_active(ssr_t *self, bool active)
{
    return write_reg(self, 0x00, active ? 1 : 0);
}

ssr_result_t ssr_get_version(ssr_t *self)
{
    return read_reg(self, 0xFE);
}
