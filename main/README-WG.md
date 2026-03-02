WireGuard object API (wg_obj)
==============================

This project uses an object-style wrapper for the `esp_wireguard` component.
The API lives in `wg_obj.h` and provides a `wg_t` opaque object with
constructor/destructor, configuration and control functions.

Basic usage example
-------------------

1. Create the object:

```c
wg_t *wg = wg_new();
if (!wg) { /* handle OOM */ }
```

2. Configure local keys (copies strings):

```c
if (wg_set_local_keys(wg, local_priv_b64, local_pub_b64, psk_b64) != WG_OK) {
    wg_destroy(&wg);
}
```

3. Add a peer:

```c
wg_peer_cfg_t peer = {0};
peer.peer_pub_b64 = peer_pub_b64;
peer.preshared_b64 = psk_b64; // optional
peer.endpoint = "1.2.3.4:51820";
peer.allowed_ips = "10.0.0.0/24";
peer.keepalive_seconds = 30;
wg_add_peer(wg, &peer);
```

4. Start and connect:

```c
wg_start(wg);
wg_connect_peer(wg, peer_pub_b64);
```

5. Cleanup:

```c
wg_disconnect_peer(wg, peer_pub_b64);
wg_stop(wg);
wg_destroy(&wg);
```

Notes
-----
- The API copies string arguments, so callers may pass temporary or stack-allocated
  strings safely.
- The current implementation in `wg_obj.c` provides single-peer support as a
  minimal working example. Consider extending to a dynamic peer list for
  production use.
- Ensure the `trombik__esp_wireguard` component is included in your project
  so `esp_wireguard_init` / `esp_wireguard_connect` link correctly.

TODO
----
- Add unit tests or a small smoke-test harness to exercise wg start/connect/stop.
- Extend to multi-peer with peer identifiers and dynamic add/remove.
