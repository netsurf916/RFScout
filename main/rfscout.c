/**
    rfscout.c : RFScout application entry point
    Description: Initializes passive RF collectors, classification, alerts, and UI.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "battery.h"
#include "ble_monitor.h"
#include "morse.h"
#include "observation_store.h"
#include "oui_database.h"
#include "platform.h"
#include "power.h"
#include "radio_monitor.h"
#include "surveillance.h"
#include "surveillance_history.h"
#include "ui.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include <string.h>

static const char *TAG = "rfscout";

static void on_observation( rf_device_t *a_observation )
{
    if( NULL == a_observation ) return;
    if( ( RF_SOURCE_BLE != a_observation->last_source ) || ( 0U == a_observation->address_type ) )
    {
        const char *vendor = oui_database_lookup( a_observation->address );
        if( NULL != vendor ) strlcpy( a_observation->vendor, vendor, sizeof( a_observation->vendor ) );
    }

    const surveillance_match_t match = surveillance_match( a_observation );
    a_observation->surveillance = match.classification;
    a_observation->surveillance_signature = match.signature;
    a_observation->surveillance_score = match.score;
    a_observation->surveillance_evidence = match.evidence;
    if( SURVEILLANCE_NONE != match.classification ) surveillance_history_observe( a_observation );

    bool is_new = false;
    if( !observation_store_observe( a_observation, &is_new ) || !is_new ) return;
    ESP_LOGI( TAG, "%s %s %02X:%02X:%02X:%02X:%02X:%02X rssi=%d ch=%u%s",
              ( RF_SOURCE_BLE == a_observation->last_source ) ? "BLE" : "WiFi",
              ( '\0' != a_observation->vendor[ 0 ] ) ? a_observation->vendor :
                  ( a_observation->has_name ? a_observation->name : "Unknown" ),
              a_observation->address[ 0 ], a_observation->address[ 1 ], a_observation->address[ 2 ],
              a_observation->address[ 3 ], a_observation->address[ 4 ], a_observation->address[ 5 ],
              a_observation->rssi, a_observation->channel,
              ( SURVEILLANCE_NONE != match.classification ) ? " SURVEILLANCE" : "" );

    if( SURVEILLANCE_NONE != match.classification )
    {
        const char letter = surveillance_morse_letter( match.classification );
        ESP_LOGW( TAG, "ALERT %s score=%u evidence=0x%02x [%c]",
                  surveillance_signature_label( match.signature ), (unsigned)match.score,
                  (unsigned)match.evidence, letter );
        platform_set_alert_led( true );
        morse_play_letter( letter, 100U, 900U, platform_audio_tone, NULL );
        platform_set_alert_led( false );
    }
}

void app_main( void )
{
    esp_err_t result = nvs_flash_init();
    if( ( ESP_ERR_NVS_NO_FREE_PAGES == result ) || ( ESP_ERR_NVS_NEW_VERSION_FOUND == result ) )
    {
        ESP_ERROR_CHECK( nvs_flash_erase() ); result = nvs_flash_init();
    }
    ESP_ERROR_CHECK( result );
    ESP_ERROR_CHECK( observation_store_init() );
    ESP_ERROR_CHECK( surveillance_history_init() );
    ESP_ERROR_CHECK( platform_init() );
    ESP_ERROR_CHECK( power_init() );
    result = battery_init();
    if( ESP_OK != result ) ESP_LOGW( TAG, "battery telemetry unavailable: %s", esp_err_to_name( result ) );
    ESP_ERROR_CHECK( ui_init() );
    ESP_ERROR_CHECK( radio_monitor_init( on_observation ) );
    ESP_ERROR_CHECK( ble_monitor_init( on_observation ) );
    ESP_LOGI( TAG, "RFScout started: Wi-Fi + BLE passive situational awareness, region=%s",
              radio_monitor_region_name( radio_monitor_get_region() ) );
    while( true )
    {
        radio_monitor_step(); platform_poll(); power_poll(); battery_poll(); ui_poll();
        vTaskDelay( pdMS_TO_TICKS( 10 ) );
    }
}
