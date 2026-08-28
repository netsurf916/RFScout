set(HOME_LOGO_SOURCE "${CMAKE_CURRENT_LIST_DIR}/../assets/logo.png")
set(HOME_LOGO_GENERATOR "${CMAKE_CURRENT_LIST_DIR}/../tools/generate_home_logo.py")
set(HOME_LOGO_HEADER "${CMAKE_CURRENT_BINARY_DIR}/home_logo_bitmap.h")

# Re-run CMake when either bitmap input changes so an incremental idf.py build
# cannot reuse a stale generated header. Keep this out of ESP-IDF's script-mode
# component-requirements pass, where directory property changes are unnecessary.
if(NOT CMAKE_SCRIPT_MODE_FILE)
    set_property(
        DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
        "${HOME_LOGO_SOURCE}"
        "${HOME_LOGO_GENERATOR}"
    )
endif()

execute_process(
    COMMAND python3
        "${HOME_LOGO_GENERATOR}"
        "${HOME_LOGO_SOURCE}"
        "${HOME_LOGO_HEADER}"
    RESULT_VARIABLE HOME_LOGO_RESULT
)
if(NOT HOME_LOGO_RESULT EQUAL 0)
    message(FATAL_ERROR "Failed to generate RFScout Home logo bitmap")
endif()

# Keep Device Settings in device_view.c while replacing its original procedural
# landing implementation with the canonical bitmap-backed Home implementation.
# ESP-IDF evaluates component CMake files once in script mode while discovering
# requirements; source-property commands are not scriptable and must only run
# during the normal configure pass.
if(NOT CMAKE_SCRIPT_MODE_FILE)
    set_source_files_properties(
        "device_view.c"
        PROPERTIES COMPILE_DEFINITIONS "device_view_landing=device_view_landing_procedural"
    )
endif()

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
