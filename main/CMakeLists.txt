idf_component_register(SRCS  "main.c" "ssr_control.c" "th_sensor.c" PRIV_REQUIRES  esp_driver_i2c esp_driver_gpio driver esp_timer esp_eth esp_netif esp_wifi)
