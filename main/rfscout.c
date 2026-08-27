/**
    rfscout.c : RFScout application entry point
    Description: Initializes passive RF collectors, configuration, wall time, logging, alerts, and UI.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "alert.h"
#include "app_logging.h"
#include "app_manager.h"
#include "battery.h"
#include "ble_monitor.h"
#include "device_config.h"
#include "device_time.h"
#include "display.h"
#include "log_writer.h"
#include "observation_store.h"
#include "oui_database.h"
#include "platform.h"
#include "power.h"
#include "radio_monitor.h"
#include "storage.h"
#include "surveillance.h"
#include "surveillance_history.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"

#include <string.h>

static const char *TAG = "rfscout";

/**
 * @brief Map a surveillance classification to the global notification category.
 * @note When WiFi/BT surveillance categories change, keep this mapping synchronized with the
 *       canonical notification enum/descriptor table in alert.h and alert.c.
 */
static rfscout_notification_type_t
notification_for_surveillance( surveillance_class_t a_classification )
{
    switch( a_classification )
    {
        case SURVEILLANCE_FLOCK:
            return RFSCOUT_NOTIFICATION_FLOCK;
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

/** @brief Classify, store, log, and alert on one Wi-Fi or BLE observation. */
static void on_observation( rf_device_t *a_observation )
{
    if( NULL == a_observation )
    {
        return;
    }

    if( ( RF_SOURCE_BLE != a_observation->last_source ) || ( 0U == a_observation->address_type ) )
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
    a_observation->surveillance_score = match.score;
    a_observation->surveillance_evidence = match.evidence;

    if( SURVEILLANCE_NONE != match.classification )
    {
        surveillance_history_observe( a_observation );
    }

    bool is_new = false;
    if( !observation_store_observe( a_observation, &is_new ) || !is_new )
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

    if( SURVEILLANCE_NONE != match.classification )
    {
        const rfscout_notification_type_t notification =
            notification_for_surveillance( match.classification );
        const char character = device_config_notification_character( notification );
        const rfscout_alert_mode_t mode = alert_mode();
        ESP_LOGW( TAG,
                  "ALERT %s score=%u evidence=0x%02x audio=%s morse=%c",
                  surveillance_signature_label( match.signature ),
                  (unsigned)match.score,
                  (unsigned)match.evidence,
                  alert_mode_name( mode ),
                  character );
        platform_set_alert_led( true );
        if( !alert_request_character( character ) )
        {
            ESP_LOGW( TAG, "audible alert queue full or speaker unavailable" );
        }
        platform_set_alert_led( false );
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
    ESP_ERROR_CHECK( observation_store_init() );
    ESP_ERROR_CHECK( surveillance_history_init() );
    ESP_ERROR_CHECK( platform_init() );
    ESP_ERROR_CHECK( power_init() );

    result = alert_init();
    if( ESP_OK != result )
    {
        ESP_LOGW( TAG, "audible alerts unavailable: %s", esp_err_to_name( result ) );
    }

    result = battery_init();
    if( ESP_OK != result )
    {
        ESP_LOGW( TAG, "battery telemetry unavailable: %s", esp_err_to_name( result ) );
    }

    result = storage_init();
    if( ESP_OK != result )
    {
        ESP_LOGW( TAG, "SD storage unavailable: %s", esp_err_to_name( result ) );
    }

    result = device_config_init();
    if( ESP_OK != result )
    {
        ESP_LOGW( TAG,
                  "configuration persistence unavailable; using runtime defaults: %s",
                  esp_err_to_name( result ) );
    }
    alert_set_mode( device_config_get()->alert_mode );
    (void)alert_set_volume_percent( device_config_get()->alert_volume_percent );

    ESP_ERROR_CHECK( device_time_init() );
    ESP_ERROR_CHECK( app_manager_init() );
    display_set_brightness( device_config_get()->brightness_percent );

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

    ESP_ERROR_CHECK( radio_monitor_init( on_observation ) );
    ESP_ERROR_CHECK( ble_monitor_init( on_observation ) );
    if( RFSCOUT_APP_WIFI_BT != app_manager_current() )
    {
        (void)ble_monitor_stop();
        (void)radio_monitor_stop();
    }
    ESP_LOGI( TAG,
              "RFScout started: app-managed radio lifecycle, Wi-Fi region=%s",
              radio_monitor_region_name( radio_monitor_get_region() ) );

    while( true )
    {
        platform_poll();
        power_poll();
        battery_poll();
        observation_store_housekeeping();
        app_manager_poll();
        app_logging_poll();
        vTaskDelay( pdMS_TO_TICKS( 10 ) );
    }
}
