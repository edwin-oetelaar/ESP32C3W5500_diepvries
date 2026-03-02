/* Object-style WireGuard wrapper (scalable C) */
#pragma once

#include <stddef.h>
#include "esp_err.h"

typedef struct wg_s wg_t;

typedef enum {
    WG_OK = 0,
    WG_ERR_ARG,
    WG_ERR_MEM,
    WG_ERR_IO,
    WG_ERR_STATE,
} wg_status_t;

typedef enum {
    WG_PEER_STATUS_DISCONNECTED = 0,
    WG_PEER_STATUS_CONNECTED,
    WG_PEER_STATUS_ERROR,
} wg_peer_status_t;

typedef struct {
    const char *peer_pub_b64;    /* copied by API */
    const char *preshared_b64;   /* optional, copied */
    const char *endpoint;        /* host:port, copied */
    const char *allowed_ips;     /* comma-separated, copied */
    int keepalive_seconds;
    int port;                    /* peer port or 0 for default */
} wg_peer_cfg_t;

/* Fixed-size limits for zero-heap configuration data */
#define WG_KEY_B64_MAX 64
#define WG_ENDPOINT_MAX 128
#define WG_ALLOWED_IPS_MAX 64

/* Constructor / destructor */
wg_t *wg_new(void);
void wg_destroy(wg_t **self_p);

/* Configure local keys (API will copy strings) */
wg_status_t wg_set_local_keys(wg_t *self,
                              const char *private_key_b64,
                              const char *public_key_b64,
                              const char *preshared_b64);

/* Add / remove peer(s) (returns WG_OK or error). This simple impl supports one peer. */
wg_status_t wg_add_peer(wg_t *self, const wg_peer_cfg_t *peer_cfg);
wg_status_t wg_remove_peer_by_pubkey(wg_t *self, const char *peer_pub_b64);

/* Control */
wg_status_t wg_start(wg_t *self);    /* init subsystem, allocate resources */
wg_status_t wg_stop(wg_t *self);     /* teardown but keep object for re-start */

/* Connect/disconnect (per-peer or global) */
wg_status_t wg_connect_peer(wg_t *self, const char *peer_pub_b64);
wg_status_t wg_disconnect_peer(wg_t *self, const char *peer_pub_b64);

/* Query */
wg_status_t wg_get_peer_status(const wg_t *self, const char *peer_pub_b64, wg_peer_status_t *out);

/* Optional: callback registration for events (connection, errors) */
typedef void (*wg_event_cb_t)(void *arg, const char *peer_pub_b64, wg_peer_status_t status);
wg_status_t wg_set_event_cb(wg_t *self, wg_event_cb_t cb, void *cb_arg);
