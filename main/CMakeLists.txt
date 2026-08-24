idf_component_register(
    SRCS
        "rfscout.c"
        "observation_store.c"
        "morse.c"
        "surveillance.c"
        "surveillance_history.c"
        "platform.c"
        "power.c"
        "board_i2c.c"
        "battery.c"
        "battery_indicator.c"
        "gps.c"
        "status_bar.c"
        "display.c"
        "ui.c"
        "radio_monitor.c"
        "ble_monitor.c"
        "oui_database.c"
        "storage.c"
        "log_writer.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES bt
    REQUIRES esp_wifi esp_event esp_netif nvs_flash esp_timer driver esp_driver_i2c esp_driver_uart esp_lcd fatfs sdmmc
)
