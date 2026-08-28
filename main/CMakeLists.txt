set(HOME_LOGO_HEADER "${CMAKE_CURRENT_BINARY_DIR}/home_logo_bitmap.h")
execute_process(
    COMMAND python3
        "${CMAKE_CURRENT_LIST_DIR}/../tools/generate_home_logo.py"
        "${CMAKE_CURRENT_LIST_DIR}/../assets/logo.png"
        "${HOME_LOGO_HEADER}"
    RESULT_VARIABLE HOME_LOGO_RESULT
)
if(NOT HOME_LOGO_RESULT EQUAL 0)
    message(FATAL_ERROR "Failed to generate RFScout Home logo bitmap")
endif()

# Keep Device Settings in device_view.c while replacing its original procedural
# landing implementation with the canonical bitmap-backed Home implementation.
set_source_files_properties(
    "device_view.c"
    PROPERTIES COMPILE_DEFINITIONS "device_view_landing=device_view_landing_procedural"
)

idf_component_register(
    SRCS
        "rfscout.c"
        "app_manager.c"
        "app_logging.c"
        "alert.c"
        "audio_output.c"
        "device_config.c"
        "device_time.c"
        "device_view.c"
        "home_landing.c"
        "radio_app_view.c"
        "audio_spectrum.c"
        "audio_view.c"
        "recorder.c"
        "recorder_view.c"
        "wifi_settings_view.c"
        "cc1101.c"
        "cc1101_activity.c"
        "nrf24.c"
        "nrf24_activity.c"
        "observation_store.c"
        "morse.c"
        "surveillance.c"
        "surveillance_history.c"
        "platform.c"
        "power.c"
        "board_spi.c"
        "board_i2c.c"
        "battery.c"
        "battery_indicator.c"
        "mlx90640.c"
        "thermal_view.c"
        "status_bar.c"
        "ui_chrome.c"
        "display.c"
        "display_brightness.c"
        "ui_present.c"
        "ui.c"
        "radio_monitor.c"
        "ble_monitor.c"
        "oui_database.c"
        "storage.c"
        "log_writer.c"
    INCLUDE_DIRS "." "${CMAKE_CURRENT_BINARY_DIR}"
    PRIV_REQUIRES bt cjson esp_driver_ledc
    REQUIRES esp_wifi esp_event esp_netif nvs_flash esp_timer driver esp_driver_i2c esp_driver_i2s esp_driver_pcnt esp_lcd fatfs sdmmc
)
