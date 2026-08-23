idf_component_register(
    SRCS
        "rfscout.c"
        "observation_store.c"
        "morse.c"
        "surveillance.c"
        "surveillance_history.c"
        "platform.c"
        "board_i2c.c"
        "battery.c"
        "display.c"
        "ui.c"
        "radio_monitor.c"
        "ble_monitor.c"
        "oui_database.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES bt
    REQUIRES esp_wifi esp_event esp_netif nvs_flash esp_timer driver esp_driver_i2c esp_lcd
)
