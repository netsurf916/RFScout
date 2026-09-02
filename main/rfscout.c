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
#include "device_config.h"
#include "device_config_recovery.h"
#include "device_time.h"
#include "display.h"
#include "display_idle.h"
#include "event.h"
#include "feedback.h"
#include "log_writer.h"
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
#include "surveillance_history.h"
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
        ESP_LOGW( TAG,
                  "ALERT %s score=%u evidence=0x%02x mode=%s morse=%c",
                  surveillance_signature_label( match.signature ),
                  (unsigned)match.score,
                  (unsigned)match.evidence,
                  alert_mode_name( alert_mode() ),
                  device_config_notification_character( notification ) );

        platform_set_alert_led( true );
        (void)notification_emit( notification );
        platform_set_alert_led( false );
        return;
    }

    /* An ordinary newly observed WiFi/BLE device gets the lightweight general
       detection cue. Classified surveillance instead uses its app-specific
       notification (for example Camera -> Morse C) so one observation does not
       produce both sounds. */
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

    ESP_ERROR_CHECK( observation_store_init() );
    ESP_ERROR_CHECK( surveillance_history_init() );
    ESP_ERROR_CHECK( platform_init() );
    ESP_ERROR_CHECK( power_init() );

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
        ESP_LOGW( TAG, "SD storage unavailable: %s", esp_err_to_name( result ) );
    }

    if( storage_is_available() )
    {
        result = device_config_recover_downgrade();
        if( ESP_OK != result )
        {
            ESP_LOGE( TAG,
                      "configuration downgrade recovery failed; preserving recovery state: %s",
                      esp_err_to_name( result ) );
            return;
        }
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

    /* Initialize reusable radio services before app selection. Neither monitor
       starts acquisition until the app manager activates WiFi/BT. */
    ESP_ERROR_CHECK( radio_monitor_init( on_observation ) );
    ESP_ERROR_CHECK( ble_monitor_init( on_observation ) );

    ESP_ERROR_CHECK( app_manager_init() );
    display_set_brightness( device_config_get()->brightness_percent );
    ESP_ERROR_CHECK( display_idle_init() );

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
        /* Display wake interception runs before semantic input processing. Applications
           continue polling/rendering normally while LCD output is disabled. */
        display_idle_poll();
        platform_poll();

        /* Refresh telemetry and evaluate protective policy before power_poll() is
           allowed to execute an expired transition. During a pending low-battery
           countdown the policy forces a fresh charger sample as an additional
           execution-time guard. */
        battery_poll();
        power_policy_poll();
        power_poll();

        observation_store_housekeeping();
        app_manager_poll();
        status_bar_poll();
        battery_indicator_poll();

        /* Presentation remains periodic for every app, including static Home. The display layer
         * skips the LCD transfer when neither framebuffer nor overlay state was invalidated. */
        (void)display_present();

        app_logging_poll();
        vTaskDelay( pdMS_TO_TICKS( 10 ) );
    }
}
