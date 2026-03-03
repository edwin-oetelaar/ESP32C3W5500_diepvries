#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_netif.h"
#include "esp_event.h"

typedef struct eth_w5500_s eth_w5500_t;

typedef enum {
    ETH_W5500_OK = 0,
    ETH_W5500_ERR_ARG,
    ETH_W5500_ERR_MEM,
    ETH_W5500_ERR_NETIF_INIT,
    ETH_W5500_ERR_EVENT_LOOP,
    ETH_W5500_ERR_SPI_BUS_INIT,
    ETH_W5500_ERR_MAC,
    ETH_W5500_ERR_PHY,
    ETH_W5500_ERR_DRV_INSTALL,
    ETH_W5500_ERR_ATTACH,
    ETH_W5500_ERR_START,
} eth_w5500_status_t;

typedef struct {
    gpio_num_t miso_io_num;
    gpio_num_t mosi_io_num;
    gpio_num_t sclk_io_num;
    gpio_num_t cs_io_num;
    gpio_num_t int_io_num;
    gpio_num_t rst_io_num;
    int spi_host_id; // e.g. SPI2_HOST
    int clock_speed_hz;
} eth_w5500_cfg_t;

typedef void (*eth_w5500_event_cb_t)(void *arg, esp_netif_ip_info_t *ip_info);

eth_w5500_t *eth_w5500_new(void);
void eth_w5500_destroy(eth_w5500_t **self_p);

eth_w5500_status_t eth_w5500_start(eth_w5500_t *self, const eth_w5500_cfg_t *cfg);
eth_w5500_status_t eth_w5500_set_event_cb(eth_w5500_t *self, eth_w5500_event_cb_t cb, void *cb_arg);
