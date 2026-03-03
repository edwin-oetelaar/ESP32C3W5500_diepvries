#include "eth_w5500.h"
#include "esp_log.h"
#include "esp_eth.h"
#include "esp_event.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_random.h"
#include "driver/spi_master.h"
#include <stdlib.h>
#include <string.h>

static const char *TAG = "eth_w5500";

struct eth_w5500_s {
    esp_eth_handle_t eth_handle;
    esp_netif_t *netif;
    eth_w5500_event_cb_t event_cb;
    void *event_arg;
};

static void got_ip_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
    eth_w5500_t *self = (eth_w5500_t *)arg;
    if (event_data == NULL || self == NULL) {
        return;
    }

    ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
    esp_netif_ip_info_t *ip_info = &event->ip_info;
    ESP_LOGI(TAG, "Ethernet got IP: " IPSTR, IP2STR(&ip_info->ip));

    if (self->event_cb) {
        self->event_cb(self->event_arg, ip_info);
    }
}

eth_w5500_t *eth_w5500_new(void) {
    eth_w5500_t *self = calloc(1, sizeof(*self));
    return self;
}

void eth_w5500_destroy(eth_w5500_t **self_p) {
    if (!self_p || !*self_p) return;
    eth_w5500_t *self = *self_p;
    
    if (self->eth_handle) {
        esp_eth_stop(self->eth_handle);
        esp_eth_driver_uninstall(self->eth_handle);
    }
    if (self->netif) {
        esp_netif_destroy(self->netif);
    }
    free(self);
    *self_p = NULL;
}

eth_w5500_status_t eth_w5500_set_event_cb(eth_w5500_t *self, eth_w5500_event_cb_t cb, void *cb_arg) {
    if (!self) return ETH_W5500_ERR_ARG;
    self->event_cb = cb;
    self->event_arg = cb_arg;
    return ETH_W5500_OK;
}

eth_w5500_status_t eth_w5500_start(eth_w5500_t *self, const eth_w5500_cfg_t *cfg) {
    if (!self || !cfg) return ETH_W5500_ERR_ARG;

    esp_err_t rc = esp_netif_init();
    if (rc != ESP_OK && rc != ESP_ERR_INVALID_STATE) {
        return ETH_W5500_ERR_NETIF_INIT;
    }

    rc = esp_event_loop_create_default();
    if (rc != ESP_OK && rc != ESP_ERR_INVALID_STATE) {
        return ETH_W5500_ERR_EVENT_LOOP;
    }

    esp_netif_config_t netif_cfg = ESP_NETIF_DEFAULT_ETH();
    self->netif = esp_netif_new(&netif_cfg);
    if (self->netif == NULL) {
        return ETH_W5500_ERR_NETIF_INIT;
    }

    const spi_bus_config_t spi_bus_cfg = {
        .miso_io_num = cfg->miso_io_num,
        .mosi_io_num = cfg->mosi_io_num,
        .sclk_io_num = cfg->sclk_io_num,
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

    rc = spi_bus_initialize(cfg->spi_host_id, &spi_bus_cfg, SPI_DMA_CH_AUTO);
    if (rc != ESP_OK && rc != ESP_ERR_INVALID_STATE) {
        return ETH_W5500_ERR_SPI_BUS_INIT;
    }

    spi_device_interface_config_t dev_cfg = {
        .command_bits = 16,
        .address_bits = 8,
        .mode = 0,
        .clock_speed_hz = cfg->clock_speed_hz,
        .spics_io_num = cfg->cs_io_num,
        .queue_size = 20,
    };

    eth_w5500_config_t w5500_cfg = ETH_W5500_DEFAULT_CONFIG(cfg->spi_host_id, &dev_cfg);
    w5500_cfg.int_gpio_num = cfg->int_io_num;

    eth_mac_config_t mac_cfg = ETH_MAC_DEFAULT_CONFIG();
    esp_eth_mac_t *mac = esp_eth_mac_new_w5500(&w5500_cfg, &mac_cfg);
    if (mac == NULL) {
        return ETH_W5500_ERR_MAC;
    }

    eth_phy_config_t phy_cfg = ETH_PHY_DEFAULT_CONFIG();
    phy_cfg.reset_gpio_num = cfg->rst_io_num;
    phy_cfg.autonego_timeout_ms = 0;
    esp_eth_phy_t *phy = esp_eth_phy_new_w5500(&phy_cfg);
    if (phy == NULL) {
        return ETH_W5500_ERR_PHY;
    }

    esp_eth_config_t eth_cfg = ETH_DEFAULT_CONFIG(mac, phy);
    rc = esp_eth_driver_install(&eth_cfg, &self->eth_handle);
    if (rc != ESP_OK) {
        return ETH_W5500_ERR_DRV_INSTALL;
    }

    uint8_t mac_addr[6] = {0};
    if (esp_read_mac(mac_addr, ESP_MAC_ETH) == ESP_OK) {
        ESP_LOGI(TAG, "Using base MAC from efuse: %02x:%02x:%02x:%02x:%02x:%02x", mac_addr[0],
                 mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5]);
    } else {
        esp_fill_random(mac_addr, sizeof(mac_addr));
        mac_addr[0] = (mac_addr[0] & 0xFE) | 0x02; // locally administered
        ESP_LOGW(TAG, "Using generated MAC: %02x:%02x:%02x:%02x:%02x:%02x", mac_addr[0],
                 mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5]);
    }

    esp_err_t ioctl_rc = esp_eth_ioctl(self->eth_handle, ETH_CMD_S_MAC_ADDR, mac_addr);
    if (ioctl_rc != ESP_OK) {
        ESP_LOGW(TAG, "esp_eth_ioctl(ETH_CMD_S_MAC_ADDR) returned %s", esp_err_to_name(ioctl_rc));
    }

    esp_err_t setmac_rc = esp_netif_set_mac(self->netif, mac_addr);
    if (setmac_rc != ESP_OK) {
        ESP_LOGW(TAG, "esp_netif_set_mac() returned %s", esp_err_to_name(setmac_rc));
    }

    rc = esp_netif_attach(self->netif, esp_eth_new_netif_glue(self->eth_handle));
    if (rc != ESP_OK) {
        return ETH_W5500_ERR_ATTACH;
    }

    rc = esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_GOT_IP, &got_ip_event_handler, self);
    if (rc != ESP_OK) {
        return ETH_W5500_ERR_EVENT_LOOP;
    }

    rc = esp_netif_dhcpc_start(self->netif);
    if (rc != ESP_OK && rc != ESP_ERR_INVALID_STATE) {
        return ETH_W5500_ERR_NETIF_INIT;
    }

    rc = esp_eth_start(self->eth_handle);
    if (rc != ESP_OK) {
        return ETH_W5500_ERR_START;
    }

    return ETH_W5500_OK;
}
