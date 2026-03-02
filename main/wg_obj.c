#include "wg_obj.h"
#include "esp_log.h"
#include "esp_wireguard.h"
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

static const char *TAG = "wg_obj";

struct wg_s {
    /* local keys (in-place, fixed buffers) */
    char local_priv[WG_KEY_B64_MAX];
    bool has_local_priv;
    char local_pub[WG_KEY_B64_MAX];
    bool has_local_pub;
    char local_psk[WG_KEY_B64_MAX];
    bool has_local_psk;

    /* single peer support for now (in-place, fixed buffers) */
    char peer_pub[WG_KEY_B64_MAX];
    bool has_peer_pub;
    char peer_psk[WG_KEY_B64_MAX];
    char peer_endpoint[WG_ENDPOINT_MAX];
    char peer_allowed_ips[WG_ALLOWED_IPS_MAX];
    char peer_allowed_mask[WG_ALLOWED_IPS_MAX];

    wireguard_ctx_t ctx;
    wireguard_config_t cfg;
    int started;
    wg_event_cb_t event_cb;
    void *event_arg;
};

/* bounded copy helper: returns 0 on success, -1 on overflow */
static int bounded_copy(char *dst, size_t dst_size, const char *src) {
    if (!dst || dst_size == 0)
        return -1;
    if (!src) {
        dst[0] = '\0';
        return 0;
    }
    size_t n = strlen(src);
    if (n >= dst_size)
        return -1;
    memcpy(dst, src, n + 1);
    return 0;
}

wg_t *wg_new(void) {
    wg_t *w = calloc(1, sizeof(*w));
    if (!w)
        return NULL;
    w->started = 0;
    w->cfg = (wireguard_config_t)ESP_WIREGUARD_CONFIG_DEFAULT();
    /* ensure flags are clear */
    w->has_local_priv = false;
    w->has_local_pub = false;
    w->has_local_psk = false;
    w->has_peer_pub = false;
    return w;
}

void wg_destroy(wg_t **self_p) {
    if (!self_p || !*self_p)
        return;
    wg_t *w = *self_p;
    if (w->started) {
        /* best-effort stop */
        esp_wireguard_disconnect(&w->ctx);
    }
    /* zero sensitive buffers */
    memset(w, 0, sizeof(*w));
    free(w);
    *self_p = NULL;
}

static wg_status_t map_esp_err(esp_err_t e) {
    if (e == ESP_OK)
        return WG_OK;
    return WG_ERR_IO;
}

wg_status_t wg_set_local_keys(wg_t *self, const char *private_key_b64, const char *public_key_b64,
                              const char *preshared_b64) {
    if (!self || !private_key_b64 || !public_key_b64)
        return WG_ERR_ARG;
    if (bounded_copy(self->local_priv, sizeof(self->local_priv), private_key_b64) != 0)
        return WG_ERR_MEM;
    if (bounded_copy(self->local_pub, sizeof(self->local_pub), public_key_b64) != 0)
        return WG_ERR_MEM;
    if (preshared_b64) {
        if (bounded_copy(self->local_psk, sizeof(self->local_psk), preshared_b64) != 0)
            return WG_ERR_MEM;
        self->has_local_psk = true;
    } else {
        self->local_psk[0] = '\0';
        self->has_local_psk = false;
    }
    self->has_local_priv = true;
    self->has_local_pub = true;
    return WG_OK;
}

wg_status_t wg_add_peer(wg_t *self, const wg_peer_cfg_t *peer_cfg) {
    if (!self || !peer_cfg || !peer_cfg->peer_pub_b64)
        return WG_ERR_ARG;
    if (bounded_copy(self->peer_pub, sizeof(self->peer_pub), peer_cfg->peer_pub_b64) != 0)
        return WG_ERR_MEM;
    if (peer_cfg->preshared_b64) {
        if (bounded_copy(self->peer_psk, sizeof(self->peer_psk), peer_cfg->preshared_b64) != 0)
            return WG_ERR_MEM;
    } else {
        self->peer_psk[0] = '\0';
    }
    if (peer_cfg->endpoint) {
        if (bounded_copy(self->peer_endpoint, sizeof(self->peer_endpoint), peer_cfg->endpoint) != 0)
            return WG_ERR_MEM;
    } else {
        self->peer_endpoint[0] = '\0';
    }
    if (peer_cfg->allowed_ips) {
        if (bounded_copy(self->peer_allowed_ips, sizeof(self->peer_allowed_ips), peer_cfg->allowed_ips) != 0)
            return WG_ERR_MEM;
    } else {
        self->peer_allowed_ips[0] = '\0';
    }
    /* default mask */
    bounded_copy(self->peer_allowed_mask, sizeof(self->peer_allowed_mask), "255.255.255.255");
    self->has_peer_pub = true;
    /* store keepalive / port into cfg when starting (left to wg_start) */
    return WG_OK;
}

wg_status_t wg_remove_peer_by_pubkey(wg_t *self, const char *peer_pub_b64) {
    if (!self || !peer_pub_b64)
        return WG_ERR_ARG;
    if (!self->has_peer_pub)
        return WG_ERR_STATE;
    if (strcmp(self->peer_pub, peer_pub_b64) != 0)
        return WG_ERR_ARG;
    /* clear peer data */
    self->peer_pub[0] = '\0';
    self->peer_psk[0] = '\0';
    self->peer_endpoint[0] = '\0';
    self->peer_allowed_ips[0] = '\0';
    self->peer_allowed_mask[0] = '\0';
    self->has_peer_pub = false;
    return WG_OK;
}

wg_status_t wg_start(wg_t *self) {
    if (!self)
        return WG_ERR_ARG;
    if (!self->has_local_priv || !self->has_peer_pub)
        return WG_ERR_STATE;
    /* prepare wireguard_config_t */
    wireguard_config_t cfg = ESP_WIREGUARD_CONFIG_DEFAULT();
    cfg.private_key = self->has_local_priv ? self->local_priv : NULL;
    cfg.public_key = self->has_peer_pub ? self->peer_pub : NULL;
    cfg.preshared_key = (self->peer_psk[0] != '\0') ? self->peer_psk : NULL;
    cfg.allowed_ip = (self->peer_allowed_ips[0] != '\0') ? self->peer_allowed_ips : NULL;
    cfg.allowed_ip_mask = (self->peer_allowed_mask[0] != '\0') ? self->peer_allowed_mask : "255.255.255.255";
    cfg.endpoint = (self->peer_endpoint[0] != '\0') ? self->peer_endpoint : NULL;
    cfg.listen_port = 0;
    cfg.fw_mark = 0;
    cfg.port = 51820;
    cfg.persistent_keepalive = 30;
    self->cfg = cfg;

    esp_err_t err = esp_wireguard_init(&self->cfg, &self->ctx);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_wireguard_init: %s", esp_err_to_name(err));
        return map_esp_err(err);
    }
    self->started = 1;
    return WG_OK;
}

wg_status_t wg_stop(wg_t *self) {
    if (!self)
        return WG_ERR_ARG;
    if (self->started) {
        /* best-effort disconnect; esp-wireguard API might not provide deinit */
        esp_wireguard_disconnect(&self->ctx);
        self->started = 0;
    }
    return WG_OK;
}

wg_status_t wg_connect_peer(wg_t *self, const char *peer_pub_b64) {
    if (!self)
        return WG_ERR_ARG;
    if (!self->started)
        return WG_ERR_STATE;
    if (self->has_peer_pub && peer_pub_b64 && strcmp(self->peer_pub, peer_pub_b64) != 0)
        return WG_ERR_ARG;
    esp_err_t err = esp_wireguard_connect(&self->ctx);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_wireguard_connect: %s", esp_err_to_name(err));
        return map_esp_err(err);
    }
    ESP_LOGI(TAG, "esp_wireguard_connect: %s", esp_err_to_name(err));
    return WG_OK;
}

wg_status_t wg_disconnect_peer(wg_t *self, const char *peer_pub_b64) {
    if (!self)
        return WG_ERR_ARG;
    if (!self->started)
        return WG_ERR_STATE;
    if (self->has_peer_pub && peer_pub_b64 && strcmp(self->peer_pub, peer_pub_b64) != 0)
        return WG_ERR_ARG;
    /* esp_wireguard may not expose a disconnect; use disconnect if present */
    esp_err_t err = esp_wireguard_disconnect(&self->ctx);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "esp_wireguard_disconnect: %s", esp_err_to_name(err));
        return map_esp_err(err);
    }
    return WG_OK;
}

wg_status_t wg_get_peer_status(const wg_t *self, const char *peer_pub_b64, wg_peer_status_t *out) {
    if (!self || !out)
        return WG_ERR_ARG;
    if (!self->has_peer_pub)
        return WG_ERR_STATE;
    if (peer_pub_b64 && strcmp(self->peer_pub, peer_pub_b64) != 0)
        return WG_ERR_ARG;
    /* this simplified impl does not query runtime state; report connected if started */
    *out = self->started ? WG_PEER_STATUS_CONNECTED : WG_PEER_STATUS_DISCONNECTED;
    return WG_OK;
}

wg_status_t wg_set_event_cb(wg_t *self, wg_event_cb_t cb, void *cb_arg) {
    if (!self)
        return WG_ERR_ARG;
    self->event_cb = cb;
    self->event_arg = cb_arg;
    return WG_OK;
}
