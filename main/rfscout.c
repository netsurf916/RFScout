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
#include "breadcrumbs.h"
#include "device_config.h"
#include "device_time.h"
#include "display.h"
#include "display_idle.h"
#include "event.h"
#include "feedback.h"
#include "firmware_identity.h"
#include "gps.h"
#include "input.h"
#include "log_writer.h"
#include "media_store.h"
#include "notification.h"
#include "notification_preferences.h"
#include "observation_store.h"
#include "ota_boot.h"
#include "oui_database.h"
#include "platform.h"
#include "power.h"
#include "system_lifecycle.h"
#include "power_policy.h"
#include "radio_monitor.h"
#include "status_bar.h"
#include "storage.h"
#include "surveillance.h"
#include "system_events.h"
#include "ui_theme.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"

#include <stdio.h>
#include <string.h>

#define CONFIG_SAVE_RETRY_US 1000000LL

static const char *TAG = "rfscout";
static bool gps_fix_state_known;
static bool gps_fix_active;
static int64_t config_save_retry_after_us;

static void log_running_image_hash( void )
{
    uint8_t digest[ RFSCOUT_FIRMWARE_SHA256_LENGTH ];
    char digest_hex[ RFSCOUT_FIRMWARE_SHA256_HEX_LENGTH + 1U ];
    size_t image_length = 0U;

    const esp_err_t result = firmware_running_image_sha256( digest, &image_length );
    if( ESP_OK != result )
    {
        ESP_LOGW( TAG, "could not hash running application image: %s", esp_err_to_name( result ) );
        return;
    }

    firmware_sha256_hex( digest, digest_hex );
    ESP_LOGI(
        TAG, "running application image: bytes=%u sha256=%s", (unsigned)image_length, digest_hex );
    memset( digest, 0, sizeof( digest ) );
    memset( digest_hex, 0, sizeof( digest_hex ) );
}

static void log_gps_fix( const rfscout_gps_snapshot_t *a_gps, const char *a_fix )
{
    if( NULL == a_gps )
    {
        return;
    }

    rfscout_log_record_t record;
    if( log_begin( &record, "gps" ) && log_str( &record, "fix", a_fix ) &&
        log_uint( &record, "satellites", a_gps->satellites ) )
    {
        (void)log_end( &record );
    }
}

static void log_gps_fix_lost( const rfscout_gps_snapshot_t *a_gps )
{
    if( NULL == a_gps )
    {
        return;
    }

    rfscout_log_record_t record;
    if( log_begin( &record, "gps" ) && log_str( &record, "fix", "lost" ) &&
        log_double( &record, "last_lat", a_gps->latitude_deg ) &&
        log_double( &record, "last_lon", a_gps->longitude_deg ) &&
        log_uint( &record, "age_ms", a_gps->location_age_ms ) &&
        log_uint( &record, "satellites", a_gps->satellites ) )
    {
        (void)log_end( &record );
    }
}

static void log_gps_detected_without_fix( void )
{
    rfscout_log_record_t record;
    if( log_begin( &record, "gps" ) && log_str( &record, "receiver", "detected" ) &&
        log_str( &record, "fix", "none" ) )
    {
        (void)log_end( &record );
    }
}

static void gps_state_poll( void )
{
    rfscout_gps_snapshot_t gps;
    if( !gps_snapshot_get( &gps ) )
    {
        return;
    }

    const bool fix_active = gps.location_valid;
    if( !gps_fix_state_known )
    {
        gps_fix_state_known = true;
        gps_fix_active = fix_active;
        if( fix_active )
        {
            ESP_LOGI( TAG,
                      "GPS detected; fix acquired lat=%.6f lon=%.6f sats=%u",
                      gps.latitude_deg,
                      gps.longitude_deg,
                      (unsigned)gps.satellites );
            log_gps_fix( &gps, "acquired" );
        }
        else
        {
            ESP_LOGI( TAG, "GPS detected; waiting for location fix" );
            log_gps_detected_without_fix();
        }
        return;
    }

    if( fix_active == gps_fix_active )
    {
        return;
    }

    gps_fix_active = fix_active;
    if( fix_active )
    {
        ESP_LOGI( TAG,
                  "GPS fix acquired lat=%.6f lon=%.6f sats=%u",
                  gps.latitude_deg,
                  gps.longitude_deg,
                  (unsigned)gps.satellites );
        log_gps_fix( &gps, "acquired" );
    }
    else
    {
        ESP_LOGW( TAG,
                  "GPS fix lost last_lat=%.6f last_lon=%.6f age_ms=%lu sats=%u",
                  gps.latitude_deg,
                  gps.longitude_deg,
                  (unsigned long)gps.location_age_ms,
                  (unsigned)gps.satellites );
        log_gps_fix_lost( &gps );
    }
}

static void persist_config_if_due( void )
{
    const int64_t now_us = esp_timer_get_time();
    if( now_us < config_save_retry_after_us )
    {
        return;
    }

    const esp_err_t result = device_config_save_if_dirty();
    if( ESP_OK == result )
    {
        config_save_retry_after_us = 0;
        return;
    }

    config_save_retry_after_us = now_us + CONFIG_SAVE_RETRY_US;
    ESP_LOGW( TAG,
              "configuration persistence failed; retrying in 1 second: %s",
              esp_err_to_name( result ) );
}

static void log_observation_event( observation_store_event_t a_event,
                                   const rf_device_t *a_device,
                                   void *a_context )
{
    (void)a_context;
    if( NULL == a_device )
    {
        return;
    }

    const char *event;
    const char *reason = NULL;
    switch( a_event )
    {
        case OBSERVATION_STORE_EVENT_ADDED:
            event = "added";
            break;
        case OBSERVATION_STORE_EVENT_SURVEILLANCE_CHANGED:
            event = "surveillance";
            break;
        case OBSERVATION_STORE_EVENT_DROPPED_CAPACITY:
            event = "dropped";
            reason = "capacity";
            break;
        default:
            event = "dropped";
            reason = "timeout";
            break;
    }

    const char *source = ( RF_SOURCE_WIFI == a_device->last_source )  ? "wifi"
                         : ( RF_SOURCE_BLE == a_device->last_source ) ? "ble"
                                                                      : "rf";
    rfscout_log_record_t record;
    if( !log_begin( &record, "device" ) )
    {
        return;
    }

    bool success = log_str( &record, "event", event ) && log_str( &record, "source", source ) &&
                   log_hex( &record, "mac", a_device->address, sizeof( a_device->address ) ) &&
                   log_bool( &record, "randomized", rf_device_address_is_randomized( a_device ) ) &&
                   log_str( &record, "vendor", a_device->vendor ) &&
                   ( !a_device->has_name || log_str( &record, "name", a_device->name ) );

    if( success && ( OBSERVATION_STORE_EVENT_ADDED == a_event ||
                     OBSERVATION_STORE_EVENT_SURVEILLANCE_CHANGED == a_event ) )
    {
        success = log_int( &record, "rssi", a_device->rssi ) &&
                  log_uint( &record, "channel", a_device->channel );
        if( success && ( SURVEILLANCE_SIGNATURE_NONE != a_device->surveillance_signature ) )
        {
            success = log_str( &record,
                               "surveillance",
                               surveillance_signature_label( a_device->surveillance_signature ) );
        }
        if( success && ( 0U != a_device->surveillance_evidence ) )
        {
            success = log_uint( &record, "evidence", a_device->surveillance_evidence );
        }
    }
    else if( success )
    {
        success = log_str( &record, "reason", reason );
        if( success && ( OBSERVATION_STORE_EVENT_DROPPED_TIMEOUT == a_event ) )
        {
            success =
                log_uint(
                    &record, "lifetime_ms", a_device->last_seen_ms - a_device->first_seen_ms ) &&
                log_uint( &record, "evidence_count", surveillance_evidence_count( a_device ) ) &&
                log_uint( &record, "score", a_device->surveillance_score );
            if( success &&
                ( SURVEILLANCE_SIGNATURE_NONE != a_device->surveillance_case_signature ) )
            {
                success = log_str(
                    &record,
                    "case_signature",
                    surveillance_signature_label( a_device->surveillance_case_signature ) );
            }
        }
    }

    if( success )
    {
        (void)log_end( &record );
    }
}

void rfscout_observation_consume( rf_device_t *a_observation )
{
    if( NULL == a_observation )
    {
        return;
    }

    const bool randomized = rf_device_address_is_randomized( a_observation );
    if( !randomized )
    {
        const char *vendor = oui_database_lookup( a_observation->address );
        if( NULL != vendor )
        {
            strlcpy( a_observation->vendor, vendor, sizeof( a_observation->vendor ) );
        }
    }

    surveillance_prepare_observation( a_observation );

    bool is_new = false;
    bool surveillance_new = false;
    if( !observation_store_observe( a_observation, &is_new, &surveillance_new ) )
    {
        return;
    }

    if( RF_SOURCE_WIFI == a_observation->last_source && a_observation->wifi_channel_state_changed )
    {
        rfscout_log_record_t record;
        if( log_begin( &record, "wifi_channel_observation" ) )
        {
            const bool logged =
                log_hex(
                    &record, "mac", a_observation->address, sizeof( a_observation->address ) ) &&
                log_bool( &record, "is_ap", a_observation->wifi_is_ap ) &&
                log_uint( &record, "ap_channel", a_observation->wifi_channel ) &&
                log_uint( &record, "rx_channel", a_observation->channel ) &&
                log_uint(
                    &record, "observed_channel_mask", a_observation->wifi_observed_channel_mask );
            if( logged )
            {
                (void)log_end( &record );
            }
        }
    }

    if( surveillance_new )
    {
        const rfscout_notification_type_t notification =
            notification_for_surveillance( a_observation->surveillance );
        ESP_LOGW( TAG,
                  "ALERT %s evidence=0x%02x source=%s name=\"%s\" company=0x%04x morse=%c",
                  surveillance_signature_label( a_observation->surveillance_signature ),
                  (unsigned)a_observation->surveillance_evidence,
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

    const char *source_name = ( RF_SOURCE_BLE == a_observation->last_source ) ? "BLE" : "WIFI";

    ESP_LOGI( TAG,
              "source=%s mac=%02X:%02X:%02X:%02X:%02X:%02X randomized=%u vendor=\"%s\" "
              "rssi=%d ch=%u surveillance=%u",
              source_name,
              a_observation->address[ 0 ],
              a_observation->address[ 1 ],
              a_observation->address[ 2 ],
              a_observation->address[ 3 ],
              a_observation->address[ 4 ],
              a_observation->address[ 5 ],
              randomized ? 1U : 0U,
              a_observation->vendor,
              a_observation->rssi,
              a_observation->channel,
              ( SURVEILLANCE_NONE != a_observation->surveillance ) ? 1U : 0U );

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
    ESP_LOGI( TAG, "reset reason: %d", (int)esp_reset_reason() );
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
        const esp_err_t rollback_result = ota_boot_rollback_if_pending();
        if( ESP_OK != rollback_result )
        {
            ESP_LOGE( TAG,
                      "could not roll back pending OTA image after platform failure: %s",
                      esp_err_to_name( rollback_result ) );
        }

        ESP_LOGE(
            TAG,
            "unsupported hardware profile; leaving ESP32-S3 awake and idle for recovery flashing" );
        while( true )
        {
            vTaskDelay( pdMS_TO_TICKS( 1000U ) );
        }
    }

    log_running_image_hash();

    ESP_ERROR_CHECK( observation_store_init() );
    observation_store_set_event_callback( log_observation_event, NULL );
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
        ESP_LOGW( TAG, "SD storage unavailable: %s", esp_err_to_name( result ) );
    }
    storage_dispatch_events();

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
    result = breadcrumbs_init();
    if( ( ESP_OK != result ) && ( ESP_ERR_NOT_SUPPORTED != result ) &&
        ( ESP_ERR_INVALID_STATE != result ) )
    {
        ESP_LOGW( TAG, "breadcrumb recorder unavailable: %s", esp_err_to_name( result ) );
    }

    ESP_ERROR_CHECK( app_manager_init() );
    /* App discovery probes nRF24 first, then leaves its shared pins inactive for GPS. */
    result = gps_init();
    if( ( ESP_OK != result ) && ( ESP_ERR_NOT_SUPPORTED != result ) )
    {
        ESP_LOGW( TAG, "GPS service unavailable: %s", esp_err_to_name( result ) );
    }
    system_events_poll();
    display_set_brightness( device_config_get()->brightness_percent );
    ESP_ERROR_CHECK( display_idle_init() );

    /*
     * Confirm a newly installed image only after mandatory common startup has succeeded.
     * Optional peripherals/storage above are deliberately not part of OTA health.
     */
    ESP_ERROR_CHECK( ota_boot_confirm_if_pending() );
    system_events_enable_notifications();

    if( media_store_available() && device_config_get()->logging_enabled )
    {
        result = log_writer_init();
        if( ESP_OK != result )
        {
            ESP_LOGW( TAG, "SD logging unavailable: %s", esp_err_to_name( result ) );
        }
        else
        {
            app_logging_init();
        }
    }

    config_save_retry_after_us = 0;
    ESP_LOGI( TAG,
              "RFScout started: app-managed radio lifecycle, passive Wi-Fi channels=1-%u",
              (unsigned)RFSCOUT_WIFI_CHANNEL_MAX );

    while( true )
    {
        /* Commit worker-requested service disables before any terminal power transition. */
        log_writer_process_disable_request();
        input_poll();
        display_idle_poll();
        gps_poll();
        gps_state_poll();
        breadcrumbs_poll();

        battery_poll();
        power_policy_poll();
        if( RFSCOUT_BOARD_USER_KEY_PIN >= 0 )
        {
            power_poll();
        }

        observation_store_housekeeping();
        app_manager_poll();
        storage_dispatch_events();
        system_lifecycle_poll();
        system_events_poll();
        status_bar_poll();
        battery_indicator_poll();

        /* Setting changes are persisted immediately in the normal case. If persistence fails,
           retain the dirty record but retry at a bounded cadence rather than hammering flash. */
        persist_config_if_due();

        (void)display_present();

        app_logging_poll();
        vTaskDelay( pdMS_TO_TICKS( 10 ) );
    }
}
