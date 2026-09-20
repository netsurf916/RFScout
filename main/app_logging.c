/**
    app_logging.c : RFScout per-application SD logging coordinator
    Description: Gives each application lifecycle and rate-limited telemetry records.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "app_logging.h"

#include "app_manager.h"
#include "cc1101.h"
#include "cc1101_activity.h"
#include "log_writer.h"
#include "mlx90640.h"
#include "nrf24.h"
#include "nrf24_activity.h"
#include "recorder.h"
#include "system_events.h"

#include "esp_timer.h"

#define APP_LOG_INTERVAL_US 1000000LL

static rfscout_app_id_t last_app;
static int64_t last_record_us;
static bool initialized;

static const char *app_name( rfscout_app_id_t a_app )
{
    static const char *names[ RFSCOUT_APP_COUNT ] = {
        [RFSCOUT_APP_SYSTEM_EVENTS] = "SYSTEM_EVENTS",
        [RFSCOUT_APP_OTA] = "OTA",
        [RFSCOUT_APP_DEVICE] = "DEVICE",
        [RFSCOUT_APP_WIFI_BT] = "WIFI_BT",
        [RFSCOUT_APP_CC1101] = "CC1101",
        [RFSCOUT_APP_NRF24] = "NRF24",
        [RFSCOUT_APP_AUDIO] = "RECORDER",
        [RFSCOUT_APP_THERMAL] = "THERMAL",
        [RFSCOUT_APP_AUDIO_SPECTRUM] = "AUDIO_SPECTRUM",
    };

    return ( a_app < RFSCOUT_APP_COUNT ) ? names[ a_app ] : "UNKNOWN";
}

static void write_lifecycle_record( rfscout_app_id_t a_app, const char *a_event )
{
    rfscout_log_record_t record;
    if( log_begin( &record, "app" ) && log_str( &record, "app", app_name( a_app ) ) &&
        log_str( &record, "event", a_event ) )
    {
        (void)log_end( &record );
    }
}

static void write_periodic_record( rfscout_app_id_t a_app )
{
    if( ( RFSCOUT_APP_WIFI_BT == a_app ) || ( RFSCOUT_APP_OTA == a_app ) ||
        ( RFSCOUT_APP_DEVICE == a_app ) || ( a_app >= RFSCOUT_APP_COUNT ) )
    {
        return;
    }

    rfscout_log_record_t record;
    if( !log_begin( &record, "telemetry" ) || !log_str( &record, "app", app_name( a_app ) ) )
    {
        return;
    }

    bool success = true;
    switch( a_app )
    {
        case RFSCOUT_APP_SYSTEM_EVENTS:
            success = log_uint( &record, "retained", system_events_history_count() );
            break;

        case RFSCOUT_APP_CC1101:
        {
            const cc1101_scan_config_t *config = cc1101_config();
            cc1101_fingerprint_t fingerprints[ CC1101_FINGERPRINT_MAX ];
            const size_t count =
                cc1101_activity_fingerprints( fingerprints, CC1101_FINGERPRINT_MAX );
            success = log_uint( &record, "low_khz", config->low_khz ) &&
                      log_uint( &record, "high_khz", config->high_khz ) &&
                      log_uint( &record, "step_khz", config->step_khz ) &&
                      log_uint( &record, "likely", count ) &&
                      log_bool( &record, "detected", cc1101_is_detected() );
            break;
        }

        case RFSCOUT_APP_NRF24:
        {
            const nrf24_scan_config_t *config = nrf24_config();
            nrf24_fingerprint_t fingerprints[ NRF24_FINGERPRINT_MAX ];
            const size_t count = nrf24_activity_fingerprints( fingerprints, NRF24_FINGERPRINT_MAX );
            success = log_uint( &record, "low_channel", config->low_channel ) &&
                      log_uint( &record, "high_channel", config->high_channel ) &&
                      log_uint( &record, "dwell_us", config->dwell_us ) &&
                      log_uint( &record, "likely", count ) &&
                      log_bool( &record, "detected", nrf24_is_detected() );
            break;
        }

        case RFSCOUT_APP_AUDIO:
            success = log_uint( &record, "state", recorder_state() ) &&
                      log_uint( &record, "files", recorder_file_count() ) &&
                      log_uint( &record, "bytes", recorder_recorded_bytes() );
            break;

        case RFSCOUT_APP_AUDIO_SPECTRUM:
            success = log_uint( &record, "state", recorder_state() ) &&
                      log_uint( &record, "range_db", recorder_monitor_range_db() );
            break;

        case RFSCOUT_APP_THERMAL:
            success = log_bool( &record, "detected", mlx90640_is_detected() );
            break;

        default:
            success = false;
            break;
    }

    if( success )
    {
        (void)log_end( &record );
    }
}

void app_logging_init( void )
{
    last_app = app_manager_current();
    last_record_us = 0;
    initialized = true;
    write_lifecycle_record( last_app, "enter" );
}

void app_logging_poll( void )
{
    if( !initialized )
    {
        return;
    }

    const rfscout_app_id_t current = app_manager_current();
    const int64_t now = esp_timer_get_time();

    if( current != last_app )
    {
        write_lifecycle_record( last_app, "exit" );
        write_lifecycle_record( current, "enter" );
        last_app = current;
        last_record_us = 0;
    }

    if( ( 0 == last_record_us ) || ( ( now - last_record_us ) >= APP_LOG_INTERVAL_US ) )
    {
        last_record_us = now;
        write_periodic_record( current );
    }
}
