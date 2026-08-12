idf_component_register(
    SRCS "fluid_pendant.c" "ble_pacon.c"
    INCLUDE_DIRS "."
    REQUIRES driver fatfs sdmmc esp_lcd esp_lcd_sh8601 esp_tinyusb lvgl__lvgl
             esp_wifi esp_event esp_netif nvs_flash esp_http_server
             esp_http_client esp-tls json bt)
