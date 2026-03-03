#include <stdbool.h>
#include <stdint.h>

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/rmt_encoder.h"
#include "driver/rmt_rx.h"
#include "driver/rmt_tx.h"
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_random.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "led_strip.h"
#include "nvs_flash.h"
#include "ssr_control.h"
#include "th_sensor.h"
#include "wg_obj.h"
#include "eth_w5500.h"
#include "wifi.h"
#include "led.h"
#include "i2c.h"

static const char *g_log_tag = "app_main";

/* Pas deze GPIO's aan op jouw S3 board routing naar W5500. */
static const gpio_num_t g_pin_spi_miso = GPIO_NUM_12;
static const gpio_num_t g_pin_spi_mosi = GPIO_NUM_11;
static const gpio_num_t g_pin_spi_sclk = GPIO_NUM_13;
static const gpio_num_t g_pin_spi_cs = GPIO_NUM_14;
static const gpio_num_t g_pin_eth_int = GPIO_NUM_10;
static const gpio_num_t g_pin_eth_rst = GPIO_NUM_9;

static wg_t *wg = NULL;

/* LED is handled by led.c module */

typedef enum app_status_tag_e {
    APP_STATUS_OK = 0,
    APP_STATUS_GPIO_CONFIG_ERR,
    APP_STATUS_TIMER_CREATE_ERR,
    APP_STATUS_TIMER_START_ERR,
    APP_STATUS_NETIF_INIT_ERR,
    APP_STATUS_EVENT_LOOP_ERR,
    APP_STATUS_I2C_BUS_NEW_ERR,
} app_status_tag_t;

typedef struct app_status_s {
    app_status_tag_t tag;
    union {
        esp_err_t esp_code;
        uint32_t reserved;
    } value;
} app_status_t;

/* expected i2c ids are internal to the i2c module now */

static void maybe_start_wireguard(void); /* Forward declaration of shared WireGuard startup helper */

static void app_eth_got_ip_cb(void *arg, esp_netif_ip_info_t *ip_info) {
    ESP_LOGI(g_log_tag, "Ethernet got IP: " IPSTR, IP2STR(&ip_info->ip));

    /* Start WireGuard if configured (shared helper) */
    maybe_start_wireguard();
}

/* Helper used by both ethernet IP handler and WiFi callback */
static void maybe_start_wireguard(void) {
    /* WIRE GUARD needs STA interface to connect, modify wireguardif.c to fix this */
    /* Example WireGuard startup: replace with your base64 keys and endpoint.
     * - WG_PRIV_B64 and WG_PEER_B64 are required.
     * - WG_PSK_B64 is optional (pre-shared key).
     */
#ifndef WG_PRIV_B64
#define WG_PRIV_B64 "SIxMY6O6WcA2I034xXEZUZiShZTZkAcLzJ+uAYO1x18="
#define WG_PEER_B64 "/09qsI3rB3N+nJHQGCHB4Yvc4sL8i18ZNkKVO2PkFUw="
#define WG_PSK_B64 "hqWEjSRslhmoL0EOZ7A9fFTuphtTEDCZnscukE1/nFI="
#define WG_PEER_ENDPOINT "l18l.nl"
#define WG_ALLOWED_IPS "10.77.76.33" /* geen netmask, enkel 1 IP, dus /32 */
#endif
    /* Migrate to object-style WireGuard API */
    if (wg != NULL) {
        ESP_LOGI(g_log_tag, "wireguard already initialised");
        return;
    }
    wg = wg_new();

    if (!wg) {
        ESP_LOGW(g_log_tag, "wg_new failed: no memory");
    } else {
        if (wg_set_local_keys(wg, WG_PRIV_B64, WG_PEER_B64, WG_PSK_B64) != WG_OK) {
            ESP_LOGW(g_log_tag, "wg_set_local_keys failed");
            wg_destroy(&wg);
        } else {
            wg_peer_cfg_t peer = {0};
            peer.peer_pub_b64 = WG_PEER_B64;
            peer.preshared_b64 = WG_PSK_B64;
            peer.endpoint = WG_PEER_ENDPOINT;
            peer.allowed_ips = WG_ALLOWED_IPS;
            peer.keepalive_seconds = 30;
            peer.port = 51820;

                if (wg_add_peer(wg, &peer) != WG_OK) {
                ESP_LOGW(g_log_tag, "wg_add_peer failed");
                wg_destroy(&wg);
            } else if (wg_start(wg) != WG_OK) {
                ESP_LOGW(g_log_tag, "wg_start failed");
                led_post_wg_status(0);
                wg_destroy(&wg);
            } else if (wg_connect_peer(wg, WG_PEER_B64) != WG_OK) {
                ESP_LOGW(g_log_tag, "wg_connect_peer failed");
                /* keep wg object around in case caller wants to inspect */
                led_post_wg_status(0);
            } else {
                led_post_wg_status(1);
                ESP_LOGI(g_log_tag, "wireguard init via object API succeeded");
            }
        }
    }
}

/* Callback invoked by WiFi module when STA gets IP */
static void wifi_got_ip_cb(void *arg) {
    (void)arg;
    ESP_LOGI(g_log_tag, "WiFi STA got IP, checking if WireGuard should start...");
    maybe_start_wireguard();
}

/* Periodic task that prints FreeRTOS run-time stats. Requires enabling
 * CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS and CONFIG_FREERTOS_USE_STATS_FORMATTING_FUNCTIONS
 * in menuconfig. */
static void task_print_stats(void *arg) {
    (void)arg;
    char *buf = malloc(2048);
    // char buf[2048];
    for (;;) {
#if CONFIG_FREERTOS_USE_STATS_FORMATTING_FUNCTIONS
        vTaskGetRunTimeStats(buf);
        ESP_LOGI(g_log_tag, "Run time stats:\n%s", buf);
#else
        ESP_LOGW(g_log_tag, "Run time stats not enabled in menuconfig");
#endif
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
    free(buf);
}

static void app_log_status(const char *label, app_status_t status) {
    if (status.tag == APP_STATUS_OK) {
        ESP_LOGI(g_log_tag, "%s: ok", label);
        return;
    }

    ESP_LOGE(g_log_tag, "%s: failed (tag=%d, err=%s)", label, (int)status.tag,
             esp_err_to_name(status.value.esp_code));
}

/* LED timer/handling moved to led.c */

void app_main(void) {
    eth_w5500_t *eth = eth_w5500_new();
    if (!eth) {
        ESP_LOGE(g_log_tag, "failed to allocate eth");
        return;
    }

    eth_w5500_cfg_t eth_cfg = {
        .miso_io_num = g_pin_spi_miso,
        .mosi_io_num = g_pin_spi_mosi,
        .sclk_io_num = g_pin_spi_sclk,
        .cs_io_num = g_pin_spi_cs,
        .int_io_num = g_pin_eth_int,
        .rst_io_num = g_pin_eth_rst,
        .spi_host_id = SPI2_HOST,
        .clock_speed_hz = 36 * 1000 * 1000,
    };

    eth_w5500_set_event_cb(eth, app_eth_got_ip_cb, NULL);
    eth_w5500_status_t eth_rc = eth_w5500_start(eth, &eth_cfg);
    if (eth_rc != ETH_W5500_OK) {
        ESP_LOGE(g_log_tag, "eth_w5500_start failed: %d", (int)eth_rc);
        return;
    }

    /* Initialize LED module (timer + handler) after event loop exists */
    if (led_init() != ESP_OK) {
        ESP_LOGW(g_log_tag, "led_init failed");
        return;
    }

    /* Initialize NVS (required by WiFi). */
    {
        esp_err_t nvs_rc = nvs_flash_init();
        if (nvs_rc == ESP_ERR_NVS_NO_FREE_PAGES || nvs_rc == ESP_ERR_NVS_NEW_VERSION_FOUND) {
            esp_err_t erase_rc = nvs_flash_erase();
            if (erase_rc != ESP_OK) {
                ESP_LOGW(g_log_tag, "nvs_flash_erase failed: %s", esp_err_to_name(erase_rc));
            }
            nvs_rc = nvs_flash_init();
        }
        if (nvs_rc != ESP_OK) {
            ESP_LOGW(g_log_tag, "nvs_flash_init failed: %s", esp_err_to_name(nvs_rc));
        }
    }

    /* Initialize WiFi station (optional). Edit WIFI_SSID/WIFI_PASS below. */
#ifndef WIFI_SSID
#define WIFI_SSID "OETELX"
#define WIFI_PASS "1234567890"
#endif
    {
        esp_err_t wrc = app_wifi_init_sta(WIFI_SSID, WIFI_PASS);
        if (wrc != ESP_OK) {
            ESP_LOGW(g_log_tag, "wifi init failed: %s", esp_err_to_name(wrc));
        } else {
            ESP_LOGI(g_log_tag, "wifi init started (STA)");
            /* Register callback to start WireGuard when STA gets an IP */
            wifi_register_got_ip_cb(wifi_got_ip_cb, NULL);
            //  esp_netif_dhcpc_stop(); /* stop DHCP client if you want to switch to static later */
        }
    }
    /* Initialize I2C via module */
    if (i2c_init() != ESP_OK) {
        ESP_LOGW(g_log_tag, "i2c_init failed");
        return;
    }
    i2c_scan_and_report();

    /* Start periodic FreeRTOS stats printer (non-blocking) */
    xTaskCreatePinnedToCore(task_print_stats, "task_stats", 4096, NULL,
                            tskIDLE_PRIORITY + 1, NULL, tskNO_AFFINITY);

    ssr_t ssr = {0}; /* AC-SSR */
    th_t th = {0};   /* KMeterISO */

    ssr_result_t r = ssr_init(&ssr, i2c_get_bus(), 0x50, 200);
    th_result_t th_r = th_init(&th, i2c_get_bus(), 0x66, 200);

    if (r.tag != SSR_STATUS_OK) {
        if (r.tag == SSR_STATUS_I2C_ERR) {
            ESP_LOGE(g_log_tag, "ssr_init failed: tag=%d err=%s", (int)r.tag,
                     esp_err_to_name(r.value.esp_code));
        } else {
            ESP_LOGE(g_log_tag, "ssr_init failed: tag=%d", (int)r.tag);
        }
    } else {
        r = ssr_get_active(&ssr);
        if (r.tag == SSR_STATUS_OK) {
            ESP_LOGI(g_log_tag, "ssr active=%s", r.value.active ? "true" : "false");
        } else if (r.tag == SSR_STATUS_I2C_ERR) {
            ESP_LOGW(g_log_tag, "ssr_get_active i2c err=%s", esp_err_to_name(r.value.esp_code));
        } else {
            ESP_LOGW(g_log_tag, "ssr_get_active err tag=%d", (int)r.tag);
        }

        r = ssr_get_version(&ssr);
        if (r.tag == SSR_STATUS_OK) {
            ESP_LOGI(g_log_tag, "ssr version=0x%02X", r.value.version);

        } else if (r.tag == SSR_STATUS_I2C_ERR) {
            ESP_LOGW(g_log_tag, "ssr_get_version i2c err=%s", esp_err_to_name(r.value.esp_code));
        } else {
            ESP_LOGW(g_log_tag, "ssr_get_version err tag=%d", (int)r.tag);
        }
        while (true) {

            int moet_koelen = 0;

            // th_r = th_get_temp_c(&th); // get float using string read + conversion

            // if (th_r.tag == TH_STATUS_OK) {
            //     ESP_LOGI(g_log_tag, "sth temp=%f C", th_r.value.temp_c);
            // } else if (th_r.tag == TH_STATUS_I2C_ERR) {
            //     ESP_LOGW(
            //         g_log_tag, "sth_get_temp_c i2c err=%s",
            //         esp_err_to_name(th_r.value.esp_code));
            // } else if (th_r.tag == TH_STATUS_SENSOR_ERR) {
            //     ESP_LOGW(
            //         g_log_tag, "sth_get_temp_c sensor error, status=0x%08X", th_r.value.status);
            // } else {
            //     ESP_LOGW(g_log_tag, "sth_get_temp_c err tag=%d", (int)th_r.tag);
            // }

            th_r = th_get_temp_c_float(&th); // get float using direct raw read

            if (th_r.tag == TH_STATUS_OK) {
                ESP_LOGI(g_log_tag, "th tempf=%f C", th_r.value.temp_c);
            } else if (th_r.tag == TH_STATUS_I2C_ERR) {
                ESP_LOGW(g_log_tag, "th_get_temp_c i2c err=%s",
                         esp_err_to_name(th_r.value.esp_code));
            } else if (th_r.tag == TH_STATUS_SENSOR_ERR) {
                ESP_LOGW(g_log_tag, "th_get_temp_c sensor error, status=0x%08X", th_r.value.status);
            } else {
                ESP_LOGW(g_log_tag, "th_get_temp_c err tag=%d", (int)th_r.tag);
            }

            r = ssr_get_active(&ssr);
            if (r.tag == SSR_STATUS_OK) {
                ESP_LOGI(g_log_tag, "ssr active=%s", r.value.active ? "true" : "false");
            } else {
                ESP_LOGW(g_log_tag, "ssr_get_active err tag=%d", (int)r.tag);
            }

            vTaskDelay(pdMS_TO_TICKS(1000));
#if 0
            r = ssr_set_active(&ssr, !r.value.active);
            if (r.tag != SSR_STATUS_OK) {
                ESP_LOGW(g_log_tag, "ssr_set_active err tag=%d", (int)r.tag);
            }
            ESP_LOGI(g_log_tag, "toggled ssr active state, sleeping 1s...");
            r = ssr_get_active(&ssr);
            if (r.tag == SSR_STATUS_OK) {
                ESP_LOGI(g_log_tag, "ssr active=%s", r.value.active ? "true" : "false");
            } else {
                ESP_LOGW(g_log_tag, "ssr_get_active err tag=%d", (int)r.tag);
            }
#endif
            //       vTaskDelay(pdMS_TO_TICKS(1000));

            if (th_r.tag == TH_STATUS_OK) {
                moet_koelen = th_r.value.temp_c > -20.0f; /* example threshold */
            }

            r = ssr_set_active(&ssr, moet_koelen);
            if (r.tag != SSR_STATUS_OK) {
                ESP_LOGW(g_log_tag, "ssr_set_active err tag=%d", (int)r.tag);
            }
            /* Post temp alarm event for LED handling */
            led_post_temp_alarm((bool)moet_koelen);
            ESP_LOGI(g_log_tag, "toggled ssr active state, sleeping 1s...");
        }
        ssr_deinit(&ssr);
    }

    ESP_LOGI(g_log_tag, "running: i2c scan done + w5500 up");
}
