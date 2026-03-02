#include "led.h"
#include "esp_log.h"
#include "esp_event.h"
#include "esp_timer.h"
#include "led_strip.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/portmacro.h"

#include <string.h>

static const char *TAG = "app_led";

/* timer frequency: 10Hz -> 100ms */
static const int g_led_period_us = 100 * 1000;

/* LED strip handle owned by module */
static led_strip_handle_t s_led_strip = NULL;
static esp_timer_handle_t s_led_timer = NULL;

/* state protected by portMUX */
static portMUX_TYPE s_led_mux = portMUX_INITIALIZER_UNLOCKED;
static bool s_led_state = false;
static bool s_led_override = false;
static uint8_t s_led_r = 0, s_led_g = 0, s_led_b = 0;

/* event base local to module (other modules won't need it) */
ESP_EVENT_DEFINE_BASE(LED_EVENT_BASE);
enum {
    LED_EVENT_SET_COLOR = 0,
    LED_EVENT_TEMP_ALARM,
    LED_EVENT_WG_STATUS,
};

typedef struct {
    uint8_t r, g, b;
} led_color_payload_t;

/* bounded helper to create led strip device */
static led_strip_handle_t create_led_strip(void) {
    led_strip_config_t strip_config = {
        .strip_gpio_num = 21,
        .max_leds = 1,
        .led_model = LED_MODEL_WS2812,
        .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB,
        .flags = {.invert_out = false},
    };
    led_strip_rmt_config_t rmt_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 10 * 1000 * 1000,
        .mem_block_symbols = 64,
        .flags = {.with_dma = false},
    };
    led_strip_handle_t handle = NULL;
    esp_err_t rc = led_strip_new_rmt_device(&strip_config, &rmt_config, &handle);
    if (rc != ESP_OK) {
        ESP_LOGE(TAG, "led_strip_new_rmt_device failed: %s", esp_err_to_name(rc));
        return NULL;
    }
    return handle;
}

static void led_timer_cb(void *arg) {
    (void)arg;
    portENTER_CRITICAL(&s_led_mux);
    s_led_state = !s_led_state;
    bool override = s_led_override;
    uint8_t r = s_led_r, g = s_led_g, b = s_led_b;
    portEXIT_CRITICAL(&s_led_mux);

    if (!s_led_strip)
        return;

    if (override) {
        led_strip_set_pixel(s_led_strip, 0, r, g, b);
    } else {
        if (s_led_state) {
            led_strip_set_pixel(s_led_strip, 0, 0x00, 0x80, 0x00);
        } else {
            led_strip_clear(s_led_strip);
        }
    }
    led_strip_refresh(s_led_strip);
}

static void led_event_handler(void *handler_arg, esp_event_base_t base, int32_t id, void *event_data) {
    (void)handler_arg; (void)base;
    if (id == LED_EVENT_SET_COLOR && event_data) {
        led_color_payload_t *p = (led_color_payload_t *)event_data;
        portENTER_CRITICAL(&s_led_mux);
        s_led_r = p->r; s_led_g = p->g; s_led_b = p->b;
        s_led_override = true;
        portEXIT_CRITICAL(&s_led_mux);
    } else if (id == LED_EVENT_TEMP_ALARM && event_data) {
        bool alarm = *(bool *)event_data;
        portENTER_CRITICAL(&s_led_mux);
        if (alarm) {
            s_led_r = 0xFF; s_led_g = 0x00; s_led_b = 0x00;
            s_led_override = true;
        } else {
            s_led_override = false;
        }
        portEXIT_CRITICAL(&s_led_mux);
    } else if (id == LED_EVENT_WG_STATUS && event_data) {
        int st = *(int *)event_data;
        portENTER_CRITICAL(&s_led_mux);
        if (st) { s_led_r = 0x00; s_led_g = 0x00; s_led_b = 0x80; s_led_override = true; }
        else    { s_led_r = 0x80; s_led_g = 0x00; s_led_b = 0x00; s_led_override = true; }
        portEXIT_CRITICAL(&s_led_mux);
    }
}

esp_err_t led_init(void) {
    if (s_led_strip != NULL)
        return ESP_OK;
    s_led_strip = create_led_strip();
    if (!s_led_strip)
        return ESP_FAIL;

    const esp_timer_create_args_t args = {
        .callback = &led_timer_cb,
        .arg = NULL,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "led_blink",
        .skip_unhandled_events = true,
    };
    esp_err_t rc = esp_timer_create(&args, &s_led_timer);
    if (rc != ESP_OK) {
        ESP_LOGE(TAG, "esp_timer_create failed: %s", esp_err_to_name(rc));
        return rc;
    }
    rc = esp_timer_start_periodic(s_led_timer, g_led_period_us);
    if (rc != ESP_OK) {
        ESP_LOGE(TAG, "esp_timer_start_periodic failed: %s", esp_err_to_name(rc));
        return rc;
    }

    rc = esp_event_handler_register(LED_EVENT_BASE, ESP_EVENT_ANY_ID, &led_event_handler, NULL);
    if (rc != ESP_OK) {
        ESP_LOGE(TAG, "esp_event_handler_register failed: %s", esp_err_to_name(rc));
        return rc;
    }

    return ESP_OK;
}

esp_err_t led_deinit(void) {
    if (s_led_timer) {
        esp_timer_stop(s_led_timer);
        esp_timer_delete(s_led_timer);
        s_led_timer = NULL;
    }
    esp_event_handler_unregister(LED_EVENT_BASE, ESP_EVENT_ANY_ID, &led_event_handler);
    if (s_led_strip) {
        led_strip_clear(s_led_strip);
        led_strip_refresh(s_led_strip);
        led_strip_del(s_led_strip);
        s_led_strip = NULL;
    }
    return ESP_OK;
}

esp_err_t led_post_color(uint8_t r, uint8_t g, uint8_t b) {
    led_color_payload_t p = {.r = r, .g = g, .b = b};
    return esp_event_post(LED_EVENT_BASE, LED_EVENT_SET_COLOR, &p, sizeof(p), portMAX_DELAY);
}

esp_err_t led_post_temp_alarm(bool alarm) {
    return esp_event_post(LED_EVENT_BASE, LED_EVENT_TEMP_ALARM, &alarm, sizeof(alarm), portMAX_DELAY);
}

esp_err_t led_post_wg_status(int status) {
    return esp_event_post(LED_EVENT_BASE, LED_EVENT_WG_STATUS, &status, sizeof(status), portMAX_DELAY);
}
