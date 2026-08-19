idf_component_register(
    SRCS
        "rfscout.c"
        "observation_store.c"
        "morse.c"
        "surveillance.c"
        "platform.c"
        "radio_monitor.c"
        "ble_monitor.c"
        "oui_database.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES bt
    REQUIRES esp_wifi esp_event esp_netif nvs_flash esp_timer driver
)
