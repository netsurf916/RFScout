set(HOME_LOGO_SOURCE "${CMAKE_CURRENT_LIST_DIR}/../assets/logo.png")
set(HOME_LOGO_GENERATOR "${CMAKE_CURRENT_LIST_DIR}/../tools/generate_home_logo.py")
set(HOME_LOGO_HEADER "${CMAKE_CURRENT_BINARY_DIR}/home_logo_bitmap.h")

# Board targets are selected during the normal configure pass. ESP-IDF also
# evaluates component CMake files in script mode while discovering component
# requirements; project Kconfig values are not available during that pass, so
# use the sole current board as a discovery placeholder and validate the actual
# selection once normal configuration is active.
if(CONFIG_RFSCOUT_BOARD_TEMBED_CC1101_PLUS OR CMAKE_SCRIPT_MODE_FILE)
    set(RFSCOUT_BOARD_DIR "${CMAKE_CURRENT_LIST_DIR}/boards/tembed_cc1101_plus")
else()
    message(FATAL_ERROR "No supported RFScout board target selected")
endif()

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

if(NOT CMAKE_SCRIPT_MODE_FILE)
    set_source_files_properties(
        "apps/home/device_view.c"
        PROPERTIES COMPILE_DEFINITIONS "device_view_landing=device_view_landing_procedural"
    )
endif()

set(RFSCOUT_INCLUDE_DIRS
    "."
    "apps"
    "apps/home"
    "apps/system_events"
    "apps/wifi_ble"
    "apps/cc1101"
    "apps/nrf24"
    "apps/radio_common"
    "apps/recorder"
    "apps/thermal"
    "services/board"
    "services/wireless"
    "services/radio"
    "services/audio"
    "services/thermal"
    "services/storage"
    "services/power"
    "services/display"
)

idf_component_register(
    SRCS
        "rfscout.c"
        "app_manager.c"
        "app_lifecycle.c"
        "app_session.c"
        "app_framework.c"
        "apps/app_registry.c"
        "apps/system_events/app.c"
        "apps/system_events/system_events_view.c"
        "apps/home/app.c"
        "apps/home/device_view.c"
        "apps/home/home_landing.c"
        "apps/wifi_ble/app.c"
        "apps/wifi_ble/ui.c"
        "apps/wifi_ble/wifi_settings_view.c"
        "apps/wifi_ble/observation_store.c"
        "apps/wifi_ble/surveillance.c"
        "apps/wifi_ble/surveillance_history.c"
        "apps/cc1101/app.c"
        "apps/cc1101/cc1101_activity.c"
        "apps/nrf24/app.c"
        "apps/nrf24/nrf24_activity.c"
        "apps/nrf24/nrf24_packet_view.c"
        "apps/radio_common/radio_app_view.c"
        "apps/recorder/app.c"
        "apps/recorder/audio_spectrum.c"
        "apps/recorder/audio_view.c"
        "apps/recorder/recorder.c"
        "apps/recorder/recorder_view.c"
        "apps/thermal/app.c"
        "apps/thermal/thermal_view.c"
        "app_logging.c"
        "app_audio_policy.c"
        "audio_policy.c"
        "memory_manager.c"
        "event.c"
        "system_events.c"
        "feedback.c"
        "notification.c"
        "notification_preferences.c"
        "notification_settings_view.c"
        "alert.c"
        "device_config.c"
        "device_time.c"
        "platform.c"
        "session_state.c"
        "services/board/board_spi.c"
        "services/board/board_i2c.c"
        "services/wireless/radio_monitor.c"
        "services/wireless/ble_monitor.c"
        "services/wireless/oui_database.c"
        "services/radio/cc1101.c"
        "services/radio/nrf24.c"
        "services/audio/audio_output.c"
        "services/audio/morse.c"
        "services/thermal/mlx90640.c"
        "services/storage/storage.c"
        "services/storage/log_writer.c"
        "services/power/battery.c"
        "services/power/power.c"
        "services/power/power_policy.c"
        "services/power/power_policy_view.c"
        "services/display/battery_indicator.c"
        "services/display/status_bar.c"
        "services/display/ui_chrome.c"
        "services/display/ui_theme.c"
        "services/display/display.c"
        "services/display/display_mask.c"
        "services/display/display_brightness.c"
        "services/display/display_idle.c"
        "services/display/ui_present.c"
    INCLUDE_DIRS ${RFSCOUT_INCLUDE_DIRS} "${RFSCOUT_BOARD_DIR}" "${CMAKE_CURRENT_BINARY_DIR}"
    PRIV_REQUIRES bt cjson esp_driver_ledc esp_driver_tsens
    REQUIRES esp_wifi esp_event esp_netif nvs_flash esp_timer driver esp_driver_i2c esp_driver_i2s esp_driver_pcnt esp_lcd fatfs sdmmc
)
