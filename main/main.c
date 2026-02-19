#include <stdbool.h>
#include <stdint.h>

#include "driver/gpio.h"
#include "driver/rmt_encoder.h"
#include "driver/rmt_rx.h"
#include "driver/rmt_tx.h"
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_err.h"
#include "esp_eth.h"
#include "esp_eth_mac_w5500.h"
#include "esp_eth_phy_w5500.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_random.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "led_strip.h"
static const char *g_log_tag = "app_main";

/* Pas deze GPIO's aan op jouw S3 board routing naar W5500. */
static const gpio_num_t g_pin_led = GPIO_NUM_21; /* WS2812 data pin */
static const gpio_num_t g_pin_spi_miso = GPIO_NUM_12;
static const gpio_num_t g_pin_spi_mosi = GPIO_NUM_11;
static const gpio_num_t g_pin_spi_sclk = GPIO_NUM_13;
static const gpio_num_t g_pin_spi_cs = GPIO_NUM_14;
static const gpio_num_t g_pin_eth_int = GPIO_NUM_10;
static const gpio_num_t g_pin_eth_rst = GPIO_NUM_9;
static const int g_led_period_us = 500 * 1000; /* LED blink period in microseconds */
static esp_eth_handle_t g_eth_handle = NULL;

static bool g_led_state = false;
static esp_timer_handle_t g_led_timer = NULL;

led_strip_handle_t led_strip = NULL;

led_strip_handle_t configure_led(void) {
                /* LED strip common configuration */
                led_strip_config_t strip_config = {
                        .strip_gpio_num = g_pin_led,
                        .max_leds = 1,
                        .led_model = LED_MODEL_WS2812,
                        .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB,
                        .flags = {.invert_out = false},
                };

                /* RMT backend specific configuration */
                led_strip_rmt_config_t rmt_config = {
                        .clk_src = RMT_CLK_SRC_DEFAULT,
                        .resolution_hz = 10 * 1000 * 1000, /* 10MHz */
                        .mem_block_symbols = 64,
                        .flags = {.with_dma = false},
                };

                /* Create the LED strip object and store it in the global handle */
                led_strip_handle_t new_strip = NULL;
                esp_err_t rc = led_strip_new_rmt_device(&strip_config, &rmt_config, &new_strip);
                if (rc != ESP_OK) {
                                ESP_LOGE(g_log_tag, "led_strip_new_rmt_device failed: %s", esp_err_to_name(rc));
                                return NULL;
                }
                led_strip = new_strip;
                return led_strip;
        }

typedef enum app_status_tag_e {
        APP_STATUS_OK = 0,
        APP_STATUS_GPIO_CONFIG_ERR,
        APP_STATUS_TIMER_CREATE_ERR,
        APP_STATUS_TIMER_START_ERR,
        APP_STATUS_NETIF_INIT_ERR,
        APP_STATUS_EVENT_LOOP_ERR,
        APP_STATUS_SPI_BUS_INIT_ERR,
        APP_STATUS_ETH_MAC_ERR,
        APP_STATUS_ETH_PHY_ERR,
        APP_STATUS_ETH_DRV_INSTALL_ERR,
        APP_STATUS_ETH_ATTACH_ERR,
        APP_STATUS_ETH_START_ERR,
} app_status_tag_t;

typedef struct app_status_s {
        app_status_tag_t tag;
        union {
                esp_err_t esp_code;
                uint32_t reserved;
        } value;
} app_status_t;

/* Forward declaration of IP event handler used during netif setup */
static void got_ip_event_handler(void *arg, esp_event_base_t event_base,
                                 int32_t event_id, void *event_data);

static app_status_t app_init_eth_w5500(void) {
        esp_err_t rc = esp_netif_init();
        if (rc != ESP_OK && rc != ESP_ERR_INVALID_STATE) {
                return (app_status_t){.tag = APP_STATUS_NETIF_INIT_ERR,
                                      .value = {.esp_code = rc}};
        }

        rc = esp_event_loop_create_default();
        if (rc != ESP_OK && rc != ESP_ERR_INVALID_STATE) {
                return (app_status_t){.tag = APP_STATUS_EVENT_LOOP_ERR,
                                      .value = {.esp_code = rc}};
        }

        esp_netif_config_t netif_cfg = ESP_NETIF_DEFAULT_ETH();
        esp_netif_t *netif = esp_netif_new(&netif_cfg);
        if (netif == NULL) {
                return (app_status_t){.tag = APP_STATUS_NETIF_INIT_ERR,
                                      .value = {.esp_code = ESP_FAIL}};
        }

        const spi_bus_config_t spi_bus_cfg = {
            .miso_io_num = g_pin_spi_miso,
            .mosi_io_num = g_pin_spi_mosi,
            .sclk_io_num = g_pin_spi_sclk,
            .quadwp_io_num = -1,
            .quadhd_io_num = -1,
            .data4_io_num = -1,
            .data5_io_num = -1,
            .data6_io_num = -1,
            .data7_io_num = -1,
            .max_transfer_sz = 0,
            .flags = 0,
            .isr_cpu_id = 0,
            .intr_flags = 0,
        };

        rc = spi_bus_initialize(SPI2_HOST, &spi_bus_cfg, SPI_DMA_CH_AUTO);
        if (rc != ESP_OK) {
                return (app_status_t){.tag = APP_STATUS_SPI_BUS_INIT_ERR,
                                      .value = {.esp_code = rc}};
        }

        spi_device_interface_config_t dev_cfg = {
            .command_bits = 16,
            .address_bits = 8,
            .mode = 0,
            .clock_speed_hz = 36 * 1000 * 1000,
            .spics_io_num = g_pin_spi_cs,
            .queue_size = 20,
        };

        eth_w5500_config_t w5500_cfg =
            ETH_W5500_DEFAULT_CONFIG(SPI2_HOST, &dev_cfg);
        w5500_cfg.int_gpio_num = g_pin_eth_int;

        eth_mac_config_t mac_cfg = ETH_MAC_DEFAULT_CONFIG();
        esp_eth_mac_t *mac = esp_eth_mac_new_w5500(&w5500_cfg, &mac_cfg);
        if (mac == NULL) {
                return (app_status_t){.tag = APP_STATUS_ETH_MAC_ERR,
                                      .value = {.esp_code = ESP_FAIL}};
        }

        eth_phy_config_t phy_cfg = ETH_PHY_DEFAULT_CONFIG();
        phy_cfg.reset_gpio_num = g_pin_eth_rst;
        phy_cfg.autonego_timeout_ms = 0;
        esp_eth_phy_t *phy = esp_eth_phy_new_w5500(&phy_cfg);
        if (phy == NULL) {
                return (app_status_t){.tag = APP_STATUS_ETH_PHY_ERR,
                                      .value = {.esp_code = ESP_FAIL}};
        }

        esp_eth_config_t eth_cfg = ETH_DEFAULT_CONFIG(mac, phy);
        rc = esp_eth_driver_install(&eth_cfg, &g_eth_handle);
        if (rc != ESP_OK) {
                return (app_status_t){.tag = APP_STATUS_ETH_DRV_INSTALL_ERR,
                                      .value = {.esp_code = rc}};
        }

        /* Ensure the MAC address is set on the MAC and the netif before
         * attaching. */
        uint8_t mac_addr[6] = {0};
        if (esp_read_mac(mac_addr, ESP_MAC_ETH) == ESP_OK) {
                ESP_LOGI(
                    g_log_tag,
                    "Using base MAC from efuse: %02x:%02x:%02x:%02x:%02x:%02x",
                    mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3],
                    mac_addr[4], mac_addr[5]);
        } else {
                /* Fallback: generate a random MAC (locally administered) */
                esp_fill_random(mac_addr, sizeof(mac_addr));
                mac_addr[0] =
                    (mac_addr[0] & 0xFE) | 0x02; // locally administered
                ESP_LOGW(g_log_tag,
                         "Using generated MAC: %02x:%02x:%02x:%02x:%02x:%02x",
                         mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3],
                         mac_addr[4], mac_addr[5]);
        }

        /* Set MAC on the driver */
        esp_err_t ioctl_rc =
            esp_eth_ioctl(g_eth_handle, ETH_CMD_S_MAC_ADDR, mac_addr);
        if (ioctl_rc != ESP_OK) {
                ESP_LOGW(g_log_tag,
                         "esp_eth_ioctl(ETH_CMD_S_MAC_ADDR) returned %s",
                         esp_err_to_name(ioctl_rc));
        }

        /* Also set MAC on esp-netif so it shows up in netif glue */
        esp_err_t setmac_rc = esp_netif_set_mac(netif, mac_addr);
        if (setmac_rc != ESP_OK) {
                ESP_LOGW(g_log_tag, "esp_netif_set_mac() returned %s",
                         esp_err_to_name(setmac_rc));
        }

        rc = esp_netif_attach(netif, esp_eth_new_netif_glue(g_eth_handle));
        if (rc != ESP_OK) {
                return (app_status_t){.tag = APP_STATUS_ETH_ATTACH_ERR,
                                      .value = {.esp_code = rc}};
        }

        /* Register IP event handler and start DHCP client on the ethernet
         * interface */
        rc = esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_GOT_IP,
                                        &got_ip_event_handler, netif);
        if (rc != ESP_OK) {
                return (app_status_t){.tag = APP_STATUS_EVENT_LOOP_ERR,
                                      .value = {.esp_code = rc}};
        }

        rc = esp_netif_dhcpc_start(netif);
        if (rc != ESP_OK) {
                return (app_status_t){.tag = APP_STATUS_NETIF_INIT_ERR,
                                      .value = {.esp_code = rc}};
        }

        rc = esp_eth_start(g_eth_handle);
        if (rc != ESP_OK) {
                return (app_status_t){.tag = APP_STATUS_ETH_START_ERR,
                                      .value = {.esp_code = rc}};
        }

        return (app_status_t){.tag = APP_STATUS_OK, .value = {.reserved = 0}};
}

static void got_ip_event_handler(void *arg, esp_event_base_t event_base,
                                 int32_t event_id, void *event_data) {
        (void)event_base;
        (void)event_id;
        // esp_netif_t *netif = (esp_netif_t *)arg;
        if (event_data == NULL) {
                return;
        }

        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        esp_netif_ip_info_t ip_info = event->ip_info;
        ESP_LOGI(g_log_tag, "Ethernet got IP: " IPSTR, IP2STR(&ip_info.ip));

        /* Optionally, stop DHCP if you want to switch to static later:
         * esp_netif_dhcpc_stop(netif);
         */
}

static void app_log_status(const char *label, app_status_t status) {
        if (status.tag == APP_STATUS_OK) {
                ESP_LOGI(g_log_tag, "%s: ok", label);
                return;
        }

        ESP_LOGE(g_log_tag, "%s: failed (tag=%d, err=%s)", label,
                 (int)status.tag, esp_err_to_name(status.value.esp_code));
}

static app_status_t app_init_led(void) {
        led_strip_handle_t s = configure_led();
        if (s == NULL) {
                return (app_status_t){.tag = APP_STATUS_GPIO_CONFIG_ERR, .value = {.esp_code = ESP_FAIL}};
        }
        esp_err_t rc = led_strip_clear(s);
        if (rc != ESP_OK) {
                return (app_status_t){.tag = APP_STATUS_GPIO_CONFIG_ERR, .value = {.esp_code = rc}};
        }
        rc = led_strip_refresh(s);
        if (rc != ESP_OK) {
                return (app_status_t){.tag = APP_STATUS_GPIO_CONFIG_ERR, .value = {.esp_code = rc}};
        }
        return (app_status_t){.tag = APP_STATUS_OK, .value = {.reserved = 0}};
}

static void app_timer_tick_led(void *arg)
{
        (void)arg;
        g_led_state = !g_led_state;
        if (led_strip == NULL) return;
        if (g_led_state) {
                /* turn green */
                led_strip_set_pixel(led_strip, 0, 0x00, 0x80, 0x00);
        } else {
                led_strip_clear(led_strip);
        }
        led_strip_refresh(led_strip);
}

static app_status_t app_init_led_timer(void)
{
        const esp_timer_create_args_t args = {
                .callback = &app_timer_tick_led,
                .arg = NULL,
                .dispatch_method = ESP_TIMER_TASK,
                .name = "led_blink",
                .skip_unhandled_events = true,
        };

        esp_err_t rc = esp_timer_create(&args, &g_led_timer);
        if (rc != ESP_OK) {
                return (app_status_t){.tag = APP_STATUS_TIMER_CREATE_ERR, .value = {.esp_code = rc}};
        }

        rc = esp_timer_start_periodic(g_led_timer, g_led_period_us);
        if (rc != ESP_OK) {
                return (app_status_t){.tag = APP_STATUS_TIMER_START_ERR, .value = {.esp_code = rc}};
        }

        return (app_status_t){.tag = APP_STATUS_OK, .value = {.reserved = 0}};
}

void app_main(void) {
        const app_status_t led_rc = app_init_led();
        if (led_rc.tag != APP_STATUS_OK) {
                app_log_status("led_init", led_rc);
                return;
        }

        const app_status_t timer_rc = app_init_led_timer();
        if (timer_rc.tag != APP_STATUS_OK) {
                app_log_status("timer_init", timer_rc);
                return;
        }

        const app_status_t eth_rc = app_init_eth_w5500();
        if (eth_rc.tag != APP_STATUS_OK) {
                app_log_status("eth_w5500_init", eth_rc);
                return;
        }

        ESP_LOGI(g_log_tag, "running: led blink timer=%lld us + W5500 up",
                 g_led_period_us);
}
