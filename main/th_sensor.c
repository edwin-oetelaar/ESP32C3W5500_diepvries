#include "th_sensor.h"
#include "driver/i2c_master.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <string.h>

#include <ctype.h>
#include <stdbool.h>

/* Parse temperature string like "+0018.50" or "-3.25" into float.
 * Returns true on success and writes value into *out.
 * Returns false for malformed input.
 */
bool temp_str_to_float(const char* s, float* out)
{
    if (!s || !out)
        return false;

    /* skip leading spaces */
    while (*s && isspace((unsigned char)*s))
        s++;

    int sign = 1;
    if (*s == '+' || *s == '-') {
        if (*s == '-')
            sign = -1;
        s++;
    }

    long long int_part = 0;
    int int_digits = 0;
    while (*s >= '0' && *s <= '9') {
        int_part = int_part * 10 + (*s - '0');
        s++;
        int_digits++;
    }

    float frac = 0.0f;
    float div = 1.0f;
    int frac_digits = 0;
    if (*s == '.') {
        s++;
        while (*s >= '0' && *s <= '9') {
            frac = frac * 10.0f + (float)(*s - '0');
            div *= 10.0f;
            s++;
            frac_digits++;
        }
    }

    /* require at least some digits either integer or fractional */
    if (int_digits == 0 && frac_digits == 0)
        return false;

    /* skip trailing spaces */
    while (*s && isspace((unsigned char)*s))
        s++;

    /* reject if any leftover characters */
    if (*s != '\0')
        return false;

    *out = sign * ((float)int_part + (frac_digits ? (frac / div) : 0.0f));
    return true;
}

/* Internal helper: read multiple bytes starting at `reg` */
static th_result_t read_regs(th_t* self, uint8_t reg, uint8_t* out, size_t len)
{
    th_result_t res = { .tag = TH_STATUS_OK };
    if (!self || !self->initialized || out == NULL || len == 0) {
        res.tag = TH_STATUS_ARG_ERR;
        return res;
    }

    esp_err_t rc = ESP_FAIL;
    if (self->dev != NULL) {
        rc = i2c_master_transmit_receive(
            self->dev, &reg, 1, out, len, pdMS_TO_TICKS(self->timeout_ms));
    } else {
        rc = ESP_ERR_INVALID_ARG;
    }

    if (rc != ESP_OK) {
        res.tag = TH_STATUS_I2C_ERR;
        res.value.esp_code = rc;
        return res;
    }
    return res;
}

// static th_result_t write_reg(th_t *self, uint8_t reg, uint8_t val)
// {
//     th_result_t res = { .tag = TH_STATUS_OK };
//     if (!self || !self->initialized) {
//         res.tag = TH_STATUS_ARG_ERR;
//         return res;
//     }
//     uint8_t buf[2] = { reg, val };
//     esp_err_t rc = ESP_FAIL;
//     if (self->dev != NULL) {
//         rc = i2c_master_transmit(self->dev, buf, sizeof(buf), pdMS_TO_TICKS(self->timeout_ms));
//     }  else {
//         rc = ESP_ERR_INVALID_ARG;
//     }

//     if (rc != ESP_OK) {
//         res.tag = TH_STATUS_I2C_ERR;
//         res.value.esp_code = rc;
//         return res;
//     }
//     return res;
// }

th_result_t th_init(
    th_t* self, i2c_master_bus_handle_t i2c_bus, uint8_t i2c_addr, uint32_t timeout_ms)
{
    th_result_t res = { .tag = TH_STATUS_OK };
    if (!self || i2c_addr == 0) {
        res.tag = TH_STATUS_ARG_ERR;
        return res;
    }
    memset(self, 0, sizeof(*self));
    self->i2c_bus = i2c_bus;
    self->i2c_addr = i2c_addr;
    self->timeout_ms = (timeout_ms == 0) ? 200u : timeout_ms;
    self->dev = NULL;

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = i2c_addr,
        .scl_speed_hz = 100000,
    };
    esp_err_t add_rc = i2c_master_bus_add_device(i2c_bus, &dev_cfg, &self->dev);
    if (add_rc != ESP_OK) {
        res.tag = TH_STATUS_I2C_ERR;
        res.value.esp_code = add_rc;
        return res;
    }

    self->initialized = true;
    return res;
}

th_result_t th_deinit(th_t* self)
{
    th_result_t res = { .tag = TH_STATUS_OK };
    if (!self) {
        res.tag = TH_STATUS_ARG_ERR;
        return res;
    }
    /* remove device from bus if added */
    if (self->dev != NULL && self->i2c_bus != NULL) {
        i2c_master_bus_rm_device(self->dev);
    }
    memset(self, 0, sizeof(*self));
    return res;
}

th_result_t th_get_temp_c_str(th_t* self)
{
    th_result_t res = { .tag = TH_STATUS_OK };
    uint8_t buf[8] = { 0 };
    th_result_t r = read_regs(self, KMETER_TEMP_CELSIUS_STRING_REG, buf, sizeof(buf));
    if (r.tag != TH_STATUS_OK)
        return r;

    res.tag = TH_STATUS_OK;
    memcpy(res.value.str_c, buf, sizeof(res.value.str_c));
    return res;
}

th_result_t th_get_temp_c(th_t* self)
{ /* read temp from sensor string representation (8 bytes) into float using conversion */
    th_result_t res = { .tag = TH_STATUS_OK };
    uint8_t buf[9] = { 0 };
    th_result_t r = read_regs(self, KMETER_TEMP_CELSIUS_STRING_REG, buf,
        sizeof(buf) - 1); /* read 8 bytes + null terminator */
    if (r.tag != TH_STATUS_OK)
        return r;

    // convert string to float
    float temp_c = 0.0f;
    if (!temp_str_to_float((char*)buf, &temp_c)) {
        res.tag = TH_STATUS_SENSOR_ERR;
        return res;
    }

    res.tag = TH_STATUS_OK;
    res.value.temp_c = temp_c;

    return res;
}

th_result_t th_get_temp_c_float(th_t* self)
{ /* read temp directly from sensor without string converstion, int to float */
    th_result_t res = { .tag = TH_STATUS_OK };
    int32_t buf = { 0 };
    th_result_t r = read_regs(self, KMETER_TEMP_VAL_REG, (uint8_t*)&buf, 4); /* read 4 bytes */
    if (r.tag != TH_STATUS_OK)
        return r;

    float temp = ((float)buf) / 100.0f; /* bytes are signed int 0.01 deg C, convert to float */

    res.tag = TH_STATUS_OK;
    res.value.temp_c = temp;
    return res;
}
