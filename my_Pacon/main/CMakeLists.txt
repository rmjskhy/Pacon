idf_component_register(
    SRCS "fluid_pendant.c" "ble_pacon.c"
    INCLUDE_DIRS "."
    EMBED_FILES "assets/kkd1_character.rgb565a"
                "assets/kkd1_second_dial.rgb565a"
                "assets/kkd1_minute_dial.rgb565a"
                "assets/kkd1_hour_dial.rgb565a"
                "assets/kkd1_mechanism.rgb565a"
                "assets/kkd1_complication.rgb565a"
                "assets/kkd2_background.rgb565a"
                "assets/kkd2_hour.rgb565a"
                "assets/kkd2_minute.rgb565a"
                "assets/kkd2_complication.rgb565a"
    REQUIRES driver fatfs sdmmc esp_lcd esp_lcd_sh8601 esp_tinyusb lvgl__lvgl
          esp_wifi esp_event esp_netif nvs_flash lwip
             esp_http_client esp-tls json bt)
