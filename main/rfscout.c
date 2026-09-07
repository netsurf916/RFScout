/**
    rfscout.c : RFScout application entry point
    Description: Initializes platform services, configuration, semantic feedback, notifications, and UI.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "alert.h"
#include "app_logging.h"
#include "app_manager.h"
#include "battery.h"
#include "battery_indicator.h"
#include "ble_monitor.h"
#include "board.h"
#include "device_config.h"
#include "device_time.h"
#include "display.h"
#include "display_idle.h"
#include "event.h"
#include "feedback.h"
#include "gps.h"
#include "input.h"
#include "log_writer.h"
#include "navigation_bar.h"
#include "notification.h"
#include "notification_preferences.h"
#include "observation_store.h"
#include "oui_database.h"
#include "platform.h"
#include "power.h"
#include "power_policy.h"
#include "radio_monitor.h"
#include "status_bar.h"
#include "storage.h"
#include "surveillance.h"
#include "system_events.h"
#include "touch.h"
#include "ui_theme.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"

#include <string.h>

static const char *TAG = "rfscout";

static rfscout_notification_type_t
notification_for_surveillance( surveillance_class_t a_classification )
{
    switch( a_classification )
    {
        case SURVEILLANCE_ALPR:
            return RFSCOUT_NOTIFICATION_ALPR;
        case SURVEILLANCE_BODY_CAMERA:
            return RFSCOUT_NOTIFICATION_BODY_CAMERA;
        case SURVEILLANCE_DRONE:
            return RFSCOUT_NOTIFICATION_DRONE;
        case SURVEILLANCE_CAMERA:
            return RFSCOUT_NOTIFICATION_CAMERA;
        case SURVEILLANCE_SMART_GLASSES:
            return RFSCOUT_NOTIFICATION_SMART_GLASSES;
        case SURVEILLANCE_AUDIO:
            return RFSCOUT_NOTIFICATION_ACOUSTIC_SENSOR;
        case SURVEILLANCE_OTHER:
        case SURVEILLANCE_NONE:
        default:
            return RFSCOUT_NOTIFICATION_SURVEILLANCE_OTHER;
    }
}

static void on_observation( rf_device_t *a_observation )
{
    if( NULL == a_observation )
    {
        return;
    }

    if( !rf_device_address_is_randomized( a_observation ) )
    {
        const char *vendor = oui_database_lookup( a_observation->address );
        if( NULL != vendor )
        {
            strlcpy( a_observation->vendor, vendor, sizeof( a_observation->vendor ) );
        }
    }

    const surveillance_match_t match = surveillance_match( a_observation );
    a_observation->surveillance = match.classification;
    a_observation->surveillance_signature = match.signature;
    a_observation->surveillance_evidence = match.evidence;

    bool is_new = false;
    bool surveillance_new = false;
    if( !observation_store_observe( a_observation, &is_new, &surveillance_new ) )
    {
        return;
    }

    if( surveillance_new )
    {
        const rfscout_notification_type_t notification =
            notification_for_surveillance( match.classification );
        ESP_LOGW( TAG,
                  "ALERT %s evidence=0x%02x source=%s name=\"%s\" company=0x%04x morse=%c",
                  surveillance_signature_label( match.signature ),
                  (unsigned)match.evidence,
                  ( RF_SOURCE_BLE == a_observation->last_source ) ? "BLE" : "WiFi",
                  a_observation->has_name ? a_observation->name : "",
                  (unsigned)a_observation->company_id,
                  device_config_notification_character( notification ) );

        platform_set_alert_led( true );
        (void)notification_emit( notification );
        platform_set_alert_led( false );
        return;
    }

    if( !is_new )
    {
        return;
    }

    const char *source_name = ( RF_SOURCE_BLE == a_observation->last_source ) ? "BLE" : "WiFi";
    const char *display_name = ( '\0' != a_observation->vendor[ 0 ] )
                                   ? a_observation->vendor
                                   : ( a_observation->has_name ? a_observation->name : "Unknown" );
    const char *alert_suffix = ( SURVEILLANCE_NONE != match.classification ) ? " SURVEILLANCE" : "";

    ESP_LOGI( TAG,
              "%s %s %02X:%02X:%02X:%02X:%02X:%02X rssi=%d ch=%u%s",
              source_name,
              display_name,
              a_observation->address[ 0 ],
              a_observation->address[ 1 ],
              a_observation->address[ 2 ],
              a_observation->address[ 3 ],
              a_observation->address[ 4 ],
              a_observation->address[ 5 ],
              a_observation->rssi,
              a_observation->channel,
              alert_suffix );

    const rfscout_wifi_detection_event_t detection_event =
        ( RF_SOURCE_BLE == a_observation->last_source ) ? RFSCOUT_WIFI_DETECTION_BLE_DEVICE
                                                        : RFSCOUT_WIFI_DETECTION_WIFI_DEVICE;
    if( notification_preferences_app_enabled( RFSCOUT_APP_WIFI_BT ) &&
        notification_preferences_wifi_detection_enabled( detection_event ) )
    {
        rfscout_event_emit( RFSCOUT_EVENT_DETECTED,
                            (uint8_t)RFSCOUT_APP_WIFI_BT,
                            (int32_t)a_observation->last_source );
    }
}

void app_main( void )
{
    esp_err_t result = nvs_flash_init();
    if( ( ESP_ERR_NVS_NO_FREE_PAGES == result ) || ( ESP_ERR_NVS_NEW_VERSION_FOUND == result ) )
    {
        ESP_ERROR_CHECK( nvs_flash_erase() );
        result = nvs_flash_init();
    }
    ESP_ERROR_CHECK( result );

    result = platform_init();
    if( ESP_OK != result )
    {
        ESP_LOGE(
            TAG,
            "unsupported hardware profile; leaving ESP32-S3 awake and idle for recovery flashing" );
        while( true )
        {
            vTaskDelay( pdMS_TO_TICKS( 1000U ) );
        }
    }

    ESP_ERROR_CHECK( observation_store_init() );
    ESP_ERROR_CHECK( system_events_init() );
    ESP_ERROR_CHECK( input_init() );
    if( RFSCOUT_BOARD_USER_KEY_PIN >= 0 )
    {
        ESP_ERROR_CHECK( power_init() );
    }

    result = notification_preferences_init();
    if( ESP_OK != result )
    {
        ESP_LOGW( TAG,
                  "per-app notification preferences unavailable; using native defaults: %s",
                  esp_err_to_name( result ) );
    }

    result = alert_init();
    if( ESP_OK != result )
    {
        ESP_LOGW( TAG, "audible notifications unavailable: %s", esp_err_to_name( result ) );
    }

    result = battery_init();
    if( ESP_OK != result )
    {
        ESP_LOGW( TAG, "battery telemetry unavailable: %s", esp_err_to_name( result ) );
    }

    result = storage_init();
    if( ESP_OK != result )
    {
        rfscout_event_emit(
            RFSCOUT_EVENT_STORAGE_MOUNT_FAILED, RFSCOUT_EVENT_SOURCE_SYSTEM, (int32_t)result );
        ESP_LOGW( TAG, "SD storage unavailable: %s", esp_err_to_name( result ) );
    }
    else
    {
        rfscout_event_emit(
            RFSCOUT_EVENT_STORAGE_MOUNTED, RFSCOUT_EVENT_SOURCE_SYSTEM, (int32_t)ESP_OK );
    }

    result = device_config_init();
    if( ESP_OK != result )
    {
        ESP_LOGW( TAG,
                  "configuration persistence unavailable; using runtime defaults: %s",
                  esp_err_to_name( result ) );
    }

    result = power_policy_init();
    if( ESP_OK != result )
    {
        ESP_LOGW( TAG, "protective power policy unavailable: %s", esp_err_to_name( result ) );
    }

    result = ui_theme_init();
    if( ESP_OK != result )
    {
        ESP_LOGW(
            TAG, "theme persistence unavailable; using Indoor: %s", esp_err_to_name( result ) );
    }

    alert_set_mode( device_config_get()->alert_mode );
    (void)alert_set_volume_percent( device_config_get()->alert_volume_percent );

    result = feedback_init();
    if( ESP_OK != result )
    {
        ESP_LOGW( TAG, "interaction feedback unavailable: %s", esp_err_to_name( result ) );
    }

    ESP_ERROR_CHECK( device_time_init() );
    result = gps_init();
    if( ( ESP_OK != result ) && ( ESP_ERR_NOT_SUPPORTED != result ) )
    {
        ESP_LOGW( TAG, "GPS service unavailable: %s", esp_err_to_name( result ) );
    }
    ESP_ERROR_CHECK( radio_monitor_init( on_observation ) );
    ESP_ERROR_CHECK( ble_monitor_init( on_observation ) );

    ESP_ERROR_CHECK( app_manager_init() );
    result = touch_init();
    if( ( ESP_OK != result ) && ( ESP_ERR_NOT_SUPPORTED != result ) )
    {
        ESP_LOGW( TAG, "touch diagnostics unavailable: %s", esp_err_to_name( result ) );
    }
    else if( ESP_OK == result && touch_calibration_active() )
    {
        display_invalidate();
    }
    system_events_poll();
    display_set_brightness( device_config_get()->brightness_percent );
    ESP_ERROR_CHECK( display_idle_init() );
    system_events_enable_notifications();

    if( storage_is_available() && device_config_get()->logging_enabled )
    {
        result = log_writer_init();
        if( ESP_OK != result )
        {
            ESP_LOGW( TAG, "SD logging unavailable: %s", esp_err_to_name( result ) );
        }
        else
        {
            observation_store_set_event_callback( log_writer_store_event, NULL );
            app_logging_init();
        }
    }

    ESP_LOGI( TAG,
              "RFScout started: app-managed radio lifecycle, Wi-Fi region=%s",
              radio_monitor_region_name( radio_monitor_get_region() ) );

    while( true )
    {
        display_idle_poll();
        input_poll();
        if( touch_poll() )
        {
            display_invalidate();
        }
        navigation_bar_poll();
        gps_poll();

        battery_poll();
        power_policy_poll();
        if( RFSCOUT_BOARD_USER_KEY_PIN >= 0 )
        {
            power_poll();
        }

        observation_store_housekeeping();
        app_manager_poll();
        system_events_poll();
        status_bar_poll();
        battery_indicator_poll();

        (void)display_present();

        app_logging_poll();
        vTaskDelay( pdMS_TO_TICKS( 10 ) );
    }
}
